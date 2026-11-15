"""Regression gates for the fixes of the 2026-11-15 whole-engine audit.

Each case pins the guard a confirmed defect was closed with, grouped by the
kind of input that reached it: content a server or a downloaded pk3 controls,
server and operator state, the platform layers' windowless rule, and the
tooling.  The assertions name the guard rather than the code around it, so an
unrelated refactor does not trip them.
"""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

RENDERERS = ("renderer", "renderervk", "rendererrtx")


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def body(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    return source[begin : source.index(end, begin)]


class WebUiScriptBoundaryTests(unittest.TestCase):
    """The retained page owns the native bridge; remote text never becomes script."""

    def setUp(self) -> None:
        self.source = read("code/client/cl_webui.cpp")

    def test_dynamic_json_events_travel_through_json_parse(self) -> None:
        publish = body(
            self.source,
            "static void CL_WebHost_PublishDynamicJsonEvent(",
            "static qboolean CL_WebHost_UpdateBrowserCatalogCache(",
        )
        self.assertIn("CL_WebHost_JsonParseExpression( json )", publish)
        self.assertIn("catch(e){return;}", publish)
        # the document must not be a format argument of the script source
        self.assertNotIn("window.EnginePublish('%s',%s)", self.source)
        self.assertNotIn("Com_sprintf", publish)

    def test_server_stats_report_uses_the_parsed_publisher(self) -> None:
        self.assertIn('CL_WebHost_PublishDynamicJsonEvent( "game.stats.report", json );', self.source)

    def test_overlay_script_is_not_resolved_through_the_game_filesystem(self) -> None:
        inject = body(self.source, "static qboolean CL_WebHost_InjectFnqlOverlayAssets( void )", "cleanup:")
        self.assertIn('CL_WebPak_Fetch( "asset://ql/fnql-settings.js"', inject)
        self.assertIn('CL_WebPak_Fetch( "asset://ql/css/fnql-settings.css"', inject)
        self.assertNotIn("CL_LauncherRequestData(", inject)
        # CL_WebPak_Fetch stops at the paks and the loose install directories
        webpak = read("code/client/cl_webpak.cpp")
        fetch = body(webpak, "qboolean CL_WebPak_Fetch(", "int CL_WebPak_GetFileList(")
        self.assertIn("CL_WebPak_ReadInternal( normalized, outBuffer, outLength )", fetch)
        self.assertNotIn("FS_FOpenFileRead", fetch)

    def test_json_escape_covers_the_script_line_separators(self) -> None:
        escape = body(self.source, "static void CL_WebUI_JsonEscape(", "static void CL_WebUI_AppendJsonFragment(")
        self.assertIn('"\\\\u2028" : "\\\\u2029"', escape)
        self.assertIn("if ( out + 6 > limit ) {", escape)


class ClientUntrustedFileTests(unittest.TestCase):
    def test_roq_frame_size_is_checked_before_any_cell_or_pixel_is_written(self) -> None:
        source = read("code/client/cl_cin.cpp")
        fits = body(source, "static bool RoQ_QuadInfoFits(", "static bool readQuadInfo(")
        self.assertIn("width > static_cast<unsigned int>( DEFAULT_CIN_WIDTH )", fits)
        self.assertIn("height > static_cast<unsigned int>( DEFAULT_CIN_HEIGHT )", fits)
        self.assertIn("ARRAY_LEN( cin.qStatus[0] )", fits)
        read_info = body(source, "static bool readQuadInfo(", "static void RoQPrepMcomp(")
        self.assertLess(read_info.index("RoQ_QuadInfoFits("), read_info.index("cinTable[currentHandle].xsize    = width;"))
        interrupt = body(source, "static void RoQInterrupt()", "static qboolean RoQ_init( void )")
        self.assertIn("if ( !readQuadInfo( framedata ) ) {", interrupt)
        # a video chunk ahead of the quad info has no decoder to run through
        self.assertLess(
            interrupt.index("if ( cinTable[currentHandle].numQuads < 0 ) {"),
            interrupt.index("cinTable[currentHandle].VQ1("),
        )

    def test_empty_screenshot_does_not_leak_its_handle(self) -> None:
        source = read("code/client/cl_ui.cpp")
        block = body(source, "static int CL_MenuReadScreenshot(", "FS_Read( buffer, bytesToRead, file );")
        self.assertIn("fileHandle_t file = FS_INVALID_HANDLE;", block)
        self.assertLess(block.index("if ( fileSize <= 0 ) {"), block.index("FS_FCloseFile( file );"))

    def test_server_cache_counts_are_held_to_the_arrays(self) -> None:
        source = read("code/client/cl_ui.cpp")
        block = body(source, "static void LAN_LoadCachedServers( void )", "static void LAN_SaveServersToCache( void )")
        self.assertIn("cls.numglobalservers <= MAX_GLOBAL_SERVERS", block)
        self.assertIn("cls.numfavoriteservers <= MAX_OTHER_SERVERS", block)
        self.assertIn("if ( countsFit && size ==", block)

    def test_import_validation_messages_end_in_a_real_newline(self) -> None:
        source = read("code/client/cl_cgame.cpp")
        self.assertNotIn('\\\\n"', source)


class MapAndModelBoundsTests(unittest.TestCase):
    def test_v43_translator_ranges_cannot_wrap(self) -> None:
        source = read("code/qcommon/bsp_v43.h")
        self.assertIn("static qboolean BSP43_RangeFits(", source)
        self.assertIn("count <= total - first", source)
        for wrapped in (
            "firstSide + numSides > brushSideCount",
            "firstVert + numVerts > drawVertCount",
            "firstFace + numFaces > surfaceCount",
            "firstLeafBrush + numLeafBrushes > leafBrushCount",
            "firstLeafSurface + numLeafSurfaces > leafSurfaceCount",
        ):
            self.assertNotIn(wrapped, source)
        self.assertEqual(source.count("!BSP43_RangeFits("), 5)
        # output sizes are checked before the single allocation they add up to
        self.assertLess(source.index("BSP43_LumpBytes( totalShaders"), source.index("output = (byte *)allocFn( outputLength );"))
        self.assertIn("INT_MAX / (int)sizeof( int32_t ) - totalIndexes", source)

    def test_collision_loader_checks_the_indices_the_traces_follow(self) -> None:
        source = read("code/qcommon/cm_load.c")
        self.assertIn("static qboolean CMod_RangeFits(", source)
        nodes = body(source, "static void CMod_LoadNodes(", "static void CM_BoundBrush(")
        self.assertIn("planeNum < 0 || planeNum >= cm.numPlanes", nodes)
        self.assertIn("child >= count || ( child < 0 && -1 - child >= cm.numLeafs )", nodes)
        brushes = body(source, "static void CMod_LoadBrushes(", "static void CMod_LoadLeafs(")
        self.assertIn("firstSide < 0 || firstSide > cm.numBrushSides - 6", brushes)
        self.assertLess(brushes.index("has a bad side range"), brushes.index("CM_BoundBrush( out );"))
        leafs = body(source, "static void CMod_LoadLeafs(", "static void CMod_LoadPlanes(")
        self.assertLess(leafs.index("out->area >= MAX_MAP_AREAS"), leafs.index("cm.numAreas * cm.numAreas"))
        sides = body(source, "static void CMod_LoadBrushSides(", "static void CMod_LoadEntityString(")
        self.assertIn("num < 0 || num >= cm.numPlanes", sides)
        submodels = body(source, "static void CMod_LoadSubmodels(", "static void CMod_LoadNodes(")
        self.assertEqual(submodels.count("!CMod_RangeFits("), 2)
        self.assertLess(submodels.index("has a bad brush range"), submodels.index("Hunk_Alloc( out->leaf.numLeafBrushes * 4"))
        # leafs load before the lumps they index, so they are checked last
        load = body(source, "void CM_LoadMap(", "void CM_ClearMap(")
        self.assertLess(load.index("CMod_LoadPatches("), load.index("CMod_CheckLeafReferences();"))
        self.assertLess(load.index("CMod_CheckLeafReferences();"), load.index("CM_InitBoxHull();"))

    def test_translated_v43_image_is_released_after_a_failed_load(self) -> None:
        source = read("code/qcommon/cm_load.c")
        load = body(source, "void CM_LoadMap(", "void CM_ClearMap(")
        self.assertLess(load.index("CM_FreeTranslatedMap();"), load.index("BSP43_TranslateToV46("))
        self.assertIn("cm_translatedMap = translated.data;", load)

    def test_hunk_allocators_refuse_negative_and_oversized_requests(self) -> None:
        source = read("code/qcommon/common.c")
        permanent = body(source, "void *Hunk_Alloc( int size, ha_pref preference ) {", "void *Hunk_AllocateTempMemory( int size ) {")
        self.assertLess(permanent.index("if ( size < 0 || size > s_hunkTotal ) {"), permanent.index("size = PAD( size, 64 );"))
        temp = body(source, "void *Hunk_AllocateTempMemory( int size ) {", "void Hunk_FreeTempMemory( void *buf ) {")
        self.assertLess(
            temp.index("if ( size < 0 || size > s_hunkTotal ) {"),
            temp.index("size = PAD(size, sizeof(intptr_t)) + sizeof( hunkHeader_t );"),
        )

    def test_draw_surface_ranges_are_checked_in_every_renderer(self) -> None:
        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                source = read(f"code/{renderer}/tr_bsp.c")
                self.assertIn("static qboolean R_SurfaceRangeFits(", source)
                load = body(source, "static void R_LoadSurfaces(", "#ifdef PATCH_STITCHING")
                self.assertLess(load.index("has a bad size or vertex range"), load.index("ParseMesh( in, dv, out );"))
                self.assertLess(load.index("has a bad vertex or index range"), load.index("ParseTriSurf( in, dv, out, indexes );"))
                self.assertLess(load.index("has a bad vertex or index range"), load.index("ParseFace( in, dv, out, indexes );"))
                self.assertIn("(int64_t)width * height > MAX_PATCH_SIZE * MAX_PATCH_SIZE", load)
                face = body(source, "static void ParseFace(", "static void ParseMesh(")
                # the index copy no longer steers itself through the block it fills
                self.assertNotIn("(byte *)cv + cv->ofsIndices ))[i] = LittleLong", face)
                self.assertIn("if ( index < 0 || index >= numPoints ) {", face)

    def test_renderer_does_not_trust_the_visibility_header(self) -> None:
        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                source = read(f"code/{renderer}/tr_bsp.c")
                vis = body(source, "static void R_LoadVisibility(", "ShaderForShaderNum")
                self.assertIn("if ( len < 8 ) {", vis)
                self.assertIn("numClusters > ( len - 8 ) / clusterBytes", vis)
                self.assertIn("(int64_t)leafClusters > (int64_t)clusterBytes * 8", vis)
                self.assertLess(vis.index("numClusters > ( len - 8 ) / clusterBytes"), vis.index("s_worldData.numClusters = numClusters;"))

    def test_model_headers_are_held_to_the_copied_block(self) -> None:
        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                source = read(f"code/{renderer}/tr_model.c")
                self.assertIn("if ( size < (int)sizeof( md3Header_t ) || size > fileSize ) {", source)
                self.assertIn("hdr->ofsFrames < 0 || hdr->ofsTags < 0 || hdr->ofsSurfaces < 0 || hdr->numSurfaces < 0", source)
                self.assertIn("(int64_t)hdr->numTags * hdr->numFrames * (int64_t)sizeof( md3Tag_t ) > size", source)
                self.assertIn("if ( filesize < (int)sizeof( mdrHeader_t ) ) {", source)
                self.assertIn("if ( size < (int)sizeof( mdrHeader_t ) || size > filesize )", source)

    def test_tga_data_is_measured_before_the_image_is_allocated(self) -> None:
        source = read("code/renderercommon/tr_image_tga.c")
        self.assertLess(source.index("if ( needed > available ) {"), source.index("targa_rgba = ri.Malloc (numPixels);"))
        self.assertIn("const uint64_t pixels = (uint64_t)columns * rows;", source)
        self.assertNotIn("buf_p + columns * rows * targa_header.pixel_size / 8 > end", source)


class AudioDecoderTests(unittest.TestCase):
    def test_ogg_decoded_size_must_fit_an_int(self) -> None:
        source = read("code/client/audio/codecs/snd_codec_ogg.cpp")
        self.assertLess(
            source.index("numSamples > INT_MAX / ( (ogg_int64_t)OGGInfo->channels * OGG_SAMPLEWIDTH )"),
            source.index("stream->info.size = stream->info.samples * stream->info.channels * stream->info.width;"),
        )

    def test_short_wav_read_is_filled_with_silence(self) -> None:
        source = read("code/client/audio/codecs/snd_codec_wav.cpp")
        self.assertIn("const int bytesRead = FileRead( file.get(), buffer, info->size );", source)
        self.assertIn("if ( filled < info->size ) {", source)

    def test_music_chunks_are_read_in_whole_frames(self) -> None:
        source = read("code/client/audio/openal/AudioSystemBackend.inl")
        service = body(source, "void AudioSystem::ServiceBackgroundTrack() {", "++chunksQueuedThisCall;")
        self.assertIn("readBytes -= readBytes % static_cast<int>( wholeFrameBytes );", service)
        self.assertIn("S_CodecReadStream( backgroundStream_, readBytes, raw.data() )", service)


class HostFontTests(unittest.TestCase):
    def test_measuring_text_does_not_change_the_draw_colour(self) -> None:
        source = read("code/renderercommon/tr_font_stash.c")
        bounds = body(source, "static float QL_FonsTextBounds(", "QL_GetHostGlyph( font, codepoint, isize, iblur );")
        self.assertNotIn("RE_SetColor(", bounds)

    def test_face_buffers_are_released_newest_first(self) -> None:
        source = read("code/renderercommon/tr_font_stash.c")
        done = body(source, "void R_DoneHostFonts( void ) {", "qboolean RE_GetScaledFontMetrics(")
        self.assertIn("for ( i = QL_HOST_FACE_COUNT - 1; i >= 0; --i ) {", done)


class FilesystemAndModuleTests(unittest.TestCase):
    def test_cached_encrypted_pak_keeps_its_decrypted_copy(self) -> None:
        source = read("code/qcommon/files.c")
        cached = body(source, "static pack_t *FS_LoadCachedPK3(", "static void FS_InsertPK3ToCache(")
        self.assertIn("if ( pak->readFilename == NULL ) {", cached)
        self.assertNotIn("\tpak->readFilename = pak->pakFilename;\n\treturn pak;", cached.replace("\r\n", "\n"))
        self.assertIn("if ( encrypted && pack->readFilename == pack->pakFilename ) {", source)

    def test_decrypt_key_position_cannot_go_negative(self) -> None:
        source = read("code/qcommon/files.c")
        decrypt = body(source, "static char *FS_CreateQuakeLiveDecryptedZipFile(", "fclose( out );\n\n\tif ( ferror( in ) )".replace("\n", "\r\n") if "\r\n" in source else "if ( ferror( in ) )")
        self.assertIn("unsigned int keyPos;", decrypt)
        self.assertNotIn("streamPos", decrypt)

    def test_small_filesystem_leaks_are_closed(self) -> None:
        source = read("code/qcommon/files.c")
        listing = body(source, "char **FS_ListFiles(", "expanded;\r\n}" if "\r\n" in source else "expanded;\n}")
        self.assertIn("if ( files ) {", listing)
        profile = body(source, "int FS_ReadProfileFile(", "data = Hunk_AllocateTempMemory( length + 1 );")
        self.assertLess(profile.index("if ( handle != FS_INVALID_HANDLE ) {"), profile.index("return -1;"))

    def test_native_module_copies_are_removed_when_unloaded(self) -> None:
        source = read("code/qcommon/vm.c")
        self.assertIn("static void VM_ReleaseNativeCacheFile( vmIndex_t index ) {", source)
        free = body(source, "void VM_Free( vm_t *vm ) {", "void VM_Clear( void ) {")
        self.assertLess(free.index("Sys_UnloadLibrary( vm->dllHandle );"), free.index("VM_ReleaseNativeCacheFile( vm->index );"))
        load = body(source, "static void * QDECL VM_LoadDll(", "static qboolean VM_LoadNativeModule") if "static qboolean VM_LoadNativeModule" in source else source
        # every unload of a freshly loaded library gives its cache copy back
        self.assertEqual(source.count("Sys_UnloadLibrary( libHandle );"), 3)
        self.assertGreaterEqual(load.count("VM_ReleaseNativeCacheFile( index );"), 3)
        for loader in ("static void *VM_LoadDllFromPakCache(", "static void *VM_LoadPinnedDll("):
            block = body(source, loader, "\n}")
            self.assertIn("VM_DiscardNativeCacheFile( cachePath );", block)


class ServerStateTests(unittest.TestCase):
    def test_frame_stops_when_an_rcon_command_shut_the_server_down(self) -> None:
        source = read("code/server/sv_main.cpp")
        frame = body(source, "void SV_Frame( int msec, int realMsec ) {", "SV_CheckPaused()")
        executed = frame.index("SV_ExecutePendingAdminCommands();")
        advance = frame.index("svs.time += realMsec;")
        self.assertIn("if ( !com_sv_running->integer ) {", frame[executed:advance])

    def test_steam_loops_are_bounded_by_the_allocated_client_array(self) -> None:
        source = read("code/server/sv_platform.cpp")
        self.assertNotIn("< sv_maxclients->integer;", source)
        self.assertEqual(source.count("< sv.maxclients; ++"), 4)

    def test_steam_timers_survive_a_server_restart(self) -> None:
        source = read("code/server/sv_platform.cpp")
        self.assertIn("if ( sincePublished >= 0 && sincePublished < 1000 ) return;", source)
        self.assertIn("if ( sinceKeepalive < 0 || sinceKeepalive >= QL_STEAM_KEEPALIVE_MSEC ) {", source)

    def test_refused_reconnect_releases_the_slot_it_tore_down(self) -> None:
        source = read("code/server/sv_client.cpp")
        connect = body(source, "bool\t\treconnectTornDown = false;", "// build a new connection")
        self.assertLess(connect.index("SV_FreeClient( newcl );"), connect.index("reconnectTornDown = true;"))
        self.assertEqual(connect.count("if ( reconnectTornDown ) {"), 3)
        for refusal in (
            "A signed-in Steam identity is required for local play.",
            "A valid platform authentication ticket is required.",
            "Platform authentication rejected the connection.",
        ):
            after = connect[connect.index(refusal) :]
            self.assertLess(after.index("*newcl = {};"), after.index("return;"))

    def test_departed_player_steam_id_is_not_left_for_a_bot(self) -> None:
        source = read("code/server/sv_client.cpp")
        drop = body(source, "// nuke user info", "drop->justConnected = qfalse;")
        self.assertIn("if ( !isBot ) {", drop)
        self.assertIn("drop->platformSteamIdValue = 0;", drop)

    def test_resized_client_array_repoints_the_empty_queue_tail(self) -> None:
        source = read("code/server/sv_init.cpp")
        self.assertIn("slot.client.netchan_end_queue = &slot.client.netchan_start_queue;", source)

    def test_rcon_peer_table_recycles_silent_request_reply_peers(self) -> None:
        source = read("code/server/sv_zmq.cpp")
        self.assertIn("std::uint64_t lastSeen = 0;", source)
        pump = body(source, "void Zmq_PumpRcon( void ) {", "qboolean Zmq_RconActive( void ) {")
        self.assertIn("candidate->requestDelimiter &&", pump)
        self.assertLess(pump.index("runtime.peers.erase( stalest );"), pump.index("++runtime.peerClock } );"))
        self.assertIn("peer->lastSeen = ++runtime.peerClock;", pump)
        # erase( end() ) is undefined when the command emptied the table
        self.assertNotIn("runtime.peers.erase( FindPeer( identity ) );", pump)


class WindowlessPlatformTests(unittest.TestCase):
    def test_kept_window_restart_stays_hidden_and_inactive(self) -> None:
        source = read("code/win32/win_glimp.cpp")
        kept = body(source, "if ( !CL_IsWindowResizeRestart() ) {", "} else {\n\t\t\tGetWindowRect".replace("\n", "\r\n") if "\r\n" in source else "GetWindowRect( g_wv.hWnd, &r );")
        self.assertIn("stylebits &= ~WS_VISIBLE;", kept)
        self.assertIn("SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE", kept)

    def test_opengl_failure_paths_do_not_prompt_or_switch_modes(self) -> None:
        source = read("code/win32/win_glimp.cpp")
        self.assertIn("const qboolean retryFullscreen = Com_IsWindowless() ? qfalse : qtrue;", source)
        self.assertIn('GLW_StartDriverAndSetMode( 3, "", 16, retryFullscreen, qfalse )', source)
        self.assertNotIn('GLW_StartDriverAndSetMode( 3, "", 16, qtrue, qfalse )', source)
        depth = body(source, "// verify desktop bit depth", "// do a CDS if needed")
        self.assertLess(depth.index("if ( Com_IsWindowless() )"), depth.index("else if ( MessageBox( NULL,"))

    def test_no_low_level_keyboard_hook_is_installed(self) -> None:
        source = read("code/win32/win_wndproc.cpp")
        self.assertNotIn("SetWindowsHookEx(", source)
        self.assertNotIn("WH_KEYBOARD_LL, WinKeyHook", source)

    def test_sdl_backend_leaves_pointer_and_gamma_alone(self) -> None:
        glimp = read("code/sdl/sdl_glimp.cpp")
        warp = glimp.index("SDL_WarpMouseInWindow( SDL_window,")
        self.assertIn("if ( !Com_IsWindowless() ) {", glimp[warp - 80 : warp])
        gamma = read("code/sdl/sdl_gamma.cpp")
        init = body(gamma, "void GLimp_InitGamma( glconfig_t *config )", "#else")
        self.assertLess(init.index("if ( Com_IsWindowless() ) {"), init.index("SDLGamma_TrackWindowMonitor( qfalse )"))

    def test_steam_provider_path_falls_back_to_the_active_code_page(self) -> None:
        source = read("code/platform/fnql_steam.cpp")
        convert = body(source, "bool Utf8ToWide(", "LibraryHandle OpenLibrary(")
        self.assertLess(convert.index("CP_UTF8, MB_ERR_INVALID_CHARS"), convert.index("MultiByteToWideChar(CP_ACP, 0,"))

    def test_registry_string_ends_at_its_returned_length(self) -> None:
        source = read("code/win32/win_shared.cpp")
        self.assertIn("buffer[dataSize] = '\\0';", source)

    def test_idle_linux_server_wait_ends_on_a_shutdown_signal(self) -> None:
        main = read("code/unix/unix_main.cpp")
        self.assertIn("while ( res == 0 && !Sys_ShutdownSignalPending() && NET_Sleep( 10 * 1000 ) );", main)
        self.assertIn("while ( !Sys_ShutdownSignalPending() && NET_Sleep( 3000 * 1000 ) )", main)
        self.assertIn("qboolean Sys_ShutdownSignalPending( void )", read("code/unix/linux_signals.cpp"))
        self.assertIn("qboolean Sys_ShutdownSignalPending(void);", read("code/unix/linux_local.h"))


class ToolingTests(unittest.TestCase):
    def test_client_launchers_default_to_windowless(self) -> None:
        for script in (
            "scripts/glx_runtime_sweep.py",
            "scripts/vk_runtime_sweep.py",
            "scripts/rtx_runtime_smoke.py",
            "scripts/dlight_shadow_test.py",
            "scripts/probe_protocol91_session.py",
        ):
            with self.subTest(script=script):
                self.assertIn('os.environ.setdefault("FNQL_HEADLESS", "2")', read(script))
        for probe in (
            "tests/retail_cgame_module_probe_tests.py",
            "tests/native_cgame_abi_probe_runtime_tests.py",
        ):
            with self.subTest(probe=probe):
                self.assertIn('FNQL_HEADLESS=os.environ.get("FNQL_HEADLESS", "2")', read(probe))

    def test_no_launcher_turns_steam_or_the_webui_off(self) -> None:
        for script in sorted((ROOT / "scripts").glob("*.py")):
            text = script.read_text(encoding="utf-8")
            with self.subTest(script=script.name):
                self.assertNotIn('"com_steamIntegration", "0"', text)
                self.assertNotIn('"cl_webuiEnable", "0"', text)

    def test_probe_treats_silence_and_reset_as_no_reply(self) -> None:
        self.assertIn("except (socket.timeout, ConnectionResetError):", read("scripts/probe_protocol91.py"))

    def test_root_archive_is_byte_stable_across_hosts(self) -> None:
        self.assertIn("info.create_system = 3", read("scripts/root_archive.py"))

    def test_release_manifest_paths_do_not_need_an_in_repo_output(self) -> None:
        release = read("scripts/release.py")
        self.assertIn("def display_path(path: Path) -> str:", release)
        self.assertNotIn("archive_path.relative_to(ROOT)", release)

    def test_multi_command_release_steps_fail_on_the_first_error(self) -> None:
        workflow = read(".github/workflows/release.yml")
        stamp = "      - name: Stamp release build version\n"
        windows_steps = 0
        position = workflow.find(stamp)
        while position != -1:
            # the default shell of a Windows runner is PowerShell, which only
            # reports the exit status of the last command in a step
            runner = workflow.rindex("    runs-on: ", 0, position)
            if "windows" in workflow[runner : workflow.index("\n", runner)]:
                windows_steps += 1
                step = workflow[position : workflow.index("        run: |", position)]
                # the MinGW jobs already run theirs under MSYS2's bash
                self.assertTrue(
                    "        shell: bash\n" in step or "        shell: msys2 {0}\n" in step,
                    step,
                )
            position = workflow.find(stamp, position + len(stamp))
        self.assertEqual(windows_steps, 4)
        glx = read(".github/workflows/glx-verification.yml")
        gates = glx[glx.index("      - name: List GLx RC gates") :]
        self.assertIn("        shell: bash\n", gates[: gates.index("        run: |")])

    def test_standalone_script_tests_are_registered(self) -> None:
        meson = read("meson.build")
        self.assertIn("files('tests/shadow_projection_source_tests.py')", meson)
        self.assertIn("files('tests/surfacelight_validation_docs_tests.py')", meson)

    def test_release_engine_targets_define_ndebug_and_tests_do_not(self) -> None:
        meson = read("meson.build").replace("\r\n", "\n")
        self.assertIn("engine_ndebug_args = get_option('buildtype') == 'release' ? ['-DNDEBUG'] : []", meson)
        self.assertIn("engine_c_args = common_c_args + engine_ndebug_args", meson)
        self.assertIn("engine_cpp_args = common_cpp_args + engine_ndebug_args", meson)
        # every engine target takes the engine arguments
        for line in (
            "  cpp_args: engine_cpp_args + ['-DBOTLIB'],",
            "  renderer_c_args = engine_c_args + ['-DUSE_RENDERER_DLOPEN']",
            "  renderer_cpp_args = engine_cpp_args + ['-DUSE_RENDERER_DLOPEN']",
            "    renderer_glx_static_c_args = engine_c_args + [",
            "    renderer_vk_static_c_args = engine_c_args + [",
            "    renderer_rtx_static_c_args = engine_c_args + [",
            "client_c_args = engine_c_args",
            "client_cpp_args = engine_cpp_args",
        ):
            with self.subTest(line=line.strip()):
                self.assertIn(line + "\n", meson)
        server = meson[meson.index("  server_exe = executable(server_binary_name,") :]
        server = server[: server.index("\n  )\n")]
        self.assertIn("c_args: engine_c_args + ['-DDEDICATED'],", server)
        self.assertIn("cpp_args: engine_cpp_args + ['-DDEDICATED'],", server)
        # The test executables assert() their results, and CI runs them from a
        # release build tree: the definition must not reach them, neither
        # through the shared arguments nor through Meson's own option.
        self.assertNotIn("common_c_args += ['-DNDEBUG']", meson)
        self.assertNotIn("common_cpp_args += ['-DNDEBUG']", meson)
        self.assertNotIn("b_ndebug", meson)
        tests = meson[meson.index("audiozonesc = executable('fnql-audiozonesc',") :]
        self.assertNotIn("engine_c_args", tests)
        self.assertNotIn("engine_cpp_args", tests)
        # what the definition selects
        platform = read("code/qcommon/q_platform.h")
        self.assertIn('#define PLATFORM_STRING OS_STRING "-" ARCH_STRING\n', platform.replace("\r\n", "\n"))


if __name__ == "__main__":
    unittest.main()
