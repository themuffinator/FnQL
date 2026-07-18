from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ROADMAP = ROOT / "docs" / "fnql" / "DLIGHT_SHADOWMAP_ROADMAP.md"
PLAN = ROOT / "docs-dev" / "plans" / "2026-06-03-vk-shadowmapping.md"


def require(text: str, needle: str, label: str, failures: list[str]) -> None:
    if needle not in text:
        failures.append(f"missing {label}: {needle}")


def section(text: str, start: str, end: str, label: str, failures: list[str]) -> str:
    start_index = text.find(start)
    if start_index < 0:
        failures.append(f"missing section start {label}: {start}")
        return ""
    end_index = text.find(end, start_index)
    if end_index < 0:
        failures.append(f"missing section end {label}: {end}")
        return text[start_index:]
    return text[start_index:end_index]


def check_roadmap(failures: list[str]) -> None:
    text = ROADMAP.read_text(encoding="utf-8")
    artifacts = section(
        text,
        "## Surfacelight Validation Artifacts",
        "## Combined Atlas Smoke Artifacts",
        "surfacelight validation artifacts",
        failures,
    )

    require(artifacts, "dlightShadowEvidenceCategories", "top-level category manifest field", failures)
    require(artifacts, "dlightShadowEvidenceScenes", "top-level scene manifest field", failures)
    require(artifacts, "dlightShadowSceneCvars", "shadow-scene cvar manifest field", failures)
    require(artifacts, "runs[].screenshots[]", "screenshot manifest path", failures)
    require(artifacts, "shadowScene: true", "shadow screenshot marker", failures)
    require(artifacts, "surfacelight-large-planar", "large planar surfacelight scene", failures)
    require(artifacts, "q3dm6", "representative map", failures)
    require(artifacts, "vk-modern-dlight-shadows-surfacelight-large-planar-q3dm6-vk", "Vulkan baseline key", failures)
    require(artifacts, "rc-parity-dlight-shadows-surfacelight-large-planar-q3dm6-glx", "GLx baseline key", failures)
    require(artifacts, "spotSurfaceCandidates", "manager surfacelight candidate telemetry", failures)
    require(artifacts, "spotSurfacePlans", "manager surfacelight plan telemetry", failures)
    require(artifacts, "spotPublished", "spot atlas publication telemetry", failures)
    require(artifacts, "spotAtlasTileSize", "spot atlas tile telemetry", failures)
    require(artifacts, "surfaceLightSpot", "surfacelight spot summary", failures)
    require(artifacts, "surfaceLightSpotLod", "surfacelight spot LOD summary", failures)
    require(artifacts, "surfaceSpotCandidates", "surfacelight candidate telemetry", failures)
    require(artifacts, "surfaceSpotPlans", "surfacelight plan telemetry", failures)
    require(artifacts, "surfaceSpotAllocated", "surfacelight atlas allocation telemetry", failures)
    require(artifacts, "surfaceSpotFootprintMax", "surfacelight footprint telemetry", failures)
    require(artifacts, "surfaceSpotCasterRadiusMax", "surfacelight caster radius telemetry", failures)
    require(artifacts, "surfaceSpotTileMax", "surfacelight effective tile telemetry", failures)
    require(artifacts, "requestedTiles.low", "low LOD tile coverage", failures)
    require(artifacts, "requestedTiles.nominal", "nominal LOD tile coverage", failures)
    require(artifacts, "requestedTiles.promoted", "promoted LOD tile coverage", failures)
    require(artifacts, "maxRequestedTile", "requested tile bound", failures)
    require(artifacts, "maxEffectiveTile", "effective tile bound", failures)
    require(artifacts, "maxAtlasTile", "atlas tile bound", failures)
    require(artifacts, "maxFill", "atlas fill bound", failures)
    require(artifacts, "REQUIRED_SURFACELIGHT_SPOT_CATEGORIES", "release-gate category contract", failures)

    for cvar in (
        "developer=1",
        "logfile=2",
        "r_dlightShadows=1",
        "r_dlightShadowDebug=1",
        "r_spotShadows=1",
        "r_spotShadowDebug=1",
        "r_surfaceLightProxies=1",
        "r_surfaceLightProxyDebug=1",
        "r_surfaceLightProxyShadows=1",
    ):
        require(artifacts, cvar, f"debug cvar {cvar}", failures)

    for review_term in (
        "Manual visual review notes",
        "large planar emitters",
        "missing surfacelight shadow",
        "unrelated geometry pulled into the cone",
        "atlas flooding",
        "85%",
        "RenderDoc inspection evidence",
    ):
        require(artifacts, review_term, f"manual review note {review_term}", failures)


