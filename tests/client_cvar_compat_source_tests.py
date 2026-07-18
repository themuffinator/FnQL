import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ClientCvarCompatibilitySourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.main = read("code/client/cl_main.cpp")
        cls.input = read("code/client/cl_input.cpp")
        cls.cgame = read("code/client/cl_cgame.cpp")
        cls.keys = read("code/client/cl_keys.cpp")
        cls.screen = read("code/client/cl_scrn.cpp")
        cls.client_sources = "\n".join(
            (cls.main, cls.input, cls.cgame, cls.keys, cls.screen)
        )

    def test_observed_retail_client_cvars_have_engine_owners(self) -> None:
        # Observed as literal client cvar names in the legitimate Steam retail
        # quakelive_steam.exe, then classified against the recovered CL_Init
        # table. cl_cdkey is storage rather than a cvar; cl_currentServerAddress,
        # cl_paused, and cl_running are intentionally dynamic/common owners.
        expected_registrations = {
            "cl_allowConsoleChat",
            "cl_anglespeedkey",
            "cl_anonymous",
            "cl_autoTimeNudge",
            "cl_avidemo",
            "cl_avidemo_latch",
            "cl_avidemo_maxtime",
            "cl_avidemo_mintime",
            "cl_debugMove",
            "cl_demoRecordMessage",
            "cl_downloadItem",
            "cl_downloadName",
            "cl_downloadTime",
            "cl_forceavidemo",
            "cl_freelook",
            "cl_freezeDemo",
            "cl_maxpackets",
            "cl_maxPing",
            "cl_motd",
            "cl_motdString",
            "cl_mouseAccel",
            "cl_mouseAccelDebug",
            "cl_mouseAccelOffset",
            "cl_mouseAccelPower",
            "cl_mouseSensCap",
            "cl_nodelta",
            "cl_packetdup",
            "cl_pitchspeed",
            "cl_platform",
            "cl_quitOnDemoCompleted",
            "cl_run",
            "cl_serverStatusResendTime",
            "cl_shownet",
            "cl_showSend",
            "cl_showTimeDelta",
            "cl_timeNudge",
            "cl_timeout",
            "cl_viewAccel",
            "cl_yawspeed",
        }
        registrations = set(
            re.findall(
                r'Cvar_Get\s*\(\s*"(cl_[A-Za-z0-9_]+)"',
                self.client_sources,
            )
        )
        self.assertEqual(set(), expected_registrations - registrations)

        qcommon = read("code/qcommon/common.c")
        qcommon_header = read("code/qcommon/qcommon.h")
        parse = read("code/client/cl_parse.cpp")
        ui = read("code/client/cl_ui.cpp")
        self.assertIn("extern char cl_cdkey[34];", qcommon_header)
        self.assertIn("CLUI_GetCDKey", ui)
        self.assertIn("CLUI_SetCDKey", ui)
        self.assertIn('Cvar_Set( "cl_currentServerAddress", server );', self.main)
        self.assertRegex(qcommon, r'Cvar_Get\s*\(\s*"cl_paused"')
        self.assertRegex(qcommon, r'Cvar_Get\s*\(\s*"cl_running"')
        self.assertIn('Cvar_Set( "cl_paused", "0" );', parse)
        for retained_progress_name in ("cl_downloadCount", "cl_downloadSize"):
            self.assertRegex(
                self.main,
                rf'Cvar_Get\s*\(\s*"{retained_progress_name}"\s*,\s*"0"\s*,'
            )

    def test_retail_defaults_bounds_and_flags_are_explicit(self) -> None:
        expected_defaults = {
            "cl_allowConsoleChat": "0",
            "cl_anglespeedkey": "1.5",
            "cl_anonymous": "0",
            "cl_autoTimeNudge": "0",
            "cl_avidemo": "0",
            "cl_avidemo_latch": "0",
            "cl_avidemo_maxtime": "0",
            "cl_avidemo_mintime": "0",
            "cl_debugMove": "0",
            "cl_demoRecordMessage": "2",
            "cl_downloadItem": "",
            "cl_downloadName": "",
            "cl_downloadTime": "0",
            "cl_forceavidemo": "0",
            "cl_freelook": "1",
            "cl_freezeDemo": "0",
            "cl_maxpackets": "125",
            "cl_maxPing": "800",
            "cl_motd": "1",
            "cl_motdString": "",
            "cl_mouseAccel": "0",
            "cl_mouseAccelDebug": "0",
            "cl_mouseAccelOffset": "0",
            "cl_mouseAccelPower": "2",
            "cl_mouseSensCap": "0",
            "cl_nodelta": "0",
            "cl_packetdup": "1",
            "cl_pitchspeed": "140",
            "cl_platform": "1",
            "cl_quitOnDemoCompleted": "0",
            "cl_run": "1",
            "cl_serverStatusResendTime": "750",
            "cl_shownet": "0",
            "cl_showSend": "0",
            "cl_showTimeDelta": "0",
            "cl_timeNudge": "0",
            "cl_timeout": "40",
            "cl_viewAccel": "1.7",
            "cl_yawspeed": "140",
        }
        for name, default in expected_defaults.items():
            with self.subTest(name=name):
                self.assertRegex(
                    self.client_sources,
                    rf'Cvar_Get\s*\(\s*"{re.escape(name)}"\s*,\s*'
                    rf'"{re.escape(default)}"\s*,',
                )

        self.assertRegex(
            self.main,
            r'Cvar_Get\(\s*"cl_timeout",\s*"40",\s*0\s*\)',
        )
        self.assertRegex(
            self.main,
            r'Cvar_Get\(\s*"cl_timeNudge",\s*"0",\s*'
            r'CVAR_ARCHIVE\s*\|\s*CVAR_PROTECTED\s*\|\s*CVAR_VM_CREATED\s*\)',
        )
        self.assertIn(
            'Cvar_CheckRange( cl_timeNudge, "-20", "0", CV_INTEGER );',
            self.main,
        )
        self.assertRegex(
            self.main,
            r'Cvar_Get\(\s*"cl_autoTimeNudge",\s*"0",\s*'
            r'CVAR_ARCHIVE\s*\|\s*CVAR_PROTECTED\s*\|\s*CVAR_VM_CREATED\s*\)',
        )
        self.assertIn(
            'Cvar_Get( "cl_allowConsoleChat", "0",\n'
            '\t\tCVAR_ARCHIVE | CVAR_PROTECTED | CVAR_CLOUD );',
            self.main,
        )
        self.assertIn('Cvar_Get( "cl_platform", "1", CVAR_ROM );', self.main)
        self.assertIn('Cvar_Get( "cl_nodelta", "0", 0 );', self.input)

    def test_new_retail_controls_are_wired_to_behavior(self) -> None:
        self.assertIn("cl_allowConsoleChat->integer", self.keys)
        self.assertIn("Con_UseAutoSay()", self.keys)
        self.assertIn("if ( !consoleChatAllowed", self.keys)
        self.assertIn("else if ( consoleChatAllowed )", self.keys)
        self.assertIn("PlanAvidemoFrame( avidemoInputs )", self.main)
        self.assertIn('Cbuf_ExecuteText( EXEC_NOW, "screenshot silent\\n" );', self.main)
        self.assertRegex(
            self.main,
            r"if \( avidemoPlan\.timingActive \) \{\s*"
            r"gameMsec = avidemoPlan\.frameMilliseconds;\s*"
            r"audioMsec = gameMsec;",
        )
        self.assertIn("cl_quitOnDemoCompleted->integer", self.main)
        self.assertIn("CL_DemoRecordMessageMode()", self.screen)
        self.assertIn("SelectTimeNudge(", self.cgame)
        self.assertIn("Sys_IsLANAddress( &clc.serverAddress )", self.cgame)

    def test_fnql_extensions_remain_available(self) -> None:
        for name in (
            "cl_allowDownload",
            "cl_autoNudge",
            "cl_aviFrameRate",
            "cl_drawRecording",
            "cl_mouseAccelStyle",
            "cl_renderer",
        ):
            self.assertIn(f'"{name}"', self.client_sources)


if __name__ == "__main__":
    unittest.main()
