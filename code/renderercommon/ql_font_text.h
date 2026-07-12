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

#ifndef FNQL_RENDERERCOMMON_QL_FONT_TEXT_H
#define FNQL_RENDERERCOMMON_QL_FONT_TEXT_H

typedef struct qlFontUtf8Result_s {
	unsigned int codepoint;
	int bytes;
} qlFontUtf8Result_t;

/* Decode one scalar without reading beyond end. Malformed input consumes one byte. */
static inline qlFontUtf8Result_t QL_FontDecodeUtf8( const char *text, const char *end ) {
	const unsigned char *s = (const unsigned char *)text;
	qlFontUtf8Result_t result = { 0, 0 };
	unsigned int codepoint;
	int length;
	int i;

	if ( !text || ( end && text >= end ) || !s[0] ) {
		return result;
	}

	result.codepoint = s[0];
	result.bytes = 1;
	if ( s[0] < 0x80 ) {
		return result;
	}

	if ( ( s[0] & 0xe0 ) == 0xc0 ) {
		codepoint = s[0] & 0x1f;
		length = 2;
	} else if ( ( s[0] & 0xf0 ) == 0xe0 ) {
		codepoint = s[0] & 0x0f;
		length = 3;
	} else if ( ( s[0] & 0xf8 ) == 0xf0 ) {
		codepoint = s[0] & 0x07;
		length = 4;
	} else {
		return result;
	}

	if ( end && end - text < length ) {
		return result;
	}
	for ( i = 1; i < length; ++i ) {
		if ( !s[i] || ( s[i] & 0xc0 ) != 0x80 ) {
			return result;
		}
		codepoint = ( codepoint << 6 ) | ( s[i] & 0x3f );
	}

	if ( ( length == 2 && codepoint < 0x80 ) ||
		( length == 3 && codepoint < 0x800 ) ||
		( length == 4 && codepoint < 0x10000 ) ||
		( codepoint >= 0xd800 && codepoint <= 0xdfff ) || codepoint > 0x10ffff ) {
		return result;
	}

	result.codepoint = codepoint;
	result.bytes = length;
	return result;
}

/* Retail host text treats only ^0 through ^7 as color controls. */
static inline int QL_FontColorEscape( const char *text, const char *end, int *colorIndex ) {
	if ( !text || text[0] != '^' || ( end && text + 2 > end ) || !text[1] ) {
		return 0;
	}
	if ( text[1] < '0' || text[1] > '7' ) {
		return 0;
	}
	if ( colorIndex ) {
		*colorIndex = text[1] - '0';
	}
	return 2;
}

static inline int QL_FontScaleTenths( float scale ) {
	int value;

	if ( scale <= 0.0f ) {
		scale = 48.0f;
	}
	value = (int)( scale * 10.0f + 0.5f );
	if ( value < 2 ) {
		return 2;
	}
	if ( value > 32767 ) {
		return 32767;
	}
	return value;
}

/*
 * Host text sizes describe the ascender-to-descender span, while FreeType's
 * character size describes the em square. Convert without relying on rounded
 * FT_Size metrics so every face and fallback shares the same baseline span.
 */
static inline int QL_FontFaceCharSize26Dot6( int scaleTenths, int unitsPerEm,
	int ascender, int descender ) {
	int pixelHeight = ( scaleTenths * 64 + 5 ) / 10;
	int fontHeight = ascender - descender;
	long long adjusted;

	if ( pixelHeight < 1 ) {
		pixelHeight = 1;
	}
	if ( unitsPerEm <= 0 || fontHeight <= 0 ) {
		return pixelHeight;
	}

	adjusted = ( (long long)pixelHeight * unitsPerEm + fontHeight / 2 ) / fontHeight;
	if ( adjusted < 1 ) {
		return 1;
	}
	if ( adjusted > 0x7fffffffLL ) {
		return 0x7fffffff;
	}
	return (int)adjusted;
}

static inline float QL_FontFaceMetric( int scaleTenths, int metric,
	int ascender, int descender ) {
	int fontHeight = ascender - descender;

	if ( fontHeight <= 0 ) {
		return 0.0f;
	}
	return ( (float)scaleTenths / 10.0f ) * (float)metric / (float)fontHeight;
}

#endif