def check_combined_atlas_roadmap(failures: list[str]) -> None:
    text = ROADMAP.read_text(encoding="utf-8")
    artifacts = section(
        text,
        "## Combined Atlas Smoke Artifacts",
        "## CSM Fallback Smoke Artifacts",
        "combined atlas smoke artifacts",
        failures,
    )

    require(artifacts, "dlightShadowEvidenceCategories", "top-level category manifest field", failures)
    require(artifacts, "dlightShadowEvidenceScenes", "top-level scene manifest field", failures)
    require(artifacts, "dlightShadowSidecars", "sidecar manifest field", failures)
    require(artifacts, "sidecarLights", "run sidecar evidence", failures)
    require(artifacts, "combined-shadow-atlas", "combined atlas scene", failures)
    require(artifacts, "q3dm6", "representative map", failures)
    require(artifacts, "maps/q3dm6.lights.json", "generated sidecar path", failures)
    require(artifacts, "combined-sidecar-spot", "generated sidecar light", failures)
    require(artifacts, "vk-modern-dlight-shadows-combined-shadow-atlas-q3dm6-vk", "Vulkan baseline key", failures)
    require(artifacts, "rc-parity-dlight-shadows-combined-shadow-atlas-q3dm6-glx", "GLx baseline key", failures)
    require(artifacts, "runs[].combinedShadowAtlas", "combined summary path", failures)
    require(artifacts, "scheduledPasses >= 4", "combined pass count bound", failures)
    require(artifacts, "scheduledMask", "combined schedule mask", failures)
    require(artifacts, "0x0f", "combined schedule bit mask", failures)
    require(artifacts, "pointScheduled", "point scheduled telemetry", failures)
    require(artifacts, "spotScheduled", "spot scheduled telemetry", failures)
    require(artifacts, "csmAtlasScheduled", "CSM atlas scheduled telemetry", failures)
    require(artifacts, "csmReceiverScheduled", "CSM receiver scheduled telemetry", failures)
    require(artifacts, "pointPublished", "point publication telemetry", failures)
    require(artifacts, "spotPublished", "spot publication telemetry", failures)
    require(artifacts, "csmPublished", "CSM publication telemetry", failures)
    require(artifacts, "spotStaticPlans", "static sidecar spot telemetry", failures)
    require(artifacts, "spotSurfacePlans", "surfacelight spot telemetry", failures)
    require(artifacts, "csmCascadeCount", "CSM cascade telemetry", failures)
    require(artifacts, "csmGeneration", "CSM generation telemetry", failures)

    for cvar in (
        "developer=1",
        "logfile=2",
        "r_dlightShadows=1",
        "r_staticLights=1",
        "r_staticLightShadows=1",
        "r_spotShadows=1",
        "r_surfaceLightProxies=1",
        "r_surfaceLightProxyShadows=1",
        "r_csmShadows=1",
        "r_csmDebug=1",
    ):
        require(artifacts, cvar, f"combined debug cvar {cvar}", failures)

    for review_term in (
        "Manual visual review notes",
        "atlas cross-talk",
        "all schedule lanes",
        "RenderDoc inspection",
    ):
        require(artifacts, review_term, f"combined manual review note {review_term}", failures)


