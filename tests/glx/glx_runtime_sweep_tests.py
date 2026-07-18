from __future__ import annotations

import argparse
import copy
import importlib.util
import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SWEEP_PATH = ROOT / "scripts" / "glx_runtime_sweep.py"
PROMOTION_PATH = ROOT / "scripts" / "glx_promotion.py"
RELEASE_PATH = ROOT / "scripts" / "release.py"
FEATURE_MATRIX_PATH = ROOT / "docs" / "fnql" / "GLX_FEATURE_MATRIX.md"
COLORSPACE_AUDIT_PATH = ROOT / "docs" / "fnql" / "GLX_COLORSPACE_AUDIT.md"
FEATURE_MATRIX_ALLOWED_STATUSES = {"covered", "partially covered", "missing"}
FEATURE_MATRIX_REQUIRED_IDS = {
    "CORE-ABI",
    "CORE-SWITCH",
    "CORE-PASS-ORDER",
    "CORE-TIERS",
    "CORE-OWNERSHIP",
    "WORLD-BSP",
    "WORLD-LIGHTMAPS",
    "WORLD-FOG",
    "WORLD-SKY",
    "WORLD-PORTALS",
    "MATERIAL-STAGES",
    "MATERIAL-TEXMODS",
    "MATERIAL-TCGEN",
    "MATERIAL-VIDEOMAP",
    "MATERIAL-SCREENMAP",
    "MATERIAL-DEPTHFRAG",
    "DYN-ENTITIES",
    "DYN-PARTICLES",
    "DYN-BEAMS",
    "DYN-DLIGHTS",
    "DYN-SHADOWS-STENCIL",
    "DYN-SHADOWS-PLANAR",
    "DYN-CEL",
    "DYN-OUTLINE",
    "POST-FBO",
    "POST-BLOOM1",
    "POST-BLOOM2",
    "POST-GAMMA",
    "POST-GREYSCALE",
    "POST-RENDERSCALE",
    "POST-MSAA",
    "POST-SSAA",
    "POST-HDR-PRECISION",
    "COLOR-SCENE-LINEAR",
    "COLOR-TONEMAP-GRADE",
    "OUTPUT-SDR",
    "OUTPUT-HDR-HARDWARE",
    "UI-HUD",
    "UI-CINEMATICS",
    "CAPTURE-SCREENSHOTS",
    "CAPTURE-CUBEMAPS",
    "DEMO-PLAYBACK",
    "MODERN-NORMALMAP",
    "MODERN-SPECULAR",
    "MODERN-PARALLAX",
    "MODERN-CUBEMAP-LIGHTING",
    "MODERN-SUNLIGHT",
    "MODERN-SHADOWMAPS",
    "MODERN-SSAO",
    "PERF-STATIC-CACHE",
    "PERF-STATIC-SHIPPED",
    "PERF-STATIC-MDI",
    "PERF-DYNAMIC-STREAM",
    "PERF-GPU-TIMING",
    "DEBUG-DIAGNOSTICS",
    "PROOF-RUNTIME",
}

spec = importlib.util.spec_from_file_location("glx_runtime_sweep", SWEEP_PATH)
assert spec is not None
glx_runtime_sweep = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(glx_runtime_sweep)
sys.path.insert(0, str(ROOT / "scripts"))

promotion_spec = importlib.util.spec_from_file_location("glx_promotion", PROMOTION_PATH)
assert promotion_spec is not None
glx_promotion = importlib.util.module_from_spec(promotion_spec)
assert promotion_spec.loader is not None
promotion_spec.loader.exec_module(glx_promotion)

release_spec = importlib.util.spec_from_file_location("fnql_release", RELEASE_PATH)
assert release_spec is not None
fnql_release = importlib.util.module_from_spec(release_spec)
assert release_spec.loader is not None
release_spec.loader.exec_module(fnql_release)


def parse_runtime_glx_profiles() -> dict[str, dict[str, str]]:
    source = (ROOT / "code" / "rendererglx" / "glx_module.cpp").read_text(encoding="utf-8")
    match = re.search(
        r"static const ProfileCvarSetting GLX_PROFILE_CVARS\[\] = \{(?P<body>.*?)\n\};",
        source,
        re.DOTALL,
    )
    assert match is not None
    profiles: dict[str, dict[str, str]] = {
        "off": {},
        "rc": {},
        "stress": {},
    }

    for name, off, rc, stress in re.findall(
        r'\{\s*"([^"]+)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\}',
        match.group("body"),
    ):
        profiles["off"][name] = off
        profiles["rc"][name] = rc
        profiles["stress"][name] = stress

    assert profiles["rc"]
    return profiles


def locked_pass_schedule_latest() -> dict[str, object]:
    return {
        "tier": "GL2X",
        "productTier": "GL2X",
        "passScheduleValid": 1,
        "passScheduleCount": glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE_COUNT,
        "passScheduleHash": glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE_HASH,
        "passScheduleOrder": glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE,
    }


def screenshot_histogram() -> dict[str, object]:
    return {
        "status": "passed",
        "width": 1,
        "height": 1,
        "pixels": 1,
        "meanLuma": 128.0,
        "meanRed": 128.0,
        "meanGreen": 128.0,
        "meanBlue": 128.0,
        "p01Luma": 128,
        "p50Luma": 128,
        "p99Luma": 128,
        "p50Red": 128,
        "p50Green": 128,
        "p50Blue": 128,
        "clippedBlackRatio": 0.0,
        "clippedWhiteRatio": 0.0,
    }


def projected_dlight_shader_metrics(
    *,
    world_inputs: int = 1,
    dynamic_inputs: int = 1,
    world_executable: int = 1,
    dynamic_executable: int = 1,
    resource_promotions: int = 1,
    resource_executable: int = 1,
    resource_records: int = 4,
) -> dict[str, object]:
    return {
        "dlightProjectedShaderInputs": {
            "inputs": world_inputs + dynamic_inputs,
            "records": world_inputs + dynamic_inputs,
            "world": world_inputs,
            "dynamic": dynamic_inputs,
            "programmable": world_inputs + dynamic_inputs,
            "fallback": 0,
            "invalid": 0,
        },
        "dlightProjectedShaderUniforms": {
            "attempts": 2,
            "binds": 2,
            "failures": 0,
            "records": 2,
            "truncated": 0,
            "executable": world_executable + dynamic_executable,
            "suppressed": 0,
            "worldExecutable": world_executable,
            "worldBinds": max(1, world_inputs),
            "dynamicExecutable": dynamic_executable,
            "dynamicBinds": max(1, dynamic_inputs),
            "limitSuppressed": 0,
            "limit": 3,
        },
        "dlightProjectedShaderResource": {
            "attempts": 1 if resource_promotions else 0,
            "binds": 1 if resource_promotions else 0,
            "executable": resource_executable,
            "suppressed": 0,
            "promotions": resource_promotions,
            "failures": 0,
            "records": resource_records,
            "worldExecutable": 0,
            "dynamicExecutable": resource_executable,
            "binding": 5,
        },
    }


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


def surface_light_spot_sample() -> dict[str, int]:
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
        "surfaceSpotConeInnerMin": 0,
        "surfaceSpotConeInnerMax": 0,
        "surfaceSpotConeOuterMin": 45,
        "surfaceSpotConeOuterMax": 65,
        "surfaceSpotAllocated": 2,
        "surfaceSpotAtlasWidth": 1024,
        "surfaceSpotAtlasHeight": 1024,
        "surfaceSpotAtlasTileSize": 256,
        "surfaceSpotAtlasFill": 12,
        "surfaceSpotRejectedWeak": 1,
        "surfaceSpotRejectedOffView": 2,
        "surfaceSpotRejectedBudget": 1,
        "surfaceSpotRejectedMalformed": 1,
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
    sample = surface_light_spot_sample()
    summary = glx_runtime_sweep.surface_light_spot_lod_summary([sample])
    summary["scenes"] = {
        scene_id: glx_runtime_sweep.surface_light_spot_lod_summary([sample])
        for scene_id in scene_ids
    }
    return summary


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
            "maxRms": glx_runtime_sweep.CSM_SHIMMER_SCREENSHOT_MAX_RMS,
            "maxChangedPixelRatio": (
                glx_runtime_sweep.CSM_SHIMMER_SCREENSHOT_MAX_CHANGED_PIXEL_RATIO
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


def sdr_capture_policy() -> dict[str, object]:
    return dict(glx_runtime_sweep.GLX_SCREENSHOT_CAPTURE_POLICY_CONTRACT)


def color_diagnostics_metrics() -> dict[str, object]:
    return {
        "colorPipeline": {
            "space": "scene-linear",
            "transfer": "sdr-srgb",
            "toneMap": "aces-fitted",
            "grade": "none",
            "exposure": 1.0,
            "paperWhite": 203.0,
            "maxOutput": 203.0,
        },
        "colorAudit": {
            "srgbDecode": 1,
            "srgbRequested": 1,
            "srgbAvailable": 1,
            "framebufferSrgb": 0,
            "framebufferRequested": 1,
            "framebufferAvailable": 1,
            "capture": "sdr-srgb",
            "captureRequest": "sdr-srgb",
            "captureHdrAware": 0,
            "captureSupported": 1,
            "targetFloat": 1,
            "finalEncode": "shader-srgb",
            "contract": 1,
        },
        "outputBackend": {
            "request": "auto",
            "selected": "sdr-srgb",
            "native": "sdr-srgb",
            "hardware": 0,
            "experimental": 0,
            "displayHdr": 0,
            "headroom": 1.0,
            "sdrWhite": 203.0,
            "displayMax": 203.0,
            "icc": 0,
            "iccBytes": 0,
        },
        "textureAudit": {
            "srgb": 4,
            "srgbDecode": 4,
            "linear": 2,
            "linearDecode": 0,
            "data": 2,
            "dataDecode": 0,
            "unknown": 0,
            "unknownDecode": 0,
            "missingSrgbDecode": 0,
            "unexpectedDecode": 0,
        },
        "targetFormat": {
            "renderWidth": 512,
            "renderHeight": 384,
            "captureWidth": 512,
            "captureHeight": 384,
            "windowWidth": 640,
            "windowHeight": 480,
            "internalFormat": 0x881A,
            "textureFormat": 0x1908,
            "textureType": 0x140B,
        },
        "postprocess": {
            "fboRequested": 1,
            "fboReady": 1,
            "fboPrograms": 1,
            "fboFramebuffer": 1,
            "fboAttempts": 1,
            "fboFailed": 0,
            "lastOutput": "gamma-blit",
        },
        "postprocessControls": {
            "sceneLinearHdr": 1,
            "precision": 16,
            "renderScale": 1,
            "bloom": 2,
            "msaa": 0,
            "supersample": 0,
            "windowAdjusted": 1,
            "greyscale": 1.0,
        },
        "postprocessFrames": {
            "frames": 3,
            "bloomFinal": 1,
            "gammaDirect": 0,
            "gammaBlit": 3,
            "minimizedOutput": 0,
            "screenshots": 1,
        },
        "postprocessFrameFeatures": {
            "bloomAvailable": 1,
            "sceneLinear": 1,
            "toneMapped": 1,
            "graded": 1,
            "renderScale": 1,
            "greyscale": 1,
            "windowAdjusted": 1,
            "minimized": 0,
        },
        "gl12Support": {
            "lightmaps": 1,
            "multitexture": 1,
            "fog": 1,
            "sprites": 1,
            "beams": 1,
            "dynamicLights": 1,
            "stencilShadows": 1,
            "screenshots": 1,
            "demos": 1,
        },
        "gl2xSupport": {
            "commonMaterials": 1,
            "dynamicEntities": 1,
            "lightmaps": 1,
            "multitexture": 1,
            "fog": 1,
            "sprites": 1,
            "beams": 1,
            "screenshots": 1,
            "demos": 1,
        },
        "staticWorld": {
            "rendererEnabled": 1,
            "arenaReady": 1,
            "packetBatchEnabled": 1,
            "packetBatchBatches": 4,
            "indirectBufferReady": 1,
            "glErrors": 0,
            "failures": 0,
        },
        "streamDraw": {
            "draws": 3,
            "attempts": 3,
            "indexes": 1024,
            "multitexture": 2,
            "fog": 2,
            "depthFragment": 1,
            "texMods": 2,
            "environment": 1,
            "dynamicLights": 1,
            "screenMaps": 0,
            "videoMaps": 0,
            "shadows": 2,
            "beams": 0,
            "postprocess": 0,
            "fallbacks": 0,
            "skips": 0,
        },
        "streamCategory": {
            "entityDraws": 2,
            "entityAttempts": 2,
            "entityObserved": 1,
            "entityFallbacks": 0,
            "particleDraws": 0,
            "particleAttempts": 0,
            "particleObserved": 0,
            "particleFallbacks": 0,
            "polyDraws": 0,
            "polyAttempts": 0,
            "polyObserved": 0,
            "polyFallbacks": 0,
            "markDraws": 0,
            "markAttempts": 0,
            "markObserved": 0,
            "markFallbacks": 0,
            "weaponDraws": 1,
            "weaponAttempts": 1,
            "weaponObserved": 1,
            "weaponFallbacks": 0,
            "uiDraws": 0,
            "uiAttempts": 0,
            "uiObserved": 0,
            "uiFallbacks": 0,
            "beamDraws": 0,
            "beamAttempts": 0,
            "beamObserved": 0,
            "beamFallbacks": 0,
            "dlightDraws": 1,
            "dlightAttempts": 1,
            "dlightObserved": 1,
            "dlightFallbacks": 0,
            "specialDraws": 0,
            "specialAttempts": 0,
            "specialObserved": 0,
            "specialFallbacks": 0,
        },
        "streamRole": {
            "genericDraws": 3,
            "genericAttempts": 3,
            "genericFallbacks": 0,
            "dlightDraws": 1,
            "dlightAttempts": 1,
            "dlightFallbacks": 0,
            "shadowDraws": 2,
            "shadowAttempts": 2,
            "shadowFallbacks": 0,
            "beamDraws": 0,
            "beamAttempts": 0,
            "beamFallbacks": 0,
            "postDraws": 0,
            "postAttempts": 0,
            "postFallbacks": 0,
        },
        "streamDrawSkip": {
            "program": 0,
        },
        "materialParameters": {
            "blocks": 4,
            "invalid": 0,
            "hash": 0x00F00D,
            "sort": 0,
            "passes": 2,
            "features": 0x0C23,
            "flags": 0x0C23,
            "state": 0x30,
            "rgbGen": 2,
            "rgbWave": 0,
            "alphaGen": 1,
            "alphaWave": 0,
            "tcGen0": 3,
            "tcGen1": 2,
        },
        "material": {
            "enabled": 1,
            "ready": 1,
            "attempts": 24,
            "compile": 0,
            "link": 0,
            "precacheFailures": 0,
            "precacheAttempts": 24,
            "bind": 0,
        },
        "materialFallbacks": {
            "unsupported": 0,
            "disabled": 0,
            "notReady": 0,
            "full": 0,
            "discarded": 0,
        },
        "materialCompilerPlans": {
            "compiled": 12,
            "unsupported": 0,
            "lastUnsupported": 0,
            "lastUnsupportedReason": "none",
        },
        "materialLastKey": {
            "name": "test",
            "flags": 0x0C23,
            "state": 0x30,
            "rgbGen": 2,
            "rgbWave": 0,
            "alphaGen": 1,
            "alphaWave": 0,
            "tcGen0": 3,
            "tcGen1": 2,
            "texMods0": 1,
            "texMods1": 0,
            "combine": 1,
            "fogPass": 0,
        },
        "materialLanguage": {
            "flags": 0x0C23,
            "state": 0x30,
            "texModMask0": 0x8,
            "texModMask1": 0,
            "texModSequence0": 0x3,
            "texModSequence1": 0,
            "texModWaveFuncs0": 0,
            "texModWaveFuncs1": 0,
            "fogAdjust": 0,
        },
        "streamMaterialGate": {
            "multitextureEnabled": 1,
            "multitextureAccepted": 2,
            "multitextureRejected": 0,
            "depthFragmentEnabled": 1,
            "depthFragmentAccepted": 1,
            "depthFragmentRejected": 0,
            "texModEnabled": 1,
            "texModAccepted": 2,
            "texModRejected": 0,
            "environmentEnabled": 1,
            "environmentAccepted": 1,
            "environmentRejected": 0,
            "dynamicLightEnabled": 1,
            "dynamicLightAccepted": 1,
            "dynamicLightRejected": 0,
            "screenMapEnabled": 0,
            "screenMapAccepted": 0,
            "screenMapRejected": 0,
            "videoMapEnabled": 0,
            "videoMapAccepted": 0,
            "videoMapRejected": 0,
        },
        "colorFrame": {
            "samples": 1,
            "latest": {
                "frame": 1,
                "backend": "sdr-srgb",
                "space": "scene-linear",
                "transfer": "sdr-srgb",
                "exposure": 1.0,
                "paperWhiteNits": 203.0,
                "maxOutputNits": 203.0,
                "srgbDecode": True,
                "framebufferSrgb": False,
                "internalFormat": "0x881a",
                "textureFormat": "0x1908",
                "textureType": "0x140b",
                "sceneTargetFloat": True,
                "shaderSrgbEncode": True,
                "contractValid": True,
            },
        },
    }


def stress_material_diagnostics_metrics() -> dict[str, object]:
    metrics = copy.deepcopy(color_diagnostics_metrics())
    material_flags = (
        0x0C23
        | glx_runtime_sweep.GLX_MATERIAL_STAGE_FLAGS["animatedImage"]
        | glx_runtime_sweep.GLX_MATERIAL_STAGE_FLAGS["screenMap"]
        | glx_runtime_sweep.GLX_MATERIAL_STAGE_FLAGS["videoMap"]
    )
    metrics["streamDraw"].update(  # type: ignore[index, union-attr]
        {
            "screenMaps": 1,
            "videoMaps": 1,
            "beams": 3,
            "shadows": 2,
        }
    )
    metrics["streamCategory"].update(  # type: ignore[index, union-attr]
        {
            "particleDraws": 2,
            "particleAttempts": 2,
            "particleObserved": 1,
            "polyDraws": 2,
            "polyAttempts": 2,
            "polyObserved": 1,
            "markDraws": 1,
            "markAttempts": 1,
            "markObserved": 1,
            "beamDraws": 3,
            "beamAttempts": 3,
            "beamObserved": 1,
        }
    )
    metrics["materialParameters"].update(  # type: ignore[index, union-attr]
        {
            "features": material_flags,
            "flags": material_flags,
        }
    )
    metrics["materialLastKey"].update(  # type: ignore[index, union-attr]
        {
            "flags": material_flags,
        }
    )
    metrics["materialLanguage"].update(  # type: ignore[index, union-attr]
        {
            "flags": material_flags,
        }
    )
    metrics["materialStageFlags"] = {
        "multitexture": 2,
        "depthFragment": 1,
        "blend": 1,
        "alphaTest": 1,
        "depthWrite": 1,
        "lightmap": 2,
        "animatedImage": 1,
        "videoMap": 1,
        "screenMap": 1,
        "dynamicLightMap": 0,
        "texMod": 2,
        "environment": 1,
        "st0": 2,
        "st1": 1,
    }
    metrics["streamMaterialGate"].update(  # type: ignore[index, union-attr]
        {
            "screenMapEnabled": 1,
            "screenMapAccepted": 1,
            "screenMapRejected": 0,
            "videoMapEnabled": 1,
            "videoMapAccepted": 1,
            "videoMapRejected": 0,
        }
    )
    return metrics


def color_contracts() -> dict[str, object]:
    return glx_runtime_sweep.color_contract_manifest()


def image_evidence_manifest() -> dict[str, object]:
    return {
        "version": glx_runtime_sweep.GLX_IMAGE_EVIDENCE_VERSION,
        "requiredSidecars": list(glx_runtime_sweep.GLX_IMAGE_EVIDENCE_SIDECARS),
        "shaderReferenceRamps": [
            {
                "rowId": str(row["id"]),
                "path": f"image-evidence/{row['id']}.reference-ramp.png",
                "width": glx_runtime_sweep.GLX_SHADER_REFERENCE_RAMP_WIDTH,
                "height": glx_runtime_sweep.GLX_SHADER_REFERENCE_RAMP_HEIGHT,
                "rampRows": list(glx_runtime_sweep.GLX_SHADER_REFERENCE_RAMP_ROWS),
                "histogram": screenshot_histogram(),
                "histogramPath": f"image-evidence/{row['id']}.histogram.json",
                "falseColorPath": f"image-evidence/{row['id']}.luma-falsecolor.png",
                "exposureFalseColorPath": f"image-evidence/{row['id']}.exposure-falsecolor.png",
            }
            for row in glx_runtime_sweep.GLX_COLOR_SWEEP_MATRIX
        ],
    }


def color_diagnostics_metrics_for_row(row: dict[str, object]) -> dict[str, object]:
    metrics = color_diagnostics_metrics()
    expect = row.get("expect", {})
    if isinstance(expect, dict):
        metrics["colorPipeline"]["space"] = expect.get("space", "scene-linear")  # type: ignore[index]
        metrics["colorPipeline"]["transfer"] = expect.get("transfer", "sdr-srgb")  # type: ignore[index]
        metrics["colorPipeline"]["toneMap"] = expect.get("toneMap", "aces-fitted")  # type: ignore[index]
        metrics["colorPipeline"]["exposure"] = expect.get("exposure", 1.0)  # type: ignore[index]
        metrics["colorAudit"]["targetFloat"] = expect.get("sceneTargetFloat", 1)  # type: ignore[index]
        metrics["colorAudit"]["finalEncode"] = expect.get("finalEncode", "shader-srgb")  # type: ignore[index]
        metrics["colorAudit"]["contract"] = expect.get("contract", 1)  # type: ignore[index]
        metrics["colorAudit"]["framebufferSrgb"] = expect.get("framebufferSrgb", 0)  # type: ignore[index]
        metrics["colorAudit"]["srgbDecode"] = expect.get("srgbDecode", 1)  # type: ignore[index]
        metrics["colorAudit"]["srgbRequested"] = expect.get("srgbRequested", 1)  # type: ignore[index]
        metrics["outputBackend"]["request"] = expect.get("outputRequest", "sdr-srgb")  # type: ignore[index]
        if "internalFormat" in expect:
            metrics["targetFormat"]["internalFormat"] = int(str(expect["internalFormat"]), 16)  # type: ignore[index]
        if "textureFormat" in expect:
            metrics["targetFormat"]["textureFormat"] = int(str(expect["textureFormat"]), 16)  # type: ignore[index]
        if "textureType" in expect:
            metrics["targetFormat"]["textureType"] = int(str(expect["textureType"]), 16)  # type: ignore[index]
        color_frame = metrics["colorFrame"]["latest"]  # type: ignore[index]
        color_frame["space"] = metrics["colorPipeline"]["space"]  # type: ignore[index]
        color_frame["transfer"] = metrics["colorPipeline"]["transfer"]  # type: ignore[index]
        color_frame["exposure"] = metrics["colorPipeline"]["exposure"]  # type: ignore[index]
        color_frame["sceneTargetFloat"] = bool(metrics["colorAudit"]["targetFloat"])  # type: ignore[index]
        color_frame["shaderSrgbEncode"] = metrics["colorAudit"]["finalEncode"] == "shader-srgb"  # type: ignore[index]
        color_frame["contractValid"] = bool(metrics["colorAudit"]["contract"])  # type: ignore[index]
        color_frame["framebufferSrgb"] = bool(metrics["colorAudit"]["framebufferSrgb"])  # type: ignore[index]
        color_frame["srgbDecode"] = bool(metrics["colorAudit"]["srgbDecode"])  # type: ignore[index]
        color_frame["internalFormat"] = f"0x{int(metrics['targetFormat']['internalFormat']):04x}"  # type: ignore[index]
        color_frame["textureFormat"] = f"0x{int(metrics['targetFormat']['textureFormat']):04x}"  # type: ignore[index]
        color_frame["textureType"] = f"0x{int(metrics['targetFormat']['textureType']):04x}"  # type: ignore[index]
    return metrics


def locked_performance_sample(row: dict[str, object] | None = None) -> dict[str, object]:
    latest = locked_pass_schedule_latest()
    metrics = color_diagnostics_metrics_for_row(row) if row is not None else color_diagnostics_metrics()
    color_pipeline = metrics["colorPipeline"]
    color_audit = metrics["colorAudit"]
    target_format = metrics["targetFormat"]
    post_controls = metrics["postprocessControls"]
    post_frames = metrics["postprocessFrames"]
    post_features = metrics["postprocessFrameFeatures"]
    color_frame = metrics["colorFrame"]["latest"]  # type: ignore[index]
    latest.update(
        {
            "colorSpace": color_pipeline["space"],
            "outputTransfer": color_pipeline["transfer"],
            "toneMap": color_pipeline["toneMap"],
            "toneMapExposure": 1.0,
            "paperWhiteNits": 203.0,
            "maxOutputNits": color_pipeline["maxOutput"],
            "colorSrgbDecode": color_audit["srgbDecode"],
            "colorSrgbRequested": color_audit["srgbRequested"],
            "colorFramebufferSrgb": color_audit["framebufferSrgb"],
            "colorSceneTargetFloat": color_audit["targetFloat"],
            "colorFinalEncode": color_audit["finalEncode"],
            "colorOutputContract": color_audit["contract"],
            "captureColorSpace": color_audit["capture"],
            "capturePolicyRequest": color_audit["captureRequest"],
            "captureHdrAware": color_audit["captureHdrAware"],
            "capturePolicySupported": color_audit["captureSupported"],
            "outputBackendRequest": metrics["outputBackend"]["request"],  # type: ignore[index]
            "outputBackendSelected": "sdr-srgb",
            "textureAuditSrgb": 4,
            "textureAuditSrgbDecode": 4,
            "textureAuditMissingSrgbDecode": 0,
            "textureAuditUnexpectedDecode": 0,
            "targetInternalFormat": target_format["internalFormat"],
            "targetTextureFormat": target_format["textureFormat"],
            "targetTextureType": target_format["textureType"],
            "targetRenderWidth": target_format["renderWidth"],
            "targetRenderHeight": target_format["renderHeight"],
            "targetCaptureWidth": target_format["captureWidth"],
            "targetCaptureHeight": target_format["captureHeight"],
            "targetWindowWidth": target_format["windowWidth"],
            "targetWindowHeight": target_format["windowHeight"],
            "fbo": "ready",
            "postprocessRenderScaleMode": post_controls["renderScale"],
            "postprocessGreyscale": post_controls["greyscale"],
            "postprocessWindowAdjusted": post_controls["windowAdjusted"],
            "postprocessFrames": post_frames["frames"],
            "postprocessScreenshots": post_frames["screenshots"],
            "postprocessMinimizedOutput": post_frames["minimizedOutput"],
            "postprocessFeatureRenderScale": post_features["renderScale"],
            "postprocessFeatureGreyscale": post_features["greyscale"],
            "postprocessFeatureWindowAdjusted": post_features["windowAdjusted"],
            "postprocessFeatureMinimized": post_features["minimized"],
            "postOutputMode": "glx-owned",
            "postOutputPostNodes": 2,
            "postOutputOutputs": 1,
            "postOutputExecutableNodes": 2,
            "postOutputExecutableOutputs": 1,
            "postOutputLegacyFallback": 0,
            "postOutputPostHash": 0x10203,
            "postOutputOutputHash": 0x40506,
            "postOutputPlanHash": 0x70809,
            "postOutputFallbackMask": 0,
            "colorFrameSamples": 1,
            "colorFrameBackend": color_frame["backend"],
            "colorFrameSpace": color_frame["space"],
            "colorFrameTransfer": color_frame["transfer"],
            "colorFrameExposure": color_frame["exposure"],
            "colorFramePaperWhiteNits": color_frame["paperWhiteNits"],
            "colorFrameMaxOutputNits": color_frame["maxOutputNits"],
            "colorFrameSrgbDecode": 1 if color_frame["srgbDecode"] else 0,
            "colorFrameFramebufferSrgb": 1 if color_frame["framebufferSrgb"] else 0,
            "colorFrameInternalFormat": color_frame["internalFormat"],
            "colorFrameTextureFormat": color_frame["textureFormat"],
            "colorFrameTextureType": color_frame["textureType"],
            "colorFrameSceneTargetFloat": 1 if color_frame["sceneTargetFloat"] else 0,
            "colorFrameShaderSrgbEncode": 1 if color_frame["shaderSrgbEncode"] else 0,
            "colorFrameContractValid": 1 if color_frame["contractValid"] else 0,
            "staticDrawAttempts": 4,
            "staticDrawIndexes": 2048,
            "staticDrawFallbacks": 0,
            "staticDrawPacketFull": 3,
            "staticDrawPacketPartial": 1,
            "staticDrawPacketMisses": 0,
            "staticQueuePacketMisses": 0,
            "staticPacketLookupMisses": 0,
            "staticMdiErrors": 0,
            "draws": 3,
            "streamDrawAttempts": 3,
            "streamDrawIndexes": 1024,
            "streamDrawFog": 2,
            "streamDrawMultitexture": 2,
            "streamDrawDepthFragment": 1,
            "streamDrawTexMods": 2,
            "streamDrawEnvironment": 1,
            "streamDrawDynamicLights": 1,
            "streamDrawScreenMaps": 0,
            "streamDrawVideoMaps": 0,
            "streamDrawShadows": 2,
            "streamDrawBeams": 0,
            "streamDrawPostProcess": 0,
            "streamDrawFallbacks": 0,
            "streamDrawSkips": 0,
            "streamCategoryEntityDraws": 2,
            "streamCategoryEntityAttempts": 2,
            "streamCategoryParticleDraws": 0,
            "streamCategoryParticleAttempts": 0,
            "streamCategoryPolyDraws": 0,
            "streamCategoryPolyAttempts": 0,
            "streamCategoryMarkDraws": 0,
            "streamCategoryMarkAttempts": 0,
            "streamCategoryWeaponDraws": 1,
            "streamCategoryWeaponAttempts": 1,
            "streamCategoryUiDraws": 0,
            "streamCategoryUiAttempts": 0,
            "streamCategoryBeamDraws": 0,
            "streamCategoryBeamAttempts": 0,
            "streamCategoryDlightDraws": 1,
            "streamCategoryDlightAttempts": 1,
            "streamCategorySpecialDraws": 0,
            "streamCategorySpecialAttempts": 0,
            "streamRoleGenericDraws": 3,
            "streamRoleGenericAttempts": 3,
            "streamRoleGenericFallbacks": 0,
            "streamRoleDlightDraws": 1,
            "streamRoleDlightAttempts": 1,
            "streamRoleDlightFallbacks": 0,
            "streamRoleShadowDraws": 2,
            "streamRoleShadowAttempts": 2,
            "streamRoleShadowFallbacks": 0,
            "streamRoleBeamDraws": 0,
            "streamRoleBeamAttempts": 0,
            "streamRoleBeamFallbacks": 0,
            "streamRolePostDraws": 0,
            "streamRolePostAttempts": 0,
            "streamRolePostFallbacks": 0,
            "renderIRRoleGenericDraws": 3,
            "renderIRRoleGenericIndexes": 1024,
            "renderIRRoleGenericVertices": 0,
            "renderIRRoleDlightDraws": 1,
            "renderIRRoleDlightIndexes": 64,
            "renderIRRoleDlightVertices": 0,
            "renderIRRoleShadowDraws": 2,
            "renderIRRoleShadowIndexes": 128,
            "renderIRRoleShadowVertices": 0,
            "renderIRRoleBeamDraws": 0,
            "renderIRRoleBeamIndexes": 0,
            "renderIRRoleBeamVertices": 0,
            "renderIRRolePostDraws": 0,
            "renderIRRolePostIndexes": 0,
            "renderIRRolePostVertices": 0,
            "renderIRPassDlightDraws": 1,
            "renderIRPassDlightIndexes": 64,
            "renderIRPassDlightVertices": 0,
            "renderIRPassSceneDraws": 3,
            "renderIRPassSceneIndexes": 1024,
            "renderIRPassSceneVertices": 0,
            "renderIRPassPostDraws": 0,
            "renderIRPassPostIndexes": 0,
            "renderIRPassPostVertices": 0,
            "renderIRPassOtherDraws": 0,
            "renderIRPassOtherIndexes": 0,
            "renderIRPassOtherVertices": 0,
            "dlightScissorActive": 1,
            "dlightScissorCandidates": 4,
            "dlightScissorComputed": 3,
            "dlightScissorApplied": 2,
            "dlightScissorFallbacks": 1,
            "dlightScissorPixels": 1200,
            "dlightScissorViewportPixels": 4800,
            "materialRenderer": "enabled",
            "materialReady": "ready",
            "materialPrograms": 3,
            "materialBinds": 4,
            "materialBindAttempts": 4,
            "materialSwitches": 2,
            "materialCacheMisses": 1,
            "materialCompileFailures": 0,
            "materialLinkFailures": 0,
            "materialPrecacheFailures": 0,
            "materialBindFailures": 0,
            "materialParameterBlocks": 4,
            "materialParameterHash": 0x00F00D,
            "materialInvalidParameterBlocks": 0,
            "materialParameterFeatures": 0x0C23,
            "materialParameterFlags": 0x0C23,
            "materialParameterState": 0x30,
            "materialParameterTcGen0": 3,
            "materialParameterTcGen1": 2,
        }
    )
    return {
        "found": True,
        "sampleCount": 1,
        "latest": latest,
        "max": {},
    }


def color_sweep_runs(gate: str) -> list[dict[str, object]]:
    runs: list[dict[str, object]] = []
    for row in glx_runtime_sweep.GLX_COLOR_SWEEP_MATRIX:
        row = dict(row)
        row_id = str(row["id"])
        runs.append(
            {
                "type": "color-sweep",
                "status": "passed",
                "renderer": "glx",
                "map": "q3dm1",
                "colorSweepRow": row,
                "screenshots": [
                    {
                        "name": f"color-{row_id}",
                        "found": True,
                        "baselineKey": f"glx-color-{row_id}-q3dm1",
                        "baselineStatus": "passed" if gate == "rc-proof" else "not-compared",
                        "comparison": {"status": "passed"} if gate == "rc-proof" else {},
                        "histogram": screenshot_histogram(),
                        "capturePolicy": sdr_capture_policy(),
                        "falseColor": {"status": "passed"},
                        "exposureFalseColor": {"status": "passed"},
                    }
                ],
                "diagnostics": {
                    "found": True,
                    "failures": [],
                    "metrics": color_diagnostics_metrics_for_row(row),
                },
                "performance": locked_performance_sample(row),
            }
        )
    return runs


def proof_corpus_for_gate(gate: str) -> dict[str, object]:
    requirements = glx_runtime_sweep.RC_GATE_PRESETS[gate]["requirements"]
    return glx_runtime_sweep.proof_corpus_manifest(
        glx_runtime_sweep.GLX_GATE_CORPUS_SCENES[gate],
        requirements.get("required_corpus_tags", ()),
        requirements.get("required_parity_suites", ()),
    )


def release_proof_manifest(gate: str, platform_id: str) -> dict[str, object]:
    maps = glx_runtime_sweep.corpus_targets(
        glx_runtime_sweep.GLX_GATE_CORPUS_SCENES[gate],
        "map",
    )
    demos = glx_runtime_sweep.corpus_targets(
        glx_runtime_sweep.GLX_GATE_CORPUS_SCENES[gate],
        "demo",
    )
    switch_sequence = ["opengl", "glx", "opengl", "glx"]
    diagnostic_metrics = (
        stress_material_diagnostics_metrics()
        if gate == "rc-stress"
        else color_diagnostics_metrics()
    )
    switch_screenshots = [
        {
            "name": f"{gate}-{platform_id}-map{map_index}-step{switch_index}-{renderer}",
            "found": True,
            "renderer": renderer,
            "map": map_name,
            "mapIndex": map_index,
            "round": 1,
            "switchStep": switch_index,
            "baselineKey": (
                f"{gate}-{platform_id}-{map_name}-round1-step{switch_index}-{renderer}"
            ),
            "baselineStatus": "passed" if gate == "rc-proof" else "not-compared",
            "comparison": {"status": "passed"} if gate == "rc-proof" else {},
            "histogram": screenshot_histogram(),
            "capturePolicy": sdr_capture_policy(),
            "falseColor": {"status": "passed"},
            "exposureFalseColor": {"status": "passed"},
        }
        for map_index, map_name in enumerate(maps, start=1)
        for switch_index, renderer in enumerate(switch_sequence, start=1)
    ]
    manifest = {
        "runId": f"{platform_id}-{gate}",
        "createdUtc": "2026-05-10T12:00:00+00:00",
        "gate": gate,
        "dryRun": False,
        "proofPlatform": platform_id,
        "maps": maps,
        "demos": demos,
        "renderers": ["opengl", "glx"],
        "switchSequence": switch_sequence,
        "switchRounds": 1,
        "proofCorpus": proof_corpus_for_gate(gate),
        "colorContracts": color_contracts(),
        "imageEvidence": image_evidence_manifest(),
        "performanceFailures": [],
        "runs": [
            {
                "type": "switch-screenshots",
                "status": "passed",
                "restartMode": "fast",
                "vidRestartEquivalent": True,
                "vidRestartPath": "CL_Vid_Restart(REF_KEEP_WINDOW)",
                "screenshots": switch_screenshots,
                "diagnostics": {
                    "found": True,
                    "failures": [],
                    "metrics": diagnostic_metrics,
                },
                "performance": locked_performance_sample(),
            },
        ],
    }
    if glx_runtime_sweep.RC_GATE_PRESETS[gate]["requirements"].get("require_dlight_shadow_scenes"):
        dlight_scenes = glx_runtime_sweep.dlight_shadow_evidence_scenes()
        surface_scene_ids = [
            str(scene["id"])
            for scene in dlight_scenes
            if any(
                category in glx_runtime_sweep.SURFACELIGHT_SPOT_EVIDENCE_CATEGORIES
                for category in scene["categories"]
            )
        ]
        csm_scene_ids = [
            str(scene["id"])
            for scene in dlight_scenes
            if any(
                category in glx_runtime_sweep.CSM_SHADOW_EVIDENCE_CATEGORIES
                for category in scene["categories"]
            )
        ]
        shadow_screenshots: list[dict[str, object]] = []
        for scene_index, scene in enumerate(dlight_scenes, start=1):
            camera_path = [
                dict(step)
                for step in scene.get("csmCameraPath", [])  # type: ignore[union-attr]
                if isinstance(step, dict)
            ]
            if camera_path:
                for step_index, step in enumerate(camera_path, start=1):
                    step_id = str(step["id"])
                    comparison = (
                        {}
                        if step_id == "baseline"
                        else {
                            "status": "passed",
                            "rms": 0.0,
                            "changedPixelRatio": 0.0,
                            "maxRms": glx_runtime_sweep.CSM_SHIMMER_SCREENSHOT_MAX_RMS,
                            "maxChangedPixelRatio": (
                                glx_runtime_sweep.CSM_SHIMMER_SCREENSHOT_MAX_CHANGED_PIXEL_RATIO
                            ),
                        }
                    )
                    shot = {
                        "name": (
                            f"{gate}-{platform_id}-dlight-{scene['id']}-"
                            f"{step_index:02d}-{step_id}"
                        ),
                        "found": True,
                        "renderer": "glx",
                        "map": scene["map"],
                        "mapIndex": scene_index,
                        "baselineKey": (
                            f"{gate}-{platform_id}-dlight-shadows-{scene['id']}-"
                            f"{step_id}-glx"
                        ),
                        "baselineStatus": "not-compared",
                        "histogram": screenshot_histogram(),
                        "capturePolicy": sdr_capture_policy(),
                        "falseColor": {"status": "passed"},
                        "exposureFalseColor": {"status": "passed"},
                        "scene": scene["id"],
                        "evidenceCategories": list(scene["categories"]),
                        "shadowScene": True,
                        "csmCameraPath": True,
                        "csmPathStep": step_id,
                        "csmPathIndex": step_index,
                        "csmSetviewpos": str(step["setviewpos"]),
                        "csmOriginDelta": str(step.get("originDelta", "")),
                        "csmAxisDelta": str(step.get("axisDelta", "")),
                        "csmShimmerRole": "baseline" if step_id == "baseline" else "candidate",
                    }
                    if comparison:
                        shot["csmShimmerBaselineStep"] = "baseline"
                        shot["csmShimmerBaselineKey"] = (
                            f"{gate}-{platform_id}-dlight-shadows-{scene['id']}-baseline-glx"
                        )
                        shot["csmShimmerComparison"] = comparison
                    shadow_screenshots.append(shot)
                continue
            shadow_screenshots.append(
                {
                    "name": f"{gate}-{platform_id}-dlight-{scene['id']}",
                    "found": True,
                    "renderer": "glx",
                    "map": scene["map"],
                    "mapIndex": scene_index,
                    "baselineKey": (
                        f"{gate}-{platform_id}-dlight-shadows-{scene['id']}-glx"
                    ),
                    "baselineStatus": "not-compared",
                    "histogram": screenshot_histogram(),
                    "capturePolicy": sdr_capture_policy(),
                    "falseColor": {"status": "passed"},
                    "exposureFalseColor": {"status": "passed"},
                    "scene": scene["id"],
                    "evidenceCategories": list(scene["categories"]),
                    "shadowScene": True,
                }
            )
        manifest["runs"].append(
            {
                "type": "dlight-shadow-scenes",
                "status": "passed",
                "renderer": "glx",
                "maps": sorted({str(scene["map"]) for scene in dlight_scenes}),
                "scenes": dlight_scenes,
                "screenshots": shadow_screenshots,
                "csmShimmerScreenshots": csm_shimmer_screenshot_summary(),
                "combinedShadowAtlas": combined_shadow_atlas_summary(),
                "csmFallbacks": csm_fallback_summary(),
                "dlightShadow": {
                    "found": True,
                    "sampleCount": 1,
                    "latest": {"planned": 2, "renderLights": 2},
                    "max": {"planned": 2, "renderLights": 2},
                    "scenes": {
                        str(scene["id"]): {
                            "sampleCount": 1,
                            "latest": {"planned": 2, "renderLights": 2},
                            "max": {"planned": 2, "renderLights": 2},
                        }
                        for scene in dlight_scenes
                    },
                    "shadowManager": shadow_manager_summary(
                        [str(scene["id"]) for scene in dlight_scenes]
                    ),
                    "surfaceLightSpot": surface_light_spot_summary(surface_scene_ids),
                    "surfaceLightSpotLod": surface_light_spot_lod_summary(surface_scene_ids),
                    "csmShadows": csm_shadow_summary(csm_scene_ids),
                    "csmFallbacks": csm_fallback_summary(),
                },
            }
        )
    if glx_runtime_sweep.RC_GATE_PRESETS[gate]["requirements"].get("require_glx_color_sweep"):
        manifest["runs"].extend(color_sweep_runs(gate))
    for demo in manifest["demos"]:
        manifest["runs"].extend(
            [
                {
                    "type": "timedemo",
                    "status": "passed",
                    "renderer": "opengl",
                    "demo": demo,
                    "timedemoMetrics": {"fps": 100.0},
                },
                {
                    "type": "timedemo",
                    "status": "passed",
                    "renderer": "glx",
                    "demo": demo,
                    "timedemoMetrics": {"fps": 95.0},
                },
            ]
        )
    if gate == "rc-proof":
        manifest.update(
            {
                "screenshotBaselineDir": "proof/screenshots",
                "performanceBaselinePath": "proof/performance-baseline.json",
                "performanceBaselineStatus": "compared",
                "performanceComparisons": [{"metric": "draws", "status": "passed"}],
                "performanceAggregate": {"sampleCount": 1, "latest": {}, "max": {}},
            }
        )
    requirements = glx_runtime_sweep.RC_GATE_PRESETS[gate]["requirements"]
    manifest["rendererSwitchEvidence"] = glx_runtime_sweep.renderer_switch_lifecycle_evidence(manifest)
    if requirements.get("require_world_proof"):
        manifest["worldProofEvidence"] = glx_runtime_sweep.world_proof_evidence(
            manifest,
            requirements,
        )
    if requirements.get("require_material_proof"):
        manifest["materialProofEvidence"] = glx_runtime_sweep.material_proof_evidence(
            manifest,
            requirements,
        )
    if requirements.get("require_dynamic_proof"):
        manifest["dynamicProofEvidence"] = glx_runtime_sweep.dynamic_proof_evidence(
            manifest,
            requirements,
        )
    if requirements.get("require_post_proof"):
        manifest["postProofEvidence"] = glx_runtime_sweep.post_proof_evidence(
            manifest,
            requirements,
        )
    return manifest


def ownership_proof_manifest(
    platform_id: str,
    calls: int = 0,
    items: int = 0,
    tier: str = "GL3X",
    modern_tier_diagnostics: bool = True,
    post_mode: str | None = "glx-owned",
    post_nodes: int = 2,
    outputs: int = 1,
    executable_nodes: int | None = 2,
    executable_outputs: int | None = 1,
    legacy_fallback: int = 0,
    post_hash: int = 1,
    output_hash: int = 1,
) -> dict[str, object]:
    metrics: dict[str, object] = {
        "ownership": {
            "calls": calls,
            "items": items,
        },
        "productTier": {
            "tier": tier,
        },
        "postShaderDirectFinal": {
            "execute": 1,
            "eligible": 1,
            "bound": 1,
            "reject": 0,
            "candidates": 2,
            "eligibleFrames": 2,
            "attempts": 2,
            "binds": 2,
            "fallbacks": 0,
            "rejects": 0,
        },
    }
    if modern_tier_diagnostics and tier == "GL3X":
        metrics["gl3xExecutor"] = {
            "active": 1,
            "fboPostProcess": 1,
        }
        metrics["gl3xSupport"] = {
            "modernPostChain": 1,
            "sceneLinearOutput": 1,
        }
    elif modern_tier_diagnostics and tier == "GL41":
        metrics["gl41Executor"] = {
            "active": 1,
            "fboPostProcess": 1,
        }
        metrics["gl41Support"] = {
            "modernPostChain": 1,
            "sceneLinearOutput": 1,
        }
    elif modern_tier_diagnostics and tier == "GL46":
        metrics["gl46Executor"] = {
            "active": 1,
        }
        metrics["gl46Support"] = {
            "modernPostChain": 1,
            "sceneLinearOutput": 1,
        }
    if post_mode is not None:
        metrics["postOutputOwnership"] = {
            "mode": post_mode,
            "postNodes": post_nodes,
            "outputs": outputs,
            "legacyFallback": legacy_fallback,
            "postHash": post_hash,
            "outputHash": output_hash,
            "planHash": 0x090A0B0C,
            "fallbackMask": 0,
        }
        if executable_nodes is not None:
            metrics["postOutputOwnership"]["executableNodes"] = executable_nodes  # type: ignore[index]
        if executable_outputs is not None:
            metrics["postOutputOwnership"]["executableOutputs"] = executable_outputs  # type: ignore[index]

    manifest = {
        "runId": f"{platform_id}-glx-ownership",
        "createdUtc": "2026-05-10T12:30:00+00:00",
        "gate": "",
        "profile": "glx-ownership",
        "dryRun": False,
        "proofPlatform": platform_id,
        "maps": ["q3dm1", "q3dm17"],
        "demos": [],
        "renderers": ["opengl", "glx"],
        "performanceFailures": [],
        "runs": [
            {
                "type": "switch-screenshots",
                "status": "passed",
                "screenshots": [
                    {
                        "name": "ownership-shot",
                        "found": True,
                        "histogram": screenshot_histogram(),
                    },
                ],
                "diagnostics": {
                    "found": True,
                    "failures": [],
                    "metrics": metrics,
                },
            },
        ],
    }
    manifest["ownershipProofEvidence"] = glx_runtime_sweep.ownership_proof_evidence(manifest)
    return manifest


def projected_dlight_shader_parity_manifest(
    *,
    screenshot_comparison: dict[str, object] | None = None,
    legacy_comparison: dict[str, object] | None = None,
    metrics: dict[str, object] | None = None,
    baseline_status: str = "passed",
) -> dict[str, object]:
    if screenshot_comparison is None:
        screenshot_comparison = {
            "status": "passed",
            "rms": 0.5,
            "changedPixelRatio": 0.001,
        }
    if legacy_comparison is None:
        legacy_comparison = {
            "status": "passed",
            "rms": 0.5,
            "changedPixelRatio": 0.001,
        }
    if metrics is None:
        metrics = projected_dlight_shader_metrics()
    suite_ids = glx_runtime_sweep.profile_parity_suite_ids("glx-dlight-shader")
    manifest = {
        "runId": "glx-dlight-shader-parity",
        "createdUtc": "2026-06-04T12:00:00+00:00",
        "gate": "",
        "profile": "glx-dlight-shader",
        "dryRun": False,
        "maps": ["q3dm1"],
        "demos": ["demo1"],
        "renderers": ["opengl", "glx"],
        "screenshotBaselineDir": "proof/screenshots",
        "approveScreenshotBaselines": False,
        "screenshotThresholds": {
            "maxRms": glx_runtime_sweep.GLX_PROJECTED_DLIGHT_SHADER_PARITY_MAX_RMS,
            "maxChangedPixelRatio": (
                glx_runtime_sweep.GLX_PROJECTED_DLIGHT_SHADER_PARITY_MAX_CHANGED_PIXEL_RATIO
            ),
        },
        "proofCorpus": glx_runtime_sweep.proof_corpus_manifest(
            glx_runtime_sweep.corpus_scene_ids_for_profile("glx-dlight-shader"),
            (),
            suite_ids,
        ),
        "performanceFailures": [],
        "runs": [
            {
                "type": "switch-screenshots",
                "status": "passed",
                "screenshots": [
                    {
                        "name": "projected-dlight-shader-q3dm1-legacy",
                        "found": True,
                        "renderer": "glx",
                        "map": "q3dm1",
                        "baselineKey": (
                            "glx-dlight-shader-projected-dlight-legacy-fallback-"
                            "q3dm1-round1-legacy-source"
                        ),
                        "baselineStatus": "reference",
                        "histogram": screenshot_histogram(),
                        "capturePolicy": sdr_capture_policy(),
                        "paritySuiteIds": list(suite_ids),
                        "projectedDlightShaderParity": True,
                        "projectedDlightShaderParityRole": (
                            glx_runtime_sweep.GLX_PROJECTED_DLIGHT_SHADER_PARITY_LEGACY_ROLE
                        ),
                        "legacyFallbackBaseline": "compat-projected-light",
                        "skipExternalBaseline": True,
                    },
                    {
                        "name": "projected-dlight-shader-q3dm1",
                        "found": True,
                        "renderer": "glx",
                        "map": "q3dm1",
                        "baselineKey": (
                            "glx-dlight-shader-projected-dlight-legacy-fallback-"
                            "q3dm1-round1"
                        ),
                        "baselineStatus": baseline_status,
                        "comparison": screenshot_comparison,
                        "histogram": screenshot_histogram(),
                        "capturePolicy": sdr_capture_policy(),
                        "paritySuiteIds": list(suite_ids),
                        "projectedDlightShaderParity": True,
                        "projectedDlightShaderParityRole": (
                            glx_runtime_sweep.GLX_PROJECTED_DLIGHT_SHADER_PARITY_CANDIDATE_ROLE
                        ),
                        "legacyFallbackBaseline": "compat-projected-light",
                        "legacyFallbackCaptureName": "projected-dlight-shader-q3dm1-legacy",
                        "baselineSourceName": "projected-dlight-shader-q3dm1-legacy",
                        "baselineSourceRole": (
                            glx_runtime_sweep.GLX_PROJECTED_DLIGHT_SHADER_PARITY_LEGACY_ROLE
                        ),
                        "legacyFallbackComparison": legacy_comparison,
                    },
                ],
                "diagnostics": {
                    "found": True,
                    "failures": [],
                    "metrics": metrics,
                },
            },
            {
                "type": "timedemo",
                "status": "passed",
                "renderer": "glx",
                "demo": "demo1",
                "timedemoMetrics": {"fps": 95.0},
                "screenshots": [
                    {
                        "name": "projected-dlight-shader-demo1-legacy",
                        "found": True,
                        "renderer": "glx",
                        "demo": "demo1",
                        "baselineKey": (
                            "glx-dlight-shader-projected-dlight-demo-legacy-"
                            "fallback-demo1-legacy-source"
                        ),
                        "baselineStatus": "reference",
                        "histogram": screenshot_histogram(),
                        "capturePolicy": sdr_capture_policy(),
                        "paritySuiteIds": list(suite_ids),
                        "projectedDlightShaderParity": True,
                        "projectedDlightShaderTimedemoParity": True,
                        "projectedDlightShaderParityRole": (
                            glx_runtime_sweep.GLX_PROJECTED_DLIGHT_SHADER_PARITY_LEGACY_ROLE
                        ),
                        "legacyFallbackBaseline": "compat-projected-light",
                        "skipExternalBaseline": True,
                    },
                    {
                        "name": "projected-dlight-shader-demo1",
                        "found": True,
                        "renderer": "glx",
                        "demo": "demo1",
                        "baselineKey": (
                            "glx-dlight-shader-projected-dlight-demo-legacy-"
                            "fallback-demo1"
                        ),
                        "baselineStatus": baseline_status,
                        "comparison": screenshot_comparison,
                        "histogram": screenshot_histogram(),
                        "capturePolicy": sdr_capture_policy(),
                        "paritySuiteIds": list(suite_ids),
                        "projectedDlightShaderParity": True,
                        "projectedDlightShaderTimedemoParity": True,
                        "projectedDlightShaderParityRole": (
                            glx_runtime_sweep.GLX_PROJECTED_DLIGHT_SHADER_PARITY_CANDIDATE_ROLE
                        ),
                        "legacyFallbackBaseline": "compat-projected-light",
                        "legacyFallbackCaptureName": "projected-dlight-shader-demo1-legacy",
                        "baselineSourceName": "projected-dlight-shader-demo1-legacy",
                        "baselineSourceRole": (
                            glx_runtime_sweep.GLX_PROJECTED_DLIGHT_SHADER_PARITY_LEGACY_ROLE
                        ),
                        "legacyFallbackComparison": legacy_comparison,
                    },
                ],
                "diagnostics": {
                    "found": True,
                    "failures": [],
                    "metrics": metrics,
                },
            },
        ],
    }
    manifest["projectedDlightShaderParityEvidence"] = (
        glx_runtime_sweep.projected_dlight_shader_parity_evidence(manifest)
    )
    return manifest


def rollback_package_metadata(
    artifact_dir: str = "fnql-legacy-opengl",
    platforms: list[str] | None = None,
    legacy_renderers: list[str] | None = None,
    required_artifacts: dict[str, bool] | None = None,
    triggers: list[str] | None = None,
) -> dict[str, object]:
    return {
        "version": glx_promotion.PROMOTION_ROLLBACK_METADATA_VERSION,
        "status": "reviewed",
        "promotedRenderer": "glx",
        "aliasRenderer": "opengl",
        "migrationInstructions": "Use cl_renderer opengl for the promoted GLx alias.",
        "rollbackInstructions": "Use the rollback package, set cl_renderer opengl, and run vid_restart.",
        "requiredArtifacts": required_artifacts
        if required_artifacts is not None
        else {
            "proofCorpus": True,
            "promotionReport": True,
            "releaseProofSummary": True,
            "checksums": True,
        },
        "rollbackTriggers": triggers
        if triggers is not None
        else [
            "unexplained demo playback regression",
            "unexplained screenshot parity regression",
            "driver startup or presentation regression",
            "confirmed performance regression",
        ],
        "rollbackPackages": [
            {
                "id": "fnql-legacy-opengl",
                "type": "rollback",
                "artifactDir": artifact_dir,
                "platforms": platforms
                if platforms is not None
                else list(glx_runtime_sweep.GLX_BLOCKING_RELEASE_PLATFORMS),
                "legacyRenderers": legacy_renderers
                if legacy_renderers is not None
                else ["opengl", "opengl2"],
                "selectionInstructions": "Select the legacy opengl renderer from this package.",
            }
        ],
    }


def parse_glx_feature_matrix() -> list[dict[str, str]]:
    text = FEATURE_MATRIX_PATH.read_text(encoding="utf-8")
    rows: list[dict[str, str]] = []
    for line in text.splitlines():
        if not line.startswith("| "):
            continue
        cells = [cell.strip().strip("`") for cell in line.strip().strip("|").split("|")]
        if len(cells) != 6 or cells[0] in {"ID", "---"} or set(cells[0]) <= {"-"}:
            continue
        rows.append(
            {
                "id": cells[0],
                "category": cells[1],
                "feature": cells[2],
                "status": cells[3],
                "evidence": cells[4],
                "closure": cells[5],
            }
        )
    return rows


class GlxArchitecturalCutoverPlanTests(unittest.TestCase):
    @staticmethod
    def _plan_section(start: str, end: str) -> str:
        plan = (ROOT / "docs" / "plans" / "glx-review-9-5-26.md").read_text(encoding="utf-8")
        return plan.split(start, 1)[1].split(end, 1)[0]

    def assert_tasks_are_implemented(self, section: str, tasks: tuple[str, ...]) -> None:
        for task in tasks:
            match = re.search(rf"\*\*{task} .*?(?=\n\*\*Task [A-Z]|$)", section, re.DOTALL)
            self.assertIsNotNone(match, task)
            self.assertIn("**Implemented by:**", match.group(0))

    def test_architectural_cutover_tasks_are_marked_implemented(self) -> None:
        section = self._plan_section("### Architectural cutover tasks", "### Tiered execution tasks")

        self.assertIn("All architectural cutover tasks are now implemented.", section)
        self.assert_tasks_are_implemented(section, ("Task A", "Task B", "Task C", "Task D"))

    def test_tiered_execution_tasks_are_marked_implemented(self) -> None:
        section = self._plan_section("### Tiered execution tasks", "### Material, map-scale, and feature-closure tasks")

        self.assertIn("All tiered execution tasks are now implemented.", section)
        self.assert_tasks_are_implemented(section, ("Task E", "Task F", "Task G", "Task H", "Task I", "Task J"))

    def test_material_map_scale_tasks_are_marked_implemented(self) -> None:
        section = self._plan_section("### Material, map-scale, and feature-closure tasks", "### HDR")

        self.assertIn("All material, map-scale, and feature-closure tasks are now implemented.", section)
        self.assert_tasks_are_implemented(section, ("Task K", "Task L", "Task M", "Task N", "Task O"))

    def test_task_k_is_marked_implemented(self) -> None:
        section = self._plan_section("### Material, map-scale, and feature-closure tasks", "**Task L")

        self.assert_tasks_are_implemented(section, ("Task K",))

    def test_task_l_is_marked_implemented(self) -> None:
        section = self._plan_section("### Material, map-scale, and feature-closure tasks", "**Task M")

        self.assert_tasks_are_implemented(section, ("Task L",))

    def test_task_m_is_marked_implemented(self) -> None:
        section = self._plan_section("### Material, map-scale, and feature-closure tasks", "**Task N")

        self.assert_tasks_are_implemented(section, ("Task M",))

    def test_task_n_is_marked_implemented(self) -> None:
        section = self._plan_section("### Material, map-scale, and feature-closure tasks", "**Task O")

        self.assert_tasks_are_implemented(section, ("Task N",))

    def test_task_o_is_marked_implemented(self) -> None:
        section = self._plan_section("### Material, map-scale, and feature-closure tasks", "### HDR")

        self.assert_tasks_are_implemented(section, ("Task O",))

    def test_task_p_is_marked_implemented(self) -> None:
        section = self._plan_section("### HDR, color grading, and output tasks", "**Task Q")

        self.assert_tasks_are_implemented(section, ("Task P",))

    def test_task_q_is_marked_implemented(self) -> None:
        section = self._plan_section("### HDR, color grading, and output tasks", "**Task R")

        self.assert_tasks_are_implemented(section, ("Task Q",))

    def test_task_r_is_marked_implemented(self) -> None:
        section = self._plan_section("### HDR, color grading, and output tasks", "**Task S")

        self.assert_tasks_are_implemented(section, ("Task R",))

    def test_task_s_is_marked_implemented(self) -> None:
        section = self._plan_section("### HDR, color grading, and output tasks", "### Performance, testing, and release tasks")

        self.assert_tasks_are_implemented(section, ("Task S",))

    def test_task_t_is_marked_implemented(self) -> None:
        section = self._plan_section("### Performance, testing, and release tasks", "**Task U")

        self.assert_tasks_are_implemented(section, ("Task T",))

    def test_task_u_is_marked_implemented(self) -> None:
        section = self._plan_section("### Performance, testing, and release tasks", "**Task V")

        self.assert_tasks_are_implemented(section, ("Task U",))

    def test_task_v_is_marked_implemented(self) -> None:
        section = self._plan_section("### Performance, testing, and release tasks", "**Task W")

        self.assert_tasks_are_implemented(section, ("Task V",))

    def test_task_w_is_marked_implemented(self) -> None:
        section = self._plan_section("### Performance, testing, and release tasks", "**Task X")

        self.assert_tasks_are_implemented(section, ("Task W",))

    def test_task_x_is_marked_implemented(self) -> None:
        section = self._plan_section("### Performance, testing, and release tasks", "## Release gates")

        self.assert_tasks_are_implemented(section, ("Task X",))

    def test_feature_closure_matrix_has_zero_ambiguous_rows(self) -> None:
        rows = parse_glx_feature_matrix()
        self.assertGreaterEqual(len(rows), 40)

        seen: set[str] = set()
        statuses: set[str] = set()
        for row in rows:
            self.assertNotIn(row["id"], seen)
            seen.add(row["id"])
            self.assertIn(row["status"], FEATURE_MATRIX_ALLOWED_STATUSES, row)
            self.assertTrue(row["category"], row)
            self.assertTrue(row["feature"], row)
            self.assertTrue(row["evidence"], row)
            self.assertTrue(row["closure"], row)
            self.assertNotRegex(row["status"], r"(?i)\b(tbd|unknown|unclear|maybe|n/a)\b")
            self.assertNotRegex(row["evidence"], r"(?i)\b(tbd|unknown|unclear|maybe|n/a)\b")
            self.assertNotRegex(row["closure"], r"(?i)\b(tbd|unknown|unclear|maybe|n/a)\b")
            statuses.add(row["status"])

        self.assertEqual(statuses, FEATURE_MATRIX_ALLOWED_STATUSES)
        self.assertTrue(FEATURE_MATRIX_REQUIRED_IDS.issubset(seen))


class GlxRuntimeSweepExecutableTests(unittest.TestCase):
    def test_default_executable_candidates_use_unified_client_names(self) -> None:
        names = glx_runtime_sweep.candidate_exe_names()

        self.assertTrue(any(name.startswith("fnql") for name in names))
        self.assertFalse(any(".glx" in name for name in names))
        self.assertFalse(any(".opengl" in name for name in names))
        self.assertFalse(any(".vulkan" in name for name in names))

    def test_launch_args_enable_engine_logfile_by_default(self) -> None:
        command = glx_runtime_sweep.base_launch_args(
            Path("fnql.exe"),
            Path("base"),
            Path("home"),
            "",
            "glx",
            "sweep.cfg",
            {},
        )

        logfile_index = command.index("logfile")
        self.assertEqual(command[logfile_index + 1], "2")
        developer_index = command.index("developer")
        self.assertEqual(command[developer_index + 1], "1")

        command = glx_runtime_sweep.base_launch_args(
            Path("fnql.exe"),
            Path("base"),
            Path("home"),
            "",
            "glx",
            "sweep.cfg",
            {"developer": "0", "logfile": "4"},
        )

        self.assertEqual(command.count("logfile"), 1)
        logfile_index = command.index("logfile")
        self.assertEqual(command[logfile_index + 1], "4")
        self.assertEqual(command.count("developer"), 1)
        developer_index = command.index("developer")
        self.assertEqual(command[developer_index + 1], "0")

    def test_run_engine_preserves_qconsole_log(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            engine_log = root / "home" / "baseq3" / "qconsole.log"
            artifact_log = root / "artifact.log"
            script = (
                "from pathlib import Path\n"
                "import sys\n"
                "path = Path(sys.argv[1])\n"
                "path.parent.mkdir(parents=True, exist_ok=True)\n"
                "path.write_text('engine color metadata\\n', encoding='utf-8')\n"
                "print('stdout metadata')\n"
            )

            result = glx_runtime_sweep.run_engine(
                [sys.executable, "-c", script, str(engine_log)],
                root,
                10.0,
                artifact_log,
                False,
                engine_log,
            )

            text = artifact_log.read_text(encoding="utf-8")
            self.assertEqual(result["status"], "passed")
            self.assertIn("stdout metadata", text)
            self.assertIn("engine color metadata", text)

    def test_default_executable_resolution_ignores_renderer_wrappers(self) -> None:
        old_output = glx_runtime_sweep.DEFAULT_OUTPUT

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            names = glx_runtime_sweep.candidate_exe_names()
            unified_name = names[0]
            stale_names = (
                ["fnql.glx.x64.exe", "fnql.opengl.x64.exe"]
                if glx_runtime_sweep.os.name == "nt"
                else ["fnql.glx", "fnql.opengl"]
            )

            glx_runtime_sweep.DEFAULT_OUTPUT = root
            try:
                for stale_name in stale_names:
                    (root / stale_name).touch()
                with self.assertRaises(FileNotFoundError):
                    glx_runtime_sweep.resolve_exe(None)

                (root / unified_name).touch()
                self.assertEqual(
                    glx_runtime_sweep.resolve_exe(None),
                    (root / unified_name).resolve(),
                )
            finally:
                glx_runtime_sweep.DEFAULT_OUTPUT = old_output

    def test_color_sweep_dry_run_plans_all_p0_rows(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(
                [
                    sys.executable,
                    str(SWEEP_PATH),
                    "--dry-run",
                    "--exe",
                    str(root / "fnql.exe"),
                    "--output-dir",
                    str(root / "sweeps"),
                    "--profile",
                    "glx-color",
                    "--renderers",
                    "glx",
                    "--switch-sequence",
                    "glx",
                    "--maps",
                    "q3dm1",
                    "--demos",
                    "",
                    "--no-demo-sweep",
                    "--color-sweep",
                ],
                cwd=str(ROOT),
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
            )
            manifest_path = next((root / "sweeps").rglob("manifest.json"))
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            color_runs = [
                run for run in manifest["runs"] if run.get("type") == "color-sweep"
            ]

            self.assertEqual(len(color_runs), len(glx_runtime_sweep.GLX_COLOR_SWEEP_MATRIX))
            self.assertEqual(manifest["colorContracts"]["version"], glx_runtime_sweep.GLX_COLOR_CONTRACT_VERSION)
            self.assertEqual(manifest["imageEvidence"]["version"], glx_runtime_sweep.GLX_IMAGE_EVIDENCE_VERSION)
            self.assertEqual(
                len(manifest["imageEvidence"]["shaderReferenceRamps"]),
                len(glx_runtime_sweep.GLX_COLOR_SWEEP_MATRIX),
            )
            self.assertEqual(
                {run["colorSweepRow"]["id"] for run in color_runs},
                {row["id"] for row in glx_runtime_sweep.GLX_COLOR_SWEEP_MATRIX},
            )
            self.assertEqual(
                manifest["visualDossier"]["version"],
                glx_runtime_sweep.GLX_VISUAL_DOSSIER_VERSION,
            )
            switch_evidence = manifest["rendererSwitchEvidence"]
            self.assertEqual(
                switch_evidence["version"],
                glx_runtime_sweep.GLX_SWITCH_LIFECYCLE_VERSION,
            )
            self.assertEqual(switch_evidence["status"], "planned")
            self.assertEqual(switch_evidence["plannedTransitions"], 1)
            self.assertIn("renderer-switch-lifecycle", manifest["visualDossier"]["sections"])
            visual_dossier_path = Path(manifest["visualDossier"]["path"])
            self.assertTrue(visual_dossier_path.exists())
            visual_dossier = visual_dossier_path.read_text(encoding="utf-8")
            self.assertIn("Current Pipeline Flow", visual_dossier)
            self.assertIn("Target Pipeline Flow", visual_dossier)
            self.assertIn("Renderer Switch Lifecycle", visual_dossier)
            self.assertIn("Driver Tier Matrix", visual_dossier)
            self.assertIn("SDR/HDR Color Sweep Review", visual_dossier)
            self.assertTrue(
                any("+set r_srgbTextures 0" in run["commandLine"] for run in color_runs)
            )
            self.assertTrue(manifest["runtimeQpathToken"].startswith("gs"))
            for run in manifest["runs"]:
                self.assertLess(len(Path(run["config"]).name), glx_runtime_sweep.Q3_MAX_QPATH)
                self.assertIn("devmap q3dm1", Path(run["config"]).read_text(encoding="utf-8"))
                for shot in run.get("screenshots", []):
                    self.assertLess(len(shot["name"]), glx_runtime_sweep.Q3_MAX_QPATH)


class GlxRendererSourceCoverageTests(unittest.TestCase):
    def test_requested_glx_renderer_load_failure_is_fatal(self) -> None:
        client_source = (ROOT / "code" / "client" / "cl_main.cpp").read_text(encoding="utf-8")
        start = client_source.index("static void CL_InitRef")
        failure_start = client_source.index("if ( !rendererLib )", start)
        failure_end = client_source.index("rendererLib = Sys_LoadLibrary( ospath );", failure_start)
        load_failure_body = client_source[failure_start:failure_end]

        self.assertIn("CL_RendererLoadFailureIsFatal", client_source)
        self.assertIn('!Q_stricmp( rendererName, "glx" )', client_source)
        self.assertIn("requestedRenderer = cl_renderer->string", client_source)
        self.assertIn("CL_RendererLoadFailureIsFatal( requestedRenderer )", load_failure_body)
        self.assertIn("Com_Error( ERR_FATAL", load_failure_body)
        self.assertNotIn("OpenGL " + "fallback", load_failure_body)

    def test_depth_fragment_does_not_block_multitexture_collapse(self) -> None:
        shader_source = (ROOT / "code" / "renderer" / "tr_shader.c").read_text(encoding="utf-8")
        start = shader_source.index("static qboolean CollapseMultitexture")
        end = shader_source.index("#ifdef USE_PMLIGHT", start)
        collapse_body = shader_source[start:end]

        self.assertNotIn("st0->depthFragment", collapse_body)
        self.assertNotRegex(collapse_body, r"depthFragment[\s\S]{0,120}return\s+qfalse")

    def test_hdr_docs_and_cvars_use_scene_linear_semantics(self) -> None:
        display_doc = (ROOT / "docs" / "DISPLAY.md").read_text(encoding="utf-8")
        glx_doc = (ROOT / "docs" / "fnql" / "GLX_RENDERER.md").read_text(encoding="utf-8")
        renderer_init = (ROOT / "code" / "renderer" / "tr_init.c").read_text(encoding="utf-8")
        vulkan_init = (ROOT / "code" / "renderervk" / "tr_init.c").read_text(encoding="utf-8")
        glx_ir = (ROOT / "code" / "rendererglx" / "glx_render_ir.h").read_text(encoding="utf-8")

        self.assertIn("`r_hdr`: Selects the HDR-capable FBO render pipeline.", display_doc)
        self.assertIn("`r_hdrPrecision`", display_doc)
        self.assertIn("legacy tone mapping preserves Quake III display-referred lighting", glx_doc)
        self.assertNotIn("`r_hdr`: Controls framebuffer precision.", display_doc)
        self.assertNotIn("HDR precision mode", glx_doc)
        self.assertIn("Selects the HDR-capable FBO render pipeline", renderer_init)
        self.assertIn("Selects the scene-linear HDR render pipeline", vulkan_init)
        for source in (renderer_init, vulkan_init):
            self.assertIn("r_hdrPrecision", source)
            self.assertNotIn("Enables high dynamic range frame buffer texture format", source)
        self.assertIn("SceneColorSpace::SceneLinear", glx_ir)
        self.assertIn("ToneMapOperator::AcesFitted", glx_ir)
        self.assertIn("Aces = AcesFitted", glx_ir)

    def test_glx_auto_exposure_has_tiered_histogram_percentile_path(self) -> None:
        color_math = (ROOT / "code" / "rendererglx" / "glx_color_math.h").read_text(encoding="utf-8")
        render_ir = (ROOT / "code" / "rendererglx" / "glx_render_ir.h").read_text(encoding="utf-8")
        postprocess = (ROOT / "code" / "rendererglx" / "glx_postprocess.cpp").read_text(encoding="utf-8")
        renderer_arb = (ROOT / "code" / "renderer" / "tr_arb.c").read_text(encoding="utf-8")

        self.assertIn("GLX_COLOR_MATH_EXPOSURE_HISTOGRAM_BINS", color_math)
        self.assertIn("GLX_ColorMath_ExposureHistogramPercentile", color_math)
        self.assertIn("ExposureReductionAlgorithm::HistogramPercentile", render_ir)
        self.assertIn("GLX_AUTO_EXPOSURE_HISTOGRAM", postprocess)
        self.assertIn("r_glxAutoExposurePercentile", postprocess)
        self.assertIn("GLX_PostProcess_ModernExposureTier", postprocess)
        self.assertIn("ExposureReductionAlgorithm::SimpleAverage", postprocess)
        self.assertIn("autoExposureHistogramFrames", postprocess)
        self.assertIn("GLX_CompatAutoExposureNeedsSamples", renderer_arb)
        self.assertIn("qglReadPixels( 0, 0, width, height, GL_RGBA, GL_FLOAT", renderer_arb)

    def test_glx_depth_fade_arb_program_preserves_vertex_color(self) -> None:
        renderer_arb = (ROOT / "code" / "renderer" / "tr_arb.c").read_text(encoding="utf-8")
        dummy_vp = renderer_arb[
            renderer_arb.index("static const char *dummyVP"):
            renderer_arb.index("static const char *spriteFP")
        ]
        depth_fade_fp = renderer_arb[
            renderer_arb.index("static const char *depthFadeFP"):
            renderer_arb.index("qboolean GL_DepthFadeProgramAvailable")
        ]

        self.assertIn("MUL base, base, fragment.color", depth_fade_fp)
        self.assertIn("MOV result.color, vertex.color", dummy_vp)

    def test_glx_depth_fade_skips_noworldmodel_hud_views(self) -> None:
        backend = (ROOT / "code" / "renderer" / "tr_backend.c").read_text(encoding="utf-8")
        shade = (ROOT / "code" / "renderer" / "tr_shade.c").read_text(encoding="utf-8")
        depth_snapshot = backend[
            backend.index("if ( !depthFadeSnapshot"):
            backend.index("FBO_CopyDepthFade();")
        ]
        depth_active = shade[
            shade.index("static qboolean RB_DepthFadeActive"):
            shade.index("static void RB_DrawDepthFadeStage")
        ]

        self.assertIn("( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) == 0", depth_snapshot)
        self.assertIn("( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) == 0", depth_active)

    def test_glx_world_cel_outline_uses_opaque_cutoff(self) -> None:
        backend = (ROOT / "code" / "renderer" / "tr_backend.c").read_text(encoding="utf-8")

        self.assertIn("if ( shader->sort > SS_OPAQUE ) {", backend)
        self.assertNotIn("if ( shader->sort >= SS_BLEND0 ) {", backend)

    def test_hdr_screenshot_capture_policy_stays_sdr_by_default(self) -> None:
        renderer_init = (ROOT / "code" / "renderer" / "tr_init.c").read_text(encoding="utf-8")
        glx_render_ir = (ROOT / "code" / "rendererglx" / "glx_render_ir.h").read_text(encoding="utf-8")
        glx_module = (ROOT / "code" / "rendererglx" / "glx_module.cpp").read_text(encoding="utf-8")
        glx_postprocess = (ROOT / "code" / "rendererglx" / "glx_postprocess.cpp").read_text(encoding="utf-8")
        screenshots_doc = (ROOT / "docs" / "SCREENSHOTS.md").read_text(encoding="utf-8")
        audit_doc = COLORSPACE_AUDIT_PATH.read_text(encoding="utf-8")
        sweep = (ROOT / "scripts" / "glx_runtime_sweep.py").read_text(encoding="utf-8")

        self.assertIn('r_screenshotCaptureMode = ri.Cvar_Get( "r_screenshotCaptureMode", "0"', renderer_init)
        self.assertIn("R_ScreenshotCaptureModeName", renderer_init)
        self.assertIn('return "sdr-srgb";', renderer_init)
        self.assertIn("R_WarnExplicitHdrScreenshotCapture", renderer_init)
        self.assertIn("captureMode %s", renderer_init)
        self.assertIn("captureColorSpace %s", renderer_init)
        self.assertIn("CaptureExportPolicy", glx_render_ir)
        self.assertIn("GLX_RenderIR_CaptureOutputTransform", glx_render_ir)
        self.assertIn("OutputTransfer::ScreenshotSrgb", glx_render_ir)
        self.assertIn("capture-request", glx_module)
        self.assertIn("capture policy", glx_postprocess)
        self.assertIn("default to SDR sRGB byte output", screenshots_doc)
        self.assertIn("`r_screenshotCaptureMode` records the explicit capture policy", screenshots_doc)
        self.assertIn("Reserved scene-linear HDR export request", screenshots_doc)
        self.assertIn("reserved HDR-output request", renderer_init)
        self.assertIn("Screenshot capture/export", audit_doc)
        self.assertIn("capture-request", audit_doc)
        self.assertIn("GLX_SCREENSHOT_CAPTURE_POLICY_CONTRACT", sweep)
        self.assertIn("capturePolicyRequest", sweep)
        self.assertIn('"defaultColorSpace": "sdr-srgb"', sweep)
        self.assertIn('"hdr-scene-linear"', sweep)
        self.assertIn('"hdr-output"', sweep)
        self.assertIn('"hdrExportStatus": "explicit-reserved"', sweep)

    def test_hdr_bloom_intermediates_have_role_based_format_policy(self) -> None:
        fbo_source = (ROOT / "code" / "renderer" / "tr_arb.c").read_text(encoding="utf-8")
        init_source = (ROOT / "code" / "renderer" / "tr_init.c").read_text(encoding="utf-8")
        cmds_source = (ROOT / "code" / "renderer" / "tr_cmds.c").read_text(encoding="utf-8")
        glx_postprocess = (ROOT / "code" / "rendererglx" / "glx_postprocess.cpp").read_text(encoding="utf-8")
        display_doc = (ROOT / "docs" / "DISPLAY.md").read_text(encoding="utf-8")
        audit_doc = COLORSPACE_AUDIT_PATH.read_text(encoding="utf-8")

        self.assertIn("return GL_RGBA16F;", fbo_source[fbo_source.index("static GLint FBO_MainInternalFormat"):])
        self.assertIn("FBO_PositiveIntermediateCandidates( qtrue", fbo_source)
        self.assertIn("GL_R11F_G11F_B10F", fbo_source)
        self.assertIn("GL_RG16F", fbo_source)
        self.assertIn("FBO_AddFormatCandidate( formats, count, GL_RGBA16F );", fbo_source)
        self.assertIn("FBO_CreateWithFormat", fbo_source)
        self.assertIn("fboBloomInternalFormat = fb->internalFormat;", fbo_source)
        self.assertIn("r_hdrBloomFormat", init_source)
        self.assertIn("r_hdrBloomFormat->modified", cmds_source)
        self.assertIn("bloom storage: policy", glx_postprocess)
        self.assertIn("`r_hdrBloomFormat`", display_doc)
        self.assertIn("positive-only bloom/extract", audit_doc)

    def test_task_q_color_pipeline_sources_are_audited(self) -> None:
        renderer_common = (ROOT / "code" / "renderer" / "tr_common.h").read_text(encoding="utf-8")
        renderer_image = (ROOT / "code" / "renderer" / "tr_image.c").read_text(encoding="utf-8")
        renderer_arb = (ROOT / "code" / "renderer" / "tr_arb.c").read_text(encoding="utf-8")
        vulkan_image = (ROOT / "code" / "renderervk" / "tr_image.c").read_text(encoding="utf-8")
        glx_module = (ROOT / "code" / "rendererglx" / "glx_module.cpp").read_text(encoding="utf-8")
        audit_doc = COLORSPACE_AUDIT_PATH.read_text(encoding="utf-8")
        texture_manifest = json.loads(
            (ROOT / "docs" / "fnql" / "GLX_TEXTURE_CLASSIFICATION_MANIFEST.json").read_text(
                encoding="utf-8"
            )
        )

        self.assertIn("IMGFLAG_COLORSPACE_SRGB", renderer_common)
        self.assertIn("IMAGE_COLORSPACE_SRGB", renderer_common)
        self.assertIn("GL_SRGB8_ALPHA8", renderer_image)
        self.assertIn("textureSrgbAvailable", renderer_image)
        self.assertIn("GL_FRAMEBUFFER_SRGB", renderer_arb)
        self.assertIn("srgbCutoff", renderer_arb)
        self.assertIn("GL_RGBA16F", renderer_arb)
        self.assertIn("VK_FORMAT_R8G8B8A8_SRGB", vulkan_image)
        self.assertIn("glx: color audit srgb-decode", glx_module)
        self.assertIn("glx: texture audit", glx_module)
        for text in (
            "Authored color maps",
            "Lightmaps",
            "GL_FRAMEBUFFER_SRGB",
            "Screenshot/video capture",
            "r_glxColorPipelineDebug",
            "RGBA16F",
        ):
            self.assertIn(text, audit_doc)
        row_ids = {row["id"] for row in texture_manifest["rows"]}
        self.assertTrue(set(glx_runtime_sweep.GLX_REQUIRED_TEXTURE_CLASS_ROWS).issubset(row_ids))
        rows_by_id = {row["id"]: row for row in texture_manifest["rows"]}
        for row_id, expected in glx_runtime_sweep.GLX_TEXTURE_CLASSIFICATION_CONTRACT.items():
            self.assertEqual(rows_by_id[row_id]["declaredSpace"], expected["declaredSpace"])
            self.assertEqual(rows_by_id[row_id]["sceneLinearDecode"], expected["sceneLinearDecode"])

    def test_task_r_color_grading_sources_are_covered(self) -> None:
        renderer_arb = (ROOT / "code" / "renderer" / "tr_arb.c").read_text(encoding="utf-8")
        vulkan_gamma = (ROOT / "code" / "renderervk" / "shaders" / "gamma.frag").read_text(encoding="utf-8")
        vulkan_backend = (ROOT / "code" / "renderervk" / "vk.c").read_text(encoding="utf-8")
        glx_color_math = (ROOT / "code" / "rendererglx" / "glx_color_math.h").read_text(encoding="utf-8")
        glx_post_output = (ROOT / "code" / "rendererglx" / "glx_post_output_reference.h").read_text(encoding="utf-8")
        glx_post_shader_plan = (ROOT / "code" / "rendererglx" / "glx_post_shader_plan.h").read_text(encoding="utf-8")
        glx_post_shader_source = (ROOT / "code" / "rendererglx" / "glx_post_shader_source.h").read_text(encoding="utf-8")
        glx_post_shader = (ROOT / "code" / "rendererglx" / "glx_post_shader.cpp").read_text(encoding="utf-8")
        glx_ir = (ROOT / "code" / "rendererglx" / "glx_render_ir.h").read_text(encoding="utf-8")
        glx_module = (ROOT / "code" / "rendererglx" / "glx_module.cpp").read_text(encoding="utf-8")
        display_doc = (ROOT / "docs" / "DISPLAY.md").read_text(encoding="utf-8")

        for text in (
            "ARB_BuildColorGradeProgram",
            "FBO_BuildBradfordAdaptation",
            "*colorGradeIdentityLUT",
            "texture[2]",
        ):
            self.assertIn(text, renderer_arb)
        for text in (
            "colorGradeLut",
            "applyLiftGammaGainAndWhitePoint",
            "sampleColorGradeLut",
        ):
            self.assertIn(text, vulkan_gamma)
        for text in (
            "GLX_ColorMath_SrgbToLinear",
            "GLX_ColorMath_ToneMapAcesFitted",
            "GLX_ColorMath_PqEncodeNits",
            "GLX_ColorMath_LutAtlasSize",
            "GLX_ColorMath_AdaptWhitePointBradford",
            "GLX_ColorMath_BuildBradfordAdaptationMatrix",
            "GLX_ColorMath_SampleLutAtlas",
        ):
            self.assertIn(text, glx_color_math)
        for text in (
            "GLX_PostOutputReference_Evaluate",
            "GLX_PostOutputReference_ApplyColorGrade",
            "GLX_PostOutputReference_EncodeHdr10Pq",
            "OutputTransform",
        ):
            self.assertIn(text, glx_post_output)
        for text in (
            "GLX_PostShader_BuildPlan",
            "GLX_POST_SHADER_FEATURE_LIFT_GAMMA_GAIN",
            "GLX_POST_SHADER_FEATURE_ENCODE_HDR10_PQ",
            "PostShaderPlan",
        ):
            self.assertIn(text, glx_post_shader_plan)
        for text in (
            "GLX_PostShaderSource_WriteFragment",
            "glxToneMapAces",
            "glxPqEncode",
            "glxSampleLutAtlas",
            "GLX_POST_SHADER_SOURCE_VERSION",
        ):
            self.assertIn(text, glx_post_shader_source)
        for text in (
            "GLX_PostShader_RecordPlan",
            "GLX_PostShader_CacheProgram",
            "GLX_PostShader_CreateProgram",
            "GLX_ColorMath_BuildBradfordAdaptationMatrix",
            "r_glxPostShaderCache",
            "r_glxPostShaderExecute",
            "GLX_PostShader_TryBindDirectFinal",
        ):
            self.assertIn(text, glx_post_shader)
        self.assertIn("vk_color_grade_lut_descriptor", vulkan_backend)
        self.assertIn("color_grade_mode", vulkan_backend)
        self.assertIn("LiftGammaGainLut3D", glx_ir)
        self.assertIn("RecordColorGradeLut", glx_module)
        self.assertIn("PostNodeKind::Grade", glx_ir)
        self.assertIn("GLX_RenderIR_BuildPostOutputPlan", glx_module)
        self.assertIn("TryBindPostShaderDirectFinal", glx_module)
        self.assertIn("post shader cache", glx_module)
        for cvar in (
            "r_colorGrade",
            "r_colorGradeLift",
            "r_colorGradeGamma",
            "r_colorGradeGain",
            "r_colorGradeWhitePoint",
            "r_colorGradeAdaptWhitePoint",
            "r_colorGradeLUT",
            "r_colorGradeLUTScale",
        ):
            self.assertIn(cvar, display_doc)

    def test_task_s_output_backend_sources_are_covered(self) -> None:
        tr_types = (ROOT / "code" / "renderercommon" / "tr_types.h").read_text(encoding="utf-8")
        tr_public = (ROOT / "code" / "renderercommon" / "tr_public.h").read_text(encoding="utf-8")
        sdl_glimp_path = ROOT / "code" / "sdl" / "sdl_glimp.cpp"
        if not sdl_glimp_path.exists():
            sdl_glimp_path = ROOT / "code" / "sdl" / "sdl_glimp.c"
        if not sdl_glimp_path.exists():
            self.fail("Expected SDL GL implementation source at code/sdl/sdl_glimp.cpp or code/sdl/sdl_glimp.c")
        sdl_glimp = sdl_glimp_path.read_text(encoding="utf-8")
        glx_postprocess = (ROOT / "code" / "rendererglx" / "glx_postprocess.cpp").read_text(encoding="utf-8")
        glx_ir = (ROOT / "code" / "rendererglx" / "glx_render_ir.h").read_text(encoding="utf-8")
        vulkan_backend = (ROOT / "code" / "renderervk" / "vk.c").read_text(encoding="utf-8")
        display_doc = (ROOT / "docs" / "DISPLAY.md").read_text(encoding="utf-8")

        for text in (
            "rendererDisplayOutput_t",
            "ROUTPUT_BACKEND_WINDOWS_SCRGB",
            "ROUTPUT_BACKEND_HDR10_PQ",
            "ROUTPUT_BACKEND_MACOS_EDR",
            "ROUTPUT_BACKEND_LINUX_EXPERIMENTAL_HDR",
        ):
            self.assertIn(text, tr_types)
        self.assertIn("GLimp_QueryDisplayOutput", tr_public)
        for text in (
            "SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT",
            "SDL_GetWindowICCProfile",
            "SDL_PROP_DISPLAY_HDR_ENABLED_BOOLEAN",
            "SDL_GL_FLOATBUFFERS",
        ):
            self.assertIn(text, sdl_glimp)
        self.assertIn("r_outputBackend", glx_postprocess)
        self.assertIn("outputHardwareActive", glx_ir)
        self.assertIn("OutputPrimaries::Bt2020", glx_ir)
        self.assertIn("vk_output_request_wants_hdr10", vulkan_backend)
        self.assertIn("precision-request", (ROOT / "code" / "rendererglx" / "glx_module.cpp").read_text(encoding="utf-8"))
        self.assertIn("output backend", glx_runtime_sweep.__dict__["GLX_OUTPUT_BACKEND_RE"].pattern)
        for cvar in ("r_outputBackend", "r_outputAllowExperimentalLinuxHDR"):
            self.assertIn(cvar, display_doc)

    def test_p1_glx_ownership_stream_and_material_sources_are_covered(self) -> None:
        compat = (ROOT / "code" / "renderer" / "tr_glx_compat.h").read_text(encoding="utf-8")
        tr_arb = (ROOT / "code" / "renderer" / "tr_arb.c").read_text(encoding="utf-8")
        tr_backend = (ROOT / "code" / "renderer" / "tr_backend.c").read_text(encoding="utf-8")
        tr_local = (ROOT / "code" / "renderer" / "tr_local.h").read_text(encoding="utf-8")
        tr_shade = (ROOT / "code" / "renderer" / "tr_shade.c").read_text(encoding="utf-8")
        tr_shadows = (ROOT / "code" / "renderer" / "tr_shadows.c").read_text(encoding="utf-8")
        tr_vbo = (ROOT / "code" / "renderer" / "tr_vbo.c").read_text(encoding="utf-8")
        tr_world = (ROOT / "code" / "renderer" / "tr_world.c").read_text(encoding="utf-8")
        bridge = (ROOT / "code" / "renderercommon" / "tr_glx_bridge.h").read_text(encoding="utf-8")
        api = (ROOT / "code" / "renderercommon" / "tr_glx_api.h").read_text(encoding="utf-8")
        public = (ROOT / "code" / "renderercommon" / "tr_glx_public.h").read_text(encoding="utf-8")
        glx_module = (ROOT / "code" / "rendererglx" / "glx_module.cpp").read_text(encoding="utf-8")
        glx_stream = (ROOT / "code" / "rendererglx" / "glx_stream.cpp").read_text(encoding="utf-8")
        glx_executor = (ROOT / "code" / "rendererglx" / "glx_executor.cpp").read_text(encoding="utf-8")
        glx_executor_h = (ROOT / "code" / "rendererglx" / "glx_executor.h").read_text(encoding="utf-8")
        glx_static_world = (ROOT / "code" / "rendererglx" / "glx_static_world.cpp").read_text(encoding="utf-8")
        glx_ir = (ROOT / "code" / "rendererglx" / "glx_render_ir.h").read_text(encoding="utf-8")
        glx_material_key = (ROOT / "code" / "rendererglx" / "glx_material_key.h").read_text(encoding="utf-8")
        glx_material = (ROOT / "code" / "rendererglx" / "glx_material.cpp").read_text(encoding="utf-8")

        for streamed_source in (compat, tr_arb, tr_shade, tr_shadows):
            self.assertNotIn("qglGetIntegerv( GL_ARRAY_BUFFER_BINDING_ARB", streamed_source)
            self.assertNotIn("qglGetIntegerv( GL_ELEMENT_ARRAY_BUFFER_BINDING_ARB", streamed_source)
        self.assertNotIn("qglGetBooleanv( GL_COLOR_WRITEMASK", tr_shadows)
        self.assertIn("GLX_CompatBindStreamArrayBuffer", compat)
        self.assertIn("GLX_CompatBindStreamElementArrayBuffer", bridge)
        self.assertIn("GLX_Renderer_BindStreamArrayBuffer", api)
        self.assertIn("GLX_Renderer_BindStreamElementArrayBuffer", api)
        self.assertIn("GLX_Renderer_DrawElementsClassified", api)
        self.assertIn("GLX_Renderer_DrawArraysClassified", api)
        self.assertIn("GLX_CompatRestoreStreamArrayBuffer", bridge)
        self.assertIn("GLX_CompatRestoreStreamElementArrayBuffer", bridge)
        self.assertIn("GLX_CompatDrawElementsClassified", bridge)
        self.assertIn("GLX_CompatDrawArraysClassified", bridge)
        self.assertIn("GLX_CompatRecordStreamBufferBind", bridge)
        self.assertIn("GLX_Stream_BindArrayBufferCached", glx_stream)
        self.assertIn("GLX_Stream_BindElementArrayBufferCached", glx_stream)
        self.assertIn("GLX_Stream_RecordExternalBufferBind", glx_stream)
        self.assertIn("bufferBindingExternalUpdates", glx_stream)
        self.assertIn("GLX_Stream_BindPreserving( StreamState *state", glx_stream)
        self.assertIn("GLX_Stream_RestoreBinding( StreamState *state", glx_stream)
        self.assertNotIn("GLX_Stream_BindPreserving( GLuint buffer", glx_stream)
        self.assertNotIn("GLX_Stream_RestoreBinding( GLint", glx_stream)
        self.assertNotIn("GL_ARRAY_BUFFER_BINDING, &oldArrayBuffer", glx_stream)
        self.assertNotIn("frameBufferMultiSampling && !depthFadeTexture", tr_arb)
        self.assertIn("if ( !depthFadeTexture &&", tr_arb)
        self.assertIn("if ( depthFadeTextureShared ) {", tr_arb)
        self.assertIn("GL_GetColorMask( rgba );", tr_shadows)
        self.assertIn("GL_ColorMask( cmd->rgba[0], cmd->rgba[1], cmd->rgba[2], cmd->rgba[3] );", tr_backend)
        self.assertIn("GL_ColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );", tr_arb)
        self.assertIn("restoreArrayBuffer = oldArrayBuffer == state->buffer ? 0u : oldArrayBuffer", glx_stream)
        self.assertIn("arrayBufferBindingCacheHits", glx_stream)
        self.assertIn("elementArrayBufferBindingKnown", glx_stream)
        self.assertIn("GLX_StaticWorld_CurrentIndirectBufferBinding", glx_static_world)
        self.assertIn("GLX_StaticWorld_BindIndirectBufferTracked", glx_static_world)
        self.assertIn("GLX_StaticWorld_RestoreIndirectBufferBinding", glx_static_world)
        self.assertIn("indirectBufferBindingKnown", glx_static_world)
        self.assertLessEqual(glx_static_world.count("s_fns.GetIntegerv( GL_DRAW_INDIRECT_BUFFER_BINDING"), 1)
        self.assertIn("GLX_Renderer_RecordStreamBufferBind", api)
        self.assertIn("VBO_BindBufferTracked", tr_vbo)
        self.assertIn("GLX_CompatRecordStreamBufferBind", tr_vbo)
        self.assertIn("dlightClipBits[SHADER_MAX_VERTEXES]", tr_local)
        self.assertIn("dlightIndexes[SHADER_MAX_INDEXES]", tr_local)
        self.assertIn("dlightColors[SHADER_MAX_VERTEXES]", tr_local)
        self.assertIn("texCoords = (float *)tess.svars.texcoords[0];", tr_shade)
        self.assertIn("colors = tess.dlightColors[0];", tr_shade)
        self.assertIn("int numIndexes, byte *visited", tr_shade)
        self.assertIn("Com_Memset( visited, 0, tess.numVertexes * sizeof( *visited ) );", tr_shade)
        self.assertIn("if ( visited[idx] )", tr_shade)
        self.assertIn("glxScissorEnabled = GLX_CompatDlightScissorEnabled();", tr_shade)
        self.assertEqual(tr_shade.count("GLX_CompatDlightScissorEnabled()"), 1)
        self.assertIn("if ( glxScissorEnabled ) {", tr_shade)
        self.assertIn("numIndexes, clipBits, &glxScissorX", tr_shade)
        self.assertIn("cullBackFaces = !r_dlightBacks->integer;", tr_shade)
        self.assertEqual(tr_shade.count("r_dlightBacks->integer"), 1)
        self.assertIn("lightBit = 1 << l;", tr_shade)
        self.assertIn("tess.dlightBits & lightBit", tr_shade)
        self.assertIn("tess.vertexDlightBits[i] & lightBit", tr_shade)
        self.assertIn("tess.vertexDlightBits[c] & lightBit", tr_shade)
        self.assertIn("radiusHalf = radius * 0.5f;", tr_shade)
        self.assertIn("colors[0] = color0 * modulate;", tr_shade)
        self.assertEqual(tr_shade.count("1 << l"), 1)
        self.assertIn("qboolean legacyDlightArraysBound = qfalse;", tr_shade)
        self.assertIn("if ( glxStreamedDraw ) {\n\t\t\tlegacyDlightArraysBound = qtrue;", tr_shade)
        self.assertIn("if ( !legacyDlightArraysBound ) {\n\t\t\t\tGL_ClientState( 1, CLS_NONE );", tr_shade)
        self.assertIn("if ( !legacyDlightArraysBound ) {\n\t\t\t\tGL_ClientState( 1, CLS_NONE );\n\t\t\t\tGL_ClientState( 0, CLS_TEXCOORD_ARRAY | CLS_COLOR_ARRAY );\n\t\t\t\tqglTexCoordPointer", tr_shade)
        self.assertNotIn("glxScissorApplied = glxScissorComputed &&", tr_shade)
        self.assertNotIn("texCoords[0] = 0.0f;", tr_shade)
        self.assertNotIn("colors[0] = 0.0f;", tr_shade)
        self.assertNotIn("texCoordsArray[SHADER_MAX_VERTEXES]", tr_shade)
        self.assertNotIn("colorArray[SHADER_MAX_VERTEXES]", tr_shade)
        self.assertIn("post/output ownership", glx_module)
        self.assertIn("GLX_RenderIR_BuildPostOutputPlan", glx_module)
        self.assertIn("GLX_Executor_ConsumeOutputTransform", glx_module)
        self.assertIn("DrawElementsClassified", glx_module)
        self.assertIn("GLX_RenderIR_ClassifyDynamicDrawRole", glx_module)
        self.assertIn("ProjectedDlightRecord", glx_ir)
        self.assertIn("ProjectedDlightListRef projectedDlights", glx_ir)
        self.assertGreaterEqual(glx_ir.count("ProjectedDlightListRef projectedDlights"), 2)
        self.assertIn("GLX_RenderIR_ValidateProjectedDlightRecord", glx_ir)
        self.assertIn("GLX_RenderIR_ValidateProjectedDlightListRef", glx_ir)
        self.assertIn("GLX_RenderIR_BuildProjectedDlightList", glx_ir)
        self.assertIn("ProjectedDlightListBuildResult", glx_ir)
        self.assertIn("ProjectedDlightShaderInput", glx_ir)
        self.assertIn("GLX_RenderIR_ValidateProjectedDlightShaderInput", glx_ir)
        self.assertIn("result.copiedMask |= bit;", glx_ir)
        self.assertIn("result.droppedMask |= bit;", glx_ir)
        self.assertIn("glxProjectedDlightRecord_t", public)
        self.assertIn("GLX_PROJECTED_DLIGHT_FLAG_ADDITIVE", public)
        self.assertIn("GLX_Renderer_RecordProjectedDlights", api)
        self.assertIn("GLX_Renderer_RecordProjectedDlightList", api)
        self.assertIn("GLX_Renderer_DrawElementsClassifiedProjectedDlights", api)
        self.assertIn("GLX_CompatRecordProjectedDlights", bridge)
        self.assertIn("GLX_CompatRecordProjectedDlightList", bridge)
        self.assertIn("GLX_CompatDrawElementsClassifiedProjectedDlights", bridge)
        self.assertIn("(unsigned int)lightBit", tr_shade)
        self.assertIn("GLX_PMLightMaskForCurrentLight", (ROOT / "code" / "renderer" / "tr_arb.c").read_text(encoding="utf-8"))
        self.assertIn("R_GLXRecordProjectedDlights", tr_world)
        self.assertIn("R_GLXStaticSurfaceItemIndex", tr_world)
        self.assertIn("VectorCopy( dlights[i].transformed, records[i].origin );", tr_world)
        self.assertIn("GLX_CompatRecordProjectedDlightList( R_GLXStaticSurfaceItemIndex( surf ),", tr_world)
        self.assertIn("GLX_Dlight_RecordProjectedDlights", glx_module)
        self.assertIn("GLX_Dlight_RecordProjectedDlightList", glx_module)
        self.assertIn("GLX_Dlight_RecordProjectedPacketDlightList", glx_module)
        # Recording must not wipe earlier views' projected packet lists before
        # the backend consumes them at end-of-frame: an unchanged light set
        # keeps the accumulated frame state and FrameComplete owns the clear.
        self.assertIn("GLX_Dlight_ProjectedRecordsEqual", glx_module)
        self.assertNotIn(
            "GLX_Dlight_ClearProjectedFrame( state );\n\tstate->projectedSourceFrames++;",
            glx_module,
        )
        self.assertIn("GLX_Dlight_ClearProjectedFrame( &dlight_ );", glx_module)
        self.assertIn("GLX_Dlight_RecordProjectedShaderInput", glx_module)
        self.assertIn("GLX_Dlight_ProjectedShaderProgrammable", glx_module)
        self.assertIn("GLX_PROJECTED_DLIGHT_UNIFORM_LIMIT", glx_module)
        self.assertIn("u_ProjectedDlightOriginRadius", glx_module)
        self.assertIn("v_LocalPos;", glx_module)
        self.assertIn("GLX_PROJECTED_DLIGHT_STREAM", glx_module)
        self.assertIn("u_ProjectedDlightRecords", glx_module)
        self.assertIn("projectedShaderResourceLimitPromotions", glx_module)
        self.assertIn("evaluateProjectedDlights", glx_module)
        self.assertIn("r_glxDlightProjectedProgram", glx_module)
        self.assertIn("r_glxDlightProjectedMdi", glx_module)
        self.assertIn("GLX_Dlight_ProjectedProgramEnabled", glx_module)
        self.assertIn("GLX_Dlight_ProjectedMdiEnabled", glx_module)
        self.assertIn("GLX_Dlight_RecordProjectedMdiSubmitPlan", glx_module)
        self.assertIn("GLX_Dlight_SubmitProjectedMdiBatch", glx_module)
        self.assertIn("glMultiDrawElementsIndirect", glx_module)
        self.assertIn("GL_DRAW_INDIRECT_BUFFER", glx_module)
        self.assertIn("GL_DRAW_INDIRECT_BUFFER_BINDING", glx_module)
        self.assertIn("GLX_Dlight_BindDrawIndirectBuffer( oldDrawIndirectBuffer );", glx_module)
        self.assertIn("projectedShaderArenaLightRecords", glx_module)
        self.assertIn("projectedShaderArenaListRecords", glx_module)
        self.assertIn("GLX_Dlight_FillProjectedArenaListRecords", glx_module)
        self.assertIn("GLX_Dlight_RecordProjectedShaderArenaReservation", glx_module)
        self.assertIn("resourceRange->authoritative = qtrue", glx_module)
        self.assertIn("projectedShaderArenaAuthoritativeBinds", glx_module)
        self.assertIn("dlight projected shader arena", glx_module)
        self.assertIn("GLX_PROJECTED_DLIGHT_MDI_COMMAND_RING_LIMIT", glx_module)
        self.assertIn("GLX_Dlight_ReserveProjectedMdiCommandRingSlot", glx_module)
        self.assertIn("projectedShaderMdiCommandRecords[ringSlot] = mdiPlan.command", glx_module)
        self.assertIn("projectedShaderMdiCommandRingCommits", glx_module)
        self.assertIn("dlight projected shader MDI command ring", glx_module)
        self.assertIn("projectedShaderMdiBatchSubmittedDraws += batch.drawCount", glx_module)
        self.assertIn("projectedShaderMdiBatchSubmittedIndexes += batch.indexCount", glx_module)
        self.assertIn("projectedMdiSubmitted", glx_module)
        self.assertIn("GLX_Executor_ConsumeDynamicDraw( &executor_, draw )", glx_module)
        self.assertIn("GLX_Executor_ConsumeDynamicDraw", glx_executor_h)
        self.assertIn("GLX_Executor_AcceptDynamicDraw", glx_executor)
        self.assertIn("GLX_Executor_SubmitDynamicDraw( draw )", glx_executor)
        self.assertIn("GLX_Dlight_BindProjectedShaderInput", glx_module)
        self.assertIn("GLX_Dlight_ClearProjectedShaderInput", glx_module)
        self.assertGreaterEqual(glx_module.count("GLX_Dlight_ClearProjectedShaderInput( &dlight_ );"), 8)
        self.assertIn("dlight projected shader inputs", glx_module)
        self.assertIn("dlight projected shader uniforms", glx_module)
        self.assertIn("projectedPacketListRefs", glx_module)
        self.assertIn("PrepareStaticWorldProjectedDlightRun", glx_module)
        self.assertIn("BindStaticWorldProjectedDlightRun", glx_module)
        self.assertIn("BindStaticWorldProjectedDlightRun( projectedPacket, projectedShaderInput,", glx_module)
        self.assertIn("StaticWorldProjectedDlightSplitActive", glx_module)
        self.assertIn("StaticWorldDrawProjectedDlightRunsFiltered", glx_module)
        self.assertIn("GLX_Dlight_ProjectedProgramEnabled( dlight_ )", glx_module)
        self.assertIn("replacementPlan.legacyFallback", glx_module)
        self.assertGreaterEqual(glx_module.count("replacementPlan.legacyFallback"), 2)
        self.assertIn("ConsumeStaticWorldProjectedDlightRun", glx_module)
        self.assertIn("GLX_Module_StaticWorldPacketForItemRange", glx_module)
        self.assertIn("worldPacketsWithProjectedDlights", glx_executor_h)
        self.assertIn("dynamicDrawsWithProjectedDlights", glx_executor_h)
        self.assertIn("GLX_RENDER_IR_PROJECTED_DLIGHT_LIST_RECORD_LIMIT", glx_ir)
        self.assertIn("projectedResourceAuthoritative", glx_ir)
        self.assertIn("ProjectedDlightShaderExecutionPlan", glx_ir)
        self.assertIn("ProjectedDlightShaderReplacementPlan", glx_ir)
        self.assertIn("GLX_RenderIR_PlanProjectedDlightShaderExecution", glx_ir)
        self.assertIn("GLX_RenderIR_PlanProjectedDlightShaderReplacement", glx_ir)
        self.assertIn("MaterialParameterBlock", glx_ir)
        self.assertIn("PostOutputPlan", glx_ir)
        self.assertIn("GLX_POST_OUTPUT_FALLBACK_EXECUTOR_REJECT", glx_ir)
        self.assertIn("GLX_POST_OUTPUT_FALLBACK_EXECUTOR_NOT_IMPLEMENTED", glx_ir)
        self.assertIn("GLX_RenderIR_PostNodeExecutorImplemented", glx_ir)
        self.assertIn("executable nodes", glx_module)
        self.assertIn("post shader plan", glx_module)
        self.assertIn("GLX_PostProcess_RecordPostShaderPlan", glx_module)
        sweep_source = SWEEP_PATH.read_text(encoding="utf-8")
        self.assertIn("GLX_DLIGHT_PROJECTED_SHADER_ARENA_RE", sweep_source)
        self.assertIn("GLX_DLIGHT_PROJECTED_SHADER_RESOURCE_RE", sweep_source)
        self.assertIn("dlightProjectedShaderResourcePromotions", sweep_source)
        self.assertIn("dlightProjectedShaderArenaUploads", sweep_source)
        self.assertIn("dlightProjectedShaderArenaAuthoritativeBinds", sweep_source)
        self.assertIn("did not bind authoritative projected-light arena ranges", sweep_source)
        self.assertIn("append_projected_dlight_shader_consistency_failures", sweep_source)
        self.assertIn("mismatched projected-light", sweep_source)
        self.assertIn("stale projected-light stream resource ranges", sweep_source)
        self.assertIn("GLX_DLIGHT_PROJECTED_SHADER_MDI_RING_RE", sweep_source)
        self.assertIn("dlightProjectedShaderMdiCommandRingCommits", sweep_source)
        self.assertIn("postShaderFeatures", sweep_source)
        self.assertIn("postShaderDirectFinalBound", SWEEP_PATH.read_text(encoding="utf-8"))
        self.assertIn("postOutputExecutableNodes", SWEEP_PATH.read_text(encoding="utf-8"))
        self.assertIn("GLX_RenderIR_HashMaterialParameterBlock", glx_ir)
        self.assertIn("GLX_RenderIR_HashPostNode", glx_ir)
        self.assertIn("GLX_RenderIR_HashOutputTransform", glx_ir)
        self.assertIn("GLX_Material_StatePlanForTierAndParameterBlock", glx_material_key)
        self.assertIn("material parameter blocks", glx_material)
        self.assertIn("lastParameterBlockHash", glx_material)
        self.assertIn("lastPostNodeHash", glx_module)
        self.assertIn("lastOutputTransformHash", glx_module)
        self.assertIn("MATERIAL_PARAMETER_BLOCKS_RE", SWEEP_PATH.read_text(encoding="utf-8"))
        self.assertIn("GLX_DLIGHT_PROJECTED_SHADER_INPUTS_RE", SWEEP_PATH.read_text(encoding="utf-8"))
        self.assertIn("GLX_DLIGHT_PROJECTED_SHADER_UNIFORMS_RE", SWEEP_PATH.read_text(encoding="utf-8"))
        self.assertIn("GLX_PROJECTED_DLIGHT_SHADER_PARITY_SUITE", sweep_source)
        self.assertIn("projected_dlight_shader_parity_evidence", sweep_source)
        self.assertIn("evaluate_projected_dlight_shader_parity", sweep_source)
        self.assertIn("projected-dlight-legacy-fallback", sweep_source)

        dynamic_projected_start = glx_module.index(
            "qboolean RendererModule::DrawElementsClassifiedProjectedDlights"
        )
        dynamic_array_start = glx_module.index(
            "qboolean RendererModule::DrawArraysClassified", dynamic_projected_start
        )
        dynamic_projected = glx_module[dynamic_projected_start:dynamic_array_start]
        self.assertIn("qboolean projectedShaderBound = qfalse;", dynamic_projected)
        self.assertIn(
            "projectedShaderBound = GLX_Dlight_BindProjectedShaderInput( &dlight_,",
            dynamic_projected,
        )
        self.assertIn("&projectedExecutionPlan", dynamic_projected)
        self.assertIn(
            "if ( !projectedShaderBound && GLX_Dlight_CurrentProgram( &dlight_ ) ) {\n"
            "\t\t\tGLX_Dlight_ClearProjectedShaderInput( &dlight_ );\n"
            "\t\t}",
            dynamic_projected,
        )
        self.assertIn("return qfalse;", dynamic_projected)
        self.assertIn(
            "} else if ( GLX_Dlight_CurrentProgram( &dlight_ ) ) {\n"
            "\t\tGLX_Dlight_ClearProjectedShaderInput( &dlight_ );\n"
            "\t}",
            dynamic_projected,
        )
        dynamic_array_end = glx_module.index(
            "void RendererModule::RecordDraw", dynamic_array_start
        )
        dynamic_array = glx_module[dynamic_array_start:dynamic_array_end]
        self.assertIn(
            "if ( GLX_Dlight_CurrentProgram( &dlight_ ) ) {\n"
            "\t\tGLX_Dlight_ClearProjectedShaderInput( &dlight_ );\n"
            "\t}\n"
            "\tif ( !GLX_Executor_ExecuteDynamicDraw",
            dynamic_array,
        )
        static_run_start = glx_module.index(
            "qboolean RendererModule::StaticWorldDrawDeviceRun"
        )
        static_runs_start = glx_module.index(
            "qboolean RendererModule::StaticWorldDrawDeviceRuns", static_run_start
        )
        static_run = glx_module[static_run_start:static_runs_start]
        self.assertIn(
            "BindStaticWorldProjectedDlightRun( projectedPacket, projectedShaderInput,\n"
            "\t\t\t&projectedExecutionPlan );",
            static_run,
        )
        self.assertIn("replacementPlan.legacyFallback", static_run)
        self.assertIn("return qfalse;", static_run)

        static_filtered_start = glx_module.index(
            "int RendererModule::StaticWorldDrawDeviceRunsFiltered"
        )
        static_filtered_end = glx_module.index(
            "void RendererModule::RecordStaticWorldQueue", static_filtered_start
        )
        static_filtered = glx_module[static_filtered_start:static_filtered_end]
        self.assertIn("StaticWorldDrawProjectedDlightRunsFiltered", static_filtered)
        self.assertIn(
            "if ( GLX_Dlight_CurrentProgram( &dlight_ ) &&\n"
            "\t\tGLX_Dlight_ProjectedProgramEnabled( dlight_ ) && counts && firstItems &&\n"
            "\t\titemCounts )",
            static_filtered,
        )
        self.assertIn(
            "PrepareStaticWorldProjectedDlightRun( firstItems[i],\n"
            "\t\t\t\titemCounts[i], &projectedPacket, &projectedShaderInput )",
            static_filtered,
        )
        self.assertIn("return 0;", static_filtered)

    def test_keep_window_renderer_restart_reinitializes_glx_caps(self) -> None:
        source = (ROOT / "code" / "renderer" / "tr_init.c").read_text(encoding="utf-8")
        init_start = source.index("static void InitOpenGL")
        first_init = source.index("if ( glConfig.vidWidth == 0 )", init_start)
        keep_window_start = source.index("else", first_init)
        keep_window_end = source.index("if ( !qglViewport )", keep_window_start)
        keep_window_block = source[keep_window_start:keep_window_end]

        self.assertIn("GLX_CompatOnOpenGLReady( &glConfig, gl_extensions );", keep_window_block)

    def test_task_t_release_proof_policy_sources_are_covered(self) -> None:
        sweep_script = SWEEP_PATH.read_text(encoding="utf-8")
        release_script = (ROOT / "scripts" / "release.py").read_text(encoding="utf-8")
        workflow = (ROOT / ".github" / "workflows" / "glx-verification.yml").read_text(encoding="utf-8")
        rc_gates = (ROOT / "docs" / "fnql" / "GLX_RC_GATES.md").read_text(encoding="utf-8")

        for text in (
            "GLX_BLOCKING_RELEASE_PLATFORMS",
            "GLX_RELEASE_REQUIRED_GATES",
            "validate_release_proof_root",
            "GLX_VISUAL_DOSSIER_VERSION",
            "glx_visual_dossier",
            "visualDossier",
            "GLX_SWITCH_LIFECYCLE_VERSION",
            "renderer_switch_lifecycle_evidence",
            "evaluate_renderer_switch_lifecycle",
            "rendererSwitchEvidence",
            "GLX_WORLD_PROOF_VERSION",
            "world_proof_evidence",
            "evaluate_world_proof",
            "worldProofEvidence",
            "GLX_MATERIAL_PROOF_VERSION",
            "material_proof_evidence",
            "evaluate_material_proof",
            "materialProofEvidence",
            "GLX_DYNAMIC_PROOF_VERSION",
            "dynamic_proof_evidence",
            "evaluate_dynamic_proof",
            "dynamicProofEvidence",
            "GLX_POST_PROOF_VERSION",
            "post_proof_evidence",
            "evaluate_post_proof",
            "postProofEvidence",
            "GLX_OWNERSHIP_PROOF_VERSION",
            "ownership_proof_evidence",
            "ownershipProofEvidence",
            "proofPlatform",
            "performance_budget_tier",
            "gpuFrameMs",
            "staticDrawPacketMisses",
            "streamSameFrameWrapRejects",
        ):
            self.assertIn(text, sweep_script)
        self.assertIn("--glx-proof-root", release_script)
        self.assertIn("resolve_glx_runtime_proof", release_script)
        self.assertIn("glx_runtime_proof", release_script)
        self.assertIn("cron: '35 4 * * 1'", workflow)
        self.assertIn("FNQL_GLX_PROOF_PLATFORM", workflow)
        self.assertIn("Release Proof Root", rc_gates)

    def test_task_v_parity_suite_sources_are_covered(self) -> None:
        sweep_script = SWEEP_PATH.read_text(encoding="utf-8")
        corpus_doc = (ROOT / glx_runtime_sweep.GLX_PROOF_CORPUS_DOC).read_text(encoding="utf-8")
        rc_gates = (ROOT / "docs" / "fnql" / "GLX_RC_GATES.md").read_text(encoding="utf-8")

        for text in (
            "GLX_PARITY_SUITES",
            "GLX_GATE_PARITY_SUITES",
            "GLX_PARITY_SUITE_VERSION",
            "required_parity_suites",
            "paritySuiteVersion",
            "paritySuiteIds",
            "paritySuites",
            "cg_shadows",
            "r_celShading",
            "r_greyscale",
            "r_renderScale",
        ):
            self.assertIn(text, sweep_script)

        for suite_id in (
            "screenshot",
            "demo-playback",
            "hud",
            "shadow",
            "bloom",
            "cel-shading",
            "greyscale",
            "render-scale",
        ):
            self.assertIn(f"`{suite_id}`", corpus_doc)
            self.assertIn(suite_id, rc_gates)

    def test_task_w_promotion_policy_sources_are_covered(self) -> None:
        promotion_script = PROMOTION_PATH.read_text(encoding="utf-8")
        release_script = (ROOT / "scripts" / "release.py").read_text(encoding="utf-8")
        workflow = (ROOT / ".github" / "workflows" / "glx-verification.yml").read_text(encoding="utf-8")
        promotion_doc = (ROOT / "docs" / "fnql" / "GLX_PROMOTION.md").read_text(encoding="utf-8")
        final_contract = (ROOT / "docs" / "fnql" / "GLX_FINAL_CONTRACT.md").read_text(encoding="utf-8")

        for text in (
            "PROMOTION_REQUIRED_TIERS",
            "PROMOTION_OWNERSHIP_PROFILE",
            "GLX_OWNERSHIP_PROOF_VERSION",
            "check_feature_matrix",
            "check_release_proof_root",
            "check_ownership_proof",
            "check_renderer_source_policy",
            "check_legacy_coupling_inventory",
            "check_rollback_package_metadata",
            "PROMOTION_LEGACY_RENDERER_SOURCE_BUDGET",
            "PROMOTION_ROLLBACK_METADATA_VERSION",
            "policyViolation",
        ):
            self.assertIn(text, promotion_script)

        self.assertIn("promotion_report", release_script)
        self.assertIn("glx_promotion", release_script)
        self.assertIn("--glx-rollback-metadata", release_script)
        self.assertIn("glx_rollback_package", release_script)
        self.assertIn("attach_glx_rollback_archives", release_script)
        self.assertIn("scripts/glx_promotion.py", workflow)
        self.assertIn("glx-promotion.json", workflow)
        self.assertIn("GLX_ROLLBACK_PACKAGE.md", workflow)
        self.assertIn("GLX_VISUAL_DOSSIER.md", workflow)
        for text in (
            "Migration Alias Plan",
            "OpenGL2 Legacy Flag Plan",
            "Rollback Package Contract",
            "Rollback Package Metadata",
            "Legacy Coupling Ledger",
        ):
            self.assertIn(text, promotion_doc)
        self.assertIn("scripts/glx_promotion.py --require-ready --proof-root <dir> --rollback-metadata <json>", final_contract)

    def test_task_x_productization_sources_are_covered(self) -> None:
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        meson_options = (ROOT / "meson_options.txt").read_text(encoding="utf-8")
        meson_build = (ROOT / "meson.build").read_text(encoding="utf-8")
        build_doc = (ROOT / "BUILD.md").read_text(encoding="utf-8")
        display_doc = (ROOT / "docs" / "DISPLAY.md").read_text(encoding="utf-8")
        screenshots_doc = (ROOT / "docs" / "SCREENSHOTS.md").read_text(encoding="utf-8")
        glx_doc = (ROOT / "docs" / "GLX.md").read_text(encoding="utf-8")
        renderer_doc = (ROOT / "docs" / "fnql" / "GLX_RENDERER.md").read_text(encoding="utf-8")
        readme_template = (ROOT / "docs" / "templates" / "README.md.in").read_text(encoding="utf-8")
        release_script = (ROOT / "scripts" / "release.py").read_text(encoding="utf-8")

        self.assertRegex(makefile, r"(?m)^USE_GLX\s*=\s*1$")
        self.assertIn("'glx'", meson_options)
        self.assertIn("renderer_prefix + '_glx_'", meson_build)
        self.assertIn("renderer_gl_src + renderer_glx_src", meson_build)
        self.assertIn("single client executable", build_doc)
        for text in (
            "canonical OpenGL-lineage renderer",
            "GLx Renderer Guide",
        ):
            self.assertIn(text, glx_doc)
        self.assertIn("troubleshooting", glx_doc.lower())
        self.assertIn("Canonical OpenGL-lineage renderer", display_doc)
        self.assertIn("canonical OpenGL-lineage renderer", renderer_doc)
        self.assertIn("docs/GLX.md", readme_template)
        self.assertIn('ROOT / "docs" / "GLX.md"', release_script)
        for current_text in (build_doc, display_doc, screenshots_doc, renderer_doc, glx_doc):
            self.assertNotRegex(current_text, r"(?i)\bexperimental\s+glx\b")
            self.assertNotRegex(current_text, r"(?i)glx\s+is\s+an\s+experimental")


class GlxRuntimeSweepImageTests(unittest.TestCase):
    def test_png_round_trip_and_exact_compare(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / "baseline.png"
            pixels = bytes(
                (
                    255, 0, 0, 255,
                    0, 255, 0, 255,
                    0, 0, 255, 255,
                    255, 255, 255, 255,
                )
            )

            glx_runtime_sweep.write_png_rgba(path, 2, 2, pixels)
            width, height, decoded = glx_runtime_sweep.read_png_rgba(path)
            self.assertEqual((width, height), (2, 2))
            self.assertEqual(decoded, pixels)

            comparison = glx_runtime_sweep.compare_png_files(path, path, 0.0, 0.0)
            self.assertEqual(comparison["status"], "passed")
            self.assertEqual(comparison["changedPixels"], 0)
            self.assertEqual(comparison["psnr"], "inf")
            self.assertEqual(comparison["ssim"], 1.0)

    def test_screenshot_results_attach_color_histogram(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            screenshot = root / "baseq3" / "screenshots" / "proof.png"
            glx_runtime_sweep.write_png_rgba(
                screenshot,
                1,
                1,
                bytes((10, 20, 30, 255)),
            )

            results = glx_runtime_sweep.screenshot_results(
                root,
                "",
                [
                    {
                        "name": "proof",
                    }
                ],
            )

            histogram = results[0]["histogram"]
            self.assertEqual(histogram["status"], "passed")
            self.assertEqual(histogram["meanRed"], 10.0)
            self.assertEqual(histogram["meanGreen"], 20.0)
            self.assertEqual(histogram["meanBlue"], 30.0)
            self.assertTrue(Path(results[0]["histogramPath"]).exists())
            self.assertEqual(results[0]["falseColor"]["status"], "passed")
            false_color_path = Path(results[0]["falseColorPath"])
            self.assertTrue(false_color_path.exists())
            width, height, _pixels = glx_runtime_sweep.read_png_rgba(false_color_path)
            self.assertEqual((width, height), (1, 1))
            self.assertEqual(results[0]["exposureFalseColor"]["status"], "passed")
            exposure_path = Path(results[0]["exposureFalseColorPath"])
            self.assertTrue(exposure_path.exists())
            width, height, _pixels = glx_runtime_sweep.read_png_rgba(exposure_path)
            self.assertEqual((width, height), (1, 1))
            self.assertEqual(results[0]["capturePolicy"]["defaultColorSpace"], "sdr-srgb")
            self.assertEqual(results[0]["capturePolicy"]["hdrExportStatus"], "explicit-reserved")

    def test_png_compare_reports_threshold_failure_and_writes_diff(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            baseline = root / "baseline.png"
            candidate = root / "candidate.png"
            diff = root / "diff.png"

            glx_runtime_sweep.write_png_rgba(
                baseline,
                1,
                1,
                bytes((20, 40, 60, 255)),
            )
            glx_runtime_sweep.write_png_rgba(
                candidate,
                1,
                1,
                bytes((21, 40, 60, 255)),
            )

            comparison = glx_runtime_sweep.compare_png_files(
                baseline,
                candidate,
                max_rms=0.0,
                max_pixel_ratio=0.0,
                diff_path=diff,
            )
            self.assertEqual(comparison["status"], "failed")
            self.assertEqual(comparison["changedPixels"], 1)
            self.assertIn("psnr", comparison)
            self.assertIn("ssim", comparison)
            self.assertTrue(diff.exists())

    def test_csm_shimmer_screenshot_diff_summary_compares_path_frames(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            baseline = root / "baseline.png"
            candidate = root / "candidate.png"
            glx_runtime_sweep.write_png_rgba(
                baseline,
                2,
                1,
                bytes((64, 96, 128, 255, 32, 32, 32, 255)),
            )
            glx_runtime_sweep.write_png_rgba(
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
                    "baselineKey": "glx-csm-baseline",
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
                        "baselineKey": f"glx-csm-{step}",
                    }
                )

            summary = glx_runtime_sweep.apply_csm_shimmer_screenshot_diffs(
                screenshots,
                root / "diffs",
            )

            self.assertEqual(summary["status"], "passed")
            self.assertEqual(summary["comparisonCount"], 3)
            self.assertEqual(screenshots[1]["csmShimmerComparison"]["status"], "passed")
            self.assertTrue(Path(screenshots[1]["csmShimmerComparison"]["diffPath"]).exists())

    def test_shader_reference_ramps_are_deterministic_offline_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            records = glx_runtime_sweep.write_shader_reference_ramps(root)

            self.assertEqual(len(records), len(glx_runtime_sweep.GLX_COLOR_SWEEP_MATRIX))
            first = records[0]
            self.assertTrue(Path(first["path"]).exists())
            self.assertTrue(Path(first["histogramPath"]).exists())
            self.assertTrue(Path(first["falseColorPath"]).exists())
            self.assertTrue(Path(first["exposureFalseColorPath"]).exists())
            width, height, _pixels = glx_runtime_sweep.read_png_rgba(Path(first["path"]))
            self.assertEqual((width, height), (
                glx_runtime_sweep.GLX_SHADER_REFERENCE_RAMP_WIDTH,
                glx_runtime_sweep.GLX_SHADER_REFERENCE_RAMP_HEIGHT,
            ))

            failures = glx_runtime_sweep.validate_image_evidence_manifest(
                {
                    "imageEvidence": {
                        "version": glx_runtime_sweep.GLX_IMAGE_EVIDENCE_VERSION,
                        "requiredSidecars": list(glx_runtime_sweep.GLX_IMAGE_EVIDENCE_SIDECARS),
                        "shaderReferenceRamps": records,
                    }
                }
            )
            self.assertEqual(failures, [])

    def test_shader_reference_ramp_compare_reports_psnr_and_ssim(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            row_id = str(glx_runtime_sweep.GLX_COLOR_SWEEP_MATRIX[0]["id"])
            reference = glx_runtime_sweep.write_shader_reference_ramp(
                root / "candidate-source",
                dict(glx_runtime_sweep.GLX_COLOR_SWEEP_MATRIX[0]),
            )

            result = glx_runtime_sweep.compare_shader_reference_ramp(
                row_id,
                Path(reference["path"]),
                root / "comparison",
                max_rms=0.0,
                max_pixel_ratio=0.0,
            )

            self.assertEqual(result["status"], "passed")
            self.assertEqual(result["comparison"]["psnr"], "inf")
            self.assertEqual(result["comparison"]["ssim"], 1.0)

    def test_screenshot_baseline_approval_then_compare(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            candidate = root / "capture.png"
            baseline_dir = root / "baselines"
            glx_runtime_sweep.write_png_rgba(
                candidate,
                1,
                1,
                bytes((10, 20, 30, 255)),
            )
            screenshots = [
                {
                    "name": "capture",
                    "baselineKey": "profile-map-round-step-renderer",
                    "path": str(candidate),
                    "found": True,
                }
            ]

            glx_runtime_sweep.apply_screenshot_baselines(
                screenshots,
                baseline_dir,
                approve_baselines=True,
                diff_dir=None,
                max_rms=0.0,
                max_pixel_ratio=0.0,
            )
            self.assertEqual(screenshots[0]["baselineStatus"], "approved")
            self.assertTrue((baseline_dir / "profile-map-round-step-renderer.png").exists())

            glx_runtime_sweep.apply_screenshot_baselines(
                screenshots,
                baseline_dir,
                approve_baselines=False,
                diff_dir=root / "diffs",
                max_rms=0.0,
                max_pixel_ratio=0.0,
            )
            self.assertEqual(screenshots[0]["baselineStatus"], "passed")
            self.assertEqual(screenshots[0]["comparison"]["status"], "passed")

    def test_projected_dlight_shader_baseline_approval_uses_legacy_capture(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            legacy = root / "legacy.png"
            candidate = root / "candidate.png"
            baseline_dir = root / "baselines"
            glx_runtime_sweep.write_png_rgba(
                legacy,
                1,
                1,
                bytes((10, 20, 30, 255)),
            )
            glx_runtime_sweep.write_png_rgba(
                candidate,
                1,
                1,
                bytes((200, 210, 220, 255)),
            )
            screenshots = [
                {
                    "name": "legacy",
                    "path": str(legacy),
                    "found": True,
                    "skipExternalBaseline": True,
                    "projectedDlightShaderParityRole": (
                        glx_runtime_sweep.GLX_PROJECTED_DLIGHT_SHADER_PARITY_LEGACY_ROLE
                    ),
                },
                {
                    "name": "candidate",
                    "baselineKey": "projected-dlight-baseline",
                    "path": str(candidate),
                    "found": True,
                    "baselineSourceName": "legacy",
                    "projectedDlightShaderParityRole": (
                        glx_runtime_sweep.GLX_PROJECTED_DLIGHT_SHADER_PARITY_CANDIDATE_ROLE
                    ),
                },
            ]

            glx_runtime_sweep.apply_screenshot_baselines(
                screenshots,
                baseline_dir,
                approve_baselines=True,
                diff_dir=None,
                max_rms=0.0,
                max_pixel_ratio=0.0,
            )
            baseline_path = baseline_dir / "projected-dlight-baseline.png"

            self.assertEqual(screenshots[0]["baselineStatus"], "reference")
            self.assertEqual(screenshots[1]["baselineStatus"], "approved")
            self.assertEqual(baseline_path.read_bytes(), legacy.read_bytes())

            glx_runtime_sweep.apply_screenshot_baselines(
                screenshots,
                baseline_dir,
                approve_baselines=False,
                diff_dir=root / "diffs",
                max_rms=255.0,
                max_pixel_ratio=1.0,
            )
            self.assertEqual(screenshots[1]["baselineStatus"], "passed")
            self.assertEqual(screenshots[1]["comparison"]["baselinePath"], str(baseline_path))

    def test_projected_dlight_shader_same_run_legacy_diff(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            legacy = root / "legacy.png"
            candidate = root / "candidate.png"
            glx_runtime_sweep.write_png_rgba(
                legacy,
                1,
                1,
                bytes((10, 20, 30, 255)),
            )
            glx_runtime_sweep.write_png_rgba(
                candidate,
                1,
                1,
                bytes((10, 20, 31, 255)),
            )
            screenshots = [
                {
                    "name": "legacy",
                    "path": str(legacy),
                    "found": True,
                    "projectedDlightShaderParityRole": (
                        glx_runtime_sweep.GLX_PROJECTED_DLIGHT_SHADER_PARITY_LEGACY_ROLE
                    ),
                },
                {
                    "name": "candidate",
                    "baselineKey": "projected-dlight-baseline",
                    "path": str(candidate),
                    "found": True,
                    "legacyFallbackCaptureName": "legacy",
                    "projectedDlightShaderParityRole": (
                        glx_runtime_sweep.GLX_PROJECTED_DLIGHT_SHADER_PARITY_CANDIDATE_ROLE
                    ),
                },
            ]

            summary = glx_runtime_sweep.apply_projected_dlight_shader_parity_diffs(
                screenshots,
                root / "diffs",
                max_rms=2.0,
                max_pixel_ratio=1.0,
            )

            self.assertEqual(summary["status"], "passed")
            self.assertEqual(summary["comparisonCount"], 1)
            self.assertEqual(screenshots[1]["legacyFallbackComparison"]["status"], "passed")
            self.assertTrue(
                (root / "diffs" / "projected-dlight-baseline.legacy-fallback.diff.png").exists()
            )

    def test_visual_dossier_summarizes_review_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            manifest = release_proof_manifest("rc-parity", "windows-x64")
            manifest_path = Path(tmp) / "manifest.json"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            dossier = glx_runtime_sweep.glx_visual_dossier(manifest, manifest_path)

            self.assertIn("GLx Visual Dossier", dossier)
            self.assertIn("Current Pipeline Flow", dossier)
            self.assertIn("Target Pipeline Flow", dossier)
            self.assertIn("Backend State Overlay", dossier)
            self.assertIn("Driver Tier Matrix", dossier)
            self.assertIn("Histogram And False-Color Evidence", dossier)
            self.assertIn("Parity Diff Sheet", dossier)
            self.assertIn("SDR/HDR Color Sweep Review", dossier)
            self.assertIn("scene-linear", dossier)
            self.assertIn("shader-srgb", dossier)


class GlxRuntimeSweepDiagnosticTests(unittest.TestCase):
    def write_projected_dlight_shader_profile_log(
        self,
        log: Path,
        *,
        binds: int = 1,
        failures: int = 0,
        executable: int = 1,
        suppressed: int = 0,
        world_inputs: int = 1,
        dynamic_inputs: int = 1,
        world_executable: int = 1,
        world_binds: int = 1,
        dynamic_executable: int | None = None,
        dynamic_binds: int | None = None,
        uniform_records: int | None = None,
        truncated: int = 0,
        limit_suppressed: int = 0,
        render_ir_projected_world: int | None = None,
        render_ir_projected_dynamic: int | None = None,
        resource_attempts: int = 0,
        resource_binds: int = 0,
        resource_executable: int = 0,
        resource_suppressed: int = 0,
        resource_promotions: int = 0,
        resource_failures: int = 0,
        resource_records: int = 0,
        resource_world_executable: int = 0,
        resource_dynamic_executable: int = 0,
        stream_failures: int = 0,
        stream_range_binds: int = 0,
        stream_range_failures: int = 0,
        stream_range_clears: int = 0,
        arena_range_binds: int = 0,
        arena_range_failures: int = 0,
        arena_range_clears: int = 0,
    ) -> None:
        if dynamic_executable is None:
            dynamic_executable = executable
        if dynamic_binds is None:
            dynamic_binds = binds
        if render_ir_projected_world is None:
            render_ir_projected_world = world_inputs
        if render_ir_projected_dynamic is None:
            render_ir_projected_dynamic = dynamic_inputs
        input_count = world_inputs + dynamic_inputs
        if uniform_records is None:
            uniform_records = input_count
        log.write_text(
            "\n".join(
                [
                    f"  GLx pass schedule: valid {glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE_COUNT}/{glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE_HASH} {glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE}",
                    f"  render IR products: passes 1, world packets 1, projected world packets {render_ir_projected_world}/{render_ir_projected_world} lights, dynamic draws 1, projected dynamic draws {render_ir_projected_dynamic}/{render_ir_projected_dynamic} lights, materials 1, uploads 0, post nodes 0, outputs 0, rejects 0",
                    "  render IR dynamic roles: generic 0/0/0, dlight 1/6/0, shadow 0/0/0, beam 0/0/0, post 0/0/0",
                    "  render IR dynamic passes: dlight 1/6/0, scene 0/0/0, post 0/0/0, other 0/0/0",
                    f"  dlight projected shader inputs: inputs {input_count}/{input_count} records, world {world_inputs}, dynamic {dynamic_inputs}, programmable {input_count}, fallback 0, invalid 0",
                    f"  dlight projected shader uniforms: attempts 1, binds {binds}, failures {failures}, records {uniform_records}, truncated {truncated}, executable {executable}, suppressed {suppressed}, world {world_executable}/{world_binds}, dynamic {dynamic_executable}/{dynamic_binds}, limit-suppressed {limit_suppressed}, limit 3",
                    f"  dlight projected shader resource: attempts {resource_attempts}, binds {resource_binds}, executable {resource_executable}, suppressed {resource_suppressed}, promotions {resource_promotions}, failures {resource_failures}, records {resource_records}, world {resource_world_executable}, dynamic {resource_dynamic_executable}, binding 5",
                    f"  dlight projected shader stream: attempts 0, uploads 0, failures {stream_failures}, skipped 0, records 0, bytes 0, persistent 0, world 0, dynamic 0, range {stream_range_binds}/{max(stream_range_binds, stream_range_failures)}, range failures {stream_range_failures}, clears {stream_range_clears}, last 0/0",
                    f"  dlight projected shader arena: reserves 0, uploads 0, failures 0, wraps 0, rejects 0, waits 0, timeouts 0, sync failures 0, bytes 0, light records 0, list records 0, world records 0, dynamic records 0, range {arena_range_binds}/{max(arena_range_binds, arena_range_failures)}, range failures {arena_range_failures}, clears {arena_range_clears}, authoritative 0/0, auth failures 0, auth fallbacks 0, auth clears 0, bound {'yes' if arena_range_binds > arena_range_clears else 'no'}, cursor 0, last 0/0/0",
                    "  dlight projected shader MDI commands: attempts 0, eligible 0, uploads 0, failures 0, skipped 0, records 0, indexes 0, bytes 0, last 0/0",
                    "  dlight projected shader MDI command ring: reserves 0, commits 0, wraps 0, failures 0, slots 256, cursor 0, last 0/0/0/0",
                    "  dlight projected shader MDI submit: attempts 0, plans 0, ready 0, fallbacks 0, skipped 0, records 0, indexes 0, buffer 0, last 0/0",
                    "  dlight projected shader MDI batch: attempts 0, batches 0, ready 0, fallbacks 0, rejects 0, gl errors 0, records 0, indexes 0, submitted 0/0, largest 0, reject 0, buffer 0, range 0/0",
                    "  dynamic stream categories: entity 0/0, particle 0/0, poly 0/0, mark 0/0, weapon 0/0, ui 0/0, beam 0/0, dlight 1/1, special 0/0",
                    "  static world GLx renderer: yes, arena upload yes, arena draw yes",
                    "  static world GLx packet batches: yes, attempts 1, batches 1, packet runs 1/3 indexes, fallback runs 0, singles 0",
                ]
            ),
            encoding="utf-8",
        )

    def write_projected_dlight_mdi_profile_log(
        self,
        log: Path,
        *,
        command_failures: int = 0,
        arena_uploads: int = 1,
        arena_failures: int = 0,
        arena_rejects: int = 0,
        arena_timeouts: int = 0,
        arena_sync_failures: int = 0,
        arena_light_records: int = 1,
        arena_list_records: int = 1,
        arena_range_binds: int = 1,
        arena_range_failures: int = 0,
        arena_authoritative_binds: int = 1,
        arena_authoritative_failures: int = 0,
        arena_authoritative_fallbacks: int = 0,
        ring_commits: int = 1,
        ring_failures: int = 0,
        submit_ready: int = 1,
        submit_fallbacks: int = 0,
        batch_ready: int = 1,
        batch_fallbacks: int = 0,
        batch_rejects: int = 0,
        batch_gl_errors: int = 0,
        submitted_draws: int = 1,
        submitted_indexes: int = 6,
    ) -> None:
        log.write_text(
            "\n".join(
                [
                    "  product tier: GL46",
                    "  GL46 high-end executor: active yes, persistent uploads yes, buffer storage uploads yes, sync-heavy streaming yes, direct state access yes, multi-draw indirect yes, aggressive static-world submission yes, detailed GPU counters yes",
                    "  GL46 high-end support: material compiler yes, common materials yes, dynamic entities yes, modern post chain yes, scene-linear output yes, hardware HDR output yes, screenshots yes, demos yes",
                    "  GL46 high-end requirements: debug output yes, buffer storage yes, direct state access yes, multi-draw indirect yes",
                    f"  GLx pass schedule: valid {glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE_COUNT}/{glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE_HASH} {glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE}",
                    "  render IR products: passes 1, world packets 1, projected world packets 0/0 lights, dynamic draws 1, projected dynamic draws 1/1 lights, materials 1, uploads 0, post nodes 0, outputs 0, rejects 0",
                    "  render IR dynamic roles: generic 0/0/0, dlight 1/6/0, shadow 0/0/0, beam 0/0/0, post 0/0/0",
                    "  render IR dynamic passes: dlight 1/6/0, scene 0/0/0, post 0/0/0, other 0/0/0",
                    "  dlight projected shader inputs: inputs 1/1 records, world 0, dynamic 1, programmable 1, fallback 0, invalid 0",
                    "  dlight projected shader uniforms: attempts 1, binds 1, failures 0, records 1, truncated 0, executable 1, suppressed 0, world 0/0, dynamic 1/1, limit-suppressed 0, limit 3",
                    "  dlight projected shader resource: attempts 0, binds 0, executable 0, suppressed 0, promotions 0, failures 0, records 0, world 0, dynamic 0, binding 5",
                    "  dlight projected shader stream: attempts 1, uploads 1, failures 0, skipped 0, records 1, bytes 32, persistent 1, world 0, dynamic 1, range 1/1, range failures 0, clears 1, last 1024/32",
                    f"  dlight projected shader arena: reserves 1, uploads {arena_uploads}, failures {arena_failures}, wraps 0, rejects {arena_rejects}, waits 0, timeouts {arena_timeouts}, sync failures {arena_sync_failures}, bytes 32, light records {arena_light_records}, list records {arena_list_records}, world records 0, dynamic records {arena_light_records}, range {arena_range_binds}/1, range failures {arena_range_failures}, clears 1, authoritative {arena_authoritative_binds}/1, auth failures {arena_authoritative_failures}, auth fallbacks {arena_authoritative_fallbacks}, auth clears 1, bound yes, cursor 32, last 11/1024/32",
                    f"  dlight projected shader MDI commands: attempts 1, eligible 1, uploads 1, failures {command_failures}, skipped 0, records 1, indexes 6, bytes 20, last 2048/20",
                    f"  dlight projected shader MDI command ring: reserves 1, commits {ring_commits}, wraps 0, failures {ring_failures}, slots 256, cursor 1, last 0/11/2048/20",
                    f"  dlight projected shader MDI submit: attempts 1, plans 1, ready {submit_ready}, fallbacks {submit_fallbacks}, skipped 0, records 1, indexes 6, buffer 11, last 2048/20",
                    f"  dlight projected shader MDI batch: attempts 1, batches 1, ready {batch_ready}, fallbacks {batch_fallbacks}, rejects {batch_rejects}, gl errors {batch_gl_errors}, records 1, indexes 6, submitted {submitted_draws}/{submitted_indexes}, largest 1, reject 0, buffer 11, range 2048/20",
                    "  dynamic stream categories: entity 0/0, particle 0/0, poly 0/0, mark 0/0, weapon 0/0, ui 0/0, beam 0/0, dlight 1/1, special 0/0",
                    "  static world GLx renderer: yes, arena upload yes, arena draw yes",
                    "  static world GLx packet batches: yes, attempts 1, batches 1, packet runs 1/3 indexes, fallback runs 0, singles 0",
                ]
            ),
            encoding="utf-8",
        )

    def test_glx_diagnostics_accept_clean_rc_log(self) -> None:
        self.assertEqual(glx_runtime_sweep.normalize_tone_map_name("aces"), "aces-fitted")
        self.assertEqual(glx_runtime_sweep.normalize_tone_map_name("reinhard"), "reinhard-simple")

        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "switch.log"
            log.write_text(
                "\n".join(
                    [
                        "  material renderer: enabled, ready yes, GLSL 1.20",
                        "  material compiles: 24 attempts, 0 compile failures, 0 link failures, precache 0/24, bind failures 0, labels 0",
                        "  material fallbacks: unsupported 0, disabled 0, not-ready 0, full 0, discarded without GL delete 0",
                        "  material compiler plans: compiled 12, unsupported 0, last unsupported 0x0 (none)",
                        "  material parameter blocks: blocks 12 invalid 0 hash 0x1234abcd, last sort 0 passes 1 features 0x0 flags 0x0 state 0x0 object rgb 2:0 alpha 1:0 tc 3/0",
                        "  product tier: GL2X",
                        "  GL2X programmable executor: active yes, client-memory fallback yes, stream uploads yes, material compiler yes, postprocess-lite yes, modern post chain no, scene-linear output no",
                        "  GL2X programmable support: common materials yes, dynamic entities yes, lightmaps yes, multitexture yes, fog yes, sprites yes, beams yes, screenshots yes, demos yes",
                        "  glx: ownership legacy delegation 0 calls/0 items, generic 0, vbo-device 0, vbo-soft 0, arrays 0",
                        f"  GLx pass schedule: valid {glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE_COUNT}/{glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE_HASH} {glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE}",
                        "  pass schedule: invalid 0/00000000 none",
                        f"  render IR products: passes {glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE_COUNT}, world packets 1, projected world packets 1/2 lights, dynamic draws 2, projected dynamic draws 1/1 lights, materials 3, uploads 4, post nodes 1, outputs 1, rejects 0",
                        "  render IR dynamic roles: generic 1/12/0, dlight 1/6/0, shadow 0/0/0, beam 0/0/0, post 0/0/0",
                        "  render IR dynamic passes: dlight 1/6/0, scene 1/12/0, post 0/0/0, other 0/0/0",
                        "  dlight projected shader inputs: inputs 2/3 records, world 1, dynamic 1, programmable 2, fallback 0, invalid 0",
                        "  dlight projected shader uniforms: attempts 1, binds 1, failures 0, records 3, truncated 0, executable 0, suppressed 1, world 0/1, dynamic 0/0, limit-suppressed 0, limit 3",
                        "  dlight projected shader resource: attempts 0, binds 0, executable 0, suppressed 0, promotions 0, failures 0, records 0, world 0, dynamic 0, binding 5",
                        "  dlight projected shader stream: attempts 0, uploads 0, failures 0, skipped 0, records 0, bytes 0, persistent 0, world 0, dynamic 0, range 0/0, range failures 0, clears 0, last 0/0",
                        "  dlight projected shader arena: reserves 0, uploads 0, failures 0, wraps 0, rejects 0, waits 0, timeouts 0, sync failures 0, bytes 0, light records 0, list records 0, world records 0, dynamic records 0, range 0/0, range failures 0, clears 0, authoritative 0/0, auth failures 0, auth fallbacks 0, auth clears 0, bound no, cursor 0, last 0/0/0",
                        "  dlight projected shader MDI commands: attempts 0, eligible 0, uploads 0, failures 0, skipped 0, records 0, indexes 0, bytes 0, last 0/0",
                        "  dlight projected shader MDI command ring: reserves 0, commits 0, wraps 0, failures 0, slots 256, cursor 0, last 0/0/0/0",
                        "  dlight projected shader MDI submit: attempts 0, plans 0, ready 0, fallbacks 0, skipped 0, records 0, indexes 0, buffer 0, last 0/0",
                        "  dlight projected shader MDI batch: attempts 0, batches 0, ready 0, fallbacks 0, rejects 0, gl errors 0, records 0, indexes 0, submitted 0/0, largest 0, reject 0, buffer 0, range 0/0",
                        "  post/output ownership: mode legacy-fallback, post nodes 1, outputs 1, legacy fallback yes, executable nodes 0, executable outputs 0, post hash 0x01020304, output hash 0x05060708, plan hash 0x090a0b0c, fallback 0x00000001",
                        "  post shader plan: valid yes, features 0x000000de, hash 0x0badcafe, textures 2, uniforms 12, frames 3, invalid 0",
                        "  post shader cache: ready yes, programs 3/32, plans 4 valid/0 invalid, cache 2 hits/3 misses, compile 3 attempts/0 failures, link failures 0, source failures 0, source hash 0x12345678, program 99",
                        "  post shader direct-final: execute no, eligible yes, bound no, reject 0x00000001, candidates 2, eligible frames 2, attempts 0, binds 0, fallbacks 2, rejects 2, program misses 0, uniform failures 0",
                        "  FBO: requested yes, ready yes, programs yes, framebuffer funcs yes, reason: FBO ready",
                        "  target: render 640x480, capture 640x480, window 640x480, format 0x881a (0x1908:0x140b)",
                        "  controls: scene-linear HDR yes, precision 16, renderScale 1, bloom 2, MSAA no, supersample no, adjusted window yes, greyscale 1.00",
                        "  frames: 3 post, 1 bloom-final, 0 gamma-direct, 3 gamma-blit, 0 minimized output, 1 screenshots",
                        "  frame features: 1 bloom-available, 1 scene-linear, 1 tone-mapped, 1 graded, 1 render-scale, 1 greyscale, 1 window-adjusted, 0 minimized",
                        "  FBO lifecycle: 1 init attempts, 1 ready, 0 failed, 0 disabled, 0 shutdowns",
                        "  color pipeline: space scene-linear, transfer sdr-srgb, tone-map aces-fitted, exposure 1.00, grade lgg-lut3d, paper-white 203 nits, max 203 nits",
                        "  exposure reduction: mode 1, algorithm simple-average, enabled yes, fallback yes, samples 1024/32x32, percentile 80.0, target-luma 0.180, measured-log2 -1.000, measured-luma 0.5000, manual 1.00, scale 0.360, target 0.36, frames 3 histogram/0 simple/3 failures/0",
                        "  color grade stage: mode lgg-lut3d, lift 0.01/0.02/0.03, gamma 1.10/1.00/0.95, gain 1.05/1.00/0.98, white-point 6504->6000 K, lut-size 16, lut-scale 4.00",
                        "  color audit: srgb-decode yes requested yes available yes, framebuffer-srgb no requested yes available yes, capture sdr-srgb, capture-request sdr-srgb, capture-hdr-aware no, capture-supported yes, target-float yes, final-encode shader-srgb, contract yes",
                        "  texture audit: srgb 4 decode 4, linear 2 decode 0, data 2 decode 0, unknown 0 decode 0, missing-srgb-decode 0, unexpected-decode 0",
                        '  glx: color-frame-json {"frame":1,"backend":"sdr-srgb","space":"scene-linear","transfer":"sdr-srgb","exposure":1.0000,"paperWhiteNits":203.0,"maxOutputNits":203.0,"srgbDecode":true,"framebufferSrgb":false,"internalFormat":"0x881a","textureFormat":"0x1908","textureType":"0x140b","sceneTargetFloat":true,"shaderSrgbEncode":true,"contractValid":true}',
                        "  output backend: request auto, selected sdr-srgb, native windows-scrgb, hardware no, experimental no, display-hdr yes, headroom 4.00, sdr-white 203 nits, display-max 812 nits, icc yes/2048, driver windows, display HDR Panel, reason: test",
                        "  display state: queries 4, changes 1, capability 1, backend 1, hdr 1, headroom 1, luminance 1, icc 1, last-frame 2, flags 0x000000fe, hash 0x12345678, previous 0x01020304",
                        "  bloom create: last success, 1/1 ready, texture-unit failures 0, FBO failures 0",
                        "  bloom storage: policy auto, format 0x8c3a (0x1907:0x140b)",
                        "  bloom passes: calls 1, rendered 1, final 1, pre-final 0, skipped 0, failures 0, mode1 0, mode2 1, reflections 0",
                        "  copies/blits: screen-map copies 0, MSAA blits 0 (0 depth), SSAA blits 0, last output bloom-final",
                        "  post pass counters: blits 4, binds 9, clears 1, fullscreen passes 6",
                        "  pass timer queries: active yes, queries 7, unavailable frames 0, ring-full skips 0",
                        "  pass GPU timings: backend=0.270ms/1 postprocess=0.420ms/1 bloom=0.250ms/1 bloom-extract=0.030ms/1 bloom-downscale=0.010ms/3 bloom-blur=0.090ms/2 bloom-blend=0.080ms/1 bloom-final=0.060ms/1 bloom-lens-reflection=n/a/0 gamma-direct=n/a/0 gamma-blit=0.110ms/1 fbo-blit=0.040ms/2 copy-screen=n/a/0 flare=0.020ms/1",
                        "  dynamic stream buffer: yes",
                        "  dynamic stream sync: yes, fences 1, waits 0, timeouts 0, failures 0, pending skips 0",
                        "  dynamic stream reservations: 1, commits: 1, wraps: 0, same-frame wrap rejects: 0, orphans: 0",
                        "  dynamic stream uploads: 1 calls, 0.01 MB, failures 0",
                        "  dynamic stream binding cache: queries 1, hits 3, restores 2, invalidations 1, external 4, array known no buffer 0, element known no buffer 0",
                        "  dynamic stream draws: 1/1 attempts, 3 verts, 3 indexes, 0.01 MB, index 0.01 MB, tex1 0.00 MB, mt 0, fog 0, depthfrag 0, texmod 0, env 0, dlight 1, screen 0, video 0, shadow 0, beam 0, post 0, fallbacks 0",
                        "  dynamic stream dynamic-light telemetry: attempts 2, draws 1, fallbacks 0, attempt 0.03 MB, draw 0.02 MB, index 0.01 MB, tex1 0.00 MB, wraps 1, same-frame rejects 0, waits 1, timeouts 0, sync failures 0",
                        "  dynamic stream categories: entity 1/1, particle 0/0, poly 0/0, mark 0/0, weapon 0/0, ui 0/0, beam 0/0, dlight 1/1, special 0/0",
                        "  dynamic stream category fallbacks: entity 0, particle 0, poly 0, mark 0, weapon 0, ui 0, beam 0, dlight 0, special 0",
                        "  dynamic stream IR roles: generic 1/1/0, dlight 1/1/0, shadow 0/0/0, beam 0/0/0, post 0/0/0",
                        "  dynamic stream draw skips: 2 (bind 0, input 0, mt 0, depthfrag 0, texcoord 0, empty 0, key 1, fog 1, program 0)",
                        "  dynamic stream material compiler: rejected 0, last unsupported 0x0 (none)",
                        "  dynamic stream multitexture gate: yes, accepted 2, rejected 0",
                        "  dynamic stream depth-fragment gate: yes, accepted 1, rejected 0",
                        "  dynamic stream reservation failures: 0",
                        "  dlight program compact: active yes, programs 2, availability 3/4, binds 5/6, failures 0, creates 2, cache hits 7",
                        "  dlight state compact: legacy passes 2, texture binds 3, fog textures 4, shadow textures 5/1, shadow fbo 6/7, state changes 8",
                        "  dlight build compact: legacy lights 9/10, no-hit 2, verts 300, indexes 600/450, pm 3, pm verts/indexes 240/360",
                        "  dlight cull compact: legacy verts 40, indexes 90",
                        "  dlight scissor compact: active yes, candidates 4, computed 3, applied 2, fallbacks 1, pixels 1200/4800",
                        "  static world GLx renderer: yes, arena upload yes, arena draw yes",
                        "  static world GLx arena: yes, builds 1, skips 0, failures 0, binds v1/i1, draw skips 0, 1.00 MB",
                        "  static world GLx packet batches: yes, attempts 1, batches 1, packet runs 1/3 indexes, fallback runs 0, singles 0",
                        "  static world GLx multidraw indirect: yes, 0/0 calls, 0 runs, 0 indexes, fallbacks 0, skips 0, errors 0, largest 0",
                    ]
                ),
                encoding="utf-8",
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-parity")
            self.assertTrue(diagnostics["found"])
            self.assertEqual(diagnostics["failures"], [])
            self.assertEqual(diagnostics["metrics"]["materialParameters"]["blocks"], 12)
            self.assertEqual(diagnostics["metrics"]["materialParameters"]["invalid"], 0)
            self.assertEqual(diagnostics["metrics"]["materialParameters"]["hash"], 0x1234ABCD)
            self.assertEqual(diagnostics["metrics"]["materialParameters"]["tcGen0"], 3)
            self.assertEqual(diagnostics["metrics"]["materialParameters"]["tcGen1"], 0)
            self.assertEqual(diagnostics["metrics"]["materialParameters"]["features"], 0)
            self.assertEqual(diagnostics["metrics"]["postOutputOwnership"]["legacyFallback"], 1)
            self.assertEqual(diagnostics["metrics"]["postOutputOwnership"]["executableNodes"], 0)
            self.assertEqual(diagnostics["metrics"]["postOutputOwnership"]["executableOutputs"], 0)
            self.assertEqual(diagnostics["metrics"]["postOutputOwnership"]["postHash"], 0x01020304)
            self.assertEqual(diagnostics["metrics"]["postOutputOwnership"]["outputHash"], 0x05060708)
            self.assertEqual(diagnostics["metrics"]["postOutputOwnership"]["planHash"], 0x090A0B0C)
            self.assertEqual(diagnostics["metrics"]["postOutputOwnership"]["fallbackMask"], 1)
            self.assertEqual(diagnostics["metrics"]["postShaderPlan"]["valid"], 1)
            self.assertEqual(diagnostics["metrics"]["postShaderPlan"]["features"], 0x000000DE)
            self.assertEqual(diagnostics["metrics"]["postShaderPlan"]["hash"], 0x0BADCAFE)
            self.assertEqual(diagnostics["metrics"]["postShaderPlan"]["textures"], 2)
            self.assertEqual(diagnostics["metrics"]["postShaderPlan"]["uniforms"], 12)
            self.assertEqual(diagnostics["metrics"]["postShaderPlan"]["frames"], 3)
            self.assertEqual(diagnostics["metrics"]["postShaderPlan"]["invalidFrames"], 0)
            self.assertEqual(diagnostics["metrics"]["postShaderCache"]["ready"], 1)
            self.assertEqual(diagnostics["metrics"]["postShaderCache"]["programs"], 3)
            self.assertEqual(diagnostics["metrics"]["postShaderCache"]["programLimit"], 32)
            self.assertEqual(diagnostics["metrics"]["postShaderCache"]["validPlans"], 4)
            self.assertEqual(diagnostics["metrics"]["postShaderCache"]["compileFailures"], 0)
            self.assertEqual(diagnostics["metrics"]["postShaderCache"]["linkFailures"], 0)
            self.assertEqual(diagnostics["metrics"]["postShaderCache"]["sourceFailures"], 0)
            self.assertEqual(diagnostics["metrics"]["postShaderCache"]["sourceHash"], 0x12345678)
            self.assertEqual(diagnostics["metrics"]["postShaderCache"]["program"], 99)
            self.assertEqual(diagnostics["metrics"]["postShaderDirectFinal"]["execute"], 0)
            self.assertEqual(diagnostics["metrics"]["postShaderDirectFinal"]["eligible"], 1)
            self.assertEqual(diagnostics["metrics"]["postShaderDirectFinal"]["bound"], 0)
            self.assertEqual(diagnostics["metrics"]["postShaderDirectFinal"]["reject"], 1)
            self.assertEqual(diagnostics["metrics"]["postShaderDirectFinal"]["candidates"], 2)
            self.assertEqual(diagnostics["metrics"]["postShaderDirectFinal"]["eligibleFrames"], 2)
            self.assertEqual(diagnostics["metrics"]["postShaderDirectFinal"]["attempts"], 0)
            self.assertEqual(diagnostics["metrics"]["postShaderDirectFinal"]["binds"], 0)
            self.assertEqual(diagnostics["metrics"]["postShaderDirectFinal"]["fallbacks"], 2)
            self.assertEqual(diagnostics["metrics"]["postShaderDirectFinal"]["rejects"], 2)
            stream_draw = diagnostics["metrics"]["streamDraw"]
            self.assertEqual(stream_draw["draws"], 1)
            self.assertEqual(stream_draw["dynamicLights"], 1)
            self.assertEqual(stream_draw["screenMaps"], 0)
            self.assertEqual(stream_draw["videoMaps"], 0)
            self.assertEqual(stream_draw["shadows"], 0)
            self.assertEqual(stream_draw["skips"], 2)
            stream_dlight = diagnostics["metrics"]["streamDlight"]
            self.assertEqual(stream_dlight["attempts"], 2)
            self.assertEqual(stream_dlight["draws"], 1)
            self.assertEqual(stream_dlight["fallbacks"], 0)
            self.assertEqual(stream_dlight["attemptMegabytes"], 0.03)
            self.assertEqual(stream_dlight["megabytes"], 0.02)
            self.assertEqual(stream_dlight["indexMegabytes"], 0.01)
            self.assertEqual(stream_dlight["wraps"], 1)
            self.assertEqual(stream_dlight["sameFrameWrapRejects"], 0)
            self.assertEqual(stream_dlight["syncWaits"], 1)
            self.assertEqual(stream_dlight["syncFailures"], 0)
            dlight_program = diagnostics["metrics"]["dlightProgram"]
            self.assertEqual(dlight_program["active"], 1)
            self.assertEqual(dlight_program["programs"], 2)
            self.assertEqual(dlight_program["availabilityHits"], 3)
            self.assertEqual(dlight_program["availabilityQueries"], 4)
            self.assertEqual(dlight_program["binds"], 5)
            self.assertEqual(dlight_program["bindAttempts"], 6)
            self.assertEqual(dlight_program["cacheHits"], 7)
            dlight_state = diagnostics["metrics"]["dlightState"]
            self.assertEqual(dlight_state["legacyPasses"], 2)
            self.assertEqual(dlight_state["textureBinds"], 3)
            self.assertEqual(dlight_state["fogTextureBinds"], 4)
            self.assertEqual(dlight_state["shadowTextureBinds"], 5)
            self.assertEqual(dlight_state["shadowTextureFallbackBinds"], 1)
            self.assertEqual(dlight_state["shadowFboBinds"], 6)
            self.assertEqual(dlight_state["shadowFboRestores"], 7)
            self.assertEqual(dlight_state["stateChanges"], 8)
            dlight_build = diagnostics["metrics"]["dlightBuild"]
            self.assertEqual(dlight_build["legacyLights"], 9)
            self.assertEqual(dlight_build["legacySkippedLights"], 10)
            self.assertEqual(dlight_build["legacyNoHitLights"], 2)
            self.assertEqual(dlight_build["legacyVertexes"], 300)
            self.assertEqual(dlight_build["legacyIndexes"], 600)
            self.assertEqual(dlight_build["legacyLitIndexes"], 450)
            self.assertEqual(dlight_build["pmPasses"], 3)
            self.assertEqual(dlight_build["pmVertexes"], 240)
            self.assertEqual(dlight_build["pmIndexes"], 360)
            dlight_cull = diagnostics["metrics"]["dlightCull"]
            self.assertEqual(dlight_cull["legacyVertexes"], 40)
            self.assertEqual(dlight_cull["legacyIndexes"], 90)
            dlight_scissor = diagnostics["metrics"]["dlightScissor"]
            self.assertEqual(dlight_scissor["active"], 1)
            self.assertEqual(dlight_scissor["candidates"], 4)
            self.assertEqual(dlight_scissor["computed"], 3)
            self.assertEqual(dlight_scissor["applied"], 2)
            self.assertEqual(dlight_scissor["fallbacks"], 1)
            self.assertEqual(dlight_scissor["pixels"], 1200)
            self.assertEqual(dlight_scissor["viewportPixels"], 4800)
            stream_binding = diagnostics["metrics"]["streamBindingCache"]
            self.assertEqual(stream_binding["queries"], 1)
            self.assertEqual(stream_binding["hits"], 3)
            self.assertEqual(stream_binding["external"], 4)
            stream_category = diagnostics["metrics"]["streamCategory"]
            self.assertEqual(stream_category["entityDraws"], 1)
            self.assertEqual(stream_category["entityAttempts"], 1)
            self.assertEqual(stream_category["particleDraws"], 0)
            self.assertEqual(stream_category["dlightDraws"], 1)
            self.assertEqual(stream_category["dlightAttempts"], 1)
            self.assertEqual(stream_category["specialDraws"], 0)
            self.assertEqual(stream_category["entityFallbacks"], 0)
            self.assertEqual(stream_category["dlightFallbacks"], 0)
            stream_role = diagnostics["metrics"]["streamRole"]
            self.assertEqual(stream_role["genericDraws"], 1)
            self.assertEqual(stream_role["genericAttempts"], 1)
            self.assertEqual(stream_role["dlightDraws"], 1)
            self.assertEqual(stream_role["dlightAttempts"], 1)
            self.assertEqual(stream_role["dlightFallbacks"], 0)
            stream_draw_skips = diagnostics["metrics"]["streamDrawSkip"]
            self.assertEqual(stream_draw_skips["key"], 1)
            self.assertEqual(stream_draw_skips["fog"], 1)
            self.assertEqual(stream_draw_skips["program"], 0)
            stream_gates = diagnostics["metrics"]["streamMaterialGate"]
            self.assertEqual(stream_gates["multitextureEnabled"], 1)
            self.assertEqual(stream_gates["multitextureAccepted"], 2)
            self.assertEqual(stream_gates["depthFragmentEnabled"], 1)
            self.assertEqual(stream_gates["depthFragmentAccepted"], 1)
            material_plans = diagnostics["metrics"]["materialCompilerPlans"]
            self.assertEqual(material_plans["compiled"], 12)
            self.assertEqual(material_plans["unsupported"], 0)
            self.assertEqual(material_plans["lastUnsupported"], 0)
            self.assertEqual(material_plans["lastUnsupportedReason"], "none")
            stream_compiler = diagnostics["metrics"]["streamMaterialCompiler"]
            self.assertEqual(stream_compiler["rejected"], 0)
            self.assertEqual(stream_compiler["lastUnsupported"], 0)
            self.assertEqual(stream_compiler["lastUnsupportedReason"], "none")
            ownership = diagnostics["metrics"]["ownership"]
            self.assertEqual(ownership["calls"], 0)
            self.assertEqual(ownership["items"], 0)
            self.assertEqual(diagnostics["metrics"]["productTier"]["tier"], "GL2X")
            executor = diagnostics["metrics"]["gl2xExecutor"]
            self.assertEqual(executor["active"], 1)
            self.assertEqual(executor["streamUploads"], 1)
            self.assertEqual(executor["materialCompiler"], 1)
            self.assertEqual(executor["postprocessLite"], 1)
            self.assertEqual(executor["modernPostChain"], 0)
            pass_schedule = diagnostics["metrics"]["passSchedule"]
            self.assertEqual(pass_schedule["valid"], 1)
            self.assertEqual(
                pass_schedule["count"],
                glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE_COUNT,
            )
            self.assertEqual(pass_schedule["hash"], glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE_HASH)
            self.assertEqual(pass_schedule["order"], glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE)
            render_ir_products = diagnostics["metrics"]["renderIRProducts"]
            self.assertEqual(render_ir_products["worldPackets"], 1)
            self.assertEqual(render_ir_products["projectedWorldPackets"], 1)
            self.assertEqual(render_ir_products["projectedWorldPacketDlights"], 2)
            self.assertEqual(render_ir_products["projectedDynamicDraws"], 1)
            self.assertEqual(render_ir_products["projectedDynamicDlights"], 1)
            render_ir_roles = diagnostics["metrics"]["renderIRDynamicRoles"]
            self.assertEqual(render_ir_roles["genericDraws"], 1)
            self.assertEqual(render_ir_roles["dlightDraws"], 1)
            self.assertEqual(render_ir_roles["dlightIndexes"], 6)
            render_ir_passes = diagnostics["metrics"]["renderIRDynamicPasses"]
            self.assertEqual(render_ir_passes["dlightDraws"], 1)
            self.assertEqual(render_ir_passes["sceneDraws"], 1)
            self.assertEqual(render_ir_passes["sceneIndexes"], 12)
            projected_shader_inputs = diagnostics["metrics"]["dlightProjectedShaderInputs"]
            self.assertEqual(projected_shader_inputs["inputs"], 2)
            self.assertEqual(projected_shader_inputs["records"], 3)
            self.assertEqual(projected_shader_inputs["world"], 1)
            self.assertEqual(projected_shader_inputs["dynamic"], 1)
            self.assertEqual(projected_shader_inputs["programmable"], 2)
            self.assertEqual(projected_shader_inputs["fallback"], 0)
            self.assertEqual(projected_shader_inputs["invalid"], 0)
            projected_shader_uniforms = diagnostics["metrics"]["dlightProjectedShaderUniforms"]
            self.assertEqual(projected_shader_uniforms["attempts"], 1)
            self.assertEqual(projected_shader_uniforms["binds"], 1)
            self.assertEqual(projected_shader_uniforms["failures"], 0)
            self.assertEqual(projected_shader_uniforms["records"], 3)
            self.assertEqual(projected_shader_uniforms["truncated"], 0)
            self.assertEqual(projected_shader_uniforms["executable"], 0)
            self.assertEqual(projected_shader_uniforms["suppressed"], 1)
            self.assertEqual(projected_shader_uniforms["worldExecutable"], 0)
            self.assertEqual(projected_shader_uniforms["worldBinds"], 1)
            self.assertEqual(projected_shader_uniforms["dynamicExecutable"], 0)
            self.assertEqual(projected_shader_uniforms["dynamicBinds"], 0)
            self.assertEqual(projected_shader_uniforms["limitSuppressed"], 0)
            self.assertEqual(projected_shader_uniforms["limit"], 3)
            projected_shader_resource = diagnostics["metrics"]["dlightProjectedShaderResource"]
            self.assertEqual(projected_shader_resource["attempts"], 0)
            self.assertEqual(projected_shader_resource["binds"], 0)
            self.assertEqual(projected_shader_resource["executable"], 0)
            self.assertEqual(projected_shader_resource["suppressed"], 0)
            self.assertEqual(projected_shader_resource["promotions"], 0)
            self.assertEqual(projected_shader_resource["failures"], 0)
            self.assertEqual(projected_shader_resource["records"], 0)
            self.assertEqual(projected_shader_resource["worldExecutable"], 0)
            self.assertEqual(projected_shader_resource["dynamicExecutable"], 0)
            self.assertEqual(projected_shader_resource["binding"], 5)
            projected_shader_stream = diagnostics["metrics"]["dlightProjectedShaderStream"]
            self.assertEqual(projected_shader_stream["attempts"], 0)
            self.assertEqual(projected_shader_stream["uploads"], 0)
            self.assertEqual(projected_shader_stream["records"], 0)
            self.assertEqual(projected_shader_stream["bytes"], 0)
            self.assertEqual(projected_shader_stream["persistent"], 0)
            self.assertEqual(projected_shader_stream["rangeBinds"], 0)
            self.assertEqual(projected_shader_stream["rangeAttempts"], 0)
            self.assertEqual(projected_shader_stream["rangeFailures"], 0)
            self.assertEqual(projected_shader_stream["rangeClears"], 0)
            projected_shader_arena = diagnostics["metrics"]["dlightProjectedShaderArena"]
            self.assertEqual(projected_shader_arena["reserves"], 0)
            self.assertEqual(projected_shader_arena["uploads"], 0)
            self.assertEqual(projected_shader_arena["failures"], 0)
            self.assertEqual(projected_shader_arena["wraps"], 0)
            self.assertEqual(projected_shader_arena["rejects"], 0)
            self.assertEqual(projected_shader_arena["waits"], 0)
            self.assertEqual(projected_shader_arena["timeouts"], 0)
            self.assertEqual(projected_shader_arena["syncFailures"], 0)
            self.assertEqual(projected_shader_arena["bytes"], 0)
            self.assertEqual(projected_shader_arena["lightRecords"], 0)
            self.assertEqual(projected_shader_arena["listRecords"], 0)
            self.assertEqual(projected_shader_arena["worldRecords"], 0)
            self.assertEqual(projected_shader_arena["dynamicRecords"], 0)
            self.assertEqual(projected_shader_arena["rangeBinds"], 0)
            self.assertEqual(projected_shader_arena["rangeAttempts"], 0)
            self.assertEqual(projected_shader_arena["rangeFailures"], 0)
            self.assertEqual(projected_shader_arena["rangeClears"], 0)
            self.assertEqual(projected_shader_arena["authoritativeBinds"], 0)
            self.assertEqual(projected_shader_arena["authoritativeAttempts"], 0)
            self.assertEqual(projected_shader_arena["authoritativeFailures"], 0)
            self.assertEqual(projected_shader_arena["authoritativeFallbacks"], 0)
            self.assertEqual(projected_shader_arena["authoritativeClears"], 0)
            self.assertEqual(projected_shader_arena["bound"], 0)
            self.assertEqual(projected_shader_arena["cursor"], 0)
            self.assertEqual(projected_shader_arena["lastBuffer"], 0)
            self.assertEqual(projected_shader_arena["lastOffset"], 0)
            self.assertEqual(projected_shader_arena["lastBytes"], 0)
            projected_shader_mdi = diagnostics["metrics"]["dlightProjectedShaderMdi"]
            self.assertEqual(projected_shader_mdi["attempts"], 0)
            self.assertEqual(projected_shader_mdi["eligible"], 0)
            self.assertEqual(projected_shader_mdi["uploads"], 0)
            self.assertEqual(projected_shader_mdi["failures"], 0)
            self.assertEqual(projected_shader_mdi["skipped"], 0)
            self.assertEqual(projected_shader_mdi["records"], 0)
            self.assertEqual(projected_shader_mdi["indexes"], 0)
            self.assertEqual(projected_shader_mdi["bytes"], 0)
            self.assertEqual(projected_shader_mdi["lastOffset"], 0)
            self.assertEqual(projected_shader_mdi["lastBytes"], 0)
            projected_shader_mdi_ring = diagnostics["metrics"]["dlightProjectedShaderMdiCommandRing"]
            self.assertEqual(projected_shader_mdi_ring["reserves"], 0)
            self.assertEqual(projected_shader_mdi_ring["commits"], 0)
            self.assertEqual(projected_shader_mdi_ring["wraps"], 0)
            self.assertEqual(projected_shader_mdi_ring["failures"], 0)
            self.assertEqual(projected_shader_mdi_ring["slots"], 256)
            self.assertEqual(projected_shader_mdi_ring["cursor"], 0)
            self.assertEqual(projected_shader_mdi_ring["lastSlot"], 0)
            self.assertEqual(projected_shader_mdi_ring["lastBuffer"], 0)
            self.assertEqual(projected_shader_mdi_ring["lastOffset"], 0)
            self.assertEqual(projected_shader_mdi_ring["lastBytes"], 0)
            projected_shader_mdi_submit = diagnostics["metrics"]["dlightProjectedShaderMdiSubmit"]
            self.assertEqual(projected_shader_mdi_submit["attempts"], 0)
            self.assertEqual(projected_shader_mdi_submit["plans"], 0)
            self.assertEqual(projected_shader_mdi_submit["ready"], 0)
            self.assertEqual(projected_shader_mdi_submit["fallbacks"], 0)
            self.assertEqual(projected_shader_mdi_submit["skipped"], 0)
            self.assertEqual(projected_shader_mdi_submit["records"], 0)
            self.assertEqual(projected_shader_mdi_submit["indexes"], 0)
            self.assertEqual(projected_shader_mdi_submit["buffer"], 0)
            self.assertEqual(projected_shader_mdi_submit["lastOffset"], 0)
            self.assertEqual(projected_shader_mdi_submit["lastBytes"], 0)
            projected_shader_mdi_batch = diagnostics["metrics"]["dlightProjectedShaderMdiBatch"]
            self.assertEqual(projected_shader_mdi_batch["attempts"], 0)
            self.assertEqual(projected_shader_mdi_batch["batches"], 0)
            self.assertEqual(projected_shader_mdi_batch["ready"], 0)
            self.assertEqual(projected_shader_mdi_batch["fallbacks"], 0)
            self.assertEqual(projected_shader_mdi_batch["rejects"], 0)
            self.assertEqual(projected_shader_mdi_batch["glErrors"], 0)
            self.assertEqual(projected_shader_mdi_batch["records"], 0)
            self.assertEqual(projected_shader_mdi_batch["indexes"], 0)
            self.assertEqual(projected_shader_mdi_batch["submittedDraws"], 0)
            self.assertEqual(projected_shader_mdi_batch["submittedIndexes"], 0)
            self.assertEqual(projected_shader_mdi_batch["largest"], 0)
            self.assertEqual(projected_shader_mdi_batch["lastReject"], 0)
            self.assertEqual(projected_shader_mdi_batch["buffer"], 0)
            self.assertEqual(projected_shader_mdi_batch["lastOffset"], 0)
            self.assertEqual(projected_shader_mdi_batch["lastBytes"], 0)
            color_pipeline = diagnostics["metrics"]["colorPipeline"]
            self.assertEqual(color_pipeline["space"], "scene-linear")
            self.assertEqual(color_pipeline["transfer"], "sdr-srgb")
            self.assertEqual(color_pipeline["toneMap"], "aces-fitted")
            self.assertEqual(color_pipeline["grade"], "lgg-lut3d")
            self.assertEqual(color_pipeline["exposure"], 1.0)
            self.assertEqual(color_pipeline["paperWhite"], 203.0)
            self.assertEqual(color_pipeline["maxOutput"], 203.0)
            auto_exposure = diagnostics["metrics"]["autoExposure"]
            self.assertEqual(auto_exposure["mode"], 1)
            self.assertEqual(auto_exposure["algorithm"], "simple-average")
            self.assertEqual(auto_exposure["enabled"], 1)
            self.assertEqual(auto_exposure["fallback"], 1)
            self.assertEqual(auto_exposure["sampleCount"], 1024)
            self.assertEqual(auto_exposure["sampleWidth"], 32)
            self.assertEqual(auto_exposure["sampleHeight"], 32)
            self.assertEqual(auto_exposure["targetLuma"], 0.18)
            self.assertEqual(auto_exposure["measuredLuma"], 0.5)
            self.assertEqual(auto_exposure["sampleFailures"], 0)
            color_grade = diagnostics["metrics"]["colorGrade"]
            self.assertEqual(color_grade["mode"], "lgg-lut3d")
            self.assertEqual(color_grade["liftR"], 0.01)
            self.assertEqual(color_grade["gammaR"], 1.10)
            self.assertEqual(color_grade["gainR"], 1.05)
            self.assertEqual(color_grade["whiteTarget"], 6000.0)
            self.assertEqual(color_grade["lutSize"], 16.0)
            self.assertEqual(color_grade["lutScale"], 4.0)
            color_audit = diagnostics["metrics"]["colorAudit"]
            self.assertEqual(color_audit["srgbDecode"], 1)
            self.assertEqual(color_audit["srgbRequested"], 1)
            self.assertEqual(color_audit["srgbAvailable"], 1)
            self.assertEqual(color_audit["framebufferSrgb"], 0)
            self.assertEqual(color_audit["framebufferRequested"], 1)
            self.assertEqual(color_audit["framebufferAvailable"], 1)
            self.assertEqual(color_audit["capture"], "sdr-srgb")
            self.assertEqual(color_audit["captureRequest"], "sdr-srgb")
            self.assertEqual(color_audit["captureHdrAware"], 0)
            self.assertEqual(color_audit["captureSupported"], 1)
            self.assertEqual(color_audit["targetFloat"], 1)
            self.assertEqual(color_audit["finalEncode"], "shader-srgb")
            self.assertEqual(color_audit["contract"], 1)
            texture_audit = diagnostics["metrics"]["textureAudit"]
            self.assertEqual(texture_audit["srgb"], 4)
            self.assertEqual(texture_audit["srgbDecode"], 4)
            self.assertEqual(texture_audit["missingSrgbDecode"], 0)
            self.assertEqual(texture_audit["unexpectedDecode"], 0)
            target_format = diagnostics["metrics"]["targetFormat"]
            self.assertEqual(target_format["internalFormat"], 0x881A)
            self.assertEqual(target_format["textureFormat"], 0x1908)
            self.assertEqual(target_format["textureType"], 0x140B)
            post_controls = diagnostics["metrics"]["postprocessControls"]
            self.assertEqual(post_controls["renderScale"], 1)
            self.assertEqual(post_controls["greyscale"], 1.0)
            self.assertEqual(post_controls["windowAdjusted"], 1)
            post_frames = diagnostics["metrics"]["postprocessFrames"]
            self.assertEqual(post_frames["frames"], 3)
            self.assertEqual(post_frames["screenshots"], 1)
            self.assertEqual(post_frames["minimizedOutput"], 0)
            post_features = diagnostics["metrics"]["postprocessFrameFeatures"]
            self.assertEqual(post_features["renderScale"], 1)
            self.assertEqual(post_features["greyscale"], 1)
            self.assertEqual(post_features["windowAdjusted"], 1)
            self.assertEqual(post_features["minimized"], 0)
            postprocess = diagnostics["metrics"]["postprocess"]
            self.assertEqual(postprocess["bloomStoragePolicy"], "auto")
            self.assertEqual(postprocess["bloomStorageInternalFormat"], 0x8C3A)
            self.assertEqual(postprocess["bloomStorageTextureFormat"], 0x1907)
            self.assertEqual(postprocess["bloomStorageTextureType"], 0x140B)
            pass_counters = diagnostics["metrics"]["gpuPassCounters"]
            self.assertEqual(pass_counters["blits"], 4)
            self.assertEqual(pass_counters["binds"], 9)
            self.assertEqual(pass_counters["clears"], 1)
            self.assertEqual(pass_counters["fullscreen"], 6)
            pass_timing = diagnostics["metrics"]["gpuPassTiming"]
            self.assertEqual(pass_timing["active"], 1)
            self.assertEqual(pass_timing["queries"], 7)
            pass_timings = diagnostics["metrics"]["gpuPassTimings"]
            self.assertEqual(pass_timings["backend"]["lastMs"], 0.27)
            self.assertEqual(pass_timings["postprocess"]["lastMs"], 0.42)
            self.assertEqual(pass_timings["bloom-downscale"]["samples"], 3)
            self.assertEqual(pass_timings["bloom-blur"]["samples"], 2)
            self.assertEqual(pass_timings["flare"]["lastMs"], 0.02)
            color_frame = diagnostics["metrics"]["colorFrame"]
            self.assertEqual(color_frame["samples"], 1)
            self.assertEqual(color_frame["latest"]["backend"], "sdr-srgb")
            self.assertEqual(color_frame["latest"]["internalFormat"], "0x881a")
            self.assertTrue(color_frame["latest"]["contractValid"])
            output_backend = diagnostics["metrics"]["outputBackend"]
            self.assertEqual(output_backend["request"], "auto")
            self.assertEqual(output_backend["selected"], "sdr-srgb")
            self.assertEqual(output_backend["native"], "windows-scrgb")
            self.assertEqual(output_backend["displayHdr"], 1)
            self.assertEqual(output_backend["headroom"], 4.0)
            self.assertEqual(output_backend["displayMax"], 812.0)
            self.assertEqual(output_backend["icc"], 1)
            display_state = diagnostics["metrics"]["displayState"]
            self.assertEqual(display_state["queries"], 4)
            self.assertEqual(display_state["changes"], 1)
            self.assertEqual(display_state["capability"], 1)
            self.assertEqual(display_state["backend"], 1)
            self.assertEqual(display_state["hdr"], 1)
            self.assertEqual(display_state["headroom"], 1)
            self.assertEqual(display_state["luminance"], 1)
            self.assertEqual(display_state["icc"], 1)
            self.assertEqual(display_state["lastFrame"], 2)
            self.assertEqual(display_state["flags"], 0x000000FE)
            self.assertEqual(display_state["hash"], 0x12345678)
            self.assertEqual(display_state["previous"], 0x01020304)

    def test_projected_dlight_shader_profile_accepts_executable_uniforms(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "dlight-shader.log"
            self.write_projected_dlight_shader_profile_log(log)

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-dlight-shader")

            self.assertTrue(diagnostics["found"])
            self.assertEqual(diagnostics["failures"], [])
            uniforms = diagnostics["metrics"]["dlightProjectedShaderUniforms"]
            self.assertEqual(uniforms["binds"], 1)
            self.assertEqual(uniforms["executable"], 1)
            self.assertEqual(uniforms["suppressed"], 0)
            self.assertEqual(uniforms["dynamicExecutable"], 1)
            self.assertEqual(uniforms["dynamicBinds"], 1)

    def test_projected_dlight_shader_profile_accepts_promoted_resource_records(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "dlight-shader-resource.log"
            self.write_projected_dlight_shader_profile_log(
                log,
                dynamic_inputs=4,
                dynamic_executable=1,
                dynamic_binds=1,
                # the world packet still binds through the uniform window
                uniform_records=1,
                resource_attempts=1,
                resource_binds=1,
                resource_executable=1,
                resource_promotions=1,
                resource_records=4,
                resource_dynamic_executable=1,
                stream_range_binds=1,
                stream_range_clears=1,
                arena_range_binds=1,
                arena_range_clears=1,
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-dlight-shader")

            self.assertEqual(diagnostics["failures"], [])
            resource = diagnostics["metrics"]["dlightProjectedShaderResource"]
            self.assertEqual(resource["attempts"], 1)
            self.assertEqual(resource["promotions"], 1)
            self.assertEqual(resource["records"], 4)
            self.assertEqual(resource["dynamicExecutable"], 1)

    def test_projected_dlight_shader_visual_parity_accepts_compared_proof(self) -> None:
        manifest = projected_dlight_shader_parity_manifest()

        failures = glx_runtime_sweep.evaluate_gate(manifest)
        evidence = glx_runtime_sweep.projected_dlight_shader_parity_evidence(manifest)
        proof = glx_runtime_sweep.proof_status(manifest)

        self.assertEqual(failures, [])
        self.assertEqual(evidence["status"], "passed")
        self.assertTrue(evidence["required"])
        self.assertEqual(evidence["screenshots"], 2)
        self.assertEqual(evidence["timedemoScreenshots"], 1)
        self.assertEqual(evidence["legacyFallbackScreenshots"], 2)
        self.assertEqual(evidence["timedemos"], 1)
        self.assertGreater(evidence["worldExecutable"], 0)
        self.assertGreater(evidence["dynamicExecutable"], 0)
        self.assertGreater(evidence["resourcePromotions"], 0)
        self.assertEqual(proof["visual"]["status"], "passed")
        self.assertEqual(proof["visual"]["screenshots"], 2)
        self.assertEqual(proof["visual"]["references"], 2)
        self.assertEqual(proof["visual"]["totalScreenshots"], 4)

    def test_projected_dlight_shader_visual_parity_rejects_incomplete_proof(self) -> None:
        manifest = projected_dlight_shader_parity_manifest(
            screenshot_comparison={
                "status": "failed",
                "reason": "diff-threshold",
                "rms": 4.0,
                "changedPixelRatio": 0.1,
            },
            metrics=projected_dlight_shader_metrics(
                world_inputs=0,
                dynamic_inputs=1,
                world_executable=0,
                dynamic_executable=0,
                resource_promotions=0,
                resource_executable=0,
                resource_records=0,
            ),
            baseline_status="failed",
        )
        manifest["runs"] = [
            run for run in manifest["runs"]
            if run.get("type") != "timedemo"
        ]
        manifest["projectedDlightShaderParityEvidence"] = (
            glx_runtime_sweep.projected_dlight_shader_parity_evidence(manifest)
        )

        failures = glx_runtime_sweep.evaluate_gate(manifest)
        text = "\n".join(failures)

        self.assertIn("legacy fallback baseline", text)
        self.assertIn("screenshot comparison did not pass", text)
        self.assertIn("missing GLx timedemo log evidence", text)
        self.assertIn("missing compared GLx timedemo screenshot", text)
        self.assertIn("static-world projected-light inputs", text)
        self.assertIn("dynamic-draw projected-light binds", text)
        self.assertIn("over-limit shader-resource promotion", text)

    def test_projected_dlight_shader_visual_parity_rejects_missing_same_run_reference(self) -> None:
        manifest = projected_dlight_shader_parity_manifest()
        screenshots = manifest["runs"][0]["screenshots"]
        manifest["runs"][0]["screenshots"] = [
            shot for shot in screenshots
            if not shot.get("skipExternalBaseline")
        ]
        manifest["runs"][0]["screenshots"][0].pop("legacyFallbackComparison", None)
        manifest["projectedDlightShaderParityEvidence"] = (
            glx_runtime_sweep.projected_dlight_shader_parity_evidence(manifest)
        )

        failures = glx_runtime_sweep.evaluate_gate(manifest)
        text = "\n".join(failures)

        self.assertIn("same-run legacy fallback capture", text)
        self.assertIn("legacy fallback reference", text)

    def test_projected_dlight_shader_visual_parity_rejects_stale_passed_rollup(self) -> None:
        manifest = projected_dlight_shader_parity_manifest()
        # Keep the precomputed passed evidence but remove the dynamic-demo screenshot pair.
        manifest["runs"][1]["screenshots"] = []

        failures = glx_runtime_sweep.evaluate_gate(manifest)
        text = "\n".join(failures)

        self.assertIn("rollup is missing dynamic-demo candidate", text)
        self.assertIn("insufficient candidate screenshots", text)
        self.assertIn("missing timedemo screenshot coverage", text)

    def test_projected_dlight_shader_visual_parity_dossier_summarizes_evidence(self) -> None:
        manifest = projected_dlight_shader_parity_manifest()

        dossier = glx_runtime_sweep.glx_visual_dossier(manifest, Path("manifest.json"))

        self.assertIn("## Projected Dlight Shader Parity", dossier)
        self.assertIn("| passed | q3dm1 | demo1 | 2 | 2 | 2 | 2 | 1 | 1 |", dossier)
        self.assertIn("2.000", dossier)
        self.assertIn("0.500%", dossier)

    def test_projected_dlight_shader_markdown_summary_summarizes_evidence(self) -> None:
        manifest = projected_dlight_shader_parity_manifest()

        summary = glx_runtime_sweep.markdown_summary(manifest, Path("manifest.json"))

        self.assertIn("## Projected Dlight Shader Parity", summary)
        self.assertIn("candidateScreenshots=2", summary)
        self.assertIn("legacyRefs=2", summary)
        self.assertIn("timedemoScreenshots=1", summary)
        self.assertIn("reviewed=2", summary)
        self.assertIn("sameRun=2", summary)
        self.assertIn("resourcePromotions=2", summary)

    def test_projected_dlight_shader_release_record_summarizes_evidence(self) -> None:
        manifest = projected_dlight_shader_parity_manifest()

        record = glx_runtime_sweep.release_proof_manifest_record(
            manifest,
            Path("proof/windows-x64/glx-dlight-shader/manifest.json"),
            Path("proof"),
        )
        parity = record["projectedDlightShaderParity"]

        self.assertEqual(parity["status"], "passed")
        self.assertEqual(parity["mapCandidates"], ["q3dm1"])
        self.assertEqual(parity["demoCandidates"], ["demo1"])
        self.assertEqual(parity["candidateScreenshots"], 2)
        self.assertEqual(parity["legacyFallbackScreenshots"], 2)
        self.assertEqual(parity["reviewedComparisonsPassed"], 2)
        self.assertEqual(parity["sameRunComparisonsPassed"], 2)

    def test_projected_dlight_shader_profile_rejects_suppressed_uniforms(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "dlight-shader-suppressed.log"
            self.write_projected_dlight_shader_profile_log(
                log,
                executable=0,
                suppressed=1,
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-dlight-shader")
            failures = "\n".join(diagnostics["failures"])

            self.assertIn("did not execute projected uniform binds", failures)
            self.assertIn("did not execute projected uniform binds for dynamic draws", failures)
            self.assertIn("still suppressed projected uniform binds", failures)

    def test_projected_dlight_shader_profile_rejects_truncated_uniforms(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "dlight-shader-truncated.log"
            self.write_projected_dlight_shader_profile_log(
                log,
                binds=1,
                executable=0,
                suppressed=1,
                dynamic_executable=0,
                dynamic_binds=1,
                truncated=1,
                limit_suppressed=1,
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-dlight-shader")
            failures = "\n".join(diagnostics["failures"])

            self.assertIn("truncated projected uniform input", failures)
            self.assertIn("suppressed over-limit projected uniform binds", failures)

    def test_projected_dlight_shader_profile_rejects_failed_resource_promotion(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "dlight-shader-resource-failed.log"
            self.write_projected_dlight_shader_profile_log(
                log,
                dynamic_inputs=4,
                dynamic_executable=1,
                dynamic_binds=1,
                uniform_records=0,
                resource_attempts=1,
                resource_failures=1,
                resource_suppressed=1,
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-dlight-shader")
            failures = "\n".join(diagnostics["failures"])

            self.assertIn("bound fewer shader records", failures)
            self.assertIn("reported projected-light resource failures", failures)
            self.assertIn("suppressed projected-light resource binds", failures)
            self.assertIn("did not promote over-limit resource binds", failures)
            self.assertIn("did not execute promoted resource binds", failures)
            self.assertIn("did not bind promoted resource records", failures)

    def test_projected_dlight_shader_profile_rejects_missing_world_execution(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "dlight-shader-world-missing.log"
            self.write_projected_dlight_shader_profile_log(
                log,
                binds=2,
                executable=1,
                world_inputs=1,
                dynamic_inputs=1,
                world_executable=0,
                world_binds=1,
                dynamic_executable=1,
                dynamic_binds=1,
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-dlight-shader")
            failures = "\n".join(diagnostics["failures"])

            self.assertIn("did not execute projected uniform binds for world packets", failures)

    def test_projected_dlight_shader_profile_rejects_mismatched_projected_input_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "dlight-shader-input-mismatch.log"
            self.write_projected_dlight_shader_profile_log(
                log,
                world_inputs=1,
                dynamic_inputs=1,
                world_executable=1,
                world_binds=1,
                dynamic_executable=1,
                dynamic_binds=1,
                render_ir_projected_world=0,
                render_ir_projected_dynamic=0,
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-dlight-shader")
            failures = "\n".join(diagnostics["failures"])

            self.assertIn("mismatched projected-light world input evidence", failures)
            self.assertIn("mismatched projected-light dynamic input evidence", failures)

    def test_projected_dlight_shader_profile_rejects_stale_resource_ranges(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "dlight-shader-stale-ranges.log"
            self.write_projected_dlight_shader_profile_log(
                log,
                stream_failures=1,
                stream_range_binds=2,
                stream_range_failures=1,
                stream_range_clears=1,
                arena_range_binds=1,
                arena_range_failures=1,
                arena_range_clears=0,
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-dlight-shader")
            failures = "\n".join(diagnostics["failures"])

            self.assertIn("reported projected-light stream upload failures", failures)
            self.assertIn("reported projected-light stream range failures", failures)
            self.assertIn("left stale projected-light stream resource ranges", failures)
            self.assertIn("reported projected-light arena range failures", failures)
            self.assertIn("left stale projected-light arena ranges", failures)

    def test_projected_dlight_mdi_profile_accepts_submitted_batches(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "dlight-mdi.log"
            self.write_projected_dlight_mdi_profile_log(log)

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-dlight-mdi")

            self.assertTrue(diagnostics["found"])
            self.assertEqual(diagnostics["failures"], [])
            arena = diagnostics["metrics"]["dlightProjectedShaderArena"]
            self.assertEqual(arena["reserves"], 1)
            self.assertEqual(arena["uploads"], 1)
            self.assertEqual(arena["failures"], 0)
            self.assertEqual(arena["lightRecords"], 1)
            self.assertEqual(arena["listRecords"], 1)
            self.assertEqual(arena["rangeBinds"], 1)
            self.assertEqual(arena["authoritativeBinds"], 1)
            self.assertEqual(arena["authoritativeFailures"], 0)
            self.assertEqual(arena["authoritativeFallbacks"], 0)
            mdi_commands = diagnostics["metrics"]["dlightProjectedShaderMdi"]
            self.assertEqual(mdi_commands["eligible"], 1)
            self.assertEqual(mdi_commands["uploads"], 1)
            mdi_ring = diagnostics["metrics"]["dlightProjectedShaderMdiCommandRing"]
            self.assertEqual(mdi_ring["reserves"], 1)
            self.assertEqual(mdi_ring["commits"], 1)
            self.assertEqual(mdi_ring["failures"], 0)
            self.assertEqual(mdi_ring["slots"], 256)
            mdi_submit = diagnostics["metrics"]["dlightProjectedShaderMdiSubmit"]
            self.assertEqual(mdi_submit["plans"], 1)
            self.assertEqual(mdi_submit["ready"], 1)
            mdi_batch = diagnostics["metrics"]["dlightProjectedShaderMdiBatch"]
            self.assertEqual(mdi_batch["ready"], 1)
            self.assertEqual(mdi_batch["submittedDraws"], 1)
            self.assertEqual(mdi_batch["submittedIndexes"], 6)

    def test_projected_dlight_mdi_profile_rejects_failed_submissions(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "dlight-mdi-fallback.log"
            self.write_projected_dlight_mdi_profile_log(
                log,
                command_failures=1,
                arena_uploads=0,
                arena_failures=1,
                arena_rejects=1,
                arena_timeouts=1,
                arena_sync_failures=1,
                arena_light_records=0,
                arena_list_records=0,
                arena_range_binds=0,
                arena_range_failures=1,
                arena_authoritative_binds=0,
                arena_authoritative_failures=1,
                arena_authoritative_fallbacks=1,
                ring_commits=0,
                ring_failures=1,
                submit_ready=0,
                submit_fallbacks=1,
                batch_ready=0,
                batch_fallbacks=1,
                batch_rejects=1,
                batch_gl_errors=1,
                submitted_draws=0,
                submitted_indexes=0,
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-dlight-mdi")
            failures = "\n".join(diagnostics["failures"])

            self.assertIn("did not upload projected-light arena records", failures)
            self.assertIn("did not record projected-light arena list evidence", failures)
            self.assertIn("did not bind projected-light arena ranges", failures)
            self.assertIn("reported projected-light arena failures", failures)
            self.assertIn("reported projected-light arena wrap rejects", failures)
            self.assertIn("reported projected-light arena sync timeouts", failures)
            self.assertIn("reported projected-light arena sync failures", failures)
            self.assertIn("reported projected-light arena range failures", failures)
            self.assertIn("did not bind authoritative projected-light arena ranges", failures)
            self.assertIn("reported authoritative projected-light arena failures", failures)
            self.assertIn("reported authoritative projected-light arena fallbacks", failures)
            self.assertIn("indirect command upload failures", failures)
            self.assertIn("did not commit indirect command ring slots", failures)
            self.assertIn("reported indirect command ring failures", failures)
            self.assertIn("did not produce submit-ready plans", failures)
            self.assertIn("reported submit fallbacks", failures)
            self.assertIn("did not produce ready MDI batches", failures)
            self.assertIn("did not submit projected-dlight MDI batches", failures)
            self.assertIn("reported batch fallbacks", failures)
            self.assertIn("reported batch rejects", failures)
            self.assertIn("reported GL errors", failures)

    def test_glx_diagnostics_reject_stream_dlights_without_ir_ownership(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "dlight-ir-mismatch.log"
            log.write_text(
                "\n".join(
                    [
                        "  product tier: GL2X",
                        "  GL2X programmable executor: active yes, client-memory fallback yes, stream uploads yes, material compiler yes, postprocess-lite yes, modern post chain no, scene-linear output no",
                        "  GL2X programmable support: common materials yes, dynamic entities yes, lightmaps yes, multitexture yes, fog yes, sprites yes, beams yes, screenshots yes, demos yes",
                        "  glx: ownership legacy delegation 0 calls/0 items, generic 0, vbo-device 0, vbo-soft 0, arrays 0",
                        f"  GLx pass schedule: valid {glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE_COUNT}/{glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE_HASH} {glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE}",
                        "  render IR dynamic roles: generic 2/12/0, dlight 0/0/0, shadow 0/0/0, beam 0/0/0, post 0/0/0",
                        "  render IR dynamic passes: dlight 0/0/0, scene 2/12/0, post 0/0/0, other 0/0/0",
                        "  dynamic stream dynamic-light telemetry: attempts 2, draws 2, fallbacks 0, attempt 0.03 MB, draw 0.02 MB, index 0.01 MB, tex1 0.00 MB, wraps 0, same-frame rejects 0, waits 0, timeouts 0, sync failures 0",
                        "  dynamic stream categories: entity 0/0, particle 0/0, poly 0/0, mark 0/0, weapon 0/0, ui 0/0, beam 0/0, dlight 2/2, special 0/0",
                    ]
                ),
                encoding="utf-8",
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-parity")

            self.assertTrue(diagnostics["found"])
            self.assertIn(
                "GLx streamed dlight draws are not classified as render IR dlight role products: "
                "stream 2, role 0.",
                diagnostics["failures"],
            )
            self.assertIn(
                "GLx streamed dlight draws are not scheduled on the render IR dynamic-lights pass: "
                "stream 2, pass 0.",
                diagnostics["failures"],
            )

    def test_glx_color_frame_csv_is_gate_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "color.csv.log"
            log.write_text(
                "\n".join(
                    [
                        "glx: color-frame-csv frame,backend,space,transfer,exposure,paperWhiteNits,maxOutputNits,srgbDecode,framebufferSrgb,internalFormat,textureFormat,textureType,sceneTargetFloat,shaderSrgbEncode,contractValid",
                        "glx: color-frame-csv 7,sdr-srgb,scene-linear,sdr-srgb,1.2500,203.0,203.0,yes,no,0x881a,0x1908,0x140b,yes,yes,yes",
                    ]
                ),
                encoding="utf-8",
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-color")
            performance = glx_runtime_sweep.analyze_glx_performance(log)

            self.assertTrue(diagnostics["found"])
            self.assertEqual(diagnostics["failures"], [])
            color_frame = diagnostics["metrics"]["colorFrame"]
            self.assertEqual(color_frame["samples"], 1)
            self.assertEqual(color_frame["latest"]["frame"], 7)
            self.assertEqual(color_frame["latest"]["space"], "scene-linear")
            self.assertEqual(color_frame["latest"]["internalFormat"], "0x881a")
            self.assertTrue(color_frame["latest"]["contractValid"])
            self.assertEqual(performance["latest"]["colorFrameSamples"], 1)
            self.assertEqual(performance["latest"]["colorFrameSpace"], "scene-linear")
            self.assertEqual(performance["latest"]["colorFrameInternalFormat"], "0x881a")

    def test_glx_diagnostics_reject_sdr_output_with_hdr_headroom(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "sdr-headroom.log"
            log.write_text(
                "glx: color pipeline scene-linear precision 16 transfer sdr-srgb "
                "tone-map aces-fitted exposure 1.00 bloom-threshold 0.75/2 knee 0.50 "
                "grade none paper-white 203 max 1000\n",
                encoding="utf-8",
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-color")

            self.assertTrue(diagnostics["found"])
            self.assertTrue(
                any("HDR max-output headroom" in failure for failure in diagnostics["failures"])
            )

    def test_glx_display_state_diagnostics_allow_sdr_fallback_after_hdr_loss(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "display-state.log"
            log.write_text(
                "\n".join(
                    [
                        "glx: output backend request auto selected sdr-srgb native sdr-srgb hardware no experimental no display-hdr no headroom 1.00 sdr-white 203 display-max 203 icc no/0",
                        "glx: display state queries 6 changes 2 capability 2 backend 1 hdr 1 headroom 1 luminance 1 icc 0 last-frame 5 flags 0x0000003e hash 0x22222222 previous 0x11111111",
                    ]
                ),
                encoding="utf-8",
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-color")

            self.assertTrue(diagnostics["found"])
            self.assertEqual(diagnostics["failures"], [])
            display_state = diagnostics["metrics"]["displayState"]
            self.assertEqual(display_state["changes"], 2)
            self.assertEqual(display_state["capability"], 2)
            self.assertEqual(display_state["backend"], 1)
            self.assertEqual(display_state["flags"], 0x0000003E)
            output_backend = diagnostics["metrics"]["outputBackend"]
            self.assertEqual(output_backend["selected"], "sdr-srgb")
            self.assertEqual(output_backend["hardware"], 0)

    def test_glx_ownership_profile_reports_legacy_delegation(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "switch.log"
            log.write_text(
                "glx: ownership legacy delegation 3 calls/96 items, generic 1, vbo-device 1, vbo-soft 0, arrays 1\n",
                encoding="utf-8",
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-ownership")
            failures = "\n".join(diagnostics["failures"])
            ownership = diagnostics["metrics"]["ownership"]

            self.assertIn("legacy draw delegation", failures)
            self.assertEqual(ownership["calls"], 3)
            self.assertEqual(ownership["items"], 96)
            self.assertEqual(ownership["generic"], 1)
            self.assertEqual(ownership["vboDevice"], 1)
            self.assertEqual(ownership["arrays"], 1)

    def test_glx_ownership_profile_accepts_glxinfo_diagnostic(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "switch.log"
            log.write_text(
                "  ownership legacy delegation: 2 calls, 12 items\n",
                encoding="utf-8",
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-ownership")
            failures = "\n".join(diagnostics["failures"])
            ownership = diagnostics["metrics"]["ownership"]

            self.assertIn("legacy draw delegation", failures)
            self.assertNotIn("No GLx ownership diagnostic", failures)
            self.assertEqual(ownership["calls"], 2)
            self.assertEqual(ownership["items"], 12)

    def test_ownership_proof_evidence_passes_for_modern_zero_delegation(self) -> None:
        manifest = ownership_proof_manifest("windows-x64")
        evidence = manifest["ownershipProofEvidence"]

        self.assertEqual(evidence["version"], glx_runtime_sweep.GLX_OWNERSHIP_PROOF_VERSION)
        self.assertEqual(evidence["status"], "passed")
        self.assertTrue(evidence["zeroDelegation"])
        self.assertEqual(evidence["delegation"]["calls"], 0)
        self.assertEqual(evidence["delegation"]["items"], 0)
        self.assertEqual(evidence["productTiers"], ["GL3X"])
        self.assertTrue(evidence["modernTierDiagnosticsOk"])
        self.assertTrue(evidence["modernPostOutput"])
        self.assertEqual(evidence["postOutputOwnership"]["modes"], ["glx-owned"])
        self.assertTrue(evidence["postOutputOwnership"]["executableCountsFound"])
        self.assertEqual(evidence["postOutputOwnership"]["executableNodes"], 2)
        self.assertEqual(evidence["postOutputOwnership"]["executableOutputs"], 1)
        self.assertEqual(evidence["postOutputOwnership"]["fallbackMask"], 0)
        self.assertTrue(evidence["postShaderDirectFinal"]["found"])
        self.assertEqual(evidence["postShaderDirectFinal"]["bound"], 1)
        self.assertEqual(evidence["postShaderDirectFinal"]["binds"], 2)
        self.assertEqual(evidence["postShaderDirectFinal"]["reject"], 0)
        self.assertEqual(evidence["failures"], [])

    def test_promotion_ownership_metrics_reject_boolean_numeric_counters(self) -> None:
        manifest = {
            "ownershipProofEvidence": {
                "version": glx_runtime_sweep.GLX_OWNERSHIP_PROOF_VERSION,
                "status": "passed",
                "failures": [],
                "delegation": {"found": True, "calls": False, "items": False},
                "productTiers": ["GL3X"],
                "postOutputOwnership": {
                    "found": True,
                    "modes": ["glx-owned"],
                    "postNodes": True,
                    "outputs": True,
                    "executableCountsFound": True,
                    "executableNodes": True,
                    "executableOutputs": True,
                    "legacyFallback": False,
                },
                "postShaderDirectFinal": {
                    "found": True,
                    "bound": True,
                    "binds": True,
                    "reject": False,
                },
                "modernTierDiagnosticsFound": True,
                "modernTierDiagnosticsOk": True,
                "modernPostOutputTier": True,
                "modernPostOutput": True,
            }
        }

        metrics = glx_promotion.manifest_ownership_metrics(manifest)

        self.assertEqual(metrics["postOutputPostNodes"], 0)
        self.assertEqual(metrics["postOutputExecutableNodes"], 0)
        self.assertEqual(metrics["postShaderDirectFinalBound"], 0)
        self.assertFalse(metrics["modernPostOutput"])

    def test_ownership_proof_evidence_fails_on_delegation_and_missing_fingerprint(self) -> None:
        manifest = ownership_proof_manifest(
            "windows-x64",
            calls=2,
            items=64,
            post_hash=0,
        )
        evidence = manifest["ownershipProofEvidence"]
        failures = "\n".join(evidence["failures"])

        self.assertEqual(evidence["status"], "failed")
        self.assertFalse(evidence["zeroDelegation"])
        self.assertFalse(evidence["modernPostOutput"])
        self.assertIn("legacy delegation", failures)
        self.assertIn("post-node fingerprint", failures)

    def test_gl12_diagnostics_report_fixed_function_executor_contract(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "switch.log"
            log.write_text(
                "\n".join(
                    [
                        "  product tier: GL12",
                        "  GL12 fixed-function executor: active yes, client-memory draws yes, stream uploads no, material compiler no, modern post chain no",
                        "  GL12 fixed-function support: lightmaps yes, multitexture yes, fog yes, sprites yes, beams yes, dynamic lights yes, stencil shadows if available yes, screenshots yes, demos yes",
                    ]
                ),
                encoding="utf-8",
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-off")
            executor = diagnostics["metrics"]["gl12Executor"]
            support = diagnostics["metrics"]["gl12Support"]

            self.assertEqual(diagnostics["failures"], [])
            self.assertEqual(executor["active"], 1)
            self.assertEqual(executor["clientMemoryDraws"], 1)
            self.assertEqual(executor["streamUploads"], 0)
            self.assertEqual(executor["materialCompiler"], 0)
            self.assertEqual(executor["modernPostChain"], 0)
            self.assertEqual(support["lightmaps"], 1)
            self.assertEqual(support["multitexture"], 1)
            self.assertEqual(support["screenshots"], 1)
            self.assertEqual(support["demos"], 1)

    def test_gl12_diagnostics_reject_missing_fixed_function_support(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "switch.log"
            log.write_text(
                "\n".join(
                    [
                        "  product tier: GL12",
                        "  GL12 fixed-function executor: active yes, client-memory draws no, stream uploads yes, material compiler yes, modern post chain yes",
                        "  GL12 fixed-function support: lightmaps yes, multitexture no, fog yes, sprites yes, beams yes, dynamic lights yes, stencil shadows if available yes, screenshots yes, demos yes",
                    ]
                ),
                encoding="utf-8",
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-off")
            failures = "\n".join(diagnostics["failures"])

            self.assertIn("client-memory", failures)
            self.assertIn("streamUploads", failures)
            self.assertIn("materialCompiler", failures)
            self.assertIn("modernPostChain", failures)
            self.assertIn("multitexture", failures)

    def test_gl2x_diagnostics_report_programmable_executor_contract(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "switch.log"
            log.write_text(
                "\n".join(
                    [
                        "  product tier: GL2X",
                        "  GL2X programmable executor: active yes, client-memory fallback yes, stream uploads yes, material compiler yes, postprocess-lite yes, modern post chain no, scene-linear output no",
                        "  GL2X programmable support: common materials yes, dynamic entities yes, lightmaps yes, multitexture yes, fog yes, sprites yes, beams yes, screenshots yes, demos yes",
                    ]
                ),
                encoding="utf-8",
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-off")
            executor = diagnostics["metrics"]["gl2xExecutor"]
            support = diagnostics["metrics"]["gl2xSupport"]

            self.assertEqual(diagnostics["failures"], [])
            self.assertEqual(executor["active"], 1)
            self.assertEqual(executor["clientMemoryFallback"], 1)
            self.assertEqual(executor["streamUploads"], 1)
            self.assertEqual(executor["materialCompiler"], 1)
            self.assertEqual(executor["postprocessLite"], 1)
            self.assertEqual(executor["modernPostChain"], 0)
            self.assertEqual(executor["sceneLinearOutput"], 0)
            self.assertEqual(support["commonMaterials"], 1)
            self.assertEqual(support["dynamicEntities"], 1)
            self.assertEqual(support["lightmaps"], 1)
            self.assertEqual(support["demos"], 1)

    def test_gl2x_diagnostics_reject_modern_requirements(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "switch.log"
            log.write_text(
                "\n".join(
                    [
                        "  product tier: GL2X",
                        "  GL2X programmable executor: active yes, client-memory fallback no, stream uploads no, material compiler no, postprocess-lite no, modern post chain yes, scene-linear output yes",
                        "  GL2X programmable support: common materials no, dynamic entities no, lightmaps yes, multitexture yes, fog yes, sprites yes, beams yes, screenshots yes, demos yes",
                    ]
                ),
                encoding="utf-8",
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-off")
            failures = "\n".join(diagnostics["failures"])

            self.assertIn("clientMemoryFallback", failures)
            self.assertIn("streamUploads", failures)
            self.assertIn("materialCompiler", failures)
            self.assertIn("postprocessLite", failures)
            self.assertIn("modernPostChain", failures)
            self.assertIn("sceneLinearOutput", failures)
            self.assertIn("commonMaterials", failures)
            self.assertIn("dynamicEntities", failures)

    def test_gl3x_diagnostics_report_performance_executor_contract(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "switch.log"
            log.write_text(
                "\n".join(
                    [
                        "  product tier: GL3X",
                        "  GL3X performance executor: active yes, FBO postprocess yes, UBO frame/object constants yes, timer queries yes, sync-aware uploads yes, static buffer ownership yes, dynamic buffer ownership yes, persistent uploads no, indirect submission no, direct state access no",
                        "  GL3X performance support: material compiler yes, common materials yes, dynamic entities yes, modern post chain yes, scene-linear output yes, screenshots yes, demos yes",
                    ]
                ),
                encoding="utf-8",
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-off")
            executor = diagnostics["metrics"]["gl3xExecutor"]
            support = diagnostics["metrics"]["gl3xSupport"]

            self.assertEqual(diagnostics["failures"], [])
            self.assertEqual(executor["active"], 1)
            self.assertEqual(executor["fboPostProcess"], 1)
            self.assertEqual(executor["uboFrameObjectConstants"], 1)
            self.assertEqual(executor["timerQueries"], 1)
            self.assertEqual(executor["syncAwareUploads"], 1)
            self.assertEqual(executor["staticBufferOwnership"], 1)
            self.assertEqual(executor["dynamicBufferOwnership"], 1)
            self.assertEqual(executor["persistentUploads"], 0)
            self.assertEqual(executor["indirectSubmission"], 0)
            self.assertEqual(executor["directStateAccess"], 0)
            self.assertEqual(support["materialCompiler"], 1)
            self.assertEqual(support["modernPostChain"], 1)
            self.assertEqual(support["sceneLinearOutput"], 1)

    def test_gl3x_diagnostics_reject_gl4_only_requirements_and_missing_modern_path(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "switch.log"
            log.write_text(
                "\n".join(
                    [
                        "  product tier: GL3X",
                        "  GL3X performance executor: active yes, FBO postprocess no, UBO frame/object constants no, timer queries no, sync-aware uploads no, static buffer ownership no, dynamic buffer ownership no, persistent uploads yes, indirect submission yes, direct state access yes",
                        "  GL3X performance support: material compiler no, common materials no, dynamic entities no, modern post chain no, scene-linear output no, screenshots yes, demos yes",
                    ]
                ),
                encoding="utf-8",
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-off")
            failures = "\n".join(diagnostics["failures"])

            self.assertIn("fboPostProcess", failures)
            self.assertIn("uboFrameObjectConstants", failures)
            self.assertIn("timerQueries", failures)
            self.assertIn("syncAwareUploads", failures)
            self.assertIn("staticBufferOwnership", failures)
            self.assertIn("dynamicBufferOwnership", failures)
            self.assertIn("persistentUploads", failures)
            self.assertIn("indirectSubmission", failures)
            self.assertIn("directStateAccess", failures)
            self.assertIn("materialCompiler", failures)
            self.assertIn("modernPostChain", failures)
            self.assertIn("sceneLinearOutput", failures)

    def test_gl41_diagnostics_report_mac_modern_executor_contract(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "switch.log"
            log.write_text(
                "\n".join(
                    [
                        "  product tier: GL41",
                        "  GL41 mac-modern executor: active yes, FBO postprocess yes, UBO frame/object constants yes, timer queries yes, sync-aware uploads yes, static buffer ownership yes, dynamic buffer ownership yes, macOS 4.1 ceiling yes",
                        "  GL41 mac-modern support: material compiler yes, common materials yes, dynamic entities yes, modern post chain yes, scene-linear output yes, high-quality SDR yes, optional hardware HDR yes, screenshots yes, demos yes",
                        "  GL41 mac-modern GL4+ requirements: debug output no, buffer storage no, direct state access no, multi-draw indirect no, persistent uploads no",
                    ]
                ),
                encoding="utf-8",
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-off")
            executor = diagnostics["metrics"]["gl41Executor"]
            support = diagnostics["metrics"]["gl41Support"]
            limits = diagnostics["metrics"]["gl41Limits"]

            self.assertEqual(diagnostics["failures"], [])
            self.assertEqual(executor["active"], 1)
            self.assertEqual(executor["fboPostProcess"], 1)
            self.assertEqual(executor["uboFrameObjectConstants"], 1)
            self.assertEqual(executor["timerQueries"], 1)
            self.assertEqual(executor["syncAwareUploads"], 1)
            self.assertEqual(executor["staticBufferOwnership"], 1)
            self.assertEqual(executor["dynamicBufferOwnership"], 1)
            self.assertEqual(executor["macOS41Ceiling"], 1)
            self.assertEqual(support["modernPostChain"], 1)
            self.assertEqual(support["sceneLinearOutput"], 1)
            self.assertEqual(support["highQualitySdr"], 1)
            self.assertEqual(limits["debugOutputRequired"], 0)
            self.assertEqual(limits["bufferStorageRequired"], 0)
            self.assertEqual(limits["directStateAccessRequired"], 0)
            self.assertEqual(limits["multiDrawIndirectRequired"], 0)
            self.assertEqual(limits["persistentUploadsRequired"], 0)

    def test_gl41_diagnostics_reject_accidental_gl43_gl44_gl45_requirements(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "switch.log"
            log.write_text(
                "\n".join(
                    [
                        "  product tier: GL41",
                        "  GL41 mac-modern executor: active yes, FBO postprocess no, UBO frame/object constants no, timer queries no, sync-aware uploads no, static buffer ownership no, dynamic buffer ownership no, macOS 4.1 ceiling no",
                        "  GL41 mac-modern support: material compiler no, common materials no, dynamic entities no, modern post chain no, scene-linear output no, high-quality SDR no, optional hardware HDR yes, screenshots yes, demos yes",
                        "  GL41 mac-modern GL4+ requirements: debug output yes, buffer storage yes, direct state access yes, multi-draw indirect yes, persistent uploads yes",
                    ]
                ),
                encoding="utf-8",
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-off")
            failures = "\n".join(diagnostics["failures"])

            self.assertIn("fboPostProcess", failures)
            self.assertIn("uboFrameObjectConstants", failures)
            self.assertIn("timerQueries", failures)
            self.assertIn("syncAwareUploads", failures)
            self.assertIn("staticBufferOwnership", failures)
            self.assertIn("dynamicBufferOwnership", failures)
            self.assertIn("macOS41Ceiling", failures)
            self.assertIn("materialCompiler", failures)
            self.assertIn("modernPostChain", failures)
            self.assertIn("sceneLinearOutput", failures)
            self.assertIn("highQualitySdr", failures)
            self.assertIn("debugOutputRequired", failures)
            self.assertIn("bufferStorageRequired", failures)
            self.assertIn("directStateAccessRequired", failures)
            self.assertIn("multiDrawIndirectRequired", failures)
            self.assertIn("persistentUploadsRequired", failures)

    def test_gl46_diagnostics_report_high_end_executor_contract(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "switch.log"
            log.write_text(
                "\n".join(
                    [
                        "  product tier: GL46",
                        "  GL46 high-end executor: active yes, persistent uploads yes, buffer storage uploads yes, sync-heavy streaming yes, direct state access yes, multi-draw indirect yes, aggressive static-world submission yes, detailed GPU counters yes",
                        "  GL46 high-end support: material compiler yes, common materials yes, dynamic entities yes, modern post chain yes, scene-linear output yes, hardware HDR output yes, screenshots yes, demos yes",
                        "  GL46 high-end requirements: debug output yes, buffer storage yes, direct state access yes, multi-draw indirect yes",
                    ]
                ),
                encoding="utf-8",
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-off")
            executor = diagnostics["metrics"]["gl46Executor"]
            support = diagnostics["metrics"]["gl46Support"]
            requirements = diagnostics["metrics"]["gl46Requirements"]

            self.assertEqual(diagnostics["failures"], [])
            self.assertEqual(executor["active"], 1)
            self.assertEqual(executor["persistentUploads"], 1)
            self.assertEqual(executor["bufferStorageUploads"], 1)
            self.assertEqual(executor["syncHeavyStreaming"], 1)
            self.assertEqual(executor["directStateAccess"], 1)
            self.assertEqual(executor["multiDrawIndirect"], 1)
            self.assertEqual(executor["aggressiveStaticWorldSubmission"], 1)
            self.assertEqual(executor["detailedGpuCounters"], 1)
            self.assertEqual(support["hardwareHdrOutput"], 1)
            self.assertEqual(requirements["debugOutputRequired"], 1)
            self.assertEqual(requirements["bufferStorageRequired"], 1)
            self.assertEqual(requirements["directStateAccessRequired"], 1)
            self.assertEqual(requirements["multiDrawIndirectRequired"], 1)

    def test_gl46_diagnostics_reject_missing_high_end_requirements(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "switch.log"
            log.write_text(
                "\n".join(
                    [
                        "  product tier: GL46",
                        "  GL46 high-end executor: active yes, persistent uploads no, buffer storage uploads no, sync-heavy streaming no, direct state access no, multi-draw indirect no, aggressive static-world submission no, detailed GPU counters no",
                        "  GL46 high-end support: material compiler no, common materials no, dynamic entities no, modern post chain no, scene-linear output no, hardware HDR output no, screenshots yes, demos yes",
                        "  GL46 high-end requirements: debug output no, buffer storage no, direct state access no, multi-draw indirect no",
                    ]
                ),
                encoding="utf-8",
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-off")
            failures = "\n".join(diagnostics["failures"])

            self.assertIn("persistentUploads", failures)
            self.assertIn("bufferStorageUploads", failures)
            self.assertIn("syncHeavyStreaming", failures)
            self.assertIn("directStateAccess", failures)
            self.assertIn("multiDrawIndirect", failures)
            self.assertIn("aggressiveStaticWorldSubmission", failures)
            self.assertIn("detailedGpuCounters", failures)
            self.assertIn("materialCompiler", failures)
            self.assertIn("hardwareHdrOutput", failures)
            self.assertIn("debugOutputRequired", failures)
            self.assertIn("bufferStorageRequired", failures)
            self.assertIn("directStateAccessRequired", failures)
            self.assertIn("multiDrawIndirectRequired", failures)

    def test_glx_diagnostics_report_high_risk_stream_material_draws(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "switch.log"
            log.write_text(
                "\n".join(
                    [
                        "  dynamic stream draws: 3/3 attempts, 9 verts, 9 indexes, 0.01 MB, index 0.01 MB, tex1 0.00 MB, mt 0, fog 0, depthfrag 0, texmod 0, env 0, dlight 0, screen 1, video 1, shadow 0, beam 0, post 0, fallbacks 0",
                    ]
                ),
                encoding="utf-8",
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-parity")
            failures = "\n".join(diagnostics["failures"])
            stream_draw = diagnostics["metrics"]["streamDraw"]

            self.assertIn("screen-map", failures)
            self.assertIn("video-map", failures)
            self.assertEqual(stream_draw["dynamicLights"], 0)
            self.assertEqual(stream_draw["screenMaps"], 1)
            self.assertEqual(stream_draw["videoMaps"], 1)

    def test_glx_diagnostics_report_material_program_stream_skip(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "switch.log"
            log.write_text(
                "\n".join(
                    [
                        "  dynamic stream draw skips: 1 (bind 0, input 0, mt 0, depthfrag 0, texcoord 0, empty 0, key 0, fog 0, program 1)",
                    ]
                ),
                encoding="utf-8",
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-parity")
            failures = "\n".join(diagnostics["failures"])
            stream_draw = diagnostics["metrics"]["streamDraw"]
            stream_draw_skips = diagnostics["metrics"]["streamDrawSkip"]

            self.assertIn("material-program", failures)
            self.assertEqual(stream_draw["skips"], 1)
            self.assertEqual(stream_draw_skips["program"], 1)

    def test_glx_diagnostics_report_renderer_failures(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "switch.log"
            log.write_text(
                "\n".join(
                    [
                        "  material renderer: enabled, ready no, GLSL 1.20",
                        "  material compiles: 1 attempts, 1 compile failures, 1 link failures, precache 1/1, bind failures 1, labels 0",
                        "  FBO: requested yes, ready no, programs yes, framebuffer funcs yes, reason: FBO creation failed",
                        "  FBO lifecycle: 1 init attempts, 0 ready, 1 failed, 0 disabled, 0 shutdowns",
                        "  bloom create: last fbo, 0/1 ready, texture-unit failures 0, FBO failures 1",
                        "  dynamic stream buffer: no",
                        "  dynamic stream uploads: 1 calls, 0.01 MB, failures 1",
                        "  dynamic stream reservation failures: 1",
                        "  static world GLx renderer: no, arena upload no, arena draw no",
                        "  static world GLx multidraw indirect: yes, 0/1 calls, 0 runs, 0 indexes, fallbacks 0, skips 0, errors 1, largest 0",
                    ]
                ),
                encoding="utf-8",
            )

            diagnostics = glx_runtime_sweep.analyze_glx_diagnostics(log, "glx-parity")
            failures = "\n".join(diagnostics["failures"])
            self.assertIn("material renderer", failures)
            self.assertIn("compile failures", failures)
            self.assertIn("FBO was requested", failures)
            self.assertIn("dynamic stream buffer", failures)
            self.assertIn("static world renderer", failures)

    def test_gate_evaluation_fails_on_diagnostic_failures(self) -> None:
        manifest = {
            "gate": "rc-parity",
            "dryRun": False,
            "runs": [
                {
                    "type": "switch-screenshots",
                    "status": "passed",
                    "screenshots": [
                        {"name": "shot", "found": True, "histogram": screenshot_histogram()},
                    ],
                    "diagnostics": {
                        "found": True,
                        "failures": ["GLx material compile failures: 1."],
                        "metrics": color_diagnostics_metrics(),
                    },
                },
                {
                    "type": "timedemo",
                    "status": "passed",
                    "renderer": "opengl",
                    "demo": "demo1",
                    "timedemoMetrics": {"fps": 100.0},
                },
                {
                    "type": "timedemo",
                    "status": "passed",
                    "renderer": "glx",
                    "demo": "demo1",
                    "timedemoMetrics": {"fps": 100.0},
                },
            ],
            "renderers": ["opengl", "glx"],
            "demos": ["demo1"],
        }

        failures = glx_runtime_sweep.evaluate_gate(manifest)
        self.assertTrue(any("GLx diagnostic failures" in failure for failure in failures))

    def test_gate_evaluation_requires_per_frame_color_metadata(self) -> None:
        metrics = color_diagnostics_metrics()
        metrics.pop("colorFrame")
        manifest = {
            "gate": "rc-smoke",
            "dryRun": False,
            "runs": [
                {
                    "type": "switch-screenshots",
                    "status": "passed",
                    "screenshots": [
                        {"name": "shot", "found": True, "histogram": screenshot_histogram()},
                    ],
                    "diagnostics": {
                        "found": True,
                        "failures": [],
                        "metrics": metrics,
                    },
                    "performance": locked_performance_sample(),
                },
            ],
            "renderers": ["opengl", "glx"],
            "demos": [],
        }

        failures = glx_runtime_sweep.evaluate_gate(manifest)
        self.assertTrue(any("colorFrame" in failure for failure in failures))

    def test_gate_evaluation_requires_detailed_performance_color_frame_metadata(self) -> None:
        performance = locked_performance_sample()
        performance["latest"].pop("colorFrameContractValid")
        manifest = {
            "gate": "rc-smoke",
            "dryRun": False,
            "runs": [
                {
                    "type": "switch-screenshots",
                    "status": "passed",
                    "screenshots": [
                        {"name": "shot", "found": True, "histogram": screenshot_histogram()},
                    ],
                    "diagnostics": {
                        "found": True,
                        "failures": [],
                        "metrics": color_diagnostics_metrics(),
                    },
                    "performance": performance,
                },
            ],
            "renderers": ["opengl", "glx"],
            "demos": [],
        }

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("complete color-pipeline metadata" in failure for failure in failures))

    def test_release_gates_require_renderer_switch_lifecycle_evidence(self) -> None:
        manifest = release_proof_manifest("rc-smoke", "windows-x64")
        manifest.pop("rendererSwitchEvidence")

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(
            any("No renderer-switch lifecycle evidence" in failure for failure in failures)
        )

    def test_renderer_switch_lifecycle_requires_completed_transitions(self) -> None:
        manifest = release_proof_manifest("rc-smoke", "windows-x64")
        switch_run = next(
            run for run in manifest["runs"] if run.get("type") == "switch-screenshots"
        )
        switch_run["screenshots"][0]["found"] = False
        manifest["rendererSwitchEvidence"] = (
            glx_runtime_sweep.renderer_switch_lifecycle_evidence(manifest)
        )

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(
            any("did not complete every planned transition" in failure for failure in failures)
        )

    def test_renderer_switch_lifecycle_requires_round_trip_for_blocking_gates(self) -> None:
        manifest = release_proof_manifest("rc-proof", "windows-x64")
        switch_run = next(
            run for run in manifest["runs"] if run.get("type") == "switch-screenshots"
        )
        manifest["switchSequence"] = ["opengl", "glx"]
        switch_run["switchSequence"] = manifest["switchSequence"]
        switch_run["screenshots"] = [
            shot for shot in switch_run["screenshots"] if int(shot["switchStep"]) <= 2
        ]
        manifest["rendererSwitchEvidence"] = (
            glx_runtime_sweep.renderer_switch_lifecycle_evidence(manifest)
        )

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("switch back out of GLx" in failure for failure in failures))

    def test_renderer_switch_lifecycle_records_restart_and_glx_leg_evidence(self) -> None:
        manifest = release_proof_manifest("rc-parity", "linux-x86_64")
        evidence = manifest["rendererSwitchEvidence"]

        self.assertEqual(evidence["status"], "passed")
        self.assertEqual(evidence["restartMode"], "fast")
        self.assertEqual(evidence["vidRestartPath"], "CL_Vid_Restart(REF_KEEP_WINDOW)")
        self.assertTrue(evidence["vidRestartEquivalent"])
        self.assertEqual(
            evidence["plannedTransitions"],
            len(manifest["maps"]) * manifest["switchRounds"] * len(manifest["switchSequence"]),
        )
        self.assertEqual(evidence["completedTransitions"], evidence["plannedTransitions"])
        self.assertGreater(evidence["transitionsIntoGlx"], 0)
        self.assertGreater(evidence["transitionsOutOfGlx"], 0)
        self.assertTrue(evidence["glxDiagnosticsFound"])
        self.assertGreater(evidence["glxPerformanceSamples"], 0)

    def test_world_proof_evidence_passes_for_blocking_rc_surface(self) -> None:
        manifest = release_proof_manifest("rc-proof", "windows-x64")
        evidence = manifest["worldProofEvidence"]

        self.assertEqual(evidence["version"], glx_runtime_sweep.GLX_WORLD_PROOF_VERSION)
        self.assertEqual(evidence["status"], "passed")
        self.assertIn("q3dm17", evidence["requiredMaps"])
        self.assertIn("q3dm15", evidence["requiredMaps"])
        self.assertIn("q3dm15", evidence["glxScreenshotMaps"])
        self.assertGreater(evidence["staticWorld"]["drawAttempts"], 0)
        self.assertEqual(evidence["staticWorld"]["drawFallbacks"], 0)
        self.assertTrue(evidence["lightmaps"]["ok"])
        self.assertTrue(evidence["fog"]["ok"])
        self.assertEqual(glx_runtime_sweep.evaluate_gate(manifest), [])

    def test_world_proof_evidence_requires_versioned_object(self) -> None:
        manifest = release_proof_manifest("rc-parity", "windows-x64")
        manifest.pop("worldProofEvidence")

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("No world proof evidence" in failure for failure in failures))

    def test_world_proof_evidence_rejects_stale_version_and_missing_fog(self) -> None:
        manifest = release_proof_manifest("rc-proof", "windows-x64")
        requirements = glx_runtime_sweep.RC_GATE_PRESETS["rc-proof"]["requirements"]
        for run in manifest["runs"]:
            diagnostics = run.get("diagnostics")
            if isinstance(diagnostics, dict) and isinstance(diagnostics.get("metrics"), dict):
                metrics = diagnostics["metrics"]
                if isinstance(metrics.get("staticWorld"), dict):
                    metrics["staticWorld"].update(
                        {
                            "rendererEnabled": 0,
                            "arenaReady": 0,
                            "packetBatchBatches": 0,
                        }
                    )
                if isinstance(metrics.get("gl2xSupport"), dict):
                    metrics["gl2xSupport"].update({"lightmaps": 0, "fog": 0})
                if isinstance(metrics.get("gl12Support"), dict):
                    metrics["gl12Support"].update({"lightmaps": 0, "fog": 0})
                if isinstance(metrics.get("streamDraw"), dict):
                    metrics["streamDraw"]["fog"] = 0
                if isinstance(metrics.get("materialParameters"), dict):
                    metrics["materialParameters"].update({"blocks": 0, "hash": 0})

            performance = run.get("performance")
            if isinstance(performance, dict) and isinstance(performance.get("latest"), dict):
                performance["latest"].update(
                    {
                        "staticDrawAttempts": 0,
                        "staticDrawIndexes": 0,
                        "staticDrawPacketFull": 0,
                        "staticDrawPacketPartial": 0,
                        "streamDrawFog": 0,
                        "materialParameterBlocks": 0,
                        "materialParameterHash": 0,
                    }
                )

            for shot in run.get("screenshots", []):
                if (
                    isinstance(shot, dict)
                    and str(shot.get("renderer", "")).lower() == "glx"
                    and str(shot.get("map", "")).lower() == "q3dm15"
                ):
                    shot["found"] = False

        manifest["performanceAggregate"] = {"sampleCount": 1, "latest": {}, "max": {}}
        manifest["worldProofEvidence"] = glx_runtime_sweep.world_proof_evidence(
            manifest,
            requirements,
        )
        manifest["worldProofEvidence"]["version"] = 0
        failures = glx_runtime_sweep.evaluate_world_proof(manifest, requirements)
        text = "\n".join(failures)

        self.assertIn("unsupported version", text)
        self.assertIn("static-world renderer", text)
        self.assertIn("fog-heavy screenshot", text)
        self.assertIn("fog support", text)

    def test_material_proof_evidence_passes_for_rc_proof_surface(self) -> None:
        manifest = release_proof_manifest("rc-proof", "windows-x64")
        evidence = manifest["materialProofEvidence"]

        self.assertEqual(evidence["version"], glx_runtime_sweep.GLX_MATERIAL_PROOF_VERSION)
        self.assertEqual(evidence["status"], "passed")
        self.assertIn("q3dm11", evidence["requiredMaps"])
        self.assertEqual(evidence["renderer"]["enabled"], 1)
        self.assertEqual(evidence["renderer"]["ready"], 1)
        self.assertGreater(evidence["renderer"]["programs"], 0)
        self.assertGreater(evidence["parameters"]["blocks"], 0)
        self.assertGreater(evidence["parameters"]["hash"], 0)
        self.assertEqual(evidence["parameters"]["invalid"], 0)
        self.assertGreater(evidence["streamFeatures"]["multitexture"], 0)
        self.assertGreater(evidence["streamFeatures"]["depthFragment"], 0)
        self.assertGreater(evidence["streamFeatures"]["texMod"], 0)
        self.assertGreater(evidence["streamFeatures"]["environment"], 0)
        self.assertEqual(glx_runtime_sweep.evaluate_gate(manifest), [])

    def test_material_proof_evidence_proves_staged_animated_screen_video_surface(self) -> None:
        manifest = release_proof_manifest("rc-stress", "windows-x64")
        evidence = manifest["materialProofEvidence"]
        stage_flags = evidence["stageFlags"]

        self.assertEqual(evidence["status"], "passed")
        self.assertIn("animatedImage", evidence["requiredStageFlags"])
        self.assertIn("screenMap", evidence["requiredStageFlags"])
        self.assertIn("videoMap", evidence["requiredStageFlags"])
        self.assertGreater(stage_flags["counts"]["animatedImage"], 0)
        self.assertGreater(stage_flags["counts"]["screenMap"], 0)
        self.assertGreater(stage_flags["counts"]["videoMap"], 0)
        self.assertGreater(evidence["streamFeatures"]["screenMap"], 0)
        self.assertGreater(evidence["streamFeatures"]["videoMap"], 0)
        self.assertEqual(glx_runtime_sweep.evaluate_gate(manifest), [])

    def test_material_proof_evidence_keeps_screen_video_guarded_out_of_rc_proof(self) -> None:
        manifest = release_proof_manifest("rc-proof", "windows-x64")
        evidence = manifest["materialProofEvidence"]

        self.assertIn("screenMap", evidence["forbiddenStreamFeatures"])
        self.assertIn("videoMap", evidence["forbiddenStreamFeatures"])
        self.assertEqual(evidence["streamFeatures"]["screenMap"], 0)
        self.assertEqual(evidence["streamFeatures"]["videoMap"], 0)
        self.assertEqual(evidence["streamMaterialGates"]["screenMap"]["accepted"], 0)
        self.assertEqual(evidence["streamMaterialGates"]["videoMap"]["accepted"], 0)
        self.assertEqual(glx_runtime_sweep.evaluate_gate(manifest), [])

    def test_material_proof_evidence_requires_versioned_object(self) -> None:
        manifest = release_proof_manifest("rc-proof", "windows-x64")
        manifest.pop("materialProofEvidence")

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("No material proof evidence" in failure for failure in failures))

    def test_material_proof_evidence_rejects_stale_version_and_unsafe_counters(self) -> None:
        manifest = release_proof_manifest("rc-proof", "windows-x64")
        requirements = glx_runtime_sweep.RC_GATE_PRESETS["rc-proof"]["requirements"]
        for run in manifest["runs"]:
            diagnostics = run.get("diagnostics")
            if isinstance(diagnostics, dict) and isinstance(diagnostics.get("metrics"), dict):
                metrics = diagnostics["metrics"]
                if isinstance(metrics.get("material"), dict):
                    metrics["material"].update(
                        {
                            "ready": 0,
                            "compile": 1,
                            "link": 1,
                            "precacheFailures": 1,
                            "bind": 1,
                        }
                    )
                if isinstance(metrics.get("materialCompilerPlans"), dict):
                    metrics["materialCompilerPlans"].update(
                        {"unsupported": 1, "lastUnsupported": 0x80}
                    )
                if isinstance(metrics.get("materialFallbacks"), dict):
                    metrics["materialFallbacks"].update({"disabled": 1})
                if isinstance(metrics.get("materialParameters"), dict):
                    metrics["materialParameters"].update(
                        {"blocks": 0, "invalid": 1, "hash": 0}
                    )
                if isinstance(metrics.get("streamDraw"), dict):
                    metrics["streamDraw"].update(
                        {
                            "multitexture": 0,
                            "depthFragment": 0,
                            "texMods": 0,
                            "environment": 0,
                            "fallbacks": 1,
                        }
                    )
                if isinstance(metrics.get("streamMaterialGate"), dict):
                    metrics["streamMaterialGate"].update(
                        {
                            "multitextureAccepted": 0,
                            "depthFragmentAccepted": 0,
                            "texModAccepted": 0,
                            "environmentAccepted": 0,
                        }
                    )

            performance = run.get("performance")
            if isinstance(performance, dict) and isinstance(performance.get("latest"), dict):
                performance["latest"].update(
                    {
                        "materialReady": "not-ready",
                        "materialPrograms": 0,
                        "materialCompileFailures": 1,
                        "materialLinkFailures": 1,
                        "materialPrecacheFailures": 1,
                        "materialBindFailures": 1,
                        "materialParameterBlocks": 0,
                        "materialParameterHash": 0,
                        "materialInvalidParameterBlocks": 1,
                        "streamDrawMultitexture": 0,
                        "streamDrawDepthFragment": 0,
                        "streamDrawTexMods": 0,
                        "streamDrawEnvironment": 0,
                        "streamDrawFallbacks": 1,
                    }
                )

        manifest["performanceAggregate"] = {"sampleCount": 1, "latest": {}, "max": {}}
        manifest["materialProofEvidence"] = glx_runtime_sweep.material_proof_evidence(
            manifest,
            requirements,
        )
        manifest["materialProofEvidence"]["version"] = 0
        failures = glx_runtime_sweep.evaluate_material_proof(manifest, requirements)
        text = "\n".join(failures)

        self.assertIn("unsupported version", text)
        self.assertIn("material renderer ready", text)
        self.assertIn("material failures", text)
        self.assertIn("disabled=1", text)
        self.assertIn("unsupported compiler plans", text)
        self.assertIn("invalid parameter blocks", text)
        self.assertIn("stream feature evidence", text)

    def test_material_proof_evidence_rejects_missing_staged_stage_flags_and_guards(self) -> None:
        manifest = release_proof_manifest("rc-stress", "windows-x64")
        requirements = glx_runtime_sweep.RC_GATE_PRESETS["rc-stress"]["requirements"]
        for run in manifest["runs"]:
            diagnostics = run.get("diagnostics")
            if isinstance(diagnostics, dict) and isinstance(diagnostics.get("metrics"), dict):
                metrics = diagnostics["metrics"]
                if isinstance(metrics.get("materialParameters"), dict):
                    metrics["materialParameters"]["flags"] = 0x0C23
                    metrics["materialParameters"]["features"] = 0x0C23
                if isinstance(metrics.get("materialLastKey"), dict):
                    metrics["materialLastKey"]["flags"] = 0x0C23
                if isinstance(metrics.get("materialLanguage"), dict):
                    metrics["materialLanguage"]["flags"] = 0x0C23
                if isinstance(metrics.get("materialStageFlags"), dict):
                    metrics["materialStageFlags"]["animatedImage"] = 0
                    metrics["materialStageFlags"]["screenMap"] = 0
                    metrics["materialStageFlags"]["videoMap"] = 0
                if isinstance(metrics.get("streamDraw"), dict):
                    metrics["streamDraw"]["screenMaps"] = 0
                    metrics["streamDraw"]["videoMaps"] = 0
                metrics.pop("streamMaterialGate", None)

        manifest["performanceAggregate"] = {"sampleCount": 1, "latest": {}, "max": {}}
        manifest["materialProofEvidence"] = glx_runtime_sweep.material_proof_evidence(
            manifest,
            requirements,
        )
        failures = glx_runtime_sweep.evaluate_material_proof(manifest, requirements)
        text = "\n".join(failures)

        self.assertIn("stage-flag evidence", text)
        self.assertIn("animatedImage", text)
        self.assertIn("screenMap", text)
        self.assertIn("videoMap", text)
        self.assertIn("stream feature evidence", text)
        self.assertIn("stream guard evidence", text)

    def test_dynamic_proof_evidence_passes_for_rc_proof_surface(self) -> None:
        manifest = release_proof_manifest("rc-proof", "windows-x64")
        evidence = manifest["dynamicProofEvidence"]

        self.assertEqual(evidence["version"], glx_runtime_sweep.GLX_DYNAMIC_PROOF_VERSION)
        self.assertEqual(evidence["status"], "passed")
        self.assertIn("q3dm1", evidence["requiredMaps"])
        self.assertIn("q3dm6", evidence["requiredMaps"])
        self.assertIn("demo1", evidence["requiredDemos"])
        self.assertGreater(evidence["streamCategories"]["entity"]["draws"], 0)
        self.assertGreater(evidence["streamCategories"]["weapon"]["draws"], 0)
        self.assertGreater(evidence["streamCategories"]["dlight"]["draws"], 0)
        self.assertGreater(evidence["streamFeatures"]["shadow"], 0)
        self.assertGreater(evidence["streamFeatures"]["dynamicLight"], 0)
        self.assertGreater(evidence["renderIRDlightOwnership"]["streamDraws"], 0)
        self.assertGreater(evidence["renderIRDlightOwnership"]["role"]["draws"], 0)
        self.assertGreater(evidence["renderIRDlightOwnership"]["pass"]["draws"], 0)
        self.assertGreater(evidence["dlightScissor"]["active"], 0)
        self.assertGreater(evidence["dlightScissor"]["computed"], 0)
        self.assertGreater(evidence["dlightScissor"]["applied"], 0)
        self.assertIn("GL2X", evidence["tierSupport"]["dynamicEntities"])
        self.assertIn("GL12", evidence["tierSupport"]["dynamicLights"])
        self.assertGreater(evidence["streamGuards"]["dynamicLight"]["accepted"], 0)
        self.assertEqual(glx_runtime_sweep.evaluate_gate(manifest), [])

    def test_dynamic_proof_evidence_proves_staged_particles_and_beams(self) -> None:
        manifest = release_proof_manifest("rc-stress", "windows-x64")
        evidence = manifest["dynamicProofEvidence"]

        self.assertEqual(evidence["status"], "passed")
        self.assertIn("fnql_glx_particles01", evidence["requiredDemos"])
        self.assertGreater(evidence["streamCategories"]["particle"]["draws"], 0)
        self.assertGreater(evidence["streamCategories"]["poly"]["draws"], 0)
        self.assertGreater(evidence["streamCategories"]["mark"]["draws"], 0)
        self.assertGreater(evidence["streamCategories"]["beam"]["draws"], 0)
        self.assertGreater(evidence["streamFeatures"]["beam"], 0)
        self.assertGreater(evidence["streamFeatures"]["dynamicLight"], 0)
        self.assertEqual(glx_runtime_sweep.evaluate_gate(manifest), [])

    def test_dynamic_proof_evidence_requires_versioned_object(self) -> None:
        manifest = release_proof_manifest("rc-proof", "windows-x64")
        manifest.pop("dynamicProofEvidence")

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("No dynamic proof evidence" in failure for failure in failures))

    def test_dynamic_proof_rejects_stale_or_missing_scissor_evidence(self) -> None:
        manifest = release_proof_manifest("rc-proof", "windows-x64")
        requirements = glx_runtime_sweep.RC_GATE_PRESETS["rc-proof"]["requirements"]
        evidence = copy.deepcopy(manifest["dynamicProofEvidence"])
        evidence["version"] = 1
        evidence.pop("dlightScissor", None)
        evidence["status"] = "passed"
        evidence["failures"] = []
        manifest["dynamicProofEvidence"] = evidence

        failures = glx_runtime_sweep.evaluate_dynamic_proof(manifest, requirements)
        text = "\n".join(failures)

        self.assertIn("unsupported version 1", text)
        self.assertIn("projected-dlight scissor evidence", text)

        evidence["version"] = glx_runtime_sweep.GLX_DYNAMIC_PROOF_VERSION
        failures = glx_runtime_sweep.evaluate_dynamic_proof(manifest, requirements)
        text = "\n".join(failures)

        self.assertNotIn("unsupported version", text)
        self.assertIn("projected-dlight scissor evidence", text)

    def test_dynamic_proof_rejects_stale_version_and_unsafe_counters(self) -> None:
        manifest = release_proof_manifest("rc-proof", "windows-x64")
        requirements = glx_runtime_sweep.RC_GATE_PRESETS["rc-proof"]["requirements"]
        for run in manifest["runs"]:
            diagnostics = run.get("diagnostics")
            if isinstance(diagnostics, dict) and isinstance(diagnostics.get("metrics"), dict):
                metrics = diagnostics["metrics"]
                if isinstance(metrics.get("gl12Support"), dict):
                    metrics["gl12Support"].update(  # type: ignore[index, union-attr]
                        {
                            "sprites": 0,
                            "dynamicLights": 0,
                            "stencilShadows": 0,
                        }
                    )
                if isinstance(metrics.get("gl2xSupport"), dict):
                    metrics["gl2xSupport"].update(  # type: ignore[index, union-attr]
                        {
                            "dynamicEntities": 0,
                            "sprites": 0,
                        }
                    )
                if isinstance(metrics.get("streamDraw"), dict):
                    metrics["streamDraw"].update(  # type: ignore[index, union-attr]
                        {
                            "draws": 0,
                            "attempts": 0,
                            "indexes": 0,
                            "dynamicLights": 1,
                            "shadows": 0,
                            "fallbacks": 1,
                            "skips": 1,
                        }
                    )
                if isinstance(metrics.get("streamCategory"), dict):
                    metrics["streamCategory"].update(  # type: ignore[index, union-attr]
                        {
                            "entityDraws": 0,
                            "entityAttempts": 0,
                            "entityFallbacks": 1,
                            "weaponDraws": 0,
                            "weaponAttempts": 0,
                        }
                    )
                if isinstance(metrics.get("streamMaterialGate"), dict):
                    metrics["streamMaterialGate"].update(  # type: ignore[index, union-attr]
                        {
                            "dynamicLightEnabled": 1,
                            "dynamicLightAccepted": 1,
                            "dynamicLightRejected": 0,
                        }
                    )
                if isinstance(metrics.get("renderIRDynamicRoles"), dict):
                    metrics["renderIRDynamicRoles"].update(  # type: ignore[index, union-attr]
                        {
                            "dlightDraws": 0,
                            "dlightIndexes": 0,
                            "dlightVertices": 0,
                        }
                    )
                if isinstance(metrics.get("renderIRDynamicPasses"), dict):
                    metrics["renderIRDynamicPasses"].update(  # type: ignore[index, union-attr]
                        {
                            "dlightDraws": 0,
                            "dlightIndexes": 0,
                            "dlightVertices": 0,
                        }
                    )
                if isinstance(metrics.get("dlightScissor"), dict):
                    metrics["dlightScissor"].update(  # type: ignore[index, union-attr]
                        {
                            "active": 0,
                            "candidates": 0,
                            "computed": 0,
                            "applied": 0,
                            "fallbacks": 0,
                            "pixels": 0,
                            "viewportPixels": 0,
                        }
                    )

            performance = run.get("performance")
            if isinstance(performance, dict) and isinstance(performance.get("latest"), dict):
                performance["latest"].update(
                    {
                        "draws": 0,
                        "streamDrawAttempts": 0,
                        "streamDrawIndexes": 0,
                        "streamDrawDynamicLights": 1,
                        "streamDrawShadows": 0,
                        "streamDrawFallbacks": 1,
                        "streamDrawSkips": 1,
                        "streamCategoryEntityDraws": 0,
                        "streamCategoryEntityAttempts": 0,
                        "streamCategoryWeaponDraws": 0,
                        "streamCategoryWeaponAttempts": 0,
                        "renderIRRoleDlightDraws": 0,
                        "renderIRRoleDlightIndexes": 0,
                        "renderIRRoleDlightVertices": 0,
                        "renderIRPassDlightDraws": 0,
                        "renderIRPassDlightIndexes": 0,
                        "renderIRPassDlightVertices": 0,
                        "dlightScissorActive": 0,
                        "dlightScissorCandidates": 0,
                        "dlightScissorComputed": 0,
                        "dlightScissorApplied": 0,
                        "dlightScissorFallbacks": 0,
                        "dlightScissorPixels": 0,
                        "dlightScissorViewportPixels": 0,
                    }
                )

        manifest["performanceAggregate"] = {"sampleCount": 1, "latest": {}, "max": {}}
        manifest["dynamicProofEvidence"] = glx_runtime_sweep.dynamic_proof_evidence(
            manifest,
            requirements,
        )
        manifest["dynamicProofEvidence"]["version"] = 0
        failures = glx_runtime_sweep.evaluate_dynamic_proof(manifest, requirements)
        text = "\n".join(failures)

        self.assertIn("unsupported version", text)
        self.assertIn("stream category evidence", text)
        self.assertIn("stream feature evidence", text)
        self.assertIn("tier-support evidence", text)
        self.assertIn("render-IR dlight role ownership", text)
        self.assertIn("render-IR dynamic-lights pass ownership", text)
        self.assertIn("projected-dlight scissor active state", text)
        self.assertIn("computed projected-dlight scissor", text)
        self.assertIn("applied projected-dlight scissor", text)
        self.assertIn("dynamic stream draw attempts", text)
        self.assertIn("submitted dynamic stream indexes or draws", text)
        self.assertIn("stream draw fallbacks", text)
        self.assertIn("stream draw skips", text)

    def test_post_proof_evidence_passes_for_rc_proof_surface(self) -> None:
        manifest = release_proof_manifest("rc-proof", "windows-x64")
        evidence = manifest["postProofEvidence"]

        self.assertEqual(evidence["version"], glx_runtime_sweep.GLX_POST_PROOF_VERSION)
        self.assertEqual(evidence["status"], "passed")
        self.assertIn("q3dm1", evidence["requiredMaps"])
        self.assertIn("q3dm17", evidence["requiredMaps"])
        self.assertIn("q3dm1", evidence["glxScreenshotMaps"])
        self.assertIn("q3dm17", evidence["glxScreenshotMaps"])
        self.assertEqual(evidence["fbo"]["requested"], 1)
        self.assertEqual(evidence["fbo"]["ready"], 1)
        self.assertEqual(evidence["fbo"]["initFailures"], 0)
        self.assertGreater(evidence["frames"]["post"], 0)
        self.assertGreater(evidence["frames"]["screenshots"], 0)
        self.assertEqual(evidence["frames"]["minimizedOutput"], 0)
        self.assertTrue(evidence["featureEvidence"]["greyscale"]["ok"])
        self.assertTrue(evidence["featureEvidence"]["renderScale"]["ok"])
        self.assertTrue(evidence["featureEvidence"]["renderScale"]["dimensionEvidence"])
        self.assertEqual(evidence["colorContract"], 1)
        self.assertEqual(glx_runtime_sweep.evaluate_gate(manifest), [])

    def test_post_proof_evidence_requires_versioned_object(self) -> None:
        manifest = release_proof_manifest("rc-proof", "windows-x64")
        manifest.pop("postProofEvidence")

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("No post proof evidence" in failure for failure in failures))

    def test_post_proof_rejects_stale_version_and_missing_feature_evidence(self) -> None:
        manifest = release_proof_manifest("rc-proof", "windows-x64")
        requirements = glx_runtime_sweep.RC_GATE_PRESETS["rc-proof"]["requirements"]
        for run in manifest["runs"]:
            diagnostics = run.get("diagnostics")
            if isinstance(diagnostics, dict) and isinstance(diagnostics.get("metrics"), dict):
                metrics = diagnostics["metrics"]
                if isinstance(metrics.get("postprocess"), dict):
                    metrics["postprocess"].update(
                        {
                            "fboRequested": 1,
                            "fboReady": 0,
                            "fboAttempts": 1,
                            "fboFailed": 1,
                            "lastOutput": "minimized",
                        }
                    )
                if isinstance(metrics.get("postprocessControls"), dict):
                    metrics["postprocessControls"].update(
                        {
                            "renderScale": 0,
                            "greyscale": 0.0,
                            "windowAdjusted": 0,
                        }
                    )
                if isinstance(metrics.get("postprocessFrames"), dict):
                    metrics["postprocessFrames"].update(
                        {
                            "frames": 0,
                            "screenshots": 0,
                            "minimizedOutput": 1,
                        }
                    )
                if isinstance(metrics.get("postprocessFrameFeatures"), dict):
                    metrics["postprocessFrameFeatures"].update(
                        {
                            "renderScale": 0,
                            "greyscale": 0,
                            "windowAdjusted": 0,
                            "minimized": 1,
                        }
                    )
                if isinstance(metrics.get("targetFormat"), dict):
                    metrics["targetFormat"].update(
                        {
                            "renderWidth": 640,
                            "renderHeight": 480,
                            "captureWidth": 640,
                            "captureHeight": 480,
                            "windowWidth": 640,
                            "windowHeight": 480,
                        }
                    )
                if isinstance(metrics.get("colorAudit"), dict):
                    metrics["colorAudit"]["contract"] = 0

            performance = run.get("performance")
            if isinstance(performance, dict) and isinstance(performance.get("latest"), dict):
                performance["latest"].update(
                    {
                        "fbo": "failed",
                        "postprocessRenderScaleMode": 0,
                        "postprocessGreyscale": 0,
                        "postprocessWindowAdjusted": 0,
                        "postprocessFrames": 0,
                        "postprocessScreenshots": 0,
                        "postprocessMinimizedOutput": 1,
                        "postprocessFeatureRenderScale": 0,
                        "postprocessFeatureGreyscale": 0,
                        "postprocessFeatureWindowAdjusted": 0,
                        "postprocessFeatureMinimized": 1,
                        "targetRenderWidth": 640,
                        "targetRenderHeight": 480,
                        "targetCaptureWidth": 640,
                        "targetCaptureHeight": 480,
                        "targetWindowWidth": 640,
                        "targetWindowHeight": 480,
                        "colorOutputContract": 0,
                    }
                )

        manifest["postProofEvidence"] = glx_runtime_sweep.post_proof_evidence(
            manifest,
            requirements,
        )
        manifest["postProofEvidence"]["version"] = 0
        failures = glx_runtime_sweep.evaluate_post_proof(manifest, requirements)
        text = "\n".join(failures)

        self.assertIn("unsupported version", text)
        self.assertIn("ready postprocess FBO", text)
        self.assertIn("FBO init failures", text)
        self.assertIn("postprocess frames", text)
        self.assertIn("feature evidence", text)
        self.assertIn("greyscale", text)
        self.assertIn("renderScale", text)
        self.assertIn("minimized output", text)
        self.assertIn("valid output color contract", text)

    def test_color_sweep_rejects_frame_dump_that_disagrees_with_row(self) -> None:
        manifest = release_proof_manifest("rc-parity", "windows-x64")
        color_run = next(
            run
            for run in manifest["runs"]
            if run.get("type") == "color-sweep"
            and run["colorSweepRow"]["id"] == "scene-linear-sdr-aces"
        )
        color_frame = color_run["diagnostics"]["metrics"]["colorFrame"]["latest"]
        color_frame["space"] = "display-referred-sdr"

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("colorFrame.space" in failure for failure in failures))

    def test_color_sweep_rejects_invalid_frame_contract(self) -> None:
        manifest = release_proof_manifest("rc-parity", "windows-x64")
        color_run = next(
            run
            for run in manifest["runs"]
            if run.get("type") == "color-sweep"
            and run["colorSweepRow"]["id"] == "scene-linear-sdr-aces"
        )
        color_frame = color_run["diagnostics"]["metrics"]["colorFrame"]["latest"]
        color_frame["contractValid"] = False

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("colorFrame.contractValid" in failure for failure in failures))

    def test_color_sweep_rejects_srgb_decode_state_drift(self) -> None:
        manifest = release_proof_manifest("rc-parity", "windows-x64")
        color_run = next(
            run
            for run in manifest["runs"]
            if run.get("type") == "color-sweep"
            and run["colorSweepRow"]["id"] == "hdr-srgb-decode-off"
        )
        metrics = color_run["diagnostics"]["metrics"]
        metrics["colorAudit"]["srgbDecode"] = 1
        metrics["colorFrame"]["latest"]["srgbDecode"] = True

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("colorAudit.srgbDecode" in failure for failure in failures))
        self.assertTrue(any("colorFrame.srgbDecode" in failure for failure in failures))

    def test_color_contract_rejects_texture_classification_policy_drift(self) -> None:
        contracts = color_contracts()
        manifest_rows = contracts["textureClassificationManifest"]["rows"]
        authored = next(row for row in manifest_rows if row["id"] == "authored-color")
        authored["sceneLinearDecode"] = "never"

        failures = glx_runtime_sweep.validate_color_contract_manifest(
            {"colorContracts": contracts}
        )

        self.assertTrue(any("authored-color sceneLinearDecode" in failure for failure in failures))

    def test_color_sweep_requires_luma_false_color_sidecars(self) -> None:
        manifest = release_proof_manifest("rc-parity", "windows-x64")
        color_run = next(
            run
            for run in manifest["runs"]
            if run.get("type") == "color-sweep"
        )
        color_run["screenshots"][0].pop("falseColor")

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("luma false-color sidecar" in failure for failure in failures))

    def test_color_sweep_requires_exposure_false_color_sidecars(self) -> None:
        manifest = release_proof_manifest("rc-parity", "windows-x64")
        color_run = next(
            run
            for run in manifest["runs"]
            if run.get("type") == "color-sweep"
        )
        color_run["screenshots"][0].pop("exposureFalseColor")

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("exposure false-color sidecar" in failure for failure in failures))

    def test_color_sweep_requires_shader_reference_ramps(self) -> None:
        manifest = release_proof_manifest("rc-parity", "windows-x64")
        manifest["imageEvidence"]["shaderReferenceRamps"].pop()

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("shader-vs-reference ramp evidence" in failure for failure in failures))


class GlxRuntimeSweepProfileTests(unittest.TestCase):
    def test_frozen_profiles_match_runtime_module_table(self) -> None:
        runtime_profiles = parse_runtime_glx_profiles()

        self.assertEqual(glx_runtime_sweep.GLX_RC_PROFILE_CVARS, runtime_profiles["rc"])
        self.assertEqual(glx_runtime_sweep.GLX_STRESS_PROFILE_CVARS, runtime_profiles["stress"])
        self.assertEqual(
            glx_runtime_sweep.PROFILE_CVARS["glx-parity"],
            {"r_glxProfile": "rc", **glx_runtime_sweep.GLX_RC_PROFILE_CVARS},
        )
        self.assertEqual(
            glx_runtime_sweep.PROFILE_CVARS["glx-ownership"],
            {
                "r_glxProfile": "rc",
                **glx_runtime_sweep.GLX_RC_PROFILE_CVARS,
                "r_glxRequireOwnership": "1",
            },
        )
        self.assertEqual(
            glx_runtime_sweep.PROFILE_CVARS["glx-dlight-shader"],
            {
                "r_glxProfile": "rc",
                **glx_runtime_sweep.GLX_RC_PROFILE_CVARS,
                "r_dynamiclight": "1",
                # legacy world dlight mode + classic VBO world path: the only
                # configuration where projected world-packet binds can execute
                "r_dlightMode": "0",
                "r_glxMaterialRenderer": "0",
                "r_glxDlightProjectedProgram": "1",
            },
        )
        self.assertEqual(
            glx_runtime_sweep.PROFILE_CVARS["glx-dlight-mdi"],
            {
                "r_glxProfile": "rc",
                **glx_runtime_sweep.GLX_RC_PROFILE_CVARS,
                "r_dynamiclight": "1",
                "r_dlightMode": "0",
                "r_glxDlightProjectedProgram": "1",
                "r_glxDlightProjectedMdi": "1",
            },
        )
        self.assertEqual(
            glx_runtime_sweep.PROFILE_CVARS["glx-stress"],
            {"r_glxProfile": "stress", **glx_runtime_sweep.GLX_STRESS_PROFILE_CVARS},
        )

    def test_official_proof_corpus_covers_task_o_scene_families(self) -> None:
        all_tags = set(glx_runtime_sweep.corpus_tags(glx_runtime_sweep.GLX_PROOF_CORPUS_SCENES))
        self.assertTrue(
            {
                "stock-map",
                "high-geometry",
                "shader-heavy",
                "fog-heavy",
                "modern-map",
                "particle-heavy-demo",
                "ui-hud-sensitive",
                "color-grade-proof",
                "tone-map-proof",
                "screenshot-parity",
                "demo-playback-parity",
                "hud-parity",
                "shadow-parity",
                "bloom-parity",
                "cel-shading-parity",
                "outline-parity",
                "greyscale-proof",
                "render-scale-proof",
                "performance-comparison",
                "projected-dlight-parity",
                "projected-dlight-static-world",
                "projected-dlight-dynamic",
                "projected-dlight-resource-overlimit",
            }.issubset(all_tags)
        )

        stress_tags = set(
            glx_runtime_sweep.corpus_tags(
                glx_runtime_sweep.GLX_GATE_CORPUS_SCENES["rc-stress"]
            )
        )
        self.assertTrue({"modern-map", "particle-heavy-demo"}.issubset(stress_tags))
        self.assertEqual(
            glx_runtime_sweep.PROFILE_MAPS["glx-color"],
            "q3dm17,q3dm11,q3dm15",
        )
        self.assertEqual(
            glx_runtime_sweep.PROFILE_MAPS["glx-material"],
            "q3dm1,q3dm11",
        )
        self.assertEqual(
            glx_runtime_sweep.corpus_scene_ids_for_profile("glx-dlight-shader"),
            ("stock-q3dm1-hud", "timedemo-demo1"),
        )
        self.assertEqual(
            glx_runtime_sweep.profile_parity_suite_ids("glx-dlight-shader"),
            ("projected-dlight-shader",),
        )
        self.assertEqual(
            glx_runtime_sweep.corpus_scene_ids_for_profile("glx-dlight-mdi"),
            ("stock-q3dm1-hud", "timedemo-demo1"),
        )
        self.assertEqual(glx_runtime_sweep.PROFILE_CVARS["glx-color"]["r_colorGrade"], "3")

    def test_task_v_parity_suites_are_versioned_and_gate_enforced(self) -> None:
        required_suites = (
            "screenshot",
            "demo-playback",
            "hud",
            "shadow",
            "bloom",
            "cel-shading",
            "greyscale",
            "render-scale",
            "projected-dlight-shader",
        )
        self.assertEqual(set(glx_runtime_sweep.GLX_PARITY_SUITES), set(required_suites))

        proof_corpus = proof_corpus_for_gate("rc-proof")
        self.assertEqual(
            proof_corpus["paritySuiteVersion"],
            glx_runtime_sweep.GLX_PARITY_SUITE_VERSION,
        )
        self.assertEqual(
            set(proof_corpus["paritySuiteIds"]),
            set(glx_runtime_sweep.GLX_GATE_PARITY_SUITES["rc-proof"]),
        )
        self.assertTrue(
            all(
                set(record["sceneIds"]).issubset(set(proof_corpus["selectedSceneIds"]))
                for record in proof_corpus["paritySuites"]
            )
        )

        switch_args = argparse.Namespace(
            startup_wait=1,
            map_wait=1,
            switch_wait=1,
            screenshot_wait=1,
            perf_sample_wait=0,
            switch_rounds=1,
            profile="glx-parity",
            no_perf_samples=True,
        )
        switch_cfg, expected_shots = glx_runtime_sweep.build_switch_cfg(
            switch_args,
            {},
            ["q3dm6", "q3dm11"],
            ["opengl", "glx"],
            "parity-suite-test",
            "gs12345678",
            glx_runtime_sweep.GLX_GATE_CORPUS_SCENES["rc-proof"],
            glx_runtime_sweep.GLX_GATE_PARITY_SUITES["rc-proof"],
        )
        self.assertIn('set cg_shadows "2"', switch_cfg)
        self.assertIn('set r_celShading "1"', switch_cfg)
        q3dm6_shots = [shot for shot in expected_shots if shot["map"] == "q3dm6"]
        q3dm11_shots = [shot for shot in expected_shots if shot["map"] == "q3dm11"]
        self.assertTrue(all("shadow" in shot["paritySuiteIds"] for shot in q3dm6_shots))
        self.assertTrue(all("cel-shading" in shot["paritySuiteIds"] for shot in q3dm11_shots))

        dlight_args = argparse.Namespace(
            startup_wait=1,
            map_wait=1,
            switch_wait=1,
            screenshot_wait=1,
            perf_sample_wait=0,
            switch_rounds=1,
            profile="glx-dlight-shader",
            no_perf_samples=True,
        )
        dlight_cfg, dlight_shots = glx_runtime_sweep.build_switch_cfg(
            dlight_args,
            {},
            ["q3dm1"],
            ["opengl", "glx"],
            "projected-dlight-suite-test",
            "gd12345678",
            glx_runtime_sweep.corpus_scene_ids_for_profile("glx-dlight-shader"),
            glx_runtime_sweep.profile_parity_suite_ids("glx-dlight-shader"),
        )
        self.assertIn('set r_glxDlightProjectedProgram "1"', dlight_cfg)
        self.assertIn('set r_glxDlightProjectedProgram "0"', dlight_cfg)
        dlight_glx_shots = [shot for shot in dlight_shots if shot["renderer"] == "glx"]
        self.assertEqual(len(dlight_glx_shots), 2)
        legacy_shot = next(
            shot for shot in dlight_glx_shots
            if shot["projectedDlightShaderParityRole"] ==
            glx_runtime_sweep.GLX_PROJECTED_DLIGHT_SHADER_PARITY_LEGACY_ROLE
        )
        glx_shot = next(
            shot for shot in dlight_glx_shots
            if shot["projectedDlightShaderParityRole"] ==
            glx_runtime_sweep.GLX_PROJECTED_DLIGHT_SHADER_PARITY_CANDIDATE_ROLE
        )
        self.assertTrue(legacy_shot["skipExternalBaseline"])
        self.assertTrue(glx_shot["projectedDlightShaderParity"])
        self.assertEqual(glx_shot["legacyFallbackBaseline"], "compat-projected-light")
        self.assertEqual(glx_shot["legacyFallbackCaptureName"], legacy_shot["name"])
        self.assertEqual(glx_shot["baselineSourceName"], legacy_shot["name"])
        self.assertIn("projected-dlight-legacy-fallback", glx_shot["baselineKey"])
        demo_cfg, demo_shots = glx_runtime_sweep.build_demo_cfg(
            dlight_args,
            {},
            "demo1",
            "glx",
            "gd12345678",
            glx_runtime_sweep.corpus_scene_ids_for_profile("glx-dlight-shader"),
            glx_runtime_sweep.profile_parity_suite_ids("glx-dlight-shader"),
        )
        self.assertIn('set r_glxDlightProjectedProgram "0"', demo_cfg)
        self.assertIn("glx_dlight_demo_legacy_capture", demo_cfg)
        self.assertIn("glx_dlight_demo_shader_run", demo_cfg)
        self.assertIn("glx_dlight_demo_shader_capture", demo_cfg)
        self.assertIn("demo demo1", demo_cfg)
        self.assertEqual(len(demo_shots), 2)
        demo_legacy = next(
            shot for shot in demo_shots
            if shot["projectedDlightShaderParityRole"] ==
            glx_runtime_sweep.GLX_PROJECTED_DLIGHT_SHADER_PARITY_LEGACY_ROLE
        )
        demo_candidate = next(
            shot for shot in demo_shots
            if shot["projectedDlightShaderParityRole"] ==
            glx_runtime_sweep.GLX_PROJECTED_DLIGHT_SHADER_PARITY_CANDIDATE_ROLE
        )
        self.assertTrue(demo_legacy["projectedDlightShaderTimedemoParity"])
        self.assertEqual(demo_candidate["legacyFallbackCaptureName"], demo_legacy["name"])
        self.assertIn("projected-dlight-demo-legacy-fallback", demo_candidate["baselineKey"])

        broken = dict(proof_corpus)
        broken["paritySuiteIds"] = ["screenshot"]
        broken["paritySuites"] = glx_runtime_sweep.parity_suite_records(["screenshot"])
        manifest = {
            "proofCorpus": broken,
            "maps": glx_runtime_sweep.corpus_targets(
                glx_runtime_sweep.GLX_GATE_CORPUS_SCENES["rc-proof"],
                "map",
            ),
            "demos": ["demo1"],
        }
        failures = glx_runtime_sweep.evaluate_proof_corpus(
            manifest,
            glx_runtime_sweep.RC_GATE_PRESETS["rc-proof"]["requirements"],
        )
        self.assertTrue(any("parity suite(s) missing" in failure for failure in failures))

    def test_gate_presets_derive_scene_targets_from_proof_corpus(self) -> None:
        for gate, scene_ids in glx_runtime_sweep.GLX_GATE_CORPUS_SCENES.items():
            with self.subTest(gate=gate):
                defaults = glx_runtime_sweep.RC_GATE_PRESETS[gate]["defaults"]
                self.assertEqual(
                    defaults["corpus_scenes"],
                    glx_runtime_sweep.corpus_scene_ids_csv(scene_ids),
                )
                self.assertEqual(
                    defaults["maps"],
                    glx_runtime_sweep.corpus_targets_csv(scene_ids, "map"),
                )
                self.assertEqual(
                    defaults["demos"],
                    glx_runtime_sweep.corpus_targets_csv(scene_ids, "demo"),
                )

    def test_corpus_manifest_is_gate_enforced(self) -> None:
        proof_corpus = proof_corpus_for_gate("rc-proof")

        self.assertEqual(proof_corpus["version"], glx_runtime_sweep.GLX_PROOF_CORPUS_VERSION)
        self.assertEqual(proof_corpus["document"], glx_runtime_sweep.GLX_PROOF_CORPUS_DOC)
        self.assertIn("stock-q3dm6-geometry", proof_corpus["selectedSceneIds"])
        self.assertIn("fog-heavy", proof_corpus["selectedTags"])

        broken = dict(proof_corpus)
        broken["selectedTags"] = ["stock-map"]
        manifest = {
            "proofCorpus": broken,
            "maps": glx_runtime_sweep.corpus_targets(
                glx_runtime_sweep.GLX_GATE_CORPUS_SCENES["rc-proof"],
                "map",
            ),
            "demos": ["demo1"],
        }
        failures = glx_runtime_sweep.evaluate_proof_corpus(
            manifest,
            glx_runtime_sweep.RC_GATE_PRESETS["rc-proof"]["requirements"],
        )
        self.assertTrue(any("missing required tag" in failure for failure in failures))

    def test_frozen_rc_profile_promotes_static_world_acceleration(self) -> None:
        profile = glx_runtime_sweep.GLX_RC_PROFILE_CVARS

        self.assertEqual(profile["r_glxWorldRenderer"], "1")
        self.assertEqual(profile["r_glxStreamDraw"], "1")
        self.assertEqual(profile["r_glxMaterialRenderer"], "1")
        self.assertEqual(profile["r_glxMaterialPrecache"], "1")
        self.assertEqual(profile["r_glxStaticWorldArena"], "1")
        self.assertEqual(profile["r_glxStaticWorldArenaDraw"], "1")
        self.assertEqual(profile["r_glxStaticWorldDraw"], "1")
        self.assertEqual(profile["r_glxStaticWorldSoftDraw"], "1")
        self.assertEqual(profile["r_glxStaticWorldPacketBatch"], "1")
        self.assertEqual(profile["r_glxStaticWorldMultiDraw"], "1")
        self.assertEqual(profile["r_glxStaticWorldIndirectBuffer"], "1")
        self.assertEqual(profile["r_glxStaticWorldIndirectDraw"], "1")
        self.assertEqual(profile["r_glxStaticWorldMultiDrawIndirect"], "1")
        self.assertEqual(profile["r_glxStaticWorldMultiDrawIndirectCompact"], "0")
        self.assertEqual(profile["r_glxStaticWorldMultiDrawIndirectSpans"], "1")

    def test_stress_profile_only_adds_compact_static_world_mdi(self) -> None:
        rc_profile = dict(glx_runtime_sweep.GLX_RC_PROFILE_CVARS)
        stress_profile = dict(glx_runtime_sweep.GLX_STRESS_PROFILE_CVARS)

        rc_profile["r_glxStaticWorldMultiDrawIndirectCompact"] = "1"
        self.assertEqual(stress_profile, rc_profile)

    def test_material_profile_requests_compile_cache_stress(self) -> None:
        profile = glx_runtime_sweep.PROFILE_CVARS["glx-material"]

        self.assertEqual(profile["r_glxMaterialRenderer"], "1")
        self.assertEqual(profile["r_glxMaterialPrecache"], "1")
        self.assertEqual(profile["r_glxGpuTiming"], "1")
        self.assertEqual(profile["r_glxGpuPassTiming"], "1")

    def test_diagnostic_gates_force_per_frame_color_debug(self) -> None:
        args = argparse.Namespace(
            width=640,
            height=480,
            profile="glx-parity",
            gate="rc-smoke",
            extra_set=[],
        )
        cvars = glx_runtime_sweep.make_cvars(args)
        self.assertEqual(cvars["r_glxColorPipelineDebug"], "2")

        args.extra_set = ["r_glxColorPipelineDebug=0"]
        cvars = glx_runtime_sweep.make_cvars(args)
        self.assertEqual(cvars["r_glxColorPipelineDebug"], "0")

    def test_extra_set_rejects_cfg_injection(self) -> None:
        with self.assertRaisesRegex(ValueError, "not safe"):
            glx_runtime_sweep.parse_extra_sets(["r_safe;quit=1"])
        with self.assertRaisesRegex(ValueError, "unsafe control"):
            glx_runtime_sweep.parse_extra_sets(["r_safe=1\nquit"])

    def test_runtime_options_reject_invalid_dimensions_waits_and_timeouts(self) -> None:
        args = argparse.Namespace(
            width=640,
            height=480,
            timeout=180.0,
            switch_rounds=1,
            startup_wait=30,
            map_wait=180,
            switch_wait=12,
            screenshot_wait=8,
            perf_sample_wait=4,
        )
        glx_runtime_sweep.validate_runtime_options(args)

        args.width = 0
        with self.assertRaisesRegex(ValueError, "width"):
            glx_runtime_sweep.validate_runtime_options(args)

        args.width = 640
        args.switch_rounds = 0
        with self.assertRaisesRegex(ValueError, "switch-rounds"):
            glx_runtime_sweep.validate_runtime_options(args)

        args.switch_rounds = 1
        args.timeout = 0
        with self.assertRaisesRegex(ValueError, "timeout"):
            glx_runtime_sweep.validate_runtime_options(args)

        args.timeout = float("nan")
        with self.assertRaisesRegex(ValueError, "timeout"):
            glx_runtime_sweep.validate_runtime_options(args)

    def test_runtime_thresholds_reject_non_finite_values(self) -> None:
        self.assertEqual(
            glx_runtime_sweep.validate_screenshot_thresholds(2.0, 0.005),
            (2.0, 0.005),
        )
        with self.assertRaisesRegex(ValueError, "screenshot-max-rms"):
            glx_runtime_sweep.validate_screenshot_thresholds(float("nan"), 0.005)
        with self.assertRaisesRegex(ValueError, "screenshot-max-pixel-ratio"):
            glx_runtime_sweep.validate_screenshot_thresholds(2.0, float("inf"))
        with self.assertRaisesRegex(ValueError, "performance-max-growth-ratio"):
            glx_runtime_sweep.validate_performance_growth_ratio(float("nan"))
        self.assertEqual(
            glx_runtime_sweep.validate_performance_growth_ratio(None),
            glx_runtime_sweep.DEFAULT_PERFORMANCE_MAX_GROWTH_RATIO,
        )

    def test_q3_command_tokens_reject_cfg_and_path_injection(self) -> None:
        self.assertEqual(
            glx_runtime_sweep.validate_q3_command_tokens(["q3dm1", "demos/demo1.dm_68"], "--maps"),
            ["q3dm1", "demos/demo1.dm_68"],
        )
        with self.assertRaisesRegex(ValueError, "safe Quake 3"):
            glx_runtime_sweep.validate_q3_command_tokens(["q3dm1\nquit"], "--maps")
        with self.assertRaisesRegex(ValueError, "single safe mod"):
            glx_runtime_sweep.validate_fs_game("../baseq3")

    def test_rc_parity_gate_enables_dlight_shadow_scenes(self) -> None:
        defaults = glx_runtime_sweep.RC_GATE_PRESETS["rc-parity"]["defaults"]
        requirements = glx_runtime_sweep.RC_GATE_PRESETS["rc-parity"]["requirements"]

        self.assertTrue(defaults["dlight_shadow_scenes"])
        self.assertTrue(requirements["require_dlight_shadow_scenes"])

    def test_dlight_shadow_config_uses_startup_cvars_and_test_lights(self) -> None:
        args = argparse.Namespace(
            startup_wait=1,
            map_wait=1,
            screenshot_wait=1,
            perf_sample_wait=1,
            profile="glx-parity",
            no_perf_samples=False,
        )
        cvars = glx_runtime_sweep.dlight_shadow_scene_cvars({"r_fbo": "1"})
        startup = glx_runtime_sweep.launch_cvars(cvars)
        scenes = glx_runtime_sweep.dlight_shadow_evidence_scenes()

        cfg, expected_shots = glx_runtime_sweep.build_dlight_shadow_cfg(
            args,
            cvars,
            scenes,
            "shadow-test",
            "gs12345678",
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
        self.assertIn("set r_csmShadows \"1\"", cfg)
        self.assertIn("set r_csmDebug \"1\"", cfg)
        self.assertIn("set r_csmDebugFallback \"1\"", cfg)
        self.assertIn("set r_csmDebugFallback \"4\"", cfg)
        self.assertIn("set r_staticLightDebug \"1\"", cfg)
        self.assertIn("set r_surfaceLightProxies \"1\"", cfg)
        self.assertIn("set r_surfaceLightProxyShadows \"1\"", cfg)
        self.assertIn("set r_spotShadows \"1\"", cfg)
        self.assertIn("r_dlightTest 8 720 224 48 0", cfg)
        self.assertIn("r_dlightTest 8 780 224 56 0", cfg)
        self.assertIn("r_dlightTest 16 900 256 72 0", cfg)
        self.assertIn("set r_speeds \"4\"", cfg)
        self.assertTrue(all(shot["shadowScene"] for shot in expected_shots))
        csm_path_shots = [shot for shot in expected_shots if shot.get("csmCameraPath")]
        self.assertEqual(len(csm_path_shots), len(glx_runtime_sweep.CSM_SHIMMER_CAMERA_PATH))
        self.assertEqual(
            [shot["csmPathStep"] for shot in csm_path_shots],
            ["baseline", "nudge-forward", "nudge-side", "micro-yaw"],
        )
        self.assertEqual(
            glx_runtime_sweep.dlight_shadow_scene_categories(expected_shots),
            set(glx_runtime_sweep.DLIGHT_SHADOW_EVIDENCE_CATEGORIES),
        )
        self.assertTrue(
            any(
                shot["baselineKey"]
                == "glx-parity-dlight-shadows-csm-shimmer-path-baseline-q3dm17-glx"
                for shot in expected_shots
            )
        )
        self.assertTrue(
            any(
                shot["baselineKey"] == "glx-parity-dlight-shadows-stress-light-budget-q3dm6-glx"
                for shot in expected_shots
            )
        )
        combined = next(
            scene for scene in scenes
            if scene["id"] == glx_runtime_sweep.COMBINED_SHADOW_ATLAS_SCENE_ID
        )
        self.assertEqual(combined["sidecarLights"][0]["type"], "spot")

    def test_dlight_shadow_sidecar_lights_are_staged_under_homepath(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            records = glx_runtime_sweep.write_dlight_shadow_sidecar_lights(
                root,
                "",
                glx_runtime_sweep.dlight_shadow_evidence_scenes(),
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
                    glx_runtime_sweep.COMBINED_SHADOW_ATLAS_SCENE_ID: {
                        "sampleCount": 1,
                        "max": maximum,
                    },
                },
            },
        }

        summary = glx_runtime_sweep.combined_shadow_atlas_summary(dlight_shadow)

        self.assertEqual(summary["status"], "passed")
        self.assertTrue(glx_runtime_sweep.combined_shadow_atlas_summary_active(summary))

        maximum["spotStaticPlans"] = 0
        maximum["scheduledMask"] = 0x0B
        failed = glx_runtime_sweep.combined_shadow_atlas_summary(dlight_shadow)

        self.assertEqual(failed["status"], "failed")
        self.assertTrue(
            any("static sidecar spot" in failure for failure in failed["failures"])
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
            for _reason in glx_runtime_sweep.CSM_FALLBACK_REQUIRED_REASONS
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
            for reason in glx_runtime_sweep.CSM_FALLBACK_REQUIRED_REASONS
        ]

        summary = glx_runtime_sweep.csm_fallback_summary(
            manager_samples,
            fallback_samples,
            glx_runtime_sweep.CSM_FALLBACK_REQUIRED_REASONS,
        )

        self.assertEqual(summary["status"], "passed")
        self.assertTrue(glx_runtime_sweep.csm_fallback_summary_active(summary))

        manager_samples[0]["csmPublished"] = 1
        failed = glx_runtime_sweep.csm_fallback_summary(
            manager_samples,
            fallback_samples[:-1],
            glx_runtime_sweep.CSM_FALLBACK_REQUIRED_REASONS,
        )

        self.assertEqual(failed["status"], "failed")
        self.assertTrue(
            any("csmPublished" in failure for failure in failed["failures"])
        )
        self.assertTrue(
            any("zero-cascade" in failure for failure in failed["failures"])
        )

    def test_csm_fallback_skip_logs_are_scene_scoped(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "glx.log"
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

            analysis = glx_runtime_sweep.analyze_dlight_shadow_log(log)

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
            log = Path(tmp) / "glx.log"
            log.write_text(
                "\n".join(
                    [
                        "DLIGHT_SHADOW_SCENE_BEGIN world-geometry",
                        "shadow manager view:1 frame:2 noworld:0 sched:3 mask:d "
                        "p:1 s:0 ca:1 cr:1 pub p:1 s:0 c:1 inputs dlight:4 "
                        "point:2/4 cand:3 records:2/3 atlas:1024x512/128 "
                        "fill:75% gen:7 spot:0/0 src static:0/0 surface:0/0 "
                        "atlas:0x0/0 fill:0% gen:0 csm:4 atlas:2048x512 gen:7",
                        "shadow atlas contract backend:glx atlas:point active:1 "
                        "tile:128 size:1024x512 records:2 fill:75% filter:poisson4 "
                        "pad:1 clamp:1 sampler:clamp-edge "
                        "allocation:priority-dlight-index deterministic:1",
                        "shadow atlas contract backend:glx atlas:spot active:0 "
                        "tile:0 size:0x0 records:0 fill:0% filter:poisson4 "
                        "pad:1 clamp:1 sampler:clamp-edge "
                        "allocation:priority-source-index deterministic:1",
                        "shadow atlas contract backend:glx atlas:csm active:1 "
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
                        "csm cascade backend:glx index:0 split:1..64 atlas:0,0/512 "
                        "view:0,0 512x512 api:0,0 512x512 sample-y:native "
                        "clip-y:native depth:forward ndc:-1..1 clear:1.0 compare:lequal "
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

            analysis = glx_runtime_sweep.analyze_dlight_shadow_log(log)

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
        self.assertEqual(atlas["max"]["shadowAtlasBackendGlx"], 1)
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
        self.assertEqual(cascade["max"]["csmCascadeBackendGlx"], 1)
        self.assertEqual(cascade["max"]["csmCascadeSampleYInverted"], 0)
        self.assertEqual(cascade["max"]["csmCascadeClipYFlipped"], 0)
        self.assertEqual(cascade["max"]["csmCascadeNdcZeroToOne"], 0)
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
        summary = glx_runtime_sweep.shadow_profile_summary(
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
        summary = glx_runtime_sweep.surface_light_spot_lod_summary(
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
        missing = glx_runtime_sweep.surface_light_spot_lod_summary(
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

    def test_gate_evaluation_requires_dlight_shadow_scene_evidence(self) -> None:
        manifest = release_proof_manifest("rc-parity", "linux-x86_64")
        manifest["runs"] = [
            run for run in manifest["runs"] if run.get("type") != "dlight-shadow-scenes"
        ]

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("dlight shadow scene run" in failure for failure in failures))

    def test_gate_evaluation_requires_dlight_shadow_category_evidence(self) -> None:
        manifest = release_proof_manifest("rc-parity", "linux-x86_64")
        shadow_run = next(
            run for run in manifest["runs"] if run.get("type") == "dlight-shadow-scenes"
        )
        shadow_run["screenshots"] = [
            shot for shot in shadow_run["screenshots"]
            if shot.get("evidenceCategories") != ["stress-light-budget"]
        ]
        shadow_run["dlightShadow"]["scenes"].pop("stress-light-budget")

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("stress-light-budget" in failure for failure in failures))

    def test_gate_evaluation_requires_shadow_manager_runtime_evidence(self) -> None:
        manifest = release_proof_manifest("rc-parity", "linux-x86_64")
        shadow_run = next(
            run for run in manifest["runs"] if run.get("type") == "dlight-shadow-scenes"
        )
        shadow_run["dlightShadow"].pop("shadowManager")

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("shadow manager" in failure for failure in failures))

    def test_gate_evaluation_requires_surface_light_spot_runtime_evidence(self) -> None:
        manifest = release_proof_manifest("rc-parity", "linux-x86_64")
        shadow_run = next(
            run for run in manifest["runs"] if run.get("type") == "dlight-shadow-scenes"
        )
        shadow_run["dlightShadow"].pop("surfaceLightSpot")

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(
            any("surfacelight spot manager" in failure for failure in failures)
        )

    def test_gate_evaluation_requires_csm_runtime_evidence(self) -> None:
        manifest = release_proof_manifest("rc-parity", "linux-x86_64")
        shadow_run = next(
            run for run in manifest["runs"] if run.get("type") == "dlight-shadow-scenes"
        )
        shadow_run["dlightShadow"].pop("csmShadows")

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("CSM runtime" in failure for failure in failures))

    def test_gate_evaluation_requires_csm_shimmer_screenshot_diff_smoke(self) -> None:
        manifest = release_proof_manifest("rc-parity", "linux-x86_64")
        shadow_run = next(
            run for run in manifest["runs"] if run.get("type") == "dlight-shadow-scenes"
        )
        shadow_run["csmShimmerScreenshots"] = csm_shimmer_screenshot_summary("failed")

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(
            any("CSM shimmer screenshot diff smoke" in failure for failure in failures)
        )

    def test_gate_evaluation_requires_combined_shadow_atlas_smoke(self) -> None:
        manifest = release_proof_manifest("rc-parity", "linux-x86_64")
        shadow_run = next(
            run for run in manifest["runs"] if run.get("type") == "dlight-shadow-scenes"
        )
        shadow_run["combinedShadowAtlas"] = combined_shadow_atlas_summary("failed")

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(
            any("combined shadow atlas smoke" in failure for failure in failures)
        )

    def test_gate_evaluation_requires_csm_fallback_smoke(self) -> None:
        manifest = release_proof_manifest("rc-parity", "linux-x86_64")
        shadow_run = next(
            run for run in manifest["runs"] if run.get("type") == "dlight-shadow-scenes"
        )
        shadow_run["csmFallbacks"] = csm_fallback_summary("failed")

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("CSM fallback smoke" in failure for failure in failures))

    def test_ownership_profile_preserves_independent_ownership_cvar(self) -> None:
        profile = dict(glx_runtime_sweep.PROFILE_CVARS["glx-ownership"])
        args = argparse.Namespace(profile="glx-ownership")

        startup = glx_runtime_sweep.launch_cvars(profile)
        filtered = glx_runtime_sweep.config_cvars(args, profile)

        self.assertEqual(startup["r_glxRequireOwnership"], "1")
        self.assertEqual(filtered["r_glxRequireOwnership"], "1")
        self.assertNotIn("r_glxStreamDraw", filtered)
        self.assertEqual(profile["r_glxStreamDrawDynamicLights"], "auto")
        self.assertEqual(profile["r_glxDlightScissor"], "auto")
        self.assertEqual(profile["r_glxStreamDrawScreenMaps"], "0")
        self.assertEqual(profile["r_glxStreamDrawVideoMaps"], "0")

    def test_dlight_shader_profile_preserves_projected_program_override(self) -> None:
        profile = dict(glx_runtime_sweep.PROFILE_CVARS["glx-dlight-shader"])
        args = argparse.Namespace(profile="glx-dlight-shader")

        startup = glx_runtime_sweep.launch_cvars(profile)
        filtered = glx_runtime_sweep.config_cvars(args, profile)

        self.assertEqual(startup["r_glxProfile"], "rc")
        self.assertEqual(startup["r_dynamiclight"], "1")
        self.assertNotIn("r_glxStreamDraw", filtered)
        self.assertEqual(filtered["r_glxDlightProjectedProgram"], "1")

    def test_dlight_mdi_profile_preserves_projected_program_and_mdi_overrides(self) -> None:
        profile = dict(glx_runtime_sweep.PROFILE_CVARS["glx-dlight-mdi"])
        args = argparse.Namespace(profile="glx-dlight-mdi")

        startup = glx_runtime_sweep.launch_cvars(profile)
        filtered = glx_runtime_sweep.config_cvars(args, profile)

        self.assertEqual(startup["r_glxProfile"], "rc")
        self.assertEqual(startup["r_dynamiclight"], "1")
        self.assertNotIn("r_glxStreamDraw", filtered)
        self.assertEqual(filtered["r_glxDlightProjectedProgram"], "1")
        self.assertEqual(filtered["r_glxDlightProjectedMdi"], "1")

    def test_proof_dir_defaults_wire_visual_and_performance_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            args = argparse.Namespace(
                proof_dir=root / "proof",
                approve_proof=False,
                screenshot_baseline_dir=None,
                screenshot_diff_dir=None,
                approve_screenshot_baselines=False,
                performance_baseline=None,
                approve_performance_baseline=False,
                summary_markdown=None,
            )

            glx_runtime_sweep.apply_proof_defaults(args, root / "run")

            self.assertEqual(args.screenshot_baseline_dir, root / "proof" / "screenshots")
            self.assertEqual(args.screenshot_diff_dir, root / "run" / "screenshot-diffs")
            self.assertEqual(args.performance_baseline, root / "proof" / "performance-baseline.json")
            self.assertEqual(args.summary_markdown, root / "run" / "summary.md")

    def test_proof_dir_defaults_support_individual_approval_flags(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            args = argparse.Namespace(
                proof_dir=root / "proof",
                approve_proof=False,
                screenshot_baseline_dir=None,
                screenshot_diff_dir=None,
                approve_screenshot_baselines=True,
                performance_baseline=None,
                approve_performance_baseline=True,
                summary_markdown=None,
            )

            glx_runtime_sweep.apply_proof_defaults(args, root / "run")

            self.assertEqual(args.screenshot_baseline_dir, root / "proof" / "screenshots")
            self.assertIsNone(args.screenshot_diff_dir)
            self.assertEqual(args.performance_baseline, root / "proof" / "performance-baseline.json")
            self.assertEqual(args.summary_markdown, root / "run" / "summary.md")

    def test_rc_proof_approval_mode_is_rejected_before_runtime_work(self) -> None:
        args = argparse.Namespace(
            gate="rc-proof",
            approve_proof=False,
            approve_screenshot_baselines=True,
            approve_performance_baseline=False,
        )

        with self.assertRaisesRegex(ValueError, "rc-proof compares"):
            glx_runtime_sweep.validate_proof_approval_mode(args)

        args.gate = "rc-parity"
        glx_runtime_sweep.validate_proof_approval_mode(args)

    def test_proof_status_fails_incomplete_visual_approval(self) -> None:
        manifest = {
            "dryRun": False,
            "screenshotBaselineDir": "proof/screenshots",
            "approveScreenshotBaselines": True,
            "runs": [
                {
                    "type": "switch-screenshots",
                    "status": "passed",
                    "screenshots": [
                        {"name": "shot", "found": False, "baselineStatus": "approved"},
                    ],
                },
            ],
        }

        proof = glx_runtime_sweep.proof_status(manifest)
        self.assertEqual(proof["status"], "failed")
        self.assertEqual(proof["visual"]["status"], "failed")

        empty = {**manifest, "runs": [{"type": "switch-screenshots", "status": "passed", "screenshots": []}]}
        proof = glx_runtime_sweep.proof_status(empty)
        self.assertEqual(proof["status"], "failed")
        self.assertEqual(proof["visual"]["status"], "failed")

    def test_rc_proof_gate_requires_reviewed_visual_and_performance_baselines(self) -> None:
        manifest = release_proof_manifest("rc-proof", "windows-x64")

        self.assertEqual(glx_runtime_sweep.evaluate_gate(manifest), [])
        proof = glx_runtime_sweep.proof_status(
            {
                **manifest,
                "performanceAggregate": {"sampleCount": 1},
            }
        )
        self.assertEqual(proof["status"], "passed")
        self.assertEqual(proof["visual"]["status"], "passed")
        self.assertEqual(proof["performance"]["status"], "passed")

        missing = dict(manifest)
        missing["screenshotBaselineDir"] = ""
        missing["performanceBaselinePath"] = ""
        failures = glx_runtime_sweep.evaluate_gate(missing)

        self.assertTrue(any("Visual proof requires" in failure for failure in failures))
        self.assertTrue(any("Performance proof requires" in failure for failure in failures))

    def test_rc_proof_gate_rejects_baseline_approval_mode(self) -> None:
        manifest = {
            "gate": "rc-proof",
            "dryRun": False,
            "performanceFailures": [],
            "runs": [
                {
                    "type": "switch-screenshots",
                    "status": "passed",
                    "screenshots": [
                        {
                            "name": "shot",
                            "found": True,
                            "baselineStatus": "approved",
                            "histogram": screenshot_histogram(),
                        }
                    ],
                    "diagnostics": {
                        "found": True,
                        "failures": [],
                        "metrics": color_diagnostics_metrics(),
                    },
                    "performance": locked_performance_sample(),
                },
            ],
            "renderers": ["opengl", "glx"],
            "demos": [],
            "screenshotBaselineDir": "proof/screenshots",
            "performanceBaselinePath": "proof/performance-baseline.json",
            "approveScreenshotBaselines": True,
            "approvePerformanceBaseline": True,
            "performanceBaselineStatus": "approved",
        }

        failures = glx_runtime_sweep.evaluate_gate(manifest)

        self.assertTrue(any("not approve" in failure for failure in failures))

    def test_release_proof_root_requires_all_blocking_platform_gates(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for platform_id in glx_runtime_sweep.GLX_BLOCKING_RELEASE_PLATFORMS:
                for gate in glx_runtime_sweep.GLX_RELEASE_REQUIRED_GATES:
                    manifest_dir = root / platform_id / gate / "run"
                    manifest_dir.mkdir(parents=True)
                    (manifest_dir / "manifest.json").write_text(
                        json.dumps(release_proof_manifest(gate, platform_id), indent=2),
                        encoding="utf-8",
                    )

            summary = glx_runtime_sweep.validate_release_proof_root(root)

            self.assertEqual(summary["status"], "passed")
            self.assertEqual(summary["failures"], [])
            self.assertEqual(
                len(summary["manifests"]),
                len(glx_runtime_sweep.GLX_BLOCKING_RELEASE_PLATFORMS)
                * len(glx_runtime_sweep.GLX_RELEASE_REQUIRED_GATES),
            )

    def test_release_proof_root_rejects_missing_platform_or_dry_run(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for gate in glx_runtime_sweep.GLX_RELEASE_REQUIRED_GATES:
                manifest = release_proof_manifest(gate, "windows-x64")
                if gate == "rc-proof":
                    manifest["dryRun"] = True
                manifest_dir = root / "windows-x64" / gate / "run"
                manifest_dir.mkdir(parents=True)
                (manifest_dir / "manifest.json").write_text(
                    json.dumps(manifest, indent=2),
                    encoding="utf-8",
                )

            summary = glx_runtime_sweep.validate_release_proof_root(root)
            failures = "\n".join(str(failure) for failure in summary["failures"])

            self.assertEqual(summary["status"], "failed")
            self.assertIn("Missing GLx rc-smoke runtime proof for linux-x86_64", failures)
            self.assertIn("No passing GLx rc-proof runtime proof for windows-x64", failures)
            self.assertIn("dry-run manifests do not count as release proof", failures)


class GlxWorkflowTests(unittest.TestCase):
    def test_runtime_workflow_proof_dir_preserves_threshold_inputs(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "glx-verification.yml").read_text(encoding="utf-8")

        self.assertIn("cron: '35 4 * * 1'", workflow)
        self.assertIn("github.event_name == 'schedule'", workflow)
        self.assertIn("FNQL_GLX_PROOF_PLATFORM", workflow)
        self.assertIn("--proof-platform", workflow)
        self.assertIn("if baseline_dir or proof_dir:", workflow)
        self.assertIn('"--screenshot-max-rms",', workflow)
        self.assertIn('os.environ["FNQL_GLX_SCREENSHOT_MAX_RMS"]', workflow)
        self.assertIn('"--screenshot-max-pixel-ratio",', workflow)
        self.assertIn('os.environ["FNQL_GLX_SCREENSHOT_MAX_PIXEL_RATIO"]', workflow)
        self.assertIn("if performance_baseline or proof_dir:", workflow)
        self.assertIn('"--performance-max-growth-ratio",', workflow)
        self.assertIn('os.environ["FNQL_GLX_PERFORMANCE_MAX_GROWTH_RATIO"]', workflow)

    def test_ci_and_release_artifacts_reference_proof_corpus(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "glx-verification.yml").read_text(encoding="utf-8")
        release_script = (ROOT / "scripts" / "release.py").read_text(encoding="utf-8")
        corpus_doc = (ROOT / glx_runtime_sweep.GLX_PROOF_CORPUS_DOC).read_text(encoding="utf-8")

        self.assertIn("--list-corpus", workflow)
        self.assertIn("GLX_PROOF_CORPUS.md", workflow)
        self.assertIn("release_corpus_manifest", release_script)
        self.assertIn("glx_proof_corpus", release_script)
        self.assertIn("--glx-proof-root", release_script)
        self.assertIn("validate_release_proof_root", release_script)
        self.assertIn("glx_runtime_proof", release_script)
        self.assertIn("GLX_PROMOTION.md", workflow)
        self.assertIn("glx-promotion.json", workflow)
        self.assertIn("glx_promotion", release_script)
        self.assertIn("GLX_VISUAL_DOSSIER.md", workflow)
        self.assertIn("GLX_VISUAL_DOSSIER.md", release_script)
        self.assertIn(glx_runtime_sweep.GLX_PROOF_CORPUS_VERSION, corpus_doc)


class GlxPromotionTests(unittest.TestCase):
    def write_manifest(
        self,
        root: Path,
        platform_id: str,
        name: str,
        manifest: dict[str, object],
    ) -> None:
        manifest_dir = root / platform_id / name / "run"
        manifest_dir.mkdir(parents=True)
        (manifest_dir / "manifest.json").write_text(
            json.dumps(manifest, indent=2),
            encoding="utf-8",
        )

    def write_complete_proof_root(self, root: Path) -> None:
        for platform_id in glx_runtime_sweep.GLX_BLOCKING_RELEASE_PLATFORMS:
            for gate in glx_runtime_sweep.GLX_RELEASE_REQUIRED_GATES:
                self.write_manifest(
                    root,
                    platform_id,
                    gate,
                    release_proof_manifest(gate, platform_id),
                )
            self.write_manifest(
                root,
                platform_id,
                "glx-ownership",
                ownership_proof_manifest(platform_id),
            )

    def write_rollback_metadata(
        self,
        root: Path,
        metadata: dict[str, object] | None = None,
    ) -> Path:
        path = root / "rollback-package.json"
        path.write_text(
            json.dumps(metadata or rollback_package_metadata(), indent=2),
            encoding="utf-8",
        )
        return path

    def test_current_tree_is_blocked_but_not_promoted(self) -> None:
        report = glx_promotion.promotion_report()

        self.assertEqual(report["status"], "blocked")
        self.assertFalse(report["policyViolation"])
        self.assertEqual(report["sourcePolicy"]["makeDefault"], "glx")
        self.assertEqual(report["sourcePolicy"]["mesonDefault"], "glx")
        self.assertEqual(report["sourcePolicy"]["makeUseGlxDefault"], "1")
        self.assertEqual(report["sourcePolicy"]["mesonUseGlxDefault"], "glx")
        checks = {check["name"]: check for check in report["checks"]}
        self.assertEqual(checks["feature-matrix-green"]["status"], "blocked")
        self.assertGreater(len(checks["feature-matrix-green"]["blockers"]), 0)
        self.assertEqual(checks["legacy-coupling-inventory"]["status"], "passed")
        self.assertEqual(checks["rollback-package-metadata"]["status"], "blocked")
        self.assertEqual(checks["migration-and-rollback-doc"]["status"], "passed")

    def test_feature_matrix_parser_skips_spaced_separator_rows(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            matrix = Path(tmp) / "matrix.md"
            matrix.write_text(
                "\n".join(
                    [
                        "| ID | Category | Feature | Status | Evidence | Closure gate |",
                        "| --- | --- | --- | --- | --- | --- |",
                        "| CORE-TEST | Core | Test feature | covered | Unit test | Keep green. |",
                    ]
                ),
                encoding="utf-8",
            )

            self.assertEqual(
                glx_promotion.parse_feature_matrix(matrix),
                [
                    {
                        "id": "CORE-TEST",
                        "category": "Core",
                        "feature": "Test feature",
                        "status": "covered",
                        "evidence": "Unit test",
                        "closure": "Keep green.",
                    }
                ],
            )

    def test_feature_matrix_parser_rejects_invalid_status(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            matrix = Path(tmp) / "matrix.md"
            matrix.write_text(
                "\n".join(
                    [
                        "| ID | Category | Feature | Status | Evidence | Closure gate |",
                        "|---|---|---|---|---|---|",
                        "| CORE-TEST | Core | Test feature | maybe | Unit test | Keep green. |",
                    ]
                ),
                encoding="utf-8",
            )

            with self.assertRaisesRegex(ValueError, "invalid status"):
                glx_promotion.parse_feature_matrix(matrix)

    def test_complete_runtime_and_ownership_proof_still_waits_for_green_matrix(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.write_complete_proof_root(root)
            rollback_metadata = self.write_rollback_metadata(root)

            report = glx_promotion.promotion_report(root, rollback_metadata)
            checks = {check["name"]: check for check in report["checks"]}

            self.assertEqual(checks["blocking-runtime-proof"]["status"], "passed")
            self.assertEqual(checks["ownership-proof"]["status"], "passed")
            self.assertEqual(checks["rollback-package-metadata"]["status"], "passed")
            self.assertEqual(checks["feature-matrix-green"]["status"], "blocked")
            self.assertEqual(report["status"], "blocked")

    def test_legacy_coupling_inventory_matches_build_systems_and_doc(self) -> None:
        check = glx_promotion.check_legacy_coupling_inventory()

        self.assertEqual(check["status"], "passed")
        self.assertEqual(check["remainingLegacyRendererSources"], 24)
        self.assertEqual(check["ratchetBudget"], 24)
        self.assertEqual(check["documentedCount"], 24)
        self.assertEqual(check["blockers"], [])
        self.assertIn("code/renderer/tr_arb.c", check["sources"])
        self.assertIn("code/renderer/tr_shade.c", check["sources"])

        builds = check["builds"]
        self.assertEqual(builds["meson"]["sources"], check["sources"])
        self.assertEqual(builds["makefile"]["sources"], check["sources"])
        self.assertEqual(builds["msvc"]["sources"], check["sources"])

    def test_legacy_coupling_inventory_blocks_missing_doc_rows(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            doc = Path(tmp) / "GLX_LEGACY_COUPLING.md"
            doc.write_text(
                "\n".join(
                    [
                        "| Source | Compatibility role | Extraction target |",
                        "|---|---|---|",
                        "| `code/renderer/tr_arb.c` | FBO substrate. | Split GLx postprocess. |",
                    ]
                ),
                encoding="utf-8",
            )

            check = glx_promotion.check_legacy_coupling_inventory(doc_path=doc)
            failures = "\n".join(str(failure) for failure in check["blockers"])

            self.assertEqual(check["status"], "blocked")
            self.assertIn("Legacy coupling ledger is missing source", failures)
            self.assertIn("code/renderer/tr_shade.c", failures)

    def test_rollback_package_metadata_validates_platform_artifacts_and_triggers(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            metadata_path = self.write_rollback_metadata(Path(tmp))

            check = glx_promotion.check_rollback_package_metadata(metadata_path)

            self.assertEqual(check["status"], "passed")
            self.assertEqual(check["metadataStatus"], "reviewed")
            self.assertEqual(
                check["coveredPlatforms"],
                sorted(glx_runtime_sweep.GLX_BLOCKING_RELEASE_PLATFORMS),
            )
            self.assertEqual(check["packageCount"], 1)
            self.assertEqual(check["packages"][0]["artifactDir"], "fnql-legacy-opengl")
            self.assertEqual(check["blockers"], [])

    def test_rollback_package_metadata_blocks_incomplete_contract(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            metadata_path = self.write_rollback_metadata(
                Path(tmp),
                rollback_package_metadata(
                    platforms=["windows-x64"],
                    legacy_renderers=["glx"],
                    required_artifacts={"proofCorpus": True},
                    triggers=["driver regression only"],
                ),
            )

            check = glx_promotion.check_rollback_package_metadata(metadata_path)
            failures = "\n".join(str(failure) for failure in check["blockers"])

            self.assertEqual(check["status"], "blocked")
            self.assertIn("legacy opengl renderer", failures)
            self.assertIn("promotionReport", failures)
            self.assertIn("releaseProofSummary", failures)
            self.assertIn("checksums", failures)
            self.assertIn("demo regressions", failures)
            self.assertIn("screenshot regressions", failures)
            self.assertIn("performance regressions", failures)
            self.assertIn("linux-x86_64", failures)

    def test_rollback_package_metadata_rejects_unsafe_artifact_names(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            metadata = rollback_package_metadata(artifact_dir="../legacy")
            packages = metadata["rollbackPackages"]
            assert isinstance(packages, list)
            package = packages[0]
            assert isinstance(package, dict)
            package["archive"] = "legacy/opengl.zip"
            metadata_path = self.write_rollback_metadata(Path(tmp), metadata)

            check = glx_promotion.check_rollback_package_metadata(metadata_path)
            failures = "\n".join(str(failure) for failure in check["blockers"])

            self.assertEqual(check["status"], "blocked")
            self.assertIn("unsafe artifactDir", failures)
            self.assertIn("unsafe archive", failures)

    def test_release_rollback_package_policy_is_not_required_until_source_promotion(self) -> None:
        args = argparse.Namespace(channel="release", glx_rollback_metadata=None)

        current = fnql_release.resolve_glx_rollback_package(
            args,
            {"sourcePolicy": {"promoted": False}},
        )
        promoted = fnql_release.resolve_glx_rollback_package(
            args,
            {"sourcePolicy": {"promoted": True}},
        )

        self.assertEqual(current["status"], "not-required")
        self.assertFalse(current["required"])
        self.assertEqual(promoted["status"], "missing")
        self.assertTrue(promoted["required"])

    def test_release_rollback_package_metadata_must_match_staged_archive(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            metadata_path = self.write_rollback_metadata(Path(tmp))
            args = argparse.Namespace(channel="release", glx_rollback_metadata=metadata_path)
            rollback = fnql_release.resolve_glx_rollback_package(
                args,
                {"sourcePolicy": {"promoted": True}},
            )
            archives = [
                {
                    "artifact_dir": "fnql-legacy-opengl",
                    "archive": "fnql-legacy-opengl.zip",
                    "path": ".install/packages/fnql-legacy-opengl.zip",
                    "sha256": "abc123",
                }
            ]

            rollback = fnql_release.attach_glx_rollback_archives(rollback, archives)

            self.assertEqual(rollback["matchedArchives"][0]["archive"], "fnql-legacy-opengl.zip")
            self.assertEqual(rollback["matchedArchives"][0]["sha256"], "abc123")

            with self.assertRaisesRegex(ValueError, "did not match a staged release archive"):
                fnql_release.attach_glx_rollback_archives(rollback, [])

    def test_ownership_proof_requires_zero_legacy_delegation(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for platform_id in glx_runtime_sweep.GLX_BLOCKING_RELEASE_PLATFORMS:
                self.write_manifest(
                    root,
                    platform_id,
                    "glx-ownership",
                    ownership_proof_manifest(
                        platform_id,
                        calls=1 if platform_id == "windows-x64" else 0,
                        items=4 if platform_id == "windows-x64" else 0,
                    ),
                )

            proof = glx_promotion.check_ownership_proof(root)
            failures = "\n".join(str(failure) for failure in proof["blockers"])

            self.assertEqual(proof["status"], "blocked")
            self.assertIn("windows-x64", failures)
            self.assertNotIn("linux-x86_64 did not report zero", failures)

    def test_ownership_proof_requires_versioned_evidence_object(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for platform_id in glx_runtime_sweep.GLX_BLOCKING_RELEASE_PLATFORMS:
                manifest = ownership_proof_manifest(platform_id)
                if platform_id == "windows-x64":
                    manifest.pop("ownershipProofEvidence")
                self.write_manifest(
                    root,
                    platform_id,
                    "glx-ownership",
                    manifest,
                )

            proof = glx_promotion.check_ownership_proof(root)
            failures = "\n".join(str(failure) for failure in proof["blockers"])

            self.assertEqual(proof["status"], "blocked")
            self.assertIn("windows-x64", failures)
            self.assertIn("versioned ownership proof evidence", failures)
            self.assertNotIn("linux-x86_64 did not include versioned", failures)

    def test_ownership_proof_rejects_stale_evidence_version(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for platform_id in glx_runtime_sweep.GLX_BLOCKING_RELEASE_PLATFORMS:
                manifest = ownership_proof_manifest(platform_id)
                if platform_id == "windows-x64":
                    manifest["ownershipProofEvidence"]["version"] = 0
                self.write_manifest(
                    root,
                    platform_id,
                    "glx-ownership",
                    manifest,
                )

            proof = glx_promotion.check_ownership_proof(root)
            failures = "\n".join(str(failure) for failure in proof["blockers"])

            self.assertEqual(proof["status"], "blocked")
            self.assertIn("unsupported ownership proof evidence version", failures)

    def test_ownership_proof_requires_modern_post_output_ownership(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for platform_id in glx_runtime_sweep.GLX_BLOCKING_RELEASE_PLATFORMS:
                self.write_manifest(
                    root,
                    platform_id,
                    "glx-ownership",
                    ownership_proof_manifest(
                        platform_id,
                        post_mode="legacy-fallback" if platform_id == "windows-x64" else "glx-owned",
                        post_nodes=0 if platform_id == "windows-x64" else 2,
                        outputs=0 if platform_id == "windows-x64" else 1,
                        legacy_fallback=1 if platform_id == "windows-x64" else 0,
                    ),
                )

            proof = glx_promotion.check_ownership_proof(root)
            failures = "\n".join(str(failure) for failure in proof["blockers"])

            self.assertEqual(proof["status"], "blocked")
            self.assertIn("windows-x64", failures)
            self.assertIn("did not prove executable GLx-owned modern post/output", failures)

    def test_ownership_proof_requires_modern_post_output_tier(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for platform_id in glx_runtime_sweep.GLX_BLOCKING_RELEASE_PLATFORMS:
                self.write_manifest(
                    root,
                    platform_id,
                    "glx-ownership",
                    ownership_proof_manifest(
                        platform_id,
                        tier="GL2X" if platform_id == "windows-x64" else "GL3X",
                    ),
                )

            proof = glx_promotion.check_ownership_proof(root)
            failures = "\n".join(str(failure) for failure in proof["blockers"])

            self.assertEqual(proof["status"], "blocked")
            self.assertIn("windows-x64", failures)
            self.assertIn("did not prove a GL3X+ modern post/output tier", failures)
            self.assertNotIn("linux-x86_64 did not prove a GL3X+", failures)

    def test_ownership_proof_requires_modern_tier_diagnostics(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for platform_id in glx_runtime_sweep.GLX_BLOCKING_RELEASE_PLATFORMS:
                self.write_manifest(
                    root,
                    platform_id,
                    "glx-ownership",
                    ownership_proof_manifest(
                        platform_id,
                        modern_tier_diagnostics=platform_id != "windows-x64",
                    ),
                )

            proof = glx_promotion.check_ownership_proof(root)
            failures = "\n".join(str(failure) for failure in proof["blockers"])

            self.assertEqual(proof["status"], "blocked")
            self.assertIn("windows-x64", failures)
            self.assertIn("did not prove modern post-chain and scene-linear tier diagnostics", failures)
            self.assertNotIn("linux-x86_64 did not prove modern post-chain", failures)


class GlxRuntimeSweepPerformanceTests(unittest.TestCase):
    def test_rc_profiles_promote_state_only_dynamic_submission(self) -> None:
        for profile_name in ("glx-parity", "glx-stress"):
            with self.subTest(profile=profile_name):
                profile = glx_runtime_sweep.PROFILE_CVARS[profile_name]

                self.assertEqual(profile["r_glxStreamDrawShadows"], "1")
                self.assertEqual(profile["r_glxStreamDrawBeams"], "1")
                self.assertEqual(profile["r_glxStreamDrawPostProcess"], "1")
                self.assertEqual(profile["r_glxStreamDrawDynamicLights"], "auto")
                self.assertEqual(profile["r_glxDlightScissor"], "auto")
                self.assertEqual(profile["r_glxDlightProjectedProgram"], "0")
                self.assertEqual(profile["r_glxDlightProjectedMdi"], "0")
                self.assertEqual(profile["r_glxStreamDrawScreenMaps"], "0")
                self.assertEqual(profile["r_glxStreamDrawVideoMaps"], "0")

    def test_glx_performance_samples_parse_compact_frame_counters(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "switch.log"
            log.write_text(
                "\n".join(
                    [
                        "glx: tier GL2X, batches 10, draws 20/300 idx, stream map-range/ready 1.25MB/2wraps/0rejects shadow 3, frames 4, backend queries 5, gpu 0.27 ms, static 6 batches/7 packets/8 surfaces/9 verts/10 indexes 2.50 MB, arena ready 3.75 MB",
                        f"glx: pass schedule valid {glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE_COUNT}/{glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE_HASH} {glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE}",
                        "pass schedule: invalid 0/00000000 none",
                        "glx: render IR dynamic roles generic 7/70/0, dlight 5/50/0, shadow 2/20/0, beam 3/0/30, post 4/0/40",
                        "glx: render IR dynamic passes dlight 5/50/0, scene 12/90/30, post 4/0/40, other 0/0/0",
                        "glx: post/output ownership mode glx-owned, post nodes 4, outputs 2, legacy fallback no, executable nodes 4, executable outputs 2, post hash 0x01020304, output hash 0x05060708, plan hash 0x090a0b0c, fallback 0x00000000",
                        "glx: post shader plan valid yes, features 0x00000d5e, hash 0x0badcafe, textures 2, uniforms 13, frames 4, invalid 0",
                        "glx: post shader cache ready yes, programs 3/32, plans 4 valid/0 invalid, cache 2 hits/3 misses, compile 3 attempts/0 failures, link failures 0, source failures 0, source hash 0x12345678, program 99",
                        "glx: post shader direct-final execute yes, eligible yes, bound yes, reject 0x00000000, candidates 4, eligible frames 4, attempts 4, binds 4, fallbacks 0, rejects 0, program misses 0, uniform failures 0",
                        "glx: material renderer on/ready programs 25, binds 12/13 attempts, switches 4, cache 5/6, failures 0 compile/0 link/0 precache/0 bind, labels 8",
                        "glx: material parameters blocks 12 invalid 0 hash 0x1234abcd, last sort 0 passes 1 features 0x0 flags 0x0 state 0x0",
                        "glx: postprocess fbo ready 640x480 capture 640x480 bloom 2, frames 3 final 2 prefinal 1 gamma 0/3, copies 4, msaa 5, ssaa 6, last bloom-final",
                        "glx: pass counters blits 7, binds 11, clears 2, fullscreen 8, pass queries 9, unavailable 1, ring skips 0",
                        "glx: pass gpu: backend=0.270ms/1 postprocess=0.420ms/1 bloom=0.250ms/1 bloom-extract=0.030ms/1 bloom-downscale=0.010ms/3 bloom-blur=0.090ms/2 bloom-blend=0.080ms/1 bloom-final=0.060ms/1 bloom-lens-reflection=n/a/0 gamma-direct=n/a/0 gamma-blit=0.110ms/1 fbo-blit=0.040ms/2 copy-screen=n/a/0 flare=0.020ms/1",
                        "glx: bloom storage policy auto format 0x8c3a (0x1907:0x140b)",
                        "target: render 640x480, capture 640x480, window 640x480, format 0x881a (0x1908:0x140b)",
                        "controls: scene-linear HDR yes, precision 16, renderScale 1, bloom 2, MSAA no, supersample no, adjusted window yes, greyscale 1.00",
                        "frames: 3 post, 1 bloom-final, 0 gamma-direct, 3 gamma-blit, 0 minimized output, 1 screenshots",
                        "frame features: 1 bloom-available, 1 scene-linear, 1 tone-mapped, 1 graded, 1 render-scale, 1 greyscale, 1 window-adjusted, 0 minimized",
                        "glx: color pipeline scene-linear precision 16 transfer hdr10-pq tone-map aces-fitted exposure 1.00 bloom-threshold 0.75/2 knee 0.50 grade lgg-lut3d paper-white 203 max 812",
                        "glx: auto exposure mode 3 algorithm histogram-percentile enabled yes fallback no samples 1024/32x32 percentile 80.0 target-luma 0.180 measured-log2 -1.000 measured-luma 0.5000 manual 1.00 scale 0.360 target 0.36 frames 3 histogram 3 simple 0 sample-failures 0",
                        "glx: color grade mode lgg-lut3d lift 0.01/0.02/0.03 gamma 1.10/1.00/0.95 gain 1.05/1.00/0.98 white-point 6504->6000 lut-size 16 lut-scale 4.00",
                        "glx: output colorimetry primaries bt2020 gamut-map compress precision-request 0 precision-resolved 16",
                        "glx: color audit srgb-decode yes requested yes available yes framebuffer-srgb no requested yes available yes capture sdr-srgb capture-request sdr-srgb capture-hdr-aware no capture-supported yes target-float yes final-encode shader-srgb contract yes",
                        "glx: texture audit srgb 4 decode 4, linear 2 decode 0, data 2 decode 0, unknown 0 decode 0, missing-srgb-decode 0, unexpected-decode 0",
                        'glx: color-frame-json {"frame":1,"backend":"hdr10-pq","space":"scene-linear","transfer":"hdr10-pq","exposure":1.0000,"paperWhiteNits":203.0,"maxOutputNits":812.0,"srgbDecode":true,"framebufferSrgb":false,"internalFormat":"0x881a","textureFormat":"0x1908","textureType":"0x140b","sceneTargetFloat":true,"shaderSrgbEncode":true,"contractValid":true}',
                        "glx: output backend request hdr10-pq selected hdr10-pq native windows-scrgb hardware yes experimental no display-hdr yes headroom 4.00 sdr-white 203 display-max 812 icc yes/2048",
                        "glx: stream draws 7/8 attempts, 90 idx, 0.50MB/index 0.10MB/tex1 0.20MB, mt 1, fog 2, depthfrag 3, texmod 4, env 5, dlight 5, screen 0, video 0, shadow 2, beam 3, post 4, fallbacks 0, skips 1",
                        "glx: stream dlight attempts 6 draws 5 fallbacks 1 bytes 0.30MB/0.25MB index 0.05MB wraps 2 rejects 0 waits 3 timeouts 0 syncfail 0 program binds 4/5 creates 2 cache 6",
                        "glx: dlight state legacy 2 texture 3 fog 4 shadowtex 5/1 fbo 6/7 changes 8",
                        "glx: dlight build legacy 9 skipped 10 nohit 2 verts 300 idx 600/450 pm 3 pmverts 240 pmidx 360",
                        "glx: dlight cull legacy verts 40 idx 90",
                        "glx: dlight scissor active yes candidates 4 computed 3 applied 2 fallbacks 1 pixels 1200/4800",
                        "glx: dlight projected shader uniforms attempts 2 binds 1 failures 1 records 3 truncated 0 executable 1 suppressed 0 world 1/1 dynamic 0/0 limit-suppressed 0 limit 3",
                        "glx: dlight projected shader resource attempts 0 binds 0 executable 0 suppressed 0 promotions 0 failures 0 records 0 world 0 dynamic 0 binding 5",
                        "glx: dlight projected shader stream attempts 2 uploads 1 failures 0 skipped 1 records 3 bytes 96 persistent 1 world 1 dynamic 0 range 1/1 rangefail 0 clears 1 last 1024/96",
                        "glx: dlight projected shader arena reserves 2 uploads 1 failures 0 wraps 1 rejects 0 waits 2 timeouts 0 syncfail 0 bytes 96 light-records 3 list-records 3 world-records 3 dynamic-records 0 range 1/1 rangefail 0 clears 1 authoritative 1/1 authfail 0 fallbacks 0 auth-clears 1 bound yes cursor 96 last 11/1024/96",
                        "glx: dlight projected shader MDI attempts 2 eligible 1 uploads 1 failures 0 skipped 1 records 3 idx 6 bytes 20 last 2048/20",
                        "glx: dlight projected shader MDI command ring reserves 2 commits 1 wraps 0 failures 0 slots 256 cursor 2 last 1/11/2048/20",
                        "glx: dlight projected shader MDI submit attempts 1 plans 1 ready 0 fallbacks 1 skipped 0 records 3 idx 6 buffer 11 last 2048/20",
                        "glx: dlight projected shader MDI batch attempts 1 batches 1 ready 0 fallbacks 1 rejects 0 glerr 0 records 3 idx 6 submitted 0/0 largest 1 reject 0 buffer 11 range 2048/20",
                        "glx: stream categories entity 2/2, particle 1/1, poly 1/1, mark 1/1, weapon 1/1, ui 1/1, beam 3/3, dlight 5/6, special 4/4",
                        "glx: stream roles generic 7/8/0, dlight 5/6/1, shadow 2/2/0, beam 3/3/0, post 4/4/0",
                        "glx: stream reservation last 256 bytes at 1024 using map-range, largest 4096 bytes, same-frame wrap rejects 0",
                        "glx: stream binding cache queries 1 hits 6 restores 8 invalidations 2 external 4 array-known yes array-buffer 0 element-known yes element-buffer 0",
                        "glx: static queue packets last 1 full/2 partial/3 miss/4 mismatch, total 5 full/6 partial/7 miss",
                        "glx: static packet lookup 64 mapped/max 63, hits 30, misses 9, fallbacks 2, mismatches 1, overflows 0",
                        "glx: static draw 11/12 calls, 130 idx, packets 1 full/2 partial/3 miss, manifest 4/5 idx, soft 6/7 calls/8 idx, arena 9, legacy 10, fallbacks 0, policy skips 1",
                        "glx: static MDI 1/2 calls, 3 runs/4 idx, fallbacks 0, skips 5, errors 0, largest 6",
                        "glx: GL3X performance draws 14 sync-uploads 3 static-buffers 2 dynamic-buffers 5 materials 7 fbo-post 4 unsupported persistent-upload 0",
                        "glx: GL41 mac-modern draws 15 sync-uploads 4 static-buffers 3 dynamic-buffers 6 materials 8 post 5 unsupported persistent-upload 0 gl43-required 0 gl44-required 0 gl45-required 0",
                        "glx: GL46 high-end draws 16 persistent-uploads 2 sync-uploads 5 dsa-products 6 mdi-products 7 aggressive-static 8 materials 9 post 10 gpu-counters 11 static-mdi 12/13 calls/140 idx",
                    ]
                ),
                encoding="utf-8",
            )

            performance = glx_runtime_sweep.analyze_glx_performance(log)
            self.assertTrue(performance["found"])
            self.assertEqual(performance["sampleCount"], 1)
            self.assertEqual(performance["latest"]["tier"], "GL2X")
            self.assertEqual(performance["latest"]["productTier"], "GL2X")
            self.assertEqual(performance["latest"]["drawIndexes"], 300)
            self.assertEqual(performance["latest"]["gpuFrameMs"], 0.27)
            self.assertEqual(performance["latest"]["gpuPassBlits"], 7)
            self.assertEqual(performance["latest"]["gpuPassBinds"], 11)
            self.assertEqual(performance["latest"]["gpuPassClears"], 2)
            self.assertEqual(performance["latest"]["gpuPassFullscreen"], 8)
            self.assertEqual(performance["latest"]["gpuPassQueries"], 9)
            self.assertEqual(performance["latest"]["gpuPassUnavailable"], 1)
            self.assertEqual(performance["latest"]["gpuPassBackendMs"], 0.27)
            self.assertEqual(performance["latest"]["gpuPassPostprocessSamples"], 1)
            self.assertEqual(performance["latest"]["gpuPassBloomDownscaleSamples"], 3)
            self.assertEqual(performance["latest"]["gpuPassFlareMs"], 0.02)
            self.assertEqual(performance["latest"]["passScheduleValid"], 1)
            self.assertEqual(
                performance["latest"]["passScheduleCount"],
                glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE_COUNT,
            )
            self.assertEqual(performance["latest"]["passScheduleHash"], glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE_HASH)
            self.assertEqual(performance["latest"]["passScheduleOrder"], glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE)
            self.assertEqual(performance["latest"]["renderIRRoleGenericDraws"], 7)
            self.assertEqual(performance["latest"]["renderIRRoleDlightDraws"], 5)
            self.assertEqual(performance["latest"]["renderIRRoleBeamVertices"], 30)
            self.assertEqual(performance["latest"]["renderIRRolePostVertices"], 40)
            self.assertEqual(performance["latest"]["renderIRPassDlightDraws"], 5)
            self.assertEqual(performance["latest"]["renderIRPassSceneIndexes"], 90)
            self.assertEqual(performance["latest"]["renderIRPassPostVertices"], 40)
            self.assertEqual(performance["latest"]["postOutputMode"], "glx-owned")
            self.assertEqual(performance["latest"]["postOutputPostNodes"], 4)
            self.assertEqual(performance["latest"]["postOutputOutputs"], 2)
            self.assertEqual(performance["latest"]["postOutputExecutableNodes"], 4)
            self.assertEqual(performance["latest"]["postOutputExecutableOutputs"], 2)
            self.assertEqual(performance["latest"]["postOutputPostHash"], 0x01020304)
            self.assertEqual(performance["latest"]["postOutputOutputHash"], 0x05060708)
            self.assertEqual(performance["latest"]["postOutputPlanHash"], 0x090A0B0C)
            self.assertEqual(performance["latest"]["postOutputFallbackMask"], 0)
            self.assertEqual(performance["latest"]["postShaderPlanValid"], 1)
            self.assertEqual(performance["latest"]["postShaderFeatures"], 0x00000D5E)
            self.assertEqual(performance["latest"]["postShaderHash"], 0x0BADCAFE)
            self.assertEqual(performance["latest"]["postShaderTextures"], 2)
            self.assertEqual(performance["latest"]["postShaderUniforms"], 13)
            self.assertEqual(performance["latest"]["postShaderFrames"], 4)
            self.assertEqual(performance["latest"]["postShaderInvalidFrames"], 0)
            self.assertEqual(performance["latest"]["postShaderCacheReady"], 1)
            self.assertEqual(performance["latest"]["postShaderPrograms"], 3)
            self.assertEqual(performance["latest"]["postShaderProgramLimit"], 32)
            self.assertEqual(performance["latest"]["postShaderValidPlans"], 4)
            self.assertEqual(performance["latest"]["postShaderInvalidPlans"], 0)
            self.assertEqual(performance["latest"]["postShaderCacheHits"], 2)
            self.assertEqual(performance["latest"]["postShaderCacheMisses"], 3)
            self.assertEqual(performance["latest"]["postShaderCompileAttempts"], 3)
            self.assertEqual(performance["latest"]["postShaderCompileFailures"], 0)
            self.assertEqual(performance["latest"]["postShaderLinkFailures"], 0)
            self.assertEqual(performance["latest"]["postShaderSourceFailures"], 0)
            self.assertEqual(performance["latest"]["postShaderSourceHash"], 0x12345678)
            self.assertEqual(performance["latest"]["postShaderProgram"], 99)
            self.assertEqual(performance["latest"]["postShaderDirectFinalExecute"], 1)
            self.assertEqual(performance["latest"]["postShaderDirectFinalEligible"], 1)
            self.assertEqual(performance["latest"]["postShaderDirectFinalBound"], 1)
            self.assertEqual(performance["latest"]["postShaderDirectFinalReject"], 0)
            self.assertEqual(performance["latest"]["postShaderDirectFinalCandidates"], 4)
            self.assertEqual(performance["latest"]["postShaderDirectFinalEligibleFrames"], 4)
            self.assertEqual(performance["latest"]["postShaderDirectFinalAttempts"], 4)
            self.assertEqual(performance["latest"]["postShaderDirectFinalBinds"], 4)
            self.assertEqual(performance["latest"]["postShaderDirectFinalFallbacks"], 0)
            self.assertEqual(performance["latest"]["postShaderDirectFinalRejects"], 0)
            self.assertEqual(performance["latest"]["materialPrograms"], 25)
            self.assertEqual(performance["latest"]["materialParameterBlocks"], 12)
            self.assertEqual(performance["latest"]["materialParameterHash"], 0x1234ABCD)
            self.assertEqual(performance["latest"]["materialInvalidParameterBlocks"], 0)
            self.assertEqual(performance["latest"]["colorSpace"], "scene-linear")
            self.assertEqual(performance["latest"]["hdrPrecision"], 16)
            self.assertEqual(performance["latest"]["outputTransfer"], "hdr10-pq")
            self.assertEqual(performance["latest"]["toneMap"], "aces-fitted")
            self.assertEqual(performance["latest"]["toneMapExposure"], 1.0)
            self.assertEqual(performance["latest"]["bloomThreshold"], 0.75)
            self.assertEqual(performance["latest"]["bloomThresholdMode"], 2)
            self.assertEqual(performance["latest"]["bloomSoftKnee"], 0.5)
            self.assertEqual(performance["latest"]["autoExposureMode"], 3)
            self.assertEqual(performance["latest"]["autoExposureAlgorithm"], "histogram-percentile")
            self.assertEqual(performance["latest"]["autoExposureEnabled"], 1)
            self.assertEqual(performance["latest"]["autoExposureFallback"], 0)
            self.assertEqual(performance["latest"]["autoExposureSampleCount"], 1024)
            self.assertEqual(performance["latest"]["autoExposureSampleWidth"], 32)
            self.assertEqual(performance["latest"]["autoExposureSampleHeight"], 32)
            self.assertEqual(performance["latest"]["autoExposureTargetLuma"], 0.18)
            self.assertEqual(performance["latest"]["autoExposureMeasuredLuma"], 0.5)
            self.assertEqual(performance["latest"]["autoExposureSampleFailures"], 0)
            self.assertEqual(performance["latest"]["colorGrade"], "lgg-lut3d")
            self.assertEqual(performance["latest"]["colorGradeMode"], "lgg-lut3d")
            self.assertEqual(performance["latest"]["colorGradeLiftR"], 0.01)
            self.assertEqual(performance["latest"]["colorGradeGammaR"], 1.10)
            self.assertEqual(performance["latest"]["colorGradeGainR"], 1.05)
            self.assertEqual(performance["latest"]["colorGradeWhiteTarget"], 6000.0)
            self.assertEqual(performance["latest"]["colorGradeLutSize"], 16.0)
            self.assertEqual(performance["latest"]["colorGradeLutScale"], 4.0)
            self.assertEqual(performance["latest"]["outputPrimaries"], "bt2020")
            self.assertEqual(performance["latest"]["gamutMap"], "compress")
            self.assertEqual(performance["latest"]["hdrPrecisionRequested"], 0)
            self.assertEqual(performance["latest"]["hdrPrecisionResolved"], 16)
            self.assertEqual(performance["latest"]["paperWhiteNits"], 203.0)
            self.assertEqual(performance["latest"]["maxOutputNits"], 812.0)
            self.assertEqual(performance["latest"]["colorSrgbDecode"], 1)
            self.assertEqual(performance["latest"]["colorSrgbAvailable"], 1)
            self.assertEqual(performance["latest"]["colorFramebufferSrgb"], 0)
            self.assertEqual(performance["latest"]["colorSceneTargetFloat"], 1)
            self.assertEqual(performance["latest"]["colorFinalEncode"], "shader-srgb")
            self.assertEqual(performance["latest"]["colorOutputContract"], 1)
            self.assertEqual(performance["latest"]["captureColorSpace"], "sdr-srgb")
            self.assertEqual(performance["latest"]["capturePolicyRequest"], "sdr-srgb")
            self.assertEqual(performance["latest"]["captureHdrAware"], 0)
            self.assertEqual(performance["latest"]["capturePolicySupported"], 1)
            self.assertEqual(performance["latest"]["textureAuditSrgb"], 4)
            self.assertEqual(performance["latest"]["textureAuditSrgbDecode"], 4)
            self.assertEqual(performance["latest"]["textureAuditMissingSrgbDecode"], 0)
            self.assertEqual(performance["latest"]["textureAuditUnexpectedDecode"], 0)
            self.assertEqual(performance["latest"]["targetInternalFormat"], 0x881A)
            self.assertEqual(performance["latest"]["targetTextureFormat"], 0x1908)
            self.assertEqual(performance["latest"]["targetTextureType"], 0x140B)
            self.assertEqual(performance["latest"]["postprocessRenderScaleMode"], 1)
            self.assertEqual(performance["latest"]["postprocessGreyscale"], 1.0)
            self.assertEqual(performance["latest"]["postprocessWindowAdjusted"], 1)
            self.assertEqual(performance["latest"]["postprocessFrames"], 3)
            self.assertEqual(performance["latest"]["postprocessScreenshots"], 1)
            self.assertEqual(performance["latest"]["postprocessMinimizedOutput"], 0)
            self.assertEqual(performance["latest"]["postprocessFeatureRenderScale"], 1)
            self.assertEqual(performance["latest"]["postprocessFeatureGreyscale"], 1)
            self.assertEqual(performance["latest"]["postprocessFeatureWindowAdjusted"], 1)
            self.assertEqual(performance["latest"]["postprocessFeatureMinimized"], 0)
            self.assertEqual(performance["latest"]["gpuPassBlits"], 7)
            self.assertEqual(performance["latest"]["gpuPassBinds"], 11)
            self.assertEqual(performance["latest"]["gpuPassClears"], 2)
            self.assertEqual(performance["latest"]["gpuPassFullscreen"], 8)
            self.assertEqual(performance["latest"]["gpuPassQueries"], 9)
            self.assertEqual(performance["latest"]["gpuPassUnavailable"], 1)
            self.assertEqual(performance["latest"]["gpuPassPostprocessMs"], 0.42)
            self.assertEqual(performance["latest"]["gpuPassBloomBlurSamples"], 2)
            self.assertEqual(performance["latest"]["bloomStoragePolicy"], "auto")
            self.assertEqual(performance["latest"]["bloomStorageInternalFormat"], 0x8C3A)
            self.assertEqual(performance["latest"]["bloomStorageTextureFormat"], 0x1907)
            self.assertEqual(performance["latest"]["bloomStorageTextureType"], 0x140B)
            self.assertEqual(performance["latest"]["colorFrameSamples"], 1)
            self.assertEqual(performance["latest"]["colorFrameBackend"], "hdr10-pq")
            self.assertEqual(performance["latest"]["colorFrameTransfer"], "hdr10-pq")
            self.assertEqual(performance["latest"]["colorFrameInternalFormat"], "0x881a")
            self.assertEqual(performance["latest"]["colorFrameContractValid"], 1)
            self.assertEqual(performance["latest"]["outputBackendRequest"], "hdr10-pq")
            self.assertEqual(performance["latest"]["outputBackendSelected"], "hdr10-pq")
            self.assertEqual(performance["latest"]["outputBackendNative"], "windows-scrgb")
            self.assertEqual(performance["latest"]["outputBackendHardware"], 1)
            self.assertEqual(performance["latest"]["streamBindingQueries"], 1)
            self.assertEqual(performance["latest"]["streamBindingCacheHits"], 6)
            self.assertEqual(performance["latest"]["streamBindingExternalUpdates"], 4)
            self.assertEqual(performance["latest"]["outputHeadroom"], 4.0)
            self.assertEqual(performance["latest"]["outputIccProfileBytes"], 2048)
            self.assertEqual(performance["latest"]["streamDrawAttempts"], 8)
            self.assertEqual(performance["latest"]["streamDrawMegabytes"], 0.5)
            self.assertEqual(performance["latest"]["streamSameFrameWrapRejects"], 0)
            self.assertEqual(performance["latest"]["streamDrawMultitexture"], 1)
            self.assertEqual(performance["latest"]["streamDrawFog"], 2)
            self.assertEqual(performance["latest"]["streamDrawDepthFragment"], 3)
            self.assertEqual(performance["latest"]["streamDrawTexMods"], 4)
            self.assertEqual(performance["latest"]["streamDrawEnvironment"], 5)
            self.assertEqual(performance["latest"]["streamDrawDynamicLights"], 5)
            self.assertEqual(performance["latest"]["streamDrawScreenMaps"], 0)
            self.assertEqual(performance["latest"]["streamDrawVideoMaps"], 0)
            self.assertEqual(performance["latest"]["streamDrawShadows"], 2)
            self.assertEqual(performance["latest"]["streamDrawBeams"], 3)
            self.assertEqual(performance["latest"]["streamDrawPostProcess"], 4)
            self.assertEqual(performance["latest"]["streamDlightAttempts"], 6)
            self.assertEqual(performance["latest"]["streamDlightDraws"], 5)
            self.assertEqual(performance["latest"]["streamDlightFallbacks"], 1)
            self.assertEqual(performance["latest"]["streamDlightAttemptMegabytes"], 0.30)
            self.assertEqual(performance["latest"]["streamDlightMegabytes"], 0.25)
            self.assertEqual(performance["latest"]["streamDlightIndexMegabytes"], 0.05)
            self.assertEqual(performance["latest"]["streamDlightWraps"], 2)
            self.assertEqual(performance["latest"]["streamDlightSameFrameWrapRejects"], 0)
            self.assertEqual(performance["latest"]["streamDlightSyncWaits"], 3)
            self.assertEqual(performance["latest"]["streamDlightSyncFailures"], 0)
            self.assertEqual(performance["latest"]["streamDlightProgramBinds"], 4)
            self.assertEqual(performance["latest"]["streamDlightProgramBindAttempts"], 5)
            self.assertEqual(performance["latest"]["streamDlightProgramCreates"], 2)
            self.assertEqual(performance["latest"]["streamDlightProgramCacheHits"], 6)
            self.assertEqual(performance["latest"]["dlightStateLegacyPasses"], 2)
            self.assertEqual(performance["latest"]["dlightStateTextureBinds"], 3)
            self.assertEqual(performance["latest"]["dlightStateFogTextureBinds"], 4)
            self.assertEqual(performance["latest"]["dlightStateShadowTextureBinds"], 5)
            self.assertEqual(performance["latest"]["dlightStateShadowTextureFallbackBinds"], 1)
            self.assertEqual(performance["latest"]["dlightStateShadowFboBinds"], 6)
            self.assertEqual(performance["latest"]["dlightStateShadowFboRestores"], 7)
            self.assertEqual(performance["latest"]["dlightStateChanges"], 8)
            self.assertEqual(performance["latest"]["dlightBuildLegacyLights"], 9)
            self.assertEqual(performance["latest"]["dlightBuildLegacySkippedLights"], 10)
            self.assertEqual(performance["latest"]["dlightBuildLegacyNoHitLights"], 2)
            self.assertEqual(performance["latest"]["dlightBuildLegacyVertexes"], 300)
            self.assertEqual(performance["latest"]["dlightBuildLegacyIndexes"], 600)
            self.assertEqual(performance["latest"]["dlightBuildLegacyLitIndexes"], 450)
            self.assertEqual(performance["latest"]["dlightBuildPmPasses"], 3)
            self.assertEqual(performance["latest"]["dlightBuildPmVertexes"], 240)
            self.assertEqual(performance["latest"]["dlightBuildPmIndexes"], 360)
            self.assertEqual(performance["latest"]["dlightCullLegacyVertexes"], 40)
            self.assertEqual(performance["latest"]["dlightCullLegacyIndexes"], 90)
            self.assertEqual(performance["latest"]["dlightScissorActive"], 1)
            self.assertEqual(performance["latest"]["dlightScissorCandidates"], 4)
            self.assertEqual(performance["latest"]["dlightScissorComputed"], 3)
            self.assertEqual(performance["latest"]["dlightScissorApplied"], 2)
            self.assertEqual(performance["latest"]["dlightScissorFallbacks"], 1)
            self.assertEqual(performance["latest"]["dlightScissorPixels"], 1200)
            self.assertEqual(performance["latest"]["dlightScissorViewportPixels"], 4800)
            self.assertEqual(performance["latest"]["dlightProjectedShaderUniformAttempts"], 2)
            self.assertEqual(performance["latest"]["dlightProjectedShaderUniformBinds"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderUniformFailures"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderUniformRecords"], 3)
            self.assertEqual(performance["latest"]["dlightProjectedShaderUniformTruncated"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderUniformExecutable"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderUniformSuppressed"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderUniformWorldExecutable"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderUniformWorldBinds"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderUniformDynamicExecutable"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderUniformDynamicBinds"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderUniformLimitSuppressed"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderUniformLimit"], 3)
            self.assertEqual(performance["latest"]["dlightProjectedShaderResourceAttempts"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderResourceBinds"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderResourceExecutable"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderResourceSuppressed"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderResourcePromotions"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderResourceFailures"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderResourceRecords"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderResourceWorldExecutable"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderResourceDynamicExecutable"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderResourceBinding"], 5)
            self.assertEqual(performance["latest"]["dlightProjectedShaderStreamAttempts"], 2)
            self.assertEqual(performance["latest"]["dlightProjectedShaderStreamUploads"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderStreamFailures"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderStreamSkipped"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderStreamRecords"], 3)
            self.assertEqual(performance["latest"]["dlightProjectedShaderStreamBytes"], 96)
            self.assertEqual(performance["latest"]["dlightProjectedShaderStreamPersistent"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderStreamWorld"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderStreamDynamic"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderStreamRangeBinds"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderStreamRangeAttempts"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderStreamRangeFailures"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderStreamRangeClears"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderStreamLastOffset"], 1024)
            self.assertEqual(performance["latest"]["dlightProjectedShaderStreamLastBytes"], 96)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaReserves"], 2)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaUploads"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaFailures"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaWraps"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaRejects"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaWaits"], 2)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaTimeouts"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaSyncFailures"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaBytes"], 96)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaLightRecords"], 3)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaListRecords"], 3)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaWorldRecords"], 3)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaDynamicRecords"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaRangeBinds"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaRangeAttempts"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaRangeFailures"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaRangeClears"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaAuthoritativeBinds"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaAuthoritativeAttempts"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaAuthoritativeFailures"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaAuthoritativeFallbacks"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaAuthoritativeClears"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaBound"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaCursor"], 96)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaLastBuffer"], 11)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaLastOffset"], 1024)
            self.assertEqual(performance["latest"]["dlightProjectedShaderArenaLastBytes"], 96)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiAttempts"], 2)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiEligible"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiUploads"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiFailures"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiSkipped"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiRecords"], 3)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiIndexes"], 6)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiBytes"], 20)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiLastOffset"], 2048)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiLastBytes"], 20)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiCommandRingReserves"], 2)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiCommandRingCommits"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiCommandRingWraps"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiCommandRingFailures"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiCommandRingSlots"], 256)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiCommandRingCursor"], 2)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiCommandRingLastSlot"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiCommandRingLastBuffer"], 11)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiCommandRingLastOffset"], 2048)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiCommandRingLastBytes"], 20)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiSubmitAttempts"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiSubmitPlans"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiSubmitReady"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiSubmitFallbacks"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiSubmitSkipped"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiSubmitRecords"], 3)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiSubmitIndexes"], 6)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiSubmitBuffer"], 11)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiSubmitLastOffset"], 2048)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiSubmitLastBytes"], 20)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiBatchAttempts"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiBatchBatches"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiBatchReady"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiBatchFallbacks"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiBatchRejects"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiBatchGlErrors"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiBatchRecords"], 3)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiBatchIndexes"], 6)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiBatchSubmittedDraws"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiBatchSubmittedIndexes"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiBatchLargest"], 1)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiBatchLastReject"], 0)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiBatchBuffer"], 11)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiBatchLastOffset"], 2048)
            self.assertEqual(performance["latest"]["dlightProjectedShaderMdiBatchLastBytes"], 20)
            self.assertEqual(performance["latest"]["streamCategoryEntityDraws"], 2)
            self.assertEqual(performance["latest"]["streamCategoryParticleDraws"], 1)
            self.assertEqual(performance["latest"]["streamCategoryPolyDraws"], 1)
            self.assertEqual(performance["latest"]["streamCategoryMarkDraws"], 1)
            self.assertEqual(performance["latest"]["streamCategoryWeaponDraws"], 1)
            self.assertEqual(performance["latest"]["streamCategoryUiDraws"], 1)
            self.assertEqual(performance["latest"]["streamCategoryBeamDraws"], 3)
            self.assertEqual(performance["latest"]["streamCategoryDlightDraws"], 5)
            self.assertEqual(performance["latest"]["streamCategorySpecialDraws"], 4)
            self.assertEqual(performance["latest"]["streamRoleGenericDraws"], 7)
            self.assertEqual(performance["latest"]["streamRoleDlightDraws"], 5)
            self.assertEqual(performance["latest"]["streamRoleDlightFallbacks"], 1)
            self.assertEqual(performance["latest"]["streamRoleShadowDraws"], 2)
            self.assertEqual(performance["latest"]["streamRoleBeamDraws"], 3)
            self.assertEqual(performance["latest"]["streamRolePostDraws"], 4)
            self.assertEqual(performance["latest"]["staticDrawPacketMisses"], 3)
            self.assertEqual(performance["latest"]["staticQueuePacketMisses"], 7)
            self.assertEqual(performance["latest"]["staticPacketLookupMisses"], 9)
            self.assertEqual(performance["latest"]["staticPacketLookupFallbacks"], 2)
            self.assertEqual(performance["latest"]["staticPacketLookupOverflows"], 0)
            self.assertEqual(performance["latest"]["gl3xDraws"], 14)
            self.assertEqual(performance["latest"]["gl3xSyncUploads"], 3)
            self.assertEqual(performance["latest"]["gl3xStaticBuffers"], 2)
            self.assertEqual(performance["latest"]["gl3xDynamicBuffers"], 5)
            self.assertEqual(performance["latest"]["gl3xMaterials"], 7)
            self.assertEqual(performance["latest"]["gl3xFboPost"], 4)
            self.assertEqual(performance["latest"]["gl3xUnsupportedPersistentUploads"], 0)
            self.assertEqual(performance["latest"]["gl41Draws"], 15)
            self.assertEqual(performance["latest"]["gl41SyncUploads"], 4)
            self.assertEqual(performance["latest"]["gl41StaticBuffers"], 3)
            self.assertEqual(performance["latest"]["gl41DynamicBuffers"], 6)
            self.assertEqual(performance["latest"]["gl41Materials"], 8)
            self.assertEqual(performance["latest"]["gl41Post"], 5)
            self.assertEqual(performance["latest"]["gl41UnsupportedPersistentUploads"], 0)
            self.assertEqual(performance["latest"]["gl41Gl43Required"], 0)
            self.assertEqual(performance["latest"]["gl41Gl44Required"], 0)
            self.assertEqual(performance["latest"]["gl41Gl45Required"], 0)
            self.assertEqual(performance["latest"]["gl46Draws"], 16)
            self.assertEqual(performance["latest"]["gl46PersistentUploads"], 2)
            self.assertEqual(performance["latest"]["gl46SyncUploads"], 5)
            self.assertEqual(performance["latest"]["gl46DsaProducts"], 6)
            self.assertEqual(performance["latest"]["gl46MdiProducts"], 7)
            self.assertEqual(performance["latest"]["gl46AggressiveStatic"], 8)
            self.assertEqual(performance["latest"]["gl46Materials"], 9)
            self.assertEqual(performance["latest"]["gl46Post"], 10)
            self.assertEqual(performance["latest"]["gl46GpuCounters"], 11)
            self.assertEqual(performance["latest"]["gl46StaticMdiCalls"], 12)
            self.assertEqual(performance["latest"]["gl46StaticMdiAttempts"], 13)
            self.assertEqual(performance["latest"]["gl46StaticMdiIndexes"], 140)
            self.assertEqual(performance["max"]["staticMdiLargest"], 6)

    def test_gate_evaluation_requires_performance_samples(self) -> None:
        manifest = {
            "gate": "rc-smoke",
            "dryRun": False,
            "runs": [
                {
                    "type": "switch-screenshots",
                    "status": "passed",
                    "screenshots": [
                        {"name": "shot", "found": True, "histogram": screenshot_histogram()},
                    ],
                    "diagnostics": {
                        "found": True,
                        "failures": [],
                        "metrics": color_diagnostics_metrics(),
                    },
                },
            ],
            "renderers": ["opengl", "glx"],
            "demos": [],
        }

        failures = glx_runtime_sweep.evaluate_gate(manifest)
        self.assertTrue(any("performance samples" in failure for failure in failures))

    def test_gate_evaluation_requires_locked_pass_schedule_in_capture_logs(self) -> None:
        manifest = {
            "gate": "rc-smoke",
            "dryRun": False,
            "runs": [
                {
                    "type": "switch-screenshots",
                    "status": "passed",
                    "screenshots": [
                        {"name": "shot", "found": True, "histogram": screenshot_histogram()},
                    ],
                    "diagnostics": {
                        "found": True,
                        "failures": [],
                        "metrics": color_diagnostics_metrics(),
                    },
                    "performance": {
                        "found": True,
                        "sampleCount": 1,
                        "latest": {
                            "passScheduleValid": 1,
                            "passScheduleCount": 8,
                            "passScheduleHash": glx_runtime_sweep.GLX_EXPECTED_PASS_SCHEDULE_HASH,
                            "passScheduleOrder": "frame-setup>postprocess",
                        },
                        "max": {},
                    },
                },
            ],
            "renderers": ["opengl", "glx"],
            "demos": [],
        }

        failures = glx_runtime_sweep.evaluate_gate(manifest)
        self.assertTrue(any("pass schedule" in failure for failure in failures))

    def test_gate_evaluation_rejects_old_capability_tier_names(self) -> None:
        manifest = {
            "gate": "rc-smoke",
            "dryRun": False,
            "runs": [
                {
                    "type": "switch-screenshots",
                    "status": "passed",
                    "screenshots": [
                        {"name": "shot", "found": True, "histogram": screenshot_histogram()},
                    ],
                    "diagnostics": {
                        "found": True,
                        "failures": [],
                        "metrics": color_diagnostics_metrics(),
                    },
                    "performance": {
                        "found": True,
                        "sampleCount": 1,
                        "latest": {
                            **locked_pass_schedule_latest(),
                            "tier": "compat",
                            "productTier": "compat",
                        },
                        "max": {},
                    },
                },
            ],
            "renderers": ["opengl", "glx"],
            "demos": [],
        }

        failures = glx_runtime_sweep.evaluate_gate(manifest)
        self.assertTrue(any("final five tiers" in failure for failure in failures))

    def test_performance_budget_flags_fallback_counters(self) -> None:
        aggregate = {
            "sampleCount": 1,
            "latest": {"productTier": "GL2X"},
            "max": {
                "streamDrawFallbacks": 2,
                "streamDrawDynamicLights": 1,
                "streamDrawScreenMaps": 0,
                "streamDrawVideoMaps": 0,
                "staticDrawFallbacks": 0,
                "streamSameFrameWrapRejects": 1,
            },
        }
        budget = glx_runtime_sweep.merge_budget(
            glx_runtime_sweep.DEFAULT_PERFORMANCE_BUDGET,
            {"min": {"sampleCount": 1}},
        )

        failures = glx_runtime_sweep.evaluate_performance_budget(aggregate, budget)

        self.assertTrue(any("streamDrawFallbacks" in failure for failure in failures))
        self.assertFalse(any("streamDrawDynamicLights" in failure for failure in failures))
        self.assertTrue(any("streamSameFrameWrapRejects" in failure for failure in failures))
        self.assertFalse(any("staticDrawFallbacks" in failure for failure in failures))

    def test_performance_budget_applies_tier_draw_upload_bind_miss_and_gpu_limits(self) -> None:
        aggregate = {
            "sampleCount": 1,
            "latest": {"productTier": "GL2X"},
            "max": {
                "draws": 12001,
                "streamMegabytes": 129.0,
                "materialBinds": 12001,
                "staticDrawPacketMisses": 12001,
                "staticQueuePacketMisses": 12001,
                "staticPacketLookupMisses": 12001,
                "gpuFrameMs": 51.0,
            },
        }

        failures = glx_runtime_sweep.evaluate_performance_budget(
            aggregate,
            glx_runtime_sweep.DEFAULT_PERFORMANCE_BUDGET,
        )

        for key in (
            "GL2X max draws",
            "GL2X max streamMegabytes",
            "GL2X max materialBinds",
            "GL2X max staticDrawPacketMisses",
            "GL2X max staticQueuePacketMisses",
            "GL2X max staticPacketLookupMisses",
            "GL2X max gpuFrameMs",
        ):
            self.assertTrue(any(key in failure for failure in failures), key)

    def test_performance_budget_requires_p1_metrics_on_modern_tiers(self) -> None:
        aggregate = {
            "sampleCount": 1,
            "latest": {"productTier": "GL46"},
            "max": {
                "draws": 1,
                "streamMegabytes": 1.0,
                "materialBinds": 1,
                "staticDrawPacketMisses": 0,
            },
        }

        failures = glx_runtime_sweep.evaluate_performance_budget(
            aggregate,
            glx_runtime_sweep.DEFAULT_PERFORMANCE_BUDGET,
        )

        self.assertTrue(any("GL46 required metric gpuFrameMs is missing" in failure for failure in failures))
        self.assertTrue(any("GL46 required metric postOutputLegacyFallback is missing" in failure for failure in failures))
        self.assertTrue(any("GL46 required metric postOutputPostHash is missing" in failure for failure in failures))
        self.assertTrue(any("GL46 required metric postOutputOutputHash is missing" in failure for failure in failures))
        self.assertTrue(any("GL46 required metric postOutputPlanHash is missing" in failure for failure in failures))
        self.assertTrue(any("GL46 required metric postOutputFallbackMask is missing" in failure for failure in failures))
        self.assertTrue(any("GL46 required metric materialCacheMisses is missing" in failure for failure in failures))
        self.assertTrue(any("GL46 required metric materialParameterBlocks is missing" in failure for failure in failures))
        self.assertTrue(any("GL46 required metric materialParameterHash is missing" in failure for failure in failures))
        self.assertTrue(any("GL46 required metric streamBindingQueries is missing" in failure for failure in failures))

    def test_performance_budget_requires_positive_p1_evidence_on_modern_tiers(self) -> None:
        aggregate = {
            "sampleCount": 1,
            "latest": {
                "productTier": "GL46",
                "gpuFrameMs": 1.0,
                "postOutputPostNodes": 0,
                "postOutputOutputs": 0,
                "postOutputLegacyFallback": 0,
                "postOutputPostHash": 0,
                "postOutputOutputHash": 0,
                "postOutputPlanHash": 0,
                "postOutputFallbackMask": 0,
                "materialPrograms": 0,
                "materialBindAttempts": 0,
                "materialCacheMisses": 0,
                "materialCompileFailures": 0,
                "materialLinkFailures": 0,
                "materialPrecacheFailures": 0,
                "materialBindFailures": 0,
                "materialParameterBlocks": 0,
                "materialParameterHash": 0,
                "materialInvalidParameterBlocks": 0,
                "streamBindingQueries": 0,
                "streamBindingCacheHits": 0,
                "streamBindingRestores": 0,
            },
            "max": {},
        }

        failures = glx_runtime_sweep.evaluate_performance_budget(
            aggregate,
            glx_runtime_sweep.DEFAULT_PERFORMANCE_BUDGET,
        )

        self.assertTrue(any("GL46 min postOutputPostNodes missed" in failure for failure in failures))
        self.assertTrue(any("GL46 min postOutputPostHash missed" in failure for failure in failures))
        self.assertTrue(any("GL46 min postOutputOutputHash missed" in failure for failure in failures))
        self.assertTrue(any("GL46 min postOutputPlanHash missed" in failure for failure in failures))
        self.assertTrue(any("GL46 min materialPrograms missed" in failure for failure in failures))
        self.assertTrue(any("GL46 min materialParameterHash missed" in failure for failure in failures))
        self.assertTrue(any("GL46 min streamBindingRestores missed" in failure for failure in failures))

    def test_performance_budget_allows_legacy_post_output_fallback_on_gl2x_only(self) -> None:
        gl2x = {
            "sampleCount": 1,
            "latest": {
                "productTier": "GL2X",
                "postOutputLegacyFallback": 1,
            },
            "max": {},
        }

        self.assertEqual(
            glx_runtime_sweep.evaluate_performance_budget(
                gl2x,
                glx_runtime_sweep.DEFAULT_PERFORMANCE_BUDGET,
            ),
            [],
        )

        gl46 = {
            "sampleCount": 1,
            "latest": {
                "productTier": "GL46",
                "gpuFrameMs": 1.0,
                "postOutputPostNodes": 2,
                "postOutputOutputs": 1,
                "postOutputLegacyFallback": 1,
                "postOutputPostHash": 1,
                "postOutputOutputHash": 1,
                "postOutputPlanHash": 1,
                "postOutputFallbackMask": 1,
                "materialPrograms": 1,
                "materialBindAttempts": 1,
                "materialCacheMisses": 0,
                "materialCompileFailures": 0,
                "materialLinkFailures": 0,
                "materialPrecacheFailures": 0,
                "materialBindFailures": 0,
                "materialParameterBlocks": 1,
                "materialParameterHash": 1,
                "materialInvalidParameterBlocks": 0,
                "streamBindingQueries": 1,
                "streamBindingCacheHits": 1,
                "streamBindingRestores": 1,
            },
            "max": {},
        }

        failures = glx_runtime_sweep.evaluate_performance_budget(
            gl46,
            glx_runtime_sweep.DEFAULT_PERFORMANCE_BUDGET,
        )

        self.assertTrue(any("GL46 max postOutputLegacyFallback exceeded" in failure for failure in failures))
        self.assertTrue(any("GL46 max postOutputFallbackMask exceeded" in failure for failure in failures))

    def test_rc_stress_budget_allows_staged_screen_video_material_streams(self) -> None:
        aggregate = {
            "sampleCount": 1,
            "latest": {},
            "max": {
                "streamDrawScreenMaps": 2,
                "streamDrawVideoMaps": 2,
                "streamDrawDynamicLights": 2,
            },
        }

        rc_proof_failures = glx_runtime_sweep.evaluate_performance_budget(
            aggregate,
            glx_runtime_sweep.performance_budget_for_gate(
                "rc-proof",
                glx_runtime_sweep.DEFAULT_PERFORMANCE_BUDGET,
            ),
        )
        rc_stress_failures = glx_runtime_sweep.evaluate_performance_budget(
            aggregate,
            glx_runtime_sweep.performance_budget_for_gate(
                "rc-stress",
                glx_runtime_sweep.DEFAULT_PERFORMANCE_BUDGET,
            ),
        )

        self.assertTrue(any("streamDrawScreenMaps" in failure for failure in rc_proof_failures))
        self.assertTrue(any("streamDrawVideoMaps" in failure for failure in rc_proof_failures))
        self.assertEqual(rc_stress_failures, [])

    def test_dlight_mdi_profile_budget_requires_submitted_batch_evidence(self) -> None:
        self.assertEqual(glx_runtime_sweep.performance_budget_for_profile("glx-parity", {}), {})
        budget = glx_runtime_sweep.performance_budget_for_profile("glx-dlight-mdi", {})
        good_aggregate = {
            "sampleCount": 1,
            "latest": {
                "dlightProjectedShaderMdiAttempts": 1,
                "dlightProjectedShaderMdiEligible": 1,
                "dlightProjectedShaderMdiUploads": 1,
                "dlightProjectedShaderMdiFailures": 0,
                "dlightProjectedShaderMdiRecords": 1,
                "dlightProjectedShaderMdiIndexes": 6,
                "dlightProjectedShaderArenaReserves": 1,
                "dlightProjectedShaderArenaUploads": 1,
                "dlightProjectedShaderArenaFailures": 0,
                "dlightProjectedShaderArenaRejects": 0,
                "dlightProjectedShaderArenaTimeouts": 0,
                "dlightProjectedShaderArenaSyncFailures": 0,
                "dlightProjectedShaderArenaLightRecords": 1,
                "dlightProjectedShaderArenaListRecords": 1,
                "dlightProjectedShaderArenaRangeBinds": 1,
                "dlightProjectedShaderArenaRangeFailures": 0,
                "dlightProjectedShaderArenaAuthoritativeBinds": 1,
                "dlightProjectedShaderArenaAuthoritativeFailures": 0,
                "dlightProjectedShaderArenaAuthoritativeFallbacks": 0,
                "dlightProjectedShaderMdiCommandRingReserves": 1,
                "dlightProjectedShaderMdiCommandRingCommits": 1,
                "dlightProjectedShaderMdiCommandRingFailures": 0,
                "dlightProjectedShaderMdiCommandRingSlots": 256,
                "dlightProjectedShaderMdiSubmitPlans": 1,
                "dlightProjectedShaderMdiSubmitReady": 1,
                "dlightProjectedShaderMdiSubmitFallbacks": 0,
                "dlightProjectedShaderMdiBatchBatches": 1,
                "dlightProjectedShaderMdiBatchReady": 1,
                "dlightProjectedShaderMdiBatchFallbacks": 0,
                "dlightProjectedShaderMdiBatchRejects": 0,
                "dlightProjectedShaderMdiBatchGlErrors": 0,
                "dlightProjectedShaderMdiBatchSubmittedDraws": 1,
                "dlightProjectedShaderMdiBatchSubmittedIndexes": 6,
            },
            "max": {},
        }
        bad_aggregate = copy.deepcopy(good_aggregate)
        bad_latest = bad_aggregate["latest"]
        bad_latest.update(
            {
                "dlightProjectedShaderMdiEligible": 0,
                "dlightProjectedShaderMdiUploads": 0,
                "dlightProjectedShaderMdiFailures": 1,
                "dlightProjectedShaderArenaUploads": 0,
                "dlightProjectedShaderArenaFailures": 1,
                "dlightProjectedShaderArenaRejects": 1,
                "dlightProjectedShaderArenaTimeouts": 1,
                "dlightProjectedShaderArenaSyncFailures": 1,
                "dlightProjectedShaderArenaRangeBinds": 0,
                "dlightProjectedShaderArenaRangeFailures": 1,
                "dlightProjectedShaderArenaAuthoritativeBinds": 0,
                "dlightProjectedShaderArenaAuthoritativeFailures": 1,
                "dlightProjectedShaderArenaAuthoritativeFallbacks": 1,
                "dlightProjectedShaderMdiCommandRingCommits": 0,
                "dlightProjectedShaderMdiCommandRingFailures": 1,
                "dlightProjectedShaderMdiSubmitReady": 0,
                "dlightProjectedShaderMdiSubmitFallbacks": 1,
                "dlightProjectedShaderMdiBatchReady": 0,
                "dlightProjectedShaderMdiBatchFallbacks": 1,
                "dlightProjectedShaderMdiBatchRejects": 1,
                "dlightProjectedShaderMdiBatchGlErrors": 1,
                "dlightProjectedShaderMdiBatchSubmittedDraws": 0,
                "dlightProjectedShaderMdiBatchSubmittedIndexes": 0,
            }
        )

        self.assertEqual(glx_runtime_sweep.evaluate_performance_budget(good_aggregate, budget), [])
        failures = glx_runtime_sweep.evaluate_performance_budget(bad_aggregate, budget)

        self.assertTrue(any("min dlightProjectedShaderMdiEligible missed" in failure for failure in failures))
        self.assertTrue(any("min dlightProjectedShaderArenaUploads missed" in failure for failure in failures))
        self.assertTrue(
            any("min dlightProjectedShaderArenaRangeBinds missed" in failure for failure in failures)
        )
        self.assertTrue(
            any("min dlightProjectedShaderArenaAuthoritativeBinds missed" in failure for failure in failures)
        )
        self.assertTrue(
            any("min dlightProjectedShaderMdiCommandRingCommits missed" in failure for failure in failures)
        )
        self.assertTrue(any("min dlightProjectedShaderMdiSubmitReady missed" in failure for failure in failures))
        self.assertTrue(any("min dlightProjectedShaderMdiBatchSubmittedDraws missed" in failure for failure in failures))
        self.assertTrue(any("max dlightProjectedShaderMdiFailures exceeded" in failure for failure in failures))
        self.assertTrue(any("max dlightProjectedShaderArenaFailures exceeded" in failure for failure in failures))
        self.assertTrue(
            any("max dlightProjectedShaderArenaRangeFailures exceeded" in failure for failure in failures)
        )
        self.assertTrue(
            any("max dlightProjectedShaderArenaAuthoritativeFailures exceeded" in failure for failure in failures)
        )
        self.assertTrue(
            any("max dlightProjectedShaderArenaAuthoritativeFallbacks exceeded" in failure for failure in failures)
        )
        self.assertTrue(
            any("max dlightProjectedShaderMdiCommandRingFailures exceeded" in failure for failure in failures)
        )
        self.assertTrue(any("max dlightProjectedShaderMdiBatchGlErrors exceeded" in failure for failure in failures))

    def test_performance_budget_merges_tier_overrides(self) -> None:
        budget = glx_runtime_sweep.merge_budget(
            glx_runtime_sweep.DEFAULT_PERFORMANCE_BUDGET,
            {
                "tiers": {
                    "GL2X": {
                        "max": {
                            "draws": 5,
                        }
                    }
                }
            },
        )

        self.assertEqual(budget["tiers"]["GL2X"]["max"]["draws"], 5)  # type: ignore[index]
        self.assertEqual(budget["tiers"]["GL2X"]["max"]["materialBinds"], 12000)  # type: ignore[index]

    def test_performance_baseline_approval_then_compare(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "glx-performance.json"
            manifest = {
                "runId": "baseline-run",
                "gate": "rc-parity",
                "profile": "glx-parity",
                "maps": ["q3dm1"],
                "demos": ["demo1"],
                "proofCorpus": proof_corpus_for_gate("rc-parity"),
            }
            baseline_aggregate = {
                "sampleCount": 1,
                "latest": {"tier": "GL2X"},
                "max": {
                    "draws": 100,
                    "drawIndexes": 200,
                    "streamDrawDepthFragment": 3,
                    "streamDrawFallbacks": 0,
                },
            }
            current_aggregate = {
                "sampleCount": 1,
                "latest": {"tier": "GL2X"},
                "max": {
                    "draws": 121,
                    "drawIndexes": 200,
                    "streamDrawDepthFragment": 3,
                    "streamDrawFallbacks": 0,
                },
            }

            glx_runtime_sweep.write_performance_baseline(path, baseline_aggregate, manifest)
            baseline = glx_runtime_sweep.load_json_file(path)
            self.assertEqual(
                baseline["proofCorpus"]["version"],
                glx_runtime_sweep.GLX_PROOF_CORPUS_VERSION,
            )
            failures, comparisons = glx_runtime_sweep.compare_performance_baseline(
                current_aggregate,
                baseline,
                0.20,
            )

            self.assertTrue(any("draws" in failure for failure in failures))
            self.assertTrue(
                any(
                    comparison["metric"] == "draws" and comparison["status"] == "failed"
                    for comparison in comparisons
                )
            )
            self.assertTrue(
                any(
                    comparison["metric"] == "streamDrawDepthFragment" and comparison["status"] == "passed"
                    for comparison in comparisons
                )
            )

            failures, comparisons = glx_runtime_sweep.compare_performance_baseline(
                baseline_aggregate,
                baseline,
                0.20,
            )
            self.assertEqual(failures, [])
            self.assertTrue(all(comparison["status"] == "passed" for comparison in comparisons))

    def test_gate_evaluation_reports_performance_budget_failures(self) -> None:
        manifest = {
            "gate": "rc-smoke",
            "dryRun": False,
            "performanceFailures": [
                "Performance budget max streamDrawFallbacks exceeded: 1 > 0.",
            ],
            "runs": [
                {
                    "type": "switch-screenshots",
                    "status": "passed",
                    "screenshots": [
                        {"name": "shot", "found": True, "histogram": screenshot_histogram()},
                    ],
                    "diagnostics": {
                        "found": True,
                        "failures": [],
                        "metrics": color_diagnostics_metrics(),
                    },
                    "performance": locked_performance_sample(),
                },
            ],
            "renderers": ["opengl", "glx"],
            "demos": [],
        }

        failures = glx_runtime_sweep.evaluate_gate(manifest)
        self.assertTrue(any("performance budget failures" in failure for failure in failures))

    def test_performance_numeric_metrics_reject_bool_and_non_finite_values(self) -> None:
        aggregate = {
            "max": {
                "streamDrawFallbacks": True,
                "gpuFrameMsec": float("inf"),
            }
        }

        self.assertIsNone(glx_runtime_sweep.numeric_metric(aggregate, "streamDrawFallbacks"))
        self.assertIsNone(glx_runtime_sweep.numeric_metric(aggregate, "gpuFrameMsec"))
        self.assertEqual(glx_runtime_sweep.int_metric(True, default=7), 7)
        self.assertEqual(glx_runtime_sweep.float_metric(float("nan"), default=1.5), 1.5)


if __name__ == "__main__":
    unittest.main()
