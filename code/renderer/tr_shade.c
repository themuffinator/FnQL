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
#include "tr_glx_compat.h"

#ifndef GL_COMBINE
#define GL_COMBINE 0x8570
#define GL_COMBINE_RGB 0x8571
#define GL_COMBINE_ALPHA 0x8572
#define GL_SOURCE0_RGB 0x8580
#define GL_SOURCE0_ALPHA 0x8588
#define GL_PRIMARY_COLOR 0x8577
#define GL_OPERAND0_RGB 0x8590
#define GL_OPERAND0_ALPHA 0x8598
#endif

/*

  THIS ENTIRE FILE IS BACK END

  This file deals with applying shaders to surface data in the tess struct.
*/


/*
==================
R_DrawElements
==================
*/
void R_DrawElements( int numIndexes, const glIndex_t *indexes ) {
#ifdef RENDERER_GLX
	GLX_CompatDrawElements( GL_TRIANGLES, numIndexes, GL_INDEX_TYPE, indexes,
		GLX_LEGACY_DELEGATION_GENERIC, GLX_DRAW_GENERIC );
#else
	qglDrawElements( GL_TRIANGLES, numIndexes, GL_INDEX_TYPE, indexes );
#endif
}


/*
=============================================================

SURFACE SHADERS

=============================================================
*/

shaderCommands_t	tess;
static qboolean	setArraysOnce;

#ifdef RENDERER_GLX
static qboolean GLX_StreamDrawStageMaterialAllowed( const shaderStage_t *pStage,
	int materialFlags, GLint multitextureEnv )
{
	return GLX_CompatStreamDrawAllowsMaterial( materialFlags, pStage->stateBits,
		pStage->rgbGen, pStage->alphaGen, pStage->bundle[0].tcGen,
		pStage->bundle[1].tcGen,
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
		GLX_CompatMaterialCombineForGLEnv( multitextureEnv ), qfalse );
}

static qboolean GLX_TryStreamDrawStage( const shaderCommands_t *input, const shaderStage_t *pStage, qboolean multitexture, GLint multitextureEnv )
{
	glxStreamReservation_t reservation;
	qboolean ok = qtrue;
	qboolean glxMaterialBound = qfalse;
	int xyzBytes;
	int colorBytes;
	int texBytes;
	int tex1Bytes;
	int indexBytes;
	int colorOffset;
	int texOffset;
	int tex1Offset;
	int indexOffset;
	int totalBytes;
	int materialFlags;
	unsigned int categoryMask;
	unsigned int oldArrayBuffer = 0;
	unsigned int oldElementArrayBuffer = 0;

	if ( !GLX_CompatStreamDrawEnabled() ) {
		return qfalse;
	}
	if ( !qglBindBufferARB ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_NO_BIND_BUFFER );
		return qfalse;
	}
	if ( !input || !pStage ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_BAD_INPUT );
		return qfalse;
	}

	materialFlags = GLX_CompatMaterialStageFlags( pStage );
	categoryMask = GLX_CompatDynamicCategoryMaskForTess( input, materialFlags );
	if ( multitexture && !GLX_CompatStreamDrawMultitextureEnabled() ) {
		GLX_StreamDrawStageMaterialAllowed( pStage, materialFlags, multitextureEnv );
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_MULTITEXTURE );
		return qfalse;
	}
	if ( pStage->depthFragment && !GLX_CompatStreamDrawDepthFragmentEnabled() ) {
		GLX_StreamDrawStageMaterialAllowed( pStage, materialFlags, multitextureEnv );
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_DEPTH_FRAGMENT );
		return qfalse;
	}
	if ( !input->svars.texcoordPtr[0] ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_NO_TEXCOORDS );
		return qfalse;
	}
	if ( multitexture && !input->svars.texcoordPtr[1] ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_NO_TEXCOORDS );
		return qfalse;
	}
	if ( input->numVertexes <= 0 || input->numIndexes <= 0 ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_EMPTY_BATCH );
		return qfalse;
	}

	if ( !GLX_StreamDrawStageMaterialAllowed( pStage, materialFlags, multitextureEnv ) ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_MATERIAL_KEY );
		return qfalse;
	}

	xyzBytes = input->numVertexes * (int)sizeof( input->xyz[0] );
	colorBytes = input->numVertexes * (int)sizeof( input->svars.colors[0] );
	texBytes = input->numVertexes * (int)sizeof( input->svars.texcoordPtr[0][0] );
	tex1Bytes = multitexture ? input->numVertexes * (int)sizeof( input->svars.texcoordPtr[1][0] ) : 0;
	indexBytes = input->numIndexes * (int)sizeof( input->indexes[0] );
	colorOffset = GLX_CompatAlignInt( xyzBytes, 16 );
	texOffset = GLX_CompatAlignInt( colorOffset + colorBytes, 16 );
	tex1Offset = GLX_CompatAlignInt( texOffset + texBytes, 16 );
	indexOffset = GLX_CompatAlignInt( tex1Offset + tex1Bytes, 16 );
	totalBytes = GLX_CompatAlignInt( indexOffset + indexBytes, 64 );

	if ( !GLX_CompatStreamReserve( totalBytes, 64, &reservation ) ) {
		GLX_CompatRecordStreamDrawResult( input->numVertexes, input->numIndexes,
			totalBytes, indexBytes, tex1Bytes, multitexture, qfalse, pStage->depthFragment,
			materialFlags, categoryMask, qfalse );
		return qfalse;
	}

	if ( !GLX_CompatStreamUploadAt( &reservation, 0, input->xyz, xyzBytes ) ) {
		ok = qfalse;
	}
	if ( ok && !GLX_CompatStreamUploadAt( &reservation, colorOffset, input->svars.colors, colorBytes ) ) {
		ok = qfalse;
	}
	if ( ok && !GLX_CompatStreamUploadAt( &reservation, texOffset, input->svars.texcoordPtr[0], texBytes ) ) {
		ok = qfalse;
	}
	if ( ok && multitexture && !GLX_CompatStreamUploadAt( &reservation, tex1Offset, input->svars.texcoordPtr[1], tex1Bytes ) ) {
		ok = qfalse;
	}
	if ( ok && !GLX_CompatStreamUploadAt( &reservation, indexOffset, input->indexes, indexBytes ) ) {
		ok = qfalse;
	}
	GLX_CompatStreamCommit( &reservation );

	if ( !ok ) {
		GLX_CompatRecordStreamDrawResult( input->numVertexes, input->numIndexes,
			totalBytes, indexBytes, tex1Bytes, multitexture, qfalse, pStage->depthFragment,
			materialFlags, categoryMask, qfalse );
		return qfalse;
	}

	if ( GLX_CompatMaterialRendererActive() ) {
		GL_ProgramDisable();
		glxMaterialBound = GLX_CompatBindMaterialStage( materialFlags, pStage->stateBits,
			pStage->rgbGen, pStage->alphaGen, pStage->bundle[0].tcGen, pStage->bundle[1].tcGen,
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
			multitextureEnv, qfalse );
		if ( !glxMaterialBound ) {
			GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_MATERIAL_PROGRAM );
			GLX_CompatRecordStreamDrawResult( input->numVertexes, input->numIndexes,
				totalBytes, indexBytes, tex1Bytes, multitexture, qfalse, pStage->depthFragment,
				materialFlags, categoryMask, qfalse );
			return qfalse;
		}
	}

	oldArrayBuffer = GLX_CompatBindStreamArrayBuffer( reservation.buffer );
	oldElementArrayBuffer = GLX_CompatBindStreamElementArrayBuffer( reservation.buffer );

	GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, sizeof( input->xyz[0] ), (const GLvoid *)(intptr_t)( reservation.offset ) );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, (const GLvoid *)(intptr_t)( reservation.offset + colorOffset ) );
	qglTexCoordPointer( 2, GL_FLOAT, 0, (const GLvoid *)(intptr_t)( reservation.offset + texOffset ) );
	if ( multitexture ) {
		GL_ClientState( 1, CLS_TEXCOORD_ARRAY );
		qglTexCoordPointer( 2, GL_FLOAT, 0, (const GLvoid *)(intptr_t)( reservation.offset + tex1Offset ) );
	} else {
		GL_ClientState( 1, CLS_NONE );
	}

	if ( !GLX_CompatDrawElementsClassified( GL_TRIANGLES, input->numIndexes, GL_INDEX_TYPE,
		(const GLvoid *)(intptr_t)( reservation.offset + indexOffset ),
		GLX_LEGACY_DELEGATION_NONE, GLX_DRAW_STREAM_GENERIC, materialFlags, categoryMask ) ) {
		ok = qfalse;
	}

	if ( glxMaterialBound && pStage->depthFragment ) {
		GLX_CompatUnbindMaterial();
		glxMaterialBound = qfalse;
	}

	if ( pStage->depthFragment ) {
		GL_State( pStage->stateBits | GLS_DEPTHMASK_TRUE );
		GL_ProgramEnable();
		if ( !GLX_CompatDrawElementsClassified( GL_TRIANGLES, input->numIndexes, GL_INDEX_TYPE,
			(const GLvoid *)(intptr_t)( reservation.offset + indexOffset ),
			GLX_LEGACY_DELEGATION_NONE, GLX_DRAW_STREAM_GENERIC, materialFlags, categoryMask ) ) {
			ok = qfalse;
		}
		GL_ProgramDisable();
	}

	if ( glxMaterialBound ) {
		GLX_CompatUnbindMaterial();
	}

	GLX_CompatRestoreStreamElementArrayBuffer( oldElementArrayBuffer );
	GLX_CompatRestoreStreamArrayBuffer( 0 );
	GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, sizeof( input->xyz[0] ), input->xyz );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, input->svars.colors[0].rgba );
	qglTexCoordPointer( 2, GL_FLOAT, 0, input->svars.texcoordPtr[0] );
	if ( multitexture ) {
		GL_ClientState( 1, CLS_TEXCOORD_ARRAY );
		qglTexCoordPointer( 2, GL_FLOAT, 0, input->svars.texcoordPtr[1] );
	} else {
		GL_ClientState( 1, CLS_NONE );
		GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );
	}
	GLX_CompatRestoreStreamArrayBuffer( oldArrayBuffer );

	GLX_CompatRecordStreamDrawResult( input->numVertexes, input->numIndexes,
		totalBytes, indexBytes, tex1Bytes, multitexture, qfalse, pStage->depthFragment,
		materialFlags, categoryMask, ok );
	return ok;
}

