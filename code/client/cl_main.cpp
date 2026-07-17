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
// cl_main.cpp -- client main loop

extern "C" {
#include "client.h"
}

#include "client_cpp.h"
#include "demo_stream.hpp"
#include "retail_challenge.hpp"
#include "../qcommon/protocol_contract.hpp"
#include "../qcommon/netchan_safety.hpp"
#include "../platform/fnql_steam.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <system_error>

using fnql::CloseFile;
using fnql::FileRead;
using fnql::FileWrite;
using fnql::OpenFileRead;
using fnql::OpenSvFileRead;
using fnql::ReadUnaligned;
using fnql::ScopedFileHandle;
using fnql::ScopedFilePosition;
using fnql::ToQboolean;

namespace {

constexpr const char *LEGACY_DEMOEXT = "dm3";
constexpr int kMaxServersPerPacket = 256;
constexpr std::size_t kServerAddressHashBuckets = 1024;

struct SteamChallengeTicket {
	std::array<byte, MAX_PACKETLEN> bytes{};
	std::uint32_t size = 0;
	std::uint32_t handle = 0;
	std::uint64_t steamId = 0;
	fnql::client::auth::ChallengeRequestMode requestMode =
		fnql::client::auth::ChallengeRequestMode::None;
};

SteamChallengeTicket steamChallengeTicket;

static void CL_ClearSteamChallengeTicket() {
	if ( steamChallengeTicket.handle != 0 ) {
		FNQL_Steam_CancelAuthTicket( steamChallengeTicket.handle );
	}
	volatile byte *ticket = steamChallengeTicket.bytes.data();
	for ( std::size_t i = 0; i < steamChallengeTicket.bytes.size(); ++i ) {
		ticket[i] = 0;
	}
	steamChallengeTicket = {};
}

static void CL_EraseSteamChallengePayload() {
	volatile byte *ticket = steamChallengeTicket.bytes.data();
	for ( std::size_t i = 0; i < steamChallengeTicket.bytes.size(); ++i ) {
		ticket[i] = 0;
	}
	steamChallengeTicket.size = 0;
	steamChallengeTicket.requestMode =
		fnql::client::auth::ChallengeRequestMode::None;
}

static bool CL_AcquireSteamChallengeTicket( std::uint32_t capacity ) {
	if ( steamChallengeTicket.handle != 0 && steamChallengeTicket.size != 0 &&
		steamChallengeTicket.steamId != 0 ) {
		return steamChallengeTicket.size <= capacity;
	}
	if ( steamChallengeTicket.handle != 0 ) {
		CL_ClearSteamChallengeTicket();
	}
	if ( !FNQL_Steam_Available( FNQL_STEAM_CAP_AUTH | FNQL_STEAM_CAP_IDENTITY ) ) {
		return false;
	}
	fnqlSteamStatus_t status{};
	status.size = sizeof( status );
	if ( !FNQL_Steam_GetStatus( &status ) || status.local_steam_id == 0 ) {
		return false;
	}
	std::uint32_t ticketSize = 0;
	std::uint32_t ticketHandle = 0;
	if ( FNQL_Steam_GetAuthTicket( steamChallengeTicket.bytes.data(), capacity,
		&ticketSize, &ticketHandle ) != FNQL_STEAM_RESULT_OK ||
		ticketSize == 0 || ticketSize > capacity || ticketHandle == 0 ) {
		CL_ClearSteamChallengeTicket();
		return false;
	}
	steamChallengeTicket.size = ticketSize;
	steamChallengeTicket.handle = ticketHandle;
	steamChallengeTicket.steamId = status.local_steam_id;
	return true;
}

static bool CL_SendRetailSteamChallenge() {
	constexpr std::size_t headerBytes =
		fnql::client::auth::RetailSteamChallengeHeaderBytes;
	static_assert( headerBytes < MAX_PACKETLEN );
	const std::uint32_t ticketCapacity = static_cast<std::uint32_t>(
		MAX_PACKETLEN - headerBytes );
	if ( !CL_AcquireSteamChallengeTicket( ticketCapacity ) ) {
		return false;
	}
	std::array<byte, MAX_PACKETLEN> packet{};
	const std::size_t packetBytes =
		fnql::client::auth::BuildRetailSteamChallenge(
			packet.data(), packet.size(), steamChallengeTicket.steamId,
			steamChallengeTicket.bytes.data(), steamChallengeTicket.size );
	if ( packetBytes == 0 ) {
		CL_ClearSteamChallengeTicket();
		return false;
	}
	NET_SendPacket( NS_CLIENT, static_cast<int>( packetBytes ), packet.data(),
		&clc.serverAddress );
	steamChallengeTicket.requestMode =
		fnql::client::auth::ChallengeRequestMode::RetailSteam;
	return true;
}

static int OpenPureFileRead( const char *qpath, ScopedFileHandle &file )
{
	FS_BypassPure();
	const int length = OpenFileRead( qpath, file, qtrue );
	FS_RestorePure();
	return length;
}

} // namespace

cvar_t	*cl_noprint;
cvar_t	*cl_debugMove;
cvar_t	*cl_motd;

#ifdef USE_RENDERER_DLOPEN
static cvar_t *cl_renderer;
static bool isValidRenderer( const char *s );
#endif

cvar_t	*rcon_client_password;
cvar_t	*rconAddress;

cvar_t	*cl_timeout;
cvar_t	*cl_autoNudge;
cvar_t	*cl_timeNudge;
cvar_t	*cl_showTimeDelta;

cvar_t	*cl_shownet;
cvar_t	*cl_autoRecordDemo;
cvar_t	*cl_freezeDemo;
cvar_t	*cl_drawRecording;
cvar_t	*cl_menuAspect;
cvar_t	*cl_menuDepthOfField;
cvar_t	*cl_menuDepthOfFieldTime;
cvar_t	*cl_cinematicAspect;
cvar_t	*cl_captureActive;
cvar_t	*cl_playerHighlight;
cvar_t	*cl_playerHighlightRimIntensity;
cvar_t	*cl_playerHighlightOutlineIntensity;
cvar_t	*cl_playerHighlightOutlineScale;
cvar_t	*cl_playerHighlightRedColor;
cvar_t	*cl_playerHighlightBlueColor;
cvar_t	*cl_playerHighlightFreeColor;
cvar_t	*cl_playerHighlightEnemyColor;
cvar_t	*cl_playerHighlightTeammateColor;
cvar_t	*r_levelshotHideHud;
cvar_t	*r_levelshotHideViewWeapon;

cvar_t	*cl_aviFrameRate;
cvar_t	*cl_aviMotionJpeg;
cvar_t	*cl_forceavidemo;
cvar_t	*cl_aviPipeFormat;

cvar_t	*cl_activeAction;

cvar_t	*cl_motdString;

cvar_t	*cl_allowDownload;
#ifdef USE_CURL
cvar_t	*cl_mapAutoDownload;
#endif
cvar_t	*cl_conXOffset;
cvar_t	*cl_conColor;
cvar_t	*cl_inGameVideo;

cvar_t	*cl_serverStatusResendTime;

cvar_t	*cl_lanForcePackets;

cvar_t	*cl_guidServerUniq;

cvar_t	*cl_dlURL;
cvar_t	*cl_dlDirectory;

cvar_t	*cl_reconnectArgs;

// common cvars for GLimp modules
cvar_t	*vid_xpos;			// X coordinate of window position
cvar_t	*vid_ypos;			// Y coordinate of window position
cvar_t	*r_noborder;

cvar_t *r_allowSoftwareGL;	// don't abort out if the pixelformat claims software
cvar_t *r_swapInterval;
cvar_t *r_glDriver;
cvar_t *r_displayRefresh;
cvar_t *r_fullscreen;
cvar_t *r_mode;
cvar_t *r_modeFullscreen;
cvar_t *r_customwidth;
cvar_t *r_customheight;
cvar_t *r_customPixelAspect;

cvar_t *r_colorbits;
// these also shared with renderers:
cvar_t *cl_stencilbits;
cvar_t *cl_depthbits;
cvar_t *cl_drawBuffer;

clientActive_t		cl;
clientConnection_t	clc;
clientStatic_t		cls;
vm_t				*cgvm = nullptr;

netadr_t			rcon_address;

char				cl_oldGame[ MAX_QPATH ];
qboolean			cl_oldGameSet;
static bool noGameRestart = false;

/*
====================
CL_IsLegacyDemoExt
====================
*/
static qboolean CL_IsLegacyDemoExt( const char *ext )
{
	return ToQboolean( !Q_stricmp( ext, LEGACY_DEMOEXT ) );
}

/*
====================
CL_ParseDemoProtocolExtension

Parses a complete dm_NN extension without accepting atoi-style trailing data.
Protocol support is deliberately checked separately so callers can distinguish
an invalid filename from a well-formed demo for an unsupported protocol.
====================
*/
static bool CL_ParseDemoProtocolExtension( const char *ext, int *protocol )
{
	constexpr std::size_t prefixLength = sizeof( DEMOEXT ) - 1;
	const char *first;
	const char *last;
	std::from_chars_result result;
	int parsedProtocol = 0;

	if ( !ext || !protocol || Q_stricmpn( ext, DEMOEXT,
		static_cast<int>( prefixLength ) ) != 0 ) {
		return false;
	}

	first = ext + prefixLength;
	last = first + std::strlen( first );
	if ( first == last ) {
		return false;
	}

	result = std::from_chars( first, last, parsedProtocol );
	if ( result.ec != std::errc{} || result.ptr != last || parsedProtocol <= 0 ) {
		return false;
	}

	*protocol = parsedProtocol;
	return true;
}

static bool CL_DemoProtocolSupported( int protocol )
{
	return Com_DemoProtocolSupported( protocol ) ||
		( com_protocol && protocol == com_protocol->integer );
}

/*
====================
CL_DetectLegacyDemoProtocol

Pre-1.27 demos store an uncompressed gamestate in the first message. Scan that
message for the literal `protocol\NN` key so the parser can pick the correct
43/46/48-era field tables before playback starts.
====================
*/
static int CL_DetectLegacyDemoProtocol( fileHandle_t handle )
{
	std::array<byte, MAX_MSGLEN> buffer;
	fnql::demo::EnvelopeResult envelope;
	int length;
	int protocol;
	int i;

	ScopedFilePosition restorePosition( handle );
	if ( !restorePosition.valid() ) {
		return 43;
	}

	if ( FS_Seek( handle, 0, FS_SEEK_SET ) < 0 ) {
		return 43;
	}

	envelope = fnql::demo::ReadEnvelope(
		[handle]( std::uint8_t *destination, std::size_t bytes ) {
			return FileRead( handle, destination, bytes );
		}, buffer.data(), buffer.size() );
	if ( envelope.status != fnql::demo::EnvelopeStatus::Message ||
		envelope.payloadLength <= 0 ) {
		return 43;
	}
	length = envelope.payloadLength;

	for ( i = 0; i + 10 < length; i++ ) {
		const char *scan = reinterpret_cast<const char *>( &buffer[i] );

		if ( Q_strncmp( scan, "protocol\\", 9 ) != 0 ) {
			continue;
		}

		if ( scan[9] < '0' || scan[9] > '9' || scan[10] < '0' || scan[10] > '9' ) {
			continue;
		}

		protocol = ( scan[9] - '0' ) * 10 + ( scan[10] - '0' );
		if ( protocol >= 43 && protocol <= 48 ) {
			return protocol;
		}
	}

	return 43;
}

#ifdef USE_CURL
download_t			download;
#endif

// Structure containing functions exported from refresh DLL
refexport_t	re;
#ifdef USE_RENDERER_DLOPEN
static void	*rendererLib;
#endif

static std::array<ping_t, MAX_PINGREQUESTS> cl_pinglist;


static void CL_MigrateLegacyPlayerHighlightCvar( cvar_t *target, const char *legacyName, const char *legacyDefault ) {
	const char *legacyValue;

	if ( !target || !legacyName || !legacyDefault ) {
		return;
	}

	legacyValue = Cvar_VariableString( legacyName );
	if ( !legacyValue[0] || !Q_stricmp( legacyValue, legacyDefault ) ) {
		return;
	}

	if ( Q_stricmp( target->string, target->resetString ) ) {
		return;
	}

	Cvar_Set( target->name, legacyValue );
}

struct serverStatus_t {
	std::array<char, BIG_INFO_STRING> string;
	netadr_t address;
	int time, startTime;
	bool pending;
	bool print;
	bool retrieved;
};

static std::array<serverStatus_t, MAX_SERVERSTATUSREQUESTS> cl_serverStatusList;

static void CL_CheckForResend( void );
static void CL_ShowIP_f( void );
static void CL_ServerStatus_f( void );
static void CL_ServerStatusResponse( const netadr_t *from, msg_t *msg );
static void CL_ServerInfoPacket( const netadr_t *from, msg_t *msg );

#ifdef USE_CURL
static void CL_Download_f( void );
#endif
static void CL_LocalServers_f( void );
static void CL_GlobalServers_f( void );
static void CL_Ping_f( void );

static void CL_InitRef( void );
static void CL_ShutdownRef( refShutdownCode_t code );
static void CL_InitGLimp_Cvars( void );

static void CL_NextDemo( void );

/*
===============
CL_CDDialog

Called by Com_Error when a cd is needed
===============
*/
void CL_CDDialog( void ) {
	cls.cddialog = qtrue;	// start it next frame
}


/*
=======================================================================

CLIENT RELIABLE COMMAND COMMUNICATION

=======================================================================
*/

/*
======================
CL_AddReliableCommand

The given command will be transmitted to the server, and is guaranteed to
not have future usercmd_t executed before it is executed
======================
*/
void CL_AddReliableCommand( const char *cmd, qboolean isDisconnectCmd ) {
	int		index;
	std::uint32_t unacknowledged = 0;

	if ( clc.serverAddress.type == NA_BAD )
		return;

	// if we would be losing an old command that hasn't been acknowledged,
	// we must drop the connection
	// also leave one slot open for the disconnect command in this case.

	if ( !fnql::net::PendingCounterCount( clc.reliableSequence,
		clc.reliableAcknowledge, unacknowledged ) ||
		(isDisconnectCmd && unacknowledged > MAX_RELIABLE_COMMANDS) ||
		(!isDisconnectCmd && unacknowledged >= MAX_RELIABLE_COMMANDS))
	{
		if( com_errorEntered )
			return;
		else
			Com_Error(ERR_DROP, "Client command overflow");
	}

	clc.reliableSequence = fnql::net::NextCounter( clc.reliableSequence );
	index = clc.reliableSequence & ( MAX_RELIABLE_COMMANDS - 1 );
	Q_strncpyz( clc.reliableCommands[ index ], cmd, sizeof( clc.reliableCommands[ index ] ) );
}


/*
=======================================================================

CLIENT SIDE DEMO RECORDING

=======================================================================
*/

/*
====================
CL_WriteDemoMessage

Dumps the current net message, prefixed by the length
====================
*/
static void CL_WriteDemoMessage( msg_t *msg, int headerBytes ) {
	int		len, swlen;

	// write the packet sequence
	len = clc.serverMessageSequence;
	swlen = LittleLong( len );
	FileWrite( clc.recordfile, &swlen, 4 );

	// skip the packet sequencing information
	len = msg->cursize - headerBytes;
	swlen = LittleLong(len);
	FileWrite( clc.recordfile, &swlen, 4 );
	FileWrite( clc.recordfile, msg->data + headerBytes, len );
}


/*
====================
CL_StopRecording_f

stop recording a demo
====================
*/
void CL_StopRecord_f( void ) {

	if ( clc.recordfile != FS_INVALID_HANDLE ) {
		std::array<char, MAX_OSPATH> tempName;
		std::array<char, MAX_OSPATH> finalName;
		int protocol;
		int	len, sequence;

		// finish up
		len = LittleLong( -1 );
		FileWrite( clc.recordfile, &len, 4 );
		FileWrite( clc.recordfile, &len, 4 );
		CloseFile( clc.recordfile );

		// Select the extension from the actual wire contract. Explicit legacy
		// demo playback retains the source protocol recorded in the file.
		if ( clc.dm68compat ) {
			protocol = OLD_PROTOCOL_VERSION;
		} else if ( clc.demoplaying && clc.demoLegacyProtocol ) {
			protocol = clc.demoLegacyProtocol;
		} else {
			protocol = fnql::protocol::ForWireProfile(
				clc.netchan.wireProfile ).demoProtocol;
		}

		Com_sprintf( tempName.data(), static_cast<int>( tempName.size() ), "%s.tmp", clc.recordName );

		Com_sprintf( finalName.data(), static_cast<int>( finalName.size() ), "%s.%s%d", clc.recordName, DEMOEXT, protocol );

		if ( clc.explicitRecordName ) {
			FS_Remove( finalName.data() );
		} else {
			// add sequence suffix to avoid overwrite
			sequence = 0;
			while ( FS_FileExists( finalName.data() ) && ++sequence < 1000 ) {
				Com_sprintf( finalName.data(), static_cast<int>( finalName.size() ), "%s-%02d.%s%d",
					clc.recordName, sequence, DEMOEXT, protocol );
			}
		}

		FS_Rename( tempName.data(), finalName.data() );
	}

	if ( !clc.demorecording ) {
		Com_Printf( "Not recording a demo.\n" );
	} else {
		Com_Printf( "Stopped demo recording.\n" );
	}

	clc.demorecording = qfalse;
	clc.spDemoRecording = qfalse;
}


/*
====================
CL_WriteServerCommands
====================
*/
static void CL_WriteServerCommands( msg_t *msg ) {
	int i;

	if ( clc.serverCommandSequence - clc.demoCommandSequence > 0 ) {

		// do not write more than MAX_RELIABLE_COMMANDS
		if ( clc.serverCommandSequence - clc.demoCommandSequence > MAX_RELIABLE_COMMANDS ) {
			clc.demoCommandSequence = clc.serverCommandSequence - MAX_RELIABLE_COMMANDS;
		}

		for ( i = clc.demoCommandSequence + 1 ; i <= clc.serverCommandSequence; i++ ) {
			MSG_WriteByte( msg, svc_serverCommand );
			MSG_WriteLong( msg, i );
			MSG_WriteString( msg, clc.serverCommands[ i & (MAX_RELIABLE_COMMANDS-1) ] );
		}
	}

	clc.demoCommandSequence = clc.serverCommandSequence;
}


/*
====================
CL_WriteGamestate
====================
*/
static void CL_WriteGamestate( qboolean initial )
{
	std::array<byte, MAX_MSGLEN_BUF> bufData;
	char		*s;
	msg_t		msg;
	int			i;
	int			len;
	entityState_t	*ent;
	entityState_t	nullstate{};

	// write out the gamestate message
	MSG_Init( &msg, bufData.data(), MAX_MSGLEN );
	MSG_Bitstream( &msg );

	// NOTE, MRE: all server->client messages now acknowledge
	MSG_WriteLong( &msg, clc.reliableSequence );

	if ( initial ) {
		clc.demoMessageSequence = 1;
		clc.demoCommandSequence = clc.serverCommandSequence;
	} else {
		CL_WriteServerCommands( &msg );
	}

	clc.demoDeltaNum = 0; // reset delta for next snapshot

	MSG_WriteByte( &msg, svc_gamestate );
	MSG_WriteLong( &msg, clc.serverCommandSequence );

	// configstrings
	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		if ( !cl.gameState.stringOffsets[i] ) {
			continue;
		}
		s = cl.gameState.stringData + cl.gameState.stringOffsets[i];
		MSG_WriteByte( &msg, svc_configstring );
		MSG_WriteShort( &msg, i );
		MSG_WriteBigString( &msg, s );
	}

	// baselines
	for ( i = 0; i < MAX_GENTITIES ; i++ ) {
		if ( !cl.baselineUsed[ i ] )
			continue;
		ent = &cl.entityBaselines[ i ];
		MSG_WriteByte( &msg, svc_baseline );
		MSG_WriteDeltaEntity( &msg, &nullstate, ent, qtrue );
	}

	// finalize message
	MSG_WriteByte( &msg, svc_EOF );

	// finished writing the gamestate stuff

	// write the client num
	MSG_WriteLong( &msg, clc.clientNum );

	// write the checksum feed
	MSG_WriteLong( &msg, clc.checksumFeed );

	// finished writing the client packet
	MSG_WriteByte( &msg, svc_EOF );

	// write it to the demo file
	if ( clc.demoplaying )
		len = LittleLong( clc.demoMessageSequence - 1 );
	else
		len = LittleLong( clc.serverMessageSequence - 1 );

	FileWrite( clc.recordfile, &len, 4 );

	len = LittleLong( msg.cursize );
	FileWrite( clc.recordfile, &len, 4 );
	FileWrite( clc.recordfile, msg.data, msg.cursize );
}


/*
=============
CL_EmitPacketEntities
=============
*/
static void CL_EmitPacketEntities( clSnapshot_t *from, clSnapshot_t *to, msg_t *msg, entityState_t *oldents ) {
	entityState_t	*oldent, *newent;
	int		oldindex, newindex;
	int		oldnum, newnum;
	int		from_num_entities;

	// generate the delta update
	if ( !from ) {
		from_num_entities = 0;
	} else {
		from_num_entities = from->numEntities;
	}

	newent = nullptr;
	oldent = nullptr;
	newindex = 0;
	oldindex = 0;
	while ( newindex < to->numEntities || oldindex < from_num_entities ) {
		if ( newindex >= to->numEntities ) {
			newnum = MAX_GENTITIES+1;
		} else {
			newent = &cl.parseEntities[(to->parseEntitiesNum + newindex) % MAX_PARSE_ENTITIES];
			newnum = newent->number;
		}

		if ( oldindex >= from_num_entities ) {
			oldnum = MAX_GENTITIES+1;
		} else {
			//oldent = &cl.parseEntities[(from->parseEntitiesNum + oldindex) % MAX_PARSE_ENTITIES];
			oldent = &oldents[ oldindex ];
			oldnum = oldent->number;
		}

		if ( newnum == oldnum ) {
			// delta update from old position
			// because the force parm is qfalse, this will not result
			// in any bytes being emitted if the entity has not changed at all
			MSG_WriteDeltaEntity (msg, oldent, newent, qfalse );
			oldindex++;
			newindex++;
			continue;
		}

		if ( newnum < oldnum ) {
			// this is a new entity, send it from the baseline
			MSG_WriteDeltaEntity (msg, &cl.entityBaselines[newnum], newent, qtrue );
			newindex++;
			continue;
		}

		if ( newnum > oldnum ) {
			// the old entity isn't present in the new message
			MSG_WriteDeltaEntity (msg, oldent, nullptr, qtrue );
			oldindex++;
			continue;
		}
	}

	MSG_WriteBits( msg, (MAX_GENTITIES-1), GENTITYNUM_BITS );	// end of packetentities
}


/*
====================
CL_WriteSnapshot
====================
*/
static void CL_WriteSnapshot( void ) {

	static	clSnapshot_t saved_snap;
	static entityState_t saved_ents[ MAX_SNAPSHOT_ENTITIES ];

	clSnapshot_t *snap, *oldSnap;
	std::array<byte, MAX_MSGLEN_BUF> bufData;
	msg_t	msg;
	int		i, len;

	snap = &cl.snapshots[ cl.snap.messageNum & PACKET_MASK ]; // current snapshot
	//if ( !snap->valid ) // should never happen?
	//	return;

	if ( clc.demoDeltaNum == 0 ) {
		oldSnap = nullptr;
	} else {
		oldSnap = &saved_snap;
	}

	MSG_Init( &msg, bufData.data(), MAX_MSGLEN );
	MSG_Bitstream( &msg );

	// NOTE, MRE: all server->client messages now acknowledge
	MSG_WriteLong( &msg, clc.reliableSequence );

	// Write all pending server commands
	CL_WriteServerCommands( &msg );

	MSG_WriteByte( &msg, svc_snapshot );
	MSG_WriteLong( &msg, snap->serverTime ); // sv.time
	MSG_WriteByte( &msg, clc.demoDeltaNum ); // 0 or 1
	MSG_WriteByte( &msg, snap->snapFlags );  // snapFlags
	MSG_WriteByte( &msg, snap->areabytes );  // areabytes
	MSG_WriteData( &msg, snap->areamask, snap->areabytes );
	if ( oldSnap )
		MSG_WriteDeltaPlayerstate( &msg, &oldSnap->ps, &snap->ps );
	else
		MSG_WriteDeltaPlayerstate( &msg, nullptr, &snap->ps );

	CL_EmitPacketEntities( oldSnap, snap, &msg, saved_ents );

	// finished writing the client packet
	MSG_WriteByte( &msg, svc_EOF );

	// write it to the demo file
	if ( clc.demoplaying )
		len = LittleLong( clc.demoMessageSequence );
	else
		len = LittleLong( clc.serverMessageSequence );
	FileWrite( clc.recordfile, &len, 4 );

	len = LittleLong( msg.cursize );
	FileWrite( clc.recordfile, &len, 4 );
	FileWrite( clc.recordfile, msg.data, msg.cursize );

	// save last sent state so if there any need - we can skip any further incoming messages
	for ( i = 0; i < snap->numEntities; i++ )
		saved_ents[ i ] = cl.parseEntities[ (snap->parseEntitiesNum + i) % MAX_PARSE_ENTITIES ];

	saved_snap = *snap;
	saved_snap.parseEntitiesNum = 0;

	clc.demoMessageSequence++;
	clc.demoDeltaNum = 1;
}


