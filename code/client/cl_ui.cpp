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

extern "C" {
#include "client.h"

#include "../botlib/botlib.h"
}

#include "client_cpp.h"
#include "ql_font_bridge.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>

#if defined( _WIN32 )
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using fnql::FileReadObject;
using fnql::FileWriteObject;
using fnql::OpenHomeFileRead;
using fnql::ScopedFileHandle;
using fnql::ScopedZoneMemory;

extern botlib_export_t	*botlib_export;

vm_t *uivm = nullptr;

namespace {

struct LANServerList {
	serverInfo_t *servers;
	int *count;
	int capacity;
};

static LANServerList LAN_GetServerList( int source ) {
	switch ( source ) {
		case AS_LOCAL:
			return { cls.localServers, &cls.numlocalservers, MAX_OTHER_SERVERS };
		case AS_MPLAYER:
		case AS_GLOBAL:
			return { cls.globalServers, &cls.numglobalservers, MAX_GLOBAL_SERVERS };
		case AS_FAVORITES:
			return { cls.favoriteServers, &cls.numfavoriteservers, MAX_OTHER_SERVERS };
		default:
			return { nullptr, nullptr, 0 };
	}
}

static serverInfo_t *LAN_GetServerAt( int source, int index ) {
	const LANServerList list = LAN_GetServerList( source );

	if ( !list.servers || index < 0 || index >= list.capacity ) {
		return nullptr;
	}

	return &list.servers[index];
}

static int LAN_CompareInt( int lhs, int rhs ) {
	return ( lhs > rhs ) - ( lhs < rhs );
}

static int LAN_ApplySortDirection( int result, int sortDir ) {
	if ( !sortDir ) {
		return result;
	}

	if ( result < 0 ) {
		return 1;
	}
	if ( result > 0 ) {
		return -1;
	}
	return 0;
}

static int RoundToInt( float value ) {
	return static_cast<int>( value + 0.5f );
}

} // namespace

/*
====================
GetClientState
====================
*/
static void GetClientState( uiClientState_t *state ) {
	state->connectPacketCount = clc.connectPacketCount;
	state->connState = cls.state;
	Q_strncpyz( state->servername, cls.servername, sizeof( state->servername ) );
	Q_strncpyz( state->updateInfoString, cls.updateInfoString, sizeof( state->updateInfoString ) );
	Q_strncpyz( state->messageString, clc.serverMessage, sizeof( state->messageString ) );
	state->clientNum = cl.snap.ps.clientNum;
}


/*
====================
LAN_LoadCachedServers
====================
*/
static void LAN_LoadCachedServers( void ) {
	int size, file_size;

	cls.numglobalservers = cls.numfavoriteservers = 0;
	cls.numGlobalServerAddresses = 0;

	ScopedFileHandle file;
	file_size = OpenHomeFileRead( "servercache.dat", file );
	if ( file_size < static_cast<int>( 3 * sizeof( int ) ) ) {
		return;
	}

	FileReadObject( file.get(), cls.numglobalservers );
	FileReadObject( file.get(), cls.numfavoriteservers );
	FileReadObject( file.get(), size );

	if ( size == sizeof(cls.globalServers) + sizeof(cls.favoriteServers) ) {
		FileReadObject( file.get(), cls.globalServers );
		FileReadObject( file.get(), cls.favoriteServers );
	} else {
		cls.numglobalservers = cls.numfavoriteservers = 0;
		cls.numGlobalServerAddresses = 0;
	}
}


/*
====================
LAN_SaveServersToCache
====================
*/
static void LAN_SaveServersToCache( void ) {
	int size;

	ScopedFileHandle file( FS_FOpenFileWrite( "servercache.dat" ) );
	if ( !file )
		return;

	FileWriteObject( file.get(), cls.numglobalservers );
	FileWriteObject( file.get(), cls.numfavoriteservers );
	size = sizeof(cls.globalServers) + sizeof(cls.favoriteServers);
	FileWriteObject( file.get(), size );
	FileWriteObject( file.get(), cls.globalServers );
	FileWriteObject( file.get(), cls.favoriteServers );
}


/*
====================
LAN_ResetPings
====================
*/
static void LAN_ResetPings(int source) {
	const LANServerList list = LAN_GetServerList( source );

	if ( !list.servers ) {
		return;
	}

	for ( int i = 0; i < list.capacity; i++ ) {
		list.servers[i].ping = -1;
	}
}


/*
====================
LAN_AddServer
====================
*/
static int LAN_AddServer(int source, const char *name, const char *address) {
	const LANServerList list = LAN_GetServerList( source );
	int i;
	netadr_t adr;

	if ( list.servers && list.count && *list.count < list.capacity ) {
		NET_StringToAdr( address, &adr, NA_UNSPEC );
		for ( i = 0; i < *list.count; i++ ) {
			if (NET_CompareAdr(&list.servers[i].adr, &adr)) {
				break;
			}
		}
		if (i >= *list.count) {
			list.servers[*list.count].adr = adr;
			Q_strncpyz(list.servers[*list.count].hostName, name, sizeof(list.servers[*list.count].hostName));
			list.servers[*list.count].visible = qtrue;
			(*list.count)++;
			return 1;
		}
		return 0;
	}
	return -1;
}


/*
====================
LAN_RemoveServer
====================
*/
static void LAN_RemoveServer(int source, const char *addr) {
	const LANServerList list = LAN_GetServerList( source );
	int i;

	if ( list.servers && list.count ) {
		netadr_t comp;
		NET_StringToAdr( addr, &comp, NA_UNSPEC );
		for (i = 0; i < *list.count; i++) {
			if (NET_CompareAdr( &comp, &list.servers[i].adr)) {
				std::move( list.servers + i + 1, list.servers + *list.count, list.servers + i );
				(*list.count)--;
				break;
			}
		}
	}
}


/*
====================
LAN_GetServerCount
====================
*/
static int LAN_GetServerCount( int source ) {
	const LANServerList list = LAN_GetServerList( source );

	return list.count ? *list.count : 0;
}


/*
====================
LAN_GetLocalServerAddressString
====================
*/
static void LAN_GetServerAddressString( int source, int n, char *buf, int buflen ) {
	const serverInfo_t *server = LAN_GetServerAt( source, n );

	if ( server ) {
		Q_strncpyz(buf, NET_AdrToStringwPort( &server->adr) , buflen );
		return;
	}
	buf[0] = '\0';
}


/*
====================
LAN_GetServerInfo
====================
*/
static void LAN_GetServerInfo( int source, int n, char *buf, int buflen ) {
	std::array<char, MAX_STRING_CHARS> info;
	serverInfo_t *server = LAN_GetServerAt( source, n );
	info[0] = '\0';
	if (server && buf) {
		buf[0] = '\0';
		Info_SetValueForKey( info.data(), "hostname", server->hostName);
		Info_SetValueForKey( info.data(), "mapname", server->mapName);
		Info_SetValueForKey( info.data(), "clients", va("%i",server->clients));
		Info_SetValueForKey( info.data(), "sv_maxclients", va("%i",server->maxClients));
		Info_SetValueForKey( info.data(), "ping", va("%i",server->ping));
		Info_SetValueForKey( info.data(), "minping", va("%i",server->minPing));
		Info_SetValueForKey( info.data(), "maxping", va("%i",server->maxPing));
		Info_SetValueForKey( info.data(), "game", server->game);
		Info_SetValueForKey( info.data(), "gametype", va("%i",server->gameType));
		Info_SetValueForKey( info.data(), "nettype", va("%i",server->netType));
		Info_SetValueForKey( info.data(), "addr", NET_AdrToStringwPort(&server->adr));
		Info_SetValueForKey( info.data(), "punkbuster", va("%i", server->punkbuster));
		Info_SetValueForKey( info.data(), "g_needpass", va("%i", server->g_needpass));
		Info_SetValueForKey( info.data(), "g_humanplayers", va("%i", server->g_humanplayers));
		Q_strncpyz(buf, info.data(), buflen);
	} else {
		if (buf) {
			buf[0] = '\0';
		}
	}
}


/*
====================
LAN_GetServerPing
====================
*/
static int LAN_GetServerPing( int source, int n ) {
	serverInfo_t *server = LAN_GetServerAt( source, n );
	if (server) {
		return server->ping;
	}
	return -1;
}

/*
====================
LAN_GetServerPtr
====================
*/
static serverInfo_t *LAN_GetServerPtr( int source, int n ) {
	return LAN_GetServerAt( source, n );
}


/*
====================
LAN_CompareServers
====================
*/
static int LAN_CompareServers( int source, int sortKey, int sortDir, int s1, int s2 ) {
	int res;
	const serverInfo_t *server1, *server2;

	server1 = LAN_GetServerPtr(source, s1);
	server2 = LAN_GetServerPtr(source, s2);
	if (!server1 || !server2) {
		return 0;
	}

	res = 0;
	switch( sortKey ) {
		case SORT_HOST:
			res = Q_stricmp( server1->hostName, server2->hostName );
			break;

		case SORT_MAP:
			res = Q_stricmp( server1->mapName, server2->mapName );
			break;
		case SORT_CLIENTS:
			res = LAN_CompareInt( server1->clients, server2->clients );
			break;
		case SORT_GAME:
			res = LAN_CompareInt( server1->gameType, server2->gameType );
			break;
		case SORT_PING:
			res = LAN_CompareInt( server1->ping, server2->ping );
			break;
	}

	return LAN_ApplySortDirection( res, sortDir );
}


/*
====================
LAN_GetPingQueueCount
====================
*/
static int LAN_GetPingQueueCount( void ) {
	return (CL_GetPingQueueCount());
}


/*
====================
LAN_ClearPing
====================
*/
static void LAN_ClearPing( int n ) {
	CL_ClearPing( n );
}


/*
====================
LAN_GetPing
====================
*/
static void LAN_GetPing( int n, char *buf, int buflen, int *pingtime ) {
	CL_GetPing( n, buf, buflen, pingtime );
}


/*
====================
LAN_GetPingInfo
====================
*/
static void LAN_GetPingInfo( int n, char *buf, int buflen ) {
	CL_GetPingInfo( n, buf, buflen );
}


/*
====================
LAN_MarkServerVisible
====================
*/
static void LAN_MarkServerVisible(int source, int n, qboolean visible ) {
	if (n == -1) {
		const LANServerList list = LAN_GetServerList( source );

		if ( list.servers ) {
			for (n = 0; n < list.capacity; n++) {
				list.servers[n].visible = visible;
			}
		}

	} else {
		serverInfo_t *server = LAN_GetServerAt( source, n );

		if ( server ) {
			server->visible = visible;
		}
	}
}


