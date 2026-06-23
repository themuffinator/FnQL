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
===============================================================================

OPERATOR CONSOLE ONLY COMMANDS

These commands can only be entered from stdin or by a remote operator datagram
===============================================================================
*/


/*
==================
SV_GetPlayerByHandle

Returns the player with player id or name from Cmd_Argv(1)
==================
*/
client_t *SV_GetPlayerByHandle( void ) {
	client_t	*cl;
	const char		*s;
	std::array<char, MAX_NAME_LENGTH> cleanName{};

	// make sure server is running
	if ( !com_sv_running->integer ) {
		return nullptr;
	}

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "No player specified.\n" );
		return nullptr;
	}

	s = Cmd_Argv(1);

	// Check whether this is a numeric player handle
	const char *end = s;
	while ( *end >= '0' && *end <= '9' ) {
		++end;
	}

	if(!*end)
	{
		int plid = SV_ParseInt(s);

		// Check for numeric playerid match
		if( SV_IsClientIndex( plid ) )
		{
			cl = &SV_ClientForIndex( plid );
			
			if (cl->state >= CS_CONNECTED)
				return cl;
		}
	}

	// check for a name match
	for ( client_t &client : SV_Clients() ) {
		if ( client.state < CS_CONNECTED ) {
			continue;
		}
		if ( !Q_stricmp( client.name, s ) ) {
			return &client;
		}

		Q_strncpyz( cleanName.data(), client.name, SV_ArraySize(cleanName) );
		Q_CleanStr( cleanName.data() );
		if ( !Q_stricmp( cleanName.data(), s ) ) {
			return &client;
		}
	}

	Com_Printf( "Player %s is not on the server\n", s );

	return nullptr;
}


/*
==================
SV_GetPlayerByNum

Returns the player with idnum from Cmd_Argv(1)
==================
*/
static client_t *SV_GetPlayerByNum( void ) {
	client_t	*cl;
	int			i;
	int			idnum;
	const char		*s;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		return nullptr;
	}

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "No player specified.\n" );
		return nullptr;
	}

	s = Cmd_Argv(1);

	for (i = 0; s[i]; i++) {
		if (s[i] < '0' || s[i] > '9') {
			Com_Printf( "Bad slot number: %s\n", s);
			return nullptr;
		}
	}
	idnum = SV_ParseInt( s );
	if ( !SV_IsClientIndex( idnum ) ) {
		Com_Printf( "Bad client slot: %i\n", idnum );
		return nullptr;
	}

	cl = &SV_ClientForIndex( idnum );
	if ( cl->state < CS_CONNECTED ) {
		Com_Printf( "Client %i is not active\n", idnum );
		return nullptr;
	}
	return cl;
}

//=========================================================


/*
==================
SV_Map_f

Restart the server on a different map
==================
*/
static void SV_Map_f( void ) {
	const char		*cmd;
	const char		*map;
	bool		killBots, cheat;
	std::array<char, MAX_QPATH> expanded{};
	std::array<char, MAX_QPATH> mapname{};
	int			len;

	map = Cmd_Argv(1);
	if ( !map || !*map ) {
		return;
	}

	// make sure the level exists before trying to change, so that
	// a typo at the server console won't end the game
	Com_sprintf( expanded.data(), SV_ArraySize(expanded), "maps/%s.bsp", map );
	// bypass pure check so we can open downloaded map
	FS_BypassPure();
	const auto restorePure = SV_MakeScopeExit( []() { FS_RestorePure(); } );
	len = FS_FOpenFileRead( expanded.data(), nullptr, qfalse );
	if ( len == -1 ) {
		Com_Printf( "Can't find map %s\n", expanded.data() );
		return;
	}

	// force latched values to get set
	Cvar_Get ("g_gametype", "0", CVAR_SERVERINFO | CVAR_USERINFO | CVAR_LATCH );

	cmd = Cmd_Argv(0);
	if( Q_stricmpn( cmd, "sp", 2 ) == 0 ) {
		Cvar_SetIntegerValue( "g_gametype", GT_SINGLE_PLAYER );
		Cvar_Set( "g_doWarmup", "0" );
		// may not set sv_maxclients directly, always set latched
		Cvar_SetLatched( "sv_maxclients", "8" );
		cmd += 2;
		if (!Q_stricmp( cmd, "devmap" ) ) {
			cheat = true;
		} else {
			cheat = false;
		}
		killBots = true;
	}
	else {
		if ( !Q_stricmp( cmd, "devmap" ) ) {
			cheat = true;
		} else {
			cheat = false;
		}
		if( sv_gametype->integer == GT_SINGLE_PLAYER ) {
			Cvar_SetIntegerValue( "g_gametype", GT_FFA );
			killBots = true;
		} else {
			killBots = false;
		}
	}

	// save the map name here cause on a map restart we reload the q3config.cfg
	// and thus nuke the arguments of the map command
	Q_strncpyz( mapname.data(), map, SV_ArraySize(mapname) );

	// start up the map
	SV_SpawnServer( mapname.data(), SV_QBool( killBots ) );

	// set the cheat value
	// if the level was started with "map <levelname>", then
	// cheats will not be allowed.  If started with "devmap <levelname>"
	// then cheats will be allowed
	if ( cheat ) {
		Cvar_Set( "sv_cheats", "1" );
	} else {
		Cvar_Set( "sv_cheats", "0" );
	}
}


