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
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <pwd.h>
#include <dlfcn.h>
#include <libgen.h>

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "unix_raii.h"

//=============================================================================

/*
================
Sys_Milliseconds
================
*/
/* base time in seconds, that's our origin
   timeval:tv_sec is an int: 
   assuming this wraps every 0x7fffffff - ~68 years since the Epoch (1970) - we're safe till 2038
   using unsigned long data type to work right with Sys_XTimeToSysTime */
unsigned long sys_timeBase = 0;
/* current time in ms, using sys_timeBase as origin
   NOTE: sys_timeBase*1000 + curtime -> ms since the Epoch
     0x7fffffff ms - ~24 days
   although timeval:tv_usec is an int, I'm not sure whether it is actually used as an unsigned int
     (which would affect the wrap period) */
int Sys_Milliseconds( void )
{
	struct timeval tp;
	int curtime;

	gettimeofday( &tp, NULL );
	
	if ( !sys_timeBase )
	{
		sys_timeBase = tp.tv_sec;
		return tp.tv_usec/1000;
	}

	curtime = (tp.tv_sec - sys_timeBase) * 1000 + tp.tv_usec / 1000;
	
	return curtime;
}


char *strlwr( char *s ) {
  if ( s==NULL ) { // bk001204 - paranoia
    assert(0);
    return s;
  }
  while (*s) {
    *s = tolower(*s);
    s++;
  }
  return s; // bk001204 - duh
}


/*
==================
Sys_RandomBytes
==================
*/
qboolean Sys_RandomBytes( byte *string, int len )
{
	fnql::posix::ScopedFd fd( open( "/dev/urandom", O_RDONLY ) );

	if( !fd )
		return qfalse;

	while ( len > 0 )
	{
		const ssize_t bytesRead = read( fd.get(), string, len );
		if ( bytesRead < 0 )
		{
			if ( errno == EINTR )
			{
				continue;
			}
			return qfalse;
		}
		if ( bytesRead == 0 )
		{
			return qfalse;
		}
		string += bytesRead;
		len -= static_cast<int>( bytesRead );
	}

	return qtrue;
}


//============================================


static int Sys_ListExtFiles( const char *directory, const char *subdir, const char *extension, const char *filter, char **list, int maxfiles, int subdirs )
{
	char		search[MAX_OSPATH * 2 + MAX_QPATH + 1];
	char		filename[MAX_OSPATH * 2];
	int		nfiles;
	struct dirent	*d;
	int		extLen;
	struct stat st;
	qboolean	hasPatterns;
	const char	*x;
	qboolean	dironly;

	if ( extension[0] == '/' && extension[1] == 0 ) {
		extension = "";
		dironly = qtrue;
	} else {
		dironly = qfalse;
	}

	extLen = (int)strlen( extension );
	hasPatterns = Com_HasPatterns( extension ); // contains either '?' or '*'
	if ( hasPatterns && extension[0] == '.' && extension[1] != '\0' ) {
		extension++;
	}

	nfiles = 0;

	if ( *subdir != '\0' ) {
		Com_sprintf( search, sizeof( search ), "%s/%s", directory, subdir );
	} else {
		Com_sprintf( search, sizeof( search ), "%s", directory );
	}

	fnql::posix::ScopedDirectory fdir( opendir( search ) );
	if ( !fdir ) {
		return nfiles;
	}

	// search
	while ((d = readdir(fdir.get())) != NULL) {
		if ( search[0] != '\0' ) {
			Com_sprintf( filename, sizeof( filename ), "%s/%s", search, d->d_name );
		} else {
			Q_strncpyz( filename, d->d_name, sizeof( filename ) );
		}
		if (stat(filename, &st) == -1) {
			continue;
		}
		if (st.st_mode & S_IFDIR) {
			// handle recursion
			if ( subdirs > 0 ) {
				if ( !Q_streq( d->d_name, "." ) && !Q_streq( d->d_name, ".." ) ) {
					char subdir2[MAX_OSPATH * 2 + MAX_QPATH + 1];
					if ( *subdir != '\0' ) {
						Com_sprintf( subdir2, sizeof( subdir2 ), "%s/%s", subdir, d->d_name );
					} else {
						Q_strncpyz( subdir2, d->d_name, sizeof( subdir2 ) );
					}
					if ( nfiles >= maxfiles ) {
						break;
					}
					nfiles += Sys_ListExtFiles( directory, subdir2, extension, filter, list + nfiles, maxfiles - nfiles, subdirs - 1);
				}
			}
			if ( !dironly ) {
				continue;
			}
		} else {
			if ( dironly ) {
				continue;
			}
		}
		if ( *subdir != '\0' ) {
			Com_sprintf( filename, sizeof( filename ), "%s/%s", subdir, d->d_name );
		} else {
			Q_strncpyz( filename, d->d_name, sizeof( filename ) );
		}
		if ( filter != NULL && *filter != '\0' ) {
			if ( !Com_FilterPath( filter, filename ) ) {
				continue;
			}
		} else if ( *extension != '\0' ) {
			if ( hasPatterns ) {
				x = strrchr( d->d_name, '.' );
				if ( x == NULL || !Com_FilterExt( extension, x + 1 ) ) {
					continue;
				}
			} else {
				// check for exact extension
				const int length = strlen( d->d_name );
				if ( length < extLen || Q_stricmp( d->d_name + length - extLen, extension ) ) {
					continue;
				}
			}
		}
		if ( nfiles >= maxfiles ) {
			break;
		}
		list[ nfiles++ ] = FS_CopyString( filename );
	}

	return nfiles;
}


