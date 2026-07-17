/*
===========================================================================
Copyright (C) 2026

This file is part of FnQL.

FnQL is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
===========================================================================
*/

#ifndef TR_MOTION_BLUR_H
#define TR_MOTION_BLUR_H

#include <float.h>
#include <math.h>

#define MOTION_BLUR_MAX_DELTA_MSEC 100
#define MOTION_BLUR_CUT_DISTANCE 256.0f
#define MOTION_BLUR_FOCUS_DISTANCE 512.0f
#define MOTION_BLUR_MAX_RADIUS_PIXELS 32.0f
#define MOTION_BLUR_MIN_RADIUS_PIXELS 0.35f

typedef struct motionBlurViewState_s {
	int lastTimeMsec;
	float origin[3];
	float axis[3][3];
	qboolean valid;
} motionBlurViewState_t;

static void R_MotionBlur_ResetView( motionBlurViewState_t *state )
{
	state->lastTimeMsec = 0;
	state->valid = qfalse;
}

/* Intersect the 3D view with its render target and return texel-centre bounds. */
static qboolean R_MotionBlur_CalculateViewRect( int targetWidth, int targetHeight,
	int viewX, int viewY, int viewWidth, int viewHeight, int viewRect[4],
	float sampleBounds[4] )
{
	long long right;
	long long top;
	int x0;
	int y0;
	int x1;
	int y1;

	if ( targetWidth <= 0 || targetHeight <= 0 || viewWidth <= 0 || viewHeight <= 0 ) {
		return qfalse;
	}
	right = (long long)viewX + (long long)viewWidth;
	top = (long long)viewY + (long long)viewHeight;
	x0 = viewX > 0 ? viewX : 0;
	y0 = viewY > 0 ? viewY : 0;
	x1 = right < targetWidth ? (int)right : targetWidth;
	y1 = top < targetHeight ? (int)top : targetHeight;
	if ( x0 >= targetWidth || y0 >= targetHeight || x1 <= x0 || y1 <= y0 ) {
		return qfalse;
	}

	viewRect[0] = x0;
	viewRect[1] = y0;
	viewRect[2] = x1 - x0;
	viewRect[3] = y1 - y0;
	sampleBounds[0] = ( (float)x0 + 0.5f ) / (float)targetWidth;
	sampleBounds[1] = ( (float)y0 + 0.5f ) / (float)targetHeight;
	sampleBounds[2] = ( (float)x1 - 0.5f ) / (float)targetWidth;
	sampleBounds[3] = ( (float)y1 - 0.5f ) / (float)targetHeight;
	return qtrue;
}

static float R_MotionBlur_Dot3( const float a[3], const float b[3] )
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static qboolean R_MotionBlur_FloatIsFinite( float value )
{
	return ( value == value && value >= -FLT_MAX && value <= FLT_MAX ) ? qtrue : qfalse;
}

static qboolean R_MotionBlur_ViewInputsAreFinite( float strength,
	const float origin[3], const float axis[3][3], float fovX, float fovY )
{
	int i;
	int j;

	if ( !R_MotionBlur_FloatIsFinite( strength ) ||
		!R_MotionBlur_FloatIsFinite( fovX ) ||
		!R_MotionBlur_FloatIsFinite( fovY ) ) {
		return qfalse;
	}
	for ( i = 0; i < 3; i++ ) {
		if ( !R_MotionBlur_FloatIsFinite( origin[i] ) ) {
			return qfalse;
		}
		for ( j = 0; j < 3; j++ ) {
			if ( !R_MotionBlur_FloatIsFinite( axis[i][j] ) ) {
				return qfalse;
			}
		}
	}

	return qtrue;
}

static void R_MotionBlur_StoreView( motionBlurViewState_t *state, int nowMsec,
	const float origin[3], const float axis[3][3] )
{
	int i;
	int j;

	state->lastTimeMsec = nowMsec;
	for ( i = 0; i < 3; i++ ) {
		state->origin[i] = origin[i];
		for ( j = 0; j < 3; j++ ) {
			state->axis[i][j] = axis[i][j];
		}
	}
	state->valid = qtrue;
}

/*
 * Calculate a current-frame, camera-driven blur radius in normalized texture
 * coordinates.  This deliberately stores only camera state, never pixels:
 * stationary frames remain untouched and stale scene color cannot leak across
 * view changes.  Rotation supplies most of the signal; lateral translation is
 * projected at a conservative reference distance to keep movement blur subtle.
 */
