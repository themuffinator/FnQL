/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_init.c -- functions that are not called every frame

#include "tr_local.h"
#include "tr_glx_compat.h"

glconfig_t	glConfig;
qboolean	nonPowerOfTwoTextures;
qboolean	textureFilterAnisotropic;
qboolean    textureBorderClampAvailable;
int			maxAnisotropy;
int			gl_version;
int			gl_clamp_mode;	// GL_CLAMP or GL_CLAMP_TO_EGGE

glstate_t	glState;

glstatic_t	gls;

static void GfxInfo( void );
static void VarInfo( void );
static void GL_SetDefaultState( void );

static void R_MakeCvarInstant( cvar_t *cvar )
{
	if ( cvar ) {
		cvar->flags &= ~CVAR_LATCH;
	}
}

cvar_t	*r_flareSize;
cvar_t	*r_flareFade;
cvar_t	*r_flareCoeff;

cvar_t	*r_railWidth;
cvar_t	*r_railCoreWidth;
cvar_t	*r_railSegmentLength;

cvar_t	*r_detailTextures;

cvar_t	*r_znear;
cvar_t	*r_zproj;
cvar_t	*r_stereoSeparation;

cvar_t	*r_skipBackEnd;

cvar_t	*r_anaglyphMode;

cvar_t	*r_greyscale;

static cvar_t *r_ignorehwgamma;

cvar_t  *r_teleporterFlash;

cvar_t	*r_fastsky;
cvar_t	*r_neatsky;
cvar_t	*r_drawSun;
cvar_t	*r_dynamiclight;
cvar_t	*r_depthFade;
cvar_t	*r_celShading;
cvar_t	*r_celShadingWorld;
cvar_t	*r_celShadingWorldWidth;
cvar_t	*r_celShadingWorldAlpha;
cvar_t	*r_celShadingWorldDepthThreshold;
cvar_t	*r_celShadingModelShadows;
cvar_t	*r_celViewWeapon;
cvar_t	*r_celShadingSteps;
cvar_t	*r_celOutline;
cvar_t	*r_celOutlineScale;
cvar_t	*r_celOutlineAlpha;
cvar_t	*r_celViewWeaponOutlineScale;
cvar_t	*r_celViewWeaponOutlineAlpha;
cvar_t	*r_celOutlineColor;
cvar_t  *r_mergeLightmaps;
#ifdef USE_PMLIGHT
cvar_t	*r_dlightMode;
cvar_t	*r_dlightSpecPower;
cvar_t	*r_dlightSpecColor;
cvar_t	*r_dlightFalloff;
cvar_t	*r_dlightShadows;
cvar_t	*r_dlightShadowStrength;
cvar_t	*r_dlightShadowBias;
cvar_t	*r_dlightShadowCasterDepthBias;
cvar_t	*r_dlightShadowCasterSlopeBias;
cvar_t	*r_dlightShadowCasterNormalBias;
cvar_t	*r_dlightShadowFilter;
cvar_t	*r_dlightShadowMaxLights;
cvar_t	*r_dlightShadowResolution;
cvar_t	*r_dlightShadowDebug;
cvar_t	*r_spotShadows;
cvar_t	*r_spotShadowMaxLights;
cvar_t	*r_spotShadowResolution;
cvar_t	*r_spotShadowDebug;
cvar_t	*r_dlightScale;
cvar_t	*r_dlightIntensity;
#endif // USE_PMLIGHT

cvar_t	*r_dlightSaturation;
cvar_t	*r_dlightOverbrightGamut;
cvar_t	*r_staticLights;
cvar_t	*r_staticLightMaxLights;
cvar_t	*r_staticLightShadows;
cvar_t	*r_staticLightShadowMaxLights;
cvar_t	*r_staticLightDebug;
cvar_t	*r_surfaceLightProxies;
cvar_t	*r_surfaceLightProxyMaxLights;
cvar_t	*r_surfaceLightProxyShadows;
cvar_t	*r_surfaceLightProxyShadowMaxLights;
cvar_t	*r_surfaceLightProxyDebug;
cvar_t	*r_csmShadows;
cvar_t	*r_csmCascadeCount;
cvar_t	*r_csmMaxDistance;
cvar_t	*r_csmSplitLambda;
cvar_t	*r_csmResolution;
cvar_t	*r_csmShadowStrength;
cvar_t	*r_csmShadowFilter;
cvar_t	*r_csmShadowBias;
cvar_t	*r_csmCasterDepthBias;
cvar_t	*r_csmCasterSlopeBias;
cvar_t	*r_csmCasterNormalBias;
cvar_t	*r_csmDebug;
cvar_t	*r_csmDebugFallback;
cvar_t	*r_shadowCorrectness;

#ifdef USE_VBO
cvar_t	*r_vbo;
#endif

#ifdef USE_FBO
cvar_t	*r_fbo;
cvar_t	*r_hdr;
cvar_t	*r_hdrPrecision;
cvar_t	*r_hdrBloomFormat;
cvar_t	*r_srgbTextures;
cvar_t	*r_framebufferSRGB;
cvar_t	*r_tonemap;
cvar_t	*r_tonemapExposure;
cvar_t	*r_hudExcludePostProcess;
cvar_t	*r_colorGrade;
cvar_t	*r_colorGradeLift;
cvar_t	*r_colorGradeGamma;
cvar_t	*r_colorGradeGain;
cvar_t	*r_colorGradeWhitePoint;
cvar_t	*r_colorGradeAdaptWhitePoint;
cvar_t	*r_colorGradeLUT;
cvar_t	*r_colorGradeLUTScale;
cvar_t	*r_outputBackend;
cvar_t	*r_outputAllowExperimentalLinuxHDR;
cvar_t	*r_flareSceneLinear;
cvar_t	*r_bloom;
cvar_t	*r_bloom_threshold;
cvar_t	*r_bloom_threshold_mode;
cvar_t	*r_bloom_modulate;
cvar_t	*r_bloom_soft_knee;
cvar_t	*r_bloom_passes;
cvar_t	*r_bloom_blend_base;
cvar_t	*r_bloom_intensity;
cvar_t	*r_bloom_filter_size;
cvar_t	*r_bloom_reflection;
cvar_t	*r_crt;
cvar_t	*r_crtAmount;
cvar_t	*r_crtScanlineStrength;
cvar_t	*r_crtMaskStrength;
cvar_t	*r_crtCurvature;
cvar_t	*r_crtChromatic;

cvar_t	*r_renderWidth;
cvar_t	*r_renderHeight;
cvar_t	*r_renderScale;
#endif // USE_FBO

cvar_t	*r_dlightBacks;

cvar_t	*r_lodbias;
cvar_t	*r_lodscale;

cvar_t	*r_norefresh;
cvar_t	*r_drawentities;
cvar_t	*r_drawworld;
cvar_t	*r_speeds;
cvar_t	*r_fullbright;
cvar_t	*r_novis;
cvar_t	*r_nocull;
cvar_t	*r_facePlaneCull;
cvar_t	*r_showcluster;
cvar_t	*r_nocurves;

cvar_t	*r_allowExtensions;

cvar_t	*r_ext_compressed_textures;
cvar_t	*r_ext_multitexture;
cvar_t	*r_ext_compiled_vertex_array;
cvar_t	*r_ext_texture_env_add;
cvar_t	*r_ext_texture_filter_anisotropic;
cvar_t	*r_ext_max_anisotropy;

cvar_t	*r_ignoreGLErrors;

//cvar_t	*r_stencilbits;
cvar_t	*r_texturebits;

#ifdef USE_FBO
cvar_t	*r_ext_multisample;
cvar_t	*r_ext_supersample;
cvar_t	*r_ext_alpha_to_coverage;
#endif // USE_FBO

cvar_t	*r_drawBuffer;
cvar_t	*r_lightmap;
cvar_t	*r_vertexLight;
cvar_t	*r_shadows;
cvar_t	*r_flares;
cvar_t	*r_nobind;
cvar_t	*r_singleShader;
cvar_t	*r_roundImagesDown;
cvar_t	*r_colorMipLevels;
cvar_t	*r_picmip;
cvar_t	*r_picmipFilter;
cvar_t	*r_nomip;
cvar_t	*r_showtris;
cvar_t	*r_showsky;
cvar_t	*r_shownormals;
cvar_t	*r_finish;
cvar_t	*r_clear;
cvar_t	*r_textureMode;
cvar_t	*r_offsetFactor;
cvar_t	*r_offsetUnits;
cvar_t	*r_gamma;
cvar_t	*r_intensity;
cvar_t	*r_lockpvs;
cvar_t	*r_noportals;
cvar_t	*r_portalOnly;

cvar_t	*r_subdivisions;
cvar_t	*r_lodCurveError;

cvar_t	*r_overBrightBits;
cvar_t	*r_mapOverBrightBits;
cvar_t	*r_mapOverBrightCap;
cvar_t	*r_mapGreyScale;

cvar_t	*r_debugSurface;
cvar_t	*r_simpleMipMaps;

cvar_t	*r_showImages;
cvar_t	*r_defaultImage;

cvar_t	*r_ambientScale;
cvar_t	*r_directedScale;
cvar_t	*r_debugLight;
cvar_t	*r_debugSort;
cvar_t	*r_printShaders;
cvar_t	*r_saveFontData;

cvar_t	*r_marksOnTriangleMeshes;

cvar_t	*r_aviMotionJpegQuality;
cvar_t	*r_screenshotJpegQuality;
cvar_t	*r_screenshotNameFormat;
cvar_t	*r_screenshotCaptureMode;
cvar_t	*r_screenshotWriteViewpos;
cvar_t	*r_screenshotWatermark;
cvar_t	*r_screenshotWatermarkAlignment;
cvar_t	*r_screenshotWatermarkScreenAlignment;
cvar_t	*r_screenshotWatermarkMargin;
cvar_t	*r_levelshotSize;
cvar_t	*r_levelshotDownscale;
cvar_t	*r_levelshotSourceAspect;

qboolean rb_allowScreenshotWatermark = qtrue;

static cvar_t *r_maxpolys;
static cvar_t* r_maxpolyverts;
int		max_polys;
int		max_polyverts;

static char gl_extensions[ 32768 ];
qboolean textureSrgbAvailable = qfalse;
qboolean framebufferSrgbAvailable = qfalse;

#define GLE( ret, name, ... ) ret ( APIENTRY * q##name )( __VA_ARGS__ );
	QGL_Core_PROCS
	QGL_Ext_PROCS
	QGL_ARB_PROGRAM_PROCS
	QGL_VBO_PROCS
	QGL_OCCLUSION_QUERY_PROCS
	QGL_FBO_PROCS
	QGL_FBO_OPT_PROCS
#undef GLE

typedef struct {
	void **symbol;
	const char *name;
} sym_t;

#define GLE( ret, name, ... ) { (void**)&q##name, XSTRING(name) },
static sym_t core_procs[] = { QGL_Core_PROCS };
static sym_t ext_procs[] = { QGL_Ext_PROCS };
static sym_t arb_procs[] = { QGL_ARB_PROGRAM_PROCS };
static sym_t vbo_procs[] = { QGL_VBO_PROCS };
static sym_t occlusion_query_procs[] = { QGL_OCCLUSION_QUERY_PROCS };
static sym_t fbo_procs[] = { QGL_FBO_PROCS };
static sym_t fbo_opt_procs[] = { QGL_FBO_OPT_PROCS };
#undef GLE


/*
==================
R_ResolveSymbols

returns NULL on success or last failed symbol name otherwise
==================
*/
static const char *R_ResolveSymbols( sym_t *syms, int count )
{
	int i;
	for ( i = 0; i < count; i++ )
	{
		*syms[ i ].symbol = ri.GL_GetProcAddress( syms[ i ].name );
		if ( *syms[ i ].symbol == NULL )
		{
			return syms[ i ].name;
		}
	}
	return NULL;
}

static void R_AssignProcAddress( void *destination, size_t destinationSize,
		const char *name )
{
	void *address = ri.GL_GetProcAddress( name );

	if ( destinationSize != sizeof( address ) )
	{
		ri.Error( ERR_FATAL, "OpenGL procedure pointer size mismatch for %s", name );
	}
	Com_Memcpy( destination, &address, destinationSize );
}


static void R_ClearSymbols( sym_t *syms, int count )
{
	int i;
	for ( i = 0; i < count; i++ )
	{
		*syms[ i ].symbol = NULL;
	}
}


static void R_ClearSymTables( void )
{
	R_ClearSymbols( core_procs, ARRAY_LEN( core_procs ) );
	R_ClearSymbols( ext_procs, ARRAY_LEN( ext_procs ) );
	R_ClearSymbols( arb_procs, ARRAY_LEN( arb_procs ) );
	R_ClearSymbols( vbo_procs, ARRAY_LEN( vbo_procs ) );
	R_ClearSymbols( occlusion_query_procs, ARRAY_LEN( occlusion_query_procs ) );
	R_ClearSymbols( fbo_procs, ARRAY_LEN( fbo_procs ) );
	R_ClearSymbols( fbo_opt_procs, ARRAY_LEN( fbo_opt_procs ) );
}


// for modular renderer
#ifdef USE_RENDERER_DLOPEN
void QDECL Com_Error( errorParm_t code, const char *fmt, ... )
{
	char buf[ 4096 ];
	va_list	argptr;
	va_start( argptr, fmt );
	Q_vsnprintf( buf, sizeof( buf ), fmt, argptr );
	va_end( argptr );
	ri.Error( code, "%s", buf );
}

void QDECL Com_Printf( const char *fmt, ... )
{
	char buf[ MAXPRINTMSG ];
	va_list	argptr;
	va_start( argptr, fmt );
	Q_vsnprintf( buf, sizeof( buf ), fmt, argptr );
	va_end( argptr );

	ri.Printf( PRINT_ALL, "%s", buf );
}
#endif


/*
** R_HaveExtension
*/
static qboolean R_HaveExtension( const char *ext )
{
	const char *ptr = Q_stristr( gl_extensions, ext );
	if (ptr == NULL)
		return qfalse;
	ptr += strlen(ext);
	return ((*ptr == ' ') || (*ptr == '\0'));  // verify its complete string.
}


/*
** R_InitExtensions
*/
static void R_InitExtensions( void )
{
	GLint max_texture_size = 0;
	float version;
	size_t len;
	const char *err;

	if ( !qglGetString( GL_EXTENSIONS ) )
	{
		ri.Error( ERR_FATAL, "OpenGL installation is broken. Please fix video drivers and/or restart your system" );
	}

	// get our config strings
	Q_strncpyz( glConfig.vendor_string, (char *)qglGetString (GL_VENDOR), sizeof( glConfig.vendor_string ) );
	Q_strncpyz( glConfig.renderer_string, (char *)qglGetString (GL_RENDERER), sizeof( glConfig.renderer_string ) );
	len = strlen( glConfig.renderer_string );
	if ( len && glConfig.renderer_string[ len - 1 ] == '\n' )
		glConfig.renderer_string[ len - 1 ] = '\0';
	Q_strncpyz( glConfig.version_string, (char *)qglGetString( GL_VERSION ), sizeof( glConfig.version_string ) );

	Q_strncpyz( gl_extensions, (char *)qglGetString( GL_EXTENSIONS ), sizeof( gl_extensions ) );
	Q_strncpyz( glConfig.extensions_string, gl_extensions, sizeof( glConfig.extensions_string ) );

	version = Q_atof( (const char *)qglGetString( GL_VERSION ) );
	gl_version = (int)(version * 10.001);

	glConfig.textureCompression = TC_NONE;

	glConfig.textureEnvAddAvailable = qfalse;

	textureBorderClampAvailable = qfalse;

	textureFilterAnisotropic = qfalse;
	maxAnisotropy = 0;

	nonPowerOfTwoTextures = qfalse;
	textureSrgbAvailable = qfalse;
	framebufferSrgbAvailable = qfalse;

	qglLockArraysEXT = NULL;
	qglUnlockArraysEXT = NULL;

	glConfig.numTextureUnits = 1;
	qglMultiTexCoord2fARB = NULL;
	qglActiveTextureARB = NULL;
	qglClientActiveTextureARB = NULL;

	gl_clamp_mode = GL_CLAMP; // by default

	// OpenGL driver constants
	qglGetIntegerv( GL_MAX_TEXTURE_SIZE, &max_texture_size );
	glConfig.maxTextureSize = max_texture_size;

	// stubbed or broken drivers may have reported 0...
	if ( glConfig.maxTextureSize <= 0 )
		glConfig.maxTextureSize = 0;
	else if ( glConfig.maxTextureSize > 2048 )
		glConfig.maxTextureSize = 2048; // ResampleTexture() relies on that maximum

	if ( !r_allowExtensions->integer )
	{
		ri.Printf( PRINT_ALL, "*** IGNORING OPENGL EXTENSIONS ***\n" );
		return;
	}

	ri.Printf( PRINT_ALL, "Initializing OpenGL extensions\n" );

	if ( gl_version >= 21 || R_HaveExtension( "GL_EXT_texture_sRGB" ) ) {
		textureSrgbAvailable = qtrue;
		ri.Printf( PRINT_ALL, "...using sRGB texture formats\n" );
	} else {
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_sRGB not found\n" );
	}

	if ( gl_version >= 30 ||
		R_HaveExtension( "GL_ARB_framebuffer_sRGB" ) ||
		R_HaveExtension( "GL_EXT_framebuffer_sRGB" ) ) {
		framebufferSrgbAvailable = qtrue;
		ri.Printf( PRINT_ALL, "...using framebuffer sRGB state control\n" );
	} else {
		ri.Printf( PRINT_ALL, "...GL_ARB_framebuffer_sRGB not found\n" );
	}

	if ( R_HaveExtension( "GL_EXT_texture_edge_clamp" ) || R_HaveExtension( "GL_SGIS_texture_edge_clamp" ) ) {
		gl_clamp_mode = GL_CLAMP_TO_EDGE;
		ri.Printf( PRINT_ALL, "...using GL_EXT_texture_edge_clamp\n" );
	} else {
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_edge_clamp not found\n" );
		ri.Printf( PRINT_ALL, S_COLOR_YELLOW "...Degraded texture support likely!\n" );
	}

	// GL_EXT_texture_compression_s3tc
	if ( R_HaveExtension( "GL_ARB_texture_compression" ) &&
		 R_HaveExtension( "GL_EXT_texture_compression_s3tc" ) )
	{
		if ( r_ext_compressed_textures->integer ){
			glConfig.textureCompression = TC_S3TC_ARB;
			ri.Printf( PRINT_ALL, "...using GL_EXT_texture_compression_s3tc\n" );
		} else {
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_compression_s3tc\n" );
		}
	} else {
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_compression_s3tc not found\n" );
	}

	// GL_S3_s3tc
	if ( glConfig.textureCompression == TC_NONE && r_ext_compressed_textures->integer ) {
		if ( R_HaveExtension( "GL_S3_s3tc" ) ) {
			if ( r_ext_compressed_textures->integer ) {
				glConfig.textureCompression = TC_S3TC;
				ri.Printf( PRINT_ALL, "...using GL_S3_s3tc\n" );
			} else {
				glConfig.textureCompression = TC_NONE;
				ri.Printf( PRINT_ALL, "...ignoring GL_S3_s3tc\n" );
			}
		} else {
			ri.Printf( PRINT_ALL, "...GL_S3_s3tc not found\n" );
		}
	}

	// GL_EXT_texture_env_add
	if ( R_HaveExtension( "EXT_texture_env_add" ) ) {
		if ( r_ext_texture_env_add->integer ) {
			glConfig.textureEnvAddAvailable = qtrue;
			ri.Printf( PRINT_ALL, "...using GL_EXT_texture_env_add\n" );
		} else {
			glConfig.textureEnvAddAvailable = qfalse;
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_env_add\n" );
		}
	} else {
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_env_add not found\n" );
	}

	// GL_ARB_texture_border_clamp
	if ( R_HaveExtension( "GL_ARB_texture_border_clamp" ) ) {
		textureBorderClampAvailable = qtrue;
		ri.Printf( PRINT_ALL, "...using GL_ARB_texture_border_clamp\n" );
	} else {
		ri.Printf( PRINT_ALL, "...GL_ARB_texture_border_clamp not found\n" );
	}

	// GL_ARB_multitexture
	if ( R_HaveExtension( "GL_ARB_multitexture" ) )
	{
		if ( r_ext_multitexture->integer )
		{
			R_AssignProcAddress( &qglMultiTexCoord2fARB, sizeof( qglMultiTexCoord2fARB ), "glMultiTexCoord2fARB" );
			R_AssignProcAddress( &qglActiveTextureARB, sizeof( qglActiveTextureARB ), "glActiveTextureARB" );
			R_AssignProcAddress( &qglClientActiveTextureARB, sizeof( qglClientActiveTextureARB ), "glClientActiveTextureARB" );

			if ( qglActiveTextureARB && qglClientActiveTextureARB )
			{
				GLint textureUnits = 0;

				qglGetIntegerv( GL_MAX_ACTIVE_TEXTURES_ARB, &textureUnits );

				if ( textureUnits > 1 )
				{
					GLint max_shader_units = 0;
					GLint max_bind_units = 0;

					qglGetIntegerv( GL_MAX_TEXTURE_IMAGE_UNITS, &max_shader_units );
					qglGetIntegerv( GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &max_bind_units );

					if ( max_bind_units > max_shader_units )
						max_bind_units = max_shader_units;
					if ( max_bind_units > MAX_TEXTURE_UNITS )
						max_bind_units = MAX_TEXTURE_UNITS;

					glConfig.numTextureUnits = MAX( textureUnits, max_bind_units );
					ri.Printf( PRINT_ALL, "...using GL_ARB_multitexture\n" );
				}
				else
				{
					qglMultiTexCoord2fARB = NULL;
					qglActiveTextureARB = NULL;
					qglClientActiveTextureARB = NULL;
					ri.Printf( PRINT_ALL, "...not using GL_ARB_multitexture, < 2 texture units\n" );
				}
			}
		}
		else
		{
			ri.Printf( PRINT_ALL, "...ignoring GL_ARB_multitexture\n" );
		}
	}
	else
	{
		ri.Printf( PRINT_ALL, "...GL_ARB_multitexture not found\n" );
	}

	// GL_EXT_compiled_vertex_array
	if ( R_HaveExtension( "GL_EXT_compiled_vertex_array" ) )
	{
		if ( r_ext_compiled_vertex_array->integer )
		{
			ri.Printf( PRINT_ALL, "...using GL_EXT_compiled_vertex_array\n" );
			R_AssignProcAddress( &qglLockArraysEXT, sizeof( qglLockArraysEXT ), "glLockArraysEXT" );
			R_AssignProcAddress( &qglUnlockArraysEXT, sizeof( qglUnlockArraysEXT ), "glUnlockArraysEXT" );
			if ( !qglLockArraysEXT || !qglUnlockArraysEXT ) {
				ri.Error( ERR_FATAL, "bad getprocaddress" );
			}
		}
		else
		{
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_compiled_vertex_array\n" );
		}
	}
	else
	{
		ri.Printf( PRINT_ALL, "...GL_EXT_compiled_vertex_array not found\n" );
	}

	if ( R_HaveExtension( "GL_EXT_texture_filter_anisotropic" ) )
	{
		if ( r_ext_texture_filter_anisotropic->integer ) {
			qglGetIntegerv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy );
			if ( maxAnisotropy <= 0 ) {
				ri.Printf( PRINT_ALL, "...GL_EXT_texture_filter_anisotropic not properly supported!\n" );
				maxAnisotropy = 0;
			}
			else
			{
				ri.Printf( PRINT_ALL, "...using GL_EXT_texture_filter_anisotropic (max: %i)\n", maxAnisotropy );
				textureFilterAnisotropic = qtrue;
				maxAnisotropy = MIN( r_ext_max_anisotropy->integer, maxAnisotropy );
			}
		}
		else
		{
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_filter_anisotropic\n" );
		}
	}
	else
	{
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_filter_anisotropic not found\n" );
	}

	if ( R_HaveExtension( "GL_ARB_vertex_program" ) && R_HaveExtension( "GL_ARB_fragment_program" ) )
	{
		err = R_ResolveSymbols( arb_procs, ARRAY_LEN( arb_procs ) );
		if ( err )
		{
			ri.Printf( PRINT_WARNING, "Error resolving ARB program function '%s'\n", err );
			qglGenProgramsARB = NULL; // indicates presence of ARB shaders functionality
		}
		else
		{
			ri.Printf( PRINT_ALL, "...using ARB vertex/fragment programs\n" );
		}
	}

