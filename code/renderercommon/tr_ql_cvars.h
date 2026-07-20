/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL.

FnQL is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 2 of the License, or (at your option) any later
version.
===========================================================================
*/

#ifndef TR_QL_CVARS_H
#define TR_QL_CVARS_H

/*
 * Retail Quake Live added a small renderer-control surface on top of the
 * Quake III cvars.  Keep those controls in one renderer-neutral contract so
 * the GLx, Vulkan, and RTX backends cannot drift apart.
 *
 * These are compatibility controls, not replacements for FnQL's newer HDR,
 * color-grade, and bloom controls. They remain queryable for retail modules
 * and configurations, but the FnQ3 renderer controls stay authoritative.
 */
typedef enum qlRendererCvarBackend_e {
	QL_CVAR_BACKEND_GLX,
	QL_CVAR_BACKEND_VULKAN,
	QL_CVAR_BACKEND_RTX
} qlRendererCvarBackend_t;

typedef struct qlRendererCvars_s {
	cvar_t *fastSkyColor;
	cvar_t *drawSkyFloor;
	cvar_t *forceMergeEntities;
	cvar_t *ignoreFastPath;
	cvar_t *primitives;
	cvar_t *smp;
	cvar_t *showSmp;
	cvar_t *uiFullscreen;
	cvar_t *skipSmallBatches;
	cvar_t *skipLargeBatches;
	cvar_t *debugShaderIndex;
	cvar_t *debugSortExcept;
	cvar_t *debugAds;
	cvar_t *floatingPointFBOs;
	cvar_t *enablePostProcess;
	cvar_t *enableBloom;
	cvar_t *enableColorCorrect;
	cvar_t *postProcessActive;
	cvar_t *bloomActive;
	cvar_t *colorCorrectActive;
	cvar_t *bloomPasses;
	cvar_t *bloomIntensity;
	cvar_t *bloomBrightThreshold;
	cvar_t *bloomBlurScale;
	cvar_t *bloomBlurRadius;
	cvar_t *bloomBlurFalloff;
	cvar_t *bloomSaturation;
	cvar_t *bloomSceneIntensity;
	cvar_t *bloomSceneSaturation;
	cvar_t *contrast;
	cvar_t *postProcessBridge;
	qlRendererCvarBackend_t backend;
} qlRendererCvars_t;

extern qlRendererCvars_t qlRendererCvars;

static ID_INLINE cvar_t *R_QLRegisterCvar( const char *name,
	const char *value, int flags, const char *minValue, const char *maxValue,
	cvarValidator_t validator, const char *description )
{
	cvar_t *cvar = ri.Cvar_Get( name, value, flags );

	if ( minValue || maxValue || validator != CV_NONE ) {
		ri.Cvar_CheckRange( cvar, minValue, maxValue, validator );
	}
	ri.Cvar_SetDescription( cvar, description );
	ri.Cvar_SetGroup( cvar, CVG_RENDERER );
	return cvar;
}

