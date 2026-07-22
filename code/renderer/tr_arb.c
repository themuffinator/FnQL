#include "tr_local.h"
#include "tr_common.h"
#include "tr_glx_compat.h"
#include "../renderercommon/tr_motion_blur.h"

#ifndef GL_RG
#define GL_RG 0x8227
#endif

#ifndef GL_RG16F
#define GL_RG16F 0x822F
#endif

#define COMMON_DEPTH_STENCIL
//#define DEPTH_RENDER_BUFFER
//#define USE_FBO_BLIT

// screenMap texture dimensions
#define SCR_WIDTH 128
#define SCR_HEIGHT 64

#define BLOOM_BASE 5
#define FBO_COUNT (BLOOM_BASE+(MAX_BLUR_PASSES*2))

#if BLOOM_BASE < 2
#error no space for main/postprocess buffers
#endif

static GLuint programs[ PROGRAM_COUNT ];
static GLuint current_vp;
static GLuint current_fp;

static int programCompiled = 0;
static int programEnabled	= 0;
static qboolean globalFogProgramCompiled = qfalse;
static qboolean globalFogCompositorStateLogged = qfalse;
static qboolean globalFogCompositorActiveLogged = qfalse;
static qboolean dlightShadowProgramsCompiled = qfalse;
static qboolean csmShadowProgramsCompiled = qfalse;
static qboolean dlightShadowCasterVPCompiled = qfalse;
#ifdef RENDERER_GLX
typedef void (APIENTRY *PFNGLXCSMUSEPROGRAMPROC)( GLuint program );
static PFNGLXCSMUSEPROGRAMPROC glxCsmUseProgram;
#endif

qboolean fboEnabled = qfalse;
qboolean fboBloomInited = qfalse;
int      fboReadIndex = 0;
GLint    fboInternalFormat;
GLint    fboTextureFormat;
GLint    fboTextureType;
static GLint fboBloomInternalFormat;
static GLint fboBloomTextureFormat;
static GLint fboBloomTextureType;
static int   fboBloomFormatMode;
static qboolean framebufferSrgbEnabled = qfalse;
int      fboBloomPasses;
int      fboBloomBlendBase;
int      fboBloomFilterSize;
static float fboEffectiveTonemapExposure = 1.0f;
static int   fboExposureFrame = -1;
#ifdef RENDERER_GLX
static GLfloat fboExposureSamplePixels[ 128 * 128 * 4 ];
#endif

qboolean windowAdjusted;
int		blitX0, blitX1;
int		blitY0, blitY1;
int		blitClear;
GLenum	blitFilter;

qboolean superSampled;

typedef struct frameBuffer_s {
	GLuint fbo;
	GLuint color;			// renderbuffer if multisampled
	GLuint depthStencil;	// renderbuffer if multisampled
	GLint  width;
	GLint  height;
	qboolean multiSampled;
	GLint  internalFormat;
} frameBuffer_t;

#ifdef USE_FBO
static GLuint commonDepthStencil;
static GLuint depthFadeTexture;
static qboolean depthFadeTextureShared;
static GLuint dlightShadowAtlasTexture;
static GLuint dlightShadowAtlasFbo;
static dlightShadowAtlasLayout_t dlightShadowAtlasLayout;
static GLuint spotShadowAtlasTexture;
static GLuint spotShadowAtlasFbo;
static spotShadowAtlasLayout_t spotShadowAtlasLayout;
static GLuint csmShadowAtlasTexture;
static GLuint csmShadowAtlasFbo;
static csmShadowAtlasLayout_t csmShadowAtlasLayout;
static qboolean csmShadowAtlasCreateFailed;
static qboolean depthFadeCopied;
static qboolean dlightShadowAtlasRendered;
static unsigned int dlightShadowAtlasGeneration;
static qboolean spotShadowAtlasRendered;
static unsigned int spotShadowAtlasGeneration;
static qboolean csmShadowAtlasRendered;
static unsigned int csmShadowAtlasGeneration;

static frameBuffer_t frameBufferMS;
static frameBuffer_t frameBuffers[ FBO_COUNT ];
static frameBuffer_t liquidScreenBuffer;
static frameBuffer_t menuDofBuffers[ 2 ];
static frameBuffer_t motionBlurBuffer;
static motionBlurViewState_t motionBlurViewState;
static int motionBlurFrame = -1;
static qboolean motionBlurCreateFailed = qfalse;

static qboolean frameBufferMultiSampling = qfalse;

qboolean blitMSfbo = qfalse;
#endif

#ifdef USE_FBO
static int FBO_HdrSceneLinearMode( void )
{
	return ( r_hdr && r_hdr->integer > 0 ) ? 1 : 0;
}

static int FBO_HdrSceneLinearColorMode( void )
{
	if ( !FBO_HdrSceneLinearMode() || !r_srgbTextures || !r_srgbTextures->integer ) {
		return 0;
	}
	if ( r_tonemap && r_tonemap->integer > 0 ) {
		return 1;
	}
	if ( r_colorGrade && r_colorGrade->integer > 0 ) {
		return 1;
	}
	if ( r_outputBackend && r_outputBackend->integer > ROUTPUT_REQUEST_SDR_SRGB ) {
		return 1;
	}
	return 0;
}

static int FBO_HdrPrecisionMode( void )
{
	const int precision = r_hdrPrecision ? r_hdrPrecision->integer : 0;

	if ( FBO_HdrSceneLinearMode() ) {
		return 16;
	}
	if ( precision == -1 || precision == 8 || precision == 16 ) {
		return precision;
	}
	if ( r_hdr && r_hdr->integer < 0 ) {
		return -1;
	}
	return 8;
}

static qboolean FBO_InternalFormatIsFloat( GLint internalFormat )
{
	return ( internalFormat == GL_RGBA16F || internalFormat == GL_RGB16F ||
		internalFormat == GL_R11F_G11F_B10F || internalFormat == GL_RG16F ) ? qtrue : qfalse;
}

static int FBO_HdrBloomFormatMode( void )
{
	const int mode = r_hdrBloomFormat ? r_hdrBloomFormat->integer : GLX_HDR_BLOOM_FORMAT_AUTO;

	if ( mode >= GLX_HDR_BLOOM_FORMAT_AUTO && mode <= GLX_HDR_BLOOM_FORMAT_RG16F ) {
		return mode;
	}
	return GLX_HDR_BLOOM_FORMAT_AUTO;
}

static const char *FBO_HdrBloomFormatModeName( int mode )
{
	switch ( mode )
	{
		case GLX_HDR_BLOOM_FORMAT_RGBA16F:
			return "rgba16f";
		case GLX_HDR_BLOOM_FORMAT_R11G11B10F:
			return "r11g11b10f";
		case GLX_HDR_BLOOM_FORMAT_RG16F:
			return "rg16f";
		case GLX_HDR_BLOOM_FORMAT_AUTO:
		default:
			return "auto";
	}
}

static qboolean FBO_FrameBufferIsBloom( const frameBuffer_t *fb )
{
	const int index = (int)( fb - frameBuffers );

	return ( index >= BLOOM_BASE && index < FBO_COUNT ) ? qtrue : qfalse;
}

static void FBO_AddFormatCandidate( GLint *formats, int *count, GLint format )
{
	int i;

	for ( i = 0; i < *count; i++ )
	{
		if ( formats[i] == format ) {
			return;
		}
	}

	formats[*count] = format;
	(*count)++;
}

static void FBO_PositiveIntermediateCandidates( qboolean rgb, GLint *formats, int *count )
{
	const int mode = FBO_HdrBloomFormatMode();

	*count = 0;

	if ( !FBO_HdrSceneLinearMode() ) {
		FBO_AddFormatCandidate( formats, count, GL_RGB10_A2 );
		return;
	}

	if ( mode == GLX_HDR_BLOOM_FORMAT_RGBA16F ) {
		FBO_AddFormatCandidate( formats, count, GL_RGBA16F );
		return;
	}

	if ( rgb ) {
		if ( mode != GLX_HDR_BLOOM_FORMAT_RG16F ) {
			FBO_AddFormatCandidate( formats, count, GL_R11F_G11F_B10F );
		}
	} else {
		if ( mode != GLX_HDR_BLOOM_FORMAT_R11G11B10F ) {
			FBO_AddFormatCandidate( formats, count, GL_RG16F );
		}
	}

	FBO_AddFormatCandidate( formats, count, GL_RGBA16F );
}

static void FBO_FormatCandidatesForBuffer( const frameBuffer_t *fb, GLint *formats, int *count )
{
	if ( FBO_FrameBufferIsBloom( fb ) ) {
		FBO_PositiveIntermediateCandidates( qtrue, formats, count );
		return;
	}

	*count = 0;
	FBO_AddFormatCandidate( formats, count, fboInternalFormat );
}

static GLint FBO_MainInternalFormat( void )
{
	if ( FBO_HdrSceneLinearMode() ) {
		return GL_RGBA16F;
	}

	switch ( FBO_HdrPrecisionMode() )
	{
		case -1:
			return GL_RGBA4;
		case 16:
			return GL_RGBA16;
		default:
			return GL_RGBA8;
	}
}

static int FBO_ToneMapMode( void )
{
	int mode;

	if ( !FBO_HdrSceneLinearColorMode() || !r_tonemap ) {
		return 0;
	}
	mode = r_tonemap->integer;
	if ( mode < 0 ) {
		return 0;
	}
	if ( mode > 2 ) {
		return 2;
	}
	return mode;
}

static float FBO_TonemapExposureCvar( void )
{
	if ( !FBO_HdrSceneLinearColorMode() || !r_tonemapExposure ) {
		return 1.0f;
	}
	return Com_Clamp( 0.1f, 8.0f, r_tonemapExposure->value );
}

static float FBO_TonemapExposure( void )
{
	if ( !FBO_HdrSceneLinearColorMode() ) {
		return 1.0f;
	}
	if ( fboExposureFrame != tr.frameCount ) {
		fboEffectiveTonemapExposure = FBO_TonemapExposureCvar();
	}
	return fboEffectiveTonemapExposure;
}

static int FBO_ColorGradeMode( void )
{
	int mode;

	if ( !FBO_HdrSceneLinearColorMode() || !r_colorGrade ) {
		return 0;
	}
	mode = r_colorGrade->integer;
	if ( mode < 0 ) {
		return 0;
	}
	if ( mode > 3 ) {
		return 3;
	}
	return mode;
}

static qboolean FBO_ColorGradeUsesLiftGammaGain( int mode )
{
	return ( mode == 1 || mode == 3 ) ? qtrue : qfalse;
}

static qboolean FBO_ColorGradeUsesLut( int mode )
{
	return ( mode == 2 || mode == 3 ) ? qtrue : qfalse;
}

static void FBO_ParseVec3Cvar( const cvar_t *cvar, float fallback0, float fallback1,
	float fallback2, float minValue, float maxValue, vec3_t out )
{
	float values[3];

	values[0] = fallback0;
	values[1] = fallback1;
	values[2] = fallback2;
	if ( cvar && cvar->string && cvar->string[0] ) {
		(void)sscanf( cvar->string, "%f %f %f", &values[0], &values[1], &values[2] );
	}
	out[0] = Com_Clamp( minValue, maxValue, values[0] );
	out[1] = Com_Clamp( minValue, maxValue, values[1] );
	out[2] = Com_Clamp( minValue, maxValue, values[2] );
}

static void FBO_SetIdentity3x3( float matrix[9] )
{
	matrix[0] = 1.0f; matrix[1] = 0.0f; matrix[2] = 0.0f;
	matrix[3] = 0.0f; matrix[4] = 1.0f; matrix[5] = 0.0f;
	matrix[6] = 0.0f; matrix[7] = 0.0f; matrix[8] = 1.0f;
}

static void FBO_CctToXyz( float kelvin, float xyz[3] )
{
	float x, y;
	const float t = Com_Clamp( 1667.0f, 25000.0f, kelvin );
	const float t2 = t * t;
	const float t3 = t2 * t;

	if ( t <= 4000.0f ) {
		x = -0.2661239e9f / t3 - 0.2343580e6f / t2 + 0.8776956e3f / t + 0.179910f;
	} else {
		x = -3.0258469e9f / t3 + 2.1070379e6f / t2 + 0.2226347e3f / t + 0.240390f;
	}

	if ( t < 2222.0f ) {
		y = -1.1063814f * x * x * x - 1.34811020f * x * x + 2.18555832f * x - 0.20219683f;
	} else if ( t < 4000.0f ) {
		y = -0.9549476f * x * x * x - 1.37418593f * x * x + 2.09137015f * x - 0.16748867f;
	} else {
		y = 3.0817580f * x * x * x - 5.87338670f * x * x + 3.75112997f * x - 0.37001483f;
	}

	if ( y <= 0.0001f ) {
		xyz[0] = 0.95047f;
		xyz[1] = 1.0f;
		xyz[2] = 1.08883f;
		return;
	}
	xyz[0] = x / y;
	xyz[1] = 1.0f;
	xyz[2] = ( 1.0f - x - y ) / y;
}

static void FBO_BuildBradfordAdaptation( float sourceKelvin, float targetKelvin, float matrix[9] )
{
	static const float bradford[9] = {
		 0.8951f,  0.2664f, -0.1614f,
		-0.7502f,  1.7135f,  0.0367f,
		 0.0389f, -0.0685f,  1.0296f
	};
	static const float bradfordInv[9] = {
		 0.9869929f, -0.1470543f,  0.1599627f,
		 0.4323053f,  0.5183603f,  0.0492912f,
		-0.0085287f,  0.0400428f,  0.9684867f
	};
	float src[3], dst[3], srcCone[3], dstCone[3], scale[3];
	float scaledBradford[9];
	int row, col;

	FBO_CctToXyz( sourceKelvin, src );
	FBO_CctToXyz( targetKelvin, dst );

	for ( row = 0; row < 3; row++ ) {
		srcCone[row] = bradford[row * 3 + 0] * src[0] +
			bradford[row * 3 + 1] * src[1] +
			bradford[row * 3 + 2] * src[2];
		dstCone[row] = bradford[row * 3 + 0] * dst[0] +
			bradford[row * 3 + 1] * dst[1] +
			bradford[row * 3 + 2] * dst[2];
		scale[row] = fabs( srcCone[row] ) > 0.0001f ? dstCone[row] / srcCone[row] : 1.0f;
	}

	for ( row = 0; row < 3; row++ ) {
		for ( col = 0; col < 3; col++ ) {
			scaledBradford[row * 3 + col] = scale[row] * bradford[row * 3 + col];
		}
	}

	for ( row = 0; row < 3; row++ ) {
		for ( col = 0; col < 3; col++ ) {
			matrix[row * 3 + col] =
				bradfordInv[row * 3 + 0] * scaledBradford[0 * 3 + col] +
				bradfordInv[row * 3 + 1] * scaledBradford[1 * 3 + col] +
				bradfordInv[row * 3 + 2] * scaledBradford[2 * 3 + col];
		}
	}
}

static qboolean FBO_ValidateColorGradeLutAtlas( const image_t *image, int *size )
{
	int lutSize;

	if ( !image || image->width <= 0 || image->height <= 0 ) {
		return qfalse;
	}
	if ( image->width != image->height * image->height ) {
		return qfalse;
	}
	lutSize = image->height;
	if ( lutSize < 2 || lutSize > 64 ) {
		return qfalse;
	}
	if ( size ) {
		*size = lutSize;
	}
	return qtrue;
}

static image_t *FBO_CreateIdentityColorGradeLut( int *size )
{
	static image_t *identityLut;
	enum { IDENTITY_LUT_SIZE = 16 };
	byte data[ IDENTITY_LUT_SIZE * IDENTITY_LUT_SIZE * IDENTITY_LUT_SIZE * 4 ];
	int r, g, b;
	const int width = IDENTITY_LUT_SIZE * IDENTITY_LUT_SIZE;

	if ( identityLut ) {
		if ( size ) {
			*size = IDENTITY_LUT_SIZE;
		}
		return identityLut;
	}

	for ( b = 0; b < IDENTITY_LUT_SIZE; b++ ) {
		for ( g = 0; g < IDENTITY_LUT_SIZE; g++ ) {
			for ( r = 0; r < IDENTITY_LUT_SIZE; r++ ) {
				const int index = ( g * width + b * IDENTITY_LUT_SIZE + r ) * 4;
				data[index + 0] = (byte)( r * 255 / ( IDENTITY_LUT_SIZE - 1 ) );
				data[index + 1] = (byte)( g * 255 / ( IDENTITY_LUT_SIZE - 1 ) );
				data[index + 2] = (byte)( b * 255 / ( IDENTITY_LUT_SIZE - 1 ) );
				data[index + 3] = 255;
			}
		}
	}

	identityLut = R_CreateImage( "*colorGradeIdentityLUT", NULL, data,
		width, IDENTITY_LUT_SIZE,
		IMGFLAG_CLAMPTOEDGE | IMGFLAG_NO_COMPRESSION |
		IMGFLAG_NOSCALE | IMGFLAG_COLORSPACE_LINEAR );
	if ( size ) {
		*size = IDENTITY_LUT_SIZE;
	}
	return identityLut;
}

static image_t *FBO_ColorGradeLutImage( int *size )
{
	static image_t *lutImage;
	static int lutSize;
	static int lutModificationCount = -1;
	const int flags = IMGFLAG_CLAMPTOEDGE | IMGFLAG_NO_COMPRESSION |
		IMGFLAG_NOSCALE | IMGFLAG_COLORSPACE_LINEAR;

	if ( !r_colorGradeLUT || r_colorGradeLUT->modificationCount == lutModificationCount ) {
		if ( size ) {
			*size = lutSize;
		}
		return lutImage;
	}

	lutModificationCount = r_colorGradeLUT->modificationCount;
	lutImage = NULL;
	lutSize = 0;

	if ( r_colorGradeLUT->string && r_colorGradeLUT->string[0] ) {
		image_t *loaded = R_FindImageFile( r_colorGradeLUT->string, flags );
		if ( FBO_ValidateColorGradeLutAtlas( loaded, &lutSize ) ) {
			lutImage = loaded;
		} else {
			ri.Printf( PRINT_WARNING,
				"WARNING: color-grade LUT '%s' must use width N*N and height N; using identity LUT\n",
				r_colorGradeLUT->string );
		}
	}

	if ( !lutImage ) {
		lutImage = FBO_CreateIdentityColorGradeLut( &lutSize );
	}

	if ( size ) {
		*size = lutSize;
	}
	return lutImage;
}

static qboolean FBO_ColorGradeLutActive( void )
{
	int size;
	const int mode = FBO_ColorGradeMode();

	if ( !FBO_ColorGradeUsesLut( mode ) || !qglActiveTextureARB || glConfig.numTextureUnits <= 2 ) {
		return qfalse;
	}
	return FBO_ColorGradeLutImage( &size ) && size > 1 ? qtrue : qfalse;
}

static float FBO_ColorGradeLutScale( void )
{
	if ( !r_colorGradeLUTScale ) {
		return 4.0f;
	}
	return Com_Clamp( 1.0f, 32.0f, r_colorGradeLUTScale->value );
}

static void FBO_BindColorGradeLut( void )
{
	int size;
	image_t *lutImage;

	if ( !FBO_ColorGradeLutActive() ) {
		return;
	}

	lutImage = FBO_ColorGradeLutImage( &size );
	if ( lutImage ) {
		GL_BindTexture( 2, lutImage->texnum );
	}
}

#ifdef RENDERER_GLX
static void FBO_PrepareGlxPostShaderColorGradeLut( void )
{
	int lutSize = 16;
	float lutScale = FBO_ColorGradeLutScale();
	const qboolean lutActive = FBO_ColorGradeLutActive();

	if ( lutActive ) {
		(void)FBO_ColorGradeLutImage( &lutSize );
		FBO_BindColorGradeLut();
	} else {
		lutScale = 4.0f;
	}
	GLX_CompatRecordColorGradeLut( lutActive, lutSize, lutScale );
}
#endif

static float FBO_OutputOverbrightScale( float obScale )
{
	return obScale;
}

static void FBO_SetOutputTransformParams( float gamma, float obScale )
{
	const int sceneLinear = FBO_HdrSceneLinearColorMode();
	const float overbrightScale = FBO_OutputOverbrightScale( obScale );
	const float outputScale = sceneLinear ? 1.0f : overbrightScale;
	const float exposure = FBO_TonemapExposure() * ( sceneLinear ? overbrightScale : 1.0f );
	const float srgbOutput = sceneLinear ? 1.0f : 0.0f;
	const int gradeMode = FBO_ColorGradeMode();
	const qboolean lgg = FBO_ColorGradeUsesLiftGammaGain( gradeMode );
	vec3_t lift, gradeGamma, invGradeGamma, gain;
	float whitePointMatrix[9];
	float sourceWhitePoint = r_colorGradeWhitePoint ?
		Com_Clamp( 1000.0f, 40000.0f, r_colorGradeWhitePoint->value ) : 6504.0f;
	float targetWhitePoint = r_colorGradeAdaptWhitePoint ?
		Com_Clamp( 1000.0f, 40000.0f, r_colorGradeAdaptWhitePoint->value ) : 6504.0f;
	int lutSize = 16;
	float lutScale = FBO_ColorGradeLutScale();
	qboolean lutActive;

	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 0, gamma, gamma, gamma, outputScale );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 2, exposure, exposure, exposure, 1.0f );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 3, srgbOutput, srgbOutput, srgbOutput, 1.0f );

	FBO_ParseVec3Cvar( r_colorGradeLift, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, lift );
	FBO_ParseVec3Cvar( r_colorGradeGamma, 1.0f, 1.0f, 1.0f, 0.1f, 8.0f, gradeGamma );
	FBO_ParseVec3Cvar( r_colorGradeGain, 1.0f, 1.0f, 1.0f, 0.0f, 8.0f, gain );

	if ( !lgg ) {
		VectorClear( lift );
		VectorSet( gradeGamma, 1.0f, 1.0f, 1.0f );
		VectorSet( gain, 1.0f, 1.0f, 1.0f );
		sourceWhitePoint = 6504.0f;
		targetWhitePoint = 6504.0f;
	}
	invGradeGamma[0] = 1.0f / gradeGamma[0];
	invGradeGamma[1] = 1.0f / gradeGamma[1];
	invGradeGamma[2] = 1.0f / gradeGamma[2];

	if ( lgg ) {
		FBO_BuildBradfordAdaptation( sourceWhitePoint, targetWhitePoint, whitePointMatrix );
	} else {
		FBO_SetIdentity3x3( whitePointMatrix );
	}
	lutActive = FBO_ColorGradeLutActive();
	if ( !lutActive ) {
		lutScale = 4.0f;
	} else {
		(void)FBO_ColorGradeLutImage( &lutSize );
	}
#ifdef RENDERER_GLX
	GLX_CompatRecordColorGradeLut( lutActive, lutSize, lutScale );
#endif

	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 4, lift[0], lift[1], lift[2], 0.0f );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 5, invGradeGamma[0], invGradeGamma[1], invGradeGamma[2], 1.0f );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 6, gain[0], gain[1], gain[2], 1.0f );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 7, whitePointMatrix[0], whitePointMatrix[1], whitePointMatrix[2], 0.0f );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 8, whitePointMatrix[3], whitePointMatrix[4], whitePointMatrix[5], 0.0f );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 9, whitePointMatrix[6], whitePointMatrix[7], whitePointMatrix[8], 0.0f );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 10, lutScale, (float)( lutSize - 1 ),
		1.0f / (float)lutSize, 1.0f / (float)( lutSize * lutSize ) );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 11, 0.5f, (float)lutSize, 1.0f / lutScale, 0.0f );
}

static void FBO_SetFramebufferSrgb( qboolean enable )
{
	static qboolean initialized = qfalse;

	if ( !framebufferSrgbAvailable ) {
		framebufferSrgbEnabled = qfalse;
		initialized = qtrue;
		return;
	}

	enable = ( enable &&
		r_framebufferSRGB && r_framebufferSRGB->integer &&
		framebufferSrgbAvailable ) ? qtrue : qfalse;

	if ( initialized && framebufferSrgbEnabled == enable ) {
		return;
	}

	if ( enable ) {
		qglEnable( GL_FRAMEBUFFER_SRGB );
	} else {
		qglDisable( GL_FRAMEBUFFER_SRGB );
	}
	framebufferSrgbEnabled = enable;
	initialized = qtrue;
}
#endif

#ifndef GL_TEXTURE_IMAGE_FORMAT
#define GL_TEXTURE_IMAGE_FORMAT 0x828F
#endif

#ifndef GL_TEXTURE_IMAGE_TYPE
#define GL_TEXTURE_IMAGE_TYPE 0x8290
#endif

extern void RB_SetGL2D( void );

#if defined(RENDERER_GLX) && defined(USE_FBO)
static void GLX_RecordFboInitState( qboolean requested, qboolean ready,
	qboolean programReady, qboolean framebufferFnsReady )
{
	GLX_CompatRecordFboInit( requested, ready, programReady, framebufferFnsReady,
		glConfig.vidWidth, glConfig.vidHeight, gls.captureWidth, gls.captureHeight,
		gls.windowWidth, gls.windowHeight, fboInternalFormat, fboTextureFormat,
		fboTextureType, frameBufferMultiSampling, superSampled, windowAdjusted,
		blitFilter, FBO_HdrSceneLinearMode(), r_renderScale ? r_renderScale->integer : 0,
		r_bloom ? r_bloom->integer : 0,
		textureSrgbAvailable, framebufferSrgbAvailable, framebufferSrgbEnabled );
}

static void GLX_RecordBloomCreateState( int result )
{
	GLX_CompatRecordBloomCreate( result,
		r_bloom_passes ? r_bloom_passes->integer : 0, fboBloomPasses,
		glConfig.numTextureUnits, fboBloomFormatMode,
		fboBloomInternalFormat, fboBloomTextureFormat, fboBloomTextureType );
}

static void GLX_RecordBloomState( int result, qboolean finalStage )
{
	GLX_CompatRecordBloom( result, finalStage,
		r_bloom ? r_bloom->integer : 0,
		r_bloom_passes ? r_bloom_passes->integer : 0,
		fboBloomPasses,
		fboBloomBlendBase,
		fboBloomFilterSize,
		glConfig.numTextureUnits,
		r_bloom_threshold_mode ? r_bloom_threshold_mode->integer : 0,
		r_bloom_modulate ? r_bloom_modulate->integer : 0,
		r_bloom_threshold ? r_bloom_threshold->value : 0.0f,
		r_bloom_intensity ? r_bloom_intensity->value : 0.0f,
		r_bloom_reflection ? r_bloom_reflection->value : 0.0f );
}
#endif

qboolean GL_ProgramAvailable( void )
{
	return (programCompiled != 0);
}


static void ARB_ProgramDisable( void )
{
	if ( current_vp )
		qglDisable( GL_VERTEX_PROGRAM_ARB );
	if ( current_fp )
		qglDisable( GL_FRAGMENT_PROGRAM_ARB );
	current_vp = 0;
	current_fp = 0;
	programEnabled = 0;
}


void GL_ProgramDisable( void )
{
	if ( programEnabled )
	{
		ARB_ProgramDisable();
	}
}


void ARB_ProgramEnableExt( GLuint vertexProgram, GLuint fragmentProgram )
{
	if ( programCompiled )
	{
		if ( current_vp != vertexProgram ) {
			current_vp = vertexProgram;
			if ( current_vp ) {
				qglEnable( GL_VERTEX_PROGRAM_ARB );
				qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, current_vp );
			} else {
				qglDisable( GL_VERTEX_PROGRAM_ARB );
			}
		}

		if ( current_fp != fragmentProgram ) {
			current_fp = fragmentProgram;
			if ( current_fp ) {
				qglEnable( GL_FRAGMENT_PROGRAM_ARB );
				qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, current_fp );
			} else {
				qglDisable( GL_FRAGMENT_PROGRAM_ARB );
			}
		}
		programEnabled = 1;
	}
}


static void ARB_ProgramEnable( programNum vp, programNum fp )
{
	ARB_ProgramEnableExt( programs[ vp ], programs[ fp ] );
}


void GL_ProgramEnable( void )
{
	ARB_ProgramEnable( DUMMY_VERTEX, SPRITE_FRAGMENT );
}


#ifdef USE_PMLIGHT
#ifdef RENDERER_GLX
static unsigned int GLX_PMLightMaskForCurrentLight( void )
{
	int i;

	if ( !tess.light ) {
		return 0u;
	}

	for ( i = 0; i < backEnd.refdef.num_dlights && i < MAX_DLIGHTS; i++ ) {
		if ( &backEnd.refdef.dlights[i] == tess.light ) {
			return 1u << i;
		}
	}
	return 0u;
}

static qboolean GLX_TryStreamDrawPMLightPass( int numIndexes, const glIndex_t *indexes )
{
	const shaderCommands_t *input;
	const vec2_t *texCoords;
	glxStreamReservation_t reservation;
	qboolean ok = qtrue;
	int xyzBytes;
	int normalBytes;
	int texBytes;
	int indexBytes;
	int normalOffset;
	int texOffset;
	int indexOffset;
	int totalBytes;
	int materialFlags;
	unsigned int categoryMask;
	unsigned int lightMask;
	unsigned int oldArrayBuffer = 0;
	unsigned int oldElementArrayBuffer = 0;

	if ( !GLX_CompatStreamDrawEnabled() ) {
		return qfalse;
	}
	if ( !qglBindBufferARB ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_NO_BIND_BUFFER );
		return qfalse;
	}

	input = &tess;
	texCoords = (const vec2_t *)input->svars.texcoordPtr[0];
	if ( !indexes || !texCoords ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_BAD_INPUT );
		return qfalse;
	}
	if ( input->numVertexes <= 0 || numIndexes <= 0 ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_EMPTY_BATCH );
		return qfalse;
	}

	materialFlags = GLX_STAGE_DLIGHT_MAP | GLX_STAGE_ST0;
	categoryMask = GLX_CompatDynamicCategoryMaskForTess( input, materialFlags );
	lightMask = GLX_PMLightMaskForCurrentLight();
	if ( !GLX_CompatStreamDrawAllowsMaterial( materialFlags, 0,
		GLX_MATERIAL_RGBGEN_IDENTITY, GLX_MATERIAL_ALPHAGEN_SKIP,
		GLX_MATERIAL_TCGEN_TEXTURE, GLX_MATERIAL_TCGEN_BAD,
		0, 0, 0, 0, 0, 0,
		GLX_MATERIAL_WAVEFUNC_NONE, GLX_MATERIAL_WAVEFUNC_NONE,
		0, 0, GLX_MATERIAL_FOG_ADJUST_NONE, 0, qfalse ) ) {
		GLX_CompatRecordStreamDrawSkip( GLX_STREAM_SKIP_MATERIAL_KEY );
		return qfalse;
	}

	xyzBytes = input->numVertexes * (int)sizeof( input->xyz[0] );
	normalBytes = input->numVertexes * (int)sizeof( input->normal[0] );
	texBytes = input->numVertexes * (int)sizeof( texCoords[0] );
	indexBytes = numIndexes * (int)sizeof( indexes[0] );
	normalOffset = GLX_CompatAlignInt( xyzBytes, 16 );
	texOffset = GLX_CompatAlignInt( normalOffset + normalBytes, 16 );
	indexOffset = GLX_CompatAlignInt( texOffset + texBytes, 16 );
	totalBytes = GLX_CompatAlignInt( indexOffset + indexBytes, 64 );

	if ( !GLX_CompatStreamReserve( totalBytes, 64, &reservation ) ) {
		GLX_CompatRecordStreamDrawResult( input->numVertexes, numIndexes,
			totalBytes, indexBytes, 0, qfalse, qfalse, qfalse, materialFlags, categoryMask, qfalse );
		return qfalse;
	}

	if ( !GLX_CompatStreamUploadAt( &reservation, 0, input->xyz, xyzBytes ) ) {
		ok = qfalse;
	}
	if ( ok && !GLX_CompatStreamUploadAt( &reservation, normalOffset, input->normal, normalBytes ) ) {
		ok = qfalse;
	}
	if ( ok && !GLX_CompatStreamUploadAt( &reservation, texOffset, texCoords, texBytes ) ) {
		ok = qfalse;
	}
	if ( ok && !GLX_CompatStreamUploadAt( &reservation, indexOffset, indexes, indexBytes ) ) {
		ok = qfalse;
	}
	GLX_CompatStreamCommit( &reservation );
	GLX_CompatRecordStreamDlightReservation( &reservation );

	if ( !ok ) {
		GLX_CompatRecordStreamDrawResult( input->numVertexes, numIndexes,
			totalBytes, indexBytes, 0, qfalse, qfalse, qfalse, materialFlags, categoryMask, qfalse );
		return qfalse;
	}

	oldArrayBuffer = GLX_CompatBindStreamArrayBuffer( reservation.buffer );
	oldElementArrayBuffer = GLX_CompatBindStreamElementArrayBuffer( reservation.buffer );

	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_NORMAL_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, sizeof( input->xyz[0] ), (const GLvoid *)(intptr_t)( reservation.offset ) );
	qglNormalPointer( GL_FLOAT, sizeof( input->normal[0] ), (const GLvoid *)(intptr_t)( reservation.offset + normalOffset ) );
	qglTexCoordPointer( 2, GL_FLOAT, 0, (const GLvoid *)(intptr_t)( reservation.offset + texOffset ) );

	if ( !GLX_CompatDrawElementsClassifiedProjectedDlights( GL_TRIANGLES,
		numIndexes, GL_INDEX_TYPE,
		(const GLvoid *)(intptr_t)( reservation.offset + indexOffset ),
		GLX_LEGACY_DELEGATION_NONE, GLX_DRAW_STREAM_GENERIC, materialFlags,
		categoryMask, lightMask ) ) {
		ok = qfalse;
	}

	GLX_CompatRestoreStreamElementArrayBuffer( oldElementArrayBuffer );
	GLX_CompatRestoreStreamArrayBuffer( 0 );
	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_NORMAL_ARRAY );
	qglVertexPointer( 3, GL_FLOAT, sizeof( input->xyz[0] ), input->xyz );
	qglNormalPointer( GL_FLOAT, sizeof( input->normal[0] ), input->normal );
	qglTexCoordPointer( 2, GL_FLOAT, 0, texCoords );
	GLX_CompatRestoreStreamArrayBuffer( oldArrayBuffer );

	GLX_CompatRecordStreamDrawResult( input->numVertexes, numIndexes,
		totalBytes, indexBytes, 0, qfalse, qfalse, qfalse, materialFlags, categoryMask, ok );
	return ok;
}
#endif