/*
================
SV_MapRestart_f

Completely restarts a level, but doesn't send a new gamestate to the clients.
This allows fair starts with variable load times.
================
*/
static void SV_MapRestart_f( void ) {
	int			delay;

	// make sure we aren't restarting twice in the same frame
	if ( com_frameTime == sv.restartedServerId ) {
		return;
	}

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( sv.restartTime != 0 ) {
		return;
	}

	if ( Cmd_Argc() > 1 ) {
		delay = SV_ParseInt( Cmd_Argv(1) );
	} else {
		delay = 5;
	}

	if ( delay != 0 && Cvar_VariableIntegerValue( "g_doWarmup" ) == 0 ) {
		sv.restartTime = sv.time + delay * 1000;
		if ( sv.restartTime == 0 ) {
			sv.restartTime = 1;
		}
		SV_SetConfigstring( CS_WARMUP, va( "%i", sv.restartTime ) );
		return;
	}

	// check for changes in variables that can't just be restarted
	// check for maxclients change
	if ( sv_maxclients->modified || sv_gametype->modified || sv_pure->modified ) {
		std::array<char, MAX_QPATH> mapname{};

		Com_Printf( "variable change -- restarting.\n" );
		// restart the map the slow way
		Q_strncpyz( mapname.data(), Cvar_VariableString( "mapname" ), SV_ArraySize(mapname) );

		SV_SpawnServer( mapname.data(), qfalse );
		return;
	}

	// toggle the server bit so clients can detect that a
	// map_restart has happened
	svs.snapFlagServerBit ^= SNAPFLAG_SERVERCOUNT;

	// generate a new restartedServerid
	sv.restartedServerId = com_frameTime;

	// if a map_restart occurs while a client is changing maps, we need
	// to give them the correct time so that when they finish loading
	// they don't violate the backwards time check in cl_cgame.c
	for ( client_t &client : SV_Clients() ) {
		if ( client.state == CS_PRIMED ) {
			client.oldServerTime = sv.restartTime;
		}
	}

	// reset all the vm data in place without changing memory allocation
	// note that we do NOT set sv.state = SS_LOADING, so configstrings that
	// had been changed from their default values will generate broadcast updates
	sv.state = SS_LOADING;
	sv.restarting = qtrue;

	// make sure that level time is not zero
	//sv.time = sv.time ? sv.time : 8;

	SV_RestartGameProgs();

	// run a few frames to allow everything to settle
	for ( int ignored : SV_Indices( 3 ) )
	{
		(void)ignored;
		Cbuf_Wait();
		sv.time += 100;
		VM_Call( gvm, 1, GAME_RUN_FRAME, sv.time );
	}

	sv.state = SS_GAME;
	sv.restarting = qfalse;

	// connect and begin all the clients
	for ( SV_ClientSlot slot : SV_ClientSlots() ) {
		// send the new gamestate to all connected clients
		if ( slot.client.state < CS_CONNECTED ) {
			continue;
		}

		const bool isBot = slot.client.netchan.remoteAddress.type == NA_BOT;

		// add the map_restart command
		SV_AddServerCommand( &slot.client, "map_restart\n" );

		// connect the client again, without the firstTime flag
		const char *denied = static_cast<const char *>( GVM_ArgPtr( VM_Call( gvm, 3, GAME_CLIENT_CONNECT, slot.index, qfalse, isBot ) ) );
		if ( denied ) {
			// this generally shouldn't happen, because the client
			// was connected before the level change
			SV_DropClient( &slot.client, denied );
			Com_Printf( "SV_MapRestart_f(%d): dropped client %i - denied!\n", delay, slot.index );
			continue;
		}

		if ( slot.client.state == CS_ACTIVE ) {
			SV_ClientEnterWorld( &slot.client );
		}
	}

	// run another frame to allow things to look at all the players
	Cbuf_Wait();
	sv.time += 100;
	VM_Call( gvm, 1, GAME_RUN_FRAME, sv.time );
	svs.time += 100;

	// cycle per-match demo recordings: finalize the old demo and start a new one
	if ( sv_autoRecordDemos && sv_autoRecordDemos->integer >= 3 ) {
		for ( client_t &client : SV_Clients() ) {
			if ( client.state < CS_CONNECTED ) {
				continue;
			}
			SV_StopDemoRecord( &client, qfalse );
			SV_StartDemoRecord( &client );
		}
	}

	for ( client_t &client : SV_Clients() ) {
		if ( client.state >= CS_PRIMED ) {
			// accept usercmds starting from current server time only
			// to emulate original behavior which dropped pre-restart commands via serverid check
			client.lastUsercmd = {};
			client.lastUsercmd.serverTime = sv.time - 1;
		}
	}
}