static ID_INLINE void R_QLRegisterRendererCvars( qlRendererCvarBackend_t backend )
{
	const int profileFlags = CVAR_ARCHIVE | CVAR_CLOUD;
	const int boundedProfileFlags = profileFlags | CVAR_PROTECTED | CVAR_VM_CREATED;
	const int postFlags = CVAR_ARCHIVE | CVAR_LATCH | CVAR_CLOUD;
	qlRendererCvars.backend = backend;

	qlRendererCvars.fastSkyColor = R_QLRegisterCvar( "r_fastSkyColor", "0x000000",
		CVAR_ARCHIVE | CVAR_PROTECTED | CVAR_CLOUD, NULL, NULL, CV_NONE,
		"Packed 0xRRGGBB clear color used by r_fastsky." );
	qlRendererCvars.drawSkyFloor = R_QLRegisterCvar( "r_drawSkyFloor", "1",
		profileFlags, "0", "1", CV_INTEGER,
		"Draw the bottom face of sky and cloud boxes." );
	qlRendererCvars.forceMergeEntities = R_QLRegisterCvar( "r_forceMergeEntities", "0",
		CVAR_CHEAT, "0", "1", CV_INTEGER,
		"Allow compatible draw surfaces to batch across entity boundaries." );
	qlRendererCvars.ignoreFastPath = R_QLRegisterCvar( "r_ignoreFastPath", "1",
		CVAR_ARCHIVE | CVAR_LATCH, "0", "1", CV_INTEGER,
		"Suppress retail's fixed-function tessellation fast path. FnQL renderers use the general path." );
	qlRendererCvars.primitives = R_QLRegisterCvar( "r_primitives", "0",
		CVAR_ARCHIVE, "0", "3", CV_INTEGER,
		"Retail primitive submission selector; modern FnQL backends safely use indexed submission." );
	qlRendererCvars.smp = R_QLRegisterCvar( "r_smp", "0", CVAR_ROM,
		"0", "0", CV_INTEGER,
		"Retail renderer-thread status. FnQL renderers submit deterministically on one renderer thread." );
	qlRendererCvars.showSmp = R_QLRegisterCvar( "r_showSmp", "0", CVAR_CHEAT,
		"0", "1", CV_INTEGER,
		"Print retail renderer-thread stall diagnostics; inert while r_smp reports the single-threaded FnQL path." );
	qlRendererCvars.uiFullscreen = R_QLRegisterCvar( "r_uifullscreen", "0", 0,
		"0", "1", CV_INTEGER,
		"Set by the UI while a full-screen menu is active." );
	qlRendererCvars.skipSmallBatches = R_QLRegisterCvar( "r_skipSmallBatches", "0",
		CVAR_CHEAT, "0", "1", CV_INTEGER,
		"Skip draw batches containing at most 512 indexes for diagnostics." );
	qlRendererCvars.skipLargeBatches = R_QLRegisterCvar( "r_skipLargeBatches", "0",
		CVAR_CHEAT, "0", "1", CV_INTEGER,
		"Skip draw batches containing at least 2000 indexes for diagnostics." );
	qlRendererCvars.debugShaderIndex = R_QLRegisterCvar( "r_debugShaderIndex", "0",
		CVAR_CHEAT, NULL, NULL, CV_INTEGER,
		"Highlight a single shader index with the triangle diagnostic overlay." );
	qlRendererCvars.debugSortExcept = R_QLRegisterCvar( "r_debugSortExcept", "0",
		CVAR_CHEAT, NULL, NULL, CV_INTEGER,
		"Shader sort value exempted from r_debugSort filtering." );
	qlRendererCvars.debugAds = R_QLRegisterCvar( "r_debugAds", "0", CVAR_TEMP,
		NULL, NULL, CV_INTEGER,
		"Print a bounded retail advertisement-surface diagnostic snapshot." );
	qlRendererCvars.floatingPointFBOs = R_QLRegisterCvar( "r_floatingPointFBOs", "0",
		CVAR_ARCHIVE | CVAR_LATCH, "0", "1", CV_INTEGER,
		"Request retail floating-point scene storage independently of FnQL's HDR lighting mode." );

	qlRendererCvars.enablePostProcess = R_QLRegisterCvar( "r_enablePostProcess", "1",
		postFlags, "0", "1", CV_INTEGER,
		"Retail post-process compatibility control; FnQ3 renderer controls remain authoritative." );
	qlRendererCvars.enableBloom = R_QLRegisterCvar( "r_enableBloom", "1",
		postFlags, "0", "2", CV_INTEGER,
		"Retail bloom compatibility selector; r_bloom owns the FnQ3 bloom pass." );
	qlRendererCvars.enableColorCorrect = R_QLRegisterCvar( "r_enableColorCorrect", "1",
		postFlags, "0", "1", CV_INTEGER,
		"Retail color-correction compatibility control; r_colorGrade owns FnQ3 color grading." );
	/* Renderer-owned mirrors are temporary and overwritten every frame. */
	qlRendererCvars.postProcessActive = R_QLRegisterCvar( "r_postProcessActive", "0",
		CVAR_TEMP, "0", "1", CV_INTEGER,
		"Reports whether the active renderer is running a post-process path." );
	qlRendererCvars.bloomActive = R_QLRegisterCvar( "r_bloomActive", "0",
		CVAR_TEMP | CVAR_CLOUD, "0", "1", CV_INTEGER,
		"Reports whether bloom is active in the current renderer." );
	qlRendererCvars.colorCorrectActive = R_QLRegisterCvar( "r_colorCorrectActive", "0",
		CVAR_TEMP, "0", "1", CV_INTEGER,
		"Reports whether color correction is active in the current renderer." );
	qlRendererCvars.bloomPasses = R_QLRegisterCvar( "r_bloomPasses", "1",
		postFlags | CVAR_PROTECTED | CVAR_VM_CREATED | CVAR_BOUNDED_DISCRETE,
		"1", "2", CV_INTEGER,
		"Retail bloom pass selector." );
	qlRendererCvars.bloomIntensity = R_QLRegisterCvar( "r_bloomIntensity", "0.5",
		boundedProfileFlags, "0", "10", CV_FLOAT, "Retail bloom blend intensity." );
	qlRendererCvars.bloomBrightThreshold = R_QLRegisterCvar( "r_bloomBrightThreshold", "0.25",
		boundedProfileFlags, "0", "1", CV_FLOAT, "Retail bloom extraction threshold." );
	qlRendererCvars.bloomBlurScale = R_QLRegisterCvar( "r_bloomBlurScale", "0.0",
		boundedProfileFlags, "1", "2", CV_FLOAT, "Retail bloom blur scale." );
	qlRendererCvars.bloomBlurRadius = R_QLRegisterCvar( "r_bloomBlurRadius", "5",
		boundedProfileFlags, "1", "12", CV_FLOAT, "Retail bloom blur radius." );
	qlRendererCvars.bloomBlurFalloff = R_QLRegisterCvar( "r_bloomBlurFalloff", "0.0",
		boundedProfileFlags, "0.75", "1", CV_FLOAT, "Retail bloom blur falloff." );
	qlRendererCvars.bloomSaturation = R_QLRegisterCvar( "r_bloomSaturation", "0.8",
		boundedProfileFlags, "0", "10", CV_FLOAT, "Retail bloom saturation." );
	qlRendererCvars.bloomSceneIntensity = R_QLRegisterCvar( "r_bloomSceneIntensity", "1.0",
		boundedProfileFlags, "0", "10", CV_FLOAT, "Retail scene intensity during bloom combine." );
	qlRendererCvars.bloomSceneSaturation = R_QLRegisterCvar( "r_bloomSceneSaturation", "1.0",
		boundedProfileFlags, "0", "10", CV_FLOAT, "Retail scene saturation during bloom combine." );
	qlRendererCvars.contrast = R_QLRegisterCvar( "r_contrast", "1.0", profileFlags,
		NULL, NULL, CV_FLOAT, "Retail color-correction contrast." );
	qlRendererCvars.postProcessBridge = R_QLRegisterCvar( "r_qlRetailPostProcessBridge", "0",
		CVAR_ROM, "0", "0", CV_INTEGER,
		"Reports that retail post-process cvars do not own FnQ3 renderer effects." );
	(void)R_QLRegisterCvar( "r_qlOcclusionQueries", "0", CVAR_TEMP | CVAR_PROTECTED,
		"0", "1", CV_INTEGER,
		"Internal common-backend occlusion-query capability status." );

	/* Older FnQL builds persisted this as an ownership marker. The ROM
	 * registration above pins it to zero so a retained profile cannot resume
	 * the retired bridge and overwrite r_bloom_* on a later renderer restart. */
	ri.Cvar_Set( "r_qlRetailPostProcessBridge", "0" );
}

