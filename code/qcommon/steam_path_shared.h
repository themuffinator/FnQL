/*
===========================================================================
Shared Steam library discovery helpers for FnQL platform bootstrap.
===========================================================================
*/

#ifndef FNQL_STEAM_PATH_SHARED_H
#define FNQL_STEAM_PATH_SHARED_H

#ifndef SYS_STEAM_PATH_SEPARATOR_CHAR
#error "SYS_STEAM_PATH_SEPARATOR_CHAR must be defined before including steam_path_shared.h"
#endif

#ifndef SYS_STEAM_PATH_ALTERNATE_SEPARATOR_CHAR
#error "SYS_STEAM_PATH_ALTERNATE_SEPARATOR_CHAR must be defined before including steam_path_shared.h"
#endif

typedef enum {
	SYS_VDF_TOKEN_NONE,
	SYS_VDF_TOKEN_STRING,
	SYS_VDF_TOKEN_OPEN_BRACE,
	SYS_VDF_TOKEN_CLOSE_BRACE
} sysVdfToken_t;


static qboolean Sys_SteamPathSeparator( char c )
{
	return ( c == SYS_STEAM_PATH_SEPARATOR_CHAR || c == SYS_STEAM_PATH_ALTERNATE_SEPARATOR_CHAR ) ? qtrue : qfalse;
}


static void Sys_NormalizeSteamPath( char *path )
{
	char *cursor;
	size_t len;

	if ( !path || !path[0] ) {
		return;
	}

	for ( cursor = path; *cursor; cursor++ ) {
		if ( *cursor == SYS_STEAM_PATH_ALTERNATE_SEPARATOR_CHAR ) {
			*cursor = SYS_STEAM_PATH_SEPARATOR_CHAR;
		}
	}

	len = strlen( path );
	while ( len > 0 && Sys_SteamPathSeparator( path[len - 1] ) ) {
		path[--len] = '\0';
	}
}


static void Sys_SteamJoinPath( char *out, int outSize, const char *base, const char *leaf )
{
	char separator[2];

	if ( !out || outSize <= 0 ) {
		return;
	}

	out[0] = '\0';
	if ( !base || !base[0] ) {
		return;
	}

	Q_strncpyz( out, base, outSize );
	Sys_NormalizeSteamPath( out );

	if ( leaf && leaf[0] ) {
		if ( out[0] && !Sys_SteamPathSeparator( out[strlen( out ) - 1] ) ) {
			separator[0] = SYS_STEAM_PATH_SEPARATOR_CHAR;
			separator[1] = '\0';
			Q_strcat( out, outSize, separator );
		}
		Q_strcat( out, outSize, leaf );
		Sys_NormalizeSteamPath( out );
	}
}


static void Sys_SteamJoinSteamAppsPath( char *out, int outSize, const char *libraryPath, const char *leaf )
{
	char steamAppsPath[MAX_OSPATH];

	Sys_SteamJoinPath( steamAppsPath, sizeof( steamAppsPath ), libraryPath, "steamapps" );
	Sys_SteamJoinPath( out, outSize, steamAppsPath, leaf );
}


static qboolean Sys_QuakeLiveSteamInstallIsValid( const char *installPath )
{
	char basePath[MAX_OSPATH];
	char assetPath[MAX_OSPATH];

	Sys_SteamJoinPath( basePath, sizeof( basePath ), installPath, BASEGAME );
	Sys_SteamJoinPath( assetPath, sizeof( assetPath ), basePath, "pak00.pk3" );
	if ( Sys_RegularFileExists( assetPath ) ) {
		return qtrue;
	}

	Sys_SteamJoinPath( assetPath, sizeof( assetPath ), basePath, "default.cfg" );
	return Sys_RegularFileExists( assetPath );
}


static qboolean Sys_CopyValidQuakeLiveSteamInstall( const char *path, char *out, int outSize )
{
	char normalized[MAX_OSPATH];

	if ( !path || !path[0] ) {
		return qfalse;
	}

	Q_strncpyz( normalized, path, sizeof( normalized ) );
	Sys_NormalizeSteamPath( normalized );

	if ( !Sys_QuakeLiveSteamInstallIsValid( normalized ) ) {
		return qfalse;
	}

	Q_strncpyz( out, normalized, outSize );
	return qtrue;
}