static void ARB_DrawLightingElements( int numIndexes, const glIndex_t *indexes )
{
	qboolean glxStreamedDraw = qfalse;

	if ( numIndexes <= 0 ) {
		return;
	}

#ifdef RENDERER_GLX
	glxStreamedDraw = GLX_TryStreamDrawPMLightPass( numIndexes, indexes );
#endif
	if ( glxStreamedDraw ) {
		return;
	}

	if ( qglLockArraysEXT )
		qglLockArraysEXT( 0, tess.numVertexes );

	R_DrawElements( numIndexes, indexes );

	if ( qglUnlockArraysEXT )
		qglUnlockArraysEXT();
}

#ifndef RENDERER_GLX
static void ARB_Lighting( const shaderStage_t* pStage )
{
	const dlight_t* dl;
	byte clipBits[ SHADER_MAX_VERTEXES ];
	glIndex_t hitIndexes[ SHADER_MAX_INDEXES ];
	int numIndexes;
	int clip;
	int i;
	
	backEnd.pc.c_lit_vertices_lateculltest += tess.numVertexes;

	dl = tess.light;

	for ( i = 0; i < tess.numVertexes; ++i ) {
		vec3_t dist;
		VectorSubtract( dl->transformed, tess.xyz[i], dist );

		if ( tess.surfType != SF_GRID && DotProduct( dist, tess.normal[i] ) <= 0.0f ) {
			clipBits[ i ] = 63;
			continue;
		}

		clip = 0;
		if ( dist[0] > dl->radius ) {
			clip |= 1;
		} else if ( dist[0] < -dl->radius ) {
			clip |= 2;
		}
		if ( dist[1] > dl->radius ) {
			clip |= 4;
		} else if ( dist[1] < -dl->radius ) {
			clip |= 8;
		}
		if ( dist[2] > dl->radius ) {
			clip |= 16;
		} else if ( dist[2] < -dl->radius ) {
			clip |= 32;
		}

		clipBits[i] = clip;
	}

	// build a list of triangles that need light
	numIndexes = 0;

	for ( i = 0 ; i < tess.numIndexes ; i += 3 ) {
		int		a, b, c;

		a = tess.indexes[i];
		b = tess.indexes[i+1];
		c = tess.indexes[i+2];
		if ( clipBits[a] & clipBits[b] & clipBits[c] ) {
			continue;	// not lighted
		}
		hitIndexes[numIndexes] = a;
		hitIndexes[numIndexes+1] = b;
		hitIndexes[numIndexes+2] = c;
		numIndexes += 3;
	}

	backEnd.pc.c_lit_indices_latecull_in += numIndexes;
	backEnd.pc.c_lit_indices_latecull_out += tess.numIndexes - numIndexes;

	if ( !numIndexes )
		return;

	if ( tess.shader->sort < SS_OPAQUE ) {
		GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
	} else {
		GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
	}

	GL_SelectTexture( 0 );

	R_BindAnimatedImage( &pStage->bundle[ tess.shader->lightingBundle ] );
	
	ARB_DrawLightingElements( numIndexes, hitIndexes );
}
#endif


static void ARB_Lighting_Fast( const shaderStage_t* pStage )
{
	if ( !tess.numIndexes )
		return;

	GLX_CompatRecordDlightBuild( 0, 0, 0, 0, 0, 0,
		1, tess.numVertexes, tess.numIndexes );

	if ( tess.shader->sort < SS_OPAQUE ) {
		GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
	} else {
		GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
	}

	GL_SelectTexture( 0 );

	R_BindAnimatedImage( &pStage->bundle[ tess.shader->lightingBundle ] );
	
	ARB_DrawLightingElements( tess.numIndexes, tess.indexes );
}


static float ARB_ComputeTextureIntensityScale( const image_t *image )
{
	if ( image == NULL || r_intensity->value <= 1.0f ) {
		return 1.0f;
	}

	if ( image->flags & IMGFLAG_NOLIGHTSCALE ) {
		return 1.0f;
	}

	if ( ( image->flags & IMGFLAG_MIPMAP ) || image->uploadWidth != image->width ||
		image->uploadHeight != image->height ) {
		return r_intensity->value;
	}

	return 1.0f;
}

static qboolean ARB_DlightShadowParams( const dlight_t *dl, const shadowPointLightPlan_t *plan,
	vec4_t shadowAtlas, vec4_t shadowDepth, float *strength )
{
#ifdef USE_FBO
	int atlasWidth;
	int atlasHeight;
	int columns;
	int atlasBaseFace;
	int atlasFaceSize;
	float zNear;
	float zFar;
	float depth;
	float s;

	atlasBaseFace = plan ? plan->atlasBaseFace : ( dl ? dl->shadowAtlasBaseFace : -1 );
	atlasFaceSize = plan ? plan->atlasFaceSize : ( dl ? dl->shadowAtlasFaceSize : 0 );
	if ( ( tr.shadowManager.planned && !plan ) ||
		!dl || dl->linear || ( !tr.shadowManager.planned && !dl->shadowPlanned ) ||
		atlasFaceSize <= 0 || atlasBaseFace < 0 ||
		!dlightShadowProgramsCompiled ||
		( tr.shadowManager.planned ?
			!tr.shadowManager.pointAtlasPublication.published : !FBO_DlightShadowsReady() ) ||
		backEnd.currentEntity != &tr.worldEntity ||
		( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) ||
		backEnd.viewParms.zFar <= 0.0f ) {
		return qfalse;
	}

	if ( tr.shadowManager.planned ) {
		atlasWidth = tr.shadowManager.pointAtlasPublication.width;
		atlasHeight = tr.shadowManager.pointAtlasPublication.height;
	} else {
		atlasWidth = FBO_DlightShadowAtlasWidth();
		atlasHeight = FBO_DlightShadowAtlasHeight();
	}
	if ( atlasWidth <= 0 || atlasHeight <= 0 ) {
		return qfalse;
	}

	columns = atlasWidth / atlasFaceSize;
	if ( columns <= 0 ) {
		return qfalse;
	}

	s = r_dlightShadowStrength ? Com_Clamp( 0.0f, 1.0f, r_dlightShadowStrength->value ) : 0.6f;
	if ( s <= 0.0f ) {
		return qfalse;
	}

	zNear = 1.0f;
	zFar = MAX( dl->radius, 64.0f );
	depth = MAX( zFar - zNear, 1.0f );

	shadowAtlas[0] = (float)atlasFaceSize;
	shadowAtlas[1] = (float)atlasBaseFace;
	shadowAtlas[2] = (float)columns;
	shadowAtlas[3] = (float)atlasHeight;

	shadowDepth[0] = zFar / depth;
	shadowDepth[1] = ( zFar * zNear ) / depth;
	shadowDepth[2] = zNear;
	shadowDepth[3] = zFar;

	*strength = s;
	return qtrue;
#else
	return qfalse;
#endif
}

static void ARB_DlightShadowFilterOffsets( float *inner, float *outer )
{
	int filter = r_dlightShadowFilter ? r_dlightShadowFilter->integer : SHADOW_FILTER_POISSON_4;

	if ( r_shadowCorrectness && r_shadowCorrectness->integer ) {
		filter = SHADOW_FILTER_HARD;
	}
	R_ShadowFilterOffsets( filter, inner, outer );
}

#ifdef RENDERER_GLX
static qboolean GLX_SpotShadowParams( const dlight_t *dl, const shadowSpotLightPlan_t *plan,
	vec4_t dlightShadow, vec4_t shadowAtlas, vec4_t shadowDepth, vec4_t shadowFilter )
{
#ifdef USE_FBO
	int atlasWidth;
	int atlasHeight;
	float zNear;
	float zFar;
	float depth;
	float fov;
	float s;

	if ( glConfig.numTextureUnits <= 2 ||
		!dl || !dl->linear || !plan || !plan->atlasAllocated ||
		plan->atlasTileSize <= 0 || plan->radius <= 0.0f ||
		!r_spotShadows || !r_spotShadows->integer ||
		!tr.shadowManager.planned || !tr.shadowManager.spotAtlasPublication.published ||
		backEnd.currentEntity != &tr.worldEntity ||
		( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) ||
		backEnd.viewParms.zFar <= 0.0f ) {
		return qfalse;
	}

	atlasWidth = tr.shadowManager.spotAtlasPublication.width;
	atlasHeight = tr.shadowManager.spotAtlasPublication.height;
	if ( atlasWidth <= 0 || atlasHeight <= 0 ) {
		return qfalse;
	}

	s = r_dlightShadowStrength ? Com_Clamp( 0.0f, 1.0f, r_dlightShadowStrength->value ) : 0.6f;
	if ( s <= 0.0f ) {
		return qfalse;
	}

	zNear = 1.0f;
	zFar = MAX( plan->radius, 64.0f );
	depth = MAX( zFar - zNear, 1.0f );
	fov = Com_Clamp( 5.0f, 170.0f,
		( plan->outerAngle > 0.0f ? plan->outerAngle : 75.0f ) * 2.0f );

	dlightShadow[0] = 1.0f / (float)atlasWidth;
	dlightShadow[1] = 1.0f / (float)atlasHeight;
	dlightShadow[2] = s;
	dlightShadow[3] = R_ShadowClampReceiverBias( r_dlightShadowBias ? r_dlightShadowBias->value : 4.0f );
	shadowAtlas[0] = (float)plan->atlasX;
	shadowAtlas[1] = (float)plan->atlasY;
	shadowAtlas[2] = (float)plan->atlasTileSize;
	shadowAtlas[3] = (float)atlasHeight;
	shadowDepth[0] = zFar / depth;
	shadowDepth[1] = ( zFar * zNear ) / depth;
	shadowDepth[2] = zNear;
	shadowDepth[3] = tanf( fov * (float)M_PI / 360.0f );
	ARB_DlightShadowFilterOffsets( &shadowFilter[0], &shadowFilter[1] );
	shadowFilter[2] = 1.0f;
	shadowFilter[3] = 0.0f;

	return qtrue;
#else
	return qfalse;
#endif
}

static qboolean GLX_LightingShadowParams( const dlight_t *dl, vec4_t dlightShadow,
	vec4_t shadowAtlas, vec4_t shadowDepth, vec4_t shadowFilter )
{
	// one-entry cache: the plan lookups below are linear scans that run twice
	// per lit batch (program eligibility + program setup) with identical
	// inputs for every batch of the same light. tess.dlightUpdateParams is
	// set whenever the light, fog or cull state changes, which covers every
	// input this function reads (shadow manager and atlas state are constant
	// across a lit pass).
	static const dlight_t *cachedLight;
	static qboolean cachedResult;
	static vec4_t cachedShadow;
	static vec4_t cachedAtlas;
	static vec4_t cachedDepth;
	static vec4_t cachedFilter;
	const shadowPointLightPlan_t *shadowPlan;
	const shadowSpotLightPlan_t *spotPlan;
	float shadowStrength = 0.0f;
	float shadowReceiverBiasScale = 0.0f;
	int atlasWidth;
	int atlasHeight;

	if ( !tess.dlightUpdateParams && dl == cachedLight ) {
		Vector4Copy( cachedShadow, dlightShadow );
		Vector4Copy( cachedAtlas, shadowAtlas );
		Vector4Copy( cachedDepth, shadowDepth );
		Vector4Copy( cachedFilter, shadowFilter );
		return cachedResult;
	}

	dlightShadow[0] = dlightShadow[1] = dlightShadow[2] = dlightShadow[3] = 0.0f;
	shadowAtlas[0] = shadowAtlas[1] = shadowAtlas[2] = shadowAtlas[3] = 0.0f;
	shadowDepth[0] = shadowDepth[1] = shadowDepth[2] = shadowDepth[3] = 0.0f;
	shadowFilter[0] = shadowFilter[1] = shadowFilter[2] = shadowFilter[3] = 0.0f;

	shadowPlan = R_ShadowManagerPointPlanForDlight( &tr.shadowManager, &backEnd.viewParms, dl );
	if ( glConfig.numTextureUnits <= 2 ||
		!ARB_DlightShadowParams( dl, shadowPlan, shadowAtlas, shadowDepth, &shadowStrength ) ) {
		spotPlan = R_ShadowManagerSpotPlanForDlight( &tr.shadowManager, dl );
		cachedResult = GLX_SpotShadowParams( dl, spotPlan, dlightShadow, shadowAtlas,
			shadowDepth, shadowFilter );
	} else {
		if ( shadowAtlas[0] > 0.0f ) {
			shadowReceiverBiasScale =
				R_ShadowClampReceiverBias( r_dlightShadowBias ? r_dlightShadowBias->value : 4.0f ) / shadowAtlas[0];
		}

		if ( tr.shadowManager.planned ) {
			atlasWidth = tr.shadowManager.pointAtlasPublication.width;
			atlasHeight = tr.shadowManager.pointAtlasPublication.height;
		} else {
			atlasWidth = FBO_DlightShadowAtlasWidth();
			atlasHeight = FBO_DlightShadowAtlasHeight();
		}
		dlightShadow[0] = atlasWidth > 0 ? 1.0f / (float)atlasWidth : 0.0f;
		dlightShadow[1] = atlasHeight > 0 ? 1.0f / (float)atlasHeight : 0.0f;
		dlightShadow[2] = shadowStrength;
		dlightShadow[3] = shadowReceiverBiasScale;
		ARB_DlightShadowFilterOffsets( &shadowFilter[0], &shadowFilter[1] );

		cachedResult = dlightShadow[0] > 0.0f && dlightShadow[1] > 0.0f && shadowStrength > 0.0f ? qtrue : qfalse;
	}

	cachedLight = dl;
	Vector4Copy( dlightShadow, cachedShadow );
	Vector4Copy( shadowAtlas, cachedAtlas );
	Vector4Copy( shadowDepth, cachedDepth );
	Vector4Copy( shadowFilter, cachedFilter );
	return cachedResult;
}

static qboolean GLX_LightingLinearVector( const dlight_t *dl, vec4_t lightVector )
{
	vec3_t ab;
	float lengthSq;

	lightVector[0] = lightVector[1] = lightVector[2] = lightVector[3] = 0.0f;
	if ( !dl || !dl->linear ) {
		return qtrue;
	}

	VectorSubtract( dl->transformed2, dl->transformed, ab );
	lengthSq = DotProduct( ab, ab );
	if ( lengthSq <= 0.0001f ) {
		return qfalse;
	}

	lightVector[0] = ab[0];
	lightVector[1] = ab[1];
	lightVector[2] = ab[2];
	lightVector[3] = 1.0f / lengthSq;
	return qtrue;
}

qboolean GLX_LightingProgramEligible( const shader_t *shader, int fogNum )
{
	qboolean linear;
	qboolean absLight;
	qboolean fogPass;
	qboolean shadow;
	vec4_t lightVector;
	vec4_t dlightShadow;
	vec4_t shadowAtlas;
	vec4_t shadowDepth;
	vec4_t shadowFilter;

	if ( !shader || !tess.light || shader->lightingStage < 0 ) {
		return qfalse;
	}
	// the VBO dlight path sources lighting texcoords from per-stage VBO
	// offsets that only exist for TESS_ST* stages; an env-mapped lighting
	// stage must fall back to the CPU path, which computes them per vertex
	if ( shader->stages[ shader->lightingStage ] &&
		!( shader->stages[ shader->lightingStage ]->tessFlags &
			( shader->lightingBundle ? TESS_ST1 : TESS_ST0 ) ) ) {
		return qfalse;
	}
	if ( !GLX_LightingLinearVector( tess.light, lightVector ) ) {
		return qfalse;
	}
	shadow = GLX_LightingShadowParams( tess.light, dlightShadow, shadowAtlas,
		shadowDepth, shadowFilter );

	linear = tess.light->linear ? qtrue : qfalse;
	absLight = shader->cullType == CT_TWO_SIDED ? qtrue : qfalse;
	fogPass = ( fogNum && shader->fogPass ) ? qtrue : qfalse;
	if ( !fogPass ) {
		return GLX_CompatDlightProgramAvailable( linear, 0, absLight, shadow );
	}

	return GLX_CompatDlightProgramAvailable( linear, 1, absLight, shadow ) &&
		GLX_CompatDlightProgramAvailable( linear, 2, absLight, shadow );
}

qboolean GLX_LightingSetupProgram( const shaderStage_t *pStage )
{
	const fogProgramParms_t *fp = NULL;
	const dlight_t *dl;
	qboolean fogPass;
	qboolean linear;
	qboolean absLight;
	qboolean dlightShadowEnabled;
	int fogMode = 0;
	vec3_t lightRGB;
	vec4_t eyePos;
	vec4_t lightPos;
	vec4_t lightColor;
	vec4_t lightVector;
	vec4_t texFactors;
	vec4_t dlightFactors;
	vec4_t dlightShadow;
	vec4_t shadowAtlas;
	vec4_t shadowDepth;
	vec4_t shadowFilter;
	float radius;
	float textureScale;

	tess.dlightUpdateParams = qfalse;
	tess.cullType = tess.shader->cullType;

	if ( !pStage || !tess.light ) {
		return qfalse;
	}

	dl = tess.light;
	if ( !GLX_LightingLinearVector( dl, lightVector ) ) {
		return qfalse;
	}
	dlightShadowEnabled = GLX_LightingShadowParams( dl, dlightShadow, shadowAtlas,
		shadowDepth, shadowFilter );

	if ( !glConfig.deviceSupportsGamma && !fboEnabled )
		VectorScale( dl->color, 2 * powf( r_intensity->value, r_gamma->value ), lightRGB );
	else
		VectorCopy( dl->color, lightRGB );

	radius = MAX( dl->radius, 1.0f );
	linear = dl->linear ? qtrue : qfalse;
	absLight = tess.shader->cullType == CT_TWO_SIDED ? qtrue : qfalse;
	fogPass = ( tess.fogNum && tess.shader->fogPass ) ? qtrue : qfalse;
	if ( fogPass ) {
		fp = RB_CalcFogProgramParms();
		fogMode = fp && fp->eyeOutside ? 1 : 2;
		GL_BindTexture( 1, tr.fogImage->texnum );
		GLX_CompatRecordDlightState( GLX_DLIGHT_STATE_FOG_TEXTURE_BIND );
		GL_SelectTexture( 0 );
	}
	if ( dlightShadowEnabled ) {
#ifdef USE_FBO
		if ( shadowFilter[2] > 0.5f ) {
			FBO_BindSpotShadowTexture( 2 );
		} else {
			FBO_BindDlightShadowTexture( 2 );
		}
		GL_SelectTexture( 0 );
#endif
	} else {
#ifdef USE_FBO
		if ( glConfig.numTextureUnits > 2 && tr.shadowManager.spotAtlasPublication.published &&
			R_ShadowManagerSpotPlanForDlight( &tr.shadowManager, dl ) ) {
			FBO_BindSpotShadowTexture( 2 );
			GL_SelectTexture( 0 );
		}
#endif
	}

	textureScale = ARB_ComputeTextureIntensityScale( pStage->bundle[ tess.shader->lightingBundle ].image[0] );
	eyePos[0] = backEnd.or.viewOrigin[0];
	eyePos[1] = backEnd.or.viewOrigin[1];
	eyePos[2] = backEnd.or.viewOrigin[2];
	eyePos[3] = 0.0f;
	lightPos[0] = dl->transformed[0];
	lightPos[1] = dl->transformed[1];
	lightPos[2] = dl->transformed[2];
	lightPos[3] = 0.0f;
	lightColor[0] = lightRGB[0];
	lightColor[1] = lightRGB[1];
	lightColor[2] = lightRGB[2];
	lightColor[3] = 1.0f / Square( radius );
	if ( r_dlightSpecColor->value > 0.0f ) {
		texFactors[0] = textureScale;
		texFactors[1] = r_dlightSpecPower->value;
		texFactors[2] = 0.0f;
		texFactors[3] = r_dlightSpecColor->value;
	} else {
		texFactors[0] = textureScale;
		texFactors[1] = r_dlightSpecPower->value;
		texFactors[2] = -r_dlightSpecColor->value;
		texFactors[3] = 0.0f;
	}
	dlightFactors[0] = r_dlightFalloff ? r_dlightFalloff->value : 1.0f;
	dlightFactors[1] = 0.0f;
	dlightFactors[2] = 0.0f;
	dlightFactors[3] = 0.0f;

	GL_ProgramDisable();
	if ( !GLX_CompatBindDlightProgram( linear, fogMode, absLight, dlightShadowEnabled,
		eyePos, lightPos, lightColor, lightVector, texFactors, dlightFactors,
		fp ? fp->fogDistanceVector : NULL,
		fp ? fp->fogDepthVector : NULL,
		fp ? fp->eyeT : 0.0f,
		dlightShadowEnabled ? dlightShadow : NULL,
		dlightShadowEnabled ? shadowAtlas : NULL,
		dlightShadowEnabled ? shadowDepth : NULL,
		dlightShadowEnabled ? shadowFilter : NULL ) ) {
		return qfalse;
	}

	return qtrue;
}

void GLX_LightingProgramUnbind( void )
{
	GLX_CompatUnbindDlightProgram();
}
#endif

#ifdef USE_PMLIGHT
/*
Bind the depth-only caster vertex program for the VBO shadow-caster fast
path: world surfaces render straight from the world VBO while the program
applies the same normal bias the CPU path applies per vertex in
RB_ApplyDlightShadowCasterNormalBias().
*/
qboolean ARB_DlightShadowCasterVBOBegin( const vec3_t lightOrigin )
{
	float normalBias;

	if ( !programCompiled || !dlightShadowCasterVPCompiled ) {
		return qfalse;
	}

	normalBias = R_ShadowClampCasterNormalBias( r_dlightShadowCasterNormalBias ?
		r_dlightShadowCasterNormalBias->value : 0.25f );
	if ( normalBias < 0.0f ) {
		normalBias = 0.0f;
	}

	ARB_ProgramEnableExt( programs[ DLIGHT_SHADOW_CASTER_VERTEX ], 0 );
	qglProgramLocalParameter4fARB( GL_VERTEX_PROGRAM_ARB, 0,
		lightOrigin[0], lightOrigin[1], lightOrigin[2], normalBias );
	return qtrue;
}

void ARB_DlightShadowCasterVBOEnd( void )
{
	GL_ProgramDisable();
}
#endif // USE_PMLIGHT

static void ARB_CSMShadowFilterOffsets( float *inner, float *outer )
{
	int filter = r_csmShadowFilter ? r_csmShadowFilter->integer : SHADOW_FILTER_POISSON_4;

	if ( r_shadowCorrectness && r_shadowCorrectness->integer ) {
		filter = SHADOW_FILTER_HARD;
	}
	R_ShadowFilterOffsets( filter, inner, outer );
}

void ARB_CSMShadowPass( void )
{
#ifdef USE_FBO
	const csmCascadePlan_t *cascade;
	vec4_t modelX, modelY, modelZ;
	vec4_t axisX, axisY, axisZ;
	vec4_t invExtents;
	vec4_t splitAtlas;
	vec4_t shadowColor;
	float filterInner, filterOuter;
	float extentX, extentY, extentZ;
	int cascadeIndex;
	int cascadeSize;
	int atlasWidth;
	int atlasHeight;

	if ( !programCompiled ||
		( tr.shadowManager.planned ?
			!tr.shadowManager.csmAtlasPublication.published : !FBO_CSMShadowsReady() ) ||
		!tr.csm.enabled || tess.csmCascade < 0 ||
		tess.csmCascade >= tr.csm.cascadeCount ) {
		return;
	}

	cascadeIndex = tess.csmCascade;
	cascade = &tr.csm.cascades[cascadeIndex];
	if ( tr.shadowManager.planned ) {
		cascadeSize = tr.shadowManager.csmAtlasPublication.tileSize;
		atlasWidth = tr.shadowManager.csmAtlasPublication.width;
		atlasHeight = tr.shadowManager.csmAtlasPublication.height;
	} else {
		cascadeSize = FBO_CSMShadowCascadeSize();
		atlasWidth = FBO_CSMShadowAtlasWidth();
		atlasHeight = FBO_CSMShadowAtlasHeight();
	}
	if ( cascadeSize <= 0 || atlasWidth <= 0 || atlasHeight <= 0 ) {
		return;
	}

	extentX = MAX( cascade->bounds[1][0] - cascade->bounds[0][0], 1.0f );
	extentY = MAX( cascade->bounds[1][1] - cascade->bounds[0][1], 1.0f );
	extentZ = MAX( cascade->bounds[1][2] - cascade->bounds[0][2], 1.0f );

	modelX[0] = backEnd.or.axis[0][0];
	modelX[1] = backEnd.or.axis[1][0];
	modelX[2] = backEnd.or.axis[2][0];
	modelX[3] = backEnd.or.origin[0];
	modelY[0] = backEnd.or.axis[0][1];
	modelY[1] = backEnd.or.axis[1][1];
	modelY[2] = backEnd.or.axis[2][1];
	modelY[3] = backEnd.or.origin[1];
	modelZ[0] = backEnd.or.axis[0][2];
	modelZ[1] = backEnd.or.axis[1][2];
	modelZ[2] = backEnd.or.axis[2][2];
	modelZ[3] = backEnd.or.origin[2];

	VectorCopy( cascade->axis[0], axisX );
	axisX[3] = cascade->bounds[0][0];
	VectorCopy( cascade->axis[1], axisY );
	axisY[3] = cascade->bounds[0][1];
	VectorCopy( cascade->axis[2], axisZ );
	axisZ[3] = cascade->bounds[0][2];

	invExtents[0] = 1.0f / extentX;
	invExtents[1] = 1.0f / extentY;
	invExtents[2] = 1.0f / extentZ;
	invExtents[3] = R_ShadowClampReceiverBias( tr.csm.receiverBias ) / extentX;

	splitAtlas[0] = cascade->splitNear;
	splitAtlas[1] = cascade->splitFar;
	splitAtlas[2] = (float)( cascadeIndex * cascadeSize ) / (float)atlasWidth;
	splitAtlas[3] = (float)cascadeSize / (float)atlasWidth;

	shadowColor[0] = tr.csm.lightColor[0] * tr.csm.shadowStrength;
	shadowColor[1] = tr.csm.lightColor[1] * tr.csm.shadowStrength;
	shadowColor[2] = tr.csm.lightColor[2] * tr.csm.shadowStrength;
	shadowColor[3] = 0.0f;

	ARB_CSMShadowFilterOffsets( &filterInner, &filterOuter );

#ifdef RENDERER_GLX
	GLX_CompatUnbindMaterial();
	if ( !glxCsmUseProgram && ri.GL_GetProcAddress ) {
		glxCsmUseProgram = (PFNGLXCSMUSEPROGRAMPROC)ri.GL_GetProcAddress( "glUseProgram" );
	}
	if ( glxCsmUseProgram ) {
		glxCsmUseProgram( 0 );
	}
#endif
	ARB_ProgramEnable( CSM_SHADOW_VERTEX, CSM_SHADOW_FRAGMENT );
	qglProgramLocalParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 0, modelX );
	qglProgramLocalParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 1, modelY );
	qglProgramLocalParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 2, modelZ );
	qglProgramLocalParameter4fARB( GL_VERTEX_PROGRAM_ARB, 3,
		backEnd.viewParms.or.axis[0][0], backEnd.viewParms.or.axis[0][1],
		backEnd.viewParms.or.axis[0][2],
		-DotProduct( backEnd.viewParms.or.origin, backEnd.viewParms.or.axis[0] ) );

	qglProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, axisX );
	qglProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 1, axisY );
	qglProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 2, axisZ );
	qglProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 3, invExtents );
	qglProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 4, splitAtlas );
	qglProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 5, shadowColor );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 6,
		1.0f / (float)atlasWidth, 1.0f / (float)atlasHeight, filterInner, filterOuter );

	FBO_BindCSMShadowTexture( 1 );
	qglDisable( GL_TEXTURE_2D );
	GL_SelectTexture( 0 );
	qglDisable( GL_TEXTURE_2D );

	GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_EQUAL );
	GL_Cull( CT_TWO_SIDED );
	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_NONE );
	qglVertexPointer( 3, GL_FLOAT, sizeof( tess.xyz[0] ), tess.xyz );
	R_DrawElements( tess.numIndexes, tess.indexes );
	GL_SelectTexture( 1 );
	qglDisable( GL_TEXTURE_2D );
	GL_SelectTexture( 0 );
	qglEnable( GL_TEXTURE_2D );
#endif
}

