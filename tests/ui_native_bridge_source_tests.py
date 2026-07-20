from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


# Retail uix86.dll's recovered structured import slab. Keep this independent
# from legacy uiImport_t syscall numbering: a native UI receives this pointer
# table through dllEntry(), whereas a QVM receives syscall numbers.
RETAIL_UI_NATIVE_IMPORT_BINDINGS = tuple(
    line.split(":", 1)
    for line in """
UI_QL_IMPORT_PRINT:QL_UI_trap_Print
UI_QL_IMPORT_ERROR:QL_UI_trap_Error
UI_QL_IMPORT_MILLISECONDS:QL_UI_trap_Milliseconds
UI_QL_IMPORT_REAL_TIME:QL_UI_trap_RealTime
UI_QL_IMPORT_CVAR_REGISTER:QL_UI_trap_Cvar_Register
UI_QL_IMPORT_CVAR_CREATE:QL_UI_trap_Cvar_Create
UI_QL_IMPORT_CVAR_UPDATE:QL_UI_trap_Cvar_Update
UI_QL_IMPORT_CVAR_SET:QL_UI_trap_Cvar_Set
UI_QL_IMPORT_CVAR_SET_VALUE:QL_UI_trap_Cvar_SetValue
UI_QL_IMPORT_CVAR_VARIABLE_STRING_BUFFER:QL_UI_trap_Cvar_VariableStringBuffer
UI_QL_IMPORT_CVAR_VARIABLE_VALUE:QL_UI_trap_Cvar_VariableValue
UI_QL_IMPORT_ARGC:QL_UI_trap_Argc
UI_QL_IMPORT_ARGV:QL_UI_trap_Argv
UI_QL_IMPORT_CMD_ARGS_BUFFER:QL_UI_trap_Cmd_ArgsBuffer_QL
UI_QL_IMPORT_FS_FOPENFILE:QL_UI_trap_FS_FOpenFile
UI_QL_IMPORT_FS_READ:QL_UI_trap_FS_Read
UI_QL_IMPORT_FS_WRITE:QL_UI_trap_FS_Write
UI_QL_IMPORT_FS_FCLOSEFILE:QL_UI_trap_FS_FCloseFile
UI_QL_IMPORT_FS_SEEK:QL_UI_trap_FS_Seek
UI_QL_IMPORT_FS_GETFILELIST:QL_UI_trap_FS_GetFileList
UI_QL_IMPORT_CMD_EXECUTETEXT:QL_UI_trap_Cmd_ExecuteText_QL
UI_QL_IMPORT_R_REGISTERMODEL:QL_UI_trap_R_RegisterModel
UI_QL_IMPORT_R_REGISTERSKIN:QL_UI_trap_R_RegisterSkin
UI_QL_IMPORT_R_REGISTERSHADERNOMIP:QL_UI_trap_R_RegisterShaderNoMip
UI_QL_IMPORT_R_CLEARSCENE:QL_UI_trap_R_ClearScene
UI_QL_IMPORT_R_ADDREFENTITYTOSCENE:QL_UI_trap_R_AddRefEntityToScene
UI_QL_IMPORT_R_ADDPOLYTOSCENE:QL_UI_trap_R_AddPolyToScene
UI_QL_IMPORT_R_ADDLIGHTTOSCENE:QL_UI_trap_R_AddLightToScene
UI_QL_IMPORT_R_RENDERSCENE:QL_UI_trap_R_RenderScene
UI_QL_IMPORT_R_SETCOLOR:QL_UI_trap_R_SetColor_QL
UI_QL_IMPORT_R_DRAWSTRETCHPIC:QL_UI_trap_R_DrawStretchPic
UI_QL_IMPORT_R_MODELBOUNDS:QL_UI_trap_R_ModelBounds
UI_QL_IMPORT_UPDATESCREEN:QL_UI_trap_UpdateScreen
UI_QL_IMPORT_CM_LERPTAG:QL_UI_trap_CM_LerpTag
UI_QL_IMPORT_S_STARTLOCALSOUND:QL_UI_trap_S_StartLocalSound
UI_QL_IMPORT_S_REGISTERSOUND:QL_UI_trap_S_RegisterSound_QL
UI_QL_IMPORT_KEY_KEYNUMTOSTRINGBUF:QL_UI_trap_Key_KeynumToStringBuf
UI_QL_IMPORT_KEY_GETBINDINGBUF:QL_UI_trap_Key_GetBindingBuf
UI_QL_IMPORT_KEY_SETBINDING:QL_UI_trap_Key_SetBinding
UI_QL_IMPORT_KEY_ISDOWN:QL_UI_trap_Key_IsDown
UI_QL_IMPORT_KEY_GETOVERSTRIKEMODE:QL_UI_trap_Key_GetOverstrikeMode
UI_QL_IMPORT_KEY_SETOVERSTRIKEMODE:QL_UI_trap_Key_SetOverstrikeMode
UI_QL_IMPORT_KEY_CLEARSTATES:QL_UI_trap_Key_ClearStates
UI_QL_IMPORT_KEY_GETCATCHER:QL_UI_trap_Key_GetCatcher
UI_QL_IMPORT_KEY_SETCATCHER:QL_UI_trap_Key_SetCatcher
UI_QL_IMPORT_GETCLIPBOARDDATA:QL_UI_trap_GetClipboardData
UI_QL_IMPORT_GETCLIENTSTATE:QL_UI_trap_GetClientState
UI_QL_IMPORT_GETGLCONFIG:QL_UI_trap_GetGlconfig
UI_QL_IMPORT_GETCONFIGSTRING:QL_UI_trap_GetConfigString
UI_QL_IMPORT_LAN_GETSERVERCOUNT:QL_UI_trap_LAN_GetServerCount
UI_QL_IMPORT_LAN_GETSERVERADDRESSSTRING:QL_UI_trap_LAN_GetServerAddressString
UI_QL_IMPORT_LAN_GETSERVERINFO:QL_UI_trap_LAN_GetServerInfo
UI_QL_IMPORT_LAN_GETSERVERPING:QL_UI_trap_LAN_GetServerPing
UI_QL_IMPORT_LAN_GETPINGQUEUECOUNT:QL_UI_trap_LAN_GetPingQueueCount
UI_QL_IMPORT_LAN_CLEARPING:QL_UI_trap_LAN_ClearPing
UI_QL_IMPORT_LAN_GETPING:QL_UI_trap_LAN_GetPing
UI_QL_IMPORT_LAN_GETPINGINFO:QL_UI_trap_LAN_GetPingInfo
UI_QL_IMPORT_LAN_LOADCACHEDSERVERS:QL_UI_trap_LAN_LoadCachedServers
UI_QL_IMPORT_LAN_SAVECACHEDSERVERS:QL_UI_trap_LAN_SaveCachedServers
UI_QL_IMPORT_LAN_MARKSERVERVISIBLE:QL_UI_trap_LAN_MarkServerVisible
UI_QL_IMPORT_LAN_SERVERISVISIBLE:QL_UI_trap_LAN_ServerIsVisible
UI_QL_IMPORT_LAN_UPDATEVISIBLEPINGS:QL_UI_trap_LAN_UpdateVisiblePings
UI_QL_IMPORT_LAN_ADDSERVER:QL_UI_trap_LAN_AddServer
UI_QL_IMPORT_LAN_REMOVESERVER:QL_UI_trap_LAN_RemoveServer
UI_QL_IMPORT_LAN_RESETPINGS:QL_UI_trap_LAN_ResetPings
UI_QL_IMPORT_LAN_SERVERSTATUS:QL_UI_trap_LAN_ServerStatus
UI_QL_IMPORT_LAN_COMPARESERVERS:QL_UI_trap_LAN_CompareServers
UI_QL_IMPORT_MEMORY_REMAINING:QL_UI_trap_MemoryRemaining
UI_QL_IMPORT_GET_CDKEY:QL_UI_trap_GetCDKey
UI_QL_IMPORT_SET_CDKEY:QL_UI_trap_SetCDKey_QL
UI_QL_IMPORT_R_REGISTERFONT:QL_UI_trap_R_RegisterFont
UI_QL_IMPORT_S_STOPBACKGROUNDTRACK:QL_UI_trap_S_StopBackgroundTrack
UI_QL_IMPORT_S_STARTBACKGROUNDTRACK:QL_UI_trap_S_StartBackgroundTrack
UI_QL_IMPORT_CIN_PLAYCINEMATIC:QL_UI_trap_CIN_PlayCinematic
UI_QL_IMPORT_CIN_STOPCINEMATIC:QL_UI_trap_CIN_StopCinematic
UI_QL_IMPORT_CIN_DRAWCINEMATIC:QL_UI_trap_CIN_DrawCinematic
UI_QL_IMPORT_CIN_RUNCINEMATIC:QL_UI_trap_CIN_RunCinematic
UI_QL_IMPORT_CIN_SETEXTENTS:QL_UI_trap_CIN_SetExtents
UI_QL_IMPORT_R_REMAP_SHADER:QL_UI_trap_R_RemapShader
UI_QL_IMPORT_VERIFY_CDKEY:QL_UI_trap_VerifyCDKey
UI_QL_IMPORT_SETUP_ADVERT_CELL_SHADER:QL_UI_trap_SetupAdvertCellShader
UI_QL_IMPORT_REFRESH_ADVERT_CELL_SHADER:QL_UI_trap_RefreshAdvertCellShader
UI_QL_IMPORT_INIT_ADVERTISEMENT_BRIDGE:QL_UI_trap_InitAdvertisementBridge
UI_QL_IMPORT_UNUSED_83:QL_UI_trap_UpdateAdvert
UI_QL_IMPORT_ACTIVATE_ADVERT:QL_UI_trap_ActivateAdvert
UI_QL_IMPORT_UNUSED_85:QL_UI_trap_Unused85
UI_QL_IMPORT_SET_CURSOR_POS:QL_UI_trap_SetCursorPos
UI_QL_IMPORT_GET_CURSOR_POS:QL_UI_trap_GetCursorPos
UI_QL_IMPORT_PC_ADD_GLOBAL_DEFINE:QL_UI_trap_PC_AddGlobalDefine
UI_QL_IMPORT_PC_LOAD_SOURCE:QL_UI_trap_PC_LoadSource
UI_QL_IMPORT_PC_FREE_SOURCE:QL_UI_trap_PC_FreeSource
UI_QL_IMPORT_PC_READ_TOKEN:QL_UI_trap_PC_ReadToken
UI_QL_IMPORT_PC_SOURCE_FILE_AND_LINE:QL_UI_trap_PC_SourceFileAndLine
UI_QL_IMPORT_IS_SUBSCRIBED_APP:QL_UI_trap_IsSubscribedApp
UI_QL_IMPORT_DRAW_SCALED_TEXT:QL_UI_trap_DrawScaledText
UI_QL_IMPORT_MEASURE_TEXT:QL_UI_trap_MeasureText
UI_QL_IMPORT_GET_ITEM_DOWNLOAD_INFO:QL_UI_trap_GetItemDownloadInfo
""".strip().splitlines()
)

