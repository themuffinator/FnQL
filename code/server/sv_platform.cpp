/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL.

FnQL is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.
===========================================================================
*/

#include "server.h"
#include "server_cvar_compat.hpp"
#include "../platform/fnql_steam.h"

#include <array>
#include <cstring>
#include <string>

namespace {

constexpr unsigned int QL_STEAM_APP_ID = 282440u;
constexpr int QL_STEAM_SERVER_ID_CONFIGSTRING = 0x2ca;
constexpr int QL_STEAM_REFERENCED_CONFIGSTRING = 0x2cb;
constexpr int QL_STEAM_VOICE_CHANNEL = 1;
constexpr int QL_STEAM_KEEPALIVE_CHANNEL = 16;
constexpr int QL_STEAM_MAX_VOICE_PACKET = 0x4000;
constexpr int QL_STEAM_MAX_PACKETS_PER_FRAME = 64;
constexpr int QL_STEAM_KEEPALIVE_MSEC = 10000;
constexpr char QL_STEAM_KEEPALIVE[] = "that's a good-ass dog";

fnqlSteamGameServerConfig_t lastGameServerConfig{};
bool hasLastGameServerConfig = false;
int lastGameServerConfigRefresh = 0;

void ApplyRetailDefaultHostname()
{
	if ( !sv_hostname || Q_stricmp( sv_hostname->string, "noname" ) ) {
		return;
	}
	fnqlSteamStatus_t status{};
	status.size = sizeof( status );
	if ( !FNQL_Steam_GetStatus( &status ) || !status.persona_name[0] ) {
		return;
	}
	char hostname[MAX_CVAR_VALUE_STRING];
	Com_sprintf( hostname, sizeof( hostname ), "%s's Match", status.persona_name );
	Cvar_Set( sv_hostname->name, hostname );
}

fnqlSteamGameServerConfig_t BuildSteamGameServerConfig( const char *mapName )
{
	fnqlSteamGameServerConfig_t config{};
	char version[MAX_CVAR_VALUE_STRING];
	char password[MAX_CVAR_VALUE_STRING];
	char account[MAX_CVAR_VALUE_STRING];
	const int gamePort = Cvar_VariableIntegerValue( "net_port" );
	const bool secure = Cvar_VariableIntegerValue( "sv_steamSecure" ) != 0
		&& ( !sv_vac || sv_vac->integer != 0 );

	config.size = sizeof( config );
	config.game_port = gamePort > 0 && gamePort <= 65535
		? static_cast<uint16_t>( gamePort ) : 0u;
	config.query_port = 0xffffu;
	config.server_mode = secure ? 3u : 1u;
	config.dedicated = com_dedicated->integer ? 1u : 0u;
	config.max_players = sv_maxclients ? static_cast<uint32_t>(
		sv_maxclients->integer ) : 0u;
	Cvar_VariableStringBuffer( "version", version, sizeof( version ) );
	Cvar_VariableStringBuffer( "sv_privatePassword", password, sizeof( password ) );
	Cvar_VariableStringBuffer( "sv_setSteamAccount", account, sizeof( account ) );
	config.password_protected = password[0] ? 1u : 0u;
	Q_strncpyz( config.version, version, sizeof( config.version ) );
	Q_strncpyz( config.product, STEAMPATH_APPID, sizeof( config.product ) );
	Q_strncpyz( config.game_description, "Quake Live", sizeof( config.game_description ) );
	Q_strncpyz( config.mod_directory, BASEGAME, sizeof( config.mod_directory ) );
	Q_strncpyz( config.server_name, sv_hostname ? sv_hostname->string : "FnQL Server",
		sizeof( config.server_name ) );
	Q_strncpyz( config.map_name, mapName ? mapName : "",
		sizeof( config.map_name ) );

	fnql::server::cvars::SteamTagInputs tagInputs;
	tagInputs.gametype = Cvar_VariableIntegerValue( "g_gametype" );
	tagInputs.cheats = Cvar_VariableIntegerValue( "sv_cheats" ) != 0;
	tagInputs.instagib = Cvar_VariableIntegerValue( "g_instagib" ) != 0;
	tagInputs.gravity = Cvar_VariableString( "g_gravity" )[0]
		? Cvar_VariableValue( "g_gravity" ) : 800.0f;
	tagInputs.vampiric = Cvar_VariableValue( "g_vampiricDamage" ) > 0.0f;
	tagInputs.infected = Cvar_VariableIntegerValue( "g_rrInfected" ) != 0;
	tagInputs.quadhog = Cvar_VariableIntegerValue( "g_quadhog" ) != 0;
	tagInputs.fnqlKeywords = Cvar_VariableString( "sv_keywords" );
	tagInputs.retailTags = sv_tags ? sv_tags->string : "";
	const std::string tags = fnql::server::cvars::BuildSteamGameTags(
		tagInputs, sizeof( config.game_tags ) );
	Q_strncpyz( config.game_tags, tags.c_str(), sizeof( config.game_tags ) );
	Q_strncpyz( config.game_server_account, account,
		sizeof( config.game_server_account ) );
	return config;
}

bool SteamGameServerConfigsEqual( const fnqlSteamGameServerConfig_t &left,
	const fnqlSteamGameServerConfig_t &right )
{
	return left.size == right.size
		&& left.bind_ip == right.bind_ip
		&& left.steam_port == right.steam_port
		&& left.game_port == right.game_port
		&& left.query_port == right.query_port
		&& left.server_mode == right.server_mode
		&& left.dedicated == right.dedicated
		&& left.password_protected == right.password_protected
		&& left.max_players == right.max_players
		&& std::strcmp( left.version, right.version ) == 0
		&& std::strcmp( left.product, right.product ) == 0
		&& std::strcmp( left.game_description, right.game_description ) == 0
		&& std::strcmp( left.mod_directory, right.mod_directory ) == 0
		&& std::strcmp( left.server_name, right.server_name ) == 0
		&& std::strcmp( left.map_name, right.map_name ) == 0
		&& std::strcmp( left.game_tags, right.game_tags ) == 0
		&& std::strcmp( left.game_server_account,
			right.game_server_account ) == 0;
}

int SteamAuthenticatedClient( uint64_t steamId ) {
	if ( !steamId || !svs.clients || !sv_maxclients ) return -1;
	for ( int index = 0; index < sv_maxclients->integer; ++index ) {
		const client_t &client = svs.clients[index];
		if ( client.state == CS_ACTIVE && client.platformAuthSession
			&& client.platformAuthTicketSession
			&& client.platformAuthValidated
			&& client.platformSteamIdValue == steamId ) return index;
	}
	return -1;
}

void PublishSteamGameServerIdentity( void ) {
	uint64_t steamId = 0;
	if ( !com_sv_running || !com_sv_running->integer || sv.state == SS_DEAD
		|| FNQL_Steam_GetGameServerSteamId( &steamId ) != FNQL_STEAM_RESULT_OK ) {
		return;
	}
	SV_SetConfigstring( QL_STEAM_SERVER_ID_CONFIGSTRING,
		va( "%llu", static_cast<unsigned long long>( steamId ) ) );
	Com_DPrintf( "Steam GameServer identity published in configstring %d: %llu\n",
		QL_STEAM_SERVER_ID_CONFIGSTRING,
		static_cast<unsigned long long>( steamId ) );
}

void PublishSteamGameServerInfo( void ) {
	if ( !FNQL_Steam_Available( FNQL_STEAM_CAP_GAME_SERVER_METADATA ) ) return;
	const char *cursor = Cvar_InfoString( CVAR_SERVERINFO, nullptr );
	while ( cursor && cursor[0] ) {
		char key[MAX_INFO_KEY];
		char value[MAX_INFO_VALUE];
		cursor = Info_NextPair( cursor, key, value );
		if ( !key[0] ) break;
		(void)FNQL_Steam_SetGameServerKeyValue( key, value );
	}
}

void HandleP2PEvent( const fnqlSteamEvent_t *event ) {
	if ( event->flags != FNQL_STEAM_ROLE_GAME_SERVER ) return;
	if ( event->type == FNQL_STEAM_EVENT_P2P_SESSION_FAILURE ) {
		Com_Printf( S_COLOR_YELLOW
			"Steam GameServer P2P session failed for %llu (error %llu).\n",
			static_cast<unsigned long long>( event->subject_id ),
			static_cast<unsigned long long>( event->value ) );
		return;
	}
	const int clientNum = SteamAuthenticatedClient( event->subject_id );
	if ( clientNum < 0 ) {
		Com_DPrintf( "Ignored Steam GameServer P2P request from unauthenticated peer %llu.\n",
			static_cast<unsigned long long>( event->subject_id ) );
		return;
	}
	const fnqlSteamResult_t result = FNQL_Steam_AcceptP2PSession(
		FNQL_STEAM_ROLE_GAME_SERVER, event->subject_id );
	Com_DPrintf( "Steam GameServer P2P request for client %d (%llu): %s\n",
		clientNum, static_cast<unsigned long long>( event->subject_id ),
		result == FNQL_STEAM_RESULT_OK ? "accepted" : "failed" );
}

void SteamProviderEvent( const fnqlSteamEvent_t *event, void * ) {
	if ( !event || event->size < sizeof( *event ) ) {
		return;
	}
	if ( event->type == FNQL_STEAM_EVENT_P2P_SESSION_REQUEST
		|| event->type == FNQL_STEAM_EVENT_P2P_SESSION_FAILURE ) {
		HandleP2PEvent( event );
	}
	if ( event->type == FNQL_STEAM_EVENT_GAME_SERVER_CONNECTED
		&& event->result == FNQL_STEAM_RESULT_OK ) {
		PublishSteamGameServerIdentity();
		SV_PublishWorkshopReferences();
		PublishSteamGameServerInfo();
	}
	SV_HandleSteamProviderEvent( event->type, event->result, event->subject_id );
}

constexpr std::array<svPlatformServiceStatus_t, SV_PLATFORM_CAPABILITY_COUNT> fallbackServiceStatus = {{
	{ "online", "Build-disabled (FnQL engine-only)",
		"compatibility-disabled (retail Steam install only)", qfalse, QL_STEAM_APP_ID },
	{ "authentication", "Build-disabled (FnQL engine-only)",
		"compatibility-unverified (Steamworks stub; identity is not authenticated)", qfalse, QL_STEAM_APP_ID },
	{ "steam-gameserver", "Build-disabled (FnQL engine-only)",
		"compatibility-disabled (no Steam GameServer owner)", qfalse, QL_STEAM_APP_ID },
	{ "workshop", "Build-disabled (FnQL engine-only)",
		"compatibility-disabled (retail assets only)", qfalse, QL_STEAM_APP_ID },
	{ "stats", "Build-disabled (FnQL engine-only)",
		"compatibility-disabled (no live Steam stats owner)", qfalse, QL_STEAM_APP_ID },
}};

std::array<svPlatformServiceStatus_t, SV_PLATFORM_CAPABILITY_COUNT> serviceStatus = fallbackServiceStatus;

uint64_t RequiredCapability( svPlatformCapability_t capability ) {
	switch ( capability ) {
		case SV_PLATFORM_CAPABILITY_ONLINE:
			return FNQL_STEAM_CAP_CLIENT | FNQL_STEAM_CAP_GAME_SERVER;
		case SV_PLATFORM_CAPABILITY_AUTH:
			return FNQL_STEAM_CAP_AUTH;
		case SV_PLATFORM_CAPABILITY_STEAM_GAME_SERVER:
			return FNQL_STEAM_CAP_GAME_SERVER;
		case SV_PLATFORM_CAPABILITY_WORKSHOP:
			return FNQL_STEAM_CAP_UGC;
		case SV_PLATFORM_CAPABILITY_STATS:
			return FNQL_STEAM_CAP_STATS | FNQL_STEAM_CAP_GAME_SERVER_STATS;
		default:
			return 0;
	}
}

void RefreshStatus( svPlatformCapability_t capability ) {
	svPlatformServiceStatus_t &status = serviceStatus[static_cast<std::size_t>( capability )];
	status = fallbackServiceStatus[static_cast<std::size_t>( capability )];
	const uint64_t required = RequiredCapability( capability );
	const qboolean available = required && ( FNQL_Steam_Capabilities() & required )
		? qtrue : qfalse;
	status.available = available;
	if ( available ) {
		status.provider = "FnQL-Steam external provider";
		status.policy = "explicit external provider (retail Steam runtime)";
	}
}

const svPlatformServiceStatus_t &Status( svPlatformCapability_t capability ) {
	if ( capability < 0 || capability >= SV_PLATFORM_CAPABILITY_COUNT ) {
		return serviceStatus[SV_PLATFORM_CAPABILITY_ONLINE];
	}
	RefreshStatus( capability );
	return serviceStatus[static_cast<std::size_t>( capability )];
}

void SetReadOnlyStatus( const char *name, const char *value, const char *description ) {
	// Status values are dynamic. Register them with a stable empty reset value,
	// then publish the current value without presenting each refresh as a
	// conflicting cvar declaration in developer builds.
	cvar_t *cvar = Cvar_Get( name, "", CVAR_ROM );
	Cvar_Set( name, value );
	Cvar_SetDescription( cvar, description );
}

void PublishSteamAccountState( const fnqlSteamGameServerConfig_t &config )
{
	const bool hasAccount = config.game_server_account[0] != '\0';
	const bool accountSupported = FNQL_Steam_Available(
		FNQL_STEAM_CAP_GAME_SERVER_ACCOUNT );
	SetReadOnlyStatus( "sv_steamAccountState",
		hasAccount ? ( accountSupported ? "requested" : "provider-unsupported" )
			: "anonymous",
		"Steam GameServer account-token state; FnQL never reports an unsupported login as authenticated." );
}

} // namespace

