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
// sv_client.cpp -- server code for dealing with clients

#include "server.h"
#include "../qcommon/protocol_contract.hpp"
#include "../qcommon/netchan_safety.hpp"
#include "../platform/fnql_steam.h"
#include "retail_auth_challenge.hpp"
#include "stats_contract.hpp"
#include "stats_event.hpp"
#include "stats_report.hpp"
#include "stats_session.hpp"
#include "json_document.hpp"

#include <array>
#include <cstdint>
#include <limits>

namespace {

constexpr std::size_t PendingRetailAuthCapacity = 256;
fnql::server::auth::AuthCache<PendingRetailAuthCapacity> pendingRetailAuth;
std::array<fnql::server::stats::Session, MAX_CLIENTS> clientStatsSessions;
fnql::server::stats::ReportAccumulator statsReports;

fnql::server::auth::AddressKey SV_AuthAddressKey( const netadr_t *address ) {
	fnql::server::auth::AddressKey key{};
	if ( !address ) {
		return key;
	}

	auto append = [&key]( std::uint8_t value ) {
		if ( key.used < key.bytes.size() ) {
			key.bytes[key.used++] = value;
		}
	};
	append( static_cast<std::uint8_t>( address->type ) );
	append( static_cast<std::uint8_t>( address->port ) );
	append( static_cast<std::uint8_t>( address->port >> 8u ) );
	if ( address->type == NA_IP ) {
		for ( std::uint8_t value : address->ipv._4 ) {
			append( value );
		}
	}
#ifdef USE_IPV6
	else if ( address->type == NA_IP6 || address->type == NA_MULTICAST6 ) {
		for ( std::uint8_t value : address->ipv._6 ) {
			append( value );
		}
		for ( unsigned int shift = 0; shift < 32; shift += 8 ) {
			append( static_cast<std::uint8_t>( address->scope_id >> shift ) );
		}
	}
#endif
	return key;
}

} // namespace

static void SV_CloseDownload( client_t *cl );

static int SV_DownloadWindow( const client_t *client ) {
	return static_cast<int>( fnql::protocol::ForWireProfile(
		client->netchan.wireProfile ).limits.downloadWindow );
}

static int SV_DownloadBlockBytes( const client_t *client ) {
	return static_cast<int>( fnql::protocol::ForWireProfile(
		client->netchan.wireProfile ).limits.downloadBlockBytes );
}

static bool SV_IsPackExtension( const char *ext )
{
	return !Q_stricmp( ext, "pk3" ) || !Q_stricmp( ext, "pak" );
}

static void SV_StripPackExtension( const char *in, char *out, int destsize )
{
	Q_strncpyz( out, in, destsize );
	if ( COM_CompareExtension( out, ".pk3" ) || COM_CompareExtension( out, ".pak" ) ) {
		COM_StripExtension( out, out, destsize );
	}
}

//
// Server-side Stateless Challenges
// backported from https://github.com/JACoders/OpenJK/pull/832
//

static constexpr int TS_SHIFT = 14; // ~16 seconds to reply to the challenge

/*
=================
SV_CreateChallenge

Create an unforgeable, temporal challenge for the given client address
=================
*/
static int SV_CreateChallenge( int timestamp, const netadr_t *from )
{
	int challenge;

	// Create an unforgeable, temporal challenge for this client using HMAC(secretKey, clientParams + timestamp)
	// Use first 4 bytes of the HMAC digest as an int (client only deals with numeric challenges)
	// The most-significant bit stores whether the timestamp is odd or even. This lets later verification code handle the
	// case where the engine timestamp has incremented between the time this challenge is sent and the client replies.
	challenge = Com_MD5Addr( from, timestamp );
	challenge &= 0x7FFFFFFF;
	challenge |= static_cast<unsigned int>( timestamp & 0x1 ) << 31;

	return challenge;
}


/*
=================
SV_CreateChallenge

Verify a challenge received by the client matches the expected challenge
=================
*/
static bool SV_VerifyChallenge( int receivedChallenge, const netadr_t *from )
{
	int currentTimestamp = svs.time >> TS_SHIFT;
	int currentPeriod = currentTimestamp & 0x1;

	// Use the current timestamp for verification if the current period matches the client challenge's period.
	// Otherwise, use the previous timestamp in case the current timestamp incremented in the time between the
	// client being sent a challenge and the client's reply that's being verified now.
	int challengePeriod = ((unsigned int)receivedChallenge >> 31) & 0x1;
	int challengeTimestamp = currentTimestamp - ( currentPeriod ^ challengePeriod );

	int expectedChallenge = SV_CreateChallenge( challengeTimestamp, from );

	return receivedChallenge == expectedChallenge;
}


/*
=================
SV_InitChallenger
=================
*/
void SV_InitChallenger( void )
{
	pendingRetailAuth.Clear();
	statsReports.Reset();
	Com_MD5Init();
}


/*
=================
SV_GetChallenge

A "getchallenge" OOB command has been received
Returns a challenge number that can be used
in a subsequent connectResponse command.
We do this to prevent denial of service attacks that
flood the server with invalid connection IPs.  With a
challenge, they must give a valid IP address.

If we are authorizing, a challenge request will cause a packet
to be sent to the authorize server.

When an authorizeip is returned, a challenge response will be
sent to that ip.

ioquake3: we added a possibility for clients to add a challenge
to their packets, to make it more difficult for malicious servers
to hi-jack client connections.
Also, the auth stuff is completely disabled for com_standalone games
as well as IPv6 connections, since there is no way to use the
v4-only auth server for these new types of connections.
=================
*/
void SV_GetChallenge( const netadr_t *from, const msg_t *msg ) {
	int		challenge;
	int		clientChallenge;

	// ignore if we are in single player
#ifndef DEDICATED
	if ( Cvar_VariableIntegerValue( "g_gametype" ) == GT_SINGLE_PLAYER || Cvar_VariableIntegerValue("ui_singlePlayerActive")) {
		return;
	}
#endif

	// Prevent using getchallenge as an amplifier
	if ( SVC_RateLimitAddress( from, 10, 1000 ) ) {
		if ( com_developer->integer ) {
			Com_Printf( "SV_GetChallenge: rate limit from %s exceeded, dropping request\n",
				NET_AdrToString( from ) );
		}
		return;
	}

	// Create a unique challenge for this client without storing state on the server
	challenge = SV_CreateChallenge( svs.time >> TS_SHIFT, from );

	const fnql::server::auth::ParsedChallenge retail =
		fnql::server::auth::ParseChallengePayload(
			msg ? reinterpret_cast<const std::uint8_t *>( msg->data ) : nullptr,
			msg && msg->cursize > 0 ? static_cast<std::size_t>( msg->cursize ) : 0u );
	if ( retail.kind == fnql::server::auth::ChallengePayloadKind::MalformedRetail ) {
		Com_DPrintf( "SV_GetChallenge: malformed retail auth payload from %s\n",
			NET_AdrToString( from ) );
		return;
	}
	if ( retail.kind == fnql::server::auth::ChallengePayloadKind::Retail ) {
		if ( !pendingRetailAuth.Store( SV_AuthAddressKey( from ), challenge,
			retail.steamId, retail.ticket, retail.ticketBytes,
			static_cast<std::uint32_t>( Sys_Milliseconds() ) ) ) {
			Com_DPrintf( "SV_GetChallenge: could not retain retail auth payload from %s\n",
				NET_AdrToString( from ) );
			return;
		}
		if ( retail.fnqlExtension ) {
			NET_OutOfBandPrint( NS_SERVER, from, "challengeResponse %i %i %i %s",
				challenge, static_cast<std::int32_t>( retail.clientChallenge ),
				com_protocol->integer,
				fnql::protocol::FnqlHandshakeMarker.data() );
		} else {
			NET_OutOfBandPrint( NS_SERVER, from, "challengeResponse %i", challenge );
		}
		return;
	}

	if ( Cmd_Argc() < 2 ) {
		// legacy client query, don't send unneeded information
		NET_OutOfBandPrint( NS_SERVER, from, "challengeResponse %i", challenge );
	} else {
		const int sv_proto = com_protocol->integer;

		// Grab the client's challenge to echo back (if given)
		clientChallenge = SV_ParseInt( Cmd_Argv( 1 ) );

		NET_OutOfBandPrint( NS_SERVER, from, "challengeResponse %i %i %i %s",
			challenge, clientChallenge, sv_proto,
			fnql::protocol::FnqlHandshakeMarker.data() );
	}
}


/*
==================
SV_IsBanned

Check whether a certain address is banned
==================
*/
#ifdef USE_BANS

static bool SV_IsBanned( const netadr_t *from, bool isexception )
{
	if(!isexception)
	{
		// If this is a query for a ban, first check whether the client is excepted
		if(SV_IsBanned(from, true))
			return false;
	}

	for( const serverBan_t &ban : SV_Bans() )
	{
		if( SV_AsBool( ban.isexception ) == isexception)
		{
			if(NET_CompareBaseAdrMask(&ban.ip, from, ban.subnet))
				return true;
		}
	}

	return false;
}
#endif


/*
==================
SV_SetClientTLD
==================
*/
#pragma pack(push,1)

struct iprange_t {
	uint32_t from;
	uint32_t to;
};

struct iprange_tld_t {
	char tld[2];
};

#pragma pack(pop)

static bool ipdb_loaded;
static iprange_t *ipdb_range;
static iprange_tld_t *ipdb_tld;
static int num_tlds;

struct tld_info_t {
	const char *tld;
	const char *country;
};

static constexpr tld_info_t tld_info[] = {
#include "tlds.h"
};

/*
==================
SV_FreeIP4DB
==================
*/
void SV_FreeIP4DB( void )
{
	iprange_t *allocation = ipdb_range;
	ipdb_range = nullptr;
	ipdb_loaded = false;
	ipdb_tld = nullptr;
	num_tlds = 0;
	SV_ZFree( allocation );
}


/*
==================
SV_LoadIP4DB

Loads geoip database into memory
==================
*/
static bool SV_LoadIP4DB( const char *filename )
{
	fileHandle_t fh = FS_INVALID_HANDLE;
	uint32_t last_ip;
	byte *buf;
	int len;

	len = FS_SV_FOpenFileRead( filename, &fh );

	if ( len <= 0 )
	{
		SV_CloseFileHandle( fh );
		return false;
	}
	const auto closeFile = SV_MakeScopeExit( [&]() { SV_CloseFileHandle( fh ); } );

	if ( len % 10 ) // should be a power of IP4:IP4:TLD2
	{
		Com_DPrintf( "%s(%s): invalid file size %i\n", __func__, filename, len );
		return false;
	}

	SV_FreeIP4DB();

	buf = SV_ZMallocArray<byte>( len );

	FS_Read( buf, len, fh );

	// check integrity of loaded database
	last_ip = 0;
	num_tlds = len / 10;
	int invalidIndex = -1;

	// database format:
	// [range1][range2]...[rangeN]
	// [tld1][tld2]...[tldN]

	ipdb_range = reinterpret_cast<iprange_t *>( buf );
	ipdb_tld = reinterpret_cast<iprange_tld_t *>( ipdb_range + num_tlds );

	for ( int index : SV_Indices( num_tlds ) )
	{
#ifdef Q3_LITTLE_ENDIAN
		ipdb_range[index].from = LongSwap( ipdb_range[index].from );
		ipdb_range[index].to = LongSwap( ipdb_range[index].to );
#endif
		if ( last_ip && last_ip >= ipdb_range[index].from )
		{
			invalidIndex = index;
			break;
		}
		if ( ipdb_range[index].from > ipdb_range[index].to )
		{
			invalidIndex = index;
			break;
		}
		if ( ipdb_tld[index].tld[0] < 'A' || ipdb_tld[index].tld[0] > 'Z' || ipdb_tld[index].tld[1] < 'A' || ipdb_tld[index].tld[1] > 'Z' )
		{
			invalidIndex = index;
			break;
		}
		last_ip = ipdb_range[index].to;
	}

	if ( invalidIndex >= 0 ) {
			Com_Printf( S_COLOR_YELLOW "invalid ip4db entry #%i: range=[%08x..%08x], tld=%c%c\n",
				invalidIndex, ipdb_range[invalidIndex].from, ipdb_range[invalidIndex].to, ipdb_tld[invalidIndex].tld[0], ipdb_tld[invalidIndex].tld[1] );
			SV_FreeIP4DB();
			return true; // to not try to load it again
	}

	Com_Printf( "ip4db: %i entries loaded\n", num_tlds );
	return true;
}


static void SV_SetTLD( char *str, const netadr_t *from, bool isLAN )
{
	const iprange_t *e;
	int lo, hi, m;
	uint32_t ip;

	str[0] = '\0';

	if ( sv_clientTLD->integer == 0 )
		return;

	if ( isLAN )
	{
		str[0] = '*';
		str[1] = '*';
		str[2] = '\0';
		return;
	}

	if ( from->type != NA_IP ) // ipv4-only
		return;

	if ( !ipdb_loaded )
		ipdb_loaded = SV_LoadIP4DB( "ip4db.dat" );

	if ( !ipdb_range )
		return;

	lo = 0;
	hi = num_tlds - 1;

	// big-endian to host-endian
#ifdef Q3_LITTLE_ENDIAN
	ip =  from->ipv._4[3] | from->ipv._4[2] << 8 | from->ipv._4[1] << 16 | from->ipv._4[0] << 24;
#else
	ip =  from->ipv._4[0] | from->ipv._4[1] << 8 | from->ipv._4[2] << 16 | from->ipv._4[3] << 24;
#endif

	// binary search
	while ( lo <= hi )
	{
		m = ( lo + hi ) / 2;
		e = ipdb_range + m;
		if ( ip >= e->from && ip <= e->to )
		{
			const iprange_tld_t *tld = ipdb_tld + m;
			str[0] = tld->tld[0];
			str[1] = tld->tld[1];
			str[2] = '\0';
			return;
		}

		if ( e->from > ip )
			hi = m - 1;
		else
			lo = m + 1;
	}
}


static std::array<int, MAX_CLIENTS> seqs{};

static void SV_SaveSequences( void ) {
	for ( SV_ClientSlot slot : SV_ClientSlots() ) {
		seqs[slot.index] = slot.client.reliableSequence;
	}
}


static void SV_InjectLocation( const char *tld, const char *country ) {
	char *cmd;
	char *str;
	int n;
	for ( SV_ClientSlot slot : SV_ClientSlots() ) {
		if ( seqs[slot.index] != slot.client.reliableSequence ) {
			for ( n = seqs[slot.index]; n != slot.client.reliableSequence + 1; n++ ) {
				cmd = slot.client.reliableCommands[n & (MAX_RELIABLE_COMMANDS-1)];
				str = strstr( cmd, "connected\n\"" );
				if ( str && str[11] == '\0' && str < cmd + 512 ) {
					const int remaining = MAX_STRING_CHARS - static_cast<int>( str - cmd );
					if ( *tld == '\0' )
						Com_sprintf( str, remaining, S_COLOR_WHITE "connected (%s)\n\"", country );
					else
						Com_sprintf( str, remaining, S_COLOR_WHITE "connected (" S_COLOR_RED "%s" S_COLOR_WHITE ", %s)\n\"", tld, country );
					break;
				}
			}
		}
	}
}


