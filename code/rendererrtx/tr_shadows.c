/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
#include "tr_local.h"

#ifdef USE_VULKAN
uint32_t VK_PushUniform( const vkUniform_t *uniform );
#endif


/*

  for a projection shadow:

  point[x] += light vector * ( z - shadow plane )
  point[y] +=
  point[z] = shadow plane

  1 0 light[x] / light[z]

*/

typedef struct {
	int		i2;
	int		facing;
} edgeDef_t;

#define	MAX_EDGE_DEFS	32

static	edgeDef_t	edgeDefs[SHADER_MAX_VERTEXES][MAX_EDGE_DEFS];
static	int			numEdgeDefs[SHADER_MAX_VERTEXES];
static	int			facing[SHADER_MAX_INDEXES/3];

static float R_EnemyHighlightScale( float scale, float fallbackScale ) {
	if ( scale <= 1.0f ) {
		scale = fallbackScale;
	}

	if ( scale < 1.001f ) {
		scale = 1.001f;
	} else if ( scale > 1.25f ) {
		scale = 1.25f;
	}

	return scale;
}


static float R_EnemyHighlightOffset( float scale, float fallbackScale ) {
	scale = R_EnemyHighlightScale( scale, fallbackScale );

	/*
	Scaling around the entity origin drifts badly on segmented player models
	because the MD3 pivots are not centered on the rendered surface. Expand
	along the tessellated normals instead and derive the shell width from the
	existing scale factor.
	*/
	return ( scale - 1.0f ) * 32.0f;
}


static float R_EnemyHighlightRimFactor( const vec4_t position, const vec3_t normal ) {
	vec3_t viewer;
	float facingDot;
	float rim;

	VectorSubtract( backEnd.or.viewOrigin, position, viewer );
	if ( VectorNormalize( viewer ) <= 0.0f ) {
		return 0.0f;
	}

	facingDot = DotProduct( normal, viewer );
	if ( facingDot < 0.0f ) {
		facingDot = -facingDot;
	}

	rim = 1.0f - facingDot;
	rim = ( rim - 0.10f ) * ( 1.0f / 0.90f );

	if ( rim <= 0.0f ) {
		return 0.0f;
	}

	if ( rim >= 1.0f ) {
		return 1.0f;
	}

	return rim * rim;
}


static void R_EnemyHighlightExpand( vec4_t *originalXYZ, float offset ) {
	int i;
	vec3_t normal;

	for ( i = 0; i < tess.numVertexes; i++ ) {
		Vector4Copy( tess.xyz[i], originalXYZ[i] );
		VectorCopy( tess.normal[i], normal );
		VectorNormalizeFast( normal );
		VectorMA( originalXYZ[i], offset, normal, tess.xyz[i] );
	}
}


static void R_EnemyHighlightRestore( const vec4_t *originalXYZ ) {
	Com_Memcpy( tess.xyz, originalXYZ, tess.numVertexes * sizeof( tess.xyz[0] ) );
}


#ifdef USE_VULKAN
static uint32_t R_EnemyHighlightPipeline( unsigned int stateBits, cullType_t cull, Vk_Shadow_Phase shadowPhase, Vk_Shader_Type shaderType ) {
	Vk_Pipeline_Def def;

	Com_Memset( &def, 0, sizeof( def ) );
	def.shader_type = shaderType;
	def.state_bits = stateBits;
	def.face_culling = cull;
	def.mirror = R_ViewPassIsMirror( &backEnd.viewParms );
	def.shadow_phase = shadowPhase;

	return vk_find_pipeline_ext( 0, &def, qfalse );
}


static void R_EnemyHighlightBindTexture( void ) {
	GL_SelectTexture( 0 );
	GL_Bind( tr.whiteImage );
	tess.svars.texcoordPtr[0] = tess.texCoords[0];
}


int R_CelBandCount( void ) {
	int bands = 4;

	if ( r_celShadingSteps ) {
		bands = r_celShadingSteps->integer;
	}

	return Com_Clamp( 2, 8, bands );
}


