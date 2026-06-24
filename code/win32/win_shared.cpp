/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#ifdef __cplusplus
extern "C" {
#endif
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "win_local.h"
#ifdef __cplusplus
}
#endif
#include "win_raii.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <io.h>
#include <conio.h>
#include <intrin.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
================
Sys_Milliseconds
================
*/
int Sys_Milliseconds( void )
{
	static qboolean	initialized = qfalse;
	static DWORD sys_timeBase;
	int	sys_curtime;

	if ( !initialized ) {
		sys_timeBase = timeGetTime();
		initialized = qtrue;
	}

	sys_curtime = timeGetTime() - sys_timeBase;

	return sys_curtime;
}


/*
================
Sys_RandomBytes
================
*/
qboolean Sys_RandomBytes( byte *string, int len )
{
	fnql::win::ScopedCryptProvider prov;

	if( !CryptAcquireContext( prov.receive(), NULL, NULL,
		PROV_RSA_FULL, CRYPT_VERIFYCONTEXT ) )  {

		return qfalse;
	}

	if( !CryptGenRandom( prov.get(), len, (BYTE *)string ) )  {
		return qfalse;
	}
	return qtrue;
}


#ifdef UNICODE
LPWSTR AtoW( const char *s ) 
{
	static WCHAR buffer[MAXPRINTMSG*2];
	MultiByteToWideChar( CP_ACP, 0, s, strlen( s ) + 1, (LPWSTR) buffer, ARRAYSIZE( buffer ) );
	return buffer;
}

const char *WtoA( const LPWSTR s ) 
{
	static char buffer[MAXPRINTMSG*2];
	WideCharToMultiByte( CP_ACP, 0, s, -1, buffer, ARRAYSIZE( buffer ), NULL, NULL );
	return buffer;
}
#endif


/*
================
Sys_DefaultHomePath
================
*/
const char *Sys_DefaultHomePath( void ) 
{
#ifdef USE_PROFILES
	char szPath[MAX_PATH];
	static char path[MAX_OSPATH];
	using SHGetFolderPathAProc = HRESULT ( WINAPI * )( HWND, int, HANDLE, DWORD, LPSTR );
	fnql::win::ScopedLibrary shfolder( LoadLibraryA( "shfolder.dll" ) );
	
	if ( !shfolder ) {
		Com_Printf("Unable to load SHFolder.dll\n");
		return NULL;
	}

	const auto qSHGetFolderPath = reinterpret_cast<SHGetFolderPathAProc>( GetProcAddress( shfolder.get(), "SHGetFolderPathA" ) );
	if ( qSHGetFolderPath == nullptr )
	{
		Com_Printf("Unable to find SHGetFolderPath in SHFolder.dll\n");
		return NULL;
	}

	if( !SUCCEEDED( qSHGetFolderPath( NULL, CSIDL_APPDATA,
		NULL, 0, szPath ) ) )
	{
		Com_Printf("Unable to detect CSIDL_APPDATA\n");
		return NULL;
	}
	Q_strncpyz( path, szPath, sizeof(path) );
	Q_strcat( path, sizeof(path), "\\Quake3" );
	if( !CreateDirectoryA( path, NULL ) )
	{
		if( GetLastError() != ERROR_ALREADY_EXISTS )
		{
			Com_Printf("Unable to create directory \"%s\"\n", path);
			return NULL;
		}
	}
	return path;
#else
    return NULL;
#endif
}


/*
================
Sys_SteamPath
================
*/
static qboolean Sys_RegularFileExists( const char *path )
{
	DWORD attributes;

	if ( !path || !path[0] ) {
		return qfalse;
	}

	attributes = GetFileAttributesA( path );
	return ( attributes != INVALID_FILE_ATTRIBUTES && !( attributes & FILE_ATTRIBUTE_DIRECTORY ) ) ? qtrue : qfalse;
}


#define SYS_STEAM_PATH_SEPARATOR_CHAR '\\'
#define SYS_STEAM_PATH_ALTERNATE_SEPARATOR_CHAR '/'
#include "../qcommon/steam_path_shared.h"
#undef SYS_STEAM_PATH_SEPARATOR_CHAR
#undef SYS_STEAM_PATH_ALTERNATE_SEPARATOR_CHAR


static qboolean Sys_QueryRegistryString( HKEY root, const char *keyPath, REGSAM viewFlags, const char *valueName, char *out, int outSize )
{
	fnql::win::ScopedRegistryKey key;
	DWORD type;
	DWORD dataSize;
	char buffer[MAX_OSPATH];

	if ( RegOpenKeyExA( root, keyPath, 0, KEY_QUERY_VALUE | viewFlags, key.receive() ) != ERROR_SUCCESS ) {
		return qfalse;
	}

	dataSize = sizeof( buffer );
	if ( RegQueryValueExA( key.get(), valueName, NULL, &type, (LPBYTE)buffer, &dataSize ) != ERROR_SUCCESS ) {
		return qfalse;
	}

	if ( type != REG_SZ && type != REG_EXPAND_SZ ) {
		return qfalse;
	}

	buffer[sizeof( buffer ) - 1] = '\0';
	if ( type == REG_EXPAND_SZ ) {
		char expanded[MAX_OSPATH];
		DWORD expandedSize;

		expandedSize = ExpandEnvironmentStringsA( buffer, expanded, sizeof( expanded ) );
		if ( expandedSize > 0 && expandedSize < sizeof( expanded ) ) {
			Q_strncpyz( buffer, expanded, sizeof( buffer ) );
		}
	}

	Q_strncpyz( out, buffer, outSize );
	Sys_NormalizeSteamPath( out );
	return out[0] ? qtrue : qfalse;
}


