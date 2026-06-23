#include "glx_material.h"

#include <cstdarg>
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
#ifndef GL_SHADING_LANGUAGE_VERSION
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#endif
#ifndef GL_PROGRAM
#define GL_PROGRAM 0x82E2
#endif

namespace glx {

typedef const GLubyte *( APIENTRY *PFNGLXGETSTRINGPROC )( GLenum name );

static constexpr MaterialProgramMode kMaterialPrecacheModes[] = {
	MaterialProgramMode::SingleTexture,
	MaterialProgramMode::MultiModulate,
	MaterialProgramMode::MultiAdd,
	MaterialProgramMode::MultiReplace,
	MaterialProgramMode::MultiDecal,
	MaterialProgramMode::Fog
};

static constexpr unsigned int kMaterialPrecacheFeatures[] = {
	GLX_MATERIAL_FEATURE_NONE,
	GLX_MATERIAL_FEATURE_TEXMOD,
	GLX_MATERIAL_FEATURE_ENVIRONMENT,
	GLX_MATERIAL_FEATURE_TEXMOD | GLX_MATERIAL_FEATURE_ENVIRONMENT,
	GLX_MATERIAL_FEATURE_DEPTH_FRAGMENT,
	GLX_MATERIAL_FEATURE_DEPTH_FRAGMENT | GLX_MATERIAL_FEATURE_TEXMOD,
	GLX_MATERIAL_FEATURE_DEPTH_FRAGMENT | GLX_MATERIAL_FEATURE_ENVIRONMENT,
	GLX_MATERIAL_FEATURE_DEPTH_FRAGMENT | GLX_MATERIAL_FEATURE_TEXMOD |
		GLX_MATERIAL_FEATURE_ENVIRONMENT
};

static void GLX_Material_FeatureName( unsigned int features, char *out, size_t outSize )
{
	char text[64] = "";

	if ( !out || outSize == 0 ) {
		return;
	}

	if ( features == GLX_MATERIAL_FEATURE_NONE ) {
		std::snprintf( out, outSize, "base" );
		out[outSize - 1] = '\0';
		return;
	}

	if ( features & GLX_MATERIAL_FEATURE_TEXMOD ) {
		std::snprintf( text + std::strlen( text ), sizeof( text ) - std::strlen( text ), "%stexmod",
			text[0] ? "+" : "" );
	}
	if ( features & GLX_MATERIAL_FEATURE_ENVIRONMENT ) {
		std::snprintf( text + std::strlen( text ), sizeof( text ) - std::strlen( text ), "%senvironment",
			text[0] ? "+" : "" );
	}
	if ( features & GLX_MATERIAL_FEATURE_DEPTH_FRAGMENT ) {
		std::snprintf( text + std::strlen( text ), sizeof( text ) - std::strlen( text ), "%sdepth-fragment",
			text[0] ? "+" : "" );
	}

	std::snprintf( out, outSize, "%s", text[0] ? text : "unknown" );
	out[outSize - 1] = '\0';
}

static void GLX_Material_KeyName( const MaterialProgramKey &key, char *out, size_t outSize )
{
	char features[64];

	if ( !out || outSize == 0 ) {
		return;
	}

	GLX_Material_FeatureName( key.features, features, sizeof( features ) );
	std::snprintf( out, outSize, "%s/%s", GLX_Material_ModeName( key.mode ), features );
	out[outSize - 1] = '\0';
}

static qboolean GLX_Material_AppendSource( char *out, size_t outSize, size_t *used,
	const char *format, ... )
{
	va_list args;
	int written;

	if ( !out || outSize == 0 || !used || !format || *used >= outSize ) {
		return qfalse;
	}

	va_start( args, format );
	written = std::vsnprintf( out + *used, outSize - *used, format, args );
	va_end( args );

	if ( written < 0 || static_cast<size_t>( written ) >= outSize - *used ) {
		out[outSize - 1] = '\0';
		return qfalse;
	}

	*used += static_cast<size_t>( written );
	return qtrue;
}

static void GLX_Material_StageKeyName( const MaterialStageKey &stageKey, char *out, size_t outSize )
{
	char compact[64];

	if ( !out || outSize == 0 ) {
		return;
	}

	GLX_Material_KeyName( stageKey.program, compact, sizeof( compact ) );
	std::snprintf( out, outSize,
		"%s flags%x state%x rgb%i:%i alpha%i:%i tc%i/%i tm%i:%x:%x/%i:%x:%x twf%x/%x fogadj%i fog%i",
		compact, stageKey.flags, stageKey.stateBits, stageKey.rgbGen,
		stageKey.rgbWaveFunc, stageKey.alphaGen, stageKey.alphaWaveFunc,
		stageKey.tcGen0, stageKey.tcGen1, stageKey.texMods0, stageKey.texModTypes0,
		stageKey.texModSequence0, stageKey.texMods1, stageKey.texModTypes1,
		stageKey.texModSequence1, stageKey.texModWaveFuncs0, stageKey.texModWaveFuncs1,
		stageKey.fogAdjust, stageKey.fogPass ? 1 : 0 );
	out[outSize - 1] = '\0';
}

static qboolean GLX_Material_StageLanguageDefines( const MaterialStageKey &stageKey,
	char *out, size_t outSize )
{
	size_t used = 0;
	const unsigned int srcBlend = stageKey.stateBits & GLX_MATERIAL_STATE_SRCBLEND_BITS;
	const unsigned int dstBlend = stageKey.stateBits & GLX_MATERIAL_STATE_DSTBLEND_BITS;
	const unsigned int alphaTest = stageKey.stateBits & GLX_MATERIAL_STATE_ATEST_BITS;

	if ( !out || outSize == 0 ) {
		return qfalse;
	}

	out[0] = '\0';
	const bool appended = GLX_Material_AppendSource( out, outSize, &used,
		"#define GLX_MATERIAL_MODE %i\n"
		"#define GLX_MATERIAL_FEATURE_TEXMOD %i\n"
		"#define GLX_MATERIAL_FEATURE_ENVIRONMENT %i\n"
		"#define GLX_MATERIAL_FEATURE_DEPTH_FRAGMENT %i\n"
		"#define GLX_MATERIAL_STAGE_FLAGS %i\n"
		"#define GLX_MATERIAL_STATE_BITS %u\n"
		"#define GLX_MATERIAL_STATE_UNKNOWN_BITS %u\n"
		"#define GLX_MATERIAL_STATE_SRCBLEND_BITS %u\n"
		"#define GLX_MATERIAL_STATE_DSTBLEND_BITS %u\n"
		"#define GLX_MATERIAL_STATE_ATEST_BITS %u\n"
		"#define GLX_MATERIAL_STATE_DEPTHMASK_TRUE %i\n"
		"#define GLX_MATERIAL_STATE_POLYMODE_LINE %i\n"
		"#define GLX_MATERIAL_STATE_DEPTHTEST_DISABLE %i\n"
		"#define GLX_MATERIAL_STATE_DEPTHFUNC_EQUAL %i\n"
		"#define GLX_MATERIAL_SRCBLEND_ZERO %i\n"
		"#define GLX_MATERIAL_SRCBLEND_ONE %i\n"
		"#define GLX_MATERIAL_SRCBLEND_DST_COLOR %i\n"
		"#define GLX_MATERIAL_SRCBLEND_ONE_MINUS_DST_COLOR %i\n"
		"#define GLX_MATERIAL_SRCBLEND_SRC_ALPHA %i\n"
		"#define GLX_MATERIAL_SRCBLEND_ONE_MINUS_SRC_ALPHA %i\n"
		"#define GLX_MATERIAL_SRCBLEND_DST_ALPHA %i\n"
		"#define GLX_MATERIAL_SRCBLEND_ONE_MINUS_DST_ALPHA %i\n"
		"#define GLX_MATERIAL_SRCBLEND_ALPHA_SATURATE %i\n"
		"#define GLX_MATERIAL_DSTBLEND_ZERO %i\n"
		"#define GLX_MATERIAL_DSTBLEND_ONE %i\n"
		"#define GLX_MATERIAL_DSTBLEND_SRC_COLOR %i\n"
		"#define GLX_MATERIAL_DSTBLEND_ONE_MINUS_SRC_COLOR %i\n"
		"#define GLX_MATERIAL_DSTBLEND_SRC_ALPHA %i\n"
		"#define GLX_MATERIAL_DSTBLEND_ONE_MINUS_SRC_ALPHA %i\n"
		"#define GLX_MATERIAL_DSTBLEND_DST_ALPHA %i\n"
		"#define GLX_MATERIAL_DSTBLEND_ONE_MINUS_DST_ALPHA %i\n"
		"#define GLX_MATERIAL_ATEST_GT_0 %i\n"
		"#define GLX_MATERIAL_ATEST_LT_80 %i\n"
		"#define GLX_MATERIAL_ATEST_GE_80 %i\n"
		"#define GLX_MATERIAL_RGBGEN %i\n"
		"#define GLX_MATERIAL_ALPHAGEN %i\n"
		"#define GLX_MATERIAL_RGB_WAVEFUNC %i\n"
		"#define GLX_MATERIAL_ALPHA_WAVEFUNC %i\n"
		"#define GLX_MATERIAL_TCGEN0 %i\n"
		"#define GLX_MATERIAL_TCGEN1 %i\n"
		"#define GLX_MATERIAL_TEXMODS0 %i\n"
		"#define GLX_MATERIAL_TEXMODS1 %i\n"
		"#define GLX_MATERIAL_TEXMOD_TYPES0 %u\n"
		"#define GLX_MATERIAL_TEXMOD_TYPES1 %u\n",
		static_cast<int>( stageKey.program.mode ),
		( stageKey.program.features & GLX_MATERIAL_FEATURE_TEXMOD ) ? 1 : 0,
		( stageKey.program.features & GLX_MATERIAL_FEATURE_ENVIRONMENT ) ? 1 : 0,
		( stageKey.program.features & GLX_MATERIAL_FEATURE_DEPTH_FRAGMENT ) ? 1 : 0,
		stageKey.flags, stageKey.stateBits,
		GLX_Material_StateUnknownBits( stageKey.stateBits ),
		srcBlend, dstBlend, alphaTest,
		( stageKey.stateBits & GLX_MATERIAL_STATE_DEPTHMASK_TRUE ) ? 1 : 0,
		( stageKey.stateBits & GLX_MATERIAL_STATE_POLYMODE_LINE ) ? 1 : 0,
		( stageKey.stateBits & GLX_MATERIAL_STATE_DEPTHTEST_DISABLE ) ? 1 : 0,
		( stageKey.stateBits & GLX_MATERIAL_STATE_DEPTHFUNC_EQUAL ) ? 1 : 0,
		srcBlend == GLX_MATERIAL_STATE_SRCBLEND_ZERO ? 1 : 0,
		srcBlend == GLX_MATERIAL_STATE_SRCBLEND_ONE ? 1 : 0,
		srcBlend == GLX_MATERIAL_STATE_SRCBLEND_DST_COLOR ? 1 : 0,
		srcBlend == GLX_MATERIAL_STATE_SRCBLEND_ONE_MINUS_DST_COLOR ? 1 : 0,
		srcBlend == GLX_MATERIAL_STATE_SRCBLEND_SRC_ALPHA ? 1 : 0,
		srcBlend == GLX_MATERIAL_STATE_SRCBLEND_ONE_MINUS_SRC_ALPHA ? 1 : 0,
		srcBlend == GLX_MATERIAL_STATE_SRCBLEND_DST_ALPHA ? 1 : 0,
		srcBlend == GLX_MATERIAL_STATE_SRCBLEND_ONE_MINUS_DST_ALPHA ? 1 : 0,
		srcBlend == GLX_MATERIAL_STATE_SRCBLEND_ALPHA_SATURATE ? 1 : 0,
		dstBlend == GLX_MATERIAL_STATE_DSTBLEND_ZERO ? 1 : 0,
		dstBlend == GLX_MATERIAL_STATE_DSTBLEND_ONE ? 1 : 0,
		dstBlend == GLX_MATERIAL_STATE_DSTBLEND_SRC_COLOR ? 1 : 0,
		dstBlend == GLX_MATERIAL_STATE_DSTBLEND_ONE_MINUS_SRC_COLOR ? 1 : 0,
		dstBlend == GLX_MATERIAL_STATE_DSTBLEND_SRC_ALPHA ? 1 : 0,
		dstBlend == GLX_MATERIAL_STATE_DSTBLEND_ONE_MINUS_SRC_ALPHA ? 1 : 0,
		dstBlend == GLX_MATERIAL_STATE_DSTBLEND_DST_ALPHA ? 1 : 0,
		dstBlend == GLX_MATERIAL_STATE_DSTBLEND_ONE_MINUS_DST_ALPHA ? 1 : 0,
		alphaTest == GLX_MATERIAL_STATE_ATEST_GT_0 ? 1 : 0,
		alphaTest == GLX_MATERIAL_STATE_ATEST_LT_80 ? 1 : 0,
		alphaTest == GLX_MATERIAL_STATE_ATEST_GE_80 ? 1 : 0,
		stageKey.rgbGen, stageKey.alphaGen, stageKey.rgbWaveFunc, stageKey.alphaWaveFunc,
		stageKey.tcGen0, stageKey.tcGen1,
		stageKey.texMods0, stageKey.texMods1,
		stageKey.texModTypes0, stageKey.texModTypes1 ) &&
	GLX_Material_AppendSource( out, outSize, &used,
		"#define GLX_MATERIAL_TMOD_OPCODE_NONE %i\n"
		"#define GLX_MATERIAL_TMOD_OPCODE_TRANSFORM %i\n"
		"#define GLX_MATERIAL_TMOD_OPCODE_TURBULENT %i\n"
		"#define GLX_MATERIAL_TMOD_OPCODE_SCROLL %i\n"
		"#define GLX_MATERIAL_TMOD_OPCODE_SCALE %i\n"
		"#define GLX_MATERIAL_TMOD_OPCODE_STRETCH %i\n"
		"#define GLX_MATERIAL_TMOD_OPCODE_ROTATE %i\n"
		"#define GLX_MATERIAL_TMOD_OPCODE_ENTITY_TRANSLATE %i\n"
		"#define GLX_MATERIAL_TMOD_OPCODE_OFFSET %i\n"
		"#define GLX_MATERIAL_TMOD_OPCODE_SCALE_OFFSET %i\n"
		"#define GLX_MATERIAL_TMOD_OPCODE_OFFSET_SCALE %i\n"
		"#define GLX_MATERIAL_WAVEFUNC_NONE %i\n"
		"#define GLX_MATERIAL_WAVEFUNC_SIN %i\n"
		"#define GLX_MATERIAL_WAVEFUNC_SQUARE %i\n"
		"#define GLX_MATERIAL_WAVEFUNC_TRIANGLE %i\n"
		"#define GLX_MATERIAL_WAVEFUNC_SAWTOOTH %i\n"
		"#define GLX_MATERIAL_WAVEFUNC_INVERSE_SAWTOOTH %i\n"
		"#define GLX_MATERIAL_WAVEFUNC_NOISE %i\n"
		"#define GLX_MATERIAL_TEXMOD_SEQUENCE0 %u\n"
		"#define GLX_MATERIAL_TEXMOD_SEQUENCE1 %u\n"
		"#define GLX_MATERIAL_TEXMOD_WAVEFUNCS0 %u\n"
		"#define GLX_MATERIAL_TEXMOD_WAVEFUNCS1 %u\n"
		"#define GLX_MATERIAL_TMOD0_SLOT0 %u\n"
		"#define GLX_MATERIAL_TMOD0_SLOT1 %u\n"
		"#define GLX_MATERIAL_TMOD0_SLOT2 %u\n"
		"#define GLX_MATERIAL_TMOD0_SLOT3 %u\n"
		"#define GLX_MATERIAL_TMOD0_SLOT4 %u\n"
		"#define GLX_MATERIAL_TMOD0_WAVEFUNC_SLOT0 %u\n"
		"#define GLX_MATERIAL_TMOD0_WAVEFUNC_SLOT1 %u\n"
		"#define GLX_MATERIAL_TMOD0_WAVEFUNC_SLOT2 %u\n"
		"#define GLX_MATERIAL_TMOD0_WAVEFUNC_SLOT3 %u\n"
		"#define GLX_MATERIAL_TMOD0_WAVEFUNC_SLOT4 %u\n"
		"#define GLX_MATERIAL_TMOD1_SLOT0 %u\n"
		"#define GLX_MATERIAL_TMOD1_SLOT1 %u\n"
		"#define GLX_MATERIAL_TMOD1_SLOT2 %u\n"
		"#define GLX_MATERIAL_TMOD1_SLOT3 %u\n"
		"#define GLX_MATERIAL_TMOD1_SLOT4 %u\n"
		"#define GLX_MATERIAL_TMOD1_WAVEFUNC_SLOT0 %u\n"
		"#define GLX_MATERIAL_TMOD1_WAVEFUNC_SLOT1 %u\n"
		"#define GLX_MATERIAL_TMOD1_WAVEFUNC_SLOT2 %u\n"
		"#define GLX_MATERIAL_TMOD1_WAVEFUNC_SLOT3 %u\n"
		"#define GLX_MATERIAL_TMOD1_WAVEFUNC_SLOT4 %u\n",
		GLX_MATERIAL_TMOD_OPCODE_NONE,
		GLX_MATERIAL_TMOD_OPCODE_TRANSFORM,
		GLX_MATERIAL_TMOD_OPCODE_TURBULENT,
		GLX_MATERIAL_TMOD_OPCODE_SCROLL,
		GLX_MATERIAL_TMOD_OPCODE_SCALE,
		GLX_MATERIAL_TMOD_OPCODE_STRETCH,
		GLX_MATERIAL_TMOD_OPCODE_ROTATE,
		GLX_MATERIAL_TMOD_OPCODE_ENTITY_TRANSLATE,
		GLX_MATERIAL_TMOD_OPCODE_OFFSET,
		GLX_MATERIAL_TMOD_OPCODE_SCALE_OFFSET,
		GLX_MATERIAL_TMOD_OPCODE_OFFSET_SCALE,
		GLX_MATERIAL_WAVEFUNC_NONE,
		GLX_MATERIAL_WAVEFUNC_SIN,
		GLX_MATERIAL_WAVEFUNC_SQUARE,
		GLX_MATERIAL_WAVEFUNC_TRIANGLE,
		GLX_MATERIAL_WAVEFUNC_SAWTOOTH,
		GLX_MATERIAL_WAVEFUNC_INVERSE_SAWTOOTH,
		GLX_MATERIAL_WAVEFUNC_NOISE,
		stageKey.texModSequence0, stageKey.texModSequence1,
		stageKey.texModWaveFuncs0, stageKey.texModWaveFuncs1,
		GLX_Material_TexModSequenceSlot( stageKey.texModSequence0, 0 ),
		GLX_Material_TexModSequenceSlot( stageKey.texModSequence0, 1 ),
		GLX_Material_TexModSequenceSlot( stageKey.texModSequence0, 2 ),
		GLX_Material_TexModSequenceSlot( stageKey.texModSequence0, 3 ),
		GLX_Material_TexModSequenceSlot( stageKey.texModSequence0, 4 ),
		GLX_Material_TexModWaveFuncSlot( stageKey.texModWaveFuncs0, 0 ),
		GLX_Material_TexModWaveFuncSlot( stageKey.texModWaveFuncs0, 1 ),
		GLX_Material_TexModWaveFuncSlot( stageKey.texModWaveFuncs0, 2 ),
		GLX_Material_TexModWaveFuncSlot( stageKey.texModWaveFuncs0, 3 ),
		GLX_Material_TexModWaveFuncSlot( stageKey.texModWaveFuncs0, 4 ),
		GLX_Material_TexModSequenceSlot( stageKey.texModSequence1, 0 ),
		GLX_Material_TexModSequenceSlot( stageKey.texModSequence1, 1 ),
		GLX_Material_TexModSequenceSlot( stageKey.texModSequence1, 2 ),
		GLX_Material_TexModSequenceSlot( stageKey.texModSequence1, 3 ),
		GLX_Material_TexModSequenceSlot( stageKey.texModSequence1, 4 ),
		GLX_Material_TexModWaveFuncSlot( stageKey.texModWaveFuncs1, 0 ),
		GLX_Material_TexModWaveFuncSlot( stageKey.texModWaveFuncs1, 1 ),
		GLX_Material_TexModWaveFuncSlot( stageKey.texModWaveFuncs1, 2 ),
		GLX_Material_TexModWaveFuncSlot( stageKey.texModWaveFuncs1, 3 ),
		GLX_Material_TexModWaveFuncSlot( stageKey.texModWaveFuncs1, 4 ) ) &&
	GLX_Material_AppendSource( out, outSize, &used,
		"#define GLX_MATERIAL_STAGE_MULTITEXTURE %i\n"
		"#define GLX_MATERIAL_STAGE_DEPTH_FRAGMENT %i\n"
		"#define GLX_MATERIAL_STAGE_BLEND %i\n"
		"#define GLX_MATERIAL_STAGE_ALPHA_TEST %i\n"
		"#define GLX_MATERIAL_STAGE_DEPTH_WRITE %i\n"
		"#define GLX_MATERIAL_STAGE_LIGHTMAP %i\n"
		"#define GLX_MATERIAL_STAGE_ANIMATED_IMAGE %i\n"
		"#define GLX_MATERIAL_STAGE_VIDEO_MAP %i\n"
		"#define GLX_MATERIAL_STAGE_SCREEN_MAP %i\n"
		"#define GLX_MATERIAL_STAGE_DLIGHT_MAP %i\n"
		"#define GLX_MATERIAL_STAGE_TEXMOD %i\n"
		"#define GLX_MATERIAL_STAGE_ENVIRONMENT %i\n"
		"#define GLX_MATERIAL_STAGE_ST0 %i\n"
		"#define GLX_MATERIAL_STAGE_ST1 %i\n"
		"#define GLX_MATERIAL_STAGE_SHADOW_PASS %i\n"
		"#define GLX_MATERIAL_STAGE_BEAM_PASS %i\n"
		"#define GLX_MATERIAL_STAGE_POSTPROCESS_PASS %i\n"
		"#define GLX_MATERIAL_STAGE_DETAIL %i\n",
		( stageKey.flags & GLX_STAGE_MULTITEXTURE ) ? 1 : 0,
		( stageKey.flags & GLX_STAGE_DEPTH_FRAGMENT ) ? 1 : 0,
		( stageKey.flags & GLX_STAGE_BLEND ) ? 1 : 0,
		( stageKey.flags & GLX_STAGE_ALPHA_TEST ) ? 1 : 0,
		( stageKey.flags & GLX_STAGE_DEPTH_WRITE ) ? 1 : 0,
		( stageKey.flags & GLX_STAGE_LIGHTMAP ) ? 1 : 0,
		( stageKey.flags & GLX_STAGE_ANIMATED_IMAGE ) ? 1 : 0,
		( stageKey.flags & GLX_STAGE_VIDEO_MAP ) ? 1 : 0,
		( stageKey.flags & GLX_STAGE_SCREEN_MAP ) ? 1 : 0,
		( stageKey.flags & GLX_STAGE_DLIGHT_MAP ) ? 1 : 0,
		( stageKey.flags & GLX_STAGE_TEXMOD ) ? 1 : 0,
		( stageKey.flags & GLX_STAGE_ENVIRONMENT ) ? 1 : 0,
		( stageKey.flags & GLX_STAGE_ST0 ) ? 1 : 0,
		( stageKey.flags & GLX_STAGE_ST1 ) ? 1 : 0,
		( stageKey.flags & GLX_STAGE_SHADOW_PASS ) ? 1 : 0,
		( stageKey.flags & GLX_STAGE_BEAM_PASS ) ? 1 : 0,
		( stageKey.flags & GLX_STAGE_POSTPROCESS_PASS ) ? 1 : 0,
		( stageKey.flags & GLX_STAGE_DETAIL ) ? 1 : 0 ) &&
	GLX_Material_AppendSource( out, outSize, &used,
		"#define GLX_MATERIAL_TMOD0_NONE %i\n"
		"#define GLX_MATERIAL_TMOD0_TRANSFORM %i\n"
		"#define GLX_MATERIAL_TMOD0_TURBULENT %i\n"
		"#define GLX_MATERIAL_TMOD0_SCROLL %i\n"
		"#define GLX_MATERIAL_TMOD0_SCALE %i\n"
		"#define GLX_MATERIAL_TMOD0_STRETCH %i\n"
		"#define GLX_MATERIAL_TMOD0_ROTATE %i\n"
		"#define GLX_MATERIAL_TMOD0_ENTITY_TRANSLATE %i\n"
		"#define GLX_MATERIAL_TMOD0_OFFSET %i\n"
		"#define GLX_MATERIAL_TMOD0_SCALE_OFFSET %i\n"
		"#define GLX_MATERIAL_TMOD0_OFFSET_SCALE %i\n"
		"#define GLX_MATERIAL_TMOD1_NONE %i\n"
		"#define GLX_MATERIAL_TMOD1_TRANSFORM %i\n"
		"#define GLX_MATERIAL_TMOD1_TURBULENT %i\n"
		"#define GLX_MATERIAL_TMOD1_SCROLL %i\n"
		"#define GLX_MATERIAL_TMOD1_SCALE %i\n"
		"#define GLX_MATERIAL_TMOD1_STRETCH %i\n"
		"#define GLX_MATERIAL_TMOD1_ROTATE %i\n"
		"#define GLX_MATERIAL_TMOD1_ENTITY_TRANSLATE %i\n"
		"#define GLX_MATERIAL_TMOD1_OFFSET %i\n"
		"#define GLX_MATERIAL_TMOD1_SCALE_OFFSET %i\n"
		"#define GLX_MATERIAL_TMOD1_OFFSET_SCALE %i\n"
		"#define GLX_MATERIAL_FOG_ADJUST_NONE %i\n"
		"#define GLX_MATERIAL_FOG_ADJUST_MODULATE_RGB %i\n"
		"#define GLX_MATERIAL_FOG_ADJUST_MODULATE_RGBA %i\n"
		"#define GLX_MATERIAL_FOG_ADJUST_MODULATE_ALPHA %i\n"
		"#define GLX_MATERIAL_FOG_ADJUST %i\n"
		"#define GLX_MATERIAL_FOG_PASS %i\n",
		( stageKey.texModTypes0 & GLX_MATERIAL_TMOD_NONE_BIT ) ? 1 : 0,
		( stageKey.texModTypes0 & GLX_MATERIAL_TMOD_TRANSFORM_BIT ) ? 1 : 0,
		( stageKey.texModTypes0 & GLX_MATERIAL_TMOD_TURBULENT_BIT ) ? 1 : 0,
		( stageKey.texModTypes0 & GLX_MATERIAL_TMOD_SCROLL_BIT ) ? 1 : 0,
		( stageKey.texModTypes0 & GLX_MATERIAL_TMOD_SCALE_BIT ) ? 1 : 0,
		( stageKey.texModTypes0 & GLX_MATERIAL_TMOD_STRETCH_BIT ) ? 1 : 0,
		( stageKey.texModTypes0 & GLX_MATERIAL_TMOD_ROTATE_BIT ) ? 1 : 0,
		( stageKey.texModTypes0 & GLX_MATERIAL_TMOD_ENTITY_TRANSLATE_BIT ) ? 1 : 0,
		( stageKey.texModTypes0 & GLX_MATERIAL_TMOD_OFFSET_BIT ) ? 1 : 0,
		( stageKey.texModTypes0 & GLX_MATERIAL_TMOD_SCALE_OFFSET_BIT ) ? 1 : 0,
		( stageKey.texModTypes0 & GLX_MATERIAL_TMOD_OFFSET_SCALE_BIT ) ? 1 : 0,
		( stageKey.texModTypes1 & GLX_MATERIAL_TMOD_NONE_BIT ) ? 1 : 0,
		( stageKey.texModTypes1 & GLX_MATERIAL_TMOD_TRANSFORM_BIT ) ? 1 : 0,
		( stageKey.texModTypes1 & GLX_MATERIAL_TMOD_TURBULENT_BIT ) ? 1 : 0,
		( stageKey.texModTypes1 & GLX_MATERIAL_TMOD_SCROLL_BIT ) ? 1 : 0,
		( stageKey.texModTypes1 & GLX_MATERIAL_TMOD_SCALE_BIT ) ? 1 : 0,
		( stageKey.texModTypes1 & GLX_MATERIAL_TMOD_STRETCH_BIT ) ? 1 : 0,
		( stageKey.texModTypes1 & GLX_MATERIAL_TMOD_ROTATE_BIT ) ? 1 : 0,
		( stageKey.texModTypes1 & GLX_MATERIAL_TMOD_ENTITY_TRANSLATE_BIT ) ? 1 : 0,
		( stageKey.texModTypes1 & GLX_MATERIAL_TMOD_OFFSET_BIT ) ? 1 : 0,
		( stageKey.texModTypes1 & GLX_MATERIAL_TMOD_SCALE_OFFSET_BIT ) ? 1 : 0,
		( stageKey.texModTypes1 & GLX_MATERIAL_TMOD_OFFSET_SCALE_BIT ) ? 1 : 0,
		GLX_MATERIAL_FOG_ADJUST_NONE,
		GLX_MATERIAL_FOG_ADJUST_MODULATE_RGB,
		GLX_MATERIAL_FOG_ADJUST_MODULATE_RGBA,
		GLX_MATERIAL_FOG_ADJUST_MODULATE_ALPHA,
		stageKey.fogAdjust,
		stageKey.fogPass ? 1 : 0 );
	return appended ? qtrue : qfalse;
}

static qboolean GLX_Material_VertexSource( const MaterialStageKey &stageKey,
	char *out, size_t outSize )
{
	char languageDefines[12288];
	int written;

	if ( !out || outSize == 0 ||
		!GLX_Material_StageLanguageDefines( stageKey, languageDefines, sizeof( languageDefines ) ) ) {
		return qfalse;
	}

	written = std::snprintf( out, outSize,
		"#version 120\n"
		"%s"
		"varying vec4 v_Color;\n"
		"varying vec2 v_TexCoord0;\n"
		"varying vec2 v_TexCoord1;\n"
		"vec4 GLX_PreparedColor(void)\n"
		"{\n"
		"#if GLX_MATERIAL_RGBGEN == 0\n"
		"    return gl_Color;\n"
		"#elif GLX_MATERIAL_RGBGEN == 1\n"
		"    return gl_Color;\n"
		"#elif GLX_MATERIAL_RGBGEN == 2\n"
		"    return gl_Color;\n"
		"#elif GLX_MATERIAL_RGBGEN == 3\n"
		"    return gl_Color;\n"
		"#elif GLX_MATERIAL_RGBGEN == 4\n"
		"    return gl_Color;\n"
		"#elif GLX_MATERIAL_RGBGEN == 5\n"
		"    return gl_Color;\n"
		"#elif GLX_MATERIAL_RGBGEN == 6\n"
		"    return gl_Color;\n"
		"#elif GLX_MATERIAL_RGBGEN == 7\n"
		"    return gl_Color;\n"
		"#elif GLX_MATERIAL_RGBGEN == 8\n"
		"    return gl_Color;\n"
		"#elif GLX_MATERIAL_RGBGEN == 9\n"
		"    return gl_Color;\n"
		"#elif GLX_MATERIAL_RGBGEN == 10\n"
		"    return gl_Color;\n"
		"#elif GLX_MATERIAL_RGBGEN == 11\n"
		"    return gl_Color;\n"
		"#else\n"
		"    return gl_Color;\n"
		"#endif\n"
		"}\n"
		"vec2 GLX_PreparedTexCoord0(void)\n"
		"{\n"
		"#if GLX_MATERIAL_TCGEN0 == 0\n"
		"    return gl_MultiTexCoord0.st;\n"
		"#elif GLX_MATERIAL_TCGEN0 == 1\n"
		"    return gl_MultiTexCoord0.st;\n"
		"#elif GLX_MATERIAL_TCGEN0 == 2\n"
		"    return gl_MultiTexCoord0.st;\n"
		"#elif GLX_MATERIAL_TCGEN0 == 3\n"
		"    return gl_MultiTexCoord0.st;\n"
		"#elif GLX_MATERIAL_TCGEN0 == 4\n"
		"    return gl_MultiTexCoord0.st;\n"
		"#elif GLX_MATERIAL_TCGEN0 == 5\n"
		"    return gl_MultiTexCoord0.st;\n"
		"#elif GLX_MATERIAL_TCGEN0 == 6\n"
		"    return gl_MultiTexCoord0.st;\n"
		"#elif GLX_MATERIAL_TCGEN0 == 7\n"
		"    return gl_MultiTexCoord0.st;\n"
		"#else\n"
		"    return gl_MultiTexCoord0.st;\n"
		"#endif\n"
		"}\n"
		"vec2 GLX_PreparedTexCoord1(void)\n"
		"{\n"
		"#if GLX_MATERIAL_TCGEN1 == 0\n"
		"    return gl_MultiTexCoord1.st;\n"
		"#elif GLX_MATERIAL_TCGEN1 == 1\n"
		"    return gl_MultiTexCoord1.st;\n"
		"#elif GLX_MATERIAL_TCGEN1 == 2\n"
		"    return gl_MultiTexCoord1.st;\n"
		"#elif GLX_MATERIAL_TCGEN1 == 3\n"
		"    return gl_MultiTexCoord1.st;\n"
		"#elif GLX_MATERIAL_TCGEN1 == 4\n"
		"    return gl_MultiTexCoord1.st;\n"
		"#elif GLX_MATERIAL_TCGEN1 == 5\n"
		"    return gl_MultiTexCoord1.st;\n"
		"#elif GLX_MATERIAL_TCGEN1 == 6\n"
		"    return gl_MultiTexCoord1.st;\n"
		"#elif GLX_MATERIAL_TCGEN1 == 7\n"
		"    return gl_MultiTexCoord1.st;\n"
		"#else\n"
		"    return gl_MultiTexCoord1.st;\n"
		"#endif\n"
		"}\n"
		"void main(void)\n"
		"{\n"
		"    gl_Position = ftransform();\n"
		"    v_Color = GLX_PreparedColor();\n"
		"    v_TexCoord0 = GLX_PreparedTexCoord0();\n"
		"    v_TexCoord1 = GLX_PreparedTexCoord1();\n"
		"}\n",
		languageDefines );
	out[outSize - 1] = '\0';
	return written >= 0 && static_cast<size_t>( written ) < outSize ? qtrue : qfalse;
}

static void GLX_Material_SetReason( MaterialState *state, const char *reason )
{
	if ( !state ) {
		return;
	}

	std::snprintf( state->reason, sizeof( state->reason ), "%s", reason ? reason : "" );
	state->reason[sizeof( state->reason ) - 1] = '\0';
}

static void GLX_Material_SetLastError( MaterialState *state, const char *error )
{
	if ( !state ) {
		return;
	}

	std::snprintf( state->lastError, sizeof( state->lastError ), "%s", error ? error : "" );
	state->lastError[sizeof( state->lastError ) - 1] = '\0';
}

static void *GLX_Material_GetProc( const char *name )
{
	return RI().GL_GetProcAddress ? RI().GL_GetProcAddress( name ) : nullptr;
}

static void GLX_Material_LoadFunctions( MaterialState *state )
{
	if ( !state ) {
		return;
	}

	state->fns.CreateShader = reinterpret_cast<PFNGLXCREATESHADERPROC>( GLX_Material_GetProc( "glCreateShader" ) );
	state->fns.ShaderSource = reinterpret_cast<PFNGLXSHADERSOURCEPROC>( GLX_Material_GetProc( "glShaderSource" ) );
	state->fns.CompileShader = reinterpret_cast<PFNGLXCOMPILESHADERPROC>( GLX_Material_GetProc( "glCompileShader" ) );
	state->fns.GetShaderiv = reinterpret_cast<PFNGLXGETSHADERIVPROC>( GLX_Material_GetProc( "glGetShaderiv" ) );
	state->fns.GetShaderInfoLog = reinterpret_cast<PFNGLXGETSHADERINFOLOGPROC>( GLX_Material_GetProc( "glGetShaderInfoLog" ) );
	state->fns.CreateProgram = reinterpret_cast<PFNGLXCREATEPROGRAMPROC>( GLX_Material_GetProc( "glCreateProgram" ) );
	state->fns.AttachShader = reinterpret_cast<PFNGLXATTACHSHADERPROC>( GLX_Material_GetProc( "glAttachShader" ) );
	state->fns.LinkProgram = reinterpret_cast<PFNGLXLINKPROGRAMPROC>( GLX_Material_GetProc( "glLinkProgram" ) );
	state->fns.GetProgramiv = reinterpret_cast<PFNGLXGETPROGRAMIVPROC>( GLX_Material_GetProc( "glGetProgramiv" ) );
	state->fns.GetProgramInfoLog = reinterpret_cast<PFNGLXGETPROGRAMINFOLOGPROC>( GLX_Material_GetProc( "glGetProgramInfoLog" ) );
	state->fns.UseProgram = reinterpret_cast<PFNGLXUSEPROGRAMPROC>( GLX_Material_GetProc( "glUseProgram" ) );
	state->fns.GetUniformLocation = reinterpret_cast<PFNGLXGETUNIFORMLOCATIONPROC>( GLX_Material_GetProc( "glGetUniformLocation" ) );
	state->fns.Uniform1i = reinterpret_cast<PFNGLXUNIFORM1IPROC>( GLX_Material_GetProc( "glUniform1i" ) );
	state->fns.DeleteProgram = reinterpret_cast<PFNGLXDELETEPROGRAMPROC>( GLX_Material_GetProc( "glDeleteProgram" ) );
	state->fns.DeleteShader = reinterpret_cast<PFNGLXDELETESHADERPROC>( GLX_Material_GetProc( "glDeleteShader" ) );
	state->fns.ObjectLabel = reinterpret_cast<PFNGLXMATERIALOBJECTLABELPROC>( GLX_Material_GetProc( "glObjectLabel" ) );
}

static qboolean GLX_Material_FunctionsReady( const MaterialState &state )
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
		state.fns.DeleteProgram &&
		state.fns.DeleteShader ? qtrue : qfalse;
}