static const char *SV_FindCountry( const char *tld ) {
	if ( *tld == '\0' )
		return "Unknown Location";

	for ( const auto &info : tld_info ) {
		if ( !strcmp( tld, info.tld ) ) {
			return info.country;
		}
	}

	return "Unknown Location";
}


static const char *SV_GetStateName( clientState_t state ) {
	switch ( state ) {
		case CS_FREE:      return "CS_FREE";
		case CS_ZOMBIE:    return "CS_ZOMBIE";
		case CS_CONNECTED: return "CS_CONNECTED";
		case CS_PRIMED:    return "CS_PRIMED";
		case CS_ACTIVE:    return "CS_ACTIVE";
		default:           return "CS_UNKNOWN";
	}
}


void SV_PrintClientStateChange( const client_t *cl, clientState_t newState ) {

	if ( cl->state == newState ) {
		return;
	}

#ifndef _DEBUG
	if ( com_developer->integer == 0 ) {
		return;
	}
#endif // !_DEBUG

	if ( cl->name[0] != '\0' ) {
		Com_Printf( "Going from %s to %s for %s\n", SV_GetStateName( cl->state ), SV_GetStateName( newState ), cl->name );
	} else {
		Com_Printf( "Going from %s to %s for client %d\n", SV_GetStateName( cl->state ), SV_GetStateName( newState ), SV_ClientIndex( cl ) );
	}
	
}


static int SV_DemoProtocol( const client_t *client )
{
	return client ? fnql::protocol::ForWireProfile(
		client->netchan.wireProfile ).demoProtocol : com_protocol->integer;
}


static void SV_SanitizeDemoToken( char *out, int outSize, const char *in )
{
	int outPos;
	bool allowed;
	bool lastUnderscore = false;
	std::array<char, MAX_OSPATH> clean{};

	if ( outSize < 1 ) {
		return;
	}

	if ( !in ) {
		in = "";
	}

	Q_strncpyz( clean.data(), in, SV_ArraySize(clean) );
	Q_CleanStr( clean.data() );

	outPos = 0;
	for ( char value : clean ) {
		if ( value == '\0' || outPos >= outSize - 1 ) {
			break;
		}
		const byte c = static_cast<byte>( value );
		allowed =
			( c >= '0' && c <= '9' ) ||
			( c >= 'A' && c <= 'Z' ) ||
			( c >= 'a' && c <= 'z' ) ||
			c == '-' || c == '_';

		if ( allowed ) {
			out[ outPos++ ] = c;
			lastUnderscore = false;
		} else if ( !lastUnderscore && outPos > 0 ) {
			out[ outPos++ ] = '_';
			lastUnderscore = true;
		}
	}

	while ( outPos > 0 && out[ outPos - 1 ] == '_' ) {
		outPos--;
	}

	out[ outPos ] = '\0';
}


static void SV_BuildDemoName( const client_t *client, char *name, int nameSize )
{
	const char *gameDir;
	qtime_t now;
	std::array<char, 32> timeString{};
	std::array<char, MAX_QPATH> mapName{};
	std::array<char, MAX_NAME_LENGTH> playerName{};

	Com_RealTime( &now );
	Com_sprintf( timeString.data(), SV_ArraySize(timeString), "%04d%02d%02d-%02d%02d%02d",
		1900 + now.tm_year,
		1 + now.tm_mon,
		now.tm_mday,
		now.tm_hour,
		now.tm_min,
		now.tm_sec );

	SV_SanitizeDemoToken( mapName.data(), SV_ArraySize(mapName),
		( sv_mapname && sv_mapname->string[ 0 ] ) ? sv_mapname->string : "nomap" );
	SV_SanitizeDemoToken( playerName.data(), SV_ArraySize(playerName), client->name );

	if ( !playerName[ 0 ] ) {
		Q_strncpyz( playerName.data(), "player", SV_ArraySize(playerName) );
	}

	gameDir = FS_GetCurrentGameDir();
	if ( !gameDir || !gameDir[ 0 ] ) {
		gameDir = BASEGAME;
	}

	Com_sprintf( name, nameSize, "%s/demos/server/%s-%s-c%02d-%s",
		gameDir, timeString.data(), mapName.data(), SV_ClientIndex( client ), playerName.data() );
}


static void SV_WriteDemoFileMessage( client_t *client, const msg_t *msg, int sequence )
{
	int len;
	int swlen;
	msg_t demoMsg;
	std::array<byte, MAX_MSGLEN_BUF> demoBuffer{};

	if ( client->demoRecordFile == FS_INVALID_HANDLE ) {
		return;
	}

	MSG_Copy( &demoMsg, demoBuffer.data(), SV_ArraySize(demoBuffer), msg );
	MSG_WriteByte( &demoMsg, svc_EOF );

	swlen = LittleLong( sequence );
	FS_Write( &swlen, 4, client->demoRecordFile );

	len = LittleLong( demoMsg.cursize );
	FS_Write( &len, 4, client->demoRecordFile );
	FS_Write( demoMsg.data, demoMsg.cursize, client->demoRecordFile );
}


static bool SV_DemoMessageCommand( const msg_t *msg, int *command )
{
	msg_t copy = *msg;
	int cmd;

	MSG_BeginReading( &copy );

	if ( copy.cursize < 5 ) {
		return false;
	}

	MSG_ReadLong( &copy );

	while ( copy.readcount < copy.cursize ) {
		cmd = MSG_ReadByte( &copy );

		if ( cmd == svc_serverCommand ) {
			MSG_ReadLong( &copy );
			MSG_ReadString( &copy );
			continue;
		}

		*command = cmd;
		return true;
	}

	return false;
}


static void SV_WriteConfigstringMessage( msg_t &msg, int index, const char *value ) {
	MSG_WriteByte( &msg, svc_configstring );
	MSG_WriteShort( &msg, index );
	MSG_WriteBigString( &msg, value );
}


static void SV_WritePureAwareConfigstring( msg_t &msg, int index ) {
	if ( index == CS_SYSTEMINFO && sv.pure != sv_pure->integer ) {
		std::array<char, BIG_INFO_STRING> systemInfo{};

		Q_strncpyz( systemInfo.data(), sv.configstrings[ index ], SV_ArraySize(systemInfo) );
		Info_SetValueForKey_s( systemInfo.data(), SV_ArraySize(systemInfo), "sv_pure", va( "%i", sv.pure ) );
		SV_WriteConfigstringMessage( msg, index, systemInfo.data() );
	} else {
		SV_WriteConfigstringMessage( msg, index, sv.configstrings[ index ] );
	}
}


static void SV_WriteBaselineMessage( msg_t &msg, const entityState_t &nullstate, int entityNum ) {
	MSG_WriteByte( &msg, svc_baseline );
	MSG_WriteDeltaEntity( &msg, &nullstate, &sv.svEntities[ entityNum ].baseline, qtrue );
}


static void SV_WriteDemoGamestate( client_t *client )
{
	entityState_t nullstate{};
	msg_t msg;
	std::array<byte, MAX_MSGLEN_BUF> msgBuffer{};

	if ( client->demoRecordFile == FS_INVALID_HANDLE ) {
		return;
	}

	MSG_Init( &msg, msgBuffer.data(), MAX_MSGLEN );

	MSG_WriteLong( &msg, client->lastClientCommand );
	SV_UpdateServerCommandsToClient( client, &msg );

	MSG_WriteByte( &msg, svc_gamestate );
	MSG_WriteLong( &msg, client->reliableSequence );

	for ( int index : SV_Indices( MAX_CONFIGSTRINGS ) ) {
		if ( *sv.configstrings[ index ] != '\0' ) {
			SV_WritePureAwareConfigstring( msg, index );
		}
	}

	for ( int entityNum : SV_Indices( MAX_GENTITIES ) ) {
		if ( !sv.baselineUsed[ entityNum ] ) {
			continue;
		}

		SV_WriteBaselineMessage( msg, nullstate, entityNum );
	}

	MSG_WriteByte( &msg, svc_EOF );
	MSG_WriteLong( &msg, SV_ClientIndex( client ) );
	MSG_WriteLong( &msg, sv.checksumFeed );

	if ( msg.overflowed ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: couldn't write server demo gamestate for %s\n", client->name );
		SV_StopDemoRecord( client, qtrue );
		return;
	}

	SV_WriteDemoFileMessage( client, &msg, client->netchan.outgoingSequence );
}


void SV_StartDemoRecord( client_t *client )
{
	std::array<char, MAX_OSPATH> name{};
	std::array<char, MAX_OSPATH> tempName{};

	if ( !sv_autoRecordDemos || !sv_autoRecordDemos->integer ) {
		return;
	}

	if ( ( sv_autoRecordDemos->integer == 2 || sv_autoRecordDemos->integer == 4 ) && sv_cheats->integer ) {
		return;
	}

	if ( client->netchan.remoteAddress.type == NA_BOT ) {
		return;
	}

	if ( client->demoRecordFile != FS_INVALID_HANDLE ) {
		if ( client->demoRecordServerId == sv.serverId ) {
			return;
		}

		SV_StopDemoRecord( client, qfalse );
	}

	SV_BuildDemoName( client, name.data(), SV_ArraySize(name) );
	Q_strncpyz( client->demoRecordName, name.data(), SV_ArraySize( client->demoRecordName ) );

	Com_sprintf( tempName.data(), SV_ArraySize(tempName), "%s.tmp", client->demoRecordName );

	client->demoRecordFile = FS_SV_FOpenFileWrite( tempName.data() );
	if ( client->demoRecordFile == FS_INVALID_HANDLE ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: couldn't open server demo for %s\n", client->name );
		client->demoRecordName[ 0 ] = '\0';
		client->demoRecordServerId = 0;
		client->demoRecordHasSnapshot = qfalse;
		return;
	}

	client->demoRecordHasSnapshot = qfalse;
	client->demoRecordServerId = sv.serverId;

	SV_WriteDemoGamestate( client );

	if ( client->demoRecordFile != FS_INVALID_HANDLE ) {
		Com_Printf( "Server demo recording to %s\n", client->demoRecordName );
	}
}


void SV_StopDemoRecord( client_t *client, qboolean discard )
{
	std::array<char, MAX_OSPATH> tempName{};
	std::array<char, MAX_OSPATH> finalName{};
	int len;
	int sequence;
	int protocol;

	if ( client->demoRecordFile == FS_INVALID_HANDLE ) {
		client->demoRecordName[ 0 ] = '\0';
		client->demoRecordServerId = 0;
		client->demoRecordHasSnapshot = qfalse;
		return;
	}

	discard = SV_QBool( discard || !client->demoRecordHasSnapshot );
	Com_sprintf( tempName.data(), SV_ArraySize(tempName), "%s.tmp", client->demoRecordName );

	if ( !discard ) {
		sequence = 0;
		protocol = SV_DemoProtocol( client );

		len = -1;
		FS_Write( &len, 4, client->demoRecordFile );
		FS_Write( &len, 4, client->demoRecordFile );
		SV_CloseFileHandle( client->demoRecordFile );

		Com_sprintf( finalName.data(), SV_ArraySize(finalName), "%s.%s%d",
			client->demoRecordName, DEMOEXT, protocol );

		while ( FS_SV_FileExists( finalName.data() ) && ++sequence < 1000 ) {
			Com_sprintf( finalName.data(), SV_ArraySize(finalName), "%s-%02d.%s%d",
				client->demoRecordName, sequence, DEMOEXT, protocol );
		}

		FS_SV_Rename( tempName.data(), finalName.data() );
		Com_Printf( "Stopped server demo recording: %s\n", finalName.data() );
	} else {
		SV_CloseFileHandle( client->demoRecordFile );
		FS_Remove( FS_BuildOSPath( FS_GetHomePath(), tempName.data(), nullptr ) );
	}

	client->demoRecordName[ 0 ] = '\0';
	client->demoRecordServerId = 0;
	client->demoRecordHasSnapshot = qfalse;
}


void SV_RecordDemoMessage( client_t *client, const msg_t *msg )
{
	int command;

	if ( client->demoRecordFile == FS_INVALID_HANDLE || client->demoRecordServerId != sv.serverId ) {
		return;
	}

	if ( !SV_DemoMessageCommand( msg, &command ) ) {
		return;
	}

	if ( command != svc_snapshot ) {
		return;
	}

	SV_WriteDemoFileMessage( client, msg, client->netchan.outgoingSequence );
	client->demoRecordHasSnapshot = qtrue;
}


/*
==================
SV_ClientSteamIdFromUserinfo

Returns a decimal SteamID supplied by the connecting client, preferring the
retail QL key name while keeping the older shorthand as a compatibility source.
==================
*/
static const char *SV_ClientSteamIdFromUserinfo( const char *userinfo ) {
	const char *steamId;

	steamId = Info_ValueForKey( userinfo, "steamid" );
	if ( steamId[0] ) {
		return steamId;
	}

	return Info_ValueForKey( userinfo, "steam" );
}


/*
==================
SV_ParseClientSteamIdString
==================
*/
static qboolean SV_ParseClientSteamIdString( const char *steamId,
		std::uint64_t *outValue ) {
	int length;
	std::uint64_t value = 0;
	if ( outValue ) {
		*outValue = 0;
	}

	if ( !steamId || !steamId[0] ) {
		return qfalse;
	}

	for ( length = 0; steamId[length]; ++length ) {
		std::uint64_t digit;

		if ( steamId[length] < '0' || steamId[length] > '9' ) {
			return qfalse;
		}
		digit = static_cast<std::uint64_t>( steamId[length] - '0' );
		if ( value > ( ( std::numeric_limits<std::uint64_t>::max )() - digit ) / 10u ) {
			return qfalse;
		}
		value = value * 10u + digit;
	}

	if ( value == 0 || length <= 0 || length >= SV_PLATFORM_STEAM_ID_SIZE ) {
		return qfalse;
	}
	if ( outValue ) {
		*outValue = value;
	}
	return qtrue;
}


/*
==================
SV_CaptureClientSteamId
==================
*/
static void SV_CaptureClientSteamId( client_t *client, const char *userinfo ) {
	const char *steamId;
	std::uint64_t value = 0;

	client->platformSteamId[0] = '\0';
	client->platformSteamIdValue = 0;

	steamId = SV_ClientSteamIdFromUserinfo( userinfo );
	if ( SV_ParseClientSteamIdString( steamId, &value ) ) {
		Q_strncpyz( client->platformSteamId, steamId, SV_ArraySize( client->platformSteamId ) );
		client->platformSteamIdValue = value;
	}
}


