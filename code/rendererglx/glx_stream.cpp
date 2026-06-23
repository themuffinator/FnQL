#include "glx_stream.h"
#include "glx_material_key.h"
#include "glx_stream_logic.h"

#include <cstdio>
#include <cstring>

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_ARRAY_BUFFER_BINDING
#define GL_ARRAY_BUFFER_BINDING 0x8894
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER_BINDING
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT 0x0002
#endif
#ifndef GL_MAP_INVALIDATE_BUFFER_BIT
#define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008
#endif
#ifndef GL_MAP_INVALIDATE_RANGE_BIT
#define GL_MAP_INVALIDATE_RANGE_BIT 0x0004
#endif
#ifndef GL_MAP_FLUSH_EXPLICIT_BIT
#define GL_MAP_FLUSH_EXPLICIT_BIT 0x0010
#endif
#ifndef GL_MAP_UNSYNCHRONIZED_BIT
#define GL_MAP_UNSYNCHRONIZED_BIT 0x0020
#endif
#ifndef GL_MAP_PERSISTENT_BIT
#define GL_MAP_PERSISTENT_BIT 0x0040
#endif
#ifndef GL_MAP_COHERENT_BIT
#define GL_MAP_COHERENT_BIT 0x0080
#endif
#ifndef GL_DYNAMIC_STORAGE_BIT
#define GL_DYNAMIC_STORAGE_BIT 0x0100
#endif
#ifndef GL_NO_ERROR
#define GL_NO_ERROR 0
#endif
#ifndef GL_OUT_OF_MEMORY
#define GL_OUT_OF_MEMORY 0x0505
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif
#ifndef GL_ALREADY_SIGNALED
#define GL_ALREADY_SIGNALED 0x911A
#endif
#ifndef GL_TIMEOUT_EXPIRED
#define GL_TIMEOUT_EXPIRED 0x911B
#endif
#ifndef GL_CONDITION_SATISFIED
#define GL_CONDITION_SATISFIED 0x911C
#endif
#ifndef GL_WAIT_FAILED
#define GL_WAIT_FAILED 0x911D
#endif
#ifndef GL_SYNC_FLUSH_COMMANDS_BIT
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001
#endif

