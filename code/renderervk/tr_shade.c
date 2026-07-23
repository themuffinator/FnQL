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
// tr_shade.c

#include "tr_local.h"

/*

  THIS ENTIRE FILE IS BACK END

  This file deals with applying shaders to surface data in the tess struct.
*/


/*
==================
R_DrawElements
==================
*/
#ifndef USE_VULKAN
void R_DrawElements( int numIndexes, const glIndex_t *indexes ) {
	qglDrawElements( GL_TRIANGLES, numIndexes, GL_INDEX_TYPE, indexes );
}
#endif


/*
=============================================================

SURFACE SHADERS

=============================================================
*/

shaderCommands_t	tess;
#ifndef USE_VULKAN
static qboolean	setArraysOnce;
#endif

#ifdef USE_VULKAN
static VkDescriptorSet R_ImageDescriptor( image_t *image )
{
	if ( !image ) {
		ri.Printf( PRINT_WARNING, "R_ImageDescriptor: NULL image\n" );
		image = tr.defaultImage;
	}

	if ( r_nobind->integer && tr.dlightImage ) {
		image = tr.dlightImage;
	}

	image->frameUsed = tr.frameCount;
	return image->descriptor;
}


static VkDescriptorSet R_AnimatedImageDescriptor( const textureBundle_t *bundle )
{
	int64_t index;
	double v;

	if ( bundle->isVideoMap ) {
		ri.CIN_RunCinematic( bundle->videoMapHandle );
		ri.CIN_UploadCinematic( bundle->videoMapHandle );
		return R_ImageDescriptor( bundle->image[0] );
	}

	if ( bundle->isScreenMap ) {
		if ( !backEnd.screenMapDone ) {
			return R_ImageDescriptor( tr.blackImage );
		}
		return vk.screenMap.color_descriptor;
	}

	if ( bundle->numImageAnimations <= 1 ) {
		return R_ImageDescriptor( bundle->image[0] );
	}

	v = tess.shaderTime * bundle->imageAnimationSpeed;
	index = v;

	if ( index < 0 ) {
		index = 0;
	}
	index %= bundle->numImageAnimations;

	return R_ImageDescriptor( bundle->image[ index ] );
}
#endif


/*
=================
R_BindAnimatedImage
=================
*/
#ifndef USE_VULKAN
static void R_BindAnimatedImage( const textureBundle_t *bundle ) {
	int64_t index;
	double	v;

	if ( bundle->isVideoMap ) {
		ri.CIN_RunCinematic(bundle->videoMapHandle);
		ri.CIN_UploadCinematic(bundle->videoMapHandle);
		return;
	}

	if ( bundle->isScreenMap /*&& backEnd.viewParms.frameSceneNum == 1*/ ) {
		if ( !backEnd.screenMapDone )
			GL_Bind( tr.blackImage );
		return;
	}

	if ( bundle->numImageAnimations <= 1 ) {
		GL_Bind( bundle->image[0] );
		return;
	}

	// it is necessary to do this messy calc to make sure animations line up
	// exactly with waveforms of the same frequency
	//v = tess.shaderTime * bundle->imageAnimationSpeed * FUNCTABLE_SIZE;
	//index = v;
	//index >>= FUNCTABLE_SIZE2;

	v = tess.shaderTime * bundle->imageAnimationSpeed; // fix for frameloss bug -EC-
	index = v;

	if ( index < 0 ) {
		index = 0;	// may happen with shader time offsets
	}
	index %= bundle->numImageAnimations;

	GL_Bind( bundle->image[ index ] );
}
#endif


/*
================
DrawTris

Draws triangle outlines for debugging
================
*/
static void DrawTris( const shaderCommands_t *input ) {
#ifdef USE_VULKAN
	uint32_t pipeline;
	qboolean mirror;

	if ( r_showtris->integer == 1 && backEnd.drawConsole )
		return;

	if ( tess.numIndexes == 0 )
		return;

	if ( r_fastsky->integer && input->shader->isSky )
		return;

	mirror = R_ViewPassIsMirror( &backEnd.viewParms );

#ifdef USE_VBO
	if ( tess.vboIndex ) {
#ifdef USE_PMLIGHT
		if ( tess.dlightPass )
			pipeline = mirror ? vk.tris_mirror_debug_red_pipeline : vk.tris_debug_red_pipeline;
		else
#endif
			pipeline = mirror ? vk.tris_mirror_debug_green_pipeline : vk.tris_debug_green_pipeline;
	} else
#endif
	{
#ifdef USE_PMLIGHT
		if ( tess.dlightPass )
			pipeline = mirror ? vk.tris_mirror_debug_red_pipeline : vk.tris_debug_red_pipeline;
		else
#endif
			pipeline = mirror ? vk.tris_mirror_debug_pipeline : vk.tris_debug_pipeline;
	}

	vk_bind_pipeline( pipeline );
	vk_draw_geometry( DEPTH_RANGE_ZERO, qtrue );

#else
	if ( r_showtris->integer == 1 && backEnd.drawConsole )
		return;

	GL_ClientState( 0, CLS_NONE );
	qglDisable( GL_TEXTURE_2D );

	qglColor4f( 1, 1, 1, 1 );

	GL_State( GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE );
	qglDepthRange( 0, 0 );

	qglVertexPointer( 3, GL_FLOAT, sizeof( input->xyz[0] ), input->xyz );

	if ( qglLockArraysEXT ) {
		qglLockArraysEXT( 0, input->numVertexes );
	}

	R_DrawElements( input->numIndexes, input->indexes );

	if ( qglUnlockArraysEXT ) {
		qglUnlockArraysEXT();
	}

	qglEnable( GL_TEXTURE_2D );

	qglDepthRange( 0, 1 );
#endif
}


/*
================
DrawNormals

Draws vertex normals for debugging
================
*/
static void DrawNormals( const shaderCommands_t *input ) {
	int		i;
#ifdef USE_VULKAN
#ifdef USE_VBO
	if ( tess.vboIndex )
		return; // must be handled specially
#endif

	GL_Bind( tr.whiteImage );

	tess.numIndexes = 0;
	for ( i = 0; i < tess.numVertexes; i++ ) {
		VectorMA( tess.xyz[i], 2.0, tess.normal[i], tess.xyz[i + tess.numVertexes] );
		tess.indexes[  tess.numIndexes + 0 ] = i;
		tess.indexes[  tess.numIndexes + 1 ] = i + tess.numVertexes;
		tess.numIndexes += 2;
	}
	tess.numVertexes *= 2;
	Com_Memset( tess.svars.colors[0][0].rgba, tr.identityLightByte, tess.numVertexes * sizeof( color4ub_t ) );

	vk_bind_pipeline( vk.normals_debug_pipeline );
	vk_bind_index();
	vk_bind_geometry( TESS_XYZ | TESS_ST0 | TESS_RGBA0 );
	vk_draw_geometry( DEPTH_RANGE_ZERO, qtrue );
#else
	GL_ClientState( 0, CLS_NONE );

	qglDisable( GL_TEXTURE_2D );
	qglColor4f( 1, 1, 1, 1 );

	qglDepthRange( 0, 0 );	// never occluded

	GL_State( GLS_DEPTHMASK_TRUE );

	for ( i = tess.numVertexes-1; i >= 0; i-- ) {
		VectorMA( tess.xyz[i], 2.0, tess.normal[i], tess.xyz[i*2 + 1] );
		VectorCopy( tess.xyz[i], tess.xyz[i*2] );
	}

	qglVertexPointer( 3, GL_FLOAT, sizeof( tess.xyz[0] ), tess.xyz );

	if ( qglLockArraysEXT ) {
		qglLockArraysEXT( 0, tess.numVertexes * 2 );
	}

	qglDrawArrays( GL_LINES, 0, tess.numVertexes * 2 );

	if ( qglUnlockArraysEXT ) {
		qglUnlockArraysEXT();
	}

	qglEnable( GL_TEXTURE_2D );

	qglDepthRange( 0, 1 );
#endif
}


/*
==============
RB_BeginSurface

We must set some things up before beginning any tesselation,
because a surface may be forced to perform a RB_End due
to overflow.
==============
*/
void RB_BeginSurface( shader_t *shader, int fogNum ) {

	shader_t *state;
#ifdef USE_TESS_NEEDS_NORMAL
	qboolean liquidNeedsNormal;
#endif

#ifdef USE_VBO
	if ( shader->isStaticShader && !shader->remappedShader ) {
		tess.allowVBO = qtrue;
	} else {
		tess.allowVBO = qfalse;
	}
#endif

	if ( shader->remappedShader ) {
		state = shader->remappedShader;
	} else {
		state = shader;
	}

#ifdef USE_TESS_NEEDS_NORMAL
	liquidNeedsNormal = ( vk.fboActive &&
		vk.liquidSnapshot.color_descriptor != VK_NULL_HANDLE &&
		r_liquid && r_liquid->integer > 0 && shader->sort >= SS_FOG &&
		R_LiquidShaderSupported( state ) &&
		R_LiquidContentsEnabled( shader->contentFlags | state->contentFlags,
			r_liquid->integer ) ) ? qtrue : qfalse;
#endif

#ifdef USE_PMLIGHT
	if ( tess.fogNum != fogNum ) {
		tess.dlightUpdateParams = qtrue;
	}
#endif

#ifdef USE_TESS_NEEDS_NORMAL
#ifdef USE_PMLIGHT
	tess.needsNormal = state->needsNormal || tess.dlightPass || r_shownormals->integer ||
		liquidNeedsNormal;
	if ( backEnd.viewParms.passFlags & VPF_DLIGHT_SHADOW ) {
		tess.needsNormal = qtrue;
	}
#else
	tess.needsNormal = state->needsNormal || r_shownormals->integer ||
		liquidNeedsNormal;
#endif
#endif

#ifdef USE_TESS_NEEDS_ST2
	tess.needsST2 = state->needsST2;
#endif

	tess.numIndexes = 0;
	tess.numVertexes = 0;
	tess.shader = state;
	tess.liquidContentFlags = shader->contentFlags | state->contentFlags;
	tess.liquidSort = shader->sort;
	tess.fogNum = fogNum;

#ifdef USE_LEGACY_DLIGHTS
	tess.dlightBits = 0;		// will be OR'd in by surface functions
#endif
	tess.xstages = state->stages;
	tess.numPasses = state->numUnfoggedPasses;

	tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
	if ( tess.shader->clampTime && tess.shaderTime >= tess.shader->clampTime ) {
		tess.shaderTime = tess.shader->clampTime;
	}
}

