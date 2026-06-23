from __future__ import annotations

import importlib.util
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SWEEP_PATH = ROOT / "scripts" / "audio_zone_sweep.py"

spec = importlib.util.spec_from_file_location("audio_zone_sweep", SWEEP_PATH)
assert spec is not None
audio_zone_sweep = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = audio_zone_sweep
assert spec.loader is not None
spec.loader.exec_module(audio_zone_sweep)


class AudioZoneSweepPlanningTests(unittest.TestCase):
    def test_dry_run_preserves_map_relative_outputs_and_overrides(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            baseq3 = root / "baseq3"
            maps = baseq3 / "maps"
            maps.mkdir(parents=True)
            bsp = maps / "q3dm1.bsp"
            bsp.write_bytes(b"IBSP")
            override = maps / "q3dm1.audiozones"
            override.write_text("audiozones 1\n", encoding="utf-8")
            material_map = root / "audio-materials.txt"
            material_map.write_text("textures/custom/* metal\n", encoding="utf-8")
            output_root = root / "generated"

            options = audio_zone_sweep.SweepOptions(
                tool=Path("fnql-audiozonesc"),
                inputs=(maps,),
                output_root=output_root,
                relative_root=baseq3,
                override_root=baseq3,
                material_map=material_map,
                report_json=None,
                report_csv=None,
                dry_run=True,
                audit=True,
                strict=True,
                samples=512,
                max_zones=64,
            )

            manifest = audio_zone_sweep.run_sweep(options)

            self.assertEqual(manifest["mapCount"], 1)
            run = manifest["runs"][0]
            self.assertEqual(run["status"], "planned")
            self.assertEqual(Path(run["output"]), output_root.resolve() / "maps" / "q3dm1.azb")
            self.assertEqual(Path(run["override"]), override.resolve())
            self.assertIn("--merge", run["generateCommand"])
            self.assertIn("--material-map", run["generateCommand"])
            self.assertIn(str(material_map.resolve()), run["generateCommand"])
            self.assertIn("--max-zones", run["generateCommand"])
            self.assertIn("--strict", run["auditCommand"])

    def test_dry_run_report_writers_emit_json_and_csv(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            maps = root / "maps"
            maps.mkdir()
            (maps / "arena.bsp").write_bytes(b"IBSP")
            report_json = root / "report.json"
            report_csv = root / "report.csv"

            with contextlib.redirect_stdout(io.StringIO()):
                result = audio_zone_sweep.main(
                    [
                        "--dry-run",
                        "--tool",
                        "fnql-audiozonesc",
                        "--output-root",
                        str(root / "out"),
                        "--report-json",
                        str(report_json),
                        "--report-csv",
                        str(report_csv),
                        str(maps),
                    ]
                )

            self.assertEqual(result, 0)
            self.assertTrue(report_json.exists())
            self.assertTrue(report_csv.exists())
            manifest = json.loads(report_json.read_text(encoding="utf-8"))
            self.assertEqual(manifest["summary"]["planned"], 1)
            self.assertIn("arena.bsp", report_csv.read_text(encoding="utf-8"))

    def test_relative_root_must_contain_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            relative_root = root / "baseq3"
            relative_root.mkdir()
            outside = root / "outside"
            outside.mkdir()
            (outside / "q3dm1.bsp").write_bytes(b"IBSP")

            options = audio_zone_sweep.SweepOptions(
                tool=Path("fnql-audiozonesc"),
                inputs=(outside,),
                output_root=root / "generated",
                relative_root=relative_root,
                override_root=None,
                material_map=None,
                report_json=None,
                report_csv=None,
                dry_run=True,
                audit=False,
                strict=False,
                samples=512,
                max_zones=None,
            )

            with self.assertRaisesRegex(ValueError, "not under --relative-root"):
                audio_zone_sweep.run_sweep(options)

    def test_rejects_duplicate_output_paths_without_relative_root(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            first = root / "first"
            second = root / "second"
            first.mkdir()
            second.mkdir()
            (first / "arena.bsp").write_bytes(b"IBSP")
            (second / "arena.bsp").write_bytes(b"IBSP")

            options = audio_zone_sweep.SweepOptions(
                tool=Path("fnql-audiozonesc"),
                inputs=(first, second),
                output_root=root / "generated",
                relative_root=None,
                override_root=None,
                material_map=None,
                report_json=None,
                report_csv=None,
                dry_run=True,
                audit=False,
                strict=False,
                samples=512,
                max_zones=None,
            )

            with self.assertRaisesRegex(ValueError, "same output"):
                audio_zone_sweep.run_sweep(options)

    def test_missing_auxiliary_roots_are_reported_before_planning(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            maps = root / "maps"
            maps.mkdir()
            (maps / "arena.bsp").write_bytes(b"IBSP")

            options = audio_zone_sweep.SweepOptions(
                tool=Path("fnql-audiozonesc"),
                inputs=(maps,),
                output_root=root / "generated",
                relative_root=None,
                override_root=root / "missing-overrides",
                material_map=root / "missing-materials.txt",
                report_json=None,
                report_csv=None,
                dry_run=True,
                audit=False,
                strict=False,
                samples=512,
                max_zones=None,
            )

            with self.assertRaisesRegex(FileNotFoundError, "--override-root is not a directory"):
                audio_zone_sweep.run_sweep(options)

            options = audio_zone_sweep.SweepOptions(
                tool=Path("fnql-audiozonesc"),
                inputs=(maps,),
                output_root=root / "generated",
                relative_root=None,
                override_root=None,
                material_map=root / "missing-materials.txt",
                report_json=None,
                report_csv=None,
                dry_run=True,
                audit=False,
                strict=False,
                samples=512,
                max_zones=None,
            )

            with self.assertRaisesRegex(FileNotFoundError, "--material-map is not a file"):
                audio_zone_sweep.run_sweep(options)


class AudioZoneSweepAuditParseTests(unittest.TestCase):
    def test_parse_audit_output_extracts_counts_profile_and_warnings(self) -> None:
        stdout = "\n".join(
            [
                "audio-zone audit maps/q3dm1.azb",
                "version 2, bytes 444, zones 3",
                "bounds 0 0 0 -> 128 128 128, summed volume 2097152.000",
                "presets: hall=1 hallway=2",
                "materials: stone=2 metal=1",
                "flags generated=3 outdoor=1 underwater=0",
                "portals total=4 maxPerZone=2 openness min/avg/max=0.250/0.625/1.000 self=0 oneWay=0",
                "portal tuning distance min/avg/max=96.000/128.000/192.000 maxBlend min/avg/max=0.250/0.350/0.450",
                "portal curves: smooth=2 ease-out=2",
                "overlaps total=1 equalPriority=0",
                "lookup profile samples=512 hits=481 portalBlends=24 avgBlend=0.421 elapsedMs=0.125 nsPerSample=244.140 checksum=12345",
                "confidence material=1.000 portal=1.000 lookup=0.939 overlap=1.000 overall=0.985 anomaly=0.011 grade=excellent",
                "warning: equal-priority overlapping zones rely on smaller-volume tie-breaks",
            ]
        )

        audit = audio_zone_sweep.parse_audit_output(stdout)

        self.assertEqual(audit["version"], 2)
        self.assertEqual(audit["zones"], 3)
        self.assertEqual(audit["presets"], {"hall": 1, "hallway": 2})
        self.assertEqual(audit["materials"], {"stone": 2, "metal": 1})
        self.assertEqual(audit["flags"]["generated"], 3)
        self.assertEqual(audit["portals"]["total"], 4)
        self.assertNotIn("max", audit["portals"])
        self.assertAlmostEqual(audit["portals"]["opennessAvg"], 0.625)
        self.assertAlmostEqual(audit["portalTuning"]["distanceAvg"], 128.0)
        self.assertEqual(audit["portalCurves"], {"smooth": 2, "ease-out": 2})
        self.assertEqual(audit["lookup"]["hits"], 481)
        self.assertAlmostEqual(audit["confidence"]["overall"], 0.985)
        self.assertEqual(audit["confidence"]["grade"], "excellent")
        self.assertEqual(len(audit["warnings"]), 1)

    def test_parse_warnings_none_stays_empty(self) -> None:
        audit = audio_zone_sweep.parse_audit_output(
            "\n".join(
                [
                    "version 2, bytes 100, zones 1",
                    "presets: room=1",
                    "materials: neutral=1",
                    "warnings none",
                ]
            )
        )

        self.assertEqual(audit["warnings"], [])

    def test_parse_audit_output_keeps_non_finite_values_out_of_json_numbers(self) -> None:
        audit = audio_zone_sweep.parse_audit_output(
            "\n".join(
                [
                    "lookup profile samples=512 hits=1 nsPerSample=1.0e309",
                    "confidence overall=nan anomaly=1.0e309 grade=unstable",
                ]
            )
        )

        self.assertEqual(audit["lookup"]["nsPerSample"], "1.0e309")
        self.assertEqual(audit["confidence"]["overall"], "nan")
        with tempfile.TemporaryDirectory() as tmp:
            report = Path(tmp) / "report.json"
            audio_zone_sweep.write_manifest_json(report, {"runs": [{"audit": audit}]})
            text = report.read_text(encoding="utf-8")

        self.assertNotIn("Infinity", text)
        self.assertNotIn("NaN", text)

    def test_csv_writer_escapes_spreadsheet_formula_cells(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            report = Path(tmp) / "report.csv"
            audio_zone_sweep.write_manifest_csv(
                report,
                {
                    "runs": [
                        {
                            "map": " =cmd",
                            "output": "+out",
                            "override": "@override",
                            "status": "-planned",
                            "audit": {
                                "warnings": ["=warning"],
                                "confidence": {"grade": "@risk"},
                            },
                        }
                    ]
                },
            )
            text = report.read_text(encoding="utf-8")

        self.assertIn("' =cmd", text)
        self.assertIn("'+out", text)
        self.assertIn("'@override", text)
        self.assertIn("'-planned", text)
        self.assertIn("'=warning", text)
        self.assertIn("'@risk", text)


if __name__ == "__main__":
    unittest.main()
