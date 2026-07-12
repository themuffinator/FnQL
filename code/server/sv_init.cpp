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

#include "server.h"

#include <array>

/*
===============
SV_SendConfigstring

Creates and sends the server command necessary to update the CS index for the
given client
===============
*/
static void SV_SendConfigstring(client_t *client, int index)
{
	int maxChunkSize = MAX_STRING_CHARS - 24;
	int len;

	len = strlen(sv.configstrings[index]);

	if( len >= maxChunkSize ) {
		int		sent = 0;
		int		remaining = len;
		const char	*cmd;
		std::array<char, MAX_STRING_CHARS> buf{};

		while (remaining > 0 ) {
			if ( sent == 0 ) {
				cmd = "bcs0";
			}
			else if( remaining < maxChunkSize ) {
				cmd = "bcs2";
			}
			else {
				cmd = "bcs1";
			}
			Q_strncpyz( buf.data(), &sv.configstrings[index][sent],
				maxChunkSize );

			SV_SendServerCommand( client, "%s %i \"%s\"", cmd,
				index, buf.data() );

			sent += (maxChunkSize - 1);
			remaining -= (maxChunkSize - 1);
		}
	} else {
		// standard cs, just send it
		SV_SendServerCommand( client, "cs %i \"%s\"", index,
			sv.configstrings[index] );
	}
}

/*
===============
SV_UpdateConfigstrings

Called when a client goes from CS_PRIMED to CS_ACTIVE.  Updates all
Configstring indexes that have changed while the client was in CS_PRIMED
===============
*/
void SV_UpdateConfigstrings(client_t *client)
{
	for( int index : SV_Indices( MAX_CONFIGSTRINGS ) ) {
		// if the CS hasn't changed since we went to CS_PRIMED, ignore
		if(!client->csUpdated[index])
			continue;

		// do not always send server info to all clients
		if ( index == CS_SERVERINFO && ( SV_GentityNum( SV_ClientIndex( client ) )->r.svFlags & SVF_NOSERVERINFO ) ) {
			continue;
		}

		SV_SendConfigstring(client, index);
		client->csUpdated[index] = qfalse;
	}
}

/*
===============
SV_SetConfigstring

===============
*/
void SV_SetConfigstring (int index, const char *val) {
	if ( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		Com_Error (ERR_DROP, "SV_SetConfigstring: bad index %i", index);
	}

	if ( !val ) {
		val = "";
	}

	// don't bother broadcasting an update if no change
	if ( !strcmp( val, sv.configstrings[ index ] ) ) {
		return;
	}

	// change the string in sv
	SV_ZFree( sv.configstrings[index] );
	sv.configstrings[index] = CopyString( val );

	// send it to all the clients if we aren't
	// spawning a new server
	if ( sv.state == SS_GAME || sv.restarting ) {

		// send the data to all relevant clients
		for ( client_t &client : SV_Clients() ) {
			if ( client.state < CS_ACTIVE ) {
				if ( client.state == CS_PRIMED || client.state == CS_CONNECTED ) {
					// track CS_CONNECTED clients as well to optimize gamestate acknowledge after downloading/retransmission
					client.csUpdated[index] = qtrue;
				}
				continue;
			}
			// do not always send server info to all clients
			if ( index == CS_SERVERINFO && ( SV_GentityNum( SV_ClientIndex( &client ) )->r.svFlags & SVF_NOSERVERINFO ) ) {
				continue;
			}

			SV_SendConfigstring(&client, index);
		}
	}
}


/*
===============
SV_GetConfigstring
===============
*/
void SV_GetConfigstring( int index, char *buffer, int bufferSize ) {
	if ( bufferSize < 1 ) {
		Com_Error( ERR_DROP, "SV_GetConfigstring: bufferSize == %i", bufferSize );
	}
	if ( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		Com_Error (ERR_DROP, "SV_GetConfigstring: bad index %i", index);
	}
	if ( !sv.configstrings[index] ) {
		buffer[0] = '\0';
		return;
	}

	Q_strncpyz( buffer, sv.configstrings[index], bufferSize );
}


/*
===============
SV_SetUserinfo

===============
*/
void SV_SetUserinfo( int index, const char *val ) {
	if ( index < 0 || index >= sv.maxclients ) {
		Com_Error( ERR_DROP, "%s: bad index %i", __func__, index );
	}

	if ( !val ) {
		val = "";
	}

	client_t &client = SV_ClientForIndex( index );
	Q_strncpyz( client.userinfo, val, SV_ArraySize( client.userinfo ) );
	Q_strncpyz( client.name, Info_ValueForKey( val, "name" ), SV_ArraySize(client.name) );
}



