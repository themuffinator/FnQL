/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL.

FnQL is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.
===========================================================================
*/

/*
 * Behavioral and bounds-safety gate for the shared BMP loader.  Fixtures are
 * assembled in memory so the tests cover the BMP byte contract without
 * relying on host image libraries or checked-in binary assets.
 */

#include "../code/qcommon/q_shared.h"
#include "../code/renderercommon/tr_public.h"

#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void R_LoadBMP( const char *name, byte **pic, int *width, int *height );

/* ------------------------------------------------------------ harness */

#define FIXTURE_CAPACITY 2048
#define GUARD_SIZE 32
#define FILE_GUARD_VALUE 0xd3
#define OUTPUT_GUARD_VALUE 0xa7

typedef struct {
	byte data[FIXTURE_CAPACITY];
	size_t length;
} bmpFixture_t;

typedef struct {
	qboolean errored;
	byte *pic;
	int width;
	int height;
} loadResult_t;

static int failures;
static const char *currentTest;

static const bmpFixture_t *readFixture;
static byte *activeFileStorage;
static byte *activeFileContent;
static size_t activeFileLength;

static byte *activeOutputStorage;
static byte *activeOutputContent;
static size_t activeOutputLength;

static jmp_buf errorJump;
static qboolean errorArmed;
static qboolean errorRaised;
static char errorMessage[256];
static byte *loadedPic;
static int loadedWidth;
static int loadedHeight;

refimport_t ri;

static void Check( int condition, const char *expression, int line )
{
	if ( condition ) {
		return;
	}
	fprintf( stderr, "%s: line %d: check failed: %s\n",
		currentTest ? currentTest : "bmp_loader_tests", line, expression );
	++failures;
}

#define CHECK( expression ) Check( ( expression ) ? 1 : 0, #expression, __LINE__ )

static void CheckGuard( const byte *guard, int value )
{
	size_t i;

	for ( i = 0; i < GUARD_SIZE; ++i ) {
		CHECK( guard[i] == (byte)value );
	}
}

static void ReleaseActiveFile( void )
{
	if ( !activeFileStorage ) {
		return;
	}

	CheckGuard( activeFileStorage, FILE_GUARD_VALUE );
	CheckGuard( activeFileContent + activeFileLength, FILE_GUARD_VALUE );
	free( activeFileStorage );
	activeFileStorage = NULL;
	activeFileContent = NULL;
	activeFileLength = 0;
}

static int StubFSReadFile( const char *name, void **buffer )
{
	size_t allocationSize;

	(void)name;
	CHECK( readFixture != NULL );
	CHECK( buffer != NULL );
	CHECK( activeFileStorage == NULL );
	if ( !readFixture || !buffer || activeFileStorage ) {
		return -1;
	}

	allocationSize = GUARD_SIZE + readFixture->length + GUARD_SIZE;
	activeFileStorage = (byte *)malloc( allocationSize );
	if ( !activeFileStorage ) {
		fprintf( stderr, "unable to allocate BMP fixture\n" );
		exit( EXIT_FAILURE );
	}
	memset( activeFileStorage, FILE_GUARD_VALUE, allocationSize );
	activeFileContent = activeFileStorage + GUARD_SIZE;
	activeFileLength = readFixture->length;
	memcpy( activeFileContent, readFixture->data, readFixture->length );
	*buffer = activeFileContent;
	return (int)readFixture->length;
}

static void StubFSFreeFile( void *buffer )
{
	CHECK( buffer == activeFileContent );
	ReleaseActiveFile();
}

static void *StubMalloc( int bytes )
{
	size_t allocationSize;

	CHECK( bytes > 0 );
	CHECK( activeOutputStorage == NULL );
	if ( bytes <= 0 || activeOutputStorage ) {
		return NULL;
	}

	activeOutputLength = (size_t)bytes;
	allocationSize = GUARD_SIZE + activeOutputLength + GUARD_SIZE;
	activeOutputStorage = (byte *)malloc( allocationSize );
	if ( !activeOutputStorage ) {
		fprintf( stderr, "unable to allocate decoded BMP\n" );
		exit( EXIT_FAILURE );
	}
	memset( activeOutputStorage, OUTPUT_GUARD_VALUE, allocationSize );
	activeOutputContent = activeOutputStorage + GUARD_SIZE;
	memset( activeOutputContent, 0xcc, activeOutputLength );
	return activeOutputContent;
}

