/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL and is distributed under the terms of the GNU
General Public License version 2 or (at your option) any later version.
===========================================================================
*/

#include "../code/qcommon/filesystem_write_bounds.h"

#include <stdio.h>

static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
		failures++; \
	} \
} while (0)

static void TestContainedQpaths(void) {
	CHECK(FS_WriteQpathIsValid("screenshots/fnql.png") == qtrue);
	CHECK(FS_WriteQpathIsValid("baseq3\\downloads\\pak.tmp") == qtrue);
	CHECK(FS_WriteQpathIsValid("configs/file..cfg") == qtrue);
	CHECK(FS_WriteQpathIsValid(".hidden/profile") == qtrue);
}

static void TestEscapingQpaths(void) {
	CHECK(FS_WriteQpathIsValid(NULL) == qfalse);
	CHECK(FS_WriteQpathIsValid("") == qfalse);
	CHECK(FS_WriteQpathIsValid("/absolute") == qfalse);
	CHECK(FS_WriteQpathIsValid("\\absolute") == qfalse);
	CHECK(FS_WriteQpathIsValid("C:\\outside") == qfalse);
	CHECK(FS_WriteQpathIsValid("file.cfg:stream") == qfalse);
	CHECK(FS_WriteQpathIsValid("../outside") == qfalse);
	CHECK(FS_WriteQpathIsValid("safe/../outside") == qfalse);
	CHECK(FS_WriteQpathIsValid("safe\\..\\outside") == qfalse);
	CHECK(FS_WriteQpathIsValid("safe/.. /outside") == qfalse);
	CHECK(FS_WriteQpathIsValid("safe/.../outside") == qfalse);
	CHECK(FS_WriteQpathIsValid("safe/./outside") == qfalse);
}

static void TestWriteArguments(void) {
	static const char byteValue = 'x';

	CHECK(FS_WriteRequestIsValid(&byteValue, 1, 1) == qtrue);
	CHECK(FS_WriteRequestIsValid(NULL, 0, 1) == qtrue);
	CHECK(FS_WriteRequestIsValid(NULL, 1, 1) == qfalse);
	CHECK(FS_WriteRequestIsValid(&byteValue, -1, 1) == qfalse);
	CHECK(FS_WriteRequestIsValid(&byteValue, 1, FS_INVALID_HANDLE) == qfalse);
	CHECK(FS_WriteRequestIsValid(&byteValue, 1, MAX_FILE_HANDLES) == qfalse);
}

int main(void) {
	TestContainedQpaths();
	TestEscapingQpaths();
	TestWriteArguments();

	if (failures) {
		fprintf(stderr, "%d filesystem write-boundary test(s) failed\n", failures);
		return 1;
	}
	puts("Filesystem write-boundary tests passed");
	return 0;
}