/*
===============
SV_GetUserinfo

===============
*/
void SV_GetUserinfo( int index, char *buffer, int bufferSize ) {
	if ( bufferSize < 1 ) {
		Com_Error( ERR_DROP, "%s: bufferSize == %i", __func__, bufferSize );
	}
	if ( index < 0 || index >= sv.maxclients ) {
		Com_Error( ERR_DROP, "%s: bad index %i", __func__, index );
	}
	Q_strncpyz( buffer, SV_ClientForIndex( index ).userinfo, bufferSize );
}


/*
================
SV_CreateBaseline

Entity baselines are used to compress non-delta messages
to the clients -- only the fields that differ from the
baseline will be transmitted
================
*/
static void SV_CreateBaseline( void ) {
	sharedEntity_t *ent;

	for ( int entnum : SV_Indices( sv.num_entities ) ) {
		ent = SV_GentityNum( entnum );
		if ( !ent->r.linked ) {
			continue;
		}
		ent->s.number = entnum;

		//
		// take current state as baseline
		//
		sv.svEntities[ entnum ].baseline = ent->s;
		sv.baselineUsed[ entnum ] = 1;
	}
}


/*
===============
SV_BoundMaxClients
===============
*/
static int SV_BoundMaxClients( int minimum ) {
	// get the current maxclients value
	Cvar_Get( "sv_maxclients", "8", 0 );

	if ( sv_maxclients->integer < minimum ) {
		Cvar_SetIntegerValue( "sv_maxclients", minimum );
		sv_maxclients->modified = qfalse;
		return minimum;
	}

	sv_maxclients->modified = qfalse;

	return sv_maxclients->integer;
}


/*
===============
SV_SetSnapshotParams
===============
*/
static void SV_SetSnapshotParams( void )
{
	// PACKET_BACKUP frames is just about 6.67MB so use that even on listen servers
	svs.numSnapshotEntities = PACKET_BACKUP * MAX_GENTITIES;
}


/*
===============
SV_AllocClients
===============
*/
static void SV_AllocClients( int count )
{
	svs.clients = SV_ZTagMallocArray<client_t>( count, TAG_CLIENTS );
	sv.maxclients = count;
	for ( client_t &client : SV_Clients() ) {
		client = {};
	}
	SV_SetSnapshotParams();
}


/*
===============
SV_Startup

Called when a host starts a map when it wasn't running
one before.  Successive map or map_restart commands will
NOT cause this to be called, unless the game is exited to
the menu system first.
===============
*/
static void SV_Startup( void ) {
	if ( svs.initialized ) {
		Com_Error( ERR_FATAL, "SV_Startup: svs.initialized" );
	}

	SV_AllocClients( sv_maxclients->integer );

	sv_maxclients->modified = qfalse;

	svs.initialized = qtrue;

	// Don't respect sv_killserver unless a server is actually running
	if ( sv_killserver->integer ) {
		Cvar_Set( "sv_killserver", "0" );
	}

	Cvar_Set( "sv_running", "1" );

	// Join the ipv6 multicast group now that a map is running so clients can scan for us on the local network.
#ifdef USE_IPV6
	NET_JoinMulticast6();
#endif
}


/*
==================
SV_ChangeMaxClients
==================
*/
static void SV_ChangeMaxClients( void ) {
	client_t *oldClients;
	int		maxclients;
	int		count;

	// get the highest client number in use
	count = 0;
	for ( SV_ClientSlot slot : SV_ClientSlots() ) {
		if ( slot.client.state >= CS_CONNECTED ) {
			if ( slot.index > count ) {
				count = slot.index;
			}
		}
	}
	count++;

	// never go below the highest client number in use
	maxclients = SV_BoundMaxClients( count );

	// if still the same
	if ( maxclients == sv.maxclients ) {
		return;
	}

	oldClients = SV_HunkTempAllocArray<client_t>( count );
	const auto freeOldClients = SV_MakeScopeExit( [&]() { SV_HunkFreeTemp( oldClients ); } );

	// copy the clients to hunk memory
	for ( SV_ClientSlot slot : SV_ClientSlots() ) {
		if ( slot.index >= count ) {
			break;
		}
		if ( slot.client.state >= CS_CONNECTED ) {
			oldClients[slot.index] = slot.client;
		} else {
			oldClients[slot.index] = {};
		}
	}

	// free old clients arrays
	SV_ZFree( svs.clients );

	// allocate new clients
	SV_AllocClients( maxclients );

	// copy the clients over
	for ( SV_ClientSlot slot : SV_ClientSlots() ) {
		if ( slot.index >= count ) {
			break;
		}
		if ( oldClients[slot.index].state >= CS_CONNECTED ) {
			slot.client = oldClients[slot.index];
		}
	}
}


