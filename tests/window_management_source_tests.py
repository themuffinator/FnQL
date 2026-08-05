import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_text(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


class WindowManagementSourceTests(unittest.TestCase):
    def test_sdl_window_is_resizable_and_decoration_aware(self) -> None:
        glimp = read_text("code/sdl/sdl_glimp.cpp")
        input_source = read_text("code/sdl/sdl_input.cpp")
        header = read_text("code/sdl/sdl_glw.h")

        self.assertIn("SDL_SetWindowResizable( SDL_window, true )", glimp)
        self.assertIn("SDL_SetWindowMinimumSize( SDL_window, 320, 240 )", glimp)
        self.assertIn("SDL_GetDisplayUsableBounds", glimp)
        self.assertIn("SDL_GetWindowBordersSize", glimp)
        self.assertIn("OuterBoundsFromClient", glimp)
        self.assertIn("FNQL_MacGetWindowBordersSize", glimp)
        self.assertIn("GLW_CanPositionTopLevelWindows", glimp)
        self.assertIn("GLW_EnsureWindowOnScreen", glimp)
        self.assertIn("case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:", input_source)
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
        self.assertIn('Cvar_Set2( "r_windowedMode", "-1", qtrue );', source)
        self.assertIn('Cvar_Set2( "r_windowedWidth", va( "%d", request.width )', source)
        self.assertIn('Cvar_Set2( "r_windowedHeight", va( "%d", request.height )', source)
        self.assertNotIn('Cvar_Set( "r_mode", "-1" );', source)
        self.assertIn("request.preserveWindow ? REF_KEEP_WINDOW : REF_DESTROY_WINDOW", source)
        self.assertNotIn('Cbuf_AddText( "vid_restart fast window_resize', source)
        self.assertIn("if ( gw_minimized ||", source)
        self.assertIn("cl_windowModeChange", source)
        self.assertIn("CL_IsWindowResizeRestart()", read_text("code/sdl/sdl_glimp.cpp"))

    def test_retail_windowed_mode_does_not_overwrite_r_mode(self) -> None:
        client = read_text("code/client/cl_main.cpp")
        self.assertIn("int CL_GetRequestedMode( qboolean fullscreen )", client)
        self.assertIn("if ( !fullscreen && r_windowedMode )", client)
        self.assertNotIn('Cvar_Set( "r_mode", r_windowedMode->string );', client)
        self.assertIn("*width = r_windowedWidth->integer;", client)
        self.assertIn("*height = r_windowedHeight->integer;", client)

        for path in (
            "code/sdl/sdl_glimp.cpp",
            "code/win32/win_glimp.cpp",
            "code/unix/linux_glimp.cpp",
        ):
            with self.subTest(path=path):
                self.assertIn("CL_GetRequestedMode(", read_text(path))

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

    def test_fullscreen_mode_comes_from_the_display_mode_list(self) -> None:
        glimp = read_text("code/sdl/sdl_glimp.cpp")

        # A hand-built SDL_DisplayMode never compares equal to an enumerated one,
        # so SDL rejects it and every fullscreen switch silently degrades to
        # desktop resolution. The request has to be resolved against the display.
        self.assertIn(
            "SDL_GetClosestFullscreenDisplayMode( display, config->vidWidth,", glimp
        )
        self.assertNotIn("SDL_PIXELFORMAT_RGB24", glimp)
        self.assertNotIn("mode.w = config->vidWidth;", glimp)

        # Desktop-sized requests must stay on borderless desktop fullscreen so
        # the default configuration performs no display mode change.
        self.assertIn("desktopMode->w != config->vidWidth", glimp)
        self.assertIn("desktopMode->h != config->vidHeight", glimp)
        self.assertIn('r_displayRefresh', glimp)

    def test_windows_resource_rebuilds_when_the_manifest_changes(self) -> None:
        build = read_text("meson.build")

        # Neither rc nor windres emits a depfile. Without depend_files a manifest
        # or icon edit leaves the previous resource blob linked in, so the change
        # silently does nothing until the build tree is wiped.
        self.assertIn("win_resource_deps = files(", build)
        for dependency in (
            "code/win32/q3.manifest",
            "code/win32/resource.h",
            "code/win32/fnql.ico",
            "version/fnql_version.h",
        ):
            self.assertIn(f"'{dependency}'", build)
        self.assertEqual(2, build.count("depend_files: win_resource_deps"))

    def test_windows_manifest_declares_per_monitor_v2_dpi_awareness(self) -> None:
        manifest = read_text("code/win32/q3.manifest")
        glimp = read_text("code/win32/win_glimp.cpp")

        # Per-Monitor V1 blocks the awareness level SDL requests for itself and
        # the one its window sizing assumes.
        self.assertIn("PerMonitorV2", manifest)
        self.assertIn(
            "http://schemas.microsoft.com/SMI/2016/WindowsSettings", manifest
        )

        # A DPI-aware process already reports monitor rects in physical pixels.
        # Replacing the extent with the driver mode while keeping the monitor
        # origin desynchronizes fullscreen geometry.
        self.assertNotIn("w = devMode.dmPelsWidth;", glimp)
        self.assertNotIn("h = devMode.dmPelsHeight;", glimp)

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

    def test_native_windows_fullscreen_keeps_the_desktop_refresh_rate(self) -> None:
        """Without DM_DISPLAYFREQUENCY the driver picks the mode's default rate,
        typically 60Hz, whenever the fullscreen resolution differs from the
        desktop's. The mode set must request the desktop rate and gracefully
        retry at the driver default if this resolution cannot support it."""
        glimp = read_text("code/win32/win_glimp.cpp")

        self.assertIn("dm.dmDisplayFrequency = dm_desktop.dmDisplayFrequency;", glimp)
        self.assertIn("dm.dmPelsHeight <= dm_desktop.dmPelsHeight", glimp)
        self.assertIn("...using desktop refresh rate: %iHz\\n", glimp)
        # Fallback: strip the frequency and retry before hunting for a larger mode.
        self.assertIn("dm.dmFields &= ~DM_DISPLAYFREQUENCY;", glimp)
        retry = glimp.index("dm.dmFields &= ~DM_DISPLAYFREQUENCY;")
        self.assertIn("ApplyDisplaySettings( &dm )", glimp[retry : retry + 200])
        # An explicit r_displayRefresh remains authoritative: no silent retry.
        self.assertIn('!Cvar_VariableIntegerValue( "r_displayRefresh" )', glimp)

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
        self.assertIn("OuterBoundsFromClient", source)
        self.assertIn("OuterOriginFromClient", source)
        self.assertIn("requestedWindowOrigin", source)

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