static void GLX_Material_ResetCounters( MaterialState *state )
{
	if ( !state ) {
		return;
	}

	state->frames = 0;
	state->bindAttempts = 0;
	state->binds = 0;
	state->programSwitches = 0;
	state->unbinds = 0;
	state->cacheHits = 0;
	state->cacheMisses = 0;
	state->compileAttempts = 0;
	state->compileFailures = 0;
	state->linkFailures = 0;
	state->precacheAttempts = 0;
	state->precacheFailures = 0;
	state->bindFailures = 0;
	state->debugLabels = 0;
	state->contextlessDeletes = 0;
	state->compiledMaterialPlans = 0;
	state->unsupportedMaterialPlans = 0;
	state->parameterBlocks = 0;
	state->invalidParameterBlocks = 0;
	state->unsupportedRequests = 0;
	state->disabledSkips = 0;
	state->notReadySkips = 0;
	state->programLimitSkips = 0;
	state->lastRequest = {};
	state->lastMaterial = {};
	state->lastParameterBlock = {};
	state->lastParameterBlockHash = 0;
	state->lastKey = { MaterialProgramMode::SingleTexture, GLX_MATERIAL_FEATURE_NONE };
	state->lastStageKey = {};
	state->lastUnsupportedReasons = GLX_MATERIAL_UNSUPPORTED_NONE;
	GLX_Material_SetLastError( state, "" );
}

