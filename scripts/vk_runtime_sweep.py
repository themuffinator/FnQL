from __future__ import annotations

import argparse
import json
import math
import re
import struct
import subprocess
import sys
import zlib
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SWEEP_ROOT = ROOT / ".tmp" / "vk-runtime-sweeps"
CVAR_NAME_RE = re.compile(r"^[A-Za-z0-9_]+$")
Q3_COMMAND_TOKEN_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.\-/]*$")
FS_GAME_RE = re.compile(r"^[A-Za-z0-9_.-]+$")
TIMEDEMO_FPS_RE = re.compile(
    r"(?P<frames>\d+)\s+frames[, ]+\s*"
    r"(?P<seconds>\d+(?:\.\d+)?)\s+seconds:?\s*"
    r"(?P<fps>\d+(?:\.\d+)?)\s+fps",
    re.IGNORECASE,
)
PIPELINE_CACHE_RE = re.compile(
    r"pipeline cache:\s*(?P<path>.*?),\s*loaded:\s*(?P<loaded>\d+)Kb,\s*saved:\s*(?P<saved>\d+)Kb",
    re.IGNORECASE,
)
DISPLAY_HDR_RE = re.compile(
    r"display HDR:\s*(?P<state>.*?),\s*metadata:\s*(?P<metadata>\w+),\s*"
    r"paper white\s*(?P<paperWhite>\d+(?:\.\d+)?)\s*nits,\s*max\s*(?P<maxLuminance>\d+(?:\.\d+)?)\s*nits",
    re.IGNORECASE,
)
OUTPUT_BACKEND_RE = re.compile(
    r"output backend:\s*request\s+(?P<request>[A-Za-z0-9_-]+),\s*"
    r"selected\s+(?P<selected>[A-Za-z0-9_-]+),\s*"
    r"native\s+(?P<native>[A-Za-z0-9_-]+),\s*"
    r"display HDR\s+(?P<displayHdr>\w+),\s*"
    r"headroom\s+(?P<headroom>-?\d+(?:\.\d+)?),\s*"
    r"SDR white\s+(?P<sdrWhite>-?\d+(?:\.\d+)?)\s*nits,\s*"
    r"display max\s+(?P<displayMax>-?\d+(?:\.\d+)?)\s*nits,\s*"
    r"ICC\s+(?P<icc>\w+)/(?P<iccBytes>\d+)",
    re.IGNORECASE,
)
TONE_MAP_RE = re.compile(
    r"tone map:\s*(?P<mode>.*?),\s*exposure\s*(?P<exposure>\d+(?:\.\d+)?)",
    re.IGNORECASE,
)
POST_GAMMA_RE = re.compile(
    r"post gamma:\s*domain\s*(?P<domain>[A-Za-z0-9_-]+),\s*"
    r"shader\s*(?P<shader>\w+),\s*"
    r"r_gamma\s*(?P<gamma>-?\d+(?:\.\d+)?),\s*"
    r"exponent\s*(?P<exponent>-?\d+(?:\.\d+)?),\s*"
    r"overbright scale\s*(?P<overbright>-?\d+(?:\.\d+)?)",
    re.IGNORECASE,
)
BLOOM_RE = re.compile(
    r"bloom:\s*threshold\s*(?P<threshold>\d+(?:\.\d+)?),\s*"
    r"soft knee\s*(?P<softKnee>\d+(?:\.\d+)?),\s*"
    r"intensity\s*(?P<intensity>\d+(?:\.\d+)?)",
    re.IGNORECASE,
)
MODERN_VULKAN_RE = re.compile(
    r"modern Vulkan:\s*sync2\s*(?P<sync2>\w+),\s*dynamic rendering\s*(?P<dynamic>.*)",
    re.IGNORECASE,
)
BARRIERS_RE = re.compile(
    r"barriers:\s*(?P<sync2>\d+)\s*sync2\s*/\s*(?P<legacy>\d+)\s*legacy",
    re.IGNORECASE,
)
DESCRIPTORS_RE = re.compile(
    r"descriptor writes:\s*(?P<writes>\d+),\s*binds:\s*(?P<bindCalls>\d+)\s*calls\s*/\s*"
    r"(?P<bindSets>\d+)\s*sets,\s*material cache:\s*(?P<hits>\d+)\s*hits\s*/\s*(?P<misses>\d+)\s*misses",
    re.IGNORECASE,
)
COMMAND_POOLS_RE = re.compile(
    r"command pool resets:\s*(?P<frame>\d+)\s*frame\s*/\s*(?P<upload>\d+)\s*upload",
    re.IGNORECASE,
)
MEMORY_RE = re.compile(
    r"memory:\s*(?P<allocs>\d+)\s*allocs\s*\((?P<peakAllocs>\d+)\s*peak\),\s*"
    r"(?P<liveKb>\d+)Kb\s*live\s*/\s*(?P<peakKb>\d+)Kb\s*peak",
    re.IGNORECASE,
)
GPU_TIMING_RE = re.compile(
    r"\s*(?P<from>.*?)\s*->\s*(?P<to>.*?):\s*(?P<msec>\d+(?:\.\d+)?)\s*ms",
    re.IGNORECASE,
)
DLIGHT_SHADOW_PLAN_RE = re.compile(
    r"dlight shadows plan:(?P<planned>\d+)/(?P<considered>\d+)\s+"
    r"cand:(?P<candidates>\d+)\s+"
    r"atlas:(?P<atlasWidth>\d+)x(?P<atlasHeight>\d+)/(?P<faceSize>\d+)\s+"
    r"fill:(?P<fill>\d+)%\s+"
    r"(?:filter:\S+\s+taps:\d+\s+offsets:[+-]?\d+(?:\.\d+)?/[+-]?\d+(?:\.\d+)?\s+)?"
    r"render lights:(?P<renderLights>\d+)\s+"
    r"faces:(?P<faces>\d+)\s+batches:(?P<batches>\d+)\s+"
    r"draws:(?P<draws>\d+)\s+surfs:(?P<surfs>\d+)\s+"
    r"cpu:(?P<cpuMsec>\d+)ms",
    re.IGNORECASE,
)
SHADOW_MANAGER_RE = re.compile(
    r"shadow manager view:(?P<view>\d+)\s+frame:(?P<frame>\d+)\s+"
    r"noworld:(?P<noworld>\d+)\s+sched:(?P<scheduledPasses>\d+)\s+"
    r"mask:(?P<scheduledMask>[0-9a-fA-F]+)\s+"
    r"p:(?P<pointScheduled>\d+)\s+s:(?P<spotScheduled>\d+)\s+"
    r"ca:(?P<csmAtlasScheduled>\d+)\s+cr:(?P<csmReceiverScheduled>\d+)\s+"
    r"pub p:(?P<pointPublished>\d+)\s+s:(?P<spotPublished>\d+)\s+"
    r"c:(?P<csmPublished>\d+)\s+inputs dlight:(?P<inputDlights>\d+)\s+"
    r"point:(?P<pointPlanned>\d+)/(?P<pointConsidered>\d+)\s+"
    r"cand:(?P<pointCandidates>\d+)\s+"
    r"records:(?P<pointRecords>\d+)/(?P<pointCandidateRecords>\d+)\s+"
    r"atlas:(?P<pointAtlasWidth>\d+)x(?P<pointAtlasHeight>\d+)/(?P<pointAtlasFaceSize>\d+)\s+"
    r"fill:(?P<pointAtlasFill>\d+)%\s+gen:(?P<pointGeneration>\d+)\s+"
    r"spot:(?P<spotPlans>\d+)/(?P<spotCandidates>\d+)\s+"
    r"(?:src\s+static:(?P<spotStaticPlans>\d+)/(?P<spotStaticCandidates>\d+)\s+"
    r"surface:(?P<spotSurfacePlans>\d+)/(?P<spotSurfaceCandidates>\d+)\s+)?"
    r"atlas:(?P<spotAtlasWidth>\d+)x(?P<spotAtlasHeight>\d+)/(?P<spotAtlasTileSize>\d+)\s+"
    r"fill:(?P<spotAtlasFill>\d+)%\s+gen:(?P<spotGeneration>\d+)\s+"
    r"csm:(?P<csmCascadeCount>\d+)\s+"
    r"atlas:(?P<csmAtlasWidth>\d+)x(?P<csmAtlasHeight>\d+)\s+"
    r"gen:(?P<csmGeneration>\d+)",
    re.IGNORECASE,
)
SHADOW_ATLAS_CONTRACT_RE = re.compile(
    r"shadow atlas contract backend:(?P<shadowAtlasBackend>\S+)\s+"
    r"atlas:(?P<shadowAtlasName>\S+)\s+"
    r"active:(?P<shadowAtlasActive>\d+)\s+"
    r"tile:(?P<shadowAtlasTileSize>\d+)\s+"
    r"size:(?P<shadowAtlasWidth>\d+)x(?P<shadowAtlasHeight>\d+)\s+"
    r"records:(?P<shadowAtlasRecords>\d+)\s+"
    r"fill:(?P<shadowAtlasFill>\d+)%\s+"
    r"filter:(?P<shadowAtlasFilter>\S+)\s+"
    r"pad:(?P<shadowAtlasPadTexels>\d+)\s+"
    r"clamp:(?P<shadowAtlasClampTexels>\d+)\s+"
    r"sampler:(?P<shadowAtlasSampler>\S+)\s+"
    r"allocation:(?P<shadowAtlasAllocation>\S+)\s+"
    r"deterministic:(?P<shadowAtlasDeterministic>\d+)",
    re.IGNORECASE,
)
SURFACELIGHT_SPOT_PLAN_RE = re.compile(
    r"surfacelight spot plan cand:(?P<surfaceSpotCandidates>\d+)\s+"
    r"plan:(?P<surfaceSpotPlans>\d+)\s+"
    r"req:(?P<surfaceSpotRequestedTileMin>\d+)-(?P<surfaceSpotRequestedTileMax>\d+)\s+"
    r"foot:(?P<surfaceSpotFootprintMin>\d+)-(?P<surfaceSpotFootprintMax>\d+)\s+"
    r"caster:(?P<surfaceSpotCasterRadiusMin>\d+)-(?P<surfaceSpotCasterRadiusMax>\d+)\s+"
    r"planreq:(?P<surfaceSpotPlanRequestedTileMin>\d+)-(?P<surfaceSpotPlanRequestedTileMax>\d+)\s+"
    r"tile:(?P<surfaceSpotTileMin>\d+)-(?P<surfaceSpotTileMax>\d+)\s+"
    r"cone:(?P<surfaceSpotConeInnerMin>\d+)-(?P<surfaceSpotConeInnerMax>\d+)/"
    r"(?P<surfaceSpotConeOuterMin>\d+)-(?P<surfaceSpotConeOuterMax>\d+)\s+"
    r"alloc:(?P<surfaceSpotAllocated>\d+)\s+"
    r"atlas:(?P<surfaceSpotAtlasWidth>\d+)x(?P<surfaceSpotAtlasHeight>\d+)/"
    r"(?P<surfaceSpotAtlasTileSize>\d+)\s+"
    r"fill:(?P<surfaceSpotAtlasFill>\d+)%\s+"
    r"reject weak:(?P<surfaceSpotRejectedWeak>\d+)\s+"
    r"offview:(?P<surfaceSpotRejectedOffView>\d+)\s+"
    r"budget:(?P<surfaceSpotRejectedBudget>\d+)\s+"
    r"malformed:(?P<surfaceSpotRejectedMalformed>\d+)",
    re.IGNORECASE,
)
CSM_SHADOWS_RE = re.compile(
    r"csm shadows sky:(?P<csmSky>\S+)\s+"
    r"cascades:(?P<csmDebugCascades>\d+)\s+"
    r"res:(?P<csmDebugResolution>\d+)\s+"
    r"max:(?P<csmDebugMaxDistance>\d+)\s+"
    r"lambda:\S+\s+filter:\S+\s+strength:\S+\s+rbias:\S+\s+"
    r"cbias:\S+\s+light-dir:\S+\s+\S+\s+\S+\s+to-sun:\S+\s+\S+\s+\S+\s+"
    r"split far:\S+\s+\S+\s+\S+\s+\S+\s+texel:\S+\s+\S+\s+\S+\s+\S+\s+"
    r"(?:(?:depth center|snap depth):(?P<csmSnapDepth0>-?\d+(?:\.\d+)?)\s+"
    r"(?P<csmSnapDepth1>-?\d+(?:\.\d+)?)\s+"
    r"(?P<csmSnapDepth2>-?\d+(?:\.\d+)?)\s+"
    r"(?P<csmSnapDepth3>-?\d+(?:\.\d+)?)\s+)?"
    r"caster:(?P<csmCasterSurfaces>\d+)\s+"
    r"cache h/m/u:(?P<csmCacheHits>\d+)/(?P<csmCacheMisses>\d+)/(?P<csmCacheUncacheable>\d+)\s+"
    r"cpu:(?P<csmCpuMsec>\d+)ms\s+"
    r"recv world:(?P<csmReceiverWorldSurfaces>\d+)\s+ent:(?P<csmReceiverEntitySurfaces>\d+)",
    re.IGNORECASE,
)
CSM_CASCADE_RE = re.compile(
    r"csm cascade backend:(?P<csmCascadeBackend>\S+)\s+"
    r"index:(?P<csmCascadeIndex>\d+)\s+"
    r"split:(?P<csmCascadeSplitNear>-?\d+(?:\.\d+)?)\.\.(?P<csmCascadeSplitFar>-?\d+(?:\.\d+)?)\s+"
    r"atlas:(?P<csmCascadeAtlasX>-?\d+),(?P<csmCascadeAtlasY>-?\d+)/(?P<csmCascadeAtlasSize>\d+)\s+"
    r"view:(?P<csmCascadeViewX>-?\d+),(?P<csmCascadeViewY>-?\d+)\s+(?P<csmCascadeViewWidth>\d+)x(?P<csmCascadeViewHeight>\d+)\s+"
    r"api:(?P<csmCascadeApiX>-?\d+),(?P<csmCascadeApiY>-?\d+)\s+(?P<csmCascadeApiWidth>\d+)x(?P<csmCascadeApiHeight>\d+)\s+"
    r"sample-y:(?P<csmCascadeSampleY>\S+)\s+clip-y:(?P<csmCascadeClipY>\S+)\s+"
    r"depth:(?P<csmCascadeDepth>\S+)\s+ndc:(?P<csmCascadeNdc>\S+)\s+"
    r"clear:(?P<csmCascadeClear>-?\d+(?:\.\d+)?)\s+compare:(?P<csmCascadeCompare>\S+)\s+"
    r"bounds x:(?P<csmCascadeBoundMinX>-?\d+(?:\.\d+)?)\.\.(?P<csmCascadeBoundMaxX>-?\d+(?:\.\d+)?)\s+"
    r"y:(?P<csmCascadeBoundMinY>-?\d+(?:\.\d+)?)\.\.(?P<csmCascadeBoundMaxY>-?\d+(?:\.\d+)?)\s+"
    r"z:(?P<csmCascadeBoundMinZ>-?\d+(?:\.\d+)?)\.\.(?P<csmCascadeBoundMaxZ>-?\d+(?:\.\d+)?)\s+"
    r"origin:(?P<csmCascadeOriginX>-?\d+(?:\.\d+)?)\s+(?P<csmCascadeOriginY>-?\d+(?:\.\d+)?)\s+(?P<csmCascadeOriginZ>-?\d+(?:\.\d+)?)\s+"
    r"texel:(?P<csmCascadeTexel>-?\d+(?:\.\d+)?)",
    re.IGNORECASE,
)
CSM_PLAN_SKIP_RE = re.compile(
    r"csm plan cascades:(?P<csmFallbackCascades>\d+)\s+skip\s+"
    r"(?P<csmFallbackReason>[A-Za-z0-9_-]+)",
    re.IGNORECASE,
)
DLIGHT_SHADOW_SCENE_BEGIN_RE = re.compile(
    r"DLIGHT_SHADOW_SCENE_BEGIN\s+(?P<scene>[A-Za-z0-9_.-]+)",
    re.IGNORECASE,
)
DLIGHT_SHADOW_SCENE_END_RE = re.compile(
    r"DLIGHT_SHADOW_SCENE_END\s+(?P<scene>[A-Za-z0-9_.-]+)",
    re.IGNORECASE,
)
VULKAN_FAILURE_RE = re.compile(
    r"(VK_ERROR_[A-Z0-9_]+|device lost|validation error|returned VK_ERROR)",
    re.IGNORECASE,
)

DEFAULT_OPTIONS = {
    "profile": "vk-modern",
    "maps": "q3dm1",
    "demos": "",
    "width": 640,
    "height": 480,
    "startup_wait": 30,
    "map_wait": 180,
    "screenshot_wait": 8,
    "perf_sample_wait": 4,
    "timeout": 240.0,
    "dlight_shadow_scenes": False,
}

COMMON_CVARS = {
    "r_fullscreen": "0",
    "r_mode": "-1",
    "r_swapInterval": "0",
    "r_screenshotWriteViewpos": "1",
}

DLIGHT_SHADOW_SCENE_CVARS = {
    "developer": "1",
    "logfile": "2",
    "r_dynamiclight": "1",
    "r_dlightMode": "2",
    "r_dlightShadows": "1",
    "r_dlightShadowDebug": "1",
    "r_dlightShadowFilter": "2",
    "r_dlightShadowMaxLights": "8",
    "r_dlightShadowResolution": "256",
    "r_staticLights": "1",
    "r_staticLightDebug": "1",
    "r_staticLightMaxLights": "8",
    "r_staticLightShadows": "1",
    "r_staticLightShadowMaxLights": "2",
    "r_csmShadows": "1",
    "r_csmDebug": "1",
    "r_csmCascadeCount": "4",
    "r_csmDebugFallback": "0",
    "r_csmResolution": "512",
    "r_csmShadowFilter": "2",
    "r_spotShadows": "1",
    "r_spotShadowDebug": "1",
    "r_spotShadowMaxLights": "16",
    "r_spotShadowResolution": "512",
    "r_surfaceLightProxies": "1",
    "r_surfaceLightProxyDebug": "1",
    "r_surfaceLightProxyShadows": "1",
}

SURFACELIGHT_SPOT_EVIDENCE_CATEGORIES = (
    "surfacelight-large-planar",
)

CSM_SKY_SUN_EVIDENCE_CATEGORIES = (
    "csm-sky-sun",
)

CSM_SHIMMER_EVIDENCE_CATEGORIES = (
    "csm-shimmer-path",
)

COMBINED_SHADOW_ATLAS_SCENE_ID = "combined-shadow-atlas"
COMBINED_SHADOW_ATLAS_EVIDENCE_CATEGORIES = (
    COMBINED_SHADOW_ATLAS_SCENE_ID,
)
CSM_FALLBACK_SCENE_REASONS = {
    "csm-fallback-no-world": "no-world",
    "csm-fallback-no-sun": "no-sky-sun",
    "csm-fallback-atlas-unavailable": "atlas",
    "csm-fallback-zero-cascade": "zero-cascade",
}
CSM_FALLBACK_EVIDENCE_CATEGORIES = tuple(CSM_FALLBACK_SCENE_REASONS)
CSM_FALLBACK_REQUIRED_REASONS = tuple(CSM_FALLBACK_SCENE_REASONS.values())

