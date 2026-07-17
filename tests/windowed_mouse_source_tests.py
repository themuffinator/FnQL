from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str:
    match = re.search(
        rf"(?:static\s+)?(?:qboolean|void|int|LRESULT)\s+{name}\s*\([^)]*\)\s*\{{",
        source,
    )
    if not match:
        raise AssertionError(f"Missing function {name}")

    depth = 1
    for index in range(match.end(), len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.end() : index]
    raise AssertionError(f"Unterminated function {name}")


class WindowedMouseSourceTests(unittest.TestCase):
    def test_reuses_the_single_retail_absolute_event_without_ui_remapping(self) -> None:
        qcommon = read_text("code/qcommon/qcommon.h")
        client_input = read_text("code/client/cl_input.cpp")

        self.assertEqual(len(re.findall(r"\bSE_MOUSE_ABSOLUTE\b", qcommon)), 1)
        self.assertIsNone(re.search(r"\bSE_MOUSE_ABS\b", qcommon))
        self.assertNotIn("-0x4000", client_input)
        self.assertIn("Con_SetMousePos( x, y );", client_input)
        self.assertIn("UI_MOUSE_EVENT, x, y", client_input)
        self.assertIn("CG_MOUSE_EVENT, x, y", client_input)

    def test_sdl_console_motion_precedes_click_and_scales_only_console_coordinates(self) -> None:
        source = read_text("code/sdl/sdl_input.cpp")
        queue_body = function_body(source, "IN_QueueConsoleAbsolutePosition")
        events = function_body(source, "HandleEvents")

        self.assertIn("cls.glconfig.vidWidth / (float)glw_state.window_width", queue_body)
        self.assertIn("cls.glconfig.vidHeight / (float)glw_state.window_height", queue_body)
        self.assertIn(
            "IN_QueueConsoleAbsolutePosition( e.motion.x, e.motion.y, in_eventTime );",
            events,
        )
        button_start = events.index("case SDL_EVENT_MOUSE_BUTTON_DOWN:")
        button_end = events.index("case SDL_EVENT_MOUSE_WHEEL:", button_start)
        button_block = events[button_start:button_end]
        self.assertLess(
            button_block.index("IN_QueueConsoleAbsolutePosition( e.button.x, e.button.y"),
            button_block.index("SE_KEY"),
        )
        # The established retail lane continues to forward raw SDL host coordinates.
        self.assertIn("(int)e.motion.x, (int)e.motion.y", events)

    def test_sdl_transitions_discard_relative_spikes_and_never_warp_absolute_owners(self) -> None:
        source = read_text("code/sdl/sdl_input.cpp")
        activate = function_body(source, "IN_ActivateMouse")
        deactivate = function_body(source, "IN_DeactivateMouse")

        self.assertIn("haveState && relativeMouse != lastRelative", activate)
        self.assertNotIn("s_absHaveLast = qfalse;\n\t\thaveState = qfalse;", activate)
        self.assertIn("const qboolean absolutePointerOwned", deactivate)
        self.assertIn("gw_active && !absolutePointerOwned", deactivate)
        self.assertIn("!absolutePointerOwned && drv", deactivate)

    def test_sdl_drag_capture_is_bounded_by_buttons_owner_and_focus_lifecycle(self) -> None:
        source = read_text("code/sdl/sdl_input.cpp")
        events = function_body(source, "HandleEvents")
        deactivate = function_body(source, "IN_DeactivateMouse")
        window_event = function_body(source, "IN_HandleWindowEvent")

        self.assertIn("SDL_CaptureMouse( true );", events)
        self.assertIn("SDL_CaptureMouse( false );", events)
        self.assertIn("s_absCaptureButtons |= buttonMask", events)
        self.assertIn("s_absCaptureButtons &= ~buttonMask", events)
        self.assertIn("IN_EndTemporaryMouseCapture();", deactivate)
        focus_lost = window_event.index("case SDL_EVENT_WINDOW_FOCUS_LOST:")
        focus_gained = window_event.index("case SDL_EVENT_WINDOW_FOCUS_GAINED:")
        self.assertIn(
            "IN_EndTemporaryMouseCapture();",
            window_event[focus_lost:focus_gained],
        )

    def test_win32_console_precedes_underlying_catchers_and_keeps_extended_buttons(self) -> None:
        win_input = read_text("code/win32/win_input.cpp")
        wndproc = read_text("code/win32/win_wndproc.cpp")
        owner = function_body(win_input, "IN_AbsolutePointerOwnerKind")

        self.assertLess(owner.index("catcher & KEYCATCH_CONSOLE"), owner.index("KEYCATCH_BROWSER"))
        self.assertIn("return !glw_state.cdsFullscreen", owner)
        self.assertIn(
            "!WIN_ConsoleOwnsPointer() && ( Key_GetCatcher() & KEYCATCH_BROWSER )",
            wndproc,
        )
        self.assertIn("case WM_XBUTTONDOWN:", wndproc)
        self.assertIn("case WM_XBUTTONUP:", wndproc)
        self.assertIn("K_MOUSE4 : K_MOUSE5", wndproc)
        self.assertIn("SetCapture( hWnd );", wndproc)
        self.assertIn("ReleaseCapture();", wndproc)
        self.assertIn("WIN_ReleaseTemporaryMouseCapture();", win_input)

    def test_x11_console_precedence_initial_poll_and_unconfined_drag_capture(self) -> None:
        source = read_text("code/unix/linux_glimp.cpp")
        owner = function_body(source, "IN_AbsolutePointerOwnerKind")
        poll = function_body(source, "IN_PollAbsolutePointerPosition")
        capture = function_body(source, "IN_BeginTemporaryPointerCapture")
        frame = function_body(source, "IN_Frame")

        self.assertLess(owner.index("catcher & KEYCATCH_CONSOLE"), owner.index("KEYCATCH_BROWSER"))
        self.assertIn("XQueryPointer", poll)
        self.assertIn("IN_QueueAbsolutePointerPosition", poll)
        self.assertIn("IN_PollAbsolutePointerPosition();", frame)
        self.assertIn("absolute_position_valid = qfalse", frame)
        self.assertIn("GrabModeAsync, GrabModeAsync, None, None", capture)
        self.assertIn("button >= 4 && button <= 7", capture)
        self.assertIn("IN_EndTemporaryPointerCapture();", frame)
        focus_out = source.index("case FocusOut:")
        expose = source.index("case Expose:", focus_out)
        self.assertIn("IN_EndTemporaryPointerCapture();", source[focus_out:expose])
        self.assertIn("XUngrabPointer", source)


if __name__ == "__main__":
    unittest.main()
