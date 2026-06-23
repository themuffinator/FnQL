import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_text(relative_path):
    return (ROOT / relative_path).read_text(encoding="utf-8")


class VidRestartFastSourceTests(unittest.TestCase):
    """Source contract for the safe windowed/fullscreen toggle via \\vid_restart fast."""

    def test_vid_restart_fast_maps_to_keep_window(self):
        source = read_text("code/client/cl_main.cpp")
        self.assertIn(
            'Q_stricmp( Cmd_Argv( 1 ), "keep_window" ) == 0 || Q_stricmp( Cmd_Argv( 1 ), "fast" ) == 0',
            source,
        )
        self.assertIn("CL_Vid_Restart( REF_KEEP_WINDOW );", source)

    def test_renderer_unload_invalidates_platform_config_pointer(self):
        source = read_text("code/client/cl_main.cpp")
        unload = source.index("Sys_UnloadLibrary( rendererLib );")
        invalidate = source.index("GLimp_InvalidateConfig();", unload)
        renderer_started = source.index("cls.rendererStarted = qfalse;", unload)
        self.assertLess(
            invalidate,
            renderer_started,
            "GLimp_InvalidateConfig() must run right after the renderer DLL is unloaded",
        )

        header = read_text("code/client/client.h")
        self.assertIn("void\tGLimp_InvalidateConfig( void );", header)

        for path in (
            "code/sdl/sdl_glimp.cpp",
            "code/win32/win_glimp.cpp",
            "code/unix/linux_glimp.cpp",
        ):
            with self.subTest(path=path):
                impl = read_text(path)
                self.assertIn("void GLimp_InvalidateConfig( void )", impl)
                body_start = impl.index("void GLimp_InvalidateConfig( void )")
                body = impl[body_start:body_start + 200]
                self.assertIn("glw_state.config = NULL;", body)

    def test_sdl_alt_enter_uses_fast_restart(self):
        source = read_text("code/sdl/sdl_input.cpp")
        handler_start = source.index("key == K_ENTER && keys[K_ALT].down")
        handler = source[handler_start:handler_start + 400]
        self.assertIn('Cvar_SetIntegerValue( "r_fullscreen", glw_state.isFullscreen ? 0 : 1 );', handler)
        self.assertIn('Cbuf_AddText( "vid_restart fast\\n" );', handler)

    def test_sdl_set_mode_attempts_window_reuse_before_destroy(self):
        source = read_text("code/sdl/sdl_glimp.cpp")

        reuse_call = source.index("reusedWindow = GLW_ReuseExistingWindow( config, display, fullscreen, vulkan,")
        destroy_call = source.index("GLW_DestroyWindow();", reuse_call)
        self.assertLess(reuse_call, destroy_call,
                        "window reuse must be attempted before the destroy fallback")

        # the create loop must be skipped entirely after a successful reuse
        self.assertIn("for ( i = 0; i < 16 && !reusedWindow; i++ )", source)

    def test_sdl_window_recipe_guards_reuse(self):
        source = read_text("code/sdl/sdl_glimp.cpp")

        self.assertIn("static windowRecipe_t glw_windowRecipe;", source)

        guard_start = source.index("static qboolean GLW_CanReuseWindow(")
        guard_end = source.index("static qboolean GLW_ReuseExistingWindow(", guard_start)
        guard = source[guard_start:guard_end]
        for field in (
            "glw_windowRecipe.valid",
            "glw_windowRecipe.vulkan != vulkan",
            "glw_windowRecipe.colorBits != colorBits",
            "glw_windowRecipe.depthBits != depthBits",
            "glw_windowRecipe.stencilBits != stencilBits",
            "glw_windowRecipe.stereo",
            "glw_windowRecipe.software",
            "glw_windowRecipe.floatFramebuffer != requestFloatFramebuffer",
        ):
            self.assertIn(field, guard)

        # destroying the window must drop the recorded recipe
        destroy_start = source.index("static void GLW_DestroyWindow( void )")
        destroy_end = source.index("GLimp_Shutdown", destroy_start)
        destroy = source[destroy_start:destroy_end]
        self.assertIn("Com_Memset( &glw_windowRecipe, 0, sizeof( glw_windowRecipe ) );", destroy)

    def test_sdl_reuse_path_handles_both_directions_and_focus_state(self):
        source = read_text("code/sdl/sdl_glimp.cpp")

        reuse_start = source.index("static qboolean GLW_ReuseExistingWindow(")
        reuse_end = source.index("GLimp_SetMode", reuse_start)
        reuse = source[reuse_start:reuse_end]

        # fullscreen direction goes through the shared mode/fallback helper
        self.assertIn("GLW_ApplyFullscreen( config, display, colorBits )", reuse)

        # windowed direction leaves fullscreen and restores borders/geometry
        self.assertIn("SDL_SetWindowFullscreen( SDL_window, false )", reuse)
        self.assertIn("SDL_SetWindowBordered( SDL_window, r_noborder->integer ? false : true );", reuse)
        self.assertIn("SDL_SetWindowSize( SDL_window, config->vidWidth, config->vidHeight );", reuse)
        self.assertIn("SDL_SetWindowPosition( SDL_window, x, y );", reuse)

        # a reused window receives no focus events, so flags must be derived live
        self.assertIn("gw_active = ( windowFlags & SDL_WINDOW_INPUT_FOCUS ) ? qtrue : qfalse;", reuse)
        self.assertIn("gw_minimized = ( windowFlags & SDL_WINDOW_MINIMIZED ) ? qtrue : qfalse;", reuse)

        self.assertIn("...reusing existing window", reuse)

    def test_sdl_hit_test_cleared_when_leaving_borderless_windowed(self):
        source = read_text("code/sdl/sdl_glimp.cpp")
        set_index = source.index("SDL_SetWindowHitTest( SDL_window, SDL_HitTestFunc, NULL );")
        clear_index = source.index("SDL_SetWindowHitTest( SDL_window, NULL, NULL );")
        self.assertLess(set_index, clear_index)

    def test_win32_kept_window_refreshes_styles_and_fullscreen_state(self):
        source = read_text("code/win32/win_glimp.cpp")

        kept_start = source.index("window is kept across \\vid_restart fast")
        kept_block = source[kept_start:kept_start + 2200]

        self.assertIn("SetWindowLong( g_wv.hWnd, GWL_EXSTYLE, exstyle );", kept_block)
        self.assertIn("SetWindowLong( g_wv.hWnd, GWL_STYLE, stylebits );", kept_block)
        self.assertIn("SWP_FRAMECHANGED | SWP_NOZORDER | SWP_SHOWWINDOW", kept_block)
        self.assertIn("glw_state.cdsFullscreen = cdsFullscreen;", kept_block)
        self.assertIn("glw_state.config->vidWidth = r.right - r.left;", kept_block)


if __name__ == "__main__":
    unittest.main()
