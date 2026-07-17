#ifndef TR_GLX_API_H
#define TR_GLX_API_H

#include "../qcommon/q_shared.h"
#include "tr_glx_public.h"

#ifndef __TR_TYPES_H
typedef struct glconfig_s glconfig_t;
#endif

#ifndef __TR_PUBLIC_H
typedef struct refimport_s refimport_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

void GLX_Renderer_RegisterCommands( void );
void GLX_Renderer_RemoveCommands( void );
void GLX_Renderer_SetImports( refimport_t *imports );
void GLX_Renderer_OnOpenGLReady( const glconfig_t *config, const char *extensions );
void GLX_Renderer_Shutdown( int code );
void GLX_Renderer_BeginBackendTimer( void );
void GLX_Renderer_EndBackendTimer( void );
void GLX_Renderer_BeginGpuPassTimer( int pass );
void GLX_Renderer_EndGpuPassTimer( int pass );
void GLX_Renderer_FrameComplete( void );
void GLX_Renderer_PrintCaps_f( void );
void GLX_Renderer_PrintInfo_f( void );
void GLX_Renderer_Profile_f( void );
void GLX_Renderer_Material_f( void );
void GLX_Renderer_PostProcess_f( void );
void GLX_Renderer_StaticWorld_f( void );
void GLX_Renderer_StreamTest_f( void );
void GLX_Renderer_PrintFrameCounters( void );
qboolean GLX_Renderer_DrawElements( unsigned int mode, int count,
	unsigned int type, const void *indices, int legacyReason, int profilerPath );
qboolean GLX_Renderer_DrawArrays( unsigned int mode, int first, int count,
	int legacyReason, int profilerPath );
qboolean GLX_Renderer_DrawElementsClassified( unsigned int mode, int count,
	unsigned int type, const void *indices, int legacyReason, int profilerPath,
	int materialFlags, unsigned int categoryMask );
qboolean GLX_Renderer_DrawElementsClassifiedProjectedDlights( unsigned int mode,
	int count, unsigned int type, const void *indices, int legacyReason,
	int profilerPath, int materialFlags, unsigned int categoryMask,
	unsigned int lightMask );
qboolean GLX_Renderer_DrawArraysClassified( unsigned int mode, int first, int count,
	int legacyReason, int profilerPath, int materialFlags, unsigned int categoryMask );
void GLX_Renderer_RecordDraw( int indexes, int path );
void GLX_Renderer_RecordShaderBatch( const char *shaderName, int sort, int numPasses,
	int numVertexes, int numIndexes, int flags );
void GLX_Renderer_RecordMaterialStage( int path, int flags, unsigned int stateBits,
	int rgbGen, int alphaGen, int tcGen0, int tcGen1, int texMods0, int texMods1,
	unsigned int texModTypes0, unsigned int texModTypes1,
	unsigned int texModSequence0, unsigned int texModSequence1,
	int rgbWaveFunc, int alphaWaveFunc,
	unsigned int texModWaveFuncs0, unsigned int texModWaveFuncs1,
	int fogAdjust, int materialCombine, qboolean fogPass,
	int numVertexes, int numIndexes );
qboolean GLX_Renderer_MaterialRendererActive( void );
qboolean GLX_Renderer_BindMaterialStage( int flags, unsigned int stateBits,
	int rgbGen, int alphaGen, int tcGen0, int tcGen1, int texMods0, int texMods1,
	unsigned int texModTypes0, unsigned int texModTypes1,
	unsigned int texModSequence0, unsigned int texModSequence1,
	int rgbWaveFunc, int alphaWaveFunc,
	unsigned int texModWaveFuncs0, unsigned int texModWaveFuncs1, int fogAdjust,
	int materialCombine, qboolean fogPass );
qboolean GLX_Renderer_BindFogMaterial( void );
qboolean GLX_Renderer_BindLiquidMaterial( const float *params,
	const float *eyeAndCount, const float *targetInverse, const float *reflect,
	const float *impulses, const float *amplitudes );
void GLX_Renderer_UnbindMaterial( void );
qboolean GLX_Renderer_DlightProgramAvailable( qboolean linear, int fogMode,
	qboolean absLight, qboolean shadow );
qboolean GLX_Renderer_BindDlightProgram( qboolean linear, int fogMode,
	qboolean absLight, qboolean shadow, const float *eyePos, const float *lightPos,
	const float *lightColor, const float *lightVector, const float *texFactors,
	const float *dlightFactors, const float *fogDistanceVector,
	const float *fogDepthVector, float fogEyeT, const float *dlightShadow,
	const float *shadowAtlas, const float *shadowDepth, const float *shadowFilter );
