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


class QAGameNativeBridgeSourceTests(unittest.TestCase):
    def test_public_header_keeps_recovered_native_abi_shape(self) -> None:
        g_public = read_repo_file("code/game/g_public.h")

        self.assertIn("#define\tGAME_NATIVE_API_VERSION\t10", g_public)
        self.assertIn("G_QL_IMPORT_CVAR_VARIABLE_VALUE = 11", g_public)
        self.assertNotIn("G_QL_IMPORT_CVAR_VARIABLE_INTEGER_VALUE = 11", g_public)
        self.assertIn("G_QL_IMPORT_SUBMIT_MATCH_REPORT = 185", g_public)
        self.assertIn("G_QL_IMPORT_FACTORY_EXISTS = 206", g_public)
        self.assertIn("G_QL_IMPORT_COUNT = 207", g_public)
        self.assertIn("GAME_NATIVE_EXPORT_REGISTER_CVARS", g_public)
        self.assertIn("GAME_NATIVE_EXPORT_GET_CLIENT_SCORE", g_public)

    def test_direct_import_slab_binds_each_recovered_non_gap_slot(self) -> None:
        g_public = read_repo_file("code/game/g_public.h")
        sv_game = read_repo_file("code/server/sv_game.cpp")
        native_body = enum_body(g_public, "gameNativeImport_t")

        declared = {
            match.group(1)
            for match in re.finditer(r"\b(G_QL_IMPORT_[A-Z0-9_]+)\b\s*(?:=|,)", native_body)
            if match.group(1)
            not in {
                "G_QL_IMPORT_COUNT",
                "G_QL_IMPORT_COMPAT_BASE",
                "G_QL_IMPORT_TOTAL_COUNT",
            }
        }
        bound = set(re.findall(r"QL_BIND\(\s*(G_QL_IMPORT_[A-Z0-9_]+)\s*,", sv_game))

        self.assertSetEqual(declared, bound)

    def test_direct_float_cvar_query_does_not_use_legacy_integer_syscall(self) -> None:
        sv_game = read_repo_file("code/server/sv_game.cpp")
        wrapper = sv_game[
            sv_game.index("static float QDECL QL_G_trap_Cvar_VariableValue") : sv_game.index(
                "QL_IMPORT1( QL_G_trap_Cvar_VariableIntegerValue"
            )
        ]

        self.assertIn("return Cvar_VariableValue( varName );", wrapper)
        self.assertNotIn("G_CVAR_VARIABLE_INTEGER_VALUE", wrapper)
        self.assertIn(
            "QL_BIND( G_QL_IMPORT_CVAR_VARIABLE_VALUE, QL_G_trap_Cvar_VariableValue );",
            sv_game,
        )
        self.assertIn(
            "QL_BIND_COMPAT( G_CVAR_VARIABLE_INTEGER_VALUE, QL_G_trap_Cvar_VariableIntegerValue );",
            sv_game,
        )

    def test_native_game_loader_and_export_dispatch_cover_retail_lifecycle(self) -> None:
        g_public = read_repo_file("code/game/g_public.h")
        sv_game = read_repo_file("code/server/sv_game.cpp")
        vm_c = read_repo_file("code/qcommon/vm.c")
        game_dispatch_start = vm_c.index("if ( vm->index == VM_GAME )")
        game_dispatch = vm_c[game_dispatch_start : vm_c.index("#ifndef USE_DEDICATED", game_dispatch_start)]

        self.assertIn("VM_CreateNative( VM_GAME, SV_GameSystemCalls, SV_DllSyscall, interpret,", sv_game)
        self.assertIn("ql_game_imports, interpret == VMI_NATIVE ? GAME_NATIVE_API_VERSION : GAME_API_VERSION", sv_game)
        self.assertIn("static qboolean SV_CallGameRegisterCvarsOnce( vm_t *vm )", sv_game)
        self.assertIn("if ( VM_CallGameRegisterCvars( vm ) )", sv_game)
        self.assertIn("SV_CallGameRegisterCvarsOnce( gvm );", sv_game)
        expected_dispatch = (
            ("GAME_INIT", "GAME_NATIVE_EXPORT_INIT"),
            ("GAME_SHUTDOWN", "GAME_NATIVE_EXPORT_SHUTDOWN"),
            ("GAME_CLIENT_CONNECT", "GAME_NATIVE_EXPORT_CLIENT_CONNECT"),
            ("GAME_CLIENT_BEGIN", "GAME_NATIVE_EXPORT_CLIENT_BEGIN"),
            ("GAME_CLIENT_USERINFO_CHANGED", "GAME_NATIVE_EXPORT_CLIENT_USERINFO_CHANGED"),
            ("GAME_CLIENT_DISCONNECT", "GAME_NATIVE_EXPORT_CLIENT_DISCONNECT"),
            ("GAME_CLIENT_COMMAND", "GAME_NATIVE_EXPORT_CLIENT_COMMAND"),
            ("GAME_CLIENT_THINK", "GAME_NATIVE_EXPORT_CLIENT_THINK"),
            ("GAME_RUN_FRAME", "GAME_NATIVE_EXPORT_RUN_FRAME"),
            ("GAME_CONSOLE_COMMAND", "GAME_NATIVE_EXPORT_CONSOLE_COMMAND"),
            ("BOTAI_START_FRAME", "GAME_NATIVE_EXPORT_BOTAI_START_FRAME"),
            ("GAME_CAN_CLIENT_SEE_CLIENT", "GAME_NATIVE_EXPORT_CAN_CLIENT_SEE_CLIENT"),
            (
                "GAME_FREEZE_CAN_SEE_THAW_PROGRESS_EVENT",
                "GAME_NATIVE_EXPORT_FREEZE_CAN_SEE_THAW_PROGRESS_EVENT",
            ),
            ("GAME_IS_OBJECTIVE_ENTITY", "GAME_NATIVE_EXPORT_IS_OBJECTIVE_ENTITY"),
            (
                "GAME_SHOULD_SUPPRESS_VOICE_TO_CLIENT",
                "GAME_NATIVE_EXPORT_SHOULD_SUPPRESS_VOICE_TO_CLIENT",
            ),
            ("GAME_IS_CLIENT_ADMIN", "GAME_NATIVE_EXPORT_IS_CLIENT_ADMIN"),
            ("GAME_ARE_ENEMY_CLIENTS", "GAME_NATIVE_EXPORT_ARE_ENEMY_CLIENTS"),
            ("GAME_GET_CLIENT_SCORE", "GAME_NATIVE_EXPORT_GET_CLIENT_SCORE"),
        )
        for legacy_call, native_slot in expected_dispatch:
            self.assertIn(legacy_call, enum_body(g_public, "gameExport_t"))
            self.assertIn(native_slot, enum_body(g_public, "gameNativeExport_t"))
            self.assertIn(f"case {legacy_call}:", game_dispatch)
            self.assertIn(f"dllExports[{native_slot}]", game_dispatch)


if __name__ == "__main__":
    unittest.main()
