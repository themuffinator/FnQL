#ifndef GLX_DEBUG_H
#define GLX_DEBUG_H

#include "glx_caps.h"

namespace glx {

typedef void ( APIENTRY *GLXDebugProc )( GLenum source, GLenum type, GLuint id, GLenum severity,
	GLsizei length, const GLchar *message, const void *userParam );
typedef void ( APIENTRY *PFNGLXENABLEPROC )( GLenum cap );
typedef void ( APIENTRY *PFNGLXDISABLEPROC )( GLenum cap );
typedef void ( APIENTRY *PFNGLXDEBUGMESSAGECALLBACKPROC )( GLXDebugProc callback, const void *userParam );
typedef void ( APIENTRY *PFNGLXDEBUGMESSAGECONTROLPROC )( GLenum source, GLenum type, GLenum severity,
	GLsizei count, const GLuint *ids, GLboolean enabled );
typedef void ( APIENTRY *PFNGLXOBJECTLABELPROC )( GLenum identifier, GLuint name, GLsizei length, const GLchar *label );
typedef void ( APIENTRY *PFNGLXPUSHDEBUGGROUPPROC )( GLenum source, GLuint id, GLsizei length, const GLchar *message );
typedef void ( APIENTRY *PFNGLXPOPDEBUGGROUPPROC )( void );

struct DebugFns {
	PFNGLXENABLEPROC Enable;
	PFNGLXDISABLEPROC Disable;
	PFNGLXDEBUGMESSAGECALLBACKPROC DebugMessageCallback;
	PFNGLXDEBUGMESSAGECONTROLPROC DebugMessageControl;
	PFNGLXOBJECTLABELPROC ObjectLabel;
	PFNGLXPUSHDEBUGGROUPPROC PushDebugGroup;
	PFNGLXPOPDEBUGGROUPPROC PopDebugGroup;
};

struct DebugState {
	cvar_t *r_glxDebug;
	cvar_t *r_glxDebugVerbose;
	cvar_t *r_glxDebugGroups;
	DebugFns fns;
	qboolean callbackInstalled;
	qboolean khrDebugOutput;
	unsigned int groupsPushed;
	int lastDebugModificationCount;
	int lastVerboseModificationCount;
};

void GLX_Debug_RegisterCvars( DebugState *state );
void GLX_Debug_OnOpenGLReady( DebugState *state, const Capabilities &caps );
void GLX_Debug_UpdateCvars( DebugState *state, const Capabilities &caps );
void GLX_Debug_Shutdown( DebugState *state );
qboolean GLX_Debug_CallbackInstalled( const DebugState &state );
qboolean GLX_Debug_Verbose( const DebugState &state );
void GLX_Debug_LabelObject( const DebugState &state, GLenum identifier, GLuint name, const char *label );
void GLX_Debug_PushGroup( DebugState *state, const char *label );
void GLX_Debug_PopGroup( DebugState *state );

} // namespace glx

#endif // GLX_DEBUG_H
