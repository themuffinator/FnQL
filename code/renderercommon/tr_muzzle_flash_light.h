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

#ifndef TR_MUZZLE_FLASH_LIGHT_H
#define TR_MUZZLE_FLASH_LIGHT_H

#define R_MUZZLE_FLASH_LIGHT_MATCH_RADIUS 0.25f

typedef struct {
	qboolean valid;
	vec3_t origin;
	vec3_t forward;
} muzzleFlashLightCandidate_t;

static muzzleFlashLightCandidate_t r_muzzleFlashLightCandidate;

static qboolean R_IsMuzzleFlashModelName( const char *name ) {
	static const char suffix[] = "_flash.md3";
	size_t nameLength;
	size_t suffixLength = sizeof( suffix ) - 1;

	if ( !name ) {
		return qfalse;
	}

	nameLength = strlen( name );
	if ( nameLength < suffixLength ) {
		return qfalse;
	}

	return Q_stricmp( name + nameLength - suffixLength, suffix ) == 0 ?
		qtrue : qfalse;
}

static void R_ClearMuzzleFlashLightCandidate( void ) {
	Com_Memset( &r_muzzleFlashLightCandidate, 0,
		sizeof( r_muzzleFlashLightCandidate ) );
}

/*
====================
R_TrackMuzzleFlashLightEntity

Retail cgame submits a weapon's registered *_flash.md3 entity immediately
before its dynamic light. Preserve that ABI and retain only the origin and
normalized model-forward axis needed to identify and adjust the next light.
Any intervening model submission invalidates the candidate.
====================
*/
static void R_TrackMuzzleFlashLightEntity( const refEntity_t *ent ) {
	model_t *model;
	vec3_t forward;
	int component;

	R_ClearMuzzleFlashLightCandidate();

	if ( !ent || ent->reType != RT_MODEL || ent->hModel <= 0 ) {
		return;
	}

	model = R_GetModelByHandle( ent->hModel );
	if ( !model || !R_IsMuzzleFlashModelName( model->name ) ) {
		return;
	}

	for ( component = 0; component < 3; component++ ) {
		if ( !R_DlightShadowFloatIsFinite( ent->origin[component] ) ||
			!R_DlightShadowFloatIsFinite( ent->axis[0][component] ) ) {
			return;
		}
	}

	if ( VectorNormalize2( ent->axis[0], forward ) <= 0.0001f ) {
		return;
	}

	VectorCopy( ent->origin, r_muzzleFlashLightCandidate.origin );
	VectorCopy( forward, r_muzzleFlashLightCandidate.forward );
	r_muzzleFlashLightCandidate.valid = qtrue;
}

/*
====================
R_PrepareMuzzleFlashLight

Consumes the pending candidate even on a mismatch so stale flash entities can
never affect a later, unrelated light. Returns qtrue only for the matching
muzzle light; adjustedOrigin always receives a safe copy of the submitted
origin.
====================
*/
static qboolean R_PrepareMuzzleFlashLight( const vec3_t origin,
	vec3_t adjustedOrigin ) {
	muzzleFlashLightCandidate_t candidate;
	vec3_t delta;
	float offset;
	int component;

	VectorCopy( origin, adjustedOrigin );
	if ( !r_muzzleFlashLightCandidate.valid ) {
		return qfalse;
	}

	candidate = r_muzzleFlashLightCandidate;
	R_ClearMuzzleFlashLightCandidate();

	for ( component = 0; component < 3; component++ ) {
		if ( !R_DlightShadowFloatIsFinite( origin[component] ) ) {
			return qfalse;
		}
	}

	VectorSubtract( origin, candidate.origin, delta );
	if ( VectorLengthSquared( delta ) >
		Square( R_MUZZLE_FLASH_LIGHT_MATCH_RADIUS ) ) {
		return qfalse;
	}

	offset = r_muzzleFlashDlightOffset ?
		r_muzzleFlashDlightOffset->value : 0.0f;
	if ( R_DlightShadowFloatIsFinite( offset ) ) {
		VectorMA( adjustedOrigin, offset, candidate.forward, adjustedOrigin );
	}

	return qtrue;
}

static qboolean R_MuzzleFlashLightShadowEligible( qboolean muzzleFlash ) {
	if ( !muzzleFlash || !r_muzzleFlashDlightShadows ) {
		return qtrue;
	}

	return r_muzzleFlashDlightShadows->integer ? qtrue : qfalse;
}

#endif