void SV_PublishWorkshopReferences( void ) {
	if ( !com_sv_running || !com_sv_running->integer || sv.state == SS_DEAD ) {
		return;
	}

	const char *references = FS_ReferencedWorkshopItems();
	if ( !references ) {
		references = "";
	}
	Cvar_Set( "sv_referencedSteamworks", references );
	SV_SetConfigstring( QL_STEAM_REFERENCED_CONFIGSTRING, references );
}

void SV_RegisterSteamEventSink( void ) {
	if ( !FNQL_Steam_AddEventSink( SteamProviderEvent, nullptr ) ) {
		Com_Printf( S_COLOR_YELLOW
			"Server could not register for Steam provider events; authenticated hosting is unavailable.\n" );
	}
}

const svPlatformServiceStatus_t *SV_GetPlatformServiceStatus( svPlatformCapability_t capability ) {
	if ( capability < 0 || capability >= SV_PLATFORM_CAPABILITY_COUNT ) {
		return nullptr;
	}
	return &Status( capability );
}

qboolean SV_PlatformServiceAvailable( svPlatformCapability_t capability ) {
	const svPlatformServiceStatus_t *status = SV_GetPlatformServiceStatus( capability );
	return status ? status->available : qfalse;
}

const char *SV_GetPlatformAuthProviderLabel( void ) {
	return Status( SV_PLATFORM_CAPABILITY_AUTH ).provider;
}