static qboolean GLX_TryStreamDrawFogPass( const shaderCommands_t *input )
{
	glxStreamReservation_t reservation;
	qboolean ok = qtrue;
	qboolean glxMaterialBound = qfalse;
	int xyzBytes;
	int colorBytes;
	int texBytes;
	int indexBytes;
	int colorOffset;
	int texOffset;
	int indexOffset;
	int totalBytes;
	unsigned int categoryMask;
	unsigned int oldArrayBuffer = 0;
	unsigned int oldElementArrayBuffer = 0;

	if ( !GLX_CompatStreamDrawEnabled() ) {
		return qfalse;
	}
	if ( !GLX_CompatStreamDrawFogEnabled() ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_FOG );
		return qfalse;
	}
	if ( !qglBindBufferARB ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_NO_BIND_BUFFER );
		return qfalse;
	}
	if ( !input ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_BAD_INPUT );
		return qfalse;
	}
	if ( input->numVertexes <= 0 || input->numIndexes <= 0 ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_EMPTY_BATCH );
		return qfalse;
	}

	categoryMask = GLX_CompatDynamicCategoryMaskForTess( input, 0 );
	xyzBytes = input->numVertexes * (int)sizeof( input->xyz[0] );
	colorBytes = input->numVertexes * (int)sizeof( input->svars.colors[0] );
	texBytes = input->numVertexes * (int)sizeof( input->svars.texcoords[0][0] );
	indexBytes = input->numIndexes * (int)sizeof( input->indexes[0] );
	colorOffset = GLX_CompatAlignInt( xyzBytes, 16 );
	texOffset = GLX_CompatAlignInt( colorOffset + colorBytes, 16 );
	indexOffset = GLX_CompatAlignInt( texOffset + texBytes, 16 );
	totalBytes = GLX_CompatAlignInt( indexOffset + indexBytes, 64 );

	if ( !GLX_CompatStreamReserve( totalBytes, 64, &reservation ) ) {
		GLX_CompatRecordStreamDrawResult( input->numVertexes, input->numIndexes,
			totalBytes, indexBytes, 0, qfalse, qtrue, qfalse, 0, categoryMask, qfalse );
		return qfalse;
	}

	if ( !GLX_CompatStreamUploadAt( &reservation, 0, input->xyz, xyzBytes ) ) {
		ok = qfalse;
	}
	if ( ok && !GLX_CompatStreamUploadAt( &reservation, colorOffset, input->svars.colors, colorBytes ) ) {
		ok = qfalse;
	}
	if ( ok && !GLX_CompatStreamUploadAt( &reservation, texOffset, input->svars.texcoords[0], texBytes ) ) {
		ok = qfalse;
	}
	if ( ok && !GLX_CompatStreamUploadAt( &reservation, indexOffset, input->indexes, indexBytes ) ) {
		ok = qfalse;
	}
	GLX_CompatStreamCommit( &reservation );

	if ( !ok ) {
		GLX_CompatRecordStreamDrawResult( input->numVertexes, input->numIndexes,
			totalBytes, indexBytes, 0, qfalse, qtrue, qfalse, 0, categoryMask, qfalse );
		return qfalse;
	}

	if ( GLX_CompatMaterialRendererActive() ) {
		GL_ProgramDisable();
		glxMaterialBound = GLX_CompatBindFogMaterial();
		if ( !glxMaterialBound ) {
			GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_MATERIAL_PROGRAM );
			GLX_CompatRecordStreamDrawResult( input->numVertexes, input->numIndexes,
				totalBytes, indexBytes, 0, qfalse, qtrue, qfalse, 0, categoryMask, qfalse );
			return qfalse;
		}
	}

	oldArrayBuffer = GLX_CompatBindStreamArrayBuffer( reservation.buffer );
	oldElementArrayBuffer = GLX_CompatBindStreamElementArrayBuffer( reservation.buffer );

	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, sizeof( input->xyz[0] ), (const GLvoid *)(intptr_t)( reservation.offset ) );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, (const GLvoid *)(intptr_t)( reservation.offset + colorOffset ) );
	qglTexCoordPointer( 2, GL_FLOAT, 0, (const GLvoid *)(intptr_t)( reservation.offset + texOffset ) );

	if ( !GLX_CompatDrawElementsClassified( GL_TRIANGLES, input->numIndexes, GL_INDEX_TYPE,
		(const GLvoid *)(intptr_t)( reservation.offset + indexOffset ),
		GLX_LEGACY_DELEGATION_NONE, GLX_DRAW_STREAM_GENERIC, 0, categoryMask ) ) {
		ok = qfalse;
	}

	if ( glxMaterialBound ) {
		GLX_CompatUnbindMaterial();
	}

	GLX_CompatRestoreStreamElementArrayBuffer( oldElementArrayBuffer );
	GLX_CompatRestoreStreamArrayBuffer( 0 );
	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, sizeof( input->xyz[0] ), input->xyz );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, input->svars.colors[0].rgba );
	qglTexCoordPointer( 2, GL_FLOAT, 0, input->svars.texcoords[0] );
	GLX_CompatRestoreStreamArrayBuffer( oldArrayBuffer );

	GLX_CompatRecordStreamDrawResult( input->numVertexes, input->numIndexes,
		totalBytes, indexBytes, 0, qfalse, qtrue, qfalse, 0, categoryMask, ok );
	return ok;
}

static qboolean GLX_TryStreamDrawDynamicLightPass( const shaderCommands_t *input,
	int numIndexes, const glIndex_t *indexes, const float *texCoords,
	const float *colors, unsigned int lightMask )
{
	glxStreamReservation_t reservation;
	qboolean ok = qtrue;
	qboolean projectedProgram;
	int xyzBytes;
	int colorBytes;
	int texBytes;
	int normalBytes;
	int indexBytes;
	int colorOffset;
	int texOffset;
	int normalOffset;
	int indexOffset;
	int totalBytes;
	int materialFlags;
	unsigned int categoryMask;
	unsigned int oldArrayBuffer = 0;
	unsigned int oldElementArrayBuffer = 0;

	if ( !GLX_CompatStreamDrawEnabled() ) {
		return qfalse;
	}
	if ( !qglBindBufferARB ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_NO_BIND_BUFFER );
		return qfalse;
	}
	if ( !input || !indexes || !texCoords || !colors ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_BAD_INPUT );
		return qfalse;
	}
	if ( input->numVertexes <= 0 || numIndexes <= 0 ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_EMPTY_BATCH );
		return qfalse;
	}

	materialFlags = GLX_STAGE_DLIGHT_MAP | GLX_STAGE_ST0;
	categoryMask = GLX_CompatDynamicCategoryMaskForTess( input, materialFlags );
	if ( !GLX_CompatStreamDrawAllowsMaterial( materialFlags, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		0, 0, 0, 0, 0, 0,
		GLX_MATERIAL_WAVEFUNC_NONE, GLX_MATERIAL_WAVEFUNC_NONE,
		0, 0, GLX_MATERIAL_FOG_ADJUST_NONE, 0, qfalse ) ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_MATERIAL_KEY );
		return qfalse;
	}

	// the projected-light shader replacement needs real normals for its
	// per-fragment front test; stream them only when it can take this draw
	projectedProgram = ( lightMask && GLX_CompatDlightProjectedProgramEnabled() ) ?
		qtrue : qfalse;

	xyzBytes = input->numVertexes * (int)sizeof( input->xyz[0] );
	colorBytes = input->numVertexes * 4 * (int)sizeof( colors[0] );
	texBytes = input->numVertexes * 2 * (int)sizeof( texCoords[0] );
	normalBytes = projectedProgram ? input->numVertexes * (int)sizeof( input->normal[0] ) : 0;
	indexBytes = numIndexes * (int)sizeof( indexes[0] );
	colorOffset = GLX_CompatAlignInt( xyzBytes, 16 );
	texOffset = GLX_CompatAlignInt( colorOffset + colorBytes, 16 );
	normalOffset = GLX_CompatAlignInt( texOffset + texBytes, 16 );
	indexOffset = GLX_CompatAlignInt( normalOffset + normalBytes, 16 );
	totalBytes = GLX_CompatAlignInt( indexOffset + indexBytes, 64 );

	if ( !GLX_CompatStreamReserve( totalBytes, 64, &reservation ) ) {
		GLX_CompatRecordStreamDrawResult( input->numVertexes, numIndexes,
			totalBytes, indexBytes, 0, qfalse, qfalse, qfalse, materialFlags, categoryMask, qfalse );
		return qfalse;
	}

	if ( !GLX_CompatStreamUploadAt( &reservation, 0, input->xyz, xyzBytes ) ) {
		ok = qfalse;
	}
	if ( ok && !GLX_CompatStreamUploadAt( &reservation, colorOffset, colors, colorBytes ) ) {
		ok = qfalse;
	}
	if ( ok && !GLX_CompatStreamUploadAt( &reservation, texOffset, texCoords, texBytes ) ) {
		ok = qfalse;
	}
	if ( ok && normalBytes > 0 &&
		!GLX_CompatStreamUploadAt( &reservation, normalOffset, input->normal, normalBytes ) ) {
		ok = qfalse;
	}
	if ( ok && !GLX_CompatStreamUploadAt( &reservation, indexOffset, indexes, indexBytes ) ) {
		ok = qfalse;
	}
	GLX_CompatStreamCommit( &reservation );
	GLX_CompatRecordStreamDlightReservation( &reservation );

	if ( !ok ) {
		GLX_CompatRecordStreamDrawResult( input->numVertexes, numIndexes,
			totalBytes, indexBytes, 0, qfalse, qfalse, qfalse, materialFlags, categoryMask, qfalse );
		return qfalse;
	}

	oldArrayBuffer = GLX_CompatBindStreamArrayBuffer( reservation.buffer );
	oldElementArrayBuffer = GLX_CompatBindStreamElementArrayBuffer( reservation.buffer );

	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY |
		( normalBytes > 0 ? CLS_NORMAL_ARRAY : 0 ) );
	qglVertexPointer( 3, GL_FLOAT, sizeof( input->xyz[0] ), (const GLvoid *)(intptr_t)( reservation.offset ) );
	qglColorPointer( 4, GL_FLOAT, 0, (const GLvoid *)(intptr_t)( reservation.offset + colorOffset ) );
	qglTexCoordPointer( 2, GL_FLOAT, 0, (const GLvoid *)(intptr_t)( reservation.offset + texOffset ) );
	if ( normalBytes > 0 ) {
		qglNormalPointer( GL_FLOAT, sizeof( input->normal[0] ),
			(const GLvoid *)(intptr_t)( reservation.offset + normalOffset ) );
	}

	if ( !GLX_CompatDrawElementsClassifiedProjectedDlights( GL_TRIANGLES,
		numIndexes, GL_INDEX_TYPE,
		(const GLvoid *)(intptr_t)( reservation.offset + indexOffset ),
		GLX_LEGACY_DELEGATION_NONE, GLX_DRAW_STREAM_GENERIC, materialFlags,
		categoryMask, lightMask ) ) {
		ok = qfalse;
	}

	GLX_CompatRestoreStreamElementArrayBuffer( oldElementArrayBuffer );
	GLX_CompatRestoreStreamArrayBuffer( 0 );
	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, sizeof( input->xyz[0] ), input->xyz );
	qglColorPointer( 4, GL_FLOAT, 0, colors );
	qglTexCoordPointer( 2, GL_FLOAT, 0, texCoords );
	GLX_CompatRestoreStreamArrayBuffer( oldArrayBuffer );

	GLX_CompatRecordStreamDrawResult( input->numVertexes, numIndexes,
		totalBytes, indexBytes, 0, qfalse, qfalse, qfalse, materialFlags, categoryMask, ok );
	return ok;
}

