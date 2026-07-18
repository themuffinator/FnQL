from __future__ import annotations

import argparse
import importlib.util
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SWEEP_PATH = ROOT / "scripts" / "vk_runtime_sweep.py"

spec = importlib.util.spec_from_file_location("vk_runtime_sweep", SWEEP_PATH)
assert spec is not None
vk_runtime_sweep = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(vk_runtime_sweep)


def shadow_manager_sample() -> dict[str, object]:
    return {
        "scheduledPasses": 1,
        "scheduledMask": 13,
        "pointScheduled": 1,
        "spotScheduled": 0,
        "csmAtlasScheduled": 1,
        "csmReceiverScheduled": 1,
        "pointPublished": 1,
        "spotPublished": 0,
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
        "spotStaticPlans": 0,
        "spotStaticCandidates": 0,
        "spotSurfacePlans": 0,
        "spotSurfaceCandidates": 0,
        "csmCascadeCount": 4,
        "csmAtlasWidth": 2048,
        "csmAtlasHeight": 512,
        "csmGeneration": 7,
    }


def shadow_manager_summary(scene_ids: list[str]) -> dict[str, object]:
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


def surface_light_spot_summary(scene_ids: list[str]) -> dict[str, object]:
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


def surface_light_spot_lod_summary(scene_ids: list[str]) -> dict[str, object]:
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


def csm_shadow_summary(scene_ids: list[str]) -> dict[str, object]:
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
            "csmGeneration": 7,
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


def csm_shimmer_screenshot_summary(status: str = "passed") -> dict[str, object]:
    failures = [] if status == "passed" else ["fixture CSM shimmer screenshot diff failed."]
    return {
        "found": True,
        "status": status,
        "scene": "csm-shimmer-path",
        "baselineStep": "baseline",
        "sampleCount": 4,
        "comparisonCount": 3,
        "passedComparisons": 3 if status == "passed" else 2,
        "maxRms": 0.0 if status == "passed" else 8.0,
        "maxChangedPixelRatio": 0.0 if status == "passed" else 0.2,
        "thresholds": {
            "maxRms": vk_runtime_sweep.CSM_SHIMMER_SCREENSHOT_MAX_RMS,
            "maxChangedPixelRatio": (
                vk_runtime_sweep.CSM_SHIMMER_SCREENSHOT_MAX_CHANGED_PIXEL_RATIO
            ),
        },
        "comparisons": [
            {
                "name": "shot",
                "step": "nudge-forward",
                "baselineStep": "baseline",
                "status": status,
                "rms": 0.0 if status == "passed" else 8.0,
                "changedPixelRatio": 0.0 if status == "passed" else 0.2,
                "reason": "" if status == "passed" else "diff-threshold",
            },
        ],
        "failures": failures,
    }


def combined_shadow_atlas_summary(status: str = "passed") -> dict[str, object]:
    failures = [] if status == "passed" else ["fixture combined shadow atlas smoke failed."]
    return {
        "found": True,
        "status": status,
        "scene": "combined-shadow-atlas",
        "sampleCount": 1,
        "max": {
            "scheduledPasses": 4,
            "scheduledMask": 15,
            "pointScheduled": 1,
            "spotScheduled": 1,
            "csmAtlasScheduled": 1,
            "csmReceiverScheduled": 1,
            "pointPublished": 1,
            "spotPublished": 1,
            "csmPublished": 1,
            "pointPlanned": 2,
            "pointRecords": 2,
            "pointAtlasWidth": 1024,
            "pointAtlasHeight": 512,
            "pointAtlasFaceSize": 128,
            "spotPlans": 2,
            "spotStaticPlans": 1,
            "spotSurfacePlans": 1,
            "spotAtlasWidth": 1024,
            "spotAtlasHeight": 1024,
            "spotAtlasTileSize": 256,
            "csmCascadeCount": 4,
            "csmAtlasWidth": 2048,
            "csmAtlasHeight": 512,
            "csmGeneration": 7,
        },
        "failures": failures,
    }


def csm_fallback_summary(status: str = "passed") -> dict[str, object]:
    failures = [] if status == "passed" else ["fixture CSM fallback smoke failed."]
    return {
        "found": True,
        "status": status,
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
            "scheduledPasses": 1,
            "scheduledMask": 1,
            "csmAtlasScheduled": 0,
            "csmReceiverScheduled": 0,
            "csmPublished": 0,
            "csmCascadeCount": 0,
            "csmAtlasWidth": 0,
            "csmAtlasHeight": 0,
            "csmGeneration": 0,
            "csmFallbackSamples": 1,
            "csmFallbackCascades": 0,
            "csmFallbackNoWorld": 1,
            "csmFallbackNoSun": 1,
            "csmFallbackAtlasUnavailable": 1,
            "csmFallbackZeroCascade": 1,
        },
        "failures": failures,
    }