#ifdef USE_VBO
	if ( R_HaveExtension( "ARB_vertex_buffer_object" ) && qglActiveTextureARB )
	{
		err = R_ResolveSymbols( vbo_procs, ARRAY_LEN( vbo_procs ) );
		if ( err )
		{
			ri.Printf( PRINT_WARNING, "Error resolving VBO function '%s'\n", err );
			qglBindBufferARB = NULL; // indicates presence of VBO functionality
		}
		else
		{
			ri.Printf( PRINT_ALL, "...using ARB vertex buffer objects\n" );
		}
	}
#endif // USE_VBO

	if ( R_HaveExtension( "GL_ARB_occlusion_query" ) )
	{
		err = R_ResolveSymbols( occlusion_query_procs, ARRAY_LEN( occlusion_query_procs ) );
		if ( err )
		{
			ri.Printf( PRINT_WARNING, "Error resolving occlusion query function '%s'\n", err );
			qglGenQueriesARB = NULL;
			qglDeleteQueriesARB = NULL;
			qglBeginQueryARB = NULL;
			qglEndQueryARB = NULL;
		}
		else
		{
			ri.Printf( PRINT_ALL, "...using ARB occlusion queries\n" );
		}
	}
	else
	{
		ri.Printf( PRINT_ALL, "...GL_ARB_occlusion_query not found\n" );
	}

#ifdef USE_FBO
	if ( R_HaveExtension( "GL_EXT_framebuffer_object" ) && R_HaveExtension( "GL_EXT_framebuffer_blit" ) )
	{
		err = R_ResolveSymbols( fbo_procs, ARRAY_LEN( fbo_procs ) );
		if ( err )
		{
			ri.Printf( PRINT_WARNING, "Error resolving FBO function '%s'\n", err );
			qglGenFramebuffers = NULL; // indicates presence of FBO functionality
		}
		else
		{
			// resolve optional fbo functions, without any warnings
			R_ResolveSymbols( fbo_opt_procs, ARRAY_LEN( fbo_opt_procs ) );
		}
	}
#endif // USE_FBO
	
	// Check if dynamic lights are available
#ifdef USE_PMLIGHT	
	if (r_dlightMode->integer && !qglGenProgramsARB) 
	{
			ri.Printf( PRINT_ALL, "...Per-pixel dynamic lights disabled. Missing ARB shader support\n" );
	}
#endif	
}


/*
** InitOpenGL
**
** This function is responsible for initializing a valid OpenGL subsystem.  This
** is done by calling GLimp_Init (which gives us a working OGL subsystem) then
** setting variables, checking GL constants, and reporting the gfx system config
** to the user.
*/
static void InitOpenGL( void )
{
	//
	// initialize OS specific portions of the renderer
	//
	// GLimp_Init directly or indirectly references the following cvars:
	//		- r_fullscreen
	//		- r_mode
	//		- r_(color|depth|stencil)bits
	//		- r_ignorehwgamma
	//		- r_gamma
	//

	if ( glConfig.vidWidth == 0 )
	{
		const char *err;

		if ( !ri.GLimp_Init )
		{
			ri.Error( ERR_FATAL, "OpenGL interface is not initialized" );
		}

		ri.GLimp_Init( &glConfig );

		R_ClearSymTables();

		err = R_ResolveSymbols( core_procs, ARRAY_LEN( core_procs ) );
		if ( err )
			ri.Error( ERR_FATAL, "Error resolving core OpenGL function '%s'", err );

		R_InitExtensions();
		GLX_CompatOnOpenGLReady( &glConfig, gl_extensions );

		gls.windowWidth = glConfig.vidWidth;
		gls.windowHeight = glConfig.vidHeight;

		gls.captureWidth = glConfig.vidWidth;
		gls.captureHeight = glConfig.vidHeight;

		ri.CL_SetScaling( 1.0, gls.captureWidth, gls.captureHeight );

#ifdef USE_FBO
		if ( r_fbo->integer && qglGenProgramsARB && qglGenFramebuffers )
		{
			if ( r_renderScale->integer )
			{
				glConfig.vidWidth = r_renderWidth->integer;
				glConfig.vidHeight = r_renderHeight->integer;
			}

			gls.captureWidth = glConfig.vidWidth;
			gls.captureHeight = glConfig.vidHeight;

			ri.CL_SetScaling( 1.0, gls.captureWidth, gls.captureHeight );

			if ( r_ext_supersample->integer )
			{
				glConfig.vidWidth *= 2;
				glConfig.vidHeight *= 2;
				ri.CL_SetScaling( 2.0, gls.captureWidth, gls.captureHeight );
			}
		}
#endif

		QGL_InitARB();

		glConfig.deviceSupportsGamma = qfalse;

		ri.GLimp_InitGamma( &glConfig );

		gls.deviceSupportsGamma = glConfig.deviceSupportsGamma;

		if ( r_ignorehwgamma->integer )
			glConfig.deviceSupportsGamma = qfalse;

		// print info
		GfxInfo();

		gls.initTime = ri.Milliseconds();
	}
	else
	{
#ifdef USE_FBO
		QGL_SetRenderScale( qfalse );
#endif
		GLX_CompatOnOpenGLReady( &glConfig, gl_extensions );
	}

	if ( !qglViewport ) // might happen after REF_KEEP_WINDOW
	{
		const char *err = R_ResolveSymbols( core_procs, ARRAY_LEN( core_procs ) );
		if ( err )
			ri.Error( ERR_FATAL, "Error resolving core OpenGL function '%s'", err );

		R_InitExtensions();
		GLX_CompatOnOpenGLReady( &glConfig, gl_extensions );

		QGL_InitARB();

		// print info
		GfxInfo();

		gls.initTime = ri.Milliseconds();
	}

	// set default state
	GL_SetDefaultState();

	tr.inited = qtrue;
}


/*
==================
GL_CheckErrors
==================
*/
void GL_CheckErrors( void ) {
    int		err;
    const char *s;
    char buf[32];

    err = qglGetError();
    if ( err == GL_NO_ERROR ) {
        return;
    }
    if ( r_ignoreGLErrors->integer ) {
        return;
    }
    switch( err ) {
        case GL_INVALID_ENUM:
            s = "GL_INVALID_ENUM";
            break;
        case GL_INVALID_VALUE:
            s = "GL_INVALID_VALUE";
            break;
        case GL_INVALID_OPERATION:
            s = "GL_INVALID_OPERATION";
            break;
        case GL_STACK_OVERFLOW:
            s = "GL_STACK_OVERFLOW";
            break;
        case GL_STACK_UNDERFLOW:
            s = "GL_STACK_UNDERFLOW";
            break;
        case GL_OUT_OF_MEMORY:
            s = "GL_OUT_OF_MEMORY";
            break;
        default:
            Com_sprintf( buf, sizeof(buf), "%i", err);
            s = buf;
            break;
    }

    ri.Error( ERR_FATAL, "GL_CheckErrors: %s", s );
}


/*
==============================================================================

						SCREEN SHOTS

NOTE TTimo
some thoughts about the screenshots system:
screenshots get written in fs_homepath + fs_gamedir
vanilla q3 .. baseq3/screenshots/ *.tga
team arena .. missionpack/screenshots/ *.tga

two commands: "screenshot" and "screenshotJPEG"
we use statics to store a count and start writing the first screenshot/screenshot????.tga (.jpg) available
(with FS_FileExists / FS_FOpenFileWrite calls)
FIXME: the statics don't get a reinit between fs_game changes

==============================================================================
*/

/*
==================
RB_ReadPixels

Reads an image but takes care of alignment issues for reading RGB images.

Reads a minimum offset for where the RGB data starts in the image from
integer stored at pointer offset. When the function has returned the actual
offset was written back to address offset. This address will always have an
alignment of packAlign to ensure efficient copying.

Stores the length of padding after a line of pixels to address padlen

Return value must be freed with ri.Hunk_FreeTempMemory()
==================
*/
static byte *RB_ReadPixels(int x, int y, int width, int height, size_t *offset, int *padlen, int lineAlign )
{
	byte *buffer, *bufstart;
	int padwidth, linelen;
	int	bufAlign;
	GLint packAlign;

	qglGetIntegerv(GL_PACK_ALIGNMENT, &packAlign);

	linelen = width * 3;

	if ( packAlign < lineAlign )
		padwidth = PAD(linelen, lineAlign);
	else
		padwidth = PAD(linelen, packAlign);

	bufAlign = MAX( packAlign, 16 ); // for SIMD

	// Allocate a few more bytes so that we can choose an alignment we like
	buffer = ri.Hunk_AllocateTempMemory(padwidth * height + *offset + bufAlign - 1);
	bufstart = PADP((intptr_t) buffer + *offset, bufAlign);

	qglReadPixels( x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, bufstart );

	*offset = bufstart - buffer;
	*padlen = PAD(linelen, packAlign) - linelen;

	return buffer;
}

typedef enum {
	SCREENSHOT_ALIGN_TOP_LEFT,
	SCREENSHOT_ALIGN_TOP,
	SCREENSHOT_ALIGN_TOP_RIGHT,
	SCREENSHOT_ALIGN_LEFT,
	SCREENSHOT_ALIGN_CENTER,
	SCREENSHOT_ALIGN_RIGHT,
	SCREENSHOT_ALIGN_BOTTOM_LEFT,
	SCREENSHOT_ALIGN_BOTTOM,
	SCREENSHOT_ALIGN_BOTTOM_RIGHT
} screenshotAlignment_t;

static screenshotAlignment_t R_ParseScreenshotAlignment( const char *value, screenshotAlignment_t fallback )
{
	if ( !value || !value[0] ) {
		return fallback;
	}

	if ( !Q_stricmp( value, "top-left" ) ) {
		return SCREENSHOT_ALIGN_TOP_LEFT;
	}
	if ( !Q_stricmp( value, "top" ) ) {
		return SCREENSHOT_ALIGN_TOP;
	}
	if ( !Q_stricmp( value, "top-right" ) ) {
		return SCREENSHOT_ALIGN_TOP_RIGHT;
	}
	if ( !Q_stricmp( value, "left" ) ) {
		return SCREENSHOT_ALIGN_LEFT;
	}
	if ( !Q_stricmp( value, "center" ) ) {
		return SCREENSHOT_ALIGN_CENTER;
	}
	if ( !Q_stricmp( value, "right" ) ) {
		return SCREENSHOT_ALIGN_RIGHT;
	}
	if ( !Q_stricmp( value, "bottom-left" ) ) {
		return SCREENSHOT_ALIGN_BOTTOM_LEFT;
	}
	if ( !Q_stricmp( value, "bottom" ) ) {
		return SCREENSHOT_ALIGN_BOTTOM;
	}
	if ( !Q_stricmp( value, "bottom-right" ) ) {
		return SCREENSHOT_ALIGN_BOTTOM_RIGHT;
	}

	return fallback;
}

