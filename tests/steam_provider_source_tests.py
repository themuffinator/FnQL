from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class SteamProviderSourceTests(unittest.TestCase):
    def test_release_policy_distributes_only_the_pinned_closed_provider_binary(self) -> None:
        manifest = json.loads(
            (ROOT / "version" / "fnql_steam_provider.json").read_text(encoding="utf-8")
        )
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(
            encoding="utf-8"
        )
        policy = (ROOT / "docs" / "fnql" / "STEAM_PROVIDER.md").read_text(
            encoding="utf-8"
        )
        notice = (
            ROOT / "docs" / "fnql" / "STEAM_PROVIDER_BINARY_NOTICE.txt"
        ).read_text(encoding="utf-8")

        self.assertEqual(manifest["version"], "0.3.1")
        self.assertEqual(manifest["asset"], "fnql_steam.dll")
        self.assertEqual(manifest["pe_machine"], "i386")
        self.assertEqual(len(manifest["sha256"]), 64)
        self.assertEqual(workflow.count("fetch_steam_provider.py"), 2)
        self.assertNotIn("FnQL-Steam.git", workflow)
        self.assertIn("must remain private", policy)
        self.assertIn("Provider source is not fetched or published", policy)
        self.assertIn("source remains proprietary", notice)
        self.assertIn("does not include Valve's steam_api.dll", notice)

    def test_public_contract_is_versioned_bounded_and_c_compatible(self) -> None:
        source = (ROOT / "code/platform/fnql_steam_api.h").read_text(encoding="utf-8")
        self.assertIn("#define FNQL_STEAM_ABI_VERSION 0x00010000u", source)
        self.assertIn('FNQL_STEAM_GET_PROVIDER_SYMBOL "FnQLSteam_GetProvider"', source)
        self.assertIn("uint32_t size;", source)
        self.assertIn("uint32_t abi_version;", source)
        self.assertIn("FNQL_STEAM_AUTH_TICKET_CAPACITY 2048u", source)
        self.assertIn("FNQL_STEAM_SERVER_BROWSER_INTERNET = 0u", source)
        self.assertIn("FNQL_STEAM_SERVER_BROWSER_FRIENDS = 5u", source)
        self.assertIn("extern \"C\"", source)
        self.assertNotIn("std::", source)
        self.assertNotIn("Steamworks", source)

    def test_loader_is_default_on_and_uses_contained_library_paths(self) -> None:
        source = (ROOT / "code/platform/fnql_steam.cpp").read_text(encoding="utf-8")
        self.assertIn('Cvar_Get("com_steamIntegration", "1", CVAR_ARCHIVE | CVAR_INIT)', source)
        self.assertIn("IsAbsolutePath", source)
        self.assertIn("IsBareFileName", source)
        self.assertIn("BuildCandidatePaths", source)
        self.assertIn("Sys_DefaultAppPath()", source)
        self.assertIn("Sys_DefaultBasePath()", source)
        primary_load = source.index("state.library = OpenLibrary(state.providerPath)")
        fallback_guard = source.index("if (!state.library && providerFallbackPath[0])")
        fallback_load = source.index(
            "state.library = OpenLibrary(state.providerPath)", fallback_guard
        )
        self.assertLess(primary_load, fallback_guard)
        self.assertLess(fallback_guard, fallback_load)
        self.assertIn(
            "CopyText(state.providerPath, sizeof(state.providerPath), providerFallbackPath)",
            source[fallback_guard:fallback_load],
        )
        self.assertIn("LoadLibraryExW", source)
        self.assertIn("LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32", source)
        self.assertIn("RTLD_NOW | RTLD_LOCAL", source)
        self.assertIn("ValidatedCapabilities", source)
        self.assertIn("requiredProviderSize", source)
        self.assertIn("FNQL_HAS_PROVIDER_FIELD(store_user_stats)", source)
        self.assertIn("FNQL_Steam_StoreUserStats", source)
        self.assertIn("FNQL_HAS_PROVIDER_FIELD(serialize_retail_json)", source)
        self.assertIn("FNQL_Steam_SerializeRetailJson", source)
        self.assertIn("FNQL_HAS_PROVIDER_FIELD(get_item_state)", source)
        self.assertIn("FNQL_Steam_GetItemState", source)
        self.assertIn("FNQL_HAS_PROVIDER_FIELD(get_avatar_rgba)", source)
        self.assertIn("FNQL_Steam_GetAvatarRGBA", source)
        self.assertIn("FNQL_HAS_PROVIDER_FIELD(get_friend)", source)
        self.assertIn("FNQL_Steam_GetFriends", source)
        self.assertIn("FNQL_Steam_GetFriend", source)
        self.assertIn("FNQL_HAS_PROVIDER_FIELD(get_ugc_query_results)", source)
        self.assertIn("FNQL_Steam_RequestUgcQuery", source)
        self.assertIn("FNQL_Steam_GetUgcQueryResults", source)
        self.assertIn("FNQL_HAS_PROVIDER_FIELD(get_game_server_steam_id)", source)
        self.assertIn("FNQL_Steam_SendP2PPacket", source)
        self.assertIn("FNQL_Steam_ReadP2PPacket", source)
        self.assertIn("FNQL_Steam_DecompressVoice", source)

    def test_async_stats_store_has_an_explicit_completion_contract(self) -> None:
        api = (ROOT / "code/platform/fnql_steam_api.h").read_text(encoding="utf-8")
        server = (ROOT / "code/server/sv_client.cpp").read_text(encoding="utf-8")
        session = (ROOT / "code/server/stats_session.hpp").read_text(encoding="utf-8")

        self.assertIn("FNQL_STEAM_EVENT_USER_STATS_STORED", api)
        self.assertIn("type == FNQL_STEAM_EVENT_USER_STATS_STORED", server)
        self.assertIn("BeginStorePending", server)
        self.assertIn("CompletePendingStore", server)
        self.assertIn("pendingFieldGeneration_", session)

    def test_server_auth_does_not_treat_ticket_acceptance_as_validation(self) -> None:
        server = (ROOT / "code/server/sv_client.cpp").read_text(encoding="utf-8")
        api = (ROOT / "code/platform/fnql_steam_api.h").read_text(encoding="utf-8")
        self.assertIn("result != FNQL_STEAM_RESULT_OK && result != FNQL_STEAM_RESULT_PENDING", server)
        self.assertIn("platformAuthValidated = result == FNQL_STEAM_RESULT_OK", server)
        self.assertIn("FNQL_STEAM_EVENT_AUTH_RESULT", server)
        self.assertIn("Final asynchronous validation for an accepted begin_auth_session", api)

    def test_fnql_client_uses_retail_ticket_shape_and_no_steam_fallback(self) -> None:
        client = (ROOT / "code/client/cl_main.cpp").read_text(encoding="utf-8")
        builder = (ROOT / "code/client/retail_challenge.hpp").read_text(encoding="utf-8")
        server = (ROOT / "code/server/sv_client.cpp").read_text(encoding="utf-8")
        parser = (ROOT / "code/server/retail_auth_challenge.hpp").read_text(encoding="utf-8")
        self.assertIn("CL_SendRetailSteamChallenge", client)
        self.assertIn("FNQL_Steam_GetAuthTicket", client)
        self.assertIn("BuildRetailSteamChallenge", client)
        self.assertIn("RetailWithoutSteam", client)
        self.assertIn('NET_OutOfBandPrint( NS_CLIENT, &clc.serverAddress, "getchallenge" )', client)
        self.assertIn("No text terminator, client nonce, or FnQL marker", builder)
        self.assertNotIn('marker[] = "FnQLAuth"', client)
        self.assertNotIn("Authenticated challenge response is not from an FnQL host", client)
        self.assertIn("retail.fnqlExtension", server)
        self.assertIn("static_cast<std::int32_t>( retail.clientChallenge )", server)
        self.assertIn("FnqlHandshakeMarker", server)
        self.assertIn("fnqlExtension = false", parser)

    def test_loopback_listen_server_consumes_the_same_bound_ticket(self) -> None:
        server = (ROOT / "code/server/sv_client.cpp").read_text(encoding="utf-8")
        self.assertIn("pendingRetailAuth.Consume", server)
        self.assertIn("if ( cl_proto == QL_RETAIL_PROTOCOL_VERSION )", server)
        self.assertNotIn(
            "cl_proto == QL_RETAIL_PROTOCOL_VERSION && !NET_IsLocalAddress( from )",
            server,
        )
        self.assertIn("localLoopback = from && from->type == NA_LOOPBACK", server)
        self.assertIn("FNQL_Steam_GetStatus( &localStatus )", server)
        self.assertIn("A signed-in Steam identity is required for local play.", server)
        self.assertIn("platformAuthTicketSession = true", server)
        self.assertIn("if ( client->platformAuthTicketSession", server)

    def test_engine_lifecycle_pumps_and_stops_provider_once(self) -> None:
        source = (ROOT / "code/qcommon/common.c").read_text(encoding="utf-8")
        self.assertIn("FNQL_Steam_Init( com_dedicated->integer", source)
        self.assertIn("FNQL_Steam_Pump();", source)
        self.assertIn("FNQL_Steam_Reconfigure( com_dedicated->integer", source)
        self.assertIn("FNQL_Steam_Shutdown();", source)

    def test_signed_out_client_is_reported_as_unavailable_not_failed(self) -> None:
        source = (ROOT / "code/platform/fnql_steam.cpp").read_text(encoding="utf-8")
        self.assertIn("result == FNQL_STEAM_RESULT_UNAVAILABLE", source)
        self.assertIn('? "unavailable" : "startup-failed"', source)

    def test_startup_checks_the_steam_account_beside_the_library_checks(self) -> None:
        loader = (ROOT / "code/platform/fnql_steam.cpp").read_text(encoding="utf-8")

        # The account is a separate condition from the provider and steam_api
        # libraries, and is read only from the provider status.
        self.assertIn("bool SteamAccountSignedIn()", loader)
        self.assertIn("FNQL_Steam_GetStatus(&status)", loader)
        self.assertIn(
            "status.client_logged_on != 0 && status.local_steam_id != 0", loader
        )

        # Both signed-out shapes reach one notification: an unavailable startup
        # result, and a provider that starts without an active user.
        self.assertIn("result == FNQL_STEAM_RESULT_UNAVAILABLE", loader)
        self.assertIn("? SteamSession::SignedOut : SteamSession::Resolved;", loader)
        started = loader.index("state.initialized = true;")
        account_check = loader.index("SteamAccountSignedIn()", started)
        self.assertLess(started, account_check)
        self.assertLess(account_check, loader.index("return SteamSession::SignedOut;", started))

        # A library failure a sign-in cannot fix keeps the existing fallback.
        for resolved in (
            "com_steamProvider must be an absolute path or a bare library name.",
            "Steam provider library was not found or could not be loaded: %s",
            "Steam API path is unavailable or com_steamApi is not absolute.",
            "Steam provider ABI is incompatible or incomplete.",
            "Steam provider exposed no usable capabilities.",
        ):
            branch = loader.index(resolved)
            self.assertIn(
                "return SteamSession::Resolved;",
                loader[branch : loader.index("\n\t}", branch)],
            )

    def test_missing_steam_login_offers_proceed_retry_or_quit(self) -> None:
        loader = (ROOT / "code/platform/fnql_steam.cpp").read_text(encoding="utf-8")
        header = (ROOT / "code/qcommon/qcommon.h").read_text(encoding="utf-8")
        windows = (ROOT / "code/win32/win_main.cpp").read_text(encoding="utf-8")
        unix = (ROOT / "code/unix/unix_main.cpp").read_text(encoding="utf-8")

        self.assertIn(
            "sysStartupChoice_t Sys_PromptStartupChoice( const char *title,"
            " const char *message );",
            header,
        )
        for source in (header, windows, unix):
            for choice in (
                "SYS_STARTUP_PROCEED",
                "SYS_STARTUP_RETRY",
                "SYS_STARTUP_QUIT",
            ):
                self.assertIn(choice, source)

        # Win32 uses the standard three-answer modal; a dialog that cannot be
        # built or shown falls through to proceed.
        self.assertIn("MB_CANCELTRYCONTINUE", windows)
        self.assertIn("case IDTRYAGAIN:", windows)
        self.assertIn("case IDCANCEL:", windows)
        self.assertEqual(windows.count("return SYS_STARTUP_PROCEED;"), 4)

        # Unix never waits for an answer that cannot arrive.
        self.assertIn(
            "if ( ttycon_on || isatty( STDIN_FILENO ) != 1"
            " || isatty( STDOUT_FILENO ) != 1 )",
            unix,
        )

        # Retry repeats the whole load-and-start sequence, quit exits through the
        # ordinary shutdown path, and proceed keeps the non-Steam fallback.
        self.assertIn("void ReleaseProviderForRetry()", loader)
        self.assertIn("bool ResolveMissingSteamLogin(bool offerChoice)", loader)
        self.assertIn("choice == SYS_STARTUP_RETRY", loader)
        self.assertIn("choice == SYS_STARTUP_QUIT", loader)
        self.assertIn("Com_Quit_f();", loader)
        self.assertIn("Continuing without a signed-in Steam account.", loader)
        self.assertIn(
            "while (StartSteamProvider(roles, enabled, providerName->string,", loader
        )
        self.assertIn("if (!ResolveMissingSteamLogin(offerLoginChoice))", loader)

        # The notification is startup-only, client-only, and never blocks
        # deterministic build-script runs.
        self.assertIn('Cvar_Get("com_steamLoginPrompt", "1"', loader)
        offer = loader.index("const bool offerLoginChoice")
        gate = loader[offer : loader.index(";", offer)]
        self.assertIn("(roles & FNQL_STEAM_ROLE_CLIENT) != 0", gate)
        self.assertIn("loginPrompt->integer != 0", gate)
        self.assertIn("!state.loginPromptOffered", gate)
        self.assertIn('!Cvar_VariableIntegerValue("com_buildScript")', gate)

    def test_opt_in_failures_announce_non_steam_fallback_once(self) -> None:
        source = (ROOT / "code/platform/fnql_steam.cpp").read_text(encoding="utf-8")
        self.assertIn("void EnterNonSteamFallback", source)
        self.assertIn("bool fallbackAnnounced{};", source)
        self.assertIn("if (!state.fallbackAnnounced)", source)
        self.assertIn("FnQL is continuing in non-Steam mode", source)
        self.assertIn('EnterNonSteamFallback("unavailable"', source)
        self.assertIn('EnterNonSteamFallback("incompatible"', source)
        self.assertIn('EnterNonSteamFallback("no-capabilities"', source)
        disabled = source[source.index("if (!enabled->integer)") :]
        disabled = disabled[: disabled.index("BuildCandidatePath")]
        self.assertNotIn("EnterNonSteamFallback", disabled)

    def test_filesystem_and_steam_install_discovery_do_not_depend_on_provider(self) -> None:
        files = (ROOT / "code/qcommon/files.c").read_text(encoding="utf-8")
        windows = (ROOT / "code/win32/win_shared.cpp").read_text(encoding="utf-8")
        unix = (ROOT / "code/unix/unix_shared.cpp").read_text(encoding="utf-8")
        self.assertNotIn("FNQL_Steam_", files)
        self.assertNotIn("fnql_steam", files)
        self.assertIn("Sys_SteamPath", windows)
        self.assertIn("Sys_SteamPath", unix)

    def test_webui_routes_provider_services_with_legacy_fallbacks(self) -> None:
        source = (ROOT / "code/client/cl_webui.cpp").read_text(encoding="utf-8")
        for call in (
            "FNQL_Steam_IsSubscribedApp",
            "FNQL_Steam_OpenOverlayUrl",
            "FNQL_Steam_RequestServers",
            "FNQL_Steam_CreateLobby",
            "FNQL_Steam_JoinLobby",
            "FNQL_Steam_GetSubscribedItems",
            "FNQL_Steam_GetItemState",
            "FNQL_Steam_RequestClientUserStats",
            "FNQL_Steam_GetItemDownloadInfo",
            "FNQL_Steam_GetAvatarRGBA",
            "FNQL_Steam_GetFriends",
            "FNQL_Steam_GetFriend",
            "FNQL_Steam_RequestUgcQuery",
            "FNQL_Steam_GetUgcQueryResults",
        ):
            self.assertIn(call, source)
        self.assertIn("CL_WebHost_SteamBridgeUnavailable", source)
        self.assertIn('"servers.details.%s.response"', source)
        self.assertNotIn('"servers.refresh.response"', source)
        self.assertIn("sizeOnDisk", source)
        self.assertIn("timestamp", source)
        self.assertIn("FNQL_STEAM_EVENT_LOBBY_CHAT_MESSAGE", source)
        self.assertIn('"lobby.%llu.chat"', source)
        self.assertIn("FNQL_STEAM_EVENT_UGC_ITEM_INSTALLED", source)
        self.assertIn('"web.ugc.item.installed"', source)
        for event in (
            "FNQL_STEAM_EVENT_LOBBY_CHAT_UPDATE",
            "FNQL_STEAM_EVENT_LOBBY_DATA_UPDATED",
            "FNQL_STEAM_EVENT_LOBBY_GAME_CREATED",
            "FNQL_STEAM_EVENT_LOBBY_KICKED",
            "FNQL_STEAM_EVENT_LOBBY_JOIN_REQUESTED",
            "FNQL_STEAM_EVENT_AVATAR_IMAGE_LOADED",
            "FNQL_STEAM_EVENT_PERSONA_STATE_CHANGED",
            "FNQL_STEAM_EVENT_FRIEND_RICH_PRESENCE_UPDATED",
            "FNQL_STEAM_EVENT_UGC_QUERY_COMPLETE",
            "FNQL_STEAM_EVENT_MICROTRANSACTION_AUTHORIZATION",
        ):
            self.assertIn(event, source)

        self.assertIn('"microtxn.authorization"', source)
        self.assertIn(r'\"appid\":%u', source)
        self.assertIn(r'\"orderid\":\"%llu\"', source)
        self.assertIn(r'\"authorized\":%u', source)

    def test_client_stats_readback_keeps_roles_distinct_and_rebuilds_retail_event(self) -> None:
        api = (ROOT / "code/platform/fnql_steam_api.h").read_text(encoding="utf-8")
        loader = (ROOT / "code/platform/fnql_steam.cpp").read_text(encoding="utf-8")
        client = (ROOT / "code/client/cl_webui.cpp").read_text(encoding="utf-8")
        contract = (ROOT / "code/platform/fnql_steam_stats.hpp").read_text(encoding="utf-8")
        self.assertIn("FNQL_STEAM_CAP_CLIENT_STATS", api)
        self.assertIn("request_client_user_stats", api)
        self.assertIn("get_client_user_achievement", api)
        self.assertIn("FNQL_Steam_RequestClientUserStats", loader)
        self.assertIn("FNQL_Steam_GetClientUserStatI32", client)
        self.assertIn("FNQL_Steam_GetClientUserAchievement", client)
        self.assertIn('"users.stats.%llu.received"', client)
        self.assertIn('\\"STATS\\":{', client)
        self.assertIn('\\"ACHIEVEMENTS\\":{', client)
        self.assertIn('Cmd_AddCommand( "stats_clear"', client)
        self.assertIn("std::array<std::string_view, 88>", contract)
        self.assertIn("std::array<std::string_view, 59>", contract)

    def test_lobby_projection_is_bounded_and_rebuilds_retail_enter_event(self) -> None:
        api = (ROOT / "code/platform/fnql_steam_api.h").read_text(encoding="utf-8")
        loader = (ROOT / "code/platform/fnql_steam.cpp").read_text(encoding="utf-8")
        client = (ROOT / "code/client/cl_webui.cpp").read_text(encoding="utf-8")
        self.assertIn("FNQL_STEAM_CAP_LOBBY_SNAPSHOT", api)
        self.assertIn("fnqlSteamLobbyMember_t", api)
        self.assertIn("fnqlSteamLobbyData_t", api)
        self.assertIn("FNQL_Steam_GetLobbyMembers", loader)
        self.assertIn("FNQL_Steam_GetLobbyData", loader)
        self.assertIn("CL_Steam_PublishLobbyEnter", client)
        self.assertIn('"lobby.%llu.enter"', client)
        self.assertIn('\\"is_owner\\":%s', client)
        self.assertIn('\\"lobbydata\\":{', client)
        self.assertIn('\\"players\\":{', client)
        self.assertIn("FNQL_Steam_InviteToGame", client)
        self.assertIn("CL_Steam_GetCurrentServerP2PIdentity", client)

    def test_gameserver_udp_bridge_is_bounded_and_does_not_replace_game_packets(self) -> None:
        api = (ROOT / "code/platform/fnql_steam_api.h").read_text(encoding="utf-8")
        loader = (ROOT / "code/platform/fnql_steam.cpp").read_text(encoding="utf-8")
        platform = (ROOT / "code/server/sv_platform.cpp").read_text(encoding="utf-8")
        server = (ROOT / "code/server/sv_main.cpp").read_text(encoding="utf-8")
        self.assertIn("FNQL_STEAM_CAP_GAME_SERVER_PACKET_IO", api)
        self.assertIn("handle_game_server_packet", api)
        self.assertIn("get_game_server_packet", api)
        self.assertIn("FNQL_Steam_HandleGameServerPacket", loader)
        self.assertIn("SV_SteamHandleIncomingPacket", platform)
        self.assertIn("SV_SteamDrainGameServerPackets", platform)
        self.assertIn("QL_STEAM_MAX_PACKETS_PER_FRAME", platform)
        packet_hook = server[server.index("void SV_PacketEvent"):]
        self.assertLess(packet_hook.index("SV_SteamHandleIncomingPacket"),
                        packet_hook.index("SV_ConnectionlessPacket"))

    def test_gameserver_metadata_publishes_serverinfo_scores_and_players(self) -> None:
        api = (ROOT / "code/platform/fnql_steam_api.h").read_text(encoding="utf-8")
        loader = (ROOT / "code/platform/fnql_steam.cpp").read_text(encoding="utf-8")
        platform = (ROOT / "code/server/sv_platform.cpp").read_text(encoding="utf-8")
        self.assertIn("FNQL_STEAM_CAP_GAME_SERVER_METADATA", api)
        self.assertIn("set_game_server_key_value", api)
        self.assertIn("update_game_server_user", api)
        self.assertIn("create_unauthenticated_user", api)
        self.assertIn("FNQL_Steam_SetGameServerKeyValue", loader)
        self.assertIn("PublishSteamGameServerInfo", platform)
        self.assertIn("Info_NextPair", platform)
        self.assertIn('"g_redScore"', platform)
        self.assertIn('"g_blueScore"', platform)
        self.assertIn("FNQL_Steam_UpdateGameServerUser", platform)
        self.assertIn("FNQL_Steam_SetGameServerBotCount", platform)
        self.assertIn("FNQL_Steam_CreateUnauthenticatedUser", platform)
        self.assertIn("playerState->persistant[PERS_SCORE]", platform)

    def test_favorites_and_server_details_keep_native_owners_and_fallbacks(self) -> None:
        api = (ROOT / "code/platform/fnql_steam_api.h").read_text(encoding="utf-8")
        loader = (ROOT / "code/platform/fnql_steam.cpp").read_text(encoding="utf-8")
        client = (ROOT / "code/client/cl_webui.cpp").read_text(encoding="utf-8")
        self.assertIn("FNQL_STEAM_CAP_FAVORITES", api)
        self.assertIn("FNQL_STEAM_CAP_SERVER_DETAILS", api)
        self.assertIn("set_favorite_server", api)
        self.assertIn("request_server_details", api)
        self.assertIn("cancel_server_details", api)
        self.assertIn("FNQL_Steam_SetFavoriteServer", loader)
        self.assertIn("FNQL_Steam_RequestServerDetails", loader)
        self.assertIn("FNQL_Steam_CancelServerDetails", loader)
        self.assertIn("CL_WebHost_PublishSteamServerDetailResponse", client)
        self.assertIn("FNQL_STEAM_EVENT_SERVER_DETAIL_PLAYER", client)
        self.assertIn("FNQL_STEAM_EVENT_SERVER_DETAIL_RULE", client)
        self.assertIn("NET_AdrToStringwPort", client)
        self.assertIn('"serverstatus %s\\n"', client)

    def test_p2p_transport_is_role_aware_and_peer_admission_is_authenticated(self) -> None:
        api = (ROOT / "code/platform/fnql_steam_api.h").read_text(encoding="utf-8")
        loader = (ROOT / "code/platform/fnql_steam.cpp").read_text(encoding="utf-8")
        client = (ROOT / "code/client/cl_webui.cpp").read_text(encoding="utf-8")
        server = (ROOT / "code/server/sv_platform.cpp").read_text(encoding="utf-8")
        self.assertIn("FNQL_STEAM_CAP_CLIENT_P2P", api)
        self.assertIn("FNQL_STEAM_CAP_GAME_SERVER_P2P", api)
        self.assertIn("FNQL_STEAM_CAP_VOICE", api)
        self.assertIn("FNQL_STEAM_EVENT_P2P_SESSION_REQUEST", api)
        self.assertIn("P2PCapability", loader)
        self.assertIn("role == FNQL_STEAM_ROLE_CLIENT", loader)
        self.assertIn("CL_Steam_GetCurrentServerP2PIdentity", client)
        self.assertIn("serverId != event->subject_id", client)
        self.assertIn("CL_IsVoiceSenderMuted", client)
        self.assertIn("S_AddVoiceSamples", client)
        self.assertIn("FNQL_Steam_SetLocalVoiceSpeaking", client)
        self.assertIn("QZ_Uncompress", client)
        self.assertIn('"game.stats.report"', client)
        self.assertIn("SteamAuthenticatedClient", server)
        self.assertIn("client.platformAuthValidated", server)
        self.assertIn("SV_GameShouldSuppressVoiceToClient", server)
        self.assertIn("QL_STEAM_MAX_PACKETS_PER_FRAME", server)
        self.assertIn("QL_STEAM_SERVER_ID_CONFIGSTRING = 0x2ca", server)

    def test_ugc_query_is_async_bounded_and_keeps_subscription_fallback(self) -> None:
        api = (ROOT / "code/platform/fnql_steam_api.h").read_text(encoding="utf-8")
        client = (ROOT / "code/client/cl_webui.cpp").read_text(encoding="utf-8")
        self.assertIn("FNQL_STEAM_CAP_UGC_QUERY", api)
        self.assertIn("FNQL_STEAM_UGC_DESCRIPTION_CAPACITY 8001u", api)
        self.assertIn("fnqlSteamUgcItem_t", api)
        self.assertIn("CL_Steam_ConsumeUgcQueryResults", client)
        self.assertIn('"web.ugc.results"', client)
        self.assertIn("CL_WebHost_PublishDynamicJsonEvent", client)
        self.assertIn("steam_ugc_query_test", client)
        capability_branch = client[client.index(
            "if ( FNQL_Steam_Available( FNQL_STEAM_CAP_UGC_QUERY ) )"
        ):]
        self.assertIn("FNQL_Steam_RequestUgcQuery", capability_branch)
        self.assertIn("FNQL_Steam_GetSubscribedItems", capability_branch)

    def test_avatar_bridge_uses_bounded_provider_pixels_and_every_renderer(self) -> None:
        public = (ROOT / "code/renderercommon/tr_public.h").read_text(encoding="utf-8")
        client = (ROOT / "code/client/cl_webui.cpp").read_text(encoding="utf-8")
        self.assertIn("(*RegisterShaderFromRGBA)", public)
        self.assertIn("required > 4u * 1024u * 1024u", client)
        self.assertIn("CL_ClearAvatarImageHandles", client)
        self.assertIn("CL_WebUI_EncodeAvatarPng", client)
        self.assertIn('"asset://steam/avatar/"', client)
        for path in (
            "code/renderer/tr_init.c",
            "code/renderervk/tr_init.c",
            "code/rendererrtx/tr_init.c",
        ):
            source = (ROOT / path).read_text(encoding="utf-8")
            self.assertIn("re.RegisterShaderFromRGBA = RE_RegisterShaderFromRGBA;", source)

    def test_avatar_cache_cannot_outlive_the_renderer_session_that_minted_it(self) -> None:
        # Every renderer teardown rebuilds tr.shaders from zero, so a cached
        # qhandle_t that survives one renders an unrelated shader as the avatar.
        main = (ROOT / "code/client/cl_main.cpp").read_text(encoding="utf-8")
        client = (ROOT / "code/client/cl_webui.cpp").read_text(encoding="utf-8")
        teardowns = main.count("cls.rendererStarted = qfalse;")
        self.assertEqual(teardowns, 2)
        self.assertEqual(
            main.count("CL_ClearAvatarImageHandles();"),
            teardowns,
            "every renderer teardown must drop the Steam avatar shader cache",
        )
        for teardown in range(teardowns):
            start = 0
            for _ in range(teardown + 1):
                start = main.index("cls.rendererStarted = qfalse;", start) + 1
            preceding = main[:start]
            self.assertGreater(
                preceding.count("CL_ClearAvatarImageHandles();"),
                teardown,
                "the cache must be cleared before the renderer is marked stopped",
            )
        # Second line of defence: entries record their renderer session and are
        # discarded on mismatch even if a future teardown path forgets to clear.
        self.assertIn("uint32_t session;", client)
        self.assertIn("entry.session != cl_steamAvatarSession", client)
        self.assertIn("target->session = cl_steamAvatarSession;", client)

    def test_avatar_transfers_reject_extents_that_changed_between_provider_calls(self) -> None:
        # Steam swaps a placeholder for the real portrait mid-download, so the
        # sizing call's extents cannot be trusted to describe the transfer.
        client = (ROOT / "code/client/cl_webui.cpp").read_text(encoding="utf-8")
        self.assertEqual(client.count("uint32_t loadedWidth = 0;"), 2)
        self.assertEqual(client.count("uint32_t loadedHeight = 0;"), 2)
        self.assertEqual(client.count("uint32_t loadedRequired = 0;"), 2)
        self.assertEqual(client.count("loadedWidth != width"), 1)
        self.assertEqual(client.count("loadedWidth == width"), 1)
        self.assertEqual(client.count("loadedRequired != required"), 1)
        self.assertEqual(client.count("loadedRequired == required"), 1)
        # RE_RegisterShaderFromImage returns the shader already bound to a name
        # and drops the new pixels, so no two attempts may share a name.
        self.assertIn("++cl_steamAvatarSerial", client)
        # One shared parser keeps the shader and browser bridges in agreement.
        self.assertEqual(client.count("static qboolean CL_Steam_ParseAvatarPath("), 2)
        self.assertEqual(client.count("CL_Steam_ParseAvatarPath( url"), 1)
        self.assertEqual(client.count("CL_Steam_ParseAvatarPath( path"), 1)
        # A pending avatar must not re-query the provider on every frame.
        self.assertIn("CL_STEAM_AVATAR_RETRY_MSEC", client)
        self.assertIn("cls.realtime < entry.retryTime", client)

    def test_friend_snapshot_is_bounded_and_keeps_server_identity_fallback(self) -> None:
        source = (ROOT / "code/client/cl_webui.cpp").read_text(encoding="utf-8")
        self.assertIn("CL_STEAM_MAX_FRIENDS 512u", source)
        self.assertIn("CL_WebHost_AppendSteamFriendJson", source)
        self.assertIn('"users.persona.%llu.change"', source)
        self.assertIn('"users.presence.%llu.change"', source)
        self.assertIn("for ( int i = 0; i < MAX_CLIENTS; ++i )", source)
        self.assertIn('"asset://steam/avatar/small/"', source)
        self.assertIn('"steam://avatar/medium/"', source)

    def test_private_sibling_is_only_an_explicit_development_dependency(self) -> None:
        script = (ROOT / ".vscode/build-release.ps1").read_text(encoding="utf-8")
        tasks = (ROOT / ".vscode/tasks.json").read_text(encoding="utf-8")
        meson = (ROOT / "meson.build").read_text(encoding="utf-8")
        self.assertIn("[switch]$WithSteam", script)
        self.assertIn("FNQL_STEAM_REPO", script)
        self.assertIn("Invoke-FnQLSteamBuild", script)
        self.assertIn("meson: build Win32 debug (Steam)", tasks)
        self.assertIn("-WithSteam", tasks)
        self.assertNotIn("FnQL-Steam", meson)
        self.assertNotIn("steam_api.dll", meson)


if __name__ == "__main__":
    unittest.main()
