from __future__ import annotations

import importlib.util
import contextlib
import io
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT_PATH = ROOT / "scripts" / "dlight_shadow_test.py"

spec = importlib.util.spec_from_file_location("dlight_shadow_test", SCRIPT_PATH)
assert spec is not None
dlight_shadow_test = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(dlight_shadow_test)


class DlightShadowTestTests(unittest.TestCase):
    def assert_parse_args_exits(self, argv: list[str]) -> None:
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                dlight_shadow_test.parse_args(argv)

    def test_extra_set_rejects_cfg_injection(self) -> None:
        with self.assertRaisesRegex(ValueError, "not safe"):
            dlight_shadow_test.parse_extra_sets(["r_safe;quit=1"])
        with self.assertRaisesRegex(ValueError, "unsafe control"):
            dlight_shadow_test.parse_extra_sets(["r_safe=1\nquit"])

    def test_parse_args_rejects_unsafe_map_and_fs_game(self) -> None:
        self.assert_parse_args_exits(["--dry-run", "--map", "q3dm1\nquit"])
        self.assert_parse_args_exits(["--dry-run", "--fs-game", "../baseq3"])

    def test_parse_args_rejects_invalid_numeric_runtime_values(self) -> None:
        invalid_options = (
            ("--timeout", "0"),
            ("--intensity", "-1"),
            ("--distance", "nan"),
            ("--seconds", "-1"),
        )
        for option, value in invalid_options:
            with self.subTest(option=option, value=value):
                self.assert_parse_args_exits(["--dry-run", option, value])


if __name__ == "__main__":
    unittest.main()
