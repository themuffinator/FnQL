from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

# The x86 retail cgame's structured 128-slot import ABI.  Keep this explicit
# rather than inferring it from FnQL's current initializer so a swapped adapter
# is caught just as reliably as a missing one.
RETAIL_CGAME_NATIVE_IMPORT_BINDINGS = tuple(
    line.split(":", 2)
    for line in """
0:CG_QL_IMPORT_PRINT:QL_CG_trap_Print
1:CG_QL_IMPORT_ERROR:QL_CG_trap_Error
2:CG_QL_IMPORT_MILLISECONDS:QL_CG_trap_Milliseconds
3:CG_QL_IMPORT_REAL_TIME:QL_CG_trap_RealTime
4:CG_QL_IMPORT_CVAR_REGISTER:QL_CG_trap_Cvar_Register
5:CG_QL_IMPORT_CVAR_REGISTER_RANGE:QL_CG_trap_Cvar_RegisterRange
6:CG_QL_IMPORT_CVAR_UPDATE:QL_CG_trap_Cvar_Update
7:CG_QL_IMPORT_CVAR_SET:QL_CG_trap_Cvar_Set
8:CG_QL_IMPORT_CVAR_SET_VALUE:QL_CG_trap_Cvar_SetValue
9:CG_QL_IMPORT_CVAR_VARIABLESTRINGBUFFER:QL_CG_trap_Cvar_VariableStringBuffer
10:CG_QL_IMPORT_CVAR_VARIABLEVALUE:QL_CG_trap_Cvar_VariableValue
11:CG_QL_IMPORT_ARGC:QL_CG_trap_Argc
12:CG_QL_IMPORT_ARGV:QL_CG_trap_Argv
13:CG_QL_IMPORT_ARGS:QL_CG_trap_Args
14:CG_QL_IMPORT_FS_FOPENFILE:QL_CG_trap_FS_FOpenFile
15:CG_QL_IMPORT_FS_READ:QL_CG_trap_FS_Read
16:CG_QL_IMPORT_FS_WRITE:QL_CG_trap_FS_Write
17:CG_QL_IMPORT_FS_FCLOSEFILE:QL_CG_trap_FS_FCloseFile
18:CG_QL_IMPORT_FS_SEEK:QL_CG_trap_FS_Seek
19:CG_QL_IMPORT_FS_GETFILELIST:QL_CG_trap_FS_GetFileList
20:CG_QL_IMPORT_SENDCONSOLECOMMAND:QL_CG_trap_SendConsoleCommand
21:CG_QL_IMPORT_ADDCOMMAND:QL_CG_trap_AddCommand
22:CG_QL_IMPORT_REMOVECOMMAND:QL_CG_trap_RemoveCommand
23:CG_QL_IMPORT_SENDCLIENTCOMMAND:QL_CG_trap_SendClientCommand
24:CG_QL_IMPORT_UPDATESCREEN:QL_CG_trap_UpdateScreen
25:CG_QL_IMPORT_CM_LOADMAP:QL_CG_trap_CM_LoadMap
26:CG_QL_IMPORT_CM_NUMINLINEMODELS:QL_CG_trap_CM_NumInlineModels
27:CG_QL_IMPORT_CM_INLINEMODEL:QL_CG_trap_CM_InlineModel
28:CG_QL_IMPORT_CM_TEMPBOXMODEL:QL_CG_trap_CM_TempBoxModel
29:CG_QL_IMPORT_CM_TEMPCAPSULEMODEL:QL_CG_trap_CM_TempCapsuleModel
30:CG_QL_IMPORT_CM_POINTCONTENTS:QL_CG_trap_CM_PointContents
31:CG_QL_IMPORT_CM_TRANSFORMEDPOINTCONTENTS:QL_CG_trap_CM_TransformedPointContents
32:CG_QL_IMPORT_CM_BOXTRACE:QL_CG_trap_CM_BoxTrace
33:CG_QL_IMPORT_CM_CAPSULETRACE:QL_CG_trap_CM_CapsuleTrace
34:CG_QL_IMPORT_CM_TRANSFORMEDBOXTRACE:QL_CG_trap_CM_TransformedBoxTrace
35:CG_QL_IMPORT_CM_TRANSFORMEDCAPSULETRACE:QL_CG_trap_CM_TransformedCapsuleTrace
36:CG_QL_IMPORT_CM_MARKFRAGMENTS:QL_CG_trap_CM_MarkFragments
37:CG_QL_IMPORT_S_STARTSOUND:QL_CG_trap_S_StartSound
38:CG_QL_IMPORT_S_STARTSOUND_VOLUME:QL_CG_trap_S_StartSoundVolume
39:CG_QL_IMPORT_S_STARTLOCALSOUND:QL_CG_trap_S_StartLocalSound
40:CG_QL_IMPORT_S_STARTLOCALSOUND_VOLUME:QL_CG_trap_S_StartLocalSoundVolume
41:CG_QL_IMPORT_S_CLEARLOOPINGSOUNDS_FRAME:QL_CG_trap_S_ClearLoopingSoundsFrame
42:CG_QL_IMPORT_S_CLEARLOOPINGSOUNDS_KILLALL:QL_CG_trap_S_ClearLoopingSoundsKillAll
43:CG_QL_IMPORT_S_ADDLOOPINGSOUND:QL_CG_trap_S_AddLoopingSound
44:CG_QL_IMPORT_S_UPDATEENTITYPOSITION:QL_CG_trap_S_UpdateEntityPosition
45:CG_QL_IMPORT_S_RESPATIALIZE:QL_CG_trap_S_Respatialize
46:CG_QL_IMPORT_S_REGISTERSOUND:QL_CG_trap_S_RegisterSound
47:CG_QL_IMPORT_S_STARTBACKGROUNDTRACK:QL_CG_trap_S_StartBackgroundTrack
48:CG_QL_IMPORT_S_STOPBACKGROUNDTRACK:QL_CG_trap_S_StopBackgroundTrack
49:CG_QL_IMPORT_R_LOADWORLDMAP:QL_CG_trap_R_LoadWorldMap
50:CG_QL_IMPORT_R_REGISTERMODEL:QL_CG_trap_R_RegisterModel
51:CG_QL_IMPORT_R_REGISTERSKIN:QL_CG_trap_R_RegisterSkin
52:CG_QL_IMPORT_R_REGISTERSHADER:QL_CG_trap_R_RegisterShader
53:CG_QL_IMPORT_R_REGISTERSHADERNOMIP:QL_CG_trap_R_RegisterShaderNoMip
54:CG_QL_IMPORT_ADVERTISEMENTBRIDGE_RESERVED_21C0:QL_CG_trap_AdvertisementBridge_Reserved21C0
55:CG_QL_IMPORT_SETUP_ADVERT_CELL_SHADER:QL_CG_trap_SetupAdvertCellShader
56:CG_QL_IMPORT_REFRESH_ADVERT_CELL_SHADER:QL_CG_trap_RefreshAdvertCellShader
57:CG_QL_IMPORT_SET_ACTIVE_ADVERT:QL_CG_trap_SetActiveAdvert
58:CG_QL_IMPORT_UPDATE_ADVERT:QL_CG_trap_UpdateAdvert
59:CG_QL_IMPORT_ADVERTISEMENTBRIDGE_RESERVED_59:QL_CG_trap_RetailReservedImport
60:CG_QL_IMPORT_ADVERTISEMENTBRIDGE_SET_MAP_PATH:QL_CG_trap_AdvertisementBridge_SetMapPath
61:CG_QL_IMPORT_ADVERTISEMENTBRIDGE_INITCGAME:QL_CG_trap_AdvertisementBridge_InitCGame
62:CG_QL_IMPORT_ADVERTISEMENTBRIDGE_SHUTDOWNCGAME:QL_CG_trap_AdvertisementBridge_ShutdownCGame
63:CG_QL_IMPORT_ADVERTISEMENTBRIDGE_RESERVED_63:QL_CG_trap_RetailReservedImport
64:CG_QL_IMPORT_ADVERTISEMENTBRIDGE_SETFRAMETIME:QL_CG_trap_AdvertisementBridge_SetFrameTime
65:CG_QL_IMPORT_ADVERTISEMENTBRIDGE_RESERVED_65:QL_CG_trap_RetailReservedImport
66:CG_QL_IMPORT_RETAIL_RESERVED_66:QL_CG_trap_RetailReservedImport
67:CG_QL_IMPORT_RETAIL_RESERVED_67:QL_CG_trap_RetailReservedImport
68:CG_QL_IMPORT_RETAIL_RESERVED_68:QL_CG_trap_RetailReservedImport
69:CG_QL_IMPORT_ADVERTISEMENTBRIDGE_RESERVED_69:QL_CG_trap_RetailReservedImport
70:CG_QL_IMPORT_R_CLEARSCENE:QL_CG_trap_R_ClearScene
71:CG_QL_IMPORT_R_ADDREFENTITYTOSCENE:QL_CG_trap_R_AddRefEntityToScene
72:CG_QL_IMPORT_R_ADDPOLYTOSCENE:QL_CG_trap_R_AddPolyToScene
73:CG_QL_IMPORT_R_ADDPOLYSTOSCENE:QL_CG_trap_R_AddPolysToScene
74:CG_QL_IMPORT_R_ADDLIGHTTOSCENE:QL_CG_trap_R_AddLightToScene
75:CG_QL_IMPORT_R_LIGHTFORPOINT:QL_CG_trap_R_LightForPoint
76:CG_QL_IMPORT_R_RENDERSCENE:QL_CG_trap_R_RenderScene
77:CG_QL_IMPORT_ADVERTISEMENTBRIDGE_UPDATE_LOADING_VIEW_PARAMETERS:QL_CG_trap_AdvertisementBridge_UpdateLoadingViewParameters
78:CG_QL_IMPORT_R_SETCOLOR:QL_CG_trap_R_SetColor_QL
79:CG_QL_IMPORT_R_DRAWSTRETCHPIC:QL_CG_trap_R_DrawStretchPic
80:CG_QL_IMPORT_RETAIL_RESERVED_80:QL_CG_trap_RetailReservedImport
81:CG_QL_IMPORT_R_MODELBOUNDS:QL_CG_trap_R_ModelBounds
82:CG_QL_IMPORT_R_LERPTAG:QL_CG_trap_R_LerpTag
83:CG_QL_IMPORT_R_REMAP_SHADER:QL_CG_trap_R_RemapShader
84:CG_QL_IMPORT_GETGLCONFIG:QL_CG_trap_GetGlconfig
85:CG_QL_IMPORT_GETGAMESTATE:QL_CG_trap_GetGameState
86:CG_QL_IMPORT_GETCURRENTSNAPSHOTNUMBER:QL_CG_trap_GetCurrentSnapshotNumber
87:CG_QL_IMPORT_GETSNAPSHOT:QL_CG_trap_GetSnapshot
88:CG_QL_IMPORT_GETSERVERCOMMAND:QL_CG_trap_GetServerCommand
89:CG_QL_IMPORT_GETCURRENTCMDNUMBER:QL_CG_trap_GetCurrentCmdNumber
90:CG_QL_IMPORT_GETUSERCMD:QL_CG_trap_GetUserCmd
91:CG_QL_IMPORT_SETUSERCMDVALUE:QL_CG_trap_SetUserCmdValue
92:CG_QL_IMPORT_MEMORY_REMAINING:QL_CG_trap_MemoryRemaining
93:CG_QL_IMPORT_R_REGISTERFONT:QL_CG_trap_R_RegisterFont
94:CG_QL_IMPORT_KEY_ISDOWN:QL_CG_trap_Key_IsDown
95:CG_QL_IMPORT_KEY_GETCATCHER:QL_CG_trap_Key_GetCatcher
96:CG_QL_IMPORT_KEY_SETCATCHER:QL_CG_trap_Key_SetCatcher
97:CG_QL_IMPORT_KEY_GETKEY:QL_CG_trap_Key_GetKey
98:CG_QL_IMPORT_KEY_KEYNUMTOSTRINGBUF:QL_CG_trap_Key_KeynumToStringBuf
99:CG_QL_IMPORT_KEY_GETBINDINGBUF:QL_CG_trap_Key_GetBindingBuf
100:CG_QL_IMPORT_NULL_100:<null>
101:CG_QL_IMPORT_CIN_PLAYCINEMATIC:QL_CG_trap_CIN_PlayCinematic
102:CG_QL_IMPORT_CIN_STOPCINEMATIC:QL_CG_trap_CIN_StopCinematic
103:CG_QL_IMPORT_CIN_RUNCINEMATIC:QL_CG_trap_CIN_RunCinematic
104:CG_QL_IMPORT_CIN_DRAWCINEMATIC:QL_CG_trap_CIN_DrawCinematic
105:CG_QL_IMPORT_CIN_SETEXTENTS:QL_CG_trap_CIN_SetExtents
106:CG_QL_IMPORT_GET_ENTITY_TOKEN:QL_CG_trap_GetEntityToken
107:CG_QL_IMPORT_PC_ADD_GLOBAL_DEFINE:QL_CG_trap_PC_AddGlobalDefine
108:CG_QL_IMPORT_PC_LOAD_SOURCE:QL_CG_trap_PC_LoadSource
109:CG_QL_IMPORT_PC_FREE_SOURCE:QL_CG_trap_PC_FreeSource
110:CG_QL_IMPORT_PC_READ_TOKEN:QL_CG_trap_PC_ReadToken
111:CG_QL_IMPORT_PC_SOURCE_FILE_AND_LINE:QL_CG_trap_PC_SourceFileAndLine
112:CG_QL_IMPORT_RETAIL_RESERVED_112:QL_CG_trap_RetailReservedImport
113:CG_QL_IMPORT_RETAIL_RESERVED_113:QL_CG_trap_RetailReservedImport
114:CG_QL_IMPORT_ADVERTISEMENTBRIDGE_UPDATE_VIEW_PARAMETERS:QL_CG_trap_AdvertisementBridge_UpdateViewParameters
115:CG_QL_IMPORT_ADVERTISEMENTBRIDGE_CLEAR_DELAY:QL_CG_trap_AdvertisementBridge_ClearDelay
116:CG_QL_IMPORT_PUBLISH_TAGGED_INFO_STRING:QL_CG_trap_PublishTaggedInfoString
117:CG_QL_IMPORT_RETAIL_RESERVED_117:QL_CG_trap_RetailReservedImport
118:CG_QL_IMPORT_NULL_118:<null>
119:CG_QL_IMPORT_NULL_119:<null>
120:CG_QL_IMPORT_R_MIRROR_POINT:QL_CG_trap_R_MirrorPoint
121:CG_QL_IMPORT_R_MIRROR_VECTOR:QL_CG_trap_R_MirrorVector
122:CG_QL_IMPORT_IS_SUBSCRIBED_APP:QL_CG_trap_IsSubscribedApp
123:CG_QL_IMPORT_DRAW_SCALED_TEXT:QL_CG_trap_DrawScaledText
124:CG_QL_IMPORT_MEASURE_TEXT:QL_CG_trap_MeasureText
125:CG_QL_IMPORT_IS_CLIENT_MUTED:QL_CG_trap_IsClientMuted
126:CG_QL_IMPORT_TOGGLE_CLIENT_MUTE:QL_CG_trap_ToggleClientMute
127:CG_QL_IMPORT_GET_AVATAR_IMAGE_HANDLE:QL_CG_trap_GetAvatarImageHandle
""".strip().splitlines()
)

