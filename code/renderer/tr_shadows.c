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
#include "tr_glx_compat.h"


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
		outColor->rgba[3] = R_CelOutlineAlpha( ent, outColor->rgba[3] );
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


static void R_EnemyHighlightColor( const color4ub_t *color ) {
	int i;
	vec3_t normal;

	for ( i = 0; i < tess.numVertexes; i++ ) {
		float rim;

		VectorCopy( tess.normal[i], normal );
		VectorNormalizeFast( normal );

		rim = R_EnemyHighlightRimFactor( tess.xyz[i], normal );

		tess.svars.colors[i].rgba[0] = color->rgba[0];
		tess.svars.colors[i].rgba[1] = color->rgba[1];
		tess.svars.colors[i].rgba[2] = color->rgba[2];
		tess.svars.colors[i].rgba[3] = (byte)( color->rgba[3] * rim + 0.5f );
	}
}


void RB_EnemyRimTessEnd( void ) {
	if ( !backEnd.currentEntity ) {
		return;
	}

	backEnd.bloomProtectHighlights = qtrue;

	GL_ProgramDisable();
	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_COLOR_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, sizeof( tess.xyz[0] ), tess.xyz );
	R_EnemyHighlightColor( &backEnd.currentEntity->e.shader );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.svars.colors[0].rgba );

	if ( qglLockArraysEXT ) {
		qglLockArraysEXT( 0, tess.numVertexes );
	}

	qglDisable( GL_TEXTURE_2D );
	GL_Cull( CT_FRONT_SIDED );
	GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE );
	R_DrawElements( tess.numIndexes, tess.indexes );
	qglEnable( GL_TEXTURE_2D );

	if ( qglUnlockArraysEXT ) {
		qglUnlockArraysEXT();
	}
}


void RB_EnemyOutlineTessEnd( void ) {
	GLboolean rgba[4];
	vec4_t originalXYZ[SHADER_MAX_VERTEXES];
	float offset;

	if ( !backEnd.currentEntity || glConfig.stencilBits <= 0 ) {
		return;
	}

	backEnd.bloomProtectHighlights = qtrue;

	offset = R_EnemyHighlightOffset( backEnd.currentEntity->e.shaderTexCoord[0], 1.01f );

	GL_ProgramDisable();
	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_NONE );
	qglVertexPointer( 3, GL_FLOAT, sizeof( tess.xyz[0] ), tess.xyz );

	qglDisable( GL_TEXTURE_2D );
	qglEnable( GL_STENCIL_TEST );
	qglStencilMask( 255 );

	GL_GetColorMask( rgba );
	GL_ColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	qglStencilFunc( GL_ALWAYS, 1, 255 );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_REPLACE );
	GL_Cull( CT_FRONT_SIDED );
	GL_State( GLS_DEPTHFUNC_EQUAL | GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE );
	R_DrawElements( tess.numIndexes, tess.indexes );

	R_EnemyHighlightExpand( originalXYZ, offset );
	qglVertexPointer( 3, GL_FLOAT, sizeof( tess.xyz[0] ), tess.xyz );
	GL_ColorMask( rgba[0], rgba[1], rgba[2], rgba[3] );
	qglStencilFunc( GL_NOTEQUAL, 1, 255 );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	GL_Cull( CT_BACK_SIDED );
	GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	R_EnemyHighlightColor( &backEnd.currentEntity->e.shader );
	GL_ClientState( 0, CLS_COLOR_ARRAY );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.svars.colors[0].rgba );
	R_DrawElements( tess.numIndexes, tess.indexes );

	R_EnemyHighlightRestore( originalXYZ );
	qglVertexPointer( 3, GL_FLOAT, sizeof( tess.xyz[0] ), tess.xyz );
	GL_ColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	qglStencilFunc( GL_ALWAYS, 0, 255 );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_ZERO );
	GL_Cull( CT_FRONT_SIDED );
	GL_State( GLS_DEPTHFUNC_EQUAL | GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE );
	GL_ClientState( 0, CLS_NONE );
	R_DrawElements( tess.numIndexes, tess.indexes );

	GL_ColorMask( rgba[0], rgba[1], rgba[2], rgba[3] );
	qglDisable( GL_STENCIL_TEST );
	qglColor4f( 1, 1, 1, 1 );
	qglEnable( GL_TEXTURE_2D );
}


