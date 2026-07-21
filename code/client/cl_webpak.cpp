/*
===========================================================================
Copyright (C) 2026

This file is part of FnQL.

FnQL is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

FnQL is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with FnQL; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// cl_webpak.cpp -- Quake Live WebUI resource bridge

#include <algorithm>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "client.h"
#include "webpak_format.h"

#define CL_WEB_FILE_LIST_BUFFER 4096
#define CL_WEB_PAK_MAX_BYTES ( 512 * 1024 * 1024 )
#define CL_WEB_RESOURCE_MAX_BYTES ( 64 * 1024 * 1024 )

typedef struct {
	uint16_t	resourceId;
	char		path[MAX_QPATH];
} clWebDataPakPath_t;

typedef struct {
	qboolean			loaded;
	uint32_t			version;
	uint32_t			resourceCount;
	uint16_t			aliasCount;
	int					headerLength;
	byte				*buffer;
	int					bufferLength;
	clWebDataPakPath_t	*paths;
	int					pathCount;
	char				sourcePath[MAX_OSPATH];
} clWebDataPak_t;

static clWebDataPak_t cl_webDataPak;
static clWebDataPak_t cl_fnqlWebDataPak;

static cvar_t *cl_webPakLoaded;
static cvar_t *cl_webPakSource;
static cvar_t *cl_webPakVersion;
static cvar_t *cl_webPakResourceCount;
static cvar_t *cl_fnqlWebPakLoaded;
static cvar_t *cl_fnqlWebPakSource;
static cvar_t *cl_fnqlWebPakVersion;
static cvar_t *cl_fnqlWebPakResourceCount;
static cvar_t *ui_resourceBridgeProvider;
static cvar_t *ui_resourceBridgePolicy;
static cvar_t *ui_resourceBridgeParityScope;
static cvar_t *ui_resourceBridgeParityReason;
static cvar_t *ui_resourceBridgeSteamDataSourceSubset;
static cvar_t *ui_resourceBridgeSteamDataSourceNativeGap;
static cvar_t *ui_resourceBridgeSteamDataSourceFallbackOwner;

static void CL_WebPak_SetCvarIfChanged( const char *name, const char *value ) {
	const char *current;

	if ( !name || !value ) {
		return;
	}

	current = Cvar_VariableString( name );
	if ( strcmp( current, value ) ) {
		Cvar_Set( name, value );
	}
}

static void CL_WebPak_BuildStandalonePath( const char *rootPath, const char *filename, char *outPath, size_t outPathSize ) {
	size_t rootLength;

	if ( !outPath || outPathSize == 0 ) {
		return;
	}

	outPath[0] = '\0';
	if ( !rootPath || !rootPath[0] || !filename || !filename[0] ) {
		return;
	}

	rootLength = strlen( rootPath );
	if ( rootPath[rootLength - 1] == '/' || rootPath[rootLength - 1] == '\\' ) {
		Com_sprintf( outPath, outPathSize, "%s%s", rootPath, filename );
	} else {
		Com_sprintf( outPath, outPathSize, "%s%c%s", rootPath, PATH_SEP, filename );
	}
}

static uint16_t CL_WebDataPak_ReadUInt16( const byte *cursor ) {
	return fnql::webui::DataPackReadU16( cursor );
}

static uint32_t CL_WebDataPak_ReadUInt32( const byte *cursor ) {
	return fnql::webui::DataPackReadU32( cursor );
}

static void CL_WebDataPak_Clear( clWebDataPak_t *dataPak ) {
	if ( !dataPak ) {
		return;
	}

	if ( dataPak->paths ) {
		Z_Free( dataPak->paths );
	}

	if ( dataPak->buffer ) {
		free( dataPak->buffer );
	}

	Com_Memset( dataPak, 0, sizeof( *dataPak ) );
}

static const byte *CL_WebDataPak_EntryPointer( const clWebDataPak_t *dataPak, int entryIndex ) {
	if ( !dataPak || !dataPak->buffer || entryIndex < 0 || entryIndex > (int)dataPak->resourceCount ) {
		return NULL;
	}

	return dataPak->buffer + dataPak->headerLength + ( entryIndex * 6 );
}

static const byte *CL_WebDataPak_AliasPointer( const clWebDataPak_t *dataPak, int aliasIndex ) {
	int aliasOffset;

	if ( !dataPak || !dataPak->buffer || aliasIndex < 0 || aliasIndex >= (int)dataPak->aliasCount ) {
		return NULL;
	}

	aliasOffset = dataPak->headerLength + ( (int)dataPak->resourceCount + 1 ) * 6;
	return dataPak->buffer + aliasOffset + ( aliasIndex * 4 );
}

static int CL_WebDataPak_FindEntryIndex( const clWebDataPak_t *dataPak, uint16_t resourceId ) {
	int low;
	int high;

	if ( !dataPak || !dataPak->buffer || dataPak->resourceCount == 0 ) {
		return -1;
	}

	low = 0;
	high = (int)dataPak->resourceCount - 1;
	while ( low <= high ) {
		int mid;
		const byte *entry;
		uint16_t currentId;

		mid = low + ( high - low ) / 2;
		entry = CL_WebDataPak_EntryPointer( dataPak, mid );
		currentId = CL_WebDataPak_ReadUInt16( entry );

		if ( currentId == resourceId ) {
			return mid;
		}

		if ( currentId < resourceId ) {
			low = mid + 1;
		} else {
			high = mid - 1;
		}
	}

	low = 0;
	high = (int)dataPak->aliasCount - 1;
	while ( low <= high ) {
		int mid;
		const byte *alias;
		uint16_t currentId;

		mid = low + ( high - low ) / 2;
		alias = CL_WebDataPak_AliasPointer( dataPak, mid );
		currentId = CL_WebDataPak_ReadUInt16( alias );

		if ( currentId == resourceId ) {
			return (int)CL_WebDataPak_ReadUInt16( alias + 2 );
		}

		if ( currentId < resourceId ) {
			low = mid + 1;
		} else {
			high = mid - 1;
		}
	}

	return -1;
}

static qboolean CL_WebDataPak_GetResourceView( const clWebDataPak_t *dataPak, uint16_t resourceId, const byte **outData, int *outLength ) {
	int entryIndex;
	const byte *entry;
	const byte *nextEntry;
	uint32_t startOffset;
	uint32_t endOffset;

	if ( outData ) {
		*outData = NULL;
	}
	if ( outLength ) {
		*outLength = 0;
	}

	if ( !dataPak || !dataPak->buffer || dataPak->resourceCount == 0 ) {
		return qfalse;
	}

	entryIndex = CL_WebDataPak_FindEntryIndex( dataPak, resourceId );
	if ( entryIndex < 0 || entryIndex >= (int)dataPak->resourceCount ) {
		return qfalse;
	}

	entry = CL_WebDataPak_EntryPointer( dataPak, entryIndex );
	nextEntry = CL_WebDataPak_EntryPointer( dataPak, entryIndex + 1 );
	if ( !entry || !nextEntry ) {
		return qfalse;
	}

	startOffset = CL_WebDataPak_ReadUInt32( entry + 2 );
	endOffset = CL_WebDataPak_ReadUInt32( nextEntry + 2 );
	if ( endOffset < startOffset || endOffset > (uint32_t)dataPak->bufferLength ) {
		return qfalse;
	}

	if ( outData ) {
		*outData = dataPak->buffer + startOffset;
	}
	if ( outLength ) {
		*outLength = (int)( endOffset - startOffset );
	}

	return qtrue;
}

static qboolean CL_WebDataPak_ReadResource( const clWebDataPak_t *dataPak, uint16_t resourceId, void **outBuffer, int *outLength ) {
	const byte *view;
	int length;
	byte *copy;

	if ( outBuffer ) {
		*outBuffer = NULL;
	}
	if ( outLength ) {
		*outLength = 0;
	}

	if ( !outBuffer || !CL_WebDataPak_GetResourceView( dataPak, resourceId, &view, &length )
		|| length < 0 || length > CL_WEB_RESOURCE_MAX_BYTES ) {
		return qfalse;
	}

	copy = (byte *)Z_Malloc( length + 1 );
	if ( !copy ) {
		return qfalse;
	}

	if ( length > 0 ) {
		Com_Memcpy( copy, view, length );
	}
	copy[length] = 0;

	*outBuffer = copy;
	if ( outLength ) {
		*outLength = length;
	}

	return qtrue;
}

static qboolean CL_WebDataPak_DecodePath( const byte *utf16Data, uint32_t charCount, char *outPath, size_t outPathSize ) {
	uint32_t i;

	if ( !utf16Data || !outPath || outPathSize == 0 || charCount == 0 || charCount >= outPathSize ) {
		return qfalse;
	}

	for ( i = 0; i < charCount; i++ ) {
		byte low;
		byte high;

		low = utf16Data[i * 2];
		high = utf16Data[i * 2 + 1];
		if ( high != 0 || low == 0 || low < 0x20 || low == 0x7f ) {
			return qfalse;
		}

		outPath[i] = ( low == '\\' ) ? '/' : (char)low;
	}
	outPath[charCount] = '\0';

	if ( outPath[0] == '/' || strstr( outPath, ".." ) || strstr( outPath, "::" ) || strchr( outPath, ':' ) ) {
		return qfalse;
	}

	Q_strlwr( outPath );
	return qtrue;
}

static qboolean CL_WebDataPak_BuildPathTable( clWebDataPak_t *dataPak ) {
	const byte *manifestData;
	uint32_t trailerResourceId;
	uint32_t manifestPayloadLength;
	int manifestLength;
	int cursor;
	char pendingPath[MAX_QPATH];
	qboolean havePending;

	if ( !dataPak || dataPak->resourceCount < 1 ) {
		return qfalse;
	}

	if ( !CL_WebDataPak_GetResourceView( dataPak, 0, &manifestData, &manifestLength ) ) {
		return qfalse;
	}

	if ( manifestLength < 16 ) {
		return qfalse;
	}
	manifestPayloadLength = CL_WebDataPak_ReadUInt32( manifestData );
	if ( manifestPayloadLength > (uint32_t)manifestLength - 4u ) {
		return qfalse;
	}

	trailerResourceId = CL_WebDataPak_ReadUInt32( manifestData + manifestLength - 4 );
	if ( trailerResourceId > UINT16_MAX
		|| CL_WebDataPak_FindEntryIndex( dataPak, (uint16_t)trailerResourceId ) < 0 ) {
		return qfalse;
	}
	dataPak->paths = (clWebDataPakPath_t *)Z_Malloc( sizeof( *dataPak->paths ) * ( dataPak->resourceCount - 1 ) );
	if ( !dataPak->paths ) {
		return qfalse;
	}

	dataPak->pathCount = 0;
	cursor = 8;
	havePending = qfalse;
	pendingPath[0] = '\0';

	while ( cursor + 12 <= manifestLength - 8 ) {
		uint32_t nextResourceId;
		uint32_t pathLength;
		char decodedPath[MAX_QPATH];

		(void)CL_WebDataPak_ReadUInt32( manifestData + cursor );
		cursor += 4;
		nextResourceId = CL_WebDataPak_ReadUInt32( manifestData + cursor );
		cursor += 4;
		pathLength = CL_WebDataPak_ReadUInt32( manifestData + cursor );
		cursor += 4;

		if ( nextResourceId > UINT16_MAX
			|| CL_WebDataPak_FindEntryIndex( dataPak, (uint16_t)nextResourceId ) < 0
			|| pathLength == 0 || pathLength >= MAX_QPATH
			|| cursor + (int)( pathLength * 2 ) > manifestLength - 8 ) {
			return qfalse;
		}

		if ( !CL_WebDataPak_DecodePath( manifestData + cursor, pathLength, decodedPath, sizeof( decodedPath ) ) ) {
			return qfalse;
		}
		cursor += (int)( pathLength * 2 );
		cursor = ( cursor + 3 ) & ~3;
		if ( cursor > manifestLength - 8 ) {
			return qfalse;
		}

		if ( havePending ) {
			if ( dataPak->pathCount >= (int)dataPak->resourceCount - 1 ) {
				return qfalse;
			}
			dataPak->paths[dataPak->pathCount].resourceId = (uint16_t)nextResourceId;
			Q_strncpyz( dataPak->paths[dataPak->pathCount].path, pendingPath, sizeof( dataPak->paths[dataPak->pathCount].path ) );
			dataPak->pathCount++;
		}

		Q_strncpyz( pendingPath, decodedPath, sizeof( pendingPath ) );
		havePending = qtrue;
	}

	if ( havePending ) {
		if ( dataPak->pathCount >= (int)dataPak->resourceCount - 1 ) {
			return qfalse;
		}
		dataPak->paths[dataPak->pathCount].resourceId = (uint16_t)trailerResourceId;
		Q_strncpyz( dataPak->paths[dataPak->pathCount].path, pendingPath, sizeof( dataPak->paths[dataPak->pathCount].path ) );
		dataPak->pathCount++;
	}

	if ( dataPak->pathCount <= 0 ) {
		return qfalse;
	}

	std::sort( dataPak->paths, dataPak->paths + dataPak->pathCount,
		[]( const clWebDataPakPath_t &lhs, const clWebDataPakPath_t &rhs ) {
			return strcmp( lhs.path, rhs.path ) < 0;
		} );
	for ( int i = 1; i < dataPak->pathCount; ++i ) {
		if ( !strcmp( dataPak->paths[i - 1].path, dataPak->paths[i].path ) ) {
			return qfalse;
		}
	}

	return qtrue;
}

static qboolean CL_WebDataPak_LoadFile( const char *pakPath, clWebDataPak_t *outDataPak ) {
	clWebDataPak_t dataPak;
	FILE *fp;
	long fileLength;
	fnql::webui::DataPackError parseError;
	fnql::webui::DataPackLayout layout;

	if ( !pakPath || !pakPath[0] || !outDataPak ) {
		return qfalse;
	}

	Com_Memset( &dataPak, 0, sizeof( dataPak ) );

	fp = fopen( pakPath, "rb" );
	if ( !fp ) {
		return qfalse;
	}

	if ( fseek( fp, 0, SEEK_END ) != 0 ) {
		fclose( fp );
		return qfalse;
	}
	fileLength = ftell( fp );
	if ( fileLength <= 0 || fileLength > INT_MAX || fileLength > CL_WEB_PAK_MAX_BYTES
		|| fseek( fp, 0, SEEK_SET ) != 0 ) {
		fclose( fp );
		return qfalse;
	}

	dataPak.buffer = (byte *)malloc( (size_t)fileLength );
	if ( !dataPak.buffer ) {
		fclose( fp );
		return qfalse;
	}

	if ( fread( dataPak.buffer, 1, (size_t)fileLength, fp ) != (size_t)fileLength ) {
		fclose( fp );
		CL_WebDataPak_Clear( &dataPak );
		return qfalse;
	}
	fclose( fp );

	dataPak.bufferLength = (int)fileLength;
	if ( !fnql::webui::InspectDataPack( dataPak.buffer, (size_t)dataPak.bufferLength, &layout, &parseError ) ) {
		Com_DPrintf( "web.pak rejected at %s: %s\n", pakPath, fnql::webui::DataPackErrorString( parseError ) );
		CL_WebDataPak_Clear( &dataPak );
		return qfalse;
	}
	if ( layout.resourceCount < 2 ) {
		Com_DPrintf( "web.pak rejected at %s: launcher manifest has no resource payload\n", pakPath );
		CL_WebDataPak_Clear( &dataPak );
		return qfalse;
	}

	dataPak.version = layout.version;
	dataPak.resourceCount = layout.resourceCount;
	dataPak.aliasCount = layout.aliasCount;
	dataPak.headerLength = (int)layout.headerSize;

	if ( !CL_WebDataPak_BuildPathTable( &dataPak ) ) {
		CL_WebDataPak_Clear( &dataPak );
		return qfalse;
	}

	dataPak.loaded = qtrue;
	Q_strncpyz( dataPak.sourcePath, pakPath, sizeof( dataPak.sourcePath ) );
	*outDataPak = dataPak;
	return qtrue;
}

static qboolean CL_WebDataPak_Load( const char *pakPath, clWebDataPak_t *target ) {
	clWebDataPak_t dataPak;

	if ( !target || !CL_WebDataPak_LoadFile( pakPath, &dataPak ) ) {
		return qfalse;
	}

	CL_WebDataPak_Clear( target );
	*target = dataPak;
	return qtrue;
}

static qboolean CL_WebDataPak_Fetch( const clWebDataPak_t *dataPak, const char *normalizedPath, void **outBuffer, int *outLength ) {
	clWebDataPakPath_t *match;

	if ( !dataPak || !dataPak->loaded || !normalizedPath || !normalizedPath[0] ) {
		return qfalse;
	}

	match = std::lower_bound( dataPak->paths, dataPak->paths + dataPak->pathCount, normalizedPath,
		[]( const clWebDataPakPath_t &entry, const char *path ) {
			return strcmp( entry.path, path ) < 0;
		} );
	if ( match != dataPak->paths + dataPak->pathCount && !strcmp( match->path, normalizedPath ) ) {
		return CL_WebDataPak_ReadResource( dataPak, match->resourceId, outBuffer, outLength );
	}

	return qfalse;
}

static const char *CL_WebPak_StripProtocol( const char *virtualPath ) {
	static const char trustedPrefix[] = "asset://ql/";
	const char *separator;

	if ( !virtualPath ) {
		return NULL;
	}

	separator = strstr( virtualPath, "://" );
	if ( !separator ) {
		return virtualPath;
	}
	if ( Q_stricmpn( virtualPath, trustedPrefix, sizeof( trustedPrefix ) - 1 ) ) {
		return NULL;
	}
	return virtualPath + sizeof( trustedPrefix ) - 1;
}

static qboolean CL_WebPak_IsSteamDataSourceRequest( const char *virtualPath ) {
	return ( virtualPath && !Q_stricmpn( virtualPath, "steam://", 8 ) ) ? qtrue : qfalse;
}

static qboolean CL_WebPak_NormalizePath( const char *virtualPath, char *normalized, size_t normalizedSize ) {
	const char *normalizedSource;
	size_t readIndex;
	size_t writeIndex;

	if ( !virtualPath || !virtualPath[0] || !normalized || normalizedSize < 1 ) {
		return qfalse;
	}

	if ( CL_WebPak_IsSteamDataSourceRequest( virtualPath ) ) {
		return qfalse;
	}

	normalizedSource = CL_WebPak_StripProtocol( virtualPath );
	if ( !normalizedSource || !normalizedSource[0] ) {
		return qfalse;
	}

	for ( readIndex = 0, writeIndex = 0; normalizedSource[readIndex] && normalizedSource[readIndex] != '?' && normalizedSource[readIndex] != '#'; readIndex++ ) {
		char ch;

		if ( writeIndex >= normalizedSize - 1 ) {
			return qfalse;
		}

		ch = normalizedSource[readIndex];
		normalized[writeIndex++] = ( ch == '\\' ) ? '/' : ch;
	}
	normalized[writeIndex] = '\0';

	if ( !normalized[0] || normalized[0] == '/' || strstr( normalized, ".." ) || strstr( normalized, "::" ) || strchr( normalized, ':' ) ) {
		return qfalse;
	}

	Q_strlwr( normalized );
	return qtrue;
}

static qboolean CL_WebPak_ReadStandaloneFile( const char *rootPath, const char *normalizedPath, const char *prefix, void **outBuffer, int *outLength ) {
	char relativePath[MAX_QPATH];
	char osPath[MAX_OSPATH];
	FILE *fp;
	long fileLength;
	byte *buffer;

	if ( !rootPath || !rootPath[0] || !normalizedPath || !normalizedPath[0] || !outBuffer ) {
		return qfalse;
	}

	if ( prefix && prefix[0] ) {
		Com_sprintf( relativePath, sizeof( relativePath ), "%s/%s", prefix, normalizedPath );
	} else {
		Q_strncpyz( relativePath, normalizedPath, sizeof( relativePath ) );
	}
	CL_WebPak_BuildStandalonePath( rootPath, relativePath, osPath, sizeof( osPath ) );

	fp = fopen( osPath, "rb" );
	if ( !fp ) {
		return qfalse;
	}

	if ( fseek( fp, 0, SEEK_END ) != 0 ) {
		fclose( fp );
		return qfalse;
	}
	fileLength = ftell( fp );
	if ( fileLength < 0 || fileLength > INT_MAX || fileLength > CL_WEB_RESOURCE_MAX_BYTES
		|| fseek( fp, 0, SEEK_SET ) != 0 ) {
		fclose( fp );
		return qfalse;
	}

	buffer = (byte *)Z_Malloc( (int)fileLength + 1 );
	if ( !buffer ) {
		fclose( fp );
		return qfalse;
	}

	if ( fileLength > 0 && fread( buffer, 1, (size_t)fileLength, fp ) != (size_t)fileLength ) {
		Z_Free( buffer );
		fclose( fp );
		return qfalse;
	}
	fclose( fp );

	buffer[fileLength] = 0;
	*outBuffer = buffer;
	if ( outLength ) {
		*outLength = (int)fileLength;
	}

	return qtrue;
}

static qboolean CL_WebPak_ReadLooseFallback( const char *normalizedPath, void **outBuffer, int *outLength ) {
	static const char *pathCvars[] = { "fs_homepath", "fs_basepath", "fs_steampath" };
	char rootPath[MAX_OSPATH];
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( pathCvars ); i++ ) {
		Cvar_VariableStringBuffer( pathCvars[i], rootPath, sizeof( rootPath ) );
		if ( CL_WebPak_ReadStandaloneFile( rootPath, normalizedPath, "web", outBuffer, outLength )
			|| CL_WebPak_ReadStandaloneFile( rootPath, normalizedPath, NULL, outBuffer, outLength ) ) {
			return qtrue;
		}
	}

	return qfalse;
}

static qboolean CL_WebPak_ReadInternal( const char *normalizedPath, void **outBuffer, int *outLength ) {
	if ( outBuffer ) {
		*outBuffer = NULL;
	}
	if ( outLength ) {
		*outLength = 0;
	}

	if ( !normalizedPath || !outBuffer ) {
		return qfalse;
	}

	// fnql-web.pak is deliberately sparse. Its project-owned resources replace
	// matching retail paths; every other request falls through to the external
	// Quake Live web.pak and finally the established loose-file fallback.
	if ( CL_WebDataPak_Fetch( &cl_fnqlWebDataPak, normalizedPath, outBuffer, outLength )
		|| CL_WebDataPak_Fetch( &cl_webDataPak, normalizedPath, outBuffer, outLength ) ) {
		return qtrue;
	}

	return CL_WebPak_ReadLooseFallback( normalizedPath, outBuffer, outLength );
}

static int CL_WebPak_AppendUniqueFile( const char *name, char *listbuf, int bufsize, int *outOffset, int count ) {
	char *cursor;
	int offset;
	int length;

	if ( !name || !name[0] || !listbuf || !outOffset || bufsize <= 0 ) {
		return count;
	}

	cursor = listbuf;
	while ( *cursor ) {
		if ( !Q_stricmp( cursor, name ) ) {
			return count;
		}
		cursor += strlen( cursor ) + 1;
	}

	offset = *outOffset;
	length = (int)strlen( name ) + 1;
	if ( length >= bufsize || offset < 0 || offset > bufsize - length - 1 ) {
		return count;
	}

	strcpy( listbuf + offset, name );
	offset += length;
	listbuf[offset] = '\0';
	*outOffset = offset;
	return count + 1;
}

static int CL_WebPak_AppendFileList( const char *sourceList, int sourceCount, char *listbuf, int bufsize, int *outOffset, int count ) {
	const char *cursor;
	int i;

	if ( !sourceList || !listbuf || !outOffset || sourceCount <= 0 ) {
		return count;
	}

	cursor = sourceList;
	for ( i = 0; i < sourceCount && *cursor; i++ ) {
		count = CL_WebPak_AppendUniqueFile( cursor, listbuf, bufsize, outOffset, count );
		cursor += strlen( cursor ) + 1;
	}

	return count;
}

static int CL_WebDataPak_GetFileList( const clWebDataPak_t *dataPak, const char *path, const char *extension, char *listbuf, int bufsize ) {
	int pathLength;
	int extensionLength;
	int offset;
	int count;
	int i;

	if ( !dataPak || !dataPak->loaded || !path || !extension || !listbuf || bufsize <= 0 ) {
		return 0;
	}

	pathLength = (int)strlen( path );
	if ( pathLength > 0 && ( path[pathLength - 1] == '\\' || path[pathLength - 1] == '/' ) ) {
		pathLength--;
	}
	extensionLength = (int)strlen( extension );

	listbuf[0] = '\0';
	offset = 0;
	count = 0;

	for ( i = 0; i < dataPak->pathCount; i++ ) {
		const char *entryPath;
		const char *name;
		int length;

		entryPath = dataPak->paths[i].path;
		if ( pathLength > 0 ) {
			if ( Q_stricmpn( entryPath, path, pathLength ) || entryPath[pathLength] != '/' ) {
				continue;
			}
			name = entryPath + pathLength + 1;
		} else {
			name = entryPath;
		}

		if ( strchr( name, '/' ) || strchr( name, '\\' ) ) {
			continue;
		}

		length = (int)strlen( entryPath );
		if ( length < extensionLength || Q_stricmp( entryPath + length - extensionLength, extension ) ) {
			continue;
		}

		count = CL_WebPak_AppendUniqueFile( name, listbuf, bufsize, &offset, count );
	}

	return count;
}

static void CL_WebPak_RegisterCvars( void ) {
	cl_webPakLoaded = Cvar_Get( "cl_webPakLoaded", "0", CVAR_ROM );
	Cvar_SetDescription( cl_webPakLoaded, "Read-only state for the external Quake Live web.pak resource bridge." );
	cl_webPakSource = Cvar_Get( "cl_webPakSource", "", CVAR_ROM );
	Cvar_SetDescription( cl_webPakSource, "Read-only source path for the mounted external Quake Live web.pak." );
	cl_webPakVersion = Cvar_Get( "cl_webPakVersion", "0", CVAR_ROM );
	Cvar_SetDescription( cl_webPakVersion, "Read-only Chromium DataPack version for the mounted external Quake Live web.pak." );
	cl_webPakResourceCount = Cvar_Get( "cl_webPakResourceCount", "0", CVAR_ROM );
	Cvar_SetDescription( cl_webPakResourceCount, "Read-only validated resource count for the mounted external Quake Live web.pak." );
	cl_fnqlWebPakLoaded = Cvar_Get( "cl_fnqlWebPakLoaded", "0", CVAR_ROM );
	Cvar_SetDescription( cl_fnqlWebPakLoaded, "Read-only state for FnQL's sparse WebUI settings overlay." );
	cl_fnqlWebPakSource = Cvar_Get( "cl_fnqlWebPakSource", "", CVAR_ROM );
	Cvar_SetDescription( cl_fnqlWebPakSource, "Read-only source path for the mounted fnql-web.pak overlay." );
	cl_fnqlWebPakVersion = Cvar_Get( "cl_fnqlWebPakVersion", "0", CVAR_ROM );
	Cvar_SetDescription( cl_fnqlWebPakVersion, "Read-only Chromium DataPack version for the mounted fnql-web.pak overlay." );
	cl_fnqlWebPakResourceCount = Cvar_Get( "cl_fnqlWebPakResourceCount", "0", CVAR_ROM );
	Cvar_SetDescription( cl_fnqlWebPakResourceCount, "Read-only validated resource count for the mounted fnql-web.pak overlay." );
	ui_resourceBridgeProvider = Cvar_Get( "ui_resourceBridgeProvider", "Unavailable", CVAR_ROM );
	Cvar_SetDescription( ui_resourceBridgeProvider, "Read-only provider label for WebUI resource requests." );
	ui_resourceBridgePolicy = Cvar_Get( "ui_resourceBridgePolicy", "webpak-unavailable", CVAR_ROM );
	Cvar_SetDescription( ui_resourceBridgePolicy, "Read-only policy label for WebUI resource requests." );
	ui_resourceBridgeParityScope = Cvar_Get( "ui_resourceBridgeParityScope", "retail web.pak resource bridge", CVAR_ROM );
	Cvar_SetDescription( ui_resourceBridgeParityScope, "Read-only parity scope for WebUI resource requests." );
	ui_resourceBridgeParityReason = Cvar_Get( "ui_resourceBridgeParityReason", "external retail assets only", CVAR_ROM );
	Cvar_SetDescription( ui_resourceBridgeParityReason, "Read-only parity note for WebUI resource requests." );
	ui_resourceBridgeSteamDataSourceSubset = Cvar_Get( "ui_resourceBridgeSteamDataSourceSubset", "avatar-only SteamDataSource", CVAR_ROM );
	Cvar_SetDescription( ui_resourceBridgeSteamDataSourceSubset, "Read-only reconstructed scope for WebUI SteamDataSource requests." );
	ui_resourceBridgeSteamDataSourceNativeGap = Cvar_Get( "ui_resourceBridgeSteamDataSourceNativeGap", "missing non-avatar SteamDataSource owner", CVAR_ROM );
	Cvar_SetDescription( ui_resourceBridgeSteamDataSourceNativeGap, "Read-only native-owner gap for WebUI SteamDataSource requests." );
	ui_resourceBridgeSteamDataSourceFallbackOwner = Cvar_Get( "ui_resourceBridgeSteamDataSourceFallbackOwner", "QLResourceInterceptor launcher/web fallback", CVAR_ROM );
	Cvar_SetDescription( ui_resourceBridgeSteamDataSourceFallbackOwner, "Read-only fallback owner for WebUI SteamDataSource requests outside the native subset." );
}

static void CL_WebPak_RefreshCvars( void ) {
	const qboolean loaded = cl_webDataPak.loaded;
	const qboolean overlayLoaded = cl_fnqlWebDataPak.loaded;

	if ( !cl_webPakLoaded ) {
		CL_WebPak_RegisterCvars();
	}

	CL_WebPak_SetCvarIfChanged( "cl_webPakLoaded", loaded ? "1" : "0" );
	CL_WebPak_SetCvarIfChanged( "cl_webPakSource", loaded ? cl_webDataPak.sourcePath : "" );
	CL_WebPak_SetCvarIfChanged( "cl_webPakVersion",
		loaded ? va( "%u", static_cast<unsigned int>( cl_webDataPak.version ) ) : "0" );
	CL_WebPak_SetCvarIfChanged( "cl_webPakResourceCount",
		loaded ? va( "%u", static_cast<unsigned int>( cl_webDataPak.resourceCount ) ) : "0" );
	CL_WebPak_SetCvarIfChanged( "cl_fnqlWebPakLoaded", overlayLoaded ? "1" : "0" );
	CL_WebPak_SetCvarIfChanged( "cl_fnqlWebPakSource", overlayLoaded ? cl_fnqlWebDataPak.sourcePath : "" );
	CL_WebPak_SetCvarIfChanged( "cl_fnqlWebPakVersion",
		overlayLoaded ? va( "%u", static_cast<unsigned int>( cl_fnqlWebDataPak.version ) ) : "0" );
	CL_WebPak_SetCvarIfChanged( "cl_fnqlWebPakResourceCount",
		overlayLoaded ? va( "%u", static_cast<unsigned int>( cl_fnqlWebDataPak.resourceCount ) ) : "0" );
	CL_WebPak_SetCvarIfChanged( "ui_resourceBridgeProvider",
		overlayLoaded ? ( loaded ? "FnQL overlay + retail web.pak" : "FnQL overlay + loose fallback" )
			: ( loaded ? "Awesomium DataPak web.pak" : "Loose filesystem fallback" ) );
	CL_WebPak_SetCvarIfChanged( "ui_resourceBridgePolicy",
		overlayLoaded ? "fnql-overlay-retail-fallback" : ( loaded ? "retail-assets-external" : "webpak-unavailable" ) );
	CL_WebPak_SetCvarIfChanged( "ui_resourceBridgeParityScope",
		overlayLoaded ? "FnQL settings overlay with retail asset fallback" : "retail web.pak resource bridge" );
	CL_WebPak_SetCvarIfChanged( "ui_resourceBridgeParityReason",
		overlayLoaded ? ( loaded ? "project-owned overrides mounted over external retail web.pak" : "FnQL overrides mounted; external retail web.pak not found" )
			: ( loaded ? "external retail web.pak mounted" : "external retail web.pak not found" ) );
	CL_WebPak_SetCvarIfChanged( "ui_resourceBridgeSteamDataSourceSubset", "avatar-only SteamDataSource" );
	CL_WebPak_SetCvarIfChanged( "ui_resourceBridgeSteamDataSourceNativeGap", "missing non-avatar SteamDataSource owner" );
	CL_WebPak_SetCvarIfChanged( "ui_resourceBridgeSteamDataSourceFallbackOwner", "QLResourceInterceptor launcher/web fallback" );
}

void CL_WebPak_Init( void ) {
	static const char *retailPathCvars[] = { "fs_homepath", "fs_basepath", "fs_steampath" };
	static const char *overlayPathCvars[] = { "fs_homepath", "fs_apppath", "fs_basepath", "fs_steampath" };
	char rootPath[MAX_OSPATH];
	char pakPath[MAX_OSPATH];
	qboolean overlayLoaded = qfalse;
	int i;

	CL_WebPak_RegisterCvars();
	CL_WebDataPak_Clear( &cl_webDataPak );
	CL_WebDataPak_Clear( &cl_fnqlWebDataPak );

	for ( i = 0; i < (int)ARRAY_LEN( retailPathCvars ); i++ ) {
		Cvar_VariableStringBuffer( retailPathCvars[i], rootPath, sizeof( rootPath ) );
		CL_WebPak_BuildStandalonePath( rootPath, "web.pak", pakPath, sizeof( pakPath ) );
		if ( CL_WebDataPak_Load( pakPath, &cl_webDataPak ) ) {
			Com_Printf( "web.pak datapack mounted from %s\n", pakPath );
			break;
		}
	}

	// Prefer the resolved executable directory on packaged Unix builds.  The
	// process working directory remains a fallback for development and Windows
	// layouts that historically stage the sidecar there.
#if defined( __APPLE__ ) || defined( __linux__ )
	const char *appPath = Sys_DefaultAppPath();
	if ( appPath && appPath[0] ) {
		CL_WebPak_BuildStandalonePath( appPath, "fnql-web.pak", pakPath, sizeof( pakPath ) );
		overlayLoaded = CL_WebDataPak_Load( pakPath, &cl_fnqlWebDataPak );
	}
#endif
	if ( !overlayLoaded ) {
		CL_WebPak_BuildStandalonePath( Sys_Pwd(), "fnql-web.pak", pakPath, sizeof( pakPath ) );
		overlayLoaded = CL_WebDataPak_Load( pakPath, &cl_fnqlWebDataPak );
	}

	if ( overlayLoaded ) {
		Com_Printf( "fnql-web.pak sparse overlay mounted from %s\n", pakPath );
	} else {
		for ( i = 0; i < (int)ARRAY_LEN( overlayPathCvars ); i++ ) {
			Cvar_VariableStringBuffer( overlayPathCvars[i], rootPath, sizeof( rootPath ) );
			CL_WebPak_BuildStandalonePath( rootPath, "fnql-web.pak", pakPath, sizeof( pakPath ) );
			if ( CL_WebDataPak_Load( pakPath, &cl_fnqlWebDataPak ) ) {
				Com_Printf( "fnql-web.pak sparse overlay mounted from %s\n", pakPath );
				break;
			}
		}
	}

	CL_WebPak_RefreshCvars();
}

void CL_WebPak_Shutdown( void ) {
	CL_WebDataPak_Clear( &cl_webDataPak );
	CL_WebDataPak_Clear( &cl_fnqlWebDataPak );
	CL_WebPak_RefreshCvars();
}

qboolean CL_WebPak_Available( void ) {
	return cl_webDataPak.loaded;
}

qboolean CL_WebPak_Fetch( const char *virtualPath, void **outBuffer, int *outLength ) {
	char normalized[MAX_QPATH];

	if ( outBuffer ) {
		*outBuffer = NULL;
	}
	if ( outLength ) {
		*outLength = 0;
	}

	if ( !CL_WebPak_NormalizePath( virtualPath, normalized, sizeof( normalized ) ) ) {
		return qfalse;
	}

	return CL_WebPak_ReadInternal( normalized, outBuffer, outLength );
}

int CL_WebPak_GetFileList( const char *path, const char *extension, char *listbuf, int bufsize ) {
	char sourceList[CL_WEB_FILE_LIST_BUFFER];
	int offset;
	int count;
	int sourceCount;

	if ( !path || !extension || !listbuf || bufsize <= 0 ) {
		return 0;
	}

	listbuf[0] = '\0';
	offset = 0;
	count = 0;

	sourceCount = CL_WebDataPak_GetFileList( &cl_fnqlWebDataPak, path, extension, sourceList, sizeof( sourceList ) );
	count = CL_WebPak_AppendFileList( sourceList, sourceCount, listbuf, bufsize, &offset, count );

	sourceCount = CL_WebDataPak_GetFileList( &cl_webDataPak, path, extension, sourceList, sizeof( sourceList ) );
	count = CL_WebPak_AppendFileList( sourceList, sourceCount, listbuf, bufsize, &offset, count );

	sourceCount = FS_GetFileList( path, extension, sourceList, sizeof( sourceList ) );
	count = CL_WebPak_AppendFileList( sourceList, sourceCount, listbuf, bufsize, &offset, count );

	return count;
}

qboolean CL_WebRequestResolve( const char *virtualPath, void **outBuffer, int *outLength ) {
	char normalized[MAX_QPATH];
	qboolean normalizedValid;
	fileHandle_t file;
	int length;
	int bytesRead;

	if ( outBuffer ) {
		*outBuffer = NULL;
	}
	if ( outLength ) {
		*outLength = 0;
	}
	if ( !outBuffer ) {
		return qfalse;
	}

	normalizedValid = CL_WebPak_NormalizePath( virtualPath, normalized, sizeof( normalized ) );
	if ( normalizedValid && CL_WebPak_ReadInternal( normalized, outBuffer, outLength ) ) {
		return qtrue;
	}

	if ( !normalizedValid ) {
		return qfalse;
	}

	file = FS_INVALID_HANDLE;
	length = FS_FOpenFileRead( normalized, &file, qfalse );
	if ( length < 0 || length > CL_WEB_RESOURCE_MAX_BYTES || file == FS_INVALID_HANDLE ) {
		if ( file != FS_INVALID_HANDLE ) {
			FS_FCloseFile( file );
		}
		return qfalse;
	}

	*outBuffer = Z_Malloc( length + 1 );
	bytesRead = length > 0 ? FS_Read( *outBuffer, length, file ) : 0;
	FS_FCloseFile( file );
	if ( bytesRead != length ) {
		Z_Free( *outBuffer );
		*outBuffer = NULL;
		return qfalse;
	}
	( (byte *)*outBuffer )[length] = 0;

	if ( outLength ) {
		*outLength = length;
	}

	return qtrue;
}

qboolean CL_LauncherRequestData( const char *virtualPath, void **outBuffer, int *outLength ) {
	return CL_WebRequestResolve( virtualPath, outBuffer, outLength );
}