static qboolean GLX_TryStreamDrawLiquidPass( const shaderCommands_t *input )
{
	glxStreamReservation_t reservation;
	qboolean ok = qtrue;
	const int materialFlags = GLX_STAGE_SCREEN_MAP | GLX_STAGE_BLEND;
	unsigned int categoryMask;
	unsigned int oldArrayBuffer;
	unsigned int oldElementArrayBuffer;
	int xyzBytes;
	int normalBytes;
	int indexBytes;
	int normalOffset;
	int indexOffset;
	int totalBytes;

	/* Enhanced liquids are themselves an explicit opt-in. Use the GLx transient
	 * stream even when the developer-wide streamed-stage cvar is off: modern
	 * contexts cannot legally source this programmable pass from client memory. */
	if ( !qglBindBufferARB || !input ||
		input->numVertexes <= 0 || input->numIndexes <= 0 ) {
		return qfalse;
	}

	categoryMask = GLX_CompatDynamicCategoryMaskForTess( input, materialFlags );
	xyzBytes = input->numVertexes * (int)sizeof( input->xyz[0] );
	normalBytes = input->numVertexes * (int)sizeof( input->normal[0] );
	indexBytes = input->numIndexes * (int)sizeof( input->indexes[0] );
	normalOffset = GLX_CompatAlignInt( xyzBytes, 16 );
	indexOffset = GLX_CompatAlignInt( normalOffset + normalBytes, 16 );
	totalBytes = GLX_CompatAlignInt( indexOffset + indexBytes, 64 );

	if ( !GLX_CompatStreamReserve( totalBytes, 64, &reservation ) ) {
		return qfalse;
	}
	if ( !GLX_CompatStreamUploadAt( &reservation, 0, input->xyz, xyzBytes ) ||
		!GLX_CompatStreamUploadAt( &reservation, normalOffset,
			input->normal, normalBytes ) ||
		!GLX_CompatStreamUploadAt( &reservation, indexOffset,
			input->indexes, indexBytes ) ) {
		ok = qfalse;
	}
	GLX_CompatStreamCommit( &reservation );
	if ( !ok ) {
		return qfalse;
	}

	oldArrayBuffer = GLX_CompatBindStreamArrayBuffer( reservation.buffer );
	oldElementArrayBuffer = GLX_CompatBindStreamElementArrayBuffer( reservation.buffer );
	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_NORMAL_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, sizeof( input->xyz[0] ),
		(const GLvoid *)(intptr_t)reservation.offset );
	qglNormalPointer( GL_FLOAT, sizeof( input->normal[0] ),
		(const GLvoid *)(intptr_t)( reservation.offset + normalOffset ) );

	ok = GLX_CompatDrawElementsClassified( GL_TRIANGLES, input->numIndexes,
		GL_INDEX_TYPE, (const GLvoid *)(intptr_t)( reservation.offset + indexOffset ),
		GLX_LEGACY_DELEGATION_NONE, GLX_DRAW_STREAM_GENERIC,
		materialFlags, categoryMask );

	GLX_CompatRestoreStreamElementArrayBuffer( oldElementArrayBuffer );
	GLX_CompatRestoreStreamArrayBuffer( 0 );
	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, sizeof( input->xyz[0] ), input->xyz );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, input->svars.colors[0].rgba );
	qglTexCoordPointer( 2, GL_FLOAT, 0, input->svars.texcoords[0] );
	GLX_CompatRestoreStreamArrayBuffer( oldArrayBuffer );
	GLX_CompatRecordStreamDrawResult( input->numVertexes, input->numIndexes,
		totalBytes, indexBytes, 0, qfalse, qfalse, qfalse,
		materialFlags, categoryMask, ok );
	return ok;
}
#endif

