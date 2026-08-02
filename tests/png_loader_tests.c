/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL and is distributed under the terms of the GNU
General Public License version 2 or (at your option) any later version.
===========================================================================
*/

#include "../code/qcommon/q_shared.h"
#include "../code/renderercommon/tr_public.h"

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void R_LoadPNG(const char *name, byte **pic, int *width, int *height);

#define FIXTURE_CAPACITY 4096
#define MAX_ALLOCATIONS 16
#define GUARD_SIZE 32
#define GUARD_VALUE 0xa7

typedef struct {
	byte data[FIXTURE_CAPACITY];
	size_t length;
} pngFixture_t;

typedef struct {
	byte *storage;
	byte *content;
	size_t length;
} allocation_t;

typedef struct {
	byte *pic;
	int width;
	int height;
	int largestAllocation;
} loadResult_t;

static int failures;
static const char *currentTest;
static const pngFixture_t *readFixture;
static byte *fileStorage;
static byte *fileContent;
static size_t fileLength;
static allocation_t allocations[MAX_ALLOCATIONS];
static int largestAllocation;

refimport_t ri;

static void Check(int condition, const char *expression, int line) {
	if (!condition) {
		fprintf(stderr, "%s:%d: check failed: %s\n",
			currentTest ? currentTest : "png_loader_tests", line, expression);
		failures++;
	}
}

#define CHECK(condition) Check((condition) ? 1 : 0, #condition, __LINE__)

static void CheckGuard(const byte *guard) {
	size_t i;
	for (i = 0; i < GUARD_SIZE; i++) {
		CHECK(guard[i] == GUARD_VALUE);
	}
}

static int ActiveAllocations(void) {
	int count = 0;
	int i;
	for (i = 0; i < MAX_ALLOCATIONS; i++) {
		if (allocations[i].storage) {
			count++;
		}
	}
	return count;
}

static void *StubMalloc(int bytes) {
	allocation_t *allocation = NULL;
	int i;

	CHECK(bytes > 0);
	/* Any larger request means a hostile layout reached the renderer allocator. */
	CHECK(bytes <= 65536);
	if (bytes <= 0 || bytes > 65536) {
		return NULL;
	}
	for (i = 0; i < MAX_ALLOCATIONS; i++) {
		if (!allocations[i].storage) {
			allocation = &allocations[i];
			break;
		}
	}
	CHECK(allocation != NULL);
	if (!allocation) {
		return NULL;
	}

	allocation->length = (size_t)bytes;
	allocation->storage = (byte *)malloc(GUARD_SIZE + allocation->length + GUARD_SIZE);
	CHECK(allocation->storage != NULL);
	if (!allocation->storage) {
		allocation->length = 0;
		return NULL;
	}
	memset(allocation->storage, GUARD_VALUE,
		GUARD_SIZE + allocation->length + GUARD_SIZE);
	allocation->content = allocation->storage + GUARD_SIZE;
	memset(allocation->content, 0xcc, allocation->length);
	if (bytes > largestAllocation) {
		largestAllocation = bytes;
	}
	return allocation->content;
}

static void StubFree(void *buffer) {
	int i;
	for (i = 0; i < MAX_ALLOCATIONS; i++) {
		allocation_t *allocation = &allocations[i];
		if (allocation->content != buffer) {
			continue;
		}
		CheckGuard(allocation->storage);
		CheckGuard(allocation->content + allocation->length);
		free(allocation->storage);
		memset(allocation, 0, sizeof(*allocation));
		return;
	}
	CHECK(0 && "free of unknown renderer allocation");
}

static int StubFSReadFile(const char *name, void **buffer) {
	(void)name;
	CHECK(readFixture != NULL);
	CHECK(buffer != NULL);
	CHECK(fileStorage == NULL);
	if (!readFixture || !buffer || fileStorage) {
		return -1;
	}

	fileLength = readFixture->length;
	fileStorage = (byte *)malloc(GUARD_SIZE + fileLength + GUARD_SIZE);
	CHECK(fileStorage != NULL);
	if (!fileStorage) {
		return -1;
	}
	memset(fileStorage, GUARD_VALUE, GUARD_SIZE + fileLength + GUARD_SIZE);
	fileContent = fileStorage + GUARD_SIZE;
	memcpy(fileContent, readFixture->data, fileLength);
	*buffer = fileContent;
	return (int)fileLength;
}

static void StubFSFreeFile(void *buffer) {
	CHECK(buffer == fileContent);
	if (!fileStorage) {
		return;
	}
	CheckGuard(fileStorage);
	CheckGuard(fileContent + fileLength);
	free(fileStorage);
	fileStorage = NULL;
	fileContent = NULL;
	fileLength = 0;
}

static void QDECL StubPrintf(printParm_t level, const char *format, ...) {
	(void)level;
	(void)format;
}