static float R_CelQuantizeUnitValue( float incoming ) {
	float scaled;
	float denom;

	if ( incoming <= 0.0f ) {
		return 0.0f;
	}

	if ( incoming >= 1.0f ) {
		return 1.0f;
	}

	denom = (float)R_CelBandCount() - 1.0f;
	if ( denom <= 0.0f ) {
		return incoming;
	}

	scaled = floorf( incoming * denom + 0.5f );
	if ( scaled < 0.0f ) {
		scaled = 0.0f;
	} else if ( scaled > denom ) {
		scaled = denom;
	}

	return scaled / denom;
}


qboolean R_CelShadingActive( const trRefEntity_t *ent ) {
	if ( !ent || !r_celShading || !r_celShading->integer ) {
		return qfalse;
	}

	if ( ent == &tr.worldEntity ) {
		return qfalse;
	}

	if ( ent->e.reType != RT_MODEL ) {
		return qfalse;
	}

	if ( ent->e.renderfx & RF_FIRST_PERSON ) {
		return r_celViewWeapon && r_celViewWeapon->integer;
	}

	return qtrue;
}


qboolean R_CelShadingWorldActive( void ) {
	return r_celShadingWorld && r_celShadingWorld->integer;
}


qboolean R_CelOutlineActive( const trRefEntity_t *ent, const shader_t *shader ) {
	if ( !R_CelShadingActive( ent ) || !r_celOutline || !r_celOutline->integer ) {
		return qfalse;
	}

	if ( shader == tr.projectionShadowShader ) {
		return qfalse;
	}

	if ( !shader || shader->sort > SS_OPAQUE ) {
		return qfalse;
	}

	if ( glConfig.stencilBits <= 0 ) {
		return qfalse;
	}

	return qtrue;
}

qboolean R_BloomProtectHighlightsActive( void ) {
	if ( backEnd.bloomProtectHighlights ) {
		return qtrue;
	}

	if ( R_CelShadingWorldActive() ) {
		return qtrue;
	}

	return r_celShading && r_celShading->integer &&
		r_celOutline && r_celOutline->integer &&
		glConfig.stencilBits > 0;
}


void R_CelQuantizeModelLighting( const trRefEntity_t *ent, byte color[4] ) {
	float maxChannel;
	float quantized;
	float scale;
	int i;

	if ( !color || !R_CelShadingActive( ent ) ||
		!r_celShadingModelShadows || !r_celShadingModelShadows->integer ) {
		return;
	}

	maxChannel = (float)color[0];
	if ( color[1] > maxChannel ) {
		maxChannel = (float)color[1];
	}
	if ( color[2] > maxChannel ) {
		maxChannel = (float)color[2];
	}

	if ( maxChannel <= 0.0f ) {
		return;
	}

	quantized = R_CelQuantizeUnitValue( maxChannel / 255.0f ) * 255.0f;
	scale = quantized / maxChannel;

	for ( i = 0; i < 3; i++ ) {
		int value = myftol( color[i] * scale );

		if ( value < 0 ) {
			value = 0;
		} else if ( value > 255 ) {
			value = 255;
		}

		color[i] = (byte)value;
	}
}


static float R_CelOutlineOffset( const trRefEntity_t *ent ) {
	if ( ent && ( ent->e.renderfx & RF_FIRST_PERSON ) ) {
		float scale = r_celViewWeaponOutlineScale ? r_celViewWeaponOutlineScale->value : 1.003f;

		return R_EnemyHighlightOffset( scale, 1.003f );
	}

	return R_EnemyHighlightOffset( r_celOutlineScale ? r_celOutlineScale->value : 1.02f, 1.02f );
}


