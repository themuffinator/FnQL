/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL and is distributed under the terms of the GNU
General Public License version 2 or (at your option) any later version.
===========================================================================
*/

#ifndef FILESYSTEM_WRITE_BOUNDS_H
#define FILESYSTEM_WRITE_BOUNDS_H

#include "q_shared.h"
#include "qcommon.h"

#include <string.h>

/*
 * Writable qpaths must remain relative to the root selected by the caller.
 * Check complete path components so names such as "file..cfg" remain valid,
 * while either separator spelling of a parent component is rejected.  Win32
 * trims trailing spaces and periods from path components, so reject components
 * which normalize to only dots as well.
 */
static ID_INLINE qboolean FS_WriteQpathIsValid( const char *qpath )
{
	const char *component;
	const char *separator;
	size_t componentLength;
	size_t normalizedLength;

	if ( !qpath || !qpath[0] || qpath[0] == '/' || qpath[0] == '\\' ||
		strchr( qpath, ':' ) ) {
		return qfalse;
	}

	for ( component = qpath; *component;
		component = *separator ? separator + 1 : separator ) {
		separator = component;
		while ( *separator && *separator != '/' && *separator != '\\' ) {
			separator++;
		}

		componentLength = (size_t)( separator - component );
		normalizedLength = componentLength;
		while ( normalizedLength > 0 &&
			( component[normalizedLength - 1] == ' ' ||
				component[normalizedLength - 1] == '.' ) ) {
			normalizedLength--;
		}
		if ( componentLength > 0 && normalizedLength == 0 ) {
			return qfalse;
		}
	}

	return qtrue;
}

/* A zero-byte write is a supported no-op and may carry a NULL buffer. */
static ID_INLINE qboolean FS_WriteRequestIsValid( const void *buffer, int len,
	fileHandle_t handle )
{
	return ( handle > FS_INVALID_HANDLE && handle < MAX_FILE_HANDLES &&
		len >= 0 && ( len == 0 || buffer != NULL ) ) ? qtrue : qfalse;
}

#endif /* FILESYSTEM_WRITE_BOUNDS_H */
