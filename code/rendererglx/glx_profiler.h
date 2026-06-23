#ifndef GLX_PROFILER_H
#define GLX_PROFILER_H

#include "glx_caps.h"

namespace glx {

typedef unsigned long long GLXQueryResult64;
typedef void ( APIENTRY *PFNGLXGENQUERIESPROC )( GLsizei n, GLuint *ids );
typedef void ( APIENTRY *PFNGLXDELETEQUERIESPROC )( GLsizei n, const GLuint *ids );
typedef void ( APIENTRY *PFNGLXBEGINQUERYPROC )( GLenum target, GLuint id );
typedef void ( APIENTRY *PFNGLXENDQUERYPROC )( GLenum target );
typedef void ( APIENTRY *PFNGLXGETQUERYOBJECTIVPROC )( GLuint id, GLenum pname, GLint *params );
typedef void ( APIENTRY *PFNGLXGETQUERYOBJECTUIVPROC )( GLuint id, GLenum pname, GLuint *params );
typedef void ( APIENTRY *PFNGLXGETQUERYOBJECTUI64VPROC )( GLuint id, GLenum pname, GLXQueryResult64 *params );
typedef void ( APIENTRY *PFNGLXQUERYCOUNTERPROC )( GLuint id, GLenum target );

static constexpr int GLX_MAX_HOT_SHADERS = 8;
static constexpr int GLX_MAX_HOT_MATERIAL_KEYS = 12;
static constexpr int GLX_PASS_HISTOGRAM_BUCKETS = 9;
static constexpr int GLX_SORT_HISTOGRAM_BUCKETS = 32;
static constexpr int GLX_STAGE_GEN_HISTOGRAM_BUCKETS = 16;
static constexpr int GLX_STAGE_TEXMOD_HISTOGRAM_BUCKETS = 9;

struct HotShaderStats {
	char name[MAX_QPATH];
	unsigned int batches;
	unsigned int indexes;
	unsigned int vertexes;
};

struct MaterialKeyStats {
	unsigned int stages;
	unsigned int indexes;
	unsigned int vertexes;
	unsigned int stateBits;
	int path;
	int flags;
	int rgbGen;
	int alphaGen;
	int tcGen0;
	int tcGen1;
	int texMods0;
	int texMods1;
};

struct GpuPassStats {
	unsigned int samples;
	double lastMilliseconds;
	double totalMilliseconds;
	char lastText[32];
};

struct GpuPassQuery {
	GLuint startQuery;
	GLuint endQuery;
	qboolean pending;
	int pass;
};

struct GpuPassScope {
	int pass;
	int slot;
};

struct ProfilerFns {
	PFNGLXGENQUERIESPROC GenQueries;
	PFNGLXDELETEQUERIESPROC DeleteQueries;
	PFNGLXBEGINQUERYPROC BeginQuery;
	PFNGLXENDQUERYPROC EndQuery;
	PFNGLXGETQUERYOBJECTIVPROC GetQueryObjectiv;
	PFNGLXGETQUERYOBJECTUIVPROC GetQueryObjectuiv;
	PFNGLXGETQUERYOBJECTUI64VPROC GetQueryObjectui64v;
	PFNGLXQUERYCOUNTERPROC QueryCounter;
};

struct ProfilerState {
	cvar_t *r_glxGpuTiming;
	cvar_t *r_glxGpuPassTiming;
	ProfilerFns fns;
	GLuint queries[4];
	qboolean pending[4];
	int writeIndex;
	GpuPassQuery passQueries[16];
	GpuPassScope passStack[8];
	int passWriteIndex;
	int passStackDepth;
	qboolean passTimerReady;
	qboolean initialized;
	qboolean queryActive;
	qboolean lastGpuValid;
	double lastGpuMilliseconds;
	char lastGpuText[32];
	unsigned int frames;
	unsigned int backendQueries;
	unsigned int gpuPassQueries;
	unsigned int queryUnavailableFrames;
	unsigned int queryRingFullSkips;
	unsigned int passQueryUnavailableFrames;
	unsigned int passQueryRingFullSkips;
	unsigned int postBlits;
	unsigned int postBinds;
	unsigned int postClears;
	unsigned int postFullscreenPasses;
	GpuPassStats gpuPassStats[GLX_GPU_PASS_COUNT];
	unsigned int drawCalls;
	unsigned int drawIndexes;
	unsigned int genericDrawCalls;
	unsigned int genericDrawIndexes;
	unsigned int vboDeviceDrawCalls;
	unsigned int vboDeviceDrawIndexes;
	unsigned int vboSoftDrawCalls;
	unsigned int vboSoftDrawIndexes;
	unsigned int debugDrawCalls;
	unsigned int debugDrawIndexes;
	unsigned int streamGenericDrawCalls;
	unsigned int streamGenericDrawIndexes;
	unsigned int legacyDelegationCalls;
	unsigned int legacyDelegationItems;
	unsigned int legacyDelegationReasonCalls[GLX_LEGACY_DELEGATION_REASON_COUNT];
	unsigned int legacyDelegationReasonItems[GLX_LEGACY_DELEGATION_REASON_COUNT];
	unsigned int shaderBatches;
	unsigned int shaderBatchIndexes;
	unsigned int shaderBatchVertexes;
	unsigned int shaderStagePasses;
	unsigned int genericShaderBatches;
	unsigned int vboShaderBatches;
	unsigned int fogShaderBatches;
	unsigned int multitextureShaderBatches;
	unsigned int polygonOffsetShaderBatches;
	unsigned int largestShaderBatchIndexes;
	unsigned int largestShaderBatchVertexes;
	char largestShaderBatchName[MAX_QPATH];
	unsigned int passHistogram[GLX_PASS_HISTOGRAM_BUCKETS];
	unsigned int sortHistogram[GLX_SORT_HISTOGRAM_BUCKETS];
	unsigned int sortOverflowBatches;
	HotShaderStats hotShaders[GLX_MAX_HOT_SHADERS];
	unsigned int materialStages;
	unsigned int materialStageIndexes;
	unsigned int materialStageVertexes;
	unsigned int genericMaterialStages;
	unsigned int vboMaterialStages;
	unsigned int multitextureMaterialStages;
	unsigned int depthFragmentMaterialStages;
	unsigned int blendMaterialStages;
	unsigned int alphaTestMaterialStages;
	unsigned int depthWriteMaterialStages;
	unsigned int lightmapMaterialStages;
	unsigned int animatedImageMaterialStages;
	unsigned int videoMapMaterialStages;
	unsigned int screenMapMaterialStages;
	unsigned int dlightMapMaterialStages;
	unsigned int texmodMaterialStages;
	unsigned int environmentMaterialStages;
	unsigned int st0MaterialStages;
	unsigned int st1MaterialStages;
	unsigned int rgbGenHistogram[GLX_STAGE_GEN_HISTOGRAM_BUCKETS];
	unsigned int alphaGenHistogram[GLX_STAGE_GEN_HISTOGRAM_BUCKETS];
	unsigned int tcGenHistogram[GLX_STAGE_GEN_HISTOGRAM_BUCKETS];
	unsigned int texModHistogram[GLX_STAGE_TEXMOD_HISTOGRAM_BUCKETS];
	MaterialKeyStats hotMaterialKeys[GLX_MAX_HOT_MATERIAL_KEYS];
};

void GLX_Profiler_RegisterCvars( ProfilerState *state );
void GLX_Profiler_OnOpenGLReady( ProfilerState *state, const Capabilities &caps );
void GLX_Profiler_Shutdown( ProfilerState *state );
void GLX_Profiler_BeginBackendTimer( ProfilerState *state );
void GLX_Profiler_EndBackendTimer( ProfilerState *state );
void GLX_Profiler_BeginGpuPassTimer( ProfilerState *state, int pass );
void GLX_Profiler_EndGpuPassTimer( ProfilerState *state, int pass );
void GLX_Profiler_FrameComplete( ProfilerState *state );
void GLX_Profiler_RecordPostBlit( ProfilerState *state );
void GLX_Profiler_RecordPostBind( ProfilerState *state );
void GLX_Profiler_RecordPostClear( ProfilerState *state );
void GLX_Profiler_RecordFullscreenPass( ProfilerState *state );
void GLX_Profiler_RecordDraw( ProfilerState *state, int indexes, int path );
void GLX_Profiler_RecordLegacyDelegation( ProfilerState *state, int reason, int items );
void GLX_Profiler_RecordShaderBatch( ProfilerState *state, const char *shaderName, int sort,
	int numPasses, int numVertexes, int numIndexes, int flags );
void GLX_Profiler_RecordMaterialStage( ProfilerState *state, int path, int flags, unsigned int stateBits,
	int rgbGen, int alphaGen, int tcGen0, int tcGen1, int texMods0, int texMods1,
	int numVertexes, int numIndexes );
void GLX_Profiler_PrintInfo( const ProfilerState &state );
qboolean GLX_Profiler_TimerReady( const ProfilerState &state );
const char *GLX_Profiler_LastGpuTimeText( const ProfilerState &state );
const char *GLX_Profiler_GpuPassName( int pass );

} // namespace glx

#endif // GLX_PROFILER_H