void ARB_SetupLightParams( const shaderStage_t *pStage )
{
	programNum vertexProgram;
	programNum fragmentProgram;
	const fogProgramParms_t *fp;
	qboolean fogPass;
	qboolean dlightShadow;
	const dlight_t *dl;
	vec3_t lightRGB;
	vec4_t shadowAtlas = { 0.0f, 0.0f, 0.0f, 0.0f };
	vec4_t shadowDepth = { 0.0f, 0.0f, 0.0f, 0.0f };
	const shadowPointLightPlan_t *shadowPlan;
	float radius;
	float textureScale;
	float shadowFilterInner = 0.0f;
	float shadowFilterOuter = 0.0f;
	float shadowReceiverBiasScale = 0.0f;
	float shadowStrength = 0.0f;
	int atlasWidth;
	int atlasHeight;

	tess.dlightUpdateParams = qfalse;
	tess.cullType = tess.shader->cullType;

	if ( !programCompiled || !pStage )
		return;

	dl = tess.light;

	if ( !glConfig.deviceSupportsGamma && !fboEnabled )
		VectorScale( dl->color, 2 * powf( r_intensity->value, r_gamma->value ), lightRGB );
	else
		VectorCopy( dl->color, lightRGB );

	radius = dl->radius;

	fogPass = ( tess.fogNum && tess.shader->fogPass );
	fp = NULL;

	vertexProgram = DLIGHT_VERTEX;
	shadowPlan = R_ShadowManagerPointPlanForDlight( &tr.shadowManager, &backEnd.viewParms, dl );
	dlightShadow = ARB_DlightShadowParams( dl, shadowPlan, shadowAtlas, shadowDepth,
		&shadowStrength );
	if ( dlightShadow && shadowAtlas[0] > 0.0f ) {
		shadowReceiverBiasScale =
			R_ShadowClampReceiverBias( r_dlightShadowBias ? r_dlightShadowBias->value : 4.0f ) / shadowAtlas[0];
	}
	if ( tr.shadowManager.planned ) {
		atlasWidth = tr.shadowManager.pointAtlasPublication.width;
		atlasHeight = tr.shadowManager.pointAtlasPublication.height;
	} else {
		atlasWidth = FBO_DlightShadowAtlasWidth();
		atlasHeight = FBO_DlightShadowAtlasHeight();
	}

	if ( dlightShadow && dl->linear ) {
		fragmentProgram = (tess.shader->cullType == CT_TWO_SIDED) ? DLIGHT_SHADOW_LINEAR_ABS_FRAGMENT : DLIGHT_SHADOW_LINEAR_FRAGMENT;
	} else if ( dlightShadow ) {
		fragmentProgram = (tess.shader->cullType == CT_TWO_SIDED) ? DLIGHT_SHADOW_ABS_FRAGMENT : DLIGHT_SHADOW_FRAGMENT;
	} else if ( dl->linear ) {
		fragmentProgram = (tess.shader->cullType == CT_TWO_SIDED) ? DLIGHT_LINEAR_ABS_FRAGMENT : DLIGHT_LINEAR_FRAGMENT;
	} else {
		fragmentProgram = (tess.shader->cullType == CT_TWO_SIDED) ? DLIGHT_ABS_FRAGMENT : DLIGHT_FRAGMENT;
	}

	if ( fogPass ) {
		fp = RB_CalcFogProgramParms();
		// switch to fog programs
		if ( fp->eyeOutside ) {
			vertexProgram += 2;
		} else {
			vertexProgram += 1;
		}
		++fragmentProgram;
	}

	ARB_ProgramEnable( vertexProgram, fragmentProgram );

	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 0, lightRGB[0], lightRGB[1], lightRGB[2], 1.0f / Square( radius ) );
	textureScale = ARB_ComputeTextureIntensityScale( pStage->bundle[ tess.shader->lightingBundle ].image[0] );
	if ( r_dlightSpecColor->value > 0.0f ) {
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 5,
			textureScale, r_dlightSpecPower->value, 0.0f, r_dlightSpecColor->value );
	} else {
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 5,
			textureScale, r_dlightSpecPower->value, -r_dlightSpecColor->value, 0.0f );
	}
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 6,
		r_dlightFalloff ? r_dlightFalloff->value : 1.0f, 0.0f, 0.0f, 0.0f );

	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 7,
		atlasWidth > 0 ? 1.0f / (float)atlasWidth : 0.0f,
		atlasHeight > 0 ? 1.0f / (float)atlasHeight : 0.0f,
		dlightShadow ? shadowStrength : 0.0f,
		shadowReceiverBiasScale );
	qglProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 8, shadowAtlas );
	qglProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 9, shadowDepth );
	if ( dlightShadow ) {
		ARB_DlightShadowFilterOffsets( &shadowFilterInner, &shadowFilterOuter );
	}
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 10,
		shadowFilterInner, shadowFilterOuter, 0.0f, 0.0f );

	if ( dlightShadow ) {
#ifdef USE_FBO
		FBO_BindDlightShadowTexture( 2 );
		GL_SelectTexture( 0 );
#endif
	} else {
#ifdef USE_FBO
		if ( glConfig.numTextureUnits > 2 && tr.shadowManager.spotAtlasPublication.published &&
			R_ShadowManagerSpotPlanForDlight( &tr.shadowManager, dl ) ) {
			FBO_BindSpotShadowTexture( 2 );
			GL_SelectTexture( 0 );
		}
#endif
	}

	if ( dl->linear )
	{
		vec3_t ab;
		VectorSubtract( dl->transformed2, dl->transformed, ab );
		//qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 2, dl->transformed[0], dl->transformed[1], dl->transformed[2], 0 );
		//qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 3, dl->transformed2[0], dl->transformed2[1], dl->transformed2[2], 0 );
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 4, ab[0], ab[1], ab[2], 1.0f / DotProduct( ab, ab ) );
	}

	qglProgramLocalParameter4fARB( GL_VERTEX_PROGRAM_ARB, 0, backEnd.or.viewOrigin[0], backEnd.or.viewOrigin[1], backEnd.or.viewOrigin[2], 0 );
	qglProgramLocalParameter4fARB( GL_VERTEX_PROGRAM_ARB, 1, dl->transformed[0], dl->transformed[1], dl->transformed[2], 0 );

	if ( fogPass )
	{
		GL_BindTexture( 1, tr.fogImage->texnum );
		//qglProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 5, fp->fogColor );
		qglProgramLocalParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 2, fp->fogDistanceVector );
		qglProgramLocalParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 3, fp->fogDepthVector );
		qglProgramLocalParameter4fARB( GL_VERTEX_PROGRAM_ARB, 4, fp->eyeT, 0.0f, 0.0f, 0.0f );
		GL_SelectTexture( 0 );
	}
}


void ARB_LightingPass( void )
{
	const shaderStage_t* pStage;
#ifdef RENDERER_GLX
	qboolean glxLightingProgram = qfalse;
#endif

	if ( tess.shader->lightingStage < 0 )
		return;

	pStage = tess.xstages[ tess.shader->lightingStage ];
	// Keep parity with VK_LightingPass: fog, light, and texture scale are
	// resolved per lit batch because fogNum and the selected surface texture
	// can change without an entity/light transform change.
#ifdef RENDERER_GLX
	glxLightingProgram = GLX_LightingSetupProgram( pStage );
	if ( !glxLightingProgram )
#endif
		ARB_SetupLightParams( pStage );

	RB_DeformTessGeometry();

	GL_Cull( tess.shader->cullType );

	// set polygon offset if necessary
	if ( tess.shader->polygonOffset )
	{
		qglEnable( GL_POLYGON_OFFSET_FILL );
		qglPolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
	}

	R_ComputeTexCoords( 0, &pStage->bundle[ tess.shader->lightingBundle ] );

	GL_ClientState( 1, CLS_NONE );
	GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_NORMAL_ARRAY );

	// Since this is a single pass, prepare the array state once; the draw helper
	// locks client arrays only when it falls back from the GLx stream path.

	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoordPtr[0] );
	qglNormalPointer( GL_FLOAT, sizeof( tess.normal[0] ), tess.normal );
	qglVertexPointer( 3, GL_FLOAT, sizeof( tess.xyz[0] ), tess.xyz );

#ifdef RENDERER_GLX
	ARB_Lighting_Fast( pStage );
#else
	// CPU may limit performance in following cases
	if ( tess.light->linear || gl_version >= 40 )
		ARB_Lighting_Fast( pStage );
	else
		ARB_Lighting( pStage );
#endif

	// reset polygon offset
	if ( tess.shader->polygonOffset ) 
	{
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}
#ifdef RENDERER_GLX
	if ( glxLightingProgram ) {
		GLX_LightingProgramUnbind();
	}
#endif
}
#endif // USE_PMLIGHT


const char *fogOutVPCode = {
	"PARAM fogDistanceVector = program.local[2]; \n"
	"PARAM fogDepthVector = program.local[3]; \n"
	"PARAM eyeT = program.local[4]; \n"
	"PARAM _01_32 = 0.03125; \n"
	"PARAM _30_32 = 0.93750; \n"
	"TEMP st; \n"
	
	// s = DotProduct( v, fogDistanceVector ) + fogDistanceVector[3];
	"DP3 st.x, fogDistanceVector, vertex.position; \n"	
	"ADD st.x, st.x, fogDistanceVector.w; \n"

	// t = DotProduct( v, fogDepthVector ) + fogDepthVector[3];
	"DP3 st.y, fogDepthVector, vertex.position; \n"	
	"ADD st.y, st.y, fogDepthVector.w; \n"

	// if ( t < 1.0 ) { t = 1.0/32; } else { t = 1.0/32 + 30.0/32 * t / ( t - eyeT ); }
	"SGE st.w, st.y, 1.0; \n"
	"SUB st.z, st.y, eyeT.x; \n"
	"RCP st.z, st.z; \n"
	"MUL st.z, st.z, st.y; \n"
	"MUL st.z, st.z, _30_32; \n"
	"MAD st.y, st.z, st.w, _01_32; \n"
	
	//"MOV st.z, {1.0}; \n"
	"MOV st.w, {1.0}; \n"

	"MOV result.texcoord[4], st; \n"
};


const char *fogInVPCode = {

	"PARAM fogDistanceVector = program.local[2]; \n"
	"PARAM fogDepthVector = program.local[3]; \n"
	"PARAM eyeT = program.local[4]; \n"
	"PARAM _01_32 = 0.03125; \n"
	"PARAM _30_32 = 0.93750; \n"
	"TEMP st; \n"
	
	// s = DotProduct( v, fogDistanceVector ) + fogDistanceVector[3];
	"DP3 st.x, fogDistanceVector, vertex.position; \n"	
	"ADD st.x, st.x, fogDistanceVector.w; \n"

	// t = DotProduct( v, fogDepthVector ) + fogDepthVector[3];
	"DP3 st.y, fogDepthVector, vertex.position; \n"
	"ADD st.y, st.y, fogDepthVector.w; \n"

	//if ( t < 0 ) { t = 1.0/32; } else { t = 31.0/32; }
	"SGE st.w, st.y, 0.0; \n"
	"MAD st.y, st.w, _30_32, _01_32; \n"

	//"MOV st.z, {1.0}; \n"
	"MOV st.w, {1.0}; \n"

	"MOV result.texcoord[4], st; \n"
};


#ifdef USE_PMLIGHT
static const char *dlightVP = {
	"!!ARBvp1.0 \n"
	"OPTION ARB_position_invariant; \n"
	"PARAM posEye = program.local[0]; \n"
	"PARAM posLight = program.local[1]; \n"
	"OUTPUT lv = result.texcoord[1]; \n" // 1
	"OUTPUT ev = result.texcoord[2]; \n" // 2
	"OUTPUT n = result.texcoord[3]; \n"  // 3
	"MOV result.texcoord[0], vertex.texcoord; \n" // 0
	"SUB lv, posLight, vertex.position; \n"
	"SUB ev, posEye, vertex.position; \n"
	"MOV n, vertex.normal; \n"
	"%s" // fog shader if needed
	"END \n"
};

/*
Depth-only caster program for the VBO shadow-caster fast path. Reproduces
RB_ApplyDlightShadowCasterNormalBias() for point/spot lights on the GPU:
displace each vertex along its unit normal by
sign(dot(n,L)) * normalBias * (0.75 - 0.5 * min(|dot(n,L)|, 1))
where L is the light-to-vertex direction. local[0].xyz = light origin in
model space (world casters only, so model == world), local[0].w = bias.
*/
static const char *dlightShadowCasterVP = {
	"!!ARBvp1.0 \n"
	"PARAM mvp[4] = { state.matrix.mvp }; \n"
	"PARAM lightOrigin = program.local[0]; \n"
	"PARAM consts = { 0.75, 0.5, 1.0, 0.000001 }; \n"
	"TEMP n; \n"
	"TEMP L; \n"
	"TEMP d; \n"
	"TEMP pos; \n"
	"DP3 n.w, vertex.normal, vertex.normal; \n"
	"MAX n.w, n.w, consts.w; \n"
	"RSQ n.w, n.w; \n"
	"MUL n.xyz, vertex.normal, n.w; \n"
	"SUB L.xyz, vertex.position, lightOrigin; \n"
	"DP3 L.w, L, L; \n"
	"MAX L.w, L.w, consts.w; \n"
	"RSQ L.w, L.w; \n"
	"MUL L.xyz, L, L.w; \n"
	"DP3 d.x, n, L; \n"
	"ABS d.y, d.x; \n"
	"MIN d.y, d.y, consts.z; \n"
	"MAD d.z, -d.y, consts.y, consts.x; \n"
	"MUL d.z, d.z, lightOrigin.w; \n"
	"SLT d.w, d.x, 0.0; \n"
	"MAD d.w, d.w, -2.0, 1.0; \n"
	"MUL d.z, d.z, d.w; \n"
	"MAD pos.xyz, n, d.z, vertex.position; \n"
	"MOV pos.w, 1.0; \n"
	"DP4 result.position.x, mvp[0], pos; \n"
	"DP4 result.position.y, mvp[1], pos; \n"
	"DP4 result.position.z, mvp[2], pos; \n"
	"DP4 result.position.w, mvp[3], pos; \n"
	"END \n"
};

static const char *csmShadowVP = {
	"!!ARBvp1.0 \n"
	"OPTION ARB_position_invariant; \n"
	"PARAM modelX = program.local[0]; \n"
	"PARAM modelY = program.local[1]; \n"
	"PARAM modelZ = program.local[2]; \n"
	"PARAM viewForward = program.local[3]; \n"
	"TEMP localPos; \n"
	"TEMP worldPos; \n"
	"OUTPUT world = result.texcoord[0]; \n"
	"OUTPUT viewDist = result.texcoord[1]; \n"
	"MOV localPos, vertex.position; \n"
	"MOV localPos.w, 1.0; \n"
	"DP4 worldPos.x, modelX, localPos; \n"
	"DP4 worldPos.y, modelY, localPos; \n"
	"DP4 worldPos.z, modelZ, localPos; \n"
	"MOV worldPos.w, 1.0; \n"
	"MOV world, worldPos; \n"
	"DP4 viewDist.x, viewForward, worldPos; \n"
	"END \n"
};

static const char *csmShadowFP = {
	"!!ARBfp1.0 \n"
	"PARAM axisX = program.local[0]; \n"
	"PARAM axisY = program.local[1]; \n"
	"PARAM axisZ = program.local[2]; \n"
	"PARAM invExtents = program.local[3]; \n"
	"PARAM splitAtlas = program.local[4]; \n"
	"PARAM shadowColor = program.local[5]; \n"
	"PARAM atlas = program.local[6]; \n"
	"PARAM one = { 1.0, 1.0, 1.0, 1.0 }; \n"
	"PARAM zero = { 0.0, 0.0, 0.0, 0.0 }; \n"
	"PARAM half = { 0.5, 0.5, 0.5, 0.5 }; \n"
	"PARAM off0 = { -1.0, -1.0, 0.0, 0.0 }; \n"
	"PARAM off1 = { 1.0, -1.0, 0.0, 0.0 }; \n"
	"PARAM off2 = { -1.0, 1.0, 0.0, 0.0 }; \n"
	"PARAM off3 = { 1.0, 1.0, 0.0, 0.0 }; \n"
	"TEMP world; \n"
	"TEMP lightCoord; \n"
	"TEMP uv; \n"
	"TEMP mask; \n"
	"TEMP kill; \n"
	"TEMP receiver; \n"
	"TEMP sample; \n"
	"TEMP occ; \n"
	"TEMP offset; \n"
	"TEMP factor; \n"
	"MOV world, fragment.texcoord[0]; \n"
	"DP3 lightCoord.x, world, axisX; \n"
	"SUB lightCoord.x, lightCoord.x, axisX.w; \n"
	"MUL lightCoord.x, lightCoord.x, invExtents.x; \n"
	"DP3 lightCoord.y, world, axisY; \n"
	"SUB lightCoord.y, lightCoord.y, axisY.w; \n"
	"MUL lightCoord.y, lightCoord.y, invExtents.y; \n"
	"DP3 lightCoord.z, world, axisZ; \n"
	"SUB lightCoord.z, lightCoord.z, axisZ.w; \n"
	"MUL lightCoord.z, lightCoord.z, invExtents.z; \n"
	"SGE mask.x, lightCoord.y, zero.x; \n"
	"SGE mask.y, one.x, lightCoord.y; \n"
	"MUL mask.x, mask.x, mask.y; \n"
	"SGE mask.y, lightCoord.z, zero.x; \n"
	"MUL mask.x, mask.x, mask.y; \n"
	"SGE mask.y, one.x, lightCoord.z; \n"
	"MUL mask.x, mask.x, mask.y; \n"
	"SGE mask.y, lightCoord.x, zero.x; \n"
	"MUL mask.x, mask.x, mask.y; \n"
	"SGE mask.y, one.x, lightCoord.x; \n"
	"MUL mask.x, mask.x, mask.y; \n"
	"SGE mask.y, fragment.texcoord[1].x, splitAtlas.x; \n"
	"MUL mask.x, mask.x, mask.y; \n"
	"SGE mask.y, splitAtlas.y, fragment.texcoord[1].x; \n"
	"MUL mask.x, mask.x, mask.y; \n"
	"SUB kill.x, mask.x, half.x; \n"
	"KIL kill.x; \n"
	"MAD uv.x, lightCoord.y, splitAtlas.w, splitAtlas.z; \n"
	"MOV uv.y, lightCoord.z; \n"
	"SUB receiver.x, lightCoord.x, invExtents.w; \n"
	"MOV offset, off0; \n"
	"MUL offset.x, offset.x, atlas.w; \n"
	"MUL offset.y, offset.y, atlas.z; \n"
	"MUL offset.x, offset.x, atlas.x; \n"
	"MUL offset.y, offset.y, atlas.y; \n"
	"ADD offset.x, offset.x, uv.x; \n"
	"ADD offset.y, offset.y, uv.y; \n"
	"TEX sample, offset, texture[1], 2D; \n"
	"SLT occ.x, sample.x, receiver.x; \n"
	"MOV offset, off1; \n"
	"MUL offset.x, offset.x, atlas.z; \n"
	"MUL offset.y, offset.y, atlas.w; \n"
	"MUL offset.x, offset.x, atlas.x; \n"
	"MUL offset.y, offset.y, atlas.y; \n"
	"ADD offset.x, offset.x, uv.x; \n"
	"ADD offset.y, offset.y, uv.y; \n"
	"TEX sample, offset, texture[1], 2D; \n"
	"SLT occ.y, sample.x, receiver.x; \n"
	"MOV offset, off2; \n"
	"MUL offset.x, offset.x, atlas.z; \n"
	"MUL offset.y, offset.y, atlas.w; \n"
	"MUL offset.x, offset.x, atlas.x; \n"
	"MUL offset.y, offset.y, atlas.y; \n"
	"ADD offset.x, offset.x, uv.x; \n"
	"ADD offset.y, offset.y, uv.y; \n"
	"TEX sample, offset, texture[1], 2D; \n"
	"SLT occ.z, sample.x, receiver.x; \n"
	"MOV offset, off3; \n"
	"MUL offset.x, offset.x, atlas.w; \n"
	"MUL offset.y, offset.y, atlas.z; \n"
	"MUL offset.x, offset.x, atlas.x; \n"
	"MUL offset.y, offset.y, atlas.y; \n"
	"ADD offset.x, offset.x, uv.x; \n"
	"ADD offset.y, offset.y, uv.y; \n"
	"TEX sample, offset, texture[1], 2D; \n"
	"SLT occ.w, sample.x, receiver.x; \n"
	"DP4 occ.x, occ, { 0.25, 0.25, 0.25, 0.25 }; \n"
	"MAD factor.xyz, -occ.x, shadowColor, one; \n"
	"MOV factor.w, one.x; \n"
	"MOV result.color, factor; \n"
	"END \n"
};


static const char *ARB_BuildDlightFP( char *program, int programIndex )
{
	qboolean fog = qfalse;
	qboolean linear = qfalse;
	qboolean abslight = qfalse;
	qboolean shadow = qfalse;

	program[0] = '\0';

	switch ( programIndex ) {
		case DLIGHT_FRAGMENT_FOG:
		case DLIGHT_ABS_FRAGMENT_FOG:
		case DLIGHT_LINEAR_FRAGMENT_FOG:
		case DLIGHT_LINEAR_ABS_FRAGMENT_FOG:
		case DLIGHT_SHADOW_FRAGMENT_FOG:
		case DLIGHT_SHADOW_ABS_FRAGMENT_FOG:
		case DLIGHT_SHADOW_LINEAR_FRAGMENT_FOG:
		case DLIGHT_SHADOW_LINEAR_ABS_FRAGMENT_FOG:
			fog = qtrue;
			break;
	}

	switch ( programIndex ) {
		case DLIGHT_LINEAR_FRAGMENT:
		case DLIGHT_LINEAR_FRAGMENT_FOG:
		case DLIGHT_LINEAR_ABS_FRAGMENT:
		case DLIGHT_LINEAR_ABS_FRAGMENT_FOG:
		case DLIGHT_SHADOW_LINEAR_FRAGMENT:
		case DLIGHT_SHADOW_LINEAR_FRAGMENT_FOG:
		case DLIGHT_SHADOW_LINEAR_ABS_FRAGMENT:
		case DLIGHT_SHADOW_LINEAR_ABS_FRAGMENT_FOG:
			linear = qtrue;
			break;
	}

	switch ( programIndex ) {
		case DLIGHT_ABS_FRAGMENT:
		case DLIGHT_ABS_FRAGMENT_FOG:
		case DLIGHT_LINEAR_ABS_FRAGMENT:
		case DLIGHT_LINEAR_ABS_FRAGMENT_FOG:
		case DLIGHT_SHADOW_ABS_FRAGMENT:
		case DLIGHT_SHADOW_ABS_FRAGMENT_FOG:
		case DLIGHT_SHADOW_LINEAR_ABS_FRAGMENT:
		case DLIGHT_SHADOW_LINEAR_ABS_FRAGMENT_FOG:
			abslight = qtrue;
			break;
	}

	shadow = ( programIndex >= DLIGHT_SHADOW_FRAGMENT && programIndex <= DLIGHT_SHADOW_LINEAR_ABS_FRAGMENT_FOG &&
		glConfig.numTextureUnits > 2 ) ? qtrue : qfalse;

	strcat( program,
	"!!ARBfp1.0 \n"
	"OPTION ARB_precision_hint_fastest; \n"
	"PARAM lightRGB = program.local[0]; \n"
	"PARAM texFactors = program.local[5]; \n"
	"PARAM dlightFactors = program.local[6]; \n"
	"PARAM attenShape = { 3.0, -2.0, 0.0, 0.0 }; \n"
	//"PARAM lightRange2recip = program.local[1]; \n"
	"TEMP base, tmp; \n"
	"TEX base, fragment.texcoord[0], texture[0], 2D; \n"
	"MUL base.xyz, base, texFactors.x; \n" );

	if ( linear ) {
		strcat( program,
		"PARAM lightVector = program.local[4]; \n"
		"ATTRIB LV = fragment.texcoord[1]; \n"
		"TEMP dnLV; \n"
		// project fragment on light vector
		"DP3 tmp.w, -LV, lightVector; \n"
		"MUL_SAT tmp.x, tmp.w, lightVector.w; \n"
		// calculate light vector from projection point
		"MAD dnLV, lightVector, tmp.x, LV; \n"
		);
	} else {
		strcat( program, "ATTRIB dnLV = fragment.texcoord[1]; \n" );
	}

	strcat( program,
	"ATTRIB dnEV = fragment.texcoord[2]; \n" // 2
	"ATTRIB n = fragment.texcoord[3]; \n"    // 3

	// normalize interpolated normal
	"TEMP nn; \n"
	"DP3 tmp.w, n, n; \n"
	"RSQ tmp.w, tmp.w; \n"
	"MUL nn.xyz, n, tmp.w; \n"
	
	// normalize light vector
	"TEMP lv; \n"
	"DP3 tmp.w, dnLV, dnLV; \n"
	"RSQ lv.w, tmp.w; \n"
	"MUL lv.xyz, dnLV, lv.w; \n"

	// calculate light intensity
	"TEMP light; \n"
	"MUL tmp.x, tmp.w, lightRGB.w; \n"
	"SUB tmp.x, {1.0}, tmp.x; \n"
	// discard blank fragments
	"KIL tmp.x; \n"
	"MAD tmp.y, tmp.x, attenShape.y, attenShape.x; \n"
	"MUL tmp.y, tmp.y, tmp.x; \n"
	"MUL tmp.y, tmp.y, tmp.x; \n"
	"LRP tmp.x, dlightFactors.x, tmp.y, tmp.x; \n"

	"MUL light, lightRGB, tmp.x; \n" ); // light.rgb

	if ( shadow ) {
		strcat( program,
		"PARAM dlightShadow = program.local[7]; \n"
		"PARAM shadowAtlas = program.local[8]; \n"
		"PARAM shadowDepth = program.local[9]; \n"
		"PARAM shadowFilter = program.local[10]; \n"
		"PARAM one = { 1.0, 1.0, 1.0, 1.0 }; \n"
		"PARAM half = { 0.5, 0.5, 0.5, 0.5 }; \n"
		"PARAM two = { 2.0, 2.0, 2.0, 2.0 }; \n"
		"PARAM faceConst = { 3.0, 5.0, 0.0001, 0.0 }; \n"
		"PARAM eps = { 0.00001, 0.0, 0.0, 0.0 }; \n"
		"TEMP shadowVec, absVec, masks, signMask, faceInfo, local, tile, depthTap, occ, shadowFactor; \n"
		"MOV shadowVec.xyz, -dnLV; \n"
		"ABS absVec.xyz, shadowVec; \n"
		"MAX faceInfo.x, absVec.x, absVec.y; \n"
		"MAX faceInfo.x, faceInfo.x, absVec.z; \n"
		"MAX faceInfo.x, faceInfo.x, faceConst.z; \n"
		"DP3 local.x, nn, lv; \n"
		"ABS local.x, local.x; \n"
		"MAD local.x, local.x, -half.x, one.x; \n"
		"MUL local.x, local.x, half.x; \n"
		"MUL local.x, local.x, dlightShadow.w; \n"
		"MUL local.x, local.x, faceInfo.x; \n"
		"ADD faceInfo.y, faceInfo.x, -local.x; \n"
		"MAX faceInfo.y, faceInfo.y, shadowDepth.z; \n"
		"RCP faceInfo.z, faceInfo.x; \n"
		"SGE masks.x, absVec.x, absVec.y; \n"
		"SGE masks.y, absVec.x, absVec.z; \n"
		"MUL masks.x, masks.x, masks.y; \n"
		"SGE masks.y, absVec.y, absVec.z; \n"
		"SUB masks.z, one.x, masks.x; \n"
		"MUL masks.y, masks.z, masks.y; \n"
		"SUB masks.z, masks.z, masks.y; \n"
		"SGE signMask.xyz, shadowVec, eps.y; \n"
		"MAD faceInfo.w, signMask.x, -one.x, one.x; \n"
		"MAD local.z, signMask.y, -one.x, faceConst.x; \n"
		"MAD local.w, signMask.z, -one.x, faceConst.y; \n"
		"MUL faceInfo.w, faceInfo.w, masks.x; \n"
		"MAD faceInfo.w, local.z, masks.y, faceInfo.w; \n"
		"MAD faceInfo.w, local.w, masks.z, faceInfo.w; \n"
		"MAD signMask.w, signMask.x, -two.x, one.x; \n"
		"MUL local.x, shadowVec.y, signMask.w; \n"
		"MAD local.x, local.x, faceInfo.z, one.x; \n"
		"MUL local.x, local.x, half.x; \n"
		"MAD signMask.w, signMask.y, two.x, -one.x; \n"
		"MUL local.y, shadowVec.x, signMask.w; \n"
		"MAD local.y, local.y, faceInfo.z, one.x; \n"
		"MUL local.y, local.y, half.x; \n"
		"MUL local.z, shadowVec.y, -faceInfo.z; \n"
		"ADD local.z, local.z, one.x; \n"
		"MUL local.z, local.z, half.x; \n"
		"MUL tile.x, local.x, masks.x; \n"
		"MAD tile.x, local.y, masks.y, tile.x; \n"
		"MAD tile.x, local.z, masks.z, tile.x; \n"
		"MAD local.x, shadowVec.z, faceInfo.z, one.x; \n"
		"MUL local.x, local.x, half.x; \n"
		"MAD signMask.w, signMask.z, -two.x, one.x; \n"
		"MUL local.y, shadowVec.x, signMask.w; \n"
		"MAD local.y, local.y, faceInfo.z, one.x; \n"
		"MUL local.y, local.y, half.x; \n"
		"ADD masks.w, masks.x, masks.y; \n"
		"MUL tile.y, local.x, masks.w; \n"
		"MAD tile.y, local.y, masks.z, tile.y; \n"
		"RCP local.z, shadowAtlas.x; \n"
		"SUB local.w, one.x, local.z; \n"
		"MAX tile.x, tile.x, local.z; \n"
		"MAX tile.y, tile.y, local.z; \n"
		"MIN tile.x, tile.x, local.w; \n"
		"MIN tile.y, tile.y, local.w; \n"
		"ADD faceInfo.w, faceInfo.w, shadowAtlas.y; \n"
		"RCP local.z, shadowAtlas.z; \n"
		"MUL local.w, faceInfo.w, local.z; \n"
		"FLR tile.z, local.w; \n"
		"MUL local.z, tile.z, shadowAtlas.z; \n"
		"SUB tile.w, faceInfo.w, local.z; \n"
		"MUL local.x, tile.w, shadowAtlas.x; \n"
		"ADD local.y, tile.z, one.x; \n"
		"MUL local.y, local.y, shadowAtlas.x; \n"
		"SUB local.y, shadowAtlas.w, local.y; \n" );
		strcat( program,
		"MAD tile.x, tile.x, shadowAtlas.x, local.x; \n"
		"MAD tile.y, tile.y, shadowAtlas.x, local.y; \n"
		"MUL tile.x, tile.x, dlightShadow.x; \n"
		"MUL tile.y, tile.y, dlightShadow.y; \n"
		"RCP occ.y, faceInfo.y; \n"
		"MUL occ.y, occ.y, shadowDepth.y; \n"
		"SUB occ.y, shadowDepth.x, occ.y; \n"
		"MUL local.x, dlightShadow.x, shadowFilter.x; \n"
		"MUL local.y, dlightShadow.y, shadowFilter.x; \n"
		"MUL masks.z, dlightShadow.x, shadowFilter.y; \n"
		"MUL masks.w, dlightShadow.y, shadowFilter.y; \n"
		"MOV occ.x, eps.y; \n"
		"SUB local.z, tile.x, masks.z; \n"
		"SUB local.w, tile.y, local.y; \n"
		"TEX depthTap, local.zwzw, texture[2], 2D; \n"
		"SLT occ.z, depthTap.x, occ.y; \n"
		"ADD occ.x, occ.x, occ.z; \n"
		"ADD local.z, tile.x, local.x; \n"
		"SUB local.w, tile.y, masks.w; \n"
		"TEX depthTap, local.zwzw, texture[2], 2D; \n"
		"SLT occ.z, depthTap.x, occ.y; \n"
		"ADD occ.x, occ.x, occ.z; \n"
		"SUB local.z, tile.x, local.x; \n"
		"ADD local.w, tile.y, masks.w; \n"
		"TEX depthTap, local.zwzw, texture[2], 2D; \n"
		"SLT occ.z, depthTap.x, occ.y; \n"
		"ADD occ.x, occ.x, occ.z; \n"
		"ADD local.z, tile.x, masks.z; \n"
		"ADD local.w, tile.y, local.y; \n"
		"TEX depthTap, local.zwzw, texture[2], 2D; \n"
		"SLT occ.z, depthTap.x, occ.y; \n"
		"ADD occ.x, occ.x, occ.z; \n"
		"MUL occ.x, occ.x, half.x; \n"
		"MUL occ.x, occ.x, half.x; \n"
		"MUL occ.x, occ.x, dlightShadow.z; \n"
		"SUB shadowFactor.x, one.x, occ.x; \n"
		"MUL light, light, shadowFactor.x; \n" );
	}

	strcat( program,
	// normalize eye vector
	"TEMP ev; \n"
	"DP3 ev.w, dnEV, dnEV; \n"
	"RSQ ev.w, ev.w; \n"
	"MUL ev.xyz, dnEV, ev.w; \n"

	// normalize (eye + light) vector
	"ADD tmp, lv, ev; \n"
	"DP3 tmp.w, tmp, tmp; \n"
	"RSQ tmp.w, tmp.w; \n"
	"MUL tmp.xyz, tmp, tmp.w; \n" );

	// modulate specular strength
	if ( abslight ) {
		strcat( program,
		"DP3 tmp.w, nn, tmp; \n"
		"ABS tmp.w, tmp.w; \n" );
	} else {
		strcat( program,
		"DP3_SAT tmp.w, nn, tmp; \n" );
	}

	strcat( program,
	"POW tmp.w, tmp.w, texFactors.y; \n"
	"TEMP spec; \n"
	"MUL spec, base, texFactors.z; \n"
	"ADD spec, spec, texFactors.w; \n"
	"MUL spec, spec, tmp.w; \n" );

	// diffuse
	if ( abslight ) {
		strcat( program,
		"TEMP bump; \n"
		"DP3 bump.w, nn, lv; \n"
		// make sure that light and eye vectors are on the same plane side
		"DP3 tmp.w, nn, ev; \n"
		"MUL tmp.w, tmp.w, bump.w; \n"
		"KIL tmp.w; \n"
		"ABS bump.w, bump.w; \n" );
	} else {
		strcat( program,
		"TEMP bump; \n"
		"DP3_SAT bump.w, nn, lv; \n" );
	}

	strcat( program, "MAD base, base, bump.w, spec; \n" );

	if ( fog ) {
		strcat( program,
		"TEMP fog; \n"
		"TEX fog, fragment.texcoord[4], texture[1], 2D; \n" // fog texture
		//"MUL fog, fog, fogColor; \n"
		// blend with fog
		//"LRP_SAT base, fog.a, fog, base; \n"
		// modulate by inverted fog alpha
		"SUB fog.a, {1.0}, fog.a; \n"
		"MUL base, base, fog.a; \n" );
	}

	strcat( program,
	"MUL result.color, base, light; \n"
	"END \n" );
	
	return program;
}

