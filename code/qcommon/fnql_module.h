/*
===========================================================================
FnQL -- Fappin' Quake Live engine.

This file is part of FnQL and is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.
===========================================================================
*/
//
// fnql_module.h -- module identity contract shared with FnQL-game.
//
// FnQL-game (the companion repository at ../FnQL-game) builds clean-room
// replacements for the retail game modules.  Each one exports
// FnQL_GetModuleInfo next to its retail-ABI dllEntry; retail modules do not,
// and the retail engine never looks the symbol up.  The engine uses the
// record to report module provenance and to refuse a provider-selected module
// that does not identify itself.  FnQL-game keeps an identical copy in
// code/game/fnql_module.h; its scripts/check_engine_abi.py compares the two.
//
// The structure is append-only: new fields go at the end and size says how
// much of it the module filled in.  See docs/fnql/GAME_MODULES.md.
//

#ifndef FNQL_MODULE_H
#define FNQL_MODULE_H

#define FNQL_MODULE_INFO_SYMBOL		"FnQL_GetModuleInfo"
#define FNQL_MODULE_INFO_VERSION	1
#define FNQL_MODULE_PRODUCT			"FnQL-game"

// The module's default profile is bit-for-bit retail on the wire: its
// snapshots, configstrings, server commands and prediction match retail
// Quake Live, so retail peers cannot tell it apart from the retail module.
#define FNQL_MODULE_CAP_RETAIL_WIRE		0x00000001u
// The module negotiates FnQL extensions with capable peers and keeps them
// away from retail peers.
#define FNQL_MODULE_CAP_EXTENSIONS		0x00000002u

typedef struct fnqlModuleInfo_s {
	int				size;			// sizeof( fnqlModuleInfo_t ) as built by the module
	int				infoVersion;	// FNQL_MODULE_INFO_VERSION
	const char		*product;		// FNQL_MODULE_PRODUCT
	const char		*module;		// "qagame", "cgame" or "ui"
	const char		*version;		// FnQL-game semantic version
	const char		*build;			// free-form build identifier
	unsigned int	capabilities;	// FNQL_MODULE_CAP_* bits
	int				nativeApi;		// native API version the module publishes
} fnqlModuleInfo_t;

typedef const fnqlModuleInfo_t *( QDECL *fnqlGetModuleInfo_t )( void );

#endif // FNQL_MODULE_H
