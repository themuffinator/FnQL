/*
===========================================================================
Copyright (C) 2026

This file is part of FnQL.

FnQL is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2 of the License, or (at your option)
any later version.
===========================================================================
*/

#ifndef TR_GLX_BRIDGE_H
#define TR_GLX_BRIDGE_H

#include "tr_glx_api.h"

/*
 * Renderer-common GLx bridge facade.
 *
 * This layer keeps module calls and non-GLX stubs out of the legacy
 * renderer-owned compatibility adapter. Renderer-specific state conversion
 * still belongs beside tr_local.h; ABI forwarding and shared accounting hooks
 * live here so future non-legacy substrates can reuse the same bridge surface.
 */

static ID_INLINE int GLX_CompatAlignInt( int value, int alignment )
{
	const int remainder = value % alignment;

	if ( remainder == 0 ) {
		return value;
	}

	return value + alignment - remainder;
}

static ID_INLINE void GLX_CompatSetImports( refimport_t *imports )
{
#ifdef RENDERER_GLX
	GLX_Renderer_SetImports( imports );
#else
	(void)imports;
#endif
}

static ID_INLINE void GLX_CompatRegisterCommands( void )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RegisterCommands();
#endif
}

static ID_INLINE void GLX_CompatRemoveCommands( void )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RemoveCommands();
#endif
}

static ID_INLINE void GLX_CompatOnOpenGLReady( const glconfig_t *config, const char *extensions )
{
#ifdef RENDERER_GLX
	GLX_Renderer_OnOpenGLReady( config, extensions );
#else
	(void)config;
	(void)extensions;
#endif
}

static ID_INLINE void GLX_CompatShutdown( int code )
{
#ifdef RENDERER_GLX
	GLX_Renderer_Shutdown( code );
#else
	(void)code;
#endif
}

static ID_INLINE void GLX_CompatBeginBackendTimer( void )
{
#ifdef RENDERER_GLX
	GLX_Renderer_BeginBackendTimer();
#endif
}

static ID_INLINE void GLX_CompatEndBackendTimer( void )
{
#ifdef RENDERER_GLX
	GLX_Renderer_EndBackendTimer();
#endif
}

static ID_INLINE void GLX_CompatBeginGpuPassTimer( int pass )
{
#ifdef RENDERER_GLX
	GLX_Renderer_BeginGpuPassTimer( pass );
#else
	(void)pass;
#endif
}

static ID_INLINE void GLX_CompatEndGpuPassTimer( int pass )
{
#ifdef RENDERER_GLX
	GLX_Renderer_EndGpuPassTimer( pass );
#else
	(void)pass;
#endif
}

static ID_INLINE void GLX_CompatFrameComplete( void )
{
#ifdef RENDERER_GLX
	GLX_Renderer_FrameComplete();
#endif
}

static ID_INLINE void GLX_CompatPrintFrameCounters( void )
{
#ifdef RENDERER_GLX
	GLX_Renderer_PrintFrameCounters();
#endif
}

static ID_INLINE qboolean GLX_CompatDrawElements( unsigned int mode, int count,
	unsigned int type, const void *indices, int legacyReason, int profilerPath )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_DrawElements( mode, count, type, indices, legacyReason, profilerPath );
#else
	(void)mode;
	(void)count;
	(void)type;
	(void)indices;
	(void)legacyReason;
	(void)profilerPath;
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatDrawElementsClassified( unsigned int mode, int count,
	unsigned int type, const void *indices, int legacyReason, int profilerPath,
	int materialFlags, unsigned int categoryMask )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_DrawElementsClassified( mode, count, type, indices, legacyReason,
		profilerPath, materialFlags, categoryMask );
