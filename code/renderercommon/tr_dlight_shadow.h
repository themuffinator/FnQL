/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL.

FnQL is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.

FnQL is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
details.
===========================================================================
*/

#ifndef TR_DLIGHT_SHADOW_H
#define TR_DLIGHT_SHADOW_H

#include <float.h>

static ID_INLINE qboolean R_DlightShadowFloatIsFinite( float value ) {
	return ( value == value && value >= -FLT_MAX && value <= FLT_MAX ) ?
		qtrue : qfalse;
}

// Map-authored and surface-proxy lights do not need temporal quantization.
static ID_INLINE float R_DlightShadowExactProjectionFarForRadius( float radius ) {
	if ( !R_DlightShadowFloatIsFinite( radius ) || radius <= 0.0f ) {
		return 0.0f;
	}
	if ( radius < 64.0f ) {
		return 64.0f;
	}
	if ( radius > (float)WORLD_SIZE ) {
		return (float)WORLD_SIZE;
	}
	return radius;
}

/*
 * Retail QL deliberately varies several sustained dlight radii by rand() & 31.
 * Keep that variation in the visible light while giving its shadow projection
 * a conservative, stable range. Quantizing before r_dlightScale is applied
 * keeps each retail radius band stable for every supported scale value.
 */
static ID_INLINE float R_DlightShadowProjectionFarForRadius(
	float submittedRadius, float appliedScale ) {
	float projectionFar;

	if ( !R_DlightShadowFloatIsFinite( submittedRadius ) ||
		!R_DlightShadowFloatIsFinite( appliedScale ) ||
		submittedRadius <= 0.0f || submittedRadius > (float)WORLD_SIZE ||
		appliedScale <= 0.0f ) {
		return 0.0f;
	}

	projectionFar = 64.0f;
	while ( projectionFar < submittedRadius && projectionFar < (float)WORLD_SIZE ) {
		projectionFar *= 2.0f;
	}
	if ( appliedScale >= (float)WORLD_SIZE / projectionFar ) {
		projectionFar = (float)WORLD_SIZE;
	} else {
		projectionFar *= appliedScale;
	}

	if ( projectionFar < 64.0f ) {
		projectionFar = 64.0f;
	} else if ( projectionFar > (float)WORLD_SIZE ) {
		projectionFar = (float)WORLD_SIZE;
	}

	return projectionFar;
}

#endif // TR_DLIGHT_SHADOW_H
