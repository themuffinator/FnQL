"""Source gates for the 2026-11-11 retail functional parity audit.

Each test pins one engine-owned behavior that retail Quake Live has and FnQL
lacked or got wrong. The retail evidence for every item is recorded in
docs/fnql/RETAIL_PARITY_GAPS.md and beside the code.
"""

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="replace")


def function_body(source: str, signature: str) -> str:
    # Skip forward declarations: take the occurrence whose next delimiter is
    # the opening brace of a body rather than a semicolon.
    start = source.index(signature)
    while True:
        after = start + len(signature)
        brace = source.index("{", after)
        semicolon = source.find(";", after)
        if semicolon == -1 or brace < semicolon:
            break
        start = source.index(signature, after)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace : index + 1]
    raise AssertionError(f"unterminated body for {signature}")


class ConsoleCommandParityTests(unittest.TestCase):
    def test_retail_console_commands_have_owners(self) -> None:
        cvar = read("code/qcommon/cvar.c")
        cmd = read("code/qcommon/cmd.c")
        common = read("code/qcommon/common.c")
        client = read("code/client/cl_main.cpp")
        webui = read("code/client/cl_webui.cpp")
        server = read("code/server/sv_ccmds.cpp")

        self.assertIn('Cmd_AddCommand( "cvarMult", Cvar_Mult_f );', cvar)
        self.assertIn('Cmd_AddCommand( "clearcvar", Cvar_Clear_f );', cvar)
        self.assertIn('Cmd_AddCommand( "listcvars", Cvar_List_f );', cvar)
        self.assertIn('Cmd_AddCommand( "listcmds", Cmd_List_f );', cmd)
        self.assertIn(
            'Cmd_AddCommand( "writeClientConfig", Com_WriteClientConfig_f );', common
        )
        self.assertIn(
            'Cmd_AddCommand( "postprocess_restart", CL_PostProcessRestart_f );', client
        )
        self.assertIn('Cmd_RemoveCommand( "postprocess_restart" );', client)
        self.assertIn('Cmd_AddCommand( "connect_lobby", CL_Steam_ConnectLobby_f );', webui)
        self.assertIn('Cmd_RemoveCommand( "connect_lobby" );', webui)
        self.assertIn('{ "arena", SV_Arena_f },', server)

    def test_clearcvar_and_cvarmult_use_the_ordinary_user_write_path(self) -> None:
        cvar = read("code/qcommon/cvar.c")
        clear = function_body(cvar, "static void Cvar_Clear_f( void )")
        self.assertIn('Cvar_Set2( Cmd_Argv( 1 ), "", qfalse );', clear)
        self.assertNotIn("qtrue", clear)
        mult = function_body(cvar, "static void Cvar_Mult_f( void )")
        self.assertIn("currentValue * amount", mult)
        self.assertIn('va( "%0.3f"', mult)
        self.assertIn("qfalse", mult)

    def test_postprocess_restart_is_owned_but_inert(self) -> None:
        client = read("code/client/cl_main.cpp")
        body = function_body(client, "static void CL_PostProcessRestart_f( void )")
        self.assertEqual(body.replace("\r", "").strip(), "{\n}")

    def test_write_client_config_uses_the_cloud_only_writer(self) -> None:
        common = read("code/qcommon/common.c")
        body = function_body(common, "static void Com_WriteClientConfig_f( void )")
        self.assertIn("Key_WriteBindings( f );", body)
        self.assertIn("Cmd_WriteAliases( f );", body)
        self.assertIn("Cvar_WriteQLConfigVariables( f, f, qtrue );", body)
        self.assertIn("FS_AllowedExtension", body)

    def test_arena_command_validates_names_and_drops_the_factory(self) -> None:
        server = read("code/server/sv_ccmds.cpp")
        body = function_body(server, "static void SV_Arena_f( void )")
        self.assertIn('"scripts/%s.sp_arena"', body)
        self.assertGreaterEqual(body.count("arena::IsSafeName"), 2)
        self.assertIn("arena::ParseScript", body)
        self.assertIn("SV_FactoryDeactivate();", body)
        self.assertIn('Cvar_Set( "g_botSpawnList"', body)
        self.assertLess(body.index("SV_SpawnServer"), body.index('Cvar_Set( "sv_cheats", "0" );'))


