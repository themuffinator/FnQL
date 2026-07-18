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

#ifndef TR_FOG_MATH_H
#define TR_FOG_MATH_H

#include <math.h>

#define FOG_DISTANCE_BIAS  ( 1.0f / 512.0f )
#define FOG_DEPTH_MIN      ( 1.0f / 32.0f )
#define FOG_DEPTH_MAX      ( 31.0f / 32.0f )
#define FOG_DEPTH_RANGE    ( 30.0f / 32.0f )
#define FOG_DISTANCE_SCALE 8.0f

static ID_INLINE float R_AnalyticFogFactor( float s, float t )
{
	s -= FOG_DISTANCE_BIAS;
	if ( s <= 0.0f || t < FOG_DEPTH_MIN ) {
		return 0.0f;
	}
	if ( t < FOG_DEPTH_MAX ) {
		s *= ( t - FOG_DEPTH_MIN ) / FOG_DEPTH_RANGE;
	}
	s *= FOG_DISTANCE_SCALE;
	return s >= 1.0f ? 1.0f : sqrtf( s );
}

static ID_INLINE float R_LegacyFogFactor( float s, float t,
	const float *fogTable, int fogTableSize )
{
	s -= FOG_DISTANCE_BIAS;
	if ( s < 0.0f || t < FOG_DEPTH_MIN || !fogTable || fogTableSize < 2 ) {
		return 0.0f;
	}
	if ( t < FOG_DEPTH_MAX ) {
		s *= ( t - FOG_DEPTH_MIN ) / FOG_DEPTH_RANGE;
	}
	s = MIN( s * FOG_DISTANCE_SCALE, 1.0f );
	return fogTable[(int)( s * ( fogTableSize - 1 ) )];
}

#endif // TR_FOG_MATH_H