CSM_SHADOW_EVIDENCE_CATEGORIES = (
    *CSM_SKY_SUN_EVIDENCE_CATEGORIES,
    *CSM_SHIMMER_EVIDENCE_CATEGORIES,
)

DLIGHT_SHADOW_EVIDENCE_CATEGORIES = (
    "world-geometry",
    "brush-models",
    "entities",
    "alpha-tested-surfaces",
    "portals-mirrors",
    "stress-light-budget",
    *CSM_SHADOW_EVIDENCE_CATEGORIES,
    *SURFACELIGHT_SPOT_EVIDENCE_CATEGORIES,
    *COMBINED_SHADOW_ATLAS_EVIDENCE_CATEGORIES,
    *CSM_FALLBACK_EVIDENCE_CATEGORIES,
)

SURFACELIGHT_SPOT_LOD_TILES = {
    "low": 128,
    "nominal": 256,
    "promoted": 512,
}
SURFACELIGHT_SPOT_REQUESTED_TILE_CAP = 512
SURFACELIGHT_SPOT_LOD_MAX_FILL_PERCENT = 85
CSM_SHIMMER_MIN_SAMPLES = 4
CSM_SHIMMER_MAX_GENERATION_DELTA = 1
CSM_SHIMMER_MAX_SNAP_DEPTH_DELTA_MILLI = 1000
CSM_SHIMMER_BASELINE_STEP = "baseline"
CSM_SHIMMER_SCREENSHOT_MAX_RMS = 6.0
CSM_SHIMMER_SCREENSHOT_MAX_CHANGED_PIXEL_RATIO = 0.10
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
COMBINED_SHADOW_ATLAS_SIDECAR_LIGHTS = (
    {
        "name": "combined-sidecar-spot",
        "type": "spot",
        "origin": [0.0, 0.0, 768.0],
        "direction": [0.0, 0.0, -1.0],
        "color": [1.0, 0.82, 0.55],
        "intensity": 900.0,
        "radius": 2048.0,
        "innerAngle": 18.0,
        "outerAngle": 48.0,
        "castsShadows": True,
        "resolution": 256,
        "priority": 8.0,
    },
)
CSM_SHIMMER_CAMERA_PATH = (
    {
        "id": "baseline",
        "setviewpos": "setviewpos 0.000 0.000 512.000 -10.000 90.000 0.000",
        "originDelta": "0 0 0",
        "axisDelta": "0 0 0",
    },
    {
        "id": "nudge-forward",
        "setviewpos": "setviewpos 0.125 0.000 512.000 -10.000 90.000 0.000",
        "originDelta": "0.125 0 0",
        "axisDelta": "0 0 0",
    },
    {
        "id": "nudge-side",
        "setviewpos": "setviewpos 0.000 0.125 512.000 -10.000 90.000 0.000",
        "originDelta": "0 0.125 0",
        "axisDelta": "0 0 0",
    },
    {
        "id": "micro-yaw",
        "setviewpos": "setviewpos 0.000 0.000 512.000 -10.000 90.030 0.000",
        "originDelta": "0 0 0",
        "axisDelta": "yaw +0.03",
    },
)

DLIGHT_SHADOW_EVIDENCE_SCENES = (
    {
        "id": "world-geometry",
        "map": "q3dm6",
        "categories": ("world-geometry",),
        "description": "Large retail geometry scene for world caster and receiver coverage.",
        "dlight": {"count": 8, "intensity": 720, "distance": 224, "height": 48, "seconds": 0},
    },
    {
        "id": "brush-models",
        "map": "q3dm1",
        "categories": ("brush-models",),
        "description": "Small retail scene with interactive brush-model coverage near player start.",
        "dlight": {"count": 6, "intensity": 700, "distance": 192, "height": 48, "seconds": 0},
    },
    {
        "id": "entities",
        "map": "q3dm1",
        "categories": ("entities",),
        "description": "Entity-model dlight shadow coverage with r_dlightMode 2 enabled.",
        "dlight": {"count": 8, "intensity": 760, "distance": 208, "height": 56, "seconds": 0},
    },
    {
        "id": "alpha-tested-surfaces",
        "map": "q3dm11",
        "categories": ("alpha-tested-surfaces",),
        "description": "Shader-heavy retail scene for alpha-test and material-stage receivers.",
        "dlight": {"count": 8, "intensity": 740, "distance": 224, "height": 48, "seconds": 0},
    },
    {
        "id": "portals-mirrors",
        "map": "q3dm17",
        "categories": ("portals-mirrors",),
        "description": "Open retail scene with portals and mirrors explicitly left enabled.",
        "cvars": {"r_noportals": "0", "r_portalOnly": "0"},
        "dlight": {"count": 8, "intensity": 720, "distance": 240, "height": 64, "seconds": 0},
    },
    {
        "id": "stress-light-budget",
        "map": "q3dm6",
        "categories": ("stress-light-budget",),
        "description": "Over-budget dynamic-light ring to exercise atlas budget and skip logging.",
        "dlight": {"count": 16, "intensity": 900, "distance": 256, "height": 72, "seconds": 0},
    },
    {
        "id": "csm-sky-sun",
        "map": "q3dm17",
        "categories": CSM_SKY_SUN_EVIDENCE_CATEGORIES,
        "description": "Outdoor sky-sun scene for CSM schedule, atlas publication, and cache telemetry evidence.",
        "dlight": {"count": 8, "intensity": 720, "distance": 240, "height": 64, "seconds": 0},
    },
    {
        "id": "csm-shimmer-path",
        "map": "q3dm17",
        "categories": CSM_SHIMMER_EVIDENCE_CATEGORIES,
        "description": "Deterministic tiny camera path for CSM snapped-coordinate, cache, and atlas generation churn evidence.",
        "dlight": {"count": 4, "intensity": 640, "distance": 192, "height": 48, "seconds": 0},
        "csmCameraPath": CSM_SHIMMER_CAMERA_PATH,
    },
    {
        "id": "surfacelight-large-planar",
        "map": "q3dm6",
        "categories": SURFACELIGHT_SPOT_EVIDENCE_CATEGORIES,
        "description": "Large planar q3map_surfaceLight emitters with surfacelight proxies and the 2D spot atlas enabled.",
        "dlight": {"count": 8, "intensity": 720, "distance": 224, "height": 64, "seconds": 0},
    },
    {
        "id": COMBINED_SHADOW_ATLAS_SCENE_ID,
        "map": "q3dm6",
        "categories": COMBINED_SHADOW_ATLAS_EVIDENCE_CATEGORIES,
        "description": "One-frame point, static sidecar spot, surfacelight spot, and CSM atlas publication smoke.",
        "setviewpos": "setviewpos 0.000 0.000 512.000 -10.000 90.000 0.000",
        "dlight": {"count": 8, "intensity": 780, "distance": 224, "height": 56, "seconds": 0},
        "sidecarLights": COMBINED_SHADOW_ATLAS_SIDECAR_LIGHTS,
    },
    {
        "id": "csm-fallback-no-world",
        "map": "q3dm17",
        "categories": ("csm-fallback-no-world",),
        "description": "Forced no-world CSM fallback smoke with no CSM atlas or receiver publication.",
        "cvars": {"r_csmDebugFallback": "1"},
        "dlight": {"count": 4, "intensity": 640, "distance": 192, "height": 48, "seconds": 0},
    },
    {
        "id": "csm-fallback-no-sun",
        "map": "q3dm17",
        "categories": ("csm-fallback-no-sun",),
        "description": "Forced no-sky-sun CSM fallback smoke with no CSM atlas or receiver publication.",
        "cvars": {"r_csmDebugFallback": "2"},
        "dlight": {"count": 4, "intensity": 640, "distance": 192, "height": 48, "seconds": 0},
    },
    {
        "id": "csm-fallback-atlas-unavailable",
        "map": "q3dm17",
        "categories": ("csm-fallback-atlas-unavailable",),
        "description": "Forced atlas-unavailable CSM fallback smoke with no CSM receiver publication.",
        "cvars": {"r_csmDebugFallback": "3"},
        "dlight": {"count": 4, "intensity": 640, "distance": 192, "height": 48, "seconds": 0},
    },
    {
        "id": "csm-fallback-zero-cascade",
        "map": "q3dm17",
        "categories": ("csm-fallback-zero-cascade",),
        "description": "Forced zero-cascade CSM fallback smoke with no CSM atlas or receiver publication.",
        "cvars": {"r_csmDebugFallback": "4"},
        "dlight": {"count": 4, "intensity": 640, "distance": 192, "height": 48, "seconds": 0},
    },
)

STARTUP_CVAR_NAMES = {
    "developer",
    "logfile",
    "r_fullscreen",
    "r_mode",
    "r_customWidth",
    "r_customHeight",
    "r_fbo",
    "r_hdr",
    "r_hdrPrecision",
    "r_hdrDisplay",
    "r_bloom",
    "r_motionBlur",
    "r_bloom_soft_knee",
    "r_ext_multisample",
    "r_tonemap",
    "r_tonemapExposure",
    "r_colorGrade",
    "r_colorGradeLift",
    "r_colorGradeGamma",
    "r_colorGradeGain",
    "r_colorGradeWhitePoint",
    "r_colorGradeAdaptWhitePoint",
    "r_colorGradeLUT",
    "r_colorGradeLUTScale",
    "r_vbo",
    "r_dynamiclight",
    "r_dlightMode",
    "r_dlightShadows",
    "r_dlightShadowDebug",
    "r_dlightShadowFilter",
    "r_dlightShadowMaxLights",
    "r_dlightShadowResolution",
    "r_staticLights",
    "r_staticLightDebug",
    "r_staticLightMaxLights",
    "r_staticLightShadows",
    "r_staticLightShadowMaxLights",
    "r_csmShadows",
    "r_csmDebug",
    "r_csmDebugFallback",
    "r_csmCascadeCount",
    "r_csmResolution",
    "r_csmShadowFilter",
    "r_spotShadows",
    "r_spotShadowDebug",
    "r_spotShadowMaxLights",
    "r_spotShadowResolution",
    "r_surfaceLightProxies",
    "r_surfaceLightProxyDebug",
    "r_surfaceLightProxyShadows",
}

PROFILE_CVARS = {
    "baseline": {
        "r_fbo": "0",
        "r_vbo": "1",
    },
    "vk-modern": {
        "r_fbo": "1",
        "r_vbo": "1",
        "r_hdr": "1",
        "r_bloom": "1",
        "r_bloom_soft_knee": "0.5",
        "r_ext_multisample": "4",
        "r_tonemap": "2",
        "r_tonemapExposure": "1.0",
        "r_colorGrade": "3",
        "r_colorGradeLift": "0 0 0",
        "r_colorGradeGamma": "1 1 1",
        "r_colorGradeGain": "1 1 1",
        "r_colorGradeWhitePoint": "6504",
        "r_colorGradeAdaptWhitePoint": "6504",
        "r_colorGradeLUTScale": "4.0",
    },
    "vk-hdr": {
        "r_fbo": "1",
        "r_vbo": "1",
        "r_hdr": "1",
        "r_bloom": "1",
        "r_bloom_soft_knee": "0.5",
        "r_ext_multisample": "4",
        "r_tonemap": "2",
        "r_tonemapExposure": "1.0",
        "r_colorGrade": "3",
        "r_colorGradeLift": "0 0 0",
        "r_colorGradeGamma": "1 1 1",
        "r_colorGradeGain": "1 1 1",
        "r_colorGradeWhitePoint": "6504",
        "r_colorGradeAdaptWhitePoint": "6504",
        "r_colorGradeLUTScale": "4.0",
        "r_hdrDisplay": "1",
        "r_outputBackend": "3",
        "r_hdrDisplayPaperWhite": "203",
        "r_hdrDisplayMaxLuminance": "1000",
        "r_hdrDisplayMaxCLL": "1000",
        "r_hdrDisplayMaxFALL": "400",
    },
}

RC_GATE_PRESETS = {
    "vk-smoke": {
        "description": "Vulkan renderer lifecycle smoke gate for module load, map load, screenshots, and vkinfo diagnostics.",
        "defaults": {
            "profile": "baseline",
            "maps": "q3dm1",
            "demos": "",
            "timeout": 240.0,
        },
        "requirements": {
            "require_screenshots": True,
            "require_vkinfo": True,
        },
    },
    "vk-modern": {
        "description": "Modern Vulkan gate for FBO/HDR/bloom/MSAA path, vkinfo counters, GPU timings, and timedemo metrics.",
        "defaults": {
            "profile": "vk-modern",
            "maps": "q3dm1,q3dm17",
            "demos": "demo1",
            "timeout": 360.0,
            "dlight_shadow_scenes": True,
        },
        "requirements": {
            "require_screenshots": True,
            "require_vkinfo": True,
            "require_gpu_timings": True,
            "require_timedemo_metrics": True,
            "require_dlight_shadow_scenes": True,
            "required_dlight_shadow_categories": DLIGHT_SHADOW_EVIDENCE_CATEGORIES,
            "required_surface_light_spot_categories": SURFACELIGHT_SPOT_EVIDENCE_CATEGORIES,
            "required_csm_shadow_categories": CSM_SHADOW_EVIDENCE_CATEGORIES,
            "require_csm_shimmer_screenshot_diff": True,
            "require_combined_shadow_atlas_smoke": True,
            "require_csm_fallback_smoke": True,
        },
    },
    "vk-hdr": {
        "description": "Native-HDR request gate for the HDR10 swapchain path on capable runtime runners.",
        "defaults": {
            "profile": "vk-hdr",
            "maps": "q3dm1",
            "demos": "",
            "timeout": 300.0,
        },
        "requirements": {
            "require_screenshots": True,
            "require_vkinfo": True,
            "require_hdr_request": True,
        },
    },
}


def split_csv(value: str | None) -> list[str]:
    if not value:
        return []
    return [part.strip() for part in value.split(",") if part.strip()]


def sanitize(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_.-]+", "-", value.strip())
    return cleaned.strip("-") or "item"


def validate_q3_command_token(value: str, option: str) -> str:
    token = value.strip()
    parts = token.split("/")
    if (
        not Q3_COMMAND_TOKEN_RE.fullmatch(token)
        or any(part in {"", ".", ".."} for part in parts)
    ):
        raise ValueError(f"{option} must contain safe Quake 3 qpath tokens")
    return token


def validate_q3_command_tokens(values: list[str], option: str) -> list[str]:
    return [validate_q3_command_token(value, option) for value in values]


def validate_fs_game(value: str) -> str:
    name = value.strip()
    if not name:
        return ""
    if (
        not FS_GAME_RE.fullmatch(name)
        or name in {".", ".."}
        or name.startswith(".")
    ):
        raise ValueError("--fs-game must be a single safe mod directory name")
    return name


def q3_quote(value: object) -> str:
    text = str(value).replace("\\", "/").replace('"', '\\"')
    return f'"{text}"'


def q3_path(path: Path) -> str:
    return str(path).replace("\\", "/")


def command_to_string(command: list[str]) -> str:
    quoted: list[str] = []
    for part in command:
        if re.search(r"\s", part):
            quoted.append('"' + part.replace('"', '\\"') + '"')
        else:
            quoted.append(part)
    return " ".join(quoted)


def game_dir(fs_game: str) -> str:
    return fs_game if fs_game else "baseq3"


def parse_extra_sets(values: list[str]) -> dict[str, str]:
    result: dict[str, str] = {}
    for item in values:
        if "=" not in item:
            raise ValueError(f"--extra-set expects NAME=VALUE, got {item!r}")
        name, value = item.split("=", 1)
        name = name.strip()
        if not name:
            raise ValueError("--extra-set cvar name must not be empty")
        if not CVAR_NAME_RE.fullmatch(name):
            raise ValueError(f"--extra-set cvar name is not safe: {name!r}")
        if any(char in value for char in ("\r", "\n", "\x00", ";")):
            raise ValueError(f"--extra-set value for {name!r} contains an unsafe control character")
        result[name] = value.strip()
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run or plan FnQL Vulkan runtime verification gates."
    )
    parser.add_argument("--gate", choices=sorted(RC_GATE_PRESETS))
    parser.add_argument("--list-gates", action="store_true")
    parser.add_argument("--exe", type=Path, help="Client executable to launch.")
    parser.add_argument(
        "--basepath",
        type=Path,
        help="Game asset basepath. Defaults to the executable directory.",
    )
    parser.add_argument(
        "--homepath",
        type=Path,
        help="Temporary fs_homepath. Defaults under .tmp/vk-runtime-sweeps/<run-id>/home.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_SWEEP_ROOT,
        help="Sweep output root for configs, logs, manifests, and default homepath.",
    )
    parser.add_argument("--fs-game", default="")
    parser.add_argument("--profile", choices=sorted(PROFILE_CVARS))
    parser.add_argument("--maps")
    parser.add_argument("--demos")
    parser.add_argument("--width", type=int)
    parser.add_argument("--height", type=int)
    parser.add_argument("--startup-wait", type=int)
    parser.add_argument("--map-wait", type=int)
    parser.add_argument("--screenshot-wait", type=int)
    parser.add_argument("--perf-sample-wait", type=int)
    parser.add_argument("--timeout", type=float)
    parser.add_argument("--extra-set", action="append", default=[], metavar="NAME=VALUE")
    shadow_group = parser.add_mutually_exclusive_group()
    shadow_group.add_argument(
        "--dlight-shadow-scenes",
        dest="dlight_shadow_scenes",
        action="store_true",
        default=None,
        help="Add dedicated r_dlightTest dynamic-light shadow map scenes to the sweep.",
    )
    shadow_group.add_argument(
        "--no-dlight-shadow-scenes",
        dest="dlight_shadow_scenes",
        action="store_false",
        help="Disable dlight shadow scenes requested by a gate preset.",
    )
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--no-map-sweep", action="store_true")
    parser.add_argument("--no-demo-sweep", action="store_true")
    parser.add_argument("--no-perf-samples", action="store_true")
    parser.add_argument("--summary-markdown", type=Path)
    return parser.parse_args()


def apply_gate_defaults(args: argparse.Namespace) -> None:
    defaults = dict(DEFAULT_OPTIONS)
    if args.gate:
        defaults.update(RC_GATE_PRESETS[args.gate]["defaults"])  # type: ignore[arg-type]

    for name, value in defaults.items():
        attr = name
        if getattr(args, attr, None) is None:
            setattr(args, attr, value)


def validate_runtime_options(args: argparse.Namespace) -> None:
    if args.width <= 0 or args.height <= 0:
        raise ValueError("--width and --height must be positive")
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        raise ValueError("--timeout must be finite and positive")
    for attr in ("startup_wait", "map_wait", "screenshot_wait", "perf_sample_wait"):
        if getattr(args, attr) < 0:
            raise ValueError(f"--{attr.replace('_', '-')} must be non-negative")


def int_metric(value: object, default: int = 0) -> int:
    if isinstance(value, bool):
        return default
    try:
        return int(value)  # type: ignore[arg-type]
    except (TypeError, ValueError, OverflowError):
        return default


def print_gate_list() -> None:
    for name in sorted(RC_GATE_PRESETS):
        gate = RC_GATE_PRESETS[name]
        defaults = gate["defaults"]
        print(f"{name}: {gate['description']}")
        print(
            "  "
            f"profile={defaults['profile']} maps={defaults['maps']} demos={defaults['demos'] or '-'}"
            f" dlight-shadow-scenes={defaults.get('dlight_shadow_scenes', False)}"
        )