/*
====================
CL_Record_f

record <demoname>

Begins recording a demo from the current position
====================
*/
static void CL_Record_f( void ) {
	std::array<char, MAX_OSPATH> demoName;
	std::array<char, MAX_OSPATH> name;
	const char	*ext;
	int protocol;
	qtime_t		t;

	if ( Cmd_Argc() > 2 ) {
		Com_Printf( "record <demoname>\n" );
		return;
	}

	if ( clc.demorecording ) {
		if ( !clc.spDemoRecording ) {
			Com_Printf( "Already recording.\n" );
		}
		return;
	}

	if ( cls.state != CA_ACTIVE ) {
		Com_Printf( "You must be in a level to record.\n" );
		return;
	}

	// sync 0 doesn't prevent recording, so not forcing it off .. everyone does g_sync 1 ; record ; g_sync 0 ..
	if ( NET_IsLocalAddress( &clc.serverAddress ) && !Cvar_VariableIntegerValue( "g_synchronousClients" ) ) {
		Com_Printf (S_COLOR_YELLOW "WARNING: You should set 'g_synchronousClients 1' for smoother demo recording\n");
	}

	if ( Cmd_Argc() == 2 ) {
		// explicit demo name specified
		Q_strncpyz( demoName.data(), Cmd_Argv( 1 ), static_cast<int>( demoName.size() ) );
		ext = COM_GetExtension( demoName.data() );
		if ( *ext ) {
			// Strip every demo extension this build can actually play, including
			// retail Quake Live dm_91, before the final protocol is appended.
			if ( CL_IsLegacyDemoExt( ext ) ||
				( CL_ParseDemoProtocolExtension( ext, &protocol ) &&
					CL_DemoProtocolSupported( protocol ) ) ) {
				*(strrchr( demoName.data(), '.' )) = '\0';
			}
		}
		Com_sprintf( name.data(), static_cast<int>( name.size() ), "demos/%s", demoName.data() );

		clc.explicitRecordName = qtrue;
	} else {

		Com_RealTime( &t );
		Com_sprintf( name.data(), static_cast<int>( name.size() ), "demos/demo-%04d%02d%02d-%02d%02d%02d",
			1900 + t.tm_year, 1 + t.tm_mon,	t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec );

		clc.explicitRecordName = qfalse;
	}

	// save desired filename without extension
	Q_strncpyz( clc.recordName, name.data(), sizeof( clc.recordName ) );

	Com_Printf( "recording to %s.\n", name.data() );

	// start new record with temporary extension
	Q_strcat( name.data(), static_cast<int>( name.size() ), ".tmp" );

	// open the demo file
	clc.recordfile = FS_FOpenFileWrite( name.data() );
	if ( clc.recordfile == FS_INVALID_HANDLE ) {
		Com_Printf( "ERROR: couldn't open.\n" );
		clc.recordName[0] = '\0';
		return;
	}

	Com_TruncateLongString( clc.recordNameShort, clc.recordName );
	clc.demorecording = qtrue;
	CL_WebView_PublishGameDemo( clc.recordName, clc.recordNameShort );

	if ( Cvar_VariableIntegerValue( "ui_recordSPDemo" ) ) {
	  clc.spDemoRecording = qtrue;
	} else {
	  clc.spDemoRecording = qfalse;
	}

	// don't start saving messages until a non-delta compressed message is received
	clc.demowaiting = qtrue;

	// we will rename record to dm_68 or dm_71 depending from this flag
	clc.dm68compat = qtrue;

	// write out the gamestate message
	CL_WriteGamestate( qtrue );

	// the rest of the demo file will be copied from net messages
}


/*
====================
CL_CompleteRecordName
====================
*/
static void CL_CompleteRecordName(const char *args, int argNum )
{
	if ( argNum == 2 )
	{
		std::array<char, 16> demoExt;

		Com_sprintf( demoExt.data(), static_cast<int>( demoExt.size() ), "." DEMOEXT "%d", com_protocol->integer );
		Field_CompleteFilename( "demos", demoExt.data(), qtrue, FS_MATCH_EXTERN | FS_MATCH_STICK );
	}
}


/*
=======================================================================

CLIENT SIDE DEMO PLAYBACK

=======================================================================
*/

/*
=================
CL_DemoCompleted
=================
*/
static void CL_DemoCompleted( void ) {
	if ( com_timedemo->integer ) {
		int	time;

		time = Sys_Milliseconds() - clc.timeDemoStart;
		if ( time > 0 ) {
			Com_Printf( "%i frames, %3.*f seconds: %3.1f fps\n", clc.timeDemoFrames,
			time > 10000 ? 1 : 2, time/1000.0, clc.timeDemoFrames*1000.0 / time );
		}
	}

	CL_Disconnect( qtrue );
	CL_NextDemo();
}


/*
=================
CL_ReadDemoMessage
=================
*/
void CL_ReadDemoMessage( void ) {
	msg_t		buf;
	std::array<byte, MAX_MSGLEN_BUF> bufData;
	fnql::demo::EnvelopeResult envelope;

	if ( clc.demofile == FS_INVALID_HANDLE ) {
		CL_DemoCompleted();
		return;
	}

	MSG_Init( &buf, bufData.data(), MAX_MSGLEN );
	envelope = fnql::demo::ReadEnvelope(
		[]( std::uint8_t *destination, std::size_t bytes ) {
			return FileRead( clc.demofile, destination, bytes );
		}, buf.data, static_cast<std::size_t>( buf.maxsize ) );

	switch ( envelope.status ) {
	case fnql::demo::EnvelopeStatus::Message:
		break;
	case fnql::demo::EnvelopeStatus::EndOfStreamTrailer:
	case fnql::demo::EnvelopeStatus::TruncatedHeader:
		CL_DemoCompleted();
		return;
	case fnql::demo::EnvelopeStatus::TruncatedPayload:
		Com_Printf( "Demo file was truncated.\n" );
		CL_DemoCompleted();
		return;
	case fnql::demo::EnvelopeStatus::NegativeLength:
	case fnql::demo::EnvelopeStatus::OversizeLength:
		Com_Error( ERR_DROP, "CL_ReadDemoMessage: invalid demo message length %d",
			envelope.payloadLength );
		return;
	}

	clc.serverMessageSequence = envelope.sequence;
	buf.cursize = envelope.payloadLength;

	clc.lastPacketTime = cls.realtime;
	buf.readcount = 0;

	clc.demoCommandSequence = clc.serverCommandSequence;

	CL_ParseServerMessage( &buf );

	if ( clc.demorecording ) {
		// track changes and write new message
		if ( clc.eventMask & EM_GAMESTATE ) {
			CL_WriteGamestate( qfalse );
			// nothing should came after gamestate in current message
		} else if ( clc.eventMask & (EM_SNAPSHOT|EM_COMMAND) ) {
			CL_WriteSnapshot();
		}
	}
}


/*
====================
CL_TryDemoProtocol
====================
*/
static bool CL_TryDemoProtocol( const char *arg, int protocol, char *name,
	int nameLength, ScopedFileHandle &handle )
{
	Com_sprintf( name, nameLength, "demos/%s.%s%d", arg, DEMOEXT, protocol );
	OpenPureFileRead( name, handle );
	if ( handle ) {
		Com_Printf( "Demo file: %s\n", name );
		return true;
	}

	Com_Printf( "Not found: %s\n", name );
	return false;
}


/*
====================
CL_WalkDemoExt
====================
*/
static int CL_WalkDemoExt( const char *arg, char *name, int name_len, ScopedFileHandle &handle )
{
	int preferredProtocol;
	int i;

	handle.reset();
	preferredProtocol = ( com_protocol &&
		com_protocol->integer == QL_RETAIL_PROTOCOL_VERSION ) ?
		QL_RETAIL_PROTOCOL_VERSION : 0;

	// Preserve the historical multi-protocol order outside the explicit QL
	// profile, while ensuring a retail session never silently chooses a same-
	// named legacy demo before its dm_91 capture.
	if ( preferredProtocol && CL_TryDemoProtocol( arg, preferredProtocol,
		name, name_len, handle ) ) {
		return preferredProtocol;
	}

	for ( i = 0; demo_protocols[ i ]; i++ )
	{
		if ( demo_protocols[ i ] == preferredProtocol ) {
			continue;
		}
		if ( CL_TryDemoProtocol( arg, demo_protocols[ i ], name, name_len,
			handle ) ) {
			return demo_protocols[ i ];
		}
	}

	Com_sprintf( name, name_len, "demos/%s.%s", arg, LEGACY_DEMOEXT );
	OpenPureFileRead( name, handle );
	if ( handle )
	{
		Com_Printf( "Demo file: %s\n", name );
		return OLD_PROTOCOL_VERSION;
	}
	else
		Com_Printf( "Not found: %s\n", name );

	return -1;
}


/*
====================
CL_DemoExtCallback
====================
*/
static qboolean CL_DemoNameCallback_f( const char *filename, int length )
{
	const int legacy_ext_len = static_cast<int>( strlen( LEGACY_DEMOEXT ) ) + 1;
	const char *extension;
	int version;

	if ( length >= legacy_ext_len && filename[ length - legacy_ext_len ] == '.' &&
		!Q_stricmp( filename + length - static_cast<int>( strlen( LEGACY_DEMOEXT ) ), LEGACY_DEMOEXT ) )
		return qtrue;

	extension = COM_GetExtension( filename );
	if ( !CL_ParseDemoProtocolExtension( extension, &version ) )
		return qfalse;

	return ToQboolean( CL_DemoProtocolSupported( version ) );
}


/*
====================
CL_CompleteDemoName
====================
*/
static void CL_CompleteDemoName(const char *args, int argNum )
{
	if ( argNum == 2 )
	{
		FS_SetFilenameCallback( CL_DemoNameCallback_f );
		Field_CompleteFilename( "demos", "", qfalse, FS_MATCH_ANY | FS_MATCH_STICK | FS_MATCH_SUBDIRS );
		FS_SetFilenameCallback( nullptr );
	}
}


/*
====================
CL_CompleteDownloadMap
====================
*/
#ifdef USE_CURL
static void CL_CompleteDownloadMap( const char *args, int argNum )
{
	if ( argNum == 2 ) {
		Field_CompleteFilename( "maps", "bsp", qtrue, FS_MATCH_ANY | FS_MATCH_STICK );
	}
}
#endif


/*
====================
CL_PlayDemo_f

demo <demoname>

====================
*/
static void CL_PlayDemo_f( void ) {
	std::array<char, MAX_OSPATH> name;
	const char		*arg;
	const char		*ext_test;
	int			protocol;
	std::array<char, MAX_OSPATH> retry;
	const char	*shortname, *slash;
	ScopedFileHandle hFile;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "demo <demoname>\n" );
		return;
	}

	// open the demo file
	arg = Cmd_Argv( 1 );

	// check for an explicit demo extension first
	ext_test = strrchr(arg, '.');
	if ( ext_test && CL_IsLegacyDemoExt( ext_test + 1 ) )
	{
		protocol = OLD_PROTOCOL_VERSION;
		Com_sprintf( name.data(), static_cast<int>( name.size() ), "demos/%s", arg );
		OpenPureFileRead( name.data(), hFile );
	}
	else if ( ext_test && !Q_stricmpn(ext_test + 1, DEMOEXT, ARRAY_LEN(DEMOEXT) - 1) )
	{
		if ( !CL_ParseDemoProtocolExtension( ext_test + 1, &protocol ) ) {
			protocol = -1;
		}

		if ( CL_DemoProtocolSupported( protocol ) )
		{
			Com_sprintf( name.data(), static_cast<int>( name.size() ), "demos/%s", arg );
			OpenPureFileRead( name.data(), hFile );
		}
		else
		{
			size_t len;

			if ( protocol > 0 ) {
				Com_Printf( "Protocol %d not supported for demos\n", protocol );
			} else {
				Com_Printf( "Invalid demo protocol extension: %s\n", ext_test + 1 );
			}
			len = ext_test - arg;

			if ( len > retry.size() - 1 ) {
				len = retry.size() - 1;
			}

			Q_strncpyz( retry.data(), arg, static_cast<int>( len + 1 ) );
			retry[len] = '\0';
			protocol = CL_WalkDemoExt( retry.data(), name.data(), static_cast<int>( name.size() ), hFile );
		}
	}
	else
		protocol = CL_WalkDemoExt( arg, name.data(), static_cast<int>( name.size() ), hFile );

	if ( !hFile ) {
		Com_Printf( S_COLOR_YELLOW "couldn't open %s\n", name.data() );
		return;
	}

	hFile.reset();

	// make sure a local server is killed
	// 2 means don't force disconnect of local client
	Cvar_Set( "sv_killserver", "2" );

	CL_Disconnect( qtrue );

	// clc.demofile will be closed during CL_Disconnect so reopen it
	if ( FS_FOpenFileRead( name.data(), &clc.demofile, qtrue ) == -1 )
	{
		// drop this time
		Com_Error( ERR_DROP, "couldn't open %s\n", name.data() );
		return;
	}

	if ( (slash = strrchr( name.data(), '/' )) != nullptr )
		shortname = slash + 1;
	else
		shortname = name.data();

	clc.demoLegacyFormat = CL_IsLegacyDemoExt( COM_GetExtension( name.data() ) );
	clc.demoLegacyProtocol = clc.demoLegacyFormat ? CL_DetectLegacyDemoProtocol( clc.demofile ) : 0;
	Q_strncpyz( clc.demoName, shortname, sizeof( clc.demoName ) );

	Con_Close();

	cls.state = CA_CONNECTED;
	clc.demoplaying = qtrue;
	Q_strncpyz( cls.servername, shortname, sizeof( cls.servername ) );

	if ( protocol <= OLD_PROTOCOL_VERSION )
		clc.compat = qtrue;
	else
		clc.compat = qfalse;

	// read demo messages until connected
#ifdef USE_CURL
	while ( cls.state >= CA_CONNECTED && cls.state < CA_PRIMED && !Com_DL_InProgress( &download ) ) {
#else
	while ( cls.state >= CA_CONNECTED && cls.state < CA_PRIMED ) {
#endif
		CL_ReadDemoMessage();
	}

	// don't get the first snapshot this frame, to prevent the long
	// time from the gamestate load from messing causing a time skip
	clc.firstDemoFrameSkipped = qfalse;
}


/*
==================
CL_NextDemo

Called when a demo or cinematic finishes
If the "nextdemo" cvar is set, that command will be issued
==================
*/
static void CL_NextDemo( void ) {
	std::array<char, MAX_CVAR_VALUE_STRING> v;

	Cvar_VariableStringBuffer( "nextdemo", v.data(), static_cast<int>( v.size() ) );
	Com_DPrintf( "CL_NextDemo: %s\n", v.data() );
	if ( !v[0] ) {
		return;
	}

	Cvar_Set( "nextdemo", "" );
	Cbuf_AddText( v.data() );
	Cbuf_AddText( "\n" );
	Cbuf_Execute();
}


//======================================================================

/*
=====================
CL_ShutdownVMs
=====================
*/
static void CL_ShutdownVMs( void )
{
	CL_ShutdownCGame();
	CL_ShutdownUI();
}


/*
=====================
Called by Com_GameRestart, CL_FlushMemory and SV_SpawnServer

CL_ShutdownAll
=====================
*/
void CL_ShutdownAll( void ) {

#ifdef USE_CURL
	CL_cURL_Shutdown();
#endif

	// clear and mute all sounds until next registration
	S_DisableSounds();

	// shutdown VMs
	CL_ShutdownVMs();

	// shutdown the renderer
	if ( re.Shutdown ) {
		if ( CL_GameSwitch() ) {
			CL_ShutdownRef( REF_DESTROY_WINDOW ); // shutdown renderer & GLimp
		} else {
			re.Shutdown( REF_KEEP_CONTEXT ); // don't destroy window or context
		}
	}

	cls.rendererStarted = qfalse;
	cls.soundRegistered = qfalse;

	SCR_Done();
}


/*
=================
CL_ClearMemory
=================
*/
void CL_ClearMemory( void ) {
	// if not running a server clear the whole hunk
	if ( !com_sv_running->integer ) {
		// clear the whole hunk
		Hunk_Clear();
		// clear collision map data
		CM_ClearMap();
	} else {
		// clear all the client data on the hunk
		Hunk_ClearToMark();
	}
}


/*
=================
CL_FlushMemory

Called by CL_Disconnect_f, CL_DownloadsComplete
Also called by Com_Error
=================
*/
void CL_FlushMemory( void ) {

	// shutdown all the client stuff
	CL_ShutdownAll();

	CL_ClearMemory();

	CL_StartHunkUsers();
}


/*
=====================
CL_MapLoading

A local server is starting to load a map, so update the
screen to let the user know about it, then dump all client
memory on the hunk from cgame, ui, and renderer
=====================
*/
void CL_MapLoading( void ) {
	if ( com_dedicated->integer ) {
		cls.state = CA_DISCONNECTED;
		Key_SetCatcher( KEYCATCH_CONSOLE );
		return;
	}

	if ( !com_cl_running->integer ) {
		return;
	}

	CL_WebHost_HideForGameTransition();
	Con_Close();
	Key_SetCatcher( 0 );

	// if we are already connected to the local host, stay connected
	if ( cls.state >= CA_CONNECTED && !Q_stricmp( cls.servername, "localhost" ) ) {
		cls.state = CA_CONNECTED;		// so the connect screen is drawn
		std::fill_n( cls.updateInfoString, sizeof( cls.updateInfoString ), '\0' );
		std::fill_n( clc.serverMessage, sizeof( clc.serverMessage ), '\0' );
		cl.gameState = {};
		clc.lastPacketSentTime = cls.realtime - 9999;  // send packet immediately
		cls.framecount++;
		SCR_UpdateScreen();
	} else {
		// clear nextmap so the cinematic shutdown doesn't execute it
		Cvar_Set( "nextmap", "" );
		CL_Disconnect( qtrue );
		Q_strncpyz( cls.servername, "localhost", sizeof(cls.servername) );
		cls.state = CA_CHALLENGING;		// so the connect screen is drawn
		Key_SetCatcher( 0 );
		cls.framecount++;
		SCR_UpdateScreen();
		clc.connectTime = -RETRANSMIT_TIMEOUT;
		NET_StringToAdr( cls.servername, &clc.serverAddress, NA_UNSPEC );
		CL_WebView_PublishGameStartForAddress( &clc.serverAddress );
		// we don't need a challenge on the localhost
		CL_CheckForResend();
	}
}


/*
=====================
CL_ClearState

Called before parsing a gamestate
=====================
*/
void CL_ClearState( void ) {

//	S_StopAllSounds();

	// clientActive_t is larger than the default Windows thread stack once QL's
	// snapshot/entity limits are represented. Aggregate assignment materializes
	// a full temporary on MSVC, so clear the POD state directly in place.
	Com_Memset( &cl, 0, sizeof( cl ) );
}

/*
====================
CL_ParseUnsignedLongLongString
====================
*/
static qboolean CL_ParseUnsignedLongLongString( const char *text, unsigned long long *outValue ) {
	const char *ch;
	unsigned long long value;

	if ( outValue ) {
		*outValue = 0ull;
	}

	if ( !text || !text[0] || !outValue ) {
		return qfalse;
	}

	value = 0ull;
	for ( ch = text; *ch; ++ch ) {
		unsigned int digit;

		if ( *ch < '0' || *ch > '9' ) {
			return qfalse;
		}

		digit = static_cast<unsigned int>( *ch - '0' );
		if ( value > ( ULLONG_MAX - digit ) / 10ull ) {
			return qfalse;
		}

		value = value * 10ull + digit;
	}

	*outValue = value;
	return qtrue;
}

/*
====================
CL_ParseSteamIdString

Parses the decimal retail SteamID transport into low/high identity words.
====================
*/
static qboolean CL_ParseSteamIdString( const char *steamId, unsigned int *steamIdLow, unsigned int *steamIdHigh ) {
	unsigned long long value;

	if ( steamIdLow ) {
		*steamIdLow = 0;
	}
	if ( steamIdHigh ) {
		*steamIdHigh = 0;
	}

	if ( !steamId || !steamId[0] || !steamIdLow || !steamIdHigh ) {
		return qfalse;
	}

	if ( !CL_ParseUnsignedLongLongString( steamId, &value ) ) {
		return qfalse;
	}

	*steamIdLow = static_cast<unsigned int>( value & 0xffffffffull );
	*steamIdHigh = static_cast<unsigned int>( ( value >> 32 ) & 0xffffffffull );
	return qtrue;
}

/*
====================
CL_GetConfigStringValue
====================
*/
static const char *CL_GetConfigStringValue( int index ) {
	int offset;

	if ( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		return "";
	}

	offset = cl.gameState.stringOffsets[index];
	if ( offset <= 0 || offset >= cl.gameState.dataCount ||
		offset >= static_cast<int>( sizeof( cl.gameState.stringData ) ) ) {
		return "";
	}

	return cl.gameState.stringData + offset;
}

/*
====================
CL_CopyClientIdentity

Returns the native cgame identity sidecar when available, with a configstring
fallback for the safe data already replicated through CS_PLAYERS.
====================
*/
qboolean CL_CopyClientIdentity( int clientNum, cgameClientIdentity_t *identity ) {
	char info[MAX_INFO_STRING];
	char generatedCleanName[CG_CLIENT_IDENTITY_NAME_CHARS];
	const char *configstring;
	const char *steamId;
	const char *name;
	const char *cleanName;
	qboolean hasIdentity;

	if ( !identity ) {
		return qfalse;
	}

	Com_Memset( identity, 0, sizeof( *identity ) );

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return qfalse;
	}

	if ( cgvm && cgvm->dllExports &&
		VM_Call( cgvm, 2, CG_COPY_CLIENT_IDENTITY,
			static_cast<intptr_t>( clientNum ), reinterpret_cast<intptr_t>( identity ) ) ) {
		return ( identity->identityLow || identity->identityHigh ||
			identity->displayName[0] || identity->cleanName[0] ) ? qtrue : qfalse;
	}

	configstring = CL_GetConfigStringValue( CS_PLAYERS + clientNum );
	if ( !configstring[0] ) {
		return qfalse;
	}

	Q_strncpyz( info, configstring, sizeof( info ) );

	identity->clientNum = clientNum;
	hasIdentity = qfalse;

	steamId = Info_ValueForKey( info, "steamid" );
	if ( !steamId[0] ) {
		steamId = Info_ValueForKey( info, "steam" );
	}
	if ( CL_ParseSteamIdString( steamId, &identity->identityLow, &identity->identityHigh ) &&
		( identity->identityLow || identity->identityHigh ) ) {
		hasIdentity = qtrue;
	}

	name = Info_ValueForKey( info, "n" );
	if ( !name[0] ) {
		name = Info_ValueForKey( info, "name" );
	}
	if ( name[0] ) {
		Q_strncpyz( identity->displayName, name, sizeof( identity->displayName ) );
		hasIdentity = qtrue;
	}

	cleanName = Info_ValueForKey( info, "cn" );
	if ( !cleanName[0] ) {
		cleanName = Info_ValueForKey( info, "cleanName" );
	}
	if ( cleanName[0] ) {
		Q_strncpyz( identity->cleanName, cleanName, sizeof( identity->cleanName ) );
		hasIdentity = qtrue;
	} else if ( identity->displayName[0] ) {
		Q_strncpyz( generatedCleanName, identity->displayName, sizeof( generatedCleanName ) );
		Q_CleanStr( generatedCleanName );
		Q_strncpyz( identity->cleanName,
			generatedCleanName[0] ? generatedCleanName : identity->displayName, sizeof( identity->cleanName ) );
	}

	return hasIdentity;
}

/*
====================
CL_GetClientSteamId
====================
*/
qboolean CL_GetClientSteamId( int clientNum, unsigned int *steamIdLow, unsigned int *steamIdHigh ) {
	cgameClientIdentity_t identity;

	if ( steamIdLow ) {
		*steamIdLow = 0;
	}
	if ( steamIdHigh ) {
		*steamIdHigh = 0;
	}

	if ( !steamIdLow || !steamIdHigh ) {
		return qfalse;
	}

	if ( !CL_CopyClientIdentity( clientNum, &identity ) ) {
		return qfalse;
	}

	if ( !identity.identityLow && !identity.identityHigh ) {
		return qfalse;
	}

	*steamIdLow = identity.identityLow;
	*steamIdHigh = identity.identityHigh;
	return qtrue;
}

