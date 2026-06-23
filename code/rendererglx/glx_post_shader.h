#ifndef GLX_POST_SHADER_H
#define GLX_POST_SHADER_H

#include "glx_caps.h"
#include "glx_post_shader_source.h"

namespace glx {

static constexpr int GLX_POST_SHADER_PROGRAM_LIMIT = 32;

typedef GLuint ( APIENTRY *PFNGLXPOSTCREATESHADERPROC )( GLenum type );
typedef void ( APIENTRY *PFNGLXPOSTSHADERSOURCEPROC )( GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length );
typedef void ( APIENTRY *PFNGLXPOSTCOMPILESHADERPROC )( GLuint shader );
typedef void ( APIENTRY *PFNGLXPOSTGETSHADERIVPROC )( GLuint shader, GLenum pname, GLint *params );
typedef void ( APIENTRY *PFNGLXPOSTGETSHADERINFOLOGPROC )( GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog );
typedef GLuint ( APIENTRY *PFNGLXPOSTCREATEPROGRAMPROC )( void );
typedef void ( APIENTRY *PFNGLXPOSTATTACHSHADERPROC )( GLuint program, GLuint shader );
typedef void ( APIENTRY *PFNGLXPOSTLINKPROGRAMPROC )( GLuint program );
typedef void ( APIENTRY *PFNGLXPOSTGETPROGRAMIVPROC )( GLuint program, GLenum pname, GLint *params );
typedef void ( APIENTRY *PFNGLXPOSTGETPROGRAMINFOLOGPROC )( GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog );
typedef void ( APIENTRY *PFNGLXPOSTUSEPROGRAMPROC )( GLuint program );
typedef GLint ( APIENTRY *PFNGLXPOSTGETUNIFORMLOCATIONPROC )( GLuint program, const GLchar *name );
typedef void ( APIENTRY *PFNGLXPOSTUNIFORM1IPROC )( GLint location, GLint v0 );
typedef void ( APIENTRY *PFNGLXPOSTUNIFORM4FPROC )( GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3 );
typedef void ( APIENTRY *PFNGLXPOSTDELETEPROGRAMPROC )( GLuint program );
typedef void ( APIENTRY *PFNGLXPOSTDELETESHADERPROC )( GLuint shader );
typedef void ( APIENTRY *PFNGLXPOSTOBJECTLABELPROC )( GLenum identifier, GLuint name, GLsizei length, const GLchar *label );

struct PostShaderFns {
	PFNGLXPOSTCREATESHADERPROC CreateShader;
	PFNGLXPOSTSHADERSOURCEPROC ShaderSource;
	PFNGLXPOSTCOMPILESHADERPROC CompileShader;
	PFNGLXPOSTGETSHADERIVPROC GetShaderiv;
	PFNGLXPOSTGETSHADERINFOLOGPROC GetShaderInfoLog;
	PFNGLXPOSTCREATEPROGRAMPROC CreateProgram;
	PFNGLXPOSTATTACHSHADERPROC AttachShader;
	PFNGLXPOSTLINKPROGRAMPROC LinkProgram;
	PFNGLXPOSTGETPROGRAMIVPROC GetProgramiv;
	PFNGLXPOSTGETPROGRAMINFOLOGPROC GetProgramInfoLog;
	PFNGLXPOSTUSEPROGRAMPROC UseProgram;
	PFNGLXPOSTGETUNIFORMLOCATIONPROC GetUniformLocation;
	PFNGLXPOSTUNIFORM1IPROC Uniform1i;
	PFNGLXPOSTUNIFORM4FPROC Uniform4f;
	PFNGLXPOSTDELETEPROGRAMPROC DeleteProgram;
	PFNGLXPOSTDELETESHADERPROC DeleteShader;
	PFNGLXPOSTOBJECTLABELPROC ObjectLabel;
};

struct PostShaderProgram {
	PostShaderPlan plan;
	PostShaderSourceSummary source;
	GLuint program;
	GLuint vertexShader;
	GLuint fragmentShader;
	GLint sceneUniform;
	GLint bloomUniform;
	GLint lutUniform;
	GLint postParams0Uniform;
	GLint outputParams1Uniform;
	GLint legacyParamsUniform;
	GLint bloomParamsUniform;
	GLint crtParams0Uniform;
	GLint crtParams1Uniform;
	GLint liftUniform;
	GLint invGammaUniform;
	GLint gainUniform;
	GLint whitePoint0Uniform;
	GLint whitePoint1Uniform;
	GLint whitePoint2Uniform;
	GLint lutParamsUniform;
	unsigned int uses;
	unsigned int lastUseSerial;
	qboolean valid;
};

struct PostShaderState {
	cvar_t *r_glxPostShaderCache;
	cvar_t *r_glxPostShaderPrecache;
	cvar_t *r_glxPostShaderDebug;
	cvar_t *r_glxPostShaderExecute;
	cvar_t *r_glxPostShaderTarget;
	PostShaderFns fns;
	PostShaderProgram programs[GLX_POST_SHADER_PROGRAM_LIMIT];
	RenderProductTier tier;
	int glMajor;
	int glMinor;
	int programCount;
	GLuint currentProgram;
	qboolean ready;
	char reason[128];
	char lastError[256];
	unsigned int frames;
	unsigned int plansObserved;
	unsigned int validPlansObserved;
	unsigned int invalidPlansObserved;
	unsigned int cacheHits;
	unsigned int cacheMisses;
	unsigned int compileAttempts;
	unsigned int compileFailures;
	unsigned int linkFailures;
	unsigned int sourceFailures;
	unsigned int programLimitSkips;
	unsigned int cacheEvictions;
	unsigned int targetFallbacks;
	unsigned int targetCapabilityFallbacks;
	unsigned int precacheAttempts;
	unsigned int precacheFailures;
	unsigned int debugLabels;
	unsigned int contextlessDeletes;
	unsigned int directFinalCandidates;
	unsigned int directFinalEligibleFrames;
	unsigned int directFinalAttempts;
	unsigned int directFinalBinds;
	unsigned int directFinalFallbacks;
	unsigned int directFinalRejects;
	unsigned int directFinalProgramMisses;
	unsigned int directFinalUniformFailures;
	unsigned int lastPlanHash;
	unsigned int lastFeatureMask;
	unsigned int lastSourceHash;
	unsigned int lastProgram;
	unsigned int lastEvictedSourceHash;
	unsigned int lastEvictedFeatureMask;
	unsigned int lastEvictedUses;
	unsigned int lastDirectFinalRejectMask;
	unsigned int useSerial;
	PostShaderSourceTarget preferredTarget;
	PostShaderSourceTarget activeTarget;
	PostShaderSourceTarget lastRequestedTarget;
	PostShaderSourceTarget lastCompileTarget;
	PostShaderSourceTarget lastFallbackFromTarget;
	PostShaderSourceTarget lastFallbackToTarget;
	PostShaderSourceTarget lastEvictedTarget;
	PostShaderSourceSummary lastSource;
	qboolean lastPlanValid;
	qboolean lastDirectFinalEligible;
	qboolean lastDirectFinalBound;
	qboolean lastTargetFallbackUsed;
	qboolean lastTargetUnsupported;
	qboolean modernTargetSuppressed;
};

void GLX_PostShader_RegisterCvars( PostShaderState *state );
void GLX_PostShader_OnOpenGLReady( PostShaderState *state, const Capabilities &caps );
void GLX_PostShader_Shutdown( PostShaderState *state, qboolean deletePrograms );
void GLX_PostShader_FrameComplete( PostShaderState *state );
qboolean GLX_PostShader_Ready( const PostShaderState &state );
qboolean GLX_PostShader_ExecutionEnabled( const PostShaderState &state );
qboolean GLX_PostShader_RecordPlan( PostShaderState *state, const PostShaderPlan &plan );
qboolean GLX_PostShader_TryBindFinal( PostShaderState *state,
	const PostShaderPlan &plan, const OutputTransform &output,
	qboolean bloomComposite, qboolean outputTransform, float bloomIntensity );
qboolean GLX_PostShader_TryBindDirectFinal( PostShaderState *state,
	const PostShaderPlan &plan, const OutputTransform &output, float greyscale );
void GLX_PostShader_Unbind( PostShaderState *state );
void GLX_PostShader_PrintInfo( const PostShaderState &state );

} // namespace glx

#endif // GLX_POST_SHADER_H
