/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL and is distributed under the terms of the GNU
General Public License version 2 or (at your option) any later version.
===========================================================================
*/

#include "../code/qcommon/vm_header_bounds.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
		failures++; \
	} \
} while (0)

static int HeaderSize(int magic) {
	return magic == VM_MAGIC_VER2 ? (int)sizeof(vmHeader_t) :
		(int)(sizeof(vmHeader_t) - sizeof(int32_t));
}

static int InitValidHeader(vmHeader_t *header, int magic) {
	int fileSize;

	memset(header, 0, sizeof(*header));
	header->vmMagic = magic;
	header->instructionCount = 1;
	header->codeOffset = HeaderSize(magic);
	header->codeLength = 4;
	header->dataOffset = header->codeOffset + header->codeLength;
	header->dataLength = 4;
	header->litLength = 3;
	header->bssLength = 1024;
	header->jtrgLength = magic == VM_MAGIC_VER2 ? 4 : 0;
	fileSize = header->dataOffset + header->dataLength + header->litLength;
	if (magic == VM_MAGIC_VER2) {
		fileSize += header->jtrgLength;
	}
	return fileSize;
}

static vmHeaderValidationResult_t Validate(const vmHeader_t *header, int fileSize) {
	return VM_ValidateHeaderLayout(header, fileSize, HeaderSize(header->vmMagic));
}

static void TestValidVersionLayouts(void) {
	vmHeader_t header;
	int fileSize;

	fileSize = InitValidHeader(&header, VM_MAGIC);
	/* jtrgLength is not part of the version-1 header and must remain ignored. */
	header.jtrgLength = -1;
	CHECK(Validate(&header, fileSize) == VM_HEADER_VALID);

	fileSize = InitValidHeader(&header, VM_MAGIC_VER2);
	CHECK(Validate(&header, fileSize) == VM_HEADER_VALID);

	/* Jump targets are int32 entries; malformed trailing bytes are rejected. */
	header.jtrgLength = 3;
	fileSize--;
	CHECK(Validate(&header, fileSize) == VM_HEADER_BAD_JUMP_TARGET_LENGTH);
}

static void TestNegativeFields(void) {
	vmHeader_t header;
	int fileSize = InitValidHeader(&header, VM_MAGIC_VER2);

	header.codeOffset = -1;
	CHECK(Validate(&header, fileSize) == VM_HEADER_BAD_CODE_OFFSET);

	fileSize = InitValidHeader(&header, VM_MAGIC_VER2);
	header.codeLength = -1;
	CHECK(Validate(&header, fileSize) == VM_HEADER_BAD_CODE_LENGTH);

	fileSize = InitValidHeader(&header, VM_MAGIC_VER2);
	header.instructionCount = -1;
	CHECK(Validate(&header, fileSize) == VM_HEADER_BAD_INSTRUCTION_COUNT);

	fileSize = InitValidHeader(&header, VM_MAGIC_VER2);
	header.dataOffset = -1;
	CHECK(Validate(&header, fileSize) == VM_HEADER_BAD_DATA_OFFSET);

	fileSize = InitValidHeader(&header, VM_MAGIC_VER2);
	header.dataLength = -1;
	CHECK(Validate(&header, fileSize) == VM_HEADER_BAD_DATA_LENGTH);

	fileSize = InitValidHeader(&header, VM_MAGIC_VER2);
	header.litLength = -1;
	CHECK(Validate(&header, fileSize) == VM_HEADER_BAD_LITERAL_LENGTH);

	fileSize = InitValidHeader(&header, VM_MAGIC_VER2);
	header.jtrgLength = -1;
	CHECK(Validate(&header, fileSize) == VM_HEADER_BAD_JUMP_TARGET_LENGTH);

	fileSize = InitValidHeader(&header, VM_MAGIC_VER2);
	header.bssLength = -1;
	CHECK(Validate(&header, fileSize) == VM_HEADER_BAD_BSS_LENGTH);
}

static void TestSignedSumsCannotWrap(void) {
	vmHeader_t header;
	int fileSize;

	fileSize = InitValidHeader(&header, VM_MAGIC_VER2);
	header.codeLength = INT_MAX;
	CHECK(Validate(&header, fileSize) == VM_HEADER_BAD_CODE_LENGTH);

	fileSize = InitValidHeader(&header, VM_MAGIC_VER2);
	header.dataLength = INT_MAX;
	CHECK(Validate(&header, fileSize) == VM_HEADER_BAD_DATA_LENGTH);

	fileSize = InitValidHeader(&header, VM_MAGIC_VER2);
	header.litLength = INT_MAX;
	CHECK(Validate(&header, fileSize) == VM_HEADER_BAD_LITERAL_LENGTH);

	fileSize = InitValidHeader(&header, VM_MAGIC_VER2);
	header.jtrgLength = INT_MAX;
	CHECK(Validate(&header, fileSize) == VM_HEADER_BAD_JUMP_TARGET_LENGTH);
}

static void TestInstructionCountBounds(void) {
	vmHeader_t header;
	int fileSize = InitValidHeader(&header, VM_MAGIC_VER2);

	header.instructionCount = 0;
	CHECK(Validate(&header, fileSize) == VM_HEADER_BAD_INSTRUCTION_COUNT);

	fileSize = InitValidHeader(&header, VM_MAGIC_VER2);
	header.instructionCount = header.codeLength + 1;
	CHECK(Validate(&header, fileSize) == VM_HEADER_BAD_INSTRUCTION_COUNT);

	memset(&header, 0, sizeof(header));
	header.vmMagic = VM_MAGIC_VER2;
	header.codeOffset = HeaderSize(VM_MAGIC_VER2);
	header.codeLength = INT_MAX - header.codeOffset;
	header.instructionCount = INT_MAX / (int)sizeof(instruction_t) - 7;
	header.dataOffset = INT_MAX;
	CHECK(Validate(&header, INT_MAX) == VM_HEADER_BAD_INSTRUCTION_COUNT);
}

static void TestDataAggregateBounds(void) {
	vmHeader_t header;
	int fileSize = InitValidHeader(&header, VM_MAGIC_VER2);
	unsigned int initialized = (unsigned int)header.dataLength +
		(unsigned int)header.litLength;

	header.bssLength = (int)(VM_MAX_EXACT_DATA_LENGTH - initialized + 1U);
	CHECK(Validate(&header, fileSize) == VM_HEADER_DATA_IMAGE_TOO_LARGE);

	fileSize = InitValidHeader(&header, VM_MAGIC_VER2);
	header.bssLength = INT_MAX;
	CHECK(Validate(&header, fileSize) == VM_HEADER_DATA_IMAGE_TOO_LARGE);
}

static void TestTruncatedV2Header(void) {
	vmHeader_t header;
	int fileSize = InitValidHeader(&header, VM_MAGIC_VER2);

	(void)fileSize;
	CHECK(VM_ValidateHeaderLayout(&header, HeaderSize(VM_MAGIC_VER2) - 1,
		HeaderSize(VM_MAGIC_VER2)) != VM_HEADER_VALID);
	CHECK(VM_ValidateHeaderLayout(&header, -1,
		HeaderSize(VM_MAGIC_VER2)) == VM_HEADER_BAD_CODE_OFFSET);
}

int main(void) {
	TestValidVersionLayouts();
	TestNegativeFields();
	TestSignedSumsCannotWrap();
	TestInstructionCountBounds();
	TestDataAggregateBounds();
	TestTruncatedV2Header();

	if (failures) {
		fprintf(stderr, "%d QVM header test(s) failed\n", failures);
		return 1;
	}
	puts("QVM header bounds tests passed");
	return 0;
}
