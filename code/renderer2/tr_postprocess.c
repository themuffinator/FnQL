/*
===========================================================================
Copyright (C) 2011 Andrei Drexler, Richard Allen, James Canete

This file is part of Reaction source code.

Reaction source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Reaction source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Reaction source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "tr_local.h"

#define AUTO_EXPOSURE_SEED_SIZE          256
#define AUTO_EXPOSURE_LOG_LUMINANCE_MIN -10.0f
#define AUTO_EXPOSURE_LOG_LUMINANCE_MAX  10.0f
#define AUTO_EXPOSURE_EV_MIN            -16.0f
#define AUTO_EXPOSURE_EV_MAX             16.0f
#define AUTO_EXPOSURE_MAX_DELTA_SECONDS   0.25f
#define AUTO_EXPOSURE_EXP2_E              1.4426950408889634f
#define AUTO_EXPOSURE_FLOAT_TAU_SECONDS   0.55f
#define AUTO_EXPOSURE_FIXED_TAU_SECONDS   0.16f
#define AUTO_EXPOSURE_HISTOGRAM_LOW_INDEX 1
#define AUTO_EXPOSURE_HISTOGRAM_MID_INDEX 8
#define AUTO_EXPOSURE_HISTOGRAM_HIGH_INDEX 14
#define TONEMAP_LINEAR_MIN_FALLBACK       0.00390625f
#define TONEMAP_LINEAR_AVG_FALLBACK       0.25f
#define TONEMAP_LINEAR_MAX_FALLBACK       1.0f

typedef struct
{
	autoExposureMode_t mode;
	qboolean initialized;
	int lastFrameCount;
	int lastTime;
} autoExposureState_t;

static autoExposureState_t autoExposureState;

static float RB_ClampFiniteFloat(float value, float minValue, float maxValue, float fallback)
{
	if (Q_isnan(value))
		value = fallback;

	if (value < minValue)
		return minValue;
	if (value > maxValue)
		return maxValue;

	return value;
}

static qboolean RB_AutoExposureHistogramPercentileAvailable(void)
{
	// The percentile path depends on float targets preserving log-luminance bins
	// through the reduction chain. Older/fixed-point tiers keep the robust mode-1
	// reduction instead of silently changing exposure behavior.
	return glRefConfig.textureFloat ? qtrue : qfalse;
}

static autoExposureMode_t RB_GetAutoExposureMode(int autoExposure)
{
	if (!autoExposure)
		return AUTO_EXPOSURE_MODE_OFF;

	if (r_autoExposure && r_autoExposure->integer >= AUTO_EXPOSURE_MODE_HISTOGRAM_PERCENTILE)
	{
		if (RB_AutoExposureHistogramPercentileAvailable())
			return AUTO_EXPOSURE_MODE_HISTOGRAM_PERCENTILE;

		return AUTO_EXPOSURE_MODE_TIME_CONSTANT;
	}

	if (r_autoExposure && r_autoExposure->integer >= AUTO_EXPOSURE_MODE_LEGACY)
		return AUTO_EXPOSURE_MODE_LEGACY;

	return AUTO_EXPOSURE_MODE_TIME_CONSTANT;
}

static void RB_SanitizeToneMapInputs(void)
{
	float autoExposureMin = RB_ClampFiniteFloat(tr.refdef.autoExposureMinMax[0],
		AUTO_EXPOSURE_LOG_LUMINANCE_MIN, AUTO_EXPOSURE_LOG_LUMINANCE_MAX, -2.0f);
	float autoExposureMax = RB_ClampFiniteFloat(tr.refdef.autoExposureMinMax[1],
		AUTO_EXPOSURE_LOG_LUMINANCE_MIN, AUTO_EXPOSURE_LOG_LUMINANCE_MAX, 2.0f);
	float toneMin = RB_ClampFiniteFloat(tr.refdef.toneMinAvgMaxLinear[0],
		0.000001f, 1048576.0f, TONEMAP_LINEAR_MIN_FALLBACK);
	float toneAvg = RB_ClampFiniteFloat(tr.refdef.toneMinAvgMaxLinear[1],
		0.000001f, 1048576.0f, TONEMAP_LINEAR_AVG_FALLBACK);
	float toneMax = RB_ClampFiniteFloat(tr.refdef.toneMinAvgMaxLinear[2],
		0.000001f, 1048576.0f, TONEMAP_LINEAR_MAX_FALLBACK);

	if (autoExposureMin > autoExposureMax)
	{
		float tmp = autoExposureMin;
		autoExposureMin = autoExposureMax;
		autoExposureMax = tmp;
	}

	if (toneMin > toneMax)
	{
		float tmp = toneMin;
		toneMin = toneMax;
		toneMax = tmp;
	}

	if (toneMax <= toneMin + 0.000001f)
		toneMax = toneMin + 0.000001f;

	toneAvg = RB_ClampFiniteFloat(toneAvg, toneMin, toneMax, TONEMAP_LINEAR_AVG_FALLBACK);

	tr.refdef.autoExposureMinMax[0] = autoExposureMin;
	tr.refdef.autoExposureMinMax[1] = autoExposureMax;
	tr.refdef.toneMinAvgMaxLinear[0] = toneMin;
	tr.refdef.toneMinAvgMaxLinear[1] = toneAvg;
	tr.refdef.toneMinAvgMaxLinear[2] = toneMax;
}

static float RB_CameraExposureScale(qboolean autoExposureEnabled)
{
	float cameraExposure = RB_ClampFiniteFloat(r_cameraExposure->value,
		AUTO_EXPOSURE_EV_MIN, AUTO_EXPOSURE_EV_MAX, 1.0f);
	float exposureEV = cameraExposure - (autoExposureEnabled ? 1.0f : 0.0f);

	exposureEV = RB_ClampFiniteFloat(exposureEV,
		AUTO_EXPOSURE_EV_MIN, AUTO_EXPOSURE_EV_MAX, autoExposureEnabled ? 0.0f : 1.0f);

	return Q_exp2f(exposureEV);
}

static void RB_CalculateAutoExposureTargetLegacy(FBO_t *hdrFbo, ivec4_t hdrBox)
{
	FBO_t *srcFbo, *dstFbo, *tmp;
	ivec4_t srcBox, dstBox;
	int size = AUTO_EXPOSURE_SEED_SIZE;

	VectorSet4(dstBox, 0, 0, size, size);

	FBO_Blit(hdrFbo, hdrBox, NULL, tr.textureScratchFbo[0], dstBox, &tr.calclevels4xShader[0], NULL, 0);

	srcFbo = tr.textureScratchFbo[0];
	dstFbo = tr.textureScratchFbo[1];

	// Legacy parity path: keep the old frame-cadenced GL_LINEAR mip-style reduction.
	while (size > 1)
	{
		VectorSet4(srcBox, 0, 0, size, size);
		size >>= 1;
		VectorSet4(dstBox, 0, 0, size, size);

		if (size == 1)
			dstFbo = tr.targetLevelsFbo;

		FBO_FastBlit(srcFbo, srcBox, dstFbo, dstBox, GL_COLOR_BUFFER_BIT, GL_LINEAR);

		tmp = srcFbo;
		srcFbo = dstFbo;
		dstFbo = tmp;
	}
}

static void RB_CalculateAutoExposureTargetRobust(FBO_t *hdrFbo, ivec4_t hdrBox)
{
	FBO_t *srcFbo, *dstFbo, *tmp;
	ivec4_t srcBox, dstBox;
	int size = AUTO_EXPOSURE_SEED_SIZE;

	VectorSet4(dstBox, 0, 0, size, size);

	FBO_Blit(hdrFbo, hdrBox, NULL, tr.textureScratchFbo[0], dstBox, &tr.calclevels4xShader[0], NULL, 0);

	srcFbo = tr.textureScratchFbo[0];
	dstFbo = tr.textureScratchFbo[1];

	// The reduction shader preserves average log luminance while tracking min/max,
	// avoiding driver-dependent GL_LINEAR filtering of packed statistics.
	while (size > 1)
	{
		VectorSet4(srcBox, 0, 0, size, size);
		size = (size > 4) ? (size >> 2) : 1;
		VectorSet4(dstBox, 0, 0, size, size);

		if (size == 1)
			dstFbo = tr.targetLevelsFbo;

		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, &tr.calclevels4xShader[1], NULL, 0);

		tmp = srcFbo;
		srcFbo = dstFbo;
		dstFbo = tmp;
	}
}

static void RB_CalculateAutoExposureTargetHistogramPercentile(FBO_t *hdrFbo, ivec4_t hdrBox)
{
	FBO_t *srcFbo, *dstFbo, *tmp;
	ivec4_t srcBox, dstBox;
	int size = AUTO_EXPOSURE_SEED_SIZE;

	VectorSet4(dstBox, 0, 0, size, size);

	FBO_Blit(hdrFbo, hdrBox, NULL, tr.textureScratchFbo[0], dstBox, &tr.calclevels4xShader[0], NULL, 0);

	srcFbo = tr.textureScratchFbo[0];
	dstFbo = tr.textureScratchFbo[1];

	// Modern-tier path: each 4x4 reduction sorts the local log-luminance
	// histogram and carries low/median/high percentile samples instead of
	// frame-global min/mean/max outliers.
	while (size > 1)
	{
		VectorSet4(srcBox, 0, 0, size, size);
		size = (size > 4) ? (size >> 2) : 1;
		VectorSet4(dstBox, 0, 0, size, size);

		if (size == 1)
			dstFbo = tr.targetLevelsFbo;

		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, &tr.calclevels4xShader[2], NULL, 0);

		tmp = srcFbo;
		srcFbo = dstFbo;
		dstFbo = tmp;
	}
}

static void RB_BlendAutoExposureTarget(float alpha)
{
	vec4_t color;

	alpha = RB_ClampFiniteFloat(alpha, 0.0f, 1.0f, 1.0f);
	if (alpha <= 0.0f)
		return;

	color[0] =
	color[1] =
	color[2] = 1.0f;
	color[3] = alpha;

	if (alpha >= 1.0f)
		FBO_Blit(tr.targetLevelsFbo, NULL, NULL, tr.calcLevelsFbo, NULL, NULL, color, 0);
	else
		FBO_Blit(tr.targetLevelsFbo, NULL, NULL, tr.calcLevelsFbo, NULL, NULL, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
}

static float RB_TimeConstantAutoExposureAlpha(autoExposureMode_t mode, qboolean *updateTarget)
{
	int now = ri.Milliseconds();
	float deltaSeconds;
	float tauSeconds;
	float alpha;
	qboolean reset;

	*updateTarget = qfalse;

	reset = !autoExposureState.initialized ||
		autoExposureState.mode != mode ||
		tr.frameCount < autoExposureState.lastFrameCount ||
		now < autoExposureState.lastTime;

	if (autoExposureState.initialized && tr.frameCount == autoExposureState.lastFrameCount)
		return 0.0f;

	*updateTarget = qtrue;

	if (reset)
	{
		autoExposureState.initialized = qtrue;
		autoExposureState.mode = mode;
		autoExposureState.lastFrameCount = tr.frameCount;
		autoExposureState.lastTime = now;
		return 1.0f;
	}

	deltaSeconds = (now - autoExposureState.lastTime) * 0.001f;

	autoExposureState.mode = mode;
	autoExposureState.lastFrameCount = tr.frameCount;
	autoExposureState.lastTime = now;

	deltaSeconds = RB_ClampFiniteFloat(deltaSeconds, 0.0f, AUTO_EXPOSURE_MAX_DELTA_SECONDS, 0.0f);
	if (deltaSeconds <= 0.0f)
		return 0.0f;

	tauSeconds = glRefConfig.textureFloat ? AUTO_EXPOSURE_FLOAT_TAU_SECONDS : AUTO_EXPOSURE_FIXED_TAU_SECONDS;
	alpha = 1.0f - Q_exp2f((-deltaSeconds * AUTO_EXPOSURE_EXP2_E) / tauSeconds);

	return RB_ClampFiniteFloat(alpha, 0.0f, 1.0f, 0.0f);
}

static void RB_UpdateAutoExposureLegacy(FBO_t *hdrFbo, ivec4_t hdrBox)
{
	static int lastFrameCount = 0;
	vec4_t color;
	ivec4_t srcBox;

	if (lastFrameCount == 0 || tr.frameCount < lastFrameCount || tr.frameCount - lastFrameCount > 5)
	{
		lastFrameCount = tr.frameCount;
		RB_CalculateAutoExposureTargetLegacy(hdrFbo, hdrBox);
	}

	// Blend with old log luminance for gradual change.
	VectorSet4(srcBox, 0, 0, 0, 0);

	color[0] =
	color[1] =
	color[2] = 1.0f;
	if (glRefConfig.textureFloat)
		color[3] = 0.03f;
	else
		color[3] = 0.1f;

	FBO_Blit(tr.targetLevelsFbo, srcBox, NULL, tr.calcLevelsFbo, NULL, NULL, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
}

static void RB_UpdateAutoExposureTimeConstant(FBO_t *hdrFbo, ivec4_t hdrBox)
{
	qboolean updateTarget;
	float alpha = RB_TimeConstantAutoExposureAlpha(AUTO_EXPOSURE_MODE_TIME_CONSTANT, &updateTarget);

	if (!updateTarget)
		return;

	RB_CalculateAutoExposureTargetRobust(hdrFbo, hdrBox);
	RB_BlendAutoExposureTarget(alpha);
}

static void RB_UpdateAutoExposureHistogramPercentile(FBO_t *hdrFbo, ivec4_t hdrBox)
{
	qboolean updateTarget;
	float alpha = RB_TimeConstantAutoExposureAlpha(AUTO_EXPOSURE_MODE_HISTOGRAM_PERCENTILE, &updateTarget);

	if (!updateTarget)
		return;

	if (!RB_AutoExposureHistogramPercentileAvailable())
		RB_CalculateAutoExposureTargetRobust(hdrFbo, hdrBox);
	else
		RB_CalculateAutoExposureTargetHistogramPercentile(hdrFbo, hdrBox);

	RB_BlendAutoExposureTarget(alpha);
}

void RB_ToneMap(FBO_t *hdrFbo, ivec4_t hdrBox, FBO_t *ldrFbo, ivec4_t ldrBox, int autoExposure)
{
	vec4_t color;
	autoExposureMode_t autoExposureMode = RB_GetAutoExposureMode(autoExposure);
	qboolean autoExposureEnabled = (autoExposureMode != AUTO_EXPOSURE_MODE_OFF);

	RB_SanitizeToneMapInputs();

	if (autoExposureMode == AUTO_EXPOSURE_MODE_LEGACY)
	{
		autoExposureState.initialized = qfalse;
		RB_UpdateAutoExposureLegacy(hdrFbo, hdrBox);
	}
	else if (autoExposureMode == AUTO_EXPOSURE_MODE_TIME_CONSTANT)
	{
		RB_UpdateAutoExposureTimeConstant(hdrFbo, hdrBox);
	}
	else if (autoExposureMode == AUTO_EXPOSURE_MODE_HISTOGRAM_PERCENTILE)
	{
		RB_UpdateAutoExposureHistogramPercentile(hdrFbo, hdrBox);
	}
	else
	{
		autoExposureState.initialized = qfalse;
	}

	// tonemap
	color[0] =
	color[1] =
	color[2] = RB_CameraExposureScale(autoExposureEnabled);
	color[3] = 1.0f;

	if (autoExposureEnabled)
		GL_BindToTMU(tr.calcLevelsImage,  TB_LEVELSMAP);
	else
		GL_BindToTMU(tr.fixedLevelsImage, TB_LEVELSMAP);

	FBO_Blit(hdrFbo, hdrBox, NULL, ldrFbo, ldrBox, &tr.tonemapShader, color, 0);
}

void RB_GammaCorrect(FBO_t *srcFbo, ivec4_t srcBox, FBO_t *dstFbo, ivec4_t dstBox, float brightness)
{
	vec4_t color;

	color[0] =
	color[1] =
	color[2] = brightness;
	color[3] = 1.0f / MAX( r_gamma->value, 0.001f );

	FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, &tr.gammaShader, color, 0);
}

/*
=============
RB_BokehBlur


Blurs a part of one framebuffer to another.

Framebuffers can be identical. 
=============
*/
void RB_BokehBlur(FBO_t *src, ivec4_t srcBox, FBO_t *dst, ivec4_t dstBox, float blur)
{
//	ivec4_t srcBox, dstBox;
	vec4_t color;
	
	blur *= 10.0f;

	if (blur < 0.004f)
		return;

	if (glRefConfig.framebufferObject)
	{
		// bokeh blur
		if (blur > 0.0f)
		{
			ivec4_t quarterBox;

			quarterBox[0] = 0;
			quarterBox[1] = tr.quarterFbo[0]->height;
			quarterBox[2] = tr.quarterFbo[0]->width;
			quarterBox[3] = -tr.quarterFbo[0]->height;

			// create a quarter texture
			//FBO_Blit(NULL, NULL, NULL, tr.quarterFbo[0], NULL, NULL, NULL, 0);
			FBO_FastBlit(src, srcBox, tr.quarterFbo[0], quarterBox, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}

#ifndef HQ_BLUR
		if (blur > 1.0f)
		{
			// create a 1/16th texture
			//FBO_Blit(tr.quarterFbo[0], NULL, NULL, tr.textureScratchFbo[0], NULL, NULL, NULL, 0);
			FBO_FastBlit(tr.quarterFbo[0], NULL, tr.textureScratchFbo[0], NULL, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}
#endif

		if (blur > 0.0f && blur <= 1.0f)
		{
			// Crossfade original with quarter texture
			VectorSet4(color, 1, 1, 1, blur);

			FBO_Blit(tr.quarterFbo[0], NULL, NULL, dst, dstBox, NULL, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
		}
#ifndef HQ_BLUR
		// ok blur, but can see some pixelization
		else if (blur > 1.0f && blur <= 2.0f)
		{
			// crossfade quarter texture with 1/16th texture
			FBO_Blit(tr.quarterFbo[0], NULL, NULL, dst, dstBox, NULL, NULL, 0);

			VectorSet4(color, 1, 1, 1, blur - 1.0f);

			FBO_Blit(tr.textureScratchFbo[0], NULL, NULL, dst, dstBox, NULL, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
		}
		else if (blur > 2.0f)
		{
			// blur 1/16th texture then replace
			int i;

			for (i = 0; i < 2; i++)
			{
				vec2_t blurTexScale;
				float subblur;

				subblur = ((blur - 2.0f) / 2.0f) / 3.0f * (float)(i + 1);

				blurTexScale[0] =
				blurTexScale[1] = subblur;

				color[0] =
				color[1] =
				color[2] = 0.5f;
				color[3] = 1.0f;

				if (i != 0)
					FBO_Blit(tr.textureScratchFbo[0], NULL, blurTexScale, tr.textureScratchFbo[1], NULL, &tr.bokehShader, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
				else
					FBO_Blit(tr.textureScratchFbo[0], NULL, blurTexScale, tr.textureScratchFbo[1], NULL, &tr.bokehShader, color, 0);
			}

			FBO_Blit(tr.textureScratchFbo[1], NULL, NULL, dst, dstBox, NULL, NULL, 0);
		}
#else // higher quality blur, but slower
		else if (blur > 1.0f)
		{
			// blur quarter texture then replace
			int i;

			src = tr.quarterFbo[0];
			dst = tr.quarterFbo[1];

			VectorSet4(color, 0.5f, 0.5f, 0.5f, 1);

			for (i = 0; i < 2; i++)
			{
				vec2_t blurTexScale;
				float subblur;

				subblur = (blur - 1.0f) / 2.0f * (float)(i + 1);

				blurTexScale[0] =
				blurTexScale[1] = subblur;

				color[0] =
				color[1] =
				color[2] = 1.0f;
				if (i != 0)
					color[3] = 1.0f;
				else
					color[3] = 0.5f;

				FBO_Blit(tr.quarterFbo[0], NULL, blurTexScale, tr.quarterFbo[1], NULL, &tr.bokehShader, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
			}

			FBO_Blit(tr.quarterFbo[1], NULL, NULL, dst, dstBox, NULL, NULL, 0);
		}
#endif
	}
}


static void RB_RadialBlur(FBO_t *srcFbo, FBO_t *dstFbo, int passes, float stretch, float x, float y, float w, float h, float xcenter, float ycenter, float alpha)
{
	ivec4_t srcBox, dstBox;
	int srcWidth, srcHeight;
	vec4_t color;
	const float inc = 1.f / passes;
	const float mul = powf(stretch, inc);
	float scale;

	alpha *= inc;
	VectorSet4(color, alpha, alpha, alpha, 1.0f);

	srcWidth  = srcFbo ? srcFbo->width  : glConfig.vidWidth;
	srcHeight = srcFbo ? srcFbo->height : glConfig.vidHeight;

	VectorSet4(srcBox, 0, 0, srcWidth, srcHeight);

	VectorSet4(dstBox, x, y, w, h);
	FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, 0);

	--passes;
	scale = mul;
	while (passes > 0)
	{
		float iscale = 1.f / scale;
		float s0 = xcenter * (1.f - iscale);
		float t0 = (1.0f - ycenter) * (1.f - iscale);

		srcBox[0] = s0 * srcWidth;
		srcBox[1] = t0 * srcHeight;
		srcBox[2] = iscale * srcWidth;
		srcBox[3] = iscale * srcHeight;
			
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );

		scale *= mul;
		--passes;
	}
}


static qboolean RB_UpdateSunFlareVis(void)
{
	GLuint sampleCount = 0;
	if (!glRefConfig.occlusionQuery)
		return qtrue;

	tr.sunFlareQueryIndex ^= 1;
	if (!tr.sunFlareQueryActive[tr.sunFlareQueryIndex])
		return qtrue;

	/* debug code */
	if (0)
	{
		int iter;
		for (iter=0 ; ; ++iter)
		{
			GLint available = 0;
			qglGetQueryObjectiv(tr.sunFlareQuery[tr.sunFlareQueryIndex], GL_QUERY_RESULT_AVAILABLE, &available);
			if (available)
				break;
		}

		ri.Printf(PRINT_DEVELOPER, "Waited %d iterations\n", iter);
	}
	
	qglGetQueryObjectuiv(tr.sunFlareQuery[tr.sunFlareQueryIndex], GL_QUERY_RESULT, &sampleCount);
	return sampleCount > 0;
}

void RB_SunRays(FBO_t *srcFbo, ivec4_t srcBox, FBO_t *dstFbo, ivec4_t dstBox)
{
	vec4_t color;
	float dot;
	const float cutoff = 0.25f;
	qboolean colorize = qtrue;

//	float w, h, w2, h2;
	mat4_t mvp;
	vec4_t pos, hpos;

	dot = DotProduct(tr.sunDirection, backEnd.viewParms.or.axis[0]);
	if (dot < cutoff)
		return;

	if (!RB_UpdateSunFlareVis())
		return;

	// From RB_DrawSun()
	{
		float dist;
		mat4_t trans, model;

		Mat4Translation( backEnd.viewParms.or.origin, trans );
		Mat4Multiply( backEnd.viewParms.world.modelMatrix, trans, model );
		Mat4Multiply(backEnd.viewParms.projectionMatrix, model, mvp);

		dist = backEnd.viewParms.zFar / 1.75;		// div sqrt(3)

		VectorScale( tr.sunDirection, dist, pos );
	}

	// project sun point
	//Mat4Multiply(backEnd.viewParms.projectionMatrix, backEnd.viewParms.world.modelMatrix, mvp);
	Mat4Transform(mvp, pos, hpos);

	// transform to UV coords
	hpos[3] = 0.5f / hpos[3];

	pos[0] = 0.5f + hpos[0] * hpos[3];
	pos[1] = 0.5f + hpos[1] * hpos[3];

	// initialize quarter buffers
	{
		float mul = 1.f;
		ivec4_t rayBox, quarterBox;
		int srcWidth  = srcFbo ? srcFbo->width  : glConfig.vidWidth;
		int srcHeight = srcFbo ? srcFbo->height : glConfig.vidHeight;

		VectorSet4(color, mul, mul, mul, 1);

		rayBox[0] = srcBox[0] * tr.sunRaysFbo->width  / srcWidth;
		rayBox[1] = srcBox[1] * tr.sunRaysFbo->height / srcHeight;
		rayBox[2] = srcBox[2] * tr.sunRaysFbo->width  / srcWidth;
		rayBox[3] = srcBox[3] * tr.sunRaysFbo->height / srcHeight;

		quarterBox[0] = 0;
		quarterBox[1] = tr.quarterFbo[0]->height;
		quarterBox[2] = tr.quarterFbo[0]->width;
		quarterBox[3] = -tr.quarterFbo[0]->height;

		// first, downsample the framebuffer
		if (colorize)
		{
			FBO_FastBlit(srcFbo, srcBox, tr.quarterFbo[0], quarterBox, GL_COLOR_BUFFER_BIT, GL_LINEAR);
			FBO_Blit(tr.sunRaysFbo, rayBox, NULL, tr.quarterFbo[0], quarterBox, NULL, color, GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO);
		}
		else
		{
			FBO_FastBlit(tr.sunRaysFbo, rayBox, tr.quarterFbo[0], quarterBox, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}
	}

	// radial blur passes, ping-ponging between the two quarter-size buffers
	{
		const float stretch_add = 2.f/3.f;
		float stretch = 1.f + stretch_add;
		int i;
		for (i=0; i<2; ++i)
		{
			RB_RadialBlur(tr.quarterFbo[i&1], tr.quarterFbo[(~i) & 1], 5, stretch, 0.f, 0.f, tr.quarterFbo[0]->width, tr.quarterFbo[0]->height, pos[0], pos[1], 1.125f);
			stretch += stretch_add;
		}
	}
	
	// add result back on top of the main buffer
	{
		float mul = 1.f;

		VectorSet4(color, mul, mul, mul, 1);

		FBO_Blit(tr.quarterFbo[0], NULL, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
	}
}

static void RB_BlurAxis(FBO_t *srcFbo, FBO_t *dstFbo, float strength, qboolean horizontal)
{
	float dx, dy;
	float xmul, ymul;
	float weights[3] = {
		0.227027027f,
		0.316216216f,
		0.070270270f,
	};
	float offsets[3] = {
		0.f,
		1.3846153846f,
		3.2307692308f,
	};

	xmul = horizontal;
	ymul = 1.f - xmul;

	xmul *= strength;
	ymul *= strength;

	{
		ivec4_t srcBox, dstBox;
		vec4_t color;

		VectorSet4(color, weights[0], weights[0], weights[0], 1.0f);
		VectorSet4(srcBox, 0, 0, srcFbo->width, srcFbo->height);
		VectorSet4(dstBox, 0, 0, dstFbo->width, dstFbo->height);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, 0);

		VectorSet4(color, weights[1], weights[1], weights[1], 1.0f);
		dx = offsets[1] * xmul;
		dy = offsets[1] * ymul;
		VectorSet4(srcBox, dx, dy, srcFbo->width, srcFbo->height);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
		VectorSet4(srcBox, -dx, -dy, srcFbo->width, srcFbo->height);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);

		VectorSet4(color, weights[2], weights[2], weights[2], 1.0f);
		dx = offsets[2] * xmul;
		dy = offsets[2] * ymul;
		VectorSet4(srcBox, dx, dy, srcFbo->width, srcFbo->height);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
		VectorSet4(srcBox, -dx, -dy, srcFbo->width, srcFbo->height);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
	}
}

static void RB_HBlur(FBO_t *srcFbo, FBO_t *dstFbo, float strength)
{
	RB_BlurAxis(srcFbo, dstFbo, strength, qtrue);
}

static void RB_VBlur(FBO_t *srcFbo, FBO_t *dstFbo, float strength)
{
	RB_BlurAxis(srcFbo, dstFbo, strength, qfalse);
}

void RB_GaussianBlur(float blur)
{
	//float mul = 1.f;
	float factor = Com_Clamp(0.f, 1.f, blur);

	if (factor <= 0.f)
		return;

	{
		ivec4_t srcBox, dstBox;
		vec4_t color;

		VectorSet4(color, 1, 1, 1, 1);

		// first, downsample the framebuffer
		FBO_FastBlit(NULL, NULL, tr.quarterFbo[0], NULL, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		FBO_FastBlit(tr.quarterFbo[0], NULL, tr.textureScratchFbo[0], NULL, GL_COLOR_BUFFER_BIT, GL_LINEAR);

		// set the alpha channel
		qglColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
		FBO_BlitFromTexture(tr.whiteImage, NULL, NULL, tr.textureScratchFbo[0], NULL, NULL, color, GLS_DEPTHTEST_DISABLE);
		qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

		// blur the tiny buffer horizontally and vertically
		RB_HBlur(tr.textureScratchFbo[0], tr.textureScratchFbo[1], factor);
		RB_VBlur(tr.textureScratchFbo[1], tr.textureScratchFbo[0], factor);

		// finally, merge back to framebuffer
		VectorSet4(srcBox, 0, 0, tr.textureScratchFbo[0]->width, tr.textureScratchFbo[0]->height);
		VectorSet4(dstBox, 0, 0, glConfig.vidWidth,              glConfig.vidHeight);
		color[3] = factor;
		FBO_Blit(tr.textureScratchFbo[0], srcBox, NULL, NULL, dstBox, NULL, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
	}
}