static byte R_CelOutlineAlpha( const trRefEntity_t *ent, byte colorAlpha ) {
	cvar_t *alphaCvar;
	float alpha;
	int value;

	alphaCvar = ( ent && ( ent->e.renderfx & RF_FIRST_PERSON ) ) ?
		r_celViewWeaponOutlineAlpha : r_celOutlineAlpha;
	alpha = alphaCvar ? Com_Clamp( 0.0f, 1.0f, alphaCvar->value ) : 1.0f;
	value = myftol( (float)colorAlpha * alpha );
	if ( value < 0 ) {
		value = 0;
	} else if ( value > 255 ) {
		value = 255;
	}

	return (byte)value;
}


static void R_GetCelOutlineColor( const trRefEntity_t *ent, color4ub_t *outColor ) {
	static color4ub_t cachedColor = { { 0, 0, 0, 255 } };
	static qboolean initialized = qfalse;
	char buffer[MAX_CVAR_VALUE_STRING];
	char *parts[4];
	int count;
	int i;

	if ( !outColor ) {
		return;
	}

	if ( initialized && ( !r_celOutlineColor || !r_celOutlineColor->modified ) ) {
		*outColor = cachedColor;
		outColor->rgba[3] = R_CelOutlineAlpha( ent, outColor->rgba[3] );
		return;
	}

	initialized = qtrue;
	if ( r_celOutlineColor ) {
		r_celOutlineColor->modified = qfalse;
	}

	if ( !r_celOutlineColor || !r_celOutlineColor->string[0] ) {
		*outColor = cachedColor;
		outColor->rgba[3] = R_CelOutlineAlpha( ent, outColor->rgba[3] );
		return;
	}

	Q_strncpyz( buffer, r_celOutlineColor->string, sizeof( buffer ) );
	count = Com_Split( buffer, parts, 4, ' ' );
	if ( count < 3 ) {
		*outColor = cachedColor;
		return;
	}

	for ( i = 0; i < count && i < 4; i++ ) {
		float value = Q_atof( parts[i] );

		if ( value < 0.0f ) {
			value = 0.0f;
		} else if ( value > 255.0f ) {
			value = 255.0f;
		}

		cachedColor.rgba[i] = (byte)( value + 0.5f );
	}

	if ( count < 4 ) {
		cachedColor.rgba[3] = 255;
	}

	*outColor = cachedColor;
	outColor->rgba[3] = R_CelOutlineAlpha( ent, outColor->rgba[3] );
}


static void R_BindOutlineResources( const color4ub_t *color ) {
	vkUniform_t uniform;

	Com_Memset( &uniform, 0, sizeof( uniform ) );
	uniform.ent.color[0][0] = color->rgba[0] / 255.0f;
	uniform.ent.color[0][1] = color->rgba[1] / 255.0f;
	uniform.ent.color[0][2] = color->rgba[2] / 255.0f;
	uniform.ent.color[0][3] = color->rgba[3] / 255.0f;
	uniform.texFactors[0] = 1.0f;
	uniform.texFactors[1] = 1.0f;
	uniform.texFactors[2] = 1.0f;
	uniform.texFactors[3] = 1.0f;

	R_EnemyHighlightBindTexture();
	VK_PushUniform( &uniform );
}


static void R_EnemyHighlightBindOutlineResources( void ) {
	R_BindOutlineResources( &backEnd.currentEntity->e.shader );
}


static void R_EnemyHighlightColor( const color4ub_t *color ) {
	int i;
	vec3_t normal;

	for ( i = 0; i < tess.numVertexes; i++ ) {
		float rim;

		VectorCopy( tess.normal[i], normal );
		VectorNormalizeFast( normal );

		rim = R_EnemyHighlightRimFactor( tess.xyz[i], normal );

		tess.svars.colors[0][i].rgba[0] = color->rgba[0];
		tess.svars.colors[0][i].rgba[1] = color->rgba[1];
		tess.svars.colors[0][i].rgba[2] = color->rgba[2];
		tess.svars.colors[0][i].rgba[3] = (byte)( color->rgba[3] * rim + 0.5f );
	}
}