/*
=================
R_BindAnimatedImage
=================
*/
void R_BindAnimatedImage( const textureBundle_t *bundle ) {
	int64_t index;
	double	v;

	if ( bundle->isVideoMap ) {
		ri.CIN_RunCinematic(bundle->videoMapHandle);
		ri.CIN_UploadCinematic(bundle->videoMapHandle);
		return;
	}

#ifdef USE_FBO
	if ( bundle->isScreenMap && backEnd.viewParms.frameSceneNum == 1 ) {
		GL_BindTexNum( FBO_ScreenTexture() );
		return;
	}
#endif

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

static qboolean RB_DepthFadeActive( const shaderStage_t *pStage )
{
	return pStage &&
		!pStage->depthFragment &&
		tess.shader->dfType > DFT_NONE &&
		tess.shader->dfType < DFT_TBD &&
		pStage->bundle[1].image[0] == NULL &&
		( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) == 0 &&
		( !backEnd.currentEntity || ( backEnd.currentEntity->e.renderfx & RF_DEPTHHACK ) == 0 ) &&
		GL_DepthFadeProgramAvailable();
}

static void RB_DrawDepthFadeStage( const shaderCommands_t *input )
{
#ifdef USE_FBO
	FBO_BindDepthFadeTexture( 1 );
	GL_DepthFadeProgramEnable( tess.shader );
	R_DrawElements( input->numIndexes, input->indexes );
	GL_ProgramDisable();
	GL_SelectTexture( 0 );
#endif
}


/*
================
DrawTris

Draws triangle outlines for debugging
================
*/
static void DrawTris( const shaderCommands_t *input ) {

	if ( r_showtris->integer == 1 && backEnd.drawConsole )
		return;

	if ( tess.numIndexes == 0 )
		return;

	GL_ProgramDisable();

#ifdef USE_PMLIGHT
	tess.dlightUpdateParams = qtrue;
#endif

	GL_ClientState( 0, CLS_NONE );
	qglDisable( GL_TEXTURE_2D );

#ifdef USE_PMLIGHT
	if ( tess.dlightPass )
		qglColor4f( 1.0f, 0.33f, 0.2f, 1.0f );
	else
#endif
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
}


/*
================
DrawNormals

Draws vertex normals for debugging
================
*/
static void DrawNormals( const shaderCommands_t *input ) {
	int		i;

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

#ifdef RENDERER_GLX
	GLX_CompatDrawArrays( GL_LINES, 0, tess.numVertexes * 2,
		GLX_LEGACY_DELEGATION_DRAW_ARRAY, GLX_DRAW_DEBUG );
#else
	qglDrawArrays( GL_LINES, 0, tess.numVertexes * 2 );
#endif

	if ( qglUnlockArraysEXT ) {
		qglUnlockArraysEXT();
	}

	qglEnable( GL_TEXTURE_2D );

	qglDepthRange( 0, 1 );
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

	if ( shader->remappedShader ) {
		state = shader->remappedShader;
	} else {
		state = shader;
	}
#ifdef USE_TESS_NEEDS_NORMAL
	liquidNeedsNormal = qfalse;
#ifdef USE_FBO
	liquidNeedsNormal = ( fboEnabled && FBO_LiquidScreenTexture() &&
		r_liquid && r_liquid->integer > 0 && shader->sort >= SS_FOG &&
		R_LiquidShaderSupported( state ) &&
			R_LiquidContentsEnabled( shader->contentFlags | state->contentFlags,
				r_liquid->integer ) ) ? qtrue : qfalse;
#endif
#endif

#ifdef USE_PMLIGHT
#ifdef RENDERER_GLX
	if ( tess.dlightPass && shader->isStaticShader && !shader->remappedShader &&
		GLX_LightingProgramEligible( state, fogNum ) )
		tess.allowVBO = qtrue;
	else if ( !tess.dlightPass && shader->isStaticShader && !shader->remappedShader )
		tess.allowVBO = qtrue;
	else
		tess.allowVBO = qfalse;
#else
	if ( !tess.dlightPass && shader->isStaticShader && !shader->remappedShader )
		tess.allowVBO = qtrue;
	else
		tess.allowVBO = qfalse;
#endif
#else
	if ( shader->isStaticShader )
		tess.allowVBO = qtrue;
	else
		tess.allowVBO = qfalse;
#endif

#ifdef USE_PMLIGHT
	if ( tess.fogNum != fogNum || tess.cullType != state->cullType ) {
		tess.dlightUpdateParams = qtrue;
	}
#endif

#ifdef USE_TESS_NEEDS_NORMAL
#ifdef USE_PMLIGHT
	tess.needsNormal = state->needsNormal || tess.dlightPass ||
		r_shownormals->integer || liquidNeedsNormal;
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
	tess.glxDynamicCategoryMask = 0;

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
static void DrawMultitextured( const shaderCommands_t *input, int stage ) {
	const shaderStage_t *pStage;
	qboolean glxStreamedDraw = qfalse;
	qboolean depthFade;
#ifdef RENDERER_GLX
	GLint glxMultitextureEnv;
#endif

	pStage = tess.xstages[ stage ];
	depthFade = RB_DepthFadeActive( pStage );

#ifdef RENDERER_GLX
	GLX_CompatRecordMaterialStage( pStage, GLX_STAGE_PATH_GENERIC, input->numVertexes, input->numIndexes );
#endif

	GL_State( pStage->stateBits );

	if ( !setArraysOnce ) {
		R_ComputeColors( pStage );
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
#ifdef RENDERER_GLX
		glxMultitextureEnv = GL_REPLACE;
#endif
	} else {
		GL_TexEnv( pStage->mtEnv );
#ifdef RENDERER_GLX
		glxMultitextureEnv = pStage->mtEnv;
#endif
	}

#ifdef RENDERER_GLX
	if ( !depthFade ) {
		glxStreamedDraw = GLX_TryStreamDrawStage( input, pStage, qtrue, glxMultitextureEnv );
	}
#endif
	if ( !glxStreamedDraw ) {
		if ( depthFade )
			RB_DrawDepthFadeStage( input );
		else
			R_DrawElements( input->numIndexes, input->indexes );
		if ( pStage->depthFragment )
		{
			GL_State( pStage->stateBits | GLS_DEPTHMASK_TRUE );
			GL_ProgramEnable();
			R_DrawElements( input->numIndexes, input->indexes );
			GL_ProgramDisable();
		}
	}

	//
	// disable texturing on TEXTURE1, then select TEXTURE0
	//
#ifdef USE_VBO
	if ( r_vbo->integer ) {
		// some drivers may try to load texcoord[1] data even with multi-texturing disabled
		// (and actually gpu shaders doesn't care about conventional GL_TEXTURE_2D states)
		// which doesn't cause problems while data pointer is the same or represents fixed-size set
		// but when we switch to/from vbo - texcoord[1] data may point on larger set (it's ok)
		// or smaller set - which will cause out-of-bounds index access/crash during non-multitexture rendering
		// GL_ClientState( 1, GLS_NONE );
	}
#endif // USE_VBO

	qglDisable( GL_TEXTURE_2D );

	GL_SelectTexture( 0 );
}


#ifdef USE_LEGACY_DLIGHTS
#ifdef RENDERER_GLX
static qboolean GLX_ProjectDlightScissorRect( const glIndex_t *indexes,
	int numIndexes, byte *visited, int *x, int *y, int *width, int *height )
{
	int i;
	int minX = 0x7fffffff;
	int minY = 0x7fffffff;
	int maxX = -0x7fffffff;
	int maxY = -0x7fffffff;
	int x0, y0, x1, y1;
	int scissorX1, scissorY1;
	vec4_t eye, clip, normalized, window;

	if ( !indexes || numIndexes <= 0 || !visited || !x || !y || !width || !height ||
		backEnd.viewParms.scissorWidth <= 0 ||
		backEnd.viewParms.scissorHeight <= 0 ) {
		return qfalse;
	}
	Com_Memset( visited, 0, tess.numVertexes * sizeof( *visited ) );

	for ( i = 0; i < numIndexes; i++ ) {
		unsigned int idx = indexes[i];
		int winX, winY;

		if ( idx >= (unsigned int)tess.numVertexes ) {
			return qfalse;
		}
		if ( visited[idx] ) {
			continue;
		}
		visited[idx] = 1;

		R_TransformModelToClip( tess.xyz[idx], backEnd.or.modelMatrix,
			backEnd.viewParms.projectionMatrix, eye, clip );
		if ( clip[3] <= 0.001f ) {
			return qfalse;
		}
		R_TransformClipToWindow( clip, &backEnd.viewParms, normalized, window );

		winX = backEnd.viewParms.viewportX + (int)window[0];
		winY = backEnd.viewParms.viewportY + (int)window[1];
		if ( winX < minX ) {
			minX = winX;
		}
		if ( winY < minY ) {
			minY = winY;
		}
		if ( winX > maxX ) {
			maxX = winX;
		}
		if ( winY > maxY ) {
			maxY = winY;
		}
	}

	scissorX1 = backEnd.viewParms.scissorX + backEnd.viewParms.scissorWidth;
	scissorY1 = backEnd.viewParms.scissorY + backEnd.viewParms.scissorHeight;
	x0 = minX - 1;
	y0 = minY - 1;
	x1 = maxX + 2;
	y1 = maxY + 2;

	if ( x0 < backEnd.viewParms.scissorX ) {
		x0 = backEnd.viewParms.scissorX;
	}
	if ( y0 < backEnd.viewParms.scissorY ) {
		y0 = backEnd.viewParms.scissorY;
	}
	if ( x1 > scissorX1 ) {
		x1 = scissorX1;
	}
	if ( y1 > scissorY1 ) {
		y1 = scissorY1;
	}
	if ( x1 <= x0 || y1 <= y0 ) {
		return qfalse;
	}

	*x = x0;
	*y = y0;
	*width = x1 - x0;
	*height = y1 - y0;
	return qtrue;
}
#endif

/*
===================
ProjectDlightTexture

Perform dynamic lighting with another rendering pass
===================
*/
static void ProjectDlightTexture( void ) {
	int		i, l;
	vec3_t	origin;
	float	*texCoords;
	float	*colors;
	qboolean glxStreamedDraw;
	byte	*clipBits;
	glIndex_t *hitIndexes;
	int		numIndexes;
	int		lightBit;
	int		glxLegacyLights = 0;
	int		glxLegacySkippedLights = 0;
	int		glxLegacyNoHitLights = 0;
	int		glxLegacyVertexes = 0;
	int		glxLegacyIndexes = 0;
	int		glxLegacyLitIndexes = 0;
	int		glxLegacyCullVertexes = 0;
	int		glxLegacyCullIndexes = 0;
	float	scale;
	float	radius;
	float	radiusHalf;
	float	color0, color1, color2;
	float	modulate = 0.0f;
	const dlight_t *dl;
	qboolean cullBackFaces;
	qboolean legacyDlightArraysBound = qfalse;
#ifdef RENDERER_GLX
	qboolean glxScissorEnabled = qfalse;
#endif

	if ( !backEnd.refdef.num_dlights ) {
		return;
	}
	clipBits = tess.dlightClipBits;
	hitIndexes = tess.dlightIndexes;
	cullBackFaces = !r_dlightBacks->integer;
#ifdef RENDERER_GLX
	glxScissorEnabled = GLX_CompatDlightScissorEnabled();
#endif

	for ( l = 0 ; l < backEnd.refdef.num_dlights ; l++ ) {
		lightBit = 1 << l;

		if ( !( tess.dlightBits & lightBit ) ) {
			glxLegacySkippedLights++;
			continue;	// this surface definitely doesn't have any of this light
		}
		glxLegacyLights++;
		glxLegacyVertexes += tess.numVertexes;
		glxLegacyIndexes += tess.numIndexes;
		texCoords = (float *)tess.svars.texcoords[0];
		tess.svars.texcoordPtr[0] = tess.svars.texcoords[0];
		colors = tess.dlightColors[0];

		dl = &backEnd.refdef.dlights[l];
		VectorCopy( dl->transformed, origin );
		radius = dl->radius;
		radiusHalf = radius * 0.5f;
		scale = 1.0f / radius;
		color0 = dl->color[0];
		color1 = dl->color[1];
		color2 = dl->color[2];

		for ( i = 0 ; i < tess.numVertexes ; i++, texCoords += 2, colors += 4 ) {
			int		clip = 0;
			vec3_t	dist;

			if ( !( tess.vertexDlightBits[i] & lightBit ) ) {
				clipBits[i] = 63;
				glxLegacyCullVertexes++;
				continue;
			}

			VectorSubtract( origin, tess.xyz[i], dist );

			backEnd.pc.c_dlightVertexes++;

			texCoords[0] = 0.5f + dist[0] * scale;
			texCoords[1] = 0.5f + dist[1] * scale;

			if ( cullBackFaces &&
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
					if ( dist[2] < radiusHalf ) {
						modulate = 1.0f;
					} else {
						modulate = 2.0f * (radius - dist[2]) * scale;
					}
				}
			}
			clipBits[i] = clip;
			colors[0] = color0 * modulate;
			colors[1] = color1 * modulate;
			colors[2] = color2 * modulate;
			colors[3] = 1.0f;
		}

		// build a list of triangles that need light
		numIndexes = 0;
		for ( i = 0 ; i < tess.numIndexes ; i += 3 ) {
			int		a, b, c;

			a = tess.indexes[i];
			b = tess.indexes[i+1];
			c = tess.indexes[i+2];
			if ( !( tess.vertexDlightBits[a] & tess.vertexDlightBits[b] &
				tess.vertexDlightBits[c] & lightBit ) ) {
				glxLegacyCullIndexes += 3;
				continue;
			}
			if ( clipBits[a] & clipBits[b] & clipBits[c] ) {
				continue;	// not lighted
			}
			hitIndexes[numIndexes] = a;
			hitIndexes[numIndexes+1] = b;
			hitIndexes[numIndexes+2] = c;
			numIndexes += 3;
		}

		if ( !numIndexes ) {
			glxLegacyNoHitLights++;
			continue;
		}
		glxLegacyLitIndexes += numIndexes;

#ifdef RENDERER_GLX
		{
			qboolean glxScissorComputed;
			qboolean glxScissorApplied;
			int glxScissorX = 0;
			int glxScissorY = 0;
			int glxScissorWidth = 0;
			int glxScissorHeight = 0;

			glxScissorComputed = qfalse;
			glxScissorApplied = qfalse;
			if ( glxScissorEnabled ) {
				glxScissorComputed = GLX_ProjectDlightScissorRect( hitIndexes,
					numIndexes, clipBits, &glxScissorX, &glxScissorY,
					&glxScissorWidth, &glxScissorHeight );
				glxScissorApplied = glxScissorComputed;
				GLX_CompatRecordDlightScissor( glxScissorComputed,
					glxScissorApplied, glxScissorX, glxScissorY,
					glxScissorWidth, glxScissorHeight,
					backEnd.viewParms.scissorWidth,
					backEnd.viewParms.scissorHeight );
				if ( glxScissorApplied ) {
					qglScissor( glxScissorX, glxScissorY,
						glxScissorWidth, glxScissorHeight );
				}
			}
#endif
		GLX_CompatRecordDlightState( GLX_DLIGHT_STATE_LEGACY_PASS );

		GL_Bind( tr.dlightImage );
		GLX_CompatRecordDlightState( GLX_DLIGHT_STATE_TEXTURE_BIND );

		// include GLS_DEPTHFUNC_EQUAL so alpha tested surfaces don't add light
		// where they aren't rendered

		if ( dl->additive ) {
			GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
		} else {
			GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
		}
		GLX_CompatRecordDlightState( GLX_DLIGHT_STATE_GL_STATE );

		glxStreamedDraw = qfalse;
#ifdef RENDERER_GLX
		// projected-light records are captured in world space, so only
		// world-entity batches may carry refs into the shader replacement
		glxStreamedDraw = GLX_TryStreamDrawDynamicLightPass( &tess, numIndexes,
			hitIndexes, (const float *)tess.svars.texcoords[0],
			tess.dlightColors[0],
			backEnd.currentEntity == &tr.worldEntity ? (unsigned int)lightBit : 0u );
#endif
		if ( glxStreamedDraw ) {
			legacyDlightArraysBound = qtrue;
		} else {
			if ( !legacyDlightArraysBound ) {
				GL_ClientState( 1, CLS_NONE );
				GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );
				qglTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords[0] );
				qglColorPointer( 4, GL_FLOAT, 0, tess.dlightColors );
				legacyDlightArraysBound = qtrue;
			}
			R_DrawElements( numIndexes, hitIndexes );
		}
#ifdef RENDERER_GLX
			if ( glxScissorApplied ) {
				qglScissor( backEnd.viewParms.scissorX,
					backEnd.viewParms.scissorY,
					backEnd.viewParms.scissorWidth,
					backEnd.viewParms.scissorHeight );
			}
		}
#endif

		backEnd.pc.c_totalIndexes += numIndexes;
		backEnd.pc.c_dlightIndexes += numIndexes;
	}
	GLX_CompatRecordDlightCull( glxLegacyCullVertexes, glxLegacyCullIndexes );
	GLX_CompatRecordDlightBuild( glxLegacyLights, glxLegacySkippedLights,
		glxLegacyNoHitLights, glxLegacyVertexes, glxLegacyIndexes,
		glxLegacyLitIndexes, 0, 0, 0 );
}
#endif // USE_LEGACY_DLIGHTS


