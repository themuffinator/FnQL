#ifndef GLX_POST_SHADER_SOURCE_H
#define GLX_POST_SHADER_SOURCE_H

#include "glx_post_shader_plan.h"

namespace glx {

static constexpr int GLX_POST_SHADER_SOURCE_VERSION = 8;
static constexpr int GLX_POST_SHADER_VERTEX_SOURCE_BYTES = 1024;
static constexpr int GLX_POST_SHADER_FRAGMENT_SOURCE_BYTES = 30000;

enum class PostShaderSourceTarget {
	Glsl120,
	Glsl130,
	Glsl150Compatibility,
	Glsl330Compatibility,
	Glsl410Compatibility
};

struct PostShaderSourceSummary {
	qboolean valid;
	qboolean truncated;
	PostShaderSourceTarget target;
	int targetVersion;
	unsigned int sourceHash;
	unsigned int featureMask;
	int vertexBytes;
	int fragmentBytes;
};

static ID_INLINE const char *GLX_PostShaderSource_TargetName(
	PostShaderSourceTarget target )
{
	switch ( target ) {
	case PostShaderSourceTarget::Glsl120:
		return "glsl120";
	case PostShaderSourceTarget::Glsl130:
		return "glsl130";
	case PostShaderSourceTarget::Glsl150Compatibility:
		return "glsl150-compat";
	case PostShaderSourceTarget::Glsl330Compatibility:
		return "glsl330-compat";
	case PostShaderSourceTarget::Glsl410Compatibility:
		return "glsl410-compat";
	default:
		return "unknown";
	}
}

static ID_INLINE int GLX_PostShaderSource_TargetVersion(
	PostShaderSourceTarget target )
{
	switch ( target ) {
	case PostShaderSourceTarget::Glsl120:
		return 120;
	case PostShaderSourceTarget::Glsl130:
		return 130;
	case PostShaderSourceTarget::Glsl150Compatibility:
		return 150;
	case PostShaderSourceTarget::Glsl330Compatibility:
		return 330;
	case PostShaderSourceTarget::Glsl410Compatibility:
		return 410;
	default:
		return 0;
	}
}

static ID_INLINE qboolean GLX_PostShaderSource_ModernTarget(
	PostShaderSourceTarget target )
{
	return target == PostShaderSourceTarget::Glsl120 ? qfalse : qtrue;
}

static ID_INLINE qboolean GLX_PostShaderSource_TargetSupportedByVersion(
	PostShaderSourceTarget target, int major, int minor )
{
	switch ( target ) {
	case PostShaderSourceTarget::Glsl120:
		(void)major;
		(void)minor;
		return qtrue;
	case PostShaderSourceTarget::Glsl130:
		return major > 3 || ( major == 3 && minor >= 0 ) ? qtrue : qfalse;
	case PostShaderSourceTarget::Glsl150Compatibility:
		return major > 3 || ( major == 3 && minor >= 2 ) ? qtrue : qfalse;
	case PostShaderSourceTarget::Glsl330Compatibility:
		return major > 3 || ( major == 3 && minor >= 3 ) ? qtrue : qfalse;
	case PostShaderSourceTarget::Glsl410Compatibility:
		return major > 4 || ( major == 4 && minor >= 1 ) ? qtrue : qfalse;
	default:
		return qfalse;
	}
}

static ID_INLINE PostShaderSourceTarget GLX_PostShaderSource_TargetForTier(
	RenderProductTier tier, int major, int minor )
{
	if ( tier == RenderProductTier::GL46 || tier == RenderProductTier::GL41 ) {
		if ( GLX_PostShaderSource_TargetSupportedByVersion(
			PostShaderSourceTarget::Glsl410Compatibility, major, minor ) ) {
			return PostShaderSourceTarget::Glsl410Compatibility;
		}
	}
	if ( tier == RenderProductTier::GL3X || tier == RenderProductTier::GL41 ||
		tier == RenderProductTier::GL46 ) {
		if ( GLX_PostShaderSource_TargetSupportedByVersion(
			PostShaderSourceTarget::Glsl330Compatibility, major, minor ) ) {
			return PostShaderSourceTarget::Glsl330Compatibility;
		}
		if ( GLX_PostShaderSource_TargetSupportedByVersion(
			PostShaderSourceTarget::Glsl150Compatibility, major, minor ) ) {
			return PostShaderSourceTarget::Glsl150Compatibility;
		}
		if ( GLX_PostShaderSource_TargetSupportedByVersion(
			PostShaderSourceTarget::Glsl130, major, minor ) ) {
			return PostShaderSourceTarget::Glsl130;
		}
	}
	return PostShaderSourceTarget::Glsl120;
}

static ID_INLINE int GLX_PostShaderSource_StringLength( const char *text )
{
	int length = 0;
	if ( !text ) {
		return 0;
	}
	while ( text[length] ) {
		length++;
	}
	return length;
}

static ID_INLINE qboolean GLX_PostShaderSource_Append( char *out, int outSize,
	int *used, const char *text )
{
	const int length = GLX_PostShaderSource_StringLength( text );
	int copied = 0;

	if ( !used || !text ) {
		return qfalse;
	}

	if ( out && outSize > 0 ) {
		if ( *used < outSize - 1 ) {
			const int room = outSize - 1 - *used;
			const int toCopy = length < room ? length : room;
			for ( copied = 0; copied < toCopy; copied++ ) {
				out[*used + copied] = text[copied];
			}
			out[*used + copied] = '\0';
		} else {
			out[outSize - 1] = '\0';
		}
	}

	*used += length;
	return ( !out || outSize <= 0 || *used < outSize ) ? qtrue : qfalse;
}

static ID_INLINE qboolean GLX_PostShaderSource_AppendFeatureDefine( char *out,
	int outSize, int *used, const char *name, qboolean enabled )
{
	qboolean ok = qtrue;
	ok = ( GLX_PostShaderSource_Append( out, outSize, used, "#define " ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_Append( out, outSize, used, name ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_Append( out, outSize, used, enabled ? " 1\n" : " 0\n" ) && ok ) ? qtrue : qfalse;
	return ok;
}

static ID_INLINE qboolean GLX_PostShaderSource_FeatureEnabled(
	const PostShaderPlan &plan, unsigned int feature )
{
	return ( plan.featureMask & feature ) != 0u ? qtrue : qfalse;
}

static ID_INLINE unsigned int GLX_PostShaderSource_HashText( unsigned int hash,
	const char *text )
{
	if ( !text ) {
		return hash;
	}
	while ( *text ) {
		hash ^= static_cast<unsigned int>( static_cast<unsigned char>( *text ) );
		hash *= 16777619u;
		text++;
	}
	return hash;
}

static ID_INLINE qboolean GLX_PostShaderSource_WriteVersion(
	PostShaderSourceTarget target, char *out, int outSize, int *used )
{
	switch ( target ) {
	case PostShaderSourceTarget::Glsl120:
		return GLX_PostShaderSource_Append( out, outSize, used, "#version 120\n" );
	case PostShaderSourceTarget::Glsl130:
		return GLX_PostShaderSource_Append( out, outSize, used, "#version 130\n" );
	case PostShaderSourceTarget::Glsl150Compatibility:
		return GLX_PostShaderSource_Append( out, outSize, used,
			"#version 150 compatibility\n" );
	case PostShaderSourceTarget::Glsl330Compatibility:
		return GLX_PostShaderSource_Append( out, outSize, used,
			"#version 330 compatibility\n" );
	case PostShaderSourceTarget::Glsl410Compatibility:
		return GLX_PostShaderSource_Append( out, outSize, used,
			"#version 410 compatibility\n" );
	default:
		return qfalse;
	}
}

static ID_INLINE qboolean GLX_PostShaderSource_WriteVertex(
	PostShaderSourceTarget target, char *out, int outSize, int *bytes )
{
	int used = 0;
	qboolean ok = qtrue;

	if ( out && outSize > 0 ) {
		out[0] = '\0';
	}

	ok = ( GLX_PostShaderSource_WriteVersion( target, out, outSize, &used ) && ok ) ?
		qtrue : qfalse;
	ok = ( GLX_PostShaderSource_Append( out, outSize, &used,
		GLX_PostShaderSource_ModernTarget( target ) ?
		"out vec2 v_TexCoord;\n" :
		"varying vec2 v_TexCoord;\n" ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_Append( out, outSize, &used,
		"void main() {\n"
		"	v_TexCoord = gl_MultiTexCoord0.st;\n"
		"	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
		"}\n" ) && ok ) ? qtrue : qfalse;

	if ( bytes ) {
		*bytes = used;
	}
	return ok;
}

static ID_INLINE qboolean GLX_PostShaderSource_WriteVertex( char *out, int outSize,
	int *bytes )
{
	return GLX_PostShaderSource_WriteVertex( PostShaderSourceTarget::Glsl120,
		out, outSize, bytes );
}

static ID_INLINE qboolean GLX_PostShaderSource_WriteFeatureDefines(
	const PostShaderPlan &plan, char *out, int outSize, int *used )
{
	qboolean ok = qtrue;

	ok = ( GLX_PostShaderSource_AppendFeatureDefine( out, outSize, used,
		"GLX_POST_LEGACY_GAMMA",
		GLX_PostShaderSource_FeatureEnabled( plan, GLX_POST_SHADER_FEATURE_LEGACY_GAMMA ) ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_AppendFeatureDefine( out, outSize, used,
		"GLX_POST_SCENE_LINEAR",
		GLX_PostShaderSource_FeatureEnabled( plan, GLX_POST_SHADER_FEATURE_SCENE_LINEAR ) ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_AppendFeatureDefine( out, outSize, used,
		"GLX_POST_LIFT_GAMMA_GAIN",
		GLX_PostShaderSource_FeatureEnabled( plan, GLX_POST_SHADER_FEATURE_LIFT_GAMMA_GAIN ) ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_AppendFeatureDefine( out, outSize, used,
		"GLX_POST_WHITE_POINT",
		GLX_PostShaderSource_FeatureEnabled( plan, GLX_POST_SHADER_FEATURE_WHITE_POINT ) ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_AppendFeatureDefine( out, outSize, used,
		"GLX_POST_LUT_3D",
		GLX_PostShaderSource_FeatureEnabled( plan, GLX_POST_SHADER_FEATURE_LUT_3D ) ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_AppendFeatureDefine( out, outSize, used,
		"GLX_POST_TONEMAP_REINHARD_SIMPLE",
		GLX_PostShaderSource_FeatureEnabled( plan, GLX_POST_SHADER_FEATURE_TONEMAP_REINHARD_SIMPLE ) ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_AppendFeatureDefine( out, outSize, used,
		"GLX_POST_TONEMAP_ACES_FITTED",
		GLX_PostShaderSource_FeatureEnabled( plan, GLX_POST_SHADER_FEATURE_TONEMAP_ACES_FITTED ) ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_Append( out, outSize, used,
		"#define GLX_POST_TONEMAP_REINHARD GLX_POST_TONEMAP_REINHARD_SIMPLE\n"
		"#define GLX_POST_TONEMAP_ACES GLX_POST_TONEMAP_ACES_FITTED\n" ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_AppendFeatureDefine( out, outSize, used,
		"GLX_POST_ENCODE_SRGB",
		GLX_PostShaderSource_FeatureEnabled( plan, GLX_POST_SHADER_FEATURE_ENCODE_SRGB ) ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_AppendFeatureDefine( out, outSize, used,
		"GLX_POST_ENCODE_HDR10_PQ",
		GLX_PostShaderSource_FeatureEnabled( plan, GLX_POST_SHADER_FEATURE_ENCODE_HDR10_PQ ) ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_AppendFeatureDefine( out, outSize, used,
		"GLX_POST_LINEAR_OUTPUT",
		GLX_PostShaderSource_FeatureEnabled( plan, GLX_POST_SHADER_FEATURE_LINEAR_OUTPUT ) ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_AppendFeatureDefine( out, outSize, used,
		"GLX_POST_BT2020_OUTPUT",
		GLX_PostShaderSource_FeatureEnabled( plan, GLX_POST_SHADER_FEATURE_BT2020_OUTPUT ) ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_AppendFeatureDefine( out, outSize, used,
		"GLX_POST_GAMUT_COMPRESS",
		GLX_PostShaderSource_FeatureEnabled( plan, GLX_POST_SHADER_FEATURE_GAMUT_COMPRESS ) ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_AppendFeatureDefine( out, outSize, used,
		"GLX_POST_GAMUT_CLIP",
		GLX_PostShaderSource_FeatureEnabled( plan, GLX_POST_SHADER_FEATURE_GAMUT_CLIP ) ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_AppendFeatureDefine( out, outSize, used,
		"GLX_POST_OUTPUT_TRANSFORM",
		GLX_PostShaderSource_FeatureEnabled( plan, GLX_POST_SHADER_FEATURE_OUTPUT_TRANSFORM ) ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_AppendFeatureDefine( out, outSize, used,
		"GLX_POST_BLOOM_COMBINE",
		GLX_PostShaderSource_FeatureEnabled( plan, GLX_POST_SHADER_FEATURE_BLOOM_COMBINE ) ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_AppendFeatureDefine( out, outSize, used,
		"GLX_POST_GREYSCALE",
		GLX_PostShaderSource_FeatureEnabled( plan, GLX_POST_SHADER_FEATURE_GREYSCALE ) ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_AppendFeatureDefine( out, outSize, used,
		"GLX_POST_DISPLAY_P3_OUTPUT",
		GLX_PostShaderSource_FeatureEnabled( plan, GLX_POST_SHADER_FEATURE_DISPLAY_P3_OUTPUT ) ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_AppendFeatureDefine( out, outSize, used,
		"GLX_POST_HDR_HEADROOM_OUTPUT",
		GLX_PostShaderSource_FeatureEnabled( plan, GLX_POST_SHADER_FEATURE_HDR_HEADROOM_OUTPUT ) ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_AppendFeatureDefine( out, outSize, used,
		"GLX_POST_CRT",
		GLX_PostShaderSource_FeatureEnabled( plan, GLX_POST_SHADER_FEATURE_CRT ) ) && ok ) ? qtrue : qfalse;

	return ok;
}

static ID_INLINE qboolean GLX_PostShaderSource_WriteFragment(
	const PostShaderPlan &plan, PostShaderSourceTarget target, char *out,
	int outSize, int *bytes )
{
	int used = 0;
	qboolean ok = plan.valid;

	if ( out && outSize > 0 ) {
		out[0] = '\0';
	}
	if ( !plan.valid ) {
		if ( bytes ) {
			*bytes = 0;
		}
		return qfalse;
	}

	ok = ( GLX_PostShaderSource_WriteVersion( target, out, outSize, &used ) && ok ) ?
		qtrue : qfalse;
	ok = ( GLX_PostShaderSource_Append( out, outSize, &used,
		"// GLx generated post/output shader source v8.\n" ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_WriteFeatureDefines( plan, out, outSize, &used ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_Append( out, outSize, &used,
		GLX_PostShaderSource_ModernTarget( target ) ?
		"in vec2 v_TexCoord;\n"
		"#define GLX_POST_SAMPLE2D texture\n" :
		"varying vec2 v_TexCoord;\n"
		"#define GLX_POST_SAMPLE2D texture2D\n"
		"#define glx_FragColor gl_FragColor\n" ) && ok ) ? qtrue : qfalse;
	if ( GLX_PostShaderSource_ModernTarget( target ) ) {
		ok = ( GLX_PostShaderSource_Append( out, outSize, &used,
			GLX_PostShaderSource_TargetVersion( target ) >= 330 ?
			"layout(location = 0) out vec4 glx_FragColor;\n" :
			"out vec4 glx_FragColor;\n" ) && ok ) ? qtrue : qfalse;
	}
	ok = ( GLX_PostShaderSource_Append( out, outSize, &used,
		"uniform sampler2D u_Scene;\n"
		"uniform sampler2D u_Bloom;\n"
		"uniform sampler2D u_ColorGradeLut;\n" ) && ok ) ? qtrue : qfalse;
	ok = ( GLX_PostShaderSource_Append( out, outSize, &used,
		"uniform vec4 u_PostParams0; // exposure, paper white, max output, greyscale\n"
		"uniform vec4 u_OutputParams1; // headroom, display SDR white, display max, unused\n"
		"uniform vec4 u_LegacyParams; // gamma, overbright, unused, unused\n"
		"uniform vec4 u_BloomParams; // intensity, unused, unused, unused\n"
		"uniform vec4 u_CrtParams0; // amount, scanline strength, mask strength, curvature\n"
		"uniform vec4 u_CrtParams1; // chromatic, time seconds, inv width, inv height\n"
		"uniform vec4 u_Lift;\n"
		"uniform vec4 u_InvGamma;\n"
		"uniform vec4 u_Gain;\n"
		"uniform vec4 u_WhitePoint0;\n"
		"uniform vec4 u_WhitePoint1;\n"
		"uniform vec4 u_WhitePoint2;\n"
		"uniform vec4 u_LutParams; // scale, sizeMinusOne, texelCenter, invScale\n"
		"const float GLX_POST_SAFE_MAX = 65504.0;\n"
		"float glxFiniteOr(float value, float fallback) {\n"
		"	if (!(value == value)) { return fallback; }\n"
		"	if (value > GLX_POST_SAFE_MAX || value < -GLX_POST_SAFE_MAX) { return fallback; }\n"
		"	return value;\n"
		"}\n"
		"float glxClampFinite(float value, float fallback, float minValue, float maxValue) {\n"
		"	return clamp(glxFiniteOr(value, fallback), minValue, maxValue);\n"
		"}\n"
		"vec3 glxFiniteVec3(vec3 color, vec3 fallback) {\n"
		"	return vec3(glxFiniteOr(color.r, fallback.r), glxFiniteOr(color.g, fallback.g), glxFiniteOr(color.b, fallback.b));\n"
		"}\n"
		"vec3 glxNonNegativeVec3(vec3 color) { return max(glxFiniteVec3(color, vec3(0.0)), vec3(0.0)); }\n"
		"vec3 glxSaturate(vec3 color) { return clamp(glxFiniteVec3(color, vec3(0.0)), 0.0, 1.0); }\n"
		"float glxExposure() { return glxClampFinite(u_PostParams0.x, 1.0, 0.0, 64.0); }\n"
		"float glxPaperWhite() { return glxClampFinite(u_PostParams0.y, 203.0, 1.0, 10000.0); }\n"
		"float glxMaxOutput() {\n"
		"	float paperWhite = glxPaperWhite();\n"
		"	return max(glxClampFinite(u_PostParams0.z, paperWhite, paperWhite, 10000.0), paperWhite);\n"
		"}\n"
		"float glxHeadroom() {\n"
		"	float fallback = max(glxMaxOutput() / max(glxPaperWhite(), 0.001), 1.0);\n"
		"	return max(glxClampFinite(u_OutputParams1.x, fallback, 1.0, 64.0), 1.0);\n"
		"}\n"
		"float glxGreyscaleAmount() { return glxClampFinite(u_PostParams0.w, 0.0, 0.0, 1.0); }\n"
		"float glxLegacyGamma() { return glxClampFinite(u_LegacyParams.x, 1.0, 0.01, 16.0); }\n"
		"float glxLegacyOverbright() { return glxClampFinite(u_LegacyParams.y, 1.0, 0.0, 64.0); }\n"
		"float glxBloomIntensity() { return glxClampFinite(u_BloomParams.x, 0.0, 0.0, 64.0); }\n"
		"vec3 glxLinearToSrgb(vec3 color) {\n"
		"	color = glxNonNegativeVec3(color);\n"
		"	vec3 lo = color * 12.92;\n"
		"	vec3 hi = 1.055 * pow(max(color, vec3(0.0)), vec3(1.0 / 2.4)) - vec3(0.055);\n"
		"	return glxFiniteVec3(mix(hi, lo, step(color, vec3(0.0031308))), vec3(0.0));\n"
		"}\n"
		"vec3 glxApplyLiftGammaGain(vec3 color) {\n"
		"	vec3 lift = glxFiniteVec3(u_Lift.xyz, vec3(0.0));\n"
		"	vec3 invGamma = vec3(glxClampFinite(u_InvGamma.x, 1.0, 0.0001, 10000.0), glxClampFinite(u_InvGamma.y, 1.0, 0.0001, 10000.0), glxClampFinite(u_InvGamma.z, 1.0, 0.0001, 10000.0));\n"
		"	vec3 gain = vec3(glxClampFinite(u_Gain.x, 1.0, 0.0, 64.0), glxClampFinite(u_Gain.y, 1.0, 0.0, 64.0), glxClampFinite(u_Gain.z, 1.0, 0.0, 64.0));\n"
		"	return glxNonNegativeVec3(pow(glxNonNegativeVec3(color + lift), invGamma) * gain);\n"
		"}\n"
		"vec3 glxApplyWhitePoint(vec3 color) {\n"
		"	color = glxNonNegativeVec3(color);\n"
		"	vec3 row0 = glxFiniteVec3(u_WhitePoint0.xyz, vec3(1.0, 0.0, 0.0));\n"
		"	vec3 row1 = glxFiniteVec3(u_WhitePoint1.xyz, vec3(0.0, 1.0, 0.0));\n"
		"	vec3 row2 = glxFiniteVec3(u_WhitePoint2.xyz, vec3(0.0, 0.0, 1.0));\n"
		"	return glxNonNegativeVec3(vec3(dot(row0, color), dot(row1, color), dot(row2, color)));\n"
		"}\n"
		"vec3 glxSampleLutAtlas(vec3 color) {\n"
		"	float lutScale = glxClampFinite(u_LutParams.x, 4.0, 0.001, 64.0);\n"
		"	float sizeMinusOne = glxClampFinite(u_LutParams.y, 1.0, 1.0, 63.0);\n"
		"	float size = sizeMinusOne + 1.0;\n"
		"	float invScale = glxClampFinite(u_LutParams.w, 1.0 / lutScale, 0.000015258789, 1000.0);\n"
		"	vec3 p = clamp(glxNonNegativeVec3(color) * invScale, 0.0, 1.0) * sizeMinusOne;\n"
		"	float slice = floor(p.b);\n"
		"	float nextSlice = min(slice + 1.0, sizeMinusOne);\n"
		"	vec2 uv0 = (vec2(p.r + slice * size, p.g) + vec2(0.5)) / vec2(size * size, size);\n"
		"	vec2 uv1 = (vec2(p.r + nextSlice * size, p.g) + vec2(0.5)) / vec2(size * size, size);\n"
		"	vec3 sample0 = glxFiniteVec3(GLX_POST_SAMPLE2D(u_ColorGradeLut, uv0).rgb, vec3(0.0));\n"
		"	vec3 sample1 = glxFiniteVec3(GLX_POST_SAMPLE2D(u_ColorGradeLut, uv1).rgb, vec3(0.0));\n"
		"	return glxNonNegativeVec3(mix(sample0, sample1, fract(p.b)) * lutScale);\n"
		"}\n"
		"vec3 glxToneMapReinhardSimple(vec3 color) {\n"
		"	color = glxNonNegativeVec3(color);\n"
		"	return glxFiniteVec3(color / (color + vec3(1.0)), vec3(0.0));\n"
		"}\n"
		"vec3 glxToneMapReinhard(vec3 color) { return glxToneMapReinhardSimple(color); }\n"
		"vec3 glxToneMapAcesFitted(vec3 color) {\n"
		"	const float a = 2.51;\n"
		"	const float b = 0.03;\n"
		"	const float c = 2.43;\n"
		"	const float d = 0.59;\n"
		"	const float e = 0.14;\n"
		"	color = glxNonNegativeVec3(color);\n"
		"	return glxSaturate(glxFiniteVec3((color * (a * color + vec3(b))) / (color * (c * color + vec3(d)) + vec3(e)), vec3(0.0)));\n"
		"}\n"
		"vec3 glxToneMapAces(vec3 color) { return glxToneMapAcesFitted(color); }\n"
		"vec3 glxLinearSrgbToBt2020(vec3 color) {\n"
		"	return glxFiniteVec3(mat3(0.6274, 0.0691, 0.0164, 0.3293, 0.9195, 0.0880, 0.0433, 0.0114, 0.8956) * glxNonNegativeVec3(color), vec3(0.0));\n"
		"}\n"
		"vec3 glxLinearSrgbToDisplayP3(vec3 color) {\n"
		"	return glxFiniteVec3(mat3(0.8225, 0.0332, 0.0171, 0.1775, 0.9668, 0.0724, 0.0000, 0.0000, 0.9105) * glxNonNegativeVec3(color), vec3(0.0));\n"
		"}\n"
		"vec3 glxPqEncode(vec3 nits) {\n"
		"	const float m1 = 0.1593017578125;\n"
		"	const float m2 = 78.84375;\n"
		"	const float c1 = 0.8359375;\n"
		"	const float c2 = 18.8515625;\n"
		"	const float c3 = 18.6875;\n"
		"	vec3 clampedNits = clamp(glxNonNegativeVec3(nits), 0.0, glxMaxOutput());\n"
		"	vec3 p = pow(clampedNits / 10000.0, vec3(m1));\n"
		"	return glxFiniteVec3(pow((vec3(c1) + c2 * p) / (vec3(1.0) + c3 * p), vec3(m2)), vec3(0.0));\n"
		"}\n"
		"vec3 glxApplyOutputPrimaries(vec3 color) {\n"
		"	color = glxNonNegativeVec3(color);\n"
		"#if GLX_POST_DISPLAY_P3_OUTPUT\n"
		"	return glxLinearSrgbToDisplayP3(color);\n"
		"#elif GLX_POST_BT2020_OUTPUT\n"
		"	return glxLinearSrgbToBt2020(color);\n"
		"#else\n"
		"	return color;\n"
		"#endif\n"
		"}\n"
		"vec3 glxApplyGamutMap(vec3 color) {\n"
		"	color = glxNonNegativeVec3(color);\n"
		"#if GLX_POST_GAMUT_COMPRESS\n"
		"	return clamp(color, 0.0, glxHeadroom());\n"
		"#elif GLX_POST_GAMUT_CLIP\n"
		"	return glxSaturate(color);\n"
		"#else\n"
		"	return color;\n"
		"#endif\n"
		"}\n"
		"vec3 glxEncodeTransfer(vec3 color) {\n"
		"	color = glxNonNegativeVec3(color);\n"
		"#if GLX_POST_ENCODE_HDR10_PQ\n"
		"	return glxPqEncode(color * glxPaperWhite());\n"
		"#elif GLX_POST_ENCODE_SRGB\n"
		"	return glxLinearToSrgb(color);\n"
		"#else\n"
		"	return color;\n"
		"#endif\n"
		"}\n"
		"vec3 glxApplyGreyscale(vec3 color) {\n"
		"	color = glxNonNegativeVec3(color);\n"
		"	float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));\n"
		"	return glxNonNegativeVec3(mix(color, vec3(luma), glxGreyscaleAmount()));\n"
		"}\n"
		"vec3 glxFinalOutput(vec3 color) {\n"
		"	color = glxNonNegativeVec3(color);\n"
		"#if GLX_POST_GREYSCALE\n"
		"	color = glxApplyGreyscale(color);\n"
		"#endif\n"
		"#if !GLX_POST_OUTPUT_TRANSFORM\n"
		"	return color;\n"
		"#elif GLX_POST_HDR_HEADROOM_OUTPUT\n"
		"	return clamp(color, 0.0, glxHeadroom());\n"
		"#else\n"
		"	return clamp(color, 0.0, 1.0);\n"
		"#endif\n"
		"}\n"
		"vec3 glxOutputDomainClamp(vec3 color) {\n"
		"	color = glxNonNegativeVec3(color);\n"
		"#if GLX_POST_HDR_HEADROOM_OUTPUT\n"
		"	return clamp(color, 0.0, glxHeadroom());\n"
		"#else\n"
		"	return clamp(color, 0.0, 1.0);\n"
		"#endif\n"
		"}\n"
		"vec3 glxResolvePostColor(vec2 uv) {\n"
		"	vec3 color = glxFiniteVec3(GLX_POST_SAMPLE2D(u_Scene, uv).rgb, vec3(0.0));\n"
		"#if GLX_POST_BLOOM_COMBINE\n"
		"	color += glxFiniteVec3(GLX_POST_SAMPLE2D(u_Bloom, uv).rgb, vec3(0.0)) * glxBloomIntensity();\n"
		"#endif\n"
		"#if GLX_POST_OUTPUT_TRANSFORM && GLX_POST_SCENE_LINEAR\n"
		"	color = glxNonNegativeVec3(color * glxExposure() * glxLegacyOverbright());\n"
		"#if GLX_POST_LIFT_GAMMA_GAIN\n"
		"	color = glxApplyLiftGammaGain(color);\n"
		"#endif\n"
		"#if GLX_POST_WHITE_POINT\n"
		"	color = glxApplyWhitePoint(color);\n"
		"#endif\n"
		"#if GLX_POST_LUT_3D\n"
		"	color = glxSampleLutAtlas(color);\n"
		"#endif\n"
		"#if GLX_POST_TONEMAP_REINHARD_SIMPLE\n"
		"	color = glxToneMapReinhardSimple(color);\n"
		"#elif GLX_POST_TONEMAP_ACES_FITTED\n"
		"	color = glxToneMapAcesFitted(color);\n"
		"#endif\n"
		"	color = glxApplyOutputPrimaries(color);\n"
		"	color = glxApplyGamutMap(color);\n"
		"	color = glxEncodeTransfer(color);\n"
		"#elif GLX_POST_OUTPUT_TRANSFORM && GLX_POST_LEGACY_GAMMA\n"
		"	color = pow(glxNonNegativeVec3(color), vec3(glxLegacyGamma())) * glxLegacyOverbright();\n"
		"#else\n"
		"	color = glxNonNegativeVec3(color);\n"
		"#endif\n"
		"	return glxFinalOutput(color);\n"
		"}\n"
		"float glxCrtAmount() { return glxClampFinite(u_CrtParams0.x, 0.0, 0.0, 1.0); }\n"
		"float glxCrtScanlineStrength() { return glxClampFinite(u_CrtParams0.y, 0.55, 0.0, 1.0); }\n"
		"float glxCrtMaskStrength() { return glxClampFinite(u_CrtParams0.z, 0.35, 0.0, 1.0); }\n"
		"float glxCrtCurvature() { return glxClampFinite(u_CrtParams0.w, 0.01, 0.0, 0.25); }\n"
		"float glxCrtChromatic() { return glxClampFinite(u_CrtParams1.x, 1.35, 0.0, 8.0); }\n"
		"float glxCrtTime() { return glxClampFinite(u_CrtParams1.y, 0.0, 0.0, 86400.0); }\n"
		"vec2 glxCrtInvTexSize() { return vec2(glxClampFinite(u_CrtParams1.z, 1.0, 0.000001, 1.0), glxClampFinite(u_CrtParams1.w, 1.0, 0.000001, 1.0)); }\n"
		"vec2 glxCrtWarpUV(vec2 uv) {\n"
		"	vec2 centered = uv * 2.0 - 1.0;\n"
		"	vec2 squared = centered * centered;\n"
		"	centered *= 1.0 + squared.yx * (glxCrtCurvature() * 1.6);\n"
		"	centered.x *= 1.0 + squared.y * (glxCrtCurvature() * 0.25);\n"
		"	centered.y *= 1.0 + squared.x * (glxCrtCurvature() * 0.20);\n"
		"	return centered * 0.5 + 0.5;\n"
		"}\n"
		"float glxCrtScreenMask(vec2 uv) {\n"
		"	vec2 edge = min(uv, 1.0 - uv);\n"
		"	float maskX = smoothstep(0.0, 0.018, edge.x);\n"
		"	float maskY = smoothstep(0.0, 0.018, edge.y);\n"
		"	return maskX * maskY;\n"
		"}\n"
		"vec3 glxCrtSampleHorizontalBeam(vec2 uv) {\n"
		"	vec2 texel = vec2(glxCrtInvTexSize().x, 0.0);\n"
		"	return glxResolvePostColor(clamp(uv - texel * 2.0, 0.0, 1.0)) * 0.08 +\n"
		"		glxResolvePostColor(clamp(uv - texel, 0.0, 1.0)) * 0.22 +\n"
		"		glxResolvePostColor(clamp(uv, 0.0, 1.0)) * 0.40 +\n"
		"		glxResolvePostColor(clamp(uv + texel, 0.0, 1.0)) * 0.22 +\n"
		"		glxResolvePostColor(clamp(uv + texel * 2.0, 0.0, 1.0)) * 0.08;\n"
		"}\n"
		"vec3 glxCrtSampleColor(vec2 uv) {\n"
		"	vec3 beam = glxCrtSampleHorizontalBeam(uv);\n"
		"	vec2 radial = uv - 0.5;\n"
		"	float spread = 1.0 + length(radial) * 2.25;\n"
		"	vec2 offset = vec2(glxCrtChromatic() * glxCrtInvTexSize().x * spread,\n"
		"		glxCrtChromatic() * glxCrtInvTexSize().y * 0.35 * spread);\n"
		"	vec3 chroma = beam;\n"
		"	chroma.r = glxResolvePostColor(clamp(uv + offset, 0.0, 1.0)).r;\n"
		"	chroma.b = glxResolvePostColor(clamp(uv - offset, 0.0, 1.0)).b;\n"
		"	return mix(beam, chroma, clamp(0.35 + glxCrtChromatic() * 0.12, 0.0, 1.0));\n"
		"}\n"
		"float glxCrtScanlineFactor(vec3 color) {\n"
		"	float luma = clamp(dot(color, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);\n"
		"	float phase = gl_FragCoord.y * 3.14159265 + sin(glxCrtTime() * 7.0) * 0.35;\n"
		"	float wave = 0.5 + 0.5 * cos(phase);\n"
		"	wave *= wave;\n"
		"	float darkFloor = 0.22 + luma * 0.35;\n"
		"	float lineValue = mix(darkFloor, 1.0, wave);\n"
		"	return mix(1.0, lineValue, glxCrtScanlineStrength());\n"
		"}\n"
		"vec3 glxCrtPhosphorMask() {\n"
		"	float strength = glxCrtMaskStrength();\n"
		"	float column = mod(floor(gl_FragCoord.x), 3.0);\n"
		"	vec3 triad;\n"
		"	if (column < 0.5) { triad = vec3(1.18, 0.80, 0.80); }\n"
		"	else if (column < 1.5) { triad = vec3(0.80, 1.18, 0.80); }\n"
		"	else { triad = vec3(0.80, 0.80, 1.18); }\n"
		"	float slot = (mod(floor(gl_FragCoord.y), 2.0) < 0.5) ? 1.0 : 0.94;\n"
		"	return mix(vec3(1.0), triad * slot, strength);\n"
		"}\n"
		"vec3 glxApplyCRT(vec2 uv, vec3 originalColor) {\n"
		"	float amount = glxCrtAmount();\n"
		"	vec2 warped = glxCrtWarpUV(uv);\n"
		"	float screen = glxCrtScreenMask(warped);\n"
		"	vec3 crtColor = glxCrtSampleColor(warped);\n"
		"	crtColor *= glxCrtScanlineFactor(crtColor);\n"
		"	crtColor *= glxCrtPhosphorMask();\n"
		"	float edge = dot(warped * 2.0 - 1.0, warped * 2.0 - 1.0);\n"
		"	float vignette = clamp(1.0 - edge * 0.22, 0.0, 1.0);\n"
		"	float shimmer = 0.985 + 0.015 * sin(gl_FragCoord.y * 0.35 + glxCrtTime() * 11.0);\n"
		"	crtColor *= mix(1.0, vignette * shimmer, 0.85);\n"
		"	crtColor *= screen;\n"
		"	return glxOutputDomainClamp(mix(originalColor, crtColor, amount));\n"
		"}\n"
		"void main() {\n"
		"	vec3 color = glxResolvePostColor(v_TexCoord);\n"
		"#if GLX_POST_CRT\n"
		"	color = glxApplyCRT(v_TexCoord, color);\n"
		"#endif\n"
		"	glx_FragColor = vec4(color, 1.0);\n"
		"}\n" ) && ok ) ? qtrue : qfalse;

	if ( bytes ) {
		*bytes = used;
	}
	return ok;
}

static ID_INLINE qboolean GLX_PostShaderSource_WriteFragment(
	const PostShaderPlan &plan, char *out, int outSize, int *bytes )
{
	return GLX_PostShaderSource_WriteFragment( plan, PostShaderSourceTarget::Glsl120,
		out, outSize, bytes );
}

static ID_INLINE PostShaderSourceSummary GLX_PostShaderSource_BuildSummary(
	const PostShaderPlan &plan, PostShaderSourceTarget target )
{
	PostShaderSourceSummary summary {};
	char vertex[GLX_POST_SHADER_VERTEX_SOURCE_BYTES];
	char fragment[GLX_POST_SHADER_FRAGMENT_SOURCE_BYTES];
	qboolean vertexOk;
	qboolean fragmentOk;

	summary.target = target;
	summary.targetVersion = GLX_PostShaderSource_TargetVersion( target );
	vertexOk = GLX_PostShaderSource_WriteVertex( target, vertex, sizeof( vertex ),
		&summary.vertexBytes );
	fragmentOk = GLX_PostShaderSource_WriteFragment( plan, target, fragment,
		sizeof( fragment ), &summary.fragmentBytes );
	summary.valid = ( plan.valid && vertexOk && fragmentOk ) ? qtrue : qfalse;
	summary.truncated = ( plan.valid && ( !vertexOk || !fragmentOk ) ) ? qtrue : qfalse;
	summary.featureMask = plan.featureMask;

	unsigned int hash = 2166136261u;
	hash = GLX_RenderIR_HashValue( hash, static_cast<unsigned int>( GLX_POST_SHADER_SOURCE_VERSION ) );
	hash = GLX_RenderIR_HashValue( hash, static_cast<unsigned int>( target ) );
	hash = GLX_RenderIR_HashValue( hash, static_cast<unsigned int>( summary.targetVersion ) );
	hash = GLX_RenderIR_HashValue( hash, plan.hash );
	hash = GLX_RenderIR_HashValue( hash, plan.featureMask );
	hash = GLX_PostShaderSource_HashText( hash, vertex );
	hash = GLX_PostShaderSource_HashText( hash, fragment );
	summary.sourceHash = hash ? hash : 1u;
	return summary;
}

static ID_INLINE PostShaderSourceSummary GLX_PostShaderSource_BuildSummary(
	const PostShaderPlan &plan )
{
	return GLX_PostShaderSource_BuildSummary( plan, PostShaderSourceTarget::Glsl120 );
}

} // namespace glx

#endif // GLX_POST_SHADER_SOURCE_H
