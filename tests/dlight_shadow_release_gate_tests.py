from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GATE_PATH = ROOT / "scripts" / "dlight_shadow_release_gate.py"

spec = importlib.util.spec_from_file_location("dlight_shadow_release_gate", GATE_PATH)
assert spec is not None
dlight_shadow_release_gate = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(dlight_shadow_release_gate)


def write_source_defaults(root: Path, glx: str = "0", vulkan: str = "0") -> None:
    sources = {
        "code/renderer/tr_init.c": glx,
        "code/renderervk/tr_init.c": vulkan,
    }
    for relative, value in sources.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(
            f'r_dlightShadows = ri.Cvar_Get( "r_dlightShadows", "{value}", CVAR_ARCHIVE_ND | CVAR_LATCH );\n',
            encoding="utf-8",
        )


def shadow_manager_sample() -> dict[str, object]:
    return {
        "scheduledPasses": 1,
        "scheduledMask": 15,
        "pointScheduled": 1,
        "spotScheduled": 1,
        "csmAtlasScheduled": 1,
        "csmReceiverScheduled": 1,
        "pointPublished": 1,
        "spotPublished": 1,
        "csmPublished": 1,
        "inputDlights": 4,
        "pointPlanned": 2,
        "pointConsidered": 4,
        "pointCandidates": 3,
        "pointRecords": 2,
        "pointCandidateRecords": 3,
        "pointAtlasWidth": 1024,
        "pointAtlasHeight": 512,
        "pointAtlasFaceSize": 128,
        "spotPlans": 2,
        "spotCandidates": 3,
        "spotStaticPlans": 0,
        "spotStaticCandidates": 0,
        "spotSurfacePlans": 2,
        "spotSurfaceCandidates": 3,
        "spotAtlasWidth": 1024,
        "spotAtlasHeight": 1024,
        "spotAtlasTileSize": 256,
        "csmCascadeCount": 4,
        "csmAtlasWidth": 2048,
        "csmAtlasHeight": 512,
        "csmGeneration": 9,
    }


def shadow_manager_summary(scene_ids: tuple[str, ...]) -> dict[str, object]:
    sample = shadow_manager_sample()
    return {
        "found": True,
        "sampleCount": 1,
        "latest": dict(sample),
        "max": dict(sample),
        "scenes": {
            scene_id: {
                "found": True,
                "sampleCount": 1,
                "latest": dict(sample),
                "max": dict(sample),
            }
            for scene_id in scene_ids
        },
    }


def surface_light_spot_sample() -> dict[str, object]:
    return {
        "surfaceSpotCandidates": 3,
        "surfaceSpotPlans": 2,
        "surfaceSpotRequestedTileMin": 128,
        "surfaceSpotRequestedTileMax": 512,
        "surfaceSpotFootprintMin": 96,
        "surfaceSpotFootprintMax": 640,
        "surfaceSpotCasterRadiusMin": 256,
        "surfaceSpotCasterRadiusMax": 1200,
        "surfaceSpotPlanRequestedTileMin": 128,
        "surfaceSpotPlanRequestedTileMax": 512,
        "surfaceSpotTileMin": 128,
        "surfaceSpotTileMax": 256,
        "surfaceSpotAllocated": 2,
        "surfaceSpotAtlasWidth": 1024,
        "surfaceSpotAtlasHeight": 1024,
        "surfaceSpotAtlasTileSize": 256,
        "surfaceSpotAtlasFill": 12,
    }


def surface_light_spot_summary(scene_ids: tuple[str, ...]) -> dict[str, object]:
    sample = surface_light_spot_sample()
    return {
        "found": True,
        "sampleCount": 1,
        "latest": dict(sample),
        "max": dict(sample),
        "scenes": {
            scene_id: {
                "found": True,
                "sampleCount": 1,
                "latest": dict(sample),
                "max": dict(sample),
            }
            for scene_id in scene_ids
        },
    }


def surface_light_spot_lod_summary(scene_ids: tuple[str, ...]) -> dict[str, object]:
    sample = {
        "found": True,
        "status": "passed",
        "sampleCount": 1,
        "requestedTiles": {"low": True, "nominal": True, "promoted": True},
        "maxRequestedTile": 512,
        "maxPlanRequestedTile": 512,
        "maxEffectiveTile": 256,
        "maxAtlasTile": 256,
        "maxFill": 12,
        "failures": [],
    }
    return {
        **sample,
        "scenes": {scene_id: dict(sample) for scene_id in scene_ids},
    }