/*
===================
RB_FogPass

Blends a fog texture on top of everything else
===================
*/
static void RB_FogPass( void ) {
	const fog_t *fog = tr.world->fogs + tess.fogNum;
	int i;
	qboolean glxStreamedDraw = qfalse;

	for ( i = 0; i < tess.numVertexes; i++ ) {
		tess.svars.colors[i] = fog->colorInt;
	}

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

#ifdef RENDERER_GLX
	glxStreamedDraw = GLX_TryStreamDrawFogPass( &tess );
#endif
	if ( !glxStreamedDraw ) {
		R_DrawElements( tess.numIndexes, tess.indexes );
	}
}


/*
===============
R_ComputeColors
===============
*/
void R_ComputeColors( const shaderStage_t *pStage )
{
	int		i;

	if ( tess.numVertexes == 0 )
		return;

	//
	// rgbGen
	//
	switch ( pStage->rgbGen )
	{
		case CGEN_IDENTITY:
			Com_Memset( tess.svars.colors, 0xff, tess.numVertexes * 4 );
			break;
		default:
		case CGEN_IDENTITY_LIGHTING:
			Com_Memset( tess.svars.colors, tr.identityLightByte, tess.numVertexes * 4 );
			break;
		case CGEN_LIGHTING_DIFFUSE:
			RB_CalcDiffuseColor( ( unsigned char * ) tess.svars.colors );
			break;
		case CGEN_EXACT_VERTEX:
			Com_Memcpy( tess.svars.colors, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
			break;
		case CGEN_CONST:
			for ( i = 0; i < tess.numVertexes; i++ ) {
				tess.svars.colors[i] = pStage->constantColor;
			}
			break;
		case CGEN_VERTEX:
			if ( tr.identityLight == 1 )
			{
				Com_Memcpy( tess.svars.colors, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
			}
			else
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					tess.svars.colors[i].rgba[0] = tess.vertexColors[i].rgba[0] * tr.identityLight;
					tess.svars.colors[i].rgba[1] = tess.vertexColors[i].rgba[1] * tr.identityLight;
					tess.svars.colors[i].rgba[2] = tess.vertexColors[i].rgba[2] * tr.identityLight;
					tess.svars.colors[i].rgba[3] = tess.vertexColors[i].rgba[3];
				}
			}
			break;
		case CGEN_ONE_MINUS_VERTEX:
			if ( tr.identityLight == 1 )
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					tess.svars.colors[i].rgba[0] = 255 - tess.vertexColors[i].rgba[0];
					tess.svars.colors[i].rgba[1] = 255 - tess.vertexColors[i].rgba[1];
					tess.svars.colors[i].rgba[2] = 255 - tess.vertexColors[i].rgba[2];
				}
			}
			else
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					tess.svars.colors[i].rgba[0] = ( 255 - tess.vertexColors[i].rgba[0] ) * tr.identityLight;
					tess.svars.colors[i].rgba[1] = ( 255 - tess.vertexColors[i].rgba[1] ) * tr.identityLight;
					tess.svars.colors[i].rgba[2] = ( 255 - tess.vertexColors[i].rgba[2] ) * tr.identityLight;
				}
			}
			break;
		case CGEN_FOG:
			{
				const fog_t *fog;

				fog = tr.world->fogs + tess.fogNum;

				for ( i = 0; i < tess.numVertexes; i++ ) {
					tess.svars.colors[i] = fog->colorInt;
				}
			}
			break;
		case CGEN_WAVEFORM:
			RB_CalcWaveColor( &pStage->rgbWave, tess.svars.colors[0].rgba );
			break;
		case CGEN_ENTITY:
			RB_CalcColorFromEntity( tess.svars.colors[0].rgba );
			break;
		case CGEN_ONE_MINUS_ENTITY:
			RB_CalcColorFromOneMinusEntity( tess.svars.colors[0].rgba );
			break;
	}

	//
	// alphaGen
	//
	switch ( pStage->alphaGen )
	{
	case AGEN_SKIP:
		break;
	case AGEN_IDENTITY:
		if ( ( pStage->rgbGen == CGEN_VERTEX && tr.identityLight != 1 ) ||
			 pStage->rgbGen != CGEN_VERTEX ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				tess.svars.colors[i].rgba[3] = 255;
			}
		}
		break;
	case AGEN_CONST:
		for ( i = 0; i < tess.numVertexes; i++ ) {
			tess.svars.colors[i].rgba[3] = pStage->constantColor.rgba[3];
		}
		break;
	case AGEN_WAVEFORM:
		RB_CalcWaveAlpha( &pStage->alphaWave, tess.svars.colors[0].rgba );
		break;
	case AGEN_LIGHTING_SPECULAR:
		RB_CalcSpecularAlpha( tess.svars.colors[0].rgba );
		break;
	case AGEN_ENTITY:
		RB_CalcAlphaFromEntity( tess.svars.colors[0].rgba );
		break;
	case AGEN_ONE_MINUS_ENTITY:
		RB_CalcAlphaFromOneMinusEntity( tess.svars.colors[0].rgba );
		break;
	case AGEN_VERTEX:
		for ( i = 0; i < tess.numVertexes; i++ ) {
			tess.svars.colors[i].rgba[3] = tess.vertexColors[i].rgba[3];
		}
		break;
	case AGEN_ONE_MINUS_VERTEX:
		for ( i = 0; i < tess.numVertexes; i++ )
		{
			tess.svars.colors[i].rgba[3] = 255 - tess.vertexColors[i].rgba[3];
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

				tess.svars.colors[i].rgba[3] = alpha;
			}
		}
		break;
	}

	//
	// fog adjustment for colors to fade out as fog increases
	//
	if ( tess.fogNum )
	{
		switch ( pStage->adjustColorsForFog )
		{
		case ACFF_MODULATE_RGB:
			RB_CalcModulateColorsByFog( tess.svars.colors[0].rgba );
			break;
		case ACFF_MODULATE_ALPHA:
			RB_CalcModulateAlphasByFog( tess.svars.colors[0].rgba );
			break;
		case ACFF_MODULATE_RGBA:
			RB_CalcModulateRGBAsByFog( tess.svars.colors[0].rgba );
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
static void RB_IterateStagesGeneric( const shaderCommands_t *input )
{
	const shaderStage_t *pStage;
	int stage;

	for ( stage = 0; stage < MAX_SHADER_STAGES; stage++ )
	{
		pStage = tess.xstages[ stage ];
		if ( !pStage )
			break;

		//
		// do multitexture
		//
		if ( pStage->mtEnv )
		{
			DrawMultitextured( input, stage );
		}
		else
		{
			qboolean glxStreamedDraw = qfalse;
			qboolean depthFade = RB_DepthFadeActive( pStage );

#ifdef RENDERER_GLX
			GLX_CompatRecordMaterialStage( pStage, GLX_STAGE_PATH_GENERIC, input->numVertexes, input->numIndexes );
#endif

			if ( !setArraysOnce )
			{
				R_ComputeTexCoords( 0, &pStage->bundle[0] );
				R_ComputeColors( pStage );

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
#ifdef RENDERER_GLX
			if ( !depthFade ) {
				glxStreamedDraw = GLX_TryStreamDrawStage( input, pStage, qfalse, 0 );
			}
#endif
			if ( !glxStreamedDraw )
			{
				if ( depthFade )
					RB_DrawDepthFadeStage( input );
				else
					R_DrawElements( input->numIndexes, input->indexes );
				if ( pStage->depthFragment )
				{
					GL_State( pStage->stateBits | GLS_DEPTHMASK_TRUE );
					GL_ProgramEnable();
					R_DrawElements( input->numIndexes, input->indexes );
					GL_ProgramDisable();
				}
			}
		}

		// allow skipping out to show just lightmaps during development
		if ( r_lightmap->integer && ( pStage->bundle[0].lightmap != LIGHTMAP_INDEX_NONE || pStage->bundle[1].lightmap != LIGHTMAP_INDEX_NONE ) )
			break;
	}
}

#ifdef USE_FBO
static vec4_t rb_liquidTexcoords[SHADER_MAX_VERTEXES];
static color4ub_t rb_liquidColors[SHADER_MAX_VERTEXES];

static qboolean RB_LiquidEffectActive( const shaderCommands_t *input )
{
	return input && input->shader && input->numPasses > 0 &&
		input->numIndexes > 0 && input->numVertexes > 0 &&
		fboEnabled && FBO_LiquidScreenTexture() &&
		r_liquid && r_liquid->integer > 0 &&
		R_LiquidContentsEnabled( input->liquidContentFlags, r_liquid->integer ) &&
		input->liquidSort >= SS_FOG &&
		R_LiquidShaderSupported( input->shader ) &&
		( ( r_liquidRefraction && r_liquidRefraction->value > 0.0f ) ||
			( r_liquidReflection && r_liquidReflection->value > 0.0f ) ) &&
		R_LiquidViewportCoversTarget( backEnd.viewParms.viewportX,
			backEnd.viewParms.viewportY, backEnd.viewParms.viewportWidth,
			backEnd.viewParms.viewportHeight, glConfig.vidWidth, glConfig.vidHeight ) &&
		!( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) &&
		!backEnd.screenshotCubeActive && !backEnd.screenshotCubeFrontPending &&
		!R_ViewPassIsPortal( &backEnd.viewParms ) && !glConfig.stereoEnabled ? qtrue : qfalse;
}

static void RB_ProjectLiquidVertex( const vec4_t position, vec4_t screen )
{
	const float *model = backEnd.or.modelMatrix;
	const float *projection = backEnd.viewParms.projectionMatrix;
	vec4_t eye;
	vec4_t clip;

	eye[0] = model[0] * position[0] + model[4] * position[1] + model[8] * position[2] + model[12];
	eye[1] = model[1] * position[0] + model[5] * position[1] + model[9] * position[2] + model[13];
	eye[2] = model[2] * position[0] + model[6] * position[1] + model[10] * position[2] + model[14];
	eye[3] = model[3] * position[0] + model[7] * position[1] + model[11] * position[2] + model[15];

	clip[0] = projection[0] * eye[0] + projection[4] * eye[1] + projection[8] * eye[2] + projection[12] * eye[3];
	clip[1] = projection[1] * eye[0] + projection[5] * eye[1] + projection[9] * eye[2] + projection[13] * eye[3];
	clip[3] = projection[3] * eye[0] + projection[7] * eye[1] + projection[11] * eye[2] + projection[15] * eye[3];
	screen[0] = clip[0] * 0.5f + clip[3] * 0.5f;
	screen[1] = clip[1] * 0.5f + clip[3] * 0.5f;
	screen[2] = 0.0f;
	screen[3] = clip[3];
}

static void RB_LiquidFixedFunctionTexEnv( qboolean enable, qboolean sampleScene )
{
	if ( enable ) {
		/* Refraction takes RGB from the scene copy; the Fresnel pass takes RGB
		 * from the bounded material tint. Alpha always comes from primary color. */
		qglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE );
		qglTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE );
		qglTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB,
			sampleScene ? GL_TEXTURE : GL_PRIMARY_COLOR );
		qglTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR );
		qglTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE );
		qglTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PRIMARY_COLOR );
		qglTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA );
		glState.texEnv[glState.currenttmu] = GL_COMBINE;
	} else {
		qglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		glState.texEnv[glState.currenttmu] = GL_MODULATE;
	}
}