/*
==================
SV_Kick_f

Kick a user off of the server  FIXME: move to game
==================
*/
static void SV_Kick_f( void ) {
	client_t	*cl;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc() != 2 ) {
		Com_Printf ("Usage: kick <player name>\nkick all = kick everyone\nkick allbots = kick all bots\n");
		return;
	}

	cl = SV_GetPlayerByHandle();
	if ( !cl ) {
		if ( !Q_stricmp( Cmd_Argv( 1 ), "all" ) ) {
			for ( client_t &client : SV_Clients() ) {
				if ( client.state < CS_CONNECTED ) {
					continue;
				}
				if ( client.netchan.remoteAddress.type == NA_LOOPBACK ) {
					continue;
				}
				SV_DropClient( &client, "was kicked" );
				client.lastPacketTime = svs.time;	// in case there is a funny zombie
			}
		}
		else if ( !Q_stricmp( Cmd_Argv( 1 ), "allbots" ) ) {
			for ( client_t &client : SV_Clients() ) {
				if ( client.state < CS_CONNECTED ) {
					continue;
				}
				if ( client.netchan.remoteAddress.type != NA_BOT ) {
					continue;
				}
				SV_DropClient( &client, "was kicked" );
				client.lastPacketTime = svs.time;	// in case there is a funny zombie
			}
		}
		return;
	}
	if ( cl->netchan.remoteAddress.type == NA_LOOPBACK ) {
		Com_Printf( "Cannot kick host player\n" );
		return;
	}

	SV_DropClient( cl, "was kicked" );
	cl->lastPacketTime = svs.time;	// in case there is a funny zombie
}

/*
==================
SV_KickBots_f

Kick all bots off of the server
==================
*/
static void SV_KickBots_f( void ) {
	client_t	*cl;
	int			i;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf("Server is not running.\n");
		return;
	}

	for( i = 0, cl = svs.clients; i < sv.maxclients; i++, cl++ ) {
		if ( cl->state < CS_CONNECTED ) {
			continue;
		}

		if ( cl->netchan.remoteAddress.type != NA_BOT ) {
			continue;
		}

		SV_DropClient( cl, "was kicked" );
		cl->lastPacketTime = svs.time; // in case there is a funny zombie
	}
}
/*
==================
SV_KickAll_f

Kick all users off of the server
==================
*/
static void SV_KickAll_f( void ) {
	client_t *cl;
	int i;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	for( i = 0, cl = svs.clients; i < sv.maxclients; i++, cl++ ) {
		if ( cl->state < CS_CONNECTED ) {
			continue;
		}

		if ( cl->netchan.remoteAddress.type == NA_LOOPBACK ) {
			continue;
		}

		SV_DropClient( cl, "was kicked" );
		cl->lastPacketTime = svs.time; // in case there is a funny zombie
	}
}

/*
==================
SV_KickNum_f

Kick a user off of the server
==================
*/
static void SV_KickNum_f( void ) {
	client_t	*cl;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc() != 2 ) {
		Com_Printf ("Usage: %s <client number>\n", Cmd_Argv(0));
		return;
	}

	cl = SV_GetPlayerByNum();
	if ( !cl ) {
		return;
	}
	if ( cl->netchan.remoteAddress.type == NA_LOOPBACK ) {
		Com_Printf("Cannot kick host player\n");
		return;
	}

	SV_DropClient( cl, "was kicked" );
	cl->lastPacketTime = svs.time;	// in case there is a funny zombie
}

#ifndef STANDALONE
// these functions require the auth server which of course is not available anymore for stand-alone games.

#ifdef USE_BANS
static bool SV_ResolveAuthorizeAddress( void ) {
	// look up the authorize server's IP
	if ( !svs.authorizeAddress.ipv._4[0] && svs.authorizeAddress.type != NA_BAD ) {
		Com_Printf( "Resolving %s\n", AUTHORIZE_SERVER_NAME );
		if ( !NET_StringToAdr( AUTHORIZE_SERVER_NAME, &svs.authorizeAddress, NA_IP ) ) {
			Com_Printf( "Couldn't resolve address\n" );
			return false;
		}
		svs.authorizeAddress.port = BigShort( PORT_AUTHORIZE );
		Com_Printf( "%s resolved to %i.%i.%i.%i:%i\n", AUTHORIZE_SERVER_NAME,
			svs.authorizeAddress.ipv._4[0], svs.authorizeAddress.ipv._4[1],
			svs.authorizeAddress.ipv._4[2], svs.authorizeAddress.ipv._4[3],
			BigShort( svs.authorizeAddress.port ) );
	}

	return svs.authorizeAddress.type != NA_BAD;
}

static void SV_BanClientThroughAuthorize( const client_t *cl ) {
	if ( !SV_ResolveAuthorizeAddress() ) {
		return;
	}

	NET_OutOfBandPrint( NS_SERVER, &svs.authorizeAddress,
		"banUser %i.%i.%i.%i", cl->netchan.remoteAddress.ipv._4[0], cl->netchan.remoteAddress.ipv._4[1],
							   cl->netchan.remoteAddress.ipv._4[2], cl->netchan.remoteAddress.ipv._4[3] );
	Com_Printf("%s was banned from coming back\n", cl->name);
}

/*
==================
SV_Ban_f

Ban a user from being able to play on this server through the auth
server
==================
*/
static void SV_Ban_f( void ) {
	client_t	*cl;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc() != 2 ) {
		Com_Printf ("Usage: banUser <player name>\n");
		return;
	}

	cl = SV_GetPlayerByHandle();

	if (!cl) {
		return;
	}

	if( cl->netchan.remoteAddress.type == NA_LOOPBACK ) {
		Com_Printf("Cannot kick host player\n");
		return;
	}

	SV_BanClientThroughAuthorize( cl );
}