# These entries preserve the current source UI when it is loaded through the
# native bridge, but deliberately do not claim recovered retail slots.
UI_NATIVE_SOURCE_EXTENSION_BINDINGS = {
    "UI_QL_IMPORT_CVAR_RESET": (97, "QL_UI_trap_Cvar_Reset"),
    "UI_QL_IMPORT_CVAR_INFOSTRINGBUFFER": (98, "QL_UI_trap_Cvar_InfoStringBuffer"),
    "UI_QL_IMPORT_CM_LOADMODEL": (108, "QL_UI_trap_CM_LoadModel"),
    "UI_QL_IMPORT_SET_PBCLSTATUS": (109, "QL_UI_trap_SetPbClStatus"),
    "UI_QL_IMPORT_LAUNCHER_READSCREENSHOT": (110, "QL_UI_trap_Launcher_ReadScreenshot"),
}

RETAIL_UI_NATIVE_EXPORT_ORDER = (
    "UI_NATIVE_EXPORT_INIT",
    "UI_NATIVE_EXPORT_SHUTDOWN",
    "UI_NATIVE_EXPORT_KEY_EVENT",
    "UI_NATIVE_EXPORT_MOUSE_EVENT",
    "UI_NATIVE_EXPORT_REFRESH",
    "UI_NATIVE_EXPORT_IS_FULLSCREEN",
    "UI_NATIVE_EXPORT_SET_ACTIVE_MENU",
    "UI_NATIVE_EXPORT_CONSOLE_COMMAND",
    "UI_NATIVE_EXPORT_DRAW_CONNECT_SCREEN",
    "UI_NATIVE_EXPORT_HAS_UNIQUE_CD_KEY",
    "UI_NATIVE_EXPORT_REFRESH_DISPLAY_CONTEXT",
    "UI_NATIVE_EXPORT_MENUS_ANY_VISIBLE",
    "UI_NATIVE_EXPORT_FOR_EACH_ARENA_NAME",
    "UI_NATIVE_EXPORT_DRAW_ADVERTISEMENT_WAIT_SCREEN",
)


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