#else
	(void)mode;
	(void)count;
	(void)type;
	(void)indices;
	(void)legacyReason;
	(void)profilerPath;
	(void)materialFlags;
	(void)categoryMask;
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatDrawElementsClassifiedProjectedDlights(
	unsigned int mode, int count, unsigned int type, const void *indices,
	int legacyReason, int profilerPath, int materialFlags, unsigned int categoryMask,
	unsigned int lightMask )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_DrawElementsClassifiedProjectedDlights( mode, count, type,
		indices, legacyReason, profilerPath, materialFlags, categoryMask, lightMask );
#else
	(void)mode;
	(void)count;
	(void)type;
	(void)indices;
	(void)legacyReason;
	(void)profilerPath;
	(void)materialFlags;
	(void)categoryMask;
	(void)lightMask;
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatDrawArrays( unsigned int mode, int first, int count,
	int legacyReason, int profilerPath )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_DrawArrays( mode, first, count, legacyReason, profilerPath );
#else
	(void)mode;
	(void)first;
	(void)count;
	(void)legacyReason;
	(void)profilerPath;
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatDrawArraysClassified( unsigned int mode, int first, int count,
	int legacyReason, int profilerPath, int materialFlags, unsigned int categoryMask )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_DrawArraysClassified( mode, first, count, legacyReason, profilerPath,
		materialFlags, categoryMask );
#else
	(void)mode;
	(void)first;
	(void)count;
	(void)legacyReason;
	(void)profilerPath;
	(void)materialFlags;
	(void)categoryMask;
	return qfalse;
#endif
}

static ID_INLINE void GLX_CompatRecordDraw( int indexes, int path )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordDraw( indexes, path );
#else
	(void)indexes;
	(void)path;
#endif
}

static ID_INLINE void GLX_CompatRecordShaderBatch( const char *shaderName, int sort,
	int numPasses, int numVertexes, int numIndexes, int flags )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordShaderBatch( shaderName, sort, numPasses, numVertexes, numIndexes, flags );
#else
	(void)shaderName;
	(void)sort;
	(void)numPasses;
	(void)numVertexes;
	(void)numIndexes;
	(void)flags;
#endif
}

static ID_INLINE void GLX_CompatPushShaderDebugGroup( const char *shaderName,
	int numVertexes, int numIndexes, int numPasses )
{
#ifdef RENDERER_GLX
	GLX_Renderer_PushShaderDebugGroup( shaderName, numVertexes, numIndexes, numPasses );
#else
	(void)shaderName;
	(void)numVertexes;
	(void)numIndexes;
	(void)numPasses;
#endif
}

static ID_INLINE void GLX_CompatPopDebugGroup( void )
{
#ifdef RENDERER_GLX
	GLX_Renderer_PopDebugGroup();
#endif
}

static ID_INLINE void GLX_CompatShadowUploadTess( int numVertexes, int numIndexes,
	const void *xyz, int xyzBytes, const void *indexes, int indexBytes )
{
#ifdef RENDERER_GLX
	GLX_Renderer_ShadowUploadTess( numVertexes, numIndexes, xyz, xyzBytes, indexes, indexBytes );
#else
	(void)numVertexes;
	(void)numIndexes;
	(void)xyz;
	(void)xyzBytes;
	(void)indexes;
	(void)indexBytes;
#endif
}

static ID_INLINE qboolean GLX_CompatMaterialRendererActive( void )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_MaterialRendererActive();
#else
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatDlightProgramAvailable( qboolean linear,
	int fogMode, qboolean absLight, qboolean shadow )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_DlightProgramAvailable( linear, fogMode, absLight, shadow );
#else
	(void)linear;
	(void)fogMode;
	(void)absLight;
	(void)shadow;
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatBindDlightProgram( qboolean linear,
	int fogMode, qboolean absLight, qboolean shadow, const float *eyePos, const float *lightPos,
	const float *lightColor, const float *lightVector, const float *texFactors,
	const float *dlightFactors, const float *fogDistanceVector,
	const float *fogDepthVector, float fogEyeT, const float *dlightShadow,
	const float *shadowAtlas, const float *shadowDepth, const float *shadowFilter )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_BindDlightProgram( linear, fogMode, absLight, shadow,
		eyePos, lightPos, lightColor, lightVector, texFactors, dlightFactors,
		fogDistanceVector, fogDepthVector, fogEyeT,
		dlightShadow, shadowAtlas, shadowDepth, shadowFilter );
