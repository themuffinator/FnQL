#include "glx_debug.h"

#ifndef GL_DEBUG_OUTPUT
#define GL_DEBUG_OUTPUT 0x92E0
#endif
#ifndef GL_DEBUG_OUTPUT_SYNCHRONOUS
#define GL_DEBUG_OUTPUT_SYNCHRONOUS 0x8242
#endif
#ifndef GL_DEBUG_SOURCE_API
#define GL_DEBUG_SOURCE_API 0x8246
#endif
#ifndef GL_DEBUG_SOURCE_WINDOW_SYSTEM
#define GL_DEBUG_SOURCE_WINDOW_SYSTEM 0x8247
#endif
#ifndef GL_DEBUG_SOURCE_SHADER_COMPILER
#define GL_DEBUG_SOURCE_SHADER_COMPILER 0x8248
#endif
#ifndef GL_DEBUG_SOURCE_THIRD_PARTY
#define GL_DEBUG_SOURCE_THIRD_PARTY 0x8249
#endif
#ifndef GL_DEBUG_SOURCE_APPLICATION
#define GL_DEBUG_SOURCE_APPLICATION 0x824A
#endif
#ifndef GL_DEBUG_SOURCE_OTHER
#define GL_DEBUG_SOURCE_OTHER 0x824B
#endif
#ifndef GL_DEBUG_TYPE_ERROR
#define GL_DEBUG_TYPE_ERROR 0x824C
#endif
#ifndef GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR
#define GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR 0x824D
#endif
#ifndef GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR
#define GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR 0x824E
#endif
#ifndef GL_DEBUG_TYPE_PORTABILITY
#define GL_DEBUG_TYPE_PORTABILITY 0x824F
#endif
#ifndef GL_DEBUG_TYPE_PERFORMANCE
#define GL_DEBUG_TYPE_PERFORMANCE 0x8250
#endif
#ifndef GL_DEBUG_TYPE_OTHER
#define GL_DEBUG_TYPE_OTHER 0x8251
#endif
#ifndef GL_DEBUG_TYPE_MARKER
#define GL_DEBUG_TYPE_MARKER 0x8268
#endif
#ifndef GL_DEBUG_TYPE_PUSH_GROUP
#define GL_DEBUG_TYPE_PUSH_GROUP 0x8269
#endif
#ifndef GL_DEBUG_TYPE_POP_GROUP
#define GL_DEBUG_TYPE_POP_GROUP 0x826A
#endif
#ifndef GL_DEBUG_SEVERITY_HIGH
#define GL_DEBUG_SEVERITY_HIGH 0x9146
#endif
#ifndef GL_DEBUG_SEVERITY_MEDIUM
#define GL_DEBUG_SEVERITY_MEDIUM 0x9147
#endif
#ifndef GL_DEBUG_SEVERITY_LOW
#define GL_DEBUG_SEVERITY_LOW 0x9148
#endif
#ifndef GL_DEBUG_SEVERITY_NOTIFICATION
#define GL_DEBUG_SEVERITY_NOTIFICATION 0x826B
#endif
#ifndef GL_DONT_CARE
#define GL_DONT_CARE 0x1100
#endif