const char *SV_GetPlatformAuthPolicyLabel( void ) {
	return Status( SV_PLATFORM_CAPABILITY_AUTH ).policy;
}

const char *SV_GetSteamServerProviderLabel( void ) {
	return Status( SV_PLATFORM_CAPABILITY_STEAM_GAME_SERVER ).provider;
}

const char *SV_GetSteamServerPolicyLabel( void ) {
	return Status( SV_PLATFORM_CAPABILITY_STEAM_GAME_SERVER ).policy;
}

const char *SV_GetWorkshopProviderLabel( void ) {
	return Status( SV_PLATFORM_CAPABILITY_WORKSHOP ).provider;
}

const char *SV_GetWorkshopPolicyLabel( void ) {
	return Status( SV_PLATFORM_CAPABILITY_WORKSHOP ).policy;
}

const char *SV_GetServerStatsProviderLabel( void ) {
	return Status( SV_PLATFORM_CAPABILITY_STATS ).provider;
}

const char *SV_GetServerStatsPolicyLabel( void ) {
	return Status( SV_PLATFORM_CAPABILITY_STATS ).policy;
}

void SV_RefreshPlatformServiceCvars( void ) {
	const svPlatformServiceStatus_t &online = Status( SV_PLATFORM_CAPABILITY_ONLINE );

	SetReadOnlyStatus( "sv_onlineServicesMode", online.provider,
		"Selected Quake Live online-services backend. FnQL ships no proprietary online-service implementation." );
	SetReadOnlyStatus( "sv_onlineServicesPolicy", online.policy,
		"Policy for Quake Live online services; retail asset compatibility does not imply live service ownership." );
	SetReadOnlyStatus( "sv_platformAuthProvider", SV_GetPlatformAuthProviderLabel(),
		"Selected platform-authentication provider." );
	SetReadOnlyStatus( "sv_platformAuthPolicy", SV_GetPlatformAuthPolicyLabel(),
		"Platform-authentication fallback policy." );
	SetReadOnlyStatus( "sv_steamServerProvider", SV_GetSteamServerProviderLabel(),
		"Selected Steam GameServer provider." );
	SetReadOnlyStatus( "sv_steamServerPolicy", SV_GetSteamServerPolicyLabel(),
		"Steam GameServer ownership policy." );
	SetReadOnlyStatus( "sv_workshopProvider", SV_GetWorkshopProviderLabel(),
		"Selected Workshop provider." );
	SetReadOnlyStatus( "sv_workshopPolicy", SV_GetWorkshopPolicyLabel(),
		"Workshop fallback policy; retail assets remain external to FnQL." );
	SetReadOnlyStatus( "sv_statsProvider", SV_GetServerStatsProviderLabel(),
		"Selected server-statistics provider." );
	SetReadOnlyStatus( "sv_statsPolicy", SV_GetServerStatsPolicyLabel(),
		"Server-statistics fallback policy." );
}