#endif // USE_PMLIGHT


static const char *dummyVP = {
	"!!ARBvp1.0 \n"
	"OPTION ARB_position_invariant; \n"
	"MOV result.color, vertex.color; \n"
	"MOV result.texcoord[0], vertex.texcoord; \n"
	"END \n" 
};


static const char *spriteFP = {
	"!!ARBfp1.0 \n"
	"OPTION ARB_precision_hint_fastest; \n"
	"TEMP base; \n"
	"TEX base, fragment.texcoord[0], texture[0], 2D; \n"
	"TEMP test; \n"
	"SUB test.a, base.a, 0.85; \n"
	"KIL test.a; \n"
	"MOV base, 0.0; \n"
	"MOV result.color, base; \n"
	"MOV result.depth, fragment.position.z; \n"
	"END \n"
};

static const char *depthFadeFP = {
	"!!ARBfp1.0 \n"
	"OPTION ARB_precision_hint_fastest; \n"
	"PARAM invTexRes = program.local[0]; \n"
	"PARAM fadeInfo = program.local[1]; \n"
	"PARAM fadeScale = program.local[2]; \n"
	"PARAM fadeBias = program.local[3]; \n"
	"PARAM one = { 1.0, 1.0, 1.0, 1.0 }; \n"
	"PARAM smooth = { -2.0, 3.0, 0.0, 0.0 }; \n"
	"TEMP base, faded, depthTC, sceneDepth, denom, sceneLinear, fragLinear, fade; \n"
	"TEX base, fragment.texcoord[0], texture[0], 2D; \n"
	"MUL base, base, fragment.color; \n"
	"MUL depthTC.xy, fragment.position, invTexRes; \n"
	"TEX sceneDepth, depthTC, texture[1], 2D; \n"
	"LRP denom.x, sceneDepth.x, one.x, fadeInfo.x; \n"
	"RCP sceneLinear.x, denom.x; \n"
	"MUL sceneLinear.x, sceneLinear.x, fadeInfo.y; \n"
	"LRP denom.x, fragment.position.z, one.x, fadeInfo.x; \n"
	"RCP fragLinear.x, denom.x; \n"
	"MUL fragLinear.x, fragLinear.x, fadeInfo.y; \n"
	"SUB fade.x, sceneLinear.x, fragLinear.x; \n"
	"ADD fade.x, fade.x, fadeInfo.w; \n"
	"MUL_SAT fade.x, fade.x, fadeInfo.z; \n"
	"MUL fade.y, fade.x, fade.x; \n"
	"MAD fade.z, smooth.x, fade.x, smooth.y; \n"
	"MUL fade.x, fade.y, fade.z; \n"
	"MUL faded, base, fadeScale; \n"
	"ADD faded, faded, fadeBias; \n"
	"LRP result.color, fade.x, base, faded; \n"
	"END \n"
};

#ifdef USE_FBO
static qboolean liquidArbProgramsCompiled = qfalse;

/* Forwards model-space position and normal to the fragment stage while
 * keeping the fixed-function position transform, so liquid overlay depth
 * matches the authored stages exactly. texcoord[0] carries the per-vertex
 * ripple pixel offset supplied by the backend. */
static const char *liquidVP = {
	"!!ARBvp1.0 \n"
	"OPTION ARB_position_invariant; \n"
	"MOV result.texcoord[0], vertex.texcoord[0]; \n"
	"MOV result.texcoord[1], vertex.position; \n"
	"MOV result.texcoord[2], vertex.normal; \n"
	"END \n"
};

/* Warped scene-color refraction underlay. The ambient wave gradient model and
 * its constants are shared with the GLx GLSL and Vulkan implementations; see
 * renderercommon/tr_liquid.h before changing any number here. */
static const char *liquidRefractionFP = {
	"!!ARBfp1.0 \n"
	"OPTION ARB_precision_hint_fastest; \n"
	"PARAM screen = program.local[0]; \n" /* 1/vpW, 1/vpH, vpX, vpY */
	"PARAM waveK1 = program.local[1]; \n" /* wave vector xyz, w = speed * wrapped time */
	"PARAM waveK2 = program.local[2]; \n"
	"PARAM waveK3 = program.local[3]; \n"
	"PARAM waveDir12 = program.local[4]; \n" /* octave 1 dir*amp in xy, octave 2 in zw */
	"PARAM waveDir3 = program.local[5]; \n"
	"PARAM tune = program.local[6]; \n" /* warp pixels, alpha, distance ref, min atten */
	"PARAM eyePos = program.local[7]; \n" /* model-space eye xyz, w = depth reject toggle */
	"PARAM edge = { 16.666667, 3.0, -2.0, 1.0 }; \n"
	"PARAM bounds = { 0.002, 0.998, 4.0, 0.00003 }; \n"
	"TEMP uv, baseUv, phase, wave, grad, atten, px, e, s, n, v, base, depth; \n"
	"SUB uv.xy, fragment.position, screen.zwzw; \n"
	"MUL uv.xy, uv, screen; \n"
	"MOV baseUv, uv; \n"
	"DPH phase.x, fragment.texcoord[1], waveK1; \n"
	"DPH phase.y, fragment.texcoord[1], waveK2; \n"
	"DPH phase.z, fragment.texcoord[1], waveK3; \n"
	"COS wave.x, phase.x; \n"
	"COS wave.y, phase.y; \n"
	"COS wave.z, phase.z; \n"
	"MUL grad.xy, waveDir12, wave.x; \n"
	"MAD grad.xy, waveDir12.zwzw, wave.y, grad; \n"
	"MAD grad.xy, waveDir3, wave.z, grad; \n"
	/* distance attenuation, then the grazing-angle fade that keeps the
	 * compressed wave field near the horizon from aliasing */
	"MUL atten.x, fragment.position.w, tune.z; \n"
	"MIN atten.x, atten.x, edge.w; \n"
	"MAX atten.x, atten.x, tune.w; \n"
	"DP3 n.w, fragment.texcoord[2], fragment.texcoord[2]; \n"
	"RSQ n.w, n.w; \n"
	"MUL n.xyz, fragment.texcoord[2], n.w; \n"
	"SUB v.xyz, eyePos, fragment.texcoord[1]; \n"
	"DP3 v.w, v, v; \n"
	"RSQ v.w, v.w; \n"
	"MUL v.xyz, v, v.w; \n"
	"DP3 s.y, n, v; \n"
	"ABS s.y, s.y; \n"
	"MUL_SAT s.y, s.y, bounds.z; \n"
	"MUL atten.x, atten.x, s.y; \n"
	"MUL atten.x, atten.x, tune.x; \n"
	"MUL px.xy, grad, atten.x; \n"
	"ADD px.xy, px, fragment.texcoord[0]; \n"
	"SUB e.xy, edge.w, uv; \n"
	"MIN e.xy, e, uv; \n"
	"MIN e.x, e.x, e.y; \n"
	"MUL_SAT e.x, e.x, edge.x; \n"
	"MAD s.x, e.x, edge.z, edge.y; \n"
	"MUL e.y, e.x, e.x; \n"
	"MUL e.x, e.y, s.x; \n"
	"MUL px.xy, px, e.x; \n"
	"MAD uv.xy, px, screen, uv; \n"
	"MAX uv.xy, uv, bounds.x; \n"
	"MIN uv.xy, uv, bounds.y; \n"
	/* opaque scene depth at the warped coordinate: samples nearer than this
	 * fragment are foreground, keep the unwarped coordinate there */
	"TEX depth, uv, texture[1], 2D; \n"
	"SUB depth.y, depth.x, fragment.position.z; \n"
	"ADD depth.y, depth.y, bounds.w; \n"
	"MUL depth.y, depth.y, eyePos.w; \n"
	"CMP uv.xy, depth.y, baseUv, uv; \n"
	"TEX base, uv, texture[0], 2D; \n"
	"MOV base.w, tune.y; \n"
	"MOV result.color, base; \n"
	"END \n"
};

/* Post-authored Fresnel pass: bounded single-tap screen-space reflection of
 * the immutable pre-transparency snapshot, falling back to the material sheen
 * color wherever the mirrored sample is invalid or off screen. */
static const char *liquidSheenFP = {
	"!!ARBfp1.0 \n"
	"OPTION ARB_precision_hint_fastest; \n"
	"PARAM waveK1 = program.local[1]; \n"
	"PARAM waveK2 = program.local[2]; \n"
	"PARAM waveK3 = program.local[3]; \n"
	"PARAM waveDir12 = program.local[4]; \n"
	"PARAM waveDir3 = program.local[5]; \n"
	"PARAM tune = program.local[6]; \n"     /* perturb scale, alpha scale, reflectivity */
	"PARAM eyePos = program.local[7]; \n"   /* model-space eye xyz, w = proxy view scale */
	"PARAM mvpRow0 = program.local[8]; \n"
	"PARAM mvpRow1 = program.local[9]; \n"
	"PARAM mvpRow3 = program.local[10]; \n"
	"PARAM sheen = program.local[11]; \n"   /* material sheen rgb, w = proxy base distance */
	"PARAM edge = { 12.5, 3.0, -2.0, 1.0 }; \n"
	"PARAM bounds = { 0.002, 0.998, 16.0, 0.5 }; \n"
	"PARAM alphaCurve = { 0.04, 0.56, 2.0, 0.0 }; \n"
	"TEMP n, v, len, phase, wave, grad, r, p, clip, uvr, e, s, valid, refl; \n"
	"DP3 n.w, fragment.texcoord[2], fragment.texcoord[2]; \n"
	"RSQ n.w, n.w; \n"
	"MUL n.xyz, fragment.texcoord[2], n.w; \n"
	"SUB v.xyz, eyePos, fragment.texcoord[1]; \n"
	"DP3 len.x, v, v; \n"
	"RSQ len.y, len.x; \n"
	"MUL v.xyz, v, len.y; \n"
	"RCP len.z, len.y; \n"
	"DPH phase.x, fragment.texcoord[1], waveK1; \n"
	"DPH phase.y, fragment.texcoord[1], waveK2; \n"
	"DPH phase.z, fragment.texcoord[1], waveK3; \n"
	"COS wave.x, phase.x; \n"
	"COS wave.y, phase.y; \n"
	"COS wave.z, phase.z; \n"
	"MUL grad.xy, waveDir12, wave.x; \n"
	"MAD grad.xy, waveDir12.zwzw, wave.y, grad; \n"
	"MAD grad.xy, waveDir3, wave.z, grad; \n"
	"MOV grad.zw, alphaCurve.w; \n"
	"MAD n.xyz, grad, tune.x, n; \n"
	"DP3 n.w, n, n; \n"
	"RSQ n.w, n.w; \n"
	"MUL n.xyz, n, n.w; \n"
	"DP3 len.w, n, v; \n"
	"ABS s.x, len.w; \n"
	"SUB_SAT s.y, edge.w, s.x; \n"
	"MUL s.z, s.y, s.y; \n"
	"MUL r.xyz, n, len.w; \n"
	"MAD r.xyz, r, alphaCurve.z, -v; \n"
	"MAD len.w, len.z, eyePos.w, sheen.w; \n"
	"MAD p.xyz, r, len.w, fragment.texcoord[1]; \n"
	"MOV p.w, edge.w; \n"
	"DP4 clip.x, mvpRow0, p; \n"
	"DP4 clip.y, mvpRow1, p; \n"
	"DP4 clip.w, mvpRow3, p; \n"
	"RCP len.y, clip.w; \n"
	"MUL uvr.xy, clip, len.y; \n"
	"MAD uvr.xy, uvr, bounds.w, bounds.w; \n"
	"SGE valid.x, clip.w, bounds.z; \n"
	"SUB e.xy, edge.w, uvr; \n"
	"MIN e.xy, e, uvr; \n"
	"MIN e.x, e.x, e.y; \n"
	"MUL_SAT e.x, e.x, edge.x; \n"
	"MAD s.w, e.x, edge.z, edge.y; \n"
	"MUL e.y, e.x, e.x; \n"
	"MUL e.x, e.y, s.w; \n"
	"MUL valid.x, valid.x, e.x; \n"
	"MUL valid.x, valid.x, tune.z; \n"
	"MAX uvr.xy, uvr, bounds.x; \n"
	"MIN uvr.xy, uvr, bounds.y; \n"
	"TEX refl, uvr, texture[0], 2D; \n"
	"LRP refl.xyz, valid.x, refl, sheen; \n"
	"MAD_SAT s.x, s.z, alphaCurve.y, alphaCurve.x; \n"
	"MUL refl.w, s.x, tune.y; \n"
	"MOV result.color, refl; \n"
	"END \n"
};

static const char *worldCelOutlineFP = {
	"!!ARBfp1.0 \n"
	"OPTION ARB_precision_hint_fastest; \n"
	"PARAM outline = program.local[0]; \n"
	"PARAM zero = { 0.0, 0.0, 0.0, 0.0 }; \n"
	"PARAM negTwo = { -2.0, -2.0, -2.0, -2.0 }; \n"
	"TEMP tc, center, left, right, up, down, edge; \n"
	"TEX center, fragment.texcoord[0], texture[0], 2D; \n"
	"ADD tc.x, fragment.texcoord[0].x, outline.x; \n"
	"MOV tc.y, fragment.texcoord[0].y; \n"
	"TEX right, tc, texture[0], 2D; \n"
	"SUB tc.x, fragment.texcoord[0].x, outline.x; \n"
	"TEX left, tc, texture[0], 2D; \n"
	"MOV tc.x, fragment.texcoord[0].x; \n"
	"ADD tc.y, fragment.texcoord[0].y, outline.y; \n"
	"TEX up, tc, texture[0], 2D; \n"
	"SUB tc.y, fragment.texcoord[0].y, outline.y; \n"
	"TEX down, tc, texture[0], 2D; \n"
	"ADD edge.x, left.x, right.x; \n"
	"MAD edge.x, center.x, negTwo.x, edge.x; \n"
	"ABS edge.x, edge.x; \n"
	"ADD edge.y, up.x, down.x; \n"
	"MAD edge.y, center.x, negTwo.x, edge.y; \n"
	"ABS edge.y, edge.y; \n"
	"MAX edge.x, edge.x, edge.y; \n"
	"SGE edge.x, edge.x, outline.z; \n"
	"MUL edge.x, edge.x, outline.w; \n"
	"MOV result.color, zero; \n"
	"MOV result.color.w, edge.x; \n"
	"END \n"
};

/* Screen-space global fog is applied to the resolved scene color using the
 * copied depth image.  It deliberately sits outside BSP fog assignment so
 * authored brush fog remains a separate compatibility-preserving layer. */
static const char *globalFogFP = {
	"!!ARBfp1.0 \n"
	"OPTION ARB_precision_hint_fastest; \n"
	"PARAM fogColor = program.local[0]; \n"
	"PARAM fogParams = program.local[1]; \n"
	"PARAM depthParams = program.local[2]; \n"
	"PARAM one = { 1.0, 1.0, 1.0, 1.0 }; \n"
	"PARAM zero = { 0.0, 0.0, 0.0, 0.0 }; \n"
	"PARAM epsilon = { 0.0001, 0.0001, 0.0001, 0.0001 }; \n"
	"PARAM expScale = { -1.442695, -1.442695, -1.442695, -1.442695 }; \n"
	"TEMP base, depth, denom, distance, exponential, linear, modeMask; \n"
	"TEX base, fragment.texcoord[0], texture[0], 2D; \n"
	"TEX depth, fragment.texcoord[0], texture[1], 2D; \n"
	"MAD denom.x, -depth.x, depthParams.z, depthParams.y; \n"
	"RCP denom.x, denom.x; \n"
	"MUL distance.x, depthParams.x, depthParams.y; \n"
	"MUL distance.x, distance.x, denom.x; \n"
	"SUB distance.x, distance.x, fogParams.x; \n"
	"MAX distance.x, distance.x, zero.x; \n"
	"MUL distance.y, distance.x, fogParams.z; \n"
	"MUL exponential.x, distance.y, expScale.x; \n"
	"EX2 exponential.x, exponential.x; \n"
	"SUB exponential.x, one.x, exponential.x; \n"
	"MUL exponential.y, distance.y, distance.y; \n"
	"MUL exponential.y, exponential.y, expScale.x; \n"
	"EX2 exponential.y, exponential.y; \n"
	"SUB exponential.y, one.x, exponential.y; \n"
	"SGE modeMask.x, fogParams.w, 0.5; \n"
	"LRP exponential.x, modeMask.x, exponential.y, exponential.x; \n"
	"SUB linear.x, fogParams.y, fogParams.x; \n"
	"MAX linear.x, linear.x, epsilon.x; \n"
	"RCP linear.y, linear.x; \n"
	"MUL linear.x, distance.x, linear.y; \n"
	"MOV_SAT linear.x, linear.x; \n"
	"SGE modeMask.x, fogParams.w, 1.5; \n"
	"LRP exponential.x, modeMask.x, linear.x, exponential.x; \n"
	"MUL exponential.x, exponential.x, fogColor.w; \n"
	"SGE modeMask.x, depth.x, 0.99999; \n"
	"SUB modeMask.x, one.x, modeMask.x; \n"
	"LRP modeMask.x, depthParams.w, one.x, modeMask.x; \n"
	"MUL exponential.x, exponential.x, modeMask.x; \n"
	"LRP base.xyz, exponential.x, fogColor, base; \n"
	"MOV base.w, one.w; \n"
	"MOV result.color, base; \n"
	"END \n"
};
#endif

qboolean GL_DepthFadeProgramAvailable( void )
{
#ifdef USE_FBO
	return programCompiled && FBO_DepthFadeReady();
#else
	return qfalse;
#endif
}

void GL_DepthFadeProgramEnable( const shader_t *shader )
{
#ifdef USE_FBO
	const byte scaleAndBias = r_depthFadeScaleAndBias[shader->dfType];
	vec4_t scale;
	vec4_t bias;
	int i;

	if ( !GL_DepthFadeProgramAvailable() ) {
		return;
	}

	for ( i = 0; i < 4; i++ ) {
		scale[i] = ( scaleAndBias & ( 1 << i ) ) ? 1.0f : 0.0f;
		bias[i] = ( scaleAndBias & ( 1 << ( i + 4 ) ) ) ? 1.0f : 0.0f;
	}

	ARB_ProgramEnable( DUMMY_VERTEX, DEPTH_FADE_FRAGMENT );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 0,
		1.0f / (float)glConfig.vidWidth, 1.0f / (float)glConfig.vidHeight, 0.0f, 0.0f );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 1,
		backEnd.viewParms.zFar / r_znear->value, backEnd.viewParms.zFar, shader->dfInvDist, shader->dfBias );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 2, scale[0], scale[1], scale[2], scale[3] );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 3, bias[0], bias[1], bias[2], bias[3] );
#endif
}


#ifdef USE_FBO
qboolean GL_LiquidProgramAvailable( void )
{
	return ( fboEnabled && liquidArbProgramsCompiled && programCompiled ) ? qtrue : qfalse;
}


void GL_LiquidProgramEnable( const liquidArbParams_t *lp, qboolean refractionBase )
{
	vec4_t waveK;
	vec3_t dirAmp[LIQUID_WAVE_OCTAVES];
	int i;

	if ( !GL_LiquidProgramAvailable() || !lp ) {
		return;
	}

	ARB_ProgramEnable( LIQUID_VERTEX,
		refractionBase ? LIQUID_REFRACTION_FRAGMENT : LIQUID_SHEEN_FRAGMENT );

	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 0,
		backEnd.viewParms.viewportWidth > 0 ? 1.0f / (float)backEnd.viewParms.viewportWidth : 1.0f,
		backEnd.viewParms.viewportHeight > 0 ? 1.0f / (float)backEnd.viewParms.viewportHeight : 1.0f,
		(float)backEnd.viewParms.viewportX, (float)backEnd.viewParms.viewportY );
	for ( i = 0; i < LIQUID_WAVE_OCTAVES; i++ ) {
		R_LiquidWaveOctave( i, waveK, dirAmp[i] );
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 1 + i,
			waveK[0], waveK[1], waveK[2], waveK[3] * lp->wrappedTime );
	}
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 4,
		dirAmp[0][0] * dirAmp[0][2], dirAmp[0][1] * dirAmp[0][2],
		dirAmp[1][0] * dirAmp[1][2], dirAmp[1][1] * dirAmp[1][2] );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 5,
		dirAmp[2][0] * dirAmp[2][2], dirAmp[2][1] * dirAmp[2][2], 0.0f, 0.0f );

	if ( refractionBase ) {
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 6,
			lp->warpPixels, lp->alphaScale,
			LIQUID_WARP_DISTANCE_REF, LIQUID_WARP_DISTANCE_MIN_ATTEN );
		/* eyePos.w: 1 enables the foreground depth rejection, 0 forces the
		 * comparison result to keep the warped coordinate. */
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 7,
			lp->eyePos[0], lp->eyePos[1], lp->eyePos[2],
			lp->depthAvailable ? 1.0f : 0.0f );
	} else {
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 6,
			LIQUID_NORMAL_PERTURB, lp->alphaScale, lp->reflectivity, 0.0f );
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 7,
			lp->eyePos[0], lp->eyePos[1], lp->eyePos[2], LIQUID_REFLECT_PROXY_VIEW );
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 8,
			lp->mvp[0], lp->mvp[4], lp->mvp[8], lp->mvp[12] );
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 9,
			lp->mvp[1], lp->mvp[5], lp->mvp[9], lp->mvp[13] );
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 10,
			lp->mvp[3], lp->mvp[7], lp->mvp[11], lp->mvp[15] );
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 11,
			lp->fresnelColor[0], lp->fresnelColor[1], lp->fresnelColor[2],
			LIQUID_REFLECT_PROXY_BASE );
	}
}
#endif /* USE_FBO */


#ifdef USE_FBO
static char *ARB_BuildGreyscaleProgram( char *buf ) {
	char *s;

	if ( r_greyscale->value == 0 ) {
		*buf = '\0';
		return buf;
	}

	s = Q_stradd( buf, "PARAM sRGB = { 0.2126, 0.7152, 0.0722, 1.0 }; \n" );

	if ( r_greyscale->value == 1.0 ) {
		Q_stradd( s, "DP3 base.xyz, base, sRGB; \n"  );
	} else {
		s = Q_stradd( s, "TEMP luma; \n" );
		s = Q_stradd( s, "DP3 luma, base, sRGB; \n" );
		/*s +=*/ sprintf( s, "LRP base.xyz, %1.2f, luma, base; \n", r_greyscale->value );
	}

	return buf;
}

static char *ARB_BuildToneMapProgram( char *buf ) {
	char *s = buf;

	*buf = '\0';

	switch ( FBO_ToneMapMode() ) {
	case 1:
		s = Q_stradd( s,
			"PARAM toneOne = { 1.0, 1.0, 1.0, 1.0 }; \n"
			"TEMP denom; \n"
			"ADD denom.xyz, base, toneOne; \n"
			"RCP denom.x, denom.x; \n"
			"RCP denom.y, denom.y; \n"
			"RCP denom.z, denom.z; \n"
			"MUL base.xyz, base, denom; \n" );
		break;
	case 2:
		s = Q_stradd( s,
			"PARAM acesA = { 2.51, 2.51, 2.51, 1.0 }; \n"
			"PARAM acesB = { 0.03, 0.03, 0.03, 1.0 }; \n"
			"PARAM acesC = { 2.43, 2.43, 2.43, 1.0 }; \n"
			"PARAM acesD = { 0.59, 0.59, 0.59, 1.0 }; \n"
			"PARAM acesE = { 0.14, 0.14, 0.14, 1.0 }; \n"
			"TEMP numerator; \n"
			"TEMP denominator; \n"
			"MAD numerator.xyz, base, acesA, acesB; \n"
			"MUL numerator.xyz, numerator, base; \n"
			"MAD denominator.xyz, base, acesC, acesD; \n"
			"MAD denominator.xyz, denominator, base, acesE; \n"
			"RCP denominator.x, denominator.x; \n"
			"RCP denominator.y, denominator.y; \n"
			"RCP denominator.z, denominator.z; \n"
			"MUL base.xyz, numerator, denominator; \n" );
		break;
	default:
		break;
	}

	return buf;
}

static char *ARB_BuildColorGradeProgram( char *buf ) {
	char *s = buf;
	const int mode = FBO_ColorGradeMode();
	const qboolean lgg = FBO_ColorGradeUsesLiftGammaGain( mode );
	const qboolean lut = FBO_ColorGradeLutActive();

	*buf = '\0';

	if ( !lgg && !lut ) {
		return buf;
	}

	s = Q_stradd( s, "PARAM zeroGrade = { 0.0, 0.0, 0.0, 0.0 }; \n" );

	if ( lgg ) {
		s = Q_stradd( s,
			"PARAM gradeLift = program.local[4]; \n"
			"PARAM gradeGamma = program.local[5]; \n"
			"PARAM gradeGain = program.local[6]; \n"
			"PARAM whitePoint0 = program.local[7]; \n"
			"PARAM whitePoint1 = program.local[8]; \n"
			"PARAM whitePoint2 = program.local[9]; \n"
			"TEMP gradeColor; \n"
			"ADD base.xyz, base, gradeLift; \n"
			"MAX base.xyz, base, zeroGrade; \n"
			"POW base.x, base.x, gradeGamma.x; \n"
			"POW base.y, base.y, gradeGamma.y; \n"
			"POW base.z, base.z, gradeGamma.z; \n"
			"MUL base.xyz, base, gradeGain; \n"
			"DP3 gradeColor.x, base, whitePoint0; \n"
			"DP3 gradeColor.y, base, whitePoint1; \n"
			"DP3 gradeColor.z, base, whitePoint2; \n"
			"MAX base.xyz, gradeColor, zeroGrade; \n" );
	}

	if ( lut ) {
		s = Q_stradd( s,
			"PARAM lutControl = program.local[10]; \n"
			"PARAM lutExtra = program.local[11]; \n"
			"PARAM oneGrade = { 1.0, 1.0, 1.0, 1.0 }; \n"
			"TEMP lutCoord; \n"
			"TEMP lutSlice; \n"
			"TEMP lutUv0; \n"
			"TEMP lutUv1; \n"
			"TEMP lutLow; \n"
			"TEMP lutHigh; \n"
			"MUL lutCoord.xyz, base, lutExtra.z; \n"
			"MAX lutCoord.xyz, lutCoord, zeroGrade; \n"
			"MIN lutCoord.xyz, lutCoord, oneGrade; \n"
			"MUL lutCoord.xyz, lutCoord, lutControl.y; \n"
			"FLR lutSlice.x, lutCoord.z; \n"
			"FRC lutSlice.y, lutCoord.z; \n"
			"ADD lutSlice.z, lutSlice.x, oneGrade.x; \n"
			"MIN lutSlice.z, lutSlice.z, lutControl.y; \n"
			"MAD lutUv0.x, lutSlice.x, lutExtra.y, lutCoord.x; \n"
			"ADD lutUv0.x, lutUv0.x, lutExtra.x; \n"
			"MUL lutUv0.x, lutUv0.x, lutControl.w; \n"
			"ADD lutUv0.y, lutCoord.y, lutExtra.x; \n"
			"MUL lutUv0.y, lutUv0.y, lutControl.z; \n"
			"MAD lutUv1.x, lutSlice.z, lutExtra.y, lutCoord.x; \n"
			"ADD lutUv1.x, lutUv1.x, lutExtra.x; \n"
			"MUL lutUv1.x, lutUv1.x, lutControl.w; \n"
			"MOV lutUv1.y, lutUv0.y; \n"
			"TEX lutLow, lutUv0, texture[2], 2D; \n"
			"TEX lutHigh, lutUv1, texture[2], 2D; \n"
			"LRP base.xyz, lutSlice.y, lutHigh, lutLow; \n"
			"MUL base.xyz, base, lutControl.x; \n" );
	}

	return buf;
}

static char *ARB_BuildOutputEncodeProgram( char *buf ) {
	char *s = buf;

	s = Q_stradd( s,
		"PARAM outputTransfer = program.local[3]; \n"
		"PARAM srgbCutoff = { 0.0031308, 0.0031308, 0.0031308, 1.0 }; \n"
		"PARAM srgbGamma = { 0.4166667, 0.4166667, 0.4166667, 1.0 }; \n"
		"PARAM srgbScaleLow = { 12.92, 12.92, 12.92, 1.0 }; \n"
		"PARAM srgbScaleHigh = { 1.055, 1.055, 1.055, 1.0 }; \n"
		"PARAM srgbOffset = { 0.055, 0.055, 0.055, 0.0 }; \n"
		"PARAM zero = { 0.0, 0.0, 0.0, 0.0 }; \n"
		"TEMP legacyOut; \n"
		"TEMP srgbHi; \n"
		"TEMP srgbLo; \n"
		"TEMP srgbMask; \n"
		"MAX base.xyz, base, zero; \n"
		"MOV legacyOut, base; \n"
		"POW legacyOut.x, legacyOut.x, gamma.x; \n"
		"POW legacyOut.y, legacyOut.y, gamma.y; \n"
		"POW legacyOut.z, legacyOut.z, gamma.z; \n"
		"MUL legacyOut.xyz, legacyOut, gamma.w; \n"
		"POW srgbHi.x, base.x, srgbGamma.x; \n"
		"POW srgbHi.y, base.y, srgbGamma.y; \n"
		"POW srgbHi.z, base.z, srgbGamma.z; \n"
		"MAD srgbHi.xyz, srgbHi, srgbScaleHigh, -srgbOffset; \n"
		"MUL srgbLo.xyz, base, srgbScaleLow; \n"
		"SLT srgbMask.xyz, base, srgbCutoff; \n"
		"LRP srgbHi.xyz, srgbMask, srgbLo, srgbHi; \n"
		"LRP base.xyz, outputTransfer.x, srgbHi, legacyOut; \n" );

	return buf;
}


static const char *gammaFP = {
	"!!ARBfp1.0 \n"
	"OPTION ARB_precision_hint_fastest; \n"
	"PARAM gamma = program.local[0]; \n"
	"PARAM exposure = program.local[2]; \n"
	"TEMP base; \n"
	"TEX base, fragment.texcoord[0], texture[0], 2D; \n"
	"MUL base.xyz, base, exposure.x; \n"
	"%s" // scene-linear color grading, if requested
	"%s" // tone scale, if scene-linear mode requested it
	"%s" // legacy gamma or SDR sRGB output transfer
	"%s" // for greyscale shader if needed
	"MOV base.w, 1.0; \n"
	"MOV_SAT result.color, base; \n"
	"END \n"
};