static void StubFree( void *buffer )
{
	CHECK( buffer == activeOutputContent );
	if ( !activeOutputStorage ) {
		return;
	}

	CheckGuard( activeOutputStorage, OUTPUT_GUARD_VALUE );
	CheckGuard( activeOutputContent + activeOutputLength, OUTPUT_GUARD_VALUE );
	free( activeOutputStorage );
	activeOutputStorage = NULL;
	activeOutputContent = NULL;
	activeOutputLength = 0;
}

static void NORETURN QDECL StubError( errorParm_t errorLevel, const char *format, ... )
{
	va_list args;

	(void)errorLevel;
	va_start( args, format );
	vsnprintf( errorMessage, sizeof( errorMessage ), format, args );
	va_end( args );
	errorRaised = qtrue;
	if ( errorArmed ) {
		longjmp( errorJump, 1 );
	}

	fprintf( stderr, "untrapped renderer error: %s\n", errorMessage );
	abort();
}

static loadResult_t LoadFixture( const bmpFixture_t *fixture )
{
	loadResult_t result;

	CHECK( activeFileStorage == NULL );
	CHECK( activeOutputStorage == NULL );
	readFixture = fixture;
	loadedPic = NULL;
	loadedWidth = -1;
	loadedHeight = -1;
	errorRaised = qfalse;
	errorMessage[0] = '\0';
	errorArmed = qtrue;
	if ( setjmp( errorJump ) == 0 ) {
		R_LoadBMP( "memory.bmp", &loadedPic, &loadedWidth, &loadedHeight );
	}
	errorArmed = qfalse;

	/* Defensively isolate later cases if any error path leaves the file active. */
	ReleaseActiveFile();

	result.errored = errorRaised;
	result.pic = loadedPic;
	result.width = loadedWidth;
	result.height = loadedHeight;
	return result;
}

static void FreeResult( loadResult_t *result )
{
	if ( result->pic ) {
		StubFree( result->pic );
	} else if ( activeOutputStorage ) {
		/* Keep later cases isolated if an error occurred after allocation. */
		StubFree( activeOutputContent );
	}
	result->pic = NULL;
}

static loadResult_t ExpectSuccess( const bmpFixture_t *fixture, int width, int height )
{
	loadResult_t result = LoadFixture( fixture );

	CHECK( result.errored == qfalse );
	CHECK( result.pic != NULL );
	CHECK( result.width == width );
	CHECK( result.height == height );
	CHECK( activeOutputLength == (size_t)width * (size_t)height * 4 );
	return result;
}

static void ExpectError( const bmpFixture_t *fixture )
{
	loadResult_t result = LoadFixture( fixture );

	CHECK( result.errored == qtrue );
	CHECK( errorMessage[0] != '\0' );
	CHECK( result.pic == NULL );
	CHECK( result.width == 0 );
	CHECK( result.height == 0 );
	FreeResult( &result );
}

/* ----------------------------------------------------------- fixtures */

static void PutU16( byte *destination, uint16_t value )
{
	destination[0] = (byte)value;
	destination[1] = (byte)( value >> 8 );
}

static void PutU32( byte *destination, uint32_t value )
{
	destination[0] = (byte)value;
	destination[1] = (byte)( value >> 8 );
	destination[2] = (byte)( value >> 16 );
	destination[3] = (byte)( value >> 24 );
}

static size_t PixelOffsetForBits( uint16_t bitsPerPixel )
{
	return bitsPerPixel == 8 ? 14 + 40 + 256 * 4 : 14 + 40;
}

static void BeginFixture( bmpFixture_t *fixture, int32_t width, int32_t height,
	uint16_t bitsPerPixel, size_t payloadBytes )
{
	const size_t pixelOffset = PixelOffsetForBits( bitsPerPixel );

	CHECK( pixelOffset + payloadBytes <= sizeof( fixture->data ) );
	memset( fixture, 0, sizeof( *fixture ) );
	fixture->length = pixelOffset + payloadBytes;
	fixture->data[0] = 'B';
	fixture->data[1] = 'M';
	PutU32( fixture->data + 2, (uint32_t)fixture->length );
	PutU32( fixture->data + 10, (uint32_t)pixelOffset );
	PutU32( fixture->data + 14, 40 );
	PutU32( fixture->data + 18, (uint32_t)width );
	PutU32( fixture->data + 22, (uint32_t)height );
	PutU16( fixture->data + 26, 1 );
	PutU16( fixture->data + 28, bitsPerPixel );
	PutU32( fixture->data + 34, (uint32_t)payloadBytes );
	if ( bitsPerPixel == 8 ) {
		PutU32( fixture->data + 46, 256 );
	}
}