typedef struct rbLiquidRipple_s {
	vec3_t center;
	vec4_t screenCenter;
	float radius;
	float width;
	float amplitude;	// screen-pixel displacement, resolution-scaled
} rbLiquidRipple_t;

/* out = b * a, both column-major; same semantics as tr_main.c myGlMultMatrix */
static void RB_LiquidMultMatrix( const float *a, const float *b, float *out )
{
	int i, j;

	for ( i = 0; i < 4; i++ ) {
		for ( j = 0; j < 4; j++ ) {
			out[ i * 4 + j ] =
				a[ i * 4 + 0 ] * b[ 0 * 4 + j ] +
				a[ i * 4 + 1 ] * b[ 1 * 4 + j ] +
				a[ i * 4 + 2 ] * b[ 2 * 4 + j ] +
				a[ i * 4 + 3 ] * b[ 3 * 4 + j ];
		}
	}
}

static vec2_t rb_liquidRippleOffsets[SHADER_MAX_VERTEXES];

/* Per-vertex ripple pixel offsets shared by the ARB program path (as a
 * texcoord varying) and the projective fixed-function fallback. */
static void RB_ComputeLiquidRippleOffsets( const shaderCommands_t *input,
	const rbLiquidRipple_t *ripples, int rippleCount )
{
	int i, j;

	if ( rippleCount <= 0 ) {
		Com_Memset( rb_liquidRippleOffsets, 0,
			input->numVertexes * sizeof( rb_liquidRippleOffsets[0] ) );
		return;
	}
	for ( i = 0; i < input->numVertexes; i++ ) {
		vec4_t screen;
		vec3_t normal;
		float normalLength;
		float invQ;
		float screenU;
		float screenV;
		float offsetX = 0.0f;
		float offsetY = 0.0f;

		RB_ProjectLiquidVertex( input->xyz[i], screen );
		invQ = fabsf( screen[3] ) > 1e-6f ? 1.0f / screen[3] : 0.0f;
		screenU = screen[0] * invQ;
		screenV = screen[1] * invQ;
		VectorCopy( input->normal[i], normal );
		normalLength = VectorLength( normal );
		if ( normalLength > 1e-6f ) {
			VectorScale( normal, 1.0f / normalLength, normal );
		} else {
			VectorSet( normal, 0.0f, 0.0f, 1.0f );
		}

		for ( j = 0; j < rippleCount; j++ ) {
			vec3_t delta;
			float height;
			float distance;
			float ringSigned;
			float ring;
			float heightFade;
			float centerInvQ;
			float screenDeltaX;
			float screenDeltaY;
			float screenDistance;

			VectorSubtract( input->xyz[i], ripples[j].center, delta );
			height = DotProduct( delta, normal );
			VectorMA( delta, -height, normal, delta );
			distance = VectorLength( delta );
			if ( distance <= 1e-4f ) {
				continue;
			}
			ringSigned = Com_Clamp( -1.0f, 1.0f,
				( distance - ripples[j].radius ) / ripples[j].width );
			ring = ( 1.0f - fabsf( ringSigned ) ) * sinf( ringSigned * (float)M_PI );
			heightFade = 1.0f - Com_Clamp( 0.0f, 1.0f,
				fabsf( height ) / MAX( 48.0f, ripples[j].width * 3.0f ) );
			ring *= heightFade * ripples[j].amplitude;
			centerInvQ = fabsf( ripples[j].screenCenter[3] ) > 1e-6f ?
				1.0f / ripples[j].screenCenter[3] : 0.0f;
			screenDeltaX = screenU - ripples[j].screenCenter[0] * centerInvQ;
			screenDeltaY = screenV - ripples[j].screenCenter[1] * centerInvQ;
			screenDistance = sqrtf( screenDeltaX * screenDeltaX + screenDeltaY * screenDeltaY );
			if ( screenDistance > 1e-6f ) {
				offsetX += screenDeltaX * ( ring / screenDistance );
				offsetY += screenDeltaY * ( ring / screenDistance );
			}
		}
		rb_liquidRippleOffsets[i][0] = offsetX;
		rb_liquidRippleOffsets[i][1] = offsetY;
	}
}

