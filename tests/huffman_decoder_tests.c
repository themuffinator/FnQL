/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL and is distributed under the terms of the GNU
General Public License version 2 or (at your option) any later version.
===========================================================================
*/

#include "q_shared.h"
#include "qcommon.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define GUARD_SIZE 16

static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
		failures++; \
	} \
} while (0)

static msg_t MakeMessage(byte *data, int maxsize, int cursize) {
	msg_t msg;

	memset(&msg, 0, sizeof(msg));
	msg.data = data;
	msg.maxsize = maxsize;
	msg.maxbits = maxsize * 8;
	msg.cursize = cursize;
	return msg;
}

static void CheckGuard(const byte *guard) {
	int i;

	for (i = 0; i < GUARD_SIZE; i++) {
		CHECK(guard[i] == 0xa5);
	}
}

static void TestFrozenAbcFixture(void) {
	static const byte compressed[] = { 0x00, 0x03, 0x86, 0x8c, 0x30, 0x06 };
	byte storage[64 + GUARD_SIZE];
	msg_t msg;

	memset(storage, 0xcc, sizeof(storage));
	memset(storage + 64, 0xa5, GUARD_SIZE);
	memcpy(storage, compressed, sizeof(compressed));
	msg = MakeMessage(storage, 64, (int)sizeof(compressed));

	CHECK(Huff_Decompress(&msg, 0) == qtrue);
	CHECK(msg.cursize == 3);
	CHECK(memcmp(storage, "abc", 3) == 0);
	CheckGuard(storage + 64);

	memcpy(storage, "abc", 3);
	msg = MakeMessage(storage, 64, 3);
	Huff_Compress(&msg, 0);
	CHECK(msg.cursize == (int)sizeof(compressed));
	CHECK(memcmp(storage, compressed, sizeof(compressed)) == 0);
	CheckGuard(storage + 64);
}

static void TestFrozenConnectFixture(void) {
	static const byte compressed[] = {
		0xff, 0xff, 0xff, 0xff, 0x63, 0x6f, 0x6e, 0x6e,
		0x65, 0x63, 0x74, 0x20, 0x00, 0x28, 0x44, 0x74,
		0x70, 0x88, 0x13, 0xec, 0xc7, 0xa5, 0x61, 0x2c,
		0xd8, 0xe8, 0x70, 0x1a, 0xc6, 0x05, 0xc7, 0xf9,
		0xc4, 0xdc, 0x30, 0x15, 0xcc, 0x09, 0xcb, 0x52,
		0x60, 0x61, 0x18, 0x26, 0x51, 0x4c, 0x11, 0x3b,
		0xc2, 0x9c, 0x3e, 0x14, 0x51, 0x14
	};
	static const byte expected[] =
		"\xff\xff\xff\xff" "connect \"\\protocol\\91\\qport\\1234\\challenge\\1234\"";
	byte storage[128 + GUARD_SIZE];
	msg_t msg;

	memset(storage, 0xcc, sizeof(storage));
	memset(storage + 128, 0xa5, GUARD_SIZE);
	memcpy(storage, compressed, sizeof(compressed));
	msg = MakeMessage(storage, 128, (int)sizeof(compressed));

	CHECK(Huff_Decompress(&msg, 12) == qtrue);
	CHECK(msg.cursize == (int)sizeof(expected) - 1);
	CHECK(memcmp(storage, expected, sizeof(expected) - 1) == 0);
	CheckGuard(storage + 128);
}

static void TestTruncatedStreamsAreAtomic(void) {
	static const byte headerOnly[] = { 0x00, 0x01 };
	static const byte partialSecondSymbol[] = { 0x00, 0x02, 0x86 };
	byte storage[32 + GUARD_SIZE];
	byte before[32];
	msg_t msg;

	memset(storage, 0xcc, sizeof(storage));
	memset(storage + 32, 0xa5, GUARD_SIZE);
	memcpy(storage, headerOnly, sizeof(headerOnly));
	memcpy(before, storage, sizeof(before));
	msg = MakeMessage(storage, 32, (int)sizeof(headerOnly));
	CHECK(Huff_Decompress(&msg, 0) == qfalse);
	CHECK(msg.cursize == (int)sizeof(headerOnly));
	CHECK(memcmp(storage, before, sizeof(before)) == 0);

	memcpy(storage, partialSecondSymbol, sizeof(partialSecondSymbol));
	memcpy(before, storage, sizeof(before));
	msg = MakeMessage(storage, 32, (int)sizeof(partialSecondSymbol));
	CHECK(Huff_Decompress(&msg, 0) == qfalse);
	CHECK(msg.cursize == (int)sizeof(partialSecondSymbol));
	CHECK(memcmp(storage, before, sizeof(before)) == 0);
	CheckGuard(storage + 32);
}

static void TestRetailOutputClamp(void) {
	byte storage[128 + GUARD_SIZE];
	msg_t msg;
	int compressedSize;

	memset(storage, 'a', 64);
	memset(storage + 64, 0xcc, 64);
	memset(storage + 128, 0xa5, GUARD_SIZE);
	msg = MakeMessage(storage, 128, 64);
	Huff_Compress(&msg, 0);
	compressedSize = msg.cursize;
	CHECK(compressedSize <= 32);

	msg.maxsize = 32;
	msg.maxbits = 32 * 8;
	msg.cursize = compressedSize;
	CHECK(Huff_Decompress(&msg, 0) == qtrue);
	CHECK(msg.cursize == 32);
	CHECK(memcmp(storage, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 32) == 0);
	CheckGuard(storage + 128);
}

static void TestInvalidMessageState(void) {
	byte storage[8] = { 0, 0 };
	msg_t msg = MakeMessage(storage, 8, 2);

	CHECK(Huff_Decompress(NULL, 0) == qfalse);
	msg.data = NULL;
	CHECK(Huff_Decompress(&msg, 0) == qfalse);
	msg.data = storage;
	CHECK(Huff_Decompress(&msg, -1) == qfalse);
	msg.cursize = 9;
	CHECK(Huff_Decompress(&msg, 0) == qfalse);
	msg.cursize = 1;
	CHECK(Huff_Decompress(&msg, 2) == qfalse);
	msg = MakeMessage(storage, INT_MAX, INT_MAX);
	CHECK(Huff_Decompress(&msg, 0) == qfalse);

	msg = MakeMessage(storage, 8, 2);
	CHECK(Huff_Decompress(&msg, 0) == qtrue);
	CHECK(msg.cursize == 0);
}

int main(void) {
	TestFrozenAbcFixture();
	TestFrozenConnectFixture();
	TestTruncatedStreamsAreAtomic();
	TestRetailOutputClamp();
	TestInvalidMessageState();

	if (failures != 0) {
		fprintf(stderr, "%d Huffman decoder test(s) failed\n", failures);
		return 1;
	}

	puts("Huffman decoder tests passed");
	return 0;
}