static void R_SanitizeScreenshotToken( const char *in, char *out, int outSize )
{
	int i, c;

	if ( outSize <= 0 ) {
		return;
	}

	if ( !in || !in[0] ) {
		Q_strncpyz( out, "none", outSize );
		return;
	}

	for ( i = 0; i < outSize - 1 && ( c = (unsigned char)in[i] ) != 0; i++ ) {
		if ( ( c >= '0' && c <= '9' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' ) || c == '-' || c == '_' ) {
			out[i] = c;
		} else {
			out[i] = '_';
		}
	}
	out[i] = '\0';

	if ( out[0] == '\0' ) {
		Q_strncpyz( out, "none", outSize );
	}
}

static qboolean R_LoadScreenshotImage( const char *name, byte **pic, int *width, int *height )
{
	const char *ext;
	char strippedName[MAX_QPATH];
	int i;
	static const char *extensions[] = { "png", "tga", "jpg", "jpeg", "bmp" };

	*pic = NULL;
	*width = 0;
	*height = 0;

	if ( !name || !name[0] ) {
		return qfalse;
	}

	ext = COM_GetExtension( name );

	if ( ext && *ext ) {
		if ( !Q_stricmp( ext, "png" ) ) {
			R_LoadPNG( name, pic, width, height );
		} else if ( !Q_stricmp( ext, "tga" ) ) {
			R_LoadTGA( name, pic, width, height );
		} else if ( !Q_stricmp( ext, "jpg" ) || !Q_stricmp( ext, "jpeg" ) ) {
			R_LoadJPG( name, pic, width, height );
		} else if ( !Q_stricmp( ext, "bmp" ) ) {
			R_LoadBMP( name, pic, width, height );
		}
		return *pic != NULL;
	}

	COM_StripExtension( name, strippedName, sizeof( strippedName ) );

	for ( i = 0; i < ARRAY_LEN( extensions ) && !*pic; i++ ) {
		char fileName[MAX_QPATH];

		Com_sprintf( fileName, sizeof( fileName ), "%s.%s", strippedName, extensions[i] );
		if ( !Q_stricmp( extensions[i], "png" ) ) {
			R_LoadPNG( fileName, pic, width, height );
		} else if ( !Q_stricmp( extensions[i], "tga" ) ) {
			R_LoadTGA( fileName, pic, width, height );
		} else if ( !Q_stricmp( extensions[i], "bmp" ) ) {
			R_LoadBMP( fileName, pic, width, height );
		} else {
			R_LoadJPG( fileName, pic, width, height );
		}
	}

	return *pic != NULL;
}

static void R_GetAlignedRect( screenshotAlignment_t align, int outerX, int outerY, int outerWidth, int outerHeight,
	int innerWidth, int innerHeight, int margin, int *x, int *y )
{
	int minX, maxX, minY, maxY;

	minX = outerX + margin;
	minY = outerY + margin;
	maxX = outerX + outerWidth - innerWidth - margin;
	maxY = outerY + outerHeight - innerHeight - margin;

	if ( maxX < minX ) {
		minX = maxX = outerX + ( outerWidth - innerWidth ) / 2;
	}
	if ( maxY < minY ) {
		minY = maxY = outerY + ( outerHeight - innerHeight ) / 2;
	}

	switch ( align ) {
		case SCREENSHOT_ALIGN_TOP_LEFT:
			*x = minX;
			*y = minY;
			break;
		case SCREENSHOT_ALIGN_TOP:
			*x = outerX + ( outerWidth - innerWidth ) / 2;
			*y = minY;
			break;
		case SCREENSHOT_ALIGN_TOP_RIGHT:
			*x = maxX;
			*y = minY;
			break;
		case SCREENSHOT_ALIGN_LEFT:
			*x = minX;
			*y = outerY + ( outerHeight - innerHeight ) / 2;
			break;
		case SCREENSHOT_ALIGN_CENTER:
			*x = outerX + ( outerWidth - innerWidth ) / 2;
			*y = outerY + ( outerHeight - innerHeight ) / 2;
			break;
		case SCREENSHOT_ALIGN_RIGHT:
			*x = maxX;
			*y = outerY + ( outerHeight - innerHeight ) / 2;
			break;
		case SCREENSHOT_ALIGN_BOTTOM_LEFT:
			*x = minX;
			*y = maxY;
			break;
		case SCREENSHOT_ALIGN_BOTTOM:
			*x = outerX + ( outerWidth - innerWidth ) / 2;
			*y = maxY;
			break;
		case SCREENSHOT_ALIGN_BOTTOM_RIGHT:
		default:
			*x = maxX;
			*y = maxY;
			break;
	}
}

static void R_ApplyScreenshotWatermark( byte *rgb, int width, int height, int padlen )
{
	byte *watermark;
	int watermarkWidth, watermarkHeight;
	int safeX, safeY, safeWidth, safeHeight;
	int drawX, drawY;
	int x, y;
	int margin;
	screenshotAlignment_t watermarkAlign;
	screenshotAlignment_t screenAlign;

	if ( !rb_allowScreenshotWatermark || !r_screenshotWatermark || !r_screenshotWatermark->string[0] ) {
		return;
	}

	if ( !R_LoadScreenshotImage( r_screenshotWatermark->string, &watermark, &watermarkWidth, &watermarkHeight ) ) {
		ri.Printf( PRINT_WARNING, "WARNING: could not load screenshot watermark '%s'\n", r_screenshotWatermark->string );
		return;
	}

	if ( watermarkWidth <= 0 || watermarkHeight <= 0 ) {
		ri.Free( watermark );
		return;
	}

	margin = r_screenshotWatermarkMargin ? r_screenshotWatermarkMargin->integer : 0;
	if ( margin < 0 ) {
		margin = 0;
	}

	screenAlign = R_ParseScreenshotAlignment( r_screenshotWatermarkScreenAlignment ? r_screenshotWatermarkScreenAlignment->string : "",
		SCREENSHOT_ALIGN_CENTER );
	watermarkAlign = R_ParseScreenshotAlignment( r_screenshotWatermarkAlignment ? r_screenshotWatermarkAlignment->string : "",
		SCREENSHOT_ALIGN_BOTTOM_RIGHT );

	safeWidth = width;
	safeHeight = ( safeWidth * 3 ) / 4;

	if ( safeHeight > height ) {
		safeHeight = height;
		safeWidth = ( safeHeight * 4 ) / 3;
	}

	R_GetAlignedRect( screenAlign, 0, 0, width, height, safeWidth, safeHeight, 0, &safeX, &safeY );
	R_GetAlignedRect( watermarkAlign, safeX, safeY, safeWidth, safeHeight, watermarkWidth, watermarkHeight, margin, &drawX, &drawY );

	for ( y = 0; y < watermarkHeight; y++ ) {
		int dstY = drawY + y;

		if ( dstY < 0 || dstY >= height ) {
			continue;
		}

		for ( x = 0; x < watermarkWidth; x++ ) {
			const byte *src = watermark + ( y * watermarkWidth + x ) * 4;
			byte *dst;
			int dstX = drawX + x;
			int alpha;

			if ( dstX < 0 || dstX >= width ) {
				continue;
			}

			alpha = src[3];
			if ( alpha <= 0 ) {
				continue;
			}

			dst = rgb + ( height - 1 - dstY ) * ( width * 3 + padlen ) + dstX * 3;
			dst[0] = (byte)( ( dst[0] * ( 255 - alpha ) + src[0] * alpha ) / 255 );
			dst[1] = (byte)( ( dst[1] * ( 255 - alpha ) + src[1] * alpha ) / 255 );
			dst[2] = (byte)( ( dst[2] * ( 255 - alpha ) + src[2] * alpha ) / 255 );
		}
	}

	ri.Free( watermark );
}

static void R_ViewAxisToAngles( vec3_t axis[3], vec3_t angles )
{
	vec3_t baseAxis[3];
	float dotLeft, dotUp;

	vectoangles( axis[0], angles );
	AnglesToAxis( angles, baseAxis );

	dotLeft = DotProduct( axis[1], baseAxis[1] );
	dotUp = DotProduct( axis[1], baseAxis[2] );
	angles[ROLL] = atan2( dotUp, dotLeft ) * 180.0f / M_PI;
}

typedef enum {
	SCREENSHOT_CAPTURE_SDR_SRGB = 0,
	SCREENSHOT_CAPTURE_HDR_SCENE_LINEAR = 1,
	SCREENSHOT_CAPTURE_HDR_OUTPUT = 2
} screenshotCaptureMode_t;

static screenshotCaptureMode_t R_ScreenshotCaptureMode( void )
{
	int mode = r_screenshotCaptureMode ? r_screenshotCaptureMode->integer : SCREENSHOT_CAPTURE_SDR_SRGB;

	if ( mode < SCREENSHOT_CAPTURE_SDR_SRGB || mode > SCREENSHOT_CAPTURE_HDR_OUTPUT ) {
		mode = SCREENSHOT_CAPTURE_SDR_SRGB;
	}

	return (screenshotCaptureMode_t)mode;
}

static const char *R_ScreenshotCaptureModeName( void )
{
	switch ( R_ScreenshotCaptureMode() ) {
	case SCREENSHOT_CAPTURE_HDR_SCENE_LINEAR:
		return "hdr-scene-linear";
	case SCREENSHOT_CAPTURE_HDR_OUTPUT:
		return "hdr-output";
	case SCREENSHOT_CAPTURE_SDR_SRGB:
	default:
		return "sdr-srgb";
	}
}

static const char *R_ScreenshotCaptureColorSpaceName( void )
{
	// Current screenshot encoders consume post-output RGB bytes. Explicit HDR
	// modes are policy markers until a float/metadata export path is wired.
	return "sdr-srgb";
}

static void R_WarnExplicitHdrScreenshotCapture( void )
{
	if ( R_ScreenshotCaptureMode() == SCREENSHOT_CAPTURE_SDR_SRGB ) {
		return;
	}

	ri.Printf( PRINT_WARNING,
		"WARNING: r_screenshotCaptureMode %s currently writes SDR sRGB screenshots; HDR export requires a future explicit capture path.\n",
		R_ScreenshotCaptureModeName() );
}

static void R_WriteScreenshotViewpos( const char *imageFileName, const vec3_t origin, vec3_t axis[3] )
{
	char textName[MAX_OSPATH];
	char text[768];
	char mapName[MAX_QPATH];
	vec3_t angles;
	int len;

	if ( !r_screenshotWriteViewpos || !r_screenshotWriteViewpos->integer || !imageFileName || !imageFileName[0] ) {
		return;
	}

	COM_StripExtension( imageFileName, textName, sizeof( textName ) );
	Q_strcat( textName, sizeof( textName ), ".txt" );

	if ( tr.world && tr.world->baseName[0] ) {
		R_SanitizeScreenshotToken( tr.world->baseName, mapName, sizeof( mapName ) );
	} else {
		Q_strncpyz( mapName, "nomap", sizeof( mapName ) );
	}

	R_ViewAxisToAngles( axis, angles );

	len = Com_sprintf( text, sizeof( text ),
		"map %s\norigin %.3f %.3f %.3f\nangles %.3f %.3f %.3f\n"
		"captureMode %s\ncaptureColorSpace %s\nsetviewpos %.3f %.3f %.3f %.3f %.3f %.3f\n",
		mapName,
		origin[0], origin[1], origin[2],
		angles[0], angles[1], angles[2],
		R_ScreenshotCaptureModeName(),
		R_ScreenshotCaptureColorSpaceName(),
		origin[0], origin[1], origin[2], angles[0], angles[1], angles[2] );

	ri.FS_WriteFile( textName, text, len );
}

static void R_AppendScreenshotToken( char *out, int outSize, const char *value )
{
	if ( value && value[0] ) {
		Q_strcat( out, outSize, value );
	}
}

static qboolean R_ExpandScreenshotPattern( char *out, int outSize, const char *pattern, const char *commandName,
	const char *faceName, const char *fileExt, int iter, qboolean *usedIter )
{
	char tokenName[64];
	char tokenValue[128];
	char mapName[MAX_QPATH];
	qtime_t t;
	int i;

	ri.Com_RealTime( &t );
	out[0] = '\0';
	*usedIter = qfalse;

	if ( tr.world && tr.world->baseName[0] ) {
		R_SanitizeScreenshotToken( tr.world->baseName, mapName, sizeof( mapName ) );
	} else {
		Q_strncpyz( mapName, "nomap", sizeof( mapName ) );
	}

	for ( i = 0; pattern && pattern[i] != '\0'; i++ ) {
		if ( pattern[i] == '{' ) {
			const char *end = strchr( pattern + i + 1, '}' );
			if ( end ) {
				int tokenLen = end - ( pattern + i + 1 );

				if ( tokenLen > 0 && tokenLen < (int)sizeof( tokenName ) ) {
					char *format;
					int padWidth = 0;

					Q_strncpyz( tokenName, pattern + i + 1, tokenLen + 1 );
					format = strchr( tokenName, ':' );
					if ( format ) {
						*format++ = '\0';
						padWidth = atoi( format );
						if ( padWidth < 0 ) {
							padWidth = 0;
						}
					}

					tokenValue[0] = '\0';

					if ( !Q_stricmp( tokenName, "map" ) ) {
						Q_strncpyz( tokenValue, mapName, sizeof( tokenValue ) );
					} else if ( !Q_stricmp( tokenName, "date" ) ) {
						Com_sprintf( tokenValue, sizeof( tokenValue ), "%04d%02d%02d",
							1900 + t.tm_year, 1 + t.tm_mon, t.tm_mday );
					} else if ( !Q_stricmp( tokenName, "time" ) ) {
						Com_sprintf( tokenValue, sizeof( tokenValue ), "%02d%02d%02d",
							t.tm_hour, t.tm_min, t.tm_sec );
					} else if ( !Q_stricmp( tokenName, "datetime" ) ) {
						Com_sprintf( tokenValue, sizeof( tokenValue ), "%04d%02d%02d-%02d%02d%02d",
							1900 + t.tm_year, 1 + t.tm_mon, t.tm_mday,
							t.tm_hour, t.tm_min, t.tm_sec );
					} else if ( !Q_stricmp( tokenName, "iter" ) ) {
						if ( padWidth > 0 ) {
							Com_sprintf( tokenValue, sizeof( tokenValue ), "%0*d", padWidth, iter );
						} else {
							Com_sprintf( tokenValue, sizeof( tokenValue ), "%d", iter );
						}
						*usedIter = qtrue;
					} else if ( !Q_stricmp( tokenName, "cmd" ) ) {
						R_SanitizeScreenshotToken( commandName, tokenValue, sizeof( tokenValue ) );
					} else if ( !Q_stricmp( tokenName, "face" ) ) {
						if ( faceName && faceName[0] ) {
							R_SanitizeScreenshotToken( faceName, tokenValue, sizeof( tokenValue ) );
						}
					} else if ( !Q_stricmp( tokenName, "type" ) ) {
						Q_strncpyz( tokenValue, fileExt, sizeof( tokenValue ) );
					}

					if ( tokenValue[0] ) {
						R_AppendScreenshotToken( out, outSize, tokenValue );
						i += tokenLen + 1;
						continue;
					}
				}
			}
		}

		tokenValue[0] = pattern[i];
		tokenValue[1] = '\0';
		R_AppendScreenshotToken( out, outSize, tokenValue );
	}

	return out[0] != '\0';
}


/*
==================
RB_TakeScreenshotPNG
==================
*/
void RB_TakeScreenshotPNG( int x, int y, int width, int height, const char *fileName )
{
	byte *buffer;
	size_t offset = 0, memcount;
	int padlen;

	buffer = RB_ReadPixels( x, y, width, height, &offset, &padlen, 0 );
	memcount = ( width * 3 + padlen ) * height;

	R_GammaCorrect( buffer + offset, memcount );
	R_ApplyScreenshotWatermark( buffer + offset, width, height, padlen );

	R_SavePNG( fileName, width, height, buffer + offset, padlen );
	R_WriteScreenshotViewpos( fileName, backEnd.refdef.vieworg, backEnd.refdef.viewaxis );
	ri.Hunk_FreeTempMemory( buffer );
}


/*
==================
RB_TakeScreenshot
==================
*/
void RB_TakeScreenshot( int x, int y, int width, int height, const char *fileName )
{
	const int header_size = 18;
	byte *allbuf, *buffer;
	byte *srcptr, *destptr;
	byte *endline, *endmem;
	byte temp;
	int linelen, padlen;
	size_t offset, memcount;

	offset = header_size;
	allbuf = RB_ReadPixels( x, y, width, height, &offset, &padlen, 0 );
	buffer = allbuf + offset - header_size;

	Com_Memset( buffer, 0, header_size );
	buffer[2] = 2;		// uncompressed type
	buffer[12] = width & 255;
	buffer[13] = width >> 8;
	buffer[14] = height & 255;
	buffer[15] = height >> 8;
	buffer[16] = 24;	// pixel size

	linelen = width * 3;

	// gamma correction and watermarking happen on the raw RGB buffer
	R_GammaCorrect( allbuf + offset, ( linelen + padlen ) * height );
	R_ApplyScreenshotWatermark( allbuf + offset, width, height, padlen );

	// swap rgb to bgr and remove padding from line endings
	srcptr = destptr = allbuf + offset;
	endmem = srcptr + (linelen + padlen) * height;

	while(srcptr < endmem)
	{
		endline = srcptr + linelen;

		while(srcptr < endline)
		{
			temp = srcptr[0];
			*destptr++ = srcptr[2];
			*destptr++ = srcptr[1];
			*destptr++ = temp;

			srcptr += 3;
		}

		// Skip the pad
		srcptr += padlen;
	}

	memcount = linelen * height;

	ri.FS_WriteFile( fileName, buffer, memcount + header_size );
	R_WriteScreenshotViewpos( fileName, backEnd.refdef.vieworg, backEnd.refdef.viewaxis );

	ri.Hunk_FreeTempMemory( allbuf );
}


/*
==================
RB_TakeScreenshotJPEG
==================
*/
void RB_TakeScreenshotJPEG( int x, int y, int width, int height, const char *fileName )
{
	byte *buffer;
	size_t offset = 0, memcount;
	int padlen;

	buffer = RB_ReadPixels(x, y, width, height, &offset, &padlen, 0);
	memcount = (width * 3 + padlen) * height;

	// gamma correction
	R_GammaCorrect( buffer + offset, memcount );
	R_ApplyScreenshotWatermark( buffer + offset, width, height, padlen );

	ri.CL_SaveJPG( fileName, r_screenshotJpegQuality->integer, width, height, buffer + offset, padlen );
	R_WriteScreenshotViewpos( fileName, backEnd.refdef.vieworg, backEnd.refdef.viewaxis );
	ri.Hunk_FreeTempMemory( buffer );
}


static void FillBMPHeader( byte *buffer, int width, int height, int memcount, int header_size )
{
	int filesize;
	Com_Memset( buffer, 0, header_size );

	// bitmap file header
	buffer[0] = 'B';
	buffer[1] = 'M';
	filesize = memcount + header_size;
	buffer[2] = (filesize >> 0) & 255;
	buffer[3] = (filesize >> 8) & 255;
	buffer[4] = (filesize >> 16) & 255;
	buffer[5] = (filesize >> 24) & 255;
	buffer[10] = header_size; // data offset

	// bitmap info header
	buffer[14] = 40; // size of this header
	buffer[18] = (width >> 0) & 255;
	buffer[19] = (width >> 8) & 255;
	buffer[20] = (width >> 16) & 255;
	buffer[21] = (width >> 24) & 255;

	buffer[22] = (height >> 0) & 255;
	buffer[23] = (height >> 8) & 255;
	buffer[24] = (height >> 16) & 255;
	buffer[25] = (height >> 24) & 255;
	buffer[26] = 1; // number of color planes
	buffer[28] = 24; // bpp

	buffer[34] = (memcount >> 0) & 255;
	buffer[35] = (memcount >> 8) & 255;
	buffer[36] = (memcount >> 16) & 255;
	buffer[37] = (memcount >> 24) & 255;
	buffer[38] = 0xC4; // horizontal dpi
	buffer[39] = 0x0E; // horizontal dpi
	buffer[42] = 0xC4; // vertical dpi
	buffer[43] = 0x0E; // vertical dpi
}


/*
==================
RB_TakeScreenshotBMP
==================
*/
void RB_TakeScreenshotBMP( int x, int y, int width, int height, const char *fileName, int clipboardOnly )
{
	byte *allbuf;
	byte *buffer; // destination buffer
	byte *srcptr, *srcline;
	byte *destptr, *dstline;
	byte *endmem;
	byte temp[4];
	size_t memcount, offset;
	const int header_size = 54; // bitmapfileheader(14) + bitmapinfoheader(40)
	int scanlen, padlen;
	int scanpad, len;

	offset = header_size;

	allbuf = RB_ReadPixels( x, y, width, height, &offset, &padlen, 4 );
	buffer = allbuf + offset;

	// scanline length
	scanlen = PAD( width*3, 4 );
	scanpad = scanlen - width*3;
	memcount = scanlen * height;

	// gamma correction and watermarking happen on the raw RGB buffer
	R_GammaCorrect( allbuf + offset, ( width * 3 + padlen ) * height );
	R_ApplyScreenshotWatermark( allbuf + offset, width, height, padlen );

	// swap rgb to bgr and add line padding
	if ( scanpad == 0 && padlen == 0 ) {
		// fastest case
		srcptr = destptr = allbuf + offset;
		endmem = srcptr + scanlen * height;
		while ( srcptr < endmem ) {
			temp[0] = srcptr[0];
			destptr[0] = srcptr[2];
			destptr[2] = temp[0];
			destptr += 3;
			srcptr += 3;
		}
	} else {
		// move destination buffer forward if source padding is greater than for BMP
		if ( padlen > scanpad )
			buffer += (width * 3 + padlen - scanlen ) * height;
		// point on last line
		srcptr = allbuf + offset + (height-1) * (width * 3 + padlen);
		destptr = buffer + (height-1) * scanlen;
		len = (width * 3 - 3);
		while ( destptr >= buffer ) {
			srcline = srcptr + len;
			dstline = destptr + len;
			while ( srcline >= srcptr ) {
				temp[2] = srcline[0];
				temp[1] = srcline[1];
				temp[0] = srcline[2];
				dstline[0] = temp[0];
				dstline[1] = temp[1];
				dstline[2] = temp[2];
				dstline-=3;
				srcline-=3;
			}
			srcptr -= (width * 3 + padlen);
			destptr -= scanlen;
		}
	}

	// fill this last to avoid data overwrite in case when we're moving destination buffer forward
	FillBMPHeader( buffer - header_size, width, height, memcount, header_size );

	if ( clipboardOnly ) {
		// copy starting from bitmapinfoheader
		ri.Sys_SetClipboardBitmap( buffer - 40, memcount + 40 );
	} else {
		ri.FS_WriteFile( fileName, buffer - header_size, memcount + header_size );
		R_WriteScreenshotViewpos( fileName, backEnd.refdef.vieworg, backEnd.refdef.viewaxis );
	}

	ri.Hunk_FreeTempMemory( allbuf );
}


/*
==================
R_ScreenshotFilename
==================
*/
static void R_ScreenshotFilename( char *fileName, const char *fileExt, const char *commandName, const char *faceName ) {
	char baseName[MAX_OSPATH];
	const char *pattern;
	int count;

	pattern = ( r_screenshotNameFormat && r_screenshotNameFormat->string[0] ) ? r_screenshotNameFormat->string : "shot-{date}-{time}";

	for ( count = 0; count < 1000; count++ ) {
		qboolean usedIter;

		if ( !R_ExpandScreenshotPattern( baseName, sizeof( baseName ), pattern, commandName, faceName, fileExt, count, &usedIter ) ) {
			Com_sprintf( baseName, sizeof( baseName ), "shot-%d", count );
			usedIter = qtrue;
		}

		if ( faceName && faceName[0] && !strstr( pattern, "{face}" ) ) {
			Q_strcat( baseName, sizeof( baseName ), va( "-%s", faceName ) );
		}

		if ( !usedIter && count > 0 ) {
			Q_strcat( baseName, sizeof( baseName ), va( "-%d", count ) );
		}

		Com_sprintf( fileName, MAX_OSPATH, "screenshots/%s.%s", baseName, fileExt );
		if ( !ri.FS_FileExists( fileName ) ) {
			return;
		}
	}

	Com_sprintf( fileName, MAX_OSPATH, "screenshots/shot-overflow.%s", fileExt );
}

typedef struct {
	int sourceX;
	int sourceY;
	int sourceWidth;
	int sourceHeight;
	int outputWidth;
	int outputHeight;
} levelshotParams_t;

static qboolean R_LevelshotValueIsDisabled( const char *value )
{
	return !value || !value[0] || !Q_stricmp( value, "0" ) || !Q_stricmp( value, "none" ) ||
		!Q_stricmp( value, "source" ) || !Q_stricmp( value, "viewport" ) || !Q_stricmp( value, "default" );
}

static const char *R_LevelshotFindSeparator( const char *value )
{
	const char *sep;

	sep = strchr( value, 'x' );
	if ( !sep ) {
		sep = strchr( value, 'X' );
	}
	if ( !sep ) {
		sep = strchr( value, ':' );
	}
	if ( !sep ) {
		sep = strchr( value, '/' );
	}

	return sep;
}

static qboolean R_ParseLevelshotSize( const char *value, int *width, int *height )
{
	const char *sep;

	if ( R_LevelshotValueIsDisabled( value ) ) {
		return qfalse;
	}

	sep = R_LevelshotFindSeparator( value );
	if ( sep ) {
		char left[32];
		char right[32];
		int leftLen = sep - value;
		int rightLen = strlen( sep + 1 );

		if ( leftLen <= 0 || leftLen >= (int)sizeof( left ) || rightLen <= 0 || rightLen >= (int)sizeof( right ) ) {
			return qfalse;
		}

		Q_strncpyz( left, value, leftLen + 1 );
		Q_strncpyz( right, sep + 1, rightLen + 1 );

		*width = atoi( left );
		*height = atoi( right );
		return *width > 0 && *height > 0;
	}

	*width = atoi( value );
	*height = *width;
	return *width > 0;
}

static qboolean R_ParseLevelshotAspect( const char *value, float *aspect )
{
	const char *sep;

	if ( R_LevelshotValueIsDisabled( value ) ) {
		return qfalse;
	}

	sep = R_LevelshotFindSeparator( value );
	if ( sep ) {
		char left[32];
		char right[32];
		float leftValue;
		float rightValue;
		int leftLen = sep - value;
		int rightLen = strlen( sep + 1 );

		if ( leftLen <= 0 || leftLen >= (int)sizeof( left ) || rightLen <= 0 || rightLen >= (int)sizeof( right ) ) {
			return qfalse;
		}

		Q_strncpyz( left, value, leftLen + 1 );
		Q_strncpyz( right, sep + 1, rightLen + 1 );

		leftValue = Q_atof( left );
		rightValue = Q_atof( right );
		if ( leftValue <= 0.0f || rightValue <= 0.0f ) {
			return qfalse;
		}

		*aspect = leftValue / rightValue;
		return qtrue;
	}

	*aspect = Q_atof( value );
	return *aspect > 0.0f;
}

static void R_GetLevelshotCenteredRect( int width, int height, float aspect, int *x, int *y, int *outWidth, int *outHeight )
{
	float viewportAspect = (float)width / (float)height;

	if ( viewportAspect > aspect ) {
		*outHeight = height;
		*outWidth = (int)( height * aspect + 0.5f );
	} else {
		*outWidth = width;
		*outHeight = (int)( width / aspect + 0.5f );
	}

	*outWidth = MAX( 1, MIN( width, *outWidth ) );
	*outHeight = MAX( 1, MIN( height, *outHeight ) );
	*x = ( width - *outWidth ) / 2;
	*y = ( height - *outHeight ) / 2;
}

static void R_ResolveLevelshotParams( int viewportWidth, int viewportHeight, levelshotParams_t *params )
{
	float sourceAspect;
	int outputWidth, outputHeight;

	params->sourceX = 0;
	params->sourceY = 0;
	params->sourceWidth = viewportWidth;
	params->sourceHeight = viewportHeight;

	if ( r_levelshotSourceAspect && R_ParseLevelshotAspect( r_levelshotSourceAspect->string, &sourceAspect ) ) {
		R_GetLevelshotCenteredRect( viewportWidth, viewportHeight, sourceAspect,
			&params->sourceX, &params->sourceY, &params->sourceWidth, &params->sourceHeight );
	} else if ( r_levelshotSourceAspect && !R_LevelshotValueIsDisabled( r_levelshotSourceAspect->string ) ) {
		ri.Printf( PRINT_WARNING, "WARNING: invalid r_levelshotSourceAspect '%s', using full viewport.\n",
			r_levelshotSourceAspect->string );
	}

	if ( r_levelshotSize && R_ParseLevelshotSize( r_levelshotSize->string, &outputWidth, &outputHeight ) ) {
		params->outputWidth = outputWidth;
		params->outputHeight = outputHeight;
		return;
	}

	if ( r_levelshotSize && !R_LevelshotValueIsDisabled( r_levelshotSize->string ) ) {
		ri.Printf( PRINT_WARNING, "WARNING: invalid r_levelshotSize '%s', using source size/downscale.\n",
			r_levelshotSize->string );
	}

	if ( r_levelshotDownscale && r_levelshotDownscale->value > 1.0f ) {
		params->outputWidth = MAX( 1, (int)( params->sourceWidth / r_levelshotDownscale->value + 0.5f ) );
		params->outputHeight = MAX( 1, (int)( params->sourceHeight / r_levelshotDownscale->value + 0.5f ) );
	} else {
		params->outputWidth = params->sourceWidth;
		params->outputHeight = params->sourceHeight;
	}
}

static void R_ResampleLevelshot( const byte *source, int sourceWidth, int padlen, const levelshotParams_t *params, byte *out )
{
	int stride = sourceWidth * 3 + padlen;
	int y;

	if ( params->outputWidth == params->sourceWidth && params->outputHeight == params->sourceHeight ) {
		for ( y = 0; y < params->outputHeight; y++ ) {
			const byte *src = source + ( params->sourceY + y ) * stride + params->sourceX * 3;
			byte *dst = out + y * params->outputWidth * 3;

			Com_Memcpy( dst, src, params->outputWidth * 3 );
		}
		return;
	}

	for ( y = 0; y < params->outputHeight; y++ ) {
		int srcY0 = params->sourceY + ( y * params->sourceHeight ) / params->outputHeight;
		int srcY1 = params->sourceY + ( ( y + 1 ) * params->sourceHeight + params->outputHeight - 1 ) / params->outputHeight;
		int x;

		if ( srcY1 <= srcY0 ) {
			srcY1 = srcY0 + 1;
		}
		if ( srcY1 > params->sourceY + params->sourceHeight ) {
			srcY1 = params->sourceY + params->sourceHeight;
		}

		for ( x = 0; x < params->outputWidth; x++ ) {
			int srcX0 = params->sourceX + ( x * params->sourceWidth ) / params->outputWidth;
			int srcX1 = params->sourceX + ( ( x + 1 ) * params->sourceWidth + params->outputWidth - 1 ) / params->outputWidth;
			unsigned long long red = 0;
			unsigned long long green = 0;
			unsigned long long blue = 0;
			int count = 0;
			int sampleY;
			byte *dst = out + ( y * params->outputWidth + x ) * 3;

			if ( srcX1 <= srcX0 ) {
				srcX1 = srcX0 + 1;
			}
			if ( srcX1 > params->sourceX + params->sourceWidth ) {
				srcX1 = params->sourceX + params->sourceWidth;
			}

			for ( sampleY = srcY0; sampleY < srcY1; sampleY++ ) {
				const byte *row = source + sampleY * stride;
				int sampleX;

				for ( sampleX = srcX0; sampleX < srcX1; sampleX++ ) {
					const byte *pixel = row + sampleX * 3;

					red += pixel[0];
					green += pixel[1];
					blue += pixel[2];
					count++;
				}
			}

			dst[0] = (byte)( red / count );
			dst[1] = (byte)( green / count );
			dst[2] = (byte)( blue / count );
		}
	}
}


/*
====================
R_LevelShot

levelshots write map preview images under levelshots/ and can
retain the viewport size, crop from a centered aspect block, or
resample to an explicit output size
====================
*/
static void R_SetCaptureActive( qboolean active )
{
	ri.Cvar_Set( "cl_captureActive", active ? "1" : "0" );
}

void RB_TakeLevelShot( void ) {
	char		checkname[MAX_OSPATH];
	byte		*buffer;
	byte		*rgb;
	byte		*source, *allsource;
	size_t		offset = 0;
	int			padlen;
	int			x, y;
	levelshotParams_t params;

	Com_sprintf(checkname, sizeof(checkname), "levelshots/%s.tga", tr.world->baseName);

	allsource = RB_ReadPixels(0, 0, gls.captureWidth, gls.captureHeight, &offset, &padlen, 0 );
	source = allsource + offset;
	R_ResolveLevelshotParams( gls.captureWidth, gls.captureHeight, &params );

	rgb = ri.Hunk_AllocateTempMemory( params.outputWidth * params.outputHeight * 3 );
	R_ResampleLevelshot( source, gls.captureWidth, padlen, &params, rgb );

	// gamma correction happens on the resampled RGB output
	R_GammaCorrect( rgb, params.outputWidth * params.outputHeight * 3 );

	buffer = ri.Hunk_AllocateTempMemory( params.outputWidth * params.outputHeight * 3 + 18 );
	Com_Memset (buffer, 0, 18);
	buffer[2] = 2;		// uncompressed type
	buffer[12] = params.outputWidth & 255;
	buffer[13] = params.outputWidth >> 8;
	buffer[14] = params.outputHeight & 255;
	buffer[15] = params.outputHeight >> 8;
	buffer[16] = 24;	// pixel size

	for ( y = 0; y < params.outputHeight; y++ ) {
		for ( x = 0; x < params.outputWidth; x++ ) {
			const byte *src = rgb + ( y * params.outputWidth + x ) * 3;
			byte *dst = buffer + 18 + ( y * params.outputWidth + x ) * 3;

			dst[0] = src[2];
			dst[1] = src[1];
			dst[2] = src[0];
		}
	}

	ri.FS_WriteFile( checkname, buffer, params.outputWidth * params.outputHeight * 3 + 18 );

	ri.Hunk_FreeTempMemory(rgb);
	ri.Hunk_FreeTempMemory(buffer);
	ri.Hunk_FreeTempMemory(allsource);

	ri.Printf( PRINT_ALL, "Wrote %s (%dx%d)\n", checkname, params.outputWidth, params.outputHeight );
}

static void R_ScheduleLevelShot( void )
{
	if ( !tr.world ) {
		ri.Printf( PRINT_WARNING, "WARNING: screenshot levelshot requires an active world.\n" );
		return;
	}

	if ( backEnd.levelshotPending ) {
		return;
	}

	backEnd.levelshotPending = qtrue;
	R_SetCaptureActive( qtrue );
}

static void R_ScheduleCubemapScreenshot( int typeMask, const char *ext )
{
	static const char *faceNames[6] = { "front", "back", "left", "right", "top", "bottom" };
	char checkname[MAX_OSPATH];
	const char *baseName;
	int baseArg;
	int i;
	qboolean silent;

	if ( !tr.world ) {
		ri.Printf( PRINT_WARNING, "WARNING: screenshot cubemap requires an active world.\n" );
		return;
	}

	if ( backEnd.screenshotCubeActive || backEnd.screenshotCubeFrontPending ) {
		return;
	}

	baseArg = 2;
	silent = qfalse;
	if ( !strcmp( ri.Cmd_Argv( baseArg ), "silent" ) ) {
		silent = qtrue;
		baseArg++;
	}

	if ( ri.Cmd_Argc() > baseArg + 1 ) {
		ri.Printf( PRINT_ALL, "usage: %s cubemap [silent] [basename]\n", ri.Cmd_Argv( 0 ) );
		return;
	}

	baseName = ( ri.Cmd_Argc() == baseArg + 1 ) ? ri.Cmd_Argv( baseArg ) : NULL;

	for ( i = 0; i < 6; i++ ) {
		if ( baseName && baseName[0] ) {
			Com_sprintf( checkname, sizeof( checkname ), "screenshots/%s-%s.%s", baseName, faceNames[i], ext );
		} else {
			R_ScreenshotFilename( checkname, ext, "screenshot-cubemap", faceNames[i] );
		}

		Q_strncpyz( backEnd.screenshotCubeNames[i], checkname, sizeof( backEnd.screenshotCubeNames[i] ) );
	}

	backEnd.screenshotCubeFormat = typeMask;
	backEnd.screenshotCubeSilent = silent;
	backEnd.screenshotCubeFrontPending = qfalse;
	backEnd.screenshotCubeActive = qtrue;
	R_SetCaptureActive( qtrue );
}


/*
==================
R_ScreenShot_f

screenshot
screenshot [silent]
screenshot [levelshot]
screenshot [cubemap [silent] [basename]]
screenshot [filename]

Doesn't print the pacifier message if there is a second arg
==================
*/
static void R_ScreenShot_f( void ) {
	char		checkname[MAX_OSPATH];
	qboolean	silent;
	int			typeMask;
	const char	*ext;

	if ( ri.CL_IsMinimized() && !RE_CanMinimize() ) {
		ri.Printf( PRINT_WARNING, "WARNING: unable to take screenshot when minimized because FBO is not available/enabled.\n" );
		return;
	}

	if ( Q_stricmp( ri.Cmd_Argv(0), "screenshotJPEG" ) == 0 ) {
		typeMask = SCREENSHOT_JPG;
		ext = "jpg";
	} else if ( Q_stricmp( ri.Cmd_Argv(0), "screenshotBMP" ) == 0 ) {
		typeMask = SCREENSHOT_BMP;
		ext = "bmp";
	} else if ( Q_stricmp( ri.Cmd_Argv(0), "screenshotTGA" ) == 0 ) {
		typeMask = SCREENSHOT_TGA;
		ext = "tga";
	} else {
		typeMask = SCREENSHOT_PNG;
		ext = "png";
	}

	R_WarnExplicitHdrScreenshotCapture();

	if ( !strcmp( ri.Cmd_Argv( 1 ), "levelshot" ) ) {
		R_ScheduleLevelShot();
		return;
	}

	if ( !strcmp( ri.Cmd_Argv( 1 ), "cubemap" ) ) {
		R_ScheduleCubemapScreenshot( typeMask, ext );
		return;
	}

	// check if already scheduled
	if ( backEnd.screenshotMask & typeMask )
		return;

	if ( !strcmp( ri.Cmd_Argv(1), "silent" ) ) {
		silent = qtrue;
	} else if ( typeMask == SCREENSHOT_BMP && !strcmp( ri.Cmd_Argv(1), "clipboard" ) ) {
		backEnd.screenshotMask |= SCREENSHOT_BMP_CLIPBOARD;
		silent = qtrue;
	} else {
		silent = qfalse;
	}

	if ( ri.Cmd_Argc() == 2 && !silent ) {
		// explicit filename
		Com_sprintf( checkname, MAX_OSPATH, "screenshots/%s.%s", ri.Cmd_Argv( 1 ), ext );
	} else {
		if ( backEnd.screenshotMask & SCREENSHOT_BMP_CLIPBOARD ) {
			// no need for filename, copy to system buffer
			checkname[0] = '\0';
		} else {
			// scan for a free filename
			R_ScreenshotFilename( checkname, ext, ri.Cmd_Argv( 0 ), NULL );
		}
	}

	// we will make screenshot right at the end of RE_EndFrame()
	backEnd.screenshotMask |= typeMask;
	if ( ri.PublishGameScreenshot && checkname[0] ) {
		ri.PublishGameScreenshot( checkname, checkname );
	}

	if ( typeMask == SCREENSHOT_PNG ) {
		backEnd.screenShotPNGsilent = silent;
		Q_strncpyz( backEnd.screenshotPNG, checkname, sizeof( backEnd.screenshotPNG ) );
	} else if ( typeMask == SCREENSHOT_JPG ) {
		backEnd.screenShotJPGsilent = silent;
		Q_strncpyz( backEnd.screenshotJPG, checkname, sizeof( backEnd.screenshotJPG ) );
	} else if ( typeMask == SCREENSHOT_BMP ) {
		backEnd.screenShotBMPsilent = silent;
		Q_strncpyz( backEnd.screenshotBMP, checkname, sizeof( backEnd.screenshotBMP ) );
	} else {
		backEnd.screenShotTGAsilent = silent;
		Q_strncpyz( backEnd.screenshotTGA, checkname, sizeof( backEnd.screenshotTGA ) );
	}
}


//============================================================================

/*
==================
RB_TakeVideoFrameCmd
==================
*/
const void *RB_TakeVideoFrameCmd( const void *data )
{
	const videoFrameCommand_t *cmd;
	byte		*cBuf;
	size_t		memcount, linelen;
	int			padwidth, avipadwidth, padlen, avipadlen;
	int			packAlign;

	cmd = (const videoFrameCommand_t *)data;

	qglGetIntegerv(GL_PACK_ALIGNMENT, &packAlign);

	linelen = cmd->width * 3;

	// Alignment stuff for glReadPixels
	padwidth = PAD(linelen, packAlign);
	padlen = padwidth - linelen;
	// AVI line padding
	avipadwidth = PAD(linelen, AVI_LINE_PADDING);
	avipadlen = avipadwidth - linelen;

	cBuf = PADP(cmd->captureBuffer, packAlign);

	qglReadPixels(0, 0, cmd->width, cmd->height, GL_RGB,
		GL_UNSIGNED_BYTE, cBuf);

	memcount = padwidth * cmd->height;

	// gamma correction
	R_GammaCorrect( cBuf, memcount );

	if ( cmd->motionJpeg )
	{
		memcount = ri.CL_SaveJPGToBuffer( cmd->encodeBuffer, linelen * cmd->height,
			r_aviMotionJpegQuality->integer,
			cmd->width, cmd->height, cBuf, padlen );
		ri.CL_WriteAVIVideoFrame(cmd->encodeBuffer, memcount);
	}
	else
	{
		byte *lineend, *memend;
		byte *srcptr, *destptr;

		srcptr = cBuf;
		destptr = cmd->encodeBuffer;
		memend = srcptr + memcount;

		// swap R and B and remove line paddings
		while(srcptr < memend)
		{
			lineend = srcptr + linelen;
			while(srcptr < lineend)
			{
				*destptr++ = srcptr[2];
				*destptr++ = srcptr[1];
				*destptr++ = srcptr[0];
				srcptr += 3;
			}

			Com_Memset(destptr, '\0', avipadlen);
			destptr += avipadlen;

			srcptr += padlen;
		}

		ri.CL_WriteAVIVideoFrame(cmd->encodeBuffer, avipadwidth * cmd->height);
	}

	return (const void *)(cmd + 1);
}


//============================================================================

/*
** GL_SetDefaultState
*/
static void GL_SetDefaultState( void )
{
	int i;

	glState.currenttmu = 0;
	glState.currentArray = 0;

	for ( i = 0; i < MAX_TEXTURE_UNITS; i++ )
	{
		glState.currenttextures[ i ] = 0;
		glState.glClientStateBits[ i ] = 0;
	}

	qglClearDepth( 1.0f );

	qglCullFace( GL_FRONT );
	glState.faceCulling = -1;

	qglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	glState.colorMaskValid = qfalse;
	GL_ColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );

	// initialize downstream texture unit if we're running
	// in a multitexture environment
	if ( qglActiveTextureARB )
	{
		qglActiveTextureARB( GL_TEXTURE1_ARB );
		GL_TextureMode( r_textureMode->string );
		GL_TexEnv( GL_MODULATE );
		qglDisable( GL_TEXTURE_2D );
		qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
		qglActiveTextureARB( GL_TEXTURE0_ARB );
	}

	qglEnable( GL_TEXTURE_2D );
	GL_TextureMode( r_textureMode->string );
	GL_TexEnv( GL_MODULATE );

	qglShadeModel( GL_SMOOTH );
	qglDepthFunc( GL_LEQUAL );

	// the vertex array is always enabled, but the color and texture
	// arrays are enabled and disabled around the compiled vertex array call
	qglEnableClientState( GL_VERTEX_ARRAY );

	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	qglDisableClientState( GL_COLOR_ARRAY );
	qglDisableClientState( GL_NORMAL_ARRAY );

	//
	// make sure our GL state vector is set correctly
	//
	glState.glStateBits = GLS_DEPTHTEST_DISABLE | GLS_DEPTHMASK_TRUE;

	qglPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	qglDepthMask( GL_TRUE );
	qglDisable( GL_DEPTH_TEST );
	qglEnable( GL_SCISSOR_TEST );
	qglDisable( GL_CULL_FACE );
	qglDisable( GL_BLEND );
	GL_SetAlphaToCoverage( qfalse );
}