/*
=======================
LAN_ServerIsVisible
=======================
*/
static int LAN_ServerIsVisible(int source, int n ) {
	const serverInfo_t *server = LAN_GetServerAt( source, n );

	if ( server ) {
		return server->visible;
	}
	return qfalse;
}


/*
=======================
LAN_UpdateVisiblePings
=======================
*/
static qboolean LAN_UpdateVisiblePings(int source ) {
	return CL_UpdateVisiblePings_f(source);
}


/*
====================
LAN_GetServerStatus
====================
*/
static int LAN_GetServerStatus( const char *serverAddress, char *serverStatus, int maxLen ) {
	return CL_ServerStatus( serverAddress, serverStatus, maxLen );
}


/*
====================
CL_GetGlConfig
====================
*/
static void CL_GetGlconfig( glconfig_t *config ) {
	*config = *re.GetConfig();
}


/*
====================
CL_GetClipboardData
====================
*/
static void CL_GetClipboardData( char *buf, int buflen ) {
	ScopedZoneMemory clipboardText( Sys_GetClipboardData() );
	char *cbd = clipboardText.as<char>();

	if ( !cbd ) {
		*buf = '\0';
		return;
	}

	Q_strncpyz( buf, cbd, buflen );
}


/*
====================
Key_KeynumToStringBuf
====================
*/
static void Key_KeynumToStringBuf( int keynum, char *buf, int buflen ) {
	Q_strncpyz( buf, Key_KeynumToString( keynum ), buflen );
}


/*
====================
Key_GetBindingBuf
====================
*/
static void Key_GetBindingBuf( int keynum, char *buf, int buflen ) {
	const char *value;

	value = Key_GetBinding( keynum );
	if ( value ) {
		Q_strncpyz( buf, value, buflen );
	}
	else {
		*buf = '\0';
	}
}


/*
====================
CLUI_GetCDKey
====================
*/
static void CLUI_GetCDKey( char *buf, int buflen ) {
#ifndef STANDALONE
	const char *gamedir;
	gamedir = Cvar_VariableString( "fs_game" );
	if ( UI_usesUniqueCDKey() && gamedir[0] != '\0' ) {
		std::copy_n( cl_cdkey + 16, 16, buf );
		buf[16] = '\0';
	} else {
		std::copy_n( cl_cdkey, 16, buf );
		buf[16] = '\0';
	}
#else
	*buf = '\0';
#endif
}


/*
====================
CLUI_SetCDKey
====================
*/
#ifndef STANDALONE
static void CLUI_SetCDKey( char *buf ) {
	const char *gamedir;
	gamedir = Cvar_VariableString( "fs_game" );
	if ( UI_usesUniqueCDKey() && gamedir[0] != '\0' ) {
		std::copy_n( buf, 16, cl_cdkey + 16 );
		cl_cdkey[32] = '\0';
		// set the flag so the flag will be written at the next opportunity
		cvar_modifiedFlags |= CVAR_ARCHIVE;
	} else {
		std::copy_n( buf, 16, cl_cdkey );
		// set the flag so the flag will be written at the next opportunity
		cvar_modifiedFlags |= CVAR_ARCHIVE;
	}
}
#endif


/*
====================
GetConfigString
====================
*/
static int GetConfigString(int index, char *buf, int size)
{
	int		offset;

	if (index < 0 || index >= MAX_CONFIGSTRINGS)
		return qfalse;

	offset = cl.gameState.stringOffsets[index];
	if (!offset) {
		if( size ) {
			buf[0] = 0;
		}
		return qfalse;
	}

	Q_strncpyz( buf, cl.gameState.stringData+offset, size);

	return qtrue;
}


/*
====================
FloatAsInt
====================
*/
static int FloatAsInt( float f ) {
	floatint_t fi;
	fi.f = f;
	return fi.i;
}


static void CL_UIAdjustStretchPic( float *x, float *y, float *w, float *h ) {
	const float xscale = cls.glconfig.vidWidth / 640.0f;
	const float yscale = cls.glconfig.vidHeight / 480.0f;

	// The retail UI applies its centered 4:3 transform before this syscall, so
	// the incoming coordinates are already framebuffer pixels.  Preserve that
	// layout by default; only undo it for the optional full-frame stretch mode.
	if ( !cl_menuAspect || cl_menuAspect->integer || cls.scale <= 0.0f
		|| xscale <= 0.0f || yscale <= 0.0f ) {
		return;
	}

	if ( x ) {
		*x = ( *x - cls.biasX ) / cls.scale * xscale;
	}
	if ( y ) {
		*y = *y / cls.scale * yscale;
	}
	if ( w ) {
		*w = *w / cls.scale * xscale;
	}
	if ( h ) {
		*h = *h / cls.scale * yscale;
	}
}


static void CL_UIAdjustRefdef( refdef_t *refdef ) {
	float x;
	float y;
	float w;
	float h;

	if ( !refdef || !cl_menuAspect || cl_menuAspect->integer ) {
		return;
	}

	x = static_cast<float>( refdef->x );
	y = static_cast<float>( refdef->y );
	w = static_cast<float>( refdef->width );
	h = static_cast<float>( refdef->height );

	CL_UIAdjustStretchPic( &x, &y, &w, &h );

	refdef->x = RoundToInt( x );
	refdef->y = RoundToInt( y );
	refdef->width = RoundToInt( w );
	refdef->height = RoundToInt( h );
}


/*
====================
VM_ArgPtr
====================
*/
static void *VM_ArgPtr( intptr_t intValue ) {

	if ( !intValue || uivm == nullptr )
	  return nullptr;

	if ( uivm->entryPoint || uivm->dllExports )
		return reinterpret_cast<void *>( intValue );
	else
		return uivm->dataBase + ( intValue & uivm->dataMask );
}

class VmArgPtr {
public:
	explicit VmArgPtr( void *ptr ) : ptr_( ptr ) {
	}

	template <typename T>
	operator T *() const {
		return static_cast<T *>( ptr_ );
	}

	operator void *() const {
		return ptr_;
	}

	const char *String() const {
		return static_cast<const char *>( ptr_ );
	}

private:
	void *ptr_;
};

#undef VMA
#define VMA(x) VmArgPtr( VM_ArgPtr( args[x] ) )


static qboolean UI_GetValue( char* value, int valueSize, const char* key ) {

	if ( !Q_stricmp( key, "trap_R_AddRefEntityToScene2" ) ) {
		Com_sprintf( value, valueSize, "%i", UI_R_ADDREFENTITYTOSCENE2 );
		return qtrue;
	}

	if ( !Q_stricmp( key, "trap_R_AddLinearLightToScene_Q3E" ) && re.AddLinearLightToScene ) {
		Com_sprintf( value, valueSize, "%i", UI_R_ADDLINEARLIGHTTOSCENE );
		return qtrue;
	}

	if ( !Q_stricmp( key, "trap_Cvar_SetDescription_Q3E" ) ) {
		Com_sprintf( value, valueSize, "%i", UI_CVAR_SETDESCRIPTION );
		return qtrue;
	}

	return qfalse;
}

#define CL_SCREENSHOT_SUBDIR "screenshots/"

static qboolean CL_ScreenshotPathIsAllowed( const char *requestedName ) {
	const char *trimmedName;

	if ( !requestedName || !requestedName[0] ) {
		return qfalse;
	}

	if ( requestedName[0] == '/' || requestedName[0] == '\\' ) {
		return qfalse;
	}

	if ( strstr( requestedName, ".." ) || strchr( requestedName, ':' ) || strchr( requestedName, '\\' ) ) {
		return qfalse;
	}

	if ( !Q_stricmpn( requestedName, CL_SCREENSHOT_SUBDIR, strlen( CL_SCREENSHOT_SUBDIR ) ) ) {
		trimmedName = requestedName + strlen( CL_SCREENSHOT_SUBDIR );
	} else {
		trimmedName = requestedName;
	}

	if ( !trimmedName[0] ) {
		return qfalse;
	}

	return qtrue;
}

static void CL_ScreenshotBuildQPath( const char *requestedName, char *qpath, int qpathLen ) {
	if ( !Q_stricmpn( requestedName, CL_SCREENSHOT_SUBDIR, strlen( CL_SCREENSHOT_SUBDIR ) ) ) {
		Com_sprintf( qpath, qpathLen, "%s", requestedName );
	} else {
		Com_sprintf( qpath, qpathLen, "%s%s", CL_SCREENSHOT_SUBDIR, requestedName );
	}
}

static int CL_MenuReadScreenshot( const char *requestedName, byte *buffer, int bufferSize ) {
	fileHandle_t file;
	char qpath[MAX_QPATH];
	int fileSize;
	int bytesToRead;

	if ( !buffer || bufferSize <= 0 ) {
		return -1;
	}

	if ( !CL_ScreenshotPathIsAllowed( requestedName ) ) {
		return -1;
	}

	CL_ScreenshotBuildQPath( requestedName, qpath, sizeof( qpath ) );

	fileSize = FS_FOpenFileRead( qpath, &file, qtrue );
	if ( fileSize <= 0 ) {
		return fileSize;
	}

	bytesToRead = fileSize;
	if ( bytesToRead > bufferSize ) {
		bytesToRead = bufferSize;
	}

	FS_Read( buffer, bytesToRead, file );
	FS_FCloseFile( file );

	return bytesToRead;
}