#else
	(void)linear;
	(void)fogMode;
	(void)absLight;
	(void)shadow;
	(void)eyePos;
	(void)lightPos;
	(void)lightColor;
	(void)lightVector;
	(void)texFactors;
	(void)dlightFactors;
	(void)fogDistanceVector;
	(void)fogDepthVector;
	(void)fogEyeT;
	(void)dlightShadow;
	(void)shadowAtlas;
	(void)shadowDepth;
	(void)shadowFilter;
	return qfalse;
#endif
}

static ID_INLINE void GLX_CompatUnbindDlightProgram( void )
{
#ifdef RENDERER_GLX
	GLX_Renderer_UnbindDlightProgram();
#endif
}

static ID_INLINE qboolean GLX_CompatDlightProjectedProgramEnabled( void )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_DlightProjectedProgramEnabled();
#else
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatProjectedDlightWorldOverlayActive( void )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_ProjectedDlightWorldOverlayActive();
#else
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatBindProjectedDlightOverlayProgram( void )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_BindProjectedDlightOverlayProgram();
#else
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatDlightScissorEnabled( void )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_DlightScissorEnabled();
#else
	return qfalse;
#endif
}

static ID_INLINE void GLX_CompatRecordDlightState( int event )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordDlightState( event );
#else
	(void)event;
#endif
}

static ID_INLINE void GLX_CompatRecordDlightBuild( int legacyLights,
	int legacySkippedLights, int legacyNoHitLights, int legacyVertexes,
	int legacyIndexes, int legacyLitIndexes, int pmPasses, int pmVertexes,
	int pmIndexes )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordDlightBuild( legacyLights, legacySkippedLights,
		legacyNoHitLights, legacyVertexes, legacyIndexes, legacyLitIndexes,
		pmPasses, pmVertexes, pmIndexes );
#else
	(void)legacyLights;
	(void)legacySkippedLights;
	(void)legacyNoHitLights;
	(void)legacyVertexes;
	(void)legacyIndexes;
	(void)legacyLitIndexes;
	(void)pmPasses;
	(void)pmVertexes;
	(void)pmIndexes;
#endif
}

static ID_INLINE void GLX_CompatRecordDlightCull( int legacyVertexes,
	int legacyIndexes )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordDlightCull( legacyVertexes, legacyIndexes );
#else
	(void)legacyVertexes;
	(void)legacyIndexes;
#endif
}

static ID_INLINE void GLX_CompatRecordDlightScissor( qboolean computed,
	qboolean applied, int x, int y, int width, int height, int viewportWidth,
	int viewportHeight )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordDlightScissor( computed, applied, x, y, width, height,
		viewportWidth, viewportHeight );
#else
	(void)computed;
	(void)applied;
	(void)x;
	(void)y;
	(void)width;
	(void)height;
	(void)viewportWidth;
	(void)viewportHeight;
#endif
}

static ID_INLINE void GLX_CompatRecordProjectedDlights(
	const glxProjectedDlightRecord_t *records, int count )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordProjectedDlights( records, count );
#else
	(void)records;
	(void)count;
#endif
}

static ID_INLINE void GLX_CompatRecordProjectedDlightList( int itemIndex,
	unsigned int lightMask )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordProjectedDlightList( itemIndex, lightMask );
#else
	(void)itemIndex;
	(void)lightMask;
#endif
}