/*
===============
RE_SyncRender
===============
*/
static void RE_SyncRender( void )
{
	if ( qglFinish && backEnd.doneSurfaces )
	{
		qglFinish();
	}
}


/*
================
R_PrintLongString

Workaround for ri.Printf's 1024 characters buffer limit.
================
*/
static void R_PrintLongString(const char *string) {
	char buffer[1024];
	const char *p;
	int size = strlen(string);

	p = string;
	while(size > 0)
	{
		Q_strncpyz(buffer, p, sizeof (buffer) );
		ri.Printf( PRINT_DEVELOPER, "%s", buffer );
		p += 1023;
		size -= 1023;
	}
}


/*
================
GfxInfo

Prints persistent rendering configuration
================
*/
static void GfxInfo( void )
{
	const char *enablestrings[] = { "disabled", "enabled" };
	const char *fsstrings[] = { "windowed", "fullscreen" };
	const char *fs;
	rendererDisplayOutput_t displayOutput;
	int mode;

	ri.Printf( PRINT_ALL, "\nGL_VENDOR: %s\n", glConfig.vendor_string );
	ri.Printf( PRINT_ALL, "GL_RENDERER: %s\n", glConfig.renderer_string );
	ri.Printf( PRINT_ALL, "GL_VERSION: %s\n", glConfig.version_string );
	ri.Printf( PRINT_DEVELOPER, "GL_EXTENSIONS: " );
	R_PrintLongString( glConfig.extensions_string );
	ri.Printf( PRINT_ALL, "\n" );
	ri.Printf( PRINT_ALL, "GL_MAX_TEXTURE_SIZE: %d\n", glConfig.maxTextureSize );
	ri.Printf( PRINT_ALL, "GL_MAX_TEXTURE_UNITS_ARB: %d\n", glConfig.numTextureUnits );
	ri.Printf( PRINT_ALL, "\nPIXELFORMAT: color(%d-bits) Z(%d-bit) stencil(%d-bits)\n", glConfig.colorBits, glConfig.depthBits, glConfig.stencilBits );

	if ( glConfig.isFullscreen )
	{
		const char *modefs = ri.Cvar_VariableString( "r_modeFullscreen" );
		if ( *modefs )
			mode = atoi( modefs );
		else
			mode = ri.Cvar_VariableIntegerValue( "r_mode" );
		fs = fsstrings[1];
	}
	else
	{
		mode = ri.Cvar_VariableIntegerValue( "r_mode" );
		fs = fsstrings[0];
	}

	if ( glConfig.vidWidth != gls.windowWidth || glConfig.vidHeight != gls.windowHeight )
	{
		ri.Printf( PRINT_ALL, "RENDER: %d x %d, MODE: %d, %d x %d %s hz:", glConfig.vidWidth, glConfig.vidHeight, mode, gls.windowWidth, gls.windowHeight, fs );
	}
	else
	{
		ri.Printf( PRINT_ALL, "MODE: %d, %d x %d %s hz:", mode, gls.windowWidth, gls.windowHeight, fs );
	}

	if ( glConfig.displayFrequency )
	{
		ri.Printf( PRINT_ALL, "%d\n", glConfig.displayFrequency );
	}
	else
	{
		ri.Printf( PRINT_ALL, "N/A\n" );
	}

	Com_Memset( &displayOutput, 0, sizeof( displayOutput ) );
	if ( ri.GLimp_QueryDisplayOutput ) {
		ri.GLimp_QueryDisplayOutput( &displayOutput );
	}
	if ( displayOutput.valid ) {
		ri.Printf( PRINT_ALL, "display output: native %s, HDR %s, headroom %.2f, SDR white %.0f nits, ICC %s/%i, driver %s, display %s\n",
			RendererOutputBackendName( displayOutput.nativeBackend ),
			enablestrings[displayOutput.hdrEnabled != 0],
			displayOutput.hdrHeadroom > 0.0f ? displayOutput.hdrHeadroom : 1.0f,
			displayOutput.sdrWhiteNits > 0.0f ? displayOutput.sdrWhiteNits : 203.0f,
			enablestrings[displayOutput.iccProfileAvailable != 0],
			displayOutput.iccProfileBytes,
			displayOutput.videoDriver[0] ? displayOutput.videoDriver : "unknown",
			displayOutput.displayName[0] ? displayOutput.displayName : "unknown" );
	}

	ri.Printf( PRINT_ALL, "multitexture: %s\n", enablestrings[qglActiveTextureARB != 0] );
	ri.Printf( PRINT_ALL, "compiled vertex arrays: %s\n", enablestrings[qglLockArraysEXT != 0 ] );
	ri.Printf( PRINT_ALL, "texenv add: %s\n", enablestrings[glConfig.textureEnvAddAvailable != 0] );
	ri.Printf( PRINT_ALL, "compressed textures: %s\n", enablestrings[glConfig.textureCompression!=TC_NONE] );
}