/* BigLong expands to this on the little-endian x86 target. */
int LongSwap(int value) {
	uint32_t input = (uint32_t)value;
	uint32_t output = ((input & 0x000000ffU) << 24) |
		((input & 0x0000ff00U) << 8) |
		((input & 0x00ff0000U) >> 8) |
		((input & 0xff000000U) >> 24);
	int result;
	memcpy(&result, &output, sizeof(result));
	return result;
}

static void PutBE32(byte *destination, uint32_t value) {
	destination[0] = (byte)(value >> 24);
	destination[1] = (byte)(value >> 16);
	destination[2] = (byte)(value >> 8);
	destination[3] = (byte)value;
}

static void AppendBytes(pngFixture_t *fixture, const void *data, size_t length) {
	CHECK(fixture->length + length <= sizeof(fixture->data));
	if (fixture->length + length > sizeof(fixture->data)) {
		return;
	}
	memcpy(fixture->data + fixture->length, data, length);
	fixture->length += length;
}

static void AppendChunkHeader(pngFixture_t *fixture, const char type[4], uint32_t length) {
	byte header[8];
	PutBE32(header, length);
	memcpy(header + 4, type, 4);
	AppendBytes(fixture, header, sizeof(header));
}

static void AppendChunk(pngFixture_t *fixture, const char type[4],
	const byte *data, uint32_t length) {
	static const byte ignoredCRC[4] = { 0, 0, 0, 0 };
	AppendChunkHeader(fixture, type, length);
	if (length) {
		AppendBytes(fixture, data, length);
	}
	AppendBytes(fixture, ignoredCRC, sizeof(ignoredCRC));
}