class ConsoleAndInputCvarParityTests(unittest.TestCase):
    def test_console_access_cvars_are_registered_with_retail_flags(self) -> None:
        console = read("code/client/cl_console.cpp")
        self.assertIn(
            'com_allowConsole = Cvar_Get( "com_allowConsole", "1",\n'
            "\t\tCVAR_ARCHIVE | CVAR_PROTECTED | CVAR_CLOUD );",
            console.replace("\r\n", "\n"),
        )
        self.assertIn(
            'con_height = Cvar_Get( "con_height", "0.5", CVAR_PROTECTED | CVAR_CLOUD );',
            console,
        )
        self.assertIn('Cvar_CheckRange( con_height, "0.1", "1", CV_FLOAT );', console)
        self.assertIn("con.finalFrac = con_height ? con_height->value : 0.5f;", console)

    def test_toggleconsole_command_is_gated_but_internal_toggles_are_not(self) -> None:
        console = read("code/client/cl_console.cpp")
        keys = read("code/client/cl_keys.cpp")
        self.assertIn(
            'Cmd_AddCommand( "toggleconsole", Con_ToggleConsoleCommand_f );', console
        )
        gate = function_body(console, "static void Con_ToggleConsoleCommand_f( void )")
        self.assertIn("ConsoleCommandMayToggle", gate)
        self.assertIn(
            'Com_DPrintf( "com_allowConsole won\'t allow toggleconsole command\\n" );', gate
        )
        self.assertIn("Con_ToggleConsole_f();", gate)

        down = function_body(keys, "static void CL_KeyDownEvent( int key, unsigned time )")
        self.assertIn("if ( Con_HotkeyMayToggle() ) {", down)
        # A refused console key runs the ordinary '`' binding, as retail does.
        self.assertIn("Key_ParseBinding( '`', qtrue, time );", down)
        up = function_body(keys, "static void CL_KeyUpEvent( int key, unsigned time )")
        self.assertIn("Key_ParseBinding( '`', qfalse, time );", up)
        # Escape still closes an open console without consulting the cvar.
        menu = function_body(keys, "static void CL_ToggleMenuInternal(")
        self.assertIn("Con_ToggleConsole_f();", menu)
        self.assertNotIn("Con_HotkeyMayToggle", menu)

    def test_userinfo_cvars_exist_before_cgame_loads(self) -> None:
        client = read("code/client/cl_main.cpp")
        for name, default in (("cg_autoAction", ""), ("cg_autoHop", "1")):
            with self.subTest(cvar=name):
                self.assertIn(
                    f'Cvar_Get( "{name}", "{default}", '
                    "CVAR_USERINFO | CVAR_ARCHIVE | CVAR_PROTECTED | CVAR_CLOUD );",
                    client,
                )

    def test_windows_mouse_mode_status_is_published(self) -> None:
        source = read("code/win32/win_input.cpp")
        self.assertIn(
            'Cvar_Get( "in_mouseMode", "undefined", CVAR_ROM | CVAR_TEMP )', source
        )
        for mode in ("win32", "DirectInput", "win32(Raw)"):
            with self.subTest(mode=mode):
                self.assertIn(f'IN_PublishMouseMode( "{mode}" );', source)


