"""Pin the engine side of the retail qagame query exports (native slots 12-18).

Retail quakelive_steam.exe consults these exports from four places; each
assertion names the retail address that motivates it. The game side of the
same contract (FnQL-game code/game/g_native.c) is gated by FnQL-game's G3
lockstep differential through the opt-in 'G' query record.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="replace")


def function_body(source: str, signature_pattern: str) -> str:
    match = re.search(signature_pattern + r"[^{;]*\{", source)
    if not match:
        raise AssertionError(f"Missing function matching {signature_pattern}")
    depth = 0
    for index in range(match.end() - 1, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.end() : index]
    raise AssertionError(f"Unterminated function matching {signature_pattern}")


class SnapshotVisibilityBridgeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.snapshot = read("code/server/sv_snapshot.cpp")

    def test_thaw_tick_answer_suppresses_before_any_forced_add(self) -> None:
        # 0x4e54a0-0x4e54a7: a non-zero slot-13 answer jumps past every add.
        body = function_body(self.snapshot, r"static SnapshotGameVisibility SV_GameSnapshotVisibility\(")
        suppress = body.index("SV_GameSuppressesThawProgressEvent( viewerClientNum, entityNum )")
        self.assertIn("return SnapshotGameVisibility::Suppressed;", body[suppress:body.index("MAX_CLIENTS")])
        self.assertLess(suppress, body.index("SV_GameCanClientSeeClient"))
        self.assertLess(suppress, body.index("SV_GameIsObjectiveEntity"))
        # 0x4e54b5: clients ask slot 12, everything else slot 14
        self.assertIn("if ( entityNum >= 0 && entityNum < MAX_CLIENTS ) {", body)
        # legacy QVM / classic DLL modules never see a QL-only export call
        self.assertLess(body.index("if ( !SV_GameQueryExportsAvailable() ) {"), suppress)

    def test_snapshot_loop_order_matches_retail(self) -> None:
        body = function_body(self.snapshot, r"static void SV_AddEntitiesVisibleFromPoint\(")
        broadcast = body.index("if ( ent->r.svFlags & SVF_BROADCAST ) {")
        game = body.index("switch ( SV_GameSnapshotVisibility( viewerClientNum, es->number ) ) {")
        area = body.index("if ( !SV_AreasVisibleForSnapshot( clientarea, svEnt ) ) {")
        # 0x4e5471 broadcast, 0x4e548c game exports, then the area/PVS pass
        self.assertLess(broadcast, game)
        self.assertLess(game, area)
        switch = body[game:area]
        self.assertIn("case SnapshotGameVisibility::Suppressed:\n\t\t\tcontinue;", switch)
        self.assertIn("case SnapshotGameVisibility::Forced:\n\t\t\teNums.Add( *svEnt, e );\n\t\t\tcontinue;", switch)
        # portals keep the same viewer
        self.assertIn("SV_AddEntitiesVisibleFromPoint( ent->s.origin2, frame, eNums, portal, viewerClientNum );", body)

    def test_viewer_is_the_snapshot_owner_slot(self) -> None:
        # 0x4e5789: the retail viewer argument is client->gentity's number,
        # not the followed ps.clientNum.
        build = function_body(self.snapshot, r"static void SV_BuildClientSnapshot\( client_t \*client \)")
        self.assertIn("cl = SV_ClientIndex( client );", build)
        self.assertIn("SV_AddEntitiesVisibleFromPoint( org, frame, entityNumbers, false, cl );", build)
        self.assertIn("SV_AddAudioReachableEntities( org, clientArea, frame, entityNumbers, cl );", build)

    def test_spectator_single_client_rule_is_native_only(self) -> None:
        # 0x4e53d2-0x4e53f6: SVF_SINGLECLIENT compares the owner slot while
        # ps.eFlags carries EF_SPECTATOR (0x4000).
        self.assertIn("constexpr int QL_EF_SPECTATOR = 0x00004000;", self.snapshot)
        body = function_body(self.snapshot, r"static int SV_SnapshotSingleClientNum\(")
        self.assertIn("SV_GameQueryExportsAvailable() && ( frame->ps.eFlags & QL_EF_SPECTATOR )", body)
        self.assertIn("return viewerClientNum;", body)
        self.assertIn("return frame->ps.clientNum;", body)
        flags = function_body(self.snapshot, r"static bool SV_EntityPassesSnapshotClientFlags\(")
        self.assertIn("ent->r.singleClient != singleClientNum", flags)
        # NOTSINGLECLIENT and the client mask keep ps.clientNum, as retail does
        self.assertIn("ent->r.singleClient == clientNum", flags)
        self.assertIn("1 << clientNum", flags)

    def test_audio_expansion_honours_game_suppression(self) -> None:
        body = function_body(self.snapshot, r"static void SV_AddAudioReachableEntities\(")
        self.assertIn("SV_GameSuppressesThawProgressEvent( viewerClientNum, es->number )", body)


class QueryExportHelperTests(unittest.TestCase):
    def test_every_helper_checks_its_slot(self) -> None:
        sv_game = read("code/server/sv_game.cpp")
        helpers = {
            "SV_GameCanClientSeeClient": "GAME_NATIVE_EXPORT_CAN_CLIENT_SEE_CLIENT",
            "SV_GameSuppressesThawProgressEvent": "GAME_NATIVE_EXPORT_FREEZE_CAN_SEE_THAW_PROGRESS_EVENT",
            "SV_GameIsObjectiveEntity": "GAME_NATIVE_EXPORT_IS_OBJECTIVE_ENTITY",
            "SV_GameShouldSuppressVoiceToClient": "GAME_NATIVE_EXPORT_SHOULD_SUPPRESS_VOICE_TO_CLIENT",
            "SV_GameIsClientAdmin": "GAME_NATIVE_EXPORT_IS_CLIENT_ADMIN",
            "SV_GameAreEnemyClients": "GAME_NATIVE_EXPORT_ARE_ENEMY_CLIENTS",
            "SV_GameGetClientScore": "GAME_NATIVE_EXPORT_GET_CLIENT_SCORE",
        }
        for helper, slot in helpers.items():
            body = function_body(sv_game, rf"\b{helper}\(")
            self.assertIn(f"if ( !SV_GameNativeExportAvailable( {slot} ) ) {{", body, helper)
        self.assertIn("return SV_QBool( gvm && gvm->dllExports );",
                      function_body(sv_game, r"qboolean SV_GameQueryExportsAvailable\( void \)"))


class AdminRconTests(unittest.TestCase):
    def setUp(self) -> None:
        self.client = read("code/server/sv_client.cpp")

    def test_rcon_is_a_client_command_gated_by_the_admin_export(self) -> None:
        # retail ucmd table 0x56bf68 -> 0x4dff80
        self.assertIn('{"rcon", SV_AdminRcon_f},', self.client)
        body = function_body(self.client, r"static void SV_AdminRcon_f\( client_t \*cl \)")
        argc = body.index("if ( Cmd_Argc() < 2 ) {")
        admin = body.index("if ( !SV_GameIsClientAdmin( clientNum ) ) {")
        self.assertLess(argc, admin)
        # never executed while the client's message is being parsed
        self.assertNotIn("Cmd_ExecuteString", body)
        self.assertIn("svPendingAdminCommands.size()", body)
        self.assertLess(admin, body.index("SV_RawTextAfterCommandToken( Cmd_Cmd() )"))

    def test_queue_runs_from_the_server_frame_with_redirected_output(self) -> None:
        body = function_body(self.client, r"void SV_ExecutePendingAdminCommands\( void \)")
        self.assertIn("svPendingAdminCommandCount = 0;", body)
        begin = body.index("Com_BeginRedirect(")
        self.assertLess(begin, body.index("Cmd_ExecuteString( command.text.data() );"))
        self.assertLess(body.index("Cmd_ExecuteString( command.text.data() );"), body.index("Com_EndRedirect();"))
        self.assertIn('"print\\n%s"', function_body(self.client, r"void SV_FlushAdminRedirect\("))
        frame = function_body(read("code/server/sv_main.cpp"), r"void SV_Frame\( int msec, int realMsec \)")
        self.assertIn("Zmq_PumpRcon();\n\tSV_ExecutePendingAdminCommands();", frame)
        shutdown = function_body(read("code/server/sv_init.cpp"), r"void SV_Shutdown\( const char \*finalmsg \)")
        self.assertIn("SV_ClearPendingAdminCommands();", shutdown)

    def test_raw_text_parser_keeps_the_command_quoting(self) -> None:
        body = function_body(self.client, r"const char \*SV_RawTextAfterCommandToken\( const char \*text \)")
        self.assertIn("if ( *text == '\"' ) {", body)


class SteamPlayerPublicationTests(unittest.TestCase):
    def test_players_publish_retail_name_and_export_score(self) -> None:
        # 0x4663cb-0x466440: "(Bot) " + userinfo name, score from export 18
        platform = read("code/server/sv_platform.cpp")
        body = function_body(platform, r"static void SV_SteamPublishGameServerMetadata\( void \)")
        self.assertIn('"(Bot) " : ""', body)
        self.assertIn('Info_ValueForKey( client.userinfo, "name" )', body)
        self.assertIn("SV_GameGetClientScore( index,\n\t\t\tplayerState->persistant[PERS_SCORE] )", body)
        self.assertIn("if ( client.state < CS_CONNECTED ) continue;", body)


if __name__ == "__main__":
    unittest.main()
