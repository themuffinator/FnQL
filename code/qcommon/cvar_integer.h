/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL and is distributed under the terms of the GNU
General Public License version 2 or (at your option) any later version.
===========================================================================
*/

#ifndef CVAR_INTEGER_H
#define CVAR_INTEGER_H

#include "q_shared.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Computes the cached cvar_t::integer for a cvar string.
 *
 * Observed: retail quakelive_steam.exe (the helper at 0x4cccc0, called from
 * the cvar create path right after atof) tests the string with
 * strstr( s, "0x" ) and, on a match, returns sscanf( s, "0x%08x" ) with a zero
 * default; otherwise it returns atoi( s ).  The retail cgame reads the packed
 * player colour cvars (cg_enemyUpperColor "0x2a8000FF" and friends) through
 * vmCvar_t::integer, so Quake III's plain atoi left them at 0 and forced enemy
 * models were drawn untinted (white).
 *
 * FnQL takes the hex path only when the string starts with "0x".  That gives
 * the retail result for every string retail parses as hex and for every string
 * atoi already maps to 0 (" 0x10", "-0x10", "0X10"), and keeps the atoi result
 * for strings that only contain "0x" further in, such as "1920x1080", where
 * retail yields 0.  Engine code reads some of those as integers, so the
 * leading number is kept rather than copying that retail quirk.
 */
static ID_INLINE int Cvar_ParseInteger( const char *s )
{
	unsigned int hexValue;

	if ( !s ) {
		return 0;
	}

	if ( s[0] == '0' && s[1] == 'x' ) {
		hexValue = 0;
		if ( sscanf( s, "0x%08x", &hexValue ) != 1 ) {
			return 0;
		}
		return (int)hexValue;
	}

	return atoi( s );
}

#endif // CVAR_INTEGER_H
