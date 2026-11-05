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

/*
 * Behavioral gate for the capsule-vs-capsule sweep.  This links the real
 * cm_trace.c against a stub collision model that only owns the temporary
 * capsule bounds, so the QL geometry and the debug trace-plane invariant are
 * exercised rather than merely described.  A source-shaped test cannot catch
 * a capsule profile that is self-consistent but geometrically wrong.
 */

#include "cm_local.h"
#include "cm_trace_contract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ harness */

static int failures;

static void Check( int condition, const char *expression, int line )
{
	if ( condition ) {
		return;
	}
	fprintf( stderr, "line %d: check failed: %s\n", line, expression );
	++failures;
}

#define CHECK( expression ) Check( ( expression ) ? 1 : 0, #expression, __LINE__ )

/* --------------------------------------------- collision model stubs */

clipMap_t	cm;
int			c_pointcontents;
int			c_traces, c_brush_traces, c_patch_traces, c_totalPatchTraces;
cvar_t		*cm_noAreas, *cm_noCurves, *cm_playerCurveClip;

static cvar_t	stubCvars[8];
static int		stubCvarCount;
static cmodel_t	stubModel;

cvar_t *Cvar_Get( const char *var_name, const char *value, int flags )
{
	int i;

	for ( i = 0; i < stubCvarCount; ++i ) {
		if ( !strcmp( stubCvars[i].name, var_name ) ) {
			return &stubCvars[i];
		}
	}
	if ( stubCvarCount == (int)( sizeof( stubCvars ) / sizeof( stubCvars[0] ) ) ) {
		fprintf( stderr, "stub cvar table overflow: %s\n", var_name );
		exit( EXIT_FAILURE );
	}
	stubCvars[stubCvarCount].name = (char *)var_name;
	stubCvars[stubCvarCount].string = (char *)value;
	stubCvars[stubCvarCount].value = (float)atof( value );
	stubCvars[stubCvarCount].integer = atoi( value );
	(void)flags;
	return &stubCvars[stubCvarCount++];
}

void QDECL Com_Error( errorParm_t level, const char *fmt, ... )
{
	(void)level;
	fprintf( stderr, "unexpected Com_Error: %s\n", fmt );
	exit( EXIT_FAILURE );
}

void QDECL Com_Printf( const char *fmt, ... )
{
	(void)fmt;
}

cmodel_t *CM_ClipHandleToModel( clipHandle_t handle )
{
	(void)handle;
	return &stubModel;
}

void CM_ModelBounds( clipHandle_t model, vec3_t mins, vec3_t maxs )
{
	const cmodel_t *cmod = CM_ClipHandleToModel( model );

	VectorCopy( cmod->mins, mins );
	VectorCopy( cmod->maxs, maxs );
}

clipHandle_t CM_TempBoxModel( const vec3_t mins, const vec3_t maxs, int capsule )
{
	VectorCopy( mins, stubModel.mins );
	VectorCopy( maxs, stubModel.maxs );
	return capsule ? CAPSULE_MODEL_HANDLE : BOX_MODEL_HANDLE;
}

void CM_BoxLeafnums_r( leafList_t *ll, int nodenum )
{
	(void)ll;
	(void)nodenum;
}

void CM_StoreLeafs( leafList_t *ll, int nodenum )
{
	(void)ll;
	(void)nodenum;
}

qboolean CM_BoundsIntersect( const vec3_t mins, const vec3_t maxs,
	const vec3_t mins2, const vec3_t maxs2 )
{
	if ( maxs[0] < mins2[0] - SURFACE_CLIP_EPSILON
		|| maxs[1] < mins2[1] - SURFACE_CLIP_EPSILON
		|| maxs[2] < mins2[2] - SURFACE_CLIP_EPSILON
		|| mins[0] > maxs2[0] + SURFACE_CLIP_EPSILON
		|| mins[1] > maxs2[1] + SURFACE_CLIP_EPSILON
		|| mins[2] > maxs2[2] + SURFACE_CLIP_EPSILON ) {
		return qfalse;
	}
	return qtrue;
}

void CM_TraceThroughPatchCollide( traceWork_t *tw, const struct patchCollide_s *pc )
{
	(void)tw;
	(void)pc;
}

qboolean CM_PositionTestInPatchCollide( traceWork_t *tw, const struct patchCollide_s *pc )
{
	(void)tw;
	(void)pc;
	return qfalse;
}

/* ----------------------------------------------------------- fixtures */

/* retail QL player hull */
static const vec3_t playerMins = { -15.0f, -15.0f, -24.0f };
static const vec3_t playerMaxs = { 15.0f, 15.0f, 32.0f };

#define PLAYER_CLIP_MASK ( CONTENTS_SOLID | CONTENTS_PLAYERCLIP | CONTENTS_BODY )

