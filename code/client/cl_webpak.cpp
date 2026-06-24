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

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "client.h"

#define CL_WEB_FILE_LIST_BUFFER 4096

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

static cvar_t *cl_webPakLoaded;
static cvar_t *cl_webPakSource;
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
	return (uint16_t)( cursor[0] | ( cursor[1] << 8 ) );
}

static uint32_t CL_WebDataPak_ReadUInt32( const byte *cursor ) {
	return (uint32_t)(
		cursor[0]
		| ( cursor[1] << 8 )
		| ( cursor[2] << 16 )
		| ( cursor[3] << 24 )
	);
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

	if ( !outBuffer || !CL_WebDataPak_GetResourceView( dataPak, resourceId, &view, &length ) ) {
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
		if ( high != 0 ) {
			return qfalse;
		}

		outPath[i] = ( low == '\\' ) ? '/' : (char)low;
	}
	outPath[charCount] = '\0';

	if ( strstr( outPath, ".." ) || strstr( outPath, "::" ) || strchr( outPath, ':' ) ) {
		return qfalse;
	}

	Q_strlwr( outPath );
	return qtrue;
}

static qboolean CL_WebDataPak_BuildPathTable( clWebDataPak_t *dataPak ) {
	const byte *manifestData;
	uint32_t trailerResourceId;
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

	if ( manifestLength < 16 || CL_WebDataPak_ReadUInt32( manifestData ) + 4u > (uint32_t)manifestLength ) {
		return qfalse;
	}

	trailerResourceId = CL_WebDataPak_ReadUInt32( manifestData + manifestLength - 4 );
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

		if ( pathLength == 0 || pathLength >= MAX_QPATH || cursor + (int)( pathLength * 2 ) > manifestLength - 8 ) {
			return qfalse;
		}

		if ( !CL_WebDataPak_DecodePath( manifestData + cursor, pathLength, decodedPath, sizeof( decodedPath ) ) ) {
			return qfalse;
		}
		cursor += (int)( pathLength * 2 );
		cursor = ( cursor + 3 ) & ~3;

		if ( havePending && dataPak->pathCount < (int)dataPak->resourceCount - 1 ) {
			dataPak->paths[dataPak->pathCount].resourceId = (uint16_t)nextResourceId;
			Q_strncpyz( dataPak->paths[dataPak->pathCount].path, pendingPath, sizeof( dataPak->paths[dataPak->pathCount].path ) );
			dataPak->pathCount++;
		}

		Q_strncpyz( pendingPath, decodedPath, sizeof( pendingPath ) );
		havePending = qtrue;
	}

	if ( havePending && dataPak->pathCount < (int)dataPak->resourceCount - 1 ) {
		dataPak->paths[dataPak->pathCount].resourceId = (uint16_t)trailerResourceId;
		Q_strncpyz( dataPak->paths[dataPak->pathCount].path, pendingPath, sizeof( dataPak->paths[dataPak->pathCount].path ) );
		dataPak->pathCount++;
	}

	return dataPak->pathCount > 0 ? qtrue : qfalse;
}

