/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL.

FnQL is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 2 of the License, or (at your option) any later
version.
===========================================================================
*/

#ifndef TR_FNQ3_BLOOM_CONFIG_H
#define TR_FNQ3_BLOOM_CONFIG_H

#define FNQ3_BLOOM_CONFIG_VERSION 1

/* A short-lived FnQL build persisted this exact canonical-cvar tuple before
 * its renderer ownership was corrected. One pass is outside the FnQ3 GLx
 * range, which makes the tuple narrow enough to repair without treating a
 * normal low-threshold profile as stale. */
static ID_INLINE qboolean R_FnQ3BloomConfigNeedsRepair( void )
{
	const char *threshold = ri.Cvar_VariableString( "r_bloom_threshold" );
	const char *intensity = ri.Cvar_VariableString( "r_bloom_intensity" );
	const char *passes = ri.Cvar_VariableString( "r_bloom_passes" );
	const char *thresholdMode = ri.Cvar_VariableString( "r_bloom_threshold_mode" );
	const char *modulate = ri.Cvar_VariableString( "r_bloom_modulate" );

	return threshold[0] && intensity[0] && passes[0]
		&& Q_fabs( (float)atof( threshold ) - 0.25f ) < 0.0001f
		&& Q_fabs( (float)atof( intensity ) - 0.5f ) < 0.0001f
		&& atoi( passes ) == 1
		&& ( !thresholdMode[0] || atoi( thresholdMode ) == 0 )
		&& ( !modulate[0] || atoi( modulate ) == 0 )
		? qtrue : qfalse;
}

static ID_INLINE void R_MigrateFnQ3BloomConfig( void )
{
	cvar_t *version = ri.Cvar_Get( "r_fnq3BloomConfigVersion", "0",
		CVAR_ARCHIVE | CVAR_PROTECTED );

	ri.Cvar_CheckRange( version, "0", XSTRING( FNQ3_BLOOM_CONFIG_VERSION ),
		CV_INTEGER );
	ri.Cvar_SetDescription( version,
		"Internal version for one-time repair of obsolete FnQL-authored FnQ3 bloom defaults." );
	ri.Cvar_SetGroup( version, CVG_RENDERER );

	if ( version->integer >= FNQ3_BLOOM_CONFIG_VERSION ) {
		return;
	}

	if ( R_FnQ3BloomConfigNeedsRepair() ) {
		ri.Cvar_Set( "r_bloom_threshold", "0.75" );
		ri.Cvar_Set( "r_bloom_passes", "5" );
		ri.Printf( PRINT_ALL,
			"Restored FnQ3 bloom defaults from obsolete FnQL profile values.\n" );
	}

	ri.Cvar_Set( "r_fnq3BloomConfigVersion",
		XSTRING( FNQ3_BLOOM_CONFIG_VERSION ) );
}

#endif /* TR_FNQ3_BLOOM_CONFIG_H */
