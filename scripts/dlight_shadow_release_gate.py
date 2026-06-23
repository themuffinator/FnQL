from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
GATE_VERSION = 1
RENDERERS = ("glx", "vulkan")
EXPECTED_RUNTIME_GATES = {
    "glx": "rc-parity",
    "vulkan": "vk-modern",
}
SOURCE_DEFAULT_PATHS = {
    "glx": ROOT / "code" / "renderer" / "tr_init.c",
    "vulkan": ROOT / "code" / "renderervk" / "tr_init.c",
}
DLIGHT_SHADOW_DEFAULT_RE = re.compile(
    r'ri\.Cvar_Get\s*\(\s*"r_dlightShadows"\s*,\s*"(?P<default>[01])"',
    re.MULTILINE,
)
PASSED_STATUSES = {"pass", "passed", "ok", "success", "succeeded", "ready", "reviewed"}
VULKAN_SHADER_VARIANTS = ("base", "fog", "line", "line+fog")
REQUIRED_DLIGHT_SHADOW_CATEGORIES = (
    "world-geometry",
    "brush-models",
    "entities",
    "alpha-tested-surfaces",
    "portals-mirrors",
    "stress-light-budget",
    "csm-sky-sun",
    "csm-shimmer-path",
    "surfacelight-large-planar",
    "combined-shadow-atlas",
    "csm-fallback-no-world",
    "csm-fallback-no-sun",
    "csm-fallback-atlas-unavailable",
    "csm-fallback-zero-cascade",
)
REQUIRED_SURFACELIGHT_SPOT_CATEGORIES = (
    "surfacelight-large-planar",
)
REQUIRED_CSM_SHADOW_CATEGORIES = (
    "csm-sky-sun",
)
REQUIRED_RENDERDOC_CHECKS = {
    "glx": (
        "resourceLifetime",
        "depthOnlyFbo",
        "framebufferComplete",
        "reportedAtlasSize",
        "plannedDlightsRendered",
        "cachedTilesPreserved",
        "uncachedTileClearsOnly",
        "nonEmptyFaceTilesOnly",
        "lightingSamplesAtlas",
        "filterKernelMatches",
    ),
    "vulkan": (
        "resourceLifetime",
        "depthAtlasImageUsage",
        "layoutTransitions",
        "depthAtlasDescriptor",
        "plannedDlightsRendered",
        "cachedTilesPreserved",
        "uncachedTileClearsOnly",
        "nonEmptyFaceTilesOnly",
        "lightingSamplesAtlas",
        "filterKernelMatches",
    ),
}