#ifdef USE_PMLIGHT
static void RB_ApplyDlightShadowCasterNormalBias( void )
{
	float normalBias;
	int i;

	if ( !( backEnd.viewParms.passFlags & VPF_DLIGHT_SHADOW ) ) {
		return;
	}

	if ( tess.csmCasterPass ) {
		normalBias = r_csmCasterNormalBias ? r_csmCasterNormalBias->value : 0.5f;
	} else {
		normalBias = r_dlightShadowCasterNormalBias ? r_dlightShadowCasterNormalBias->value : 0.25f;
	}
	normalBias = R_ShadowClampCasterNormalBias( normalBias );
	if ( normalBias <= 0.0f ) {
		return;
	}

	for ( i = 0; i < tess.numVertexes; i++ ) {
		vec3_t world;
		vec3_t normalWorld;
		vec3_t lightToVertex;
		float normalLength;
		float lightSide;
		float normalScale;
		float scale;

		normalWorld[0] = tess.normal[i][0] * backEnd.or.axis[0][0] +
			tess.normal[i][1] * backEnd.or.axis[1][0] +
			tess.normal[i][2] * backEnd.or.axis[2][0];
		normalWorld[1] = tess.normal[i][0] * backEnd.or.axis[0][1] +
			tess.normal[i][1] * backEnd.or.axis[1][1] +
			tess.normal[i][2] * backEnd.or.axis[2][1];
		normalWorld[2] = tess.normal[i][0] * backEnd.or.axis[0][2] +
			tess.normal[i][1] * backEnd.or.axis[1][2] +
			tess.normal[i][2] * backEnd.or.axis[2][2];

		normalLength = VectorNormalize( normalWorld );
		if ( normalLength <= 0.0f ) {
			continue;
		}

		if ( tess.csmCasterPass ) {
			VectorScale( tr.csm.lightDirection, -1.0f, lightToVertex );
		} else {
			world[0] = backEnd.or.origin[0] +
				tess.xyz[i][0] * backEnd.or.axis[0][0] +
				tess.xyz[i][1] * backEnd.or.axis[1][0] +
				tess.xyz[i][2] * backEnd.or.axis[2][0];
			world[1] = backEnd.or.origin[1] +
				tess.xyz[i][0] * backEnd.or.axis[0][1] +
				tess.xyz[i][1] * backEnd.or.axis[1][1] +
				tess.xyz[i][2] * backEnd.or.axis[2][1];
			world[2] = backEnd.or.origin[2] +
				tess.xyz[i][0] * backEnd.or.axis[0][2] +
				tess.xyz[i][1] * backEnd.or.axis[1][2] +
				tess.xyz[i][2] * backEnd.or.axis[2][2];

			VectorSubtract( world, backEnd.viewParms.or.origin, lightToVertex );
			if ( VectorNormalize( lightToVertex ) <= 0.0f ) {
				continue;
			}
		}
		lightSide = DotProduct( normalWorld, lightToVertex );
		normalScale = 0.25f + 0.50f * ( 1.0f - Com_Clamp( 0.0f, 1.0f, fabsf( lightSide ) ) );
		scale = ( normalBias * normalScale ) / normalLength;
		if ( lightSide < 0.0f ) {
			scale = -scale;
		}
		VectorMA( tess.xyz[i], scale, tess.normal[i], tess.xyz[i] );
	}
}
#endif


/*
===================
DrawMultitextured

output = t0 * t1 or t0 + t1

t0 = most upstream according to spec
t1 = most downstream according to spec
===================
*/
#ifndef USE_VULKAN
static void DrawMultitextured( const shaderCommands_t *input, int stage ) {
	const shaderStage_t *pStage;

	pStage = tess.xstages[ stage ];

	GL_State( pStage->stateBits );

	if ( !setArraysOnce ) {
		R_ComputeColors( 0, tess.svars.colors[0], pStage );
		R_ComputeTexCoords( 0, &pStage->bundle[0] );
		R_ComputeTexCoords( 1, &pStage->bundle[1] );
		GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );

		qglTexCoordPointer( 2, GL_FLOAT, 0, input->svars.texcoordPtr[0] );
		qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, input->svars.colors[0].rgba );

		GL_ClientState( 1, CLS_TEXCOORD_ARRAY );
		qglTexCoordPointer( 2, GL_FLOAT, 0, input->svars.texcoordPtr[1] );
	}

	//
	// base
	//
	GL_SelectTexture( 0 );
	R_BindAnimatedImage( &pStage->bundle[0] );

	//
	// lightmap/secondary pass
	//
	GL_SelectTexture( 1 );
	qglEnable( GL_TEXTURE_2D );
	R_BindAnimatedImage( &pStage->bundle[1] );

	if ( r_lightmap->integer ) {
		GL_TexEnv( GL_REPLACE );
	} else {
		GL_TexEnv( pStage->mtEnv );
	}

	R_DrawElements( input->numIndexes, input->indexes );

	//
	// disable texturing on TEXTURE1, then select TEXTURE0
	//
	//GL_ClientState( 1, CLS_NONE );

	qglDisable( GL_TEXTURE_2D );
	GL_SelectTexture( 0 );
}
#endif


#ifdef USE_LEGACY_DLIGHTS
/*
===================
ProjectDlightTexture

Perform dynamic lighting with another rendering pass
===================
*/
#ifdef USE_VULKAN
static qboolean ProjectDlightTexture( void ) {
#else
static void ProjectDlightTexture( void ) {
#endif
	int		i, l;
	vec3_t	origin;
	float	*texCoords;
	byte	*colors;
	byte	clipBits[SHADER_MAX_VERTEXES];
#ifdef USE_VULKAN
	uint32_t pipeline;
	qboolean rebindIndex = qfalse;
#else
	float	texCoordsArray[SHADER_MAX_VERTEXES][2];
	byte	colorArray[SHADER_MAX_VERTEXES][4];
#endif
	glIndex_t hitIndexes[SHADER_MAX_INDEXES];
	int		numIndexes;
	float	scale;
	float	radius;
	float	modulate = 0.0f;
	const dlight_t *dl;

	if ( !backEnd.refdef.num_dlights ) {
#ifdef USE_VULKAN
		return rebindIndex;
#else
		return;
#endif
	}

	for ( l = 0 ; l < backEnd.refdef.num_dlights ; l++ ) {

		if ( !( tess.dlightBits & ( 1 << l ) ) ) {
			continue;	// this surface definitely doesn't have any of this light
		}

#ifdef USE_VULKAN
		texCoords = (float*)&tess.svars.texcoords[0][0];
		tess.svars.texcoordPtr[0] = tess.svars.texcoords[0];
		colors = tess.svars.colors[0][0].rgba;
#else
		texCoords = texCoordsArray[0];
		colors = colorArray[0];
#endif

		dl = &backEnd.refdef.dlights[l];
		VectorCopy( dl->transformed, origin );
		radius = dl->radius;
		scale = 1.0f / radius;

		for ( i = 0 ; i < tess.numVertexes ; i++, texCoords += 2, colors += 4 ) {
			int		clip = 0;
			vec3_t	dist;

			VectorSubtract( origin, tess.xyz[i], dist );

			backEnd.pc.c_dlightVertexes++;

			texCoords[0] = 0.5f + dist[0] * scale;
			texCoords[1] = 0.5f + dist[1] * scale;

			if ( !r_dlightBacks->integer &&
					// dist . tess.normal[i]
					( dist[0] * tess.normal[i][0] +
					dist[1] * tess.normal[i][1] +
					dist[2] * tess.normal[i][2] ) < 0.0f ) {
				clip = 63;
			} else {
				if ( texCoords[0] < 0.0f ) {
					clip |= 1;
				} else if ( texCoords[0] > 1.0f ) {
					clip |= 2;
				}
				if ( texCoords[1] < 0.0f ) {
					clip |= 4;
				} else if ( texCoords[1] > 1.0f ) {
					clip |= 8;
				}

				// modulate the strength based on the height and color
				if ( dist[2] > radius ) {
					clip |= 16;
					modulate = 0.0f;
				} else if ( dist[2] < -radius ) {
					clip |= 32;
					modulate = 0.0f;
				} else {
					//*((int*)&dist[2]) &= 0x7FFFFFFF;
					dist[2] = fabsf( dist[2] );
					if ( dist[2] < radius * 0.5f ) {
						modulate = 1.0 * 255.0;
					} else {
						modulate = 2.0f * (radius - dist[2]) * scale * 255.0;
					}
				}
			}
			clipBits[i] = clip;
			colors[0] = dl->color[0] * modulate;
			colors[1] = dl->color[1] * modulate;
			colors[2] = dl->color[2] * modulate;
			colors[3] = 255;
		}

		// build a list of triangles that need light
		numIndexes = 0;
		for ( i = 0 ; i < tess.numIndexes ; i += 3 ) {
			glIndex_t a, b, c;

			a = tess.indexes[i];
			b = tess.indexes[i+1];
			c = tess.indexes[i+2];
			if ( clipBits[a] & clipBits[b] & clipBits[c] ) {
				continue;	// not lighted
			}
			hitIndexes[numIndexes] = a;
			hitIndexes[numIndexes+1] = b;
			hitIndexes[numIndexes+2] = c;
			numIndexes += 3;
		}

		if ( numIndexes == 0 ) {
			continue;
		}

#ifndef USE_VULKAN
		GL_ClientState( 1, CLS_NONE );
		GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );

		qglTexCoordPointer( 2, GL_FLOAT, 0, texCoordsArray[0] );
		qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, colorArray );
#endif

#ifdef USE_VULKAN
		{
			vk_material_t material;

			vk_material_init( &material );
			vk_material_set_descriptor( &material, VK_DESC_TEXTURE0, R_ImageDescriptor( tr.dlightImage ) );
			vk_bind_material( &material );
		}

		if ( numIndexes != tess.numIndexes ) {
			// re-bind index buffer for later fog pass
			rebindIndex = qtrue;
		}
		pipeline = vk.dlight_pipelines[dl->additive > 0 ? 1 : 0][tess.shader->cullType][tess.shader->polygonOffset];
		vk_bind_pipeline( pipeline );
		vk_bind_index_ext( numIndexes, hitIndexes );
		vk_bind_geometry( TESS_RGBA0 | TESS_ST0 );
		vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );
#else
		GL_Bind( tr.dlightImage );

		// include GLS_DEPTHFUNC_EQUAL so alpha tested surfaces don't add light
		// where they aren't rendered

		if ( dl->additive ) {
			GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
		} else {
			GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
		}

		R_DrawElements( numIndexes, hitIndexes );
#endif
		backEnd.pc.c_totalIndexes += numIndexes;
		backEnd.pc.c_dlightIndexes += numIndexes;
	}

#ifdef USE_VULKAN
	return rebindIndex;
#endif
}

#endif // USE_LEGACY_DLIGHTS

uint32_t VK_PushUniform( const vkUniform_t *uniformData );
void VK_SetFogParams( vkUniform_t *uniformData, int *fogStage );
static vkUniform_t uniform;

static float VK_ComputeTextureIntensityScale( const image_t *image )
{
	if ( image == NULL || r_intensity->value <= 1.0f )
	{
		return 1.0f;
	}

	if ( image->flags & IMGFLAG_NOLIGHTSCALE )
	{
		return 1.0f;
	}

	if ( ( image->flags & IMGFLAG_MIPMAP ) || image->uploadWidth != image->width || image->uploadHeight != image->height )
	{
		return r_intensity->value;
	}

	return 1.0f;
}

static void VK_SetTextureFactors( vkUniform_t *uniformData, const shaderStage_t *pStage, int bundle )
{
	float specColor;

	uniformData->texFactors[0] = VK_ComputeTextureIntensityScale( pStage->bundle[ bundle ].image[0] );
	uniformData->texFactors[1] = r_dlightSpecPower ? r_dlightSpecPower->value : 10.0f;

	specColor = r_dlightSpecColor ? r_dlightSpecColor->value : -0.2f;
	if ( specColor > 0.0f ) {
		uniformData->texFactors[2] = 0.0f;
		uniformData->texFactors[3] = specColor;
	} else {
		uniformData->texFactors[2] = -specColor;
		uniformData->texFactors[3] = 0.0f;
	}
}

/*
===================
RB_FogPass

Blends a fog texture on top of everything else
===================
*/
#ifdef USE_VULKAN
static void RB_FogPass( qboolean rebindIndex ) {
	uint32_t pipeline = vk.fog_pipelines[tess.shader->fogPass - 1][tess.shader->cullType][tess.shader->polygonOffset];
#ifdef USE_FOG_ONLY
	int fog_stage;

	// fog parameters
	vk_bind_pipeline( pipeline );
	if ( rebindIndex ) {
		vk_bind_index();
	}
	VK_SetFogParams( &uniform, &fog_stage );
	VK_PushUniform( &uniform );
	{
		vk_material_t material;

		vk_material_init( &material );
		vk_material_set_descriptor( &material, VK_DESC_FOG_ONLY, tr.fogImage->descriptor );
		vk_bind_material( &material );
	}
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );
#else
	const fog_t	*fog = tr.world->fogs + tess.fogNum;
	int	i;

	for ( i = 0; i < tess.numVertexes; i++ ) {
		tess.svars.colors[0][i] = fog->colorInt;
	}

	RB_CalcFogTexCoords( ( float * ) tess.svars.texcoords[0] );
	tess.svars.texcoordPtr[ 0 ] = tess.svars.texcoords[ 0 ];
	{
		vk_material_t material;

		vk_material_init( &material );
		vk_material_set_descriptor( &material, VK_DESC_TEXTURE0, R_ImageDescriptor( tr.fogImage ) );
		vk_bind_material( &material );
	}

	vk_bind_pipeline( pipeline );
	if ( rebindIndex ) {
		vk_bind_index();
	}
	vk_bind_geometry( TESS_ST0 | TESS_RGBA0 );
	vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );
#endif
#else
static void RB_FogPass( void ) {
	const fog_t	*fog;
	int			i;

	RB_CalcFogTexCoords( ( float * ) tess.svars.texcoords[0] );

	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );

	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.svars.colors[0].rgba );
	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords[0] );

	GL_SelectTexture( 0 );
	GL_Bind( tr.fogImage );

	if ( tess.shader->fogPass == FP_EQUAL ) {
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL );
	} else {
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	}

	R_DrawElements( tess.numIndexes, tess.indexes );
#endif
}


/*
===============
R_ComputeColors
===============
*/
void R_ComputeColors( const int b, color4ub_t *dest, const shaderStage_t *pStage )
{
	int		i;

	if ( tess.numVertexes == 0 )
		return;

	//
	// rgbGen
	//
	switch ( pStage->bundle[b].rgbGen )
	{
		case CGEN_IDENTITY:
			Com_Memset( dest, 0xff, tess.numVertexes * 4 );
			break;
		default:
		case CGEN_IDENTITY_LIGHTING:
			Com_Memset( dest, tr.identityLightByte, tess.numVertexes * 4 );
			break;
		case CGEN_LIGHTING_DIFFUSE:
			RB_CalcDiffuseColor( ( unsigned char * ) dest );
			break;
		case CGEN_EXACT_VERTEX:
			Com_Memcpy( dest, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
			break;
		case CGEN_CONST:
			for ( i = 0; i < tess.numVertexes; i++ ) {
				dest[i] = pStage->bundle[b].constantColor;
			}
			break;
		case CGEN_VERTEX:
			if ( tr.identityLight == 1 )
			{
				Com_Memcpy( dest, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
			}
			else
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					dest[i].rgba[0] = tess.vertexColors[i].rgba[0] * tr.identityLight;
					dest[i].rgba[1] = tess.vertexColors[i].rgba[1] * tr.identityLight;
					dest[i].rgba[2] = tess.vertexColors[i].rgba[2] * tr.identityLight;
					dest[i].rgba[3] = tess.vertexColors[i].rgba[3];
				}
			}
			break;
		case CGEN_ONE_MINUS_VERTEX:
			if ( tr.identityLight == 1 )
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					dest[i].rgba[0] = 255 - tess.vertexColors[i].rgba[0];
					dest[i].rgba[1] = 255 - tess.vertexColors[i].rgba[1];
					dest[i].rgba[2] = 255 - tess.vertexColors[i].rgba[2];
				}
			}
			else
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					dest[i].rgba[0] = ( 255 - tess.vertexColors[i].rgba[0] ) * tr.identityLight;
					dest[i].rgba[1] = ( 255 - tess.vertexColors[i].rgba[1] ) * tr.identityLight;
					dest[i].rgba[2] = ( 255 - tess.vertexColors[i].rgba[2] ) * tr.identityLight;
				}
			}
			break;
		case CGEN_FOG:
			{
				const fog_t *fog = tr.world->fogs + tess.fogNum;

				for ( i = 0; i < tess.numVertexes; i++ ) {
					dest[i] = fog->colorInt;
				}
			}
			break;
		case CGEN_WAVEFORM:
			RB_CalcWaveColor( &pStage->bundle[b].rgbWave, dest->rgba );
			break;
		case CGEN_ENTITY:
			RB_CalcColorFromEntity( dest->rgba );
			break;
		case CGEN_ONE_MINUS_ENTITY:
			RB_CalcColorFromOneMinusEntity( dest->rgba );
			break;
	}

	//
	// alphaGen
	//
	switch ( pStage->bundle[b].alphaGen )
	{
	case AGEN_SKIP:
		break;
	case AGEN_IDENTITY:
		if ( ( pStage->bundle[b].rgbGen == CGEN_VERTEX && tr.identityLight != 1 ) ||
			 pStage->bundle[b].rgbGen != CGEN_VERTEX ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				dest[i].rgba[3] = 255;
			}
		}
		break;
	case AGEN_CONST:
		for ( i = 0; i < tess.numVertexes; i++ ) {
			dest[i].rgba[3] = pStage->bundle[b].constantColor.rgba[3];
		}
		break;
	case AGEN_WAVEFORM:
		RB_CalcWaveAlpha( &pStage->bundle[b].alphaWave, dest->rgba );
		break;
	case AGEN_LIGHTING_SPECULAR:
		RB_CalcSpecularAlpha( dest->rgba );
		break;
	case AGEN_ENTITY:
		RB_CalcAlphaFromEntity( dest->rgba );
		break;
	case AGEN_ONE_MINUS_ENTITY:
		RB_CalcAlphaFromOneMinusEntity( dest->rgba );
		break;
	case AGEN_VERTEX:
		for ( i = 0; i < tess.numVertexes; i++ ) {
			dest[i].rgba[3] = tess.vertexColors[i].rgba[3];
		}
		break;
	case AGEN_ONE_MINUS_VERTEX:
		for ( i = 0; i < tess.numVertexes; i++ )
		{
			dest[i].rgba[3] = 255 - tess.vertexColors[i].rgba[3];
		}
		break;
	case AGEN_PORTAL:
		{
			for ( i = 0; i < tess.numVertexes; i++ )
			{
				unsigned char alpha;
				float len;
				vec3_t v;

				VectorSubtract( tess.xyz[i], backEnd.viewParms.or.origin, v );
				len = VectorLength( v ) * tess.shader->portalRangeR;

				if ( len > 1 )
				{
					alpha = 0xff;
				}
				else
				{
					alpha = len * 0xff;
				}

				dest[i].rgba[3] = alpha;
			}
		}
		break;
	}

	//
	// fog adjustment for colors to fade out as fog increases
	//
	if ( tess.fogNum )
	{
		switch ( pStage->bundle[b].adjustColorsForFog )
		{
		case ACFF_MODULATE_RGB:
			RB_CalcModulateColorsByFog( dest->rgba );
			break;
		case ACFF_MODULATE_ALPHA:
			RB_CalcModulateAlphasByFog( dest->rgba );
			break;
		case ACFF_MODULATE_RGBA:
			RB_CalcModulateRGBAsByFog( dest->rgba );
			break;
		case ACFF_NONE:
			break;
		}
	}
}