def make_cvars(args: argparse.Namespace) -> dict[str, str]:
    cvars = dict(COMMON_CVARS)
    cvars.update(PROFILE_CVARS[args.profile])
    cvars.update(
        {
            "r_customWidth": str(args.width),
            "r_customHeight": str(args.height),
        }
    )
    cvars.update(parse_extra_sets(args.extra_set))
    return cvars


def dlight_shadow_scene_cvars(cvars: dict[str, str]) -> dict[str, str]:
    shadow_cvars = dict(cvars)
    for name, value in DLIGHT_SHADOW_SCENE_CVARS.items():
        shadow_cvars.setdefault(name, value)
    return shadow_cvars


def dlight_shadow_evidence_scenes() -> list[dict[str, object]]:
    scenes: list[dict[str, object]] = []
    for scene in DLIGHT_SHADOW_EVIDENCE_SCENES:
        copied = {
            **scene,
            "categories": list(scene["categories"]),
            "cvars": dict(scene.get("cvars", {})),
            "dlight": dict(scene["dlight"]),
        }
        if scene.get("csmCameraPath"):
            copied["csmCameraPath"] = [
                dict(step)
                for step in scene.get("csmCameraPath", [])  # type: ignore[union-attr]
                if isinstance(step, dict)
            ]
        if scene.get("sidecarLights"):
            copied["sidecarLights"] = [
                dict(light)
                for light in scene.get("sidecarLights", [])  # type: ignore[union-attr]
                if isinstance(light, dict)
            ]
        scenes.append(copied)
    return scenes


def dlight_shadow_scene_categories(screenshots: list[dict[str, object]]) -> set[str]:
    categories: set[str] = set()
    for shot in screenshots:
        if not shot.get("shadowScene"):
            continue
        for category in shot.get("evidenceCategories", []):
            text = str(category).strip()
            if text:
                categories.add(text)
    return categories