static ID_INLINE void R_QLUpdateRendererCvars( const cvar_t *postProcess,
	const cvar_t *bloom, const cvar_t *colorCorrect )
{
	const qboolean postActive = postProcess ? ( postProcess->integer != 0 )
		: ( ( bloom && bloom->integer ) || ( colorCorrect && colorCorrect->integer ) );
	const qboolean bloomActive = postActive && bloom && bloom->integer;
	const qboolean colorActive = postActive && colorCorrect && colorCorrect->integer;

	if ( qlRendererCvars.postProcessActive->integer != postActive ) {
		ri.Cvar_Set( "r_postProcessActive", postActive ? "1" : "0" );
	}
	if ( qlRendererCvars.bloomActive->integer != bloomActive ) {
		ri.Cvar_Set( "r_bloomActive", bloomActive ? "1" : "0" );
	}
	if ( qlRendererCvars.colorCorrectActive->integer != colorActive ) {
		ri.Cvar_Set( "r_colorCorrectActive", colorActive ? "1" : "0" );
	}
}

static ID_INLINE void R_QLFastSkyColor( vec4_t color )
{
	char *end = NULL;
	unsigned long rgb = 0;
	const char *value = qlRendererCvars.fastSkyColor
		? qlRendererCvars.fastSkyColor->string : NULL;

	if ( value && value[0] ) {
		rgb = strtoul( value, &end, 0 );
		if ( end == value ) {
			rgb = (unsigned long)qlRendererCvars.fastSkyColor->integer;
		}
	}
	color[0] = ( ( rgb >> 16 ) & 0xff ) / 255.0f;
	color[1] = ( ( rgb >> 8 ) & 0xff ) / 255.0f;
	color[2] = ( rgb & 0xff ) / 255.0f;
	color[3] = 1.0f;
}

static ID_INLINE qboolean R_QLSkipBatch( int numIndexes )
{
	return ( qlRendererCvars.skipSmallBatches->integer && numIndexes > 0
			&& numIndexes <= 512 )
		|| ( qlRendererCvars.skipLargeBatches->integer && numIndexes >= 2000 )
		? qtrue : qfalse;
}

static ID_INLINE float R_QLRetailContrast( void )
{
	return 1.0f;
}

#endif /* TR_QL_CVARS_H */