static void GLX_Material_PrintObjectLog( const MaterialState &state, GLuint object, qboolean program, printParm_t printLevel )
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

static MaterialIR GLX_Material_IRForRequest( const MaterialRequest &request )
{
	MaterialIR material = GLX_RenderIR_MakeMaterial( 0, request.flags,
		request.stateBits, 1 );

	material.rgbGen = request.rgbGen;
	material.alphaGen = request.alphaGen;
	material.rgbWaveFunc = request.rgbWaveFunc;
	material.alphaWaveFunc = request.alphaWaveFunc;
	material.tcGen0 = request.tcGen0;
	material.tcGen1 = request.tcGen1;
	material.texMods0 = request.texMods0;
	material.texMods1 = request.texMods1;
	material.texModTypes0 = request.texModTypes0;
	material.texModTypes1 = request.texModTypes1;
	material.texModSequence0 = request.texModSequence0;
	material.texModSequence1 = request.texModSequence1;
	material.texModWaveFuncs0 = request.texModWaveFuncs0;
	material.texModWaveFuncs1 = request.texModWaveFuncs1;
	material.fogAdjust = request.fogAdjust;
	material.materialCombine = request.materialCombine;
	material.fogPass = request.fogPass;
	return material;
}

static qboolean GLX_Material_FragmentSource( const MaterialStageKey &stageKey,
	char *out, size_t outSize )
{
	const MaterialProgramKey &key = stageKey.program;
	char languageDefines[12288];
	const char *body = "";
	int written;

	switch ( key.mode ) {
	case MaterialProgramMode::SingleTexture:
	case MaterialProgramMode::Fog:
		body =
			"    vec4 base = texture2D(u_Texture0, v_TexCoord0);\n"
			"    gl_FragColor = GLX_ApplyPreparedStageLanguage(base * v_Color);\n";
		break;
	case MaterialProgramMode::MultiModulate:
		body =
			"    vec4 base = texture2D(u_Texture0, v_TexCoord0) * v_Color;\n"
			"    vec4 layer = texture2D(u_Texture1, v_TexCoord1);\n"
			"    gl_FragColor = GLX_ApplyPreparedStageLanguage(base * layer);\n";
		break;
	case MaterialProgramMode::MultiAdd:
		body =
			"    vec4 base = texture2D(u_Texture0, v_TexCoord0) * v_Color;\n"
			"    vec4 layer = texture2D(u_Texture1, v_TexCoord1);\n"
			"    gl_FragColor = GLX_ApplyPreparedStageLanguage(min(base + layer, vec4(1.0)));\n";
		break;
	case MaterialProgramMode::MultiReplace:
		body =
			"    gl_FragColor = GLX_ApplyPreparedStageLanguage(texture2D(u_Texture1, v_TexCoord1));\n";
		break;
	case MaterialProgramMode::MultiDecal:
		body =
			"    vec4 base = texture2D(u_Texture0, v_TexCoord0) * v_Color;\n"
			"    vec4 layer = texture2D(u_Texture1, v_TexCoord1);\n"
			"    gl_FragColor = GLX_ApplyPreparedStageLanguage(vec4(mix(base.rgb, layer.rgb, layer.a), base.a));\n";
		break;
	}

	if ( !out || outSize == 0 ||
		!GLX_Material_StageLanguageDefines( stageKey, languageDefines, sizeof( languageDefines ) ) ) {
		return qfalse;
	}

	written = std::snprintf( out, outSize,
		"#version 120\n"
		"%s"
		"uniform sampler2D u_Texture0;\n"
		"uniform sampler2D u_Texture1;\n"
		"varying vec4 v_Color;\n"
		"varying vec2 v_TexCoord0;\n"
		"varying vec2 v_TexCoord1;\n"
		"vec4 GLX_ApplyPreparedStageLanguage(vec4 color)\n"
		"{\n"
		"    vec4 prepared = color;\n"
		"#if GLX_MATERIAL_ALPHAGEN == 0\n"
		"    prepared.a = color.a;\n"
		"#elif GLX_MATERIAL_ALPHAGEN == 1\n"
		"    prepared.a = color.a;\n"
		"#elif GLX_MATERIAL_ALPHAGEN == 2\n"
		"    prepared.a = color.a;\n"
		"#elif GLX_MATERIAL_ALPHAGEN == 3\n"
		"    prepared.a = color.a;\n"
		"#elif GLX_MATERIAL_ALPHAGEN == 4\n"
		"    prepared.a = color.a;\n"
		"#elif GLX_MATERIAL_ALPHAGEN == 5\n"
		"    prepared.a = color.a;\n"
		"#elif GLX_MATERIAL_ALPHAGEN == 6\n"
		"    prepared.a = color.a;\n"
		"#elif GLX_MATERIAL_ALPHAGEN == 7\n"
		"    prepared.a = color.a;\n"
		"#elif GLX_MATERIAL_ALPHAGEN == 8\n"
		"    prepared.a = color.a;\n"
		"#elif GLX_MATERIAL_ALPHAGEN == 9\n"
		"    prepared.a = color.a;\n"
		"#else\n"
		"    prepared.a = color.a;\n"
		"#endif\n"
		"#if GLX_MATERIAL_RGB_WAVEFUNC != GLX_MATERIAL_WAVEFUNC_NONE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_ALPHA_WAVEFUNC != GLX_MATERIAL_WAVEFUNC_NONE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_SRCBLEND_ZERO\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_SRCBLEND_ONE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_SRCBLEND_DST_COLOR\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_SRCBLEND_ONE_MINUS_DST_COLOR\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_SRCBLEND_SRC_ALPHA\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_SRCBLEND_ONE_MINUS_SRC_ALPHA\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_SRCBLEND_DST_ALPHA\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_SRCBLEND_ONE_MINUS_DST_ALPHA\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_SRCBLEND_ALPHA_SATURATE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_DSTBLEND_ZERO\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_DSTBLEND_ONE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_DSTBLEND_SRC_COLOR\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_DSTBLEND_ONE_MINUS_SRC_COLOR\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_DSTBLEND_SRC_ALPHA\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_DSTBLEND_ONE_MINUS_SRC_ALPHA\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_DSTBLEND_DST_ALPHA\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_DSTBLEND_ONE_MINUS_DST_ALPHA\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_ATEST_GT_0\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_ATEST_LT_80\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_ATEST_GE_80\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STATE_DEPTHMASK_TRUE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STATE_POLYMODE_LINE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STATE_DEPTHTEST_DISABLE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STATE_DEPTHFUNC_EQUAL\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STATE_UNKNOWN_BITS != 0\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STAGE_MULTITEXTURE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STAGE_DEPTH_FRAGMENT\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STAGE_BLEND\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STAGE_ALPHA_TEST\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STAGE_DEPTH_WRITE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STAGE_LIGHTMAP\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STAGE_ANIMATED_IMAGE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STAGE_VIDEO_MAP\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STAGE_SCREEN_MAP\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STAGE_DLIGHT_MAP\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STAGE_ENVIRONMENT\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STAGE_TEXMOD\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STAGE_ST0\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STAGE_ST1\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD0_NONE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD0_TRANSFORM\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD0_TURBULENT\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD0_SCROLL\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD0_SCALE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD0_STRETCH\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD0_ROTATE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD0_ENTITY_TRANSLATE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD0_OFFSET\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD0_SCALE_OFFSET\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD0_OFFSET_SCALE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD1_NONE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD1_TRANSFORM\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD1_TURBULENT\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD1_SCROLL\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD1_SCALE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD1_STRETCH\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD1_ROTATE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD1_ENTITY_TRANSLATE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD1_OFFSET\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD1_SCALE_OFFSET\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TMOD1_OFFSET_SCALE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TEXMOD_WAVEFUNCS0 != 0\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_TEXMOD_WAVEFUNCS1 != 0\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STAGE_SHADOW_PASS\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STAGE_BEAM_PASS\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STAGE_POSTPROCESS_PASS\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_STAGE_DETAIL\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_FOG_ADJUST != GLX_MATERIAL_FOG_ADJUST_NONE\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"#if GLX_MATERIAL_FOG_PASS\n"
		"    prepared += vec4(0.0);\n"
		"#endif\n"
		"    return prepared;\n"
		"}\n"
		"void main(void)\n"
		"{\n"
		"%s"
		"}\n",
		languageDefines,
		body );
	out[outSize - 1] = '\0';
	return written >= 0 && static_cast<size_t>( written ) < outSize ? qtrue : qfalse;
}