static void CapsuleSweep( trace_t *trace, const vec3_t start, const vec3_t end )
{
	CM_BoxTrace( trace, start, end, playerMins, playerMaxs,
		CAPSULE_MODEL_HANDLE, PLAYER_CLIP_MASK, qtrue );
}

static float SweepFraction( float sx, float sy, float sz, float ex, float ey, float ez )
{
	trace_t trace;
	vec3_t start, end;

	VectorSet( start, sx, sy, sz );
	VectorSet( end, ex, ey, ez );
	CapsuleSweep( &trace, start, end );
	return trace.fraction;
}

static qboolean NearlyEqual( float value, float expected )
{
	const float delta = value - expected;

	return ( delta > -0.001f && delta < 0.001f ) ? qtrue : qfalse;
}

/* --------------------------------------------------------------- tests */

/*
 * A capsule target is a cylinder capped by a sphere at each end.  Every
 * approach that reaches the hull has to be blocked, including the ones that
 * only the cap spheres can catch.  An engine that models the target as one
 * cylinder plus a single head sphere passes straight through from below and
 * at foot level, which is the regression this gate exists for.
 */
static void TestEveryApproachIsBlocked( void )
{
	CHECK( NearlyEqual( SweepFraction( 80, 0, 0, 20, 0, 0 ), 0.766667f ) );
	CHECK( NearlyEqual( SweepFraction( 0, 0, 200, 0, 0, 20 ), 0.794445f ) );
	CHECK( NearlyEqual( SweepFraction( 0, 0, -200, 0, 0, -20 ), 0.794445f ) );
	CHECK( NearlyEqual( SweepFraction( 90, 0, 90, 0, 0, 30 ), 0.735575f ) );
	CHECK( NearlyEqual( SweepFraction( 90, 0, -90, 0, 0, -30 ), 0.735575f ) );
	CHECK( NearlyEqual( SweepFraction( 90, 0, 56, 0, 0, 56 ), 0.913220f ) );
	CHECK( NearlyEqual( SweepFraction( 90, 0, -30, 0, 0, -30 ), 0.658436f ) );

	/* a sweep that clears the expanded radius still misses */
	CHECK( SweepFraction( 100, 90, 0, -100, 90, 0 ) == 1.0f );
	CHECK( SweepFraction( 0, 0, 400, 0, 0, 300 ) == 1.0f );
}

/*
 * The cap spheres answer with an outward radial plane, so a sweep from below
 * must be pushed down and a sweep from above must be pushed up.
 */
static void TestCapContactsFaceTheSweep( void )
{
	trace_t above, below;
	vec3_t start, end;

	VectorSet( start, 0, 0, 200 );
	VectorSet( end, 0, 0, 20 );
	CapsuleSweep( &above, start, end );

	VectorSet( start, 0, 0, -200 );
	VectorSet( end, 0, 0, -20 );
	CapsuleSweep( &below, start, end );

	CHECK( above.plane.normal[2] > 0.99f );
	CHECK( below.plane.normal[2] < -0.99f );
	CHECK( above.fraction < 1.0f && below.fraction < 1.0f );
}

/*
 * Moving capsule contacts retain CONTENTS_BODY without hitscan metadata.
 */
static void TestContactContentsStayRetailShaped( void )
{
	trace_t trace;
	vec3_t start, end;

	VectorSet( start, 0, 0, 200 );
	VectorSet( end, 0, 0, 20 );
	CapsuleSweep( &trace, start, end );

	CHECK( trace.fraction < 1.0f );
	CHECK( trace.contents == CONTENTS_BODY );
}

/* The retail point profile has a full-height body and a smaller sphere at
 * the top. An earlier head hit marks 0x400; torso/feet and misses do not.
 * Check real traces, including ducked bounds and a normal box-trace query. */