static qboolean R_MotionBlur_Calculate( motionBlurViewState_t *state,
	qboolean enabled, float strength, int nowMsec, const float origin[3],
	const float axis[3][3], float fovX, float fovY, int width, int height,
	float radiusUv[2] )
{
	float previousForward[3];
	float deltaOrigin[3];
	float forwardDot;
	float horizontalAngle;
	float verticalAngle;
	float distanceSquared;
	float radiusPixels;
	float scale;
	int64_t elapsedMsec;
	int i;

	radiusUv[0] = 0.0f;
	radiusUv[1] = 0.0f;
	if ( !enabled ||
		!R_MotionBlur_ViewInputsAreFinite( strength, origin, axis, fovX, fovY ) ||
		strength <= 0.0f || width <= 0 || height <= 0 ||
		fovX <= 1.0f || fovY <= 1.0f ) {
		R_MotionBlur_ResetView( state );
		return qfalse;
	}

	if ( strength > 1.0f ) {
		strength = 1.0f;
	}
	if ( !state->valid ) {
		R_MotionBlur_StoreView( state, nowMsec, origin, axis );
		return qfalse;
	}

	elapsedMsec = (int64_t)nowMsec - (int64_t)state->lastTimeMsec;
	for ( i = 0; i < 3; i++ ) {
		previousForward[i] = state->axis[0][i];
		deltaOrigin[i] = origin[i] - state->origin[i];
	}
	forwardDot = R_MotionBlur_Dot3( previousForward, axis[0] );
	distanceSquared = R_MotionBlur_Dot3( deltaOrigin, deltaOrigin );
	if ( !R_MotionBlur_FloatIsFinite( forwardDot ) ||
		!R_MotionBlur_FloatIsFinite( distanceSquared ) ) {
		R_MotionBlur_ResetView( state );
		return qfalse;
	}

	R_MotionBlur_StoreView( state, nowMsec, origin, axis );
	if ( elapsedMsec <= 0 || elapsedMsec > MOTION_BLUR_MAX_DELTA_MSEC ||
		forwardDot < 0.70710678f ||
		distanceSquared > MOTION_BLUR_CUT_DISTANCE * MOTION_BLUR_CUT_DISTANCE ) {
		return qfalse;
	}

	/* axis[1] is view-left in Quake, axis[2] is view-up. */
	horizontalAngle = atan2f( R_MotionBlur_Dot3( previousForward, axis[1] ), forwardDot );
	verticalAngle = atan2f( R_MotionBlur_Dot3( previousForward, axis[2] ), forwardDot );
	horizontalAngle += atan2f( R_MotionBlur_Dot3( deltaOrigin, axis[1] ),
		MOTION_BLUR_FOCUS_DISTANCE );
	verticalAngle += atan2f( R_MotionBlur_Dot3( deltaOrigin, axis[2] ),
		MOTION_BLUR_FOCUS_DISTANCE );

	radiusUv[0] = horizontalAngle / ( fovX * 0.01745329251994329577f ) * strength;
	radiusUv[1] = verticalAngle / ( fovY * 0.01745329251994329577f ) * strength;
	radiusPixels = sqrtf( radiusUv[0] * radiusUv[0] * width * width +
		radiusUv[1] * radiusUv[1] * height * height );
	if ( !R_MotionBlur_FloatIsFinite( radiusUv[0] ) ||
		!R_MotionBlur_FloatIsFinite( radiusUv[1] ) ||
		!R_MotionBlur_FloatIsFinite( radiusPixels ) ) {
		radiusUv[0] = radiusUv[1] = 0.0f;
		R_MotionBlur_ResetView( state );
		return qfalse;
	}
	if ( radiusPixels < MOTION_BLUR_MIN_RADIUS_PIXELS ) {
		radiusUv[0] = radiusUv[1] = 0.0f;
		return qfalse;
	}
	if ( radiusPixels > MOTION_BLUR_MAX_RADIUS_PIXELS ) {
		scale = MOTION_BLUR_MAX_RADIUS_PIXELS / radiusPixels;
		radiusUv[0] *= scale;
		radiusUv[1] *= scale;
	}

	return qtrue;
}

#endif // TR_MOTION_BLUR_H
