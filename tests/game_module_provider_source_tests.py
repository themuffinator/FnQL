from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_repo_file(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(source: str, signature_pattern: str) -> str:
    match = re.search(signature_pattern + r"[^{;]*\{", source)
    if not match:
        raise AssertionError(f"Missing function matching {signature_pattern}")
    depth = 0
    for index in range(match.end() - 1, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.end() : index]
    raise AssertionError(f"Unterminated function matching {signature_pattern}")


class GameModuleProviderSourceTests(unittest.TestCase):
    """FnQL-game companion module hosting (docs/fnql/GAME_MODULES.md)."""

    def test_game_provider_cvar_defaults_to_retail_and_is_protected(self) -> None:
        vm = read_repo_file("code/qcommon/vm.c")

        self.assertRegex(
            vm,
            r'vm_provider\[ VM_GAME \] = Cvar_Get\( "vm_gameProvider", "retail", '
            r"CVAR_ARCHIVE \| CVAR_PROTECTED \);",
        )

    def test_client_provider_cvars_are_protected_and_retail_on_win32_x86(self) -> None:
        vm = read_repo_file("code/qcommon/vm.c")
        qcommon = read_repo_file("code/qcommon/qcommon.h")

        for slot, name in (("VM_CGAME", "vm_cgameProvider"), ("VM_UI", "vm_uiProvider")):
            self.assertRegex(
                vm,
                rf'vm_provider\[ {slot} \] = Cvar_Get\( "{name}", VM_CLIENT_PROVIDER_DEFAULT,\s*'
                r"CVAR_ARCHIVE \| CVAR_PROTECTED \);",
            )
        # retail client modules exist only for Win32 x86, so only there is the
        # default the retail module
        default = re.search(
            r"#if id386 && defined\( _WIN32 \)\s*#define\tVM_CLIENT_PROVIDER_DEFAULT\t\"retail\"\s*"
            r"#else\s*#define\tVM_CLIENT_PROVIDER_DEFAULT\t\"fnql\"\s*#endif",
            qcommon,
        )
        self.assertIsNotNone(default)
        # dedicated servers have no client provider cvars
        init = function_body(vm, r"void VM_Init\( void \)")
        self.assertLess(init.index("#ifndef DEDICATED\n\tvm_provider[ VM_CGAME ]"), init.index("vm_provider[ VM_UI ]"))

    def test_provider_choice_parses_every_slot_and_warns_on_typos(self) -> None:
        vm = read_repo_file("code/qcommon/vm.c")
        policy = read_repo_file("code/qcommon/module_provider_policy.h")
        body = function_body(vm, r"static vmProviderChoice_t VM_RequestedProvider\( vmIndex_t index \)")

        self.assertIn("if ( (unsigned)index >= VM_COUNT || !vm_provider[ index ] ) {", body)
        self.assertIn("VM_ParseProviderChoice( vm_provider[ index ]->string, &recognized )", body)
        self.assertIn("if ( !recognized ) {", body)
        # unknown values keep the historical retail path; auto needs the claim
        self.assertIn("return VM_PROVIDER_CHOICE_RETAIL;\n}", policy)
        self.assertIn("return ( capabilities & FNQL_MODULE_CAP_RETAIL_WIRE ) ? 1 : 0;", policy)

    def test_fnql_provider_tries_prefixed_module_then_falls_back_to_retail(self) -> None:
        vm = read_repo_file("code/qcommon/vm.c")
        body = function_body(vm, r"vm_t \*VM_CreateNative\(")
        provider = function_body(vm, r"static qboolean VM_LoadProviderModule\(")

        fnql_attempt = body.index("if ( VM_LoadProviderModule( vm, choice, interpret ) ) {")
        retail_load = body.index("VM_LoadNativeModule( vm, name,")
        qvm_fallback = body.index('Com_Printf( "Failed to load dll, looking for qvm.\\n" );')

        self.assertLess(fnql_attempt, retail_load)
        self.assertLess(retail_load, qvm_fallback)
        self.assertIn('FNQL_GAME_MODULE_PREFIX "%s", vm->name', provider)
        self.assertIn(
            "VM_LoadNativeModule( vm, fnqlModuleName, VM_NATIVE_SOURCE_SEARCH, 0, choice )", provider
        )
        # Pinned (pure-restart) loads of qagame never consult the provider.
        self.assertIn(
            "( interpret == VMI_NATIVE || index == VM_CGAME || index == VM_UI )", body
        )

    def test_pure_server_client_module_comes_only_from_an_accepted_pak(self) -> None:
        vm = read_repo_file("code/qcommon/vm.c")
        provider = function_body(vm, r"static qboolean VM_LoadProviderModule\(")
        open_native = function_body(vm, r"static void \*VM_OpenNativeModule\(")

        pak = provider.index("if ( !FS_PurePakForFile( fnqlFile, &pakChecksum ) ) {")
        accepted = provider.index("if ( !VM_ChecksumListContains( vmPureClientModulePaks, pakChecksum ) ) {")
        load = provider.index(
            "VM_LoadNativeModule( vm, fnqlModuleName, VM_NATIVE_SOURCE_PURE_PAK, pakChecksum, choice )"
        )
        self.assertLess(pak, accepted)
        self.assertLess(accepted, load)
        # the accepted pak's own bytes, never the OS loader's search
        pure_branch = open_native[open_native.index("if ( source == VM_NATIVE_SOURCE_PURE_PAK ) {"):]
        pure_branch = pure_branch[: pure_branch.index("}")]
        self.assertIn("return VM_LoadDllFromPakCache( index, filename, qtrue, pakChecksum );", pure_branch)
        self.assertNotIn("FS_LoadLibrary", pure_branch)
        # only retail-named modules are pinned
        cache = function_body(vm, r"static void \*VM_LoadDllFromPakCache\(")
        self.assertIn("Q_stricmpn( filename, FNQL_GAME_MODULE_PREFIX", cache)

    def test_client_reports_the_pak_it_ran_and_never_the_retail_one_for_fnql(self) -> None:
        cl_main = read_repo_file("code/client/cl_main.cpp")
        cl_parse = read_repo_file("code/client/cl_parse.cpp")
        send = function_body(cl_main, r"static void CL_SendPureChecksums\( void \)")
        systeminfo = function_body(cl_parse, r"void CL_SystemInfoChanged\( qboolean onlyGame \)")

        fnql = send.index("if ( VM_NativeModulePureChecksum( cgvm, &fnqlCgame, &binChecksum ) ) {")
        refuse = send.index("} else if ( fnqlCgame ) {")
        retail = send.index("} else if ( !FS_FileIsInPAK( QL_NATIVE_CGAME_DLL, &binChecksum, nullptr ) ) {")
        self.assertLess(fnql, refuse)
        self.assertLess(refuse, retail)
        # the accept list comes from this gamestate's systeminfo only
        self.assertIn('Info_ValueForKey( systemInfo, "sv_fnqlClientPaks" )', systeminfo)
        self.assertIn('if ( !Q_stricmp( key.data(), "sv_fnqlClientPaks" ) ) {', systeminfo)
        self.assertIn('VM_SetPureClientModulePaks( "" );', read_repo_file("code/client/cl_main.cpp"))

    def test_server_accepts_only_published_client_packages(self) -> None:
        sv_init = read_repo_file("code/server/sv_init.cpp")
        sv_client = read_repo_file("code/server/sv_client.cpp")
        publish = function_body(sv_init, r"void SV_PublishFnQLClientPaks\( void \)")
        verify = function_body(sv_client, r"static void SV_VerifyPaks_f\( client_t \*cl \)")

        self.assertIn("if ( sv.pure != 0 && sv_fnqlClientModules && sv_fnqlClientModules->integer ) {", publish)
        self.assertIn("FS_PureChecksumsForFile( FNQL_GAME_CGAME_MODULE_FILE,", publish)
        self.assertIn('Cvar_Set( "sv_fnqlClientPaks", list.data() );', publish)
        self.assertIn('Cvar_Get( "sv_fnqlClientPaks", "", CVAR_SYSTEMINFO | CVAR_ROM );', sv_init)
        self.assertIn("!( retailBinKnown && clientBin == nBinChkSum )", verify)
        self.assertIn("!SV_AcceptsFnQLClientPak( clientBin )", verify)

    def test_fnql_game_client_modules_are_never_references(self) -> None:
        files = read_repo_file("code/qcommon/files.c")
        general_ref = function_body(files, r"static qboolean FS_GeneralRef\( const char \*filename \)")

        self.assertIn("!Q_stricmp( filename, FNQL_GAME_CGAME_MODULE_FILE )", general_ref)
        self.assertIn("!Q_stricmp( filename, FNQL_GAME_UI_MODULE_FILE )", general_ref)

    def test_provider_selected_module_must_identify_itself(self) -> None:
        vm = read_repo_file("code/qcommon/vm.c")
        load = function_body(vm, r"static qboolean VM_LoadNativeModule\(")
        identify = function_body(vm, r"static qboolean VM_IdentifyNativeModule\( vm_t \*vm \)")

        self.assertIn("if ( vm->dllExports && !VM_ValidateNativeDllInterface( vm ) ) {", load)
        self.assertIn("if ( !identified || !vm->dllExports ) {", load)
        self.assertIn("Sys_UnloadLibrary( vm->dllHandle );", load)
        self.assertIn("vm->dllApiVersion = engineApiVersion;", load)
        self.assertIn("Sys_LoadFunction( vm->dllHandle, FNQL_MODULE_INFO_SYMBOL )", identify)
        self.assertIn("Q_stricmp( info->product, FNQL_MODULE_PRODUCT )", identify)
        self.assertIn("Q_stricmp( info->module, vmName[ vm->index ] )", identify)
        self.assertIn("info->nativeApi != VM_GetExpectedNativeApiVersion( vm->index )", identify)

    def test_retail_parity_gap_is_reported_for_fnql_game_server_modules(self) -> None:
        vm = read_repo_file("code/qcommon/vm.c")
        load = function_body(vm, r"static qboolean VM_LoadNativeModule\(")

        self.assertIn(
            "vm->index == VM_GAME && !( vm->providerCapabilities & FNQL_MODULE_CAP_RETAIL_WIRE )",
            load,
        )

    def test_module_identity_contract_is_append_only_v1(self) -> None:
        header = read_repo_file("code/qcommon/fnql_module.h")
        struct = re.search(
            r"typedef struct fnqlModuleInfo_s \{(?P<body>.*?)\} fnqlModuleInfo_t;",
            header,
            re.DOTALL,
        )
        self.assertIsNotNone(struct)
        fields = re.findall(r"\*?(\w+);", struct.group("body"))

        self.assertEqual(
            fields,
            ["size", "infoVersion", "product", "module", "version", "build", "capabilities", "nativeApi"],
        )
        self.assertIn('#define FNQL_MODULE_INFO_SYMBOL\t\t"FnQL_GetModuleInfo"', header)
        self.assertIn('#define FNQL_MODULE_PRODUCT\t\t\t"FnQL-game"', header)
        self.assertIn("#define FNQL_MODULE_CAP_RETAIL_WIRE\t\t0x00000001u", header)

    def test_fnql_game_server_module_is_never_a_client_reference(self) -> None:
        qcommon = read_repo_file("code/qcommon/qcommon.h")
        files = read_repo_file("code/qcommon/files.c")
        general_ref = function_body(files, r"static qboolean FS_GeneralRef\( const char \*filename \)")

        self.assertIn(
            '#define\tFNQL_GAME_SERVER_MODULE_FILE\tFNQL_GAME_MODULE_PREFIX "qagame" ARCH_STRING DLL_EXT',
            qcommon,
        )
        self.assertIn("if ( !Q_stricmp( filename, FNQL_GAME_SERVER_MODULE_FILE ) )", general_ref)
        # The retail module keeps its historical reference behavior.
        self.assertNotRegex(general_ref, r'"qagame[^"]*\.(?:dll|so)"')
        self.assertNotIn("QL_NATIVE_", general_ref)

    def test_bot_predict_visible_position_forwards_all_five_arguments(self) -> None:
        sv_game = read_repo_file("code/server/sv_game.cpp")

        self.assertIn(
            "QL_IMPORT5( QL_G_trap_BotPredictVisiblePosition, BOTLIB_AI_PREDICT_VISIBLE_POSITION, "
            "vec3_t, int, void *, int, vec3_t )",
            sv_game,
        )
        self.assertIn(
            "botlib_export->ai.BotPredictVisiblePosition( VMA(1), args[2], VMA(3), args[4], VMA(5) )",
            sv_game,
        )

    def test_vminfo_reports_module_provenance(self) -> None:
        vm = read_repo_file("code/qcommon/vm.c")
        body = function_body(vm, r"static void VM_VmInfo_f\( void \)")

        self.assertIn("vm->moduleFile", body)
        self.assertIn("vm->providerName", body)


if __name__ == "__main__":
    unittest.main()