/*
==================
SV_MirrorClientSteamIdToUserinfo
==================
*/
static void SV_MirrorClientSteamIdToUserinfo( const client_t *client, char *userinfo, int userinfoSize ) {
	std::array<char, MAX_INFO_STRING> mirrored{};

	if ( !client->platformSteamId[0] ) {
		return;
	}

	Q_strncpyz( mirrored.data(), userinfo, SV_ArraySize( mirrored ) );
	if ( Info_SetValueForKey_s( mirrored.data(), userinfoSize, "steam", client->platformSteamId ) &&
		Info_SetValueForKey_s( mirrored.data(), userinfoSize, "steamid", client->platformSteamId ) ) {
		Q_strncpyz( userinfo, mirrored.data(), userinfoSize );
	}
}


/*
==================
SV_VerifyClientSteamAuth
==================
*/
qboolean SV_VerifyClientSteamAuth( int clientNum ) {
	if ( !SV_IsClientIndex( clientNum ) ) {
		Com_DPrintf( "Server auth validate client %d via %s [%s]: invalid client slot\n",
			clientNum, SV_GetPlatformAuthProviderLabel(), SV_GetPlatformAuthPolicyLabel() );
		return qfalse;
	}

	const client_t &client = SV_ClientForIndex( clientNum );
	if ( client.state == CS_FREE || client.state == CS_ZOMBIE ) {
		Com_DPrintf( "Server auth validate client %d via %s [%s]: inactive client slot\n",
			clientNum, SV_GetPlatformAuthProviderLabel(), SV_GetPlatformAuthPolicyLabel() );
		return qfalse;
	}

	if ( SV_PlatformServiceAvailable( SV_PLATFORM_CAPABILITY_AUTH ) ) {
		return client.platformAuthSession && client.platformAuthValidated
			? qtrue : qfalse;
	}

	Com_DPrintf( "Server auth validate client %d via %s [%s]: compatibility-unverified\n",
		clientNum, SV_GetPlatformAuthProviderLabel(), SV_GetPlatformAuthPolicyLabel() );
	return qtrue;
}

static void SV_LogSteamStatsLifecycle( const char *stage, const char *detail ) {
	Com_DPrintf( "Server stats %s via %s [%s]: %s\n",
		stage ? stage : "update",
		SV_GetServerStatsProviderLabel(),
		SV_GetServerStatsPolicyLabel(),
		detail ? detail : "no detail" );
}

static bool SV_SerializeStatsJson( const void *value,
		std::array<char, fnql::server::json::MaximumDocumentBytes + 1> &document,
		std::uint32_t &documentBytes ) {
	document.fill( '\0' );
	documentBytes = 0;
	if ( !value || FNQL_Steam_SerializeRetailJson( value, document.data(),
		static_cast<std::uint32_t>( document.size() ), &documentBytes ) !=
		FNQL_STEAM_RESULT_OK || documentBytes == 0 ||
		documentBytes > fnql::server::json::MaximumDocumentBytes ||
		document[documentBytes] != '\0' ||
		!fnql::server::json::DocumentIsValid(
			std::string_view( document.data(), documentBytes ) ) ) {
		document.fill( '\0' );
		documentBytes = 0;
		return false;
	}
	return true;
}

static int SV_FindStatsClientByIdentity( std::uint64_t identity ) {
	if ( identity == 0 || !svs.clients ) {
		return -1;
	}
	for ( int clientNum = 0; clientNum < sv.maxclients; ++clientNum ) {
		const client_t &client = svs.clients[clientNum];
		if ( client.platformSteamIdValue == identity && client.state != CS_FREE &&
			client.state != CS_ZOMBIE && client.netchan.remoteAddress.type != NA_BOT ) {
			return clientNum;
		}
	}
	return -1;
}

static fnql::server::stats::Session *SV_ClientStatsSession( int clientNum,
		const char *stage ) {
	if ( !SV_IsClientIndex( clientNum ) ) {
		SV_LogSteamStatsLifecycle( stage, "invalid client slot" );
		return nullptr;
	}
	client_t &client = SV_ClientForIndex( clientNum );
	if ( client.state == CS_FREE || client.state == CS_ZOMBIE ||
		client.netchan.remoteAddress.type == NA_BOT ) {
		SV_LogSteamStatsLifecycle( stage, "inactive or bot client slot" );
		return nullptr;
	}

	fnql::server::stats::Session &session = clientStatsSessions[clientNum];
	session.Begin( client.platformSteamIdValue );
	if ( client.platformAuthSession && client.platformAuthValidated &&
		client.platformAuthTicketSession &&
		client.platformSteamIdValue != 0 &&
		!session.RequestIssued() &&
		( FNQL_Steam_Capabilities() &
			( FNQL_STEAM_CAP_STATS | FNQL_STEAM_CAP_GAME_SERVER_STATS ) ) ) {
		const fnqlSteamResult_t result =
			FNQL_Steam_RequestUserStats( client.platformSteamIdValue );
		if ( result == FNQL_STEAM_RESULT_OK || result == FNQL_STEAM_RESULT_PENDING ) {
			session.MarkRequestIssued();
		}
	}
	return &session;
}

static bool SV_StatsProviderReady( const client_t &client ) {
	return client.platformAuthSession && client.platformAuthValidated &&
		client.platformAuthTicketSession &&
		client.platformSteamIdValue != 0 &&
		FNQL_Steam_Available( FNQL_STEAM_CAP_GAME_SERVER_STATS );
}

static bool SV_ShouldUnlockSteamAchievement() {
	std::array<char, MAX_STRING_CHARS> gameState{};
	Cvar_VariableStringBuffer( "g_gameState", gameState.data(),
		static_cast<int>( gameState.size() ) );
	return !gameState[0] || !Q_stricmp( gameState.data(), "IN_PROGRESS" ) ||
		Cvar_VariableIntegerValue( "g_training" ) != 0 ||
		Cvar_VariableIntegerValue( "practiceflags" ) != 0;
}

static void SV_LoadStatsField( const client_t &client,
		fnql::server::stats::Session &session, int statIndex ) {
	if ( !SV_StatsProviderReady( client ) || session.FieldLoaded( statIndex ) ||
		statIndex < 0 || static_cast<std::size_t>( statIndex ) >=
			fnql::server::stats::FieldNames.size() ) {
		return;
	}
	std::int32_t value = 0;
	if ( FNQL_Steam_GetUserStatI32( client.platformSteamIdValue,
		fnql::server::stats::FieldNames[statIndex].data(), &value ) ==
		FNQL_STEAM_RESULT_OK ) {
		(void)session.LoadField( statIndex, value );
	}
}

static void SV_LoadStatsAchievement( const client_t &client,
		fnql::server::stats::Session &session, int achievementId ) {
	if ( !SV_StatsProviderReady( client ) ||
		session.AchievementLoaded( achievementId ) || achievementId < 0 ||
		static_cast<std::size_t>( achievementId ) >=
			fnql::server::stats::AchievementNames.size() ) {
		return;
	}
	qboolean unlocked = qfalse;
	if ( FNQL_Steam_GetUserAchievement( client.platformSteamIdValue,
		fnql::server::stats::AchievementNames[achievementId].data(), &unlocked ) ==
		FNQL_STEAM_RESULT_OK ) {
		(void)session.LoadAchievement( achievementId, unlocked != qfalse );
	}
}

static void SV_AddEventStatsField( int clientNum,
		fnql::server::stats::Session &session, int statIndex, int delta ) {
	if ( delta == 0 ) {
		return;
	}
	SV_LoadStatsField( SV_ClientForIndex( clientNum ), session, statIndex );
	if ( !session.AddField( statIndex, delta ) ) {
		SV_LogSteamStatsLifecycle( "event-process", "invalid mapped retail stat index" );
	}
}

static void SV_UnlockEventAchievement( int clientNum,
		fnql::server::stats::Session &session, int achievementId ) {
	if ( !SV_ShouldUnlockSteamAchievement() ) {
		return;
	}
	SV_LoadStatsAchievement( SV_ClientForIndex( clientNum ), session, achievementId );
	if ( !session.HasAchievement( achievementId ) ) {
		(void)session.UnlockAchievement( achievementId );
	}
}

static void SV_FlushClientStats( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !svs.clients ) {
		return;
	}
	client_t &client = svs.clients[clientNum];
	fnql::server::stats::Session &session = clientStatsSessions[clientNum];
	if ( !session.Active() || session.StorePending() ||
		!SV_StatsProviderReady( client ) ) {
		return;
	}

	std::array<bool, fnql::server::stats::FieldCount> storedFields{};
	std::array<bool, fnql::server::stats::AchievementCount> storedAchievements{};
	bool changed = false;
	for ( std::size_t index = 0; index < fnql::server::stats::FieldCount; ++index ) {
		if ( !session.FieldDirty( static_cast<int>( index ) ) ) {
			continue;
		}
		SV_LoadStatsField( client, session, static_cast<int>( index ) );
		if ( !session.FieldLoaded( static_cast<int>( index ) ) ) {
			continue;
		}
		if ( FNQL_Steam_SetUserStatI32( client.platformSteamIdValue,
			fnql::server::stats::FieldNames[index].data(),
			session.Field( static_cast<int>( index ) ) ) == FNQL_STEAM_RESULT_OK ) {
			storedFields[index] = true;
			changed = true;
		}
	}
	for ( std::size_t index = 0; index < fnql::server::stats::AchievementCount; ++index ) {
		if ( !session.AchievementDirty( static_cast<int>( index ) ) ) {
			continue;
		}
		if ( FNQL_Steam_SetUserAchievement( client.platformSteamIdValue,
			fnql::server::stats::AchievementNames[index].data() ) ==
			FNQL_STEAM_RESULT_OK ) {
			storedAchievements[index] = true;
			changed = true;
		}
	}
	if ( !changed ) {
		return;
	}
	const fnqlSteamResult_t storeResult =
		FNQL_Steam_StoreUserStats( client.platformSteamIdValue );
	// Retain the submitted generation until USER_STATS_STORED reports the final
	// asynchronous outcome; newer changes remain dirty independently.
	if ( storeResult != FNQL_STEAM_RESULT_OK ) {
		if ( storeResult == FNQL_STEAM_RESULT_PENDING ) {
			session.BeginStorePending( storedFields, storedAchievements );
		}
		return;
	}
	for ( std::size_t index = 0; index < storedFields.size(); ++index ) {
		if ( storedFields[index] ) {
			session.MarkFieldStored( static_cast<int>( index ) );
		}
	}
	for ( std::size_t index = 0; index < storedAchievements.size(); ++index ) {
		if ( storedAchievements[index] ) {
			session.MarkAchievementStored( static_cast<int>( index ) );
		}
	}
}

void SV_FlushAllSteamStats( void ) {
	for ( int clientNum = 0; clientNum < sv.maxclients; ++clientNum ) {
		SV_FlushClientStats( clientNum );
	}
}

void SV_HandleSteamProviderEvent( unsigned int type, int result,
		uint64_t subjectId ) {
	if ( subjectId == 0 || !svs.clients ) {
		return;
	}

	for ( int clientNum = 0; clientNum < sv.maxclients; ++clientNum ) {
		client_t &client = svs.clients[clientNum];
		if ( client.platformSteamIdValue != subjectId ||
			client.state == CS_FREE || client.state == CS_ZOMBIE ) {
			continue;
		}
		if ( type == FNQL_STEAM_EVENT_AUTH_RESULT && client.platformAuthTicketSession ) {
			if ( result == FNQL_STEAM_RESULT_OK ) {
				if ( !client.platformAuthValidated ) {
					client.platformAuthValidated = qtrue;
					Com_DPrintf( "Platform authentication validated client %d.\n", clientNum );
				}
			} else if ( result != FNQL_STEAM_RESULT_PENDING ) {
				Com_Printf( S_COLOR_YELLOW
					"Platform authentication rejected client %d (result %d).\n",
					clientNum, result );
				SV_DropClient( &client, "Platform authentication failed" );
			}
		} else if ( type == FNQL_STEAM_EVENT_USER_STATS_RECEIVED ) {
			if ( result == FNQL_STEAM_RESULT_OK ) {
				SV_FlushClientStats( clientNum );
			} else if ( result != FNQL_STEAM_RESULT_PENDING ) {
				// Permit a later game request or flush to retry a transient failure.
				clientStatsSessions[clientNum].ClearRequestIssued();
			}
		} else if ( type == FNQL_STEAM_EVENT_USER_STATS_STORED ) {
			clientStatsSessions[clientNum].CompletePendingStore(
				result == FNQL_STEAM_RESULT_OK );
		}
	}
}


/*
==================
SV_SteamStats_AddFieldValue
==================
*/
void SV_SteamStats_AddFieldValue( int clientNum, int statIndex, int delta ) {
	if ( delta == 0 ) {
		return;
	}
	fnql::server::stats::Session *session =
		SV_ClientStatsSession( clientNum, "field-delta" );
	if ( session ) {
		SV_LoadStatsField( SV_ClientForIndex( clientNum ), *session, statIndex );
	}
	if ( session && !session->AddField( statIndex, delta ) ) {
		SV_LogSteamStatsLifecycle( "field-delta", "invalid retail stat index" );
	}
}


/*
==================
SV_SteamStats_UnlockAchievement
==================
*/
void SV_SteamStats_UnlockAchievement( int clientNum, int achievementId ) {
	if ( achievementId < 0 || static_cast<std::size_t>( achievementId ) >=
		fnql::server::stats::AchievementCount ) {
		SV_LogSteamStatsLifecycle( "achievement-unlock", "invalid retail achievement index" );
		return;
	}
	if ( !SV_ShouldUnlockSteamAchievement() ) {
		SV_LogSteamStatsLifecycle( "achievement-unlock", "blocked by gameplay gate" );
		return;
	}
	fnql::server::stats::Session *session =
		SV_ClientStatsSession( clientNum, "achievement-unlock" );
	if ( session ) {
		SV_LoadStatsAchievement( SV_ClientForIndex( clientNum ), *session, achievementId );
	}
	if ( session && !session->UnlockAchievement( achievementId ) ) {
		SV_LogSteamStatsLifecycle( "achievement-unlock", "invalid retail achievement index" );
	}
}


/*
==================
SV_SteamStats_HasAchievement
==================
*/
qboolean SV_SteamStats_HasAchievement( int clientNum, int achievementId ) {
	fnql::server::stats::Session *session =
		SV_ClientStatsSession( clientNum, "achievement-query" );
	if ( session ) {
		SV_LoadStatsAchievement( SV_ClientForIndex( clientNum ), *session, achievementId );
	}
	return session && session->HasAchievement( achievementId ) ? qtrue : qfalse;
}


