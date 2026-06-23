#ifndef GLX_MATERIAL_KEY_H
#define GLX_MATERIAL_KEY_H

#include "../qcommon/q_shared.h"
#include "../renderercommon/tr_glx_public.h"
#include "glx_render_ir.h"

namespace glx {

static constexpr unsigned int GLX_MATERIAL_FEATURE_NONE = 0x0000;
static constexpr unsigned int GLX_MATERIAL_FEATURE_TEXMOD = 0x0001;
static constexpr unsigned int GLX_MATERIAL_FEATURE_ENVIRONMENT = 0x0002;
static constexpr unsigned int GLX_MATERIAL_FEATURE_DEPTH_FRAGMENT = 0x0004;

enum class MaterialProgramMode {
	SingleTexture,
	MultiModulate,
	MultiAdd,
	MultiReplace,
	MultiDecal,
	Fog
};

struct MaterialProgramKey {
	MaterialProgramMode mode;
	unsigned int features;
};

struct MaterialStageKey {
	MaterialProgramKey program;
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
	qboolean fogPass;
};

struct MaterialStatePlan {
	RenderProductTier tier;
	qboolean programmable;
	int sort;
	MaterialStageKey stage;
};

static constexpr unsigned int GLX_MATERIAL_UNSUPPORTED_NONE = 0x00000000u;
static constexpr unsigned int GLX_MATERIAL_UNSUPPORTED_INVALID_IR = 0x00000001u;
static constexpr unsigned int GLX_MATERIAL_UNSUPPORTED_INVALID_COMBINE = 0x00000002u;
static constexpr unsigned int GLX_MATERIAL_UNSUPPORTED_FEATURE_SET = 0x00000004u;
static constexpr unsigned int GLX_MATERIAL_UNSUPPORTED_RGBGEN = 0x00000008u;
static constexpr unsigned int GLX_MATERIAL_UNSUPPORTED_ALPHAGEN = 0x00000010u;
static constexpr unsigned int GLX_MATERIAL_UNSUPPORTED_RGB_WAVE = 0x00000020u;
static constexpr unsigned int GLX_MATERIAL_UNSUPPORTED_ALPHA_WAVE = 0x00000040u;
static constexpr unsigned int GLX_MATERIAL_UNSUPPORTED_TCGEN = 0x00000080u;
static constexpr unsigned int GLX_MATERIAL_UNSUPPORTED_FOG_ADJUST = 0x00000100u;
static constexpr unsigned int GLX_MATERIAL_UNSUPPORTED_TEXMOD_COUNT = 0x00000200u;
static constexpr unsigned int GLX_MATERIAL_UNSUPPORTED_TEXMOD_MASK = 0x00000400u;
static constexpr unsigned int GLX_MATERIAL_UNSUPPORTED_TEXMOD_EMPTY = 0x00000800u;
static constexpr unsigned int GLX_MATERIAL_UNSUPPORTED_TEXMOD_SEQUENCE = 0x00001000u;
static constexpr unsigned int GLX_MATERIAL_UNSUPPORTED_TEXMOD_WAVE = 0x00002000u;

static constexpr int GLX_MATERIAL_MAX_TEXMODS_PER_BUNDLE =
	GLX_MATERIAL_TMOD_SEQUENCE_MAX_SLOTS;

static ID_INLINE const char *GLX_Material_UnsupportedReasonName( unsigned int reasons )
{
	if ( reasons == GLX_MATERIAL_UNSUPPORTED_NONE ) {
		return "none";
	}
	if ( reasons & GLX_MATERIAL_UNSUPPORTED_INVALID_IR ) {
		return "invalid material IR";
	}
	if ( reasons & GLX_MATERIAL_UNSUPPORTED_INVALID_COMBINE ) {
		return "invalid multitexture combine";
	}
	if ( reasons & GLX_MATERIAL_UNSUPPORTED_FEATURE_SET ) {
		return "unsupported material feature set";
	}
	if ( reasons & GLX_MATERIAL_UNSUPPORTED_RGBGEN ) {
		return "unknown rgbGen";
	}
	if ( reasons & GLX_MATERIAL_UNSUPPORTED_ALPHAGEN ) {
		return "unknown alphaGen";
	}
	if ( reasons & GLX_MATERIAL_UNSUPPORTED_RGB_WAVE ) {
		return "invalid rgb waveform";
	}
	if ( reasons & GLX_MATERIAL_UNSUPPORTED_ALPHA_WAVE ) {
		return "invalid alpha waveform";
	}
	if ( reasons & GLX_MATERIAL_UNSUPPORTED_TCGEN ) {
		return "unknown tcGen";
	}
	if ( reasons & GLX_MATERIAL_UNSUPPORTED_FOG_ADJUST ) {
		return "unknown fog adjustment";
	}
	if ( reasons & GLX_MATERIAL_UNSUPPORTED_TEXMOD_COUNT ) {
		return "unsupported texmod count";
	}
	if ( reasons & GLX_MATERIAL_UNSUPPORTED_TEXMOD_MASK ) {
		return "unknown texmod opcode";
	}
	if ( reasons & GLX_MATERIAL_UNSUPPORTED_TEXMOD_EMPTY ) {
		return "missing texmod opcode";
	}
	if ( reasons & GLX_MATERIAL_UNSUPPORTED_TEXMOD_SEQUENCE ) {
		return "invalid texmod sequence";
	}
	if ( reasons & GLX_MATERIAL_UNSUPPORTED_TEXMOD_WAVE ) {
		return "invalid texmod waveform";
	}
	return "unsupported material language";
}

static ID_INLINE qboolean GLX_Material_IsKnownRgbGen( int rgbGen )
{
	switch ( rgbGen ) {
	case GLX_MATERIAL_RGBGEN_BAD:
	case GLX_MATERIAL_RGBGEN_IDENTITY_LIGHTING:
	case GLX_MATERIAL_RGBGEN_IDENTITY:
	case GLX_MATERIAL_RGBGEN_ENTITY:
	case GLX_MATERIAL_RGBGEN_ONE_MINUS_ENTITY:
	case GLX_MATERIAL_RGBGEN_EXACT_VERTEX:
	case GLX_MATERIAL_RGBGEN_VERTEX:
	case GLX_MATERIAL_RGBGEN_ONE_MINUS_VERTEX:
	case GLX_MATERIAL_RGBGEN_WAVEFORM:
	case GLX_MATERIAL_RGBGEN_LIGHTING_DIFFUSE:
	case GLX_MATERIAL_RGBGEN_FOG:
	case GLX_MATERIAL_RGBGEN_CONST:
		return qtrue;
	default:
		return qfalse;
	}
}

static ID_INLINE qboolean GLX_Material_IsKnownAlphaGen( int alphaGen )
{
	switch ( alphaGen ) {
	case GLX_MATERIAL_ALPHAGEN_IDENTITY:
	case GLX_MATERIAL_ALPHAGEN_SKIP:
	case GLX_MATERIAL_ALPHAGEN_ENTITY:
	case GLX_MATERIAL_ALPHAGEN_ONE_MINUS_ENTITY:
	case GLX_MATERIAL_ALPHAGEN_VERTEX:
	case GLX_MATERIAL_ALPHAGEN_ONE_MINUS_VERTEX:
	case GLX_MATERIAL_ALPHAGEN_LIGHTING_SPECULAR:
	case GLX_MATERIAL_ALPHAGEN_WAVEFORM:
	case GLX_MATERIAL_ALPHAGEN_PORTAL:
	case GLX_MATERIAL_ALPHAGEN_CONST:
		return qtrue;
	default:
		return qfalse;
	}
}

static ID_INLINE qboolean GLX_Material_IsKnownTcGen( int tcGen )
{
	switch ( tcGen ) {
	case GLX_MATERIAL_TCGEN_BAD:
	case GLX_MATERIAL_TCGEN_IDENTITY:
	case GLX_MATERIAL_TCGEN_LIGHTMAP:
	case GLX_MATERIAL_TCGEN_TEXTURE:
	case GLX_MATERIAL_TCGEN_ENVIRONMENT_MAPPED:
	case GLX_MATERIAL_TCGEN_ENVIRONMENT_MAPPED_FP:
	case GLX_MATERIAL_TCGEN_FOG:
	case GLX_MATERIAL_TCGEN_VECTOR:
		return qtrue;
	default:
		return qfalse;
	}
}

static ID_INLINE qboolean GLX_Material_IsKnownWaveFunc( int waveFunc )
{
	switch ( waveFunc ) {
	case GLX_MATERIAL_WAVEFUNC_NONE:
	case GLX_MATERIAL_WAVEFUNC_SIN:
	case GLX_MATERIAL_WAVEFUNC_SQUARE:
	case GLX_MATERIAL_WAVEFUNC_TRIANGLE:
	case GLX_MATERIAL_WAVEFUNC_SAWTOOTH:
	case GLX_MATERIAL_WAVEFUNC_INVERSE_SAWTOOTH:
	case GLX_MATERIAL_WAVEFUNC_NOISE:
		return qtrue;
	default:
		return qfalse;
	}
}

static ID_INLINE qboolean GLX_Material_IsKnownFogAdjust( int fogAdjust )
{
	switch ( fogAdjust ) {
	case GLX_MATERIAL_FOG_ADJUST_NONE:
	case GLX_MATERIAL_FOG_ADJUST_MODULATE_RGB:
	case GLX_MATERIAL_FOG_ADJUST_MODULATE_RGBA:
	case GLX_MATERIAL_FOG_ADJUST_MODULATE_ALPHA:
		return qtrue;
	default:
		return qfalse;
	}
}

static ID_INLINE qboolean GLX_Material_TexModMaskKnown( unsigned int texModTypes )
{
	return ( texModTypes & ~GLX_MATERIAL_TMOD_KNOWN_BITS ) == 0 ? qtrue : qfalse;
}

static ID_INLINE qboolean GLX_Material_TexModCountKnown( int texMods )
{
	return texMods >= 0 && texMods <= GLX_MATERIAL_MAX_TEXMODS_PER_BUNDLE ? qtrue : qfalse;
}

static ID_INLINE unsigned int GLX_Material_TexModOpcodeBit( unsigned int opcode )
{
	switch ( opcode ) {
	case GLX_MATERIAL_TMOD_OPCODE_NONE:
		return GLX_MATERIAL_TMOD_NONE_BIT;
	case GLX_MATERIAL_TMOD_OPCODE_TRANSFORM:
		return GLX_MATERIAL_TMOD_TRANSFORM_BIT;
	case GLX_MATERIAL_TMOD_OPCODE_TURBULENT:
		return GLX_MATERIAL_TMOD_TURBULENT_BIT;
	case GLX_MATERIAL_TMOD_OPCODE_SCROLL:
		return GLX_MATERIAL_TMOD_SCROLL_BIT;
	case GLX_MATERIAL_TMOD_OPCODE_SCALE:
		return GLX_MATERIAL_TMOD_SCALE_BIT;
	case GLX_MATERIAL_TMOD_OPCODE_STRETCH:
		return GLX_MATERIAL_TMOD_STRETCH_BIT;
	case GLX_MATERIAL_TMOD_OPCODE_ROTATE:
		return GLX_MATERIAL_TMOD_ROTATE_BIT;
	case GLX_MATERIAL_TMOD_OPCODE_ENTITY_TRANSLATE:
		return GLX_MATERIAL_TMOD_ENTITY_TRANSLATE_BIT;
	case GLX_MATERIAL_TMOD_OPCODE_OFFSET:
		return GLX_MATERIAL_TMOD_OFFSET_BIT;
	case GLX_MATERIAL_TMOD_OPCODE_SCALE_OFFSET:
		return GLX_MATERIAL_TMOD_SCALE_OFFSET_BIT;
	case GLX_MATERIAL_TMOD_OPCODE_OFFSET_SCALE:
		return GLX_MATERIAL_TMOD_OFFSET_SCALE_BIT;
	default:
		return GLX_MATERIAL_TMOD_UNKNOWN_BIT;
	}
}

static ID_INLINE unsigned int GLX_Material_TexModSequenceSlot( unsigned int sequence, int slot )
{
	return ( sequence >> ( slot * GLX_MATERIAL_TMOD_SEQUENCE_SLOT_BITS ) ) &
		GLX_MATERIAL_TMOD_SEQUENCE_SLOT_MASK;
}

static ID_INLINE unsigned int GLX_Material_TexModWaveFuncSlot( unsigned int waveFuncs, int slot )
{
	return GLX_Material_TexModSequenceSlot( waveFuncs, slot );
}

static ID_INLINE unsigned int GLX_Material_TexModSequenceSetSlot( unsigned int sequence,
	int slot, unsigned int opcode )
{
	const unsigned int shift = static_cast<unsigned int>( slot * GLX_MATERIAL_TMOD_SEQUENCE_SLOT_BITS );
	const unsigned int mask = GLX_MATERIAL_TMOD_SEQUENCE_SLOT_MASK << shift;

	return ( sequence & ~mask ) |
		( ( opcode & GLX_MATERIAL_TMOD_SEQUENCE_SLOT_MASK ) << shift );
}

static ID_INLINE unsigned int GLX_Material_TexModWaveFuncSetSlot( unsigned int waveFuncs,
	int slot, unsigned int waveFunc )
{
	return GLX_Material_TexModSequenceSetSlot( waveFuncs, slot, waveFunc );
}

static ID_INLINE unsigned int GLX_Material_TexModSequenceMask( int texMods,
	unsigned int sequence )
{
	unsigned int mask = 0;

	if ( !GLX_Material_TexModCountKnown( texMods ) ) {
		return GLX_MATERIAL_TMOD_UNKNOWN_BIT;
	}

	for ( int i = 0; i < texMods; ++i ) {
		mask |= GLX_Material_TexModOpcodeBit( GLX_Material_TexModSequenceSlot( sequence, i ) );
	}

	return mask;
}

static ID_INLINE unsigned int GLX_Material_TexModSequenceTailMask( int texMods )
{
	unsigned int mask = 0;

	for ( int i = texMods; i < GLX_MATERIAL_MAX_TEXMODS_PER_BUNDLE; ++i ) {
		mask |= GLX_MATERIAL_TMOD_SEQUENCE_SLOT_MASK <<
			( i * GLX_MATERIAL_TMOD_SEQUENCE_SLOT_BITS );
	}

	return mask;
}

static ID_INLINE unsigned int GLX_Material_DefaultTexModSequence( int texMods,
	unsigned int texModTypes )
{
	unsigned int sequence = 0;
	int slot = 0;
	const unsigned int bits[] = {
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
	const unsigned int opcodes[] = {
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
		GLX_MATERIAL_TMOD_OPCODE_OFFSET_SCALE
	};

	if ( !GLX_Material_TexModCountKnown( texMods ) || texMods == 0 ) {
		return 0;
	}

	for ( size_t i = 0; i < sizeof( bits ) / sizeof( bits[0] ) && slot < texMods; ++i ) {
		if ( texModTypes & bits[i] ) {
			sequence = GLX_Material_TexModSequenceSetSlot( sequence, slot, opcodes[i] );
			slot++;
		}
	}

	return sequence;
}

static ID_INLINE unsigned int GLX_Material_DefaultTexModWaveFuncs( int texMods,
	unsigned int texModSequence )
{
	unsigned int waveFuncs = 0;

	if ( !GLX_Material_TexModCountKnown( texMods ) || texMods == 0 ) {
		return 0;
	}

	for ( int i = 0; i < texMods; ++i ) {
		const unsigned int opcode = GLX_Material_TexModSequenceSlot( texModSequence, i );
		if ( opcode == GLX_MATERIAL_TMOD_OPCODE_STRETCH ) {
			waveFuncs = GLX_Material_TexModWaveFuncSetSlot( waveFuncs, i,
				GLX_MATERIAL_WAVEFUNC_SIN );
		}
	}

	return waveFuncs;
}

static ID_INLINE qboolean GLX_Material_TexModSequenceKnown( int texMods,
	unsigned int texModTypes, unsigned int sequence )
{
	if ( !GLX_Material_TexModCountKnown( texMods ) ) {
		return qfalse;
	}
	if ( texMods == 0 ) {
		return sequence == 0 ? qtrue : qfalse;
	}
	if ( sequence & GLX_Material_TexModSequenceTailMask( texMods ) ) {
		return qfalse;
	}
	return GLX_Material_TexModSequenceMask( texMods, sequence ) == texModTypes ?
		qtrue : qfalse;
}

static ID_INLINE qboolean GLX_Material_TexModWaveFuncsKnown( int texMods,
	unsigned int texModSequence, unsigned int waveFuncs )
{
	if ( !GLX_Material_TexModCountKnown( texMods ) ) {
		return qfalse;
	}
	if ( texMods == 0 ) {
		return waveFuncs == 0 ? qtrue : qfalse;
	}
	if ( waveFuncs & GLX_Material_TexModSequenceTailMask( texMods ) ) {
		return qfalse;
	}
	for ( int i = 0; i < texMods; ++i ) {
		const unsigned int opcode = GLX_Material_TexModSequenceSlot( texModSequence, i );
		const unsigned int waveFunc = GLX_Material_TexModWaveFuncSlot( waveFuncs, i );
		if ( !GLX_Material_IsKnownWaveFunc( static_cast<int>( waveFunc ) ) ) {
			return qfalse;
		}
		if ( opcode != GLX_MATERIAL_TMOD_OPCODE_STRETCH &&
			waveFunc != GLX_MATERIAL_WAVEFUNC_NONE ) {
			return qfalse;
		}
	}
	return qtrue;
}

static ID_INLINE qboolean GLX_Material_GenWaveFuncKnown( int gen, int waveFunc, int waveformGen )
{
	if ( !GLX_Material_IsKnownWaveFunc( waveFunc ) ) {
		return qfalse;
	}
	if ( gen != waveformGen && waveFunc != GLX_MATERIAL_WAVEFUNC_NONE ) {
		return qfalse;
	}
	return qtrue;
}

static ID_INLINE unsigned int GLX_Material_StateUnknownBits( unsigned int stateBits )
{
	return stateBits & ~GLX_MATERIAL_STATE_KNOWN_BITS;
}

static ID_INLINE qboolean GLX_Material_FeaturesAllowedForMode( MaterialProgramMode mode,
	unsigned int features )
{
	const unsigned int singleTextureFeatures = GLX_MATERIAL_FEATURE_TEXMOD |
		GLX_MATERIAL_FEATURE_ENVIRONMENT | GLX_MATERIAL_FEATURE_DEPTH_FRAGMENT;
	const unsigned int multitextureFeatures = GLX_MATERIAL_FEATURE_TEXMOD |
		GLX_MATERIAL_FEATURE_ENVIRONMENT | GLX_MATERIAL_FEATURE_DEPTH_FRAGMENT;

	switch ( mode ) {
	case MaterialProgramMode::SingleTexture:
		return ( features & ~singleTextureFeatures ) == 0 ? qtrue : qfalse;
	case MaterialProgramMode::MultiModulate:
	case MaterialProgramMode::MultiAdd:
	case MaterialProgramMode::MultiReplace:
	case MaterialProgramMode::MultiDecal:
		return ( features & ~multitextureFeatures ) == 0 ? qtrue : qfalse;
	case MaterialProgramMode::Fog:
		return features == GLX_MATERIAL_FEATURE_NONE ? qtrue : qfalse;
	default:
		return qfalse;
	}
}

static ID_INLINE qboolean GLX_Material_ModeForInputs( int flags, int materialCombine,
	qboolean fogPass, MaterialProgramMode *mode )
{
	if ( !mode ) {
		return qfalse;
	}

	if ( fogPass ) {
		*mode = MaterialProgramMode::Fog;
		return qtrue;
	}

	if ( !( flags & GLX_STAGE_MULTITEXTURE ) ) {
		*mode = MaterialProgramMode::SingleTexture;
		return qtrue;
	}

	switch ( materialCombine ) {
	case GLX_MATERIAL_COMBINE_MODULATE:
		*mode = MaterialProgramMode::MultiModulate;
		return qtrue;
	case GLX_MATERIAL_COMBINE_ADD:
		*mode = MaterialProgramMode::MultiAdd;
		return qtrue;
	case GLX_MATERIAL_COMBINE_REPLACE:
		*mode = MaterialProgramMode::MultiReplace;
		return qtrue;
	case GLX_MATERIAL_COMBINE_DECAL:
		*mode = MaterialProgramMode::MultiDecal;
		return qtrue;
	default:
		return qfalse;
	}
}

static ID_INLINE unsigned int GLX_Material_FeaturesForInputs( int flags,
	int texMods0, int texMods1, qboolean fogPass )
{
	unsigned int features = GLX_MATERIAL_FEATURE_NONE;

	if ( fogPass ) {
		return GLX_MATERIAL_FEATURE_NONE;
	}

	if ( ( flags & GLX_STAGE_TEXMOD ) || texMods0 > 0 || texMods1 > 0 ) {
		features |= GLX_MATERIAL_FEATURE_TEXMOD;
	}
	if ( flags & GLX_STAGE_ENVIRONMENT ) {
		features |= GLX_MATERIAL_FEATURE_ENVIRONMENT;
	}
	if ( flags & GLX_STAGE_DEPTH_FRAGMENT ) {
		features |= GLX_MATERIAL_FEATURE_DEPTH_FRAGMENT;
	}

	return features;
}

static ID_INLINE qboolean GLX_Material_KeyForInputs( int flags, int materialCombine,
	int texMods0, int texMods1, qboolean fogPass, MaterialProgramKey *key )
{
	MaterialProgramMode mode;

	if ( !key || !GLX_Material_ModeForInputs( flags, materialCombine, fogPass, &mode ) ) {
		return qfalse;
	}

	key->mode = mode;
	key->features = GLX_Material_FeaturesForInputs( flags, texMods0, texMods1, fogPass );
	if ( !GLX_Material_FeaturesAllowedForMode( key->mode, key->features ) ) {
		return qfalse;
	}
	return qtrue;
}

static ID_INLINE unsigned int GLX_Material_UnsupportedReasonsForInputsFull(
	int flags, unsigned int stateBits, int materialCombine, int rgbGen, int alphaGen,
	int rgbWaveFunc, int alphaWaveFunc, int tcGen0, int tcGen1,
	int texMods0, int texMods1, unsigned int texModTypes0, unsigned int texModTypes1,
	unsigned int texModSequence0, unsigned int texModSequence1,
	unsigned int texModWaveFuncs0, unsigned int texModWaveFuncs1,
	int fogAdjust, qboolean fogPass )
{
	unsigned int reasons = GLX_MATERIAL_UNSUPPORTED_NONE;
	MaterialProgramMode mode;
	(void)stateBits;

	if ( !GLX_Material_ModeForInputs( flags, materialCombine, fogPass, &mode ) ) {
		reasons |= GLX_MATERIAL_UNSUPPORTED_INVALID_COMBINE;
	} else {
		const unsigned int features = GLX_Material_FeaturesForInputs( flags,
			texMods0, texMods1, fogPass );
		if ( !GLX_Material_FeaturesAllowedForMode( mode, features ) ) {
			reasons |= GLX_MATERIAL_UNSUPPORTED_FEATURE_SET;
		}
	}

	if ( fogPass ) {
		return reasons;
	}

	if ( !GLX_Material_IsKnownRgbGen( rgbGen ) ) {
		reasons |= GLX_MATERIAL_UNSUPPORTED_RGBGEN;
	}
	if ( !GLX_Material_IsKnownAlphaGen( alphaGen ) ) {
		reasons |= GLX_MATERIAL_UNSUPPORTED_ALPHAGEN;
	}
	if ( !GLX_Material_GenWaveFuncKnown( rgbGen, rgbWaveFunc,
		GLX_MATERIAL_RGBGEN_WAVEFORM ) ) {
		reasons |= GLX_MATERIAL_UNSUPPORTED_RGB_WAVE;
	}
	if ( !GLX_Material_GenWaveFuncKnown( alphaGen, alphaWaveFunc,
		GLX_MATERIAL_ALPHAGEN_WAVEFORM ) ) {
		reasons |= GLX_MATERIAL_UNSUPPORTED_ALPHA_WAVE;
	}
	if ( !GLX_Material_IsKnownTcGen( tcGen0 ) || !GLX_Material_IsKnownTcGen( tcGen1 ) ) {
		reasons |= GLX_MATERIAL_UNSUPPORTED_TCGEN;
	}
	if ( !GLX_Material_IsKnownFogAdjust( fogAdjust ) ) {
		reasons |= GLX_MATERIAL_UNSUPPORTED_FOG_ADJUST;
	}

	const qboolean texModCountKnown =
		GLX_Material_TexModCountKnown( texMods0 ) &&
		GLX_Material_TexModCountKnown( texMods1 ) ? qtrue : qfalse;
	const qboolean texModMaskKnown =
		GLX_Material_TexModMaskKnown( texModTypes0 ) &&
		GLX_Material_TexModMaskKnown( texModTypes1 ) ? qtrue : qfalse;

	if ( !texModCountKnown ) {
		reasons |= GLX_MATERIAL_UNSUPPORTED_TEXMOD_COUNT;
	}
	if ( !texModMaskKnown ) {
		reasons |= GLX_MATERIAL_UNSUPPORTED_TEXMOD_MASK;
	}
	if ( texModCountKnown && ( ( texMods0 > 0 && texModTypes0 == 0 ) ||
		( texMods1 > 0 && texModTypes1 == 0 ) ) ) {
		reasons |= GLX_MATERIAL_UNSUPPORTED_TEXMOD_EMPTY;
	}
	if ( texModCountKnown && texModMaskKnown &&
		( !GLX_Material_TexModSequenceKnown( texMods0, texModTypes0,
			texModSequence0 ) ||
		!GLX_Material_TexModSequenceKnown( texMods1, texModTypes1,
			texModSequence1 ) ) ) {
		reasons |= GLX_MATERIAL_UNSUPPORTED_TEXMOD_SEQUENCE;
	}
	if ( texModCountKnown &&
		( !GLX_Material_TexModWaveFuncsKnown( texMods0, texModSequence0,
			texModWaveFuncs0 ) ||
		!GLX_Material_TexModWaveFuncsKnown( texMods1, texModSequence1,
			texModWaveFuncs1 ) ) ) {
		reasons |= GLX_MATERIAL_UNSUPPORTED_TEXMOD_WAVE;
	}

	return reasons;
}

static ID_INLINE qboolean GLX_Material_StageKeyForInputsFull( int flags, unsigned int stateBits,
	int materialCombine, int rgbGen, int alphaGen, int rgbWaveFunc, int alphaWaveFunc,
	int tcGen0, int tcGen1,
	int texMods0, int texMods1, unsigned int texModTypes0, unsigned int texModTypes1,
	unsigned int texModSequence0, unsigned int texModSequence1,
	unsigned int texModWaveFuncs0, unsigned int texModWaveFuncs1,
	int fogAdjust, qboolean fogPass, MaterialStageKey *key )
{
	MaterialProgramKey program;
	const unsigned int unsupportedReasons = GLX_Material_UnsupportedReasonsForInputsFull(
		flags, stateBits, materialCombine, rgbGen, alphaGen, rgbWaveFunc,
		alphaWaveFunc, tcGen0, tcGen1, texMods0, texMods1, texModTypes0,
		texModTypes1, texModSequence0, texModSequence1, texModWaveFuncs0,
		texModWaveFuncs1, fogAdjust, fogPass );

	if ( !key || unsupportedReasons != GLX_MATERIAL_UNSUPPORTED_NONE ||
		!GLX_Material_KeyForInputs( flags, materialCombine, texMods0, texMods1,
		fogPass, &program ) ) {
		return qfalse;
	}

	key->program = program;
	key->flags = flags;
	key->stateBits = stateBits;
	key->rgbGen = rgbGen;
	key->alphaGen = alphaGen;
	key->rgbWaveFunc = rgbWaveFunc;
	key->alphaWaveFunc = alphaWaveFunc;
	key->tcGen0 = tcGen0;
	key->tcGen1 = tcGen1;
	key->texMods0 = texMods0;
	key->texMods1 = texMods1;
	key->texModTypes0 = texModTypes0;
	key->texModTypes1 = texModTypes1;
	key->texModSequence0 = texModSequence0;
	key->texModSequence1 = texModSequence1;
	key->texModWaveFuncs0 = texModWaveFuncs0;
	key->texModWaveFuncs1 = texModWaveFuncs1;
	key->fogAdjust = fogAdjust;
	key->fogPass = fogPass;
	return qtrue;
}

static ID_INLINE unsigned int GLX_Material_UnsupportedReasonsForIR( const MaterialIR &material )
{
	unsigned int reasons = GLX_RenderIR_ValidateMaterial( material ) ?
		GLX_MATERIAL_UNSUPPORTED_NONE : GLX_MATERIAL_UNSUPPORTED_INVALID_IR;

	reasons |= GLX_Material_UnsupportedReasonsForInputsFull( material.flags,
		material.stateBits, material.materialCombine, material.rgbGen, material.alphaGen,
		material.rgbWaveFunc, material.alphaWaveFunc, material.tcGen0, material.tcGen1,
		material.texMods0, material.texMods1, material.texModTypes0,
		material.texModTypes1, material.texModSequence0, material.texModSequence1,
		material.texModWaveFuncs0, material.texModWaveFuncs1, material.fogAdjust,
		material.fogPass );
	return reasons;
}

static ID_INLINE unsigned int GLX_Material_UnsupportedReasonsForParameterBlock(
	const MaterialParameterBlock &block )
{
	unsigned int reasons = GLX_RenderIR_ValidateMaterialParameterBlock( block ) ?
		GLX_MATERIAL_UNSUPPORTED_NONE : GLX_MATERIAL_UNSUPPORTED_INVALID_IR;

	reasons |= GLX_Material_UnsupportedReasonsForInputsFull(
		block.material.flags,
		block.material.stateBits,
		block.material.materialCombine,
		block.object.rgbGen,
		block.object.alphaGen,
		block.object.rgbWaveFunc,
		block.object.alphaWaveFunc,
		block.object.tcGen0,
		block.object.tcGen1,
		block.material.texMods0,
		block.material.texMods1,
		block.material.texModTypes0,
		block.material.texModTypes1,
		block.material.texModSequence0,
		block.material.texModSequence1,
		block.material.texModWaveFuncs0,
		block.material.texModWaveFuncs1,
		block.material.fogAdjust,
		block.material.fogPass );
	return reasons;
}

static ID_INLINE qboolean GLX_Material_StageKeyForIR( const MaterialIR &material,
	MaterialStageKey *key, unsigned int *unsupportedReasons )
{
	const unsigned int reasons = GLX_Material_UnsupportedReasonsForIR( material );

	if ( unsupportedReasons ) {
		*unsupportedReasons = reasons;
	}
	if ( reasons != GLX_MATERIAL_UNSUPPORTED_NONE ) {
		return qfalse;
	}
	return GLX_Material_StageKeyForInputsFull( material.flags, material.stateBits,
		material.materialCombine, material.rgbGen, material.alphaGen,
		material.rgbWaveFunc, material.alphaWaveFunc, material.tcGen0, material.tcGen1,
		material.texMods0, material.texMods1, material.texModTypes0,
		material.texModTypes1, material.texModSequence0, material.texModSequence1,
		material.texModWaveFuncs0, material.texModWaveFuncs1, material.fogAdjust,
		material.fogPass, key );
}

static ID_INLINE qboolean GLX_Material_StageKeyForParameterBlock(
	const MaterialParameterBlock &block, MaterialStageKey *key,
	unsigned int *unsupportedReasons )
{
	const unsigned int reasons = GLX_Material_UnsupportedReasonsForParameterBlock( block );

	if ( unsupportedReasons ) {
		*unsupportedReasons = reasons;
	}
	if ( reasons != GLX_MATERIAL_UNSUPPORTED_NONE ) {
		return qfalse;
	}
	return GLX_Material_StageKeyForInputsFull( block.material.flags,
		block.material.stateBits, block.material.materialCombine,
		block.object.rgbGen, block.object.alphaGen,
		block.object.rgbWaveFunc, block.object.alphaWaveFunc,
		block.object.tcGen0, block.object.tcGen1,
		block.material.texMods0, block.material.texMods1,
		block.material.texModTypes0, block.material.texModTypes1,
		block.material.texModSequence0, block.material.texModSequence1,
		block.material.texModWaveFuncs0, block.material.texModWaveFuncs1,
		block.material.fogAdjust, block.material.fogPass, key );
}

static ID_INLINE qboolean GLX_Material_StatePlanForTierAndIR( RenderProductTier tier,
	const MaterialIR &material,
	MaterialStatePlan *plan, unsigned int *unsupportedReasons )
{
	MaterialStageKey stageKey {};
	const TierExecutionPolicy policy = GLX_RenderIR_TierExecutionPolicy( tier );

	if ( !plan || !GLX_Material_StageKeyForIR( material, &stageKey,
		unsupportedReasons ) ) {
		return qfalse;
	}
	if ( !GLX_RenderIR_TierConsumesProduct( tier, RenderProductKind::MaterialIR ) ||
		!GLX_RenderIR_TierSupportsMaterial( tier, material ) ) {
		if ( unsupportedReasons ) {
			*unsupportedReasons |= GLX_MATERIAL_UNSUPPORTED_FEATURE_SET;
		}
		return qfalse;
	}

	plan->tier = tier;
	plan->programmable = policy.materialCompiler;
	plan->sort = material.sort;
	plan->stage = stageKey;
	return qtrue;
}

static ID_INLINE qboolean GLX_Material_StatePlanForTierAndParameterBlock(
	RenderProductTier tier, const MaterialParameterBlock &block,
	MaterialStatePlan *plan, unsigned int *unsupportedReasons )
{
	MaterialStageKey stageKey {};
	const TierExecutionPolicy policy = GLX_RenderIR_TierExecutionPolicy( tier );

	if ( !plan || !GLX_Material_StageKeyForParameterBlock( block, &stageKey,
		unsupportedReasons ) ) {
		return qfalse;
	}
	if ( !GLX_RenderIR_TierConsumesProduct( tier, RenderProductKind::MaterialIR ) ) {
		if ( unsupportedReasons ) {
			*unsupportedReasons |= GLX_MATERIAL_UNSUPPORTED_FEATURE_SET;
		}
		return qfalse;
	}

	plan->tier = tier;
	plan->programmable = policy.materialCompiler;
	plan->sort = block.frame.sort;
	plan->stage = stageKey;
	return qtrue;
}

static ID_INLINE qboolean GLX_Material_StatePlanForIR( const MaterialIR &material,
	MaterialStatePlan *plan, unsigned int *unsupportedReasons )
{
	return GLX_Material_StatePlanForTierAndIR( RenderProductTier::GL2X, material,
		plan, unsupportedReasons );
}

static ID_INLINE qboolean GLX_Material_StageKeyForInputs( int flags, unsigned int stateBits,
	int materialCombine, int rgbGen, int alphaGen, int tcGen0, int tcGen1,
	int texMods0, int texMods1, unsigned int texModTypes0, unsigned int texModTypes1,
	qboolean fogPass, MaterialStageKey *key )
{
	const unsigned int texModSequence0 = GLX_Material_DefaultTexModSequence( texMods0,
		texModTypes0 );
	const unsigned int texModSequence1 = GLX_Material_DefaultTexModSequence( texMods1,
		texModTypes1 );

	return GLX_Material_StageKeyForInputsFull( flags, stateBits, materialCombine,
		rgbGen, alphaGen, GLX_MATERIAL_WAVEFUNC_NONE, GLX_MATERIAL_WAVEFUNC_NONE,
		tcGen0, tcGen1, texMods0, texMods1, texModTypes0,
		texModTypes1, texModSequence0, texModSequence1,
		GLX_Material_DefaultTexModWaveFuncs( texMods0, texModSequence0 ),
		GLX_Material_DefaultTexModWaveFuncs( texMods1, texModSequence1 ),
		GLX_MATERIAL_FOG_ADJUST_NONE, fogPass, key );
}

static ID_INLINE qboolean GLX_Material_KeyEquals( const MaterialProgramKey &lhs,
	const MaterialProgramKey &rhs )
{
	return lhs.mode == rhs.mode && lhs.features == rhs.features ? qtrue : qfalse;
}

static ID_INLINE qboolean GLX_Material_StageKeyEquals( const MaterialStageKey &lhs,
	const MaterialStageKey &rhs )
{
	return GLX_Material_KeyEquals( lhs.program, rhs.program ) &&
		lhs.flags == rhs.flags &&
		lhs.stateBits == rhs.stateBits &&
		lhs.rgbGen == rhs.rgbGen &&
		lhs.alphaGen == rhs.alphaGen &&
		lhs.rgbWaveFunc == rhs.rgbWaveFunc &&
		lhs.alphaWaveFunc == rhs.alphaWaveFunc &&
		lhs.tcGen0 == rhs.tcGen0 &&
		lhs.tcGen1 == rhs.tcGen1 &&
		lhs.texMods0 == rhs.texMods0 &&
		lhs.texMods1 == rhs.texMods1 &&
		lhs.texModTypes0 == rhs.texModTypes0 &&
		lhs.texModTypes1 == rhs.texModTypes1 &&
		lhs.texModSequence0 == rhs.texModSequence0 &&
		lhs.texModSequence1 == rhs.texModSequence1 &&
		lhs.texModWaveFuncs0 == rhs.texModWaveFuncs0 &&
		lhs.texModWaveFuncs1 == rhs.texModWaveFuncs1 &&
		lhs.fogAdjust == rhs.fogAdjust &&
		lhs.fogPass == rhs.fogPass ? qtrue : qfalse;
}

} // namespace glx

#endif // GLX_MATERIAL_KEY_H