void RB_EnemyRimTessEnd( void ) {
	if ( !backEnd.currentEntity ) {
		return;
	}

	backEnd.bloomProtectHighlights = qtrue;

	R_EnemyHighlightColor( &backEnd.currentEntity->e.shader );
	R_EnemyHighlightBindTexture();

	vk_bind_pipeline( R_EnemyHighlightPipeline( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE, CT_FRONT_SIDED, SHADOW_DISABLED, TYPE_SIGNLE_TEXTURE ) );
	vk_bind_index();
	vk_bind_geometry( TESS_XYZ | TESS_RGBA0 | TESS_ST0 );
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );
}


void RB_EnemyOutlineTessEnd( void ) {
	vec4_t originalXYZ[SHADER_MAX_VERTEXES];
	float offset;

	if ( !backEnd.currentEntity || glConfig.stencilBits <= 0 ) {
		return;
	}

	backEnd.bloomProtectHighlights = qtrue;

	offset = R_EnemyHighlightOffset( backEnd.currentEntity->e.shaderTexCoord[0], 1.01f );
	R_EnemyHighlightBindOutlineResources();

	vk_bind_pipeline( R_EnemyHighlightPipeline( GLS_DEPTHFUNC_EQUAL, CT_FRONT_SIDED, SHADOW_OUTLINE_MASK, TYPE_SIGNLE_TEXTURE_ENT_COLOR ) );
	vk_bind_index();
	vk_bind_geometry( TESS_XYZ | TESS_ST0 );
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );

	R_EnemyHighlightExpand( originalXYZ, offset );
	vk_bind_pipeline( R_EnemyHighlightPipeline( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA, CT_BACK_SIDED, SHADOW_OUTLINE_SHELL, TYPE_SIGNLE_TEXTURE_ENT_COLOR ) );
	vk_bind_geometry( TESS_XYZ | TESS_ST0 );
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );

	R_EnemyHighlightRestore( originalXYZ );
	vk_bind_pipeline( R_EnemyHighlightPipeline( GLS_DEPTHFUNC_EQUAL, CT_FRONT_SIDED, SHADOW_OUTLINE_CLEAR, TYPE_SIGNLE_TEXTURE_ENT_COLOR ) );
	vk_bind_geometry( TESS_XYZ | TESS_ST0 );
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );
}


void RB_CelOutlineTessEnd( void ) {
	vec4_t originalXYZ[SHADER_MAX_VERTEXES];
	color4ub_t outlineColor;
	float offset;

	if ( !R_CelOutlineActive( backEnd.currentEntity, tess.shader ) ) {
		return;
	}

	offset = R_CelOutlineOffset( backEnd.currentEntity );
	R_GetCelOutlineColor( backEnd.currentEntity, &outlineColor );
	if ( outlineColor.rgba[3] == 0 ) {
		return;
	}
	R_BindOutlineResources( &outlineColor );

	vk_bind_pipeline( R_EnemyHighlightPipeline( GLS_DEPTHFUNC_EQUAL, CT_FRONT_SIDED, SHADOW_OUTLINE_MASK, TYPE_SIGNLE_TEXTURE_ENT_COLOR ) );
	vk_bind_index();
	vk_bind_geometry( TESS_XYZ | TESS_ST0 );
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );

	R_EnemyHighlightExpand( originalXYZ, offset );
	vk_bind_pipeline( R_EnemyHighlightPipeline( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA, CT_BACK_SIDED, SHADOW_OUTLINE_SHELL, TYPE_SIGNLE_TEXTURE_ENT_COLOR ) );
	vk_bind_geometry( TESS_XYZ | TESS_ST0 );
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );

	R_EnemyHighlightRestore( originalXYZ );
	vk_bind_pipeline( R_EnemyHighlightPipeline( GLS_DEPTHFUNC_EQUAL, CT_FRONT_SIDED, SHADOW_OUTLINE_CLEAR, TYPE_SIGNLE_TEXTURE_ENT_COLOR ) );
	vk_bind_geometry( TESS_XYZ | TESS_ST0 );
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );
}

#endif