/*
===============
R_ComputeTexCoords
===============
*/
void R_ComputeTexCoords( const int b, const textureBundle_t *bundle ) {
	int	i;
	int tm;
	vec2_t *src, *dst;

	if ( !tess.numVertexes )
		return;

	src = dst = tess.svars.texcoords[b];

	//
	// generate the texture coordinates
	//
	switch ( bundle->tcGen )
	{
	case TCGEN_IDENTITY:
		src = tess.texCoords00;
		break;
	case TCGEN_TEXTURE:
		src = tess.texCoords[0];
		break;
	case TCGEN_LIGHTMAP:
		src = tess.texCoords[1];
		break;
	case TCGEN_VECTOR:
		for ( i = 0 ; i < tess.numVertexes ; i++ ) {
			dst[i][0] = DotProduct( tess.xyz[i], bundle->tcGenVectors[0] );
			dst[i][1] = DotProduct( tess.xyz[i], bundle->tcGenVectors[1] );
		}
		break;
	case TCGEN_FOG:
		RB_CalcFogTexCoords( ( float * ) dst );
		break;
	case TCGEN_ENVIRONMENT_MAPPED:
		RB_CalcEnvironmentTexCoords( ( float * ) dst );
		break;
	case TCGEN_ENVIRONMENT_MAPPED_FP:
		RB_CalcEnvironmentTexCoordsFP( ( float * ) dst, bundle->isScreenMap );
		break;
	case TCGEN_BAD:
		return;
	}

	//
	// alter texture coordinates
	//
	for ( tm = 0; tm < bundle->numTexMods ; tm++ ) {
		switch ( bundle->texMods[tm].type )
		{
		case TMOD_NONE:
			tm = TR_MAX_TEXMODS; // break out of for loop
			break;

		case TMOD_TURBULENT:
			RB_CalcTurbulentTexCoords( &bundle->texMods[tm].wave, (float *)src, (float *) dst );
			src = dst;
			break;

		case TMOD_ENTITY_TRANSLATE:
			RB_CalcScrollTexCoords( backEnd.currentEntity->e.shaderTexCoord, (float *)src, (float *) dst );
			src = dst;
			break;

		case TMOD_SCROLL:
			RB_CalcScrollTexCoords( bundle->texMods[tm].scroll, (float *)src, (float *) dst );
			src = dst;
			break;

		case TMOD_SCALE:
			RB_CalcScaleTexCoords( bundle->texMods[tm].scale, (float *) src, (float *) dst );
			src = dst;
			break;

		case TMOD_OFFSET:
			for ( i = 0; i < tess.numVertexes; i++ ) {
				dst[i][0] = src[i][0] + bundle->texMods[tm].offset[0];
				dst[i][1] = src[i][1] + bundle->texMods[tm].offset[1];
			}
			src = dst;
			break;

		case TMOD_SCALE_OFFSET:
			for ( i = 0; i < tess.numVertexes; i++ ) {
				dst[i][0] = (src[i][0] * bundle->texMods[tm].scale[0] ) + bundle->texMods[tm].offset[0];
				dst[i][1] = (src[i][1] * bundle->texMods[tm].scale[1] ) + bundle->texMods[tm].offset[1];
			}
			src = dst;
			break;

		case TMOD_OFFSET_SCALE:
			for ( i = 0; i < tess.numVertexes; i++ ) {
				dst[i][0] = (src[i][0] + bundle->texMods[tm].offset[0]) * bundle->texMods[tm].scale[0];
				dst[i][1] = (src[i][1] + bundle->texMods[tm].offset[1]) * bundle->texMods[tm].scale[1];
			}
			src = dst;
			break;

		case TMOD_STRETCH:
			RB_CalcStretchTexCoords( &bundle->texMods[tm].wave, (float *)src, (float *) dst );
			src = dst;
			break;

		case TMOD_TRANSFORM:
			RB_CalcTransformTexCoords( &bundle->texMods[tm], (float *)src, (float *) dst );
			src = dst;
			break;

		case TMOD_ROTATE:
			RB_CalcRotateTexCoords( bundle->texMods[tm].rotateSpeed, (float *) src, (float *) dst );
			src = dst;
			break;

		default:
			ri.Error( ERR_DROP, "ERROR: unknown texmod '%d' in shader '%s'", bundle->texMods[tm].type, tess.shader->name );
			break;
		}
	}

	tess.svars.texcoordPtr[ b ] = src;
}


/*
** RB_IterateStagesGeneric
*/
#ifdef USE_VULKAN
static qboolean RB_DepthFadeShaderSupported( const shaderStage_t *pStage )
{
	return ( r_depthFade->integer &&
		pStage &&
		tess.shader->dfType > DFT_NONE &&
		tess.shader->dfType < DFT_TBD &&
		pStage->bundle[1].image[0] == NULL &&
		!pStage->depthFragment &&
		vk_depth_fade_supported() ) ? qtrue : qfalse;
}

static qboolean RB_DepthFadeActive( const shaderStage_t *pStage )
{
	return ( RB_DepthFadeShaderSupported( pStage ) &&
		( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) == 0 &&
		( !backEnd.currentEntity || ( backEnd.currentEntity->e.renderfx & RF_DEPTHHACK ) == 0 ) &&
		vk_depth_fade_ready() ) ? qtrue : qfalse;
}


static void RB_SetDepthFade( const shaderStage_t *pStage, vk_material_t *material, qboolean *pushUniform )
{
	const byte scaleAndBias = r_depthFadeScaleAndBias[tess.shader->dfType];
	float zFar;
	int i;

	if ( !RB_DepthFadeActive( pStage ) ) {
		Com_Memset( uniform.depthFadeInfo, 0, sizeof( uniform.depthFadeInfo ) );
		if ( RB_DepthFadeShaderSupported( pStage ) ) {
			vk_material_set_descriptor( material, VK_DESC_DEPTH_FADE,
				vk_depth_fade_available() ? vk.depth_fade_descriptor : VK_NULL_HANDLE );
			*pushUniform = qtrue;
		}
		return;
	}

	zFar = backEnd.viewParms.zFar;
	uniform.depthFadeInfo[0] = zFar / r_znear->value;
	uniform.depthFadeInfo[1] = zFar;
	uniform.depthFadeInfo[2] = tess.shader->dfInvDist;
	uniform.depthFadeInfo[3] = tess.shader->dfBias;

	for ( i = 0; i < 4; i++ ) {
		uniform.depthFadeScale[i] = ( scaleAndBias & ( 1 << i ) ) ? 1.0f : 0.0f;
		uniform.depthFadeBias[i] = ( scaleAndBias & ( 1 << ( i + 4 ) ) ) ? 1.0f : 0.0f;
	}

	vk_material_set_descriptor( material, VK_DESC_DEPTH_FADE, vk.depth_fade_descriptor );
	*pushUniform = qtrue;
}
#endif


#ifdef USE_VULKAN
#ifdef USE_PMLIGHT
static uint32_t VK_ShadowCasterPipeline( uint32_t pipeline )
{
	Vk_Pipeline_Def def;

	if ( !tess.csmCasterPass &&
		!( backEnd.viewParms.passFlags & VPF_DLIGHT_SHADOW ) ) {
		return pipeline;
	}

	vk_get_pipeline_def( pipeline, &def );
	def.face_culling = tess.csmCasterPass ?
		CT_TWO_SIDED : tess.shadowCasterCullType;
	return vk_find_pipeline_ext( 0, &def, qfalse );
}
#endif