class AudioParityTests(unittest.TestCase):
    def test_background_mute_has_one_owner_honoring_the_retail_switch(self) -> None:
        main = read("code/client/audio/snd_main.cpp")
        self.assertIn('s_muteBackground = Cvar_Get( "s_muteBackground", "1", CVAR_ARCHIVE );', main)
        owner = function_body(main, 'extern "C" qboolean S_MutedByWindowState( void )')
        self.assertLess(
            owner.index("s_muteBackground"), owner.index("s_muteWhenMinimized")
        )
        self.assertIn("s_muteWhenUnfocused", owner)

        for path in (
            "code/client/audio/legacy/snd_dma.cpp",
            "code/client/audio/legacy/snd_mix.cpp",
            "code/client/audio/openal/AudioSystemBackend.inl",
        ):
            with self.subTest(path=path):
                source = read(path)
                self.assertIn("S_MutedByWindowState()", source)
                self.assertNotIn("s_muteWhenUnfocused", source)
                self.assertNotIn("s_muteWhenMinimized", source)

    def test_announcer_volume_is_engine_registered_and_cgame_applied(self) -> None:
        main = read("code/client/audio/snd_main.cpp")
        self.assertIn(
            's_announcerVolume = Cvar_Get( "s_announcerVolume", "1.0",\n'
            "\t\tCVAR_ARCHIVE | CVAR_PROTECTED | CVAR_CLOUD );",
            main.replace("\r\n", "\n"),
        )
        self.assertIn('Cvar_CheckRange( s_announcerVolume, "0", "2", CV_FLOAT );', main)
        # The lane cgame drives: import 40 carries the per-sound volume.
        public = read("code/cgame/cg_public.h")
        self.assertIn("CG_QL_IMPORT_S_STARTLOCALSOUND_VOLUME = 40,", public)
        cgame = read("code/client/cl_cgame.cpp")
        self.assertIn("S_StartLocalSoundVolume( sfx, channelNum, volume );", cgame)

    def test_settings_page_no_longer_hides_working_retail_rows(self) -> None:
        overlay = read("code/client/webui/fnql-settings.js")
        block = re.search(r"var RETAIL_UNSUPPORTED = \{(.*?)\n  \};", overlay, re.S)
        assert block is not None
        for name in (
            "com_allowconsole",
            "s_announcervolume",
            "s_killbeepvolume",
            "s_mutebackground",
            "s_ambient",
        ):
            with self.subTest(cvar=name):
                self.assertNotIn(name, block.group(1))

        webui = read("code/client/cl_webui.cpp")
        allow = re.search(
            r"static const char \*const configCvars\[\] = \{(.*?)\n\t\tNULL", webui, re.S
        )
        assert allow is not None
        for name in (
            "com_allowConsole",
            "s_muteBackground",
            "s_announcerVolume",
            "lobby_autoconnect",
            "web_skipLegals",
            "ui_bgcolor",
        ):
            with self.subTest(cvar=name):
                self.assertIn(f'"{name}"', allow.group(1))
        # cgame-owned settings travel with their retail defaults until the
        # module registers them.
        self.assertIn('{ "s_killBeepVolume", "1" },', webui)
        self.assertIn('{ "s_ambient", "1" },', webui)
        self.assertIn("cl_webModuleSettingCvars[i].value", webui)