static ID_INLINE qboolean GLX_CompatStreamDrawEnabled( void )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_StreamDrawEnabled();
#else
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatStreamDrawMultitextureEnabled( void )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_StreamDrawMultitextureEnabled();
#else
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatStreamDrawFogEnabled( void )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_StreamDrawFogEnabled();
#else
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatStreamDrawDepthFragmentEnabled( void )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_StreamDrawDepthFragmentEnabled();
#else
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatStreamDrawShadowsEnabled( void )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_StreamDrawShadowsEnabled();
#else
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatStreamDrawBeamsEnabled( void )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_StreamDrawBeamsEnabled();
#else
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatStreamDrawPostProcessEnabled( void )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_StreamDrawPostProcessEnabled();
#else
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatStreamDrawAllowsMaterial( int flags,
	unsigned int stateBits, int rgbGen, int alphaGen, int tcGen0, int tcGen1,
	int texMods0, int texMods1, unsigned int texModTypes0, unsigned int texModTypes1,
	unsigned int texModSequence0, unsigned int texModSequence1,
	int rgbWaveFunc, int alphaWaveFunc,
	unsigned int texModWaveFuncs0, unsigned int texModWaveFuncs1,
	int fogAdjust, int materialCombine, qboolean fogPass )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_StreamDrawAllowsMaterial( flags, stateBits, rgbGen, alphaGen,
		tcGen0, tcGen1, texMods0, texMods1, texModTypes0, texModTypes1,
		texModSequence0, texModSequence1, rgbWaveFunc, alphaWaveFunc,
		texModWaveFuncs0, texModWaveFuncs1, fogAdjust, materialCombine, fogPass );
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
	(void)materialCombine;
	(void)fogPass;
	return qfalse;
#endif
}

static ID_INLINE void GLX_CompatRecordStreamDrawSkip( int reason )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordStreamDrawSkip( reason );
#else
	(void)reason;
#endif
}

static ID_INLINE void GLX_CompatResetImageColorAudit( void )
{
#ifdef RENDERER_GLX
	GLX_Renderer_ResetImageColorAudit();
#endif
}

static ID_INLINE void GLX_CompatRecordImageColorAudit( int colorSpace, qboolean srgbDecode )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordImageColorAudit( colorSpace, srgbDecode );
#else
	(void)colorSpace;
	(void)srgbDecode;
#endif
}

static ID_INLINE qboolean GLX_CompatStreamReserve( int bytes, int alignment,
	glxStreamReservation_t *reservation )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_StreamReserve( bytes, alignment, reservation );
#else
	(void)bytes;
	(void)alignment;
	(void)reservation;
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatStreamUploadAt( glxStreamReservation_t *reservation,
	int relativeOffset, const void *data, int bytes )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_StreamUploadAt( reservation, relativeOffset, data, bytes );
#else
	(void)reservation;
	(void)relativeOffset;
	(void)data;
	(void)bytes;
	return qfalse;
#endif
}

static ID_INLINE void GLX_CompatStreamCommit( glxStreamReservation_t *reservation )
{
#ifdef RENDERER_GLX
	GLX_Renderer_StreamCommit( reservation );
#else
	(void)reservation;
#endif
}

static ID_INLINE void GLX_CompatRecordStreamDlightReservation( const glxStreamReservation_t *reservation )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordStreamDlightReservation( reservation );
#else
	(void)reservation;
#endif
}

static ID_INLINE unsigned int GLX_CompatBindStreamArrayBuffer( unsigned int buffer )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_BindStreamArrayBuffer( buffer );
#else
	(void)buffer;
	return 0u;
#endif
}

static ID_INLINE void GLX_CompatRestoreStreamArrayBuffer( unsigned int buffer )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RestoreStreamArrayBuffer( buffer );
#else
	(void)buffer;
#endif
}

static ID_INLINE unsigned int GLX_CompatBindStreamElementArrayBuffer( unsigned int buffer )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_BindStreamElementArrayBuffer( buffer );
#else
	(void)buffer;
	return 0u;
#endif
}

static ID_INLINE void GLX_CompatRestoreStreamElementArrayBuffer( unsigned int buffer )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RestoreStreamElementArrayBuffer( buffer );
#else
	(void)buffer;