/*
====================
CL_UISystemCalls

The ui module is making a system call
====================
*/
static intptr_t CL_UISystemCalls( intptr_t *args ) {
	switch( args[0] ) {
	case UI_ERROR:
		Com_Error( ERR_DROP, "%s", VMA(1).String() );
		return 0;

	case UI_PRINT:
		Com_Printf( "%s", VMA(1).String() );
		return 0;

	case UI_MILLISECONDS:
		return Sys_Milliseconds();

	case UI_CVAR_REGISTER:
		if ( uivm->dllExports ) {
			Cvar_Register( VMA(1), VMA(2), VMA(3), args[4], uivm->privateFlag );
		} else {
			Cvar_RegisterLegacy( VMA(1), VMA(2), VMA(3), args[4], uivm->privateFlag );
		}
		return 0;

	case UI_CVAR_UPDATE:
		if ( uivm->dllExports ) {
			Cvar_Update( VMA(1), uivm->privateFlag );
		} else {
			Cvar_UpdateLegacy( VMA(1), uivm->privateFlag );
		}
		return 0;

	case UI_CVAR_SET:
		Cvar_SetSafe( VMA(1), VMA(2) );
		return 0;

	case UI_CVAR_VARIABLEVALUE:
		return FloatAsInt( Cvar_VariableValue( VMA(1) ) );

	case UI_CVAR_VARIABLESTRINGBUFFER:
		VM_CHECKBOUNDS( uivm, args[2], args[3] );
		Cvar_VariableStringBufferSafe( VMA(1), VMA(2), args[3], CVAR_PRIVATE );
		return 0;

	case UI_CVAR_SETVALUE:
		Cvar_SetValueSafe( VMA(1), VMF(2) );
		return 0;

	case UI_CVAR_RESET:
		Cvar_Reset( VMA(1) );
		return 0;

	case UI_CVAR_CREATE:
		Cvar_Register( nullptr, VMA(1), VMA(2), args[3], uivm->privateFlag );
		return 0;

	case UI_CVAR_INFOSTRINGBUFFER:
		VM_CHECKBOUNDS( uivm, args[2], args[3] );
		Cvar_InfoStringBuffer( args[1], VMA(2), args[3] );
		return 0;

	case UI_ARGC:
		return Cmd_Argc();

	case UI_ARGV:
		VM_CHECKBOUNDS( uivm, args[2], args[3] );
		Cmd_ArgvBuffer( args[1], VMA(2), args[3] );
		return 0;

	case UI_CMD_EXECUTETEXT:
		if(args[1] == EXEC_NOW
		&& (!strncmp(VMA(2), "snd_restart", 11)
		|| !strncmp(VMA(2), "vid_restart", 11)
		|| !strncmp(VMA(2), "disconnect", 10)
		|| !strncmp(VMA(2), "quit", 5)))
		{
			Com_Printf (S_COLOR_YELLOW "turning EXEC_NOW '%.11s' into EXEC_INSERT\n", VMA(2).String());
			args[1] = EXEC_INSERT;
		}
		Cbuf_ExecuteText( static_cast<cbufExec_t>( args[1] ), VMA(2) );
		return 0;

	case UI_FS_FOPENFILE:
		return FS_VM_OpenFile( VMA(1), VMA(2), static_cast<fsMode_t>( args[3] ), H_Q3UI );

	case UI_FS_READ:
		VM_CHECKBOUNDS( uivm, args[1], args[2] );
		FS_VM_ReadFile( VMA(1), args[2], args[3], H_Q3UI );
		return 0;

	case UI_FS_WRITE:
		VM_CHECKBOUNDS( uivm, args[1], args[2] );
		FS_VM_WriteFile( VMA(1), args[2], args[3], H_Q3UI );
		return 0;

	case UI_FS_FCLOSEFILE:
		FS_VM_CloseFile( args[1], H_Q3UI );
		return 0;

	case UI_FS_SEEK:
		return FS_VM_SeekFile( args[1], args[2], static_cast<fsOrigin_t>( args[3] ), H_Q3UI );

	case UI_LAUNCHER_READSCREENSHOT:
		VM_CHECKBOUNDS( uivm, args[2], args[3] );
		return CL_MenuReadScreenshot( VMA(1), VMA(2), args[3] );

	case UI_FS_GETFILELIST:
		VM_CHECKBOUNDS( uivm, args[3], args[4] );
		return FS_GetFileList( VMA(1), VMA(2), VMA(3), args[4] );

	case UI_R_REGISTERMODEL:
		return re.RegisterModel( VMA(1) );

	case UI_R_REGISTERSKIN:
		return re.RegisterSkin( VMA(1) );

	case UI_R_REGISTERSHADERNOMIP:
		return CL_Steam_RegisterShader( VMA(1) );

	case UI_R_CLEARSCENE:
		re.ClearScene();
		return 0;

	case UI_R_ADDREFENTITYTOSCENE:
		re.AddRefEntityToScene( VMA(1), qfalse );
		return 0;

	case UI_R_ADDPOLYTOSCENE:
		re.AddPolyToScene( args[1], args[2], VMA(3), 1 );
		return 0;

	case UI_R_ADDLIGHTTOSCENE:
		re.AddLightToScene( VMA(1), VMF(2), VMF(3), VMF(4), VMF(5) );
		return 0;

	case UI_R_RENDERSCENE: {
		const refdef_t *vmRefdef = VMA(1);
		refdef_t refdef = *vmRefdef;
		CL_UIAdjustRefdef( &refdef );
		re.RenderScene( &refdef );
		return 0;
	}

	case UI_R_SETCOLOR:
		re.SetColor( VMA(1) );
		return 0;

	case UI_R_DRAWSTRETCHPIC: {
		float x = VMF(1);
		float y = VMF(2);
		float w = VMF(3);
		float h = VMF(4);

		CL_UIAdjustStretchPic( &x, &y, &w, &h );
		re.DrawStretchPic( x, y, w, h, VMF(5), VMF(6), VMF(7), VMF(8), args[9] );
		return 0;
	}

	case UI_R_MODELBOUNDS:
		re.ModelBounds( args[1], VMA(2), VMA(3) );
		return 0;

	case UI_UPDATESCREEN:
		SCR_UpdateScreen();
		return 0;

	case UI_CM_LERPTAG:
		re.LerpTag( VMA(1), args[2], args[3], args[4], VMF(5), VMA(6) );
		return 0;

	case UI_CM_LOADMODEL:
		return 0;

	case UI_S_REGISTERSOUND:
		return S_RegisterSound( VMA(1), static_cast<qboolean>( args[2] ) );

	case UI_S_STARTLOCALSOUND:
		S_StartLocalSound( args[1], args[2] );
		return 0;

	case UI_KEY_KEYNUMTOSTRINGBUF:
		VM_CHECKBOUNDS( uivm, args[2], args[3] );
		Key_KeynumToStringBuf( args[1], VMA(2), args[3] );
		return 0;

	case UI_KEY_GETBINDINGBUF:
		VM_CHECKBOUNDS( uivm, args[2], args[3] );
		Key_GetBindingBuf( args[1], VMA(2), args[3] );
		return 0;

	case UI_KEY_SETBINDING:
		Key_SetBinding( args[1], VMA(2) );
		return 0;

	case UI_KEY_ISDOWN:
		return Key_IsDown( args[1] );

	case UI_KEY_GETOVERSTRIKEMODE:
		return Key_GetOverstrikeMode();

	case UI_KEY_SETOVERSTRIKEMODE:
		Key_SetOverstrikeMode( static_cast<qboolean>( args[1] ) );
		return 0;

	case UI_KEY_CLEARSTATES:
		Key_ClearStates();
		return 0;

	case UI_KEY_GETCATCHER:
		return Key_GetCatcher();

	case UI_KEY_SETCATCHER:
		// Console and browser ownership are controlled by their host subsystems.
		Key_SetCatcher( args[1] | ( Key_GetCatcher( ) & ( KEYCATCH_CONSOLE | KEYCATCH_BROWSER ) ) );
		return 0;

	case UI_GETCLIPBOARDDATA:
		VM_CHECKBOUNDS( uivm, args[1], args[2] );
		CL_GetClipboardData( VMA(1), args[2] );
		return 0;

	case UI_GETCLIENTSTATE:
		VM_CHECKBOUNDS( uivm, args[1], sizeof( uiClientState_t ) );
		GetClientState( VMA(1) );
		return 0;

	case UI_GETGLCONFIG:
		VM_CHECKBOUNDS( uivm, args[1], sizeof( glconfig_t ) );
		CL_GetGlconfig( VMA(1) );
		return 0;

	case UI_GETCONFIGSTRING:
		VM_CHECKBOUNDS( uivm, args[2], args[3] );
		return GetConfigString( args[1], VMA(2), args[3] );

	case UI_LAN_LOADCACHEDSERVERS:
		LAN_LoadCachedServers();
		return 0;

	case UI_LAN_SAVECACHEDSERVERS:
		LAN_SaveServersToCache();
		return 0;

	case UI_LAN_ADDSERVER:
		return LAN_AddServer(args[1], VMA(2), VMA(3));

	case UI_LAN_REMOVESERVER:
		LAN_RemoveServer(args[1], VMA(2));
		return 0;

	case UI_LAN_GETPINGQUEUECOUNT:
		return LAN_GetPingQueueCount();

	case UI_LAN_CLEARPING:
		LAN_ClearPing( args[1] );
		return 0;

	case UI_LAN_GETPING:
		VM_CHECKBOUNDS( uivm, args[2], args[3] );
		LAN_GetPing( args[1], VMA(2), args[3], VMA(4) );
		return 0;

	case UI_LAN_GETPINGINFO:
		VM_CHECKBOUNDS( uivm, args[2], args[3] );
		LAN_GetPingInfo( args[1], VMA(2), args[3] );
		return 0;

	case UI_LAN_GETSERVERCOUNT:
		return LAN_GetServerCount(args[1]);

	case UI_LAN_GETSERVERADDRESSSTRING:
		VM_CHECKBOUNDS( uivm, args[3], args[4] );
		LAN_GetServerAddressString( args[1], args[2], VMA(3), args[4] );
		return 0;

	case UI_LAN_GETSERVERINFO:
		VM_CHECKBOUNDS( uivm, args[3], args[4] );
		LAN_GetServerInfo( args[1], args[2], VMA(3), args[4] );
		return 0;

	case UI_LAN_GETSERVERPING:
		return LAN_GetServerPing( args[1], args[2] );

	case UI_LAN_MARKSERVERVISIBLE:
		LAN_MarkServerVisible( args[1], args[2], static_cast<qboolean>( args[3] ) );
		return 0;

	case UI_LAN_SERVERISVISIBLE:
		return LAN_ServerIsVisible( args[1], args[2] );

	case UI_LAN_UPDATEVISIBLEPINGS:
		return LAN_UpdateVisiblePings( args[1] );

	case UI_LAN_RESETPINGS:
		LAN_ResetPings( args[1] );
		return 0;

	case UI_LAN_SERVERSTATUS:
		VM_CHECKBOUNDS( uivm, args[2], args[3] );
		return LAN_GetServerStatus( VMA(1), VMA(2), args[3] );

	case UI_LAN_COMPARESERVERS:
		return LAN_CompareServers( args[1], args[2], args[3], args[4], args[5] );

	case UI_MEMORY_REMAINING:
		return Hunk_MemoryRemaining();

	case UI_GET_CDKEY:
		VM_CHECKBOUNDS( uivm, args[1], args[2] );
		CLUI_GetCDKey( VMA(1), args[2] );
		return 0;

	case UI_SET_CDKEY:
#ifndef STANDALONE
		CLUI_SetCDKey( VMA(1) );
#endif
		return 0;

	case UI_SET_PBCLSTATUS:
		return 0;

	case UI_R_REGISTERFONT:
		re.RegisterFont( VMA(1), args[2], VMA(3));
		return 0;

	// shared syscalls

	case TRAP_MEMSET:
		VM_CHECKBOUNDS( uivm, args[1], args[3] );
		Com_Memset( VMA(1), args[2], args[3] );
		return args[1];

	case TRAP_MEMCPY:
		VM_CHECKBOUNDS2( uivm, args[1], args[2], args[3] );
		Com_Memcpy( VMA(1), VMA(2), args[3] );
		return args[1];

	case TRAP_STRNCPY:
		VM_CHECKBOUNDS( uivm, args[1], args[3] );
		Q_strncpy( VMA(1), VMA(2), args[3] );
		return args[1];

	case TRAP_SIN:
		return FloatAsInt( sin( VMF(1) ) );

	case TRAP_COS:
		return FloatAsInt( cos( VMF(1) ) );

	case TRAP_ATAN2:
		return FloatAsInt( atan2( VMF(1), VMF(2) ) );

	case TRAP_SQRT:
		return FloatAsInt( sqrt( VMF(1) ) );

	case UI_FLOOR:
		return FloatAsInt( floor( VMF(1) ) );

	case UI_CEIL:
		return FloatAsInt( ceil( VMF(1) ) );

	case UI_PC_ADD_GLOBAL_DEFINE:
		return botlib_export->PC_AddGlobalDefine( VMA(1) );
	case UI_PC_LOAD_SOURCE:
		return botlib_export->PC_LoadSourceHandle( VMA(1) );
	case UI_PC_FREE_SOURCE:
		return botlib_export->PC_FreeSourceHandle( args[1] );
	case UI_PC_READ_TOKEN:
		return botlib_export->PC_ReadTokenHandle( args[1], VMA(2) );
	case UI_PC_SOURCE_FILE_AND_LINE:
		return botlib_export->PC_SourceFileAndLine( args[1], VMA(2), VMA(3) );

	case UI_S_STOPBACKGROUNDTRACK:
		S_StopBackgroundTrack();
		return 0;
	case UI_S_STARTBACKGROUNDTRACK:
		S_StartBackgroundTrack( VMA(1), VMA(2));
		return 0;

	case UI_REAL_TIME:
		return Com_RealTime( VMA(1) );

	case UI_CIN_PLAYCINEMATIC:
		Com_DPrintf("UI_CIN_PlayCinematic\n");
		return CIN_PlayCinematic(VMA(1), args[2], args[3], args[4], args[5], args[6]);

	case UI_CIN_STOPCINEMATIC:
		return CIN_StopCinematic(args[1]);

	case UI_CIN_RUNCINEMATIC:
		return CIN_RunCinematic(args[1]);

	case UI_CIN_DRAWCINEMATIC:
		CIN_DrawCinematicUI( args[1] );
		return 0;

	case UI_CIN_SETEXTENTS:
		CIN_SetExtents(args[1], args[2], args[3], args[4], args[5]);
		return 0;

	case UI_R_REMAP_SHADER:
		re.RemapShader( VMA(1), VMA(2), VMA(3) );
		return 0;

	case UI_VERIFY_CDKEY:
		return Com_CDKeyValidate(VMA(1), VMA(2));

	// engine extensions
	case UI_R_ADDREFENTITYTOSCENE2:
		re.AddRefEntityToScene( VMA(1), qtrue );
		return 0;

	// engine extensions
	case UI_R_ADDLINEARLIGHTTOSCENE:
		re.AddLinearLightToScene( VMA(1), VMA(2), VMF(3), VMF(4), VMF(5), VMF(6) );
		return 0;

	case UI_CVAR_SETDESCRIPTION:
		Cvar_SetDescription2( VMA(1).String(), VMA(2).String() );
		return 0;

	case UI_TRAP_GETVALUE:
		VM_CHECKBOUNDS( uivm, args[1], args[2] );
		return UI_GetValue( VMA(1), args[2], VMA(3) );

	default:
		Com_Error( ERR_DROP, "Bad UI system trap: %ld", static_cast<long>( args[0] ) );

	}

	return 0;
}