/*
==================
SV_SteamStats_ProcessMatchReport
==================
*/
const void *SV_SteamStats_ProcessMatchReport( const void *report, char *buffer, int bufferSize ) {
	std::array<char, fnql::server::json::MaximumDocumentBytes + 1> document{};
	std::array<char, fnql::server::json::MaximumDocumentBytes + 1> summary{};
	std::uint32_t documentBytes = 0;
	SV_FlushAllSteamStats();
	if ( SV_SerializeStatsJson( report, document, documentBytes ) ) {
		const std::string_view reportDocument( document.data(), documentBytes );
		const bool summaryAllowed =
			fnql::server::stats::ReportAccumulator::MatchSummaryAllowed( reportDocument );
		const bool merged = summaryAllowed && statsReports.Build(
			reportDocument, summary.data(), summary.size() );
		if ( buffer && bufferSize > 0 ) {
			Q_strncpyz( buffer, merged ? summary.data() : document.data(), bufferSize );
		}
		Zmq_SubmitMatchReportJson( document.data() );
		if ( merged ) {
			Zmq_SubmitMatchSummaryJson( summary.data() );
		}
		SV_LogSteamStatsLifecycle( "match-report",
			merged ? "published report and eligible PLYR_STATS/PLYR_EVENTS summary"
				: summaryAllowed ? "published report; bounded summary merge failed"
					: "published report; training or aborted summary suppressed" );
	} else {
		if ( buffer && bufferSize > 0 ) {
			buffer[0] = '\0';
		}
		Zmq_SubmitMatchReport( report );
		SV_LogSteamStatsLifecycle( "match-report",
			"retail JSON adapter unavailable; published a null payload envelope" );
	}
	statsReports.Reset();
	return report;
}


/*
==================
SV_SteamStats_ProcessEvent
==================
*/
void SV_SteamStats_ProcessEvent( unsigned int steamIdLow, unsigned int steamIdHigh,
		const void *clientStats, const char *eventName, const void *payload ) {
	std::array<char, fnql::server::json::MaximumDocumentBytes + 1> document{};
	std::uint32_t documentBytes = 0;
	const std::uint64_t identity =
		( static_cast<std::uint64_t>( steamIdHigh ) << 32u ) | steamIdLow;
	if ( !eventName || !eventName[0] ) {
		return;
	}
	if ( !SV_SerializeStatsJson( payload, document, documentBytes ) ) {
		Zmq_ReportPlayerEvent( steamIdLow, steamIdHigh, clientStats, eventName, payload );
		SV_LogSteamStatsLifecycle( "event-process",
			"retail JSON adapter unavailable; event side effects safely deferred" );
		return;
	}

	Zmq_ReportPlayerEventJson( eventName, document.data() );
	const fnql::server::stats::ParsedPlayerEvent event =
		fnql::server::stats::ParsePlayerEvent( eventName,
			std::string_view( document.data(), documentBytes ) );
	if ( !event.valid || event.ignored ||
		event.kind == fnql::server::stats::PlayerEventKind::Unknown ) {
		return;
	}
	using fnql::server::stats::PlayerEventKind;
	const bool race = Cvar_VariableIntegerValue( "g_gametype" ) == GT_SINGLE_PLAYER;
	if ( event.kind == PlayerEventKind::Stats ) {
		if ( !statsReports.CachePlayerStats(
			std::string_view( document.data(), documentBytes ) ) ) {
			SV_LogSteamStatsLifecycle( "event-process", "PLAYER_STATS summary cache full" );
		}
	} else if ( event.kind == PlayerEventKind::Death && !race ) {
		if ( !statsReports.CachePlayerDeath(
			std::string_view( document.data(), documentBytes ) ) ) {
			SV_LogSteamStatsLifecycle( "event-process", "PLAYER_DEATH summary cache full" );
		}
	}
	const int clientNum = SV_FindStatsClientByIdentity( identity );
	if ( clientNum < 0 ) {
		SV_LogSteamStatsLifecycle( "event-process", "no live session for event identity" );
		return;
	}
	fnql::server::stats::Session *session =
		SV_ClientStatsSession( clientNum, "event-process" );
	if ( !session ) {
		return;
	}

	if ( event.kind == PlayerEventKind::Stats ) {
		SV_AddEventStatsField( clientNum, *session, 0x51, event.wins );
		SV_AddEventStatsField( clientNum, *session, 0x52, event.losses );
		SV_AddEventStatsField( clientNum, *session, 0x53, 1 );
		if ( event.wins > 0 && !Q_stricmp( Cvar_VariableString( "mapname" ), "qztraining" ) &&
			Cvar_VariableIntegerValue( "g_training" ) > 0 ) {
			SV_UnlockEventAchievement( clientNum, *session, 9 );
		}
		if ( Cvar_VariableIntegerValue( "g_gametype" ) == 5 && event.score == 666 ) {
			SV_UnlockEventAchievement( clientNum, *session, 0x0e );
		}
	} else if ( event.kind == PlayerEventKind::Kill && !race ) {
		SV_AddEventStatsField( clientNum, *session, 0x56, 1 );
		if ( event.mappedStat > 0 ) {
			SV_AddEventStatsField( clientNum, *session, event.mappedStat, 1 );
		}
		if ( event.speed > 500.0 ) {
			SV_UnlockEventAchievement( clientNum, *session, 1 );
		}
	} else if ( event.kind == PlayerEventKind::Death && !race ) {
		SV_AddEventStatsField( clientNum, *session, 0x57, 1 );
		if ( event.mappedStat > 0 ) {
			SV_AddEventStatsField( clientNum, *session, event.mappedStat, 1 );
		}
	} else if ( event.kind == PlayerEventKind::Medal && event.mappedStat >= 0 ) {
		SV_AddEventStatsField( clientNum, *session, event.mappedStat, 1 );
		SV_LoadStatsField( SV_ClientForIndex( clientNum ), *session, 0x4a );
		SV_LoadStatsField( SV_ClientForIndex( clientNum ), *session, 0x4b );
		SV_LoadStatsField( SV_ClientForIndex( clientNum ), *session, 0x4c );
		if ( static_cast<std::int64_t>( session->Field( 0x4a ) ) +
			session->Field( 0x4b ) + session->Field( 0x4c ) >= 1000 ) {
			SV_UnlockEventAchievement( clientNum, *session, 0x2e );
		}
	}
	SV_FlushClientStats( clientNum );

	(void)clientStats;
}


/*
==================
SV_DirectConnect

A "connect" OOB command has been received
==================
*/
void SV_DirectConnect( const netadr_t *from ) {
	static		rateLimit_t bucket;
	std::array<char, MAX_INFO_STRING> userinfo{};
	std::array<char, 3> tld{};
	int			n;
	client_t	*newcl;
	//sharedEntity_t *ent;
	int			clientNum;
	int			qport;
	int			challenge;
	const char		*password;
	int			startIndex;
	intptr_t	denied;
	int			count;
	int			cl_proto, sv_proto;
	const char	*ip, *info, *v;
	bool		compat;
	bool		longstr;
	bool		localLoopback;
	std::uint64_t retailSteamId = 0;
	bool		platformAuthSession = false;
	bool		platformAuthTicketSession = false;
	bool		platformAuthValidated = false;

	Com_DPrintf( "SVC_DirectConnect()\n" );

#ifdef USE_BANS
	// Check whether this client is banned.
	if(SV_IsBanned(from, false))
	{
		NET_OutOfBandPrint(NS_SERVER, &from, "print\nYou are banned from this server.\n");
		return;
	}
#endif

	// Prevent using connect as an amplifier
	if ( SVC_RateLimitAddress( from, 10, 1000 ) ) {
		if ( com_developer->integer ) {
			Com_Printf( "SV_DirectConnect: rate limit from %s exceeded, dropping request\n",
				NET_AdrToString( from ) );
		}
		return;
	}

	// check for concurrent connections
	n = 0;
	for ( const client_t &client : SV_Clients() ) {
		const netadr_t *addr = &client.netchan.remoteAddress;
		if ( addr->type != NA_BOT && NET_CompareBaseAdr( addr, from ) ) {
			if ( client.state >= CS_CONNECTED && !client.justConnected ) {
				if ( ++n >= sv_maxclientsPerIP->integer ) {
					// avoid excessive outgoing traffic
					if ( !SVC_RateLimit( &bucket, 10, 200 ) ) {
						NET_OutOfBandPrint( NS_SERVER, from, "print\nToo many connections.\n" );
					}
					return;
				}
			}
		}
	}

	// verify challenge in first place
	info = Cmd_Argv( 1 );
	v = Info_ValueForKey( info, "challenge" );
	if ( *v == '\0' )
	{
		if ( !SVC_RateLimit( &bucket, 10, 200 ) )
		{
			NET_OutOfBandPrint( NS_SERVER, from, "print\nMissing challenge in userinfo.\n" );
		}
		return;
	}
	challenge = SV_ParseInt( v );

	// see if the challenge is valid (localhost clients don't need to challenge)
	if ( !NET_IsLocalAddress( from ) )
	{
		// Verify the received challenge against the expected challenge
		if ( !SV_VerifyChallenge( challenge, from ) )
		{
			// avoid excessive outgoing traffic
			if ( !SVC_RateLimit( &bucket, 10, 200 ) )
			{
				NET_OutOfBandPrint( NS_SERVER, from, "print\nIncorrect challenge, please reconnect.\n" );
			}
			return;
		}
	}

	Q_strncpyz( userinfo.data(), info, SV_ArraySize(userinfo) );

	v = Info_ValueForKey( userinfo.data(), "protocol" );
	if ( *v == '\0' )
	{
		if ( !SVC_RateLimit( &bucket, 10, 200 ) )
		{
			NET_OutOfBandPrint( NS_SERVER, from, "print\nMissing protocol in userinfo.\n" );
		}
		return;
	}
	cl_proto = SV_ParseInt( v );

	sv_proto = com_protocol->integer;

	if ( cl_proto <= OLD_PROTOCOL_VERSION )
		compat = true;
	else
	{
		if ( cl_proto != sv_proto )
		{
			// avoid excessive outgoing traffic
			if ( !SVC_RateLimit( &bucket, 10, 200 ) )
			{
				NET_OutOfBandPrint( NS_SERVER, from, "print\nServer uses protocol version %i "
					"(yours is %i).\n", sv_proto, cl_proto );
			}
			Com_DPrintf( "    rejected connect from version %i\n", cl_proto );
			return;
		}
		compat = false;
	}

	v = Info_ValueForKey( userinfo.data(), "qport" );
	if ( *v == '\0' )
	{
		if ( !SVC_RateLimit( &bucket, 10, 200 ) )
		{
			NET_OutOfBandPrint( NS_SERVER, from, "print\nMissing qport in userinfo.\n" );
		}
		return;
	}
	qport = SV_ParseInt( v );

	// if "client" is present in userinfo and it is a modern client
	// then assume it can properly decode long strings and protocol extensions
	if ( !compat && *Info_ValueForKey( userinfo.data(), "client" ) != '\0' ) {
		longstr = true;
	} else {
		longstr = false;
		if ( com_protocolCompat && cl_proto != QL_RETAIL_PROTOCOL_VERSION ) {
			// enforce dm68-compatible stream for other clients
			compat = true;
		}
	}

	// we don't need these keys after connection, release some space in userinfo
	Info_RemoveKey( userinfo.data(), "challenge" );
	Info_RemoveKey( userinfo.data(), "qport" );
	Info_RemoveKey( userinfo.data(), "protocol" );
	Info_RemoveKey( userinfo.data(), "client" );

	// don't let "ip" overflow userinfo string
	if ( NET_IsLocalAddress( from ) )
		ip = "localhost";
	else
		ip = NET_AdrToString( from );

	if ( !Info_SetValueForKey( userinfo.data(), "ip", ip ) ) {
		// avoid excessive outgoing traffic
		if ( !SVC_RateLimit( &bucket, 10, 200 ) ) {
			NET_OutOfBandPrint( NS_SERVER, from, "print\nUserinfo string length exceeded.  "
				"Try removing setu cvars from your config.\n" );
		}
		return;
	}

	// run userinfo filter
	SV_SetTLD( tld.data(), from, SV_AsBool( Sys_IsLANAddress( from ) ) );
	Info_SetValueForKey( userinfo.data(), "tld", tld.data() );
	v = SV_RunFilters( userinfo.data(), from );
	if ( *v != '\0' ) {
		NET_OutOfBandPrint( NS_SERVER, from, "print\n%s\n", v );
		Com_DPrintf( "Engine rejected a connection: %s.\n", v );
		return;
	}

	// restore burst capacity
	SVC_RateRestoreBurstAddress( from, 10, 1000 );

	// quick reject
	newcl = nullptr;
	for ( client_t &client : SV_Clients() ) {
		if ( NET_CompareAdr( from, &client.netchan.remoteAddress ) ) {
			int elapsed = svs.time - client.lastConnectTime;
			if ( elapsed < ( sv_reconnectlimit->integer * 1000 ) && elapsed >= 0 ) {
				int remains = ( ( sv_reconnectlimit->integer * 1000 ) - elapsed + 999 ) / 1000;
				if ( com_developer->integer ) {
					Com_Printf( "%s:reconnect rejected : too soon\n", NET_AdrToString( from ) );
				}
				// avoid excessive outgoing traffic
				if ( !SVC_RateLimit( &bucket, 10, 200 ) ) {
					NET_OutOfBandPrint( NS_SERVER, from, "print\nReconnecting, please wait %i second%s.\n",
						remains, (remains != 1) ? "s" : "" );
				}
				return;
			}
			newcl = &client; // we may reuse this slot
			break;
		}
	}

	// if there is already a slot for this ip, reuse it
	for ( client_t &client : SV_Clients() ) {
		if ( client.state == CS_FREE ) {
			continue;
		}
		if ( NET_CompareAdr( from, &client.netchan.remoteAddress ) && client.netchan.qport == qport ) {
			// both qport and netport should match for a reconnecting client
			Com_Printf( "%s:reconnect\n", NET_AdrToString( from ) );
			newcl = &client;

			if ( newcl->state >= CS_CONNECTED ) {
				// call QVM disconnect function before calling connect again
				// fixes issues such as disappearing CTF flags in unpatched mods
				VM_Call( gvm, 1, GAME_CLIENT_DISCONNECT, SV_ClientIndex( newcl ) );

				// don't leak memory or file handles due to e.g. downloads in progress
				SV_FreeClient( newcl );
			}

			goto gotnewcl;
		}
	}

	// find a client slot
	// if "sv_privateClients" is set > 0, then that number
	// of client slots will be reserved for connections that
	// have "password" set to the value of "sv_privatePassword"
	// Info requests will report the maxclients as if the private
	// slots didn't exist, to prevent people from trying to connect
	// to a full server.
	// This is to allow us to reserve a couple slots here on our
	// servers so we can play without having to kick people.

	// check for privateClient password
	password = Info_ValueForKey( userinfo.data(), "password" );
	if ( *password && !strcmp( password, sv_privatePassword->string ) ) {
		startIndex = 0;
	} else {
		// skip past the reserved slots
		startIndex = sv_privateClients->integer;
	}

	if ( newcl && SV_ClientIndex( newcl ) >= startIndex && newcl->state == CS_FREE ) {
		Com_Printf( "%s: reuse slot %i\n", NET_AdrToString( from ), SV_ClientIndex( newcl ) );
		goto gotnewcl;
	}

	// select least used free slot
	n = 0;
	newcl = nullptr;
	for ( SV_ClientSlot slot : SV_ClientSlots() ) {
		if ( slot.index < startIndex ) {
			continue;
		}
		if ( slot.client.state == CS_FREE && ( newcl == nullptr || svs.time - slot.client.lastDisconnectTime > n ) ) {
			n = svs.time - slot.client.lastDisconnectTime;
			newcl = &slot.client;
		}
	}

	if ( !newcl ) {
		if ( NET_IsLocalAddress( from ) ) {
			count = 0;
			for ( SV_ClientSlot slot : SV_ClientSlots() ) {
				if ( slot.index < startIndex ) {
					continue;
				}
				if ( slot.client.netchan.remoteAddress.type == NA_BOT ) {
					count++;
				}
			}
			// if they're all bots
			if (count >= sv.maxclients - startIndex) {
				client_t &botClient = SV_ClientForIndex( sv.maxclients - 1 );
				SV_DropClient( &botClient, "only bots on server" );
				newcl = &botClient;
			}
			else {
				Com_Error( ERR_DROP, "server is full on local connect" );
				return;
			}
		}
		else {
			NET_OutOfBandPrint( NS_SERVER, from, "print\nServer is full.\n" );
			Com_DPrintf ("Rejected a connection.\n");
			return;
		}
	}

gotnewcl:
	localLoopback = from && from->type == NA_LOOPBACK;

	// FnQL clients send their retail-shaped ticket in the challenge request for
	// both remote and loopback connections.  Keep the ticket bound to the
	// challenge in both cases. For the in-process loopback transport, retail
	// qagame needs an authenticated provisional slot before it will accept the
	// client, but Steam can legitimately decline to mint a GameServer ticket for
	// the same running client. A signed-in provider identity is sufficient for
	// that non-networked path. This does not relax the external policy: remote
	// protocol-91 clients still need a matching ticket whenever the provider
	// exposes authentication.
	if ( cl_proto == QL_RETAIL_PROTOCOL_VERSION ) {
		fnql::server::auth::AuthRecord auth{};
		const bool hasRetailAuth = pendingRetailAuth.Consume(
			SV_AuthAddressKey( from ), challenge,
			static_cast<std::uint32_t>( Sys_Milliseconds() ), auth );
		const bool providerAvailable =
			SV_PlatformServiceAvailable( SV_PLATFORM_CAPABILITY_AUTH ) != qfalse;

		if ( localLoopback && providerAvailable ) {
			fnqlSteamStatus_t localStatus{};
			localStatus.size = sizeof( localStatus );
			if ( !FNQL_Steam_GetStatus( &localStatus ) || !localStatus.local_steam_id ) {
				fnql::server::auth::SecureErase( &auth, sizeof( auth ) );
				NET_OutOfBandPrint( NS_SERVER, from,
					"print\nA signed-in Steam identity is required for local play.\n" );
				return;
			}
			retailSteamId = localStatus.local_steam_id;
			platformAuthSession = true;
			platformAuthValidated = true;
			Com_DPrintf( "Accepted local loopback Steam identity %llu without a GameServer ticket.\n",
				static_cast<unsigned long long>( retailSteamId ) );
		} else {
			if ( providerAvailable && !hasRetailAuth ) {
				NET_OutOfBandPrint( NS_SERVER, from,
					"print\nA valid platform authentication ticket is required.\n" );
				return;
			}
			if ( hasRetailAuth ) {
				retailSteamId = auth.steamId;
				if ( providerAvailable ) {
					const fnqlSteamResult_t result = FNQL_Steam_BeginAuthSession(
						auth.ticket.data(), static_cast<std::uint32_t>( auth.ticketBytes ),
						auth.steamId );
					if ( result != FNQL_STEAM_RESULT_OK && result != FNQL_STEAM_RESULT_PENDING ) {
						fnql::server::auth::SecureErase( &auth, sizeof( auth ) );
						NET_OutOfBandPrint( NS_SERVER, from,
							"print\nPlatform authentication rejected the connection.\n" );
						return;
					}
					platformAuthSession = true;
					platformAuthTicketSession = true;
					platformAuthValidated = result == FNQL_STEAM_RESULT_OK;
				}
			}
		}
		fnql::server::auth::SecureErase( &auth, sizeof( auth ) );
	}

	// build a new connection
	// accept the new client
	// this is the only place a client_t is ever initialized
	// we got a newcl, so reset the reliableSequence and reliableAcknowledge
	*newcl = {};
	clientNum = SV_ClientIndex( newcl );
	clientStatsSessions[clientNum].Reset();
#if 0 // skip this until CS_PRIMED
	//ent = SV_GentityNum( clientNum );
	//newcl->gentity = ent;
#endif

	// save the challenge
	newcl->challenge = challenge;

	// save the address
	newcl->compat = SV_QBool( compat );
	Netchan_Setup( NS_SERVER, &newcl->netchan, from, qport, challenge,
		Netchan_SelectWireProfile( cl_proto, compat ) );

	// init the netchan queue
	newcl->netchan_end_queue = &newcl->netchan_start_queue;

	SV_CaptureClientSteamId( newcl, userinfo.data() );
	if ( retailSteamId != 0 ) {
		newcl->platformSteamIdValue = retailSteamId;
		newcl->platformAuthSession = platformAuthSession ? qtrue : qfalse;
		newcl->platformAuthTicketSession = platformAuthTicketSession ? qtrue : qfalse;
		newcl->platformAuthValidated = platformAuthValidated ? qtrue : qfalse;
		newcl->platformAuthStartedTime = static_cast<std::uint32_t>( svs.time );
		Com_sprintf( newcl->platformSteamId, SV_ArraySize( newcl->platformSteamId ),
			"%llu", static_cast<unsigned long long>( retailSteamId ) );
	}
	SV_MirrorClientSteamIdToUserinfo( newcl, userinfo.data(), SV_ArraySize( userinfo ) );

	// save the userinfo
	Q_strncpyz( newcl->userinfo, userinfo.data(), SV_ArraySize(newcl->userinfo) );

	newcl->longstr = SV_QBool( longstr );

	Q_strncpyz( newcl->tld, tld.data(), SV_ArraySize( newcl->tld ) );
	newcl->country = SV_FindCountry( newcl->tld );

	SV_UserinfoChanged( newcl, qtrue, qfalse ); // update userinfo, do not run filter

	if ( sv_clientTLD->integer ) {
		SV_SaveSequences();
	}

	// get the game a chance to reject this connection or modify the userinfo
	denied = VM_Call( gvm, 3, GAME_CLIENT_CONNECT, clientNum, qtrue, qfalse ); // firstTime = qtrue
	if ( denied ) {
		// we can't just use VM_ArgPtr, because that is only valid inside a VM_Call
		const char *str = static_cast<const char *>( GVM_ArgPtr( denied ) );

		NET_OutOfBandPrint( NS_SERVER, from, "print\n%s\n", str );
		Com_DPrintf( "Game rejected a connection: %s.\n", str );
		SV_FreeClient( newcl );
		*newcl = {};
		return;
	}

	if ( sv_clientTLD->integer ) {
		SV_InjectLocation( newcl->tld, newcl->country );
	}

	// send the connect packet to the client
	if ( cl_proto == QL_RETAIL_PROTOCOL_VERSION && !longstr ) {
		NET_OutOfBandPrint( NS_SERVER, from, "connectResponse" );
	} else if ( longstr ) {
		NET_OutOfBandPrint( NS_SERVER, from, "connectResponse %d %d", challenge, sv_proto );
	} else {
		NET_OutOfBandPrint( NS_SERVER, from, "connectResponse %d", challenge );
	}

	SV_PrintClientStateChange( newcl, CS_CONNECTED );

	newcl->state = CS_CONNECTED;
	newcl->lastSnapshotTime = svs.time - 9999; // generate a snapshot immediately
	newcl->lastPacketTime = svs.time;
	newcl->lastConnectTime = svs.time;
	newcl->lastDisconnectTime = svs.time;

	SVC_RateRestoreToxicAddress( &newcl->netchan.remoteAddress, 10, 1000 );
	newcl->justConnected = qtrue;

	// when we receive the first packet from the client, we will
	// notice that it is from a different serverid and that the
	// gamestate message was not just sent, forcing a retransmit
	newcl->gamestateMessageNum = fnql::net::PreviousSequence(
		newcl->messageAcknowledge ); // force gamestate retransmit

	// if this was the first client on the server, or the last client
	// the server can hold, send a heartbeat to the master.
	count = 0;
	for ( const client_t &client : SV_Clients() ) {
		if ( client.state >= CS_CONNECTED ) {
			count++;
		}
	}
	if ( count == 1 || count == sv.maxclients ) {
		SV_Heartbeat_f();
	}
}