class ModuleBridgeParityTests(unittest.TestCase):
    def test_import_192_marks_a_ragequit_not_the_pure_check(self) -> None:
        game = read("code/server/sv_game.cpp")
        body = function_body(game, "static void *QDECL QL_G_trap_MarkClientRageQuit( int clientNum )")
        self.assertIn("client.rageQuit = qtrue;", body)
        self.assertNotIn("gotCP", body)
        self.assertIn(
            "QL_BIND( G_QL_IMPORT_MARK_CLIENT_RAGEQUIT, QL_G_trap_MarkClientRageQuit );", game
        )
        # Only the cp command and the gamestate reset may touch the pure flag.
        self.assertNotIn("gotCP", game)

        client = read("code/server/sv_client.cpp")
        drop = function_body(client, "void SV_DropClient( client_t *drop, const char *reason )")
        self.assertIn("if ( drop->rageQuit ) {", drop)
        self.assertIn('S_COLOR_RED "rage" S_COLOR_WHITE "quits\\n\\""', drop)
        self.assertIn("drop->rageQuit = qfalse;", drop)

    def test_native_cgame_cvar_writes_use_the_ui_policy(self) -> None:
        cgame = read("code/client/cl_cgame.cpp")
        case = cgame[cgame.index("case CG_CVAR_SET:") : cgame.index("case CG_CVAR_VARIABLESTRINGBUFFER:")]
        self.assertIn("if ( cgvm->dllExports ) {", case)
        self.assertIn("Cvar_SetFromUI( VMA(1), VMA(2) );", case)
        # A legacy VM cgame keeps the stricter rule.
        self.assertIn("Cvar_SetSafe( VMA(1), VMA(2) );", case)
        value = function_body(
            cgame, "static void QDECL QL_CG_trap_Cvar_SetValue( const char *varName, float value )"
        )
        self.assertIn("Cvar_SetValueFromUI( varName, value );", value)

    def test_steamid_query_answers_zero_for_bots(self) -> None:
        game = read("code/server/sv_game.cpp")
        body = function_body(game, "static std::uint64_t QDECL QL_G_trap_GetSteamId64( int clientNum )")
        self.assertIn("NA_BOT", body)
        self.assertIn('"characterfile"', body)
        self.assertLess(body.index("return 0;"), body.index("SV_ClientSteamId"))