def load_json_file(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain a JSON object.")
    return value


def report_path(path: Path, root: Path = ROOT) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return str(path)


def resolve_path(value: object, base_dir: Path) -> Path:
    path = Path(str(value))
    if path.is_absolute():
        return path
    local_path = (base_dir / path).resolve()
    if local_path.exists():
        return local_path
    root_path = (ROOT / path).resolve()
    if root_path.exists():
        return root_path
    return local_path


def resolve_manifest_path(value: object, base_dir: Path) -> Path:
    raw = str(value)
    path = Path(raw)
    if not path.is_absolute():
        if any(part == ".." for part in path.parts):
            raise ValueError(f"Manifest path must not contain '..': {raw}")
        if any(any(ord(char) < 32 or ord(char) == 127 for char in part) for part in path.parts):
            raise ValueError(f"Manifest path contains an unsafe control character: {raw!r}")
    return resolve_path(value, base_dir)


def status_text(record: object) -> str:
    if isinstance(record, bool):
        return "passed" if record else "blocked"
    if isinstance(record, str):
        return record.strip().lower()
    if isinstance(record, dict):
        for key in ("status", "result", "outcome"):
            value = record.get(key)
            if value is not None:
                return str(value).strip().lower()
    return ""


def status_passed(record: object) -> bool:
    return status_text(record) in PASSED_STATUSES


def section_record(evidence: dict[str, Any], names: tuple[str, ...], renderer: str) -> object:
    for name in names:
        section = evidence.get(name)
        if isinstance(section, dict):
            for key in (renderer, renderer.upper(), renderer.capitalize()):
                if key in section:
                    return section[key]
            if str(section.get("renderer", "")).lower() == renderer:
                return section
        elif isinstance(section, list):
            for item in section:
                if isinstance(item, dict) and str(item.get("renderer", "")).lower() == renderer:
                    return item
    return None


def check_status_record(
    evidence: dict[str, Any],
    names: tuple[str, ...],
    renderer: str,
    label: str,
) -> tuple[dict[str, Any], list[str], object]:
    record = section_record(evidence, names, renderer)
    failures: list[str] = []
    if record is None:
        failures.append(f"Missing {renderer} {label} evidence.")
        return {"status": "blocked", "blockers": failures}, failures, record
    if not status_passed(record):
        failures.append(
            f"{renderer} {label} status is {status_text(record) or '-'}, expected passed."
        )
    return {
        "status": "passed" if not failures else "blocked",
        "recordStatus": status_text(record) or "-",
        "blockers": failures,
    }, failures, record


def variant_name(value: object) -> str:
    if isinstance(value, dict):
        value = value.get("name", value.get("id", ""))
    return str(value).strip().lower()


def passed_variants(record: object) -> set[str]:
    if not isinstance(record, dict):
        return set()
    variants = record.get("variants", record.get("checkedVariants", []))
    if isinstance(variants, dict):
        return {
            str(name).strip().lower()
            for name, value in variants.items()
            if value is True or status_passed(value)
        }
    if isinstance(variants, list):
        return {
            variant_name(item)
            for item in variants
            if not isinstance(item, dict) or status_passed(item) or item.get("passed") is True
        }
    return set()


def shader_failures(renderer: str, record: object) -> list[str]:
    if renderer != "vulkan":
        return []
    variants = passed_variants(record)
    missing = [variant for variant in VULKAN_SHADER_VARIANTS if variant not in variants]
    if missing:
        return [
            "Vulkan shader validation is missing variant(s): " + ", ".join(missing) + "."
        ]
    return []


def load_manifest_value(value: object, base_dir: Path) -> tuple[dict[str, Any] | None, str]:
    if isinstance(value, str):
        path = resolve_manifest_path(value, base_dir)
        return load_json_file(path), report_path(path)
    if isinstance(value, dict):
        manifest_path = (
            value.get("manifest")
            or value.get("manifestPath")
            or value.get("path")
            or value.get("file")
        )
        if manifest_path:
            path = resolve_manifest_path(manifest_path, base_dir)
            return load_json_file(path), report_path(path)
        return value, ""
    return None, ""


def int_value(value: object) -> int:
    if isinstance(value, bool):
        return 0
    try:
        return int(value)  # type: ignore[arg-type]
    except (TypeError, ValueError, OverflowError):
        return 0


def sanitize(value: object) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_.-]+", "-", str(value).strip())
    return cleaned.strip("-")


def screenshot_categories(shots: list[dict[str, Any]]) -> set[str]:
    categories: set[str] = set()
    for shot in shots:
        for category in shot.get("evidenceCategories", []):
            text = str(category).strip()
            if text:
                categories.add(text)
        scene = str(shot.get("scene", "")).strip()
        if scene in REQUIRED_DLIGHT_SHADOW_CATEGORIES:
            categories.add(scene)
    return categories


def shadow_manager_summary_active(summary: object) -> bool:
    if not isinstance(summary, dict) or not summary.get("found"):
        return False
    maximum = summary.get("max")
    if not isinstance(maximum, dict):
        return False
    return (
        int_value(maximum.get("scheduledPasses")) > 0
        and int_value(maximum.get("scheduledMask")) > 0
        and int_value(maximum.get("pointScheduled")) > 0
        and int_value(maximum.get("pointPublished")) > 0
        and int_value(maximum.get("pointPlanned")) > 0
        and int_value(maximum.get("pointRecords")) > 0
        and int_value(maximum.get("pointAtlasWidth")) > 0
        and int_value(maximum.get("pointAtlasHeight")) > 0
        and int_value(maximum.get("pointAtlasFaceSize")) > 0
    )


