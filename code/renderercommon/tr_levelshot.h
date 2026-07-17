/*
===========================================================================
Copyright (C) 2026 FnQL contributors

Strict, renderer-independent validation for levelshot dimensions and aspect
ratios.  Levelshots are written as uncompressed 24-bit TGA files, so both the
format's 16-bit dimensions and the engine's signed byte-count interfaces must
be respected before allocating or resampling.
===========================================================================
*/

#ifndef TR_LEVELSHOT_H
#define TR_LEVELSHOT_H

#include <limits.h>
#include <float.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define LEVELSHOT_TGA_MAX_DIMENSION 65535
/* Two output buffers coexist while the TGA is assembled.  Keep each one at a
 * practical, portable ceiling instead of allowing a cvar to request gigabytes. */
#define LEVELSHOT_MAX_RGB_BYTES ( (size_t)64 * 1024 * 1024 )

static ID_INLINE qboolean R_LevelshotFloatIsFinite( float value )
{
	return ( value == value && value <= FLT_MAX && value >= -FLT_MAX ) ?
		qtrue : qfalse;
}

static ID_INLINE qboolean R_LevelshotValueIsDisabled( const char *value )
{
	return ( !value || !value[0] || !Q_stricmp( value, "0" ) ||
		!Q_stricmp( value, "none" ) || !Q_stricmp( value, "source" ) ||
		!Q_stricmp( value, "viewport" ) || !Q_stricmp( value, "default" ) ) ?
		qtrue : qfalse;
}

static ID_INLINE const char *R_LevelshotFindSeparator( const char *value )
{
	const char *sep;

	sep = strchr( value, 'x' );
	if ( !sep ) {
		sep = strchr( value, 'X' );
	}
	if ( !sep ) {
		sep = strchr( value, ':' );
	}
	if ( !sep ) {
		sep = strchr( value, '/' );
	}
	return sep;
}

static ID_INLINE qboolean R_LevelshotCheckedRgbBytes( int width, int height,
	size_t *rgbBytes )
{
	size_t bytes;

	if ( width <= 0 || height <= 0 || width > LEVELSHOT_TGA_MAX_DIMENSION ||
		height > LEVELSHOT_TGA_MAX_DIMENSION ) {
		return qfalse;
	}
	/* Division before multiplication keeps this valid on 32-bit size_t too. */
	if ( (size_t)width > LEVELSHOT_MAX_RGB_BYTES / 3u / (size_t)height ) {
		return qfalse;
	}
	bytes = (size_t)width * (size_t)height * 3u;
	if ( bytes > (size_t)INT_MAX - 18u ) {
		return qfalse;
	}
	if ( rgbBytes ) {
		*rgbBytes = bytes;
	}
	return qtrue;
}

static ID_INLINE qboolean R_LevelshotParseDimension( const char *value,
	size_t length, int *dimension )
{
	unsigned int parsed = 0;
	size_t i;

	if ( !value || !length || !dimension ) {
		return qfalse;
	}
	for ( i = 0; i < length; i++ ) {
		unsigned int digit;

		if ( value[i] < '0' || value[i] > '9' ) {
			return qfalse;
		}
		digit = (unsigned int)( value[i] - '0' );
		if ( parsed > ( LEVELSHOT_TGA_MAX_DIMENSION - digit ) / 10u ) {
			return qfalse;
		}
		parsed = parsed * 10u + digit;
	}
	if ( parsed == 0 ) {
		return qfalse;
	}
	*dimension = (int)parsed;
	return qtrue;
}

static ID_INLINE qboolean R_ParseLevelshotSize( const char *value, int *width,
	int *height )
{
	const char *sep;
	size_t length;

	if ( R_LevelshotValueIsDisabled( value ) || !width || !height ) {
		return qfalse;
	}
	length = strlen( value );
	sep = R_LevelshotFindSeparator( value );
	if ( sep ) {
		const size_t leftLength = (size_t)( sep - value );
		const size_t rightLength = length - leftLength - 1u;

		if ( !R_LevelshotParseDimension( value, leftLength, width ) ||
			!R_LevelshotParseDimension( sep + 1, rightLength, height ) ) {
			return qfalse;
		}
	} else {
		if ( !R_LevelshotParseDimension( value, length, width ) ) {
			return qfalse;
		}
		*height = *width;
	}
	return R_LevelshotCheckedRgbBytes( *width, *height, NULL );
}

static ID_INLINE qboolean R_LevelshotParsePositiveDouble( const char *value,
	size_t length, double *result )
{
	char token[64];
	char *end;
	double parsed;

	if ( !value || !length || length >= sizeof( token ) || !result ) {
		return qfalse;
	}
	Com_Memcpy( token, value, length );
	token[length] = '\0';
	parsed = strtod( token, &end );
	if ( end == token || *end || !( parsed > 0.0 ) || parsed != parsed ||
		parsed > DBL_MAX ) {
		return qfalse;
	}
	*result = parsed;
	return qtrue;
}

static ID_INLINE qboolean R_ParseLevelshotAspect( const char *value,
	float *aspect )
{
	const char *sep;
	double numerator;
	double denominator = 1.0;
	double ratio;
	size_t length;

	if ( R_LevelshotValueIsDisabled( value ) || !aspect ) {
		return qfalse;
	}
	length = strlen( value );
	sep = R_LevelshotFindSeparator( value );
	if ( sep ) {
		const size_t leftLength = (size_t)( sep - value );
		const size_t rightLength = length - leftLength - 1u;

		if ( !R_LevelshotParsePositiveDouble( value, leftLength, &numerator ) ||
			!R_LevelshotParsePositiveDouble( sep + 1, rightLength, &denominator ) ) {
			return qfalse;
		}
	} else if ( !R_LevelshotParsePositiveDouble( value, length, &numerator ) ) {
		return qfalse;
	}
	ratio = numerator / denominator;
	if ( !( ratio > 0.0 ) || ratio != ratio || ratio > FLT_MAX ) {
		return qfalse;
	}
	*aspect = (float)ratio;
	return *aspect > 0.0f ? qtrue : qfalse;
}

#endif /* TR_LEVELSHOT_H */