#endif
}

static ID_INLINE void GLX_CompatRecordStreamBufferBind( unsigned int target, unsigned int buffer )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordStreamBufferBind( target, buffer );
#else
	(void)target;
	(void)buffer;
#endif
}

static ID_INLINE void GLX_CompatRecordStreamDrawResult( int numVertexes, int numIndexes,
	int totalBytes, int indexBytes, int texcoord1Bytes, qboolean multitexture,
	qboolean fog, qboolean depthFragment, int materialFlags, unsigned int categoryMask,
	qboolean success )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordStreamDrawResult( numVertexes, numIndexes, totalBytes, indexBytes,
		texcoord1Bytes, multitexture, fog, depthFragment, materialFlags, categoryMask, success );
#else
	(void)numVertexes;
	(void)numIndexes;
	(void)totalBytes;
	(void)indexBytes;
	(void)texcoord1Bytes;
	(void)multitexture;
	(void)fog;
	(void)depthFragment;
	(void)materialFlags;
	(void)categoryMask;
	(void)success;
#endif
}

static ID_INLINE qboolean GLX_CompatBindFogMaterial( void )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_BindFogMaterial();
#else
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatBindLiquidMaterial( const float *params,
	const float *eyeAndCount, const float *targetInverse, const float *reflect,
	const float *impulses, const float *amplitudes )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_BindLiquidMaterial( params, eyeAndCount, targetInverse,
		reflect, impulses, amplitudes );
#else
	(void)params;
	(void)eyeAndCount;
	(void)targetInverse;
	(void)reflect;
	(void)impulses;
	(void)amplitudes;
	return qfalse;
#endif
}

static ID_INLINE void GLX_CompatUnbindMaterial( void )
{
#ifdef RENDERER_GLX
	GLX_Renderer_UnbindMaterial();
#endif
}

static ID_INLINE void GLX_CompatRecordFboInit( qboolean requested, qboolean ready,
	qboolean programReady, qboolean framebufferFnsReady, int vidWidth, int vidHeight,
	int captureWidth, int captureHeight, int windowWidth, int windowHeight,
	int internalFormat, int textureFormat, int textureType, qboolean multiSampled,
	qboolean fboSuperSampled, qboolean fboWindowAdjusted, int blitFilter, int hdrMode,
	int renderScaleMode, int bloomMode, qboolean textureSrgbReady,
	qboolean framebufferSrgbReady, qboolean framebufferSrgbEnabled )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordFboInit( requested, ready, programReady, framebufferFnsReady,
		vidWidth, vidHeight, captureWidth, captureHeight, windowWidth, windowHeight,
		internalFormat, textureFormat, textureType, multiSampled, fboSuperSampled,
		fboWindowAdjusted, blitFilter, hdrMode, renderScaleMode, bloomMode,
		textureSrgbReady, framebufferSrgbReady, framebufferSrgbEnabled );
#else
	(void)requested;
	(void)ready;
	(void)programReady;
	(void)framebufferFnsReady;
	(void)vidWidth;
	(void)vidHeight;
	(void)captureWidth;
	(void)captureHeight;
	(void)windowWidth;
	(void)windowHeight;
	(void)internalFormat;
	(void)textureFormat;
	(void)textureType;
	(void)multiSampled;
	(void)fboSuperSampled;
	(void)fboWindowAdjusted;
	(void)blitFilter;
	(void)hdrMode;
	(void)renderScaleMode;
	(void)bloomMode;
	(void)textureSrgbReady;
	(void)framebufferSrgbReady;
	(void)framebufferSrgbEnabled;
#endif
}

static ID_INLINE void GLX_CompatRecordFboShutdown( void )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordFboShutdown();
#endif
}

