#include "glx_profiler.h"

#include <cstring>
#include <cstdio>

#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP 0x8E28
#endif

namespace glx {

static constexpr int GLX_QUERY_COUNT = 4;
static constexpr int GLX_PASS_QUERY_COUNT = 16;
static constexpr int GLX_PASS_QUERY_IDS = GLX_PASS_QUERY_COUNT * 2;
static constexpr int GLX_PASS_STACK_DEPTH = 8;

const char *GLX_Profiler_GpuPassName( int pass )
{
	switch ( pass ) {
	case GLX_GPU_PASS_BACKEND:
		return "backend";
	case GLX_GPU_PASS_POSTPROCESS:
		return "postprocess";
	case GLX_GPU_PASS_BLOOM:
		return "bloom";
	case GLX_GPU_PASS_BLOOM_EXTRACT:
		return "bloom-extract";
	case GLX_GPU_PASS_BLOOM_DOWNSCALE:
		return "bloom-downscale";
	case GLX_GPU_PASS_BLOOM_BLUR:
		return "bloom-blur";
	case GLX_GPU_PASS_BLOOM_BLEND:
		return "bloom-blend";
	case GLX_GPU_PASS_BLOOM_FINAL:
		return "bloom-final";
	case GLX_GPU_PASS_BLOOM_LENS_REFLECTION:
		return "bloom-lens-reflection";
	case GLX_GPU_PASS_GAMMA_DIRECT:
		return "gamma-direct";
	case GLX_GPU_PASS_GAMMA_BLIT:
		return "gamma-blit";
	case GLX_GPU_PASS_FBO_BLIT:
		return "fbo-blit";
	case GLX_GPU_PASS_COPY_SCREEN:
		return "copy-screen";
	case GLX_GPU_PASS_FLARE:
		return "flare";
	case GLX_GPU_PASS_DLIGHT_SHADOW:
		return "dlight-shadow-atlas";
	case GLX_GPU_PASS_CSM_SHADOW:
		return "csm-shadow-atlas";
	default:
		return "unknown";
	}
}

static void GLX_Profiler_CopyName( char *dst, size_t dstSize, const char *src )
{
	std::snprintf( dst, dstSize, "%s", src && *src ? src : "<unnamed>" );
}

static HotShaderStats *GLX_Profiler_FindHotShader( ProfilerState *state, const char *shaderName )
{
	HotShaderStats *empty = nullptr;

	for ( int i = 0; i < GLX_MAX_HOT_SHADERS; i++ ) {
		if ( state->hotShaders[i].batches == 0 ) {
			if ( !empty ) {
				empty = &state->hotShaders[i];
			}
			continue;
		}

		if ( !std::strcmp( state->hotShaders[i].name, shaderName ) ) {
			return &state->hotShaders[i];
		}
	}

	return empty;
}

static void GLX_Profiler_RecordHotShader( ProfilerState *state, const char *shaderName, int numVertexes, int numIndexes )
{
	HotShaderStats *slot = GLX_Profiler_FindHotShader( state, shaderName );

	if ( !slot ) {
		int weakest = 0;
		for ( int i = 1; i < GLX_MAX_HOT_SHADERS; i++ ) {
			if ( state->hotShaders[i].indexes < state->hotShaders[weakest].indexes ) {
				weakest = i;
			}
		}

		if ( static_cast<unsigned int>( numIndexes ) <= state->hotShaders[weakest].indexes ) {
			return;
		}

		slot = &state->hotShaders[weakest];
		*slot = {};
	}

	if ( slot->batches == 0 ) {
		GLX_Profiler_CopyName( slot->name, sizeof( slot->name ), shaderName );
	}

	slot->batches++;
	slot->indexes += static_cast<unsigned int>( numIndexes );
	slot->vertexes += static_cast<unsigned int>( numVertexes );
}

static const char *GLX_Profiler_StagePathName( int path )
{
	switch ( path ) {
	case GLX_STAGE_PATH_GENERIC:
		return "generic";
	case GLX_STAGE_PATH_VBO:
		return "vbo";
	default:
		return "unknown";
	}
}

static const char *GLX_Profiler_LegacyDelegationName( int reason )
{
	switch ( reason ) {
	case GLX_LEGACY_DELEGATION_GENERIC:
		return "generic";
	case GLX_LEGACY_DELEGATION_VBO_DEVICE:
		return "vbo-device";
	case GLX_LEGACY_DELEGATION_VBO_SOFT:
		return "vbo-soft";
	case GLX_LEGACY_DELEGATION_DRAW_ARRAY:
		return "draw-array";
	default:
		return "unknown";
	}
}

static int GLX_Profiler_ClampedBucket( int value, int buckets )
{
	if ( value < 0 ) {
		return 0;
	}

	if ( value >= buckets ) {
		return buckets - 1;
	}

	return value;
}

static qboolean GLX_Profiler_MaterialKeyMatches( const MaterialKeyStats &key, int path, int flags, unsigned int stateBits,
	int rgbGen, int alphaGen, int tcGen0, int tcGen1, int texMods0, int texMods1 )
{
	return key.path == path &&
		key.flags == flags &&
		key.stateBits == stateBits &&
		key.rgbGen == rgbGen &&
		key.alphaGen == alphaGen &&
		key.tcGen0 == tcGen0 &&
		key.tcGen1 == tcGen1 &&
		key.texMods0 == texMods0 &&
		key.texMods1 == texMods1 ? qtrue : qfalse;
}

static MaterialKeyStats *GLX_Profiler_FindMaterialKey( ProfilerState *state, int path, int flags, unsigned int stateBits,
	int rgbGen, int alphaGen, int tcGen0, int tcGen1, int texMods0, int texMods1 )
{
	MaterialKeyStats *empty = nullptr;

	for ( int i = 0; i < GLX_MAX_HOT_MATERIAL_KEYS; i++ ) {
		MaterialKeyStats *slot = &state->hotMaterialKeys[i];

		if ( slot->stages == 0 ) {
			if ( !empty ) {
				empty = slot;
			}
			continue;
		}

		if ( GLX_Profiler_MaterialKeyMatches( *slot, path, flags, stateBits,
			rgbGen, alphaGen, tcGen0, tcGen1, texMods0, texMods1 ) ) {
			return slot;
		}
	}

	return empty;
}

static void GLX_Profiler_RecordMaterialKey( ProfilerState *state, int path, int flags, unsigned int stateBits,
	int rgbGen, int alphaGen, int tcGen0, int tcGen1, int texMods0, int texMods1,
	int numVertexes, int numIndexes )
{
	MaterialKeyStats *slot = GLX_Profiler_FindMaterialKey( state, path, flags, stateBits,
		rgbGen, alphaGen, tcGen0, tcGen1, texMods0, texMods1 );

	if ( !slot ) {
		int weakest = 0;
		for ( int i = 1; i < GLX_MAX_HOT_MATERIAL_KEYS; i++ ) {
			if ( state->hotMaterialKeys[i].indexes < state->hotMaterialKeys[weakest].indexes ) {
				weakest = i;
			}
		}

		if ( static_cast<unsigned int>( numIndexes ) <= state->hotMaterialKeys[weakest].indexes ) {
			return;
		}

		slot = &state->hotMaterialKeys[weakest];
		*slot = {};
	}

	if ( slot->stages == 0 ) {
		slot->path = path;
		slot->flags = flags;
		slot->stateBits = stateBits;
		slot->rgbGen = rgbGen;
		slot->alphaGen = alphaGen;
		slot->tcGen0 = tcGen0;
		slot->tcGen1 = tcGen1;
		slot->texMods0 = texMods0;
		slot->texMods1 = texMods1;
	}

	slot->stages++;
	slot->indexes += static_cast<unsigned int>( numIndexes );
	slot->vertexes += static_cast<unsigned int>( numVertexes );
}

static void *GLX_Profiler_GetProc( const char *name, const char *fallbackName = nullptr )
{
	void *proc = RI().GL_GetProcAddress ? RI().GL_GetProcAddress( name ) : nullptr;

	if ( !proc && fallbackName ) {
		proc = RI().GL_GetProcAddress( fallbackName );
	}

	return proc;
}

static qboolean GLX_Profiler_Enabled( const ProfilerState &state )
{
	return state.initialized && state.r_glxGpuTiming && state.r_glxGpuTiming->integer ? qtrue : qfalse;
}

static void GLX_Profiler_ClearQueryState( ProfilerState *state )
{
	state->queries[0] = 0;
	state->queries[1] = 0;
	state->queries[2] = 0;
	state->queries[3] = 0;
	for ( int i = 0; i < GLX_PASS_QUERY_COUNT; i++ ) {
		state->passQueries[i] = {};
	}
	for ( int i = 0; i < GLX_PASS_STACK_DEPTH; i++ ) {
		state->passStack[i] = {};
		state->passStack[i].pass = -1;
		state->passStack[i].slot = -1;
	}
	state->pending[0] = qfalse;
	state->pending[1] = qfalse;
	state->pending[2] = qfalse;
	state->pending[3] = qfalse;
	state->writeIndex = 0;
	state->passWriteIndex = 0;
	state->passStackDepth = 0;
	state->passTimerReady = qfalse;
	state->initialized = qfalse;
	state->queryActive = qfalse;
	state->lastGpuValid = qfalse;
	state->lastGpuMilliseconds = 0.0;
	std::snprintf( state->lastGpuText, sizeof( state->lastGpuText ), "%s", "n/a" );
}

static qboolean GLX_Profiler_PassTimingEnabled( const ProfilerState &state )
{
	return ( GLX_Profiler_Enabled( state ) && state.passTimerReady &&
		state.r_glxGpuPassTiming && state.r_glxGpuPassTiming->integer ) ? qtrue : qfalse;
}

static void GLX_Profiler_RecordPassMilliseconds( ProfilerState *state, int pass, double milliseconds )
{
	if ( !state || pass < 0 || pass >= GLX_GPU_PASS_COUNT || milliseconds < 0.0 ) {
		return;
	}

	GpuPassStats &stats = state->gpuPassStats[pass];
	stats.samples++;
	stats.lastMilliseconds = milliseconds;
	stats.totalMilliseconds += milliseconds;
	std::snprintf( stats.lastText, sizeof( stats.lastText ), "%.3fms", milliseconds );
}

static void GLX_Profiler_CollectResults( ProfilerState *state )
{
	qboolean sawUnavailable = qfalse;
	qboolean sawPassUnavailable = qfalse;

	if ( !state || !state->initialized || !state->fns.GetQueryObjectiv ) {
		return;
	}

	for ( int i = 0; i < GLX_QUERY_COUNT; i++ ) {
		if ( !state->pending[i] || !state->queries[i] ) {
			continue;
		}

		GLint available = 0;
		state->fns.GetQueryObjectiv( state->queries[i], GL_QUERY_RESULT_AVAILABLE, &available );
		if ( !available ) {
			sawUnavailable = qtrue;
			continue;
		}

		if ( state->fns.GetQueryObjectui64v ) {
			GLXQueryResult64 nanoseconds = 0;
			state->fns.GetQueryObjectui64v( state->queries[i], GL_QUERY_RESULT, &nanoseconds );
			state->lastGpuMilliseconds = static_cast<double>( nanoseconds ) / 1000000.0;
		} else if ( state->fns.GetQueryObjectuiv ) {
			GLuint nanoseconds = 0;
			state->fns.GetQueryObjectuiv( state->queries[i], GL_QUERY_RESULT, &nanoseconds );
			state->lastGpuMilliseconds = static_cast<double>( nanoseconds ) / 1000000.0;
		}

		state->lastGpuValid = qtrue;
		std::snprintf( state->lastGpuText, sizeof( state->lastGpuText ), "%.3fms", state->lastGpuMilliseconds );
		GLX_Profiler_RecordPassMilliseconds( state, GLX_GPU_PASS_BACKEND, state->lastGpuMilliseconds );
		state->pending[i] = qfalse;
	}

	for ( int i = 0; i < GLX_PASS_QUERY_COUNT; i++ ) {
		GpuPassQuery &query = state->passQueries[i];

		if ( !query.pending || !query.startQuery || !query.endQuery ) {
			continue;
		}

		GLint startAvailable = 0;
		GLint endAvailable = 0;
		state->fns.GetQueryObjectiv( query.startQuery, GL_QUERY_RESULT_AVAILABLE, &startAvailable );
		state->fns.GetQueryObjectiv( query.endQuery, GL_QUERY_RESULT_AVAILABLE, &endAvailable );
		if ( !startAvailable || !endAvailable ) {
			sawPassUnavailable = qtrue;
			continue;
		}

		if ( state->fns.GetQueryObjectui64v ) {
			GLXQueryResult64 startNanoseconds = 0;
			GLXQueryResult64 endNanoseconds = 0;
			state->fns.GetQueryObjectui64v( query.startQuery, GL_QUERY_RESULT, &startNanoseconds );
			state->fns.GetQueryObjectui64v( query.endQuery, GL_QUERY_RESULT, &endNanoseconds );
			if ( endNanoseconds >= startNanoseconds ) {
				GLX_Profiler_RecordPassMilliseconds( state, query.pass,
					static_cast<double>( endNanoseconds - startNanoseconds ) / 1000000.0 );
			}
		} else if ( state->fns.GetQueryObjectuiv ) {
			GLuint startNanoseconds = 0;
			GLuint endNanoseconds = 0;
			state->fns.GetQueryObjectuiv( query.startQuery, GL_QUERY_RESULT, &startNanoseconds );
			state->fns.GetQueryObjectuiv( query.endQuery, GL_QUERY_RESULT, &endNanoseconds );
			if ( endNanoseconds >= startNanoseconds ) {
				GLX_Profiler_RecordPassMilliseconds( state, query.pass,
					static_cast<double>( endNanoseconds - startNanoseconds ) / 1000000.0 );
			}
		}

		query.pending = qfalse;
	}

	if ( sawUnavailable ) {
		state->queryUnavailableFrames++;
	}
	if ( sawPassUnavailable ) {
		state->passQueryUnavailableFrames++;
	}
}

void GLX_Profiler_RegisterCvars( ProfilerState *state )
{
	if ( !state ) {
		return;
	}

	state->r_glxGpuTiming = RI().Cvar_Get( "r_glxGpuTiming", "1", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxGpuTiming, "Collect non-blocking GLx backend GPU timings with timer queries when supported." );
	state->r_glxGpuPassTiming = RI().Cvar_Get( "r_glxGpuPassTiming", "1", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxGpuPassTiming, "Collect non-blocking GLx pass timings with timestamp timer queries when supported." );

	if ( state->lastGpuText[0] == '\0' ) {
		std::snprintf( state->lastGpuText, sizeof( state->lastGpuText ), "%s", "n/a" );
	}
	for ( int i = 0; i < GLX_GPU_PASS_COUNT; i++ ) {
		if ( state->gpuPassStats[i].lastText[0] == '\0' ) {
			std::snprintf( state->gpuPassStats[i].lastText,
				sizeof( state->gpuPassStats[i].lastText ), "%s", "n/a" );
		}
	}
}

void GLX_Profiler_OnOpenGLReady( ProfilerState *state, const Capabilities &caps )
{
	if ( !state ) {
		return;
	}

	GLX_Profiler_Shutdown( state );

	if ( !caps.features.timerQuery || !RI().GL_GetProcAddress ) {
		return;
	}

	state->fns.GenQueries = reinterpret_cast<PFNGLXGENQUERIESPROC>( GLX_Profiler_GetProc( "glGenQueries", "glGenQueriesARB" ) );
	state->fns.DeleteQueries = reinterpret_cast<PFNGLXDELETEQUERIESPROC>( GLX_Profiler_GetProc( "glDeleteQueries", "glDeleteQueriesARB" ) );
	state->fns.BeginQuery = reinterpret_cast<PFNGLXBEGINQUERYPROC>( GLX_Profiler_GetProc( "glBeginQuery", "glBeginQueryARB" ) );
	state->fns.EndQuery = reinterpret_cast<PFNGLXENDQUERYPROC>( GLX_Profiler_GetProc( "glEndQuery", "glEndQueryARB" ) );
	state->fns.GetQueryObjectiv = reinterpret_cast<PFNGLXGETQUERYOBJECTIVPROC>( GLX_Profiler_GetProc( "glGetQueryObjectiv", "glGetQueryObjectivARB" ) );
	state->fns.GetQueryObjectuiv = reinterpret_cast<PFNGLXGETQUERYOBJECTUIVPROC>( GLX_Profiler_GetProc( "glGetQueryObjectuiv", "glGetQueryObjectuivARB" ) );
	state->fns.GetQueryObjectui64v = reinterpret_cast<PFNGLXGETQUERYOBJECTUI64VPROC>( GLX_Profiler_GetProc( "glGetQueryObjectui64v", "glGetQueryObjectui64vEXT" ) );
	state->fns.QueryCounter = reinterpret_cast<PFNGLXQUERYCOUNTERPROC>( GLX_Profiler_GetProc( "glQueryCounter", "glQueryCounterARB" ) );

	if ( !state->fns.GenQueries || !state->fns.DeleteQueries || !state->fns.BeginQuery ||
		!state->fns.EndQuery || !state->fns.GetQueryObjectiv ||
		( !state->fns.GetQueryObjectui64v && !state->fns.GetQueryObjectuiv ) ) {
		RI().Printf( PRINT_DEVELOPER, "GLx timer query advertised, but required query functions are unavailable.\n" );
		state->fns = {};
		return;
	}

	state->fns.GenQueries( GLX_QUERY_COUNT, state->queries );
	if ( !state->queries[0] ) {
		RI().Printf( PRINT_DEVELOPER, "GLx timer query allocation failed.\n" );
		GLX_Profiler_ClearQueryState( state );
		state->fns = {};
		return;
	}

	state->initialized = qtrue;

	if ( state->fns.QueryCounter ) {
		GLuint passQueryIds[GLX_PASS_QUERY_IDS] = {};

		state->fns.GenQueries( GLX_PASS_QUERY_IDS, passQueryIds );
		if ( passQueryIds[0] ) {
			for ( int i = 0; i < GLX_PASS_QUERY_COUNT; i++ ) {
				state->passQueries[i].startQuery = passQueryIds[i * 2];
				state->passQueries[i].endQuery = passQueryIds[i * 2 + 1];
				state->passQueries[i].pass = -1;
			}
			state->passTimerReady = qtrue;
		} else {
			RI().Printf( PRINT_DEVELOPER, "GLx pass timer query allocation failed.\n" );
		}
	}
}

void GLX_Profiler_Shutdown( ProfilerState *state )
{
	if ( !state ) {
		return;
	}

	if ( state->initialized && state->fns.DeleteQueries && state->queries[0] ) {
		state->fns.DeleteQueries( GLX_QUERY_COUNT, state->queries );
	}
	if ( state->initialized && state->fns.DeleteQueries && state->passQueries[0].startQuery ) {
		GLuint passQueryIds[GLX_PASS_QUERY_IDS] = {};

		for ( int i = 0; i < GLX_PASS_QUERY_COUNT; i++ ) {
			passQueryIds[i * 2] = state->passQueries[i].startQuery;
			passQueryIds[i * 2 + 1] = state->passQueries[i].endQuery;
		}
		state->fns.DeleteQueries( GLX_PASS_QUERY_IDS, passQueryIds );
	}

	const unsigned int frames = state->frames;
	const unsigned int backendQueries = state->backendQueries;
	const unsigned int gpuPassQueries = state->gpuPassQueries;
	const unsigned int unavailable = state->queryUnavailableFrames;
	const unsigned int ringFullSkips = state->queryRingFullSkips;
	const unsigned int passUnavailable = state->passQueryUnavailableFrames;
	const unsigned int passRingFullSkips = state->passQueryRingFullSkips;
	cvar_t *gpuTiming = state->r_glxGpuTiming;
	cvar_t *gpuPassTiming = state->r_glxGpuPassTiming;

	state->fns = {};
	GLX_Profiler_ClearQueryState( state );

	state->frames = frames;
	state->backendQueries = backendQueries;
	state->gpuPassQueries = gpuPassQueries;
	state->queryUnavailableFrames = unavailable;
	state->queryRingFullSkips = ringFullSkips;
	state->passQueryUnavailableFrames = passUnavailable;
	state->passQueryRingFullSkips = passRingFullSkips;
	state->r_glxGpuTiming = gpuTiming;
	state->r_glxGpuPassTiming = gpuPassTiming;
}

void GLX_Profiler_BeginBackendTimer( ProfilerState *state )
{
	if ( !state || !GLX_Profiler_Enabled( *state ) || state->queryActive ) {
		return;
	}

	// results normally drain once per frame in GLX_Profiler_FrameComplete;
	// poll the driver here only when this ring slot is still pending
	if ( state->pending[state->writeIndex] ) {
		GLX_Profiler_CollectResults( state );
	}

	if ( state->pending[state->writeIndex] ) {
		state->queryRingFullSkips++;
		return;
	}

	state->fns.BeginQuery( GL_TIME_ELAPSED, state->queries[state->writeIndex] );
	state->queryActive = qtrue;
}

void GLX_Profiler_EndBackendTimer( ProfilerState *state )
{
	if ( !state || !state->queryActive ) {
		return;
	}

	state->fns.EndQuery( GL_TIME_ELAPSED );
	state->pending[state->writeIndex] = qtrue;
	state->writeIndex = ( state->writeIndex + 1 ) % GLX_QUERY_COUNT;
	state->backendQueries++;
	state->queryActive = qfalse;
}

void GLX_Profiler_BeginGpuPassTimer( ProfilerState *state, int pass )
{
	GpuPassQuery *query;

	if ( !state || pass < 0 || pass >= GLX_GPU_PASS_COUNT ||
		!GLX_Profiler_PassTimingEnabled( *state ) ) {
		return;
	}

	// results normally drain once per frame in GLX_Profiler_FrameComplete;
	// poll the driver here only when the next ring slot is still pending
	if ( state->passQueries[state->passWriteIndex].pending ) {
		GLX_Profiler_CollectResults( state );
	}

	if ( state->passStackDepth >= GLX_PASS_STACK_DEPTH ) {
		state->passQueryRingFullSkips++;
		return;
	}
	query = &state->passQueries[state->passWriteIndex];
	if ( query->pending ) {
		state->passQueryRingFullSkips++;
		return;
	}

	query->pass = pass;
	state->fns.QueryCounter( query->startQuery, GL_TIMESTAMP );
	state->passStack[state->passStackDepth].pass = pass;
	state->passStack[state->passStackDepth].slot = state->passWriteIndex;
	state->passStackDepth++;
	state->passWriteIndex = ( state->passWriteIndex + 1 ) % GLX_PASS_QUERY_COUNT;
}

void GLX_Profiler_EndGpuPassTimer( ProfilerState *state, int pass )
{
	if ( !state || pass < 0 || pass >= GLX_GPU_PASS_COUNT ||
		!GLX_Profiler_PassTimingEnabled( *state ) || state->passStackDepth <= 0 ) {
		return;
	}

	int stackIndex = state->passStackDepth - 1;
	while ( stackIndex >= 0 && state->passStack[stackIndex].pass != pass ) {
		stackIndex--;
	}
	if ( stackIndex < 0 ) {
		return;
	}

	const int slot = state->passStack[stackIndex].slot;
	for ( int i = stackIndex; i < state->passStackDepth - 1; i++ ) {
		state->passStack[i] = state->passStack[i + 1];
	}
	state->passStackDepth--;
	state->passStack[state->passStackDepth].pass = -1;
	state->passStack[state->passStackDepth].slot = -1;

	if ( slot < 0 || slot >= GLX_PASS_QUERY_COUNT || state->passQueries[slot].pending ) {
		return;
	}

	state->fns.QueryCounter( state->passQueries[slot].endQuery, GL_TIMESTAMP );
	state->passQueries[slot].pending = qtrue;
	state->gpuPassQueries++;
}

void GLX_Profiler_FrameComplete( ProfilerState *state )
{
	if ( !state ) {
		return;
	}

	state->frames++;
	GLX_Profiler_CollectResults( state );
}

void GLX_Profiler_RecordPostBlit( ProfilerState *state )
{
	if ( state ) {
		state->postBlits++;
	}
}

void GLX_Profiler_RecordPostBind( ProfilerState *state )
{
	if ( state ) {
		state->postBinds++;
	}
}

void GLX_Profiler_RecordPostClear( ProfilerState *state )
{
	if ( state ) {
		state->postClears++;
	}
}

void GLX_Profiler_RecordFullscreenPass( ProfilerState *state )
{
	if ( state ) {
		state->postFullscreenPasses++;
	}
}

void GLX_Profiler_RecordDraw( ProfilerState *state, int indexes, int path )
{
	if ( !state || indexes <= 0 ) {
		return;
	}

	state->drawCalls++;
	state->drawIndexes += static_cast<unsigned int>( indexes );

	switch ( path ) {
	case GLX_DRAW_GENERIC:
		state->genericDrawCalls++;
		state->genericDrawIndexes += static_cast<unsigned int>( indexes );
		break;
	case GLX_DRAW_VBO_DEVICE:
		state->vboDeviceDrawCalls++;
		state->vboDeviceDrawIndexes += static_cast<unsigned int>( indexes );
		break;
	case GLX_DRAW_VBO_SOFT:
		state->vboSoftDrawCalls++;
		state->vboSoftDrawIndexes += static_cast<unsigned int>( indexes );
		break;
	case GLX_DRAW_DEBUG:
		state->debugDrawCalls++;
		state->debugDrawIndexes += static_cast<unsigned int>( indexes );
		break;
	case GLX_DRAW_STREAM_GENERIC:
		state->streamGenericDrawCalls++;
		state->streamGenericDrawIndexes += static_cast<unsigned int>( indexes );
		break;
	default:
		break;
	}
}

void GLX_Profiler_RecordLegacyDelegation( ProfilerState *state, int reason, int items )
{
	if ( !state || items <= 0 ) {
		return;
	}

	state->legacyDelegationCalls++;
	state->legacyDelegationItems += static_cast<unsigned int>( items );

	if ( reason >= 0 && reason < GLX_LEGACY_DELEGATION_REASON_COUNT ) {
		state->legacyDelegationReasonCalls[reason]++;
		state->legacyDelegationReasonItems[reason] += static_cast<unsigned int>( items );
	}
}

void GLX_Profiler_RecordShaderBatch( ProfilerState *state, const char *shaderName, int sort,
	int numPasses, int numVertexes, int numIndexes, int flags )
{
	int passBucket;

	if ( !state || numVertexes <= 0 || numIndexes <= 0 ) {
		return;
	}

	state->shaderBatches++;
	state->shaderBatchIndexes += static_cast<unsigned int>( numIndexes );
	state->shaderBatchVertexes += static_cast<unsigned int>( numVertexes );
	if ( numPasses > 0 ) {
		state->shaderStagePasses += static_cast<unsigned int>( numPasses );
	}

	if ( flags & GLX_BATCH_VBO ) {
		state->vboShaderBatches++;
	} else {
		state->genericShaderBatches++;
	}
	if ( flags & GLX_BATCH_FOG ) {
		state->fogShaderBatches++;
	}
	if ( flags & GLX_BATCH_MULTITEXTURE ) {
		state->multitextureShaderBatches++;
	}
	if ( flags & GLX_BATCH_POLYGON_OFFSET ) {
		state->polygonOffsetShaderBatches++;
	}

	passBucket = numPasses;
	if ( passBucket < 0 ) {
		passBucket = 0;
	}
	if ( passBucket >= GLX_PASS_HISTOGRAM_BUCKETS ) {
		passBucket = GLX_PASS_HISTOGRAM_BUCKETS - 1;
	}
	state->passHistogram[passBucket]++;

	if ( sort >= 0 && sort < GLX_SORT_HISTOGRAM_BUCKETS ) {
		state->sortHistogram[sort]++;
	} else {
		state->sortOverflowBatches++;
	}

	if ( static_cast<unsigned int>( numIndexes ) > state->largestShaderBatchIndexes ) {
		state->largestShaderBatchIndexes = static_cast<unsigned int>( numIndexes );
		state->largestShaderBatchVertexes = static_cast<unsigned int>( numVertexes );
		GLX_Profiler_CopyName( state->largestShaderBatchName, sizeof( state->largestShaderBatchName ), shaderName );
	}

	GLX_Profiler_RecordHotShader( state, shaderName && *shaderName ? shaderName : "<unnamed>", numVertexes, numIndexes );
}

void GLX_Profiler_RecordMaterialStage( ProfilerState *state, int path, int flags, unsigned int stateBits,
	int rgbGen, int alphaGen, int tcGen0, int tcGen1, int texMods0, int texMods1,
	int numVertexes, int numIndexes )
{
	const bool hasBundle1 = ( flags & ( GLX_STAGE_MULTITEXTURE | GLX_STAGE_ST1 ) ) != 0;
	const int keyTcGen1 = hasBundle1 ? tcGen1 : -1;
	const int keyTexMods1 = hasBundle1 ? texMods1 : 0;
	const int texModCount = texMods0 + keyTexMods1;

	if ( !state || numVertexes <= 0 || numIndexes <= 0 ) {
		return;
	}

	state->materialStages++;
	state->materialStageIndexes += static_cast<unsigned int>( numIndexes );
	state->materialStageVertexes += static_cast<unsigned int>( numVertexes );

	switch ( path ) {
	case GLX_STAGE_PATH_VBO:
		state->vboMaterialStages++;
		break;
	case GLX_STAGE_PATH_GENERIC:
	default:
		state->genericMaterialStages++;
		break;
	}

	if ( flags & GLX_STAGE_MULTITEXTURE ) {
		state->multitextureMaterialStages++;
	}
	if ( flags & GLX_STAGE_DEPTH_FRAGMENT ) {
		state->depthFragmentMaterialStages++;
	}
	if ( flags & GLX_STAGE_BLEND ) {
		state->blendMaterialStages++;
	}
	if ( flags & GLX_STAGE_ALPHA_TEST ) {
		state->alphaTestMaterialStages++;
	}
	if ( flags & GLX_STAGE_DEPTH_WRITE ) {
		state->depthWriteMaterialStages++;
	}
	if ( flags & GLX_STAGE_LIGHTMAP ) {
		state->lightmapMaterialStages++;
	}
	if ( flags & GLX_STAGE_ANIMATED_IMAGE ) {
		state->animatedImageMaterialStages++;
	}
	if ( flags & GLX_STAGE_VIDEO_MAP ) {
		state->videoMapMaterialStages++;
	}
	if ( flags & GLX_STAGE_SCREEN_MAP ) {
		state->screenMapMaterialStages++;
	}
	if ( flags & GLX_STAGE_DLIGHT_MAP ) {
		state->dlightMapMaterialStages++;
	}
	if ( flags & GLX_STAGE_TEXMOD ) {
		state->texmodMaterialStages++;
	}
	if ( flags & GLX_STAGE_ENVIRONMENT ) {
		state->environmentMaterialStages++;
	}
	if ( flags & GLX_STAGE_ST0 ) {
		state->st0MaterialStages++;
	}
	if ( flags & GLX_STAGE_ST1 ) {
		state->st1MaterialStages++;
	}

	state->rgbGenHistogram[GLX_Profiler_ClampedBucket( rgbGen, GLX_STAGE_GEN_HISTOGRAM_BUCKETS )]++;
	state->alphaGenHistogram[GLX_Profiler_ClampedBucket( alphaGen, GLX_STAGE_GEN_HISTOGRAM_BUCKETS )]++;
	state->tcGenHistogram[GLX_Profiler_ClampedBucket( tcGen0, GLX_STAGE_GEN_HISTOGRAM_BUCKETS )]++;
	if ( hasBundle1 ) {
		state->tcGenHistogram[GLX_Profiler_ClampedBucket( tcGen1, GLX_STAGE_GEN_HISTOGRAM_BUCKETS )]++;
	}
	state->texModHistogram[GLX_Profiler_ClampedBucket( texModCount, GLX_STAGE_TEXMOD_HISTOGRAM_BUCKETS )]++;

	GLX_Profiler_RecordMaterialKey( state, path, flags, stateBits, rgbGen, alphaGen,
		tcGen0, keyTcGen1, texMods0, keyTexMods1, numVertexes, numIndexes );
}

void GLX_Profiler_PrintInfo( const ProfilerState &state )
{
	int printed;

	RI().Printf( PRINT_ALL, "  frames observed: %u\n", state.frames );
	RI().Printf( PRINT_ALL, "  backend timer queries: %u\n", state.backendQueries );
	RI().Printf( PRINT_ALL, "  last backend GPU time: %s\n", GLX_Profiler_LastGpuTimeText( state ) );
	RI().Printf( PRINT_ALL, "  timer query unavailable frames: %u\n", state.queryUnavailableFrames );
	RI().Printf( PRINT_ALL, "  timer query ring-full skips: %u\n", state.queryRingFullSkips );
	RI().Printf( PRINT_ALL,
		"  post pass counters: blits %u, binds %u, clears %u, fullscreen passes %u\n",
		state.postBlits, state.postBinds, state.postClears, state.postFullscreenPasses );
	RI().Printf( PRINT_ALL,
		"  pass timer queries: active %s, queries %u, unavailable frames %u, ring-full skips %u\n",
		BoolName( state.passTimerReady ), state.gpuPassQueries,
		state.passQueryUnavailableFrames, state.passQueryRingFullSkips );
	RI().Printf( PRINT_ALL, "  pass GPU timings:" );
	for ( int i = 0; i < GLX_GPU_PASS_COUNT; i++ ) {
		const GpuPassStats &stats = state.gpuPassStats[i];
		const char *text = stats.samples ? stats.lastText : "n/a";
		RI().Printf( PRINT_ALL, " %s=%s/%u",
			GLX_Profiler_GpuPassName( i ), text, stats.samples );
	}
	RI().Printf( PRINT_ALL, "\n" );
	RI().Printf( PRINT_ALL, "  draw calls: %u, indexes: %u\n", state.drawCalls, state.drawIndexes );
	RI().Printf( PRINT_ALL, "  draw calls generic: %u calls, %u indexes\n", state.genericDrawCalls, state.genericDrawIndexes );
	RI().Printf( PRINT_ALL, "  draw calls vbo-device: %u calls, %u indexes\n", state.vboDeviceDrawCalls, state.vboDeviceDrawIndexes );
	RI().Printf( PRINT_ALL, "  draw calls vbo-soft: %u calls, %u indexes\n", state.vboSoftDrawCalls, state.vboSoftDrawIndexes );
	RI().Printf( PRINT_ALL, "  draw calls debug: %u calls, %u indexes\n", state.debugDrawCalls, state.debugDrawIndexes );
	RI().Printf( PRINT_ALL, "  draw calls stream-generic: %u calls, %u indexes\n",
		state.streamGenericDrawCalls, state.streamGenericDrawIndexes );
	RI().Printf( PRINT_ALL, "  ownership legacy delegation: %u calls, %u items\n",
		state.legacyDelegationCalls, state.legacyDelegationItems );
	for ( int i = 0; i < GLX_LEGACY_DELEGATION_REASON_COUNT; i++ ) {
		if ( state.legacyDelegationReasonCalls[i] ) {
			RI().Printf( PRINT_ALL, "    %s: %u calls, %u items\n",
				GLX_Profiler_LegacyDelegationName( i ),
				state.legacyDelegationReasonCalls[i],
				state.legacyDelegationReasonItems[i] );
		}
	}
	RI().Printf( PRINT_ALL, "  shader batches: %u, verts %u, indexes %u, stage passes %u\n",
		state.shaderBatches, state.shaderBatchVertexes, state.shaderBatchIndexes, state.shaderStagePasses );
	RI().Printf( PRINT_ALL, "  shader batches generic/vbo: %u/%u\n",
		state.genericShaderBatches, state.vboShaderBatches );
	RI().Printf( PRINT_ALL, "  shader batches fog/multitexture/polygonoffset: %u/%u/%u\n",
		state.fogShaderBatches, state.multitextureShaderBatches, state.polygonOffsetShaderBatches );
	RI().Printf( PRINT_ALL, "  largest shader batch: %s, %u verts, %u indexes\n",
		state.largestShaderBatchName[0] ? state.largestShaderBatchName : "n/a",
		state.largestShaderBatchVertexes, state.largestShaderBatchIndexes );
	RI().Printf( PRINT_ALL, "  shader pass histogram: p0=%u p1=%u p2=%u p3=%u p4=%u p5=%u p6=%u p7=%u p8+=%u\n",
		state.passHistogram[0], state.passHistogram[1], state.passHistogram[2],
		state.passHistogram[3], state.passHistogram[4], state.passHistogram[5],
		state.passHistogram[6], state.passHistogram[7], state.passHistogram[8] );

	printed = 0;
	if ( state.sortOverflowBatches ) {
		printed = 1;
	}
	for ( int i = 0; i < GLX_SORT_HISTOGRAM_BUCKETS; i++ ) {
		if ( state.sortHistogram[i] ) {
			printed = 1;
		}
	}
	if ( printed ) {
		RI().Printf( PRINT_ALL, "  shader sort histogram:" );
		for ( int i = 0; i < GLX_SORT_HISTOGRAM_BUCKETS; i++ ) {
			if ( state.sortHistogram[i] ) {
				RI().Printf( PRINT_ALL, " %i=%u", i, state.sortHistogram[i] );
			}
		}
		if ( state.sortOverflowBatches ) {
			RI().Printf( PRINT_ALL, " other=%u", state.sortOverflowBatches );
		}
		RI().Printf( PRINT_ALL, "\n" );
	}

	printed = 0;
	for ( int i = 0; i < GLX_MAX_HOT_SHADERS; i++ ) {
		if ( state.hotShaders[i].batches ) {
			if ( printed == 0 ) {
				RI().Printf( PRINT_ALL, "  hot shaders by index pressure:\n" );
			}
			printed++;
			RI().Printf( PRINT_ALL, "    #%i: %s, %u batches, %u verts, %u indexes\n",
				printed, state.hotShaders[i].name, state.hotShaders[i].batches,
				state.hotShaders[i].vertexes, state.hotShaders[i].indexes );
		}
	}

	RI().Printf( PRINT_ALL, "  material stages: %u, verts %u, indexes %u\n",
		state.materialStages, state.materialStageVertexes, state.materialStageIndexes );
	RI().Printf( PRINT_ALL, "  material stages generic/vbo: %u/%u\n",
		state.genericMaterialStages, state.vboMaterialStages );
	RI().Printf( PRINT_ALL, "  material flags mt/depthfrag/blend/atest/depthwrite/lightmap: %u/%u/%u/%u/%u/%u\n",
		state.multitextureMaterialStages, state.depthFragmentMaterialStages,
		state.blendMaterialStages, state.alphaTestMaterialStages,
		state.depthWriteMaterialStages, state.lightmapMaterialStages );
	RI().Printf( PRINT_ALL, "  material flags anim/video/screen/dlight/texmod/env/st0/st1: %u/%u/%u/%u/%u/%u/%u/%u\n",
		state.animatedImageMaterialStages, state.videoMapMaterialStages,
		state.screenMapMaterialStages, state.dlightMapMaterialStages,
		state.texmodMaterialStages, state.environmentMaterialStages,
		state.st0MaterialStages, state.st1MaterialStages );
	RI().Printf( PRINT_ALL, "  material texmod histogram: t0=%u t1=%u t2=%u t3=%u t4=%u t5=%u t6=%u t7=%u t8+=%u\n",
		state.texModHistogram[0], state.texModHistogram[1], state.texModHistogram[2],
		state.texModHistogram[3], state.texModHistogram[4], state.texModHistogram[5],
		state.texModHistogram[6], state.texModHistogram[7], state.texModHistogram[8] );

	printed = 0;
	for ( int i = 0; i < GLX_STAGE_GEN_HISTOGRAM_BUCKETS; i++ ) {
		if ( state.rgbGenHistogram[i] || state.alphaGenHistogram[i] || state.tcGenHistogram[i] ) {
			if ( printed == 0 ) {
				RI().Printf( PRINT_ALL, "  material gen histograms:" );
			}
			printed = 1;
			RI().Printf( PRINT_ALL, " %i=rgb%u/a%u/tc%u",
				i, state.rgbGenHistogram[i], state.alphaGenHistogram[i], state.tcGenHistogram[i] );
		}
	}
	if ( printed ) {
		RI().Printf( PRINT_ALL, "\n" );
	}

	printed = 0;
	for ( int i = 0; i < GLX_MAX_HOT_MATERIAL_KEYS; i++ ) {
		const MaterialKeyStats &key = state.hotMaterialKeys[i];

		if ( key.stages ) {
			if ( printed == 0 ) {
				RI().Printf( PRINT_ALL, "  hot material keys by index pressure:\n" );
			}
			printed++;
			RI().Printf( PRINT_ALL,
				"    #%i: %s state 0x%08x flags 0x%04x rgb %i alpha %i tc %i/%i texmods %i/%i, %u stages, %u verts, %u indexes\n",
				printed, GLX_Profiler_StagePathName( key.path ), key.stateBits, key.flags,
				key.rgbGen, key.alphaGen, key.tcGen0, key.tcGen1, key.texMods0, key.texMods1,
				key.stages, key.vertexes, key.indexes );
		}
	}
}

qboolean GLX_Profiler_TimerReady( const ProfilerState &state )
{
	return state.initialized;
}

const char *GLX_Profiler_LastGpuTimeText( const ProfilerState &state )
{
	if ( !state.initialized ) {
		return "unavailable";
	}

	if ( !state.r_glxGpuTiming || !state.r_glxGpuTiming->integer ) {
		return "disabled";
	}

	if ( !state.lastGpuValid ) {
		return "pending";
	}

	return state.lastGpuText;
}

} // namespace glx