namespace glx {

static const char *GLX_Debug_SourceName( GLenum source )
{
	switch ( source ) {
	case GL_DEBUG_SOURCE_API:
		return "api";
	case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
		return "window-system";
	case GL_DEBUG_SOURCE_SHADER_COMPILER:
		return "shader-compiler";
	case GL_DEBUG_SOURCE_THIRD_PARTY:
		return "third-party";
	case GL_DEBUG_SOURCE_APPLICATION:
		return "application";
	case GL_DEBUG_SOURCE_OTHER:
		return "other";
	default:
		return "unknown";
	}
}

static const char *GLX_Debug_TypeName( GLenum type )
{
	switch ( type ) {
	case GL_DEBUG_TYPE_ERROR:
		return "error";
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
		return "deprecated";
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
		return "undefined";
	case GL_DEBUG_TYPE_PORTABILITY:
		return "portability";
	case GL_DEBUG_TYPE_PERFORMANCE:
		return "performance";
	case GL_DEBUG_TYPE_OTHER:
		return "other";
	case GL_DEBUG_TYPE_MARKER:
		return "marker";
	case GL_DEBUG_TYPE_PUSH_GROUP:
		return "push-group";
	case GL_DEBUG_TYPE_POP_GROUP:
		return "pop-group";
	default:
		return "unknown";
	}
}

static const char *GLX_Debug_SeverityName( GLenum severity )
{
	switch ( severity ) {
	case GL_DEBUG_SEVERITY_HIGH:
		return "high";
	case GL_DEBUG_SEVERITY_MEDIUM:
		return "medium";
	case GL_DEBUG_SEVERITY_LOW:
		return "low";
	case GL_DEBUG_SEVERITY_NOTIFICATION:
		return "notification";
	default:
		return "unknown";
	}
}

static void *GLX_Debug_GetProc( const char *name, const char *fallbackName = nullptr )
{
	void *proc = RI().GL_GetProcAddress ? RI().GL_GetProcAddress( name ) : nullptr;

	if ( !proc && fallbackName ) {
		proc = RI().GL_GetProcAddress( fallbackName );
	}

	return proc;
}

static void APIENTRY GLX_Debug_Callback( GLenum source, GLenum type, GLuint id, GLenum severity,
	GLsizei length, const GLchar *message, const void *userParam );

static int GLX_Debug_CvarModificationCount( const cvar_t *cvar )
{
	return cvar ? cvar->modificationCount : 0;
}

static void GLX_Debug_RecordRuntimeCvarCounts( DebugState *state )
{
	if ( !state ) {
		return;
	}

	state->lastDebugModificationCount = GLX_Debug_CvarModificationCount( state->r_glxDebug );
	state->lastVerboseModificationCount = GLX_Debug_CvarModificationCount( state->r_glxDebugVerbose );
}

static qboolean GLX_Debug_RuntimeCvarsChanged( const DebugState *state )
{
	if ( !state ) {
		return qfalse;
	}

	return ( state->lastDebugModificationCount != GLX_Debug_CvarModificationCount( state->r_glxDebug ) ||
		state->lastVerboseModificationCount != GLX_Debug_CvarModificationCount( state->r_glxDebugVerbose ) ) ?
		qtrue : qfalse;
}

static void GLX_Debug_ApplyNotificationFilter( DebugState *state )
{
	if ( !state || !state->khrDebugOutput || !state->fns.DebugMessageControl ) {
		return;
	}

	state->fns.DebugMessageControl( GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr,
		GLX_Debug_Verbose( *state ) ? GL_TRUE : GL_FALSE );
}

static void GLX_Debug_DisconnectCallback( DebugState *state )
{
	if ( !state ) {
		return;
	}

	while ( state->groupsPushed && state->fns.PopDebugGroup ) {
		state->fns.PopDebugGroup();
		state->groupsPushed--;
	}

	if ( state->callbackInstalled && state->fns.DebugMessageCallback ) {
		state->fns.DebugMessageCallback( nullptr, nullptr );
	}
	if ( state->fns.Disable ) {
		state->fns.Disable( GL_DEBUG_OUTPUT_SYNCHRONOUS );
		if ( state->khrDebugOutput ) {
			state->fns.Disable( GL_DEBUG_OUTPUT );
		}
	}
	state->callbackInstalled = qfalse;
}

static void GLX_Debug_InstallCallback( DebugState *state, const Capabilities &caps )
{
	if ( !state || !state->r_glxDebug || !state->r_glxDebug->integer ) {
		GLX_Debug_DisconnectCallback( state );
		return;
	}

	if ( !caps.features.debugOutput ) {
		return;
	}

	if ( !state->fns.DebugMessageCallback ) {
		RI().Printf( PRINT_WARNING, "GLx debug requested, but debug-output callback functions are unavailable.\n" );
		return;
	}

	if ( caps.features.khrDebug && !state->fns.Enable ) {
		RI().Printf( PRINT_WARNING, "GLx KHR_debug requested, but glEnable is unavailable.\n" );
		return;
	}

	if ( state->fns.Enable ) {
		if ( caps.features.khrDebug ) {
			state->fns.Enable( GL_DEBUG_OUTPUT );
		}
		state->fns.Enable( GL_DEBUG_OUTPUT_SYNCHRONOUS );
	}

	GLX_Debug_ApplyNotificationFilter( state );
	state->fns.DebugMessageCallback( GLX_Debug_Callback, state );
	state->callbackInstalled = qtrue;
}

static void APIENTRY GLX_Debug_Callback( GLenum source, GLenum type, GLuint id, GLenum severity,
	GLsizei length, const GLchar *message, const void *userParam )
{
	const DebugState *state = static_cast<const DebugState *>( userParam );

	if ( severity == GL_DEBUG_SEVERITY_NOTIFICATION && state && !GLX_Debug_Verbose( *state ) ) {
		return;
	}

	if ( !ImportsReady() || !g_imports->Printf ) {
		return;
	}

	RI().Printf( PRINT_DEVELOPER, "GLx debug [%s/%s/%s #%u]: %.*s\n",
		GLX_Debug_SourceName( source ), GLX_Debug_TypeName( type ), GLX_Debug_SeverityName( severity ),
		static_cast<unsigned int>( id ), static_cast<int>( length ), message ? message : "" );
}

void GLX_Debug_RegisterCvars( DebugState *state )
{
	if ( !state ) {
		return;
	}

	state->r_glxDebug = RI().Cvar_Get( "r_glxDebug", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	MakeCvarInstant( state->r_glxDebug );
	RI().Cvar_CheckRange( state->r_glxDebug, "0", "1", CV_INTEGER );
	RI().Cvar_SetDescription( state->r_glxDebug, "Request a GLx debug context at startup and toggle debug-output callback wiring immediately when KHR_debug or ARB_debug_output is available." );

	state->r_glxDebugVerbose = RI().Cvar_Get( "r_glxDebugVerbose", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_CheckRange( state->r_glxDebugVerbose, "0", "1", CV_INTEGER );
	RI().Cvar_SetDescription( state->r_glxDebugVerbose, "Print low-volume GLx debug notifications in addition to warnings and errors." );

	state->r_glxDebugGroups = RI().Cvar_Get( "r_glxDebugGroups", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_CheckRange( state->r_glxDebugGroups, "0", "1", CV_INTEGER );
	RI().Cvar_SetDescription( state->r_glxDebugGroups, "Wrap GLx-observed shader batches in KHR_debug groups when available." );
}

void GLX_Debug_OnOpenGLReady( DebugState *state, const Capabilities &caps )
{
	if ( !state ) {
		return;
	}

	state->fns = {};
	state->callbackInstalled = qfalse;
	state->khrDebugOutput = caps.features.khrDebug;
	state->groupsPushed = 0;

	if ( !caps.features.debugOutput || !RI().GL_GetProcAddress ) {
		GLX_Debug_RecordRuntimeCvarCounts( state );
		return;
	}

	state->fns.Enable = reinterpret_cast<PFNGLXENABLEPROC>( GLX_Debug_GetProc( "glEnable" ) );
	state->fns.Disable = reinterpret_cast<PFNGLXDISABLEPROC>( GLX_Debug_GetProc( "glDisable" ) );
	state->fns.DebugMessageCallback = reinterpret_cast<PFNGLXDEBUGMESSAGECALLBACKPROC>(
		GLX_Debug_GetProc( "glDebugMessageCallback", "glDebugMessageCallbackARB" ) );
	state->fns.DebugMessageControl = reinterpret_cast<PFNGLXDEBUGMESSAGECONTROLPROC>(
		GLX_Debug_GetProc( "glDebugMessageControl", "glDebugMessageControlARB" ) );

	if ( caps.features.khrDebug ) {
		state->fns.ObjectLabel = reinterpret_cast<PFNGLXOBJECTLABELPROC>( GLX_Debug_GetProc( "glObjectLabel" ) );
		state->fns.PushDebugGroup = reinterpret_cast<PFNGLXPUSHDEBUGGROUPPROC>( GLX_Debug_GetProc( "glPushDebugGroup" ) );
		state->fns.PopDebugGroup = reinterpret_cast<PFNGLXPOPDEBUGGROUPPROC>( GLX_Debug_GetProc( "glPopDebugGroup" ) );
	}

	GLX_Debug_InstallCallback( state, caps );
	GLX_Debug_RecordRuntimeCvarCounts( state );
}

void GLX_Debug_UpdateCvars( DebugState *state, const Capabilities &caps )
{
	if ( !state || !caps.config || !GLX_Debug_RuntimeCvarsChanged( state ) ) {
		return;
	}

	if ( !state->r_glxDebug || !state->r_glxDebug->integer ) {
		GLX_Debug_DisconnectCallback( state );
	} else if ( state->callbackInstalled ) {
		GLX_Debug_ApplyNotificationFilter( state );
	} else {
		GLX_Debug_InstallCallback( state, caps );
	}
	GLX_Debug_RecordRuntimeCvarCounts( state );
}

void GLX_Debug_Shutdown( DebugState *state )
{
	if ( !state ) {
		return;
	}

	GLX_Debug_DisconnectCallback( state );
	state->fns = {};
	state->callbackInstalled = qfalse;
	state->khrDebugOutput = qfalse;
	state->groupsPushed = 0;
}

qboolean GLX_Debug_CallbackInstalled( const DebugState &state )
{
	return state.callbackInstalled;
}

qboolean GLX_Debug_Verbose( const DebugState &state )
{
	return state.r_glxDebugVerbose && state.r_glxDebugVerbose->integer ? qtrue : qfalse;
}

void GLX_Debug_LabelObject( const DebugState &state, GLenum identifier, GLuint name, const char *label )
{
	if ( !state.fns.ObjectLabel || !name || !label || !*label ) {
		return;
	}

	state.fns.ObjectLabel( identifier, name, -1, label );
}

void GLX_Debug_PushGroup( DebugState *state, const char *label )
{
	if ( !state || !state->r_glxDebugGroups || !state->r_glxDebugGroups->integer ||
		!state->fns.PushDebugGroup || !label || !*label ) {
		return;
	}

	state->fns.PushDebugGroup( GL_DEBUG_SOURCE_APPLICATION, 0, -1, label );
	state->groupsPushed++;
}

void GLX_Debug_PopGroup( DebugState *state )
{
	if ( !state || !state->groupsPushed || !state->fns.PopDebugGroup ) {
		return;
	}

	state->fns.PopDebugGroup();
	state->groupsPushed--;
}

} // namespace glx