def csm_shadow_summary(scene_ids: tuple[str, ...]) -> dict[str, object]:
    sample = {
        "found": True,
        "status": "passed",
        "managerSampleCount": 1,
        "debugSampleCount": 1,
        "max": {
            "csmAtlasScheduled": 1,
            "csmReceiverScheduled": 1,
            "csmPublished": 1,
            "csmCascadeCount": 4,
            "csmAtlasWidth": 2048,
            "csmAtlasHeight": 512,
            "csmGeneration": 9,
            "csmDebugCascades": 4,
            "csmDebugResolution": 512,
            "csmCacheHits": 1,
            "csmCacheMisses": 1,
            "csmCacheUncacheable": 0,
            "csmCacheEvents": 2,
        },
        "failures": [],
    }
    return {
        **sample,
        "scenes": {scene_id: dict(sample) for scene_id in scene_ids},
    }


def csm_fallback_summary() -> dict[str, object]:
    return {
        "found": True,
        "status": "passed",
        "managerSampleCount": 4,
        "fallbackSampleCount": 4,
        "reasonCoverage": {
            "no-world": True,
            "no-sky-sun": True,
            "atlas": True,
            "zero-cascade": True,
        },
        "max": {
            "noworld": 1,
            "csmAtlasScheduled": 0,
            "csmReceiverScheduled": 0,
            "csmPublished": 0,
            "csmCascadeCount": 0,
            "csmAtlasWidth": 0,
            "csmAtlasHeight": 0,
            "csmGeneration": 0,
            "csmFallbackNoWorld": 1,
            "csmFallbackNoSun": 1,
            "csmFallbackAtlasUnavailable": 1,
            "csmFallbackZeroCascade": 1,
        },
        "failures": [],
    }


def dlight_shadow_run(renderer: str) -> dict[str, object]:
    categories = dlight_shadow_release_gate.REQUIRED_DLIGHT_SHADOW_CATEGORIES
    surface_categories = dlight_shadow_release_gate.REQUIRED_SURFACELIGHT_SPOT_CATEGORIES
    csm_categories = dlight_shadow_release_gate.REQUIRED_CSM_SHADOW_CATEGORIES
    return {
        "type": "dlight-shadow-scenes",
        "status": "passed",
        "renderer": renderer,
        "screenshots": [
            {
                "name": f"{renderer}-dlight-{category}",
                "found": True,
                "shadowScene": True,
                "scene": category,
                "evidenceCategories": [category],
            }
            for category in categories
        ],
        "csmFallbacks": csm_fallback_summary(),
        "dlightShadow": {
            "found": True,
            "max": {"planned": 2, "renderLights": 2},
            "scenes": {
                category: {"max": {"planned": 2, "renderLights": 2}}
                for category in categories
            },
            "shadowManager": shadow_manager_summary(categories),
            "surfaceLightSpot": surface_light_spot_summary(surface_categories),
            "surfaceLightSpotLod": surface_light_spot_lod_summary(surface_categories),
            "csmShadows": csm_shadow_summary(csm_categories),
            "csmFallbacks": csm_fallback_summary(),
        },
    }


def runtime_manifest(renderer: str) -> dict[str, object]:
    return {
        "gate": dlight_shadow_release_gate.EXPECTED_RUNTIME_GATES[renderer],
        "dryRun": False,
        "runs": [
            {
                "type": "map-screenshots",
                "status": "passed",
                "screenshots": [{"name": f"{renderer}-map", "found": True}],
            },
            dlight_shadow_run(renderer),
        ],
        "gateFailures": [],
    }


def renderdoc_record(renderer: str) -> dict[str, object]:
    return {
        "status": "passed",
        "captureFile": f"{renderer}.rdc",
        "checks": {
            check: True
            for check in dlight_shadow_release_gate.REQUIRED_RENDERDOC_CHECKS[renderer]
        },
    }


def complete_evidence() -> dict[str, object]:
    return {
        "version": 1,
        "build": {
            "glx": {"status": "passed"},
            "vk": {"status": "passed"},
        },
        "shaders": {
            "glx": {"status": "passed"},
            "vk": {
                "status": "passed",
                "variants": list(dlight_shadow_release_gate.VULKAN_SHADER_VARIANTS),
            },
        },
        "runtimeSweeps": {
            "glx": "glx-manifest.json",
            "vk": "vk-manifest.json",
        },
        "renderdoc": {
            "glx": renderdoc_record("glx"),
            "vk": renderdoc_record("vk"),
        },
    }