/*
====================
UI_DllSyscall
====================
*/
static intptr_t QDECL UI_DllSyscall( intptr_t arg, ... ) {
#if !id386 || defined __clang__
	intptr_t	args[10]; // max.count for UI
	va_list	ap;
	int i;

	args[0] = arg;
	va_start( ap, arg );
	for (i = 1; i < static_cast<int>( ARRAY_LEN( args ) ); i++ )
		args[ i ] = va_arg( ap, intptr_t );
	va_end( ap );

	return CL_UISystemCalls( args );
#else
	return CL_UISystemCalls( &arg );
#endif
}

typedef void (QDECL *ql_import_f)( void );

static ql_import_f ql_ui_imports[UI_QL_NATIVE_IMPORT_COUNT];
static vec4_t ql_ui_currentColor = { 1.0f, 1.0f, 1.0f, 1.0f };

static intptr_t UI_Import_Call( intptr_t callNum, std::initializer_list<intptr_t> values ) {
	intptr_t args[16] = { 0 };
	int i = 1;

	args[0] = callNum;
	for ( intptr_t value : values ) {
		if ( i >= static_cast<int>( ARRAY_LEN( args ) ) ) {
			break;
		}
		args[i++] = value;
	}

	return CL_UISystemCalls( args );
}

static intptr_t UI_Import_Ptr( const void *ptr ) {
	return reinterpret_cast<intptr_t>( ptr );
}

static int QDECL QL_UI_PASSFLOAT( float value ) {
	return FloatAsInt( value );
}

static float QL_UI_IntAsFloat( intptr_t value ) {
	floatint_t fi;
	fi.i = static_cast<int32_t>( value );
	return fi.f;
}

static void QDECL QL_UI_trap_Print( const char *string ) {
	UI_Import_Call( UI_PRINT, { UI_Import_Ptr( string ) } );
}

static void QDECL QL_UI_trap_Error( const char *string ) {
	UI_Import_Call( UI_ERROR, { UI_Import_Ptr( string ) } );
}

static int QDECL QL_UI_trap_Milliseconds( void ) {
	return static_cast<int>( UI_Import_Call( UI_MILLISECONDS, {} ) );
}

static int QDECL QL_UI_trap_RealTime( qtime_t *qtime ) {
	return static_cast<int>( UI_Import_Call( UI_REAL_TIME, { UI_Import_Ptr( qtime ) } ) );
}

static void QDECL QL_UI_trap_Cvar_Register( vmCvar_t *cvar, const char *varName, const char *value, int flags ) {
	UI_Import_Call( UI_CVAR_REGISTER, { UI_Import_Ptr( cvar ), UI_Import_Ptr( varName ), UI_Import_Ptr( value ), flags } );
}

static void QDECL QL_UI_trap_Cvar_Create( const char *varName, const char *value, int flags ) {
	UI_Import_Call( UI_CVAR_CREATE, { UI_Import_Ptr( varName ), UI_Import_Ptr( value ), flags } );
}

static void QDECL QL_UI_trap_Cvar_Update( vmCvar_t *cvar ) {
	UI_Import_Call( UI_CVAR_UPDATE, { UI_Import_Ptr( cvar ) } );
}

static void QDECL QL_UI_trap_Cvar_Set( const char *varName, const char *value ) {
	UI_Import_Call( UI_CVAR_SET, { UI_Import_Ptr( varName ), UI_Import_Ptr( value ) } );
}

static void QDECL QL_UI_trap_Cvar_SetValue( const char *varName, float value ) {
	UI_Import_Call( UI_CVAR_SETVALUE, { UI_Import_Ptr( varName ), QL_UI_PASSFLOAT( value ) } );
}

static void QDECL QL_UI_trap_Cvar_VariableStringBuffer( const char *varName, char *buffer, int bufsize ) {
	UI_Import_Call( UI_CVAR_VARIABLESTRINGBUFFER, { UI_Import_Ptr( varName ), UI_Import_Ptr( buffer ), bufsize } );
}

static float QDECL QL_UI_trap_Cvar_VariableValue( const char *varName ) {
	return QL_UI_IntAsFloat( UI_Import_Call( UI_CVAR_VARIABLEVALUE, { UI_Import_Ptr( varName ) } ) );
}

static void QDECL QL_UI_trap_Cvar_Reset( const char *name ) {
	UI_Import_Call( UI_CVAR_RESET, { UI_Import_Ptr( name ) } );
}

static void QDECL QL_UI_trap_Cvar_InfoStringBuffer( int bit, char *buffer, int bufsize ) {
	UI_Import_Call( UI_CVAR_INFOSTRINGBUFFER, { bit, UI_Import_Ptr( buffer ), bufsize } );
}

static int QDECL QL_UI_trap_Argc( void ) {
	return static_cast<int>( UI_Import_Call( UI_ARGC, {} ) );
}

static void QDECL QL_UI_trap_Argv( int n, char *buffer, int bufferLength ) {
	UI_Import_Call( UI_ARGV, { n, UI_Import_Ptr( buffer ), bufferLength } );
}

static void QDECL QL_UI_trap_Cmd_ArgsBuffer_QL( char *buffer, int bufferLength ) {
	Cmd_ArgsBuffer( buffer, bufferLength );
}

static void QDECL QL_UI_trap_Cmd_ExecuteText_QL( int maybeExec, const char *maybeText ) {
	const char *text = maybeText;
	int execWhen = maybeExec;

	if ( execWhen < EXEC_NOW || execWhen > EXEC_APPEND ) {
		text = reinterpret_cast<const char *>( static_cast<intptr_t>( maybeExec ) );
		execWhen = EXEC_APPEND;
	}

	if ( text ) {
		Cbuf_ExecuteText( static_cast<cbufExec_t>( execWhen ), text );
	}
}

static int QDECL QL_UI_trap_FS_FOpenFile( const char *qpath, fileHandle_t *f, fsMode_t mode ) {
	return static_cast<int>( UI_Import_Call( UI_FS_FOPENFILE, { UI_Import_Ptr( qpath ), UI_Import_Ptr( f ), mode } ) );
}