/*
==================
SV_BanNum_f

Ban a user from being able to play on this server through the auth
server
==================
*/
static void SV_BanNum_f( void ) {
	client_t	*cl;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc() != 2 ) {
		Com_Printf ("Usage: banClient <client number>\n");
		return;
	}

	cl = SV_GetPlayerByNum();
	if ( !cl ) {
		return;
	}
	if( cl->netchan.remoteAddress.type == NA_LOOPBACK ) {
		Com_Printf("Cannot kick host player\n");
		return;
	}

	SV_BanClientThroughAuthorize( cl );
}

#endif // USE_BANS
#endif // !COM_STANDALONE

#ifdef USE_BANS
static int SV_DefaultSubnetForAddress( const netadr_t &address );
static int SV_NormalizeSubnetForAddress( const netadr_t &address, int subnet );

/*
==================
SV_RehashBans_f

Load saved bans from file.
==================
*/
static void SV_RehashBans_f(void)
{
	int index, filelen;
	fileHandle_t readfrom;
	char *textbuf, *curpos, *maskpos, *newlinepos;
	const char *endpos;
	std::array<char, MAX_QPATH> filepath{};
	
	// make sure server is running
	if ( !com_sv_running->integer ) {
		return;
	}
	
	serverBansCount = 0;
	
	if(!sv_banFile->string || !*sv_banFile->string)
		return;

	Com_sprintf( filepath.data(), SV_ArraySize(filepath), "%s/%s", FS_GetCurrentGameDir(), sv_banFile->string );

	if((filelen = FS_SV_FOpenFileRead(filepath.data(), &readfrom)) >= 0)
	{
		const auto closeRead = SV_MakeScopeExit( [&]() { SV_CloseFileHandle( readfrom ); } );

		if(filelen < 2)
		{
			// Don't bother if file is too short.
			return;
		}

		curpos = textbuf = SV_ZMallocArray<char>( filelen );
		const auto freeText = SV_MakeScopeExit( [&]() { SV_ZFree( textbuf ); } );
		
		filelen = FS_Read(textbuf, filelen, readfrom);
		
		endpos = textbuf + filelen;
		
		index = 0;
		while(index < SERVER_MAXBANS && curpos + 2 < endpos)
		{
			// find the end of the address string
			for(maskpos = curpos + 2; maskpos < endpos && *maskpos != ' '; maskpos++);
			
			if(maskpos + 1 >= endpos)
				break;

			*maskpos = '\0';
			maskpos++;
			
			// find the end of the subnet specifier
			for(newlinepos = maskpos; newlinepos < endpos && *newlinepos != '\n'; newlinepos++);
			
			if(newlinepos >= endpos)
				break;
			
			*newlinepos = '\0';
			
			serverBan_t &ban = SV_BanForIndex( index );

			if(NET_StringToAdr(curpos + 2, &ban.ip, NA_UNSPEC))
			{
				ban.isexception = SV_QBool( curpos[0] != '0' );
				ban.subnet = SV_ParseInt(maskpos);
				
				ban.subnet = SV_NormalizeSubnetForAddress( ban.ip, ban.subnet );
			}
			
			curpos = newlinepos + 1;
			index++;
		}
			
		serverBansCount = index;
	}
}

/*
==================
SV_WriteBans

Save bans to file.
==================
*/
static void SV_WriteBans(void)
{
	fileHandle_t writeto;
	std::array<char, MAX_QPATH> filepath{};
	
	if(!sv_banFile->string || !*sv_banFile->string)
		return;
	
	Com_sprintf( filepath.data(), SV_ArraySize(filepath), "%s/%s", FS_GetCurrentGameDir(), sv_banFile->string );

	if((writeto = FS_SV_FOpenFileWrite(filepath.data())))
	{
		const auto closeWrite = SV_MakeScopeExit( [&]() { SV_CloseFileHandle( writeto ); } );
		std::array<char, 128> writebuf{};
		
		for( const serverBan_t &curban : SV_Bans() )
		{
			Com_sprintf( writebuf.data(), SV_ArraySize(writebuf), "%d %s %d\n",
				    curban.isexception, NET_AdrToString(&curban.ip), curban.subnet);
			FS_Write( writebuf.data(), strlen( writebuf.data() ), writeto );
		}
	}
}

/*
==================
SV_DelBanEntryFromList

Remove a ban or an exception from the list.
==================
*/

static bool SV_DelBanEntryFromList(int index)
{
	if(index == serverBansCount - 1)
		serverBansCount--;
	else if(index < SV_ArraySize(serverBans) - 1)
	{
		for ( int next = index + 1; next < serverBansCount; ++next ) {
			SV_BanForIndex( next - 1 ) = SV_BanForIndex( next );
		}
		serverBansCount--;
	}
	else
		return true;

	return false;
}

static int SV_DefaultSubnetForAddress( const netadr_t &address )
{
#ifdef USE_IPV6
	return ( address.type == NA_IP6 ) ? 128 : 32;
#else
	return 32;
#endif
}

static int SV_NormalizeSubnetForAddress( const netadr_t &address, int subnet )
{
	const int defaultSubnet = SV_DefaultSubnetForAddress( address );
	return ( subnet < 1 || subnet > defaultSubnet ) ? defaultSubnet : subnet;
}

/*
==================
SV_ParseCIDRNotation

Parse a CIDR notation type string and return a netadr_t and suffix by reference
==================
*/