/*
====================
CL_GetWorkshopDownloadInfo

Uses live provider progress when available, with the active QL-style download
cvars retained as the provider-free fallback.
====================
*/
qboolean CL_GetWorkshopDownloadInfo( unsigned int itemIdLow, unsigned int itemIdHigh, unsigned long long *outDownloaded, unsigned long long *outTotal ) {
	char itemText[32];
	char downloadedText[32];
	char totalText[32];
	unsigned long long itemId;
	unsigned long long requestedItemId;
	unsigned long long downloaded;
	unsigned long long total;

	if ( outDownloaded ) {
		*outDownloaded = 0ull;
	}
	if ( outTotal ) {
		*outTotal = 0ull;
	}
	requestedItemId = ( static_cast<unsigned long long>( itemIdHigh ) << 32 ) | itemIdLow;
	if ( FNQL_Steam_Available( FNQL_STEAM_CAP_UGC ) ) {
		itemId = requestedItemId;
		if ( itemId == 0 ) {
			Cvar_VariableStringBuffer( "cl_downloadItem", itemText, sizeof( itemText ) );
			(void)CL_ParseUnsignedLongLongString( itemText, &itemId );
		}
		if ( itemId != 0 ) {
			std::uint64_t liveDownloaded = 0;
			std::uint64_t liveTotal = 0;
			if ( FNQL_Steam_GetItemDownloadInfo( itemId,
				&liveDownloaded, &liveTotal ) == FNQL_STEAM_RESULT_OK ) {
				if ( outDownloaded ) {
					*outDownloaded = static_cast<unsigned long long>( liveDownloaded );
				}
				if ( outTotal ) {
					*outTotal = static_cast<unsigned long long>( liveTotal );
				}
				return qtrue;
			}
		}
	}

	Cvar_VariableStringBuffer( "cl_downloadItem", itemText, sizeof( itemText ) );
	if ( !itemText[0] || !CL_ParseUnsignedLongLongString( itemText, &itemId ) ) {
		return qfalse;
	}

	if ( requestedItemId && requestedItemId != itemId ) {
		return qfalse;
	}

	Cvar_VariableStringBuffer( "cl_downloadCount", downloadedText, sizeof( downloadedText ) );
	Cvar_VariableStringBuffer( "cl_downloadSize", totalText, sizeof( totalText ) );
	if ( !CL_ParseUnsignedLongLongString( downloadedText, &downloaded ) ) {
		downloaded = 0ull;
	}
	if ( !CL_ParseUnsignedLongLongString( totalText, &total ) ) {
		total = 0ull;
	}

	if ( outDownloaded ) {
		*outDownloaded = downloaded;
	}
	if ( outTotal ) {
		*outTotal = total;
	}

	return qtrue;
}

/*
====================
CL_IsVoiceSenderMuted
====================
*/
qboolean CL_IsVoiceSenderMuted( int clientNum ) {
	unsigned int steamIdLow;
	unsigned int steamIdHigh;

	if ( !CL_GetClientSteamId( clientNum, &steamIdLow, &steamIdHigh ) ) {
		return qfalse;
	}

	return CL_IsSteamIdentityMuted( steamIdLow, steamIdHigh );
}

/*
====================
CL_SetClientSpeakingState
====================
*/
void CL_SetClientSpeakingState( int clientNum, qboolean speaking ) {
	if ( !cgvm || !cgvm->dllExports || cls.state != CA_ACTIVE || !cl.snap.valid ) {
		return;
	}

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}

	VM_Call( cgvm, 2, CG_SET_CLIENT_SPEAKING_STATE,
		static_cast<intptr_t>( clientNum ), static_cast<intptr_t>( speaking ? 1 : 0 ) );
}

/*
====================
CL_SetLocalSpeakingState
====================
*/
void CL_SetLocalSpeakingState( qboolean speaking ) {
	CL_SetClientSpeakingState( cl.snap.ps.clientNum, speaking );
}


/*
====================
CL_UpdateGUID

update cl_guid using QKEY_FILE and optional prefix
====================
*/
static void CL_UpdateGUID( const char *prefix, int prefix_len )
{
#ifdef USE_Q3KEY
	int len;

	ScopedFileHandle keyFile;
	len = OpenSvFileRead( QKEY_FILE, keyFile );
	keyFile.reset();

	if( len != QKEY_SIZE )
		Cvar_Set( "cl_guid", "" );
	else
		Cvar_Set( "cl_guid", Com_MD5File( QKEY_FILE, QKEY_SIZE,
			prefix, prefix_len ) );
#else
	Cvar_Set( "cl_guid", Com_MD5Buf( &cl_cdkey[0], sizeof(cl_cdkey), prefix, prefix_len));
#endif
}


/*
=====================
CL_ResetOldGame
=====================
*/
void CL_ResetOldGame( void )
{
	cl_oldGameSet = qfalse;
	cl_oldGame[0] = '\0';
}


/*
=====================
CL_RestoreOldGame

change back to previous fs_game
=====================
*/
static bool CL_RestoreOldGame( void )
{
	if ( cl_oldGameSet )
	{
		cl_oldGameSet = qfalse;
		Cvar_Set( "fs_game", cl_oldGame );
		FS_ConditionalRestart( clc.checksumFeed, qtrue );
		return true;
	}
	return false;
}


/*
=====================
CL_Disconnect

Called when a connection, demo, or cinematic is being terminated.
Goes from a connected state to either a menu state or a console state
Sends a disconnect message to the server
This is also called on Com_Error and Com_Quit, so it shouldn't cause any errors
=====================
*/
qboolean CL_Disconnect( qboolean showMainMenu ) {
	static bool cl_disconnecting = false;
	bool cl_restarted = false;

	if ( !com_cl_running || !com_cl_running->integer ) {
		return ToQboolean( cl_restarted );
	}

	if ( cl_disconnecting ) {
		return ToQboolean( cl_restarted );
	}

	cl_disconnecting = true;
	const qboolean publishGameEnd =
		( cls.state >= CA_CONNECTED || clc.demoplaying || clc.demorecording )
			? qtrue : qfalse;

	// Retail enters fullscreen UI mode before publishing game.end. Resetting
	// timescale here also prevents a slow-motion or demo timescale from leaking
	// into the browser menu after the session has ended.
	Cvar_Set( "timescale", "1" );
	Cvar_Set( "r_uiFullScreen", "1" );
	CL_ClearSteamChallengeTicket();
	CL_Workshop_Reset();

	if ( publishGameEnd ) {
		CL_WebView_PublishGameEnd();
	}

	// Stop demo recording
	if ( clc.demorecording ) {
		CL_StopRecord_f();
	}

	// Stop demo playback
	if ( clc.demofile != FS_INVALID_HANDLE ) {
		CloseFile( clc.demofile );
	}
	clc.demoLegacyFormat = qfalse;
	clc.demoLegacyProtocol = 0;

	// Finish downloads
	if ( clc.download != FS_INVALID_HANDLE ) {
		CloseFile( clc.download );
	}
	*clc.downloadTempName = *clc.downloadName = '\0';
	Cvar_Set( "cl_downloadName", "" );

	// Stop recording any video
	if ( CL_VideoRecording() ) {
		// Finish rendering current frame
		cls.framecount++;
		SCR_UpdateScreen();
		CL_CloseAVI( qfalse );
	}

	if ( cgvm ) {
		// do that right after we rendered last video frame
		CL_ShutdownCGame();
	}

	SCR_StopCinematic();
	S_StopAllSounds();
	Key_ClearStates();

	if ( uivm && showMainMenu ) {
		VM_Call( uivm, 1, UI_SET_ACTIVE_MENU, UIMENU_NONE );
	}

	// Remove pure paks
	FS_PureServerSetLoadedPaks( "", "" );
	FS_PureServerSetReferencedPaks( "", "" );

	FS_ClearPakReferences( FS_GENERAL_REF | FS_UI_REF | FS_CGAME_REF );

	if ( CL_GameSwitch() ) {
		// keep current gamestate and connection
		cl_disconnecting = false;
		return qfalse;
	}

	// send a disconnect message to the server
	// send it a few times in case one is dropped
	if ( cls.state >= CA_CONNECTED && cls.state != CA_CINEMATIC && !clc.demoplaying ) {
		CL_AddReliableCommand( "disconnect", qtrue );
		CL_WritePacket( 2 );
	}

	CL_ClearState();

	// Wipe in place; avoid manufacturing a sizeable aggregate temporary on the
	// comparatively small Win32 client thread stack.
	Com_Memset( &clc, 0, sizeof( clc ) );

	cls.state = CA_DISCONNECTED;

	// allow cheats locally
	Cvar_Set( "sv_cheats", "1" );

	// not connected to a pure server anymore
	cl_connectedToPureServer = 0;

	CL_UpdateGUID( nullptr, 0 );

	// Cmd_RemoveCommand( "callvote" );
	Cmd_RemoveCgameCommands();

	if ( noGameRestart )
		noGameRestart = false;
	else
		cl_restarted = CL_RestoreOldGame();

	if ( showMainMenu && !CL_WebHost_ShowAfterDisconnect() && uivm ) {
		// The retail WebUI is the primary menu. Keep the retail native UI module
		// usable when its external Awesomium runtime or web.pak is unavailable.
		VM_Call( uivm, 1, UI_SET_ACTIVE_MENU, UIMENU_MAIN );
		Key_SetCatcher( ( Key_GetCatcher() & ~KEYCATCH_BROWSER ) | KEYCATCH_UI );
	}

	cl_disconnecting = false;

	return ToQboolean( cl_restarted );
}


/*
===================
CL_ForwardCommandToServer

adds the current command line as a clientCommand
things like godmode, noclip, etc, are commands directed to the server,
so when they are typed in at the console, they will need to be forwarded.
===================
*/
void CL_ForwardCommandToServer( const char *string ) {
	const char *cmd;

	cmd = Cmd_Argv( 0 );

	// ignore key up commands
	if ( cmd[0] == '-' ) {
		return;
	}

	// no userinfo updates from command line
	if ( !strcmp( cmd, "userinfo" ) ) {
		return;
	}

	if ( clc.demoplaying || cls.state < CA_CONNECTED || cmd[0] == '+' ) {
		Com_Printf( "Unknown command \"%s" S_COLOR_WHITE "\"\n", cmd );
		return;
	}

	if ( Cmd_Argc() > 1 ) {
		CL_AddReliableCommand( string, qfalse );
	} else {
		CL_AddReliableCommand( cmd, qfalse );
	}
}


/*
===================
CL_RequestMotd

===================
*/
#if 0
static void CL_RequestMotd( void ) {
	char		info[MAX_INFO_STRING];

	if ( !cl_motd->integer ) {
		return;
	}
	Com_Printf( "Resolving %s\n", UPDATE_SERVER_NAME );
	if ( !NET_StringToAdr( UPDATE_SERVER_NAME, &cls.updateServer, NA_IP ) ) {
		Com_Printf( "Couldn't resolve address\n" );
		return;
	}
	cls.updateServer.port = BigShort( PORT_UPDATE );
	Com_Printf( "%s resolved to %i.%i.%i.%i:%i\n", UPDATE_SERVER_NAME,
		cls.updateServer.ip[0], cls.updateServer.ip[1],
		cls.updateServer.ip[2], cls.updateServer.ip[3],
		BigShort( cls.updateServer.port ) );

	info[0] = 0;
	// NOTE TTimo xoring against Com_Milliseconds, otherwise we may not have a true randomization
	// only srand I could catch before here is tr_noise.c l:26 srand(1001)
	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=382
	// NOTE: the Com_Milliseconds xoring only affects the lower 16-bit word,
	//   but I decided it was enough randomization
	Com_sprintf( cls.updateChallenge, sizeof( cls.updateChallenge ), "%i", ((rand() << 16) ^ rand()) ^ Com_Milliseconds());

	Info_SetValueForKey( info, "challenge", cls.updateChallenge );
	Info_SetValueForKey( info, "renderer", cls.glconfig.renderer_string );
	Info_SetValueForKey( info, "version", com_version->string );

	NET_OutOfBandPrint( NS_CLIENT, &cls.updateServer, "getmotd \"%s\"\n", info );
}
#endif

/*
===================
CL_RequestAuthorization

Authorization server protocol
-----------------------------

All commands are text in Q3 out of band packets (leading 0xff 0xff 0xff 0xff).

Whenever the client tries to get a challenge from the server it wants to
connect to, it also blindly fires off a packet to the authorize server:

getKeyAuthorize <challenge> <cdkey>

cdkey may be "demo"


#OLD The authorize server returns a:
#OLD
#OLD keyAthorize <challenge> <accept | deny>
#OLD
#OLD A client will be accepted if the cdkey is valid and it has not been used by any other IP
#OLD address in the last 15 minutes.


The server sends a:

getIpAuthorize <challenge> <ip>

The authorize server returns a:

ipAuthorize <challenge> <accept | deny | demo | unknown >

A client will be accepted if a valid cdkey was sent by that ip (only) in the last 15 minutes.
If no response is received from the authorize server after two tries, the client will be let
in anyway.
===================
*/
#ifndef STANDALONE
static void CL_RequestAuthorization( void ) {
	std::array<char, 64> nums;
	int		i, j, l;
	cvar_t	*fs;

	if ( !cls.authorizeServer.port ) {
		Com_Printf( "Resolving %s\n", AUTHORIZE_SERVER_NAME );
		if ( !NET_StringToAdr( AUTHORIZE_SERVER_NAME, &cls.authorizeServer, NA_IP ) ) {
			Com_Printf( "Couldn't resolve address\n" );
			return;
		}

		cls.authorizeServer.port = BigShort( PORT_AUTHORIZE );
		Com_Printf( "%s resolved to %i.%i.%i.%i:%i\n", AUTHORIZE_SERVER_NAME,
			cls.authorizeServer.ipv._4[0], cls.authorizeServer.ipv._4[1],
			cls.authorizeServer.ipv._4[2], cls.authorizeServer.ipv._4[3],
			BigShort( cls.authorizeServer.port ) );
	}
	if ( cls.authorizeServer.type == NA_BAD ) {
		return;
	}

	// only grab the alphanumeric values from the cdkey, to avoid any dashes or spaces
	j = 0;
	l = strlen( cl_cdkey );
	if ( l > 32 ) {
		l = 32;
	}
	for ( i = 0 ; i < l ; i++ ) {
		if ( ( cl_cdkey[i] >= '0' && cl_cdkey[i] <= '9' )
				|| ( cl_cdkey[i] >= 'a' && cl_cdkey[i] <= 'z' )
				|| ( cl_cdkey[i] >= 'A' && cl_cdkey[i] <= 'Z' )
			 ) {
			nums[j] = cl_cdkey[i];
			j++;
		}
	}
	nums[j] = 0;

	fs = Cvar_Get( "cl_anonymous", "0", CVAR_INIT | CVAR_SYSTEMINFO );

	NET_OutOfBandPrint(NS_CLIENT, &cls.authorizeServer, "getKeyAuthorize %i %s", fs->integer, nums.data() );
}
#endif


/*
======================================================================

CONSOLE COMMANDS

======================================================================
*/

/*
==================
CL_ForwardToServer_f
==================
*/
static void CL_ForwardToServer_f( void ) {
	if ( cls.state != CA_ACTIVE || clc.demoplaying ) {
		Com_Printf ("Not connected to a server.\n");
		return;
	}

	if ( Cmd_Argc() <= 1 || strcmp( Cmd_Argv( 1 ), "userinfo" ) == 0 )
		return;

	// don't forward the first argument
	CL_AddReliableCommand( Cmd_ArgsFrom( 1 ), qfalse );
}


/*
==================
CL_Disconnect_f
==================
*/
void CL_Disconnect_f( void ) {
	SCR_StopCinematic();
	Cvar_Set( "ui_singlePlayerActive", "0" );
	if ( cls.state != CA_DISCONNECTED && cls.state != CA_CINEMATIC ) {
		if ( com_sv_running && com_sv_running->integer
			&& ( !com_dedicated || !com_dedicated->integer ) ) {
			SV_Shutdown( "Server quit\n" );
			return;
		}

		// Match retail's single recovery path so console, VM, and WebUI-issued
		// disconnects cannot diverge in cleanup or menu restoration.
		Cvar_Set( "com_errorMessage", "" );
		Com_Error( ERR_DISCONNECT, "Disconnected from server" );
	}
}


/*
================
CL_Reconnect_f
================
*/
static void CL_Reconnect_f( void ) {
	if ( cl_reconnectArgs->string[0] == '\0' || Q_stricmp( cl_reconnectArgs->string, "localhost" ) == 0 )
		return;
	Cvar_Set( "ui_singlePlayerActive", "0" );
	Cbuf_AddText( va( "connect %s\n", cl_reconnectArgs->string ) );
}


/*
================
CL_Connect_f
================
*/
static void CL_Connect_f( void ) {
	netadrtype_t family;
	netadr_t	addr;
	std::array<char, sizeof( cls.servername )> buffer;  // same length as cls.servername
	std::array<char, sizeof( cls.servername ) + MAX_CVAR_VALUE_STRING> args;
	const char	*server;
	const char	*serverString;
	int		len;
	int		argc;

	argc = Cmd_Argc();
	family = NA_UNSPEC;

	if ( argc != 2 && argc != 3 ) {
		Com_Printf( "usage: connect [-4|-6] <server>\n");
		return;
	}

	if ( argc == 2 ) {
		server = Cmd_Argv(1);
	} else {
		if( !strcmp( Cmd_Argv(1), "-4" ) )
			family = NA_IP;
#ifdef USE_IPV6
		else if( !strcmp( Cmd_Argv(1), "-6" ) )
			family = NA_IP6;
		else
			Com_Printf( S_COLOR_YELLOW "warning: only -4 or -6 as address type understood.\n" );
#else
			Com_Printf( S_COLOR_YELLOW "warning: only -4 as address type understood.\n" );
#endif
		server = Cmd_Argv(2);
	}

	Q_strncpyz( buffer.data(), server, static_cast<int>( buffer.size() ) );
	server = buffer.data();

	// skip leading "q3a:/" in connection string
	if ( !Q_stricmpn( server, "q3a:/", 5 ) ) {
		server += 5;
	}

	// skip all slash prefixes
	while ( *server == '/' ) {
		server++;
	}

	len = strlen( server );
	if ( len <= 0 ) {
		return;
	}

	// some programs may add ending slash
	if ( buffer[len-1] == '/' ) {
		buffer[len-1] = '\0';
	}

	if ( !*server ) {
		return;
	}

	// try resolve remote server first
	if ( !NET_StringToAdr( server, &addr, family ) ) {
		Com_Printf( S_COLOR_YELLOW "Bad server address - %s\n", server );
		return;
	}

	// save arguments for reconnect
	Q_strncpyz( args.data(), Cmd_ArgsFrom( 1 ), static_cast<int>( args.size() ) );

	Cvar_Set( "ui_singlePlayerActive", "0" );

	// clear any previous "server full" type messages
	clc.serverMessage[0] = '\0';

	// if running a local server, kill it
	if ( com_sv_running->integer && !strcmp( server, "localhost" ) ) {
		SV_Shutdown( "Server quit" );
	}

	// make sure a local server is killed
	Cvar_Set( "sv_killserver", "1" );
	SV_Frame( 0, 0 );

	noGameRestart = true;
	CL_Disconnect( qtrue );
	Con_Close();

	Q_strncpyz( cls.servername, server, sizeof( cls.servername ) );

	// copy resolved address
	clc.serverAddress = addr;

	if (clc.serverAddress.port == 0) {
		clc.serverAddress.port = BigShort( PORT_SERVER );
	}

	serverString = NET_AdrToStringwPort( &clc.serverAddress );

	Com_Printf( "%s resolved to %s\n", cls.servername, serverString );

	if ( cl_guidServerUniq->integer )
		CL_UpdateGUID( serverString, strlen( serverString ) );
	else
		CL_UpdateGUID( nullptr, 0 );

	// if we aren't playing on a lan, we need to authenticate
	// with the cd key
	if ( NET_IsLocalAddress( &clc.serverAddress ) ) {
		cls.state = CA_CHALLENGING;
	} else {
		cls.state = CA_CONNECTING;

		// Set a client challenge number that ideally is mirrored back by the server.
		//clc.challenge = ((rand() << 16) ^ rand()) ^ Com_Milliseconds();
		Com_RandomBytes( reinterpret_cast<byte *>( &clc.challenge ), sizeof( clc.challenge ) );
	}

	Key_SetCatcher( 0 );
	clc.connectTime = -99999;	// CL_CheckForResend() will fire immediately
	clc.connectPacketCount = 0;

	Cvar_Set( "cl_reconnectArgs", args.data() );

	// server connection string
	Cvar_Set( "cl_currentServerAddress", server );
	CL_WebView_PublishGameStartForAddress( &clc.serverAddress );
	CL_WebHost_HideForGameTransition();
}

#define MAX_RCON_MESSAGE (MAX_STRING_CHARS+4)

/*
==================
CL_CompleteRcon
==================
*/
static void CL_CompleteRcon(const char *args, int argNum )
{
	if ( argNum >= 2 )
	{
		// Skip "rcon "
		const char *p = Com_SkipTokens( args, 1, " " );

		if ( p > args )
			Field_CompleteCommand( p, qtrue, qtrue );
	}
}


/*
=====================
CL_Rcon_f

Send the rest of the command line over as
an unconnected command.
=====================
*/
static void CL_Rcon_f( void ) {
	std::array<char, MAX_RCON_MESSAGE> message;
	const char *sp;
	int len;

	if ( !rcon_client_password->string[0] ) {
		Com_Printf( "You must set 'rconpassword' before\n"
			"issuing an rcon command.\n" );
		return;
	}

	if ( cls.state >= CA_CONNECTED ) {
		rcon_address = clc.netchan.remoteAddress;
	} else {
		if ( !rconAddress->string[0] ) {
			Com_Printf( "You must either be connected,\n"
				"or set the 'rconAddress' cvar\n"
				"to issue rcon commands\n" );
			return;
		}
		if ( !NET_StringToAdr( rconAddress->string, &rcon_address, NA_UNSPEC ) ) {
			return;
		}
		if ( rcon_address.port == 0 ) {
			rcon_address.port = BigShort( PORT_SERVER );
		}
	}

	message[0] = -1;
	message[1] = -1;
	message[2] = -1;
	message[3] = -1;
	message[4] = '\0';

	// we may need to quote password if it contains spaces
	sp = strchr( rcon_client_password->string, ' ' );

	len = Com_sprintf( message.data() + 4, static_cast<int>( message.size() ) - 4,
		sp ? "rcon \"%s\" %s" : "rcon %s %s",
		rcon_client_password->string,
		Cmd_Cmd() + 5 ) + 4 + 1; // including OOB marker and '\0'

	NET_SendPacket( NS_CLIENT, len, message.data(), &rcon_address );
}


/*
=================
CL_SendPureChecksums
=================
*/
static void CL_SendPureChecksums( void ) {
	std::array<char, MAX_STRING_CHARS - 1> cMsg;
	const char *pChecksums;
	int binChecksum;
	int len;

	if ( !cl_connectedToPureServer || clc.demoplaying )
		return;

	// if we are pure we need to send back a command with our referenced pk3 checksums
	if ( !FS_FileIsInPAK( QL_NATIVE_CGAME_DLL, &binChecksum, nullptr ) ) {
		Com_Error( ERR_FATAL, "CL_SendPureChecksums: no pak file for %s", QL_NATIVE_CGAME_DLL );
	}

	len = Com_sprintf( cMsg.data(), static_cast<int>( cMsg.size() ), "cp %d %d ", cl.serverId, binChecksum );
	pChecksums = FS_ReferencedPakPureChecksums( static_cast<int>( cMsg.size() ) - len - 1 );
	Q_strncpyz( cMsg.data() + len, pChecksums, static_cast<int>( cMsg.size() ) - len );

	CL_AddReliableCommand( cMsg.data(), qfalse );
}


/*
=================
CL_ResetPureClientAtServer
=================
*/
static void CL_ResetPureClientAtServer( void ) {
	CL_AddReliableCommand( "vdr", qfalse );
}


/*
=================
CL_Vid_Restart

Restart the video subsystem

we also have to reload the UI and CGame because the renderer
doesn't know what graphics to reload
=================
*/
static int cl_windowResizeDeadline;
static int cl_windowResizeWidth;
static int cl_windowResizeHeight;
static qboolean cl_windowResizePreserveWindow;
static qboolean cl_windowResizeRestart;
static qboolean cl_windowModeChange;


/*
=================
CL_NotifyWindowResize

Record the final client-area size reported by a desktop window manager. The
renderer is restarted only after the resize stream settles, avoiding repeated
context/swapchain rebuilds during an interactive drag.
=================
*/
void CL_NotifyWindowResize( int width, int height, qboolean preserveWindow ) {
	if ( !cls.rendererStarted || cl_windowModeChange || cl_windowResizeRestart ||
		width < 4 || height < 4 ) {
		return;
	}

	cl_windowResizeWidth = width;
	cl_windowResizeHeight = height;
	if ( !cl_windowResizeDeadline ) {
		cl_windowResizePreserveWindow = preserveWindow;
	} else if ( !preserveWindow ) {
		cl_windowResizePreserveWindow = qfalse;
	}
	cl_windowResizeDeadline = Sys_Milliseconds() + 250;
}


qboolean CL_IsWindowResizeRestart( void ) {
	return cl_windowResizeRestart;
}


static void CL_CheckWindowResize( void ) {
	if ( !cl_windowResizeDeadline || Sys_Milliseconds() < cl_windowResizeDeadline ) {
		return;
	}

	cl_windowResizeDeadline = 0;
	Cvar_SetIntegerValue( "r_customWidth", cl_windowResizeWidth );
	Cvar_SetIntegerValue( "r_customHeight", cl_windowResizeHeight );
	Cvar_Set( "r_mode", "-1" );

	if ( cl_windowResizePreserveWindow ) {
		Cbuf_AddText( "vid_restart fast window_resize\n" );
	} else {
		Cbuf_AddText( "vid_restart window_resize\n" );
	}
}