static char *ARB_BuildBloomProgram( char *buf ) {
	char *s = buf;

	s = Q_stradd( s,
		"!!ARBfp1.0 \n"
		"OPTION ARB_precision_hint_fastest; \n"
		"PARAM thres = program.local[0]; \n"
		"PARAM exposure = program.local[1]; \n"
		"PARAM knee = program.local[2]; \n"
		"TEMP base; \n"
		"TEMP intensity; \n"
		"TEX base, fragment.texcoord[0], texture[0], 2D; \n"
		"MUL base.xyz, base, exposure.x; \n" );

	if ( r_bloom_threshold_mode->integer == 0 ) {
		// max(r, g, b)
		s = Q_stradd( s,
			"MAX intensity.x, base.x, base.y; \n"
			"MAX intensity.x, intensity.x, base.z; \n" );
	} else if ( r_bloom_threshold_mode->integer == 1 ) {
		// (r+g+b)/3
		s = Q_stradd( s,
			"PARAM scale = { 0.3333, 0.3334, 0.3333, 1.0 }; \n"
			"DP3 intensity.x, base, scale; \n" );
	} else {
		// luma(r,g,b)
		s = Q_stradd( s,
			"PARAM luma = { 0.2126, 0.7152, 0.0722, 1.0 }; \n"
			"DP3 intensity.x, base, luma; \n" );
	}

	if ( r_bloom_soft_knee && r_bloom_soft_knee->value > 0.0f ) {
		s = Q_stradd( s,
			"PARAM smooth = { -2.0, 3.0, 0.0, 1.0 }; \n"
			"TEMP weight; \n"
			"TEMP weight2; \n"
			"TEMP smoothTerm; \n"
			"ADD weight.x, intensity.x, -knee.x; \n"
			"MUL_SAT weight.x, weight.x, knee.y; \n"
			"MUL weight2.x, weight.x, weight.x; \n"
			"MAD smoothTerm.x, smooth.x, weight.x, smooth.y; \n"
			"MUL weight.x, weight2.x, smoothTerm.x; \n"
			"MUL base.rgb, base, weight.x; \n" );
	} else {
		s = Q_stradd( s,
			"SGE intensity.w, intensity.x, thres.x; \n"
			"MUL base.rgb, base, intensity.w; \n" );
	}

	// modulation
	if ( r_bloom_modulate->integer ) {
		if ( r_bloom_modulate->integer == 1 ) {
			// by itself
			s = Q_stradd( s, "MUL base, base, base; \n" );
		} else {
			// by intensity
			if ( r_bloom_threshold_mode->integer != 2 ) {
				s = Q_stradd( s,
					"PARAM modLuma = { 0.2126, 0.7152, 0.0722, 1.0 }; \n"
					"DP3 intensity.x, base, modLuma; \n" );
			}
			s = Q_stradd( s, "MUL base, base, intensity.x; \n" );
		}
	}

	/*s = */ Q_stradd( s,
		"MOV base.w, 1.0; \n"
		"MOV result.color, base; \n"
		"END \n" );

	return buf;
}


// Gaussian blur shader
static char *ARB_BuildBlurProgram( char *buf, int taps, qboolean clampOutput,
	qboolean clampSamples ) {
	int i;
	char *s = buf;

	*s = '\0';

	s = Q_stradd( s,
		"!!ARBfp1.0 \n"
		"OPTION ARB_precision_hint_fastest; \n"
		"ATTRIB tc = fragment.texcoord[0]; \n" );

	for ( i = 0; i < taps; i++ ) {
		s = Q_stradd( s, va( "PARAM p%i = program.local[%i]; \n", i, i ) ); // tex_offset_x, tex_offset_y, 0.0, weight
	}
	if ( clampSamples ) {
		s = Q_stradd( s, va( "PARAM sampleMin = program.local[%i]; \n"
			"PARAM sampleMax = program.local[%i]; \n", taps, taps + 1 ) );
	}

	s = Q_stradd( s, "TEMP cc; \n"
		"MOV cc, {0.0, 0.0, 0.0, 1.0};\n" ); // initialize final color

	for ( i = 0; i < taps; i++ ) {
		s = Q_stradd( s, va( "TEMP c%i, tc%i; \n", i, i ) );
	}

	for ( i = 0; i < taps; i++ ) {
		s = Q_stradd( s, va( "ADD tc%i.xy, tc, p%i; \n", i, i ) );
		if ( clampSamples ) {
			s = Q_stradd( s, va( "MAX tc%i.xy, tc%i, sampleMin; \n"
				"MIN tc%i.xy, tc%i, sampleMax; \n", i, i, i, i ) );
		}
	}

	for ( i = 0; i < taps; i++ ) {
		s = Q_stradd( s, va( "TEX c%i, tc%i, texture[0], 2D; \n", i, i ) );
		s = Q_stradd( s, va( "MAD cc, c%i, p%i.w, cc; \n", i, i ) ); // cc = cc + cN + pN.w
	}

	s = Q_stradd( s, "MOV cc.a, 1.0; \n" );
	s = Q_stradd( s, clampOutput ? "MOV_SAT result.color, cc; \n" :
		"MOV result.color, cc; \n" );
	/*s = */ Q_stradd( s, "END \n" );

	return buf;
}


static char *ARB_BuildBlendProgram( char *buf, int count ) {
	int i;
	char *s = buf;

	*s = '\0';
	s = Q_stradd( s, 
		"!!ARBfp1.0 \n"
		"OPTION ARB_precision_hint_fastest; \n"
		"ATTRIB tc = fragment.texcoord[0]; \n"
		"TEMP cx, cc;\n"
		"MOV cc, {0.0, 0.0, 0.0, 1.0}; \n" );

	for ( i = 0; i < count; i++ ) {
		s = Q_stradd( s, va( "TEX cx, fragment.texcoord[0], texture[%i], 2D; \n"
			"ADD cc, cx, cc; \n", i ) );
	}

	/*s = */ Q_stradd( s,
		"MOV cc.a, 1.0; \n"
		"MOV_SAT result.color, cc; \n"
		"END \n" );

	return buf;
}


// blend 2 texture together
static const char *blend2FP = {
	"!!ARBfp1.0 \n"
	"OPTION ARB_precision_hint_fastest; \n"
	"PARAM factor = program.local[1]; \n"
	"TEMP base; \n"
	"TEMP post; \n"
	"TEMP gate; \n"
	"TEMP guarded; \n"
	"TEX base, fragment.texcoord[0], texture[0], 2D; \n"
	"TEX post, fragment.texcoord[0], texture[1], 2D; \n"
	"MOV_SAT gate, base; \n"
	"MUL guarded, post, gate; \n"
	"SUB guarded, guarded, post; \n"
	"MAD post, guarded, factor.y, post; \n"
	"MAD base, post, factor.x, base; \n"
	//"ADD base, base, post; \n"
	"MOV base.w, 1.0; \n"
	"MOV_SAT result.color, base; \n"
	"END \n"
};

// combined blend + gamma correction pass
static const char *blend2gammaFP = {
	"!!ARBfp1.0 \n"
	"OPTION ARB_precision_hint_fastest; \n"
	"PARAM gamma = program.local[0]; \n"
	"PARAM factor = program.local[1]; \n"
	"PARAM exposure = program.local[2]; \n"
	"TEMP base; \n"
	"TEMP post; \n"
	"TEMP gate; \n"
	"TEMP guarded; \n"
	"TEX base, fragment.texcoord[0], texture[0], 2D; \n"
	"TEX post, fragment.texcoord[0], texture[1], 2D; \n"
	//"ADD base, base, post; \n"
	"MOV_SAT gate, base; \n"
	"MUL guarded, post, gate; \n"
	"SUB guarded, guarded, post; \n"
	"MAD post, guarded, factor.y, post; \n"
	"MAD base, post, factor.x, base; \n"
	"MUL base.xyz, base, exposure.x; \n"
	"%s" // scene-linear color grading, if requested
	"%s" // tone scale, if scene-linear mode requested it
	"%s" // legacy gamma or SDR sRGB output transfer
	"%s" // for greyscale shader if needed
	"MOV base.w, 1.0; \n"
	"MOV_SAT result.color, base; \n"
	"END \n" 
};


static void RenderQuad( int w, int h )
{
	static const vec2_t t[4] = { {0.0, 1.0}, {1.0, 1.0}, {0.0, 0.0}, {1.0, 0.0} };
	static vec3_t v[4] = { { 0 } };
	qboolean glxStreamedDraw = qfalse;
	
	v[1][0] = w;
	v[2][1] = h;
	v[3][0] = w;
	v[3][1] = h;

	GL_ClientState( 0, CLS_TEXCOORD_ARRAY );
#ifdef RENDERER_GLX
	GLX_CompatRecordFullscreenPass();
#endif

	qglVertexPointer( 3, GL_FLOAT, 0, v );
	qglTexCoordPointer( 2, GL_FLOAT, 0, t );

	if ( GLX_CompatStreamDrawPostProcessEnabled() ) {
		glxStreamedDraw = GLX_CompatTryStreamDrawArrayTexcoordPass( 4, v,
			(int)sizeof( v[0] ), t, 0, GL_TRIANGLE_STRIP, GLX_STAGE_POSTPROCESS_PASS,
			GLX_DYNAMIC_CATEGORY_MASK_SPECIAL );
	}
	if ( !glxStreamedDraw ) {
#ifdef RENDERER_GLX
		GLX_CompatDrawArrays( GL_TRIANGLE_STRIP, 0, 4,
			GLX_LEGACY_DELEGATION_DRAW_ARRAY, GLX_DRAW_NONE );
#else
		qglDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
#endif
	}
}


static void ARB_BlurParams( int width, int height, int ksize, qboolean horizontal )
{
	static float weight[ MAX_FILTER_SIZE ];
	static int old_ksize = -1;

	static const float x_k[ MAX_FILTER_SIZE+1 ][ MAX_FILTER_SIZE + 1 ] = {
		// [1/weight], coeff.1, coeff.2, [...]
		{ 0 },
		{ 1.0/1, 1 },
		{ 1.0/2, 1, 1 },
	//	{ 1/4,   1, 2, 1 },
		{ 1.0/16,  5, 6, 5 },
		{ 1.0/8,   1, 3, 3, 1 },
		{ 1.0/16,  1, 4, 6, 4, 1 },
		{ 1.0/32,  1, 5, 10, 10, 5, 1 },
		{ 1.0/64,  1, 6, 15, 20, 15, 6, 1 },
		{ 1.0/128, 1, 7, 21, 35, 35, 21, 7, 1 },
		{ 1.0/256, 1, 8, 28, 56, 70, 56, 28, 8, 1 },
		{ 1.0/512, 1, 9, 36, 84, 126, 126, 84, 36, 9, 1 },
		{ 1.0/1024, 1, 10, 45, 120, 210, 252, 210, 120, 45, 10, 1 },
		{ 1.0/2048, 1, 11, 55, 165, 330, 462, 462, 330, 165, 55, 11, 1 },
		{ 1.0/4096, 1, 12, 66, 220, 495, 792, 924, 792, 495, 220, 66, 12, 1 },
		{ 1.0/8192, 1, 13, 78, 286, 715, 1287, 1716, 1716, 1287, 715, 286, 78, 13, 1 },
		{ 1.0/16384, 1, 14, 91, 364, 1001, 2002, 3003, 3432, 3003, 2002, 1001, 364, 91, 14, 1 },
		{ 1.0/32768, 1, 15, 105, 455, 1365, 3003, 5005, 6435, 6435, 5005, 3003, 1365, 455, 105, 15, 1 },
		{ 1.0/65536, 1, 16, 120, 560, 1820, 4368, 8008, 11440, 12870, 11440, 8008, 4368, 1820, 560, 120, 16, 1 },
		{ 1.0/131072, 1, 17, 136, 680, 2380, 6188, 12376, 19448, 24310, 24310, 19448, 12376, 6188, 2380, 680, 136, 17, 1 },
		{ 1.0/262144, 1, 18, 153, 816, 3060, 8568, 18564, 31824, 43758, 48620, 43758, 31824, 18564, 8568, 3060, 816, 153, 18, 1 },
		{ 1.0/524288, 1, 19, 171, 969, 3876, 11628, 27132, 50388, 75582, 92378, 92378, 75582, 50388, 27132, 11628, 3876, 969, 171, 19, 1 },

	};

	static const float x_o[ MAX_FILTER_SIZE+1 ][ MAX_FILTER_SIZE ] = {
		{ 0 },
		{ 0.0 },
		{ -0.5, 0.5 },
	//	{ -1.0, 0.0, 1.0 },
		{ -1.2f, 0.0, 1.2f },
		{ -1.5, -0.5, 0.5, 1.5 },
		{ -2.0, -1.0, 0.0, 1.0, 2.0 },
		{ -2.5, -1.5, -0.5, 0.5, 1.5, 2.5 },
		{ -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0 },
		{ -3.5, -2.5, -1.5, -0.5, 0.5, 1.5, 2.5, 3.5 },
		{ -4.0, -3.0, -2.0, -1.0, 0.0, 1.0,	2.0, 3.0, 4.0 },
		{ -4.5, -3.5, -2.5, -1.5, -0.5, 0.5, 1.5, 2.5, 3.5, 4.5 },
		{ -5.0, -4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0, 5.0 },
		{ -5.5, -4.5, -3.5, -2.5, -1.5, -0.5, 0.5, 1.5, 2.5, 3.5, 4.5, 5.5 },
		{ -6.0, -5.0, -4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0 },
		{ -6.5, -5.5, -4.5, -3.5, -2.5, -1.5, -0.5, 0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5 },
		{ -7.0, -6.0, -5.0, -4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0 },
		{ -7.5, -6.5, -5.5, -4.5, -3.5, -2.5, -1.5, -0.5, 0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5 },
		{ -8.0, -7.0, -6.0, -5.0, -4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0 },
		{ -8.5, -7.5, -6.5, -5.5, -4.5, -3.5, -2.5, -1.5, -0.5, 0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5 },
		{ -9.0, -8.0, -7.0, -6.0, -5.0, -4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0 },
		{ -9.5, -8.5, -7.5, -6.5, -5.5, -4.5, -3.5, -2.5, -1.5, -0.5, 0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5 },
	};

	const float *coeffs = x_k[ ksize ] + 1;
	const float *off = x_o[ ksize ];

	int i;
	float rsum;
	float texel_size_x;
	float texel_size_y;
	float offset[ MAX_FILTER_SIZE ][ 2 ]; // xy

	// texel size
	texel_size_x = 1.0 / (float) width;
	texel_size_y = 1.0 / (float) height;
	rsum = x_k[ ksize ][ 0 ];

	if ( old_ksize != ksize ) {
		old_ksize = ksize;
		for ( i = 0; i < ksize; i++ ) {
			weight[i] = coeffs[i] * rsum;
		}
	}

	// calculate texture offsets for lookup
	for ( i = 0; i < ksize; i++ ) {
		offset[i][0] = texel_size_x * off[i];
		offset[i][1] = texel_size_y * off[i];
	}

	if ( horizontal ) {
		// horizontal pass
		for (  i = 0; i < ksize; i++ )
			qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, i, offset[i][0], 0.0, 0.0, weight[i] );
	} else {
		// vertical pass
		for (  i = 0; i < ksize; i++ )
			qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, i, 0.0, offset[i][1], 0.0, weight[i] );
	}
}


static void ARB_MotionBlurParams( const float radiusUv[2], const float sampleBounds[4] )
{
	static const float offsets[7] = { -1.0f, -0.6666667f, -0.3333333f, 0.0f,
		0.3333333f, 0.6666667f, 1.0f };
	static const float weights[7] = { 1.0f / 64.0f, 6.0f / 64.0f, 15.0f / 64.0f,
		20.0f / 64.0f, 15.0f / 64.0f, 6.0f / 64.0f, 1.0f / 64.0f };
	int i;

	for ( i = 0; i < 7; i++ ) {
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, i,
			radiusUv[0] * offsets[i], radiusUv[1] * offsets[i], 0.0f, weights[i] );
	}
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 7,
		sampleBounds[0], sampleBounds[1], 0.0f, 0.0f );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 8,
		sampleBounds[2], sampleBounds[3], 0.0f, 0.0f );
}
#endif // USE_FBO


static void ARB_DeletePrograms( void )
{
	qglDeleteProgramsARB( ARRAY_LEN( programs ) - PROGRAM_BASE, programs + PROGRAM_BASE );
	Com_Memset( programs, 0, sizeof( programs ) );
	programCompiled = 0;
	globalFogProgramCompiled = qfalse;
	globalFogCompositorStateLogged = qfalse;
	globalFogCompositorActiveLogged = qfalse;
	dlightShadowProgramsCompiled = qfalse;
	csmShadowProgramsCompiled = qfalse;
#ifdef USE_FBO
	liquidArbProgramsCompiled = qfalse;
#endif
}


static qboolean ARB_CompileProgramInternal( programType ptype, const char *text, GLuint program, qboolean fatal )
{
	GLint errorPos;
	unsigned int errCode;
	int kind;

	if ( ptype == Fragment )
		kind = GL_FRAGMENT_PROGRAM_ARB;
	else
		kind = GL_VERTEX_PROGRAM_ARB;

	qglBindProgramARB( kind, program );
	qglProgramStringARB( kind, GL_PROGRAM_FORMAT_ASCII_ARB, strlen( text ), text );
	qglGetIntegerv( GL_PROGRAM_ERROR_POSITION_ARB, &errorPos );
	if ( (errCode = qglGetError()) != GL_NO_ERROR || errorPos != -1 )
	{
		// we may receive error with active FBO but compiled programs will continue to work properly
		if ( (errCode == GL_INVALID_OPERATION && !fboEnabled) || errorPos != -1 )
		{
			ri.Printf( PRINT_ALL, S_COLOR_YELLOW "%s Compile Error(%i,%i): %s\n" S_COLOR_CYAN "%s\n", (ptype == Fragment) ? "FP" : "VP",
				errCode, errorPos, qglGetString( GL_PROGRAM_ERROR_STRING_ARB ), text );
			qglBindProgramARB( kind, 0 );
			if ( fatal ) {
				ARB_DeletePrograms();
			}
			return qfalse;
		}
	}

	return qtrue;
}


qboolean ARB_CompileProgram( programType ptype, const char *text, GLuint program )
{
	return ARB_CompileProgramInternal( ptype, text, program, qtrue );
}


qboolean ARB_UpdatePrograms( void )
{
#ifdef USE_PMLIGHT
	const char *program;
	int i;
#endif
#if defined (USE_FBO) || defined (USE_PMLIGHT)
	char buf[16384];
#endif
#ifdef USE_FBO
	char buf2[8192];
	char buf3[8192];
	char buf4[8192];
#endif

	if ( !qglGenProgramsARB )
		return qfalse;

	if ( programCompiled ) // delete old programs
	{
		ARB_ProgramDisable();
		ARB_DeletePrograms();
	}

	qglGenProgramsARB( ARRAY_LEN( programs ) - PROGRAM_BASE, programs + PROGRAM_BASE );

#ifdef USE_PMLIGHT
	if ( !ARB_CompileProgram( Vertex, va( dlightVP, "" ), programs[ DLIGHT_VERTEX ] ) )
		return qfalse;
	if ( !ARB_CompileProgram( Vertex, va( dlightVP, fogInVPCode ), programs[ DLIGHT_VERTEX_FOG_IN ] ) )
		return qfalse;
	if ( !ARB_CompileProgram( Vertex, va( dlightVP, fogOutVPCode ), programs[ DLIGHT_VERTEX_FOG_OUT ] ) )
		return qfalse;

	for ( i = DLIGHT_FRAGMENT; i <= DLIGHT_LINEAR_ABS_FRAGMENT_FOG; i++ ) {
		program = ARB_BuildDlightFP( buf, i );
		if ( !ARB_CompileProgram( Fragment, program, programs[ i ] ) ) {
			return qfalse;
		}
	}

	dlightShadowProgramsCompiled = qfalse;
	if ( ( ( r_dlightShadows && r_dlightShadows->integer ) ||
		( r_shadowCorrectness && r_shadowCorrectness->integer ) ) &&
		glConfig.numTextureUnits > 2 ) {
		qboolean shadowProgramsOk = qtrue;

		for ( i = DLIGHT_SHADOW_FRAGMENT; i <= DLIGHT_SHADOW_LINEAR_ABS_FRAGMENT_FOG; i++ ) {
			program = ARB_BuildDlightFP( buf, i );
			if ( !ARB_CompileProgramInternal( Fragment, program, programs[ i ], qfalse ) ) {
				shadowProgramsOk = qfalse;
				break;
			}
		}

		if ( shadowProgramsOk ) {
			dlightShadowProgramsCompiled = qtrue;
		} else {
			ri.Printf( PRINT_ALL, S_COLOR_YELLOW "WARNING: ARB dynamic light shadow programs failed; disabling r_dlightShadows on GLx.\n" );
		}
	}

	csmShadowProgramsCompiled = qfalse;
	if ( glConfig.numTextureUnits > 1 ) {
		if ( ARB_CompileProgramInternal( Vertex, csmShadowVP, programs[ CSM_SHADOW_VERTEX ], qfalse ) &&
			ARB_CompileProgramInternal( Fragment, csmShadowFP, programs[ CSM_SHADOW_FRAGMENT ], qfalse ) ) {
			csmShadowProgramsCompiled = qtrue;
		} else if ( r_csmShadows && r_csmShadows->integer ) {
			ri.Printf( PRINT_ALL, S_COLOR_YELLOW "WARNING: ARB sky-sun shadow programs failed; disabling r_csmShadows on GLx for this session.\n" );
		}
	}

	// optional: the caster loops fall back to CPU tessellation when absent
	dlightShadowCasterVPCompiled = ARB_CompileProgramInternal( Vertex,
		dlightShadowCasterVP, programs[ DLIGHT_SHADOW_CASTER_VERTEX ], qfalse );
#endif // USE_PMLIGHT

	if ( !ARB_CompileProgram( Vertex, dummyVP, programs[ DUMMY_VERTEX ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Fragment, spriteFP, programs[ SPRITE_FRAGMENT ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Fragment, depthFadeFP, programs[ DEPTH_FADE_FRAGMENT ] ) )
		return qfalse;

#ifdef USE_FBO
	liquidArbProgramsCompiled = qfalse;
	if ( ARB_CompileProgramInternal( Vertex, liquidVP, programs[ LIQUID_VERTEX ], qfalse ) &&
		ARB_CompileProgramInternal( Fragment, liquidRefractionFP, programs[ LIQUID_REFRACTION_FRAGMENT ], qfalse ) &&
		ARB_CompileProgramInternal( Fragment, liquidSheenFP, programs[ LIQUID_SHEEN_FRAGMENT ], qfalse ) ) {
		liquidArbProgramsCompiled = qtrue;
	} else {
		ri.Printf( PRINT_WARNING,
			"...per-fragment liquid programs unavailable; enhanced liquids use the projective fallback\n" );
	}

	if ( !ARB_CompileProgram( Fragment, worldCelOutlineFP, programs[ WORLD_CEL_FRAGMENT ] ) )
		return qfalse;
	globalFogProgramCompiled = qfalse;
	if ( r_globalFog && r_globalFog->integer ) {
		if ( ARB_CompileProgramInternal( Fragment, globalFogFP,
			programs[ GLOBAL_FOG_FRAGMENT ], qfalse ) ) {
			globalFogProgramCompiled = qtrue;
		} else {
			ri.Printf( PRINT_WARNING,
				"WARNING: optional global fog program unavailable; r_globalFog is disabled for this renderer session\n" );
		}
	}
	if ( !ARB_CompileProgram( Fragment, ARB_BuildBlurProgram( buf, 7, qfalse, qtrue ), programs[ MOTION_BLUR_FRAGMENT ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Fragment, va( gammaFP,
			ARB_BuildColorGradeProgram( buf ), ARB_BuildToneMapProgram( buf2 ),
			ARB_BuildOutputEncodeProgram( buf3 ), ARB_BuildGreyscaleProgram( buf4 ) ),
			programs[ GAMMA_FRAGMENT ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Fragment, ARB_BuildBloomProgram( buf ), programs[ BLOOM_EXTRACT_FRAGMENT ] ) )
		return qfalse;
	
	// only 1, 2, 3, 6, 8, 10, 12, 14, 16, 18 and 20 produces real visual difference
	fboBloomFilterSize = r_bloom_filter_size->integer;
	if ( !ARB_CompileProgram( Fragment, ARB_BuildBlurProgram( buf, fboBloomFilterSize, qtrue, qfalse ), programs[ BLUR_FRAGMENT ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Fragment, ARB_BuildBlurProgram( buf, 6, qtrue, qfalse ), programs[ BLUR2_FRAGMENT ] ) )
		return qfalse;

	fboBloomBlendBase = r_bloom_blend_base->integer;
	if ( fboBloomBlendBase < 0 )
		fboBloomBlendBase = 0;
	if ( fboBloomBlendBase >= r_bloom_passes->integer )
		fboBloomBlendBase = r_bloom_passes->integer - 1;
	if ( !ARB_CompileProgram( Fragment, ARB_BuildBlendProgram( buf, r_bloom_passes->integer - fboBloomBlendBase ), programs[ BLENDX_FRAGMENT ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Fragment, blend2FP, programs[ BLEND2_FRAGMENT ] ) )
		return qfalse;

	if ( !ARB_CompileProgram( Fragment, va( blend2gammaFP,
			ARB_BuildColorGradeProgram( buf ), ARB_BuildToneMapProgram( buf2 ),
			ARB_BuildOutputEncodeProgram( buf3 ), ARB_BuildGreyscaleProgram( buf4 ) ),
			programs[ BLEND2_GAMMA_FRAGMENT ] ) )
		return qfalse;
#endif // USE_FBO

	programCompiled = 1;

	return qtrue;
}

#ifdef USE_FBO

static void FBO_Bind( GLuint target, GLuint buffer );
static const char *glDefToStr( GLint define );
static qboolean FBO_Create( frameBuffer_t *fb, GLsizei width, GLsizei height,
	qboolean depthStencil, GLint *outFormat, GLint *outType );

void FBO_Clean( frameBuffer_t *fb )
{
	if ( fb->fbo )
	{
		FBO_Bind( GL_FRAMEBUFFER, fb->fbo );
		if ( fb->multiSampled )
		{
			qglBindRenderbuffer( GL_RENDERBUFFER, 0 );
			if ( fb->color )
			{
				qglDeleteRenderbuffers( 1, &fb->color );
				fb->color = 0;
			}
			if ( fb->depthStencil )
			{
				qglDeleteRenderbuffers( 1, &fb->depthStencil );
				fb->depthStencil = 0;
			}
		}
		else
		{
			GL_BindTexture( 0, 0 );
			if ( fb->color )
			{
				qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0 );
				qglDeleteTextures( 1, &fb->color );
				GL_InvalidateDeletedTexture( fb->color );
				fb->color = 0;
			}
			if ( fb->depthStencil )
			{
#ifdef DEPTH_RENDER_BUFFER
				if ( fb->depthStencil && fb->depthStencil != commonDepthStencil )
				{
					qglDeleteRenderbuffers( 1, &fb->depthStencil );
					fb->depthStencil = 0;
				}
#else
				if ( glConfig.stencilBits == 0 )
					qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0 );
				else
					qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0 );

				if ( fb->depthStencil && fb->depthStencil != commonDepthStencil )
				{
					qglDeleteTextures( 1, &fb->depthStencil );
					GL_InvalidateDeletedTexture( fb->depthStencil );
					fb->depthStencil = 0;
				}
#endif
			}
		}
		FBO_Bind( GL_FRAMEBUFFER, 0 );
		qglDeleteFramebuffers( 1, &fb->fbo );
		fb->fbo = 0;
	}
	Com_Memset( fb, 0, sizeof( *fb ) );
}


static void FBO_CleanBloom( void )
{
	int i;
	for ( i = 0; i < MAX_BLUR_PASSES; i++ )
	{
		FBO_Clean( &frameBuffers[ i * 2 + BLOOM_BASE + 0 ] );
		FBO_Clean( &frameBuffers[ i * 2 + BLOOM_BASE + 1 ] );
	}
}


static void FBO_InvalidateDlightShadowAtlasGeneration( void )
{
	dlightShadowAtlasGeneration++;
	if ( !dlightShadowAtlasGeneration ) {
		dlightShadowAtlasGeneration++;
	}
}

static void FBO_InvalidateSpotShadowAtlasGeneration( void )
{
	spotShadowAtlasGeneration++;
	if ( !spotShadowAtlasGeneration ) {
		spotShadowAtlasGeneration++;
	}
}

static void FBO_InvalidateCSMShadowAtlasGeneration( void )
{
	csmShadowAtlasGeneration++;
	if ( !csmShadowAtlasGeneration ) {
		csmShadowAtlasGeneration++;
	}
}


static void FBO_CleanDlightShadowAtlas( void )
{
	if ( dlightShadowAtlasFbo )
	{
		FBO_Bind( GL_FRAMEBUFFER, 0 );
		qglDeleteFramebuffers( 1, &dlightShadowAtlasFbo );
		dlightShadowAtlasFbo = 0;
	}
	if ( dlightShadowAtlasTexture )
	{
		GL_BindTexture( 1, 0 );
		qglDeleteTextures( 1, &dlightShadowAtlasTexture );
		GL_InvalidateDeletedTexture( dlightShadowAtlasTexture );
		dlightShadowAtlasTexture = 0;
	}
	Com_Memset( &dlightShadowAtlasLayout, 0, sizeof( dlightShadowAtlasLayout ) );
	dlightShadowAtlasRendered = qfalse;
	FBO_InvalidateDlightShadowAtlasGeneration();
}

static void FBO_CleanSpotShadowAtlas( void )
{
	if ( spotShadowAtlasFbo )
	{
		FBO_Bind( GL_FRAMEBUFFER, 0 );
		qglDeleteFramebuffers( 1, &spotShadowAtlasFbo );
		spotShadowAtlasFbo = 0;
	}
	if ( spotShadowAtlasTexture )
	{
		GL_BindTexture( 1, 0 );
		GL_SelectTexture( 0 );
		qglDeleteTextures( 1, &spotShadowAtlasTexture );
		GL_InvalidateDeletedTexture( spotShadowAtlasTexture );
		spotShadowAtlasTexture = 0;
	}
	Com_Memset( &spotShadowAtlasLayout, 0, sizeof( spotShadowAtlasLayout ) );
	spotShadowAtlasRendered = qfalse;
	FBO_InvalidateSpotShadowAtlasGeneration();
}

static void FBO_CleanCSMShadowAtlas( void )
{
	if ( csmShadowAtlasFbo )
	{
		FBO_Bind( GL_FRAMEBUFFER, 0 );
		qglDeleteFramebuffers( 1, &csmShadowAtlasFbo );
		csmShadowAtlasFbo = 0;
	}
	if ( csmShadowAtlasTexture )
	{
		GL_BindTexture( 1, 0 );
		GL_SelectTexture( 0 );
		qglDeleteTextures( 1, &csmShadowAtlasTexture );
		GL_InvalidateDeletedTexture( csmShadowAtlasTexture );
		csmShadowAtlasTexture = 0;
	}
	Com_Memset( &csmShadowAtlasLayout, 0, sizeof( csmShadowAtlasLayout ) );
	csmShadowAtlasRendered = qfalse;
	FBO_InvalidateCSMShadowAtlasGeneration();
}


static void FBO_CleanDepth( void )
{
	FBO_CleanDlightShadowAtlas();
	FBO_CleanSpotShadowAtlas();
	FBO_CleanCSMShadowAtlas();

	if ( depthFadeTexture )
	{
		GL_BindTexture( 1, 0 );
		if ( !depthFadeTextureShared || depthFadeTexture != commonDepthStencil )
		{
			if ( commonDepthStencil == depthFadeTexture )
			{
				commonDepthStencil = 0;
			}
			qglDeleteTextures( 1, &depthFadeTexture );
			GL_InvalidateDeletedTexture( depthFadeTexture );
		}
		depthFadeTexture = 0;
		depthFadeCopied = qfalse;
		depthFadeTextureShared = qfalse;
	}

#ifdef COMMON_DEPTH_STENCIL
	if ( commonDepthStencil )
	{
#ifdef DEPTH_RENDER_BUFFER
		qglDeleteRenderbuffers( 1, &commonDepthStencil );
#else
		GL_BindTexture( 0, 0 );
		qglDeleteTextures( 1, &commonDepthStencil );
		GL_InvalidateDeletedTexture( commonDepthStencil );
#endif
		commonDepthStencil = 0;
	}
#endif
}


static GLuint FBO_CreateDepthFadeTexture( GLsizei width, GLsizei height, qboolean depthStencil )
{
	GLuint tex;

	qglGenTextures( 1, &tex );
	GL_BindTexture( 1, tex );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	if ( depthStencil )
		qglTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0,
			GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL );
	else
		qglTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32, width, height, 0,
			GL_DEPTH_COMPONENT, GL_FLOAT, NULL );

	return tex;
}


static qboolean FBO_DlightShadowAtlasWanted( dlightShadowAtlasLayout_t *layout )
{
#ifdef USE_PMLIGHT
	qboolean correctnessMode = ( r_shadowCorrectness && r_shadowCorrectness->integer ) ? qtrue : qfalse;

	if ( ( !correctnessMode && ( !r_dlightShadows || !r_dlightShadows->integer ||
		!r_dlightShadowMaxLights || r_dlightShadowMaxLights->integer <= 0 ) ) ||
		!r_dlightMode || !R_GetDlightMode() ) {
		return qfalse;
	}

	return R_DlightShadowAtlasLayout( correctnessMode ? 1 : r_dlightShadowMaxLights->integer,
		r_dlightShadowResolution ? r_dlightShadowResolution->integer : 256,
		glConfig.maxTextureSize, layout );
#else
	return qfalse;
#endif
}


static qboolean FBO_CreateDlightShadowAtlas( void )
{
	dlightShadowAtlasLayout_t layout;
	int fboStatus;

	if ( !FBO_DlightShadowAtlasWanted( &layout ) ) {
		return qfalse;
	}

	qglGenTextures( 1, &dlightShadowAtlasTexture );
	GL_BindTexture( 1, dlightShadowAtlasTexture );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32, layout.width, layout.height, 0,
		GL_DEPTH_COMPONENT, GL_FLOAT, NULL );

	qglGenFramebuffers( 1, &dlightShadowAtlasFbo );
	FBO_Bind( GL_FRAMEBUFFER, dlightShadowAtlasFbo );
	qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, dlightShadowAtlasTexture, 0 );
	qglDrawBuffer( GL_NONE );
	qglReadBuffer( GL_NONE );

	fboStatus = qglCheckFramebufferStatus( GL_FRAMEBUFFER );
	if ( fboStatus != GL_FRAMEBUFFER_COMPLETE )
	{
		ri.Printf( PRINT_WARNING, "Failed to create dynamic-light shadow atlas FBO (status %s, error %s)\n",
			glDefToStr( fboStatus ), glDefToStr( (int)qglGetError() ) );
		FBO_CleanDlightShadowAtlas();
		qglDrawBuffer( GL_BACK );
		qglReadBuffer( GL_BACK );
		return qfalse;
	}

	qglClear( GL_DEPTH_BUFFER_BIT );
	dlightShadowAtlasLayout = layout;
	FBO_InvalidateDlightShadowAtlasGeneration();
	FBO_Bind( GL_FRAMEBUFFER, 0 );
	qglDrawBuffer( GL_BACK );
	qglReadBuffer( GL_BACK );

	ri.Printf( PRINT_ALL, "...dynamic-light shadow atlas %ix%i (%i px faces, %i lights)\n",
		layout.width, layout.height, layout.faceSize, layout.maxLights );

	return qtrue;
}

