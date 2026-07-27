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


def read_pointer_policy() -> str:
    return (ROOT / "code/client/input_compat.hpp").read_text(encoding="utf-8")


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

    def test_native_ui_menu_claim_is_not_gated_on_the_ui_menu_stack_query(self) -> None:
        # UI_MENUS_ANY_VISIBLE is a recovered retail slot that is only
        # established for the fullscreen main menu.  Gating KEYCATCH_UI on it
        # here withholds the in-game menu's draw pass, absolute-pointer cursor,
        # and mouse grab, which all key off that catcher bit.
        body = function_body(read_source(), "CL_ActivateNativeMenu")

        self.assertNotIn("CL_UIMenusAreVisible", body)
        self.assertIn(
            "Key_SetCatcher( ( Key_GetCatcher() & ~( KEYCATCH_CGAME | KEYCATCH_BROWSER ) )\n"
            "\t\t| KEYCATCH_UI );",
            body,
        )

    def test_menu_toggle_uses_the_join_menu_for_spectators_only(self) -> None:
        body = function_body(read_source(), "CL_ToggleMenuInternal")

        self.assertIn("const qboolean openJoinMenu = CL_ShouldOpenJoinMenu();", body)
        self.assertIn("( cls.state != CA_ACTIVE || clc.demoplaying )", body)
        self.assertIn("CL_WebHost_HideForGameTransition();", body)
        self.assertNotIn("CG_EVENT_HANDLING, CGAME_EVENT_TEAMMENU", body)
        self.assertIn("if ( !openJoinMenu )", body)
        self.assertIn(
            "CL_ActivateNativeMenu( openJoinMenu ? UIMENU_TEAM : UIMENU_INGAME );",
            body,
        )

    def test_menu_toggle_does_not_query_the_ui_menu_stack(self) -> None:
        body = function_body(read_source(), "CL_ToggleMenuInternal")

        self.assertNotIn("CL_UIMenusAreVisible", body)

    def test_browser_keeps_escape_only_while_it_can_present_a_surface(self) -> None:
        body = function_body(read_source(), "CL_ToggleMenuInternal")

        # A browser input claim that outlived its drawable surface can neither
        # show a menu nor act on Escape, so it must not swallow the key.
        self.assertIn(
            "if ( CL_WebHost_HasDrawableSurface()\n"
            "\t\t\t&& ( cls.state != CA_ACTIVE || clc.demoplaying ) ) {",
            body,
        )

    def test_menu_toggle_records_its_entry_state_for_developers(self) -> None:
        body = function_body(read_source(), "CL_ToggleMenuInternal")

        self.assertIn("Com_DPrintf( \"CL_ToggleMenuInternal: catcher", body)
        self.assertIn("Key_GetCatcher(), (int)cls.state, (int)openJoinMenu );", body)

    def test_join_menu_uses_a_visible_polled_system_cursor(self) -> None:
        sdl_input = read_sdl_input()
        win_input = read_win_input()
        win_wndproc = read_win_wndproc()
        common = (ROOT / "code/qcommon/common.c").read_text(encoding="utf-8")
        qcommon = (ROOT / "code/qcommon/qcommon.h").read_text(encoding="utf-8")

        # The menu owner keeps the visible OS cursor and is fed by a poll, not
        # only by motion events, so a menu opened under a stationary pointer
        # still gets deterministic coordinates.
        self.assertIn("case PointerOwner::Menu:", read_pointer_policy())
        self.assertIn("mode.showSystemCursor = true;", read_pointer_policy())
        self.assertIn("IN_ShowCursor( mode.showSystemCursor ? qtrue : qfalse );", sdl_input)
        self.assertIn("static void IN_PollAbsolutePointerPosition", sdl_input)
        self.assertIn(
            "Com_QueueEvent( eventTime, SE_MOUSE_ABSOLUTE, position.x, position.y, 0, NULL );",
            sdl_input,
        )
        self.assertIn(
            "if ( s_pointerMode.driveInput && s_pointerMode.reportAbsolute ) {\n"
            "\t\tIN_PollAbsolutePointerPosition( s_pointerOwner );",
            sdl_input,
        )
        # Only the confined relative pointer is warped to the window centre.
        self.assertIn("mode.recenterPointer &&", sdl_input)
        self.assertIn("static void IN_WindowMouse", win_input)
        self.assertIn("fnql::input::PointerOwner WIN_ResolvePointerOwner", win_input)
        self.assertIn("inputs.menuMask = kPointerMenuMask;", win_input)
        self.assertIn(
            "kPointerMenuMask = KEYCATCH_UI | KEYCATCH_CGAME | KEYCATCH_BROWSER",
            win_input,
        )
        self.assertIn("PointerOwnerReportsAbsolute( owner )", win_input)
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

        # The console hides the host pointer and mirrors it; browser/UI/cgame
        # retain their visible retail lane. One shared policy decides both.
        policy = read_pointer_policy()
        console_case = policy[
            policy.index("case PointerOwner::Console:") : policy.index("case PointerOwner::Menu:")
        ]
        self.assertIn("mode.reportAbsolute = true;", console_case)
        self.assertIn("mode.showSystemCursor = false;", console_case)
        self.assertIn(
            "return inputs.consoleUsesAbsolutePointer ? PointerOwner::Console",
            policy,
        )

        # SDL grants the console absolute ownership only in a window.
        self.assertIn("inputs.consoleUsesAbsolutePointer = !glw_state.isFullscreen;", sdl_input)
        self.assertIn("IN_PollAbsolutePointerPosition( owner );", sdl_input)

        # The non-SDL platform paths provide the same windowed-console policy.
        self.assertIn("inputs.consoleUsesAbsolutePointer = !glw_state.cdsFullscreen;", win_input)
        self.assertIn("pointerOwner != fnql::input::PointerOwner::Gameplay", win_wndproc)
        self.assertIn("SE_MOUSE_ABSOLUTE, x, y", win_wndproc)
        self.assertIn("static qboolean IN_ConsoleUsesAbsolutePointer", linux_glimp)
        self.assertIn("if ( IN_AbsolutePointerOwner() )", linux_glimp)
        self.assertIn("SE_MOUSE_ABSOLUTE", linux_glimp)


if __name__ == "__main__":
    unittest.main()
