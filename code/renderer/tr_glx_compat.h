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

#ifndef TR_GLX_COMPAT_H
#define TR_GLX_COMPAT_H

#include "tr_local.h"
#include "../renderercommon/tr_glx_bridge.h"

#ifdef RENDERER_GLX
#ifndef GL_ARRAY_BUFFER_BINDING_ARB
#define GL_ARRAY_BUFFER_BINDING_ARB 0x8894
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER_BINDING_ARB
#define GL_ELEMENT_ARRAY_BUFFER_BINDING_ARB 0x8895
#endif
#endif

/*
 * Legacy-renderer GLx compatibility adapter.
 *
 * Shared ABI forwarding and non-GLX stubs live in renderercommon/tr_glx_bridge.h.
 * Keep only legacy renderer state conversion and fixed-function draw adapters here
 * so the remaining compatibility substrate is explicit and easier to carve away.
 */

static ID_INLINE unsigned int GLX_CompatEntityDynamicCategoryMask( const refEntity_t *entity )
{
#ifdef RENDERER_GLX
	if ( !entity ) {
		return GLX_DYNAMIC_CATEGORY_MASK_ENTITY;
	}

	switch ( entity->reType ) {
	case RT_BEAM:
	case RT_RAIL_CORE:
	case RT_RAIL_RINGS:
	case RT_LIGHTNING:
		return GLX_DYNAMIC_CATEGORY_MASK_BEAM;
	default:
		break;
	}

	if ( entity->renderfx & ( RF_FIRST_PERSON | RF_DEPTHHACK ) ) {
		return GLX_DYNAMIC_CATEGORY_MASK_WEAPON;
	}
	return GLX_DYNAMIC_CATEGORY_MASK_ENTITY;
#else
	(void)entity;
	return 0u;
#endif
}

static ID_INLINE unsigned int GLX_CompatPolyDynamicCategoryMaskForShader( const shader_t *shader,
	int numVertexes )
{
#ifdef RENDERER_GLX
	if ( shader && ( shader->polygonOffset || shader->sort <= SS_DECAL ) ) {
		return GLX_DYNAMIC_CATEGORY_MASK_MARK;
	}
	if ( numVertexes <= 4 && shader && shader->sort >= SS_BLEND0 ) {
		return GLX_DYNAMIC_CATEGORY_MASK_PARTICLE;
	}
	return GLX_DYNAMIC_CATEGORY_MASK_POLY;
#else
	(void)shader;
	(void)numVertexes;
	return 0u;
#endif
}

static ID_INLINE unsigned int GLX_CompatPolyDynamicCategoryMask( const shaderCommands_t *input )
{
#ifdef RENDERER_GLX
	return GLX_CompatPolyDynamicCategoryMaskForShader( input ? input->shader : NULL,
		input ? input->numVertexes : 0 );
#else
	(void)input;
	return 0u;
#endif
}

static ID_INLINE unsigned int GLX_CompatDynamicCategoryMaskForTess( const shaderCommands_t *input,
	int materialFlags )
{
#ifdef RENDERER_GLX
	unsigned int categoryMask = input ? input->glxDynamicCategoryMask : 0u;

	if ( materialFlags & GLX_STAGE_BEAM_PASS ) {
		categoryMask |= GLX_DYNAMIC_CATEGORY_MASK_BEAM;
	}
	if ( materialFlags & ( GLX_STAGE_SHADOW_PASS | GLX_STAGE_POSTPROCESS_PASS ) ) {
		categoryMask |= GLX_DYNAMIC_CATEGORY_MASK_SPECIAL;
	}
	if ( materialFlags & GLX_STAGE_DLIGHT_MAP ) {
		categoryMask |= GLX_DYNAMIC_CATEGORY_MASK_DLIGHT;
	}
	if ( backEnd.currentEntity == &backEnd.entity2D || backEnd.projection2D ) {
		categoryMask |= GLX_DYNAMIC_CATEGORY_MASK_UI;
	} else if ( backEnd.currentEntity && backEnd.currentEntity != &tr.worldEntity ) {
		categoryMask |= GLX_CompatEntityDynamicCategoryMask( &backEnd.currentEntity->e );
	}
	if ( input && input->surfType == SF_POLY ) {
		categoryMask |= GLX_CompatPolyDynamicCategoryMask( input );
	}
	if ( !categoryMask ) {
		categoryMask = GLX_DYNAMIC_CATEGORY_MASK_SPECIAL;
	}
	return categoryMask;
#else
	(void)input;
	(void)materialFlags;
	return 0u;
#endif
}