static byte *FixturePixels( bmpFixture_t *fixture )
{
	uint32_t offset = (uint32_t)fixture->data[10]
		| ( (uint32_t)fixture->data[11] << 8 )
		| ( (uint32_t)fixture->data[12] << 16 )
		| ( (uint32_t)fixture->data[13] << 24 );

	CHECK( offset < sizeof( fixture->data ) );
	return fixture->data + offset;
}

static void CheckPixels( const loadResult_t *result, const byte *expected,
	size_t expectedLength )
{
	CHECK( result->pic != NULL );
	CHECK( activeOutputLength == expectedLength );
	if ( result->pic && activeOutputLength == expectedLength ) {
		CHECK( memcmp( result->pic, expected, expectedLength ) == 0 );
	}
	if ( activeOutputStorage ) {
		CheckGuard( activeOutputStorage, OUTPUT_GUARD_VALUE );
		CheckGuard( activeOutputContent + activeOutputLength, OUTPUT_GUARD_VALUE );
	}
}

/* --------------------------------------------------------------- tests */

static void TestBottomUpRowsHonorPadding( void )
{
	static const byte expected[] = {
		0x44, 0x55, 0x66, 0xff,
		0x11, 0x22, 0x33, 0xff
	};
	bmpFixture_t fixture;
	loadResult_t result;
	byte *pixels;

	currentTest = "24-bit bottom-up row padding";
	BeginFixture( &fixture, 1, 2, 24, 8 );
	pixels = FixturePixels( &fixture );
	pixels[0] = 0x33; pixels[1] = 0x22; pixels[2] = 0x11; pixels[3] = 0xee;
	pixels[4] = 0x66; pixels[5] = 0x55; pixels[6] = 0x44; pixels[7] = 0xdd;

	result = ExpectSuccess( &fixture, 1, 2 );
	CheckPixels( &result, expected, sizeof( expected ) );
	FreeResult( &result );
}

static void TestTopDownRowsKeepFileOrder( void )
{
	static const byte expected[] = {
		0x10, 0x20, 0x30, 0xff,
		0x40, 0x50, 0x60, 0xff
	};
	bmpFixture_t fixture;
	loadResult_t result;
	byte *pixels;

	currentTest = "negative-height top-down BMP";
	BeginFixture( &fixture, 1, -2, 24, 8 );
	pixels = FixturePixels( &fixture );
	pixels[0] = 0x30; pixels[1] = 0x20; pixels[2] = 0x10; pixels[3] = 0xee;
	pixels[4] = 0x60; pixels[5] = 0x50; pixels[6] = 0x40; pixels[7] = 0xdd;

	result = ExpectSuccess( &fixture, 1, 2 );
	CheckPixels( &result, expected, sizeof( expected ) );
	FreeResult( &result );
}

static void TestLegacyTightlyPackedRowsRemainSupported( void )
{
	static const byte expected[] = {
		0x44, 0x55, 0x66, 0xff,
		0x11, 0x22, 0x33, 0xff
	};
	bmpFixture_t fixture;
	loadResult_t result;
	byte *pixels;

	currentTest = "legacy tightly-packed 24-bit rows";
	BeginFixture( &fixture, 1, 2, 24, 6 );
	pixels = FixturePixels( &fixture );
	pixels[0] = 0x33; pixels[1] = 0x22; pixels[2] = 0x11;
	pixels[3] = 0x66; pixels[4] = 0x55; pixels[5] = 0x44;

	result = ExpectSuccess( &fixture, 1, 2 );
	CheckPixels( &result, expected, sizeof( expected ) );
	FreeResult( &result );
}

static void Test16BitGoldenPixelsStayInBounds( void )
{
	static const byte expected[] = {
		0xf8, 0x00, 0x00, 0xff,
		0x00, 0xf8, 0x00, 0xff
	};
	bmpFixture_t fixture;
	loadResult_t result;
	byte *pixels;

	currentTest = "16-bit golden pixels and canaries";
	BeginFixture( &fixture, 2, 1, 16, 4 );
	pixels = FixturePixels( &fixture );
	PutU16( pixels, 0x7c00 );
	PutU16( pixels + 2, 0x03e0 );

	result = ExpectSuccess( &fixture, 2, 1 );
	CheckPixels( &result, expected, sizeof( expected ) );
	FreeResult( &result );
}

static void Test32BitAlphaIsPreserved( void )
{
	static const byte expected[] = {
		0x30, 0x20, 0x10, 0x40,
		0x70, 0x60, 0x50, 0x80
	};
	bmpFixture_t fixture;
	loadResult_t result;
	byte *pixels;

	currentTest = "32-bit alpha";
	BeginFixture( &fixture, 2, 1, 32, 8 );
	pixels = FixturePixels( &fixture );
	pixels[0] = 0x10; pixels[1] = 0x20; pixels[2] = 0x30; pixels[3] = 0x40;
	pixels[4] = 0x50; pixels[5] = 0x60; pixels[6] = 0x70; pixels[7] = 0x80;

	result = ExpectSuccess( &fixture, 2, 1 );
	CheckPixels( &result, expected, sizeof( expected ) );
	FreeResult( &result );
}

