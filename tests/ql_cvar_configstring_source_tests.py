from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^)]*\)\s*\{{", source)
    if not match:
        raise AssertionError(f"Missing {name}")

    depth = 1
    for index in range(match.end(), len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.end() : index]
    raise AssertionError(f"Unterminated {name}")


class QLCvarConfigstringSourceTests(unittest.TestCase):
    def test_retail_cvar_flags_and_configstring_slots_are_preserved(self) -> None:
        shared = read_source("code/qcommon/q_shared.h")

        expected = {
            "CVAR_QL_PMOVE_SETTINGS": "0x00004000",
            "CVAR_QL_WEAPON_RELOAD": "0x00008000",
            "CVAR_QL_PLAYER_APPEARANCE": "0x00010000",
            "CVAR_QL_ARMOR_TIERED": "0x00020000",
            "CS_QL_PMOVE_SETTINGS": "0x2A9",
            "CS_QL_ARMOR_TIERED": "0x2AA",
            "CS_QL_WEAPON_RELOAD": "0x2AB",
            "CS_QL_PLAYER_APPEARANCE": "0x2AC",
        }
        for name, value in expected.items():
            self.assertRegex(shared, rf"#define\s+{name}\s+{value}\b")

    def test_server_publishes_each_flag_group_as_an_info_string(self) -> None:
        init = read_source("code/server/sv_init.cpp")
        update = function_body(init, "SV_UpdateQLCvarConfigstrings")

        expected_channels = (
            ("CVAR_QL_PMOVE_SETTINGS", "CS_QL_PMOVE_SETTINGS"),
            ("CVAR_QL_ARMOR_TIERED", "CS_QL_ARMOR_TIERED"),
            ("CVAR_QL_WEAPON_RELOAD", "CS_QL_WEAPON_RELOAD"),
            ("CVAR_QL_PLAYER_APPEARANCE", "CS_QL_PLAYER_APPEARANCE"),
        )
        for cvar_flag, configstring in expected_channels:
            self.assertIn(f"{{ {cvar_flag}, {configstring},", init)

        self.assertIn("Cvar_InfoString( channel.cvarFlag, &truncated )", update)
        self.assertIn("SV_SetConfigstring( channel.configstring, info );", update)
        self.assertIn("cvar_modifiedFlags &= ~channel.cvarFlag;", update)

    def test_spawn_and_frame_paths_publish_the_retail_channels(self) -> None:
        init = read_source("code/server/sv_init.cpp")
        main = read_source("code/server/sv_main.cpp")
        spawn = function_body(init, "SV_SpawnServer")
        frame = function_body(main, "SV_Frame")

        self.assertIn("SV_UpdateQLCvarConfigstrings( qtrue );", spawn)
        self.assertIn("SV_UpdateQLCvarConfigstrings( qfalse );", frame)
        self.assertNotIn("0x2BE", init)
        self.assertNotIn("0x2BE", main)


if __name__ == "__main__":
    unittest.main()