def ui_native_import_values(ui_public_h: str) -> dict[str, int]:
    body = re.sub(
        r"/\*.*?\*/", "", enum_body(ui_public_h, "uiQlImport_t"), flags=re.DOTALL
    )
    body = re.sub(r"//.*", "", body)

    values: dict[str, int] = {}
    next_value = 0
    for entry in body.split(","):
        entry = entry.strip()
        if not entry:
            continue
        match = re.match(r"(UI_QL_IMPORT_[A-Z0-9_]+)(?:\s*=\s*(.*))?$", entry)
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


def ui_native_import_bindings(cl_ui: str) -> dict[str, str]:
    return dict(
        re.findall(
            r"ql_ui_imports\[(UI_QL_IMPORT_[A-Z0-9_]+)\]\s*=\s*"
            r"\(ql_import_f\)(QL_UI_trap_[A-Za-z0-9_]+);",
            cl_ui,
        )
    )


def ui_native_export_values(ui_public_h: str) -> dict[str, int]:
    body = re.sub(
        r"/\*.*?\*/", "", enum_body(ui_public_h, "uiNativeExport_t"), flags=re.DOTALL
    )
    body = re.sub(r"//.*", "", body)

    values: dict[str, int] = {}
    next_value = 0
    for entry in body.split(","):
        entry = entry.strip()
        if not entry:
            continue
        match = re.match(r"(UI_NATIVE_EXPORT_[A-Z0-9_]+)(?:\s*=\s*(.*))?$", entry)
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

        self.assertIn("static void *VM_LoadDllFromPakCache( vmIndex_t index", vm_c)
        self.assertIn("length = FS_ReadFile( filename, &buffer );", vm_c)
        self.assertIn('"native/%s/%s"', vm_c)
        self.assertIn('FS_BuildOSPath( basePath, ".tmp", cacheName )', vm_c)
        self.assertIn("libHandle = Sys_LoadLibrary( cachePath );", vm_c)
        self.assertIn("libHandle = VM_LoadDllFromPakCache( index, filename );", vm_c)
        self.assertIn("VM_PinNativeModule( index, filename, buffer, length );", vm_c)

    def test_pure_restarts_only_reload_previously_pinned_native_bytes(self) -> None:
        qcommon_h = read_repo_file("code/qcommon/qcommon.h")
        vm_c = read_repo_file("code/qcommon/vm.c")
        ui = read_repo_file("code/client/cl_ui.cpp")
        cgame = read_repo_file("code/client/cl_cgame.cpp")

        self.assertIn("VMI_PINNED_NATIVE", qcommon_h)
        self.assertIn("VM_PINNED_NATIVE_MAX_BYTES", vm_c)
        self.assertIn("VM_LoadPinnedDll", vm_c)
        self.assertIn("Com_RandomBytes( nonce", vm_c)
        self.assertIn("pinnedOnly", vm_c)
        self.assertIn("VM_HasPinnedNativeModule( VM_UI )", ui)
        self.assertIn("VM_HasPinnedNativeModule( VM_CGAME )", cgame)
        self.assertIn("? VMI_PINNED_NATIVE : VMI_COMPILED", ui)
        self.assertIn("? VMI_PINNED_NATIVE : VMI_COMPILED", cgame)

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
        self.assertIn("RE_DrawScaledText( x, y, text, fontHandle, scale, limit, maxX", cl_ui)
        self.assertIn("ql_ui_currentColor", cl_ui)
        self.assertIn("RE_MeasureScaledText( text, end, fontHandle, scale, limit, bounds );", cl_ui)
        self.assertIn("fnql::font::CopyMeasureBounds( outLeft, bounds );", cl_ui)
        self.assertIn("width = bounds[2] - bounds[0];", cl_ui)
        self.assertIn("height = bounds[4];", cl_ui)
        self.assertIn("QL_UI_PackFloatBits64( width, height )", cl_ui)
        self.assertNotIn("QL_UI_MeasureFallbackText", cl_ui)

    def test_ui_native_glconfig_uses_retail_layout(self) -> None:
        client_h = read_repo_file("code/client/client.h")
        cl_ui = read_repo_file("code/client/cl_ui.cpp")

        self.assertIn("void\tCL_CopyRetailGlconfig( void *glconfig );", client_h)
        self.assertIn("CL_CopyRetailGlconfig( glconfig );", cl_ui)
        self.assertIn("generic UI syscall", cl_ui)

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

    def test_ui_registersound_import_matches_retail_one_argument_abi(self) -> None:
        cl_ui = read_repo_file("code/client/cl_ui.cpp")

        register_sound = cl_ui[
            cl_ui.index("static sfxHandle_t QDECL QL_UI_trap_S_RegisterSound_QL"):
            cl_ui.index("static void QDECL QL_UI_trap_Key_KeynumToStringBuf")
        ]

        self.assertIn(
            "QL_UI_trap_S_RegisterSound_QL( const char *sample )",
            register_sound,
        )
        self.assertNotIn("compressed", register_sound.split("{", 1)[0])
        self.assertIn("return S_RegisterSound( sample, qfalse );", register_sound)

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

    def test_console_completion_stays_owned_by_fnql(self) -> None:
        cl_console = read_repo_file("code/client/cl_console.cpp")
        cl_keys = read_repo_file("code/client/cl_keys.cpp")

        self.assertNotIn("Con_CollectNativeArenaCompletion", cl_console)
        self.assertNotIn("UI_FOR_EACH_ARENA_NAME", cl_console)
        self.assertIn("Field_QueryCompletionMatches", cl_console)
        self.assertIn("Field_QueryCompletionCandidates", cl_console)
        self.assertIn("Con_ApplySelectedCompletion", cl_console)

        console_key = cl_keys[
            cl_keys.index("static void Console_Key"):
            cl_keys.index("static void Message_Key")
        ]
        self.assertNotIn("Field_AutoComplete", console_key)
        self.assertIn("if ( Con_InputKey( key ) )", console_key)

    def test_ui_key_events_translate_legacy_tuple_to_retail_native_abi(self) -> None:
        cl_keys = read_repo_file("code/client/cl_keys.cpp")
        vm = read_repo_file("code/qcommon/vm.c")

        self.assertIn("const qboolean characterEvent = ( key & K_CHAR_FLAG ) ? qtrue : qfalse;", vm)
        self.assertIn("const int nativeKey = key & ~K_CHAR_FLAG;", vm)
        self.assertIn("((void (QDECL *)( int, qboolean, qboolean ))exportFunc)", vm)
        self.assertIn("characterEvent, VM_NormalizeQbooleanArg( args[1] )", vm)
        self.assertNotIn("VM_NormalizeQbooleanArg( args[1] ), (int)args[2]", vm)
        self.assertIn("VM_Call( uivm, 3, UI_KEY_EVENT, key, qtrue, time );", cl_keys)
        self.assertIn("VM_Call( uivm, 3, UI_KEY_EVENT, key, qfalse, time );", cl_keys)
        self.assertIn("VM_Call( uivm, 3, UI_KEY_EVENT, utf8Byte | K_CHAR_FLAG, qtrue, cls.realtime );", cl_keys)
        self.assertNotIn("VM_Call( uivm, 2, UI_KEY_EVENT", cl_keys)

    def test_ui_native_import_table_has_no_unbound_recovered_slots(self) -> None:
        ui_public = read_repo_file("code/ui/ui_public.h")
        cl_ui = read_repo_file("code/client/cl_ui.cpp")

        import_names = set(ui_native_import_names(ui_public))
        assigned_names = set(re.findall(r"ql_ui_imports\[(UI_QL_IMPORT_[A-Z0-9_]+)\]", cl_ui))

        self.assertEqual(102, len(import_names))
        self.assertFalse(import_names - assigned_names)

    def test_ui_native_import_abi_matches_retail_slots_and_host_adapters(self) -> None:
        ui_public = read_repo_file("code/ui/ui_public.h")
        cl_ui = read_repo_file("code/client/cl_ui.cpp")

        values = ui_native_import_values(ui_public)
        bindings = ui_native_import_bindings(cl_ui)
        expected_bindings = dict(RETAIL_UI_NATIVE_IMPORT_BINDINGS)
        expected_bindings.update(
            {
                name: adapter
                for name, (_, adapter) in UI_NATIVE_SOURCE_EXTENSION_BINDINGS.items()
            }
        )

        self.assertEqual(97, len(RETAIL_UI_NATIVE_IMPORT_BINDINGS))
        self.assertEqual(
            list(range(97)),
            [values[name] for name, _ in RETAIL_UI_NATIVE_IMPORT_BINDINGS],
        )
        self.assertEqual(
            {
                name: slot
                for name, (slot, _) in UI_NATIVE_SOURCE_EXTENSION_BINDINGS.items()
            },
            {
                name: values[name]
                for name in UI_NATIVE_SOURCE_EXTENSION_BINDINGS
            },
        )
        self.assertEqual(expected_bindings, bindings)

    def test_ui_native_export_table_is_dispatched(self) -> None:
        ui_public = read_repo_file("code/ui/ui_public.h")
        vm_c = read_repo_file("code/qcommon/vm.c")

        export_names = set(ui_native_export_names(ui_public))
        export_values = ui_native_export_values(ui_public)
        expected_call_names = {ui_call_for_native_export(name) for name in export_names}
        handled_call_names = set(re.findall(r"case\s+(UI_[A-Z0-9_]+)\s*:", vm_c))

        self.assertEqual(list(RETAIL_UI_NATIVE_EXPORT_ORDER), ui_native_export_names(ui_public))
        self.assertEqual(
            list(range(len(RETAIL_UI_NATIVE_EXPORT_ORDER))),
            [export_values[name] for name in RETAIL_UI_NATIVE_EXPORT_ORDER],
        )
        self.assertEqual(14, len(export_names))
        self.assertFalse(expected_call_names - handled_call_names)
        self.assertIn("if ( vm->dllApiVersion > UI_API_VERSION )", vm_c)
        self.assertIn("uiExportIndex = callnum - 1;", vm_c)
        self.assertIn("dllExports[uiExportIndex]", vm_c)

    def test_structured_native_ui_requires_the_retail_api_and_all_exports(self) -> None:
        vm_c = read_repo_file("code/qcommon/vm.c")

        self.assertIn("return UI_QL_API_VERSION;", vm_c)
        self.assertIn("return UI_NATIVE_EXPORT_COUNT;", vm_c)
        self.assertIn("if ( vm->dllApiVersion != expectedApiVersion )", vm_c)
        self.assertIn("if ( !dllExports[i] )", vm_c)
        self.assertIn("Rejected DLL '%s': missing native export slot %d", vm_c)

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
        sv_factory = read_repo_file("code/server/sv_factory.cpp")
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
        self.assertIn("static void SV_LogSteamStatsLifecycle", sv_client)
        self.assertIn("static fnql::server::stats::Session *SV_ClientStatsSession", sv_client)
        self.assertIn("compatibility-unverified", sv_client)
        self.assertIn("std::numeric_limits<std::uint64_t>::max", sv_client)
        self.assertIn("client.platformAuthSession && client.platformAuthValidated", sv_client)
        self.assertIn("FNQL_Steam_Capabilities()", sv_client)
        self.assertIn("FNQL_STEAM_CAP_STATS | FNQL_STEAM_CAP_GAME_SERVER_STATS", sv_client)
        self.assertIn("FNQL_Steam_RequestUserStats( client.platformSteamIdValue )", sv_client)
        self.assertIn('SV_LogSteamStatsLifecycle( "field-delta", "invalid retail stat index" );', sv_client)
        self.assertIn('SV_LogSteamStatsLifecycle( "achievement-unlock", "invalid retail achievement index" );', sv_client)
        self.assertIn("statsReports.Build", sv_client)
        self.assertIn("published report and eligible PLYR_STATS/PLYR_EVENTS summary", sv_client)
        self.assertIn("Zmq_SubmitMatchReportJson( document.data() );", sv_client)
        self.assertIn("Zmq_SubmitMatchSummaryJson( summary.data() );", sv_client)
        self.assertIn("Zmq_SubmitMatchReport( report );", sv_client)
        self.assertIn("Zmq_ReportPlayerEvent( steamIdLow, steamIdHigh, clientStats, eventName, payload );", sv_client)
        self.assertIn("Zmq_ReportPlayerEventJson( eventName, document.data() );", sv_client)
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
        self.assertIn("qboolean SV_FactoryExists( const char *factoryName );", server_h)
        self.assertIn("return SV_FactoryExists( factoryName );", sv_game)
        self.assertIn('"scripts/factories.txt"', sv_factory)
        self.assertIn('ForEachScriptFile( ".factories"', sv_factory)
        self.assertIn('ForEachScriptFile( ".factory"', sv_factory)
        self.assertIn("compatibility extension", sv_factory)
        self.assertIn("Cvar_SaveFactoryValue( setting.name.c_str() );", sv_factory)
        self.assertIn("Cvar_RestoreFactoryValue( setting.name.c_str() );", sv_factory)
        self.assertIn('Cvar_SetIntegerValue( "g_gametype"', sv_factory)
        self.assertIn('Cvar_Set( "g_factory"', sv_factory)
        self.assertIn('Cvar_Set( "g_factoryTitle"', sv_factory)
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
        self.assertIn('cvar_t *cvar = Cvar_Get( name, "", CVAR_ROM );', sv_platform)

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