def shadow_manager_surface_spot_active(summary: object) -> bool:
    if not isinstance(summary, dict) or not summary.get("found"):
        return False
    maximum = summary.get("max")
    if not isinstance(maximum, dict):
        return False
    return (
        int_value(maximum.get("spotScheduled")) > 0
        and int_value(maximum.get("spotPublished")) > 0
        and int_value(maximum.get("spotSurfacePlans")) > 0
        and int_value(maximum.get("spotSurfaceCandidates")) > 0
        and int_value(maximum.get("spotAtlasWidth")) > 0
        and int_value(maximum.get("spotAtlasHeight")) > 0
        and int_value(maximum.get("spotAtlasTileSize")) > 0
    )


def surface_light_spot_summary_active(summary: object) -> bool:
    if not isinstance(summary, dict) or not summary.get("found"):
        return False
    maximum = summary.get("max")
    if not isinstance(maximum, dict):
        return False
    return (
        int_value(maximum.get("surfaceSpotCandidates")) > 0
        and int_value(maximum.get("surfaceSpotPlans")) > 0
        and int_value(maximum.get("surfaceSpotAllocated")) > 0
        and int_value(maximum.get("surfaceSpotAtlasWidth")) > 0
        and int_value(maximum.get("surfaceSpotAtlasHeight")) > 0
        and int_value(maximum.get("surfaceSpotAtlasTileSize")) > 0
        and int_value(maximum.get("surfaceSpotFootprintMax")) > 0
        and int_value(maximum.get("surfaceSpotCasterRadiusMax")) > 0
        and int_value(maximum.get("surfaceSpotTileMax")) > 0
    )


def surface_light_spot_lod_summary_active(summary: object) -> bool:
    return (
        isinstance(summary, dict)
        and bool(summary.get("found"))
        and str(summary.get("status", "")).lower() == "passed"
    )


def shadow_manager_csm_active(summary: object) -> bool:
    if not isinstance(summary, dict) or not summary.get("found"):
        return False
    maximum = summary.get("max")
    if not isinstance(maximum, dict):
        return False
    return (
        int_value(maximum.get("csmAtlasScheduled")) > 0
        and int_value(maximum.get("csmReceiverScheduled")) > 0
        and int_value(maximum.get("csmPublished")) > 0
        and int_value(maximum.get("csmCascadeCount")) > 0
        and int_value(maximum.get("csmAtlasWidth")) > 0
        and int_value(maximum.get("csmAtlasHeight")) > 0
        and int_value(maximum.get("csmGeneration")) > 0
    )


def csm_shadow_runtime_summary_active(summary: object) -> bool:
    return (
        isinstance(summary, dict)
        and bool(summary.get("found"))
        and str(summary.get("status", "")).lower() == "passed"
    )


def csm_fallback_summary_active(summary: object) -> bool:
    return (
        isinstance(summary, dict)
        and bool(summary.get("found"))
        and str(summary.get("status", "")).lower() == "passed"
    )