static bool SV_ParseCIDRNotation(netadr_t *dest, int *mask, const char *adrstr)
{
	std::array<char, NET_ADDRSTRMAXLEN> address{};
	char *suffix;

	Q_strncpyz( address.data(), adrstr, SV_ArraySize(address) );
	suffix = strchr(address.data(), '/');
	if(suffix)
	{
		*suffix = '\0';
		suffix++;
	}

	if(!NET_StringToAdr(address.data(), dest, NA_UNSPEC))
		return true;

	if(suffix)
	{
		*mask = SV_NormalizeSubnetForAddress( *dest, SV_ParseInt(suffix) );
	}
	else
		*mask = SV_DefaultSubnetForAddress( *dest );
	
	return false;
}

/*
==================
SV_AddBanToList

Ban a user from being able to play on this server based on his ip address.
==================
*/

static void SV_AddBanToList( bool isexception )
{
	const char *banstring;
	std::array<char, NET_ADDRSTRMAXLEN> addy2{};
	netadr_t ip;
	int index, argc, mask;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	argc = Cmd_Argc();
	
	if(argc < 2 || argc > 3)
	{
		Com_Printf ("Usage: %s (ip[/subnet] | clientnum [subnet])\n", Cmd_Argv(0));
		return;
	}

	if(serverBansCount >= SV_ArraySize(serverBans))
	{
		Com_Printf ("Error: Maximum number of bans/exceptions exceeded.\n");
		return;
	}

	banstring = Cmd_Argv(1);
	
	if(strchr(banstring, '.') || strchr(banstring, ':'))
	{
		// This is an ip address, not a client num.
		
		if(SV_ParseCIDRNotation(&ip, &mask, banstring))
		{
			Com_Printf("Error: Invalid address %s\n", banstring);
			return;
		}
	}
	else
	{
		client_t *cl;
		
		// client num.
		
		cl = SV_GetPlayerByNum();

		if(!cl)
		{
			Com_Printf("Error: Playernum %s does not exist.\n", Cmd_Argv(1));
			return;
		}
		
		ip = cl->netchan.remoteAddress;
		
		if(argc == 3)
		{
			mask = SV_NormalizeSubnetForAddress( ip, SV_ParseInt(Cmd_Argv(2)) );
		}
		else
			mask = SV_DefaultSubnetForAddress( ip );
	}

	if(ip.type != NA_IP && ip.type != NA_IP6)
	{
		Com_Printf("Error: Can ban players connected via the internet only.\n");
		return;
	}

	// first check whether a conflicting ban exists that would supersede the new one.
	for( const serverBan_t &curban : SV_Bans() )
	{
		if(curban.subnet <= mask)
		{
			if((curban.isexception || !isexception) && NET_CompareBaseAdrMask(&curban.ip, &ip, curban.subnet))
			{
				Q_strncpyz(addy2.data(), NET_AdrToString(&ip), SV_ArraySize(addy2));
				
				Com_Printf("Error: %s %s/%d supersedes %s %s/%d\n", curban.isexception ? "Exception" : "Ban",
					   NET_AdrToString(&curban.ip), curban.subnet,
					   isexception ? "exception" : "ban", addy2.data(), mask);
				return;
			}
		}
		if(curban.subnet >= mask)
		{
			if(!curban.isexception && isexception && NET_CompareBaseAdrMask(&curban.ip, &ip, mask))
			{
				Q_strncpyz(addy2.data(), NET_AdrToString(&curban.ip), SV_ArraySize(addy2));
			
				Com_Printf("Error: %s %s/%d supersedes already existing %s %s/%d\n", isexception ? "Exception" : "Ban",
					   NET_AdrToString(&ip), mask,
					   curban.isexception ? "exception" : "ban", addy2.data(), curban.subnet);
				return;
			}
		}
	}

	// now delete bans that are superseded by the new one
	index = 0;
	while(index < serverBansCount)
	{
		const serverBan_t &curban = SV_BanForIndex( index );
		
		if(curban.subnet > mask && (!curban.isexception || isexception) && NET_CompareBaseAdrMask(&curban.ip, &ip, mask))
			SV_DelBanEntryFromList(index);
		else
			index++;
	}

	serverBan_t &newBan = SV_BanForIndex( serverBansCount );
	newBan.ip = ip;
	newBan.subnet = mask;
	newBan.isexception = SV_QBool( isexception );
	
	serverBansCount++;
	
	SV_WriteBans();

	Com_Printf("Added %s: %s/%d\n", isexception ? "ban exception" : "ban",
		   NET_AdrToString(&ip), mask);
}

/*
==================
SV_DelBanFromList

Remove a ban or an exception from the list.
==================
*/