static ID_INLINE void GLX_CompatRecordPostProcessFrame( qboolean minimized,
	qboolean bloomAvailable, qboolean programReady, int screenshotMask,
	qboolean postWindowAdjusted, int fboReadIndex, int hdrMode, int renderScaleMode,
	float greyscale, float legacyGamma, float legacyOverbright )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordPostProcessFrame( minimized, bloomAvailable, programReady,
		screenshotMask, postWindowAdjusted, fboReadIndex, hdrMode, renderScaleMode,
		greyscale, legacyGamma, legacyOverbright );
#else
	(void)minimized;
	(void)bloomAvailable;
	(void)programReady;
	(void)screenshotMask;
	(void)postWindowAdjusted;
	(void)fboReadIndex;
	(void)hdrMode;
	(void)renderScaleMode;
	(void)greyscale;
	(void)legacyGamma;
	(void)legacyOverbright;
#endif
}

static ID_INLINE qboolean GLX_CompatAutoExposureNeedsSamples( int *width, int *height )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_AutoExposureNeedsSamples( width, height );
#else
	if ( width ) {
		*width = 0;
	}
	if ( height ) {
		*height = 0;
	}
	return qfalse;
#endif
}

static ID_INLINE float GLX_CompatUpdateAutoExposure( float manualExposure,
	const float *rgba, int width, int height )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_UpdateAutoExposure( manualExposure, rgba, width, height );
#else
	(void)rgba;
	(void)width;
	(void)height;
	return manualExposure;
#endif
}

static ID_INLINE qboolean GLX_CompatTryBindPostShaderDirectFinal( void )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_TryBindPostShaderDirectFinal();
#else
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatTryBindPostShaderFinal( qboolean bloomComposite,
	qboolean outputTransform, float bloomIntensity )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_TryBindPostShaderFinal( bloomComposite, outputTransform,
		bloomIntensity );
#else
	(void)bloomComposite;
	(void)outputTransform;
	(void)bloomIntensity;
	return qfalse;
#endif
}

static ID_INLINE void GLX_CompatUnbindPostShader( void )
{
#ifdef RENDERER_GLX
	GLX_Renderer_UnbindPostShader();
#endif
}

static ID_INLINE void GLX_CompatRecordPostProcessResult( int result )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordPostProcessResult( result );
#else
	(void)result;
#endif
}

static ID_INLINE void GLX_CompatRecordColorGradeLut( qboolean active, int size, float scale )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordColorGradeLut( active, size, scale );
#else
	(void)active;
	(void)size;
	(void)scale;
#endif
}

static ID_INLINE void GLX_CompatRecordBloomCreate( int result, int requestedPasses,
	int effectivePasses, int textureUnits, int formatMode, int internalFormat,
	int textureFormat, int textureType )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordBloomCreate( result, requestedPasses, effectivePasses,
		textureUnits, formatMode, internalFormat, textureFormat, textureType );
#else
	(void)result;
	(void)requestedPasses;
	(void)effectivePasses;
	(void)textureUnits;
	(void)formatMode;
	(void)internalFormat;
	(void)textureFormat;
	(void)textureType;
#endif
}

static ID_INLINE void GLX_CompatRecordBloom( int result, qboolean finalStage,
	int bloomMode, int requestedPasses, int effectivePasses, int blendBase,
	int filterSize, int textureUnits, int thresholdMode, int modulate,
	float threshold, float intensity, float reflection )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordBloom( result, finalStage, bloomMode, requestedPasses,
		effectivePasses, blendBase, filterSize, textureUnits, thresholdMode,
		modulate, threshold, intensity, reflection );
#else
	(void)result;
	(void)finalStage;
	(void)bloomMode;
	(void)requestedPasses;
	(void)effectivePasses;
	(void)blendBase;
	(void)filterSize;
	(void)textureUnits;
	(void)thresholdMode;
	(void)modulate;
	(void)threshold;
	(void)intensity;
	(void)reflection;
#endif
}

static ID_INLINE void GLX_CompatRecordFboCopyScreen( int viewportWidth, int viewportHeight )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordFboCopyScreen( viewportWidth, viewportHeight );
#else
	(void)viewportWidth;
	(void)viewportHeight;
