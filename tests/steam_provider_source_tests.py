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