static void RB_IterateStagesGeneric( const shaderCommands_t *input, qboolean fogCollapse )
#else
static void RB_IterateStagesGeneric( const shaderCommands_t *input )
#endif
{
	const shaderStage_t *pStage;
	int tess_flags;
	int stage, i;

#ifdef USE_VULKAN
	uint32_t pipeline;
	int fog_stage;
	qboolean pushUniform;

	vk_bind_index();

	tess_flags = input->shader->tessFlags;

	pushUniform = qfalse;

#ifdef USE_FOG_COLLAPSE
	if ( fogCollapse ) {
		VK_SetFogParams( &uniform, &fog_stage );
		VectorCopy( backEnd.or.viewOrigin, uniform.eyePos );
		pushUniform = qtrue;
	} else
#endif
	{
		fog_stage = 0;
		if ( tess_flags & TESS_VPOS ) {
			VectorCopy( backEnd.or.viewOrigin, uniform.eyePos );
			tess_flags &= ~TESS_VPOS;
			pushUniform = qtrue;
		}
	}
#endif // USE_VULKAN

	for ( stage = 0; stage < MAX_SHADER_STAGES; stage++ )
	{
		pStage = tess.xstages[ stage ];
		if ( !pStage )
			break;

#ifdef USE_VBO
		tess.vboStage = stage;
#endif

#ifdef USE_VULKAN
		{
			vk_material_t material;

			vk_material_init( &material );

			tess_flags |= pStage->tessFlags;

			for ( i = 0;  i < pStage->numTexBundles; i++ ) {
				if ( pStage->bundle[i].image[0] != NULL ) {
					vk_material_set_descriptor( &material, VK_DESC_TEXTURE_BASE + i, R_AnimatedImageDescriptor( &pStage->bundle[i] ) );
					if ( tess_flags & ( TESS_ST0 << i ) ) {
						R_ComputeTexCoords( i, &pStage->bundle[i] );
					}
					if ( tess_flags & ( TESS_RGBA0 << i ) ) {
						R_ComputeColors( i, tess.svars.colors[i], pStage );
					}
					if ( tess_flags & (TESS_ENT0 << i) && backEnd.currentEntity ) {
						uniform.ent.color[i][0] = backEnd.currentEntity->e.shader.rgba[0] / 255.0;
						uniform.ent.color[i][1] = backEnd.currentEntity->e.shader.rgba[1] / 255.0;
						uniform.ent.color[i][2] = backEnd.currentEntity->e.shader.rgba[2] / 255.0;
						uniform.ent.color[i][3] = pStage->bundle[i].alphaGen == AGEN_IDENTITY ? 1.0 : (backEnd.currentEntity->e.shader.rgba[3] / 255.0);
						pushUniform = qtrue;
					}
				}
			}

			if ( fogCollapse ) {
				vk_material_set_descriptor( &material, VK_DESC_FOG_COLLAPSE, tr.fogImage->descriptor );
			}

			VK_SetTextureFactors( &uniform, pStage, 0 );
			RB_SetDepthFade( pStage, &material, &pushUniform );
			pushUniform = qtrue;

			if ( pushUniform ) {
				pushUniform = qfalse;
				VK_PushUniform( &uniform );
			}

			GL_SelectTexture( 0 );

			if ( r_lightmap->integer && pStage->bundle[1].lightmap != LIGHTMAP_INDEX_NONE ) {
				// Replace diffuse texture with a white one thus effectively render only lightmap.
				vk_material_set_descriptor( &material, VK_DESC_TEXTURE0, R_ImageDescriptor( tr.whiteImage ) );
			}

			vk_bind_material( &material );

			if ( R_ViewPassIsMirror( &backEnd.viewParms ) ) {
				pipeline = pStage->vk_mirror_pipeline[fog_stage];
			} else {
				pipeline = pStage->vk_pipeline[fog_stage];
			}
#ifdef USE_PMLIGHT
			pipeline = VK_ShadowCasterPipeline( pipeline );
#endif

			vk_bind_pipeline( pipeline );
			vk_bind_geometry( tess_flags );
			vk_draw_geometry( tess.depthRange, qtrue );

			if ( pStage->depthFragment ) {
				if ( R_ViewPassIsMirror( &backEnd.viewParms ) )
					pipeline = pStage->vk_mirror_pipeline_df;
				else
					pipeline = pStage->vk_pipeline_df;
				vk_bind_pipeline( pipeline );
				vk_draw_geometry( tess.depthRange, qtrue );
			}
		}
#else
		R_ComputeColors( 0, tess.svars.colors[0].rgba, pStage );

		R_ComputeTexCoords( 0, &pStage->bundle[0] );

		//
		// do multitexture
		//
		if ( pStage->bundle[1].image[0] != NULL )
		{
			DrawMultitextured( input, stage );
		}
		else
		{
			if ( !setArraysOnce )
			{
				R_ComputeTexCoords( 0, &pStage->bundle[0] );
				R_ComputeColors( 0, tess.svars.colors[0], pStage );

				GL_ClientState( 1, CLS_NONE );
				GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );

				qglTexCoordPointer( 2, GL_FLOAT, 0, input->svars.texcoordPtr[0] );
				qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, input->svars.colors[0].rgba );
			}

			//
			// set state
			//
			R_BindAnimatedImage( &pStage->bundle[0] );

			GL_State( pStage->stateBits );

			//
			// draw
			//
			R_DrawElements( input->numIndexes, input->indexes );
		}
#endif

		// allow skipping out to show just lightmaps during development
		if ( r_lightmap->integer && ( pStage->bundle[0].lightmap != LIGHTMAP_INDEX_NONE || pStage->bundle[1].lightmap != LIGHTMAP_INDEX_NONE ) )
			break;

		tess_flags = 0;
	}

#ifdef USE_VULKAN
	if ( pushUniform ) {
		VK_PushUniform( &uniform );
	}
	if ( tess_flags ) // fog-only shaders?
		vk_bind_geometry( tess_flags );
#endif
}


#ifdef USE_VULKAN

void VK_SetFogParams( vkUniform_t *uniformData, int *fogStage )
{
	if ( tess.fogNum && tess.shader->fogPass ) {
		const fogProgramParms_t *fp = RB_CalcFogProgramParms();
		// vertex data
		Vector4Copy( fp->fogDistanceVector, uniformData->fogDistanceVector );
		Vector4Copy( fp->fogDepthVector, uniformData->fogDepthVector );
		uniformData->fogEyeT[0] = fp->eyeT;
		if ( fp->eyeOutside ) {
			uniformData->fogEyeT[1] = 0.0; // fog eye out
		} else {
			uniformData->fogEyeT[1] = 1.0; // fog eye in
		}
		// fragment data
		Vector4Copy( fp->fogColor, uniformData->fogColor );
		*fogStage = 1;
	} else {
		*fogStage = 0;
	}
}


#ifdef USE_PMLIGHT
static qboolean VK_DlightShadowParams( const dlight_t *dl,
	const shadowPointLightPlan_t *plan, vec4_t shadowInfo, vec4_t shadowAtlas,
	vec4_t shadowMode, float *strength )
{
	int columns;
	int atlasBaseFace;
	int atlasFaceSize;
	int atlasWidth;
	float zNear;
	float zFar;
	float s;

	atlasBaseFace = plan ? plan->atlasBaseFace : ( dl ? dl->shadowAtlasBaseFace : -1 );
	atlasFaceSize = plan ? plan->atlasFaceSize : ( dl ? dl->shadowAtlasFaceSize : 0 );
	if ( ( tr.shadowManager.planned && !plan ) ||
		!dl || dl->linear || ( !tr.shadowManager.planned && !dl->shadowPlanned ) ||
		atlasFaceSize <= 0 || atlasBaseFace < 0 ||
		( ( !r_dlightShadows || !r_dlightShadows->integer ) &&
			( !r_shadowCorrectness || !r_shadowCorrectness->integer ) ) ||
		!r_dlightMode || !r_dlightMode->integer ||
		( tr.shadowManager.planned ?
			!tr.shadowManager.pointAtlasPublication.published : !vk_dlight_shadow_atlas_ready() ) ||
		vk.renderPassIndex != RENDER_PASS_MAIN ||
		backEnd.currentEntity != &tr.worldEntity ||
		( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) ||
		backEnd.viewParms.zFar <= 0.0f ) {
		return qfalse;
	}

	if ( tr.shadowManager.planned ) {
		atlasWidth = tr.shadowManager.pointAtlasPublication.width;
		columns = atlasFaceSize > 0 ? atlasWidth / atlasFaceSize : 0;
	} else {
		columns = vk_dlight_shadow_atlas_columns();
	}
	if ( columns <= 0 ) {
		return qfalse;
	}

	s = r_dlightShadowStrength ? Com_Clamp( 0.0f, 1.0f, r_dlightShadowStrength->value ) : 0.6f;
	if ( s <= 0.0f ) {
		return qfalse;
	}

	zNear = 1.0f;
	zFar = plan ? plan->projectionFar : dl->shadowProjectionFar;
	if ( zFar <= zNear ) {
		return qfalse;
	}

	shadowInfo[0] = zNear;
	shadowInfo[1] = zFar;
	shadowInfo[2] = R_ShadowClampReceiverBias( r_dlightShadowBias ? r_dlightShadowBias->value : 4.0f );
	shadowInfo[3] = 0.0f;
	shadowAtlas[0] = (float)atlasFaceSize;
	shadowAtlas[1] = (float)atlasBaseFace;
	shadowAtlas[2] = (float)columns;
	shadowAtlas[3] = 1.0f;
#ifdef USE_REVERSED_DEPTH
	shadowMode[0] = 1.0f;
#else
	shadowMode[0] = 0.0f;
#endif
	shadowMode[1] = 0.0f;
	shadowMode[2] = 0.0f;
	shadowMode[3] = 0.0f;

	*strength = s;
	return qtrue;
}

static void VK_DlightShadowFilterOffsets( float *inner, float *outer )
{
	int filter = r_dlightShadowFilter ? r_dlightShadowFilter->integer : SHADOW_FILTER_POISSON_4;

	if ( r_shadowCorrectness && r_shadowCorrectness->integer ) {
		filter = SHADOW_FILTER_HARD;
	}
	R_ShadowFilterOffsets( filter, inner, outer );
}

static qboolean VK_SpotShadowParams( const dlight_t *dl, const shadowSpotLightPlan_t *plan,
	vec4_t shadowInfo, vec4_t shadowAtlas, vec4_t shadowMode, float *strength )
{
	float zNear;
	float zFar;
	float fov;
	float tanHalfFov;
	float s;

	if ( !dl || !dl->linear || !plan || !plan->atlasAllocated ||
		plan->atlasTileSize <= 0 || plan->radius <= 0.0f ||
		!r_spotShadows || !r_spotShadows->integer ||
		!tr.shadowManager.planned || !tr.shadowManager.spotAtlasPublication.published ||
		vk.renderPassIndex != RENDER_PASS_MAIN ||
		backEnd.currentEntity != &tr.worldEntity ||
		( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) ||
		backEnd.viewParms.zFar <= 0.0f ) {
		return qfalse;
	}

	if ( tr.shadowManager.spotAtlasPublication.width <= 0 ||
		tr.shadowManager.spotAtlasPublication.height <= 0 ) {
		return qfalse;
	}

	s = r_dlightShadowStrength ? Com_Clamp( 0.0f, 1.0f, r_dlightShadowStrength->value ) : 0.6f;
	if ( s <= 0.0f ) {
		return qfalse;
	}

	zNear = 1.0f;
	zFar = MAX( plan->radius, 64.0f );
	fov = Com_Clamp( 5.0f, 170.0f, ( plan->outerAngle > 0.0f ? plan->outerAngle : 75.0f ) * 2.0f );
	tanHalfFov = tanf( fov * (float)M_PI / 360.0f );

	shadowInfo[0] = zNear;
	shadowInfo[1] = zFar;
	shadowInfo[2] = R_ShadowClampReceiverBias( r_dlightShadowBias ? r_dlightShadowBias->value : 4.0f );
	shadowInfo[3] = tanHalfFov;
	shadowAtlas[0] = (float)plan->atlasX;
	shadowAtlas[1] = (float)plan->atlasY;
	shadowAtlas[2] = (float)plan->atlasTileSize;
	shadowAtlas[3] = 1.0f;
#ifdef USE_REVERSED_DEPTH
	shadowMode[0] = 1.0f;
#else
	shadowMode[0] = 0.0f;
#endif
	shadowMode[1] = 1.0f;
	shadowMode[2] = 0.0f;
	shadowMode[3] = 0.0f;

	*strength = s;
	return qtrue;
}