void GLX_Renderer_UnbindDlightProgram( void );
qboolean GLX_Renderer_DlightProjectedProgramEnabled( void );
qboolean GLX_Renderer_ProjectedDlightWorldOverlayActive( void );
qboolean GLX_Renderer_BindProjectedDlightOverlayProgram( void );
qboolean GLX_Renderer_DlightScissorEnabled( void );
void GLX_Renderer_RecordDlightState( int event );
void GLX_Renderer_RecordDlightBuild( int legacyLights, int legacySkippedLights,
	int legacyNoHitLights, int legacyVertexes, int legacyIndexes, int legacyLitIndexes,
	int pmPasses, int pmVertexes, int pmIndexes );
void GLX_Renderer_RecordDlightCull( int legacyVertexes, int legacyIndexes );
void GLX_Renderer_RecordDlightScissor( qboolean computed, qboolean applied,
	int x, int y, int width, int height, int viewportWidth, int viewportHeight );
void GLX_Renderer_RecordProjectedDlights( const glxProjectedDlightRecord_t *records, int count );
void GLX_Renderer_RecordProjectedDlightList( int itemIndex, unsigned int lightMask );
qboolean GLX_Renderer_StreamDrawEnabled( void );
qboolean GLX_Renderer_StreamDrawMultitextureEnabled( void );
qboolean GLX_Renderer_StreamDrawFogEnabled( void );
qboolean GLX_Renderer_StreamDrawDepthFragmentEnabled( void );
qboolean GLX_Renderer_StreamDrawShadowsEnabled( void );
qboolean GLX_Renderer_StreamDrawBeamsEnabled( void );
qboolean GLX_Renderer_StreamDrawPostProcessEnabled( void );
qboolean GLX_Renderer_StreamDrawAllowsMaterial( int flags, unsigned int stateBits,
	int rgbGen, int alphaGen, int tcGen0, int tcGen1, int texMods0, int texMods1,
	unsigned int texModTypes0, unsigned int texModTypes1,
	unsigned int texModSequence0, unsigned int texModSequence1,
	int rgbWaveFunc, int alphaWaveFunc,
	unsigned int texModWaveFuncs0, unsigned int texModWaveFuncs1,
	int fogAdjust, int materialCombine, qboolean fogPass );
qboolean GLX_Renderer_StreamReserve( int bytes, int alignment, glxStreamReservation_t *reservation );
qboolean GLX_Renderer_StreamUploadAt( glxStreamReservation_t *reservation, int relativeOffset,
	const void *data, int bytes );
void GLX_Renderer_StreamCommit( glxStreamReservation_t *reservation );
void GLX_Renderer_RecordStreamDlightReservation( const glxStreamReservation_t *reservation );
unsigned int GLX_Renderer_BindStreamArrayBuffer( unsigned int buffer );
void GLX_Renderer_RestoreStreamArrayBuffer( unsigned int buffer );
unsigned int GLX_Renderer_BindStreamElementArrayBuffer( unsigned int buffer );
void GLX_Renderer_RestoreStreamElementArrayBuffer( unsigned int buffer );
void GLX_Renderer_RecordStreamBufferBind( unsigned int target, unsigned int buffer );
void GLX_Renderer_RecordStreamDrawResult( int numVertexes, int numIndexes,
	int totalBytes, int indexBytes, int texcoord1Bytes, qboolean multitexture, qboolean fog,
	qboolean depthFragment, int materialFlags, unsigned int categoryMask, qboolean success );
void GLX_Renderer_RecordStreamDrawSkip( int reason );
void GLX_Renderer_ResetImageColorAudit( void );
void GLX_Renderer_RecordImageColorAudit( int colorSpace, qboolean srgbDecode );
void GLX_Renderer_RecordFboInit( qboolean requested, qboolean ready,
	qboolean programReady, qboolean framebufferFnsReady, int vidWidth, int vidHeight,
	int captureWidth, int captureHeight, int windowWidth, int windowHeight,
	int internalFormat, int textureFormat, int textureType, qboolean multiSampled,
	qboolean superSampled, qboolean windowAdjusted, int blitFilter, int hdrMode,
	int renderScaleMode, int bloomMode, qboolean textureSrgbAvailable,
	qboolean framebufferSrgbAvailable, qboolean framebufferSrgbEnabled );
void GLX_Renderer_RecordFboShutdown( void );
void GLX_Renderer_RecordPostProcessFrame( qboolean minimized, qboolean bloomAvailable,
	qboolean programReady, int screenshotMask, qboolean windowAdjusted, int fboReadIndex,
	int hdrMode, int renderScaleMode, float greyscale, float legacyGamma,
	float legacyOverbright );
