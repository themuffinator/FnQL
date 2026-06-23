#ifndef GLX_COLOR_MATH_H
#define GLX_COLOR_MATH_H

#include <cmath>

namespace glx {

struct ColorMathVec3 {
	float r;
	float g;
	float b;
};

struct ColorMathXy {
	float x;
	float y;
};

struct ColorMathLutAddress {
	int r0;
	int r1;
	int g0;
	int g1;
	int b0;
	int b1;
	float tr;
	float tg;
	float tb;
};

static constexpr int GLX_COLOR_MATH_EXPOSURE_HISTOGRAM_BINS = 64;

struct ColorMathExposureHistogram {
	unsigned int bins[GLX_COLOR_MATH_EXPOSURE_HISTOGRAM_BINS];
	unsigned int sampleCount;
	float minLog2;
	float maxLog2;
	float logLumaSum;
	float lumaSum;
	float minLuma;
	float maxLuma;
};

struct ColorMathExposureResult {
	bool valid;
	float measuredLog2Luma;
	float measuredLuma;
	float exposureScale;
	int bin;
};

static inline float GLX_ColorMath_Clamp( float value, float minValue, float maxValue )
{
	if ( !std::isfinite( minValue ) ) {
		minValue = 0.0f;
	}
	if ( !std::isfinite( maxValue ) ) {
		maxValue = minValue;
	}
	if ( maxValue < minValue ) {
		const float tmp = minValue;
		minValue = maxValue;
		maxValue = tmp;
	}
	if ( value < minValue ) {
		return minValue;
	}
	if ( value > maxValue ) {
		return maxValue;
	}
	if ( !std::isfinite( value ) ) {
		return minValue;
	}
	return value;
}

static inline float GLX_ColorMath_SanitizeFinite( float value, float fallback )
{
	return std::isfinite( value ) ? value : fallback;
}

static inline float GLX_ColorMath_SanitizeFiniteRange( float value,
	float minValue, float maxValue, float fallback )
{
	value = GLX_ColorMath_SanitizeFinite( value, fallback );
	return GLX_ColorMath_Clamp( value, minValue, maxValue );
}

static inline float GLX_ColorMath_Clamp01( float value )
{
	return GLX_ColorMath_Clamp( value, 0.0f, 1.0f );
}

static inline ColorMathVec3 GLX_ColorMath_SanitizeVec3( const ColorMathVec3 &color,
	float fallback )
{
	ColorMathVec3 out {};
	out.r = GLX_ColorMath_SanitizeFinite( color.r, fallback );
	out.g = GLX_ColorMath_SanitizeFinite( color.g, fallback );
	out.b = GLX_ColorMath_SanitizeFinite( color.b, fallback );
	return out;
}

static inline ColorMathVec3 GLX_ColorMath_Max0( const ColorMathVec3 &color )
{
	const ColorMathVec3 finite = GLX_ColorMath_SanitizeVec3( color, 0.0f );
	ColorMathVec3 out {};
	out.r = finite.r > 0.0f ? finite.r : 0.0f;
	out.g = finite.g > 0.0f ? finite.g : 0.0f;
	out.b = finite.b > 0.0f ? finite.b : 0.0f;
	return out;
}

static inline ColorMathVec3 GLX_ColorMath_ClampVec3( const ColorMathVec3 &color,
	float minValue, float maxValue )
{
	ColorMathVec3 out {};
	out.r = GLX_ColorMath_Clamp( color.r, minValue, maxValue );
	out.g = GLX_ColorMath_Clamp( color.g, minValue, maxValue );
	out.b = GLX_ColorMath_Clamp( color.b, minValue, maxValue );
	return out;
}

static inline float GLX_ColorMath_SrgbToLinear( float srgb )
{
	srgb = GLX_ColorMath_Clamp01( srgb );
	if ( srgb <= 0.04045f ) {
		return srgb / 12.92f;
	}
	return std::pow( ( srgb + 0.055f ) / 1.055f, 2.4f );
}

static inline float GLX_ColorMath_LinearToSrgb( float linear )
{
	linear = GLX_ColorMath_SanitizeFinite( linear, 0.0f );
	linear = linear < 0.0f ? 0.0f : linear;
	if ( linear <= 0.0031308f ) {
		return linear * 12.92f;
	}
	return 1.055f * std::pow( linear, 1.0f / 2.4f ) - 0.055f;
}

static inline float GLX_ColorMath_ToneMapReinhardSimple( float value )
{
	if ( !std::isfinite( value ) ) {
		return value > 0.0f ? 1.0f : 0.0f;
	}
	value = value < 0.0f ? 0.0f : value;
	return value / ( 1.0f + value );
}

static inline float GLX_ColorMath_ToneMapReinhard( float value )
{
	return GLX_ColorMath_ToneMapReinhardSimple( value );
}

static inline float GLX_ColorMath_ToneMapAcesFitted( float value )
{
	static constexpr float a = 2.51f;
	static constexpr float b = 0.03f;
	static constexpr float c = 2.43f;
	static constexpr float d = 0.59f;
	static constexpr float e = 0.14f;

	if ( !std::isfinite( value ) ) {
		return value > 0.0f ? 1.0f : 0.0f;
	}
	value = value < 0.0f ? 0.0f : value;
	value = value > 65504.0f ? 65504.0f : value;
	return GLX_ColorMath_Clamp01( ( value * ( a * value + b ) ) /
		( value * ( c * value + d ) + e ) );
}

static inline float GLX_ColorMath_PqEncodeNits( float nits, float maxNits )
{
	static constexpr float m1 = 0.1593017578125f;
	static constexpr float m2 = 78.84375f;
	static constexpr float c1 = 0.8359375f;
	static constexpr float c2 = 18.8515625f;
	static constexpr float c3 = 18.6875f;

	if ( !std::isfinite( maxNits ) || maxNits <= 0.0f ) {
		maxNits = 10000.0f;
	}
	maxNits = GLX_ColorMath_Clamp( maxNits, 1.0f, 10000.0f );
	if ( !std::isfinite( nits ) ) {
		nits = nits > 0.0f ? maxNits : 0.0f;
	}
	nits = GLX_ColorMath_Clamp( nits, 0.0f, maxNits );
	const float y = std::pow( nits / 10000.0f, m1 );
	return std::pow( ( c1 + c2 * y ) / ( 1.0f + c3 * y ), m2 );
}

static inline ColorMathVec3 GLX_ColorMath_LinearSrgbToBt2020( const ColorMathVec3 &color )
{
	const ColorMathVec3 finite = GLX_ColorMath_SanitizeVec3( color, 0.0f );
	ColorMathVec3 out {};
	out.r = finite.r * 0.6274040f + finite.g * 0.3292820f + finite.b * 0.0433136f;
	out.g = finite.r * 0.0690970f + finite.g * 0.9195400f + finite.b * 0.0113612f;
	out.b = finite.r * 0.0163916f + finite.g * 0.0880132f + finite.b * 0.8955950f;
	return out;
}

static inline ColorMathVec3 GLX_ColorMath_LinearSrgbToDisplayP3( const ColorMathVec3 &color )
{
	const ColorMathVec3 finite = GLX_ColorMath_SanitizeVec3( color, 0.0f );
	ColorMathVec3 out {};
	out.r = finite.r * 0.8224621f + finite.g * 0.1775380f;
	out.g = finite.r * 0.0331941f + finite.g * 0.9668059f;
	out.b = finite.r * 0.0170827f + finite.g * 0.0723974f + finite.b * 0.9105199f;
	return out;
}

static inline ColorMathVec3 GLX_ColorMath_LinearSrgbToXyz( const ColorMathVec3 &color )
{
	const ColorMathVec3 finite = GLX_ColorMath_SanitizeVec3( color, 0.0f );
	ColorMathVec3 out {};
	out.r = finite.r * 0.4124564f + finite.g * 0.3575761f + finite.b * 0.1804375f;
	out.g = finite.r * 0.2126729f + finite.g * 0.7151522f + finite.b * 0.0721750f;
	out.b = finite.r * 0.0193339f + finite.g * 0.1191920f + finite.b * 0.9503041f;
	return out;
}

static inline ColorMathVec3 GLX_ColorMath_XyzToLinearSrgb( const ColorMathVec3 &color )
{
	const ColorMathVec3 finite = GLX_ColorMath_SanitizeVec3( color, 0.0f );
	ColorMathVec3 out {};
	out.r = finite.r * 3.2404542f + finite.g * -1.5371385f + finite.b * -0.4985314f;
	out.g = finite.r * -0.9692660f + finite.g * 1.8760108f + finite.b * 0.0415560f;
	out.b = finite.r * 0.0556434f + finite.g * -0.2040259f + finite.b * 1.0572252f;
	return out;
}

static inline ColorMathXy GLX_ColorMath_WhitePointXyFromKelvin( float kelvin )
{
	kelvin = GLX_ColorMath_Clamp( kelvin, 1667.0f, 25000.0f );

	ColorMathXy xy {};
	if ( kelvin <= 4000.0f ) {
		xy.x = -0.2661239e9f / ( kelvin * kelvin * kelvin ) -
			0.2343580e6f / ( kelvin * kelvin ) + 0.8776956e3f / kelvin + 0.179910f;
		if ( kelvin <= 2222.0f ) {
			xy.y = -1.1063814f * xy.x * xy.x * xy.x -
				1.34811020f * xy.x * xy.x + 2.18555832f * xy.x - 0.20219683f;
		} else {
			xy.y = -0.9549476f * xy.x * xy.x * xy.x -
				1.37418593f * xy.x * xy.x + 2.09137015f * xy.x - 0.16748867f;
		}
		return xy;
	}

	xy.x = -3.0258469e9f / ( kelvin * kelvin * kelvin ) +
		2.1070379e6f / ( kelvin * kelvin ) + 0.2226347e3f / kelvin + 0.240390f;
	xy.y = 3.0817580f * xy.x * xy.x * xy.x -
		5.87338670f * xy.x * xy.x + 3.75112997f * xy.x - 0.37001483f;
	return xy;
}

static inline ColorMathVec3 GLX_ColorMath_WhitePointXyzFromKelvin( float kelvin )
{
	const ColorMathXy xy = GLX_ColorMath_WhitePointXyFromKelvin( kelvin );
	const float y = xy.y > 0.000001f ? xy.y : 0.000001f;

	ColorMathVec3 out {};
	out.r = xy.x / y;
	out.g = 1.0f;
	out.b = ( 1.0f - xy.x - xy.y ) / y;
	return out;
}

static inline ColorMathVec3 GLX_ColorMath_XyzToBradfordLms( const ColorMathVec3 &xyz )
{
	const ColorMathVec3 finite = GLX_ColorMath_SanitizeVec3( xyz, 0.0f );
	ColorMathVec3 out {};
	out.r = finite.r * 0.8951f + finite.g * 0.2664f + finite.b * -0.1614f;
	out.g = finite.r * -0.7502f + finite.g * 1.7135f + finite.b * 0.0367f;
	out.b = finite.r * 0.0389f + finite.g * -0.0685f + finite.b * 1.0296f;
	return out;
}

static inline ColorMathVec3 GLX_ColorMath_BradfordLmsToXyz( const ColorMathVec3 &lms )
{
	const ColorMathVec3 finite = GLX_ColorMath_SanitizeVec3( lms, 0.0f );
	ColorMathVec3 out {};
	out.r = finite.r * 0.9869929f + finite.g * -0.1470543f + finite.b * 0.1599627f;
	out.g = finite.r * 0.4323053f + finite.g * 0.5183603f + finite.b * 0.0492912f;
	out.b = finite.r * -0.0085287f + finite.g * 0.0400428f + finite.b * 0.9684867f;
	return out;
}

static inline void GLX_ColorMath_BuildBradfordAdaptationMatrix( float sourceKelvin,
	float targetKelvin, float matrix[9] )
{
	const ColorMathVec3 source = GLX_ColorMath_WhitePointXyzFromKelvin( sourceKelvin );
	const ColorMathVec3 target = GLX_ColorMath_WhitePointXyzFromKelvin( targetKelvin );
	const ColorMathVec3 sourceLms = GLX_ColorMath_XyzToBradfordLms( source );
	const ColorMathVec3 targetLms = GLX_ColorMath_XyzToBradfordLms( target );
	const float scale[3] = {
		std::fabs( sourceLms.r ) > 0.000001f ? targetLms.r / sourceLms.r : 1.0f,
		std::fabs( sourceLms.g ) > 0.000001f ? targetLms.g / sourceLms.g : 1.0f,
		std::fabs( sourceLms.b ) > 0.000001f ? targetLms.b / sourceLms.b : 1.0f,
	};
	static constexpr float bradford[9] = {
		 0.8951f,  0.2664f, -0.1614f,
		-0.7502f,  1.7135f,  0.0367f,
		 0.0389f, -0.0685f,  1.0296f
	};
	static constexpr float bradfordInv[9] = {
		 0.9869929f, -0.1470543f,  0.1599627f,
		 0.4323053f,  0.5183603f,  0.0492912f,
		-0.0085287f,  0.0400428f,  0.9684867f
	};
	float scaledBradford[9];

	if ( !matrix ) {
		return;
	}

	for ( int row = 0; row < 3; row++ ) {
		for ( int col = 0; col < 3; col++ ) {
			scaledBradford[row * 3 + col] = scale[row] *
				bradford[row * 3 + col];
		}
	}

	for ( int row = 0; row < 3; row++ ) {
		for ( int col = 0; col < 3; col++ ) {
			matrix[row * 3 + col] =
				bradfordInv[row * 3 + 0] * scaledBradford[0 * 3 + col] +
				bradfordInv[row * 3 + 1] * scaledBradford[1 * 3 + col] +
				bradfordInv[row * 3 + 2] * scaledBradford[2 * 3 + col];
		}
	}
}

static inline ColorMathVec3 GLX_ColorMath_AdaptWhitePointBradford(
	const ColorMathVec3 &linearSrgb, float sourceKelvin, float targetKelvin )
{
	const ColorMathVec3 sourceLms = GLX_ColorMath_XyzToBradfordLms(
		GLX_ColorMath_WhitePointXyzFromKelvin( sourceKelvin ) );
	const ColorMathVec3 targetLms = GLX_ColorMath_XyzToBradfordLms(
		GLX_ColorMath_WhitePointXyzFromKelvin( targetKelvin ) );
	ColorMathVec3 lms = GLX_ColorMath_XyzToBradfordLms(
		GLX_ColorMath_LinearSrgbToXyz( linearSrgb ) );

	if ( std::fabs( sourceLms.r ) > 0.000001f ) {
		lms.r *= targetLms.r / sourceLms.r;
	}
	if ( std::fabs( sourceLms.g ) > 0.000001f ) {
		lms.g *= targetLms.g / sourceLms.g;
	}
	if ( std::fabs( sourceLms.b ) > 0.000001f ) {
		lms.b *= targetLms.b / sourceLms.b;
	}
	return GLX_ColorMath_XyzToLinearSrgb( GLX_ColorMath_BradfordLmsToXyz( lms ) );
}

static inline float GLX_ColorMath_Luma( const ColorMathVec3 &color )
{
	const ColorMathVec3 finite = GLX_ColorMath_SanitizeVec3( color, 0.0f );
	return finite.r * 0.2126f + finite.g * 0.7152f + finite.b * 0.0722f;
}

static inline float GLX_ColorMath_BloomMetric( const ColorMathVec3 &color, int mode )
{
	const ColorMathVec3 finite = GLX_ColorMath_SanitizeVec3( color, 0.0f );

	if ( mode == 1 ) {
		return ( finite.r + finite.g + finite.b ) * 0.33333333f;
	}
	if ( mode == 2 ) {
		return GLX_ColorMath_Luma( finite );
	}

	float maxChannel = finite.r > finite.g ? finite.r : finite.g;
	return maxChannel > finite.b ? maxChannel : finite.b;
}

static inline float GLX_ColorMath_SmoothStep( float edge0, float edge1, float value )
{
	edge0 = GLX_ColorMath_SanitizeFinite( edge0, 0.0f );
	edge1 = GLX_ColorMath_SanitizeFinite( edge1, 0.0f );
	value = GLX_ColorMath_SanitizeFinite( value, 0.0f );
	if ( edge1 <= edge0 ) {
		return value >= edge1 ? 1.0f : 0.0f;
	}

	const float t = GLX_ColorMath_Clamp01( ( value - edge0 ) / ( edge1 - edge0 ) );
	return t * t * ( 3.0f - 2.0f * t );
}

static inline float GLX_ColorMath_BloomWeight( const ColorMathVec3 &color, int mode,
	float threshold, float exposure, int toneMapMode, float softKnee )
{
	float metric = GLX_ColorMath_BloomMetric( color, mode );

	threshold = GLX_ColorMath_SanitizeFinite( threshold, 0.0f );
	exposure = GLX_ColorMath_SanitizeFinite( exposure, 0.0f );
	softKnee = GLX_ColorMath_SanitizeFinite( softKnee, 0.0f );
	if ( toneMapMode != 0 ) {
		metric *= exposure > 0.0f ? exposure : 0.0f;
	}
	if ( softKnee <= 0.0f ) {
		return metric >= threshold ? 1.0f : 0.0f;
	}

	const float knee = threshold * softKnee > 0.0001f ? threshold * softKnee : 0.0001f;
	return GLX_ColorMath_SmoothStep( threshold - knee, threshold + knee, metric );
}

static inline ColorMathVec3 GLX_ColorMath_FlareSceneColor(
	const ColorMathVec3 &displayColor, bool sceneLinear, bool legacyCompatibility )
{
	const ColorMathVec3 finite = GLX_ColorMath_Max0( displayColor );

	if ( !sceneLinear || legacyCompatibility ) {
		return finite;
	}

	ColorMathVec3 out {};
	out.r = GLX_ColorMath_SrgbToLinear( finite.r );
	out.g = GLX_ColorMath_SrgbToLinear( finite.g );
	out.b = GLX_ColorMath_SrgbToLinear( finite.b );
	return out;
}

static inline float GLX_ColorMath_SceneLinearBloomWeight( const ColorMathVec3 &color,
	int mode, float threshold, float exposure, float softKnee )
{
	ColorMathVec3 exposed = GLX_ColorMath_Max0( color );

	exposure = GLX_ColorMath_SanitizeFinite( exposure, 0.0f );
	if ( exposure < 0.0f ) {
		exposure = 0.0f;
	}
	exposed.r *= exposure;
	exposed.g *= exposure;
	exposed.b *= exposure;

	return GLX_ColorMath_BloomWeight( exposed, mode, threshold, 1.0f, 0, softKnee );
}

static inline void GLX_ColorMath_ExposureHistogramReset(
	ColorMathExposureHistogram *histogram, float minLog2, float maxLog2 )
{
	if ( !histogram ) {
		return;
	}
	for ( int i = 0; i < GLX_COLOR_MATH_EXPOSURE_HISTOGRAM_BINS; i++ ) {
		histogram->bins[i] = 0u;
	}
	minLog2 = GLX_ColorMath_SanitizeFinite( minLog2, -12.0f );
	maxLog2 = GLX_ColorMath_SanitizeFinite( maxLog2, 12.0f );
	if ( maxLog2 <= minLog2 + 0.001f ) {
		maxLog2 = minLog2 + 0.001f;
	}
	histogram->sampleCount = 0u;
	histogram->minLog2 = minLog2;
	histogram->maxLog2 = maxLog2;
	histogram->logLumaSum = 0.0f;
	histogram->lumaSum = 0.0f;
	histogram->minLuma = 1048576.0f;
	histogram->maxLuma = 0.0f;
}

static inline float GLX_ColorMath_ExposureLuma( const ColorMathVec3 &color )
{
	const float luma = GLX_ColorMath_Luma( GLX_ColorMath_Max0( color ) );

	if ( !std::isfinite( luma ) || luma <= 0.000001f ) {
		return 0.000001f;
	}
	return luma > 1048576.0f ? 1048576.0f : luma;
}

static inline bool GLX_ColorMath_ExposureHistogramAddLuma(
	ColorMathExposureHistogram *histogram, float luma )
{
	float logLuma;
	float t;
	int bin;

	if ( !histogram ) {
		return false;
	}
	luma = GLX_ColorMath_SanitizeFinite( luma, 0.0f );
	if ( luma <= 0.0f ) {
		return false;
	}
	luma = luma > 1048576.0f ? 1048576.0f : luma;
	logLuma = std::log2( luma );
	logLuma = GLX_ColorMath_Clamp( logLuma, histogram->minLog2, histogram->maxLog2 );
	t = ( logLuma - histogram->minLog2 ) /
		( histogram->maxLog2 - histogram->minLog2 );
	bin = static_cast<int>( std::floor( t *
		static_cast<float>( GLX_COLOR_MATH_EXPOSURE_HISTOGRAM_BINS ) ) );
	if ( bin < 0 ) {
		bin = 0;
	} else if ( bin >= GLX_COLOR_MATH_EXPOSURE_HISTOGRAM_BINS ) {
		bin = GLX_COLOR_MATH_EXPOSURE_HISTOGRAM_BINS - 1;
	}
	histogram->bins[bin]++;
	histogram->sampleCount++;
	histogram->logLumaSum += logLuma;
	histogram->lumaSum += luma;
	histogram->minLuma = luma < histogram->minLuma ? luma : histogram->minLuma;
	histogram->maxLuma = luma > histogram->maxLuma ? luma : histogram->maxLuma;
	return true;
}

static inline bool GLX_ColorMath_ExposureHistogramAddColor(
	ColorMathExposureHistogram *histogram, const ColorMathVec3 &color )
{
	return GLX_ColorMath_ExposureHistogramAddLuma( histogram,
		GLX_ColorMath_ExposureLuma( color ) );
}

static inline float GLX_ColorMath_ExposureScaleForLuma( float measuredLuma,
	float targetLuma, float minScale, float maxScale )
{
	measuredLuma = GLX_ColorMath_SanitizeFiniteRange( measuredLuma,
		0.000001f, 1048576.0f, 1.0f );
	targetLuma = GLX_ColorMath_SanitizeFiniteRange( targetLuma,
		0.000001f, 64.0f, 0.18f );
	return GLX_ColorMath_Clamp( targetLuma / measuredLuma, minScale, maxScale );
}

static inline ColorMathExposureResult GLX_ColorMath_ExposureResultForLogLuma(
	float measuredLog2Luma, float targetLuma, float minScale, float maxScale, int bin )
{
	ColorMathExposureResult result {};

	measuredLog2Luma = GLX_ColorMath_SanitizeFiniteRange( measuredLog2Luma,
		-24.0f, 24.0f, 0.0f );
	result.valid = true;
	result.measuredLog2Luma = measuredLog2Luma;
	result.measuredLuma = std::pow( 2.0f, measuredLog2Luma );
	result.exposureScale = GLX_ColorMath_ExposureScaleForLuma( result.measuredLuma,
		targetLuma, minScale, maxScale );
	result.bin = bin;
	return result;
}

static inline ColorMathExposureResult GLX_ColorMath_ExposureSimpleAverage(
	const ColorMathExposureHistogram &histogram, float targetLuma,
	float minScale, float maxScale )
{
	ColorMathExposureResult result {};

	if ( histogram.sampleCount == 0u ) {
		return result;
	}
	return GLX_ColorMath_ExposureResultForLogLuma(
		histogram.logLumaSum / static_cast<float>( histogram.sampleCount ),
		targetLuma, minScale, maxScale, -1 );
}

static inline ColorMathExposureResult GLX_ColorMath_ExposureHistogramPercentile(
	const ColorMathExposureHistogram &histogram, float percentile, float targetLuma,
	float minScale, float maxScale )
{
	ColorMathExposureResult result {};
	unsigned int threshold;
	unsigned int cumulative = 0u;
	float binCenter;

	if ( histogram.sampleCount == 0u ) {
		return result;
	}
	percentile = GLX_ColorMath_SanitizeFiniteRange( percentile, 1.0f, 99.0f, 80.0f );
	threshold = static_cast<unsigned int>( std::ceil(
		static_cast<float>( histogram.sampleCount ) * ( percentile * 0.01f ) ) );
	if ( threshold == 0u ) {
		threshold = 1u;
	}
	for ( int i = 0; i < GLX_COLOR_MATH_EXPOSURE_HISTOGRAM_BINS; i++ ) {
		cumulative += histogram.bins[i];
		if ( cumulative >= threshold ) {
			binCenter = ( static_cast<float>( i ) + 0.5f ) /
				static_cast<float>( GLX_COLOR_MATH_EXPOSURE_HISTOGRAM_BINS );
			return GLX_ColorMath_ExposureResultForLogLuma(
				histogram.minLog2 +
				binCenter * ( histogram.maxLog2 - histogram.minLog2 ),
				targetLuma, minScale, maxScale, i );
		}
	}

	return GLX_ColorMath_ExposureResultForLogLuma( histogram.maxLog2,
		targetLuma, minScale, maxScale,
		GLX_COLOR_MATH_EXPOSURE_HISTOGRAM_BINS - 1 );
}

static inline bool GLX_ColorMath_LutAtlasSize( int width, int height, int *size )
{
	if ( width <= 0 || height <= 0 ) {
		return false;
	}
	if ( static_cast<long long>( width ) != static_cast<long long>( height ) * height ) {
		return false;
	}
	if ( height < 2 || height > 64 ) {
		return false;
	}
	if ( size ) {
		*size = height;
	}
	return true;
}

static inline bool GLX_ColorMath_LutAtlasAddress( int size, const ColorMathVec3 &color,
	float scale, ColorMathLutAddress *address )
{
	if ( !address || size < 2 || size > 64 ) {
		return false;
	}
	scale = GLX_ColorMath_SanitizeFiniteRange( scale, 0.0001f, 64.0f, 1.0f );

	const float maxIndex = static_cast<float>( size - 1 );
	const float fr = GLX_ColorMath_Clamp01( color.r / scale ) * maxIndex;
	const float fg = GLX_ColorMath_Clamp01( color.g / scale ) * maxIndex;
	const float fb = GLX_ColorMath_Clamp01( color.b / scale ) * maxIndex;

	address->r0 = static_cast<int>( std::floor( fr ) );
	address->g0 = static_cast<int>( std::floor( fg ) );
	address->b0 = static_cast<int>( std::floor( fb ) );
	address->r1 = address->r0 < size - 1 ? address->r0 + 1 : address->r0;
	address->g1 = address->g0 < size - 1 ? address->g0 + 1 : address->g0;
	address->b1 = address->b0 < size - 1 ? address->b0 + 1 : address->b0;
	address->tr = fr - static_cast<float>( address->r0 );
	address->tg = fg - static_cast<float>( address->g0 );
	address->tb = fb - static_cast<float>( address->b0 );
	return true;
}

static inline int GLX_ColorMath_LutAtlasIndex( int size, int r, int g, int b )
{
	return g * size * size + b * size + r;
}

static inline ColorMathVec3 GLX_ColorMath_LutIdentityTexel( int size, int x, int y,
	float scale )
{
	ColorMathVec3 out {};
	if ( size < 2 ) {
		return out;
	}
	scale = GLX_ColorMath_SanitizeFiniteRange( scale, 0.0001f, 64.0f, 1.0f );

	const int r = x % size;
	const int b = x / size;
	out.r = static_cast<float>( r ) * scale / static_cast<float>( size - 1 );
	out.g = static_cast<float>( y ) * scale / static_cast<float>( size - 1 );
	out.b = static_cast<float>( b ) * scale / static_cast<float>( size - 1 );
	return out;
}

static inline ColorMathVec3 GLX_ColorMath_Mix( const ColorMathVec3 &a,
	const ColorMathVec3 &b, float t )
{
	const ColorMathVec3 finiteA = GLX_ColorMath_SanitizeVec3( a, 0.0f );
	const ColorMathVec3 finiteB = GLX_ColorMath_SanitizeVec3( b, 0.0f );
	t = GLX_ColorMath_Clamp01( t );

	ColorMathVec3 out {};
	out.r = finiteA.r + ( finiteB.r - finiteA.r ) * t;
	out.g = finiteA.g + ( finiteB.g - finiteA.g ) * t;
	out.b = finiteA.b + ( finiteB.b - finiteA.b ) * t;
	return out;
}

static inline ColorMathVec3 GLX_ColorMath_SampleLutAtlas( const ColorMathVec3 *atlas,
	int width, int height, const ColorMathVec3 &color, float scale )
{
	int size = 0;
	ColorMathLutAddress address {};

	if ( !atlas || !GLX_ColorMath_LutAtlasSize( width, height, &size ) ||
		!GLX_ColorMath_LutAtlasAddress( size, color, scale, &address ) ) {
		return color;
	}

	const ColorMathVec3 c000 = atlas[GLX_ColorMath_LutAtlasIndex( size,
		address.r0, address.g0, address.b0 )];
	const ColorMathVec3 c100 = atlas[GLX_ColorMath_LutAtlasIndex( size,
		address.r1, address.g0, address.b0 )];
	const ColorMathVec3 c010 = atlas[GLX_ColorMath_LutAtlasIndex( size,
		address.r0, address.g1, address.b0 )];
	const ColorMathVec3 c110 = atlas[GLX_ColorMath_LutAtlasIndex( size,
		address.r1, address.g1, address.b0 )];
	const ColorMathVec3 c001 = atlas[GLX_ColorMath_LutAtlasIndex( size,
		address.r0, address.g0, address.b1 )];
	const ColorMathVec3 c101 = atlas[GLX_ColorMath_LutAtlasIndex( size,
		address.r1, address.g0, address.b1 )];
	const ColorMathVec3 c011 = atlas[GLX_ColorMath_LutAtlasIndex( size,
		address.r0, address.g1, address.b1 )];
	const ColorMathVec3 c111 = atlas[GLX_ColorMath_LutAtlasIndex( size,
		address.r1, address.g1, address.b1 )];

	const ColorMathVec3 x00 = GLX_ColorMath_Mix( c000, c100, address.tr );
	const ColorMathVec3 x10 = GLX_ColorMath_Mix( c010, c110, address.tr );
	const ColorMathVec3 x01 = GLX_ColorMath_Mix( c001, c101, address.tr );
	const ColorMathVec3 x11 = GLX_ColorMath_Mix( c011, c111, address.tr );
	const ColorMathVec3 y0 = GLX_ColorMath_Mix( x00, x10, address.tg );
	const ColorMathVec3 y1 = GLX_ColorMath_Mix( x01, x11, address.tg );
	return GLX_ColorMath_Mix( y0, y1, address.tb );
}

} // namespace glx

#endif // GLX_COLOR_MATH_H