static void QDECL QL_UI_trap_FS_Read( void *buffer, int len, fileHandle_t f ) {
	UI_Import_Call( UI_FS_READ, { UI_Import_Ptr( buffer ), len, f } );
}

static void QDECL QL_UI_trap_FS_Write( const void *buffer, int len, fileHandle_t f ) {
	UI_Import_Call( UI_FS_WRITE, { UI_Import_Ptr( buffer ), len, f } );
}

static void QDECL QL_UI_trap_FS_FCloseFile( fileHandle_t f ) {
	UI_Import_Call( UI_FS_FCLOSEFILE, { f } );
}

static int QDECL QL_UI_trap_FS_Seek( fileHandle_t f, long offset, int origin ) {
	return static_cast<int>( UI_Import_Call( UI_FS_SEEK, { f, offset, origin } ) );
}

static int QDECL QL_UI_trap_FS_GetFileList( const char *path, const char *extension, char *listbuf, int bufsize ) {
	return static_cast<int>( UI_Import_Call( UI_FS_GETFILELIST, { UI_Import_Ptr( path ), UI_Import_Ptr( extension ), UI_Import_Ptr( listbuf ), bufsize } ) );
}

static qhandle_t QDECL QL_UI_trap_R_RegisterModel( const char *name ) {
	return static_cast<qhandle_t>( UI_Import_Call( UI_R_REGISTERMODEL, { UI_Import_Ptr( name ) } ) );
}

static qhandle_t QDECL QL_UI_trap_R_RegisterSkin( const char *name ) {
	return static_cast<qhandle_t>( UI_Import_Call( UI_R_REGISTERSKIN, { UI_Import_Ptr( name ) } ) );
}

static qhandle_t QDECL QL_UI_trap_R_RegisterShaderNoMip( const char *name ) {
	return static_cast<qhandle_t>( UI_Import_Call( UI_R_REGISTERSHADERNOMIP, { UI_Import_Ptr( name ) } ) );
}

static void QDECL QL_UI_trap_R_RegisterFont( const char *fontName, int pointSize, fontInfo_t *font ) {
	UI_Import_Call( UI_R_REGISTERFONT, { UI_Import_Ptr( fontName ), pointSize, UI_Import_Ptr( font ) } );
}

static void QDECL QL_UI_trap_R_ClearScene( void ) {
	UI_Import_Call( UI_R_CLEARSCENE, {} );
}

static void QDECL QL_UI_trap_R_AddRefEntityToScene( const refEntity_t *refent ) {
	UI_Import_Call( UI_R_ADDREFENTITYTOSCENE, { UI_Import_Ptr( refent ) } );
}

static void QDECL QL_UI_trap_R_AddPolyToScene( qhandle_t shader, int numVerts, const polyVert_t *verts ) {
	UI_Import_Call( UI_R_ADDPOLYTOSCENE, { shader, numVerts, UI_Import_Ptr( verts ) } );
}

static void QDECL QL_UI_trap_R_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b ) {
	UI_Import_Call( UI_R_ADDLIGHTTOSCENE, {
		UI_Import_Ptr( org ), QL_UI_PASSFLOAT( intensity ), QL_UI_PASSFLOAT( r ),
		QL_UI_PASSFLOAT( g ), QL_UI_PASSFLOAT( b )
	} );
}

static void QDECL QL_UI_trap_R_RenderScene( const refdef_t *refdef ) {
	UI_Import_Call( UI_R_RENDERSCENE, { UI_Import_Ptr( refdef ) } );
}

static void QDECL QL_UI_trap_R_SetColor_QL( const float *rgba ) {
	if ( rgba ) {
		ql_ui_currentColor[0] = rgba[0];
		ql_ui_currentColor[1] = rgba[1];
		ql_ui_currentColor[2] = rgba[2];
		ql_ui_currentColor[3] = rgba[3];
	} else {
		ql_ui_currentColor[0] = 1.0f;
		ql_ui_currentColor[1] = 1.0f;
		ql_ui_currentColor[2] = 1.0f;
		ql_ui_currentColor[3] = 1.0f;
	}
	UI_Import_Call( UI_R_SETCOLOR, { UI_Import_Ptr( rgba ) } );
}

static void QDECL QL_UI_trap_R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t shader ) {
	UI_Import_Call( UI_R_DRAWSTRETCHPIC, {
		QL_UI_PASSFLOAT( x ), QL_UI_PASSFLOAT( y ), QL_UI_PASSFLOAT( w ), QL_UI_PASSFLOAT( h ),
		QL_UI_PASSFLOAT( s1 ), QL_UI_PASSFLOAT( t1 ), QL_UI_PASSFLOAT( s2 ), QL_UI_PASSFLOAT( t2 ), shader
	} );
}

static void QDECL QL_UI_trap_R_ModelBounds( clipHandle_t model, vec3_t mins, vec3_t maxs ) {
	UI_Import_Call( UI_R_MODELBOUNDS, { model, UI_Import_Ptr( mins ), UI_Import_Ptr( maxs ) } );
}

static void QDECL QL_UI_trap_UpdateScreen( void ) {
	UI_Import_Call( UI_UPDATESCREEN, {} );
}

static int QDECL QL_UI_trap_CM_LerpTag( orientation_t *tag, clipHandle_t mod, int startFrame, int endFrame, float frac, const char *tagName ) {
	return static_cast<int>( UI_Import_Call( UI_CM_LERPTAG, {
		UI_Import_Ptr( tag ), mod, startFrame, endFrame, QL_UI_PASSFLOAT( frac ), UI_Import_Ptr( tagName )
	} ) );
}

static int QDECL QL_UI_trap_CM_LoadModel( const char *name ) {
	(void)name;
	return 0;
}

static void QDECL QL_UI_trap_S_StartLocalSound( sfxHandle_t sfx, int channelNum ) {
	UI_Import_Call( UI_S_STARTLOCALSOUND, { sfx, channelNum } );
}

static sfxHandle_t QDECL QL_UI_trap_S_RegisterSound_QL( const char *sample ) {
	// Retail uix86.dll import 35 passes only the sample path. Compression is
	// an engine-side detail and must not consume an unprovided stack argument.
	return S_RegisterSound( sample, qfalse );
}

static void QDECL QL_UI_trap_Key_KeynumToStringBuf( int keynum, char *buf, int buflen ) {
	UI_Import_Call( UI_KEY_KEYNUMTOSTRINGBUF, { keynum, UI_Import_Ptr( buf ), buflen } );
}

static void QDECL QL_UI_trap_Key_GetBindingBuf( int keynum, char *buf, int buflen ) {
	UI_Import_Call( UI_KEY_GETBINDINGBUF, { keynum, UI_Import_Ptr( buf ), buflen } );
}

static void QDECL QL_UI_trap_Key_SetBinding( int keynum, const char *binding ) {
	UI_Import_Call( UI_KEY_SETBINDING, { keynum, UI_Import_Ptr( binding ) } );
}

static qboolean QDECL QL_UI_trap_Key_IsDown( int keynum ) {
	return UI_Import_Call( UI_KEY_ISDOWN, { keynum } ) ? qtrue : qfalse;
}

static qboolean QDECL QL_UI_trap_Key_GetOverstrikeMode( void ) {
	return UI_Import_Call( UI_KEY_GETOVERSTRIKEMODE, {} ) ? qtrue : qfalse;
}

static void QDECL QL_UI_trap_Key_SetOverstrikeMode( qboolean state ) {
	UI_Import_Call( UI_KEY_SETOVERSTRIKEMODE, { state } );
}

static void QDECL QL_UI_trap_Key_ClearStates( void ) {
	UI_Import_Call( UI_KEY_CLEARSTATES, {} );
}

static int QDECL QL_UI_trap_Key_GetCatcher( void ) {
	return static_cast<int>( UI_Import_Call( UI_KEY_GETCATCHER, {} ) );
}

static void QDECL QL_UI_trap_Key_SetCatcher( int catcher ) {
	UI_Import_Call( UI_KEY_SETCATCHER, { catcher } );
}

static void QDECL QL_UI_trap_GetClipboardData( char *buf, int bufsize ) {
	UI_Import_Call( UI_GETCLIPBOARDDATA, { UI_Import_Ptr( buf ), bufsize } );
}

static void QDECL QL_UI_trap_GetClientState( uiClientState_t *state ) {
	UI_Import_Call( UI_GETCLIENTSTATE, { UI_Import_Ptr( state ) } );
}

static void QDECL QL_UI_trap_GetGlconfig( glconfig_t *glconfig ) {
	// The native retail UI consumes the QL glconfig ABI, which has an extra
	// multitexture field ahead of the video dimensions.  The generic UI syscall
	// path is retained for bytecode UI modules and uses the legacy layout.
	CL_CopyRetailGlconfig( glconfig );
}

static int QDECL QL_UI_trap_GetConfigString( int index, char *buffer, int bufferSize ) {
	return static_cast<int>( UI_Import_Call( UI_GETCONFIGSTRING, { index, UI_Import_Ptr( buffer ), bufferSize } ) );
}

static int QDECL QL_UI_trap_LAN_GetServerCount( int source ) {
	return static_cast<int>( UI_Import_Call( UI_LAN_GETSERVERCOUNT, { source } ) );
}

static void QDECL QL_UI_trap_LAN_GetServerAddressString( int source, int n, char *buf, int buflen ) {
	UI_Import_Call( UI_LAN_GETSERVERADDRESSSTRING, { source, n, UI_Import_Ptr( buf ), buflen } );
}

static void QDECL QL_UI_trap_LAN_GetServerInfo( int source, int n, char *buf, int buflen ) {
	UI_Import_Call( UI_LAN_GETSERVERINFO, { source, n, UI_Import_Ptr( buf ), buflen } );
}

static int QDECL QL_UI_trap_LAN_GetServerPing( int source, int n ) {
	return static_cast<int>( UI_Import_Call( UI_LAN_GETSERVERPING, { source, n } ) );
}

static int QDECL QL_UI_trap_LAN_GetPingQueueCount( void ) {
	return static_cast<int>( UI_Import_Call( UI_LAN_GETPINGQUEUECOUNT, {} ) );
}

static void QDECL QL_UI_trap_LAN_ClearPing( int n ) {
	UI_Import_Call( UI_LAN_CLEARPING, { n } );
}

static void QDECL QL_UI_trap_LAN_GetPing( int n, char *buf, int buflen, int *pingtime ) {
	UI_Import_Call( UI_LAN_GETPING, { n, UI_Import_Ptr( buf ), buflen, UI_Import_Ptr( pingtime ) } );
}