static void SV_DelBanFromList( bool isexception )
{
	int index, count = 0, todel, mask;
	netadr_t ip;
	const char *banstring;
	
	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}
	
	if(Cmd_Argc() != 2)
	{
		Com_Printf ("Usage: %s (ip[/subnet] | num)\n", Cmd_Argv(0));
		return;
	}

	banstring = Cmd_Argv(1);
	
	if(strchr(banstring, '.') || strchr(banstring, ':'))
	{
		if(SV_ParseCIDRNotation(&ip, &mask, banstring))
		{
			Com_Printf("Error: Invalid address %s\n", banstring);
			return;
		}
		
		index = 0;
		
		while(index < serverBansCount)
		{
			serverBan_t &curban = SV_BanForIndex( index );
			
			if( SV_AsBool( curban.isexception ) == isexception		&&
			   curban.subnet >= mask 			&&
			   NET_CompareBaseAdrMask(&curban.ip, &ip, mask))
			{
				Com_Printf("Deleting %s %s/%d\n",
					   isexception ? "exception" : "ban",
					   NET_AdrToString(&curban.ip), curban.subnet);
					   
				SV_DelBanEntryFromList(index);
			}
			else
				index++;
		}
	}
	else
	{
		todel = SV_ParseInt(Cmd_Argv(1));

		if(todel < 1 || todel > serverBansCount)
		{
			Com_Printf("Error: Invalid ban number given\n");
			return;
		}
	
		for( SV_BanSlot slot : SV_BanSlots() )
		{
			if( SV_AsBool( slot.ban.isexception ) == isexception)
			{
				count++;
			
				if(count == todel)
				{
					Com_Printf("Deleting %s %s/%d\n",
					   isexception ? "exception" : "ban",
					   NET_AdrToString(&slot.ban.ip), slot.ban.subnet);

					SV_DelBanEntryFromList(slot.index);

					break;
				}
			}
		}
	}
	
	SV_WriteBans();
}


/*
==================
SV_ListBans_f

List all bans and exceptions on console
==================
*/

static void SV_ListBans_f(void)
{
	int count;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}
	
	// List all bans
	count = 0;
	for( const serverBan_t &ban : SV_Bans() )
	{
		if(!ban.isexception)
		{
			count++;

			Com_Printf("Ban #%d: %s/%d\n", count,
				    NET_AdrToString(&ban.ip), ban.subnet);
		}
	}
	// List all exceptions
	count = 0;
	for( const serverBan_t &ban : SV_Bans() )
	{
		if(ban.isexception)
		{
			count++;

			Com_Printf("Except #%d: %s/%d\n", count,
				    NET_AdrToString(&ban.ip), ban.subnet);
		}
	}
}

/*
==================
SV_FlushBans_f

Delete all bans and exceptions.
==================
*/

static void SV_FlushBans_f(void)
{
	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	serverBansCount = 0;
	
	// empty the ban file.
	SV_WriteBans();
	
	Com_Printf("All bans and exceptions have been deleted.\n");
}

static void SV_BanAddr_f(void)
{
	SV_AddBanToList( false );
}

static void SV_ExceptAddr_f(void)
{
	SV_AddBanToList( true );
}

static void SV_BanDel_f(void)
{
	SV_DelBanFromList( false );
}

static void SV_ExceptDel_f(void)
{
	SV_DelBanFromList( true );
}

#endif // USE_BANS

/*
** SV_Strlen -- skips color escape codes
*/
int SV_Strlen( const char *str ) {
	const char *s = str;
	int count = 0;

	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			s += 2;
		} else {
			count++;
			s++;
		}
	}

	return count;
}

static void SV_PrintRepeatedChar( char value, int count ) {
	for ( int ignored : SV_Indices( count ) ) {
		(void)ignored;
		Com_Printf( "%c", value );
	}
}


/*
================
SV_Status_f
================
*/
static void SV_Status_f( void ) {
	int l;
	const client_t *cl;
	const playerState_t *ps;
	const char *s;
	int max_namelength;
	int max_addrlength;
	std::array<char, MAX_CLIENTS * MAX_NAME_LENGTH> names{};
	std::array<char, MAX_CLIENTS * 48> addrs{};
	std::array<const char *, MAX_CLIENTS> np{};
	std::array<const char *, MAX_CLIENTS> ap{};
	std::array<int, MAX_CLIENTS> nl{};
	std::array<int, MAX_CLIENTS> al{};
	char *nc;
	char *ac;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	max_namelength = 4; // strlen( "name" )
	max_addrlength = 7; // strlen( "address" )

	nc = names.data(); *nc = '\0';
	ac = addrs.data(); *ac = '\0';

	// first pass: save and determine max.lengths of name/address fields
	for ( SV_ClientSlot slot : SV_ClientSlots() )
	{
		client_t *cl = &slot.client;

		if ( cl->state == CS_FREE )
			continue;

		l = strlen( cl->name ) + 1;
		Q_strncpyz( nc, cl->name, SV_ArraySize(names) - static_cast<int>( nc - names.data() ) );
		np[ slot.index ] = nc; nc += l;			// name pointer in name buffer
		nl[ slot.index ] = SV_Strlen( cl->name );// name length without color sequences
		if ( nl[ slot.index ] > max_namelength )
			max_namelength = nl[ slot.index ];

		s = NET_AdrToString( &cl->netchan.remoteAddress );
		l = strlen( s ) + 1;
		Q_strncpyz( ac, s, SV_ArraySize(addrs) - static_cast<int>( ac - addrs.data() ) );
		ap[ slot.index ] = ac; ac += l;			// address pointer in address buffer
		al[ slot.index ] = l - 1;				// address length
		if ( al[ slot.index ] > max_addrlength )
			max_addrlength = al[ slot.index ];
	}

	Com_Printf( "map: %s\n", sv_mapname->string );

#if 0
	Com_Printf( "cl score ping name                        address                     rate\n" );
	Com_Printf( "-- ----- ---- --------------------------- --------------------------- -----\n" );
#else // variable-length fields
	Com_Printf( "cl score ping name" );
	SV_PrintRepeatedChar( ' ', max_namelength - 4 );
	Com_Printf( " address" );
	SV_PrintRepeatedChar( ' ', max_addrlength - 7 );
	Com_Printf( " rate\n" );

	Com_Printf( "-- ----- ---- " );
	SV_PrintRepeatedChar( '-', max_namelength );
	Com_Printf( " " );
	SV_PrintRepeatedChar( '-', max_addrlength );
	Com_Printf( " -----\n" );
#endif

	for ( SV_ClientSlot slot : SV_ClientSlots() )
	{
		cl = &slot.client;
		if ( cl->state == CS_FREE )
			continue;

		Com_Printf( "%2i ", slot.index ); // id
		ps = SV_GameClientNum( slot.index );
		Com_Printf( "%5i ", ps->persistant[PERS_SCORE] );

		// ping/status
		if ( cl->state == CS_PRIMED )
			Com_Printf( " PRM " );
		else if ( cl->state == CS_CONNECTED )
			Com_Printf( " CON " );
		else if ( cl->state == CS_ZOMBIE )
			Com_Printf( " ZMB " );
		else
			Com_Printf( "%4i ", cl->ping < 999 ? cl->ping : 999 );
	
		// variable-length name field
		s = np[ slot.index ];
		Com_Printf( "%s", s );
		l = max_namelength - nl[ slot.index ];
		SV_PrintRepeatedChar( ' ', l );

		// variable-length address field
		s = ap[ slot.index ];
		Com_Printf( S_COLOR_WHITE " %s", s );
		l = max_addrlength - al[ slot.index ];
		SV_PrintRepeatedChar( ' ', l );

		// rate
		Com_Printf( " %5i\n", cl->rate );
	}

	Com_Printf( "\n" );
}


