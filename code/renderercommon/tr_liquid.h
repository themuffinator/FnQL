/*
===========================================================================
Copyright (C) 2026 FnQL contributors

Shared, renderer-only liquid helpers. Keep this header independent of a
specific backend so OpenGL/GLx and Vulkan classify and age liquid effects in
exactly the same way.
===========================================================================
*/
#ifndef TR_LIQUID_H
#define TR_LIQUID_H

#include "../qcommon/q_shared.h"

#define LIQUID_CONTENTS_MASK ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA )
#define LIQUID_MAX_ACTIVE_IMPULSES 8
#define LIQUID_MAX_STORED_IMPULSES 16
#define LIQUID_IMPULSE_LIFETIME_MSEC 2400

/* Displacement is authored in pixels at a 1080-line reference view and scaled
 * by the real viewport height so the effect keeps the same angular size at
 * every resolution. r_liquidWarpScale is a plain 0..2 multiplier of the base. */
#define LIQUID_REFERENCE_VIEW_HEIGHT 1080.0f
#define LIQUID_WARP_BASE_PIXELS 12.0f
#define LIQUID_WARP_SCALE_MAX 2.0f
#define LIQUID_RIPPLE_PIXEL_SCALE 10.0f

/* Screen-space displacement fades with eye distance (clip-space W is the
 * view-space depth in idTech3 units) so distant liquid does not shimmer, and
 * with the grazing angle so the compressed wave field near the horizon does
 * not alias. Physically this also matches falling Fresnel transmission. */
#define LIQUID_WARP_DISTANCE_REF 320.0f
#define LIQUID_WARP_DISTANCE_MIN_ATTEN 0.30f
#define LIQUID_GRAZING_FADE_SCALE 4.0f

/* Refraction samples that land on opaque geometry nearer than the liquid
 * surface are foreground, not background: the shaders fall back to the
 * unwarped coordinate there so the waterline stays crisp. The epsilon is in
 * nonlinear window depth; both depth conventions use the same magnitude. */
#define LIQUID_DEPTH_REJECT_EPSILON 0.00003f

/* Bounded single-tap screen-space reflection for the post-authored sheen
 * pass. It samples only the immutable pre-transparency snapshot, so no
 * feedback loop with the live color buffer is possible. */
#define LIQUID_REFLECT_PROXY_BASE 384.0f
#define LIQUID_REFLECT_PROXY_VIEW 0.75f
#define LIQUID_REFLECT_INTENSITY 0.85f
#define LIQUID_FRESNEL_ALPHA_BASE 0.04f
#define LIQUID_FRESNEL_ALPHA_SPAN 0.56f
#define LIQUID_NORMAL_PERTURB 0.12f

/*
Ambient wave gradient model, evaluated per fragment on programmable paths and
per vertex on the fixed-function fallback. The GLx GLSL, Vulkan GLSL, and ARB
assembly implementations must keep these constants byte-for-byte identical;
tests/liquid_rendering_source_tests.py enforces the parity.

  phase[i]    = dot( WAVE_K[i].xyz, position ) + WAVE_K[i].w * time
  gradient   += WAVE_DIR_AMP[i].xy * ( WAVE_DIR_AMP[i].z * cos( phase[i] ) )
  |gradient| <= 1

Every octave speed is a multiple of 0.05 rad/s, so all octaves share the
common period 2*pi/0.05. Backends wrap scene time to that period before
uploading it, which keeps trigonometric arguments small for low-precision
fragment paths without introducing a visible phase jump.
*/
#define LIQUID_WAVE_OCTAVES 3
#define LIQUID_WAVE_TIME_PERIOD 125.66370614  /* 40*pi seconds */

static ID_INLINE float R_LiquidWaveTime( double sceneTime )
{
	return (float)fmod( sceneTime, LIQUID_WAVE_TIME_PERIOD );
}

static ID_INLINE void R_LiquidWaveOctave( int octave, vec4_t phaseCoeffs, vec3_t dirAmp )
{
	/* xyz: world-space wave vector (rad/unit), w: angular speed (rad/s) */
	static const vec4_t waveK[LIQUID_WAVE_OCTAVES] = {
		{  0.0096f,  0.0109f, 0.0051f, 0.85f },
		{ -0.0269f,  0.0196f, 0.0116f, 1.40f },
		{  0.0320f, -0.0693f, 0.0266f, 2.30f },
	};
	/* xy: screen-plane displacement direction, z: octave amplitude */
	static const vec3_t waveDirAmp[LIQUID_WAVE_OCTAVES] = {
		{  0.66f,  0.75f, 0.42f },
		{ -0.81f,  0.59f, 0.33f },
		{  0.42f, -0.91f, 0.25f },
	};

	if ( octave < 0 || octave >= LIQUID_WAVE_OCTAVES ) {
		octave = 0;
	}
	Vector4Copy( waveK[octave], phaseCoeffs );
	VectorCopy( waveDirAmp[octave], dirAmp );
}

