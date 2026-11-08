/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL and is distributed under the terms of the GNU
General Public License version 2 or (at your option) any later version.
===========================================================================
*/

#include "../code/qcommon/q_shared.h"
#include "../code/qcommon/cvar_ui_policy.h"

#include <stdio.h>

static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
		failures++; \
	} \
} while (0)

/* Retail settings cvars carry 0x800; the installed menus must change them. */
static void TestRetailSettingsWritable(void) {
	CHECK(CVAR_PROTECTED == 0x800);
	/* cg_railStyle, cg_weaponConfig: retail cgame registration. */
	CHECK(Cvar_UIWriteAllowed(CVAR_ARCHIVE | CVAR_PROTECTED | CVAR_VM_CREATED | CVAR_CLOUD));
	/* r_windowedMode, m_cpi: engine-owned rows of the retail settings page. */
	CHECK(Cvar_UIWriteAllowed(CVAR_ARCHIVE | CVAR_LATCH | CVAR_PROTECTED | CVAR_CLOUD));
	CHECK(Cvar_UIWriteAllowed(CVAR_ARCHIVE | CVAR_PROTECTED | CVAR_CLOUD));
	/* Unflagged and missing cvars keep the Cvar_SetSafe behaviour. */
	CHECK(Cvar_UIWriteAllowed(0));
	CHECK(Cvar_UIWriteAllowed(CVAR_ARCHIVE | CVAR_VM_CREATED));
	CHECK(Cvar_UIWriteAllowed(CVAR_NONEXISTENT));
}

/* Engine secrets, identity and startup state stay out of the UI's reach. */
static void TestEngineStateRefused(void) {
	/* fs_basepath, fs_homepath, fs_steampath */
	CHECK(!Cvar_UIWriteAllowed(CVAR_INIT | CVAR_PROTECTED | CVAR_PRIVATE));
	CHECK(!Cvar_UIWriteAllowed(CVAR_PRIVATE));
	/* version, sys_cpustring, cl_guid */
	CHECK(!Cvar_UIWriteAllowed(CVAR_PROTECTED | CVAR_ROM | CVAR_SERVERINFO));
	CHECK(!Cvar_UIWriteAllowed(CVAR_USERINFO | CVAR_ROM | CVAR_PROTECTED));
	/* vm_rtChecks, journal, fs_basegame */
	CHECK(!Cvar_UIWriteAllowed(CVAR_INIT | CVAR_PROTECTED));
}

int main(void) {
	TestRetailSettingsWritable();
	TestEngineStateRefused();
	if (failures) {
		fprintf(stderr, "%d cvar UI policy check(s) failed\n", failures);
		return 1;
	}
	puts("cvar UI policy OK");
	return 0;
}