char** Sys_ListFiles( const char *directory, const char *extension, const char *filter, int *numfiles, int subdirs )
{
	char**	listCopy;
	char*	list[MAX_FOUND_FILES];
	int	i, nfiles;

	if ( extension == NULL ) {
		extension = "";
	}

	nfiles = Sys_ListExtFiles( directory, "", extension, filter, list, ARRAY_LEN( list ), subdirs );

	// copy list from stack, reserve extra space for NULL
	listCopy = static_cast<char **>( Z_Malloc( (nfiles + 1) * sizeof( listCopy[0] ) ) );
	for ( i = 0; i < nfiles; i++ ) {
		listCopy[i] = list[i];
	}
	listCopy[i] = NULL;


	if ( nfiles > 1 ) {
		Com_SortList( listCopy, nfiles - 1 );
		if ( nfiles > 2 ) {
			if ( Q_streq( listCopy[0], "." ) && Q_streq( listCopy[1], ".." ) ) {
				// emulate old strgtr() function sort behavior for special entries
				char* dot1 = listCopy[0];
				char* dot2 = listCopy[1];
				for ( i = 0; i < nfiles - 2; i++ ) {
					listCopy[i] = listCopy[i + 2];
				}
				listCopy[nfiles - 2] = dot1;
				listCopy[nfiles - 1] = dot2;
			}
		}
	}

	*numfiles = nfiles;
	return listCopy;
}


/*
=================
Sys_FreeFileList
=================
*/
void Sys_FreeFileList( char **list ) {
	int		i;

	if ( !list ) {
		return;
	}

	for ( i = 0 ; list[i] ; i++ ) {
		Z_Free( list[i] );
	}

	Z_Free( list );
}


/*
=============
Sys_GetFileStats
=============
*/
qboolean Sys_GetFileStats( const char *filename, fileOffset_t *size, fileTime_t *mtime, fileTime_t *ctime ) {
	struct stat s;

	if ( stat( filename, &s ) == 0 ) {
		*size = (fileOffset_t)s.st_size;
		*mtime = (fileTime_t)s.st_mtime;
		*ctime = (fileTime_t)s.st_ctime;
		return qtrue;
	} else {
		*size = 0;
		*mtime = *ctime = 0;
		return qfalse;
	}
}


/*
=================
Sys_Mkdir
=================
*/
qboolean Sys_Mkdir( const char *path )
{

	if ( mkdir( path, 0750 ) == 0 ) {
		return qtrue;
	} else {
		if ( errno == EEXIST ) {
			return qtrue;
		} else {
			return qfalse;
		}
	}
}


/*
=================
Sys_FOpen
=================
*/
FILE *Sys_FOpen( const char *ospath, const char *mode )
{
	struct stat buf;

	// check if path exists and it is not a directory
	if ( stat( ospath, &buf ) == 0 && S_ISDIR( buf.st_mode ) )
		return NULL;

	return fopen( ospath, mode );
}