static void VK_SetLightParams( vkUniform_t *uniformData, const dlight_t *dl ) {
	float radius;
	vec4_t shadowInfo = { 0.0f, 0.0f, 0.0f, 0.0f };
	vec4_t shadowAtlas = { 0.0f, 0.0f, 0.0f, 0.0f };
	vec4_t shadowMode = { 0.0f, 0.0f, 0.0f, 0.0f };
	float shadowStrength = 0.0f;
	const shadowPointLightPlan_t *shadowPlan;
	const shadowSpotLightPlan_t *spotPlan;
	qboolean dlightShadow;

#ifdef USE_VULKAN
	if ( !glConfig.deviceSupportsGamma && !vk.fboActive )
#else
	if ( !glConfig.deviceSupportsGamma )
#endif
		VectorScale( dl->color, 2 * powf( r_intensity->value, r_gamma->value ), uniformData->light.color);
	else
		VectorCopy( dl->color, uniformData->light.color );

	radius = dl->radius;

	// vertex data
	VectorCopy( backEnd.or.viewOrigin, uniformData->eyePos ); uniformData->eyePos[3] = 0.0f;
	VectorCopy( dl->transformed, uniformData->light.pos ); uniformData->light.pos[3] = 0.0f;

	// fragment data
	uniformData->light.color[3] = 1.0f / Square( radius );
	uniformData->dlightFactors[0] = r_dlightFalloff ? r_dlightFalloff->value : 1.0f;
	shadowPlan = R_ShadowManagerPointPlanForDlight( &tr.shadowManager, &backEnd.viewParms, dl );
	spotPlan = R_ShadowManagerSpotPlanForDlight( &tr.shadowManager, dl );
	dlightShadow = VK_DlightShadowParams( dl, shadowPlan, shadowInfo, shadowAtlas, shadowMode,
		&shadowStrength );
	if ( !dlightShadow ) {
		dlightShadow = VK_SpotShadowParams( dl, spotPlan, shadowInfo, shadowAtlas,
			shadowMode, &shadowStrength );
	}
	uniformData->dlightFactors[1] = dlightShadow ? shadowStrength : 0.0f;
	if ( dlightShadow ) {
		VK_DlightShadowFilterOffsets( &uniformData->dlightFactors[2], &uniformData->dlightFactors[3] );
	} else {
		uniformData->dlightFactors[2] = 0.0f;
		uniformData->dlightFactors[3] = 0.0f;
	}

	Vector4Copy( shadowInfo, uniformData->depthFadeInfo );
	Vector4Copy( shadowAtlas, uniformData->depthFadeScale );
	Vector4Copy( shadowMode, uniformData->depthFadeBias );
	uniformData->depthFadeScale[3] = dlightShadow ? 1.0f : 0.0f;

	if ( dl->linear )
	{
		vec4_t ab;
		VectorSubtract( dl->transformed2, dl->transformed, ab );
		ab[3] = 1.0f / DotProduct( ab, ab );
		Vector4Copy( ab, uniformData->light.vector );
	}
}
#endif


uint32_t VK_PushUniform( const vkUniform_t *uniformData ) {
	const uint32_t offset = vk.cmd->uniform_read_offset = PAD( vk.cmd->vertex_buffer_offset, vk.uniform_alignment );

	if ( offset + vk.uniform_item_size > vk.geometry_buffer_size )
		return ~0U;

	// push uniform
	Com_Memcpy( vk.cmd->vertex_buffer_ptr + offset, uniformData, sizeof( *uniformData ) );
	vk.cmd->vertex_buffer_offset = offset + vk.uniform_item_size;

	vk_update_descriptor( VK_DESC_UNIFORM, vk.cmd->uniform_descriptor );
	vk_update_descriptor_offset( VK_DESC_UNIFORM, vk.cmd->uniform_read_offset );

	return offset;
}


#ifdef USE_PMLIGHT
static void VK_CSMShadowFilterOffsets( float *inner, float *outer )
{
	int filter = r_csmShadowFilter ? r_csmShadowFilter->integer : SHADOW_FILTER_POISSON_4;

	if ( r_shadowCorrectness && r_shadowCorrectness->integer ) {
		filter = SHADOW_FILTER_HARD;
	}
	R_ShadowFilterOffsets( filter, inner, outer );
}

void VK_CSMShadowPass( void )
{
	const csmCascadePlan_t *cascade;
	float filterInner, filterOuter;
	float extentX, extentY, extentZ;
	int cascadeIndex;
	int cascadeSize;
	int atlasWidth;
	int atlasHeight;
	vk_material_t material;

	if ( ( tr.shadowManager.planned ?
			!tr.shadowManager.csmAtlasPublication.published : !vk_csm_shadow_atlas_ready() ) ||
		!tr.csm.enabled || tess.csmCascade < 0 ||
		tess.csmCascade >= tr.csm.cascadeCount ) {
		return;
	}

	cascadeIndex = tess.csmCascade;
	cascade = &tr.csm.cascades[cascadeIndex];
	if ( tr.shadowManager.planned ) {
		cascadeSize = tr.shadowManager.csmAtlasPublication.tileSize;
		atlasWidth = tr.shadowManager.csmAtlasPublication.width;
		atlasHeight = tr.shadowManager.csmAtlasPublication.height;
	} else {
		cascadeSize = vk_csm_shadow_cascade_size();
		atlasWidth = vk_csm_shadow_atlas_width();
		atlasHeight = vk_csm_shadow_atlas_height();
	}
	if ( cascadeSize <= 0 || atlasWidth <= 0 || atlasHeight <= 0 ) {
		return;
	}

	extentX = MAX( cascade->bounds[1][0] - cascade->bounds[0][0], 1.0f );
	extentY = MAX( cascade->bounds[1][1] - cascade->bounds[0][1], 1.0f );
	extentZ = MAX( cascade->bounds[1][2] - cascade->bounds[0][2], 1.0f );

	Com_Memset( &uniform, 0, sizeof( uniform ) );

	uniform.csmModelX[0] = backEnd.or.axis[0][0];
	uniform.csmModelX[1] = backEnd.or.axis[1][0];
	uniform.csmModelX[2] = backEnd.or.axis[2][0];
	uniform.csmModelX[3] = backEnd.or.origin[0];
	uniform.csmModelY[0] = backEnd.or.axis[0][1];
	uniform.csmModelY[1] = backEnd.or.axis[1][1];
	uniform.csmModelY[2] = backEnd.or.axis[2][1];
	uniform.csmModelY[3] = backEnd.or.origin[1];
	uniform.csmModelZ[0] = backEnd.or.axis[0][2];
	uniform.csmModelZ[1] = backEnd.or.axis[1][2];
	uniform.csmModelZ[2] = backEnd.or.axis[2][2];
	uniform.csmModelZ[3] = backEnd.or.origin[2];

	VectorCopy( cascade->axis[0], uniform.csmAxisX );
	uniform.csmAxisX[3] = cascade->bounds[0][0];
	VectorCopy( cascade->axis[1], uniform.csmAxisY );
	uniform.csmAxisY[3] = cascade->bounds[0][1];
	VectorCopy( cascade->axis[2], uniform.csmAxisZ );
	uniform.csmAxisZ[3] = cascade->bounds[0][2];

	uniform.csmInvExtents[0] = 1.0f / extentX;
	uniform.csmInvExtents[1] = 1.0f / extentY;
	uniform.csmInvExtents[2] = 1.0f / extentZ;
	uniform.csmInvExtents[3] = R_ShadowClampReceiverBias( tr.csm.receiverBias ) / extentX;

	uniform.csmSplitAtlas[0] = cascade->splitNear;
	uniform.csmSplitAtlas[1] = cascade->splitFar;
	uniform.csmSplitAtlas[2] = (float)( cascadeIndex * cascadeSize ) / (float)atlasWidth;
	uniform.csmSplitAtlas[3] = (float)cascadeSize / (float)atlasWidth;

	uniform.csmShadowColor[0] = tr.csm.lightColor[0] * tr.csm.shadowStrength;
	uniform.csmShadowColor[1] = tr.csm.lightColor[1] * tr.csm.shadowStrength;
	uniform.csmShadowColor[2] = tr.csm.lightColor[2] * tr.csm.shadowStrength;
	uniform.csmShadowColor[3] = 0.0f;

	VectorCopy( backEnd.viewParms.or.axis[0], uniform.csmView );
	uniform.csmView[3] = -DotProduct( backEnd.viewParms.or.origin, backEnd.viewParms.or.axis[0] );

	VK_CSMShadowFilterOffsets( &filterInner, &filterOuter );
	uniform.texFactors[2] = filterInner;
	uniform.texFactors[3] = filterOuter;

	if ( VK_PushUniform( &uniform ) == ~0U ) {
		return;
	}

	vk_material_init( &material );
	vk_material_set_descriptor( &material, VK_DESC_TEXTURE0, vk_csm_shadow_descriptor() );
	vk_bind_material( &material );

	vk_bind_pipeline( vk.csm_shadow_pipeline );
	vk_bind_index();
	vk_bind_geometry( TESS_XYZ );
	vk_draw_geometry( tess.depthRange, qtrue );
}