def write_dlight_shadow_sidecar_lights(
    homepath: Path,
    fs_game: str,
    scenes: list[dict[str, object]],
) -> list[dict[str, object]]:
    lights_by_map: dict[str, list[dict[str, object]]] = {}
    scene_ids_by_map: dict[str, list[str]] = {}
    for scene in scenes:
        sidecar_lights = scene.get("sidecarLights", [])
        if not isinstance(sidecar_lights, list) or not sidecar_lights:
            continue
        map_name = str(scene.get("map", "")).strip()
        if not map_name:
            continue
        lights_by_map.setdefault(map_name, []).extend(
            dict(light) for light in sidecar_lights if isinstance(light, dict)
        )
        scene_ids_by_map.setdefault(map_name, []).append(str(scene.get("id", "")))

    records: list[dict[str, object]] = []
    maps_dir = homepath / game_dir(fs_game) / "maps"
    for map_name, lights in sorted(lights_by_map.items()):
        if not lights:
            continue
        path = maps_dir / f"{map_name}.lights.json"
        payload = {
            "version": 1,
            "lights": lights,
        }
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(
            json.dumps(payload, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        records.append(
            {
                "map": map_name,
                "path": str(path),
                "lightCount": len(lights),
                "scenes": [scene_id for scene_id in scene_ids_by_map.get(map_name, []) if scene_id],
            }
        )
    return records


def shadow_manager_summary_active(summary: object) -> bool:
    if not isinstance(summary, dict) or not summary.get("found"):
        return False
    maximum = summary.get("max")
    if not isinstance(maximum, dict):
        return False

    def field(name: str) -> int:
        return int_metric(maximum.get(name, 0))

    return (
        field("scheduledPasses") > 0
        and field("scheduledMask") > 0
        and field("pointScheduled") > 0
        and field("pointPublished") > 0
        and field("pointPlanned") > 0
        and field("pointRecords") > 0
        and field("pointAtlasWidth") > 0
        and field("pointAtlasHeight") > 0
        and field("pointAtlasFaceSize") > 0
    )


def surface_light_spot_summary_active(summary: object) -> bool:
    if not isinstance(summary, dict) or not summary.get("found"):
        return False
    maximum = summary.get("max")
    if not isinstance(maximum, dict):
        return False

    def field(name: str) -> int:
        return int_metric(maximum.get(name, 0))

    return (
        field("surfaceSpotCandidates") > 0
        and field("surfaceSpotPlans") > 0
        and field("surfaceSpotAllocated") > 0
        and field("surfaceSpotAtlasWidth") > 0
        and field("surfaceSpotAtlasHeight") > 0
        and field("surfaceSpotAtlasTileSize") > 0
        and field("surfaceSpotFootprintMax") > 0
        and field("surfaceSpotCasterRadiusMax") > 0
        and field("surfaceSpotTileMax") > 0
    )


def surface_light_spot_lod_summary_active(summary: object) -> bool:
    return (
        isinstance(summary, dict)
        and bool(summary.get("found"))
        and str(summary.get("status", "")).lower() == "passed"
    )


def csm_shadow_runtime_summary_active(summary: object) -> bool:
    return (
        isinstance(summary, dict)
        and bool(summary.get("found"))
        and str(summary.get("status", "")).lower() == "passed"
    )


def combined_shadow_atlas_summary_active(summary: object) -> bool:
    return (
        isinstance(summary, dict)
        and bool(summary.get("found"))
        and str(summary.get("status", "")).lower() == "passed"
    )


def shadow_profile_summary_active(summary: object) -> bool:
    return (
        isinstance(summary, dict)
        and bool(summary.get("found"))
        and bool(summary.get("profileReady"))
        and str(summary.get("status", "")).lower() == "passed"
    )


def combined_shadow_atlas_summary(dlight_shadow: object) -> dict[str, object]:
    if not isinstance(dlight_shadow, dict):
        return {
            "found": False,
            "status": "missing",
            "scene": COMBINED_SHADOW_ATLAS_SCENE_ID,
            "sampleCount": 0,
            "max": {},
            "failures": ["Combined shadow atlas smoke is missing dlight shadow analysis."],
        }

    manager = dlight_shadow.get("shadowManager")
    scene_summary: object = None
    if isinstance(manager, dict) and isinstance(manager.get("scenes"), dict):
        scene_summary = manager["scenes"].get(COMBINED_SHADOW_ATLAS_SCENE_ID)  # type: ignore[index]
    if not isinstance(scene_summary, dict):
        return {
            "found": False,
            "status": "missing",
            "scene": COMBINED_SHADOW_ATLAS_SCENE_ID,
            "sampleCount": 0,
            "max": {},
            "failures": ["Combined shadow atlas smoke is missing manager scene samples."],
        }

    maximum = scene_summary.get("max", {})
    if not isinstance(maximum, dict):
        maximum = {}

    def field(name: str) -> int:
        return int_metric(maximum.get(name, 0))

    checks = (
        ("scheduledPasses", 4, "scheduled all point, spot, CSM atlas, and CSM receiver passes"),
        ("pointScheduled", 1, "scheduled the point shadow atlas"),
        ("spotScheduled", 1, "scheduled the spot shadow atlas"),
        ("csmAtlasScheduled", 1, "scheduled the CSM atlas"),
        ("csmReceiverScheduled", 1, "scheduled the CSM receiver pass"),
        ("pointPublished", 1, "published the point shadow atlas"),
        ("spotPublished", 1, "published the spot shadow atlas"),
        ("csmPublished", 1, "published the CSM atlas"),
        ("pointPlanned", 1, "planned point shadow lights"),
        ("pointRecords", 1, "recorded point shadow atlas metadata"),
        ("pointAtlasWidth", 1, "reported point atlas width"),
        ("pointAtlasHeight", 1, "reported point atlas height"),
        ("pointAtlasFaceSize", 1, "reported point atlas face size"),
        ("spotPlans", 1, "planned spot shadow lights"),
        ("spotStaticPlans", 1, "planned static sidecar spot shadows"),
        ("spotSurfacePlans", 1, "planned surfacelight spot proxy shadows"),
        ("spotAtlasWidth", 1, "reported spot atlas width"),
        ("spotAtlasHeight", 1, "reported spot atlas height"),
        ("spotAtlasTileSize", 1, "reported spot atlas tile size"),
        ("csmCascadeCount", 1, "reported CSM cascades"),
        ("csmAtlasWidth", 1, "reported CSM atlas width"),
        ("csmAtlasHeight", 1, "reported CSM atlas height"),
        ("csmGeneration", 1, "reported CSM atlas generation"),
    )
    failures: list[str] = []
    for name, minimum, label in checks:
        if field(name) < minimum:
            failures.append(
                "Combined shadow atlas smoke did not "
                f"{label}: {name}={field(name)}<{minimum}."
            )
    if (field("scheduledMask") & 0x0F) != 0x0F:
        failures.append(
            "Combined shadow atlas smoke did not report all combined shadow pass bits "
            f"in the schedule mask: scheduledMask=0x{field('scheduledMask'):x}, expected bits 0x0f."
        )

    return {
        "found": True,
        "status": "failed" if failures else "passed",
        "scene": COMBINED_SHADOW_ATLAS_SCENE_ID,
        "sampleCount": int(scene_summary.get("sampleCount", 0)),
        "max": {
            name: field(name)
            for name, _minimum, _label in checks
        } | {"scheduledMask": field("scheduledMask")},
        "failures": failures,
    }


def int_sample_field(sample: dict[str, int], name: str) -> int:
    return int_metric(sample.get(name, 0))


def surface_light_spot_range_includes(
    sample: dict[str, int], min_name: str, max_name: str, value: int
) -> bool:
    minimum = int_sample_field(sample, min_name)
    maximum = int_sample_field(sample, max_name)
    return minimum > 0 and minimum <= value <= maximum


def surface_light_spot_lod_summary(samples: list[dict[str, int]]) -> dict[str, object]:
    requested_tiles = {name: False for name in SURFACELIGHT_SPOT_LOD_TILES}
    failures: list[str] = []
    max_requested_tile = 0
    max_plan_requested_tile = 0
    max_effective_tile = 0
    max_atlas_tile = 0
    max_fill = 0

    if not samples:
        return {
            "found": False,
            "status": "missing",
            "sampleCount": 0,
            "requestedTiles": requested_tiles,
            "maxRequestedTile": 0,
            "maxPlanRequestedTile": 0,
            "maxEffectiveTile": 0,
            "maxAtlasTile": 0,
            "maxFill": 0,
            "failures": [],
        }

    for sample in samples:
        requested_max = int_sample_field(sample, "surfaceSpotRequestedTileMax")
        plan_requested_max = int_sample_field(sample, "surfaceSpotPlanRequestedTileMax")
        effective_max = int_sample_field(sample, "surfaceSpotTileMax")
        atlas_tile = int_sample_field(sample, "surfaceSpotAtlasTileSize")
        fill = int_sample_field(sample, "surfaceSpotAtlasFill")

        max_requested_tile = max(max_requested_tile, requested_max)
        max_plan_requested_tile = max(max_plan_requested_tile, plan_requested_max)
        max_effective_tile = max(max_effective_tile, effective_max)
        max_atlas_tile = max(max_atlas_tile, atlas_tile)
        max_fill = max(max_fill, fill)

        for label, tile_size in SURFACELIGHT_SPOT_LOD_TILES.items():
            if surface_light_spot_range_includes(
                sample,
                "surfaceSpotRequestedTileMin",
                "surfaceSpotRequestedTileMax",
                tile_size,
            ):
                requested_tiles[label] = True

        if requested_max > SURFACELIGHT_SPOT_REQUESTED_TILE_CAP:
            failures.append(
                "Surfacelight spot requested tile exceeded cap: "
                f"{requested_max}>{SURFACELIGHT_SPOT_REQUESTED_TILE_CAP}."
            )
        if plan_requested_max > SURFACELIGHT_SPOT_REQUESTED_TILE_CAP:
            failures.append(
                "Surfacelight spot planned request exceeded cap: "
                f"{plan_requested_max}>{SURFACELIGHT_SPOT_REQUESTED_TILE_CAP}."
            )
        if atlas_tile > 0 and effective_max > atlas_tile:
            failures.append(
                "Surfacelight spot effective tile exceeded atlas tile: "
                f"{effective_max}>{atlas_tile}."
            )
        if fill > SURFACELIGHT_SPOT_LOD_MAX_FILL_PERCENT:
            failures.append(
                "Surfacelight spot atlas fill exceeded smoke bound: "
                f"{fill}%>{SURFACELIGHT_SPOT_LOD_MAX_FILL_PERCENT}%."
            )

    missing_tiles = [
        f"{label}({tile_size})"
        for label, tile_size in SURFACELIGHT_SPOT_LOD_TILES.items()
        if not requested_tiles[label]
    ]
    if missing_tiles:
        failures.append(
            "Surfacelight spot LOD smoke missing requested tile coverage: "
            + ", ".join(missing_tiles)
            + "."
        )
    if max_effective_tile > 0 and max_atlas_tile <= 0:
        failures.append("Surfacelight spot LOD smoke has effective tiles without an atlas tile size.")

    return {
        "found": True,
        "status": "failed" if failures else "passed",
        "sampleCount": len(samples),
        "requestedTiles": requested_tiles,
        "maxRequestedTile": max_requested_tile,
        "maxPlanRequestedTile": max_plan_requested_tile,
        "maxEffectiveTile": max_effective_tile,
        "maxAtlasTile": max_atlas_tile,
        "maxFill": max_fill,
        "failures": list(dict.fromkeys(failures)),
    }


def csm_shadow_runtime_summary(
    manager_samples: list[dict[str, int]],
    csm_samples: list[dict[str, int]],
) -> dict[str, object]:
    manager_max = summarize_int_samples(manager_samples)["max"] if manager_samples else {}
    csm_max = summarize_int_samples(csm_samples)["max"] if csm_samples else {}
    failures: list[str] = []

    def manager_field(name: str) -> int:
        return int_metric(manager_max.get(name, 0)) if isinstance(manager_max, dict) else 0

    def csm_field(name: str) -> int:
        return int_metric(csm_max.get(name, 0)) if isinstance(csm_max, dict) else 0

    if not manager_samples and not csm_samples:
        return {
            "found": False,
            "status": "missing",
            "managerSampleCount": 0,
            "debugSampleCount": 0,
            "max": {},
            "failures": [],
        }

    if not manager_samples:
        failures.append("CSM runtime smoke missing shadow manager samples.")
    if not csm_samples:
        failures.append("CSM runtime smoke missing csm debug samples.")
    if manager_samples:
        if manager_field("csmAtlasScheduled") <= 0:
            failures.append("CSM runtime smoke did not schedule the CSM atlas pass.")
        if manager_field("csmReceiverScheduled") <= 0:
            failures.append("CSM runtime smoke did not schedule the CSM receiver pass.")
        if manager_field("csmPublished") <= 0:
            failures.append("CSM runtime smoke did not publish the CSM atlas.")
        if manager_field("csmCascadeCount") <= 0:
            failures.append("CSM runtime smoke reported zero manager cascades.")
        if manager_field("csmAtlasWidth") <= 0 or manager_field("csmAtlasHeight") <= 0:
            failures.append("CSM runtime smoke reported missing manager atlas dimensions.")
        if manager_field("csmGeneration") <= 0:
            failures.append("CSM runtime smoke reported no CSM atlas generation.")
    if csm_samples:
        if csm_field("csmDebugCascades") <= 0:
            failures.append("CSM runtime smoke reported zero debug cascades.")
        if csm_field("csmDebugResolution") <= 0:
            failures.append("CSM runtime smoke reported missing debug resolution.")
        cache_events = (
            csm_field("csmCacheHits")
            + csm_field("csmCacheMisses")
            + csm_field("csmCacheUncacheable")
        )
        if cache_events <= 0:
            failures.append("CSM runtime smoke reported no cache hit/miss/uncacheable telemetry.")

    maximum = {}
    if isinstance(manager_max, dict):
        maximum.update(
            {
                "csmAtlasScheduled": manager_field("csmAtlasScheduled"),
                "csmReceiverScheduled": manager_field("csmReceiverScheduled"),
                "csmPublished": manager_field("csmPublished"),
                "csmCascadeCount": manager_field("csmCascadeCount"),
                "csmAtlasWidth": manager_field("csmAtlasWidth"),
                "csmAtlasHeight": manager_field("csmAtlasHeight"),
                "csmGeneration": manager_field("csmGeneration"),
            }
        )
    if isinstance(csm_max, dict):
        maximum.update(csm_max)
        maximum["csmCacheEvents"] = (
            csm_field("csmCacheHits")
            + csm_field("csmCacheMisses")
            + csm_field("csmCacheUncacheable")
        )

    return {
        "found": bool(manager_samples or csm_samples),
        "status": "failed" if failures else "passed",
        "managerSampleCount": len(manager_samples),
        "debugSampleCount": len(csm_samples),
        "max": maximum,
        "failures": list(dict.fromkeys(failures)),
    }


def csm_generation_delta(manager_samples: list[dict[str, int]]) -> tuple[int, int, int]:
    generations = [
        int_sample_field(sample, "csmGeneration")
        for sample in manager_samples
        if int_sample_field(sample, "csmGeneration") > 0
    ]
    if not generations:
        return 0, 0, 0
    return min(generations), max(generations), max(generations) - min(generations)


def csm_snap_depth_delta_milli(csm_samples: list[dict[str, int]]) -> int:
    deltas: list[int] = []
    for index in range(4):
        name = f"csmSnapDepth{index}Milli"
        values = [
            int_sample_field(sample, name)
            for sample in csm_samples
            if int_sample_field(sample, "csmSnapDepthSamples") > 0
        ]
        if values:
            deltas.append(max(values) - min(values))
    return max(deltas) if deltas else 0


def csm_stability_summary(
    manager_samples: list[dict[str, int]],
    csm_samples: list[dict[str, int]],
) -> dict[str, object]:
    manager_max = summarize_int_samples(manager_samples)["max"] if manager_samples else {}
    csm_max = summarize_int_samples(csm_samples)["max"] if csm_samples else {}
    min_generation, max_generation, generation_delta = csm_generation_delta(manager_samples)
    max_snap_depth_delta = csm_snap_depth_delta_milli(csm_samples)
    failures: list[str] = []

    def manager_field(name: str) -> int:
        return int_metric(manager_max.get(name, 0)) if isinstance(manager_max, dict) else 0

    def csm_field(name: str) -> int:
        return int_metric(csm_max.get(name, 0)) if isinstance(csm_max, dict) else 0

    if not manager_samples and not csm_samples:
        return {
            "found": False,
            "status": "missing",
            "managerSampleCount": 0,
            "debugSampleCount": 0,
            "minGeneration": 0,
            "maxGeneration": 0,
            "generationDelta": 0,
            "maxSnapDepthDeltaMilli": 0,
            "max": {},
            "failures": [],
        }

    if len(manager_samples) < CSM_SHIMMER_MIN_SAMPLES:
        failures.append(
            "CSM shimmer path manager samples below minimum: "
            f"{len(manager_samples)}<{CSM_SHIMMER_MIN_SAMPLES}."
        )
    if len(csm_samples) < CSM_SHIMMER_MIN_SAMPLES:
        failures.append(
            "CSM shimmer path debug samples below minimum: "
            f"{len(csm_samples)}<{CSM_SHIMMER_MIN_SAMPLES}."
        )
    if manager_field("csmPublished") <= 0 or manager_field("csmGeneration") <= 0:
        failures.append("CSM shimmer path did not publish a sampleable atlas generation.")
    if csm_field("csmDebugCascades") <= 0 or csm_field("csmDebugResolution") <= 0:
        failures.append("CSM shimmer path did not report cascade debug telemetry.")
    if csm_field("csmSnapDepthSamples") <= 0:
        failures.append("CSM shimmer path is missing light-depth center telemetry.")
    if (
        csm_field("csmCacheHits")
        + csm_field("csmCacheMisses")
        + csm_field("csmCacheUncacheable")
    ) <= 0:
        failures.append("CSM shimmer path reported no cache hit/miss/uncacheable telemetry.")
    if generation_delta > CSM_SHIMMER_MAX_GENERATION_DELTA:
        failures.append(
            "CSM shimmer path atlas generation churn exceeded smoke bound: "
            f"{generation_delta}>{CSM_SHIMMER_MAX_GENERATION_DELTA}."
        )
    if max_snap_depth_delta > CSM_SHIMMER_MAX_SNAP_DEPTH_DELTA_MILLI:
        failures.append(
            "CSM shimmer path light-depth center delta exceeded smoke bound: "
            f"{max_snap_depth_delta}>{CSM_SHIMMER_MAX_SNAP_DEPTH_DELTA_MILLI} milli-units."
        )

    maximum = {}
    if isinstance(manager_max, dict):
        maximum.update(manager_max)
    if isinstance(csm_max, dict):
        maximum.update(csm_max)
        maximum["csmCacheEvents"] = (
            csm_field("csmCacheHits")
            + csm_field("csmCacheMisses")
            + csm_field("csmCacheUncacheable")
        )

    return {
        "found": bool(manager_samples or csm_samples),
        "status": "failed" if failures else "passed",
        "managerSampleCount": len(manager_samples),
        "debugSampleCount": len(csm_samples),
        "minGeneration": min_generation,
        "maxGeneration": max_generation,
        "generationDelta": generation_delta,
        "maxSnapDepthDeltaMilli": max_snap_depth_delta,
        "max": maximum,
        "failures": list(dict.fromkeys(failures)),
    }


def format_dlight_test_command(dlight: dict[str, object]) -> str:
    return (
        "r_dlightTest "
        f"{int(dlight['count'])} "
        f"{float(dlight['intensity']):g} "
        f"{float(dlight['distance']):g} "
        f"{float(dlight['height']):g} "
        f"{float(dlight['seconds']):g}"
    )


def launch_cvars(cvars: dict[str, str]) -> dict[str, str]:
    return {name: value for name, value in cvars.items() if name in STARTUP_CVAR_NAMES}


def cfg_preamble(cvars: dict[str, str], title: str) -> list[str]:
    lines = [f"// Generated by scripts/vk_runtime_sweep.py for {title}"]
    for name in sorted(cvars):
        lines.append(f"set {name} {q3_quote(cvars[name])}")
    lines.append('set timedemo "0"')
    lines.append('set nextdemo ""')
    return lines


def build_map_cfg(
    args: argparse.Namespace,
    cvars: dict[str, str],
    maps: list[str],
    run_id: str,
) -> tuple[str, list[dict[str, object]]]:
    lines = cfg_preamble(cvars, "Vulkan map screenshot sweep")
    screenshots: list[dict[str, object]] = []

    lines.append(f"wait {args.startup_wait}")
    for map_index, map_name in enumerate(maps, start=1):
        safe_map = sanitize(map_name)
        shot_name = f"{run_id}-map{map_index}-{safe_map}-vulkan"
        lines.append(f"map {map_name}")
        lines.append(f"wait {args.map_wait}")
        lines.append("vkinfo")
        if not args.no_perf_samples:
            lines.append('set r_speeds "7"')
            lines.append(f"wait {args.perf_sample_wait}")
        lines.append(f"screenshotPNG {shot_name}")
        lines.append(f"wait {args.screenshot_wait}")
        if not args.no_perf_samples:
            lines.append('set r_speeds "0"')
            lines.append("wait 1")
        lines.append("vkinfo")
        lines.append("disconnect")
        lines.append("wait 30")
        screenshots.append(
            {
                "name": shot_name,
                "baselineKey": f"{args.profile}-map{map_index}-{safe_map}-vulkan",
                "renderer": "vulkan",
                "map": map_name,
                "mapIndex": map_index,
            }
        )

    lines.append("quit")
    lines.append("")
    return "\n".join(lines), screenshots


def build_dlight_shadow_cfg(
    args: argparse.Namespace,
    cvars: dict[str, str],
    scenes: list[dict[str, object]],
    run_id: str,
) -> tuple[str, list[dict[str, object]]]:
    lines = cfg_preamble(cvars, "Vulkan dynamic-light shadow scene sweep")
    screenshots: list[dict[str, object]] = []

    lines.append(f"wait {args.startup_wait}")
    for scene_index, scene in enumerate(scenes, start=1):
        scene_id = sanitize(str(scene["id"]))
        map_name = str(scene["map"])
        safe_map = sanitize(map_name)
        categories = [str(category) for category in scene.get("categories", [])]
        dlight = dict(scene["dlight"])  # type: ignore[arg-type]
        camera_path = [
            dict(step)
            for step in scene.get("csmCameraPath", [])  # type: ignore[union-attr]
            if isinstance(step, dict)
        ]
        shot_name = f"vkdl-{scene_index:02d}-{scene_id[:24].rstrip('._-')}-{safe_map}"
        for name, value in sorted(dict(scene.get("cvars", {})).items()):
            lines.append(f"set {name} {q3_quote(value)}")
        lines.append(f"echo DLIGHT_SHADOW_SCENE_BEGIN {scene_id}")
        lines.append(f"devmap {map_name}")
        lines.append(f"wait {args.map_wait}")
        if scene.get("setviewpos"):
            lines.append(str(scene["setviewpos"]))
            lines.append("wait 4")
        lines.append("vkinfo")
        lines.append(format_dlight_test_command(dlight))
        lines.append("wait 4")
        if camera_path:
            for step_index, step in enumerate(camera_path, start=1):
                step_id = sanitize(str(step["id"]))
                step_shot_name = (
                    f"vkdl-{scene_index:02d}-{scene_id[:18].rstrip('._-')}-"
                    f"{step_index:02d}-{step_id[:14].rstrip('._-')}-{safe_map}"
                )
                lines.append(f"echo CSM_SHIMMER_STEP_BEGIN {scene_id} {step_id}")
                lines.append(str(step["setviewpos"]))
                lines.append("wait 4")
                if not args.no_perf_samples:
                    lines.append('set r_speeds "4"')
                    lines.append(f"wait {args.perf_sample_wait}")
                lines.append(f"screenshotPNG {step_shot_name}")
                lines.append(f"wait {args.screenshot_wait}")
                if not args.no_perf_samples:
                    lines.append('set r_speeds "0"')
                    lines.append("wait 1")
                lines.append(f"echo CSM_SHIMMER_STEP_END {scene_id} {step_id}")
                screenshots.append(
                    {
                        "name": step_shot_name,
                        "baselineKey": (
                            f"{args.profile}-dlight-shadows-{scene_id}-"
                            f"{step_id}-{safe_map}-vulkan"
                        ),
                        "renderer": "vulkan",
                        "map": map_name,
                        "mapIndex": scene_index,
                        "scene": scene_id,
                        "description": scene.get("description", ""),
                        "evidenceCategories": categories,
                        "shadowScene": True,
                        "dlightTest": dlight,
                        "csmCameraPath": True,
                        "csmPathStep": step_id,
                        "csmPathIndex": step_index,
                        "csmSetviewpos": str(step["setviewpos"]),
                        "csmOriginDelta": str(step.get("originDelta", "")),
                        "csmAxisDelta": str(step.get("axisDelta", "")),
                    }
                )
        else:
            if not args.no_perf_samples:
                lines.append('set r_speeds "4"')
                lines.append(f"wait {args.perf_sample_wait}")
            lines.append(f"screenshotPNG {shot_name}")
            lines.append(f"wait {args.screenshot_wait}")
            if not args.no_perf_samples:
                lines.append('set r_speeds "0"')
                lines.append("wait 1")
            screenshots.append(
                {
                    "name": shot_name,
                    "baselineKey": f"{args.profile}-dlight-shadows-{scene_id}-{safe_map}-vulkan",
                    "renderer": "vulkan",
                    "map": map_name,
                    "mapIndex": scene_index,
                    "scene": scene_id,
                    "description": scene.get("description", ""),
                    "evidenceCategories": categories,
                    "shadowScene": True,
                    "dlightTest": dlight,
                }
            )
        lines.append("r_dlightTest off")
        lines.append("wait 1")
        lines.append("vkinfo")
        lines.append(f"echo DLIGHT_SHADOW_SCENE_END {scene_id}")
        lines.append("disconnect")
        lines.append("wait 30")

    lines.append("quit")
    lines.append("")
    return "\n".join(lines), screenshots


def build_demo_cfg(args: argparse.Namespace, cvars: dict[str, str], demo: str) -> str:
    lines = cfg_preamble(cvars, f"Vulkan timedemo sweep for {demo}")
    lines.extend(
        [
            f"wait {args.startup_wait}",
            "vkinfo",
            'set timedemo "1"',
            'set nextdemo "quit"',
            f"demo {demo}",
            "",
        ]
    )
    return "\n".join(lines)


def write_cfg(homepath: Path, fs_game: str, cfg_name: str, contents: str) -> Path:
    cfg_dir = homepath / game_dir(fs_game)
    cfg_dir.mkdir(parents=True, exist_ok=True)
    cfg_path = cfg_dir / cfg_name
    cfg_path.write_text(contents, encoding="utf-8", newline="\n")
    return cfg_path


def resolve_exe(path: Path | None, allow_missing: bool) -> Path:
    if path is None:
        if allow_missing:
            return (ROOT / ".tmp" / "vk-runtime-sweeps" / "fnql").resolve()
        raise ValueError("--exe is required unless --dry-run is used")
    resolved = path.resolve()
    if not allow_missing and not resolved.exists():
        raise FileNotFoundError(f"Executable does not exist: {resolved}")
    return resolved


def base_launch_args(
    exe: Path,
    basepath: Path,
    homepath: Path,
    fs_game: str,
    cfg_name: str,
    startup_cvars: dict[str, str],
) -> list[str]:
    command = [
        str(exe),
        "+set",
        "fs_homepath",
        q3_path(homepath),
        "+set",
        "fs_basepath",
        q3_path(basepath),
        "+set",
        "cl_renderer",
        "vulkan",
    ]

    for name in sorted(startup_cvars):
        command.extend(["+set", name, startup_cvars[name]])

    if fs_game:
        command.extend(["+set", "fs_game", fs_game])

    command.extend(["+exec", cfg_name])
    return command


def run_engine(
    command: list[str],
    cwd: Path,
    timeout: float,
    log_path: Path,
    dry_run: bool,
) -> dict[str, object]:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    printable = command_to_string(command)

    if dry_run:
        log_path.write_text(f"DRY RUN\n{printable}\n", encoding="utf-8")
        return {
            "status": "planned",
            "returncode": None,
            "command": command,
            "commandLine": printable,
            "log": str(log_path),
        }

    try:
        completed = subprocess.run(
            command,
            cwd=str(cwd),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
            check=False,
        )
        log_path.write_text(completed.stdout or "", encoding="utf-8")
        return {
            "status": "passed" if completed.returncode == 0 else "failed",
            "returncode": completed.returncode,
            "command": command,
            "commandLine": printable,
            "log": str(log_path),
        }
    except subprocess.TimeoutExpired as exc:
        output = exc.stdout or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
        log_path.write_text(
            output + f"\nTIMEOUT after {timeout:.1f} seconds\n",
            encoding="utf-8",
        )
        return {
            "status": "timed_out",
            "returncode": None,
            "command": command,
            "commandLine": printable,
            "log": str(log_path),
        }


def png_unfilter(filter_type: int, row: bytes, previous: bytes, bpp: int) -> bytes:
    if filter_type == 0:
        return row

    out = bytearray(row)
    for i, value in enumerate(row):
        left = out[i - bpp] if i >= bpp else 0
        up = previous[i] if previous else 0
        up_left = previous[i - bpp] if previous and i >= bpp else 0

        if filter_type == 1:
            predictor = left
        elif filter_type == 2:
            predictor = up
        elif filter_type == 3:
            predictor = (left + up) // 2
        elif filter_type == 4:
            p = left + up - up_left
            pa = abs(p - left)
            pb = abs(p - up)
            pc = abs(p - up_left)
            if pa <= pb and pa <= pc:
                predictor = left
            elif pb <= pc:
                predictor = up
            else:
                predictor = up_left
        else:
            raise ValueError(f"Unsupported PNG filter type {filter_type}.")

        out[i] = (value + predictor) & 0xFF
    return bytes(out)


def read_png_rgba(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError(f"{path} is not a PNG file.")

    offset = len(PNG_SIGNATURE)
    width = height = bit_depth = color_type = interlace = None
    palette: bytes | None = None
    transparency: bytes | None = None
    compressed = bytearray()

    while offset + 12 <= len(data):
        length = struct.unpack(">I", data[offset:offset + 4])[0]
        chunk_type = data[offset + 4:offset + 8]
        chunk_data = data[offset + 8:offset + 8 + length]
        offset += 12 + length

        if chunk_type == b"IHDR":
            (
                width,
                height,
                bit_depth,
                color_type,
                _compression,
                _filter_method,
                interlace,
            ) = struct.unpack(">IIBBBBB", chunk_data)
        elif chunk_type == b"PLTE":
            palette = chunk_data
        elif chunk_type == b"tRNS":
            transparency = chunk_data
        elif chunk_type == b"IDAT":
            compressed.extend(chunk_data)
        elif chunk_type == b"IEND":
            break

    if width is None or height is None or bit_depth is None or color_type is None:
        raise ValueError(f"{path} is missing a PNG IHDR chunk.")
    if bit_depth != 8:
        raise ValueError(f"{path} uses unsupported PNG bit depth {bit_depth}.")
    if interlace:
        raise ValueError(f"{path} uses unsupported interlaced PNG encoding.")

    channels_by_type = {
        0: 1,
        2: 3,
        3: 1,
        4: 2,
        6: 4,
    }
    channels = channels_by_type.get(color_type)
    if channels is None:
        raise ValueError(f"{path} uses unsupported PNG color type {color_type}.")

    raw = zlib.decompress(bytes(compressed))
    stride = width * channels
    source_rows: list[bytes] = []
    previous = b""
    source_offset = 0
    for _row_index in range(height):
        if source_offset >= len(raw):
            raise ValueError(f"{path} ended before all PNG rows were decoded.")
        filter_type = raw[source_offset]
        source_offset += 1
        row = raw[source_offset:source_offset + stride]
        source_offset += stride
        if len(row) != stride:
            raise ValueError(f"{path} has a truncated PNG row.")
        decoded = png_unfilter(filter_type, row, previous, channels)
        source_rows.append(decoded)
        previous = decoded

    pixels = bytearray(width * height * 4)
    out = 0
    for row in source_rows:
        if color_type == 0:
            for gray in row:
                pixels[out:out + 4] = bytes((gray, gray, gray, 255))
                out += 4
        elif color_type == 2:
            for i in range(0, len(row), 3):
                pixels[out:out + 4] = row[i:i + 3] + b"\xff"
                out += 4
        elif color_type == 3:
            if palette is None:
                raise ValueError(f"{path} is an indexed PNG without a palette.")
            for index in row:
                palette_offset = index * 3
                if palette_offset + 3 > len(palette):
                    raise ValueError(f"{path} references palette index {index} out of range.")
                alpha = transparency[index] if transparency and index < len(transparency) else 255
                pixels[out:out + 4] = palette[palette_offset:palette_offset + 3] + bytes((alpha,))
                out += 4
        elif color_type == 4:
            for i in range(0, len(row), 2):
                gray = row[i]
                alpha = row[i + 1]
                pixels[out:out + 4] = bytes((gray, gray, gray, alpha))
                out += 4
        elif color_type == 6:
            pixels[out:out + len(row)] = row
            out += len(row)

    return width, height, bytes(pixels)


def png_filter_none_rows(width: int, height: int, pixels: bytes) -> bytes:
    stride = width * 4
    rows = bytearray()
    for row_index in range(height):
        start = row_index * stride
        rows.append(0)
        rows.extend(pixels[start:start + stride])
    return bytes(rows)


def write_png_rgba(path: Path, width: int, height: int, pixels: bytes) -> None:
    if width <= 0 or height <= 0:
        raise ValueError("PNG dimensions must be positive.")
    if len(pixels) != width * height * 4:
        raise ValueError("RGBA pixel data does not match the requested PNG dimensions.")

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload)) +
            kind +
            payload +
            struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
        )

    path.parent.mkdir(parents=True, exist_ok=True)
    raw = png_filter_none_rows(width, height, pixels)
    encoded = bytearray(PNG_SIGNATURE)
    encoded.extend(
        chunk(
            b"IHDR",
            struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0),
        )
    )
    encoded.extend(chunk(b"IDAT", zlib.compress(raw)))
    encoded.extend(chunk(b"IEND", b""))
    path.write_bytes(bytes(encoded))


def compare_rgba_pixels(
    width: int,
    height: int,
    baseline_pixels: bytes,
    candidate_pixels: bytes,
) -> tuple[dict[str, object], bytes]:
    if len(baseline_pixels) != len(candidate_pixels):
        raise ValueError("PNG pixel buffers have different lengths.")

    diff_pixels = bytearray(width * height * 4)
    squared_error = 0
    absolute_error = 0
    max_delta = 0
    changed_pixels = 0
    pixel_count = width * height
    channel_count = pixel_count * 3

    for pixel_index in range(pixel_count):
        base_offset = pixel_index * 4
        pixel_changed = False
        for channel in range(3):
            delta = abs(
                baseline_pixels[base_offset + channel] -
                candidate_pixels[base_offset + channel]
            )
            squared_error += delta * delta
            absolute_error += delta
            max_delta = max(max_delta, delta)
            diff_pixels[base_offset + channel] = min(255, delta * 4)
            if delta:
                pixel_changed = True
        diff_pixels[base_offset + 3] = 255
        if pixel_changed:
            changed_pixels += 1

    rms = (squared_error / channel_count) ** 0.5 if channel_count else 0.0
    mean_absolute = absolute_error / channel_count if channel_count else 0.0
    changed_ratio = changed_pixels / pixel_count if pixel_count else 0.0
    metrics = {
        "width": width,
        "height": height,
        "pixels": pixel_count,
        "changedPixels": changed_pixels,
        "changedPixelRatio": changed_ratio,
        "rms": rms,
        "meanAbsolute": mean_absolute,
        "maxDelta": max_delta,
    }
    return metrics, bytes(diff_pixels)


