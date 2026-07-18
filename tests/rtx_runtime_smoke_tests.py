from __future__ import annotations

import importlib.util
import io
import hashlib
import json
import struct
import sys
import tempfile
import unittest
import zlib
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SCRIPT_PATH = ROOT / "scripts" / "rtx_runtime_smoke.py"
SPEC = importlib.util.spec_from_file_location("rtx_runtime_smoke", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
rtx_runtime_smoke = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(rtx_runtime_smoke)


def cvar_value(command: list[str], name: str) -> str:
    for index in range(len(command) - 2):
        if command[index] == "+set" and command[index + 1] == name:
            return command[index + 2]
    raise AssertionError(f"missing command-line cvar {name}")


def png_chunk(chunk_type: bytes, payload: bytes) -> bytes:
    crc = zlib.crc32(payload, zlib.crc32(chunk_type)) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + chunk_type + payload + struct.pack(">I", crc)


def paeth(left: int, above: int, upper_left: int) -> int:
    estimate = left + above - upper_left
    distances = (
        (abs(estimate - left), left),
        (abs(estimate - above), above),
        (abs(estimate - upper_left), upper_left),
    )
    return min(distances, key=lambda item: item[0])[1]


def make_test_png(
    width: int,
    height: int,
    pixel=lambda x, y: ((x * 17 + y * 7) % 220 + 10,) * 3,
    filter_type: int = 0,
    compressed_suffix: bytes = b"",
) -> bytes:
    rows: list[bytes] = []
    previous = bytes(width * 3)
    for y in range(height):
        current = bytes(channel for x in range(width) for channel in pixel(x, y))
        encoded = bytearray(len(current))
        for index, value in enumerate(current):
            left = current[index - 3] if index >= 3 else 0
            above = previous[index]
            upper_left = previous[index - 3] if index >= 3 else 0
            if filter_type == 0:
                predictor = 0
            elif filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = above
            elif filter_type == 3:
                predictor = (left + above) // 2
            elif filter_type == 4:
                predictor = paeth(left, above, upper_left)
            else:
                predictor = 0
            encoded[index] = (value - predictor) & 0xFF
        rows.append(bytes((filter_type,)) + bytes(encoded))
        previous = current
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    compressed = zlib.compress(b"".join(rows)) + compressed_suffix
    return (
        rtx_runtime_smoke.PNG_SIGNATURE
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", compressed)
        + png_chunk(b"IEND", b"")
    )


class RtxRuntimeSmokeTests(unittest.TestCase):
    def make_retail_root(self, root: Path, app_id: str = "282440") -> Path:
        retail = root / "Quake Live"
        retail.mkdir()
        (retail / "steam_appid.txt").write_text(app_id + "\n", encoding="ascii")
        (retail / "quakelive_steam.exe").write_bytes(b"retail-marker")
        (retail / "web.pak").write_bytes(b"retail-marker")
        return retail

    def test_script_does_not_assume_quake_three_fixture_names_or_game_dirs(self) -> None:
        source = SCRIPT_PATH.read_text(encoding="utf-8").lower()
        self.assertNotIn("q3dm", source)
        self.assertNotIn("baseq3", source)
        self.assertNotIn("missionpack", source)
        self.assertNotIn("rmtree", source)
        self.assertNotIn(".unlink(", source)

    def test_retail_install_is_verified_by_steam_identity_and_ql_markers(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            retail = self.make_retail_root(root)
            rtx_runtime_smoke.validate_retail_install(retail)

            (retail / "steam_appid.txt").write_text("220\n", encoding="ascii")
            with self.assertRaisesRegex(ValueError, "Steam identity mismatch"):
                rtx_runtime_smoke.validate_retail_install(retail)

            (retail / "steam_appid.txt").write_bytes(b"2" * 65)
            with self.assertRaisesRegex(ValueError, "unexpectedly large"):
                rtx_runtime_smoke.validate_retail_install(retail)

    def test_retail_install_rejects_incomplete_or_invented_asset_roots(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            retail = self.make_retail_root(root)
            (retail / "web.pak").unlink()
            with self.assertRaisesRegex(ValueError, "not a complete retail"):
                rtx_runtime_smoke.validate_retail_install(retail)

    def test_runtime_requires_an_explicit_safe_retail_map_name(self) -> None:
        parser = rtx_runtime_smoke.create_parser()
        missing = parser.parse_args(["--exe", "fnql.exe", "--plan"])
        with self.assertRaisesRegex(ValueError, "--map is required"):
            rtx_runtime_smoke.validate_options(missing)

        for value in ("../campgrounds", "campgrounds;quit", "camp grounds", "+quit"):
            args = parser.parse_args(
                ["--exe", "fnql.exe", "--map", value, "--plan"]
            )
            with self.assertRaisesRegex(ValueError, "simple retail map name"):
                rtx_runtime_smoke.validate_options(args)

    def test_launch_is_windowed_isolated_offline_and_bounded(self) -> None:
        command, screenshot = rtx_runtime_smoke.build_launch_command(
            Path("fnql.exe"),
            Path("retail"),
            Path("isolated-home"),
            "raster",
            "campgrounds",
            "ffa",
            960,
            540,
            600,
            30,
        )
        self.assertEqual(cvar_value(command, "fs_basepath"), "retail")
        self.assertEqual(cvar_value(command, "fs_steampath"), "retail")
        self.assertEqual(cvar_value(command, "fs_homepath"), "isolated-home")
        self.assertEqual(cvar_value(command, "cl_allowDownload"), "0")
        self.assertEqual(cvar_value(command, "cl_renderer"), "rtx")
        self.assertEqual(cvar_value(command, "r_fullscreen"), "0")
        self.assertEqual(cvar_value(command, "r_mode"), "-1")
        self.assertEqual(command[command.index("+devmap") + 1], "campgrounds")
        self.assertEqual(screenshot, "fnql-rtx-raster-campgrounds")
        self.assertLessEqual(
            sum(argument.startswith("+") for argument in command),
            rtx_runtime_smoke.MAX_STARTUP_COMMANDS,
        )
        self.assertNotIn("+connect", command)
        self.assertNotIn("fs_game", command)

    def test_profiles_keep_optional_features_off_and_native_mode_strict(self) -> None:
        conservative = {
            "r_fogMode": "0",
            "r_globalFog": "0",
            "r_hdr": "0",
            "r_liquid": "0",
            "r_surfaceLightProxies": "0",
            "rtx_rt_async_overlap": "0",
            "rtx_rt_dynamic_blas": "0",
            "rtx_rt_material_heuristics": "0",
        }
        raster = rtx_runtime_smoke.profile_cvars("raster", 960, 540)
        native = rtx_runtime_smoke.profile_cvars("native", 960, 540)
        for cvar, expected in conservative.items():
            self.assertEqual(raster[cvar], expected)
            self.assertEqual(native[cvar], expected)
        self.assertEqual((raster["rtx_rt_mode"], raster["rtx_rt_require"]), ("0", "0"))
        self.assertEqual((native["rtx_rt_mode"], native["rtx_rt_require"]), ("2", "1"))

    def test_all_plan_describes_both_profiles_without_starting_a_process(self) -> None:
        parser = rtx_runtime_smoke.create_parser()
        args = parser.parse_args(
            [
                "--exe",
                "fnql.exe",
                "--retail-root",
                "retail",
                "--map",
                "campgrounds",
                "--profile",
                "all",
                "--plan",
            ]
        )
        rtx_runtime_smoke.validate_options(args)
        plan = rtx_runtime_smoke.planned_manifest(args, Path("retail"))
        encoded = json.dumps(plan)
        self.assertEqual([entry["name"] for entry in plan["profiles"]], ["raster", "native"])
        self.assertIn('"r_fullscreen": "0"', encoded)
        self.assertIn('"rtx_rt_require": "1"', encoded)

    def test_plan_mode_is_read_only(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            retail = self.make_retail_root(root)
            output = root / "must-not-be-created"
            stdout = io.StringIO()
            with redirect_stdout(stdout):
                result = rtx_runtime_smoke.main(
                    [
                        "--exe",
                        str(root / "not-built-fnql.exe"),
                        "--retail-root",
                        str(retail),
                        "--map",
                        "campgrounds",
                        "--output-dir",
                        str(output),
                        "--plan",
                    ]
                )
            self.assertEqual(result, 0)
            self.assertFalse(output.exists())
            self.assertTrue(json.loads(stdout.getvalue())["planned"])

    def test_capability_evidence_accepts_both_renderer_diagnostic_forms(self) -> None:
        compact = "rtx_rt_mode: requested=2 active=2 require=1"
        named = (
            "RTX capability gate: requested=ray_tracing_pipeline (2), "
            "active=ray_tracing_pipeline (2), require=1"
        )
        expected = {"requested": 2, "active": 2, "require": 1}
        self.assertEqual(rtx_runtime_smoke.parse_mode_evidence(compact), expected)
        self.assertEqual(rtx_runtime_smoke.parse_mode_evidence(named), expected)

    def test_evaluation_fails_closed_on_mode_downgrade_or_validation_error(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            capture = Path(temporary_directory) / "capture.png"
            capture.write_bytes(make_test_png(32, 24))
            process = {"status": "exited", "returnCode": 0}
            passed, failures, _ = rtx_runtime_smoke.evaluate_profile(
                "native",
                process,
                "rtx_rt_mode: requested=2 active=0 require=1\nVUID-example",
                [capture],
                32,
                24,
            )
            self.assertFalse(passed)
            self.assertTrue(any("active mode" in failure for failure in failures))
            self.assertTrue(any("validation" in failure for failure in failures))

    def test_evaluation_accepts_clean_raster_fallback_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            capture = Path(temporary_directory) / "capture.png"
            capture.write_bytes(make_test_png(32, 24))
            passed, failures, mode = rtx_runtime_smoke.evaluate_profile(
                "raster",
                {"status": "exited", "returnCode": 0},
                "rtx_rt_mode: requested=0 active=0 require=0",
                [capture],
                32,
                24,
            )
            self.assertTrue(passed)
            self.assertEqual(failures, [])
            self.assertEqual(mode, {"requested": 0, "active": 0, "require": 0})

    def test_png_evidence_decodes_every_standard_filter_and_hashes_capture(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            for filter_type in range(5):
                capture = root / f"filter-{filter_type}.png"
                contents = make_test_png(32, 24, filter_type=filter_type)
                capture.write_bytes(contents)
                metrics = rtx_runtime_smoke.inspect_png(capture, 32, 24)
                self.assertTrue(metrics["valid"], metrics)
                self.assertTrue(metrics["crcValidated"])
                self.assertEqual(metrics["scanlineFilters"], [filter_type])
                self.assertEqual(metrics["fileBytes"], len(contents))
                self.assertEqual(metrics["sha256"], hashlib.sha256(contents).hexdigest())
                self.assertGreater(metrics["luminanceRange"], 8.0)
                self.assertGreater(metrics["luminanceVariance"], 4.0)

    def test_png_evidence_fails_wrong_dimensions_uniform_and_clipped_captures(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            valid = root / "valid.png"
            uniform = root / "uniform.png"
            white_clipped = root / "white.png"
            black_clipped = root / "black.png"
            valid.write_bytes(make_test_png(40, 30))
            uniform.write_bytes(make_test_png(40, 30, pixel=lambda _x, _y: (96, 96, 96)))
            white_clipped.write_bytes(
                make_test_png(
                    40,
                    30,
                    pixel=lambda x, y: (255, 255, 255) if x < 20 else (x * 3, y * 5, 32),
                )
            )
            black_clipped.write_bytes(
                make_test_png(
                    40,
                    30,
                    pixel=lambda x, y: (0, 0, 0) if x < 36 else (128, 64 + y, 32),
                )
            )

            wrong_size = rtx_runtime_smoke.inspect_png(valid, 41, 30)
            flat = rtx_runtime_smoke.inspect_png(uniform, 40, 30)
            white = rtx_runtime_smoke.inspect_png(white_clipped, 40, 30)
            black = rtx_runtime_smoke.inspect_png(black_clipped, 40, 30)
            self.assertFalse(wrong_size["valid"])
            self.assertFalse(wrong_size["dimensionsMatch"])
            self.assertFalse(flat["valid"])
            self.assertFalse(flat["nontrivial"])
            self.assertFalse(white["valid"])
            self.assertGreater(white["nearWhiteFraction"], 0.25)
            self.assertFalse(black["valid"])
            self.assertGreater(black["nearBlackFraction"], 0.85)

    def test_png_evidence_rejects_crc_truncation_bad_filter_and_trailing_zlib(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            crc_bad = bytearray(make_test_png(20, 16))
            idat = crc_bad.index(b"IDAT")
            crc_bad[idat + 4] ^= 0x01
            fixtures = {
                "crc.png": bytes(crc_bad),
                "truncated.png": make_test_png(20, 16)[:-3],
                "filter.png": make_test_png(20, 16, filter_type=5),
                "trailing-zlib.png": make_test_png(
                    20, 16, compressed_suffix=zlib.compress(b"unexpected")
                ),
            }
            errors: dict[str, str] = {}
            for name, contents in fixtures.items():
                path = root / name
                path.write_bytes(contents)
                metrics = rtx_runtime_smoke.inspect_png(path, 20, 16)
                self.assertFalse(metrics["valid"], metrics)
                errors[name] = str(metrics["error"])
            self.assertIn("CRC mismatch", errors["crc.png"])
            self.assertIn("truncated", errors["truncated.png"])
            self.assertIn("unsupported PNG scanline filter", errors["filter.png"])
            self.assertIn("trailing zlib stream", errors["trailing-zlib.png"])

    def test_png_evidence_enforces_file_pixel_and_chunk_bounds_before_decode(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            capture = root / "capture.png"
            capture.write_bytes(make_test_png(20, 16))
            with mock.patch.object(rtx_runtime_smoke, "PNG_MAX_FILE_BYTES", 8):
                metrics = rtx_runtime_smoke.inspect_png(capture, 20, 16)
            self.assertFalse(metrics["valid"])
            self.assertIn("byte smoke-evidence limit", metrics["error"])

            with mock.patch.object(rtx_runtime_smoke, "PNG_MAX_PIXELS", 100):
                metrics = rtx_runtime_smoke.inspect_png(capture, 20, 16)
            self.assertFalse(metrics["valid"])
            self.assertIn("pixel smoke-evidence limit", metrics["error"])

            with mock.patch.object(rtx_runtime_smoke, "PNG_MAX_CHUNKS", 1):
                metrics = rtx_runtime_smoke.inspect_png(capture, 20, 16)
            self.assertFalse(metrics["valid"])
            self.assertIn("chunk limit", metrics["error"])

    def test_evaluation_fails_closed_on_invalid_capture_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            capture = Path(temporary_directory) / "capture.png"
            capture.write_bytes(make_test_png(32, 24, pixel=lambda _x, _y: (255, 255, 255)))
            passed, failures, _ = rtx_runtime_smoke.evaluate_profile(
                "raster",
                {"status": "exited", "returnCode": 0},
                "rtx_rt_mode: requested=0 active=0 require=0",
                [capture],
                32,
                24,
            )
            self.assertFalse(passed)
            self.assertTrue(any("invalid screenshot evidence" in item for item in failures))

    def test_manifest_records_executable_platform_and_capture_identity(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            executable = root / "fnql.exe"
            executable.write_bytes(b"fnql-test-executable")
            parser = rtx_runtime_smoke.create_parser()
            args = parser.parse_args(
                [
                    "--exe", str(executable),
                    "--retail-root", str(root / "retail"),
                    "--map", "campgrounds",
                    "--width", "32",
                    "--height", "24",
                    "--output-dir", str(root / "evidence"),
                ]
            )

            def fake_run(command, _cwd, log_path, _timeout):
                homepath = Path(cvar_value(command, "fs_homepath"))
                screenshot_name = command[command.index("+screenshotPNG") + 1]
                screenshot = homepath / "baseq3-not-assumed" / "screenshots" / f"{screenshot_name}.png"
                screenshot.parent.mkdir(parents=True)
                screenshot.write_bytes(make_test_png(32, 24))
                log_path.write_text(
                    "rtx_rt_mode: requested=0 active=0 require=0\n",
                    encoding="utf-8",
                )
                return {"status": "exited", "returnCode": 0, "cleanup": "natural-exit"}

            with mock.patch.object(rtx_runtime_smoke, "run_process", side_effect=fake_run):
                manifest, manifest_path = rtx_runtime_smoke.run_smoke(args, root / "retail")
            self.assertTrue(manifest["passed"])
            self.assertEqual(
                manifest["executableEvidence"]["sha256"],
                hashlib.sha256(executable.read_bytes()).hexdigest(),
            )
            self.assertIn("machine", manifest["platform"])
            capture = manifest["profiles"][0]["screenshotEvidence"][0]
            self.assertTrue(capture["valid"])
            self.assertGreater(capture["fileBytes"], 0)
            self.assertEqual(len(capture["sha256"]), 64)
            self.assertEqual(json.loads(manifest_path.read_text())["schemaVersion"], 1)

    def test_process_runner_captures_output_and_exits_without_shell(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            log = root / "process.log"
            result = rtx_runtime_smoke.run_process(
                [sys.executable, "-c", "print('rtx-smoke-child')"],
                root,
                log,
                10.0,
            )
            self.assertEqual(result["status"], "exited")
            self.assertEqual(result["returnCode"], 0)
            self.assertIn("rtx-smoke-child", log.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