static qboolean FBO_SpotShadowAtlasWanted( spotShadowAtlasLayout_t *layout )
{
#ifdef USE_PMLIGHT
	if ( ( r_shadowCorrectness && r_shadowCorrectness->integer ) ||
		!r_spotShadows || !r_spotShadows->integer ||
		!r_spotShadowMaxLights || r_spotShadowMaxLights->integer <= 0 ) {
		return qfalse;
	}

	return R_SpotShadowAtlasLayout( r_spotShadowMaxLights->integer,
		r_spotShadowResolution ? r_spotShadowResolution->integer : 256,
		glConfig.maxTextureSize, layout );
#else
	return qfalse;
#endif
}

static qboolean FBO_CreateSpotShadowAtlas( void )
{
	spotShadowAtlasLayout_t layout;
	int fboStatus;

	if ( !FBO_SpotShadowAtlasWanted( &layout ) ) {
		return qfalse;
	}

	qglGenTextures( 1, &spotShadowAtlasTexture );
	GL_BindTexture( 1, spotShadowAtlasTexture );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32, layout.width, layout.height, 0,
		GL_DEPTH_COMPONENT, GL_FLOAT, NULL );

	qglGenFramebuffers( 1, &spotShadowAtlasFbo );
	FBO_Bind( GL_FRAMEBUFFER, spotShadowAtlasFbo );
	qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, spotShadowAtlasTexture, 0 );
	qglDrawBuffer( GL_NONE );
	qglReadBuffer( GL_NONE );

	fboStatus = qglCheckFramebufferStatus( GL_FRAMEBUFFER );
	if ( fboStatus != GL_FRAMEBUFFER_COMPLETE )
	{
		ri.Printf( PRINT_WARNING, "Failed to create spotlight shadow atlas FBO (status %s, error %s)\n",
			glDefToStr( fboStatus ), glDefToStr( (int)qglGetError() ) );
		FBO_CleanSpotShadowAtlas();
		qglDrawBuffer( GL_BACK );
		qglReadBuffer( GL_BACK );
		GL_SelectTexture( 0 );
		return qfalse;
	}

	qglClear( GL_DEPTH_BUFFER_BIT );
	spotShadowAtlasLayout = layout;
	FBO_InvalidateSpotShadowAtlasGeneration();
	FBO_Bind( GL_FRAMEBUFFER, 0 );
	qglDrawBuffer( GL_BACK );
	qglReadBuffer( GL_BACK );
	GL_SelectTexture( 0 );

	ri.Printf( PRINT_ALL, "...spotlight shadow atlas %ix%i (%i px tiles, %i lights)\n",
		layout.width, layout.height, layout.tileSize, layout.maxLights );

	return qtrue;
}

static qboolean FBO_CSMShadowAtlasWanted( csmShadowAtlasLayout_t *layout )
{
#ifdef USE_PMLIGHT
	if ( ( r_shadowCorrectness && r_shadowCorrectness->integer ) ||
		!r_csmShadows || !r_csmShadows->integer ) {
		return qfalse;
	}

	return R_CSMShadowAtlasLayout(
		r_csmCascadeCount ? r_csmCascadeCount->integer : CSM_MAX_CASCADES,
		r_csmResolution ? r_csmResolution->integer : 1024,
		glConfig.maxTextureSize, layout );
#else
	return qfalse;
#endif
}

static qboolean FBO_CreateCSMShadowAtlas( void )
{
	csmShadowAtlasLayout_t layout;
	int fboStatus;

	if ( !FBO_CSMShadowAtlasWanted( &layout ) ) {
		return qfalse;
	}
	if ( csmShadowAtlasCreateFailed ) {
		return qfalse;
	}

	qglGenTextures( 1, &csmShadowAtlasTexture );
	GL_BindTexture( 1, csmShadowAtlasTexture );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32, layout.width, layout.height, 0,
		GL_DEPTH_COMPONENT, GL_FLOAT, NULL );

	qglGenFramebuffers( 1, &csmShadowAtlasFbo );
	FBO_Bind( GL_FRAMEBUFFER, csmShadowAtlasFbo );
	qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, csmShadowAtlasTexture, 0 );
	qglDrawBuffer( GL_NONE );
	qglReadBuffer( GL_NONE );

	fboStatus = qglCheckFramebufferStatus( GL_FRAMEBUFFER );
	if ( fboStatus != GL_FRAMEBUFFER_COMPLETE )
	{
		ri.Printf( PRINT_WARNING, "Failed to create sky-sun shadow atlas FBO (status %s, error %s)\n",
			glDefToStr( fboStatus ), glDefToStr( (int)qglGetError() ) );
		FBO_CleanCSMShadowAtlas();
		csmShadowAtlasCreateFailed = qtrue;
		qglDrawBuffer( GL_BACK );
		qglReadBuffer( GL_BACK );
		GL_SelectTexture( 0 );
		return qfalse;
	}

	qglClear( GL_DEPTH_BUFFER_BIT );
	csmShadowAtlasLayout = layout;
	FBO_InvalidateCSMShadowAtlasGeneration();
	FBO_Bind( GL_FRAMEBUFFER, 0 );
	qglDrawBuffer( GL_BACK );
	qglReadBuffer( GL_BACK );
	GL_SelectTexture( 0 );
	csmShadowAtlasCreateFailed = qfalse;

	ri.Printf( PRINT_ALL, "...sky-sun shadow atlas %ix%i (%i px cascades, %i cascades)\n",
		layout.width, layout.height, layout.cascadeSize, layout.cascadeCount );

	return qtrue;
}


static GLuint FBO_CreateDepthTextureOrBuffer( GLsizei width, GLsizei height )
{
#ifdef DEPTH_RENDER_BUFFER
	GLuint buffer;
	qglGenRenderbuffers( 1, &buffer );
	qglBindRenderbuffer( GL_RENDERBUFFER, buffer );
	if ( glConfig.stencilBits == 0 )
		qglRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT32, width, height );
	else
		qglRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height );
	return buffer;
#else
	GLuint tex;
	// Let the main scene depth attachment double as the sampled depth texture
	// for depth-fade and world-outline shaders.
	if ( !depthFadeTexture &&
		width == glConfig.vidWidth && height == glConfig.vidHeight )
	{
		depthFadeTexture = FBO_CreateDepthFadeTexture( width, height,
			glConfig.stencilBits > 0 ? qtrue : qfalse );
		depthFadeTextureShared = qtrue;
		return depthFadeTexture;
	}
	qglGenTextures( 1, &tex );
	GL_BindTexture( 0, tex );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	if ( glConfig.stencilBits == 0 )
		qglTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL );
	else
		qglTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL );
	return tex;
#endif
}


static const char *glDefToStr( GLint define )
{
	#define CASE_STR(x) case (x): return #x
	static int index;
	static char buf[8][32];
	char *s;

	switch ( define )
	{
		// texture formats
		CASE_STR(GL_BGR);
		CASE_STR(GL_BGRA);
		CASE_STR(GL_RG);
		CASE_STR(GL_RGB);
		CASE_STR(GL_RGBA);
		CASE_STR(GL_RGBA4);
		CASE_STR(GL_RGBA8);
		CASE_STR(GL_RGBA12);
		CASE_STR(GL_RGBA16);
		CASE_STR(GL_RG16F);
		CASE_STR(GL_RGBA16F);
		CASE_STR(GL_RGB16F);
		CASE_STR(GL_RGB10_A2);
		CASE_STR(GL_R11F_G11F_B10F);
		CASE_STR(GL_SRGB);
		CASE_STR(GL_SRGB8);
		CASE_STR(GL_SRGB_ALPHA);
		CASE_STR(GL_SRGB8_ALPHA8);
		// data types
		CASE_STR(GL_BYTE);
		CASE_STR(GL_UNSIGNED_BYTE);
		CASE_STR(GL_SHORT);
		CASE_STR(GL_UNSIGNED_SHORT);
		CASE_STR(GL_INT);
		CASE_STR(GL_UNSIGNED_INT);
		CASE_STR(GL_FLOAT);
		CASE_STR(GL_HALF_FLOAT);
		CASE_STR(GL_DOUBLE);
		CASE_STR(GL_UNSIGNED_SHORT_4_4_4_4);
		CASE_STR(GL_UNSIGNED_INT_8_8_8_8);
		CASE_STR(GL_UNSIGNED_INT_10_10_10_2);
		CASE_STR(GL_UNSIGNED_SHORT_4_4_4_4_REV);
		CASE_STR(GL_UNSIGNED_INT_8_8_8_8_REV);
		CASE_STR(GL_UNSIGNED_INT_2_10_10_10_REV);
		CASE_STR(GL_UNSIGNED_NORMALIZED);
		// error codes
		CASE_STR(GL_NO_ERROR);
		CASE_STR(GL_INVALID_ENUM);
		CASE_STR(GL_INVALID_VALUE);
		CASE_STR(GL_INVALID_OPERATION);
		CASE_STR(GL_STACK_OVERFLOW);
		CASE_STR(GL_STACK_UNDERFLOW);
		CASE_STR(GL_OUT_OF_MEMORY);
		// fbo error codes
		CASE_STR(GL_FRAMEBUFFER_COMPLETE);
		CASE_STR(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT);
		CASE_STR(GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT);
		CASE_STR(GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER);
		CASE_STR(GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER);
		CASE_STR(GL_FRAMEBUFFER_UNSUPPORTED);
	}
	s = buf[ index ]; // to handle multiple invocations as function parameters
	sprintf( s, "0x%04x", define );
	index = ( index + 1 ) & 7;
	return s;
}


static void getPreferredFormatAndType( GLint format, GLint *pFormat, GLint *pType )
{
	GLint preferredFormat;
	GLint preferredType;

	if ( format == GL_RGBA16F ) {
		*pFormat = GL_RGBA;
		*pType = GL_HALF_FLOAT;
		return;
	}
	if ( format == GL_RG16F ) {
		*pFormat = GL_RG;
		*pType = GL_HALF_FLOAT;
		return;
	}
	if ( format == GL_RGB16F || format == GL_R11F_G11F_B10F ) {
		*pFormat = GL_RGB;
		*pType = GL_HALF_FLOAT;
		return;
	}

	if ( qglGetInternalformativ && gl_version >= 43 ) {
		qglGetInternalformativ( GL_TEXTURE_2D, /*GL_RGBA8*/ format, GL_TEXTURE_IMAGE_FORMAT, 1, &preferredFormat );
		if ( qglGetError() != GL_NO_ERROR ) {
			goto __fallback;
		}
		qglGetInternalformativ( GL_TEXTURE_2D, /*GL_RGBA8*/ format, GL_TEXTURE_IMAGE_TYPE, 1, &preferredType );
		if ( qglGetError() != GL_NO_ERROR ) {
			goto __fallback;
		}
		if ( preferredFormat == 0 ) // nVidia ION drivers can do that
			preferredFormat = GL_RGBA;
		if ( preferredType == GL_UNSIGNED_NORMALIZED ) { // Intel HD 530 drivers can do that as well
			if ( format == GL_RGBA16F || format == GL_RGB16F ||
				format == GL_R11F_G11F_B10F || format == GL_RG16F )
				preferredType = GL_HALF_FLOAT;
			else if ( format == GL_RGBA12 || format == GL_RGBA16 )
				preferredType = GL_UNSIGNED_SHORT;
			else
				preferredType = GL_UNSIGNED_BYTE;
		}
	} else {
__fallback:
		if ( format == GL_RGBA16F ) {
			preferredFormat = GL_RGBA;
			preferredType = GL_HALF_FLOAT;
		} else if ( format == GL_RG16F ) {
			preferredFormat = GL_RG;
			preferredType = GL_HALF_FLOAT;
		} else if ( format == GL_RGB16F || format == GL_R11F_G11F_B10F ) {
			preferredFormat = GL_RGB;
			preferredType = GL_HALF_FLOAT;
		} else if ( format == GL_RGBA12 || format == GL_RGBA16 ) {
			preferredFormat = GL_RGBA;
			preferredType = GL_UNSIGNED_SHORT;
		} else {
			preferredFormat = GL_RGBA;
			preferredType = GL_UNSIGNED_BYTE;
		}
	}

	*pFormat = preferredFormat;
	*pType = preferredType;
}


static qboolean FBO_CreateWithFormat( frameBuffer_t *fb, GLsizei width, GLsizei height,
	qboolean depthStencil, GLint internalFormat, GLint *outFormat, GLint *outType )
{
	int fboStatus;
	GLint textureFormat;
	GLint textureType;

	fb->multiSampled = qfalse;
	fb->depthStencil = 0;
	fb->internalFormat = internalFormat;

	// color texture
	qglGenTextures( 1, &fb->color );
	GL_BindTexture( 0, fb->color );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl_clamp_mode );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl_clamp_mode );

	getPreferredFormatAndType( internalFormat, &textureFormat, &textureType );

	qglTexImage2D( GL_TEXTURE_2D, 0, internalFormat, width, height, 0, textureFormat, textureType, NULL );
	// TODO: handle GL_INVALID_OPERATION in case of unsupported internalFormat/textureFormat
	
	if ( outFormat )
		*outFormat = textureFormat;
	if ( outType )
		*outType = textureType;

	qglGenFramebuffers( 1, &fb->fbo );
	FBO_Bind( GL_FRAMEBUFFER, fb->fbo );
	
	qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb->color, 0 );

	if ( depthStencil )
	{
#ifdef COMMON_DEPTH_STENCIL
		if ( !commonDepthStencil )
			commonDepthStencil = FBO_CreateDepthTextureOrBuffer( width, height );

		fb->depthStencil = commonDepthStencil;
#else
		fb->depthStencil = FBO_CreateDepthTextureOrBuffer( width, height );
#endif
#ifdef DEPTH_RENDER_BUFFER
		if ( glConfig.stencilBits == 0 )
			qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fb->depthStencil );
		else
			qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb->depthStencil );
#else
		if ( glConfig.stencilBits == 0 )
			qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fb->depthStencil, 0 );
		else
			qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, fb->depthStencil, 0 );
#endif
	}

	GL_BindTexture( 0, 0 );

	fboStatus = qglCheckFramebufferStatus( GL_FRAMEBUFFER );
	if ( fboStatus != GL_FRAMEBUFFER_COMPLETE )
	{
		ri.Printf( PRINT_ALL, "Failed to create %s (%s:%s) FBO (status %s, error %s)\n",
			glDefToStr( internalFormat ), glDefToStr( textureFormat ), glDefToStr( textureType ),
			glDefToStr( fboStatus ), glDefToStr( (int)qglGetError() ) );
		FBO_Clean( fb );
		return qfalse;
	}

	fb->width = width;
	fb->height = height;
	fb->internalFormat = internalFormat;

	qglClearColor( 0.0, 0.0, 0.0, 1.0 );
	if ( depthStencil )
		qglClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT );
	else
		qglClear( GL_COLOR_BUFFER_BIT );
#ifdef RENDERER_GLX
	GLX_CompatRecordPostClear();
#endif

	FBO_Bind( GL_FRAMEBUFFER, 0 );

	return qtrue;
}


static void FBO_ClearGLErrors( void )
{
	int i;

	for ( i = 0; i < 16; i++ ) {
		if ( qglGetError() == GL_NO_ERROR ) {
			break;
		}
	}
}

static GLsizei FBO_NormalizeRequestedSamples( int requested )
{
	GLint maxSamples = 0;

	if ( requested <= 0 ) {
		return 0;
	}
	if ( requested < 2 ) {
		requested = 2;
	}
	if ( requested > 64 ) {
		requested = 64;
	}
	if ( requested & 1 ) {
		requested--;
	}

	FBO_ClearGLErrors();
	qglGetIntegerv( GL_MAX_SAMPLES, &maxSamples );
	if ( qglGetError() == GL_NO_ERROR && maxSamples >= 2 && requested > maxSamples ) {
		requested = maxSamples;
		if ( requested & 1 ) {
			requested--;
		}
	}

	return requested >= 2 ? (GLsizei)requested : 0;
}

static GLsizei FBO_NextLowerSampleCount( GLsizei samples )
{
	if ( samples <= 2 ) {
		return 0;
	}

	if ( samples > 8 ) {
		GLsizei next = 8;
		while ( next * 2 < samples ) {
			next *= 2;
		}
		return next;
	}

	return samples - 2;
}

static qboolean FBO_TryCreateMS( frameBuffer_t *fb, int width, int height, GLsizei nSamples )
{
	int fboStatus;
	
	fb->multiSampled = qtrue;

	qglGenFramebuffers( 1, &fb->fbo );
	FBO_Bind( GL_FRAMEBUFFER, fb->fbo );

	qglGenRenderbuffers( 1, &fb->color );
	qglBindRenderbuffer( GL_RENDERBUFFER, fb->color );
	FBO_ClearGLErrors();
	qglRenderbufferStorageMultisample( GL_RENDERBUFFER, nSamples, fboInternalFormat, width, height );
	if ( qglGetError() != GL_NO_ERROR ) {
		FBO_Clean( fb );
		return qfalse;
	}
	qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, fb->color );

	qglGenRenderbuffers( 1, &fb->depthStencil );
	qglBindRenderbuffer( GL_RENDERBUFFER, fb->depthStencil );
	FBO_ClearGLErrors();
	if ( glConfig.stencilBits == 0 )
		qglRenderbufferStorageMultisample( GL_RENDERBUFFER, nSamples, GL_DEPTH_COMPONENT32, width, height );
	else
		qglRenderbufferStorageMultisample( GL_RENDERBUFFER, nSamples, GL_DEPTH24_STENCIL8, width, height );

	if ( (int)qglGetError() != GL_NO_ERROR )
	{
		FBO_Clean( fb );
		return qfalse;
	}

	if ( glConfig.stencilBits == 0 )
		qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fb->depthStencil );
	else
		qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb->depthStencil );

	fboStatus = qglCheckFramebufferStatus( GL_FRAMEBUFFER );
	if ( fboStatus != GL_FRAMEBUFFER_COMPLETE )
	{
		ri.Printf( PRINT_WARNING, "Failed to create MS FBO (status %s, error %s)\n", glDefToStr( fboStatus ), glDefToStr( (int)qglGetError() ) );
		FBO_Clean( fb );
		return qfalse;
	}

	fb->width = width;
	fb->height = height;

	qglClearColor( 0.0, 0.0, 0.0, 1.0 );
	qglClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT );
#ifdef RENDERER_GLX
	GLX_CompatRecordPostClear();
#endif

	FBO_Bind( GL_FRAMEBUFFER, 0 );

	return qtrue;
}


static qboolean FBO_CreateBloom( void )
{
	int width = glConfig.vidWidth;
	int height = glConfig.vidHeight;
	int i;

	fboBloomPasses = 0;
	fboBloomInternalFormat = 0;
	fboBloomTextureFormat = 0;
	fboBloomTextureType = 0;
	fboBloomFormatMode = FBO_HdrBloomFormatMode();

	if ( FBO_HdrSceneLinearMode() && fboBloomFormatMode == GLX_HDR_BLOOM_FORMAT_RG16F )
	{
		ri.Printf( PRINT_WARNING,
			"...r_hdrBloomFormat %s is reserved for positive RG intermediates; RGB bloom will use the conservative fallback\n",
			FBO_HdrBloomFormatModeName( fboBloomFormatMode ) );
	}

	if ( glConfig.numTextureUnits < r_bloom_passes->integer )
	{
		ri.Printf( PRINT_WARNING, "...not enough texture units (%i) for %i-pass bloom\n",
			glConfig.numTextureUnits, r_bloom_passes->integer );
#ifdef RENDERER_GLX
		GLX_RecordBloomCreateState( GLX_BLOOM_CREATE_TEXTURE_UNITS );
#endif
		return qfalse;
	}

	for ( i = 0; i < r_bloom_passes->integer; i++ )
	{
		// we may need depth/stencil buffers for first bloom buffer in \r_bloom 2 mode
		if ( !FBO_Create( &frameBuffers[ i*2 + BLOOM_BASE + 0 ], width, height, i == 0 ? qtrue : qfalse, NULL, NULL ) ||
			!FBO_Create( &frameBuffers[ i*2 + BLOOM_BASE + 1 ], width, height, qfalse, NULL, NULL ) ) {
			FBO_CleanBloom();
#ifdef RENDERER_GLX
			GLX_RecordBloomCreateState( GLX_BLOOM_CREATE_FBO );
#endif
			return qfalse;
		}
		width = width / 2;
		height = height / 2;
		fboBloomPasses++;
		if ( width < 2 || height < 2 )
			break;
	}

	ri.Printf( PRINT_ALL, "...%i bloom passes\n", fboBloomPasses );
	if ( fboBloomInternalFormat )
	{
		ri.Printf( PRINT_ALL, "...bloom intermediate policy %s, format %s (%s:%s)\n",
			FBO_HdrBloomFormatModeName( fboBloomFormatMode ),
			glDefToStr( fboBloomInternalFormat ),
			glDefToStr( fboBloomTextureFormat ),
			glDefToStr( fboBloomTextureType ) );
	}
#ifdef RENDERER_GLX
	GLX_RecordBloomCreateState( GLX_BLOOM_CREATE_SUCCESS );
#endif

	return qtrue;
}


static qboolean FBO_CreateMS( frameBuffer_t *fb, int width, int height )
{
	const GLsizei requestedSamples = FBO_NormalizeRequestedSamples( r_ext_multisample->integer );
	GLsizei nSamples = requestedSamples;

	if ( nSamples <= 0 || !qglRenderbufferStorageMultisample )
	{
		return qfalse;
	}

	while ( nSamples >= 2 ) {
		if ( FBO_TryCreateMS( fb, width, height, nSamples ) ) {
			if ( nSamples == requestedSamples ) {
				ri.Printf( PRINT_ALL, "...using %ix MSAA\n", nSamples );
			} else {
				ri.Printf( PRINT_ALL, "...using %ix MSAA (requested %ix)\n",
					nSamples, requestedSamples );
			}
			return qtrue;
		}

		ri.Printf( PRINT_ALL, "...%ix MSAA is not available\n", nSamples );
		nSamples = FBO_NextLowerSampleCount( nSamples );
	}

	return qfalse;
}


static qboolean FBO_Create( frameBuffer_t *fb, GLsizei width, GLsizei height,
	qboolean depthStencil, GLint *outFormat, GLint *outType )
{
	GLint formats[4];
	int formatCount;
	int i;

	FBO_FormatCandidatesForBuffer( fb, formats, &formatCount );

	for ( i = 0; i < formatCount; i++ )
	{
		if ( FBO_CreateWithFormat( fb, width, height, depthStencil, formats[i], outFormat, outType ) )
		{
			if ( FBO_FrameBufferIsBloom( fb ) ) {
				fboBloomInternalFormat = fb->internalFormat;
				getPreferredFormatAndType( fboBloomInternalFormat,
					&fboBloomTextureFormat, &fboBloomTextureType );
				fboBloomFormatMode = FBO_HdrBloomFormatMode();
			}
			return qtrue;
		}
	}

	return qfalse;
}


GLuint FBO_ScreenTexture( void )
{
	return frameBuffers[ 2 ].color;
}

GLuint FBO_LiquidScreenTexture( void )
{
	return liquidScreenBuffer.color;
}

qboolean FBO_MultisamplingEnabled( void )
{
	return ( fboEnabled && frameBufferMultiSampling ) ? qtrue : qfalse;
}

qboolean FBO_DepthFadeAvailable( void )
{
	return ( fboEnabled && depthFadeTexture && r_depthFade && r_depthFade->integer ) ? qtrue : qfalse;
}

static qboolean FBO_DepthTextureAvailable( void )
{
	return ( fboEnabled && depthFadeTexture ) ? qtrue : qfalse;
}

static qboolean FBO_DepthTextureReady( void )
{
	return ( FBO_DepthTextureAvailable() && depthFadeCopied ) ? qtrue : qfalse;
}

qboolean FBO_DepthFadeReady( void )
{
	return ( FBO_DepthFadeAvailable() && depthFadeCopied ) ? qtrue : qfalse;
}

qboolean FBO_DlightShadowsAvailable( void )
{
#ifdef USE_PMLIGHT
	return ( FBO_DlightShadowAtlasAvailable() &&
		dlightShadowProgramsCompiled &&
		r_dlightMode && r_dlightMode->integer &&
		( ( r_dlightShadows && r_dlightShadows->integer ) ||
			( r_shadowCorrectness && r_shadowCorrectness->integer ) ) &&
		glConfig.numTextureUnits > 2 ) ? qtrue : qfalse;
#else
	return qfalse;
#endif
}

qboolean FBO_DlightShadowAtlasAvailable( void )
{
	return ( fboEnabled && dlightShadowAtlasTexture && dlightShadowAtlasFbo &&
		dlightShadowAtlasLayout.faceSize > 0 ) ? qtrue : qfalse;
}

qboolean FBO_BeginDlightShadowAtlas( void )
{
	dlightShadowAtlasRendered = qfalse;

	if ( !FBO_DlightShadowAtlasAvailable() ) {
		return qfalse;
	}

	FBO_Bind( GL_FRAMEBUFFER, dlightShadowAtlasFbo );
	GLX_CompatRecordDlightState( GLX_DLIGHT_STATE_SHADOW_FBO_BIND );
	qglDrawBuffer( GL_NONE );
	qglReadBuffer( GL_NONE );
	qglViewport( 0, 0, dlightShadowAtlasLayout.width, dlightShadowAtlasLayout.height );
	qglScissor( 0, 0, dlightShadowAtlasLayout.width, dlightShadowAtlasLayout.height );
	GL_State( GLS_DEFAULT );
	GLX_CompatRecordDlightState( GLX_DLIGHT_STATE_GL_STATE );

	return qtrue;
}

void FBO_EndDlightShadowAtlas( void )
{
	if ( fboEnabled ) {
		FBO_BindMain();
		GLX_CompatRecordDlightState( GLX_DLIGHT_STATE_SHADOW_FBO_RESTORE );
		qglDrawBuffer( GL_COLOR_ATTACHMENT0 );
		qglReadBuffer( GL_COLOR_ATTACHMENT0 );
	}
	dlightShadowAtlasRendered = qtrue;
}

int FBO_DlightShadowAtlasWidth( void )
{
	return dlightShadowAtlasLayout.width;
}

int FBO_DlightShadowAtlasHeight( void )
{
	return dlightShadowAtlasLayout.height;
}

unsigned int FBO_DlightShadowAtlasGeneration( void )
{
	return dlightShadowAtlasGeneration;
}

qboolean FBO_DlightShadowsReady( void )
{
	return ( FBO_DlightShadowsAvailable() && dlightShadowAtlasRendered ) ? qtrue : qfalse;
}

qboolean FBO_SpotShadowAtlasAvailable( void )
{
	return ( qglGenFramebuffers && spotShadowAtlasTexture && spotShadowAtlasFbo &&
		spotShadowAtlasLayout.tileSize > 0 ) ? qtrue : qfalse;
}

qboolean FBO_SpotShadowAtlasReady( void )
{
	return ( FBO_SpotShadowAtlasAvailable() && spotShadowAtlasRendered ) ? qtrue : qfalse;
}

qboolean FBO_BeginSpotShadowAtlas( void )
{
	spotShadowAtlasRendered = qfalse;

	if ( !FBO_SpotShadowAtlasAvailable() ) {
		return qfalse;
	}

	FBO_Bind( GL_FRAMEBUFFER, spotShadowAtlasFbo );
	qglDrawBuffer( GL_NONE );
	qglReadBuffer( GL_NONE );
	qglViewport( 0, 0, spotShadowAtlasLayout.width, spotShadowAtlasLayout.height );
	qglScissor( 0, 0, spotShadowAtlasLayout.width, spotShadowAtlasLayout.height );
	GL_State( GLS_DEFAULT );

	return qtrue;
}

void FBO_EndSpotShadowAtlas( void )
{
	if ( fboEnabled ) {
		FBO_BindMain();
		qglDrawBuffer( GL_COLOR_ATTACHMENT0 );
		qglReadBuffer( GL_COLOR_ATTACHMENT0 );
	} else {
		FBO_Bind( GL_FRAMEBUFFER, 0 );
		qglDrawBuffer( GL_BACK );
		qglReadBuffer( GL_BACK );
	}
	spotShadowAtlasRendered = qtrue;
}

void FBO_MarkSpotShadowAtlasRendered( void )
{
	if ( FBO_SpotShadowAtlasAvailable() ) {
		spotShadowAtlasRendered = qtrue;
	}
}

int FBO_SpotShadowAtlasWidth( void )
{
	return spotShadowAtlasLayout.width;
}

int FBO_SpotShadowAtlasHeight( void )
{
	return spotShadowAtlasLayout.height;
}

int FBO_SpotShadowTileSize( void )
{
	return spotShadowAtlasLayout.tileSize;
}

unsigned int FBO_SpotShadowAtlasGeneration( void )
{
	return spotShadowAtlasGeneration;
}

qboolean FBO_CSMShadowsAvailable( void )
{
#ifdef USE_PMLIGHT
	if ( !csmShadowProgramsCompiled ||
		( r_shadowCorrectness && r_shadowCorrectness->integer ) ||
		!r_csmShadows || !r_csmShadows->integer ||
		glConfig.numTextureUnits <= 1 || !qglGenFramebuffers ) {
		return qfalse;
	}
	if ( !FBO_CSMShadowAtlasAvailable() ) {
		FBO_CreateCSMShadowAtlas();
	}
	return FBO_CSMShadowAtlasAvailable();
#else
	return qfalse;
#endif
}

qboolean FBO_CSMShadowAtlasAvailable( void )
{
	return ( qglGenFramebuffers && csmShadowAtlasTexture && csmShadowAtlasFbo &&
		csmShadowAtlasLayout.cascadeSize > 0 ) ? qtrue : qfalse;
}

qboolean FBO_BeginCSMShadowAtlas( void )
{
	csmShadowAtlasRendered = qfalse;

	if ( !FBO_CSMShadowAtlasAvailable() ) {
		return qfalse;
	}

	FBO_Bind( GL_FRAMEBUFFER, csmShadowAtlasFbo );
	qglDrawBuffer( GL_NONE );
	qglReadBuffer( GL_NONE );
	qglViewport( 0, 0, csmShadowAtlasLayout.width, csmShadowAtlasLayout.height );
	qglScissor( 0, 0, csmShadowAtlasLayout.width, csmShadowAtlasLayout.height );
	GL_State( GLS_DEFAULT );
	qglClear( GL_DEPTH_BUFFER_BIT );

	return qtrue;
}

void FBO_EndCSMShadowAtlas( void )
{
	if ( fboEnabled ) {
		FBO_BindMain();
		qglDrawBuffer( GL_COLOR_ATTACHMENT0 );
		qglReadBuffer( GL_COLOR_ATTACHMENT0 );
	} else {
		FBO_Bind( GL_FRAMEBUFFER, 0 );
		qglDrawBuffer( GL_BACK );
		qglReadBuffer( GL_BACK );
	}
	csmShadowAtlasRendered = qtrue;
}