/*
================
SV_ClearServer
================
*/
static void SV_ClearServer( void ) {
	for ( int index : SV_Indices( MAX_CONFIGSTRINGS ) ) {
		SV_ZFree( sv.configstrings[index] );
	}

	if ( !sv_levelTimeReset->integer ) {
		const int preservedTime = sv.time;
		sv = {};
		sv.time = preservedTime;
	} else {
		sv = {};
	}
}


/*
================
SV_SpawnServer

Change the server to a new map, taking all connected
clients along with it.
This is NOT called for map_restart
================
*/
void SV_SpawnServer( const char *mapname, qboolean killBots ) {
	int			checksum;
	bool		isBot;
	const char	*p;

	// shut down the existing game if it is running
	SV_ShutdownGameProgs();

	Com_Printf( "------ Server Initialization ------\n" );
	Com_Printf( "Server: %s\n", mapname );

	Sys_SetStatus( "Initializing server..." );

#ifndef DEDICATED
	// if not running a dedicated server CL_MapLoading will connect the client to the server
	// also print some status stuff
	CL_MapLoading();

	// make sure all the client stuff is unloaded
	CL_ShutdownAll();
#endif

	// clear the whole hunk because we're (re)loading the server
	Hunk_Clear();

	// clear collision map data
	CM_ClearMap();

	Cvar_CheckRange( com_timescale, "0", nullptr, CV_FLOAT );

	// Restart renderer?
	// CL_StartHunkUsers( );

	// init client structures and svs.numSnapshotEntities
	if ( !Cvar_VariableIntegerValue( "sv_running" ) ) {
		SV_Startup();
	} else {
		// check for maxclients change
		if ( sv_maxclients->modified ) {
			SV_ChangeMaxClients();
		}
	}

#ifndef DEDICATED
	// remove pure paks that may left from client-side
	FS_PureServerSetLoadedPaks( "", "" );
	FS_PureServerSetReferencedPaks( "", "" );
#endif

	// clear pak references
	FS_ClearPakReferences( 0 );

	// allocate the snapshot entities on the hunk
	svs.snapshotEntities = SV_HunkAllocArray<entityState_t>( svs.numSnapshotEntities, h_high );

	// initialize snapshot storage
	SV_InitSnapshotStorage();

	// toggle the server bit so clients can detect that a
	// server has changed
	svs.snapFlagServerBit ^= SNAPFLAG_SERVERCOUNT;

	// set nextmap to the same map, but it may be overridden
	// by the game startup or another console command
	Cvar_Set( "nextmap", "map_restart 0" );
//	Cvar_Set( "nextmap", va("map %s", server) );

	// try to reset level time if server is empty
	if ( !sv_levelTimeReset->integer && !sv.restartTime ) {
		bool hasConnectedClient = false;
		for ( const client_t &client : SV_Clients() ) {
			if ( client.state >= CS_CONNECTED ) {
				hasConnectedClient = true;
				break;
			}
		}
		if ( !hasConnectedClient ) {
			sv.time = 0;
		}
	}

	for ( client_t &client : SV_Clients() ) {
		// save when the server started for each client already connected
		if ( client.state >= CS_CONNECTED && sv_levelTimeReset->integer ) {
			client.oldServerTime = sv.time;
		} else {
			client.oldServerTime = 0;
		}
	}

	// preserve maxclients
	const int preservedMaxClients = sv.maxclients;
	// wipe the entire per-level structure
	SV_ClearServer();
	sv.maxclients = preservedMaxClients;
	for ( int index : SV_Indices( MAX_CONFIGSTRINGS ) ) {
		sv.configstrings[index] = CopyString("");
	}

	// make sure we are not paused
#ifndef DEDICATED
	Cvar_Set( "cl_paused", "0" );
#endif

	// get latched value
	sv_pure = Cvar_Get( "sv_pure", "1", CVAR_SYSTEMINFO | CVAR_LATCH );

	// VMs can change latched cvars instantly which could cause side-effects in SV_UserMove()
	sv.pure = sv_pure->integer;

	// get a new checksum feed and restart the file system
	srand( Com_Milliseconds() );
	Com_RandomBytes( reinterpret_cast<byte *>( &sv.checksumFeed ), sizeof( sv.checksumFeed ) );
	FS_Restart( sv.checksumFeed );

	Sys_SetStatus( "Loading map %s", mapname );
	CM_LoadMap( va( "maps/%s.bsp", mapname ), qfalse, &checksum );

	// set serverinfo visible name
	Cvar_Set( "mapname", mapname );

	Cvar_SetIntegerValue( "sv_mapChecksum", checksum );

	// serverid should be different each time
	sv.serverId = com_frameTime;
	sv.restartedServerId = sv.serverId;
	Cvar_SetIntegerValue( "sv_serverid", sv.serverId );

	// clear physics interaction links
	SV_ClearWorld();

	// media configstring setting should be done during
	// the loading stage, so connected clients don't have
	// to load during actual gameplay
	sv.state = SS_LOADING;

	// make sure that level time is not zero
	//sv.time = sv.time ? sv.time : 8;

	// load and spawn all other entities
	SV_InitGameProgs();

	// don't allow a map_restart if game is modified
	sv_gametype->modified = qfalse;

	sv_pure->modified = qfalse;

	// run a few frames to allow everything to settle
	for ( int ignored : SV_Indices( 3 ) ) {
		(void)ignored;
		Cbuf_Wait();
		sv.time += 100;
		VM_Call( gvm, 1, GAME_RUN_FRAME, sv.time );
		SV_BotFrame( sv.time );
	}

	// create a baseline for more efficient communications
	SV_CreateBaseline();

	for ( SV_ClientSlot slot : SV_ClientSlots() ) {
		// send the new gamestate to all connected clients
		if ( slot.client.state >= CS_CONNECTED ) {
			const char *denied;

			if ( slot.client.netchan.remoteAddress.type == NA_BOT ) {
				if ( killBots ) {
					SV_DropClient( &slot.client, "was kicked" );
					continue;
				}
				isBot = true;
			}
			else {
				isBot = false;
			}

			// connect the client again
			denied = static_cast<const char *>( GVM_ArgPtr( VM_Call( gvm, 3, GAME_CLIENT_CONNECT, slot.index, qfalse, isBot ) ) );	// firstTime = qfalse
			if ( denied ) {
				// this generally shouldn't happen, because the client
				// was connected before the level change
				SV_DropClient( &slot.client, denied );
			} else {
				if ( !isBot ) {
					slot.client.gamestateAck = GSA_INIT; // resend gamestate, accept first correct serverId
					// when we get the next packet from a connected client,
					// the new gamestate will be sent
					slot.client.state = CS_CONNECTED;
					slot.client.gentity = nullptr;
				} else {
					SV_ClientEnterWorld( &slot.client );
				}
			}
		}
	}

	// run another frame to allow things to look at all the players
	Cbuf_Wait();
	sv.time += 100;
	VM_Call( gvm, 1, GAME_RUN_FRAME, sv.time );
	SV_BotFrame( sv.time );
	svs.time += 100;

	// Retail QL pure validation keys off the native cgame binary pak.
	FS_TouchFileInPak( QL_NATIVE_CGAME_DLL );

	// the server sends these to the clients so they can figure
	// out which pk3s should be auto-downloaded
	p = FS_ReferencedPakNames();
	if ( FS_ExcludeReference() ) {
		// \fs_excludeReference may mask our current native cgame binary.
		FS_TouchFileInPak( QL_NATIVE_CGAME_DLL );
		// rebuild referenced paks list
		p = FS_ReferencedPakNames();
	}
	Cvar_Set( "sv_referencedPakNames", p );

	p = FS_ReferencedPakChecksums();
	Cvar_Set( "sv_referencedPaks", p );

	Cvar_Set( "sv_paks", "" );
	Cvar_Set( "sv_pakNames", "" ); // not used on client-side

	if ( sv.pure != 0 ) {
		int freespace, pakslen, infolen;
		qboolean overflowed = qfalse;
		qboolean infoTruncated = qfalse;

		p = FS_LoadedPakChecksums( &overflowed );

		pakslen = strlen( p ) + 9; // + strlen( "\\sv_paks\\" )
		freespace = SV_RemainingGameState();
		infolen = strlen( Cvar_InfoString_Big( CVAR_SYSTEMINFO, &infoTruncated ) );

		if ( infoTruncated ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: truncated systeminfo!\n" );
		}

		if ( pakslen > freespace || infolen + pakslen >= BIG_INFO_STRING || overflowed ) {
			// switch to degraded pure mode
			// this could *potentially* lead to a false "unpure client" detection
			// which is better than guaranteed drop
			Com_DPrintf( S_COLOR_YELLOW "WARNING: skipping sv_paks setup to avoid gamestate overflow\n" );
		} else {
			// the server sends these to the clients so they will only
			// load pk3s also loaded at the server
			Cvar_Set( "sv_paks", p );
			if ( *p == '\0' ) {
				Com_Printf( S_COLOR_YELLOW "WARNING: sv_pure set but no PK3 files loaded\n" );
			}
		}
	}

	// save systeminfo and serverinfo strings
	SV_SetConfigstring( CS_SYSTEMINFO, Cvar_InfoString_Big( CVAR_SYSTEMINFO, nullptr ) );
	cvar_modifiedFlags &= ~CVAR_SYSTEMINFO;

	SV_SetConfigstring( CS_SERVERINFO, Cvar_InfoString( CVAR_SERVERINFO, nullptr ) );
	cvar_modifiedFlags &= ~CVAR_SERVERINFO;

	// any media configstring setting now should issue a warning
	// and any configstring changes should be reliably transmitted
	// to all clients
	sv.state = SS_GAME;
	Zmq_InitStatsPublisher();
	SV_SteamGameServerStart( mapname );

	// send a heartbeat now so the master will get up to date info
	SV_Heartbeat_f();

	Hunk_SetMark();

	Com_Printf ("-----------------------------------\n");

	Sys_SetStatus( "Running map %s", mapname );

	// suppress hitch warning
	Com_FrameInit();
}