void SV_SteamGameServerStart( const char *mapName ) {
	cvar_t *secure = Cvar_Get( "sv_steamSecure", "0", CVAR_ARCHIVE | CVAR_LATCH );

	Cvar_CheckRange( secure, "0", "1", CV_INTEGER );
	Cvar_SetDescription( secure,
		"Request Steam authenticated-and-secure GameServer mode. The default preserves FnQL's unauthenticated compatibility lane." );
	ApplyRetailDefaultHostname();
	const fnqlSteamGameServerConfig_t config = BuildSteamGameServerConfig( mapName );
	const bool firstPublication = !hasLastGameServerConfig;
	const bool hasAccount = config.game_server_account[0] != '\0';
	const bool accountSupported = FNQL_Steam_Available(
		FNQL_STEAM_CAP_GAME_SERVER_ACCOUNT );
	PublishSteamAccountState( config );
	if ( !FNQL_Steam_Available( FNQL_STEAM_CAP_GAME_SERVER )
		|| config.game_port == 0 ) {
		return;
	}

	const fnqlSteamResult_t result = FNQL_Steam_StartGameServer( &config );
	if ( result == FNQL_STEAM_RESULT_OK || result == FNQL_STEAM_RESULT_PENDING ) {
		lastGameServerConfig = config;
		hasLastGameServerConfig = true;
		PublishSteamGameServerInfo();
	}
	SetReadOnlyStatus( "sv_steamServerState",
		( result == FNQL_STEAM_RESULT_OK || result == FNQL_STEAM_RESULT_PENDING )
			? "starting" : "failed",
		"Steam GameServer runtime state for the external provider." );
	if ( result != FNQL_STEAM_RESULT_OK && result != FNQL_STEAM_RESULT_PENDING ) {
		Com_Printf( S_COLOR_YELLOW "Steam GameServer startup failed with result %d; legacy master publication remains active.\n",
			(int)result );
	}
	else if ( firstPublication && hasAccount && !accountSupported ) {
		Com_Printf( S_COLOR_YELLOW
			"sv_setSteamAccount is configured, but the active provider does not own account login; anonymous provider policy remains in effect.\n" );
	}
}