namespace glx {

typedef void ( APIENTRY *PFNGLXGENBUFFERSPROC )( GLsizei n, GLuint *buffers );
typedef void ( APIENTRY *PFNGLXDELETEBUFFERSPROC )( GLsizei n, const GLuint *buffers );
typedef void ( APIENTRY *PFNGLXBINDBUFFERPROC )( GLenum target, GLuint buffer );
typedef void ( APIENTRY *PFNGLXBUFFERDATAPROC )( GLenum target, ptrdiff_t size, const void *data, GLenum usage );
typedef void ( APIENTRY *PFNGLXBUFFERSUBDATAPROC )( GLenum target, ptrdiff_t offset, ptrdiff_t size, const void *data );
typedef void ( APIENTRY *PFNGLXBUFFERSTORAGEPROC )( GLenum target, ptrdiff_t size, const void *data, GLbitfield flags );
typedef void *( APIENTRY *PFNGLXMAPBUFFERRANGEPROC )( GLenum target, ptrdiff_t offset, ptrdiff_t length, GLbitfield access );
typedef GLboolean ( APIENTRY *PFNGLXUNMAPBUFFERPROC )( GLenum target );
typedef void ( APIENTRY *PFNGLXGETINTEGERVPROC )( GLenum pname, GLint *params );
typedef GLenum ( APIENTRY *PFNGLXGETERRORPROC )( void );
typedef void *( APIENTRY *PFNGLXFENCESYNCPROC )( GLenum condition, GLbitfield flags );
typedef GLenum ( APIENTRY *PFNGLXCLIENTWAITSYNCPROC )( void *sync, GLbitfield flags, unsigned long long timeout );
typedef void ( APIENTRY *PFNGLXDELETESYNCPROC )( void *sync );

struct StreamFns {
	PFNGLXGENBUFFERSPROC GenBuffers;
	PFNGLXDELETEBUFFERSPROC DeleteBuffers;
	PFNGLXBINDBUFFERPROC BindBuffer;
	PFNGLXBUFFERDATAPROC BufferData;
	PFNGLXBUFFERSUBDATAPROC BufferSubData;
	PFNGLXBUFFERSTORAGEPROC BufferStorage;
	PFNGLXMAPBUFFERRANGEPROC MapBufferRange;
	PFNGLXUNMAPBUFFERPROC UnmapBuffer;
	PFNGLXGETINTEGERVPROC GetIntegerv;
	PFNGLXGETERRORPROC GetError;
	PFNGLXFENCESYNCPROC FenceSync;
	PFNGLXCLIENTWAITSYNCPROC ClientWaitSync;
	PFNGLXDELETESYNCPROC DeleteSync;
};

static StreamFns s_fns {};

static void *GLX_Stream_GetProc( const char *name, const char *fallbackName = nullptr )
{
	void *proc = RI().GL_GetProcAddress ? RI().GL_GetProcAddress( name ) : nullptr;

	if ( !proc && fallbackName ) {
		proc = RI().GL_GetProcAddress( fallbackName );
	}

	return proc;
}

static size_t GLX_Stream_AlignOffset( size_t offset, size_t alignment )
{
	if ( alignment <= 1 ) {
		return offset;
	}

	const size_t remainder = offset % alignment;
	if ( remainder == 0 ) {
		return offset;
	}

	return offset + alignment - remainder;
}

static int GLX_Stream_DrawKeyMode( const StreamState &state )
{
	int mode = state.r_glxStreamDrawKeyMode ? state.r_glxStreamDrawKeyMode->integer : 0;

	if ( mode < 0 ) {
		mode = 0;
	}
	if ( mode > 2 ) {
		mode = 2;
	}

	return mode;
}

static const char *GLX_Stream_DrawKeyModeName( int mode )
{
	switch ( mode ) {
	case 0:
		return "plain";
	case 1:
		return "computed";
	case 2:
		return "broad-single-texture";
	default:
		return "unknown";
	}
}

static void GLX_Stream_SetReason( StreamState *state, const char *reason )
{
	std::snprintf( state->reason, sizeof( state->reason ), "%s", reason ? reason : "" );
}

static void GLX_Stream_ResetRuntime( StreamState *state )
{
	if ( !state ) {
		return;
	}

	state->ringBytes = 0;
	state->writeOffset = 0;
	state->mappedPtr = nullptr;
	state->frameSync = nullptr;
	state->buffer = 0;
	state->arrayBufferBinding = 0;
	state->elementArrayBufferBinding = 0;
	state->ready = qfalse;
	state->persistentMapped = qfalse;
	state->syncReady = qfalse;
	state->frameTouched = qfalse;
	state->arrayBufferBindingKnown = qfalse;
	state->elementArrayBufferBindingKnown = qfalse;
}

static void GLX_Stream_ResetCounters( StreamState *state )
{
	if ( !state ) {
		return;
	}

	state->fallbackCount = 0;
	state->allocationFailures = 0;
	state->mapFailures = 0;
	state->unmapFailures = 0;
	state->reserveFailures = 0;
	state->uploadFailures = 0;
	state->reservations = 0;
	state->commits = 0;
	state->uploadCalls = 0;
	state->wraps = 0;
	state->sameFrameWrapRejects = 0;
	state->orphans = 0;
	state->syncInsertions = 0;
	state->syncWaits = 0;
	state->syncTimeouts = 0;
	state->syncFailures = 0;
	state->syncFenceSkips = 0;
	state->selfTests = 0;
	state->arrayBufferBindingQueries = 0;
	state->arrayBufferBindingCacheHits = 0;
	state->arrayBufferBindingRestores = 0;
	state->arrayBufferBindingInvalidations = 0;
	state->bufferBindingExternalUpdates = 0;
	state->shadowTessUploads = 0;
	state->shadowTessSkips = 0;
	state->shadowTessFailures = 0;
	state->streamedDrawAttempts = 0;
	state->streamedDraws = 0;
	state->streamedDrawFallbacks = 0;
	state->streamedDrawSkips = 0;
	std::memset( state->streamedDrawSkipReasons, 0, sizeof( state->streamedDrawSkipReasons ) );
	state->streamedDrawMaterialAccepted = 0;
	state->streamedDrawMaterialRejected = 0;
	state->streamedDrawMaterialCompilerRejected = 0;
	state->streamedDrawMaterialCompilerLastUnsupportedReasons = GLX_MATERIAL_UNSUPPORTED_NONE;
	state->streamedDrawMultitextureAccepted = 0;
	state->streamedDrawMultitextureRejected = 0;
	state->streamedDrawMultitextureDraws = 0;
	state->streamedDrawFogDraws = 0;
	state->streamedDrawDepthFragmentAccepted = 0;
	state->streamedDrawDepthFragmentRejected = 0;
	state->streamedDrawDepthFragmentDraws = 0;
	state->streamedDrawTexModDraws = 0;
	state->streamedDrawTexModAccepted = 0;
	state->streamedDrawTexModRejected = 0;
	state->streamedDrawEnvironmentDraws = 0;
	state->streamedDrawEnvironmentAccepted = 0;
	state->streamedDrawEnvironmentRejected = 0;
	state->streamedDrawDynamicLightDraws = 0;
	state->streamedDrawDynamicLightAttempts = 0;
	state->streamedDrawDynamicLightFallbacks = 0;
	state->streamedDrawDynamicLightAccepted = 0;
	state->streamedDrawDynamicLightRejected = 0;
	state->streamedDrawDynamicLightReserveWraps = 0;
	state->streamedDrawDynamicLightSameFrameWrapRejects = 0;
	state->streamedDrawDynamicLightSyncWaits = 0;
	state->streamedDrawDynamicLightSyncTimeouts = 0;
	state->streamedDrawDynamicLightSyncFailures = 0;
	state->streamedDrawScreenMapDraws = 0;
	state->streamedDrawScreenMapAccepted = 0;
	state->streamedDrawScreenMapRejected = 0;
	state->streamedDrawVideoMapDraws = 0;
	state->streamedDrawVideoMapAccepted = 0;
	state->streamedDrawVideoMapRejected = 0;
	state->streamedDrawShadowDraws = 0;
	state->streamedDrawBeamDraws = 0;
	state->streamedDrawPostProcessDraws = 0;
	std::memset( state->streamedDrawCategoryAttempts, 0, sizeof( state->streamedDrawCategoryAttempts ) );
	std::memset( state->streamedDrawCategoryDraws, 0, sizeof( state->streamedDrawCategoryDraws ) );
	std::memset( state->streamedDrawCategoryFallbacks, 0, sizeof( state->streamedDrawCategoryFallbacks ) );
	std::memset( state->streamedDrawRoleAttempts, 0, sizeof( state->streamedDrawRoleAttempts ) );
	std::memset( state->streamedDrawRoleDraws, 0, sizeof( state->streamedDrawRoleDraws ) );
	std::memset( state->streamedDrawRoleFallbacks, 0, sizeof( state->streamedDrawRoleFallbacks ) );
	state->streamedDrawVertexes = 0;
	state->streamedDrawIndexes = 0;
	state->largestReservationBytes = 0;
	state->lastReservationBytes = 0;
	state->lastReservationOffset = 0;
	state->lastReservationStrategy = StreamStrategy::OrphanSubData;
	state->uploadBytes = 0;
	state->shadowTessBytes = 0;
	state->streamedDrawBytes = 0;
	state->streamedDrawIndexBytes = 0;
	state->streamedDrawTexcoord1Bytes = 0;
	state->streamedDrawDynamicLightAttemptBytes = 0;
	state->streamedDrawDynamicLightBytes = 0;
	state->streamedDrawDynamicLightIndexBytes = 0;
	state->streamedDrawDynamicLightTexcoord1Bytes = 0;
	state->frames = 0;
}

static int GLX_Stream_CvarModificationCount( const cvar_t *cvar )
{
	return cvar ? cvar->modificationCount : 0;
}

static int GLX_Stream_RequestedMegabytes( const StreamState *state )
{
	int megabytes = state && state->r_glxStreamMegabytes ? state->r_glxStreamMegabytes->integer : 8;

	if ( megabytes < 1 ) {
		megabytes = 1;
	}
	if ( megabytes > 128 ) {
		megabytes = 128;
	}
	return megabytes;
}

static void GLX_Stream_RecordRuntimeCvarCounts( StreamState *state )
{
	if ( !state ) {
		return;
	}

	state->lastStreamModeModificationCount =
		GLX_Stream_CvarModificationCount( state->r_glxStreamMode );
	state->lastStreamMegabytesModificationCount =
		GLX_Stream_CvarModificationCount( state->r_glxStreamMegabytes );
}

static qboolean GLX_Stream_RuntimeCvarsChanged( const StreamState *state )
{
	if ( !state ) {
		return qfalse;
	}

	return ( state->lastStreamModeModificationCount !=
		GLX_Stream_CvarModificationCount( state->r_glxStreamMode ) ||
		state->lastStreamMegabytesModificationCount !=
		GLX_Stream_CvarModificationCount( state->r_glxStreamMegabytes ) ) ? qtrue : qfalse;
}

static qboolean GLX_Stream_FunctionsReady()
{
	if ( !RI().GL_GetProcAddress ) {
		return qfalse;
	}

	s_fns.GenBuffers = reinterpret_cast<PFNGLXGENBUFFERSPROC>( GLX_Stream_GetProc( "glGenBuffers", "glGenBuffersARB" ) );
	s_fns.DeleteBuffers = reinterpret_cast<PFNGLXDELETEBUFFERSPROC>( GLX_Stream_GetProc( "glDeleteBuffers", "glDeleteBuffersARB" ) );
	s_fns.BindBuffer = reinterpret_cast<PFNGLXBINDBUFFERPROC>( GLX_Stream_GetProc( "glBindBuffer", "glBindBufferARB" ) );
	s_fns.BufferData = reinterpret_cast<PFNGLXBUFFERDATAPROC>( GLX_Stream_GetProc( "glBufferData", "glBufferDataARB" ) );
	s_fns.BufferSubData = reinterpret_cast<PFNGLXBUFFERSUBDATAPROC>( GLX_Stream_GetProc( "glBufferSubData", "glBufferSubDataARB" ) );
	s_fns.BufferStorage = reinterpret_cast<PFNGLXBUFFERSTORAGEPROC>( GLX_Stream_GetProc( "glBufferStorage" ) );
	s_fns.MapBufferRange = reinterpret_cast<PFNGLXMAPBUFFERRANGEPROC>( GLX_Stream_GetProc( "glMapBufferRange", "glMapBufferRangeEXT" ) );
	s_fns.UnmapBuffer = reinterpret_cast<PFNGLXUNMAPBUFFERPROC>( GLX_Stream_GetProc( "glUnmapBuffer", "glUnmapBufferARB" ) );
	s_fns.GetIntegerv = reinterpret_cast<PFNGLXGETINTEGERVPROC>( GLX_Stream_GetProc( "glGetIntegerv" ) );
	s_fns.GetError = reinterpret_cast<PFNGLXGETERRORPROC>( GLX_Stream_GetProc( "glGetError" ) );

	return s_fns.GenBuffers && s_fns.DeleteBuffers && s_fns.BindBuffer && s_fns.BufferData ? qtrue : qfalse;
}

static qboolean GLX_Stream_SyncFunctionsReady()
{
	if ( !RI().GL_GetProcAddress ) {
		return qfalse;
	}

	s_fns.FenceSync = reinterpret_cast<PFNGLXFENCESYNCPROC>( GLX_Stream_GetProc( "glFenceSync" ) );
	s_fns.ClientWaitSync = reinterpret_cast<PFNGLXCLIENTWAITSYNCPROC>( GLX_Stream_GetProc( "glClientWaitSync" ) );
	s_fns.DeleteSync = reinterpret_cast<PFNGLXDELETESYNCPROC>( GLX_Stream_GetProc( "glDeleteSync" ) );

	return s_fns.FenceSync && s_fns.ClientWaitSync && s_fns.DeleteSync ? qtrue : qfalse;
}

static void GLX_Stream_ClearGLErrors()
{
	if ( !s_fns.GetError ) {
		return;
	}

	for ( int i = 0; i < 8 && s_fns.GetError() != GL_NO_ERROR; i++ ) {
	}
}

static GLenum GLX_Stream_GetGLError()
{
	return s_fns.GetError ? s_fns.GetError() : GL_NO_ERROR;
}

void GLX_Stream_InvalidateArrayBufferCache( StreamState *state )
{
	if ( !state ) {
		return;
	}

	state->arrayBufferBinding = 0;
	state->arrayBufferBindingKnown = qfalse;
	state->elementArrayBufferBinding = 0;
	state->elementArrayBufferBindingKnown = qfalse;
	state->arrayBufferBindingInvalidations++;
}

void GLX_Stream_RecordExternalBufferBind( StreamState *state, unsigned int target, GLuint buffer )
{
	if ( !state ) {
		return;
	}

	if ( target == GL_ARRAY_BUFFER ) {
		state->arrayBufferBinding = buffer;
		state->arrayBufferBindingKnown = qtrue;
		state->bufferBindingExternalUpdates++;
	} else if ( target == GL_ELEMENT_ARRAY_BUFFER ) {
		state->elementArrayBufferBinding = buffer;
		state->elementArrayBufferBindingKnown = qtrue;
		state->bufferBindingExternalUpdates++;
	}
}

static GLuint GLX_Stream_CurrentArrayBufferBinding( StreamState *state )
{
	GLint current = 0;

	if ( !state ) {
		return 0;
	}

	if ( state->arrayBufferBindingKnown ) {
		state->arrayBufferBindingCacheHits++;
		return state->arrayBufferBinding;
	}

	if ( s_fns.GetIntegerv ) {
		s_fns.GetIntegerv( GL_ARRAY_BUFFER_BINDING, &current );
		state->arrayBufferBindingQueries++;
	}
	state->arrayBufferBinding = static_cast<GLuint>( current );
	state->arrayBufferBindingKnown = qtrue;
	return state->arrayBufferBinding;
}

static void GLX_Stream_BindArrayBufferTracked( StreamState *state, GLuint buffer )
{
	if ( s_fns.BindBuffer ) {
		s_fns.BindBuffer( GL_ARRAY_BUFFER, buffer );
	}
	if ( state ) {
		state->arrayBufferBinding = buffer;
		state->arrayBufferBindingKnown = qtrue;
	}
}

static GLuint GLX_Stream_CurrentElementArrayBufferBinding( StreamState *state )
{
	GLint current = 0;

	if ( !state ) {
		return 0;
	}

	if ( state->elementArrayBufferBindingKnown ) {
		state->arrayBufferBindingCacheHits++;
		return state->elementArrayBufferBinding;
	}

	if ( s_fns.GetIntegerv ) {
		s_fns.GetIntegerv( GL_ELEMENT_ARRAY_BUFFER_BINDING, &current );
		state->arrayBufferBindingQueries++;
	}
	state->elementArrayBufferBinding = static_cast<GLuint>( current );
	state->elementArrayBufferBindingKnown = qtrue;
	return state->elementArrayBufferBinding;
}

static void GLX_Stream_BindElementArrayBufferTracked( StreamState *state, GLuint buffer )
{
	if ( s_fns.BindBuffer ) {
		s_fns.BindBuffer( GL_ELEMENT_ARRAY_BUFFER, buffer );
	}
	if ( state ) {
		state->elementArrayBufferBinding = buffer;
		state->elementArrayBufferBindingKnown = qtrue;
	}
}

static void GLX_Stream_DeleteFrameFence( StreamState *state )
{
	if ( !state || !state->frameSync ) {
		return;
	}

	if ( s_fns.DeleteSync ) {
		s_fns.DeleteSync( state->frameSync );
	}
	state->frameSync = nullptr;
}

static qboolean GLX_Stream_WaitFrameFence( StreamState *state )
{
	GLenum result;

	if ( !state || !state->frameSync ) {
		return qtrue;
	}

	if ( !state->syncReady || !s_fns.ClientWaitSync || !s_fns.DeleteSync ) {
		state->syncFailures++;
		return qfalse;
	}

	result = s_fns.ClientWaitSync( state->frameSync, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ULL );
	if ( result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED ) {
		state->syncWaits++;
		GLX_Stream_DeleteFrameFence( state );
		return qtrue;
	}

	if ( result == GL_TIMEOUT_EXPIRED ) {
		state->syncTimeouts++;
		return qfalse;
	}

	state->syncFailures++;
	return qfalse;
}

static void GLX_Stream_DeleteBuffer( StreamState *state )
{
	if ( !state ) {
		return;
	}

	GLX_Stream_DeleteFrameFence( state );

	if ( !state->buffer || !s_fns.DeleteBuffers ) {
		return;
	}

	GLuint oldArrayBuffer = GLX_Stream_CurrentArrayBufferBinding( state );
	const GLuint restoreArrayBuffer = oldArrayBuffer == state->buffer ? 0u : oldArrayBuffer;

	if ( state->mappedPtr && s_fns.UnmapBuffer && s_fns.BindBuffer ) {
		GLX_Stream_BindArrayBufferTracked( state, state->buffer );
		s_fns.UnmapBuffer( GL_ARRAY_BUFFER );
	}

	s_fns.DeleteBuffers( 1, &state->buffer );

	if ( s_fns.BindBuffer ) {
		GLX_Stream_BindArrayBufferTracked( state, restoreArrayBuffer );
	}

	GLX_Stream_ResetRuntime( state );
}

static GLuint GLX_Stream_BindPreserving( StreamState *state, GLuint buffer )
{
	if ( !s_fns.BindBuffer ) {
		return 0;
	}

	const GLuint oldArrayBuffer = GLX_Stream_CurrentArrayBufferBinding( state );
	if ( oldArrayBuffer != buffer ) {
		GLX_Stream_BindArrayBufferTracked( state, buffer );
	}
	return oldArrayBuffer;
}

static void GLX_Stream_RestoreBinding( StreamState *state, GLuint oldArrayBuffer )
{
	if ( !s_fns.BindBuffer ) {
		return;
	}

	if ( !state ) {
		s_fns.BindBuffer( GL_ARRAY_BUFFER, oldArrayBuffer );
		return;
	}

	if ( !state->arrayBufferBindingKnown || state->arrayBufferBinding != oldArrayBuffer ) {
		GLX_Stream_BindArrayBufferTracked( state, oldArrayBuffer );
	}
	state->arrayBufferBindingRestores++;
}

static void GLX_Stream_OrphanBuffer( StreamState *state )
{
	if ( !state || !state->buffer || !s_fns.BufferData ) {
		return;
	}

	s_fns.BufferData( GL_ARRAY_BUFFER, static_cast<ptrdiff_t>( state->ringBytes ), nullptr, GL_STREAM_DRAW );
	state->orphans++;
}

static qboolean GLX_Stream_StrategyNeedsFrameFence( const StreamState *state )
{
	/*
	Orphan and map-range frames re-specify the backing store at offset zero
	(BufferData orphan or MAP_INVALIDATE_BUFFER), so the driver ghosts data
	still referenced by in-flight draws and no cross-frame fence is needed.
	Only the persistently mapped ring rewrites the same storage every frame
	and must therefore wait on the previous frame's fence. Skipping the
	fence elsewhere restores CPU/GPU frame overlap for streamed draws.
	*/
	return state && state->strategy == StreamStrategy::PersistentMapped ? qtrue : qfalse;
}

static qboolean GLX_Stream_CreateBufferObject( StreamState *state )
{
	GLuint oldArrayBuffer = 0;

	s_fns.GenBuffers( 1, &state->buffer );
	if ( !state->buffer ) {
		state->allocationFailures++;
		return qfalse;
	}

	oldArrayBuffer = GLX_Stream_BindPreserving( state, state->buffer );
	GLX_Stream_ClearGLErrors();

	if ( state->strategy == StreamStrategy::PersistentMapped ) {
		if ( !s_fns.BufferStorage || !s_fns.MapBufferRange || !s_fns.UnmapBuffer ) {
			state->fallbackCount++;
			if ( s_fns.MapBufferRange ) {
				state->strategy = StreamStrategy::MapBufferRange;
				GLX_Stream_SetReason( state, "persistent functions unavailable, using map range" );
			} else {
				state->strategy = StreamStrategy::OrphanSubData;
				GLX_Stream_SetReason( state, "persistent functions unavailable, using orphan/subdata" );
			}
		} else {
			const GLbitfield storageFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT | GL_DYNAMIC_STORAGE_BIT;
			const GLbitfield mapFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
			s_fns.BufferStorage( GL_ARRAY_BUFFER, static_cast<ptrdiff_t>( state->ringBytes ), nullptr, storageFlags );

			const GLenum storageError = GLX_Stream_GetGLError();
			if ( storageError == GL_NO_ERROR ) {
				state->mappedPtr = s_fns.MapBufferRange( GL_ARRAY_BUFFER, 0, static_cast<ptrdiff_t>( state->ringBytes ), mapFlags );
				if ( state->mappedPtr ) {
					state->ready = qtrue;
					state->persistentMapped = qtrue;
				} else {
					state->mapFailures++;
				}
			} else {
				state->allocationFailures++;
				if ( storageError == GL_OUT_OF_MEMORY ) {
					RI().Printf( PRINT_DEVELOPER, "GLx stream persistent allocation hit GL_OUT_OF_MEMORY; falling back.\n" );
				}
			}

			if ( !state->ready ) {
				GLuint failedBuffer = state->buffer;
				if ( state->mappedPtr && s_fns.UnmapBuffer ) {
					s_fns.UnmapBuffer( GL_ARRAY_BUFFER );
				}
				state->mappedPtr = nullptr;
				s_fns.DeleteBuffers( 1, &failedBuffer );
				state->buffer = 0;
				state->fallbackCount++;
				if ( s_fns.MapBufferRange ) {
					state->strategy = StreamStrategy::MapBufferRange;
					GLX_Stream_SetReason( state, "persistent allocation unavailable, using map range" );
				} else {
					state->strategy = StreamStrategy::OrphanSubData;
					GLX_Stream_SetReason( state, "persistent allocation unavailable, using orphan/subdata" );
				}
				s_fns.GenBuffers( 1, &state->buffer );
				if ( state->buffer ) {
					GLX_Stream_BindArrayBufferTracked( state, state->buffer );
					GLX_Stream_ClearGLErrors();
				}
			}
		}
	}

	if ( !state->ready && state->buffer ) {
		s_fns.BufferData( GL_ARRAY_BUFFER, static_cast<ptrdiff_t>( state->ringBytes ), nullptr, GL_STREAM_DRAW );
		if ( GLX_Stream_GetGLError() == GL_NO_ERROR ) {
			state->ready = qtrue;
		} else {
			state->allocationFailures++;
		}
	}

	GLX_Stream_RestoreBinding( state, oldArrayBuffer );

	if ( !state->ready ) {
		GLX_Stream_DeleteBuffer( state );
		return qfalse;
	}

	return qtrue;
}

static qboolean GLX_Stream_ConfigureRuntime( StreamState *state, const Capabilities &caps,
	qboolean resetCounters )
{
	const char *requestedMode;
	StreamStrategySelection selection {};
	StreamRuntimeSupport runtimeSupport {};
	StreamRuntimeFallback runtimeFallback {};

	if ( !state ) {
		return qfalse;
	}

	state->strategy = StreamStrategy::OrphanSubData;
	if ( resetCounters ) {
		GLX_Stream_ResetCounters( state );
	}
	state->tier = caps.tier;
	state->ringMegabytes = GLX_Stream_RequestedMegabytes( state );
	state->ringBytes = static_cast<size_t>( state->ringMegabytes ) * 1024u * 1024u;

	if ( caps.tier == RenderProductTier::GL12 ) {
		GLX_Stream_SetReason( state, "GL12 fixed-function tier uses client-memory draw submission" );
		state->ready = qfalse;
		GLX_Stream_RecordRuntimeCvarCounts( state );
		return qtrue;
	}

	requestedMode = state->r_glxStreamMode && state->r_glxStreamMode->string ?
		state->r_glxStreamMode->string : "auto";
	selection = GLX_Stream_SelectStrategy( requestedMode, caps.features );

	if ( !selection.knownMode ) {
		RI().Printf( PRINT_WARNING, "Unknown r_glxStreamMode '%s', using auto.\n",
			requestedMode ? requestedMode : "" );
	}

	state->strategy = selection.strategy;
	state->fallbackCount += selection.fallbackCount;
	GLX_Stream_SetReason( state, selection.reason );

	if ( !GLX_Stream_FunctionsReady() ) {
		GLX_Stream_SetReason( state, "buffer functions unavailable" );
		state->ready = qfalse;
		GLX_Stream_RecordRuntimeCvarCounts( state );
		return qfalse;
	}

	runtimeSupport = {
		state->strategy,
		caps.features.syncObjects,
		caps.features.syncObjects ? GLX_Stream_SyncFunctionsReady() : qfalse,
		caps.features.mapBufferRange,
		s_fns.MapBufferRange ? qtrue : qfalse,
		s_fns.BufferSubData ? qtrue : qfalse
	};
	runtimeFallback = GLX_Stream_ApplyRuntimeFunctionFallbacks( runtimeSupport );
	state->strategy = runtimeFallback.strategy;
	state->syncReady = runtimeFallback.syncReady;
	state->fallbackCount += runtimeFallback.fallbackCount;
	if ( runtimeFallback.reason ) {
		GLX_Stream_SetReason( state, runtimeFallback.reason );
	}
	if ( !runtimeFallback.ready ) {
		state->ready = qfalse;
		GLX_Stream_RecordRuntimeCvarCounts( state );
		return qfalse;
	}

	if ( !GLX_Stream_CreateBufferObject( state ) ) {
		GLX_Stream_SetReason( state, "stream buffer allocation failed" );
		RI().Printf( PRINT_DEVELOPER, "GLx dynamic stream buffer allocation failed.\n" );
		GLX_Stream_RecordRuntimeCvarCounts( state );
		return qfalse;
	}

	GLX_Stream_RecordRuntimeCvarCounts( state );
	return qtrue;
}

static qboolean GLX_Stream_PrepareRange( StreamState *state, size_t bytes, size_t alignment, size_t *offset )
{
	size_t alignedOffset;

	if ( !state || !offset || !state->ready || !state->buffer || bytes == 0 || bytes > state->ringBytes ) {
		if ( state ) {
			state->reserveFailures++;
		}
		return qfalse;
	}

	alignedOffset = GLX_Stream_AlignOffset( state->writeOffset, alignment );
	if ( alignedOffset + bytes > state->ringBytes ) {
		state->wraps++;
		if ( state->frameTouched ) {
			state->sameFrameWrapRejects++;
			state->reserveFailures++;
			return qfalse;
		}
		alignedOffset = 0;
	}

	*offset = alignedOffset;
	state->writeOffset = alignedOffset + bytes;
	return qtrue;
}

void GLX_Stream_RegisterCvars( StreamState *state )
{
	if ( !state ) {
		return;
	}

	state->r_glxStreamMode = RI().Cvar_Get( "r_glxStreamMode", "auto", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	MakeCvarInstant( state->r_glxStreamMode );
	RI().Cvar_SetDescription( state->r_glxStreamMode,
		"Select GLx dynamic geometry streaming strategy: auto, persistent, maprange, or orphan. Applies at the next safe frame boundary." );

	state->r_glxStreamMegabytes = RI().Cvar_Get( "r_glxStreamMegabytes", "8", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	MakeCvarInstant( state->r_glxStreamMegabytes );
	RI().Cvar_CheckRange( state->r_glxStreamMegabytes, "1", "128", CV_INTEGER );
	RI().Cvar_SetDescription( state->r_glxStreamMegabytes, "Target GLx dynamic stream ring size in megabytes. Applies at the next safe frame boundary." );

	state->r_glxStreamTess = RI().Cvar_Get( "r_glxStreamTess", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxStreamTess,
		"Shadow-upload legacy tessellation vertex/index data through the GLx stream ring without drawing from it." );

	state->r_glxStreamDraw = RI().Cvar_Get( "r_glxStreamDraw", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxStreamDraw,
		"Draw eligible generic shader stages from the GLx stream ring. Enabled by the GLx RC profile." );

	state->r_glxStreamDrawKeyMode = RI().Cvar_Get( "r_glxStreamDrawKeyMode", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxStreamDrawKeyMode,
		"Filter GLx streamed draw material keys: 0 plain plus explicit gates, 1 computed, 2 broad single-texture keys." );

	state->r_glxStreamDrawMultitexture = RI().Cvar_Get( "r_glxStreamDrawMultitexture", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxStreamDrawMultitexture,
		"Allow GLx streamed draws for eligible fixed-function multitexture shader stages. Enabled by the GLx RC profile." );

	state->r_glxStreamDrawFog = RI().Cvar_Get( "r_glxStreamDrawFog", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxStreamDrawFog,
		"Allow GLx streamed draws for fog-only passes. Enabled by the GLx RC profile." );

	state->r_glxStreamDrawDepthFragment = RI().Cvar_Get( "r_glxStreamDrawDepthFragment", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxStreamDrawDepthFragment,
		"Allow GLx streamed draws for eligible depthFragment stages. Enabled by the GLx RC profile." );

	state->r_glxStreamDrawTexMods = RI().Cvar_Get( "r_glxStreamDrawTexMods", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxStreamDrawTexMods,
		"Allow GLx streamed draws for stages whose texture coordinates were modified by the legacy CPU texmod path." );

	state->r_glxStreamDrawEnvironment = RI().Cvar_Get( "r_glxStreamDrawEnvironment", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxStreamDrawEnvironment,
		"Allow GLx streamed draws for stages using legacy CPU-computed environment texture coordinates." );

	state->r_glxStreamDrawDynamicLights = RI().Cvar_Get( "r_glxStreamDrawDynamicLights", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxStreamDrawDynamicLights,
		"Allow GLx streamed draws for dynamic-light map stages: 0 off, 1 forced, auto on GL3X+ when the stream is ready." );

	state->r_glxStreamDrawScreenMaps = RI().Cvar_Get( "r_glxStreamDrawScreenMaps", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxStreamDrawScreenMaps,
		"Allow GLx streamed draws for screen-map stages. Experimental and off by default." );

	state->r_glxStreamDrawVideoMaps = RI().Cvar_Get( "r_glxStreamDrawVideoMaps", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxStreamDrawVideoMaps,
		"Allow GLx streamed draws for video-map stages. Experimental and off by default." );

	state->r_glxStreamDrawShadows = RI().Cvar_Get( "r_glxStreamDrawShadows", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxStreamDrawShadows,
		"Allow GLx streamed draws for stencil shadow-volume passes. Enabled by the GLx RC profile." );

	state->r_glxStreamDrawBeams = RI().Cvar_Get( "r_glxStreamDrawBeams", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxStreamDrawBeams,
		"Allow GLx streamed draws for immediate beam entity draw-array passes. Enabled by the GLx RC profile." );

	state->r_glxStreamDrawPostProcess = RI().Cvar_Get( "r_glxStreamDrawPostProcess", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxStreamDrawPostProcess,
		"Allow GLx streamed draws for fullscreen postprocess draw-array passes. Enabled by the GLx RC profile." );
}

void GLX_Stream_OnOpenGLReady( StreamState *state, const Capabilities &caps )
{
	if ( !state ) {
		return;
	}

	GLX_Stream_Shutdown( state );
	GLX_Stream_ConfigureRuntime( state, caps, qtrue );
}

void GLX_Stream_UpdateCvars( StreamState *state, const Capabilities &caps )
{
	if ( !state || !caps.config || !GLX_Stream_RuntimeCvarsChanged( state ) ) {
		return;
	}

	if ( state->frameTouched ) {
		GLX_Stream_SetReason( state, "stream cvar change pending frame boundary" );
		return;
	}

	if ( state->frameSync && !GLX_Stream_WaitFrameFence( state ) ) {
		GLX_Stream_SetReason( state, "stream cvar change pending GPU fence" );
		return;
	}

	GLX_Stream_DeleteBuffer( state );
	GLX_Stream_ConfigureRuntime( state, caps, qfalse );
}

void GLX_Stream_Shutdown( StreamState *state )
{
	if ( !state ) {
		return;
	}

	GLX_Stream_DeleteBuffer( state );
	s_fns = {};

	state->strategy = StreamStrategy::OrphanSubData;
	state->reason[0] = '\0';
	state->ringMegabytes = 0;
	GLX_Stream_ResetRuntime( state );
	GLX_Stream_ResetCounters( state );
}

void GLX_Stream_FrameComplete( StreamState *state )
{
	if ( !state ) {
		return;
	}

	if ( state->syncReady && state->frameTouched && GLX_Stream_StrategyNeedsFrameFence( state ) ) {
		if ( state->frameSync ) {
			state->syncFenceSkips++;
		} else if ( s_fns.FenceSync ) {
			state->frameSync = s_fns.FenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE, 0 );
			if ( state->frameSync ) {
				state->syncInsertions++;
			} else {
				state->syncFailures++;
			}
		}
	}

	state->frames++;
	state->writeOffset = 0;
	state->frameTouched = qfalse;
	GLX_Stream_InvalidateArrayBufferCache( state );
}

qboolean GLX_Stream_Reserve( StreamState *state, size_t bytes, size_t alignment, StreamReservation *reservation )
{
	GLuint oldArrayBuffer = 0;
	size_t offset = 0;
	unsigned int wrapsBefore = 0;
	unsigned int sameFrameWrapRejectsBefore = 0;
	unsigned int syncWaitsBefore = 0;
	unsigned int syncTimeoutsBefore = 0;
	unsigned int syncFailuresBefore = 0;

	if ( !reservation ) {
		if ( state ) {
			state->reserveFailures++;
		}
		return qfalse;
	}

	*reservation = {};

	if ( state ) {
		wrapsBefore = state->wraps;
		sameFrameWrapRejectsBefore = state->sameFrameWrapRejects;
		syncWaitsBefore = state->syncWaits;
		syncTimeoutsBefore = state->syncTimeouts;
		syncFailuresBefore = state->syncFailures;
	}

	if ( state && state->writeOffset == 0 && state->frameSync && !GLX_Stream_WaitFrameFence( state ) ) {
		state->reserveFailures++;
		return qfalse;
	}

	if ( !GLX_Stream_PrepareRange( state, bytes, alignment, &offset ) ) {
		return qfalse;
	}

	reservation->buffer = state->buffer;
	reservation->offset = offset;
	reservation->bytes = bytes;
	reservation->strategy = state->strategy;
	reservation->reserveWraps = state->wraps - wrapsBefore;
	reservation->reserveSameFrameWrapRejects = state->sameFrameWrapRejects - sameFrameWrapRejectsBefore;
	reservation->reserveSyncWaits = state->syncWaits - syncWaitsBefore;
	reservation->reserveSyncTimeouts = state->syncTimeouts - syncTimeoutsBefore;
	reservation->reserveSyncFailures = state->syncFailures - syncFailuresBefore;
	state->frameTouched = qtrue;
	state->lastReservationBytes = static_cast<unsigned int>( bytes > ~0u ? ~0u : bytes );
	state->lastReservationOffset = static_cast<unsigned int>( offset > ~0u ? ~0u : offset );
	state->lastReservationStrategy = state->strategy;
	if ( state->lastReservationBytes > state->largestReservationBytes ) {
		state->largestReservationBytes = state->lastReservationBytes;
	}

	if ( state->strategy == StreamStrategy::PersistentMapped ) {
		reservation->ptr = static_cast<byte *>( state->mappedPtr ) + offset;
		reservation->mapped = qtrue;
		state->reservations++;
		return qtrue;
	}

	oldArrayBuffer = GLX_Stream_BindPreserving( state, state->buffer );

	if ( state->strategy == StreamStrategy::MapBufferRange && s_fns.MapBufferRange && s_fns.UnmapBuffer ) {
		GLbitfield access = GL_MAP_WRITE_BIT;

		if ( offset == 0 ) {
			/*
			First reservation of the frame: re-specify the store so the
			driver orphans data still referenced by in-flight draws. Not
			unsynchronized here - the orphan replaces the cross-frame fence.
			*/
			access |= GL_MAP_INVALIDATE_BUFFER_BIT;
		} else {
			access |= GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_RANGE_BIT;
		}

		reservation->ptr = s_fns.MapBufferRange( GL_ARRAY_BUFFER, static_cast<ptrdiff_t>( offset ),
			static_cast<ptrdiff_t>( bytes ), access );
		if ( reservation->ptr ) {
			reservation->mapped = qtrue;
		} else {
			state->mapFailures++;
			state->strategy = StreamStrategy::OrphanSubData;
			reservation->strategy = StreamStrategy::OrphanSubData;
			GLX_Stream_SetReason( state, "map range reservation failed, using orphan/subdata" );
		}
	}

	if ( reservation->strategy == StreamStrategy::OrphanSubData ) {
		if ( offset == 0 ) {
			GLX_Stream_OrphanBuffer( state );
		}
	}

	GLX_Stream_RestoreBinding( state, oldArrayBuffer );

	state->reservations++;
	return qtrue;
}

qboolean GLX_Stream_Upload( StreamState *state, StreamReservation *reservation, const void *data, size_t bytes )
{
	return GLX_Stream_UploadAt( state, reservation, 0, data, bytes );
}

qboolean GLX_Stream_UploadAt( StreamState *state, StreamReservation *reservation, size_t relativeOffset,
	const void *data, size_t bytes )
{
	GLuint oldArrayBuffer = 0;

	if ( !state || !reservation || !data || bytes == 0 || relativeOffset > reservation->bytes ||
		bytes > reservation->bytes - relativeOffset || reservation->committed ) {
		if ( state ) {
			state->uploadFailures++;
		}
		return qfalse;
	}

	if ( reservation->mapped && reservation->ptr ) {
		std::memcpy( static_cast<byte *>( reservation->ptr ) + relativeOffset, data, bytes );
		state->uploadCalls++;
		state->uploadBytes += static_cast<unsigned long long>( bytes );
		return qtrue;
	}

	if ( !s_fns.BufferSubData ) {
		state->uploadFailures++;
		return qfalse;
	}

	oldArrayBuffer = GLX_Stream_BindPreserving( state, reservation->buffer );
	s_fns.BufferSubData( GL_ARRAY_BUFFER, static_cast<ptrdiff_t>( reservation->offset + relativeOffset ),
		static_cast<ptrdiff_t>( bytes ), data );
	if ( GLX_Stream_GetGLError() != GL_NO_ERROR ) {
		state->uploadFailures++;
		GLX_Stream_RestoreBinding( state, oldArrayBuffer );
		return qfalse;
	}
	GLX_Stream_RestoreBinding( state, oldArrayBuffer );

	state->uploadCalls++;
	state->uploadBytes += static_cast<unsigned long long>( bytes );
	return qtrue;
}

void GLX_Stream_Commit( StreamState *state, StreamReservation *reservation )
{
	GLuint oldArrayBuffer = 0;

	if ( !state || !reservation || reservation->committed ) {
		return;
	}

	if ( reservation->strategy == StreamStrategy::MapBufferRange && reservation->mapped && s_fns.UnmapBuffer ) {
		oldArrayBuffer = GLX_Stream_BindPreserving( state, reservation->buffer );
		if ( !s_fns.UnmapBuffer( GL_ARRAY_BUFFER ) ) {
			state->unmapFailures++;
		}
		GLX_Stream_RestoreBinding( state, oldArrayBuffer );
	}

	reservation->committed = qtrue;
	state->commits++;
}

void GLX_Stream_RecordDlightReservation( StreamState *state, const StreamReservation &reservation )
{
	if ( !state ) {
		return;
	}

	state->streamedDrawDynamicLightReserveWraps += reservation.reserveWraps;
	state->streamedDrawDynamicLightSameFrameWrapRejects += reservation.reserveSameFrameWrapRejects;
	state->streamedDrawDynamicLightSyncWaits += reservation.reserveSyncWaits;
	state->streamedDrawDynamicLightSyncTimeouts += reservation.reserveSyncTimeouts;
	state->streamedDrawDynamicLightSyncFailures += reservation.reserveSyncFailures;
}

GLuint GLX_Stream_BindArrayBufferCached( StreamState *state, GLuint buffer )
{
	if ( !state || !s_fns.BindBuffer ) {
		return 0;
	}

	const GLuint previous = GLX_Stream_CurrentArrayBufferBinding( state );
	if ( previous != buffer ) {
		GLX_Stream_BindArrayBufferTracked( state, buffer );
	}
	return previous;
}

void GLX_Stream_RestoreArrayBufferCached( StreamState *state, GLuint buffer )
{
	if ( !state || !s_fns.BindBuffer ) {
		return;
	}

	if ( !state->arrayBufferBindingKnown || state->arrayBufferBinding != buffer ) {
		GLX_Stream_BindArrayBufferTracked( state, buffer );
	}
	state->arrayBufferBindingRestores++;
}

GLuint GLX_Stream_BindElementArrayBufferCached( StreamState *state, GLuint buffer )
{
	if ( !state || !s_fns.BindBuffer ) {
		return 0;
	}

	const GLuint previous = GLX_Stream_CurrentElementArrayBufferBinding( state );
	if ( previous != buffer ) {
		GLX_Stream_BindElementArrayBufferTracked( state, buffer );
	}
	return previous;
}

void GLX_Stream_RestoreElementArrayBufferCached( StreamState *state, GLuint buffer )
{
	if ( !state || !s_fns.BindBuffer ) {
		return;
	}

	if ( !state->elementArrayBufferBindingKnown || state->elementArrayBufferBinding != buffer ) {
		GLX_Stream_BindElementArrayBufferTracked( state, buffer );
	}
	state->arrayBufferBindingRestores++;
}

qboolean GLX_Stream_DrawEnabled( const StreamState &state )
{
	return state.ready && state.r_glxStreamDraw && state.r_glxStreamDraw->integer ? qtrue : qfalse;
}

qboolean GLX_Stream_DrawMultitextureEnabled( const StreamState &state )
{
	return GLX_Stream_DrawEnabled( state ) &&
		state.r_glxStreamDrawMultitexture && state.r_glxStreamDrawMultitexture->integer ? qtrue : qfalse;
}

qboolean GLX_Stream_DrawFogEnabled( const StreamState &state )
{
	return GLX_Stream_DrawEnabled( state ) &&
		state.r_glxStreamDrawFog && state.r_glxStreamDrawFog->integer ? qtrue : qfalse;
}

qboolean GLX_Stream_DrawDepthFragmentEnabled( const StreamState &state )
{
	return GLX_Stream_DrawEnabled( state ) &&
		state.r_glxStreamDrawDepthFragment && state.r_glxStreamDrawDepthFragment->integer ? qtrue : qfalse;
}

qboolean GLX_Stream_DrawTexModsEnabled( const StreamState &state )
{
	return GLX_Stream_DrawEnabled( state ) &&
		state.r_glxStreamDrawTexMods && state.r_glxStreamDrawTexMods->integer ? qtrue : qfalse;
}

qboolean GLX_Stream_DrawEnvironmentEnabled( const StreamState &state )
{
	return GLX_Stream_DrawEnabled( state ) &&
		state.r_glxStreamDrawEnvironment && state.r_glxStreamDrawEnvironment->integer ? qtrue : qfalse;
}

qboolean GLX_Stream_DrawDynamicLightsEnabled( const StreamState &state )
{
	const cvar_t *cvar = state.r_glxStreamDrawDynamicLights;
	StreamDynamicLightGateConfig config {};

	config.streamDraw = GLX_Stream_DrawEnabled( state );
	config.streamReady = state.ready;
	config.tier = state.tier;
	config.mode = GLX_Stream_ParseDynamicLightGateMode(
		cvar ? cvar->string : nullptr,
		cvar ? cvar->integer : 0 );
	return GLX_Stream_EvaluateDynamicLightGate( config );
}

qboolean GLX_Stream_DrawScreenMapsEnabled( const StreamState &state )
{
	return GLX_Stream_DrawEnabled( state ) &&
		state.r_glxStreamDrawScreenMaps && state.r_glxStreamDrawScreenMaps->integer ? qtrue : qfalse;
}

qboolean GLX_Stream_DrawVideoMapsEnabled( const StreamState &state )
{
	return GLX_Stream_DrawEnabled( state ) &&
		state.r_glxStreamDrawVideoMaps && state.r_glxStreamDrawVideoMaps->integer ? qtrue : qfalse;
}

qboolean GLX_Stream_DrawShadowsEnabled( const StreamState &state )
{
	StreamSpecialDrawGateConfig config {};

	config.streamDraw = GLX_Stream_DrawEnabled( state );
	config.shadows = state.r_glxStreamDrawShadows && state.r_glxStreamDrawShadows->integer ? qtrue : qfalse;
	return GLX_Stream_EvaluateShadowDrawGate( config );
}

qboolean GLX_Stream_DrawBeamsEnabled( const StreamState &state )
{
	StreamSpecialDrawGateConfig config {};

	config.streamDraw = GLX_Stream_DrawEnabled( state );
	config.beams = state.r_glxStreamDrawBeams && state.r_glxStreamDrawBeams->integer ? qtrue : qfalse;
	return GLX_Stream_EvaluateBeamDrawGate( config );
}

qboolean GLX_Stream_DrawPostProcessEnabled( const StreamState &state )
{
	StreamSpecialDrawGateConfig config {};

	config.streamDraw = GLX_Stream_DrawEnabled( state );
	config.postprocess = state.r_glxStreamDrawPostProcess && state.r_glxStreamDrawPostProcess->integer ? qtrue : qfalse;
	return GLX_Stream_EvaluatePostProcessDrawGate( config );
}

static void GLX_Stream_RecordMaterialGate( StreamState *state, qboolean present, qboolean allowed,
	unsigned int *accepted, unsigned int *rejected )
{
	if ( !state || !present ) {
		return;
	}

	if ( allowed ) {
		( *accepted )++;
	} else {
		( *rejected )++;
	}
}

qboolean GLX_Stream_DrawAllowsMaterial( StreamState *state, const MaterialIR &material )
{
	StreamMaterialGateConfig config {};
	StreamMaterialGateResult result;
	MaterialStatePlan plan {};
	unsigned int unsupportedReasons = GLX_MATERIAL_UNSUPPORTED_NONE;
	const RenderProductTier tier = state ? state->tier : RenderProductTier::GL2X;
	const qboolean compilerAllows =
		GLX_Material_StatePlanForTierAndIR( tier, material, &plan,
			&unsupportedReasons );

	if ( state ) {
		config.keyMode = GLX_Stream_DrawKeyMode( *state );
		config.multitexture = GLX_Stream_DrawMultitextureEnabled( *state );
		config.depthFragment = GLX_Stream_DrawDepthFragmentEnabled( *state );
		config.texMods = GLX_Stream_DrawTexModsEnabled( *state );
		config.environment = GLX_Stream_DrawEnvironmentEnabled( *state );
		config.dynamicLights = GLX_Stream_DrawDynamicLightsEnabled( *state );
		config.screenMaps = GLX_Stream_DrawScreenMapsEnabled( *state );
		config.videoMaps = GLX_Stream_DrawVideoMapsEnabled( *state );
	}
	result = GLX_Stream_EvaluateMaterialGate( material.flags, material.texMods0,
		material.texMods1, config );
	if ( !compilerAllows ) {
		result.allowed = qfalse;
	}

	if ( state ) {
		GLX_Stream_RecordMaterialGate( state, result.hasMultitexture, result.multitextureGateAllowed,
			&state->streamedDrawMultitextureAccepted, &state->streamedDrawMultitextureRejected );
		GLX_Stream_RecordMaterialGate( state, result.hasDepthFragment, result.depthFragmentGateAllowed,
			&state->streamedDrawDepthFragmentAccepted, &state->streamedDrawDepthFragmentRejected );
		GLX_Stream_RecordMaterialGate( state, result.hasTexMods, result.texModsGateAllowed,
			&state->streamedDrawTexModAccepted, &state->streamedDrawTexModRejected );
		GLX_Stream_RecordMaterialGate( state, result.hasEnvironment, result.environmentGateAllowed,
			&state->streamedDrawEnvironmentAccepted, &state->streamedDrawEnvironmentRejected );
		GLX_Stream_RecordMaterialGate( state, result.hasDynamicLight, result.dynamicLightGateAllowed,
			&state->streamedDrawDynamicLightAccepted, &state->streamedDrawDynamicLightRejected );
		GLX_Stream_RecordMaterialGate( state, result.hasScreenMap, result.screenMapGateAllowed,
			&state->streamedDrawScreenMapAccepted, &state->streamedDrawScreenMapRejected );
		GLX_Stream_RecordMaterialGate( state, result.hasVideoMap, result.videoMapGateAllowed,
			&state->streamedDrawVideoMapAccepted, &state->streamedDrawVideoMapRejected );
		if ( result.allowed ) {
			state->streamedDrawMaterialAccepted++;
		} else {
			state->streamedDrawMaterialRejected++;
		}
		if ( !compilerAllows ) {
			state->streamedDrawMaterialCompilerRejected++;
			state->streamedDrawMaterialCompilerLastUnsupportedReasons = unsupportedReasons;
		}
	}

	(void)plan;
	(void)unsupportedReasons;
	return result.allowed;
}

static void GLX_Stream_RecordDynamicCategoryResult( StreamState *state, unsigned int categoryMask,
	qboolean success )
{
	if ( !state ) {
		return;
	}

	for ( int i = 0; i < GLX_DYNAMIC_CATEGORY_COUNT; i++ ) {
		if ( !( categoryMask & ( 1u << i ) ) ) {
			continue;
		}
		state->streamedDrawCategoryAttempts[i]++;
		if ( success ) {
			state->streamedDrawCategoryDraws[i]++;
		} else {
			state->streamedDrawCategoryFallbacks[i]++;
		}
	}
}

static void GLX_Stream_RecordDynamicRoleResult( StreamState *state, DynamicDrawRole role,
	qboolean success )
{
	const int index = static_cast<int>( role );

	if ( !state || !GLX_RenderIR_DynamicDrawRoleImplemented( role ) ||
		index < 0 || index >= GLX_RENDER_IR_DYNAMIC_DRAW_ROLE_COUNT ) {
		return;
	}

	state->streamedDrawRoleAttempts[index]++;
	if ( success ) {
		state->streamedDrawRoleDraws[index]++;
	} else {
		state->streamedDrawRoleFallbacks[index]++;
	}
}

void GLX_Stream_RecordDrawResult( StreamState *state, int numVertexes, int numIndexes,
	int totalBytes, int indexBytes, int texcoord1Bytes, qboolean multitexture, qboolean fog,
	qboolean depthFragment, int materialFlags, unsigned int categoryMask, qboolean success )
{
	const unsigned int normalizedCategoryMask =
		GLX_Stream_NormalizeDynamicCategoryMask( categoryMask, materialFlags );
	const DynamicDrawRole role =
		GLX_RenderIR_ClassifyDynamicDrawRole( materialFlags, normalizedCategoryMask );

	if ( !state ) {
		return;
	}

	if ( materialFlags & GLX_STAGE_DLIGHT_MAP ) {
		state->streamedDrawDynamicLightAttempts++;
		if ( totalBytes > 0 ) {
			state->streamedDrawDynamicLightAttemptBytes += static_cast<unsigned long long>( totalBytes );
		}
	}
	state->streamedDrawAttempts++;
	GLX_Stream_RecordDynamicCategoryResult( state, normalizedCategoryMask, success );
	GLX_Stream_RecordDynamicRoleResult( state, role, success );
	if ( success ) {
		state->streamedDraws++;
		if ( numVertexes > 0 ) {
			state->streamedDrawVertexes += static_cast<unsigned int>( numVertexes );
		}
		if ( numIndexes > 0 ) {
			state->streamedDrawIndexes += static_cast<unsigned int>( numIndexes );
		}
		if ( totalBytes > 0 ) {
			state->streamedDrawBytes += static_cast<unsigned long long>( totalBytes );
		}
		if ( indexBytes > 0 ) {
			state->streamedDrawIndexBytes += static_cast<unsigned long long>( indexBytes );
		}
		if ( texcoord1Bytes > 0 ) {
			state->streamedDrawTexcoord1Bytes += static_cast<unsigned long long>( texcoord1Bytes );
		}
		if ( multitexture ) {
			state->streamedDrawMultitextureDraws++;
		}
		if ( fog ) {
			state->streamedDrawFogDraws++;
		}
		if ( depthFragment ) {
			state->streamedDrawDepthFragmentDraws++;
		}
		if ( materialFlags & GLX_STAGE_TEXMOD ) {
			state->streamedDrawTexModDraws++;
		}
		if ( materialFlags & GLX_STAGE_ENVIRONMENT ) {
			state->streamedDrawEnvironmentDraws++;
		}
		if ( materialFlags & GLX_STAGE_DLIGHT_MAP ) {
			state->streamedDrawDynamicLightDraws++;
			if ( totalBytes > 0 ) {
				state->streamedDrawDynamicLightBytes += static_cast<unsigned long long>( totalBytes );
			}
			if ( indexBytes > 0 ) {
				state->streamedDrawDynamicLightIndexBytes += static_cast<unsigned long long>( indexBytes );
			}
			if ( texcoord1Bytes > 0 ) {
				state->streamedDrawDynamicLightTexcoord1Bytes += static_cast<unsigned long long>( texcoord1Bytes );
			}
		}
		if ( materialFlags & GLX_STAGE_SCREEN_MAP ) {
			state->streamedDrawScreenMapDraws++;
		}
		if ( materialFlags & GLX_STAGE_VIDEO_MAP ) {
			state->streamedDrawVideoMapDraws++;
		}
		if ( materialFlags & GLX_STAGE_SHADOW_PASS ) {
			state->streamedDrawShadowDraws++;
		}
		if ( materialFlags & GLX_STAGE_BEAM_PASS ) {
			state->streamedDrawBeamDraws++;
		}
		if ( materialFlags & GLX_STAGE_POSTPROCESS_PASS ) {
			state->streamedDrawPostProcessDraws++;
		}
	} else {
		state->streamedDrawFallbacks++;
		if ( materialFlags & GLX_STAGE_DLIGHT_MAP ) {
			state->streamedDrawDynamicLightFallbacks++;
		}
	}
}

void GLX_Stream_RecordDrawSkip( StreamState *state, int reason )
{
	if ( !state ) {
		return;
	}

	state->streamedDrawSkips++;
	if ( reason >= 0 && reason < GLX_STREAM_SKIP_REASON_COUNT ) {
		state->streamedDrawSkipReasons[reason]++;
	}
}

void GLX_Stream_RunSelfTest( StreamState *state )
{
	byte payload[256];
	StreamReservation reservation;

	if ( !state || !state->ready ) {
		RI().Printf( PRINT_ALL, "GLx stream test: stream buffer is not ready.\n" );
		return;
	}

	for ( size_t i = 0; i < sizeof( payload ); i++ ) {
		payload[i] = static_cast<byte>( i ^ 0x5a );
	}

	state->selfTests++;
	if ( !GLX_Stream_Reserve( state, sizeof( payload ), 64, &reservation ) ) {
		RI().Printf( PRINT_WARNING, "GLx stream test: reservation failed.\n" );
		return;
	}

	if ( !GLX_Stream_Upload( state, &reservation, payload, sizeof( payload ) ) ) {
		RI().Printf( PRINT_WARNING, "GLx stream test: upload failed.\n" );
		GLX_Stream_Commit( state, &reservation );
		return;
	}

	GLX_Stream_Commit( state, &reservation );
	RI().Printf( PRINT_ALL, "GLx stream test: uploaded %u bytes at offset %u using %s.\n",
		static_cast<unsigned int>( reservation.bytes ),
		static_cast<unsigned int>( reservation.offset ),
		GLX_Stream_StrategyName( reservation.strategy ) );
}

void GLX_Stream_ShadowUploadTess( StreamState *state, int numVertexes, int numIndexes,
	const void *xyz, size_t xyzBytes, const void *indexes, size_t indexBytes )
{
	StreamReservation vertices;
	StreamReservation elements;
	qboolean ok = qtrue;

	if ( !state || !state->r_glxStreamTess || !state->r_glxStreamTess->integer ) {
		return;
	}

	if ( !state->ready || numVertexes <= 0 || numIndexes <= 0 || !xyz || !indexes || xyzBytes == 0 || indexBytes == 0 ) {
		if ( state ) {
			state->shadowTessSkips++;
		}
		return;
	}

	if ( !GLX_Stream_Reserve( state, xyzBytes, 64, &vertices ) ) {
		state->shadowTessFailures++;
		return;
	}

	if ( !GLX_Stream_Upload( state, &vertices, xyz, xyzBytes ) ) {
		ok = qfalse;
	}
	GLX_Stream_Commit( state, &vertices );

	if ( !GLX_Stream_Reserve( state, indexBytes, 64, &elements ) ) {
		state->shadowTessFailures++;
		return;
	}

	if ( !GLX_Stream_Upload( state, &elements, indexes, indexBytes ) ) {
		ok = qfalse;
	}
	GLX_Stream_Commit( state, &elements );

	if ( ok ) {
		state->shadowTessUploads++;
		state->shadowTessBytes += static_cast<unsigned long long>( xyzBytes + indexBytes );
	} else {
		state->shadowTessFailures++;
	}
}

const char *GLX_Stream_StrategyName( StreamStrategy strategy )
{
	switch ( strategy ) {
	case StreamStrategy::PersistentMapped:
		return "persistent-map";
	case StreamStrategy::MapBufferRange:
		return "map-range";
	case StreamStrategy::OrphanSubData:
		return "orphan-subdata";
	default:
		return "unknown";
	}
}

void GLX_Stream_PrintInfo( const StreamState &state )
{
	RI().Printf( PRINT_ALL, "  dynamic stream strategy: %s\n", GLX_Stream_StrategyName( state.strategy ) );
	RI().Printf( PRINT_ALL, "  dynamic stream reason: %s\n", state.reason[0] ? state.reason : "not initialized" );
	RI().Printf( PRINT_ALL, "  dynamic stream target ring: %i MB\n", state.ringMegabytes );
	RI().Printf( PRINT_ALL, "  dynamic stream buffer: %s%s\n", BoolName( state.ready ),
		state.persistentMapped ? " (persistent mapped)" : "" );
	RI().Printf( PRINT_ALL, "  dynamic stream forced fallbacks: %u\n", state.fallbackCount );
	RI().Printf( PRINT_ALL, "  dynamic stream allocation failures: %u\n", state.allocationFailures );
	RI().Printf( PRINT_ALL, "  dynamic stream map failures: %u\n", state.mapFailures );
	RI().Printf( PRINT_ALL, "  dynamic stream unmap failures: %u\n", state.unmapFailures );
	RI().Printf( PRINT_ALL, "  dynamic stream sync: %s, fences %u, waits %u, timeouts %u, failures %u, pending skips %u\n",
		BoolName( state.syncReady ), state.syncInsertions, state.syncWaits,
		state.syncTimeouts, state.syncFailures, state.syncFenceSkips );
	RI().Printf( PRINT_ALL, "  dynamic stream reservations: %u, commits: %u, wraps: %u, same-frame wrap rejects: %u, orphans: %u\n",
		state.reservations, state.commits, state.wraps, state.sameFrameWrapRejects, state.orphans );
	RI().Printf( PRINT_ALL, "  dynamic stream reservation shape: last %u bytes at %u using %s, largest %u bytes\n",
		state.lastReservationBytes, state.lastReservationOffset,
		GLX_Stream_StrategyName( state.lastReservationStrategy ),
		state.largestReservationBytes );
	RI().Printf( PRINT_ALL, "  dynamic stream uploads: %u calls, %.2f MB, failures %u\n",
		state.uploadCalls, static_cast<double>( state.uploadBytes ) / ( 1024.0 * 1024.0 ), state.uploadFailures );
	RI().Printf( PRINT_ALL, "  dynamic stream binding cache: queries %u, hits %u, restores %u, invalidations %u, external %u, array known %s buffer %u, element known %s buffer %u\n",
		state.arrayBufferBindingQueries,
		state.arrayBufferBindingCacheHits,
		state.arrayBufferBindingRestores,
		state.arrayBufferBindingInvalidations,
		state.bufferBindingExternalUpdates,
		BoolName( state.arrayBufferBindingKnown ),
		state.arrayBufferBinding,
		BoolName( state.elementArrayBufferBindingKnown ),
		state.elementArrayBufferBinding );
	RI().Printf( PRINT_ALL, "  dynamic stream tess shadow uploads: %u batches, %.2f MB, skips %u, failures %u\n",
		state.shadowTessUploads, static_cast<double>( state.shadowTessBytes ) / ( 1024.0 * 1024.0 ),
		state.shadowTessSkips, state.shadowTessFailures );
	RI().Printf( PRINT_ALL, "  dynamic stream draw key mode: %i (%s), accepted %u, rejected %u\n",
		GLX_Stream_DrawKeyMode( state ), GLX_Stream_DrawKeyModeName( GLX_Stream_DrawKeyMode( state ) ),
		state.streamedDrawMaterialAccepted, state.streamedDrawMaterialRejected );
	RI().Printf( PRINT_ALL, "  dynamic stream material compiler: rejected %u, last unsupported 0x%x (%s)\n",
		state.streamedDrawMaterialCompilerRejected,
		state.streamedDrawMaterialCompilerLastUnsupportedReasons,
		GLX_Material_UnsupportedReasonName(
			state.streamedDrawMaterialCompilerLastUnsupportedReasons ) );
	RI().Printf( PRINT_ALL, "  dynamic stream multitexture gate: %s, accepted %u, rejected %u\n",
		BoolName( GLX_Stream_DrawMultitextureEnabled( state ) ),
		state.streamedDrawMultitextureAccepted, state.streamedDrawMultitextureRejected );
	RI().Printf( PRINT_ALL, "  dynamic stream depth-fragment gate: %s, accepted %u, rejected %u\n",
		BoolName( GLX_Stream_DrawDepthFragmentEnabled( state ) ),
		state.streamedDrawDepthFragmentAccepted, state.streamedDrawDepthFragmentRejected );
	RI().Printf( PRINT_ALL, "  dynamic stream texmod gate: %s, accepted %u, rejected %u\n",
		BoolName( GLX_Stream_DrawTexModsEnabled( state ) ),
		state.streamedDrawTexModAccepted, state.streamedDrawTexModRejected );
	RI().Printf( PRINT_ALL, "  dynamic stream environment gate: %s, accepted %u, rejected %u\n",
		BoolName( GLX_Stream_DrawEnvironmentEnabled( state ) ),
		state.streamedDrawEnvironmentAccepted, state.streamedDrawEnvironmentRejected );
	RI().Printf( PRINT_ALL, "  dynamic stream dynamic-light gate: %s, accepted %u, rejected %u\n",
		BoolName( GLX_Stream_DrawDynamicLightsEnabled( state ) ),
		state.streamedDrawDynamicLightAccepted, state.streamedDrawDynamicLightRejected );
	RI().Printf( PRINT_ALL, "  dynamic stream dynamic-light telemetry: attempts %u, draws %u, fallbacks %u, attempt %.2f MB, draw %.2f MB, index %.2f MB, tex1 %.2f MB, wraps %u, same-frame rejects %u, waits %u, timeouts %u, sync failures %u\n",
		state.streamedDrawDynamicLightAttempts,
		state.streamedDrawDynamicLightDraws,
		state.streamedDrawDynamicLightFallbacks,
		static_cast<double>( state.streamedDrawDynamicLightAttemptBytes ) / ( 1024.0 * 1024.0 ),
		static_cast<double>( state.streamedDrawDynamicLightBytes ) / ( 1024.0 * 1024.0 ),
		static_cast<double>( state.streamedDrawDynamicLightIndexBytes ) / ( 1024.0 * 1024.0 ),
		static_cast<double>( state.streamedDrawDynamicLightTexcoord1Bytes ) / ( 1024.0 * 1024.0 ),
		state.streamedDrawDynamicLightReserveWraps,
		state.streamedDrawDynamicLightSameFrameWrapRejects,
		state.streamedDrawDynamicLightSyncWaits,
		state.streamedDrawDynamicLightSyncTimeouts,
		state.streamedDrawDynamicLightSyncFailures );
	RI().Printf( PRINT_ALL, "  dynamic stream screen-map gate: %s, accepted %u, rejected %u\n",
		BoolName( GLX_Stream_DrawScreenMapsEnabled( state ) ),
		state.streamedDrawScreenMapAccepted, state.streamedDrawScreenMapRejected );
	RI().Printf( PRINT_ALL, "  dynamic stream video-map gate: %s, accepted %u, rejected %u\n",
		BoolName( GLX_Stream_DrawVideoMapsEnabled( state ) ),
		state.streamedDrawVideoMapAccepted, state.streamedDrawVideoMapRejected );
	RI().Printf( PRINT_ALL, "  dynamic stream shadow-volume gate: %s, draws %u\n",
		BoolName( GLX_Stream_DrawShadowsEnabled( state ) ),
		state.streamedDrawShadowDraws );
	RI().Printf( PRINT_ALL, "  dynamic stream beam gate: %s, draws %u\n",
		BoolName( GLX_Stream_DrawBeamsEnabled( state ) ),
		state.streamedDrawBeamDraws );
	RI().Printf( PRINT_ALL, "  dynamic stream postprocess gate: %s, draws %u\n",
		BoolName( GLX_Stream_DrawPostProcessEnabled( state ) ),
		state.streamedDrawPostProcessDraws );
	RI().Printf( PRINT_ALL, "  dynamic stream draws: %u/%u attempts, %u verts, %u indexes, %.2f MB, index %.2f MB, tex1 %.2f MB, mt %u, fog %u, depthfrag %u, texmod %u, env %u, dlight %u, screen %u, video %u, shadow %u, beam %u, post %u, fallbacks %u\n",
		state.streamedDraws, state.streamedDrawAttempts,
		state.streamedDrawVertexes, state.streamedDrawIndexes,
		static_cast<double>( state.streamedDrawBytes ) / ( 1024.0 * 1024.0 ),
		static_cast<double>( state.streamedDrawIndexBytes ) / ( 1024.0 * 1024.0 ),
		static_cast<double>( state.streamedDrawTexcoord1Bytes ) / ( 1024.0 * 1024.0 ),
		state.streamedDrawMultitextureDraws,
		state.streamedDrawFogDraws,
		state.streamedDrawDepthFragmentDraws,
		state.streamedDrawTexModDraws,
		state.streamedDrawEnvironmentDraws,
		state.streamedDrawDynamicLightDraws,
		state.streamedDrawScreenMapDraws,
		state.streamedDrawVideoMapDraws,
		state.streamedDrawShadowDraws,
		state.streamedDrawBeamDraws,
		state.streamedDrawPostProcessDraws,
		state.streamedDrawFallbacks );
	RI().Printf( PRINT_ALL, "  dynamic stream categories: entity %u/%u, particle %u/%u, poly %u/%u, mark %u/%u, weapon %u/%u, ui %u/%u, beam %u/%u, dlight %u/%u, special %u/%u\n",
		state.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_ENTITY],
		state.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_ENTITY],
		state.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_PARTICLE],
		state.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_PARTICLE],
		state.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_POLY],
		state.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_POLY],
		state.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_MARK],
		state.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_MARK],
		state.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_WEAPON],
		state.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_WEAPON],
		state.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_UI],
		state.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_UI],
		state.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_BEAM],
		state.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_BEAM],
		state.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_DLIGHT],
		state.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_DLIGHT],
		state.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_SPECIAL],
		state.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_SPECIAL] );
	RI().Printf( PRINT_ALL, "  dynamic stream category fallbacks: entity %u, particle %u, poly %u, mark %u, weapon %u, ui %u, beam %u, dlight %u, special %u\n",
		state.streamedDrawCategoryFallbacks[GLX_DYNAMIC_CATEGORY_ENTITY],
		state.streamedDrawCategoryFallbacks[GLX_DYNAMIC_CATEGORY_PARTICLE],
		state.streamedDrawCategoryFallbacks[GLX_DYNAMIC_CATEGORY_POLY],
		state.streamedDrawCategoryFallbacks[GLX_DYNAMIC_CATEGORY_MARK],
		state.streamedDrawCategoryFallbacks[GLX_DYNAMIC_CATEGORY_WEAPON],
		state.streamedDrawCategoryFallbacks[GLX_DYNAMIC_CATEGORY_UI],
		state.streamedDrawCategoryFallbacks[GLX_DYNAMIC_CATEGORY_BEAM],
		state.streamedDrawCategoryFallbacks[GLX_DYNAMIC_CATEGORY_DLIGHT],
		state.streamedDrawCategoryFallbacks[GLX_DYNAMIC_CATEGORY_SPECIAL] );
	RI().Printf( PRINT_ALL, "  dynamic stream IR roles: generic %u/%u/%u, dlight %u/%u/%u, shadow %u/%u/%u, beam %u/%u/%u, post %u/%u/%u\n",
		state.streamedDrawRoleDraws[static_cast<int>( DynamicDrawRole::Generic )],
		state.streamedDrawRoleAttempts[static_cast<int>( DynamicDrawRole::Generic )],
		state.streamedDrawRoleFallbacks[static_cast<int>( DynamicDrawRole::Generic )],
		state.streamedDrawRoleDraws[static_cast<int>( DynamicDrawRole::DynamicLight )],
		state.streamedDrawRoleAttempts[static_cast<int>( DynamicDrawRole::DynamicLight )],
		state.streamedDrawRoleFallbacks[static_cast<int>( DynamicDrawRole::DynamicLight )],
		state.streamedDrawRoleDraws[static_cast<int>( DynamicDrawRole::Shadow )],
		state.streamedDrawRoleAttempts[static_cast<int>( DynamicDrawRole::Shadow )],
		state.streamedDrawRoleFallbacks[static_cast<int>( DynamicDrawRole::Shadow )],
		state.streamedDrawRoleDraws[static_cast<int>( DynamicDrawRole::Beam )],
		state.streamedDrawRoleAttempts[static_cast<int>( DynamicDrawRole::Beam )],
		state.streamedDrawRoleFallbacks[static_cast<int>( DynamicDrawRole::Beam )],
		state.streamedDrawRoleDraws[static_cast<int>( DynamicDrawRole::PostProcess )],
		state.streamedDrawRoleAttempts[static_cast<int>( DynamicDrawRole::PostProcess )],
		state.streamedDrawRoleFallbacks[static_cast<int>( DynamicDrawRole::PostProcess )] );
	RI().Printf( PRINT_ALL, "  dynamic stream draw skips: %u (bind %u, input %u, mt %u, depthfrag %u, texcoord %u, empty %u, key %u, fog %u, program %u)\n",
		state.streamedDrawSkips,
		state.streamedDrawSkipReasons[GLX_STREAM_SKIP_NO_BIND_BUFFER],
		state.streamedDrawSkipReasons[GLX_STREAM_SKIP_BAD_INPUT],
		state.streamedDrawSkipReasons[GLX_STREAM_SKIP_MULTITEXTURE],
		state.streamedDrawSkipReasons[GLX_STREAM_SKIP_DEPTH_FRAGMENT],
		state.streamedDrawSkipReasons[GLX_STREAM_SKIP_NO_TEXCOORDS],
		state.streamedDrawSkipReasons[GLX_STREAM_SKIP_EMPTY_BATCH],
		state.streamedDrawSkipReasons[GLX_STREAM_SKIP_MATERIAL_KEY],
		state.streamedDrawSkipReasons[GLX_STREAM_SKIP_FOG],
		state.streamedDrawSkipReasons[GLX_STREAM_SKIP_MATERIAL_PROGRAM] );
	RI().Printf( PRINT_ALL, "  dynamic stream reservation failures: %u\n", state.reserveFailures );
	RI().Printf( PRINT_ALL, "  dynamic stream self-tests: %u\n", state.selfTests );
	RI().Printf( PRINT_ALL, "  dynamic stream frames observed: %u\n", state.frames );
}

} // namespace glx
