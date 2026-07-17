#ifndef GLX_MATERIAL_H
#define GLX_MATERIAL_H

#include "glx_caps.h"
#include "glx_material_key.h"

namespace glx {

static constexpr int GLX_MATERIAL_PROGRAM_LIMIT = 256;

typedef GLuint ( APIENTRY *PFNGLXCREATESHADERPROC )( GLenum type );
typedef void ( APIENTRY *PFNGLXSHADERSOURCEPROC )( GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length );
typedef void ( APIENTRY *PFNGLXCOMPILESHADERPROC )( GLuint shader );
typedef void ( APIENTRY *PFNGLXGETSHADERIVPROC )( GLuint shader, GLenum pname, GLint *params );
typedef void ( APIENTRY *PFNGLXGETSHADERINFOLOGPROC )( GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog );
typedef GLuint ( APIENTRY *PFNGLXCREATEPROGRAMPROC )( void );
typedef void ( APIENTRY *PFNGLXATTACHSHADERPROC )( GLuint program, GLuint shader );
typedef void ( APIENTRY *PFNGLXLINKPROGRAMPROC )( GLuint program );
typedef void ( APIENTRY *PFNGLXGETPROGRAMIVPROC )( GLuint program, GLenum pname, GLint *params );
typedef void ( APIENTRY *PFNGLXGETPROGRAMINFOLOGPROC )( GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog );
typedef void ( APIENTRY *PFNGLXUSEPROGRAMPROC )( GLuint program );
typedef GLint ( APIENTRY *PFNGLXGETUNIFORMLOCATIONPROC )( GLuint program, const GLchar *name );
typedef void ( APIENTRY *PFNGLXUNIFORM1IPROC )( GLint location, GLint v0 );
typedef void ( APIENTRY *PFNGLXMATERIALUNIFORM4FVPROC )( GLint location, GLsizei count, const GLfloat *value );
typedef void ( APIENTRY *PFNGLXDELETEPROGRAMPROC )( GLuint program );
typedef void ( APIENTRY *PFNGLXDELETESHADERPROC )( GLuint shader );
typedef void ( APIENTRY *PFNGLXMATERIALOBJECTLABELPROC )( GLenum identifier, GLuint name, GLsizei length, const GLchar *label );

struct MaterialFns {
	PFNGLXCREATESHADERPROC CreateShader;
	PFNGLXSHADERSOURCEPROC ShaderSource;
	PFNGLXCOMPILESHADERPROC CompileShader;
	PFNGLXGETSHADERIVPROC GetShaderiv;
	PFNGLXGETSHADERINFOLOGPROC GetShaderInfoLog;
	PFNGLXCREATEPROGRAMPROC CreateProgram;
	PFNGLXATTACHSHADERPROC AttachShader;
	PFNGLXLINKPROGRAMPROC LinkProgram;
	PFNGLXGETPROGRAMIVPROC GetProgramiv;
	PFNGLXGETPROGRAMINFOLOGPROC GetProgramInfoLog;
	PFNGLXUSEPROGRAMPROC UseProgram;
	PFNGLXGETUNIFORMLOCATIONPROC GetUniformLocation;
	PFNGLXUNIFORM1IPROC Uniform1i;
	PFNGLXMATERIALUNIFORM4FVPROC Uniform4fv;
	PFNGLXDELETEPROGRAMPROC DeleteProgram;
	PFNGLXDELETESHADERPROC DeleteShader;
	PFNGLXMATERIALOBJECTLABELPROC ObjectLabel;
};

struct MaterialProgram {
	MaterialStageKey stageKey;
	MaterialProgramKey key;
	GLuint program;
	GLuint vertexShader;
	GLuint fragmentShader;
	GLint texture0Uniform;
	GLint texture1Uniform;
	unsigned int binds;
	qboolean valid;
};

struct LiquidProgram {
	GLuint program;
	GLuint vertexShader;
	GLuint fragmentShader;
	GLint textureUniform;
	GLint depthTextureUniform;
	GLint paramsUniform;
	GLint eyeAndCountUniform;
	GLint targetInverseUniform;
	GLint reflectUniform;
	GLint impulsesUniform;
	GLint amplitudesUniform;
	unsigned int binds;
	qboolean valid;
};

struct MaterialRequest {
	int flags;
	unsigned int stateBits;
	int rgbGen;
	int alphaGen;
	int rgbWaveFunc;
	int alphaWaveFunc;
	int tcGen0;
	int tcGen1;
	int texMods0;
	int texMods1;
	unsigned int texModTypes0;
	unsigned int texModTypes1;
	unsigned int texModSequence0;
	unsigned int texModSequence1;
	unsigned int texModWaveFuncs0;
	unsigned int texModWaveFuncs1;
	int fogAdjust;
	int materialCombine;
	qboolean fogPass;
};

struct MaterialState {
	cvar_t *r_glxMaterialRenderer;
	cvar_t *r_glxMaterialDebug;
	cvar_t *r_glxMaterialPrecache;
	MaterialFns fns;
	MaterialProgram programs[GLX_MATERIAL_PROGRAM_LIMIT];
	LiquidProgram liquidProgram;
	qboolean liquidProgramAttempted;
	RenderProductTier tier;
	int programCount;
	int lastFoundProgram;
	GLuint currentProgram;
	qboolean ready;
	char reason[128];
	char glslVersion[64];
	unsigned int frames;
	unsigned int bindAttempts;
	unsigned int binds;
	unsigned int programSwitches;
	unsigned int unbinds;
	unsigned int cacheHits;
	unsigned int cacheMisses;
	unsigned int compileAttempts;
	unsigned int compileFailures;
	unsigned int linkFailures;
	unsigned int precacheAttempts;
	unsigned int precacheFailures;
	unsigned int bindFailures;
	unsigned int debugLabels;
	unsigned int contextlessDeletes;
	unsigned int compiledMaterialPlans;
	unsigned int unsupportedMaterialPlans;
	unsigned int parameterBlocks;
	unsigned int invalidParameterBlocks;
	unsigned int unsupportedRequests;
	unsigned int disabledSkips;
	unsigned int notReadySkips;
	unsigned int programLimitSkips;
	MaterialRequest lastRequest;
	MaterialIR lastMaterial;
	MaterialParameterBlock lastParameterBlock;
	unsigned int lastParameterBlockHash;
	MaterialProgramKey lastKey;
	MaterialStageKey lastStageKey;
	unsigned int lastUnsupportedReasons;
	char lastError[256];
};

void GLX_Material_RegisterCvars( MaterialState *state );
void GLX_Material_OnOpenGLReady( MaterialState *state, const Capabilities &caps );
void GLX_Material_Shutdown( MaterialState *state, qboolean deletePrograms );
void GLX_Material_FrameComplete( MaterialState *state );
qboolean GLX_Material_Active( const MaterialState &state );
qboolean GLX_Material_BindIR( MaterialState *state, const MaterialIR &material );
qboolean GLX_Material_BindStage( MaterialState *state, const MaterialRequest &request );
qboolean GLX_Material_BindFog( MaterialState *state );
qboolean GLX_Material_BindLiquid( MaterialState *state, const float *params,
	const float *eyeAndCount, const float *targetInverse, const float *reflect,
	const float *impulses, const float *amplitudes );
void GLX_Material_Unbind( MaterialState *state );
void GLX_Material_PrintInfo( const MaterialState &state );
const char *GLX_Material_ModeName( MaterialProgramMode mode );

} // namespace glx

#endif // GLX_MATERIAL_H