def compare_png_files(
    baseline_path: Path,
    candidate_path: Path,
    max_rms: float,
    max_pixel_ratio: float,
    diff_path: Path | None = None,
) -> dict[str, object]:
    base_width, base_height, base_pixels = read_png_rgba(baseline_path)
    candidate_width, candidate_height, candidate_pixels = read_png_rgba(candidate_path)

    if (base_width, base_height) != (candidate_width, candidate_height):
        return {
            "status": "failed",
            "reason": "size-mismatch",
            "baselineWidth": base_width,
            "baselineHeight": base_height,
            "candidateWidth": candidate_width,
            "candidateHeight": candidate_height,
        }

    metrics, diff_pixels = compare_rgba_pixels(
        base_width,
        base_height,
        base_pixels,
        candidate_pixels,
    )
    passed = (
        float(metrics["rms"]) <= max_rms and
        float(metrics["changedPixelRatio"]) <= max_pixel_ratio
    )

    if diff_path:
        write_png_rgba(diff_path, base_width, base_height, diff_pixels)

    metrics.update(
        {
            "status": "passed" if passed else "failed",
            "baselinePath": str(baseline_path),
            "candidatePath": str(candidate_path),
            "diffPath": str(diff_path) if diff_path else "",
            "maxRms": max_rms,
            "maxChangedPixelRatio": max_pixel_ratio,
        }
    )
    return metrics


def screenshot_results(homepath: Path, fs_game: str, expected: list[dict[str, object]]) -> list[dict[str, object]]:
    screenshot_dir = homepath / game_dir(fs_game) / "screenshots"
    results: list[dict[str, object]] = []
    for item in expected:
        name = str(item["name"])
        path = screenshot_dir / f"{name}.png"
        result = dict(item)
        result.update(
            {
                "path": str(path),
                "found": path.exists(),
                "size": path.stat().st_size if path.exists() else 0,
            }
        )
        results.append(result)
    return results


def csm_shimmer_path_screenshots(
    screenshots: list[dict[str, object]],
) -> list[dict[str, object]]:
    return [
        shot
        for shot in screenshots
        if shot.get("csmCameraPath")
        and str(shot.get("scene", "")) in CSM_SHIMMER_EVIDENCE_CATEGORIES
    ]


def csm_shimmer_screenshot_summary_active(summary: object) -> bool:
    return (
        isinstance(summary, dict)
        and bool(summary.get("found"))
        and str(summary.get("status", "")).lower() == "passed"
    )


def csm_shimmer_screenshot_summary(
    screenshots: list[dict[str, object]],
) -> dict[str, object]:
    shimmer_shots = csm_shimmer_path_screenshots(screenshots)
    thresholds = {
        "maxRms": CSM_SHIMMER_SCREENSHOT_MAX_RMS,
        "maxChangedPixelRatio": CSM_SHIMMER_SCREENSHOT_MAX_CHANGED_PIXEL_RATIO,
    }
    if not shimmer_shots:
        return {
            "found": False,
            "status": "missing",
            "scene": "csm-shimmer-path",
            "baselineStep": CSM_SHIMMER_BASELINE_STEP,
            "sampleCount": 0,
            "comparisonCount": 0,
            "passedComparisons": 0,
            "maxRms": 0.0,
            "maxChangedPixelRatio": 0.0,
            "thresholds": thresholds,
            "comparisons": [],
            "failures": [],
        }

    failures: list[str] = []
    comparisons: list[dict[str, object]] = []
    expected_steps = {str(step["id"]) for step in CSM_SHIMMER_CAMERA_PATH}
    seen_steps = {str(shot.get("csmPathStep", "")) for shot in shimmer_shots}
    missing_steps = sorted(expected_steps - seen_steps)
    if missing_steps:
        failures.append(
            "CSM shimmer screenshot smoke is missing path step"
            f"{'s' if len(missing_steps) != 1 else ''}: "
            + ", ".join(missing_steps)
            + "."
        )
    baseline_shots = [
        shot for shot in shimmer_shots
        if str(shot.get("csmPathStep", "")) == CSM_SHIMMER_BASELINE_STEP
    ]
    if not baseline_shots:
        failures.append("CSM shimmer screenshot smoke did not capture a baseline step.")
    elif not baseline_shots[0].get("found"):
        failures.append(
            "CSM shimmer screenshot smoke baseline is missing: "
            f"{baseline_shots[0].get('name', 'baseline')}."
        )

    for shot in shimmer_shots:
        if str(shot.get("csmPathStep", "")) == CSM_SHIMMER_BASELINE_STEP:
            continue
        name = str(shot.get("name", "screenshot"))
        step = str(shot.get("csmPathStep", ""))
        if not shot.get("found"):
            failures.append(f"CSM shimmer screenshot smoke image is missing: {name}.")
            continue
        comparison = shot.get("csmShimmerComparison")
        if not isinstance(comparison, dict):
            failures.append(
                "CSM shimmer screenshot smoke did not compare step "
                f"{step or name} against the baseline."
            )
            continue
        status = str(comparison.get("status", "failed"))
        rms = float(comparison.get("rms", 0.0)) if isinstance(comparison.get("rms"), (int, float)) else 0.0
        changed_ratio = (
            float(comparison.get("changedPixelRatio", 0.0))
            if isinstance(comparison.get("changedPixelRatio"), (int, float))
            else 0.0
        )
        record = {
            "name": name,
            "step": step,
            "baselineStep": str(shot.get("csmShimmerBaselineStep", CSM_SHIMMER_BASELINE_STEP)),
            "status": status,
            "rms": rms,
            "changedPixelRatio": changed_ratio,
            "reason": str(comparison.get("reason", "")),
        }
        comparisons.append(record)
        if status != "passed":
            reason = str(comparison.get("reason", "diff-threshold")) or "diff-threshold"
            failures.append(
                f"CSM shimmer screenshot smoke comparison failed for {step or name}: {reason}."
            )

    if not comparisons:
        failures.append("CSM shimmer screenshot smoke did not produce any baseline comparisons.")

    passed = sum(1 for comparison in comparisons if comparison.get("status") == "passed")
    max_rms = max((float(comparison.get("rms", 0.0)) for comparison in comparisons), default=0.0)
    max_changed_ratio = max(
        (float(comparison.get("changedPixelRatio", 0.0)) for comparison in comparisons),
        default=0.0,
    )

    return {
        "found": True,
        "status": "failed" if failures else "passed",
        "scene": "csm-shimmer-path",
        "baselineStep": CSM_SHIMMER_BASELINE_STEP,
        "sampleCount": len(shimmer_shots),
        "comparisonCount": len(comparisons),
        "passedComparisons": passed,
        "maxRms": max_rms,
        "maxChangedPixelRatio": max_changed_ratio,
        "thresholds": thresholds,
        "comparisons": comparisons,
        "failures": list(dict.fromkeys(failures)),
    }


def apply_csm_shimmer_screenshot_diffs(
    screenshots: list[dict[str, object]],
    diff_dir: Path | None,
    max_rms: float = CSM_SHIMMER_SCREENSHOT_MAX_RMS,
    max_pixel_ratio: float = CSM_SHIMMER_SCREENSHOT_MAX_CHANGED_PIXEL_RATIO,
) -> dict[str, object]:
    shimmer_shots = csm_shimmer_path_screenshots(screenshots)
    baseline_shot = next(
        (
            shot for shot in shimmer_shots
            if str(shot.get("csmPathStep", "")) == CSM_SHIMMER_BASELINE_STEP
        ),
        None,
    )
    if baseline_shot is None:
        return csm_shimmer_screenshot_summary(screenshots)

    baseline_shot["csmShimmerRole"] = "baseline"
    baseline_path = Path(str(baseline_shot.get("path", "")))
    baseline_key = str(baseline_shot.get("baselineKey") or baseline_shot.get("name") or "baseline")

    for shot in shimmer_shots:
        if shot is baseline_shot:
            continue
        shot["csmShimmerRole"] = "candidate"
        shot["csmShimmerBaselineStep"] = CSM_SHIMMER_BASELINE_STEP
        shot["csmShimmerBaselineKey"] = baseline_key
        shot["csmShimmerBaselinePath"] = str(baseline_path)

        candidate_path = Path(str(shot.get("path", "")))
        if not baseline_shot.get("found") or not shot.get("found"):
            continue

        candidate_key = str(shot.get("baselineKey") or shot.get("name") or "candidate")
        comparison_diff_path = (
            diff_dir.resolve() / f"{candidate_key}.csm-shimmer.diff.png"
            if diff_dir is not None
            else None
        )
        try:
            comparison = compare_png_files(
                baseline_path,
                candidate_path,
                max_rms,
                max_pixel_ratio,
                comparison_diff_path,
            )
        except Exception as exc:
            comparison = {
                "status": "failed",
                "reason": str(exc),
                "baselinePath": str(baseline_path),
                "candidatePath": str(candidate_path),
                "diffPath": str(comparison_diff_path) if comparison_diff_path else "",
                "maxRms": max_rms,
                "maxChangedPixelRatio": max_pixel_ratio,
            }
        shot["csmShimmerComparison"] = comparison

    return csm_shimmer_screenshot_summary(screenshots)


def timedemo_metrics(log_path: Path) -> dict[str, object] | None:
    if not log_path.exists():
        return None
    text = log_path.read_text(encoding="utf-8", errors="replace")
    matches = list(TIMEDEMO_FPS_RE.finditer(text))
    if not matches:
        return None
    match = matches[-1]
    return {
        "frames": int(match.group("frames")),
        "seconds": float(match.group("seconds")),
        "fps": float(match.group("fps")),
    }


def summarize_int_samples(samples: list[dict[str, int]]) -> dict[str, object]:
    if not samples:
        return {
            "found": False,
            "sampleCount": 0,
            "latest": {},
            "max": {},
        }

    return {
        "found": True,
        "sampleCount": len(samples),
        "latest": samples[-1],
        "max": {
            name: max(sample[name] for sample in samples)
            for name in samples[0]
        },
    }


def shadow_manager_sample(match: re.Match[str]) -> dict[str, int]:
    sample: dict[str, int] = {}
    for name, value in match.groupdict().items():
        if value is None:
            sample[name] = 0
        else:
            sample[name] = int(value, 16) if name == "scheduledMask" else int(value)
    return sample


def shadow_atlas_contract_sample(match: re.Match[str]) -> dict[str, int]:
    backend = match.group("shadowAtlasBackend").lower()
    atlas = match.group("shadowAtlasName").lower()
    sampler = match.group("shadowAtlasSampler").lower()
    allocation = match.group("shadowAtlasAllocation").lower()

    return {
        "shadowAtlasSamples": 1,
        "shadowAtlasBackendGlx": 1 if backend == "glx" else 0,
        "shadowAtlasBackendVulkan": 1 if backend == "vulkan" else 0,
        "shadowAtlasPoint": 1 if atlas == "point" else 0,
        "shadowAtlasSpot": 1 if atlas == "spot" else 0,
        "shadowAtlasCsm": 1 if atlas == "csm" else 0,
        "shadowAtlasActive": int(match.group("shadowAtlasActive")),
        "shadowAtlasTileSize": int(match.group("shadowAtlasTileSize")),
        "shadowAtlasWidth": int(match.group("shadowAtlasWidth")),
        "shadowAtlasHeight": int(match.group("shadowAtlasHeight")),
        "shadowAtlasRecords": int(match.group("shadowAtlasRecords")),
        "shadowAtlasFill": int(match.group("shadowAtlasFill")),
        "shadowAtlasPadTexels": int(match.group("shadowAtlasPadTexels")),
        "shadowAtlasClampTexels": int(match.group("shadowAtlasClampTexels")),
        "shadowAtlasSamplerClampEdge": 1 if sampler == "clamp-edge" else 0,
        "shadowAtlasAllocationPriorityDlightIndex": (
            1 if allocation == "priority-dlight-index" else 0
        ),
        "shadowAtlasAllocationPrioritySourceIndex": (
            1 if allocation == "priority-source-index" else 0
        ),
        "shadowAtlasAllocationCascadeIndex": 1 if allocation == "cascade-index" else 0,
        "shadowAtlasDeterministic": int(match.group("shadowAtlasDeterministic")),
    }


def shadow_atlas_contract_summary(samples: list[dict[str, int]]) -> dict[str, object]:
    if not samples:
        return {
            "found": False,
            "status": "missing",
            "sampleCount": 0,
            "atlasCoverage": {"point": False, "spot": False, "csm": False},
            "activeAtlasCoverage": {"point": False, "spot": False, "csm": False},
            "latest": {},
            "max": {},
            "failures": [],
        }

    summary = summarize_int_samples(samples)
    failures: list[str] = []
    atlas_fields = {
        "point": ("shadowAtlasPoint", "shadowAtlasAllocationPriorityDlightIndex"),
        "spot": ("shadowAtlasSpot", "shadowAtlasAllocationPrioritySourceIndex"),
        "csm": ("shadowAtlasCsm", "shadowAtlasAllocationCascadeIndex"),
    }
    atlas_coverage = {
        name: any(sample.get(field, 0) > 0 for sample in samples)
        for name, (field, _allocation_field) in atlas_fields.items()
    }
    active_atlas_coverage = {
        name: any(
            sample.get(field, 0) > 0 and sample.get("shadowAtlasActive", 0) > 0
            for sample in samples
        )
        for name, (field, _allocation_field) in atlas_fields.items()
    }

    for name, (field, allocation_field) in atlas_fields.items():
        for sample in samples:
            if sample.get(field, 0) <= 0:
                continue
            if sample.get("shadowAtlasClampTexels", 0) < sample.get("shadowAtlasPadTexels", 0):
                failures.append(f"{name} atlas clamp is smaller than its filter pad.")
            if sample.get("shadowAtlasSamplerClampEdge", 0) <= 0:
                failures.append(f"{name} atlas sampler is not clamp-edge.")
            if sample.get(allocation_field, 0) <= 0:
                failures.append(f"{name} atlas allocation policy is unexpected.")
            if sample.get("shadowAtlasDeterministic", 0) <= 0:
                failures.append(f"{name} atlas did not report deterministic allocation.")
            if sample.get("shadowAtlasActive", 0) <= 0:
                continue
            if sample.get("shadowAtlasWidth", 0) <= 0 or sample.get("shadowAtlasHeight", 0) <= 0:
                failures.append(f"{name} atlas active sample reported no dimensions.")
            if sample.get("shadowAtlasTileSize", 0) <= 0:
                failures.append(f"{name} atlas active sample reported no tile size.")
            if sample.get("shadowAtlasRecords", 0) <= 0:
                failures.append(f"{name} atlas active sample reported no records.")
            fill = sample.get("shadowAtlasFill", 0)
            if fill <= 0 or fill > 100:
                failures.append(f"{name} atlas pressure is outside 1..100 percent.")

    return {
        "found": True,
        "status": "failed" if failures else "passed",
        "sampleCount": summary["sampleCount"],
        "atlasCoverage": atlas_coverage,
        "activeAtlasCoverage": active_atlas_coverage,
        "latest": summary["latest"],
        "max": summary["max"],
        "failures": list(dict.fromkeys(failures)),
    }


def csm_shadow_sample(match: re.Match[str]) -> dict[str, int]:
    sample: dict[str, int] = {}
    snap_depth_present = False
    for name, value in match.groupdict().items():
        if name == "csmSky":
            continue
        if value is None:
            if name.startswith("csmSnapDepth"):
                sample[f"{name}Milli"] = 0
            else:
                sample[name] = 0
            continue
        if name.startswith("csmSnapDepth"):
            sample[f"{name}Milli"] = int(round(float(value) * 1000.0))
            snap_depth_present = True
        else:
            sample[name] = int(value)
    sample["csmSnapDepthSamples"] = 1 if snap_depth_present else 0
    return sample


def csm_cascade_sample(match: re.Match[str]) -> dict[str, int]:
    def milli(name: str) -> int:
        return int(round(float(match.group(name)) * 1000.0))

    backend = match.group("csmCascadeBackend").lower()
    sample_y = match.group("csmCascadeSampleY").lower()
    clip_y = match.group("csmCascadeClipY").lower()
    depth = match.group("csmCascadeDepth").lower()
    ndc = match.group("csmCascadeNdc")
    compare = match.group("csmCascadeCompare").lower()

    sample = {
        "csmCascadeSamples": 1,
        "csmCascadeBackendGlx": 1 if backend == "glx" else 0,
        "csmCascadeBackendVulkan": 1 if backend == "vulkan" else 0,
        "csmCascadeIndex": int(match.group("csmCascadeIndex")),
        "csmCascadeSplitNearMilli": milli("csmCascadeSplitNear"),
        "csmCascadeSplitFarMilli": milli("csmCascadeSplitFar"),
        "csmCascadeAtlasX": int(match.group("csmCascadeAtlasX")),
        "csmCascadeAtlasY": int(match.group("csmCascadeAtlasY")),
        "csmCascadeAtlasSize": int(match.group("csmCascadeAtlasSize")),
        "csmCascadeViewX": int(match.group("csmCascadeViewX")),
        "csmCascadeViewY": int(match.group("csmCascadeViewY")),
        "csmCascadeViewWidth": int(match.group("csmCascadeViewWidth")),
        "csmCascadeViewHeight": int(match.group("csmCascadeViewHeight")),
        "csmCascadeApiX": int(match.group("csmCascadeApiX")),
        "csmCascadeApiY": int(match.group("csmCascadeApiY")),
        "csmCascadeApiWidth": int(match.group("csmCascadeApiWidth")),
        "csmCascadeApiHeight": int(match.group("csmCascadeApiHeight")),
        "csmCascadeSampleYInverted": 1 if sample_y == "inverted" else 0,
        "csmCascadeClipYFlipped": 1 if clip_y == "flipped" else 0,
        "csmCascadeDepthForward": 1 if depth == "forward" else 0,
        "csmCascadeNdcZeroToOne": 1 if ndc == "0..1" else 0,
        "csmCascadeClearMilli": milli("csmCascadeClear"),
        "csmCascadeCompareLequal": 1 if compare == "lequal" else 0,
        "csmCascadeBoundMinXMilli": milli("csmCascadeBoundMinX"),
        "csmCascadeBoundMaxXMilli": milli("csmCascadeBoundMaxX"),
        "csmCascadeBoundMinYMilli": milli("csmCascadeBoundMinY"),
        "csmCascadeBoundMaxYMilli": milli("csmCascadeBoundMaxY"),
        "csmCascadeBoundMinZMilli": milli("csmCascadeBoundMinZ"),
        "csmCascadeBoundMaxZMilli": milli("csmCascadeBoundMaxZ"),
        "csmCascadeOriginXMilli": milli("csmCascadeOriginX"),
        "csmCascadeOriginYMilli": milli("csmCascadeOriginY"),
        "csmCascadeOriginZMilli": milli("csmCascadeOriginZ"),
        "csmCascadeTexelMilli": milli("csmCascadeTexel"),
    }
    return sample


