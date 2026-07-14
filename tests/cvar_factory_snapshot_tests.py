from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_repo_file(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing function signature: {signature}")

    opening_brace = source.find("{", start + len(signature))
    if opening_brace < 0:
        raise AssertionError(f"Missing function body: {signature}")

    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening_brace + 1 : index]

    raise AssertionError(f"Unterminated function body: {signature}")


class CvarFactorySnapshotSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.shared = read_repo_file("code/qcommon/q_shared.h")
        cls.public = read_repo_file("code/qcommon/qcommon.h")
        cls.source = read_repo_file("code/qcommon/cvar.c")

    def test_snapshot_storage_is_internal_and_preserves_public_field_offsets(self) -> None:
        cvar_match = re.search(
            r"struct cvar_s\s*\{(?P<body>.*?)\n\};",
            self.shared,
            re.DOTALL,
        )
        self.assertIsNotNone(cvar_match)
        cvar_body = cvar_match.group("body")
        self.assertRegex(
            cvar_body,
            r"cvarGroup_t\s+group\s*;.*\n(?:.*\n){0,3}\s*char\s*\*factoryString\s*;",
        )
        self.assertGreater(
            cvar_body.index("factoryString"), cvar_body.index("group")
        )

        vm_match = re.search(
            r"typedef struct\s*\{(?P<body>[^{}]*)\}\s*vmCvar_t\s*;",
            self.shared,
            re.DOTALL,
        )
        self.assertIsNotNone(vm_match)
        self.assertNotIn("factoryString", vm_match.group("body"))

    def test_engine_api_exposes_explicit_save_and_restore_operations(self) -> None:
        self.assertIn(
            "qboolean Cvar_SaveFactoryValue( const char *var_name );", self.public
        )
        self.assertIn(
            "qboolean Cvar_RestoreFactoryValue( const char *var_name );",
            self.public,
        )

    def test_save_replaces_the_snapshot_with_the_current_value(self) -> None:
        body = function_body(
            self.source,
            "qboolean Cvar_SaveFactoryValue( const char *var_name )",
        )
        self.assertIn("var = Cvar_FindVar( var_name );", body)
        self.assertRegex(
            body,
            re.compile(r"if\s*\(\s*!var\s*\).*?return qfalse;", re.DOTALL),
        )
        self.assertIn("Z_Free( var->factoryString );", body)
        self.assertIn("var->factoryString = CopyString( var->string );", body)
        self.assertLess(
            body.index("Z_Free( var->factoryString );"),
            body.index("var->factoryString = CopyString( var->string );"),
        )
        self.assertIn("return qtrue;", body)

    def test_restore_force_sets_once_and_consumes_the_snapshot(self) -> None:
        body = function_body(
            self.source,
            "qboolean Cvar_RestoreFactoryValue( const char *var_name )",
        )
        self.assertIn("var = Cvar_FindVar( var_name );", body)
        self.assertRegex(
            body,
            re.compile(
                r"if\s*\(\s*!var\s*\|\|\s*!var->factoryString\s*\).*?return qfalse;",
                re.DOTALL,
            ),
        )
        self.assertIn("factoryString = var->factoryString;", body)
        self.assertIn("Cvar_Set2( var_name, factoryString, qtrue );", body)
        self.assertIn("Z_Free( factoryString );", body)
        self.assertIn("var->factoryString = NULL;", body)
        self.assertLess(
            body.index("Cvar_Set2( var_name, factoryString, qtrue );"),
            body.index("Z_Free( factoryString );"),
        )
        self.assertLess(
            body.index("Z_Free( factoryString );"),
            body.index("var->factoryString = NULL;"),
        )
        self.assertIn("return qtrue;", body)

    def test_reset_of_an_unchanged_readonly_cvar_is_a_noop_before_protection(self) -> None:
        body = function_body(
            self.source,
            "cvar_t *Cvar_Set2( const char *var_name, const char *value, qboolean force )",
        )
        no_op_reset = "if ( !value && strcmp( var->resetString, var->string ) == 0 )"
        protected_write = "if ( var->flags & (CVAR_ROM | CVAR_INIT | CVAR_CHEAT | CVAR_DEVELOPER) && !force )"
        self.assertIn(no_op_reset, body)
        self.assertIn(protected_write, body)
        self.assertLess(body.index(no_op_reset), body.index(protected_write))

    def test_allocation_and_unset_paths_zero_or_free_snapshot_storage(self) -> None:
        self.assertIn("static cvar_t\tcvar_indexes[MAX_CVARS];", self.source)
        init_body = function_body(self.source, "void Cvar_Init (void)")
        self.assertIn(
            "Com_Memset(cvar_indexes, '\\0', sizeof(cvar_indexes));", init_body
        )

        unset_body = function_body(self.source, "static cvar_t *Cvar_Unset( cvar_t *cv )")
        self.assertIn("Z_Free( cv->factoryString );", unset_body)
        self.assertIn("Com_Memset( cv, '\\0', sizeof( *cv ) );", unset_body)
        self.assertLess(
            unset_body.index("Z_Free( cv->factoryString );"),
            unset_body.index("Com_Memset( cv, '\\0', sizeof( *cv ) );"),
        )


if __name__ == "__main__":
    unittest.main()
