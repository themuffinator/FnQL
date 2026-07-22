#ifndef GLX_POST_SHADER_PLAN_H
#define GLX_POST_SHADER_PLAN_H

#include "glx_render_ir.h"

namespace glx {

static constexpr unsigned int GLX_POST_SHADER_FEATURE_NONE = 0x00000000u;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_LEGACY_GAMMA = 0x00000001u;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_SCENE_LINEAR = 0x00000002u;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_LIFT_GAMMA_GAIN = 0x00000004u;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_WHITE_POINT = 0x00000008u;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_LUT_3D = 0x00000010u;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_TONEMAP_REINHARD_SIMPLE = 0x00000020u;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_TONEMAP_ACES_FITTED = 0x00000040u;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_TONEMAP_REINHARD =
	GLX_POST_SHADER_FEATURE_TONEMAP_REINHARD_SIMPLE;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_TONEMAP_ACES =
	GLX_POST_SHADER_FEATURE_TONEMAP_ACES_FITTED;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_ENCODE_SRGB = 0x00000080u;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_ENCODE_HDR10_PQ = 0x00000100u;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_LINEAR_OUTPUT = 0x00000200u;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_BT2020_OUTPUT = 0x00000400u;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_GAMUT_COMPRESS = 0x00000800u;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_OUTPUT_TRANSFORM = 0x00001000u;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_BLOOM_COMBINE = 0x00002000u;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_GREYSCALE = 0x00004000u;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_DISPLAY_P3_OUTPUT = 0x00008000u;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_HDR_HEADROOM_OUTPUT = 0x00010000u;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_GAMUT_CLIP = 0x00020000u;
static constexpr unsigned int GLX_POST_SHADER_FEATURE_CRT = 0x00040000u;

static constexpr unsigned int GLX_POST_SHADER_DIRECT_REJECT_NONE = 0x00000000u;
static constexpr unsigned int GLX_POST_SHADER_DIRECT_REJECT_DISABLED = 0x00000001u;
static constexpr unsigned int GLX_POST_SHADER_DIRECT_REJECT_NOT_READY = 0x00000002u;
static constexpr unsigned int GLX_POST_SHADER_DIRECT_REJECT_INVALID_PLAN = 0x00000004u;
static constexpr unsigned int GLX_POST_SHADER_DIRECT_REJECT_NOT_SCENE_LINEAR = 0x00000008u;
static constexpr unsigned int GLX_POST_SHADER_DIRECT_REJECT_GRADE = 0x00000010u;
static constexpr unsigned int GLX_POST_SHADER_DIRECT_REJECT_TRANSFER = 0x00000020u;
static constexpr unsigned int GLX_POST_SHADER_DIRECT_REJECT_OUTPUT_COLORIMETRY = 0x00000040u;
static constexpr unsigned int GLX_POST_SHADER_DIRECT_REJECT_GREYSCALE = 0x00000080u;
static constexpr unsigned int GLX_POST_SHADER_DIRECT_REJECT_PROGRAM = 0x00000100u;
static constexpr unsigned int GLX_POST_SHADER_DIRECT_REJECT_UNIFORM = 0x00000200u;

struct PostShaderKey {
	qboolean sceneLinear;
	qboolean outputTransform;
	qboolean bloomComposite;
	qboolean greyscale;
	qboolean crt;
	ColorGradeMode grade;
	ToneMapOperator toneMap;
	OutputTransfer transfer;
	OutputPrimaries outputPrimaries;
	GamutMapMode gamutMap;
	qboolean lutActive;
	qboolean whitePointAdaptation;
};

struct PostShaderPlan {
	PostShaderKey key;
	qboolean valid;
	unsigned int featureMask;
	unsigned int hash;
	int textureCount;
	int uniformVec4Count;
};

static ID_INLINE qboolean GLX_PostShader_GradeUsesLiftGammaGain( ColorGradeMode grade )
{
	return grade == ColorGradeMode::LiftGammaGain ||
		grade == ColorGradeMode::LiftGammaGainLut3D ? qtrue : qfalse;
}

static ID_INLINE qboolean GLX_PostShader_GradeUsesLut( ColorGradeMode grade )
{
	return grade == ColorGradeMode::Lut3D ||
		grade == ColorGradeMode::LiftGammaGainLut3D ? qtrue : qfalse;
}

static ID_INLINE qboolean GLX_PostShader_WhitePointAdaptationActive(
	const OutputTransform &transform )
{
	if ( !GLX_PostShader_GradeUsesLiftGammaGain( transform.grade ) ) {
		return qfalse;
	}
	if ( transform.whitePointSourceKelvin < 1000.0f ||
		transform.whitePointSourceKelvin > 40000.0f ||
		transform.whitePointTargetKelvin < 1000.0f ||
		transform.whitePointTargetKelvin > 40000.0f ) {
		return qfalse;
	}
	const float delta = transform.whitePointSourceKelvin - transform.whitePointTargetKelvin;
	return ( delta > 1.0f || delta < -1.0f ) ? qtrue : qfalse;
}

static ID_INLINE qboolean GLX_PostShader_LutActive( const OutputTransform &transform )
{
	return ( GLX_PostShader_GradeUsesLut( transform.grade ) &&
		transform.lutSize >= 2.0f && transform.lutSize <= 64.0f &&
		transform.lutScale > 0.0f && transform.lutScale <= 64.0f ) ? qtrue : qfalse;
}

static ID_INLINE qboolean GLX_PostShader_TransferUsesHdrHeadroom(
	OutputTransfer transfer )
{
	return transfer == OutputTransfer::LinearSrgb ||
		transfer == OutputTransfer::ScRgb ||
		transfer == OutputTransfer::MacEdr ? qtrue : qfalse;
}

static ID_INLINE qboolean GLX_PostShader_FinalTransferSupported(
	OutputTransfer transfer )
{
	switch ( transfer ) {
	case OutputTransfer::SdrSrgb:
	case OutputTransfer::ScreenshotSrgb:
	case OutputTransfer::LinearSrgb:
	case OutputTransfer::ScRgb:
	case OutputTransfer::Hdr10Pq:
	case OutputTransfer::MacEdr:
		return qtrue;
	default:
		return qfalse;
	}
}

static ID_INLINE qboolean GLX_PostShader_LegacyGammaTransferSupported(
	OutputTransfer transfer )
{
	return transfer == OutputTransfer::SdrSrgb ||
		transfer == OutputTransfer::ScreenshotSrgb ? qtrue : qfalse;
}

static ID_INLINE qboolean GLX_PostShader_FinalOutputDomainSupported(
	const PostShaderPlan &plan, const OutputTransform &output,
	qboolean outputTransform )
{
	if ( !outputTransform || plan.key.crt ) {
		return qtrue;
	}
	if ( plan.key.sceneLinear &&
		output.sceneColorSpace == SceneColorSpace::SceneLinear &&
		output.hdrMode > 0 ) {
		return qtrue;
	}
	if ( !plan.key.sceneLinear &&
		output.sceneColorSpace == SceneColorSpace::DisplayReferredSdr &&
		output.hdrMode == 0 &&
		( plan.featureMask & GLX_POST_SHADER_FEATURE_LEGACY_GAMMA ) != 0u &&
		GLX_PostShader_LegacyGammaTransferSupported( output.transfer ) ) {
		return qtrue;
	}
	return qfalse;
}

static ID_INLINE unsigned int GLX_PostShader_FinalCompatibilityRejectMask(
	const PostShaderPlan &plan, const OutputTransform &output,
	qboolean bloomComposite, qboolean outputTransform )
{
	unsigned int rejectMask = GLX_POST_SHADER_DIRECT_REJECT_NONE;

	if ( !plan.valid || !GLX_RenderIR_ValidateOutputTransform( output ) ) {
		rejectMask |= GLX_POST_SHADER_DIRECT_REJECT_INVALID_PLAN;
	}
	if ( plan.key.bloomComposite != bloomComposite ||
		plan.key.outputTransform != outputTransform ) {
		rejectMask |= GLX_POST_SHADER_DIRECT_REJECT_INVALID_PLAN;
	}
	if ( outputTransform &&
		!GLX_PostShader_FinalOutputDomainSupported( plan, output,
			outputTransform ) ) {
		rejectMask |= GLX_POST_SHADER_DIRECT_REJECT_NOT_SCENE_LINEAR;
	}
	if ( outputTransform && ( plan.key.transfer != output.transfer ||
		!GLX_PostShader_FinalTransferSupported( output.transfer ) ||
		( !plan.key.sceneLinear &&
		!GLX_PostShader_LegacyGammaTransferSupported( output.transfer ) ) ) ) {
		rejectMask |= GLX_POST_SHADER_DIRECT_REJECT_TRANSFER;
	}
	if ( outputTransform ) {
		if ( plan.key.outputPrimaries != output.outputPrimaries ||
			plan.key.gamutMap != output.gamutMap ) {
			rejectMask |= GLX_POST_SHADER_DIRECT_REJECT_OUTPUT_COLORIMETRY;
		}
		switch ( output.outputPrimaries ) {
		case OutputPrimaries::SrgbBt709:
		case OutputPrimaries::DisplayP3:
		case OutputPrimaries::Bt2020:
		case OutputPrimaries::Native:
		case OutputPrimaries::Unknown:
			break;
		default:
			rejectMask |= GLX_POST_SHADER_DIRECT_REJECT_OUTPUT_COLORIMETRY;
			break;
		}
	}

	return rejectMask;
}

static ID_INLINE unsigned int GLX_PostShader_FeaturesForKey( const PostShaderKey &key )
{
	unsigned int features = GLX_POST_SHADER_FEATURE_NONE;

	if ( key.outputTransform ) {
		features |= GLX_POST_SHADER_FEATURE_OUTPUT_TRANSFORM;
	}
	if ( key.bloomComposite ) {
		features |= GLX_POST_SHADER_FEATURE_BLOOM_COMBINE;
	}
	if ( key.greyscale ) {
		features |= GLX_POST_SHADER_FEATURE_GREYSCALE;
	}
	if ( key.crt ) {
		features |= GLX_POST_SHADER_FEATURE_CRT;
	}
	if ( key.sceneLinear ) {
		features |= GLX_POST_SHADER_FEATURE_SCENE_LINEAR;
	} else if ( key.outputTransform ) {
		features |= GLX_POST_SHADER_FEATURE_LEGACY_GAMMA;
	}
	if ( key.outputTransform && key.sceneLinear &&
		GLX_PostShader_GradeUsesLiftGammaGain( key.grade ) ) {
		features |= GLX_POST_SHADER_FEATURE_LIFT_GAMMA_GAIN;
	}
	if ( key.outputTransform && key.sceneLinear && key.whitePointAdaptation ) {
		features |= GLX_POST_SHADER_FEATURE_WHITE_POINT;
	}
	if ( key.outputTransform && key.sceneLinear && key.lutActive ) {
		features |= GLX_POST_SHADER_FEATURE_LUT_3D;
	}
	if ( key.outputTransform && key.sceneLinear &&
		key.toneMap == ToneMapOperator::ReinhardSimple ) {
		features |= GLX_POST_SHADER_FEATURE_TONEMAP_REINHARD_SIMPLE;
	}
	if ( key.outputTransform && key.sceneLinear &&
		key.toneMap == ToneMapOperator::AcesFitted ) {
		features |= GLX_POST_SHADER_FEATURE_TONEMAP_ACES_FITTED;
	}

	if ( !key.outputTransform ) {
		features |= GLX_POST_SHADER_FEATURE_LINEAR_OUTPUT;
		return features;
	}

	switch ( key.transfer ) {
	case OutputTransfer::SdrSrgb:
	case OutputTransfer::ScreenshotSrgb:
		features |= key.sceneLinear ? GLX_POST_SHADER_FEATURE_ENCODE_SRGB :
			GLX_POST_SHADER_FEATURE_LEGACY_GAMMA;
		break;
	case OutputTransfer::Hdr10Pq:
		features |= GLX_POST_SHADER_FEATURE_ENCODE_HDR10_PQ;
		break;
	case OutputTransfer::LinearSrgb:
	case OutputTransfer::ScRgb:
	case OutputTransfer::MacEdr:
	default:
		features |= GLX_POST_SHADER_FEATURE_LINEAR_OUTPUT;
		break;
	}
	if ( GLX_PostShader_TransferUsesHdrHeadroom( key.transfer ) ) {
		features |= GLX_POST_SHADER_FEATURE_HDR_HEADROOM_OUTPUT;
	}
	if ( key.outputPrimaries == OutputPrimaries::DisplayP3 ) {
		features |= GLX_POST_SHADER_FEATURE_DISPLAY_P3_OUTPUT;
	}
	if ( key.outputPrimaries == OutputPrimaries::Bt2020 ) {
		features |= GLX_POST_SHADER_FEATURE_BT2020_OUTPUT;
	}
	if ( key.gamutMap == GamutMapMode::CompressToOutput ) {
		features |= GLX_POST_SHADER_FEATURE_GAMUT_COMPRESS;
	} else if ( key.gamutMap == GamutMapMode::Clip ) {
		features |= GLX_POST_SHADER_FEATURE_GAMUT_CLIP;
	}
	return features;
}

static ID_INLINE qboolean GLX_PostShader_PostParamsRequired(
	const PostShaderPlan &plan )
{
	if ( plan.key.outputTransform && plan.key.sceneLinear ) {
		return qtrue;
	}

	return ( plan.featureMask &
		( GLX_POST_SHADER_FEATURE_GREYSCALE |
		GLX_POST_SHADER_FEATURE_HDR_HEADROOM_OUTPUT ) ) != 0u ? qtrue : qfalse;
}

static ID_INLINE unsigned int GLX_PostShader_HashKey( const PostShaderKey &key,
	unsigned int featureMask )
{
	unsigned int hash = 2166136261u;

	hash = GLX_RenderIR_HashValue( hash, key.sceneLinear ? 1u : 0u );
	hash = GLX_RenderIR_HashValue( hash, key.outputTransform ? 1u : 0u );
	hash = GLX_RenderIR_HashValue( hash, key.bloomComposite ? 1u : 0u );
	hash = GLX_RenderIR_HashValue( hash, key.greyscale ? 1u : 0u );
	hash = GLX_RenderIR_HashValue( hash, key.crt ? 1u : 0u );
	hash = GLX_RenderIR_HashValue( hash, static_cast<unsigned int>( key.grade ) );
	hash = GLX_RenderIR_HashValue( hash, static_cast<unsigned int>( key.toneMap ) );
	hash = GLX_RenderIR_HashValue( hash, static_cast<unsigned int>( key.transfer ) );
	hash = GLX_RenderIR_HashValue( hash, static_cast<unsigned int>( key.outputPrimaries ) );
	hash = GLX_RenderIR_HashValue( hash, static_cast<unsigned int>( key.gamutMap ) );
	hash = GLX_RenderIR_HashValue( hash, key.lutActive ? 1u : 0u );
	hash = GLX_RenderIR_HashValue( hash, key.whitePointAdaptation ? 1u : 0u );
	hash = GLX_RenderIR_HashValue( hash, featureMask );
	return hash ? hash : 1u;
}

static ID_INLINE PostShaderPlan GLX_PostShader_BuildPlanForPass(
	const OutputTransform &transform, qboolean bloomComposite,
	qboolean outputTransform )
{
	PostShaderPlan plan {};

	plan.valid = GLX_RenderIR_ValidateOutputTransform( transform );
	plan.key.sceneLinear = ( transform.sceneColorSpace == SceneColorSpace::SceneLinear ) ?
		qtrue : qfalse;
	plan.key.outputTransform = outputTransform;
	plan.key.bloomComposite = bloomComposite;
	plan.key.greyscale = ( outputTransform && transform.greyscale != 0.0f ) ? qtrue : qfalse;
	plan.key.crt = ( outputTransform && transform.crtAmount > 0.001f ) ? qtrue : qfalse;
	plan.key.grade = ( outputTransform && plan.key.sceneLinear ) ? transform.grade :
		ColorGradeMode::NoColorGrade;
	plan.key.toneMap = ( outputTransform && plan.key.sceneLinear ) ? transform.toneMap :
		ToneMapOperator::Legacy;
	plan.key.transfer = transform.transfer;
	plan.key.outputPrimaries = transform.outputPrimaries;
	plan.key.gamutMap = transform.gamutMap;
	plan.key.lutActive = ( outputTransform && plan.key.sceneLinear ) ?
		GLX_PostShader_LutActive( transform ) : qfalse;
	plan.key.whitePointAdaptation = ( outputTransform && plan.key.sceneLinear ) ?
		GLX_PostShader_WhitePointAdaptationActive( transform ) : qfalse;
	plan.featureMask = GLX_PostShader_FeaturesForKey( plan.key );
	plan.textureCount = 1;
	if ( plan.key.bloomComposite ) {
		plan.textureCount++;
	}
	if ( plan.key.lutActive ) {
		plan.textureCount++;
	}
	plan.uniformVec4Count = 4;
	if ( plan.key.bloomComposite ) {
		plan.uniformVec4Count += 1;
	}
	if ( GLX_PostShader_GradeUsesLiftGammaGain( plan.key.grade ) ) {
		plan.uniformVec4Count += 6;
	}
	if ( plan.key.lutActive ) {
		plan.uniformVec4Count += 2;
	}
	if ( plan.key.transfer == OutputTransfer::Hdr10Pq ) {
		plan.uniformVec4Count += 1;
	}
	if ( ( plan.featureMask &
		( GLX_POST_SHADER_FEATURE_HDR_HEADROOM_OUTPUT |
		GLX_POST_SHADER_FEATURE_GAMUT_COMPRESS ) ) != 0u ) {
		plan.uniformVec4Count += 1;
	}
	if ( plan.key.crt ) {
		plan.uniformVec4Count += 2;
	}
	if ( ( plan.featureMask & GLX_POST_SHADER_FEATURE_LEGACY_GAMMA ) != 0u ) {
		plan.uniformVec4Count += 1;
	}
	plan.hash = GLX_PostShader_HashKey( plan.key, plan.featureMask );
	return plan;
}

static ID_INLINE PostShaderPlan GLX_PostShader_BuildPlanForOutput(
	const OutputTransform &transform, qboolean bloomComposite )
{
	return GLX_PostShader_BuildPlanForPass( transform, bloomComposite, qtrue );
}

static ID_INLINE PostShaderPlan GLX_PostShader_BuildPlan(
	const OutputTransform &transform )
{
	return GLX_PostShader_BuildPlanForOutput( transform, qfalse );
}

} // namespace glx

#endif // GLX_POST_SHADER_PLAN_H
