/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL and is distributed under the terms of the GNU
General Public License version 2 or (at your option) any later version.
===========================================================================
*/

/* Gates code/qcommon/module_provider_policy.h: the provider values and the
 * sv_fnqlClientPaks checksum list (docs/fnql/GAME_MODULES.md). */

#include "../code/qcommon/q_shared.h"
#include "../code/qcommon/fnql_module.h"
#include "../code/qcommon/module_provider_policy.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;

/* the header's only engine dependency, with q_shared.c's ASCII semantics */
int Q_stricmp( const char *s1, const char *s2 ) {
	unsigned char c1, c2;

	if ( !s1 || !s2 ) {
		return s1 == s2 ? 0 : ( s1 ? 1 : -1 );
	}
	do {
		c1 = (unsigned char)*s1++;
		c2 = (unsigned char)*s2++;
		if ( c1 >= 'A' && c1 <= 'Z' ) c1 = (unsigned char)( c1 + 32 );
		if ( c2 >= 'A' && c2 <= 'Z' ) c2 = (unsigned char)( c2 + 32 );
		if ( c1 != c2 ) {
			return c1 < c2 ? -1 : 1;
		}
	} while ( c1 );
	return 0;
}

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
		failures++; \
	} \
} while (0)

static void TestProviderValues(void) {
	int recognized = -1;

	CHECK(VM_ParseProviderChoice("retail", &recognized) == VM_PROVIDER_CHOICE_RETAIL && recognized);
	CHECK(VM_ParseProviderChoice("RETAIL", &recognized) == VM_PROVIDER_CHOICE_RETAIL && recognized);
	CHECK(VM_ParseProviderChoice("fnql", &recognized) == VM_PROVIDER_CHOICE_FNQL && recognized);
	CHECK(VM_ParseProviderChoice("FnQL", &recognized) == VM_PROVIDER_CHOICE_FNQL && recognized);
	CHECK(VM_ParseProviderChoice("auto", &recognized) == VM_PROVIDER_CHOICE_AUTO && recognized);
	/* empty and missing values are the historical retail path, silently */
	CHECK(VM_ParseProviderChoice("", &recognized) == VM_PROVIDER_CHOICE_RETAIL && recognized);
	CHECK(VM_ParseProviderChoice(NULL, &recognized) == VM_PROVIDER_CHOICE_RETAIL && recognized);
	/* anything else is retail, with a warning */
	CHECK(VM_ParseProviderChoice("fnql-game", &recognized) == VM_PROVIDER_CHOICE_RETAIL && !recognized);
	CHECK(VM_ParseProviderChoice("1", &recognized) == VM_PROVIDER_CHOICE_RETAIL && !recognized);
	CHECK(VM_ParseProviderChoice("autox", NULL) == VM_PROVIDER_CHOICE_RETAIL);
}

static void TestProviderAcceptance(void) {
	/* fnql takes any identified FnQL-game module */
	CHECK(VM_ProviderAcceptsModule(VM_PROVIDER_CHOICE_FNQL, 0));
	CHECK(VM_ProviderAcceptsModule(VM_PROVIDER_CHOICE_FNQL, FNQL_MODULE_CAP_EXTENSIONS));
	/* auto requires the retail wire parity claim */
	CHECK(!VM_ProviderAcceptsModule(VM_PROVIDER_CHOICE_AUTO, 0));
	CHECK(!VM_ProviderAcceptsModule(VM_PROVIDER_CHOICE_AUTO, FNQL_MODULE_CAP_EXTENSIONS));
	CHECK(VM_ProviderAcceptsModule(VM_PROVIDER_CHOICE_AUTO, FNQL_MODULE_CAP_RETAIL_WIRE));
	CHECK(VM_ProviderAcceptsModule(VM_PROVIDER_CHOICE_AUTO,
		FNQL_MODULE_CAP_RETAIL_WIRE | FNQL_MODULE_CAP_EXTENSIONS));
	/* retail never selects an FnQL-game module */
	CHECK(!VM_ProviderAcceptsModule(VM_PROVIDER_CHOICE_RETAIL, FNQL_MODULE_CAP_RETAIL_WIRE));
}

static void TestChecksumList(void) {
	char longList[2048];
	int i, len = 0;

	CHECK(VM_ChecksumListContains("12345", 12345));
	CHECK(VM_ChecksumListContains("-12345", -12345));
	CHECK(VM_ChecksumListContains("1 2 3", 3));
	CHECK(VM_ChecksumListContains("  7   8 ", 8));
	CHECK(VM_ChecksumListContains("2147483647", 2147483647));
	CHECK(VM_ChecksumListContains("-2147483648", (int)0x80000000u));
	CHECK(!VM_ChecksumListContains("", 0));
	CHECK(!VM_ChecksumListContains(NULL, 0));
	CHECK(!VM_ChecksumListContains("12345", 1234));
	CHECK(!VM_ChecksumListContains("123456", 12345));
	/* malformed or out-of-range tokens never match, and never stop the scan */
	CHECK(!VM_ChecksumListContains("12x45", 12));
	CHECK(!VM_ChecksumListContains("2147483648", (int)0x80000000u));
	CHECK(!VM_ChecksumListContains("4294967296", 0));
	CHECK(!VM_ChecksumListContains("99999999999", 1215752191));
	CHECK(!VM_ChecksumListContains("-", 0));
	CHECK(!VM_ChecksumListContains("--5", -5));
	CHECK(VM_ChecksumListContains("bad 5", 5));
	CHECK(VM_ChecksumListContains("4294967296 6", 6));
	/* the scan is bounded */
	for (i = 0; i < VM_CHECKSUM_LIST_MAX_TOKENS; i++) {
		len += snprintf(longList + len, sizeof(longList) - (size_t)len, "%d ", i + 100);
	}
	snprintf(longList + len, sizeof(longList) - (size_t)len, "%d", 42);
	CHECK(VM_ChecksumListContains(longList, 100));
	CHECK(VM_ChecksumListContains(longList, 100 + VM_CHECKSUM_LIST_MAX_TOKENS - 1));
	CHECK(!VM_ChecksumListContains(longList, 42));
}

int main(void) {
	TestProviderValues();
	TestProviderAcceptance();
	TestChecksumList();
	if (failures) {
		fprintf(stderr, "%d module provider policy check(s) failed\n", failures);
		return 1;
	}
	puts("module provider policy: all checks passed");
	return 0;
}