static qboolean CL_WebDataPak_LoadFile( const char *pakPath, clWebDataPak_t *outDataPak ) {
	clWebDataPak_t dataPak;
	FILE *fp;
	long fileLength;
	uint32_t version;
	int tableLength;
	int aliasLength;
	int i;

	if ( !pakPath || !pakPath[0] || !outDataPak ) {
		return qfalse;
	}

	Com_Memset( &dataPak, 0, sizeof( dataPak ) );

	fp = fopen( pakPath, "rb" );
	if ( !fp ) {
		return qfalse;
	}

	fseek( fp, 0, SEEK_END );
	fileLength = ftell( fp );
	fseek( fp, 0, SEEK_SET );
	if ( fileLength <= 0 || fileLength > INT_MAX ) {
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
	version = CL_WebDataPak_ReadUInt32( dataPak.buffer );
	if ( version == 4u ) {
		dataPak.version = version;
		dataPak.resourceCount = CL_WebDataPak_ReadUInt32( dataPak.buffer + 4 );
		dataPak.aliasCount = 0;
		dataPak.headerLength = 9;
	} else if ( version == 5u ) {
		dataPak.version = version;
		dataPak.resourceCount = CL_WebDataPak_ReadUInt16( dataPak.buffer + 8 );
		dataPak.aliasCount = CL_WebDataPak_ReadUInt16( dataPak.buffer + 10 );
		dataPak.headerLength = 12;
	} else {
		CL_WebDataPak_Clear( &dataPak );
		return qfalse;
	}

	if ( dataPak.resourceCount < 2 || dataPak.resourceCount > 65535u ) {
		CL_WebDataPak_Clear( &dataPak );
		return qfalse;
	}

	tableLength = ( (int)dataPak.resourceCount + 1 ) * 6;
	aliasLength = (int)dataPak.aliasCount * 4;
	if ( dataPak.headerLength + tableLength + aliasLength > dataPak.bufferLength ) {
		CL_WebDataPak_Clear( &dataPak );
		return qfalse;
	}

	for ( i = 0; i <= (int)dataPak.resourceCount; i++ ) {
		const byte *entry;
		uint32_t offset;

		entry = CL_WebDataPak_EntryPointer( &dataPak, i );
		offset = CL_WebDataPak_ReadUInt32( entry + 2 );
		if ( offset > (uint32_t)dataPak.bufferLength ) {
			CL_WebDataPak_Clear( &dataPak );
			return qfalse;
		}

		if ( i > 0 ) {
			const byte *prevEntry;
			uint32_t previousOffset;

			prevEntry = CL_WebDataPak_EntryPointer( &dataPak, i - 1 );
			previousOffset = CL_WebDataPak_ReadUInt32( prevEntry + 2 );
			if ( offset < previousOffset ) {
				CL_WebDataPak_Clear( &dataPak );
				return qfalse;
			}
		}
	}

	if ( !CL_WebDataPak_BuildPathTable( &dataPak ) ) {
		CL_WebDataPak_Clear( &dataPak );
		return qfalse;
	}

	dataPak.loaded = qtrue;
	Q_strncpyz( dataPak.sourcePath, pakPath, sizeof( dataPak.sourcePath ) );
	*outDataPak = dataPak;
	return qtrue;
}

static qboolean CL_WebDataPak_Load( const char *pakPath ) {
	clWebDataPak_t dataPak;

	if ( !CL_WebDataPak_LoadFile( pakPath, &dataPak ) ) {
		return qfalse;
	}

	CL_WebDataPak_Clear( &cl_webDataPak );
	cl_webDataPak = dataPak;
	return qtrue;
}

static qboolean CL_WebDataPak_Fetch( const char *normalizedPath, void **outBuffer, int *outLength ) {
	int i;

	if ( !cl_webDataPak.loaded || !normalizedPath || !normalizedPath[0] ) {
		return qfalse;
	}

	for ( i = 0; i < cl_webDataPak.pathCount; i++ ) {
		if ( !Q_stricmp( cl_webDataPak.paths[i].path, normalizedPath ) ) {
			return CL_WebDataPak_ReadResource( &cl_webDataPak, cl_webDataPak.paths[i].resourceId, outBuffer, outLength );
		}
	}

	return qfalse;
}

static const char *CL_WebPak_StripProtocol( const char *virtualPath ) {
	const char *pathStart;
	const char *separator;

	if ( !virtualPath ) {
		return NULL;
	}

	pathStart = virtualPath;
	separator = strstr( virtualPath, "://" );
	if ( separator ) {
		pathStart = separator + 3;
		separator = strchr( pathStart, '/' );
		if ( separator ) {
			pathStart = separator + 1;
		}
	}

	while ( *pathStart == '/' || *pathStart == '\\' ) {
		pathStart++;
	}

	return pathStart;
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

	fseek( fp, 0, SEEK_END );
	fileLength = ftell( fp );
	fseek( fp, 0, SEEK_SET );
	if ( fileLength < 0 || fileLength > INT_MAX ) {
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

	if ( CL_WebDataPak_Fetch( normalizedPath, outBuffer, outLength ) ) {
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
	if ( offset + length + 1 >= bufsize ) {
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

static int CL_WebDataPak_GetFileList( const char *path, const char *extension, char *listbuf, int bufsize ) {
	int pathLength;
	int extensionLength;
	int offset;
	int count;
	int i;

	if ( !cl_webDataPak.loaded || !path || !extension || !listbuf || bufsize <= 0 ) {
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

	for ( i = 0; i < cl_webDataPak.pathCount; i++ ) {
		const char *entryPath;
		const char *name;
		int length;

		entryPath = cl_webDataPak.paths[i].path;
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

	if ( !cl_webPakLoaded ) {
		CL_WebPak_RegisterCvars();
	}

	CL_WebPak_SetCvarIfChanged( "cl_webPakLoaded", loaded ? "1" : "0" );
	CL_WebPak_SetCvarIfChanged( "cl_webPakSource", loaded ? cl_webDataPak.sourcePath : "" );
	CL_WebPak_SetCvarIfChanged( "ui_resourceBridgeProvider", loaded ? "Awesomium DataPak web.pak" : "Loose filesystem fallback" );
	CL_WebPak_SetCvarIfChanged( "ui_resourceBridgePolicy", loaded ? "retail-assets-external" : "webpak-unavailable" );
	CL_WebPak_SetCvarIfChanged( "ui_resourceBridgeParityScope", "retail web.pak resource bridge" );
	CL_WebPak_SetCvarIfChanged( "ui_resourceBridgeParityReason", loaded ? "external retail web.pak mounted" : "external retail web.pak not found" );
	CL_WebPak_SetCvarIfChanged( "ui_resourceBridgeSteamDataSourceSubset", "avatar-only SteamDataSource" );
	CL_WebPak_SetCvarIfChanged( "ui_resourceBridgeSteamDataSourceNativeGap", "missing non-avatar SteamDataSource owner" );
	CL_WebPak_SetCvarIfChanged( "ui_resourceBridgeSteamDataSourceFallbackOwner", "QLResourceInterceptor launcher/web fallback" );
}

void CL_WebPak_Init( void ) {
	static const char *pathCvars[] = { "fs_homepath", "fs_basepath", "fs_steampath" };
	char rootPath[MAX_OSPATH];
	char pakPath[MAX_OSPATH];
	int i;

	CL_WebPak_RegisterCvars();
	CL_WebDataPak_Clear( &cl_webDataPak );

	for ( i = 0; i < (int)ARRAY_LEN( pathCvars ); i++ ) {
		Cvar_VariableStringBuffer( pathCvars[i], rootPath, sizeof( rootPath ) );
		CL_WebPak_BuildStandalonePath( rootPath, "web.pak", pakPath, sizeof( pakPath ) );
		if ( CL_WebDataPak_Load( pakPath ) ) {
			Com_Printf( "web.pak datapack mounted from %s\n", pakPath );
			CL_WebPak_RefreshCvars();
			return;
		}
	}

	CL_WebPak_RefreshCvars();
}

void CL_WebPak_Shutdown( void ) {
	CL_WebDataPak_Clear( &cl_webDataPak );
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

	sourceCount = CL_WebDataPak_GetFileList( path, extension, sourceList, sizeof( sourceList ) );
	count = CL_WebPak_AppendFileList( sourceList, sourceCount, listbuf, bufsize, &offset, count );

	sourceCount = FS_GetFileList( path, extension, sourceList, sizeof( sourceList ) );
	count = CL_WebPak_AppendFileList( sourceList, sourceCount, listbuf, bufsize, &offset, count );

	return count;
}

qboolean CL_WebRequestResolve( const char *virtualPath, void **outBuffer, int *outLength ) {
	char normalized[MAX_QPATH];
	qboolean normalizedValid;
	void *fsBuffer;
	int length;

	if ( outBuffer ) {
		*outBuffer = NULL;
	}
	if ( outLength ) {
		*outLength = 0;
	}

	normalizedValid = CL_WebPak_NormalizePath( virtualPath, normalized, sizeof( normalized ) );
	if ( normalizedValid && CL_WebPak_ReadInternal( normalized, outBuffer, outLength ) ) {
		return qtrue;
	}

	if ( !normalizedValid ) {
		return qfalse;
	}

	length = FS_ReadFile( normalized, &fsBuffer );
	if ( length < 0 ) {
		return qfalse;
	}

	*outBuffer = Z_Malloc( length + 1 );
	if ( fsBuffer && length > 0 ) {
		Com_Memcpy( *outBuffer, fsBuffer, length );
	}
	( (byte *)*outBuffer )[length] = 0;
	FS_FreeFile( fsBuffer );

	if ( outLength ) {
		*outLength = length;
	}

	return qtrue;
}

qboolean CL_LauncherRequestData( const char *virtualPath, void **outBuffer, int *outLength ) {
	return CL_WebRequestResolve( virtualPath, outBuffer, outLength );
}
