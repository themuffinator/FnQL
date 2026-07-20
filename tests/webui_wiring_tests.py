from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class WebUiWiringTests(unittest.TestCase):
    def test_webui_source_enables_retail_adapter_only_on_windows_x86(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")

        self.assertIn("defined( _M_IX86 ) || defined( __i386__ )", source)
        self.assertIn('"1",\n#else\n\t\t"0",', source)
        self.assertIn('Cvar_Get( "web_browserActive", "0", CVAR_ROM )', source)
        self.assertIn('Cvar_Get( "ui_browserAwesomium", "0", CVAR_ROM )', source)
        self.assertIn('"runtime-backend-unavailable"', source)
        self.assertIn("external Awesomium SDK/runtime is not bundled", source)
        runtime_available = source[
            source.index("static qboolean CL_WebUI_RuntimeAvailable"):
            source.index("static void CL_WebUI_SetLastError")
        ]
        self.assertIn("ClientBackendHost().IsAvailable()", runtime_available)

    def test_retail_web_commands_are_registered_and_removed_together(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")

        for command in (
            "web_showBrowser",
            "web_changeHash",
            "web_browserActive",
            "web_hideBrowser",
            "web_showError",
            "web_clearCache",
            "web_reload",
            "web_stopRefresh",
            "clientviewprofile",
            "clientfriendinvite",
        ):
            with self.subTest(command=command):
                self.assertIn(f'Cmd_AddCommand( "{command}"', source)
                self.assertIn(f'Cmd_RemoveCommand( "{command}"', source)

    def test_awesomium_resource_requests_route_through_launcher_resolver(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")
        header = (ROOT / "code" / "client" / "client.h").read_text(encoding="utf-8")

        self.assertIn("CL_Awesomium_RequestResource", source)
        self.assertIn("CL_LauncherRequestData( virtualPath, outBuffer, outLength )", source)
        self.assertIn("WebUI resource request could not be resolved.", source)
        self.assertIn("qboolean CL_Awesomium_RequestResource", header)

    def test_webui_commands_normalize_hashes_and_track_overlay_ownership(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")

        self.assertIn('#define CL_WEB_DEFAULT_URL "asset://ql/index.html"', source)
        self.assertIn("static void CL_WebHost_NormalizeHash", source)
        self.assertIn("while ( *cursor == '#' || *cursor == ' '", source)
        self.assertIn('Com_sprintf( buffer, (int)bufferSize, "%s#%s", CL_WEB_DEFAULT_URL, normalizedHash );', source)
        self.assertIn("static qboolean CL_WebHost_SetLocationHash", source)
        self.assertIn("CL_WebHost_NormalizeHash( hash, cl_webui.pendingHash", source)
        self.assertIn("CL_WebHost_BuildCurrentURL( cl_webui.pendingHash, cl_webui.currentUrl", source)
        self.assertIn('window.location.hash.replace(/^#/,\\"\\")!==h', source)
        self.assertIn("window.main_hook_v2", source)
        self.assertIn("CL_WebHost_NormalizeHash( requestedUrl, cl_webui.pendingHash", source)
        self.assertIn('CL_WebHost_OpenRequestedURL( requestedUrl, "web_showBrowser" );', source)
        self.assertIn('CL_WebHost_OpenRequestedURL( requestedUrl, "web_changeHash" );', source)
        self.assertIn("CL_Awesomium_OpenURL( cl_webui.currentUrl )", source)
        self.assertIn("static void CL_WebHost_UpdateOverlayOwnership", source)
        self.assertIn("Key_SetCatcher( Key_GetCatcher() | KEYCATCH_BROWSER );", source)
        self.assertIn("Key_SetCatcher( Key_GetCatcher() & ~KEYCATCH_BROWSER );", source)
        self.assertIn('CL_WebUI_SetCvarIfChanged( "web_browserActive", ownsOverlay ? "1" : "0" );', source)

    def test_webui_retains_browser_target_while_waiting_for_runtime_surface(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")

        self.assertIn(
            "const qboolean pendingSurface = ( available && cl_webui.browserVisible && cl_webui.browserActive && !CL_WebHost_HasDrawableSurface() ) ? qtrue : qfalse;",
            source,
        )
        self.assertIn('CL_WebUI_SetCvarIfChanged( "ui_browserAwesomiumPending", pendingSurface ? "1" : "0" );', source)
        show_browser = source[
            source.index("static void CL_Web_ShowBrowser_f"):
            source.index("static void CL_Web_ChangeHash_f")
        ]
        opener = source[
            source.index("static qboolean CL_WebHost_OpenRequestedURL"):
            source.index("static void CL_Web_ShowBrowser_f")
        ]
        self.assertIn('Cmd_Argc() > 1 ? Cmd_ArgsFrom( 1 ) : "#"', show_browser)
        self.assertIn("CL_WebHost_BuildCurrentURL( cl_webui.pendingHash, cl_webui.currentUrl", opener)
        self.assertIn("cl_webui.browserVisible = qtrue;", opener)
        self.assertIn("cl_webui.browserActive = qtrue;", opener)
        self.assertNotIn("CL_WebUI_ClearBrowserState();", show_browser)
        browser_active = source[
            source.index("static void CL_Web_BrowserActive_f"):
            source.index("static void CL_Web_HideBrowser_f")
        ]
        self.assertIn('CL_WebUI_ReportUnavailable( "web_browserActive" );', browser_active)
        self.assertIn("CL_WebHost_HideBrowser();", browser_active)

    def test_client_and_common_lifecycle_hooks_are_wired(self) -> None:
        client = (ROOT / "code" / "client" / "cl_main.cpp").read_text(encoding="utf-8")
        common = (ROOT / "code" / "qcommon" / "common.c").read_text(encoding="utf-8")

        self.assertIn("CL_WebHost_Init();", client)
        self.assertIn("QLWebHost_RegisterCommands();", client)
        self.assertIn("CL_WebHost_BootstrapAwesomiumMenu();", client)
        self.assertIn("CL_WebHost_Shutdown();", client)
        self.assertIn("QLWebHost_UnregisterCommands();", client)
        self.assertIn("Cbuf_Execute();\n\n\t\tCL_WebHost_Frame();", common)

    def test_browser_keycatcher_routes_input_to_webview(self) -> None:
        shared = (ROOT / "code" / "qcommon" / "q_shared.h").read_text(encoding="utf-8")
        keycodes = (ROOT / "code" / "client" / "keycodes.h").read_text(encoding="utf-8")
        key_names = (ROOT / "code" / "qcommon" / "keys.c").read_text(encoding="utf-8")
        keys = (ROOT / "code" / "client" / "cl_keys.cpp").read_text(encoding="utf-8")
        input_source = (ROOT / "code" / "client" / "cl_input.cpp").read_text(encoding="utf-8")

        self.assertIn("#define KEYCATCH_RETAIL_MOUSEPASS 0x0010", shared)
        self.assertIn("#define KEYCATCH_BROWSER    0x0020", shared)
        for name in ("K_MOUSE6", "K_MOUSE7", "K_MOUSE8", "K_MOUSE9"):
            self.assertIn(name, keycodes)
            self.assertIn(f'"{name.removeprefix("K_")}", {name}', key_names)
        self.assertIn("static void CL_DispatchBrowserKeyEvent", keys)
        self.assertIn("key >= K_MOUSE1 && key <= K_MOUSE9", keys)
        self.assertIn("CL_WebView_OnMouseButtonEvent( key, down );", keys)
        self.assertIn("CL_WebView_OnMouseWheelEvent( 1 );", keys)
        self.assertIn("fnql::input::EncodeUtf8( *codepoint )", keys)
        self.assertIn("CL_WebView_OnKeyEvent( utf8Byte | K_CHAR_FLAG, qtrue );", keys)
        self.assertIn("Key_GetCatcher( ) & KEYCATCH_BROWSER", keys)
        self.assertIn("CL_WebView_OnMouseMove( dx, dy );", input_source)
        self.assertIn("CL_AdvertisementBridge_IsDelayElapsed()", input_source)
        self.assertIn('Cvar_VariableIntegerValue( "cg_ignoreMouseInput" )', input_source)
        self.assertIn('( Key_GetCatcher() & ~KEYCATCH_RETAIL_MOUSEPASS ) == 0', input_source)
        self.assertIn('Cvar_Get( "cg_ignoreMouseInput", "0", CVAR_ROM )', input_source)

    def test_browser_input_releases_the_gameplay_mouse_and_uses_surface_coordinates(self) -> None:
        sdl_input = (ROOT / "code" / "sdl" / "sdl_input.cpp").read_text(encoding="utf-8")
        webui = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")
        wndproc = (ROOT / "code" / "win32" / "win_wndproc.cpp").read_text(encoding="utf-8")

        self.assertIn("static qboolean mouseAbsoluteMode", sdl_input)
        self.assertIn("browserActive || nativeUiActive || cgameUiActive", sdl_input)
        self.assertIn("const qboolean retailAbsolute = ( !consoleActive &&", sdl_input)
        self.assertIn("const qboolean showCursor = ( retailAbsolute", sdl_input)
        self.assertIn("IN_ShowCursor( showCursor );", sdl_input)
        self.assertIn("mouseAbsoluteMode != retailAbsolute", sdl_input)
        self.assertIn("(int)e.motion.x, (int)e.motion.y", sdl_input)
        self.assertIn("static void IN_ProjectBrowserMousePosition", sdl_input)
        self.assertIn("cls.glconfig.vidWidth / (float)glw_state.window_width", sdl_input)
        self.assertIn("IN_ProjectBrowserMousePosition( e.button.x, e.button.y", sdl_input)
        self.assertIn("static int CL_WebHost_MapCursorCoordinate", webui)
        self.assertIn("cls.glconfig.vidWidth", webui)
        self.assertIn("status.surface.width", webui)
        self.assertIn("CL_WebView_OnMouseMove( x, y );", wndproc)
        self.assertIn("CL_WebView_OnMouseButtonEvent( key, down );", wndproc)
        self.assertIn("CL_WebView_OnMouseWheelEvent( 1 );", wndproc)

    def test_demo_playback_keys_use_retail_mousepass_and_freeze_bridge(self) -> None:
        client_h = (ROOT / "code" / "client" / "client.h").read_text(encoding="utf-8")
        cvars = (ROOT / "code" / "qcommon" / "cvar.c").read_text(encoding="utf-8")
        keys = (ROOT / "code" / "client" / "cl_keys.cpp").read_text(encoding="utf-8")
        client = (ROOT / "code" / "client" / "cl_main.cpp").read_text(encoding="utf-8")
        cgame = (ROOT / "code" / "client" / "cl_cgame.cpp").read_text(encoding="utf-8")

        self.assertIn("extern\tcvar_t\t*cl_freezeDemo;", client_h)
        self.assertIn("static void Cvar_Add_f( void )", cvars)
        self.assertIn('Cmd_AddCommand( "cvarAdd", Cvar_Add_f );', cvars)
        self.assertIn('va( "%0.3f", currentValue + amount )', cvars)
        self.assertIn("static qboolean CL_HandleDemoPlaybackKeyEvent( int key )", keys)
        self.assertIn("!clc.demoplaying || ( Key_GetCatcher() & ~KEYCATCH_RETAIL_MOUSEPASS ) != 0", keys)
        self.assertIn('Cbuf_ExecuteText( EXEC_APPEND, "toggle cl_freezeDemo\\n" );', keys)
        self.assertIn('Cbuf_ExecuteText( EXEC_APPEND, "timescale 1\\n" );', keys)
        self.assertIn('Cbuf_ExecuteText( EXEC_APPEND, "cvarAdd timescale -0.1\\n" );', keys)
        self.assertIn('Cbuf_ExecuteText( EXEC_APPEND, "cvarAdd timescale 0.1\\n" );', keys)
        self.assertIn('Cbuf_ExecuteText( EXEC_APPEND, "toggle cg_drawDemoHUD\\n" );', keys)
        self.assertIn("if ( CL_HandleDemoPlaybackKeyEvent( key ) )", keys)
        self.assertIn('cl_freezeDemo = Cvar_Get( "cl_freezeDemo", "0", CVAR_TEMP );', client)
        self.assertIn("if ( clc.demoplaying && cl_freezeDemo && cl_freezeDemo->integer )", client)
        self.assertIn("gameMsec = 0;", client)
        self.assertIn("( cl_freezeDemo && cl_freezeDemo->integer ) || com_timescale->value == 0.0f", cgame)

    def test_togglemenu_command_reuses_retail_escape_menu_bridge(self) -> None:
        client_h = (ROOT / "code" / "client" / "client.h").read_text(encoding="utf-8")
        keys = (ROOT / "code" / "client" / "cl_keys.cpp").read_text(encoding="utf-8")
        client = (ROOT / "code" / "client" / "cl_main.cpp").read_text(encoding="utf-8")

        self.assertIn("void CL_ToggleMenu_f( void );", client_h)
        self.assertIn("static void CL_ToggleMenuInternal( int key, qboolean sendKeyUp, unsigned time )", keys)
        self.assertIn("CL_ToggleMenuInternal( key, qfalse, time );", keys)
        self.assertIn("CL_ToggleMenuInternal( K_ESCAPE, qtrue, cls.realtime );", keys)
        self.assertIn("if ( sendKeyUp )", keys)
        self.assertIn("VM_Call( uivm, 3, UI_KEY_EVENT, key, qfalse, time );", keys)
        self.assertIn("Key_GetCatcher() & KEYCATCH_BROWSER", keys)
        self.assertIn('Cmd_AddCommand( "togglemenu", CL_ToggleMenu_f );', client)

    def test_key_bind_changes_publish_to_webui_when_client_build(self) -> None:
        keys = (ROOT / "code" / "qcommon" / "keys.c").read_text(encoding="utf-8")

        self.assertIn("#ifndef DEDICATED\nvoid CL_WebView_PublishBindChanged", keys)
        self.assertIn("cvar_modifiedFlags |= CVAR_ARCHIVE;", keys)
        self.assertIn(
            'CL_WebView_PublishBindChanged( Key_KeynumToString( keynum ), keys[ keynum ].binding ? keys[ keynum ].binding : "" );',
            keys,
        )

    def test_cvar_changes_publish_to_webui_when_client_build(self) -> None:
        cvars = (ROOT / "code" / "qcommon" / "cvar.c").read_text(encoding="utf-8")

        self.assertIn("#ifndef DEDICATED\nvoid CL_WebView_PublishCvarChange", cvars)
        self.assertIn("static qboolean Cvar_ShouldReplicateChange", cvars)
        self.assertIn("static void Cvar_PublishChange", cvars)
        self.assertIn("CL_WebView_PublishCvarChange( var->name, value, Cvar_ShouldReplicateChange( var ) );", cvars)
        self.assertGreaterEqual(cvars.count("Cvar_PublishChange( var );"), 3)

    def test_webui_game_bridge_formats_browser_event_payloads(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")

        self.assertIn("#define CL_WEB_BROWSER_EVENT_COUNT 32", source)
        self.assertIn("clWebUiBrowserEvent_t events[CL_WEB_BROWSER_EVENT_COUNT]", source)
        self.assertIn("int\t\t\tlastReplayedEventSequence;", source)
        self.assertIn("static qboolean CL_WebHost_CanDispatchLiveEvent", source)
        self.assertIn("static void CL_WebView_DispatchLiveEvent", source)
        self.assertIn("static void CL_WebView_ReplayRetainedEvents", source)
        self.assertIn("window.EnginePublish", source)
        self.assertIn('CL_Awesomium_ExecuteJavascript( script, "" );', source)
        self.assertIn("slot = cl_webui.eventHead % CL_WEB_BROWSER_EVENT_COUNT;", source)
        self.assertIn("event->sequence = cl_webui.eventSequence;", source)
        self.assertIn("firstIndex = cl_webui.eventHead - CL_WEB_BROWSER_EVENT_COUNT;", source)
        self.assertIn("event->sequence <= cl_webui.lastReplayedEventSequence", source)
        self.assertIn("cl_webui.lastReplayedEventSequence = event->sequence;", source)
        self.assertLess(
            source.index("CL_WebView_ReplayRetainedEvents();"),
            source.index('CL_WebView_PublishEvent( "web.object.ready", NULL );'),
        )
        self.assertIn("static void CL_WebUI_JsonEscape", source)
        self.assertIn("static void CL_WebUI_AppendJsonFragment", source)
        self.assertIn("static void CL_WebView_AppendTaggedInfoPair", source)
        self.assertIn('"MSG_TYPE"', source)
        self.assertIn("cursor = Info_NextPair( cursor, key, value );", source)
        self.assertIn('"{\\"text\\":\\"%s\\"}"', source)
        self.assertIn('Com_sprintf( eventName, sizeof( eventName ), "cvar.%s", name );', source)
        self.assertIn('"bind.changed"', source)
        self.assertIn('"{\\"ip\\":%u,\\"port\\":%u}"', source)
        self.assertIn('"game.demo"', source)
        self.assertIn('"game.screenshot"', source)
        self.assertIn('"game.key"', source)

    def test_webui_native_javascript_requests_are_pumped_into_engine(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")

        pump_start = source.index("static void CL_WebHost_PumpNativeJavascriptRequests( void ) {")
        pump_end = source.index("static void CL_WebView_DispatchLiveEvent", pump_start)
        pump = source[pump_start:pump_end]

        self.assertIn("#define CL_WEB_NATIVE_REQUESTS_PER_FRAME 8", source)
        self.assertIn("int\t\t\tframeSequence;", source)
        self.assertIn("int\t\t\tnextNativeRequestPollFrame;", source)
        self.assertIn("cl_webui.frameSequence++;", source)
        self.assertIn("CL_WebHost_PumpNativeJavascriptRequests();", source)
        self.assertIn("static void CL_WebHost_ProcessNativeJavascriptRequest", source)
        self.assertIn('!Q_stricmp( kind, "cmd" )', source)
        self.assertIn('!Q_stricmp( kind, "get" )', source)
        self.assertIn('!Q_stricmp( kind, "set" )', source)
        self.assertIn('!Q_stricmp( kind, "reset" )', source)
        self.assertIn('Cbuf_ExecuteText( EXEC_APPEND, va( "%s\\n", payload ) );', source)
        self.assertIn("CL_WebHost_UpdateBrowserCvarCache( name, value );", source)
        self.assertIn("CL_Awesomium_PopJavascriptRequest( request, sizeof( request ) )", source)
        self.assertIn("CL_WEB_NATIVE_REQUEST_BUSY_POLL_FRAMES", source)
        self.assertNotIn("CL_Awesomium_IsLoading()", pump)
        self.assertIn("window.__qlr_native_requests||[]", source)
        self.assertIn("SendGameCommand:function(cmd)", source)
        self.assertIn("return queue('cmd',cmd);", source)
        self.assertIn("window.__qlr_native_read=String(q.shift())", source)
        self.assertIn("s.charCodeAt(%d)||0", source)
        self.assertIn('CL_Awesomium_ExecuteJavascript( "(function(){window.__qlr_native_read=\'\';})()", "" );', source)

    def test_webui_startup_bridge_installs_qz_instance_helper(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")

        self.assertIn("#define CL_WEB_BRIDGE_RETRY_FRAMES 30", source)
        self.assertIn("qboolean\tstartupBridgeInjected;", source)
        self.assertIn("int\t\t\tnextBridgeRetryFrame;", source)
        self.assertIn("static void CL_WebHost_BuildStartupBridgeScript", source)
        self.assertIn("static void CL_WebHost_BuildStartupBridgeRetryScript", source)
        self.assertIn("static qboolean CL_WebHost_InjectStartupBridge", source)
        self.assertIn("static void CL_WebHost_EnsureStartupBridge", source)
        self.assertIn("static void CL_WebHost_SyncNativeSnapshots", source)
        self.assertIn("CL_WebHost_EnsureStartupBridge();", source)
        self.assertIn("CL_WebHost_SyncNativeSnapshots( qfalse );", source)
        self.assertIn("window.__qlr_qz_instance_ready", source)
        self.assertIn("window.FakeClient.qz_instance", source)
        self.assertIn("window.qz_instance=qz", source)
        self.assertIn("var noop=function(){return true;};", source)
        self.assertIn("playerAvatar:''", source)
        self.assertIn("playerAvatarUrl:''", source)
        self.assertIn("playerProfileUrl:''", source)
        self.assertIn("playerProfile:{id:'0',name:'',avatar:'',avatarUrl:'',profileUrl:''}", source)
        self.assertIn("qz.playerProfile=c.playerProfile", source)
        self.assertIn("onlineServicesMode:'Unavailable'", source)
        self.assertIn("onlineServicesPolicy:'compatibility-unavailable'", source)
        self.assertIn("matchmakingProvider:'Unavailable'", source)
        self.assertIn("matchmakingPolicy:'Steamworks bridge unavailable'", source)
        self.assertIn("workshopProvider:'Unavailable'", source)
        self.assertIn("workshopPolicy:'Steamworks bridge unavailable'", source)
        self.assertIn("config.onlineServicesMode=String(c.onlineServicesMode||'')", source)
        self.assertIn("config.matchmakingProvider=String(c.matchmakingProvider||'')", source)
        self.assertIn("config.workshopProvider=String(c.workshopProvider||'')", source)
        self.assertIn("SendGameCommand:function(cmd)", source)
        self.assertIn("OpenURL:function(url){url=String(url||'');return url?queue('openurl',url):false;}", source)
        self.assertIn("OpenSteamOverlayURL:function(url){url=String(url||'');return url?queue('opensteamurl',url):false;}", source)
        self.assertIn("GetCvar:function(name)", source)
        self.assertIn("SetCvar:function(name,value)", source)
        self.assertIn("ResetCvar:function(name)", source)
        self.assertIn("GetConfig:function(){return config;}", source)
        self.assertIn("var setNativeConfig=function(c)", source)
        self.assertIn("window.__qlr_set_native_config=setNativeConfig", source)
        self.assertIn("fileExistsCache={}", source)
        self.assertIn("cursorPosition={x:0,y:0}", source)
        self.assertIn("clipboardText=''", source)
        self.assertIn("clipboardPrimed=false", source)
        self.assertIn("mapPrimed=false", source)
        self.assertIn("factoryPrimed=false", source)
        self.assertIn("pendingNativeMaps={}", source)
        self.assertIn("pendingNativeFactories={}", source)
        self.assertIn("demoList=[]", source)
        self.assertIn("demoPrimed=false", source)
        self.assertIn("setDemoList=function(v)", source)
        self.assertIn("setMapList=function(v)", source)
        self.assertIn("setFactoryList=function(v)", source)
        self.assertIn("beginNativeMaps=function(){pendingNativeMaps={};return true;}", source)
        self.assertIn("addNativeMaps=function(v){return addCatalogObjects(pendingNativeMaps,v);}", source)
        self.assertIn("commitNativeMaps=function(){return setMapList(pendingNativeMaps);}", source)
        self.assertIn("beginNativeFactories=function(){pendingNativeFactories={};return true;}", source)
        self.assertIn("addNativeFactories=function(v){return addFactoryObjects(pendingNativeFactories,v);}", source)
        self.assertIn("Object.defineProperty(o,k,{value:v", source)
        self.assertIn("Object.prototype.toString.call(v)==='[object Array]'", source)
        self.assertIn("commitNativeFactories=function(){return setFactoryList(pendingNativeFactories);}", source)
        self.assertIn("friendList=[]", source)
        self.assertIn("friendPrimed=false", source)
        self.assertIn("setFriendList=function(v)", source)
        self.assertIn("config.friends=friendList", source)
        self.assertIn("ugcList=[]", source)
        self.assertIn("ugcPrimed=false", source)
        self.assertIn("setUGCList=function(v)", source)
        self.assertIn("config.ugc=ugcList", source)
        self.assertIn("nativeState={pakPresent:false,gameRunning:false}", source)
        self.assertIn("var m=/^\\\\s*web_changeHash", source)
        self.assertIn("window.location.hash=(m[1]||'').replace(/^#/,'')", source)
        self.assertIn("WriteTextFile:function(path,contents)", source)
        self.assertIn("queue('write',String(path||'')+'\\\\n'+String(contents||''))", source)
        self.assertIn("FileExists:function(path)", source)
        self.assertIn("queue('exists',path)", source)
        self.assertIn("GetClipboardText:function(){if(!clipboardPrimed){queue('clipget','');}return clipboardText;}", source)
        self.assertIn("SetClipboardText:function(text)", source)
        self.assertIn("queue('clipset',clipboardText)", source)
        self.assertIn("var queueSocial=function(kind,payload){return queue('social.'+String(kind||''),String(payload||''));};", source)
        self.assertIn("CreateLobby:function(){return queueSocial('createlobby','');}", source)
        self.assertIn("LeaveLobby:function(){return queueSocial('leavelobby','');}", source)
        self.assertIn("JoinLobby:function(lobby){return queueSocial('joinlobby'", source)
        self.assertIn("SetLobbyServer:function(ip,port){return queueSocial('setlobbyserver'", source)
        self.assertIn("ShowInviteOverlay:function(){return queueSocial('showinviteoverlay','');}", source)
        self.assertIn("SayLobby:function(message){return queueSocial('saylobby'", source)
        self.assertIn("RequestUserStats:function(steamId){return queueSocial('requestuserstats'", source)
        self.assertIn("ActivateGameOverlayToUser:function(dialog,steamId){return queueSocial('activategameoverlaytouser'", source)
        self.assertIn("Invite:function(steamId){return queueSocial('invite'", source)
        self.assertIn("GetAllUGC:function(filter){ugcPrimed=true;queueSocial('getallugc',String(typeof filter==='undefined'?1:filter));return ugcList;}", source)
        self.assertIn("RequestServers:function(source){return queue('servers',String(typeof source==='undefined'?2:source));}", source)
        self.assertIn("RequestServerDetails:function(ip,port){return queue('serverdetails',String(ip||'')+'\\\\n'+String(port||''));}", source)
        self.assertIn("RefreshList:function(){return queue('refreshservers','');}", source)
        self.assertIn("SetFavoriteServer:function(ip,port,add){var addText=String(add).toLowerCase();var addValue=(add===false||addText==='false')?0:parseInt(add,10);return queue('favorite',String(ip||'')+'\\\\n'+String(port||'')+'\\\\n'+((isNaN(addValue)?1:addValue)!==0?'1':'0'));}", source)
        self.assertNotIn("RefreshList:function(source)", source)
        self.assertNotIn("CreateLobby:function(maxMembers)", source)
        self.assertNotIn("LeaveLobby:function(lobby)", source)
        self.assertNotIn("SetLobbyServer:function(lobby,ip,port)", source)
        self.assertNotIn("ShowInviteOverlay:function(lobby)", source)
        self.assertNotIn("SayLobby:function(lobby,message)", source)
        self.assertNotIn("Invite:function(steamId,connect)", source)
        self.assertIn("GetMapList:function(){if(!mapPrimed){mapPrimed=true;queue('maps','');}return maps;}", source)
        self.assertIn("GetFactoryList:function(){if(!factoryPrimed){factoryPrimed=true;queue('factories','');}return factories;}", source)
        self.assertIn("GetDemoList:function(){if(!demoPrimed){demoPrimed=true;queue('demos','');}return demoList;}", source)
        self.assertIn("GetFriendList:function(){if(!friendPrimed){friendPrimed=true;queue('friends','');}return friendList;}", source)
        self.assertIn("GetNextKeyDown:function(active)", source)
        self.assertIn("if(typeof active==='undefined'){return queue('keycapture','1');}", source)
        self.assertIn("var activeValue=(active===false||activeText==='false')?0:parseInt(active,10);", source)
        self.assertIn("return queue('keycapture',((isNaN(activeValue)?1:activeValue)!==0?'1':'0'));", source)
        self.assertIn("IsPakFilePresent:function(path){path=String(path||'');if(!path){return !!nativeState.pakPresent;}if(!Object.prototype.hasOwnProperty.call(fileExistsCache,path)){queue('exists',path);}return !!fileExistsCache[path];}", source)
        self.assertIn("IsGameRunning:function(){return !!nativeState.gameRunning;}", source)
        self.assertIn("GetCursorPosition:function(){return {x:cursorPosition.x,y:cursorPosition.y};}", source)
        self.assertIn("NoOp:noop", source)
        retail_qz_method_order = [
            "IsPakFilePresent:function",
            "IsGameRunning:function",
            "SendGameCommand:function",
            "WriteTextFile:function",
            "GetCvar:function",
            "SetCvar:function",
            "ResetCvar:function",
            "GetMapList:function",
            "GetFactoryList:function",
            "GetDemoList:function",
            "OpenURL:function",
            "OpenSteamOverlayURL:function",
            "GetClipboardText:function",
            "SetClipboardText:function",
            "RequestServers:function",
            "RequestServerDetails:function",
            "RefreshList:function",
            "CreateLobby:function",
            "LeaveLobby:function",
            "JoinLobby:function",
            "SetLobbyServer:function",
            "ShowInviteOverlay:function",
            "SayLobby:function",
            "RequestUserStats:function",
            "GetFriendList:function",
            "ActivateGameOverlayToUser:function",
            "Invite:function",
            "FileExists:function",
            "GetConfig:function",
            "GetCursorPosition:function",
            "GetAllUGC:function",
            "GetNextKeyDown:function",
            "SetFavoriteServer:function",
            "NoOp:noop",
        ]
        retail_qz_method_positions = [source.index(token) for token in retail_qz_method_order]
        self.assertEqual(retail_qz_method_positions, sorted(retail_qz_method_positions))
        self.assertIn("window.__qlr_set_demo_list=setDemoList", source)
        self.assertIn("window.__qlr_set_friend_list=setFriendList", source)
        self.assertIn("window.__qlr_set_ugc_list=setUGCList", source)
        self.assertIn("window.__qlr_set_native_maps=setMapList", source)
        self.assertIn("window.__qlr_set_native_factories=setFactoryList", source)
        self.assertIn("window.__qlr_begin_native_maps=beginNativeMaps", source)
        self.assertIn("window.__qlr_add_native_maps=addNativeMaps", source)
        self.assertIn("window.__qlr_commit_native_maps=commitNativeMaps", source)
        self.assertIn("window.__qlr_begin_native_factories=beginNativeFactories", source)
        self.assertIn("window.__qlr_add_native_factories=addNativeFactories", source)
        self.assertIn("window.__qlr_commit_native_factories=commitNativeFactories", source)
        self.assertIn("window.__qlr_browser_helpers_ready=true", source)
        self.assertIn("window.__fnql_retry_qz_bridge=syncQzBridge", source)
        self.assertIn("document.addEventListener('DOMContentLoaded',syncQzBridge,false)", source)
        self.assertIn("var qlrBridgeTries=0", source)
        self.assertIn("setInterval(function(){syncQzBridge();", source)
        self.assertNotIn("setInterval(function(){window.main_hook_v2();", source)
        self.assertIn("clearInterval(qlrBridgeTimer)", source)
        self.assertIn("qz_instance.ready", source)
        self.assertIn("CL_WebView_PublishEvent( \"web.object.ready\", NULL );", source)
        self.assertIn("CL_WebHost_SyncNativeSnapshots( qtrue );", source)
        inject_start = source.index(
            "static qboolean CL_WebHost_InjectStartupBridge",
            source.index("static void CL_WebHost_BuildStartupBridgeRetryScript"),
        )
        inject = source[
            inject_start : source.index("static void CL_WebHost_EnsureStartupBridge", inject_start)
        ]
        self.assertLess(
            inject.index("CL_WebHost_SyncNativeSnapshots( qtrue );"),
            inject.index('CL_WebView_PublishEvent( "web.object.ready", NULL );'),
        )
        self.assertIn("cl_webui.startupBridgeInjected = qfalse;", source)

    def test_webui_native_file_and_cursor_requests_are_bounded(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")

        self.assertIn("static void CL_WebHost_UpdateBrowserCursorPosition", source)
        self.assertIn("window.__qlr_set_cursor_position", source)
        self.assertIn("CL_WebHost_UpdateBrowserCursorPosition( x, y );", source)
        self.assertIn("static qboolean CL_WebHost_PathIsSafeRelative", source)
        self.assertIn('strstr( path, ".." )', source)
        self.assertIn('strstr( path, "::" )', source)
        self.assertIn("strchr( path, ':' )", source)
        self.assertIn("path[0] == '/' || path[0] == '\\\\'", source)
        self.assertIn("static void CL_WebHost_UpdateBrowserFileExistsCache", source)
        self.assertIn("window.__qlr_set_file_exists", source)
        self.assertIn("static qboolean CL_WebHost_FileExists", source)
        self.assertIn("return FS_FileExists( path ) ? qtrue : qfalse;", source)
        self.assertIn("static qboolean CL_WebHost_WriteTextFile", source)
        self.assertIn('FS_WriteFile( path, contents ? contents : "", (int)strlen( contents ? contents : "" ) );', source)
        self.assertIn('!Q_stricmp( kind, "exists" )', source)
        self.assertIn('!Q_stricmp( kind, "write" )', source)
        self.assertIn("CL_WebHost_UpdateBrowserFileExistsCache( path, exists );", source)
        self.assertIn("wrote = CL_WebHost_WriteTextFile( path, contentsStart );", source)

    def test_webui_native_clipboard_requests_use_platform_clipboard_api(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")

        self.assertIn("window.__qlr_set_clipboard_text", source)
        self.assertIn("static void CL_WebHost_UpdateBrowserClipboardCache", source)
        self.assertIn("static void CL_WebHost_ReadClipboardText", source)
        self.assertIn("static void CL_WebHost_SetClipboardText", source)
        self.assertIn("clipboardData = Sys_GetClipboardData();", source)
        self.assertIn("Z_Free( clipboardData );", source)
        self.assertIn('Sys_SetClipboardData( text ? text : "" );', source)
        self.assertIn('!Q_stricmp( kind, "clipget" )', source)
        self.assertIn('!Q_stricmp( kind, "clipset" )', source)
        self.assertIn("CL_WebHost_UpdateBrowserClipboardCache( clipboardText );", source)
        self.assertIn("CL_WebHost_SetClipboardText( payload );", source)

    def test_webui_native_url_requests_retain_browser_targets(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")
        client_header = (ROOT / "code" / "client" / "client.h").read_text(encoding="utf-8")

        self.assertIn("static qboolean CL_WebHost_OpenRequestedURL", source)
        self.assertIn('strstr( requestedUrl, "://" )', source)
        self.assertIn("Q_strncpyz( cl_webui.currentUrl, requestedUrl, sizeof( cl_webui.currentUrl ) );", source)
        self.assertIn("relativeUrl = strstr( requestedUrl, \"://\" ) ? qfalse : qtrue;", source)
        self.assertIn("CL_WebHost_HasLiveView() && CL_WebHost_HasBoundWindowObject() && CL_WebHost_SetLocationHash( requestedUrl )", source)
        self.assertIn("CL_WebHost_NormalizeHash( requestedUrl, cl_webui.pendingHash", source)
        self.assertIn('CL_WebUI_ReportUnavailable( owner ? owner : "qz.OpenURL" );', source)
        self.assertIn("qboolean CL_Steam_OpenOverlayUrl( const char *url );", client_header)
        self.assertIn("qboolean CL_Steam_OpenOverlayUrl( const char *url )", source)
        self.assertIn('return CL_WebHost_SteamBridgeUnavailable( "OpenSteamOverlayURL", url );', source)
        self.assertIn('!Q_stricmp( kind, "openurl" )', source)
        self.assertIn('CL_WebHost_OpenRequestedURL( payload, "qz.OpenURL" );', source)
        self.assertIn('!Q_stricmp( kind, "opensteamurl" )', source)
        self.assertIn("CL_Steam_OpenOverlayUrl( payload );", source)

    def test_webui_server_browser_uses_retail_events_and_fallback_source_lists(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")
        client_header = (ROOT / "code" / "client" / "client.h").read_text(encoding="utf-8")
        client_main = (ROOT / "code" / "client" / "cl_main.cpp").read_text(encoding="utf-8")

        self.assertIn("#define CL_WEB_SERVER_LOCAL_REFRESH_WAIT_MSEC 1000", source)
        self.assertIn("#define CL_WEB_SERVER_REMOTE_REFRESH_WAIT_MSEC 5000", source)
        self.assertIn("#define CL_WEB_SERVER_REFRESH_TIMEOUT_MSEC 15000", source)
        self.assertIn("#define CL_WEB_SERVER_DETAILS_TIMEOUT_MSEC 5000", source)
        self.assertIn("qboolean\tserverRefreshActive;", source)
        self.assertIn("int\t\t\tserverRefreshSource;", source)
        self.assertIn("qboolean\tserverDetailActive;", source)
        self.assertIn("netadr_t\tserverDetailAddress;", source)
        self.assertIn("static void CL_WebHost_SaveServerCache", source)
        self.assertIn('FS_FOpenFileWrite( "servercache.dat" )', source)
        self.assertIn("FS_Write( cls.globalServers, sizeof( cls.globalServers ), file );", source)
        self.assertIn("FS_Write( cls.favoriteServers, sizeof( cls.favoriteServers ), file );", source)
        self.assertIn("static qboolean CL_WebHost_ParseServerEndpoint", source)
        numeric_address = source[
            source.index("static void CL_WebHost_FormatNumericAddress"):
            source.index("static qboolean CL_WebHost_ParseServerEndpoint")
        ]
        self.assertLess(numeric_address.index("( ip >> 24 ) & 0xffu"), numeric_address.index("( ip >> 16 ) & 0xffu"))
        self.assertLess(numeric_address.index("( ip >> 16 ) & 0xffu"), numeric_address.index("( ip >> 8 ) & 0xffu"))
        self.assertLess(numeric_address.index("( ip >> 8 ) & 0xffu"), numeric_address.index("ip & 0xffu"))
        self.assertIn("static qboolean CL_WebHost_SetFavoriteServer", source)
        self.assertIn("atoi( addStart + 1 ) != 0", source)
        self.assertIn("NET_StringToAdr( address, &adr, NA_UNSPEC )", source)
        self.assertIn("NET_CompareAdr( &cls.favoriteServers[i].adr, &adr )", source)
        self.assertIn("CL_WebHost_SaveServerCache();", source)
        self.assertIn("static int CL_WebHost_ServerRequestModeToSource", source)
        self.assertIn("case 1:\n\t\t\treturn AS_LOCAL;", source)
        self.assertIn("case 3:\n\t\tcase 4:\n\t\t\treturn AS_FAVORITES;", source)
        self.assertIn("static serverInfo_t *CL_WebHost_GetServerList", source)
        self.assertIn("static void CL_WebHost_MarkServerVisible", source)
        self.assertIn("static void CL_WebHost_ResetServerPings", source)
        self.assertIn("static void CL_WebHost_StartServerRefresh", source)
        self.assertIn("qboolean\tserverRefreshInitialized;", source)
        self.assertIn("int\t\t\tserverRefreshRequestMode;", source)
        self.assertIn("cl_webui.serverRefreshInitialized = qtrue;", source)
        self.assertIn("cl_webui.serverRefreshRequestMode = requestMode;", source)
        self.assertIn("static void CL_WebHost_ServerBrowserFrame", source)
        self.assertIn("CL_WebHost_ServerBrowserFrame();", source)
        self.assertIn("CL_UpdateVisiblePings_f( cl_webui.serverRefreshSource )", source)
        self.assertIn("static unsigned int CL_WebHost_PackedIPv4FromAddress", source)
        self.assertIn("static void CL_WebHost_FormatServerDetailId", source)
        self.assertIn("static serverInfo_t *CL_WebHost_FindServerInfoByAddress", source)
        self.assertIn("static qboolean CL_WebHost_DetailMatchesAddress", source)
        self.assertIn("static void CL_WebHost_PublishSteamServerResponse", source)
        self.assertIn("static void CL_WebHost_PublishServerBrowserResponse", source)
        self.assertIn("static void CL_WebHost_PublishUnresponsiveServerRows", source)
        self.assertIn("static void CL_WebHost_PublishServerDetailResponse", source)
        self.assertIn("static void CL_WebHost_PublishServerDetailFailed", source)
        self.assertIn("static void CL_WebHost_BuildServerDetailEndpointPayload", source)
        self.assertIn("static void CL_WebHost_PublishServerDetailRulesEnd", source)
        self.assertIn("static void CL_WebHost_PublishServerDetailPlayersEnd", source)
        self.assertIn("static void CL_WebHost_PublishServerDetailRulesFailed", source)
        self.assertIn("static void CL_WebHost_PublishServerDetailPlayersFailed", source)
        self.assertIn("static void CL_WebHost_PublishServerDetailRuleResponse", source)
        self.assertIn("static void CL_WebHost_PublishServerDetailRulesFromInfoString", source)
        self.assertIn("static void CL_WebHost_PublishServerDetailPlayerResponse", source)
        self.assertIn("static qboolean CL_WebHost_StartServerDetailRequest", source)
        self.assertIn("static void CL_WebHost_ServerDetailsFrame", source)
        self.assertIn("CL_WebHost_ServerDetailsFrame();", source)
        self.assertIn('Com_sprintf( eventName, sizeof( eventName ), "servers.details.%s.response", detailId );', source)
        self.assertIn('Com_sprintf( eventName, sizeof( eventName ), "servers.details.%s.response", responseId );', source)
        self.assertIn('Com_sprintf( eventName, sizeof( eventName ), "servers.details.%s.failed", cl_webui.serverDetailId );', source)
        self.assertIn('Com_sprintf( eventName, sizeof( eventName ), "servers.rules.%s.response", cl_webui.serverDetailId );', source)
        self.assertIn('Com_sprintf( eventName, sizeof( eventName ), "servers.rules.%s.end", cl_webui.serverDetailId );', source)
        self.assertIn('Com_sprintf( eventName, sizeof( eventName ), "servers.players.%s.response", cl_webui.serverDetailId );', source)
        self.assertIn('Com_sprintf( eventName, sizeof( eventName ), "servers.players.%s.end", cl_webui.serverDetailId );', source)
        self.assertIn('{\\"id\\":\\"%s\\",\\"ip\\":%u,\\"port\\":%u}', source)
        rules_end = source[
            source.index("static void CL_WebHost_PublishServerDetailRulesEnd") :
            source.index("static void CL_WebHost_PublishServerDetailPlayersEnd")
        ]
        players_end = source[
            source.index("static void CL_WebHost_PublishServerDetailPlayersEnd") :
            source.index("static void CL_WebHost_PublishServerDetailRulesFailed")
        ]
        for terminal_event in (rules_end, players_end):
            self.assertIn("CL_WebHost_BuildServerDetailEndpointPayload", terminal_event)
            self.assertNotIn("CL_WebView_PublishEvent( eventName, NULL )", terminal_event)
        udp_complete = source[
            source.index("void CL_WebHost_OnServerStatusResponseComplete") :
            source.index("static const char *CL_WebHost_ServerSourceLabel")
        ]
        self.assertLess(
            udp_complete.index("CL_WebHost_PublishServerDetailRulesEnd();"),
            udp_complete.index("CL_WebHost_PublishServerDetailPlayersEnd();"),
        )
        self.assertLess(
            udp_complete.index("CL_WebHost_PublishServerDetailPlayersEnd();"),
            udp_complete.index("CL_WebHost_ClearServerDetailRequest();"),
        )
        self.assertIn('\\"numPlayers\\":%d', source)
        self.assertIn('\\"steam_id\\":\\"%s\\"', source)
        self.assertIn('\\"rule\\":\\"%s\\",\\"value\\":\\"%s\\"', source)
        self.assertIn('\\"name\\":\\"%s\\",\\"score\\":%d,\\"time\\":%d', source)
        self.assertIn('\\"gamedir\\":\\"%s\\"', source)
        self.assertIn('\\"password\\":%s,\\"vac\\":%s', source)
        self.assertIn('sscanf( playerLine, "%d %d \\"%31[^\\"]\\""', source)
        self.assertIn("Info_ValueForKey( infoString, \"hostname\" )", source)
        self.assertIn("static const char *CL_WebHost_ServerSourceLabel", source)
        self.assertIn("static const char *CL_WebHost_ServerRequestModeLabel", source)
        self.assertIn("static const char *CL_WebHost_ServerBrowserCompatibilityReason", source)
        self.assertIn("static void CL_WebHost_PublishServerBrowserCompatibility", source)
        self.assertIn('"servers.refresh.compatibility"', source)
        self.assertIn('CL_WebView_PublishEvent( "servers.refresh.end", NULL );', source)
        self.assertIn("legacy engine server browser", source)
        self.assertIn("SteamMatchmakingServers", source)
        self.assertIn("Steamworks server browser is not available in this build", source)
        self.assertIn("history fallback mapped to favorites source", source)
        self.assertIn("friends fallback mapped to global source", source)
        self.assertIn('\\"modeLabel\\":\\"%s\\"', source)
        self.assertIn("policy", source)
        self.assertIn("qboolean CL_Steam_RequestServers( int requestMode );", client_header)
        self.assertIn("qboolean CL_Steam_RequestServerDetails( unsigned int serverIp, unsigned short serverPort );", client_header)
        self.assertIn("qboolean CL_Steam_RefreshServerList( void );", client_header)
        self.assertIn("qboolean CL_Steam_RequestServers( int requestMode )", source)
        self.assertIn("const int source = CL_WebHost_ServerRequestModeToSource( requestMode );", source)
        self.assertIn("CL_WebHost_StartServerRefresh( requestMode, source,", source)
        self.assertIn("static uint32_t CL_WebHost_ServerRequestModeToSteamRequestMode", source)
        self.assertIn("FNQL_STEAM_SERVER_BROWSER_FRIENDS", source)
        steam_mode_mapping = source[
            source.index("static uint32_t CL_WebHost_ServerRequestModeToSteamRequestMode"):
            source.index("qboolean CL_Steam_RequestServers", source.index("static uint32_t CL_WebHost_ServerRequestModeToSteamRequestMode"))
        ]
        for request_mode, provider_mode in (
            (1, "FNQL_STEAM_SERVER_BROWSER_LAN"),
            (2, "FNQL_STEAM_SERVER_BROWSER_FRIENDS"),
            (3, "FNQL_STEAM_SERVER_BROWSER_FAVORITES"),
            (4, "FNQL_STEAM_SERVER_BROWSER_HISTORY"),
        ):
            self.assertIn(f"case {request_mode}:", steam_mode_mapping)
            self.assertIn(f"return {provider_mode};", steam_mode_mapping)
        self.assertIn("return FNQL_STEAM_SERVER_BROWSER_INTERNET;", steam_mode_mapping)
        self.assertIn("nativeAvailable ? qfalse : qtrue", source)
        self.assertIn("CL_WebHost_StartServerRefresh( requestMode, source, qtrue );", source)
        self.assertIn("RefreshQuery can synchronously dispatch callbacks", source)
        self.assertIn("CL_WebHost_PublishUnresponsiveServerRows( cl_webui.serverRefreshSource );", source)
        self.assertNotIn("CL_Steam_ResetServerResponseList", source)
        self.assertNotIn("CL_Steam_StoreServerResponse", source)
        self.assertIn('CL_WebView_PublishEvent( "servers.refresh.start", NULL );', source)
        self.assertIn("CL_WebHost_PublishServerBrowserCompatibility( requestMode, source );", source)
        steam_request = source[
            source.index("qboolean CL_Steam_RequestServers( int requestMode )"):
            source.index("qboolean CL_Steam_RequestServerDetails", source.index("qboolean CL_Steam_RequestServers( int requestMode )"))
        ]
        self.assertLess(
            steam_request.index("cl_webui.serverRefreshSteam = qtrue;"),
            steam_request.index("FNQL_Steam_RequestServers("),
        )
        self.assertIn("cl_webui.serverRefreshSteam = qfalse;", steam_request)
        self.assertIn('Cbuf_ExecuteText( EXEC_APPEND, "localservers\\n" );', source)
        self.assertIn("source == AS_FAVORITES", source)
        self.assertIn('Cbuf_ExecuteText( EXEC_APPEND, va( "globalservers 0 %d\\n"', source)
        self.assertIn("return qtrue;", source)
        self.assertIn("qboolean CL_Steam_RequestServerDetails( unsigned int serverIp, unsigned short serverPort )", source)
        self.assertIn("CL_WebHost_FormatNumericAddress( serverIp, serverPort, address, sizeof( address ) );", source)
        self.assertIn("CL_WebHost_StartServerDetailRequest( &adr );", source)
        self.assertIn('Cbuf_ExecuteText( EXEC_APPEND, va( "serverstatus %s\\n", address ) );', source)
        self.assertIn("qboolean CL_Steam_RefreshServerList( void )", source)
        self.assertIn("!cl_webui.serverRefreshInitialized", source)
        self.assertIn("return CL_Steam_RequestServers( cl_webui.serverRefreshRequestMode );", source)
        self.assertIn("static void CL_WebHost_RequestServers", source)
        self.assertIn("CL_Steam_RequestServers( payload && payload[0] ? atoi( payload ) : AS_GLOBAL );", source)
        self.assertIn("static void CL_WebHost_RequestServerDetails", source)
        self.assertIn("CL_WebHost_StartServerDetailRequest( &adr );", source)
        self.assertIn('Cbuf_ExecuteText( EXEC_APPEND, va( "serverstatus %s\\n", address ) );', source)
        self.assertIn('!Q_stricmp( kind, "servers" )', source)
        self.assertIn("CL_WebHost_RequestServers( payload );", source)
        self.assertIn('!Q_stricmp( kind, "refreshservers" )', source)
        self.assertIn("CL_Steam_RefreshServerList();", source)
        self.assertNotIn('!Q_stricmp( kind, "servers" ) || !Q_stricmp( kind, "refreshservers" )', source)
        self.assertIn('!Q_stricmp( kind, "serverdetails" )', source)
        self.assertIn('!Q_stricmp( kind, "favorite" )', source)
        self.assertIn("void CL_WebHost_OnServerInfoResponse( const netadr_t *address, const char *infoString, int ping );", client_header)
        self.assertIn("qboolean CL_WebHost_OnServerStatusResponseInfo( const netadr_t *address, const char *infoString );", client_header)
        self.assertIn("CL_WebHost_OnServerInfoResponse( from, infoString, ping.time );", client_main)
        self.assertIn("void CL_WebHost_OnServerStatusResponsePlayer( const netadr_t *address, const char *playerLine );", client_header)
        self.assertIn("void CL_WebHost_OnServerStatusResponseComplete( const netadr_t *address );", client_header)
        self.assertIn("qboolean publishBrowserDetails;", client_main)
        self.assertIn("publishBrowserDetails = CL_WebHost_OnServerStatusResponseInfo( from, s );", client_main)
        self.assertIn("CL_WebHost_OnServerStatusResponsePlayer( from, s );", client_main)
        self.assertIn("CL_WebHost_OnServerStatusResponseComplete( from );", client_main)

    def test_steam_server_refresh_releases_after_marking_the_request_complete(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")
        finish = source[
            source.index(
                "static void CL_WebHost_PublishServerBrowserRefreshEnd",
                source.index("static void CL_WebHost_StartServerRefresh"),
            ):
            source.index(
                "static void CL_WebHost_ServerBrowserFrame",
                source.index("static void CL_WebHost_StartServerRefresh"),
            )
        ]
        frame = source[
            source.index(
                "static void CL_WebHost_ServerBrowserFrame",
                source.index("static void CL_WebHost_StartServerRefresh"),
            ):
            source.index("static unsigned int CL_WebHost_PackedIPv4FromAddress")
        ]

        self.assertIn("qboolean releaseSteamRequest;", finish)
        self.assertIn("releaseSteamRequest = cl_webui.serverRefreshSteam;", finish)
        self.assertIn("cl_webui.serverRefreshActive = qfalse;", finish)
        self.assertIn("cl_webui.serverRefreshSteam = qfalse;", finish)
        self.assertIn("if ( releaseSteamRequest ) {\n\t\tFNQL_Steam_CancelServers();", finish)
        self.assertLess(
            finish.index("cl_webui.serverRefreshActive = qfalse;"),
            finish.index("FNQL_Steam_CancelServers();"),
        )
        # The sole occurrence is this function's definition. A second would
        # be self-recursion in the terminal path, which previously exhausted
        # the stack after a timed-out Steam request.
        self.assertEqual(finish.count("CL_WebHost_PublishServerBrowserRefreshEnd("), 1)
        self.assertIn("if ( cl_webui.serverRefreshSteam ) {", frame)
        self.assertIn("CL_WebHost_PublishServerBrowserRefreshEnd();", frame)
        self.assertIn("\t\treturn;\n\t}\n\n\twait = qfalse;", frame)

    def test_webui_social_bridge_requests_publish_unsupported_status_until_steamworks_import(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")
        header = (ROOT / "code" / "client" / "client.h").read_text(encoding="utf-8")

        self.assertIn("static void CL_WebHost_UpdateBrowserUGCList", source)
        self.assertIn("window.__qlr_set_ugc_list", source)
        self.assertIn("static qboolean CL_WebHost_HandleSocialBridgeRequest", source)
        self.assertIn('static const char prefix[] = "social.";', source)
        self.assertIn("static void CL_WebHost_CopyPayloadLine", source)
        self.assertIn("static qboolean CL_WebHost_SteamBridgeUnavailable", source)
        self.assertIn("static void CL_Steam_OverlayCommand_f", source)
        self.assertIn('Cmd_AddCommand( "clientviewprofile", CL_Steam_OverlayCommand_f );', source)
        self.assertIn('Cmd_AddCommand( "clientfriendinvite", CL_Steam_OverlayCommand_f );', source)
        self.assertIn('Cmd_RemoveCommand( "clientviewprofile" );', source)
        self.assertIn('Cmd_RemoveCommand( "clientfriendinvite" );', source)
        self.assertIn('dialog = "steamid";', source)
        self.assertIn('dialog = "friendadd";', source)
        self.assertIn("CL_GetClientSteamId( clientNum, &steamIdLow, &steamIdHigh )", source)
        self.assertIn("CL_Steam_ActivateOverlayToUser( dialog, steamId );", source)
        for prototype in (
            "qboolean CL_Steam_CreateLobby( void );",
            "qboolean CL_Steam_LeaveLobby( void );",
            "qboolean CL_Steam_JoinLobby( const char *lobbyId );",
            "qboolean CL_Steam_SetLobbyServer( unsigned int serverIp, unsigned short serverPort );",
            "qboolean CL_Steam_ShowInviteOverlay( void );",
            "qboolean CL_Steam_Invite( const char *steamId );",
            "qboolean CL_Steam_SayLobby( const char *message );",
            "qboolean CL_Steam_RequestAllUGC( int filter );",
            "qboolean CL_Steam_RequestUserStats( const char *steamId );",
            "qboolean CL_Steam_ActivateOverlayToUser( const char *dialog, const char *steamId );",
            "void CL_Steam_OnRichPresenceJoinRequested( const char *command );",
            "void CL_Steam_OnGameServerChangeRequested( const char *server, const char *password );",
        ):
            self.assertIn(prototype, header)
        self.assertIn("static void CL_Steam_LogOnlineCallbackIgnored", source)
        self.assertIn("void CL_Steam_OnRichPresenceJoinRequested( const char *command )", source)
        self.assertIn("void CL_Steam_OnGameServerChangeRequested( const char *server, const char *password )", source)
        self.assertIn('"rich_presence_join_requested"', source)
        self.assertIn('"server_change_requested"', source)
        self.assertIn('"Steamworks callback bridge unavailable"', source)
        self.assertIn("char kind[64];", source)
        self.assertIn("CL_WebHost_HandleSocialBridgeRequest( kind, payload )", source)
        for method in (
            "createlobby",
            "leavelobby",
            "joinlobby",
            "setlobbyserver",
            "showinviteoverlay",
            "saylobby",
            "requestuserstats",
            "activategameoverlaytouser",
            "invite",
            "getallugc",
        ):
            self.assertIn(f'!Q_stricmp( method, "{method}" )', source)
        self.assertIn("CL_Steam_CreateLobby();", source)
        self.assertIn("CL_Steam_JoinLobby( payload );", source)
        self.assertIn("CL_Steam_SetLobbyServer( (unsigned int)strtoul( ipText, NULL, 10 ), (unsigned short)atoi( portText ) );", source)
        self.assertIn("CL_Steam_SayLobby( second[0] ? second : first );", source)
        self.assertIn("CL_Steam_RequestUserStats( payload );", source)
        self.assertIn("CL_Steam_Invite( steamId[0] ? steamId : payload );", source)
        self.assertIn('!Q_stricmp( method, "getallugc" )', source)
        self.assertIn("CL_Steam_RequestAllUGC( payload && payload[0] ? atoi( payload ) : 0 );", source)
        self.assertIn('CL_WebHost_UpdateBrowserUGCList( "[]" );', source)
        self.assertIn('CL_WebView_PublishEvent( "web.ugc.failed", NULL );', source)
        self.assertIn('static const char unsupportedReason[] = "Steamworks bridge is not available in this build";', source)
        self.assertIn("CL_WebUI_JsonEscape( unsupportedReason, escapedReason, sizeof( escapedReason ) );", source)
        self.assertIn('\\"reason\\":\\"%s\\"', source)
        self.assertIn('CL_WebView_PublishEvent( "qz.unsupported", eventPayload );', source)

    def test_webui_native_demo_list_requests_scan_retail_demo_paths(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")

        self.assertIn("static void CL_WebHost_UpdateBrowserDemoList", source)
        self.assertIn("static void CL_WebHost_AppendDemoListForExtension", source)
        self.assertIn("static void CL_WebHost_BuildDemoListJson", source)
        self.assertIn('FS_GetFileList( "demos", extension', source)
        self.assertIn('Com_sprintf( demoExtension, sizeof( demoExtension ), DEMOEXT "%d", protocol );', source)
        self.assertIn("CL_WebUI_JsonEscape( cursor, escapedName, sizeof( escapedName ) );", source)
        self.assertIn("window.__qlr_set_demo_list", source)
        self.assertIn('!Q_stricmp( kind, "demos" )', source)
        self.assertIn("CL_WebHost_BuildDemoListJson( demoJson, sizeof( demoJson ) );", source)
        self.assertIn("CL_WebHost_UpdateBrowserDemoList( demoJson );", source)

    def test_webui_native_catalog_snapshots_scan_retail_asset_indexes(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")

        self.assertIn("#define CL_WEB_CATALOG_JSON_LENGTH 65536", source)
        self.assertIn("#define CL_WEB_CATALOG_SYNC_CHUNK_CHARS 8192", source)
        self.assertIn("qboolean\tmapCatalogSynced;", source)
        self.assertIn("qboolean\tfactoryCatalogSynced;", source)
        self.assertIn("static void CL_WebHost_UpdateBrowserMapList", source)
        self.assertIn("static qboolean CL_WebHost_UpdateBrowserFactoryList", source)
        self.assertIn("static qboolean CL_WebHost_ExecuteCatalogBatch", source)
        self.assertIn("static qboolean CL_WebHost_QueueCatalogEntry", source)
        self.assertIn("static qboolean CL_WebHost_UpdateBrowserCatalogCacheBatched", source)
        self.assertIn('std::string payload = "[";', source)
        self.assertIn("CL_WebHost_JsonParseExpression( payload.c_str() )", source)
        self.assertIn("CL_WebHost_UpdateBrowserCatalogCacheBatched( \"__qlr_begin_native_maps\", \"__qlr_add_native_maps\", \"__qlr_commit_native_maps\", mapListJson )", source)
        self.assertNotIn("CL_WebHost_UpdateBrowserCatalogCacheBatched( \"__qlr_begin_native_factories\"", source)
        self.assertIn("static void CL_WebHost_BuildMapListJson", source)
        self.assertIn('FS_GetFileList( "maps", ".bsp"', source)
        self.assertIn("static char *CL_WebHost_AllocateMapListJson", source)
        self.assertIn("SV_MapPoolWebCatalogJsonSize()", source)
        self.assertIn("SV_MapPoolBuildWebCatalogJson( buffer, bufferSize )", source)
        self.assertIn('\\"gametypes\\":[true,false,false,false,false,false,false,false,false,false,false,false,false]', source)
        self.assertNotIn('\\"gametypes\\":[0,0,0,0,0,0,0,0,0,0,0,0,0]', source)
        self.assertIn("static char *CL_WebHost_AllocateFactoryListJson", source)
        self.assertIn("SV_FactoryWebCatalogJsonSize()", source)
        self.assertIn("SV_FactoryBuildWebCatalogJson( buffer, bufferSize )", source)
        self.assertNotIn("CL_WebHost_AppendFactoryDefinitionsFromFile", source)
        self.assertNotIn("CL_WebHost_CopyFactoryJsonString", source)
        self.assertIn('Q_strncpyz( buffer, "{}", (int)bufferSize );', source)
        self.assertIn("for(k in factories){if(hasOwn(factories,k)){delete factories[k];}}", source)
        self.assertIn("for(k in maps){if(hasOwn(maps,k)){delete maps[k];}}", source)
        self.assertNotIn("maps=o;mapPrimed=true", source)
        self.assertIn("__qlr_set_native_maps", source)
        self.assertIn("__qlr_set_native_factories", source)
        self.assertIn('!Q_stricmp( kind, "maps" )', source)
        self.assertIn('!Q_stricmp( kind, "factories" )', source)
        self.assertIn("CL_WebHost_UpdateBrowserMapList( mapJson );", source)
        self.assertIn("cl_webui.factoryCatalogSynced =", source)
        self.assertIn("CL_WebHost_UpdateBrowserFactoryList( factoryJson );", source)
        self.assertIn("CL_WebHost_JsonParseExpression", source)
        self.assertIn("JSON.parse('", source)
        self.assertIn('{ "mapCount",', source)
        self.assertIn('{ "ffaMapCount",', source)
        self.assertIn('{ "nanOutputs",', source)
        self.assertIn('{ "openSelects",', source)
        self.assertIn('{ "selectLeft",', source)
        self.assertIn("CL_WebHost_InvalidateDocumentSnapshots();\n\tCL_Awesomium_Reload", source)
        self.assertIn("!CL_WebHost_HasLiveView() || CL_Awesomium_IsLoading()", source)
        self.assertIn(
            "startupScript = CL_WebHost_AllocateStartupBridgeScript( preloadConfigJson,",
            source,
        )

    def test_webui_native_friend_list_uses_client_identity_snapshot(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")

        self.assertIn("static void CL_WebHost_UpdateBrowserFriendList", source)
        self.assertIn("window.__qlr_set_friend_list", source)
        self.assertIn("window.EnginePublish('users.persona.'+v.id+'.change'", source)
        self.assertIn("static void CL_WebHost_BuildFriendListJson", source)
        self.assertIn("for ( int i = 0; i < MAX_CLIENTS; ++i )", source)
        self.assertIn("cgameClientIdentity_t identity;", source)
        self.assertIn("CL_CopyClientIdentity( i, &identity )", source)
        self.assertIn("identity.identityLow", source)
        self.assertIn("identity.identityHigh", source)
        self.assertIn("CL_IsSteamIdentityMuted( identity.identityLow, identity.identityHigh )", source)
        self.assertIn('\\"clientNum\\"', source)
        self.assertIn('\\"steamId\\"', source)
        self.assertIn('\\"muted\\"', source)
        self.assertIn('!Q_stricmp( kind, "friends" )', source)
        self.assertIn("#define CL_WEB_FRIEND_JSON_LENGTH 262144", source)
        self.assertIn("CL_WebHost_BuildFriendListJson( friendJson, CL_WEB_FRIEND_JSON_LENGTH );", source)
        self.assertIn("CL_WebHost_UpdateBrowserFriendList( friendJson );", source)
        self.assertIn('{ "qzSteamId",', source)
        self.assertIn('{ "qzPlayerNameLength",', source)
        self.assertIn('{ "qzFriendCount",', source)
        self.assertIn('{ "steamAvatarImages",', source)

    def test_webui_startup_preload_is_bounded_and_snapshots_follow_document_load(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")
        client_header = (ROOT / "code" / "client" / "client.h").read_text(encoding="utf-8")

        self.assertIn("const char *initialFriendJson", source)
        self.assertIn("const char *initialFriendJson", client_header)
        self.assertIn("CL_WebHost_BuildFriendListJson( friendJson, CL_WEB_FRIEND_JSON_LENGTH );", source)
        self.assertIn("#define CL_WEB_AWESOMIUM_PRELOAD_MAX_LENGTH 16384", source)
        self.assertIn("char preloadCvarJson[2048];", source)
        self.assertIn("char preloadConfigJson[3072];", source)
        self.assertIn("CL_WebHost_BuildStartMatchCvarJson( preloadCvarJson", source)
        self.assertIn('\\"cvars\\":{%s}', source)
        self.assertIn(
            "CL_WebHost_AllocateStartupBridgeScript( preloadConfigJson,\n\t\tNULL, NULL )",
            source,
        )
        self.assertIn(
            "strlen( startupScript ) >= CL_WEB_AWESOMIUM_PRELOAD_MAX_LENGTH",
            source,
        )
        self.assertIn("capture both during their initial require()", source)
        self.assertIn("CL_WebHost_SyncNativeSnapshots( qtrue );", source)
        self.assertIn("window.__qlr_set_native_config", source)
        self.assertIn("window.__qlr_set_friend_list", source)
        self.assertIn("configExpression", source)
        self.assertIn("friendExpression", source)

    def test_webui_native_config_snapshot_seeds_qz_cache(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")
        client_header = (ROOT / "code" / "client" / "client.h").read_text(encoding="utf-8")

        self.assertIn("#define CL_WEB_CONFIG_SYNC_FRAMES 300", source)
        self.assertIn("#define CL_WEB_CONFIG_JSON_LENGTH 8192", source)
        self.assertIn("qboolean\tconfigSnapshotSynced;", source)
        self.assertIn("qboolean\tdemoSnapshotSynced;", source)
        self.assertIn("static void CL_WebHost_BuildConfigJson", source)
        self.assertIn("static void CL_WebHost_BuildConfigCvarJson", source)
        self.assertIn("static void CL_WebHost_BuildConfigBindJson", source)
        self.assertIn("static void CL_WebHost_UpdateBrowserConfigCache", source)
        self.assertIn("static void CL_WebHost_SyncNativeSnapshots", source)
        self.assertIn("static const char *CL_WebHost_OnlineServicesModeLabel", source)
        self.assertIn("static const char *CL_WebHost_OnlineServicesParityScopeLabel", source)
        self.assertIn("static const char *CL_WebHost_MatchmakingProviderLabel", source)
        self.assertIn("static const char *CL_WebHost_WorkshopProviderLabel", source)
        self.assertIn("static void CL_WebHost_FormatSteamAvatarUrl", source)
        self.assertIn("static void CL_WebHost_FormatSteamProfileUrl", source)
        self.assertIn("static const char *CL_WebHost_ResourceBridgeProviderLabel", source)
        self.assertIn("static const char *CL_WebHost_ResourceBridgePolicyLabel", source)
        self.assertIn("static const char *CL_WebHost_ResourceBridgeParityScopeLabel", source)
        self.assertIn("static const char *CL_WebHost_ResourceBridgeParityReasonLabel", source)
        self.assertIn("static qboolean CL_WebHost_IsResourceURI", source)
        self.assertIn('strstr( url, "://" )', source)
        self.assertIn("qhandle_t CL_Steam_RegisterShader( const char *url );", client_header)
        self.assertIn("qhandle_t CL_Steam_RegisterShader( const char *url )", source)
        self.assertIn('{ "asset://steam/avatar/", FNQL_STEAM_AVATAR_LARGE }', source)
        self.assertIn("return re.RegisterShaderNoMip( url );", source)
        self.assertIn("UI resource bridge request %s ignored", source)
        self.assertIn("CL_CopyClientIdentity( clc.clientNum, &identity )", source)
        self.assertIn('Com_sprintf( buffer, bufferSize, "asset://steam/avatar/%s", steamId );', source)
        self.assertIn("static qboolean CL_WebUI_RequestSteamAvatarPng", source)
        self.assertIn("CL_WebUI_EncodeAvatarPng", source)
        self.assertIn("FNQL_Steam_GetAvatarRGBA", source)
        self.assertIn("CL_WebUI_RequestSteamAvatarPng( path, &buffer, &length )", source)
        self.assertIn('Com_sprintf( buffer, bufferSize, "https://steamcommunity.com/profiles/%s", steamId );', source)
        self.assertIn("static void CL_Steam_GetLocalDisplayName", source)
        self.assertIn("status.persona_name", source)
        self.assertIn("CL_Steam_GetLocalDisplayName( playerName, sizeof( playerName ) );", source)
        self.assertIn('Cvar_VariableStringBuffer( "fs_steampath", retailPath', source)
        self.assertIn('\\"playerAvatar\\":\\"%s\\"', source)
        self.assertIn('\\"playerAvatarUrl\\":\\"%s\\"', source)
        self.assertIn('\\"playerProfileUrl\\":\\"%s\\"', source)
        self.assertIn('\\"playerProfile\\":{\\"id\\":\\"%s\\"', source)
        self.assertIn('\\"onlineServicesMode\\":\\"%s\\"', source)
        self.assertIn('\\"onlineServicesPolicy\\":\\"%s\\"', source)
        self.assertIn('\\"matchmakingProvider\\":\\"%s\\"', source)
        self.assertIn('\\"matchmakingPolicy\\":\\"%s\\"', source)
        self.assertIn('\\"workshopProvider\\":\\"%s\\"', source)
        self.assertIn('\\"workshopPolicy\\":\\"%s\\"', source)
        self.assertIn('CL_WebUI_SetCvarIfChanged( "ui_onlineServicesMode"', source)
        self.assertIn('CL_WebUI_SetCvarIfChanged( "ui_subscriptionBridgeMode", CL_WebHost_OnlineServicesModeLabel() );', source)
        self.assertIn('CL_WebUI_SetCvarIfChanged( "ui_subscriptionBridgeParityReason", CL_WebHost_OnlineServicesParityReasonLabel() );', source)
        self.assertIn('"ui_subscriptionBridgeMode"', source)
        self.assertIn('"ui_subscriptionBridgePolicy"', source)
        self.assertIn('"ui_subscriptionBridgeParityScope"', source)
        self.assertIn('"ui_subscriptionBridgeParityReason"', source)
        self.assertIn('"ui_matchmakingProvider"', source)
        self.assertIn('"ui_workshopProvider"', source)
        self.assertIn('"ui_resourceBridgeProvider"', source)
        self.assertIn('CL_WebUI_SetCvarIfChanged( "ui_resourceBridgeProvider", CL_WebHost_ResourceBridgeProviderLabel() );', source)
        self.assertIn('CL_WebUI_SetCvarIfChanged( "ui_resourceBridgePolicy", CL_WebHost_ResourceBridgePolicyLabel() );', source)
        self.assertIn('CL_WebUI_SetCvarIfChanged( "ui_resourceBridgeParityScope", CL_WebHost_ResourceBridgeParityScopeLabel() );', source)
        self.assertIn('CL_WebUI_SetCvarIfChanged( "ui_resourceBridgeParityReason", CL_WebHost_ResourceBridgeParityReasonLabel() );', source)
        self.assertIn('"ui_resourceBridgeSteamDataSourceSubset"', source)
        self.assertIn('"ui_resourceBridgeSteamDataSourceNativeGap"', source)
        self.assertIn('"ui_resourceBridgeSteamDataSourceFallbackOwner"', source)
        self.assertIn('flags == CVAR_NONEXISTENT', source)
        self.assertIn('"cl_webuiEnable"', source)
        self.assertIn('"ui_browserAwesomiumProvider"', source)
        self.assertIn('"fs_game"', source)
        self.assertIn("static const clWebConfigCvarDefault_t cl_webStartMatchCvars[]", source)
        for name, value in (
            ("sv_hostname", "noname"),
            ("sv_serverType", "0"),
            ("net_port", "27960"),
            ("sv_maxclients", "8"),
            ("bot_minplayers", "0"),
            ("g_spSkill", "2"),
            ("teamsize", "0"),
            ("g_password", ""),
            ("sv_warmupReadyPercentage", "0.51"),
            ("sv_mapPoolFile", "mappool.txt"),
        ):
            self.assertIn(f'{{ "{name}", "{value}" }}', source)
        self.assertIn("cl_webStartMatchCvars[i].name", source)
        self.assertIn("cl_webStartMatchCvars[i].value", source)
        self.assertIn("for ( int i = 0; i < MAX_KEYS; ++i )", source)
        self.assertIn("binding = Key_GetBinding( i );", source)
        self.assertIn("keyName = Key_KeynumToString( i );", source)
        self.assertIn("window.__qlr_set_native_config(%s)", source)
        self.assertIn("CL_WebHost_SyncNativeSnapshots( qtrue );", source)
        self.assertIn("CL_WebHost_SyncNativeSnapshots( qfalse );", source)
        self.assertIn("cl_webui.configSnapshotSynced = qtrue;", source)
        self.assertIn("cl_webui.nextConfigSnapshotFrame = cl_webui.frameSequence + CL_WEB_CONFIG_SYNC_FRAMES;", source)
        self.assertIn("CL_WebHost_UpdateBrowserDemoList( demoJson );", source)
        self.assertIn("CL_WebHost_UpdateBrowserFriendList( friendJson );", source)

    def test_ui_shader_registration_uses_ql_resource_bridge(self) -> None:
        ui_source = (ROOT / "code" / "client" / "cl_ui.cpp").read_text(encoding="utf-8")

        self.assertIn("case UI_R_REGISTERSHADERNOMIP:", ui_source)
        self.assertIn("return CL_Steam_RegisterShader( VMA(1) );", ui_source)

    def test_webui_native_state_and_key_capture_are_cached_to_browser(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")

        self.assertIn("qboolean\tkeyCaptureArmed;", source)
        self.assertIn("window.__qlr_set_native_state", source)
        self.assertIn("static qboolean CL_WebHost_IsGameRunning", source)
        self.assertIn("cls.state >= CA_CONNECTED && cls.state < CA_CINEMATIC", source)
        self.assertIn("static void CL_WebHost_UpdateBrowserNativeState", source)
        self.assertIn("CL_WebPak_Available() ? \"true\" : \"false\"", source)
        self.assertIn("CL_WebHost_IsGameRunning() ? \"true\" : \"false\"", source)
        self.assertGreaterEqual(source.count("CL_WebHost_UpdateBrowserNativeState();"), 2)
        self.assertIn('!Q_stricmp( kind, "keycapture" )', source)
        self.assertIn("cl_webui.keyCaptureArmed = ( !payload[0] || atoi( payload ) != 0 ) ? qtrue : qfalse;", source)
        self.assertIn("cl_webui.keyCaptureArmed = qfalse;", source)
        hide_browser = source[
            source.index("void CL_WebHost_HideBrowser"):
            source.index("void CL_WebHost_NotifyAppActivation")
        ]
        self.assertIn("if ( cl_webui.keyCaptureArmed )", hide_browser)
        self.assertLess(hide_browser.index("if ( cl_webui.keyCaptureArmed )"), hide_browser.index("CL_WebUI_ClearBrowserState();"))

    def test_webui_native_cvar_cache_updates_live_qz_instance(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")

        self.assertIn("static void CL_WebHost_UpdateBrowserCvarCache", source)
        self.assertIn("window.__qlr_set_native_cvar", source)
        self.assertIn("CL_WebHost_HasBoundWindowObject()", source)
        self.assertIn("Cvar_Set( name, value );", source)
        self.assertIn("Cvar_Reset( name );", source)
        self.assertIn("Cvar_VariableStringBuffer( name, value, sizeof( value ) );", source)
        self.assertIn('CL_WebHost_UpdateBrowserCvarCache( name, value ? value : "" );', source)

    def test_webui_game_lifecycle_events_are_published_from_client_paths(self) -> None:
        client = (ROOT / "code" / "client" / "cl_main.cpp").read_text(encoding="utf-8")
        cgame = (ROOT / "code" / "client" / "cl_cgame.cpp").read_text(encoding="utf-8")
        header = (ROOT / "code" / "client" / "client.h").read_text(encoding="utf-8")
        tr_public = (ROOT / "code" / "renderercommon" / "tr_public.h").read_text(encoding="utf-8")

        self.assertIn("void CL_WebView_PublishGameStartForAddress( const netadr_t *serverAddress );", header)
        self.assertIn("CL_WebView_PublishGameStartForAddress( &clc.serverAddress );", client)
        self.assertIn("CL_WebView_PublishGameStart();", cgame)
        self.assertIn("CL_WebView_PublishGameDemo( clc.recordName, clc.recordNameShort );", client)
        self.assertIn("const qboolean publishGameEnd", client)
        self.assertIn("CL_WebView_PublishGameEnd();", client)
        self.assertIn("void\t(*PublishGameScreenshot)( const char *id, const char *name );", tr_public)
        self.assertIn("rimp.PublishGameScreenshot = CL_WebView_PublishGameScreenshot;", client)

    def test_cgame_tagged_info_import_reaches_webui_comm_notice(self) -> None:
        cgame = (ROOT / "code" / "client" / "cl_cgame.cpp").read_text(encoding="utf-8")
        webui = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")

        self.assertIn("CL_WebView_PublishTaggedInfoString( messageType, infoString );", cgame)
        self.assertNotIn("(void)messageType;\n\t(void)infoString;", cgame)
        self.assertIn("CL_WebView_InvokeCommNotice( payload );", webui)

    def test_browser_key_capture_publishes_game_key_and_uses_retail_event_types(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")

        self.assertIn("static void CL_WebView_PublishGameKey( int key )", source)
        self.assertIn("Key_KeynumToString( key & ~K_CHAR_FLAG )", source)
        self.assertIn('"{\\"id\\":%d,\\"key\\":\\"%s\\"}"', source)
        self.assertIn("#define CL_WEB_KEYBOARD_EVENT_KEYDOWN_TYPE 0u", source)
        self.assertIn("#define CL_WEB_KEYBOARD_EVENT_KEYUP_TYPE 1u", source)
        self.assertIn("#define CL_WEB_KEYBOARD_EVENT_CHAR_TYPE 2u", source)
        self.assertIn("#define CL_WEB_KEYBOARD_EVENT_ACTIVATION_TYPE 0u", source)
        self.assertIn("#define CL_WEB_KEYBOARD_EVENT_ACTIVATION_VIRTUAL_KEY 0x11u", source)
        self.assertIn("#define CL_WEB_KEYBOARD_EVENT_ACTIVATION_NATIVE_KEY 0x1d0001L", source)
        self.assertIn("if ( !down && cl_webui.keyCaptureArmed && !( key & K_CHAR_FLAG ) )", source)
        self.assertIn("CL_WebView_PublishGameKey( key );", source)
        self.assertIn("cl_webui.keyCaptureArmed = qfalse;", source)
        self.assertIn("CL_Awesomium_InjectKeyboardEvent( CL_WEB_KEYBOARD_EVENT_CHAR_TYPE, (unsigned int)( key & ~K_CHAR_FLAG ), 0 );", source)
        self.assertIn("down ? CL_WEB_KEYBOARD_EVENT_KEYDOWN_TYPE : CL_WEB_KEYBOARD_EVENT_KEYUP_TYPE", source)
        self.assertIn("(unsigned int)key,\n\t\t\t\t0 );", source)
        self.assertIn("static void CL_WebView_InjectActivationKeyboardEvent( void )", source)
        self.assertIn("if ( cl_webui.browserVisible || cl_webui.browserActive )", source)
        self.assertIn("CL_WEB_KEYBOARD_EVENT_ACTIVATION_TYPE,\n\t\t\tCL_WEB_KEYBOARD_EVENT_ACTIVATION_VIRTUAL_KEY,\n\t\t\tCL_WEB_KEYBOARD_EVENT_ACTIVATION_NATIVE_KEY", source)
        notify_activation = source[
            source.index("void CL_WebHost_NotifyAppActivation"):
            source.index("void CL_WebView_OnKeyEvent")
        ]
        self.assertIn("cl_webui.appActive = active;", notify_activation)
        self.assertIn("if ( !active )", notify_activation)
        self.assertIn("CL_WebView_InjectActivationKeyboardEvent();", notify_activation)

    def test_browser_input_bridge_maps_mouse_buttons_and_requires_active_browser(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")

        self.assertIn("static qboolean CL_WebHost_BrowserAcceptsInput( void )", source)
        self.assertIn("return ( cl_webui.browserVisible && cl_webui.browserActive ) ? qtrue : qfalse;", source)
        self.assertIn("static int CL_WebHost_MapMouseButton( int key )", source)
        self.assertIn("case K_MOUSE1:\n\t\t\treturn 0;", source)
        self.assertIn("case K_MOUSE2:\n\t\t\treturn 2;", source)
        self.assertIn("case K_MOUSE3:\n\t\t\treturn 1;", source)
        self.assertIn("default:\n\t\t\treturn -1;", source)
        self.assertIn("button = CL_WebHost_MapMouseButton( key );", source)
        self.assertIn("if ( button < 0 )", source)
        self.assertIn("CL_Awesomium_InjectMouseDown( button );", source)
        self.assertIn("CL_Awesomium_InjectMouseUp( button );", source)
        self.assertIn("if ( !CL_WebHost_BrowserAcceptsInput() || direction == 0 )", source)
        mouse_button = source[
            source.index("void CL_WebView_OnMouseButtonEvent"):
            source.index("void CL_WebView_OnMouseWheelEvent")
        ]
        self.assertNotIn("CL_Awesomium_InjectMouseDown( key );", mouse_button)
        self.assertNotIn("CL_Awesomium_InjectMouseUp( key );", mouse_button)

    def test_webui_game_bridge_notifies_screenshots_from_supported_renderers(self) -> None:
        for renderer in ("renderer", "renderervk", "rendererrtx"):
            with self.subTest(renderer=renderer):
                source = (ROOT / "code" / renderer / "tr_init.c").read_text(encoding="utf-8")
                self.assertIn("if ( ri.PublishGameScreenshot && checkname[0] )", source)
                self.assertIn("ri.PublishGameScreenshot( checkname, checkname );", source)

    def test_webui_game_start_address_payload_uses_stable_loopback(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")

        self.assertIn("static unsigned int CL_WebView_PackAddressIP", source)
        self.assertIn("return ( 127u << 24 ) | 1u;", source)
        self.assertIn("void CL_WebView_PublishGameStartForAddress( const netadr_t *serverAddress )", source)
        self.assertIn("if ( clc.demoplaying || !serverAddress )", source)
        self.assertIn("if ( port == 0 )", source)
        self.assertIn("serverAddress = &clc.netchan.remoteAddress;", source)

    def test_advertisement_bridge_routes_ui_cgame_and_renderer_imports(self) -> None:
        webui = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")
        cgame = (ROOT / "code" / "client" / "cl_cgame.cpp").read_text(encoding="utf-8")
        ui = (ROOT / "code" / "client" / "cl_ui.cpp").read_text(encoding="utf-8")
        header = (ROOT / "code" / "client" / "client.h").read_text(encoding="utf-8")
        client = (ROOT / "code" / "client" / "cl_main.cpp").read_text(encoding="utf-8")
        tr_public = (ROOT / "code" / "renderercommon" / "tr_public.h").read_text(encoding="utf-8")
        renderer_scene = (ROOT / "code" / "renderer" / "tr_scene.c").read_text(encoding="utf-8")
        renderer_init = (ROOT / "code" / "renderer" / "tr_init.c").read_text(encoding="utf-8")
        vk_scene = (ROOT / "code" / "renderervk" / "tr_scene.c").read_text(encoding="utf-8")
        vk_init = (ROOT / "code" / "renderervk" / "tr_init.c").read_text(encoding="utf-8")
        rtx_scene = (ROOT / "code" / "rendererrtx" / "tr_scene.c").read_text(encoding="utf-8")
        rtx_init = (ROOT / "code" / "rendererrtx" / "tr_init.c").read_text(encoding="utf-8")

        for prototype in (
            "qhandle_t CL_AdvertisementBridge_SetupAdvertCellShader",
            "void CL_AdvertisementBridge_InitCGame( void );",
            "void CL_AdvertisementBridge_SetActiveAdvert( int cellId );",
            "void CL_AdvertisementBridge_RefreshLoadingViewParameters( void );",
            "int CL_AdvertisementBridge_GetCellDisplayState( int cellId );",
            "void CL_AdvertisementBridge_GetLabelList2Entry( int index, char *buffer, int bufferSize );",
        ):
            with self.subTest(prototype=prototype):
                self.assertIn(prototype, header)

        self.assertIn("clAdvertisementBridgeState_t", webui)
        self.assertIn("activeAdvertCellId", webui)
        self.assertIn("activatedAdvertCellId", webui)
        self.assertIn('CL_WebUI_SetCvarIfChanged( "ui_advertisementBridgeProvider"', webui)
        self.assertIn('"retail WebUI advertisement bridge"', webui)
        self.assertIn("CL_AdvertisementBridge_RegisterDefaultAdvertCellShader", webui)
        self.assertIn("return re.RegisterShaderNoMip( defaultContent );", webui)
        self.assertIn("return 2;", webui)
        self.assertIn("return 1;", webui)
        self.assertIn('"cell %d activated"', webui)
        self.assertIn('"overlay: compiled=%d available=%d browser=%d"', webui)
        self.assertIn("void CL_AdvertisementBridge_RefreshLoadingViewParameters( void )", webui)
        self.assertIn("if ( !cl_advertisementBridge.initialised )", webui)
        self.assertIn("CL_AdvertisementBridge_RefreshLoadingViewParameters();", webui)

        self.assertIn("CL_AdvertisementBridge_InitCGame();", cgame)
        self.assertIn("CL_AdvertisementBridge_UpdateLoadingViewParameters();", cgame)
        self.assertIn("CL_AdvertisementBridge_SetupAdvertCellShader( defaultContent, rect, cellId );", cgame)
        self.assertIn("CL_AdvertisementBridge_SetMapPath( mapPath );", cgame)
        self.assertIn("CL_AdvertisementBridge_ClearDelay();", cgame)
        self.assertIn("CL_AdvertisementBridge_SetupUIAdvertCellShader( defaultContent, rect, cellId );", ui)
        self.assertIn("CL_AdvertisementBridge_InitUI();", ui)
        self.assertIn("CL_AdvertisementBridge_ActivateAdvert( cellId );", ui)

        self.assertIn("void\t(*AdvertisementBridge_UpdateLoadingViewParameters)( void );", tr_public)
        self.assertIn("void\t(*AdvertisementBridge_RefreshLoadingViewParameters)( void );", tr_public)
        self.assertIn("int\t\t(*AdvertisementBridge_GetCellDisplayState)( int cellId );", tr_public)
        self.assertIn("rimp.AdvertisementBridge_RefreshLoadingViewParameters = CL_AdvertisementBridge_RefreshLoadingViewParameters;", client)
        self.assertIn("rimp.AdvertisementBridge_GetCellDisplayState = CL_AdvertisementBridge_GetCellDisplayState;", client)
        self.assertIn("rimp.AdvertisementBridge_GetLabelList2Entry = CL_AdvertisementBridge_GetLabelList2Entry;", client)
        for scene, init in (
            (renderer_scene, renderer_init),
            (vk_scene, vk_init),
            (rtx_scene, rtx_init),
        ):
            with self.subTest(renderer="advertisement-loading-view"):
                self.assertIn("void AdvertisementBridge_UpdateLoadingViewParameters( void )", scene)
                self.assertIn("ri.AdvertisementBridge_RefreshLoadingViewParameters();", scene)
                self.assertIn("re.AdvertisementBridge_UpdateLoadingViewParameters = AdvertisementBridge_UpdateLoadingViewParameters;", init)

    def test_renderer_loads_ql_advertisement_lump_for_bridge_diagnostics(self) -> None:
        ql_bsp = (ROOT / "code" / "qcommon" / "ql_bsp.h").read_text(encoding="utf-8")
        self.assertIn("#define QL_BSP_ADVERTISEMENT_LUMP_INDEX 17", ql_bsp)
        self.assertIn("} qlBspAdvertisementDisk_t;", ql_bsp)
        self.assertIn("QLBSP_ReadAdvertisementLump", ql_bsp)
        self.assertIn("QLBSP_ParseAdvertisementModel", ql_bsp)

        for renderer in ("renderer", "renderervk", "rendererrtx"):
            with self.subTest(renderer=renderer):
                tr_local = (ROOT / "code" / renderer / "tr_local.h").read_text(encoding="utf-8")
                tr_bsp = (ROOT / "code" / renderer / "tr_bsp.c").read_text(encoding="utf-8")
                tr_world = (ROOT / "code" / renderer / "tr_world.c").read_text(encoding="utf-8")
                tr_init = (ROOT / "code" / renderer / "tr_init.c").read_text(encoding="utf-8")
                tr_main = (ROOT / "code" / renderer / "tr_main.c").read_text(encoding="utf-8")

                self.assertIn("#define\tMAX_MAP_ADVERTISEMENTS\t30", tr_local)
                self.assertIn("} qlAdvertisement_t;", tr_local)
                self.assertIn("GLuint\t\tocclusionQueryIds[2];", tr_local)
                self.assertIn("int\t\t\tnumAdvertisements;", tr_local)
                self.assertIn("qlAdvertisement_t\t*advertisements;", tr_local)
                self.assertIn("void\t\tR_AdvertisementList_f( void );", tr_local)
                self.assertIn("void\t\tR_UpdateAdvertisements( void );", tr_local)

                self.assertIn('#include "../qcommon/ql_bsp.h"', tr_bsp)
                self.assertIn("qlBspAdvertisementDisk_t record;", tr_bsp)
                self.assertIn("Com_Memcpy( &record, records + i * sizeof( record )", tr_bsp)
                self.assertIn("static void R_LoadAdvertisements( const lump_t *l )", tr_bsp)
                self.assertIn("QLBSP_ParseAdvertisementModel( in->model, &modelNum )", tr_bsp)
                self.assertIn("out[s_worldData.numAdvertisements].cellId = LittleLong( in->cellId );", tr_bsp)
                self.assertIn("out[s_worldData.numAdvertisements].occlusionQueryIds[0] = 0;", tr_bsp)
                self.assertIn("out[s_worldData.numAdvertisements].sourceIndex = i;", tr_bsp)
                self.assertIn("QLBSP_ReadAdvertisementLump( buffer.b", tr_bsp)
                self.assertIn("if ( header->version == BSP_VERSION_QL )", tr_bsp)
                self.assertRegex(
                    tr_bsp,
                    r"R_LoadSubmodels[^\n]*\n\tif \( header->version == BSP_VERSION_QL \) \{\n\t\tR_LoadAdvertisements",
                )
                self.assertIn("BSP_VERSION, BSP_VERSION_QL", tr_bsp)

                self.assertIn("void R_AdvertisementList_f( void )", tr_world)
                self.assertIn("static int R_CullAdvertisementQuad( const vec3_t points[4] )", tr_world)
                self.assertIn("static int R_AddAdvertisementSurface( qlAdvertisement_t *advertisement )", tr_world)
                self.assertIn("void R_UpdateAdvertisements( void )", tr_world)
                self.assertIn("R_inPVS( tr.refdef.vieworg, advertisement->center )", tr_world)
                self.assertIn("tr.currentEntity = &tr.worldEntity;", tr_world)
                self.assertIn("R_AddDrawSurf( surface->data, surface->shader, surface->fogIndex", tr_world)
                self.assertIn("advertisement->queryListIndex = -1;", tr_world)
                self.assertIn("advertisement->projectedNormalX = DotProduct", tr_world)
                self.assertIn('ri.Printf( PRINT_ALL, "advertlist: world=%s loaded=%d\\n"', tr_world)
                self.assertIn("advertisement->cellId", tr_world)
                self.assertIn("advertisement->points[3][2]", tr_world)

                self.assertIn('ri.Cmd_AddCommand( "advertlist", R_AdvertisementList_f );', tr_init)
                self.assertIn('ri.Cmd_RemoveCommand( "advertlist" );', tr_init)

                self.assertIn("R_AddWorldSurfaces ();\n\n\tR_UpdateAdvertisements();", tr_main)

                if renderer in ("renderervk", "rendererrtx"):
                    self.assertNotIn("R_QueueAdvertisementQueryCmd();", tr_main)
                    continue

                tr_cmds = (ROOT / "code" / renderer / "tr_cmds.c").read_text(encoding="utf-8")
                tr_backend = (ROOT / "code" / renderer / "tr_backend.c").read_text(encoding="utf-8")

                self.assertIn("} advertisementQueryEntry_t;", tr_local)
                self.assertIn("advertisementQueryCommand_t", tr_local)
                self.assertIn("RC_ADVERTISEMENT_QUERIES", tr_local)
                self.assertIn("void\t\tR_QueueAdvertisementQueryCmd( void );", tr_local)
                self.assertIn("void\t\tR_ShutdownAdvertisements( void );", tr_local)
                self.assertIn("void R_AddAdvertisementQueryCmd( const advertisementQueryEntry_t *entries, int numEntries );", tr_local)
                self.assertIn("R_ShutdownAdvertisements();", tr_bsp)
                self.assertIn("void R_QueueAdvertisementQueryCmd( void )", tr_world)
                self.assertIn("void R_ShutdownAdvertisements( void )", tr_world)
                self.assertIn("R_AddAdvertisementQueryCmd( r_advertisementQueryEntries, r_numAdvertisementQueryEntries );", tr_world)
                self.assertIn("advertisement->queryListIndex = r_numAdvertisementQueryEntries;", tr_world)
                self.assertIn("void R_AddAdvertisementQueryCmd( const advertisementQueryEntry_t *entries, int numEntries )", tr_cmds)
                self.assertIn("cmd->commandId = RC_ADVERTISEMENT_QUERIES;", tr_cmds)
                self.assertIn("static const void *RB_DrawAdvertisementQueries( const void *data )", tr_backend)
                self.assertIn("RB_DrawAdvertisementQueryQuad( cmd->entries[i].points );", tr_backend)
                self.assertIn("case RC_ADVERTISEMENT_QUERIES:", tr_backend)
                self.assertIn("R_QueueAdvertisementQueryCmd();", tr_main)

                if renderer == "renderer":
                    qgl = (ROOT / "code" / renderer / "qgl.h").read_text(encoding="utf-8")
                    self.assertIn("#define QGL_OCCLUSION_QUERY_PROCS", qgl)
                    self.assertIn("glGenQueriesARB", qgl)
                    self.assertIn('R_HaveExtension( "GL_ARB_occlusion_query" )', tr_init)
                    self.assertIn("qglGenQueriesARB( 2, out[s_worldData.numAdvertisements].occlusionQueryIds );", tr_bsp)
                    self.assertIn("qglBeginQueryARB( GL_SAMPLES_PASSED_ARB", tr_backend)
                    self.assertIn("qglDeleteQueriesARB( 2, advertisement->occlusionQueryIds );", tr_world)
                else:
                    self.assertIn("glRefConfig.occlusionQuery && qglGenQueries", tr_bsp)
                    self.assertIn("qglBeginQuery( GL_SAMPLES_PASSED", tr_backend)
                    self.assertIn("qglDeleteQueries( 2, advertisement->occlusionQueryIds );", tr_world)

    def test_win32_window_events_notify_webui_host(self) -> None:
        wndproc = (ROOT / "code" / "win32" / "win_wndproc.cpp").read_text(encoding="utf-8")

        self.assertIn("CL_WebHost_NotifyAppActivation( active );", wndproc)
        self.assertIn("case WM_SETCURSOR:", wndproc)
        self.assertIn("browserCursor = (HCURSOR)CL_WebHost_GetCursorHandle();", wndproc)
        self.assertIn("SetCursor( browserCursor );", wndproc)

    def test_webui_view_callbacks_track_cursor_tooltip_and_position(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")
        header = (ROOT / "code" / "client" / "client.h").read_text(encoding="utf-8")

        for prototype in (
            "void *CL_WebHost_OnChangeCursor( int cursorType );",
            "void CL_WebHost_OnChangeTooltip( const char *tooltip );",
            "qboolean CL_WebHost_RequestCursorPosition( int *x, int *y );",
        ):
            with self.subTest(prototype=prototype):
                self.assertIn(prototype, header)

        self.assertIn("char\t\ttooltip[MAX_QPATH];", source)
        self.assertIn("HCURSOR\t\tactiveCursorHandle;", source)
        self.assertIn("static HCURSOR CL_WebHost_LoadWin32CursorHandle", source)
        self.assertIn("case 2:\n\t\t\tcursorId = IDC_HAND;", source)
        self.assertIn("case 14:\n\t\t\tcursorId = IDC_NO;", source)
        self.assertIn("static void CL_WebHost_ClearCursorOverride", source)
        self.assertIn("CL_WebHost_ClearTooltip();", source)
        self.assertIn("CL_WebHost_ClearCursorOverride();", source)
        self.assertIn("void *CL_WebHost_OnChangeCursor( int cursorType )", source)
        self.assertIn("cl_webui.restoreCursorHandle = GetCursor();", source)
        self.assertIn("SetCursor( cl_webui.activeCursorHandle );", source)
        self.assertIn("void CL_WebHost_OnChangeTooltip( const char *tooltip )", source)
        self.assertIn('"{\\"tooltip\\":\\"%s\\"}"', source)
        self.assertIn('"web.tooltip"', source)
        self.assertIn("qboolean CL_WebHost_RequestCursorPosition( int *x, int *y )", source)
        self.assertIn('#include "../win32/win_local.h"', source)
        self.assertIn("cl_webui.cursorPositionValid = qtrue;", source)
        self.assertIn("cl_webui.cursorX = x;", source)
        self.assertIn("cl_webui.cursorY = y;", source)
        self.assertIn("if ( cl_webui.cursorPositionValid && ( cl_webui.browserVisible || cl_webui.browserActive ) )", source)
        self.assertIn("return CL_WebHost_GetCursorPosition( x, y );", source)
        self.assertIn("POINT point;", source)
        self.assertIn("GetCursorPos( &point )", source)
        self.assertIn("if ( g_wv.hWnd )", source)
        self.assertIn("ScreenToClient( g_wv.hWnd, &point );", source)
        self.assertIn("*x = point.x;", source)
        self.assertIn("*y = point.y;", source)

    def test_sdl_window_events_notify_webui_host(self) -> None:
        sdl_input = (ROOT / "code" / "sdl" / "sdl_input.cpp").read_text(encoding="utf-8")

        self.assertIn("case SDL_EVENT_WINDOW_FOCUS_LOST:", sdl_input)
        self.assertIn("case SDL_EVENT_WINDOW_FOCUS_GAINED:", sdl_input)
        self.assertIn("CL_WebHost_NotifyAppActivation( qfalse );", sdl_input)
        self.assertIn("CL_WebHost_NotifyAppActivation( qtrue );", sdl_input)

    def test_browser_mouse_capture_stays_out_of_relative_gameplay_mode(self) -> None:
        sdl_input = (ROOT / "code" / "sdl" / "sdl_input.cpp").read_text(encoding="utf-8")
        win_input = (ROOT / "code" / "win32" / "win_input.cpp").read_text(encoding="utf-8")
        linux_input = (ROOT / "code" / "unix" / "linux_glimp.cpp").read_text(encoding="utf-8")

        self.assertIn("browserActive = ( Key_GetCatcher() & KEYCATCH_BROWSER )", sdl_input)
        self.assertIn("browserActive || nativeUiActive || cgameUiActive", sdl_input)
        self.assertIn("const qboolean retailAbsolute = ( !consoleActive &&", sdl_input)
        self.assertIn("relativeMouse = ( in_mouse->integer > 0 && grabMouse )", sdl_input)
        self.assertIn("mouseAbsoluteMode != retailAbsolute", sdl_input)
        self.assertIn("IN_ShowCursor( qtrue );", sdl_input)
        self.assertIn("(int)e.motion.x, (int)e.motion.y", sdl_input)
        self.assertIn("&& !nativeUiActive && !cgameUiActive", sdl_input)
        self.assertIn("b = K_MOUSE6 + ( e.button.button - ( SDL_BUTTON_X2 + 1 ) );", sdl_input)
        self.assertIn("catcher & KEYCATCH_BROWSER", win_input)
        self.assertIn("catcher & KEYCATCH_UI", win_input)
        self.assertIn("catcher & KEYCATCH_CGAME", win_input)
        self.assertIn("kDInputMouseButtonKeys[]", win_input)
        self.assertIn("K_MOUSE5, K_MOUSE6, K_MOUSE7, K_MOUSE8", win_input)
        self.assertIn("buttonIndex < ARRAY_LEN( kDInputMouseButtonKeys )", win_input)
        self.assertIn("btn_code = event.xbutton.button - 8 + K_MOUSE6;", linux_input)
        browser_input = win_input[
            win_input.index("if ( absolutePointerOwner )"):
            win_input.index("if ( !gw_active", win_input.index("if ( absolutePointerOwner )"))
        ]
        self.assertIn("IN_DeactivateMouse();", browser_input)
        self.assertNotIn("IN_MouseMove();", browser_input)

    def test_screen_compositor_gives_webui_a_draw_pass(self) -> None:
        screen = (ROOT / "code" / "client" / "cl_scrn.cpp").read_text(encoding="utf-8")

        self.assertIn("Key_GetCatcher() & KEYCATCH_BROWSER", screen)
        self.assertIn('Cvar_VariableIntegerValue( "web_browserActive" )', screen)
        self.assertIn('Cvar_VariableIntegerValue( "ui_browserAwesomiumPending" )', screen)
        self.assertIn("browserSuppressUiRefresh = browserDrawableSurface || ( Key_GetCatcher() & KEYCATCH_BROWSER );", screen)
        self.assertIn("if ( uiVisible && !browserSuppressUiRefresh )", screen)
        self.assertIn("CL_WebHost_DrawBrowserSurface();", screen)

    def test_build_manifests_include_webui_client_source(self) -> None:
        expected = "code/client/cl_webui.cpp"

        self.assertIn(expected, (ROOT / "meson.build").read_text(encoding="utf-8"))
        self.assertIn("$(B)/client/cl_webui.o", (ROOT / "Makefile").read_text(encoding="utf-8"))
        self.assertIn(r"..\..\client\cl_webui.cpp", (ROOT / "code" / "win32" / "msvc2017" / "fnql.vcxproj").read_text(encoding="utf-8"))
        self.assertIn(r"..\..\client\cl_webui.cpp", (ROOT / "code" / "win32" / "msvc2005" / "fnql.vcproj").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