static char *Sys_ReadTextFile( const char *path )
{
	FILE *file;
	long length;
	char *buffer;
	size_t bytesRead;

	file = fopen( path, "rb" );
	if ( !file ) {
		return NULL;
	}

	if ( fseek( file, 0, SEEK_END ) != 0 ) {
		fclose( file );
		return NULL;
	}

	length = ftell( file );
	if ( length < 0 ) {
		fclose( file );
		return NULL;
	}

	if ( fseek( file, 0, SEEK_SET ) != 0 ) {
		fclose( file );
		return NULL;
	}

	buffer = (char *)malloc( (size_t)length + 1 );
	if ( !buffer ) {
		fclose( file );
		return NULL;
	}

	bytesRead = fread( buffer, 1, (size_t)length, file );
	fclose( file );
	buffer[bytesRead] = '\0';
	return buffer;
}


static const char *Sys_ReadVdfToken( const char *cursor, char *token, int tokenSize, sysVdfToken_t *type )
{
	int len;

	*type = SYS_VDF_TOKEN_NONE;
	if ( tokenSize > 0 ) {
		token[0] = '\0';
	}

	while ( *cursor && (unsigned char)*cursor <= ' ' ) {
		cursor++;
	}

	if ( !*cursor ) {
		return cursor;
	}

	if ( *cursor == '{' ) {
		*type = SYS_VDF_TOKEN_OPEN_BRACE;
		return cursor + 1;
	}

	if ( *cursor == '}' ) {
		*type = SYS_VDF_TOKEN_CLOSE_BRACE;
		return cursor + 1;
	}

	*type = SYS_VDF_TOKEN_STRING;
	len = 0;

	if ( *cursor == '"' ) {
		cursor++;
		while ( *cursor && *cursor != '"' ) {
			if ( *cursor == '\\' && cursor[1] ) {
				if ( len < tokenSize - 1 ) {
					token[len++] = cursor[1];
				}
				cursor += 2;
				continue;
			}
			if ( len < tokenSize - 1 ) {
				token[len++] = *cursor;
			}
			cursor++;
		}
		if ( *cursor == '"' ) {
			cursor++;
		}
	} else {
		while ( *cursor && (unsigned char)*cursor > ' ' && *cursor != '{' && *cursor != '}' ) {
			if ( len < tokenSize - 1 ) {
				token[len++] = *cursor;
			}
			cursor++;
		}
	}

	if ( tokenSize > 0 ) {
		token[len] = '\0';
	}
	return cursor;
}


static qboolean Sys_SteamManifestInstallDir( const char *libraryPath, char *installDir, int installDirSize )
{
	char manifestPath[MAX_OSPATH];
	char token[MAX_OSPATH];
	char *manifestText;
	const char *cursor;
	sysVdfToken_t type;
	qboolean found;

	Sys_SteamJoinSteamAppsPath( manifestPath, sizeof( manifestPath ), libraryPath, "appmanifest_" STEAMPATH_APPID ".acf" );
	manifestText = Sys_ReadTextFile( manifestPath );
	if ( !manifestText ) {
		return qfalse;
	}

	found = qfalse;
	cursor = manifestText;
	while ( *cursor ) {
		cursor = Sys_ReadVdfToken( cursor, token, sizeof( token ), &type );
		if ( type != SYS_VDF_TOKEN_STRING || Q_stricmp( token, "installdir" ) ) {
			continue;
		}

		cursor = Sys_ReadVdfToken( cursor, installDir, installDirSize, &type );
		found = ( type == SYS_VDF_TOKEN_STRING && installDir[0] ) ? qtrue : qfalse;
		break;
	}

	free( manifestText );
	return found;
}