void FBO_MarkCSMShadowAtlasRendered( void )
{
	if ( FBO_CSMShadowAtlasAvailable() ) {
		csmShadowAtlasRendered = qtrue;
	}
}

int FBO_CSMShadowAtlasWidth( void )
{
	return csmShadowAtlasLayout.width;
}

int FBO_CSMShadowAtlasHeight( void )
{
	return csmShadowAtlasLayout.height;
}

int FBO_CSMShadowCascadeSize( void )
{
	return csmShadowAtlasLayout.cascadeSize;
}

unsigned int FBO_CSMShadowAtlasGeneration( void )
{
	return csmShadowAtlasGeneration;
}

qboolean FBO_CSMShadowsReady( void )
{
	return ( FBO_CSMShadowsAvailable() && csmShadowAtlasRendered ) ? qtrue : qfalse;
}

void FBO_ResetDepthFade( void )
{
	depthFadeCopied = qfalse;
}

static void FBO_CopyDepthTexture( void )
{
	if ( !FBO_DepthTextureAvailable() ) {
		return;
	}

	if ( depthFadeTextureShared ) {
		if ( frameBufferMultiSampling ) {
			FBO_BlitMS( qtrue );
		}
		depthFadeCopied = qtrue;
		FBO_BindMain();
		return;
	}

	FBO_Bind( GL_READ_FRAMEBUFFER, frameBuffers[ 0 ].fbo );
	GL_BindTexture( 1, depthFadeTexture );
	qglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, frameBuffers[ 0 ].width, frameBuffers[ 0 ].height );
	GL_SelectTexture( 0 );
	depthFadeCopied = qtrue;

	FBO_BindMain();
}

void FBO_CopyDepthFade( void )
{
	if ( !FBO_DepthFadeAvailable() ) {
		return;
	}

	FBO_CopyDepthTexture();
}


/* Enhanced liquids reuse the depth-fade texture for waterline rejection and
 * copy it at the liquid boundary regardless of the r_depthFade toggle. */
void FBO_CopyLiquidDepth( void )
{
	if ( depthFadeCopied ) {
		return;
	}
	FBO_CopyDepthTexture();
}

qboolean FBO_LiquidDepthReady( void )
{
	return ( FBO_DepthTextureAvailable() && depthFadeCopied ) ? qtrue : qfalse;
}

void FBO_BindLiquidDepthTexture( int texUnit )
{
	GL_BindTexture( texUnit, FBO_LiquidDepthReady() ? depthFadeTexture : 0 );
}

void FBO_BindDepthFadeTexture( int texUnit )
{
	GL_BindTexture( texUnit, FBO_DepthFadeReady() ? depthFadeTexture : 0 );
}

void FBO_BindDlightShadowTexture( int texUnit )
{
	qboolean ready = FBO_DlightShadowsReady();

	GL_BindTexture( texUnit, ready ? dlightShadowAtlasTexture : 0 );
	GLX_CompatRecordDlightState( ready ?
		GLX_DLIGHT_STATE_SHADOW_TEXTURE_BIND :
		GLX_DLIGHT_STATE_SHADOW_TEXTURE_FALLBACK_BIND );
}

void FBO_BindSpotShadowTexture( int texUnit )
{
	// the cached bind is safe because every atlas delete path runs
	// GL_InvalidateDeletedTexture(), so a recycled name cannot be skipped
	GL_BindTexture( texUnit, FBO_SpotShadowAtlasReady() ? spotShadowAtlasTexture : 0 );
}

void FBO_BindCSMShadowTexture( int texUnit )
{
	GL_BindTexture( texUnit, FBO_CSMShadowsReady() ? csmShadowAtlasTexture : 0 );
}

void FBO_DrawWorldCelOutline( void )
{
	qboolean restore3D;
	float width;
	float alpha;
	float threshold;

	if ( !R_CelShadingWorldActive() ||
		( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) ||
		!programCompiled || !FBO_DepthTextureAvailable() ) {
		return;
	}

	width = r_celShadingWorldWidth ? Com_Clamp( 1.0f, 8.0f, r_celShadingWorldWidth->value ) : 2.0f;
	alpha = r_celShadingWorldAlpha ? Com_Clamp( 0.0f, 1.0f, r_celShadingWorldAlpha->value ) : 1.0f;
	threshold = r_celShadingWorldDepthThreshold ? Com_Clamp( 0.0001f, 0.02f, r_celShadingWorldDepthThreshold->value ) : 0.0015f;
	if ( alpha <= 0.0f ) {
		return;
	}

	if ( !FBO_DepthTextureReady() ) {
		FBO_CopyDepthTexture();
	}
	if ( !FBO_DepthTextureReady() ) {
		return;
	}

	restore3D = !backEnd.projection2D;
	if ( restore3D ) {
		qglMatrixMode( GL_PROJECTION );
		qglPushMatrix();
		qglMatrixMode( GL_MODELVIEW );
		qglPushMatrix();
	}

	RB_SetGL2D();
	FBO_BindMain();
	GL_Cull( CT_TWO_SIDED );
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	GL_BindTexture( 0, depthFadeTexture );
	ARB_ProgramEnable( DUMMY_VERTEX, WORLD_CEL_FRAGMENT );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 0,
		width / (float)glConfig.vidWidth, width / (float)glConfig.vidHeight,
		threshold, alpha );
	RenderQuad( glConfig.vidWidth, glConfig.vidHeight );
	ARB_ProgramDisable();

	if ( restore3D ) {
		qglMatrixMode( GL_PROJECTION );
		qglPopMatrix();
		qglMatrixMode( GL_MODELVIEW );
		qglPopMatrix();
		backEnd.projection2D = qfalse;
	}
}


void FBO_DrawGlobalFog( void )
{
	const globalFog_t *fog;
	const frameBuffer_t *source;
	const frameBuffer_t *destination;
	qboolean restore3D;
	float opacity;
	float zNear;
	float zFar;
	int sourceIndex;
	int destinationIndex;

	if ( r_globalFog && r_globalFog->integer && tr.world && tr.world->globalFog.loaded &&
		!globalFogCompositorStateLogged ) {
		ri.Printf( PRINT_DEVELOPER,
			"Global fog: OpenGL compositor state fbo %i depth %i program %i rdflags 0x%x\n",
			fboEnabled, FBO_DepthTextureAvailable(), globalFogProgramCompiled,
			backEnd.refdef.rdflags );
		globalFogCompositorStateLogged = qtrue;
	}

	if ( !r_globalFog || !r_globalFog->integer || !r_globalFogStrength ||
		!tr.world || !tr.world->globalFog.loaded ||
		( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) ||
		!programCompiled || !globalFogProgramCompiled ||
		!FBO_DepthTextureAvailable() ) {
		return;
	}

	fog = &tr.world->globalFog;
	opacity = Com_Clamp( 0.0f, 1.0f, fog->opacity * r_globalFogStrength->value );
	zNear = r_znear ? r_znear->value : 4.0f;
	zFar = backEnd.viewParms.zFar;
	if ( opacity <= 0.0f || zNear <= 0.0f || zFar <= zNear ) {
		return;
	}

	if ( !FBO_DepthTextureReady() ) {
		FBO_CopyDepthTexture();
	}
	if ( !FBO_DepthTextureReady() ) {
		return;
	}
	if ( !globalFogCompositorActiveLogged ) {
		ri.Printf( PRINT_DEVELOPER,
			"Global fog: OpenGL compositor active (%s, density %.6f, effective opacity %.3f)\n",
			fog->mode == GLOBAL_FOG_EXP ? "exp" :
				( fog->mode == GLOBAL_FOG_EXP2 ? "exp2" : "linear" ),
			fog->density, opacity );
		globalFogCompositorActiveLogged = qtrue;
	}

	/* A multisample color target cannot be sampled directly.  Preserve depth,
	 * resolve scene color, and use the ordinary ping-pong pair so this pass
	 * never samples its own color attachment. */
	if ( frameBufferMultiSampling ) {
		FBO_BlitMS( qfalse );
		blitMSfbo = qfalse;
	}

	sourceIndex = fboReadIndex;
	if ( sourceIndex < 0 || sourceIndex > 1 ) {
		return;
	}
	destinationIndex = sourceIndex == 0 ? 1 : 0;
	source = &frameBuffers[sourceIndex];
	destination = &frameBuffers[destinationIndex];
	if ( !source->color || !destination->fbo ) {
		return;
	}

	restore3D = !backEnd.projection2D;
	if ( restore3D ) {
		qglMatrixMode( GL_PROJECTION );
		qglPushMatrix();
		qglMatrixMode( GL_MODELVIEW );
		qglPushMatrix();
	}

	RB_SetGL2D();
	FBO_Bind( GL_FRAMEBUFFER, destination->fbo );
	GL_Cull( CT_TWO_SIDED );
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	qglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	GL_BindTexture( 0, source->color );
	GL_BindTexture( 1, depthFadeTexture );
	GL_SelectTexture( 0 );
	ARB_ProgramEnable( DUMMY_VERTEX, GLOBAL_FOG_FRAGMENT );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 0,
		fog->color[0], fog->color[1], fog->color[2], opacity );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 1,
		fog->start, fog->end, fog->density, (float)fog->mode );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 2,
		zNear, zFar, zFar - zNear, fog->sky ? 1.0f : 0.0f );
	RenderQuad( glConfig.vidWidth, glConfig.vidHeight );
	ARB_ProgramDisable();

	/* Bloom and motion blur consume the primary resolved scene buffer.  Copy
	 * the ping-pong result back instead of bypassing those later passes. */
	if ( destinationIndex != 0 ) {
		FBO_Bind( GL_READ_FRAMEBUFFER, destination->fbo );
		FBO_Bind( GL_DRAW_FRAMEBUFFER, frameBuffers[0].fbo );
		qglBlitFramebuffer( 0, 0, destination->width, destination->height,
			0, 0, frameBuffers[0].width, frameBuffers[0].height,
			GL_COLOR_BUFFER_BIT, GL_NEAREST );
	}
	FBO_Bind( GL_FRAMEBUFFER, frameBuffers[0].fbo );
	fboReadIndex = 0;

	if ( restore3D ) {
		qglMatrixMode( GL_PROJECTION );
		qglPopMatrix();
		qglMatrixMode( GL_MODELVIEW );
		qglPopMatrix();
		backEnd.projection2D = qfalse;
	}
}


static void FBO_Bind( GLuint target, GLuint buffer )
{
#if 1
	static GLuint draw_buffer = (GLuint)-1;
	static GLuint read_buffer = (GLuint)-1;
	if ( target == GL_FRAMEBUFFER ) {
		if ( draw_buffer != buffer || read_buffer != buffer ) {
			qglBindFramebuffer( GL_FRAMEBUFFER, buffer );
#ifdef RENDERER_GLX
			GLX_CompatRecordFboBind();
#endif
		}
		draw_buffer = buffer;
		read_buffer = buffer;
	} else {
		if ( target == GL_READ_FRAMEBUFFER ) {
			if ( read_buffer != buffer ) {
				qglBindFramebuffer( GL_READ_FRAMEBUFFER, buffer );
#ifdef RENDERER_GLX
				GLX_CompatRecordFboBind();
#endif
			}
			read_buffer = buffer;
		} else {
			if ( draw_buffer != buffer ) {
				qglBindFramebuffer( GL_DRAW_FRAMEBUFFER, buffer );
#ifdef RENDERER_GLX
				GLX_CompatRecordFboBind();
#endif
			}
			draw_buffer = buffer;
		}
	}
#else
	qglBindFramebuffer( target, buffer );
#ifdef RENDERER_GLX
	GLX_CompatRecordFboBind();
#endif
#endif
	FBO_SetFramebufferSrgb( qfalse );
}


void FBO_BindMain( void )
{
	if ( fboEnabled )
	{
		const frameBuffer_t *fb;
		if ( frameBufferMultiSampling )
		{
			blitMSfbo = qtrue;
			fb = &frameBufferMS;
		}
		else
		{
			blitMSfbo = qfalse;
			fb = &frameBuffers[ 0 ];
		}
		FBO_Bind( GL_FRAMEBUFFER, fb->fbo );
		fboReadIndex = 0;
	}
}

static void FBO_UpdateTonemapExposure( void )
{
	float manualExposure;

	if ( fboExposureFrame == tr.frameCount ) {
		return;
	}

	manualExposure = FBO_TonemapExposureCvar();
	fboExposureFrame = tr.frameCount;
	fboEffectiveTonemapExposure = manualExposure;

#ifdef RENDERER_GLX
	if ( FBO_HdrSceneLinearColorMode() )
	{
		int width = 0;
		int height = 0;
		qboolean sampled = qfalse;

		if ( GLX_CompatAutoExposureNeedsSamples( &width, &height ) )
		{
			if ( width > 128 ) {
				width = 128;
			}
			if ( height > 128 ) {
				height = 128;
			}
			if ( width > 0 && height > 0 && frameBuffers[ 0 ].color &&
				frameBuffers[ 3 ].fbo && width <= frameBuffers[ 3 ].width &&
				height <= frameBuffers[ 3 ].height )
			{
				GLenum error;

				ARB_ProgramDisable();
				FBO_Bind( GL_FRAMEBUFFER, frameBuffers[ 3 ].fbo );
				GL_BindTexture( 0, frameBuffers[ 0 ].color );
				GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
				qglViewport( 0, 0, width, height );
				qglScissor( 0, 0, width, height );
				RenderQuad( glConfig.vidWidth, glConfig.vidHeight );
				qglReadPixels( 0, 0, width, height, GL_RGBA, GL_FLOAT,
					fboExposureSamplePixels );
				error = qglGetError();
				sampled = ( error == GL_NO_ERROR ) ? qtrue : qfalse;
				qglViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
				qglScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
			}
		}

		fboEffectiveTonemapExposure = GLX_CompatUpdateAutoExposure(
			manualExposure, sampled ? fboExposureSamplePixels : NULL,
			sampled ? width : 0, sampled ? height : 0 );
	}
#endif
}


static void FBO_BlitToBackBuffer( int index )
{
	const frameBuffer_t *src = &frameBuffers[ index ];
#ifdef RENDERER_GLX
	GLX_CompatRecordFboBlit( GLX_FBO_BLIT_BACKBUFFER, qfalse,
		src->width, src->height, blitX1 - blitX0, blitY1 - blitY0 );
	GLX_CompatBeginGpuPassTimer( GLX_GPU_PASS_FBO_BLIT );
#endif

	FBO_Bind( GL_READ_FRAMEBUFFER, src->fbo );
	FBO_Bind( GL_DRAW_FRAMEBUFFER, 0 );
	//qglReadBuffer( GL_COLOR_ATTACHMENT0 );
	qglDrawBuffer( GL_BACK );

	if ( windowAdjusted )
	{
		if ( blitClear > 0 )
		{
			blitClear--;
			qglClearColor( 0.0, 0.0, 0.0, 1.0 );
			qglClear( GL_COLOR_BUFFER_BIT );
#ifdef RENDERER_GLX
			GLX_CompatRecordPostClear();
#endif
		}
		qglViewport( blitX0, blitY0, blitX1 - blitX0, blitY1 - blitY0 );
		qglScissor( blitX0, blitY0, blitX1 - blitX0, blitY1 - blitY0 );
	}

	qglBlitFramebuffer( 0, 0, src->width, src->height, blitX0, blitY0, blitX1, blitY1, GL_COLOR_BUFFER_BIT, blitFilter );
	fboReadIndex = index;
#ifdef RENDERER_GLX
	GLX_CompatEndGpuPassTimer( GLX_GPU_PASS_FBO_BLIT );
#endif
}


void FBO_BlitSS( void )
{
	const frameBuffer_t *src = &frameBuffers[ fboReadIndex ];
	const frameBuffer_t *dst = &frameBuffers[ 4 ];
#ifdef RENDERER_GLX
	GLX_CompatRecordFboBlit( GLX_FBO_BLIT_SS, qfalse,
		src->width, src->height, dst->width, dst->height );
	GLX_CompatBeginGpuPassTimer( GLX_GPU_PASS_FBO_BLIT );
#endif

	FBO_Bind( GL_DRAW_FRAMEBUFFER, dst->fbo );
	
	qglBlitFramebuffer( 0, 0, src->width, src->height, 0, 0, dst->width, dst->height, GL_COLOR_BUFFER_BIT, GL_LINEAR );

	FBO_Bind( GL_READ_FRAMEBUFFER, dst->fbo );
#ifdef RENDERER_GLX
	GLX_CompatEndGpuPassTimer( GLX_GPU_PASS_FBO_BLIT );
#endif
}


void FBO_BlitMS( qboolean depthOnly )
{
	//if ( blitMSfbo )
	//{
	const int w = glConfig.vidWidth;
	const int h = glConfig.vidHeight;

	const frameBuffer_t *r = &frameBufferMS;
	const frameBuffer_t *d = &frameBuffers[ 0 ];
#ifdef RENDERER_GLX
	GLX_CompatRecordFboBlit( GLX_FBO_BLIT_MS, depthOnly,
		r->width, r->height, d->width, d->height );
	GLX_CompatBeginGpuPassTimer( GLX_GPU_PASS_FBO_BLIT );
#endif

	fboReadIndex = 0;

	FBO_Bind( GL_READ_FRAMEBUFFER, r->fbo );
	FBO_Bind( GL_DRAW_FRAMEBUFFER, d->fbo );

	if ( depthOnly )
	{
		qglBlitFramebuffer( 0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST );
		if ( depthFadeTextureShared ) {
			depthFadeCopied = qtrue;
		}
		FBO_Bind( GL_READ_FRAMEBUFFER, d->fbo );
#ifdef RENDERER_GLX
		GLX_CompatEndGpuPassTimer( GLX_GPU_PASS_FBO_BLIT );
#endif
		return;
	}

	qglBlitFramebuffer( 0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST );
	// bind all further reads to main buffer
	FBO_Bind( GL_READ_FRAMEBUFFER, d->fbo );
#ifdef RENDERER_GLX
	GLX_CompatEndGpuPassTimer( GLX_GPU_PASS_FBO_BLIT );
#endif
}


static void FBO_Blur( const frameBuffer_t *fb1, const frameBuffer_t *fb2,  const frameBuffer_t *fb3 )
{
	const int w = glConfig.vidWidth;
	const int h = glConfig.vidHeight;
#ifdef RENDERER_GLX
	GLX_CompatBeginGpuPassTimer( GLX_GPU_PASS_BLOOM_BLUR );
#endif

	qglViewport( 0, 0, fb1->width, fb1->height );

	// apply horizontal blur - render from FBO1 to FBO2
	FBO_Bind( GL_DRAW_FRAMEBUFFER, fb2->fbo );
	GL_BindTexture( 0, fb1->color );
	ARB_ProgramEnable( DUMMY_VERTEX, BLUR_FRAGMENT );
	ARB_BlurParams( fb1->width, fb1->height, fboBloomFilterSize, qtrue );
	RenderQuad( w, h );

	// apply vectical blur - render from FBO2 to FBO3
	FBO_Bind( GL_DRAW_FRAMEBUFFER, fb3->fbo );
	GL_BindTexture( 0, fb2->color );
	ARB_BlurParams( fb1->width, fb1->height, fboBloomFilterSize, qfalse );
	RenderQuad( w, h );
#ifdef RENDERER_GLX
	GLX_CompatEndGpuPassTimer( GLX_GPU_PASS_BLOOM_BLUR );
#endif
}


static void FBO_Blur2( const frameBuffer_t *fb1, const frameBuffer_t *fb2,  const frameBuffer_t *fb3 )
{
	const int w = glConfig.vidWidth;
	const int h = glConfig.vidHeight;
#ifdef RENDERER_GLX
	GLX_CompatBeginGpuPassTimer( GLX_GPU_PASS_BLOOM_BLUR );
#endif

	qglViewport( 0, 0, fb1->width, fb1->height );

	// apply horizontal blur - render from FBO1 to FBO2
	FBO_Bind( GL_DRAW_FRAMEBUFFER, fb2->fbo );
	GL_BindTexture( 0, fb1->color );
	ARB_ProgramEnable( DUMMY_VERTEX, BLUR2_FRAGMENT );
	ARB_BlurParams( fb1->width, fb1->height, 6, qtrue );
	RenderQuad( w, h );

	// apply vectical blur - render from FBO2 to FBO3
	FBO_Bind( GL_DRAW_FRAMEBUFFER, fb3->fbo );
	GL_BindTexture( 0, fb2->color );
	ARB_BlurParams( fb1->width, fb1->height, 6, qfalse );
	RenderQuad( w, h );
#ifdef RENDERER_GLX
	GLX_CompatEndGpuPassTimer( GLX_GPU_PASS_BLOOM_BLUR );
#endif
}


static qboolean FBO_EnsureMenuDepthOfFieldBuffers( void )
{
	const int width = MAX( 1, glConfig.vidWidth / 2 );
	const int height = MAX( 1, glConfig.vidHeight / 2 );

	if ( menuDofBuffers[0].fbo && menuDofBuffers[1].fbo &&
		menuDofBuffers[0].width == width && menuDofBuffers[0].height == height &&
		menuDofBuffers[1].width == width && menuDofBuffers[1].height == height ) {
		return qtrue;
	}

	FBO_Clean( &menuDofBuffers[0] );
	FBO_Clean( &menuDofBuffers[1] );

	if ( !FBO_Create( &menuDofBuffers[0], width, height, qfalse, NULL, NULL ) ||
		!FBO_Create( &menuDofBuffers[1], width, height, qfalse, NULL, NULL ) ) {
		FBO_Clean( &menuDofBuffers[0] );
		FBO_Clean( &menuDofBuffers[1] );
		return qfalse;
	}

	return qtrue;
}


void FBO_MenuDepthOfField( float amount )
{
	frameBuffer_t *source;
	int sourceIndex;
	int pass;

	amount = Com_Clamp( 0.0f, 1.0f, amount );
	if ( amount <= 0.0f || !fboEnabled || !programCompiled || !backEnd.doneSurfaces ||
		backEnd.framePostProcessed || ri.CL_IsMinimized() ) {
		return;
	}

	if ( blitMSfbo ) {
		FBO_BlitMS( qfalse );
		blitMSfbo = qfalse;
	}

	sourceIndex = fboReadIndex;
	if ( sourceIndex < 0 || sourceIndex >= FBO_COUNT ) {
		return;
	}

	source = &frameBuffers[ sourceIndex ];
	if ( !source->fbo || source->multiSampled || !source->color ) {
		return;
	}
	if ( !FBO_EnsureMenuDepthOfFieldBuffers() ) {
		return;
	}

	if ( !backEnd.projection2D ) {
		RB_SetGL2D();
	}

	FBO_Bind( GL_READ_FRAMEBUFFER, source->fbo );
	FBO_Bind( GL_DRAW_FRAMEBUFFER, menuDofBuffers[0].fbo );
	qglBlitFramebuffer( 0, 0, source->width, source->height,
		0, 0, menuDofBuffers[0].width, menuDofBuffers[0].height,
		GL_COLOR_BUFFER_BIT, GL_LINEAR );

	qglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	for ( pass = 0; pass < 2; ++pass ) {
		FBO_Blur2( &menuDofBuffers[0], &menuDofBuffers[1], &menuDofBuffers[0] );
	}

	ARB_ProgramDisable();
	FBO_Bind( GL_FRAMEBUFFER, source->fbo );
	GL_BindTexture( 0, menuDofBuffers[0].color );
	GL_TexEnv( GL_MODULATE );
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_SRC_ALPHA |
		GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	qglViewport( 0, 0, source->width, source->height );
	qglScissor( 0, 0, source->width, source->height );
	qglColor4f( 1.0f, 1.0f, 1.0f, amount );
	RenderQuad( source->width, source->height );
	qglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );

	fboReadIndex = sourceIndex;
	RB_SetGL2D();
}


static qboolean FBO_CopyScreenInternal( qboolean liquidSnapshot )
{
	const frameBuffer_t *dst;
	const frameBuffer_t *src;
	int yCrop;

	if ( !fboEnabled || !frameBuffers[0].fbo ||
		( liquidSnapshot ? !liquidScreenBuffer.fbo : !frameBuffers[2].fbo ) ) {
		return qfalse;
	}
#ifdef RENDERER_GLX
	GLX_CompatRecordFboCopyScreen( backEnd.viewParms.viewportWidth,
		backEnd.viewParms.viewportHeight );
	GLX_CompatBeginGpuPassTimer( GLX_GPU_PASS_COPY_SCREEN );
#endif

	qglViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	qglScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );

	// resolve multisample buffer first
	if ( blitMSfbo )
	{
		src = &frameBufferMS;
		dst = &frameBuffers[ 0 ];
#ifdef RENDERER_GLX
		GLX_CompatRecordFboBlit( GLX_FBO_BLIT_COPY_SCREEN, qfalse,
			src->width, src->height, dst->width, dst->height );
#endif
		FBO_Bind( GL_READ_FRAMEBUFFER, src->fbo );
		FBO_Bind( GL_DRAW_FRAMEBUFFER, dst->fbo );
		qglBlitFramebuffer( 0, 0, src->width, src->height, 0, 0, dst->width, dst->height, GL_COLOR_BUFFER_BIT, GL_NEAREST );
	}

	src = &frameBuffers[ 0 ];
	dst = liquidSnapshot ? &liquidScreenBuffer : &frameBuffers[ 2 ];
#ifdef RENDERER_GLX
	GLX_CompatRecordFboBlit( GLX_FBO_BLIT_COPY_SCREEN, qfalse,
		src->width, src->height, dst->width, dst->height );
#endif
	FBO_Bind( GL_READ_FRAMEBUFFER, src->fbo );
	FBO_Bind( GL_DRAW_FRAMEBUFFER, dst->fbo );

	yCrop = liquidSnapshot ? 0 : backEnd.viewParms.viewportHeight / 4;

	qglBlitFramebuffer( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY + yCrop,
		backEnd.viewParms.viewportWidth + backEnd.viewParms.viewportX,
		backEnd.viewParms.viewportHeight + backEnd.viewParms.viewportY,
		0, 0, dst->width, dst->height, GL_COLOR_BUFFER_BIT, GL_LINEAR );

	//if ( !backEnd.projection2D )
	{
		qglMatrixMode( GL_PROJECTION );
		qglLoadMatrixf( GL_Ortho( 0, glConfig.vidWidth, glConfig.vidHeight, 0, 0, 1 ) );
		qglMatrixMode( GL_MODELVIEW );
		qglLoadIdentity();
		GL_Cull( CT_TWO_SIDED );
		qglDisable( GL_CLIP_PLANE0 );
	}

	qglColor4f( 1, 1, 1, 1 );
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	if ( !liquidSnapshot ) {
		FBO_Blur2( dst, dst+1, dst );
	}
	ARB_ProgramDisable();

	//restore viewport and scissor
	qglViewport( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight ); 
	qglScissor( backEnd.viewParms.scissorX, backEnd.viewParms.scissorY,
		backEnd.viewParms.scissorWidth, backEnd.viewParms.scissorHeight ); 

	FBO_BindMain();
#ifdef RENDERER_GLX
	GLX_CompatEndGpuPassTimer( GLX_GPU_PASS_COPY_SCREEN );
#endif
	return qtrue;
}

void FBO_CopyScreen( void )
{
	(void)FBO_CopyScreenInternal( qfalse );
}

qboolean FBO_CopyLiquidScreen( void )
{
	return FBO_CopyScreenInternal( qtrue );
}


static void R_Setup_Quad_Lens( float offset, vec4_t color, vec3_t *verts, vec2_t *coords, vec4_t *colors )
{
	static const vec2_t t[6] = { {1.0, 0.0}, {0.0, 0.0}, {0.0, 1.0}, {0.0, 1.0}, {1.0, 1.0}, {1.0, 0.0} };

	const float width = (float)glConfig.vidWidth;
	const float height = (float)glConfig.vidHeight;
	int i;
	
	for ( i = 0; i < 6; i++ ) {
		coords[i][0] = t[i][0];
		coords[i][1] = t[i][1];
		Vector4Copy( color, colors[i] );
		verts[i][2] = 0.0;
	}

	verts[0][0] = -offset;
	verts[0][1] = -offset;

	verts[1][0] = width + offset;
	verts[1][1] = -offset;

	verts[2][0] = width + offset;
	verts[2][1] = height + offset;

	verts[3][0] = width + offset;
	verts[3][1] = height + offset;

	verts[4][0] = -offset;
	verts[4][1] = height + offset;

	verts[5][0] = -offset;
	verts[5][1] = -offset;
}


static void R_Bloom_LensEffect( float alpha )
{
	// lens rainbow colors
	static const GLfloat lc[][3] = {
		{ 0.78f, 0.23f, 0.34f },
		{ 0.78f, 0.39f, 0.21f },
		{ 0.78f, 0.59f, 0.21f },
		{ 0.71f, 0.75f, 0.21f },
		{ 0.52f, 0.78f, 0.21f },
		{ 0.32f, 0.78f, 0.21f },
		{ 0.21f, 0.78f, 0.28f },
		{ 0.21f, 0.78f, 0.47f },
		{ 0.21f, 0.77f, 0.66f },
		{ 0.21f, 0.67f, 0.78f },
		{ 0.21f, 0.47f, 0.78f },
		{ 0.21f, 0.28f, 0.78f },
		{ 0.35f, 0.21f, 0.78f },
		{ 0.53f, 0.21f, 0.78f },
		{ 0.72f, 0.21f, 0.75f },
		{ 0.78f, 0.21f, 0.59f },
	};
	int i;

	vec3_t verts[ ARRAY_LEN(lc) * 6 ];
	vec2_t coords[ ARRAY_LEN(lc) * 6 ];
	vec4_t colors[ ARRAY_LEN(lc) * 6 ];
	vec4_t color;
	qboolean glxStreamedDraw = qfalse;

	alpha /= (float)ARRAY_LEN( lc );
	for ( i = 0; i < ARRAY_LEN( lc ); i++ ) {
		VectorCopy( lc[i], color ); color[3] = alpha;
		R_Setup_Quad_Lens( (i+1)*144, color, &verts[i*6], &coords[i*6], &colors[i*6] );
	}

	GL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );

	qglVertexPointer( 3, GL_FLOAT, 0, verts );
	qglTexCoordPointer( 2, GL_FLOAT, 0, coords );
	qglColorPointer( 4, GL_FLOAT, 0, colors );

	if ( GLX_CompatStreamDrawPostProcessEnabled() ) {
		glxStreamedDraw = GLX_CompatTryStreamDrawArrayTexcoordColorPass(
			(int)ARRAY_LEN( verts ), verts, (int)sizeof( verts[0] ), coords, 0,
			colors, 4, GL_FLOAT, 0, GL_TRIANGLES, GLX_STAGE_POSTPROCESS_PASS,
			GLX_DYNAMIC_CATEGORY_MASK_SPECIAL );
	}
	if ( !glxStreamedDraw ) {
#ifdef RENDERER_GLX
		GLX_CompatDrawArrays( GL_TRIANGLES, 0, (int)ARRAY_LEN( verts ),
			GLX_LEGACY_DELEGATION_DRAW_ARRAY, GLX_DRAW_NONE );
#else
		qglDrawArrays( GL_TRIANGLES, 0, ARRAY_LEN( verts ) );
#endif
	}
}