static void CL_Vid_Restart( refShutdownCode_t shutdownCode ) {

	// Settings may have changed so stop recording now
	if ( CL_VideoRecording() )
		CL_CloseAVI( qfalse );

	if ( clc.demorecording )
		CL_StopRecord_f();

	// clear and mute all sounds until next registration
	S_DisableSounds();

	// shutdown VMs
	CL_ShutdownVMs();

	// shutdown the renderer and clear the renderer interface
	CL_ShutdownRef( shutdownCode ); // REF_KEEP_CONTEXT, REF_KEEP_WINDOW, REF_DESTROY_WINDOW

	// client is no longer pure until new checksums are sent
	CL_ResetPureClientAtServer();

	// clear pak references
	FS_ClearPakReferences( FS_UI_REF | FS_CGAME_REF );

	// reinitialize the filesystem if the game directory or checksum has changed
	if ( !clc.demoplaying ) // -EC-
		FS_ConditionalRestart( clc.checksumFeed, qfalse );

	cls.soundRegistered = qfalse;

	// unpause so the cgame definitely gets a snapshot and renders a frame
	Cvar_Set( "cl_paused", "0" );

	CL_ClearMemory();

	// startup all the client stuff
	CL_StartHunkUsers();

	// start the cgame if connected
	if ( ( cls.state > CA_CONNECTED && cls.state != CA_CINEMATIC ) || cls.startCgame ) {
		cls.cgameStarted = qtrue;
		CL_InitCGame();
		// send pure checksums
		CL_SendPureChecksums();
	}

	cls.startCgame = qfalse;
}


/*
=================
CL_Vid_Restart_f

Wrapper for CL_Vid_Restart
=================
*/
static void CL_Vid_Restart_f( void ) {
	const qboolean windowResize =
		( !Q_stricmp( Cmd_Argv( 1 ), "window_resize" ) ||
		  !Q_stricmp( Cmd_Argv( 2 ), "window_resize" ) ) ? qtrue : qfalse;

	if ( Q_stricmp( Cmd_Argv( 1 ), "keep_window" ) == 0 || Q_stricmp( Cmd_Argv( 1 ), "fast" ) == 0 ) {
		// fast path: keep window
		cl_windowResizeRestart = windowResize;
		CL_Vid_Restart( REF_KEEP_WINDOW );
		cl_windowResizeRestart = qfalse;
	} else {
		if ( cls.lastVidRestart ) {
			if ( abs( cls.lastVidRestart - Sys_Milliseconds() ) < 500 ) {
				// hack for OSP mod: do not allow vid restart right after cgame init
				return;
			}
		}
		cl_windowResizeRestart = windowResize;
		CL_Vid_Restart( REF_DESTROY_WINDOW );
		cl_windowResizeRestart = qfalse;
	}
}


#ifdef USE_RENDERER_DLOPEN
/*
=================
CL_RendererSwitch_f

Switch the active renderer and restart the video subsystem.
=================
*/
static void CL_RendererSwitch_f( void ) {
	const char *renderer;
	const char *mode;
	refShutdownCode_t shutdownCode;

	if ( !cl_renderer ) {
		Com_Printf( "renderer_switch: renderer cvars are not initialized\n" );
		return;
	}

	if ( Cmd_Argc() < 2 || Cmd_Argc() > 3 ) {
		Com_Printf( "usage: renderer_switch <renderer> [fast|keep_window|full]\n" );
		Com_Printf( "current renderer: %s\n", cl_renderer->string );
		return;
	}

	renderer = Cmd_Argv( 1 );
	if ( !renderer[0] || !isValidRenderer( renderer ) ) {
		Com_Printf( "renderer_switch: invalid renderer name \"%s\"\n", renderer );
		return;
	}

	mode = Cmd_Argv( 2 );
	if ( !mode[0] || Q_stricmp( mode, "fast" ) == 0 || Q_stricmp( mode, "keep_window" ) == 0 ) {
		shutdownCode = REF_KEEP_WINDOW;
	} else if ( Q_stricmp( mode, "full" ) == 0 || Q_stricmp( mode, "destroy_window" ) == 0 ) {
		shutdownCode = REF_DESTROY_WINDOW;
	} else {
		Com_Printf( "usage: renderer_switch <renderer> [fast|keep_window|full]\n" );
		return;
	}

	if ( Q_stricmp( cl_renderer->string, renderer ) == 0 ) {
		Com_Printf( "renderer_switch: %s is already active, restarting renderer.\n", cl_renderer->string );
	} else {
		Com_Printf( "renderer_switch: %s -> %s\n", cl_renderer->string, renderer );
		Cvar_Set( "cl_renderer", renderer );
	}

	CL_Vid_Restart( shutdownCode );
}
#endif


/*
=================
CL_Snd_Restart_f

Restart the sound subsystem
The cgame and game must also be forced to restart because
handles will be invalid
=================
*/
static void CL_Snd_Restart_f( void )
{
	S_Shutdown();

	// sound will be reinitialized by vid_restart
	CL_Vid_Restart( REF_KEEP_CONTEXT /*REF_KEEP_WINDOW*/ );
}


/*
==================
CL_PK3List_f
==================
*/
void CL_OpenedPK3List_f( void ) {
	Com_Printf("Opened Pack Names: %s\n", FS_LoadedPakNames());
}


/*
==================
CL_PureList_f
==================
*/
static void CL_ReferencedPK3List_f( void ) {
	Com_Printf( "Referenced Pack Names: %s\n", FS_ReferencedPakNames() );
}


/*
==================
CL_Configstrings_f
==================
*/
static void CL_Configstrings_f( void ) {
	int		i;
	int		ofs;

	if ( cls.state != CA_ACTIVE ) {
		Com_Printf( "Not connected to a server.\n");
		return;
	}

	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		ofs = cl.gameState.stringOffsets[ i ];
		if ( !ofs ) {
			continue;
		}
		Com_Printf( "%4i: %s\n", i, cl.gameState.stringData + ofs );
	}
}


/*
==============
CL_Clientinfo_f
==============
*/
static void CL_Clientinfo_f( void ) {
	Com_Printf( "--------- Client Information ---------\n" );
	Com_Printf( "state: %i\n", cls.state );
	Com_Printf( "Server: %s\n", cls.servername );
	Com_Printf ("User info settings:\n");
	Info_Print( Cvar_InfoString( CVAR_USERINFO, nullptr ) );
	Com_Printf( "--------------------------------------\n" );
}


/*
==============
CL_Serverinfo_f
==============
*/
static void CL_Serverinfo_f( void ) {
	int		ofs;

	ofs = cl.gameState.stringOffsets[ CS_SERVERINFO ];
	if ( !ofs )
		return;

	Com_Printf( "Server info settings:\n" );
	Info_Print( cl.gameState.stringData + ofs );
}


/*
===========
CL_Systeminfo_f
===========
*/
static void CL_Systeminfo_f( void ) {
	int ofs;

	ofs = cl.gameState.stringOffsets[ CS_SYSTEMINFO ];
	if ( !ofs )
		return;

	Com_Printf( "System info settings:\n" );
	Info_Print( cl.gameState.stringData + ofs );
}


static void CL_CompleteCallvote(const char *args, int argNum )
{
	if( argNum >= 2 )
	{
		// Skip "callvote "
		const char *p = Com_SkipTokens( args, 1, " " );

		if ( p > args )
			Field_CompleteCommand( p, qtrue, qtrue );
	}
}


//====================================================================

/*
=================
CL_DownloadsComplete

Called when all downloading has been completed
=================
*/
static void CL_DownloadsComplete( void ) {

#ifdef USE_CURL
	// if we downloaded with cURL
	if ( clc.cURLUsed ) {
		clc.cURLUsed = qfalse;
		CL_cURL_Shutdown();
		if ( clc.cURLDisconnected ) {
			if ( clc.downloadRestart ) {
				FS_Restart( clc.checksumFeed );
				clc.downloadRestart = qfalse;
			}
			clc.cURLDisconnected = qfalse;
			CL_Reconnect_f();
			return;
		}
	}
#endif

	// if we downloaded files we need to restart the file system
	if ( clc.downloadRestart ) {
		clc.downloadRestart = qfalse;

		FS_Restart(clc.checksumFeed); // We possibly downloaded a pak, restart the file system to load it

		// inform the server so we get new gamestate info
		CL_AddReliableCommand( "donedl", qfalse );

		// by sending the donedl command we request a new gamestate
		// so we don't want to load stuff yet
		return;
	}

	// let the client game init and load data
	cls.state = CA_LOADING;

	// Pump the loop, this may change gamestate!
	Com_EventLoop();

	// if the gamestate was changed by calling Com_EventLoop
	// then we loaded everything already and we don't want to do it again.
	if ( cls.state != CA_LOADING ) {
		return;
	}

	// flush client memory and start loading stuff
	// this will also (re)load the UI
	// if this is a local client then only the client part of the hunk
	// will be cleared, note that this is done after the hunk mark has been set
	//if ( !com_sv_running->integer )
	CL_FlushMemory();

	// initialize the CGame
	cls.cgameStarted = qtrue;
	CL_InitCGame();

	if ( clc.demofile == FS_INVALID_HANDLE ) {
		Cmd_AddCommand( "callvote", nullptr );
		Cmd_SetCommandCompletionFunc( "callvote", CL_CompleteCallvote );
	}

	// set pure checksums
	CL_SendPureChecksums();

	CL_WritePacket( 2 );
}


/*
=================
CL_BeginDownload

Requests a file to download from the server.  Stores it in the current
game directory.
=================
*/
static void CL_BeginDownload( const char *localName, const char *remoteName ) {

	Com_DPrintf("***** CL_BeginDownload *****\n"
				"Localname: %s\n"
				"Remotename: %s\n"
				"****************************\n", localName, remoteName);

	Q_strncpyz ( clc.downloadName, localName, sizeof(clc.downloadName) );
	Com_sprintf( clc.downloadTempName, sizeof(clc.downloadTempName), "%s.tmp", localName );

	// Set so UI gets access to it
	Cvar_Set( "cl_downloadName", remoteName );
	Cvar_Set( "cl_downloadItem", "" );
	Cvar_Set( "cl_downloadSize", "0" );
	Cvar_Set( "cl_downloadCount", "0" );
	Cvar_SetIntegerValue( "cl_downloadTime", cls.realtime );

	clc.downloadBlock = 0; // Starting new file
	clc.downloadCount = 0;

	CL_AddReliableCommand( va("download %s", remoteName), qfalse );
}


/*
=================
CL_NextDownload

A download completed or failed
=================
*/
void CL_NextDownload( void )
{
	char *s;
	char *remoteName, *localName;
	bool useCURL = false;

	// A download has finished, check whether this matches a referenced checksum
	if(*clc.downloadName)
	{
		const char *zippath = FS_BuildOSPath(Cvar_VariableString("fs_homepath"), clc.downloadName, nullptr );

		if(!FS_CompareZipChecksum(zippath))
			Com_Error(ERR_DROP, "Incorrect checksum for file: %s", clc.downloadName);
	}

	*clc.downloadTempName = *clc.downloadName = '\0';
	Cvar_Set("cl_downloadName", "");

	// We are looking to start a download here
	if (*clc.downloadList) {
		s = clc.downloadList;

		// format is:
		//  @remotename@localname@remotename@localname, etc.

		if (*s == '@')
			s++;
		remoteName = s;

		if ( (s = strchr(s, '@')) == nullptr ) {
			CL_DownloadsComplete();
			return;
		}

		*s++ = '\0';
		localName = s;
		if ( (s = strchr(s, '@')) != nullptr )
			*s++ = '\0';
		else
			s = localName + strlen(localName); // point at the null byte

#ifdef USE_CURL
		if(!(cl_allowDownload->integer & DLF_NO_REDIRECT)) {
			if(clc.sv_allowDownload & DLF_NO_REDIRECT) {
				Com_Printf("WARNING: server does not "
					"allow download redirection "
					"(sv_allowDownload is %d)\n",
					clc.sv_allowDownload);
			}
			else if(!*clc.sv_dlURL) {
				Com_Printf("WARNING: server allows "
					"download redirection, but does not "
					"have sv_dlURL set\n");
			}
			else if(!CL_cURL_Init()) {
				Com_Printf("WARNING: could not load "
					"cURL library\n");
			}
			else {
				CL_cURL_BeginDownload(localName, va("%s/%s",
					clc.sv_dlURL, remoteName));
				useCURL = true;
			}
		}
		else if(!(clc.sv_allowDownload & DLF_NO_REDIRECT)) {
			Com_Printf("WARNING: server allows download "
				"redirection, but it disabled by client "
				"configuration (cl_allowDownload is %d)\n",
				cl_allowDownload->integer);
		}
#endif /* USE_CURL */

		if( !useCURL ) {
		if( (cl_allowDownload->integer & DLF_NO_UDP) ) {
				Com_Error(ERR_DROP, "UDP Downloads are "
					"disabled on your client. "
					"(cl_allowDownload is %d)",
					cl_allowDownload->integer);
				return;
			}
			else {
				CL_BeginDownload( localName, remoteName );
			}
		}
		clc.downloadRestart = qtrue;

		// move over the rest
		std::copy_n( s, strlen( s ) + 1, clc.downloadList );

		return;
	}

	CL_DownloadsComplete();
}


/*
=================
CL_InitDownloads

After receiving a valid game state, we valid the cgame and local zip files here
and determine if we need to download them
=================
*/
static void CL_InitLegacyDownloads( void ) {

	if ( !(cl_allowDownload->integer & DLF_ENABLE) )
	{
		std::array<char, MAXPRINTMSG> missingfiles;

		// autodownload is disabled on the client
		// but it's possible that some referenced files on the server are missing
		if ( FS_ComparePaks( missingfiles.data(), static_cast<int>( missingfiles.size() ), qfalse ) )
		{
			// NOTE TTimo I would rather have that printed as a modal message box
			// but at this point while joining the game we don't know whether we will successfully join or not
			Com_Printf( "\nWARNING: You are missing some files referenced by the server:\n%s"
				"You might not be able to join the game\n"
				"Go to the setting menu to turn on autodownload, or get the file elsewhere\n\n", missingfiles.data() );
		}
	}
	else if ( FS_ComparePaks( clc.downloadList, sizeof( clc.downloadList ) , qtrue ) ) {

		Com_Printf( "Need paks: %s\n", clc.downloadList );

		if ( *clc.downloadList ) {
			// if autodownloading is not enabled on the server
			cls.state = CA_CONNECTED;

			*clc.downloadTempName = *clc.downloadName = '\0';
			Cvar_Set( "cl_downloadName", "" );

			CL_NextDownload();
			return;
		}

	}

#ifdef USE_CURL
	if ( cl_mapAutoDownload->integer && ( !(clc.sv_allowDownload & DLF_ENABLE) || clc.demoplaying ) )
	{
		const char *info, *mapname, *bsp;

		// get map name and BSP file name
		info = cl.gameState.stringData + cl.gameState.stringOffsets[ CS_SERVERINFO ];
		mapname = Info_ValueForKey( info, "mapname" );
		bsp = va( "maps/%s.bsp", mapname );

		if ( FS_FOpenFileRead( bsp, nullptr, qfalse ) == -1 )
		{
			if ( CL_Download( "dlmap", mapname, qtrue ) )
			{
				cls.state = CA_CONNECTED; // prevent continue loading and shows the ui download progress screen
				return;
			}
		}
	}
#endif // USE_CURL

	CL_DownloadsComplete();
}

void CL_ResumeDownloadsAfterWorkshop( void ) {
	CL_InitLegacyDownloads();
}

void CL_InitDownloads( void ) {
	constexpr int QL_STEAM_REFERENCED_CONFIGSTRING = 0x2cb;
	const fnql::protocol::Contract &activeContract =
		fnql::protocol::ForWireProfile( clc.netchan.wireProfile );
	if ( !clc.demoplaying &&
		activeContract.Has( fnql::protocol::Capability::WorkshopContent ) &&
		CL_Workshop_BeginRequiredDownloads(
			CL_GetConfigStringValue( QL_STEAM_REFERENCED_CONFIGSTRING ) ) ) {
		return;
	}
	CL_InitLegacyDownloads();
}


/*
=================
CL_CheckForResend

Resend a connect message if the last one has timed out
=================
*/
static void CL_CheckForResend( void ) {
	int		port, len, connectProtocol;
	std::array<char, MAX_INFO_STRING * 2> info; // larger buffer to detect overflows
	std::array<char, MAX_INFO_STRING> data;
	bool		notOverflowed;
	qboolean	infoTruncated;

	// don't send anything if playing back a demo
	if ( clc.demoplaying ) {
		return;
	}

	// resend if we haven't gotten a reply yet
	if ( cls.state != CA_CONNECTING && cls.state != CA_CHALLENGING ) {
		return;
	}

	if ( cls.realtime - clc.connectTime < RETRANSMIT_TIMEOUT ) {
		return;
	}

	clc.connectTime = cls.realtime;	// for retransmit requests
	clc.connectPacketCount++;

	switch ( cls.state ) {
	case CA_CONNECTING:
		// The retired Quake III authorize service applies only to the legacy
		// protocol. Protocol 91 authenticates with its Steam challenge ticket.
#ifndef STANDALONE
		if ( com_protocol->integer <= OLD_PROTOCOL_VERSION &&
			!Cvar_VariableIntegerValue("com_standalone") &&
			clc.serverAddress.type == NA_IP &&
			!Sys_IsLANAddress( &clc.serverAddress ) )
			CL_RequestAuthorization();
#endif
		if ( com_protocol->integer == QL_RETAIL_PROTOCOL_VERSION ) {
			if ( !CL_SendRetailSteamChallenge() ) {
				// Retail's deterministic no-Steam fallback is a bare textual
				// challenge. The remote server remains authoritative about whether
				// an unauthenticated client is allowed.
				NET_OutOfBandPrint( NS_CLIENT, &clc.serverAddress, "getchallenge" );
				steamChallengeTicket.requestMode =
					fnql::client::auth::ChallengeRequestMode::RetailWithoutSteam;
				Com_DPrintf( "Steam challenge ticket unavailable; sent protocol-91 no-Steam fallback.\n" );
			}
		} else {
			// Legacy/ioq3 peers echo this nonce, preventing an unrelated server
			// from redirecting the connection.
			NET_OutOfBandPrint( NS_CLIENT, &clc.serverAddress,
				"getchallenge %d %s", clc.challenge, GAMENAME_FOR_MASTER );
			steamChallengeTicket.requestMode =
				fnql::client::auth::ChallengeRequestMode::LegacyNonce;
		}
		break;

	case CA_CHALLENGING:
		// sending back the challenge
		port = Cvar_VariableIntegerValue( "net_qport" );

		infoTruncated = qfalse;
		Q_strncpyz( info.data(), Cvar_InfoString( CVAR_USERINFO, &infoTruncated ), static_cast<int>( info.size() ) );

		// remove some non-important keys that may cause overflow during connection
		if ( strlen( info.data() ) > MAX_USERINFO_LENGTH - 64 ) {
			infoTruncated = ToQboolean( infoTruncated || Info_RemoveKey( info.data(), "xp_name" ) );
			infoTruncated = ToQboolean( infoTruncated || Info_RemoveKey( info.data(), "xp_country" ) );
		}

		len = strlen( info.data() );
		if ( len > MAX_USERINFO_LENGTH ) {
			notOverflowed = false;
		} else {
			notOverflowed = true;
		}

		connectProtocol = clc.compat ? OLD_PROTOCOL_VERSION :
			( clc.handshakeProtocol > 0 ? clc.handshakeProtocol : com_protocol->integer );
		notOverflowed = notOverflowed &&
			Info_SetValueForKey_s( info.data(), MAX_USERINFO_LENGTH, "protocol",
				va( "%i", connectProtocol ) );

		notOverflowed = notOverflowed &&
			Info_SetValueForKey_s( info.data(), MAX_USERINFO_LENGTH, "qport", va( "%i", port ) );

		notOverflowed = notOverflowed &&
			Info_SetValueForKey_s( info.data(), MAX_USERINFO_LENGTH, "challenge", va( "%i", clc.challenge ) );

		// for now - this will be used to inform server about q3msgboom fix
		// this is optional key so will not trigger oversize warning
		Info_SetValueForKey_s( info.data(), MAX_USERINFO_LENGTH, "client", Q3_VERSION );

		if ( !notOverflowed ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: oversize userinfo, you might be not able to join remote server!\n" );
		}

		len = Com_sprintf( data.data(), static_cast<int>( data.size() ), "connect \"%s\"", info.data() );
		// NOTE TTimo don't forget to set the right data length!
		NET_OutOfBandCompress( NS_CLIENT, &clc.serverAddress, reinterpret_cast<byte *>( data.data() ), len );
		// the most current userinfo has been sent, so watch for any
		// newer changes to userinfo variables
		cvar_modifiedFlags &= ~CVAR_USERINFO;

		// ... but force re-send if userinfo was truncated in any way
		if ( infoTruncated || !notOverflowed ) {
			cvar_modifiedFlags |= CVAR_USERINFO;
		}
		break;

	default:
		Com_Error( ERR_FATAL, "CL_CheckForResend: bad cls.state" );
	}
}


/*
===================
CL_MotdPacket
===================
*/
static void CL_MotdPacket( const netadr_t *from ) {
	const char *challenge;
	const char *info;

	// if not from our server, ignore it
	if ( !NET_CompareAdr( from, &cls.updateServer ) ) {
		return;
	}

	info = Cmd_Argv(1);

	// check challenge
	challenge = Info_ValueForKey( info, "challenge" );
	if ( strcmp( challenge, cls.updateChallenge ) ) {
		return;
	}

	challenge = Info_ValueForKey( info, "motd" );

	Q_strncpyz( cls.updateInfoString, info, sizeof( cls.updateInfoString ) );
	Cvar_Set( "cl_motdString", challenge );
}


/*
===================
CL_InitServerInfo
===================
*/
static void CL_InitServerInfo( serverInfo_t *server, const netadr_t *address ) {
	server->adr = *address;
	server->clients = 0;
	server->hostName[0] = '\0';
	server->mapName[0] = '\0';
	server->maxClients = 0;
	server->maxPing = 0;
	server->minPing = 0;
	server->ping = -1;
	server->game[0] = '\0';
	server->gameType = 0;
	server->netType = 0;
	server->punkbuster = 0;
	server->g_humanplayers = 0;
	server->g_needpass = 0;
}

struct hash_chain_t {
	netadr_t             addr;
	hash_chain_t *next;
};

static std::array<hash_chain_t *, kServerAddressHashBuckets> hash_table;
static std::array<hash_chain_t, MAX_GLOBAL_SERVERS> hash_list;
static std::size_t hash_count = 0;

static unsigned int hash_func( const netadr_t *addr ) {

	const byte		*ip = nullptr;
	unsigned int	size;
	unsigned int	i;
	unsigned int	hash = 0;

	switch ( addr->type ) {
		case NA_IP:  ip = addr->ipv._4; size = 4;  break;
#ifdef USE_IPV6
		case NA_IP6: ip = addr->ipv._6; size = 16; break;
#endif
		default: size = 0; break;
	}

	for ( i = 0; i < size; i++ )
		hash = hash * 101 + static_cast<int>( *ip++ );

	hash = hash ^ ( hash >> 16 );

	return ( hash & static_cast<unsigned int>( kServerAddressHashBuckets - 1 ) );
}

static void hash_insert( const netadr_t *addr )
{
	hash_chain_t **tab, *cur;
	unsigned int hash;
	if ( hash_count >= hash_list.size() )
		return;
	hash = hash_func( addr );
	tab = &hash_table[ hash ];
	cur = &hash_list[ hash_count++ ];
	cur->addr = *addr;
	cur->next = *tab;
	*tab = cur;
}

static void hash_reset( void )
{
	hash_count = 0;
	hash_list = {};
	hash_table.fill( nullptr );
}

static hash_chain_t *hash_find( const netadr_t *addr )
{
	hash_chain_t *cur;
	cur = hash_table[ hash_func( addr ) ];
	while ( cur != nullptr ) {
		if ( NET_CompareAdr( addr, &cur->addr ) )
			return cur;
		cur = cur->next;
	}
	return nullptr;
}