qboolean GLX_Renderer_AutoExposureNeedsSamples( int *width, int *height );
float GLX_Renderer_UpdateAutoExposure( float manualExposure, const float *rgba,
	int width, int height );
qboolean GLX_Renderer_TryBindPostShaderFinal( qboolean bloomComposite,
	qboolean outputTransform, float bloomIntensity );
qboolean GLX_Renderer_TryBindPostShaderDirectFinal( void );
void GLX_Renderer_UnbindPostShader( void );
void GLX_Renderer_RecordPostProcessResult( int result );
void GLX_Renderer_RecordColorGradeLut( qboolean active, int size, float scale );
void GLX_Renderer_RecordBloomCreate( int result, int requestedPasses,
	int effectivePasses, int textureUnits, int formatMode, int internalFormat,
	int textureFormat, int textureType );
void GLX_Renderer_RecordBloom( int result, qboolean finalStage, int bloomMode,
	int requestedPasses, int effectivePasses, int blendBase, int filterSize,
	int textureUnits, int thresholdMode, int modulate, float threshold, float intensity,
	float reflection );
void GLX_Renderer_RecordFboCopyScreen( int viewportWidth, int viewportHeight );
void GLX_Renderer_RecordFboBlit( int kind, qboolean depthOnly,
	int srcWidth, int srcHeight, int dstWidth, int dstHeight );
void GLX_Renderer_RecordFboBind( void );
void GLX_Renderer_RecordPostClear( void );
void GLX_Renderer_RecordFullscreenPass( void );
void GLX_Renderer_PushShaderDebugGroup( const char *shaderName, int numVertexes, int numIndexes, int numPasses );
void GLX_Renderer_PopDebugGroup( void );
void GLX_Renderer_ShadowUploadTess( int numVertexes, int numIndexes,
	const void *xyz, int xyzBytes, const void *indexes, int indexBytes );
void GLX_Renderer_RecordStaticWorldCache( int surfaces, int vertexes, int indexes, int vertexBytes, int indexBytes );
void GLX_Renderer_RecordStaticWorldBatches( int batches, int largestBatchSurfaces,
	int faceSurfaces, int gridSurfaces, int triangleSurfaces, int shaderStagePasses, int maxShaderStages );
void GLX_Renderer_RecordStaticWorldPacket( const char *shaderName, int sort,
	int surfaces, int vertexes, int indexes, int firstItem, int itemCount, int vertexOffset, int vertexBytes,
	int indexOffset, int indexBytes, int shaderStagePasses, int flags );
void GLX_Renderer_UploadStaticWorldArena( const void *vertexData, int vertexBytes,
	const void *indexData, int indexBytes );
unsigned int GLX_Renderer_StaticWorldArenaVertexBuffer( void );
unsigned int GLX_Renderer_StaticWorldArenaIndexBuffer( void );
qboolean GLX_Renderer_StaticWorldDrawDeviceRun( int indexes, int offsetBytes,
	int firstItem, int itemCount, unsigned int indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound );
qboolean GLX_Renderer_StaticWorldDrawDeviceRuns( int runCount, const int *counts,
	const void *const *offsets, const int *firstItems, const int *itemCounts,
	unsigned int indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound );
qboolean GLX_Renderer_StaticWorldDrawSoftIndexes( int indexes, const void *indexData,
	unsigned int indexType, int indexBytes, const char *shaderName, int sort, qboolean arenaBound );
int GLX_Renderer_StaticWorldDrawDeviceRunsFiltered( int runCount, const int *counts,
	const void *const *offsets, const int *firstItems, const int *itemCounts,
	int *drawnRuns, unsigned int indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound );
int GLX_Renderer_StaticWorldDrawProjectedDlightRuns( int runCount, const int *counts,
	const void *const *offsets, const int *firstItems, const int *itemCounts,
	int *drawnRuns, unsigned int indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound );
void GLX_Renderer_RecordStaticWorldQueue( int queuedItems, int queuedVertexes, int queuedIndexes,
	int deviceRuns, int deviceIndexes, int softIndexes, int largestDeviceRunIndexes );
void GLX_Renderer_RecordStaticWorldDeviceRuns( int runCount, const int *counts,
	const void *const *offsets, const int *firstItems, const int *itemCounts,
	int indexBytes, const char *shaderName, int sort );

#ifdef __cplusplus
}
#endif

#endif // TR_GLX_API_H
