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

#include "fnql_steam.h"

#include "../qcommon/qcommon.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <array>

#if defined(_WIN32)
# include <windows.h>
#else
# include <dlfcn.h>
# include <limits.h>
# include <unistd.h>
#endif

namespace {

constexpr uint32_t kQuakeLiveAppId = 282440u;

#if defined(_WIN32)
constexpr const char *kDefaultProviderName = "fnql_steam.dll";
constexpr const char *kSteamApiName = "steam_api.dll";
using LibraryHandle = HMODULE;
#elif defined(__APPLE__)
constexpr const char *kDefaultProviderName = "libfnql_steam.dylib";
constexpr const char *kSteamApiName = "libsteam_api.dylib";
using LibraryHandle = void *;
#else
constexpr const char *kDefaultProviderName = "libfnql_steam.so";
constexpr const char *kSteamApiName = "libsteam_api.so";
using LibraryHandle = void *;
#endif

struct SteamState {
	struct EventObserver {
		fnqlSteamEngineEventFn sink{};
		void *context{};
	};

	static constexpr std::size_t MaxEventObservers = 4;
	LibraryHandle library{};
	const fnqlSteamProvider_t *provider{};
	uint32_t roles{};
	uint64_t capabilities{};
	uint32_t nextCapabilityRefresh{};
	bool initialized{};
	bool commandRegistered{};
	bool fallbackAnnounced{};
	fnqlSteamEngineEventFn eventSink{};
	void *eventContext{};
	std::array<EventObserver, MaxEventObservers> eventObservers{};
	char detail[FNQL_STEAM_TEXT_CAPACITY]{};
	char providerPath[FNQL_STEAM_PATH_CAPACITY]{};
};

SteamState state;

bool IsAbsolutePath(const char *path) {
	if (!path || !path[0]) {
		return false;
	}
#if defined(_WIN32)
	return (path[0] && path[1] == ':')
		|| ((path[0] == '\\' || path[0] == '/')
			&& (path[1] == '\\' || path[1] == '/'));
#else
	return path[0] == '/';
#endif
}

bool IsBareFileName(const char *path) {
	return path && path[0] && !strchr(path, '/') && !strchr(path, '\\')
		&& !strstr(path, "..");
}

void CopyText(char *target, size_t capacity, const char *source) {
	if (!target || capacity == 0) {
		return;
	}
	Q_strncpyz(target, source ? source : "", static_cast<int>(capacity));
}

void SetDetail(const char *detail) {
	CopyText(state.detail, sizeof(state.detail), detail);
	Cvar_Set("com_steamProviderDetail", state.detail);
}

void SetStatusCvars(const char *status, const char *name, const char *version) {
	char capabilities[32];
	Com_sprintf(capabilities, sizeof(capabilities), "0x%016llx",
		static_cast<unsigned long long>(state.capabilities));
	Cvar_Set("com_steamProviderStatus", status ? status : "unavailable");
	Cvar_Set("com_steamProviderName", name ? name : "none");
	Cvar_Set("com_steamProviderVersion", version ? version : "none");
	Cvar_Set("com_steamProviderCapabilities", capabilities);
}

void EnterNonSteamFallback(const char *status, const char *name,
	const char *version, const char *detail) {
	state.initialized = false;
	state.capabilities = 0;
	SetDetail(detail);
	SetStatusCvars(status, name, version);
	if (!state.fallbackAnnounced) {
		Com_Printf("Steam integration unavailable: %s\n",
			state.detail[0] ? state.detail : "no compatible provider is active.");
		Com_Printf("FnQL is continuing in non-Steam mode; retail assets, local play, "
			"legacy server discovery, and non-Steam hosting remain available.\n");
		state.fallbackAnnounced = true;
	}
}

void FNQL_STEAM_CALL HostLog(void *, uint32_t level, const char *message) {
	if (!message || !message[0]) {
		return;
	}
	if (level >= FNQL_STEAM_LOG_ERROR) {
		Com_Printf(S_COLOR_RED "Steam provider error: %s\n", message);
	} else if (level == FNQL_STEAM_LOG_WARNING) {
		Com_Printf(S_COLOR_YELLOW "Steam provider warning: %s\n", message);
	} else if (level == FNQL_STEAM_LOG_INFO) {
		Com_Printf("Steam provider: %s\n", message);
	} else {
		Com_DPrintf("Steam provider: %s\n", message);
	}
}

void FNQL_STEAM_CALL HostEvent(void *, const fnqlSteamEvent_t *event) {
	if (!event || event->size < sizeof(fnqlSteamEvent_t)
		|| event->type == FNQL_STEAM_EVENT_NONE) {
		Com_DPrintf("Steam provider discarded an invalid event.\n");
		return;
	}
	if (event->type == FNQL_STEAM_EVENT_GAME_SERVER_CONNECTED) {
		Cvar_Set("sv_steamServerState", "logged-on");
		state.nextCapabilityRefresh = 0;
	} else if (event->type == FNQL_STEAM_EVENT_GAME_SERVER_DISCONNECTED) {
		Cvar_Set("sv_steamServerState", "disconnected");
		state.nextCapabilityRefresh = 0;
	}
	if (state.eventSink) {
		state.eventSink(event, state.eventContext);
	}
	// Copy the fixed observer set before dispatch so a callback may safely
	// remove itself without invalidating this delivery pass.
	const auto observers = state.eventObservers;
	for (const SteamState::EventObserver &observer : observers) {
		if (observer.sink) {
			observer.sink(event, observer.context);
		}
	}
}

uint64_t FNQL_STEAM_CALL HostMilliseconds(void *) {
	return static_cast<uint64_t>(Com_Milliseconds());
}

void BuildCandidatePath(const char *configured, char *target, size_t capacity) {
	const char *name = configured && configured[0] ? configured : kDefaultProviderName;
	if (IsAbsolutePath(name)) {
		CopyText(target, capacity, name);
		return;
	}
	if (!IsBareFileName(name)) {
		target[0] = '\0';
		return;
	}
	Com_sprintf(target, static_cast<int>(capacity), "%s%c%s",
		Sys_DefaultBasePath(), PATH_SEP, name);
}

void BuildSteamApiPath(const char *configured, char *target, size_t capacity) {
	if (configured && configured[0]) {
		if (IsAbsolutePath(configured)) {
			CopyText(target, capacity, configured);
		} else {
			target[0] = '\0';
		}
		return;
	}
	const char *retailPath = Sys_SteamPath();
	if (!retailPath || !retailPath[0]) {
		target[0] = '\0';
		return;
	}
	Com_sprintf(target, static_cast<int>(capacity), "%s%c%s",
		retailPath, PATH_SEP, kSteamApiName);
}

#if defined(_WIN32)
bool Utf8ToWide(const char *source, wchar_t *target, size_t capacity) {
	if (!source || !target || capacity == 0) {
		return false;
	}
	const int converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		source, -1, target, static_cast<int>(capacity));
	if (converted <= 0) {
		target[0] = L'\0';
		return false;
	}
	return true;
}
#endif