def csm_cascade_contract_summary(samples: list[dict[str, int]]) -> dict[str, object]:
    summary = summarize_int_samples(samples)
    failures: list[str] = []
    maximum = summary["max"] if isinstance(summary.get("max"), dict) else {}

    if not samples:
        return {
            "found": False,
            "status": "missing",
            "sampleCount": 0,
            "latest": {},
            "max": {},
            "failures": [],
        }

    if int_metric(maximum.get("csmCascadeAtlasSize", 0)) <= 0:
        failures.append("CSM cascade contract reported no atlas tile size.")
    if int_metric(maximum.get("csmCascadeViewWidth", 0)) <= 0 or int_metric(maximum.get("csmCascadeViewHeight", 0)) <= 0:
        failures.append("CSM cascade contract reported no renderer viewport dimensions.")
    if int_metric(maximum.get("csmCascadeApiWidth", 0)) <= 0 or int_metric(maximum.get("csmCascadeApiHeight", 0)) <= 0:
        failures.append("CSM cascade contract reported no API viewport dimensions.")
    if int_metric(maximum.get("csmCascadeDepthForward", 0)) <= 0 or int_metric(maximum.get("csmCascadeCompareLequal", 0)) <= 0:
        failures.append("CSM cascade contract did not report forward lequal depth.")
    if int_metric(maximum.get("csmCascadeTexelMilli", 0)) <= 0:
        failures.append("CSM cascade contract reported no texel size.")

    return {
        "found": True,
        "status": "failed" if failures else "passed",
        "sampleCount": summary["sampleCount"],
        "latest": summary["latest"],
        "max": maximum,
        "failures": list(dict.fromkeys(failures)),
    }


def shadow_profile_summary(
    dlight_samples: list[dict[str, int]],
    manager_samples: list[dict[str, int]],
    shadow_atlas_samples: list[dict[str, int]],
    surface_spot_samples: list[dict[str, int]],
    csm_samples: list[dict[str, int]],
    csm_cascade_samples: list[dict[str, int]],
) -> dict[str, object]:
    total_samples = (
        len(dlight_samples)
        + len(manager_samples)
        + len(shadow_atlas_samples)
        + len(surface_spot_samples)
        + len(csm_samples)
        + len(csm_cascade_samples)
    )
    empty_buckets = {
        "rasterWork": {},
        "samplerPressure": {},
        "orchestrationPressure": {},
        "cpuTiming": {},
    }
    if total_samples <= 0:
        return {
            "found": False,
            "status": "missing",
            "profileReady": False,
            "sampleCount": 0,
            "sampleCounts": {
                "dlight": 0,
                "manager": 0,
                "atlas": 0,
                "surfaceSpot": 0,
                "csm": 0,
                "csmCascade": 0,
            },
            "costBuckets": empty_buckets,
            "failures": [],
        }

    dlight_max = summarize_int_samples(dlight_samples)["max"] if dlight_samples else {}
    manager_max = summarize_int_samples(manager_samples)["max"] if manager_samples else {}
    atlas_summary = shadow_atlas_contract_summary(shadow_atlas_samples)
    atlas_max = atlas_summary.get("max", {}) if isinstance(atlas_summary, dict) else {}
    surface_spot_max = (
        summarize_int_samples(surface_spot_samples)["max"] if surface_spot_samples else {}
    )
    csm_runtime = csm_shadow_runtime_summary(manager_samples, csm_samples)
    csm_max = summarize_int_samples(csm_samples)["max"] if csm_samples else {}
    cascade_contract = csm_cascade_contract_summary(csm_cascade_samples)

    def field(maximum: object, name: str) -> int:
        return int_sample_field(maximum, name) if isinstance(maximum, dict) else 0

    def atlas_field(atlas_name: str, field_name: str) -> int:
        atlas_flag = f"shadowAtlas{atlas_name}"
        return max(
            (
                int_sample_field(sample, field_name)
                for sample in shadow_atlas_samples
                if int_sample_field(sample, atlas_flag) > 0
            ),
            default=0,
        )

    point_work = (
        field(manager_max, "pointScheduled") > 0
        or field(manager_max, "pointPublished") > 0
        or field(manager_max, "pointRecords") > 0
        or field(dlight_max, "planned") > 0
        or field(dlight_max, "renderLights") > 0
    )
    spot_work = (
        field(manager_max, "spotScheduled") > 0
        or field(manager_max, "spotPublished") > 0
        or field(manager_max, "spotPlans") > 0
        or field(surface_spot_max, "surfaceSpotPlans") > 0
    )
    csm_work = (
        field(manager_max, "csmAtlasScheduled") > 0
        or field(manager_max, "csmReceiverScheduled") > 0
        or field(manager_max, "csmPublished") > 0
        or field(manager_max, "csmCascadeCount") > 0
        or field(csm_max, "csmDebugCascades") > 0
    )
    atlas_work = (
        point_work
        or spot_work
        or csm_work
        or field(atlas_max, "shadowAtlasActive") > 0
        or field(atlas_max, "shadowAtlasRecords") > 0
    )

    raster_work = {
        "pointRenderLights": field(dlight_max, "renderLights"),
        "pointFaces": field(dlight_max, "faces"),
        "pointBatches": field(dlight_max, "batches"),
        "pointDraws": field(dlight_max, "draws"),
        "pointSurfaces": field(dlight_max, "surfs"),
        "spotPlans": field(manager_max, "spotPlans"),
        "spotSurfacePlans": field(manager_max, "spotSurfacePlans"),
        "spotAllocated": field(surface_spot_max, "surfaceSpotAllocated"),
        "csmCascades": max(field(manager_max, "csmCascadeCount"), field(csm_max, "csmDebugCascades")),
        "csmCasterSurfaces": field(csm_max, "csmCasterSurfaces"),
        "csmReceiverWorldSurfaces": field(csm_max, "csmReceiverWorldSurfaces"),
        "csmReceiverEntitySurfaces": field(csm_max, "csmReceiverEntitySurfaces"),
        "csmReceiverSurfaces": (
            field(csm_max, "csmReceiverWorldSurfaces")
            + field(csm_max, "csmReceiverEntitySurfaces")
        ),
    }
    point_atlas_fill = max(field(manager_max, "pointAtlasFill"), atlas_field("Point", "shadowAtlasFill"))
    spot_atlas_fill = max(field(manager_max, "spotAtlasFill"), atlas_field("Spot", "shadowAtlasFill"))
    csm_atlas_fill = atlas_field("Csm", "shadowAtlasFill")
    sampler_pressure = {
        "pointAtlasFill": point_atlas_fill,
        "spotAtlasFill": spot_atlas_fill,
        "csmAtlasFill": csm_atlas_fill,
        "maxAtlasFill": max(point_atlas_fill, spot_atlas_fill, csm_atlas_fill),
        "maxAtlasRecords": max(
            field(manager_max, "pointRecords"),
            field(manager_max, "spotPlans"),
            atlas_field("Point", "shadowAtlasRecords"),
            atlas_field("Spot", "shadowAtlasRecords"),
            atlas_field("Csm", "shadowAtlasRecords"),
        ),
        "maxFilterPadTexels": field(atlas_max, "shadowAtlasPadTexels"),
        "maxClampTexels": field(atlas_max, "shadowAtlasClampTexels"),
        "samplerClampEdge": field(atlas_max, "shadowAtlasSamplerClampEdge"),
        "deterministicAtlas": field(atlas_max, "shadowAtlasDeterministic"),
    }
    orchestration_pressure = {
        "scheduledPasses": field(manager_max, "scheduledPasses"),
        "scheduledMask": field(manager_max, "scheduledMask"),
        "pointScheduled": field(manager_max, "pointScheduled"),
        "spotScheduled": field(manager_max, "spotScheduled"),
        "csmAtlasScheduled": field(manager_max, "csmAtlasScheduled"),
        "csmReceiverScheduled": field(manager_max, "csmReceiverScheduled"),
        "publishedAtlases": (
            field(manager_max, "pointPublished")
            + field(manager_max, "spotPublished")
            + field(manager_max, "csmPublished")
        ),
    }
    cpu_timing = {
        "pointCpuMsec": field(dlight_max, "cpuMsec"),
        "csmCpuMsec": field(csm_max, "csmCpuMsec"),
        "maxCpuMsec": max(field(dlight_max, "cpuMsec"), field(csm_max, "csmCpuMsec")),
        "timedPointSamples": len(dlight_samples),
        "timedCsmSamples": len(csm_samples),
    }

    failures: list[str] = []
    if not manager_samples:
        failures.append("Shadow profile is missing shadow manager scheduling samples.")
    if point_work and not dlight_samples:
        failures.append("Shadow profile is missing point shadow raster and CPU timing samples.")
    if csm_work and not csm_samples:
        failures.append("Shadow profile is missing CSM raster and CPU timing samples.")
    if not point_work and not spot_work and not csm_work:
        failures.append("Shadow profile found no scheduled shadow work to profile.")
    if atlas_work:
        if not shadow_atlas_samples:
            failures.append("Shadow profile is missing shadow atlas contract samples.")
        elif atlas_summary.get("status") != "passed":
            failures.append("Shadow profile is blocked by failing shadow atlas contract evidence.")
            failures.extend(str(failure) for failure in atlas_summary.get("failures", [])[:4])
    if csm_work:
        if csm_runtime.get("status") != "passed":
            failures.append("Shadow profile is blocked by failing CSM runtime evidence.")
            failures.extend(str(failure) for failure in csm_runtime.get("failures", [])[:4])
        if not csm_cascade_samples:
            failures.append("Shadow profile is missing CSM cascade contract samples.")
        elif cascade_contract.get("status") != "passed":
            failures.append("Shadow profile is blocked by failing CSM cascade contract evidence.")
            failures.extend(str(failure) for failure in cascade_contract.get("failures", [])[:4])

    return {
        "found": True,
        "status": "failed" if failures else "passed",
        "profileReady": not failures,
        "sampleCount": total_samples,
        "sampleCounts": {
            "dlight": len(dlight_samples),
            "manager": len(manager_samples),
            "atlas": len(shadow_atlas_samples),
            "surfaceSpot": len(surface_spot_samples),
            "csm": len(csm_samples),
            "csmCascade": len(csm_cascade_samples),
        },
        "costBuckets": {
            "rasterWork": raster_work,
            "samplerPressure": sampler_pressure,
            "orchestrationPressure": orchestration_pressure,
            "cpuTiming": cpu_timing,
        },
        "failures": list(dict.fromkeys(failures)),
    }


def csm_fallback_sample(match: re.Match[str]) -> dict[str, int]:
    reason = sanitize(match.group("csmFallbackReason"))
    sample = {
        "csmFallbackSamples": 1,
        "csmFallbackCascades": int(match.group("csmFallbackCascades")),
        "csmFallbackNoWorld": 0,
        "csmFallbackNoSun": 0,
        "csmFallbackAtlasUnavailable": 0,
        "csmFallbackZeroCascade": 0,
        "csmFallbackProjection": 0,
        "csmFallbackStrength": 0,
        "csmFallbackDisabled": 0,
    }
    if reason == "no-world":
        sample["csmFallbackNoWorld"] = 1
    elif reason == "no-sky-sun":
        sample["csmFallbackNoSun"] = 1
    elif reason == "atlas":
        sample["csmFallbackAtlasUnavailable"] = 1
    elif reason == "zero-cascade":
        sample["csmFallbackZeroCascade"] = 1
    elif reason == "projection":
        sample["csmFallbackProjection"] = 1
    elif reason == "strength":
        sample["csmFallbackStrength"] = 1
    elif reason == "disabled":
        sample["csmFallbackDisabled"] = 1
    return sample


def csm_fallback_summary_active(summary: object) -> bool:
    return (
        isinstance(summary, dict)
        and bool(summary.get("found"))
        and str(summary.get("status", "")).lower() == "passed"
    )


def csm_fallback_summary(
    manager_samples: list[dict[str, int]],
    fallback_samples: list[dict[str, int]],
    expected_reasons: tuple[str, ...] = (),
) -> dict[str, object]:
    manager_max = summarize_int_samples(manager_samples)["max"] if manager_samples else {}
    fallback_max = summarize_int_samples(fallback_samples)["max"] if fallback_samples else {}
    failures: list[str] = []

    def manager_field(name: str) -> int:
        return int_metric(manager_max.get(name, 0)) if isinstance(manager_max, dict) else 0

    def fallback_field(name: str) -> int:
        return int_metric(fallback_max.get(name, 0)) if isinstance(fallback_max, dict) else 0

    reason_fields = {
        "no-world": "csmFallbackNoWorld",
        "no-sky-sun": "csmFallbackNoSun",
        "atlas": "csmFallbackAtlasUnavailable",
        "zero-cascade": "csmFallbackZeroCascade",
    }

    if not manager_samples and not fallback_samples:
        return {
            "found": False,
            "status": "missing",
            "managerSampleCount": 0,
            "fallbackSampleCount": 0,
            "reasonCoverage": {reason: False for reason in expected_reasons},
            "max": {},
            "failures": [],
        }

    if not manager_samples:
        failures.append("CSM fallback smoke missing shadow manager samples.")
    if not fallback_samples:
        failures.append("CSM fallback smoke missing CSM skip reason samples.")

    if manager_samples:
        blocked_fields = (
            "csmAtlasScheduled",
            "csmReceiverScheduled",
            "csmPublished",
            "csmCascadeCount",
            "csmAtlasWidth",
            "csmAtlasHeight",
            "csmGeneration",
        )
        for name in blocked_fields:
            if manager_field(name) > 0:
                failures.append(
                    "CSM fallback smoke published or scheduled invalid CSM work: "
                    f"{name}={manager_field(name)}."
                )
    if fallback_field("csmFallbackCascades") > 0:
        failures.append(
            "CSM fallback smoke reported non-zero cascades in a skip sample: "
            f"{fallback_field('csmFallbackCascades')}."
        )

    reason_coverage = {
        reason: fallback_field(field) > 0
        for reason, field in reason_fields.items()
        if not expected_reasons or reason in expected_reasons
    }
    missing_reasons = [
        reason for reason in expected_reasons
        if not reason_coverage.get(reason, False)
    ]
    if missing_reasons:
        failures.append(
            "CSM fallback smoke missing skip reason coverage: "
            + ", ".join(missing_reasons)
            + "."
        )
    if "no-world" in expected_reasons and manager_field("noworld") <= 0:
        failures.append("CSM fallback no-world smoke did not mark the manager noworld sample.")

    maximum: dict[str, int] = {}
    if isinstance(manager_max, dict):
        maximum.update(
            {
                "noworld": manager_field("noworld"),
                "scheduledPasses": manager_field("scheduledPasses"),
                "scheduledMask": manager_field("scheduledMask"),
                "csmAtlasScheduled": manager_field("csmAtlasScheduled"),
                "csmReceiverScheduled": manager_field("csmReceiverScheduled"),
                "csmPublished": manager_field("csmPublished"),
                "csmCascadeCount": manager_field("csmCascadeCount"),
                "csmAtlasWidth": manager_field("csmAtlasWidth"),
                "csmAtlasHeight": manager_field("csmAtlasHeight"),
                "csmGeneration": manager_field("csmGeneration"),
            }
        )
    if isinstance(fallback_max, dict):
        maximum.update(fallback_max)

    return {
        "found": bool(manager_samples or fallback_samples),
        "status": "failed" if failures else "passed",
        "managerSampleCount": len(manager_samples),
        "fallbackSampleCount": len(fallback_samples),
        "reasonCoverage": reason_coverage,
        "max": maximum,
        "failures": list(dict.fromkeys(failures)),
    }