void RB_CelOutlineTessEnd( void ) {
	GLboolean rgba[4];
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

	GL_ProgramDisable();
	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_NONE );
	qglVertexPointer( 3, GL_FLOAT, sizeof( tess.xyz[0] ), tess.xyz );

	qglDisable( GL_TEXTURE_2D );
	qglEnable( GL_STENCIL_TEST );
	qglStencilMask( 255 );

	GL_GetColorMask( rgba );
	GL_ColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	qglStencilFunc( GL_ALWAYS, 1, 255 );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_REPLACE );
	GL_Cull( CT_FRONT_SIDED );
	GL_State( GLS_DEPTHFUNC_EQUAL | GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE );
	R_DrawElements( tess.numIndexes, tess.indexes );

	R_EnemyHighlightExpand( originalXYZ, offset );
	qglVertexPointer( 3, GL_FLOAT, sizeof( tess.xyz[0] ), tess.xyz );
	GL_ColorMask( rgba[0], rgba[1], rgba[2], rgba[3] );
	qglStencilFunc( GL_NOTEQUAL, 1, 255 );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	GL_Cull( CT_BACK_SIDED );
	GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	qglColor4f( outlineColor.rgba[0] / 255.0f, outlineColor.rgba[1] / 255.0f,
		outlineColor.rgba[2] / 255.0f, outlineColor.rgba[3] / 255.0f );
	GL_ClientState( 0, CLS_NONE );
	R_DrawElements( tess.numIndexes, tess.indexes );

	R_EnemyHighlightRestore( originalXYZ );
	qglVertexPointer( 3, GL_FLOAT, sizeof( tess.xyz[0] ), tess.xyz );
	GL_ColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	qglStencilFunc( GL_ALWAYS, 0, 255 );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_ZERO );
	GL_Cull( CT_FRONT_SIDED );
	GL_State( GLS_DEPTHFUNC_EQUAL | GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE );
	GL_ClientState( 0, CLS_NONE );
	R_DrawElements( tess.numIndexes, tess.indexes );

	GL_ColorMask( rgba[0], rgba[1], rgba[2], rgba[3] );
	qglDisable( GL_STENCIL_TEST );
	qglColor4f( 1, 1, 1, 1 );
	qglEnable( GL_TEXTURE_2D );
}

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
				tess.indexes[ tess.numIndexes + 0 ] = i;
				tess.indexes[ tess.numIndexes + 1 ] = i + tess.numVertexes;
				tess.indexes[ tess.numIndexes + 2 ] = i2;
				tess.indexes[ tess.numIndexes + 3 ] = i2;
				tess.indexes[ tess.numIndexes + 4 ] = i + tess.numVertexes;
				tess.indexes[ tess.numIndexes + 5 ] = i2 + tess.numVertexes;
				tess.numIndexes += 6;
			}
		}
	}
}

#ifdef RENDERER_GLX
static qboolean GLX_TryStreamDrawStencilShadowVolume( void )
{
	glxStreamReservation_t reservation;
	qboolean ok = qtrue;
	int numVertexes;
	int xyzBytes;
	int indexBytes;
	int indexOffset;
	int totalBytes;
	unsigned int oldArrayBuffer = 0;
	unsigned int oldElementArrayBuffer = 0;

	if ( !GLX_CompatStreamDrawShadowsEnabled() ) {
		return qfalse;
	}
	if ( !qglBindBufferARB ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_NO_BIND_BUFFER );
		return qfalse;
	}
	if ( tess.numVertexes <= 0 || tess.numIndexes <= 0 ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_EMPTY_BATCH );
		return qfalse;
	}

	numVertexes = tess.numVertexes * 2;
	xyzBytes = numVertexes * (int)sizeof( tess.xyz[0] );
	indexBytes = tess.numIndexes * (int)sizeof( tess.indexes[0] );
	indexOffset = GLX_CompatAlignInt( xyzBytes, 16 );
	totalBytes = GLX_CompatAlignInt( indexOffset + indexBytes, 64 );

	if ( !GLX_CompatStreamReserve( totalBytes, 64, &reservation ) ) {
		GLX_CompatRecordStreamDrawResult( numVertexes, tess.numIndexes,
			totalBytes, indexBytes, 0, qfalse, qfalse, qfalse, GLX_STAGE_SHADOW_PASS,
			GLX_DYNAMIC_CATEGORY_MASK_SPECIAL, qfalse );
		return qfalse;
	}

	if ( !GLX_CompatStreamUploadAt( &reservation, 0, tess.xyz, xyzBytes ) ) {
		ok = qfalse;
	}
	if ( ok && !GLX_CompatStreamUploadAt( &reservation, indexOffset, tess.indexes, indexBytes ) ) {
		ok = qfalse;
	}
	GLX_CompatStreamCommit( &reservation );

	if ( !ok ) {
		GLX_CompatRecordStreamDrawResult( numVertexes, tess.numIndexes,
			totalBytes, indexBytes, 0, qfalse, qfalse, qfalse, GLX_STAGE_SHADOW_PASS,
			GLX_DYNAMIC_CATEGORY_MASK_SPECIAL, qfalse );
		return qfalse;
	}

	oldArrayBuffer = GLX_CompatBindStreamArrayBuffer( reservation.buffer );
	oldElementArrayBuffer = GLX_CompatBindStreamElementArrayBuffer( reservation.buffer );

	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_NONE );
	qglVertexPointer( 3, GL_FLOAT, sizeof( tess.xyz[0] ), (const GLvoid *)(intptr_t)( reservation.offset ) );

	GL_Cull( CT_BACK_SIDED );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_INCR );
	if ( !GLX_CompatDrawElementsClassified( GL_TRIANGLES, tess.numIndexes, GL_INDEX_TYPE,
		(const GLvoid *)(intptr_t)( reservation.offset + indexOffset ),
		GLX_LEGACY_DELEGATION_NONE, GLX_DRAW_STREAM_GENERIC, GLX_STAGE_SHADOW_PASS,
		GLX_DYNAMIC_CATEGORY_MASK_SPECIAL ) ) {
		ok = qfalse;
	}

	GL_Cull( CT_FRONT_SIDED );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_DECR );
	if ( !GLX_CompatDrawElementsClassified( GL_TRIANGLES, tess.numIndexes, GL_INDEX_TYPE,
		(const GLvoid *)(intptr_t)( reservation.offset + indexOffset ),
		GLX_LEGACY_DELEGATION_NONE, GLX_DRAW_STREAM_GENERIC, GLX_STAGE_SHADOW_PASS,
		GLX_DYNAMIC_CATEGORY_MASK_SPECIAL ) ) {
		ok = qfalse;
	}

	GLX_CompatRestoreStreamElementArrayBuffer( oldElementArrayBuffer );
	GLX_CompatRestoreStreamArrayBuffer( 0 );
	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_NONE );
	qglVertexPointer( 3, GL_FLOAT, sizeof( tess.xyz[0] ), tess.xyz );
	GLX_CompatRestoreStreamArrayBuffer( oldArrayBuffer );

	GLX_CompatRecordStreamDrawResult( numVertexes, tess.numIndexes,
		totalBytes, indexBytes, 0, qfalse, qfalse, qfalse, GLX_STAGE_SHADOW_PASS,
		GLX_DYNAMIC_CATEGORY_MASK_SPECIAL, ok );
	return ok;
}
#endif


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
	GLboolean rgba[4];
	qboolean glxStreamedDraw = qfalse;

	if ( glConfig.stencilBits < 4 ) {
		return;
	}