/*
==================
SV_ConSay_f
==================
*/
static void SV_ConSay_f( void ) {
	char	*p;
	std::array<char, MAX_STRING_CHARS> text{};
	int		len;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc () < 2 ) {
		return;
	}

	p = Cmd_ArgsFrom( 1 );
	len = static_cast<int>( strlen( p ) );

	if ( len > 1000 ) {
		return;
	}

	if ( *p == '"' ) {
		p[len-1] = '\0';
		p++;
	}

	Com_sprintf( text.data(), SV_ArraySize(text), "console: %s", p );

	SV_SendServerCommand( nullptr, "chat \"%s\"", text.data() );
}


/*
==================
SV_ConTell_f
==================
*/
static void SV_ConTell_f( void ) {
	char	*p;
	std::array<char, MAX_STRING_CHARS> text{};
	client_t	*cl;
	int		len;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc() < 3 ) {
		Com_Printf( "Usage: tell <client number> <text>\n" );
		return;
	}

	cl = SV_GetPlayerByNum();
	if ( !cl ) {
		return;
	}

	p = Cmd_ArgsFrom( 2 );
	len = static_cast<int>( strlen( p ) );

	if ( len > 1000 ) {
		return;
	}

	if ( *p == '"' ) {
		p[len-1] = '\0';
		p++;
	}

	Com_sprintf( text.data(), SV_ArraySize(text), S_COLOR_MAGENTA "console: %s", p );

	Com_Printf( "%s\n", text.data() );
	SV_SendServerCommand( cl, "chat \"%s\"", text.data() );
}


/*
==================
SV_Heartbeat_f

Also called by SV_DropClient, SV_DirectConnect, and SV_SpawnServer
==================
*/
void SV_Heartbeat_f( void ) {
	svs.nextHeartbeatTime = svs.time;
}


/*
===========
SV_Serverinfo_f

Examine the serverinfo string
===========
*/
static void SV_Serverinfo_f( void ) {
	const char *info;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	Com_Printf ("Server info settings:\n");
	info = sv.configstrings[ CS_SERVERINFO ];
	if ( info ) {
		Info_Print( info );
	}
}


/*
===========
SV_Systeminfo_f

Examine the systeminfo string
===========
*/
static void SV_Systeminfo_f( void ) {
	const char *info;
	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}
	Com_Printf( "System info settings:\n" );
	info = sv.configstrings[ CS_SYSTEMINFO ];
	if ( info ) {
		Info_Print( info );
	}
}


/*
===========
SV_DumpUser_f

Examine all a users info strings
===========
*/
static void SV_DumpUser_f( void ) {
	client_t	*cl;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc() != 2 ) {
		Com_Printf ("Usage: dumpuser <userid>\n");
		return;
	}

	cl = SV_GetPlayerByHandle();
	if ( !cl ) {
		return;
	}

	Com_Printf( "userinfo\n" );
	Com_Printf( "--------\n" );
	Info_Print( cl->userinfo );
}


/*
=================
SV_KillServer
=================
*/
static void SV_KillServer_f( void ) {
	SV_Shutdown( "killserver" );
}


/*
=================
SV_Locations
=================
*/
static void SV_Locations_f( void ) {

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( !sv_clientTLD->integer ) {
		Com_Printf( "Disabled on this server.\n" );
		return;
	}

	SV_PrintLocations_f( nullptr );
}

//===========================================================