class ServerParityTests(unittest.TestCase):
    def test_server_type_gates_packets_and_challenges(self) -> None:
        main = read("code/server/sv_main.cpp")
        packet = function_body(main, "void SV_PacketEvent( const netadr_t *from, msg_t *msg )")
        gate = packet.index("ServerTypeAcceptsPacket")
        # Nothing, the Steam packet bridge included, sees a refused packet.
        self.assertLess(gate, packet.index("SV_SteamHandleIncomingPacket"))
        self.assertLess(gate, packet.index("SV_ConnectionlessPacket"))
        self.assertIn("from->type == NA_LOOPBACK", packet)

        client = read("code/server/sv_client.cpp")
        challenge = function_body(client, "void SV_GetChallenge( const netadr_t *from, const msg_t *msg )")
        self.assertIn("ServerTypeAcceptsChallenge", challenge)
        self.assertIn("kLanOnlyChallengeReply", challenge)
        # The refusal is sent only after the amplification limiter.
        self.assertLess(
            challenge.index("SVC_RateLimitAddress"), challenge.index("ServerTypeAcceptsChallenge")
        )
        self.assertLess(
            challenge.index("ServerTypeAcceptsChallenge"), challenge.index("SV_CreateChallenge")
        )

        init = read("code/server/sv_init.cpp")
        self.assertIn('sv_serverType = Cvar_Get( "sv_serverType", "0", CVAR_ARCHIVE );', init)
        self.assertIn("sv_serverType is 0 (offline)", init)

    def test_stray_sequenced_packets_get_a_rate_limited_disconnect(self) -> None:
        main = read("code/server/sv_main.cpp")
        packet = function_body(main, "void SV_PacketEvent( const netadr_t *from, msg_t *msg )")
        self.assertIn("knownSender = true;", packet)
        self.assertIn(
            "if ( !knownSender && !SVC_RateLimitAddress( from, 4, 1000 ) ) {", packet
        )
        self.assertIn('NET_OutOfBandPrint( NS_SERVER, from, "disconnect" );', packet)
        # A dead server stays silent.
        self.assertLess(packet.index("sv.state == SS_DEAD"), packet.index('"disconnect"'))

    def test_steam_game_server_identity_follows_retail(self) -> None:
        platform = read("code/server/sv_platform.cpp")
        config = function_body(
            platform, "fnqlSteamGameServerConfig_t BuildSteamGameServerConfig( const char *mapName )"
        )
        self.assertIn('Cvar_VariableIntegerValue( "g_needpass" ) != 0', config)
        self.assertNotIn("sv_privatePassword", config)
        self.assertIn("kRetailSteamServerVersion", config)
        self.assertIn("kRetailSteamServerProduct", config)
        self.assertIn("SteamGameDescription", config)
        self.assertIn("SV_FactoryActiveTags( factoryTags, sizeof( factoryTags ) );", config)

    def test_steam_rules_follow_serverinfo_changes(self) -> None:
        main = read("code/server/sv_main.cpp")
        block = main[main.index("if ( cvar_modifiedFlags & CVAR_SERVERINFO ) {") :]
        block = block[: block.index("}")]
        self.assertIn("SV_SteamPublishServerInfo();", block)
        self.assertIn("SV_SetConfigstring( CS_SERVERINFO", block)
        platform = read("code/server/sv_platform.cpp")
        body = function_body(platform, "void SV_SteamPublishServerInfo( void )")
        self.assertIn("PublishSteamGameServerInfo();", body)

    def test_status_prints_the_retail_table_for_native_modules(self) -> None:
        server = read("code/server/sv_ccmds.cpp")
        table = function_body(server, "static void SV_StatusRetail( void )")
        self.assertIn(
            '"num score ping name            lastmsg address               qport rate  steamid\\n"',
            table,
        )
        self.assertIn(
            '"--- ----- ---- --------------- ------- --------------------- ----- ----- -----------------\\n"',
            table,
        )
        for fragment in ('"CNCT "', '"ZMBI "', '"%7i "', '"%5i"', '" %5i"', '" %llu"', "9999"):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, table)
        status = function_body(server, "static void SV_Status_f( void )")
        self.assertIn("if ( SV_GameQueryExportsAvailable() ) {", status)
        self.assertIn("SV_StatusRetail();", status)
        # The Quake III table stays for classic modules.
        self.assertIn('"cl score ping name"', status)

    def test_console_say_and_kick_follow_retail(self) -> None:
        server = read("code/server/sv_ccmds.cpp")
        say = function_body(server, "static void SV_ConSay_f( void )")
        self.assertIn('"%i Server: %s", MAX_CLIENTS - 1, p', say)
        self.assertIn("SV_GameQueryExportsAvailable()", say)
        self.assertIn('"console: %s"', say)  # classic modules keep the Quake III form
        kick = function_body(server, "static void SV_Kick_f( void )")
        self.assertIn('"was kicked: %s"', kick)
        self.assertIn("strlen( Cmd_Argv( 2 ) ) < 128", kick)
        self.assertIn("SV_DropClient( cl, kickMessage );", kick)