static ID_INLINE void GLX_CompatMarkDynamicCategory( unsigned int categoryMask )
{
#ifdef RENDERER_GLX
	tess.glxDynamicCategoryMask |= categoryMask & GLX_DYNAMIC_CATEGORY_MASK_ALL;
#else
	(void)categoryMask;
#endif
}

static ID_INLINE qboolean GLX_CompatTryStreamDrawArrayPass( int vertexCount,
	const void *xyz, int xyzStride, unsigned int primitive, int materialFlags,
	unsigned int categoryMask )
{
#ifdef RENDERER_GLX
	glxStreamReservation_t reservation;
	qboolean ok = qtrue;
	int xyzBytes;
	int totalBytes;
	unsigned int oldArrayBuffer = 0;

	if ( !GLX_CompatStreamDrawEnabled() ) {
		return qfalse;
	}
	if ( !qglBindBufferARB ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_NO_BIND_BUFFER );
		return qfalse;
	}
	if ( vertexCount <= 0 || !xyz || xyzStride <= 0 ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_BAD_INPUT );
		return qfalse;
	}

	xyzBytes = vertexCount * xyzStride;
	totalBytes = GLX_CompatAlignInt( xyzBytes, 64 );

	if ( !GLX_CompatStreamReserve( totalBytes, 64, &reservation ) ) {
		GLX_CompatRecordStreamDrawResult( vertexCount, vertexCount,
			totalBytes, 0, 0, qfalse, qfalse, qfalse, materialFlags, categoryMask, qfalse );
		return qfalse;
	}

	if ( !GLX_CompatStreamUploadAt( &reservation, 0, xyz, xyzBytes ) ) {
		ok = qfalse;
	}
	GLX_CompatStreamCommit( &reservation );

	if ( !ok ) {
		GLX_CompatRecordStreamDrawResult( vertexCount, vertexCount,
			totalBytes, 0, 0, qfalse, qfalse, qfalse, materialFlags, categoryMask, qfalse );
		return qfalse;
	}

	oldArrayBuffer = GLX_CompatBindStreamArrayBuffer( reservation.buffer );

	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_NONE );
	qglVertexPointer( 3, GL_FLOAT, xyzStride, (const GLvoid *)(intptr_t)( reservation.offset ) );

	if ( !GLX_CompatDrawArraysClassified( primitive, 0, vertexCount,
		GLX_LEGACY_DELEGATION_NONE, GLX_DRAW_STREAM_GENERIC, materialFlags, categoryMask ) ) {
		ok = qfalse;
	}

	GLX_CompatRestoreStreamArrayBuffer( 0 );
	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_NONE );
	qglVertexPointer( 3, GL_FLOAT, xyzStride, xyz );
	GLX_CompatRestoreStreamArrayBuffer( oldArrayBuffer );

	GLX_CompatRecordStreamDrawResult( vertexCount, vertexCount,
		totalBytes, 0, 0, qfalse, qfalse, qfalse, materialFlags, categoryMask, ok );
	return ok;
