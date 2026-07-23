#include "glx_local.h"
#include "glx_module.h"
#include "glx_caps.h"
#include "glx_debug.h"
#include "glx_executor.h"
#include "glx_material.h"
#include "glx_postprocess.h"
#include "glx_post_shader.h"
#include "glx_profiler.h"
#include "glx_static_world.h"
#include "glx_stream.h"

#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#ifndef GL_BUFFER
#define GL_BUFFER 0x82E0
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_SHADER_STORAGE_BUFFER
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#endif
#ifndef GL_DRAW_INDIRECT_BUFFER
#define GL_DRAW_INDIRECT_BUFFER 0x8F3F
#endif
#ifndef GL_DRAW_INDIRECT_BUFFER_BINDING
#define GL_DRAW_INDIRECT_BUFFER_BINDING 0x8F43
#endif
#ifndef GL_NO_ERROR
#define GL_NO_ERROR 0
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
#ifndef GL_UNSIGNED_BYTE
#define GL_UNSIGNED_BYTE 0x1401
#endif
#ifndef GL_UNSIGNED_SHORT
#define GL_UNSIGNED_SHORT 0x1403
#endif
#ifndef GL_UNSIGNED_INT
#define GL_UNSIGNED_INT 0x1405
#endif

namespace glx {

refimport_t *g_imports = nullptr;

refimport_t &RI()
{
	return *g_imports;
}

qboolean ImportsReady()
{
	return g_imports ? qtrue : qfalse;
}

const char *BoolName( qboolean value )
{
	return value ? "yes" : "no";
}

qboolean ToQBool( bool value )
{
	return value ? qtrue : qfalse;
}

static unsigned int GLX_Module_IndexStrideBytes( unsigned int type )
{
	switch ( type ) {
	case GL_UNSIGNED_BYTE:
		return 1u;
	case GL_UNSIGNED_SHORT:
		return 2u;
	case GL_UNSIGNED_INT:
		return 4u;
	default:
		return 0u;
	}
}

static int GLX_Module_Stricmp( const char *lhs, const char *rhs )
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

static const char *GLX_Module_MdiSourceName( unsigned int source )
{
	switch ( source ) {
	case 1:
		return "static";
	case 2:
		return "compact";
	default:
		return "none";
	}
}

enum class GlxProfile {
	Off,
	Rc,
	Stress,
};

struct ProfileCvarSetting {
	const char *name;
	const char *offValue;
	const char *rcValue;
	const char *stressValue;
};

static const ProfileCvarSetting GLX_PROFILE_CVARS[] = {
	{ "r_fbo", "0", "1", "1" },
	{ "r_bloom", "0", "2", "2" },
	{ "r_bloom_passes", "5", "3", "3" },
	{ "r_hdrBloomFormat", "0", "0", "0" },
	{ "r_vbo", "0", "1", "1" },
	{ "r_glxWorldRenderer", "0", "1", "1" },
	{ "r_glxStreamDraw", "0", "1", "1" },
	{ "r_glxStreamDrawKeyMode", "0", "0", "0" },
	{ "r_glxStreamDrawMultitexture", "0", "1", "1" },
	{ "r_glxStreamDrawFog", "0", "1", "1" },
	{ "r_glxStreamDrawDepthFragment", "0", "1", "1" },
	{ "r_glxStreamDrawTexMods", "0", "1", "1" },
	{ "r_glxStreamDrawEnvironment", "0", "1", "1" },
	{ "r_glxStreamDrawDynamicLights", "0", "auto", "auto" },
	{ "r_glxDlightScissor", "0", "auto", "auto" },
	{ "r_glxDlightProjectedProgram", "0", "0", "0" },
	{ "r_glxDlightProjectedMdi", "0", "0", "0" },
	{ "r_glxStreamDrawScreenMaps", "0", "0", "0" },
	{ "r_glxStreamDrawVideoMaps", "0", "0", "0" },
	{ "r_glxStreamDrawShadows", "0", "1", "1" },
	{ "r_glxStreamDrawBeams", "0", "1", "1" },
	{ "r_glxStreamDrawPostProcess", "0", "1", "1" },
	{ "r_glxMaterialRenderer", "0", "1", "1" },
	{ "r_glxMaterialPrecache", "0", "1", "1" },
	{ "r_glxGpuTiming", "0", "1", "1" },
	{ "r_glxGpuPassTiming", "0", "1", "1" },
	{ "r_glxStaticWorldArena", "0", "1", "1" },
	{ "r_glxStaticWorldArenaDraw", "0", "1", "1" },
	{ "r_glxStaticWorldDraw", "0", "1", "1" },
	{ "r_glxStaticWorldSoftDraw", "0", "1", "1" },
	{ "r_glxStaticWorldDrawPolicy", "full", "full", "full" },
	{ "r_glxStaticWorldMultiDraw", "0", "1", "1" },
	{ "r_glxStaticWorldPacketBatch", "0", "1", "1" },
	{ "r_glxStaticWorldIndirectBuffer", "0", "1", "1" },
	{ "r_glxStaticWorldIndirectDraw", "0", "1", "1" },
	{ "r_glxStaticWorldMultiDrawIndirect", "0", "1", "1" },
	{ "r_glxStaticWorldMultiDrawIndirectCompact", "0", "0", "1" },
	{ "r_glxStaticWorldMultiDrawIndirectSpans", "0", "1", "1" },
};

typedef void ( APIENTRY *PFNGLXUNIFORM1FPROC )( GLint location, GLfloat v0 );
typedef void ( APIENTRY *PFNGLXUNIFORM4FVPROC )( GLint location, GLsizei count, const GLfloat *value );
typedef void ( APIENTRY *PFNGLXBINDBUFFERRANGEPROC )( GLenum target, GLuint index,
	GLuint buffer, ptrdiff_t offset, ptrdiff_t size );
typedef void ( APIENTRY *PFNGLXDLIGHTBINDBUFFERPROC )( GLenum target, GLuint buffer );
typedef void ( APIENTRY *PFNGLXDLIGHTGETINTEGERVPROC )( GLenum pname, GLint *params );
typedef GLenum ( APIENTRY *PFNGLXDLIGHTGETERRORPROC )( void );
typedef void ( APIENTRY *PFNGLXDLIGHTMULTIDRAWELEMENTSINDIRECTPROC )( GLenum mode,
	GLenum type, const GLvoid *indirect, GLsizei drawcount, GLsizei stride );

struct DlightFns {
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
	PFNGLXUNIFORM1FPROC Uniform1f;
	PFNGLXUNIFORM4FVPROC Uniform4fv;
	PFNGLXBINDBUFFERRANGEPROC BindBufferRange;
	PFNGLXDELETEPROGRAMPROC DeleteProgram;
	PFNGLXDELETESHADERPROC DeleteShader;
};

struct DlightProgramKey {
	qboolean linear;
	int fogMode;
	qboolean absLight;
	qboolean shadow;
	qboolean projectedStream;
};

struct DlightProgram {
	DlightProgramKey key;
	GLuint program;
	GLint texture0Uniform;
	GLint fogTextureUniform;
	GLint shadowTextureUniform;
	GLint eyePosUniform;
	GLint lightPosUniform;
	GLint lightColorUniform;
	GLint lightVectorUniform;
	GLint texFactorsUniform;
	GLint dlightFactorsUniform;
	GLint fogDistanceVectorUniform;
	GLint fogDepthVectorUniform;
	GLint fogEyeTUniform;
	GLint dlightShadowUniform;
	GLint shadowAtlasUniform;
	GLint shadowDepthUniform;
	GLint shadowFilterUniform;
	GLint projectedControlUniform;
	GLint projectedOriginRadiusUniform;
	GLint projectedColorFlagsUniform;
};

struct ProjectedDlightMdiFns {
	PFNGLXDLIGHTBINDBUFFERPROC BindBuffer;
	PFNGLXDLIGHTGETINTEGERVPROC GetIntegerv;
	PFNGLXDLIGHTGETERRORPROC GetError;
	PFNGLXDLIGHTMULTIDRAWELEMENTSINDIRECTPROC MultiDrawElementsIndirect;
};

static ProjectedDlightMdiFns s_projectedDlightMdiFns {};

/*
Must cover the full DlightProgramKey space: linear(2) x fogMode(3) x
absLight(2) x shadow(2) x projectedStream(2) = 48, since a full cache
permanently fails every uncached bind for the rest of the session.
*/
static constexpr int GLX_DLIGHT_PROGRAM_LIMIT = 48;
static constexpr int GLX_PROJECTED_DLIGHT_UNIFORM_LIMIT = 3;
static constexpr int GLX_PROJECTED_DLIGHT_STREAM_ALIGNMENT = 64;
static constexpr GLuint GLX_PROJECTED_DLIGHT_STREAM_BINDING = 5;
static constexpr unsigned int GLX_PROJECTED_DLIGHT_MDI_BATCH_LIMIT = 256u;
static constexpr unsigned int GLX_PROJECTED_DLIGHT_MDI_COMMAND_RING_LIMIT =
	GLX_PROJECTED_DLIGHT_MDI_BATCH_LIMIT;
static constexpr int GLX_PROJECTED_DLIGHT_LIST_ARENA_LIMIT =
	GLX_RENDER_IR_PROJECTED_DLIGHT_LIST_RECORD_LIMIT;

struct ProjectedDlightStreamRecord {
	float originRadius[4];
	float colorFlags[4];
};

struct ProjectedDlightArenaListRecord {
	unsigned int recordIndex;
	unsigned int flags;
};

struct DlightState {
	cvar_t *enabled;
	cvar_t *scissor;
	cvar_t *projectedProgram;
	cvar_t *projectedMdi;
	DlightFns fns;
	DlightProgram programs[GLX_DLIGHT_PROGRAM_LIMIT];
	int programCount;
	GLuint currentProgram;
	qboolean ready;
	char reason[96];
	unsigned int availabilityQueries;
	unsigned int availabilityHits;
	unsigned int availabilityMisses;
	unsigned int programCacheHits;
	unsigned int programCreates;
	unsigned int programCreateFailures;
	unsigned int shaderCompileFailures;
	unsigned int programLinkFailures;
	unsigned int bindAttempts;
	unsigned int binds;
	unsigned int bindFailures;
	unsigned int unbinds;
	unsigned int uniformUpdates;
	unsigned int legacyPasses;
	unsigned int textureBinds;
	unsigned int fogTextureBinds;
	unsigned int shadowTextureBinds;
	unsigned int shadowTextureFallbackBinds;
	unsigned int shadowFboBinds;
	unsigned int shadowFboRestores;
	unsigned int stateChanges;
	unsigned int buildLegacyLights;
	unsigned int buildLegacySkippedLights;
	unsigned int buildLegacyNoHitLights;
	unsigned int buildLegacyVertexes;
	unsigned int buildLegacyIndexes;
	unsigned int buildLegacyLitIndexes;
	unsigned int buildPmPasses;
	unsigned int buildPmVertexes;
	unsigned int buildPmIndexes;
	unsigned int cullLegacyVertexes;
	unsigned int cullLegacyIndexes;
	unsigned int scissorCandidates;
	unsigned int scissorComputed;
	unsigned int scissorApplied;
	unsigned int scissorFallbacks;
	unsigned long long scissorPixels;
	unsigned long long scissorViewportPixels;
	ProjectedDlightRecord projectedSourceRecords[GLX_RENDER_IR_PROJECTED_DLIGHT_RECORD_LIMIT];
	ProjectedDlightRecord projectedListRecords[GLX_PROJECTED_DLIGHT_LIST_ARENA_LIMIT];
	ProjectedDlightListRef projectedPacketListRefs[GLX_STATIC_WORLD_PACKET_LIMIT];
	unsigned int projectedPacketLightMasks[GLX_STATIC_WORLD_PACKET_LIMIT];
	int projectedSourceRecordCount;
	int projectedListRecordCount;
	unsigned int projectedPacketActivePackets;
	unsigned int projectedPacketCopiedMask;
	unsigned int projectedPacketDroppedMask;
	unsigned int projectedSourceFrames;
	unsigned int projectedSourceRecordCopies;
	unsigned int projectedSourceDropped;
	unsigned int projectedListBuilds;
	unsigned int projectedListComplete;
	unsigned int projectedListIncomplete;
	unsigned int projectedListRecordCopies;
	unsigned int projectedListDropped;
	unsigned int projectedListCopiedMask;
	unsigned int projectedListDroppedMask;
	unsigned int projectedPacketItemLookups;
	unsigned int projectedPacketItemLookupHits;
	unsigned int projectedPacketItemLookupMisses;
	unsigned int projectedPacketItemLookupOverflows;
	unsigned int projectedPacketListBuilds;
	unsigned int projectedPacketListComplete;
	unsigned int projectedPacketListIncomplete;
	unsigned int projectedPacketListRecordCopies;
	unsigned int projectedPacketListDropped;
	unsigned int projectedPacketIRProducts;
	unsigned int projectedPacketIRRejected;
	unsigned int projectedDynamicListBuilds;
	unsigned int projectedDynamicListComplete;
	unsigned int projectedDynamicListIncomplete;
	unsigned int projectedDynamicListRecordCopies;
	unsigned int projectedDynamicListDropped;
	unsigned int projectedDynamicIRProducts;
	unsigned int projectedDynamicIRRejected;
	unsigned int projectedShaderInputs;
	unsigned int projectedShaderInputRecords;
	unsigned int projectedShaderWorldPackets;
	unsigned int projectedShaderDynamicDraws;
	unsigned int projectedShaderProgrammableInputs;
	unsigned int projectedShaderFallbackInputs;
	unsigned int projectedShaderInvalidInputs;
	unsigned int projectedShaderUniformAttempts;
	unsigned int projectedShaderUniformBinds;
	unsigned int projectedShaderUniformFailures;
	unsigned int projectedShaderUniformRecords;
	unsigned int projectedShaderUniformTruncated;
	unsigned int projectedShaderUniformExecutableBinds;
	unsigned int projectedShaderUniformSuppressedBinds;
	unsigned int projectedShaderUniformWorldBinds;
	unsigned int projectedShaderUniformWorldExecutableBinds;
	unsigned int projectedShaderUniformDynamicBinds;
	unsigned int projectedShaderUniformDynamicExecutableBinds;
	unsigned int projectedShaderUniformLimitSuppressions;
	unsigned int projectedShaderResourceAttempts;
	unsigned int projectedShaderResourceBinds;
	unsigned int projectedShaderResourceExecutableBinds;
	unsigned int projectedShaderResourceSuppressedBinds;
	unsigned int projectedShaderResourceLimitPromotions;
	unsigned int projectedShaderResourceFailures;
	unsigned int projectedShaderResourceRecords;
	unsigned int projectedShaderResourceWorldExecutableBinds;
	unsigned int projectedShaderResourceDynamicExecutableBinds;
	unsigned int projectedShaderStreamAttempts;
	unsigned int projectedShaderStreamUploads;
	unsigned int projectedShaderStreamFailures;
	unsigned int projectedShaderStreamSkips;
	unsigned int projectedShaderStreamRecords;
	unsigned int projectedShaderStreamPersistentUploads;
	unsigned int projectedShaderStreamWorldUploads;
	unsigned int projectedShaderStreamDynamicUploads;
	unsigned int projectedShaderStreamRangeAttempts;
	unsigned int projectedShaderStreamRangeBinds;
	unsigned int projectedShaderStreamRangeFailures;
	unsigned int projectedShaderStreamRangeClears;
	unsigned int projectedShaderStreamLastOffset;
	unsigned int projectedShaderStreamLastBytes;
	unsigned long long projectedShaderStreamBytes;
	qboolean projectedShaderStreamRangeBound;
	ProjectedDlightStreamRecord
		projectedShaderArenaLightRecords[GLX_RENDER_IR_PROJECTED_DLIGHT_RECORD_LIMIT];
	ProjectedDlightArenaListRecord
		projectedShaderArenaListRecords[GLX_RENDER_IR_PROJECTED_DLIGHT_RECORD_LIMIT];
	unsigned int projectedShaderArenaReserveAttempts;
	unsigned int projectedShaderArenaUploads;
	unsigned int projectedShaderArenaFailures;
	unsigned int projectedShaderArenaWraps;
	unsigned int projectedShaderArenaSameFrameRejects;
	unsigned int projectedShaderArenaWaits;
	unsigned int projectedShaderArenaTimeouts;
	unsigned int projectedShaderArenaSyncFailures;
	unsigned int projectedShaderArenaLightRecordUploads;
	unsigned int projectedShaderArenaListRecordUploads;
	unsigned int projectedShaderArenaWorldRecords;
	unsigned int projectedShaderArenaDynamicRecords;
	unsigned int projectedShaderArenaRangeAttempts;
	unsigned int projectedShaderArenaRangeBinds;
	unsigned int projectedShaderArenaRangeFailures;
	unsigned int projectedShaderArenaRangeClears;
	unsigned int projectedShaderArenaAuthoritativeAttempts;
	unsigned int projectedShaderArenaAuthoritativeBinds;
	unsigned int projectedShaderArenaAuthoritativeFailures;
	unsigned int projectedShaderArenaAuthoritativeFallbacks;
	unsigned int projectedShaderArenaAuthoritativeClears;
	unsigned int projectedShaderArenaFrameBytes;
	unsigned int projectedShaderArenaLastBuffer;
	unsigned int projectedShaderArenaLastOffset;
	unsigned int projectedShaderArenaLastBytes;
	unsigned long long projectedShaderArenaBytes;
	qboolean projectedShaderArenaRangeBound;
	unsigned int projectedShaderMdiAttempts;
	unsigned int projectedShaderMdiEligible;
	unsigned int projectedShaderMdiCommandUploads;
	unsigned int projectedShaderMdiCommandFailures;
	unsigned int projectedShaderMdiCommandSkips;
	unsigned int projectedShaderMdiRecords;
	unsigned int projectedShaderMdiIndexes;
	unsigned int projectedShaderMdiLastOffset;
	unsigned int projectedShaderMdiLastBytes;
	unsigned long long projectedShaderMdiCommandBytes;
	DrawElementsIndirectCommand
		projectedShaderMdiCommandRecords[GLX_PROJECTED_DLIGHT_MDI_COMMAND_RING_LIMIT];
	ProjectedDlightDynamicMdiCommandUpload
		projectedShaderMdiCommandRing[GLX_PROJECTED_DLIGHT_MDI_COMMAND_RING_LIMIT];
	unsigned int projectedShaderMdiCommandRingCursor;
	unsigned int projectedShaderMdiCommandRingReserves;
	unsigned int projectedShaderMdiCommandRingCommits;
	unsigned int projectedShaderMdiCommandRingWraps;
	unsigned int projectedShaderMdiCommandRingFailures;
	unsigned int projectedShaderMdiCommandRingLastSlot;
	unsigned int projectedShaderMdiCommandRingLastBuffer;
	unsigned int projectedShaderMdiCommandRingLastOffset;
	unsigned int projectedShaderMdiCommandRingLastBytes;
	unsigned int projectedShaderMdiSubmitAttempts;
	unsigned int projectedShaderMdiSubmitPlans;
	unsigned int projectedShaderMdiSubmitReady;
	unsigned int projectedShaderMdiSubmitFallbacks;
	unsigned int projectedShaderMdiSubmitSkips;
	unsigned int projectedShaderMdiSubmitRecords;
	unsigned int projectedShaderMdiSubmitIndexes;
	unsigned int projectedShaderMdiSubmitLastBuffer;
	unsigned int projectedShaderMdiSubmitLastOffset;
	unsigned int projectedShaderMdiSubmitLastBytes;
	unsigned int projectedShaderMdiBatchAttempts;
	unsigned int projectedShaderMdiBatchBatches;
	unsigned int projectedShaderMdiBatchReady;
	unsigned int projectedShaderMdiBatchFallbacks;
	unsigned int projectedShaderMdiBatchRejects;
	unsigned int projectedShaderMdiBatchGlErrors;
	unsigned int projectedShaderMdiBatchRecords;
	unsigned int projectedShaderMdiBatchIndexes;
	unsigned int projectedShaderMdiBatchSubmittedDraws;
	unsigned int projectedShaderMdiBatchSubmittedIndexes;
	unsigned int projectedShaderMdiBatchLargest;
	unsigned int projectedShaderMdiBatchLastReject;
	unsigned int projectedShaderMdiBatchLastBuffer;
	unsigned int projectedShaderMdiBatchLastOffset;
	unsigned int projectedShaderMdiBatchLastBytes;
};

static int GLX_Dlight_ClampFogMode( int fogMode )
{
	if ( fogMode < 0 ) {
		return 0;
	}
	if ( fogMode > 2 ) {
		return 2;
	}
	return fogMode;
}

static qboolean GLX_Dlight_KeyEquals( const DlightProgramKey &lhs,
	const DlightProgramKey &rhs )
{
	return lhs.linear == rhs.linear &&
		lhs.fogMode == rhs.fogMode &&
		lhs.absLight == rhs.absLight &&
		lhs.shadow == rhs.shadow &&
		lhs.projectedStream == rhs.projectedStream ? qtrue : qfalse;
}

static void GLX_Dlight_SetReason( DlightState *state, const char *reason )
{
	if ( !state ) {
		return;
	}
	std::snprintf( state->reason, sizeof( state->reason ), "%s",
		reason ? reason : "" );
}

static void GLX_Dlight_ResetCounters( DlightState *state )
{
	if ( !state ) {
		return;
	}

	state->availabilityQueries = 0;
	state->availabilityHits = 0;
	state->availabilityMisses = 0;
	state->programCacheHits = 0;
	state->programCreates = 0;
	state->programCreateFailures = 0;
	state->shaderCompileFailures = 0;
	state->programLinkFailures = 0;
	state->bindAttempts = 0;
	state->binds = 0;
	state->bindFailures = 0;
	state->unbinds = 0;
	state->uniformUpdates = 0;
	state->legacyPasses = 0;
	state->textureBinds = 0;
	state->fogTextureBinds = 0;
	state->shadowTextureBinds = 0;
	state->shadowTextureFallbackBinds = 0;
	state->shadowFboBinds = 0;
	state->shadowFboRestores = 0;
	state->stateChanges = 0;
	state->buildLegacyLights = 0;
	state->buildLegacySkippedLights = 0;
	state->buildLegacyNoHitLights = 0;
	state->buildLegacyVertexes = 0;
	state->buildLegacyIndexes = 0;
	state->buildLegacyLitIndexes = 0;
	state->buildPmPasses = 0;
	state->buildPmVertexes = 0;
	state->buildPmIndexes = 0;
	state->cullLegacyVertexes = 0;
	state->cullLegacyIndexes = 0;
	state->scissorCandidates = 0;
	state->scissorComputed = 0;
	state->scissorApplied = 0;
	state->scissorFallbacks = 0;
	state->scissorPixels = 0;
	state->scissorViewportPixels = 0;
	state->projectedSourceFrames = 0;
	state->projectedSourceRecordCopies = 0;
	state->projectedSourceDropped = 0;
	state->projectedListBuilds = 0;
	state->projectedListComplete = 0;
	state->projectedListIncomplete = 0;
	state->projectedListRecordCopies = 0;
	state->projectedListDropped = 0;
	state->projectedListCopiedMask = 0;
	state->projectedListDroppedMask = 0;
	state->projectedPacketItemLookups = 0;
	state->projectedPacketItemLookupHits = 0;
	state->projectedPacketItemLookupMisses = 0;
	state->projectedPacketItemLookupOverflows = 0;
	state->projectedPacketListBuilds = 0;
	state->projectedPacketListComplete = 0;
	state->projectedPacketListIncomplete = 0;
	state->projectedPacketListRecordCopies = 0;
	state->projectedPacketListDropped = 0;
	state->projectedPacketIRProducts = 0;
	state->projectedPacketIRRejected = 0;
	state->projectedDynamicListBuilds = 0;
	state->projectedDynamicListComplete = 0;
	state->projectedDynamicListIncomplete = 0;
	state->projectedDynamicListRecordCopies = 0;
	state->projectedDynamicListDropped = 0;
	state->projectedDynamicIRProducts = 0;
	state->projectedDynamicIRRejected = 0;
	state->projectedShaderInputs = 0;
	state->projectedShaderInputRecords = 0;
	state->projectedShaderWorldPackets = 0;
	state->projectedShaderDynamicDraws = 0;
	state->projectedShaderProgrammableInputs = 0;
	state->projectedShaderFallbackInputs = 0;
	state->projectedShaderInvalidInputs = 0;
	state->projectedShaderUniformAttempts = 0;
	state->projectedShaderUniformBinds = 0;
	state->projectedShaderUniformFailures = 0;
	state->projectedShaderUniformRecords = 0;
	state->projectedShaderUniformTruncated = 0;
	state->projectedShaderUniformExecutableBinds = 0;
	state->projectedShaderUniformSuppressedBinds = 0;
	state->projectedShaderUniformWorldBinds = 0;
	state->projectedShaderUniformWorldExecutableBinds = 0;
	state->projectedShaderUniformDynamicBinds = 0;
	state->projectedShaderUniformDynamicExecutableBinds = 0;
	state->projectedShaderUniformLimitSuppressions = 0;
	state->projectedShaderResourceAttempts = 0;
	state->projectedShaderResourceBinds = 0;
	state->projectedShaderResourceExecutableBinds = 0;
	state->projectedShaderResourceSuppressedBinds = 0;
	state->projectedShaderResourceLimitPromotions = 0;
	state->projectedShaderResourceFailures = 0;
	state->projectedShaderResourceRecords = 0;
	state->projectedShaderResourceWorldExecutableBinds = 0;
	state->projectedShaderResourceDynamicExecutableBinds = 0;
	state->projectedShaderStreamAttempts = 0;
	state->projectedShaderStreamUploads = 0;
	state->projectedShaderStreamFailures = 0;
	state->projectedShaderStreamSkips = 0;
	state->projectedShaderStreamRecords = 0;
	state->projectedShaderStreamPersistentUploads = 0;
	state->projectedShaderStreamWorldUploads = 0;
	state->projectedShaderStreamDynamicUploads = 0;
	state->projectedShaderStreamRangeAttempts = 0;
	state->projectedShaderStreamRangeBinds = 0;
	state->projectedShaderStreamRangeFailures = 0;
	state->projectedShaderStreamRangeClears = 0;
	state->projectedShaderStreamLastOffset = 0;
	state->projectedShaderStreamLastBytes = 0;
	state->projectedShaderStreamBytes = 0;
	state->projectedShaderStreamRangeBound = qfalse;
	state->projectedShaderArenaReserveAttempts = 0;
	state->projectedShaderArenaUploads = 0;
	state->projectedShaderArenaFailures = 0;
	state->projectedShaderArenaWraps = 0;
	state->projectedShaderArenaSameFrameRejects = 0;
	state->projectedShaderArenaWaits = 0;
	state->projectedShaderArenaTimeouts = 0;
	state->projectedShaderArenaSyncFailures = 0;
	state->projectedShaderArenaLightRecordUploads = 0;
	state->projectedShaderArenaListRecordUploads = 0;
	state->projectedShaderArenaWorldRecords = 0;
	state->projectedShaderArenaDynamicRecords = 0;
	state->projectedShaderArenaRangeAttempts = 0;
	state->projectedShaderArenaRangeBinds = 0;
	state->projectedShaderArenaRangeFailures = 0;
	state->projectedShaderArenaRangeClears = 0;
	state->projectedShaderArenaAuthoritativeAttempts = 0;
	state->projectedShaderArenaAuthoritativeBinds = 0;
	state->projectedShaderArenaAuthoritativeFailures = 0;
	state->projectedShaderArenaAuthoritativeFallbacks = 0;
	state->projectedShaderArenaAuthoritativeClears = 0;
	state->projectedShaderArenaFrameBytes = 0;
	state->projectedShaderArenaLastBuffer = 0;
	state->projectedShaderArenaLastOffset = 0;
	state->projectedShaderArenaLastBytes = 0;
	state->projectedShaderArenaBytes = 0;
	state->projectedShaderArenaRangeBound = qfalse;
	state->projectedShaderMdiAttempts = 0;
	state->projectedShaderMdiEligible = 0;
	state->projectedShaderMdiCommandUploads = 0;
	state->projectedShaderMdiCommandFailures = 0;
	state->projectedShaderMdiCommandSkips = 0;
	state->projectedShaderMdiRecords = 0;
	state->projectedShaderMdiIndexes = 0;
	state->projectedShaderMdiLastOffset = 0;
	state->projectedShaderMdiLastBytes = 0;
	state->projectedShaderMdiCommandBytes = 0;
	state->projectedShaderMdiCommandRingCursor = 0;
	state->projectedShaderMdiCommandRingReserves = 0;
	state->projectedShaderMdiCommandRingCommits = 0;
	state->projectedShaderMdiCommandRingWraps = 0;
	state->projectedShaderMdiCommandRingFailures = 0;
	state->projectedShaderMdiCommandRingLastSlot = 0;
	state->projectedShaderMdiCommandRingLastBuffer = 0;
	state->projectedShaderMdiCommandRingLastOffset = 0;
	state->projectedShaderMdiCommandRingLastBytes = 0;
	state->projectedShaderMdiSubmitAttempts = 0;
	state->projectedShaderMdiSubmitPlans = 0;
	state->projectedShaderMdiSubmitReady = 0;
	state->projectedShaderMdiSubmitFallbacks = 0;
	state->projectedShaderMdiSubmitSkips = 0;
	state->projectedShaderMdiSubmitRecords = 0;
	state->projectedShaderMdiSubmitIndexes = 0;
	state->projectedShaderMdiSubmitLastBuffer = 0;
	state->projectedShaderMdiSubmitLastOffset = 0;
	state->projectedShaderMdiSubmitLastBytes = 0;
	state->projectedShaderMdiBatchAttempts = 0;
	state->projectedShaderMdiBatchBatches = 0;
	state->projectedShaderMdiBatchReady = 0;
	state->projectedShaderMdiBatchFallbacks = 0;
	state->projectedShaderMdiBatchRejects = 0;
	state->projectedShaderMdiBatchGlErrors = 0;
	state->projectedShaderMdiBatchRecords = 0;
	state->projectedShaderMdiBatchIndexes = 0;
	state->projectedShaderMdiBatchSubmittedDraws = 0;
	state->projectedShaderMdiBatchSubmittedIndexes = 0;
	state->projectedShaderMdiBatchLargest = 0;
	state->projectedShaderMdiBatchLastReject =
		GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_NONE;
	state->projectedShaderMdiBatchLastBuffer = 0;
	state->projectedShaderMdiBatchLastOffset = 0;
	state->projectedShaderMdiBatchLastBytes = 0;
	state->projectedSourceRecordCount = 0;
	state->projectedListRecordCount = 0;
	state->projectedPacketActivePackets = 0;
	state->projectedPacketCopiedMask = 0;
	state->projectedPacketDroppedMask = 0;
	for ( unsigned int i = 0; i < GLX_PROJECTED_DLIGHT_MDI_COMMAND_RING_LIMIT; i++ ) {
		state->projectedShaderMdiCommandRecords[i] = {};
		state->projectedShaderMdiCommandRing[i] = {};
	}
	for ( int i = 0; i < GLX_RENDER_IR_PROJECTED_DLIGHT_RECORD_LIMIT; i++ ) {
		state->projectedShaderArenaLightRecords[i] = {};
		state->projectedShaderArenaListRecords[i] = {};
	}
}

static void GLX_Dlight_ClearProjectedFrame( DlightState *state )
{
	if ( !state ) {
		return;
	}
	state->projectedSourceRecordCount = 0;
	state->projectedListRecordCount = 0;
	state->projectedListCopiedMask = 0;
	state->projectedListDroppedMask = 0;
	state->projectedPacketActivePackets = 0;
	state->projectedPacketCopiedMask = 0;
	state->projectedPacketDroppedMask = 0;
	state->projectedShaderArenaFrameBytes = 0;
	state->projectedShaderMdiCommandRingCursor = 0;
	for ( int i = 0; i < GLX_STATIC_WORLD_PACKET_LIMIT; i++ ) {
		state->projectedPacketListRefs[i] = {};
		state->projectedPacketLightMasks[i] = 0u;
	}
}

static ProjectedDlightDynamicMdiCommandUpload *
GLX_Dlight_ReserveProjectedMdiCommandRingSlot( DlightState *state,
	unsigned int *slotOut )
{
	unsigned int slot;

	if ( !state ) {
		return nullptr;
	}

	state->projectedShaderMdiCommandRingReserves++;
	if ( state->projectedShaderMdiCommandRingCursor >=
			GLX_PROJECTED_DLIGHT_MDI_COMMAND_RING_LIMIT ) {
		state->projectedShaderMdiCommandRingCursor = 0;
		state->projectedShaderMdiCommandRingWraps++;
	}

	slot = state->projectedShaderMdiCommandRingCursor++;
	state->projectedShaderMdiCommandRingLastSlot = slot;
	state->projectedShaderMdiCommandRecords[slot] = {};
	state->projectedShaderMdiCommandRing[slot] = {};
	if ( slotOut ) {
		*slotOut = slot;
	}
	return &state->projectedShaderMdiCommandRing[slot];
}

static void GLX_Dlight_RecordState( DlightState *state, int event )
{
	if ( !state ) {
		return;
	}

	switch ( event ) {
		case GLX_DLIGHT_STATE_LEGACY_PASS:
			state->legacyPasses++;
			break;
		case GLX_DLIGHT_STATE_TEXTURE_BIND:
			state->textureBinds++;
			break;
		case GLX_DLIGHT_STATE_FOG_TEXTURE_BIND:
			state->fogTextureBinds++;
			break;
		case GLX_DLIGHT_STATE_SHADOW_TEXTURE_BIND:
			state->shadowTextureBinds++;
			break;
		case GLX_DLIGHT_STATE_SHADOW_TEXTURE_FALLBACK_BIND:
			state->shadowTextureFallbackBinds++;
			break;
		case GLX_DLIGHT_STATE_SHADOW_FBO_BIND:
			state->shadowFboBinds++;
			break;
		case GLX_DLIGHT_STATE_SHADOW_FBO_RESTORE:
			state->shadowFboRestores++;
			break;
		case GLX_DLIGHT_STATE_GL_STATE:
			state->stateChanges++;
			break;
		default:
			break;
	}
}

static unsigned int GLX_Dlight_PositiveCounter( int value )
{
	return value > 0 ? static_cast<unsigned int>( value ) : 0u;
}

static void GLX_Dlight_RecordBuild( DlightState *state, int legacyLights,
	int legacySkippedLights, int legacyNoHitLights, int legacyVertexes,
	int legacyIndexes, int legacyLitIndexes, int pmPasses, int pmVertexes,
	int pmIndexes )
{
	if ( !state ) {
		return;
	}

	state->buildLegacyLights += GLX_Dlight_PositiveCounter( legacyLights );
	state->buildLegacySkippedLights += GLX_Dlight_PositiveCounter( legacySkippedLights );
	state->buildLegacyNoHitLights += GLX_Dlight_PositiveCounter( legacyNoHitLights );
	state->buildLegacyVertexes += GLX_Dlight_PositiveCounter( legacyVertexes );
	state->buildLegacyIndexes += GLX_Dlight_PositiveCounter( legacyIndexes );
	state->buildLegacyLitIndexes += GLX_Dlight_PositiveCounter( legacyLitIndexes );
	state->buildPmPasses += GLX_Dlight_PositiveCounter( pmPasses );
	state->buildPmVertexes += GLX_Dlight_PositiveCounter( pmVertexes );
	state->buildPmIndexes += GLX_Dlight_PositiveCounter( pmIndexes );
}

static void GLX_Dlight_RecordCull( DlightState *state, int legacyVertexes,
	int legacyIndexes )
{
	if ( !state ) {
		return;
	}

	state->cullLegacyVertexes += GLX_Dlight_PositiveCounter( legacyVertexes );
	state->cullLegacyIndexes += GLX_Dlight_PositiveCounter( legacyIndexes );
}

static qboolean GLX_Dlight_ScissorEnabled( const DlightState &state )
{
	if ( !state.scissor ) {
		return qfalse;
	}
	if ( state.scissor->integer ) {
		return qtrue;
	}
	if ( state.scissor->string &&
		!GLX_Module_Stricmp( state.scissor->string, "auto" ) ) {
		return qtrue;
	}
	return qfalse;
}

static qboolean GLX_Dlight_ProjectedProgramEnabled( const DlightState &state )
{
	return state.projectedProgram && state.projectedProgram->integer ? qtrue : qfalse;
}

static qboolean GLX_Dlight_ProjectedStreamShaderEnabled( const DlightState &state,
	RenderProductTier tier )
{
	return tier == RenderProductTier::GL46 &&
		GLX_Dlight_ProjectedProgramEnabled( state ) &&
		state.fns.BindBufferRange ? qtrue : qfalse;
}

static qboolean GLX_Dlight_ProjectedMdiEnabled( const DlightState &state )
{
	return state.projectedMdi && state.projectedMdi->integer ? qtrue : qfalse;
}

static void GLX_Dlight_RecordProjectedMdiSubmitPlan( DlightState *state,
	const ProjectedDlightDynamicMdiSubmitPlan &plan )
{
	if ( !state ) {
		return;
	}

	state->projectedShaderMdiSubmitAttempts++;
	if ( !plan.valid ) {
		state->projectedShaderMdiSubmitSkips++;
		return;
	}

	state->projectedShaderMdiSubmitPlans++;
	state->projectedShaderMdiSubmitRecords += plan.projectedRecordCount;
	state->projectedShaderMdiSubmitIndexes += plan.indexCount;
	state->projectedShaderMdiSubmitLastBuffer = plan.commandBuffer;
	state->projectedShaderMdiSubmitLastOffset = plan.commandOffset;
	state->projectedShaderMdiSubmitLastBytes = plan.commandBytes;
	if ( !plan.eligible ) {
		state->projectedShaderMdiSubmitFallbacks++;
		return;
	}

	state->projectedShaderMdiSubmitReady++;
}

static void GLX_Dlight_RecordProjectedMdiBatchPlan( DlightState *state,
	const ProjectedDlightDynamicMdiBatchPlan &batch )
{
	if ( !state ) {
		return;
	}

	state->projectedShaderMdiBatchAttempts++;
	if ( !batch.valid ) {
		state->projectedShaderMdiBatchRejects++;
		state->projectedShaderMdiBatchLastReject = batch.rejectReason;
		return;
	}

	state->projectedShaderMdiBatchBatches++;
	state->projectedShaderMdiBatchRecords += batch.projectedRecordCount;
	state->projectedShaderMdiBatchIndexes += batch.indexCount;
	state->projectedShaderMdiBatchLastReject = batch.rejectReason;
	state->projectedShaderMdiBatchLastBuffer = batch.commandBuffer;
	state->projectedShaderMdiBatchLastOffset = batch.commandOffset;
	state->projectedShaderMdiBatchLastBytes = batch.commandBytes;
	if ( batch.drawCount > state->projectedShaderMdiBatchLargest ) {
		state->projectedShaderMdiBatchLargest = batch.drawCount;
	}
	if ( batch.rejectReason !=
			GLX_RENDER_IR_PROJECTED_DLIGHT_MDI_BATCH_REJECT_NONE ) {
		state->projectedShaderMdiBatchRejects++;
	}
	if ( !batch.eligible ) {
		state->projectedShaderMdiBatchFallbacks++;
		return;
	}

	state->projectedShaderMdiBatchReady++;
}

static void GLX_Dlight_RecordScissor( DlightState *state, qboolean computed,
	qboolean applied, int width, int height, int viewportWidth, int viewportHeight )
{
	unsigned long long pixels;
	unsigned long long viewportPixels;

	if ( !state ) {
		return;
	}

	state->scissorCandidates++;
	if ( !computed || width <= 0 || height <= 0 ||
		viewportWidth <= 0 || viewportHeight <= 0 ) {
		state->scissorFallbacks++;
		return;
	}

	pixels = static_cast<unsigned long long>( width ) *
		static_cast<unsigned long long>( height );
	viewportPixels = static_cast<unsigned long long>( viewportWidth ) *
		static_cast<unsigned long long>( viewportHeight );
	state->scissorComputed++;
	state->scissorPixels += pixels;
	state->scissorViewportPixels += viewportPixels;
	if ( applied ) {
		state->scissorApplied++;
	}
}

static ProjectedDlightRecord GLX_Dlight_ProjectRecordFromPublic(
	const glxProjectedDlightRecord_t &source )
{
	ProjectedDlightRecord record {};

	for ( int i = 0; i < 3; i++ ) {
		record.origin[i] = source.origin[i];
		record.color[i] = source.color[i];
	}
	record.radius = source.radius;
	if ( source.flags & GLX_PROJECTED_DLIGHT_FLAG_ADDITIVE ) {
		record.flags |= GLX_PROJECTED_DLIGHT_ADDITIVE;
	}
	if ( source.flags & GLX_PROJECTED_DLIGHT_FLAG_LINEAR ) {
		record.flags |= GLX_PROJECTED_DLIGHT_LINEAR;
	}
	if ( source.flags & GLX_PROJECTED_DLIGHT_FLAG_FRONT_CULL ) {
		record.flags |= GLX_PROJECTED_DLIGHT_FRONT_CULL;
	}
	record.flags |= ( source.flags &
		~( GLX_PROJECTED_DLIGHT_FLAG_ADDITIVE | GLX_PROJECTED_DLIGHT_FLAG_LINEAR |
			GLX_PROJECTED_DLIGHT_FLAG_FRONT_CULL ) );
	return record;
}

static qboolean GLX_Dlight_ProjectedRecordsEqual( const ProjectedDlightRecord &a,
	const ProjectedDlightRecord &b )
{
	for ( int i = 0; i < 3; i++ ) {
		if ( a.origin[i] != b.origin[i] || a.color[i] != b.color[i] ) {
			return qfalse;
		}
	}
	return a.radius == b.radius && a.flags == b.flags ? qtrue : qfalse;
}

/*
Every view records here (portal and mirror passes included) while the packet
light lists of earlier views are still queued for end-of-frame backend
execution, so an unchanged light set must keep the accumulated frame state:
packet masks then merge across views. Only a changed set resets, because the
packet light masks are bit indexes into this source table. The frame state is
cleared in FrameComplete once the backend has consumed it.
*/
static void GLX_Dlight_RecordProjectedDlights( DlightState *state,
	const glxProjectedDlightRecord_t *records, int count )
{
	ProjectedDlightRecord validated[GLX_RENDER_IR_PROJECTED_DLIGHT_RECORD_LIMIT];
	int validatedCount = 0;
	qboolean unchanged;

	if ( !state ) {
		return;
	}

	state->projectedSourceFrames++;
	if ( count > 0 && records ) {
		if ( count > GLX_RENDER_IR_PROJECTED_DLIGHT_RECORD_LIMIT ) {
			state->projectedSourceDropped +=
				static_cast<unsigned int>( count - GLX_RENDER_IR_PROJECTED_DLIGHT_RECORD_LIMIT );
			count = GLX_RENDER_IR_PROJECTED_DLIGHT_RECORD_LIMIT;
		}
		for ( int i = 0; i < count; i++ ) {
			const ProjectedDlightRecord record = GLX_Dlight_ProjectRecordFromPublic( records[i] );

			if ( !GLX_RenderIR_ValidateProjectedDlightRecord( record ) ) {
				state->projectedSourceDropped++;
				continue;
			}
			validated[validatedCount++] = record;
		}
	}

	if ( validatedCount <= 0 ) {
		return;
	}

	unchanged = validatedCount == state->projectedSourceRecordCount ? qtrue : qfalse;
	for ( int i = 0; unchanged && i < validatedCount; i++ ) {
		unchanged = GLX_Dlight_ProjectedRecordsEqual( validated[i],
			state->projectedSourceRecords[i] );
	}
	if ( unchanged ) {
		return;
	}

	GLX_Dlight_ClearProjectedFrame( state );
	for ( int i = 0; i < validatedCount; i++ ) {
		state->projectedSourceRecords[state->projectedSourceRecordCount++] = validated[i];
		state->projectedSourceRecordCopies++;
	}
}

static ProjectedDlightListBuildResult GLX_Dlight_CopyProjectedDlightList(
	DlightState *state, unsigned int lightMask )
{
	ProjectedDlightListBuildResult result {};

	result.complete = qtrue;
	if ( !state || !lightMask ) {
		return result;
	}

	if ( state->projectedSourceRecordCount <= 0 ||
		state->projectedListRecordCount >= GLX_PROJECTED_DLIGHT_LIST_ARENA_LIMIT ) {
		result.droppedMask = lightMask;
		result.complete = qfalse;
		return result;
	}

	result = GLX_RenderIR_BuildProjectedDlightList( state->projectedSourceRecords,
		state->projectedSourceRecordCount, lightMask, state->projectedListRecords,
		GLX_PROJECTED_DLIGHT_LIST_ARENA_LIMIT, state->projectedListRecordCount );

	state->projectedListRecordCount += result.ref.recordCount;
	state->projectedListRecordCopies += static_cast<unsigned int>( result.ref.recordCount );
	state->projectedListCopiedMask |= result.copiedMask;
	state->projectedListDroppedMask |= result.droppedMask;
	return result;
}

static int GLX_Dlight_StaticPacketForItem( DlightState *state,
	const StaticWorldStats *staticWorld, int itemIndex )
{
	int packetIndex;

	if ( state ) {
		state->projectedPacketItemLookups++;
	}
	if ( !staticWorld || itemIndex <= 0 ) {
		if ( state ) {
			state->projectedPacketItemLookupMisses++;
		}
		return -1;
	}
	if ( itemIndex > GLX_STATIC_WORLD_ITEM_LOOKUP_LIMIT ) {
		if ( state ) {
			state->projectedPacketItemLookupOverflows++;
		}
		return -1;
	}

	packetIndex = staticWorld->itemToPacket[itemIndex];
	if ( packetIndex < 0 || packetIndex >= staticWorld->packetCount ||
		packetIndex >= GLX_STATIC_WORLD_PACKET_LIMIT ) {
		if ( state ) {
			state->projectedPacketItemLookupMisses++;
		}
		return -1;
	}

	if ( state ) {
		state->projectedPacketItemLookupHits++;
	}
	return packetIndex;
}

static void GLX_Dlight_RecordProjectedPacketDlightList( DlightState *state,
	const StaticWorldStats *staticWorld, int itemIndex, unsigned int lightMask )
{
	ProjectedDlightListBuildResult result;
	unsigned int mergedMask;
	int packetIndex;

	if ( !state || !lightMask ) {
		return;
	}

	packetIndex = GLX_Dlight_StaticPacketForItem( state, staticWorld, itemIndex );
	if ( packetIndex < 0 ) {
		return;
	}

	mergedMask = state->projectedPacketLightMasks[packetIndex] | lightMask;
	if ( mergedMask == state->projectedPacketLightMasks[packetIndex] ) {
		return;
	}

	state->projectedPacketListBuilds++;
	result = GLX_Dlight_CopyProjectedDlightList( state, mergedMask );
	if ( result.ref.recordCount > 0 ) {
		if ( state->projectedPacketLightMasks[packetIndex] == 0u ) {
			state->projectedPacketActivePackets++;
		}
		state->projectedPacketLightMasks[packetIndex] = result.copiedMask;
		state->projectedPacketListRefs[packetIndex] = result.ref;
		state->projectedPacketCopiedMask |= result.copiedMask;
		state->projectedPacketListRecordCopies +=
			static_cast<unsigned int>( result.ref.recordCount );
	}
	state->projectedPacketDroppedMask |= result.droppedMask;
	if ( result.droppedMask ) {
		state->projectedPacketListDropped++;
	}
	if ( result.complete ) {
		state->projectedPacketListComplete++;
	} else {
		state->projectedPacketListIncomplete++;
	}
}

static void GLX_Dlight_RecordProjectedDlightList( DlightState *state,
	const StaticWorldStats *staticWorld, int itemIndex, unsigned int lightMask )
{
	ProjectedDlightListBuildResult result;

	if ( !state || !lightMask ) {
		return;
	}

	state->projectedListBuilds++;
	result = GLX_Dlight_CopyProjectedDlightList( state, lightMask );
	if ( result.droppedMask ) {
		state->projectedListDropped++;
	}
	if ( result.complete ) {
		state->projectedListComplete++;
	} else {
		state->projectedListIncomplete++;
	}

	GLX_Dlight_RecordProjectedPacketDlightList( state, staticWorld, itemIndex,
		result.copiedMask );
}

static qboolean GLX_Dlight_Active( const DlightState &state );

static qboolean GLX_Dlight_ProjectedShaderProgrammable(
	const DlightState &state, RenderProductTier tier )
{
	const TierExecutionPolicy policy = GLX_RenderIR_TierExecutionPolicy( tier );

	return GLX_Dlight_Active( state ) && policy.materialCompiler &&
		policy.dynamicLights ? qtrue : qfalse;
}

static qboolean GLX_Dlight_RecordProjectedShaderInput( DlightState *state,
	const ProjectedDlightShaderInput &input )
{
	if ( !state ) {
		return qfalse;
	}

	if ( !GLX_RenderIR_ValidateProjectedDlightShaderInput( input ) ) {
		state->projectedShaderInvalidInputs++;
		return qfalse;
	}

	state->projectedShaderInputs++;
	state->projectedShaderInputRecords +=
		static_cast<unsigned int>( input.projectedDlights.recordCount );
	if ( input.target == ProjectedDlightShaderTarget::WorldPacket ) {
		state->projectedShaderWorldPackets++;
	} else if ( input.target == ProjectedDlightShaderTarget::DynamicDraw ) {
		state->projectedShaderDynamicDraws++;
	}
	if ( input.programmable ) {
		state->projectedShaderProgrammableInputs++;
	} else {
		state->projectedShaderFallbackInputs++;
	}
	return input.programmable;
}

static ProjectedDlightListRef GLX_Dlight_BuildProjectedDynamicDlightList(
	DlightState *state, unsigned int lightMask )
{
	ProjectedDlightListBuildResult result;

	if ( !state || !lightMask ) {
		return {};
	}

	state->projectedDynamicListBuilds++;
	result = GLX_Dlight_CopyProjectedDlightList( state, lightMask );
	state->projectedDynamicListRecordCopies +=
		static_cast<unsigned int>( result.ref.recordCount );
	if ( result.droppedMask ) {
		state->projectedDynamicListDropped++;
	}
	if ( result.complete ) {
		state->projectedDynamicListComplete++;
	} else {
		state->projectedDynamicListIncomplete++;
	}
	return result.ref;
}

static void *GLX_Dlight_GetProc( const char *name )
{
	return RI().GL_GetProcAddress ? RI().GL_GetProcAddress( name ) : nullptr;
}

static void GLX_Dlight_LoadFunctions( DlightState *state )
{
	if ( !state ) {
		return;
	}

	state->fns.CreateShader = reinterpret_cast<PFNGLXCREATESHADERPROC>( GLX_Dlight_GetProc( "glCreateShader" ) );
	state->fns.ShaderSource = reinterpret_cast<PFNGLXSHADERSOURCEPROC>( GLX_Dlight_GetProc( "glShaderSource" ) );
	state->fns.CompileShader = reinterpret_cast<PFNGLXCOMPILESHADERPROC>( GLX_Dlight_GetProc( "glCompileShader" ) );
	state->fns.GetShaderiv = reinterpret_cast<PFNGLXGETSHADERIVPROC>( GLX_Dlight_GetProc( "glGetShaderiv" ) );
	state->fns.GetShaderInfoLog = reinterpret_cast<PFNGLXGETSHADERINFOLOGPROC>( GLX_Dlight_GetProc( "glGetShaderInfoLog" ) );
	state->fns.CreateProgram = reinterpret_cast<PFNGLXCREATEPROGRAMPROC>( GLX_Dlight_GetProc( "glCreateProgram" ) );
	state->fns.AttachShader = reinterpret_cast<PFNGLXATTACHSHADERPROC>( GLX_Dlight_GetProc( "glAttachShader" ) );
	state->fns.LinkProgram = reinterpret_cast<PFNGLXLINKPROGRAMPROC>( GLX_Dlight_GetProc( "glLinkProgram" ) );
	state->fns.GetProgramiv = reinterpret_cast<PFNGLXGETPROGRAMIVPROC>( GLX_Dlight_GetProc( "glGetProgramiv" ) );
	state->fns.GetProgramInfoLog = reinterpret_cast<PFNGLXGETPROGRAMINFOLOGPROC>( GLX_Dlight_GetProc( "glGetProgramInfoLog" ) );
	state->fns.UseProgram = reinterpret_cast<PFNGLXUSEPROGRAMPROC>( GLX_Dlight_GetProc( "glUseProgram" ) );
	state->fns.GetUniformLocation = reinterpret_cast<PFNGLXGETUNIFORMLOCATIONPROC>( GLX_Dlight_GetProc( "glGetUniformLocation" ) );
	state->fns.Uniform1i = reinterpret_cast<PFNGLXUNIFORM1IPROC>( GLX_Dlight_GetProc( "glUniform1i" ) );
	state->fns.Uniform1f = reinterpret_cast<PFNGLXUNIFORM1FPROC>( GLX_Dlight_GetProc( "glUniform1f" ) );
	state->fns.Uniform4fv = reinterpret_cast<PFNGLXUNIFORM4FVPROC>( GLX_Dlight_GetProc( "glUniform4fv" ) );
	state->fns.BindBufferRange = reinterpret_cast<PFNGLXBINDBUFFERRANGEPROC>(
		GLX_Dlight_GetProc( "glBindBufferRange" ) );
	state->fns.DeleteProgram = reinterpret_cast<PFNGLXDELETEPROGRAMPROC>( GLX_Dlight_GetProc( "glDeleteProgram" ) );
	state->fns.DeleteShader = reinterpret_cast<PFNGLXDELETESHADERPROC>( GLX_Dlight_GetProc( "glDeleteShader" ) );
}

static qboolean GLX_Dlight_ResolveProjectedMdiFns()
{
	if ( s_projectedDlightMdiFns.BindBuffer &&
		s_projectedDlightMdiFns.GetIntegerv &&
		s_projectedDlightMdiFns.GetError &&
		s_projectedDlightMdiFns.MultiDrawElementsIndirect ) {
		return qtrue;
	}

	s_projectedDlightMdiFns.BindBuffer =
		reinterpret_cast<PFNGLXDLIGHTBINDBUFFERPROC>(
			GLX_Dlight_GetProc( "glBindBuffer" ) );
	if ( !s_projectedDlightMdiFns.BindBuffer ) {
		s_projectedDlightMdiFns.BindBuffer =
			reinterpret_cast<PFNGLXDLIGHTBINDBUFFERPROC>(
				GLX_Dlight_GetProc( "glBindBufferARB" ) );
	}
	s_projectedDlightMdiFns.GetIntegerv =
		reinterpret_cast<PFNGLXDLIGHTGETINTEGERVPROC>(
			GLX_Dlight_GetProc( "glGetIntegerv" ) );
	s_projectedDlightMdiFns.GetError =
		reinterpret_cast<PFNGLXDLIGHTGETERRORPROC>(
			GLX_Dlight_GetProc( "glGetError" ) );
	s_projectedDlightMdiFns.MultiDrawElementsIndirect =
		reinterpret_cast<PFNGLXDLIGHTMULTIDRAWELEMENTSINDIRECTPROC>(
			GLX_Dlight_GetProc( "glMultiDrawElementsIndirect" ) );

	return s_projectedDlightMdiFns.BindBuffer &&
		s_projectedDlightMdiFns.GetIntegerv &&
		s_projectedDlightMdiFns.GetError &&
		s_projectedDlightMdiFns.MultiDrawElementsIndirect ? qtrue : qfalse;
}

static GLuint GLX_Dlight_CurrentDrawIndirectBuffer()
{
	GLint current = 0;

	if ( s_projectedDlightMdiFns.GetIntegerv ) {
		s_projectedDlightMdiFns.GetIntegerv( GL_DRAW_INDIRECT_BUFFER_BINDING,
			&current );
	}
	return static_cast<GLuint>( current );
}

static void GLX_Dlight_ClearMdiGLErrors()
{
	if ( !s_projectedDlightMdiFns.GetError ) {
		return;
	}

	for ( int i = 0; i < 8 && s_projectedDlightMdiFns.GetError() != GL_NO_ERROR; i++ ) {
	}
}

static void GLX_Dlight_BindDrawIndirectBuffer( GLuint buffer )
{
	if ( s_projectedDlightMdiFns.BindBuffer ) {
		s_projectedDlightMdiFns.BindBuffer( GL_DRAW_INDIRECT_BUFFER, buffer );
	}
}

static qboolean GLX_Dlight_SubmitProjectedMdiBatch( DlightState *state,
	const ProjectedDlightDynamicMdiBatchPlan &batch )
{
	GLuint oldDrawIndirectBuffer;
	GLenum err;
	const GLvoid *commandOffset;

	if ( !state || !batch.valid || !batch.eligible || batch.drawCount == 0u ||
		batch.commandBuffer == 0u || batch.commandBytes == 0u ||
		batch.commandStride == 0u || batch.indexBuffer == 0u ) {
		if ( state ) {
			state->projectedShaderMdiBatchFallbacks++;
		}
		return qfalse;
	}
	if ( !GLX_Dlight_ResolveProjectedMdiFns() ) {
		state->projectedShaderMdiBatchFallbacks++;
		return qfalse;
	}

	commandOffset = reinterpret_cast<const GLvoid *>(
		static_cast<intptr_t>( batch.commandOffset ) );
	oldDrawIndirectBuffer = GLX_Dlight_CurrentDrawIndirectBuffer();
	/*
	Drain every pending error category: one stale flag from earlier legacy
	GL work would be misread as an MDI failure below and trigger a second,
	additive fallback draw of the same geometry.
	*/
	GLX_Dlight_ClearMdiGLErrors();
	GLX_Dlight_BindDrawIndirectBuffer( batch.commandBuffer );
	s_projectedDlightMdiFns.MultiDrawElementsIndirect(
		static_cast<GLenum>( batch.primitive ),
		static_cast<GLenum>( batch.indexType ), commandOffset,
		static_cast<GLsizei>( batch.drawCount ),
		static_cast<GLsizei>( batch.commandStride ) );
	err = s_projectedDlightMdiFns.GetError();
	GLX_Dlight_BindDrawIndirectBuffer( oldDrawIndirectBuffer );

	if ( err != GL_NO_ERROR ) {
		state->projectedShaderMdiBatchGlErrors++;
		state->projectedShaderMdiBatchFallbacks++;
		return qfalse;
	}

	state->projectedShaderMdiBatchSubmittedDraws += batch.drawCount;
	state->projectedShaderMdiBatchSubmittedIndexes += batch.indexCount;
	return qtrue;
}

static qboolean GLX_Dlight_FunctionsReady( const DlightState &state )
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
		state.fns.Uniform1f &&
		state.fns.Uniform4fv &&
		state.fns.DeleteProgram &&
		state.fns.DeleteShader ? qtrue : qfalse;
}

static qboolean GLX_Dlight_SourceFits( int written, size_t capacity )
{
	return written > 0 && static_cast<size_t>( written ) < capacity ? qtrue : qfalse;
}

static GLint GLX_Dlight_GetUniformLocationAny( const DlightState *state, GLuint program,
	const char *primaryName, const char *fallbackName )
{
	GLint location;

	if ( !state || !state->fns.GetUniformLocation || !program || !primaryName ) {
		return -1;
	}

	location = state->fns.GetUniformLocation( program, primaryName );
	if ( location < 0 && fallbackName ) {
		location = state->fns.GetUniformLocation( program, fallbackName );
	}
	return location;
}

static qboolean GLX_Dlight_VertexSource( const DlightProgramKey &key,
	char *out, size_t outSize )
{
	const char *version = key.projectedStream ?
		"#version 430 compatibility\n" : "#version 120\n";
	const char *varyingOut = key.projectedStream ? "out" : "varying";
	const char *positionExpr = key.projectedStream ?
		"gl_ModelViewProjectionMatrix * pos" : "ftransform()";
	const int written = std::snprintf( out, outSize,
		"%s"
		"#define GLX_DLIGHT_FOG %i\n"
		"#define GLX_DLIGHT_FOG_OUTSIDE %i\n"
		"uniform vec4 u_EyePos;\n"
		"uniform vec4 u_LightPos;\n"
		"uniform vec4 u_FogDistanceVector;\n"
		"uniform vec4 u_FogDepthVector;\n"
		"uniform float u_FogEyeT;\n"
		"%s vec2 v_TexCoord;\n"
		"%s vec3 v_LocalPos;\n"
		"%s vec3 v_LightVector;\n"
		"%s vec3 v_ViewVector;\n"
		"%s vec3 v_Normal;\n"
		"#if GLX_DLIGHT_FOG\n"
		"%s vec2 v_FogTexCoord;\n"
		"#endif\n"
		"void main(void) {\n"
		"    vec4 pos = gl_Vertex;\n"
		"    gl_Position = %s;\n"
		"    v_TexCoord = gl_MultiTexCoord0.xy;\n"
		"    v_LocalPos = pos.xyz;\n"
		"    v_LightVector = u_LightPos.xyz - pos.xyz;\n"
		"    v_ViewVector = u_EyePos.xyz - pos.xyz;\n"
		"    v_Normal = gl_Normal.xyz;\n"
		"#if GLX_DLIGHT_FOG\n"
		"    float s = dot(pos.xyz, u_FogDistanceVector.xyz) + u_FogDistanceVector.w;\n"
		"    float t = dot(pos.xyz, u_FogDepthVector.xyz) + u_FogDepthVector.w;\n"
		"#if GLX_DLIGHT_FOG_OUTSIDE\n"
		"    if (t < 1.0) {\n"
		"        t = 0.03125;\n"
		"    } else {\n"
		"        t = 0.03125 + 0.9375 * t / (t - u_FogEyeT);\n"
		"    }\n"
		"#else\n"
		"    t = t < 0.0 ? 0.03125 : 0.96875;\n"
		"#endif\n"
		"    v_FogTexCoord = vec2(s, t);\n"
		"#endif\n"
		"}\n",
		version,
		key.fogMode != 0 ? 1 : 0,
		key.fogMode == 1 ? 1 : 0,
		varyingOut,
		varyingOut,
		varyingOut,
		varyingOut,
		varyingOut,
		varyingOut,
		positionExpr );
	return GLX_Dlight_SourceFits( written, outSize );
}

static qboolean GLX_Dlight_FragmentSource( const DlightProgramKey &key,
	char *out, size_t outSize )
{
	const char *version = key.projectedStream ?
		"#version 430 compatibility\n"
		"#define GLX_TEX2D texture\n"
		"#define GLX_FRAG_COLOR o_FragColor\n" :
		"#version 120\n"
		"#define GLX_TEX2D texture2D\n"
		"#define GLX_FRAG_COLOR gl_FragColor\n";
	const char *varyingIn = key.projectedStream ? "in" : "varying";
	const char *fragmentOutput = key.projectedStream ?
		"out vec4 o_FragColor;\n" : "";
	const char *streamBlock = key.projectedStream ?
		"struct GLXProjectedDlightRecord {\n"
		"    vec4 originRadius;\n"
		"    vec4 colorFlags;\n"
		"};\n"
		"layout(std430, binding = GLX_PROJECTED_DLIGHT_STREAM_BINDING) readonly buffer GLXProjectedDlightStream {\n"
		"    GLXProjectedDlightRecord u_ProjectedDlightRecords[];\n"
		"};\n" : "";
	const int written = std::snprintf( out, outSize,
		"%s"
		"#define GLX_DLIGHT_LINEAR %i\n"
		"#define GLX_DLIGHT_FOG %i\n"
		"#define GLX_DLIGHT_ABS_LIGHT %i\n"
		"#define GLX_DLIGHT_SHADOW %i\n"
		"#define GLX_PROJECTED_DLIGHT_UNIFORM_LIMIT %i\n"
		"#define GLX_PROJECTED_DLIGHT_LOOP_LIMIT %i\n"
		"#define GLX_PROJECTED_DLIGHT_STREAM %i\n"
		"#define GLX_PROJECTED_DLIGHT_STREAM_BINDING %u\n"
		"%s"
		"uniform sampler2D u_Texture0;\n"
		"uniform sampler2D u_FogTexture;\n"
		"uniform sampler2D u_ShadowTexture;\n"
		"uniform vec4 u_LightColor;\n"
		"uniform vec4 u_LightVector;\n"
		"uniform vec4 u_TexFactors;\n"
		"uniform vec4 u_DlightFactors;\n"
		"uniform vec4 u_DlightShadow;\n"
		"uniform vec4 u_ShadowAtlas;\n"
		"uniform vec4 u_ShadowDepth;\n"
		"uniform vec4 u_ShadowFilter;\n"
		"uniform vec4 u_ProjectedDlightControl;\n"
		"uniform vec4 u_ProjectedDlightOriginRadius[GLX_PROJECTED_DLIGHT_UNIFORM_LIMIT];\n"
		"uniform vec4 u_ProjectedDlightColorFlags[GLX_PROJECTED_DLIGHT_UNIFORM_LIMIT];\n"
		"%s vec2 v_TexCoord;\n"
		"%s vec3 v_LocalPos;\n"
		"%s vec3 v_LightVector;\n"
		"%s vec3 v_ViewVector;\n"
		"%s vec3 v_Normal;\n"
		"#if GLX_DLIGHT_FOG\n"
		"%s vec2 v_FogTexCoord;\n"
		"#endif\n"
		"%s"
		"vec3 safeNormalize(vec3 value) {\n"
		"    return value * inversesqrt(max(dot(value, value), 0.00000001));\n"
		"}\n"
		"vec3 evaluateProjectedDlights(vec3 localPos, vec3 normal) {\n"
		"    vec3 accum = vec3(0.0);\n"
		"    for (int i = 0; i < GLX_PROJECTED_DLIGHT_LOOP_LIMIT; i++) {\n"
		"        if (float(i) >= u_ProjectedDlightControl.x) {\n"
		"            break;\n"
		"        }\n"
		"        vec4 originRadius;\n"
		"        vec4 colorFlags;\n"
		"#if GLX_PROJECTED_DLIGHT_STREAM\n"
		"        if (u_ProjectedDlightControl.w > 0.5) {\n"
		"            originRadius = u_ProjectedDlightRecords[i].originRadius;\n"
		"            colorFlags = u_ProjectedDlightRecords[i].colorFlags;\n"
		"        } else\n"
		"#endif\n"
		"        {\n"
		"            if (i >= GLX_PROJECTED_DLIGHT_UNIFORM_LIMIT) {\n"
		"                break;\n"
		"            }\n"
		"            originRadius = u_ProjectedDlightOriginRadius[i];\n"
		"            colorFlags = u_ProjectedDlightColorFlags[i];\n"
		"        }\n"
		"        vec3 delta = originRadius.xyz - localPos;\n"
		"        float invRadius = 1.0 / max(originRadius.w, 0.0001);\n"
		"        vec2 st = vec2(0.5) + delta.xy * invRadius;\n"
		"        vec2 clipped = step(vec2(0.0), st) * step(st, vec2(1.0));\n"
		"        float frontCull = mod(floor(colorFlags.w * 0.25), 2.0);\n"
		"        float front = max(step(0.0, dot(delta, normal)), 1.0 - frontCull);\n"
		"        float zDistance = abs(delta.z);\n"
		"        float zAtten = max(1.0 - max(zDistance - originRadius.w * 0.5, 0.0) * 2.0 * invRadius, 0.0);\n"
		"        accum += colorFlags.rgb * clipped.x * clipped.y * front * zAtten;\n"
		"    }\n"
		"    return accum;\n"
		"}\n"
		"#if GLX_DLIGHT_SHADOW\n"
		"float dlightShadowFactor(vec3 dnLV, vec3 rawLV, vec3 nn, vec3 lv) {\n"
		"    bool spotShadow = u_ShadowFilter.z > 0.5;\n"
		"    if (u_DlightShadow.z <= 0.0 || u_ShadowAtlas.z <= 0.0 || (!spotShadow && u_ShadowAtlas.x <= 0.0)) {\n"
		"        return 1.0;\n"
		"    }\n"
		"    if (spotShadow) {\n"
		"        vec3 lightToFrag = -rawLV;\n"
		"        vec3 spotDir = safeNormalize(u_LightVector.xyz);\n"
		"        float spotDist = dot(lightToFrag, spotDir);\n"
		"        float tanHalfFov = max(u_ShadowDepth.w, 0.0001);\n"
		"        if (spotDist <= u_ShadowDepth.z) {\n"
		"            return 1.0;\n"
		"        }\n"
		"        vec3 spotRightBase = vec3(spotDir.z, -spotDir.x, spotDir.y);\n"
		"        vec3 spotRight = safeNormalize(spotRightBase - spotDir * dot(spotRightBase, spotDir));\n"
		"        vec3 spotUp = cross(spotRight, spotDir);\n"
		"        vec2 uv = vec2(-dot(lightToFrag, spotRight), -dot(lightToFrag, spotUp)) / (spotDist * tanHalfFov);\n"
		"        float coneRadius2 = dot(uv, uv);\n"
		"        if (coneRadius2 > 1.0) {\n"
		"            return 1.0;\n"
		"        }\n"
		"        uv = vec2(0.5) + uv * 0.5;\n"
		"        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {\n"
		"            return 1.0;\n"
		"        }\n"
		"        float tileSize = u_ShadowAtlas.z;\n"
		"        float inset = 1.0 / tileSize;\n"
		"        uv = clamp(uv, vec2(inset), vec2(1.0 - inset));\n"
		"        float slope = 1.0 - abs(dot(nn, lv));\n"
		"        float bias = u_DlightShadow.w * (0.125 + 0.375 * slope);\n"
		"        float texelBias = max(2.0 * spotDist * tanHalfFov / tileSize, 0.125);\n"
		"        float receiver = max(spotDist - min(bias, texelBias), u_ShadowDepth.z);\n"
		"        float receiverDepth = u_ShadowDepth.x - u_ShadowDepth.y / receiver;\n"
		"        vec2 atlasPx = vec2(u_ShadowAtlas.x + uv.x * tileSize,\n"
		"            u_ShadowAtlas.w - (u_ShadowAtlas.y + tileSize) + uv.y * tileSize);\n"
		"        vec2 tc = atlasPx * u_DlightShadow.xy;\n"
		"        vec2 inner = u_DlightShadow.xy * u_ShadowFilter.x;\n"
		"        vec2 outer = u_DlightShadow.xy * u_ShadowFilter.y;\n"
		"        float occ = 0.0;\n"
		"        occ += GLX_TEX2D(u_ShadowTexture, tc + vec2(-outer.x, -inner.y)).r < receiverDepth ? 1.0 : 0.0;\n"
		"        occ += GLX_TEX2D(u_ShadowTexture, tc + vec2( inner.x, -outer.y)).r < receiverDepth ? 1.0 : 0.0;\n"
		"        occ += GLX_TEX2D(u_ShadowTexture, tc + vec2(-inner.x,  outer.y)).r < receiverDepth ? 1.0 : 0.0;\n"
		"        occ += GLX_TEX2D(u_ShadowTexture, tc + vec2( outer.x,  inner.y)).r < receiverDepth ? 1.0 : 0.0;\n"
		"        return 1.0 - occ * 0.25 * u_DlightShadow.z;\n"
		"    }\n"
		"    vec3 shadowVec = -dnLV;\n"
		"    vec3 absVec = abs(shadowVec);\n"
		"    float major = max(max(absVec.x, absVec.y), max(absVec.z, 0.0001));\n"
		"    float receiverNDotL = clamp(abs(dot(nn, lv)), 0.0, 1.0);\n"
		"    float receiverSlope = 1.0 - receiverNDotL;\n"
		"    float bias = u_DlightShadow.w * (0.125 + 0.375 * receiverSlope);\n"
		"    float texelBias = max(2.0 * major / u_ShadowAtlas.x, 0.125);\n"
		"    float receiver = max(major - min(bias, texelBias), u_ShadowDepth.z);\n"
		"    float receiverDepth = u_ShadowDepth.x - u_ShadowDepth.y / receiver;\n"
		"    float face;\n"
		"    vec2 uv;\n"
		"    if (absVec.x >= absVec.y && absVec.x >= absVec.z) {\n"
		"        face = shadowVec.x >= 0.0 ? 0.0 : 1.0;\n"
		"        uv.x = ((shadowVec.x >= 0.0 ? -shadowVec.y : shadowVec.y) / major + 1.0) * 0.5;\n"
		"        uv.y = (shadowVec.z / major + 1.0) * 0.5;\n"
		"    } else if (absVec.y >= absVec.z) {\n"
		"        face = shadowVec.y >= 0.0 ? 2.0 : 3.0;\n"
		"        uv.x = ((shadowVec.y >= 0.0 ? shadowVec.x : -shadowVec.x) / major + 1.0) * 0.5;\n"
		"        uv.y = (shadowVec.z / major + 1.0) * 0.5;\n"
		"    } else {\n"
		"        face = shadowVec.z >= 0.0 ? 4.0 : 5.0;\n"
		"        uv.x = (-shadowVec.y / major + 1.0) * 0.5;\n"
		"        uv.y = ((shadowVec.z >= 0.0 ? -shadowVec.x : shadowVec.x) / major + 1.0) * 0.5;\n"
		"    }\n"
		"    float inset = 1.0 / u_ShadowAtlas.x;\n"
		"    uv = clamp(uv, vec2(inset), vec2(1.0 - inset));\n"
		"    float faceIndex = face + u_ShadowAtlas.y;\n"
		"    float row = floor(faceIndex / u_ShadowAtlas.z);\n"
		"    float column = faceIndex - row * u_ShadowAtlas.z;\n"
		"    vec2 atlasPx;\n"
		"    atlasPx.x = column * u_ShadowAtlas.x + uv.x * u_ShadowAtlas.x;\n"
		"    atlasPx.y = u_ShadowAtlas.w - (row + 1.0) * u_ShadowAtlas.x + uv.y * u_ShadowAtlas.x;\n"
		"    vec2 tc = atlasPx * u_DlightShadow.xy;\n"
		"    vec2 inner = u_DlightShadow.xy * u_ShadowFilter.x;\n"
		"    vec2 outer = u_DlightShadow.xy * u_ShadowFilter.y;\n"
		"    float occ = 0.0;\n"
		"    occ += GLX_TEX2D(u_ShadowTexture, tc + vec2(-outer.x, -inner.y)).r < receiverDepth ? 1.0 : 0.0;\n"
		"    occ += GLX_TEX2D(u_ShadowTexture, tc + vec2( inner.x, -outer.y)).r < receiverDepth ? 1.0 : 0.0;\n"
		"    occ += GLX_TEX2D(u_ShadowTexture, tc + vec2(-inner.x,  outer.y)).r < receiverDepth ? 1.0 : 0.0;\n"
		"    occ += GLX_TEX2D(u_ShadowTexture, tc + vec2( outer.x,  inner.y)).r < receiverDepth ? 1.0 : 0.0;\n"
		"    return 1.0 - occ * 0.25 * u_DlightShadow.z;\n"
		"}\n"
		"#endif\n"
		"void main(void) {\n"
		"    vec4 base = GLX_TEX2D(u_Texture0, v_TexCoord);\n"
		"    base.rgb *= u_TexFactors.x;\n"
		"    vec3 dnLV = v_LightVector;\n"
		"#if GLX_DLIGHT_LINEAR\n"
		"    float along = clamp(dot(-v_LightVector, u_LightVector.xyz) * u_LightVector.w, 0.0, 1.0);\n"
		"    dnLV = u_LightVector.xyz * along + v_LightVector;\n"
		"#endif\n"
		"    vec3 nn = safeNormalize(v_Normal);\n"
		"    float dist2 = dot(dnLV, dnLV);\n"
		"    float intensity = 1.0 - dist2 * u_LightColor.a;\n"
		"    if (intensity <= 0.0) {\n"
		"        discard;\n"
		"    }\n"
		"    float smoothIntensity = (3.0 - 2.0 * intensity) * intensity * intensity;\n"
		"    intensity = mix(intensity, smoothIntensity, u_DlightFactors.x);\n"
		"    vec3 lv = safeNormalize(dnLV);\n"
		"    float shadowFactor = 1.0;\n"
		"#if GLX_DLIGHT_SHADOW\n"
		"    shadowFactor = dlightShadowFactor(dnLV, v_LightVector, nn, lv);\n"
		"#endif\n"
		"    vec3 ev = safeNormalize(v_ViewVector);\n"
		"    vec3 halfVector = safeNormalize(lv + ev);\n"
		"#if GLX_DLIGHT_ABS_LIGHT\n"
		"    float specDot = abs(dot(nn, halfVector));\n"
		"#else\n"
		"    float specDot = max(dot(nn, halfVector), 0.0);\n"
		"#endif\n"
		"    float specTerm = pow(specDot, max(u_TexFactors.y, 1.0));\n"
		"    vec4 spec = (base * u_TexFactors.z + vec4(u_TexFactors.w)) * specTerm;\n"
		"    float diffuse;\n"
		"#if GLX_DLIGHT_ABS_LIGHT\n"
		"    diffuse = dot(nn, lv);\n"
		"    if (diffuse * dot(nn, ev) < 0.0) {\n"
		"        discard;\n"
		"    }\n"
		"    diffuse = abs(diffuse);\n"
		"#else\n"
		"    diffuse = max(dot(nn, lv), 0.0);\n"
		"#endif\n"
		"    vec4 lit = base * diffuse + spec;\n"
		"#if GLX_DLIGHT_FOG\n"
		"    vec4 fog = GLX_TEX2D(u_FogTexture, v_FogTexCoord);\n"
		"    lit *= 1.0 - fog.a;\n"
		"#endif\n"
		"    vec4 currentLight = lit * vec4(u_LightColor.rgb * intensity * shadowFactor, intensity * shadowFactor);\n"
		"    vec3 projectedLight = evaluateProjectedDlights(v_LocalPos, nn);\n"
		"    GLX_FRAG_COLOR = currentLight + vec4(base.rgb * projectedLight, 0.0) * u_ProjectedDlightControl.z;\n"
		"}\n",
		version,
		key.linear ? 1 : 0,
		key.fogMode != 0 ? 1 : 0,
		key.absLight ? 1 : 0,
		key.shadow ? 1 : 0,
		GLX_PROJECTED_DLIGHT_UNIFORM_LIMIT,
		key.projectedStream ? GLX_RENDER_IR_PROJECTED_DLIGHT_RECORD_LIMIT :
			GLX_PROJECTED_DLIGHT_UNIFORM_LIMIT,
		key.projectedStream ? 1 : 0,
		GLX_PROJECTED_DLIGHT_STREAM_BINDING,
		streamBlock,
		varyingIn,
		varyingIn,
		varyingIn,
		varyingIn,
		varyingIn,
		varyingIn,
		fragmentOutput );
	return GLX_Dlight_SourceFits( written, outSize );
}

static void GLX_Dlight_PrintObjectLog( const DlightState &state, GLuint object,
	qboolean program, printParm_t printLevel )
{
	GLint length = 0;
	GLsizei written = 0;
	char log[2048];

	if ( program ) {
		state.fns.GetProgramiv( object, GL_INFO_LOG_LENGTH, &length );
	} else {
		state.fns.GetShaderiv( object, GL_INFO_LOG_LENGTH, &length );
	}
	if ( length <= 1 ) {
		return;
	}

	if ( program ) {
		state.fns.GetProgramInfoLog( object, sizeof( log ), &written, log );
	} else {
		state.fns.GetShaderInfoLog( object, sizeof( log ), &written, log );
	}
	if ( written > 0 ) {
		log[sizeof( log ) - 1] = '\0';
		RI().Printf( printLevel, "GLx dlight program log: %s\n", log );
	}
}

static GLuint GLX_Dlight_CompileShader( DlightState *state, GLenum shaderType,
	const char *source )
{
	GLuint shader;
	GLint ok = 0;

	shader = state->fns.CreateShader( shaderType );
	if ( !shader ) {
		GLX_Dlight_SetReason( state, "glCreateShader failed" );
		return 0;
	}

	state->fns.ShaderSource( shader, 1, &source, nullptr );
	state->fns.CompileShader( shader );
	state->fns.GetShaderiv( shader, GL_COMPILE_STATUS, &ok );
	if ( !ok ) {
		GLX_Dlight_SetReason( state,
			shaderType == GL_VERTEX_SHADER ? "dlight vertex compile failed" : "dlight fragment compile failed" );
		state->shaderCompileFailures++;
		GLX_Dlight_PrintObjectLog( *state, shader, qfalse, PRINT_WARNING );
		state->fns.DeleteShader( shader );
		return 0;
	}

	return shader;
}

static DlightProgram *GLX_Dlight_CreateProgram( DlightState *state,
	const DlightProgramKey &key )
{
	DlightProgram *program;
	char vertexSource[4096];
	char fragmentSource[24576];
	GLuint vertexShader;
	GLuint fragmentShader;
	GLint ok = 0;

	if ( state->programCount >= GLX_DLIGHT_PROGRAM_LIMIT ) {
		GLX_Dlight_SetReason( state, "dlight program cache full" );
		state->programCreateFailures++;
		return nullptr;
	}
	if ( !GLX_Dlight_VertexSource( key, vertexSource, sizeof( vertexSource ) ) ||
		!GLX_Dlight_FragmentSource( key, fragmentSource, sizeof( fragmentSource ) ) ) {
		GLX_Dlight_SetReason( state, "dlight shader source overflow" );
		state->programCreateFailures++;
		return nullptr;
	}

	vertexShader = GLX_Dlight_CompileShader( state, GL_VERTEX_SHADER, vertexSource );
	if ( !vertexShader ) {
		return nullptr;
	}
	fragmentShader = GLX_Dlight_CompileShader( state, GL_FRAGMENT_SHADER, fragmentSource );
	if ( !fragmentShader ) {
		state->fns.DeleteShader( vertexShader );
		return nullptr;
	}

	program = &state->programs[state->programCount];
	std::memset( program, 0, sizeof( *program ) );
	program->key = key;
	program->program = state->fns.CreateProgram();
	if ( !program->program ) {
		GLX_Dlight_SetReason( state, "glCreateProgram failed" );
		state->programCreateFailures++;
		state->fns.DeleteShader( vertexShader );
		state->fns.DeleteShader( fragmentShader );
		return nullptr;
	}

	state->fns.AttachShader( program->program, vertexShader );
	state->fns.AttachShader( program->program, fragmentShader );
	state->fns.LinkProgram( program->program );
	state->fns.GetProgramiv( program->program, GL_LINK_STATUS, &ok );
	state->fns.DeleteShader( vertexShader );
	state->fns.DeleteShader( fragmentShader );
	if ( !ok ) {
		GLX_Dlight_SetReason( state, "dlight program link failed" );
		state->programLinkFailures++;
		state->programCreateFailures++;
		GLX_Dlight_PrintObjectLog( *state, program->program, qtrue, PRINT_WARNING );
		state->fns.DeleteProgram( program->program );
		program->program = 0;
		return nullptr;
	}

	program->texture0Uniform = state->fns.GetUniformLocation( program->program, "u_Texture0" );
	program->fogTextureUniform = state->fns.GetUniformLocation( program->program, "u_FogTexture" );
	program->shadowTextureUniform = state->fns.GetUniformLocation( program->program, "u_ShadowTexture" );
	program->eyePosUniform = state->fns.GetUniformLocation( program->program, "u_EyePos" );
	program->lightPosUniform = state->fns.GetUniformLocation( program->program, "u_LightPos" );
	program->lightColorUniform = state->fns.GetUniformLocation( program->program, "u_LightColor" );
	program->lightVectorUniform = state->fns.GetUniformLocation( program->program, "u_LightVector" );
	program->texFactorsUniform = state->fns.GetUniformLocation( program->program, "u_TexFactors" );
	program->dlightFactorsUniform = state->fns.GetUniformLocation( program->program, "u_DlightFactors" );
	program->fogDistanceVectorUniform = state->fns.GetUniformLocation( program->program, "u_FogDistanceVector" );
	program->fogDepthVectorUniform = state->fns.GetUniformLocation( program->program, "u_FogDepthVector" );
	program->fogEyeTUniform = state->fns.GetUniformLocation( program->program, "u_FogEyeT" );
	program->dlightShadowUniform = state->fns.GetUniformLocation( program->program, "u_DlightShadow" );
	program->shadowAtlasUniform = state->fns.GetUniformLocation( program->program, "u_ShadowAtlas" );
	program->shadowDepthUniform = state->fns.GetUniformLocation( program->program, "u_ShadowDepth" );
	program->shadowFilterUniform = state->fns.GetUniformLocation( program->program, "u_ShadowFilter" );
	program->projectedControlUniform = state->fns.GetUniformLocation( program->program,
		"u_ProjectedDlightControl" );
	program->projectedOriginRadiusUniform = GLX_Dlight_GetUniformLocationAny( state,
		program->program, "u_ProjectedDlightOriginRadius[0]",
		"u_ProjectedDlightOriginRadius" );
	program->projectedColorFlagsUniform = GLX_Dlight_GetUniformLocationAny( state,
		program->program, "u_ProjectedDlightColorFlags[0]",
		"u_ProjectedDlightColorFlags" );

	state->programCount++;
	state->programCreates++;
	GLX_Dlight_SetReason( state, "" );
	return program;
}

static DlightProgram *GLX_Dlight_FindProgram( DlightState *state,
	const DlightProgramKey &key )
{
	for ( int i = 0; i < state->programCount; i++ ) {
		if ( GLX_Dlight_KeyEquals( state->programs[i].key, key ) ) {
			return &state->programs[i];
		}
	}
	return nullptr;
}

static DlightProgram *GLX_Dlight_CurrentProgram( DlightState *state )
{
	if ( !state || !state->currentProgram ) {
		return nullptr;
	}

	for ( int i = 0; i < state->programCount; i++ ) {
		if ( state->programs[i].program == state->currentProgram ) {
			return &state->programs[i];
		}
	}
	return nullptr;
}

static qboolean GLX_Dlight_Active( const DlightState &state )
{
	if ( !state.enabled || !state.enabled->integer ) {
		return qfalse;
	}
	return state.ready;
}

static DlightProgram *GLX_Dlight_FindOrCreateProgram( DlightState *state,
	qboolean linear, int fogMode, qboolean absLight, qboolean shadow,
	qboolean projectedStream )
{
	DlightProgramKey key;
	DlightProgram *program;

	if ( !state || !GLX_Dlight_Active( *state ) ) {
		return nullptr;
	}

	key.linear = linear;
	key.fogMode = GLX_Dlight_ClampFogMode( fogMode );
	key.absLight = absLight;
	key.shadow = shadow;
	key.projectedStream = projectedStream;

	program = GLX_Dlight_FindProgram( state, key );
	if ( program ) {
		state->programCacheHits++;
		return program;
	}
	return GLX_Dlight_CreateProgram( state, key );
}

static void GLX_Dlight_SetUniform4fv( DlightState *state, GLint location,
	const float *value )
{
	if ( location >= 0 && value ) {
		state->fns.Uniform4fv( location, 1, value );
		state->uniformUpdates++;
	}
}

static void GLX_Dlight_SetUniform4fvCount( DlightState *state, GLint location,
	int count, const float *value )
{
	if ( location >= 0 && count > 0 && value ) {
		state->fns.Uniform4fv( location, count, value );
		state->uniformUpdates += static_cast<unsigned int>( count );
	}
}

static qboolean GLX_Dlight_FillProjectedStreamRecords( DlightState *state,
	const ProjectedDlightShaderInput &input, ProjectedDlightStreamRecord *records )
{
	if ( !state || !records ||
		!GLX_RenderIR_ValidateProjectedDlightShaderInput( input ) ) {
		return qfalse;
	}

	for ( int i = 0; i < input.projectedDlights.recordCount; i++ ) {
		const ProjectedDlightRecord &record =
			state->projectedListRecords[input.projectedDlights.firstRecord + i];
		ProjectedDlightStreamRecord &out = records[i];

		if ( !GLX_RenderIR_ValidateProjectedDlightRecord( record ) ) {
			return qfalse;
		}

		out.originRadius[0] = record.origin[0];
		out.originRadius[1] = record.origin[1];
		out.originRadius[2] = record.origin[2];
		out.originRadius[3] = record.radius;
		out.colorFlags[0] = record.color[0];
		out.colorFlags[1] = record.color[1];
		out.colorFlags[2] = record.color[2];
		out.colorFlags[3] = static_cast<float>( record.flags & GLX_PROJECTED_DLIGHT_FLAGS_ALL );
	}
	return qtrue;
}

static int GLX_Dlight_FillProjectedArenaListRecords( DlightState *state,
	const ProjectedDlightShaderInput &input )
{
	if ( !state ||
		!GLX_RenderIR_ValidateProjectedDlightShaderInput( input ) ) {
		return 0;
	}

	for ( int i = 0; i < input.projectedDlights.recordCount; i++ ) {
		ProjectedDlightArenaListRecord &out = state->projectedShaderArenaListRecords[i];
		const int recordIndex = input.projectedDlights.firstRecord + i;
		const ProjectedDlightRecord &record = state->projectedListRecords[recordIndex];

		out.recordIndex = static_cast<unsigned int>( recordIndex );
		out.flags = record.flags & GLX_PROJECTED_DLIGHT_FLAGS_ALL;
	}
	return input.projectedDlights.recordCount;
}

static void GLX_Dlight_RecordProjectedShaderArenaReservation(
	DlightState *state, const StreamReservation &reservation )
{
	if ( !state ) {
		return;
	}

	state->projectedShaderArenaWraps += reservation.reserveWraps;
	state->projectedShaderArenaSameFrameRejects +=
		reservation.reserveSameFrameWrapRejects;
	state->projectedShaderArenaWaits += reservation.reserveSyncWaits;
	state->projectedShaderArenaTimeouts += reservation.reserveSyncTimeouts;
	state->projectedShaderArenaSyncFailures += reservation.reserveSyncFailures;
}

static void GLX_Dlight_RecordProjectedShaderArenaUpload( DlightState *state,
	const ProjectedDlightShaderInput &input, const StreamReservation &reservation,
	unsigned int uploadBytes, unsigned int lightRecords, unsigned int listRecords )
{
	if ( !state ) {
		return;
	}

	state->projectedShaderArenaUploads++;
	state->projectedShaderArenaLightRecordUploads += lightRecords;
	state->projectedShaderArenaListRecordUploads += listRecords;
	state->projectedShaderArenaBytes += static_cast<unsigned long long>( uploadBytes );
	state->projectedShaderArenaFrameBytes += uploadBytes;
	state->projectedShaderArenaLastBuffer = reservation.buffer;
	state->projectedShaderArenaLastOffset =
		static_cast<unsigned int>( reservation.offset > ~0u ? ~0u : reservation.offset );
	state->projectedShaderArenaLastBytes =
		static_cast<unsigned int>( reservation.bytes > ~0u ? ~0u : reservation.bytes );
	if ( input.target == ProjectedDlightShaderTarget::WorldPacket ) {
		state->projectedShaderArenaWorldRecords += lightRecords;
	} else if ( input.target == ProjectedDlightShaderTarget::DynamicDraw ) {
		state->projectedShaderArenaDynamicRecords += lightRecords;
	}
}

static void GLX_Dlight_RecordProjectedShaderArenaRange( DlightState *state,
	qboolean rangeReady )
{
	if ( !state ) {
		return;
	}

	state->projectedShaderArenaRangeAttempts++;
	state->projectedShaderArenaAuthoritativeAttempts++;
	if ( rangeReady ) {
		if ( state->projectedShaderArenaRangeBound ) {
			// rebind over a live arena range: account the implicit clear
			state->projectedShaderArenaRangeClears++;
			state->projectedShaderArenaAuthoritativeClears++;
		}
		state->projectedShaderArenaRangeBinds++;
		state->projectedShaderArenaRangeBound = qtrue;
		state->projectedShaderArenaAuthoritativeBinds++;
	} else {
		state->projectedShaderArenaRangeFailures++;
		state->projectedShaderArenaRangeBound = qfalse;
		state->projectedShaderArenaAuthoritativeFailures++;
		state->projectedShaderArenaAuthoritativeFallbacks++;
	}
}

static qboolean GLX_Dlight_BindProjectedShaderInput( DlightState *state,
	const ProjectedDlightShaderInput &input,
	const ProjectedDlightResourceRange *resourceRange = nullptr,
	ProjectedDlightShaderExecutionPlan *executionPlanOut = nullptr )
{
	DlightProgram *program;
	ProjectedDlightShaderExecutionPlan executionPlan {};
	int count;
	float control[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float originRadius[GLX_PROJECTED_DLIGHT_UNIFORM_LIMIT * 4] {};
	float colorFlags[GLX_PROJECTED_DLIGHT_UNIFORM_LIMIT * 4] {};

	if ( executionPlanOut ) {
		*executionPlanOut = {};
	}
	if ( !state || !input.programmable ) {
		return qfalse;
	}

	state->projectedShaderUniformAttempts++;
	program = GLX_Dlight_CurrentProgram( state );
	executionPlan = GLX_RenderIR_PlanProjectedDlightShaderExecution( input,
		GLX_PROJECTED_DLIGHT_UNIFORM_LIMIT,
		GLX_Dlight_ProjectedProgramEnabled( *state ),
		program && program->key.projectedStream ? qtrue : qfalse,
		resourceRange,
		static_cast<unsigned int>( sizeof( ProjectedDlightStreamRecord ) ),
		GLX_PROJECTED_DLIGHT_STREAM_ALIGNMENT );
	if ( !executionPlan.valid ) {
		state->projectedShaderUniformFailures++;
		return qfalse;
	}

	if ( executionPlan.requestedRecords > GLX_PROJECTED_DLIGHT_UNIFORM_LIMIT ) {
		state->projectedShaderResourceAttempts++;
		if ( !executionPlan.resourcePromoted ) {
			state->projectedShaderResourceFailures++;
		}
	}

	if ( !program || program->projectedControlUniform < 0 ) {
		state->projectedShaderUniformFailures++;
		return qfalse;
	}
	if ( executionPlan.backend == ProjectedDlightShaderBackend::UniformWindow &&
		( program->projectedOriginRadiusUniform < 0 ||
			program->projectedColorFlagsUniform < 0 ) ) {
		state->projectedShaderUniformFailures++;
		return qfalse;
	}

	count = executionPlan.uniformRecords;
	if ( executionPlan.truncatedRecords > 0 ) {
		state->projectedShaderUniformTruncated += executionPlan.truncatedRecords;
	}

	for ( int i = 0; i < count; i++ ) {
		const ProjectedDlightRecord &record =
			state->projectedListRecords[input.projectedDlights.firstRecord + i];
		float *originOut = &originRadius[i * 4];
		float *colorOut = &colorFlags[i * 4];

		if ( !GLX_RenderIR_ValidateProjectedDlightRecord( record ) ) {
			state->projectedShaderUniformFailures++;
			return qfalse;
		}

		originOut[0] = record.origin[0];
		originOut[1] = record.origin[1];
		originOut[2] = record.origin[2];
		originOut[3] = record.radius;
		colorOut[0] = record.color[0];
		colorOut[1] = record.color[1];
		colorOut[2] = record.color[2];
		colorOut[3] = static_cast<float>( record.flags & GLX_PROJECTED_DLIGHT_FLAGS_ALL );
	}

	control[0] = executionPlan.backend == ProjectedDlightShaderBackend::StreamResource ?
		static_cast<float>( executionPlan.streamRecords ) : static_cast<float>( count );
	control[1] = static_cast<float>( input.projectedDlights.firstRecord );
	control[2] = executionPlan.execute ? 1.0f : 0.0f;
	control[3] = executionPlan.backend == ProjectedDlightShaderBackend::StreamResource ?
		1.0f : 0.0f;
	GLX_Dlight_SetUniform4fvCount( state, program->projectedControlUniform, 1,
		control );
	GLX_Dlight_SetUniform4fvCount( state, program->projectedOriginRadiusUniform,
		count, originRadius );
	GLX_Dlight_SetUniform4fvCount( state, program->projectedColorFlagsUniform,
		count, colorFlags );
	state->projectedShaderUniformBinds++;
	state->projectedShaderUniformRecords += static_cast<unsigned int>( count );
	if ( executionPlan.execute ) {
		state->projectedShaderUniformExecutableBinds++;
	} else {
		state->projectedShaderUniformSuppressedBinds++;
	}
	if ( executionPlan.limitSuppressed ) {
		state->projectedShaderUniformLimitSuppressions++;
	}
	if ( executionPlan.backend == ProjectedDlightShaderBackend::StreamResource ) {
		state->projectedShaderResourceBinds++;
		state->projectedShaderResourceRecords += executionPlan.streamRecords;
		state->projectedShaderResourceLimitPromotions++;
		if ( executionPlan.execute ) {
			state->projectedShaderResourceExecutableBinds++;
		} else {
			state->projectedShaderResourceSuppressedBinds++;
		}
	}
	if ( input.target == ProjectedDlightShaderTarget::WorldPacket ) {
		state->projectedShaderUniformWorldBinds++;
		if ( executionPlan.execute ) {
			state->projectedShaderUniformWorldExecutableBinds++;
		}
		if ( executionPlan.backend == ProjectedDlightShaderBackend::StreamResource &&
			executionPlan.execute ) {
			state->projectedShaderResourceWorldExecutableBinds++;
		}
	} else if ( input.target == ProjectedDlightShaderTarget::DynamicDraw ) {
		state->projectedShaderUniformDynamicBinds++;
		if ( executionPlan.execute ) {
			state->projectedShaderUniformDynamicExecutableBinds++;
		}
		if ( executionPlan.backend == ProjectedDlightShaderBackend::StreamResource &&
			executionPlan.execute ) {
			state->projectedShaderResourceDynamicExecutableBinds++;
		}
	}
	if ( executionPlanOut ) {
		*executionPlanOut = executionPlan;
	}
	return qtrue;
}

static void GLX_Dlight_ClearProjectedStreamRange( DlightState *state )
{
	if ( !state ) {
		return;
	}
	if ( state->projectedShaderArenaRangeBound ) {
		state->projectedShaderArenaRangeBound = qfalse;
		state->projectedShaderArenaRangeClears++;
		state->projectedShaderArenaAuthoritativeClears++;
	}
	if ( !state->projectedShaderStreamRangeBound || !state->fns.BindBufferRange ) {
		return;
	}

	state->fns.BindBufferRange( GL_SHADER_STORAGE_BUFFER,
		GLX_PROJECTED_DLIGHT_STREAM_BINDING, 0, 0, 0 );
	state->projectedShaderStreamRangeBound = qfalse;
	state->projectedShaderStreamRangeClears++;
}

static qboolean GLX_Dlight_BindProjectedStreamRange( DlightState *state,
	GLuint buffer, size_t offset, size_t bytes )
{
	if ( !state ) {
		return qfalse;
	}

	state->projectedShaderStreamRangeAttempts++;
	if ( !state->fns.BindBufferRange || !buffer || bytes == 0 ||
		offset > static_cast<size_t>( ~0u ) || bytes > static_cast<size_t>( ~0u ) ) {
		state->projectedShaderStreamRangeFailures++;
		return qfalse;
	}

	if ( state->projectedShaderStreamRangeBound ) {
		// rebinding over a live range replaces it; account the implicit clear
		// so cumulative binds and clears stay matched for stale-range checks
		state->projectedShaderStreamRangeClears++;
	}
	state->fns.BindBufferRange( GL_SHADER_STORAGE_BUFFER,
		GLX_PROJECTED_DLIGHT_STREAM_BINDING, buffer,
		static_cast<ptrdiff_t>( offset ), static_cast<ptrdiff_t>( bytes ) );
	state->projectedShaderStreamRangeBound = qtrue;
	state->projectedShaderStreamRangeBinds++;
	return qtrue;
}

static qboolean GLX_Dlight_ClearProjectedShaderInput( DlightState *state )
{
	DlightProgram *program = GLX_Dlight_CurrentProgram( state );
	const float noProjectedDlights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	GLX_Dlight_ClearProjectedStreamRange( state );
	if ( !program || program->projectedControlUniform < 0 ) {
		return qfalse;
	}

	GLX_Dlight_SetUniform4fv( state, program->projectedControlUniform,
		noProjectedDlights );
	return qtrue;
}

static void GLX_Dlight_RegisterCvars( DlightState *state )
{
	if ( !state ) {
		return;
	}

	state->enabled = RI().Cvar_Get( "r_glxDlightProgram", "1", CVAR_ARCHIVE_ND );
	RI().Cvar_SetDescription( state->enabled,
		"Use a GLx-native GLSL dynamic-light program for r_dlightMode 1/2, falling back to the ARB path when unsupported." );
	state->scissor = RI().Cvar_Get( "r_glxDlightScissor", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->scissor,
		"Restrict legacy projected dynamic-light overlay draws to computed screen-space scissor rectangles; auto enables this in GLx RC/stress profiles." );
	state->projectedProgram = RI().Cvar_Get( "r_glxDlightProjectedProgram", "0",
		CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->projectedProgram,
		"Evaluate compact projected-light records in the GLx GLSL dlight program as an opt-in guarded replacement; unsupported or invalid binds fall back to legacy projected lights." );
	state->projectedMdi = RI().Cvar_Get( "r_glxDlightProjectedMdi", "0",
		CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->projectedMdi,
		"Prepare GLx projected dynamic-light indirect submission plans from staged persistent command records; experimental and off by default until projected-light shader parity is authoritative." );
}

static void GLX_Dlight_Shutdown( DlightState *state, qboolean deletePrograms )
{
	if ( !state ) {
		return;
	}

	GLX_Dlight_ClearProjectedStreamRange( state );
	s_projectedDlightMdiFns = {};
	if ( state->currentProgram && state->fns.UseProgram ) {
		state->fns.UseProgram( 0 );
	}
	if ( deletePrograms && state->fns.DeleteProgram ) {
		for ( int i = 0; i < state->programCount; i++ ) {
			if ( state->programs[i].program ) {
				state->fns.DeleteProgram( state->programs[i].program );
			}
		}
	}

	std::memset( state->programs, 0, sizeof( state->programs ) );
	state->programCount = 0;
	state->currentProgram = 0;
	state->ready = qfalse;
	GLX_Dlight_SetReason( state, "not initialized" );
}

static void GLX_Dlight_OnOpenGLReady( DlightState *state )
{
	if ( !state ) {
		return;
	}

	GLX_Dlight_Shutdown( state, qtrue );
	GLX_Dlight_ResetCounters( state );
	GLX_Dlight_LoadFunctions( state );
	state->ready = GLX_Dlight_FunctionsReady( *state );
	GLX_Dlight_SetReason( state, state->ready ? "" : "missing GLSL program functions" );
}

static qboolean GLX_Dlight_ProgramAvailable( DlightState *state, qboolean linear,
	int fogMode, qboolean absLight, qboolean shadow, qboolean projectedStream )
{
	DlightProgram *program;

	if ( state ) {
		state->availabilityQueries++;
	}
	program = GLX_Dlight_FindOrCreateProgram( state, linear, fogMode, absLight, shadow,
		projectedStream );
	if ( state ) {
		if ( program ) {
			state->availabilityHits++;
		} else {
			state->availabilityMisses++;
		}
	}
	return program ? qtrue : qfalse;
}

static qboolean GLX_Dlight_BindProgram( DlightState *state, qboolean linear,
	int fogMode, qboolean absLight, qboolean shadow, qboolean projectedStream,
	const float *eyePos, const float *lightPos,
	const float *lightColor, const float *lightVector, const float *texFactors,
	const float *dlightFactors, const float *fogDistanceVector,
	const float *fogDepthVector, float fogEyeT, const float *dlightShadow,
	const float *shadowAtlas, const float *shadowDepth, const float *shadowFilter )
{
	DlightProgram *program;

	if ( state ) {
		state->bindAttempts++;
	}
	fogMode = GLX_Dlight_ClampFogMode( fogMode );
	if ( fogMode && ( !fogDistanceVector || !fogDepthVector ) ) {
		if ( state ) {
			state->bindFailures++;
		}
		return qfalse;
	}
	if ( !eyePos || !lightPos || !lightColor || !lightVector || !texFactors ||
		!dlightFactors ) {
		if ( state ) {
			state->bindFailures++;
		}
		return qfalse;
	}
	if ( shadow && ( !dlightShadow || !shadowAtlas || !shadowDepth || !shadowFilter ) ) {
		if ( state ) {
			state->bindFailures++;
		}
		return qfalse;
	}

	program = GLX_Dlight_FindOrCreateProgram( state, linear, fogMode, absLight,
		shadow, projectedStream );
	if ( !program ) {
		if ( state ) {
			state->bindFailures++;
		}
		return qfalse;
	}

	state->fns.UseProgram( program->program );
	state->currentProgram = program->program;
	state->binds++;
	if ( program->texture0Uniform >= 0 ) {
		state->fns.Uniform1i( program->texture0Uniform, 0 );
	}
	if ( program->fogTextureUniform >= 0 ) {
		state->fns.Uniform1i( program->fogTextureUniform, 1 );
	}
	if ( program->shadowTextureUniform >= 0 ) {
		state->fns.Uniform1i( program->shadowTextureUniform, 2 );
	}
	GLX_Dlight_SetUniform4fv( state, program->eyePosUniform, eyePos );
	GLX_Dlight_SetUniform4fv( state, program->lightPosUniform, lightPos );
	GLX_Dlight_SetUniform4fv( state, program->lightColorUniform, lightColor );
	GLX_Dlight_SetUniform4fv( state, program->lightVectorUniform, lightVector );
	GLX_Dlight_SetUniform4fv( state, program->texFactorsUniform, texFactors );
	GLX_Dlight_SetUniform4fv( state, program->dlightFactorsUniform, dlightFactors );
	GLX_Dlight_SetUniform4fv( state, program->fogDistanceVectorUniform, fogDistanceVector );
	GLX_Dlight_SetUniform4fv( state, program->fogDepthVectorUniform, fogDepthVector );
	GLX_Dlight_SetUniform4fv( state, program->dlightShadowUniform, dlightShadow );
	GLX_Dlight_SetUniform4fv( state, program->shadowAtlasUniform, shadowAtlas );
	GLX_Dlight_SetUniform4fv( state, program->shadowDepthUniform, shadowDepth );
	GLX_Dlight_SetUniform4fv( state, program->shadowFilterUniform, shadowFilter );
	GLX_Dlight_ClearProjectedShaderInput( state );
	if ( program->fogEyeTUniform >= 0 ) {
		state->fns.Uniform1f( program->fogEyeTUniform, fogEyeT );
	}
	return qtrue;
}

static void GLX_Dlight_Unbind( DlightState *state )
{
	if ( !state ) {
		return;
	}

	GLX_Dlight_ClearProjectedStreamRange( state );
	if ( !state->currentProgram || !state->fns.UseProgram ) {
		return;
	}
	state->fns.UseProgram( 0 );
	state->currentProgram = 0;
	state->unbinds++;
}

/*
Bind the dlight program with the per-pixel light term neutralized so only the
projected-light window contributes: a zero light alpha keeps the radius discard
from firing and a zero light color zeroes the current-light output, leaving
base.rgb * projectedLight * control.z as the draw's color. Used for legacy-mode
projected draws, where no PM lighting pass ever makes the program current.
*/
static qboolean GLX_Dlight_BindProjectedOnlyProgram( DlightState *state,
	RenderProductTier tier )
{
	static const float zeroVec[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	static const float texFactors[4] = { 1.0f, 1.0f, 0.0f, 0.0f };

	if ( !state || !GLX_Dlight_ProjectedProgramEnabled( *state ) ) {
		return qfalse;
	}

	return GLX_Dlight_BindProgram( state, qfalse, 0, qfalse, qfalse,
		GLX_Dlight_ProjectedStreamShaderEnabled( *state, tier ),
		zeroVec, zeroVec, zeroVec, zeroVec, texFactors, zeroVec,
		nullptr, nullptr, 0.0f, nullptr, nullptr, nullptr, nullptr );
}

static void GLX_Dlight_PrintInfo( const DlightState &state )
{
	RI().Printf( PRINT_ALL, "  dlight program active: %s, ready %s, programs %i, current %u\n",
		BoolName( GLX_Dlight_Active( state ) ), BoolName( state.ready ),
		state.programCount, state.currentProgram );
	RI().Printf( PRINT_ALL, "  dlight program reason: %s\n",
		state.reason[0] ? state.reason : "ready" );
	RI().Printf( PRINT_ALL, "  dlight program availability: %u queries, %u hits, %u misses, cache hits %u, creates %u, create failures %u, shader compile failures %u, link failures %u\n",
		state.availabilityQueries, state.availabilityHits, state.availabilityMisses,
		state.programCacheHits, state.programCreates, state.programCreateFailures,
		state.shaderCompileFailures, state.programLinkFailures );
	RI().Printf( PRINT_ALL, "  dlight program binds: %u/%u, failures %u, unbinds %u, uniform vec4 updates %u\n",
		state.binds, state.bindAttempts, state.bindFailures, state.unbinds,
		state.uniformUpdates );
	RI().Printf( PRINT_ALL, "  dlight state: legacy passes %u, texture binds %u, fog textures %u, shadow textures %u/%u, shadow fbo %u/%u, state changes %u\n",
		state.legacyPasses,
		state.textureBinds,
		state.fogTextureBinds,
		state.shadowTextureBinds,
		state.shadowTextureFallbackBinds,
		state.shadowFboBinds,
		state.shadowFboRestores,
		state.stateChanges );
	RI().Printf( PRINT_ALL, "  dlight build: legacy lights %u/%u skipped, no-hit %u, verts %u, indexes %u/%u, pm passes %u, pm verts/indexes %u/%u\n",
		state.buildLegacyLights,
		state.buildLegacySkippedLights,
		state.buildLegacyNoHitLights,
		state.buildLegacyVertexes,
		state.buildLegacyIndexes,
		state.buildLegacyLitIndexes,
		state.buildPmPasses,
		state.buildPmVertexes,
		state.buildPmIndexes );
	RI().Printf( PRINT_ALL, "  dlight cull: legacy verts %u, indexes %u\n",
		state.cullLegacyVertexes,
		state.cullLegacyIndexes );
	RI().Printf( PRINT_ALL, "  dlight scissor: active %s, candidates %u, computed %u, applied %u, fallbacks %u, pixels %llu/%llu\n",
		BoolName( GLX_Dlight_ScissorEnabled( state ) ),
		state.scissorCandidates,
		state.scissorComputed,
		state.scissorApplied,
		state.scissorFallbacks,
		state.scissorPixels,
		state.scissorViewportPixels );
	RI().Printf( PRINT_ALL, "  dlight projected lists: sources %i/%u frames, surface builds %u complete/%u incomplete, packet lists %u active, lookups %u/%u hits, packet builds %u complete/%u incomplete, packet IR %u/%u rejected, dynamic builds %u complete/%u incomplete, dynamic IR %u/%u rejected, dropped masks 0x%08x/0x%08x\n",
		state.projectedSourceRecordCount,
		state.projectedSourceFrames,
		state.projectedListComplete,
		state.projectedListIncomplete,
		state.projectedPacketActivePackets,
		state.projectedPacketItemLookupHits,
		state.projectedPacketItemLookups,
		state.projectedPacketListComplete,
		state.projectedPacketListIncomplete,
		state.projectedPacketIRProducts,
		state.projectedPacketIRRejected,
		state.projectedDynamicListComplete,
		state.projectedDynamicListIncomplete,
		state.projectedDynamicIRProducts,
		state.projectedDynamicIRRejected,
		state.projectedListDroppedMask,
		state.projectedPacketDroppedMask );
	RI().Printf( PRINT_ALL, "  dlight projected shader inputs: inputs %u/%u records, world %u, dynamic %u, programmable %u, fallback %u, invalid %u\n",
		state.projectedShaderInputs,
		state.projectedShaderInputRecords,
		state.projectedShaderWorldPackets,
		state.projectedShaderDynamicDraws,
		state.projectedShaderProgrammableInputs,
		state.projectedShaderFallbackInputs,
		state.projectedShaderInvalidInputs );
	RI().Printf( PRINT_ALL, "  dlight projected shader uniforms: attempts %u, binds %u, failures %u, records %u, truncated %u, executable %u, suppressed %u, world %u/%u, dynamic %u/%u, limit-suppressed %u, limit %i\n",
		state.projectedShaderUniformAttempts,
		state.projectedShaderUniformBinds,
		state.projectedShaderUniformFailures,
		state.projectedShaderUniformRecords,
		state.projectedShaderUniformTruncated,
		state.projectedShaderUniformExecutableBinds,
		state.projectedShaderUniformSuppressedBinds,
		state.projectedShaderUniformWorldExecutableBinds,
		state.projectedShaderUniformWorldBinds,
		state.projectedShaderUniformDynamicExecutableBinds,
		state.projectedShaderUniformDynamicBinds,
		state.projectedShaderUniformLimitSuppressions,
		GLX_PROJECTED_DLIGHT_UNIFORM_LIMIT );
	RI().Printf( PRINT_ALL, "  dlight projected shader resource: attempts %u, binds %u, executable %u, suppressed %u, promotions %u, failures %u, records %u, world %u, dynamic %u, binding %u\n",
		state.projectedShaderResourceAttempts,
		state.projectedShaderResourceBinds,
		state.projectedShaderResourceExecutableBinds,
		state.projectedShaderResourceSuppressedBinds,
		state.projectedShaderResourceLimitPromotions,
		state.projectedShaderResourceFailures,
		state.projectedShaderResourceRecords,
		state.projectedShaderResourceWorldExecutableBinds,
		state.projectedShaderResourceDynamicExecutableBinds,
		GLX_PROJECTED_DLIGHT_STREAM_BINDING );
	RI().Printf( PRINT_ALL, "  dlight projected shader stream: attempts %u, uploads %u, failures %u, skipped %u, records %u, bytes %llu, persistent %u, world %u, dynamic %u, range %u/%u, range failures %u, clears %u, last %u/%u\n",
		state.projectedShaderStreamAttempts,
		state.projectedShaderStreamUploads,
		state.projectedShaderStreamFailures,
		state.projectedShaderStreamSkips,
		state.projectedShaderStreamRecords,
		state.projectedShaderStreamBytes,
		state.projectedShaderStreamPersistentUploads,
		state.projectedShaderStreamWorldUploads,
		state.projectedShaderStreamDynamicUploads,
		state.projectedShaderStreamRangeBinds,
		state.projectedShaderStreamRangeAttempts,
		state.projectedShaderStreamRangeFailures,
		state.projectedShaderStreamRangeClears,
		state.projectedShaderStreamLastOffset,
		state.projectedShaderStreamLastBytes );
	RI().Printf( PRINT_ALL, "  dlight projected shader arena: reserves %u, uploads %u, failures %u, wraps %u, rejects %u, waits %u, timeouts %u, sync failures %u, bytes %llu, light records %u, list records %u, world records %u, dynamic records %u, range %u/%u, range failures %u, clears %u, authoritative %u/%u, auth failures %u, auth fallbacks %u, auth clears %u, bound %s, cursor %u, last %u/%u/%u\n",
		state.projectedShaderArenaReserveAttempts,
		state.projectedShaderArenaUploads,
		state.projectedShaderArenaFailures,
		state.projectedShaderArenaWraps,
		state.projectedShaderArenaSameFrameRejects,
		state.projectedShaderArenaWaits,
		state.projectedShaderArenaTimeouts,
		state.projectedShaderArenaSyncFailures,
		state.projectedShaderArenaBytes,
		state.projectedShaderArenaLightRecordUploads,
		state.projectedShaderArenaListRecordUploads,
		state.projectedShaderArenaWorldRecords,
		state.projectedShaderArenaDynamicRecords,
		state.projectedShaderArenaRangeBinds,
		state.projectedShaderArenaRangeAttempts,
		state.projectedShaderArenaRangeFailures,
		state.projectedShaderArenaRangeClears,
		state.projectedShaderArenaAuthoritativeBinds,
		state.projectedShaderArenaAuthoritativeAttempts,
		state.projectedShaderArenaAuthoritativeFailures,
		state.projectedShaderArenaAuthoritativeFallbacks,
		state.projectedShaderArenaAuthoritativeClears,
		BoolName( state.projectedShaderArenaRangeBound ),
		state.projectedShaderArenaFrameBytes,
		state.projectedShaderArenaLastBuffer,
		state.projectedShaderArenaLastOffset,
		state.projectedShaderArenaLastBytes );
	RI().Printf( PRINT_ALL, "  dlight projected shader MDI commands: attempts %u, eligible %u, uploads %u, failures %u, skipped %u, records %u, indexes %u, bytes %llu, last %u/%u\n",
		state.projectedShaderMdiAttempts,
		state.projectedShaderMdiEligible,
		state.projectedShaderMdiCommandUploads,
		state.projectedShaderMdiCommandFailures,
		state.projectedShaderMdiCommandSkips,
		state.projectedShaderMdiRecords,
		state.projectedShaderMdiIndexes,
		state.projectedShaderMdiCommandBytes,
		state.projectedShaderMdiLastOffset,
		state.projectedShaderMdiLastBytes );
	RI().Printf( PRINT_ALL, "  dlight projected shader MDI command ring: reserves %u, commits %u, wraps %u, failures %u, slots %u, cursor %u, last %u/%u/%u/%u\n",
		state.projectedShaderMdiCommandRingReserves,
		state.projectedShaderMdiCommandRingCommits,
		state.projectedShaderMdiCommandRingWraps,
		state.projectedShaderMdiCommandRingFailures,
		GLX_PROJECTED_DLIGHT_MDI_COMMAND_RING_LIMIT,
		state.projectedShaderMdiCommandRingCursor,
		state.projectedShaderMdiCommandRingLastSlot,
		state.projectedShaderMdiCommandRingLastBuffer,
		state.projectedShaderMdiCommandRingLastOffset,
		state.projectedShaderMdiCommandRingLastBytes );
	RI().Printf( PRINT_ALL, "  dlight projected shader MDI submit: attempts %u, plans %u, ready %u, fallbacks %u, skipped %u, records %u, indexes %u, buffer %u, last %u/%u\n",
		state.projectedShaderMdiSubmitAttempts,
		state.projectedShaderMdiSubmitPlans,
		state.projectedShaderMdiSubmitReady,
		state.projectedShaderMdiSubmitFallbacks,
		state.projectedShaderMdiSubmitSkips,
		state.projectedShaderMdiSubmitRecords,
		state.projectedShaderMdiSubmitIndexes,
		state.projectedShaderMdiSubmitLastBuffer,
		state.projectedShaderMdiSubmitLastOffset,
		state.projectedShaderMdiSubmitLastBytes );
	RI().Printf( PRINT_ALL, "  dlight projected shader MDI batch: attempts %u, batches %u, ready %u, fallbacks %u, rejects %u, gl errors %u, records %u, indexes %u, submitted %u/%u, largest %u, reject %u, buffer %u, range %u/%u\n",
		state.projectedShaderMdiBatchAttempts,
		state.projectedShaderMdiBatchBatches,
		state.projectedShaderMdiBatchReady,
		state.projectedShaderMdiBatchFallbacks,
		state.projectedShaderMdiBatchRejects,
		state.projectedShaderMdiBatchGlErrors,
		state.projectedShaderMdiBatchRecords,
		state.projectedShaderMdiBatchIndexes,
		state.projectedShaderMdiBatchSubmittedDraws,
		state.projectedShaderMdiBatchSubmittedIndexes,
		state.projectedShaderMdiBatchLargest,
		state.projectedShaderMdiBatchLastReject,
		state.projectedShaderMdiBatchLastBuffer,
		state.projectedShaderMdiBatchLastOffset,
		state.projectedShaderMdiBatchLastBytes );
}

static const char *GLX_Module_ProfileName( GlxProfile profile )
{
	switch ( profile ) {
	case GlxProfile::Off:
		return "off";
	case GlxProfile::Rc:
		return "rc";
	case GlxProfile::Stress:
		return "stress";
	default:
		return "custom";
	}
}

static const char *GLX_Module_ProfileValue( const ProfileCvarSetting &setting, GlxProfile profile )
{
	switch ( profile ) {
	case GlxProfile::Off:
		return setting.offValue;
	case GlxProfile::Rc:
		return setting.rcValue;
	case GlxProfile::Stress:
		return setting.stressValue;
	default:
		return "";
	}
}

static qboolean GLX_Module_ParseProfile( const char *name, GlxProfile *profile )
{
	if ( !name || !name[0] ) {
		return qfalse;
	}

	if ( !GLX_Module_Stricmp( name, "off" ) ||
		!GLX_Module_Stricmp( name, "baseline" ) ||
		!GLX_Module_Stricmp( name, "compat" ) ) {
		if ( profile ) {
			*profile = GlxProfile::Off;
		}
		return qtrue;
	}

	if ( !GLX_Module_Stricmp( name, "rc" ) ||
		!GLX_Module_Stricmp( name, "parity" ) ||
		!GLX_Module_Stricmp( name, "candidate" ) ) {
		if ( profile ) {
			*profile = GlxProfile::Rc;
		}
		return qtrue;
	}

	if ( !GLX_Module_Stricmp( name, "stress" ) ) {
		if ( profile ) {
			*profile = GlxProfile::Stress;
		}
		return qtrue;
	}

	return qfalse;
}

static void GLX_Module_CurrentCvarValue( const char *name, char *buffer, int bufferSize )
{
	if ( !buffer || bufferSize <= 0 ) {
		return;
	}

	buffer[0] = '\0';
	if ( RI().Cvar_VariableStringBuffer ) {
		RI().Cvar_VariableStringBuffer( name, buffer, bufferSize );
	}
}

static qboolean GLX_Module_ProfileMatches( GlxProfile profile )
{
	char current[64];

	for ( unsigned int i = 0; i < sizeof( GLX_PROFILE_CVARS ) / sizeof( GLX_PROFILE_CVARS[0] ); i++ ) {
		const ProfileCvarSetting &setting = GLX_PROFILE_CVARS[i];
		const char *expected = GLX_Module_ProfileValue( setting, profile );

		GLX_Module_CurrentCvarValue( setting.name, current, sizeof( current ) );
		if ( GLX_Module_Stricmp( current, expected ) ) {
			return qfalse;
		}
	}

	return qtrue;
}

static const char *GLX_Module_DetectedProfileName()
{
	if ( GLX_Module_ProfileMatches( GlxProfile::Rc ) ) {
		return "rc";
	}
	if ( GLX_Module_ProfileMatches( GlxProfile::Stress ) ) {
		return "stress";
	}
	if ( GLX_Module_ProfileMatches( GlxProfile::Off ) ) {
		return "off";
	}

	return "custom";
}

static void GLX_Module_PrintProfileUsage()
{
	RI().Printf( PRINT_ALL, "usage: glxprofile [off|rc|stress|manual|status]\n" );
	RI().Printf( PRINT_ALL, "  off     restore compatibility defaults for the cvars owned by the GLx profile\n" );
	RI().Printf( PRINT_ALL, "  rc      conservative release-candidate profile: world, stream, dynamic scene, material, bloom, timing\n" );
	RI().Printf( PRINT_ALL, "  stress  rc profile plus indirect static-world stress paths\n" );
	RI().Printf( PRINT_ALL, "  manual  clear r_glxProfile without changing the current cvars\n" );
}

static StreamReservation GLX_Module_FromPublicReservation( const glxStreamReservation_t &reservation )
{
	StreamReservation streamReservation {};

	streamReservation.buffer = reservation.buffer;
	streamReservation.offset = reservation.offset;
	streamReservation.bytes = reservation.bytes;
	streamReservation.ptr = reservation.ptr;
	streamReservation.strategy = static_cast<StreamStrategy>( reservation.strategy );
	streamReservation.mapped = reservation.mapped ? qtrue : qfalse;
	streamReservation.committed = reservation.committed ? qtrue : qfalse;
	streamReservation.reserveWraps = reservation.reserveWraps;
	streamReservation.reserveSameFrameWrapRejects = reservation.reserveSameFrameWrapRejects;
	streamReservation.reserveSyncWaits = reservation.reserveSyncWaits;
	streamReservation.reserveSyncTimeouts = reservation.reserveSyncTimeouts;
	streamReservation.reserveSyncFailures = reservation.reserveSyncFailures;

	return streamReservation;
}

static void GLX_Module_ToPublicReservation( const StreamReservation &streamReservation, glxStreamReservation_t *reservation )
{
	if ( !reservation ) {
		return;
	}

	reservation->buffer = streamReservation.buffer;
	reservation->offset = static_cast<unsigned int>( streamReservation.offset );
	reservation->bytes = static_cast<unsigned int>( streamReservation.bytes );
	reservation->ptr = streamReservation.ptr;
	reservation->strategy = static_cast<int>( streamReservation.strategy );
	reservation->mapped = streamReservation.mapped;
	reservation->committed = streamReservation.committed;
	reservation->reserveWraps = streamReservation.reserveWraps;
	reservation->reserveSameFrameWrapRejects = streamReservation.reserveSameFrameWrapRejects;
	reservation->reserveSyncWaits = streamReservation.reserveSyncWaits;
	reservation->reserveSyncTimeouts = streamReservation.reserveSyncTimeouts;
	reservation->reserveSyncFailures = streamReservation.reserveSyncFailures;
}

static UploadPlan GLX_Module_ClientMemoryUploadPlan()
{
	return GLX_RenderIR_MakeUploadPlan( UploadPlanKind::ClientMemory, -1, 0, 0, 0 );
}

static UploadPlan GLX_Module_StreamUploadPlan( int totalBytes, int vertexBytes, int indexBytes )
{
	UploadPlan plan = GLX_RenderIR_MakeUploadPlan( UploadPlanKind::TransientStream,
		-1, totalBytes > 0 ? static_cast<unsigned int>( totalBytes ) : 0,
		vertexBytes > 0 ? static_cast<unsigned int>( vertexBytes ) : 0,
		indexBytes > 0 ? static_cast<unsigned int>( indexBytes ) : 0 );
	plan.alignment = 64;
	plan.sync = UploadSyncPolicy::FrameFence;
	return plan;
}

static MaterialIR GLX_Module_DrawMaterialIR( int profilerPath )
{
	MaterialIR material = GLX_RenderIR_MakeMaterial( 0, 0, 0, 0 );
	if ( profilerPath == GLX_DRAW_DEBUG ) {
		material.flags = GLX_STAGE_DETAIL;
	}
	return material;
}

static MaterialIR GLX_Module_StageMaterialIR( int sort, int flags, unsigned int stateBits,
	int rgbGen, int alphaGen, int tcGen0, int tcGen1, int texMods0, int texMods1,
	unsigned int texModTypes0, unsigned int texModTypes1,
	unsigned int texModSequence0, unsigned int texModSequence1,
	int rgbWaveFunc, int alphaWaveFunc,
	unsigned int texModWaveFuncs0, unsigned int texModWaveFuncs1,
	int fogAdjust, int materialCombine, qboolean fogPass )
{
	MaterialIR material = GLX_RenderIR_MakeMaterial( sort, flags, stateBits, 1 );
	material.rgbGen = rgbGen;
	material.alphaGen = alphaGen;
	material.rgbWaveFunc = rgbWaveFunc;
	material.alphaWaveFunc = alphaWaveFunc;
	material.tcGen0 = tcGen0;
	material.tcGen1 = tcGen1;
	material.texMods0 = texMods0;
	material.texMods1 = texMods1;
	material.texModTypes0 = texModTypes0;
	material.texModTypes1 = texModTypes1;
	material.texModSequence0 = texModSequence0;
	material.texModSequence1 = texModSequence1;
	material.texModWaveFuncs0 = texModWaveFuncs0;
	material.texModWaveFuncs1 = texModWaveFuncs1;
	material.fogAdjust = fogAdjust;
	material.materialCombine = materialCombine;
	material.fogPass = fogPass;
	return material;
}

static DynamicDraw GLX_Module_IndexedDrawIR( unsigned int mode, int count, unsigned int type,
	const void *indices, int legacyReason, int profilerPath, int materialFlags,
	unsigned int categoryMask )
{
	DynamicDraw draw {};
	draw.kind = DynamicDrawKind::Indexed;
	draw.role = GLX_RenderIR_ClassifyDynamicDrawRole( materialFlags, categoryMask );
	draw.pass = GLX_RenderIR_DefaultPassForDynamicDrawRole( draw.role );
	draw.primitive = mode;
	draw.count = count;
	draw.indexType = type;
	draw.indices = indices;
	draw.legacyReason = legacyReason;
	draw.profilerPath = profilerPath;
	draw.material = GLX_Module_DrawMaterialIR( profilerPath );
	draw.material.flags |= materialFlags;
	draw.upload = legacyReason >= 0 ? GLX_Module_ClientMemoryUploadPlan() :
		GLX_Module_StreamUploadPlan( count > 0 ? count : 0, 0, count > 0 ? count : 0 );
	return draw;
}

static DynamicDraw GLX_Module_ArrayDrawIR( unsigned int mode, int first, int count,
	int legacyReason, int profilerPath, int materialFlags, unsigned int categoryMask )
{
	DynamicDraw draw {};
	draw.kind = DynamicDrawKind::Arrays;
	draw.role = GLX_RenderIR_ClassifyDynamicDrawRole( materialFlags, categoryMask );
	draw.pass = GLX_RenderIR_DefaultPassForDynamicDrawRole( draw.role );
	draw.primitive = mode;
	draw.first = first;
	draw.count = count;
	draw.legacyReason = legacyReason;
	draw.profilerPath = profilerPath;
	draw.material = GLX_Module_DrawMaterialIR( profilerPath );
	draw.material.flags |= materialFlags;
	draw.upload = legacyReason >= 0 ? GLX_Module_ClientMemoryUploadPlan() :
		GLX_Module_StreamUploadPlan( count > 0 ? count : 0, count > 0 ? count : 0, 0 );
	return draw;
}

static WorldPacket GLX_Module_WorldPacketIR( int packetIndex, int sort,
	int surfaces, int vertexes, int indexes, int firstItem, int itemCount,
	int vertexOffset, int vertexBytes, int indexOffset, int indexBytes,
	int shaderStagePasses, int flags )
{
	WorldPacket packet {};
	packet.packetIndex = packetIndex;
	packet.pass = FramePassKind::SkyAndOpaqueWorld;
	packet.surfaces = surfaces;
	packet.vertexes = vertexes;
	packet.indexes = indexes;
	packet.firstItem = firstItem;
	packet.itemCount = itemCount;
	packet.vertexOffset = vertexOffset;
	packet.indexOffset = indexOffset;
	packet.material = GLX_RenderIR_MakeMaterial( sort, flags, 0, shaderStagePasses );
	packet.upload = GLX_RenderIR_MakeUploadPlan( UploadPlanKind::StaticWorld, -1,
		static_cast<unsigned int>( ( vertexBytes > 0 ? vertexBytes : 0 ) +
			( indexBytes > 0 ? indexBytes : 0 ) ),
		vertexBytes > 0 ? static_cast<unsigned int>( vertexBytes ) : 0,
		indexBytes > 0 ? static_cast<unsigned int>( indexBytes ) : 0 );
	return packet;
}

static WorldPacket GLX_Module_StaticWorldPacketIR(
	const StaticWorldPacketStats &staticPacket, int packetIndex,
	const ProjectedDlightListRef &projectedDlights )
{
	WorldPacket packet = GLX_Module_WorldPacketIR( packetIndex, staticPacket.sort,
		staticPacket.surfaces, staticPacket.vertexes, staticPacket.indexes,
		staticPacket.firstItem, staticPacket.itemCount, staticPacket.vertexOffset,
		staticPacket.vertexBytes, staticPacket.indexOffset, staticPacket.indexBytes,
		staticPacket.shaderStagePasses, staticPacket.flags );

	packet.projectedDlights = projectedDlights;
	return packet;
}

static int GLX_Module_StaticWorldPacketForItemRange(
	const StaticWorldStats &staticWorld, int firstItem, int itemCount )
{
	int packetIndex;
	const long long lastItem =
		static_cast<long long>( firstItem ) + static_cast<long long>( itemCount ) - 1;

	if ( firstItem <= 0 || itemCount <= 0 || lastItem < firstItem ||
		lastItem > GLX_STATIC_WORLD_ITEM_LOOKUP_LIMIT ) {
		return -1;
	}

	packetIndex = staticWorld.itemToPacket[firstItem];
	if ( packetIndex < 0 || packetIndex >= staticWorld.packetCount ||
		packetIndex >= GLX_STATIC_WORLD_PACKET_LIMIT ) {
		return -1;
	}

	for ( int item = firstItem + 1; item <= static_cast<int>( lastItem ); item++ ) {
		if ( staticWorld.itemToPacket[item] != packetIndex ) {
			return -1;
		}
	}
	return packetIndex;
}

static OutputTransform GLX_Module_OutputTransformIR( const PostProcessState &postprocess )
{
	if ( GLX_RenderIR_ValidateOutputTransform( postprocess.lastOutput ) ) {
		return postprocess.lastOutput;
	}
	return GLX_RenderIR_DefaultOutputTransform();
}

static qboolean GLX_Module_PostOutputPlanHasNode( const PostOutputPlan &plan,
	PostNodeKind kind )
{
	for ( int i = 0; i < plan.nodeCount && i < GLX_RENDER_IR_MAX_POST_OUTPUT_NODES; i++ ) {
		if ( plan.nodes[i].kind == kind ) {
			return qtrue;
		}
	}
	return qfalse;
}

static unsigned int GLX_Module_PostOutputExpectedShaderBinds( const PostOutputPlan &plan )
{
	unsigned int binds = 0u;

	for ( int i = 0; i < plan.nodeCount && i < GLX_RENDER_IR_MAX_POST_OUTPUT_NODES; i++ ) {
		switch ( plan.nodes[i].kind ) {
		case PostNodeKind::BloomPrefinal:
		case PostNodeKind::BloomFinal:
		case PostNodeKind::GammaDirect:
		case PostNodeKind::GammaBlit:
			binds++;
			break;
		default:
			break;
		}
	}
	return binds;
}

static PostShaderPlan GLX_Module_PostShaderPlanForOutputPlan(
	const OutputTransform &output, const PostOutputPlan &plan )
{
	const qboolean bloomFinal = GLX_Module_PostOutputPlanHasNode( plan,
		PostNodeKind::BloomFinal );
	return GLX_PostShader_BuildPlanForOutput( output, bloomFinal );
}

static qboolean GLX_Module_EmitFrameSchedule( FramePass *passes, int capacity, int *count )
{
	return GLX_RenderIR_DefaultPassSchedule( passes, capacity, count );
}

class RendererModule {
public:
	void RegisterCommands();
	void RemoveCommands();
	void OnOpenGLReady( const glconfig_t *config, const char *extensions );
	void Shutdown( int code );
	void BeginBackendTimer();
	void EndBackendTimer();
	void BeginGpuPassTimer( int pass );
	void EndGpuPassTimer( int pass );
	void FrameComplete();
	void PrintCaps() const;
	void PrintInfo() const;
	void PrintProfile() const;
	void ProfileCommand();
	void PrintMaterial() const;
	void PrintPostProcess() const;
	void PrintStaticWorld() const;
	void PrintFrameCounters() const;
	void StreamTest();
	qboolean DrawElements( unsigned int mode, int count, unsigned int type,
		const void *indices, int legacyReason, int profilerPath );
	qboolean DrawArrays( unsigned int mode, int first, int count,
		int legacyReason, int profilerPath );
	qboolean DrawElementsClassified( unsigned int mode, int count, unsigned int type,
		const void *indices, int legacyReason, int profilerPath,
		int materialFlags, unsigned int categoryMask );
	qboolean DrawElementsClassifiedProjectedDlights( unsigned int mode, int count,
		unsigned int type, const void *indices, int legacyReason, int profilerPath,
		int materialFlags, unsigned int categoryMask, unsigned int lightMask );
	qboolean DrawArraysClassified( unsigned int mode, int first, int count,
		int legacyReason, int profilerPath, int materialFlags, unsigned int categoryMask );
	void RecordDraw( int indexes, int path );
	void RecordShaderBatch( const char *shaderName, int sort, int numPasses, int numVertexes, int numIndexes, int flags );
	void RecordMaterialStage( int path, int flags, unsigned int stateBits, int rgbGen, int alphaGen,
		int tcGen0, int tcGen1, int texMods0, int texMods1,
		unsigned int texModTypes0, unsigned int texModTypes1,
		unsigned int texModSequence0, unsigned int texModSequence1,
		int rgbWaveFunc, int alphaWaveFunc,
		unsigned int texModWaveFuncs0, unsigned int texModWaveFuncs1,
		int fogAdjust, int materialCombine, qboolean fogPass,
		int numVertexes, int numIndexes );
	qboolean MaterialRendererActive() const;
	qboolean BindMaterialStage( int flags, unsigned int stateBits, int rgbGen, int alphaGen,
		int tcGen0, int tcGen1, int texMods0, int texMods1,
		unsigned int texModTypes0, unsigned int texModTypes1,
		unsigned int texModSequence0, unsigned int texModSequence1,
		int rgbWaveFunc, int alphaWaveFunc,
		unsigned int texModWaveFuncs0, unsigned int texModWaveFuncs1, int fogAdjust,
		int materialCombine, qboolean fogPass );
	qboolean BindFogMaterial();
	qboolean BindLiquidMaterial( const float *params, const float *eyeAndCount,
		const float *targetInverse, const float *reflect,
		const float *impulses, const float *amplitudes );
	void UnbindMaterial();
	qboolean DlightProgramAvailable( qboolean linear, int fogMode, qboolean absLight,
		qboolean shadow );
	qboolean BindDlightProgram( qboolean linear, int fogMode, qboolean absLight,
		qboolean shadow,
		const float *eyePos, const float *lightPos, const float *lightColor,
		const float *lightVector, const float *texFactors, const float *dlightFactors,
		const float *fogDistanceVector, const float *fogDepthVector, float fogEyeT,
		const float *dlightShadow, const float *shadowAtlas,
		const float *shadowDepth, const float *shadowFilter );
	void UnbindDlightProgram();
	qboolean DlightProjectedProgramEnabled() const;
	qboolean ProjectedDlightWorldOverlayActive();
	qboolean BindProjectedDlightOverlayProgram();
	qboolean DlightScissorEnabled() const;
	void RecordDlightState( int event );
	void RecordDlightBuild( int legacyLights, int legacySkippedLights,
		int legacyNoHitLights, int legacyVertexes, int legacyIndexes,
		int legacyLitIndexes, int pmPasses, int pmVertexes, int pmIndexes );
	void RecordDlightCull( int legacyVertexes, int legacyIndexes );
	void RecordDlightScissor( qboolean computed, qboolean applied, int x, int y,
		int width, int height, int viewportWidth, int viewportHeight );
	void RecordProjectedDlights( const glxProjectedDlightRecord_t *records, int count );
	void RecordProjectedDlightList( int itemIndex, unsigned int lightMask );
	qboolean StreamDrawEnabled() const;
	qboolean StreamDrawMultitextureEnabled() const;
	qboolean StreamDrawFogEnabled() const;
	qboolean StreamDrawDepthFragmentEnabled() const;
	qboolean StreamDrawShadowsEnabled() const;
	qboolean StreamDrawBeamsEnabled() const;
	qboolean StreamDrawPostProcessEnabled() const;
	qboolean StreamDrawAllowsMaterial( int flags, unsigned int stateBits,
		int rgbGen, int alphaGen, int tcGen0, int tcGen1, int texMods0, int texMods1,
		unsigned int texModTypes0, unsigned int texModTypes1,
		unsigned int texModSequence0, unsigned int texModSequence1,
		int rgbWaveFunc, int alphaWaveFunc,
		unsigned int texModWaveFuncs0, unsigned int texModWaveFuncs1,
		int fogAdjust, int materialCombine, qboolean fogPass );
	qboolean StreamReserve( int bytes, int alignment, glxStreamReservation_t *reservation );
	qboolean StreamUploadAt( glxStreamReservation_t *reservation, int relativeOffset, const void *data, int bytes );
	void StreamCommit( glxStreamReservation_t *reservation );
	void RecordStreamDlightReservation( const glxStreamReservation_t *reservation );
	GLuint BindStreamArrayBuffer( GLuint buffer );
	void RestoreStreamArrayBuffer( GLuint buffer );
	GLuint BindStreamElementArrayBuffer( GLuint buffer );
	void RestoreStreamElementArrayBuffer( GLuint buffer );
	void RecordStreamBufferBind( unsigned int target, GLuint buffer );
	void RecordStreamDrawResult( int numVertexes, int numIndexes, int totalBytes, int indexBytes,
		int texcoord1Bytes, qboolean multitexture, qboolean fog, qboolean depthFragment, int materialFlags,
		unsigned int categoryMask, qboolean success );
	void RecordStreamDrawSkip( int reason );
	void ResetImageColorAudit();
	void RecordImageColorAudit( int colorSpace, qboolean srgbDecode );
	void RecordFboInit( qboolean requested, qboolean ready, qboolean programReady, qboolean framebufferFnsReady,
		int vidWidth, int vidHeight, int captureWidth, int captureHeight, int windowWidth, int windowHeight,
		int internalFormat, int textureFormat, int textureType, qboolean multiSampled, qboolean superSampled,
		qboolean windowAdjusted, int blitFilter, int hdrMode, int renderScaleMode, int bloomMode,
		qboolean textureSrgbAvailable, qboolean framebufferSrgbAvailable, qboolean framebufferSrgbEnabled );
	void RecordFboShutdown();
	void RecordPostProcessFrame( qboolean minimized, qboolean bloomAvailable, qboolean programReady,
		int screenshotMask, qboolean windowAdjusted, int fboReadIndex, int hdrMode, int renderScaleMode,
		float greyscale, float legacyGamma, float legacyOverbright );
	qboolean AutoExposureNeedsSamples( int *width, int *height ) const;
	float UpdateAutoExposure( float manualExposure, const float *rgba, int width,
		int height );
	qboolean TryBindPostShaderFinal( qboolean bloomComposite, qboolean outputTransform,
		float bloomIntensity );
	qboolean TryBindPostShaderDirectFinal();
	void UnbindPostShader();
	void RecordPostProcessResult( int result );
	void RecordColorGradeLut( qboolean active, int size, float scale );
	void RecordBloomCreate( int result, int requestedPasses, int effectivePasses,
		int textureUnits, int formatMode, int internalFormat, int textureFormat,
		int textureType );
	void RecordBloom( int result, qboolean finalStage, int bloomMode, int requestedPasses,
		int effectivePasses, int blendBase, int filterSize, int textureUnits, int thresholdMode,
		int modulate, float threshold, float intensity, float reflection );
	void RecordFboCopyScreen( int viewportWidth, int viewportHeight );
	void RecordFboBlit( int kind, qboolean depthOnly, int srcWidth, int srcHeight, int dstWidth, int dstHeight );
	void RecordFboBind();
	void RecordPostClear();
	void RecordFullscreenPass();
	void PushShaderDebugGroup( const char *shaderName, int numVertexes, int numIndexes, int numPasses );
	void PopDebugGroup();
	void ShadowUploadTess( int numVertexes, int numIndexes, const void *xyz, int xyzBytes, const void *indexes, int indexBytes );
	void RecordStaticWorldCache( int surfaces, int vertexes, int indexes, int vertexBytes, int indexBytes );
	void RecordStaticWorldBatches( int batches, int largestBatchSurfaces, int faceSurfaces,
		int gridSurfaces, int triangleSurfaces, int shaderStagePasses, int maxShaderStages );
	void RecordStaticWorldPacket( const char *shaderName, int sort,
		int surfaces, int vertexes, int indexes, int firstItem, int itemCount, int vertexOffset, int vertexBytes,
		int indexOffset, int indexBytes, int shaderStagePasses, int flags );
	void UploadStaticWorldArena( const void *vertexData, int vertexBytes, const void *indexData, int indexBytes );
	GLuint StaticWorldArenaVertexBuffer();
	GLuint StaticWorldArenaIndexBuffer();
	qboolean StaticWorldDrawDeviceRun( int indexes, int offsetBytes,
		int firstItem, int itemCount, unsigned int indexType, int indexBytes,
		const char *shaderName, int sort, qboolean arenaBound );
	qboolean StaticWorldDrawDeviceRuns( int runCount, const int *counts, const void *const *offsets,
		const int *firstItems, const int *itemCounts, unsigned int indexType, int indexBytes,
		const char *shaderName, int sort, qboolean arenaBound );
	qboolean StaticWorldDrawSoftIndexes( int indexes, const void *indexData,
		unsigned int indexType, int indexBytes, const char *shaderName, int sort, qboolean arenaBound );
	int StaticWorldDrawDeviceRunsFiltered( int runCount, const int *counts, const void *const *offsets,
		const int *firstItems, const int *itemCounts, int *drawnRuns, unsigned int indexType, int indexBytes,
		const char *shaderName, int sort, qboolean arenaBound );
	qboolean PrepareStaticWorldProjectedDlightRun( int firstItem, int itemCount,
		WorldPacket *packet, ProjectedDlightShaderInput *shaderInput );
	qboolean StageProjectedDlightShaderInput( const ProjectedDlightShaderInput &shaderInput,
		ProjectedDlightResourceRange *resourceRange );
	qboolean StageProjectedDlightDynamicMdiCommand(
		const ProjectedDlightDynamicMdiPlan &mdiPlan,
		ProjectedDlightDynamicMdiCommandUpload *commandUpload );
	qboolean BindStaticWorldProjectedDlightRun( const WorldPacket &packet,
		const ProjectedDlightShaderInput &shaderInput,
		ProjectedDlightShaderExecutionPlan *executionPlan );
	void ConsumeStaticWorldProjectedDlightRun( const WorldPacket &packet,
		const ProjectedDlightShaderInput &shaderInput );
	void ConsumeStaticWorldProjectedDlightRun( int firstItem, int itemCount );
	qboolean StaticWorldProjectedDlightSplitActive();
	int StaticWorldDrawProjectedDlightRunsFiltered( int runCount, const int *counts,
		const void *const *offsets, const int *firstItems, const int *itemCounts,
		int *drawnRuns, unsigned int indexType, int indexBytes,
		const char *shaderName, int sort, qboolean arenaBound );
	void RecordStaticWorldQueue( int queuedItems, int queuedVertexes, int queuedIndexes,
		int deviceRuns, int deviceIndexes, int softIndexes, int largestDeviceRunIndexes );
	void RecordStaticWorldDeviceRuns( int runCount, const int *counts, const void *const *offsets,
		const int *firstItems, const int *itemCounts, int indexBytes, const char *shaderName, int sort );

private:
	void ApplyProfile( GlxProfile profile, qboolean rememberProfile, qboolean startupProfile );
	void ApplyStartupProfile();

	Capabilities caps_ {};
	DebugState debug_ {};
	ExecutorState executor_ {};
	MaterialState material_ {};
	DlightState dlight_ {};
	PostProcessState postprocess_ {};
	PostShaderState postShader_ {};
	unsigned int postShaderBindBaseline_ {};
	unsigned int postShaderExpectedBinds_ {};
	ProfilerState profiler_ {};
	cvar_t *profile_ {};
	cvar_t *requireOwnership_ {};
	StaticWorldStats staticWorld_ {};
	StreamState stream_ {};
};

RendererModule g_module;

void RendererModule::RegisterCommands()
{
	RI().Cmd_AddCommand( "glxinfo", GLX_Renderer_PrintInfo_f );
	RI().Cmd_AddCommand( "glxcaps", GLX_Renderer_PrintCaps_f );
	RI().Cmd_AddCommand( "glxprofile", GLX_Renderer_Profile_f );
	RI().Cmd_AddCommand( "glxmaterial", GLX_Renderer_Material_f );
	RI().Cmd_AddCommand( "glxpostprocess", GLX_Renderer_PostProcess_f );
	RI().Cmd_AddCommand( "glxstaticworld", GLX_Renderer_StaticWorld_f );
	RI().Cmd_AddCommand( "glxstreamtest", GLX_Renderer_StreamTest_f );

	profile_ = RI().Cvar_Get( "r_glxProfile", "", CVAR_ARCHIVE_ND );
	RI().Cvar_SetDescription( profile_,
		"Apply a named GLx startup profile during renderer registration: off, rc, stress, manual, or blank for manual cvars." );
	requireOwnership_ = RI().Cvar_Get( "r_glxRequireOwnership", "0", CVAR_ARCHIVE_ND );
	RI().Cvar_SetDescription( requireOwnership_,
		"Reject GLx legacy-delegation draw submissions so ownership-proof runs cannot pass through compatibility draw paths." );

	GLX_Debug_RegisterCvars( &debug_ );
	GLX_Material_RegisterCvars( &material_ );
	GLX_Dlight_RegisterCvars( &dlight_ );
	GLX_PostProcess_RegisterCvars( &postprocess_ );
	GLX_PostShader_RegisterCvars( &postShader_ );
	GLX_Profiler_RegisterCvars( &profiler_ );
	GLX_StaticWorld_RegisterCvars( &staticWorld_ );
	GLX_Stream_RegisterCvars( &stream_ );

	ApplyStartupProfile();
}

void RendererModule::RemoveCommands()
{
	RI().Cmd_RemoveCommand( "glxcaps" );
	RI().Cmd_RemoveCommand( "glxinfo" );
	RI().Cmd_RemoveCommand( "glxprofile" );
	RI().Cmd_RemoveCommand( "glxmaterial" );
	RI().Cmd_RemoveCommand( "glxpostprocess" );
	RI().Cmd_RemoveCommand( "glxstaticworld" );
	RI().Cmd_RemoveCommand( "glxstreamtest" );
}

void RendererModule::OnOpenGLReady( const glconfig_t *config, const char *extensions )
{
	FramePass frameSchedule[GLX_RENDER_IR_PASS_COUNT];
	int frameScheduleCount = 0;

	GLX_Caps_Init( &caps_, config, extensions );
	GLX_Executor_Init( &executor_, caps_ );
	if ( !GLX_Module_EmitFrameSchedule( frameSchedule, GLX_RENDER_IR_PASS_COUNT, &frameScheduleCount ) ||
		!GLX_Executor_ConsumeFrameSchedule( &executor_, frameSchedule, frameScheduleCount ) ) {
		RI().Error( ERR_DROP, "GLx front-end pass schedule is invalid" );
	}
	GLX_Debug_OnOpenGLReady( &debug_, caps_ );
	GLX_Material_OnOpenGLReady( &material_, caps_ );
	GLX_Dlight_OnOpenGLReady( &dlight_ );
	GLX_PostProcess_OnOpenGLReady( &postprocess_, caps_ );
	GLX_PostShader_OnOpenGLReady( &postShader_, caps_ );
	GLX_Stream_OnOpenGLReady( &stream_, caps_ );
	GLX_StaticWorld_SetCapabilities( &staticWorld_, caps_.features.drawIndirect, caps_.features.multiDrawIndirect );
	GLX_Debug_LabelObject( debug_, GL_BUFFER, stream_.buffer, "GLx dynamic stream ring" );
	GLX_Profiler_OnOpenGLReady( &profiler_, caps_ );

	RI().Printf( PRINT_ALL, "GLx renderer bootstrap: product tier %s, hint %s, executor %s/%s, GL %i.%i, material %s, dlight %s, post-shader %s, stream %s, timer query %s, debug output %s\n",
		GLX_Caps_TierName( caps_.tier ), GLX_Caps_HintName( caps_.hint ),
		GLX_Executor_TierName( executor_ ), GLX_Executor_ModeName( executor_ ),
		caps_.major, caps_.minor,
		BoolName( GLX_Material_Active( material_ ) ),
		BoolName( GLX_Dlight_Active( dlight_ ) ),
		BoolName( GLX_PostShader_Ready( postShader_ ) ),
		GLX_Stream_StrategyName( stream_.strategy ),
		BoolName( GLX_Profiler_TimerReady( profiler_ ) ),
		BoolName( GLX_Debug_CallbackInstalled( debug_ ) ) );
	RI().Printf( PRINT_ALL, "GLx pass schedule: %s %i/%08x %s\n",
		executor_.frameScheduleValid ? "valid" : "invalid",
		executor_.frameScheduleCount,
		executor_.frameScheduleHash,
		executor_.frameScheduleText[0] ? executor_.frameScheduleText : "none" );
}

void RendererModule::Shutdown( int code )
{
	(void)code;

	GLX_Dlight_Shutdown( &dlight_, qtrue );
	GLX_Material_Shutdown( &material_, qtrue );
	GLX_PostShader_Shutdown( &postShader_, qtrue );
	if ( code == REF_KEEP_CONTEXT ) {
		postprocess_.glReady = qfalse;
	} else {
		GLX_PostProcess_Shutdown( &postprocess_ );
	}
	GLX_Profiler_Shutdown( &profiler_ );
	GLX_Debug_Shutdown( &debug_ );
	GLX_Executor_Shutdown( &executor_ );
	GLX_Stream_Shutdown( &stream_ );
	GLX_StaticWorld_Clear( &staticWorld_ );
	GLX_Caps_Reset( &caps_ );
}

void RendererModule::ApplyProfile( GlxProfile profile, qboolean rememberProfile, qboolean startupProfile )
{
	const char *profileName = GLX_Module_ProfileName( profile );

	if ( rememberProfile && profile_ ) {
		RI().Cvar_Set( profile_->name, profileName );
	}

	for ( unsigned int i = 0; i < sizeof( GLX_PROFILE_CVARS ) / sizeof( GLX_PROFILE_CVARS[0] ); i++ ) {
		const ProfileCvarSetting &setting = GLX_PROFILE_CVARS[i];
		RI().Cvar_Set( setting.name, GLX_Module_ProfileValue( setting, profile ) );
	}

	if ( startupProfile ) {
		RI().Printf( PRINT_ALL, "glxprofile: applied %s startup profile\n", profileName );
	} else {
		RI().Printf( PRINT_ALL, "glxprofile: applied %s profile\n", profileName );
		RI().Printf( PRINT_ALL, "glxprofile: FBO and stream settings apply automatically; reload the map for VBO/static-world resources.\n" );
	}
}

void RendererModule::ApplyStartupProfile()
{
	GlxProfile profile;

	if ( !profile_ || !profile_->string || !profile_->string[0] ) {
		return;
	}

	if ( !GLX_Module_Stricmp( profile_->string, "manual" ) ||
		!GLX_Module_Stricmp( profile_->string, "custom" ) ) {
		return;
	}

	if ( !GLX_Module_ParseProfile( profile_->string, &profile ) ) {
		RI().Printf( PRINT_WARNING, "glxprofile: unknown r_glxProfile '%s'; expected off, rc, stress, manual, or blank\n",
			profile_->string );
		return;
	}

	ApplyProfile( profile, qfalse, qtrue );
}

void RendererModule::BeginBackendTimer()
{
	GLX_Profiler_BeginBackendTimer( &profiler_ );
}

void RendererModule::EndBackendTimer()
{
	GLX_Profiler_EndBackendTimer( &profiler_ );
}

void RendererModule::BeginGpuPassTimer( int pass )
{
	GLX_Profiler_BeginGpuPassTimer( &profiler_, pass );
}

void RendererModule::EndGpuPassTimer( int pass )
{
	GLX_Profiler_EndGpuPassTimer( &profiler_, pass );
}

void RendererModule::FrameComplete()
{
	GLX_Profiler_FrameComplete( &profiler_ );
	GLX_Material_FrameComplete( &material_ );
	GLX_PostShader_FrameComplete( &postShader_ );
	GLX_Stream_FrameComplete( &stream_ );
	GLX_Dlight_ClearProjectedFrame( &dlight_ );
	GLX_Debug_UpdateCvars( &debug_, caps_ );
	GLX_Stream_UpdateCvars( &stream_, caps_ );
}

void RendererModule::PrintCaps() const
{
	if ( !caps_.config ) {
		RI().Printf( PRINT_ALL, "GLx renderer bootstrap is loaded, but OpenGL is not initialized yet.\n" );
		return;
	}

	RI().Printf( PRINT_ALL, "\nGLx renderer bootstrap\n" );
	RI().Printf( PRINT_ALL, "  profile: %s (startup %s)\n",
		GLX_Module_DetectedProfileName(),
		profile_ && profile_->string && profile_->string[0] ? profile_->string : "manual" );
	RI().Printf( PRINT_ALL, "  GL vendor: %s\n", caps_.config->vendor_string );
	RI().Printf( PRINT_ALL, "  GL renderer: %s\n", caps_.config->renderer_string );
	RI().Printf( PRINT_ALL, "  GL version: %s\n", caps_.config->version_string );
	RI().Printf( PRINT_ALL, "  product tier: %s\n", GLX_Caps_TierName( caps_.tier ) );
	RI().Printf( PRINT_ALL, "  capability hint: %s\n", GLX_Caps_HintName( caps_.hint ) );
	RI().Printf( PRINT_ALL, "  render IR executor tier: %s\n", GLX_Executor_TierName( executor_ ) );
	RI().Printf( PRINT_ALL, "  render IR executor mode: %s\n", GLX_Executor_ModeName( executor_ ) );
	RI().Printf( PRINT_ALL, "  map buffer range: %s\n", BoolName( caps_.features.mapBufferRange ) );
	RI().Printf( PRINT_ALL, "  uniform buffers: %s\n", BoolName( caps_.features.uniformBufferObject ) );
	RI().Printf( PRINT_ALL, "  instanced arrays: %s\n", BoolName( caps_.features.instancedArrays ) );
	RI().Printf( PRINT_ALL, "  persistent buffers: %s\n", BoolName( caps_.features.bufferStorage ) );
	RI().Printf( PRINT_ALL, "  sync objects: %s\n", BoolName( caps_.features.syncObjects ) );
	RI().Printf( PRINT_ALL, "  draw indirect: %s\n", BoolName( caps_.features.drawIndirect ) );
	RI().Printf( PRINT_ALL, "  multi draw indirect: %s\n", BoolName( caps_.features.multiDrawIndirect ) );
	RI().Printf( PRINT_ALL, "  direct state access: %s\n", BoolName( caps_.features.directStateAccess ) );
	RI().Printf( PRINT_ALL, "  debug context: %s\n", BoolName( caps_.features.debugContext ) );
	RI().Printf( PRINT_ALL, "  debug output: %s\n", BoolName( caps_.features.debugOutput ) );
	RI().Printf( PRINT_ALL, "  KHR_debug labels/groups: %s\n", BoolName( caps_.features.khrDebug ) );
	RI().Printf( PRINT_ALL, "  timer query feature: %s\n", BoolName( caps_.features.timerQuery ) );
	RI().Printf( PRINT_ALL, "  timer query active: %s\n", BoolName( GLX_Profiler_TimerReady( profiler_ ) ) );
	RI().Printf( PRINT_ALL, "  debug output callback: %s\n", BoolName( GLX_Debug_CallbackInstalled( debug_ ) ) );
	RI().Printf( PRINT_ALL, "  KHR_debug groups: %s requested, %s available\n",
		BoolName( debug_.r_glxDebugGroups && debug_.r_glxDebugGroups->integer ? qtrue : qfalse ),
		BoolName( debug_.fns.PushDebugGroup && debug_.fns.PopDebugGroup ? qtrue : qfalse ) );
	RI().Printf( PRINT_ALL, "  material renderer: %s, ready %s, GLSL %s, programs %i\n",
		material_.r_glxMaterialRenderer && material_.r_glxMaterialRenderer->integer ? "enabled" : "disabled",
		BoolName( material_.ready ),
		material_.glslVersion[0] ? material_.glslVersion : "unknown",
		material_.programCount );
	RI().Printf( PRINT_ALL, "  post shader cache: ready %s, programs %i/%i, compile %u attempts/%u failures, link failures %u, source hash 0x%08x, target %s, evictions %u\n",
		BoolName( GLX_PostShader_Ready( postShader_ ) ),
		postShader_.programCount, GLX_POST_SHADER_PROGRAM_LIMIT,
		postShader_.compileAttempts, postShader_.compileFailures,
		postShader_.linkFailures, postShader_.lastSourceHash,
		GLX_PostShaderSource_TargetName( postShader_.activeTarget ),
		postShader_.cacheEvictions );
	RI().Printf( PRINT_ALL, "  postprocess FBO: %s, render %ix%i, capture %ix%i, bloom %i, passes %i/%i, last %s\n",
		BoolName( postprocess_.fboReady ), postprocess_.vidWidth, postprocess_.vidHeight,
		postprocess_.captureWidth, postprocess_.captureHeight, postprocess_.bloomMode,
		postprocess_.lastBloomEffectivePasses, postprocess_.lastBloomRequestedPasses,
		GLX_PostProcess_ResultName( postprocess_.lastResult ) );
	RI().Printf( PRINT_ALL, "  post/output ownership: mode %s, post nodes %u, outputs %u, legacy fallback %s, executable nodes %u, executable outputs %u, post hash 0x%08x, output hash 0x%08x, plan hash 0x%08x, fallback 0x%08x\n",
		GLX_PostProcess_PostOutputModeName( postprocess_.lastPostOutputGlxOwned ),
		executor_.postNodes,
		executor_.outputTransforms,
		BoolName( postprocess_.lastPostOutputGlxOwned ? qfalse : qtrue ),
		postprocess_.lastPostOutputExecutableNodeCount,
		postprocess_.lastPostOutputExecutableOutputCount,
		executor_.lastPostNodeHash,
		executor_.lastOutputTransformHash,
		postprocess_.lastPostOutputPlanHash,
		postprocess_.lastPostOutputFallbackReasons );
	RI().Printf( PRINT_ALL, "  post shader plan: valid %s, features 0x%08x, hash 0x%08x, textures %u, uniforms %u, frames %u, invalid %u\n",
		BoolName( postprocess_.lastPostShaderPlanValid ),
		postprocess_.lastPostShaderFeatureMask,
		postprocess_.lastPostShaderPlanHash,
		postprocess_.lastPostShaderTextureCount,
		postprocess_.lastPostShaderUniformVec4Count,
		postprocess_.postShaderPlanFrames,
		postprocess_.postShaderPlanInvalidFrames );
	RI().Printf( PRINT_ALL, "  post shader source: valid %s, truncated %s, hash 0x%08x, vertex bytes %i, fragment bytes %i, last program %u\n",
		BoolName( postShader_.lastSource.valid ),
		BoolName( postShader_.lastSource.truncated ),
		postShader_.lastSourceHash,
		postShader_.lastSource.vertexBytes,
		postShader_.lastSource.fragmentBytes,
		postShader_.lastProgram );
	RI().Printf( PRINT_ALL, "  post shader direct-final: execute %s, eligible %s, bound %s, reject 0x%08x, candidates %u, eligible frames %u, attempts %u, binds %u, fallbacks %u, rejects %u\n",
		BoolName( postShader_.r_glxPostShaderExecute && postShader_.r_glxPostShaderExecute->integer ? qtrue : qfalse ),
		BoolName( postShader_.lastDirectFinalEligible ),
		BoolName( postShader_.lastDirectFinalBound ),
		postShader_.lastDirectFinalRejectMask,
		postShader_.directFinalCandidates,
		postShader_.directFinalEligibleFrames,
		postShader_.directFinalAttempts,
		postShader_.directFinalBinds,
		postShader_.directFinalFallbacks,
		postShader_.directFinalRejects );
	RI().Printf( PRINT_ALL, "  static world GLx arena: %s, %.2f MB\n",
		BoolName( staticWorld_.arenaReady ),
		( staticWorld_.arenaVertexBytes + staticWorld_.arenaIndexBytes ) / ( 1024.0f * 1024.0f ) );
	RI().Printf( PRINT_ALL, "  static world GLx renderer: %s, arena draw %s\n",
		BoolName( staticWorld_.r_glxWorldRenderer && staticWorld_.r_glxWorldRenderer->integer ? qtrue : qfalse ),
		BoolName( ( staticWorld_.r_glxWorldRenderer && staticWorld_.r_glxWorldRenderer->integer ) ||
			( staticWorld_.r_glxStaticWorldArenaDraw && staticWorld_.r_glxStaticWorldArenaDraw->integer ) ? qtrue : qfalse ) );
	RI().Printf( PRINT_ALL, "  static world GLx draw: %s, soft %s, policy %s, %u/%u calls, packets %u full/%u partial/%u miss\n",
		BoolName( ( staticWorld_.r_glxWorldRenderer && staticWorld_.r_glxWorldRenderer->integer ) ||
			( staticWorld_.r_glxStaticWorldDraw && staticWorld_.r_glxStaticWorldDraw->integer ) ? qtrue : qfalse ),
		BoolName( ( staticWorld_.r_glxWorldRenderer && staticWorld_.r_glxWorldRenderer->integer ) ||
			( staticWorld_.r_glxStaticWorldSoftDraw && staticWorld_.r_glxStaticWorldSoftDraw->integer ) ? qtrue : qfalse ),
		staticWorld_.r_glxWorldRenderer && staticWorld_.r_glxWorldRenderer->integer ? "all" :
			staticWorld_.r_glxStaticWorldDrawPolicy && staticWorld_.r_glxStaticWorldDrawPolicy->string ?
			staticWorld_.r_glxStaticWorldDrawPolicy->string : "full",
		staticWorld_.drawCalls, staticWorld_.drawAttempts,
		staticWorld_.drawPacketFullHits, staticWorld_.drawPacketPartialHits, staticWorld_.drawPacketMisses );
	RI().Printf( PRINT_ALL, "  static world GLx packet lookup: %u mapped, hits %u, fallbacks %u\n",
		staticWorld_.itemLookupMappedItems, staticWorld_.itemLookupHits,
		staticWorld_.itemLookupFallbacks );
	RI().Printf( PRINT_ALL, "  static world GLx indirect packets: %u commands, %u bytes\n",
		staticWorld_.indirectPacketCount, staticWorld_.indirectPacketBytes );
	RI().Printf( PRINT_ALL, "  static world GLx indirect caps: draw %s, multidraw %s\n",
		BoolName( staticWorld_.drawIndirectAvailable ),
		BoolName( staticWorld_.multiDrawIndirectAvailable ) );
	RI().Printf( PRINT_ALL, "  static world GLx indirect buffer: %s, %u commands, %u bytes\n",
		BoolName( staticWorld_.indirectBufferReady ), staticWorld_.indirectBufferCommands,
		staticWorld_.indirectBufferBytes );
	RI().Printf( PRINT_ALL, "  static world GLx indirect draw: %s, %u/%u calls\n",
		BoolName( staticWorld_.r_glxStaticWorldIndirectDraw &&
			staticWorld_.r_glxStaticWorldIndirectDraw->integer ? qtrue : qfalse ),
		staticWorld_.indirectDrawCalls, staticWorld_.indirectDrawAttempts );
	RI().Printf( PRINT_ALL, "  static world GLx multidraw: %s, %u calls, %u runs\n",
		BoolName( ( staticWorld_.r_glxWorldRenderer && staticWorld_.r_glxWorldRenderer->integer ) ||
			( staticWorld_.r_glxStaticWorldMultiDraw && staticWorld_.r_glxStaticWorldMultiDraw->integer ) ? qtrue : qfalse ),
		staticWorld_.multiDrawCalls, staticWorld_.multiDrawRuns );
	RI().Printf( PRINT_ALL, "  static world GLx packet batch: %s, %u batches, %u packet runs, %u fallback runs\n",
		BoolName( staticWorld_.r_glxStaticWorldPacketBatch &&
			staticWorld_.r_glxStaticWorldPacketBatch->integer ? qtrue : qfalse ),
		staticWorld_.packetBatchBatches,
		staticWorld_.packetBatchRuns,
		staticWorld_.packetBatchFallbackRuns );
	RI().Printf( PRINT_ALL, "  static world GLx multidraw indirect: %s, %u/%u calls\n",
		BoolName( staticWorld_.r_glxStaticWorldMultiDrawIndirect &&
			staticWorld_.r_glxStaticWorldMultiDrawIndirect->integer ? qtrue : qfalse ),
		staticWorld_.multiDrawIndirectCalls, staticWorld_.multiDrawIndirectAttempts );
	RI().Printf( PRINT_ALL, "  static world GLx multidraw indirect shape: last %s %u runs/%u indexes, largest %u runs/%u indexes\n",
		GLX_Module_MdiSourceName( staticWorld_.multiDrawIndirectLastSource ),
		staticWorld_.multiDrawIndirectLastRuns,
		staticWorld_.multiDrawIndirectLastIndexes,
		staticWorld_.multiDrawIndirectLargestRun,
		staticWorld_.multiDrawIndirectLargestIndexes );
	RI().Printf( PRINT_ALL, "  static world GLx multidraw indirect sources: static %u, compact %s/%u calls, uploads %u, subdata %u, orphans %u\n",
		staticWorld_.multiDrawIndirectStaticCalls,
		BoolName( staticWorld_.r_glxStaticWorldMultiDrawIndirectCompact &&
			staticWorld_.r_glxStaticWorldMultiDrawIndirectCompact->integer ? qtrue : qfalse ),
		staticWorld_.multiDrawIndirectCompactCalls,
		staticWorld_.multiDrawIndirectCompactUploads,
		staticWorld_.multiDrawIndirectCompactSubDatas,
		staticWorld_.multiDrawIndirectCompactOrphans );
	RI().Printf( PRINT_ALL, "  static world GLx multidraw indirect last reject: %s, %u runs/%u indexes, command %i\n",
		GLX_StaticWorld_MdiRejectName( staticWorld_.multiDrawIndirectLastRejectReason ),
		staticWorld_.multiDrawIndirectLastRejectRuns,
		staticWorld_.multiDrawIndirectLastRejectIndexes,
		staticWorld_.multiDrawIndirectLastRejectCommand );
	RI().Printf( PRINT_ALL, "  static world GLx multidraw indirect spans: %s, batches %u, mdi runs %u, fallback runs %u, singles %u, single draws %u/%u indirect\n",
		BoolName( staticWorld_.r_glxStaticWorldMultiDrawIndirectSpans &&
			staticWorld_.r_glxStaticWorldMultiDrawIndirectSpans->integer ? qtrue : qfalse ),
		staticWorld_.multiDrawIndirectSpanBatches,
		staticWorld_.multiDrawIndirectSpanMdiRuns,
		staticWorld_.multiDrawIndirectSpanFallbackRuns,
		staticWorld_.multiDrawIndirectSpanSingles,
		staticWorld_.multiDrawIndirectSpanSingleDraws,
		staticWorld_.multiDrawIndirectSpanSingleIndirectDraws );
	RI().Printf( PRINT_ALL, "  static world GLx multidraw indirect span shape: last %u segments, %u mdi runs, %u fallback runs, %u singles, largest %u segments\n",
		staticWorld_.multiDrawIndirectSpanLastSegments,
		staticWorld_.multiDrawIndirectSpanLastMdiRuns,
		staticWorld_.multiDrawIndirectSpanLastFallbackRuns,
		staticWorld_.multiDrawIndirectSpanLastSingles,
		staticWorld_.multiDrawIndirectSpanLargestSegments );
	RI().Printf( PRINT_ALL, "  static world GLx multidraw indirect command spans: batches %u, mdi runs %u, fallback runs %u, singleton draws %u/%u indirect, singleton blocks %u\n",
		staticWorld_.multiDrawIndirectCommandSpanBatches,
		staticWorld_.multiDrawIndirectCommandSpanMdiRuns,
		staticWorld_.multiDrawIndirectCommandSpanFallbackRuns,
		staticWorld_.multiDrawIndirectCommandSpanSingletonDraws,
		staticWorld_.multiDrawIndirectCommandSpanSingletonIndirectDraws,
		staticWorld_.multiDrawIndirectCommandSpanSingletons );
	RI().Printf( PRINT_ALL, "  static world GLx multidraw indirect command span shape: last %u segments, %u mdi runs, %u fallback runs, largest %u segments\n",
		staticWorld_.multiDrawIndirectCommandSpanLastSegments,
		staticWorld_.multiDrawIndirectCommandSpanLastMdiRuns,
		staticWorld_.multiDrawIndirectCommandSpanLastFallbackRuns,
		staticWorld_.multiDrawIndirectCommandSpanLargestSegments );
	RI().Printf( PRINT_ALL, "  static world GLx multidraw indirect reasons: unsupported %u, nonmanifest %u, missing %u, noncontiguous %u\n",
		staticWorld_.multiDrawIndirectUnsupported, staticWorld_.multiDrawIndirectNonManifest,
		staticWorld_.multiDrawIndirectMissingCommand, staticWorld_.multiDrawIndirectNonContiguous );
	RI().Printf( PRINT_ALL, "  dynamic stream strategy: %s\n", GLX_Stream_StrategyName( stream_.strategy ) );
	RI().Printf( PRINT_ALL, "  dynamic stream ring: %i MB\n", stream_.ringMegabytes );
	RI().Printf( PRINT_ALL, "  dynamic stream buffer: %s\n", BoolName( stream_.ready ) );
	RI().Printf( PRINT_ALL, "  dynamic stream sync: %s, fences %u, waits %u, timeouts %u\n",
		BoolName( stream_.syncReady ), stream_.syncInsertions, stream_.syncWaits, stream_.syncTimeouts );
	RI().Printf( PRINT_ALL, "  dynamic stream uploads: %.2f MB, wraps %u, same-frame rejects %u\n",
		static_cast<double>( stream_.uploadBytes ) / ( 1024.0 * 1024.0 ),
		stream_.wraps, stream_.sameFrameWrapRejects );
	RI().Printf( PRINT_ALL, "  dynamic stream dlight uploads: attempts %u, draws %u, fallbacks %u, attempt %.2f MB, draw %.2f MB, wraps %u, waits %u, timeouts %u, sync failures %u\n",
		stream_.streamedDrawDynamicLightAttempts,
		stream_.streamedDrawDynamicLightDraws,
		stream_.streamedDrawDynamicLightFallbacks,
		static_cast<double>( stream_.streamedDrawDynamicLightAttemptBytes ) / ( 1024.0 * 1024.0 ),
		static_cast<double>( stream_.streamedDrawDynamicLightBytes ) / ( 1024.0 * 1024.0 ),
		stream_.streamedDrawDynamicLightReserveWraps,
		stream_.streamedDrawDynamicLightSyncWaits,
		stream_.streamedDrawDynamicLightSyncTimeouts,
		stream_.streamedDrawDynamicLightSyncFailures );
	RI().Printf( PRINT_ALL, "  dynamic stream tess shadow: %u batches, %.2f MB\n",
		stream_.shadowTessUploads,
		static_cast<double>( stream_.shadowTessBytes ) / ( 1024.0 * 1024.0 ) );
	RI().Printf( PRINT_ALL, "  dynamic stream draw enabled: %s\n", BoolName( GLX_Stream_DrawEnabled( stream_ ) ) );
	RI().Printf( PRINT_ALL, "  dynamic stream draw multitexture: %s\n",
		BoolName( GLX_Stream_DrawMultitextureEnabled( stream_ ) ) );
	RI().Printf( PRINT_ALL, "  dynamic stream draw fog: %s\n",
		BoolName( GLX_Stream_DrawFogEnabled( stream_ ) ) );
	RI().Printf( PRINT_ALL, "  dynamic stream draw depthFragment: %s\n",
		BoolName( GLX_Stream_DrawDepthFragmentEnabled( stream_ ) ) );
	RI().Printf( PRINT_ALL, "  dynamic stream draw texmods: %s\n",
		BoolName( GLX_Stream_DrawTexModsEnabled( stream_ ) ) );
	RI().Printf( PRINT_ALL, "  dynamic stream draw environment: %s\n",
		BoolName( GLX_Stream_DrawEnvironmentEnabled( stream_ ) ) );
	RI().Printf( PRINT_ALL, "  dynamic stream draw dynamic lights: %s\n",
		BoolName( GLX_Stream_DrawDynamicLightsEnabled( stream_ ) ) );
	RI().Printf( PRINT_ALL, "  dynamic stream draw screen maps: %s\n",
		BoolName( GLX_Stream_DrawScreenMapsEnabled( stream_ ) ) );
	RI().Printf( PRINT_ALL, "  dynamic stream draw video maps: %s\n",
		BoolName( GLX_Stream_DrawVideoMapsEnabled( stream_ ) ) );
	RI().Printf( PRINT_ALL, "  dynamic stream draw shadows: %s\n",
		BoolName( GLX_Stream_DrawShadowsEnabled( stream_ ) ) );
	RI().Printf( PRINT_ALL, "  dynamic stream draw beams: %s\n",
		BoolName( GLX_Stream_DrawBeamsEnabled( stream_ ) ) );
	RI().Printf( PRINT_ALL, "  dynamic stream draw postprocess: %s\n",
		BoolName( GLX_Stream_DrawPostProcessEnabled( stream_ ) ) );
	RI().Printf( PRINT_ALL, "  dynamic stream draws: %u/%u attempts, %.2f MB, index %.2f MB, tex1 %.2f MB, mt %u, fog %u, depthfrag %u, texmod %u, env %u, dlight %u, screen %u, video %u, shadow %u, beam %u, post %u\n",
		stream_.streamedDraws, stream_.streamedDrawAttempts,
		static_cast<double>( stream_.streamedDrawBytes ) / ( 1024.0 * 1024.0 ),
		static_cast<double>( stream_.streamedDrawIndexBytes ) / ( 1024.0 * 1024.0 ),
		static_cast<double>( stream_.streamedDrawTexcoord1Bytes ) / ( 1024.0 * 1024.0 ),
		stream_.streamedDrawMultitextureDraws,
		stream_.streamedDrawFogDraws,
		stream_.streamedDrawDepthFragmentDraws,
		stream_.streamedDrawTexModDraws,
		stream_.streamedDrawEnvironmentDraws,
		stream_.streamedDrawDynamicLightDraws,
		stream_.streamedDrawScreenMapDraws,
		stream_.streamedDrawVideoMapDraws,
		stream_.streamedDrawShadowDraws,
		stream_.streamedDrawBeamDraws,
		stream_.streamedDrawPostProcessDraws );
	RI().Printf( PRINT_ALL, "  dynamic stream categories: entity %u/%u, particle %u/%u, poly %u/%u, mark %u/%u, weapon %u/%u, ui %u/%u, beam %u/%u, dlight %u/%u, special %u/%u\n",
		stream_.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_ENTITY],
		stream_.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_ENTITY],
		stream_.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_PARTICLE],
		stream_.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_PARTICLE],
		stream_.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_POLY],
		stream_.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_POLY],
		stream_.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_MARK],
		stream_.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_MARK],
		stream_.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_WEAPON],
		stream_.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_WEAPON],
		stream_.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_UI],
		stream_.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_UI],
		stream_.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_BEAM],
		stream_.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_BEAM],
		stream_.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_DLIGHT],
		stream_.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_DLIGHT],
		stream_.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_SPECIAL],
		stream_.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_SPECIAL] );
	RI().Printf( PRINT_ALL, "  dynamic stream IR roles: generic %u/%u/%u, dlight %u/%u/%u, shadow %u/%u/%u, beam %u/%u/%u, post %u/%u/%u\n",
		stream_.streamedDrawRoleDraws[static_cast<int>( DynamicDrawRole::Generic )],
		stream_.streamedDrawRoleAttempts[static_cast<int>( DynamicDrawRole::Generic )],
		stream_.streamedDrawRoleFallbacks[static_cast<int>( DynamicDrawRole::Generic )],
		stream_.streamedDrawRoleDraws[static_cast<int>( DynamicDrawRole::DynamicLight )],
		stream_.streamedDrawRoleAttempts[static_cast<int>( DynamicDrawRole::DynamicLight )],
		stream_.streamedDrawRoleFallbacks[static_cast<int>( DynamicDrawRole::DynamicLight )],
		stream_.streamedDrawRoleDraws[static_cast<int>( DynamicDrawRole::Shadow )],
		stream_.streamedDrawRoleAttempts[static_cast<int>( DynamicDrawRole::Shadow )],
		stream_.streamedDrawRoleFallbacks[static_cast<int>( DynamicDrawRole::Shadow )],
		stream_.streamedDrawRoleDraws[static_cast<int>( DynamicDrawRole::Beam )],
		stream_.streamedDrawRoleAttempts[static_cast<int>( DynamicDrawRole::Beam )],
		stream_.streamedDrawRoleFallbacks[static_cast<int>( DynamicDrawRole::Beam )],
		stream_.streamedDrawRoleDraws[static_cast<int>( DynamicDrawRole::PostProcess )],
		stream_.streamedDrawRoleAttempts[static_cast<int>( DynamicDrawRole::PostProcess )],
		stream_.streamedDrawRoleFallbacks[static_cast<int>( DynamicDrawRole::PostProcess )] );
	RI().Printf( PRINT_ALL, "  dynamic stream draw material keys: accepted %u, rejected %u, mt accepted %u, mt rejected %u, depthfrag accepted %u, depthfrag rejected %u, texmod accepted %u, texmod rejected %u, env accepted %u, env rejected %u, dlight accepted %u, dlight rejected %u, screen accepted %u, screen rejected %u, video accepted %u, video rejected %u\n",
		stream_.streamedDrawMaterialAccepted, stream_.streamedDrawMaterialRejected,
		stream_.streamedDrawMultitextureAccepted, stream_.streamedDrawMultitextureRejected,
		stream_.streamedDrawDepthFragmentAccepted, stream_.streamedDrawDepthFragmentRejected,
		stream_.streamedDrawTexModAccepted, stream_.streamedDrawTexModRejected,
		stream_.streamedDrawEnvironmentAccepted, stream_.streamedDrawEnvironmentRejected,
		stream_.streamedDrawDynamicLightAccepted, stream_.streamedDrawDynamicLightRejected,
		stream_.streamedDrawScreenMapAccepted, stream_.streamedDrawScreenMapRejected,
		stream_.streamedDrawVideoMapAccepted, stream_.streamedDrawVideoMapRejected );
	RI().Printf( PRINT_ALL, "  dlight program compact: active %s, programs %i, availability %u/%u, binds %u/%u, failures %u, creates %u, cache hits %u\n",
		BoolName( GLX_Dlight_Active( dlight_ ) ),
		dlight_.programCount,
		dlight_.availabilityHits,
		dlight_.availabilityQueries,
		dlight_.binds,
		dlight_.bindAttempts,
		dlight_.bindFailures + dlight_.programCreateFailures,
		dlight_.programCreates,
		dlight_.programCacheHits );
	RI().Printf( PRINT_ALL, "  dlight state compact: legacy passes %u, texture binds %u, fog textures %u, shadow textures %u/%u, shadow fbo %u/%u, state changes %u\n",
		dlight_.legacyPasses,
		dlight_.textureBinds,
		dlight_.fogTextureBinds,
		dlight_.shadowTextureBinds,
		dlight_.shadowTextureFallbackBinds,
		dlight_.shadowFboBinds,
		dlight_.shadowFboRestores,
		dlight_.stateChanges );
	RI().Printf( PRINT_ALL, "  dlight build compact: legacy lights %u/%u, no-hit %u, verts %u, indexes %u/%u, pm %u, pm verts/indexes %u/%u\n",
		dlight_.buildLegacyLights,
		dlight_.buildLegacySkippedLights,
		dlight_.buildLegacyNoHitLights,
		dlight_.buildLegacyVertexes,
		dlight_.buildLegacyIndexes,
		dlight_.buildLegacyLitIndexes,
		dlight_.buildPmPasses,
		dlight_.buildPmVertexes,
		dlight_.buildPmIndexes );
	RI().Printf( PRINT_ALL, "  dlight cull compact: legacy verts %u, indexes %u\n",
		dlight_.cullLegacyVertexes,
		dlight_.cullLegacyIndexes );
	RI().Printf( PRINT_ALL, "  dlight scissor compact: active %s, candidates %u, computed %u, applied %u, fallbacks %u, pixels %llu/%llu\n",
		BoolName( GLX_Dlight_ScissorEnabled( dlight_ ) ),
		dlight_.scissorCandidates,
		dlight_.scissorComputed,
		dlight_.scissorApplied,
		dlight_.scissorFallbacks,
		dlight_.scissorPixels,
		dlight_.scissorViewportPixels );
	RI().Printf( PRINT_ALL, "  dlight projected lists compact: sources %i, surface %u/%u, packet active %u, lookup %u/%u, packet %u/%u, packet IR %u/%u rejected, dynamic %u/%u, dynamic IR %u/%u rejected, dropped 0x%08x/0x%08x\n",
		dlight_.projectedSourceRecordCount,
		dlight_.projectedListComplete,
		dlight_.projectedListIncomplete,
		dlight_.projectedPacketActivePackets,
		dlight_.projectedPacketItemLookupHits,
		dlight_.projectedPacketItemLookups,
		dlight_.projectedPacketListComplete,
		dlight_.projectedPacketListIncomplete,
		dlight_.projectedPacketIRProducts,
		dlight_.projectedPacketIRRejected,
		dlight_.projectedDynamicListComplete,
		dlight_.projectedDynamicListIncomplete,
		dlight_.projectedDynamicIRProducts,
		dlight_.projectedDynamicIRRejected,
		dlight_.projectedListDroppedMask,
		dlight_.projectedPacketDroppedMask );
	RI().Printf( PRINT_ALL, "  dlight projected shader compact: inputs %u/%u, world %u, dynamic %u, programmable %u, fallback %u, invalid %u\n",
		dlight_.projectedShaderInputs,
		dlight_.projectedShaderInputRecords,
		dlight_.projectedShaderWorldPackets,
		dlight_.projectedShaderDynamicDraws,
		dlight_.projectedShaderProgrammableInputs,
		dlight_.projectedShaderFallbackInputs,
		dlight_.projectedShaderInvalidInputs );
	RI().Printf( PRINT_ALL, "  dlight projected shader uniform compact: attempts %u, binds %u, failures %u, records %u, truncated %u, executable %u, suppressed %u, world %u/%u, dynamic %u/%u, limit-suppressed %u, limit %i\n",
		dlight_.projectedShaderUniformAttempts,
		dlight_.projectedShaderUniformBinds,
		dlight_.projectedShaderUniformFailures,
		dlight_.projectedShaderUniformRecords,
		dlight_.projectedShaderUniformTruncated,
		dlight_.projectedShaderUniformExecutableBinds,
		dlight_.projectedShaderUniformSuppressedBinds,
		dlight_.projectedShaderUniformWorldExecutableBinds,
		dlight_.projectedShaderUniformWorldBinds,
		dlight_.projectedShaderUniformDynamicExecutableBinds,
		dlight_.projectedShaderUniformDynamicBinds,
		dlight_.projectedShaderUniformLimitSuppressions,
		GLX_PROJECTED_DLIGHT_UNIFORM_LIMIT );
	RI().Printf( PRINT_ALL, "  dlight projected shader resource compact: attempts %u, binds %u, executable %u, suppressed %u, promotions %u, failures %u, records %u, world %u, dynamic %u, binding %u\n",
		dlight_.projectedShaderResourceAttempts,
		dlight_.projectedShaderResourceBinds,
		dlight_.projectedShaderResourceExecutableBinds,
		dlight_.projectedShaderResourceSuppressedBinds,
		dlight_.projectedShaderResourceLimitPromotions,
		dlight_.projectedShaderResourceFailures,
		dlight_.projectedShaderResourceRecords,
		dlight_.projectedShaderResourceWorldExecutableBinds,
		dlight_.projectedShaderResourceDynamicExecutableBinds,
		GLX_PROJECTED_DLIGHT_STREAM_BINDING );
	RI().Printf( PRINT_ALL, "  dlight projected shader stream compact: attempts %u, uploads %u, failures %u, skipped %u, records %u, bytes %llu, persistent %u, world %u, dynamic %u, range %u/%u, range failures %u, clears %u, last %u/%u\n",
		dlight_.projectedShaderStreamAttempts,
		dlight_.projectedShaderStreamUploads,
		dlight_.projectedShaderStreamFailures,
		dlight_.projectedShaderStreamSkips,
		dlight_.projectedShaderStreamRecords,
		dlight_.projectedShaderStreamBytes,
		dlight_.projectedShaderStreamPersistentUploads,
		dlight_.projectedShaderStreamWorldUploads,
		dlight_.projectedShaderStreamDynamicUploads,
		dlight_.projectedShaderStreamRangeBinds,
		dlight_.projectedShaderStreamRangeAttempts,
		dlight_.projectedShaderStreamRangeFailures,
		dlight_.projectedShaderStreamRangeClears,
		dlight_.projectedShaderStreamLastOffset,
		dlight_.projectedShaderStreamLastBytes );
	RI().Printf( PRINT_ALL, "  dlight projected shader arena compact: reserves %u, uploads %u, failures %u, wraps %u, rejects %u, waits %u, timeouts %u, sync failures %u, bytes %llu, light records %u, list records %u, world records %u, dynamic records %u, range %u/%u, range failures %u, clears %u, authoritative %u/%u, auth failures %u, auth fallbacks %u, auth clears %u, bound %s, cursor %u, last %u/%u/%u\n",
		dlight_.projectedShaderArenaReserveAttempts,
		dlight_.projectedShaderArenaUploads,
		dlight_.projectedShaderArenaFailures,
		dlight_.projectedShaderArenaWraps,
		dlight_.projectedShaderArenaSameFrameRejects,
		dlight_.projectedShaderArenaWaits,
		dlight_.projectedShaderArenaTimeouts,
		dlight_.projectedShaderArenaSyncFailures,
		dlight_.projectedShaderArenaBytes,
		dlight_.projectedShaderArenaLightRecordUploads,
		dlight_.projectedShaderArenaListRecordUploads,
		dlight_.projectedShaderArenaWorldRecords,
		dlight_.projectedShaderArenaDynamicRecords,
		dlight_.projectedShaderArenaRangeBinds,
		dlight_.projectedShaderArenaRangeAttempts,
		dlight_.projectedShaderArenaRangeFailures,
		dlight_.projectedShaderArenaRangeClears,
		dlight_.projectedShaderArenaAuthoritativeBinds,
		dlight_.projectedShaderArenaAuthoritativeAttempts,
		dlight_.projectedShaderArenaAuthoritativeFailures,
		dlight_.projectedShaderArenaAuthoritativeFallbacks,
		dlight_.projectedShaderArenaAuthoritativeClears,
		BoolName( dlight_.projectedShaderArenaRangeBound ),
		dlight_.projectedShaderArenaFrameBytes,
		dlight_.projectedShaderArenaLastBuffer,
		dlight_.projectedShaderArenaLastOffset,
		dlight_.projectedShaderArenaLastBytes );
	RI().Printf( PRINT_ALL, "  dlight projected shader MDI compact: attempts %u, eligible %u, uploads %u, failures %u, skipped %u, records %u, indexes %u, bytes %llu, last %u/%u\n",
		dlight_.projectedShaderMdiAttempts,
		dlight_.projectedShaderMdiEligible,
		dlight_.projectedShaderMdiCommandUploads,
		dlight_.projectedShaderMdiCommandFailures,
		dlight_.projectedShaderMdiCommandSkips,
		dlight_.projectedShaderMdiRecords,
		dlight_.projectedShaderMdiIndexes,
		dlight_.projectedShaderMdiCommandBytes,
		dlight_.projectedShaderMdiLastOffset,
		dlight_.projectedShaderMdiLastBytes );
	RI().Printf( PRINT_ALL, "  dlight projected shader MDI command ring compact: reserves %u, commits %u, wraps %u, failures %u, slots %u, cursor %u, last %u/%u/%u/%u\n",
		dlight_.projectedShaderMdiCommandRingReserves,
		dlight_.projectedShaderMdiCommandRingCommits,
		dlight_.projectedShaderMdiCommandRingWraps,
		dlight_.projectedShaderMdiCommandRingFailures,
		GLX_PROJECTED_DLIGHT_MDI_COMMAND_RING_LIMIT,
		dlight_.projectedShaderMdiCommandRingCursor,
		dlight_.projectedShaderMdiCommandRingLastSlot,
		dlight_.projectedShaderMdiCommandRingLastBuffer,
		dlight_.projectedShaderMdiCommandRingLastOffset,
		dlight_.projectedShaderMdiCommandRingLastBytes );
	RI().Printf( PRINT_ALL, "  dlight projected shader MDI submit compact: attempts %u, plans %u, ready %u, fallbacks %u, skipped %u, records %u, indexes %u, buffer %u, last %u/%u\n",
		dlight_.projectedShaderMdiSubmitAttempts,
		dlight_.projectedShaderMdiSubmitPlans,
		dlight_.projectedShaderMdiSubmitReady,
		dlight_.projectedShaderMdiSubmitFallbacks,
		dlight_.projectedShaderMdiSubmitSkips,
		dlight_.projectedShaderMdiSubmitRecords,
		dlight_.projectedShaderMdiSubmitIndexes,
		dlight_.projectedShaderMdiSubmitLastBuffer,
		dlight_.projectedShaderMdiSubmitLastOffset,
		dlight_.projectedShaderMdiSubmitLastBytes );
	RI().Printf( PRINT_ALL, "  dlight projected shader MDI batch compact: attempts %u, batches %u, ready %u, fallbacks %u, rejects %u, gl errors %u, records %u, indexes %u, submitted %u/%u, largest %u, reject %u, buffer %u, range %u/%u\n",
		dlight_.projectedShaderMdiBatchAttempts,
		dlight_.projectedShaderMdiBatchBatches,
		dlight_.projectedShaderMdiBatchReady,
		dlight_.projectedShaderMdiBatchFallbacks,
		dlight_.projectedShaderMdiBatchRejects,
		dlight_.projectedShaderMdiBatchGlErrors,
		dlight_.projectedShaderMdiBatchRecords,
		dlight_.projectedShaderMdiBatchIndexes,
		dlight_.projectedShaderMdiBatchSubmittedDraws,
		dlight_.projectedShaderMdiBatchSubmittedIndexes,
		dlight_.projectedShaderMdiBatchLargest,
		dlight_.projectedShaderMdiBatchLastReject,
		dlight_.projectedShaderMdiBatchLastBuffer,
		dlight_.projectedShaderMdiBatchLastOffset,
		dlight_.projectedShaderMdiBatchLastBytes );
}

void RendererModule::PrintInfo() const
{
	PrintCaps();
	GLX_Material_PrintInfo( material_ );
	GLX_Dlight_PrintInfo( dlight_ );
	GLX_PostShader_PrintInfo( postShader_ );
	GLX_PostProcess_PrintInfo( postprocess_ );
	GLX_Executor_PrintInfo( executor_ );
	GLX_Profiler_PrintInfo( profiler_ );
	GLX_StaticWorld_PrintInfo( staticWorld_ );
	GLX_Stream_PrintInfo( stream_ );
}

void RendererModule::PrintProfile() const
{
	char current[64];

	RI().Printf( PRINT_ALL, "glxprofile: active %s, startup %s\n",
		GLX_Module_DetectedProfileName(),
		profile_ && profile_->string && profile_->string[0] ? profile_->string : "manual" );

	for ( unsigned int i = 0; i < sizeof( GLX_PROFILE_CVARS ) / sizeof( GLX_PROFILE_CVARS[0] ); i++ ) {
		const ProfileCvarSetting &setting = GLX_PROFILE_CVARS[i];

		GLX_Module_CurrentCvarValue( setting.name, current, sizeof( current ) );
		RI().Printf( PRINT_ALL, "  %-42s %s\n", setting.name, current[0] ? current : "<unset>" );
	}
}

void RendererModule::ProfileCommand()
{
	GlxProfile profile;

	if ( !RI().Cmd_Argc || RI().Cmd_Argc() < 2 ) {
		PrintProfile();
		GLX_Module_PrintProfileUsage();
		return;
	}

	const char *arg = RI().Cmd_Argv( 1 );

	if ( !GLX_Module_Stricmp( arg, "status" ) ||
		!GLX_Module_Stricmp( arg, "current" ) ) {
		PrintProfile();
		return;
	}

	if ( !GLX_Module_Stricmp( arg, "list" ) ||
		!GLX_Module_Stricmp( arg, "help" ) ) {
		GLX_Module_PrintProfileUsage();
		return;
	}

	if ( !GLX_Module_Stricmp( arg, "manual" ) ||
		!GLX_Module_Stricmp( arg, "custom" ) ||
		!GLX_Module_Stricmp( arg, "clear" ) ) {
		if ( profile_ ) {
			RI().Cvar_Set( profile_->name, "" );
		}
		RI().Printf( PRINT_ALL, "glxprofile: startup profile cleared; current cvars unchanged\n" );
		return;
	}

	if ( !GLX_Module_ParseProfile( arg, &profile ) ) {
		RI().Printf( PRINT_WARNING, "glxprofile: unknown profile '%s'\n", arg ? arg : "" );
		GLX_Module_PrintProfileUsage();
		return;
	}

	ApplyProfile( profile, qtrue, qfalse );
}

void RendererModule::PrintMaterial() const
{
	GLX_Material_PrintInfo( material_ );
}

void RendererModule::PrintPostProcess() const
{
	GLX_PostProcess_PrintInfo( postprocess_ );
}

void RendererModule::PrintStaticWorld() const
{
	int limit = 16;
	qboolean hot = qfalse;
	qboolean commands = qfalse;
	qboolean spans = qfalse;

	if ( RI().Cmd_Argc && RI().Cmd_Argc() > 1 ) {
		const char *arg = RI().Cmd_Argv( 1 );

		if ( arg && ( !GLX_Module_Stricmp( arg, "hot" ) || !GLX_Module_Stricmp( arg, "top" ) ) ) {
			hot = qtrue;
			if ( RI().Cmd_Argc() > 2 ) {
				limit = std::atoi( RI().Cmd_Argv( 2 ) );
			}
		} else if ( arg && ( !GLX_Module_Stricmp( arg, "commands" ) || !GLX_Module_Stricmp( arg, "cmds" ) ) ) {
			commands = qtrue;
			if ( RI().Cmd_Argc() > 2 ) {
				limit = std::atoi( RI().Cmd_Argv( 2 ) );
			}
		} else if ( arg && ( !GLX_Module_Stricmp( arg, "spans" ) || !GLX_Module_Stricmp( arg, "mdi" ) ) ) {
			spans = qtrue;
			if ( RI().Cmd_Argc() > 2 ) {
				limit = std::atoi( RI().Cmd_Argv( 2 ) );
			}
		} else {
			limit = std::atoi( arg );
		}
	}
	if ( limit < 0 ) {
		limit = 0;
	}
	if ( limit > 128 ) {
		limit = 128;
	}

	if ( hot ) {
		GLX_StaticWorld_PrintHotPackets( staticWorld_, limit );
	} else if ( commands ) {
		GLX_StaticWorld_PrintIndirectCommands( staticWorld_, limit );
	} else if ( spans ) {
		GLX_StaticWorld_PrintSpanDiagnostics( staticWorld_, limit );
	} else {
		GLX_StaticWorld_PrintPackets( staticWorld_, limit );
	}
}

void RendererModule::PrintFrameCounters() const
{
	if ( !caps_.config ) {
		RI().Printf( PRINT_ALL, "glx: no OpenGL context\n" );
		return;
	}

	const int genericRole = static_cast<int>( DynamicDrawRole::Generic );
	const int dlightRole = static_cast<int>( DynamicDrawRole::DynamicLight );
	const int shadowRole = static_cast<int>( DynamicDrawRole::Shadow );
	const int beamRole = static_cast<int>( DynamicDrawRole::Beam );
	const int postRole = static_cast<int>( DynamicDrawRole::PostProcess );
	const int dlightPass = static_cast<int>( FramePassKind::DynamicLights );
	const int scenePass = static_cast<int>( FramePassKind::DynamicScene );
	const int postPass = static_cast<int>( FramePassKind::PostProcess );
	const unsigned int focusedPassDraws = executor_.dynamicDrawPassDraws[dlightPass] +
		executor_.dynamicDrawPassDraws[scenePass] + executor_.dynamicDrawPassDraws[postPass];
	const unsigned int focusedPassIndexes = executor_.dynamicDrawPassIndexes[dlightPass] +
		executor_.dynamicDrawPassIndexes[scenePass] + executor_.dynamicDrawPassIndexes[postPass];
	const unsigned int focusedPassVertices = executor_.dynamicDrawPassVertices[dlightPass] +
		executor_.dynamicDrawPassVertices[scenePass] + executor_.dynamicDrawPassVertices[postPass];
	const unsigned int otherPassDraws = executor_.dynamicDraws > focusedPassDraws ?
		executor_.dynamicDraws - focusedPassDraws : 0u;
	const unsigned int otherPassIndexes = executor_.dynamicIndexes > focusedPassIndexes ?
		executor_.dynamicIndexes - focusedPassIndexes : 0u;
	const unsigned int otherPassVertices = executor_.dynamicVertices > focusedPassVertices ?
		executor_.dynamicVertices - focusedPassVertices : 0u;

	RI().Printf( PRINT_ALL, "glx: tier %s, batches %u, draws %u/%u idx, stream %s/%s %.2fMB/%uwraps/%urejects shadow %u, frames %u, backend queries %u, gpu %s, static %i batches/%i packets/%i surfaces/%i verts/%i indexes %.2f MB, arena %s %.2f MB\n",
		GLX_Caps_TierName( caps_.tier ),
		profiler_.shaderBatches,
		profiler_.drawCalls,
		profiler_.drawIndexes,
		GLX_Stream_StrategyName( stream_.strategy ),
		stream_.ready ? "ready" : "off",
		static_cast<double>( stream_.uploadBytes ) / ( 1024.0 * 1024.0 ),
		stream_.wraps,
		stream_.sameFrameWrapRejects,
		stream_.shadowTessUploads,
		profiler_.frames,
		profiler_.backendQueries,
		GLX_Profiler_LastGpuTimeText( profiler_ ),
		staticWorld_.batches,
		staticWorld_.packetCount,
		staticWorld_.surfaces,
		staticWorld_.vertexes,
		staticWorld_.indexes,
		GLX_StaticWorld_TotalMegabytes( staticWorld_ ),
		staticWorld_.arenaReady ? "ready" : "off",
		( staticWorld_.arenaVertexBytes + staticWorld_.arenaIndexBytes ) / ( 1024.0f * 1024.0f ) );
	RI().Printf( PRINT_ALL, "glx: pass counters blits %u, binds %u, clears %u, fullscreen %u, pass queries %u, unavailable %u, ring skips %u\n",
		profiler_.postBlits,
		profiler_.postBinds,
		profiler_.postClears,
		profiler_.postFullscreenPasses,
		profiler_.gpuPassQueries,
		profiler_.passQueryUnavailableFrames,
		profiler_.passQueryRingFullSkips );
	RI().Printf( PRINT_ALL, "glx: pass gpu:" );
	for ( int i = 0; i < GLX_GPU_PASS_COUNT; i++ ) {
		const GpuPassStats &stats = profiler_.gpuPassStats[i];
		RI().Printf( PRINT_ALL, " %s=%s/%u",
			GLX_Profiler_GpuPassName( i ),
			stats.samples ? stats.lastText : "n/a",
			stats.samples );
	}
	RI().Printf( PRINT_ALL, "\n" );
	RI().Printf( PRINT_ALL, "glx: material stages %u generic/%u vbo/%u mt/%u blend/%u texmod/%u env/%u\n",
		profiler_.materialStages,
		profiler_.genericMaterialStages,
		profiler_.vboMaterialStages,
		profiler_.multitextureMaterialStages,
		profiler_.blendMaterialStages,
		profiler_.texmodMaterialStages,
		profiler_.environmentMaterialStages );
	RI().Printf( PRINT_ALL, "glx: render IR executor %s/%s passes %u world %u projected %u/%u dynamic %u projected dynamic %u/%u draws/%u idx/%u verts materials %u uploads %u post %u outputs %u rejects %u\n",
		GLX_Executor_TierName( executor_ ),
		GLX_Executor_ModeName( executor_ ),
		executor_.framePasses,
		executor_.worldPackets,
		executor_.worldPacketsWithProjectedDlights,
		executor_.worldPacketProjectedDlightRecords,
		executor_.dynamicDraws,
		executor_.dynamicDrawsWithProjectedDlights,
		executor_.dynamicDrawProjectedDlightRecords,
		executor_.dynamicIndexes,
		executor_.dynamicVertices,
		executor_.materialPlans,
		executor_.uploadPlans,
		executor_.postNodes,
		executor_.outputTransforms,
		executor_.rejectedProducts );
	RI().Printf( PRINT_ALL, "glx: render IR dynamic roles generic %u/%u/%u, dlight %u/%u/%u, shadow %u/%u/%u, beam %u/%u/%u, post %u/%u/%u\n",
		executor_.dynamicDrawRoleDraws[genericRole],
		executor_.dynamicDrawRoleIndexes[genericRole],
		executor_.dynamicDrawRoleVertices[genericRole],
		executor_.dynamicDrawRoleDraws[dlightRole],
		executor_.dynamicDrawRoleIndexes[dlightRole],
		executor_.dynamicDrawRoleVertices[dlightRole],
		executor_.dynamicDrawRoleDraws[shadowRole],
		executor_.dynamicDrawRoleIndexes[shadowRole],
		executor_.dynamicDrawRoleVertices[shadowRole],
		executor_.dynamicDrawRoleDraws[beamRole],
		executor_.dynamicDrawRoleIndexes[beamRole],
		executor_.dynamicDrawRoleVertices[beamRole],
		executor_.dynamicDrawRoleDraws[postRole],
		executor_.dynamicDrawRoleIndexes[postRole],
		executor_.dynamicDrawRoleVertices[postRole] );
	RI().Printf( PRINT_ALL, "glx: render IR dynamic passes dlight %u/%u/%u, scene %u/%u/%u, post %u/%u/%u, other %u/%u/%u\n",
		executor_.dynamicDrawPassDraws[dlightPass],
		executor_.dynamicDrawPassIndexes[dlightPass],
		executor_.dynamicDrawPassVertices[dlightPass],
		executor_.dynamicDrawPassDraws[scenePass],
		executor_.dynamicDrawPassIndexes[scenePass],
		executor_.dynamicDrawPassVertices[scenePass],
		executor_.dynamicDrawPassDraws[postPass],
		executor_.dynamicDrawPassIndexes[postPass],
		executor_.dynamicDrawPassVertices[postPass],
		otherPassDraws,
		otherPassIndexes,
		otherPassVertices );
	RI().Printf( PRINT_ALL, "glx: post/output ownership mode %s, post nodes %u, outputs %u, legacy fallback %s, executable nodes %u, executable outputs %u, post hash 0x%08x, output hash 0x%08x, plan hash 0x%08x, fallback 0x%08x\n",
		GLX_PostProcess_PostOutputModeName( postprocess_.lastPostOutputGlxOwned ),
		executor_.postNodes,
		executor_.outputTransforms,
		BoolName( postprocess_.lastPostOutputGlxOwned ? qfalse : qtrue ),
		postprocess_.lastPostOutputExecutableNodeCount,
		postprocess_.lastPostOutputExecutableOutputCount,
		executor_.lastPostNodeHash,
		executor_.lastOutputTransformHash,
		postprocess_.lastPostOutputPlanHash,
		postprocess_.lastPostOutputFallbackReasons );
	RI().Printf( PRINT_ALL, "glx: post shader plan valid %s, features 0x%08x, hash 0x%08x, textures %u, uniforms %u, frames %u, invalid %u\n",
		BoolName( postprocess_.lastPostShaderPlanValid ),
		postprocess_.lastPostShaderFeatureMask,
		postprocess_.lastPostShaderPlanHash,
		postprocess_.lastPostShaderTextureCount,
		postprocess_.lastPostShaderUniformVec4Count,
		postprocess_.postShaderPlanFrames,
		postprocess_.postShaderPlanInvalidFrames );
	RI().Printf( PRINT_ALL, "glx: post shader cache ready %s, programs %i/%i, plans %u valid/%u invalid, cache %u hits/%u misses, compile %u attempts/%u failures, link failures %u, source failures %u, source hash 0x%08x, program %u, target %s, preferred %s, evictions %u, target fallbacks %u\n",
		BoolName( GLX_PostShader_Ready( postShader_ ) ),
		postShader_.programCount,
		GLX_POST_SHADER_PROGRAM_LIMIT,
		postShader_.validPlansObserved,
		postShader_.invalidPlansObserved,
		postShader_.cacheHits,
		postShader_.cacheMisses,
		postShader_.compileAttempts,
		postShader_.compileFailures,
		postShader_.linkFailures,
		postShader_.sourceFailures,
		postShader_.lastSourceHash,
		postShader_.lastProgram,
		GLX_PostShaderSource_TargetName( postShader_.activeTarget ),
		GLX_PostShaderSource_TargetName( postShader_.preferredTarget ),
		postShader_.cacheEvictions,
		postShader_.targetFallbacks );
	RI().Printf( PRINT_ALL, "glx: post shader direct-final execute %s, eligible %s, bound %s, reject 0x%08x, candidates %u, eligible frames %u, attempts %u, binds %u, fallbacks %u, rejects %u, program misses %u, uniform failures %u\n",
		BoolName( postShader_.r_glxPostShaderExecute && postShader_.r_glxPostShaderExecute->integer ? qtrue : qfalse ),
		BoolName( postShader_.lastDirectFinalEligible ),
		BoolName( postShader_.lastDirectFinalBound ),
		postShader_.lastDirectFinalRejectMask,
		postShader_.directFinalCandidates,
		postShader_.directFinalEligibleFrames,
		postShader_.directFinalAttempts,
		postShader_.directFinalBinds,
		postShader_.directFinalFallbacks,
		postShader_.directFinalRejects,
		postShader_.directFinalProgramMisses,
		postShader_.directFinalUniformFailures );
	if ( caps_.tier == RenderProductTier::GL12 ) {
		RI().Printf( PRINT_ALL, "glx: GL12 fixed-function draws %u client-memory %u unsupported stream %u post %u output %u\n",
			executor_.fixedFunctionDraws,
			executor_.clientMemoryDraws,
			executor_.unsupportedStreamUploads,
			executor_.unsupportedPostNodes,
			executor_.unsupportedOutputTransforms );
	}
	if ( caps_.tier == RenderProductTier::GL2X ) {
		RI().Printf( PRINT_ALL, "glx: GL2X programmable draws %u stream %u materials %u post-lite %u unsupported advanced-upload %u post %u output %u\n",
			executor_.programmableDraws,
			executor_.streamUploadDraws,
			executor_.programmableMaterialPlans,
			executor_.postprocessLiteNodes,
			executor_.unsupportedAdvancedUploads,
			executor_.unsupportedPostNodes,
			executor_.unsupportedOutputTransforms );
	}
	if ( caps_.tier == RenderProductTier::GL3X ) {
		RI().Printf( PRINT_ALL, "glx: GL3X performance draws %u sync-uploads %u static-buffers %u dynamic-buffers %u materials %u fbo-post %u unsupported persistent-upload %u\n",
			executor_.performanceDraws,
			executor_.syncUploadPlans,
			executor_.staticBufferProducts,
			executor_.dynamicBufferProducts,
			executor_.performanceMaterialPlans,
			executor_.fboPostNodes,
			executor_.unsupportedPersistentUploads );
	}
	if ( caps_.tier == RenderProductTier::GL41 ) {
		RI().Printf( PRINT_ALL, "glx: GL41 mac-modern draws %u sync-uploads %u static-buffers %u dynamic-buffers %u materials %u post %u unsupported persistent-upload %u gl43-required 0 gl44-required 0 gl45-required 0\n",
			executor_.macModernDraws,
			executor_.macModernSyncUploadPlans,
			executor_.macModernStaticBufferProducts,
			executor_.macModernDynamicBufferProducts,
			executor_.macModernMaterialPlans,
			executor_.macModernPostNodes,
			executor_.unsupportedPersistentUploads );
	}
	if ( caps_.tier == RenderProductTier::GL46 ) {
		RI().Printf( PRINT_ALL, "glx: GL46 high-end draws %u persistent-uploads %u sync-uploads %u dsa-products %u mdi-products %u aggressive-static %u materials %u post %u gpu-counters %u static-mdi %u/%u calls/%u idx\n",
			executor_.highEndDraws,
			executor_.highEndPersistentUploads,
			executor_.highEndSyncUploads,
			executor_.highEndDsaProducts,
			executor_.highEndMdiProducts,
			executor_.highEndAggressiveStaticProducts,
			executor_.highEndMaterialPlans,
			executor_.highEndPostNodes,
			profiler_.backendQueries,
			staticWorld_.multiDrawIndirectCalls,
			staticWorld_.multiDrawIndirectAttempts,
			staticWorld_.multiDrawIndirectIndexes );
	}
	RI().Printf( PRINT_ALL, "glx: pass schedule %s %i/%08x %s\n",
		executor_.frameScheduleValid ? "valid" : "invalid",
		executor_.frameScheduleCount,
		executor_.frameScheduleHash,
		executor_.frameScheduleText[0] ? executor_.frameScheduleText : "none" );
	RI().Printf( PRINT_ALL, "glx: ownership legacy delegation %u calls/%u items, generic %u, vbo-device %u, vbo-soft %u, arrays %u\n",
		profiler_.legacyDelegationCalls,
		profiler_.legacyDelegationItems,
		profiler_.legacyDelegationReasonCalls[GLX_LEGACY_DELEGATION_GENERIC],
		profiler_.legacyDelegationReasonCalls[GLX_LEGACY_DELEGATION_VBO_DEVICE],
		profiler_.legacyDelegationReasonCalls[GLX_LEGACY_DELEGATION_VBO_SOFT],
		profiler_.legacyDelegationReasonCalls[GLX_LEGACY_DELEGATION_DRAW_ARRAY] );
	RI().Printf( PRINT_ALL, "glx: material renderer %s/%s programs %i, binds %u/%u attempts, switches %u, cache %u/%u, failures %u compile/%u link/%u precache/%u bind, labels %u\n",
		material_.r_glxMaterialRenderer && material_.r_glxMaterialRenderer->integer ? "on" : "off",
		material_.ready ? "ready" : "not-ready",
		material_.programCount,
		material_.binds,
		material_.bindAttempts,
		material_.programSwitches,
		material_.cacheHits,
		material_.cacheMisses,
		material_.compileFailures,
		material_.linkFailures,
		material_.precacheFailures,
		material_.bindFailures,
		material_.debugLabels );
	RI().Printf( PRINT_ALL, "glx: material parameters blocks %u invalid %u hash 0x%08x, last sort %i passes %i features 0x%x flags 0x%x state 0x%x\n",
		material_.parameterBlocks,
		material_.invalidParameterBlocks,
		material_.lastParameterBlockHash,
		material_.lastParameterBlock.frame.sort,
		material_.lastParameterBlock.frame.shaderStagePasses,
		material_.lastParameterBlock.frame.featureMask,
		material_.lastParameterBlock.material.flags,
		material_.lastParameterBlock.material.stateBits );
	RI().Printf( PRINT_ALL, "glx: postprocess fbo %s %ix%i capture %ix%i bloom %i, frames %u final %u prefinal %u gamma %u/%u, copies %u, msaa %u, ssaa %u, last %s\n",
		postprocess_.fboReady ? "ready" : "off",
		postprocess_.vidWidth,
		postprocess_.vidHeight,
		postprocess_.captureWidth,
		postprocess_.captureHeight,
		postprocess_.bloomMode,
		postprocess_.frames,
		postprocess_.bloomFinalPasses,
		postprocess_.bloomIntermediatePasses,
		postprocess_.gammaDirectFrames,
		postprocess_.gammaBlitFrames,
		postprocess_.copyScreenCalls,
		postprocess_.msaaBlits,
		postprocess_.ssaaBlits,
		GLX_PostProcess_ResultName( postprocess_.lastResult ) );
	RI().Printf( PRINT_ALL, "glx: bloom storage policy %s format 0x%04x (0x%04x:0x%04x)\n",
		GLX_PostProcess_BloomFormatModeName( postprocess_.lastBloomFormatMode ),
		postprocess_.lastBloomInternalFormat,
		postprocess_.lastBloomTextureFormat,
		postprocess_.lastBloomTextureType );
	RI().Printf( PRINT_ALL, "glx: color pipeline %s precision %i transfer %s tone-map %s exposure %.2f bloom-threshold %.2f/%i knee %.2f grade %s paper-white %.0f max %.0f\n",
		GLX_RenderIR_SceneColorSpaceName( postprocess_.lastOutput.sceneColorSpace ),
		postprocess_.lastOutput.precisionMode,
		GLX_RenderIR_OutputTransferName( postprocess_.lastOutput.transfer ),
		GLX_RenderIR_ToneMapName( postprocess_.lastOutput.toneMap ),
		postprocess_.lastOutput.exposure,
		postprocess_.lastOutput.bloomThreshold,
		postprocess_.bloomThresholdMode,
		postprocess_.lastOutput.bloomSoftKnee,
		GLX_RenderIR_ColorGradeName( postprocess_.lastOutput.grade ),
		postprocess_.lastOutput.paperWhiteNits,
		postprocess_.lastOutput.maxOutputNits );
	RI().Printf( PRINT_ALL, "glx: auto exposure mode %i algorithm %s enabled %s fallback %s samples %i/%ix%i percentile %.1f target-luma %.3f measured-log2 %.3f measured-luma %.4f manual %.2f scale %.3f target %.2f frames %u histogram %u simple %u sample-failures %u\n",
		postprocess_.lastAutoExposureMode,
		GLX_RenderIR_ExposureReductionName( postprocess_.lastExposureAlgorithm ),
		BoolName( postprocess_.lastAutoExposureEnabled ),
		BoolName( postprocess_.lastAutoExposureFallback ),
		postprocess_.lastAutoExposureSampleCount,
		postprocess_.lastAutoExposureSampleWidth,
		postprocess_.lastAutoExposureSampleHeight,
		postprocess_.lastAutoExposurePercentile,
		postprocess_.lastAutoExposureTargetLuma,
		postprocess_.lastAutoExposureLogLuma,
		postprocess_.lastAutoExposureLuma,
		postprocess_.lastManualExposure,
		postprocess_.lastAutoExposureScale,
		postprocess_.lastAutoExposureTargetExposure,
		postprocess_.autoExposureFrames,
		postprocess_.autoExposureHistogramFrames,
		postprocess_.autoExposureSimpleFrames,
		postprocess_.autoExposureSampleFailures );
	RI().Printf( PRINT_ALL, "glx: output colorimetry primaries %s gamut-map %s precision-request %i precision-resolved %i\n",
		GLX_RenderIR_OutputPrimariesName( postprocess_.lastOutput.outputPrimaries ),
		GLX_RenderIR_GamutMapName( postprocess_.lastOutput.gamutMap ),
		postprocess_.lastOutput.requestedPrecisionMode,
		postprocess_.lastOutput.precisionMode );
	RI().Printf( PRINT_ALL, "glx: output backend request %s selected %s native %s hardware %s experimental %s display-hdr %s headroom %.2f sdr-white %.0f display-max %.0f icc %s/%i\n",
		RendererOutputRequestName( postprocess_.lastOutput.requestedBackend ),
		RendererOutputBackendName( postprocess_.lastOutput.selectedBackend ),
		RendererOutputBackendName( postprocess_.lastOutput.nativeBackend ),
		BoolName( postprocess_.lastOutput.outputHardwareActive ),
		BoolName( postprocess_.lastOutput.outputExperimental ),
		BoolName( postprocess_.lastOutput.displayHdrEnabled ),
		postprocess_.lastOutput.displayHdrHeadroom,
		postprocess_.lastOutput.displaySdrWhiteNits,
		postprocess_.lastOutput.displayMaxNits,
		BoolName( postprocess_.lastOutput.displayIccProfileAvailable ),
		postprocess_.lastOutput.displayIccProfileBytes );
	RI().Printf( PRINT_ALL, "glx: display state queries %u changes %u capability %u backend %u hdr %u headroom %u luminance %u icc %u last-frame %u flags 0x%08x hash 0x%08x previous 0x%08x\n",
		postprocess_.displayOutputQueries,
		postprocess_.displayOutputStateChanges,
		postprocess_.displayOutputCapabilityChanges,
		postprocess_.displayOutputBackendChanges,
		postprocess_.displayOutputHdrChanges,
		postprocess_.displayOutputHeadroomChanges,
		postprocess_.displayOutputLuminanceChanges,
		postprocess_.displayOutputIccChanges,
		postprocess_.lastDisplayOutputChangeFrame,
		postprocess_.lastDisplayOutputChangeMask,
		postprocess_.lastDisplayOutputHash,
		postprocess_.previousDisplayOutputHash );
	RI().Printf( PRINT_ALL, "glx: color grade mode %s lift %.2f/%.2f/%.2f gamma %.2f/%.2f/%.2f gain %.2f/%.2f/%.2f white-point %.0f->%.0f lut-size %.0f lut-scale %.2f\n",
		GLX_RenderIR_ColorGradeName( postprocess_.lastOutput.grade ),
		postprocess_.lastGradeLift[0], postprocess_.lastGradeLift[1], postprocess_.lastGradeLift[2],
		postprocess_.lastGradeGamma[0], postprocess_.lastGradeGamma[1], postprocess_.lastGradeGamma[2],
		postprocess_.lastGradeGain[0], postprocess_.lastGradeGain[1], postprocess_.lastGradeGain[2],
		postprocess_.lastWhitePointSourceKelvin, postprocess_.lastWhitePointTargetKelvin,
		postprocess_.lastColorGradeLutSize, postprocess_.lastColorGradeLutScale );
	RI().Printf( PRINT_ALL, "glx: color audit srgb-decode %s requested %s available %s framebuffer-srgb %s requested %s available %s capture %s capture-request %s capture-hdr-aware %s capture-supported %s target-float %s final-encode %s contract %s texture-consistent %s stale-srgb-decode %u\n",
		BoolName( postprocess_.textureSrgbDecode ),
		BoolName( postprocess_.textureSrgbDecodeDesired ),
		BoolName( postprocess_.textureSrgbAvailable ),
		BoolName( postprocess_.framebufferSrgbEnabled ),
		BoolName( postprocess_.r_framebufferSRGB && postprocess_.r_framebufferSRGB->integer ? qtrue : qfalse ),
		BoolName( postprocess_.framebufferSrgbAvailable ),
		GLX_RenderIR_CaptureExportPolicyName( postprocess_.lastCaptureSelected ),
		GLX_RenderIR_CaptureExportPolicyName( postprocess_.lastCaptureRequest ),
		BoolName( postprocess_.lastCaptureHdrAware ),
		BoolName( postprocess_.lastCaptureSupported ),
		BoolName( postprocess_.sceneTargetFloat ),
		postprocess_.finalShaderSrgbEncode ? "shader-srgb" : "none",
		BoolName( postprocess_.outputContractValid ),
		BoolName( postprocess_.textureSrgbDecodeConsistent ),
		postprocess_.textureSrgbStaleDecode );
	RI().Printf( PRINT_ALL, "glx: capture policy request %s selected %s hdr-aware %s supported %s sdr-frames %u hdr-requests %u unsupported-requests %u\n",
		GLX_RenderIR_CaptureExportPolicyName( postprocess_.lastCaptureRequest ),
		GLX_RenderIR_CaptureExportPolicyName( postprocess_.lastCaptureSelected ),
		BoolName( postprocess_.lastCaptureHdrAware ),
		BoolName( postprocess_.lastCaptureSupported ),
		postprocess_.captureSdrFrames,
		postprocess_.captureHdrRequestFrames,
		postprocess_.captureUnsupportedRequestFrames );
	RI().Printf( PRINT_ALL, "glx: texture audit srgb %u decode %u, linear %u decode %u, data %u decode %u, unknown %u decode %u, missing-srgb-decode %u, unexpected-decode %u\n",
		postprocess_.imageColorSpaceCounts[GLX_IMAGE_COLORSPACE_SRGB],
		postprocess_.imageSrgbDecodeCounts[GLX_IMAGE_COLORSPACE_SRGB],
		postprocess_.imageColorSpaceCounts[GLX_IMAGE_COLORSPACE_LINEAR],
		postprocess_.imageSrgbDecodeCounts[GLX_IMAGE_COLORSPACE_LINEAR],
		postprocess_.imageColorSpaceCounts[GLX_IMAGE_COLORSPACE_DATA],
		postprocess_.imageSrgbDecodeCounts[GLX_IMAGE_COLORSPACE_DATA],
		postprocess_.imageColorSpaceCounts[GLX_IMAGE_COLORSPACE_UNKNOWN],
		postprocess_.imageSrgbDecodeCounts[GLX_IMAGE_COLORSPACE_UNKNOWN],
		postprocess_.textureSrgbMissingDecode,
		postprocess_.imageUnexpectedSrgbDecode );
	RI().Printf( PRINT_ALL, "glx: stream draws %u/%u attempts, %u idx, %.2fMB/index %.2fMB/tex1 %.2fMB, mt %u, fog %u, depthfrag %u, texmod %u, env %u, dlight %u, screen %u, video %u, shadow %u, beam %u, post %u, fallbacks %u, skips %u\n",
		stream_.streamedDraws,
		stream_.streamedDrawAttempts,
		stream_.streamedDrawIndexes,
		static_cast<double>( stream_.streamedDrawBytes ) / ( 1024.0 * 1024.0 ),
		static_cast<double>( stream_.streamedDrawIndexBytes ) / ( 1024.0 * 1024.0 ),
		static_cast<double>( stream_.streamedDrawTexcoord1Bytes ) / ( 1024.0 * 1024.0 ),
		stream_.streamedDrawMultitextureDraws,
		stream_.streamedDrawFogDraws,
		stream_.streamedDrawDepthFragmentDraws,
		stream_.streamedDrawTexModDraws,
		stream_.streamedDrawEnvironmentDraws,
		stream_.streamedDrawDynamicLightDraws,
		stream_.streamedDrawScreenMapDraws,
		stream_.streamedDrawVideoMapDraws,
		stream_.streamedDrawShadowDraws,
		stream_.streamedDrawBeamDraws,
		stream_.streamedDrawPostProcessDraws,
		stream_.streamedDrawFallbacks,
		stream_.streamedDrawSkips );
	RI().Printf( PRINT_ALL, "glx: stream dlight attempts %u draws %u fallbacks %u bytes %.2fMB/%.2fMB index %.2fMB wraps %u rejects %u waits %u timeouts %u syncfail %u program binds %u/%u creates %u cache %u\n",
		stream_.streamedDrawDynamicLightAttempts,
		stream_.streamedDrawDynamicLightDraws,
		stream_.streamedDrawDynamicLightFallbacks,
		static_cast<double>( stream_.streamedDrawDynamicLightAttemptBytes ) / ( 1024.0 * 1024.0 ),
		static_cast<double>( stream_.streamedDrawDynamicLightBytes ) / ( 1024.0 * 1024.0 ),
		static_cast<double>( stream_.streamedDrawDynamicLightIndexBytes ) / ( 1024.0 * 1024.0 ),
		stream_.streamedDrawDynamicLightReserveWraps,
		stream_.streamedDrawDynamicLightSameFrameWrapRejects,
		stream_.streamedDrawDynamicLightSyncWaits,
		stream_.streamedDrawDynamicLightSyncTimeouts,
		stream_.streamedDrawDynamicLightSyncFailures,
		dlight_.binds,
		dlight_.bindAttempts,
		dlight_.programCreates,
		dlight_.programCacheHits );
	RI().Printf( PRINT_ALL, "glx: dlight state legacy %u texture %u fog %u shadowtex %u/%u fbo %u/%u changes %u\n",
		dlight_.legacyPasses,
		dlight_.textureBinds,
		dlight_.fogTextureBinds,
		dlight_.shadowTextureBinds,
		dlight_.shadowTextureFallbackBinds,
		dlight_.shadowFboBinds,
		dlight_.shadowFboRestores,
		dlight_.stateChanges );
	RI().Printf( PRINT_ALL, "glx: dlight build legacy %u skipped %u nohit %u verts %u idx %u/%u pm %u pmverts %u pmidx %u\n",
		dlight_.buildLegacyLights,
		dlight_.buildLegacySkippedLights,
		dlight_.buildLegacyNoHitLights,
		dlight_.buildLegacyVertexes,
		dlight_.buildLegacyIndexes,
		dlight_.buildLegacyLitIndexes,
		dlight_.buildPmPasses,
		dlight_.buildPmVertexes,
		dlight_.buildPmIndexes );
	RI().Printf( PRINT_ALL, "glx: dlight cull legacy verts %u idx %u\n",
		dlight_.cullLegacyVertexes,
		dlight_.cullLegacyIndexes );
	RI().Printf( PRINT_ALL, "glx: dlight scissor active %s candidates %u computed %u applied %u fallbacks %u pixels %llu/%llu\n",
		BoolName( GLX_Dlight_ScissorEnabled( dlight_ ) ),
		dlight_.scissorCandidates,
		dlight_.scissorComputed,
		dlight_.scissorApplied,
		dlight_.scissorFallbacks,
		dlight_.scissorPixels,
		dlight_.scissorViewportPixels );
	RI().Printf( PRINT_ALL, "glx: dlight projected lists sources %i/%u surface %u/%u packet active %u lookup %u/%u packet %u/%u copied %u dropped %u ir %u/%u dynamic %u/%u dynir %u/%u masks 0x%08x/0x%08x\n",
		dlight_.projectedSourceRecordCount,
		dlight_.projectedSourceFrames,
		dlight_.projectedListComplete,
		dlight_.projectedListIncomplete,
		dlight_.projectedPacketActivePackets,
		dlight_.projectedPacketItemLookupHits,
		dlight_.projectedPacketItemLookups,
		dlight_.projectedPacketListComplete,
		dlight_.projectedPacketListIncomplete,
		dlight_.projectedPacketListRecordCopies,
		dlight_.projectedPacketListDropped,
		dlight_.projectedPacketIRProducts,
		dlight_.projectedPacketIRRejected,
		dlight_.projectedDynamicListComplete,
		dlight_.projectedDynamicListIncomplete,
		dlight_.projectedDynamicIRProducts,
		dlight_.projectedDynamicIRRejected,
		dlight_.projectedListDroppedMask,
		dlight_.projectedPacketDroppedMask );
	RI().Printf( PRINT_ALL, "glx: dlight projected shader inputs %u/%u world %u dynamic %u programmable %u fallback %u invalid %u\n",
		dlight_.projectedShaderInputs,
		dlight_.projectedShaderInputRecords,
		dlight_.projectedShaderWorldPackets,
		dlight_.projectedShaderDynamicDraws,
		dlight_.projectedShaderProgrammableInputs,
		dlight_.projectedShaderFallbackInputs,
		dlight_.projectedShaderInvalidInputs );
	RI().Printf( PRINT_ALL, "glx: dlight projected shader uniforms attempts %u binds %u failures %u records %u truncated %u executable %u suppressed %u world %u/%u dynamic %u/%u limit-suppressed %u limit %i\n",
		dlight_.projectedShaderUniformAttempts,
		dlight_.projectedShaderUniformBinds,
		dlight_.projectedShaderUniformFailures,
		dlight_.projectedShaderUniformRecords,
		dlight_.projectedShaderUniformTruncated,
		dlight_.projectedShaderUniformExecutableBinds,
		dlight_.projectedShaderUniformSuppressedBinds,
		dlight_.projectedShaderUniformWorldExecutableBinds,
		dlight_.projectedShaderUniformWorldBinds,
		dlight_.projectedShaderUniformDynamicExecutableBinds,
		dlight_.projectedShaderUniformDynamicBinds,
		dlight_.projectedShaderUniformLimitSuppressions,
		GLX_PROJECTED_DLIGHT_UNIFORM_LIMIT );
	RI().Printf( PRINT_ALL, "glx: dlight projected shader resource attempts %u binds %u executable %u suppressed %u promotions %u failures %u records %u world %u dynamic %u binding %u\n",
		dlight_.projectedShaderResourceAttempts,
		dlight_.projectedShaderResourceBinds,
		dlight_.projectedShaderResourceExecutableBinds,
		dlight_.projectedShaderResourceSuppressedBinds,
		dlight_.projectedShaderResourceLimitPromotions,
		dlight_.projectedShaderResourceFailures,
		dlight_.projectedShaderResourceRecords,
		dlight_.projectedShaderResourceWorldExecutableBinds,
		dlight_.projectedShaderResourceDynamicExecutableBinds,
		GLX_PROJECTED_DLIGHT_STREAM_BINDING );
	RI().Printf( PRINT_ALL, "glx: dlight projected shader stream attempts %u uploads %u failures %u skipped %u records %u bytes %llu persistent %u world %u dynamic %u range %u/%u rangefail %u clears %u last %u/%u\n",
		dlight_.projectedShaderStreamAttempts,
		dlight_.projectedShaderStreamUploads,
		dlight_.projectedShaderStreamFailures,
		dlight_.projectedShaderStreamSkips,
		dlight_.projectedShaderStreamRecords,
		dlight_.projectedShaderStreamBytes,
		dlight_.projectedShaderStreamPersistentUploads,
		dlight_.projectedShaderStreamWorldUploads,
		dlight_.projectedShaderStreamDynamicUploads,
		dlight_.projectedShaderStreamRangeBinds,
		dlight_.projectedShaderStreamRangeAttempts,
		dlight_.projectedShaderStreamRangeFailures,
		dlight_.projectedShaderStreamRangeClears,
		dlight_.projectedShaderStreamLastOffset,
		dlight_.projectedShaderStreamLastBytes );
	RI().Printf( PRINT_ALL, "glx: dlight projected shader arena reserves %u uploads %u failures %u wraps %u rejects %u waits %u timeouts %u syncfail %u bytes %llu light-records %u list-records %u world-records %u dynamic-records %u range %u/%u rangefail %u clears %u authoritative %u/%u authfail %u fallbacks %u auth-clears %u bound %s cursor %u last %u/%u/%u\n",
		dlight_.projectedShaderArenaReserveAttempts,
		dlight_.projectedShaderArenaUploads,
		dlight_.projectedShaderArenaFailures,
		dlight_.projectedShaderArenaWraps,
		dlight_.projectedShaderArenaSameFrameRejects,
		dlight_.projectedShaderArenaWaits,
		dlight_.projectedShaderArenaTimeouts,
		dlight_.projectedShaderArenaSyncFailures,
		dlight_.projectedShaderArenaBytes,
		dlight_.projectedShaderArenaLightRecordUploads,
		dlight_.projectedShaderArenaListRecordUploads,
		dlight_.projectedShaderArenaWorldRecords,
		dlight_.projectedShaderArenaDynamicRecords,
		dlight_.projectedShaderArenaRangeBinds,
		dlight_.projectedShaderArenaRangeAttempts,
		dlight_.projectedShaderArenaRangeFailures,
		dlight_.projectedShaderArenaRangeClears,
		dlight_.projectedShaderArenaAuthoritativeBinds,
		dlight_.projectedShaderArenaAuthoritativeAttempts,
		dlight_.projectedShaderArenaAuthoritativeFailures,
		dlight_.projectedShaderArenaAuthoritativeFallbacks,
		dlight_.projectedShaderArenaAuthoritativeClears,
		BoolName( dlight_.projectedShaderArenaRangeBound ),
		dlight_.projectedShaderArenaFrameBytes,
		dlight_.projectedShaderArenaLastBuffer,
		dlight_.projectedShaderArenaLastOffset,
		dlight_.projectedShaderArenaLastBytes );
	RI().Printf( PRINT_ALL, "glx: dlight projected shader MDI attempts %u eligible %u uploads %u failures %u skipped %u records %u idx %u bytes %llu last %u/%u\n",
		dlight_.projectedShaderMdiAttempts,
		dlight_.projectedShaderMdiEligible,
		dlight_.projectedShaderMdiCommandUploads,
		dlight_.projectedShaderMdiCommandFailures,
		dlight_.projectedShaderMdiCommandSkips,
		dlight_.projectedShaderMdiRecords,
		dlight_.projectedShaderMdiIndexes,
		dlight_.projectedShaderMdiCommandBytes,
		dlight_.projectedShaderMdiLastOffset,
		dlight_.projectedShaderMdiLastBytes );
	RI().Printf( PRINT_ALL, "glx: dlight projected shader MDI command ring reserves %u commits %u wraps %u failures %u slots %u cursor %u last %u/%u/%u/%u\n",
		dlight_.projectedShaderMdiCommandRingReserves,
		dlight_.projectedShaderMdiCommandRingCommits,
		dlight_.projectedShaderMdiCommandRingWraps,
		dlight_.projectedShaderMdiCommandRingFailures,
		GLX_PROJECTED_DLIGHT_MDI_COMMAND_RING_LIMIT,
		dlight_.projectedShaderMdiCommandRingCursor,
		dlight_.projectedShaderMdiCommandRingLastSlot,
		dlight_.projectedShaderMdiCommandRingLastBuffer,
		dlight_.projectedShaderMdiCommandRingLastOffset,
		dlight_.projectedShaderMdiCommandRingLastBytes );
	RI().Printf( PRINT_ALL, "glx: dlight projected shader MDI submit attempts %u plans %u ready %u fallbacks %u skipped %u records %u idx %u buffer %u last %u/%u\n",
		dlight_.projectedShaderMdiSubmitAttempts,
		dlight_.projectedShaderMdiSubmitPlans,
		dlight_.projectedShaderMdiSubmitReady,
		dlight_.projectedShaderMdiSubmitFallbacks,
		dlight_.projectedShaderMdiSubmitSkips,
		dlight_.projectedShaderMdiSubmitRecords,
		dlight_.projectedShaderMdiSubmitIndexes,
		dlight_.projectedShaderMdiSubmitLastBuffer,
		dlight_.projectedShaderMdiSubmitLastOffset,
		dlight_.projectedShaderMdiSubmitLastBytes );
	RI().Printf( PRINT_ALL, "glx: dlight projected shader MDI batch attempts %u batches %u ready %u fallbacks %u rejects %u glerr %u records %u idx %u submitted %u/%u largest %u reject %u buffer %u range %u/%u\n",
		dlight_.projectedShaderMdiBatchAttempts,
		dlight_.projectedShaderMdiBatchBatches,
		dlight_.projectedShaderMdiBatchReady,
		dlight_.projectedShaderMdiBatchFallbacks,
		dlight_.projectedShaderMdiBatchRejects,
		dlight_.projectedShaderMdiBatchGlErrors,
		dlight_.projectedShaderMdiBatchRecords,
		dlight_.projectedShaderMdiBatchIndexes,
		dlight_.projectedShaderMdiBatchSubmittedDraws,
		dlight_.projectedShaderMdiBatchSubmittedIndexes,
		dlight_.projectedShaderMdiBatchLargest,
		dlight_.projectedShaderMdiBatchLastReject,
		dlight_.projectedShaderMdiBatchLastBuffer,
		dlight_.projectedShaderMdiBatchLastOffset,
		dlight_.projectedShaderMdiBatchLastBytes );
	RI().Printf( PRINT_ALL, "glx: stream categories entity %u/%u, particle %u/%u, poly %u/%u, mark %u/%u, weapon %u/%u, ui %u/%u, beam %u/%u, dlight %u/%u, special %u/%u\n",
		stream_.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_ENTITY],
		stream_.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_ENTITY],
		stream_.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_PARTICLE],
		stream_.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_PARTICLE],
		stream_.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_POLY],
		stream_.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_POLY],
		stream_.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_MARK],
		stream_.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_MARK],
		stream_.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_WEAPON],
		stream_.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_WEAPON],
		stream_.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_UI],
		stream_.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_UI],
		stream_.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_BEAM],
		stream_.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_BEAM],
		stream_.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_DLIGHT],
		stream_.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_DLIGHT],
		stream_.streamedDrawCategoryDraws[GLX_DYNAMIC_CATEGORY_SPECIAL],
		stream_.streamedDrawCategoryAttempts[GLX_DYNAMIC_CATEGORY_SPECIAL] );
	RI().Printf( PRINT_ALL, "glx: stream roles generic %u/%u/%u, dlight %u/%u/%u, shadow %u/%u/%u, beam %u/%u/%u, post %u/%u/%u\n",
		stream_.streamedDrawRoleDraws[static_cast<int>( DynamicDrawRole::Generic )],
		stream_.streamedDrawRoleAttempts[static_cast<int>( DynamicDrawRole::Generic )],
		stream_.streamedDrawRoleFallbacks[static_cast<int>( DynamicDrawRole::Generic )],
		stream_.streamedDrawRoleDraws[static_cast<int>( DynamicDrawRole::DynamicLight )],
		stream_.streamedDrawRoleAttempts[static_cast<int>( DynamicDrawRole::DynamicLight )],
		stream_.streamedDrawRoleFallbacks[static_cast<int>( DynamicDrawRole::DynamicLight )],
		stream_.streamedDrawRoleDraws[static_cast<int>( DynamicDrawRole::Shadow )],
		stream_.streamedDrawRoleAttempts[static_cast<int>( DynamicDrawRole::Shadow )],
		stream_.streamedDrawRoleFallbacks[static_cast<int>( DynamicDrawRole::Shadow )],
		stream_.streamedDrawRoleDraws[static_cast<int>( DynamicDrawRole::Beam )],
		stream_.streamedDrawRoleAttempts[static_cast<int>( DynamicDrawRole::Beam )],
		stream_.streamedDrawRoleFallbacks[static_cast<int>( DynamicDrawRole::Beam )],
		stream_.streamedDrawRoleDraws[static_cast<int>( DynamicDrawRole::PostProcess )],
		stream_.streamedDrawRoleAttempts[static_cast<int>( DynamicDrawRole::PostProcess )],
		stream_.streamedDrawRoleFallbacks[static_cast<int>( DynamicDrawRole::PostProcess )] );
	RI().Printf( PRINT_ALL, "glx: stream reservation last %u bytes at %u using %s, largest %u bytes, same-frame wrap rejects %u\n",
		stream_.lastReservationBytes,
		stream_.lastReservationOffset,
		GLX_Stream_StrategyName( stream_.lastReservationStrategy ),
		stream_.largestReservationBytes,
		stream_.sameFrameWrapRejects );
	RI().Printf( PRINT_ALL, "glx: stream binding cache queries %u hits %u restores %u invalidations %u external %u array-known %s array-buffer %u element-known %s element-buffer %u\n",
		stream_.arrayBufferBindingQueries,
		stream_.arrayBufferBindingCacheHits,
		stream_.arrayBufferBindingRestores,
		stream_.arrayBufferBindingInvalidations,
		stream_.bufferBindingExternalUpdates,
		BoolName( stream_.arrayBufferBindingKnown ),
		stream_.arrayBufferBinding,
		BoolName( stream_.elementArrayBufferBindingKnown ),
		stream_.elementArrayBufferBinding );
	RI().Printf( PRINT_ALL, "glx: static queues %u, last %u items/%u idx, device %u idx in %u runs, soft %u idx, largest run %u idx\n",
		staticWorld_.queueBatches,
		staticWorld_.lastQueueItems,
		staticWorld_.lastQueueIndexes,
		staticWorld_.lastQueueDeviceIndexes,
		staticWorld_.lastQueueDeviceRuns,
		staticWorld_.lastQueueSoftIndexes,
		staticWorld_.lastQueueLargestDeviceRunIndexes );
	RI().Printf( PRINT_ALL, "glx: static queue packets last %u full/%u partial/%u miss/%u mismatch, total %u full/%u partial/%u miss\n",
		staticWorld_.lastQueueDeviceFullPacketRuns,
		staticWorld_.lastQueueDevicePartialPacketRuns,
		staticWorld_.lastQueueDevicePacketMisses,
		staticWorld_.lastQueueDeviceItemMismatches,
		staticWorld_.queueDeviceFullPacketRuns,
		staticWorld_.queueDevicePartialPacketRuns,
		staticWorld_.queueDevicePacketMisses );
	RI().Printf( PRINT_ALL, "glx: static indirect queue last %u commands/%u runs/%u idx/%u breaks, largest %u cmds/%u idx, spans %u+%u overflow; total %u commands/%u runs/%u idx/%u breaks, largest %u idx\n",
		staticWorld_.lastQueueDeviceIndirectCommands,
		staticWorld_.lastQueueDeviceIndirectCommandRuns,
		staticWorld_.lastQueueDeviceIndirectIndexes,
		staticWorld_.lastQueueDeviceIndirectBreaks,
		staticWorld_.lastQueueLargestIndirectCommandRun,
		staticWorld_.lastQueueLargestIndirectCommandSpanIndexes,
		staticWorld_.lastQueueCommandSpanCount,
		staticWorld_.lastQueueCommandSpanOverflow,
		staticWorld_.queueDeviceIndirectCommands,
		staticWorld_.queueDeviceIndirectCommandRuns,
		staticWorld_.queueDeviceIndirectIndexes,
		staticWorld_.queueDeviceIndirectBreaks,
		staticWorld_.largestIndirectCommandSpanIndexes );
	RI().Printf( PRINT_ALL, "glx: static packet lookup %u mapped/max %i, hits %u, misses %u, fallbacks %u, mismatches %u, overflows %u\n",
		staticWorld_.itemLookupMappedItems,
		staticWorld_.itemLookupMaxItem,
		staticWorld_.itemLookupHits,
		staticWorld_.itemLookupMisses,
		staticWorld_.itemLookupFallbacks,
		staticWorld_.itemLookupMismatches,
		staticWorld_.itemLookupOverflows );
	RI().Printf( PRINT_ALL, "glx: static indirect packets %u commands/%u idx/%u bytes, invalid %u, misaligned %u\n",
		staticWorld_.indirectPacketCount,
		staticWorld_.indirectPacketIndexes,
		staticWorld_.indirectPacketBytes,
		staticWorld_.indirectPacketInvalid,
		staticWorld_.indirectPacketMisaligned );
	RI().Printf( PRINT_ALL, "glx: static indirect caps draw %s, multidraw %s\n",
		BoolName( staticWorld_.drawIndirectAvailable ),
		BoolName( staticWorld_.multiDrawIndirectAvailable ) );
	RI().Printf( PRINT_ALL, "glx: static indirect buffer %s, builds %u, skips %u, unsupported %u, failures %u, %u commands/%u bytes\n",
		staticWorld_.indirectBufferReady ? "ready" : "off",
		staticWorld_.indirectBufferBuilds,
		staticWorld_.indirectBufferSkips,
		staticWorld_.indirectBufferUnsupported,
		staticWorld_.indirectBufferFailures,
		staticWorld_.indirectBufferCommands,
		staticWorld_.indirectBufferBytes );
	RI().Printf( PRINT_ALL, "glx: static indirect draw %u/%u calls, %u idx, fallbacks %u, skips %u, no-command %u, errors %u\n",
		staticWorld_.indirectDrawCalls,
		staticWorld_.indirectDrawAttempts,
		staticWorld_.indirectDrawIndexes,
		staticWorld_.indirectDrawFallbacks,
		staticWorld_.indirectDrawSkips,
		staticWorld_.indirectDrawNoCommand,
		staticWorld_.indirectDrawErrors );
	RI().Printf( PRINT_ALL, "glx: static draw %u/%u calls, %u idx, packets %u full/%u partial/%u miss, manifest %u/%u idx, soft %u/%u calls/%u idx, arena %u, legacy %u, fallbacks %u, policy skips %u\n",
		staticWorld_.drawCalls,
		staticWorld_.drawAttempts,
		staticWorld_.drawIndexes,
		staticWorld_.drawPacketFullHits,
		staticWorld_.drawPacketPartialHits,
		staticWorld_.drawPacketMisses,
		staticWorld_.drawManifestPacketCalls,
		staticWorld_.drawManifestPacketIndexes,
		staticWorld_.softDrawCalls,
		staticWorld_.softDrawAttempts,
		staticWorld_.softDrawIndexes,
		staticWorld_.drawArenaCalls,
		staticWorld_.drawLegacyBufferCalls,
		staticWorld_.drawFallbacks,
		staticWorld_.drawPolicySkips );
	RI().Printf( PRINT_ALL, "glx: static multidraw %u calls/%u runs/%u idx, attempts %u, fallbacks %u\n",
		staticWorld_.multiDrawCalls,
		staticWorld_.multiDrawRuns,
		staticWorld_.multiDrawIndexes,
		staticWorld_.multiDrawAttempts,
		staticWorld_.multiDrawFallbacks );
	RI().Printf( PRINT_ALL, "glx: static filtered multidraw attempts %u, batches %u, runs %u, candidates %u, skips %u\n",
		staticWorld_.multiDrawFilteredAttempts,
		staticWorld_.multiDrawFilteredBatches,
		staticWorld_.multiDrawFilteredRuns,
		staticWorld_.multiDrawFilteredCandidates,
		staticWorld_.multiDrawFilteredSkips );
	RI().Printf( PRINT_ALL, "glx: static filtered multidraw barriers %u, invalid %u, policy %u, last %s at run %i\n",
		staticWorld_.multiDrawFilteredOrderBarriers,
		staticWorld_.multiDrawFilteredInvalidBarriers,
		staticWorld_.multiDrawFilteredPolicyBarriers,
		GLX_StaticWorld_FilteredBarrierName( staticWorld_.multiDrawFilteredLastBarrierReason ),
		staticWorld_.multiDrawFilteredLastBarrierRun );
	RI().Printf( PRINT_ALL, "glx: static MDI %u/%u calls, %u runs/%u idx, fallbacks %u, skips %u, errors %u, largest %u\n",
		staticWorld_.multiDrawIndirectCalls,
		staticWorld_.multiDrawIndirectAttempts,
		staticWorld_.multiDrawIndirectRuns,
		staticWorld_.multiDrawIndirectIndexes,
		staticWorld_.multiDrawIndirectFallbacks,
		staticWorld_.multiDrawIndirectSkips,
		staticWorld_.multiDrawIndirectErrors,
		staticWorld_.multiDrawIndirectLargestRun );
	RI().Printf( PRINT_ALL, "glx: static MDI shape last %s %u runs/%u idx/first %i, largest %u idx\n",
		GLX_Module_MdiSourceName( staticWorld_.multiDrawIndirectLastSource ),
		staticWorld_.multiDrawIndirectLastRuns,
		staticWorld_.multiDrawIndirectLastIndexes,
		staticWorld_.multiDrawIndirectLastFirstCommand,
		staticWorld_.multiDrawIndirectLargestIndexes );
	RI().Printf( PRINT_ALL, "glx: static MDI sources static %u, compact %u calls/%u uploads/%u subdata/%u orphan/%u grow/%u bytes, failures %u, buffer %u/%u\n",
		staticWorld_.multiDrawIndirectStaticCalls,
		staticWorld_.multiDrawIndirectCompactCalls,
		staticWorld_.multiDrawIndirectCompactUploads,
		staticWorld_.multiDrawIndirectCompactSubDatas,
		staticWorld_.multiDrawIndirectCompactOrphans,
		staticWorld_.multiDrawIndirectCompactGrows,
		staticWorld_.multiDrawIndirectCompactBytes,
		staticWorld_.multiDrawIndirectCompactFailures,
		staticWorld_.indirectCompactBufferBytes,
		staticWorld_.indirectCompactBufferCapacityBytes );
	RI().Printf( PRINT_ALL, "glx: static MDI last reject %s, %u runs/%u idx, command %i\n",
		GLX_StaticWorld_MdiRejectName( staticWorld_.multiDrawIndirectLastRejectReason ),
		staticWorld_.multiDrawIndirectLastRejectRuns,
		staticWorld_.multiDrawIndirectLastRejectIndexes,
		staticWorld_.multiDrawIndirectLastRejectCommand );
	RI().Printf( PRINT_ALL, "glx: static MDI spans attempts %u, batches %u, mdi runs %u, fallback runs %u, singles %u, single draws %u/%u indirect\n",
		staticWorld_.multiDrawIndirectSpanAttempts,
		staticWorld_.multiDrawIndirectSpanBatches,
		staticWorld_.multiDrawIndirectSpanMdiRuns,
		staticWorld_.multiDrawIndirectSpanFallbackRuns,
		staticWorld_.multiDrawIndirectSpanSingles,
		staticWorld_.multiDrawIndirectSpanSingleDraws,
		staticWorld_.multiDrawIndirectSpanSingleIndirectDraws );
	RI().Printf( PRINT_ALL, "glx: static MDI span shape last %u seg/%u mdi/%u fallback/%u single, largest %u seg\n",
		staticWorld_.multiDrawIndirectSpanLastSegments,
		staticWorld_.multiDrawIndirectSpanLastMdiRuns,
		staticWorld_.multiDrawIndirectSpanLastFallbackRuns,
		staticWorld_.multiDrawIndirectSpanLastSingles,
		staticWorld_.multiDrawIndirectSpanLargestSegments );
	RI().Printf( PRINT_ALL, "glx: static MDI command spans attempts %u, batches %u, mdi runs %u, fallback runs %u, singleton draws %u/%u indirect, singleton blocks %u\n",
		staticWorld_.multiDrawIndirectCommandSpanAttempts,
		staticWorld_.multiDrawIndirectCommandSpanBatches,
		staticWorld_.multiDrawIndirectCommandSpanMdiRuns,
		staticWorld_.multiDrawIndirectCommandSpanFallbackRuns,
		staticWorld_.multiDrawIndirectCommandSpanSingletonDraws,
		staticWorld_.multiDrawIndirectCommandSpanSingletonIndirectDraws,
		staticWorld_.multiDrawIndirectCommandSpanSingletons );
	RI().Printf( PRINT_ALL, "glx: static MDI command span shape last %u seg/%u mdi/%u fallback/%u singleton, largest %u seg\n",
		staticWorld_.multiDrawIndirectCommandSpanLastSegments,
		staticWorld_.multiDrawIndirectCommandSpanLastMdiRuns,
		staticWorld_.multiDrawIndirectCommandSpanLastFallbackRuns,
		staticWorld_.multiDrawIndirectCommandSpanLastSingletonDraws,
		staticWorld_.multiDrawIndirectCommandSpanLargestSegments );
	RI().Printf( PRINT_ALL, "glx: static MDI reasons unsupported %u, short %u, nonmanifest %u, missing %u, noncontiguous %u\n",
		staticWorld_.multiDrawIndirectUnsupported,
		staticWorld_.multiDrawIndirectShortBatches,
		staticWorld_.multiDrawIndirectNonManifest,
		staticWorld_.multiDrawIndirectMissingCommand,
		staticWorld_.multiDrawIndirectNonContiguous );
}

void RendererModule::StreamTest()
{
	GLX_Stream_RunSelfTest( &stream_ );
}

qboolean RendererModule::DrawElements( unsigned int mode, int count, unsigned int type,
	const void *indices, int legacyReason, int profilerPath )
{
	return DrawElementsClassified( mode, count, type, indices, legacyReason, profilerPath, 0, 0u );
}

qboolean RendererModule::DrawElementsClassified( unsigned int mode, int count, unsigned int type,
	const void *indices, int legacyReason, int profilerPath, int materialFlags,
	unsigned int categoryMask )
{
	return DrawElementsClassifiedProjectedDlights( mode, count, type, indices,
		legacyReason, profilerPath, materialFlags, categoryMask, 0u );
}

qboolean RendererModule::DrawElementsClassifiedProjectedDlights( unsigned int mode,
	int count, unsigned int type, const void *indices, int legacyReason,
	int profilerPath, int materialFlags, unsigned int categoryMask,
	unsigned int lightMask )
{
	DynamicDraw draw = GLX_Module_IndexedDrawIR( mode, count, type, indices,
		legacyReason, profilerPath, materialFlags, categoryMask );
	ProjectedDlightShaderExecutionPlan projectedExecutionPlan {};
	ProjectedDlightShaderInput projectedShaderInput {};
	ProjectedDlightResourceRange projectedResourceRange {};
	qboolean hasProjectedShaderInput = qfalse;
	qboolean projectedProgrammable = qfalse;
	qboolean projectedShaderBound = qfalse;
	qboolean projectedStreamReady = qfalse;
	qboolean projectedMdiSubmitted = qfalse;
	qboolean projectedOwnedProgram = qfalse;

	if ( draw.role == DynamicDrawRole::DynamicLight && lightMask ) {
		draw.projectedDlights = GLX_Dlight_BuildProjectedDynamicDlightList(
			&dlight_, lightMask );
	}

	if ( legacyReason >= 0 ) {
		GLX_Profiler_RecordLegacyDelegation( &profiler_, legacyReason, count );
		if ( requireOwnership_ && requireOwnership_->integer ) {
			return qfalse;
		}
	}

	if ( draw.projectedDlights.recordCount > 0 ) {
		projectedProgrammable = GLX_Dlight_ProjectedShaderProgrammable( dlight_,
			caps_.tier );
		projectedShaderInput = GLX_RenderIR_MakeProjectedDlightShaderInput( draw,
			projectedProgrammable );
		hasProjectedShaderInput = qtrue;
		if ( projectedProgrammable &&
			GLX_RenderIR_TierSupportsDynamicDraw( caps_.tier, draw ) ) {
			if ( !GLX_Dlight_CurrentProgram( &dlight_ ) &&
				GLX_Dlight_ProjectedProgramEnabled( dlight_ ) ) {
				// Legacy-mode streamed dlight draws arrive with no lighting
				// pass owning the dlight program; bind the projected-only
				// variant for this draw so the shader input can execute.
				projectedOwnedProgram = GLX_Dlight_BindProjectedOnlyProgram(
					&dlight_, caps_.tier );
			}
			if ( GLX_Dlight_CurrentProgram( &dlight_ ) ) {
				projectedStreamReady =
					StageProjectedDlightShaderInput( projectedShaderInput,
						&projectedResourceRange );
				projectedShaderBound = GLX_Dlight_BindProjectedShaderInput( &dlight_,
					projectedShaderInput,
					projectedStreamReady ? &projectedResourceRange : nullptr,
					&projectedExecutionPlan );
				if ( !projectedShaderBound ) {
					GLX_Dlight_ClearProjectedShaderInput( &dlight_ );
				}
			}
		}
		if ( !projectedShaderBound && GLX_Dlight_CurrentProgram( &dlight_ ) ) {
			GLX_Dlight_ClearProjectedShaderInput( &dlight_ );
		}
		ProjectedDlightShaderReplacementPlan replacementPlan =
			GLX_RenderIR_PlanProjectedDlightShaderReplacement(
				projectedShaderInput,
				GLX_Dlight_ProjectedProgramEnabled( dlight_ ),
				GLX_RenderIR_TierSupportsDynamicDraw( caps_.tier, draw ),
				projectedExecutionPlan );
		if ( replacementPlan.legacyFallback ) {
			GLX_Dlight_ClearProjectedShaderInput( &dlight_ );
			if ( projectedOwnedProgram ) {
				GLX_Dlight_Unbind( &dlight_ );
			}
			return qfalse;
		}
	} else if ( GLX_Dlight_CurrentProgram( &dlight_ ) ) {
		GLX_Dlight_ClearProjectedShaderInput( &dlight_ );
	}

	if ( draw.projectedDlights.recordCount > 0 && projectedStreamReady ) {
		ProjectedDlightDynamicMdiCommandUpload commandUpload {};
		ProjectedDlightDynamicMdiPlan mdiPlan =
			GLX_RenderIR_PlanProjectedDlightDynamicMdi( draw, caps_.tier,
				caps_.features.multiDrawIndirect, qtrue,
				GLX_Module_IndexStrideBytes( type ),
				GLX_PROJECTED_DLIGHT_STREAM_ALIGNMENT );
		if ( StageProjectedDlightDynamicMdiCommand( mdiPlan, &commandUpload ) ) {
			ProjectedDlightDynamicMdiSubmitPlan submitPlan =
				GLX_RenderIR_PlanProjectedDlightDynamicMdiSubmit( mdiPlan,
					commandUpload, mode, type,
					GLX_Dlight_ProjectedMdiEnabled( dlight_ ),
					&projectedResourceRange,
					stream_.elementArrayBufferBindingKnown ?
						stream_.elementArrayBufferBinding : 0u );
			ProjectedDlightDynamicMdiBatchPlan batchPlan =
				GLX_RenderIR_PlanProjectedDlightDynamicMdiBatch(
					&submitPlan, 1, GLX_PROJECTED_DLIGHT_MDI_BATCH_LIMIT );
			GLX_Dlight_RecordProjectedMdiSubmitPlan( &dlight_, submitPlan );
			GLX_Dlight_RecordProjectedMdiBatchPlan( &dlight_, batchPlan );
			GLX_Executor_ConsumeProjectedDlightDynamicMdiPlan(
				&executor_, mdiPlan );
			if ( batchPlan.eligible ) {
				projectedMdiSubmitted =
					GLX_Dlight_SubmitProjectedMdiBatch( &dlight_, batchPlan );
			}
		}
	}

	if ( projectedMdiSubmitted ) {
		if ( !GLX_Executor_ConsumeDynamicDraw( &executor_, draw ) ) {
			dlight_.projectedDynamicIRRejected++;
		} else {
			dlight_.projectedDynamicIRProducts++;
			if ( hasProjectedShaderInput ) {
				GLX_Dlight_RecordProjectedShaderInput( &dlight_,
					projectedShaderInput );
			}
		}
		if ( profilerPath >= 0 ) {
			GLX_Profiler_RecordDraw( &profiler_, count, profilerPath );
		}
		if ( projectedOwnedProgram ) {
			GLX_Dlight_Unbind( &dlight_ );
		}
		return qtrue;
	}

	if ( !GLX_Executor_ExecuteDynamicDraw( &executor_, draw ) ) {
		if ( draw.projectedDlights.recordCount > 0 ) {
			dlight_.projectedDynamicIRRejected++;
		}
		if ( projectedOwnedProgram ) {
			GLX_Dlight_Unbind( &dlight_ );
		}
		return qfalse;
	}
	if ( draw.projectedDlights.recordCount > 0 ) {
		dlight_.projectedDynamicIRProducts++;
		if ( hasProjectedShaderInput ) {
			GLX_Dlight_RecordProjectedShaderInput( &dlight_, projectedShaderInput );
		}
	}
	if ( profilerPath >= 0 ) {
		GLX_Profiler_RecordDraw( &profiler_, count, profilerPath );
	}
	if ( projectedOwnedProgram ) {
		GLX_Dlight_Unbind( &dlight_ );
	}
	return qtrue;
}

qboolean RendererModule::DrawArrays( unsigned int mode, int first, int count,
	int legacyReason, int profilerPath )
{
	return DrawArraysClassified( mode, first, count, legacyReason, profilerPath, 0, 0u );
}

qboolean RendererModule::DrawArraysClassified( unsigned int mode, int first, int count,
	int legacyReason, int profilerPath, int materialFlags, unsigned int categoryMask )
{
	DynamicDraw draw = GLX_Module_ArrayDrawIR( mode, first, count, legacyReason,
		profilerPath, materialFlags, categoryMask );

	if ( legacyReason >= 0 ) {
		GLX_Profiler_RecordLegacyDelegation( &profiler_, legacyReason, count );
		if ( requireOwnership_ && requireOwnership_->integer ) {
			return qfalse;
		}
	}

	if ( GLX_Dlight_CurrentProgram( &dlight_ ) ) {
		GLX_Dlight_ClearProjectedShaderInput( &dlight_ );
	}
	if ( !GLX_Executor_ExecuteDynamicDraw( &executor_, draw ) ) {
		return qfalse;
	}
	if ( profilerPath >= 0 ) {
		GLX_Profiler_RecordDraw( &profiler_, count, profilerPath );
	}
	return qtrue;
}

void RendererModule::RecordDraw( int indexes, int path )
{
	GLX_Profiler_RecordDraw( &profiler_, indexes, path );
}

void RendererModule::RecordShaderBatch( const char *shaderName, int sort, int numPasses, int numVertexes, int numIndexes, int flags )
{
	GLX_Profiler_RecordShaderBatch( &profiler_, shaderName, sort, numPasses, numVertexes, numIndexes, flags );
}

void RendererModule::RecordMaterialStage( int path, int flags, unsigned int stateBits, int rgbGen, int alphaGen,
	int tcGen0, int tcGen1, int texMods0, int texMods1,
	unsigned int texModTypes0, unsigned int texModTypes1,
	unsigned int texModSequence0, unsigned int texModSequence1,
	int rgbWaveFunc, int alphaWaveFunc,
	unsigned int texModWaveFuncs0, unsigned int texModWaveFuncs1,
	int fogAdjust, int materialCombine, qboolean fogPass,
	int numVertexes, int numIndexes )
{
	MaterialIR material = GLX_Module_StageMaterialIR( 0, flags, stateBits,
		rgbGen, alphaGen, tcGen0, tcGen1, texMods0, texMods1,
		texModTypes0, texModTypes1, texModSequence0, texModSequence1,
		rgbWaveFunc, alphaWaveFunc, texModWaveFuncs0, texModWaveFuncs1,
		fogAdjust, materialCombine, fogPass );

	GLX_Profiler_RecordMaterialStage( &profiler_, path, flags, stateBits, rgbGen, alphaGen,
		tcGen0, tcGen1, texMods0, texMods1, numVertexes, numIndexes );
	GLX_Executor_ConsumeMaterial( &executor_, material );
}

qboolean RendererModule::MaterialRendererActive() const
{
	return GLX_Material_Active( material_ );
}

qboolean RendererModule::BindMaterialStage( int flags, unsigned int stateBits, int rgbGen, int alphaGen,
	int tcGen0, int tcGen1, int texMods0, int texMods1,
	unsigned int texModTypes0, unsigned int texModTypes1,
	unsigned int texModSequence0, unsigned int texModSequence1,
	int rgbWaveFunc, int alphaWaveFunc,
	unsigned int texModWaveFuncs0, unsigned int texModWaveFuncs1, int fogAdjust,
	int materialCombine, qboolean fogPass )
{
	MaterialRequest request {};
	MaterialIR material = GLX_Module_StageMaterialIR( 0, flags, stateBits,
		rgbGen, alphaGen, tcGen0, tcGen1, texMods0, texMods1,
		texModTypes0, texModTypes1, texModSequence0, texModSequence1,
		rgbWaveFunc, alphaWaveFunc, texModWaveFuncs0, texModWaveFuncs1,
		fogAdjust, materialCombine, fogPass );

	request.flags = flags;
	request.stateBits = stateBits;
	request.rgbGen = rgbGen;
	request.alphaGen = alphaGen;
	request.rgbWaveFunc = rgbWaveFunc;
	request.alphaWaveFunc = alphaWaveFunc;
	request.tcGen0 = tcGen0;
	request.tcGen1 = tcGen1;
	request.texMods0 = texMods0;
	request.texMods1 = texMods1;
	request.texModTypes0 = texModTypes0;
	request.texModTypes1 = texModTypes1;
	request.texModSequence0 = texModSequence0;
	request.texModSequence1 = texModSequence1;
	request.texModWaveFuncs0 = texModWaveFuncs0;
	request.texModWaveFuncs1 = texModWaveFuncs1;
	request.fogAdjust = fogAdjust;
	request.materialCombine = materialCombine;
	request.fogPass = fogPass;

	material_.lastRequest = request;
	return GLX_Material_BindIR( &material_, material );
}

qboolean RendererModule::BindFogMaterial()
{
	return GLX_Material_BindFog( &material_ );
}

qboolean RendererModule::BindLiquidMaterial( const float *params,
	const float *eyeAndCount, const float *targetInverse, const float *reflect,
	const float *impulses, const float *amplitudes )
{
	return GLX_Material_BindLiquid( &material_, params, eyeAndCount,
		targetInverse, reflect, impulses, amplitudes );
}

void RendererModule::UnbindMaterial()
{
	GLX_Material_Unbind( &material_ );
}

qboolean RendererModule::DlightProgramAvailable( qboolean linear, int fogMode,
	qboolean absLight, qboolean shadow )
{
	return GLX_Dlight_ProgramAvailable( &dlight_, linear, fogMode, absLight, shadow,
		GLX_Dlight_ProjectedStreamShaderEnabled( dlight_, caps_.tier ) );
}

qboolean RendererModule::BindDlightProgram( qboolean linear, int fogMode,
	qboolean absLight, qboolean shadow, const float *eyePos, const float *lightPos,
	const float *lightColor, const float *lightVector, const float *texFactors,
	const float *dlightFactors, const float *fogDistanceVector,
	const float *fogDepthVector, float fogEyeT, const float *dlightShadow,
	const float *shadowAtlas, const float *shadowDepth, const float *shadowFilter )
{
	GLX_Material_Unbind( &material_ );
	return GLX_Dlight_BindProgram( &dlight_, linear, fogMode, absLight, shadow,
		GLX_Dlight_ProjectedStreamShaderEnabled( dlight_, caps_.tier ),
		eyePos, lightPos, lightColor, lightVector, texFactors, dlightFactors,
		fogDistanceVector, fogDepthVector, fogEyeT,
		dlightShadow, shadowAtlas, shadowDepth, shadowFilter );
}

void RendererModule::UnbindDlightProgram()
{
	GLX_Dlight_Unbind( &dlight_ );
}

qboolean RendererModule::DlightProjectedProgramEnabled() const
{
	return GLX_Dlight_ProjectedProgramEnabled( dlight_ );
}

qboolean RendererModule::ProjectedDlightWorldOverlayActive()
{
	if ( !GLX_Dlight_ProjectedProgramEnabled( dlight_ ) ||
		!GLX_Dlight_ProjectedShaderProgrammable( dlight_, caps_.tier ) ) {
		return qfalse;
	}
	return dlight_.projectedPacketActivePackets > 0 ? qtrue : qfalse;
}

qboolean RendererModule::BindProjectedDlightOverlayProgram()
{
	GLX_Material_Unbind( &material_ );
	return GLX_Dlight_BindProjectedOnlyProgram( &dlight_, caps_.tier );
}

qboolean RendererModule::DlightScissorEnabled() const
{
	return GLX_Dlight_ScissorEnabled( dlight_ );
}

void RendererModule::RecordDlightState( int event )
{
	GLX_Dlight_RecordState( &dlight_, event );
}

void RendererModule::RecordDlightBuild( int legacyLights, int legacySkippedLights,
	int legacyNoHitLights, int legacyVertexes, int legacyIndexes,
	int legacyLitIndexes, int pmPasses, int pmVertexes, int pmIndexes )
{
	GLX_Dlight_RecordBuild( &dlight_, legacyLights, legacySkippedLights,
		legacyNoHitLights, legacyVertexes, legacyIndexes, legacyLitIndexes,
		pmPasses, pmVertexes, pmIndexes );
}

void RendererModule::RecordDlightCull( int legacyVertexes, int legacyIndexes )
{
	GLX_Dlight_RecordCull( &dlight_, legacyVertexes, legacyIndexes );
}

void RendererModule::RecordDlightScissor( qboolean computed, qboolean applied,
	int x, int y, int width, int height, int viewportWidth, int viewportHeight )
{
	(void)x;
	(void)y;
	GLX_Dlight_RecordScissor( &dlight_, computed, applied, width, height,
		viewportWidth, viewportHeight );
}

void RendererModule::RecordProjectedDlights( const glxProjectedDlightRecord_t *records,
	int count )
{
	GLX_Dlight_RecordProjectedDlights( &dlight_, records, count );
}

void RendererModule::RecordProjectedDlightList( int itemIndex, unsigned int lightMask )
{
	GLX_Dlight_RecordProjectedDlightList( &dlight_, &staticWorld_, itemIndex,
		lightMask );
}

qboolean RendererModule::StreamDrawEnabled() const
{
	return GLX_Stream_DrawEnabled( stream_ );
}

qboolean RendererModule::StreamDrawMultitextureEnabled() const
{
	return GLX_Stream_DrawMultitextureEnabled( stream_ );
}

qboolean RendererModule::StreamDrawFogEnabled() const
{
	return GLX_Stream_DrawFogEnabled( stream_ );
}

qboolean RendererModule::StreamDrawDepthFragmentEnabled() const
{
	return GLX_Stream_DrawDepthFragmentEnabled( stream_ );
}

qboolean RendererModule::StreamDrawShadowsEnabled() const
{
	return GLX_Stream_DrawShadowsEnabled( stream_ );
}

qboolean RendererModule::StreamDrawBeamsEnabled() const
{
	return GLX_Stream_DrawBeamsEnabled( stream_ );
}

qboolean RendererModule::StreamDrawPostProcessEnabled() const
{
	return GLX_Stream_DrawPostProcessEnabled( stream_ );
}

qboolean RendererModule::StreamDrawAllowsMaterial( int flags, unsigned int stateBits,
	int rgbGen, int alphaGen, int tcGen0, int tcGen1, int texMods0, int texMods1,
	unsigned int texModTypes0, unsigned int texModTypes1,
	unsigned int texModSequence0, unsigned int texModSequence1,
	int rgbWaveFunc, int alphaWaveFunc,
	unsigned int texModWaveFuncs0, unsigned int texModWaveFuncs1,
	int fogAdjust, int materialCombine, qboolean fogPass )
{
	const MaterialIR material = GLX_Module_StageMaterialIR( 0, flags, stateBits,
		rgbGen, alphaGen, tcGen0, tcGen1, texMods0, texMods1,
		texModTypes0, texModTypes1, texModSequence0, texModSequence1,
		rgbWaveFunc, alphaWaveFunc, texModWaveFuncs0, texModWaveFuncs1,
		fogAdjust, materialCombine, fogPass );
	return GLX_Stream_DrawAllowsMaterial( &stream_, material );
}

qboolean RendererModule::StreamReserve( int bytes, int alignment, glxStreamReservation_t *reservation )
{
	StreamReservation streamReservation;

	if ( bytes <= 0 || alignment <= 0 || !reservation ) {
		stream_.reserveFailures++;
		return qfalse;
	}

	if ( !GLX_Stream_Reserve( &stream_, static_cast<size_t>( bytes ), static_cast<size_t>( alignment ), &streamReservation ) ) {
		return qfalse;
	}

	GLX_Module_ToPublicReservation( streamReservation, reservation );
	return qtrue;
}

qboolean RendererModule::StreamUploadAt( glxStreamReservation_t *reservation, int relativeOffset, const void *data, int bytes )
{
	StreamReservation streamReservation;
	qboolean ok;

	if ( !reservation || relativeOffset < 0 || bytes <= 0 ) {
		stream_.uploadFailures++;
		return qfalse;
	}

	streamReservation = GLX_Module_FromPublicReservation( *reservation );
	ok = GLX_Stream_UploadAt( &stream_, &streamReservation, static_cast<size_t>( relativeOffset ), data, static_cast<size_t>( bytes ) );
	GLX_Module_ToPublicReservation( streamReservation, reservation );
	return ok;
}

void RendererModule::StreamCommit( glxStreamReservation_t *reservation )
{
	StreamReservation streamReservation;

	if ( !reservation ) {
		return;
	}

	streamReservation = GLX_Module_FromPublicReservation( *reservation );
	GLX_Stream_Commit( &stream_, &streamReservation );
	GLX_Module_ToPublicReservation( streamReservation, reservation );
}

void RendererModule::RecordStreamDlightReservation( const glxStreamReservation_t *reservation )
{
	StreamReservation streamReservation;

	if ( !reservation ) {
		return;
	}

	streamReservation = GLX_Module_FromPublicReservation( *reservation );
	GLX_Stream_RecordDlightReservation( &stream_, streamReservation );
}

GLuint RendererModule::BindStreamArrayBuffer( GLuint buffer )
{
	return GLX_Stream_BindArrayBufferCached( &stream_, buffer );
}

void RendererModule::RestoreStreamArrayBuffer( GLuint buffer )
{
	GLX_Stream_RestoreArrayBufferCached( &stream_, buffer );
}

GLuint RendererModule::BindStreamElementArrayBuffer( GLuint buffer )
{
	return GLX_Stream_BindElementArrayBufferCached( &stream_, buffer );
}

void RendererModule::RestoreStreamElementArrayBuffer( GLuint buffer )
{
	GLX_Stream_RestoreElementArrayBufferCached( &stream_, buffer );
}

void RendererModule::RecordStreamBufferBind( unsigned int target, GLuint buffer )
{
	GLX_Stream_RecordExternalBufferBind( &stream_, target, buffer );
}

void RendererModule::RecordStreamDrawResult( int numVertexes, int numIndexes, int totalBytes, int indexBytes,
	int texcoord1Bytes, qboolean multitexture, qboolean fog, qboolean depthFragment, int materialFlags,
	unsigned int categoryMask, qboolean success )
{
	UploadPlan upload = GLX_Module_StreamUploadPlan( totalBytes,
		totalBytes > indexBytes + texcoord1Bytes ? totalBytes - indexBytes - texcoord1Bytes : 0,
		indexBytes );
	upload.texcoordBytes = texcoord1Bytes > 0 ? static_cast<unsigned int>( texcoord1Bytes ) : 0;

	GLX_Stream_RecordDrawResult( &stream_, numVertexes, numIndexes, totalBytes, indexBytes,
		texcoord1Bytes, multitexture, fog, depthFragment, materialFlags, categoryMask, success );
	GLX_Executor_ConsumeUploadPlan( &executor_, upload );
}

void RendererModule::RecordStreamDrawSkip( int reason )
{
	GLX_Stream_RecordDrawSkip( &stream_, reason );
}

void RendererModule::ResetImageColorAudit()
{
	GLX_PostProcess_ResetImageColorAudit( &postprocess_ );
}

void RendererModule::RecordImageColorAudit( int colorSpace, qboolean srgbDecode )
{
	GLX_PostProcess_RecordImageColorAudit( &postprocess_, colorSpace, srgbDecode );
}

void RendererModule::RecordFboInit( qboolean requested, qboolean ready, qboolean programReady, qboolean framebufferFnsReady,
	int vidWidth, int vidHeight, int captureWidth, int captureHeight, int windowWidth, int windowHeight,
	int internalFormat, int textureFormat, int textureType, qboolean multiSampled, qboolean superSampled,
	qboolean windowAdjusted, int blitFilter, int hdrMode, int renderScaleMode, int bloomMode,
	qboolean textureSrgbAvailable, qboolean framebufferSrgbAvailable, qboolean framebufferSrgbEnabled )
{
	GLX_PostProcess_RecordFboInit( &postprocess_, requested, ready, programReady, framebufferFnsReady,
		vidWidth, vidHeight, captureWidth, captureHeight, windowWidth, windowHeight,
		internalFormat, textureFormat, textureType, multiSampled, superSampled,
		windowAdjusted, blitFilter, hdrMode, renderScaleMode, bloomMode,
		textureSrgbAvailable, framebufferSrgbAvailable, framebufferSrgbEnabled );
}

void RendererModule::RecordFboShutdown()
{
	GLX_PostProcess_RecordFboShutdown( &postprocess_ );
}

void RendererModule::RecordPostProcessFrame( qboolean minimized, qboolean bloomAvailable, qboolean programReady,
	int screenshotMask, qboolean windowAdjusted, int fboReadIndex, int hdrMode, int renderScaleMode,
	float greyscale, float legacyGamma, float legacyOverbright )
{
	GLX_PostProcess_RecordFrame( &postprocess_, minimized, bloomAvailable, programReady,
		screenshotMask, windowAdjusted, fboReadIndex, hdrMode, renderScaleMode, greyscale,
		legacyGamma, legacyOverbright );
	OutputTransform output = GLX_Module_OutputTransformIR( postprocess_ );
	PostOutputPlanInputs inputs {};
	inputs.tier = caps_.tier;
	inputs.output = output;
	inputs.captureRequest = postprocess_.lastCaptureRequest;
	inputs.fboReady = postprocess_.fboReady;
	inputs.programReady = programReady;
	inputs.framebufferFnsReady = postprocess_.framebufferFnsReady;
	inputs.outputContractValid = postprocess_.outputContractValid;
	inputs.bloomAvailable = bloomAvailable;
	inputs.postShaderExecutorEnabled = ( GLX_PostShader_ExecutionEnabled( postShader_ ) ||
		( output.crtAmount > 0.001f && postShader_.ready ) ) ? qtrue : qfalse;
	inputs.minimized = minimized;
	inputs.windowAdjusted = windowAdjusted;
	inputs.screenshotMask = screenshotMask;
	inputs.fboReadIndex = fboReadIndex;
	inputs.sequenceBase = static_cast<int>( postprocess_.frames ? postprocess_.frames - 1 : 0 );
	inputs.flags = ( minimized ? 0x1u : 0u ) | ( programReady ? 0x2u : 0u ) |
		( windowAdjusted ? 0x4u : 0u ) | ( screenshotMask ? 0x8u : 0u );

	PostOutputPlan plan = GLX_RenderIR_BuildPostOutputPlan( inputs );
	PostShaderPlan shaderPlan = GLX_Module_PostShaderPlanForOutputPlan( output, plan );
	if ( GLX_Module_PostOutputPlanHasNode( plan, PostNodeKind::BloomPrefinal ) ) {
		GLX_PostShader_RecordPlan( &postShader_,
			GLX_PostShader_BuildPlanForPass( output, qtrue, qfalse ) );
	}
	GLX_PostShader_RecordPlan( &postShader_, shaderPlan );
	postShaderBindBaseline_ = postShader_.directFinalBinds;
	postShaderExpectedBinds_ = plan.glxOwned ?
		GLX_Module_PostOutputExpectedShaderBinds( plan ) : 0u;
	qboolean consumed = qtrue;
	for ( int i = 0; i < plan.nodeCount; i++ ) {
		if ( !GLX_Executor_ConsumePostNode( &executor_, plan.nodes[i] ) ) {
			consumed = qfalse;
		}
	}
	if ( plan.outputTransformPresent && !GLX_Executor_ConsumeOutputTransform( &executor_, plan.output ) ) {
		consumed = qfalse;
	}
	GLX_PostProcess_RecordPostOutputPlan( &postprocess_, plan, consumed );
	GLX_PostProcess_RecordPostShaderPlan( &postprocess_, shaderPlan );
}

qboolean RendererModule::AutoExposureNeedsSamples( int *width, int *height ) const
{
	return GLX_PostProcess_AutoExposureNeedsSamples( &postprocess_, width, height );
}

float RendererModule::UpdateAutoExposure( float manualExposure, const float *rgba,
	int width, int height )
{
	return GLX_PostProcess_UpdateAutoExposure( &postprocess_, manualExposure,
		rgba, width, height );
}

qboolean RendererModule::TryBindPostShaderFinal( qboolean bloomComposite,
	qboolean outputTransform, float bloomIntensity )
{
	const OutputTransform output = GLX_Module_OutputTransformIR( postprocess_ );
	const PostShaderPlan shaderPlan = GLX_PostShader_BuildPlanForPass( output,
		bloomComposite, outputTransform );

	return GLX_PostShader_TryBindFinal( &postShader_, shaderPlan, output,
		bloomComposite, outputTransform, bloomIntensity );
}

qboolean RendererModule::TryBindPostShaderDirectFinal()
{
	return TryBindPostShaderFinal( qfalse, qtrue, 0.0f );
}

void RendererModule::UnbindPostShader()
{
	GLX_PostShader_Unbind( &postShader_ );
}

void RendererModule::RecordPostProcessResult( int result )
{
	PostNode node {};
	node.pass = FramePassKind::PostProcess;
	node.sequence = static_cast<int>( postprocess_.frames );
	node.output = GLX_Module_OutputTransformIR( postprocess_ );

	switch ( result ) {
	case GLX_POSTPROCESS_RESULT_BLOOM_FINAL:
		node.kind = PostNodeKind::BloomFinal;
		break;
	case GLX_POSTPROCESS_RESULT_GAMMA_DIRECT:
		node.kind = PostNodeKind::GammaDirect;
		break;
	case GLX_POSTPROCESS_RESULT_GAMMA_BLIT:
		node.kind = PostNodeKind::GammaBlit;
		break;
	default:
		node.kind = PostNodeKind::Resolve;
		break;
	}

	GLX_PostProcess_RecordFrameResult( &postprocess_, result );
	GLX_Executor_ConsumePostNode( &executor_, node );
	if ( postShaderExpectedBinds_ > 0u &&
		postShader_.directFinalBinds - postShaderBindBaseline_ < postShaderExpectedBinds_ ) {
		GLX_PostProcess_RecordPostOutputExecutionFallback( &postprocess_,
			GLX_POST_OUTPUT_FALLBACK_EXECUTOR_NOT_BOUND );
	}
	postShaderBindBaseline_ = postShader_.directFinalBinds;
	postShaderExpectedBinds_ = 0u;
}

void RendererModule::RecordColorGradeLut( qboolean active, int size, float scale )
{
	GLX_PostProcess_RecordColorGradeLut( &postprocess_, active, size, scale );
}

void RendererModule::RecordBloomCreate( int result, int requestedPasses, int effectivePasses,
	int textureUnits, int formatMode, int internalFormat, int textureFormat,
	int textureType )
{
	GLX_PostProcess_RecordBloomCreate( &postprocess_, result, requestedPasses,
		effectivePasses, textureUnits, formatMode, internalFormat,
		textureFormat, textureType );
}

void RendererModule::RecordBloom( int result, qboolean finalStage, int bloomMode, int requestedPasses,
	int effectivePasses, int blendBase, int filterSize, int textureUnits, int thresholdMode,
	int modulate, float threshold, float intensity, float reflection )
{
	GLX_PostProcess_RecordBloom( &postprocess_, result, finalStage, bloomMode, requestedPasses,
		effectivePasses, blendBase, filterSize, textureUnits, thresholdMode, modulate,
		threshold, intensity, reflection );
}

void RendererModule::RecordFboCopyScreen( int viewportWidth, int viewportHeight )
{
	GLX_PostProcess_RecordCopyScreen( &postprocess_, viewportWidth, viewportHeight );
}

void RendererModule::RecordFboBlit( int kind, qboolean depthOnly, int srcWidth, int srcHeight, int dstWidth, int dstHeight )
{
	GLX_PostProcess_RecordBlit( &postprocess_, kind, depthOnly, srcWidth, srcHeight, dstWidth, dstHeight );
	GLX_Profiler_RecordPostBlit( &profiler_ );
}

void RendererModule::RecordFboBind()
{
	GLX_Profiler_RecordPostBind( &profiler_ );
}

void RendererModule::RecordPostClear()
{
	GLX_Profiler_RecordPostClear( &profiler_ );
}

void RendererModule::RecordFullscreenPass()
{
	GLX_Profiler_RecordFullscreenPass( &profiler_ );
}

void RendererModule::PushShaderDebugGroup( const char *shaderName, int numVertexes, int numIndexes, int numPasses )
{
	char label[192];

	if ( !shaderName || !*shaderName ) {
		shaderName = "<unnamed>";
	}

	std::snprintf( label, sizeof( label ), "shader %s verts %i indexes %i passes %i",
		shaderName, numVertexes, numIndexes, numPasses );
	GLX_Debug_PushGroup( &debug_, label );
}

void RendererModule::PopDebugGroup()
{
	GLX_Debug_PopGroup( &debug_ );
}

void RendererModule::ShadowUploadTess( int numVertexes, int numIndexes, const void *xyz, int xyzBytes, const void *indexes, int indexBytes )
{
	if ( xyzBytes <= 0 || indexBytes <= 0 ) {
		return;
	}

	GLX_Stream_ShadowUploadTess( &stream_, numVertexes, numIndexes,
		xyz, static_cast<size_t>( xyzBytes ), indexes, static_cast<size_t>( indexBytes ) );
}

void RendererModule::RecordStaticWorldCache( int surfaces, int vertexes, int indexes, int vertexBytes, int indexBytes )
{
	GLX_StaticWorld_Record( &staticWorld_, surfaces, vertexes, indexes, vertexBytes, indexBytes );
}

void RendererModule::RecordStaticWorldBatches( int batches, int largestBatchSurfaces, int faceSurfaces,
	int gridSurfaces, int triangleSurfaces, int shaderStagePasses, int maxShaderStages )
{
	GLX_StaticWorld_RecordBatches( &staticWorld_, batches, largestBatchSurfaces, faceSurfaces,
		gridSurfaces, triangleSurfaces, shaderStagePasses, maxShaderStages );
}

void RendererModule::RecordStaticWorldPacket( const char *shaderName, int sort,
	int surfaces, int vertexes, int indexes, int firstItem, int itemCount, int vertexOffset, int vertexBytes,
	int indexOffset, int indexBytes, int shaderStagePasses, int flags )
{
	WorldPacket packet = GLX_Module_WorldPacketIR( staticWorld_.packetCount, sort,
		surfaces, vertexes, indexes, firstItem, itemCount, vertexOffset, vertexBytes,
		indexOffset, indexBytes, shaderStagePasses, flags );

	GLX_StaticWorld_RecordPacket( &staticWorld_, shaderName, sort,
		surfaces, vertexes, indexes, firstItem, itemCount, vertexOffset, vertexBytes,
		indexOffset, indexBytes, shaderStagePasses, flags );
	GLX_Executor_ConsumeWorldPacket( &executor_, packet );
}

void RendererModule::UploadStaticWorldArena( const void *vertexData, int vertexBytes, const void *indexData, int indexBytes )
{
	GLX_StaticWorld_UploadArena( &staticWorld_, vertexData, vertexBytes, indexData, indexBytes );
	GLX_StaticWorld_UploadIndirectCommands( &staticWorld_, caps_.features.drawIndirect );
	GLX_Debug_LabelObject( debug_, GL_BUFFER, staticWorld_.arenaVertexBuffer, "GLx static world vertex arena" );
	GLX_Debug_LabelObject( debug_, GL_BUFFER, staticWorld_.arenaIndexBuffer, "GLx static world index arena" );
	GLX_Debug_LabelObject( debug_, GL_BUFFER, staticWorld_.indirectCommandBuffer, "GLx static world indirect commands" );
}

GLuint RendererModule::StaticWorldArenaVertexBuffer()
{
	return GLX_StaticWorld_ArenaVertexBufferForDraw( &staticWorld_ );
}

GLuint RendererModule::StaticWorldArenaIndexBuffer()
{
	return GLX_StaticWorld_ArenaIndexBufferForDraw( &staticWorld_ );
}

qboolean RendererModule::PrepareStaticWorldProjectedDlightRun( int firstItem,
	int itemCount, WorldPacket *packet, ProjectedDlightShaderInput *shaderInput )
{
	int packetIndex;
	ProjectedDlightListRef projectedDlights {};

	if ( packet ) {
		*packet = {};
	}
	if ( shaderInput ) {
		*shaderInput = {};
	}
	if ( !packet || !shaderInput ) {
		return qfalse;
	}

	packetIndex = GLX_Module_StaticWorldPacketForItemRange( staticWorld_, firstItem,
		itemCount );
	if ( packetIndex < 0 ) {
		return qfalse;
	}

	projectedDlights = dlight_.projectedPacketListRefs[packetIndex];
	if ( projectedDlights.recordCount <= 0 ) {
		return qfalse;
	}

	*packet = GLX_Module_StaticWorldPacketIR( staticWorld_.packets[packetIndex],
		packetIndex, projectedDlights );
	*shaderInput = GLX_RenderIR_MakeProjectedDlightShaderInput( *packet,
		GLX_Dlight_ProjectedShaderProgrammable( dlight_, caps_.tier ) );
	return GLX_RenderIR_ValidateProjectedDlightShaderInput( *shaderInput );
}

qboolean RendererModule::StageProjectedDlightShaderInput(
	const ProjectedDlightShaderInput &shaderInput,
	ProjectedDlightResourceRange *resourceRange )
{
	StreamReservation reservation {};
	ProjectedDlightShaderStreamPlan streamPlan {};
	unsigned int uploadBytes;
	unsigned int lightRecords;
	unsigned int listRecords;
	qboolean rangeReady = qfalse;

	if ( resourceRange ) {
		*resourceRange = {};
	}
	if ( !GLX_RenderIR_ValidateProjectedDlightShaderInput( shaderInput ) ||
		!shaderInput.programmable || !GLX_Dlight_ProjectedProgramEnabled( dlight_ ) ) {
		return qfalse;
	}

	dlight_.projectedShaderStreamAttempts++;
	streamPlan = GLX_RenderIR_PlanProjectedDlightStreamUpload( shaderInput,
		caps_.tier,
		stream_.ready && stream_.persistentMapped &&
			stream_.strategy == StreamStrategy::PersistentMapped ? qtrue : qfalse,
		static_cast<unsigned int>( sizeof( ProjectedDlightStreamRecord ) ),
		GLX_PROJECTED_DLIGHT_STREAM_ALIGNMENT );
	if ( !streamPlan.upload ) {
		dlight_.projectedShaderStreamSkips++;
		return qfalse;
	}

	if ( !GLX_Dlight_FillProjectedStreamRecords( &dlight_, shaderInput,
			dlight_.projectedShaderArenaLightRecords ) ) {
		dlight_.projectedShaderStreamFailures++;
		dlight_.projectedShaderArenaFailures++;
		return qfalse;
	}
	listRecords = static_cast<unsigned int>(
		GLX_Dlight_FillProjectedArenaListRecords( &dlight_, shaderInput ) );
	lightRecords = static_cast<unsigned int>( shaderInput.projectedDlights.recordCount );
	uploadBytes = streamPlan.uploadPlan.bytes;

	dlight_.projectedShaderArenaReserveAttempts++;
	if ( !GLX_Stream_Reserve( &stream_, streamPlan.uploadPlan.bytes,
			static_cast<size_t>( streamPlan.uploadPlan.alignment ), &reservation ) ) {
		dlight_.projectedShaderStreamFailures++;
		dlight_.projectedShaderArenaFailures++;
		return qfalse;
	}
	GLX_Dlight_RecordProjectedShaderArenaReservation( &dlight_, reservation );

	if ( !GLX_Stream_Upload( &stream_, &reservation,
			dlight_.projectedShaderArenaLightRecords,
			streamPlan.uploadPlan.bytes ) ) {
		dlight_.projectedShaderStreamFailures++;
		dlight_.projectedShaderArenaFailures++;
		GLX_Stream_Commit( &stream_, &reservation );
		GLX_Stream_RecordDlightReservation( &stream_, reservation );
		return qfalse;
	}

	GLX_Stream_Commit( &stream_, &reservation );
	GLX_Stream_RecordDlightReservation( &stream_, reservation );
	rangeReady = GLX_Dlight_BindProjectedStreamRange( &dlight_, reservation.buffer,
		reservation.offset, reservation.bytes );
	GLX_Dlight_RecordProjectedShaderArenaRange( &dlight_, rangeReady );
	GLX_Dlight_RecordProjectedShaderArenaUpload( &dlight_, shaderInput, reservation,
		uploadBytes, lightRecords, listRecords );
	dlight_.projectedShaderStreamUploads++;
	dlight_.projectedShaderStreamRecords +=
		static_cast<unsigned int>( shaderInput.projectedDlights.recordCount );
	dlight_.projectedShaderStreamBytes +=
		static_cast<unsigned long long>( streamPlan.uploadPlan.bytes );
	dlight_.projectedShaderStreamLastOffset =
		static_cast<unsigned int>( reservation.offset > ~0u ? ~0u : reservation.offset );
	dlight_.projectedShaderStreamLastBytes =
		static_cast<unsigned int>( reservation.bytes > ~0u ? ~0u : reservation.bytes );
	if ( reservation.strategy == StreamStrategy::PersistentMapped ) {
		dlight_.projectedShaderStreamPersistentUploads++;
	}
	if ( shaderInput.target == ProjectedDlightShaderTarget::WorldPacket ) {
		dlight_.projectedShaderStreamWorldUploads++;
	} else if ( shaderInput.target == ProjectedDlightShaderTarget::DynamicDraw ) {
		dlight_.projectedShaderStreamDynamicUploads++;
	}
	if ( rangeReady && resourceRange ) {
		resourceRange->valid = qtrue;
		resourceRange->authoritative = qtrue;
		resourceRange->buffer = reservation.buffer;
		resourceRange->offset = dlight_.projectedShaderStreamLastOffset;
		resourceRange->bytes = dlight_.projectedShaderStreamLastBytes;
	}
	return rangeReady;
}

qboolean RendererModule::StageProjectedDlightDynamicMdiCommand(
	const ProjectedDlightDynamicMdiPlan &mdiPlan,
	ProjectedDlightDynamicMdiCommandUpload *commandUpload )
{
	StreamReservation reservation {};
	ProjectedDlightDynamicMdiCommandUpload *ringUpload;
	unsigned int ringSlot;

	if ( commandUpload ) {
		*commandUpload = {};
	}
	dlight_.projectedShaderMdiAttempts++;
	if ( !mdiPlan.valid || !mdiPlan.commandReady || !mdiPlan.eligible ||
		mdiPlan.commandUploadPlan.bytes == 0 ) {
		dlight_.projectedShaderMdiCommandSkips++;
		return qfalse;
	}

	dlight_.projectedShaderMdiEligible++;
	ringSlot = 0u;
	ringUpload = GLX_Dlight_ReserveProjectedMdiCommandRingSlot( &dlight_,
		&ringSlot );
	if ( !ringUpload ) {
		dlight_.projectedShaderMdiCommandFailures++;
		dlight_.projectedShaderMdiCommandRingFailures++;
		return qfalse;
	}

	if ( !GLX_Stream_Reserve( &stream_, mdiPlan.commandUploadPlan.bytes,
			static_cast<size_t>( mdiPlan.commandUploadPlan.alignment ), &reservation ) ) {
		dlight_.projectedShaderMdiCommandFailures++;
		dlight_.projectedShaderMdiCommandRingFailures++;
		return qfalse;
	}

	dlight_.projectedShaderMdiCommandRecords[ringSlot] = mdiPlan.command;
	if ( !GLX_Stream_Upload( &stream_, &reservation,
			&dlight_.projectedShaderMdiCommandRecords[ringSlot],
			mdiPlan.commandUploadPlan.bytes ) ) {
		dlight_.projectedShaderMdiCommandFailures++;
		dlight_.projectedShaderMdiCommandRingFailures++;
		GLX_Stream_Commit( &stream_, &reservation );
		GLX_Stream_RecordDlightReservation( &stream_, reservation );
		return qfalse;
	}

	GLX_Stream_Commit( &stream_, &reservation );
	GLX_Stream_RecordDlightReservation( &stream_, reservation );
	dlight_.projectedShaderMdiCommandUploads++;
	dlight_.projectedShaderMdiRecords += mdiPlan.projectedRecordCount;
	dlight_.projectedShaderMdiIndexes += mdiPlan.indexCount;
	dlight_.projectedShaderMdiCommandBytes +=
		static_cast<unsigned long long>( mdiPlan.commandUploadPlan.bytes );
	dlight_.projectedShaderMdiLastOffset =
		static_cast<unsigned int>( reservation.offset > ~0u ? ~0u : reservation.offset );
	dlight_.projectedShaderMdiLastBytes =
		static_cast<unsigned int>( reservation.bytes > ~0u ? ~0u : reservation.bytes );
	ringUpload->valid = qtrue;
	ringUpload->buffer = reservation.buffer;
	ringUpload->offset = dlight_.projectedShaderMdiLastOffset;
	ringUpload->bytes = dlight_.projectedShaderMdiLastBytes;
	dlight_.projectedShaderMdiCommandRingCommits++;
	dlight_.projectedShaderMdiCommandRingLastSlot = ringSlot;
	dlight_.projectedShaderMdiCommandRingLastBuffer = ringUpload->buffer;
	dlight_.projectedShaderMdiCommandRingLastOffset = ringUpload->offset;
	dlight_.projectedShaderMdiCommandRingLastBytes = ringUpload->bytes;
	if ( commandUpload ) {
		*commandUpload = *ringUpload;
	}
	return qtrue;
}

qboolean RendererModule::BindStaticWorldProjectedDlightRun( const WorldPacket &packet,
	const ProjectedDlightShaderInput &shaderInput,
	ProjectedDlightShaderExecutionPlan *executionPlan )
{
	if ( executionPlan ) {
		*executionPlan = {};
	}
	if ( !GLX_Dlight_CurrentProgram( &dlight_ ) ) {
		return qfalse;
	}
	if ( !GLX_RenderIR_ValidateProjectedDlightShaderInput( shaderInput ) ||
		!shaderInput.programmable ||
		!GLX_RenderIR_TierSupportsWorldPacket( caps_.tier, packet ) ) {
		GLX_Dlight_ClearProjectedShaderInput( &dlight_ );
		return qfalse;
	}

	ProjectedDlightResourceRange resourceRange {};
	qboolean projectedStreamReady =
		StageProjectedDlightShaderInput( shaderInput, &resourceRange );
	if ( !GLX_Dlight_BindProjectedShaderInput( &dlight_, shaderInput,
			projectedStreamReady ? &resourceRange : nullptr, executionPlan ) ) {
		GLX_Dlight_ClearProjectedShaderInput( &dlight_ );
		return qfalse;
	}
	return qtrue;
}

void RendererModule::ConsumeStaticWorldProjectedDlightRun( const WorldPacket &packet,
	const ProjectedDlightShaderInput &shaderInput )
{
	if ( !GLX_RenderIR_ValidateProjectedDlightShaderInput( shaderInput ) ) {
		return;
	}

	if ( GLX_Executor_ConsumeWorldPacket( &executor_, packet ) ) {
		dlight_.projectedPacketIRProducts++;
		GLX_Dlight_RecordProjectedShaderInput( &dlight_, shaderInput );
	} else {
		dlight_.projectedPacketIRRejected++;
	}
}

void RendererModule::ConsumeStaticWorldProjectedDlightRun( int firstItem, int itemCount )
{
	WorldPacket packet {};
	ProjectedDlightShaderInput shaderInput {};

	if ( GLX_Dlight_ProjectedProgramEnabled( dlight_ ) &&
		!GLX_Dlight_CurrentProgram( &dlight_ ) ) {
		// Evidence-only consumption stays with the default-off path; once the
		// projected program is opted in, products must match overlay binds.
		return;
	}

	if ( PrepareStaticWorldProjectedDlightRun( firstItem, itemCount, &packet,
		&shaderInput ) ) {
		ConsumeStaticWorldProjectedDlightRun( packet, shaderInput );
	}
}

qboolean RendererModule::StaticWorldProjectedDlightSplitActive()
{
	return GLX_Dlight_CurrentProgram( &dlight_ ) &&
		GLX_Dlight_ProjectedProgramEnabled( dlight_ ) &&
		GLX_Dlight_ProjectedShaderProgrammable( dlight_, caps_.tier ) ? qtrue : qfalse;
}

int RendererModule::StaticWorldDrawProjectedDlightRunsFiltered( int runCount,
	const int *counts, const void *const *offsets, const int *firstItems,
	const int *itemCounts, int *drawnRuns, unsigned int indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound )
{
	int drawnCount = 0;

	if ( runCount <= 0 || !counts || !offsets || !firstItems || !itemCounts ||
		!drawnRuns || !StaticWorldProjectedDlightSplitActive() ) {
		return 0;
	}

	for ( int i = 0; i < runCount; i++ ) {
		WorldPacket projectedPacket {};
		ProjectedDlightShaderInput projectedShaderInput {};
		const int offsetBytes = static_cast<int>(
			reinterpret_cast<intptr_t>( offsets[i] ) );

		if ( drawnRuns[i] || counts[i] <= 0 ||
			!PrepareStaticWorldProjectedDlightRun( firstItems[i], itemCounts[i],
				&projectedPacket, &projectedShaderInput ) ) {
			continue;
		}

		if ( StaticWorldDrawDeviceRun( counts[i], offsetBytes, firstItems[i],
			itemCounts[i], indexType, indexBytes, shaderName, sort, arenaBound ) ) {
			drawnRuns[i] = 1;
			drawnCount++;
		}
	}

	return drawnCount;
}

qboolean RendererModule::StaticWorldDrawDeviceRun( int indexes, int offsetBytes,
	int firstItem, int itemCount, unsigned int indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound )
{
	WorldPacket projectedPacket {};
	ProjectedDlightShaderExecutionPlan projectedExecutionPlan {};
	ProjectedDlightShaderInput projectedShaderInput {};
	qboolean hasProjectedShaderInput = PrepareStaticWorldProjectedDlightRun(
		firstItem, itemCount, &projectedPacket, &projectedShaderInput );

	if ( hasProjectedShaderInput && GLX_Dlight_ProjectedProgramEnabled( dlight_ ) &&
		!GLX_Dlight_CurrentProgram( &dlight_ ) ) {
		// Base-pass device runs draw normally; projected binds and IR
		// consumption belong to the dedicated overlay pass that draws this
		// run again under the dlight program.
		hasProjectedShaderInput = qfalse;
	}

	if ( hasProjectedShaderInput ) {
		BindStaticWorldProjectedDlightRun( projectedPacket, projectedShaderInput,
			&projectedExecutionPlan );
		ProjectedDlightShaderReplacementPlan replacementPlan =
			GLX_RenderIR_PlanProjectedDlightShaderReplacement(
				projectedShaderInput,
				GLX_Dlight_ProjectedProgramEnabled( dlight_ ),
				GLX_RenderIR_TierSupportsWorldPacket( caps_.tier, projectedPacket ),
				projectedExecutionPlan );
		if ( replacementPlan.legacyFallback ) {
			GLX_Dlight_ClearProjectedShaderInput( &dlight_ );
			return qfalse;
		}
	} else if ( GLX_Dlight_CurrentProgram( &dlight_ ) ) {
		GLX_Dlight_ClearProjectedShaderInput( &dlight_ );
	}

	if ( GLX_StaticWorld_DrawDeviceRun( &staticWorld_, indexes, offsetBytes,
		firstItem, itemCount, static_cast<GLenum>( indexType ), indexBytes,
		shaderName, sort, arenaBound ) ) {
		if ( hasProjectedShaderInput ) {
			ConsumeStaticWorldProjectedDlightRun( projectedPacket, projectedShaderInput );
		}
		GLX_Profiler_RecordDraw( &profiler_, indexes, GLX_DRAW_VBO_DEVICE );
		return qtrue;
	}

	return qfalse;
}

qboolean RendererModule::StaticWorldDrawDeviceRuns( int runCount, const int *counts, const void *const *offsets,
	const int *firstItems, const int *itemCounts, unsigned int indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound )
{
	unsigned int totalIndexes = 0;

	if ( GLX_Dlight_CurrentProgram( &dlight_ ) &&
		GLX_Dlight_ProjectedProgramEnabled( dlight_ ) && counts && firstItems &&
		itemCounts ) {
		for ( int i = 0; i < runCount; i++ ) {
			WorldPacket projectedPacket {};
			ProjectedDlightShaderInput projectedShaderInput {};

			if ( counts[i] <= 0 ) {
				continue;
			}
			if ( PrepareStaticWorldProjectedDlightRun( firstItems[i],
				itemCounts[i], &projectedPacket, &projectedShaderInput ) ) {
				return qfalse;
			}
		}
	}

	if ( GLX_Dlight_CurrentProgram( &dlight_ ) ) {
		GLX_Dlight_ClearProjectedShaderInput( &dlight_ );
	}

	if ( GLX_StaticWorld_DrawDeviceRuns( &staticWorld_, runCount, counts, offsets, firstItems, itemCounts,
		static_cast<GLenum>( indexType ), indexBytes, shaderName, sort, arenaBound ) ) {
		for ( int i = 0; i < runCount; i++ ) {
			if ( counts && counts[i] > 0 ) {
				totalIndexes += static_cast<unsigned int>( counts[i] );
			}
			if ( firstItems && itemCounts ) {
				ConsumeStaticWorldProjectedDlightRun( firstItems[i], itemCounts[i] );
			}
		}
		GLX_Profiler_RecordDraw( &profiler_, static_cast<int>( totalIndexes ), GLX_DRAW_VBO_DEVICE );
		return qtrue;
	}

	return qfalse;
}

qboolean RendererModule::StaticWorldDrawSoftIndexes( int indexes, const void *indexData,
	unsigned int indexType, int indexBytes, const char *shaderName, int sort, qboolean arenaBound )
{
	if ( GLX_StaticWorld_DrawSoftIndexes( &staticWorld_, indexes, indexData,
		static_cast<GLenum>( indexType ), indexBytes, shaderName, sort, arenaBound ) ) {
		GLX_Profiler_RecordDraw( &profiler_, indexes, GLX_DRAW_VBO_SOFT );
		return qtrue;
	}

	return qfalse;
}

int RendererModule::StaticWorldDrawDeviceRunsFiltered( int runCount, const int *counts, const void *const *offsets,
	const int *firstItems, const int *itemCounts, int *drawnRuns, unsigned int indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound )
{
	int drawnIndexes = 0;
	int drawnRunsCount;

	drawnRunsCount = StaticWorldDrawProjectedDlightRunsFiltered( runCount, counts,
		offsets, firstItems, itemCounts, drawnRuns, indexType, indexBytes,
		shaderName, sort, arenaBound );
	if ( drawnRunsCount > 0 ) {
		return drawnRunsCount;
	}

	if ( GLX_Dlight_CurrentProgram( &dlight_ ) &&
		GLX_Dlight_ProjectedProgramEnabled( dlight_ ) && counts && firstItems &&
		itemCounts ) {
		for ( int i = 0; i < runCount; i++ ) {
			WorldPacket projectedPacket {};
			ProjectedDlightShaderInput projectedShaderInput {};

			if ( ( drawnRuns && drawnRuns[i] ) || counts[i] <= 0 ) {
				continue;
			}
			if ( PrepareStaticWorldProjectedDlightRun( firstItems[i],
				itemCounts[i], &projectedPacket, &projectedShaderInput ) ) {
				return 0;
			}
		}
	}

	if ( GLX_Dlight_CurrentProgram( &dlight_ ) ) {
		GLX_Dlight_ClearProjectedShaderInput( &dlight_ );
	}

	drawnRunsCount = GLX_StaticWorld_DrawDeviceRunsFiltered( &staticWorld_, runCount, counts, offsets,
		firstItems, itemCounts, drawnRuns, static_cast<GLenum>( indexType ), indexBytes,
		shaderName, sort, arenaBound );
	GLX_Debug_LabelObject( debug_, GL_BUFFER, staticWorld_.indirectCompactCommandBuffer,
		"GLx static world compact indirect commands" );
	if ( drawnRunsCount <= 0 ) {
		return 0;
	}

	for ( int i = 0; i < runCount; i++ ) {
		if ( drawnRuns && drawnRuns[i] && counts && counts[i] > 0 ) {
			drawnIndexes += counts[i];
			if ( firstItems && itemCounts ) {
				ConsumeStaticWorldProjectedDlightRun( firstItems[i], itemCounts[i] );
			}
		}
	}
	GLX_Profiler_RecordDraw( &profiler_, drawnIndexes, GLX_DRAW_VBO_DEVICE );
	return drawnRunsCount;
}

void RendererModule::RecordStaticWorldQueue( int queuedItems, int queuedVertexes, int queuedIndexes,
	int deviceRuns, int deviceIndexes, int softIndexes, int largestDeviceRunIndexes )
{
	GLX_StaticWorld_RecordQueue( &staticWorld_, queuedItems, queuedVertexes, queuedIndexes,
		deviceRuns, deviceIndexes, softIndexes, largestDeviceRunIndexes );
}

void RendererModule::RecordStaticWorldDeviceRuns( int runCount, const int *counts, const void *const *offsets,
	const int *firstItems, const int *itemCounts, int indexBytes, const char *shaderName, int sort )
{
	GLX_StaticWorld_RecordDeviceRuns( &staticWorld_, runCount, counts, offsets,
		firstItems, itemCounts, indexBytes, shaderName, sort );
}

} // namespace glx

extern "C" void GLX_Renderer_RegisterCommands( void )
{
	glx::g_module.RegisterCommands();
}

extern "C" void GLX_Renderer_RemoveCommands( void )
{
	glx::g_module.RemoveCommands();
}

extern "C" void GLX_Renderer_SetImports( refimport_t *imports )
{
	glx::g_imports = imports;
}

extern "C" void GLX_Renderer_OnOpenGLReady( const glconfig_t *config, const char *extensions )
{
	glx::g_module.OnOpenGLReady( config, extensions );
}

extern "C" void GLX_Renderer_Shutdown( int code )
{
	glx::g_module.Shutdown( code );
}

extern "C" void GLX_Renderer_BeginBackendTimer( void )
{
	glx::g_module.BeginBackendTimer();
}

extern "C" void GLX_Renderer_EndBackendTimer( void )
{
	glx::g_module.EndBackendTimer();
}

extern "C" void GLX_Renderer_BeginGpuPassTimer( int pass )
{
	glx::g_module.BeginGpuPassTimer( pass );
}

extern "C" void GLX_Renderer_EndGpuPassTimer( int pass )
{
	glx::g_module.EndGpuPassTimer( pass );
}

extern "C" void GLX_Renderer_FrameComplete( void )
{
	glx::g_module.FrameComplete();
}

extern "C" void GLX_Renderer_PrintCaps_f( void )
{
	glx::g_module.PrintCaps();
}

extern "C" void GLX_Renderer_PrintInfo_f( void )
{
	glx::g_module.PrintInfo();
}

extern "C" void GLX_Renderer_Profile_f( void )
{
	glx::g_module.ProfileCommand();
}

extern "C" void GLX_Renderer_Material_f( void )
{
	glx::g_module.PrintMaterial();
}

extern "C" void GLX_Renderer_PostProcess_f( void )
{
	glx::g_module.PrintPostProcess();
}

extern "C" void GLX_Renderer_StaticWorld_f( void )
{
	glx::g_module.PrintStaticWorld();
}

extern "C" void GLX_Renderer_StreamTest_f( void )
{
	glx::g_module.StreamTest();
}

extern "C" void GLX_Renderer_PrintFrameCounters( void )
{
	glx::g_module.PrintFrameCounters();
}

extern "C" qboolean GLX_Renderer_DrawElements( unsigned int mode, int count,
	unsigned int type, const void *indices, int legacyReason, int profilerPath )
{
	return glx::g_module.DrawElements( mode, count, type, indices, legacyReason, profilerPath );
}

extern "C" qboolean GLX_Renderer_DrawArrays( unsigned int mode, int first, int count,
	int legacyReason, int profilerPath )
{
	return glx::g_module.DrawArrays( mode, first, count, legacyReason, profilerPath );
}

extern "C" qboolean GLX_Renderer_DrawElementsClassified( unsigned int mode, int count,
	unsigned int type, const void *indices, int legacyReason, int profilerPath,
	int materialFlags, unsigned int categoryMask )
{
	return glx::g_module.DrawElementsClassified( mode, count, type, indices, legacyReason,
		profilerPath, materialFlags, categoryMask );
}

extern "C" qboolean GLX_Renderer_DrawElementsClassifiedProjectedDlights(
	unsigned int mode, int count, unsigned int type, const void *indices,
	int legacyReason, int profilerPath, int materialFlags, unsigned int categoryMask,
	unsigned int lightMask )
{
	return glx::g_module.DrawElementsClassifiedProjectedDlights( mode, count, type,
		indices, legacyReason, profilerPath, materialFlags, categoryMask, lightMask );
}

extern "C" qboolean GLX_Renderer_DrawArraysClassified( unsigned int mode, int first, int count,
	int legacyReason, int profilerPath, int materialFlags, unsigned int categoryMask )
{
	return glx::g_module.DrawArraysClassified( mode, first, count, legacyReason, profilerPath,
		materialFlags, categoryMask );
}

extern "C" void GLX_Renderer_RecordDraw( int indexes, int path )
{
	glx::g_module.RecordDraw( indexes, path );
}

extern "C" void GLX_Renderer_RecordShaderBatch( const char *shaderName, int sort, int numPasses,
	int numVertexes, int numIndexes, int flags )
{
	glx::g_module.RecordShaderBatch( shaderName, sort, numPasses, numVertexes, numIndexes, flags );
}

extern "C" void GLX_Renderer_RecordMaterialStage( int path, int flags, unsigned int stateBits,
	int rgbGen, int alphaGen, int tcGen0, int tcGen1, int texMods0, int texMods1,
	unsigned int texModTypes0, unsigned int texModTypes1,
	unsigned int texModSequence0, unsigned int texModSequence1,
	int rgbWaveFunc, int alphaWaveFunc,
	unsigned int texModWaveFuncs0, unsigned int texModWaveFuncs1,
	int fogAdjust, int materialCombine, qboolean fogPass,
	int numVertexes, int numIndexes )
{
	glx::g_module.RecordMaterialStage( path, flags, stateBits, rgbGen, alphaGen,
		tcGen0, tcGen1, texMods0, texMods1, texModTypes0, texModTypes1,
		texModSequence0, texModSequence1, rgbWaveFunc, alphaWaveFunc,
		texModWaveFuncs0, texModWaveFuncs1, fogAdjust, materialCombine, fogPass,
		numVertexes, numIndexes );
}

extern "C" qboolean GLX_Renderer_MaterialRendererActive( void )
{
	return glx::g_module.MaterialRendererActive();
}

extern "C" qboolean GLX_Renderer_BindMaterialStage( int flags, unsigned int stateBits,
	int rgbGen, int alphaGen, int tcGen0, int tcGen1, int texMods0, int texMods1,
	unsigned int texModTypes0, unsigned int texModTypes1,
	unsigned int texModSequence0, unsigned int texModSequence1,
	int rgbWaveFunc, int alphaWaveFunc,
	unsigned int texModWaveFuncs0, unsigned int texModWaveFuncs1, int fogAdjust,
	int materialCombine, qboolean fogPass )
{
	return glx::g_module.BindMaterialStage( flags, stateBits, rgbGen, alphaGen,
		tcGen0, tcGen1, texMods0, texMods1, texModTypes0, texModTypes1,
		texModSequence0, texModSequence1, rgbWaveFunc, alphaWaveFunc,
		texModWaveFuncs0, texModWaveFuncs1, fogAdjust,
		materialCombine, fogPass );
}

extern "C" qboolean GLX_Renderer_BindFogMaterial( void )
{
	return glx::g_module.BindFogMaterial();
}

extern "C" qboolean GLX_Renderer_BindLiquidMaterial( const float *params,
	const float *eyeAndCount, const float *targetInverse, const float *reflect,
	const float *impulses, const float *amplitudes )
{
	return glx::g_module.BindLiquidMaterial( params, eyeAndCount, targetInverse,
		reflect, impulses, amplitudes );
}

extern "C" void GLX_Renderer_UnbindMaterial( void )
{
	glx::g_module.UnbindMaterial();
}

extern "C" qboolean GLX_Renderer_DlightProgramAvailable( qboolean linear,
	int fogMode, qboolean absLight, qboolean shadow )
{
	return glx::g_module.DlightProgramAvailable( linear, fogMode, absLight, shadow );
}

extern "C" qboolean GLX_Renderer_BindDlightProgram( qboolean linear,
	int fogMode, qboolean absLight, qboolean shadow, const float *eyePos, const float *lightPos,
	const float *lightColor, const float *lightVector, const float *texFactors,
	const float *dlightFactors, const float *fogDistanceVector,
	const float *fogDepthVector, float fogEyeT, const float *dlightShadow,
	const float *shadowAtlas, const float *shadowDepth, const float *shadowFilter )
{
	return glx::g_module.BindDlightProgram( linear, fogMode, absLight, shadow,
		eyePos, lightPos, lightColor, lightVector, texFactors, dlightFactors,
		fogDistanceVector, fogDepthVector, fogEyeT,
		dlightShadow, shadowAtlas, shadowDepth, shadowFilter );
}

extern "C" void GLX_Renderer_UnbindDlightProgram( void )
{
	glx::g_module.UnbindDlightProgram();
}

extern "C" qboolean GLX_Renderer_DlightProjectedProgramEnabled( void )
{
	return glx::g_module.DlightProjectedProgramEnabled();
}

extern "C" qboolean GLX_Renderer_ProjectedDlightWorldOverlayActive( void )
{
	return glx::g_module.ProjectedDlightWorldOverlayActive();
}

extern "C" qboolean GLX_Renderer_BindProjectedDlightOverlayProgram( void )
{
	return glx::g_module.BindProjectedDlightOverlayProgram();
}

extern "C" qboolean GLX_Renderer_DlightScissorEnabled( void )
{
	return glx::g_module.DlightScissorEnabled();
}

extern "C" void GLX_Renderer_RecordDlightState( int event )
{
	glx::g_module.RecordDlightState( event );
}

extern "C" void GLX_Renderer_RecordDlightBuild( int legacyLights,
	int legacySkippedLights, int legacyNoHitLights, int legacyVertexes,
	int legacyIndexes, int legacyLitIndexes, int pmPasses, int pmVertexes,
	int pmIndexes )
{
	glx::g_module.RecordDlightBuild( legacyLights, legacySkippedLights,
		legacyNoHitLights, legacyVertexes, legacyIndexes, legacyLitIndexes,
		pmPasses, pmVertexes, pmIndexes );
}

extern "C" void GLX_Renderer_RecordDlightCull( int legacyVertexes,
	int legacyIndexes )
{
	glx::g_module.RecordDlightCull( legacyVertexes, legacyIndexes );
}

extern "C" void GLX_Renderer_RecordDlightScissor( qboolean computed,
	qboolean applied, int x, int y, int width, int height, int viewportWidth,
	int viewportHeight )
{
	glx::g_module.RecordDlightScissor( computed, applied, x, y, width, height,
		viewportWidth, viewportHeight );
}

extern "C" void GLX_Renderer_RecordProjectedDlights(
	const glxProjectedDlightRecord_t *records, int count )
{
	glx::g_module.RecordProjectedDlights( records, count );
}

extern "C" void GLX_Renderer_RecordProjectedDlightList( int itemIndex,
	unsigned int lightMask )
{
	glx::g_module.RecordProjectedDlightList( itemIndex, lightMask );
}

extern "C" qboolean GLX_Renderer_StreamDrawEnabled( void )
{
	return glx::g_module.StreamDrawEnabled();
}

extern "C" qboolean GLX_Renderer_StreamDrawMultitextureEnabled( void )
{
	return glx::g_module.StreamDrawMultitextureEnabled();
}

extern "C" qboolean GLX_Renderer_StreamDrawFogEnabled( void )
{
	return glx::g_module.StreamDrawFogEnabled();
}

extern "C" qboolean GLX_Renderer_StreamDrawDepthFragmentEnabled( void )
{
	return glx::g_module.StreamDrawDepthFragmentEnabled();
}

extern "C" qboolean GLX_Renderer_StreamDrawShadowsEnabled( void )
{
	return glx::g_module.StreamDrawShadowsEnabled();
}

extern "C" qboolean GLX_Renderer_StreamDrawBeamsEnabled( void )
{
	return glx::g_module.StreamDrawBeamsEnabled();
}

extern "C" qboolean GLX_Renderer_StreamDrawPostProcessEnabled( void )
{
	return glx::g_module.StreamDrawPostProcessEnabled();
}

extern "C" qboolean GLX_Renderer_StreamDrawAllowsMaterial( int flags, unsigned int stateBits,
	int rgbGen, int alphaGen, int tcGen0, int tcGen1, int texMods0, int texMods1,
	unsigned int texModTypes0, unsigned int texModTypes1,
	unsigned int texModSequence0, unsigned int texModSequence1,
	int rgbWaveFunc, int alphaWaveFunc,
	unsigned int texModWaveFuncs0, unsigned int texModWaveFuncs1,
	int fogAdjust, int materialCombine, qboolean fogPass )
{
	return glx::g_module.StreamDrawAllowsMaterial( flags, stateBits, rgbGen, alphaGen,
		tcGen0, tcGen1, texMods0, texMods1, texModTypes0, texModTypes1,
		texModSequence0, texModSequence1, rgbWaveFunc, alphaWaveFunc,
		texModWaveFuncs0, texModWaveFuncs1, fogAdjust, materialCombine, fogPass );
}

extern "C" qboolean GLX_Renderer_StreamReserve( int bytes, int alignment, glxStreamReservation_t *reservation )
{
	return glx::g_module.StreamReserve( bytes, alignment, reservation );
}

extern "C" qboolean GLX_Renderer_StreamUploadAt( glxStreamReservation_t *reservation, int relativeOffset,
	const void *data, int bytes )
{
	return glx::g_module.StreamUploadAt( reservation, relativeOffset, data, bytes );
}

extern "C" void GLX_Renderer_StreamCommit( glxStreamReservation_t *reservation )
{
	glx::g_module.StreamCommit( reservation );
}

extern "C" void GLX_Renderer_RecordStreamDlightReservation( const glxStreamReservation_t *reservation )
{
	glx::g_module.RecordStreamDlightReservation( reservation );
}

extern "C" unsigned int GLX_Renderer_BindStreamArrayBuffer( unsigned int buffer )
{
	return glx::g_module.BindStreamArrayBuffer( static_cast<GLuint>( buffer ) );
}

extern "C" void GLX_Renderer_RestoreStreamArrayBuffer( unsigned int buffer )
{
	glx::g_module.RestoreStreamArrayBuffer( static_cast<GLuint>( buffer ) );
}

extern "C" unsigned int GLX_Renderer_BindStreamElementArrayBuffer( unsigned int buffer )
{
	return glx::g_module.BindStreamElementArrayBuffer( static_cast<GLuint>( buffer ) );
}

extern "C" void GLX_Renderer_RestoreStreamElementArrayBuffer( unsigned int buffer )
{
	glx::g_module.RestoreStreamElementArrayBuffer( static_cast<GLuint>( buffer ) );
}

extern "C" void GLX_Renderer_RecordStreamBufferBind( unsigned int target, unsigned int buffer )
{
	glx::g_module.RecordStreamBufferBind( target, static_cast<GLuint>( buffer ) );
}

extern "C" void GLX_Renderer_RecordStreamDrawResult( int numVertexes, int numIndexes,
	int totalBytes, int indexBytes, int texcoord1Bytes, qboolean multitexture, qboolean fog,
	qboolean depthFragment, int materialFlags, unsigned int categoryMask, qboolean success )
{
	glx::g_module.RecordStreamDrawResult( numVertexes, numIndexes, totalBytes, indexBytes,
		texcoord1Bytes, multitexture, fog, depthFragment, materialFlags, categoryMask, success );
}

extern "C" void GLX_Renderer_RecordStreamDrawSkip( int reason )
{
	glx::g_module.RecordStreamDrawSkip( reason );
}

extern "C" void GLX_Renderer_ResetImageColorAudit( void )
{
	glx::g_module.ResetImageColorAudit();
}

extern "C" void GLX_Renderer_RecordImageColorAudit( int colorSpace, qboolean srgbDecode )
{
	glx::g_module.RecordImageColorAudit( colorSpace, srgbDecode );
}

extern "C" void GLX_Renderer_RecordFboInit( qboolean requested, qboolean ready,
	qboolean programReady, qboolean framebufferFnsReady, int vidWidth, int vidHeight,
	int captureWidth, int captureHeight, int windowWidth, int windowHeight,
	int internalFormat, int textureFormat, int textureType, qboolean multiSampled,
	qboolean superSampled, qboolean windowAdjusted, int blitFilter, int hdrMode,
	int renderScaleMode, int bloomMode, qboolean textureSrgbAvailable,
	qboolean framebufferSrgbAvailable, qboolean framebufferSrgbEnabled )
{
	glx::g_module.RecordFboInit( requested, ready, programReady, framebufferFnsReady,
		vidWidth, vidHeight, captureWidth, captureHeight, windowWidth, windowHeight,
		internalFormat, textureFormat, textureType, multiSampled, superSampled,
		windowAdjusted, blitFilter, hdrMode, renderScaleMode, bloomMode,
		textureSrgbAvailable, framebufferSrgbAvailable, framebufferSrgbEnabled );
}

extern "C" void GLX_Renderer_RecordFboShutdown( void )
{
	glx::g_module.RecordFboShutdown();
}

extern "C" void GLX_Renderer_RecordPostProcessFrame( qboolean minimized, qboolean bloomAvailable,
	qboolean programReady, int screenshotMask, qboolean windowAdjusted, int fboReadIndex,
	int hdrMode, int renderScaleMode, float greyscale, float legacyGamma,
	float legacyOverbright )
{
	glx::g_module.RecordPostProcessFrame( minimized, bloomAvailable, programReady,
		screenshotMask, windowAdjusted, fboReadIndex, hdrMode, renderScaleMode, greyscale,
		legacyGamma, legacyOverbright );
}

extern "C" qboolean GLX_Renderer_AutoExposureNeedsSamples( int *width, int *height )
{
	return glx::g_module.AutoExposureNeedsSamples( width, height );
}

extern "C" float GLX_Renderer_UpdateAutoExposure( float manualExposure,
	const float *rgba, int width, int height )
{
	return glx::g_module.UpdateAutoExposure( manualExposure, rgba, width, height );
}

extern "C" qboolean GLX_Renderer_TryBindPostShaderDirectFinal( void )
{
	return glx::g_module.TryBindPostShaderDirectFinal();
}

extern "C" qboolean GLX_Renderer_TryBindPostShaderFinal( qboolean bloomComposite,
	qboolean outputTransform, float bloomIntensity )
{
	return glx::g_module.TryBindPostShaderFinal( bloomComposite, outputTransform,
		bloomIntensity );
}

extern "C" void GLX_Renderer_UnbindPostShader( void )
{
	glx::g_module.UnbindPostShader();
}

extern "C" void GLX_Renderer_RecordPostProcessResult( int result )
{
	glx::g_module.RecordPostProcessResult( result );
}

extern "C" void GLX_Renderer_RecordColorGradeLut( qboolean active, int size, float scale )
{
	glx::g_module.RecordColorGradeLut( active, size, scale );
}

extern "C" void GLX_Renderer_RecordBloomCreate( int result, int requestedPasses,
	int effectivePasses, int textureUnits, int formatMode, int internalFormat,
	int textureFormat, int textureType )
{
	glx::g_module.RecordBloomCreate( result, requestedPasses, effectivePasses,
		textureUnits, formatMode, internalFormat, textureFormat, textureType );
}

extern "C" void GLX_Renderer_RecordBloom( int result, qboolean finalStage, int bloomMode,
	int requestedPasses, int effectivePasses, int blendBase, int filterSize,
	int textureUnits, int thresholdMode, int modulate, float threshold, float intensity,
	float reflection )
{
	glx::g_module.RecordBloom( result, finalStage, bloomMode, requestedPasses,
		effectivePasses, blendBase, filterSize, textureUnits, thresholdMode,
		modulate, threshold, intensity, reflection );
}

extern "C" void GLX_Renderer_RecordFboCopyScreen( int viewportWidth, int viewportHeight )
{
	glx::g_module.RecordFboCopyScreen( viewportWidth, viewportHeight );
}

extern "C" void GLX_Renderer_RecordFboBlit( int kind, qboolean depthOnly,
	int srcWidth, int srcHeight, int dstWidth, int dstHeight )
{
	glx::g_module.RecordFboBlit( kind, depthOnly, srcWidth, srcHeight, dstWidth, dstHeight );
}

extern "C" void GLX_Renderer_RecordFboBind( void )
{
	glx::g_module.RecordFboBind();
}

extern "C" void GLX_Renderer_RecordPostClear( void )
{
	glx::g_module.RecordPostClear();
}

extern "C" void GLX_Renderer_RecordFullscreenPass( void )
{
	glx::g_module.RecordFullscreenPass();
}

extern "C" void GLX_Renderer_PushShaderDebugGroup( const char *shaderName, int numVertexes, int numIndexes, int numPasses )
{
	glx::g_module.PushShaderDebugGroup( shaderName, numVertexes, numIndexes, numPasses );
}

extern "C" void GLX_Renderer_PopDebugGroup( void )
{
	glx::g_module.PopDebugGroup();
}

extern "C" void GLX_Renderer_ShadowUploadTess( int numVertexes, int numIndexes,
	const void *xyz, int xyzBytes, const void *indexes, int indexBytes )
{
	glx::g_module.ShadowUploadTess( numVertexes, numIndexes, xyz, xyzBytes, indexes, indexBytes );
}

extern "C" void GLX_Renderer_RecordStaticWorldCache( int surfaces, int vertexes, int indexes, int vertexBytes, int indexBytes )
{
	glx::g_module.RecordStaticWorldCache( surfaces, vertexes, indexes, vertexBytes, indexBytes );
}

extern "C" void GLX_Renderer_RecordStaticWorldBatches( int batches, int largestBatchSurfaces,
	int faceSurfaces, int gridSurfaces, int triangleSurfaces, int shaderStagePasses, int maxShaderStages )
{
	glx::g_module.RecordStaticWorldBatches( batches, largestBatchSurfaces, faceSurfaces,
		gridSurfaces, triangleSurfaces, shaderStagePasses, maxShaderStages );
}

extern "C" void GLX_Renderer_RecordStaticWorldPacket( const char *shaderName, int sort,
	int surfaces, int vertexes, int indexes, int firstItem, int itemCount, int vertexOffset, int vertexBytes,
	int indexOffset, int indexBytes, int shaderStagePasses, int flags )
{
	glx::g_module.RecordStaticWorldPacket( shaderName, sort, surfaces, vertexes, indexes,
		firstItem, itemCount, vertexOffset, vertexBytes, indexOffset, indexBytes, shaderStagePasses, flags );
}

extern "C" void GLX_Renderer_UploadStaticWorldArena( const void *vertexData, int vertexBytes,
	const void *indexData, int indexBytes )
{
	glx::g_module.UploadStaticWorldArena( vertexData, vertexBytes, indexData, indexBytes );
}

extern "C" unsigned int GLX_Renderer_StaticWorldArenaVertexBuffer( void )
{
	return glx::g_module.StaticWorldArenaVertexBuffer();
}

extern "C" unsigned int GLX_Renderer_StaticWorldArenaIndexBuffer( void )
{
	return glx::g_module.StaticWorldArenaIndexBuffer();
}

extern "C" qboolean GLX_Renderer_StaticWorldDrawDeviceRun( int indexes, int offsetBytes,
	int firstItem, int itemCount, unsigned int indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound )
{
	return glx::g_module.StaticWorldDrawDeviceRun( indexes, offsetBytes,
		firstItem, itemCount, indexType, indexBytes, shaderName, sort, arenaBound );
}

extern "C" qboolean GLX_Renderer_StaticWorldDrawDeviceRuns( int runCount, const int *counts,
	const void *const *offsets, const int *firstItems, const int *itemCounts,
	unsigned int indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound )
{
	return glx::g_module.StaticWorldDrawDeviceRuns( runCount, counts, offsets, firstItems, itemCounts,
		indexType, indexBytes, shaderName, sort, arenaBound );
}

extern "C" qboolean GLX_Renderer_StaticWorldDrawSoftIndexes( int indexes, const void *indexData,
	unsigned int indexType, int indexBytes, const char *shaderName, int sort, qboolean arenaBound )
{
	return glx::g_module.StaticWorldDrawSoftIndexes( indexes, indexData,
		indexType, indexBytes, shaderName, sort, arenaBound );
}

extern "C" int GLX_Renderer_StaticWorldDrawDeviceRunsFiltered( int runCount, const int *counts,
	const void *const *offsets, const int *firstItems, const int *itemCounts,
	int *drawnRuns, unsigned int indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound )
{
	return glx::g_module.StaticWorldDrawDeviceRunsFiltered( runCount, counts, offsets,
		firstItems, itemCounts, drawnRuns, indexType, indexBytes, shaderName, sort, arenaBound );
}

extern "C" int GLX_Renderer_StaticWorldDrawProjectedDlightRuns( int runCount, const int *counts,
	const void *const *offsets, const int *firstItems, const int *itemCounts,
	int *drawnRuns, unsigned int indexType, int indexBytes,
	const char *shaderName, int sort, qboolean arenaBound )
{
	return glx::g_module.StaticWorldDrawProjectedDlightRunsFiltered( runCount, counts, offsets,
		firstItems, itemCounts, drawnRuns, indexType, indexBytes, shaderName, sort, arenaBound );
}

extern "C" void GLX_Renderer_RecordStaticWorldQueue( int queuedItems, int queuedVertexes, int queuedIndexes,
	int deviceRuns, int deviceIndexes, int softIndexes, int largestDeviceRunIndexes )
{
	glx::g_module.RecordStaticWorldQueue( queuedItems, queuedVertexes, queuedIndexes,
		deviceRuns, deviceIndexes, softIndexes, largestDeviceRunIndexes );
}

extern "C" void GLX_Renderer_RecordStaticWorldDeviceRuns( int runCount, const int *counts,
	const void *const *offsets, const int *firstItems, const int *itemCounts,
	int indexBytes, const char *shaderName, int sort )
{
	glx::g_module.RecordStaticWorldDeviceRuns( runCount, counts, offsets,
		firstItems, itemCounts, indexBytes, shaderName, sort );
}