static void RB_DrawLiquidPass( shaderCommands_t *input, qboolean refractionBase )
{
	rbLiquidRipple_t ripples[LIQUID_MAX_ACTIVE_IMPULSES];
	const float time = R_LiquidWaveTime( backEnd.refdef.floatTime );
	const float heightScale = R_LiquidViewHeightScale( backEnd.viewParms.viewportHeight );
	/* R_LiquidWarpPixels maps the archived cvar through LIQUID_WARP_TO_PIXELS
	 * and scales it to the active viewport height. */
	const float warpPixels = R_LiquidWarpPixels( r_liquidWarpScale ? r_liquidWarpScale->value : 0.0f,
		backEnd.viewParms.viewportHeight );
	const float fresnelStrength = r_liquidReflection ? r_liquidReflection->value : 0.0f;
	const float refractionStrength = r_liquidRefraction ? r_liquidRefraction->value : 1.0f;
	const float rippleStrength = r_liquidRipples ? r_liquidRipples->value : 0.0f;
	const float typeScale = R_LiquidContentsReflectionScale( input->liquidContentFlags );
	const float passStrength = Com_Clamp( 0.0f, 1.0f,
		refractionBase ? refractionStrength : fresnelStrength );
	const float reflectivity = backEnd.liquidScreenMapDone ? LIQUID_REFLECT_INTENSITY : 0.0f;
	vec3_t fresnelColor;
	GLboolean previousColorMask[4];
	qboolean rippleOffsetsReady = qfalse;
	int rippleCount = 0;
	int i;
#ifdef RENDERER_GLX
	vec4_t glxParams;
	vec4_t glxEyeAndCount;
	vec4_t glxTargetInverse;
	vec4_t glxReflect;
	vec4_t glxImpulses[LIQUID_MAX_ACTIVE_IMPULSES];
	vec4_t glxAmplitudes[2];
	qboolean glxLiquidBound;
#endif

	if ( !RB_LiquidEffectActive( input ) ||
		( refractionBase && ( !backEnd.liquidScreenMapDone || refractionStrength <= 0.0f ) ) ||
		( !refractionBase && fresnelStrength <= 0.0f ) ) {
		return;
	}
	R_LiquidContentsFresnelColor( input->liquidContentFlags, fresnelColor );

	/* Ripple rings displace the refraction underlay only; the sheen pass sees
	 * the liquid motion through its wave-perturbed normal instead. */
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
				backEnd.or.axis, ripples[rippleCount].center );
			ripples[rippleCount].radius = interaction->radius + age * 150.0f;
			ripples[rippleCount].width = 20.0f + interaction->radius * 0.35f;
			ripples[rippleCount].amplitude = interaction->strength * rippleStrength * life *
				LIQUID_RIPPLE_PIXEL_SCALE * heightScale;
			rippleCount++;
		}
	}
	for ( i = 0; i < rippleCount; i++ ) {
		vec4_t centerPosition;

		Vector4Set( centerPosition, ripples[i].center[0], ripples[i].center[1],
			ripples[i].center[2], 1.0f );
		RB_ProjectLiquidVertex( centerPosition, ripples[i].screenCenter );
	}
	GL_GetColorMask( previousColorMask );
	GL_ColorMask( previousColorMask[0], previousColorMask[1],
		previousColorMask[2], GL_FALSE );

#ifdef RENDERER_GLX
	Com_Memset( glxImpulses, 0, sizeof( glxImpulses ) );
	Com_Memset( glxAmplitudes, 0, sizeof( glxAmplitudes ) );
	Vector4Set( glxParams, time, warpPixels, passStrength,
		refractionBase ? -typeScale : typeScale );
	Vector4Set( glxEyeAndCount, backEnd.or.viewOrigin[0], backEnd.or.viewOrigin[1],
		backEnd.or.viewOrigin[2], (float)rippleCount );
	Vector4Set( glxTargetInverse,
		backEnd.viewParms.viewportWidth > 0 ?
			1.0f / (float)backEnd.viewParms.viewportWidth : 1.0f,
		backEnd.viewParms.viewportHeight > 0 ?
			1.0f / (float)backEnd.viewParms.viewportHeight : 1.0f,
		FBO_LiquidDepthReady() ? 1.0f : 0.0f, 0.0f );
	Vector4Set( glxReflect, fresnelColor[0], fresnelColor[1], fresnelColor[2],
		reflectivity );
	for ( i = 0; i < rippleCount; i++ ) {
		VectorCopy( ripples[i].center, glxImpulses[i] );
		glxImpulses[i][3] = ripples[i].radius;
		glxAmplitudes[i >> 2][i & 3] = ripples[i].amplitude;
	}

	/* The normal GLx path evaluates waves, refraction, reflection, and ripple
	 * rings per fragment. Keep destination alpha intact so later idTech3 blend
	 * modes see exactly the value produced by the authored scene. */
	GL_ProgramDisable();
	GLX_CompatUnbindMaterial();
	if ( refractionBase ) {
		FBO_BindLiquidDepthTexture( 1 );
	}
	GL_SelectTexture( 0 );
	GL_BindTexNum( FBO_LiquidScreenTexture() );
	GL_TexEnv( GL_MODULATE );
	GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	glxLiquidBound = GLX_CompatBindLiquidMaterial( glxParams, glxEyeAndCount,
		glxTargetInverse, glxReflect, glxImpulses[0], glxAmplitudes[0] );
	if ( glxLiquidBound ) {
		if ( GLX_TryStreamDrawLiquidPass( input ) ) {
			GLX_CompatUnbindMaterial();
			GL_ColorMask( previousColorMask[0], previousColorMask[1],
				previousColorMask[2], previousColorMask[3] );
			return;
		}
		GLX_CompatUnbindMaterial();
	}
#endif

	/* FBO support guarantees ARB programs on this renderer, so the normal
	 * OpenGL-lineage tier evaluates the liquid function per fragment. */
	if ( GL_LiquidProgramAvailable() ) {
		liquidArbParams_t arb;

		RB_LiquidMultMatrix( backEnd.or.modelMatrix,
			backEnd.viewParms.projectionMatrix, arb.mvp );
		VectorCopy( backEnd.or.viewOrigin, arb.eyePos );
		arb.wrappedTime = time;
		arb.warpPixels = warpPixels;
		arb.alphaScale = typeScale * passStrength;
		arb.reflectivity = reflectivity;
		arb.depthAvailable = FBO_LiquidDepthReady();
		VectorCopy( fresnelColor, arb.fresnelColor );

		GL_ProgramDisable();
		if ( refractionBase ) {
			FBO_BindLiquidDepthTexture( 1 );
		}
		GL_SelectTexture( 0 );
		GL_BindTexNum( FBO_LiquidScreenTexture() );
		GL_TexEnv( GL_MODULATE );
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
		GL_LiquidProgramEnable( &arb, refractionBase );
		GL_ClientState( 1, CLS_NONE );
		if ( refractionBase ) {
			RB_ComputeLiquidRippleOffsets( input, ripples, rippleCount );
			rippleOffsetsReady = qtrue;
			GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_NORMAL_ARRAY );
			qglTexCoordPointer( 2, GL_FLOAT, 0, rb_liquidRippleOffsets );
		} else {
			GL_ClientState( 0, CLS_NORMAL_ARRAY );
		}
		qglNormalPointer( GL_FLOAT, sizeof( input->normal[0] ), input->normal );
		qglVertexPointer( 3, GL_FLOAT, sizeof( input->xyz[0] ), input->xyz );
		R_DrawElements( input->numIndexes, input->indexes );
		GL_ProgramDisable();
		GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );
		GL_ColorMask( previousColorMask[0], previousColorMask[1],
			previousColorMask[2], previousColorMask[3] );
		return;
	}

	/* Projective per-vertex fallback for program compile failures. */
	if ( refractionBase && !rippleOffsetsReady ) {
		RB_ComputeLiquidRippleOffsets( input, ripples, rippleCount );
	}
	for ( i = 0; i < input->numVertexes; i++ ) {
		vec4_t *screen = &rb_liquidTexcoords[i];
		vec3_t normal;
		vec3_t view;
		float normalLength;
		float viewLength;
		float gradientX;
		float gradientY;
		float distanceAtten;
		float ambientX;
		float ambientY;
		float rippleX = 0.0f;
		float rippleY = 0.0f;
		float fresnel;
		float alpha;
		float edgeFade;
		float invQ;
		float screenU;
		float screenV;

		RB_ProjectLiquidVertex( input->xyz[i], *screen );
		VectorCopy( input->normal[i], normal );
		normalLength = VectorLength( normal );
		if ( normalLength > 1e-6f ) {
			VectorScale( normal, 1.0f / normalLength, normal );
		} else {
			VectorSet( normal, 0.0f, 0.0f, 1.0f );
		}
		invQ = fabsf( (*screen)[3] ) > 1e-6f ? 1.0f / (*screen)[3] : 0.0f;
		screenU = (*screen)[0] * invQ;
		screenV = (*screen)[1] * invQ;
		VectorSubtract( backEnd.or.viewOrigin, input->xyz[i], view );
		viewLength = VectorLength( view );
		if ( viewLength > 1e-6f ) {
			VectorScale( view, 1.0f / viewLength, view );
		}

		R_LiquidWaveGradient( input->xyz[i], time, &gradientX, &gradientY );
		distanceAtten = R_LiquidDistanceAttenuation( fabsf( (*screen)[3] ) );
		distanceAtten *= Com_Clamp( 0.0f, 1.0f,
			fabsf( DotProduct( normal, view ) ) * LIQUID_GRAZING_FADE_SCALE );
		ambientX = gradientX * warpPixels * distanceAtten;
		ambientY = gradientY * warpPixels * distanceAtten;
		if ( refractionBase ) {
			rippleX = rb_liquidRippleOffsets[i][0];
			rippleY = rb_liquidRippleOffsets[i][1];
		}

		edgeFade = MIN( MIN( screenU, 1.0f - screenU ),
			MIN( screenV, 1.0f - screenV ) );
		edgeFade = Com_Clamp( 0.0f, 1.0f, edgeFade * ( 1.0f / 0.06f ) );
		edgeFade = edgeFade * edgeFade * ( 3.0f - 2.0f * edgeFade );
		(*screen)[0] = Com_Clamp( 0.002f, 0.998f,
			screenU + edgeFade * ( ambientX + rippleX ) /
				(float)MAX( 1, backEnd.viewParms.viewportWidth ) ) * (*screen)[3];
		(*screen)[1] = Com_Clamp( 0.002f, 0.998f,
			screenV + edgeFade * ( ambientY + rippleY ) /
				(float)MAX( 1, backEnd.viewParms.viewportHeight ) ) * (*screen)[3];

		if ( refractionBase ) {
			alpha = typeScale * passStrength;
		} else {
			fresnel = 1.0f - fabsf( DotProduct( normal, view ) );
			fresnel = Com_Clamp( 0.0f, 1.0f, fresnel );
			fresnel *= fresnel;
			alpha = typeScale * passStrength *
				Com_Clamp( 0.0f, 0.60f, LIQUID_FRESNEL_ALPHA_BASE +
					fresnel * LIQUID_FRESNEL_ALPHA_SPAN );
		}
		rb_liquidColors[i].rgba[0] = refractionBase ? 255 : (byte)( fresnelColor[0] * 255.0f + 0.5f );
		rb_liquidColors[i].rgba[1] = refractionBase ? 255 : (byte)( fresnelColor[1] * 255.0f + 0.5f );
		rb_liquidColors[i].rgba[2] = refractionBase ? 255 : (byte)( fresnelColor[2] * 255.0f + 0.5f );
		rb_liquidColors[i].rgba[3] = (byte)( alpha * 255.0f + 0.5f );
	}

	GL_ProgramDisable();
