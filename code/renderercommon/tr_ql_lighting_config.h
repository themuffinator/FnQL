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

#ifndef TR_QL_LIGHTING_CONFIG_H
#define TR_QL_LIGHTING_CONFIG_H

#define QL_LIGHTING_CONFIG_VERSION 1

/* FnQL initially inherited FnQ3's renderer defaults. Repair only exact stale
 * values, before the renderer registers them with the retail defaults below,
 * so explicit user choices remain untouched. */
static ID_INLINE void R_MigrateQLLightingConfig( void )
{
	cvar_t *version = ri.Cvar_Get( "r_qlLightingConfigVersion", "0",
		CVAR_ARCHIVE | CVAR_PROTECTED );
	const char *intensity = ri.Cvar_VariableString( "r_intensity" );
	const char *textureMode = ri.Cvar_VariableString( "r_textureMode" );

	ri.Cvar_CheckRange( version, "0", XSTRING( QL_LIGHTING_CONFIG_VERSION ),
		CV_INTEGER );
	ri.Cvar_SetDescription( version,
		"Internal version for one-time repair of inherited FnQ3 renderer defaults." );
	ri.Cvar_SetGroup( version, CVG_RENDERER );

	if ( version->integer >= QL_LIGHTING_CONFIG_VERSION ) {
		return;
	}

	if ( intensity[0] && textureMode[0]
		&& Q_fabs( (float)atof( intensity ) - 1.25f ) < 0.0001f
		&& !Q_stricmp( textureMode, "GL_LINEAR_MIPMAP_NEAREST" ) ) {
		ri.Cvar_Set( "r_intensity", "1" );
		ri.Cvar_Set( "r_textureMode", "GL_LINEAR_MIPMAP_LINEAR" );
	}

	ri.Cvar_Set( "r_qlLightingConfigVersion",
		XSTRING( QL_LIGHTING_CONFIG_VERSION ) );
}

#endif /* TR_QL_LIGHTING_CONFIG_H */