class VkRuntimeSweepParseTests(unittest.TestCase):
    def test_vkinfo_parser_extracts_modern_diagnostics(self) -> None:
        text = "\n".join(
            [
                "pipeline cache: cache/vkpc_123.bin, loaded: 64Kb, saved: 96Kb",
                "display HDR: requested unavailable, metadata: disabled, paper white 203 nits, max 1000 nits",
                "output backend: request hdr10-pq, selected sdr-srgb, native windows-scrgb, display HDR enabled, headroom 4.00, SDR white 203 nits, display max 812 nits, ICC yes/2048, driver windows, display HDR Panel, reason: test",
                "tone map: ACES, exposure 1.00",
                "post gamma: domain display-referred-sdr, shader enabled, r_gamma 1.20, exponent 0.833, overbright scale 2.00",
                "bloom: threshold 0.75, soft knee 0.50, intensity 0.50",
                "modern Vulkan: sync2 enabled, dynamic rendering feature enabled, render-pass backend active",
                "barriers: 12 sync2 / 0 legacy",
                "descriptor writes: 4, binds: 8 calls / 10 sets, material cache: 6 hits / 2 misses",
                "command pool resets: 2 frame / 3 upload",
                "memory: 9 allocs (11 peak), 2048Kb live / 4096Kb peak",
                "Vulkan GPU timings:",
                "  frame begin -> main render pass begin: 0.125 ms",
            ]
        )

        info = vk_runtime_sweep.parse_vkinfo_text(text)

        self.assertTrue(info["found"])
        self.assertEqual(info["modernVulkan"]["sync2"], "enabled")
        self.assertEqual(info["barriers"]["sync2"], 12)
        self.assertEqual(info["displayHdr"]["state"], "requested unavailable")
        self.assertEqual(info["outputBackend"]["request"], "hdr10-pq")
        self.assertEqual(info["outputBackend"]["native"], "windows-scrgb")
        self.assertEqual(info["outputBackend"]["displayMax"], 812.0)
        self.assertEqual(info["toneMap"]["mode"], "ACES")
        self.assertEqual(info["gamma"]["domain"], "display-referred-sdr")
        self.assertEqual(info["gamma"]["shader"], "enabled")
        self.assertAlmostEqual(info["gamma"]["exponent"], 0.833, places=3)
        self.assertEqual(info["gamma"]["overbright"], 2.0)
        self.assertEqual(info["bloom"]["softKnee"], 0.5)
        self.assertEqual(len(info["gpuTimings"]), 1)

    def test_vk_log_analysis_flags_missing_sync2_barriers(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "vk.log"
            log.write_text(
                "\n".join(
                    [
                        "pipeline cache: cache/vkpc_123.bin, loaded: 1Kb, saved: 1Kb",
                        "display HDR: disabled, metadata: disabled, paper white 203 nits, max 1000 nits",
                        "output backend: request auto, selected sdr-srgb, native sdr-srgb, display HDR disabled, headroom 1.00, SDR white 203 nits, display max 203 nits, ICC no/0, driver windows, display SDR Panel, reason: test",
                        "tone map: legacy, exposure 1.00",
                        "post gamma: domain display-referred-sdr, shader enabled, r_gamma 1.00, exponent 1.000, overbright scale 2.00",
                        "bloom: threshold 0.75, soft knee 0.00, intensity 0.50",
                        "modern Vulkan: sync2 enabled, dynamic rendering disabled",
                        "barriers: 0 sync2 / 4 legacy",
                        "descriptor writes: 1, binds: 1 calls / 1 sets, material cache: 0 hits / 1 misses",
                        "command pool resets: 1 frame / 0 upload",
                        "memory: 1 allocs (1 peak), 1Kb live / 1Kb peak",
                    ]
                ),
                encoding="utf-8",
            )

            analysis = vk_runtime_sweep.analyze_vk_log(log, "vk-modern")

            self.assertTrue(
                any("sync2 is enabled" in failure for failure in analysis["failures"])
            )

    def test_hdr_profile_requires_visible_hdr_request(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "vk.log"
            log.write_text(
                "\n".join(
                    [
                        "pipeline cache: cache/vkpc_123.bin, loaded: 1Kb, saved: 1Kb",
                        "display HDR: disabled, metadata: disabled, paper white 203 nits, max 1000 nits",
                        "output backend: request hdr10-pq, selected sdr-srgb, native sdr-srgb, display HDR disabled, headroom 1.00, SDR white 203 nits, display max 203 nits, ICC no/0, driver windows, display SDR Panel, reason: test",
                        "tone map: ACES, exposure 1.00",
                        "post gamma: domain scene-linear, shader enabled, r_gamma 1.00, exponent 1.000, overbright scale 2.00",
                        "bloom: threshold 0.75, soft knee 0.50, intensity 0.50",
                        "modern Vulkan: sync2 disabled, dynamic rendering disabled",
                        "barriers: 0 sync2 / 4 legacy",
                        "descriptor writes: 1, binds: 1 calls / 1 sets, material cache: 0 hits / 1 misses",
                        "command pool resets: 1 frame / 0 upload",
                        "memory: 1 allocs (1 peak), 1Kb live / 1Kb peak",
                    ]
                ),
                encoding="utf-8",
            )

            analysis = vk_runtime_sweep.analyze_vk_log(log, "vk-hdr")

            self.assertTrue(
                any("requested HDR display" in failure for failure in analysis["failures"])
            )


class VkRuntimeSweepGateTests(unittest.TestCase):
    def test_modern_gate_requires_gpu_timings_and_timedemo_metrics(self) -> None:
        manifest = {
            "gate": "vk-modern",
            "dryRun": False,
            "demos": ["demo1"],
            "runs": [
                {
                    "type": "map-screenshots",
                    "status": "passed",
                    "screenshots": [{"name": "shot", "found": True}],
                    "vkinfo": {
                        "failures": [],
                        "gpuTimings": [],
                    },
                },
                {
                    "type": "timedemo",
                    "status": "passed",
                    "demo": "demo1",
                },
            ],
        }

        failures = vk_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("GPU timing" in failure for failure in failures))
        self.assertTrue(any("Missing timedemo metrics" in failure for failure in failures))

    def test_dry_run_gate_is_planning_only(self) -> None:
        manifest = {
            "gate": "vk-modern",
            "dryRun": True,
            "demos": ["demo1"],
            "runs": [],
        }

        self.assertEqual(vk_runtime_sweep.evaluate_gate(manifest), [])

    def test_map_config_contains_vkinfo_and_timestamp_sampling(self) -> None:
        class Args:
            profile = "vk-modern"
            startup_wait = 30
            map_wait = 180
            screenshot_wait = 8
            perf_sample_wait = 4
            no_perf_samples = False

        cfg, screenshots = vk_runtime_sweep.build_map_cfg(
            Args(),
            {"r_fbo": "1"},
            ["q3dm1"],
            "run",
        )

        self.assertIn("vkinfo", cfg)
        self.assertIn('set r_speeds "7"', cfg)
        self.assertEqual(screenshots[0]["baselineKey"], "vk-modern-map1-q3dm1-vk")

    def test_extra_set_rejects_cfg_injection(self) -> None:
        with self.assertRaisesRegex(ValueError, "not safe"):
            vk_runtime_sweep.parse_extra_sets(["r_safe;quit=1"])
        with self.assertRaisesRegex(ValueError, "unsafe control"):
            vk_runtime_sweep.parse_extra_sets(["r_safe=1\nquit"])

    def test_motion_blur_toggle_is_applied_before_vulkan_initialization(self) -> None:
        startup = vk_runtime_sweep.launch_cvars(
            {"r_motionBlur": "1", "r_motionBlurStrength": "0.25"}
        )

        self.assertEqual(startup, {"r_motionBlur": "1"})

    def test_runtime_options_reject_invalid_waits_and_timeouts(self) -> None:
        args = argparse.Namespace(
            width=640,
            height=480,
            timeout=240.0,
            startup_wait=30,
            map_wait=180,
            screenshot_wait=8,
            perf_sample_wait=4,
        )
        vk_runtime_sweep.validate_runtime_options(args)

        args.timeout = 0
        with self.assertRaisesRegex(ValueError, "timeout"):
            vk_runtime_sweep.validate_runtime_options(args)

        args.timeout = float("nan")
        with self.assertRaisesRegex(ValueError, "timeout"):
            vk_runtime_sweep.validate_runtime_options(args)

        args.timeout = 240.0
        args.screenshot_wait = -1
        with self.assertRaisesRegex(ValueError, "screenshot-wait"):
            vk_runtime_sweep.validate_runtime_options(args)

    def test_q3_command_tokens_reject_cfg_and_path_injection(self) -> None:
        self.assertEqual(
            vk_runtime_sweep.validate_q3_command_tokens(["q3dm1", "demos/demo1.dm_68"], "--maps"),
            ["q3dm1", "demos/demo1.dm_68"],
        )
        with self.assertRaisesRegex(ValueError, "safe Quake 3"):
            vk_runtime_sweep.validate_q3_command_tokens(["q3dm1;quit"], "--maps")
        with self.assertRaisesRegex(ValueError, "single safe mod"):
            vk_runtime_sweep.validate_fs_game("../baseq3")

    def test_modern_gate_requires_dlight_shadow_scene(self) -> None:
        manifest = {
            "gate": "vk-modern",
            "dryRun": False,
            "demos": ["demo1"],
            "runs": [
                {
                    "type": "map-screenshots",
                    "status": "passed",
                    "screenshots": [{"name": "shot", "found": True}],
                    "vkinfo": {
                        "failures": [],
                        "gpuTimings": [{"from": "a", "to": "b", "msec": 0.1}],
                    },
                },
                {
                    "type": "timedemo",
                    "status": "passed",
                    "demo": "demo1",
                    "timedemoMetrics": {"fps": 100.0},
                },
            ],
        }

        failures = vk_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("dlight shadow scene run" in failure for failure in failures))

    def test_dlight_shadow_config_uses_startup_cvars_and_test_lights(self) -> None:
        class Args:
            profile = "vk-modern"
            startup_wait = 30
            map_wait = 180
            screenshot_wait = 8
            perf_sample_wait = 4
            no_perf_samples = False

        cvars = vk_runtime_sweep.dlight_shadow_scene_cvars({"r_fbo": "1"})
        startup = vk_runtime_sweep.launch_cvars(cvars)
        scenes = vk_runtime_sweep.dlight_shadow_evidence_scenes()
        cfg, screenshots = vk_runtime_sweep.build_dlight_shadow_cfg(
            Args(),
            cvars,
            scenes,
            "run",
        )

        self.assertEqual(startup["r_dlightShadows"], "1")
        self.assertEqual(startup["r_dlightShadowMaxLights"], "8")
        self.assertEqual(startup["r_staticLights"], "1")
        self.assertEqual(startup["r_staticLightShadows"], "1")
        self.assertEqual(startup["r_csmShadows"], "1")
        self.assertEqual(startup["r_csmResolution"], "512")
        self.assertEqual(startup["r_csmDebugFallback"], "0")
        self.assertEqual(startup["r_spotShadows"], "1")
        self.assertEqual(startup["r_spotShadowResolution"], "512")
        self.assertEqual(startup["r_surfaceLightProxies"], "1")
        self.assertEqual(startup["r_surfaceLightProxyShadows"], "1")
        self.assertIn("echo DLIGHT_SHADOW_SCENE_BEGIN world-geometry", cfg)
        self.assertIn("echo DLIGHT_SHADOW_SCENE_BEGIN csm-sky-sun", cfg)
        self.assertIn("echo DLIGHT_SHADOW_SCENE_BEGIN csm-shimmer-path", cfg)
        self.assertIn("echo CSM_SHIMMER_STEP_BEGIN csm-shimmer-path baseline", cfg)
        self.assertIn("setviewpos 0.000 0.000 512.000 -10.000 90.000 0.000", cfg)
        self.assertIn("setviewpos 0.000 0.000 512.000 -10.000 90.030 0.000", cfg)
        self.assertIn("echo DLIGHT_SHADOW_SCENE_BEGIN surfacelight-large-planar", cfg)
        self.assertIn("echo DLIGHT_SHADOW_SCENE_BEGIN combined-shadow-atlas", cfg)
        self.assertIn("echo DLIGHT_SHADOW_SCENE_BEGIN csm-fallback-no-world", cfg)
        self.assertIn("echo DLIGHT_SHADOW_SCENE_BEGIN csm-fallback-atlas-unavailable", cfg)
        self.assertIn("devmap q3dm6", cfg)
        self.assertIn("devmap q3dm11", cfg)
        self.assertIn("devmap q3dm17", cfg)
        self.assertIn('set r_csmShadows "1"', cfg)
        self.assertIn('set r_csmDebug "1"', cfg)
        self.assertIn('set r_csmDebugFallback "1"', cfg)
        self.assertIn('set r_csmDebugFallback "4"', cfg)
        self.assertIn('set r_staticLightDebug "1"', cfg)
        self.assertIn('set r_surfaceLightProxies "1"', cfg)
        self.assertIn('set r_surfaceLightProxyShadows "1"', cfg)
        self.assertIn('set r_spotShadows "1"', cfg)
        self.assertIn("r_dlightTest 8 720 224 48 0", cfg)
        self.assertIn("r_dlightTest 8 780 224 56 0", cfg)
        self.assertIn("r_dlightTest 16 900 256 72 0", cfg)
        self.assertIn('set r_speeds "4"', cfg)
        self.assertTrue(all(shot["shadowScene"] for shot in screenshots))
        csm_path_shots = [shot for shot in screenshots if shot.get("csmCameraPath")]
        self.assertEqual(len(csm_path_shots), len(vk_runtime_sweep.CSM_SHIMMER_CAMERA_PATH))
        self.assertEqual(
            [shot["csmPathStep"] for shot in csm_path_shots],
            ["baseline", "nudge-forward", "nudge-side", "micro-yaw"],
        )
        self.assertEqual(
            vk_runtime_sweep.dlight_shadow_scene_categories(screenshots),
            set(vk_runtime_sweep.DLIGHT_SHADOW_EVIDENCE_CATEGORIES),
        )
        self.assertTrue(
            any(
                shot["baselineKey"]
                == "vk-modern-dlight-shadows-csm-shimmer-path-baseline-q3dm17-vk"
                for shot in screenshots
            )
        )
        self.assertTrue(
            any(
                shot["baselineKey"] == "vk-modern-dlight-shadows-stress-light-budget-q3dm6-vk"
                for shot in screenshots
            )
        )
        combined = next(
            scene for scene in scenes
            if scene["id"] == vk_runtime_sweep.COMBINED_SHADOW_ATLAS_SCENE_ID
        )
        self.assertEqual(combined["sidecarLights"][0]["type"], "spot")

    def test_dlight_shadow_sidecar_lights_are_staged_under_homepath(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            records = vk_runtime_sweep.write_dlight_shadow_sidecar_lights(
                root,
                "",
                vk_runtime_sweep.dlight_shadow_evidence_scenes(),
            )

            self.assertEqual(len(records), 1)
            self.assertEqual(records[0]["map"], "q3dm6")
            sidecar = root / "baseq3" / "maps" / "q3dm6.lights.json"
            self.assertTrue(sidecar.exists())
            text = sidecar.read_text(encoding="utf-8")
            self.assertIn('"type": "spot"', text)
            self.assertIn('"combined-sidecar-spot"', text)

    def test_combined_shadow_atlas_summary_requires_all_active_atlas_types(self) -> None:
        maximum = dict(combined_shadow_atlas_summary()["max"])
        dlight_shadow = {
            "shadowManager": {
                "scenes": {
                    vk_runtime_sweep.COMBINED_SHADOW_ATLAS_SCENE_ID: {
                        "sampleCount": 1,
                        "max": maximum,
                    },
                },
            },
        }

        summary = vk_runtime_sweep.combined_shadow_atlas_summary(dlight_shadow)

        self.assertEqual(summary["status"], "passed")
        self.assertTrue(vk_runtime_sweep.combined_shadow_atlas_summary_active(summary))

        maximum["spotSurfacePlans"] = 0
        maximum["scheduledMask"] = 0x0D
        failed = vk_runtime_sweep.combined_shadow_atlas_summary(dlight_shadow)

        self.assertEqual(failed["status"], "failed")
        self.assertTrue(
            any("surfacelight spot proxy" in failure for failure in failed["failures"])
        )
        self.assertTrue(
            any("schedule mask" in failure for failure in failed["failures"])
        )

    def test_csm_fallback_summary_requires_no_csm_publication(self) -> None:
        manager_samples = [
            {
                **shadow_manager_sample(),
                "noworld": 1,
                "scheduledPasses": 1,
                "scheduledMask": 1,
                "csmAtlasScheduled": 0,
                "csmReceiverScheduled": 0,
                "csmPublished": 0,
                "csmCascadeCount": 0,
                "csmAtlasWidth": 0,
                "csmAtlasHeight": 0,
                "csmGeneration": 0,
            }
            for _reason in vk_runtime_sweep.CSM_FALLBACK_REQUIRED_REASONS
        ]
        fallback_samples = [
            {
                "csmFallbackSamples": 1,
                "csmFallbackCascades": 0,
                "csmFallbackNoWorld": 1 if reason == "no-world" else 0,
                "csmFallbackNoSun": 1 if reason == "no-sky-sun" else 0,
                "csmFallbackAtlasUnavailable": 1 if reason == "atlas" else 0,
                "csmFallbackZeroCascade": 1 if reason == "zero-cascade" else 0,
                "csmFallbackProjection": 0,
                "csmFallbackStrength": 0,
                "csmFallbackDisabled": 0,
            }
            for reason in vk_runtime_sweep.CSM_FALLBACK_REQUIRED_REASONS
        ]

        summary = vk_runtime_sweep.csm_fallback_summary(
            manager_samples,
            fallback_samples,
            vk_runtime_sweep.CSM_FALLBACK_REQUIRED_REASONS,
        )

        self.assertEqual(summary["status"], "passed")
        self.assertTrue(vk_runtime_sweep.csm_fallback_summary_active(summary))

        manager_samples[0]["csmReceiverScheduled"] = 1
        failed = vk_runtime_sweep.csm_fallback_summary(
            manager_samples,
            fallback_samples[:-1],
            vk_runtime_sweep.CSM_FALLBACK_REQUIRED_REASONS,
        )

        self.assertEqual(failed["status"], "failed")
        self.assertTrue(
            any("csmReceiverScheduled" in failure for failure in failed["failures"])
        )
        self.assertTrue(
            any("zero-cascade" in failure for failure in failed["failures"])
        )

    def test_csm_fallback_skip_logs_are_scene_scoped(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "vk.log"
            log.write_text(
                "\n".join(
                    [
                        "DLIGHT_SHADOW_SCENE_BEGIN csm-fallback-no-world",
                        "shadow manager view:1 frame:7 noworld:1 sched:1 mask:1 "
                        "p:1 s:0 ca:0 cr:0 pub p:1 s:0 c:0 inputs dlight:4 "
                        "point:1/4 cand:1 records:1/1 atlas:512x256/128 "
                        "fill:25% gen:9 spot:0/0 src static:0/0 surface:0/0 "
                        "atlas:0x0/0 fill:0% gen:0 csm:0 atlas:0x0 gen:0",
                        "csm plan cascades:0 skip no-world",
                        "DLIGHT_SHADOW_SCENE_END csm-fallback-no-world",
                        "DLIGHT_SHADOW_SCENE_BEGIN csm-fallback-no-sun",
                        "shadow manager view:1 frame:8 noworld:0 sched:1 mask:1 "
                        "p:1 s:0 ca:0 cr:0 pub p:1 s:0 c:0 inputs dlight:4 "
                        "point:1/4 cand:1 records:1/1 atlas:512x256/128 "
                        "fill:25% gen:10 spot:0/0 src static:0/0 surface:0/0 "
                        "atlas:0x0/0 fill:0% gen:0 csm:0 atlas:0x0 gen:0",
                        "csm plan cascades:0 skip no-sky-sun",
                        "DLIGHT_SHADOW_SCENE_END csm-fallback-no-sun",
                        "DLIGHT_SHADOW_SCENE_BEGIN csm-fallback-atlas-unavailable",
                        "shadow manager view:1 frame:9 noworld:0 sched:1 mask:1 "
                        "p:1 s:0 ca:0 cr:0 pub p:1 s:0 c:0 inputs dlight:4 "
                        "point:1/4 cand:1 records:1/1 atlas:512x256/128 "
                        "fill:25% gen:11 spot:0/0 src static:0/0 surface:0/0 "
                        "atlas:0x0/0 fill:0% gen:0 csm:0 atlas:0x0 gen:0",
                        "csm plan cascades:0 skip atlas",
                        "DLIGHT_SHADOW_SCENE_END csm-fallback-atlas-unavailable",
                        "DLIGHT_SHADOW_SCENE_BEGIN csm-fallback-zero-cascade",
                        "shadow manager view:1 frame:10 noworld:0 sched:1 mask:1 "
                        "p:1 s:0 ca:0 cr:0 pub p:1 s:0 c:0 inputs dlight:4 "
                        "point:1/4 cand:1 records:1/1 atlas:512x256/128 "
                        "fill:25% gen:12 spot:0/0 src static:0/0 surface:0/0 "
                        "atlas:0x0/0 fill:0% gen:0 csm:0 atlas:0x0 gen:0",
                        "csm plan cascades:0 skip zero-cascade",
                        "DLIGHT_SHADOW_SCENE_END csm-fallback-zero-cascade",
                    ]
                ),
                encoding="utf-8",
            )

            analysis = vk_runtime_sweep.analyze_dlight_shadow_log(log)

        fallback = analysis["csmFallbacks"]
        self.assertEqual(fallback["status"], "passed")
        self.assertEqual(fallback["fallbackSampleCount"], 4)
        self.assertEqual(fallback["reasonCoverage"]["atlas"], True)
        self.assertEqual(
            fallback["scenes"]["csm-fallback-no-world"]["max"]["noworld"],
            1,
        )

    def test_dlight_shadow_log_analysis_extracts_active_samples(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "vk.log"
            log.write_text(
                "\n".join(
                    [
                        "DLIGHT_SHADOW_SCENE_BEGIN world-geometry",
                        "shadow manager view:1 frame:2 noworld:0 sched:3 mask:d "
                        "p:1 s:0 ca:1 cr:1 pub p:1 s:0 c:1 inputs dlight:4 "
                        "point:2/4 cand:3 records:2/3 atlas:1024x512/128 "
                        "fill:75% gen:7 spot:0/0 src static:0/0 surface:0/0 "
                        "atlas:0x0/0 fill:0% gen:0 csm:4 atlas:2048x512 gen:7",
                        "shadow atlas contract backend:vulkan atlas:point active:1 "
                        "tile:128 size:1024x512 records:2 fill:75% filter:poisson4 "
                        "pad:1 clamp:1 sampler:clamp-edge "
                        "allocation:priority-dlight-index deterministic:1",
                        "shadow atlas contract backend:vulkan atlas:spot active:0 "
                        "tile:0 size:0x0 records:0 fill:0% filter:poisson4 "
                        "pad:1 clamp:1 sampler:clamp-edge "
                        "allocation:priority-source-index deterministic:1",
                        "shadow atlas contract backend:vulkan atlas:csm active:1 "
                        "tile:512 size:2048x512 records:4 fill:100% filter:poisson4 "
                        "pad:1 clamp:1 sampler:clamp-edge "
                        "allocation:cascade-index deterministic:1",
                        "csm shadows sky:textures/skies/dimclouds cascades:4 res:512 max:2048 "
                        "lambda:0.65 filter:poisson-4 strength:1.00 rbias:8.00 "
                        "cbias:1.50/1.50/0.50 light-dir:0.00 -0.71 -0.71 "
                        "to-sun:0.00 0.71 0.71 split far:64 256 768 2048 "
                        "texel:1.00 2.00 4.00 8.00 depth center:100 200 300 400 "
                        "caster:20 cache h/m/u:1/1/0 "
                        "cpu:2ms recv world:12 ent:3",
                        "csm cascade backend:vulkan index:0 split:1..64 atlas:0,0/512 "
                        "view:0,0 512x512 api:0,0 512x512 sample-y:inverted "
                        "clip-y:flipped depth:forward ndc:0..1 clear:1.0 compare:lequal "
                        "bounds x:-924..1124 y:-256..256 z:-256..256 "
                        "origin:100 0 0 texel:1.00",
                        "surfacelight spot plan cand:3 plan:2 req:128-512 "
                        "foot:96-640 caster:256-1200 planreq:128-512 "
                        "tile:128-256 cone:0-0/45-65 alloc:2 "
                        "atlas:1024x1024/256 fill:12% reject weak:1 offview:2 "
                        "budget:1 malformed:1",
                        "dlight shadows plan:2/4 cand:3 atlas:1024x512/128 fill:75% "
                        "filter:poisson4 taps:4 offsets:0.25/0.75 "
                        "render lights:2 faces:10 batches:5 draws:5 surfs:20 cpu:1ms",
                        "DLIGHT_SHADOW_SCENE_END world-geometry",
                        "DLIGHT_SHADOW_SCENE_BEGIN csm-shimmer-path",
                        "shadow manager view:1 frame:3 noworld:0 sched:3 mask:d "
                        "p:1 s:0 ca:1 cr:1 pub p:1 s:0 c:1 inputs dlight:4 "
                        "point:2/4 cand:3 records:2/3 atlas:1024x512/128 "
                        "fill:75% gen:7 spot:0/0 src static:0/0 surface:0/0 "
                        "atlas:0x0/0 fill:0% gen:0 csm:4 atlas:2048x512 gen:7",
                        "csm shadows sky:textures/skies/dimclouds cascades:4 res:512 max:2048 "
                        "lambda:0.65 filter:poisson-4 strength:1.00 rbias:8.00 "
                        "cbias:1.50/1.50/0.50 light-dir:0.00 -0.71 -0.71 "
                        "to-sun:0.00 0.71 0.71 split far:64 256 768 2048 "
                        "texel:1.00 2.00 4.00 8.00 depth center:100.0 200.0 300.0 400.0 "
                        "caster:20 cache h/m/u:0/1/0 cpu:2ms recv world:12 ent:3",
                        "shadow manager view:1 frame:4 noworld:0 sched:3 mask:d "
                        "p:1 s:0 ca:1 cr:1 pub p:1 s:0 c:1 inputs dlight:4 "
                        "point:2/4 cand:3 records:2/3 atlas:1024x512/128 "
                        "fill:75% gen:7 spot:0/0 src static:0/0 surface:0/0 "
                        "atlas:0x0/0 fill:0% gen:0 csm:4 atlas:2048x512 gen:7",
                        "csm shadows sky:textures/skies/dimclouds cascades:4 res:512 max:2048 "
                        "lambda:0.65 filter:poisson-4 strength:1.00 rbias:8.00 "
                        "cbias:1.50/1.50/0.50 light-dir:0.00 -0.71 -0.71 "
                        "to-sun:0.00 0.71 0.71 split far:64 256 768 2048 "
                        "texel:1.00 2.00 4.00 8.00 depth center:100.5 200.0 300.0 400.0 "
                        "caster:20 cache h/m/u:1/1/0 cpu:0ms recv world:12 ent:3",
                        "shadow manager view:1 frame:5 noworld:0 sched:3 mask:d "
                        "p:1 s:0 ca:1 cr:1 pub p:1 s:0 c:1 inputs dlight:4 "
                        "point:2/4 cand:3 records:2/3 atlas:1024x512/128 "
                        "fill:75% gen:7 spot:0/0 src static:0/0 surface:0/0 "
                        "atlas:0x0/0 fill:0% gen:0 csm:4 atlas:2048x512 gen:7",
                        "csm shadows sky:textures/skies/dimclouds cascades:4 res:512 max:2048 "
                        "lambda:0.65 filter:poisson-4 strength:1.00 rbias:8.00 "
                        "cbias:1.50/1.50/0.50 light-dir:0.00 -0.71 -0.71 "
                        "to-sun:0.00 0.71 0.71 split far:64 256 768 2048 "
                        "texel:1.00 2.00 4.00 8.00 depth center:100.5 200.0 300.0 400.0 "
                        "caster:20 cache h/m/u:2/1/0 cpu:0ms recv world:12 ent:3",
                        "shadow manager view:1 frame:6 noworld:0 sched:3 mask:d "
                        "p:1 s:0 ca:1 cr:1 pub p:1 s:0 c:1 inputs dlight:4 "
                        "point:2/4 cand:3 records:2/3 atlas:1024x512/128 "
                        "fill:75% gen:8 spot:0/0 src static:0/0 surface:0/0 "
                        "atlas:0x0/0 fill:0% gen:0 csm:4 atlas:2048x512 gen:8",
                        "csm shadows sky:textures/skies/dimclouds cascades:4 res:512 max:2048 "
                        "lambda:0.65 filter:poisson-4 strength:1.00 rbias:8.00 "
                        "cbias:1.50/1.50/0.50 light-dir:0.00 -0.71 -0.71 "
                        "to-sun:0.00 0.71 0.71 split far:64 256 768 2048 "
                        "texel:1.00 2.00 4.00 8.00 depth center:100.5 200.0 300.0 400.0 "
                        "caster:20 cache h/m/u:2/2/0 cpu:1ms recv world:12 ent:3",
                        "DLIGHT_SHADOW_SCENE_END csm-shimmer-path",
                    ]
                ),
                encoding="utf-8",
            )

            analysis = vk_runtime_sweep.analyze_dlight_shadow_log(log)

        self.assertTrue(analysis["found"])
        self.assertEqual(analysis["max"]["planned"], 2)
        self.assertEqual(analysis["max"]["renderLights"], 2)
        self.assertEqual(analysis["scenes"]["world-geometry"]["max"]["planned"], 2)
        manager = analysis["shadowManager"]
        self.assertTrue(manager["found"])
        self.assertEqual(manager["max"]["pointScheduled"], 1)
        self.assertEqual(manager["max"]["pointPublished"], 1)
        self.assertEqual(manager["max"]["csmAtlasScheduled"], 1)
        self.assertEqual(manager["max"]["csmPublished"], 1)
        self.assertEqual(manager["max"]["spotStaticCandidates"], 0)
        self.assertEqual(
            manager["scenes"]["world-geometry"]["max"]["pointRecords"],
            2,
        )
        atlas = analysis["shadowAtlasContract"]
        self.assertTrue(atlas["found"])
        self.assertEqual(atlas["status"], "passed")
        self.assertEqual(atlas["sampleCount"], 3)
        self.assertEqual(
            atlas["atlasCoverage"],
            {"point": True, "spot": True, "csm": True},
        )
        self.assertEqual(
            atlas["activeAtlasCoverage"],
            {"point": True, "spot": False, "csm": True},
        )
        self.assertEqual(atlas["max"]["shadowAtlasBackendVulkan"], 1)
        self.assertEqual(atlas["max"]["shadowAtlasClampTexels"], 1)
        self.assertEqual(atlas["max"]["shadowAtlasDeterministic"], 1)
        surface_spot = analysis["surfaceLightSpot"]
        self.assertTrue(surface_spot["found"])
        self.assertEqual(surface_spot["max"]["surfaceSpotCandidates"], 3)
        self.assertEqual(surface_spot["max"]["surfaceSpotPlans"], 2)
        self.assertEqual(surface_spot["max"]["surfaceSpotFootprintMax"], 640)
        self.assertEqual(surface_spot["max"]["surfaceSpotCasterRadiusMax"], 1200)
        self.assertEqual(surface_spot["max"]["surfaceSpotTileMax"], 256)
        self.assertEqual(surface_spot["max"]["surfaceSpotRejectedMalformed"], 1)
        self.assertEqual(
            surface_spot["scenes"]["world-geometry"]["max"]["surfaceSpotAllocated"],
            2,
        )
        lod = analysis["surfaceLightSpotLod"]
        self.assertTrue(lod["found"])
        self.assertEqual(lod["status"], "passed")
        self.assertEqual(
            lod["requestedTiles"],
            {"low": True, "nominal": True, "promoted": True},
        )
        self.assertEqual(lod["maxRequestedTile"], 512)
        self.assertEqual(lod["maxEffectiveTile"], 256)
        self.assertEqual(lod["maxAtlasTile"], 256)
        self.assertEqual(lod["maxFill"], 12)
        self.assertEqual(lod["scenes"]["world-geometry"]["status"], "passed")
        csm = analysis["csmShadows"]
        self.assertTrue(csm["found"])
        self.assertEqual(csm["status"], "passed")
        self.assertEqual(csm["max"]["csmCascadeCount"], 4)
        self.assertEqual(csm["max"]["csmDebugResolution"], 512)
        self.assertEqual(csm["max"]["csmCacheEvents"], 4)
        self.assertEqual(csm["scenes"]["world-geometry"]["status"], "passed")
        stability = analysis["csmStability"]
        self.assertTrue(stability["found"])
        self.assertEqual(stability["status"], "passed")
        self.assertEqual(stability["managerSampleCount"], 4)
        self.assertEqual(stability["debugSampleCount"], 4)
        self.assertEqual(stability["generationDelta"], 1)
        self.assertEqual(stability["maxSnapDepthDeltaMilli"], 500)
        self.assertEqual(stability["max"]["csmCacheEvents"], 4)
        self.assertEqual(stability["scenes"]["csm-shimmer-path"]["status"], "passed")
        cascade = analysis["csmCascadeContract"]
        self.assertEqual(cascade["status"], "passed")
        self.assertEqual(cascade["max"]["csmCascadeBackendVulkan"], 1)
        self.assertEqual(cascade["max"]["csmCascadeSampleYInverted"], 1)
        self.assertEqual(cascade["max"]["csmCascadeClipYFlipped"], 1)
        self.assertEqual(cascade["max"]["csmCascadeNdcZeroToOne"], 1)
        profile = analysis["shadowProfile"]
        self.assertTrue(profile["found"])
        self.assertEqual(profile["status"], "passed")
        self.assertTrue(profile["profileReady"])
        self.assertEqual(profile["sampleCounts"]["atlas"], 3)
        self.assertEqual(
            profile["costBuckets"]["rasterWork"]["pointDraws"],
            5,
        )
        self.assertEqual(
            profile["costBuckets"]["rasterWork"]["csmCasterSurfaces"],
            20,
        )
        self.assertEqual(
            profile["costBuckets"]["samplerPressure"]["maxAtlasFill"],
            100,
        )
        self.assertEqual(
            profile["costBuckets"]["orchestrationPressure"]["scheduledPasses"],
            3,
        )
        self.assertEqual(profile["costBuckets"]["cpuTiming"]["maxCpuMsec"], 2)
        self.assertEqual(profile["scenes"]["world-geometry"]["status"], "passed")

    def test_shadow_profile_waits_for_correctness_contracts(self) -> None:
        summary = vk_runtime_sweep.shadow_profile_summary(
            dlight_samples=[
                {
                    "planned": 1,
                    "renderLights": 1,
                    "draws": 2,
                    "cpuMsec": 1,
                }
            ],
            manager_samples=[
                {
                    "scheduledPasses": 1,
                    "scheduledMask": 1,
                    "pointScheduled": 1,
                    "pointPublished": 1,
                    "pointRecords": 1,
                }
            ],
            shadow_atlas_samples=[],
            surface_spot_samples=[],
            csm_samples=[],
            csm_cascade_samples=[],
        )

        self.assertEqual(summary["status"], "failed")
        self.assertFalse(summary["profileReady"])
        self.assertTrue(
            any("shadow atlas contract" in failure for failure in summary["failures"])
        )

    def test_surface_light_spot_lod_smoke_reports_bad_ranges(self) -> None:
        summary = vk_runtime_sweep.surface_light_spot_lod_summary(
            [
                {
                    "surfaceSpotRequestedTileMin": 128,
                    "surfaceSpotRequestedTileMax": 768,
                    "surfaceSpotPlanRequestedTileMin": 128,
                    "surfaceSpotPlanRequestedTileMax": 768,
                    "surfaceSpotTileMin": 128,
                    "surfaceSpotTileMax": 512,
                    "surfaceSpotAtlasTileSize": 256,
                    "surfaceSpotAtlasFill": 92,
                }
            ]
        )

        self.assertEqual(summary["status"], "failed")
        self.assertTrue(
            any("requested tile exceeded cap" in failure for failure in summary["failures"])
        )
        self.assertTrue(
            any("effective tile exceeded atlas tile" in failure for failure in summary["failures"])
        )
        self.assertTrue(any("atlas fill exceeded" in failure for failure in summary["failures"]))
        missing = vk_runtime_sweep.surface_light_spot_lod_summary(
            [
                {
                    "surfaceSpotRequestedTileMin": 128,
                    "surfaceSpotRequestedTileMax": 256,
                    "surfaceSpotPlanRequestedTileMin": 128,
                    "surfaceSpotPlanRequestedTileMax": 256,
                    "surfaceSpotTileMin": 128,
                    "surfaceSpotTileMax": 256,
                    "surfaceSpotAtlasTileSize": 256,
                    "surfaceSpotAtlasFill": 12,
                }
            ]
        )
        self.assertTrue(
            any("promoted(512)" in failure for failure in missing["failures"])
        )

    def test_csm_shimmer_screenshot_diff_summary_compares_path_frames(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            baseline = root / "baseline.png"
            candidate = root / "candidate.png"
            vk_runtime_sweep.write_png_rgba(
                baseline,
                2,
                1,
                bytes((64, 96, 128, 255, 32, 32, 32, 255)),
            )
            vk_runtime_sweep.write_png_rgba(
                candidate,
                2,
                1,
                bytes((64, 96, 128, 255, 32, 32, 32, 255)),
            )
            screenshots = [
                {
                    "name": "baseline",
                    "found": True,
                    "path": str(baseline),
                    "scene": "csm-shimmer-path",
                    "csmCameraPath": True,
                    "csmPathStep": "baseline",
                    "baselineKey": "vk-csm-baseline",
                }
            ]
            for index, step in enumerate(("nudge-forward", "nudge-side", "micro-yaw"), start=1):
                screenshots.append(
                    {
                        "name": f"candidate-{index}",
                        "found": True,
                        "path": str(candidate),
                        "scene": "csm-shimmer-path",
                        "csmCameraPath": True,
                        "csmPathStep": step,
                        "baselineKey": f"vk-csm-{step}",
                    }
                )

            summary = vk_runtime_sweep.apply_csm_shimmer_screenshot_diffs(
                screenshots,
                root / "diffs",
            )

            self.assertEqual(summary["status"], "passed")
            self.assertEqual(summary["comparisonCount"], 3)
            self.assertEqual(screenshots[1]["csmShimmerComparison"]["status"], "passed")
            self.assertTrue(Path(screenshots[1]["csmShimmerComparison"]["diffPath"]).exists())

    def test_modern_gate_requires_dlight_shadow_category_evidence(self) -> None:
        screenshots = [
            {
                "name": f"shot-{category}",
                "found": True,
                "shadowScene": True,
                "scene": category,
                "evidenceCategories": [category],
            }
            for category in vk_runtime_sweep.DLIGHT_SHADOW_EVIDENCE_CATEGORIES
        ]
        screenshots = [
            shot for shot in screenshots
            if shot["evidenceCategories"] != ["stress-light-budget"]
        ]
        surface_scene_ids = list(vk_runtime_sweep.SURFACELIGHT_SPOT_EVIDENCE_CATEGORIES)
        csm_scene_ids = list(vk_runtime_sweep.CSM_SHADOW_EVIDENCE_CATEGORIES)
        manifest = {
            "gate": "vk-modern",
            "dryRun": False,
            "demos": ["demo1"],
            "runs": [
                {
                    "type": "map-screenshots",
                    "status": "passed",
                    "screenshots": [{"name": "shot", "found": True}],
                    "vkinfo": {
                        "failures": [],
                        "gpuTimings": [{"from": "a", "to": "b", "msec": 0.1}],
                    },
                },
                {
                    "type": "dlight-shadow-scenes",
                    "status": "passed",
                    "screenshots": screenshots,
                    "dlightShadow": {
                        "found": True,
                        "max": {"planned": 2, "renderLights": 2},
                        "scenes": {
                            str(shot["scene"]): {
                                "max": {"planned": 2, "renderLights": 2}
                            }
                            for shot in screenshots
                        },
                        "shadowManager": shadow_manager_summary(
                            [str(shot["scene"]) for shot in screenshots]
                        ),
                        "surfaceLightSpot": surface_light_spot_summary(surface_scene_ids),
                        "surfaceLightSpotLod": surface_light_spot_lod_summary(surface_scene_ids),
                        "csmShadows": csm_shadow_summary(csm_scene_ids),
                    },
                },
                {
                    "type": "timedemo",
                    "status": "passed",
                    "demo": "demo1",
                    "timedemoMetrics": {"fps": 100.0},
                },
            ],
        }

        failures = vk_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("stress-light-budget" in failure for failure in failures))

    def test_modern_gate_requires_shadow_manager_runtime_evidence(self) -> None:
        screenshots = [
            {
                "name": f"shot-{category}",
                "found": True,
                "shadowScene": True,
                "scene": category,
                "evidenceCategories": [category],
            }
            for category in vk_runtime_sweep.DLIGHT_SHADOW_EVIDENCE_CATEGORIES
        ]
        surface_scene_ids = list(vk_runtime_sweep.SURFACELIGHT_SPOT_EVIDENCE_CATEGORIES)
        csm_scene_ids = list(vk_runtime_sweep.CSM_SHADOW_EVIDENCE_CATEGORIES)
        manifest = {
            "gate": "vk-modern",
            "dryRun": False,
            "demos": ["demo1"],
            "runs": [
                {
                    "type": "map-screenshots",
                    "status": "passed",
                    "screenshots": [{"name": "shot", "found": True}],
                    "vkinfo": {
                        "failures": [],
                        "gpuTimings": [{"from": "a", "to": "b", "msec": 0.1}],
                    },
                },
                {
                    "type": "dlight-shadow-scenes",
                    "status": "passed",
                    "screenshots": screenshots,
                    "dlightShadow": {
                        "found": True,
                        "max": {"planned": 2, "renderLights": 2},
                        "scenes": {
                            str(shot["scene"]): {
                                "max": {"planned": 2, "renderLights": 2}
                            }
                            for shot in screenshots
                        },
                        "surfaceLightSpot": surface_light_spot_summary(surface_scene_ids),
                        "surfaceLightSpotLod": surface_light_spot_lod_summary(surface_scene_ids),
                        "csmShadows": csm_shadow_summary(csm_scene_ids),
                    },
                },
                {
                    "type": "timedemo",
                    "status": "passed",
                    "demo": "demo1",
                    "timedemoMetrics": {"fps": 100.0},
                },
            ],
        }

        failures = vk_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("shadow manager" in failure for failure in failures))

    def test_modern_gate_rejects_boolean_shadow_counters(self) -> None:
        screenshots = [
            {
                "name": f"shot-{category}",
                "found": True,
                "shadowScene": True,
                "scene": category,
                "evidenceCategories": [category],
            }
            for category in vk_runtime_sweep.DLIGHT_SHADOW_EVIDENCE_CATEGORIES
        ]
        surface_scene_ids = list(vk_runtime_sweep.SURFACELIGHT_SPOT_EVIDENCE_CATEGORIES)
        csm_scene_ids = list(vk_runtime_sweep.CSM_SHADOW_EVIDENCE_CATEGORIES)
        manifest = {
            "gate": "vk-modern",
            "dryRun": False,
            "demos": ["demo1"],
            "runs": [
                {
                    "type": "map-screenshots",
                    "status": "passed",
                    "screenshots": [{"name": "shot", "found": True}],
                    "vkinfo": {
                        "failures": [],
                        "gpuTimings": [{"from": "a", "to": "b", "msec": 0.1}],
                    },
                },
                {
                    "type": "dlight-shadow-scenes",
                    "status": "passed",
                    "screenshots": screenshots,
                    "dlightShadow": {
                        "found": True,
                        "max": {"planned": True, "renderLights": True},
                        "scenes": {
                            str(shot["scene"]): {
                                "max": {"planned": 2, "renderLights": 2}
                            }
                            for shot in screenshots
                        },
                        "shadowManager": shadow_manager_summary(
                            [str(shot["scene"]) for shot in screenshots]
                        ),
                        "surfaceLightSpot": surface_light_spot_summary(surface_scene_ids),
                        "surfaceLightSpotLod": surface_light_spot_lod_summary(surface_scene_ids),
                        "csmShadows": csm_shadow_summary(csm_scene_ids),
                    },
                },
                {
                    "type": "timedemo",
                    "status": "passed",
                    "demo": "demo1",
                    "timedemoMetrics": {"fps": 100.0},
                },
            ],
        }

        failures = vk_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(
            any("planning/render log samples" in failure for failure in failures)
        )

    def test_modern_gate_requires_surface_light_spot_runtime_evidence(self) -> None:
        screenshots = [
            {
                "name": f"shot-{category}",
                "found": True,
                "shadowScene": True,
                "scene": category,
                "evidenceCategories": [category],
            }
            for category in vk_runtime_sweep.DLIGHT_SHADOW_EVIDENCE_CATEGORIES
        ]
        csm_scene_ids = list(vk_runtime_sweep.CSM_SHADOW_EVIDENCE_CATEGORIES)
        manifest = {
            "gate": "vk-modern",
            "dryRun": False,
            "demos": ["demo1"],
            "runs": [
                {
                    "type": "map-screenshots",
                    "status": "passed",
                    "screenshots": [{"name": "shot", "found": True}],
                    "vkinfo": {
                        "failures": [],
                        "gpuTimings": [{"from": "a", "to": "b", "msec": 0.1}],
                    },
                },
                {
                    "type": "dlight-shadow-scenes",
                    "status": "passed",
                    "screenshots": screenshots,
                    "dlightShadow": {
                        "found": True,
                        "max": {"planned": 2, "renderLights": 2},
                        "scenes": {
                            str(shot["scene"]): {
                                "max": {"planned": 2, "renderLights": 2}
                            }
                            for shot in screenshots
                        },
                        "shadowManager": shadow_manager_summary(
                            [str(shot["scene"]) for shot in screenshots]
                        ),
                        "csmShadows": csm_shadow_summary(csm_scene_ids),
                    },
                },
                {
                    "type": "timedemo",
                    "status": "passed",
                    "demo": "demo1",
                    "timedemoMetrics": {"fps": 100.0},
                },
            ],
        }

        failures = vk_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("surfacelight spot manager" in failure for failure in failures))

    def test_modern_gate_requires_csm_runtime_evidence(self) -> None:
        screenshots = [
            {
                "name": f"shot-{category}",
                "found": True,
                "shadowScene": True,
                "scene": category,
                "evidenceCategories": [category],
            }
            for category in vk_runtime_sweep.DLIGHT_SHADOW_EVIDENCE_CATEGORIES
        ]
        surface_scene_ids = list(vk_runtime_sweep.SURFACELIGHT_SPOT_EVIDENCE_CATEGORIES)
        manifest = {
            "gate": "vk-modern",
            "dryRun": False,
            "demos": ["demo1"],
            "runs": [
                {
                    "type": "map-screenshots",
                    "status": "passed",
                    "screenshots": [{"name": "shot", "found": True}],
                    "vkinfo": {
                        "failures": [],
                        "gpuTimings": [{"from": "a", "to": "b", "msec": 0.1}],
                    },
                },
                {
                    "type": "dlight-shadow-scenes",
                    "status": "passed",
                    "screenshots": screenshots,
                    "dlightShadow": {
                        "found": True,
                        "max": {"planned": 2, "renderLights": 2},
                        "scenes": {
                            str(shot["scene"]): {
                                "max": {"planned": 2, "renderLights": 2}
                            }
                            for shot in screenshots
                        },
                        "shadowManager": shadow_manager_summary(
                            [str(shot["scene"]) for shot in screenshots]
                        ),
                        "surfaceLightSpot": surface_light_spot_summary(surface_scene_ids),
                        "surfaceLightSpotLod": surface_light_spot_lod_summary(surface_scene_ids),
                    },
                },
                {
                    "type": "timedemo",
                    "status": "passed",
                    "demo": "demo1",
                    "timedemoMetrics": {"fps": 100.0},
                },
            ],
        }

        failures = vk_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("CSM runtime" in failure for failure in failures))

    def test_modern_gate_requires_csm_shimmer_screenshot_diff_smoke(self) -> None:
        screenshots = [
            {
                "name": f"shot-{category}",
                "found": True,
                "shadowScene": True,
                "scene": category,
                "evidenceCategories": [category],
            }
            for category in vk_runtime_sweep.DLIGHT_SHADOW_EVIDENCE_CATEGORIES
        ]
        surface_scene_ids = list(vk_runtime_sweep.SURFACELIGHT_SPOT_EVIDENCE_CATEGORIES)
        csm_scene_ids = list(vk_runtime_sweep.CSM_SHADOW_EVIDENCE_CATEGORIES)
        manifest = {
            "gate": "vk-modern",
            "dryRun": False,
            "demos": ["demo1"],
            "runs": [
                {
                    "type": "map-screenshots",
                    "status": "passed",
                    "screenshots": [{"name": "shot", "found": True}],
                    "vkinfo": {
                        "failures": [],
                        "gpuTimings": [{"from": "a", "to": "b", "msec": 0.1}],
                    },
                },
                {
                    "type": "dlight-shadow-scenes",
                    "status": "passed",
                    "screenshots": screenshots,
                    "csmShimmerScreenshots": csm_shimmer_screenshot_summary("failed"),
                    "dlightShadow": {
                        "found": True,
                        "max": {"planned": 2, "renderLights": 2},
                        "scenes": {
                            str(shot["scene"]): {
                                "max": {"planned": 2, "renderLights": 2}
                            }
                            for shot in screenshots
                        },
                        "shadowManager": shadow_manager_summary(
                            [str(shot["scene"]) for shot in screenshots]
                        ),
                        "surfaceLightSpot": surface_light_spot_summary(surface_scene_ids),
                        "surfaceLightSpotLod": surface_light_spot_lod_summary(surface_scene_ids),
                        "csmShadows": csm_shadow_summary(csm_scene_ids),
                    },
                },
                {
                    "type": "timedemo",
                    "status": "passed",
                    "demo": "demo1",
                    "timedemoMetrics": {"fps": 100.0},
                },
            ],
        }

        failures = vk_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(
            any("CSM shimmer screenshot diff smoke" in failure for failure in failures)
        )

    def test_modern_gate_requires_combined_shadow_atlas_smoke(self) -> None:
        screenshots = [
            {
                "name": f"shot-{category}",
                "found": True,
                "shadowScene": True,
                "scene": category,
                "evidenceCategories": [category],
            }
            for category in vk_runtime_sweep.DLIGHT_SHADOW_EVIDENCE_CATEGORIES
        ]
        surface_scene_ids = list(vk_runtime_sweep.SURFACELIGHT_SPOT_EVIDENCE_CATEGORIES)
        csm_scene_ids = list(vk_runtime_sweep.CSM_SHADOW_EVIDENCE_CATEGORIES)
        manifest = {
            "gate": "vk-modern",
            "dryRun": False,
            "demos": ["demo1"],
            "runs": [
                {
                    "type": "map-screenshots",
                    "status": "passed",
                    "screenshots": [{"name": "shot", "found": True}],
                    "vkinfo": {
                        "failures": [],
                        "gpuTimings": [{"from": "a", "to": "b", "msec": 0.1}],
                    },
                },
                {
                    "type": "dlight-shadow-scenes",
                    "status": "passed",
                    "screenshots": screenshots,
                    "csmShimmerScreenshots": csm_shimmer_screenshot_summary(),
                    "combinedShadowAtlas": combined_shadow_atlas_summary("failed"),
                    "dlightShadow": {
                        "found": True,
                        "max": {"planned": 2, "renderLights": 2},
                        "scenes": {
                            str(shot["scene"]): {
                                "max": {"planned": 2, "renderLights": 2}
                            }
                            for shot in screenshots
                        },
                        "shadowManager": shadow_manager_summary(
                            [str(shot["scene"]) for shot in screenshots]
                        ),
                        "surfaceLightSpot": surface_light_spot_summary(surface_scene_ids),
                        "surfaceLightSpotLod": surface_light_spot_lod_summary(surface_scene_ids),
                        "csmShadows": csm_shadow_summary(csm_scene_ids),
                    },
                },
                {
                    "type": "timedemo",
                    "status": "passed",
                    "demo": "demo1",
                    "timedemoMetrics": {"fps": 100.0},
                },
            ],
        }

        failures = vk_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(
            any("combined shadow atlas smoke" in failure for failure in failures)
        )

    def test_modern_gate_requires_csm_fallback_smoke(self) -> None:
        screenshots = [
            {
                "name": f"shot-{category}",
                "found": True,
                "shadowScene": True,
                "scene": category,
                "evidenceCategories": [category],
            }
            for category in vk_runtime_sweep.DLIGHT_SHADOW_EVIDENCE_CATEGORIES
        ]
        surface_scene_ids = list(vk_runtime_sweep.SURFACELIGHT_SPOT_EVIDENCE_CATEGORIES)
        csm_scene_ids = list(vk_runtime_sweep.CSM_SHADOW_EVIDENCE_CATEGORIES)
        manifest = {
            "gate": "vk-modern",
            "dryRun": False,
            "demos": ["demo1"],
            "runs": [
                {
                    "type": "map-screenshots",
                    "status": "passed",
                    "screenshots": [{"name": "shot", "found": True}],
                    "vkinfo": {
                        "failures": [],
                        "gpuTimings": [{"from": "a", "to": "b", "msec": 0.1}],
                    },
                },
                {
                    "type": "dlight-shadow-scenes",
                    "status": "passed",
                    "screenshots": screenshots,
                    "csmShimmerScreenshots": csm_shimmer_screenshot_summary(),
                    "combinedShadowAtlas": combined_shadow_atlas_summary(),
                    "csmFallbacks": csm_fallback_summary("failed"),
                    "dlightShadow": {
                        "found": True,
                        "max": {"planned": 2, "renderLights": 2},
                        "scenes": {
                            str(shot["scene"]): {
                                "max": {"planned": 2, "renderLights": 2}
                            }
                            for shot in screenshots
                        },
                        "shadowManager": shadow_manager_summary(
                            [str(shot["scene"]) for shot in screenshots]
                        ),
                        "surfaceLightSpot": surface_light_spot_summary(surface_scene_ids),
                        "surfaceLightSpotLod": surface_light_spot_lod_summary(surface_scene_ids),
                        "csmShadows": csm_shadow_summary(csm_scene_ids),
                    },
                },
                {
                    "type": "timedemo",
                    "status": "passed",
                    "demo": "demo1",
                    "timedemoMetrics": {"fps": 100.0},
                },
            ],
        }

        failures = vk_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("CSM fallback smoke" in failure for failure in failures))