static GLuint GLX_Material_CompileShader( MaterialState *state, GLenum shaderType, const char *source )
{
	GLuint shader;
	GLint ok = 0;
	const GLchar *sources[1];

	if ( !state || !source ) {
		return 0;
	}

	shader = state->fns.CreateShader( shaderType );
	if ( !shader ) {
		GLX_Material_SetLastError( state, "glCreateShader returned 0" );
		return 0;
	}

	sources[0] = source;
	state->fns.ShaderSource( shader, 1, sources, nullptr );
	state->fns.CompileShader( shader );
	state->fns.GetShaderiv( shader, GL_COMPILE_STATUS, &ok );

	if ( !ok ) {
		state->compileFailures++;
		GLX_Material_SetLastError( state, shaderType == GL_VERTEX_SHADER ? "vertex shader compile failed" : "fragment shader compile failed" );
		RI().Printf( PRINT_WARNING, "GLx material %s shader compile failed:\n",
			shaderType == GL_VERTEX_SHADER ? "vertex" : "fragment" );
		GLX_Material_PrintObjectLog( *state, shader, qfalse, PRINT_WARNING );
		if ( state->r_glxMaterialDebug && state->r_glxMaterialDebug->integer > 1 ) {
			RI().Printf( PRINT_ALL, "%s\n", source );
		}
		state->fns.DeleteShader( shader );
		return 0;
	}

	return shader;
}

