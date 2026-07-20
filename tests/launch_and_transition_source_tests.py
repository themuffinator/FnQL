from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str:
    start = source.index(f"{name}( void )")
    opening = source.index("{", start)
    depth = 1
    for index in range(opening + 1, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"Unterminated {name}")


class LaunchAndTransitionSourceTests(unittest.TestCase):
    def test_vscode_launches_force_steam_and_build_matching_target(self) -> None:
        launch = json.loads(read_source(".vscode/launch.json"))

        self.assertTrue(launch["configurations"])
        for configuration in launch["configurations"]:
            args = configuration["args"]
            steam_index = args.index("com_steamIntegration")
            self.assertEqual(args[steam_index + 1], "1", configuration["name"])
            if configuration["name"].startswith("RTX "):
                expected_task = "meson: build RTX (Steam)"
            else:
                expected_task = "meson: build (Steam)"
            self.assertEqual(
                configuration.get("preLaunchTask"), expected_task, configuration["name"]
            )
            self.assertNotIn("disabled", configuration["name"].lower())
            self.assertIn("Retail QL / Win32", configuration["name"])

            if "Retail QL / Win32" in configuration["name"]:
                expected_build_dir = (
                    "meson\\build\\win32-rtx\\"
                    if configuration["name"].startswith("RTX ")
                    else "meson\\build\\win32\\"
                )
                self.assertIn(expected_build_dir, configuration["program"])
                self.assertNotIn("win32-debug", configuration["program"])
                if "Dedicated" not in configuration["name"]:
                    webui_index = args.index("cl_webuiEnable")
                    self.assertEqual(args[webui_index + 1], "1", configuration["name"])

    def test_vscode_build_tasks_stage_the_steam_provider(self) -> None:
        tasks = json.loads(read_source(".vscode/tasks.json"))["tasks"]
        build_tasks = [task for task in tasks if task.get("label", "").startswith("meson: build")]

        self.assertTrue(build_tasks)
        for task in build_tasks:
            self.assertIn("-WithSteam", task["args"], task["label"])
            platform_index = task["args"].index("-Platform")
            self.assertEqual(task["args"][platform_index + 1], "Win32")
            self.assertNotIn("-RunTests", task["args"])

        self.assertNotIn("meson: test", {task.get("label") for task in tasks})

    def test_game_transitions_release_the_browser_without_shutting_down_the_host(self) -> None:
        main = read_source("code/client/cl_main.cpp")
        map_loading = function_body(main, "CL_MapLoading")
        connect = function_body(main, "CL_Connect_f")
        webui = read_source("code/client/cl_webui.cpp")

        self.assertIn("CL_WebHost_HideForGameTransition();", map_loading)
        self.assertNotIn("CL_WebHost_Shutdown();", map_loading)
        self.assertIn("CL_Disconnect( qfalse );", map_loading)
        self.assertNotIn("CL_Disconnect( qtrue );", map_loading)
        self.assertIn("CL_WebHost_HideForGameTransition();", connect)
        self.assertLess(
            connect.index("CL_WebView_PublishGameStartForAddress"),
            connect.index("CL_WebHost_HideForGameTransition();"),
        )

        transition = function_body(webui, "CL_WebHost_HideForGameTransition")
        self.assertIn("cl_webui.keyCaptureArmed = qfalse;", transition)
        self.assertIn("CL_WebHost_HideBrowser();", transition)

        self.assertIn("static qboolean CL_WebHost_CommandStartsGameTransition", webui)
        native_request = webui[webui.index("static void CL_WebHost_ProcessNativeJavascriptRequest") :]
        hide = native_request.index("CL_WebHost_HideForGameTransition();")
        queue = native_request.index("Cbuf_ExecuteText( EXEC_APPEND")
        self.assertLess(hide, queue)

    def test_connection_and_level_load_screens_redraw_after_browser_release(self) -> None:
        screen = read_source("code/client/cl_scrn.cpp")
        main = read_source("code/client/cl_main.cpp")

        self.assertIn("uiMenuVisible = CL_UIMenusAreVisible();", screen)
        self.assertIn("cls.state == CA_LOADING || cls.state == CA_PRIMED || cls.state == CA_ACTIVE", screen)
        self.assertIn("uiFullscreen = false;", screen)
        self.assertIn("static void SCR_DrawNonGameBackdrop( void )", screen)
        self.assertIn("if ( cls.state != CA_ACTIVE )", screen)
        self.assertIn("SCR_DrawNonGameBackdrop();", screen)
        self.assertIn("drawConnectScreen = cls.state == CA_CONNECTING", screen)
        self.assertIn("browserOverlayAllowed = !drawConnectScreen && cls.state != CA_LOADING", screen)
        self.assertIn("&& cls.state != CA_PRIMED;", screen)
        self.assertIn("browserOverlayRequested = browserOverlayAllowed", screen)
        self.assertIn("uivm && ( !uiFullscreen || drawConnectScreen )", screen)
        browser_draw = screen[screen.index("// Native connection and level-loading screens"):]
        self.assertIn("if ( browserOverlayAllowed )", browser_draw)
        self.assertIn("CL_WebHost_DrawBrowserSurface();", browser_draw)
        self.assertIn("UI_DRAW_CONNECT_SCREEN, qfalse", screen)
        self.assertIn("UI_DRAW_CONNECT_SCREEN, qtrue", screen)

        self.assertIn("!uiMenuVisible", screen)
        self.assertIn("!browserOverlayRequested", screen)
        self.assertIn("!CL_UIMenusAreVisible()", main)
        self.assertIn("KEYCATCH_BROWSER", main)

    def test_retail_disconnect_flow_restores_webui_with_native_fallback(self) -> None:
        main = read_source("code/client/cl_main.cpp")
        common = read_source("code/qcommon/common.c")
        keys = read_source("code/client/cl_keys.cpp")
        webui = read_source("code/client/cl_webui.cpp")

        disconnect_start = main.index("qboolean CL_Disconnect( qboolean showMainMenu )")
        disconnect_end = main.index("void CL_ForwardCommandToServer", disconnect_start)
        disconnect = main[disconnect_start:disconnect_end]

        self.assertLess(
            disconnect.index('Cvar_Set( "r_uiFullScreen", "1" );'),
            disconnect.index("CL_WebView_PublishGameEnd();"),
        )
        self.assertIn('Cvar_Set( "timescale", "1" );', disconnect)
        self.assertIn('CL_AddReliableCommand( "disconnect", qtrue );', disconnect)
        self.assertIn("CL_WritePacket( 2 );", disconnect)
        self.assertIn("CL_WebHost_ShowAfterDisconnect()", disconnect)
        self.assertIn("UI_SET_ACTIVE_MENU, UIMENU_MAIN", disconnect)
        self.assertLess(
            disconnect.index("cls.state = CA_DISCONNECTED;"),
            disconnect.index("CL_WebHost_ShowAfterDisconnect()"),
        )

        show = function_body(webui, "CL_WebHost_ShowAfterDisconnect")
        self.assertIn("host.SetRenderingPaused( false )", show)
        self.assertIn("host.SetFocus( true )", show)
        self.assertIn("cl_webui.browserVisible = qtrue;", show)
        self.assertIn("cl_webui.browserActive = qtrue;", show)
        self.assertNotIn("CL_Awesomium_OpenURL", show)

        recovery_start = common.index("if ( code == ERR_DISCONNECT")
        recovery_end = common.index("} else {", common.index("} else if ( code == ERR_NEED_CD", recovery_start))
        recovery = common[recovery_start:recovery_end]
        self.assertNotIn("CL_Disconnect( qfalse );", recovery)
        self.assertGreaterEqual(recovery.count("CL_Disconnect( qtrue );"), 3)

        command_start = main.index("void CL_Disconnect_f( void )")
        command_end = main.index("void CL_Reconnect_f", command_start)
        command = main[command_start:command_end]
        self.assertIn('Com_Error( ERR_DISCONNECT, "Disconnected from server" );', command)
        self.assertNotIn("CL_Disconnect( qfalse )", command)

        self.assertIn('if ( !Q_stricmp( c, "disconnect" ) )', main)
        self.assertIn("cls.realtime - clc.lastPacketTime >= 3000", main)
        self.assertIn("NET_CompareAdr( from, &clc.netchan.remoteAddress )", main)
        self.assertIn("CL_Disconnect( qtrue )", keys)

    def test_retail_ui_coordinates_are_not_aspect_corrected_twice(self) -> None:
        ui = read_source("code/client/cl_ui.cpp")
        main = read_source("code/client/cl_main.cpp")

        self.assertIn('cl_menuAspect = Cvar_Get( "cl_menuAspect", "1", CVAR_ARCHIVE );', main)
        self.assertIn("The retail UI applies its centered 4:3 transform", ui)
        self.assertIn("!cl_menuAspect || cl_menuAspect->integer || cls.scale <= 0.0f", ui)
        self.assertIn("*x = ( *x - cls.biasX ) / cls.scale * xscale;", ui)
        self.assertIn("if ( !refdef || !cl_menuAspect || cl_menuAspect->integer )", ui)
        self.assertNotIn("SCR_AdjustFrom640Uniform( x, y, w, h );", ui)

    def test_retail_cgame_hud_and_fov_are_not_post_corrected_by_the_engine(self) -> None:
        cgame = read_source("code/client/cl_cgame.cpp")
        main = read_source("code/client/cl_main.cpp")
        public = read_source("code/renderercommon/tr_types.h")

        self.assertFalse((ROOT / "code/client/cl_hud.cpp").exists())
        self.assertFalse((ROOT / "pkg/baseq3/fnql-hud.json").exists())
        self.assertNotIn("CL_Hud", cgame)
        self.assertNotIn("cl_hudAspect", main)
        self.assertNotIn("r_fovCorrection", main)
        self.assertNotIn("RDF_NOFOVCORRECTION", public)

    def test_screen_update_recursion_is_depth_bounded(self) -> None:
        screen = read_source("code/client/cl_scrn.cpp")
        update = function_body(screen, "SCR_UpdateScreen")

        self.assertIn("if ( recursive >= 2 )", update)
        self.assertIn("++recursive;", update)
        self.assertIn("--recursive;", update)
        self.assertNotIn("recursive = 1;", update)

    def test_internal_webui_state_cvars_do_not_retry_user_facing_rom_sets(self) -> None:
        webui = read_source("code/client/cl_webui.cpp")
        start = webui.index("static void CL_WebUI_SetCvarIfChanged")
        end = webui.index("static const char *CL_WebUI_BackendProviderLabel", start)
        setter = webui[start:end]

        self.assertIn("Cvar_Set2( name, value, qtrue );", setter)
        self.assertNotIn("Cvar_Set( name, value );", setter)

    def test_ip_database_shutdown_invalidates_aliases_before_freeing(self) -> None:
        server = read_source("code/server/sv_client.cpp")
        cleanup = function_body(server, "SV_FreeIP4DB")

        self.assertIn("iprange_t *allocation = ipdb_range;", cleanup)
        self.assertLess(cleanup.index("ipdb_range = nullptr;"), cleanup.index("SV_ZFree( allocation );"))
        self.assertLess(cleanup.index("ipdb_tld = nullptr;"), cleanup.index("SV_ZFree( allocation );"))
        self.assertIn("num_tlds = 0;", cleanup)

    def test_server_state_clearing_does_not_create_large_stack_temporaries(self) -> None:
        source = read_source("code/server/sv_init.cpp")
        clear_start = source.index("static void SV_ClearServer( void )")
        shutdown_start = source.index("void SV_Shutdown( const char *finalmsg )")
        clear = source[clear_start:shutdown_start]
        shutdown = source[shutdown_start:]

        self.assertIn("std::memset( &sv, 0, sizeof( sv ) );", clear)
        self.assertNotIn("sv = {};", clear)
        self.assertIn("std::memset( &svs, 0, sizeof( svs ) );", shutdown)
        self.assertNotIn("svs = {};", shutdown)

    def test_startup_system_console_flushes_before_the_main_message_pump(self) -> None:
        console = read_source("code/win32/win_syscon.cpp")

        self.assertIn("extern qboolean com_fullyInitialized;", console)
        self.assertIn("if ( !com_fullyInitialized && s_wcd.hwndBuffer && conBufPos )", console)
        self.assertIn("AddBufferText( conBuffer, conBufPos );", console)
        self.assertIn("UpdateWindow( s_wcd.hwndBuffer );", console)

    def test_windowed_windows_rendering_never_changes_the_desktop_gamma_ramp(self) -> None:
        gamma = read_source("code/win32/win_gamma.cpp")
        wndproc = read_source("code/win32/win_wndproc.cpp")

        start = gamma.index("void GLimp_SetGamma(")
        end = gamma.index("void GLW_RestoreGamma(", start)
        setter = gamma[start:end]
        guard = setter.index("if ( !glw_state.cdsFullscreen )")
        ramp = setter.index("SetDeviceGammaRamp")
        self.assertLess(guard, ramp)
        self.assertIn("GLW_RestoreGamma();", setter[guard:ramp])

        close = wndproc[wndproc.index("case WM_CLOSE:") :]
        self.assertLess(close.index("GLW_RestoreGamma();"), close.index('"quit\\n"'))

    def test_webui_searches_the_detected_retail_steam_install(self) -> None:
        webui = read_source("code/client/cl_webui.cpp")
        contract = read_source("code/client/webui_backend.hpp")
        backend = read_source("code/client/awesomium_backend_win32.cpp")

        self.assertIn('Cvar_VariableStringBuffer( "fs_steampath", retailPath', webui)
        self.assertIn("std::string_view retailPath{};", contract)
        self.assertIn("parameters.retailPath = retailPath ? retailPath", webui)
        self.assertIn("const std::wstring retail = ToWide( parameters.retailPath );", backend)
        self.assertIn("base, retail, home, ExecutableDirectory()", backend)


if __name__ == "__main__":
    unittest.main()