/*
==============
Sys_ReplaceFile

POSIX rename atomically replaces a file within the same filesystem.
==============
*/
qboolean Sys_ReplaceFile( const char *from, const char *to ) {
	return rename( from, to ) == 0 ? qtrue : qfalse;
}


/*
==============
Sys_ResetReadOnlyAttribute
==============
*/
qboolean Sys_ResetReadOnlyAttribute( const char *ospath )
{
	return qfalse;
}


/*
=================
Sys_Pwd
=================
*/
const char *Sys_Pwd( void ) 
{
	static char pwd[ MAX_OSPATH ];

	if ( pwd[0] )
		return pwd;

	// more reliable, linux-specific
	if ( readlink( "/proc/self/exe", pwd, sizeof( pwd ) - 1 ) != -1 )
	{
		pwd[ sizeof( pwd ) - 1 ] = '\0';
		dirname( pwd );
		return pwd;
	}

	if ( !getcwd( pwd, sizeof( pwd ) ) )
	{
		pwd[0] = '\0';
	}

	return pwd;
}


/*
=================
Sys_DefaultHomePath
=================
*/
const char *Sys_DefaultHomePath( void )
{
	// Used to determine where to store user-specific files
	static char homePath[ MAX_OSPATH ];

	const char *p;

	if ( *homePath )
		return homePath;
            
	if ( (p = getenv("HOME")) != NULL ) 
	{
		Q_strncpyz( homePath, p, sizeof( homePath ) );
#ifdef MACOS_X
		Q_strcat( homePath, sizeof(homePath), "/Library/Application Support/Quake3" );
#else
		Q_strcat( homePath, sizeof( homePath ), "/.q3a" );
#endif
		if ( mkdir( homePath, 0750 ) ) 
		{
			if ( errno != EEXIST ) 
				Sys_Error( "Unable to create directory \"%s\", error is %s(%d)\n", 
					homePath, strerror( errno ), errno );
		}
		return homePath;
	}
	return ""; // assume current dir
}


/*
 ================
Sys_SteamPath
================
*/
static qboolean Sys_RegularFileExists( const char *path )
{
	struct stat info;

	if ( !path || !path[0] ) {
		return qfalse;
	}

	if ( stat( path, &info ) != 0 ) {
		return qfalse;
	}

	return S_ISREG( info.st_mode ) ? qtrue : qfalse;
}


#define SYS_STEAM_PATH_SEPARATOR_CHAR '/'
#define SYS_STEAM_PATH_ALTERNATE_SEPARATOR_CHAR '\\'
#include "../qcommon/steam_path_shared.h"
#undef SYS_STEAM_PATH_SEPARATOR_CHAR
#undef SYS_STEAM_PATH_ALTERNATE_SEPARATOR_CHAR


static qboolean Sys_TrySteamRoot( const char *steamRoot, char *out, int outSize )
{
	char normalized[MAX_OSPATH];

	if ( !steamRoot || !steamRoot[0] ) {
		return qfalse;
	}

	Q_strncpyz( normalized, steamRoot, sizeof( normalized ) );
	Sys_NormalizeSteamPath( normalized );
	return Sys_SteamAppPathFromRoot( normalized, out, outSize );
}


static qboolean Sys_TrySteamRootUnderHome( const char *home, const char *suffix, char *out, int outSize )
{
	char root[MAX_OSPATH];

	if ( !home || !home[0] || !suffix || !suffix[0] ) {
		return qfalse;
	}

	Sys_SteamJoinPath( root, sizeof( root ), home, suffix );
	return Sys_TrySteamRoot( root, out, outSize );
}