def runtime_sweep_failures(renderer: str, manifest: dict[str, Any] | None) -> list[str]:
    failures: list[str] = []
    if manifest is None:
        return [f"Missing {renderer} runtime sweep manifest."]

    expected_gate = EXPECTED_RUNTIME_GATES[renderer]
    gate = str(manifest.get("gate", "")).strip()
    if gate != expected_gate:
        failures.append(f"{renderer} runtime sweep gate is {gate or '-'}, expected {expected_gate}.")
    if manifest.get("dryRun"):
        failures.append(f"{renderer} runtime sweep must be a non-dry-run capture.")

    stored_failures = manifest.get("gateFailures", [])
    if isinstance(stored_failures, list):
        failures.extend(
            f"{renderer} runtime sweep gate failure: {failure}"
            for failure in stored_failures
            if str(failure).strip()
        )
    elif stored_failures:
        failures.append(f"{renderer} runtime sweep gateFailures must be a list.")

    runs = manifest.get("runs", [])
    if not isinstance(runs, list) or not runs:
        failures.append(f"{renderer} runtime sweep has no runs.")
        runs = []

    bad_runs = [
        str(run.get("type", "run"))
        for run in runs
        if isinstance(run, dict) and run.get("status") != "passed"
    ]
    if bad_runs:
        failures.append(f"{renderer} runtime sweep has non-passing run(s): {', '.join(bad_runs[:8])}.")

    screenshots = [
        shot
        for run in runs
        if isinstance(run, dict)
        for shot in run.get("screenshots", [])  # type: ignore[union-attr]
        if isinstance(shot, dict)
    ]
    if not screenshots:
        failures.append(f"{renderer} runtime sweep has no screenshot evidence.")
    else:
        missing = [str(shot.get("name", "screenshot")) for shot in screenshots if not shot.get("found")]
        if missing:
            failures.append(
                f"{renderer} runtime sweep is missing screenshot(s): "
                + ", ".join(missing[:8])
                + ("..." if len(missing) > 8 else "")
                + "."
            )

    shadow_runs = [
        run
        for run in runs
        if isinstance(run, dict) and run.get("type") == "dlight-shadow-scenes"
    ]
    if not shadow_runs:
        failures.append(f"{renderer} runtime sweep has no dlight shadow scene run.")
        return failures

    shadow_screenshots = [
        shot
        for run in shadow_runs
        for shot in run.get("screenshots", [])  # type: ignore[union-attr]
        if isinstance(shot, dict) and shot.get("shadowScene")
    ]
    if not shadow_screenshots:
        failures.append(f"{renderer} runtime sweep has no dlight shadow screenshots.")
    else:
        covered = screenshot_categories(shadow_screenshots)
        missing_categories = [
            category
            for category in REQUIRED_DLIGHT_SHADOW_CATEGORIES
            if category not in covered
        ]
        if missing_categories:
            failures.append(
                f"{renderer} dlight shadow screenshots are missing category evidence: "
                + ", ".join(missing_categories)
                + "."
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
        and int_value(summary["max"].get("planned")) > 0  # type: ignore[index]
        and int_value(summary["max"].get("renderLights")) > 0  # type: ignore[index]
    ]
    if not active_logs:
        failures.append(f"{renderer} runtime sweep has no active dlight shadow log samples.")
        return failures

    active_manager_logs = [
        summary.get("shadowManager")
        for summary in active_logs
        if shadow_manager_summary_active(summary.get("shadowManager"))
    ]
    if not active_manager_logs:
        failures.append(
            f"{renderer} runtime sweep has no active shadow manager schedule/publication log samples."
        )

    scene_categories: dict[str, set[str]] = {}
    for shot in shadow_screenshots:
        scene_id = sanitize(shot.get("scene", ""))
        if scene_id:
            scene_categories.setdefault(scene_id, set()).update(
                str(category).strip()
                for category in shot.get("evidenceCategories", [])
                if str(category).strip()
            )

    logged_scenes = {
        sanitize(scene_id)
        for summary in shadow_logs
        if isinstance(summary, dict) and isinstance(summary.get("scenes"), dict)
        for scene_id, scene_summary in summary["scenes"].items()  # type: ignore[union-attr]
        if isinstance(scene_summary, dict)
        and isinstance(scene_summary.get("max"), dict)
        and int_value(scene_summary["max"].get("planned")) > 0  # type: ignore[index]
        and int_value(scene_summary["max"].get("renderLights")) > 0  # type: ignore[index]
    }
    logged_categories = {
        category
        for scene_id in logged_scenes
        for category in scene_categories.get(scene_id, set())
    }
    missing_log_categories = [
        category
        for category in REQUIRED_DLIGHT_SHADOW_CATEGORIES
        if category not in logged_categories
    ]
    if missing_log_categories:
        failures.append(
            f"{renderer} dlight shadow logs are missing category evidence: "
            + ", ".join(missing_log_categories)
            + "."
        )

    if active_manager_logs:
        manager_logged_scenes = {
            sanitize(scene_id)
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
            for category in REQUIRED_DLIGHT_SHADOW_CATEGORIES
            if category not in manager_logged_categories
        ]
        if missing_manager_categories:
            failures.append(
                f"{renderer} shadow manager logs are missing category evidence: "
                + ", ".join(missing_manager_categories)
                + "."
            )

    surface_manager_scenes = {
        sanitize(scene_id)
        for summary in active_logs
        if isinstance(summary, dict)
        and isinstance(summary.get("shadowManager"), dict)
        and isinstance(summary["shadowManager"].get("scenes"), dict)  # type: ignore[index]
        for scene_id, scene_summary in summary["shadowManager"]["scenes"].items()  # type: ignore[index]
        if shadow_manager_surface_spot_active(scene_summary)
    }
    if not surface_manager_scenes:
        failures.append(
            f"{renderer} runtime sweep has no published surfacelight spot atlas manager samples."
        )
    surface_manager_categories = {
        category
        for scene_id in surface_manager_scenes
        for category in scene_categories.get(scene_id, set())
    }
    missing_surface_manager_categories = [
        category
        for category in REQUIRED_SURFACELIGHT_SPOT_CATEGORIES
        if category not in surface_manager_categories
    ]
    if missing_surface_manager_categories:
        failures.append(
            f"{renderer} surfacelight spot atlas manager logs are missing category evidence: "
            + ", ".join(missing_surface_manager_categories)
            + "."
        )

    surface_logged_scenes = {
        sanitize(scene_id)
        for summary in shadow_logs
        if isinstance(summary, dict)
        and isinstance(summary.get("surfaceLightSpot"), dict)
        and isinstance(summary["surfaceLightSpot"].get("scenes"), dict)  # type: ignore[index]
        for scene_id, scene_summary in summary["surfaceLightSpot"]["scenes"].items()  # type: ignore[index]
        if surface_light_spot_summary_active(scene_summary)
    }
    if not surface_logged_scenes:
        failures.append(f"{renderer} runtime sweep has no active surfacelight spot log samples.")
    surface_logged_categories = {
        category
        for scene_id in surface_logged_scenes
        for category in scene_categories.get(scene_id, set())
    }
    missing_surface_categories = [
        category
        for category in REQUIRED_SURFACELIGHT_SPOT_CATEGORIES
        if category not in surface_logged_categories
    ]
    if missing_surface_categories:
        failures.append(
            f"{renderer} surfacelight spot logs are missing category evidence: "
            + ", ".join(missing_surface_categories)
            + "."
        )

    surface_lod_scenes = {
        sanitize(scene_id)
        for summary in shadow_logs
        if isinstance(summary, dict)
        and isinstance(summary.get("surfaceLightSpotLod"), dict)
        and isinstance(summary["surfaceLightSpotLod"].get("scenes"), dict)  # type: ignore[index]
        for scene_id, scene_summary in summary["surfaceLightSpotLod"]["scenes"].items()  # type: ignore[index]
        if surface_light_spot_lod_summary_active(scene_summary)
    }
    if not surface_lod_scenes:
        failures.append(f"{renderer} runtime sweep has no passing surfacelight spot LOD samples.")
    surface_lod_categories = {
        category
        for scene_id in surface_lod_scenes
        for category in scene_categories.get(scene_id, set())
    }
    missing_surface_lod_categories = [
        category
        for category in REQUIRED_SURFACELIGHT_SPOT_CATEGORIES
        if category not in surface_lod_categories
    ]
    if missing_surface_lod_categories:
        failures.append(
            f"{renderer} surfacelight spot LOD logs are missing category evidence: "
            + ", ".join(missing_surface_lod_categories)
            + "."
        )

    csm_manager_scenes = {
        sanitize(scene_id)
        for summary in active_logs
        if isinstance(summary, dict)
        and isinstance(summary.get("shadowManager"), dict)
        and isinstance(summary["shadowManager"].get("scenes"), dict)  # type: ignore[index]
        for scene_id, scene_summary in summary["shadowManager"]["scenes"].items()  # type: ignore[index]
        if shadow_manager_csm_active(scene_summary)
    }
    if not csm_manager_scenes:
        failures.append(
            f"{renderer} runtime sweep has no published CSM atlas manager samples."
        )
    csm_manager_categories = {
        category
        for scene_id in csm_manager_scenes
        for category in scene_categories.get(scene_id, set())
    }
    missing_csm_manager_categories = [
        category
        for category in REQUIRED_CSM_SHADOW_CATEGORIES
        if category not in csm_manager_categories
    ]
    if missing_csm_manager_categories:
        failures.append(
            f"{renderer} CSM shadow manager logs are missing category evidence: "
            + ", ".join(missing_csm_manager_categories)
            + "."
        )

    csm_logged_scenes = {
        sanitize(scene_id)
        for summary in shadow_logs
        if isinstance(summary, dict)
        and isinstance(summary.get("csmShadows"), dict)
        and isinstance(summary["csmShadows"].get("scenes"), dict)  # type: ignore[index]
        for scene_id, scene_summary in summary["csmShadows"]["scenes"].items()  # type: ignore[index]
        if csm_shadow_runtime_summary_active(scene_summary)
    }
    if not csm_logged_scenes:
        failures.append(f"{renderer} runtime sweep has no passing CSM runtime smoke samples.")
    csm_logged_categories = {
        category
        for scene_id in csm_logged_scenes
        for category in scene_categories.get(scene_id, set())
    }
    missing_csm_categories = [
        category
        for category in REQUIRED_CSM_SHADOW_CATEGORIES
        if category not in csm_logged_categories
    ]
    if missing_csm_categories:
        failures.append(
            f"{renderer} CSM runtime logs are missing category evidence: "
            + ", ".join(missing_csm_categories)
            + "."
        )

    csm_fallback_summaries = [
        summary
        for run in shadow_runs
        for summary in (
            run.get("csmFallbacks"),
            run.get("dlightShadow", {}).get("csmFallbacks")
            if isinstance(run.get("dlightShadow"), dict)
            else None,
        )
        if isinstance(summary, dict)
    ]
    if not any(csm_fallback_summary_active(summary) for summary in csm_fallback_summaries):
        failures.append(f"{renderer} runtime sweep has no passing CSM fallback smoke samples.")

    return failures