/*
===============
SV_Init

Only called at main exe startup, not for each game
===============
*/
void SV_Init( void )
{
	SV_AddOperatorCommands();

	if ( com_dedicated->integer )
		SV_AddDedicatedCommands();

	// serverinfo vars
	Cvar_Get ("dmflags", "0", CVAR_SERVERINFO);
	// The currently shipped retail qagame registers 50 here. Match the module
	// boundary so startup does not retain a stale Quake III reset value.
	Cvar_Get ("fraglimit", "50", CVAR_SERVERINFO);
	Cvar_Get ("timelimit", "0", CVAR_SERVERINFO);
	sv_gametype = Cvar_Get ("g_gametype", "0", CVAR_SERVERINFO | CVAR_LATCH );
	Cvar_SetDescription( sv_gametype, "Set the gametype to mod." );
	Cvar_Get ("sv_keywords", "", CVAR_SERVERINFO);
	//Cvar_Get ("protocol", va("%i", PROTOCOL_VERSION), CVAR_SERVERINFO | CVAR_ROM);
	sv_mapname = Cvar_Get ("mapname", "nomap", CVAR_SERVERINFO | CVAR_ROM);
	Cvar_SetDescription( sv_mapname, "Display the name of the current map being used on a server." );
	sv_privateClients = Cvar_Get( "sv_privateClients", "0", CVAR_SERVERINFO );
	Cvar_CheckRange( sv_privateClients, "0", va( "%i", MAX_CLIENTS-1 ), CV_INTEGER );
	Cvar_SetDescription( sv_privateClients, "The number of spots, out of sv_maxclients, reserved for players with the server password (sv_privatePassword)." );
	sv_hostname = Cvar_Get ("sv_hostname", "noname", CVAR_SERVERINFO | CVAR_ARCHIVE );
	Cvar_SetDescription( sv_hostname, "Sets the name of the server." );
	sv_maxclients = Cvar_Get ("sv_maxclients", "8", CVAR_SERVERINFO | CVAR_LATCH);
	Cvar_CheckRange( sv_maxclients, "1", XSTRING(MAX_CLIENTS), CV_INTEGER );
	Cvar_SetDescription( sv_maxclients, "Maximum number of people allowed to join the server." );

	sv_maxclientsPerIP = Cvar_Get( "sv_maxclientsPerIP", "3", CVAR_ARCHIVE );
	Cvar_CheckRange( sv_maxclientsPerIP, "1", nullptr, CV_INTEGER );
	Cvar_SetDescription( sv_maxclientsPerIP, "Limits number of simultaneous connections from the same IP address." );

	sv_clientTLD = Cvar_Get( "sv_clientTLD", "0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( sv_clientTLD, nullptr, nullptr, CV_INTEGER );
	Cvar_SetDescription( sv_clientTLD, "Client country detection code." );

	sv_minRate = Cvar_Get( "sv_minRate", "0", CVAR_ARCHIVE_ND | CVAR_SERVERINFO );
	Cvar_SetDescription( sv_minRate, "Minimum server bandwidth (in bit per second) a client can use." );
	sv_maxRate = Cvar_Get( "sv_maxRate", "0", CVAR_ARCHIVE_ND | CVAR_SERVERINFO );
	Cvar_SetDescription( sv_maxRate, "Maximum server bandwidth (in bit per second) a client can use." );
	sv_dlRate = Cvar_Get( "sv_dlRate", "100", CVAR_ARCHIVE | CVAR_SERVERINFO );
	Cvar_CheckRange( sv_dlRate, "0", "500", CV_INTEGER );
	Cvar_SetDescription( sv_dlRate, "Bandwidth allotted to pack archive downloads via UDP, in kbyte/s." );
	sv_floodProtect = Cvar_Get( "sv_floodProtect", "1", CVAR_ARCHIVE | CVAR_SERVERINFO );
	Cvar_SetDescription( sv_floodProtect, "Toggle server flood protection to keep players from bringing the server down." );
	sv_enableRankings = Cvar_Get( "sv_enableRankings", "0", CVAR_ARCHIVE | CVAR_SERVERINFO );
	Cvar_CheckRange( sv_enableRankings, "0", "1", CV_INTEGER );
	Cvar_SetDescription( sv_enableRankings, "Retained Quake Live rankings compatibility toggle. FnQL keeps this disabled because no live rankings service is owned by the engine." );
	sv_rankingsActive = Cvar_Get( "sv_rankingsActive", "0", CVAR_SERVERINFO );
	Cvar_SetDescription( sv_rankingsActive, "Reports whether the retained Quake Live rankings compatibility surface is active." );

	// systeminfo
	sv_cheats = Cvar_Get( "sv_cheats", "1", CVAR_SYSTEMINFO | CVAR_ROM );
	sv_serverid = Cvar_Get( "sv_serverid", "0", CVAR_SYSTEMINFO | CVAR_ROM );
	sv_pure = Cvar_Get( "sv_pure", "1", CVAR_SYSTEMINFO | CVAR_LATCH );
	Cvar_SetDescription( sv_pure, "Requires clients to only get data from pack archives the server is using." );
	Cvar_Get( "sv_paks", "", CVAR_SYSTEMINFO | CVAR_ROM );
	Cvar_Get( "sv_pakNames", "", CVAR_SYSTEMINFO | CVAR_ROM );
	Cvar_Get( "sv_referencedPaks", "", CVAR_SYSTEMINFO | CVAR_ROM );
	sv_referencedPakNames = Cvar_Get( "sv_referencedPakNames", "", CVAR_SYSTEMINFO | CVAR_ROM );
	Cvar_SetDescription( sv_referencedPakNames, "Variable holds a list of all the pack archives the server loaded data from." );
	Cvar_Get( "sv_rankingsProvider", "Unavailable", CVAR_ROM );
	Cvar_Get( "sv_rankingsPolicy", "compatibility-unavailable", CVAR_ROM );
	SV_RefreshPlatformServiceCvars();
	SV_RegisterSteamEventSink();
	SV_GameRefreshRankingsPolicyCvars();

	// server vars
	sv_rconPassword = Cvar_Get ("rconPassword", "", CVAR_TEMP | CVAR_PRIVATE );
	Cvar_SetDescription( sv_rconPassword, "Password for remote server commands." );
	sv_privatePassword = Cvar_Get ("sv_privatePassword", "", CVAR_TEMP | CVAR_PRIVATE );
	Cvar_SetDescription( sv_privatePassword, "Set password for private clients to login with." );
	sv_fps = Cvar_Get ("sv_fps", "40", CVAR_TEMP );
	Cvar_CheckRange( sv_fps, "10", "125", CV_INTEGER );
	Cvar_SetDescription( sv_fps, "Set the max frames per second the server sends the client." );
	sv_audioPVS = Cvar_Get( "sv_audioPVS", "0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( sv_audioPVS, "0", "2", CV_INTEGER );
	Cvar_SetDescription( sv_audioPVS, "Compatibility-preserving opt-in for sending nearby occluded sound emitters outside normal visual PVS.\n 0 = disabled, 1 = sound-only emitters, 2 = sound events plus all loopSound and speaker entities." );
	sv_audioPVSRange = Cvar_Get( "sv_audioPVSRange", "1024", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( sv_audioPVSRange, "0", "4096", CV_INTEGER );
	Cvar_SetDescription( sv_audioPVSRange, "Maximum distance for sv_audioPVS sound emitter expansion." );
	sv_audioPVSMaxEntities = Cvar_Get( "sv_audioPVSMaxEntities", "16", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( sv_audioPVSMaxEntities, "0", "64", CV_INTEGER );
	Cvar_SetDescription( sv_audioPVSMaxEntities, "Maximum number of extra sound emitters sv_audioPVS may add to a client snapshot." );
	sv_timeout = Cvar_Get( "sv_timeout", "200", CVAR_TEMP );
	Cvar_CheckRange( sv_timeout, "4", nullptr, CV_INTEGER );
	Cvar_SetDescription( sv_timeout, "Seconds without any message before automatic client disconnect." );
	sv_zombietime = Cvar_Get( "sv_zombietime", "2", CVAR_TEMP );
	Cvar_CheckRange( sv_zombietime, "1", nullptr, CV_INTEGER );
	Cvar_SetDescription( sv_zombietime, "Seconds to sink messages after disconnect." );
	Cvar_Get ("nextmap", "", CVAR_TEMP );

	sv_allowDownload = Cvar_Get ("sv_allowDownload", "1", CVAR_SERVERINFO);
	Cvar_SetDescription( sv_allowDownload, "Toggle the ability for clients to download files maps etc. from server." );
	Cvar_Get ("sv_dlURL", "", CVAR_SERVERINFO | CVAR_ARCHIVE);

	// moved to Com_Init()
	//sv_master[0] = Cvar_Get( "sv_master1", MASTER_SERVER_NAME, CVAR_INIT | CVAR_ARCHIVE_ND );
	//sv_master[1] = Cvar_Get( "sv_master2", "master.ioquake3.org", CVAR_INIT | CVAR_ARCHIVE_ND );
	//sv_master[2] = Cvar_Get( "sv_master3", "master.maverickservers.com", CVAR_INIT | CVAR_ARCHIVE_ND );

	for ( int index : SV_Indices( MAX_MASTER_SERVERS ) ) {
		sv_master[ index ] = Cvar_Get( va( "sv_master%d", index + 1 ), "", CVAR_ARCHIVE_ND );
	}

	sv_reconnectlimit = Cvar_Get( "sv_reconnectlimit", "3", 0 );
	Cvar_CheckRange( sv_reconnectlimit, "0", "12", CV_INTEGER );
	Cvar_SetDescription( sv_reconnectlimit, "Number of seconds a disconnected client should wait before next reconnect." );

	sv_padPackets = Cvar_Get( "sv_padPackets", "0", CVAR_DEVELOPER );
	Cvar_SetDescription( sv_padPackets, "Adds padding bytes to network packets for rate debugging." );
	sv_killserver = Cvar_Get( "sv_killserver", "0", 0 );
	Cvar_SetDescription( sv_killserver, "Internal flag to manage server state." );
	sv_mapChecksum = Cvar_Get( "sv_mapChecksum", "", CVAR_ROM );
	Cvar_SetDescription( sv_mapChecksum, "Allows check for client server map to match." );
	sv_lanForceRate = Cvar_Get( "sv_lanForceRate", "1", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( sv_lanForceRate, "Forces LAN clients to the maximum rate instead of accepting client setting." );
	sv_autoRecordDemos = Cvar_Get( "sv_autoRecordDemos", "0", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( sv_autoRecordDemos, "Automatically records a server-side demo for each human client.\n 1 = record per map, 2 = record per map but skip devmap, 3 = record per match, 4 = record per match but skip devmap." );

#ifdef USE_BANS
	sv_banFile = Cvar_Get("sv_banFile", "serverbans.dat", CVAR_ARCHIVE);
	Cvar_SetDescription( sv_banFile, "Name of the file that is used for storing the server bans." );
#endif

	sv_levelTimeReset = Cvar_Get( "sv_levelTimeReset", "0", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( sv_levelTimeReset, "Whether or not to reset leveltime after new map loads." );

	sv_filter = Cvar_Get( "sv_filter", "filter.txt", CVAR_ARCHIVE );
	Cvar_SetDescription( sv_filter, "Cvar that point on filter file, if it is "" then filtering will be disabled." );

	// initialize bot cvars so they are listed and can be set before loading the botlib
	SV_BotInitCvars();

	// init the botlib here because we need the pre-compiler in the UI
	SV_BotInitBotLib();

#ifdef USE_BANS
	// Load saved bans
	Cbuf_AddText("rehashbans\n");
#endif

	// track group cvar changes
	Cvar_SetGroup( sv_lanForceRate, CVG_SERVER );
	Cvar_SetGroup( sv_minRate, CVG_SERVER );
	Cvar_SetGroup( sv_maxRate, CVG_SERVER );
	Cvar_SetGroup( sv_fps, CVG_SERVER );
	Cvar_SetGroup( sv_enableRankings, CVG_SERVER );
	Cvar_SetGroup( sv_rankingsActive, CVG_SERVER );
	Cvar_SetGroup( sv_audioPVS, CVG_SERVER );
	Cvar_SetGroup( sv_audioPVSRange, CVG_SERVER );
	Cvar_SetGroup( sv_audioPVSMaxEntities, CVG_SERVER );
	Cvar_SetGroup( sv_autoRecordDemos, CVG_SERVER );

	// force initial check
	SV_TrackCvarChanges();

	Zmq_RegisterCvarsAndInitRcon();
	SV_InitChallenger();
}


/*
==================
SV_FinalMessage

Used by SV_Shutdown to send a final message to all
connected clients before the server goes down.  The messages are sent immediately,
not just stuck on the outgoing message list, because the server is going
to totally exit after returning from this function.
==================
*/
static void SV_FinalMessage( const char *message ) {
	int			j;

	// send it twice, ignoring rate
	for ( j = 0 ; j < 2 ; j++ ) {
		for ( client_t &client : SV_Clients() ) {
			if (client.state >= CS_CONNECTED ) {
				// don't send a disconnect to a local client
				if ( client.netchan.remoteAddress.type != NA_LOOPBACK ) {
					SV_SendServerCommand( &client, "print \"%s\n\"\n", message );
					SV_SendServerCommand( &client, "disconnect \"%s\"", message );
				}
				// force a snapshot to be sent
				client.lastSnapshotTime = svs.time - 9999; // generate a snapshot immediately
				client.state = CS_ZOMBIE; // skip delta generation
				SV_SendClientSnapshot( &client );
			}
		}
	}

	NET_FlushPacketQueue( 99999 );
}


/*
================
SV_Shutdown

Called when each game quits,
before Sys_Quit or Sys_Error
================
*/
void SV_Shutdown( const char *finalmsg ) {
	if ( !com_sv_running || !com_sv_running->integer ) {
		return;
	}

	Com_Printf( "----- Server Shutdown (%s) -----\n", finalmsg );

#ifdef USE_IPV6
	NET_LeaveMulticast6();
#endif

	if ( svs.clients && !com_errorEntered ) {
		SV_FinalMessage( finalmsg );
	}

	SV_RemoveOperatorCommands();
	SV_MasterShutdown();
	SV_FlushAllSteamStats();
	SV_SteamGameServerStop();
	SV_ShutdownGameProgs();
	Zmq_ShutdownStatsPublisher();
	SV_InitChallenger();

	// free current level
	SV_ClearServer();

	SV_FreeIP4DB();

	// free server static data
	if ( svs.clients ) {
		for ( client_t &client : SV_Clients() ) {
			SV_FreeClient( &client );
		}

		SV_ZFree( svs.clients );
	}
	svs = {};
	sv.time = 0;

	Cvar_Set( "sv_running", "0" );

#ifndef DEDICATED
	Cvar_Set( "ui_singlePlayerActive", "0" );
#endif

	Com_Printf( "---------------------------\n" );

#ifndef DEDICATED
	// disconnect any local clients
	if ( sv_killserver->integer != 2 )
		CL_Disconnect( qfalse );
#endif

	// clean some server cvars
	Cvar_Set( "sv_referencedPaks", "" );
	Cvar_Set( "sv_referencedPakNames", "" );
	Cvar_Set( "sv_mapChecksum", "" );
	Cvar_Set( "sv_serverid", "0" );

	Sys_SetStatus( "Server is not running" );
}
