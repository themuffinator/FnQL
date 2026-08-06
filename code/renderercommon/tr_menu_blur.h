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

#ifndef TR_MENU_BLUR_H
#define TR_MENU_BLUR_H

/*
Menu soft focus.

While an in-game menu owns the screen the finished frame behind it is replaced
with a soft-focus copy of itself, so the menu reads as the foreground instead of
competing with a sharp, moving scene. The client owns the decision and the fade;
this header owns the sampling plan, so the GL, Vulkan, and RTX backends produce
the same softness from the same inputs instead of each inventing one.

The plan is a small Gaussian pyramid rather than one wide kernel:

- the finished frame is box-downsampled to 1/2 and then to 1/4. Going straight
  to 1/4 in a single bilinear step discards three of every four texels, and the
  discarded set changes as the scene animates, which makes the backdrop crawl.
  Two exact halvings average every texel instead;
- 1/4 resolution carries the separable Gaussian iterations. Each iteration is
  cheap there, and a texel of spacing covers four screen pixels, so a wide blur
  needs few passes;
- per-pass sigmas grow linearly. The first pass is tight, which removes the
  high frequencies that make sparse taps visible as banding; later passes run on
  already-smooth data, where wider gaps cost nothing in quality;
- the result is bilinearly resampled back to full resolution and composited.

Total sigma is a fraction of render-target height, so the softness looks the
same at 720p and at 4K, and it scales with the caller's strength so a fade in or
out is a real focus pull rather than a cross-fade between two sharp-ish images.

Backends differ in the kernel their blur pass implements, so the plan reports
per-pass sigma and each backend converts that to its own tap spacing with
R_MenuBlur_TapSpacing and its own kernel variance. Matching sigma is what makes
the three renderers agree; matching tap offsets would not.
*/

#include <math.h>

#define MENU_BLUR_PASS_COUNT 4

/* Sigma of the finished blur as a fraction of render-target height. */
#define MENU_BLUR_SIGMA_FRACTION 0.013f

/* The pyramid level that carries the separable iterations. */
#define MENU_BLUR_LEVEL_DIVISOR 4

/*
Unit-spacing variance of each backend's separable kernel.

GL builds a 6-tap binomial kernel (weights 1,5,10,10,5,1 at +/-0.5, +/-1.5,
+/-2.5 texels). Vulkan and RTX use the 3-tap linear-sampling kernel that
emulates a 5-tap binomial (weights 5,6,5 at -1.2, 0, +1.2 texels).
*/
#define MENU_BLUR_KERNEL_VARIANCE_BINOMIAL6 1.25f
#define MENU_BLUR_KERNEL_VARIANCE_LINEAR3 0.9f

/*
Where the linear-3 kernel's outer taps sit, in units of spacing. Its variance
above is 2 * (5/16) * this squared, so the two constants move together.
*/
#define MENU_BLUR_LINEAR3_TAP 1.2f

/* Below this the composite is invisible but the passes are not free. */
#define MENU_BLUR_MIN_ALPHA 0.004f

typedef struct menuBlurPlan_s {
	qboolean	enabled;
	int			halfWidth;
	int			halfHeight;
	int			levelWidth;		/* 1/MENU_BLUR_LEVEL_DIVISOR of the target */
	int			levelHeight;
	int			passCount;		/* separable horizontal+vertical iterations */
	float		passSigma[MENU_BLUR_PASS_COUNT];	/* in level texels */
	float		alpha;			/* composite weight over the sharp frame */
} menuBlurPlan_t;

static int R_MenuBlur_ScaleExtent( int extent, int divisor )
{
	if ( extent < divisor ) {
		return 1;
	}
	return extent / divisor;
}

/*
Convert a per-pass sigma into the tap spacing a separable kernel of the given
unit-spacing variance has to use to reach it.
*/
static float R_MenuBlur_TapSpacing( float passSigma, float kernelUnitVariance )
{
	if ( !( passSigma > 0.0f ) || !( kernelUnitVariance > 0.0f ) ) {
		return 0.0f;
	}
	return passSigma / (float)sqrt( (double)kernelUnitVariance );
}

/*
Resolve the sampling plan for a render target of the given size.

strength is the client's faded soft-focus amount in 0..1. A non-positive or
non-finite strength, or a target too small to hold a pyramid level, reports a
disabled plan, which every backend treats as "draw nothing and leave the frame
alone" rather than as an error.
*/
static qboolean R_MenuBlur_Plan( float strength, int targetWidth, int targetHeight,
	menuBlurPlan_t *plan )
{
	float totalSigma;
	float unitSum;
	float scale;
	int i;

	if ( !plan ) {
		return qfalse;
	}

	plan->enabled = qfalse;
	plan->halfWidth = 0;
	plan->halfHeight = 0;
	plan->levelWidth = 0;
	plan->levelHeight = 0;
	plan->passCount = 0;
	plan->alpha = 0.0f;
	for ( i = 0; i < MENU_BLUR_PASS_COUNT; i++ ) {
		plan->passSigma[i] = 0.0f;
	}

	/* A NaN strength fails every comparison, so test for the usable range
	 * rather than excluding the unusable one. */
	if ( !( strength > MENU_BLUR_MIN_ALPHA ) ) {
		return qfalse;
	}
	if ( strength > 1.0f ) {
		strength = 1.0f;
	}
	if ( targetWidth <= 0 || targetHeight <= 0 ) {
		return qfalse;
	}
	/* Two exact halvings need a target that still has texels left at 1/4. */
	if ( targetWidth < MENU_BLUR_LEVEL_DIVISOR || targetHeight < MENU_BLUR_LEVEL_DIVISOR ) {
		return qfalse;
	}

	plan->halfWidth = R_MenuBlur_ScaleExtent( targetWidth, 2 );
	plan->halfHeight = R_MenuBlur_ScaleExtent( targetHeight, 2 );
	plan->levelWidth = R_MenuBlur_ScaleExtent( targetWidth, MENU_BLUR_LEVEL_DIVISOR );
	plan->levelHeight = R_MenuBlur_ScaleExtent( targetHeight, MENU_BLUR_LEVEL_DIVISOR );

	/* Sigma is expressed against the full-resolution frame; the iterations run
	 * on a level whose texels are MENU_BLUR_LEVEL_DIVISOR pixels wide. */
	totalSigma = MENU_BLUR_SIGMA_FRACTION * (float)targetHeight * strength /
		(float)MENU_BLUR_LEVEL_DIVISOR;
	if ( !( totalSigma > 0.0f ) ) {
		return qfalse;
	}

	/* Per-pass sigmas grow as 1,2,3,4 and combine in quadrature, so scale that
	 * ramp to land exactly on the requested total. */
	unitSum = 0.0f;
	for ( i = 0; i < MENU_BLUR_PASS_COUNT; i++ ) {
		unitSum += (float)( ( i + 1 ) * ( i + 1 ) );
	}
	scale = totalSigma / (float)sqrt( (double)unitSum );
	for ( i = 0; i < MENU_BLUR_PASS_COUNT; i++ ) {
		plan->passSigma[i] = scale * (float)( i + 1 );
	}

	plan->passCount = MENU_BLUR_PASS_COUNT;
	plan->alpha = strength;
	plan->enabled = qtrue;

	return qtrue;
}

#endif // TR_MENU_BLUR_H