def analyze_dlight_shadow_log(log_path: Path) -> dict[str, object]:
    info: dict[str, object] = {
        "found": False,
        "sampleCount": 0,
        "latest": {},
        "max": {},
        "scenes": {},
        "shadowManager": {
            "found": False,
            "sampleCount": 0,
            "latest": {},
            "max": {},
            "scenes": {},
        },
        "surfaceLightSpot": {
            "found": False,
            "sampleCount": 0,
            "latest": {},
            "max": {},
            "scenes": {},
        },
        "surfaceLightSpotLod": surface_light_spot_lod_summary([]),
        "shadowAtlasContract": shadow_atlas_contract_summary([]),
        "csmShadows": csm_shadow_runtime_summary([], []),
        "csmStability": csm_stability_summary([], []),
        "csmCascadeContract": csm_cascade_contract_summary([]),
        "csmFallbacks": csm_fallback_summary([], [], CSM_FALLBACK_REQUIRED_REASONS),
        "shadowProfile": shadow_profile_summary([], [], [], [], [], []),
    }
    if not log_path.exists():
        return info

    text = log_path.read_text(encoding="utf-8", errors="replace")
    samples: list[dict[str, int]] = []
    scene_samples: dict[str, list[dict[str, int]]] = {}
    manager_samples: list[dict[str, int]] = []
    manager_scene_samples: dict[str, list[dict[str, int]]] = {}
    shadow_atlas_samples: list[dict[str, int]] = []
    shadow_atlas_scene_samples: dict[str, list[dict[str, int]]] = {}
    surface_spot_samples: list[dict[str, int]] = []
    surface_spot_scene_samples: dict[str, list[dict[str, int]]] = {}
    csm_samples: list[dict[str, int]] = []
    csm_scene_samples: dict[str, list[dict[str, int]]] = {}
    csm_cascade_samples: list[dict[str, int]] = []
    csm_cascade_scene_samples: dict[str, list[dict[str, int]]] = {}
    csm_fallback_samples: list[dict[str, int]] = []
    csm_fallback_scene_samples: dict[str, list[dict[str, int]]] = {}
    current_scene = ""
    for line in text.splitlines():
        if match := DLIGHT_SHADOW_SCENE_BEGIN_RE.search(line):
            current_scene = sanitize(match.group("scene"))
            scene_samples.setdefault(current_scene, [])
            manager_scene_samples.setdefault(current_scene, [])
            shadow_atlas_scene_samples.setdefault(current_scene, [])
            surface_spot_scene_samples.setdefault(current_scene, [])
            csm_scene_samples.setdefault(current_scene, [])
            csm_cascade_scene_samples.setdefault(current_scene, [])
            csm_fallback_scene_samples.setdefault(current_scene, [])
            continue
        if match := DLIGHT_SHADOW_SCENE_END_RE.search(line):
            if sanitize(match.group("scene")) == current_scene:
                current_scene = ""
            continue
        if match := SHADOW_MANAGER_RE.search(line):
            sample = shadow_manager_sample(match)
            manager_samples.append(sample)
            if current_scene:
                manager_scene_samples.setdefault(current_scene, []).append(sample)
            continue
        if match := SHADOW_ATLAS_CONTRACT_RE.search(line):
            sample = shadow_atlas_contract_sample(match)
            shadow_atlas_samples.append(sample)
            if current_scene:
                shadow_atlas_scene_samples.setdefault(current_scene, []).append(sample)
            continue
        if match := SURFACELIGHT_SPOT_PLAN_RE.search(line):
            sample = {name: int(value) for name, value in match.groupdict().items()}
            surface_spot_samples.append(sample)
            if current_scene:
                surface_spot_scene_samples.setdefault(current_scene, []).append(sample)
            continue
        if match := CSM_SHADOWS_RE.search(line):
            sample = csm_shadow_sample(match)
            csm_samples.append(sample)
            if current_scene:
                csm_scene_samples.setdefault(current_scene, []).append(sample)
            continue
        if match := CSM_CASCADE_RE.search(line):
            sample = csm_cascade_sample(match)
            csm_cascade_samples.append(sample)
            if current_scene:
                csm_cascade_scene_samples.setdefault(current_scene, []).append(sample)
            continue
        if match := CSM_PLAN_SKIP_RE.search(line):
            sample = csm_fallback_sample(match)
            csm_fallback_samples.append(sample)
            if current_scene:
                csm_fallback_scene_samples.setdefault(current_scene, []).append(sample)
            continue
        if match := DLIGHT_SHADOW_PLAN_RE.search(line):
            sample = {name: int(value) for name, value in match.groupdict().items()}
            samples.append(sample)
            if current_scene:
                scene_samples.setdefault(current_scene, []).append(sample)

    if manager_samples:
        manager_summary = summarize_int_samples(manager_samples)
        manager_summary["scenes"] = {
            scene_id: {
                **summarize_int_samples(records),
            }
            for scene_id, records in sorted(manager_scene_samples.items())
            if records
        }
        info["shadowManager"] = manager_summary
    if surface_spot_samples:
        surface_spot_summary = summarize_int_samples(surface_spot_samples)
        surface_spot_summary["scenes"] = {
            scene_id: {
                **summarize_int_samples(records),
            }
            for scene_id, records in sorted(surface_spot_scene_samples.items())
            if records
        }
        info["surfaceLightSpot"] = surface_spot_summary
        surface_spot_lod = surface_light_spot_lod_summary(surface_spot_samples)
        surface_spot_lod["scenes"] = {
            scene_id: surface_light_spot_lod_summary(records)
            for scene_id, records in sorted(surface_spot_scene_samples.items())
            if records
        }
        info["surfaceLightSpotLod"] = surface_spot_lod
    if shadow_atlas_samples:
        atlas_contract = shadow_atlas_contract_summary(shadow_atlas_samples)
        atlas_contract["scenes"] = {
            scene_id: shadow_atlas_contract_summary(records)
            for scene_id, records in sorted(shadow_atlas_scene_samples.items())
            if records
        }
        info["shadowAtlasContract"] = atlas_contract
    if manager_samples or csm_samples:
        csm_runtime = csm_shadow_runtime_summary(manager_samples, csm_samples)
        csm_runtime["scenes"] = {
            scene_id: csm_shadow_runtime_summary(
                manager_scene_samples.get(scene_id, []),
                csm_scene_samples.get(scene_id, []),
            )
            for scene_id in sorted(set(manager_scene_samples) | set(csm_scene_samples))
            if manager_scene_samples.get(scene_id) or csm_scene_samples.get(scene_id)
        }
        info["csmShadows"] = csm_runtime
        stability_scene_ids = sorted(
            scene_id
            for scene_id in set(manager_scene_samples) | set(csm_scene_samples)
            if scene_id in CSM_SHIMMER_EVIDENCE_CATEGORIES
        )
        stability_manager_samples = [
            sample
            for scene_id in stability_scene_ids
            for sample in manager_scene_samples.get(scene_id, [])
        ]
        stability_csm_samples = [
            sample
            for scene_id in stability_scene_ids
            for sample in csm_scene_samples.get(scene_id, [])
        ]
        csm_stability = csm_stability_summary(stability_manager_samples, stability_csm_samples)
        csm_stability["scenes"] = {
            scene_id: csm_stability_summary(
                manager_scene_samples.get(scene_id, []),
                csm_scene_samples.get(scene_id, []),
            )
            for scene_id in stability_scene_ids
        }
        info["csmStability"] = csm_stability
    if csm_cascade_samples:
        cascade_contract = csm_cascade_contract_summary(csm_cascade_samples)
        cascade_contract["scenes"] = {
            scene_id: csm_cascade_contract_summary(records)
            for scene_id, records in sorted(csm_cascade_scene_samples.items())
            if records
        }
        info["csmCascadeContract"] = cascade_contract
    if manager_samples or csm_fallback_samples:
        fallback_scene_ids = sorted(
            scene_id
            for scene_id in set(manager_scene_samples) | set(csm_fallback_scene_samples)
            if scene_id in CSM_FALLBACK_SCENE_REASONS
        )
        fallback_manager_samples = [
            sample
            for scene_id in fallback_scene_ids
            for sample in manager_scene_samples.get(scene_id, [])
        ]
        fallback_skip_samples = [
            sample
            for scene_id in fallback_scene_ids
            for sample in csm_fallback_scene_samples.get(scene_id, [])
        ]
        csm_fallbacks = csm_fallback_summary(
            fallback_manager_samples,
            fallback_skip_samples,
            CSM_FALLBACK_REQUIRED_REASONS,
        )
        csm_fallbacks["scenes"] = {
            scene_id: csm_fallback_summary(
                manager_scene_samples.get(scene_id, []),
                csm_fallback_scene_samples.get(scene_id, []),
                (CSM_FALLBACK_SCENE_REASONS[scene_id],),
            )
            for scene_id in fallback_scene_ids
        }
        info["csmFallbacks"] = csm_fallbacks

    if (
        samples
        or manager_samples
        or shadow_atlas_samples
        or surface_spot_samples
        or csm_samples
        or csm_cascade_samples
    ):
        profile = shadow_profile_summary(
            samples,
            manager_samples,
            shadow_atlas_samples,
            surface_spot_samples,
            csm_samples,
            csm_cascade_samples,
        )
        profile_scene_ids = sorted(
            (
                set(scene_samples)
                | set(manager_scene_samples)
                | set(shadow_atlas_scene_samples)
                | set(surface_spot_scene_samples)
                | set(csm_scene_samples)
                | set(csm_cascade_scene_samples)
            )
            - set(CSM_FALLBACK_SCENE_REASONS)
        )
        profile["scenes"] = {
            scene_id: shadow_profile_summary(
                scene_samples.get(scene_id, []),
                manager_scene_samples.get(scene_id, []),
                shadow_atlas_scene_samples.get(scene_id, []),
                surface_spot_scene_samples.get(scene_id, []),
                csm_scene_samples.get(scene_id, []),
                csm_cascade_scene_samples.get(scene_id, []),
            )
            for scene_id in profile_scene_ids
            if (
                scene_samples.get(scene_id)
                or manager_scene_samples.get(scene_id)
                or shadow_atlas_scene_samples.get(scene_id)
                or surface_spot_scene_samples.get(scene_id)
                or csm_scene_samples.get(scene_id)
                or csm_cascade_scene_samples.get(scene_id)
            )
        }
        info["shadowProfile"] = profile

    if not samples:
        return info

    scene_summaries = {
        scene_id: {
            **summarize_int_samples(records),
        }
        for scene_id, records in sorted(scene_samples.items())
        if records
    }
    summary = summarize_int_samples(samples)
    info.update(
        {
            "found": True,
            "sampleCount": summary["sampleCount"],
            "latest": summary["latest"],
            "max": summary["max"],
            "scenes": scene_summaries,
        }
    )
    return info


def parse_vkinfo_text(text: str) -> dict[str, object]:
    info: dict[str, object] = {
        "found": False,
        "gpuTimings": [],
        "vulkanFailures": [],
    }
    lines = text.splitlines()

    for line in lines:
        if match := PIPELINE_CACHE_RE.search(line):
            info["found"] = True
            info["pipelineCache"] = {
                "path": match.group("path").strip(),
                "loadedKb": int(match.group("loaded")),
                "savedKb": int(match.group("saved")),
            }
        elif match := DISPLAY_HDR_RE.search(line):
            info["found"] = True
            info["displayHdr"] = {
                "state": match.group("state").strip(),
                "metadata": match.group("metadata").lower(),
                "paperWhite": float(match.group("paperWhite")),
                "maxLuminance": float(match.group("maxLuminance")),
            }
        elif match := OUTPUT_BACKEND_RE.search(line):
            info["found"] = True
            info["outputBackend"] = {
                "request": match.group("request").lower(),
                "selected": match.group("selected").lower(),
                "native": match.group("native").lower(),
                "displayHdr": match.group("displayHdr").lower(),
                "headroom": float(match.group("headroom")),
                "sdrWhite": float(match.group("sdrWhite")),
                "displayMax": float(match.group("displayMax")),
                "icc": match.group("icc").lower(),
                "iccBytes": int(match.group("iccBytes")),
            }
        elif match := TONE_MAP_RE.search(line):
            info["found"] = True
            info["toneMap"] = {
                "mode": match.group("mode").strip(),
                "exposure": float(match.group("exposure")),
            }
        elif match := POST_GAMMA_RE.search(line):
            info["found"] = True
            info["gamma"] = {
                "domain": match.group("domain").lower(),
                "shader": match.group("shader").lower(),
                "rGamma": float(match.group("gamma")),
                "exponent": float(match.group("exponent")),
                "overbright": float(match.group("overbright")),
            }
        elif match := BLOOM_RE.search(line):
            info["found"] = True
            info["bloom"] = {
                "threshold": float(match.group("threshold")),
                "softKnee": float(match.group("softKnee")),
                "intensity": float(match.group("intensity")),
            }
        elif match := MODERN_VULKAN_RE.search(line):
            info["found"] = True
            info["modernVulkan"] = {
                "sync2": match.group("sync2").lower(),
                "dynamicRendering": match.group("dynamic").strip(),
            }
        elif match := BARRIERS_RE.search(line):
            info["found"] = True
            info["barriers"] = {
                "sync2": int(match.group("sync2")),
                "legacy": int(match.group("legacy")),
            }
        elif match := DESCRIPTORS_RE.search(line):
            info["found"] = True
            info["descriptors"] = {
                "writes": int(match.group("writes")),
                "bindCalls": int(match.group("bindCalls")),
                "bindSets": int(match.group("bindSets")),
                "materialHits": int(match.group("hits")),
                "materialMisses": int(match.group("misses")),
            }
        elif match := COMMAND_POOLS_RE.search(line):
            info["found"] = True
            info["commandPools"] = {
                "frameResets": int(match.group("frame")),
                "uploadResets": int(match.group("upload")),
            }
        elif match := MEMORY_RE.search(line):
            info["found"] = True
            info["memory"] = {
                "allocs": int(match.group("allocs")),
                "peakAllocs": int(match.group("peakAllocs")),
                "liveKb": int(match.group("liveKb")),
                "peakKb": int(match.group("peakKb")),
            }
        elif match := GPU_TIMING_RE.search(line):
            timings = info["gpuTimings"]
            assert isinstance(timings, list)
            timings.append(
                {
                    "from": match.group("from").strip(),
                    "to": match.group("to").strip(),
                    "msec": float(match.group("msec")),
                }
            )

        if VULKAN_FAILURE_RE.search(line):
            failures = info["vulkanFailures"]
            assert isinstance(failures, list)
            failures.append(line.strip())

    return info


def analyze_vk_log(log_path: Path, profile: str) -> dict[str, object]:
    text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
    info = parse_vkinfo_text(text)
    failures: list[str] = []

    if not info.get("found"):
        failures.append("vkinfo output was not found.")

    for required_key in ("pipelineCache", "displayHdr", "outputBackend", "toneMap", "gamma", "bloom", "modernVulkan", "barriers", "descriptors", "commandPools", "memory"):
        if required_key not in info:
            failures.append(f"vkinfo field is missing: {required_key}.")

    modern = info.get("modernVulkan")
    barriers = info.get("barriers")
    if isinstance(modern, dict) and isinstance(barriers, dict):
        if modern.get("sync2") == "enabled" and int_metric(barriers.get("sync2", 0)) == 0:
            failures.append("sync2 is enabled but no sync2 barriers were observed.")

    if profile == "vk-hdr":
        display_hdr = info.get("displayHdr")
        output_backend = info.get("outputBackend")
        if isinstance(display_hdr, dict):
            state = str(display_hdr.get("state", "")).lower()
            if state == "disabled":
                failures.append("vk-hdr profile requested HDR display, but vkinfo reported it disabled.")
        if isinstance(output_backend, dict) and output_backend.get("request") != "hdr10-pq":
            failures.append("vk-hdr profile did not request the HDR10 output backend.")

    vulkan_failures = info.get("vulkanFailures", [])
    if isinstance(vulkan_failures, list) and vulkan_failures:
        failures.append(f"Vulkan error lines were found: {len(vulkan_failures)}.")

    info["failures"] = failures
    return info


def evaluate_gate(manifest: dict[str, object]) -> list[str]:
    if manifest.get("dryRun"):
        return []

    gate_name = str(manifest.get("gate") or "")
    requirements = (
        RC_GATE_PRESETS.get(gate_name, {}).get("requirements", {}) if gate_name else {}
    )
    runs = manifest.get("runs", [])
    failures: list[str] = []

    if not isinstance(runs, list):
        return ["Manifest runs field is not a list."]

    failed_runs = [
        str(run.get("type", "run"))
        for run in runs
        if isinstance(run, dict) and run.get("status") != "passed"
    ]
    if failed_runs:
        failures.append("Runtime runs failed: " + ", ".join(failed_runs[:8]))

    if requirements.get("require_screenshots"):
        screenshots = [
            shot
            for run in runs
            if isinstance(run, dict)
            for shot in run.get("screenshots", [])  # type: ignore[union-attr]
            if isinstance(shot, dict)
        ]
        if not screenshots:
            failures.append("No screenshots were planned or captured.")
        missing = [str(shot.get("name")) for shot in screenshots if not shot.get("found")]
        if missing:
            failures.append(
                f"Missing screenshots: {len(missing)}/{len(screenshots)} "
                + ", ".join(missing[:8])
            )

    analyses = [
        run.get("vkinfo")
        for run in runs
        if isinstance(run, dict) and isinstance(run.get("vkinfo"), dict)
    ]
    if requirements.get("require_vkinfo"):
        if not analyses:
            failures.append("No vkinfo analysis was collected.")
        for analysis in analyses:
            assert isinstance(analysis, dict)
            analysis_failures = analysis.get("failures", [])
            if isinstance(analysis_failures, list) and analysis_failures:
                failures.append(
                    "Vulkan diagnostic failures: "
                    + "; ".join(str(failure) for failure in analysis_failures[:8])
                )

    if requirements.get("require_gpu_timings"):
        timing_count = sum(
            len(analysis.get("gpuTimings", []))
            for analysis in analyses
            if isinstance(analysis.get("gpuTimings", []), list)
        )
        if timing_count <= 0:
            failures.append("No Vulkan GPU timing samples were found.")

    if requirements.get("require_dlight_shadow_scenes"):
        shadow_runs = [
            run for run in runs
            if isinstance(run, dict) and run.get("type") == "dlight-shadow-scenes"
        ]
        if not shadow_runs:
            failures.append("No Vulkan dlight shadow scene run was planned or captured.")
        else:
            shadow_screenshots = [
                shot
                for run in shadow_runs
                for shot in run.get("screenshots", [])  # type: ignore[union-attr]
                if isinstance(shot, dict) and shot.get("shadowScene")
            ]
            if not shadow_screenshots:
                failures.append("No Vulkan dlight shadow scene screenshots were planned or captured.")
            required_categories = [
                str(category)
                for category in requirements.get(
                    "required_dlight_shadow_categories",
                    DLIGHT_SHADOW_EVIDENCE_CATEGORIES,
                )
            ]
            covered_categories = dlight_shadow_scene_categories(shadow_screenshots)
            missing_categories = [
                category
                for category in required_categories
                if category not in covered_categories
            ]
            if missing_categories:
                failures.append(
                    "Vulkan dlight shadow scene coverage is missing categor"
                    f"{'ies' if len(missing_categories) != 1 else 'y'}: "
                    + ", ".join(missing_categories)
                    + "."
                )
            missing_shadow = [
                str(shot.get("name"))
                for shot in shadow_screenshots
                if not shot.get("found")
            ]
            if missing_shadow:
                failures.append(
                    f"Missing Vulkan dlight shadow screenshots: {len(missing_shadow)}/{len(shadow_screenshots)} "
                    + ", ".join(missing_shadow[:8])
                )
            shadow_logs = [
                run.get("dlightShadow")
                for run in shadow_runs
                if isinstance(run.get("dlightShadow"), dict)
            ]
            active_logs = [
                summary
                for summary in shadow_logs
                if isinstance(summary, dict)
                and summary.get("found")
                and isinstance(summary.get("max"), dict)
                and int_metric(summary["max"].get("planned", 0)) > 0  # type: ignore[index]
                and int_metric(summary["max"].get("renderLights", 0)) > 0  # type: ignore[index]
            ]
            if not active_logs:
                failures.append("No Vulkan dlight shadow planning/render log samples were found.")
            else:
                active_manager_logs = [
                    summary.get("shadowManager")
                    for summary in active_logs
                    if shadow_manager_summary_active(summary.get("shadowManager"))
                ]
                if not active_manager_logs:
                    failures.append(
                        "No Vulkan shadow manager schedule/publication log samples were found."
                    )
                scene_categories: dict[str, set[str]] = {}
                for shot in shadow_screenshots:
                    scene_id = sanitize(str(shot.get("scene", "")))
                    if not scene_id:
                        continue
                    scene_categories.setdefault(scene_id, set()).update(
                        str(category)
                        for category in shot.get("evidenceCategories", [])
                        if str(category).strip()
                    )
                logged_scenes = {
                    sanitize(str(scene_id))
                    for summary in shadow_logs
                    if isinstance(summary, dict)
                    and isinstance(summary.get("scenes"), dict)
                    for scene_id, scene_summary in summary["scenes"].items()  # type: ignore[union-attr]
                    if isinstance(scene_summary, dict)
                    and isinstance(scene_summary.get("max"), dict)
                    and int_metric(scene_summary["max"].get("planned", 0)) > 0  # type: ignore[index]
                    and int_metric(scene_summary["max"].get("renderLights", 0)) > 0  # type: ignore[index]
                }
                logged_categories = {
                    category
                    for scene_id in logged_scenes
                    for category in scene_categories.get(scene_id, set())
                }
                missing_log_categories = [
                    category
                    for category in required_categories
                    if category not in logged_categories
                ]
                if missing_log_categories:
                    failures.append(
                        "Vulkan dlight shadow logs are missing categor"
                        f"{'ies' if len(missing_log_categories) != 1 else 'y'}: "
                        + ", ".join(missing_log_categories)
                        + "."
                    )
                if active_manager_logs:
                    manager_logged_scenes = {
                        sanitize(str(scene_id))
                        for summary in active_logs
                        if isinstance(summary, dict)
                        and isinstance(summary.get("shadowManager"), dict)
                        and isinstance(summary["shadowManager"].get("scenes"), dict)  # type: ignore[index]
                        for scene_id, scene_summary in summary["shadowManager"]["scenes"].items()  # type: ignore[index]
                        if shadow_manager_summary_active(scene_summary)
                    }
                    manager_logged_categories = {
                        category
                        for scene_id in manager_logged_scenes
                        for category in scene_categories.get(scene_id, set())
                    }
                    missing_manager_categories = [
                        category
                        for category in required_categories
                        if category not in manager_logged_categories
                    ]
                    if missing_manager_categories:
                        failures.append(
                            "Vulkan shadow manager logs are missing categor"
                            f"{'ies' if len(missing_manager_categories) != 1 else 'y'}: "
                            + ", ".join(missing_manager_categories)
                            + "."
                        )
                required_surface_categories = [
                    str(category)
                    for category in requirements.get(
                        "required_surface_light_spot_categories",
                        (),
                    )
                ]
                if required_surface_categories:
                    surface_logged_scenes = {
                        sanitize(str(scene_id))
                        for summary in shadow_logs
                        if isinstance(summary, dict)
                        and isinstance(summary.get("surfaceLightSpot"), dict)
                        and isinstance(summary["surfaceLightSpot"].get("scenes"), dict)  # type: ignore[index]
                        for scene_id, scene_summary in summary["surfaceLightSpot"]["scenes"].items()  # type: ignore[index]
                        if surface_light_spot_summary_active(scene_summary)
                    }
                    if not surface_logged_scenes:
                        failures.append(
                            "No Vulkan surfacelight spot manager log samples were found."
                        )
                    surface_logged_categories = {
                        category
                        for scene_id in surface_logged_scenes
                        for category in scene_categories.get(scene_id, set())
                    }
                    missing_surface_categories = [
                        category
                        for category in required_surface_categories
                        if category not in surface_logged_categories
                    ]
                    if missing_surface_categories:
                        failures.append(
                            "Vulkan surfacelight spot manager logs are missing categor"
                            f"{'ies' if len(missing_surface_categories) != 1 else 'y'}: "
                            + ", ".join(missing_surface_categories)
                            + "."
                        )
                    lod_logged_scenes = {
                        sanitize(str(scene_id))
                        for summary in shadow_logs
                        if isinstance(summary, dict)
                        and isinstance(summary.get("surfaceLightSpotLod"), dict)
                        and isinstance(summary["surfaceLightSpotLod"].get("scenes"), dict)  # type: ignore[index]
                        for scene_id, scene_summary in summary["surfaceLightSpotLod"]["scenes"].items()  # type: ignore[index]
                        if surface_light_spot_lod_summary_active(scene_summary)
                    }
                    if not lod_logged_scenes:
                        failures.append(
                            "No Vulkan surfacelight spot LOD smoke log samples passed."
                        )
                    lod_logged_categories = {
                        category
                        for scene_id in lod_logged_scenes
                        for category in scene_categories.get(scene_id, set())
                    }
                    missing_lod_categories = [
                        category
                        for category in required_surface_categories
                        if category not in lod_logged_categories
                    ]
                    if missing_lod_categories:
                        failures.append(
                            "Vulkan surfacelight spot LOD logs are missing categor"
                            f"{'ies' if len(missing_lod_categories) != 1 else 'y'}: "
                            + ", ".join(missing_lod_categories)
                            + "."
                        )
                required_csm_categories = [
                    str(category)
                    for category in requirements.get(
                        "required_csm_shadow_categories",
                        (),
                    )
                ]
                if required_csm_categories:
                    csm_logged_scenes = {
                        sanitize(str(scene_id))
                        for summary in shadow_logs
                        if isinstance(summary, dict)
                        and isinstance(summary.get("csmShadows"), dict)
                        and isinstance(summary["csmShadows"].get("scenes"), dict)  # type: ignore[index]
                        for scene_id, scene_summary in summary["csmShadows"]["scenes"].items()  # type: ignore[index]
                        if csm_shadow_runtime_summary_active(scene_summary)
                    }
                    if not csm_logged_scenes:
                        failures.append(
                            "No Vulkan CSM runtime smoke log samples passed."
                        )
                    csm_logged_categories = {
                        category
                        for scene_id in csm_logged_scenes
                        for category in scene_categories.get(scene_id, set())
                    }
                    missing_csm_categories = [
                        category
                        for category in required_csm_categories
                        if category not in csm_logged_categories
                    ]
                    if missing_csm_categories:
                        failures.append(
                            "Vulkan CSM runtime logs are missing categor"
                            f"{'ies' if len(missing_csm_categories) != 1 else 'y'}: "
                            + ", ".join(missing_csm_categories)
                            + "."
                        )
                    if requirements.get("require_csm_shimmer_screenshot_diff"):
                        shimmer_summaries = [
                            run.get("csmShimmerScreenshots")
                            for run in shadow_runs
                            if isinstance(run.get("csmShimmerScreenshots"), dict)
                        ]
                        if not any(
                            csm_shimmer_screenshot_summary_active(summary)
                            for summary in shimmer_summaries
                        ):
                            failures.append(
                                "No Vulkan CSM shimmer screenshot diff smoke passed."
                            )
                            for summary in shimmer_summaries:
                                if not isinstance(summary, dict):
                                    continue
                                summary_failures = summary.get("failures", [])
                                if isinstance(summary_failures, list):
                                    failures.extend(str(failure) for failure in summary_failures[:4])
                if requirements.get("require_combined_shadow_atlas_smoke"):
                    combined_summaries: list[dict[str, object]] = []
                    for run in shadow_runs:
                        combined = run.get("combinedShadowAtlas")
                        if isinstance(combined, dict):
                            combined_summaries.append(combined)
                            continue
                        dlight_shadow = run.get("dlightShadow")
                        if isinstance(dlight_shadow, dict):
                            combined_summaries.append(
                                combined_shadow_atlas_summary(dlight_shadow)
                            )
                    if not any(
                        combined_shadow_atlas_summary_active(summary)
                        for summary in combined_summaries
                    ):
                        failures.append(
                            "No Vulkan combined shadow atlas smoke passed."
                        )
                        for summary in combined_summaries:
                            summary_failures = summary.get("failures", [])
                            if isinstance(summary_failures, list):
                                failures.extend(str(failure) for failure in summary_failures[:6])
                if requirements.get("require_csm_fallback_smoke"):
                    fallback_summaries: list[dict[str, object]] = []
                    for run in shadow_runs:
                        fallback = run.get("csmFallbacks")
                        if isinstance(fallback, dict):
                            fallback_summaries.append(fallback)
                            continue
                        dlight_shadow = run.get("dlightShadow")
                        if isinstance(dlight_shadow, dict) and isinstance(
                            dlight_shadow.get("csmFallbacks"), dict
                        ):
                            fallback_summaries.append(dlight_shadow["csmFallbacks"])  # type: ignore[index]
                    if not any(
                        csm_fallback_summary_active(summary)
                        for summary in fallback_summaries
                    ):
                        failures.append(
                            "No Vulkan CSM fallback smoke passed."
                        )
                        for summary in fallback_summaries:
                            summary_failures = summary.get("failures", [])
                            if isinstance(summary_failures, list):
                                failures.extend(str(failure) for failure in summary_failures[:6])

    if requirements.get("require_timedemo_metrics"):
        demos = manifest.get("demos", [])
        if not isinstance(demos, list) or not demos:
            failures.append("No demos were configured for a timedemo gate.")
        for demo in demos if isinstance(demos, list) else []:
            found = any(
                isinstance(run, dict)
                and run.get("type") == "timedemo"
                and str(run.get("demo", "")).lower() == str(demo).lower()
                and isinstance(run.get("timedemoMetrics"), dict)
                for run in runs
            )
            if not found:
                failures.append(f"Missing timedemo metrics for {demo}.")

    if requirements.get("require_hdr_request"):
        hdr_states = [
            str(analysis.get("displayHdr", {}).get("state", "")).lower()
            for analysis in analyses
            if isinstance(analysis.get("displayHdr"), dict)
        ]
        hdr_backend_requests = [
            str(analysis.get("outputBackend", {}).get("request", "")).lower()
            for analysis in analyses
            if isinstance(analysis.get("outputBackend"), dict)
        ]
        if hdr_states and all(state == "disabled" for state in hdr_states):
            failures.append("HDR display request was not visible in vkinfo.")
        if hdr_backend_requests and "hdr10-pq" not in hdr_backend_requests:
            failures.append("HDR10 output backend request was not visible in vkinfo.")

    return failures


