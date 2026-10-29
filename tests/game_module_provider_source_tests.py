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
            r'vm_gameProvider = Cvar_Get\( "vm_gameProvider", "retail", '
            r"CVAR_ARCHIVE \| CVAR_PROTECTED \);",
        )

    def test_only_qagame_has_a_provider_choice(self) -> None:
        vm = read_repo_file("code/qcommon/vm.c")
        body = function_body(vm, r"static vmProvider_t VM_RequestedProvider\( vmIndex_t index \)")

        self.assertIn("if ( index != VM_GAME || !vm_gameProvider ) {", body)
        self.assertIn('if ( !Q_stricmp( value, "fnql" ) ) {', body)
        self.assertIn("return VM_PROVIDER_FNQL;", body)
        # Anything else, including typos, keeps the historical retail path.
        self.assertTrue(body.rstrip().endswith("return VM_PROVIDER_RETAIL;"))

    def test_fnql_provider_tries_prefixed_module_then_falls_back_to_retail(self) -> None:
        vm = read_repo_file("code/qcommon/vm.c")
        body = function_body(vm, r"vm_t \*VM_CreateNative\(")

        fnql_attempt = body.index("VM_RequestedProvider( index ) == VM_PROVIDER_FNQL")
        prefixed = body.index('FNQL_GAME_MODULE_PREFIX "%s", name')
        fnql_load = body.index("VM_LoadNativeModule( vm, fnqlModuleName, qfalse, qtrue )")
        retail_load = body.index(
            "VM_LoadNativeModule( vm, name, interpret == VMI_PINNED_NATIVE ? qtrue : qfalse, qfalse )"
        )
        qvm_fallback = body.index('Com_Printf( "Failed to load dll, looking for qvm.\\n" );')

        self.assertLess(fnql_attempt, prefixed)
        self.assertLess(prefixed, fnql_load)
        self.assertLess(fnql_load, retail_load)
        self.assertLess(retail_load, qvm_fallback)
        # Pinned (pure-restart) loads never consult the provider.
        self.assertIn("interpret == VMI_NATIVE && VM_RequestedProvider( index )", body)

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