static void GLX_Material_DeleteProgram( MaterialState *state, MaterialProgram *program )
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

static void GLX_Material_ResetRuntime( MaterialState *state, qboolean deletePrograms )
{
	qboolean canDeletePrograms;

	if ( !state ) {
		return;
	}

	canDeletePrograms = deletePrograms && state->fns.DeleteProgram && state->fns.DeleteShader ? qtrue : qfalse;

	if ( state->programCount > 0 && !canDeletePrograms ) {
		state->contextlessDeletes += static_cast<unsigned int>( state->programCount );
	}

	if ( canDeletePrograms ) {
		for ( int i = 0; i < state->programCount; i++ ) {
			GLX_Material_DeleteProgram( state, &state->programs[i] );
		}
	} else {
		state->currentProgram = 0;
		for ( int i = 0; i < state->programCount; i++ ) {
			state->programs[i] = {};
		}
	}

	for ( int i = state->programCount; i < GLX_MATERIAL_PROGRAM_LIMIT; i++ ) {
		state->programs[i] = {};
	}

	state->fns = {};
	state->programCount = 0;
	state->lastFoundProgram = 0;
	state->currentProgram = 0;
	state->ready = qfalse;
	GLX_Material_SetReason( state, "not initialized" );
}

static MaterialProgram *GLX_Material_FindProgram( MaterialState *state,
	const MaterialStageKey &stageKey )
{
	if ( !state ) {
		return nullptr;
	}

	/* Draws arrive sorted by shader, so consecutive binds usually repeat the
	   same stage key; try the most recently found program before scanning. */
	if ( state->lastFoundProgram >= 0 && state->lastFoundProgram < state->programCount ) {
		MaterialProgram *last = &state->programs[state->lastFoundProgram];

		if ( last->valid && GLX_Material_StageKeyEquals( last->stageKey, stageKey ) ) {
			state->cacheHits++;
			return last;
		}
	}

	for ( int i = 0; i < state->programCount; i++ ) {
		if ( state->programs[i].valid &&
			GLX_Material_StageKeyEquals( state->programs[i].stageKey, stageKey ) ) {
			state->cacheHits++;
			state->lastFoundProgram = i;
			return &state->programs[i];
		}
	}

	state->cacheMisses++;
	return nullptr;
}

static void GLX_Material_LabelProgram( MaterialState *state, MaterialProgram *program )
{
	char label[224];
	char keyName[192];

	if ( !state || !program || !program->program || !state->fns.ObjectLabel ) {
		return;
	}

	GLX_Material_StageKeyName( program->stageKey, keyName, sizeof( keyName ) );
	std::snprintf( label, sizeof( label ), "GLx material %s", keyName );
	label[sizeof( label ) - 1] = '\0';
	state->fns.ObjectLabel( GL_PROGRAM, program->program, static_cast<GLsizei>( -1 ), label );
	state->debugLabels++;
}

static MaterialProgram *GLX_Material_CreateProgram( MaterialState *state,
	const MaterialStageKey &stageKey )
{
	char vertexSource[32768];
	char fragmentSource[32768];
	char keyName[192];
	MaterialProgram *program;
	GLint ok = 0;

	if ( !state ) {
		return nullptr;
	}

	if ( state->programCount >= GLX_MATERIAL_PROGRAM_LIMIT ) {
		state->programLimitSkips++;
		GLX_Material_SetLastError( state, "material program cache is full" );
		return nullptr;
	}

	state->compileAttempts++;
	program = &state->programs[state->programCount];
	*program = {};
	program->stageKey = stageKey;
	program->key = stageKey.program;
	GLX_Material_StageKeyName( stageKey, keyName, sizeof( keyName ) );

	if ( !GLX_Material_VertexSource( stageKey, vertexSource, sizeof( vertexSource ) ) ||
		!GLX_Material_FragmentSource( stageKey, fragmentSource, sizeof( fragmentSource ) ) ) {
		GLX_Material_SetLastError( state, "material shader source exceeded generator buffer" );
		GLX_Material_DeleteProgram( state, program );
		return nullptr;
	}
	program->vertexShader = GLX_Material_CompileShader( state, GL_VERTEX_SHADER, vertexSource );
	program->fragmentShader = GLX_Material_CompileShader( state, GL_FRAGMENT_SHADER, fragmentSource );
	if ( !program->vertexShader || !program->fragmentShader ) {
		GLX_Material_DeleteProgram( state, program );
		return nullptr;
	}

	program->program = state->fns.CreateProgram();
	if ( !program->program ) {
		GLX_Material_SetLastError( state, "glCreateProgram returned 0" );
		GLX_Material_DeleteProgram( state, program );
		return nullptr;
	}

	state->fns.AttachShader( program->program, program->vertexShader );
	state->fns.AttachShader( program->program, program->fragmentShader );
	state->fns.LinkProgram( program->program );
	state->fns.GetProgramiv( program->program, GL_LINK_STATUS, &ok );

	if ( !ok ) {
		state->linkFailures++;
		GLX_Material_SetLastError( state, "program link failed" );
		RI().Printf( PRINT_WARNING, "GLx material program link failed for %s:\n", keyName );
		GLX_Material_PrintObjectLog( *state, program->program, qtrue, PRINT_WARNING );
		GLX_Material_DeleteProgram( state, program );
		return nullptr;
	}

	program->texture0Uniform = state->fns.GetUniformLocation( program->program, "u_Texture0" );
	program->texture1Uniform = state->fns.GetUniformLocation( program->program, "u_Texture1" );
	state->fns.UseProgram( program->program );
	if ( program->texture0Uniform >= 0 ) {
		state->fns.Uniform1i( program->texture0Uniform, 0 );
	}
	if ( program->texture1Uniform >= 0 ) {
		state->fns.Uniform1i( program->texture1Uniform, 1 );
	}
	state->fns.UseProgram( 0 );
	state->currentProgram = 0;

	program->valid = qtrue;
	GLX_Material_LabelProgram( state, program );
	state->programCount++;
	GLX_Material_SetLastError( state, "" );

	if ( state->r_glxMaterialDebug && state->r_glxMaterialDebug->integer ) {
		RI().Printf( PRINT_ALL, "GLx material compiled %s program %u.\n",
			keyName, program->program );
	}

	return program;
}

static qboolean GLX_Material_CombineForMode( MaterialProgramMode mode, int *combine )
{
	if ( !combine ) {
		return qfalse;
	}

	switch ( mode ) {
	case MaterialProgramMode::SingleTexture:
	case MaterialProgramMode::Fog:
		*combine = 0;
		return qtrue;
	case MaterialProgramMode::MultiModulate:
		*combine = GLX_MATERIAL_COMBINE_MODULATE;
		return qtrue;
	case MaterialProgramMode::MultiAdd:
		*combine = GLX_MATERIAL_COMBINE_ADD;
		return qtrue;
	case MaterialProgramMode::MultiReplace:
		*combine = GLX_MATERIAL_COMBINE_REPLACE;
		return qtrue;
	case MaterialProgramMode::MultiDecal:
		*combine = GLX_MATERIAL_COMBINE_DECAL;
		return qtrue;
	default:
		return qfalse;
	}
}