static qboolean Sys_FindSteamAppInstallFromRegistry( char *out, int outSize )
{
	const char *appKey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App " STEAMPATH_APPID;
	const HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
	const REGSAM views[] = { KEY_WOW64_32KEY, KEY_WOW64_64KEY, 0 };
	size_t rootIndex;
	size_t viewIndex;
	char candidate[MAX_OSPATH];

	for ( rootIndex = 0; rootIndex < ARRAY_LEN( roots ); rootIndex++ ) {
		for ( viewIndex = 0; viewIndex < ARRAY_LEN( views ); viewIndex++ ) {
			if ( Sys_QueryRegistryString( roots[rootIndex], appKey, views[viewIndex], "InstallLocation", candidate, sizeof( candidate ) )
				&& Sys_CopyValidQuakeLiveSteamInstall( candidate, out, outSize ) ) {
				return qtrue;
			}
		}
	}

	return qfalse;
}


static qboolean Sys_FindSteamRootFromRegistry( char *out, int outSize )
{
	const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
	const REGSAM views[] = { 0, KEY_WOW64_32KEY, KEY_WOW64_64KEY };
	const char *values[] = { "SteamPath", "InstallPath" };
	size_t rootIndex;
	size_t viewIndex;
	size_t valueIndex;

	for ( rootIndex = 0; rootIndex < ARRAY_LEN( roots ); rootIndex++ ) {
		for ( viewIndex = 0; viewIndex < ARRAY_LEN( views ); viewIndex++ ) {
			for ( valueIndex = 0; valueIndex < ARRAY_LEN( values ); valueIndex++ ) {
				if ( Sys_QueryRegistryString( roots[rootIndex], "Software\\Valve\\Steam", views[viewIndex], values[valueIndex], out, outSize ) ) {
					return qtrue;
				}
			}
		}
	}

	return qfalse;
}


static qboolean Sys_FindSteamRootFromKnownPaths( char *out, int outSize )
{
	const char *programFilesX86;
	const char *programFiles;
	char candidate[MAX_OSPATH];

	programFilesX86 = getenv( "ProgramFiles(x86)" );
	if ( programFilesX86 && programFilesX86[0] ) {
		Sys_SteamJoinPath( candidate, sizeof( candidate ), programFilesX86, "Steam" );
		if ( Sys_SteamAppPathFromRoot( candidate, out, outSize ) ) {
			return qtrue;
		}
	}

	programFiles = getenv( "ProgramFiles" );
	if ( programFiles && programFiles[0] ) {
		Sys_SteamJoinPath( candidate, sizeof( candidate ), programFiles, "Steam" );
		if ( Sys_SteamAppPathFromRoot( candidate, out, outSize ) ) {
			return qtrue;
		}
	}

	return Sys_SteamAppPathFromRoot( "C:\\Program Files (x86)\\Steam", out, outSize );
}


const char *Sys_SteamPath( void )
{
	static char steamPath[ MAX_OSPATH ];
	char steamRoot[MAX_OSPATH];

	if ( steamPath[0] ) {
		return steamPath;
	}

	if ( Sys_FindSteamAppInstallFromRegistry( steamPath, sizeof( steamPath ) ) ) {
		return steamPath;
	}

	if ( Sys_FindSteamRootFromRegistry( steamRoot, sizeof( steamRoot ) )
		&& Sys_SteamAppPathFromRoot( steamRoot, steamPath, sizeof( steamPath ) ) ) {
		return steamPath;
	}

	Sys_FindSteamRootFromKnownPaths( steamPath, sizeof( steamPath ) );
	return steamPath;
}


/*
================
Sys_SetAffinityMask
================
*/
#ifdef USE_AFFINITY_MASK
static HANDLE hCurrentProcess = 0;

uint64_t Sys_GetAffinityMask( void )
{
	DWORD_PTR dwProcessAffinityMask;
	DWORD_PTR dwSystemAffinityMask;

	if ( hCurrentProcess == 0 )	{
		hCurrentProcess = GetCurrentProcess();
	}

	if ( GetProcessAffinityMask( hCurrentProcess, &dwProcessAffinityMask, &dwSystemAffinityMask ) )	{
		return (uint64_t)dwProcessAffinityMask;
	}

	return 0;
}


qboolean Sys_SetAffinityMask( const uint64_t mask )
{
	DWORD_PTR dwProcessAffinityMask = (DWORD_PTR)mask;

	if ( hCurrentProcess == 0 ) {
		hCurrentProcess = GetCurrentProcess();
	}

	if ( SetProcessAffinityMask( hCurrentProcess, dwProcessAffinityMask ) )	{
		//Sleep( 0 );
		return qtrue;
	}

	return qfalse;
}
#endif // USE_AFFINITY_MASK

#ifdef __cplusplus
}
#endif