#else
	(void)vertexCount;
	(void)xyz;
	(void)xyzStride;
	(void)primitive;
	(void)materialFlags;
	(void)categoryMask;
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatTryStreamDrawArrayTexcoordPass( int vertexCount,
	const void *xyz, int xyzStride, const void *texcoords, int texcoordStride,
	unsigned int primitive, int materialFlags, unsigned int categoryMask )
{
#ifdef RENDERER_GLX
	glxStreamReservation_t reservation;
	qboolean ok = qtrue;
	int xyzBytes;
	int texcoordElementStride;
	int texcoordBytes;
	int texcoordOffset;
	int totalBytes;
	unsigned int oldArrayBuffer = 0;

	if ( !GLX_CompatStreamDrawEnabled() ) {
		return qfalse;
	}
	if ( !qglBindBufferARB ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_NO_BIND_BUFFER );
		return qfalse;
	}
	if ( vertexCount <= 0 || !xyz || xyzStride <= 0 || !texcoords || texcoordStride < 0 ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_BAD_INPUT );
		return qfalse;
	}

	xyzBytes = vertexCount * xyzStride;
	texcoordElementStride = texcoordStride > 0 ? texcoordStride : (int)( 2 * sizeof( float ) );
	texcoordBytes = vertexCount * texcoordElementStride;
	texcoordOffset = GLX_CompatAlignInt( xyzBytes, 64 );
	totalBytes = GLX_CompatAlignInt( texcoordOffset + texcoordBytes, 64 );

	if ( !GLX_CompatStreamReserve( totalBytes, 64, &reservation ) ) {
		GLX_CompatRecordStreamDrawResult( vertexCount, vertexCount,
			totalBytes, 0, 0, qfalse, qfalse, qfalse, materialFlags, categoryMask, qfalse );
		return qfalse;
	}

	if ( !GLX_CompatStreamUploadAt( &reservation, 0, xyz, xyzBytes ) ) {
		ok = qfalse;
	}
	if ( !GLX_CompatStreamUploadAt( &reservation, texcoordOffset, texcoords, texcoordBytes ) ) {
		ok = qfalse;
	}
	GLX_CompatStreamCommit( &reservation );

	if ( !ok ) {
		GLX_CompatRecordStreamDrawResult( vertexCount, vertexCount,
			totalBytes, 0, 0, qfalse, qfalse, qfalse, materialFlags, categoryMask, qfalse );
		return qfalse;
	}

	oldArrayBuffer = GLX_CompatBindStreamArrayBuffer( reservation.buffer );

	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_TEXCOORD_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, xyzStride, (const GLvoid *)(intptr_t)( reservation.offset ) );
	qglTexCoordPointer( 2, GL_FLOAT, texcoordStride,
		(const GLvoid *)(intptr_t)( reservation.offset + texcoordOffset ) );

	if ( !GLX_CompatDrawArraysClassified( primitive, 0, vertexCount,
		GLX_LEGACY_DELEGATION_NONE, GLX_DRAW_STREAM_GENERIC, materialFlags, categoryMask ) ) {
		ok = qfalse;
	}

	GLX_CompatRestoreStreamArrayBuffer( 0 );
	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_TEXCOORD_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, xyzStride, xyz );
	qglTexCoordPointer( 2, GL_FLOAT, texcoordStride, texcoords );
	GLX_CompatRestoreStreamArrayBuffer( oldArrayBuffer );

	GLX_CompatRecordStreamDrawResult( vertexCount, vertexCount,
		totalBytes, 0, 0, qfalse, qfalse, qfalse, materialFlags, categoryMask, ok );
	return ok;
#else
	(void)vertexCount;
	(void)xyz;
	(void)xyzStride;
	(void)texcoords;
	(void)texcoordStride;
	(void)primitive;
	(void)materialFlags;
	(void)categoryMask;
	return qfalse;
#endif
}

static ID_INLINE int GLX_CompatArrayElementBytes( int components, unsigned int type )
{
	if ( components <= 0 ) {
		return 0;
	}

	switch ( type ) {
	case GL_FLOAT:
		return components * (int)sizeof( float );
	case GL_UNSIGNED_BYTE:
		return components * (int)sizeof( byte );
	default:
		return 0;
	}
}

