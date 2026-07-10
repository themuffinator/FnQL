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

#ifndef FNQL_QCOMMON_QL_BSP_H
#define FNQL_QCOMMON_QL_BSP_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Quake Live keeps the Quake III v46 header prefix and appends one lump
 * descriptor for advertisements in IBSP v47.  Keep this disk contract in a
 * single engine-owned header so every renderer validates and decodes it in
 * exactly the same way.
 */
#define QL_BSP_ADVERTISEMENT_LUMP_INDEX 17
#define QL_BSP_EXTRA_LUMP_COUNT 1

typedef struct qlBspAdvertisementDisk_s {
	int32_t	cellId;
	float	normal[3];
	float	points[4][3];
	char	model[MAX_QPATH];
} qlBspAdvertisementDisk_t;

typedef enum qlBspLumpResult_e {
	QL_BSP_LUMP_OK,
	QL_BSP_LUMP_TRUNCATED_HEADER,
	QL_BSP_LUMP_INVALID_RANGE
} qlBspLumpResult_t;

static inline qboolean QLBSP_LumpRangeValid( const lump_t *lump, size_t fileSize ) {
	size_t offset;
	size_t length;

	if ( !lump || lump->fileofs < 0 || lump->filelen < 0 ) {
		return qfalse;
	}
	offset = (size_t)lump->fileofs;
	length = (size_t)lump->filelen;
	return offset <= fileSize && length <= fileSize - offset ? qtrue : qfalse;
}

static inline qboolean QLBSP_AdvertisementLumpShapeValid( const lump_t *lump ) {
	return lump && lump->filelen >= 0
		&& (size_t)lump->filelen % sizeof( qlBspAdvertisementDisk_t ) == 0
		? qtrue : qfalse;
}

static inline uint32_t QLBSP_ReadLE32( const void *value ) {
	const byte *bytes = (const byte *)value;
	return (uint32_t)bytes[0] |
		( (uint32_t)bytes[1] << 8 ) |
		( (uint32_t)bytes[2] << 16 ) |
		( (uint32_t)bytes[3] << 24 );
}

/*
 * Read the v47 extension without assuming alignment or performing signed
 * offset arithmetic.  The returned lump remains in host integer form.
 */
static inline qlBspLumpResult_t QLBSP_ReadAdvertisementLump( const void *fileData,
		size_t fileSize, lump_t *outLump ) {
	const byte *extra;
	uint32_t offset;
	uint32_t length;

	if ( outLump ) {
		outLump->fileofs = 0;
		outLump->filelen = 0;
	}
	if ( !fileData || !outLump || fileSize < sizeof( dheader_t ) + sizeof( lump_t ) ) {
		return QL_BSP_LUMP_TRUNCATED_HEADER;
	}

	extra = (const byte *)fileData + sizeof( dheader_t );
	offset = QLBSP_ReadLE32( extra + offsetof( lump_t, fileofs ) );
	length = QLBSP_ReadLE32( extra + offsetof( lump_t, filelen ) );

	/* dheader_t exposes signed fields, so reject values it cannot represent. */
	if ( offset > INT_MAX || length > INT_MAX ) {
		return QL_BSP_LUMP_INVALID_RANGE;
	}

	outLump->fileofs = (int32_t)offset;
	outLump->filelen = (int32_t)length;
	if ( !QLBSP_LumpRangeValid( outLump, fileSize ) ) {
		outLump->fileofs = 0;
		outLump->filelen = 0;
		return QL_BSP_LUMP_INVALID_RANGE;
	}
	return QL_BSP_LUMP_OK;
}

/*
 * Decode the fixed-width "*<index>" model field without relying on a NUL
 * outside the BSP record.  A full-width digit sequence is accepted because
 * the on-disk field is bounded even when an authoring tool omitted the NUL.
 */
static inline qboolean QLBSP_ParseAdvertisementModel( const char model[MAX_QPATH], int *outModelIndex ) {
	uint32_t value = 0;
	size_t i;

	if ( outModelIndex ) {
		*outModelIndex = -1;
	}
	if ( !model || !outModelIndex || model[0] != '*' ) {
		return qfalse;
	}

	for ( i = 1; i < MAX_QPATH && model[i] != '\0'; ++i ) {
		const unsigned int digit = (unsigned int)( (unsigned char)model[i] - (unsigned char)'0' );
		if ( digit > 9 || value > ( (uint32_t)INT_MAX - digit ) / 10u ) {
			return qfalse;
		}
		value = value * 10u + digit;
	}
	if ( i == 1 ) {
		return qfalse;
	}

	*outModelIndex = (int)value;
	return qtrue;
}

static inline qboolean QLBSP_AdvertisementGeometryFinite( const qlBspAdvertisementDisk_t *record ) {
	const byte *normal;
	const byte *points;
	size_t i;

	if ( !record ) {
		return qfalse;
	}
	normal = (const byte *)record + offsetof( qlBspAdvertisementDisk_t, normal );
	points = (const byte *)record + offsetof( qlBspAdvertisementDisk_t, points );
	for ( i = 0; i < 3; ++i ) {
		if ( ( QLBSP_ReadLE32( normal + i * sizeof( float ) ) & 0x7f800000u ) == 0x7f800000u ) {
			return qfalse;
		}
	}
	for ( i = 0; i < 12; ++i ) {
		if ( ( QLBSP_ReadLE32( points + i * sizeof( float ) ) & 0x7f800000u ) == 0x7f800000u ) {
			return qfalse;
		}
	}
	return qtrue;
}

#if defined(__cplusplus)
static_assert( QL_BSP_ADVERTISEMENT_LUMP_INDEX == HEADER_LUMPS,
	"Quake Live advertisement lump must immediately follow the v46 header" );
static_assert( sizeof( qlBspAdvertisementDisk_t ) == 128,
	"Quake Live advertisement record layout changed" );
#else
typedef char qlBspAdvertisementLumpMustFollowHeader[
	QL_BSP_ADVERTISEMENT_LUMP_INDEX == HEADER_LUMPS ? 1 : -1];
typedef char qlBspAdvertisementRecordMustBe128Bytes[
	sizeof( qlBspAdvertisementDisk_t ) == 128 ? 1 : -1];
#endif

#endif /* FNQL_QCOMMON_QL_BSP_H */