def check_runtime_sweep(
    evidence: dict[str, Any],
    renderer: str,
    base_dir: Path,
) -> tuple[dict[str, Any], list[str]]:
    value = section_record(evidence, ("runtimeSweeps", "runtime", "sweeps"), renderer)
    try:
        manifest, path = load_manifest_value(value, base_dir)
    except Exception as exc:
        failures = [f"Could not read {renderer} runtime sweep manifest: {exc}"]
        return {"status": "blocked", "blockers": failures}, failures
    failures = runtime_sweep_failures(renderer, manifest)
    record: dict[str, Any] = {
        "status": "passed" if not failures else "blocked",
        "gate": manifest.get("gate") if manifest else "",
        "path": path,
        "blockers": failures,
    }
    return record, failures


def check_renderdoc_record(renderer: str, record: object) -> tuple[dict[str, Any], list[str]]:
    failures: list[str] = []
    if record is None:
        failures.append(f"Missing {renderer} RenderDoc inspection evidence.")
        return {"status": "blocked", "blockers": failures}, failures
    if not isinstance(record, dict):
        failures.append(f"{renderer} RenderDoc inspection evidence must be a JSON object.")
        return {"status": "blocked", "blockers": failures}, failures
    if not status_passed(record):
        failures.append(
            f"{renderer} RenderDoc inspection status is {status_text(record) or '-'}, expected passed."
        )
    capture = (
        record.get("captureFile")
        or record.get("capture")
        or record.get("capturePath")
        or record.get("rdc")
    )
    if not str(capture or "").strip():
        failures.append(f"{renderer} RenderDoc inspection must name a capture file.")

    checks = record.get("checks", record.get("inspection", {}))
    if not isinstance(checks, dict):
        checks = {}
    missing_checks = [
        check
        for check in REQUIRED_RENDERDOC_CHECKS[renderer]
        if not (checks.get(check) is True or status_passed(checks.get(check)))
    ]
    if missing_checks:
        failures.append(
            f"{renderer} RenderDoc inspection is missing passed check(s): "
            + ", ".join(missing_checks)
            + "."
        )
    return {
        "status": "passed" if not failures else "blocked",
        "recordStatus": status_text(record) or "-",
        "captureFile": str(capture or ""),
        "requiredChecks": list(REQUIRED_RENDERDOC_CHECKS[renderer]),
        "blockers": failures,
    }, failures