def markdown_summary(manifest: dict[str, object], manifest_path: Path) -> str:
    lines = [
        f"# Vulkan Runtime Sweep: {manifest.get('gate') or 'custom'}",
        "",
        f"- Manifest: `{manifest_path}`",
        f"- Profile: `{manifest.get('profile')}`",
        f"- Dry run: `{manifest.get('dryRun')}`",
        "",
    ]

    gate_failures = manifest.get("gateFailures", [])
    if isinstance(gate_failures, list) and gate_failures:
        lines.append("## Gate Failures")
        lines.append("")
        for failure in gate_failures:
            lines.append(f"- {failure}")
        lines.append("")

    runs = manifest.get("runs", [])
    if isinstance(runs, list):
        lines.append("## Runs")
        lines.append("")
        lines.append("| Type | Status | Log |")
        lines.append("|---|---:|---|")
        for run in runs:
            if not isinstance(run, dict):
                continue
            lines.append(
                f"| {run.get('type', '-')} | {run.get('status', '-')} | `{run.get('log', '-')}` |"
            )
        lines.append("")

        analyses = [
            run.get("vkinfo")
            for run in runs
            if isinstance(run, dict) and isinstance(run.get("vkinfo"), dict)
        ]
        if analyses:
            lines.append("## Vulkan Diagnostics")
            lines.append("")
            for index, analysis in enumerate(analyses, start=1):
                assert isinstance(analysis, dict)
                modern = analysis.get("modernVulkan", {})
                barriers = analysis.get("barriers", {})
                display_hdr = analysis.get("displayHdr", {})
                tone_map = analysis.get("toneMap", {})
                bloom = analysis.get("bloom", {})
                timings = analysis.get("gpuTimings", [])
                lines.append(
                    f"- Log {index}: sync2 `{modern.get('sync2', '-') if isinstance(modern, dict) else '-'}`, "
                    f"dynamic rendering `{modern.get('dynamicRendering', '-') if isinstance(modern, dict) else '-'}`, "
                    f"barriers `{barriers.get('sync2', '-') if isinstance(barriers, dict) else '-'}/"
                    f"{barriers.get('legacy', '-') if isinstance(barriers, dict) else '-'}`, "
                    f"HDR `{display_hdr.get('state', '-') if isinstance(display_hdr, dict) else '-'}`, "
                    f"tone map `{tone_map.get('mode', '-') if isinstance(tone_map, dict) else '-'}`, "
                    f"bloom knee `{bloom.get('softKnee', '-') if isinstance(bloom, dict) else '-'}`, "
                    f"GPU timing spans `{len(timings) if isinstance(timings, list) else 0}`"
                )
            lines.append("")

        screenshots = [
            shot
            for run in runs
            if isinstance(run, dict)
            for shot in run.get("screenshots", [])  # type: ignore[union-attr]
            if isinstance(shot, dict)
        ]
        if screenshots:
            found = sum(1 for shot in screenshots if shot.get("found"))
            planned = len(screenshots)
            label = "Planned" if manifest.get("dryRun") else "Found"
            lines.append(f"## Screenshots\n\n- {label}: `{found if not manifest.get('dryRun') else planned}/{planned}`\n")

        timedemos = [
            run
            for run in runs
            if isinstance(run, dict) and run.get("type") == "timedemo"
        ]
        if timedemos:
            lines.append("## Timedemos")
            lines.append("")
            lines.append("| Demo | Status | FPS | Frames | Seconds |")
            lines.append("|---|---:|---:|---:|---:|")
            for run in timedemos:
                metrics = run.get("timedemoMetrics")
                if isinstance(metrics, dict):
                    fps = f"{float(metrics.get('fps', 0.0)):.1f}"
                    frames = str(metrics.get("frames", "-"))
                    seconds = f"{float(metrics.get('seconds', 0.0)):.2f}"
                else:
                    fps = frames = seconds = "-"
                lines.append(
                    f"| {run.get('demo', '-')} | {run.get('status', '-')} | {fps} | {frames} | {seconds} |"
                )
            lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def main() -> int:
    args = parse_args()
    if args.list_gates:
        print_gate_list()
        return 0

    apply_gate_defaults(args)

    validate_runtime_options(args)
    args.fs_game = validate_fs_game(args.fs_game)

    exe = resolve_exe(args.exe, allow_missing=args.dry_run)
    basepath = args.basepath.resolve() if args.basepath else exe.parent.resolve()
    maps = validate_q3_command_tokens(split_csv(args.maps), "--maps")
    demos = validate_q3_command_tokens(split_csv(args.demos), "--demos")
    cvars = make_cvars(args)
    startup_cvars = launch_cvars(cvars)
    dlight_shadow_cvars = dlight_shadow_scene_cvars(cvars)
    dlight_shadow_startup_cvars = launch_cvars(dlight_shadow_cvars)
    dlight_shadow_scenes = dlight_shadow_evidence_scenes()

    run_id = (
        datetime.now(timezone.utc).strftime("vk-sweep-%Y%m%d-%H%M%S-%f")
        + f"-p{Path.cwd().name}-{sanitize(str(Path.cwd().drive or 'root'))}"
    )
    output_root = args.output_dir.resolve() / run_id
    homepath = args.homepath.resolve() if args.homepath else output_root / "home"
    logs_dir = output_root / "logs"
    runs: list[dict[str, object]] = []
    dlight_shadow_sidecars = (
        write_dlight_shadow_sidecar_lights(homepath, args.fs_game, dlight_shadow_scenes)
        if args.dlight_shadow_scenes
        else []
    )

    if maps and not args.no_map_sweep:
        cfg_name = f"{run_id}-maps.cfg"
        cfg, expected_screenshots = build_map_cfg(args, cvars, maps, run_id)
        cfg_path = write_cfg(homepath, args.fs_game, cfg_name, cfg)
        command = base_launch_args(exe, basepath, homepath, args.fs_game, cfg_name, startup_cvars)
        log_path = logs_dir / "maps.log"
        result = run_engine(command, exe.parent, args.timeout, log_path, args.dry_run)
        shots = screenshot_results(homepath, args.fs_game, expected_screenshots)
        if not args.dry_run:
            result["vkinfo"] = analyze_vk_log(log_path, args.profile)
        result.update(
            {
                "type": "map-screenshots",
                "config": str(cfg_path),
                "maps": maps,
                "screenshots": shots,
            }
        )
        runs.append(result)

    if args.dlight_shadow_scenes:
        cfg_name = f"{run_id}-dlight-shadows.cfg"
        cfg, expected_screenshots = build_dlight_shadow_cfg(
            args,
            dlight_shadow_cvars,
            dlight_shadow_scenes,
            run_id,
        )
        cfg_path = write_cfg(homepath, args.fs_game, cfg_name, cfg)
        command = base_launch_args(
            exe,
            basepath,
            homepath,
            args.fs_game,
            cfg_name,
            dlight_shadow_startup_cvars,
        )
        log_path = logs_dir / "dlight-shadows.log"
        result = run_engine(command, exe.parent, args.timeout, log_path, args.dry_run)
        shots = screenshot_results(homepath, args.fs_game, expected_screenshots)
        csm_shimmer_screenshots = csm_shimmer_screenshot_summary(shots)
        if not args.dry_run:
            csm_shimmer_screenshots = apply_csm_shimmer_screenshot_diffs(
                shots,
                output_root / "csm-shimmer-diffs",
            )
            result["vkinfo"] = analyze_vk_log(log_path, args.profile)
            result["dlightShadow"] = analyze_dlight_shadow_log(log_path)
            result["combinedShadowAtlas"] = combined_shadow_atlas_summary(result["dlightShadow"])
            fallback_summary = result["dlightShadow"].get("csmFallbacks")
            if isinstance(fallback_summary, dict):
                result["csmFallbacks"] = fallback_summary
        result.update(
            {
                "type": "dlight-shadow-scenes",
                "config": str(cfg_path),
                "maps": sorted({str(scene["map"]) for scene in dlight_shadow_scenes}),
                "scenes": dlight_shadow_scenes,
                "screenshots": shots,
                "csmShimmerScreenshots": csm_shimmer_screenshots,
                "sidecarLights": dlight_shadow_sidecars,
                "cvars": dlight_shadow_cvars,
                "startupCvars": dlight_shadow_startup_cvars,
            }
        )
        runs.append(result)

    if demos and not args.no_demo_sweep:
        for demo in demos:
            safe_demo = sanitize(demo)
            cfg_name = f"{run_id}-demo-{safe_demo}.cfg"
            cfg_path = write_cfg(homepath, args.fs_game, cfg_name, build_demo_cfg(args, cvars, demo))
            command = base_launch_args(exe, basepath, homepath, args.fs_game, cfg_name, startup_cvars)
            log_path = logs_dir / f"demo-{safe_demo}.log"
            result = run_engine(command, exe.parent, args.timeout, log_path, args.dry_run)
            metrics = timedemo_metrics(log_path)
            if metrics:
                result["timedemoMetrics"] = metrics
            if not args.dry_run:
                result["vkinfo"] = analyze_vk_log(log_path, args.profile)
            result.update(
                {
                    "type": "timedemo",
                    "config": str(cfg_path),
                    "demo": demo,
                }
            )
            runs.append(result)

    manifest: dict[str, object] = {
        "runId": run_id,
        "createdUtc": datetime.now(timezone.utc).isoformat(),
        "dryRun": args.dry_run,
        "gate": args.gate or "",
        "gateDescription": RC_GATE_PRESETS[args.gate]["description"] if args.gate else "",
        "gateRequirements": RC_GATE_PRESETS[args.gate]["requirements"] if args.gate else {},
        "exe": str(exe),
        "cwd": str(exe.parent),
        "basepath": str(basepath),
        "homepath": str(homepath),
        "fsGame": args.fs_game,
        "profile": args.profile,
        "cvars": cvars,
        "startupCvars": startup_cvars,
        "dlightShadowScenes": bool(args.dlight_shadow_scenes),
        "dlightShadowEvidenceCategories": list(DLIGHT_SHADOW_EVIDENCE_CATEGORIES),
        "dlightShadowEvidenceScenes": dlight_shadow_scenes if args.dlight_shadow_scenes else [],
        "dlightShadowSidecars": dlight_shadow_sidecars if args.dlight_shadow_scenes else [],
        "dlightShadowSceneCvars": dlight_shadow_cvars if args.dlight_shadow_scenes else {},
        "dlightShadowSceneStartupCvars": dlight_shadow_startup_cvars if args.dlight_shadow_scenes else {},
        "maps": maps,
        "demos": demos,
        "runs": runs,
    }
    manifest["gateFailures"] = evaluate_gate(manifest)

    output_root.mkdir(parents=True, exist_ok=True)
    manifest_path = output_root / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    if args.summary_markdown:
        summary_path = args.summary_markdown.resolve()
        summary_path.parent.mkdir(parents=True, exist_ok=True)
        summary_path.write_text(
            markdown_summary(manifest, manifest_path),
            encoding="utf-8",
            newline="\n",
        )

    run_count = len(runs)
    passed_runs = sum(1 for run in runs if run["status"] in {"passed", "planned"})
    screenshots = [
        shot
        for run in runs
        for shot in run.get("screenshots", [])  # type: ignore[attr-defined]
    ]
    found_screenshots = sum(1 for shot in screenshots if shot["found"])
    missing_screenshots = len(screenshots) - found_screenshots
    gate_failures = manifest["gateFailures"]

    print(f"Run id: {run_id}")
    if args.gate:
        gate_status = "planned" if args.dry_run else ("passed" if not gate_failures else "failed")
        print(f"Gate: {args.gate} ({gate_status})")
        for failure in gate_failures:
            print(f"  - {failure}")
    print(f"Manifest: {manifest_path}")
    print(f"Runs: {passed_runs}/{run_count} passed or planned")
    if screenshots:
        if args.dry_run:
            print(f"Screenshots: {len(screenshots)} planned")
        else:
            print(f"Screenshots: {found_screenshots}/{len(screenshots)} found")

    if args.dry_run:
        return 0
    if gate_failures or passed_runs != run_count or missing_screenshots:
        return 1
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"vk_runtime_sweep.py: {exc}", file=sys.stderr)
        raise SystemExit(2)