void SV_SteamGameServerStop( void ) {
	FNQL_Steam_StopGameServer();
	lastGameServerConfig = {};
	hasLastGameServerConfig = false;
	lastGameServerConfigRefresh = 0;
	if ( Cvar_VariableString( "sv_steamServerState" )[0] ) {
		Cvar_Set( "sv_steamServerState", "stopped" );
	}
}

void SV_SteamP2PCloseClient( uint64_t steamId ) {
	if ( steamId && FNQL_Steam_Available( FNQL_STEAM_CAP_GAME_SERVER_P2P ) ) {
		(void)FNQL_Steam_CloseP2PSession( FNQL_STEAM_ROLE_GAME_SERVER, steamId );
	}
}

void SV_SteamHandleIncomingPacket( const netadr_t *from, const msg_t *msg ) {
	if ( !from || !msg || !msg->data || msg->cursize <= 0 || from->type != NA_IP
		|| !FNQL_Steam_Available( FNQL_STEAM_CAP_GAME_SERVER_PACKET_IO )
		|| !com_sv_running || !com_sv_running->integer ) return;
	const uint32_t packedIp = ( static_cast<uint32_t>( from->ipv._4[0] ) << 24u )
		| ( static_cast<uint32_t>( from->ipv._4[1] ) << 16u )
		| ( static_cast<uint32_t>( from->ipv._4[2] ) << 8u )
		| static_cast<uint32_t>( from->ipv._4[3] );
	(void)FNQL_Steam_HandleGameServerPacket( msg->data,
		static_cast<uint32_t>( msg->cursize ), packedIp, from->port );
}