def source_default_for(path: Path) -> str:
    if not path.exists():
        return ""
    match = DLIGHT_SHADOW_DEFAULT_RE.search(path.read_text(encoding="utf-8"))
    return match.group("default") if match else ""


def source_defaults(source_root: Path) -> dict[str, dict[str, Any]]:
    defaults: dict[str, dict[str, Any]] = {}
    for renderer, path in SOURCE_DEFAULT_PATHS.items():
        local_path = source_root / path.relative_to(ROOT)
        defaults[renderer] = {
            "path": report_path(local_path, source_root),
            "default": source_default_for(local_path),
        }
    return defaults


def source_default_failures(
    defaults: dict[str, dict[str, Any]],
    evidence_ready: bool,
) -> tuple[list[str], bool]:
    failures: list[str] = []
    missing = [
        renderer
        for renderer, record in defaults.items()
        if str(record.get("default", "")) not in {"0", "1"}
    ]
    if missing:
        failures.append(
            "Could not read r_dlightShadows default for renderer(s): "
            + ", ".join(missing)
            + "."
        )

    enabled = [renderer for renderer, record in defaults.items() if record.get("default") == "1"]
    disabled = [renderer for renderer, record in defaults.items() if record.get("default") == "0"]
    if enabled and disabled:
        failures.append(
            "r_dlightShadows default enablement is inconsistent across renderers: "
            + ", ".join(f"{renderer}={defaults[renderer]['default']}" for renderer in RENDERERS)
            + "."
        )

    policy_violation = bool(enabled and not evidence_ready)
    if policy_violation:
        failures.append(
            "r_dlightShadows is enabled by default before the dlight shadow release gate passed."
        )
    return failures, policy_violation


