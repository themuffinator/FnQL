/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL.

FnQL is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.
===========================================================================
*/

#ifndef FNQL_QCOMMON_CM_TRACE_CONTRACT_H
#define FNQL_QCOMMON_CM_TRACE_CONTRACT_H

#include "q_shared.h"

/*
 * Retail Quake Live expands the target radius by the moving capsule radius,
 * uses the target half-height for the body cylinder, and tests a smaller
 * sphere at the top of the target.  Keep this geometry in one independently
 * implemented helper so the engine path and parity tests share the contract.
 */
#define CM_RETAIL_CAPSULE_HEAD_RADIUS_SCALE 0.7f

typedef struct {
	vec3_t bodyOrigin;
	float bodyRadius;
	float bodyHalfheight;
	vec3_t headOrigin;
	float headRadius;
} cm_retailCapsuleProfile_t;

static ID_INLINE void CM_BuildRetailCapsuleProfile(
	const vec3_t mins,
	const vec3_t maxs,
	float movingRadius,
	cm_retailCapsuleProfile_t *profile )
{
	float halfwidth;
	float targetRadius;
	int i;

	for ( i = 0; i < 3; ++i ) {
		profile->bodyOrigin[i] = ( mins[i] + maxs[i] ) * 0.5f;
	}

	halfwidth = maxs[0] - profile->bodyOrigin[0];
	profile->bodyHalfheight = maxs[2] - profile->bodyOrigin[2];
	targetRadius = halfwidth < profile->bodyHalfheight
		? halfwidth
		: profile->bodyHalfheight;

	profile->bodyRadius = targetRadius + movingRadius;
	VectorCopy( profile->bodyOrigin, profile->headOrigin );
	profile->headOrigin[2] += profile->bodyHalfheight;
	profile->headRadius =
		profile->bodyRadius * CM_RETAIL_CAPSULE_HEAD_RADIUS_SCALE;
}

static ID_INLINE int CM_RetailCapsuleHitContents(
	int contents,
	float bodyFraction,
	float headFraction )
{
	return headFraction < bodyFraction
		? contents | CONTENTS_HEAD
		: contents;
}

/*
 * Retail's analytic capsule helpers do not promise a plane for a zero-
 * fraction contact.  A trace may start inside the shape (no plane), or inside
 * the one-unit collision epsilon (a deliberately sub-unit radial plane).
 * Fractional impacts are the states for which callers can rely on a unit
 * plane.  Also reject non-finite/out-of-range fractions and normals.
 */
static ID_INLINE qboolean CM_TraceResultHasValidPlaneContract(
	const trace_t *trace )
{
	double normalLengthSquared;

	if ( !trace ||
		!( trace->fraction >= 0.0f && trace->fraction <= 1.0f ) ) {
		return qfalse;
	}

	if ( trace->fraction == 0.0f || trace->fraction == 1.0f ) {
		return qtrue;
	}

	normalLengthSquared =
		(double)trace->plane.normal[0] * trace->plane.normal[0] +
		(double)trace->plane.normal[1] * trace->plane.normal[1] +
		(double)trace->plane.normal[2] * trace->plane.normal[2];

	return normalLengthSquared > 0.9999 &&
		normalLengthSquared < 1.0001
		? qtrue
		: qfalse;
}

#endif