#ifdef USE_PMLIGHT
	if ( R_GetDlightMode() == 2 && r_shadows->integer == 2 )
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

	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_NONE );

	qglVertexPointer( 3, GL_FLOAT, sizeof( tess.xyz[0] ), tess.xyz );

	// draw the silhouette edges

	qglDisable( GL_TEXTURE_2D );
	//GL_Bind( tr.whiteImage );
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	qglColor4f( 0.2f, 0.2f, 0.2f, 1.0f );

	// don't write to the color buffer
	GL_GetColorMask( rgba );
	GL_ColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );

	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_ALWAYS, 1, 255 );

#ifdef RENDERER_GLX
	glxStreamedDraw = GLX_TryStreamDrawStencilShadowVolume();
#endif
	if ( !glxStreamedDraw ) {
		if ( qglLockArraysEXT )
			qglLockArraysEXT( 0, tess.numVertexes*2 );

		GL_Cull( CT_BACK_SIDED );
		qglStencilOp( GL_KEEP, GL_KEEP, GL_INCR );

		R_DrawElements( tess.numIndexes, tess.indexes );

		GL_Cull( CT_FRONT_SIDED );
		qglStencilOp( GL_KEEP, GL_KEEP, GL_DECR );

		R_DrawElements( tess.numIndexes, tess.indexes );

		if ( qglUnlockArraysEXT )
			qglUnlockArraysEXT();
	}

	// re-enable writing to the color buffer
	GL_ColorMask( rgba[0], rgba[1], rgba[2], rgba[3] );

	qglEnable( GL_TEXTURE_2D );

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

	static const vec3_t verts[4] = {
		{ -100, 100, -10 },
		{  100, 100, -10 },
		{ -100,-100, -10 },
		{  100,-100, -10 }
	};
	qboolean glxStreamedDraw = qfalse;

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
#ifdef RENDERER_GLX
	if ( GLX_CompatStreamDrawShadowsEnabled() ) {
		glxStreamedDraw = GLX_CompatTryStreamDrawArrayPass( 4, verts,
			(int)sizeof( verts[0] ), GL_TRIANGLE_STRIP, GLX_STAGE_SHADOW_PASS,
			GLX_DYNAMIC_CATEGORY_MASK_SPECIAL );
	}
#endif
	if ( !glxStreamedDraw ) {
#ifdef RENDERER_GLX
		GLX_CompatDrawArrays( GL_TRIANGLE_STRIP, 0, 4,
			GLX_LEGACY_DELEGATION_DRAW_ARRAY, GLX_DRAW_NONE );
#else
		qglDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
#endif
	}

	qglColor4f( 1, 1, 1, 1 );
	qglDisable( GL_STENCIL_TEST );

	qglEnable( GL_TEXTURE_2D );
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
	if ( R_GetDlightMode() == 2 && r_shadows->integer == 2 )
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