# The retail DLL's dllEntry export table is distinct from the legacy VM call
# numbers.  Keep the relationship explicit, including the registration-only
# entry point and the confirmed null slot, so dispatch changes cannot silently
# target the adjacent retail function.
RETAIL_CGAME_NATIVE_EXPORT_BINDINGS = tuple(
    line.split(":", 2)
    for line in """
0:CG_INIT:CG_NATIVE_EXPORT_INIT
1:<register-cvars>:CG_NATIVE_EXPORT_REGISTER_CVARS
2:CG_SHUTDOWN:CG_NATIVE_EXPORT_SHUTDOWN
3:CG_CONSOLE_COMMAND:CG_NATIVE_EXPORT_CONSOLE_COMMAND
4:CG_DRAW_ACTIVE_FRAME:CG_NATIVE_EXPORT_DRAW_ACTIVE_FRAME
5:CG_CROSSHAIR_PLAYER:CG_NATIVE_EXPORT_CROSSHAIR_PLAYER
6:CG_LAST_ATTACKER:CG_NATIVE_EXPORT_LAST_ATTACKER
7:CG_KEY_EVENT:CG_NATIVE_EXPORT_KEY_EVENT
8:CG_MOUSE_EVENT:CG_NATIVE_EXPORT_MOUSE_EVENT
9:CG_EVENT_HANDLING:CG_NATIVE_EXPORT_EVENT_HANDLING
10:CG_SHOW_1ST_TRACKED_PLAYER:CG_NATIVE_EXPORT_SHOW_1ST_TRACKED_PLAYER
11:CG_SHOW_2ND_TRACKED_PLAYER:CG_NATIVE_EXPORT_SHOW_2ND_TRACKED_PLAYER
12:CG_CHAT_DOWN:CG_NATIVE_EXPORT_CHAT_DOWN
13:CG_CHAT_UP:CG_NATIVE_EXPORT_CHAT_UP
14:CG_GET_PHYSICS_TIME:CG_NATIVE_EXPORT_GET_PHYSICS_TIME
15:CG_COPY_CLIENT_IDENTITY:CG_NATIVE_EXPORT_COPY_CLIENT_IDENTITY
16:<null>:CG_NATIVE_EXPORT_RESERVED_NULL
17:CG_GET_CHAT_FIELD_Y:CG_NATIVE_EXPORT_GET_CHAT_FIELD_Y
18:CG_GET_CHAT_FIELD_PIXEL_WIDTH:CG_NATIVE_EXPORT_GET_CHAT_FIELD_PIXEL_WIDTH
19:CG_GET_CHAT_FIELD_WIDTH_IN_CHARS:CG_NATIVE_EXPORT_GET_CHAT_FIELD_WIDTH_IN_CHARS
20:CG_SET_CLIENT_SPEAKING_STATE:CG_NATIVE_EXPORT_SET_CLIENT_SPEAKING_STATE
""".strip().splitlines()
)