/*
=====================
SV_FreeClient

Destructor for data allocated in a client structure
=====================
*/
void SV_FreeClient(client_t *client)
{
	if ( client && svs.clients ) {
		const int clientNum = SV_ClientIndex( client );
		if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
			SV_FlushClientStats( clientNum );
			clientStatsSessions[clientNum].Reset();
		}
	}
	if ( client->platformAuthTicketSession && client->platformSteamIdValue != 0 ) {
		SV_SteamP2PCloseClient( client->platformSteamIdValue );
		FNQL_Steam_EndAuthSession( client->platformSteamIdValue );
	}
	client->platformAuthTicketSession = qfalse;
	client->platformAuthSession = qfalse;
	client->platformAuthValidated = qfalse;
	SV_StopDemoRecord( client, qfalse );
	SV_Netchan_FreeQueue(client);
	SV_CloseDownload(client);
}


/*
=====================
SV_DropClient

Called when the player is totally leaving the server, either willingly
or unwillingly.  This is NOT called if the entire server is quitting
or crashing -- SV_FinalMessage() will handle that
=====================
*/
void SV_DropClient( client_t *drop, const char *reason ) {
	std::array<char, sizeof( drop->name )> name{};
	bool isBot;

	if ( drop->state == CS_ZOMBIE ) {
		return;		// already dropped
	}

	isBot = drop->netchan.remoteAddress.type == NA_BOT;

	Q_strncpyz( name.data(), drop->name, SV_ArraySize(name) );	// for further DPrintf() because drop->name will be nuked in SV_SetUserinfo()

	// Free all allocated data on the client structure
	SV_FreeClient( drop );

	// tell everyone why they got dropped
	if ( reason ) {
		SV_SendServerCommand( nullptr, "print \"%s" S_COLOR_WHITE " %s\n\"", name.data(), reason );
	}

	// call the prog function for removing a client
	// this will remove the body, among other things
	VM_Call( gvm, 1, GAME_CLIENT_DISCONNECT, SV_ClientIndex( drop ) );

	// add the disconnect command
	if ( reason ) {
		SV_SendServerCommand( drop, "disconnect \"%s\"", reason );
	}

	if ( isBot ) {
		SV_BotFreeClient( SV_ClientIndex( drop ) );
	}

	// nuke user info
	SV_SetUserinfo( SV_ClientIndex( drop ), "" );
	drop->platformSteamId[0] = '\0';

	drop->justConnected = qfalse;

	drop->lastDisconnectTime = svs.time;

	if ( isBot ) {
		// bots shouldn't go zombie, as there's no real net connection.
		drop->state = CS_FREE;
	} else {
		Q_strncpyz( drop->name, name.data(), SV_ArraySize(name) );
		SV_PrintClientStateChange( drop, CS_ZOMBIE );
		drop->state = CS_ZOMBIE;		// become free in a few seconds
	}

	if ( !reason ) {
		return;
	}

	// if this was the last client on the server, send a heartbeat
	// to the master so it is known the server is empty
	// send a heartbeat now so the master will get up to date info
	bool hasConnectedClient = false;
	for ( const client_t &client : SV_Clients() ) {
		if ( client.state >= CS_CONNECTED ) {
			hasConnectedClient = true;
			break;
		}
	}
	if ( !hasConnectedClient ) {
		SV_Heartbeat_f();
	}
}


/*
================
SV_RemainingGameState

estimates free space available for additional systeminfo keys
================
*/
int SV_RemainingGameState( void )
{
	int			len;
	entityState_t nullstate{};
	msg_t		msg;
	std::array<byte, MAX_MSGLEN_BUF> msgBuffer{};

	MSG_Init( &msg, msgBuffer.data(), MAX_MSGLEN );

	MSG_WriteLong( &msg, 7 ); // last client command

	for ( int i : SV_Indices( 256 ) ) { // simulate dummy client commands
		MSG_WriteByte( &msg, i & 127 );
	}

	// send the gamestate
	MSG_WriteByte( &msg, svc_gamestate );
	MSG_WriteLong( &msg, 7 ); // client->reliableSequence

	// write the configstrings
	for ( int index : SV_Indices( MAX_CONFIGSTRINGS ) ) {
		if ( index == CS_SERVERINFO ) {
			SV_WriteConfigstringMessage( msg, index, Cvar_InfoString( CVAR_SERVERINFO, nullptr ) );
			continue;
		}
		if ( index == CS_SYSTEMINFO ) {
			SV_WriteConfigstringMessage( msg, index, Cvar_InfoString_Big( CVAR_SYSTEMINFO, nullptr ) );
			continue;
		}
		if ( sv.configstrings[index][0] ) {
			SV_WriteConfigstringMessage( msg, index, sv.configstrings[index] );
		}
	}

	// write the baselines
	for ( int entityNum : SV_Indices( MAX_GENTITIES ) ) {
		if ( !sv.baselineUsed[ entityNum ] ) {
			continue;
		}
		SV_WriteBaselineMessage( msg, nullstate, entityNum );
	}

	MSG_WriteByte( &msg, svc_EOF );

	MSG_WriteLong( &msg, 7 ); // client num

	// write the checksum feed
	MSG_WriteLong( &msg, sv.checksumFeed );

	// finalize packet
	MSG_WriteByte( &msg, svc_EOF );

	len = PAD( msg.bit, 8 ) / 8;

	// reserve some space for potential userinfo expansion
	len += 512;

	return MAX_MSGLEN - len;
}


