from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

RETURN_TYPE = r"(?:[A-Za-z_][A-Za-z0-9_:]*\s*[*&]?)"


def read_text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str:
    match = re.search(
        rf"(?:static\s+)?{RETURN_TYPE}\s+{name}\s*\([^)]*\)\s*(?:noexcept\s*)?\{{",
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


class SharedPointerPolicyTests(unittest.TestCase):
    """Every backend must reach the same ownership and presentation decision."""

    # Backend source and the function that applies the resolved pointer mode.
    BACKENDS = {
        "code/sdl/sdl_input.cpp": "IN_ApplyPointerMode",
        "code/win32/win_input.cpp": "IN_Frame",
        "code/unix/linux_glimp.cpp": "IN_Frame",
    }

    def test_every_backend_resolves_ownership_through_the_shared_policy(self) -> None:
        for path in self.BACKENDS:
            source = read_text(path)
            with self.subTest(path=path):
                self.assertIn('#include "../client/input_compat.hpp"', source)
                self.assertIn("fnql::input::ResolvePointerOwner( inputs )", source)
                self.assertIn("fnql::input::ResolvePointerMode( inputs )", source)
                self.assertIn("inputs.consoleMask = KEYCATCH_CONSOLE;", source)
                self.assertIn("inputs.menuMask = kPointerMenuMask;", source)
                self.assertIn(
                    "kPointerMenuMask = KEYCATCH_UI | KEYCATCH_CGAME | KEYCATCH_BROWSER",
                    source,
                )
                # No backend keeps a private copy of the ownership decision.
                self.assertNotIn("ABSOLUTE_POINTER_RETAIL", source)

    def test_policy_separates_confinement_from_relative_motion_and_cursor(self) -> None:
        policy = read_text("code/client/input_compat.hpp")
        mode = function_body(policy, "ResolvePointerMode")

        # An unusable window drives nothing and holds nothing.
        self.assertIn("if ( !inputs.focused || inputs.minimized ) {", mode)
        # Overlays report absolute positions and are confined only in fullscreen,
        # where there is no desktop edge to stop the pointer.
        self.assertEqual(mode.count("mode.confineToWindow = inputs.fullscreen;"), 2)
        self.assertIn("case PointerOwner::Menu:", mode)
        self.assertIn("case PointerOwner::Console:", mode)
        # Only gameplay takes the relative, hidden, re-centred pointer.
        self.assertIn("mode.relativeMotion = inputs.relativeAvailable;", mode)
        self.assertIn("mode.recenterPointer = true;", mode)
        self.assertEqual(mode.count("mode.recenterPointer = true;"), 1)

    def test_every_backend_applies_fullscreen_menu_confinement(self) -> None:
        for path, applier in self.BACKENDS.items():
            body = function_body(read_text(path), applier)
            with self.subTest(path=path):
                self.assertIn("mode.confineToWindow", body)
                self.assertIn("mode.driveInput", body)


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

    def test_every_absolute_consumer_gets_renderer_drawable_pixels(self) -> None:
        """Retail's _UI_MouseEvent divides by glconfig.vidWidth/vidHeight and
        drops the event when the result leaves 640x480, so sending it raw
        host-window coordinates makes an in-game menu unresponsive whenever the
        renderer resolution is not the window size."""
        policy = read_text("code/client/input_compat.hpp")
        project = function_body(policy, "ProjectPointerToDrawable")

        self.assertIn("projection.hostWidth > 0 && projection.drawableWidth > 0", project)
        self.assertIn("projection.hostHeight > 0 && projection.drawableHeight > 0", project)
        # Unknown geometry passes the coordinate through instead of zeroing it.
        self.assertIn("PointerPosition projected{ x, y };", project)

        for path, host in (
            ("code/sdl/sdl_input.cpp", "glw_state.window_width"),
            ("code/win32/win_input.cpp", "client.right - client.left"),
            ("code/unix/linux_glimp.cpp", "window_width"),
        ):
            source = read_text(path)
            with self.subTest(path=path):
                self.assertIn("fnql::input::ProjectPointerToDrawable", source)
                self.assertIn("projection.drawableWidth = cls.glconfig.vidWidth;", source)
                self.assertIn("projection.drawableHeight = cls.glconfig.vidHeight;", source)
                self.assertIn(f"projection.hostWidth = {host};", source)

        # The Win32 message pump feeds the same lane and must project too.
        wndproc = read_text("code/win32/win_wndproc.cpp")
        self.assertIn("WIN_ProjectClientPointerToDrawable( &x, &y );", wndproc)
        self.assertIn(
            "void WIN_ProjectClientPointerToDrawable( int *x, int *y );",
            read_text("code/win32/win_local.h"),
        )
        # No backend may keep a private "raw host coordinates" lane any more.
        for path in ("code/sdl/sdl_input.cpp", "code/win32/win_wndproc.cpp"):
            self.assertNotIn("raw host-window coordinates", read_text(path))

    def test_sdl_uses_one_owner_keyed_dedup_cache_for_every_absolute_owner(self) -> None:
        source = read_text("code/sdl/sdl_input.cpp")
        queue = function_body(source, "IN_QueueAbsolutePointerPosition")

        # A cached sample from one coordinate space must not suppress the first
        # sample of the next.
        self.assertIn("s_absHaveLast && owner == s_absLastOwner", queue)
        self.assertIn("s_absLastOwner = owner;", queue)
        # The per-frame poll and the event path share that one cache.
        self.assertIn(
            "IN_QueueAbsolutePointerPosition( owner, x, y, in_eventTime );",
            function_body(source, "IN_PollAbsolutePointerPosition"),
        )
        self.assertNotIn("mouseAbsolutePositionValid", source)

    def test_sdl_events_route_by_owner_and_keep_position_before_click(self) -> None:
        source = read_text("code/sdl/sdl_input.cpp")
        events = function_body(source, "HandleEvents")

        motion_start = events.index("case SDL_EVENT_MOUSE_MOTION:")
        button_start = events.index("case SDL_EVENT_MOUSE_BUTTON_DOWN:")
        button_end = events.index("case SDL_EVENT_MOUSE_WHEEL:", button_start)
        motion_block = events[motion_start:button_start]
        button_block = events[button_start:button_end]

        for block in (motion_block, button_block):
            self.assertIn("const PointerOwner owner = IN_ResolvePointerOwner();", block)
            self.assertIn("fnql::input::PointerOwnerReportsAbsolute( owner )", block)
        self.assertLess(
            button_block.index("IN_QueueAbsolutePointerPosition( owner,"),
            button_block.index("SE_KEY"),
        )
        # Drag capture covers every absolute owner, matching Win32 and X11, so a
        # menu drag that leaves the window still delivers its release.
        self.assertIn("SDL_CaptureMouse( true );", button_block)
        self.assertIn("SDL_CaptureMouse( false );", button_block)
        self.assertIn("s_absCaptureButtons |= buttonMask", button_block)
        self.assertIn("s_absCaptureButtons &= ~buttonMask", button_block)

    def test_sdl_latches_the_applied_mode_and_only_recentres_on_entry(self) -> None:
        source = read_text("code/sdl/sdl_input.cpp")
        apply_mode = function_body(source, "IN_ApplyPointerMode")
        release = function_body(source, "IN_ReleasePointer")

        # A steady state must issue no SDL calls at all.
        self.assertIn("mode == s_pointerMode && !in_nograb->modified", apply_mode)
        self.assertIn("mode.relativeMotion != s_pointerMode.relativeMotion", apply_mode)
        self.assertIn("SDL_SetWindowMouseGrab( SDL_window, mode.confineToWindow )", apply_mode)
        self.assertIn("SDL_SetWindowRelativeMouseMode( SDL_window, mode.relativeMotion )", apply_mode)
        self.assertIn("IN_ShowCursor( mode.showSystemCursor ? qtrue : qfalse )", apply_mode)
        self.assertIn("!s_pointerModeValid || !s_pointerMode.recenterPointer", apply_mode)
        # Never warp a visible overlay cursor out from under the user.
        restore = function_body(source, "IN_RestoreDesktopPointer")
        self.assertIn(
            "fnql::input::PointerOwnerReportsAbsolute( previousOwner )", restore
        )
        self.assertIn("IN_EndTemporaryMouseCapture();", release)
        self.assertIn("s_pointerModeValid = qfalse;", release)

    def test_sdl_focus_lifecycle_releases_capture(self) -> None:
        source = read_text("code/sdl/sdl_input.cpp")
        window_event = function_body(source, "IN_HandleWindowEvent")

        focus_lost = window_event.index("case SDL_EVENT_WINDOW_FOCUS_LOST:")
        focus_gained = window_event.index("case SDL_EVENT_WINDOW_FOCUS_GAINED:")
        self.assertIn(
            "IN_EndTemporaryMouseCapture();",
            window_event[focus_lost:focus_gained],
        )

    def test_win32_shares_one_resolver_between_the_pump_and_the_frame(self) -> None:
        win_input = read_text("code/win32/win_input.cpp")
        win_local = read_text("code/win32/win_local.h")
        wndproc = read_text("code/win32/win_wndproc.cpp")

        # win_wndproc must not re-derive ownership with its own predicate.
        self.assertIn("fnql::input::PointerOwner WIN_ResolvePointerOwner( void );", win_local)
        self.assertIn("fnql::input::PointerOwner WIN_ResolvePointerOwner( void )", win_input)
        self.assertIn(
            "const fnql::input::PointerOwner pointerOwner = WIN_ResolvePointerOwner();",
            wndproc,
        )
        self.assertIn("pointerOwner != fnql::input::PointerOwner::Gameplay", wndproc)
        self.assertIn("pointerOwner == fnql::input::PointerOwner::Menu", wndproc)
        self.assertNotIn("WIN_ConsoleUsesAbsolutePointer", wndproc)
        # Extended buttons and drag capture stay intact.
        self.assertIn("case WM_XBUTTONDOWN:", wndproc)
        self.assertIn("case WM_XBUTTONUP:", wndproc)
        self.assertIn("K_MOUSE4 : K_MOUSE5", wndproc)
        self.assertIn("SetCapture( hWnd );", wndproc)
        self.assertIn("ReleaseCapture();", wndproc)

    def test_win32_stops_driving_overlays_while_unfocused_and_confines_fullscreen(self) -> None:
        win_input = read_text("code/win32/win_input.cpp")
        frame = function_body(win_input, "IN_Frame")
        confinement = function_body(win_input, "IN_SetPointerConfinement")
        activate = function_body(win_input, "IN_Activate")

        absolute_start = frame.index("PointerOwnerReportsAbsolute( owner )")
        absolute_block = frame[absolute_start:]
        # An unfocused or minimized window must not feed the menu cursor from the
        # desktop pointer, and must give back capture and confinement.
        self.assertIn("if ( !mode.driveInput ) {", absolute_block)
        self.assertLess(
            absolute_block.index("if ( !mode.driveInput ) {"),
            absolute_block.index("IN_WindowMouse();"),
        )
        self.assertIn("IN_SetPointerConfinement( mode.confineToWindow ? qtrue : qfalse );",
                      absolute_block)
        self.assertIn("ClipCursor( &window_rect );", confinement)
        self.assertIn("EqualRect( &window_rect, &s_pointerConfineRect )", confinement)
        self.assertIn("ClipCursor( NULL );", confinement)
        # Windows drops the clip region on deactivation; the latch must follow.
        self.assertIn("IN_SetPointerConfinement( qfalse );", activate)
        self.assertIn("IN_SetPointerConfinement( qfalse );",
                      function_body(win_input, "IN_Shutdown"))

    def test_x11_shares_one_grab_and_latches_the_cursor(self) -> None:
        source = read_text("code/unix/linux_glimp.cpp")
        show_cursor = function_body(source, "IN_ShowWindowCursor")
        apply_grab = function_body(source, "IN_ApplyPointerGrab")
        capture = function_body(source, "IN_BeginTemporaryPointerCapture")
        end_capture = function_body(source, "IN_EndTemporaryPointerCapture")
        frame = function_body(source, "IN_Frame")

        # IN_Frame evaluates the cursor every frame; without a latch that is an X
        # round trip per frame for as long as an overlay is open.
        self.assertIn("window_cursor_valid && window_cursor_shown == show", show_cursor)
        # One client grab serves both confinement and drag capture.
        self.assertIn("( reasons & POINTER_GRAB_CONFINE ) ? win : None", apply_grab)
        self.assertIn("XUngrabPointer( dpy, CurrentTime );", apply_grab)
        self.assertIn("pointer_grab_reasons | POINTER_GRAB_DRAG", capture)
        self.assertIn("pointer_grab_reasons & ~POINTER_GRAB_DRAG", end_capture)
        self.assertIn("button >= 4 && button <= 7", capture)
        self.assertIn("IN_SetPointerConfinement( mode.confineToWindow ? qtrue : qfalse );", frame)
        self.assertIn("IN_ShowWindowCursor( mode.showSystemCursor ? qtrue : qfalse );", frame)

    def test_x11_console_precedence_initial_poll_and_focus_release(self) -> None:
        source = read_text("code/unix/linux_glimp.cpp")
        poll = function_body(source, "IN_PollAbsolutePointerPosition")
        frame = function_body(source, "IN_Frame")

        self.assertIn("XQueryPointer", poll)
        self.assertIn("IN_QueueAbsolutePointerPosition", poll)
        self.assertIn("IN_PollAbsolutePointerPosition();", frame)
        self.assertIn("absolute_position_valid = qfalse", frame)
        focus_out = source.index("Com_DPrintf( \"FocusOut\\n\" );")
        focus_block = source[source.index("case FocusIn:") : focus_out]
        self.assertIn("IN_EndTemporaryPointerCapture();", focus_block)
        self.assertIn("IN_SetPointerConfinement( qfalse );", focus_block)
        self.assertIn("IN_ShowWindowCursor( qtrue );", focus_block)
        # A recreated window inherits neither the grab nor the cursor attribute.
        self.assertIn("window_cursor_valid = qfalse;", source)
        self.assertIn("pointer_grab_reasons = 0;", source)


if __name__ == "__main__":
    unittest.main()