static ID_INLINE qboolean GLX_CompatTryStreamDrawArrayTexcoordColorPass( int vertexCount,
	const void *xyz, int xyzStride, const void *texcoords, int texcoordStride,
	const void *colors, int colorComponents, unsigned int colorType, int colorStride,
	unsigned int primitive, int materialFlags, unsigned int categoryMask )
{
#ifdef RENDERER_GLX
	glxStreamReservation_t reservation;
	qboolean ok = qtrue;
	int xyzBytes;
	int texcoordElementStride;
	int texcoordBytes;
	int colorElementBytes;
	int colorElementStride;
	int colorBytes;
	int texcoordOffset;
	int colorOffset;
	int totalBytes;
	unsigned int oldArrayBuffer = 0;

	if ( !GLX_CompatStreamDrawEnabled() ) {
		return qfalse;
	}
	if ( !qglBindBufferARB ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_NO_BIND_BUFFER );
		return qfalse;
	}

	colorElementBytes = GLX_CompatArrayElementBytes( colorComponents, colorType );
	if ( vertexCount <= 0 || !xyz || xyzStride <= 0 || !texcoords || texcoordStride < 0 ||
		!colors || colorElementBytes <= 0 || colorStride < 0 ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_BAD_INPUT );
		return qfalse;
	}

	xyzBytes = vertexCount * xyzStride;
	texcoordElementStride = texcoordStride > 0 ? texcoordStride : (int)( 2 * sizeof( float ) );
	texcoordBytes = vertexCount * texcoordElementStride;
	colorElementStride = colorStride > 0 ? colorStride : colorElementBytes;
	colorBytes = vertexCount * colorElementStride;
	texcoordOffset = GLX_CompatAlignInt( xyzBytes, 64 );
	colorOffset = GLX_CompatAlignInt( texcoordOffset + texcoordBytes, 64 );
	totalBytes = GLX_CompatAlignInt( colorOffset + colorBytes, 64 );

	if ( !GLX_CompatStreamReserve( totalBytes, 64, &reservation ) ) {
		GLX_CompatRecordStreamDrawResult( vertexCount, vertexCount,
			totalBytes, 0, 0, qfalse, qfalse, qfalse, materialFlags, categoryMask, qfalse );
		return qfalse;
	}

	if ( !GLX_CompatStreamUploadAt( &reservation, 0, xyz, xyzBytes ) ) {
		ok = qfalse;
	}
	if ( !GLX_CompatStreamUploadAt( &reservation, texcoordOffset, texcoords, texcoordBytes ) ) {
		ok = qfalse;
	}
	if ( !GLX_CompatStreamUploadAt( &reservation, colorOffset, colors, colorBytes ) ) {
		ok = qfalse;
	}
	GLX_CompatStreamCommit( &reservation );

	if ( !ok ) {
		GLX_CompatRecordStreamDrawResult( vertexCount, vertexCount,
			totalBytes, 0, 0, qfalse, qfalse, qfalse, materialFlags, categoryMask, qfalse );
		return qfalse;
	}

	oldArrayBuffer = GLX_CompatBindStreamArrayBuffer( reservation.buffer );

	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, xyzStride, (const GLvoid *)(intptr_t)( reservation.offset ) );
	qglTexCoordPointer( 2, GL_FLOAT, texcoordStride,
		(const GLvoid *)(intptr_t)( reservation.offset + texcoordOffset ) );
	qglColorPointer( colorComponents, colorType, colorStride,
		(const GLvoid *)(intptr_t)( reservation.offset + colorOffset ) );

	if ( !GLX_CompatDrawArraysClassified( primitive, 0, vertexCount,
		GLX_LEGACY_DELEGATION_NONE, GLX_DRAW_STREAM_GENERIC, materialFlags, categoryMask ) ) {
		ok = qfalse;
	}

	GLX_CompatRestoreStreamArrayBuffer( 0 );
	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, xyzStride, xyz );
	qglTexCoordPointer( 2, GL_FLOAT, texcoordStride, texcoords );
	qglColorPointer( colorComponents, colorType, colorStride, colors );
	GLX_CompatRestoreStreamArrayBuffer( oldArrayBuffer );

	GLX_CompatRecordStreamDrawResult( vertexCount, vertexCount,
		totalBytes, 0, 0, qfalse, qfalse, qfalse, materialFlags, categoryMask, ok );
	return ok;
#else
	(void)vertexCount;
	(void)xyz;
	(void)xyzStride;
	(void)texcoords;
	(void)texcoordStride;
	(void)colors;
	(void)colorComponents;
	(void)colorType;
	(void)colorStride;
	(void)primitive;
	(void)materialFlags;
	(void)categoryMask;
	return qfalse;
#endif
}