static void R_AddEdgeDef( int i1, int i2, int f ) {
	int		c;

	c = numEdgeDefs[ i1 ];
	if ( c == MAX_EDGE_DEFS ) {
		return;		// overflow
	}
	edgeDefs[ i1 ][ c ].i2 = i2;
	edgeDefs[ i1 ][ c ].facing = f;

	numEdgeDefs[ i1 ]++;
}


static void R_CalcShadowEdges( void ) {
	qboolean sil_edge;
	int		i;
	int		c, c2;
	int		j, k;
	int		i2;
	color4ub_t *colors;

	tess.numIndexes = 0;

	// an edge is NOT a silhouette edge if its face doesn't face the light,
	// or if it has a reverse paired edge that also faces the light.
	// A well behaved polyhedron would have exactly two faces for each edge,
	// but lots of models have dangling edges or overfanned edges
	for ( i = 0; i < tess.numVertexes; i++ ) {
		c = numEdgeDefs[ i ];
		for ( j = 0 ; j < c ; j++ ) {
			if ( !edgeDefs[ i ][ j ].facing ) {
				continue;
			}

			sil_edge = qtrue;
			i2 = edgeDefs[ i ][ j ].i2;
			c2 = numEdgeDefs[ i2 ];
			for ( k = 0 ; k < c2 ; k++ ) {
				if ( edgeDefs[ i2 ][ k ].i2 == i && edgeDefs[ i2 ][ k ].facing ) {
					sil_edge = qfalse;
					break;
				}
			}

			// if it doesn't share the edge with another front facing
			// triangle, it is a sil edge
			if ( sil_edge ) {
				if ( tess.numIndexes > ARRAY_LEN( tess.indexes ) - 6 ) {
					i = tess.numVertexes;
					break;
				}
#ifdef USE_VULKAN
				tess.indexes[ tess.numIndexes + 0 ] = i;
				tess.indexes[ tess.numIndexes + 1 ] = i2;
				tess.indexes[ tess.numIndexes + 2 ] = i + tess.numVertexes;
				tess.indexes[ tess.numIndexes + 3 ] = i2;
				tess.indexes[ tess.numIndexes + 4 ] = i2 + tess.numVertexes;
				tess.indexes[ tess.numIndexes + 5 ] = i + tess.numVertexes;
#else
				tess.indexes[ tess.numIndexes + 0 ] = i;
				tess.indexes[ tess.numIndexes + 1 ] = i + tess.numVertexes;
				tess.indexes[ tess.numIndexes + 2 ] = i2;
				tess.indexes[ tess.numIndexes + 3 ] = i2;
				tess.indexes[ tess.numIndexes + 4 ] = i + tess.numVertexes;
				tess.indexes[ tess.numIndexes + 5 ] = i2 + tess.numVertexes;
#endif
				tess.numIndexes += 6;
			}
		}
	}

#ifdef USE_VULKAN
	tess.numVertexes *= 2;

	colors = &tess.svars.colors[0][0]; // we need at least 2x SHADER_MAX_VERTEXES there

	for ( i = 0; i < tess.numVertexes; i++ ) {
		Vector4Set( colors[i].rgba, 50, 50, 50, 255 );
	}
#endif
}