static void QDECL QL_UI_trap_LAN_GetPingInfo( int n, char *buf, int buflen ) {
	UI_Import_Call( UI_LAN_GETPINGINFO, { n, UI_Import_Ptr( buf ), buflen } );
}

static void QDECL QL_UI_trap_LAN_LoadCachedServers( void ) {
	UI_Import_Call( UI_LAN_LOADCACHEDSERVERS, {} );
}

static void QDECL QL_UI_trap_LAN_SaveCachedServers( void ) {
	UI_Import_Call( UI_LAN_SAVECACHEDSERVERS, {} );
}

static void QDECL QL_UI_trap_LAN_MarkServerVisible( int source, int n, qboolean visible ) {
	UI_Import_Call( UI_LAN_MARKSERVERVISIBLE, { source, n, visible } );
}

static int QDECL QL_UI_trap_LAN_ServerIsVisible( int source, int n ) {
	return static_cast<int>( UI_Import_Call( UI_LAN_SERVERISVISIBLE, { source, n } ) );
}

static qboolean QDECL QL_UI_trap_LAN_UpdateVisiblePings( int source ) {
	return UI_Import_Call( UI_LAN_UPDATEVISIBLEPINGS, { source } ) ? qtrue : qfalse;
}

static int QDECL QL_UI_trap_LAN_AddServer( int source, const char *name, const char *address ) {
	return static_cast<int>( UI_Import_Call( UI_LAN_ADDSERVER, { source, UI_Import_Ptr( name ), UI_Import_Ptr( address ) } ) );
}

static void QDECL QL_UI_trap_LAN_RemoveServer( int source, const char *address ) {
	UI_Import_Call( UI_LAN_REMOVESERVER, { source, UI_Import_Ptr( address ) } );
}

static void QDECL QL_UI_trap_LAN_ResetPings( int source ) {
	UI_Import_Call( UI_LAN_RESETPINGS, { source } );
}

static int QDECL QL_UI_trap_LAN_ServerStatus( const char *serverAddress, char *serverStatus, int maxLen ) {
	return static_cast<int>( UI_Import_Call( UI_LAN_SERVERSTATUS, { UI_Import_Ptr( serverAddress ), UI_Import_Ptr( serverStatus ), maxLen } ) );
}

static int QDECL QL_UI_trap_LAN_CompareServers( int source, int sortKey, int sortDir, int s1, int s2 ) {
	return static_cast<int>( UI_Import_Call( UI_LAN_COMPARESERVERS, { source, sortKey, sortDir, s1, s2 } ) );
}

static int QDECL QL_UI_trap_MemoryRemaining( void ) {
	return static_cast<int>( UI_Import_Call( UI_MEMORY_REMAINING, {} ) );
}

static void QDECL QL_UI_trap_GetCDKey( char *buf, int buflen ) {
	UI_Import_Call( UI_GET_CDKEY, { UI_Import_Ptr( buf ), buflen } );
}

static void QDECL QL_UI_trap_SetCDKey( char *buf ) {
	UI_Import_Call( UI_SET_CDKEY, { UI_Import_Ptr( buf ) } );
}

static int QDECL QL_UI_trap_SetCDKey_QL( char *buf ) {
	QL_UI_trap_SetCDKey( buf );
	return 0;
}

static int QDECL QL_UI_trap_PC_AddGlobalDefine( char *define ) {
	return static_cast<int>( UI_Import_Call( UI_PC_ADD_GLOBAL_DEFINE, { UI_Import_Ptr( define ) } ) );
}

static int QDECL QL_UI_trap_PC_LoadSource( const char *filename ) {
	return static_cast<int>( UI_Import_Call( UI_PC_LOAD_SOURCE, { UI_Import_Ptr( filename ) } ) );
}

static int QDECL QL_UI_trap_PC_FreeSource( int handle ) {
	return static_cast<int>( UI_Import_Call( UI_PC_FREE_SOURCE, { handle } ) );
}

static int QDECL QL_UI_trap_PC_ReadToken( int handle, pc_token_t *token ) {
	return static_cast<int>( UI_Import_Call( UI_PC_READ_TOKEN, { handle, UI_Import_Ptr( token ) } ) );
}

static int QDECL QL_UI_trap_PC_SourceFileAndLine( int handle, char *filename, int *line ) {
	return static_cast<int>( UI_Import_Call( UI_PC_SOURCE_FILE_AND_LINE, { handle, UI_Import_Ptr( filename ), UI_Import_Ptr( line ) } ) );
}

static void QDECL QL_UI_trap_S_StopBackgroundTrack( void ) {
	UI_Import_Call( UI_S_STOPBACKGROUNDTRACK, {} );
}

static void QDECL QL_UI_trap_S_StartBackgroundTrack( const char *intro, const char *loop ) {
	UI_Import_Call( UI_S_STARTBACKGROUNDTRACK, { UI_Import_Ptr( intro ), UI_Import_Ptr( loop ) } );
}

static int QDECL QL_UI_trap_CIN_PlayCinematic( const char *arg0, int xpos, int ypos, int width, int height, int bits ) {
	return static_cast<int>( UI_Import_Call( UI_CIN_PLAYCINEMATIC, { UI_Import_Ptr( arg0 ), xpos, ypos, width, height, bits } ) );
}

static e_status QDECL QL_UI_trap_CIN_StopCinematic( int handle ) {
	return static_cast<e_status>( UI_Import_Call( UI_CIN_STOPCINEMATIC, { handle } ) );
}

static e_status QDECL QL_UI_trap_CIN_RunCinematic( int handle ) {
	return static_cast<e_status>( UI_Import_Call( UI_CIN_RUNCINEMATIC, { handle } ) );
}

static void QDECL QL_UI_trap_CIN_DrawCinematic( int handle ) {
	UI_Import_Call( UI_CIN_DRAWCINEMATIC, { handle } );
}

static void QDECL QL_UI_trap_CIN_SetExtents( int handle, int x, int y, int w, int h ) {
	UI_Import_Call( UI_CIN_SETEXTENTS, { handle, x, y, w, h } );
}

static void QDECL QL_UI_trap_R_RemapShader( const char *oldShader, const char *newShader, const char *timeOffset ) {
	UI_Import_Call( UI_R_REMAP_SHADER, { UI_Import_Ptr( oldShader ), UI_Import_Ptr( newShader ), UI_Import_Ptr( timeOffset ) } );
}

static qboolean QDECL QL_UI_trap_VerifyCDKey( const char *key, const char *checksum ) {
	return UI_Import_Call( UI_VERIFY_CDKEY, { UI_Import_Ptr( key ), UI_Import_Ptr( checksum ) } ) ? qtrue : qfalse;
}

static void QDECL QL_UI_trap_SetPbClStatus( int status ) {
	UI_Import_Call( UI_SET_PBCLSTATUS, { status } );
}

static int QDECL QL_UI_trap_Launcher_ReadScreenshot( const char *requestedName, void *buffer, int bufferSize ) {
	return static_cast<int>( UI_Import_Call( UI_LAUNCHER_READSCREENSHOT, { UI_Import_Ptr( requestedName ), UI_Import_Ptr( buffer ), bufferSize } ) );
}

static qhandle_t QDECL QL_UI_trap_SetupAdvertCellShader( const char *defaultContent, const void *rect, int cellId ) {
	return CL_AdvertisementBridge_SetupUIAdvertCellShader( defaultContent, rect, cellId );
}

static qhandle_t QDECL QL_UI_trap_RefreshAdvertCellShader( const char *defaultContent, const void *rect, int cellId ) {
	return CL_AdvertisementBridge_RefreshUIAdvertCellShader( defaultContent, rect, cellId );
}

static void QDECL QL_UI_trap_InitAdvertisementBridge( void ) {
	CL_AdvertisementBridge_InitUI();
}

static void QDECL QL_UI_trap_UpdateAdvert( int handleOrToken, int area ) {
	(void)handleOrToken;
	(void)area;
}

static void QDECL QL_UI_trap_ActivateAdvert( int cellId ) {
	CL_AdvertisementBridge_ActivateAdvert( cellId );
}

static int QDECL QL_UI_trap_Unused85( void ) {
	return 0;
}

static int QDECL QL_UI_trap_SetCursorPos( int x, int y ) {
#if defined( _WIN32 )
	const qboolean moved = SetCursorPos( x, y ) ? qtrue : qfalse;

	CL_WebHost_SetCursorPosition( x, y );
	return moved ? 1 : 0;
#else
	CL_WebHost_SetCursorPosition( x, y );
	return 1;
#endif
}

static int QDECL QL_UI_trap_GetCursorPos( int *x, int *y ) {
#if defined( _WIN32 )
	POINT point;

	if ( GetCursorPos( &point ) ) {
		if ( x ) {
			*x = point.x;
		}
		if ( y ) {
			*y = point.y;
		}
		CL_WebHost_SetCursorPosition( point.x, point.y );
		return 1;
	}
#endif

	return CL_WebHost_GetCursorPosition( x, y ) ? 1 : 0;
}

static int QDECL QL_UI_trap_IsSubscribedApp( int appId ) {
	return CL_IsSubscribedApp( appId ) ? 1 : 0;
}

static void QDECL QL_UI_trap_DrawScaledText( int x, int y, const char *text, int fontHandle, float scale, int limit, float *maxX, int forceColor ) {
	RE_DrawScaledText( x, y, text, fontHandle, scale, limit, maxX, forceColor != qfalse ? qtrue : qfalse, ql_ui_currentColor );
}

static unsigned long long QL_UI_PackFloatBits64( float lo, float hi ) {
	unsigned long long result = static_cast<unsigned int>( FloatAsInt( lo ) );
	result |= static_cast<unsigned long long>( static_cast<unsigned int>( FloatAsInt( hi ) ) ) << 32;
	return result;
}

static unsigned long long QDECL QL_UI_trap_MeasureText( const char *text, const char *end, int fontHandle, float scale, int limit, float *outLeft ) {
	float bounds[5] = {};
	float width;
	float height;

	RE_MeasureScaledText( text, end, fontHandle, scale, limit, bounds );
	fnql::font::CopyMeasureBounds( outLeft, bounds );
	width = bounds[2] - bounds[0];
	height = bounds[4];
	return QL_UI_PackFloatBits64( width, height );
}