static void BeginPNG(pngFixture_t *fixture, uint32_t width, uint32_t height,
	byte bitDepth, byte colourType, byte interlace) {
	static const byte signature[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
	byte ihdr[13];

	memset(fixture, 0, sizeof(*fixture));
	AppendBytes(fixture, signature, sizeof(signature));
	PutBE32(ihdr, width);
	PutBE32(ihdr + 4, height);
	ihdr[8] = bitDepth;
	ihdr[9] = colourType;
	ihdr[10] = 0;
	ihdr[11] = 0;
	ihdr[12] = interlace;
	AppendChunk(fixture, "IHDR", ihdr, sizeof(ihdr));
}

static size_t MakeStoredZlib(byte *output, const byte *raw, uint16_t rawLength) {
	uint16_t complement = (uint16_t)~rawLength;
	size_t position = 0;

	output[position++] = 0x78;
	output[position++] = 0x01;
	output[position++] = 0x01;
	output[position++] = (byte)rawLength;
	output[position++] = (byte)(rawLength >> 8);
	output[position++] = (byte)complement;
	output[position++] = (byte)(complement >> 8);
	memcpy(output + position, raw, rawLength);
	position += rawLength;
	/* The inherited loader does not validate Adler-32; framing is still required. */
	memset(output + position, 0, 4);
	return position + 4;
}

static void AppendStoredIDAT(pngFixture_t *fixture, const byte *raw, uint16_t rawLength) {
	byte zlib[128];
	size_t zlibLength = MakeStoredZlib(zlib, raw, rawLength);
	AppendChunk(fixture, "IDAT", zlib, (uint32_t)zlibLength);
}

static void FinishPNG(pngFixture_t *fixture) {
	AppendChunk(fixture, "IEND", NULL, 0);
}

static loadResult_t LoadFixture(const pngFixture_t *fixture) {
	loadResult_t result;

	CHECK(ActiveAllocations() == 0);
	CHECK(fileStorage == NULL);
	readFixture = fixture;
	largestAllocation = 0;
	result.pic = NULL;
	result.width = -1;
	result.height = -1;
	R_LoadPNG("memory.png", &result.pic, &result.width, &result.height);
	result.largestAllocation = largestAllocation;
	CHECK(fileStorage == NULL);
	return result;
}

static void ExpectRejected(const pngFixture_t *fixture) {
	loadResult_t result = LoadFixture(fixture);
	CHECK(result.pic == NULL);
	CHECK(result.width == 0);
	CHECK(result.height == 0);
	CHECK(ActiveAllocations() == 0);
}

static void TestTinyRGBA(void) {
	static const byte raw[] = { 0, 0x11, 0x22, 0x33, 0x44 };
	static const byte expected[] = { 0x11, 0x22, 0x33, 0x44 };
	pngFixture_t fixture;
	loadResult_t result;

	currentTest = "tiny non-interlaced RGBA";
	BeginPNG(&fixture, 1, 1, 8, 6, 0);
	AppendStoredIDAT(&fixture, raw, sizeof(raw));
	FinishPNG(&fixture);
	result = LoadFixture(&fixture);
	CHECK(result.pic != NULL);
	CHECK(result.width == 1 && result.height == 1);
	if (result.pic) {
		CHECK(memcmp(result.pic, expected, sizeof(expected)) == 0);
		StubFree(result.pic);
	}
	CHECK(ActiveAllocations() == 0);
}

static void TestZeroLengthChunksConsumeCRC(void) {
	static const byte raw[] = { 0, 0x51, 0x62, 0x73, 0x84 };
	pngFixture_t fixture;
	loadResult_t result;

	currentTest = "zero-length ancillary and IDAT chunks";
	BeginPNG(&fixture, 1, 1, 8, 6, 0);
	AppendChunk(&fixture, "aaAA", NULL, 0);
	AppendChunk(&fixture, "IDAT", NULL, 0);
	AppendStoredIDAT(&fixture, raw, sizeof(raw));
	FinishPNG(&fixture);
	result = LoadFixture(&fixture);
	CHECK(result.pic != NULL);
	CHECK(result.width == 1 && result.height == 1);
	if (result.pic) {
		StubFree(result.pic);
	}
	CHECK(ActiveAllocations() == 0);
}

static void TestTinyAdam7RGBA(void) {
	static const byte raw[] = { 0, 0x10, 0x20, 0x30, 0x40 };
	pngFixture_t fixture;
	loadResult_t result;

	currentTest = "tiny Adam7 RGBA";
	BeginPNG(&fixture, 1, 1, 8, 6, 1);
	AppendStoredIDAT(&fixture, raw, sizeof(raw));
	FinishPNG(&fixture);
	result = LoadFixture(&fixture);
	CHECK(result.pic != NULL);
	CHECK(result.width == 1 && result.height == 1);
	if (result.pic) {
		CHECK(result.pic[0] == 0x10 && result.pic[3] == 0x40);
		StubFree(result.pic);
	}
	CHECK(ActiveAllocations() == 0);
}

static void TestOversizedPalette(void) {
	byte palette[771];
	pngFixture_t fixture;

	currentTest = "oversized PLTE";
	memset(palette, 0x5a, sizeof(palette));
	BeginPNG(&fixture, 1, 1, 8, 3, 0);
	AppendChunk(&fixture, "PLTE", palette, (uint32_t)sizeof(palette));
	FinishPNG(&fixture);
	ExpectRejected(&fixture);
}

static void TestWrappedChunkLength(void) {
	static const byte ignoredCRC[4] = { 0, 0, 0, 0 };
	pngFixture_t fixture;

	currentTest = "wrapped ancillary chunk length";
	BeginPNG(&fixture, 1, 1, 8, 6, 0);
	AppendChunkHeader(&fixture, "ruST", UINT32_MAX);
	AppendBytes(&fixture, ignoredCRC, sizeof(ignoredCRC));
	FinishPNG(&fixture);
	ExpectRejected(&fixture);
}

static void TestOversizedIDATLength(void) {
	pngFixture_t fixture;

	currentTest = "oversized IDAT aggregate";
	BeginPNG(&fixture, 1, 1, 8, 6, 0);
	AppendChunkHeader(&fixture, "IDAT", UINT32_MAX);
	FinishPNG(&fixture);
	ExpectRejected(&fixture);
}

static void TestShortZlibFraming(void) {
	static const byte shortStream[5] = { 0x78, 0x01, 0, 0, 0 };
	pngFixture_t fixture;

	currentTest = "short zlib wrapper";
	BeginPNG(&fixture, 1, 1, 8, 6, 0);
	AppendChunk(&fixture, "IDAT", shortStream, sizeof(shortStream));
	FinishPNG(&fixture);
	ExpectRejected(&fixture);
}

static void TestInflateCannotExceedIHDRLayout(void) {
	static const byte tooMuchRaw[] = { 0, 1, 2, 3, 4, 5 };
	pngFixture_t fixture;

	currentTest = "inflate output exceeds IHDR layout";
	BeginPNG(&fixture, 1, 1, 8, 6, 0);
	AppendStoredIDAT(&fixture, tooMuchRaw, sizeof(tooMuchRaw));
	FinishPNG(&fixture);
	ExpectRejected(&fixture);
}

static void TestOversizedScanlines(void) {
	pngFixture_t fixture;

	currentTest = "oversized non-interlaced scanline";
	BeginPNG(&fixture, (uint32_t)INT_MAX / 4, 1, 16, 6, 0);
	FinishPNG(&fixture);
	ExpectRejected(&fixture);

	currentTest = "oversized Adam7 scanlines";
	BeginPNG(&fixture, (uint32_t)INT_MAX / 4, 1, 16, 6, 1);
	FinishPNG(&fixture);
	ExpectRejected(&fixture);
}

int main(void) {
	memset(&ri, 0, sizeof(ri));
	ri.Printf = StubPrintf;
	ri.Malloc = StubMalloc;
	ri.Free = StubFree;
	ri.FS_ReadFile = StubFSReadFile;
	ri.FS_FreeFile = StubFSFreeFile;

	TestTinyRGBA();
	TestZeroLengthChunksConsumeCRC();
	TestTinyAdam7RGBA();
	TestOversizedPalette();
	TestWrappedChunkLength();
	TestOversizedIDATLength();
	TestShortZlibFraming();
	TestInflateCannotExceedIHDRLayout();
	TestOversizedScanlines();

	CHECK(fileStorage == NULL);
	CHECK(ActiveAllocations() == 0);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