static ID_INLINE int GLX_CompatMaterialStageFlags( const shaderStage_t *pStage )
{
	int flags = 0;

#ifdef RENDERER_GLX
	if ( !pStage ) {
		return 0;
	}

	if ( pStage->mtEnv ) {
		flags |= GLX_STAGE_MULTITEXTURE;
	}
	if ( pStage->depthFragment ) {
		flags |= GLX_STAGE_DEPTH_FRAGMENT;
	}
	if ( pStage->stateBits & GLS_BLEND_BITS ) {
		flags |= GLX_STAGE_BLEND;
	}
	if ( pStage->stateBits & GLS_ATEST_BITS ) {
		flags |= GLX_STAGE_ALPHA_TEST;
	}
	if ( pStage->stateBits & GLS_DEPTHMASK_TRUE ) {
		flags |= GLX_STAGE_DEPTH_WRITE;
	}
	if ( pStage->bundle[0].lightmap != LIGHTMAP_INDEX_NONE ||
		pStage->bundle[1].lightmap != LIGHTMAP_INDEX_NONE ) {
		flags |= GLX_STAGE_LIGHTMAP;
	}
	if ( pStage->bundle[0].numImageAnimations > 1 ||
		pStage->bundle[1].numImageAnimations > 1 ) {
		flags |= GLX_STAGE_ANIMATED_IMAGE;
	}
	if ( pStage->bundle[0].isVideoMap || pStage->bundle[1].isVideoMap ) {
		flags |= GLX_STAGE_VIDEO_MAP;
	}
	if ( pStage->bundle[0].isScreenMap || pStage->bundle[1].isScreenMap ) {
		flags |= GLX_STAGE_SCREEN_MAP;
	}
	if ( pStage->bundle[0].dlight || pStage->bundle[1].dlight ) {
		flags |= GLX_STAGE_DLIGHT_MAP;
	}
	if ( pStage->isDetail ) {
		flags |= GLX_STAGE_DETAIL;
	}
	if ( pStage->bundle[0].numTexMods || pStage->bundle[1].numTexMods ) {
		flags |= GLX_STAGE_TEXMOD;
	}
	if ( pStage->tessFlags & ( TESS_ENV0 | TESS_ENV1 ) ) {
		flags |= GLX_STAGE_ENVIRONMENT;
	}
	if ( pStage->tessFlags & TESS_ST0 ) {
		flags |= GLX_STAGE_ST0;
	}
	if ( pStage->tessFlags & TESS_ST1 ) {
		flags |= GLX_STAGE_ST1;
	}
#else
	(void)pStage;
#endif

	return flags;
}

static ID_INLINE int GLX_CompatMaterialCombineForGLEnv( int mtEnv )
{
	switch ( mtEnv ) {
	case GL_MODULATE:
		return GLX_MATERIAL_COMBINE_MODULATE;
	case GL_ADD:
		return GLX_MATERIAL_COMBINE_ADD;
	case GL_REPLACE:
		return GLX_MATERIAL_COMBINE_REPLACE;
	case GL_DECAL:
		return GLX_MATERIAL_COMBINE_DECAL;
	default:
		return GLX_MATERIAL_COMBINE_INVALID;
	}
}

static ID_INLINE unsigned int GLX_CompatMaterialTexModBit( texMod_t type )
{
	switch ( type ) {
	case TMOD_NONE:
		return GLX_MATERIAL_TMOD_NONE_BIT;
	case TMOD_TRANSFORM:
		return GLX_MATERIAL_TMOD_TRANSFORM_BIT;
	case TMOD_TURBULENT:
		return GLX_MATERIAL_TMOD_TURBULENT_BIT;
	case TMOD_SCROLL:
		return GLX_MATERIAL_TMOD_SCROLL_BIT;
	case TMOD_SCALE:
		return GLX_MATERIAL_TMOD_SCALE_BIT;
	case TMOD_STRETCH:
		return GLX_MATERIAL_TMOD_STRETCH_BIT;
	case TMOD_ROTATE:
		return GLX_MATERIAL_TMOD_ROTATE_BIT;
	case TMOD_ENTITY_TRANSLATE:
		return GLX_MATERIAL_TMOD_ENTITY_TRANSLATE_BIT;
	case TMOD_OFFSET:
		return GLX_MATERIAL_TMOD_OFFSET_BIT;
	case TMOD_SCALE_OFFSET:
		return GLX_MATERIAL_TMOD_SCALE_OFFSET_BIT;
	case TMOD_OFFSET_SCALE:
		return GLX_MATERIAL_TMOD_OFFSET_SCALE_BIT;
	default:
		return GLX_MATERIAL_TMOD_UNKNOWN_BIT;
	}
}

