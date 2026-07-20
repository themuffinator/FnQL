from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import generate_stock_ql_sidecars
from stock_ql_maps import STOCK_QL_MAPS


MAPS_ROOT = ROOT / "pkg" / "baseq3" / "maps"


def synthetic_bsp(*, native_fog: bool) -> bytes:
    header_size = generate_stock_ql_sidecars.BSP_HEADER_BYTES
    model = struct.pack(
        "<6f4i",
        -1024.0,
        -768.0,
        -256.0,
        1024.0,
        768.0,
        768.0,
        0,
        0,
        0,
        0,
    )
    fog = bytes(generate_stock_ql_sidecars.DFOG_BYTES) if native_fog else b""
    lightmap = bytes((80, 100, 120)) * 64
    lumps = [(0, 0)] * generate_stock_ql_sidecars.BSP_HEADER_LUMPS
    offset = header_size
    lumps[generate_stock_ql_sidecars.LUMP_MODELS] = (offset, len(model))
    offset += len(model)
    lumps[generate_stock_ql_sidecars.LUMP_FOGS] = (offset, len(fog))
    offset += len(fog)
    lumps[generate_stock_ql_sidecars.LUMP_LIGHTMAPS] = (offset, len(lightmap))
    flattened = [value for lump in lumps for value in lump]
    header = (
        generate_stock_ql_sidecars.BSP_IDENT
        + struct.pack("<i", generate_stock_ql_sidecars.BSP_VERSION_QL)
        + struct.pack(f"<{len(flattened)}i", *flattened)
    )
    return header + model + fog + lightmap


class StockQuakeLiveSidecarTests(unittest.TestCase):
    def test_manifest_is_canonical_sorted_and_unique(self) -> None:
        self.assertEqual(len(STOCK_QL_MAPS), 149)
        self.assertEqual(tuple(sorted(STOCK_QL_MAPS)), STOCK_QL_MAPS)
        self.assertEqual(len(set(STOCK_QL_MAPS)), len(STOCK_QL_MAPS))
        for representative in (
            "bloodrun",
            "campgrounds",
            "longestyard",
            "qztraining",
            "siberia",
        ):
            self.assertIn(representative, STOCK_QL_MAPS)
        for optional_workshop_map in ("arenagate", "silentnight", "wintersedge"):
            self.assertNotIn(optional_workshop_map, STOCK_QL_MAPS)

    def test_tracked_audio_and_fog_coverage_exactly_matches_manifest(self) -> None:
        expected = set(STOCK_QL_MAPS)
        audio = {path.stem for path in MAPS_ROOT.glob("*.azb") if path.is_file()}
        fog = {path.stem for path in MAPS_ROOT.glob("*.fog") if path.is_file()}
        self.assertEqual(audio, expected)
        self.assertEqual(fog, expected)

    def test_audio_zone_sidecars_have_bounded_version_three_headers(self) -> None:
        for map_name in STOCK_QL_MAPS:
            with self.subTest(map=map_name):
                data = (MAPS_ROOT / f"{map_name}.azb").read_bytes()
                self.assertGreaterEqual(len(data), 12)
                self.assertLessEqual(len(data), 1024 * 1024)
                magic, version, zone_count = struct.unpack_from("<4sII", data)
                self.assertEqual(magic, b"FQAZ")
                self.assertEqual(version, 3)
                self.assertGreaterEqual(zone_count, 1)
                self.assertLessEqual(zone_count, 512)

    def test_fog_sidecars_are_complete_conservative_and_opt_in_compatible(self) -> None:
        expected_keys = {"color", "mode", "density", "start", "opacity", "sky"}
        for map_name in STOCK_QL_MAPS:
            with self.subTest(map=map_name):
                directives: dict[str, list[str]] = {}
                for raw_line in (MAPS_ROOT / f"{map_name}.fog").read_text(
                    encoding="ascii"
                ).splitlines():
                    line = raw_line.partition("//")[0].strip()
                    if not line:
                        continue
                    key, *values = line.split()
                    self.assertNotIn(key, directives)
                    directives[key] = values

                self.assertEqual(set(directives), expected_keys)
                self.assertEqual(directives["mode"], ["exp"])
                self.assertEqual(directives["sky"], ["1"])
                color = tuple(float(value) for value in directives["color"])
                self.assertEqual(len(color), 3)
                self.assertTrue(all(0.44 <= value <= 0.68 for value in color))
                self.assertLessEqual(max(color) - min(color), 0.08)
                self.assertLessEqual(float(directives["density"][0]), 0.00085)
                self.assertGreater(float(directives["density"][0]), 0.0)
                self.assertIn(float(directives["opacity"][0]), (0.14, 0.24))
                self.assertTrue(96 <= int(directives["start"][0]) <= 384)

    def test_fog_derivation_is_deterministic_and_reduces_native_fog_maps(self) -> None:
        plain = generate_stock_ql_sidecars.fog_preset_from_bsp(
            synthetic_bsp(native_fog=False)
        )
        native = generate_stock_ql_sidecars.fog_preset_from_bsp(
            synthetic_bsp(native_fog=True)
        )
        self.assertEqual(plain.native_fog_count, 0)
        self.assertEqual(native.native_fog_count, 1)
        self.assertEqual(plain.opacity, 0.24)
        self.assertEqual(native.opacity, 0.14)
        self.assertLess(native.density, plain.density)
        self.assertEqual(
            generate_stock_ql_sidecars.fog_sidecar_text("synthetic", plain),
            generate_stock_ql_sidecars.fog_sidecar_text("synthetic", plain),
        )

    def test_user_and_maintainer_docs_describe_the_stock_set(self) -> None:
        audio = (ROOT / "docs" / "AUDIO.md").read_text(encoding="utf-8")
        fog = (ROOT / "docs" / "fnql" / "GLOBAL_FOG.md").read_text(encoding="utf-8")
        technical = (ROOT / "docs" / "fnql" / "TECHNICAL.md").read_text(
            encoding="utf-8"
        )
        self.assertIn("149 BSP maps", audio)
        self.assertIn("149 BSP maps", fog)
        self.assertIn("scripts/stock_ql_maps.py", technical)
        self.assertIn("scripts/generate_stock_ql_sidecars.py", technical)

    def test_sidecar_git_attributes_are_release_host_stable(self) -> None:
        attributes = (ROOT / ".gitattributes").read_text(encoding="utf-8")
        self.assertIn("pkg/baseq3/maps/*.azb binary", attributes)
        self.assertIn("pkg/baseq3/maps/*.fog text eol=lf", attributes)


if __name__ == "__main__":
    unittest.main()