void FBO_MotionBlur( void )
{
	const int w = glConfig.vidWidth;
	const int h = glConfig.vidHeight;
	const qboolean requested = ( r_motionBlur && r_motionBlur->integer &&
		r_motionBlurStrength && r_motionBlurStrength->value > 0.0f ) ? qtrue : qfalse;
	float radiusUv[2];
	float sampleBounds[4];
	int viewRect[4];
	int viewX, viewY, viewWidth, viewHeight;
	frameBuffer_t *source = &frameBuffers[ 0 ];

	if ( !requested ) {
		R_MotionBlur_ResetView( &motionBlurViewState );
		motionBlurFrame = -1;
		motionBlurCreateFailed = qfalse;
		if ( motionBlurBuffer.fbo ) {
			FBO_Clean( &motionBlurBuffer );
		}
		return;
	}

	if ( backEnd.screenshotCubeActive || backEnd.screenshotCubeFrontPending ) {
		R_MotionBlur_ResetView( &motionBlurViewState );
		return;
	}
	/* Recursive portal/mirror views are submitted before the primary view.
	 * Ignore them without disturbing the primary camera's temporal history. */
	if ( R_ViewPassIsPortal( &backEnd.viewParms ) ) {
		return;
	}

	if ( !fboEnabled || !programCompiled || !backEnd.doneSurfaces || ri.CL_IsMinimized() ||
		glConfig.stereoEnabled || ( r_anaglyphMode && r_anaglyphMode->integer ) ) {
		R_MotionBlur_ResetView( &motionBlurViewState );
		return;
	}
	if ( motionBlurFrame == tr.frameCount || motionBlurCreateFailed ) {
		return;
	}
	motionBlurFrame = tr.frameCount;
	if ( !R_MotionBlur_Calculate( &motionBlurViewState, qtrue,
		r_motionBlurStrength->value, ri.Milliseconds(), backEnd.refdef.vieworg,
		backEnd.refdef.viewaxis, backEnd.refdef.fov_x, backEnd.refdef.fov_y,
		w, h, radiusUv ) ) {
		return;
	}

	if ( !motionBlurBuffer.fbo ) {
		if ( !FBO_CreateWithFormat( &motionBlurBuffer, w, h, qfalse,
			fboInternalFormat, NULL, NULL ) ) {
			ri.Printf( PRINT_WARNING, "...unable to create motion-blur buffer; disabling r_motionBlur\n" );
			motionBlurCreateFailed = qtrue;
			R_MotionBlur_ResetView( &motionBlurViewState );
			ri.Cvar_Set( "r_motionBlur", "0" );
			return;
		}
		ri.Printf( PRINT_ALL, "...motion-blur buffer created (%ix%i, %s)\n",
			w, h, glDefToStr( motionBlurBuffer.internalFormat ) );
	}

	if ( blitMSfbo ) {
		FBO_BlitMS( qfalse );
		blitMSfbo = qfalse;
	}
	if ( !backEnd.projection2D ) {
		RB_SetGL2D();
	}
	if ( !R_MotionBlur_CalculateViewRect( w, h, backEnd.viewParms.viewportX,
		backEnd.viewParms.viewportY, backEnd.viewParms.viewportWidth,
		backEnd.viewParms.viewportHeight, viewRect, sampleBounds ) ) {
		return;
	}
	viewX = viewRect[0];
	viewY = viewRect[1];
	viewWidth = viewRect[2];
	viewHeight = viewRect[3];

	qglViewport( 0, 0, w, h );
	qglScissor( viewX, viewY, viewWidth, viewHeight );
	FBO_Bind( GL_FRAMEBUFFER, motionBlurBuffer.fbo );
	GL_BindTexture( 0, source->color );
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	qglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	ARB_ProgramEnable( DUMMY_VERTEX, MOTION_BLUR_FRAGMENT );
	ARB_MotionBlurParams( radiusUv, sampleBounds );
	RenderQuad( w, h );
	ARB_ProgramDisable();

	FBO_Bind( GL_READ_FRAMEBUFFER, motionBlurBuffer.fbo );
	FBO_Bind( GL_DRAW_FRAMEBUFFER, source->fbo );
	qglBlitFramebuffer( viewX, viewY, viewX + viewWidth, viewY + viewHeight,
		viewX, viewY, viewX + viewWidth, viewY + viewHeight,
		GL_COLOR_BUFFER_BIT, GL_NEAREST );

	qglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	FBO_Bind( GL_FRAMEBUFFER, source->fbo );
	qglScissor( 0, 0, w, h );
	fboReadIndex = 0;
}


qboolean FBO_Bloom( const float gamma, const float obScale, qboolean finalStage )
{
	const int w = glConfig.vidWidth;
	const int h = glConfig.vidHeight;

	frameBuffer_t *src, *dst;
	int finalBloomFBO;
	int i;
	float bloomThreshold;
	float bloomSoftKnee;
	float bloomKneeWidth;
#ifdef RENDERER_GLX
	qboolean glxPostShaderBound = qfalse;
#endif

	if ( backEnd.screenshotCubeActive || backEnd.screenshotCubeFrontPending ) {
		return qfalse;
	}

	if ( backEnd.doneBloom || !backEnd.doneSurfaces )
	{
#ifdef RENDERER_GLX
		GLX_RecordBloomState( GLX_BLOOM_RESULT_SKIPPED, finalStage );
#endif
		return qfalse;
	}

	backEnd.doneBloom = qtrue;
#ifdef RENDERER_GLX
	GLX_CompatBeginGpuPassTimer( GLX_GPU_PASS_BLOOM );
#endif

	if ( !fboBloomInited )
	{
		if ( (fboBloomInited = FBO_CreateBloom() ) == qfalse )
		{
			ri.Printf( PRINT_WARNING, "...error creating framebuffers for bloom\n" );
			ri.Cvar_Set( "r_bloom", "0" );
			FBO_CleanBloom();
#ifdef RENDERER_GLX
			GLX_RecordBloomState( GLX_BLOOM_RESULT_CREATE_FAILED, finalStage );
			GLX_CompatEndGpuPassTimer( GLX_GPU_PASS_BLOOM );
#endif
			return qfalse;
		}
		else
		{
			ri.Printf( PRINT_ALL, "...bloom framebuffers created\n" );
		}
	}

	if ( blitMSfbo )
	{
		FBO_BlitMS( qfalse );
		blitMSfbo = qfalse;
	}
	FBO_UpdateTonemapExposure();
	
	// extract intensity from main FBO to BLOOM_BASE
	src = &frameBuffers[ 0 ];
	dst = &frameBuffers[ BLOOM_BASE ];
#ifdef RENDERER_GLX
	GLX_CompatBeginGpuPassTimer( GLX_GPU_PASS_BLOOM_EXTRACT );
#endif
	FBO_Bind( GL_FRAMEBUFFER, dst->fbo );
	GL_BindTexture( 0, src->color );
	qglViewport( 0, 0, dst->width, dst->height );
	ARB_ProgramEnable( DUMMY_VERTEX, BLOOM_EXTRACT_FRAGMENT );
	bloomThreshold = r_bloom_threshold ? Com_Clamp( 0.0f, 64.0f, r_bloom_threshold->value ) : 0.75f;
	bloomSoftKnee = r_bloom_soft_knee ? Com_Clamp( 0.0f, 1.0f, r_bloom_soft_knee->value ) : 0.0f;
	bloomKneeWidth = bloomThreshold * bloomSoftKnee;
	if ( bloomKneeWidth < 0.0001f ) {
		bloomKneeWidth = 0.0001f;
	}
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 0, bloomThreshold, bloomThreshold,
		bloomThreshold, 1.0 );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 1, FBO_TonemapExposure(), FBO_TonemapExposure(),
		FBO_TonemapExposure(), 1.0f );
	qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 2, bloomThreshold - bloomKneeWidth,
		1.0f / ( bloomKneeWidth * 2.0f ), 0.0f, 1.0f );
	RenderQuad( w, h );
#ifdef RENDERER_GLX
	GLX_CompatEndGpuPassTimer( GLX_GPU_PASS_BLOOM_EXTRACT );
#endif

	// downscale and blur
	src = frameBuffers + BLOOM_BASE;
	for ( i = 1; i < fboBloomPasses; i++, src+=2 ) {
		dst = src + 2;
		// copy image to next level
#ifdef RENDERER_GLX
		GLX_CompatBeginGpuPassTimer( GLX_GPU_PASS_BLOOM_DOWNSCALE );
#endif
#ifdef USE_FBO_BLIT
		FBO_Bind( GL_READ_FRAMEBUFFER, src->fbo );
		FBO_Bind( GL_DRAW_FRAMEBUFFER, dst->fbo );
		qglBlitFramebuffer( 0, 0, src->width, src->height, 0, 0, dst->width, dst->height, GL_COLOR_BUFFER_BIT, GL_LINEAR );
#else
		ARB_ProgramDisable();
		FBO_Bind( GL_FRAMEBUFFER, dst->fbo );
		GL_BindTexture( 0, src->color );
		GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
		qglViewport( 0, 0, dst->width, dst->height );
		RenderQuad( w, h );
#endif
#ifdef RENDERER_GLX
		GLX_CompatEndGpuPassTimer( GLX_GPU_PASS_BLOOM_DOWNSCALE );
#endif
		FBO_Blur( dst, dst+1, dst );
	}

	// restore viewport
	qglViewport( 0, 0, w, h );

	// blend all bloom buffers to BLOOM_BASE+1 texture
	finalBloomFBO = BLOOM_BASE+1;
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
#ifdef RENDERER_GLX
	GLX_CompatBeginGpuPassTimer( GLX_GPU_PASS_BLOOM_BLEND );
#endif
	FBO_Bind( GL_FRAMEBUFFER, frameBuffers[ finalBloomFBO ].fbo );
	ARB_ProgramEnable( DUMMY_VERTEX, BLENDX_FRAGMENT );
	// setup all texture units
	for ( i = 0; i < fboBloomPasses - fboBloomBlendBase; i++ ) {
		GL_BindTexture( i, frameBuffers[ (i+fboBloomBlendBase)*2 + BLOOM_BASE ].color );
	}
	RenderQuad( w, h );
#ifdef RENDERER_GLX
	GLX_CompatEndGpuPassTimer( GLX_GPU_PASS_BLOOM_BLEND );
#endif

	if ( r_bloom_reflection->value )
	{
#ifdef RENDERER_GLX
		GLX_CompatBeginGpuPassTimer( GLX_GPU_PASS_BLOOM_LENS_REFLECTION );
#endif
		ARB_ProgramDisable();

		// copy final bloom image to some downscaled buffer
		src = &frameBuffers[ finalBloomFBO ];
		dst = &frameBuffers[ BLOOM_BASE + 2 + 2 ]; // 4x downscale
		FBO_Bind( GL_DRAW_FRAMEBUFFER, dst->fbo );
		FBO_Bind( GL_READ_FRAMEBUFFER, src->fbo );
		qglBlitFramebuffer( 0, 0, src->width, src->height, 0, 0, dst->width, dst->height, GL_COLOR_BUFFER_BIT, GL_LINEAR );
		
		// set render target to paired destination buffer and draw reflections
		FBO_Bind( GL_DRAW_FRAMEBUFFER, (dst+1)->fbo );
		GL_BindTexture( 0, dst->color );
		GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE );
		qglViewport( 0, 0, dst->width, dst->height );
		R_Bloom_LensEffect( fabs( r_bloom_reflection->value ) );
		
		// restore color and blend mode
		qglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
		GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
		
		// blur lens effect in paired buffer
		FBO_Blur( dst+1, dst, dst+1 );
		ARB_ProgramDisable();

		// add lens effect to final bloom buffer
		FBO_Bind( GL_FRAMEBUFFER, src->fbo );
		if ( r_bloom_reflection->value > 0 ) {
			GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
		} else {
			// negative reflection values will replace bloom texture with just lens effect
		}
		qglViewport( 0, 0, w, h );
		GL_BindTexture( 0, (dst+1)->color );
		RenderQuad( w, h );

		// restore blend mode
		GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
#ifdef RENDERER_GLX
		GLX_CompatEndGpuPassTimer( GLX_GPU_PASS_BLOOM_LENS_REFLECTION );
#endif
	}

	if ( windowAdjusted || backEnd.screenshotMask ) {
		finalStage = qfalse; // can't blit directly into back buffer in this case
	}

	// if we don't need to read pixels later - blend directly to back buffer
	if ( finalStage ) {
		if ( backEnd.screenshotMask ) {
			FBO_Bind( GL_FRAMEBUFFER, frameBuffers[ BLOOM_BASE ].fbo );
		} else {
			FBO_Bind( GL_FRAMEBUFFER, 0 );
		}
	} else {
		FBO_Bind( GL_FRAMEBUFFER, frameBuffers[ BLOOM_BASE ].fbo );
	}

	GL_BindTexture( 1, frameBuffers[ finalBloomFBO ].color ); // final bloom texture
	GL_BindTexture( 0, frameBuffers[ 0 ].color ); // original image
#ifdef RENDERER_GLX
	GLX_CompatBeginGpuPassTimer( GLX_GPU_PASS_BLOOM_FINAL );
#endif
	if ( finalStage ) {
#ifdef RENDERER_GLX
		FBO_PrepareGlxPostShaderColorGradeLut();
		if ( !R_BloomProtectHighlightsActive() ) {
			glxPostShaderBound = GLX_CompatTryBindPostShaderFinal( qtrue, qtrue,
				r_bloom_intensity->value );
		}
		if ( !glxPostShaderBound ) {
#endif
		// blend & apply gamma in one pass
		ARB_ProgramEnable( DUMMY_VERTEX, BLEND2_GAMMA_FRAGMENT );
		FBO_SetOutputTransformParams( gamma, obScale );
		FBO_BindColorGradeLut();
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 1,
			r_bloom_intensity->value, R_BloomProtectHighlightsActive() ? 1.0f : 0.0f, 0, 0 );
#ifdef RENDERER_GLX
		}
#endif
	} else {
#ifdef RENDERER_GLX
		if ( !R_BloomProtectHighlightsActive() ) {
			glxPostShaderBound = GLX_CompatTryBindPostShaderFinal( qtrue, qfalse,
				r_bloom_intensity->value );
		}
		if ( !glxPostShaderBound ) {
#endif
		// just blend
		ARB_ProgramEnable( DUMMY_VERTEX, BLEND2_FRAGMENT );
		qglProgramLocalParameter4fARB( GL_FRAGMENT_PROGRAM_ARB, 1,
			r_bloom_intensity->value, R_BloomProtectHighlightsActive() ? 1.0f : 0.0f, 0, 0 );
#ifdef RENDERER_GLX
		}
#endif
	}
	RenderQuad( w, h );
#ifdef RENDERER_GLX
	if ( glxPostShaderBound ) {
		GLX_CompatUnbindPostShader();
	} else {
		ARB_ProgramDisable();
	}
	GLX_CompatEndGpuPassTimer( GLX_GPU_PASS_BLOOM_FINAL );
#else
	ARB_ProgramDisable();
#endif

	if ( finalStage ) {
		if ( backEnd.screenshotMask ) {
			FBO_BlitToBackBuffer( BLOOM_BASE ); // so any further qglReadPixels() will read from BLOOM_BASE
			 // fboReadIndex = 0;
		} else {
			//	already in back buffer
			fboReadIndex = 0;
		}
	} else {
		// we need depth/stencil buffers there
		fboReadIndex = BLOOM_BASE;
	}

#ifdef RENDERER_GLX
	GLX_RecordBloomState( finalStage ? GLX_BLOOM_RESULT_FINAL : GLX_BLOOM_RESULT_INTERMEDIATE,
		finalStage );
	GLX_CompatEndGpuPassTimer( GLX_GPU_PASS_BLOOM );
#endif

	return finalStage;
}


void R_BloomScreen( void )
{
	if ( r_bloom->integer == 1 && fboEnabled && qglActiveTextureARB )
	{
		if ( !backEnd.framePostProcessed && !backEnd.doneBloom && backEnd.doneSurfaces )
		{
			if ( !backEnd.projection2D )
				RB_SetGL2D();
			qglColor4f( 1, 1, 1, 1 );
			FBO_Bloom( 0, 0, qfalse );
		}
	}
}


qboolean FBO_CubemapCaptureOutput( void )
{
	const float obScale = 1 << tr.overbrightBits;
	const float gamma = 1.0f / r_gamma->value;
	const int w = glConfig.vidWidth;
	const int h = glConfig.vidHeight;

	if ( !fboEnabled || !programCompiled || w <= 0 || h <= 0 ) {
		return qfalse;
	}

	ARB_ProgramDisable();
	if ( blitMSfbo ) {
		FBO_BlitMS( qfalse );
		blitMSfbo = qfalse;
	}
	FBO_UpdateTonemapExposure();

	if ( !backEnd.projection2D ) {
		qglMatrixMode( GL_PROJECTION );
		qglLoadMatrixf( GL_Ortho( 0, w, h, 0, 0, 1 ) );
		qglMatrixMode( GL_MODELVIEW );
		qglLoadIdentity();
		backEnd.projection2D = qtrue;
	}

	qglViewport( 0, 0, w, h );
	qglScissor( 0, 0, w, h );
	qglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	GL_Cull( CT_TWO_SIDED );
	FBO_Bind( GL_FRAMEBUFFER, frameBuffers[ 1 ].fbo );
	GL_BindTexture( 0, frameBuffers[ 0 ].color );
	ARB_ProgramEnable( DUMMY_VERTEX, GAMMA_FRAGMENT );
	FBO_SetOutputTransformParams( gamma, obScale );
	FBO_BindColorGradeLut();
	RenderQuad( w, h );
	ARB_ProgramDisable();

	fboReadIndex = 1;
	FBO_Bind( GL_READ_FRAMEBUFFER, frameBuffers[ 1 ].fbo );
	return qtrue;
}


void FBO_FinishCubemapCaptureOutput( void )
{
	fboReadIndex = 0;
	FBO_BindMain();
}


void FBO_PostProcess( void )
{
	const float obScale = 1 << tr.overbrightBits;
	const float gamma = 1.0f / r_gamma->value;
	const float w = glConfig.vidWidth;
	const float h = glConfig.vidHeight;
	qboolean minimized;

	ARB_ProgramDisable();

	if ( !backEnd.projection2D )
	{
		qglViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
		qglScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
		qglMatrixMode( GL_PROJECTION );
		qglLoadMatrixf( GL_Ortho( 0, glConfig.vidWidth, glConfig.vidHeight, 0, 0, 1 ) );
		qglMatrixMode( GL_MODELVIEW );
		qglLoadIdentity();
		backEnd.projection2D = qtrue;
	}

	if ( blitMSfbo )
	{
		FBO_BlitMS( qfalse );
		blitMSfbo = qfalse;
	}
	FBO_UpdateTonemapExposure();

	qglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	GL_Cull( CT_TWO_SIDED );
	if ( r_anaglyphMode->integer )
		GL_ColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );

	minimized = ri.CL_IsMinimized();
#ifdef RENDERER_GLX
	GLX_CompatRecordPostProcessFrame( minimized,
		( r_bloom->integer && programCompiled && qglActiveTextureARB ) ? qtrue : qfalse,
		programCompiled ? qtrue : qfalse, backEnd.screenshotMask, windowAdjusted,
		fboReadIndex, FBO_HdrSceneLinearColorMode(), r_renderScale->integer, r_greyscale->value,
		gamma, FBO_OutputOverbrightScale( obScale ) );
	GLX_CompatBeginGpuPassTimer( GLX_GPU_PASS_POSTPROCESS );
#endif

	if ( r_bloom->integer && programCompiled && qglActiveTextureARB ) {
		if ( FBO_Bloom( gamma, obScale, !minimized ) ) {
#ifdef RENDERER_GLX
			GLX_CompatRecordPostProcessResult( GLX_POSTPROCESS_RESULT_BLOOM_FINAL );
			GLX_CompatEndGpuPassTimer( GLX_GPU_PASS_POSTPROCESS );
#endif
			return;
		}
	}

	// check if we can perform final draw directly into back buffer
	if ( backEnd.screenshotMask == 0 && !windowAdjusted && !minimized ) {
#ifdef RENDERER_GLX
		qboolean glxPostShaderBound = qfalse;
#endif

		FBO_Bind( GL_FRAMEBUFFER, 0 );
		GL_BindTexture( 0, frameBuffers[ fboReadIndex ].color );
#ifdef RENDERER_GLX
		GLX_CompatBeginGpuPassTimer( GLX_GPU_PASS_GAMMA_DIRECT );
		FBO_PrepareGlxPostShaderColorGradeLut();
		glxPostShaderBound = GLX_CompatTryBindPostShaderFinal( qfalse, qtrue, 0.0f );
		if ( !glxPostShaderBound ) {
#endif
		ARB_ProgramEnable( DUMMY_VERTEX, GAMMA_FRAGMENT );
		FBO_SetOutputTransformParams( gamma, obScale );
		FBO_BindColorGradeLut();
#ifdef RENDERER_GLX
		}
#endif
		RenderQuad( w, h );
#ifdef RENDERER_GLX
		if ( glxPostShaderBound ) {
			GLX_CompatUnbindPostShader();
		} else {
			ARB_ProgramDisable();
		}
#else
		ARB_ProgramDisable();
#endif
#ifdef RENDERER_GLX
		GLX_CompatRecordPostProcessResult( GLX_POSTPROCESS_RESULT_GAMMA_DIRECT );
		GLX_CompatEndGpuPassTimer( GLX_GPU_PASS_GAMMA_DIRECT );
		GLX_CompatEndGpuPassTimer( GLX_GPU_PASS_POSTPROCESS );
#endif
		return;
	}

	// apply gamma shader
#ifdef RENDERER_GLX
	{
	qboolean glxPostShaderBound = qfalse;
#endif
	FBO_Bind( GL_FRAMEBUFFER, frameBuffers[ 1 ].fbo ); // destination - secondary buffer
	GL_BindTexture( 0, frameBuffers[ fboReadIndex ].color );  // source - main color buffer
#ifdef RENDERER_GLX
	GLX_CompatBeginGpuPassTimer( GLX_GPU_PASS_GAMMA_BLIT );
	FBO_PrepareGlxPostShaderColorGradeLut();
	glxPostShaderBound = GLX_CompatTryBindPostShaderFinal( qfalse, qtrue, 0.0f );
	if ( !glxPostShaderBound ) {
#endif
	ARB_ProgramEnable( DUMMY_VERTEX, GAMMA_FRAGMENT );
	FBO_SetOutputTransformParams( gamma, obScale );
	FBO_BindColorGradeLut();
#ifdef RENDERER_GLX
	}
#endif
	RenderQuad( w, h );
#ifdef RENDERER_GLX
	if ( glxPostShaderBound ) {
		GLX_CompatUnbindPostShader();
	} else {
		ARB_ProgramDisable();
	}
	GLX_CompatEndGpuPassTimer( GLX_GPU_PASS_GAMMA_BLIT );
	}
#else
	ARB_ProgramDisable();
#endif

	if ( !minimized ) {
		FBO_BlitToBackBuffer( 1 );
#ifdef RENDERER_GLX
		GLX_CompatRecordPostProcessResult( GLX_POSTPROCESS_RESULT_GAMMA_BLIT );
#endif
#ifdef RENDERER_GLX
	} else {
		GLX_CompatRecordPostProcessResult( GLX_POSTPROCESS_RESULT_MINIMIZED );
#endif
	}
#ifdef RENDERER_GLX
	GLX_CompatEndGpuPassTimer( GLX_GPU_PASS_POSTPROCESS );
#endif
}


static void QGL_ResolveRenderSizeFromCvars( void )
{
	glConfig.vidWidth = gls.windowWidth;
	glConfig.vidHeight = gls.windowHeight;
	gls.captureWidth = glConfig.vidWidth;
	gls.captureHeight = glConfig.vidHeight;
	ri.CL_SetScaling( 1.0, gls.captureWidth, gls.captureHeight );

	if ( !qglGenProgramsARB || !qglGenFramebuffers || !r_fbo || !r_fbo->integer ) {
		return;
	}

	if ( r_renderScale && r_renderScale->integer ) {
		glConfig.vidWidth = r_renderWidth ? r_renderWidth->integer : glConfig.vidWidth;
		glConfig.vidHeight = r_renderHeight ? r_renderHeight->integer : glConfig.vidHeight;
		gls.captureWidth = glConfig.vidWidth;
		gls.captureHeight = glConfig.vidHeight;
		ri.CL_SetScaling( 1.0, gls.captureWidth, gls.captureHeight );
	}

	if ( r_ext_supersample && r_ext_supersample->integer ) {
		glConfig.vidWidth *= 2;
		glConfig.vidHeight *= 2;
		ri.CL_SetScaling( 2.0, gls.captureWidth, gls.captureHeight );
	}
}

void QGL_SetRenderScale( qboolean verbose )
{
	windowAdjusted = qfalse;

	blitX0 = blitY0 = 0;
	blitX1 = gls.windowWidth;
	blitY1 = gls.windowHeight;

	blitFilter = GL_NEAREST;

	superSampled = qfalse;

	if ( !qglGenProgramsARB || !qglGenFramebuffers )
		return;

	if ( !r_fbo->integer )
	{
		if ( verbose && r_renderScale->integer )
		{
			ri.Printf( PRINT_ALL, "...ignoring \\r_renderScale due to disabled FBO\n" );
		}
		return;
	}

	if ( r_ext_supersample->integer )
	{
		superSampled = qtrue;
		blitFilter = GL_LINEAR; // default value for (r_renderScale==0) case
	}

	if ( gls.windowWidth != glConfig.vidWidth || gls.windowHeight != glConfig.vidHeight )
	{
		if ( r_renderScale->integer > 0 )
		{
			int scaleMode = r_renderScale->integer - 1;
			if ( scaleMode & 1 )
			{
				// preserve aspect ratio (black bars on sides)
				float windowAspect = (float) gls.windowWidth / (float) gls.windowHeight;
				float renderAspect = (float) glConfig.vidWidth / (float) glConfig.vidHeight;
				if ( windowAspect >= renderAspect ) 
				{
					float scale = (float) gls.windowHeight / ( float ) glConfig.vidHeight;
					int bias = ( gls.windowWidth - scale * (float) glConfig.vidWidth ) / 2;
					blitX0 += bias;
					blitX1 -= bias;
				}
				else
				{
					float scale = (float) gls.windowWidth / ( float ) glConfig.vidWidth;
					int bias = ( gls.windowHeight - scale * (float) glConfig.vidHeight ) / 2;
					blitY0 += bias;
					blitY1 -= bias;
				}
			}
			// linear filtering
			if ( scaleMode & 2 )
				blitFilter = GL_LINEAR;
			else
				blitFilter = GL_NEAREST;
		}

		windowAdjusted = qtrue;
	}
}


void QGL_DoneFBO( void )
{
	if ( qglGenFramebuffers )
	{
		FBO_Bind(GL_FRAMEBUFFER, 0);
		FBO_Clean(&frameBufferMS);
		FBO_Clean(&frameBuffers[0]);
		FBO_Clean(&frameBuffers[1]);
		FBO_Clean(&frameBuffers[2]);
		FBO_Clean(&frameBuffers[3]);
		FBO_Clean(&frameBuffers[4]);
		FBO_Clean(&liquidScreenBuffer);
		FBO_Clean(&menuDofBuffers[0]);
		FBO_Clean(&menuDofBuffers[1]);
		FBO_Clean(&motionBlurBuffer);
		FBO_CleanBloom();
		FBO_CleanDepth();
		fboEnabled = qfalse;
		fboBloomInited = qfalse;
		R_MotionBlur_ResetView( &motionBlurViewState );
		motionBlurFrame = -1;
		motionBlurCreateFailed = qfalse;
#ifdef RENDERER_GLX
		GLX_CompatRecordFboShutdown();
#endif
	}
}


void QGL_InitFBO( void )
{
	int w, h;
	int screenMapWidth = SCR_WIDTH;
	int screenMapHeight = SCR_HEIGHT;
	int liquidScreenWidth = 0;
	int liquidScreenHeight = 0;
	qboolean programReady;
	qboolean depthStencil;
	qboolean result = qfalse;

	QGL_DoneFBO();
	QGL_ResolveRenderSizeFromCvars();
	QGL_SetRenderScale( qtrue );

	w = glConfig.vidWidth;
	h = glConfig.vidHeight;
	if ( r_liquid && r_liquid->integer > 0 && r_liquidResolution ) {
		const float scale = Com_Clamp( 0.25f, 1.0f, r_liquidResolution->value );
		liquidScreenWidth = MAX( 64, (int)( (float)w * scale + 0.5f ) );
		liquidScreenHeight = MAX( 64, (int)( (float)h * scale + 0.5f ) );
	}
	
	fboEnabled = qfalse;
	frameBufferMultiSampling = qfalse;
	depthFadeTextureShared = qfalse;
	fboInternalFormat = FBO_MainInternalFormat();
	fboTextureFormat = 0;
	fboTextureType = 0;
	csmShadowAtlasCreateFailed = qfalse;
	programReady = ( qglGenProgramsARB && GL_ProgramAvailable() ) ? qtrue : qfalse;

	if ( r_fbo->integer && ( !programReady || !qglGenFramebuffers ) )
		ri.Printf( PRINT_WARNING, "...FBO is not available\n" );

	if ( !programReady || !qglGenFramebuffers )
	{
#ifdef RENDERER_GLX
		GLX_RecordFboInitState( r_fbo->integer ? qtrue : qfalse, qfalse,
			programReady, qglGenFramebuffers ? qtrue : qfalse );
#endif
		return;
	}

	if ( !r_fbo->integer )
	{
		FBO_CreateSpotShadowAtlas();
		if ( csmShadowProgramsCompiled ) {
			FBO_CreateCSMShadowAtlas();
		}
#ifdef RENDERER_GLX
		GLX_RecordFboInitState( qfalse, qfalse, qtrue, qtrue );
#endif
		return;
	}

	qglGetError(); // reset error code

	if ( windowAdjusted )
		blitClear = 2; // front & back buffers
	else
		blitClear = 0;

	if ( FBO_HdrSceneLinearMode() && !FBO_InternalFormatIsFloat( fboInternalFormat ) ) {
		ri.Printf( PRINT_WARNING, "...r_hdr 1 requires a floating-point scene FBO, got %s\n",
			glDefToStr( fboInternalFormat ) );
#ifdef RENDERER_GLX
		GLX_RecordFboInitState( qtrue, qfalse, qtrue, qtrue );
#endif
		return;
	}

	if ( FBO_CreateMS( &frameBufferMS, w, h ) )
	{
		frameBufferMultiSampling = qtrue;
		if ( r_flares->integer || ( r_depthFade && r_depthFade->integer ) ||
			( r_globalFog && r_globalFog->integer ) ||
			R_CelShadingWorldActive() )
			depthStencil = qtrue;
		else
			depthStencil = qfalse;
		result = FBO_Create( &frameBuffers[ 0 ], w, h, depthStencil, &fboTextureFormat, &fboTextureType )
			&& FBO_Create( &frameBuffers[ 1 ], w, h, depthStencil, NULL, NULL )
			&& FBO_Create( &frameBuffers[ 2 ], screenMapWidth, screenMapHeight, qfalse, NULL, NULL )
			&& FBO_Create( &frameBuffers[ 3 ], screenMapWidth, screenMapHeight, qfalse, NULL, NULL );
		frameBufferMultiSampling = result;
	}
	else
	{
		result = FBO_Create( &frameBuffers[ 0 ], w, h, qtrue, &fboTextureFormat, &fboTextureType )
			&& FBO_Create( &frameBuffers[ 1 ], w, h, qtrue, NULL, NULL )
			&& FBO_Create( &frameBuffers[ 2 ], screenMapWidth, screenMapHeight, qfalse, NULL, NULL )
			&& FBO_Create( &frameBuffers[ 3 ], screenMapWidth, screenMapHeight, qfalse, NULL, NULL );
	}

	if ( result && superSampled )
	{
		result &= FBO_Create( &frameBuffers[ 4 ], gls.captureWidth, gls.captureHeight, qfalse, NULL, NULL );
	}

	if ( result && liquidScreenWidth > 0 && liquidScreenHeight > 0 &&
		!FBO_Create( &liquidScreenBuffer, liquidScreenWidth, liquidScreenHeight,
			qfalse, NULL, NULL ) ) {
		FBO_Clean( &liquidScreenBuffer );
		ri.Printf( PRINT_WARNING,
			"WARNING: could not create the liquid scene snapshot; enhanced liquid rendering is disabled\n" );
	}

	if ( result )
	{
		fboEnabled = qtrue;
		if ( !depthFadeTexture )
			depthFadeTexture = FBO_CreateDepthFadeTexture( w, h, qfalse );
		FBO_CreateDlightShadowAtlas();
		FBO_CreateSpotShadowAtlas();
		FBO_CreateCSMShadowAtlas();
		FBO_BindMain();
		ri.Printf( PRINT_ALL, "...using %s (%s:%s) FBO\n", glDefToStr( fboInternalFormat ),
			glDefToStr( fboTextureFormat ), glDefToStr( fboTextureType ) );
	}
	else
	{
		QGL_DoneFBO();
	}
#ifdef RENDERER_GLX
	GLX_RecordFboInitState( qtrue, fboEnabled, qtrue, qtrue );
#endif
}
#endif // USE_FBO


void QGL_InitARB( void )
{
	ARB_UpdatePrograms();
#ifdef USE_FBO
	QGL_InitFBO();
#endif
	ri.Cvar_ResetGroup( CVG_RENDERER, qtrue );
}


void QGL_DoneARB( void )
{
#ifdef USE_FBO
	QGL_DoneFBO();
#endif
	if ( programCompiled )
	{
		ARB_ProgramDisable();
		ARB_DeletePrograms();
	}
}