static qboolean GLX_Material_StageKeySupported( const MaterialStageKey &stageKey )
{
	MaterialStageKey normalized {};
	int combine = 0;

	if ( !GLX_Material_CombineForMode( stageKey.program.mode, &combine ) ) {
		return qfalse;
	}
	if ( !GLX_Material_StageKeyForInputsFull( stageKey.flags, stageKey.stateBits,
		combine, stageKey.rgbGen, stageKey.alphaGen,
		stageKey.rgbWaveFunc, stageKey.alphaWaveFunc,
		stageKey.tcGen0, stageKey.tcGen1,
		stageKey.texMods0, stageKey.texMods1, stageKey.texModTypes0, stageKey.texModTypes1,
		stageKey.texModSequence0, stageKey.texModSequence1,
		stageKey.texModWaveFuncs0, stageKey.texModWaveFuncs1,
		stageKey.fogAdjust, stageKey.fogPass, &normalized ) ) {
		return qfalse;
	}

	return GLX_Material_StageKeyEquals( normalized, stageKey );
}

static qboolean GLX_Material_DefaultStageKeyForProgram( MaterialProgramMode mode,
	unsigned int features, MaterialStageKey *stageKey )
{
	if ( !stageKey || !GLX_Material_FeaturesAllowedForMode( mode, features ) ) {
		return qfalse;
	}

	*stageKey = {};
	stageKey->program = { mode, features };
	stageKey->rgbGen = GLX_MATERIAL_RGBGEN_IDENTITY;
	stageKey->alphaGen = GLX_MATERIAL_ALPHAGEN_SKIP;
	stageKey->tcGen0 = GLX_MATERIAL_TCGEN_TEXTURE;
	stageKey->tcGen1 = GLX_MATERIAL_TCGEN_BAD;
	stageKey->fogPass = mode == MaterialProgramMode::Fog ? qtrue : qfalse;

	if ( mode == MaterialProgramMode::Fog ) {
		stageKey->rgbGen = GLX_MATERIAL_RGBGEN_FOG;
		stageKey->alphaGen = GLX_MATERIAL_ALPHAGEN_IDENTITY;
		stageKey->tcGen0 = GLX_MATERIAL_TCGEN_FOG;
		return qtrue;
	}

	if ( mode != MaterialProgramMode::SingleTexture ) {
		stageKey->flags |= GLX_STAGE_MULTITEXTURE | GLX_STAGE_ST0 | GLX_STAGE_ST1;
		stageKey->tcGen1 = GLX_MATERIAL_TCGEN_LIGHTMAP;
	}

	if ( features & GLX_MATERIAL_FEATURE_DEPTH_FRAGMENT ) {
		stageKey->flags |= GLX_STAGE_DEPTH_FRAGMENT;
	}
	if ( features & GLX_MATERIAL_FEATURE_ENVIRONMENT ) {
		stageKey->flags |= GLX_STAGE_ENVIRONMENT;
		stageKey->tcGen0 = GLX_MATERIAL_TCGEN_ENVIRONMENT_MAPPED;
	}
	if ( features & GLX_MATERIAL_FEATURE_TEXMOD ) {
		stageKey->flags |= GLX_STAGE_TEXMOD;
		stageKey->texMods0 = 1;
		stageKey->texModTypes0 = GLX_MATERIAL_TMOD_SCROLL_BIT;
		stageKey->texModSequence0 = GLX_Material_TexModSequenceSetSlot( 0, 0,
			GLX_MATERIAL_TMOD_OPCODE_SCROLL );
	}

	return GLX_Material_StageKeySupported( *stageKey );
}

static qboolean GLX_Material_PrecacheStageKey( MaterialState *state,
	const MaterialStageKey &stageKey )
{
	MaterialProgram *program;

	if ( !state || !GLX_Material_StageKeySupported( stageKey ) ) {
		if ( state ) {
			GLX_Material_SetLastError( state, "unsupported precache material stage language" );
		}
		return qfalse;
	}

	program = GLX_Material_FindProgram( state, stageKey );
	if ( !program ) {
		program = GLX_Material_CreateProgram( state, stageKey );
	}
	return program && program->valid ? qtrue : qfalse;
}

static void GLX_Material_ApplyFlagsForStateBits( MaterialStageKey *stageKey )
{
	if ( !stageKey ) {
		return;
	}

	if ( stageKey->stateBits & GLX_MATERIAL_STATE_BLEND_BITS ) {
		stageKey->flags |= GLX_STAGE_BLEND;
	}
	if ( stageKey->stateBits & GLX_MATERIAL_STATE_ATEST_BITS ) {
		stageKey->flags |= GLX_STAGE_ALPHA_TEST;
	}
	if ( stageKey->stateBits & GLX_MATERIAL_STATE_DEPTHMASK_TRUE ) {
		stageKey->flags |= GLX_STAGE_DEPTH_WRITE;
	}
}