static void QDECL QL_UI_trap_GetItemDownloadInfo( unsigned int itemIdLow, unsigned int itemIdHigh, unsigned long long *outDownloaded, unsigned long long *outTotal ) {
	unsigned long long downloaded = 0ull;
	unsigned long long total = 0ull;

	if ( !CL_GetWorkshopDownloadInfo( itemIdLow, itemIdHigh, &downloaded, &total ) ) {
		CL_Steam_GetItemDownloadInfo( itemIdLow, itemIdHigh, &downloaded, &total );
	}

	if ( outDownloaded ) {
		*outDownloaded = downloaded;
	}
	if ( outTotal ) {
		*outTotal = total;
	}
}

static void CL_InitUIImports( void ) {
	Com_Memset( ql_ui_imports, 0, sizeof( ql_ui_imports ) );

	ql_ui_imports[UI_QL_IMPORT_PRINT] = (ql_import_f)QL_UI_trap_Print;
	ql_ui_imports[UI_QL_IMPORT_ERROR] = (ql_import_f)QL_UI_trap_Error;
	ql_ui_imports[UI_QL_IMPORT_MILLISECONDS] = (ql_import_f)QL_UI_trap_Milliseconds;
	ql_ui_imports[UI_QL_IMPORT_REAL_TIME] = (ql_import_f)QL_UI_trap_RealTime;
	ql_ui_imports[UI_QL_IMPORT_CVAR_REGISTER] = (ql_import_f)QL_UI_trap_Cvar_Register;
	ql_ui_imports[UI_QL_IMPORT_CVAR_CREATE] = (ql_import_f)QL_UI_trap_Cvar_Create;
	ql_ui_imports[UI_QL_IMPORT_CVAR_UPDATE] = (ql_import_f)QL_UI_trap_Cvar_Update;
	ql_ui_imports[UI_QL_IMPORT_CVAR_SET] = (ql_import_f)QL_UI_trap_Cvar_Set;
	ql_ui_imports[UI_QL_IMPORT_CVAR_SET_VALUE] = (ql_import_f)QL_UI_trap_Cvar_SetValue;
	ql_ui_imports[UI_QL_IMPORT_CVAR_VARIABLE_STRING_BUFFER] = (ql_import_f)QL_UI_trap_Cvar_VariableStringBuffer;
	ql_ui_imports[UI_QL_IMPORT_CVAR_VARIABLE_VALUE] = (ql_import_f)QL_UI_trap_Cvar_VariableValue;
	ql_ui_imports[UI_QL_IMPORT_ARGC] = (ql_import_f)QL_UI_trap_Argc;
	ql_ui_imports[UI_QL_IMPORT_ARGV] = (ql_import_f)QL_UI_trap_Argv;
	ql_ui_imports[UI_QL_IMPORT_CMD_ARGS_BUFFER] = (ql_import_f)QL_UI_trap_Cmd_ArgsBuffer_QL;
	ql_ui_imports[UI_QL_IMPORT_FS_FOPENFILE] = (ql_import_f)QL_UI_trap_FS_FOpenFile;
	ql_ui_imports[UI_QL_IMPORT_FS_READ] = (ql_import_f)QL_UI_trap_FS_Read;
	ql_ui_imports[UI_QL_IMPORT_FS_WRITE] = (ql_import_f)QL_UI_trap_FS_Write;
	ql_ui_imports[UI_QL_IMPORT_FS_FCLOSEFILE] = (ql_import_f)QL_UI_trap_FS_FCloseFile;
	ql_ui_imports[UI_QL_IMPORT_FS_SEEK] = (ql_import_f)QL_UI_trap_FS_Seek;
	ql_ui_imports[UI_QL_IMPORT_FS_GETFILELIST] = (ql_import_f)QL_UI_trap_FS_GetFileList;
	ql_ui_imports[UI_QL_IMPORT_CMD_EXECUTETEXT] = (ql_import_f)QL_UI_trap_Cmd_ExecuteText_QL;
	ql_ui_imports[UI_QL_IMPORT_R_REGISTERMODEL] = (ql_import_f)QL_UI_trap_R_RegisterModel;
	ql_ui_imports[UI_QL_IMPORT_R_REGISTERSKIN] = (ql_import_f)QL_UI_trap_R_RegisterSkin;
	ql_ui_imports[UI_QL_IMPORT_R_REGISTERSHADERNOMIP] = (ql_import_f)QL_UI_trap_R_RegisterShaderNoMip;
	ql_ui_imports[UI_QL_IMPORT_R_CLEARSCENE] = (ql_import_f)QL_UI_trap_R_ClearScene;
	ql_ui_imports[UI_QL_IMPORT_R_ADDREFENTITYTOSCENE] = (ql_import_f)QL_UI_trap_R_AddRefEntityToScene;
	ql_ui_imports[UI_QL_IMPORT_R_ADDPOLYTOSCENE] = (ql_import_f)QL_UI_trap_R_AddPolyToScene;
	ql_ui_imports[UI_QL_IMPORT_R_ADDLIGHTTOSCENE] = (ql_import_f)QL_UI_trap_R_AddLightToScene;
	ql_ui_imports[UI_QL_IMPORT_R_RENDERSCENE] = (ql_import_f)QL_UI_trap_R_RenderScene;
	ql_ui_imports[UI_QL_IMPORT_R_SETCOLOR] = (ql_import_f)QL_UI_trap_R_SetColor_QL;
	ql_ui_imports[UI_QL_IMPORT_R_DRAWSTRETCHPIC] = (ql_import_f)QL_UI_trap_R_DrawStretchPic;
	ql_ui_imports[UI_QL_IMPORT_R_MODELBOUNDS] = (ql_import_f)QL_UI_trap_R_ModelBounds;
	ql_ui_imports[UI_QL_IMPORT_UPDATESCREEN] = (ql_import_f)QL_UI_trap_UpdateScreen;
	ql_ui_imports[UI_QL_IMPORT_CM_LERPTAG] = (ql_import_f)QL_UI_trap_CM_LerpTag;
	ql_ui_imports[UI_QL_IMPORT_S_STARTLOCALSOUND] = (ql_import_f)QL_UI_trap_S_StartLocalSound;
	ql_ui_imports[UI_QL_IMPORT_S_REGISTERSOUND] = (ql_import_f)QL_UI_trap_S_RegisterSound_QL;
	ql_ui_imports[UI_QL_IMPORT_KEY_KEYNUMTOSTRINGBUF] = (ql_import_f)QL_UI_trap_Key_KeynumToStringBuf;
	ql_ui_imports[UI_QL_IMPORT_KEY_GETBINDINGBUF] = (ql_import_f)QL_UI_trap_Key_GetBindingBuf;
	ql_ui_imports[UI_QL_IMPORT_KEY_SETBINDING] = (ql_import_f)QL_UI_trap_Key_SetBinding;
	ql_ui_imports[UI_QL_IMPORT_KEY_ISDOWN] = (ql_import_f)QL_UI_trap_Key_IsDown;
	ql_ui_imports[UI_QL_IMPORT_KEY_GETOVERSTRIKEMODE] = (ql_import_f)QL_UI_trap_Key_GetOverstrikeMode;
	ql_ui_imports[UI_QL_IMPORT_KEY_SETOVERSTRIKEMODE] = (ql_import_f)QL_UI_trap_Key_SetOverstrikeMode;
	ql_ui_imports[UI_QL_IMPORT_KEY_CLEARSTATES] = (ql_import_f)QL_UI_trap_Key_ClearStates;
	ql_ui_imports[UI_QL_IMPORT_KEY_GETCATCHER] = (ql_import_f)QL_UI_trap_Key_GetCatcher;
	ql_ui_imports[UI_QL_IMPORT_KEY_SETCATCHER] = (ql_import_f)QL_UI_trap_Key_SetCatcher;
	ql_ui_imports[UI_QL_IMPORT_GETCLIPBOARDDATA] = (ql_import_f)QL_UI_trap_GetClipboardData;
	ql_ui_imports[UI_QL_IMPORT_GETCLIENTSTATE] = (ql_import_f)QL_UI_trap_GetClientState;
	ql_ui_imports[UI_QL_IMPORT_GETGLCONFIG] = (ql_import_f)QL_UI_trap_GetGlconfig;
	ql_ui_imports[UI_QL_IMPORT_GETCONFIGSTRING] = (ql_import_f)QL_UI_trap_GetConfigString;
	ql_ui_imports[UI_QL_IMPORT_LAN_GETSERVERCOUNT] = (ql_import_f)QL_UI_trap_LAN_GetServerCount;
	ql_ui_imports[UI_QL_IMPORT_LAN_GETSERVERADDRESSSTRING] = (ql_import_f)QL_UI_trap_LAN_GetServerAddressString;
	ql_ui_imports[UI_QL_IMPORT_LAN_GETSERVERINFO] = (ql_import_f)QL_UI_trap_LAN_GetServerInfo;
	ql_ui_imports[UI_QL_IMPORT_LAN_GETSERVERPING] = (ql_import_f)QL_UI_trap_LAN_GetServerPing;
	ql_ui_imports[UI_QL_IMPORT_LAN_GETPINGQUEUECOUNT] = (ql_import_f)QL_UI_trap_LAN_GetPingQueueCount;
	ql_ui_imports[UI_QL_IMPORT_LAN_CLEARPING] = (ql_import_f)QL_UI_trap_LAN_ClearPing;
	ql_ui_imports[UI_QL_IMPORT_LAN_GETPING] = (ql_import_f)QL_UI_trap_LAN_GetPing;
	ql_ui_imports[UI_QL_IMPORT_LAN_GETPINGINFO] = (ql_import_f)QL_UI_trap_LAN_GetPingInfo;
	ql_ui_imports[UI_QL_IMPORT_LAN_LOADCACHEDSERVERS] = (ql_import_f)QL_UI_trap_LAN_LoadCachedServers;
	ql_ui_imports[UI_QL_IMPORT_LAN_SAVECACHEDSERVERS] = (ql_import_f)QL_UI_trap_LAN_SaveCachedServers;
	ql_ui_imports[UI_QL_IMPORT_LAN_MARKSERVERVISIBLE] = (ql_import_f)QL_UI_trap_LAN_MarkServerVisible;
	ql_ui_imports[UI_QL_IMPORT_LAN_SERVERISVISIBLE] = (ql_import_f)QL_UI_trap_LAN_ServerIsVisible;
	ql_ui_imports[UI_QL_IMPORT_LAN_UPDATEVISIBLEPINGS] = (ql_import_f)QL_UI_trap_LAN_UpdateVisiblePings;
	ql_ui_imports[UI_QL_IMPORT_LAN_ADDSERVER] = (ql_import_f)QL_UI_trap_LAN_AddServer;
	ql_ui_imports[UI_QL_IMPORT_LAN_REMOVESERVER] = (ql_import_f)QL_UI_trap_LAN_RemoveServer;
	ql_ui_imports[UI_QL_IMPORT_LAN_RESETPINGS] = (ql_import_f)QL_UI_trap_LAN_ResetPings;
	ql_ui_imports[UI_QL_IMPORT_LAN_SERVERSTATUS] = (ql_import_f)QL_UI_trap_LAN_ServerStatus;
	ql_ui_imports[UI_QL_IMPORT_LAN_COMPARESERVERS] = (ql_import_f)QL_UI_trap_LAN_CompareServers;
	ql_ui_imports[UI_QL_IMPORT_MEMORY_REMAINING] = (ql_import_f)QL_UI_trap_MemoryRemaining;
	ql_ui_imports[UI_QL_IMPORT_GET_CDKEY] = (ql_import_f)QL_UI_trap_GetCDKey;
	ql_ui_imports[UI_QL_IMPORT_SET_CDKEY] = (ql_import_f)QL_UI_trap_SetCDKey_QL;
	ql_ui_imports[UI_QL_IMPORT_R_REGISTERFONT] = (ql_import_f)QL_UI_trap_R_RegisterFont;
	ql_ui_imports[UI_QL_IMPORT_S_STOPBACKGROUNDTRACK] = (ql_import_f)QL_UI_trap_S_StopBackgroundTrack;
	ql_ui_imports[UI_QL_IMPORT_S_STARTBACKGROUNDTRACK] = (ql_import_f)QL_UI_trap_S_StartBackgroundTrack;
	ql_ui_imports[UI_QL_IMPORT_CIN_PLAYCINEMATIC] = (ql_import_f)QL_UI_trap_CIN_PlayCinematic;
	ql_ui_imports[UI_QL_IMPORT_CIN_STOPCINEMATIC] = (ql_import_f)QL_UI_trap_CIN_StopCinematic;
	ql_ui_imports[UI_QL_IMPORT_CIN_DRAWCINEMATIC] = (ql_import_f)QL_UI_trap_CIN_DrawCinematic;
	ql_ui_imports[UI_QL_IMPORT_CIN_RUNCINEMATIC] = (ql_import_f)QL_UI_trap_CIN_RunCinematic;
	ql_ui_imports[UI_QL_IMPORT_CIN_SETEXTENTS] = (ql_import_f)QL_UI_trap_CIN_SetExtents;
	ql_ui_imports[UI_QL_IMPORT_R_REMAP_SHADER] = (ql_import_f)QL_UI_trap_R_RemapShader;
	ql_ui_imports[UI_QL_IMPORT_VERIFY_CDKEY] = (ql_import_f)QL_UI_trap_VerifyCDKey;
	ql_ui_imports[UI_QL_IMPORT_SETUP_ADVERT_CELL_SHADER] = (ql_import_f)QL_UI_trap_SetupAdvertCellShader;
	ql_ui_imports[UI_QL_IMPORT_REFRESH_ADVERT_CELL_SHADER] = (ql_import_f)QL_UI_trap_RefreshAdvertCellShader;
	ql_ui_imports[UI_QL_IMPORT_INIT_ADVERTISEMENT_BRIDGE] = (ql_import_f)QL_UI_trap_InitAdvertisementBridge;
	ql_ui_imports[UI_QL_IMPORT_UNUSED_83] = (ql_import_f)QL_UI_trap_UpdateAdvert;
	ql_ui_imports[UI_QL_IMPORT_ACTIVATE_ADVERT] = (ql_import_f)QL_UI_trap_ActivateAdvert;
	ql_ui_imports[UI_QL_IMPORT_UNUSED_85] = (ql_import_f)QL_UI_trap_Unused85;
	ql_ui_imports[UI_QL_IMPORT_SET_CURSOR_POS] = (ql_import_f)QL_UI_trap_SetCursorPos;
	ql_ui_imports[UI_QL_IMPORT_GET_CURSOR_POS] = (ql_import_f)QL_UI_trap_GetCursorPos;
	ql_ui_imports[UI_QL_IMPORT_PC_ADD_GLOBAL_DEFINE] = (ql_import_f)QL_UI_trap_PC_AddGlobalDefine;
	ql_ui_imports[UI_QL_IMPORT_PC_LOAD_SOURCE] = (ql_import_f)QL_UI_trap_PC_LoadSource;
	ql_ui_imports[UI_QL_IMPORT_PC_FREE_SOURCE] = (ql_import_f)QL_UI_trap_PC_FreeSource;
	ql_ui_imports[UI_QL_IMPORT_PC_READ_TOKEN] = (ql_import_f)QL_UI_trap_PC_ReadToken;
	ql_ui_imports[UI_QL_IMPORT_PC_SOURCE_FILE_AND_LINE] = (ql_import_f)QL_UI_trap_PC_SourceFileAndLine;
	ql_ui_imports[UI_QL_IMPORT_IS_SUBSCRIBED_APP] = (ql_import_f)QL_UI_trap_IsSubscribedApp;
	ql_ui_imports[UI_QL_IMPORT_DRAW_SCALED_TEXT] = (ql_import_f)QL_UI_trap_DrawScaledText;
	ql_ui_imports[UI_QL_IMPORT_MEASURE_TEXT] = (ql_import_f)QL_UI_trap_MeasureText;
	ql_ui_imports[UI_QL_IMPORT_GET_ITEM_DOWNLOAD_INFO] = (ql_import_f)QL_UI_trap_GetItemDownloadInfo;
	ql_ui_imports[UI_QL_IMPORT_CVAR_RESET] = (ql_import_f)QL_UI_trap_Cvar_Reset;
	ql_ui_imports[UI_QL_IMPORT_CVAR_INFOSTRINGBUFFER] = (ql_import_f)QL_UI_trap_Cvar_InfoStringBuffer;
	ql_ui_imports[UI_QL_IMPORT_CM_LOADMODEL] = (ql_import_f)QL_UI_trap_CM_LoadModel;
	ql_ui_imports[UI_QL_IMPORT_SET_PBCLSTATUS] = (ql_import_f)QL_UI_trap_SetPbClStatus;
	ql_ui_imports[UI_QL_IMPORT_LAUNCHER_READSCREENSHOT] = (ql_import_f)QL_UI_trap_Launcher_ReadScreenshot;
}


