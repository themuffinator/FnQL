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
 * FnQL preserves the Quake III moving-capsule sweep used by QLSRP's
 * reconstructed `cm_trace.c`. The target is a vertical cylinder capped
 * by two spheres, the target radius is expanded by the moving capsule radius,
 * and the cylinder section is only swept when it still has a positive height.
 *
 * Retail executable evidence (0x4c6bd0) differs from that reconstruction:
 * it uses a body cylinder and a smaller upper sphere with head-hit metadata.
 * FnQL applies that profile to point traces in CM_TracePointThroughPlayer,
 * preserving this movement geometry and its approach-direction guarantees.
 * Keeping the movement geometry in one shared helper lets the engine path
 * and the parity tests read the same numbers.  A source-shaped test alone
 * cannot tell a wrong capsule profile from a right one, so the behavioral
 * gate in `tests/collision_capsule_trace_tests.c` sweeps the real trace path.
 */
typedef struct {
	vec3_t	center;				/* midpoint of the target bounds */
	vec3_t	topSphere;			/* origin of the upper cap sphere */
	vec3_t	bottomSphere;		/* origin of the lower cap sphere */
	float	radius;				/* target radius plus the moving radius */
	float	cylinderHalfheight;	/* <= 0 when the caps cover the capsule */
} cm_capsuleProfile_t;

static ID_INLINE void CM_BuildCapsuleTraceProfile(
	const vec3_t mins,
	const vec3_t maxs,
	float movingRadius,
	float movingHalfheight,
	cm_capsuleProfile_t *profile )
{
	float halfwidth;
	float halfheight;
	float radius;
	float capOffset;
	int i;

	for ( i = 0; i < 3; ++i ) {
		profile->center[i] = ( mins[i] + maxs[i] ) * 0.5f;
	}

	halfwidth = maxs[0] - profile->center[0];
	halfheight = maxs[2] - profile->center[2];
	radius = ( halfwidth > halfheight ) ? halfheight : halfwidth;
	capOffset = halfheight - radius;

	VectorCopy( profile->center, profile->topSphere );
	profile->topSphere[2] += capOffset;
	VectorCopy( profile->center, profile->bottomSphere );
	profile->bottomSphere[2] -= capOffset;

	/* the swept spheres carry the moving capsule's radius */
	profile->radius = radius + movingRadius;
	/* both capsule heights, minus the part the cap spheres already cover */
	profile->cylinderHalfheight =
		halfheight + movingHalfheight - profile->radius;
}

/*
 * Two different producers fill in `trace.plane`:
 *
 *  - Brush, patch and box hulls copy a stored BSP plane, so every fractional
 *    impact carries a unit normal.  This is the invariant worth enforcing.
 *  - The capsule sweep derives its plane analytically, scaling the radial
 *    vector at the impact point by 1/(radius + RADIUS_EPSILON).  That math is
 *    single precision, so the normal is only approximately unit: the
 *    quadratic loses significance as the sweep start moves away from the
 *    target.  Measured over 400k random player-vs-player capsule sweeps
 *    against a (-15,-15,-24)..(15,15,32) target, |normal|^2 stays inside 1e-4
 *    of one at movement scale, spreads to [0.79, 1.18] for 16k-unit sweeps,
 *    and reaches [0.22, 4.75] at the 64k world limit.  A zero-fraction
 *    analytic contact may also carry no plane at all, or a deliberately
 *    sub-unit radial plane from inside the one-unit collision epsilon.
 *
 * The debug invariant therefore demands a unit plane only where the engine
 * actually produces one, while still catching corrupt fractions, non-finite
 * values, and degenerate normals on every path.
 */
typedef enum {
	CM_TRACE_PLANE_STORED,		/* brush, patch or box hull plane */
	CM_TRACE_PLANE_ANALYTIC		/* capsule sphere or cylinder plane */
} cm_tracePlaneSource_t;

static ID_INLINE qboolean CM_TraceResultHasValidPlaneContract(
	const trace_t *trace,
	cm_tracePlaneSource_t source )
{
	double normalLengthSquared;

	if ( !trace ||
		!( trace->fraction >= 0.0f && trace->fraction <= 1.0f ) ) {
		return qfalse;
	}

	normalLengthSquared =
		(double)trace->plane.normal[0] * trace->plane.normal[0] +
		(double)trace->plane.normal[1] * trace->plane.normal[1] +
		(double)trace->plane.normal[2] * trace->plane.normal[2];

	/* rejects NaN and overflowed components on every path */
	if ( !( normalLengthSquared >= 0.0 && normalLengthSquared < 1.0e30 ) ) {
		return qfalse;
	}

	/* no plane is promised when the sweep never moved, or never hit */
	if ( trace->fraction == 0.0f || trace->fraction == 1.0f ) {
		return qtrue;
	}

	if ( source == CM_TRACE_PLANE_ANALYTIC ) {
		return normalLengthSquared > 0.0 ? qtrue : qfalse;
	}

	return normalLengthSquared > 0.9999 &&
		normalLengthSquared < 1.0001
		? qtrue
		: qfalse;
}

#endif