static ID_INLINE unsigned int GLX_CompatMaterialTexModOpcode( texMod_t type )
{
	switch ( type ) {
	case TMOD_NONE:
		return GLX_MATERIAL_TMOD_OPCODE_NONE;
	case TMOD_TRANSFORM:
		return GLX_MATERIAL_TMOD_OPCODE_TRANSFORM;
	case TMOD_TURBULENT:
		return GLX_MATERIAL_TMOD_OPCODE_TURBULENT;
	case TMOD_SCROLL:
		return GLX_MATERIAL_TMOD_OPCODE_SCROLL;
	case TMOD_SCALE:
		return GLX_MATERIAL_TMOD_OPCODE_SCALE;
	case TMOD_STRETCH:
		return GLX_MATERIAL_TMOD_OPCODE_STRETCH;
	case TMOD_ROTATE:
		return GLX_MATERIAL_TMOD_OPCODE_ROTATE;
	case TMOD_ENTITY_TRANSLATE:
		return GLX_MATERIAL_TMOD_OPCODE_ENTITY_TRANSLATE;
	case TMOD_OFFSET:
		return GLX_MATERIAL_TMOD_OPCODE_OFFSET;
	case TMOD_SCALE_OFFSET:
		return GLX_MATERIAL_TMOD_OPCODE_SCALE_OFFSET;
	case TMOD_OFFSET_SCALE:
		return GLX_MATERIAL_TMOD_OPCODE_OFFSET_SCALE;
	default:
		return GLX_MATERIAL_TMOD_OPCODE_UNKNOWN;
	}
}

static ID_INLINE int GLX_CompatMaterialWaveFunc( genFunc_t func )
{
	switch ( func ) {
	case GF_NONE:
		return GLX_MATERIAL_WAVEFUNC_NONE;
	case GF_SIN:
		return GLX_MATERIAL_WAVEFUNC_SIN;
	case GF_SQUARE:
		return GLX_MATERIAL_WAVEFUNC_SQUARE;
	case GF_TRIANGLE:
		return GLX_MATERIAL_WAVEFUNC_TRIANGLE;
	case GF_SAWTOOTH:
		return GLX_MATERIAL_WAVEFUNC_SAWTOOTH;
	case GF_INVERSE_SAWTOOTH:
		return GLX_MATERIAL_WAVEFUNC_INVERSE_SAWTOOTH;
	case GF_NOISE:
		return GLX_MATERIAL_WAVEFUNC_NOISE;
	default:
		return GLX_MATERIAL_WAVEFUNC_NONE;
	}
}

static ID_INLINE int GLX_CompatMaterialFogAdjust( acff_t adjust )
{
	switch ( adjust ) {
	case ACFF_NONE:
		return GLX_MATERIAL_FOG_ADJUST_NONE;
	case ACFF_MODULATE_RGB:
		return GLX_MATERIAL_FOG_ADJUST_MODULATE_RGB;
	case ACFF_MODULATE_RGBA:
		return GLX_MATERIAL_FOG_ADJUST_MODULATE_RGBA;
	case ACFF_MODULATE_ALPHA:
		return GLX_MATERIAL_FOG_ADJUST_MODULATE_ALPHA;
	default:
		return GLX_MATERIAL_FOG_ADJUST_NONE;
	}
}

static ID_INLINE unsigned int GLX_CompatMaterialTexModMask( const textureBundle_t *bundle )
{
	unsigned int mask = 0;
	int i;

	if ( !bundle || !bundle->texMods ) {
		return bundle && bundle->numTexMods > 0 ? GLX_MATERIAL_TMOD_UNKNOWN_BIT : 0;
	}

	for ( i = 0; i < bundle->numTexMods; ++i ) {
		mask |= GLX_CompatMaterialTexModBit( bundle->texMods[ i ].type );
	}

	return mask;
}

static ID_INLINE unsigned int GLX_CompatMaterialTexModSequence( const textureBundle_t *bundle )
{
	unsigned int sequence = 0;
	int i;

	if ( !bundle || !bundle->texMods ) {
		return bundle && bundle->numTexMods > 0 ? GLX_MATERIAL_TMOD_OPCODE_UNKNOWN : 0;
	}

	for ( i = 0; i < bundle->numTexMods && i < GLX_MATERIAL_TMOD_SEQUENCE_MAX_SLOTS; ++i ) {
		const unsigned int opcode = GLX_CompatMaterialTexModOpcode( bundle->texMods[ i ].type );
		sequence |= ( opcode & GLX_MATERIAL_TMOD_SEQUENCE_SLOT_MASK ) <<
			( i * GLX_MATERIAL_TMOD_SEQUENCE_SLOT_BITS );
	}

	return sequence;
}