/*
===================
CL_ServersResponsePacket
===================
*/
static void CL_ServersResponsePacket( const netadr_t* from, msg_t *msg, qboolean extended ) {
	int				i, count, total;
	netadr_t addresses[kMaxServersPerPacket];
	int				numservers;
	byte*			buffptr;
	byte*			buffend;
	serverInfo_t	*server;

	//Com_Printf("CL_ServersResponsePacket\n"); // moved down

	if (cls.numglobalservers == -1) {
		// state to detect lack of servers or lack of response
		cls.numglobalservers = 0;
		cls.numGlobalServerAddresses = 0;
		hash_reset();
	}

	// parse through server response string
	numservers = 0;
	buffptr    = msg->data;
	buffend    = buffptr + msg->cursize;

	// advance to initial token
	do
	{
		if(*buffptr == '\\' || (extended && *buffptr == '/'))
			break;

		buffptr++;
	} while (buffptr < buffend);

	while (buffptr + 1 < buffend)
	{
		// IPv4 address
		if (*buffptr == '\\')
		{
			buffptr++;

			if (buffend - buffptr < static_cast<decltype(buffend - buffptr)>( sizeof(addresses[numservers].ipv._4) + sizeof(addresses[numservers].port) + 1 ))
				break;

			for(i = 0; i < static_cast<int>( sizeof(addresses[numservers].ipv._4) ); i++)
				addresses[numservers].ipv._4[i] = *buffptr++;

			addresses[numservers].type = NA_IP;
		}
#ifdef USE_IPV6
		// IPv6 address, if it's an extended response
		else if (extended && *buffptr == '/')
		{
			buffptr++;

			if (buffend - buffptr < static_cast<decltype(buffend - buffptr)>( sizeof(addresses[numservers].ipv._6) + sizeof(addresses[numservers].port) + 1 ))
				break;

			for(i = 0; i < static_cast<int>( sizeof(addresses[numservers].ipv._6) ); i++)
				addresses[numservers].ipv._6[i] = *buffptr++;

			addresses[numservers].type = NA_IP6;
			addresses[numservers].scope_id = from->scope_id;
		}
#endif
		else
			// syntax error!
			break;

		// parse out port
		addresses[numservers].port = (*buffptr++) << 8;
		addresses[numservers].port += *buffptr++;
		addresses[numservers].port = BigShort( addresses[numservers].port );

		// syntax check
		if (*buffptr != '\\' && *buffptr != '/')
			break;

		numservers++;
		if (numservers >= kMaxServersPerPacket)
			break;
	}

	count = cls.numglobalservers;

	for (i = 0; i < numservers && count < MAX_GLOBAL_SERVERS; i++) {

		// Tequila: It's possible to have sent many master server requests. Then
		// we may receive many times the same addresses from the master server.
		// We just avoid to add a server if it is still in the global servers list.
		if ( hash_find( &addresses[i] ) )
			continue;

		hash_insert( &addresses[i] );

		// build net address
		server = &cls.globalServers[count];

		CL_InitServerInfo( server, &addresses[i] );
		// advance to next slot
		count++;
	}

	// if getting the global list
	if ( count >= MAX_GLOBAL_SERVERS && cls.numGlobalServerAddresses < MAX_GLOBAL_SERVERS )
	{
		// if we couldn't store the servers in the main list anymore
		for (; i < numservers && cls.numGlobalServerAddresses < MAX_GLOBAL_SERVERS; i++)
		{
			// just store the addresses in an additional list
			cls.globalServerAddresses[cls.numGlobalServerAddresses++] = addresses[i];
		}
	}

	cls.numglobalservers = count;
	total = count + cls.numGlobalServerAddresses;

	Com_Printf( "getserversResponse:%3d servers parsed (total %d)\n", numservers, total);
}


/*
=================
CL_ConnectionlessPacket

Responses to broadcasts, etc

return true only for commands indicating that our server is alive
or connection sequence is going into the right way
=================
*/
static bool CL_ConnectionlessPacket( const netadr_t *from, msg_t *msg ) {
	bool fromserver;
	const char *s;
	const char *c;
	int challenge = 0;

	MSG_BeginReadingOOB( msg );
	MSG_ReadLong( msg );	// skip the -1

	s = MSG_ReadStringLine( msg );

	Cmd_TokenizeString( s );

	c = Cmd_Argv(0);

	if ( com_developer->integer ) {
		Com_Printf( "CL packet %s: %s\n", NET_AdrToStringwPort( from ), s );
	}

	// challenge from the server we are connecting to
	if ( !Q_stricmp(c, "challengeResponse" ) ) {
		int serverProtocol = 0;
		const fnql::client::auth::ChallengeRequestMode requestMode =
			steamChallengeTicket.requestMode;
		const bool retailRequest =
			fnql::client::auth::IsRetailRequest( requestMode );

		if ( cls.state != CA_CONNECTING ) {
			Com_DPrintf( "Unwanted challenge response received. Ignored.\n" );
			return false;
		}

		c = Cmd_Argv( 2 );
		if ( *c != '\0' )
			challenge = std::atoi( c );

		clc.compat = qtrue;
		s = Cmd_Argv( 3 ); // analyze server protocol version
		if ( *s != '\0' ) {
			int sv_proto = std::atoi( s );
			serverProtocol = sv_proto;
			if ( sv_proto > OLD_PROTOCOL_VERSION ) {
				if ( sv_proto == NEW_PROTOCOL_VERSION ||
					sv_proto == QL_RETAIL_PROTOCOL_VERSION ||
					sv_proto == com_protocol->integer ) {
					clc.compat = qfalse;
				} else {
					int cl_proto = com_protocol->integer;
					if ( cl_proto == DEFAULT_PROTOCOL_VERSION ) {
						cl_proto = QL_RETAIL_PROTOCOL_VERSION;
					}
					Com_Printf( S_COLOR_YELLOW "Warning: Server reports protocol version %d, "
						"we have %d. Trying legacy protocol %d.\n",
						sv_proto, cl_proto, OLD_PROTOCOL_VERSION );
				}
			}
		}
		if ( serverProtocol <= 0 && retailRequest ) {
			// Retail protocol 91 sends a bare challengeResponse for both its
			// binary Steam-ticket request and the no-Steam fallback. The request
			// mode is therefore the protocol discriminator.
			serverProtocol = QL_RETAIL_PROTOCOL_VERSION;
			clc.compat = qfalse;
		}

		if ( retailRequest )
		{
			// Retail does not echo a client nonce. Constrain its handoff to the
			// address that received our credential-bearing request; a same-host
			// port handoff remains compatible with the retail proxy behavior.
			if ( !NET_CompareBaseAdr( from, &clc.serverAddress ) )
			{
				Com_Printf( "Retail challenge response from unexpected host. Ignored.\n" );
				return false;
			}
		}
		else if ( clc.compat )
		{
			if ( !NET_CompareAdr( from, &clc.serverAddress ) )
			{
				// This challenge response is not coming from the expected address.
				// Check whether we have a matching client challenge to prevent
				// connection hi-jacking.
				if ( *c == '\0' || challenge != clc.challenge )
				{
					Com_DPrintf( "Challenge response received from unexpected source. Ignored.\n" );
					return false;
				}
			}
		}
		else
		{
			if ( *c == '\0' || challenge != clc.challenge )
			{
				Com_Printf( "Bad challenge for challengeResponse. Ignored.\n" );
				return false;
			}
		}

		// start sending connect instead of challenge request packets
		clc.challenge = std::atoi( Cmd_Argv(1) );
		clc.handshakeProtocol = fnql::client::auth::SelectHandshakeProtocol(
			requestMode, serverProtocol, QL_RETAIL_PROTOCOL_VERSION,
			OLD_PROTOCOL_VERSION );
		cls.state = CA_CHALLENGING;
		clc.connectPacketCount = 0;
		clc.connectTime = -99999;

		// take this address as the new server address.  This allows
		// a server proxy to hand off connections to multiple servers
		clc.serverAddress = *from;
		// Keep the Steam ticket handle alive until disconnect so a retail
		// server's asynchronous validation cannot be invalidated by us. The
		// raw ticket bytes are no longer needed after the accepted challenge.
		CL_EraseSteamChallengePayload();
		Com_DPrintf( "challengeResponse: %d\n", clc.challenge );
		return true;
	}

	// server connection
	if ( !Q_stricmp(c, "connectResponse") ) {
		int negotiatedProtocol;

		if ( cls.state >= CA_CONNECTED ) {
			Com_Printf( "Dup connect received. Ignored.\n" );
			return false;
		}
		if ( cls.state != CA_CHALLENGING ) {
			Com_Printf( "connectResponse packet while not connecting. Ignored.\n" );
			return false;
		}
		if ( !NET_CompareBaseAdr( from, &clc.serverAddress ) ) {
			Com_Printf( "connectResponse from wrong host. Ignored.\n" );
			return false;
		}
		// Complete an allowed same-host proxy handoff so subsequent
		// connectionless diagnostics are matched against the active port too.
		clc.serverAddress = *from;

		negotiatedProtocol = clc.compat ? OLD_PROTOCOL_VERSION :
			( clc.handshakeProtocol > OLD_PROTOCOL_VERSION ? clc.handshakeProtocol :
				com_protocol->integer );

		const fnql::protocol::Contract *contract =
			fnql::protocol::Find( negotiatedProtocol, clc.compat != qfalse );

		if ( !clc.compat &&
			( !contract || contract->family != fnql::protocol::Family::QuakeLive ) ) {
			// first argument: challenge response
			c = Cmd_Argv( 1 );
			if ( *c != '\0' ) {
				challenge = std::atoi( c );
			} else {
				Com_Printf( "Bad connectResponse received. Ignored.\n" );
				return false;
			}

			if ( challenge != clc.challenge ) {
				Com_Printf( "ConnectResponse with bad challenge received. Ignored.\n" );
				return false;
			}

			// second (optional) argument: actual protocol version used on server-side
			c = Cmd_Argv( 2 );
			if ( *c != '\0' ) {
				int protocol = std::atoi( c );
				if ( protocol > 0 ) {
					negotiatedProtocol = protocol;
					if ( protocol <= OLD_PROTOCOL_VERSION ) {
						clc.compat = qtrue;
					} else {
						clc.compat = qfalse;
					}
				}
			}

			if ( com_protocolCompat &&
				negotiatedProtocol != QL_RETAIL_PROTOCOL_VERSION ) {
				// Preserve the explicit ioq3 compatibility fallback without
				// downgrading the retail Quake Live wire contract.
				clc.compat = qtrue;
			}
		} else if ( !clc.compat && Cmd_Argc() > 1 ) {
			// FnQL peers may echo the challenge and selected protocol even on
			// the QL wire. Validate it when present, while accepting the bare
			// connectResponse required by retail clients.
			challenge = std::atoi( Cmd_Argv( 1 ) );
			if ( challenge != clc.challenge ) {
				Com_Printf( "ConnectResponse with bad challenge received. Ignored.\n" );
				return false;
			}
			if ( Cmd_Argc() > 2 &&
				std::atoi( Cmd_Argv( 2 ) ) != QL_RETAIL_PROTOCOL_VERSION ) {
				Com_Printf( "ConnectResponse selected an invalid QL protocol. Ignored.\n" );
				return false;
			}
		}

		Netchan_Setup( NS_CLIENT, &clc.netchan, from,
			Cvar_VariableIntegerValue( "net_qport" ), clc.challenge,
			Netchan_SelectWireProfile( negotiatedProtocol, clc.compat ) );

		cls.state = CA_CONNECTED;
		clc.lastPacketSentTime = cls.realtime - 9999; // send first packet immediately
		return true;
	}

	// server responding to an info broadcast
	if ( !Q_stricmp(c, "infoResponse") ) {
		CL_ServerInfoPacket( from, msg );
		return false;
	}

	// server responding to a get playerlist
	if ( !Q_stricmp(c, "statusResponse") ) {
		CL_ServerStatusResponse( from, msg );
		return false;
	}

	// A server that has already dropped our netchan sends an out-of-band
	// disconnect in response to late client packets. Retail accepts it only
	// from the active endpoint and only after three quiet seconds, which keeps
	// a spoofed packet from tearing down a healthy session.
	if ( !Q_stricmp( c, "disconnect" ) ) {
		if ( cls.state >= CA_AUTHORIZING
			&& NET_CompareAdr( from, &clc.netchan.remoteAddress )
			&& cls.realtime - clc.lastPacketTime >= 3000 ) {
			Com_Printf( "Server disconnected for unknown reason\n" );
			Cvar_Set( "com_errorMessage",
				"Server disconnected for unknown reason\n" );
			CL_Disconnect( qtrue );
		}
		return false;
	}

	// echo request from server
	if ( !Q_stricmp(c, "echo") ) {
		// NOTE: we may have to add exceptions for auth and update servers
		if ( ( fromserver = NET_CompareAdr( from, &clc.serverAddress ) ) || NET_CompareAdr( from, &rcon_address ) ) {
			NET_OutOfBandPrint( NS_CLIENT, from, "%s", Cmd_Argv(1) );
		}
		return fromserver;
	}

	// cd check
	if ( !Q_stricmp(c, "keyAuthorize") ) {
		// we don't use these now, so dump them on the floor
		return false;
	}

	// global MOTD from id
	if ( !Q_stricmp(c, "motd") ) {
		CL_MotdPacket( from );
		return false;
	}

	// print string from server
	if ( !Q_stricmp(c, "print") ) {
		// NOTE: we may have to add exceptions for auth and update servers
		if ( ( fromserver = NET_CompareAdr( from, &clc.serverAddress ) ) || NET_CompareAdr( from, &rcon_address ) ) {
			s = MSG_ReadString( msg );
			Q_strncpyz( clc.serverMessage, s, sizeof( clc.serverMessage ) );
			Com_Printf( "%s", s );
		}
		return fromserver;
	}

	// list of servers sent back by a master server (classic)
	if ( !Q_strncmp(c, "getserversResponse", 18) ) {
		CL_ServersResponsePacket( from, msg, qfalse );
		return false;
	}

	// list of servers sent back by a master server (extended)
	if ( !Q_strncmp(c, "getserversExtResponse", 21) ) {
		CL_ServersResponsePacket( from, msg, qtrue );
		return false;
	}

	Com_DPrintf( "Unknown connectionless packet command.\n" );
	return false;
}


/*
=================
CL_PacketEvent

A packet has arrived from the main event loop
=================
*/
void CL_PacketEvent( const netadr_t *from, msg_t *msg ) {
	int		headerBytes;

	if ( msg->cursize < 5 ) {
		Com_DPrintf( "%s: Runt packet\n", NET_AdrToStringwPort( from ) );
		return;
	}

	if ( ReadUnaligned<int>( msg->data ) == -1 ) {
		if ( CL_ConnectionlessPacket( from, msg ) )
			clc.lastPacketTime = cls.realtime;
		return;
	}

	if ( cls.state < CA_CONNECTED ) {
		return;		// can't be a valid sequenced packet
	}

	//
	// packet from server
	//
	if ( !NET_CompareAdr( from, &clc.netchan.remoteAddress ) ) {
		if ( com_developer->integer ) {
			Com_Printf( "%s:sequenced packet without connection\n",
				NET_AdrToStringwPort( from ) );
		}
		// FIXME: send a client disconnect?
		return;
	}

	if ( !CL_Netchan_Process( &clc.netchan, msg ) ) {
		return;		// out of order, duplicated, etc
	}

	// the header is different lengths for reliable and unreliable messages
	headerBytes = msg->readcount;

	// track the last message received so it can be returned in
	// client messages, allowing the server to detect a dropped
	// gamestate
	clc.serverMessageSequence = LittleLong( ReadUnaligned<int32_t>( msg->data ) );

	clc.lastPacketTime = cls.realtime;
	CL_ParseServerMessage( msg );

	//
	// we don't know if it is ok to save a demo message until
	// after we have parsed the frame
	//
	if ( clc.demorecording && !clc.demowaiting && !clc.demoplaying ) {
		CL_WriteDemoMessage( msg, headerBytes );
	}
}


/*
==================
CL_CheckTimeout
==================
*/
static void CL_CheckTimeout( void ) {
	//
	// check timeout
	//
	if ( ( !CL_CheckPaused() || !sv_paused->integer )
		&& cls.state >= CA_CONNECTED && cls.state != CA_CINEMATIC
		&& cls.realtime - clc.lastPacketTime > cl_timeout->integer * 1000 ) {
		if ( ++cl.timeoutcount > 5 ) { // timeoutcount saves debugger
			Com_Printf( "\nServer connection timed out.\n" );
			Cvar_Set( "com_errorMessage", "Server connection timed out." );
			if ( !CL_Disconnect( qtrue ) ) { // restart client if not done already
				CL_FlushMemory();
			}
			return;
		}
	} else {
		cl.timeoutcount = 0;
	}
}


/*
==================
CL_CheckPaused
Check whether client has been paused.
==================
*/
qboolean CL_CheckPaused( void )
{
	// if cl_paused->modified is set, the cvar has only been changed in
	// this frame. Keep paused in this frame to ensure the server doesn't
	// lag behind.
	if(cl_paused->integer || cl_paused->modified)
		return qtrue;

	return qfalse;
}


/*
==================
CL_NoDelay
==================
*/
qboolean CL_NoDelay( void )
{
	if ( CL_VideoRecording() || ( com_timedemo->integer && clc.demofile != FS_INVALID_HANDLE ) )
		return qtrue;

	return qfalse;
}


/*
==================
CL_CheckUserinfo
==================
*/
static void CL_CheckUserinfo( void ) {

	// don't add reliable commands when not yet connected
	if ( cls.state < CA_CONNECTED )
		return;

	// don't overflow the reliable command buffer when paused
	if ( CL_CheckPaused() )
		return;

	// send a reliable userinfo update if needed
	if ( cvar_modifiedFlags & CVAR_USERINFO )
	{
		qboolean infoTruncated = qfalse;
		const char *info;

		cvar_modifiedFlags &= ~CVAR_USERINFO;

		info = Cvar_InfoString( CVAR_USERINFO, &infoTruncated );
		if ( strlen( info ) > MAX_USERINFO_LENGTH || infoTruncated ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: oversize userinfo, you might be not able to play on remote server!\n" );
		}

		CL_AddReliableCommand( va( "userinfo \"%s\"", info ), qfalse );
	}
}


/*
==================
CL_Frame
==================
*/
void CL_Frame( int msec, int realMsec ) {
	int gameMsec = msec;
	int audioMsec = realMsec;
	float oldViewYaw;
	float oldViewPitch;

#ifdef USE_CURL
	if ( download.cURL ) {
		Com_DL_Perform( &download );
	}
#endif

	if ( !com_cl_running->integer ) {
		return;
	}

	CL_CheckWindowResize();

	// save the msec before checking pause
	cls.realFrametime = realMsec;

#ifdef USE_CURL
	if ( clc.downloadCURLM ) {
		CL_cURL_PerformDownload();
		// we can't process frames normally when in disconnected
		// download mode since the ui vm expects cls.state to be
		// CA_CONNECTED
		if ( clc.cURLDisconnected ) {
			cls.frametime = realMsec;
			cls.gameFrametime = gameMsec;
			cls.realtime += realMsec;
			cls.gametime += gameMsec;
			cls.framecount++;
			SCR_UpdateScreen();
			S_Update( realMsec );
			Con_RunConsole();
			return;
		}
	}
#endif

	if ( cls.cddialog ) {
		// bring up the cd error dialog if needed
		cls.cddialog = qfalse;
		VM_Call( uivm, 1, UI_SET_ACTIVE_MENU, UIMENU_NEED_CD );
	} else	if ( cls.state == CA_DISCONNECTED && !( Key_GetCatcher( ) & KEYCATCH_UI )
		&& !( Key_GetCatcher() & KEYCATCH_BROWSER ) && !CL_UIMenusAreVisible()
		&& !com_sv_running->integer && uivm ) {
		// Bring up the menu once.  UI_MENUS_ANY_VISIBLE covers menus whose input
		// catcher was temporarily yielded during a renderer or browser transition.
		S_StopAllSounds();
		VM_Call( uivm, 1, UI_SET_ACTIVE_MENU, UIMENU_MAIN );
	}

	// if recording an avi, lock to a fixed fps
	if ( CL_VideoRecording() && gameMsec ) {
		// save the current screen
		if ( cls.state == CA_ACTIVE || cl_forceavidemo->integer ) {
			float fps, frameDuration;

			if ( com_timescale->value > 0.0001f )
				fps = MIN( cl_aviFrameRate->value / com_timescale->value, 1000.0f );
			else
				fps = 1000.0f;

			frameDuration = MAX( 1000.0f / fps, 1.0f ) + clc.aviVideoFrameRemainder;

			CL_TakeVideoFrame();

			gameMsec = static_cast<int>( frameDuration );
			clc.aviVideoFrameRemainder = frameDuration - gameMsec;

			audioMsec = gameMsec; // sync sound duration
		}
	}

	if ( cl_autoRecordDemo->integer && !clc.demoplaying ) {
		if ( cls.state == CA_ACTIVE && !clc.demorecording ) {
			// If not recording a demo, and we should be, start one
			qtime_t	now;
			const char	*nowString;
			char		*p;
			std::array<char, MAX_QPATH> mapName;
			std::array<char, MAX_OSPATH> serverName;

			Com_RealTime( &now );
			nowString = va( "%04d%02d%02d%02d%02d%02d",
					1900 + now.tm_year,
					1 + now.tm_mon,
					now.tm_mday,
					now.tm_hour,
					now.tm_min,
					now.tm_sec );

			Q_strncpyz( serverName.data(), cls.servername, static_cast<int>( serverName.size() ) );
			// Replace the ":" in the address as it is not a valid
			// file name character
			p = strchr( serverName.data(), ':' );
			if ( p ) {
				*p = '.';
			}

			Q_strncpyz( mapName.data(), COM_SkipPath( cl.mapname ), static_cast<int>( mapName.size() ) );
			COM_StripExtension(mapName.data(), mapName.data(), static_cast<int>( mapName.size() ));

			Cbuf_ExecuteText( EXEC_NOW,
					va( "record %s-%s-%s", nowString, serverName.data(), mapName.data() ) );
		}
		else if ( cls.state != CA_ACTIVE && clc.demorecording ) {
			// Recording, but not CA_ACTIVE, so stop recording
			CL_StopRecord_f();
		}
	}

	// decide the simulation time
	if ( clc.demoplaying && cl_freezeDemo && cl_freezeDemo->integer ) {
		gameMsec = 0;
	}
	cls.frametime = realMsec;
	cls.gameFrametime = gameMsec;
	cls.realtime += realMsec;
	cls.gametime += gameMsec;
	CL_SteamP2PFrame();
	CL_Workshop_Frame();

	if ( cl_timegraph->integer ) {
		SCR_DebugGraph( realMsec * 0.25f );
	}

	// see if we need to update any userinfo
	CL_CheckUserinfo();

	// if we haven't gotten a packet in a long time, drop the connection
	if ( !clc.demoplaying ) {
		CL_CheckTimeout();
	}

	// send intentions now
	CL_SendCmd();

	// resend a connection request if necessary
	CL_CheckForResend();

	CL_CheckCGameNativeImportIntegrity();

	// decide on the serverTime to render
	CL_SetCGameTime();

	oldViewYaw = cl.viewangles[YAW];
	oldViewPitch = cl.viewangles[PITCH];

	// update the screen
	cls.framecount++;
	SCR_UpdateScreen();

	if ( oldViewYaw != cl.viewangles[YAW] || oldViewPitch != cl.viewangles[PITCH] ) {
		CL_SetRetailClientMessageViewangleDeltaFlag();
	}

	// update audio
	S_Update( audioMsec );

	// advance local effects for next frame
	SCR_RunCinematic();

	Con_RunConsole();
}


//============================================================================

/*
================
CL_RefPrintf
================
*/
static void FORMAT_PRINTF(2, 3) QDECL CL_RefPrintf( printParm_t level, const char *fmt, ... ) {
	va_list		argptr;
	std::array<char, MAXPRINTMSG> msg;

	va_start( argptr, fmt );
	Q_vsnprintf( msg.data(), static_cast<int>( msg.size() ), fmt, argptr );
	va_end( argptr );

	switch ( level ) {
		default: Com_Printf( "%s", msg.data() ); break;
		case PRINT_DEVELOPER: Com_DPrintf( "%s", msg.data() ); break;
		case PRINT_WARNING: Com_Printf( S_COLOR_WARNING "%s", msg.data() ); break;
		case PRINT_ERROR: Com_Printf( S_COLOR_ERROR "%s", msg.data() ); break;
	}
}


/*
============
CL_ShutdownRef
============
*/
static void CL_ShutdownRef( refShutdownCode_t code ) {

#ifdef USE_RENDERER_DLOPEN
	if ( cl_renderer->modified ) {
		code = REF_UNLOAD_DLL;
	}
#endif

	// clear and mute all sounds until next registration
	// S_DisableSounds();

	if ( code >= REF_DESTROY_WINDOW ) { // +REF_UNLOAD_DLL
		// shutdown sound system before renderer
		// because it may depend from window handle
		S_Shutdown();
	}

	SCR_Done();

	if ( re.Shutdown ) {
		re.Shutdown( code );
	}
	CL_ClearAvatarImageHandles();

#ifdef USE_RENDERER_DLOPEN
	if ( rendererLib ) {
		Sys_UnloadLibrary( rendererLib );
		rendererLib = nullptr;

		// the glconfig_t fed back to the platform layer lived inside the
		// renderer module: make sure nothing dereferences it anymore
		GLimp_InvalidateConfig();
	}
#endif

	re = {};

	cls.rendererStarted = qfalse;
}


/*
============
CL_InitRenderer
============
*/
static void CL_InitRenderer( void ) {

	// fixup renderer -EC-
	if ( !re.BeginRegistration ) {
		CL_InitRef();
	}

	// Native window APIs can deliver resize messages synchronously while the
	// requested mode is being installed. They describe that mode rather than a
	// user resize and must not queue a second restart.
	cl_windowModeChange = qtrue;

	// this sets up the renderer and calls R_Init
	re.BeginRegistration( &cls.glconfig );
	cl_windowModeChange = qfalse;

	// load character sets
	cls.charSetShader = re.RegisterShader( "gfx/2d/bigchars" );
	cls.recordShader = re.RegisterShaderNoMip( "icons/record" );
	cls.whiteShader = re.RegisterShader( "white" );
	// pak00 contains two historical shaders named "console"; the earlier Q3
	// definition references textures not shipped by retail QL. Use FnQL's
	// uniquely named sidecar shader, which reproduces the complete QL variant.
	cls.consoleShader = re.RegisterShader( "fnql/console" );
	cls.cursorShader = re.RegisterShaderNoMip( "menu/art/3_cursor2" );

	Con_CheckResize();

	// for 640x480 virtualized screen
	cls.biasY = 0;
	cls.biasX = 0;
	if ( cls.glconfig.vidWidth * 480 > cls.glconfig.vidHeight * 640 ) {
		// wide screen, scale by height
		cls.scale = cls.glconfig.vidHeight * (1.0/480.0);
		cls.biasX = 0.5 * ( cls.glconfig.vidWidth - ( cls.glconfig.vidHeight * (640.0/480.0) ) );
	} else {
		// no wide screen, scale by width
		cls.scale = cls.glconfig.vidWidth * (1.0/640.0);
		cls.biasY = 0.5 * ( cls.glconfig.vidHeight - ( cls.glconfig.vidWidth * (480.0/640) ) );
	}

	SCR_Init();
}


