import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ServerCvarCompatibilitySourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.init = read("code/server/sv_init.cpp")
        cls.main = read("code/server/sv_main.cpp")
        cls.game = read("code/server/sv_game.cpp")
        cls.commands = read("code/server/sv_ccmds.cpp")
        cls.platform = read("code/server/sv_platform.cpp")
        cls.collision = read("code/qcommon/cm_trace.c")
        cls.collision_load = read("code/qcommon/cm_load.c")
        cls.common = read("code/qcommon/common.c")
        cls.api = read("code/platform/fnql_steam_api.h")
        cls.server_sources = "\n".join(
            (cls.init, cls.main, cls.game, cls.commands, cls.platform)
        )

    def test_observed_retail_engine_cvars_have_explicit_owners(self) -> None:
        # Literal names and the pre-map cvarlist were captured from the user's
        # legitimate Steam build. qagame-owned names are checked separately.
        expected = {
            "sv_altEntDir",
            "sv_cylinderScale",
            "sv_dumpEntities",
            "sv_errorExit",
            "sv_floodProtect",
            "sv_fps",
            "sv_gtid",
            "sv_hostname",
            "sv_idleExit",
            "sv_idleRestart",
            "sv_killserver",
            "sv_lanForceRate",
            "sv_master",
            "sv_maxclients",
            "sv_padPackets",
            "sv_privateClients",
            "sv_privatePassword",
            "sv_pure",
            "sv_quitOnEmpty",
            "sv_quitOnExitLevel",
            "sv_reconnectlimit",
            "sv_setSteamAccount",
            "sv_showloss",
            "sv_tags",
            "sv_timeout",
            "sv_vac",
            "sv_zombietime",
        }
        registrations = set(
            re.findall(r'Cvar_Get\s*\(\s*"(sv_[A-Za-z0-9_]+)"', self.server_sources)
        )
        self.assertEqual(set(), expected - registrations)

        factory = read("code/server/sv_factory.cpp")
        for name in ("sv_includeCurrentMapInVote", "sv_mapPoolFile", "sv_serverType"):
            self.assertRegex(factory, rf'Cvar_Get\s*\(\s*"{name}"')
        self.assertIn('va( "sv_master%d", index + 1 )', self.init)
        self.assertRegex(
            read("code/qcommon/q_shared.h"),
            r"#define\s+MAX_MASTER_SERVERS\s+5\b",
        )

    def test_retail_defaults_and_sensitive_flags_are_pinned(self) -> None:
        defaults = {
            "sv_altEntDir": "",
            "sv_cylinderScale": "1.1f",
            "sv_dumpEntities": "0",
            "sv_errorExit": "1",
            "sv_gtid": "",
            "sv_idleExit": "120",
            "sv_idleRestart": "1",
            "sv_master": "1",
            "sv_quitOnEmpty": "0",
            "sv_quitOnExitLevel": "0",
            "sv_setSteamAccount": "",
            "sv_showloss": "0",
            "sv_tags": "",
            "sv_vac": "1",
        }
        for name, default in defaults.items():
            self.assertRegex(
                self.init,
                rf'Cvar_Get\s*\(\s*"{name}"\s*,\s*"{re.escape(default)}"',
            )
        self.assertRegex(
            self.init,
            r'Cvar_Get\s*\(\s*"sv_gtid"\s*,\s*""\s*,\s*'
            r'CVAR_SERVERINFO\s*\|\s*CVAR_ROM',
        )
        self.assertRegex(
            self.init,
            r'Cvar_Get\s*\(\s*"sv_setSteamAccount"\s*,\s*""\s*,'
            r'[\s\S]{0,100}CVAR_PROTECTED[\s\S]{0,100}CVAR_PRIVATE',
        )

    def test_game_module_cvar_boundary_is_not_duplicated(self) -> None:
        # Retail qagame owns these names. The engine loads/registers native game
        # cvars early, but must not invent replacement gameplay behavior.
        self.assertNotRegex(
            self.server_sources,
            r'Cvar_Get\s*\(\s*"sv_warmupReadyPercentage"',
        )
        self.assertNotRegex(self.server_sources, r'Cvar_Get\s*\(\s*"sv_mapname"')
        self.assertIn("SV_CallGameRegisterCvarsOnce", self.game)
        self.assertIn('Cvar_Get ("mapname", "nomap", CVAR_SERVERINFO | CVAR_ROM)', self.init)

    def test_entity_override_and_dump_are_bounded_vfs_operations(self) -> None:
        self.assertIn("BuildEntityFilePath", self.game)
        self.assertIn("FS_ReadFile", self.game)
        self.assertIn("FS_FreeFile", self.game)
        self.assertIn("FS_WriteFile", self.game)
        self.assertIn('"ents"', self.game)
        self.assertIn("std::memchr", self.game)
        self.assertIn("SelectServerEntityString()", self.game)
        self.assertIn("serverEntityOverride.clear()", self.game)
        # Retail preserves advertisement entities at the qagame boundary and
        # sv_dumpEntities writes those original classnames verbatim.
        self.assertNotIn("CMod_CopyRewrittenEntityString", self.collision_load)
        self.assertIn("Com_Memcpy( cm.entityString", self.collision_load)
        self.assertIn("cm.entityString[ l->filelen ] = '\\0';", self.collision_load)

    def test_lifecycle_and_error_policies_are_wired_at_transition_owners(self) -> None:
        self.assertIn("SV_ShouldErrorExit( code )", self.common)
        self.assertLess(
            self.common.index("Q_vsnprintf( com_errorMessage"),
            self.common.index("SV_ShouldErrorExit( code )"),
        )
        self.assertIn("emptyServerTimer.Poll", self.main)
        self.assertIn("idleExitTimer.Poll", self.main)
        self.assertIn("!SV_HasHumanClients()", self.main)
        self.assertIn('Cbuf_AddText( "vstr nextmap\\n" )', self.main)
        self.assertIn("sv_masterAdvertise && !sv_masterAdvertise->integer", self.main)
        self.assertGreaterEqual(self.main.count("SV_HandleQuitOnExitLevel"), 3)
        self.assertGreaterEqual(self.commands.count("SV_HandleQuitOnExitLevel"), 2)

    def test_cylinder_scale_changes_only_vertical_cylinder_radius(self) -> None:
        function = self.collision[
            self.collision.index("static void CM_TraceThroughVerticalCylinder") :
            self.collision.index("static void CM_TraceCapsuleThroughCapsule")
        ]
        self.assertIn('Cvar_Get( "sv_cylinderScale", "1.1f", 0 )', function)
        self.assertIn("radius *= cylinderScale->value", function)
        self.assertNotIn("halfheight *=", function)

    def test_steam_surface_preserves_fnql_policy_and_exposes_retail_fields(self) -> None:
        self.assertIn("BuildSteamGameTags", self.platform)
        self.assertIn("tagInputs.fnqlKeywords", self.platform)
        self.assertIn("tagInputs.retailTags", self.platform)
        self.assertIn("FNQL_Steam_UpdateGameServer", self.platform)
        self.assertIn("SteamGameServerConfigsEqual", self.platform)
        self.assertNotIn("std::memcmp", self.platform)
        self.assertIn("&& ( !sv_vac || sv_vac->integer != 0 )", self.platform)
        self.assertIn('Cvar_Get( "sv_steamSecure", "0"', self.platform)
        self.assertIn("FNQL_STEAM_CAP_GAME_SERVER_ACCOUNT", self.api)
        self.assertIn("game_server_account", self.api)
        self.assertIn("provider-unsupported", self.platform)

    def test_fnql_server_extensions_and_non_regressive_defaults_remain(self) -> None:
        retained = {
            "sv_allowDownload": "1",
            "sv_audioPVS": "0",
            "sv_autoRecordDemos": "0",
            "sv_dlRate": "100",
            "sv_floodProtect": "1",
            "sv_maxRate": "0",
            "sv_timeout": "200",
        }
        for name, default in retained.items():
            self.assertRegex(
                self.init,
                rf'Cvar_Get\s*\(\s*"{name}"\s*,\s*"{re.escape(default)}"',
            )
        self.assertIn("sv_levelTimeReset", self.main)
        self.assertIn("sv_maxclientsPerIP", self.server_sources)


if __name__ == "__main__":
    unittest.main()