/*
================
VarInfo

Prints info that may change every R_Init() call
================
*/
static void VarInfo( void )
{
	if ( glConfig.deviceSupportsGamma ) {
		ri.Printf( PRINT_ALL, "GAMMA: hardware w/ %d overbright bits\n", tr.overbrightBits );
	} else {
		ri.Printf( PRINT_ALL, "GAMMA: software w/ %d overbright bits\n", tr.overbrightBits );
	}

	ri.Printf( PRINT_ALL, "texturemode: %s\n", r_textureMode->string );
	ri.Printf( PRINT_ALL, "texture bits: %d\n", r_texturebits->integer ? r_texturebits->integer : 32 );
	ri.Printf( PRINT_ALL, "picmip: %d%s, filter: %d\n", r_picmip->integer, r_nomip->integer ? ", worldspawn only" : "", r_picmipFilter->integer );

	if ( r_vertexLight->integer || glConfig.hardwareType == GLHW_PERMEDIA2 ) {
		ri.Printf( PRINT_ALL, "HACK: using vertex lightmap approximation\n" );
	} else if ( glConfig.hardwareType == GLHW_RAGEPRO ) {
		ri.Printf( PRINT_ALL, "HACK: ragePro approximations\n" );
	} else if ( glConfig.hardwareType == GLHW_RIVA128 ) {
		ri.Printf( PRINT_ALL, "HACK: riva128 approximations\n" );
	}

	if ( r_finish->integer ) {
		ri.Printf( PRINT_ALL, "Forcing glFinish\n" );
	}
}


/*
===============
GfxInfo_f
===============
*/
static void GfxInfo_f( void )
{
	GfxInfo();
	VarInfo();
}