/*
================
SV_SendClientGameState

Sends the first message from the server to a connected client.
This will be sent on the initial connection and upon each new map load.

It will be resent if the client acknowledges a later message but has
the wrong gamestate.
================
*/
static void SV_SendClientGameState( client_t *client ) {
	entityState_t nullstate{};
	msg_t		msg;
	std::array<byte, MAX_MSGLEN_BUF> msgBuffer{};
	bool		csUpdated;

	Com_DPrintf( "SV_SendClientGameState() for %s\n", client->name );

	SV_PrintClientStateChange( client, CS_PRIMED );

	client->state = CS_PRIMED;

	client->downloading = qfalse;

	client->pureAuthentic = qfalse;
	client->gotCP = qfalse;

	// to start generating delta for packet entities
	client->gentity = SV_GentityNum( SV_ClientIndex( client ) );

	// when we receive the first packet from the client, we will
	// notice that it is from a different serverid and that the
	// gamestate message was not just sent, forcing a retransmit
	client->gamestateMessageNum = client->netchan.outgoingSequence;

	// accept usercmds starting from current server time only
	client->lastUsercmd = {};
	client->lastUsercmd.serverTime = sv.time - 1;

	MSG_Init( &msg, msgBuffer.data(), MAX_MSGLEN );

	// NOTE, MRE: all server->client messages now acknowledge
	// let the client know which reliable clientCommands we have received
	MSG_WriteLong( &msg, client->lastClientCommand );

	// send any server commands waiting to be sent first.
	// we have to do this cause we send the client->reliableSequence
	// with a gamestate and it sets the clc.serverCommandSequence at
	// the client side
	SV_UpdateServerCommandsToClient( client, &msg );

	// send the gamestate
	MSG_WriteByte( &msg, svc_gamestate );
	MSG_WriteLong( &msg, client->reliableSequence );

	// write the configstrings
	csUpdated = false;
	for ( int index : SV_Indices( MAX_CONFIGSTRINGS ) ) {
		if ( *sv.configstrings[ index ] != '\0' ) {
			SV_WritePureAwareConfigstring( msg, index );
		}
		if ( client->csUpdated[index] ) {
			csUpdated = true;
		}
		client->csUpdated[index] = qfalse;
	}

	if ( client->gamestateAck == GSA_INIT ) {
		// inital submission, accept any messageAcknowledge with matching serverId
		client->gamestateAck = GSA_SENT_ONCE;
	} else {
		if ( client->gamestateAck == GSA_SENT_ONCE && !csUpdated ) {
			// if no configstrings being updated since last submission then assume that we're (re)sending identical gamestate
		} else {
			// expect exact messageAcknowledge
			client->gamestateAck = GSA_SENT_MANY;
		}
	}

	// write the baselines
	for ( int entityNum : SV_Indices( MAX_GENTITIES ) ) {
		if ( !sv.baselineUsed[ entityNum ] ) {
			continue;
		}
		SV_WriteBaselineMessage( msg, nullstate, entityNum );
	}

	MSG_WriteByte( &msg, svc_EOF );

	MSG_WriteLong( &msg, SV_ClientIndex( client ) );

	// write the checksum feed
	MSG_WriteLong( &msg, sv.checksumFeed );

	// it is important to handle gamestate overflow
	// but at this stage client can't process any reliable commands
	// so at least try to inform him in console and release connection slot
	if ( msg.overflowed ) {
		if ( client->netchan.remoteAddress.type == NA_LOOPBACK ) {
			Com_Error( ERR_DROP, "gamestate overflow" );
		} else {
			NET_OutOfBandPrint( NS_SERVER, &client->netchan.remoteAddress, "print\n" S_COLOR_RED "SERVER ERROR: gamestate overflow\n" );
			SV_DropClient( client, "gamestate overflow" );
		}
		return;
	}

	// deliver this to the client
	if ( client->demoRecordServerId != sv.serverId ) {
		SV_StopDemoRecord( client, qfalse );
	}
	if ( sv_autoRecordDemos && sv_autoRecordDemos->integer ) {
		if ( ( sv_autoRecordDemos->integer != 2 && sv_autoRecordDemos->integer != 4 ) || !sv_cheats->integer ) {
			SV_StartDemoRecord( client );
		}
	}
	SV_SendMessageToClient( &msg, client );
}


/*
==================
SV_ClientEnterWorld
==================
*/
void SV_ClientEnterWorld( client_t *client ) {
	sharedEntity_t *ent;
	bool isBot;
	int clientNum;

	isBot = client->netchan.remoteAddress.type == NA_BOT;

	if ( !isBot ) {
		SV_PrintClientStateChange( client, CS_ACTIVE );
	} else {
		// client->serverId = sv.serverId;
	}

	client->state = CS_ACTIVE;
	client->gamestateAck = GSA_ACKED;

	client->oldServerTime = 0;

	// resend all configstrings using the cs commands since these are
	// no longer sent when the client is CS_PRIMED
	if ( !isBot ) {
		SV_UpdateConfigstrings( client );
	}

	// set up the entity for the client
	clientNum = SV_ClientIndex( client );
	ent = SV_GentityNum( clientNum );
	ent->s.number = clientNum;
	client->gentity = ent;

	client->deltaMessage = fnql::net::RetreatSequence(
		client->netchan.outgoingSequence, PACKET_BACKUP + 1u ); // force delta reset
	client->lastSnapshotTime = svs.time - 9999; // generate a snapshot immediately

	// call the game begin function
	VM_Call( gvm, 1, GAME_CLIENT_BEGIN, clientNum );
}


/*
============================================================

CLIENT COMMAND EXECUTION

============================================================
*/

/*
==================
SV_CloseDownload

clear/free any download vars
==================
*/
static void SV_CloseDownload( client_t *cl ) {
	int i;

	// EOF
	SV_CloseFileHandle( cl->download );

	*cl->downloadName = '\0';

	// Free the temporary buffer space
	for (i = 0; i < MAX_DOWNLOAD_WINDOW; i++) {
		SV_ZFree( cl->downloadBlocks[i] );
	}

}


/*
==================
SV_StopDownload_f

Abort a download if in progress
==================
*/
static void SV_StopDownload_f( client_t *cl ) {
	if (*cl->downloadName)
		Com_DPrintf( "clientDownload: %d : file \"%s\" aborted\n", SV_ClientIndex( cl ), cl->downloadName );

	SV_CloseDownload( cl );
}


/*
==================
SV_DoneDownload_f

Downloads are finished
==================
*/
static void SV_DoneDownload_f( client_t *cl ) {
	if ( cl->state == CS_ACTIVE )
		return;

	Com_DPrintf( "clientDownload: %s Done\n", cl->name );

	// resend the game state to update any clients that entered during the download
	SV_SendClientGameState( cl );
}


/*
==================
SV_NextDownload_f

The argument will be the last acknowledged block from the client, it should be
the same as cl->downloadClientBlock
==================
*/
static void SV_NextDownload_f( client_t *cl )
{
	int block = SV_ParseInt( Cmd_Argv(1) );
	const int downloadWindow = SV_DownloadWindow( cl );

	if (block == cl->downloadClientBlock) {
	Com_DPrintf( "clientDownload: %d : client acknowledge of block %d\n", SV_ClientIndex( cl ), block );

		// Find out if we are done.  A zero-length block indicates EOF
		if (cl->downloadBlockSize[cl->downloadClientBlock % downloadWindow] == 0) {
			Com_Printf( "clientDownload: %d : file \"%s\" completed\n", SV_ClientIndex( cl ), cl->downloadName );
			SV_CloseDownload( cl );
			return;
		}

		cl->downloadSendTime = svs.time;
		cl->downloadClientBlock++;
		return;
	}
	// We aren't getting an acknowledge for the correct block, drop the client
	// FIXME: this is bad... the client will never parse the disconnect message
	//			because the cgame isn't loaded yet
	SV_DropClient( cl, "broken download" );
}


/*
==================
SV_BeginDownload_f
==================
*/
static void SV_BeginDownload_f( client_t *cl ) {
	if ( cl->state == CS_ACTIVE )
		return;

	SV_StopDemoRecord( cl, qfalse );

	// Kill any existing download
	SV_CloseDownload( cl );

	// cl->downloadName is non-zero now, SV_WriteDownloadToClient will see this and open
	// the file itself
	Q_strncpyz( cl->downloadName, Cmd_Argv(1), SV_ArraySize(cl->downloadName) );

	SV_PrintClientStateChange( cl, CS_CONNECTED );
	cl->state = CS_CONNECTED;
	cl->gentity = nullptr;

	cl->downloading = qtrue;

	if ( cl->gamestateAck == GSA_ACKED ) {
		cl->gamestateAck = GSA_SENT_ONCE;
	}
}


/*
==================
SV_WriteDownloadToClient

Check to see if the client wants a file, open it if needed and start pumping the client
Fill up msg with data, returning the profile-sized byte charge used by the
global download throttle (zero when no packet was sent).
==================
*/
static int SV_WriteDownloadToClient( client_t *cl )
{
	int curindex;
	const int downloadWindow = SV_DownloadWindow( cl );
	const int downloadBlockBytes = SV_DownloadBlockBytes( cl );
	int unreferenced = 1;
	std::array<char, 1024> errorMessage{};
	std::array<char, MAX_QPATH> referencedName{};
	std::array<char, MAX_QPATH> alternateName{};
	std::array<char, MAX_QPATH> pakbuf{};
	char *pakptr;
	int numRefPaks;
	msg_t msg;
	std::array<byte, MAX_DOWNLOAD_BLKSIZE * 2 + 8> msgBuffer{};

	if ( cl->download == FS_INVALID_HANDLE ) {
		bool idPack = false;
		bool missionPack = false;
 		// Chop off filename extension.
		Q_strncpyz( pakbuf.data(), cl->downloadName, SV_ArraySize(pakbuf) );
		pakptr = strrchr( pakbuf.data(), '.' );

		if(pakptr)
		{
			*pakptr = '\0';

			// Check for pack filename extensions.
			if ( SV_IsPackExtension( pakptr + 1 ) )
			{
				// Check whether the file appears in the list of referenced
				// paks to prevent downloading of arbitrary files.
				Cmd_TokenizeStringIgnoreQuotes( sv_referencedPakNames->string );
				numRefPaks = Cmd_Argc();

				for( int refPak : SV_Indices( numRefPaks ) )
				{
					SV_StripPackExtension( Cmd_Argv( refPak ), referencedName.data(), SV_ArraySize(referencedName) );
					if(!FS_FilenameCompare(referencedName.data(), pakbuf.data()))
					{
						unreferenced = 0;

						// now that we know the file is referenced,
						// check whether it's legal to download it.
						missionPack = FS_idPak(pakbuf.data(), BASETA, NUM_TA_PAKS);
						idPack = missionPack || FS_idPak( pakbuf.data(), BASEGAME, NUM_ID_PAKS );

						break;
					}
				}
			}
		}

		cl->download = FS_INVALID_HANDLE;

		// We open the file here
		if ( !(sv_allowDownload->integer & DLF_ENABLE) ||
			(sv_allowDownload->integer & DLF_NO_UDP) ||
			idPack || unreferenced ) {

			cl->downloadSize = -1;
		} else {
			cl->downloadSize = FS_SV_FOpenFileRead( cl->downloadName, &cl->download );
			if ( cl->downloadSize < 0 && pakptr && SV_IsPackExtension( pakptr + 1 ) ) {
				Com_sprintf( alternateName.data(), SV_ArraySize(alternateName), "%s%s",
					pakbuf.data(), !Q_stricmp( pakptr + 1, "pk3" ) ? ".pak" : ".pk3" );
				cl->downloadSize = FS_SV_FOpenFileRead( alternateName.data(), &cl->download );
			}
		}

		if ( cl->downloadSize < 0 || !(sv_allowDownload->integer & DLF_ENABLE) ||
			(sv_allowDownload->integer & DLF_NO_UDP) ||
			idPack || unreferenced ) {

			// cannot auto-download file
			if(unreferenced)
			{
				Com_Printf("clientDownload: %d : \"%s\" is not referenced and cannot be downloaded.\n", SV_ClientIndex( cl ), cl->downloadName);
				Com_sprintf(errorMessage.data(), SV_ArraySize(errorMessage), "File \"%s\" is not referenced and cannot be downloaded.", cl->downloadName);
			}
			else if (idPack) {
				Com_Printf("clientDownload: %d : \"%s\" cannot download official game packs\n", SV_ClientIndex( cl ), cl->downloadName);
				if (missionPack) {
					Com_sprintf(errorMessage.data(), SV_ArraySize(errorMessage), "Cannot autodownload Team Arena file \"%s\"\n"
									"The Team Arena mission pack can be found in your local game store.", cl->downloadName);
				}
				else {
					Com_sprintf(errorMessage.data(), SV_ArraySize(errorMessage), "Cannot autodownload official game pack \"%s\"", cl->downloadName);
				}
			}
			else if ( !(sv_allowDownload->integer & DLF_ENABLE) ||
				(sv_allowDownload->integer & DLF_NO_UDP) ) {

				Com_Printf("clientDownload: %d : \"%s\" download disabled\n", SV_ClientIndex( cl ), cl->downloadName);
				if ( sv.pure != 0 ) {
					Com_sprintf(errorMessage.data(), SV_ArraySize(errorMessage), "Could not download \"%s\" because autodownloading is disabled on the server.\n\n"
										"You will need to get this file elsewhere before you "
										"can connect to this pure server.\n", cl->downloadName);
				} else {
					Com_sprintf(errorMessage.data(), SV_ArraySize(errorMessage), "Could not download \"%s\" because autodownloading is disabled on the server.\n\n"
                    "The server you are connecting to is not a pure server, "
                    "set autodownload to No in your settings and you might be "
                    "able to join the game anyway.\n", cl->downloadName);
				}
			} else {
        // NOTE TTimo this is NOT supposed to happen unless bug in our filesystem scheme?
        //   if the pk3 is referenced, it must have been found somewhere in the filesystem
				Com_Printf("clientDownload: %d : \"%s\" file not found on server\n", SV_ClientIndex( cl ), cl->downloadName);
				Com_sprintf(errorMessage.data(), SV_ArraySize(errorMessage), "File \"%s\" not found on server for autodownloading.\n", cl->downloadName);
			}

			MSG_Init( &msg, msgBuffer.data(), SV_ArraySize(msgBuffer) - 8 );
			MSG_WriteLong( &msg, cl->lastClientCommand );

			MSG_WriteByte( &msg, svc_download );
			MSG_WriteShort( &msg, 0 ); // client is expecting block zero
			MSG_WriteLong( &msg, -1 ); // illegal file size
			MSG_WriteString( &msg, errorMessage.data() );

			MSG_WriteByte( &msg, svc_EOF );
			SV_Netchan_Transmit( cl, &msg );

			*cl->downloadName = '\0';

			SV_CloseFileHandle( cl->download );

			return downloadBlockBytes;
		}

		Com_Printf( "clientDownload: %d : beginning \"%s\"\n", SV_ClientIndex( cl ), cl->downloadName );

		cl->downloadCurrentBlock = cl->downloadClientBlock = cl->downloadXmitBlock = 0;
		cl->downloadCount = 0;
		cl->downloadEOF = qfalse;
	}

	// Perform any reads that we need to
	while (cl->downloadCurrentBlock - cl->downloadClientBlock < downloadWindow &&
		cl->downloadSize != cl->downloadCount) {

		curindex = (cl->downloadCurrentBlock % downloadWindow);

		if (!cl->downloadBlocks[curindex])
			cl->downloadBlocks[curindex] = SV_ZMallocArray<unsigned char>( downloadBlockBytes );

		cl->downloadBlockSize[curindex] = FS_Read( cl->downloadBlocks[curindex],
			downloadBlockBytes, cl->download );

		if (cl->downloadBlockSize[curindex] < 0) {
			// EOF right now
			cl->downloadCount = cl->downloadSize;
			break;
		}

		cl->downloadCount += cl->downloadBlockSize[curindex];

		// Load in next block
		cl->downloadCurrentBlock++;
	}

	// Check to see if we have eof condition and add the EOF block
	if (cl->downloadCount == cl->downloadSize &&
		!cl->downloadEOF &&
		cl->downloadCurrentBlock - cl->downloadClientBlock < downloadWindow) {

		cl->downloadBlockSize[cl->downloadCurrentBlock % downloadWindow] = 0;
		cl->downloadCurrentBlock++;

		cl->downloadEOF = qtrue;  // We have added the EOF block
	}

	if (cl->downloadClientBlock == cl->downloadCurrentBlock)
		return 0; // Nothing to transmit

	// Write out the next section of the file, if we have already reached our window,
	// automatically start retransmitting
	if (cl->downloadXmitBlock == cl->downloadCurrentBlock)
	{
		// We have transmitted the complete window, should we start resending?
		if (svs.time - cl->downloadSendTime > 1000)
			cl->downloadXmitBlock = cl->downloadClientBlock;
		else
			return 0;
	}

	// Send current block
	curindex = (cl->downloadXmitBlock % downloadWindow);

	MSG_Init( &msg, msgBuffer.data(), SV_ArraySize(msgBuffer) - 8 );
	MSG_WriteLong( &msg, cl->lastClientCommand );

	MSG_WriteByte( &msg, svc_download );
	MSG_WriteShort( &msg, cl->downloadXmitBlock );

	// block zero is special, contains file size
	if ( cl->downloadXmitBlock == 0 )
		MSG_WriteLong( &msg, cl->downloadSize );

	MSG_WriteShort( &msg, cl->downloadBlockSize[curindex] );

	// Write the block
	if ( cl->downloadBlockSize[curindex] > 0 )
		MSG_WriteData( &msg, cl->downloadBlocks[curindex], cl->downloadBlockSize[curindex] );

	MSG_WriteByte( &msg, svc_EOF );
	SV_Netchan_Transmit( cl, &msg );

	Com_DPrintf( "clientDownload: %d : writing block %d\n", SV_ClientIndex( cl ), cl->downloadXmitBlock );

	// Move on to the next block
	// It will get sent with next snap shot.  The rate will keep us in line.
	cl->downloadXmitBlock++;
	cl->downloadSendTime = svs.time;

	return downloadBlockBytes;
}