LibraryHandle OpenLibrary(const char *path) {
#if defined(_WIN32)
	wchar_t widePath[FNQL_STEAM_PATH_CAPACITY];
	if (!Utf8ToWide(path, widePath, sizeof(widePath) / sizeof(widePath[0]))) {
		return nullptr;
	}
	return LoadLibraryExW(widePath, nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
	return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

void *LoadSymbol(LibraryHandle library, const char *name) {
#if defined(_WIN32)
	return reinterpret_cast<void *>(GetProcAddress(library, name));
#else
	return dlsym(library, name);
#endif
}

void CloseLibrary(LibraryHandle library) {
	if (!library) {
		return;
	}
#if defined(_WIN32)
	FreeLibrary(library);
#else
	dlclose(library);
#endif
}

bool HasProviderField(size_t offset, size_t fieldSize) {
	return state.provider && state.provider->size >= offset + fieldSize;
}

#define FNQL_HAS_PROVIDER_FIELD(field) \
	HasProviderField(offsetof(fnqlSteamProvider_t, field), sizeof(state.provider->field))

uint64_t ValidatedCapabilities(const fnqlSteamProvider_t *provider,
	uint64_t reportedCapabilities) {
	uint64_t capabilities = reportedCapabilities;
	if (!provider->is_subscribed_app || !provider->get_status) {
		capabilities &= ~(FNQL_STEAM_CAP_CLIENT | FNQL_STEAM_CAP_IDENTITY);
	}
	if (!provider->open_overlay_url || !provider->open_overlay_user) {
		capabilities &= ~FNQL_STEAM_CAP_OVERLAY;
	}
	if (!provider->set_rich_presence) {
		capabilities &= ~FNQL_STEAM_CAP_RICH_PRESENCE;
	}
	if (!provider->request_servers || !provider->refresh_servers
		|| !provider->cancel_servers) {
		capabilities &= ~FNQL_STEAM_CAP_SERVER_BROWSER;
	}
	if (!provider->create_lobby || !provider->join_lobby || !provider->leave_lobby
		|| !provider->set_lobby_server || !provider->invite_to_lobby
		|| !provider->send_lobby_chat) {
		capabilities &= ~FNQL_STEAM_CAP_LOBBIES;
	}
	if (!provider->get_subscribed_items || !provider->get_item_install_info
		|| !provider->get_item_download_info || !provider->download_item
		|| !provider->subscribe_item || !provider->unsubscribe_item
		|| !FNQL_HAS_PROVIDER_FIELD(get_item_state)
		|| !provider->get_item_state) {
		capabilities &= ~FNQL_STEAM_CAP_UGC;
	}
	if (!provider->request_user_stats) {
		capabilities &= ~FNQL_STEAM_CAP_STATS;
	}
	if (!provider->get_auth_ticket || !provider->cancel_auth_ticket
		|| !provider->begin_auth_session || !provider->end_auth_session) {
		capabilities &= ~FNQL_STEAM_CAP_AUTH;
	}
	if (!provider->start_game_server || !provider->stop_game_server
		|| !provider->update_game_server) {
		capabilities &= ~(FNQL_STEAM_CAP_GAME_SERVER
			| FNQL_STEAM_CAP_GAME_SERVER_STATS);
	}
	if (!FNQL_HAS_PROVIDER_FIELD(store_user_stats)
		|| !provider->get_user_stat_i32 || !provider->set_user_stat_i32
		|| !provider->get_user_achievement || !provider->set_user_achievement
		|| !provider->store_user_stats || !provider->request_user_stats) {
		capabilities &= ~FNQL_STEAM_CAP_GAME_SERVER_STATS;
	}
	if (!FNQL_HAS_PROVIDER_FIELD(serialize_retail_json)
		|| !provider->serialize_retail_json) {
		capabilities &= ~FNQL_STEAM_CAP_RETAIL_JSON;
	}
	if (!FNQL_HAS_PROVIDER_FIELD(get_avatar_rgba)
		|| !provider->get_avatar_rgba) {
		capabilities &= ~FNQL_STEAM_CAP_AVATARS;
	}
	if (!FNQL_HAS_PROVIDER_FIELD(get_friend) || !provider->get_friends
		|| !provider->get_friend) {
		capabilities &= ~FNQL_STEAM_CAP_FRIENDS;
	}
	if (!FNQL_HAS_PROVIDER_FIELD(get_ugc_query_results)
		|| !provider->request_ugc_query || !provider->get_ugc_query_results) {
		capabilities &= ~FNQL_STEAM_CAP_UGC_QUERY;
	}
	if (!FNQL_HAS_PROVIDER_FIELD(close_p2p_session)
		|| !provider->send_p2p_packet || !provider->peek_p2p_packet
		|| !provider->read_p2p_packet || !provider->accept_p2p_session
		|| !provider->close_p2p_session) {
		capabilities &= ~(FNQL_STEAM_CAP_CLIENT_P2P
			| FNQL_STEAM_CAP_GAME_SERVER_P2P);
	}
	if (!FNQL_HAS_PROVIDER_FIELD(get_game_server_steam_id)
		|| !provider->get_game_server_steam_id) {
		capabilities &= ~FNQL_STEAM_CAP_GAME_SERVER_P2P;
	}
	if (!FNQL_HAS_PROVIDER_FIELD(set_local_voice_speaking)
		|| !provider->start_voice_recording || !provider->stop_voice_recording
		|| !provider->get_compressed_voice || !provider->decompress_voice
		|| !provider->get_voice_sample_rate
		|| !provider->set_local_voice_speaking) {
		capabilities &= ~FNQL_STEAM_CAP_VOICE;
	}
	if (!FNQL_HAS_PROVIDER_FIELD(reset_client_stats)
		|| !provider->request_client_user_stats
		|| !provider->get_client_user_stat_i32
		|| !provider->get_client_user_stat_f32
		|| !provider->get_client_user_achievement
		|| !provider->get_achievement_display_attribute
		|| !provider->set_client_achievement
		|| !provider->store_client_stats
		|| !provider->reset_client_stats) {
		capabilities &= ~FNQL_STEAM_CAP_CLIENT_STATS;
	}
	if (!FNQL_HAS_PROVIDER_FIELD(invite_to_game)
		|| !provider->get_lobby_members || !provider->get_lobby_data
		|| !provider->set_lobby_data || !provider->invite_to_game) {
		capabilities &= ~FNQL_STEAM_CAP_LOBBY_SNAPSHOT;
	}
	if (!FNQL_HAS_PROVIDER_FIELD(get_game_server_packet)
		|| !provider->handle_game_server_packet
		|| !provider->get_game_server_packet) {
		capabilities &= ~FNQL_STEAM_CAP_GAME_SERVER_PACKET_IO;
	}
	if (!FNQL_HAS_PROVIDER_FIELD(set_game_server_bot_count)
		|| !provider->set_game_server_key_value
		|| !provider->update_game_server_user
		|| !provider->get_game_server_public_ip
		|| !provider->create_unauthenticated_user
		|| !provider->set_game_server_bot_count) {
		capabilities &= ~FNQL_STEAM_CAP_GAME_SERVER_METADATA;
	}
	if (!FNQL_HAS_PROVIDER_FIELD(set_favorite_server)
		|| !provider->set_favorite_server) {
		capabilities &= ~FNQL_STEAM_CAP_FAVORITES;
	}
	if (!FNQL_HAS_PROVIDER_FIELD(cancel_server_details)
		|| !provider->request_server_details
		|| !provider->cancel_server_details) {
		capabilities &= ~FNQL_STEAM_CAP_SERVER_DETAILS;
	}
	return capabilities;
}

void RefreshProviderCapabilities(const char *reason) {
	if (!state.initialized || !state.provider) {
		return;
	}

	uint64_t reportedCapabilities = state.provider->info.capabilities;
	if (state.provider->get_status) {
		fnqlSteamStatus_t status{};
		status.size = sizeof(status);
		if (state.provider->get_status(&status) != FNQL_STEAM_RESULT_OK) {
			/* A transient status failure is not evidence that an already
			 * validated runtime capability disappeared. */
			return;
		}
		reportedCapabilities = status.capabilities;
	}
	const uint64_t capabilities = ValidatedCapabilities(
		state.provider, reportedCapabilities);
	if (capabilities == state.capabilities) {
		return;
	}

	state.capabilities = capabilities;
	SetStatusCvars("active", state.provider->info.name,
		state.provider->info.version);
	SV_RefreshPlatformServiceCvars();
	Com_DPrintf("Steam provider capabilities changed after %s: 0x%016llx.\n",
		reason ? reason : "a runtime transition",
		static_cast<unsigned long long>(state.capabilities));
}

void SteamStatusCommand() {
	fnqlSteamStatus_t status{};
	status.size = sizeof(status);
	Com_Printf("Steam provider: %s\n", state.initialized ? "active" : "inactive");
	Com_Printf("  library: %s\n", state.providerPath[0] ? state.providerPath : "not loaded");
	Com_Printf("  detail: %s\n", state.detail[0] ? state.detail : "none");
	Com_Printf("  capabilities: 0x%016llx\n",
		static_cast<unsigned long long>(state.capabilities));
	if (FNQL_Steam_GetStatus(&status)) {
		Com_Printf("  roles: 0x%x, client logged on: %s, game server logged on: %s\n",
			status.initialized_roles, status.client_logged_on ? "yes" : "no",
			status.game_server_logged_on ? "yes" : "no");
		Com_Printf("  identity: %llu (%s), country: %s\n",
			static_cast<unsigned long long>(status.local_steam_id),
			status.persona_name[0] ? status.persona_name : "unavailable",
			status.country[0] ? status.country : "unavailable");
	}
}

fnqlSteamResult_t UnavailableResult() {
	return state.initialized ? FNQL_STEAM_RESULT_UNSUPPORTED
		: FNQL_STEAM_RESULT_UNAVAILABLE;
}

} // namespace

extern "C" {

void FNQL_Steam_Init(uint32_t roles) {
	cvar_t *enabled = Cvar_Get("com_steamIntegration", "1", CVAR_ARCHIVE | CVAR_INIT);
	cvar_t *providerName = Cvar_Get("com_steamProvider", kDefaultProviderName,
		CVAR_ARCHIVE | CVAR_INIT | CVAR_PROTECTED);
	cvar_t *steamApiName = Cvar_Get("com_steamApi", "",
		CVAR_INIT | CVAR_PROTECTED);
	Cvar_Get("com_steamProviderStatus", "disabled", CVAR_ROM);
	Cvar_Get("com_steamProviderName", "none", CVAR_ROM);
	Cvar_Get("com_steamProviderVersion", "none", CVAR_ROM);
	Cvar_Get("com_steamProviderCapabilities", "0x0000000000000000", CVAR_ROM);
	Cvar_Get("com_steamProviderDetail", "Steam integration is disabled by policy.", CVAR_ROM);

	if (!state.commandRegistered) {
		Cmd_AddCommand("steam_status", SteamStatusCommand);
		state.commandRegistered = true;
	}
	if (state.initialized) {
		return;
	}
	state.roles = roles;
	state.capabilities = 0;

	if (!enabled->integer) {
		SetDetail("Steam integration is disabled by +set com_steamIntegration 0.");
		SetStatusCvars("disabled", "none", "none");
		return;
	}

	BuildCandidatePath(providerName->string, state.providerPath,
		sizeof(state.providerPath));
	if (!state.providerPath[0]) {
		EnterNonSteamFallback("rejected", "none", "none",
			"com_steamProvider must be an absolute path or a bare library name.");
		return;
	}
	state.library = OpenLibrary(state.providerPath);
	if (!state.library) {
		EnterNonSteamFallback("unavailable", "none", "none",
			va("Steam provider library was not found or could not be loaded: %s",
				state.providerPath));
		return;
	}

	auto getProvider = reinterpret_cast<fnqlSteamGetProviderFn>(
		LoadSymbol(state.library, FNQL_STEAM_GET_PROVIDER_SYMBOL));
	if (!getProvider) {
		CloseLibrary(state.library);
		state.library = nullptr;
		EnterNonSteamFallback("invalid", "none", "none",
			"Steam provider does not export the required versioned entry point.");
		return;
	}
	state.provider = getProvider(FNQL_STEAM_ABI_VERSION);
	const size_t requiredProviderSize = offsetof(fnqlSteamProvider_t, update_game_server)
		+ sizeof(state.provider->update_game_server);
	if (!state.provider || state.provider->abi_version != FNQL_STEAM_ABI_VERSION
		|| state.provider->size < requiredProviderSize
		|| state.provider->info.size < sizeof(fnqlSteamProviderInfo_t)
		|| state.provider->info.abi_version != FNQL_STEAM_ABI_VERSION
		|| !state.provider->startup || !state.provider->shutdown
		|| !state.provider->run_callbacks || !state.provider->get_status) {
		CloseLibrary(state.library);
		state.library = nullptr;
		state.provider = nullptr;
		EnterNonSteamFallback("incompatible", "none", "none",
			"Steam provider ABI is incompatible or incomplete.");
		return;
	}

	fnqlSteamHost_t host{};
	host.size = sizeof(host);
	host.abi_version = FNQL_STEAM_ABI_VERSION;
	host.log = HostLog;
	host.emit_event = HostEvent;
	host.milliseconds = HostMilliseconds;

	fnqlSteamStartup_t startup{};
	startup.size = sizeof(startup);
	startup.abi_version = FNQL_STEAM_ABI_VERSION;
	startup.app_id = kQuakeLiveAppId;
	startup.roles = roles;
	CopyText(startup.retail_install_path, sizeof(startup.retail_install_path),
		Sys_SteamPath());
	BuildSteamApiPath(steamApiName->string, startup.steam_api_path,
		sizeof(startup.steam_api_path));
	if (!startup.steam_api_path[0]) {
		char providerDisplayName[FNQL_STEAM_NAME_CAPACITY];
		char providerVersion[FNQL_STEAM_NAME_CAPACITY];
		CopyText(providerDisplayName, sizeof(providerDisplayName), state.provider->info.name);
		CopyText(providerVersion, sizeof(providerVersion), state.provider->info.version);
		CloseLibrary(state.library);
		state.library = nullptr;
		state.provider = nullptr;
		EnterNonSteamFallback("rejected", providerDisplayName, providerVersion,
			"Steam API path is unavailable or com_steamApi is not absolute.");
		return;
	}
	CopyText(startup.product_name, sizeof(startup.product_name), "FnQL");
	CopyText(startup.product_version, sizeof(startup.product_version), Q3_VERSION);
	CopyText(startup.game_directory, sizeof(startup.game_directory), BASEGAME);

	const fnqlSteamResult_t result = state.provider->startup(&host, &startup);
	if (result != FNQL_STEAM_RESULT_OK) {
		char providerDisplayName[FNQL_STEAM_NAME_CAPACITY];
		char providerVersion[FNQL_STEAM_NAME_CAPACITY];
		char failureDetail[FNQL_STEAM_TEXT_CAPACITY];
		CopyText(providerDisplayName, sizeof(providerDisplayName), state.provider->info.name);
		CopyText(providerVersion, sizeof(providerVersion), state.provider->info.version);
		if (result == FNQL_STEAM_RESULT_UNAVAILABLE) {
			CopyText(failureDetail, sizeof(failureDetail),
				"Steam provider is unavailable for the current runtime session.");
		} else {
			Com_sprintf(failureDetail, sizeof(failureDetail),
				"Steam provider startup failed with result %d.", result);
		}
		state.provider->shutdown();
		CloseLibrary(state.library);
		state.library = nullptr;
		state.provider = nullptr;
		EnterNonSteamFallback(result == FNQL_STEAM_RESULT_UNAVAILABLE
			? "unavailable" : "startup-failed", providerDisplayName, providerVersion,
			failureDetail);
		return;
	}

	state.capabilities = ValidatedCapabilities(state.provider,
		state.provider->info.capabilities);
	if (!state.capabilities) {
		char providerDisplayName[FNQL_STEAM_NAME_CAPACITY];
		char providerVersion[FNQL_STEAM_NAME_CAPACITY];
		CopyText(providerDisplayName, sizeof(providerDisplayName), state.provider->info.name);
		CopyText(providerVersion, sizeof(providerVersion), state.provider->info.version);
		state.provider->shutdown();
		CloseLibrary(state.library);
		state.library = nullptr;
		state.provider = nullptr;
		EnterNonSteamFallback("no-capabilities", providerDisplayName, providerVersion,
			"Steam provider exposed no usable capabilities.");
		return;
	}
	state.initialized = true;
	SetDetail("Steam provider started successfully.");
	SetStatusCvars("active", state.provider->info.name, state.provider->info.version);
	Com_Printf("Steam provider %s %s active (capabilities 0x%016llx).\n",
		state.provider->info.name, state.provider->info.version,
		static_cast<unsigned long long>(state.capabilities));
}

void FNQL_Steam_Reconfigure(uint32_t roles) {
	if (state.roles == roles && state.initialized) {
		return;
	}
	FNQL_Steam_Shutdown();
	FNQL_Steam_Init(roles);
}

void FNQL_Steam_Shutdown(void) {
	if (state.provider && state.initialized) {
		state.provider->shutdown();
	}
	state.initialized = false;
	state.capabilities = 0;
	state.provider = nullptr;
	if (state.library) {
		CloseLibrary(state.library);
	}
	state.library = nullptr;
	state.roles = 0;
	state.nextCapabilityRefresh = 0;
	state.providerPath[0] = '\0';
	SetDetail("Steam provider is stopped.");
	SetStatusCvars("stopped", "none", "none");
	if (state.commandRegistered) {
		Cmd_RemoveCommand("steam_status");
		state.commandRegistered = false;
	}
}

void FNQL_Steam_Pump(void) {
	if (!state.initialized || !state.provider) {
		return;
	}
	if (state.provider->run_callbacks) {
		state.provider->run_callbacks();
	}
	/* A GameServer-owned interface can become available on a callback-driven
	 * transition. Refresh before common Workshop polling consumes this frame,
	 * while keeping routine status probes bounded to once per second. */
	const uint32_t now = static_cast<uint32_t>(Com_Milliseconds());
	if (!state.nextCapabilityRefresh
		|| static_cast<int32_t>(now - state.nextCapabilityRefresh) >= 0) {
		RefreshProviderCapabilities("the callback pump");
		state.nextCapabilityRefresh = now + 1000u;
	}
}

void FNQL_Steam_SetEventSink(fnqlSteamEngineEventFn sink, void *context) {
	state.eventSink = sink;
	state.eventContext = context;
}

qboolean FNQL_Steam_AddEventSink(fnqlSteamEngineEventFn sink, void *context) {
	if (!sink) {
		return qfalse;
	}
	for (const SteamState::EventObserver &observer : state.eventObservers) {
		if (observer.sink == sink && observer.context == context) {
			return qtrue;
		}
	}
	for (SteamState::EventObserver &observer : state.eventObservers) {
		if (!observer.sink) {
			observer.sink = sink;
			observer.context = context;
			return qtrue;
		}
	}
	Com_Printf(S_COLOR_YELLOW "Steam provider event observer capacity exhausted.\n");
	return qfalse;
}

void FNQL_Steam_RemoveEventSink(fnqlSteamEngineEventFn sink, void *context) {
	if (!sink) {
		return;
	}
	for (SteamState::EventObserver &observer : state.eventObservers) {
		if (observer.sink == sink && observer.context == context) {
			observer = {};
		}
	}
}

qboolean FNQL_Steam_Available(uint64_t capability) {
	return state.initialized && (state.capabilities & capability) == capability
		? qtrue : qfalse;
}

uint64_t FNQL_Steam_Capabilities(void) {
	return state.capabilities;
}

qboolean FNQL_Steam_GetStatus(fnqlSteamStatus_t *status) {
	if (!status || status->size < sizeof(fnqlSteamStatus_t)) {
		return qfalse;
	}
	if (!state.initialized || !state.provider || !state.provider->get_status) {
		memset(status, 0, sizeof(*status));
		status->size = sizeof(*status);
		CopyText(status->detail, sizeof(status->detail), state.detail);
		return qfalse;
	}
	return state.provider->get_status(status) == FNQL_STEAM_RESULT_OK
		? qtrue : qfalse;
}

const char *FNQL_Steam_StatusDetail(void) {
	return state.detail;
}

qboolean FNQL_Steam_IsSubscribedApp(uint32_t appId) {
	return FNQL_Steam_Available(FNQL_STEAM_CAP_CLIENT) && state.provider->is_subscribed_app
		&& state.provider->is_subscribed_app(appId) ? qtrue : qfalse;
}

#define FNQL_STEAM_CALL_RESULT(capability, field, arguments) \
	do { \
		if (!FNQL_Steam_Available(capability) || !FNQL_HAS_PROVIDER_FIELD(field) \
			|| !state.provider->field) { \
			return UnavailableResult(); \
		} \
		return state.provider->field arguments; \
	} while (0)

fnqlSteamResult_t FNQL_Steam_OpenOverlayUrl(const char *url) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_OVERLAY, open_overlay_url, (url));
}
fnqlSteamResult_t FNQL_Steam_OpenOverlayUser(const char *dialog, uint64_t steamId) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_OVERLAY, open_overlay_user, (dialog, steamId));
}
fnqlSteamResult_t FNQL_Steam_SetRichPresence(const char *key, const char *value) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_RICH_PRESENCE, set_rich_presence, (key, value));
}
fnqlSteamResult_t FNQL_Steam_RequestServers(uint32_t requestMode, uint32_t appId) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_SERVER_BROWSER, request_servers,
		(requestMode, appId));
}
fnqlSteamResult_t FNQL_Steam_RefreshServers(void) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_SERVER_BROWSER, refresh_servers, ());
}
void FNQL_Steam_CancelServers(void) {
	if (FNQL_Steam_Available(FNQL_STEAM_CAP_SERVER_BROWSER)
		&& state.provider->cancel_servers) {
		state.provider->cancel_servers();
	}
}
fnqlSteamResult_t FNQL_Steam_CreateLobby(uint32_t lobbyType, uint32_t maxMembers) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_LOBBIES, create_lobby, (lobbyType, maxMembers));
}
fnqlSteamResult_t FNQL_Steam_JoinLobby(uint64_t lobbyId) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_LOBBIES, join_lobby, (lobbyId));
}
fnqlSteamResult_t FNQL_Steam_LeaveLobby(uint64_t lobbyId) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_LOBBIES, leave_lobby, (lobbyId));
}
fnqlSteamResult_t FNQL_Steam_SetLobbyServer(uint64_t lobbyId, uint32_t ip,
	uint16_t port, uint64_t serverId) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_LOBBIES, set_lobby_server,
		(lobbyId, ip, port, serverId));
}
fnqlSteamResult_t FNQL_Steam_InviteToLobby(uint64_t lobbyId, uint64_t steamId) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_LOBBIES, invite_to_lobby, (lobbyId, steamId));
}
fnqlSteamResult_t FNQL_Steam_SendLobbyChat(uint64_t lobbyId, const void *bytes,
	uint32_t byteCount) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_LOBBIES, send_lobby_chat,
		(lobbyId, bytes, byteCount));
}
fnqlSteamResult_t FNQL_Steam_GetSubscribedItems(uint64_t *itemIds,
	uint32_t capacity, uint32_t *count) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_UGC, get_subscribed_items,
		(itemIds, capacity, count));
}
fnqlSteamResult_t FNQL_Steam_GetItemInstallInfo(uint64_t itemId, char *folder,
	uint32_t folderCapacity, uint64_t *sizeOnDisk, uint32_t *timestamp) {
	if (!folder || folderCapacity == 0) {
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	}
	std::memset(folder, 0, folderCapacity);
	if (sizeOnDisk) {
		*sizeOnDisk = 0;
	}
	if (timestamp) {
		*timestamp = 0;
	}
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_UGC)
		|| !FNQL_HAS_PROVIDER_FIELD(get_item_install_info)
		|| !state.provider->get_item_install_info) {
		return UnavailableResult();
	}
	const fnqlSteamResult_t result = state.provider->get_item_install_info(
		itemId, folder, folderCapacity, sizeOnDisk, timestamp);
	if (result != FNQL_STEAM_RESULT_OK) {
		folder[0] = '\0';
		if (sizeOnDisk) {
			*sizeOnDisk = 0;
		}
		if (timestamp) {
			*timestamp = 0;
		}
		return result;
	}
	if (!std::memchr(folder, '\0', folderCapacity)) {
		folder[0] = '\0';
		if (sizeOnDisk) {
			*sizeOnDisk = 0;
		}
		if (timestamp) {
			*timestamp = 0;
		}
		return FNQL_STEAM_RESULT_FAILED;
	}
	return FNQL_STEAM_RESULT_OK;
}
fnqlSteamResult_t FNQL_Steam_GetItemDownloadInfo(uint64_t itemId,
	uint64_t *downloaded, uint64_t *total) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_UGC, get_item_download_info,
		(itemId, downloaded, total));
}
fnqlSteamResult_t FNQL_Steam_GetItemState(uint64_t itemId,
	uint32_t *stateFlags) {
	if (stateFlags) {
		*stateFlags = 0;
	}
	if (!itemId || !stateFlags) {
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	}
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_UGC)
		|| !FNQL_HAS_PROVIDER_FIELD(get_item_state)
		|| !state.provider->get_item_state) {
		return UnavailableResult();
	}
	return state.provider->get_item_state(itemId, stateFlags);
}
fnqlSteamResult_t FNQL_Steam_GetAvatarRGBA(uint64_t steamId,
	uint32_t avatarSize, void *rgba, uint32_t rgbaCapacity,
	uint32_t *width, uint32_t *height, uint32_t *requiredSize) {
	if (width) *width = 0;
	if (height) *height = 0;
	if (requiredSize) *requiredSize = 0;
	if (!steamId || avatarSize > FNQL_STEAM_AVATAR_LARGE
		|| !width || !height || !requiredSize) {
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	}
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_AVATARS)
		|| !FNQL_HAS_PROVIDER_FIELD(get_avatar_rgba)
		|| !state.provider->get_avatar_rgba) {
		return UnavailableResult();
	}
	return state.provider->get_avatar_rgba(steamId, avatarSize, rgba,
		rgbaCapacity, width, height, requiredSize);
}
fnqlSteamResult_t FNQL_Steam_GetFriends(uint32_t friendFlags,
	fnqlSteamFriend_t *friends, uint32_t capacity, uint32_t *count) {
	if (count) *count = 0;
	if (!friendFlags || !count) {
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	}
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_FRIENDS)
		|| !FNQL_HAS_PROVIDER_FIELD(get_friend) || !state.provider->get_friends) {
		return UnavailableResult();
	}
	return state.provider->get_friends(friendFlags, friends, capacity, count);
}
fnqlSteamResult_t FNQL_Steam_GetFriend(uint64_t steamId,
	fnqlSteamFriend_t *friendInfo) {
	if (!steamId || !friendInfo || friendInfo->size < sizeof(*friendInfo)) {
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	}
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_FRIENDS)
		|| !FNQL_HAS_PROVIDER_FIELD(get_friend) || !state.provider->get_friend) {
		return UnavailableResult();
	}
	return state.provider->get_friend(steamId, friendInfo);
}
fnqlSteamResult_t FNQL_Steam_RequestUgcQuery(uint32_t rawFilter) {
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_UGC_QUERY)
		|| !FNQL_HAS_PROVIDER_FIELD(get_ugc_query_results)
		|| !state.provider->request_ugc_query) {
		return UnavailableResult();
	}
	return state.provider->request_ugc_query(rawFilter);
}
fnqlSteamResult_t FNQL_Steam_GetUgcQueryResults(fnqlSteamUgcItem_t *items,
	uint32_t capacity, uint32_t *count, uint32_t *totalMatching) {
	if (count) *count = 0;
	if (totalMatching) *totalMatching = 0;
	if (!count || !totalMatching) return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_UGC_QUERY)
		|| !FNQL_HAS_PROVIDER_FIELD(get_ugc_query_results)
		|| !state.provider->get_ugc_query_results) {
		return UnavailableResult();
	}
	return state.provider->get_ugc_query_results(items, capacity, count,
		totalMatching);
}
static uint64_t P2PCapability(uint32_t role) {
	return role == FNQL_STEAM_ROLE_CLIENT ? FNQL_STEAM_CAP_CLIENT_P2P
		: role == FNQL_STEAM_ROLE_GAME_SERVER
			? FNQL_STEAM_CAP_GAME_SERVER_P2P : 0;
}
fnqlSteamResult_t FNQL_Steam_SendP2PPacket(uint32_t role, uint64_t remoteId,
	const void *data, uint32_t dataSize, uint32_t sendType, int32_t channel) {
	const uint64_t capability = P2PCapability(role);
	if (!capability || !FNQL_Steam_Available(capability)
		|| !FNQL_HAS_PROVIDER_FIELD(send_p2p_packet)
		|| !state.provider->send_p2p_packet) return UnavailableResult();
	return state.provider->send_p2p_packet(role, remoteId, data, dataSize,
		sendType, channel);
}
fnqlSteamResult_t FNQL_Steam_PeekP2PPacket(uint32_t role, int32_t channel,
	uint32_t *packetSize) {
	if (packetSize) *packetSize = 0;
	const uint64_t capability = P2PCapability(role);
	if (!packetSize || !capability) return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	if (!FNQL_Steam_Available(capability)
		|| !FNQL_HAS_PROVIDER_FIELD(peek_p2p_packet)
		|| !state.provider->peek_p2p_packet) return UnavailableResult();
	return state.provider->peek_p2p_packet(role, channel, packetSize);
}
fnqlSteamResult_t FNQL_Steam_ReadP2PPacket(uint32_t role, int32_t channel,
	void *data, uint32_t capacity, uint32_t *dataSize, uint64_t *remoteId) {
	if (dataSize) *dataSize = 0;
	if (remoteId) *remoteId = 0;
	const uint64_t capability = P2PCapability(role);
	if (!data || !capacity || !dataSize || !remoteId || !capability)
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	if (!FNQL_Steam_Available(capability)
		|| !FNQL_HAS_PROVIDER_FIELD(read_p2p_packet)
		|| !state.provider->read_p2p_packet) return UnavailableResult();
	return state.provider->read_p2p_packet(role, channel, data, capacity,
		dataSize, remoteId);
}
fnqlSteamResult_t FNQL_Steam_AcceptP2PSession(uint32_t role,
	uint64_t remoteId) {
	const uint64_t capability = P2PCapability(role);
	if (!capability || !remoteId) return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	if (!FNQL_Steam_Available(capability)
		|| !FNQL_HAS_PROVIDER_FIELD(accept_p2p_session)
		|| !state.provider->accept_p2p_session) return UnavailableResult();
	return state.provider->accept_p2p_session(role, remoteId);
}
fnqlSteamResult_t FNQL_Steam_CloseP2PSession(uint32_t role,
	uint64_t remoteId) {
	const uint64_t capability = P2PCapability(role);
	if (!capability || !remoteId) return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	if (!FNQL_Steam_Available(capability)
		|| !FNQL_HAS_PROVIDER_FIELD(close_p2p_session)
		|| !state.provider->close_p2p_session) return UnavailableResult();
	return state.provider->close_p2p_session(role, remoteId);
}
fnqlSteamResult_t FNQL_Steam_StartVoiceRecording(void) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_VOICE, start_voice_recording, ());
}
void FNQL_Steam_StopVoiceRecording(void) {
	if (FNQL_Steam_Available(FNQL_STEAM_CAP_VOICE)
		&& FNQL_HAS_PROVIDER_FIELD(stop_voice_recording)
		&& state.provider->stop_voice_recording) state.provider->stop_voice_recording();
}
fnqlSteamResult_t FNQL_Steam_GetCompressedVoice(void *data,
	uint32_t capacity, uint32_t *dataSize) {
	if (dataSize) *dataSize = 0;
	if (!data || !capacity || !dataSize) return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_VOICE)
		|| !FNQL_HAS_PROVIDER_FIELD(get_compressed_voice)
		|| !state.provider->get_compressed_voice) return UnavailableResult();
	return state.provider->get_compressed_voice(data, capacity, dataSize);
}
fnqlSteamResult_t FNQL_Steam_DecompressVoice(const void *compressed,
	uint32_t compressedSize, void *pcm, uint32_t pcmCapacity,
	uint32_t *pcmSize, uint32_t sampleRate) {
	if (pcmSize) *pcmSize = 0;
	if (!compressed || !compressedSize || !pcm || !pcmCapacity || !pcmSize
		|| sampleRate < 11025u || sampleRate > 48000u)
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_VOICE)
		|| !FNQL_HAS_PROVIDER_FIELD(decompress_voice)
		|| !state.provider->decompress_voice) return UnavailableResult();
	return state.provider->decompress_voice(compressed, compressedSize, pcm,
		pcmCapacity, pcmSize, sampleRate);
}
uint32_t FNQL_Steam_GetVoiceSampleRate(void) {
	return FNQL_Steam_Available(FNQL_STEAM_CAP_VOICE)
		&& FNQL_HAS_PROVIDER_FIELD(get_voice_sample_rate)
		&& state.provider->get_voice_sample_rate
		? state.provider->get_voice_sample_rate() : 0u;
}
fnqlSteamResult_t FNQL_Steam_GetGameServerSteamId(uint64_t *steamId) {
	if (steamId) *steamId = 0;
	if (!steamId) return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_GAME_SERVER_P2P)
		|| !FNQL_HAS_PROVIDER_FIELD(get_game_server_steam_id)
		|| !state.provider->get_game_server_steam_id) return UnavailableResult();
	return state.provider->get_game_server_steam_id(steamId);
}
fnqlSteamResult_t FNQL_Steam_GetClientUserStatI32(uint64_t steamId,
	const char *name, int32_t *value) {
	if (value) *value = 0;
	if (!steamId || !name || !name[0] || !value)
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_CLIENT_STATS)
		|| !FNQL_HAS_PROVIDER_FIELD(get_client_user_stat_i32)
		|| !state.provider->get_client_user_stat_i32) return UnavailableResult();
	return state.provider->get_client_user_stat_i32(steamId, name, value);
}
fnqlSteamResult_t FNQL_Steam_RequestClientUserStats(uint64_t steamId) {
	if (!steamId) return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_CLIENT_STATS,
		request_client_user_stats, (steamId));
}
fnqlSteamResult_t FNQL_Steam_GetClientUserStatF32(uint64_t steamId,
	const char *name, float *value) {
	if (value) *value = 0.0f;
	if (!steamId || !name || !name[0] || !value)
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_CLIENT_STATS)
		|| !FNQL_HAS_PROVIDER_FIELD(get_client_user_stat_f32)
		|| !state.provider->get_client_user_stat_f32) return UnavailableResult();
	return state.provider->get_client_user_stat_f32(steamId, name, value);
}
fnqlSteamResult_t FNQL_Steam_GetClientUserAchievement(uint64_t steamId,
	const char *name, qboolean *unlocked, uint32_t *unlockTime) {
	fnqlSteamBool_t providerUnlocked = 0;
	if (unlocked) *unlocked = qfalse;
	if (unlockTime) *unlockTime = 0;
	if (!steamId || !name || !name[0] || !unlocked || !unlockTime)
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_CLIENT_STATS)
		|| !FNQL_HAS_PROVIDER_FIELD(get_client_user_achievement)
		|| !state.provider->get_client_user_achievement) return UnavailableResult();
	const fnqlSteamResult_t result = state.provider->get_client_user_achievement(
		steamId, name, &providerUnlocked, unlockTime);
	if (result == FNQL_STEAM_RESULT_OK) *unlocked = providerUnlocked ? qtrue : qfalse;
	return result;
}
fnqlSteamResult_t FNQL_Steam_GetAchievementDisplayAttribute(const char *name,
	const char *key, char *value, uint32_t valueCapacity) {
	if (value && valueCapacity) value[0] = '\0';
	if (!name || !name[0] || !key || !key[0] || !value || valueCapacity < 2)
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_CLIENT_STATS)
		|| !FNQL_HAS_PROVIDER_FIELD(get_achievement_display_attribute)
		|| !state.provider->get_achievement_display_attribute) return UnavailableResult();
	return state.provider->get_achievement_display_attribute(name, key, value,
		valueCapacity);
}
fnqlSteamResult_t FNQL_Steam_SetClientAchievement(const char *name) {
	if (!name || !name[0]) return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_CLIENT_STATS,
		set_client_achievement, (name));
}
fnqlSteamResult_t FNQL_Steam_StoreClientStats(void) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_CLIENT_STATS, store_client_stats, ());
}
fnqlSteamResult_t FNQL_Steam_ResetClientStats(qboolean includeAchievements) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_CLIENT_STATS, reset_client_stats,
		(includeAchievements ? 1u : 0u));
}
fnqlSteamResult_t FNQL_Steam_GetLobbyMembers(uint64_t lobbyId,
	fnqlSteamLobbyMember_t *members, uint32_t capacity, uint32_t *count,
	uint64_t *ownerId, uint32_t *memberLimit) {
	if (count) *count = 0;
	if (ownerId) *ownerId = 0;
	if (memberLimit) *memberLimit = 0;
	if (!lobbyId || !count || !ownerId || !memberLimit)
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_LOBBY_SNAPSHOT)
		|| !FNQL_HAS_PROVIDER_FIELD(get_lobby_members)
		|| !state.provider->get_lobby_members) return UnavailableResult();
	return state.provider->get_lobby_members(lobbyId, members, capacity, count,
		ownerId, memberLimit);
}
fnqlSteamResult_t FNQL_Steam_GetLobbyData(uint64_t lobbyId,
	fnqlSteamLobbyData_t *items, uint32_t capacity, uint32_t *count) {
	if (count) *count = 0;
	if (!lobbyId || !count) return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_LOBBY_SNAPSHOT)
		|| !FNQL_HAS_PROVIDER_FIELD(get_lobby_data)
		|| !state.provider->get_lobby_data) return UnavailableResult();
	return state.provider->get_lobby_data(lobbyId, items, capacity, count);
}
fnqlSteamResult_t FNQL_Steam_SetLobbyData(uint64_t lobbyId, const char *key,
	const char *value) {
	if (!lobbyId || !key || !key[0] || !value)
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_LOBBY_SNAPSHOT)
		|| !FNQL_HAS_PROVIDER_FIELD(set_lobby_data)
		|| !state.provider->set_lobby_data) return UnavailableResult();
	return state.provider->set_lobby_data(lobbyId, key, value);
}
fnqlSteamResult_t FNQL_Steam_InviteToGame(uint64_t steamId,
	const char *connectString) {
	if (!steamId || !connectString || !connectString[0])
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_LOBBY_SNAPSHOT)
		|| !FNQL_HAS_PROVIDER_FIELD(invite_to_game)
		|| !state.provider->invite_to_game) return UnavailableResult();
	return state.provider->invite_to_game(steamId, connectString);
}
fnqlSteamResult_t FNQL_Steam_HandleGameServerPacket(const void *data,
	uint32_t dataSize, uint32_t sourceIp, uint16_t sourcePort) {
	if (!data || !dataSize || !sourcePort)
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_GAME_SERVER_PACKET_IO)
		|| !FNQL_HAS_PROVIDER_FIELD(handle_game_server_packet)
		|| !state.provider->handle_game_server_packet) return UnavailableResult();
	return state.provider->handle_game_server_packet(data, dataSize, sourceIp,
		sourcePort);
}
fnqlSteamResult_t FNQL_Steam_GetGameServerPacket(void *data,
	uint32_t capacity, uint32_t *dataSize, uint32_t *destinationIp,
	uint16_t *destinationPort) {
	if (dataSize) *dataSize = 0;
	if (destinationIp) *destinationIp = 0;
	if (destinationPort) *destinationPort = 0;
	if (!data || !capacity || !dataSize || !destinationIp || !destinationPort)
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_GAME_SERVER_PACKET_IO)
		|| !FNQL_HAS_PROVIDER_FIELD(get_game_server_packet)
		|| !state.provider->get_game_server_packet) return UnavailableResult();
	return state.provider->get_game_server_packet(data, capacity, dataSize,
		destinationIp, destinationPort);
}
fnqlSteamResult_t FNQL_Steam_SetGameServerKeyValue(const char *key,
	const char *value) {
	if (!key || !key[0] || !value) return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_GAME_SERVER_METADATA,
		set_game_server_key_value, (key, value));
}
fnqlSteamResult_t FNQL_Steam_UpdateGameServerUser(uint64_t steamId,
	const char *playerName, uint32_t score) {
	if (!steamId || !playerName) return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_GAME_SERVER_METADATA,
		update_game_server_user, (steamId, playerName, score));
}
fnqlSteamResult_t FNQL_Steam_GetGameServerPublicIp(uint32_t *publicIp) {
	if (publicIp) *publicIp = 0;
	if (!publicIp) return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_GAME_SERVER_METADATA)
		|| !FNQL_HAS_PROVIDER_FIELD(get_game_server_public_ip)
		|| !state.provider->get_game_server_public_ip) return UnavailableResult();
	return state.provider->get_game_server_public_ip(publicIp);
}
fnqlSteamResult_t FNQL_Steam_CreateUnauthenticatedUser(uint64_t *steamId) {
	if (steamId) *steamId = 0;
	if (!steamId) return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_GAME_SERVER_METADATA)
		|| !FNQL_HAS_PROVIDER_FIELD(create_unauthenticated_user)
		|| !state.provider->create_unauthenticated_user) return UnavailableResult();
	return state.provider->create_unauthenticated_user(steamId);
}
fnqlSteamResult_t FNQL_Steam_SetFavoriteServer(uint32_t appId, uint32_t ip,
	uint16_t connectionPort, uint16_t queryPort, qboolean add) {
	if (!appId || !ip || !connectionPort || !queryPort) {
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	}
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_FAVORITES, set_favorite_server,
		(appId, ip, connectionPort, queryPort, add ? 1u : 0u));
}
fnqlSteamResult_t FNQL_Steam_RequestServerDetails(uint32_t ip,
	uint16_t queryPort) {
	if (!ip || !queryPort) return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_SERVER_DETAILS,
		request_server_details, (ip, queryPort));
}
void FNQL_Steam_CancelServerDetails(void) {
	if (FNQL_Steam_Available(FNQL_STEAM_CAP_SERVER_DETAILS)
		&& FNQL_HAS_PROVIDER_FIELD(cancel_server_details)
		&& state.provider->cancel_server_details) {
		state.provider->cancel_server_details();
	}
}
fnqlSteamResult_t FNQL_Steam_SetGameServerBotCount(uint32_t botCount) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_GAME_SERVER_METADATA,
		set_game_server_bot_count, (botCount));
}
fnqlSteamResult_t FNQL_Steam_SetLocalVoiceSpeaking(qboolean speaking) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_VOICE, set_local_voice_speaking,
		(speaking ? 1u : 0u));
}
fnqlSteamResult_t FNQL_Steam_DownloadItem(uint64_t itemId, qboolean highPriority) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_UGC, download_item,
		(itemId, highPriority ? 1u : 0u));
}
fnqlSteamResult_t FNQL_Steam_SubscribeItem(uint64_t itemId) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_UGC, subscribe_item, (itemId));
}
fnqlSteamResult_t FNQL_Steam_UnsubscribeItem(uint64_t itemId) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_UGC, unsubscribe_item, (itemId));
}
fnqlSteamResult_t FNQL_Steam_RequestUserStats(uint64_t steamId) {
	if (!state.initialized ||
		!(state.capabilities & (FNQL_STEAM_CAP_STATS | FNQL_STEAM_CAP_GAME_SERVER_STATS)) ||
		!state.provider->request_user_stats) {
		return UnavailableResult();
	}
	return state.provider->request_user_stats(steamId);
}
fnqlSteamResult_t FNQL_Steam_GetUserStatI32(uint64_t steamId,
	const char *name, int32_t *value) {
	if (!name || !name[0] || !value) {
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	}
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_GAME_SERVER_STATS) ||
		!FNQL_HAS_PROVIDER_FIELD(get_user_stat_i32) ||
		!state.provider->get_user_stat_i32) {
		return UnavailableResult();
	}
	return state.provider->get_user_stat_i32(steamId, name, value);
}
fnqlSteamResult_t FNQL_Steam_SetUserStatI32(uint64_t steamId,
	const char *name, int32_t value) {
	if (!name || !name[0]) {
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	}
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_GAME_SERVER_STATS) ||
		!FNQL_HAS_PROVIDER_FIELD(set_user_stat_i32) ||
		!state.provider->set_user_stat_i32) {
		return UnavailableResult();
	}
	return state.provider->set_user_stat_i32(steamId, name, value);
}
fnqlSteamResult_t FNQL_Steam_GetUserAchievement(uint64_t steamId,
	const char *name, qboolean *unlocked) {
	fnqlSteamBool_t providerUnlocked = 0;
	if (unlocked) {
		*unlocked = qfalse;
	}
	if (!name || !name[0] || !unlocked) {
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	}
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_GAME_SERVER_STATS) ||
		!FNQL_HAS_PROVIDER_FIELD(get_user_achievement) ||
		!state.provider->get_user_achievement) {
		return UnavailableResult();
	}
	const fnqlSteamResult_t result = state.provider->get_user_achievement(
		steamId, name, &providerUnlocked);
	if (result == FNQL_STEAM_RESULT_OK) {
		*unlocked = providerUnlocked ? qtrue : qfalse;
	}
	return result;
}
fnqlSteamResult_t FNQL_Steam_SetUserAchievement(uint64_t steamId,
	const char *name) {
	if (!name || !name[0]) {
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	}
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_GAME_SERVER_STATS) ||
		!FNQL_HAS_PROVIDER_FIELD(set_user_achievement) ||
		!state.provider->set_user_achievement) {
		return UnavailableResult();
	}
	return state.provider->set_user_achievement(steamId, name);
}
fnqlSteamResult_t FNQL_Steam_StoreUserStats(uint64_t steamId) {
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_GAME_SERVER_STATS) ||
		!FNQL_HAS_PROVIDER_FIELD(store_user_stats) ||
		!state.provider->store_user_stats) {
		return UnavailableResult();
	}
	return state.provider->store_user_stats(steamId);
}

