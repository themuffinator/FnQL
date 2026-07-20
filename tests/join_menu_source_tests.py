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


def read_linux_glimp() -> str:
    return (ROOT / "code/unix/linux_glimp.cpp").read_text(encoding="utf-8")


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
        self.assertIn("if ( menu == UIMENU_TEAM )", body)
        self.assertIn("UI_SET_ACTIVE_MENU, UIMENU_INGAME", body)
        self.assertLess(
            body.index("UI_SET_ACTIVE_MENU, UIMENU_INGAME"),
            body.index("UI_SET_ACTIVE_MENU, menu"),
        )
        self.assertIn("CL_WebHost_HideForGameTransition();", body)
        self.assertIn("KEYCATCH_CGAME | KEYCATCH_BROWSER", body)
        self.assertIn("| KEYCATCH_UI", body)

    def test_menu_toggle_uses_the_join_menu_for_spectators_only(self) -> None:
        body = function_body(read_source(), "CL_ToggleMenuInternal")

        self.assertIn("const qboolean openJoinMenu = CL_ShouldOpenJoinMenu();", body)
        self.assertIn("if ( cls.state != CA_ACTIVE || clc.demoplaying )", body)
        self.assertIn("CL_WebHost_HideForGameTransition();", body)
        self.assertNotIn("CG_EVENT_HANDLING, CGAME_EVENT_TEAMMENU", body)
        self.assertIn("if ( !openJoinMenu )", body)
        self.assertIn(
            "CL_ActivateNativeMenu( openJoinMenu ? UIMENU_TEAM : UIMENU_INGAME );",
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
            "if ( grabMouse )\n"
            "\t\t\tSDL_WarpMouseInWindow",
            sdl_input,
        )
        self.assertIn(
            "if ( browserActive || nativeUiActive || cgameUiActive ) {\n"
            "\t\tIN_QueueAbsoluteMousePosition();",
            sdl_input,
        )
        self.assertIn("static void IN_WindowMouse", win_input)
        self.assertIn("static int IN_AbsolutePointerOwnerKind", win_input)
        self.assertIn("catcher & KEYCATCH_BROWSER", win_input)
        self.assertIn("catcher & KEYCATCH_UI", win_input)
        self.assertIn("catcher & KEYCATCH_CGAME", win_input)
        self.assertIn("if ( absolutePointerOwner )", win_input)
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

    def test_windowed_console_reuses_retail_absolute_event_without_changing_ui_coordinates(self) -> None:
        sdl_input = read_sdl_input()
        win_input = read_win_input()
        win_wndproc = read_win_wndproc()
        linux_glimp = read_linux_glimp()
        client_input = (ROOT / "code/client/cl_input.cpp").read_text(encoding="utf-8")
        qcommon = (ROOT / "code/qcommon/qcommon.h").read_text(encoding="utf-8")

        # FnQL already has the retail-shaped absolute event. Do not add FnQ3's
        # second event or its synthetic 640x480 cursor-delta priming scheme.
        self.assertIsNone(re.search(r"\bSE_MOUSE_ABS\b", qcommon))
        self.assertNotIn("-0x4000", client_input)
        self.assertIn("void CL_MouseAbsoluteEvent( int x, int y )", client_input)
        self.assertIn("Con_SetMousePos( x, y );", client_input)
        self.assertIn("VM_Call( uivm, 2, UI_MOUSE_EVENT, x, y );", client_input)
        self.assertIn("VM_Call( cgvm, 2, CG_MOUSE_EVENT, x, y );", client_input)

        # SDL frees and hides the host pointer only for the windowed console;
        # browser/UI/cgame retain their visible, raw-coordinate retail lane.
        self.assertIn(
            "const qboolean consoleAbsolute = ( consoleActive && !glw_state.isFullscreen )",
            sdl_input,
        )
        self.assertIn(
            "const qboolean retailAbsolute = ( !consoleActive &&",
            sdl_input,
        )
        self.assertIn("s_absCursor = consoleAbsolute;", sdl_input)
        self.assertIn("if ( s_absCursor )", sdl_input)
        self.assertIn("IN_DriveAbsCursor();", sdl_input)

        # The non-SDL platform paths provide the same windowed-console policy.
        self.assertIn("if ( catcher & KEYCATCH_CONSOLE )", win_input)
        self.assertIn("return !glw_state.cdsFullscreen ? KEYCATCH_CONSOLE : 0", win_input)
        self.assertIn("static qboolean WIN_ConsoleUsesAbsolutePointer", win_wndproc)
        self.assertIn("SE_MOUSE_ABSOLUTE, x, y", win_wndproc)
        self.assertIn("static qboolean IN_ConsoleUsesAbsolutePointer", linux_glimp)
        self.assertIn("if ( IN_AbsolutePointerOwner() )", linux_glimp)
        self.assertIn("SE_MOUSE_ABSOLUTE", linux_glimp)


if __name__ == "__main__":
    unittest.main()