/*
==================
SV_SendQueuedMessages

Send one round of fragments, or queued messages to all clients that have data pending.
Return the shortest time interval for sending next packet to client
==================
*/
int SV_SendQueuedMessages( void )
{
	int retval = -1, nextFragT;

	for( client_t &client : SV_Clients() )
	{
		if ( client.state )
		{
			nextFragT = SV_RateMsec(&client);

			if(!nextFragT)
				nextFragT = SV_Netchan_TransmitNextFragment(&client);

			if(nextFragT >= 0 && (retval == -1 || retval > nextFragT))
				retval = nextFragT;
		}
	}

	return retval;
}


/*
==================
SV_SendDownloadMessages

Send one round of download messages to all clients
==================
*/
int SV_SendDownloadMessages( void )
{
	int chargedBytes = 0;

	for( client_t &client : SV_Clients() )
	{
		if ( client.state >= CS_CONNECTED && *client.downloadName )
		{
			chargedBytes += SV_WriteDownloadToClient( &client );
		}
	}

	return chargedBytes;
}


/*
=================
SV_Disconnect_f

The client is going to disconnect, so remove the connection immediately  FIXME: move to game?
=================
*/
static void SV_Disconnect_f( client_t *cl ) {
	SV_DropClient( cl, "disconnected" );
}


/*
=================
SV_VerifyPaks_f

If we are pure, disconnect the client if they do no meet the following conditions:

1. the first two checksums match our view of cgame and ui
2. there are no any additional checksums that we do not have

This routine would be a bit simpler with a goto but i abstained

=================
*/
static bool SV_HasDuplicateClientChecksums( const std::array<int, 512> &checksums, int count ) {
	for ( int i : SV_Indices( count ) ) {
		for ( int j : SV_Indices( i + 1, count ) ) {
			if ( checksums[i] == checksums[j] ) {
				return true;
			}
		}
	}
	return false;
}


static bool SV_ClientChecksumsArePure( const std::array<int, 512> &checksums, int count ) {
	for ( int i : SV_Indices( count ) ) {
		if ( !FS_IsPureChecksum( checksums[i] ) ) {
			return false;
		}
	}
	return true;
}


static int SV_MixedClientChecksumFeed( const std::array<int, 512> &checksums, int count ) {
	int checksum = sv.checksumFeed;

	for ( int i : SV_Indices( count ) ) {
		checksum ^= checksums[i];
	}
	checksum ^= count;

	return checksum;
}


static void SV_VerifyPaks_f( client_t *cl ) {
	int nBinChkSum, nChkSum1, nClientPaks, i, nCurArg;
	std::array<int, 512> nClientChkSum{};
	const char *pArg;
	bool bGood = true;

	// if we are pure, we "expect" the client to load certain things from
	// certain pk3 files, namely we want the client to have loaded the
	// retail cgame binary that we think should be loaded based on the pure setting
	//
	if ( sv.pure != 0 ) {

		nBinChkSum = nChkSum1 = 0;
		bGood = FS_FileIsInPAK( QL_NATIVE_CGAME_DLL, &nBinChkSum, nullptr );

		nClientPaks = Cmd_Argc();

		if ( nClientPaks > SV_ArraySize(nClientChkSum) )
			nClientPaks = SV_ArraySize(nClientChkSum);

		// start at arg 2 ( skip serverId cl_paks )
		nCurArg = 1;

		pArg = Cmd_Argv(nCurArg++);
		if ( !*pArg ) {
			bGood = false;
		}
		else
		{
			// we may get incoming cp sequences from a previous serverId, which we need to ignore
			if ( SV_ParseInt( pArg ) != sv.serverId /* || !cl->gamestateAcked */ ) {
				Com_DPrintf( "ignoring outdated cp command from client %s\n", cl->name );
				return;
			}
		}

		// we basically use this while loop to avoid using 'goto' :)
		while (bGood) {

			// must be at least 6: "cl_paks serverId bin @ firstref ... numChecksums"
			// numChecksums is encoded
			if (nClientPaks < 6) {
				bGood = false;
				break;
			}
			// verify first to be the binary checksum
			pArg = Cmd_Argv(nCurArg++);
			if ( !*pArg || *pArg == '@' || SV_ParseInt(pArg) != nBinChkSum ) {
				bGood = false;
				break;
			}
			// should be sitting at the delimeter now
			pArg = Cmd_Argv(nCurArg++);
			if (*pArg != '@') {
				bGood = false;
				break;
			}
			// store checksums since tokenization is not re-entrant
			for (i = 0; nCurArg < nClientPaks; i++) {
				nClientChkSum[i] = SV_ParseInt(Cmd_Argv(nCurArg++));
			}

			// store number to compare against (minus one cause the last is the number of checksums)
			nClientPaks = i - 1;

			// make sure none of the client check sums are the same
			// so the client can't send 5 the same checksums
			bGood = !SV_HasDuplicateClientChecksums( nClientChkSum, nClientPaks );
			if ( !bGood )
				break;

			// check if the client has provided any pure checksums of pk3 files not loaded by the server
			bGood = SV_ClientChecksumsArePure( nClientChkSum, nClientPaks );
			if ( !bGood ) {
				break;
			}

			// check if the number of checksums was correct
			nChkSum1 = SV_MixedClientChecksumFeed( nClientChkSum, nClientPaks );
			if (nChkSum1 != nClientChkSum[nClientPaks]) {
				bGood = false;
				break;
			}

			// break out
			break;
		}

		cl->gotCP = qtrue;

		if ( bGood ) {
			cl->pureAuthentic = qtrue;
		} else {
			cl->pureAuthentic = qfalse;
			cl->lastSnapshotTime = svs.time - 9999; // generate a snapshot immediately
			cl->state = CS_ZOMBIE; // skip delta generation
			SV_SendClientSnapshot( cl );
			cl->state = CS_ACTIVE;
			SV_DropClient( cl, "Unpure client detected. Invalid pack files referenced!" );
		}
	}
}


/*
=================
SV_ResetPureClient_f
=================
*/
static void SV_ResetPureClient_f( client_t *cl ) {
	cl->pureAuthentic = qfalse;
	cl->gotCP = qfalse;
}


/*
=================
SV_UserinfoChanged

Pull specific info from a newly changed userinfo string
into a more C friendly form.
=================
*/
void SV_UserinfoChanged( client_t *cl, qboolean updateUserinfo, qboolean runFilter ) {
	std::array<char, MAX_NAME_LENGTH> buf{};
	const char *val;
	const char *ip;
	int	i;

	if ( cl->netchan.remoteAddress.type == NA_BOT ) {
		cl->lastSnapshotTime = svs.time - 9999; // generate a snapshot immediately
		cl->snapshotMsec = 1000 / sv_fps->integer;
		cl->rate = 0;
		return;
	}

	// rate command

	// if the client is on the same subnet as the server and we aren't running an
	// internet public server, assume they don't need a rate choke
	if ( cl->netchan.remoteAddress.type == NA_LOOPBACK || ( cl->netchan.isLANAddress && com_dedicated->integer != 2 && sv_lanForceRate->integer ) ) {
		cl->rate = 0; // lans should not rate limit
	} else {
		val = Info_ValueForKey( cl->userinfo, "rate" );
		if ( val[0] )
			cl->rate = SV_ParseInt( val );
		else
			cl->rate = 10000; // was 3000

		if ( sv_maxRate->integer ) {
			if ( cl->rate > sv_maxRate->integer )
				cl->rate = sv_maxRate->integer;
		}

		if ( sv_minRate->integer ) {
			if ( cl->rate < sv_minRate->integer )
				cl->rate = sv_minRate->integer;
		}
	}

	// snaps command
	val = Info_ValueForKey( cl->userinfo, "snaps" );
	if ( val[0] && !NET_IsLocalAddress( &cl->netchan.remoteAddress ) )
		i = SV_ParseInt( val );
	else
		i = sv_fps->integer; // sync with server

	// range check
	if ( i < 1 )
		i = 1;
	else if ( i > sv_fps->integer )
		i = sv_fps->integer;

	i = 1000 / i; // from FPS to milliseconds

	if ( i != cl->snapshotMsec )
	{
		// Reset last sent snapshot so we avoid desync between server frame time and snapshot send time
		cl->lastSnapshotTime = svs.time - 9999; // generate a snapshot immediately
		cl->snapshotMsec = i;
	}

	if ( !updateUserinfo )
		return;

	SV_MirrorClientSteamIdToUserinfo( cl, cl->userinfo, SV_ArraySize( cl->userinfo ) );

	// name for C code
	val = Info_ValueForKey( cl->userinfo, "name" );
	// truncate if it is too long as it may cause memory corruption in OSP mod
	if ( gvm->forceDataMask && strlen( val ) >= buf.size() ) {
		Q_strncpyz( buf.data(), val, SV_ArraySize(buf) );
		Info_SetValueForKey( cl->userinfo, "name", buf.data() );
		val = buf.data();
	}
	Q_strncpyz( cl->name, val, SV_ArraySize( cl->name ) );

	val = Info_ValueForKey( cl->userinfo, "handicap" );
	if ( val[0] ) {
		i = SV_ParseInt( val );
		if ( i <= 0 || i > 100 || strlen( val ) > 4 ) {
			Info_SetValueForKey( cl->userinfo, "handicap", "100" );
		}
	}

	// TTimo
	// maintain the IP information
	// the banning code relies on this being consistently present
	if ( NET_IsLocalAddress( &cl->netchan.remoteAddress ) )
		ip = "localhost";
	else
		ip = NET_AdrToString( &cl->netchan.remoteAddress );

	if ( !Info_SetValueForKey( cl->userinfo, "ip", ip ) )
		SV_DropClient( cl, "userinfo string length exceeded" );

	Info_SetValueForKey( cl->userinfo, "tld", cl->tld );

	if ( runFilter )
	{
		val = SV_RunFilters( cl->userinfo, &cl->netchan.remoteAddress );
		if ( *val != '\0' )
		{
			SV_DropClient( cl, val );
		}
	}
}


/*
==================
SV_UpdateUserinfo_f
==================
*/
static void SV_UpdateUserinfo_f( client_t *cl ) {
	const char *info;

	info = Cmd_Argv( 1 );

	if ( Cmd_Argc() != 2 || *info == '\0' ) {
		// this is something erroneous, client should never send that
		return;
	}

	Q_strncpyz( cl->userinfo, info, SV_ArraySize( cl->userinfo ) );

	SV_UserinfoChanged( cl, qtrue, qtrue ); // update userinfo, run filter
	// call prog code to allow overrides
	VM_Call( gvm, 1, GAME_CLIENT_USERINFO_CHANGED, SV_ClientIndex( cl ) );
}

extern int SV_Strlen( const char *str );

/*
==================
SV_PrintLocations_f
==================
*/
void SV_PrintLocations_f( client_t *client ) {
	int len;
	int max_namelength;
	int max_ctrylength;
	std::array<char, 128> line{};
	std::array<char, 1400 - 4 - 8> buf{};
	std::array<char, MAX_NAME_LENGTH> filln{};
	std::array<char, 64> fillc{};
	char *s;

	if ( !svs.clients )
		return;

	max_namelength = 4; // strlen( "name" )
	max_ctrylength = 7; // strlen( "country" )

	// first pass: save and determine max.lengths of name/address fields
	for ( const client_t &cl : SV_Clients() )
	{
		if ( cl.state == CS_FREE )
			continue;

		len = SV_Strlen( cl.name );// name length without color sequences
		if ( len > max_namelength )
			max_namelength = len;

		len = strlen( cl.country );
		if ( len > max_ctrylength )
			max_ctrylength = len;
	}

	s = buf.data(); *s = '\0';
	std::fill_n( filln.data(), max_namelength, '-' );
	filln[max_namelength] = '\0';
	std::fill_n( fillc.data(), max_ctrylength, '-' );
	fillc[max_ctrylength] = '\0';
	// Start this on a new line to be viewed properly in console
	s = Q_stradd( s, "\n" );
	Com_sprintf( line.data(), SV_ArraySize(line), "ID %-*s CC Country\n", max_namelength, "Name" );
	s = Q_stradd( s, line.data() );
	Com_sprintf( line.data(), SV_ArraySize(line), "-- %s -- %s\n", filln.data(), fillc.data() );
	s = Q_stradd( s, line.data() );

	for ( SV_ClientSlot slot : SV_ClientSlots() )
	{
		if ( slot.client.state == CS_FREE )
			continue;

		len = Com_sprintf( line.data(), SV_ArraySize(line), "%2i %s%-*s" S_COLOR_WHITE " %2s %s\n",
			slot.index, slot.client.name, max_namelength-SV_Strlen(slot.client.name), "", slot.client.tld, slot.client.country );

		if ( s - buf.data() + len >= SV_ArraySize(buf) - 1 ) // flush accumulated buffer
		{
			if ( client )
				NET_OutOfBandPrint( NS_SERVER, &client->netchan.remoteAddress, "print\n%s", buf.data() );
			else
				Com_Printf( "%s", buf.data() );

			s = buf.data(); *s = '\0';
		}

		s = Q_stradd( s, line.data() );
	}

	if ( buf[0] )
	{
		if ( client )
			NET_OutOfBandPrint( NS_SERVER, &client->netchan.remoteAddress, "print\n%s", buf.data() );
		else
			Com_Printf( "%s", buf.data() );
	}
}