/*
============================
CL_StartHunkUsers

After the server has cleared the hunk, these will need to be restarted
This is the only place that any of these functions are called from
============================
*/
void CL_StartHunkUsers( void ) {

	if ( !com_cl_running || !com_cl_running->integer ) {
		return;
	}

	if ( cls.state >= CA_LOADING ) {
		// try to apply map-depending configuration from cvar cl_mapConfig_<mapname> cvars
		const char *info = cl.gameState.stringData + cl.gameState.stringOffsets[ CS_SERVERINFO ];
		const char *mapname = Info_ValueForKey( info, "mapname" );
		if ( mapname && *mapname != '\0' ) {
			const char *fmt = "cl_mapConfig_%s";
			const char *cmd = Cvar_VariableString( va( fmt, mapname ) );
			if ( cmd && *cmd != '\0' ) {
				Cbuf_AddText( cmd );
				Cbuf_AddText( "\n" );
			} else {
				// apply mapname "default" if present
				cmd = Cvar_VariableString( va( fmt, "default" ) );
				if ( cmd && *cmd != '\0' ) {
					Cbuf_AddText( cmd );
					Cbuf_AddText( "\n" );
				}
			}
		}
	}

	if ( !cls.rendererStarted ) {
		cls.rendererStarted = qtrue;
		CL_InitRenderer();
	}

	if ( !cls.soundStarted ) {
		cls.soundStarted = qtrue;
		S_Init();
	}

	if ( !cls.soundRegistered ) {
		cls.soundRegistered = qtrue;
		S_BeginRegistration();
	}

	if ( !cls.uiStarted ) {
		cls.uiStarted = qtrue;
		CL_InitUI();
	}
}


/*
============
CL_RefMalloc
============
*/
static void *CL_RefMalloc( int size ) {
	return Z_TagMalloc( size, TAG_RENDERER );
}


/*
============
CL_RefFreeAll
============
*/
static void CL_RefFreeAll( void ) {
	Z_FreeTags( TAG_RENDERER );
}

#ifdef HUNK_DEBUG
static void *CL_HunkAllocDebugAdapter( int size, ha_pref preference, char *label, char *file, int line ) {
	return Hunk_AllocDebug( size, preference, label, file, line );
}
#endif


/*
============
CL_ScaledMilliseconds
============
*/
int CL_ScaledMilliseconds( void ) {
	if ( cls.realtime ) {
		return cls.realtime;
	}

	return Sys_Milliseconds();
}


/*
============
CL_IsMinimized
============
*/
static qboolean CL_IsMininized( void ) {
	return gw_minimized;
}


/*
============
CL_SetScaling

Sets console chars height
============
*/
static void CL_SetScaling( float factor, int captureWidth, int captureHeight ) {

	if ( cls.con_factor != factor ) {
		// rescale console
		con_scale->modified = qtrue;
	}

	cls.con_factor = factor;

	// set custom capture resolution
	cls.captureWidth = captureWidth;
	cls.captureHeight = captureHeight;
}

#ifdef USE_RENDERER_DLOPEN
static bool CL_RendererLoadFailureIsFatal( const char *rendererName )
{
	if ( rendererName && !Q_stricmp( rendererName, "glx" ) ) {
		return true;
	}

	return false;
}
#endif


/*
============
CL_InitRef
============
*/
static void CL_InitRef( void ) {
	refimport_t	rimp{};
	refexport_t	*ret;
#ifdef USE_RENDERER_DLOPEN
	GetRefAPI_t		GetRefAPI;
	std::array<char, MAX_OSPATH> dllName;
	char			*ospath;
	const char		*requestedRenderer;
#endif

	CL_InitGLimp_Cvars();

	Com_Printf( "----- Initializing Renderer ----\n" );

#ifdef USE_RENDERER_DLOPEN

#if defined (__linux__) && defined(__i386__)
#define REND_ARCH_STRING "x86"
#else
#define REND_ARCH_STRING ARCH_STRING
#endif

	requestedRenderer = cl_renderer->string;
	Com_sprintf( dllName.data(), static_cast<int>( dllName.size() ), RENDERER_PREFIX "_%s_" REND_ARCH_STRING DLL_EXT, requestedRenderer );
	ospath = FS_BuildOSPath( Sys_DefaultBasePath(), dllName.data(), nullptr );
	rendererLib = Sys_LoadLibrary( ospath );
	if ( !rendererLib )
	{
		if ( CL_RendererLoadFailureIsFatal( requestedRenderer ) ) {
			Com_Error( ERR_FATAL, "Failed to load requested GLx renderer %s; GLx load is fail-closed", dllName.data() );
		}

		Com_Printf( S_COLOR_YELLOW "WARNING: failed to load renderer %s; trying default renderer\n", dllName.data() );
		Cvar_ForceReset( "cl_renderer" );
		Com_sprintf( dllName.data(), static_cast<int>( dllName.size() ), RENDERER_PREFIX "_%s_" REND_ARCH_STRING DLL_EXT, cl_renderer->string );
		ospath = FS_BuildOSPath( Sys_DefaultBasePath(), dllName.data(), nullptr );
		rendererLib = Sys_LoadLibrary( ospath );
		if ( !rendererLib )
		{
			Com_Error( ERR_FATAL, "Failed to load renderer %s", dllName.data() );
		}
	}

	GetRefAPI = reinterpret_cast<GetRefAPI_t>( Sys_LoadFunction( rendererLib, "GetRefAPI" ) );
	if( !GetRefAPI )
	{
		Com_Error( ERR_FATAL, "Can't load symbol GetRefAPI" );
		return;
	}

	cl_renderer->modified = qfalse;
#endif

	rimp.Cmd_AddCommand = Cmd_AddCommand;
	rimp.Cmd_RemoveCommand = Cmd_RemoveCommand;
	rimp.Cmd_Argc = Cmd_Argc;
	rimp.Cmd_Argv = Cmd_Argv;
	rimp.Cmd_ExecuteText = Cbuf_ExecuteText;
	rimp.Printf = CL_RefPrintf;
	rimp.Error = Com_Error;
	rimp.Milliseconds = CL_ScaledMilliseconds;
	rimp.Microseconds = Sys_Microseconds;
	rimp.Malloc = CL_RefMalloc;
	rimp.FreeAll = CL_RefFreeAll;
	rimp.Free = Z_Free;
#ifdef HUNK_DEBUG
	rimp.Hunk_AllocDebug = CL_HunkAllocDebugAdapter;
#else
	rimp.Hunk_Alloc = Hunk_Alloc;
#endif
	rimp.Hunk_AllocateTempMemory = Hunk_AllocateTempMemory;
	rimp.Hunk_FreeTempMemory = Hunk_FreeTempMemory;

	rimp.CM_ClusterPVS = CM_ClusterPVS;
	rimp.CM_BoxTrace = CM_BoxTrace;
	rimp.CM_DrawDebugSurface = CM_DrawDebugSurface;

	rimp.FS_ReadFile = FS_ReadFile;
	rimp.FS_FreeFile = FS_FreeFile;
	rimp.FS_WriteFile = FS_WriteFile;
	rimp.FS_FreeFileList = FS_FreeFileList;
	rimp.FS_ListFiles = FS_ListFiles;
	//rimp.FS_FileIsInPAK = FS_FileIsInPAK;
	rimp.FS_FileExists = FS_FileExists;

	rimp.Cvar_Get = Cvar_Get;
	rimp.Cvar_Set = Cvar_Set;
	rimp.Cvar_SetValue = Cvar_SetValue;
	rimp.Cvar_CheckRange = Cvar_CheckRange;
	rimp.Cvar_SetDescription = Cvar_SetDescription;
	rimp.Cvar_VariableStringBuffer = Cvar_VariableStringBuffer;
	rimp.Cvar_VariableString = Cvar_VariableString;
	rimp.Cvar_VariableIntegerValue = Cvar_VariableIntegerValue;

	rimp.Cvar_SetGroup = Cvar_SetGroup;
	rimp.Cvar_CheckGroup = Cvar_CheckGroup;
	rimp.Cvar_ResetGroup = Cvar_ResetGroup;

	// cinematic stuff

	rimp.CIN_UploadCinematic = CIN_UploadCinematic;
	rimp.CIN_PlayCinematic = CIN_PlayCinematic;
	rimp.CIN_RunCinematic = CIN_RunCinematic;

	rimp.CL_WriteAVIVideoFrame = CL_WriteAVIVideoFrame;
	rimp.CL_SaveJPGToBuffer = CL_SaveJPGToBuffer;
	rimp.CL_SaveJPG = CL_SaveJPG;
	rimp.CL_LoadJPG = CL_LoadJPG;

	rimp.CL_IsMinimized = CL_IsMininized;
	rimp.CL_SetScaling = CL_SetScaling;

	rimp.Sys_SetClipboardBitmap = Sys_SetClipboardBitmap;
	rimp.Sys_LowPhysicalMemory = Sys_LowPhysicalMemory;
	rimp.Com_RealTime = Com_RealTime;
	rimp.SetClientMessageRendererNodeCount = CL_SetRetailClientMessageRendererNodeCount;
	rimp.PublishGameScreenshot = CL_WebView_PublishGameScreenshot;
	rimp.AdvertisementBridge_RefreshLoadingViewParameters = CL_AdvertisementBridge_RefreshLoadingViewParameters;
	rimp.AdvertisementBridge_GetCellDisplayState = CL_AdvertisementBridge_GetCellDisplayState;
	rimp.AdvertisementBridge_GetCellLabel = CL_AdvertisementBridge_GetCellLabel;
	rimp.AdvertisementBridge_GetLabelList1Count = CL_AdvertisementBridge_GetLabelList1Count;
	rimp.AdvertisementBridge_GetLabelList1Entry = CL_AdvertisementBridge_GetLabelList1Entry;
	rimp.AdvertisementBridge_GetLabelList2Count = CL_AdvertisementBridge_GetLabelList2Count;
	rimp.AdvertisementBridge_GetLabelList2Entry = CL_AdvertisementBridge_GetLabelList2Entry;

	rimp.GLimp_InitGamma = GLimp_InitGamma;
	rimp.GLimp_SetGamma = GLimp_SetGamma;
	rimp.GLimp_QueryDisplayOutput = GLimp_QueryDisplayOutput;

	// OpenGL API
#ifdef USE_OPENGL_API
	rimp.GLimp_Init = GLimp_Init;
	rimp.GLimp_Shutdown = GLimp_Shutdown;
	rimp.GL_GetProcAddress = GL_GetProcAddress;
	rimp.GLimp_EndFrame = GLimp_EndFrame;
#endif

	// Vulkan API
#ifdef USE_VULKAN_API
	rimp.VKimp_Init = VKimp_Init;
	rimp.VKimp_Shutdown = VKimp_Shutdown;
	rimp.VK_GetInstanceProcAddr = VK_GetInstanceProcAddr;
	rimp.VK_CreateSurface = VK_CreateSurface;
#endif

	ret = GetRefAPI( REF_API_VERSION, &rimp );

	Com_Printf( "-------------------------------\n");

	if ( !ret ) {
		Com_Error (ERR_FATAL, "Couldn't initialize refresh" );
	}

	re = *ret;

	// unpause so the cgame definitely gets a snapshot and renders a frame
	Cvar_Set( "cl_paused", "0" );
}


//===========================================================================================


static void CL_SetModel_f( void ) {
	const char *arg;
	std::array<char, MAX_CVAR_VALUE_STRING> name;

	arg = Cmd_Argv( 1 );
	if ( arg[0] ) {
		Cvar_Set( "model", arg );
		Cvar_Set( "headmodel", arg );
	} else {
		Cvar_VariableStringBuffer( "model", name.data(), static_cast<int>( name.size() ) );
		Com_Printf( "model is set to %s\n", name.data() );
	}
}


//===========================================================================================


/*
===============
CL_Video_f

video
video [filename]
===============
*/
static void CL_Video_f( void )
{
	std::array<char, MAX_OSPATH> filename;
	const char *ext;
	bool pipe;
	int i;

	if( !clc.demoplaying )
	{
		Com_Printf( "The %s command can only be used when playing back demos\n", Cmd_Argv( 0 ) );
		return;
	}

	pipe = Q_stricmp( Cmd_Argv( 0 ), "video-pipe" ) == 0;

	if ( pipe )
		ext = "mp4";
	else
		ext = "avi";

	if ( Cmd_Argc() == 2 )
	{
		// explicit filename
		Com_sprintf( filename.data(), static_cast<int>( filename.size() ), "videos/%s", Cmd_Argv( 1 ) );

		// override video file extension
		if ( pipe )
		{
			char *sep = strrchr( filename.data(), '/' ); // last path separator
			char *e = strrchr( filename.data(), '.' );

			if ( e && e > sep && *(e+1) != '\0' ) {
				ext = e + 1;
				*e = '\0';
			}
		}
	}
	else
	{
		 // scan for a free filename
		for ( i = 0; i <= 9999; i++ )
		{
			Com_sprintf( filename.data(), static_cast<int>( filename.size() ), "videos/video%04d.%s", i, ext );
			if ( !FS_FileExists( filename.data() ) )
				break; // file doesn't exist
		}

		if ( i > 9999 )
		{
			Com_Printf( S_COLOR_RED "ERROR: no free file names to create video\n" );
			return;
		}

		// without extension
		Com_sprintf( filename.data(), static_cast<int>( filename.size() ), "videos/video%04d", i );
	}


	clc.aviSoundFrameRemainder = 0.0f;
	clc.aviVideoFrameRemainder = 0.0f;

	Q_strncpyz( clc.videoName, filename.data(), sizeof( clc.videoName ) );
	clc.videoIndex = 0;

	CL_OpenAVIForWriting( va( "%s.%s", clc.videoName, ext ), ToQboolean( pipe ), qfalse );
}


/*
===============
CL_StopVideo_f
===============
*/
static void CL_StopVideo_f( void )
{
	CL_CloseAVI( qfalse );
}


/*
====================
CL_CompleteRecordName
====================
*/
static void CL_CompleteVideoName(const char *args, int argNum )
{
	if ( argNum == 2 )
	{
		Field_CompleteFilename( "videos", ".avi", qtrue, FS_MATCH_EXTERN | FS_MATCH_STICK );
	}
}


/*
===============
CL_GenerateQKey

test to see if a valid QKEY_FILE exists.  If one does not, try to generate
it by filling it with 2048 bytes of random data.
===============
*/
#ifdef USE_Q3KEY
static void CL_GenerateQKey()
{
	int len = 0;
	std::array<unsigned char, QKEY_SIZE> buff;

	ScopedFileHandle qkeyFile;
	len = OpenSvFileRead( QKEY_FILE, qkeyFile );
	qkeyFile.reset();
	if( len == QKEY_SIZE ) {
		Com_Printf( "QKEY found.\n" );
		return;
	}
	else {
		if( len > 0 ) {
			Com_Printf( "QKEY file size != %d, regenerating\n",
				QKEY_SIZE );
		}

		Com_Printf( "QKEY building random string\n" );
		Com_RandomBytes( buff.data(), static_cast<int>( buff.size() ) );

		ScopedFileHandle outFile( FS_SV_FOpenFileWrite( QKEY_FILE ) );
		if( !outFile ) {
			Com_Printf( "QKEY could not open %s for write\n",
				QKEY_FILE );
			return;
		}
		FileWrite( outFile.get(), buff.data(), buff.size() );
		Com_Printf( "QKEY generated\n" );
	}
}
#endif


/*
** CL_GetModeInfo
*/
struct vidmode_t {
	const char	*description;
	int			width, height;
	float		pixelAspect;		// pixel width / height
};

static constexpr std::array<vidmode_t, 25> cl_vidModes = {{
	{ "Mode  0: 320x240",			320,	240,	1 },
	{ "Mode  1: 400x300",			400,	300,	1 },
	{ "Mode  2: 512x384",			512,	384,	1 },
	{ "Mode  3: 640x480",			640,	480,	1 },
	{ "Mode  4: 800x600",			800,	600,	1 },
	{ "Mode  5: 960x720",			960,	720,	1 },
	{ "Mode  6: 1024x768",			1024,	768,	1 },
	{ "Mode  7: 1152x864",			1152,	864,	1 },
	{ "Mode  8: 1280x1024 (5:4)",	1280,	1024,	1 },
	{ "Mode  9: 1600x1200",			1600,	1200,	1 },
	{ "Mode 10: 2048x1536",			2048,	1536,	1 },
	{ "Mode 11: 856x480 (wide)",	856,	480,	1 },
	// extra modes:
	{ "Mode 12: 1280x960",			1280,	960,	1 },
	{ "Mode 13: 1280x720",			1280,	720,	1 },
	{ "Mode 14: 1280x800 (16:10)",	1280,	800,	1 },
	{ "Mode 15: 1366x768",			1366,	768,	1 },
	{ "Mode 16: 1440x900 (16:10)",	1440,	900,	1 },
	{ "Mode 17: 1600x900",			1600,	900,	1 },
	{ "Mode 18: 1680x1050 (16:10)",	1680,	1050,	1 },
	{ "Mode 19: 1920x1080",			1920,	1080,	1 },
	{ "Mode 20: 1920x1200 (16:10)",	1920,	1200,	1 },
	{ "Mode 21: 2560x1080 (21:9)",	2560,	1080,	1 },
	{ "Mode 22: 3440x1440 (21:9)",	3440,	1440,	1 },
	{ "Mode 23: 3840x2160",			3840,	2160,	1 },
	{ "Mode 24: 4096x2160 (4K)",	4096,	2160,	1 }
}};
static constexpr int s_numVidModes = static_cast<int>( cl_vidModes.size() );

qboolean CL_GetModeInfo( int *width, int *height, float *windowAspect, int mode, const char *modeFS, int dw, int dh, qboolean fullscreen )
{
	const	vidmode_t *vm;
	float	pixelAspect;

	// set dedicated fullscreen mode
	if ( fullscreen && *modeFS )
		mode = std::atoi( modeFS );

	if ( mode < -2 )
		return qfalse;

	if ( mode >= s_numVidModes )
		return qfalse;

	// fix unknown desktop resolution
	if ( mode == -2 && (dw == 0 || dh == 0) )
		mode = 3;

	if ( mode == -2 ) { // desktop resolution
		*width = dw;
		*height = dh;
		pixelAspect = r_customPixelAspect->value;
	} else if ( mode == -1 ) { // custom resolution
		*width = r_customwidth->integer;
		*height = r_customheight->integer;
		pixelAspect = r_customPixelAspect->value;
	} else { // predefined resolution
		vm = &cl_vidModes[ mode ];
		*width  = vm->width;
		*height = vm->height;
		pixelAspect = vm->pixelAspect;
	}

	*windowAspect = static_cast<float>( *width ) / ( *height * pixelAspect );

	return qtrue;
}


/*
** CL_ModeList_f
*/
static void CL_ModeList_f( void )
{
	int i;

	Com_Printf( "\n" );
	for ( i = 0; i < s_numVidModes; i++ )
	{
		Com_Printf( "%s\n", cl_vidModes[ i ].description );
	}
	Com_Printf( "\n" );
}


#ifdef USE_RENDERER_DLOPEN
static bool isValidRenderer( const char *s ) {
	while ( *s ) {
		if ( !((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '1' && *s <= '9')) )
			return false;
		++s;
	}
	return true;
}
#endif


