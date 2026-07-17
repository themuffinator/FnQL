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

#ifndef TR_LENS_FLARE_H
#define TR_LENS_FLARE_H

#include <float.h>

typedef struct {
	float axisPosition;
	float halfWidthScale;
	float halfHeightScale;
	float intensity;
	float tint[3];
} lensFlareSprite_t;

/*
 * Positions run from the light (0) through the optical centre (1).  Every
 * element reuses the retail flare image through a dedicated additive shader,
 * keeping this shared layout independent of backend-specific implementation.
 */
static const lensFlareSprite_t r_lensFlareSprites[] = {
	/* broad source halo and a restrained horizontal sensor streak */
	{ 0.00f, 1.80f, 1.80f, 0.28f, { 1.00f, 0.98f, 0.92f } },
	{ 0.00f, 2.70f, 0.12f, 0.18f, { 0.78f, 0.90f, 1.00f } },
	/* aperture ghosts distributed across the light-to-centre axis */
	{ 0.36f, 0.38f, 0.38f, 0.30f, { 0.70f, 0.90f, 1.00f } },
	{ 0.70f, 0.20f, 0.20f, 0.35f, { 1.00f, 0.72f, 0.50f } },
	{ 1.08f, 0.52f, 0.52f, 0.22f, { 0.62f, 1.00f, 0.72f } },
	{ 1.48f, 0.30f, 0.30f, 0.28f, { 0.68f, 0.80f, 1.00f } }
};

static ID_INLINE int R_LensFlareSpriteCount( void ) {
	return (int)( sizeof( r_lensFlareSprites ) / sizeof( r_lensFlareSprites[0] ) );
}

static ID_INLINE float R_LensFlareClamp01( float value ) {
	if ( value != value ) {
		return 0.0f;
	}
	if ( value < 0.0f ) {
		return 0.0f;
	}
	if ( value > 1.0f ) {
		return 1.0f;
	}
	return value;
}

static ID_INLINE qboolean R_LensFlareFinite( float value ) {
	return ( value == value && value <= FLT_MAX && value >= -FLT_MAX ) ?
		qtrue : qfalse;
}

static ID_INLINE float R_LensFlareEdgeAttenuation( float lightX, float lightY,
	float centreX, float centreY, float viewportWidth, float viewportHeight ) {
	float halfWidth = viewportWidth * 0.5f;
	float halfHeight = viewportHeight * 0.5f;
	float nx;
	float ny;
	float radial;

	if ( !R_LensFlareFinite( lightX ) || !R_LensFlareFinite( lightY ) ||
		!R_LensFlareFinite( centreX ) || !R_LensFlareFinite( centreY ) ||
		!R_LensFlareFinite( viewportWidth ) || !R_LensFlareFinite( viewportHeight ) ||
		halfWidth <= 0.0f || halfHeight <= 0.0f ) {
		return 0.0f;
	}

	nx = ( lightX - centreX ) / halfWidth;
	ny = ( lightY - centreY ) / halfHeight;
	radial = R_LensFlareClamp01( nx * nx + ny * ny );
	return 1.0f - radial * 0.25f;
}

static ID_INLINE void R_LensFlareSpritePosition( const lensFlareSprite_t *sprite,
	float lightX, float lightY, float centreX, float centreY, float *x, float *y ) {
	*x = lightX + ( centreX - lightX ) * sprite->axisPosition;
	*y = lightY + ( centreY - lightY ) * sprite->axisPosition;
}

static ID_INLINE unsigned char R_LensFlareSpriteColor( float color, unsigned char fog,
	const lensFlareSprite_t *sprite, int channel, float attenuation ) {
	double value;

	if ( !sprite || channel < 0 || channel >= 3 || !R_LensFlareFinite( color ) ||
		!R_LensFlareFinite( attenuation ) || !R_LensFlareFinite( sprite->intensity ) ||
		!R_LensFlareFinite( sprite->tint[channel] ) || color <= 0.0f || !fog ||
		sprite->intensity <= 0.0f || sprite->tint[channel] <= 0.0f ||
		attenuation <= 0.0f ) {
		return 0;
	}
	value = (double)color * (double)fog * (double)sprite->intensity *
		(double)sprite->tint[channel] * (double)attenuation;
	if ( value >= 255.0f ) {
		return 255;
	}
	return (unsigned char)( value + 0.5f );
}

#endif