static void SV_SteamDrainGameServerPackets( void ) {
	if ( !FNQL_Steam_Available( FNQL_STEAM_CAP_GAME_SERVER_PACKET_IO )
		|| !com_sv_running || !com_sv_running->integer ) return;
	for ( int packetIndex = 0; packetIndex < QL_STEAM_MAX_PACKETS_PER_FRAME;
		++packetIndex ) {
		byte packet[1024];
		uint32_t packetSize = 0;
		uint32_t packedIp = 0;
		uint16_t port = 0;
		if ( FNQL_Steam_GetGameServerPacket( packet, sizeof( packet ), &packetSize,
			&packedIp, &port ) != FNQL_STEAM_RESULT_OK || packetSize == 0 ) break;
		if ( packetSize > sizeof( packet ) || !packedIp || !port ) {
			Com_Printf( S_COLOR_YELLOW
				"Rejected invalid Steam GameServer UDP packet (%u bytes).\n",
				packetSize );
			break;
		}
		netadr_t destination = {};
		destination.type = NA_IP;
		destination.ipv._4[0] = static_cast<byte>( packedIp >> 24u );
		destination.ipv._4[1] = static_cast<byte>( packedIp >> 16u );
		destination.ipv._4[2] = static_cast<byte>( packedIp >> 8u );
		destination.ipv._4[3] = static_cast<byte>( packedIp );
		destination.port = port;
		NET_SendPacket( NS_SERVER, static_cast<int>( packetSize ), packet,
			&destination );
	}
}

static void SV_SteamRefreshGameServerConfig( void ) {
	if ( !FNQL_Steam_Available( FNQL_STEAM_CAP_GAME_SERVER )
		|| !com_sv_running || !com_sv_running->integer
		|| svs.time - lastGameServerConfigRefresh < 1000 ) return;
	lastGameServerConfigRefresh = svs.time;
	ApplyRetailDefaultHostname();
	const fnqlSteamGameServerConfig_t config = BuildSteamGameServerConfig(
		Cvar_VariableString( "mapname" ) );
	PublishSteamAccountState( config );
	if ( hasLastGameServerConfig
		&& SteamGameServerConfigsEqual( config, lastGameServerConfig ) ) {
		return;
	}
	const fnqlSteamResult_t result = FNQL_Steam_UpdateGameServer( &config );
	if ( result == FNQL_STEAM_RESULT_OK || result == FNQL_STEAM_RESULT_PENDING ) {
		lastGameServerConfig = config;
		hasLastGameServerConfig = true;
	} else {
		Com_DPrintf( "Steam GameServer metadata update failed with result %d.\n",
			static_cast<int>( result ) );
	}
}

static void SV_SteamPublishGameServerMetadata( void ) {
	static int lastPublished;
	uint32_t botCount = 0;
	if ( !FNQL_Steam_Available( FNQL_STEAM_CAP_GAME_SERVER_METADATA )
		|| !com_sv_running || !com_sv_running->integer || !svs.clients
		|| !sv_maxclients || svs.time - lastPublished < 1000 ) return;
	lastPublished = svs.time;
	(void)FNQL_Steam_SetGameServerKeyValue( "g_redScore",
		Cvar_VariableString( "g_redScore" ) );
	(void)FNQL_Steam_SetGameServerKeyValue( "g_blueScore",
		Cvar_VariableString( "g_blueScore" ) );
	for ( int index = 0; index < sv_maxclients->integer; ++index ) {
		client_t &client = svs.clients[index];
		if ( client.state != CS_ACTIVE ) continue;
		if ( client.netchan.remoteAddress.type == NA_BOT ) ++botCount;
		if ( client.netchan.remoteAddress.type == NA_BOT
			&& !client.platformSteamIdValue ) {
			uint64_t botId = 0;
			if ( FNQL_Steam_CreateUnauthenticatedUser( &botId )
				== FNQL_STEAM_RESULT_OK ) {
				client.platformSteamIdValue = botId;
				Com_sprintf( client.platformSteamId,
					sizeof( client.platformSteamId ), "%llu",
					static_cast<unsigned long long>( botId ) );
			}
		}
		if ( !client.platformSteamIdValue ) continue;
		const playerState_t *playerState = SV_GameClientNum( index );
		if ( !playerState ) continue;
		(void)FNQL_Steam_UpdateGameServerUser( client.platformSteamIdValue,
			client.name, static_cast<uint32_t>( playerState->persistant[PERS_SCORE] ) );
	}
	(void)FNQL_Steam_SetGameServerBotCount( botCount );
}

