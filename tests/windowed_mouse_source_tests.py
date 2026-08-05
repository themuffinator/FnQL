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
        coordinate = function_body(policy, "ProjectPointerCoordinate")

        self.assertIn(
            "x, projection.hostWidth, projection.drawableWidth", project
        )
        self.assertIn(
            "y, projection.hostHeight, projection.drawableHeight", project
        )
        # Unknown geometry passes the coordinate through instead of zeroing it,
        # while large products use a defined, saturating integer path.
        self.assertIn("hostExtent <= 0 || drawableExtent <= 0", coordinate)
        self.assertIn("return value;", coordinate)
        self.assertIn("static_cast<std::int64_t>( value )", coordinate)
        self.assertIn("SaturatingIntFromInt64( scaled )", coordinate)

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

    def test_retail_modules_receive_the_public_supersampled_pointer_space(self) -> None:
        """Supersampling doubles the renderer's private target, but
        CL_CopyRetailGlconfig hands native retail modules the public capture
        dimensions. _UI_MouseEvent divides by that glconfig and discards any
        result outside 640x480, so the drawable position has to be converted
        back before dispatch or an in-game menu tracks at double speed and then
        stops responding outside its top-left quadrant."""
        client_input = read_text("code/client/cl_input.cpp")
        convert = function_body(client_input, "CL_ProjectDrawableToRetailModule")

        # Private renderer target in, public retail module space out.
        self.assertIn("projection.hostWidth = cls.glconfig.vidWidth;", convert)
        self.assertIn("projection.hostHeight = cls.glconfig.vidHeight;", convert)
        self.assertIn("projection.drawableWidth = cls.captureWidth;", convert)
        self.assertIn("projection.drawableHeight = cls.captureHeight;", convert)
        # Reuse the shared truncating projection, so an unset capture size is an
        # identity and a position strictly inside the drawable stays strictly
        # inside the module's space.
        self.assertIn("fnql::input::ProjectPointerToDrawable( *x, *y, projection )", convert)
        # Bytecode modules read the private dimensions through CL_GetGlconfig and
        # draw through the unscaled syscall lane, so they keep the drawable space.
        self.assertIn("if ( !vm || !vm->dllExports ) {", convert)

        dispatch = function_body(client_input, "CL_MouseAbsoluteEvent")
        self.assertIn("CL_ProjectDrawableToRetailModule( uivm, &x, &y );", dispatch)
        self.assertIn("CL_ProjectDrawableToRetailModule( cgvm, &x, &y );", dispatch)
        # The console and the WebUI browser address the private target directly
        # and must not be converted.
        self.assertIn("Con_SetMousePos( x, y );", dispatch)
        self.assertIn("CL_WebView_OnMouseMove( x, y );", dispatch)

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
        self.assertIn("e.button.y, in_eventTime, qtrue", button_block)
        # Drag capture covers every absolute owner, matching Win32 and X11, so a
        # menu drag that leaves the window still delivers its release. Requested
        # and applied capture remain separate so failed transitions are retried.
        self.assertIn("s_absCaptureButtons |= buttonMask", button_block)
        self.assertIn("s_absCaptureButtons &= ~buttonMask", button_block)
        self.assertEqual(
            button_block.count("IN_UpdateTemporaryMouseCapture();"), 2
        )

        update_capture = function_body(
            source, "IN_UpdateTemporaryMouseCapture"
        )
        self.assertIn(
            "const qboolean requested = s_absCaptureButtons ? qtrue : qfalse;",
            update_capture,
        )
        self.assertIn("requested == s_absCaptureActive", update_capture)
        self.assertIn(
            "SDL_CaptureMouse( requested != qfalse )", update_capture
        )
        self.assertLess(
            update_capture.index("return;", update_capture.index("SDL_CaptureMouse")),
            update_capture.index("s_absCaptureActive = requested;"),
        )

        release_capture = function_body(source, "IN_EndTemporaryMouseCapture")
        self.assertLess(
            release_capture.index("s_absCaptureButtons = 0;"),
            release_capture.index("IN_UpdateTemporaryMouseCapture();"),
        )

    def test_sdl_latches_the_applied_mode_and_only_recentres_on_entry(self) -> None:
        source = read_text("code/sdl/sdl_input.cpp")
        apply_mode = function_body(source, "IN_ApplyPointerMode")
        release = function_body(source, "IN_ReleasePointer")

        # Pointer-mode calls remain transition-latched. Temporary drag capture
        # is reconciled before that early return so a failed capture/release can
        # be retried on the next frame.
        self.assertLess(
            apply_mode.index("IN_UpdateTemporaryMouseCapture();"),
            apply_mode.index(
                "mode == s_pointerMode && !in_nograb->modified"
            ),
        )
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
            "IN_QueueInputReset( qfalse );",
            window_event[focus_lost:focus_gained],
        )
        self.assertIn(
            "IN_ResetInputState();",
            function_body(source, "IN_QueueInputReset"),
        )
        self.assertIn(
            "IN_EndTemporaryMouseCapture();",
            function_body(source, "IN_ResetInputState"),
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
        self.assertIn(
            "fnql::input::PointerOwnerReportsAbsolute( pointerOwner )", wndproc
        )
        self.assertNotIn("WIN_ConsoleUsesAbsolutePointer", wndproc)
        # Extended buttons and drag capture stay intact.
        self.assertIn("case WM_XBUTTONDOWN:", wndproc)
        self.assertIn("case WM_XBUTTONUP:", wndproc)
        self.assertIn("K_MOUSE4 : K_MOUSE5", wndproc)
        self.assertIn("SetCapture( hWnd );", wndproc)
        self.assertIn("WIN_ReleaseTemporaryMouseCapture();", wndproc)
        release = function_body(wndproc, "WIN_ReleaseTemporaryMouseCapture")
        self.assertIn("if ( !ReleaseCapture() )", release)
        self.assertLess(
            release.index("return;"),
            release.index("temporaryMouseCapture = qfalse;"),
        )

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
        self.assertIn("if ( ClipCursor( &window_rect ) )", confinement)
        self.assertIn("s_pointerConfined = qtrue;", confinement)
        self.assertIn("EqualRect( &window_rect, &s_pointerConfineRect )", confinement)
        self.assertIn("if ( ClipCursor( NULL ) )", confinement)
        self.assertIn("s_pointerConfined = qfalse;", confinement)
        # Windows drops the clip region on deactivation; the latch must follow.
        self.assertIn("IN_SetPointerConfinement( qfalse );", activate)
        self.assertIn("IN_SetPointerConfinement( qfalse );",
                      function_body(win_input, "IN_Shutdown"))

    def test_win32_cursor_transitions_are_latched_and_nograb_is_gameplay_only(
        self,
    ) -> None:
        win_input = read_text("code/win32/win_input.cpp")
        cursor = function_body(win_input, "IN_SetSystemCursorVisible")
        frame = function_body(win_input, "IN_Frame")
        mouse_active = function_body(win_input, "IN_MouseActive")

        assert_latch = (
            "s_cursorVisibilityRequestValid &&\n"
            "\t\ts_cursorVisibilityRequested == visible"
        )
        self.assertIn(assert_latch, cursor)
        self.assertLess(cursor.index(assert_latch), cursor.index("ShowCursor("))
        self.assertIn("s_cursorVisibilityRequestValid = qtrue;", cursor)
        self.assertIn("s_cursorVisibilityRequested = visible;", cursor)

        absolute_start = frame.index("PointerOwnerReportsAbsolute( owner )")
        gameplay_start = frame.rindex("WIN_ReleaseTemporaryMouseCapture();")
        absolute = frame[absolute_start:gameplay_start]
        gameplay = frame[gameplay_start:]
        self.assertIn("IN_SetSystemCursorVisible( qtrue );", absolute)
        self.assertNotIn("in_nograb->integer", absolute)
        self.assertIn("!mode.driveInput || in_nograb->integer", gameplay)

        self.assertIn("in_nograb->integer == 0", mouse_active)
        self.assertIn("gw_active && WIN_WindowFocused()", mouse_active)
        self.assertIn("!gw_minimized", mouse_active)

    def test_win32_legacy_mouse_messages_only_drive_the_legacy_source(self) -> None:
        """A legacy WM_MOUSEMOVE that arrives while raw input or DirectInput owns
        the device was queued before (re)registration: a stale position from the
        click that closed an in-game menu, or from crossing the window while
        regaining focus. Converting it into a delta kicks the view by
        (position - window centre) with no physical mouse motion, so the pump
        must feed the legacy lane only while the legacy Win32 mouse is the
        active input source."""
        win_input = read_text("code/win32/win_input.cpp")
        win_local = read_text("code/win32/win_local.h")
        wndproc = read_text("code/win32/win_wndproc.cpp")

        gate = function_body(win_input, "IN_LegacyMouseDrivesInput")
        self.assertIn("IN_MouseActive()", gate)
        self.assertIn("s_legacyMouseDriving", gate)
        self.assertIn("qboolean IN_LegacyMouseDrivesInput( void );", win_local)
        # The pump consults the gate before synthesizing gameplay deltas, and
        # still swallows the stale message rather than handing it to Windows.
        self.assertIn("if ( IN_LegacyMouseDrivesInput() ) {", wndproc)
        legacy_gate = wndproc.index("if ( IN_LegacyMouseDrivesInput() ) {")
        legacy_call = wndproc.index("IN_Win32MouseEvent(", legacy_gate)
        self.assertLess(
            legacy_gate,
            legacy_call,
        )
        legacy_block = wndproc[legacy_call : wndproc.index(
            "return WIN_MouseMessageResult( uMsg );", legacy_call
        )]
        self.assertIn(
            "static_cast<int>( static_cast<short>( LOWORD( lParam ) ) )",
            legacy_block,
        )
        self.assertIn(
            "static_cast<int>( static_cast<short>( HIWORD( lParam ) ) )",
            legacy_block,
        )

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