#endif
}

static ID_INLINE void GLX_CompatRecordFboBlit( int kind, qboolean depthOnly,
	int srcWidth, int srcHeight, int dstWidth, int dstHeight )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordFboBlit( kind, depthOnly, srcWidth, srcHeight, dstWidth, dstHeight );
#else
	(void)kind;
	(void)depthOnly;
	(void)srcWidth;
	(void)srcHeight;
	(void)dstWidth;
	(void)dstHeight;
#endif
}

static ID_INLINE void GLX_CompatRecordFboBind( void )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordFboBind();
#endif
}

static ID_INLINE void GLX_CompatRecordPostClear( void )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordPostClear();
#endif
}

static ID_INLINE void GLX_CompatRecordFullscreenPass( void )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordFullscreenPass();
#endif
}

static ID_INLINE void GLX_CompatRecordStaticWorldCache( int surfaces, int vertexes,
	int indexes, int vertexBytes, int indexBytes )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordStaticWorldCache( surfaces, vertexes, indexes, vertexBytes, indexBytes );
#else
	(void)surfaces;
	(void)vertexes;
	(void)indexes;
	(void)vertexBytes;
	(void)indexBytes;
#endif
}

static ID_INLINE void GLX_CompatRecordStaticWorldBatches( int batches,
	int largestBatchSurfaces, int faceSurfaces, int gridSurfaces,
	int triangleSurfaces, int shaderStagePasses, int maxShaderStages )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordStaticWorldBatches( batches, largestBatchSurfaces,
		faceSurfaces, gridSurfaces, triangleSurfaces, shaderStagePasses,
		maxShaderStages );
#else
	(void)batches;
	(void)largestBatchSurfaces;
	(void)faceSurfaces;
	(void)gridSurfaces;
	(void)triangleSurfaces;
	(void)shaderStagePasses;
	(void)maxShaderStages;
#endif
}

static ID_INLINE void GLX_CompatRecordStaticWorldPacket( const char *shaderName, int sort,
	int surfaces, int vertexes, int indexes, int firstItem, int itemCount,
	int vertexOffset, int vertexBytes, int indexOffset, int indexBytes,
	int shaderStagePasses, int flags )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordStaticWorldPacket( shaderName, sort, surfaces, vertexes, indexes,
		firstItem, itemCount, vertexOffset, vertexBytes, indexOffset, indexBytes,
		shaderStagePasses, flags );
#else
	(void)shaderName;
	(void)sort;
	(void)surfaces;
	(void)vertexes;
	(void)indexes;
	(void)firstItem;
	(void)itemCount;
	(void)vertexOffset;
	(void)vertexBytes;
	(void)indexOffset;
	(void)indexBytes;
	(void)shaderStagePasses;
	(void)flags;
#endif
}

static ID_INLINE void GLX_CompatUploadStaticWorldArena( const void *vertexData,
	int vertexBytes, const void *indexData, int indexBytes )
{
#ifdef RENDERER_GLX
	GLX_Renderer_UploadStaticWorldArena( vertexData, vertexBytes, indexData, indexBytes );
#else
	(void)vertexData;
	(void)vertexBytes;
	(void)indexData;
	(void)indexBytes;
#endif
}

static ID_INLINE unsigned int GLX_CompatStaticWorldArenaVertexBuffer( void )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_StaticWorldArenaVertexBuffer();
#else
	return 0;
#endif
}

static ID_INLINE unsigned int GLX_CompatStaticWorldArenaIndexBuffer( void )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_StaticWorldArenaIndexBuffer();
#else
	return 0;
#endif
}

static ID_INLINE qboolean GLX_CompatStaticWorldDrawDeviceRun( int indexes,
	int offsetBytes, int firstItem, int itemCount, unsigned int indexType,
	int indexBytes, const char *shaderName, int sort, qboolean arenaBound )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_StaticWorldDrawDeviceRun( indexes, offsetBytes,
		firstItem, itemCount, indexType, indexBytes, shaderName, sort, arenaBound );