def write_evidence(root: Path, evidence: dict[str, object]) -> Path:
    (root / "glx-manifest.json").write_text(
        json.dumps(runtime_manifest("glx")),
        encoding="utf-8",
    )
    (root / "vk-manifest.json").write_text(
        json.dumps(runtime_manifest("vk")),
        encoding="utf-8",
    )
    path = root / "evidence.json"
    path.write_text(json.dumps(evidence), encoding="utf-8")
    return path


class DlightShadowReleaseGateTests(unittest.TestCase):
    def test_complete_evidence_allows_default_enable(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_source_defaults(root)
            evidence_path = write_evidence(root, complete_evidence())

            report = dlight_shadow_release_gate.release_gate_report(
                evidence_path,
                source_root=root,
            )

        self.assertEqual(report["status"], "ready")
        self.assertTrue(report["defaultEnableAllowed"])
        self.assertEqual(report["failures"], [])

    def test_missing_renderdoc_check_blocks_default_enable(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_source_defaults(root)
            evidence = complete_evidence()
            renderdoc = evidence["renderdoc"]  # type: ignore[index]
            assert isinstance(renderdoc, dict)
            vulkan = renderdoc["vk"]
            assert isinstance(vulkan, dict)
            checks = vulkan["checks"]
            assert isinstance(checks, dict)
            checks.pop("layoutTransitions")
            evidence_path = write_evidence(root, evidence)

            report = dlight_shadow_release_gate.release_gate_report(
                evidence_path,
                source_root=root,
            )

        self.assertEqual(report["status"], "blocked")
        self.assertFalse(report["defaultEnableAllowed"])
        self.assertTrue(
            any("layoutTransitions" in failure for failure in report["failures"])
        )

    def test_runtime_sweep_requires_screenshot_and_log_categories(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_source_defaults(root)
            evidence = complete_evidence()
            (root / "glx-manifest.json").write_text(
                json.dumps(runtime_manifest("glx")),
                encoding="utf-8",
            )
            manifest = runtime_manifest("vk")
            shadow_run = manifest["runs"][1]  # type: ignore[index]
            assert isinstance(shadow_run, dict)
            shadow_run["screenshots"] = [
                shot
                for shot in shadow_run["screenshots"]  # type: ignore[index]
                if shot["evidenceCategories"] != ["stress-light-budget"]
            ]
            dlight_shadow = shadow_run["dlightShadow"]
            assert isinstance(dlight_shadow, dict)
            scenes = dlight_shadow["scenes"]
            assert isinstance(scenes, dict)
            scenes.pop("stress-light-budget")
            manager = dlight_shadow["shadowManager"]
            assert isinstance(manager, dict)
            manager_scenes = manager["scenes"]
            assert isinstance(manager_scenes, dict)
            manager_scenes.pop("stress-light-budget")
            (root / "vk-manifest.json").write_text(
                json.dumps(manifest),
                encoding="utf-8",
            )
            evidence_path = root / "evidence.json"
            evidence_path.write_text(json.dumps(evidence), encoding="utf-8")

            report = dlight_shadow_release_gate.release_gate_report(
                evidence_path,
                source_root=root,
            )

        self.assertEqual(report["status"], "blocked")
        self.assertTrue(
            any("stress-light-budget" in failure for failure in report["failures"])
        )

    def test_runtime_sweep_requires_shadow_manager_log_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_source_defaults(root)
            evidence = complete_evidence()
            (root / "glx-manifest.json").write_text(
                json.dumps(runtime_manifest("glx")),
                encoding="utf-8",
            )
            manifest = runtime_manifest("vk")
            shadow_run = manifest["runs"][1]  # type: ignore[index]
            assert isinstance(shadow_run, dict)
            dlight_shadow = shadow_run["dlightShadow"]
            assert isinstance(dlight_shadow, dict)
            dlight_shadow.pop("shadowManager")
            (root / "vk-manifest.json").write_text(
                json.dumps(manifest),
                encoding="utf-8",
            )
            evidence_path = root / "evidence.json"
            evidence_path.write_text(json.dumps(evidence), encoding="utf-8")

            report = dlight_shadow_release_gate.release_gate_report(
                evidence_path,
                source_root=root,
            )

        self.assertEqual(report["status"], "blocked")
        self.assertTrue(
            any("shadow manager" in failure for failure in report["failures"])
        )

    def test_runtime_sweep_rejects_boolean_shadow_counters(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_source_defaults(root)
            evidence = complete_evidence()
            (root / "glx-manifest.json").write_text(
                json.dumps(runtime_manifest("glx")),
                encoding="utf-8",
            )
            manifest = runtime_manifest("vk")
            shadow_run = manifest["runs"][1]  # type: ignore[index]
            assert isinstance(shadow_run, dict)
            dlight_shadow = shadow_run["dlightShadow"]
            assert isinstance(dlight_shadow, dict)
            dlight_shadow["max"] = {"planned": True, "renderLights": True}
            (root / "vk-manifest.json").write_text(
                json.dumps(manifest),
                encoding="utf-8",
            )
            evidence_path = root / "evidence.json"
            evidence_path.write_text(json.dumps(evidence), encoding="utf-8")

            report = dlight_shadow_release_gate.release_gate_report(
                evidence_path,
                source_root=root,
            )

        self.assertEqual(report["status"], "blocked")
        self.assertTrue(
            any(
                "active dlight shadow log samples" in failure
                for failure in report["failures"]
            )
        )

    def test_runtime_sweep_requires_surface_light_spot_log_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_source_defaults(root)
            evidence = complete_evidence()
            (root / "glx-manifest.json").write_text(
                json.dumps(runtime_manifest("glx")),
                encoding="utf-8",
            )
            manifest = runtime_manifest("vk")
            shadow_run = manifest["runs"][1]  # type: ignore[index]
            assert isinstance(shadow_run, dict)
            dlight_shadow = shadow_run["dlightShadow"]
            assert isinstance(dlight_shadow, dict)
            dlight_shadow.pop("surfaceLightSpot")
            (root / "vk-manifest.json").write_text(
                json.dumps(manifest),
                encoding="utf-8",
            )
            evidence_path = root / "evidence.json"
            evidence_path.write_text(json.dumps(evidence), encoding="utf-8")

            report = dlight_shadow_release_gate.release_gate_report(
                evidence_path,
                source_root=root,
            )

        self.assertEqual(report["status"], "blocked")
        self.assertTrue(
            any("surfacelight spot log" in failure for failure in report["failures"])
        )

    def test_runtime_sweep_requires_published_surface_light_spot_atlas(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_source_defaults(root)
            evidence = complete_evidence()
            (root / "glx-manifest.json").write_text(
                json.dumps(runtime_manifest("glx")),
                encoding="utf-8",
            )
            manifest = runtime_manifest("vk")
            shadow_run = manifest["runs"][1]  # type: ignore[index]
            assert isinstance(shadow_run, dict)
            dlight_shadow = shadow_run["dlightShadow"]
            assert isinstance(dlight_shadow, dict)
            manager = dlight_shadow["shadowManager"]
            assert isinstance(manager, dict)
            manager_scenes = manager["scenes"]
            assert isinstance(manager_scenes, dict)
            surfacelight = manager_scenes["surfacelight-large-planar"]
            assert isinstance(surfacelight, dict)
            maximum = surfacelight["max"]
            assert isinstance(maximum, dict)
            maximum["spotPublished"] = 0
            maximum["spotSurfacePlans"] = 0
            (root / "vk-manifest.json").write_text(
                json.dumps(manifest),
                encoding="utf-8",
            )
            evidence_path = root / "evidence.json"
            evidence_path.write_text(json.dumps(evidence), encoding="utf-8")

            report = dlight_shadow_release_gate.release_gate_report(
                evidence_path,
                source_root=root,
            )

        self.assertEqual(report["status"], "blocked")
        self.assertTrue(
            any("surfacelight spot atlas manager" in failure for failure in report["failures"])
        )

    def test_runtime_sweep_requires_csm_runtime_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_source_defaults(root)
            evidence = complete_evidence()
            (root / "glx-manifest.json").write_text(
                json.dumps(runtime_manifest("glx")),
                encoding="utf-8",
            )
            manifest = runtime_manifest("vk")
            shadow_run = manifest["runs"][1]  # type: ignore[index]
            assert isinstance(shadow_run, dict)
            dlight_shadow = shadow_run["dlightShadow"]
            assert isinstance(dlight_shadow, dict)
            dlight_shadow.pop("csmShadows")
            (root / "vk-manifest.json").write_text(
                json.dumps(manifest),
                encoding="utf-8",
            )
            evidence_path = root / "evidence.json"
            evidence_path.write_text(json.dumps(evidence), encoding="utf-8")

            report = dlight_shadow_release_gate.release_gate_report(
                evidence_path,
                source_root=root,
            )

        self.assertEqual(report["status"], "blocked")
        self.assertTrue(
            any("CSM runtime" in failure for failure in report["failures"])
        )

    def test_runtime_sweep_requires_published_csm_atlas(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_source_defaults(root)
            evidence = complete_evidence()
            (root / "glx-manifest.json").write_text(
                json.dumps(runtime_manifest("glx")),
                encoding="utf-8",
            )
            manifest = runtime_manifest("vk")
            shadow_run = manifest["runs"][1]  # type: ignore[index]
            assert isinstance(shadow_run, dict)
            dlight_shadow = shadow_run["dlightShadow"]
            assert isinstance(dlight_shadow, dict)
            manager = dlight_shadow["shadowManager"]
            assert isinstance(manager, dict)
            manager_scenes = manager["scenes"]
            assert isinstance(manager_scenes, dict)
            csm_scene = manager_scenes["csm-sky-sun"]
            assert isinstance(csm_scene, dict)
            maximum = csm_scene["max"]
            assert isinstance(maximum, dict)
            maximum["csmPublished"] = 0
            maximum["csmGeneration"] = 0
            (root / "vk-manifest.json").write_text(
                json.dumps(manifest),
                encoding="utf-8",
            )
            evidence_path = root / "evidence.json"
            evidence_path.write_text(json.dumps(evidence), encoding="utf-8")

            report = dlight_shadow_release_gate.release_gate_report(
                evidence_path,
                source_root=root,
            )

        self.assertEqual(report["status"], "blocked")
        self.assertTrue(
            any("CSM shadow manager" in failure for failure in report["failures"])
        )

    def test_runtime_sweep_requires_csm_fallback_smoke(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_source_defaults(root)
            evidence = complete_evidence()
            (root / "glx-manifest.json").write_text(
                json.dumps(runtime_manifest("glx")),
                encoding="utf-8",
            )
            manifest = runtime_manifest("vk")
            shadow_run = manifest["runs"][1]  # type: ignore[index]
            assert isinstance(shadow_run, dict)
            shadow_run.pop("csmFallbacks")
            dlight_shadow = shadow_run["dlightShadow"]
            assert isinstance(dlight_shadow, dict)
            dlight_shadow.pop("csmFallbacks")
            (root / "vk-manifest.json").write_text(
                json.dumps(manifest),
                encoding="utf-8",
            )
            evidence_path = root / "evidence.json"
            evidence_path.write_text(json.dumps(evidence), encoding="utf-8")

            report = dlight_shadow_release_gate.release_gate_report(
                evidence_path,
                source_root=root,
            )

        self.assertEqual(report["status"], "blocked")
        self.assertTrue(
            any("CSM fallback" in failure for failure in report["failures"])
        )

    def test_source_default_enabled_before_gate_is_policy_violation(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_source_defaults(root, glx="1", vulkan="1")
            evidence = complete_evidence()
            renderdoc = evidence["renderdoc"]  # type: ignore[index]
            assert isinstance(renderdoc, dict)
            renderdoc.pop("vk")
            evidence_path = write_evidence(root, evidence)

            report = dlight_shadow_release_gate.release_gate_report(
                evidence_path,
                source_root=root,
            )

        self.assertEqual(report["status"], "failed")
        self.assertTrue(report["policyViolation"])
        self.assertTrue(
            any("enabled by default before" in failure for failure in report["failures"])
        )

    def test_cli_writes_ready_summary(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_source_defaults(root)
            evidence_path = write_evidence(root, complete_evidence())
            summary_path = root / "summary.json"

            exit_code = dlight_shadow_release_gate.main(
                [
                    "--evidence",
                    str(evidence_path),
                    "--source-root",
                    str(root),
                    "--summary",
                    str(summary_path),
                    "--require-ready",
                ]
            )
            summary = json.loads(summary_path.read_text(encoding="utf-8"))

        self.assertEqual(exit_code, 0)
        self.assertEqual(summary["status"], "ready")

    def test_manifest_paths_must_not_escape_evidence_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)

            with self.assertRaisesRegex(ValueError, "must not contain"):
                dlight_shadow_release_gate.load_manifest_value(
                    "../outside.json",
                    root,
                )

            with self.assertRaisesRegex(ValueError, "must not contain"):
                dlight_shadow_release_gate.load_manifest_value(
                    {"manifestPath": "../outside.json"},
                    root,
                )


if __name__ == "__main__":
    unittest.main()