def read_repo_file(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class CGameNativeBridgeSourceTests(unittest.TestCase):
    def test_cgame_public_exposes_recovered_ql_native_abi(self) -> None:
        cg_public = read_repo_file("code/cgame/cg_public.h")

        self.assertIn("#define\tCGAME_NATIVE_API_VERSION\t8", cg_public)
        self.assertIn("#define CGAME_NATIVE_IMPORT_COUNT\tCG_QL_IMPORT_TOTAL_COUNT", cg_public)
        self.assertIn("CG_QL_IMPORT_R_RENDERSCENE = 76", cg_public)
        self.assertIn("CG_QL_IMPORT_R_SETCOLOR = 78", cg_public)
        self.assertIn("CG_QL_IMPORT_DRAW_SCALED_TEXT = 123", cg_public)
        self.assertIn("CG_QL_IMPORT_CVAR_VARIABLEVALUE = 10", cg_public)
        self.assertIn("CG_NATIVE_EXPORT_REGISTER_CVARS", cg_public)
        self.assertIn("CG_NATIVE_EXPORT_COPY_CLIENT_IDENTITY", cg_public)
        self.assertIn("CG_NATIVE_EXPORT_SET_CLIENT_SPEAKING_STATE", cg_public)
        self.assertIn("CG_NATIVE_EXPORT_SHOW_1ST_TRACKED_PLAYER", cg_public)
        self.assertIn("CG_NATIVE_EXPORT_SHOW_2ND_TRACKED_PLAYER", cg_public)
        self.assertIn("CG_NATIVE_EXPORT_GET_PHYSICS_TIME", cg_public)

    def test_client_cgame_uses_native_first_loader_and_startup_cvar_pass(self) -> None:
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        self.assertIn("static ql_import_f ql_cgame_imports[CGAME_NATIVE_IMPORT_COUNT];", cl_cgame)
        self.assertIn("static vm_t *CL_LoadCGameVM( vmInterpret_t interpret )", cl_cgame)
        self.assertIn("interpret == VMI_PINNED_NATIVE ? VMI_PINNED_NATIVE : VMI_NATIVE", cl_cgame)
        self.assertIn("ql_cgame_imports, CGAME_NATIVE_API_VERSION", cl_cgame)
        self.assertIn("if ( vm->dllHandle || interpret != VMI_BYTECODE || !vm->compiled )", cl_cgame)
        self.assertIn("registrationVm = CL_LoadCGameVM( interpret );", cl_cgame)
        self.assertIn("VM_CallCGameRegisterCvars( registrationVm );", cl_cgame)
        self.assertIn("cgvm = CL_LoadCGameVM( interpret );", cl_cgame)

    def test_every_non_null_cgame_import_slot_has_host_wiring(self) -> None:
        cg_public = read_repo_file("code/cgame/cg_public.h")
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        import_names = {
            match.group(1)
            for match in re.finditer(r"\b(CG_QL_IMPORT_[A-Z0-9_]+)\b(?:\s*=|,)", cg_public)
            if match.group(1) not in {"CG_QL_IMPORT_COUNT", "CG_QL_IMPORT_TOTAL_COUNT"}
        }
        assigned_names = set(re.findall(r"ql_cgame_imports\[(CG_QL_IMPORT_[A-Z0-9_]+)\]", cl_cgame))
        expected_null_names = {
            "CG_QL_IMPORT_NULL_100",
            "CG_QL_IMPORT_NULL_118",
            "CG_QL_IMPORT_NULL_119",
        }

        self.assertEqual(import_names - assigned_names, expected_null_names)

    def test_retail_cgame_import_slots_have_exact_host_adapters(self) -> None:
        cg_public = read_repo_file("code/cgame/cg_public.h")
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        self.assertEqual(len(RETAIL_CGAME_NATIVE_IMPORT_BINDINGS), 128)
        observed_slots: set[int] = set()
        observed_names: set[str] = set()
        for slot_text, import_name, adapter_name in RETAIL_CGAME_NATIVE_IMPORT_BINDINGS:
            slot = int(slot_text)
            self.assertNotIn(slot, observed_slots)
            self.assertNotIn(import_name, observed_names)
            observed_slots.add(slot)
            observed_names.add(import_name)
            self.assertIn(f"{import_name} = {slot}", cg_public)

            assignment = f"ql_cgame_imports[{import_name}] = (ql_import_f){adapter_name};"
            if adapter_name == "<null>":
                self.assertNotIn(f"ql_cgame_imports[{import_name}] =", cl_cgame)
            else:
                self.assertIn(assignment, cl_cgame)

        self.assertEqual(observed_slots, set(range(128)))

    def test_retail_cvar_numeric_lookup_uses_slot_ten(self) -> None:
        cg_public = read_repo_file("code/cgame/cg_public.h")
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        self.assertIn("CG_QL_IMPORT_CVAR_VARIABLEVALUE = 10", cg_public)
        self.assertIn("CG_QL_IMPORT_COMPAT_CVAR_RESET", cg_public)
        self.assertIn(
            "static float QDECL QL_CG_trap_Cvar_VariableValue( const char *varName )",
            cl_cgame,
        )
        self.assertIn("return Cvar_VariableValue( varName );", cl_cgame)
        self.assertIn(
            "ql_cgame_imports[CG_QL_IMPORT_CVAR_VARIABLEVALUE] = (ql_import_f)QL_CG_trap_Cvar_VariableValue;",
            cl_cgame,
        )
        self.assertIn(
            "ql_cgame_imports[CG_QL_IMPORT_COMPAT_CVAR_RESET] = (ql_import_f)QL_CG_trap_Cvar_Reset;",
            cl_cgame,
        )
        self.assertNotIn("CG_QL_IMPORT_CVAR_RESET", cg_public)

    def test_native_cgame_unbound_key_buffer_is_empty_not_fatal(self) -> None:
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        key_binding_case = cl_cgame[cl_cgame.index("case CG_KEY_GETBINDINGBUF:"):]
        self.assertIn("const char *binding;", key_binding_case)
        self.assertIn("binding = Key_GetBinding( args[1] );", key_binding_case)
        self.assertIn('Q_strncpyz( VMA(2), binding ? binding : "", args[3] );', key_binding_case)

    def test_retail_cgame_export_slots_have_exact_legacy_dispatch(self) -> None:
        cg_public = read_repo_file("code/cgame/cg_public.h")
        vm_c = read_repo_file("code/qcommon/vm.c")

        export_enum = re.search(
            r"typedef enum \{(?P<body>.*?)\} cgameNativeExport_t;",
            cg_public,
            re.DOTALL,
        )
        self.assertIsNotNone(export_enum)
        export_names = re.findall(
            r"\b(CG_NATIVE_EXPORT_[A-Z0-9_]+)\b(?:\s*=|,)",
            export_enum.group("body"),
        )

        self.assertEqual(len(RETAIL_CGAME_NATIVE_EXPORT_BINDINGS), 21)
        observed_slots: set[int] = set()
        expected_export_names: list[str] = []
        for slot_text, call_name, export_name in RETAIL_CGAME_NATIVE_EXPORT_BINDINGS:
            slot = int(slot_text)
            self.assertNotIn(slot, observed_slots)
            observed_slots.add(slot)
            expected_export_names.append(export_name)

            if call_name == "<register-cvars>":
                self.assertIn(
                    f"exportFunc = dllExports[{export_name}];",
                    vm_c[vm_c.index("qboolean VM_CallCGameRegisterCvars"):],
                )
            elif call_name == "<null>":
                self.assertIn(
                    f"slot == {export_name}",
                    vm_c[vm_c.index("static qboolean VM_NativeExportSlotIsRequired"):],
                )
            else:
                self.assertIn(
                    f"case {call_name}:\n\t\t\texportFunc = dllExports[{export_name}];",
                    vm_c,
                )

        self.assertEqual(observed_slots, set(range(21)))
        self.assertEqual(export_names, expected_export_names)

    def test_retail_cgame_import_slab_is_validated_before_native_load(self) -> None:
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        self.assertIn("static bool CL_IsExpectedRetailCGameNullImport( int slot )", cl_cgame)
        self.assertIn("static bool CL_ValidateRetailCGameImportTable( void )", cl_cgame)
        self.assertIn("static_assert( CG_QL_IMPORT_COUNT == 128", cl_cgame)
        for null_slot in (
            "CG_QL_IMPORT_NULL_100",
            "CG_QL_IMPORT_NULL_118",
            "CG_QL_IMPORT_NULL_119",
        ):
            self.assertIn(f"case {null_slot}:", cl_cgame)
        self.assertIn("return CL_ValidateRetailCGameImportTable();", cl_cgame)
        self.assertIn("if ( !CL_InitCGameImports() )", cl_cgame)
        self.assertIn("Native cgame import table validation failed.", cl_cgame)

    def test_unclassified_retail_slots_remain_explicit_fallbacks(self) -> None:
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        fallback_slots = (
            "CG_QL_IMPORT_ADVERTISEMENTBRIDGE_RESERVED_59",
            "CG_QL_IMPORT_ADVERTISEMENTBRIDGE_RESERVED_63",
            "CG_QL_IMPORT_ADVERTISEMENTBRIDGE_RESERVED_65",
            "CG_QL_IMPORT_RETAIL_RESERVED_66",
            "CG_QL_IMPORT_RETAIL_RESERVED_67",
            "CG_QL_IMPORT_RETAIL_RESERVED_68",
            "CG_QL_IMPORT_ADVERTISEMENTBRIDGE_RESERVED_69",
            "CG_QL_IMPORT_RETAIL_RESERVED_80",
            "CG_QL_IMPORT_RETAIL_RESERVED_112",
            "CG_QL_IMPORT_RETAIL_RESERVED_113",
            "CG_QL_IMPORT_RETAIL_RESERVED_117",
        )

        self.assertIn(
            "static bool CL_IsExpectedRetailCGameReservedFallbackImport( int slot )",
            cl_cgame,
        )
        for slot in fallback_slots:
            self.assertIn(f"case {slot}:", cl_cgame)
            self.assertIn(
                f"ql_cgame_imports[{slot}] = (ql_import_f)QL_CG_trap_RetailReservedImport;",
                cl_cgame,
            )
        self.assertIn(
            "ql_cgame_imports[slot] != (ql_import_f)QL_CG_trap_RetailReservedImport",
            cl_cgame,
        )
        self.assertIn("retail cgame reserved import slot %i has no fallback", cl_cgame)

    def test_vm_defaults_and_native_pointer_paths_support_retail_cgame(self) -> None:
        qcommon_h = read_repo_file("code/qcommon/qcommon.h")
        vm_c = read_repo_file("code/qcommon/vm.c")

        self.assertIn('Cvar_Get( "vm_cgame", "0"', vm_c)
        self.assertIn("VM_Free( vm );\n\t\treturn NULL;", vm_c)
        self.assertIn("vm->entryPoint || vm->dllExports", vm_c)
        self.assertIn("VM_CreateNative", qcommon_h)
        self.assertIn("VM_CallCGameRegisterCvars", qcommon_h)

    def test_retail_client_message_sideband_is_gated_and_wired(self) -> None:
        qcommon_h = read_repo_file("code/qcommon/qcommon.h")
        client_h = read_repo_file("code/client/client.h")
        cl_input = read_repo_file("code/client/cl_input.cpp")
        cl_main = read_repo_file("code/client/cl_main.cpp")
        sv_client = read_repo_file("code/server/sv_client.cpp")

        self.assertIn(
            "#define\tQL_RETAIL_PROTOCOL_VERSION\tNETCHAN_QL_RETAIL_PROTOCOL_VERSION",
            qcommon_h,
        )
        self.assertIn("void CL_SetRetailClientMessageViewangleDeltaFlag( void );", client_h)
        self.assertIn("void CL_SetRetailClientMessageCGameImportGuardFlag( void );", client_h)
        self.assertIn("void CL_SetRetailClientMessageRendererNodeCount( int nodeCount );", client_h)
        self.assertIn("void CL_CheckCGameNativeImportIntegrity( void );", client_h)
        self.assertIn("RETAIL_CLIENT_MESSAGE_FLAG_CGAME_IMPORT_GUARD", cl_input)
        self.assertIn("clc.netchan.wireProfile == NETCHAN_WIRE_QL_RETAIL", cl_input)
        self.assertIn("MSG_WriteByte( &buf, CL_RetailClientMessageFlags() ^ ( clc.serverCommandSequence & 0xff ) );", cl_input)
        self.assertIn("CL_CheckCGameNativeImportIntegrity();", cl_main)
        self.assertIn("CL_SetRetailClientMessageViewangleDeltaFlag();", cl_main)
        self.assertIn("cl->netchan.wireProfile == NETCHAN_WIRE_QL_RETAIL", sv_client)
        self.assertIn("MSG_ReadByte( msg );", sv_client)

    def test_cgame_usercmd_value_uses_ql_primary_and_fov_fields(self) -> None:
        q_shared = read_repo_file("code/qcommon/q_shared.h")
        client_h = read_repo_file("code/client/client.h")
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")
        cl_input = read_repo_file("code/client/cl_input.cpp")
        msg_c = read_repo_file("code/qcommon/msg.cpp")

        self.assertIn("byte\t\t\tweaponPrimary;", q_shared)
        self.assertIn("byte\t\t\tfov;", q_shared)
        self.assertIn("int\t\t\tcgameUserCmdPrimary;", client_h)
        self.assertIn("int\t\t\tcgameUserCmdFov;", client_h)
        self.assertIn("static void CL_SetUserCmdValue( int userCmdValue, int userCmdPrimary, float sensitivityScale, int userCmdFov )", cl_cgame)
        self.assertIn("cl.cgameUserCmdPrimary = userCmdPrimary;", cl_cgame)
        self.assertIn("cl.cgameUserCmdFov = userCmdFov;", cl_cgame)
        self.assertIn("CL_SetUserCmdValue( args[1], args[2], VMF(3), args[4] );", cl_cgame)
        self.assertIn("CL_SetUserCmdValue( static_cast<int>( args[1] ), static_cast<int>( args[2] ), _vmf( args[3] ), static_cast<int>( args[4] ) );", cl_cgame)
        self.assertIn("cmd->weaponPrimary = cl.cgameUserCmdPrimary;", cl_input)
        self.assertIn("cmd->fov = cl.cgameUserCmdFov;", cl_input)
        self.assertIn("from->weaponPrimary == to->weaponPrimary", msg_c)
        self.assertIn("from->fov == to->fov", msg_c)
        self.assertIn("MSG_WriteDeltaKey( msg, key, from->weaponPrimary, to->weaponPrimary, 8 );", msg_c)
        self.assertIn("MSG_WriteDeltaKey( msg, key, from->fov, to->fov, 8 );", msg_c)
        self.assertIn("to->weaponPrimary = MSG_ReadDeltaKey( msg, key, from->weaponPrimary, 8);", msg_c)
        self.assertIn("to->fov = MSG_ReadDeltaKey( msg, key, from->fov, 8);", msg_c)
        self.assertIn("to->weaponPrimary = from->weaponPrimary;", msg_c)
        self.assertIn("to->fov = from->fov;", msg_c)

    def test_native_getglconfig_uses_retail_ql_layout(self) -> None:
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        self.assertIn("} qlRetailGlconfig_t;", cl_cgame)
        self.assertIn("qlRetailGlconfigSizeCheck[( sizeof( qlRetailGlconfig_t ) == 0x2c44 ) ? 1 : -1 ]", cl_cgame)
        self.assertIn("void CL_CopyRetailGlconfig( void *glconfig )", cl_cgame)
        self.assertIn("retailConfig.maxActiveTextures = cls.glconfig.numTextureUnits;", cl_cgame)
        self.assertIn("retailConfig.multitextureAvailable = ( cls.glconfig.numTextureUnits > 1 ) ? qtrue : qfalse;", cl_cgame)
        self.assertIn("Com_Memcpy( glconfig, &retailConfig, sizeof( retailConfig ) );", cl_cgame)
        self.assertIn("VM_CHECKBOUNDS( cgvm, args[1], sizeof( glconfig_t ) );\n\t\tCL_GetGlconfig( VMA(1) );", cl_cgame)
        self.assertIn("CL_CopyRetailGlconfig( reinterpret_cast<void *>( args[1] ) );", cl_cgame)

    def test_native_getsnapshot_uses_the_retail_entity_window_and_offsets(self) -> None:
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        self.assertIn("constexpr int kRetailPlayerStateBytes = 0x250;", cl_cgame)
        self.assertIn("constexpr int kRetailSnapshotEntityCount = 0x180;", cl_cgame)
        self.assertIn("offsetof( RetailNativeSnapshot, playerState ) == 0x2c", cl_cgame)
        self.assertIn("offsetof( RetailNativeSnapshot, numEntities ) == 0x27c", cl_cgame)
        self.assertIn("offsetof( RetailNativeSnapshot, entities ) == 0x280", cl_cgame)
        self.assertIn("sizeof( RetailNativeSnapshot ) == 0x16488", cl_cgame)
        self.assertIn("static qboolean CL_GetRetailNativeSnapshot", cl_cgame)
        self.assertIn("if ( arg == CG_GETSNAPSHOT )", cl_cgame)
        self.assertIn("return CL_GetRetailNativeSnapshot", cl_cgame)
        self.assertIn("case CG_GETSNAPSHOT:\n\t\treturn CL_GetSnapshot", cl_cgame)

    def test_snapshot_structs_and_deltas_use_ql_native_layout(self) -> None:
        q_shared = read_repo_file("code/qcommon/q_shared.h")
        msg_c = read_repo_file("code/qcommon/msg.cpp")

        self.assertIn("int\t\t\tlocation;\t\t// retail team location index", q_shared)
        self.assertIn("int\t\t\tweaponPrimary;\t// mirrored usercmd byte for retail follow/demo HUD widgets", q_shared)
        self.assertIn("int\t\t\tfov;\t\t\t// mirrored usercmd byte for retail prediction/HUD consumers", q_shared)
        self.assertIn("int\t\t\tjumpTime;", q_shared)
        self.assertIn("int\t\t\tdoubleJumped;", q_shared)
        self.assertIn("int\t\t\tcrouchTime;", q_shared)
        self.assertIn("int\t\t\tcrouchSlideTime;", q_shared)
        self.assertIn("signed char\tforwardmove;\t// mirrored usercmd byte for retail follow/demo HUD widgets", q_shared)
        self.assertIn("TR_QL_ACCEL", q_shared)
        self.assertIn("float\tgravity;\t\t\t// Quake Live trajectory acceleration scalar", q_shared)
        self.assertIn("int\t\thealth;\t\t\t// replicated player health for retail observers/HUD", q_shared)
        self.assertIn("int\t\tarmor;\t\t\t// replicated player armor for retail observers/HUD", q_shared)

        self.assertIn("{ NETF(pos.gravity), 32 }", msg_c)
        self.assertIn("{ NETF(apos.gravity), 32 }", msg_c)
        self.assertIn("{ NETF(jumpTime), 32 }", msg_c)
        self.assertIn("{ NETF(doubleJumped), 1 }", msg_c)
        self.assertIn("{ NETF(health), 16 }", msg_c)
        self.assertIn("{ NETF(armor), 16 }", msg_c)
        self.assertIn("{ NETF(location), 8 }", msg_c)
        self.assertIn("{ PSF(pm_flags), 24 }", msg_c)
        self.assertIn("{ PSF(weaponPrimary), 8 }", msg_c)
        self.assertIn("{ PSF(jumpTime), 32 }", msg_c)
        self.assertIn("{ PSF(crouchSlideTime), 32 }", msg_c)
        self.assertIn("{ PSF(forwardmove), 8 }", msg_c)
        self.assertIn("static qboolean MSG_PlayerStateFieldIsSignedByte( const netField_t *field )", msg_c)
        self.assertIn("MSG_PlayerStateFieldNetworkValue( to, field )", msg_c)
        self.assertIn("MSG_SetPlayerStateFieldValue( to, field, value );", msg_c)

    def test_renderer_reports_node_count_to_retail_sideband(self) -> None:
        tr_public = read_repo_file("code/renderercommon/tr_public.h")
        cl_main = read_repo_file("code/client/cl_main.cpp")
        tr_cmds = read_repo_file("code/renderer/tr_cmds.c")
        vk_cmds = read_repo_file("code/renderervk/tr_cmds.c")

        self.assertIn("SetClientMessageRendererNodeCount", tr_public)
        self.assertIn("rimp.SetClientMessageRendererNodeCount = CL_SetRetailClientMessageRendererNodeCount;", cl_main)
        for source in (tr_cmds, vk_cmds):
            self.assertIn("ri.SetClientMessageRendererNodeCount( tr.pc.c_leafs );", source)

    def test_cgame_native_text_imports_use_shared_scaled_text_bridge(self) -> None:
        client_h = read_repo_file("code/client/client.h")
        cl_scrn = read_repo_file("code/client/cl_scrn.cpp")
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        self.assertIn("void\tRE_DrawScaledText(", client_h)
        self.assertIn("void\tRE_MeasureScaledText(", client_h)
        self.assertIn("void RE_DrawScaledText( int x, int y, const char *text, int fontHandle", cl_scrn)
        self.assertIn("void RE_MeasureScaledText( const char *text, const char *end, int fontHandle", cl_scrn)
        self.assertIn("re.GetScaledFontMetrics( fontHandle, scale", cl_scrn)
        self.assertIn("re.DrawScaledText( x, y, text, fontHandle, scale, limit, outMaxX", cl_scrn)
        self.assertIn("RE_DrawScaledText( x, y, text, fontHandle, scale, limit, maxX", cl_cgame)
        self.assertIn("RE_MeasureScaledText( text, end, fontHandle, scale, limit, bounds );", cl_cgame)
        self.assertIn("fnql::font::CopyMeasureBounds( outLeft, bounds );", cl_cgame)
        self.assertIn("width = bounds[2] - bounds[0];", cl_cgame)
        self.assertIn("height = bounds[4];", cl_cgame)
        self.assertNotIn("QL_CG_MeasureFallbackText", cl_cgame)

    def test_console_chat_field_uses_native_cgame_geometry_exports(self) -> None:
        cl_console = read_repo_file("code/client/cl_console.cpp")

        self.assertIn("static int Con_GetChatFieldY( void )", cl_console)
        self.assertIn("CG_GET_CHAT_FIELD_Y", cl_console)
        self.assertIn("static int Con_GetChatFieldPixelWidth( void )", cl_console)
        self.assertIn("CG_GET_CHAT_FIELD_PIXEL_WIDTH", cl_console)
        self.assertIn("static int Con_GetChatFieldWidthInChars( qboolean teamChat )", cl_console)
        self.assertIn("CG_GET_CHAT_FIELD_WIDTH_IN_CHARS", cl_console)
        self.assertIn("static void Con_ResetChatField( qboolean teamChat )", cl_console)
        self.assertIn("chatField.widthInChars = Con_GetChatFieldWidthInChars( teamChat );", cl_console)
        self.assertIn("chatFieldY = Con_GetChatFieldY();", cl_console)
        self.assertIn("chatFieldPixelWidth = Con_GetChatFieldPixelWidth();", cl_console)

    def test_console_timestamps_use_native_cgame_physics_time(self) -> None:
        cl_console = read_repo_file("code/client/cl_console.cpp")

        self.assertIn('con_timestamps = Cvar_Get( "con_timestamps", "0", CVAR_ARCHIVE_ND | CVAR_PROTECTED );', cl_console)
        self.assertIn("static int Con_GetTimestampTime( void )", cl_console)
        self.assertIn("const int physicsTime = CL_GetCGamePhysicsTime();", cl_console)
        self.assertIn("if ( physicsTime > 0 )", cl_console)
        self.assertIn("static void Con_FormatTimestamp( char *buffer, int bufferSize )", cl_console)
        self.assertIn('Com_sprintf( buffer, bufferSize, "[%d:%02d.%03d] ", minutes, seconds, millis );', cl_console)
        self.assertIn("static void Con_WriteTimestampPrefix( bool skipnotify, int colorIndex )", cl_console)
        self.assertIn("Con_WriteTimestampPrefix( skipnotify, colorIndex );", cl_console)
        self.assertIn("if ( con.newline )", cl_console)

    def test_recovered_cgame_exports_have_client_helpers(self) -> None:
        client_h = read_repo_file("code/client/client.h")
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")
        vm_c = read_repo_file("code/qcommon/vm.c")

        self.assertIn("void CL_ShowFirstTrackedPlayer( void );", client_h)
        self.assertIn("void CL_ShowSecondTrackedPlayer( void );", client_h)
        self.assertIn("int CL_GetCGamePhysicsTime( void );", client_h)
        self.assertIn("void CL_ShowFirstTrackedPlayer( void )", cl_cgame)
        self.assertIn("VM_Call( cgvm, 0, CG_SHOW_1ST_TRACKED_PLAYER );", cl_cgame)
        self.assertIn("void CL_ShowSecondTrackedPlayer( void )", cl_cgame)
        self.assertIn("VM_Call( cgvm, 0, CG_SHOW_2ND_TRACKED_PLAYER );", cl_cgame)
        self.assertIn("int CL_GetCGamePhysicsTime( void )", cl_cgame)
        self.assertIn("VM_Call( cgvm, 0, CG_GET_PHYSICS_TIME )", cl_cgame)
        self.assertIn("return physicsTime > 0 ? physicsTime : 0;", cl_cgame)
        self.assertIn("case CG_SHOW_1ST_TRACKED_PLAYER:", vm_c)
        self.assertIn("dllExports[CG_NATIVE_EXPORT_SHOW_1ST_TRACKED_PLAYER]", vm_c)
        self.assertIn("case CG_SHOW_2ND_TRACKED_PLAYER:", vm_c)
        self.assertIn("dllExports[CG_NATIVE_EXPORT_SHOW_2ND_TRACKED_PLAYER]", vm_c)
        self.assertIn("case CG_GET_PHYSICS_TIME:", vm_c)
        self.assertIn("dllExports[CG_NATIVE_EXPORT_GET_PHYSICS_TIME]", vm_c)

    def test_client_identity_and_speaking_state_exports_are_wired(self) -> None:
        client_h = read_repo_file("code/client/client.h")
        cl_main = read_repo_file("code/client/cl_main.cpp")

        self.assertIn("qboolean CL_CopyClientIdentity( int clientNum, cgameClientIdentity_t *identity );", client_h)
        self.assertIn("qboolean CL_GetClientSteamId( int clientNum, unsigned int *steamIdLow, unsigned int *steamIdHigh );", client_h)
        self.assertIn("qboolean CL_IsSteamIdentityMuted( unsigned int identityLow, unsigned int identityHigh );", client_h)
        self.assertIn("qboolean CL_ToggleSteamIdentityMute( unsigned int identityLow, unsigned int identityHigh );", client_h)
        self.assertIn("qboolean CL_IsVoiceSenderMuted( int clientNum );", client_h)
        self.assertIn("void CL_SetClientSpeakingState( int clientNum, qboolean speaking );", client_h)
        self.assertIn("void CL_SetLocalSpeakingState( qboolean speaking );", client_h)
        self.assertIn("static qboolean CL_ParseSteamIdString( const char *steamId, unsigned int *steamIdLow, unsigned int *steamIdHigh )", cl_main)
        self.assertIn("ULLONG_MAX", cl_main)
        self.assertIn("static const char *CL_GetConfigStringValue( int index )", cl_main)
        self.assertIn("VM_Call( cgvm, 2, CG_COPY_CLIENT_IDENTITY", cl_main)
        self.assertIn("CL_GetConfigStringValue( CS_PLAYERS + clientNum )", cl_main)
        self.assertIn('Info_ValueForKey( info, "steamid" )', cl_main)
        self.assertIn('Info_ValueForKey( info, "steam" )', cl_main)
        self.assertIn('Info_ValueForKey( info, "n" )', cl_main)
        self.assertIn('Info_ValueForKey( info, "cn" )', cl_main)
        self.assertIn("qboolean CL_GetClientSteamId( int clientNum, unsigned int *steamIdLow, unsigned int *steamIdHigh )", cl_main)
        self.assertIn("!identity.identityLow && !identity.identityHigh", cl_main)
        self.assertIn("qboolean CL_IsVoiceSenderMuted( int clientNum )", cl_main)
        self.assertIn("return CL_IsSteamIdentityMuted( steamIdLow, steamIdHigh );", cl_main)
        self.assertIn("!cgvm || !cgvm->dllExports || cls.state != CA_ACTIVE || !cl.snap.valid", cl_main)
        self.assertIn("VM_Call( cgvm, 2, CG_SET_CLIENT_SPEAKING_STATE", cl_main)
        self.assertIn("CL_SetClientSpeakingState( cl.snap.ps.clientNum, speaking );", cl_main)

    def test_cgame_mute_imports_use_shared_client_identity_mute_state(self) -> None:
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        self.assertIn("qboolean CL_IsSteamIdentityMuted( unsigned int identityLow, unsigned int identityHigh )", cl_cgame)
        self.assertIn("qboolean CL_ToggleSteamIdentityMute( unsigned int identityLow, unsigned int identityHigh )", cl_cgame)
        self.assertIn("return ( identity && QL_CG_FindMutedIdentityIndex( identity ) >= 0 ) ? qtrue : qfalse;", cl_cgame)
        self.assertIn("ql_cgame_mutedIdentitySet[ql_cgame_mutedIdentityCount] = 0;", cl_cgame)
        self.assertIn("return CL_IsSteamIdentityMuted( identityLow, identityHigh ) ? 1 : 0;", cl_cgame)
        self.assertIn("return CL_ToggleSteamIdentityMute( identityLow, identityHigh ) ? 1 : 0;", cl_cgame)
        self.assertNotIn("ql_cgame_mutedIdentitySet.fill( 0 );", cl_cgame)
        self.assertNotIn("ql_cgame_mutedIdentityCount = 0;\n\n\tql_cgame_currentColor", cl_cgame)

    def test_cgame_service_imports_share_default_off_client_boundary(self) -> None:
        client_h = read_repo_file("code/client/client.h")
        cl_webui = read_repo_file("code/client/cl_webui.cpp")
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        self.assertIn("qboolean CL_IsSubscribedApp( int appId );", client_h)
        self.assertIn("qhandle_t CL_GetAvatarImageHandle( unsigned int identityLow, unsigned int identityHigh );", client_h)
        self.assertIn("qboolean CL_IsSubscribedApp( int appId )", cl_webui)
        self.assertIn('Com_DPrintf( "UI subscription bridge ignored for app %d: %s (%s [%s])\\n"', cl_webui)
        self.assertIn('"subscription bridge provider unavailable"', cl_webui)
        self.assertIn('"ui_subscriptionBridgePolicy"', cl_webui)
        self.assertIn("qhandle_t CL_GetAvatarImageHandle( unsigned int identityLow, unsigned int identityHigh )", cl_webui)
        self.assertIn("return qfalse;", cl_webui)
        self.assertIn("return 0;", cl_webui)
        self.assertIn("return CL_IsSubscribedApp( appId ) ? 1 : 0;", cl_cgame)
        self.assertIn("return CL_GetAvatarImageHandle( identityLow, identityHigh );", cl_cgame)

    def test_cgame_volume_sound_imports_reach_audio_backends(self) -> None:
        snd_public = read_repo_file("code/client/audio/snd_public.h")
        snd_local = read_repo_file("code/client/audio/snd_local.h")
        snd_main = read_repo_file("code/client/audio/snd_main.cpp")
        snd_dma = read_repo_file("code/client/audio/legacy/snd_dma.cpp")
        openal_backend = read_repo_file("code/client/audio/openal/AudioSystemBackend.inl")
        openal_world = read_repo_file("code/client/audio/openal/AudioSystemWorld.inl")
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        self.assertIn("void S_StartSoundVolume( vec3_t origin, int entnum, int entchannel, sfxHandle_t sfx, float volume );", snd_public)
        self.assertIn("void S_StartLocalSoundVolume( sfxHandle_t sfx, int channelNum, float volume );", snd_public)
        self.assertIn("void (*StartSound)( const vec3_t origin, int entnum, int entchannel, sfxHandle_t sfx, float volume );", snd_local)
        self.assertIn("void (*StartLocalSound)( sfxHandle_t sfx, int channelNum, float volume );", snd_local)
        self.assertIn("S_StartSoundVolume( origin, entnum, entchannel, sfx, 1.0f );", snd_main)
        self.assertIn("S_StartLocalSoundVolume( sfx, channelNum, 1.0f );", snd_main)
        self.assertIn("si.StartSound( origin, entnum, entchannel, sfx, volume );", snd_main)
        self.assertIn("si.StartLocalSound( sfx, channelNum, volume );", snd_main)
        self.assertIn("static int S_VolumeToMasterVolume( float volume )", snd_dma)
        self.assertIn("static void S_Base_StartSound( const vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfxHandle, float volume )", snd_dma)
        self.assertIn("ch->master_vol = masterVolume;", snd_dma)
        self.assertIn("static void S_Base_StartLocalSound( sfxHandle_t sfxHandle, int channelNum, float volume )", snd_dma)
        self.assertIn("void StartSound( const float *origin, int entnum, int entchannel, sfxHandle_t sfxHandle, float volume );", openal_backend)
        self.assertIn("void StartLocalSound( sfxHandle_t sfxHandle, int channelNum, float volume );", openal_backend)
        self.assertIn("world_.StartSound( entnum, entchannel, sfxHandle, sample, origin, volume );", openal_backend)
        self.assertIn("float volumeScale = 1.0f;", openal_world)
        self.assertIn("voice->volumeScale = ClampFloat( volume, 0.0f, 2.0f );", openal_world)
        self.assertIn("gain = ClampFloat( gain * gainScale * voice.volumeScale, 0.0f, 2.0f );", openal_world)
        self.assertIn("S_StartSoundVolume( origin, entityNum, entchannel, sfx, volume );", cl_cgame)
        self.assertIn("S_StartLocalSoundVolume( sfx, channelNum, volume );", cl_cgame)


if __name__ == "__main__":
    unittest.main()