class RetailJoinPathTests(unittest.TestCase):
    """A retail client joining an FnQL server, and FnQL joining a retail one."""

    def test_message_limit_is_retails_and_legacy_peers_keep_their_own(self) -> None:
        qcommon = read("code/qcommon/qcommon.h")
        self.assertRegex(qcommon, r"#define\s+MAX_MSGLEN\s+32768\b")
        self.assertRegex(qcommon, r"#define\s+MAX_MSGLEN_LEGACY\s+16384\b")
        contract = read("code/qcommon/protocol_contract.hpp")
        limits = contract[contract.index("inline constexpr Limits QuakeLiveLimits{") :]
        self.assertIn("1'400u, 32'768u,", limits[: limits.index("};")])
        modern = contract[contract.index("inline constexpr Limits ModernizedQ3Limits{") :]
        self.assertIn("1'400u, 16'384u,", modern[: modern.index("};")])

        client = read("code/server/sv_client.cpp")
        limit = function_body(client, "int SV_ClientMessageLimit( const client_t *client )")
        self.assertIn("client->netchan.wireProfile ).limits.messageBytes", limit)
        gamestate = function_body(client, "static void SV_SendClientGameState( client_t *client )")
        self.assertIn(
            "MSG_Init( &msg, msgBuffer.data(), SV_ClientMessageLimit( client ) );", gamestate
        )
        snapshot = read("code/server/sv_snapshot.cpp")
        self.assertIn(
            "MSG_Init( &msg, msg_buf.data(), SV_ClientMessageLimit( client ) );", snapshot
        )
        remaining = function_body(client, "int SV_RemainingGameState( void )")
        self.assertIn("fnql::protocol::Find( com_protocol->integer )", remaining)
        self.assertIn("return messageLimit - len;", remaining)

    def test_listen_servers_answer_remote_requests_for_quake_live_modules(self) -> None:
        main = read("code/server/sv_main.cpp")
        rule = function_body(main, "qboolean SV_SinglePlayerIgnoresRemote( void )")
        self.assertLess(
            rule.index("SV_GameQueryExportsAvailable()"), rule.index('"ui_singlePlayerActive"')
        )
        self.assertIn("return qfalse;", rule[: rule.index('"g_gametype"')])
        client = read("code/server/sv_client.cpp")
        challenge = function_body(client, "void SV_GetChallenge( const netadr_t *from, const msg_t *msg )")
        self.assertIn("if ( SV_SinglePlayerIgnoresRemote() ) {", challenge)
        self.assertNotIn("GT_SINGLE_PLAYER", challenge)
        # The Quake III test survives only inside the shared rule.
        self.assertEqual(main.count('"ui_singlePlayerActive"'), 1)
        self.assertEqual(main.count("SV_SinglePlayerIgnoresRemote() ) {"), 2)

    def test_a_repeated_connect_is_answered_without_tearing_the_slot_down(self) -> None:
        client = read("code/server/sv_client.cpp")
        resend = client.index("repeated connect, resent connectResponse")
        block = client[client.rindex("if ( cl_proto == QL_RETAIL_PROTOCOL_VERSION ) {", 0, resend) : resend]
        self.assertIn("client.state != CS_CONNECTED || client.challenge != challenge", block)
        self.assertIn("client.netchan.qport != qport", block)
        self.assertIn('NET_OutOfBandPrint( NS_SERVER, from, "connectResponse" );', block)
        self.assertNotIn("GAME_CLIENT_DISCONNECT", block)
        self.assertNotIn("SV_FreeClient", block)
        # It runs before the reconnect limit and the slot reuse scan.
        self.assertLess(resend, client.index("// quick reject"))
        # ...and only after the challenge itself was verified.
        self.assertLess(client.index("SV_VerifyChallenge( challenge, from )"), resend)

    def test_loading_clients_receive_only_chat_broadcasts(self) -> None:
        main = read("code/server/sv_main.cpp")
        loop = main[main.index("// send the data to all relevant clients") :]
        loop = loop[: loop.index("MASTER SERVER FUNCTIONS")]
        self.assertIn("client.state == CS_PRIMED", loop)
        self.assertIn("client.netchan.wireProfile == NETCHAN_WIRE_QL_RETAIL", loop)
        self.assertIn('strncmp( message.data(), "chat ", 5 ) != 0', loop)
        self.assertLess(loop.index("continue;"), loop.index("SV_AddServerCommand( &client"))

    def test_shutdown_and_pure_messages_use_retail_wording(self) -> None:
        init = read("code/server/sv_init.cpp")
        final = init[init.index("// send it twice, ignoring rate") :]
        final = final[: final.index("// force a snapshot to be sent")]
        retail = final[: final.index("} else {")]
        self.assertIn("client.netchan.wireProfile == NETCHAN_WIRE_QL_RETAIL", retail)
        self.assertIn('SV_SendServerCommand( &client, "print \\"%s\\"", message );', retail)
        self.assertIn('SV_SendServerCommand( &client, "disconnect" );', retail)
        self.assertIn('"disconnect \\"%s\\""', final[final.index("} else {") :])
        client = read("code/server/sv_client.cpp")
        self.assertIn('"Unpure client detected. Invalid .PK3 files referenced!"', client)

    def test_client_does_not_ask_a_retail_server_for_paks(self) -> None:
        client = read("code/client/cl_main.cpp")
        downloads = function_body(client, "static void CL_InitLegacyDownloads( void )")
        retail = downloads[: downloads.index("else if ( !(cl_allowDownload->integer & DLF_ENABLE) )")]
        self.assertIn("clc.netchan.wireProfile == NETCHAN_WIRE_QL_RETAIL", retail)
        self.assertIn("!( clc.sv_allowDownload & DLF_ENABLE )", retail)
        self.assertIn("WARNING: Missing pk3s referenced by the server:", retail)
        self.assertNotIn("CL_NextDownload", retail)
        # Either way the join continues.
        self.assertIn("CL_DownloadsComplete();", downloads)

    def test_client_paces_packets_during_workshop_downloads(self) -> None:
        source = read("code/client/cl_input.cpp")
        self.assertIn("|| CL_WorkshopDownloadsActive() != qfalse;", source)
        self.assertIn("if ( downloading && cls.realtime - clc.lastPacketSentTime < 50 ) {", source)
        self.assertIn("\t\t!downloading &&\n", source.replace("\r\n", "\n"))

    def test_client_userinfo_uses_retail_colors_and_teamtask(self) -> None:
        client = read("code/client/cl_main.cpp").replace("\r\n", "\n")
        for name, default in (("color1", "7"), ("color2", "25")):
            with self.subTest(cvar=name):
                self.assertIn(
                    f'Cvar_CheckRange( Cvar_Get( "{name}", "{default}", '
                    "CVAR_USERINFO | CVAR_ARCHIVE | CVAR_PROTECTED | CVAR_CLOUD ),\n"
                    '\t\t"1", "26", CV_INTEGER );',
                    client,
                )
        self.assertIn('Cvar_Get( "teamtask", "0", CVAR_USERINFO );', client)