def template_manifest() -> dict[str, Any]:
    return {
        "version": GATE_VERSION,
        "build": {
            "glx": {"status": "passed", "command": "meson compile ... fnql_glx_x86_64"},
            "vulkan": {"status": "passed", "command": "meson compile ... fnql_vulkan_x86_64"},
        },
        "shaders": {
            "glx": {"status": "passed", "notes": "GLx program validation passed."},
            "vulkan": {"status": "passed", "variants": list(VULKAN_SHADER_VARIANTS)},
        },
        "runtimeSweeps": {
            "glx": ".tmp/runtime-sweeps/glx/manifest.json",
            "vulkan": ".tmp/vk-runtime-sweeps/vulkan/manifest.json",
        },
        "renderdoc": {
            renderer: {
                "status": "passed",
                "captureFile": f".tmp/dlight-shadow-tests/captures/{renderer}.rdc",
                "checks": {check: True for check in REQUIRED_RENDERDOC_CHECKS[renderer]},
            }
            for renderer in RENDERERS
        },
    }


def release_gate_report(
    evidence_path: Path | None = None,
    *,
    evidence: dict[str, Any] | None = None,
    source_root: Path = ROOT,
) -> dict[str, Any]:
    if evidence is None:
        if evidence_path is None:
            raise ValueError("Either evidence_path or evidence must be provided.")
        evidence = load_json_file(evidence_path)
    base_dir = evidence_path.resolve().parent if evidence_path is not None else ROOT

    checks: dict[str, Any] = {
        "build": {},
        "shaders": {},
        "runtimeSweeps": {},
        "renderdoc": {},
    }
    evidence_failures: list[str] = []

    for renderer in RENDERERS:
        build_record, failures, _ = check_status_record(
            evidence,
            ("build", "builds"),
            renderer,
            "build",
        )
        checks["build"][renderer] = build_record
        evidence_failures.extend(failures)

        shader_record, failures, shader_source = check_status_record(
            evidence,
            ("shader", "shaders", "shaderValidation"),
            renderer,
            "shader validation",
        )
        extra_shader_failures = shader_failures(renderer, shader_source)
        if extra_shader_failures:
            shader_record["status"] = "blocked"
            shader_record["blockers"].extend(extra_shader_failures)
            failures.extend(extra_shader_failures)
        checks["shaders"][renderer] = shader_record
        evidence_failures.extend(failures)

        runtime_record, failures = check_runtime_sweep(evidence, renderer, base_dir)
        checks["runtimeSweeps"][renderer] = runtime_record
        evidence_failures.extend(failures)

        renderdoc_source = section_record(
            evidence,
            ("renderdoc", "renderDoc", "renderdocInspections", "renderDocInspections"),
            renderer,
        )
        renderdoc_record, failures = check_renderdoc_record(renderer, renderdoc_source)
        checks["renderdoc"][renderer] = renderdoc_record
        evidence_failures.extend(failures)

    defaults = source_defaults(source_root)
    evidence_ready = not evidence_failures
    source_failures, policy_violation = source_default_failures(defaults, evidence_ready)
    all_failures = evidence_failures + source_failures

    status = "ready" if not all_failures else "blocked"
    if policy_violation:
        status = "failed"

    return {
        "version": GATE_VERSION,
        "status": status,
        "ready": status == "ready",
        "defaultEnableAllowed": evidence_ready and not source_failures,
        "policyViolation": policy_violation,
        "evidencePath": report_path(evidence_path) if evidence_path else "",
        "sourceRoot": report_path(source_root),
        "sourceDefaults": defaults,
        "checks": checks,
        "failures": all_failures,
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Check the dlight shadow evidence gate before enabling shadow maps by default."
    )
    parser.add_argument("--evidence", type=Path, help="Reviewed dlight shadow gate evidence JSON.")
    parser.add_argument("--source-root", type=Path, default=ROOT, help="Source tree to inspect.")
    parser.add_argument("--summary", type=Path, help="Write the machine-readable gate report.")
    parser.add_argument("--json", action="store_true", help="Print the full gate report as JSON.")
    parser.add_argument(
        "--require-ready",
        action="store_true",
        help="Return a failing exit code unless every requirement passes.",
    )
    parser.add_argument("--print-template", action="store_true", help="Print an evidence template.")
    args = parser.parse_args(argv)
    if not args.print_template and args.evidence is None:
        parser.error("--evidence is required unless --print-template is used")
    return args