/*
=================
RB_ShadowTessEnd

triangleFromEdge[ v1 ][ v2 ]


  set triangle from edge( v1, v2, tri )
  if ( facing[ triangleFromEdge[ v1 ][ v2 ] ] && !facing[ triangleFromEdge[ v2 ][ v1 ] ) {
  }
=================
*/
void RB_ShadowTessEnd( void ) {
	int		i;
	int		numTris;
	vec3_t	lightDir;
#ifdef USE_VULKAN
	uint32_t pipeline[2];
#else
	GLboolean rgba[4];
#endif

	if ( glConfig.stencilBits < 4 ) {
		return;
	}

#ifdef USE_PMLIGHT
	if ( r_dlightMode->integer == 2 && r_shadows->integer == 2 )
		VectorCopy( backEnd.currentEntity->shadowLightDir, lightDir );
	else
#endif
		VectorCopy( backEnd.currentEntity->lightDir, lightDir );

	// clamp projection by height
	if ( lightDir[2] > 0.1 ) {
		float s = 0.1 / lightDir[2];
		VectorScale( lightDir, s, lightDir );
	}

	// project vertexes away from light direction
	for ( i = 0; i < tess.numVertexes; i++ ) {
		VectorMA( tess.xyz[i], -512, lightDir, tess.xyz[i+tess.numVertexes] );
	}

	// decide which triangles face the light
	Com_Memset( numEdgeDefs, 0, tess.numVertexes * sizeof( numEdgeDefs[0] ) );

	numTris = tess.numIndexes / 3;
	for ( i = 0 ; i < numTris ; i++ ) {
		int		i1, i2, i3;
		vec3_t	d1, d2, normal;
		float	*v1, *v2, *v3;
		float	d;

		i1 = tess.indexes[ i*3 + 0 ];
		i2 = tess.indexes[ i*3 + 1 ];
		i3 = tess.indexes[ i*3 + 2 ];

		v1 = tess.xyz[ i1 ];
		v2 = tess.xyz[ i2 ];
		v3 = tess.xyz[ i3 ];

		VectorSubtract( v2, v1, d1 );
		VectorSubtract( v3, v1, d2 );
		CrossProduct( d1, d2, normal );

		d = DotProduct( normal, lightDir );
		if ( d > 0 ) {
			facing[ i ] = 1;
		} else {
			facing[ i ] = 0;
		}

		// create the edges
		R_AddEdgeDef( i1, i2, facing[ i ] );
		R_AddEdgeDef( i2, i3, facing[ i ] );
		R_AddEdgeDef( i3, i1, facing[ i ] );
	}

	R_CalcShadowEdges();

	// draw the silhouette edges
#ifdef USE_VULKAN
	GL_Bind( tr.whiteImage );

	// mirrors have the culling order reversed
	if ( backEnd.viewParms.portalView == PV_MIRROR ) {
		pipeline[0] = vk.shadow_volume_pipelines[0][1];
		pipeline[1] = vk.shadow_volume_pipelines[1][1];
	} else {
		pipeline[0] = vk.shadow_volume_pipelines[0][0];
		pipeline[1] = vk.shadow_volume_pipelines[1][0];

	}
	vk_bind_pipeline( pipeline[0] ); // back-sided
	vk_bind_index();
	vk_bind_geometry( TESS_XYZ | TESS_RGBA0 );
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );
	vk_bind_pipeline( pipeline[1] ); // front-sided
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );

	tess.numVertexes /= 2;
#else
	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_NONE );

	qglVertexPointer( 3, GL_FLOAT, sizeof( tess.xyz[0] ), tess.xyz );

	if ( qglLockArraysEXT )
		qglLockArraysEXT( 0, tess.numVertexes*2 );

	// draw the silhouette edges

	qglDisable( GL_TEXTURE_2D );
	//GL_Bind( tr.whiteImage );
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	qglColor4f( 0.2f, 0.2f, 0.2f, 1.0f );

	// don't write to the color buffer
	qglGetBooleanv( GL_COLOR_WRITEMASK, rgba );
	qglColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );

	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_ALWAYS, 1, 255 );

	GL_Cull( CT_BACK_SIDED );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_INCR );

	R_DrawElements( tess.numIndexes, tess.indexes );

	GL_Cull( CT_FRONT_SIDED );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_DECR );

	R_DrawElements( tess.numIndexes, tess.indexes );

	if ( qglUnlockArraysEXT )
		qglUnlockArraysEXT();

	// re-enable writing to the color buffer
	qglColorMask(rgba[0], rgba[1], rgba[2], rgba[3]);

	qglEnable( GL_TEXTURE_2D );
#endif

	backEnd.doneShadows = qtrue;

	tess.numIndexes = 0;
}