class VkRendererSourceTests(unittest.TestCase):
    def test_vulkan_cubemap_capture_batches_readback_and_subrect_copy_is_origin_relative(self) -> None:
        vk_c = (ROOT / "code" / "renderervk" / "vk.c").read_text(encoding="utf-8")
        backend = (ROOT / "code" / "renderervk" / "tr_backend.c").read_text(encoding="utf-8")

        self.assertIn("vk.cubemap_capture.face_stride * 6", vk_c)
        self.assertIn("copy.bufferOffset = (VkDeviceSize)faceIndex * vk.cubemap_capture.face_stride", vk_c)
        self.assertIn("vk.cubemap_capture.invalidate_pending = qfalse", vk_c)
        self.assertIn("vk_queue_wait_idle();\n\t\t\tRB_SaveCubemapScreenshots();", backend)
        self.assertIn("region.dstOffset.x = 0;", vk_c)
        self.assertIn("region.dstOffset.y = 0;", vk_c)
        self.assertNotIn("region.dstOffset = region.srcOffset;", vk_c)

    def test_vulkan_depth_fade_msaa_fallback_and_depth_resolve_scaffolding(self) -> None:
        vk_c = (ROOT / "code" / "renderervk" / "vk.c").read_text(encoding="utf-8")
        vk_h = (ROOT / "code" / "renderervk" / "vk.h").read_text(encoding="utf-8")

        self.assertIn("VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME", vk_c)
        self.assertIn("vkCreateRenderPass2KHR", vk_c)
        self.assertIn("vk_depth_fade_uses_depth_resolve", vk_c)
        self.assertIn("vk.depthStencilResolve", vk_c)
        self.assertIn("VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT", vk_c)
        self.assertIn("RENDER_PASS_MAIN_LOAD", vk_h)
        self.assertIn("vk_pipeline_render_pass_index", vk_c)
        self.assertIn("disabling MSAA so depth fade can use the single-sample depth copy path", vk_c)
        self.assertIn("vkSamples = VK_SAMPLE_COUNT_1_BIT", vk_c)
        self.assertIn("#define VK_DESC_DEPTH_FADE   VK_DESC_TEXTURE1", vk_h)

    def test_vulkan_depth_fade_matches_glx_single_texture_stage_rule(self) -> None:
        vk_shader = (ROOT / "code" / "renderervk" / "tr_shader.c").read_text(encoding="utf-8")
        vk_shade = (ROOT / "code" / "renderervk" / "tr_shade.c").read_text(encoding="utf-8")
        gen_frag = (ROOT / "code" / "renderervk" / "shaders" / "gen_frag.tmpl").read_text(encoding="utf-8")

        self.assertIn("pStage->bundle[1].image[0] == NULL && !pStage->depthFragment", vk_shader)
        self.assertIn("pStage->bundle[1].image[0] == NULL", vk_shade)
        self.assertIn("#define USE_DEPTH_FADE", gen_frag)
        self.assertIn("layout(set = 2, binding = 0) uniform sampler2D depth_texture", gen_frag)

    def test_vulkan_world_cel_outline_uses_opaque_cutoff(self) -> None:
        backend = (ROOT / "code" / "renderervk" / "tr_backend.c").read_text(encoding="utf-8")

        self.assertIn("if ( shader->sort > SS_OPAQUE ) {", backend)
        self.assertNotIn("if ( shader->sort >= SS_BLEND0 ) {", backend)

    def test_vulkan_post_gamma_contract_covers_modern_sdr_path(self) -> None:
        gamma_frag = (ROOT / "code" / "renderervk" / "shaders" / "gamma.frag").read_text(encoding="utf-8")
        vk_backend = (ROOT / "code" / "renderervk" / "vk.c").read_text(encoding="utf-8")
        tr_cmds = (ROOT / "code" / "renderervk" / "tr_cmds.c").read_text(encoding="utf-8")
        tr_init = (ROOT / "code" / "renderervk" / "tr_init.c").read_text(encoding="utf-8")
        vk_gates = (ROOT / "docs" / "fnql" / "VK_RC_GATES.md").read_text(encoding="utf-8")

        self.assertIn("layout(constant_id = 0) const float gamma = 1.0;", gamma_frag)
        self.assertIn("layout(constant_id = 1) const float obScale = 2.0;", gamma_frag)
        self.assertIn("if ( sceneLinearMode != 0 )", gamma_frag)
        self.assertIn("color = max(base * obScale * max(toneMapExposure, 0.0), vec3(0.0));", gamma_frag)
        self.assertIn("color = pow(max(base, vec3(0.0)), vec3(gamma)) * obScale;", gamma_frag)
        self.assertIn("frag_spec_data.gamma = 1.0 / (r_gamma->value);", vk_backend)
        self.assertIn("frag_spec_data.overbright = (float)(1 << tr.overbrightBits);", vk_backend)
        self.assertIn("frag_spec_data.scene_linear_mode = ( r_hdr && r_hdr->integer > 0 ) ? 1 : 0;", vk_backend)
        self.assertIn("frag_spec_data.output_color_space = ( program_index == 0 && vk.hdrDisplayActive )", vk_backend)
        self.assertIn("if ( r_gamma->modified )", tr_cmds)
        self.assertIn("vk_update_post_process_pipelines();", tr_cmds)
        self.assertIn("post gamma: domain %s, shader %s, r_gamma", tr_init)
        self.assertIn("post-gamma", vk_gates)


if __name__ == "__main__":
    unittest.main()