class WebUiBridgeParityTests(unittest.TestCase):
    def test_lobby_events_carry_the_retail_topics_and_fields(self) -> None:
        webui = read("code/client/cl_webui.cpp")
        self.assertIn('"lobby.%llu.left"', webui)
        self.assertIn('\\"num_players\\":%u,\\"max_players\\":%u', webui)
        entered = webui[
            webui.index("case FNQL_STEAM_EVENT_LOBBY_ENTERED:") : webui.index(
                "case FNQL_STEAM_EVENT_LOBBY_LEFT:"
            )
        ]
        self.assertIn('CL_WebView_PublishEvent( "lobby.error", payload );', entered)
        self.assertIn('\\"code\\":%d,\\"id\\":\\"%llu\\",\\"message\\"', entered)

    def test_lobby_autoconnect_is_registered_and_set_by_connect_lobby(self) -> None:
        webui = read("code/client/cl_webui.cpp")
        self.assertIn('Cvar_Get( "lobby_autoconnect", "", CVAR_TEMP )', webui)
        body = function_body(webui, "static void CL_Steam_ConnectLobby_f( void )")
        self.assertIn('Cvar_Set( "lobby_autoconnect", Cmd_Argv( 1 ) );', body)

    def test_browser_writes_skip_cheat_protected_cvars(self) -> None:
        webui = read("code/client/cl_webui.cpp")
        guard = function_body(webui, "static qboolean CL_WebHost_BrowserMayWriteCvar( const char *name )")
        self.assertIn("if ( flags & CVAR_CHEAT ) {", guard)
        self.assertLess(guard.index("CVAR_CHEAT"), guard.index("CVAR_PRIVATE | CVAR_ROM"))


if __name__ == "__main__":
    unittest.main()
