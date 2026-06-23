#include "glx_post_shader.h"
#include "glx_color_math.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_PROGRAM
#define GL_PROGRAM 0x82E2
#endif

namespace glx {

static void GLX_PostShader_SetReason( PostShaderState *state, const char *reason )
{
	if ( !state ) {
		return;
	}

	std::snprintf( state->reason, sizeof( state->reason ), "%s", reason ? reason : "" );
	state->reason[sizeof( state->reason ) - 1] = '\0';
}

static void GLX_PostShader_SetLastError( PostShaderState *state, const char *error )
{
	if ( !state ) {
		return;
	}

	std::snprintf( state->lastError, sizeof( state->lastError ), "%s", error ? error : "" );
	state->lastError[sizeof( state->lastError ) - 1] = '\0';
}

static void *GLX_PostShader_GetProc( const char *name )
{
	return RI().GL_GetProcAddress ? RI().GL_GetProcAddress( name ) : nullptr;
}

static int GLX_PostShader_Stricmp( const char *lhs, const char *rhs )
{
	if ( !lhs ) {
		lhs = "";
	}
	if ( !rhs ) {
		rhs = "";
	}

	while ( *lhs || *rhs ) {
		const int l = std::tolower( static_cast<unsigned char>( *lhs ) );
		const int r = std::tolower( static_cast<unsigned char>( *rhs ) );
		if ( l != r ) {
			return l - r;
		}
		if ( *lhs ) {
			lhs++;
		}
		if ( *rhs ) {
			rhs++;
		}
	}

	return 0;
}

static PostShaderSourceTarget GLX_PostShader_ParseTargetCvar( const cvar_t *cvar,
	qboolean *explicitTarget )
{
	const char *value = ( cvar && cvar->string ) ? cvar->string : "auto";

	if ( explicitTarget ) {
		*explicitTarget = qfalse;
	}

	if ( !value[0] || !GLX_PostShader_Stricmp( value, "auto" ) ||
		!GLX_PostShader_Stricmp( value, "0" ) ) {
		return PostShaderSourceTarget::Glsl120;
	}
	if ( explicitTarget ) {
		*explicitTarget = qtrue;
	}
	if ( !GLX_PostShader_Stricmp( value, "120" ) ||
		!GLX_PostShader_Stricmp( value, "glsl120" ) ||
		!GLX_PostShader_Stricmp( value, "glsl-120" ) ) {
		return PostShaderSourceTarget::Glsl120;
	}
	if ( !GLX_PostShader_Stricmp( value, "130" ) ||
		!GLX_PostShader_Stricmp( value, "glsl130" ) ||
		!GLX_PostShader_Stricmp( value, "glsl-130" ) ) {
		return PostShaderSourceTarget::Glsl130;
	}
	if ( !GLX_PostShader_Stricmp( value, "150" ) ||
		!GLX_PostShader_Stricmp( value, "glsl150" ) ||
		!GLX_PostShader_Stricmp( value, "glsl150-compat" ) ||
		!GLX_PostShader_Stricmp( value, "glsl-150-compat" ) ) {
		return PostShaderSourceTarget::Glsl150Compatibility;
	}
	if ( !GLX_PostShader_Stricmp( value, "330" ) ||
		!GLX_PostShader_Stricmp( value, "glsl330" ) ||
		!GLX_PostShader_Stricmp( value, "glsl330-compat" ) ||
		!GLX_PostShader_Stricmp( value, "glsl-330-compat" ) ) {
		return PostShaderSourceTarget::Glsl330Compatibility;
	}
	if ( !GLX_PostShader_Stricmp( value, "410" ) ||
		!GLX_PostShader_Stricmp( value, "glsl410" ) ||
		!GLX_PostShader_Stricmp( value, "glsl410-compat" ) ||
		!GLX_PostShader_Stricmp( value, "glsl-410-compat" ) ) {
		return PostShaderSourceTarget::Glsl410Compatibility;
	}

	if ( explicitTarget ) {
		*explicitTarget = qfalse;
	}
	return PostShaderSourceTarget::Glsl120;
}

static void GLX_PostShader_LoadFunctions( PostShaderState *state )
{
	if ( !state ) {
		return;
	}

	state->fns.CreateShader = reinterpret_cast<PFNGLXPOSTCREATESHADERPROC>(
		GLX_PostShader_GetProc( "glCreateShader" ) );
	state->fns.ShaderSource = reinterpret_cast<PFNGLXPOSTSHADERSOURCEPROC>(
		GLX_PostShader_GetProc( "glShaderSource" ) );
	state->fns.CompileShader = reinterpret_cast<PFNGLXPOSTCOMPILESHADERPROC>(
		GLX_PostShader_GetProc( "glCompileShader" ) );
	state->fns.GetShaderiv = reinterpret_cast<PFNGLXPOSTGETSHADERIVPROC>(
		GLX_PostShader_GetProc( "glGetShaderiv" ) );
	state->fns.GetShaderInfoLog = reinterpret_cast<PFNGLXPOSTGETSHADERINFOLOGPROC>(
		GLX_PostShader_GetProc( "glGetShaderInfoLog" ) );
	state->fns.CreateProgram = reinterpret_cast<PFNGLXPOSTCREATEPROGRAMPROC>(
		GLX_PostShader_GetProc( "glCreateProgram" ) );
	state->fns.AttachShader = reinterpret_cast<PFNGLXPOSTATTACHSHADERPROC>(
		GLX_PostShader_GetProc( "glAttachShader" ) );
	state->fns.LinkProgram = reinterpret_cast<PFNGLXPOSTLINKPROGRAMPROC>(
		GLX_PostShader_GetProc( "glLinkProgram" ) );
	state->fns.GetProgramiv = reinterpret_cast<PFNGLXPOSTGETPROGRAMIVPROC>(
		GLX_PostShader_GetProc( "glGetProgramiv" ) );
	state->fns.GetProgramInfoLog = reinterpret_cast<PFNGLXPOSTGETPROGRAMINFOLOGPROC>(
		GLX_PostShader_GetProc( "glGetProgramInfoLog" ) );
	state->fns.UseProgram = reinterpret_cast<PFNGLXPOSTUSEPROGRAMPROC>(
		GLX_PostShader_GetProc( "glUseProgram" ) );
	state->fns.GetUniformLocation = reinterpret_cast<PFNGLXPOSTGETUNIFORMLOCATIONPROC>(
		GLX_PostShader_GetProc( "glGetUniformLocation" ) );
	state->fns.Uniform1i = reinterpret_cast<PFNGLXPOSTUNIFORM1IPROC>(
		GLX_PostShader_GetProc( "glUniform1i" ) );
	state->fns.Uniform4f = reinterpret_cast<PFNGLXPOSTUNIFORM4FPROC>(
		GLX_PostShader_GetProc( "glUniform4f" ) );
	state->fns.DeleteProgram = reinterpret_cast<PFNGLXPOSTDELETEPROGRAMPROC>(
		GLX_PostShader_GetProc( "glDeleteProgram" ) );
	state->fns.DeleteShader = reinterpret_cast<PFNGLXPOSTDELETESHADERPROC>(
		GLX_PostShader_GetProc( "glDeleteShader" ) );
	state->fns.ObjectLabel = reinterpret_cast<PFNGLXPOSTOBJECTLABELPROC>(
		GLX_PostShader_GetProc( "glObjectLabel" ) );
}

static qboolean GLX_PostShader_FunctionsReady( const PostShaderState &state )
{
	return state.fns.CreateShader &&
		state.fns.ShaderSource &&
		state.fns.CompileShader &&
		state.fns.GetShaderiv &&
		state.fns.GetShaderInfoLog &&
		state.fns.CreateProgram &&
		state.fns.AttachShader &&
		state.fns.LinkProgram &&
		state.fns.GetProgramiv &&
		state.fns.GetProgramInfoLog &&
		state.fns.UseProgram &&
		state.fns.GetUniformLocation &&
		state.fns.Uniform1i &&
		state.fns.Uniform4f &&
		state.fns.DeleteProgram &&
		state.fns.DeleteShader ? qtrue : qfalse;
}

static void GLX_PostShader_ResetCounters( PostShaderState *state )
{
	if ( !state ) {
		return;
	}

	state->frames = 0;
	state->plansObserved = 0;
	state->validPlansObserved = 0;
	state->invalidPlansObserved = 0;
	state->cacheHits = 0;
	state->cacheMisses = 0;
	state->compileAttempts = 0;
	state->compileFailures = 0;
	state->linkFailures = 0;
	state->sourceFailures = 0;
	state->programLimitSkips = 0;
	state->cacheEvictions = 0;
	state->targetFallbacks = 0;
	state->targetCapabilityFallbacks = 0;
	state->precacheAttempts = 0;
	state->precacheFailures = 0;
	state->debugLabels = 0;
	state->contextlessDeletes = 0;
	state->directFinalCandidates = 0;
	state->directFinalEligibleFrames = 0;
	state->directFinalAttempts = 0;
	state->directFinalBinds = 0;
	state->directFinalFallbacks = 0;
	state->directFinalRejects = 0;
	state->directFinalProgramMisses = 0;
	state->directFinalUniformFailures = 0;
	state->lastPlanHash = 0;
	state->lastFeatureMask = 0;
	state->lastSourceHash = 0;
	state->lastProgram = 0;
	state->lastEvictedSourceHash = 0;
	state->lastEvictedFeatureMask = 0;
	state->lastEvictedUses = 0;
	state->lastDirectFinalRejectMask = GLX_POST_SHADER_DIRECT_REJECT_NONE;
	state->useSerial = 0;
	state->preferredTarget = state->activeTarget;
	state->lastRequestedTarget = state->activeTarget;
	state->lastCompileTarget = state->activeTarget;
	state->lastFallbackFromTarget = state->activeTarget;
	state->lastFallbackToTarget = state->activeTarget;
	state->lastEvictedTarget = state->activeTarget;
	state->lastSource = {};
	state->lastPlanValid = qfalse;
	state->lastDirectFinalEligible = qfalse;
	state->lastDirectFinalBound = qfalse;
	state->lastTargetFallbackUsed = qfalse;
	state->lastTargetUnsupported = qfalse;
	state->modernTargetSuppressed = qfalse;
	GLX_PostShader_SetLastError( state, "" );
}

static void GLX_PostShader_PrintObjectLog( const PostShaderState &state, GLuint object,
	qboolean program, printParm_t printLevel )
{
	GLint length = 0;
	GLsizei written = 0;
	char smallLog[1024];
	char *log = smallLog;

	if ( program ) {
		state.fns.GetProgramiv( object, GL_INFO_LOG_LENGTH, &length );
	} else {
		state.fns.GetShaderiv( object, GL_INFO_LOG_LENGTH, &length );
	}

	if ( length <= 1 ) {
		return;
	}

	if ( length > static_cast<GLint>( sizeof( smallLog ) ) ) {
		log = static_cast<char *>( RI().Malloc( static_cast<size_t>( length ) ) );
		if ( !log ) {
			return;
		}
	}

	if ( program ) {
		state.fns.GetProgramInfoLog( object, length, &written, log );
	} else {
		state.fns.GetShaderInfoLog( object, length, &written, log );
	}
	log[length - 1] = '\0';
	RI().Printf( printLevel, "%s\n", log );

	if ( log != smallLog ) {
		RI().Free( log );
	}
}

static GLuint GLX_PostShader_CompileShader( PostShaderState *state, GLenum shaderType,
	const char *source, PostShaderSourceTarget target, unsigned int featureMask )
{
	GLuint shader;
	GLint ok = 0;
	const GLchar *sources[1];

	if ( !state || !source ) {
		return 0;
	}

	shader = state->fns.CreateShader( shaderType );
	if ( !shader ) {
		GLX_PostShader_SetLastError( state, "glCreateShader returned 0" );
		return 0;
	}

	sources[0] = source;
	state->fns.ShaderSource( shader, 1, sources, nullptr );
	state->fns.CompileShader( shader );
	state->fns.GetShaderiv( shader, GL_COMPILE_STATUS, &ok );

	if ( !ok ) {
		state->compileFailures++;
		std::snprintf( state->lastError, sizeof( state->lastError ),
			"%s %s shader compile failed for features 0x%08x",
			GLX_PostShaderSource_TargetName( target ),
			shaderType == GL_VERTEX_SHADER ? "vertex" : "fragment",
			featureMask );
		state->lastError[sizeof( state->lastError ) - 1] = '\0';
		RI().Printf( PRINT_WARNING,
			"GLx post %s %s shader compile failed for features 0x%08x:\n",
			GLX_PostShaderSource_TargetName( target ),
			shaderType == GL_VERTEX_SHADER ? "vertex" : "fragment",
			featureMask );
		GLX_PostShader_PrintObjectLog( *state, shader, qfalse, PRINT_WARNING );
		if ( state->r_glxPostShaderDebug && state->r_glxPostShaderDebug->integer > 1 ) {
			RI().Printf( PRINT_ALL, "%s\n", source );
		}
		state->fns.DeleteShader( shader );
		return 0;
	}

	return shader;
}

static void GLX_PostShader_DeleteProgram( PostShaderState *state, PostShaderProgram *program )
{
	if ( !state || !program ) {
		return;
	}

	if ( program->program && state->currentProgram == program->program && state->fns.UseProgram ) {
		state->fns.UseProgram( 0 );
		state->currentProgram = 0;
	}
	if ( program->program && state->fns.DeleteProgram ) {
		state->fns.DeleteProgram( program->program );
	}
	if ( program->vertexShader && state->fns.DeleteShader ) {
		state->fns.DeleteShader( program->vertexShader );
	}
	if ( program->fragmentShader && state->fns.DeleteShader ) {
		state->fns.DeleteShader( program->fragmentShader );
	}

	*program = {};
}

static void GLX_PostShader_ResetRuntime( PostShaderState *state, qboolean deletePrograms )
{
	qboolean canDeletePrograms;

	if ( !state ) {
		return;
	}

	canDeletePrograms = deletePrograms && state->fns.DeleteProgram &&
		state->fns.DeleteShader ? qtrue : qfalse;

	if ( state->programCount > 0 && !canDeletePrograms ) {
		state->contextlessDeletes += static_cast<unsigned int>( state->programCount );
	}

	if ( canDeletePrograms ) {
		for ( int i = 0; i < state->programCount; i++ ) {
			GLX_PostShader_DeleteProgram( state, &state->programs[i] );
		}
	} else {
		state->currentProgram = 0;
		for ( int i = 0; i < state->programCount; i++ ) {
			state->programs[i] = {};
		}
	}

	for ( int i = state->programCount; i < GLX_POST_SHADER_PROGRAM_LIMIT; i++ ) {
		state->programs[i] = {};
	}

	state->fns = {};
	state->programCount = 0;
	state->currentProgram = 0;
	state->ready = qfalse;
	GLX_PostShader_SetReason( state, "not initialized" );
}

static PostShaderSourceTarget GLX_PostShader_ResolveTarget( PostShaderState *state )
{
	qboolean explicitTarget = qfalse;
	PostShaderSourceTarget target;

	if ( !state ) {
		return PostShaderSourceTarget::Glsl120;
	}

	target = GLX_PostShader_ParseTargetCvar( state->r_glxPostShaderTarget,
		&explicitTarget );
	if ( !explicitTarget ) {
		target = GLX_PostShaderSource_TargetForTier( state->tier, state->glMajor,
			state->glMinor );
		state->preferredTarget = target;
		if ( state->modernTargetSuppressed &&
			GLX_PostShaderSource_ModernTarget( target ) ) {
			target = PostShaderSourceTarget::Glsl120;
		}
	} else {
		state->preferredTarget = target;
	}
	state->lastRequestedTarget = target;
	state->lastTargetUnsupported = qfalse;

	if ( !GLX_PostShaderSource_TargetSupportedByVersion( target,
		state->glMajor, state->glMinor ) ) {
		state->targetCapabilityFallbacks++;
		state->lastTargetUnsupported = qtrue;
		target = PostShaderSourceTarget::Glsl120;
	}

	return target;
}

static PostShaderSourceSummary GLX_PostShader_BuildSourceSummary(
	PostShaderState *state, const PostShaderPlan &plan )
{
	PostShaderSourceTarget target = GLX_PostShader_ResolveTarget( state );
	PostShaderSourceSummary source = GLX_PostShaderSource_BuildSummary( plan, target );

	if ( state ) {
		state->activeTarget = target;
		state->lastSource = source;
		state->lastSourceHash = source.sourceHash;
	}
	return source;
}

static void GLX_PostShader_TouchProgram( PostShaderState *state,
	PostShaderProgram *program )
{
	if ( !state || !program ) {
		return;
	}
	state->useSerial++;
	if ( state->useSerial == 0 ) {
		state->useSerial = 1;
	}
	program->lastUseSerial = state->useSerial;
}

static PostShaderProgram *GLX_PostShader_AllocateProgramSlot( PostShaderState *state,
	qboolean *appendSlot )
{
	int victim = -1;
	unsigned int oldestSerial = 0xffffffffu;

	if ( appendSlot ) {
		*appendSlot = qfalse;
	}
	if ( !state ) {
		return nullptr;
	}

	for ( int i = 0; i < state->programCount; i++ ) {
		if ( !state->programs[i].valid ) {
			return &state->programs[i];
		}
	}

	if ( state->programCount < GLX_POST_SHADER_PROGRAM_LIMIT ) {
		if ( appendSlot ) {
			*appendSlot = qtrue;
		}
		return &state->programs[state->programCount];
	}

	for ( int i = 0; i < state->programCount; i++ ) {
		const PostShaderProgram &program = state->programs[i];
		if ( state->currentProgram && program.program == state->currentProgram ) {
			continue;
		}
		if ( victim < 0 || program.lastUseSerial < oldestSerial ) {
			victim = i;
			oldestSerial = program.lastUseSerial;
		}
	}
	if ( victim < 0 && state->programCount > 0 ) {
		victim = 0;
	}
	if ( victim < 0 ) {
		state->programLimitSkips++;
		GLX_PostShader_SetLastError( state, "post shader program cache has no evictable slot" );
		return nullptr;
	}

	state->lastEvictedSourceHash = state->programs[victim].source.sourceHash;
	state->lastEvictedFeatureMask = state->programs[victim].plan.featureMask;
	state->lastEvictedUses = state->programs[victim].uses;
	state->lastEvictedTarget = state->programs[victim].source.target;
	if ( state->r_glxPostShaderDebug && state->r_glxPostShaderDebug->integer ) {
		RI().Printf( PRINT_ALL,
			"GLx post shader evicting %s source 0x%08x features 0x%08x uses %u.\n",
			GLX_PostShaderSource_TargetName( state->lastEvictedTarget ),
			state->lastEvictedSourceHash, state->lastEvictedFeatureMask,
			state->lastEvictedUses );
	}
	GLX_PostShader_DeleteProgram( state, &state->programs[victim] );
	state->cacheEvictions++;
	return &state->programs[victim];
}

static qboolean GLX_PostShader_SameProgramShape( const PostShaderProgram &program,
	const PostShaderPlan &plan, const PostShaderSourceSummary &source )
{
	return program.valid &&
		program.plan.hash == plan.hash &&
		program.plan.featureMask == plan.featureMask &&
		program.source.target == source.target &&
		program.source.sourceHash == source.sourceHash ? qtrue : qfalse;
}

static PostShaderProgram *GLX_PostShader_FindProgram( PostShaderState *state,
	const PostShaderPlan &plan, const PostShaderSourceSummary &source )
{
	if ( !state ) {
		return nullptr;
	}

	for ( int i = 0; i < state->programCount; i++ ) {
		if ( GLX_PostShader_SameProgramShape( state->programs[i], plan, source ) ) {
			state->cacheHits++;
			GLX_PostShader_TouchProgram( state, &state->programs[i] );
			return &state->programs[i];
		}
	}

	state->cacheMisses++;
	return nullptr;
}

static PostShaderProgram *GLX_PostShader_FindProgramForUse( PostShaderState *state,
	const PostShaderPlan &plan, const PostShaderSourceSummary &source )
{
	if ( !state ) {
		return nullptr;
	}

	for ( int i = 0; i < state->programCount; i++ ) {
		if ( GLX_PostShader_SameProgramShape( state->programs[i], plan, source ) ) {
			GLX_PostShader_TouchProgram( state, &state->programs[i] );
			return &state->programs[i];
		}
	}

	return nullptr;
}

static void GLX_PostShader_LabelProgram( PostShaderState *state, PostShaderProgram *program )
{
	char label[160];

	if ( !state || !program || !program->program || !state->fns.ObjectLabel ) {
		return;
	}

	std::snprintf( label, sizeof( label ), "GLx post shader %s 0x%08x features 0x%08x",
		GLX_PostShaderSource_TargetName( program->source.target ),
		program->source.sourceHash, program->plan.featureMask );
	label[sizeof( label ) - 1] = '\0';
	state->fns.ObjectLabel( GL_PROGRAM, program->program, static_cast<GLsizei>( -1 ), label );
	state->debugLabels++;
}

static PostShaderProgram *GLX_PostShader_CreateProgram( PostShaderState *state,
	const PostShaderPlan &plan, const PostShaderSourceSummary &source )
{
	char vertexSource[GLX_POST_SHADER_VERTEX_SOURCE_BYTES];
	char fragmentSource[GLX_POST_SHADER_FRAGMENT_SOURCE_BYTES];
	PostShaderProgram *program;
	GLint ok = 0;
	int ignoredBytes = 0;
	qboolean appendSlot = qfalse;

	if ( !state ) {
		return nullptr;
	}

	if ( !plan.valid || !source.valid ) {
		state->sourceFailures++;
		GLX_PostShader_SetLastError( state, "post shader source plan is invalid" );
		return nullptr;
	}

	if ( !GLX_PostShaderSource_WriteVertex( source.target, vertexSource,
		sizeof( vertexSource ), &ignoredBytes ) ||
		!GLX_PostShaderSource_WriteFragment( plan, source.target, fragmentSource,
		sizeof( fragmentSource ), &ignoredBytes ) ) {
		state->sourceFailures++;
		GLX_PostShader_SetLastError( state, "post shader source exceeded generator buffer" );
		return nullptr;
	}

	program = GLX_PostShader_AllocateProgramSlot( state, &appendSlot );
	if ( !program ) {
		return nullptr;
	}

	state->compileAttempts++;
	*program = {};
	program->plan = plan;
	program->source = source;
	program->sceneUniform = -1;
	program->bloomUniform = -1;
	program->lutUniform = -1;
	program->postParams0Uniform = -1;
	program->outputParams1Uniform = -1;
	program->legacyParamsUniform = -1;
	program->bloomParamsUniform = -1;
	program->crtParams0Uniform = -1;
	program->crtParams1Uniform = -1;
	program->liftUniform = -1;
	program->invGammaUniform = -1;
	program->gainUniform = -1;
	program->whitePoint0Uniform = -1;
	program->whitePoint1Uniform = -1;
	program->whitePoint2Uniform = -1;
	program->lutParamsUniform = -1;

	program->vertexShader = GLX_PostShader_CompileShader( state, GL_VERTEX_SHADER,
		vertexSource, source.target, plan.featureMask );
	program->fragmentShader = GLX_PostShader_CompileShader( state, GL_FRAGMENT_SHADER,
		fragmentSource, source.target, plan.featureMask );
	if ( !program->vertexShader || !program->fragmentShader ) {
		GLX_PostShader_DeleteProgram( state, program );
		return nullptr;
	}

	program->program = state->fns.CreateProgram();
	if ( !program->program ) {
		GLX_PostShader_SetLastError( state, "glCreateProgram returned 0" );
		GLX_PostShader_DeleteProgram( state, program );
		return nullptr;
	}

	state->fns.AttachShader( program->program, program->vertexShader );
	state->fns.AttachShader( program->program, program->fragmentShader );
	state->fns.LinkProgram( program->program );
	state->fns.GetProgramiv( program->program, GL_LINK_STATUS, &ok );

	if ( !ok ) {
		state->linkFailures++;
		std::snprintf( state->lastError, sizeof( state->lastError ),
			"%s program link failed for source 0x%08x features 0x%08x",
			GLX_PostShaderSource_TargetName( source.target ), source.sourceHash,
			plan.featureMask );
		state->lastError[sizeof( state->lastError ) - 1] = '\0';
		RI().Printf( PRINT_WARNING,
			"GLx post shader %s program link failed for source 0x%08x features 0x%08x:\n",
			GLX_PostShaderSource_TargetName( source.target ), source.sourceHash,
			plan.featureMask );
		GLX_PostShader_PrintObjectLog( *state, program->program, qtrue, PRINT_WARNING );
		GLX_PostShader_DeleteProgram( state, program );
		return nullptr;
	}

	program->sceneUniform = state->fns.GetUniformLocation( program->program, "u_Scene" );
	program->bloomUniform = state->fns.GetUniformLocation( program->program, "u_Bloom" );
	program->lutUniform = state->fns.GetUniformLocation( program->program, "u_ColorGradeLut" );
	program->postParams0Uniform = state->fns.GetUniformLocation( program->program, "u_PostParams0" );
	program->outputParams1Uniform = state->fns.GetUniformLocation( program->program, "u_OutputParams1" );
	program->legacyParamsUniform = state->fns.GetUniformLocation( program->program, "u_LegacyParams" );
	program->bloomParamsUniform = state->fns.GetUniformLocation( program->program, "u_BloomParams" );
	program->crtParams0Uniform = state->fns.GetUniformLocation( program->program, "u_CrtParams0" );
	program->crtParams1Uniform = state->fns.GetUniformLocation( program->program, "u_CrtParams1" );
	program->liftUniform = state->fns.GetUniformLocation( program->program, "u_Lift" );
	program->invGammaUniform = state->fns.GetUniformLocation( program->program, "u_InvGamma" );
	program->gainUniform = state->fns.GetUniformLocation( program->program, "u_Gain" );
	program->whitePoint0Uniform = state->fns.GetUniformLocation( program->program, "u_WhitePoint0" );
	program->whitePoint1Uniform = state->fns.GetUniformLocation( program->program, "u_WhitePoint1" );
	program->whitePoint2Uniform = state->fns.GetUniformLocation( program->program, "u_WhitePoint2" );
	program->lutParamsUniform = state->fns.GetUniformLocation( program->program, "u_LutParams" );
	state->fns.UseProgram( program->program );
	if ( program->sceneUniform >= 0 ) {
		state->fns.Uniform1i( program->sceneUniform, 0 );
	}
	if ( program->bloomUniform >= 0 ) {
		state->fns.Uniform1i( program->bloomUniform, 1 );
	}
	if ( program->lutUniform >= 0 ) {
		state->fns.Uniform1i( program->lutUniform, 2 );
	}
	state->fns.UseProgram( 0 );
	state->currentProgram = 0;

	program->valid = qtrue;
	GLX_PostShader_TouchProgram( state, program );
	GLX_PostShader_LabelProgram( state, program );
	if ( appendSlot ) {
		state->programCount++;
	}
	state->lastProgram = program->program;
	state->lastCompileTarget = source.target;
	GLX_PostShader_SetLastError( state, "" );

	if ( state->r_glxPostShaderDebug && state->r_glxPostShaderDebug->integer ) {
		RI().Printf( PRINT_ALL,
			"GLx post shader compiled %s source 0x%08x features 0x%08x program %u.\n",
			GLX_PostShaderSource_TargetName( source.target ), source.sourceHash,
			plan.featureMask, program->program );
	}

	return program;
}

static qboolean GLX_PostShader_CacheProgram( PostShaderState *state,
	const PostShaderPlan &plan )
{
	PostShaderSourceSummary source;
	PostShaderProgram *program;

	if ( !state ) {
		return qfalse;
	}

	state->lastTargetFallbackUsed = qfalse;
	source = GLX_PostShader_BuildSourceSummary( state, plan );

	state->lastPlanHash = plan.hash;
	state->lastFeatureMask = plan.featureMask;
	state->lastSource = source;
	state->lastSourceHash = source.sourceHash;
	state->lastPlanValid = plan.valid;

	if ( !plan.valid ) {
		state->invalidPlansObserved++;
		return qfalse;
	}
	state->validPlansObserved++;

	if ( !source.valid ) {
		state->sourceFailures++;
		GLX_PostShader_SetLastError( state, source.truncated ?
			"post shader source was truncated" : "post shader source invalid" );
		return qfalse;
	}

	program = GLX_PostShader_FindProgram( state, plan, source );
	if ( !program ) {
		program = GLX_PostShader_CreateProgram( state, plan, source );
		if ( !program && state->activeTarget != PostShaderSourceTarget::Glsl120 ) {
			const PostShaderSourceTarget failedTarget = state->activeTarget;
			state->targetFallbacks++;
			state->lastTargetFallbackUsed = qtrue;
			state->lastFallbackFromTarget = failedTarget;
			state->lastFallbackToTarget = PostShaderSourceTarget::Glsl120;
			state->activeTarget = PostShaderSourceTarget::Glsl120;
			state->modernTargetSuppressed = qtrue;
			source = GLX_PostShaderSource_BuildSummary( plan,
				PostShaderSourceTarget::Glsl120 );
			state->lastSource = source;
			state->lastSourceHash = source.sourceHash;
			program = GLX_PostShader_FindProgram( state, plan, source );
			if ( !program ) {
				program = GLX_PostShader_CreateProgram( state, plan, source );
			}
			if ( program ) {
				GLX_PostShader_SetReason( state,
					"GLSL 1.20 fallback after modern post shader compile/link failure" );
			}
		}
	}
	if ( !program ) {
		return qfalse;
	}

	program->uses++;
	state->lastProgram = program->program;
	return qtrue;
}

static void GLX_PostShader_Identity3x3( float matrix[9] )
{
	if ( !matrix ) {
		return;
	}
	matrix[0] = 1.0f; matrix[1] = 0.0f; matrix[2] = 0.0f;
	matrix[3] = 0.0f; matrix[4] = 1.0f; matrix[5] = 0.0f;
	matrix[6] = 0.0f; matrix[7] = 0.0f; matrix[8] = 1.0f;
}

static qboolean GLX_PostShader_SetOptionalVec4( PostShaderState *state,
	PostShaderProgram *program, GLint location, float x, float y, float z, float w,
	unsigned int requiredFeature )
{
	if ( !state || !program || !state->fns.Uniform4f ) {
		return qfalse;
	}
	if ( location < 0 ) {
		return ( program->plan.featureMask & requiredFeature ) != 0u ? qfalse : qtrue;
	}
	state->fns.Uniform4f( location, x, y, z, w );
	return qtrue;
}

static float GLX_PostShader_SanitizeFloat( float value, float fallback,
	float minValue, float maxValue )
{
	if ( !std::isfinite( value ) ) {
		value = fallback;
	}
	if ( value < minValue ) {
		return minValue;
	}
	if ( value > maxValue ) {
		return maxValue;
	}
	return value;
}

static void GLX_PostShader_SanitizeVec3( const float in[3], const float fallback[3],
	float minValue, float maxValue, float out[3] )
{
	for ( int i = 0; i < 3; i++ ) {
		out[i] = GLX_PostShader_SanitizeFloat( in ? in[i] : fallback[i],
			fallback[i], minValue, maxValue );
	}
}

static float GLX_PostShader_SanitizePaperWhite( const OutputTransform &output )
{
	return GLX_PostShader_SanitizeFloat( output.paperWhiteNits, 203.0f,
		1.0f, 10000.0f );
}

static float GLX_PostShader_SanitizeMaxOutput( const OutputTransform &output,
	float paperWhite )
{
	return GLX_PostShader_SanitizeFloat( output.maxOutputNits, paperWhite,
		paperWhite, 10000.0f );
}

static float GLX_PostShader_SanitizeHeadroom( const OutputTransform &output,
	float paperWhite, float maxOutput )
{
	const float nitsHeadroom = maxOutput / ( paperWhite > 0.001f ? paperWhite : 0.001f );
	float headroom = nitsHeadroom;

	if ( output.displayHdrHeadroomValid ) {
		headroom = GLX_PostShader_SanitizeFloat( output.displayHdrHeadroom,
			nitsHeadroom, 1.0f, 64.0f );
		if ( headroom > nitsHeadroom ) {
			headroom = nitsHeadroom;
		}
	}
	return GLX_PostShader_SanitizeFloat( headroom, nitsHeadroom, 1.0f, 64.0f );
}

static float GLX_PostShader_SanitizeKelvin( float kelvin )
{
	return GLX_PostShader_SanitizeFloat( kelvin, 6504.0f, 1000.0f, 40000.0f );
}

static qboolean GLX_PostShader_SetFinalUniforms( PostShaderState *state,
	PostShaderProgram *program, const OutputTransform &output, float bloomIntensity )
{
	float whitePointMatrix[9];
	float lift[3];
	float gamma[3];
	float invGamma[3];
	float gain[3];
	const float liftFallback[3] = { 0.0f, 0.0f, 0.0f };
	const float gammaFallback[3] = { 1.0f, 1.0f, 1.0f };
	const float gainFallback[3] = { 1.0f, 1.0f, 1.0f };
	const float exposure = GLX_PostShader_SanitizeFloat( output.exposure, 1.0f,
		0.0f, 64.0f );
	const float paperWhite = GLX_PostShader_SanitizePaperWhite( output );
	const float maxOutput = GLX_PostShader_SanitizeMaxOutput( output, paperWhite );
	const float headroom = GLX_PostShader_SanitizeHeadroom( output, paperWhite,
		maxOutput );
	const float displaySdrWhite = GLX_PostShader_SanitizeFloat(
		output.displaySdrWhiteNits, paperWhite, 1.0f, 10000.0f );
	const float displayMax = GLX_PostShader_SanitizeFloat( output.displayMaxNits,
		maxOutput, displaySdrWhite, 10000.0f );
	const float greyscale = GLX_PostShader_SanitizeFloat( output.greyscale, 0.0f,
		0.0f, 1.0f );
	const float legacyGamma = GLX_PostShader_SanitizeFloat( output.legacyGamma,
		1.0f, 0.01f, 16.0f );
	const float legacyOverbright = GLX_PostShader_SanitizeFloat( output.legacyOverbright,
		1.0f, 0.0f, 64.0f );
	const float bloom = GLX_PostShader_SanitizeFloat( bloomIntensity, 0.0f,
		0.0f, 64.0f );
	const float crtAmount = GLX_PostShader_SanitizeFloat( output.crtAmount, 0.0f,
		0.0f, 1.0f );
	const float crtScanline = GLX_PostShader_SanitizeFloat( output.crtScanlineStrength,
		0.55f, 0.0f, 1.0f );
	const float crtMask = GLX_PostShader_SanitizeFloat( output.crtMaskStrength,
		0.35f, 0.0f, 1.0f );
	const float crtCurvature = GLX_PostShader_SanitizeFloat( output.crtCurvature,
		0.01f, 0.0f, 0.25f );
	const float crtChromatic = GLX_PostShader_SanitizeFloat( output.crtChromatic,
		1.35f, 0.0f, 8.0f );
	const float crtInvWidth = GLX_PostShader_SanitizeFloat( output.crtInvWidth,
		1.0f, 0.000001f, 1.0f );
	const float crtInvHeight = GLX_PostShader_SanitizeFloat( output.crtInvHeight,
		1.0f, 0.000001f, 1.0f );
	const float crtTime = state ? static_cast<float>( state->frames ) * ( 1.0f / 60.0f ) : 0.0f;
	const float sourceKelvin = GLX_PostShader_SanitizeKelvin(
		output.whitePointSourceKelvin );
	const float targetKelvin = GLX_PostShader_SanitizeKelvin(
		output.whitePointTargetKelvin );
	const qboolean lgg = program ?
		GLX_PostShader_GradeUsesLiftGammaGain( program->plan.key.grade ) : qfalse;
	const qboolean lut = ( program && program->plan.key.lutActive ) ? qtrue : qfalse;
	const qboolean postParamsRequired =
		( program && ( program->plan.featureMask &
		( GLX_POST_SHADER_FEATURE_OUTPUT_TRANSFORM |
		GLX_POST_SHADER_FEATURE_GREYSCALE |
		GLX_POST_SHADER_FEATURE_HDR_HEADROOM_OUTPUT ) ) != 0u ) ? qtrue : qfalse;
	const qboolean outputParamsRequired =
		( program && ( program->plan.featureMask &
		( GLX_POST_SHADER_FEATURE_HDR_HEADROOM_OUTPUT |
		GLX_POST_SHADER_FEATURE_GAMUT_COMPRESS ) ) != 0u ) ? qtrue : qfalse;

	if ( !state || !program || !state->fns.Uniform4f ) {
		return qfalse;
	}
	if ( postParamsRequired && program->postParams0Uniform < 0 ) {
		GLX_PostShader_SetLastError( state, "post shader final missing u_PostParams0" );
		return qfalse;
	}
	if ( outputParamsRequired && program->outputParams1Uniform < 0 ) {
		GLX_PostShader_SetLastError( state, "post shader final missing u_OutputParams1" );
		return qfalse;
	}
	if ( lut && program->lutUniform < 0 ) {
		GLX_PostShader_SetLastError( state, "post shader final missing u_ColorGradeLut" );
		return qfalse;
	}

	if ( program->postParams0Uniform >= 0 ) {
		state->fns.Uniform4f( program->postParams0Uniform, exposure,
			paperWhite, maxOutput, greyscale );
	}
	if ( program->outputParams1Uniform >= 0 ) {
		state->fns.Uniform4f( program->outputParams1Uniform, headroom,
			displaySdrWhite, displayMax, 0.0f );
	}
	if ( !GLX_PostShader_SetOptionalVec4( state, program, program->bloomParamsUniform,
		bloom, 0.0f, 0.0f, 0.0f,
		GLX_POST_SHADER_FEATURE_BLOOM_COMBINE ) ) {
		GLX_PostShader_SetLastError( state, "post shader final missing bloom uniform" );
		return qfalse;
	}
	if ( !GLX_PostShader_SetOptionalVec4( state, program, program->legacyParamsUniform,
		legacyGamma, legacyOverbright, 0.0f, 0.0f,
		GLX_POST_SHADER_FEATURE_LEGACY_GAMMA ) ) {
		GLX_PostShader_SetLastError( state, "post shader final missing legacy gamma uniform" );
		return qfalse;
	}
	if ( !GLX_PostShader_SetOptionalVec4( state, program, program->crtParams0Uniform,
		crtAmount, crtScanline, crtMask, crtCurvature,
		GLX_POST_SHADER_FEATURE_CRT ) ||
		!GLX_PostShader_SetOptionalVec4( state, program, program->crtParams1Uniform,
		crtChromatic, crtTime, crtInvWidth, crtInvHeight,
		GLX_POST_SHADER_FEATURE_CRT ) ) {
		GLX_PostShader_SetLastError( state, "post shader final missing CRT uniform" );
		return qfalse;
	}
	if ( lgg && sourceKelvin != targetKelvin ) {
		GLX_ColorMath_BuildBradfordAdaptationMatrix( sourceKelvin,
			targetKelvin, whitePointMatrix );
	} else {
		GLX_PostShader_Identity3x3( whitePointMatrix );
	}
	GLX_PostShader_SanitizeVec3( output.gradeLift, liftFallback, -1.0f, 1.0f, lift );
	GLX_PostShader_SanitizeVec3( output.gradeGamma, gammaFallback, 0.0001f, 10000.0f,
		gamma );
	GLX_PostShader_SanitizeVec3( output.gradeGain, gainFallback, 0.0f, 64.0f, gain );
	invGamma[0] = 1.0f / gamma[0];
	invGamma[1] = 1.0f / gamma[1];
	invGamma[2] = 1.0f / gamma[2];
	if ( !GLX_PostShader_SetOptionalVec4( state, program, program->liftUniform,
		lift[0], lift[1], lift[2], 0.0f,
		GLX_POST_SHADER_FEATURE_LIFT_GAMMA_GAIN ) ||
		!GLX_PostShader_SetOptionalVec4( state, program, program->invGammaUniform,
		invGamma[0], invGamma[1], invGamma[2], 1.0f,
		GLX_POST_SHADER_FEATURE_LIFT_GAMMA_GAIN ) ||
		!GLX_PostShader_SetOptionalVec4( state, program, program->gainUniform,
		gain[0], gain[1], gain[2], 1.0f,
		GLX_POST_SHADER_FEATURE_LIFT_GAMMA_GAIN ) ||
		!GLX_PostShader_SetOptionalVec4( state, program, program->whitePoint0Uniform,
		whitePointMatrix[0], whitePointMatrix[1], whitePointMatrix[2], 0.0f,
		GLX_POST_SHADER_FEATURE_WHITE_POINT ) ||
		!GLX_PostShader_SetOptionalVec4( state, program, program->whitePoint1Uniform,
		whitePointMatrix[3], whitePointMatrix[4], whitePointMatrix[5], 0.0f,
		GLX_POST_SHADER_FEATURE_WHITE_POINT ) ||
		!GLX_PostShader_SetOptionalVec4( state, program, program->whitePoint2Uniform,
		whitePointMatrix[6], whitePointMatrix[7], whitePointMatrix[8], 0.0f,
		GLX_POST_SHADER_FEATURE_WHITE_POINT ) ) {
		GLX_PostShader_SetLastError( state, "post shader final missing grade uniform" );
		return qfalse;
	}
	if ( lut ) {
		const float lutSizeFloat = GLX_PostShader_SanitizeFloat( output.lutSize,
			0.0f, 0.0f, 64.0f );
		const int lutSize = static_cast<int>( lutSizeFloat + 0.5f );
		const float lutScale = GLX_PostShader_SanitizeFloat( output.lutScale,
			4.0f, 0.001f, 64.0f );
		if ( lutSize < 2 || lutSize > 64 ) {
			GLX_PostShader_SetLastError( state, "post shader final LUT size is invalid" );
			return qfalse;
		}
		if ( program->lutParamsUniform < 0 ) {
			GLX_PostShader_SetLastError( state, "post shader final missing u_LutParams" );
			return qfalse;
		}
		state->fns.Uniform4f( program->lutParamsUniform, lutScale,
			static_cast<float>( lutSize - 1 ), 0.5f, 1.0f / lutScale );
	}
	return qtrue;
}

static qboolean GLX_PostShader_PrecachePrograms( PostShaderState *state )
{
	OutputTransform output;
	PostShaderPlan plan;
	qboolean ok = qtrue;

	if ( !state ) {
		return qfalse;
	}

	state->precacheAttempts++;

	output = GLX_RenderIR_DefaultOutputTransform();
	plan = GLX_PostShader_BuildPlan( output );
	ok = ( GLX_PostShader_CacheProgram( state, plan ) && ok ) ? qtrue : qfalse;

	output.sceneColorSpace = SceneColorSpace::SceneLinear;
	output.hdrMode = 1;
	output.precisionMode = 16;
	output.transfer = OutputTransfer::SdrSrgb;
	output.toneMap = ToneMapOperator::Aces;
	output.grade = ColorGradeMode::NoColorGrade;
	plan = GLX_PostShader_BuildPlan( output );
	ok = ( GLX_PostShader_CacheProgram( state, plan ) && ok ) ? qtrue : qfalse;
	plan = GLX_PostShader_BuildPlanForOutput( output, qtrue );
	ok = ( GLX_PostShader_CacheProgram( state, plan ) && ok ) ? qtrue : qfalse;

	output.transfer = OutputTransfer::Hdr10Pq;
	output.outputPrimaries = OutputPrimaries::Bt2020;
	output.gamutMap = GamutMapMode::CompressToOutput;
	output.paperWhiteNits = 203.0f;
	output.maxOutputNits = 1000.0f;
	plan = GLX_PostShader_BuildPlan( output );
	ok = ( GLX_PostShader_CacheProgram( state, plan ) && ok ) ? qtrue : qfalse;

	output.transfer = OutputTransfer::MacEdr;
	output.outputPrimaries = OutputPrimaries::DisplayP3;
	output.gamutMap = GamutMapMode::CompressToOutput;
	plan = GLX_PostShader_BuildPlanForOutput( output, qtrue );
	ok = ( GLX_PostShader_CacheProgram( state, plan ) && ok ) ? qtrue : qfalse;

	if ( !ok ) {
		state->precacheFailures++;
	}
	return ok;
}

void GLX_PostShader_RegisterCvars( PostShaderState *state )
{
	if ( !state ) {
		return;
	}

	state->r_glxPostShaderCache = RI().Cvar_Get( "r_glxPostShaderCache", "1",
		CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxPostShaderCache,
		"Compile and cache generated GLx post/output GLSL programs without enabling the owned final-pass executor." );

	state->r_glxPostShaderPrecache = RI().Cvar_Get( "r_glxPostShaderPrecache", "1",
		CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxPostShaderPrecache,
		"Compile representative GLx post/output shader shapes during OpenGL startup." );

	state->r_glxPostShaderExecute = RI().Cvar_Get( "r_glxPostShaderExecute", "0",
		CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxPostShaderExecute,
		"Experimentally bind the generated GLx GLSL shader for eligible display-referred SDR gamma and scene-linear post/output passes, including bloom composite, greyscale, blit/capture, and hardware HDR output. Falls back to the legacy ARB path when disabled or unsupported." );

	state->r_glxPostShaderTarget = RI().Cvar_Get( "r_glxPostShaderTarget", "0",
		CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxPostShaderTarget,
		"Generated GLx post shader GLSL target: 0 auto, 120 compatibility fallback, 130, 150 compatibility, 330 compatibility, or 410 compatibility." );

	state->r_glxPostShaderDebug = RI().Cvar_Get( "r_glxPostShaderDebug", "0",
		CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxPostShaderDebug,
		"Print GLx post shader cache diagnostics. Set to 2 to also dump failed GLSL source." );
}

void GLX_PostShader_OnOpenGLReady( PostShaderState *state, const Capabilities &caps )
{
	if ( !state ) {
		return;
	}

	GLX_PostShader_Shutdown( state, qtrue );
	GLX_PostShader_ResetCounters( state );
	state->tier = caps.tier;
	state->glMajor = caps.major;
	state->glMinor = caps.minor;
	state->activeTarget = GLX_PostShader_ResolveTarget( state );
	state->modernTargetSuppressed = qfalse;
	GLX_PostShader_SetReason( state, "not initialized" );

	if ( caps.tier == RenderProductTier::GL12 ) {
		GLX_PostShader_SetReason( state, "GL12 fixed-function tier has no GLSL post shader cache" );
		return;
	}

	GLX_PostShader_LoadFunctions( state );
	if ( !GLX_PostShader_FunctionsReady( *state ) ) {
		GLX_PostShader_SetReason( state, "required GLSL program functions are unavailable" );
		return;
	}

	state->ready = qtrue;
	if ( ( !state->r_glxPostShaderPrecache || state->r_glxPostShaderPrecache->integer ) &&
		!GLX_PostShader_PrecachePrograms( state ) ) {
		state->ready = qfalse;
		GLX_PostShader_SetReason( state, "GLSL post shader precache failed" );
		return;
	}

	GLX_PostShader_SetReason( state, "GLSL post shader cache ready" );
}

void GLX_PostShader_Shutdown( PostShaderState *state, qboolean deletePrograms )
{
	if ( !state ) {
		return;
	}

	GLX_PostShader_ResetRuntime( state, deletePrograms );
}

void GLX_PostShader_FrameComplete( PostShaderState *state )
{
	if ( state ) {
		state->frames++;
	}
}

qboolean GLX_PostShader_Ready( const PostShaderState &state )
{
	return state.ready && state.r_glxPostShaderCache &&
		state.r_glxPostShaderCache->integer ? qtrue : qfalse;
}

qboolean GLX_PostShader_ExecutionEnabled( const PostShaderState &state )
{
	return GLX_PostShader_Ready( state ) && state.r_glxPostShaderExecute &&
		state.r_glxPostShaderExecute->integer ? qtrue : qfalse;
}

qboolean GLX_PostShader_RecordPlan( PostShaderState *state, const PostShaderPlan &plan )
{
	if ( !state ) {
		return qfalse;
	}

	state->plansObserved++;
	if ( !GLX_PostShader_Ready( *state ) ) {
		state->lastPlanHash = plan.hash;
		state->lastFeatureMask = plan.featureMask;
		state->lastSource = GLX_PostShader_BuildSourceSummary( state, plan );
		state->lastSourceHash = state->lastSource.sourceHash;
		state->lastPlanValid = plan.valid;
		if ( plan.valid ) {
			state->validPlansObserved++;
		} else {
			state->invalidPlansObserved++;
		}
		return qfalse;
	}

	return GLX_PostShader_CacheProgram( state, plan );
}

qboolean GLX_PostShader_TryBindFinal( PostShaderState *state,
	const PostShaderPlan &plan, const OutputTransform &output,
	qboolean bloomComposite, qboolean outputTransform, float bloomIntensity )
{
	PostShaderSourceSummary source;
	PostShaderProgram *program;
	unsigned int rejectMask;
	qboolean forceForCrt;

	if ( !state ) {
		return qfalse;
	}

	state->directFinalCandidates++;
	state->lastDirectFinalBound = qfalse;
	rejectMask = GLX_PostShader_FinalCompatibilityRejectMask( plan, output,
		bloomComposite, outputTransform );
	forceForCrt = ( plan.key.crt && outputTransform ) ? qtrue : qfalse;
	if ( !( forceForCrt ? state->ready : GLX_PostShader_Ready( *state ) ) ) {
		rejectMask |= GLX_POST_SHADER_DIRECT_REJECT_NOT_READY;
	}

	state->lastDirectFinalEligible = ( rejectMask == GLX_POST_SHADER_DIRECT_REJECT_NONE ) ?
		qtrue : qfalse;
	if ( state->lastDirectFinalEligible ) {
		state->directFinalEligibleFrames++;
	}

	if ( !forceForCrt && ( !state->r_glxPostShaderExecute ||
		!state->r_glxPostShaderExecute->integer ) ) {
		rejectMask |= GLX_POST_SHADER_DIRECT_REJECT_DISABLED;
	}
	if ( rejectMask != GLX_POST_SHADER_DIRECT_REJECT_NONE ) {
		state->lastDirectFinalRejectMask = rejectMask;
		state->directFinalRejects++;
		state->directFinalFallbacks++;
		return qfalse;
	}

	state->directFinalAttempts++;
	state->lastTargetFallbackUsed = qfalse;
	source = GLX_PostShader_BuildSourceSummary( state, plan );
	program = GLX_PostShader_FindProgramForUse( state, plan, source );
	if ( !program ) {
		state->directFinalProgramMisses++;
		program = GLX_PostShader_CreateProgram( state, plan, source );
		if ( !program && state->activeTarget != PostShaderSourceTarget::Glsl120 ) {
			const PostShaderSourceTarget failedTarget = state->activeTarget;
			state->targetFallbacks++;
			state->lastTargetFallbackUsed = qtrue;
			state->lastFallbackFromTarget = failedTarget;
			state->lastFallbackToTarget = PostShaderSourceTarget::Glsl120;
			state->activeTarget = PostShaderSourceTarget::Glsl120;
			state->modernTargetSuppressed = qtrue;
			source = GLX_PostShaderSource_BuildSummary( plan,
				PostShaderSourceTarget::Glsl120 );
			state->lastSource = source;
			state->lastSourceHash = source.sourceHash;
			program = GLX_PostShader_FindProgramForUse( state, plan, source );
			if ( !program ) {
				program = GLX_PostShader_CreateProgram( state, plan, source );
			}
			if ( program ) {
				GLX_PostShader_SetReason( state,
					"GLSL 1.20 fallback after modern post shader compile/link failure" );
			}
		}
	}
	if ( !program ) {
		state->lastDirectFinalRejectMask = GLX_POST_SHADER_DIRECT_REJECT_PROGRAM;
		state->directFinalRejects++;
		state->directFinalFallbacks++;
		return qfalse;
	}

	state->fns.UseProgram( program->program );
	state->currentProgram = program->program;
	if ( !GLX_PostShader_SetFinalUniforms( state, program, output, bloomIntensity ) ) {
		GLX_PostShader_Unbind( state );
		state->lastDirectFinalRejectMask = GLX_POST_SHADER_DIRECT_REJECT_UNIFORM;
		state->directFinalUniformFailures++;
		state->directFinalRejects++;
		state->directFinalFallbacks++;
		return qfalse;
	}

	program->uses++;
	state->lastProgram = program->program;
	state->lastDirectFinalRejectMask = GLX_POST_SHADER_DIRECT_REJECT_NONE;
	state->lastDirectFinalBound = qtrue;
	state->directFinalBinds++;
	GLX_PostShader_SetLastError( state, "" );
	return qtrue;
}

qboolean GLX_PostShader_TryBindDirectFinal( PostShaderState *state,
	const PostShaderPlan &plan, const OutputTransform &output, float greyscale )
{
	(void)greyscale;
	return GLX_PostShader_TryBindFinal( state, plan, output, qfalse, qtrue, 0.0f );
}

void GLX_PostShader_Unbind( PostShaderState *state )
{
	if ( !state || !state->fns.UseProgram ) {
		return;
	}
	if ( state->currentProgram ) {
		state->fns.UseProgram( 0 );
		state->currentProgram = 0;
	}
}

void GLX_PostShader_PrintInfo( const PostShaderState &state )
{
	RI().Printf( PRINT_ALL,
		"  post shader cache: ready %s, programs %i/%i, plans %u valid/%u invalid, cache %u hits/%u misses, compile %u attempts/%u failures, link failures %u, source failures %u, precache %u/%u failures, labels %u, contextless deletes %u, evictions %u\n",
		BoolName( GLX_PostShader_Ready( state ) ),
		state.programCount, GLX_POST_SHADER_PROGRAM_LIMIT,
		state.validPlansObserved, state.invalidPlansObserved,
		state.cacheHits, state.cacheMisses,
		state.compileAttempts, state.compileFailures,
		state.linkFailures, state.sourceFailures,
		state.precacheAttempts, state.precacheFailures,
		state.debugLabels, state.contextlessDeletes, state.cacheEvictions );
	RI().Printf( PRINT_ALL,
		"  post shader target: active %s, preferred %s, requested %s, last compile %s, fallback used %s %s->%s, target fallbacks %u, capability fallbacks %u, unsupported %s, modern suppressed %s\n",
		GLX_PostShaderSource_TargetName( state.activeTarget ),
		GLX_PostShaderSource_TargetName( state.preferredTarget ),
		GLX_PostShaderSource_TargetName( state.lastRequestedTarget ),
		GLX_PostShaderSource_TargetName( state.lastCompileTarget ),
		BoolName( state.lastTargetFallbackUsed ),
		GLX_PostShaderSource_TargetName( state.lastFallbackFromTarget ),
		GLX_PostShaderSource_TargetName( state.lastFallbackToTarget ),
		state.targetFallbacks,
		state.targetCapabilityFallbacks,
		BoolName( state.lastTargetUnsupported ),
		BoolName( state.modernTargetSuppressed ) );
	RI().Printf( PRINT_ALL,
		"  post shader source: plan valid %s, target %s v%i, features 0x%08x, plan hash 0x%08x, source hash 0x%08x, source valid %s, truncated %s, vertex bytes %i, fragment bytes %i, last program %u, evicted 0x%08x features 0x%08x uses %u target %s, reason: %s\n",
		BoolName( state.lastPlanValid ),
		GLX_PostShaderSource_TargetName( state.lastSource.target ),
		state.lastSource.targetVersion,
		state.lastFeatureMask,
		state.lastPlanHash,
		state.lastSourceHash,
		BoolName( state.lastSource.valid ),
		BoolName( state.lastSource.truncated ),
		state.lastSource.vertexBytes,
		state.lastSource.fragmentBytes,
		state.lastProgram,
		state.lastEvictedSourceHash,
		state.lastEvictedFeatureMask,
		state.lastEvictedUses,
		GLX_PostShaderSource_TargetName( state.lastEvictedTarget ),
		state.reason[0] ? state.reason : "none" );
	RI().Printf( PRINT_ALL,
		"  post shader direct-final: execute %s, eligible %s, bound %s, reject 0x%08x, candidates %u, eligible frames %u, attempts %u, binds %u, fallbacks %u, rejects %u, program misses %u, uniform failures %u\n",
		BoolName( state.r_glxPostShaderExecute && state.r_glxPostShaderExecute->integer ? qtrue : qfalse ),
		BoolName( state.lastDirectFinalEligible ),
		BoolName( state.lastDirectFinalBound ),
		state.lastDirectFinalRejectMask,
		state.directFinalCandidates,
		state.directFinalEligibleFrames,
		state.directFinalAttempts,
		state.directFinalBinds,
		state.directFinalFallbacks,
		state.directFinalRejects,
		state.directFinalProgramMisses,
		state.directFinalUniformFailures );
	if ( state.lastError[0] ) {
		RI().Printf( PRINT_ALL, "  post shader last error: %s\n", state.lastError );
	}
}

} // namespace glx
