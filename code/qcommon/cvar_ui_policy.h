/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL and is distributed under the terms of the GNU
General Public License version 2 or (at your option) any later version.
===========================================================================
*/
#ifndef FNQL_CVAR_UI_POLICY_H
#define FNQL_CVAR_UI_POLICY_H

/* Include q_shared.h first for the CVAR_* flags.
 *
 * Observed: retail Quake Live modules register their player settings with
 * CVAR_PROTECTED (0x800, the bit Cvar_WriteVariables routes to
 * repconfig.cfg), and the installed retail menus bind items to those cvars
 * (cg_railStyle, cg_autoswitch, cg_kickScale, cg_weaponConfig,
 * r_windowedMode). Both the retail uix86.dll and FnQL-game's UI write them
 * through UI imports 7 and 8 when such an item is clicked or edited.
 * Inference: the retail engine lets the UI change protected cvars, or those
 * menus would be inert.
 *
 * The UI may therefore change protected cvars, unlike servers and game
 * modules (Cvar_SetSafe). It may never change a private cvar, nor a
 * protected one that is also read-only or init-only: those hold engine
 * identity and startup state such as version, fs_basegame, cl_guid and
 * vm_rtChecks. */
static ID_INLINE int Cvar_UIWriteAllowed( unsigned flags )
{
	if ( flags == CVAR_NONEXISTENT ) {
		return 1;
	}
	if ( flags & CVAR_PRIVATE ) {
		return 0;
	}
	if ( ( flags & CVAR_PROTECTED ) && ( flags & ( CVAR_ROM | CVAR_INIT ) ) ) {
		return 0;
	}
	return 1;
}

#endif