/*
===============
R_Register
===============
*/
static void R_Register( void )
{
	// make sure all the commands added here are also removed in R_Shutdown
	ri.Cmd_AddCommand( "imagelist", R_ImageList_f );
	ri.Cmd_AddCommand( "shaderlist", R_ShaderList_f );
	ri.Cmd_AddCommand( "skinlist", R_SkinList_f );
	ri.Cmd_AddCommand( "modellist", R_Modellist_f );
	ri.Cmd_AddCommand( "advertlist", R_AdvertisementList_f );
	ri.Cmd_AddCommand( "screenshot", R_ScreenShot_f );
	ri.Cmd_AddCommand( "screenshotPNG", R_ScreenShot_f );
	ri.Cmd_AddCommand( "screenshotTGA", R_ScreenShot_f );
	ri.Cmd_AddCommand( "screenshotJPEG", R_ScreenShot_f );
	ri.Cmd_AddCommand( "screenshotBMP", R_ScreenShot_f );
	ri.Cmd_AddCommand( "gfxinfo", GfxInfo_f );
#ifdef USE_PMLIGHT
	ri.Cmd_AddCommand( "r_dlightTest", R_DlightTest_f );
#endif
	ri.Cmd_AddCommand( "r_staticLightReload", R_StaticMapLightsReload_f );
	GLX_CompatRegisterCommands();

	//
	// temporary latched variables that can only change over a restart
	//
	r_fullbright = ri.Cvar_Get( "r_fullbright", "0", CVAR_LATCH );
	ri.Cvar_SetDescription( r_fullbright, "Debugging tool to render the entire level without lighting." );
	r_overBrightBits = ri.Cvar_Get( "r_overBrightBits", "1", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_SetDescription( r_overBrightBits, "Sets the intensity of overall brightness of texture pixels." );
	r_mapOverBrightBits = ri.Cvar_Get( "r_mapOverBrightBits", "2", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_SetDescription( r_mapOverBrightBits, "Sets the number of overbright bits baked into all lightmaps and map data." );
	// Retail Quake Live persists this bounded cap and applies it while normalizing overbright map RGB.
	r_mapOverBrightCap = ri.Cvar_Get( "r_mapOverBrightCap", "255", CVAR_ARCHIVE | CVAR_LATCH | CVAR_VM_CREATED | CVAR_CLOUD );
	ri.Cvar_CheckRange( r_mapOverBrightCap, "0", "255", CV_FLOAT );
	ri.Cvar_SetDescription( r_mapOverBrightCap, "Caps the brightest normalized map-lighting channel after overbright conversion; 255 preserves retail default brightness." );
	r_intensity = ri.Cvar_Get( "r_intensity", "1.25", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_CheckRange( r_intensity, "1", "255", CV_FLOAT );
	ri.Cvar_SetDescription( r_intensity, "Global texture lighting scale." );
	r_singleShader = ri.Cvar_Get( "r_singleShader", "0", CVAR_CHEAT | CVAR_LATCH );
	ri.Cvar_SetDescription( r_singleShader, "Debugging tool that only uses the default shader for all rendering." );
	r_defaultImage = ri.Cvar_Get( "r_defaultImage", "", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_SetDescription( r_defaultImage, "Replace default (missing) image texture by either exact file or solid #rgb|#rrggbb background color." );

	r_simpleMipMaps = ri.Cvar_Get( "r_simpleMipMaps", "1", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_SetDescription( r_simpleMipMaps, "Whether or not to use a simple mipmapping algorithm or a more correct one:\n 0: off (proper linear filter)\n 1: on (for slower machines)" );
	r_vertexLight = ri.Cvar_Get( "r_vertexLight", "0", CVAR_ARCHIVE | CVAR_LATCH );
	ri.Cvar_SetDescription( r_vertexLight, "Set to 1 to use vertex light instead of lightmaps, collapse all multi-stage shaders into single-stage ones, might cause rendering artifacts." );

	r_picmip = ri.Cvar_Get( "r_picmip", "0", CVAR_ARCHIVE | CVAR_LATCH );
	ri.Cvar_CheckRange( r_picmip, "0", "16", CV_INTEGER );
	ri.Cvar_SetDescription( r_picmip, "Set texture quality, lower is better." );

	r_picmipFilter = ri.Cvar_Get( "r_picmipFilter", "1", CVAR_ARCHIVE | CVAR_LATCH );
	ri.Cvar_CheckRange( r_picmipFilter, "0", "15", CV_INTEGER );
	ri.Cvar_SetDescription( r_picmipFilter, "Filter shader paths allowed to use \\r_picmip:\n 0: off (legacy, all picmip-capable images)\n 1: textures/*\n 2: models/*\n 4: sprites/*\n 8: gfx/*, icons/*, menu/*, ui/*, fonts/*\n Add values to combine categories." );

	r_nomip = ri.Cvar_Get( "r_nomip", "0", CVAR_ARCHIVE | CVAR_LATCH );
	ri.Cvar_CheckRange( r_nomip, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_nomip, "Apply picmip only on worldspawn textures." );

	r_neatsky = ri.Cvar_Get( "r_neatsky", "0", CVAR_ARCHIVE | CVAR_LATCH );
	ri.Cvar_SetDescription( r_neatsky, "Disables texture mipping for skies." );
	r_roundImagesDown = ri.Cvar_Get ("r_roundImagesDown", "1", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_SetDescription( r_roundImagesDown, "When images are scaled, round images down instead of up." );
	r_colorMipLevels = ri.Cvar_Get ("r_colorMipLevels", "0", CVAR_LATCH );
	ri.Cvar_SetDescription( r_colorMipLevels, "Debugging tool to artificially color different mipmap levels so that they are more apparent." );
	r_detailTextures = ri.Cvar_Get( "r_detailtextures", "1", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_SetDescription( r_detailTextures, "Enables usage of shader stages flagged as detail." );
	r_texturebits = ri.Cvar_Get( "r_texturebits", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_SetDescription( r_texturebits, "Number of texture bits per texture." );

	r_mergeLightmaps = ri.Cvar_Get( "r_mergeLightmaps", "1", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_SetDescription( r_mergeLightmaps, "Merge built-in small lightmaps into bigger lightmaps (atlases)." );

#ifdef USE_VBO
	r_vbo = ri.Cvar_Get( "r_vbo", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_SetDescription( r_vbo, "Use Vertex Buffer Objects to cache static map geometry, may improve FPS on modern GPUs, increases hunk memory usage by 15-30MB (map-dependent)." );
#endif

	r_mapGreyScale = ri.Cvar_Get( "r_mapGreyScale", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_CheckRange( r_mapGreyScale, "-1", "1", CV_FLOAT );
	ri.Cvar_SetDescription(r_mapGreyScale, "Desaturate world map textures only, works independently from \\r_greyscale, negative values only desaturate lightmaps.");

	r_subdivisions = ri.Cvar_Get( "r_subdivisions", "1", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_SetDescription(r_subdivisions, "Distance to subdivide bezier curved surfaces. Higher values mean less subdivision and less geometric complexity.");

	r_maxpolys = ri.Cvar_Get( "r_maxpolys", XSTRING( MAX_POLYS ), CVAR_LATCH );
	ri.Cvar_SetDescription( r_maxpolys, "Maximum number of polygons to draw in a scene." );
	r_maxpolyverts = ri.Cvar_Get( "r_maxpolyverts", XSTRING( MAX_POLYVERTS ), CVAR_LATCH );
	ri.Cvar_SetDescription( r_maxpolyverts, "Maximum number of polygon vertices to draw in a scene." );

	//
	// archived variables that can change at any time
	//
	r_lodCurveError = ri.Cvar_Get( "r_lodCurveError", "250", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_lodCurveError, "-1", "8192", CV_FLOAT );
	ri.Cvar_SetDescription( r_lodCurveError, "Level of detail error on curved surface grids. Higher values result in better quality at a distance." );
	r_lodbias = ri.Cvar_Get( "r_lodbias", "-2", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_lodbias, "Sets the level of detail of in-game models:\n -2: Ultra (further delays LOD transition in the distance)\n -1: Very High (delays LOD transition in the distance)\n 0: High\n 1: Medium\n 2: Low" );
	r_znear = ri.Cvar_Get( "r_znear", "4", CVAR_CHEAT );
	ri.Cvar_CheckRange( r_znear, "0.001", "200", CV_FLOAT );
	ri.Cvar_SetDescription( r_znear, "Viewport distance from view origin (how close objects can be to the player before they're clipped out of the scene)." );
	r_zproj = ri.Cvar_Get( "r_zproj", "64", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_zproj, "Projected viewport frustum." );
	r_stereoSeparation = ri.Cvar_Get( "r_stereoSeparation", "64", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_stereoSeparation, "Control eye separation. Resulting separation is \\r_zproj divided by this value in standard units." );
	r_ignoreGLErrors = ri.Cvar_Get( "r_ignoreGLErrors", "1", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_ignoreGLErrors, "Ignore OpenGL errors." );
	r_teleporterFlash = ri.Cvar_Get( "r_teleporterFlash", "1", CVAR_ARCHIVE );
	ri.Cvar_SetDescription( r_teleporterFlash, "Show a white screen instead of a black screen when being teleported in hyperspace." );
	r_fastsky = ri.Cvar_Get( "r_fastsky", "0", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_fastsky, "Draw flat colored skies." );
	r_drawSun = ri.Cvar_Get( "r_drawSun", "0", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_drawSun, "Draw sun shader in skies." );
	r_dynamiclight = ri.Cvar_Get( "r_dynamiclight", "1", CVAR_ARCHIVE );
	ri.Cvar_SetDescription( r_dynamiclight, "Enables dynamic lighting." );
	r_depthFade = ri.Cvar_Get( "r_depthFade", "1", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_CheckRange( r_depthFade, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_depthFade, "Softens intersections between translucent particles and world geometry when the renderer supports depth-fade sampling." );
	r_celShading = ri.Cvar_Get( "r_celShading", "0", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_celShading, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_celShading, "Enable cel shading on model entities, including brush models, player models, and the first-person weapon." );
	r_celShadingWorld = ri.Cvar_Get( "r_celShadingWorld", "0", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_celShadingWorld, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_celShadingWorld, "Draw cel-style black edge outlines over opaque BSP world geometry without modifying lightmaps." );
	r_celShadingWorldWidth = ri.Cvar_Get( "r_celShadingWorldWidth", "2.0", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_celShadingWorldWidth, "1.0", "8.0", CV_FLOAT );
	ri.Cvar_SetDescription( r_celShadingWorldWidth, "Screen-space radius in pixels used by world cel outline depth edge detection. Larger values draw thicker outlines." );
	r_celShadingWorldAlpha = ri.Cvar_Get( "r_celShadingWorldAlpha", "1.0", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_celShadingWorldAlpha, "0.0", "1.0", CV_FLOAT );
	ri.Cvar_SetDescription( r_celShadingWorldAlpha, "Opacity of world cel outline edges." );
	r_celShadingWorldDepthThreshold = ri.Cvar_Get( "r_celShadingWorldDepthThreshold", "0.0015", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_celShadingWorldDepthThreshold, "0.0001", "0.02", CV_FLOAT );
	ri.Cvar_SetDescription( r_celShadingWorldDepthThreshold, "Depth discontinuity threshold for world cel outlines. Lower values catch more subtle edges." );
	r_celShadingModelShadows = ri.Cvar_Get( "r_celShadingModelShadows", "1", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_celShadingModelShadows, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_celShadingModelShadows, "Quantize model lighting intensity into cel shadow bands when r_celShading is enabled." );
	r_celViewWeapon = ri.Cvar_Get( "r_celViewWeapon", "1", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_celViewWeapon, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_celViewWeapon, "Allow cel shading and cel outlines on the first-person weapon when r_celShading is enabled." );
	r_celShadingSteps = ri.Cvar_Get( "r_celShadingSteps", "4", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_celShadingSteps, "2", "8", CV_INTEGER );
	ri.Cvar_SetDescription( r_celShadingSteps, "Number of diffuse lighting bands used by model cel shading. Higher values keep more intermediate tones." );
	r_celOutline = ri.Cvar_Get( "r_celOutline", "1", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_celOutline, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_celOutline, "Draw a silhouette outline shell around cel shaded model entities. Requires a stencil buffer." );
	r_celOutlineScale = ri.Cvar_Get( "r_celOutlineScale", "1.03", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_celOutlineScale, "1.0", "1.25", CV_FLOAT );
	ri.Cvar_SetDescription( r_celOutlineScale, "Expansion scale used for cel-shaded model outlines. Values just above 1.0 keep the outline tight." );
	r_celOutlineAlpha = ri.Cvar_Get( "r_celOutlineAlpha", "1.0", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_celOutlineAlpha, "0.0", "1.0", CV_FLOAT );
	ri.Cvar_SetDescription( r_celOutlineAlpha, "Opacity multiplier for cel-shaded model outlines." );
	r_celViewWeaponOutlineScale = ri.Cvar_Get( "r_celViewWeaponOutlineScale", "1.006", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_celViewWeaponOutlineScale, "1.0", "1.10", CV_FLOAT );
	ri.Cvar_SetDescription( r_celViewWeaponOutlineScale, "Expansion scale used for first-person weapon cel outlines." );
	r_celViewWeaponOutlineAlpha = ri.Cvar_Get( "r_celViewWeaponOutlineAlpha", "1.0", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_celViewWeaponOutlineAlpha, "0.0", "1.0", CV_FLOAT );
	ri.Cvar_SetDescription( r_celViewWeaponOutlineAlpha, "Opacity multiplier for first-person weapon cel outlines." );
	r_celOutlineColor = ri.Cvar_Get( "r_celOutlineColor", "0 0 0 255", CVAR_ARCHIVE );
	ri.Cvar_SetDescription( r_celOutlineColor, "Outline color for cel shaded model entities as \"r g b a\"." );
#ifdef USE_PMLIGHT
#if arm32 || arm64 // RPi4 GL driver have very poor ARB shaders performance...
	r_dlightMode = ri.Cvar_Get( "r_dlightMode", "0", CVAR_ARCHIVE );
#else
	r_dlightMode = ri.Cvar_Get( "r_dlightMode", "1", CVAR_ARCHIVE );
#endif
	ri.Cvar_CheckRange( r_dlightMode, "0", "2", CV_INTEGER );
	ri.Cvar_SetDescription( r_dlightMode, "Dynamic light mode:\n 0: VQ3 'fake' dynamic lights\n 1: High-quality per-pixel dynamic lights, slightly faster than VQ3's on modern hardware\n 2: Same as 1 but applies to entity models too" );
	r_dlightScale = ri.Cvar_Get( "r_dlightScale", "0.5", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_dlightScale, "0.1", "1", CV_FLOAT );
	ri.Cvar_SetDescription( r_dlightScale, "Scales dynamic light radius." );
	r_dlightSpecPower = ri.Cvar_Get( "r_dlightSpecPower", "10", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_dlightSpecPower, "1", "32", CV_FLOAT );
	ri.Cvar_SetDescription( r_dlightSpecPower, "Factors specularity effect from dynamic lights on surfaces." );
	ri.Cvar_SetGroup( r_dlightSpecPower, CVG_RENDERER );
	r_dlightSpecColor = ri.Cvar_Get( "r_dlightSpecColor", "-0.2", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_dlightSpecColor, "-1", "1", CV_FLOAT );
	ri.Cvar_SetDescription( r_dlightSpecColor, "Color base for specular component:\n <= 0: use current texture and modulate by abs(r_dlightSpecColor)\n > 0: use constant color with RGB components set to \\r_dlightSpecColor" );
	ri.Cvar_SetGroup( r_dlightSpecColor, CVG_RENDERER );
	r_dlightFalloff = ri.Cvar_Get( "r_dlightFalloff", "1", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_dlightFalloff, "0", "1", CV_FLOAT );
	ri.Cvar_SetDescription( r_dlightFalloff, "Blends PMLIGHT dynamic light attenuation from the original curve at 0 to a smooth edge falloff at 1." );
	ri.Cvar_SetGroup( r_dlightFalloff, CVG_RENDERER );
	r_dlightShadows = ri.Cvar_Get( "r_dlightShadows", "1", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_CheckRange( r_dlightShadows, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_dlightShadows, "Enables dynamic-light shadow planning, atlas rendering, and filtered shadow-map sampling." );
	ri.Cvar_SetGroup( r_dlightShadows, CVG_RENDERER );
	r_dlightShadowStrength = ri.Cvar_Get( "r_dlightShadowStrength", "0.95", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_dlightShadowStrength, "0", "1", CV_FLOAT );
	ri.Cvar_SetDescription( r_dlightShadowStrength, "Controls how strongly dynamic-light shadow-map occlusion dims the light." );
	ri.Cvar_SetGroup( r_dlightShadowStrength, CVG_RENDERER );
	r_dlightShadowBias = ri.Cvar_Get( "r_dlightShadowBias", "4", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_dlightShadowBias, "0", "64", CV_FLOAT );
	ri.Cvar_SetDescription( r_dlightShadowBias, "Maximum receiver bias in world units for angle-aware, texel-aware dynamic-light shadow-map sampling." );
	ri.Cvar_SetGroup( r_dlightShadowBias, CVG_RENDERER );
	r_dlightShadowCasterDepthBias = ri.Cvar_Get( "r_dlightShadowCasterDepthBias", "1", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_dlightShadowCasterDepthBias, "0", "64", CV_FLOAT );
	ri.Cvar_SetDescription( r_dlightShadowCasterDepthBias, "Constant depth bias units applied while rendering dynamic-light shadow casters." );
	ri.Cvar_SetGroup( r_dlightShadowCasterDepthBias, CVG_RENDERER );
	r_dlightShadowCasterSlopeBias = ri.Cvar_Get( "r_dlightShadowCasterSlopeBias", "1", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_dlightShadowCasterSlopeBias, "0", "8", CV_FLOAT );
	ri.Cvar_SetDescription( r_dlightShadowCasterSlopeBias, "Slope-scaled depth bias applied while rendering dynamic-light shadow casters." );
	ri.Cvar_SetGroup( r_dlightShadowCasterSlopeBias, CVG_RENDERER );
	r_dlightShadowCasterNormalBias = ri.Cvar_Get( "r_dlightShadowCasterNormalBias", "0.25", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_dlightShadowCasterNormalBias, "0", "8", CV_FLOAT );
	ri.Cvar_SetDescription( r_dlightShadowCasterNormalBias, "Maximum light-aware normal offset in world units applied while rendering dynamic-light shadow casters." );
	ri.Cvar_SetGroup( r_dlightShadowCasterNormalBias, CVG_RENDERER );
	r_dlightShadowFilter = ri.Cvar_Get( "r_dlightShadowFilter", "2", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_dlightShadowFilter, "0", va( "%i", SHADOW_FILTER_COUNT - 1 ), CV_INTEGER );
	ri.Cvar_SetDescription( r_dlightShadowFilter, R_ShadowFilterModeDescription() );
	ri.Cvar_SetGroup( r_dlightShadowFilter, CVG_RENDERER );
	r_dlightShadowMaxLights = ri.Cvar_Get( "r_dlightShadowMaxLights", "4", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_CheckRange( r_dlightShadowMaxLights, "0", va( "%i", MAX_DLIGHTS ), CV_INTEGER );
	ri.Cvar_SetDescription( r_dlightShadowMaxLights, "Maximum number of dynamic lights allowed to cast shadows in a view." );
	ri.Cvar_SetGroup( r_dlightShadowMaxLights, CVG_RENDERER );
	r_dlightShadowResolution = ri.Cvar_Get( "r_dlightShadowResolution", "256", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_CheckRange( r_dlightShadowResolution, "64", "1024", CV_INTEGER );
	ri.Cvar_SetDescription( r_dlightShadowResolution, "Nominal per-face resolution for dynamic-light shadow maps. The renderer rounds it down as needed to fit the atlas." );
	ri.Cvar_SetGroup( r_dlightShadowResolution, CVG_RENDERER );
	r_dlightShadowDebug = ri.Cvar_Get( "r_dlightShadowDebug", "0", CVAR_CHEAT );
	ri.Cvar_CheckRange( r_dlightShadowDebug, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_dlightShadowDebug, "Prints dynamic-light shadow planning counters each frame." );
	ri.Cvar_SetGroup( r_dlightShadowDebug, CVG_RENDERER );
	r_spotShadows = ri.Cvar_Get( "r_spotShadows", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_CheckRange( r_spotShadows, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_spotShadows, "Enables planning for the 2D spotlight shadow atlas used by sidecar spot lights and surfacelight proxies." );
	ri.Cvar_SetGroup( r_spotShadows, CVG_RENDERER );
	r_spotShadowMaxLights = ri.Cvar_Get( "r_spotShadowMaxLights", "16", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_CheckRange( r_spotShadowMaxLights, "0", va( "%i", SPOT_SHADOW_MAX_LIGHTS ), CV_INTEGER );
	ri.Cvar_SetDescription( r_spotShadowMaxLights, "Maximum number of spotlight shadow tiles the planned 2D atlas may reserve." );
	ri.Cvar_SetGroup( r_spotShadowMaxLights, CVG_RENDERER );
	r_spotShadowResolution = ri.Cvar_Get( "r_spotShadowResolution", "256", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_CheckRange( r_spotShadowResolution, va( "%i", SPOT_SHADOW_MIN_TILE_SIZE ), va( "%i", SPOT_SHADOW_MAX_TILE_SIZE ), CV_INTEGER );
	ri.Cvar_SetDescription( r_spotShadowResolution, "Nominal tile resolution for planned 2D spotlight shadow maps. The renderer rounds it down as needed to fit the atlas." );
	ri.Cvar_SetGroup( r_spotShadowResolution, CVG_RENDERER );
	r_spotShadowDebug = ri.Cvar_Get( "r_spotShadowDebug", "0", CVAR_CHEAT );
	ri.Cvar_CheckRange( r_spotShadowDebug, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_spotShadowDebug, "Prints spotlight atlas planning counters each frame." );
	ri.Cvar_SetGroup( r_spotShadowDebug, CVG_RENDERER );
	r_dlightIntensity = ri.Cvar_Get( "r_dlightIntensity", "1.0", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_dlightIntensity, "0.1", "1", CV_FLOAT );
	ri.Cvar_SetDescription( r_dlightIntensity, "Adjusts dynamic light intensity but not radius." );
#endif // USE_PMLIGHT
	r_dlightSaturation = ri.Cvar_Get( "r_dlightSaturation", "0.8", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_dlightSaturation, "0", "1", CV_FLOAT );
	ri.Cvar_SetDescription( r_dlightSaturation, "Adjusts dynamic light color saturation in linear light before rendering." );
	r_dlightOverbrightGamut = ri.Cvar_Get( "r_dlightOverbrightGamut", "1", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_dlightOverbrightGamut, "0", "1", CV_FLOAT );
	ri.Cvar_SetDescription( r_dlightOverbrightGamut, "Compresses overbright dynamic light chroma toward linear luminance; 0 preserves raw mod-provided colors." );
	r_staticLights = ri.Cvar_Get( "r_staticLights", "1", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_staticLights, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_staticLights, "Enables renderer-only static map lights loaded from maps/<mapname>.lights.json sidecar files." );
	ri.Cvar_SetGroup( r_staticLights, CVG_RENDERER );
	r_staticLightMaxLights = ri.Cvar_Get( "r_staticLightMaxLights", "8", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_staticLightMaxLights, "0", va( "%i", MAX_DLIGHTS ), CV_INTEGER );
	ri.Cvar_SetDescription( r_staticLightMaxLights, "Maximum number of static sidecar lights promoted into a scene." );
	ri.Cvar_SetGroup( r_staticLightMaxLights, CVG_RENDERER );
	r_staticLightShadows = ri.Cvar_Get( "r_staticLightShadows", "1", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_staticLightShadows, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_staticLightShadows, "Allows eligible static sidecar lights to enter the point-light shadow planner." );
	ri.Cvar_SetGroup( r_staticLightShadows, CVG_RENDERER );
	r_staticLightShadowMaxLights = ri.Cvar_Get( "r_staticLightShadowMaxLights", "2", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_staticLightShadowMaxLights, "0", va( "%i", MAX_DLIGHTS ), CV_INTEGER );
	ri.Cvar_SetDescription( r_staticLightShadowMaxLights, "Maximum number of promoted static sidecar lights allowed to be shadow candidates." );
	ri.Cvar_SetGroup( r_staticLightShadowMaxLights, CVG_RENDERER );
	r_staticLightDebug = ri.Cvar_Get( "r_staticLightDebug", "0", CVAR_CHEAT );
	ri.Cvar_CheckRange( r_staticLightDebug, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_staticLightDebug, "Prints static sidecar light loading and promotion counters." );
	ri.Cvar_SetGroup( r_staticLightDebug, CVG_RENDERER );
	r_surfaceLightProxies = ri.Cvar_Get( "r_surfaceLightProxies", "0", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_surfaceLightProxies, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_surfaceLightProxies, "Promotes non-sky q3map_surfaceLight world surfaces into renderer-only proxy lights." );
	ri.Cvar_SetGroup( r_surfaceLightProxies, CVG_RENDERER );
	r_surfaceLightProxyMaxLights = ri.Cvar_Get( "r_surfaceLightProxyMaxLights", "4", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_surfaceLightProxyMaxLights, "0", va( "%i", MAX_DLIGHTS ), CV_INTEGER );
	ri.Cvar_SetDescription( r_surfaceLightProxyMaxLights, "Maximum number of surfacelight proxies promoted into a scene." );
	ri.Cvar_SetGroup( r_surfaceLightProxyMaxLights, CVG_RENDERER );
	r_surfaceLightProxyShadows = ri.Cvar_Get( "r_surfaceLightProxyShadows", "0", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_surfaceLightProxyShadows, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_surfaceLightProxyShadows, "Allows promoted surfacelight proxies to enter the point-light shadow planner." );
	ri.Cvar_SetGroup( r_surfaceLightProxyShadows, CVG_RENDERER );
	r_surfaceLightProxyShadowMaxLights = ri.Cvar_Get( "r_surfaceLightProxyShadowMaxLights", "1", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_surfaceLightProxyShadowMaxLights, "0", va( "%i", MAX_DLIGHTS ), CV_INTEGER );
	ri.Cvar_SetDescription( r_surfaceLightProxyShadowMaxLights, "Maximum number of promoted surfacelight proxies allowed to be shadow candidates." );
	ri.Cvar_SetGroup( r_surfaceLightProxyShadowMaxLights, CVG_RENDERER );
	r_surfaceLightProxyDebug = ri.Cvar_Get( "r_surfaceLightProxyDebug", "0", CVAR_CHEAT );
	ri.Cvar_CheckRange( r_surfaceLightProxyDebug, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_surfaceLightProxyDebug, "Prints surfacelight proxy build and promotion counters." );
	ri.Cvar_SetGroup( r_surfaceLightProxyDebug, CVG_RENDERER );
	r_csmShadows = ri.Cvar_Get( "r_csmShadows", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_CheckRange( r_csmShadows, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_csmShadows, "Enables sky-sun cascaded shadow maps for opaque world geometry, entity models, and brush models." );
	ri.Cvar_SetGroup( r_csmShadows, CVG_RENDERER );
	r_csmCascadeCount = ri.Cvar_Get( "r_csmCascadeCount", "4", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_CheckRange( r_csmCascadeCount, "1", XSTRING( CSM_MAX_CASCADES ), CV_INTEGER );
	ri.Cvar_SetDescription( r_csmCascadeCount, "Number of sky-sun directional shadow cascades." );
	ri.Cvar_SetGroup( r_csmCascadeCount, CVG_RENDERER );
	r_csmMaxDistance = ri.Cvar_Get( "r_csmMaxDistance", "2048", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_csmMaxDistance, "64", "32768", CV_FLOAT );
	ri.Cvar_SetDescription( r_csmMaxDistance, "Maximum camera distance covered by planned directional shadow cascades." );
	ri.Cvar_SetGroup( r_csmMaxDistance, CVG_RENDERER );
	r_csmSplitLambda = ri.Cvar_Get( "r_csmSplitLambda", "0.65", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_csmSplitLambda, "0", "1", CV_FLOAT );
	ri.Cvar_SetDescription( r_csmSplitLambda, "Blend between linear and logarithmic directional shadow cascade splits: 0 linear, 1 logarithmic." );
	ri.Cvar_SetGroup( r_csmSplitLambda, CVG_RENDERER );
	r_csmResolution = ri.Cvar_Get( "r_csmResolution", "1024", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_CheckRange( r_csmResolution, "128", "4096", CV_INTEGER );
	ri.Cvar_SetDescription( r_csmResolution, "Nominal per-cascade sky-sun shadow-map resolution used for stable texel snapping." );
	ri.Cvar_SetGroup( r_csmResolution, CVG_RENDERER );
	r_csmShadowStrength = ri.Cvar_Get( "r_csmShadowStrength", "1", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_csmShadowStrength, "0", "1", CV_FLOAT );
	ri.Cvar_SetDescription( r_csmShadowStrength, "Maximum visible strength of sky-sun shadows; map sun color tints the final channel response." );
	ri.Cvar_SetGroup( r_csmShadowStrength, CVG_RENDERER );
	r_csmShadowFilter = ri.Cvar_Get( "r_csmShadowFilter", "2", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_csmShadowFilter, "0", va( "%i", SHADOW_FILTER_COUNT - 1 ), CV_INTEGER );
	ri.Cvar_SetDescription( r_csmShadowFilter, R_ShadowFilterModeDescription() );
	ri.Cvar_SetGroup( r_csmShadowFilter, CVG_RENDERER );
	r_csmShadowBias = ri.Cvar_Get( "r_csmShadowBias", "8", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_csmShadowBias, "0", "64", CV_FLOAT );
	ri.Cvar_SetDescription( r_csmShadowBias, "Maximum receiver bias in world units for directional sky-sun shadow-map sampling." );
	ri.Cvar_SetGroup( r_csmShadowBias, CVG_RENDERER );
	r_csmCasterDepthBias = ri.Cvar_Get( "r_csmCasterDepthBias", "1.5", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_csmCasterDepthBias, "0", "64", CV_FLOAT );
	ri.Cvar_SetDescription( r_csmCasterDepthBias, "Constant depth bias units for directional sky-sun shadow caster rendering." );
	ri.Cvar_SetGroup( r_csmCasterDepthBias, CVG_RENDERER );
	r_csmCasterSlopeBias = ri.Cvar_Get( "r_csmCasterSlopeBias", "1.5", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_csmCasterSlopeBias, "0", "8", CV_FLOAT );
	ri.Cvar_SetDescription( r_csmCasterSlopeBias, "Slope-scaled depth bias for directional sky-sun shadow caster rendering." );
	ri.Cvar_SetGroup( r_csmCasterSlopeBias, CVG_RENDERER );
	r_csmCasterNormalBias = ri.Cvar_Get( "r_csmCasterNormalBias", "0.5", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_csmCasterNormalBias, "0", "8", CV_FLOAT );
	ri.Cvar_SetDescription( r_csmCasterNormalBias, "Maximum normal offset in world units reserved for directional sky-sun shadow caster rendering." );
	ri.Cvar_SetGroup( r_csmCasterNormalBias, CVG_RENDERER );
	r_csmDebug = ri.Cvar_Get( "r_csmDebug", "0", CVAR_CHEAT );
	ri.Cvar_CheckRange( r_csmDebug, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_csmDebug, "Prints directional sky-sun shadow-map planning and render counters each frame." );
	ri.Cvar_SetGroup( r_csmDebug, CVG_RENDERER );
	r_csmDebugFallback = ri.Cvar_Get( "r_csmDebugFallback", "0", CVAR_TEMP );
	ri.Cvar_CheckRange( r_csmDebugFallback, "0", "4", CV_INTEGER );
	ri.Cvar_SetDescription( r_csmDebugFallback, "Forces CSM fallback diagnostics: 0 off, 1 no-world, 2 no-sun, 3 atlas-unavailable, 4 zero-cascade." );
	ri.Cvar_SetGroup( r_csmDebugFallback, CVG_RENDERER );
	r_shadowCorrectness = ri.Cvar_Get( "r_shadowCorrectness", "0", CVAR_CHEAT );
	ri.Cvar_CheckRange( r_shadowCorrectness, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_shadowCorrectness, "Forces the minimal shadow-map correctness path: one hard-filtered dynamic light, no spot or CSM passes, and no alpha-tested shadow casters." );
	ri.Cvar_SetGroup( r_shadowCorrectness, CVG_RENDERER );

#ifdef USE_FBO
	r_ext_multisample = ri.Cvar_Get( "r_ext_multisample", "0", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_ext_multisample, "0", "64", CV_INTEGER );
	ri.Cvar_SetDescription( r_ext_multisample, "Requests multisample anti-aliasing for geometry edges. Requires \\r_fbo 1. Common values are 0, 2, 4, 8, and 16; unsupported values resolve to the best supported sample count not above the request." );
	ri.Cvar_SetGroup( r_ext_multisample, CVG_RENDERER );

	r_ext_alpha_to_coverage = ri.Cvar_Get( "r_ext_alpha_to_coverage", "0", CVAR_ARCHIVE_ND );
	R_MakeCvarInstant( r_ext_alpha_to_coverage );
	ri.Cvar_CheckRange( r_ext_alpha_to_coverage, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_ext_alpha_to_coverage, "Use multisample alpha-to-coverage for alpha-tested texture edges when \\r_ext_multisample is active. Default off for strict legacy alpha-test parity." );
	ri.Cvar_SetGroup( r_ext_alpha_to_coverage, CVG_RENDERER );

	r_hdr = ri.Cvar_Get( "r_hdr", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_CheckRange( r_hdr, "-1", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_hdr,
		"Selects the HDR-capable FBO render pipeline. Requires \\r_fbo 1.\n"
		" 0: display-referred SDR compatibility path\n"
		" 1: high-precision FBO path; legacy tone mapping preserves Quake III lighting, while non-legacy tone mapping, color grading, and explicit HDR output use scene-linear color\n"
		"-1: legacy debug alias for \\r_hdrPrecision -1 without enabling scene-linear HDR\n"
		"Requires vid_restart so FBO storage and texture sRGB decode state rebuild together." );
	ri.Cvar_SetGroup( r_hdr, CVG_RENDERER );
	r_hdrPrecision = ri.Cvar_Get( "r_hdrPrecision", "0", CVAR_ARCHIVE_ND );
	R_MakeCvarInstant( r_hdrPrecision );
	ri.Cvar_CheckRange( r_hdrPrecision, "-1", "16", CV_INTEGER );
	ri.Cvar_SetDescription( r_hdrPrecision,
		"Internal FBO color precision for the display pipeline.\n"
		" 0: automatic (8-bit SDR, RGBA16F when \\r_hdr 1)\n"
		"-1: debug 4-bit storage for banding tests\n"
		" 8: force 8-bit storage\n"
		"16: force 16-bit normalized storage for SDR; \\r_hdr 1 still uses RGBA16F\n"
		"Applies after the current frame." );
	ri.Cvar_SetGroup( r_hdrPrecision, CVG_RENDERER );
	r_hdrBloomFormat = ri.Cvar_Get( "r_hdrBloomFormat", "0", CVAR_ARCHIVE_ND );
	R_MakeCvarInstant( r_hdrBloomFormat );
	ri.Cvar_CheckRange( r_hdrBloomFormat, "0", "3", CV_INTEGER );
	ri.Cvar_SetDescription( r_hdrBloomFormat,
		"HDR bloom/extract intermediate storage policy for positive-only postprocess buffers.\n"
		" 0: automatic (try R11G11B10F for RGB bloom, fall back to RGBA16F)\n"
		" 1: force RGBA16F for conservative parity\n"
		" 2: prefer R11G11B10F for RGB bloom intermediates\n"
		" 3: prefer RG16F for positive two-channel intermediates; RGB bloom falls back safely\n"
		"Applies when the bloom FBO chain is recreated." );
	ri.Cvar_SetGroup( r_hdrBloomFormat, CVG_RENDERER );
	r_srgbTextures = ri.Cvar_Get( "r_srgbTextures", "1", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_CheckRange( r_srgbTextures, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_srgbTextures, "Use sRGB texture formats for authored color images when the HDR path is actually running scene-linear color. Data, lightmap, fog, and utility textures stay linear/data. Requires vid_restart so existing textures can be reloaded safely." );
	ri.Cvar_SetGroup( r_srgbTextures, CVG_RENDERER );
	r_framebufferSRGB = ri.Cvar_Get( "r_framebufferSRGB", "1", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_framebufferSRGB, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_framebufferSRGB, "Allow GL_FRAMEBUFFER_SRGB when the active draw target is an sRGB-encoded framebuffer. The current GLx/OpenGL SDR output shader keeps it disabled to avoid double encoding." );
	ri.Cvar_SetGroup( r_framebufferSRGB, CVG_RENDERER );
	r_outputBackend = ri.Cvar_Get( "r_outputBackend", "0", CVAR_ARCHIVE_ND );
	R_MakeCvarInstant( r_outputBackend );
	ri.Cvar_CheckRange( r_outputBackend, "0", "5", CV_INTEGER );
	ri.Cvar_SetDescription( r_outputBackend,
		"Final display output backend: 0 auto, 1 SDR sRGB, 2 Windows scRGB, 3 HDR10 PQ, 4 macOS EDR, 5 Linux experimental HDR telemetry/prototype. Non-SDR output requires r_hdr 1 and platform support. Applies immediately." );
	ri.Cvar_SetGroup( r_outputBackend, CVG_RENDERER );
	r_outputAllowExperimentalLinuxHDR = ri.Cvar_Get( "r_outputAllowExperimentalLinuxHDR", "0", CVAR_ARCHIVE_ND );
	R_MakeCvarInstant( r_outputAllowExperimentalLinuxHDR );
	ri.Cvar_CheckRange( r_outputAllowExperimentalLinuxHDR, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_outputAllowExperimentalLinuxHDR, "Allows Linux HDR telemetry/prototype output only when SDL reports HDR headroom and an explicit compositor/protocol path. Applies immediately." );
	ri.Cvar_SetGroup( r_outputAllowExperimentalLinuxHDR, CVG_RENDERER );
	r_tonemap = ri.Cvar_Get( "r_tonemap", "0", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_tonemap, "0", "2", CV_INTEGER );
	ri.Cvar_SetDescription( r_tonemap,
		"Final-pass tone mapper used by the scene-linear HDR pipeline:\n"
		" 0: legacy gamma/overbright\n"
		" 1: simple Reinhard (legacy alias Reinhard)\n"
		" 2: ACES-fitted filmic curve (legacy alias ACES)" );
	ri.Cvar_SetGroup( r_tonemap, CVG_RENDERER );
	r_tonemapExposure = ri.Cvar_Get( "r_tonemapExposure", "1.0", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_tonemapExposure, "0.1", "8.0", CV_FLOAT );
	ri.Cvar_SetDescription( r_tonemapExposure, "Exposure multiplier used by scene-linear tone mapping and bloom extraction." );
	ri.Cvar_SetGroup( r_tonemapExposure, CVG_RENDERER );
	r_hudExcludePostProcess = ri.Cvar_Get( "r_hudExcludePostProcess", "1", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_hudExcludePostProcess, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_hudExcludePostProcess, "Exclude 3D HUD scenes (RDF_NOWORLDMODEL after the world view) from bloom extraction while keeping HUD/2D in the final output transform. Set to 0 for legacy post-processed HUD models." );
	ri.Cvar_SetGroup( r_hudExcludePostProcess, CVG_RENDERER );
	r_crt = ri.Cvar_Get( "r_crt", "0", CVAR_ARCHIVE_ND );
	R_MakeCvarInstant( r_crt );
	ri.Cvar_CheckRange( r_crt, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_crt, "Enable final-pass CRT emulation after gamma, tone mapping, bloom, and color grading. Requires \\r_fbo 1." );
	ri.Cvar_SetGroup( r_crt, CVG_RENDERER );
	r_crtAmount = ri.Cvar_Get( "r_crtAmount", "1.0", CVAR_ARCHIVE_ND );
	R_MakeCvarInstant( r_crtAmount );
	ri.Cvar_CheckRange( r_crtAmount, "0.0", "1.0", CV_FLOAT );
	ri.Cvar_SetDescription( r_crtAmount, "Blend amount for \\r_crt final-pass CRT emulation." );
	ri.Cvar_SetGroup( r_crtAmount, CVG_RENDERER );
	r_crtScanlineStrength = ri.Cvar_Get( "r_crtScanlineStrength", "0.55", CVAR_ARCHIVE_ND );
	R_MakeCvarInstant( r_crtScanlineStrength );
	ri.Cvar_CheckRange( r_crtScanlineStrength, "0.0", "1.0", CV_FLOAT );
	ri.Cvar_SetDescription( r_crtScanlineStrength, "Scanline darkening strength for \\r_crt." );
	ri.Cvar_SetGroup( r_crtScanlineStrength, CVG_RENDERER );
	r_crtMaskStrength = ri.Cvar_Get( "r_crtMaskStrength", "0.35", CVAR_ARCHIVE_ND );
	R_MakeCvarInstant( r_crtMaskStrength );
	ri.Cvar_CheckRange( r_crtMaskStrength, "0.0", "1.0", CV_FLOAT );
	ri.Cvar_SetDescription( r_crtMaskStrength, "RGB phosphor mask strength for \\r_crt." );
	ri.Cvar_SetGroup( r_crtMaskStrength, CVG_RENDERER );
	r_crtCurvature = ri.Cvar_Get( "r_crtCurvature", "0.01", CVAR_ARCHIVE_ND );
	R_MakeCvarInstant( r_crtCurvature );
	ri.Cvar_CheckRange( r_crtCurvature, "0.0", "0.25", CV_FLOAT );
	ri.Cvar_SetDescription( r_crtCurvature, "Screen curvature amount for \\r_crt." );
	ri.Cvar_SetGroup( r_crtCurvature, CVG_RENDERER );
	r_crtChromatic = ri.Cvar_Get( "r_crtChromatic", "1.35", CVAR_ARCHIVE_ND );
	R_MakeCvarInstant( r_crtChromatic );
	ri.Cvar_CheckRange( r_crtChromatic, "0.0", "8.0", CV_FLOAT );
	ri.Cvar_SetDescription( r_crtChromatic, "Chromatic edge separation in source texels for \\r_crt." );
	ri.Cvar_SetGroup( r_crtChromatic, CVG_RENDERER );
	r_colorGrade = ri.Cvar_Get( "r_colorGrade", "0", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_colorGrade, "0", "3", CV_INTEGER );
	ri.Cvar_SetDescription( r_colorGrade,
		"Scene-linear final-pass color grading for \\r_hdr 1:\n"
		" 0: disabled\n"
		" 1: lift/gamma/gain and white-point adaptation\n"
		" 2: 3D LUT atlas\n"
		" 3: lift/gamma/gain, white-point adaptation, then 3D LUT atlas." );
	ri.Cvar_SetGroup( r_colorGrade, CVG_RENDERER );
	r_colorGradeLift = ri.Cvar_Get( "r_colorGradeLift", "0 0 0", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_colorGradeLift, "Lift offset applied to scene-linear RGB before tone mapping, formatted as \"r g b\"." );
	ri.Cvar_SetGroup( r_colorGradeLift, CVG_RENDERER );
	r_colorGradeGamma = ri.Cvar_Get( "r_colorGradeGamma", "1 1 1", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_colorGradeGamma, "Per-channel color-grade gamma in scene-linear space, formatted as \"r g b\". Values above 1 lift midtones." );
	ri.Cvar_SetGroup( r_colorGradeGamma, CVG_RENDERER );
	r_colorGradeGain = ri.Cvar_Get( "r_colorGradeGain", "1 1 1", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_colorGradeGain, "Per-channel scene-linear gain applied before tone mapping, formatted as \"r g b\"." );
	ri.Cvar_SetGroup( r_colorGradeGain, CVG_RENDERER );
	r_colorGradeWhitePoint = ri.Cvar_Get( "r_colorGradeWhitePoint", "6504", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_colorGradeWhitePoint, "1000", "40000", CV_FLOAT );
	ri.Cvar_SetDescription( r_colorGradeWhitePoint, "Source scene white point in Kelvin for Bradford chromatic adaptation." );
	ri.Cvar_SetGroup( r_colorGradeWhitePoint, CVG_RENDERER );
	r_colorGradeAdaptWhitePoint = ri.Cvar_Get( "r_colorGradeAdaptWhitePoint", "6504", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_colorGradeAdaptWhitePoint, "1000", "40000", CV_FLOAT );
	ri.Cvar_SetDescription( r_colorGradeAdaptWhitePoint, "Target scene white point in Kelvin for Bradford chromatic adaptation." );
	ri.Cvar_SetGroup( r_colorGradeAdaptWhitePoint, CVG_RENDERER );
	r_colorGradeLUT = ri.Cvar_Get( "r_colorGradeLUT", "", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_colorGradeLUT, "Optional 3D LUT atlas image for \\r_colorGrade 2/3. Layout is width N*N, height N, blue slices laid out horizontally." );
	ri.Cvar_SetGroup( r_colorGradeLUT, CVG_RENDERER );
	r_colorGradeLUTScale = ri.Cvar_Get( "r_colorGradeLUTScale", "4.0", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_colorGradeLUTScale, "1.0", "32.0", CV_FLOAT );
	ri.Cvar_SetDescription( r_colorGradeLUTScale, "Scene-linear range represented by the 3D LUT atlas. A value of 4 maps 0..4 scene-linear RGB into the LUT domain." );
	ri.Cvar_SetGroup( r_colorGradeLUTScale, CVG_RENDERER );
	// bloom
	r_bloom = ri.Cvar_Get( "r_bloom", "0", CVAR_ARCHIVE_ND );
	R_MakeCvarInstant( r_bloom ); // If we were running renderervk before, we need to remove latch
	ri.Cvar_SetDescription(r_bloom, "Enables bloom post-processing effect. Requires \\r_fbo 1.");
	r_flareSceneLinear = ri.Cvar_Get( "r_flareSceneLinear", "1", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_flareSceneLinear, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_flareSceneLinear, "Decode flare vertex colors into scene-linear RGB when \\r_hdr 1 is active. Set to 0 for legacy display-referred flare color parity." );
	ri.Cvar_SetGroup( r_flareSceneLinear, CVG_RENDERER );
	r_bloom_threshold = ri.Cvar_Get( "r_bloom_threshold", "0.75", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription(r_bloom_threshold, "Scene-linear color level to extract to bloom texture, default is 0.75.");
	ri.Cvar_SetGroup( r_bloom_threshold, CVG_RENDERER );
	r_bloom_threshold_mode = ri.Cvar_Get( "r_bloom_threshold_mode", "0", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_bloom_threshold_mode, "Color extraction mode:\n 0: (r|g|b) >= threshold\n 1: (r + g + b ) / 3 >= threshold\n 2: luma(r, g, b) >= threshold" );
	ri.Cvar_SetGroup( r_bloom_threshold_mode, CVG_RENDERER );
	r_bloom_soft_knee = ri.Cvar_Get( "r_bloom_soft_knee", "0.0", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_bloom_soft_knee, "0.0", "1.0", CV_FLOAT );
	ri.Cvar_SetDescription( r_bloom_soft_knee, "Softens scene-linear bloom extraction around \\r_bloom_threshold. 0 keeps the legacy hard cutoff; 1 uses a full threshold-width knee." );
	ri.Cvar_SetGroup( r_bloom_soft_knee, CVG_RENDERER );
	r_bloom_intensity = ri.Cvar_Get( "r_bloom_intensity", "0.5", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_bloom_intensity, "Final bloom blend factor, default is 0.5." );
	r_bloom_passes = ri.Cvar_Get( "r_bloom_passes", "5", CVAR_ARCHIVE_ND );
	R_MakeCvarInstant( r_bloom_passes );
	ri.Cvar_CheckRange( r_bloom_passes, "3", XSTRING( MAX_BLUR_PASSES ), CV_INTEGER );
	ri.Cvar_SetDescription( r_bloom_passes, "Count of downsampled passes (framebuffers) to blend on final bloom image, default is 5. Applies after the current frame." );
	ri.Cvar_SetGroup( r_bloom_passes, CVG_RENDERER );
	r_bloom_blend_base = ri.Cvar_Get( "r_bloom_blend_base", "1", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_bloom_blend_base, "0", va("%i", r_bloom_passes->integer-1), CV_INTEGER );
	ri.Cvar_SetDescription( r_bloom_blend_base, "0-based, topmost downsampled framebuffer to use for final image, high values can be used for stronger haze effect, results in overall weaker intensity." );
	ri.Cvar_SetGroup( r_bloom_blend_base, CVG_RENDERER );
	r_bloom_modulate = ri.Cvar_Get( "r_bloom_modulate", "0", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_bloom_modulate, "Modulate extracted color:\n 0: off (color = color, i.e. no changes)\n 1: by itself (color = color * color)\n 2: by intensity (color = color * luma(color))" );
	ri.Cvar_SetGroup( r_bloom_modulate, CVG_RENDERER );
	r_bloom_filter_size = ri.Cvar_Get( "r_bloom_filter_size", "6", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_bloom_filter_size, XSTRING( MIN_FILTER_SIZE ), XSTRING( MAX_FILTER_SIZE ), CV_INTEGER );
	ri.Cvar_SetDescription( r_bloom_filter_size, "Filter size of Gaussian Blur effect for each pass, bigger filter size means stronger and wider blur, lower values are faster, default is 6." );
	ri.Cvar_SetGroup( r_bloom_filter_size, CVG_RENDERER );

	r_bloom_reflection = ri.Cvar_Get( "r_bloom_reflection", "0", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_bloom_reflection, "-4", "4", CV_FLOAT );
	ri.Cvar_SetDescription( r_bloom_reflection, "Bloom lens reflection effect, value is an intensity factor of the effect, negative value means blend only reflection and skip main bloom texture." );
#endif // USE_FBO

	r_dlightBacks = ri.Cvar_Get( "r_dlightBacks", "1", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_dlightBacks, "Whether or not dynamic lights should light up back-face culled geometry, affects only VQ3 dynamic lights." );
	r_finish = ri.Cvar_Get( "r_finish", "0", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_finish, "Force a glFinish call after rendering a scene." );
	r_textureMode = ri.Cvar_Get( "r_textureMode", "GL_LINEAR_MIPMAP_NEAREST", CVAR_ARCHIVE );
	ri.Cvar_SetDescription( r_textureMode, "Texture interpolation mode:\n GL_NEAREST: Nearest neighbor interpolation and will therefore appear similar to Quake II except with the added colored lighting\n GL_LINEAR: Linear interpolation and will appear to blend in objects that are closer than the resolution that the textures are set as\n GL_NEAREST_MIPMAP_NEAREST: Nearest neighbor interpolation with mipmapping for bilinear hardware, mipmapping will blend objects that are farther away than the resolution that they are set as\n GL_LINEAR_MIPMAP_NEAREST: Linear interpolation with mipmapping for bilinear hardware\n GL_NEAREST_MIPMAP_LINEAR: Nearest neighbor interpolation with mipmapping for trilinear hardware\n GL_LINEAR_MIPMAP_LINEAR: Linear interpolation with mipmapping for trilinear hardware" );
	ri.Cvar_SetGroup( r_textureMode, CVG_RENDERER );
	r_gamma = ri.Cvar_Get( "r_gamma", "1", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_gamma, "0.5", "3", CV_FLOAT );
	ri.Cvar_SetDescription( r_gamma, "Gamma correction factor." );
	ri.Cvar_SetGroup( r_gamma, CVG_RENDERER );
	r_facePlaneCull = ri.Cvar_Get ("r_facePlaneCull", "1", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_facePlaneCull, "Enables culling of planar surfaces with back side test." );

	r_railWidth = ri.Cvar_Get( "r_railWidth", "16", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_railWidth, "Radius of railgun trails." );
	r_railCoreWidth = ri.Cvar_Get( "r_railCoreWidth", "6", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_railCoreWidth, "Size of railgun trail rings when enabled in game code (normally \\cg_oldRail 0)." );
	r_railSegmentLength = ri.Cvar_Get( "r_railSegmentLength", "32", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_railSegmentLength, "Length of segments in railgun trails." );

	r_ambientScale = ri.Cvar_Get( "r_ambientScale", "0.6", CVAR_CHEAT );
	ri.Cvar_SetDescription( r_ambientScale, "Light grid ambient light scaling on entity models." );
	r_directedScale = ri.Cvar_Get( "r_directedScale", "1", CVAR_CHEAT );
	ri.Cvar_SetDescription( r_directedScale, "Light grid direct light scaling on entity models." );

	r_anaglyphMode = ri.Cvar_Get( "r_anaglyphMode", "0", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_anaglyphMode, "Enable rendering of anaglyph images. Valid options for 3D glasses types:\n 0: Disabled\n 1: Red-cyan\n 2: Red-blue\n 3: Red-green\n 4: Green-magenta" );

	r_greyscale = ri.Cvar_Get( "r_greyscale", "0", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_greyscale, "-1", "1", CV_FLOAT );
	ri.Cvar_SetDescription( r_greyscale, "Desaturate rendered frame, requires \\r_fbo 1." );
	ri.Cvar_SetGroup( r_greyscale, CVG_RENDERER );

	//
	// temporary variables that can change at any time
	//
	r_showImages = ri.Cvar_Get( "r_showImages", "0", CVAR_TEMP );
	ri.Cvar_SetDescription( r_showImages, "Draw all images currently loaded into memory:\n 0: Disabled\n 1: Show images set to uniform size\n 2: Show images with scaled relative to largest image" );

	r_debugLight = ri.Cvar_Get( "r_debuglight", "0", CVAR_TEMP );
	ri.Cvar_SetDescription( r_debugLight, "Debugging tool to print ambient and directed lighting information." );
	r_debugSort = ri.Cvar_Get( "r_debugSort", "0", CVAR_CHEAT );
	ri.Cvar_SetDescription( r_debugSort, "Debugging tool to filter out shaders with depth sorting order values higher than the set value." );
	r_printShaders = ri.Cvar_Get( "r_printShaders", "0", 0 );
	ri.Cvar_SetDescription( r_printShaders, "Debugging tool to print on console of the number of shaders used." );
	r_saveFontData = ri.Cvar_Get( "r_saveFontData", "0", 0 );

	r_nocurves = ri.Cvar_Get ("r_nocurves", "0", CVAR_CHEAT );
	ri.Cvar_SetDescription( r_nocurves, "Set to 1 to disable drawing world bezier curves. Set to 0 to enable." );
	r_drawworld = ri.Cvar_Get ("r_drawworld", "1", CVAR_CHEAT );
	ri.Cvar_SetDescription( r_drawworld, "Set to 0 to disable drawing the world. Set to 1 to enable." );
	r_lightmap = ri.Cvar_Get ("r_lightmap", "0", 0 );
	ri.Cvar_SetDescription( r_lightmap, "Show only lightmaps on all world surfaces." );
	r_portalOnly = ri.Cvar_Get ("r_portalOnly", "0", CVAR_CHEAT );
	ri.Cvar_SetDescription( r_portalOnly, "Set to 1 to render only first mirror/portal view if it is present on the scene." );

	r_flareSize = ri.Cvar_Get( "r_flareSize", "40", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_flareSize, "Radius of light flares. Requires \\r_flares 1." );
	ri.Cvar_CheckRange( r_flareSize, "1", "40", CV_FLOAT );

	r_flareFade = ri.Cvar_Get( "r_flareFade", "10", CVAR_CHEAT );
	ri.Cvar_SetDescription( r_flareFade, "Distance to fade out light flares. Requires \\r_flares 1." );
	r_flareCoeff = ri.Cvar_Get( "r_flareCoeff", "150", CVAR_CHEAT );
	ri.Cvar_CheckRange( r_flareCoeff, "0.1", NULL, CV_FLOAT );
	ri.Cvar_SetDescription( r_flareCoeff, "Coefficient for the light flare intensity falloff function. Requires \\r_flares 1." );

	r_skipBackEnd = ri.Cvar_Get ("r_skipBackEnd", "0", CVAR_CHEAT);
	ri.Cvar_SetDescription( r_skipBackEnd, "Skips loading rendering backend." );

	r_lodscale = ri.Cvar_Get( "r_lodscale", "5", CVAR_CHEAT );
	ri.Cvar_SetDescription( r_lodscale, "Set scale for level of detail adjustment." );
	r_norefresh = ri.Cvar_Get ("r_norefresh", "0", CVAR_CHEAT);
	ri.Cvar_SetDescription( r_norefresh, "Bypasses refreshing of the rendered scene." );
	r_drawentities = ri.Cvar_Get ("r_drawentities", "1", CVAR_CHEAT );
	ri.Cvar_SetDescription( r_drawentities, "Draw all world entities." );
	r_nocull = ri.Cvar_Get ("r_nocull", "0", CVAR_CHEAT);
	ri.Cvar_SetDescription( r_nocull, "Draw all culled objects." );
	r_novis = ri.Cvar_Get ("r_novis", "0", CVAR_CHEAT);
	ri.Cvar_SetDescription( r_novis, "Disables usage of PVS." );
	r_showcluster = ri.Cvar_Get ("r_showcluster", "0", CVAR_CHEAT);
	ri.Cvar_SetDescription( r_showcluster, "Shows current cluster index." );
	r_speeds = ri.Cvar_Get ("r_speeds", "0", CVAR_CHEAT);
	ri.Cvar_SetDescription( r_speeds, "Prints out various debugging stats from PVS:\n 0: Disabled\n 1: Backend BSP\n 2: Frontend grid culling\n 3: Current view cluster index\n 4: Dynamic lighting, dlight shadow planning, and CSM planning\n 5: zFar clipping\n 6: Flares"
#ifdef RENDERER_GLX
		"\n 7: GLx capability, bootstrap counters, and GPU pass timings"
#endif
	);
	r_debugSurface = ri.Cvar_Get ("r_debugSurface", "0", CVAR_CHEAT);
	ri.Cvar_SetDescription( r_debugSurface, "Backend visual debugging tool for bezier mesh surfaces." );
	r_nobind = ri.Cvar_Get ("r_nobind", "0", CVAR_CHEAT);
	ri.Cvar_SetDescription( r_nobind, "Backend debugging tool: Disables texture binding." );
	r_showtris = ri.Cvar_Get ("r_showtris", "0", CVAR_CHEAT);
	ri.Cvar_SetDescription( r_showtris, "Debugging tool: Wireframe rendering of polygon triangles in the world." );
	r_showsky = ri.Cvar_Get( "r_showsky", "0", 0 );
	ri.Cvar_SetDescription( r_showsky, "Forces sky in front of all surfaces." );
	r_shownormals = ri.Cvar_Get ("r_shownormals", "0", CVAR_CHEAT);
	ri.Cvar_SetDescription( r_shownormals, "Debugging tool: Show wireframe surface normals." );
	r_clear = ri.Cvar_Get( "r_clear", "0", 0 );
	ri.Cvar_SetDescription( r_clear, "Forces screen buffer clearing every frame, removing any hall of mirrors effect in void.\n Use \\r_clearColor to set color." );
	r_offsetFactor = ri.Cvar_Get( "r_offsetFactor", "-1", CVAR_CHEAT );
	ri.Cvar_SetDescription( r_offsetFactor, "Offset factor for shaders with polygonOffset stages." );
	r_offsetUnits = ri.Cvar_Get( "r_offsetunits", "-2", CVAR_CHEAT );
	ri.Cvar_SetDescription( r_offsetUnits, "Offset units for shaders with polygonOffset stages." );
	r_drawBuffer = ri.Cvar_Get( "r_drawBuffer", "GL_BACK", CVAR_CHEAT );
	ri.Cvar_SetDescription( r_drawBuffer, "Sets which frame buffer to draw into." );
	r_lockpvs = ri.Cvar_Get ("r_lockpvs", "0", CVAR_CHEAT);
	ri.Cvar_SetDescription( r_lockpvs, "Debugging tool: Locks to current potentially visible set. Useful for testing vis-culling in maps." );
	r_noportals = ri.Cvar_Get( "r_noportals", "0", 0 );
	ri.Cvar_SetDescription(r_noportals, "Disables in-game portals, valid values: 0: Portals enabled\n 1: Portals disabled\n 2: Portals and mirrors disabled" );
	r_shadows = ri.Cvar_Get( "cg_shadows", "1", 0 );

	r_marksOnTriangleMeshes = ri.Cvar_Get("r_marksOnTriangleMeshes", "0", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_marksOnTriangleMeshes, "Enables impact marks on triangle mesh surfaces (ie: MD3 models.) Requires impact marks to be enabled in the game code." );

	r_aviMotionJpegQuality = ri.Cvar_Get( "r_aviMotionJpegQuality", "90", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_aviMotionJpegQuality, "Controls quality of Jpeg video capture when \\cl_aviMotionJpeg 1." );
	r_screenshotJpegQuality = ri.Cvar_Get( "r_screenshotJpegQuality", "90", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_screenshotJpegQuality, "Controls quality of Jpeg screenshots when using screenshotJpeg." );
	r_screenshotNameFormat = ri.Cvar_Get( "r_screenshotNameFormat", "shot-{date}-{time}", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_screenshotNameFormat, "Controls auto-generated screenshot names. Tokens: {map}, {date}, {time}, {datetime}, {iter[:width]}, {cmd}, {face}, {type}." );
	r_screenshotCaptureMode = ri.Cvar_Get( "r_screenshotCaptureMode", "0", CVAR_ARCHIVE_ND );
	R_MakeCvarInstant( r_screenshotCaptureMode );
	ri.Cvar_CheckRange( r_screenshotCaptureMode, "0", "2", CV_INTEGER );
	ri.Cvar_SetDescription( r_screenshotCaptureMode,
		"Screenshot/video capture export policy: 0 SDR sRGB after final output transform, 1 reserved scene-linear HDR request, 2 reserved HDR-output request. HDR modes currently resolve to SDR sRGB byte output." );
	r_screenshotWriteViewpos = ri.Cvar_Get( "r_screenshotWriteViewpos", "0", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_screenshotWriteViewpos, "Write a companion .txt file with map, origin, angles and setviewpos for each saved screenshot." );
	r_screenshotWatermark = ri.Cvar_Get( "r_screenshotWatermark", "", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_screenshotWatermark, "Optional watermark image path for screenshots. Supported formats: png, tga, jpg, jpeg, bmp." );
	r_screenshotWatermarkAlignment = ri.Cvar_Get( "r_screenshotWatermarkAlignment", "bottom-right", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_screenshotWatermarkAlignment, "Watermark alignment inside the 4:3 screenshot safe area: top-left, top, top-right, left, center, right, bottom-left, bottom, bottom-right." );
	r_screenshotWatermarkScreenAlignment = ri.Cvar_Get( "r_screenshotWatermarkScreenAlignment", "center", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_screenshotWatermarkScreenAlignment, "Alignment of the 4:3 watermark safe area inside the full screenshot." );
	r_screenshotWatermarkMargin = ri.Cvar_Get( "r_screenshotWatermarkMargin", "16", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_screenshotWatermarkMargin, "Margin in pixels between the watermark and the chosen alignment edges." );
	ri.Cvar_Set( "cl_captureActive", "0" );
	r_levelshotSize = ri.Cvar_Get( "r_levelshotSize", "", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_levelshotSize, "Controls levelshot output size. Blank keeps the resolved source size, a single integer makes a square image, and WxH sets an explicit output size." );
	r_levelshotDownscale = ri.Cvar_Get( "r_levelshotDownscale", "1", CVAR_ARCHIVE_ND );
	ri.Cvar_CheckRange( r_levelshotDownscale, "1", NULL, CV_FLOAT );
	ri.Cvar_SetDescription( r_levelshotDownscale, "Divides the resolved levelshot source size when r_levelshotSize is blank. 1 keeps full size, 2 halves it." );
	r_levelshotSourceAspect = ri.Cvar_Get( "r_levelshotSourceAspect", "", CVAR_ARCHIVE_ND );
	ri.Cvar_SetDescription( r_levelshotSourceAspect, "Optional centered source crop aspect for levelshots, such as 4:3, 16:9, or 1:1. Blank keeps the full viewport." );

	if ( glConfig.vidWidth )
		return;

	//
	// startup-probed variables that can only change over a vid_restart,
	// followed by FBO/runtime toggles with explicit end-of-frame rebuild paths.
	//
	r_allowExtensions = ri.Cvar_Get( "r_allowExtensions", "1", CVAR_ARCHIVE_ND | CVAR_LATCH | CVAR_DEVELOPER );
	ri.Cvar_SetDescription( r_allowExtensions, "Use all of the OpenGL extensions your card is capable of." );
	r_ext_compressed_textures = ri.Cvar_Get( "r_ext_compressed_textures", "0", CVAR_ARCHIVE_ND | CVAR_LATCH | CVAR_DEVELOPER );
	ri.Cvar_SetDescription( r_ext_compressed_textures, "Enables texture compression." );
	r_ext_multitexture = ri.Cvar_Get( "r_ext_multitexture", "1", CVAR_ARCHIVE_ND | CVAR_LATCH | CVAR_DEVELOPER );
	ri.Cvar_SetDescription( r_ext_multitexture, "Enables hardware multi-texturing (0: off, 1: on)." );
	r_ext_compiled_vertex_array = ri.Cvar_Get( "r_ext_compiled_vertex_array", "1", CVAR_ARCHIVE_ND | CVAR_LATCH | CVAR_DEVELOPER );
	ri.Cvar_SetDescription( r_ext_compiled_vertex_array, "Enables hardware-compiled vertex array rendering method." );
	r_ext_texture_env_add = ri.Cvar_Get( "r_ext_texture_env_add", "1", CVAR_ARCHIVE_ND | CVAR_LATCH | CVAR_DEVELOPER );
	ri.Cvar_SetDescription( r_ext_texture_env_add, "Enables additive blending in multitexturing. Requires \\r_ext_multitexture 1." );

	r_ext_texture_filter_anisotropic = ri.Cvar_Get( "r_ext_texture_filter_anisotropic",	"1", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_CheckRange( r_ext_texture_filter_anisotropic, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_ext_texture_filter_anisotropic, "Allow anisotropic filtering." );
	ri.Cvar_SetGroup( r_ext_texture_filter_anisotropic, CVG_RENDERER );

	r_ext_max_anisotropy = ri.Cvar_Get( "r_ext_max_anisotropy", "8", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_CheckRange( r_ext_max_anisotropy, "1", NULL, CV_INTEGER );
	ri.Cvar_SetDescription( r_ext_max_anisotropy, "Sets maximum anisotropic level for your graphics driver. Requires \\r_ext_texture_filter_anisotropic." );
	ri.Cvar_SetGroup( r_ext_max_anisotropy, CVG_RENDERER );

	//r_stencilbits = ri.Cvar_Get( "r_stencilbits", "8", CVAR_ARCHIVE_ND | CVAR_LATCH );
	r_ignorehwgamma = ri.Cvar_Get( "r_ignorehwgamma", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
	ri.Cvar_CheckRange( r_ignorehwgamma, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_ignorehwgamma, "Overrides hardware gamma capabilities." );

	r_flares = ri.Cvar_Get( "r_flares", "0", CVAR_ARCHIVE_ND );
	R_MakeCvarInstant( r_flares );
	ri.Cvar_SetDescription( r_flares, "Enables corona effects on light sources. Applies after the current frame." );
	ri.Cvar_SetGroup( r_flares, CVG_RENDERER );

#ifdef USE_FBO
	r_fbo = ri.Cvar_Get( "r_fbo", "0", CVAR_ARCHIVE_ND );
	R_MakeCvarInstant( r_fbo );
	ri.Cvar_SetDescription( r_fbo, "Use framebuffer objects, enables gamma correction in windowed mode and allows arbitrary video size and screenshot/video capture.\n Required for bloom, HDR rendering, anti-aliasing and greyscale effects.\n OpenGL 3.0+ required. Applies after the current frame." );
	ri.Cvar_SetGroup( r_fbo, CVG_RENDERER );

	r_ext_supersample = ri.Cvar_Get( "r_ext_supersample", "0", CVAR_ARCHIVE_ND );
	R_MakeCvarInstant( r_ext_supersample );
	ri.Cvar_CheckRange( r_ext_supersample, "0", "1", CV_INTEGER );
	ri.Cvar_SetDescription( r_ext_supersample, "Super-sample anti-aliasing, requires \\r_fbo 1. Applies after the current frame." );
	ri.Cvar_SetGroup( r_ext_supersample, CVG_RENDERER );

	r_renderWidth = ri.Cvar_Get( "r_renderWidth", "800", CVAR_ARCHIVE_ND );
	R_MakeCvarInstant( r_renderWidth );
	ri.Cvar_CheckRange( r_renderWidth, "96", NULL, CV_INTEGER );
	ri.Cvar_SetDescription( r_renderWidth, "Video width to render to when \\r_renderScale > 0. Applies after the current frame." );
	ri.Cvar_SetGroup( r_renderWidth, CVG_RENDERER );
	r_renderHeight = ri.Cvar_Get( "r_renderHeight", "600", CVAR_ARCHIVE_ND );
	R_MakeCvarInstant( r_renderHeight );
	ri.Cvar_CheckRange( r_renderHeight, "72", NULL, CV_INTEGER );
	ri.Cvar_SetDescription( r_renderHeight, "Video height to render to when \\r_renderScale > 0. Applies after the current frame." );
	ri.Cvar_SetGroup( r_renderHeight, CVG_RENDERER );

	r_renderScale = ri.Cvar_Get( "r_renderScale", "0", CVAR_ARCHIVE_ND );
	R_MakeCvarInstant( r_renderScale );
	ri.Cvar_CheckRange( r_renderScale, "0", "4", CV_INTEGER );
	ri.Cvar_SetDescription( r_renderScale, "Scaling mode to be used with custom render resolution:\n"
		" 0 - disabled\n"
		" 1 - nearest filtering, stretch to full size\n"
		" 2 - nearest filtering, preserve aspect ratio (black bars on sides)\n"
		" 3 - linear filtering, stretch to full size\n"
		" 4 - linear filtering, preserve aspect ratio (black bars on sides)\n"
		" Applies after the current frame.\n" );
	ri.Cvar_SetGroup( r_renderScale, CVG_RENDERER );
#endif // USE_FBO
}

#define EPSILON 1e-6f

/*
===============
R_Init
===============
*/
void R_Init( void ) {
	int	err;
	int i;
	byte *ptr;

	ri.Printf( PRINT_ALL, "----- R_Init -----\n" );

	// clear all our internal state
	Com_Memset( &tr, 0, sizeof( tr ) );
	Com_Memset( &backEnd, 0, sizeof( backEnd ) );
	Com_Memset( &tess, 0, sizeof( tess ) );
	Com_Memset( &glState, 0, sizeof( glState ) );

	if ( sizeof( glconfig_t ) != 11332 )
		ri.Error( ERR_FATAL, "Mod ABI incompatible: sizeof(glconfig_t) == %u != 11332", (unsigned int) sizeof( glconfig_t ) );

	if ( (intptr_t)tess.xyz & 15 ) {
		ri.Printf( PRINT_WARNING, "tess.xyz not 16 byte aligned\n" );
	}
	Com_Memset( tess.constantColor255, 255, sizeof( tess.constantColor255 ) );

	//
	// init function tables
	//
	for ( i = 0; i < FUNCTABLE_SIZE; i++ ) {
		tr.sinTable[i] = sin( DEG2RAD( i * 360.0f / FUNCTABLE_SIZE ) + 0.0001f );
		tr.squareTable[i] = (i < FUNCTABLE_SIZE / 2) ? 1.0f : -1.0f;
		if ( i == 0 ) {
			tr.sawToothTable[i] = EPSILON;
		} else {
			tr.sawToothTable[i] = (float)i / FUNCTABLE_SIZE;
		}
		tr.inverseSawToothTable[i] = 1.0f - tr.sawToothTable[i];
		if ( i < FUNCTABLE_SIZE / 2 ) {
			if ( i < FUNCTABLE_SIZE / 4 ) {
				if ( i == 0 ) {
					tr.triangleTable[i] = EPSILON;
				} else {
					tr.triangleTable[i] = (float)i / (FUNCTABLE_SIZE / 4);
				}
			} else {
				tr.triangleTable[i] = 1.0f - tr.triangleTable[i - FUNCTABLE_SIZE / 4];
			}
		} else {
			tr.triangleTable[i] = -tr.triangleTable[i - FUNCTABLE_SIZE / 2];
		}
	}

	R_InitFogTable();

	R_NoiseInit();

	R_Register();

	max_polys = r_maxpolys->integer;
	max_polyverts = r_maxpolyverts->integer;

	ptr = ri.Hunk_Alloc( sizeof( *backEndData ) + sizeof(srfPoly_t) * max_polys + sizeof(polyVert_t) * max_polyverts, h_low);
	backEndData = (backEndData_t *) ptr;
	backEndData->polys = (srfPoly_t *) ((char *) ptr + sizeof( *backEndData ));
	backEndData->polyVerts = (polyVert_t *) ((char *) ptr + sizeof( *backEndData ) + sizeof(srfPoly_t) * max_polys);

	R_InitNextFrame();

	InitOpenGL();

	R_InitImages();

	VarInfo();

	R_InitShaders();

	R_InitSkins();

	R_ModelInit();

	R_InitFreeType();

	err = qglGetError();
	if ( err != GL_NO_ERROR )
		ri.Printf( PRINT_WARNING, "glGetError() = 0x%x\n", err );

	ri.Printf( PRINT_ALL, "----- finished R_Init -----\n" );
}


/*
===============
RE_Shutdown
===============
*/
static void RE_Shutdown( refShutdownCode_t code ) {

	ri.Printf( PRINT_ALL, "RE_Shutdown( %i )\n", code );

	ri.Cmd_RemoveCommand( "modellist" );
	ri.Cmd_RemoveCommand( "advertlist" );
	ri.Cmd_RemoveCommand( "screenshotBMP" );
	ri.Cmd_RemoveCommand( "screenshotJPEG" );
	ri.Cmd_RemoveCommand( "screenshotTGA" );
	ri.Cmd_RemoveCommand( "screenshotPNG" );
	ri.Cmd_RemoveCommand( "screenshot" );
	ri.Cmd_RemoveCommand( "imagelist" );
	ri.Cmd_RemoveCommand( "shaderlist" );
	ri.Cmd_RemoveCommand( "skinlist" );
	ri.Cmd_RemoveCommand( "gfxinfo" );
#ifdef USE_PMLIGHT
	ri.Cmd_RemoveCommand( "r_dlightTest" );
#endif
	ri.Cmd_RemoveCommand( "r_staticLightReload" );
	ri.Cmd_RemoveCommand( "shaderstate" );
	GLX_CompatRemoveCommands();

	//if ( tr.registered ) {
		//R_IssuePendingRenderCommands();
		R_ShutdownAdvertisements();
		R_DeleteTextures();
	//}

	R_DoneFreeType();

	GLX_CompatShutdown( code );

	// shut down platform specific OpenGL stuff
	if ( code != REF_KEEP_CONTEXT ) {

		QGL_DoneARB();

#ifdef USE_VBO
		VBO_Cleanup();
#endif

		R_ClearSymTables();

		Com_Memset( &glState, 0, sizeof( glState ) );

		if ( code != REF_KEEP_WINDOW ) {
			if ( ri.GLimp_Shutdown ) {
				ri.GLimp_Shutdown( code == REF_UNLOAD_DLL ? qtrue : qfalse );
			}
			Com_Memset( &glConfig, 0, sizeof( glConfig ) );
		}
	}

	ri.FreeAll();

	tr.registered = qfalse;
	tr.inited = qfalse;
}


/*
=============
RE_EndRegistration

Touch all images to make sure they are resident (probably obsolete on modern systems)
=============
*/
static void RE_EndRegistration( void ) {
	//FBO_BindMain(); // otherwise we may draw images to the back buffer
	//R_IssuePendingRenderCommands();
	//if ( !ri.Sys_LowPhysicalMemory() ) {
	//	RB_ShowImages();
	//}
}


static qhandle_t RE_RegisterShaderFromRGBA( const char *name, byte *rgba,
		int width, int height ) {
	image_t *image;
	if ( !name || !name[0] || !rgba || width <= 0 || height <= 0
		|| width > 4096 || height > 4096 ) {
		return 0;
	}
	image = R_CreateImage( name, NULL, rgba, width, height,
		IMGFLAG_CLAMPTOEDGE | IMGFLAG_NO_COMPRESSION | IMGFLAG_NOSCALE );
	return image ? RE_RegisterShaderFromImage( name, LIGHTMAP_2D, image, qfalse ) : 0;
}

/*
@@@@@@@@@@@@@@@@@@@@@
GetRefAPI
@@@@@@@@@@@@@@@@@@@@@
*/
#ifdef USE_RENDERER_DLOPEN
Q_EXPORT refexport_t* QDECL GetRefAPI ( int apiVersion, refimport_t *rimp ) {
#else
refexport_t *GetRefAPI ( int apiVersion, refimport_t *rimp ) {
#endif

	static refexport_t	re;

	ri = *rimp;
	GLX_CompatSetImports( &ri );

	Com_Memset( &re, 0, sizeof( re ) );

	if ( apiVersion != REF_API_VERSION ) {
		ri.Printf(PRINT_ALL, "Mismatched REF_API_VERSION: expected %i, got %i\n",
			REF_API_VERSION, apiVersion );
		return NULL;
	}

	// the RE_ functions are Renderer Entry points

	re.Shutdown = RE_Shutdown;

	re.BeginRegistration = RE_BeginRegistration;
	re.RegisterModel = RE_RegisterModel;
	re.RegisterSkin = RE_RegisterSkin;
	re.RegisterShader = RE_RegisterShader;
	re.RegisterShaderNoMip = RE_RegisterShaderNoMip;
	re.LoadWorld = RE_LoadWorldMap;
	re.SetWorldVisData = RE_SetWorldVisData;
	re.EndRegistration = RE_EndRegistration;

	re.BeginFrame = RE_BeginFrame;
	re.EndFrame = RE_EndFrame;

	re.MarkFragments = R_MarkFragments;
	re.LerpTag = R_LerpTag;
	re.ModelBounds = R_ModelBounds;

	re.ClearScene = RE_ClearScene;
	re.AddRefEntityToScene = RE_AddRefEntityToScene;
	re.AddPolyToScene = RE_AddPolyToScene;
	re.LightForPoint = R_LightForPoint;
	re.AddLightToScene = RE_AddLightToScene;
	re.AddAdditiveLightToScene = RE_AddAdditiveLightToScene;
	re.AddLinearLightToScene = RE_AddLinearLightToScene;

	re.RenderScene = RE_RenderScene;
	re.AdvertisementBridge_UpdateLoadingViewParameters = AdvertisementBridge_UpdateLoadingViewParameters;

	re.SetColor = RE_SetColor;
	re.DrawStretchPic = RE_StretchPic;
	re.DrawStretchRaw = RE_StretchRaw;
	re.UploadCinematic = RE_UploadCinematic;
	re.DrawWebUISurface = RE_DrawWebUISurface;
	re.RegisterShaderFromRGBA = RE_RegisterShaderFromRGBA;

	re.RegisterFont = RE_RegisterFont;
	re.DrawScaledText = RE_DrawScaledText;
	re.MeasureScaledText = RE_MeasureScaledText;
	re.GetScaledFontMetrics = RE_GetScaledFontMetrics;
	re.GetFontAtlasDebugShader = RE_GetFontAtlasDebugShader;
	re.RemapShader = RE_RemapShader;
	re.GetEntityToken = RE_GetEntityToken;
	re.inPVS = R_inPVS;

	re.TakeVideoFrame = RE_TakeVideoFrame;
	re.SetColorMappings = R_SetColorMappings;

	re.ThrottleBackend = RE_ThrottleBackend;
	re.FinishBloom = RE_FinishBloom;
	re.DrawMenuDepthOfField = RE_DrawMenuDepthOfField;
	re.CanMinimize = RE_CanMinimize;
	re.GetConfig = RE_GetConfig;
	re.VertexLighting = RE_VertexLighting;
	re.SyncRender = RE_SyncRender;

	return &re;
}
