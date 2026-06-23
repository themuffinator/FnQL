#ifndef GLX_POST_OUTPUT_REFERENCE_H
#define GLX_POST_OUTPUT_REFERENCE_H

#include "glx_color_math.h"
#include "glx_render_ir.h"

namespace glx {

static inline bool GLX_PostOutputReference_UsesLiftGammaGain( ColorGradeMode grade )
{
	return grade == ColorGradeMode::LiftGammaGain ||
		grade == ColorGradeMode::LiftGammaGainLut3D;
}

static inline bool GLX_PostOutputReference_UsesLut( ColorGradeMode grade )
{
	return grade == ColorGradeMode::Lut3D ||
		grade == ColorGradeMode::LiftGammaGainLut3D;
}

struct PostOutputReferenceContract {
	OutputTransfer transfer;
	ToneMapOperator toneMap;
	ColorGradeMode grade;
	OutputPrimaries outputPrimaries;
	GamutMapMode gamutMap;
	float exposure;
	float paperWhiteNits;
	float maxOutputNits;
	float displayHdrHeadroom;
	float outputHeadroom;
	float gradeLift[3];
	float gradeGamma[3];
	float gradeGain[3];
	float whitePointSourceKelvin;
	float whitePointTargetKelvin;
	int lutSize;
	float lutScale;
	bool lutActive;
};

static inline ColorMathVec3 GLX_PostOutputReference_Max0( const ColorMathVec3 &color )
{
	return GLX_ColorMath_Max0( color );
}

static inline float GLX_PostOutputReference_SanitizeExposure(
	const OutputTransform &transform )
{
	return GLX_ColorMath_SanitizeFiniteRange( transform.exposure, 0.0f, 64.0f, 1.0f );
}

static inline float GLX_PostOutputReference_SanitizePaperWhite(
	const OutputTransform &transform )
{
	return GLX_ColorMath_SanitizeFiniteRange( transform.paperWhiteNits, 1.0f, 10000.0f,
		203.0f );
}

static inline float GLX_PostOutputReference_SanitizeMaxOutput(
	const OutputTransform &transform, float paperWhite )
{
	const float fallback = paperWhite > 0.0f ? paperWhite : 203.0f;
	float maxOutput = GLX_ColorMath_SanitizeFiniteRange( transform.maxOutputNits,
		1.0f, 10000.0f, fallback );
	if ( maxOutput < paperWhite ) {
		maxOutput = paperWhite;
	}
	return maxOutput;
}

static inline float GLX_PostOutputReference_SanitizeDisplayHeadroom(
	const OutputTransform &transform );

static inline float GLX_PostOutputReference_OutputHeadroom(
	const OutputTransform &transform )
{
	const float paperWhite = GLX_PostOutputReference_SanitizePaperWhite( transform );
	const float maxOutput = GLX_PostOutputReference_SanitizeMaxOutput( transform,
		paperWhite );
	float headroom = GLX_ColorMath_Clamp( maxOutput / paperWhite, 1.0f, 64.0f );

	if ( transform.displayHdrHeadroomValid ) {
		const float displayHeadroom = GLX_PostOutputReference_SanitizeDisplayHeadroom(
			transform );
		if ( displayHeadroom < headroom ) {
			headroom = displayHeadroom;
		}
	}
	return headroom;
}

static inline float GLX_PostOutputReference_SanitizeDisplayHeadroom(
	const OutputTransform &transform )
{
	return GLX_ColorMath_SanitizeFiniteRange( transform.displayHdrHeadroom,
		1.0f, 64.0f, 1.0f );
}

static inline float GLX_PostOutputReference_SanitizeGradeGamma( float gamma )
{
	return GLX_ColorMath_SanitizeFiniteRange( gamma, 0.0001f, 64.0f, 1.0f );
}

static inline float GLX_PostOutputReference_SanitizeGradeGain( float gain )
{
	return GLX_ColorMath_SanitizeFiniteRange( gain, 0.0f, 64.0f, 1.0f );
}

static inline float GLX_PostOutputReference_SanitizeWhitePointKelvin( float kelvin )
{
	return GLX_ColorMath_SanitizeFiniteRange( kelvin, 1000.0f, 40000.0f, 6504.0f );
}

static inline float GLX_PostOutputReference_SanitizeLutScale( float scale )
{
	return GLX_ColorMath_SanitizeFiniteRange( scale, 0.0001f, 64.0f, 4.0f );
}

static inline int GLX_PostOutputReference_SanitizeLutSize( const OutputTransform &transform,
	int lutWidth, int lutHeight )
{
	int atlasSize = 0;

	if ( !GLX_PostOutputReference_UsesLut( transform.grade ) ||
		!GLX_ColorMath_LutAtlasSize( lutWidth, lutHeight, &atlasSize ) ||
		!std::isfinite( transform.lutSize ) ) {
		return 0;
	}
	if ( transform.lutSize < 2.0f ) {
		return 0;
	}

	const int requestedSize = static_cast<int>( transform.lutSize + 0.5f );
	if ( requestedSize < 2 || requestedSize > 64 || requestedSize != atlasSize ) {
		return 0;
	}
	return requestedSize;
}

static inline OutputTransfer GLX_PostOutputReference_SanitizeTransfer(
	OutputTransfer transfer )
{
	switch ( transfer ) {
	case OutputTransfer::SdrSrgb:
	case OutputTransfer::LinearSrgb:
	case OutputTransfer::ScRgb:
	case OutputTransfer::Hdr10Pq:
	case OutputTransfer::MacEdr:
	case OutputTransfer::ScreenshotSrgb:
		return transfer;
	default:
		return OutputTransfer::LinearSrgb;
	}
}

static inline ToneMapOperator GLX_PostOutputReference_SanitizeToneMap(
	ToneMapOperator toneMap )
{
	switch ( toneMap ) {
	case ToneMapOperator::Legacy:
	case ToneMapOperator::ReinhardSimple:
	case ToneMapOperator::AcesFitted:
		return toneMap;
	default:
		return ToneMapOperator::Legacy;
	}
}

static inline ColorGradeMode GLX_PostOutputReference_SanitizeGrade(
	ColorGradeMode grade )
{
	switch ( grade ) {
	case ColorGradeMode::NoColorGrade:
	case ColorGradeMode::LiftGammaGain:
	case ColorGradeMode::Lut3D:
	case ColorGradeMode::LiftGammaGainLut3D:
		return grade;
	default:
		return ColorGradeMode::NoColorGrade;
	}
}

static inline OutputPrimaries GLX_PostOutputReference_SanitizePrimaries(
	const OutputTransform &transform )
{
	switch ( transform.outputPrimaries ) {
	case OutputPrimaries::SrgbBt709:
	case OutputPrimaries::DisplayP3:
	case OutputPrimaries::Bt2020:
		return transform.outputPrimaries;
	case OutputPrimaries::Native:
		return GLX_RenderIR_OutputPrimariesNativePassthroughAllowed( transform ) ?
			OutputPrimaries::Native : OutputPrimaries::SrgbBt709;
	case OutputPrimaries::Unknown:
	default:
		return OutputPrimaries::SrgbBt709;
	}
}

static inline GamutMapMode GLX_PostOutputReference_SanitizeGamutMap(
	GamutMapMode gamutMap )
{
	switch ( gamutMap ) {
	case GamutMapMode::NoGamutMap:
	case GamutMapMode::Clip:
	case GamutMapMode::CompressToOutput:
		return gamutMap;
	default:
		return GamutMapMode::NoGamutMap;
	}
}

static inline PostOutputReferenceContract GLX_PostOutputReference_BuildContract(
	const OutputTransform &transform, int lutWidth, int lutHeight )
{
	PostOutputReferenceContract contract {};

	contract.transfer = GLX_PostOutputReference_SanitizeTransfer( transform.transfer );
	contract.toneMap = GLX_PostOutputReference_SanitizeToneMap( transform.toneMap );
	contract.grade = GLX_PostOutputReference_SanitizeGrade( transform.grade );
	contract.outputPrimaries = GLX_PostOutputReference_SanitizePrimaries( transform );
	contract.gamutMap = GLX_PostOutputReference_SanitizeGamutMap( transform.gamutMap );
	contract.exposure = GLX_PostOutputReference_SanitizeExposure( transform );
	contract.paperWhiteNits = GLX_PostOutputReference_SanitizePaperWhite( transform );
	contract.maxOutputNits = GLX_PostOutputReference_SanitizeMaxOutput( transform,
		contract.paperWhiteNits );
	contract.displayHdrHeadroom = GLX_PostOutputReference_SanitizeDisplayHeadroom(
		transform );
	contract.outputHeadroom = GLX_PostOutputReference_OutputHeadroom( transform );
	contract.whitePointSourceKelvin = GLX_PostOutputReference_SanitizeWhitePointKelvin(
		transform.whitePointSourceKelvin );
	contract.whitePointTargetKelvin = GLX_PostOutputReference_SanitizeWhitePointKelvin(
		transform.whitePointTargetKelvin );
	contract.lutSize = GLX_PostOutputReference_SanitizeLutSize( transform,
		lutWidth, lutHeight );
	contract.lutScale = GLX_PostOutputReference_SanitizeLutScale( transform.lutScale );
	contract.lutActive = contract.lutSize >= 2;

	for ( int i = 0; i < 3; i++ ) {
		contract.gradeLift[i] = GLX_ColorMath_SanitizeFiniteRange(
			transform.gradeLift[i], -64.0f, 64.0f, 0.0f );
		contract.gradeGamma[i] = GLX_PostOutputReference_SanitizeGradeGamma(
			transform.gradeGamma[i] );
		contract.gradeGain[i] = GLX_PostOutputReference_SanitizeGradeGain(
			transform.gradeGain[i] );
	}
	return contract;
}

static inline ColorMathVec3 GLX_PostOutputReference_ApplyLiftGammaGain(
	const ColorMathVec3 &color, const PostOutputReferenceContract &contract )
{
	ColorMathVec3 out {};
	const ColorMathVec3 finite = GLX_ColorMath_Max0( color );

	out.r = std::pow( finite.r + contract.gradeLift[0] > 0.0f ?
		finite.r + contract.gradeLift[0] : 0.0f,
		1.0f / contract.gradeGamma[0] ) * contract.gradeGain[0];
	out.g = std::pow( finite.g + contract.gradeLift[1] > 0.0f ?
		finite.g + contract.gradeLift[1] : 0.0f,
		1.0f / contract.gradeGamma[1] ) * contract.gradeGain[1];
	out.b = std::pow( finite.b + contract.gradeLift[2] > 0.0f ?
		finite.b + contract.gradeLift[2] : 0.0f,
		1.0f / contract.gradeGamma[2] ) * contract.gradeGain[2];

	return GLX_PostOutputReference_Max0( GLX_ColorMath_AdaptWhitePointBradford(
		out, contract.whitePointSourceKelvin, contract.whitePointTargetKelvin ) );
}

static inline ColorMathVec3 GLX_PostOutputReference_ApplyLiftGammaGain(
	const ColorMathVec3 &color, const OutputTransform &transform )
{
	return GLX_PostOutputReference_ApplyLiftGammaGain( color,
		GLX_PostOutputReference_BuildContract( transform, 0, 0 ) );
}

static inline ColorMathVec3 GLX_PostOutputReference_ApplyColorGrade(
	const ColorMathVec3 &color, const PostOutputReferenceContract &contract,
	const ColorMathVec3 *lutAtlas, int lutWidth, int lutHeight )
{
	ColorMathVec3 out = color;

	if ( GLX_PostOutputReference_UsesLiftGammaGain( contract.grade ) ) {
		out = GLX_PostOutputReference_ApplyLiftGammaGain( out, contract );
	}
	if ( contract.lutActive ) {
		out = GLX_ColorMath_SampleLutAtlas( lutAtlas, lutWidth, lutHeight,
			out, contract.lutScale );
	}
	return GLX_ColorMath_SanitizeVec3( out, 0.0f );
}

static inline ColorMathVec3 GLX_PostOutputReference_ApplyColorGrade(
	const ColorMathVec3 &color, const OutputTransform &transform,
	const ColorMathVec3 *lutAtlas, int lutWidth, int lutHeight )
{
	return GLX_PostOutputReference_ApplyColorGrade( color,
		GLX_PostOutputReference_BuildContract( transform, lutWidth, lutHeight ),
		lutAtlas, lutWidth, lutHeight );
}

static inline ColorMathVec3 GLX_PostOutputReference_ApplyToneMap(
	const ColorMathVec3 &color, ToneMapOperator toneMap )
{
	ColorMathVec3 out = color;

	if ( toneMap == ToneMapOperator::ReinhardSimple ) {
		out.r = GLX_ColorMath_ToneMapReinhardSimple( out.r );
		out.g = GLX_ColorMath_ToneMapReinhardSimple( out.g );
		out.b = GLX_ColorMath_ToneMapReinhardSimple( out.b );
	} else if ( toneMap == ToneMapOperator::AcesFitted ) {
		out.r = GLX_ColorMath_ToneMapAcesFitted( out.r );
		out.g = GLX_ColorMath_ToneMapAcesFitted( out.g );
		out.b = GLX_ColorMath_ToneMapAcesFitted( out.b );
	}
	return GLX_ColorMath_SanitizeVec3( out, 0.0f );
}

static inline ColorMathVec3 GLX_PostOutputReference_EncodeSrgb( const ColorMathVec3 &color )
{
	ColorMathVec3 out {};
	out.r = GLX_ColorMath_LinearToSrgb( color.r );
	out.g = GLX_ColorMath_LinearToSrgb( color.g );
	out.b = GLX_ColorMath_LinearToSrgb( color.b );
	return out;
}

static inline ColorMathVec3 GLX_PostOutputReference_EncodeHdr10Pq(
	const ColorMathVec3 &color, const PostOutputReferenceContract &contract )
{
	const ColorMathVec3 outputLinear = GLX_PostOutputReference_Max0( color );

	ColorMathVec3 out {};
	out.r = GLX_ColorMath_PqEncodeNits( outputLinear.r * contract.paperWhiteNits,
		contract.maxOutputNits );
	out.g = GLX_ColorMath_PqEncodeNits( outputLinear.g * contract.paperWhiteNits,
		contract.maxOutputNits );
	out.b = GLX_ColorMath_PqEncodeNits( outputLinear.b * contract.paperWhiteNits,
		contract.maxOutputNits );
	return out;
}

static inline ColorMathVec3 GLX_PostOutputReference_EncodeHdr10Pq(
	const ColorMathVec3 &color, const OutputTransform &transform )
{
	return GLX_PostOutputReference_EncodeHdr10Pq( color,
		GLX_PostOutputReference_BuildContract( transform, 0, 0 ) );
}

static inline ColorMathVec3 GLX_PostOutputReference_MapPrimaries(
	const ColorMathVec3 &color, const PostOutputReferenceContract &contract )
{
	const ColorMathVec3 positive = GLX_PostOutputReference_Max0( color );

	switch ( contract.outputPrimaries ) {
	case OutputPrimaries::DisplayP3:
		return GLX_ColorMath_LinearSrgbToDisplayP3( positive );
	case OutputPrimaries::Bt2020:
		return GLX_ColorMath_LinearSrgbToBt2020( positive );
	case OutputPrimaries::SrgbBt709:
	case OutputPrimaries::Native:
	case OutputPrimaries::Unknown:
	default:
		return positive;
	}
}

static inline ColorMathVec3 GLX_PostOutputReference_MapPrimaries(
	const ColorMathVec3 &color, const OutputTransform &transform )
{
	return GLX_PostOutputReference_MapPrimaries( color,
		GLX_PostOutputReference_BuildContract( transform, 0, 0 ) );
}

static inline ColorMathVec3 GLX_PostOutputReference_ApplyGamutMap(
	const ColorMathVec3 &color, const PostOutputReferenceContract &contract )
{
	switch ( contract.gamutMap ) {
	case GamutMapMode::Clip:
		return GLX_ColorMath_ClampVec3( color, 0.0f, 1.0f );
	case GamutMapMode::CompressToOutput:
		return GLX_ColorMath_ClampVec3( color, 0.0f, contract.outputHeadroom );
	case GamutMapMode::NoGamutMap:
	default:
		return GLX_PostOutputReference_Max0( color );
	}
}

static inline ColorMathVec3 GLX_PostOutputReference_EncodeTransfer(
	const ColorMathVec3 &color, const PostOutputReferenceContract &contract )
{
	switch ( contract.transfer ) {
	case OutputTransfer::SdrSrgb:
	case OutputTransfer::ScreenshotSrgb:
		return GLX_PostOutputReference_EncodeSrgb( color );
	case OutputTransfer::Hdr10Pq:
		return GLX_PostOutputReference_EncodeHdr10Pq( color, contract );
	case OutputTransfer::LinearSrgb:
	case OutputTransfer::ScRgb:
	case OutputTransfer::MacEdr:
	default:
		return GLX_PostOutputReference_Max0( color );
	}
}

static inline ColorMathVec3 GLX_PostOutputReference_ApplyGamutMap(
	const ColorMathVec3 &color, const OutputTransform &transform )
{
	return GLX_PostOutputReference_ApplyGamutMap( color,
		GLX_PostOutputReference_BuildContract( transform, 0, 0 ) );
}

static inline ColorMathVec3 GLX_PostOutputReference_EncodeTransfer(
	const ColorMathVec3 &color, const OutputTransform &transform )
{
	return GLX_PostOutputReference_EncodeTransfer( color,
		GLX_PostOutputReference_BuildContract( transform, 0, 0 ) );
}

static inline ColorMathVec3 GLX_PostOutputReference_FinalClamp(
	const ColorMathVec3 &color, const PostOutputReferenceContract &contract )
{
	switch ( contract.transfer ) {
	case OutputTransfer::LinearSrgb:
	case OutputTransfer::ScRgb:
	case OutputTransfer::MacEdr:
		return GLX_ColorMath_ClampVec3( color, 0.0f, contract.outputHeadroom );
	case OutputTransfer::SdrSrgb:
	case OutputTransfer::ScreenshotSrgb:
	case OutputTransfer::Hdr10Pq:
	default:
		return GLX_ColorMath_ClampVec3( color, 0.0f, 1.0f );
	}
}

static inline ColorMathVec3 GLX_PostOutputReference_Evaluate(
	const ColorMathVec3 &sceneLinear, const OutputTransform &transform,
	const ColorMathVec3 *lutAtlas, int lutWidth, int lutHeight )
{
	const PostOutputReferenceContract contract =
		GLX_PostOutputReference_BuildContract( transform, lutWidth, lutHeight );
	ColorMathVec3 color = GLX_ColorMath_SanitizeVec3( sceneLinear, 0.0f );
	color.r *= contract.exposure;
	color.g *= contract.exposure;
	color.b *= contract.exposure;
	color = GLX_PostOutputReference_Max0( color );
	color = GLX_PostOutputReference_ApplyColorGrade( color, contract,
		lutAtlas, lutWidth, lutHeight );
	color = GLX_PostOutputReference_ApplyToneMap( color, contract.toneMap );
	color = GLX_PostOutputReference_MapPrimaries( color, contract );
	color = GLX_PostOutputReference_ApplyGamutMap( color, contract );
	color = GLX_PostOutputReference_EncodeTransfer( color, contract );
	return GLX_PostOutputReference_FinalClamp( color, contract );
}

} // namespace glx

#endif // GLX_POST_OUTPUT_REFERENCE_H
