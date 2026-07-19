from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class WebUiSecretBoundaryTests(unittest.TestCase):
    def test_private_cvars_never_cross_the_browser_event_boundary(self) -> None:
        cvar = (ROOT / "code/qcommon/cvar.c").read_text(encoding="utf-8")
        webui = (ROOT / "code/client/cl_webui.cpp").read_text(encoding="utf-8")

        publish = cvar[
            cvar.index("static void Cvar_PublishChange") : cvar.index(
                "return a hash value for the filename"
            )
        ]
        self.assertIn("var->flags & CVAR_PRIVATE", publish)
        self.assertLess(
            publish.index("var->flags & CVAR_PRIVATE"),
            publish.index("CL_WebView_PublishCvarChange"),
        )
        config_snapshot = webui[
            webui.index("static qboolean CL_WebHost_AppendConfigCvar") : webui.index(
                "static void CL_WebHost_BuildConfigCvarJson"
            )
        ]
        self.assertIn("flags & CVAR_PRIVATE", config_snapshot)
        self.assertGreaterEqual(webui.count("Cvar_Flags( name ) & CVAR_PRIVATE"), 3)
        self.assertGreaterEqual(
            webui.count("Cvar_Flags( name ) & ( CVAR_PRIVATE | CVAR_PROTECTED )"),
            2,
        )
        browser_publish = webui[
            webui.index("void CL_WebView_PublishCvarChange") : webui.index(
                "void CL_WebView_PublishBindChanged"
            )
        ]
        self.assertIn("Cvar_Flags( name ) & CVAR_PRIVATE", browser_publish)
        self.assertLess(
            browser_publish.index("Cvar_Flags( name ) & CVAR_PRIVATE"),
            browser_publish.index("CL_WebUI_JsonEscape( value"),
        )
        self.assertGreaterEqual(cvar.count("<private>"), 4)

    def test_password_cvars_are_marked_private_at_registration(self) -> None:
        sources = {
            "code/server/sv_zmq.cpp": (
                'Cvar_Get( "zmq_rcon_password", "", CVAR_TEMP | CVAR_PRIVATE )',
                'Cvar_Get( "zmq_stats_password", "", CVAR_TEMP | CVAR_PRIVATE )',
            ),
            "code/server/sv_init.cpp": (
                'Cvar_Get ("rconPassword", "", CVAR_TEMP | CVAR_PRIVATE )',
                'Cvar_Get ("sv_privatePassword", "", CVAR_TEMP | CVAR_PRIVATE )',
            ),
            "code/client/cl_main.cpp": (
                'Cvar_Get ("rconPassword", "", CVAR_TEMP | CVAR_PRIVATE )',
            ),
            "code/qcommon/net_ip.c": (
                'Cvar_Get( "net_socksPassword", "", CVAR_LATCH | CVAR_ARCHIVE_ND | CVAR_PRIVATE )',
            ),
        }

        for relative, registrations in sources.items():
            source = (ROOT / relative).read_text(encoding="utf-8")
            for registration in registrations:
                with self.subTest(file=relative, registration=registration):
                    self.assertIn(registration, source)


if __name__ == "__main__":
    unittest.main()
