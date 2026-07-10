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

#include <array>

namespace {

constexpr unsigned int QL_STEAM_APP_ID = 282440u;

constexpr std::array<svPlatformServiceStatus_t, SV_PLATFORM_CAPABILITY_COUNT> serviceStatus = {{
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

const svPlatformServiceStatus_t &Status( svPlatformCapability_t capability ) {
	if ( capability < 0 || capability >= SV_PLATFORM_CAPABILITY_COUNT ) {
		return serviceStatus[SV_PLATFORM_CAPABILITY_ONLINE];
	}
	return serviceStatus[static_cast<std::size_t>( capability )];
}

void SetReadOnlyStatus( const char *name, const char *value, const char *description ) {
	cvar_t *cvar = Cvar_Get( name, value, CVAR_ROM );
	Cvar_Set( name, value );
	Cvar_SetDescription( cvar, description );
}

} // namespace

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