static qboolean Sys_SteamAppPathFromLibrary( const char *libraryPath, char *out, int outSize )
{
	char installDir[MAX_OSPATH];
	char commonPath[MAX_OSPATH];
	char candidate[MAX_OSPATH];

	if ( !libraryPath || !libraryPath[0] ) {
		return qfalse;
	}

	Sys_SteamJoinSteamAppsPath( commonPath, sizeof( commonPath ), libraryPath, "common" );
	if ( Sys_SteamManifestInstallDir( libraryPath, installDir, sizeof( installDir ) ) ) {
		Sys_SteamJoinPath( candidate, sizeof( candidate ), commonPath, installDir );
		if ( Sys_CopyValidQuakeLiveSteamInstall( candidate, out, outSize ) ) {
			return qtrue;
		}
	}

	Sys_SteamJoinPath( candidate, sizeof( candidate ), commonPath, STEAMPATH_NAME );
	return Sys_CopyValidQuakeLiveSteamInstall( candidate, out, outSize );
}


static qboolean Sys_SteamAppPathFromLibraryFolders( const char *steamRoot, char *out, int outSize )
{
	char libraryFile[MAX_OSPATH];
	char libraryPath[MAX_OSPATH];
	char token[MAX_OSPATH];
	char *libraryText;
	const char *cursor;
	sysVdfToken_t type;
	int depth;
	int appsDepth;
	qboolean nextOpenIsApps;
	qboolean inApps;
	qboolean found;

	Sys_SteamJoinSteamAppsPath( libraryFile, sizeof( libraryFile ), steamRoot, "libraryfolders.vdf" );
	libraryText = Sys_ReadTextFile( libraryFile );
	if ( !libraryText ) {
		return qfalse;
	}

	libraryPath[0] = '\0';
	cursor = libraryText;
	depth = 0;
	appsDepth = -1;
	nextOpenIsApps = qfalse;
	inApps = qfalse;
	found = qfalse;

	while ( *cursor && !found ) {
		cursor = Sys_ReadVdfToken( cursor, token, sizeof( token ), &type );

		if ( type == SYS_VDF_TOKEN_OPEN_BRACE ) {
			depth++;
			if ( nextOpenIsApps ) {
				appsDepth = depth;
				inApps = qtrue;
				nextOpenIsApps = qfalse;
			}
			continue;
		}

		if ( type == SYS_VDF_TOKEN_CLOSE_BRACE ) {
			if ( inApps && depth == appsDepth ) {
				inApps = qfalse;
				appsDepth = -1;
			}
			if ( depth > 0 ) {
				depth--;
			}
			continue;
		}

		if ( type != SYS_VDF_TOKEN_STRING ) {
			continue;
		}

		if ( inApps ) {
			if ( !Q_stricmp( token, STEAMPATH_APPID ) && libraryPath[0] ) {
				found = Sys_SteamAppPathFromLibrary( libraryPath, out, outSize );
			}
			continue;
		}

		if ( !Q_stricmp( token, "path" ) ) {
			cursor = Sys_ReadVdfToken( cursor, libraryPath, sizeof( libraryPath ), &type );
			if ( type == SYS_VDF_TOKEN_STRING ) {
				Sys_NormalizeSteamPath( libraryPath );
			} else {
				libraryPath[0] = '\0';
			}
			continue;
		}

		if ( !Q_stricmp( token, "apps" ) ) {
			nextOpenIsApps = qtrue;
		}
	}

	free( libraryText );
	return found;
}


static qboolean Sys_SteamAppPathFromRoot( const char *steamRoot, char *out, int outSize )
{
	char commonPath[MAX_OSPATH];
	char candidate[MAX_OSPATH];

	if ( !steamRoot || !steamRoot[0] ) {
		return qfalse;
	}

	if ( Sys_SteamAppPathFromLibraryFolders( steamRoot, out, outSize ) ) {
		return qtrue;
	}

	if ( Sys_SteamAppPathFromLibrary( steamRoot, out, outSize ) ) {
		return qtrue;
	}

	Sys_SteamJoinSteamAppsPath( commonPath, sizeof( commonPath ), steamRoot, "common" );
	Sys_SteamJoinPath( candidate, sizeof( candidate ), commonPath, STEAMPATH_NAME );
	return Sys_CopyValidQuakeLiveSteamInstall( candidate, out, outSize );
}

#endif // FNQL_STEAM_PATH_SHARED_H