#ifdef RENDERER_GLX
	GLX_CompatUnbindMaterial();
#endif
	GL_SelectTexture( 0 );
	GL_BindTexNum( FBO_LiquidScreenTexture() );
	GL_TexEnv( GL_MODULATE );
	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, sizeof( input->xyz[0] ), input->xyz );
	qglTexCoordPointer( 4, GL_FLOAT, 0, rb_liquidTexcoords );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, rb_liquidColors[0].rgba );
	GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	RB_LiquidFixedFunctionTexEnv( qtrue, refractionBase );
#ifdef RENDERER_GLX
	/* GL12 and allocation-failure fallback. The dedicated shader is the normal
	 * GLx path; fixed function retains conservative compatibility behavior. */
	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, sizeof( input->xyz[0] ), input->xyz );
	qglTexCoordPointer( 4, GL_FLOAT, 0, rb_liquidTexcoords );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, rb_liquidColors[0].rgba );
	(void)GLX_CompatDrawElementsClassified( GL_TRIANGLES, input->numIndexes,
		GL_INDEX_TYPE, input->indexes, GLX_LEGACY_DELEGATION_GENERIC,
		GLX_DRAW_GENERIC, GLX_STAGE_SCREEN_MAP | GLX_STAGE_BLEND,
		GLX_CompatDynamicCategoryMaskForTess( input,
			GLX_STAGE_SCREEN_MAP | GLX_STAGE_BLEND ) );
#else
	R_DrawElements( input->numIndexes, input->indexes );
#endif
	RB_LiquidFixedFunctionTexEnv( qfalse, qfalse );
	GL_ColorMask( previousColorMask[0], previousColorMask[1],
		previousColorMask[2], previousColorMask[3] );
}
#endif /* USE_FBO */


/*
** RB_StageIteratorGeneric
*/
void RB_StageIteratorGeneric( void )
{
	const shaderCommands_t *input;
	shader_t		*shader;
	qboolean arraysLocked = qfalse;

#ifdef USE_PMLIGHT
	if ( tess.dlightPass )
	{
#if defined( USE_VBO ) && defined( RENDERER_GLX )
		if ( tess.vboIndex && RB_StageIteratorVBODlight() ) {
			return;
		}
#endif
		ARB_LightingPass();
		return;
	}

	GL_ProgramDisable();
#endif // USE_PMLIGHT

#ifdef USE_VBO
	if ( tess.vboIndex )
	{
		RB_StageIteratorVBO();
		return;
	}

	VBO_UnBind();
#endif

	input = &tess;
	shader = input->shader;

	RB_DeformTessGeometry();
#ifdef USE_PMLIGHT
	RB_ApplyDlightShadowCasterNormalBias();
#endif

	//
	// set face culling appropriately
	//
#ifdef USE_PMLIGHT
	GL_Cull( tess.csmCasterPass ? CT_TWO_SIDED : shader->cullType );
#else
	GL_Cull( shader->cullType );
#endif

	// set polygon offset if necessary
	if ( shader->polygonOffset )
	{
		qglEnable( GL_POLYGON_OFFSET_FILL );
		qglPolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
	}

#ifdef USE_FBO
	/* Draw the warped captured scene first. Authored translucent stages then tint,
	 * scroll, and deform that refracted background instead of hiding it. */
	if ( RB_LiquidEffectActive( input ) ) {
		RB_DrawLiquidPass( &tess, qtrue );
	}
#endif

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
			R_ComputeColors( tess.xstages[0] );
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
#ifdef RENDERER_GLX
		if ( !GLX_CompatStreamDrawEnabled() )
#endif
		{
			qglLockArraysEXT( 0, input->numVertexes );
			arraysLocked = qtrue;
		}
	}

	//
	// call shader function
	//
	RB_IterateStagesGeneric( input );

#ifdef USE_FBO
	if ( RB_LiquidEffectActive( input ) ) {
		if ( arraysLocked && qglUnlockArraysEXT ) {
			qglUnlockArraysEXT();
			arraysLocked = qfalse;
		}
		RB_DrawLiquidPass( &tess, qfalse );
	}
#endif

	//
	// now do any dynamic lighting needed
	//
#ifdef USE_LEGACY_DLIGHTS
#ifdef USE_PMLIGHT
	if ( !R_GetDlightMode() )
#endif
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE && !(tess.shader->surfaceFlags & (SURF_NODLIGHT | SURF_SKY) ) )
	{
		ProjectDlightTexture();
	}
#endif // USE_LEGACY_DLIGHTS

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
	if ( arraysLocked && qglUnlockArraysEXT )
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


/*
** RB_EndSurface
*/
void RB_EndSurface( void ) {
	const shaderCommands_t *input;

	input = &tess;

	if ( input->numIndexes == 0 ) {
#ifdef USE_VBO
		VBO_UnBind();
#endif
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
		ARB_CSMShadowPass();
		tess.csmShadowPass = qfalse;
		tess.numIndexes = 0;
		tess.numVertexes = 0;
		return;
	}
#endif

	if ( R_QLSkipBatch( tess.numIndexes ) ) {
		tess.numIndexes = 0;
		tess.numVertexes = 0;
		return;
	}

	// for debugging of sort order issues, stop rendering after a given sort value
	if ( r_debugSort->integer && r_debugSort->integer < tess.shader->sort
		&& qlRendererCvars.debugSortExcept->integer != (int)tess.shader->sort
		&& !backEnd.doneSurfaces ) {
#ifdef USE_VBO
		VBO_UnBind();
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

#ifdef RENDERER_GLX
	{
		int glxBatchFlags = 0;

#ifdef USE_VBO
		if ( tess.vboIndex ) {
			glxBatchFlags |= GLX_BATCH_VBO;
		}
#endif
		if ( tess.fogNum && tess.shader->fogPass ) {
			glxBatchFlags |= GLX_BATCH_FOG;
		}
		if ( tess.shader->multitextureEnv ) {
			glxBatchFlags |= GLX_BATCH_MULTITEXTURE;
		}
		if ( tess.shader->polygonOffset ) {
			glxBatchFlags |= GLX_BATCH_POLYGON_OFFSET;
		}

		GLX_CompatRecordShaderBatch( tess.shader->name, (int)tess.shader->sort,
			tess.numPasses, input->numVertexes, input->numIndexes, glxBatchFlags );
	}
#endif

	//
	// call off to shader specific tess end function
	//
#ifdef RENDERER_GLX
	GLX_CompatPushShaderDebugGroup( tess.shader->name, input->numVertexes, input->numIndexes, tess.numPasses );
#endif
	tess.shader->optimalStageIteratorFunc();
	RB_CelOutlineTessEnd();
#ifdef RENDERER_GLX
	GLX_CompatPopDebugGroup();
#endif

#ifdef RENDERER_GLX
#ifdef USE_VBO
	if ( !VBO_Active() )
#endif
	{
		GLX_CompatShadowUploadTess( input->numVertexes, input->numIndexes,
			input->xyz, input->numVertexes * (int)sizeof( input->xyz[0] ),
			input->indexes, input->numIndexes * (int)sizeof( input->indexes[0] ) );
	}
#endif

	//
	// draw debugging stuff
	//
#ifdef USE_VBO
	if ( !VBO_Active() ) {
#else
	{
#endif
		if ( r_showtris->integer || ( qlRendererCvars.debugShaderIndex->integer > 0
			&& qlRendererCvars.debugShaderIndex->integer == tess.shader->index ) ) {
			DrawTris( input );
		}
		if ( r_shownormals->integer ) {
			DrawNormals( input );
		}
	}

	// clear shader so we can tell we don't have any unclosed surfaces
	tess.numIndexes = 0;
	tess.numVertexes = 0;
}