/*
==================
SV_CompleteMapName
==================
*/
static void SV_CompleteMapName( const char *args, int argNum ) {
	if ( argNum == 2 ) 	{
		if ( sv.pure != 0 ) {
			Field_CompleteFilename( "maps", "bsp", qtrue, FS_MATCH_PK3s | FS_MATCH_STICK );
		} else {
			Field_CompleteFilename( "maps", "bsp", qtrue, FS_MATCH_ANY | FS_MATCH_STICK );
		}
	}
}

struct operatorCommand_t {
	const char *name;
	xcommand_t function;
};

template <std::size_t N>
static void SV_AddCommandSet( const std::array<operatorCommand_t, N> &commands )
{
	for ( const auto &command : commands ) {
		Cmd_AddCommand( command.name, command.function );
	}
}

template <std::size_t N>
static void SV_SetMapCompletionForCommands( const std::array<operatorCommand_t, N> &commands )
{
	for ( const auto &command : commands ) {
		Cmd_SetCommandCompletionFunc( command.name, SV_CompleteMapName );
	}
}


/*
==================
SV_AddOperatorCommands
==================
*/
void SV_AddOperatorCommands( void ) {
	static bool	initialized;

	if ( initialized ) {
		return;
	}
	initialized = true;

	static constexpr std::array<operatorCommand_t, 13> commands = {{
		{ "heartbeat", SV_Heartbeat_f },
		{ "kick", SV_Kick_f },
		{ "kickbots", SV_KickBots_f },
		{ "kickall", SV_KickAll_f },
		{ "kicknum", SV_KickNum_f },
		{ "clientkick", SV_KickNum_f },
		{ "status", SV_Status_f },
		{ "dumpuser", SV_DumpUser_f },
		{ "map_restart", SV_MapRestart_f },
		{ "sectorlist", SV_SectorList_f },
		{ "killserver", SV_KillServer_f },
		{ "filter", SV_AddFilter_f },
		{ "filtercmd", SV_AddFilterCmd_f },
	}};
	static constexpr std::array<operatorCommand_t, 1> mapCommands = {{
		{ "map", SV_Map_f },
	}};
#ifndef PRE_RELEASE_DEMO
	static constexpr std::array<operatorCommand_t, 3> extraMapCommands = {{
		{ "devmap", SV_Map_f },
		{ "spmap", SV_Map_f },
		{ "spdevmap", SV_Map_f },
	}};
#endif
#ifdef USE_BANS
	static constexpr std::array<operatorCommand_t, 7> banCommands = {{
		{ "rehashbans", SV_RehashBans_f },
		{ "listbans", SV_ListBans_f },
		{ "banaddr", SV_BanAddr_f },
		{ "exceptaddr", SV_ExceptAddr_f },
		{ "bandel", SV_BanDel_f },
		{ "exceptdel", SV_ExceptDel_f },
		{ "flushbans", SV_FlushBans_f },
	}};
#ifndef STANDALONE
	static constexpr std::array<operatorCommand_t, 2> accountBanCommands = {{
		{ "banUser", SV_Ban_f },
		{ "banClient", SV_BanNum_f },
	}};
#endif
#endif

	SV_AddCommandSet( commands );
#ifndef STANDALONE
#ifdef USE_BANS
	if(!Cvar_VariableIntegerValue("com_standalone"))
	{
		SV_AddCommandSet( accountBanCommands );
	}
#endif
#endif
	SV_AddCommandSet( mapCommands );
	SV_SetMapCompletionForCommands( mapCommands );
#ifndef PRE_RELEASE_DEMO
	SV_AddCommandSet( extraMapCommands );
	SV_SetMapCompletionForCommands( extraMapCommands );
#endif
#ifdef USE_BANS	
	SV_AddCommandSet( banCommands );
#endif
}


/*
==================
SV_RemoveOperatorCommands
==================
*/
void SV_RemoveOperatorCommands( void ) {
#if 0
	// removing these won't let the server start again
	Cmd_RemoveCommand ("heartbeat");
	Cmd_RemoveCommand ("kick");
	Cmd_RemoveCommand ("kicknum");
	Cmd_RemoveCommand ("clientkick");
	Cmd_RemoveCommand ("kickall");
	Cmd_RemoveCommand ("kickbots");
	Cmd_RemoveCommand ("banUser");
	Cmd_RemoveCommand ("banClient");
	Cmd_RemoveCommand ("status");
	Cmd_RemoveCommand ("dumpuser");
	Cmd_RemoveCommand ("map_restart");
	Cmd_RemoveCommand ("sectorlist");
#endif
}


void SV_AddDedicatedCommands( void )
{
	static constexpr std::array<operatorCommand_t, 5> commands = {{
		{ "serverinfo", SV_Serverinfo_f },
		{ "systeminfo", SV_Systeminfo_f },
		{ "tell", SV_ConTell_f },
		{ "say", SV_ConSay_f },
		{ "locations", SV_Locations_f },
	}};

	SV_AddCommandSet( commands );
}


void SV_RemoveDedicatedCommands( void )
{
	Cmd_RemoveCommand( "serverinfo" );
	Cmd_RemoveCommand( "systeminfo" );
	Cmd_RemoveCommand( "tell" );
	Cmd_RemoveCommand( "say" );
	Cmd_RemoveCommand( "locations" );
}