#else
	(void)indexes;
	(void)offsetBytes;
	(void)firstItem;
	(void)itemCount;
	(void)indexType;
	(void)indexBytes;
	(void)shaderName;
	(void)sort;
	(void)arenaBound;
	return qfalse;
#endif
}

static ID_INLINE qboolean GLX_CompatStaticWorldDrawSoftIndexes( int indexes,
	const void *indexData, unsigned int indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_StaticWorldDrawSoftIndexes( indexes, indexData, indexType,
		indexBytes, shaderName, sort, arenaBound );
#else
	(void)indexes;
	(void)indexData;
	(void)indexType;
	(void)indexBytes;
	(void)shaderName;
	(void)sort;
	(void)arenaBound;
	return qfalse;
#endif
}

static ID_INLINE int GLX_CompatStaticWorldDrawDeviceRunsFiltered( int runCount,
	const int *counts, const void *const *offsets, const int *firstItems,
	const int *itemCounts, int *drawnRuns, unsigned int indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_StaticWorldDrawDeviceRunsFiltered( runCount, counts, offsets,
		firstItems, itemCounts, drawnRuns, indexType, indexBytes, shaderName, sort,
		arenaBound );
#else
	(void)runCount;
	(void)counts;
	(void)offsets;
	(void)firstItems;
	(void)itemCounts;
	(void)drawnRuns;
	(void)indexType;
	(void)indexBytes;
	(void)shaderName;
	(void)sort;
	(void)arenaBound;
	return 0;
#endif
}

static ID_INLINE int GLX_CompatStaticWorldDrawProjectedDlightRuns( int runCount,
	const int *counts, const void *const *offsets, const int *firstItems,
	const int *itemCounts, int *drawnRuns, unsigned int indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound )
{
#ifdef RENDERER_GLX
	return GLX_Renderer_StaticWorldDrawProjectedDlightRuns( runCount, counts, offsets,
		firstItems, itemCounts, drawnRuns, indexType, indexBytes, shaderName, sort,
		arenaBound );
#else
	(void)runCount;
	(void)counts;
	(void)offsets;
	(void)firstItems;
	(void)itemCounts;
	(void)drawnRuns;
	(void)indexType;
	(void)indexBytes;
	(void)shaderName;
	(void)sort;
	(void)arenaBound;
	return 0;
#endif
}

static ID_INLINE void GLX_CompatRecordStaticWorldQueue( int queuedItems,
	int queuedVertexes, int queuedIndexes, int deviceRuns, int deviceIndexes,
	int softIndexes, int largestDeviceRunIndexes )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordStaticWorldQueue( queuedItems, queuedVertexes, queuedIndexes,
		deviceRuns, deviceIndexes, softIndexes, largestDeviceRunIndexes );
#else
	(void)queuedItems;
	(void)queuedVertexes;
	(void)queuedIndexes;
	(void)deviceRuns;
	(void)deviceIndexes;
	(void)softIndexes;
	(void)largestDeviceRunIndexes;
#endif
}

static ID_INLINE void GLX_CompatRecordStaticWorldDeviceRuns( int runCount,
	const int *counts, const void *const *offsets, const int *firstItems,
	const int *itemCounts, int indexBytes, const char *shaderName, int sort )
{
#ifdef RENDERER_GLX
	GLX_Renderer_RecordStaticWorldDeviceRuns( runCount, counts, offsets,
		firstItems, itemCounts, indexBytes, shaderName, sort );
#else
	(void)runCount;
	(void)counts;
	(void)offsets;
	(void)firstItems;
	(void)itemCounts;
	(void)indexBytes;
	(void)shaderName;
	(void)sort;
#endif
}

#endif // TR_GLX_BRIDGE_H