static void TestStandard256EntryPalette( void )
{
	static const byte expected[] = {
		0x33, 0x22, 0x11, 0xff,
		0xcc, 0xbb, 0xaa, 0xff
	};
	bmpFixture_t fixture;
	loadResult_t result;
	byte *palette;
	byte *pixels;

	currentTest = "standard 8-bit 256-entry palette";
	BeginFixture( &fixture, 2, 1, 8, 4 );
	palette = fixture.data + 54;
	palette[5 * 4 + 0] = 0x11;
	palette[5 * 4 + 1] = 0x22;
	palette[5 * 4 + 2] = 0x33;
	palette[200 * 4 + 0] = 0xaa;
	palette[200 * 4 + 1] = 0xbb;
	palette[200 * 4 + 2] = 0xcc;
	pixels = FixturePixels( &fixture );
	pixels[0] = 5;
	pixels[1] = 200;
	pixels[2] = 0xee;
	pixels[3] = 0xdd;

	result = ExpectSuccess( &fixture, 2, 1 );
	CheckPixels( &result, expected, sizeof( expected ) );
	FreeResult( &result );
}

static void TestMalformedSignaturesAreRejected( void )
{
	bmpFixture_t fixture;

	currentTest = "malformed BMP signatures";
	BeginFixture( &fixture, 1, 1, 24, 4 );
	fixture.data[0] = 'B';
	fixture.data[1] = 'X';
	ExpectError( &fixture );

	BeginFixture( &fixture, 1, 1, 24, 4 );
	fixture.data[0] = 'Z';
	fixture.data[1] = 'M';
	ExpectError( &fixture );
}

static void TestHostileDimensionsAndOffsetsAreRejected( void )
{
	bmpFixture_t fixture;

	currentTest = "UINT32_MAX pixel offset";
	BeginFixture( &fixture, 1, 1, 24, 4 );
	PutU32( fixture.data + 10, UINT32_MAX );
	ExpectError( &fixture );

	currentTest = "INT32_MIN height";
	BeginFixture( &fixture, 1, INT32_MIN, 24, 0 );
	ExpectError( &fixture );

	currentTest = "huge BMP dimensions";
	BeginFixture( &fixture, INT32_MAX, INT32_MAX, 32, 0 );
	ExpectError( &fixture );
}

static void TestTruncatedPaddedPayloadIsRejected( void )
{
	bmpFixture_t fixture;

	currentTest = "truncated padded payload";
	BeginFixture( &fixture, 1, 2, 24, 7 );
	/* Two 24-bit rows each occupy four bytes, even though only six are pixels. */
	PutU32( fixture.data + 34, 8 );
	ExpectError( &fixture );
}

static void TestStructuralHeaderErrorsAreRejected( void )
{
	bmpFixture_t fixture;

	currentTest = "planes other than one";
	BeginFixture( &fixture, 1, 1, 24, 4 );
	PutU16( fixture.data + 26, 2 );
	ExpectError( &fixture );

	currentTest = "DIB header too small";
	BeginFixture( &fixture, 1, 1, 24, 4 );
	PutU32( fixture.data + 14, 12 );
	ExpectError( &fixture );

	currentTest = "8-bit pixel offset before palette";
	BeginFixture( &fixture, 1, 1, 8, 4 );
	PutU32( fixture.data + 10, 54 );
	ExpectError( &fixture );
}

int main( void )
{
	memset( &ri, 0, sizeof( ri ) );
	ri.Error = StubError;
	ri.Malloc = StubMalloc;
	ri.Free = StubFree;
	ri.FS_ReadFile = StubFSReadFile;
	ri.FS_FreeFile = StubFSFreeFile;

	TestBottomUpRowsHonorPadding();
	TestTopDownRowsKeepFileOrder();
	TestLegacyTightlyPackedRowsRemainSupported();
	Test16BitGoldenPixelsStayInBounds();
	Test32BitAlphaIsPreserved();
	TestStandard256EntryPalette();
	TestMalformedSignaturesAreRejected();
	TestHostileDimensionsAndOffsetsAreRejected();
	TestTruncatedPaddedPayloadIsRejected();
	TestStructuralHeaderErrorsAreRejected();

	ReleaseActiveFile();
	if ( activeOutputStorage ) {
		StubFree( activeOutputContent );
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