static qboolean GLX_Material_PrecacheLanguageCoverage( MaterialState *state )
{
	qboolean ok = qtrue;
	const int rgbGens[] = {
		GLX_MATERIAL_RGBGEN_BAD,
		GLX_MATERIAL_RGBGEN_IDENTITY_LIGHTING,
		GLX_MATERIAL_RGBGEN_IDENTITY,
		GLX_MATERIAL_RGBGEN_ENTITY,
		GLX_MATERIAL_RGBGEN_ONE_MINUS_ENTITY,
		GLX_MATERIAL_RGBGEN_EXACT_VERTEX,
		GLX_MATERIAL_RGBGEN_VERTEX,
		GLX_MATERIAL_RGBGEN_ONE_MINUS_VERTEX,
		GLX_MATERIAL_RGBGEN_WAVEFORM,
		GLX_MATERIAL_RGBGEN_LIGHTING_DIFFUSE,
		GLX_MATERIAL_RGBGEN_FOG,
		GLX_MATERIAL_RGBGEN_CONST
	};
	const int alphaGens[] = {
		GLX_MATERIAL_ALPHAGEN_IDENTITY,
		GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_ALPHAGEN_ENTITY,
		GLX_MATERIAL_ALPHAGEN_ONE_MINUS_ENTITY,
		GLX_MATERIAL_ALPHAGEN_VERTEX,
		GLX_MATERIAL_ALPHAGEN_ONE_MINUS_VERTEX,
		GLX_MATERIAL_ALPHAGEN_LIGHTING_SPECULAR,
		GLX_MATERIAL_ALPHAGEN_WAVEFORM,
		GLX_MATERIAL_ALPHAGEN_PORTAL,
		GLX_MATERIAL_ALPHAGEN_CONST
	};
	const int tcGens[] = {
		GLX_MATERIAL_TCGEN_BAD,
		GLX_MATERIAL_TCGEN_IDENTITY,
		GLX_MATERIAL_TCGEN_LIGHTMAP,
		GLX_MATERIAL_TCGEN_TEXTURE,
		GLX_MATERIAL_TCGEN_ENVIRONMENT_MAPPED,
		GLX_MATERIAL_TCGEN_ENVIRONMENT_MAPPED_FP,
		GLX_MATERIAL_TCGEN_FOG,
		GLX_MATERIAL_TCGEN_VECTOR
	};
	const int waveFuncs[] = {
		GLX_MATERIAL_WAVEFUNC_NONE,
		GLX_MATERIAL_WAVEFUNC_SIN,
		GLX_MATERIAL_WAVEFUNC_SQUARE,
		GLX_MATERIAL_WAVEFUNC_TRIANGLE,
		GLX_MATERIAL_WAVEFUNC_SAWTOOTH,
		GLX_MATERIAL_WAVEFUNC_INVERSE_SAWTOOTH,
		GLX_MATERIAL_WAVEFUNC_NOISE
	};
	const unsigned int texModTypes[] = {
		GLX_MATERIAL_TMOD_NONE_BIT,
		GLX_MATERIAL_TMOD_TRANSFORM_BIT,
		GLX_MATERIAL_TMOD_TURBULENT_BIT,
		GLX_MATERIAL_TMOD_SCROLL_BIT,
		GLX_MATERIAL_TMOD_SCALE_BIT,
		GLX_MATERIAL_TMOD_STRETCH_BIT,
		GLX_MATERIAL_TMOD_ROTATE_BIT,
		GLX_MATERIAL_TMOD_ENTITY_TRANSLATE_BIT,
		GLX_MATERIAL_TMOD_OFFSET_BIT,
		GLX_MATERIAL_TMOD_SCALE_OFFSET_BIT,
		GLX_MATERIAL_TMOD_OFFSET_SCALE_BIT
	};
	const int sceneFlags[] = {
		GLX_STAGE_BLEND,
		GLX_STAGE_ALPHA_TEST,
		GLX_STAGE_DEPTH_WRITE,
		GLX_STAGE_LIGHTMAP,
		GLX_STAGE_ANIMATED_IMAGE,
		GLX_STAGE_VIDEO_MAP,
		GLX_STAGE_SCREEN_MAP,
		GLX_STAGE_DLIGHT_MAP,
		GLX_STAGE_DETAIL,
		GLX_STAGE_SHADOW_PASS,
		GLX_STAGE_BEAM_PASS,
		GLX_STAGE_POSTPROCESS_PASS
	};
	const int fogAdjusts[] = {
		GLX_MATERIAL_FOG_ADJUST_NONE,
		GLX_MATERIAL_FOG_ADJUST_MODULATE_RGB,
		GLX_MATERIAL_FOG_ADJUST_MODULATE_RGBA,
		GLX_MATERIAL_FOG_ADJUST_MODULATE_ALPHA
	};
	const unsigned int stateBits[] = {
		GLX_MATERIAL_STATE_SRCBLEND_ZERO | GLX_MATERIAL_STATE_DSTBLEND_ONE,
		GLX_MATERIAL_STATE_SRCBLEND_ONE | GLX_MATERIAL_STATE_DSTBLEND_ZERO,
		GLX_MATERIAL_STATE_SRCBLEND_DST_COLOR | GLX_MATERIAL_STATE_DSTBLEND_SRC_COLOR,
		GLX_MATERIAL_STATE_SRCBLEND_ONE_MINUS_DST_COLOR |
			GLX_MATERIAL_STATE_DSTBLEND_ONE_MINUS_SRC_COLOR,
		GLX_MATERIAL_STATE_SRCBLEND_SRC_ALPHA | GLX_MATERIAL_STATE_DSTBLEND_SRC_ALPHA,
		GLX_MATERIAL_STATE_SRCBLEND_ONE_MINUS_SRC_ALPHA |
			GLX_MATERIAL_STATE_DSTBLEND_ONE_MINUS_SRC_ALPHA,
		GLX_MATERIAL_STATE_SRCBLEND_DST_ALPHA | GLX_MATERIAL_STATE_DSTBLEND_DST_ALPHA,
		GLX_MATERIAL_STATE_SRCBLEND_ONE_MINUS_DST_ALPHA |
			GLX_MATERIAL_STATE_DSTBLEND_ONE_MINUS_DST_ALPHA,
		GLX_MATERIAL_STATE_SRCBLEND_ALPHA_SATURATE | GLX_MATERIAL_STATE_DSTBLEND_ONE,
		GLX_MATERIAL_STATE_ATEST_GT_0,
		GLX_MATERIAL_STATE_ATEST_LT_80,
		GLX_MATERIAL_STATE_ATEST_GE_80,
		GLX_MATERIAL_STATE_DEPTHMASK_TRUE,
		GLX_MATERIAL_STATE_POLYMODE_LINE,
		GLX_MATERIAL_STATE_DEPTHTEST_DISABLE,
		GLX_MATERIAL_STATE_DEPTHFUNC_EQUAL
	};

	for ( int rgbGen : rgbGens ) {
		MaterialStageKey stageKey {};

		if ( !GLX_Material_DefaultStageKeyForProgram( MaterialProgramMode::SingleTexture,
			GLX_MATERIAL_FEATURE_NONE, &stageKey ) ) {
			ok = qfalse;
			continue;
		}
		stageKey.rgbGen = rgbGen;
		if ( !GLX_Material_PrecacheStageKey( state, stageKey ) ) {
			ok = qfalse;
		}
	}

	for ( int alphaGen : alphaGens ) {
		MaterialStageKey stageKey {};

		if ( !GLX_Material_DefaultStageKeyForProgram( MaterialProgramMode::SingleTexture,
			GLX_MATERIAL_FEATURE_NONE, &stageKey ) ) {
			ok = qfalse;
			continue;
		}
		stageKey.alphaGen = alphaGen;
		if ( !GLX_Material_PrecacheStageKey( state, stageKey ) ) {
			ok = qfalse;
		}
	}

	for ( int tcGen : tcGens ) {
		MaterialStageKey stageKey {};

		if ( !GLX_Material_DefaultStageKeyForProgram( MaterialProgramMode::SingleTexture,
			GLX_MATERIAL_FEATURE_NONE, &stageKey ) ) {
			ok = qfalse;
			continue;
		}
		stageKey.tcGen0 = tcGen;
		if ( !GLX_Material_PrecacheStageKey( state, stageKey ) ) {
			ok = qfalse;
		}
	}

	for ( int waveFunc : waveFuncs ) {
		MaterialStageKey stageKey {};

		if ( !GLX_Material_DefaultStageKeyForProgram( MaterialProgramMode::SingleTexture,
			GLX_MATERIAL_FEATURE_NONE, &stageKey ) ) {
			ok = qfalse;
			continue;
		}
		stageKey.rgbGen = GLX_MATERIAL_RGBGEN_WAVEFORM;
		stageKey.rgbWaveFunc = waveFunc;
		if ( !GLX_Material_PrecacheStageKey( state, stageKey ) ) {
			ok = qfalse;
		}
	}

	for ( int waveFunc : waveFuncs ) {
		MaterialStageKey stageKey {};

		if ( !GLX_Material_DefaultStageKeyForProgram( MaterialProgramMode::SingleTexture,
			GLX_MATERIAL_FEATURE_NONE, &stageKey ) ) {
			ok = qfalse;
			continue;
		}
		stageKey.alphaGen = GLX_MATERIAL_ALPHAGEN_WAVEFORM;
		stageKey.alphaWaveFunc = waveFunc;
		if ( !GLX_Material_PrecacheStageKey( state, stageKey ) ) {
			ok = qfalse;
		}
	}

	for ( unsigned int texModType : texModTypes ) {
		MaterialStageKey stageKey {};

		if ( !GLX_Material_DefaultStageKeyForProgram( MaterialProgramMode::SingleTexture,
			GLX_MATERIAL_FEATURE_TEXMOD, &stageKey ) ) {
			ok = qfalse;
			continue;
		}
		stageKey.texModTypes0 = texModType;
		stageKey.texModSequence0 = GLX_Material_DefaultTexModSequence( stageKey.texMods0,
			stageKey.texModTypes0 );
		if ( !GLX_Material_PrecacheStageKey( state, stageKey ) ) {
			ok = qfalse;
		}
	}

	for ( int waveFunc : waveFuncs ) {
		MaterialStageKey stageKey {};

		if ( !GLX_Material_DefaultStageKeyForProgram( MaterialProgramMode::SingleTexture,
			GLX_MATERIAL_FEATURE_TEXMOD, &stageKey ) ) {
			ok = qfalse;
			continue;
		}
		stageKey.texModTypes0 = GLX_MATERIAL_TMOD_STRETCH_BIT;
		stageKey.texModSequence0 = GLX_Material_TexModSequenceSetSlot( 0, 0,
			GLX_MATERIAL_TMOD_OPCODE_STRETCH );
		stageKey.texModWaveFuncs0 = GLX_Material_TexModWaveFuncSetSlot( 0, 0,
			static_cast<unsigned int>( waveFunc ) );
		if ( !GLX_Material_PrecacheStageKey( state, stageKey ) ) {
			ok = qfalse;
		}
	}

	for ( unsigned int texModType : texModTypes ) {
		MaterialStageKey stageKey {};

		if ( !GLX_Material_DefaultStageKeyForProgram( MaterialProgramMode::MultiModulate,
			GLX_MATERIAL_FEATURE_TEXMOD, &stageKey ) ) {
			ok = qfalse;
			continue;
		}
		stageKey.texMods0 = 0;
		stageKey.texModTypes0 = 0;
		stageKey.texModSequence0 = 0;
		stageKey.texMods1 = 1;
		stageKey.texModTypes1 = texModType;
		stageKey.texModSequence1 = GLX_Material_DefaultTexModSequence( stageKey.texMods1,
			stageKey.texModTypes1 );
		if ( !GLX_Material_PrecacheStageKey( state, stageKey ) ) {
			ok = qfalse;
		}
	}

	{
		MaterialStageKey scrollScale {};
		MaterialStageKey scaleScroll {};
		const unsigned int mask = GLX_MATERIAL_TMOD_SCROLL_BIT | GLX_MATERIAL_TMOD_SCALE_BIT;

		if ( !GLX_Material_DefaultStageKeyForProgram( MaterialProgramMode::SingleTexture,
			GLX_MATERIAL_FEATURE_TEXMOD, &scrollScale ) ||
			!GLX_Material_DefaultStageKeyForProgram( MaterialProgramMode::SingleTexture,
			GLX_MATERIAL_FEATURE_TEXMOD, &scaleScroll ) ) {
			ok = qfalse;
		} else {
			scrollScale.texMods0 = 2;
			scrollScale.texModTypes0 = mask;
			scrollScale.texModSequence0 = GLX_Material_TexModSequenceSetSlot( 0, 0,
				GLX_MATERIAL_TMOD_OPCODE_SCROLL );
			scrollScale.texModSequence0 = GLX_Material_TexModSequenceSetSlot(
				scrollScale.texModSequence0, 1, GLX_MATERIAL_TMOD_OPCODE_SCALE );

			scaleScroll.texMods0 = 2;
			scaleScroll.texModTypes0 = mask;
			scaleScroll.texModSequence0 = GLX_Material_TexModSequenceSetSlot( 0, 0,
				GLX_MATERIAL_TMOD_OPCODE_SCALE );
			scaleScroll.texModSequence0 = GLX_Material_TexModSequenceSetSlot(
				scaleScroll.texModSequence0, 1, GLX_MATERIAL_TMOD_OPCODE_SCROLL );

			if ( !GLX_Material_PrecacheStageKey( state, scrollScale ) ||
				!GLX_Material_PrecacheStageKey( state, scaleScroll ) ) {
				ok = qfalse;
			}
		}
	}

	for ( int fogAdjust : fogAdjusts ) {
		MaterialStageKey stageKey {};

		if ( !GLX_Material_DefaultStageKeyForProgram( MaterialProgramMode::SingleTexture,
			GLX_MATERIAL_FEATURE_NONE, &stageKey ) ) {
			ok = qfalse;
			continue;
		}
		stageKey.fogAdjust = fogAdjust;
		if ( !GLX_Material_PrecacheStageKey( state, stageKey ) ) {
			ok = qfalse;
		}
	}

	for ( int flag : sceneFlags ) {
		MaterialStageKey stageKey {};

		if ( !GLX_Material_DefaultStageKeyForProgram( MaterialProgramMode::SingleTexture,
			GLX_MATERIAL_FEATURE_NONE, &stageKey ) ) {
			ok = qfalse;
			continue;
		}
		stageKey.flags |= flag;
		if ( flag == GLX_STAGE_LIGHTMAP ) {
			stageKey.tcGen0 = GLX_MATERIAL_TCGEN_LIGHTMAP;
		}
		if ( !GLX_Material_PrecacheStageKey( state, stageKey ) ) {
			ok = qfalse;
		}
	}

	for ( unsigned int stateBitsValue : stateBits ) {
		MaterialStageKey stageKey {};

		if ( !GLX_Material_DefaultStageKeyForProgram( MaterialProgramMode::SingleTexture,
			GLX_MATERIAL_FEATURE_NONE, &stageKey ) ) {
			ok = qfalse;
			continue;
		}
		stageKey.stateBits = stateBitsValue;
		GLX_Material_ApplyFlagsForStateBits( &stageKey );
		if ( !GLX_Material_PrecacheStageKey( state, stageKey ) ) {
			ok = qfalse;
		}
	}

	return ok;
}

static qboolean GLX_Material_PrecachePrograms( MaterialState *state )
{
	qboolean ok = qtrue;
	const size_t modeCount = sizeof( kMaterialPrecacheModes ) / sizeof( kMaterialPrecacheModes[0] );

	if ( !state ) {
		return qfalse;
	}

	state->precacheAttempts++;
	for ( size_t i = 0; i < modeCount; i++ ) {
		const MaterialProgramMode mode = kMaterialPrecacheModes[i];
		const unsigned int *features = kMaterialPrecacheFeatures;
		size_t featureCount = sizeof( kMaterialPrecacheFeatures ) / sizeof( kMaterialPrecacheFeatures[0] );

		if ( mode == MaterialProgramMode::Fog ) {
			featureCount = 1u;
		}

		for ( size_t featureIndex = 0; featureIndex < featureCount; featureIndex++ ) {
			MaterialStageKey stageKey {};

			if ( !GLX_Material_DefaultStageKeyForProgram( mode, features[featureIndex],
				&stageKey ) ||
				!GLX_Material_PrecacheStageKey( state, stageKey ) ) {
				ok = qfalse;
			}
		}
	}

	if ( !GLX_Material_PrecacheLanguageCoverage( state ) ) {
		ok = qfalse;
	}

	if ( !ok ) {
		state->precacheFailures++;
	}

	return ok;
}

void GLX_Material_RegisterCvars( MaterialState *state )
{
	if ( !state ) {
		return;
	}

	state->r_glxMaterialRenderer = RI().Cvar_Get( "r_glxMaterialRenderer", "1", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxMaterialRenderer,
		"Use the independent GLx GLSL material renderer for GLx streamed draws when available." );

	state->r_glxMaterialDebug = RI().Cvar_Get( "r_glxMaterialDebug", "0", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxMaterialDebug,
		"Print GLx material renderer diagnostics. Set to 2 to also dump failed GLSL source." );

	state->r_glxMaterialPrecache = RI().Cvar_Get( "r_glxMaterialPrecache", "1", CVAR_ARCHIVE_ND | CVAR_DEVELOPER );
	RI().Cvar_SetDescription( state->r_glxMaterialPrecache,
		"Compile the built-in GLx GLSL material stage-language coverage during OpenGL startup." );
}