/*
=================
RB_ShadowFinish

Darken everything that is is a shadow volume.
We have to delay this until everything has been shadowed,
because otherwise shadows from different body parts would
overlap and double darken.
=================
*/
void RB_ShadowFinish( void ) {
#ifdef USE_VULKAN
	float tmp[16];
	int i;
#endif
	static const vec3_t verts[4] = {
		{ -100, 100, -10 },
		{  100, 100, -10 },
		{ -100,-100, -10 },
		{  100,-100, -10 }
	};

	if ( !backEnd.doneShadows ) {
		return;
	}

	backEnd.doneShadows = qfalse;

	if ( r_shadows->integer != 2 ) {
		return;
	}
	if ( glConfig.stencilBits < 4 ) {
		return;
	}

#ifdef USE_VULKAN
	GL_Bind( tr.whiteImage );

	for ( i = 0; i < 4; i++ )
	{
		VectorCopy( verts[i], tess.xyz[i] );
		Vector4Set( tess.svars.colors[0][i].rgba, 153, 153, 153, 255 );
	}

	tess.numVertexes = 4;

	Com_Memcpy( tmp, vk_world.modelview_transform, 64 );
	Com_Memset( vk_world.modelview_transform, 0, 64 );

	vk_world.modelview_transform[0] = 1.0f;
	vk_world.modelview_transform[5] = 1.0f;
	vk_world.modelview_transform[10] = 1.0f;
	vk_world.modelview_transform[15] = 1.0f;

	vk_bind_pipeline( vk.shadow_finish_pipeline );

	vk_update_mvp( NULL );

	vk_bind_geometry( TESS_XYZ | TESS_RGBA0 /*| TESS_ST0 */ );
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qfalse );

	Com_Memcpy( vk_world.modelview_transform, tmp, 64 );

	tess.numIndexes = 0;
	tess.numVertexes = 0;

#else
	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_NOTEQUAL, 0, 255 );

	qglDisable( GL_CLIP_PLANE0 );
	GL_Cull( CT_TWO_SIDED );

	qglDisable( GL_TEXTURE_2D );

	qglLoadIdentity();

	qglColor4f( 0.6f, 0.6f, 0.6f, 1 );
	GL_State( GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO );

	//qglColor4f( 1, 0, 0, 1 );
	//GL_State( GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );

	GL_ClientState( 0, CLS_NONE );
	qglVertexPointer( 3, GL_FLOAT, 0, verts );
	qglDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );

	qglColor4f( 1, 1, 1, 1 );
	qglDisable( GL_STENCIL_TEST );

	qglEnable( GL_TEXTURE_2D );
#endif
}


/*
=================
RB_ProjectionShadowDeform

=================
*/
void RB_ProjectionShadowDeform( void ) {
	float	*xyz;
	int		i;
	float	h;
	vec3_t	ground;
	vec3_t	light;
	float	groundDist;
	float	d;
	vec3_t	lightDir;

	xyz = ( float * ) tess.xyz;

	ground[0] = backEnd.or.axis[0][2];
	ground[1] = backEnd.or.axis[1][2];
	ground[2] = backEnd.or.axis[2][2];

	groundDist = backEnd.or.origin[2] - backEnd.currentEntity->e.shadowPlane;

#ifdef USE_PMLIGHT
	if ( r_dlightMode->integer == 2 && r_shadows->integer == 2 )
		VectorCopy( backEnd.currentEntity->shadowLightDir, lightDir );
	else
#endif
		VectorCopy( backEnd.currentEntity->lightDir, lightDir );

	d = DotProduct( lightDir, ground );
	// don't let the shadows get too long or go negative
	if ( d < 0.5 ) {
		VectorMA( lightDir, (0.5 - d), ground, lightDir );
		d = DotProduct( lightDir, ground );
	}
	d = 1.0 / d;

	light[0] = lightDir[0] * d;
	light[1] = lightDir[1] * d;
	light[2] = lightDir[2] * d;

	for ( i = 0; i < tess.numVertexes; i++, xyz += 4 ) {
		h = DotProduct( xyz, ground ) + groundDist;

		xyz[0] -= light[0] * h;
		xyz[1] -= light[1] * h;
		xyz[2] -= light[2] * h;
	}
}