def print_text_report(report: dict[str, Any]) -> None:
    print(f"Dlight shadow default gate: {report['status']}")
    print(f"Default enable allowed: {str(bool(report.get('defaultEnableAllowed'))).lower()}")
    for section_name, section in report.get("checks", {}).items():
        if not isinstance(section, dict):
            continue
        for renderer in RENDERERS:
            record = section.get(renderer, {})
            if isinstance(record, dict):
                print(f"- {section_name}/{renderer}: {record.get('status', '-')}")
    failures = report.get("failures", [])
    if isinstance(failures, list) and failures:
        print("Blockers:")
        for failure in failures[:12]:
            print(f"- {failure}")
        if len(failures) > 12:
            print(f"- ... {len(failures) - 12} more")


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if args.print_template:
        print(json.dumps(template_manifest(), indent=2))
        return 0

    assert args.evidence is not None
    try:
        report = release_gate_report(args.evidence, source_root=args.source_root)
    except Exception as exc:
        print(f"dlight shadow release gate could not run: {exc}", file=sys.stderr)
        return 2

    if args.summary:
        args.summary.parent.mkdir(parents=True, exist_ok=True)
        args.summary.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print_text_report(report)

    if report.get("policyViolation"):
        return 1
    if args.require_ready and report.get("status") != "ready":
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