def check_csm_fallback_roadmap(failures: list[str]) -> None:
    text = ROADMAP.read_text(encoding="utf-8")
    artifacts = section(
        text,
        "## CSM Fallback Smoke Artifacts",
        "## Roadmap",
        "CSM fallback smoke artifacts",
        failures,
    )

    for scene in (
        "csm-fallback-no-world",
        "csm-fallback-no-sun",
        "csm-fallback-atlas-unavailable",
        "csm-fallback-zero-cascade",
    ):
        require(artifacts, scene, f"CSM fallback scene {scene}", failures)

    require(artifacts, "dlightShadowEvidenceCategories", "top-level category manifest field", failures)
    require(artifacts, "dlightShadowEvidenceScenes", "top-level scene manifest field", failures)
    require(artifacts, "runs[].screenshots[]", "screenshot manifest path", failures)
    require(artifacts, "shadowScene: true", "shadow screenshot marker", failures)
    require(artifacts, "q3dm17", "representative CSM fallback map", failures)
    require(artifacts, "vk-modern-dlight-shadows-csm-fallback-no-world-q3dm17-vk", "Vulkan baseline key", failures)
    require(artifacts, "rc-parity-dlight-shadows-csm-fallback-no-world-q3dm17-glx", "GLx baseline key", failures)
    require(artifacts, "runs[].csmFallbacks", "top-level CSM fallback summary path", failures)
    require(artifacts, "runs[].dlightShadow.csmFallbacks", "nested CSM fallback summary path", failures)
    require(artifacts, "status: passed", "passing CSM fallback summary", failures)
    require(artifacts, "reason coverage", "CSM fallback reason coverage wording", failures)
    require(artifacts, "no-world", "no-world fallback reason", failures)
    require(artifacts, "no-sky-sun", "no-sun fallback reason", failures)
    require(artifacts, "atlas", "atlas-unavailable fallback reason", failures)
    require(artifacts, "zero-cascade", "zero-cascade fallback reason", failures)
    require(artifacts, "csmAtlasScheduled", "CSM atlas schedule zero field", failures)
    require(artifacts, "csmReceiverScheduled", "CSM receiver schedule zero field", failures)
    require(artifacts, "csmPublished", "CSM publication zero field", failures)
    require(artifacts, "csmCascadeCount", "CSM cascade zero field", failures)
    require(artifacts, "csmAtlasWidth", "CSM atlas width zero field", failures)
    require(artifacts, "csmAtlasHeight", "CSM atlas height zero field", failures)
    require(artifacts, "csmGeneration", "CSM generation zero field", failures)
    require(artifacts, "csmFallbackCascades", "CSM fallback cascade zero field", failures)
    require(artifacts, "csmFallbackNoWorld", "no-world fallback counter", failures)
    require(artifacts, "csmFallbackNoSun", "no-sun fallback counter", failures)
    require(artifacts, "csmFallbackAtlasUnavailable", "atlas fallback counter", failures)
    require(artifacts, "csmFallbackZeroCascade", "zero-cascade fallback counter", failures)
    require(artifacts, "noworld", "no-world manager sample", failures)
    require(artifacts, "r_csmDebugFallback", "CSM debug fallback cvar", failures)
    require(artifacts, "r_csmDebugFallback=1", "no-world fallback cvar value", failures)
    require(artifacts, "r_csmDebugFallback=2", "no-sun fallback cvar value", failures)
    require(artifacts, "r_csmDebugFallback=3", "atlas fallback cvar value", failures)
    require(artifacts, "r_csmDebugFallback=4", "zero-cascade fallback cvar value", failures)
    require(artifacts, "scripts/dlight_shadow_release_gate.py", "release-gate script", failures)
    require(artifacts, "csm plan cascades:0 skip", "CSM skip debug log", failures)
    require(artifacts, "RenderDoc inspection", "RenderDoc fallback review boundary", failures)


def check_plan(failures: list[str]) -> None:
    text = PLAN.read_text(encoding="utf-8")
    require(
        text,
        "- [x] Document the expected surfacelight validation artifacts",
        "completed surfacelight artifact plan item",
        failures,
    )
    require(
        text,
        "- [x] Finish atlas-backed surfacelight projection validation",
        "completed surfacelight projection milestone",
        failures,
    )
    require(
        text,
        "- [x] Add combined sidecar/shadow-atlas smoke scenes",
        "completed combined atlas smoke item",
        failures,
    )
    require(
        text,
        "- [x] Update maintainer docs with the CSM/sidecar smoke workflow",
        "completed combined docs item",
        failures,
    )
    require(
        text,
        "- [x] Add fallback smoke coverage for no-sun, no-world, atlas-unavailable, and zero-cascade cases",
        "completed CSM fallback smoke item",
        failures,
    )


def main() -> int:
    failures: list[str] = []
    check_roadmap(failures)
    check_combined_atlas_roadmap(failures)
    check_csm_fallback_roadmap(failures)
    check_plan(failures)

    if failures:
        print("Shadow validation documentation contract violations:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print("Checked surfacelight, combined atlas, and CSM fallback validation documentation contract.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
