from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_repo_file(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def enum_body(source: str, enum_name: str) -> str:
    for match in re.finditer(
        r"typedef enum\s*\{(?P<body>.*?)\}\s*(?P<name>[A-Za-z][A-Za-z0-9_]*)\s*;",
        source,
        re.DOTALL,
    ):
        if match.group("name") == enum_name:
            return match.group("body")
    raise AssertionError(f"Missing enum {enum_name}")


def qagame_native_import_names(g_public_h: str) -> list[str]:
    names: list[str] = []
    for match in re.finditer(r"\b(G_QL_IMPORT_[A-Z0-9_]+)\b(?:\s*=|,)", g_public_h):
        name = match.group(1)
        if name not in {
            "G_QL_IMPORT_COUNT",
            "G_QL_IMPORT_COMPAT_BASE",
            "G_QL_IMPORT_TOTAL_COUNT",
        }:
            names.append(name)
    return names


def enum_value(expr: str, values: dict[str, int]) -> int:
    total = 0
    for part in expr.split("+"):
        part = part.strip()
        if re.fullmatch(r"\d+", part):
            total += int(part)
        elif part in values:
            total += values[part]
        else:
            raise ValueError(f"Unsupported enum expression: {expr}")
    return total


def qagame_native_import_values(g_public_h: str) -> dict[str, int]:
    body = re.sub(r"/\*.*?\*/", "", enum_body(g_public_h, "gameNativeImport_t"), flags=re.DOTALL)
    body = re.sub(r"//.*", "", body)

    values: dict[str, int] = {}
    next_value = 0
    for entry in body.split(","):
        entry = entry.strip()
        if not entry:
            continue
        match = re.match(r"(G_QL_IMPORT_[A-Z0-9_]+)(?:\s*=\s*(.*))?$", entry)
        if match is None:
            continue
        name, expr = match.groups()
        try:
            value = next_value if expr is None else enum_value(expr, values)
        except ValueError:
            continue
        values[name] = value
        next_value = value + 1
    return values


def qagame_native_export_names(g_public_h: str) -> list[str]:
    names: list[str] = []
    for match in re.finditer(r"\b(GAME_NATIVE_EXPORT_[A-Z0-9_]+)\b(?:\s*=|,)", g_public_h):
        name = match.group(1)
        if name != "GAME_NATIVE_EXPORT_COUNT":
            names.append(name)
    return names


def ui_native_import_names(ui_public_h: str) -> list[str]:
    body_match = re.search(
        r"typedef enum\s*\{(?P<body>.*?)\}\s*uiQlImport_t\s*;",
        ui_public_h,
        re.DOTALL,
    )
    assert body_match is not None

    names: list[str] = []
    for match in re.finditer(r"\b(UI_QL_IMPORT_[A-Z0-9_]+)\b(?:\s*=|,)", body_match.group("body")):
        names.append(match.group(1))
    return names


def ui_native_export_names(ui_public_h: str) -> list[str]:
    names: list[str] = []
    for match in re.finditer(r"\b(UI_NATIVE_EXPORT_[A-Z0-9_]+)\b(?:\s*=|,)", ui_public_h):
        name = match.group(1)
        if name != "UI_NATIVE_EXPORT_COUNT":
            names.append(name)
    return names


def ui_call_for_native_export(name: str) -> str:
    suffix = name.removeprefix("UI_NATIVE_EXPORT_")
    if suffix == "HAS_UNIQUE_CD_KEY":
        return "UI_HASUNIQUECDKEY"
    return f"UI_{suffix}"


def qagame_legacy_import_dispatch_names(g_public_h: str) -> list[str]:
    body_match = re.search(
        r"typedef enum\s*\{(?P<body>.*?)\}\s*gameImport_t\s*;",
        g_public_h,
        re.DOTALL,
    )
    assert body_match is not None
    body = re.sub(r"/\*.*?\*/", "", body_match.group("body"), flags=re.DOTALL)
    body = re.sub(r"//.*", "", body)

    names: list[str] = []
    for entry in body.split(","):
        entry = entry.strip()
        if not entry:
            continue
        match = re.match(r"([A-Z][A-Z0-9_]*)(?:\s*=\s*([A-Z][A-Z0-9_]*|\d+))?$", entry)
        if match is None:
            continue
        name, value = match.groups()
        names.append(value if value and value.startswith("TRAP_") else name)
    return names


class UiNativeBridgeSourceTests(unittest.TestCase):
    def test_ui_public_exposes_recovered_ql_native_abi(self) -> None:
        ui_public = read_repo_file("code/ui/ui_public.h")

        self.assertIn("#define UI_QL_API_VERSION\t8", ui_public)
        self.assertIn("#define UI_QL_NATIVE_IMPORT_COUNT\t256", ui_public)
        self.assertIn("UI_QL_IMPORT_DRAW_SCALED_TEXT = 94", ui_public)
        self.assertIn("UI_QL_IMPORT_LAUNCHER_READSCREENSHOT = 110", ui_public)
        self.assertIn("UI_NATIVE_EXPORT_MOUSE_EVENT", ui_public)
        self.assertIn("UI_NATIVE_EXPORT_DRAW_ADVERTISEMENT_WAIT_SCREEN", ui_public)

    def test_vm_supports_structured_native_ui_exports(self) -> None:
        qcommon_h = read_repo_file("code/qcommon/qcommon.h")
        vm_local_h = read_repo_file("code/qcommon/vm_local.h")
        vm_c = read_repo_file("code/qcommon/vm.c")

        self.assertIn("VM_CreateNative", qcommon_h)
        self.assertIn("void\t\t*dllExports;", vm_local_h)
        self.assertIn("void\t\t*dllImports;", vm_local_h)
        self.assertIn("VM_CallNativeExports", vm_c)
        self.assertIn("uiExportIndex = callnum - 1;", vm_c)
        self.assertIn("case UI_MOUSE_EVENT:", vm_c)
        self.assertIn("dllExports[uiExportIndex]", vm_c)

    def test_vm_loader_can_materialize_native_modules_from_paks(self) -> None:
        vm_c = read_repo_file("code/qcommon/vm.c")

        self.assertIn("static void *VM_LoadDllFromPakCache", vm_c)
        self.assertIn("length = FS_ReadFile( filename, &buffer );", vm_c)
        self.assertIn('Com_sprintf( cacheName, sizeof( cacheName ), "native/%s", filename );', vm_c)
        self.assertIn('FS_BuildOSPath( basePath, ".tmp", cacheName )', vm_c)
        self.assertIn("libHandle = Sys_LoadLibrary( cachePath );", vm_c)
        self.assertIn("libHandle = VM_LoadDllFromPakCache( filename );", vm_c)

    def test_vm_loader_selects_retail_or_legacy_dllentry_before_calling_it(self) -> None:
        vm_c = read_repo_file("code/qcommon/vm.c")

        selector = vm_c[
            vm_c.index("static vmDllEntryAbi_t VM_SelectDllEntryAbi"):
            vm_c.index("static void *VM_LoadDllFromPakCache")
        ]
        loader = vm_c[
            vm_c.index("static void * QDECL VM_LoadDll"):
            vm_c.index("VM_Create\n")
        ]

        self.assertIn("if ( entryPoint )", selector)
        self.assertIn("return VM_DLL_ENTRY_LEGACY;", selector)
        self.assertIn("dllExports && dllImports && dllApiVersion", selector)
        self.assertIn("return VM_DLL_ENTRY_STRUCTURED;", selector)
        self.assertIn("entryAbi = VM_SelectDllEntryAbi", loader)
        self.assertIn("if ( entryAbi == VM_DLL_ENTRY_STRUCTURED )", loader)
        self.assertIn("dllEntryNative( dllExports, dllImports, dllApiVersion );", loader)
        self.assertIn("if ( entryAbi == VM_DLL_ENTRY_LEGACY )", loader)
        self.assertIn("dllEntry( systemcalls );", loader)
        structured_branch = loader[
            loader.index("if ( entryAbi == VM_DLL_ENTRY_STRUCTURED )"):
            loader.index("if ( entryAbi == VM_DLL_ENTRY_LEGACY )")
        ]
        self.assertIn("if ( !*dllExports )", structured_branch)
        self.assertIn("Sys_UnloadLibrary( libHandle );", structured_branch)
        self.assertIn("return NULL;", structured_branch)
        self.assertIn("return libHandle;", structured_branch)
        self.assertLess(
            structured_branch.index("if ( !*dllExports )"),
            structured_branch.index("return libHandle;"),
        )
        self.assertLess(
            loader.index("entryAbi = VM_SelectDllEntryAbi"),
            loader.index("dllEntryNative( dllExports, dllImports, dllApiVersion );"),
        )

    def test_client_ui_initializes_full_ql_import_table(self) -> None:
        cl_ui = read_repo_file("code/client/cl_ui.cpp")

        self.assertIn("static ql_import_f ql_ui_imports[UI_QL_NATIVE_IMPORT_COUNT];", cl_ui)
        self.assertIn("static void CL_InitUIImports( void )", cl_ui)
        self.assertIn("ql_ui_imports[UI_QL_IMPORT_MEASURE_TEXT]", cl_ui)
        self.assertIn("ql_ui_imports[UI_QL_IMPORT_GET_ITEM_DOWNLOAD_INFO]", cl_ui)
        self.assertIn("VM_CreateNative( VM_UI", cl_ui)
        self.assertIn("ql_ui_imports, UI_QL_API_VERSION", cl_ui)
        self.assertIn("return CL_IsSubscribedApp( appId ) ? 1 : 0;", cl_ui)

    def test_ui_native_text_imports_use_shared_scaled_text_bridge(self) -> None:
        client_h = read_repo_file("code/client/client.h")
        cl_ui = read_repo_file("code/client/cl_ui.cpp")

        self.assertIn("void\tRE_DrawScaledText(", client_h)
        self.assertIn("void\tRE_MeasureScaledText(", client_h)
        self.assertIn("RE_DrawScaledText( x, y, text, fontHandle, scale, maxX, outMaxX", cl_ui)
        self.assertIn("ql_ui_currentColor", cl_ui)
        self.assertIn("RE_MeasureScaledText( text, end, fontHandle, scale, maxX, &width, &height, outLeft );", cl_ui)
        self.assertIn("QL_UI_PackFloatBits64( width, height )", cl_ui)
        self.assertNotIn("QL_UI_MeasureFallbackText", cl_ui)

    def test_ui_item_download_info_import_uses_workshop_progress_then_steam_fallback(self) -> None:
        client_h = read_repo_file("code/client/client.h")
        cl_main = read_repo_file("code/client/cl_main.cpp")
        cl_ui = read_repo_file("code/client/cl_ui.cpp")
        cl_webui = read_repo_file("code/client/cl_webui.cpp")

        self.assertIn("qboolean CL_GetWorkshopDownloadInfo( unsigned int itemIdLow, unsigned int itemIdHigh, unsigned long long *outDownloaded, unsigned long long *outTotal );", client_h)
        self.assertIn("qboolean CL_Steam_GetItemDownloadInfo( unsigned int itemIdLow, unsigned int itemIdHigh, unsigned long long *outDownloaded, unsigned long long *outTotal );", client_h)
        self.assertIn("static qboolean CL_ParseUnsignedLongLongString", cl_main)
        self.assertIn("qboolean CL_GetWorkshopDownloadInfo( unsigned int itemIdLow, unsigned int itemIdHigh, unsigned long long *outDownloaded, unsigned long long *outTotal )", cl_main)
        self.assertIn('Cvar_VariableStringBuffer( "cl_downloadItem", itemText, sizeof( itemText ) );', cl_main)
        self.assertIn('Cvar_VariableStringBuffer( "cl_downloadCount", downloadedText, sizeof( downloadedText ) );', cl_main)
        self.assertIn('Cvar_VariableStringBuffer( "cl_downloadSize", totalText, sizeof( totalText ) );', cl_main)
        self.assertIn('Cvar_Set( "cl_downloadItem", "" );', cl_main)
        self.assertIn("if ( !CL_GetWorkshopDownloadInfo( itemIdLow, itemIdHigh, &downloaded, &total ) )", cl_ui)
        self.assertIn("CL_Steam_GetItemDownloadInfo( itemIdLow, itemIdHigh, &downloaded, &total );", cl_ui)
        self.assertIn("qboolean CL_Steam_GetItemDownloadInfo( unsigned int itemIdLow, unsigned int itemIdHigh, unsigned long long *outDownloaded, unsigned long long *outTotal )", cl_webui)
        self.assertIn("*outDownloaded = 0ull;", cl_webui)
        self.assertIn("*outTotal = 0ull;", cl_webui)

    def test_ui_launcher_screenshot_import_reads_screenshots_from_filesystem(self) -> None:
        cl_ui = read_repo_file("code/client/cl_ui.cpp")

        self.assertIn('#define CL_SCREENSHOT_SUBDIR "screenshots/"', cl_ui)
        self.assertIn("static qboolean CL_ScreenshotPathIsAllowed( const char *requestedName )", cl_ui)
        self.assertIn("strstr( requestedName, \"..\" )", cl_ui)
        self.assertIn("strchr( requestedName, ':' )", cl_ui)
        self.assertIn("static void CL_ScreenshotBuildQPath( const char *requestedName, char *qpath, int qpathLen )", cl_ui)
        self.assertIn("static int CL_MenuReadScreenshot( const char *requestedName, byte *buffer, int bufferSize )", cl_ui)
        self.assertIn("return -1;", cl_ui)
        self.assertIn("fileSize = FS_FOpenFileRead( qpath, &file, qtrue );", cl_ui)
        self.assertIn("FS_Read( buffer, bytesToRead, file );", cl_ui)
        self.assertIn("FS_FCloseFile( file );", cl_ui)
        self.assertIn("case UI_LAUNCHER_READSCREENSHOT:", cl_ui)
        self.assertIn("VM_CHECKBOUNDS( uivm, args[2], args[3] );", cl_ui)
        self.assertIn("return CL_MenuReadScreenshot( VMA(1), VMA(2), args[3] );", cl_ui)
        self.assertIn("UI_Import_Call( UI_LAUNCHER_READSCREENSHOT", cl_ui)

    def test_ui_advert_update_import_keeps_retail_noop_stub_shape(self) -> None:
        cl_ui = read_repo_file("code/client/cl_ui.cpp")

        update_advert = cl_ui[
            cl_ui.index("static void QDECL QL_UI_trap_UpdateAdvert"):
            cl_ui.index("static void QDECL QL_UI_trap_ActivateAdvert")
        ]

        self.assertIn("(void)handleOrToken;", update_advert)
        self.assertIn("(void)area;", update_advert)
        self.assertNotIn("CL_AdvertisementBridge_UpdateAdvert", update_advert)
        self.assertIn("ql_ui_imports[UI_QL_IMPORT_UNUSED_83] = (ql_import_f)QL_UI_trap_UpdateAdvert;", cl_ui)

    def test_ui_cm_loadmodel_import_keeps_retail_noop_stub_shape(self) -> None:
        cl_ui = read_repo_file("code/client/cl_ui.cpp")

        cm_load_model = cl_ui[
            cl_ui.index("static int QDECL QL_UI_trap_CM_LoadModel"):
            cl_ui.index("static void QDECL QL_UI_trap_S_StartLocalSound")
        ]

        self.assertIn("(void)name;", cm_load_model)
        self.assertIn("return 0;", cm_load_model)
        self.assertNotIn("UI_Import_Call( UI_CM_LOADMODEL", cm_load_model)
        self.assertIn("ql_ui_imports[UI_QL_IMPORT_CM_LOADMODEL] = (ql_import_f)QL_UI_trap_CM_LoadModel;", cl_ui)

    def test_ui_registersound_import_normalizes_retail_boolean_argument(self) -> None:
        cl_ui = read_repo_file("code/client/cl_ui.cpp")

        register_sound = cl_ui[
            cl_ui.index("static sfxHandle_t QDECL QL_UI_trap_S_RegisterSound_QL"):
            cl_ui.index("static void QDECL QL_UI_trap_Key_KeynumToStringBuf")
        ]

        self.assertIn("if ( compressed != qfalse && compressed != qtrue )", register_sound)
        self.assertIn("compressed = ( compressed != 0 ) ? qtrue : qfalse;", register_sound)
        self.assertIn("return S_RegisterSound( sample, static_cast<qboolean>( compressed ) );", register_sound)

    def test_ui_cursor_imports_share_webui_host_cursor_state(self) -> None:
        client_h = read_repo_file("code/client/client.h")
        cl_ui = read_repo_file("code/client/cl_ui.cpp")
        cl_webui = read_repo_file("code/client/cl_webui.cpp")

        self.assertIn("void CL_WebHost_SetCursorPosition( int x, int y );", client_h)
        self.assertIn("qboolean CL_WebHost_GetCursorPosition( int *x, int *y );", client_h)
        set_cursor = cl_ui[
            cl_ui.index("static int QDECL QL_UI_trap_SetCursorPos"):
            cl_ui.index("static int QDECL QL_UI_trap_GetCursorPos")
        ]
        self.assertIn("SetCursorPos( x, y )", set_cursor)
        self.assertIn("CL_WebHost_SetCursorPosition( x, y );", set_cursor)
        self.assertNotIn("(void)x;", set_cursor)
        get_cursor = cl_ui[
            cl_ui.index("static int QDECL QL_UI_trap_GetCursorPos"):
            cl_ui.index("static int QDECL QL_UI_trap_IsSubscribedApp")
        ]
        self.assertIn("GetCursorPos( &point )", get_cursor)
        self.assertIn("CL_WebHost_SetCursorPosition( point.x, point.y );", get_cursor)
        self.assertIn("return CL_WebHost_GetCursorPosition( x, y ) ? 1 : 0;", get_cursor)
        self.assertIn("void CL_WebHost_SetCursorPosition( int x, int y )", cl_webui)
        self.assertIn("qboolean CL_WebHost_GetCursorPosition( int *x, int *y )", cl_webui)
        self.assertIn("CL_WebHost_SetCursorPosition( x, y );", cl_webui)

    def test_console_completion_bridges_native_ui_arena_names(self) -> None:
        cl_console = read_repo_file("code/client/cl_console.cpp")

        self.assertIn("static void QDECL Con_CollectNativeArenaCompletionMatch", cl_console)
        self.assertIn("static void Con_CollectNativeArenaCompletionMatches( qboolean firstArg )", cl_console)
        self.assertIn("if ( firstArg || !uivm || !uivm->dllExports )", cl_console)
        self.assertIn("VM_Call( uivm, 1, UI_FOR_EACH_ARENA_NAME, (intptr_t)Con_CollectNativeArenaCompletionMatch );", cl_console)
        self.assertIn("Con_CollectNativeArenaCompletionMatches( firstArg ? qtrue : qfalse );", cl_console)

    def test_ui_key_events_pass_retail_timestamp_argument(self) -> None:
        cl_keys = read_repo_file("code/client/cl_keys.cpp")

        self.assertIn("((void (QDECL *)( int, qboolean, int ))exportFunc)", read_repo_file("code/qcommon/vm.c"))
        self.assertIn("VM_Call( uivm, 3, UI_KEY_EVENT, key, qtrue, time );", cl_keys)
        self.assertIn("VM_Call( uivm, 3, UI_KEY_EVENT, key, qfalse, time );", cl_keys)
        self.assertIn("VM_Call( uivm, 3, UI_KEY_EVENT, key | K_CHAR_FLAG, qtrue, cls.realtime );", cl_keys)
        self.assertNotIn("VM_Call( uivm, 2, UI_KEY_EVENT", cl_keys)

    def test_ui_native_import_table_has_no_unbound_recovered_slots(self) -> None:
        ui_public = read_repo_file("code/ui/ui_public.h")
        cl_ui = read_repo_file("code/client/cl_ui.cpp")

        import_names = set(ui_native_import_names(ui_public))
        assigned_names = set(re.findall(r"ql_ui_imports\[(UI_QL_IMPORT_[A-Z0-9_]+)\]", cl_ui))

        self.assertEqual(102, len(import_names))
        self.assertFalse(import_names - assigned_names)

    def test_ui_native_export_table_is_dispatched(self) -> None:
        ui_public = read_repo_file("code/ui/ui_public.h")
        vm_c = read_repo_file("code/qcommon/vm.c")

        export_names = set(ui_native_export_names(ui_public))
        expected_call_names = {ui_call_for_native_export(name) for name in export_names}
        handled_call_names = set(re.findall(r"case\s+(UI_[A-Z0-9_]+)\s*:", vm_c))

        self.assertEqual(14, len(export_names))
        self.assertFalse(expected_call_names - handled_call_names)
        self.assertIn("if ( vm->dllApiVersion > UI_API_VERSION )", vm_c)
        self.assertIn("uiExportIndex = callnum - 1;", vm_c)
        self.assertIn("dllExports[uiExportIndex]", vm_c)

    def test_cgame_register_cvars_bridge_is_wired(self) -> None:
        qcommon_h = read_repo_file("code/qcommon/qcommon.h")
        vm_c = read_repo_file("code/qcommon/vm.c")
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        self.assertIn("VM_CallCGameRegisterCvars", qcommon_h)
        self.assertIn("CG_NATIVE_EXPORT_REGISTER_CVARS", vm_c)
        self.assertIn("VM_CallCGameRegisterCvars( registrationVm );", cl_cgame)

    def test_server_game_native_import_slab_is_wired(self) -> None:
        g_public_h = read_repo_file("code/game/g_public.h")
        qcommon_h = read_repo_file("code/qcommon/qcommon.h")
        common_c = read_repo_file("code/qcommon/common.c")
        cvar_c = read_repo_file("code/qcommon/cvar.c")
        server_h = read_repo_file("code/server/server.h")
        sv_client = read_repo_file("code/server/sv_client.cpp")
        sv_bot = read_repo_file("code/server/sv_bot.cpp")
        sv_game = read_repo_file("code/server/sv_game.cpp")
        sv_ccmds = read_repo_file("code/server/sv_ccmds.cpp")
        sv_snapshot = read_repo_file("code/server/sv_snapshot.cpp")

        self.assertIn("#define\tGAME_MATCH_GUID_BUFFER_SIZE\t64", g_public_h)
        self.assertIn("TRAP_MATRIXMULTIPLY", qcommon_h)
        self.assertIn("G_MATRIXMULTIPLY = TRAP_MATRIXMULTIPLY", g_public_h)
        self.assertIn("G_TESTPRINTFLOAT = TRAP_TESTPRINTFLOAT", g_public_h)
        self.assertIn("G_STEAMID_QUERY", g_public_h)
        self.assertIn("G_STEAM_AUTH_VALIDATE", g_public_h)
        self.assertIn("G_RANK_BEGIN", g_public_h)
        self.assertIn("G_RANK_REPORT_STR", g_public_h)
        self.assertIn("void SV_RegisterGameCvars( void );", qcommon_h)
        self.assertIn("SV_RegisterGameCvars();", common_c)
        self.assertIn("qboolean\tSV_GameShouldSuppressVoiceToClient", server_h)
        self.assertIn("qboolean\tSV_GameIsClientAdmin", server_h)
        self.assertIn("qboolean\tSV_GameAreEnemyClients", server_h)
        self.assertIn("SV_ClientSteamId", server_h)
        self.assertIn("SV_VerifyClientSteamAuth", server_h)
        self.assertIn("SV_SteamStats_AddFieldValue", server_h)
        self.assertIn("SV_SteamStats_ProcessMatchReport", server_h)
        self.assertIn("SV_GetPlatformAuthProviderLabel", server_h)
        self.assertIn("SV_GetServerStatsProviderLabel", server_h)
        self.assertIn("SV_RefreshPlatformServiceCvars", server_h)
        self.assertIn("#define SV_PLATFORM_STEAM_ID_SIZE\t32", server_h)
        self.assertIn("char\t\t\tplatformSteamId[SV_PLATFORM_STEAM_ID_SIZE]", server_h)
        self.assertIn("SV_GameGetClientScore", server_h)
        self.assertIn("static void SV_CaptureClientSteamId", sv_client)
        self.assertIn("SV_MirrorClientSteamIdToUserinfo( newcl, userinfo.data()", sv_client)
        self.assertIn("SV_MirrorClientSteamIdToUserinfo( cl, cl->userinfo", sv_client)
        self.assertIn("qboolean SV_VerifyClientSteamAuth( int clientNum )", sv_client)
        self.assertIn("void SV_SteamStats_AddFieldValue", sv_client)
        self.assertIn("qboolean SV_SteamStats_HasAchievement", sv_client)
        self.assertIn("SV_GetPlatformAuthProviderLabel()", sv_client)
        self.assertIn("SV_GetPlatformAuthPolicyLabel()", sv_client)
        self.assertIn("SV_GetServerStatsProviderLabel()", sv_client)
        self.assertIn("SV_GetServerStatsPolicyLabel()", sv_client)
        self.assertIn("static void SV_LogSteamStatsStubLifecycle", sv_client)
        self.assertIn("accepted without identity verification", sv_client)
        self.assertIn("std::numeric_limits<std::uint64_t>::max", sv_client)
        self.assertIn('SV_LogSteamStatsStubLifecycle( "field-delta", detail );', sv_client)
        self.assertIn('SV_LogSteamStatsStubLifecycle( "achievement-unlock", detail );', sv_client)
        self.assertIn('SV_LogSteamStatsStubLifecycle( "achievement-query", detail );', sv_client)
        self.assertIn('SV_LogSteamStatsStubLifecycle( "match-report", "ignored MATCH_REPORT for disabled Steam stats owner" );', sv_client)
        self.assertIn('SV_LogSteamStatsStubLifecycle( "event-process", detail );', sv_client)
        self.assertGreaterEqual(sv_bot.count("platformSteamId[0] = '\\0';"), 2)
        self.assertIn("static ql_import_f ql_game_imports[GAME_NATIVE_IMPORT_COUNT];", sv_game)
        self.assertGreaterEqual(sv_game.count("std::array<intptr_t, MAX_VMMAIN_CALL_ARGS> args{}"), 2)
        self.assertNotIn("std::array<intptr_t, 14>", sv_game)
        self.assertIn("qagame native import call exceeds VM argument buffer", sv_game)
        self.assertIn("static qboolean sv_gameCvarsRegistered", sv_game)
        self.assertIn("static void SV_InitGameImports( void )", sv_game)
        self.assertIn("void SV_RegisterGameCvars( void )", sv_game)
        self.assertIn("cvar_t *Cvar_RegisterBounded", qcommon_h)
        self.assertIn("cvar_t *Cvar_RegisterBounded", cvar_c)
        self.assertIn("Cvar_CheckRange( cv, minimumValue, maximumValue, CV_FLOAT );", cvar_c)
        self.assertIn("return Cvar_Set2( var_name, val, qtrue );", sv_game)
        self.assertIn("return Cvar_RegisterBounded( cvar, var_name, value, minValue, maxValue, flags,", sv_game)
        self.assertIn("QL_BIND( G_QL_IMPORT_SUBMIT_MATCH_REPORT", sv_game)
        self.assertIn("static void QDECL QL_G_trap_SendConsoleCommandText( const char *text )", sv_game)
        self.assertIn("SV_GameImportCall( G_SEND_CONSOLE_COMMAND, static_cast<int>( EXEC_APPEND ), text );", sv_game)
        self.assertNotIn("QL_IMPORT1( QL_G_trap_SendConsoleCommandText", sv_game)
        self.assertIn("case TRAP_MATRIXMULTIPLY:", sv_game)
        self.assertIn("QL_BIND_COMPAT( TRAP_MATRIXMULTIPLY", sv_game)
        self.assertIn("case G_STEAMID_QUERY:", sv_game)
        self.assertIn("case G_STEAM_AUTH_VALIDATE:", sv_game)
        self.assertIn("case G_RANK_BEGIN:", sv_game)
        self.assertIn("SV_GameRankActive", sv_game)
        self.assertIn("return qfalse;", sv_game[sv_game.index("static qboolean SV_GameRankActive"):])
        self.assertIn("QL_BIND_COMPAT( G_STEAMID_QUERY", sv_game)
        self.assertIn("QL_BIND_COMPAT( G_RANK_REPORT_STR", sv_game)
        self.assertIn("QL_BIND_COMPAT( G_TRAP_GETVALUE", sv_game)
        self.assertIn("return SV_VerifyClientSteamAuth( clientNum );", sv_game)
        self.assertIn("static void SV_SubmitMatchReport", sv_game)
        self.assertIn("SV_SubmitMatchReport( report );", sv_game)
        self.assertIn("static void SV_ReportPlayerEvent", sv_game)
        self.assertIn("SV_ReportPlayerEvent( steamIdLow, steamIdHigh, clientStats, eventName, payload );", sv_game)
        self.assertIn("SV_SteamStats_AddFieldValue( clientNum, statIndex, delta );", sv_game)
        self.assertIn("SV_SteamStats_ProcessEvent( steamIdLow, steamIdHigh, clientStats, eventName, payload );", sv_game)
        self.assertIn("SV_SteamStats_HasAchievement( clientNum, achievementId )", sv_game)
        self.assertIn("static qboolean SV_ParseSteamIdString", sv_game)
        self.assertIn("SV_ParseSteamIdString( client.platformSteamId", sv_game)
        self.assertIn('Info_ValueForKey( client.userinfo, "steamid" )', sv_game)
        self.assertIn('Info_ValueForKey( client.userinfo, "steam" )', sv_game)
        self.assertIn("qboolean SV_ClientSteamId", sv_game)
        self.assertIn("SV_ClientSteamId( clientNum, &low, &high )", sv_game)
        self.assertIn("static void SV_GenerateMatchGuid", sv_game)
        self.assertIn("SV_CreateSystemGuid", sv_game)
        self.assertIn("CoCreateGuid", sv_game)
        self.assertIn("SV_CreateFallbackGuid", sv_game)
        self.assertIn("Com_RandomBytes( randomBytes, sizeof( randomBytes ) );", sv_game)
        self.assertIn("randomBytes[6] & 0x0f ) | 0x40", sv_game)
        self.assertIn("randomBytes[8] & 0x3f ) | 0x80", sv_game)
        self.assertIn("SV_FormatMatchGuid", sv_game)
        self.assertIn("GAME_MATCH_GUID_BUFFER_SIZE", sv_game)
        self.assertIn("#define JSON_IMPLEMENTATION", sv_game)
        self.assertIn("static qboolean SV_FactoryJsonHasId", sv_game)
        self.assertIn('JSON_ObjectGetNamedValue( objectJson, jsonEnd, "id" )', sv_game)
        self.assertIn('SV_FactoryFileHasId( "scripts/factories.txt", factoryName )', sv_game)
        self.assertIn('FS_GetFileList( "scripts", ".factory"', sv_game)
        self.assertIn("return SV_GameFactoryExists( factoryName );", sv_game)
        self.assertIn("static bool SV_GameNativeExportAvailable", sv_game)
        self.assertIn("GAME_NATIVE_EXPORT_SHOULD_SUPPRESS_VOICE_TO_CLIENT", sv_game)
        self.assertIn("GAME_NATIVE_EXPORT_IS_CLIENT_ADMIN", sv_game)
        self.assertIn("GAME_NATIVE_EXPORT_ARE_ENEMY_CLIENTS", sv_game)
        self.assertIn("VM_Call( gvm, 1, GAME_GET_CLIENT_SCORE, clientNum )", sv_game)
        self.assertIn("static unsigned long long SV_StatusClientSteamId", sv_ccmds)
        self.assertIn("SV_ClientSteamId( clientNum, &low, &high )", sv_ccmds)
        self.assertIn("SV_GameGetClientScore( slot.index, ps->persistant[PERS_SCORE] )", sv_ccmds)
        self.assertIn('Com_Printf( " rate steamid\\n" );', sv_ccmds)
        self.assertIn("VM_CreateNative( VM_GAME", sv_game)
        self.assertIn("static qboolean SV_CallGameRegisterCvarsOnce( vm_t *vm )", sv_game)
        self.assertIn("static void SV_ResetGameCvarRegistration( void )", sv_game)
        self.assertIn("VM_CallGameRegisterCvars( vm )", sv_game)
        self.assertIn("SV_CallGameRegisterCvarsOnce( gvm );", sv_game)
        self.assertGreaterEqual(sv_game.count("SV_ResetGameCvarRegistration();"), 4)
        self.assertIn("static bool SV_GameForcesSnapshotEntity", sv_snapshot)
        self.assertIn("GAME_CAN_CLIENT_SEE_CLIENT", sv_snapshot)
        self.assertIn("GAME_FREEZE_CAN_SEE_THAW_PROGRESS_EVENT", sv_snapshot)
        self.assertIn("GAME_IS_OBJECTIVE_ENTITY", sv_snapshot)
        self.assertIn("SV_GameForcesSnapshotEntity( frame->ps.clientNum, es->number )", sv_snapshot)

    def test_qagame_rankings_bridge_is_retained_but_disabled(self) -> None:
        server_h = read_repo_file("code/server/server.h")
        sv_main = read_repo_file("code/server/sv_main.cpp")
        sv_init = read_repo_file("code/server/sv_init.cpp")
        sv_game = read_repo_file("code/server/sv_game.cpp")

        self.assertIn("extern\tcvar_t\t*sv_enableRankings;", server_h)
        self.assertIn("extern\tcvar_t\t*sv_rankingsActive;", server_h)
        self.assertIn("void\t\tSV_GameRefreshRankingsPolicyCvars( void );", server_h)
        self.assertIn("cvar_t\t*sv_enableRankings;", sv_main)
        self.assertIn("cvar_t\t*sv_rankingsActive;", sv_main)

        self.assertIn('sv_enableRankings = Cvar_Get( "sv_enableRankings", "0", CVAR_ARCHIVE | CVAR_SERVERINFO );', sv_init)
        self.assertIn('Cvar_CheckRange( sv_enableRankings, "0", "1", CV_INTEGER );', sv_init)
        self.assertIn('sv_rankingsActive = Cvar_Get( "sv_rankingsActive", "0", CVAR_SERVERINFO );', sv_init)
        self.assertIn('Cvar_Get( "sv_rankingsProvider", "Unavailable", CVAR_ROM );', sv_init)
        self.assertIn('Cvar_Get( "sv_rankingsPolicy", "compatibility-unavailable", CVAR_ROM );', sv_init)
        self.assertIn("SV_GameRefreshRankingsPolicyCvars();", sv_init)
        self.assertIn("Cvar_SetGroup( sv_enableRankings, CVG_SERVER );", sv_init)
        self.assertIn("Cvar_SetGroup( sv_rankingsActive, CVG_SERVER );", sv_init)

        self.assertIn('SV_GAME_RANKINGS_PROVIDER = "Build-disabled (FnQL engine-only)"', sv_game)
        self.assertIn('SV_GAME_RANKINGS_POLICY = "compatibility-disabled (no live rankings owner)"', sv_game)
        self.assertIn('Cvar_Set( "sv_rankingsProvider", SV_GAME_RANKINGS_PROVIDER );', sv_game)
        self.assertIn('Cvar_Set( "sv_rankingsPolicy", SV_GAME_RANKINGS_POLICY );', sv_game)
        self.assertIn('Cvar_Set( "sv_rankingsActive", "0" );', sv_game)
        self.assertIn("static qboolean sv_gameRankStubAnnounced = qfalse;", sv_game)
        self.assertIn("static int sv_gameRankStubServerId = -1;", sv_game)
        self.assertIn("Rankings disabled by FnQL engine policy", sv_game)
        self.assertIn("Rankings requested but disabled by FnQL engine policy", sv_game)
        self.assertIn('Cvar_Set( "sv_enableRankings", "0" );', sv_game)
        self.assertIn("sv_gameRankStubServerId = sv.serverId;", sv_game)
        self.assertIn("return sv_gameRankStubServerId == sv.serverId ? qtrue : qfalse;", sv_game)
        self.assertIn("return qfalse;", sv_game[sv_game.index("static qboolean SV_GameRankActive"):])

    def test_qagame_platform_service_policy_cvars_are_exposed(self) -> None:
        server_h = read_repo_file("code/server/server.h")
        sv_init = read_repo_file("code/server/sv_init.cpp")
        sv_platform = read_repo_file("code/server/sv_platform.cpp")

        for name in [
            "SV_GetPlatformAuthProviderLabel",
            "SV_GetPlatformAuthPolicyLabel",
            "SV_GetSteamServerProviderLabel",
            "SV_GetSteamServerPolicyLabel",
            "SV_GetWorkshopProviderLabel",
            "SV_GetWorkshopPolicyLabel",
            "SV_GetServerStatsProviderLabel",
            "SV_GetServerStatsPolicyLabel",
            "SV_RefreshPlatformServiceCvars",
        ]:
            self.assertIn(name, server_h)
            self.assertIn(name, sv_platform)

        self.assertIn('"Build-disabled (FnQL engine-only)"', sv_platform)
        self.assertIn('"compatibility-unverified (Steamworks stub; identity is not authenticated)"', sv_platform)
        self.assertIn('"compatibility-disabled (no Steam GameServer owner)"', sv_platform)
        self.assertIn('"compatibility-disabled (retail assets only)"', sv_platform)
        self.assertIn('"compatibility-disabled (no live Steam stats owner)"', sv_platform)
        self.assertIn("constexpr unsigned int QL_STEAM_APP_ID = 282440u;", sv_platform)

        for cvar in [
            "sv_onlineServicesMode",
            "sv_onlineServicesPolicy",
            "sv_platformAuthProvider",
            "sv_platformAuthPolicy",
            "sv_steamServerProvider",
            "sv_steamServerPolicy",
            "sv_workshopProvider",
            "sv_workshopPolicy",
            "sv_statsProvider",
            "sv_statsPolicy",
        ]:
            self.assertIn(f'SetReadOnlyStatus( "{cvar}",', sv_platform)

        self.assertIn("SV_RefreshPlatformServiceCvars();", sv_init)
        self.assertLess(
            sv_init.index("SV_RefreshPlatformServiceCvars();"),
            sv_init.index("SV_GameRefreshRankingsPolicyCvars();"),
        )

    def test_ql_pure_checksum_handshake_uses_native_cgame_binary(self) -> None:
        qcommon_h = read_repo_file("code/qcommon/qcommon.h")
        cl_main = read_repo_file("code/client/cl_main.cpp")
        sv_client = read_repo_file("code/server/sv_client.cpp")
        sv_init = read_repo_file("code/server/sv_init.cpp")

        self.assertIn('#define\tQL_NATIVE_CGAME_DLL\t"cgamex86.dll"', qcommon_h)
        self.assertIn("FS_FileIsInPAK( QL_NATIVE_CGAME_DLL, &binChecksum, nullptr )", cl_main)
        self.assertIn('"cp %d %d "', cl_main)
        self.assertIn(
            "FS_ReferencedPakPureChecksums( static_cast<int>( cMsg.size() ) - len - 1 )",
            cl_main,
        )

        verify_paks = sv_client[
            sv_client.index("static void SV_VerifyPaks_f"):
            sv_client.index("static void SV_ResetPureClient_f")
        ]
        self.assertIn("FS_FileIsInPAK( QL_NATIVE_CGAME_DLL, &nBinChkSum, nullptr )", verify_paks)
        self.assertIn("SV_ParseInt(pArg) != nBinChkSum", verify_paks)
        self.assertNotIn('"vm/cgame.qvm"', verify_paks)
        self.assertNotIn('"vm/ui.qvm"', verify_paks)

        self.assertIn("FS_TouchFileInPak( QL_NATIVE_CGAME_DLL );", sv_init)
        self.assertNotIn('FS_TouchFileInPak( "vm/cgame.qvm" );', sv_init)
        self.assertNotIn('FS_TouchFileInPak( "vm/ui.qvm" );', sv_init)

    def test_qagame_native_import_table_has_no_unbound_recovered_slots(self) -> None:
        g_public_h = read_repo_file("code/game/g_public.h")
        sv_game = read_repo_file("code/server/sv_game.cpp")

        import_names = set(qagame_native_import_names(g_public_h))
        assigned_names = set(
            re.findall(r"ql_game_imports\[(G_QL_IMPORT_[A-Z0-9_]+)\]", sv_game)
        )
        assigned_names |= set(re.findall(r"QL_BIND\(\s*(G_QL_IMPORT_[A-Z0-9_]+)", sv_game))

        self.assertEqual(197, len(import_names))
        self.assertFalse(import_names - assigned_names)

    def test_qagame_native_import_tail_preserves_recovered_gaps(self) -> None:
        g_public_h = read_repo_file("code/game/g_public.h")
        sv_game = read_repo_file("code/server/sv_game.cpp")

        import_values = qagame_native_import_values(g_public_h)
        self.assertEqual(185, import_values["G_QL_IMPORT_SUBMIT_MATCH_REPORT"])
        self.assertEqual(186, import_values["G_QL_IMPORT_REPORT_PLAYER_EVENT"])
        self.assertEqual(192, import_values["G_QL_IMPORT_MARK_CLIENT_GOT_CP"])
        self.assertEqual(198, import_values["G_QL_IMPORT_RETURN_ZERO_198"])
        self.assertEqual(199, import_values["G_QL_IMPORT_RETURN_ZERO_199"])
        self.assertEqual(200, import_values["G_QL_IMPORT_STEAMID_QUERY"])
        self.assertEqual(206, import_values["G_QL_IMPORT_FACTORY_EXISTS"])
        self.assertEqual(207, import_values["G_QL_IMPORT_COUNT"])

        direct_slot_count = import_values["G_QL_IMPORT_COUNT"]
        named_direct_slots = {
            value
            for name, value in import_values.items()
            if name not in {"G_QL_IMPORT_COUNT", "G_QL_IMPORT_COMPAT_BASE", "G_QL_IMPORT_TOTAL_COUNT"}
            and value < direct_slot_count
        }
        self.assertEqual(set(range(187, 192)) | set(range(193, 198)), set(range(direct_slot_count)) - named_direct_slots)

        self.assertIn("static_assert( G_QL_IMPORT_SUBMIT_MATCH_REPORT == 185", sv_game)
        self.assertIn("static_assert( G_QL_IMPORT_MARK_CLIENT_GOT_CP == 192", sv_game)
        self.assertIn("static_assert( G_QL_IMPORT_RETURN_ZERO_198 == 198", sv_game)
        self.assertIn("static_assert( G_QL_IMPORT_RETURN_ZERO_199 == 199", sv_game)
        self.assertIn("static_assert( G_QL_IMPORT_FACTORY_EXISTS == 206", sv_game)
        self.assertIn("static_assert( G_QL_IMPORT_COUNT == 207", sv_game)
        self.assertIn("QL_BIND( G_QL_IMPORT_RETURN_ZERO_198, QL_G_trap_ReturnZero );", sv_game)
        self.assertIn("QL_BIND( G_QL_IMPORT_RETURN_ZERO_199, QL_G_trap_ReturnZero );", sv_game)

    def test_qagame_legacy_imports_are_handled_and_bound(self) -> None:
        g_public_h = read_repo_file("code/game/g_public.h")
        sv_game = read_repo_file("code/server/sv_game.cpp")

        import_names = set(qagame_legacy_import_dispatch_names(g_public_h))
        handled_names = set(re.findall(r"case\s+([A-Z][A-Z0-9_]+)\s*:", sv_game))
        bound_names = set(re.findall(r"QL_BIND_COMPAT\(\s*([A-Z][A-Z0-9_]+)", sv_game))

        self.assertEqual(202, len(import_names))
        self.assertFalse(import_names - handled_names)
        self.assertFalse(import_names - bound_names)

    def test_qagame_bot_debug_imports_reach_botlib(self) -> None:
        botlib_h = read_repo_file("code/botlib/botlib.h")
        be_aas_bsp_h = read_repo_file("code/botlib/be_aas_bsp.h")
        be_aas_bspq3 = read_repo_file("code/botlib/be_aas_bspq3.cpp")
        be_interface = read_repo_file("code/botlib/be_interface.cpp")
        be_ai_move = read_repo_file("code/botlib/be_ai_move.cpp")
        sv_game = read_repo_file("code/server/sv_game.cpp")

        self.assertIn("void\t(*BotDrawDebugAreas)(vec3_t origin, int enable, int areanum);", botlib_h)
        self.assertIn("void\t(*BotDrawAvoidSpots)(int movestate);", botlib_h)
        self.assertIn("qboolean AAS_inPVS(vec3_t p1, vec3_t p2);", be_aas_bsp_h)
        self.assertIn("return botimport.inPVS(p1, p2) ? qtrue : qfalse;", be_aas_bspq3)
        self.assertIn("static constexpr float RETAIL_BOT_DEBUG_AREA_REFRESH = 0.1f;", be_interface)
        self.assertIn("static constexpr int RETAIL_BOT_DEBUG_AREA_RADIUS = 512;", be_interface)
        self.assertIn("static void BotDrawDebugAreas(vec3_t origin, int enable, int areanum)", be_interface)
        self.assertIn("AAS_ShowAreaPolygons(area, LINECOLOR_ORANGE, qtrue);", be_interface)
        self.assertIn("AAS_DrawArrow(reach.start, reach.end, linecolor, LINECOLOR_YELLOW);", be_interface)
        self.assertIn("ai->BotDrawDebugAreas = BotDrawDebugAreas;", be_interface)
        self.assertIn("void BotDrawAvoidSpots(int movestate)", be_ai_move)
        self.assertIn("AAS_DrawCross(ms->avoidspots[i].origin, ms->avoidspots[i].radius, LINECOLOR_RED);", be_ai_move)
        self.assertIn("ai->BotDrawAvoidSpots = BotDrawAvoidSpots;", be_interface)
        self.assertIn("botlib_export->ai.BotDrawDebugAreas( origin, enable, areanum );", sv_game)
        self.assertIn("botlib_export->ai.BotDrawAvoidSpots( movestate );", sv_game)

    def test_qagame_native_export_table_is_dispatched(self) -> None:
        g_public_h = read_repo_file("code/game/g_public.h")
        vm_c = read_repo_file("code/qcommon/vm.c")

        export_names = set(qagame_native_export_names(g_public_h))
        dispatched_names = set(re.findall(r"dllExports\[(GAME_NATIVE_EXPORT_[A-Z0-9_]+)\]", vm_c))

        self.assertEqual(19, len(export_names))
        self.assertFalse(export_names - dispatched_names)
        self.assertIn("VM_CallGameRegisterCvars", vm_c)
        self.assertIn("dllExports[GAME_NATIVE_EXPORT_REGISTER_CVARS]", vm_c)

    def test_server_consumes_ql_retail_client_message_marker_before_commands(self) -> None:
        sv_client = read_repo_file("code/server/sv_client.cpp")

        marker = (
            "if ( cl->netchan.wireProfile == NETCHAN_WIRE_QL_RETAIL ) {\n"
            "\t\t(void)MSG_ReadByte( msg );\n"
            "\t}"
        )
        self.assertIn(marker, sv_client)
        self.assertLess(sv_client.index("cl->justConnected = qfalse;"), sv_client.index(marker))
        self.assertLess(sv_client.index(marker), sv_client.index("if ( cl->state == CS_CONNECTED )"))


if __name__ == "__main__":
    unittest.main()