void GLX_Material_OnOpenGLReady( MaterialState *state, const Capabilities &caps )
{
	const GLubyte *version;
	PFNGLXGETSTRINGPROC GetString;

	if ( !state ) {
		return;
	}

	GLX_Material_Shutdown( state, qtrue );
	GLX_Material_ResetCounters( state );
	state->tier = caps.tier;
	GLX_Material_SetReason( state, "not initialized" );
	state->lastKey = { MaterialProgramMode::SingleTexture, GLX_MATERIAL_FEATURE_NONE };
	state->lastStageKey = {};

	GetString = reinterpret_cast<PFNGLXGETSTRINGPROC>( GLX_Material_GetProc( "glGetString" ) );
	version = GetString ? GetString( GL_SHADING_LANGUAGE_VERSION ) : nullptr;
	std::snprintf( state->glslVersion, sizeof( state->glslVersion ), "%s", version ? reinterpret_cast<const char *>( version ) : "unknown" );
	state->glslVersion[sizeof( state->glslVersion ) - 1] = '\0';

	if ( caps.tier == RenderProductTier::GL12 ) {
		GLX_Material_SetReason( state, "GL12 fixed-function tier has no GLSL material compiler" );
		return;
	}

	GLX_Material_LoadFunctions( state );
	if ( !GLX_Material_FunctionsReady( *state ) ) {
		GLX_Material_SetReason( state, "required GLSL program functions are unavailable" );
		return;
	}

	state->ready = qtrue;
	if ( ( !state->r_glxMaterialPrecache || state->r_glxMaterialPrecache->integer ) &&
		!GLX_Material_PrecachePrograms( state ) ) {
		state->ready = qfalse;
		GLX_Material_SetReason( state, "GLSL material program precache failed" );
		return;
	}

	GLX_Material_SetReason( state, "GLSL material program path ready" );
}

void GLX_Material_Shutdown( MaterialState *state, qboolean deletePrograms )
{
	if ( !state ) {
		return;
	}

	GLX_Material_ResetRuntime( state, deletePrograms );
}

void GLX_Material_FrameComplete( MaterialState *state )
{
	if ( state ) {
		state->frames++;
	}
}

qboolean GLX_Material_Active( const MaterialState &state )
{
	return state.ready && state.r_glxMaterialRenderer && state.r_glxMaterialRenderer->integer ? qtrue : qfalse;
}

qboolean GLX_Material_BindIR( MaterialState *state, const MaterialIR &material )
{
	MaterialStatePlan plan {};
	MaterialProgram *program;
	unsigned int unsupportedReasons = GLX_MATERIAL_UNSUPPORTED_NONE;

	if ( !state ) {
		return qfalse;
	}

	state->bindAttempts++;
	state->lastMaterial = material;
	state->lastParameterBlock = GLX_RenderIR_MakeMaterialParameterBlock( material );
	state->lastParameterBlockHash =
		GLX_RenderIR_HashMaterialParameterBlock( state->lastParameterBlock );
	state->lastStageKey = {};
	state->lastUnsupportedReasons = GLX_MATERIAL_UNSUPPORTED_NONE;
	state->parameterBlocks++;

	if ( !GLX_RenderIR_ValidateMaterialParameterBlock( state->lastParameterBlock ) ) {
		state->invalidParameterBlocks++;
		state->unsupportedRequests++;
		GLX_Material_SetLastError( state, "invalid material parameter block" );
		return qfalse;
	}

	if ( !state->r_glxMaterialRenderer || !state->r_glxMaterialRenderer->integer ) {
		state->disabledSkips++;
		return qfalse;
	}
	if ( !state->ready ) {
		state->notReadySkips++;
		return qfalse;
	}
	if ( !GLX_Material_StatePlanForTierAndParameterBlock( state->tier,
		state->lastParameterBlock, &plan,
		&unsupportedReasons ) ) {
		state->lastUnsupportedReasons = unsupportedReasons;
		state->unsupportedMaterialPlans++;
		state->unsupportedRequests++;
		GLX_Material_SetLastError( state,
			GLX_Material_UnsupportedReasonName( unsupportedReasons ) );
		return qfalse;
	}

	state->lastKey = plan.stage.program;
	state->lastStageKey = plan.stage;
	state->compiledMaterialPlans++;
	program = GLX_Material_FindProgram( state, plan.stage );
	if ( !program ) {
		program = GLX_Material_CreateProgram( state, plan.stage );
	}
	if ( !program || !program->valid ) {
		state->bindFailures++;
		if ( !state->lastError[0] ) {
			GLX_Material_SetLastError( state, "material program unavailable" );
		}
		return qfalse;
	}

	if ( state->currentProgram != program->program ) {
		state->fns.UseProgram( program->program );
		state->currentProgram = program->program;
		state->programSwitches++;
	}

	program->binds++;
	state->binds++;
	GLX_Material_SetLastError( state, "" );
	return qtrue;
}

qboolean GLX_Material_BindStage( MaterialState *state, const MaterialRequest &request )
{
	if ( state ) {
		state->lastRequest = request;
	}
	return GLX_Material_BindIR( state, GLX_Material_IRForRequest( request ) );
}

qboolean GLX_Material_BindFog( MaterialState *state )
{
	MaterialRequest request {};

	request.rgbGen = GLX_MATERIAL_RGBGEN_FOG;
	request.alphaGen = GLX_MATERIAL_ALPHAGEN_IDENTITY;
	request.tcGen0 = GLX_MATERIAL_TCGEN_FOG;
	request.tcGen1 = GLX_MATERIAL_TCGEN_BAD;
	request.fogPass = qtrue;
	return GLX_Material_BindStage( state, request );
}

void GLX_Material_Unbind( MaterialState *state )
{
	if ( !state || !state->currentProgram || !state->fns.UseProgram ) {
		return;
	}

	state->fns.UseProgram( 0 );
	state->currentProgram = 0;
	state->unbinds++;
}

const char *GLX_Material_ModeName( MaterialProgramMode mode )
{
	switch ( mode ) {
	case MaterialProgramMode::SingleTexture:
		return "single";
	case MaterialProgramMode::MultiModulate:
		return "multi-modulate";
	case MaterialProgramMode::MultiAdd:
		return "multi-add";
	case MaterialProgramMode::MultiReplace:
		return "multi-replace";
	case MaterialProgramMode::MultiDecal:
		return "multi-decal";
	case MaterialProgramMode::Fog:
		return "fog";
	default:
		return "unknown";
	}
}

void GLX_Material_PrintInfo( const MaterialState &state )
{
	char lastKeyName[64];

	GLX_Material_KeyName( state.lastKey, lastKeyName, sizeof( lastKeyName ) );

	RI().Printf( PRINT_ALL, "  material renderer: %s, ready %s, GLSL %s\n",
		state.r_glxMaterialRenderer && state.r_glxMaterialRenderer->integer ? "enabled" : "disabled",
		BoolName( state.ready ), state.glslVersion[0] ? state.glslVersion : "unknown" );
	RI().Printf( PRINT_ALL, "  material reason: %s\n", state.reason[0] ? state.reason : "none" );
	RI().Printf( PRINT_ALL, "  material programs: %i/%i, attempts %u, binds %u, switches %u, unbinds %u, cache %u hits/%u misses\n",
		state.programCount, GLX_MATERIAL_PROGRAM_LIMIT, state.bindAttempts, state.binds,
		state.programSwitches, state.unbinds, state.cacheHits, state.cacheMisses );
	RI().Printf( PRINT_ALL, "  material compiles: %u attempts, %u compile failures, %u link failures, precache %u/%u, bind failures %u, labels %u\n",
		state.compileAttempts, state.compileFailures, state.linkFailures,
		state.precacheFailures, state.precacheAttempts, state.bindFailures, state.debugLabels );
	RI().Printf( PRINT_ALL, "  material fallbacks: unsupported %u, disabled %u, not-ready %u, full %u, discarded without GL delete %u\n",
		state.unsupportedRequests, state.disabledSkips, state.notReadySkips,
		state.programLimitSkips, state.contextlessDeletes );
	RI().Printf( PRINT_ALL, "  material compiler plans: compiled %u, unsupported %u, last unsupported 0x%x (%s)\n",
		state.compiledMaterialPlans, state.unsupportedMaterialPlans,
		state.lastUnsupportedReasons,
		GLX_Material_UnsupportedReasonName( state.lastUnsupportedReasons ) );
	RI().Printf( PRINT_ALL, "  material parameter blocks: blocks %u invalid %u hash 0x%08x, last sort %i passes %i features 0x%x flags 0x%x state 0x%x object rgb %i:%i alpha %i:%i tc %i/%i\n",
		state.parameterBlocks,
		state.invalidParameterBlocks,
		state.lastParameterBlockHash,
		state.lastParameterBlock.frame.sort,
		state.lastParameterBlock.frame.shaderStagePasses,
		state.lastParameterBlock.frame.featureMask,
		state.lastParameterBlock.material.flags,
		state.lastParameterBlock.material.stateBits,
		state.lastParameterBlock.object.rgbGen,
		state.lastParameterBlock.object.rgbWaveFunc,
		state.lastParameterBlock.object.alphaGen,
		state.lastParameterBlock.object.alphaWaveFunc,
		state.lastParameterBlock.object.tcGen0,
		state.lastParameterBlock.object.tcGen1 );
	RI().Printf( PRINT_ALL, "  material last key: %s, flags 0x%x, state 0x%x, rgb %i:%i alpha %i:%i tc %i/%i texmods %i/%i combine %i fog %s\n",
		lastKeyName, state.lastMaterial.flags, state.lastMaterial.stateBits,
		state.lastMaterial.rgbGen, state.lastMaterial.rgbWaveFunc,
		state.lastMaterial.alphaGen, state.lastMaterial.alphaWaveFunc, state.lastMaterial.tcGen0,
		state.lastMaterial.tcGen1, state.lastMaterial.texMods0, state.lastMaterial.texMods1,
		state.lastMaterial.materialCombine, BoolName( state.lastMaterial.fogPass ) );
	RI().Printf( PRINT_ALL, "  material last language: flags 0x%x, state 0x%x, tmasks 0x%x/0x%x, tseq 0x%x/0x%x, twf 0x%x/0x%x, fogadjust %i\n",
		state.lastStageKey.flags, state.lastStageKey.stateBits,
		state.lastStageKey.texModTypes0, state.lastStageKey.texModTypes1,
		state.lastStageKey.texModSequence0, state.lastStageKey.texModSequence1,
		state.lastStageKey.texModWaveFuncs0, state.lastStageKey.texModWaveFuncs1,
		state.lastStageKey.fogAdjust );
	if ( state.lastError[0] ) {
		RI().Printf( PRINT_ALL, "  material last error: %s\n", state.lastError );
	}

	for ( int i = 0; i < state.programCount; i++ ) {
		const MaterialProgram &program = state.programs[i];
		char keyName[192];

		if ( !program.valid ) {
			continue;
		}
		GLX_Material_StageKeyName( program.stageKey, keyName, sizeof( keyName ) );
		RI().Printf( PRINT_ALL, "    program %u: %s, binds %u\n",
			program.program, keyName, program.binds );
	}
}

} // namespace glx