static void TestPointHitRegions( void )
{
	trace_t trace;
	vec3_t start, end;
	int capsule;

	VectorSet( start, 0, 0, 100 );
	VectorSet( end, 0, 0, 0 );
	CM_BoxTrace( &trace, start, end, vec3_origin, vec3_origin,
		CAPSULE_MODEL_HANDLE, PLAYER_CLIP_MASK, qtrue );
	CHECK( NearlyEqual( trace.fraction, 0.565f ) );
	CHECK( trace.contents == ( CONTENTS_BODY | CONTENTS_HEADSHOT ) );
	CHECK( trace.plane.normal[2] > 0.99f );

	VectorSet( start, 100, 0, 0 );
	VectorSet( end, -100, 0, 0 );
	CM_BoxTrace( &trace, start, end, vec3_origin, vec3_origin,
		CAPSULE_MODEL_HANDLE, PLAYER_CLIP_MASK, qtrue );
	CHECK( NearlyEqual( trace.fraction, 0.4125f ) );
	CHECK( trace.contents == CONTENTS_BODY );
	start[2] = end[2] = -23;
	CM_BoxTrace( &trace, start, end, vec3_origin, vec3_origin,
		CAPSULE_MODEL_HANDLE, PLAYER_CLIP_MASK, qtrue );
	CHECK( NearlyEqual( trace.fraction, 0.4125f ) );
	CHECK( trace.contents == CONTENTS_BODY );

	VectorSet( start, 100, 40, 33 );
	VectorSet( end, -100, 40, 33 );
	CM_BoxTrace( &trace, start, end, vec3_origin, vec3_origin,
		CAPSULE_MODEL_HANDLE, PLAYER_CLIP_MASK, qtrue );
	CHECK( trace.fraction == 1.0f );
	CHECK( trace.contents == 0 );

	stubModel.maxs[2] = 16;
	for ( capsule = 0; capsule <= 1; ++capsule ) {
		VectorSet( start, 0, 0, 100 );
		VectorClear( end );
		CM_BoxTrace( &trace, start, end, vec3_origin, vec3_origin,
			CAPSULE_MODEL_HANDLE, PLAYER_CLIP_MASK, capsule );
		if ( capsule ) {
			CHECK( NearlyEqual( trace.fraction, 0.725f ) );
			CHECK( trace.contents == ( CONTENTS_BODY | CONTENTS_HEADSHOT ) );
		} else {
			CHECK( !( trace.contents & CONTENTS_HEADSHOT ) );
		}
		/* The box query replaces the temporary model in the test stub. */
		VectorCopy( playerMins, stubModel.mins );
		VectorCopy( playerMaxs, stubModel.maxs );
		stubModel.maxs[2] = 16;
	}
	VectorCopy( playerMaxs, stubModel.maxs );
}

/*
 * Position tests and zero-length sweeps must stay startsolid-shaped rather
 * than reporting a blocked fractional move.
 */
static void TestOverlapReportsStartSolid( void )
{
	trace_t trace;
	vec3_t start, end;

	VectorSet( start, 0, 0, 0 );
	VectorSet( end, 0, 0, 0 );
	CapsuleSweep( &trace, start, end );

	CHECK( trace.startsolid == qtrue );
	CHECK( trace.fraction == 0.0f );
	CHECK( CM_TraceResultHasValidPlaneContract( &trace, CM_TRACE_PLANE_ANALYTIC ) );
}

/*
 * The analytic capsule plane is scaled by 1/(radius + RADIUS_EPSILON) in
 * single precision, so it drifts off unit length as the sweep start moves
 * away from the target.  Sweeping the whole world extent must still satisfy
 * the invariant the debug build asserts.
 */
static void TestPlaneContractHoldsAcrossTheWorld( void )
{
	static const float ranges[] = { 120.0f, 4000.0f, 65536.0f };
	unsigned int rng = 0x1234567u;
	unsigned int range;
	int violations = 0;
	int i;

	for ( range = 0; range < sizeof( ranges ) / sizeof( ranges[0] ); ++range ) {
		for ( i = 0; i < 20000; ++i ) {
			trace_t trace;
			vec3_t start, end;
			float axis[6];
			int axisIndex;

			for ( axisIndex = 0; axisIndex < 6; ++axisIndex ) {
				rng = rng * 1664525u + 1013904223u;
				axis[axisIndex] = ( (float)( ( rng >> 8 ) & 0xFFFFFF )
					/ (float)0x1000000 - 0.5f ) * 2.0f;
			}

			VectorSet( start, axis[0] * ranges[range], axis[1] * ranges[range],
				axis[2] * ranges[range] * 0.15f );
			VectorSet( end, axis[3] * 60.0f, axis[4] * 60.0f, axis[5] * 60.0f );
			if ( VectorCompare( start, end ) ) {
				continue;
			}

			CapsuleSweep( &trace, start, end );
			if ( !CM_TraceResultHasValidPlaneContract( &trace,
					CM_TRACE_PLANE_ANALYTIC ) ) {
				++violations;
			}
		}
	}

	CHECK( violations == 0 );
}

int main( void )
{
	cm.numNodes = 1;
	cm_noCurves = Cvar_Get( "cm_noCurves", "0", 0 );
	cm_playerCurveClip = Cvar_Get( "cm_playerCurveClip", "1", 0 );
	VectorCopy( playerMins, stubModel.mins );
	VectorCopy( playerMaxs, stubModel.maxs );

	TestEveryApproachIsBlocked();
	TestCapContactsFaceTheSweep();
	TestContactContentsStayRetailShaped();
	TestPointHitRegions();
	TestOverlapReportsStartSolid();
	TestPlaneContractHoldsAcrossTheWorld();

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