static ID_INLINE unsigned int GLX_CompatMaterialTexModWaveFuncs( const textureBundle_t *bundle )
{
	unsigned int waveFuncs = 0;
	int i;

	if ( !bundle || !bundle->texMods ) {
		return 0;
	}

	for ( i = 0; i < bundle->numTexMods && i < GLX_MATERIAL_TMOD_SEQUENCE_MAX_SLOTS; ++i ) {
		if ( bundle->texMods[ i ].type == TMOD_STRETCH ) {
			const unsigned int waveFunc =
				(unsigned int)GLX_CompatMaterialWaveFunc( bundle->texMods[ i ].wave.func );
			waveFuncs |= ( waveFunc & GLX_MATERIAL_TMOD_SEQUENCE_SLOT_MASK ) <<
				( i * GLX_MATERIAL_TMOD_SEQUENCE_SLOT_BITS );
		}
	}

	return waveFuncs;
}

static ID_INLINE void GLX_CompatRecordMaterialStage( const shaderStage_t *pStage,
	int path, int numVertexes, int numIndexes )
{
#ifdef RENDERER_GLX
	if ( !pStage ) {
		return;
	}

	GLX_Renderer_RecordMaterialStage( path, GLX_CompatMaterialStageFlags( pStage ),
		pStage->stateBits, pStage->rgbGen, pStage->alphaGen,
		pStage->bundle[0].tcGen, pStage->bundle[1].tcGen,
		pStage->bundle[0].numTexMods, pStage->bundle[1].numTexMods,
		GLX_CompatMaterialTexModMask( &pStage->bundle[0] ),
		GLX_CompatMaterialTexModMask( &pStage->bundle[1] ),
		GLX_CompatMaterialTexModSequence( &pStage->bundle[0] ),
		GLX_CompatMaterialTexModSequence( &pStage->bundle[1] ),
		pStage->rgbGen == CGEN_WAVEFORM ?
			GLX_CompatMaterialWaveFunc( pStage->rgbWave.func ) : GLX_MATERIAL_WAVEFUNC_NONE,
		pStage->alphaGen == AGEN_WAVEFORM ?
			GLX_CompatMaterialWaveFunc( pStage->alphaWave.func ) : GLX_MATERIAL_WAVEFUNC_NONE,
		GLX_CompatMaterialTexModWaveFuncs( &pStage->bundle[0] ),
		GLX_CompatMaterialTexModWaveFuncs( &pStage->bundle[1] ),
		GLX_CompatMaterialFogAdjust( pStage->adjustColorsForFog ),
		GLX_CompatMaterialCombineForGLEnv( pStage->mtEnv ), qfalse,
		numVertexes, numIndexes );
#else
	(void)pStage;
	(void)path;
	(void)numVertexes;
	(void)numIndexes;
#endif
}

static ID_INLINE qboolean GLX_CompatBindMaterialStage( int flags, unsigned int stateBits,
	int rgbGen, int alphaGen, int tcGen0, int tcGen1, int texMods0, int texMods1,
	unsigned int texModTypes0, unsigned int texModTypes1,
	unsigned int texModSequence0, unsigned int texModSequence1,
	int rgbWaveFunc, int alphaWaveFunc,
	unsigned int texModWaveFuncs0, unsigned int texModWaveFuncs1, int fogAdjust,
	int multitextureEnv, qboolean fogPass )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_BindMaterialStage( flags, stateBits, rgbGen, alphaGen,
		tcGen0, tcGen1, texMods0, texMods1, texModTypes0, texModTypes1,
		texModSequence0, texModSequence1, rgbWaveFunc, alphaWaveFunc,
		texModWaveFuncs0, texModWaveFuncs1, fogAdjust,
		GLX_CompatMaterialCombineForGLEnv( multitextureEnv ), fogPass );
#else
	(void)flags;
	(void)stateBits;
	(void)rgbGen;
	(void)alphaGen;
	(void)tcGen0;
	(void)tcGen1;
	(void)texMods0;
	(void)texMods1;
	(void)texModTypes0;
	(void)texModTypes1;
	(void)texModSequence0;
	(void)texModSequence1;
	(void)rgbWaveFunc;
	(void)alphaWaveFunc;
	(void)texModWaveFuncs0;
	(void)texModWaveFuncs1;
	(void)fogAdjust;
	(void)multitextureEnv;
	(void)fogPass;
	return qfalse;
#endif
}

#endif // TR_GLX_COMPAT_H
