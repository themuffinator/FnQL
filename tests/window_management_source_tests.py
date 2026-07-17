import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_text(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


class WindowManagementSourceTests(unittest.TestCase):
    def test_sdl_window_is_resizable_and_decoration_aware(self) -> None:
        glimp = read_text("code/sdl/sdl_glimp.cpp")
        header = read_text("code/sdl/sdl_glw.h")

        self.assertIn("SDL_SetWindowResizable( SDL_window, true )", glimp)
        self.assertIn("SDL_SetWindowMinimumSize( SDL_window, 320, 240 )", glimp)
        self.assertIn("SDL_GetDisplayUsableBounds", glimp)
        self.assertIn("SDL_GetWindowBordersSize", glimp)
        self.assertIn("GLW_EnsureWindowOnScreen", glimp)
        self.assertIn("SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY", glimp)
        self.assertIn("int pixel_width;", header)
        self.assertIn("int pixel_height;", header)

    def test_sdl_tracks_display_topology_and_drawable_resize(self) -> None:
        source = read_text("code/sdl/sdl_input.cpp")

        for event in (
            "SDL_EVENT_DISPLAY_ADDED",
            "SDL_EVENT_DISPLAY_REMOVED",
            "SDL_EVENT_DISPLAY_MOVED",
            "SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED",
            "SDL_EVENT_DISPLAY_USABLE_BOUNDS_CHANGED",
            "SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED",
        ):
            self.assertIn(event, source)
        self.assertIn("CL_NotifyWindowResize( glw_state.window_width", source)
        self.assertIn("GLW_EnsureWindowOnScreen();", source)

    def test_resize_refresh_is_debounced_and_persistent(self) -> None:
        source = read_text("code/client/cl_main.cpp")

        self.assertIn("cl_windowResizeDeadline = Sys_Milliseconds() + 250;", source)
        self.assertIn('Cvar_SetIntegerValue( "r_customWidth", cl_windowResizeWidth );', source)
        self.assertIn('Cvar_SetIntegerValue( "r_customHeight", cl_windowResizeHeight );', source)
        self.assertIn('Cvar_Set( "r_mode", "-1" );', source)
        self.assertIn('Cbuf_AddText( "vid_restart fast window_resize\\n" );', source)
        self.assertIn("cl_windowModeChange", source)
        self.assertIn("CL_IsWindowResizeRestart()", read_text("code/sdl/sdl_glimp.cpp"))

    def test_native_windows_supports_snap_dpi_and_work_area_recovery(self) -> None:
        local = read_text("code/win32/win_local.h")
        glimp = read_text("code/win32/win_glimp.cpp")
        wndproc = read_text("code/win32/win_wndproc.cpp")

        self.assertIn("WS_MAXIMIZEBOX|WS_THICKFRAME", local)
        self.assertIn("AdjustWindowRectExForDpi", glimp)
        self.assertIn("glw_state.workArea", glimp)
        self.assertIn("case WM_DPICHANGED:", wndproc)
        self.assertIn("case WM_DISPLAYCHANGE:", wndproc)
        self.assertIn("case WM_SETTINGCHANGE:", wndproc)
        self.assertIn("CL_NotifyWindowResize( LOWORD( lParam ), HIWORD( lParam ), qtrue );", wndproc)

    def test_native_x11_no_longer_locks_window_size(self) -> None:
        source = read_text("code/unix/linux_glimp.cpp")
        hints_start = source.index("memset( &sizehints")
        hints = source[hints_start : hints_start + 350]

        self.assertIn("sizehints.flags = PMinSize;", hints)
        self.assertNotIn("PMaxSize", hints)
        self.assertIn("sizehints.min_width = 320;", hints)
        self.assertIn("CL_NotifyWindowResize( event.xconfigure.width", source)
        self.assertIn('"_NET_FRAME_EXTENTS"', source)
        self.assertIn('"_NET_WORKAREA"', source)
        self.assertIn("X11_EnsureWindowOnScreen", source)

    def test_keep_window_restart_reenters_platform_mode_setup(self) -> None:
        for path in (
            "code/renderer/tr_init.c",
            "code/renderervk/tr_init.c",
            "code/renderer2/tr_init.c",
        ):
            with self.subTest(path=path):
                source = read_text(path)
                keep = source.index("code != REF_KEEP_WINDOW")
                block = source[keep : keep + 800]
                self.assertIn("Com_Memset( &glConfig, 0, sizeof( glConfig ) );", block)


if __name__ == "__main__":
    unittest.main()