void VK_LightingPass( void )
{
	static uint32_t uniform_offset;
	static int fog_stage;
	uint32_t pipeline;
	const shaderStage_t *pStage;
	const shadowSpotLightPlan_t *spotPlan;
	VkDescriptorSet shadowDescriptor = VK_NULL_HANDLE;
	cullType_t cull;
	int abs_light;

	if ( tess.shader->lightingStage < 0 )
		return;

	pStage = tess.xstages[ tess.shader->lightingStage ];

	// fog, light and texture scaling are all surface-dependent in the shader path.
	VK_SetFogParams( &uniform, &fog_stage );
	VK_SetLightParams( &uniform, tess.light );
	VK_SetTextureFactors( &uniform, pStage, tess.shader->lightingBundle );
	spotPlan = R_ShadowManagerSpotPlanForDlight( &tr.shadowManager, tess.light );

	uniform_offset = VK_PushUniform( &uniform );
	tess.dlightUpdateParams = qfalse;

	if ( uniform_offset == ~0 )
		return; // no space left...

	cull = tess.shader->cullType;
	if ( R_ViewPassIsMirror( &backEnd.viewParms ) ) {
		switch ( cull ) {
			case CT_FRONT_SIDED: cull = CT_BACK_SIDED; break;
			case CT_BACK_SIDED: cull = CT_FRONT_SIDED; break;
			default: break;
		}
	}

	abs_light = /* (pStage->stateBits & GLS_ATEST_BITS) && */ (cull == CT_TWO_SIDED) ? 1 : 0;

	if ( tess.light->linear )
		pipeline = vk.dlight1_pipelines_x[cull][tess.shader->polygonOffset][fog_stage][abs_light];
	else
		pipeline = vk.dlight_pipelines_x[cull][tess.shader->polygonOffset][fog_stage][abs_light];

	if ( tr.shadowManager.planned ) {
		if ( spotPlan && tr.shadowManager.spotAtlasPublication.published ) {
			shadowDescriptor = vk_spot_shadow_descriptor();
		} else if ( tr.shadowManager.pointAtlasPublication.published ) {
			shadowDescriptor = vk.dlight_shadow_descriptor;
		}
	} else if ( vk_dlight_shadow_atlas_available() ) {
		shadowDescriptor = vk.dlight_shadow_descriptor;
	}

	GL_SelectTexture( 0 );
	{
		vk_material_t material;

		vk_material_init( &material );
		vk_material_set_descriptor( &material, VK_DESC_TEXTURE0, R_AnimatedImageDescriptor( &pStage->bundle[ tess.shader->lightingBundle ] ) );
		vk_material_set_descriptor( &material, VK_DESC_TEXTURE2, shadowDescriptor );
		if ( fog_stage ) {
			vk_material_set_descriptor( &material, VK_DESC_FOG_DLIGHT, tr.fogImage->descriptor );
		}
		vk_bind_material( &material );
	}

#ifdef USE_VBO
	if ( tess.vboIndex == 0 )
#endif
	{
		R_ComputeTexCoords( tess.shader->lightingBundle, &pStage->bundle[ tess.shader->lightingBundle ] );
	}

	vk_bind_pipeline( pipeline );
	vk_bind_index();
	vk_bind_lighting( tess.shader->lightingStage, tess.shader->lightingBundle );
	vk_draw_geometry( tess.depthRange, qtrue );
}
#endif // USE_PMLIGHT


#ifdef USE_VULKAN
static qboolean VK_LiquidEffectActive( const shaderCommands_t *input )
{
	return input && input->shader && input->numPasses > 0 &&
		input->numIndexes > 0 && input->numVertexes > 0 &&
		vk.fboActive && vk.liquidSnapshot.color_descriptor != VK_NULL_HANDLE &&
		r_liquid && r_liquid->integer > 0 &&
		R_LiquidContentsEnabled( input->liquidContentFlags, r_liquid->integer ) &&
		input->liquidSort >= SS_FOG &&
		R_LiquidShaderSupported( input->shader ) &&
		( ( r_liquidRefraction && r_liquidRefraction->value > 0.0f ) ||
			( r_liquidReflection && r_liquidReflection->value > 0.0f ) ) &&
		R_LiquidViewportCoversTarget( backEnd.viewParms.viewportX,
			backEnd.viewParms.viewportY, backEnd.viewParms.viewportWidth,
			backEnd.viewParms.viewportHeight, glConfig.vidWidth, glConfig.vidHeight ) &&
		vk.renderPassIndex == RENDER_PASS_MAIN &&
		!( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) &&
		!backEnd.screenshotCubeActive && !backEnd.screenshotCubeFrontPending &&
		!R_ViewPassIsPortal( &backEnd.viewParms ) && !glConfig.stereoEnabled ? qtrue : qfalse;
}


static void VK_DrawLiquidPass( const shaderCommands_t *input, qboolean refractionBase )
{
	vkUniform_t liquid;
	vec_t *slots[18];
	vk_material_t material;
	const float rippleStrength = r_liquidRipples ? r_liquidRipples->value : 0.0f;
	const float heightScale = R_LiquidViewHeightScale( glConfig.vidHeight );
	vec3_t fresnelColor;
	float liquidMvp[16];
	int rippleCount = 0;
	int i;
	uint32_t pipeline;

	if ( !VK_LiquidEffectActive( input ) ||
		( refractionBase && ( !backEnd.liquidScreenMapDone ||
			( r_liquidRefraction && r_liquidRefraction->value <= 0.0f ) ) ) ||
		( !refractionBase && r_liquidReflection && r_liquidReflection->value <= 0.0f ) ) {
		return;
	}

	Com_Memset( &liquid, 0, sizeof( liquid ) );
	VectorCopy( backEnd.or.viewOrigin, liquid.eyePos );

	/* The liquid shaders use the first eighteen vec4 slots after eyePos.
	 * Keeping this mapping explicit makes the shared vkUniform_t layout
	 * auditable: params, info, eight impulses, two amplitude packs, the
	 * reflection color/weight, the depth-reject controls, and the
	 * model-to-clip matrix columns. */
	slots[0] = liquid.light.pos;
	slots[1] = liquid.light.color;
	slots[2] = liquid.light.vector;
	slots[3] = liquid.fogDistanceVector;
	slots[4] = liquid.fogDepthVector;
	slots[5] = liquid.fogEyeT;
	slots[6] = liquid.fogColor;
	slots[7] = liquid.texFactors;
	slots[8] = liquid.depthFadeInfo;
	slots[9] = liquid.depthFadeScale;
	slots[10] = liquid.depthFadeBias;
	slots[11] = liquid.dlightFactors;
	slots[12] = liquid.csmModelX;
	slots[13] = liquid.csmModelY;
	slots[14] = liquid.csmModelZ;
	slots[15] = liquid.csmAxisX;
	slots[16] = liquid.csmAxisY;
	slots[17] = liquid.csmAxisZ;

	/* R_LiquidWarpPixels maps the archived cvar through LIQUID_WARP_TO_PIXELS
	 * and scales it to the active view height. */
	slots[0][0] = R_LiquidWaveTime( backEnd.refdef.floatTime );
	slots[0][1] = r_liquidWarpScale ?
		R_LiquidWarpPixels( r_liquidWarpScale->value, glConfig.vidHeight ) : 0.0f;
	slots[0][2] = refractionBase ?
		( r_liquidRefraction ? Com_Clamp( 0.0f, 1.0f, r_liquidRefraction->value ) : 1.0f ) :
		( r_liquidReflection ? Com_Clamp( 0.0f, 1.0f, r_liquidReflection->value ) : 0.0f );
	slots[0][3] = glConfig.vidWidth > 0 ? 1.0f / (float)glConfig.vidWidth : 1.0f;
	slots[1][0] = R_LiquidContentsReflectionScale( input->liquidContentFlags );
	slots[1][2] = glConfig.vidHeight > 0 ? 1.0f / (float)glConfig.vidHeight : 1.0f;
	slots[1][3] = refractionBase ? 1.0f : 0.0f;

	if ( refractionBase && rippleStrength > 0.0f ) {
		for ( i = 0; i < backEnd.refdef.numLiquidInteractions &&
			rippleCount < LIQUID_MAX_ACTIVE_IMPULSES; i++ ) {
			const liquidInteraction_t *interaction = &backEnd.refdef.liquidInteractions[i];
			float age;
			float life;

			if ( !R_LiquidInteractionActive( interaction, backEnd.refdef.time ) ) {
				continue;
			}
			age = (float)( (int64_t)backEnd.refdef.time -
				(int64_t)interaction->time ) * 0.001f;
			life = 1.0f - age * ( 1000.0f / (float)LIQUID_IMPULSE_LIFETIME_MSEC );
			R_LiquidWorldToLocal( interaction->origin, backEnd.or.origin,
				backEnd.or.axis, slots[2 + rippleCount] );
			slots[2 + rippleCount][3] = interaction->radius + age * 150.0f;
			slots[10 + rippleCount / 4][rippleCount & 3] =
				interaction->strength * rippleStrength * life *
				LIQUID_RIPPLE_PIXEL_SCALE * heightScale;
			rippleCount++;
		}
	}
	slots[1][1] = (float)rippleCount;

	R_LiquidContentsFresnelColor( input->liquidContentFlags, fresnelColor );
	VectorCopy( fresnelColor, slots[12] );
	slots[12][3] = backEnd.liquidScreenMapDone ? LIQUID_REFLECT_INTENSITY : 0.0f;
	slots[13][0] = vk_depth_fade_ready() ? 1.0f : 0.0f;
	vk_get_liquid_mvp( liquidMvp );
	Com_Memcpy( slots[14], liquidMvp + 0, sizeof( vec4_t ) );
	Com_Memcpy( slots[15], liquidMvp + 4, sizeof( vec4_t ) );
	Com_Memcpy( slots[16], liquidMvp + 8, sizeof( vec4_t ) );
	Com_Memcpy( slots[17], liquidMvp + 12, sizeof( vec4_t ) );

	if ( VK_PushUniform( &liquid ) == ~0U ) {
		return;
	}
	vk_material_init( &material );
	vk_material_set_descriptor( &material, VK_DESC_TEXTURE0,
		vk.liquidSnapshot.color_descriptor );
	/* The liquid pipelines statically use the depth set; bind the copied
	 * scene depth when it exists and let the NULL fallback (white image)
	 * satisfy the layout when it does not - the uniform toggle keeps the
	 * shader from acting on it. */
	vk_material_set_descriptor( &material, VK_DESC_DEPTH_FADE,
		vk_depth_fade_ready() ? vk.depth_fade_descriptor : VK_NULL_HANDLE );
	vk_bind_material( &material );

	pipeline = vk.liquid_pipelines[input->shader->cullType]
		[input->shader->polygonOffset ? 1 : 0]
		[R_ViewPassIsMirror( &backEnd.viewParms ) ? 1 : 0];
	vk_bind_pipeline( pipeline );
	vk_bind_index();
	vk_bind_geometry( TESS_XYZ | TESS_NNN );
	vk_draw_geometry( tess.depthRange, qtrue );
}
#endif


