/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL and is distributed under the terms of the GNU
General Public License version 2 or (at your option) any later version.
===========================================================================
*/
#ifndef FNQL_MODULE_PROVIDER_POLICY_H
#define FNQL_MODULE_PROVIDER_POLICY_H

/* Include q_shared.h and fnql_module.h first.
 *
 * Pure decisions behind vm_gameProvider, vm_cgameProvider and vm_uiProvider
 * (docs/fnql/GAME_MODULES.md), kept free of engine state so
 * tests/module_provider_policy_tests.c can gate them.
 *
 * Retail has no provider choice: its engine loads qagamex86.dll,
 * cgamex86.dll and uix86.dll by name from the "bin" paks.  Everything here
 * is FnQL policy, and "retail" always keeps the historical path. */

typedef enum {
	VM_PROVIDER_CHOICE_RETAIL,	// the retail-named module (historical behavior)
	VM_PROVIDER_CHOICE_FNQL,	// the FnQL-game module, whatever it claims
	VM_PROVIDER_CHOICE_AUTO		// the FnQL-game module only if it claims retail wire parity
} vmProviderChoice_t;

/* Parses a provider cvar value.  An empty value is retail.  An unknown value
 * is retail too, and *recognized tells the caller to warn about it. */
static ID_INLINE vmProviderChoice_t VM_ParseProviderChoice( const char *value, int *recognized )
{
	if ( recognized ) {
		*recognized = 1;
	}
	if ( !value || !value[0] || !Q_stricmp( value, "retail" ) ) {
		return VM_PROVIDER_CHOICE_RETAIL;
	}
	if ( !Q_stricmp( value, "fnql" ) ) {
		return VM_PROVIDER_CHOICE_FNQL;
	}
	if ( !Q_stricmp( value, "auto" ) ) {
		return VM_PROVIDER_CHOICE_AUTO;
	}
	if ( recognized ) {
		*recognized = 0;
	}
	return VM_PROVIDER_CHOICE_RETAIL;
}

/* Whether an identified FnQL-game module may serve a provider choice.
 * "auto" is the selection a default can safely use: it takes only a module
 * that claims its default profile is retail on the wire. */
static ID_INLINE int VM_ProviderAcceptsModule( vmProviderChoice_t choice, unsigned capabilities )
{
	switch ( choice ) {
	case VM_PROVIDER_CHOICE_FNQL:
		return 1;
	case VM_PROVIDER_CHOICE_AUTO:
		return ( capabilities & FNQL_MODULE_CAP_RETAIL_WIRE ) ? 1 : 0;
	default:
		return 0;
	}
}

/* Whether a space-separated list of decimal pure checksums (the
 * sv_fnqlClientPaks systeminfo value) names `checksum`.  The list comes from
 * the server, so it is parsed defensively: bounded tokens, optional sign,
 * nothing but digits; a malformed token never matches. */
#define VM_CHECKSUM_LIST_MAX_TOKENS 64

static ID_INLINE int VM_ChecksumListContains( const char *list, int checksum )
{
	int tokens = 0;

	if ( !list ) {
		return 0;
	}
	while ( *list && tokens < VM_CHECKSUM_LIST_MAX_TOKENS ) {
		const char *start;
		int negative = 0, digits = 0, valid = 1;
		unsigned int magnitude = 0;

		while ( *list == ' ' ) {
			list++;
		}
		if ( !*list ) {
			break;
		}
		start = list;
		if ( *list == '-' ) {
			negative = 1;
			list++;
		}
		while ( *list && *list != ' ' ) {
			if ( *list < '0' || *list > '9' || digits >= 10 ) {
				valid = 0;
			} else {
				unsigned int next = magnitude * 10u + (unsigned int)( *list - '0' );
				if ( next / 10u != magnitude ) {
					valid = 0;
				}
				magnitude = next;
				digits++;
			}
			list++;
		}
		tokens++;
		if ( !valid || !digits || list - start > 11 ) {
			continue;
		}
		if ( negative ? magnitude > 2147483648u : magnitude > 2147483647u ) {
			continue;
		}
		if ( ( negative ? (int)( 0u - magnitude ) : (int)magnitude ) == checksum ) {
			return 1;
		}
	}
	return 0;
}

#endif