static ID_INLINE void R_LiquidWaveGradient( const vec3_t position, float time, float *gradientX, float *gradientY )
{
	vec4_t k;
	vec3_t dirAmp;
	int i;

	*gradientX = 0.0f;
	*gradientY = 0.0f;
	for ( i = 0; i < LIQUID_WAVE_OCTAVES; i++ ) {
		float wave;

		R_LiquidWaveOctave( i, k, dirAmp );
		wave = dirAmp[2] * cosf( k[0] * position[0] + k[1] * position[1] +
			k[2] * position[2] + k[3] * time );
		*gradientX += dirAmp[0] * wave;
		*gradientY += dirAmp[1] * wave;
	}
}

static ID_INLINE float R_LiquidViewHeightScale( int viewportHeight )
{
	return viewportHeight > 0 ? (float)viewportHeight / LIQUID_REFERENCE_VIEW_HEIGHT : 1.0f;
}

static ID_INLINE float R_LiquidWarpPixels( float warpScale, int viewportHeight )
{
	return Com_Clamp( 0.0f, LIQUID_WARP_SCALE_MAX, warpScale ) *
		LIQUID_WARP_BASE_PIXELS * R_LiquidViewHeightScale( viewportHeight );
}

static ID_INLINE float R_LiquidDistanceAttenuation( float clipW )
{
	if ( clipW <= 1.0f ) {
		return 1.0f;
	}
	return Com_Clamp( LIQUID_WARP_DISTANCE_MIN_ATTEN, 1.0f, LIQUID_WARP_DISTANCE_REF / clipW );
}

/* Projective liquid coordinates cover the whole snapshot.  Use the enhanced
 * path only when the 3D viewport covers that target; reduced/sub-rect views
 * retain the authored material instead of sampling a mismatched screen area. */
static ID_INLINE qboolean R_LiquidViewportCoversTarget( int viewportX,
	int viewportY, int viewportWidth, int viewportHeight, int targetWidth,
	int targetHeight )
{
	return ( viewportX == 0 && viewportY == 0 && targetWidth > 0 &&
		targetHeight > 0 && viewportWidth == targetWidth &&
		viewportHeight == targetHeight ) ? qtrue : qfalse;
}

static ID_INLINE qboolean R_LiquidContentsEnabled( int contents, int reflectionMode )
{
	if ( reflectionMode <= 0 ) {
		return qfalse;
	}
	if ( contents & CONTENTS_WATER ) {
		return qtrue;
	}
	return ( reflectionMode >= 2 && ( contents & ( CONTENTS_SLIME | CONTENTS_LAVA ) ) ) ? qtrue : qfalse;
}

static ID_INLINE float R_LiquidContentsReflectionScale( int contents )
{
	if ( contents & CONTENTS_WATER ) {
		return 1.0f;
	}
	if ( contents & CONTENTS_SLIME ) {
		return 0.55f;
	}
	if ( contents & CONTENTS_LAVA ) {
		return 0.25f;
	}
	return 0.0f;
}

static ID_INLINE void R_LiquidContentsFresnelColor( int contents, vec3_t color )
{
	if ( contents & CONTENTS_WATER ) {
		color[0] = 0.42f;
		color[1] = 0.58f;
		color[2] = 0.70f;
	} else if ( contents & CONTENTS_SLIME ) {
		color[0] = 0.30f;
		color[1] = 0.55f;
		color[2] = 0.18f;
	} else {
		color[0] = 0.95f;
		color[1] = 0.38f;
		color[2] = 0.08f;
	}
}

static ID_INLINE qboolean R_LiquidInteractionActive( const liquidInteraction_t *interaction, int sceneTime )
{
	int64_t age;

	if ( !interaction || interaction->radius <= 0.0f || interaction->strength <= 0.0f ) {
		return qfalse;
	}
	age = (int64_t)sceneTime - (int64_t)interaction->time;
	return ( age >= 0 && age < LIQUID_IMPULSE_LIFETIME_MSEC ) ? qtrue : qfalse;
}

static ID_INLINE void R_LiquidWorldToLocal( const vec3_t world, const vec3_t origin,
	const vec3_t axis[3], vec3_t local )
{
	vec3_t delta;

	VectorSubtract( world, origin, delta );
	local[0] = DotProduct( delta, axis[0] );
	local[1] = DotProduct( delta, axis[1] );
	local[2] = DotProduct( delta, axis[2] );
}

#endif /* TR_LIQUID_H */