void SV_SteamP2PFrame( void ) {
	static int lastKeepalive;
	SV_SteamRefreshGameServerConfig();
	SV_SteamDrainGameServerPackets();
	SV_SteamPublishGameServerMetadata();
	if ( !FNQL_Steam_Available( FNQL_STEAM_CAP_GAME_SERVER_P2P )
		|| !com_sv_running || !com_sv_running->integer || !svs.clients
		|| !sv_maxclients ) return;

	if ( svs.time - lastKeepalive >= QL_STEAM_KEEPALIVE_MSEC ) {
		lastKeepalive = svs.time;
		for ( int index = 0; index < sv_maxclients->integer; ++index ) {
			const client_t &client = svs.clients[index];
			if ( client.state != CS_ACTIVE || !client.platformAuthSession
				|| !client.platformAuthTicketSession
				|| !client.platformAuthValidated || !client.platformSteamIdValue ) continue;
			(void)FNQL_Steam_SendP2PPacket( FNQL_STEAM_ROLE_GAME_SERVER,
				client.platformSteamIdValue, QL_STEAM_KEEPALIVE,
				static_cast<uint32_t>( sizeof( QL_STEAM_KEEPALIVE ) - 1 ), 2u,
				QL_STEAM_KEEPALIVE_CHANNEL );
		}
	}

	for ( int packetIndex = 0; packetIndex < QL_STEAM_MAX_PACKETS_PER_FRAME;
		++packetIndex ) {
		uint32_t packetSize = 0;
		if ( FNQL_Steam_PeekP2PPacket( FNQL_STEAM_ROLE_GAME_SERVER,
			QL_STEAM_VOICE_CHANNEL, &packetSize ) != FNQL_STEAM_RESULT_OK ) break;
		if ( packetSize == 0 || packetSize > QL_STEAM_MAX_VOICE_PACKET ) {
			Com_Printf( S_COLOR_YELLOW
				"Rejected oversized Steam voice packet (%u bytes).\n", packetSize );
			break;
		}
		byte *packet = static_cast<byte *>( Z_Malloc( packetSize + 1u ) );
		if ( !packet ) break;
		uint32_t bytesRead = 0;
		uint64_t remoteId = 0;
		const fnqlSteamResult_t readResult = FNQL_Steam_ReadP2PPacket(
			FNQL_STEAM_ROLE_GAME_SERVER, QL_STEAM_VOICE_CHANNEL, packet + 1,
			packetSize, &bytesRead, &remoteId );
		if ( readResult != FNQL_STEAM_RESULT_OK || bytesRead == 0
			|| bytesRead > packetSize ) {
			Z_Free( packet );
			continue;
		}
		const int sender = SteamAuthenticatedClient( remoteId );
		if ( sender < 0 || sender > 255 ) {
			SV_SteamP2PCloseClient( remoteId );
			Z_Free( packet );
			continue;
		}
		packet[0] = static_cast<byte>( sender );
		for ( int recipient = 0; recipient < sv_maxclients->integer; ++recipient ) {
			const client_t &client = svs.clients[recipient];
			if ( client.state != CS_ACTIVE || !client.platformAuthSession
				|| !client.platformAuthTicketSession
				|| !client.platformAuthValidated || !client.platformSteamIdValue
				|| SV_GameShouldSuppressVoiceToClient( sender, recipient ) ) continue;
			(void)FNQL_Steam_SendP2PPacket( FNQL_STEAM_ROLE_GAME_SERVER,
				client.platformSteamIdValue, packet, bytesRead + 1u, 1u,
				QL_STEAM_VOICE_CHANNEL );
		}
		Z_Free( packet );
	}
}
