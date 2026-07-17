from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source() -> str:
    return (ROOT / "code/client/cl_keys.cpp").read_text(encoding="utf-8")


def read_sdl_input() -> str:
    return (ROOT / "code/sdl/sdl_input.cpp").read_text(encoding="utf-8")


def read_win_input() -> str:
    return (ROOT / "code/win32/win_input.cpp").read_text(encoding="utf-8")


def read_win_wndproc() -> str:
    return (ROOT / "code/win32/win_wndproc.cpp").read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str:
    match = re.search(
        rf"static\s+(?:qboolean|void)\s+{name}\s*\([^)]*\)\s*\{{",
        source,
    )
    if not match:
        raise AssertionError(f"Missing {name}")

    depth = 1
    for index in range(match.end(), len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.end() : index]
    raise AssertionError(f"Unterminated {name}")


class JoinMenuSourceTests(unittest.TestCase):
    def test_spectator_join_menu_is_gated_by_a_valid_active_snapshot(self) -> None:
        body = function_body(read_source(), "CL_ShouldOpenJoinMenu")

        self.assertIn("cls.state != CA_ACTIVE", body)
        self.assertIn("clc.demoplaying", body)
        self.assertIn("!cl.snap.valid", body)
        self.assertIn("cl.snap.ps.persistant[PERS_TEAM] == TEAM_SPECTATOR", body)

    def test_native_ui_menu_releases_browser_capture(self) -> None:
        body = function_body(read_source(), "CL_ActivateNativeMenu")

        self.assertIn("UI_SET_ACTIVE_MENU, menu", body)
        self.assertIn("CL_WebHost_HideForGameTransition();", body)
        self.assertIn("KEYCATCH_CGAME | KEYCATCH_BROWSER", body)
        self.assertIn("| KEYCATCH_UI", body)

    def test_menu_toggle_uses_the_join_menu_for_spectators_only(self) -> None:
        body = function_body(read_source(), "CL_ToggleMenuInternal")

        self.assertIn("const qboolean openJoinMenu = CL_ShouldOpenJoinMenu();", body)
        self.assertIn("if ( cls.state != CA_ACTIVE || clc.demoplaying )", body)
        self.assertIn("CL_WebHost_HideForGameTransition();", body)
        self.assertIn("CG_EVENT_HANDLING, CGAME_EVENT_TEAMMENU", body)
        self.assertIn("| KEYCATCH_CGAME", body)
        self.assertIn("if ( !openJoinMenu )", body)
        self.assertIn(
            "CL_ActivateNativeMenu( UIMENU_INGAME );",
            body,
        )

    def test_join_menu_uses_a_visible_polled_system_cursor(self) -> None:
        sdl_input = read_sdl_input()
        win_input = read_win_input()
        win_wndproc = read_win_wndproc()
        common = (ROOT / "code/qcommon/common.c").read_text(encoding="utf-8")
        qcommon = (ROOT / "code/qcommon/qcommon.h").read_text(encoding="utf-8")

        self.assertIn(
            "if ( browserActive || nativeUiActive || cgameUiActive )",
            sdl_input,
        )
        self.assertIn("IN_ShowCursor( qtrue );", sdl_input)
        self.assertIn("static void IN_QueueAbsoluteMousePosition", sdl_input)
        self.assertIn("SE_MOUSE_ABSOLUTE, positionX, positionY", sdl_input)
        self.assertIn(
            "if ( !absoluteMouse ) {\n"
            "\t\t\tSDL_WarpMouseInWindow",
            sdl_input,
        )
        self.assertIn(
            "if ( nativeUiActive || cgameUiActive ) {\n"
            "\t\tIN_QueueAbsoluteMousePosition();",
            sdl_input,
        )
        self.assertIn("static void IN_WindowMouse", win_input)
        self.assertIn(
            "Key_GetCatcher() & ( KEYCATCH_BROWSER | KEYCATCH_UI | KEYCATCH_CGAME )",
            win_input,
        )
        self.assertIn("IN_WindowMouse();", win_input)
        self.assertIn(
            "SetCursor( LoadCursor( NULL, IDC_ARROW ) );",
            win_wndproc,
        )
        self.assertIn("SE_MOUSE_ABSOLUTE", qcommon)
        self.assertIn(
            "lastEvent->evValue = value;\n"
            "\t\tlastEvent->evValue2 = value2;",
            common,
        )
        self.assertIn("case SE_MOUSE_ABSOLUTE:", common)


if __name__ == "__main__":
    unittest.main()
