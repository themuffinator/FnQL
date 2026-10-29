"""Guard the dedicated-server lockstep tooling (docs/fnql/SERVER_LOCKSTEP.md).

sv_lockstep exists for FnQL-game's full-server differential (gate G3). It must
stay inert unless a dedicated server is started with `+set sv_lockstep 1`:
the cvars default to off and are command-line only, the enable latch requires
a dedicated server, and every hook outside sv_lockstep.cpp is a guarded call
whose disabled branch is the original code path.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LOCKSTEP = "code/server/sv_lockstep.cpp"


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


class ServerLockstepDefaultsTests(unittest.TestCase):
    def test_cvars_default_off_and_are_command_line_only(self) -> None:
        source = read(LOCKSTEP)
        expected = {
            "sv_lockstep": "0",
            "sv_lockstepSeed": "1",
            "sv_lockstepScript": "",
            "sv_lockstepRecord": "",
            "sv_lockstepFrames": "0",
        }
        for name, default in expected.items():
            self.assertRegex(
                source,
                rf'{name} = Cvar_Get\( "{name}", "{re.escape(default)}", CVAR_INIT \| CVAR_PROTECTED \);',
                name,
            )

    def test_enable_latch_requires_a_dedicated_server(self) -> None:
        body = function_body(read(LOCKSTEP), r"void SV_LockstepInit\( void \)")
        off = body.index("if ( !sv_lockstep->integer ) {")
        dedicated = body.index("if ( !com_dedicated || !com_dedicated->integer ) {")
        enabled = body.index("lockstepEnabled = true;")
        self.assertLess(off, dedicated)
        self.assertLess(dedicated, enabled)
        self.assertIn("sv_lockstep is ignored", body)
        # the latch is only ever set here
        self.assertEqual(read(LOCKSTEP).count("lockstepEnabled = true;"), 1)
        self.assertIn("bool lockstepEnabled = false;", read(LOCKSTEP))

    def test_interface_is_inert_while_disabled(self) -> None:
        source = read(LOCKSTEP)
        self.assertIn(
            "return lockstepEnabled ? qtrue : qfalse;",
            function_body(source, r"qboolean SV_LockstepActive\( void \)"),
        )
        frame_msec = function_body(source, r"int SV_LockstepFrameMsec\( void \)")
        self.assertIn("if ( !lockstepEnabled || !sv_fps || sv_fps->integer <= 0 ) {", frame_msec)
        self.assertIn("return 0;", frame_msec)
        for name in ("void SV_LockstepBeginFrame", "void SV_LockstepShutdown"):
            body = function_body(source, re.escape(name) + r"\( [^)]*\)")
            self.assertRegex(body.lstrip(), r"^(\(void\)reason;\s*)?if \( !lockstepEnabled", name)
        for name in ("void SV_LockstepClientThink", "void SV_LockstepEndFrame",
                     "void SV_LockstepRecordServerCommand"):
            body = function_body(source, re.escape(name) + r"\( [^)]*\)")
            self.assertTrue(body.lstrip().startswith("if ( !Running()"), name)
        self.assertIn("return lockstepEnabled && session.started && !session.finished;",
                      function_body(source, r"bool Running\(\)"))
        scripted = function_body(source, r"int ScriptIndexForClient\( const client_t \*client \)")
        self.assertIn("if ( !lockstepEnabled || !session.started || !client || !svs.clients ) {", scripted)
        self.assertIn("client->netchan.remoteAddress.type != NA_BAD", scripted)


class ServerLockstepHookTests(unittest.TestCase):
    def test_common_frame_keeps_the_paced_loop_when_off(self) -> None:
        common = read("code/qcommon/common.c")
        frame = function_body(common, r"void Com_Frame\( qboolean noDelay \)")
        self.assertIn("lockstepMsec = SV_LockstepFrameMsec();", frame)
        self.assertIn("if ( lockstepMsec > 0 ) {\n\t\t// never sleep", frame.replace("\r\n", "\n"))
        self.assertIn("} else\n\tif ( noDelay == qfalse )\n\tdo {", frame.replace("\r\n", "\n"))
        self.assertIn("gameMsec = Com_ModifyMsec( realMsec );", frame)
        self.assertRegex(frame, r"if \( lockstepMsec > 0 \) \{\s*gameMsec = lockstepMsec;\s*\} else \{")
        init = function_body(common, r"void Com_FrameInit\( void \)")
        self.assertIn("if ( SV_LockstepFrameMsec() > 0 ) {", init)
        self.assertTrue(init.rstrip().endswith("lastTime = com_frameTime = Com_Milliseconds();"))

    def test_server_frame_hooks_are_gated(self) -> None:
        main = read("code/server/sv_main.cpp")
        frame = function_body(main, r"void SV_Frame\( int msec, int realMsec \)")
        self.assertIn("const bool lockstep = SV_LockstepActive() != qfalse;", frame)
        self.assertRegex(frame, r"\} else \{\s*sv\.timeResidual \+= msec;\s*\}")
        self.assertRegex(frame, r"if \( lockstep \) \{\s*SV_LockstepClientThink\(\);\s*\}")
        self.assertRegex(frame, r"if \( lockstep \) \{\s*SV_LockstepEndFrame\(\);\s*\}")
        self.assertIn("VM_Call( gvm, 1, GAME_RUN_FRAME, sv.time );", frame)
        self.assertEqual(frame.count("SV_LockstepBeginFrame();"), 1)
        pings = function_body(main, r"static void SV_CalcPings\( void \)")
        self.assertIn("if ( SV_LockstepClientPing( &client, &client.ping ) ) {", pings)

    def test_game_imports_fall_back_to_the_original_sources(self) -> None:
        game = read("code/server/sv_game.cpp")
        self.assertIn(
            "const int randomSeed = SV_LockstepActive() ? SV_LockstepGameSeed() : Com_Milliseconds();",
            game,
        )
        self.assertIn("VM_Call( gvm, 3, GAME_INIT, sv.time, randomSeed, SV_QBool( restart ) );", game)
        self.assertIn(
            "return SV_LockstepActive() ? SV_LockstepMilliseconds() : Sys_Milliseconds();", game
        )
        self.assertIn(
            "return SV_LockstepActive() ? SV_LockstepRealTime( VMA(1) ) : Com_RealTime( VMA(1) );", game
        )
        guid = function_body(game, r"static void QDECL QL_G_trap_GenerateMatchGuid\( char \*buffer, int bufferSize \)")
        self.assertRegex(guid, r"if \( SV_LockstepActive\(\) \) \{\s*// [^\n]*\n\s*SV_LockstepMatchGuid\( buffer, bufferSize \);\s*return;\s*\}")
        self.assertTrue(guid.rstrip().endswith("SV_GenerateMatchGuid( buffer, bufferSize );"))
        send = function_body(game, r"static void SV_GameSendServerCommand\( int clientNum, const char \*text \)")
        self.assertIn("SV_LockstepRecordServerCommand( clientNum, text );", send)
        self.assertIn('SV_SendServerCommand( nullptr, "%s", text );', send)

    def test_level_seed_and_lifecycle_hooks(self) -> None:
        init = read("code/server/sv_init.cpp")
        self.assertRegex(
            init,
            r"if \( SV_LockstepActive\(\) \) \{\s*// [^\n]*\n\s*SV_LockstepSeedLevel\( &sv\.checksumFeed \);\s*\} else \{\s*"
            r"srand\( Com_Milliseconds\(\) \);\s*Com_RandomBytes\(",
        )
        self.assertIn("SV_LockstepInit();", function_body(init, r"void SV_Init\( void \)"))
        shutdown = function_body(init, r"void SV_Shutdown\( const char \*finalmsg \)")
        self.assertLess(shutdown.index("SV_LockstepShutdown( finalmsg );"), shutdown.index("SV_FinalMessage( finalmsg );"))

    def test_scripted_clients_get_no_network_traffic(self) -> None:
        snapshot = function_body(read("code/server/sv_snapshot.cpp"), r"void SV_SendClientMessages\( void \)")
        self.assertRegex(snapshot, r"if \( SV_LockstepIsScriptedClient\( &client \) \)\s*continue;")
        userinfo = function_body(read("code/server/sv_client.cpp"),
                                 r"void SV_UserinfoChanged\( client_t \*cl, qboolean updateUserinfo, qboolean runFilter \)")
        self.assertRegex(
            userinfo,
            r"if \( const char \*scriptedAddress = SV_LockstepClientAddress\( cl \) \)\s*ip = scriptedAddress;[^\n]*\n"
            r"\s*else if \( NET_IsLocalAddress\( &cl->netchan\.remoteAddress \) \)\s*ip = \"localhost\";\s*else\s*"
            r"ip = NET_AdrToString\( &cl->netchan\.remoteAddress \);",
        )
        lockstep = read(LOCKSTEP)
        connect = function_body(lockstep, r"void ConnectScriptedClient\( std::size_t scriptIndex \)")
        self.assertIn("address.type = NA_BAD;", connect)
        self.assertIn("VM_Call( gvm, 3, GAME_CLIENT_CONNECT, clientNum, qtrue, qfalse );", connect)
        self.assertNotIn("SVF_BOT", lockstep)
        self.assertNotIn("NA_BOT", lockstep)


class ServerLockstepIntegrationTests(unittest.TestCase):
    def test_source_is_built_everywhere_the_server_is(self) -> None:
        self.assertIn("'code/server/sv_lockstep.cpp',", read("meson.build"))
        self.assertIn("code/server/sv_lockstep.cpp", read("CMakeLists.txt"))
        makefile = read("Makefile")
        self.assertIn("$(B)/client/sv_lockstep.o", makefile)
        self.assertIn("$(B)/ded/sv_lockstep.o", makefile)
        for project in ("fnql.vcxproj", "fnql-ded.vcxproj", "fnql.vcxproj.filters", "fnql-ded.vcxproj.filters"):
            self.assertIn(r"..\..\server\sv_lockstep.cpp", read(f"code/win32/msvc2017/{project}"), project)

    def test_interface_is_declared_for_c_callers(self) -> None:
        qcommon = read("code/qcommon/qcommon.h")
        self.assertIn("int SV_LockstepFrameMsec( void );", qcommon)
        self.assertIn("int SV_LockstepClockOrigin( void );", qcommon)
        server = read("code/server/server.h")
        for declaration in ("SV_LockstepInit( void );", "SV_LockstepActive( void );",
                            "SV_LockstepIsScriptedClient( const client_t *client );"):
            self.assertIn(declaration, server)

    def test_documented_and_linked(self) -> None:
        doc = read("docs/fnql/SERVER_LOCKSTEP.md")
        for term in ("sv_lockstep", "sv_lockstepSeed", "sv_lockstepScript", "sv_lockstepRecord",
                     "sv_lockstepFrames", "FNQLLSTP", "fnql-lockstep 1", "NA_BAD", "_time64"):
            self.assertIn(term, doc)
        self.assertIn("[`SERVER_LOCKSTEP.md`](./SERVER_LOCKSTEP.md)", read("docs/fnql/TECHNICAL.md"))
        # the documented record tags are the ones the writer emits
        source = read(LOCKSTEP)
        for tag in "FUKPERCSOXQ":
            self.assertIn(f"= '{tag}',", source)
            self.assertIn(f"| `{tag}` |", doc)
        self.assertIn("constexpr std::array<char, 8> RecordMagic = { 'F', 'N', 'Q', 'L', 'L', 'S', 'T', 'P' };", source)


if __name__ == "__main__":
    unittest.main()