/*
====================
CL_UIMenusAreVisible

The retail UI keeps its own menu stack.  Input ownership alone cannot tell us
whether reactivating the main menu would replace an already-visible menu.
====================
*/
qboolean CL_UIMenusAreVisible( void ) {
	return ( uivm && VM_Call( uivm, 0, UI_MENUS_ANY_VISIBLE ) ) ? qtrue : qfalse;
}


/*
====================
CL_ShutdownUI
====================
*/
void CL_ShutdownUI( void ) {
	Key_SetCatcher( Key_GetCatcher() & ~KEYCATCH_UI );
	cls.uiStarted = qfalse;
	if ( !uivm ) {
		return;
	}
	VM_Call( uivm, 0, UI_SHUTDOWN );
	VM_Free( uivm );
	uivm = nullptr;
	FS_VM_CloseFiles( H_Q3UI );
}


/*
====================
CL_InitUI
====================
*/
#define UI_OLD_API_VERSION	4

void CL_InitUI( void ) {
	int		v;
	vmInterpret_t		interpret;

	// disallow vl.collapse for UI elements
	re.VertexLighting( qfalse );

	// load the dll or bytecode
	interpret = static_cast<vmInterpret_t>( Cvar_VariableIntegerValue( "vm_ui" ) );
	if ( cl_connectedToPureServer )
	{
		// Retail QL has no UI QVM. A pure server may reload only the exact
		// native image pinned before its filesystem policy took effect.
		if ( interpret != VMI_COMPILED && interpret != VMI_BYTECODE ) {
			interpret = VM_HasPinnedNativeModule( VM_UI )
				? VMI_PINNED_NATIVE : VMI_COMPILED;
		}
	}

	CL_InitUIImports();
	uivm = VM_CreateNative( VM_UI, CL_UISystemCalls, UI_DllSyscall, interpret, ql_ui_imports, UI_QL_API_VERSION );
	if ( !uivm ) {
		if ( cl_connectedToPureServer && CL_GameSwitch() ) {
			// server-side modification may require and reference only single custom ui.qvm
			// so allow referencing everything until we download all files
			// new gamestate will be requested after downloads complete
			// which will correct filesystem permissions
			fs_reordered = qfalse;
			FS_PureServerSetLoadedPaks( "", "" );
			uivm = VM_CreateNative( VM_UI, CL_UISystemCalls, UI_DllSyscall, interpret, ql_ui_imports, UI_QL_API_VERSION );
			if ( !uivm ) {
				Com_Error( ERR_FATAL, "VM_Create on UI failed; retail Quake Live on Windows requires the Win32 FnQL build" );
			}
		} else {
			Com_Error( ERR_FATAL, "VM_Create on UI failed; retail Quake Live on Windows requires the Win32 FnQL build" );
		}
	}

	// sanity check
	v = VM_Call( uivm, 0, UI_GETAPIVERSION );
	if ( v != UI_OLD_API_VERSION && v != UI_Q3_API_VERSION && v != UI_API_VERSION && v != UI_QL_API_VERSION ) {
		// Free uivm now, so UI_SHUTDOWN doesn't get called later.
		VM_Free( uivm );
		uivm = nullptr;

		Com_Error( ERR_DROP, "User Interface is version %d, expected %d or %d", v, UI_API_VERSION, UI_QL_API_VERSION );
		cls.uiStarted = qfalse;
		return;
	}

	// init for this gamestate
	VM_Call( uivm, 1, UI_INIT, (cls.state >= CA_AUTHORIZING && cls.state < CA_ACTIVE) );
}


#ifndef STANDALONE
qboolean UI_usesUniqueCDKey( void ) {
	if (uivm) {
		return (VM_Call( uivm, 0, UI_HASUNIQUECDKEY ) != 0) ? qtrue : qfalse;
	} else {
		return qfalse;
	}
}
#endif


/*
====================
UI_GameCommand

See if the current console command is claimed by the ui
====================
*/
qboolean UI_GameCommand( void ) {
	if ( !uivm ) {
		return qfalse;
	}

	return VM_Call( uivm, 1, UI_CONSOLE_COMMAND, cls.realtime ) ? qtrue : qfalse;
}
