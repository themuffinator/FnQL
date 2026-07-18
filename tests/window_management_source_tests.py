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
        scheduler = read_text("code/client/window_resize.hpp")

        self.assertIn("WindowResizeScheduler", source)
        self.assertIn("kDebounceMilliseconds = 100", scheduler)
        self.assertIn("now - deadline < 0x80000000u", scheduler)
        self.assertIn("ConsumeIfReady", source)
        self.assertIn('Cvar_SetIntegerValue( "r_customWidth", request.width );', source)
        self.assertIn('Cvar_SetIntegerValue( "r_customHeight", request.height );', source)
        self.assertIn('Cvar_Set( "r_mode", "-1" );', source)
        self.assertIn('Cvar_Set2( "r_windowedWidth", r_customwidth->string', source)
        self.assertIn('Cvar_Set2( "r_windowedHeight", r_customheight->string', source)
        self.assertIn("request.preserveWindow ? REF_KEEP_WINDOW : REF_DESTROY_WINDOW", source)
        self.assertNotIn('Cbuf_AddText( "vid_restart fast window_resize', source)
        self.assertIn("if ( gw_minimized ||", source)
        self.assertIn("cl_windowModeChange", source)
        self.assertIn("CL_IsWindowResizeRestart()", read_text("code/sdl/sdl_glimp.cpp"))

    def test_canvas_geometry_is_refreshed_before_console_and_web_surfaces(self) -> None:
        client = read_text("code/client/cl_main.cpp")
        geometry = read_text("code/client/canvas_geometry.hpp")
        webui = read_text("code/client/cl_webui.cpp")

        self.assertIn("CalculateCanvasGeometry", geometry)
        canvas_update = client.index("fnql::client::CalculateCanvasGeometry")
        console_reflow = client.index("Con_CheckResize();", canvas_update)
        web_resize = client.index("CL_WebHost_RefreshSurfaceSize();", canvas_update)
        self.assertLess(canvas_update, console_reflow)
        self.assertLess(console_reflow, web_resize)
        self.assertIn("void CL_WebHost_RefreshSurfaceSize( void )", webui)
        self.assertIn("CL_Awesomium_Resize( desired.width, desired.height )", webui)
        self.assertIn("requestedSurfaceSize", webui)

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
        self.assertIn("case WM_GETMINMAXINFO:", wndproc)
        self.assertIn("WIN_ApplyMinimumTrackSize", wndproc)
        self.assertIn("GetClientRect( hWnd, &clientRect )", wndproc)
        self.assertIn("CL_CompleteWindowResize();", wndproc)
        self.assertIn("clientWidth == glw_state.config->vidWidth", wndproc)
        self.assertIn("CL_CancelWindowResize();", wndproc)

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
            "code/rendererrtx/tr_init.c",
        ):
            with self.subTest(path=path):
                source = read_text(path)
                keep = source.index("code != REF_KEEP_WINDOW")
                block = source[keep : keep + 800]
                self.assertIn("Com_Memset( &glConfig, 0, sizeof( glConfig ) );", block)


if __name__ == "__main__":
    unittest.main()