void RB_StageIteratorGeneric( void )
{
#ifdef USE_VULKAN
	qboolean rebindIndex = qfalse;
#endif
	qboolean fogCollapse = qfalse;

#ifdef USE_VBO
	if ( tess.vboIndex != 0 ) {
		VBO_PrepareQueues();
		tess.vboStage = 0;
	} else
#endif
	{
		RB_DeformTessGeometry();
#ifdef USE_PMLIGHT
		RB_ApplyDlightShadowCasterNormalBias();
#endif
	}

#ifdef USE_PMLIGHT
	if ( tess.dlightPass ) {
		VK_LightingPass();
		return;
	}
#endif

#ifdef USE_FOG_COLLAPSE
	fogCollapse = tess.fogNum && tess.shader->fogPass && tess.shader->fogCollapse;
	if ( VK_LiquidEffectActive( &tess ) ) {
		fogCollapse = qfalse;
	}
#endif

	/* Seed transparent liquid faces with a warped copy of the captured scene.
	 * The authored material stages then tint and animate the refraction. */
#ifdef USE_VULKAN
	VK_DrawLiquidPass( &tess, qtrue );
#endif

	// call shader function
	RB_IterateStagesGeneric( &tess, fogCollapse );

#ifdef USE_VULKAN
	VK_DrawLiquidPass( &tess, qfalse );
#endif

	// now do any dynamic lighting needed
#ifdef USE_LEGACY_DLIGHTS
#ifdef USE_PMLIGHT
	if ( r_dlightMode->integer == 0 )
#endif
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE && !(tess.shader->surfaceFlags & (SURF_NODLIGHT | SURF_SKY) ) ) {
		if ( !fogCollapse ) {
#ifdef USE_VULKAN
			rebindIndex = ProjectDlightTexture();
#else	
			ProjectDlightTexture();
#endif
		}
	}
#endif // USE_LEGACY_DLIGHTS

	// now do fog
	if ( tess.fogNum && tess.shader->fogPass && !fogCollapse ) {
#ifdef USE_VULKAN
		RB_FogPass( rebindIndex );
#else
		RB_FogPass();
#endif
	}
}

#else

/*
** RB_StageIteratorGeneric
*/
void RB_StageIteratorGeneric( void )
{
	const shaderCommands_t *input;
	shader_t		*shader;

	RB_DeformTessGeometry();

	input = &tess;
	shader = input->shader;

	//
	// set face culling appropriately
	//
	GL_Cull( shader->cullType );

	// set polygon offset if necessary
	if ( shader->polygonOffset )
	{
		qglEnable( GL_POLYGON_OFFSET_FILL );
		qglPolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
	}

	//
	// if there is only a single pass then we can enable color
	// and texture arrays before we compile, otherwise we need
	// to avoid compiling those arrays since they will change
	// during multipass rendering
	//
	if ( tess.numPasses > 1 )
	{
		setArraysOnce = qfalse;

		GL_ClientState( 1, CLS_NONE );
		GL_ClientState( 0, CLS_NONE );
	}
	else
	{
		// FIXME: we can't do that if going to lighting/fog later?
		setArraysOnce = qtrue;

		GL_ClientState( 0, CLS_COLOR_ARRAY | CLS_TEXCOORD_ARRAY );

		if ( tess.xstages[0] )
		{
			R_ComputeColors( 0, tess.svars.colors, tess.xstages[0] );
			qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.svars.colors[0].rgba );
			R_ComputeTexCoords( 0, &tess.xstages[0]->bundle[0] );
			qglTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoordPtr[0] );
			if ( shader->multitextureEnv )
			{
				GL_ClientState( 1, CLS_TEXCOORD_ARRAY );
				R_ComputeTexCoords( 1, &tess.xstages[0]->bundle[1] );
				qglTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoordPtr[1] );
			}
			else
			{
				GL_ClientState( 1, CLS_NONE );
			}
		}
	}

	qglVertexPointer( 3, GL_FLOAT, sizeof( input->xyz[0] ), input->xyz ); // padded for SIMD

	//
	// lock XYZ
	//
	if ( qglLockArraysEXT )
	{
		qglLockArraysEXT( 0, input->numVertexes );
	}

	//
	// call shader function
	//
	RB_IterateStagesGeneric( input );

	//
	// now do any dynamic lighting needed
	//
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE && !(tess.shader->surfaceFlags & (SURF_NODLIGHT | SURF_SKY) ) )
	{
		ProjectDlightTexture();
	}

	//
	// now do fog
	//
	if ( tess.fogNum && tess.shader->fogPass )
	{
		RB_FogPass();
	}

	//
	// unlock arrays
	//
	if ( qglUnlockArraysEXT )
	{
		qglUnlockArraysEXT();
	}

	GL_ClientState( 1, CLS_NONE );

	//
	// reset polygon offset
	//
	if ( shader->polygonOffset )
	{
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}
}
#endif // !USE_VULKAN


/*
** RB_EndSurface
*/
void RB_EndSurface( void ) {
	const shaderCommands_t *input;

	input = &tess;

	if ( input->numIndexes == 0 ) {
		//VBO_UnBind();
		return;
	}

	if ( input->numIndexes > SHADER_MAX_INDEXES ) {
		ri.Error( ERR_DROP, "RB_EndSurface() - SHADER_MAX_INDEXES hit" );
	}

	if ( input->numVertexes > SHADER_MAX_VERTEXES ) {
		ri.Error( ERR_DROP, "RB_EndSurface() - SHADER_MAX_VERTEXES hit" );
	}

	if ( tess.shader == tr.shadowShader ) {
		RB_ShadowTessEnd();
		return;
	}

	if ( tess.shader == tr.enemyRimShader ) {
		RB_EnemyRimTessEnd();
		// clear the batch: a later defensive RB_EndSurface would otherwise
		// re-draw it with whatever modelview matrix is current by then
		tess.numIndexes = 0;
		tess.numVertexes = 0;
		return;
	}

	if ( tess.shader == tr.enemyOutlineShader ) {
		RB_EnemyOutlineTessEnd();
		tess.numIndexes = 0;
		tess.numVertexes = 0;
		return;
	}

#ifdef USE_PMLIGHT
	if ( tess.csmShadowPass ) {
		VK_CSMShadowPass();
		tess.csmShadowPass = qfalse;
		tess.numIndexes = 0;
		tess.numVertexes = 0;
#ifdef USE_VBO
		tess.vboIndex = 0;
#endif
		return;
	}
#endif

	if ( R_QLSkipBatch( tess.numIndexes ) ) {
		tess.numIndexes = 0;
		tess.numVertexes = 0;
#ifdef USE_VBO
		tess.vboIndex = 0;
#endif
		return;
	}

	// for debugging of sort order issues, stop rendering after a given sort value
	if ( r_debugSort->integer && r_debugSort->integer < tess.shader->sort
		&& qlRendererCvars.debugSortExcept->integer != (int)tess.shader->sort
		&& !backEnd.doneSurfaces ) {
#ifdef USE_VBO
		tess.vboIndex = 0; //VBO_UnBind();
#endif
		return;
	}

	//
	// update performance counters
	//
#ifdef USE_PMLIGHT
	if ( tess.dlightPass ) {
		backEnd.pc.c_lit_batches++;
		backEnd.pc.c_lit_vertices += tess.numVertexes;
		backEnd.pc.c_lit_indices += tess.numIndexes;
	} else
#endif
	{
		backEnd.pc.c_shaders++;
		backEnd.pc.c_vertexes += tess.numVertexes;
		backEnd.pc.c_indexes += tess.numIndexes;
	}
	backEnd.pc.c_totalIndexes += tess.numIndexes * tess.numPasses;

	//
	// call off to shader specific tess end function
	//
	tess.shader->optimalStageIteratorFunc();
	RB_CelOutlineTessEnd();

	//
	// draw debugging stuff
	//
	if ( r_showtris->integer || ( qlRendererCvars.debugShaderIndex->integer > 0
		&& qlRendererCvars.debugShaderIndex->integer == tess.shader->index ) ) {
		DrawTris( input );
	}
	if ( r_shownormals->integer ) {
		DrawNormals( input );
	}

	// clear shader so we can tell we don't have any unclosed surfaces
	tess.numIndexes = 0;
	tess.numVertexes = 0;

#ifdef USE_VBO
	tess.vboIndex = 0;
	//VBO_ClearQueue();
#endif
}