const char *Sys_SteamPath( void )
{
	static char steamPath[ MAX_OSPATH ];
	const char *home;
	const char *steamRoot;

	if ( steamPath[0] ) {
		return steamPath;
	}

	steamRoot = getenv( "STEAM_DIR" );
	if ( Sys_TrySteamRoot( steamRoot, steamPath, sizeof( steamPath ) ) ) {
		return steamPath;
	}

	steamRoot = getenv( "STEAM_HOME" );
	if ( Sys_TrySteamRoot( steamRoot, steamPath, sizeof( steamPath ) ) ) {
		return steamPath;
	}

	home = getenv( "HOME" );
	if ( home && home[0] ) {
#ifdef MACOS_X
		if ( Sys_TrySteamRootUnderHome( home, "Library/Application Support/Steam", steamPath, sizeof( steamPath ) ) ) {
			return steamPath;
		}
#else
		if ( Sys_TrySteamRootUnderHome( home, ".steam/steam", steamPath, sizeof( steamPath ) ) ) {
			return steamPath;
		}
		if ( Sys_TrySteamRootUnderHome( home, ".steam/root", steamPath, sizeof( steamPath ) ) ) {
			return steamPath;
		}
		if ( Sys_TrySteamRootUnderHome( home, ".local/share/Steam", steamPath, sizeof( steamPath ) ) ) {
			return steamPath;
		}
		if ( Sys_TrySteamRootUnderHome( home, ".var/app/com.valvesoftware.Steam/.local/share/Steam", steamPath, sizeof( steamPath ) ) ) {
			return steamPath;
		}
#endif
	}

	return steamPath;
}


/*
========================================================================

LOAD/UNLOAD DLL

========================================================================
*/


static int dll_err_count = 0;


/*
=================
Sys_LoadLibrary
=================
*/
void *Sys_LoadLibrary( const char *name )
{
	const char *ext;
	void *handle;

	if ( FS_AllowedExtension( name, qfalse, &ext ) )
	{
		Com_Error( ERR_FATAL, "Sys_LoadLibrary: Unable to load library with '%s' extension", ext );
	}

	handle = dlopen( name, RTLD_NOW );
	return handle;
}


/*
=================
Sys_UnloadLibrary
=================
*/
void Sys_UnloadLibrary( void *handle )
{
	if ( handle != NULL )
		dlclose( handle );
}


/*
=================
Sys_LoadFunction
=================
*/
void *Sys_LoadFunction( void *handle, const char *name )
{
	const char *error;
	char buf[1024];
	void *symbol;
	size_t nlen;

	if ( handle == NULL || name == NULL || *name == '\0' ) 
	{
		dll_err_count++;
		return NULL;
	}

	dlerror(); /* clear old error state */
	symbol = dlsym( handle, name );
	error = dlerror();
	if ( error != NULL )
	{
		nlen = strlen( name ) + 1;
		if ( nlen >= sizeof( buf ) )
			return NULL;
		buf[0] = '_';
		strcpy( buf+1, name );
		dlerror(); /* clear old error state */
		symbol = dlsym( handle, buf );
	}

	if ( !symbol )
		dll_err_count++;

	return symbol;
}


/*
=================
Sys_LoadFunctionErrors
=================
*/
int Sys_LoadFunctionErrors( void )
{
	int result = dll_err_count;
	dll_err_count = 0;
	return result;
}


#ifdef USE_AFFINITY_MASK
/*
=================
Sys_GetAffinityMask
=================
*/
uint64_t Sys_GetAffinityMask( void )
{
	cpu_set_t cpu_set;

	if ( sched_getaffinity( getpid(), sizeof( cpu_set ), &cpu_set ) == 0 ) {
		uint64_t mask = 0;
		int cpu;
		for ( cpu = 0; cpu < static_cast<int>( sizeof( mask ) * 8 ); cpu++ ) {
			if ( CPU_ISSET( cpu, &cpu_set ) ) {
				mask |= (1ULL << cpu);
			}
		}
		return mask;
	} else {
		return 0;
	}
}


/*
=================
Sys_SetAffinityMask
=================
*/
qboolean Sys_SetAffinityMask( const uint64_t mask )
{
	cpu_set_t cpu_set;
	int cpu;

	CPU_ZERO( &cpu_set );
	for ( cpu = 0; cpu < static_cast<int>( sizeof( mask ) * 8 ); cpu++ ) {
		if ( mask & (1ULL << cpu) ) {
			CPU_SET( cpu, &cpu_set );
		}
	}

	if ( sched_setaffinity( getpid(), sizeof( cpu_set ), &cpu_set ) == 0 ) {
		return qtrue;
	} else {
		return qfalse;
	}
}
#endif // USE_AFFINITY_MASK