struct ucmd_t {
	const char *name;
	void (*func)( client_t *cl );
};

static constexpr std::array<ucmd_t, 9> ucmds = {{
	{"userinfo", SV_UpdateUserinfo_f},
	{"disconnect", SV_Disconnect_f},
	{"cp", SV_VerifyPaks_f},
	{"vdr", SV_ResetPureClient_f},
	{"download", SV_BeginDownload_f},
	{"nextdl", SV_NextDownload_f},
	{"stopdl", SV_StopDownload_f},
	{"donedl", SV_DoneDownload_f},
	{"locations", SV_PrintLocations_f},
}};

static const ucmd_t *SV_FindClientCommand( const char *name )
{
	for ( const auto &ucmd : ucmds ) {
		if ( !strcmp( name, ucmd.name ) ) {
			return &ucmd;
		}
	}
	return nullptr;
}


/*
================
SV_FloodProtect
================
*/
static bool SV_FloodProtect( client_t *cl ) {
	if ( sv_floodProtect->integer ) {
		return SVC_RateLimit( &cl->cmd_rate, 8, 500 );
	} else {
		return false;
	}
}


/*
==================
SV_ExecuteClientCommand

Also called by bot code
==================
*/
qboolean SV_ExecuteClientCommand( client_t *cl, const char *s ) {
	const ucmd_t *ucmd;
	bool bFloodProtect;
	bool isBot;

	Cmd_TokenizeString( s );

	// malicious users may try using too many string commands
	// to lag other players.  If we decide that we want to stall
	// the command, we will stop processing the rest of the packet,
	// including the usercmd.  This causes flooders to lag themselves
	// but not other people

	// We don't do this when the client hasn't been active yet since it's
	// normal to spam a lot of commands when downloading
	isBot = cl->netchan.remoteAddress.type == NA_BOT;
	bFloodProtect = !isBot && cl->state >= CS_ACTIVE;

	// see if it is a server level command
	ucmd = SV_FindClientCommand( Cmd_Argv(0) );
	if ( ucmd != nullptr ) {
		if ( ucmd->func == SV_UpdateUserinfo_f ) {
			if ( bFloodProtect ) {
				if ( SVC_RateLimit( &cl->info_rate, 5, 1000 ) ) {
					return qfalse; // lag flooder
				}
			}
		} else if ( ucmd->func == SV_PrintLocations_f && !sv_clientTLD->integer ) {
			ucmd = nullptr; // bypass this command to the gamecode
		}
		if ( ucmd != nullptr ) {
			ucmd->func( cl );
			bFloodProtect = false;
		}
	}

	// if ( !isBot && ( !cl->gamestateAcked || sv.serverId != cl->serverId ) ) {
	//		Com_Printf( "%s: ignoring pre map_restart / outdated client command '%s'\n", cl->name, s );
	//	return qtrue;
	// }

#ifndef DEDICATED
	if ( !com_cl_running->integer && bFloodProtect && SV_FloodProtect( cl ) ) {
#else
	if ( bFloodProtect && SV_FloodProtect( cl ) ) {
#endif
		// ignore any other text messages from this client but let them keep playing
		Com_DPrintf( "client text ignored for %s: %s\n", cl->name, Cmd_Argv(0) );
	} else {
		// pass unknown strings to the game
		if ( ucmd == nullptr && sv.state == SS_GAME && cl->state >= CS_PRIMED ) {
			if ( gvm->forceDataMask )
				Cmd_Args_Sanitize( "\n\r;" ); // handle ';' for OSP
			else
				Cmd_Args_Sanitize( "\n\r" );
			VM_Call( gvm, 1, GAME_CLIENT_COMMAND, SV_ClientIndex( cl ) );
		}
	}

	return qtrue;
}


/*
===============
SV_ClientCommand
===============
*/
static bool SV_ClientCommand( client_t *cl, msg_t *msg ) {
	int		seq;
	const char	*s;

	seq = MSG_ReadLong( msg );
	s = MSG_ReadString( msg );

	// see if we have already executed it
	if ( seq - cl->lastClientCommand <= 0 ) {
		return true;
	}

	Com_DPrintf( "clientCommand: %s : %i : %s\n", cl->name, seq, s );

	// drop the connection if we have somehow lost commands
	if ( seq - cl->lastClientCommand > 1 ) {
		Com_Printf( "Client %s lost %i clientCommands\n", cl->name, seq - cl->lastClientCommand - 1 );
		SV_DropClient( cl, "Lost reliable commands" );
		return false;
	}

	if ( !SV_ExecuteClientCommand( cl, s ) ) {
		return false;
	}

	cl->lastClientCommand = seq;
	Q_strncpyz( cl->lastClientCommandString, s, SV_ArraySize( cl->lastClientCommandString ) );

	return true; // continue processing
}


//==================================================================================


/*
==================
SV_ClientThink

Also called by bot code
==================
*/
void SV_ClientThink (client_t *cl, usercmd_t *cmd) {
	cl->lastUsercmd = *cmd;

	if ( cl->state != CS_ACTIVE ) {
		return;		// may have been kicked during the last usercmd
	}

	VM_Call( gvm, 1, GAME_CLIENT_THINK, SV_ClientIndex( cl ) );
}


/*
==================
SV_UserMove

The message usually contains all the movement commands
that were in the last three packets, so that the information
in dropped packets can be recovered.

On very fast clients, there may be multiple usercmd packed into
each of the backup packets.
==================
*/
static void SV_UserMove( client_t *cl, msg_t *msg, bool delta ) {
	int			key;
	int			cmdCount;
	static const usercmd_t nullcmd{};
	std::array<usercmd_t, MAX_PACKET_USERCMDS> cmds{};
	usercmd_t	*cmd;
	const usercmd_t *oldcmd;

	if ( delta ) {
		cl->deltaMessage = cl->messageAcknowledge;
	} else {
		cl->deltaMessage = fnql::net::RetreatSequence(
			cl->netchan.outgoingSequence, PACKET_BACKUP + 1u ); // force delta reset
	}

	cmdCount = MSG_ReadByte( msg );

	if ( cmdCount < 1 ) {
		Com_Printf( "cmdCount < 1\n" );
		return;
	}

	if ( cmdCount > MAX_PACKET_USERCMDS ) {
		Com_Printf( "cmdCount > MAX_PACKET_USERCMDS\n" );
		return;
	}

	// use the checksum feed in the key
	key = sv.checksumFeed;
	// also use the message acknowledge
	key ^= cl->messageAcknowledge;
	// also use the last acknowledged server command in the key
	key ^= MSG_HashKey(cl->reliableCommands[ cl->reliableAcknowledge & (MAX_RELIABLE_COMMANDS-1) ], 32);

	oldcmd = &nullcmd;
	for ( int i : SV_Indices( cmdCount ) ) {
		cmd = &cmds[i];
		MSG_ReadDeltaUsercmdKey( msg, key, oldcmd, cmd );
		oldcmd = cmd;
	}

	// save time for ping calculation
	if ( cl->frames[ cl->messageAcknowledge & PACKET_MASK ].messageAcked == 0 ) {
		cl->frames[ cl->messageAcknowledge & PACKET_MASK ].messageAcked = Sys_Milliseconds();
	}

	// if this is the first usercmd we have received
	// this gamestate, put the client into the world
	if ( cl->state == CS_PRIMED ) {
		if ( sv.pure != 0 && !cl->gotCP ) {
			// we didn't get a cp yet, don't assume anything and just send the gamestate all over again
			if ( !SVC_RateLimit( &cl->gamestate_rate, 2, 1000 ) ) {
				Com_DPrintf( "%s: didn't get cp command, resending gamestate\n", cl->name );
				SV_SendClientGameState( cl );
			}
			return;
		}
		SV_ClientEnterWorld( cl );
		// the moves can be processed normally
	}

	// a bad cp command was sent, drop the client
	if ( sv.pure != 0 && !cl->pureAuthentic ) {
		SV_DropClient( cl, "Cannot validate pure client!" );
		return;
	}

	if ( cl->state != CS_ACTIVE ) {
		cl->deltaMessage = fnql::net::RetreatSequence(
			cl->netchan.outgoingSequence, PACKET_BACKUP + 1u ); // force delta reset
		return;
	}

	// usually, the first couple commands will be duplicates
	// of ones we have previously received, but the servertimes
	// in the commands will cause them to be immediately discarded
	for ( int i : SV_Indices( cmdCount ) ) {
		// if this is a cmd from before a map_restart ignore it
		if ( cmds[i].serverTime - cmds[cmdCount-1].serverTime > 0 ) {
			continue;
		}
		// extremely lagged or cmd from before a map_restart
		//if ( cmds[i].serverTime > svs.time + 3000 ) {
		//	continue;
		//}
		// don't execute if this is an old cmd which is already executed
		// these old cmds are included when cl_packetdup > 0
		//if ( cmds[i].serverTime <= cl->lastUsercmd.serverTime ) {
		if ( cmds[i].serverTime - cl->lastUsercmd.serverTime <= 0 ) {
			continue;
		}
		SV_ClientThink( cl, &cmds[ i ] );
	}
}


/*
===========================================================================

USER CMD EXECUTION

===========================================================================
*/

/*
===================
SV_AcknowledgeGamestate
===================
*/
static bool SV_AcknowledgeGamestate( client_t *cl, int serverId )
{
	if ( serverId == sv.serverId ) {
		// accept either exact message delta or any positive delta with known identical gamestate sent before
		if ( cl->messageAcknowledge == cl->gamestateMessageNum ||
			( fnql::net::IsNewerSequence( cl->messageAcknowledge,
				cl->gamestateMessageNum ) && cl->gamestateAck == GSA_SENT_ONCE ) ) {
			cl->gamestateAck = GSA_ACKED;
			// this client has acknowledged the new gamestate so it's
			// safe to start sending it the real time again
			Com_DPrintf( "%s acknowledged gamestate\n", cl->name );
			cl->oldServerTime = 0;
			return true;
		}
	}
	return false;
}


/*
===================
SV_ExecuteClientMessage

Parse a client packet
===================
*/
void SV_ExecuteClientMessage( client_t *cl, msg_t *msg ) {
	int	c;
	int	serverId;
	int reliableAcknowledge;
	std::uint32_t pendingReliable = 0;

	MSG_Bitstream( msg );

	serverId = MSG_ReadLong( msg );

	cl->messageAcknowledge = MSG_ReadLong( msg );

	//if ( cl->messageAcknowledge < 0 ) {
	if ( !fnql::net::IsNewerSequence( cl->netchan.outgoingSequence,
		cl->messageAcknowledge ) ) {
		// usually only hackers create messages like this
		// it is more annoying for them to let them hanging
#ifdef _DEBUG
		SV_DropClient( cl, "DEBUG: illegible client message" );
#endif
		return;
	}

	reliableAcknowledge = MSG_ReadLong( msg );

	if ( !fnql::net::PendingCounterCount( cl->reliableSequence,
		reliableAcknowledge, pendingReliable ) ) {
#ifdef _DEBUG
		SV_DropClient( cl, "DEBUG: illegible client message" );
#endif
		return;
	}

	// NOTE: when the client message is fux0red the acknowledgement numbers
	// can be out of range, this could cause the server to send thousands of server
	// commands which the server thinks are not yet acknowledged in SV_UpdateServerCommandsToClient
	if ( pendingReliable > MAX_RELIABLE_COMMANDS ) {
		// usually only hackers create messages like this
		// it is more annoying for them to let them hanging
#ifdef _DEBUG
		SV_DropClient( cl, "DEBUG: illegible client message" );
#else
		Com_Printf( S_COLOR_YELLOW "WARNING: dropping %u commands from %s\n",
			pendingReliable, cl->name );
#endif
		cl->reliableAcknowledge = cl->reliableSequence;
		return;
	}

	cl->reliableAcknowledge = reliableAcknowledge;

	cl->justConnected = qfalse;

	if ( cl->netchan.wireProfile == NETCHAN_WIRE_QL_RETAIL ) {
		(void)MSG_ReadByte( msg );
	}

	// cl->serverId = serverId;

	// if this is a usercmd from a previous gamestate,
	// ignore it or retransmit the current gamestate
	//
	// if the client was downloading, let it stay at whatever serverId and
	// gamestate it was at.  This allows it to keep downloading even when
	// the gamestate changes.  After the download is finished, we'll
	// notice and send it a new game state
	//
	if ( cl->state == CS_CONNECTED ) {
		if ( !cl->downloading ) {
			// send initial gamestate, client may not acknowledge it in next command but start downloading after SV_ClientCommand()
			if ( !SVC_RateLimit( &cl->gamestate_rate, 1, 1000 ) ) {
				SV_SendClientGameState( cl );
			}
			return;
		}
	} else if ( cl->gamestateAck != GSA_ACKED ) {
		// early check for gamestate acknowledge
		SV_AcknowledgeGamestate( cl, serverId );
	}
	// else if ( cl->state == CS_PRIMED ) {
		// in case of download intention client replies with (messageAcknowledge - gamestateMessageNum) >= 0 and (serverId == sv.serverId), sv.serverId can drift away later
		// in case of lost gamestate client replies with (messageAcknowledge - gamestateMessageNum) > 0 and (serverId == sv.serverId)
		// in case of disconnect/etc. client replies with any serverId
	//}

	// read optional clientCommand strings
	do {
		c = MSG_ReadByte( msg );
		if ( c != clc_clientCommand ) {
			break;
		}
		if ( !SV_ClientCommand( cl, msg ) ) {
			return;	// we couldn't execute it because of the flood protection
		}
		if ( cl->state == CS_ZOMBIE ) {
			return;	// disconnect command
		}
	} while ( true );

	if ( cl->gamestateAck != GSA_ACKED ) {
		// late check for gamestate acknowledge & resend
		if ( cl->state == CS_PRIMED ) {
			if ( !SV_AcknowledgeGamestate( cl, serverId ) ) {
				Com_DPrintf( "%s: dropped gamestate, resending\n", cl->name );
				if ( !SVC_RateLimit( &cl->gamestate_rate, 1, 1000 ) ) {
					SV_SendClientGameState( cl );
				}
				return; // message delta or serverId mismatch
			}
		} else {
			return; // cl->state <= CS_CONNECTED
		}
	}

	// read the usercmd_t
	if ( c == clc_move ) {
		SV_UserMove( cl, msg, true );
	} else if ( c == clc_moveNoDelta ) {
		SV_UserMove( cl, msg, false );
	} else if ( c != clc_EOF ) {
		Com_Printf( "WARNING: bad command byte %i for client %i\n", c, SV_ClientIndex( cl ) );
	}
//	if ( msg->readcount != msg->cursize ) {
//		Com_Printf( "WARNING: Junk at end of packet for client %i\n", SV_ClientIndex( cl ) );
//	}
}