static void CL_InitGLimp_Cvars( void )
{
	// shared with GLimp
	r_allowSoftwareGL = Cvar_Get( "r_allowSoftwareGL", "0", CVAR_LATCH );
	Cvar_SetDescription( r_allowSoftwareGL, "Toggle the use of the default software OpenGL driver supplied by the Operating System." );
	r_swapInterval = Cvar_Get( "r_swapInterval", "0", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( r_swapInterval, "V-blanks to wait before swapping buffers.\n 0: No V-Sync\n 1: Synced to the monitor's refresh rate." );
	r_glDriver = Cvar_Get( "r_glDriver", OPENGL_DRIVER_NAME, CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_SetDescription( r_glDriver, "Specifies the OpenGL driver to use, will revert back to default if driver name set is invalid." );

	r_displayRefresh = Cvar_Get( "r_displayRefresh", "0", CVAR_LATCH );
	Cvar_CheckRange( r_displayRefresh, "0", "500", CV_INTEGER );
	Cvar_SetDescription( r_displayRefresh, "Override monitor refresh rate in fullscreen mode:\n   0 - use current monitor refresh rate\n > 0 - use custom refresh rate" );

	vid_xpos = Cvar_Get( "vid_xpos", "3", CVAR_ARCHIVE );
	Cvar_CheckRange( vid_xpos, nullptr, nullptr, CV_INTEGER );
	Cvar_SetDescription( vid_xpos, "Saves/sets window X-coordinate when windowed, requires \\vid_restart." );
	vid_ypos = Cvar_Get( "vid_ypos", "22", CVAR_ARCHIVE );
	Cvar_CheckRange( vid_ypos, nullptr, nullptr, CV_INTEGER );
	Cvar_SetDescription( vid_ypos, "Saves/sets window Y-coordinate when windowed, requires \\vid_restart." );

	r_noborder = Cvar_Get( "r_noborder", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_CheckRange( r_noborder, "0", "1", CV_INTEGER );
	Cvar_SetDescription( r_noborder, "Setting to 1 will remove window borders and title bar in windowed mode, hold ALT to drag & drop it with opened console." );

	r_mode = Cvar_Get( "r_mode", "-2", CVAR_ARCHIVE | CVAR_LATCH );
	Cvar_CheckRange( r_mode, "-2", va( "%i", s_numVidModes-1 ), CV_INTEGER );
	Cvar_SetDescription( r_mode, "Set video mode:\n -2 - use current desktop resolution\n -1 - use \\r_customWidth and \\r_customHeight\n  0..N - enter \\modelist for details" );
#ifdef _DEBUG
	r_modeFullscreen = Cvar_Get( "r_modeFullscreen", "", CVAR_ARCHIVE | CVAR_LATCH );
#else
	r_modeFullscreen = Cvar_Get( "r_modeFullscreen", "-2", CVAR_ARCHIVE | CVAR_LATCH );
#endif
	Cvar_SetDescription( r_modeFullscreen, "Dedicated fullscreen mode, set to \"\" to use \\r_mode in all cases." );
	r_fullscreen = Cvar_Get( "r_fullscreen", "1", CVAR_ARCHIVE | CVAR_LATCH );
	Cvar_SetDescription( r_fullscreen, "Fullscreen mode. Set to 0 for windowed mode." );
	r_customPixelAspect = Cvar_Get( "r_customPixelAspect", "1", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_SetDescription( r_customPixelAspect, "Enables custom aspect of the screen, with \\r_mode -1." );
	r_customwidth = Cvar_Get( "r_customWidth", "1600", CVAR_ARCHIVE | CVAR_LATCH );
	Cvar_CheckRange( r_customwidth, "4", nullptr, CV_INTEGER );
	Cvar_SetDescription( r_customwidth, "Custom width to use with \\r_mode -1." );
	r_customheight = Cvar_Get( "r_customHeight", "1024", CVAR_ARCHIVE | CVAR_LATCH );
	Cvar_CheckRange( r_customheight, "4", nullptr, CV_INTEGER );
	Cvar_SetDescription( r_customheight, "Custom height to use with \\r_mode -1." );

	r_colorbits = Cvar_Get( "r_colorbits", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_CheckRange( r_colorbits, "0", "32", CV_INTEGER );
	Cvar_SetDescription( r_colorbits, "Sets color bit depth, set to 0 to use desktop settings." );

	// shared with renderer:
	cl_stencilbits = Cvar_Get( "r_stencilbits", "8", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_CheckRange( cl_stencilbits, "0", "8", CV_INTEGER );
	Cvar_SetDescription( cl_stencilbits, "Stencil buffer size, required to be 8 for stencil shadows." );
	cl_depthbits = Cvar_Get( "r_depthbits", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_CheckRange( cl_depthbits, "0", "32", CV_INTEGER );
	Cvar_SetDescription( cl_depthbits, "Sets precision of Z-buffer." );

	cl_drawBuffer = Cvar_Get( "r_drawBuffer", "GL_BACK", CVAR_CHEAT );
	Cvar_SetDescription( cl_drawBuffer, "Specifies buffer to draw from: GL_FRONT or GL_BACK." );
#ifdef USE_RENDERER_DLOPEN
#ifdef RENDERER_DEFAULT
	cl_renderer = Cvar_Get( "cl_renderer", XSTRING( RENDERER_DEFAULT ), CVAR_ARCHIVE | CVAR_LATCH );
#else
	cl_renderer = Cvar_Get( "cl_renderer", "vulkan", CVAR_ARCHIVE | CVAR_LATCH );
#endif
	Cvar_SetDescription( cl_renderer, "Sets your desired renderer, requires \\vid_restart." );

	if ( !isValidRenderer( cl_renderer->string ) ) {
		Cvar_ForceReset( "cl_renderer" );
	}
#endif
}


/*
====================
CL_Init
====================
*/
void CL_Init( void ) {
	const char *s;
	cvar_t *cv;

	Com_Printf( "----- Client Initialization -----\n" );

	Con_Init();

	CL_ClearState();
	cls.state = CA_DISCONNECTED;	// no longer CA_UNINITIALIZED

	CL_ResetOldGame();

	cls.realtime = 0;

	CL_InitInput();

	//
	// register client variables
	//
	cl_noprint = Cvar_Get( "cl_noprint", "0", 0 );
	Cvar_SetDescription( cl_noprint, "Disable printing of information in the console." );
	cl_motd = Cvar_Get( "cl_motd", "1", 0 );
	Cvar_SetDescription( cl_motd, "Toggle the display of the 'Message of the day'. When Quake 3 Arena starts a map up, it sends the GL_RENDERER string to the Message Of The Day server at id. This responds back with a message of the day to the client." );

	cl_timeout = Cvar_Get( "cl_timeout", "200", 0 );
	Cvar_CheckRange( cl_timeout, "5", nullptr, CV_INTEGER );
	Cvar_SetDescription( cl_timeout, "Duration of receiving nothing from server for client to decide it must be disconnected (in seconds)." );

	cl_autoNudge = Cvar_Get( "cl_autoNudge", "0", CVAR_TEMP );
	Cvar_CheckRange( cl_autoNudge, "0", "1", CV_FLOAT );
	Cvar_SetDescription( cl_autoNudge, "Automatic time nudge that uses your average ping as the time nudge, values:\n  0 - use fixed \\cl_timeNudge\n (0..1] - factor of median average ping to use as timenudge\n" );
	cl_timeNudge = Cvar_Get( "cl_timeNudge", "0", CVAR_TEMP );
	Cvar_CheckRange( cl_timeNudge, "-30", "30", CV_INTEGER );
	Cvar_SetDescription( cl_timeNudge, "Allows more or less latency to be added in the interest of better smoothness or better responsiveness." );

	cl_shownet = Cvar_Get ("cl_shownet", "0", CVAR_TEMP );
	Cvar_SetDescription( cl_shownet, "Toggle the display of current network status." );
	cl_showTimeDelta = Cvar_Get ("cl_showTimeDelta", "0", CVAR_TEMP );
	Cvar_SetDescription( cl_showTimeDelta, "Prints the time delta of each packet to the console (the time delta between server updates)." );
	rcon_client_password = Cvar_Get ("rconPassword", "", CVAR_TEMP | CVAR_PRIVATE );
	Cvar_SetDescription( rcon_client_password, "Sets a remote console password so clients may change server settings without direct access to the server console." );
	cl_activeAction = Cvar_Get( "activeAction", "", CVAR_TEMP );
	Cvar_SetDescription( cl_activeAction, "Contents of this variable will be executed upon first frame of play.\nNote: It is cleared every time it is executed." );

	cl_autoRecordDemo = Cvar_Get ("cl_autoRecordDemo", "0", CVAR_ARCHIVE);
	Cvar_SetDescription( cl_autoRecordDemo, "Auto-record demos when starting or joining a game." );
	cl_freezeDemo = Cvar_Get( "cl_freezeDemo", "0", CVAR_TEMP );
	Cvar_SetDescription( cl_freezeDemo, "Hold demo simulation time while preserving client input and UI frame timing." );
	cl_drawRecording = Cvar_Get("cl_drawRecording", "1", CVAR_ARCHIVE);
	Cvar_SetDescription( cl_drawRecording, "Demo recording indicator: hidden (0), detailed (1), or compact REC icon (2)." );
	cl_menuAspect = Cvar_Get( "cl_menuAspect", "1", CVAR_ARCHIVE );
	Cvar_CheckRange( cl_menuAspect, "0", "1", CV_INTEGER );
	Cvar_SetDescription( cl_menuAspect,
		"Menu aspect correction:\n"
		" 1 - retain the retail QL centered 4:3 menu layout (default)\n"
		" 0 - stretch menu widgets, including 3D model viewports, to the framebuffer" );
	cl_menuDepthOfField = Cvar_Get( "cl_menuDepthOfField", "0", CVAR_ARCHIVE );
	Cvar_CheckRange( cl_menuDepthOfField, "0", "1", CV_FLOAT );
	Cvar_SetDescription( cl_menuDepthOfField,
		"Depth-of-field strength for the live game view behind the in-game menu. 0 disables the effect; requires renderer support." );
	cl_menuDepthOfFieldTime = Cvar_Get( "cl_menuDepthOfFieldTime", "160", CVAR_ARCHIVE );
	Cvar_CheckRange( cl_menuDepthOfFieldTime, "0", "1000", CV_INTEGER );
	Cvar_SetDescription( cl_menuDepthOfFieldTime,
		"Milliseconds used to fade the in-game menu depth-of-field effect in and out." );
	cl_cinematicAspect = Cvar_Get( "cl_cinematicAspect", "1", CVAR_ARCHIVE );
	Cvar_CheckRange( cl_cinematicAspect, "0", "1", CV_INTEGER );
	Cvar_SetDescription( cl_cinematicAspect,
		"Cinematic aspect correction:\n"
		" 0 - stretch UI and fullscreen cinematics to the framebuffer\n"
		" 1 - keep UI and fullscreen cinematics in centered 4:3 space" );
	cl_captureActive = Cvar_Get( "cl_captureActive", "0", CVAR_TEMP | CVAR_PRIVATE | CVAR_NORESTART | CVAR_NOTABCOMPLETE );
	Cvar_SetDescription( cl_captureActive, "Internal capture flag used while screenshot levelshot and cubemap frames are being rendered." );
	r_levelshotHideHud = Cvar_Get( "r_levelshotHideHud", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( r_levelshotHideHud, "0", "1", CV_INTEGER );
	Cvar_SetDescription( r_levelshotHideHud, "Hide the HUD while rendering screenshot levelshot and cubemap capture frames. When disabled, cube maps keep HUD only on the front face." );
	r_levelshotHideViewWeapon = Cvar_Get( "r_levelshotHideViewWeapon", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( r_levelshotHideViewWeapon, "0", "1", CV_INTEGER );
	Cvar_SetDescription( r_levelshotHideViewWeapon, "Hide first-person weapon models while rendering screenshot levelshot and cubemap capture frames. When disabled, cube maps keep the view weapon only on the front face." );
	cl_playerHighlight = Cvar_Get( "cl_playerHighlight", "0", CVAR_ARCHIVE );
	Cvar_CheckRange( cl_playerHighlight, "0", "3", CV_INTEGER );
	Cvar_SetDescription( cl_playerHighlight,
		"Optional player highlight effect bitmask:\n"
		" 0 - disabled\n"
		" 1 - rimlight only\n"
		" 2 - stencil border only\n"
		" 3 - rimlight and stencil border\n"
		"Team modes tint red and blue teams separately unless teammate/enemy overrides are set; non-team modes use the free color.\n"
		"Self and corpses are excluded." );
	cl_playerHighlightRimIntensity = Cvar_Get( "cl_playerHighlightRimIntensity", "2.0", CVAR_ARCHIVE );
	Cvar_CheckRange( cl_playerHighlightRimIntensity, "0.0", "2.0", CV_FLOAT );
	Cvar_SetDescription( cl_playerHighlightRimIntensity, "Overall rimlight opacity multiplier for player highlighting; 0 disables the rimlight pass and 1 preserves the default strength." );
	cl_playerHighlightOutlineIntensity = Cvar_Get( "cl_playerHighlightOutlineIntensity", "2.0", CVAR_ARCHIVE );
	Cvar_CheckRange( cl_playerHighlightOutlineIntensity, "0.0", "2.0", CV_FLOAT );
	Cvar_SetDescription( cl_playerHighlightOutlineIntensity, "Overall stencil border opacity multiplier for player highlighting; 0 disables the outline pass and 1 preserves the default strength." );
	cl_playerHighlightOutlineScale = Cvar_Get( "cl_playerHighlightOutlineScale", "1.01", CVAR_ARCHIVE );
	Cvar_CheckRange( cl_playerHighlightOutlineScale, "1.001", "1.250", CV_FLOAT );
	Cvar_SetDescription( cl_playerHighlightOutlineScale, "Relative thickness factor applied to the highlight stencil border shell." );
	cl_playerHighlightRedColor = Cvar_Get( "cl_playerHighlightRedColor", "255 32 0", CVAR_ARCHIVE );
	Cvar_SetDescription( cl_playerHighlightRedColor, "Base highlight color for red-team players as 'R G B [A]' in 0-255 space." );
	cl_playerHighlightBlueColor = Cvar_Get( "cl_playerHighlightBlueColor", "0 32 255", CVAR_ARCHIVE );
	Cvar_SetDescription( cl_playerHighlightBlueColor, "Base highlight color for blue-team players as 'R G B [A]' in 0-255 space." );
	cl_playerHighlightFreeColor = Cvar_Get( "cl_playerHighlightFreeColor", "255 32 0", CVAR_ARCHIVE );
	Cvar_SetDescription( cl_playerHighlightFreeColor, "Base highlight color for non-team player modes as 'R G B [A]' in 0-255 space." );
	cl_playerHighlightEnemyColor = Cvar_Get( "cl_playerHighlightEnemyColor", "", CVAR_ARCHIVE );
	Cvar_SetDescription( cl_playerHighlightEnemyColor, "Override enemy highlight color as 'R G B [A]' in 0-255 space. Leave blank to use team/free colors." );
	cl_playerHighlightTeammateColor = Cvar_Get( "cl_playerHighlightTeammateColor", "", CVAR_ARCHIVE );
	Cvar_SetDescription( cl_playerHighlightTeammateColor, "Override teammate highlight color as 'R G B [A]' in 0-255 space. Leave blank to use team colors." );
	CL_MigrateLegacyPlayerHighlightCvar( cl_playerHighlight, "cl_enemyHighlight", "0" );
	CL_MigrateLegacyPlayerHighlightCvar( cl_playerHighlightOutlineScale, "cl_enemyHighlightOutlineScale", "1.01" );
	CL_MigrateLegacyPlayerHighlightCvar( cl_playerHighlightRedColor, "cl_enemyHighlightRedColor", "208 96 96" );
	CL_MigrateLegacyPlayerHighlightCvar( cl_playerHighlightBlueColor, "cl_enemyHighlightBlueColor", "96 144 224" );
	CL_MigrateLegacyPlayerHighlightCvar( cl_playerHighlightFreeColor, "cl_enemyHighlightFreeColor", "208 96 96" );
	cl_aviFrameRate = Cvar_Get ("cl_aviFrameRate", "25", CVAR_ARCHIVE);
	Cvar_CheckRange( cl_aviFrameRate, "1", "1000", CV_INTEGER );
	Cvar_SetDescription( cl_aviFrameRate, "The framerate used for capturing video." );
	cl_aviMotionJpeg = Cvar_Get ("cl_aviMotionJpeg", "1", CVAR_ARCHIVE);
	Cvar_SetDescription( cl_aviMotionJpeg, "Enable/disable the MJPEG codec for avi output." );
	cl_forceavidemo = Cvar_Get ("cl_forceavidemo", "0", 0);
	Cvar_SetDescription( cl_forceavidemo, "Forces all demo recording into a sequence of screenshots in TGA format." );

	cl_aviPipeFormat = Cvar_Get( "cl_aviPipeFormat",
		"-preset medium -crf 23 -c:v libx264 -flags +cgop -pix_fmt yuvj420p "
		"-bf 2 -c:a aac -strict -2 -b:a 160k -movflags faststart",
		CVAR_ARCHIVE );
	Cvar_SetDescription( cl_aviPipeFormat, "Encoder parameters used for \\video-pipe." );

	rconAddress = Cvar_Get ("rconAddress", "", 0);
	Cvar_SetDescription( rconAddress, "The IP address of the remote console you wish to connect to." );

	cl_allowDownload = Cvar_Get( "cl_allowDownload", "1", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( cl_allowDownload, "Enables downloading of content needed in server. Valid bitmask flags:\n 1: Downloading enabled\n 2: Do not use HTTP/FTP downloads\n 4: Do not use UDP downloads" );
#ifdef USE_CURL
	cl_mapAutoDownload = Cvar_Get( "cl_mapAutoDownload", "0", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( cl_mapAutoDownload, "Automatic map download for play and demo playback (via automatic \\dlmap call)." );
#ifdef USE_CURL_DLOPEN
	cl_cURLLib = Cvar_Get( "cl_cURLLib", DEFAULT_CURL_LIB, 0 );
	Cvar_SetDescription( cl_cURLLib, "Filename of cURL library to load." );
#endif
#endif

	cl_conXOffset = Cvar_Get ("cl_conXOffset", "0", 0);
	Cvar_SetDescription( cl_conXOffset, "Console notifications X-offset." );
	cl_conColor = Cvar_Get( "cl_conColor", "", 0 );
	Cvar_SetDescription( cl_conColor, "Console background color, set as R G B A values from 0-255, use with \\seta to save in config." );

#ifdef MACOS_X
	// In game video is REALLY slow in Mac OS X right now due to driver slowness
	cl_inGameVideo = Cvar_Get( "r_inGameVideo", "0", CVAR_ARCHIVE_ND );
#else
	cl_inGameVideo = Cvar_Get( "r_inGameVideo", "1", CVAR_ARCHIVE_ND );
#endif
	Cvar_SetDescription( cl_inGameVideo, "Controls whether in-game video should be drawn." );

	cl_serverStatusResendTime = Cvar_Get ("cl_serverStatusResendTime", "750", 0);
	Cvar_SetDescription( cl_serverStatusResendTime, "Time between re-sending server status requests if no response is received (in milliseconds)." );

	// init cg_autoswitch so the ui will have it correctly even
	// if the cgame hasn't been started
	Cvar_Get ("cg_autoswitch", "0", CVAR_ARCHIVE);

	cl_motdString = Cvar_Get( "cl_motdString", "", CVAR_ROM );
	Cvar_SetDescription( cl_motdString, "Message of the day string from id's master server, it is a read only variable." );

	cv = Cvar_Get( "cl_maxPing", "800", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( cv, "100", "999", CV_INTEGER );
	Cvar_SetDescription( cv, "Specify the maximum allowed ping to a server." );

	cl_lanForcePackets = Cvar_Get( "cl_lanForcePackets", "1", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( cl_lanForcePackets, "Bypass \\cl_maxpackets for LAN games, send packets every frame." );

	cl_guidServerUniq = Cvar_Get( "cl_guidServerUniq", "1", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( cl_guidServerUniq, "Makes cl_guid unique for each server." );

	cl_dlURL = Cvar_Get( "cl_dlURL", "http://ws.q3df.org/maps/download/%1", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( cl_dlURL, "Cvar must point to download location." );

	cl_dlDirectory = Cvar_Get( "cl_dlDirectory", "0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( cl_dlDirectory, "0", "1", CV_INTEGER );
	s = va( "Save downloads initiated by \\dlmap and \\download commands in:\n"
		" 0 - current game directory\n"
		" 1 - basegame (%s) directory\n", FS_GetBaseGameDir() );
	Cvar_SetDescription( cl_dlDirectory, s );

	cl_reconnectArgs = Cvar_Get( "cl_reconnectArgs", "", CVAR_ARCHIVE_ND | CVAR_NOTABCOMPLETE );

	// userinfo
	Cvar_Get ("name", "UnnamedPlayer", CVAR_USERINFO | CVAR_ARCHIVE_ND );
	Cvar_Get ("rate", "25000", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get ("snaps", "40", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get ("model", "sarge", CVAR_USERINFO | CVAR_ARCHIVE_ND );
	Cvar_Get ("headmodel", "sarge", CVAR_USERINFO | CVAR_ARCHIVE_ND );
 	Cvar_Get ("team_model", "sarge", CVAR_USERINFO | CVAR_ARCHIVE_ND );
	Cvar_Get ("team_headmodel", "sarge", CVAR_USERINFO | CVAR_ARCHIVE_ND );
//	Cvar_Get ("g_redTeam", "Stroggs", CVAR_SERVERINFO | CVAR_ARCHIVE);
//	Cvar_Get ("g_blueTeam", "Pagans", CVAR_SERVERINFO | CVAR_ARCHIVE);
	Cvar_Get ("color1", "4", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get ("color2", "5", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get ("handicap", "100", CVAR_USERINFO | CVAR_ARCHIVE_ND );
//	Cvar_Get ("teamtask", "0", CVAR_USERINFO );
	Cvar_Get ("sex", "male", CVAR_USERINFO | CVAR_ARCHIVE_ND );
	Cvar_Get ("cl_anonymous", "0", CVAR_USERINFO | CVAR_ARCHIVE_ND );

	Cvar_Get ("password", "", CVAR_USERINFO | CVAR_NORESTART);
	Cvar_Get ("cg_predictItems", "1", CVAR_USERINFO | CVAR_ARCHIVE );


	// cgame might not be initialized before menu is used
	Cvar_Get ("cg_viewsize", "100", CVAR_ARCHIVE_ND );
	Cvar_Get ("cg_stereoSeparation", "0.4", CVAR_ARCHIVE);

	CL_WebHost_Init();
	CL_Workshop_Init();

	//
	// register client commands
	//
	Cmd_AddCommand ("cmd", CL_ForwardToServer_f);
	Cmd_AddCommand ("configstrings", CL_Configstrings_f);
	Cmd_AddCommand ("clientinfo", CL_Clientinfo_f);
	Cmd_AddCommand ("snd_restart", CL_Snd_Restart_f);
	Cmd_AddCommand ("vid_restart", CL_Vid_Restart_f);
#ifdef USE_RENDERER_DLOPEN
	Cmd_AddCommand ("renderer_switch", CL_RendererSwitch_f);
#endif
	Cmd_AddCommand ("disconnect", CL_Disconnect_f);
	Cmd_AddCommand( "togglemenu", CL_ToggleMenu_f );
	Cmd_AddCommand ("record", CL_Record_f);
	Cmd_SetCommandCompletionFunc( "record", CL_CompleteRecordName );
	Cmd_AddCommand ("demo", CL_PlayDemo_f);
	Cmd_SetCommandCompletionFunc( "demo", CL_CompleteDemoName );
	Cmd_AddCommand ("cinematic", CL_PlayCinematic_f);
	Cmd_AddCommand ("stoprecord", CL_StopRecord_f);
	Cmd_AddCommand ("connect", CL_Connect_f);
	Cmd_AddCommand ("reconnect", CL_Reconnect_f);
	Cmd_AddCommand ("localservers", CL_LocalServers_f);
	Cmd_AddCommand ("globalservers", CL_GlobalServers_f);
	Cmd_AddCommand ("rcon", CL_Rcon_f);
	Cmd_SetCommandCompletionFunc( "rcon", CL_CompleteRcon );
	Cmd_AddCommand ("ping", CL_Ping_f );
	Cmd_AddCommand ("serverstatus", CL_ServerStatus_f );
	Cmd_AddCommand ("showip", CL_ShowIP_f );
	Cmd_AddCommand ("fs_openedList", CL_OpenedPK3List_f );
	Cmd_AddCommand ("fs_referencedList", CL_ReferencedPK3List_f );
	Cmd_AddCommand ("model", CL_SetModel_f );
	Cmd_AddCommand ("video", CL_Video_f );
	Cmd_AddCommand ("video-pipe", CL_Video_f );
	Cmd_SetCommandCompletionFunc( "video", CL_CompleteVideoName );
	Cmd_AddCommand ("stopvideo", CL_StopVideo_f );
	Cmd_AddCommand ("serverinfo", CL_Serverinfo_f );
	Cmd_AddCommand ("systeminfo", CL_Systeminfo_f );

#ifdef USE_CURL
	Cmd_AddCommand( "download", CL_Download_f );
	Cmd_AddCommand( "dlmap", CL_Download_f );
	Cmd_SetCommandCompletionFunc( "dlmap", CL_CompleteDownloadMap );
#endif
	Cmd_AddCommand( "modelist", CL_ModeList_f );
	QLWebHost_RegisterCommands();
	CL_WebPak_Init();
	CL_WebHost_BootstrapAwesomiumMenu();

	Cvar_Set( "cl_running", "1" );
#ifdef USE_MD5
	CL_GenerateQKey();
#endif
	Cvar_Get( "cl_guid", "", CVAR_USERINFO | CVAR_ROM | CVAR_PROTECTED );
	CL_UpdateGUID( nullptr, 0 );

	Com_Printf( "----- Client Initialization Complete -----\n" );
}


/*
===============
CL_Shutdown

Called on fatal error, quit and dedicated mode switch
===============
*/
void CL_Shutdown( const char *finalmsg, qboolean quit ) {
	static bool recursive = false;

	// check whether the client is running at all.
	if ( !( com_cl_running && com_cl_running->integer ) )
		return;

	Com_Printf( "----- Client Shutdown (%s) -----\n", finalmsg );

	if ( recursive ) {
		Com_Printf( "WARNING: Recursive CL_Shutdown()\n" );
		return;
	}
	recursive = true;

	noGameRestart = quit != qfalse;
	CL_Disconnect( qfalse );
	CL_Workshop_Shutdown();
	CL_WebHost_Shutdown();
	CL_WebPak_Shutdown();

	// clear and mute all sounds until next registration
	S_DisableSounds();

	CL_ShutdownVMs();

	CL_ShutdownRef( quit ? REF_UNLOAD_DLL : REF_DESTROY_WINDOW );

	Con_Shutdown();

	Cmd_RemoveCommand ("cmd");
	Cmd_RemoveCommand ("configstrings");
	Cmd_RemoveCommand ("userinfo");
	Cmd_RemoveCommand ("clientinfo");
	Cmd_RemoveCommand ("snd_restart");
	Cmd_RemoveCommand ("vid_restart");
#ifdef USE_RENDERER_DLOPEN
	Cmd_RemoveCommand ("renderer_switch");
#endif
	Cmd_RemoveCommand ("disconnect");
	Cmd_RemoveCommand ("record");
	Cmd_RemoveCommand ("demo");
	Cmd_RemoveCommand ("cinematic");
	Cmd_RemoveCommand ("stoprecord");
	Cmd_RemoveCommand ("connect");
	Cmd_RemoveCommand ("reconnect");
	Cmd_RemoveCommand ("localservers");
	Cmd_RemoveCommand ("globalservers");
	Cmd_RemoveCommand ("rcon");
	Cmd_RemoveCommand ("ping");
	Cmd_RemoveCommand ("serverstatus");
	Cmd_RemoveCommand ("showip");
	Cmd_RemoveCommand ("fs_openedList");
	Cmd_RemoveCommand ("fs_referencedList");
	Cmd_RemoveCommand ("model");
	Cmd_RemoveCommand ("video");
	Cmd_RemoveCommand ("stopvideo");
	Cmd_RemoveCommand ("serverinfo");
	Cmd_RemoveCommand ("systeminfo");
	Cmd_RemoveCommand ("modelist");
	QLWebHost_UnregisterCommands();

#ifdef USE_CURL
	Com_DL_Cleanup( &download );

	Cmd_RemoveCommand( "download" );
	Cmd_RemoveCommand( "dlmap" );
#endif

	CL_ClearInput();

	Cvar_Set( "cl_running", "0" );

	recursive = false;

	// clientStatic_t contains the full retail server browser arrays and is close
	// to one MiB on Win32. Keep shutdown independent of compiler temporary size.
	Com_Memset( &cls, 0, sizeof( cls ) );
	Key_SetCatcher( 0 );
	Com_Printf( "-----------------------\n" );
}


static void CL_SetServerInfo(serverInfo_t *server, const char *info, int ping) {
	if (server) {
		if (info) {
			server->clients = std::atoi( Info_ValueForKey( info, "clients" ) );
			Q_strncpyz(server->hostName,Info_ValueForKey(info, "hostname"), MAX_NAME_LENGTH);
			Q_strncpyz(server->mapName, Info_ValueForKey(info, "mapname"), MAX_NAME_LENGTH);
			server->maxClients = std::atoi( Info_ValueForKey( info, "sv_maxclients" ) );
			Q_strncpyz(server->game,Info_ValueForKey(info, "game"), MAX_NAME_LENGTH);
			server->gameType = std::atoi( Info_ValueForKey( info, "gametype" ) );
			server->netType = std::atoi( Info_ValueForKey( info, "nettype" ) );
			server->minPing = std::atoi( Info_ValueForKey( info, "minping" ) );
			server->maxPing = std::atoi( Info_ValueForKey( info, "maxping" ) );
			server->punkbuster = std::atoi( Info_ValueForKey( info, "punkbuster" ) );
			server->g_humanplayers = std::atoi( Info_ValueForKey( info, "g_humanplayers" ) );
			server->g_needpass = std::atoi( Info_ValueForKey( info, "g_needpass" ) );
		}
		server->ping = ping;
	}
}

static ping_t *CL_GetPingSlot( int index ) {
	if ( index < 0 || static_cast<std::size_t>( index ) >= cl_pinglist.size() ) {
		return nullptr;
	}

	return &cl_pinglist[index];
}


static void CL_SetServerInfoByAddress(const netadr_t *from, const char *info, int ping) {
	int i;

	for (i = 0; i < MAX_OTHER_SERVERS; i++) {
		if (NET_CompareAdr(from, &cls.localServers[i].adr) ) {
			CL_SetServerInfo(&cls.localServers[i], info, ping);
		}
	}

	for (i = 0; i < MAX_GLOBAL_SERVERS; i++) {
		if (NET_CompareAdr(from, &cls.globalServers[i].adr)) {
			CL_SetServerInfo(&cls.globalServers[i], info, ping);
		}
	}

	for (i = 0; i < MAX_OTHER_SERVERS; i++) {
		if (NET_CompareAdr(from, &cls.favoriteServers[i].adr)) {
			CL_SetServerInfo(&cls.favoriteServers[i], info, ping);
		}
	}
}


/*
===================
CL_ServerInfoPacket
===================
*/
static void CL_ServerInfoPacket( const netadr_t *from, msg_t *msg ) {
	int		i, type, len;
	std::array<char, MAX_INFO_STRING> info;
	const char *infoString;
	int		prot;

	infoString = MSG_ReadString( msg );

	// if this isn't the correct protocol version, ignore it
	prot = std::atoi( Info_ValueForKey( infoString, "protocol" ) );
	if ( prot != OLD_PROTOCOL_VERSION && prot != NEW_PROTOCOL_VERSION && prot != com_protocol->integer ) {
		Com_DPrintf( "Different protocol info packet: %s\n", infoString );
		return;
	}

	// iterate servers waiting for ping response
	for ( ping_t& ping : cl_pinglist )
	{
		if ( ping.adr.port && !ping.time && NET_CompareAdr( from, &ping.adr ) )
		{
			// calc ping time
			ping.time = Sys_Milliseconds() - ping.start;
			if ( ping.time < 1 )
			{
				ping.time = 1;
			}
			if ( com_developer->integer )
			{
				Com_Printf( "ping time %dms from %s\n", ping.time, NET_AdrToString( from ) );
			}

			// save of info
			Q_strncpyz( ping.info, infoString, sizeof( ping.info ) );

			// tack on the net type
			// NOTE: make sure these types are in sync with the netnames strings in the UI
			switch (from->type)
			{
				case NA_BROADCAST:
				case NA_IP:
					type = 1;
					break;
#ifdef USE_IPV6
				case NA_IP6:
					type = 2;
					break;
#endif
				default:
					type = 0;
					break;
			}

			Info_SetValueForKey( ping.info, "nettype", va( "%d", type ) );
			CL_SetServerInfoByAddress( from, infoString, ping.time );
			CL_WebHost_OnServerInfoResponse( from, infoString, ping.time );

			return;
		}
	}

	// if not just sent a local broadcast or pinging local servers
	if (cls.pingUpdateSource != AS_LOCAL) {
		return;
	}

	for ( i = 0 ; i < MAX_OTHER_SERVERS ; i++ ) {
		// empty slot
		if ( cls.localServers[i].adr.port == 0 ) {
			break;
		}

		// avoid duplicate
		if ( NET_CompareAdr( from, &cls.localServers[i].adr ) ) {
			return;
		}
	}

	if ( i == MAX_OTHER_SERVERS ) {
		Com_DPrintf( "MAX_OTHER_SERVERS hit, dropping infoResponse\n" );
		return;
	}

	// add this to the list
	cls.numlocalservers = i+1;
	CL_InitServerInfo( &cls.localServers[i], from );

	Q_strncpyz( info.data(), MSG_ReadString( msg ), static_cast<int>( info.size() ) );
	len = static_cast<int>( strlen( info.data() ) );
	if ( len > 0 ) {
		if ( info[ len-1 ] == '\n' ) {
			info[ len-1 ] = '\0';
		}
		Com_Printf( "%s: %s\n", NET_AdrToStringwPort( from ), info.data() );
	}
}


/*
===================
CL_GetServerStatus
===================
*/
static serverStatus_t *CL_GetServerStatus( const netadr_t *from ) {
	std::size_t oldest;
	int oldestTime;

	for ( serverStatus_t& status : cl_serverStatusList ) {
		if ( NET_CompareAdr( from, &status.address ) ) {
			return &status;
		}
	}
	for ( serverStatus_t& status : cl_serverStatusList ) {
		if ( status.retrieved ) {
			return &status;
		}
	}
	oldest = 0;
	oldestTime = cl_serverStatusList[oldest].startTime;
	for ( std::size_t i = 1; i < cl_serverStatusList.size(); i++ ) {
		if ( cl_serverStatusList[i].startTime < oldestTime ) {
			oldest = i;
			oldestTime = cl_serverStatusList[i].startTime;
		}
	}
	return &cl_serverStatusList[oldest];
}


/*
===================
CL_ServerStatus
===================
*/
int CL_ServerStatus( const char *serverAddress, char *serverStatusString, int maxLen ) {
	netadr_t	to{};
	serverStatus_t *serverStatus;

	// if no server address then reset all server status requests
	if ( !serverAddress ) {
		for ( serverStatus_t& status : cl_serverStatusList ) {
			status.address.port = 0;
			status.retrieved = true;
		}
		return qfalse;
	}
	// get the address
	if ( !NET_StringToAdr( serverAddress, &to, NA_UNSPEC ) ) {
		return qfalse;
	}
	serverStatus = CL_GetServerStatus( &to );
	// if no server status string then reset the server status request for this address
	if ( !serverStatusString ) {
		serverStatus->retrieved = true;
		return qfalse;
	}

	// if this server status request has the same address
	if ( NET_CompareAdr( &to, &serverStatus->address) ) {
		// if we received a response for this server status request
		if (!serverStatus->pending) {
			Q_strncpyz(serverStatusString, serverStatus->string.data(), maxLen);
			serverStatus->retrieved = true;
			serverStatus->startTime = 0;
			return qtrue;
		}
		// resend the request regularly
		else if ( Sys_Milliseconds() - serverStatus->startTime > cl_serverStatusResendTime->integer ) {
			serverStatus->print = false;
			serverStatus->pending = true;
			serverStatus->retrieved = false;
			serverStatus->time = 0;
			serverStatus->startTime = Sys_Milliseconds();
			NET_OutOfBandPrint( NS_CLIENT, &to, "getstatus" );
			return qfalse;
		}
	}
	// if retrieved
	else if ( serverStatus->retrieved ) {
		serverStatus->address = to;
		serverStatus->print = false;
		serverStatus->pending = true;
		serverStatus->retrieved = false;
		serverStatus->startTime = Sys_Milliseconds();
		serverStatus->time = 0;
		NET_OutOfBandPrint( NS_CLIENT, &to, "getstatus" );
		return qfalse;
	}
	return qfalse;
}


/*
===================
CL_ServerStatusResponse
===================
*/
static void CL_ServerStatusResponse( const netadr_t *from, msg_t *msg ) {
	const char	*s;
	std::array<char, MAX_INFO_STRING> info;
	std::array<char, 64> buf;
	char	*v[2];
	int		i, l, score, ping;
	int		len;
	qboolean publishBrowserDetails;
	serverStatus_t *serverStatus;

	serverStatus = nullptr;
	for ( serverStatus_t& status : cl_serverStatusList ) {
		if ( NET_CompareAdr( from, &status.address ) ) {
			serverStatus = &status;
			break;
		}
	}
	// if we didn't request this server status
	if (!serverStatus) {
		return;
	}

	s = MSG_ReadStringLine( msg );
	publishBrowserDetails = CL_WebHost_OnServerStatusResponseInfo( from, s );

	len = 0;
	Com_sprintf(serverStatus->string.data() + len, static_cast<int>( serverStatus->string.size() ) - len, "%s", s);

	if (serverStatus->print) {
		Com_Printf("Server settings:\n");
		// print cvars
		while (*s) {
			for (i = 0; i < 2 && *s; i++) {
				if (*s == '\\')
					s++;
				l = 0;
				while (*s) {
					info[l++] = *s;
					if (l >= MAX_INFO_STRING-1)
						break;
					s++;
					if (*s == '\\') {
						break;
					}
				}
				info[l] = '\0';
				if (i) {
					Com_Printf("%s\n", info.data());
				}
				else {
					Com_Printf("%-24s", info.data());
				}
			}
		}
	}

	len = strlen(serverStatus->string.data());
	Com_sprintf(serverStatus->string.data() + len, static_cast<int>( serverStatus->string.size() ) - len, "\\");

	if (serverStatus->print) {
		Com_Printf("\nPlayers:\n");
		Com_Printf("num: score: ping: name:\n");
	}
	for (i = 0, s = MSG_ReadStringLine( msg ); *s; s = MSG_ReadStringLine( msg ), i++) {

		len = strlen(serverStatus->string.data());
		Com_sprintf(serverStatus->string.data() + len, static_cast<int>( serverStatus->string.size() ) - len, "\\%s", s);
		if ( publishBrowserDetails ) {
			CL_WebHost_OnServerStatusResponsePlayer( from, s );
		}

		if (serverStatus->print) {
			//score = ping = 0;
			//sscanf(s, "%d %d", &score, &ping);
			Q_strncpyz( buf.data(), s, static_cast<int>( buf.size() ) );
			Com_Split( buf.data(), v, 2, ' ' );
			score = std::atoi( v[0] );
			ping = std::atoi( v[1] );
			s = strchr(s, ' ');
			if (s)
				s = strchr(s+1, ' ');
			if (s)
				s++;
			else
				s = "unknown";
			Com_Printf("%-2d   %-3d    %-3d   %s\n", i, score, ping, s );
		}
	}
	len = strlen(serverStatus->string.data());
	Com_sprintf(serverStatus->string.data() + len, static_cast<int>( serverStatus->string.size() ) - len, "\\");

	serverStatus->time = Sys_Milliseconds();
	serverStatus->address = *from;
	serverStatus->pending = false;
	if (serverStatus->print) {
		serverStatus->retrieved = true;
	}
	if ( publishBrowserDetails ) {
		CL_WebHost_OnServerStatusResponseComplete( from );
	}
}


/*
==================
CL_LocalServers_f
==================
*/
static void CL_LocalServers_f( void ) {
	const char	*message;
	int			i, j, n;
	netadr_t	to;

	Com_Printf( "Scanning for servers on the local network...\n");

	// reset the list, waiting for response
	cls.numlocalservers = 0;
	cls.pingUpdateSource = AS_LOCAL;

	for (i = 0; i < MAX_OTHER_SERVERS; i++) {
		bool b = cls.localServers[i].visible != qfalse;
		cls.localServers[i] = {};
		cls.localServers[i].visible = ToQboolean( b );
	}

	// The 'xxx' in the message is a challenge that will be echoed back
	// by the server.  We don't care about that here, but master servers
	// can use that to prevent spoofed server responses from invalid ip
	message = "\377\377\377\377getinfo xxx";
	n = static_cast<int>( strlen( message ) );

	// send each message twice in case one is dropped
	for ( i = 0 ; i < 2 ; i++ ) {
		// send a broadcast packet on each server port
		// we support multiple server ports so a single machine
		// can nicely run multiple servers
		for ( j = 0 ; j < NUM_SERVER_PORTS ; j++ ) {
			to.port = BigShort( static_cast<short>( PORT_SERVER + j ) );

			to.type = NA_BROADCAST;
			NET_SendPacket( NS_CLIENT, n, message, &to );
#ifdef USE_IPV6
			to.type = NA_MULTICAST6;
			NET_SendPacket( NS_CLIENT, n, message, &to );
#endif
		}
	}
}


/*
==================
CL_GlobalServers_f

Originally master 0 was Internet and master 1 was MPlayer.
ioquake3 2008; added support for requesting five separate master servers using 0-4.
ioquake3 2017; made master 0 fetch all master servers and 1-5 request a single master server.
==================
*/
static void CL_GlobalServers_f( void ) {
	netadr_t	to{};
	int			count, i, masterNum;
	std::array<char, 1024> command;
	const char	*masteraddress;

	if ( (count = Cmd_Argc()) < 3 || (masterNum = std::atoi( Cmd_Argv(1) )) < 0 || masterNum > MAX_MASTER_SERVERS )
	{
		Com_Printf( "usage: globalservers <master# 0-%d> <protocol> [keywords]\n", MAX_MASTER_SERVERS );
		return;
	}

	// request from all master servers
	if ( masterNum == 0 ) {
		int numAddress = 0;

		for ( i = 1; i <= MAX_MASTER_SERVERS; i++ ) {
			Com_sprintf( command.data(), static_cast<int>( command.size() ), "sv_master%d", i );
			masteraddress = Cvar_VariableString( command.data() );

			if ( !*masteraddress )
				continue;

			numAddress++;

			Com_sprintf( command.data(), static_cast<int>( command.size() ), "globalservers %d %s %s\n", i, Cmd_Argv( 2 ), Cmd_ArgsFrom( 3 ) );
			Cbuf_AddText( command.data() );
		}

		if ( !numAddress ) {
			Com_Printf( "CL_GlobalServers_f: Error: No master server addresses.\n");
		}
		return;
	}

	Com_sprintf( command.data(), static_cast<int>( command.size() ), "sv_master%d", masterNum );
	masteraddress = Cvar_VariableString( command.data() );

	if ( !*masteraddress )
	{
		Com_Printf( "CL_GlobalServers_f: Error: No master server address given.\n");
		return;
	}

	// reset the list, waiting for response
	// -1 is used to distinguish a "no response"

	i = NET_StringToAdr( masteraddress, &to, NA_UNSPEC );

	if ( i == 0 )
	{
		Com_Printf( "CL_GlobalServers_f: Error: could not resolve address of master %s\n", masteraddress );
		return;
	}
	else if ( i == 2 )
		to.port = BigShort( PORT_MASTER );

	Com_Printf( "Requesting servers from %s (%s)...\n", masteraddress, NET_AdrToStringwPort( &to ) );

	cls.numglobalservers = -1;
	cls.pingUpdateSource = AS_GLOBAL;

	// Use the extended query for IPv6 masters
#ifdef USE_IPV6
	if ( to.type == NA_IP6 || to.type == NA_MULTICAST6 )
	{
		int v4enabled = Cvar_VariableIntegerValue( "net_enabled" ) & NET_ENABLEV4;

		if ( v4enabled )
		{
			Com_sprintf( command.data(), static_cast<int>( command.size() ), "getserversExt %s %s",
				GAMENAME_FOR_MASTER, Cmd_Argv(2) );
		}
		else
		{
			Com_sprintf( command.data(), static_cast<int>( command.size() ), "getserversExt %s %s ipv6",
				GAMENAME_FOR_MASTER, Cmd_Argv(2) );
		}
	}
	else
#endif
		Com_sprintf( command.data(), static_cast<int>( command.size() ), "getservers %s", Cmd_Argv(2) );

	for ( i = 3; i < count; i++ )
	{
		Q_strcat( command.data(), static_cast<int>( command.size() ), " " );
		Q_strcat( command.data(), static_cast<int>( command.size() ), Cmd_Argv( i ) );
	}

	NET_OutOfBandPrint( NS_SERVER, &to, "%s", command.data() );
}


/*
==================
CL_GetPing
==================
*/
void CL_GetPing( int n, char *buf, int buflen, int *pingtime )
{
	const ping_t *ping = CL_GetPingSlot( n );
	const char	*str;
	int		time;
	int		maxPing;

	if ( !ping || !ping->adr.port )
	{
		// empty or invalid slot
		buf[0]    = '\0';
		*pingtime = 0;
		return;
	}

	str = NET_AdrToStringwPort( &ping->adr );
	Q_strncpyz( buf, str, buflen );

	time = ping->time;
	if ( time == 0 )
	{
		// check for timeout
		time = Sys_Milliseconds() - ping->start;
		maxPing = Cvar_VariableIntegerValue( "cl_maxPing" );
		if ( time < maxPing )
		{
			// not timed out yet
			time = 0;
		}
	}

	CL_SetServerInfoByAddress(&ping->adr, ping->info, ping->time);

	*pingtime = time;
}


/*
==================
CL_GetPingInfo
==================
*/
void CL_GetPingInfo( int n, char *buf, int buflen )
{
	const ping_t *ping = CL_GetPingSlot( n );

	if ( !ping || !ping->adr.port )
	{
		// empty or invalid slot
		if (buflen)
			buf[0] = '\0';
		return;
	}

	Q_strncpyz( buf, ping->info, buflen );
}


/*
==================
CL_ClearPing
==================
*/
void CL_ClearPing( int n )
{
	ping_t *ping = CL_GetPingSlot( n );

	if ( !ping )
		return;

	ping->adr.port = 0;
}


/*
==================
CL_GetPingQueueCount
==================
*/
int CL_GetPingQueueCount( void )
{
	int		count;

	count   = 0;
	for ( const ping_t& ping : cl_pinglist ) {
		if (ping.adr.port) {
			count++;
		}
	}

	return (count);
}


/*
==================
CL_GetFreePing
==================
*/
static ping_t* CL_GetFreePing( void )
{
	ping_t* best;
	int		oldest;
	int		time, msec;

	msec = Sys_Milliseconds();
	for ( ping_t& ping : cl_pinglist )
	{
		// find free ping slot
		if ( ping.adr.port )
		{
			if ( ping.time == 0 )
			{
				if ( msec - ping.start < 500 )
				{
					// still waiting for response
					continue;
				}
			}
			else if ( ping.time < 500 )
			{
				// results have not been queried
				continue;
			}
		}

		// clear it
		ping.adr.port = 0;
		return &ping;
	}

	// use oldest entry
	best    = &cl_pinglist.front();
	oldest  = INT_MIN;
	for ( ping_t& ping : cl_pinglist )
	{
		// scan for oldest
		time = msec - ping.start;
		if ( time > oldest )
		{
			oldest = time;
			best   = &ping;
		}
	}

	return best;
}


/*
==================
CL_Ping_f
==================
*/
static void CL_Ping_f( void ) {
	netadr_t	to;
	ping_t*		pingptr;
	const char*		server;
	int			argc;
	netadrtype_t	family = NA_UNSPEC;

	argc = Cmd_Argc();

	if ( argc != 2 && argc != 3 ) {
		Com_Printf( "usage: ping [-4|-6] <server>\n");
		return;
	}

	if ( argc == 2 )
		server = Cmd_Argv(1);
	else
	{
		if( !strcmp( Cmd_Argv(1), "-4" ) )
			family = NA_IP;
#ifdef USE_IPV6
		else if( !strcmp( Cmd_Argv(1), "-6" ) )
			family = NA_IP6;
		else
			Com_Printf( "warning: only -4 or -6 as address type understood.\n" );
#else
		else
			Com_Printf( "warning: only -4 as address type understood.\n" );
#endif

		server = Cmd_Argv(2);
	}

	if ( !NET_StringToAdr( server, &to, family ) ) {
		return;
	}

	pingptr = CL_GetFreePing();

	pingptr->adr = to;
	pingptr->start = Sys_Milliseconds();
	pingptr->time  = 0;

	CL_SetServerInfoByAddress( &pingptr->adr, nullptr, 0 );

	NET_OutOfBandPrint( NS_CLIENT, &to, "getinfo xxx" );
}


/*
==================
CL_UpdateVisiblePings_f
==================
*/
qboolean CL_UpdateVisiblePings_f(int source) {
	int			slots, i;
	std::array<char, MAX_STRING_CHARS> buff;
	int			pingTime;
	int			max;
	bool status = false;

	if (source < 0 || source > AS_FAVORITES) {
		return qfalse;
	}

	cls.pingUpdateSource = source;

	slots = CL_GetPingQueueCount();
	if (slots < static_cast<int>( cl_pinglist.size() )) {
		serverInfo_t *server = nullptr;

		switch (source) {
			case AS_LOCAL :
				server = &cls.localServers[0];
				max = cls.numlocalservers;
			break;
			case AS_GLOBAL :
				server = &cls.globalServers[0];
				max = cls.numglobalservers;
			break;
			case AS_FAVORITES :
				server = &cls.favoriteServers[0];
				max = cls.numfavoriteservers;
			break;
			default:
				return qfalse;
		}
		for (i = 0; i < max; i++) {
			if (server[i].visible) {
				if (server[i].ping == -1) {
					int j;

					if (slots >= static_cast<int>( cl_pinglist.size() )) {
						break;
					}
					for (j = 0; j < static_cast<int>( cl_pinglist.size() ); j++) {
						if (!cl_pinglist[j].adr.port) {
							continue;
						}
						if (NET_CompareAdr( &cl_pinglist[j].adr, &server[i].adr)) {
							// already on the list
							break;
						}
					}
					if (j >= static_cast<int>( cl_pinglist.size() )) {
						status = true;
						for (ping_t& ping : cl_pinglist) {
							if (!ping.adr.port) {
								ping.adr = server[i].adr;
								ping.start = Sys_Milliseconds();
								ping.time = 0;
								NET_OutOfBandPrint(NS_CLIENT, &ping.adr, "getinfo xxx");
								slots++;
								break;
							}
						}
					}
				}
				// if the server has a ping higher than cl_maxPing or
				// the ping packet got lost
				else if (server[i].ping == 0) {
					// if we are updating global servers
					if (source == AS_GLOBAL) {
						//
						if ( cls.numGlobalServerAddresses > 0 ) {
							// overwrite this server with one from the additional global servers
							cls.numGlobalServerAddresses--;
							CL_InitServerInfo(&server[i], &cls.globalServerAddresses[cls.numGlobalServerAddresses]);
							// NOTE: the server[i].visible flag stays untouched
						}
					}
				}
			}
		}
	}

	if (slots) {
		status = true;
	}
	for (i = 0; i < static_cast<int>( cl_pinglist.size() ); i++) {
		if (!cl_pinglist[i].adr.port) {
			continue;
		}
		CL_GetPing( i, buff.data(), static_cast<int>( buff.size() ), &pingTime );
		if (pingTime != 0) {
			CL_ClearPing(i);
			status = true;
		}
	}

	return ToQboolean( status );
}


/*
==================
CL_ServerStatus_f
==================
*/
static void CL_ServerStatus_f( void ) {
	netadr_t	to{}, *toptr = nullptr;
	const char		*server;
	serverStatus_t *serverStatus;
	int			argc;
	netadrtype_t	family = NA_UNSPEC;

	argc = Cmd_Argc();

	if ( argc != 2 && argc != 3 )
	{
		if (cls.state != CA_ACTIVE || clc.demoplaying)
		{
			Com_Printf( "Not connected to a server.\n" );
#ifdef USE_IPV6
			Com_Printf( "usage: serverstatus [-4|-6] <server>\n" );
#else
			Com_Printf("usage: serverstatus <server>\n");
#endif
			return;
		}

		toptr = &clc.serverAddress;
	}

	if ( !toptr )
	{
		if ( argc == 2 )
			server = Cmd_Argv(1);
		else
		{
			if ( !strcmp( Cmd_Argv(1), "-4" ) )
				family = NA_IP;
#ifdef USE_IPV6
			else if ( !strcmp( Cmd_Argv(1), "-6" ) )
				family = NA_IP6;
			else
				Com_Printf( "warning: only -4 or -6 as address type understood.\n" );
#else
			else
				Com_Printf( "warning: only -4 as address type understood.\n" );
#endif

			server = Cmd_Argv(2);
		}

		toptr = &to;
		if ( !NET_StringToAdr( server, toptr, family ) )
			return;
	}

	NET_OutOfBandPrint( NS_CLIENT, toptr, "getstatus" );

	serverStatus = CL_GetServerStatus( toptr );
	serverStatus->address = *toptr;
	serverStatus->print = true;
	serverStatus->pending = true;
}


/*
==================
CL_ShowIP_f
==================
*/
static void CL_ShowIP_f( void ) {
	Sys_ShowIP();
}


#ifdef USE_CURL

qboolean CL_Download( const char *cmd, const char *pakname, qboolean autoDownload )
{
	std::array<char, MAX_OSPATH> url;
	std::array<char, MAX_CVAR_VALUE_STRING> name;
	const char *s;

	if ( cl_dlURL->string[0] == '\0' )
	{
		Com_Printf( S_COLOR_YELLOW "cl_dlURL cvar is not set\n" );
		return qfalse;
	}

	// skip leading slashes
	while ( *pakname == '/' || *pakname == '\\' )
		pakname++;

	// skip gamedir
	s = strrchr( pakname, '/' );
	if ( s )
		pakname = s+1;

	if ( !Com_DL_ValidFileName( pakname ) )
	{
		Com_Printf( S_COLOR_YELLOW "invalid file name: '%s'.\n", pakname );
		return qfalse;
	}

	if ( !Q_stricmp( cmd, "dlmap" ) )
	{
		Q_strncpyz( name.data(), pakname, static_cast<int>( name.size() ) );
		if ( !FS_StripExt( name.data(), ".pk3" ) ) {
			FS_StripExt( name.data(), ".pak" );
		}
		if ( !name[0] )
			return qfalse;
		s = va( "maps/%s.bsp", name.data() );
	if ( FS_FileIsInPAK( s, nullptr, url.data() ) )
		{
			Com_Printf( S_COLOR_YELLOW " map %s already exists in pack %s\n", name.data(), url.data() );
			return qfalse;
		}
	}

	return Com_DL_Begin( &download, pakname, cl_dlURL->string, autoDownload );
}


/*
==================
CL_Download_f
==================
*/
static void CL_Download_f( void )
{
	if ( Cmd_Argc() < 2 || *Cmd_Argv( 1 ) == '\0' )
	{
		Com_Printf( "usage: %s <mapname>\n", Cmd_Argv( 0 ) );
		return;
	}

	if ( !strcmp( Cmd_Argv(1), "-" ) )
	{
		Com_DL_Cleanup( &download );
		return;
	}

	CL_Download( Cmd_Argv( 0 ), Cmd_Argv( 1 ), qfalse );
}
#endif // USE_CURL