fnqlSteamResult_t FNQL_Steam_SerializeRetailJson(const void *retailValue,
	char *json, uint32_t jsonCapacity, uint32_t *jsonSize) {
	if (jsonSize) {
		*jsonSize = 0;
	}
	if (!retailValue || !json || jsonCapacity < 2 || !jsonSize) {
		return FNQL_STEAM_RESULT_INVALID_ARGUMENT;
	}
	json[0] = '\0';
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_RETAIL_JSON)
		|| !FNQL_HAS_PROVIDER_FIELD(serialize_retail_json)
		|| !state.provider->serialize_retail_json) {
		return FNQL_STEAM_RESULT_UNAVAILABLE;
	}
	const fnqlSteamResult_t result = state.provider->serialize_retail_json(
		retailValue, json, jsonCapacity, jsonSize);
	if (result != FNQL_STEAM_RESULT_OK) {
		json[0] = '\0';
		*jsonSize = 0;
		return result;
	}
	if (*jsonSize >= jsonCapacity || json[*jsonSize] != '\0') {
		json[0] = '\0';
		*jsonSize = 0;
		return FNQL_STEAM_RESULT_FAILED;
	}
	return FNQL_STEAM_RESULT_OK;
}
fnqlSteamResult_t FNQL_Steam_GetAuthTicket(void *ticket, uint32_t ticketCapacity,
	uint32_t *ticketSize, uint32_t *ticketHandle) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_AUTH, get_auth_ticket,
		(ticket, ticketCapacity, ticketSize, ticketHandle));
}
void FNQL_Steam_CancelAuthTicket(uint32_t ticketHandle) {
	if (FNQL_Steam_Available(FNQL_STEAM_CAP_AUTH) && state.provider->cancel_auth_ticket) {
		state.provider->cancel_auth_ticket(ticketHandle);
	}
}
fnqlSteamResult_t FNQL_Steam_BeginAuthSession(const void *ticket,
	uint32_t ticketSize, uint64_t steamId) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_AUTH, begin_auth_session,
		(ticket, ticketSize, steamId));
}
void FNQL_Steam_EndAuthSession(uint64_t steamId) {
	if (FNQL_Steam_Available(FNQL_STEAM_CAP_AUTH) && state.provider->end_auth_session) {
		state.provider->end_auth_session(steamId);
	}
}
fnqlSteamResult_t FNQL_Steam_StartGameServer(const fnqlSteamGameServerConfig_t *config) {
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_GAME_SERVER)
		|| !FNQL_HAS_PROVIDER_FIELD(start_game_server)
		|| !state.provider->start_game_server) {
		return UnavailableResult();
	}
	const fnqlSteamResult_t result = state.provider->start_game_server(config);
	if (result == FNQL_STEAM_RESULT_OK || result == FNQL_STEAM_RESULT_PENDING) {
		RefreshProviderCapabilities("GameServer startup");
	}
	return result;
}
void FNQL_Steam_StopGameServer(void) {
	if (FNQL_Steam_Available(FNQL_STEAM_CAP_GAME_SERVER) && state.provider->stop_game_server) {
		state.provider->stop_game_server();
		RefreshProviderCapabilities("GameServer shutdown");
	}
}
fnqlSteamResult_t FNQL_Steam_UpdateGameServer(const fnqlSteamGameServerConfig_t *config) {
	FNQL_STEAM_CALL_RESULT(FNQL_STEAM_CAP_GAME_SERVER, update_game_server, (config));
}

#undef FNQL_STEAM_CALL_RESULT

} // extern "C"
