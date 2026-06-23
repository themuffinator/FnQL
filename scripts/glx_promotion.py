from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Iterable

from glx_runtime_sweep import (
    GLX_BLOCKING_RELEASE_PLATFORMS,
    GLX_OWNERSHIP_PROOF_VERSION,
    GLX_PRODUCT_TIERS,
    evaluate_gate,
    load_json_file,
    normalize_proof_platform,
    proof_platform_for_manifest,
    run_status,
    validate_release_proof_root,
)


ROOT = Path(__file__).resolve().parents[1]
FEATURE_MATRIX_PATH = ROOT / "docs" / "fnql" / "GLX_FEATURE_MATRIX.md"
FINAL_CONTRACT_PATH = ROOT / "docs" / "fnql" / "GLX_FINAL_CONTRACT.md"
PROMOTION_DOC_PATH = ROOT / "docs" / "fnql" / "GLX_PROMOTION.md"
LEGACY_COUPLING_DOC_PATH = ROOT / "docs" / "fnql" / "GLX_LEGACY_COUPLING.md"
ROLLBACK_PACKAGE_DOC_PATH = ROOT / "docs" / "fnql" / "GLX_ROLLBACK_PACKAGE.md"
MAKEFILE_PATH = ROOT / "Makefile"
MESON_PATH = ROOT / "meson.build"
MESON_OPTIONS_PATH = ROOT / "meson_options.txt"
MSVC_GLX_PROJECT_PATH = ROOT / "code" / "win32" / "msvc2017" / "rendererglx.vcxproj"

PROMOTION_CHECK_VERSION = 1
PROMOTION_REQUIRED_TIERS = ("GL12", "GL2X", "GL3X", "GL41", "GL46")
PROMOTION_MODERN_POST_OUTPUT_TIERS = ("GL3X", "GL41", "GL46")
PROMOTION_MODERN_TIER_DIAGNOSTICS = {
    "GL3X": ("gl3xExecutor", "gl3xSupport", True),
    "GL41": ("gl41Executor", "gl41Support", True),
    "GL46": ("gl46Executor", "gl46Support", False),
}
PROMOTION_REQUIRED_FEATURE_STATUS = "covered"
PROMOTION_VALID_FEATURE_STATUSES = frozenset(
    {"covered", "partially covered", "missing"}
)
PROMOTION_OWNERSHIP_PROFILE = "glx-ownership"
PROMOTION_DOC_REQUIRED_TEXT = (
    "Migration Alias Plan",
    "OpenGL2 Legacy Flag Plan",
    "Rollback Package Contract",
    "Rollback Package Metadata",
    "Legacy Coupling Ledger",
)
PROMOTION_LEGACY_RENDERER_SOURCE_BUDGET = 24
PROMOTION_ROLLBACK_METADATA_VERSION = 1
PROMOTION_ROLLBACK_REQUIRED_ARTIFACTS = (
    "proofCorpus",
    "promotionReport",
    "releaseProofSummary",
    "checksums",
)
PROMOTION_ROLLBACK_REQUIRED_TRIGGER_TERMS = (
    "demo",
    "screenshot",
    "driver",
    "performance",
)
ROLLBACK_PACKAGE_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]*$")


def parse_feature_matrix(path: Path = FEATURE_MATRIX_PATH) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith("| "):
            continue
        columns = [column.strip() for column in line.strip().strip("|").split("|")]
        if len(columns) != 6 or columns[0] == "ID":
            continue
        if all(set(column) <= {"-", ":"} for column in columns):
            continue
        if columns[3] not in PROMOTION_VALID_FEATURE_STATUSES:
            raise ValueError(
                f"GLx feature matrix row {columns[0]!r} has invalid status {columns[3]!r}"
            )
        rows.append(
            {
                "id": columns[0],
                "category": columns[1],
                "feature": columns[2],
                "status": columns[3],
                "evidence": columns[4],
                "closure": columns[5],
            }
        )
    return rows


def check_feature_matrix(path: Path = FEATURE_MATRIX_PATH) -> dict[str, object]:
    rows = parse_feature_matrix(path)
    blockers = [
        {
            "id": row["id"],
            "status": row["status"],
            "feature": row["feature"],
        }
        for row in rows
        if row["status"] != PROMOTION_REQUIRED_FEATURE_STATUS
    ]
    return {
        "name": "feature-matrix-green",
        "status": "passed" if rows and not blockers else "blocked",
        "path": path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path),
        "requiredStatus": PROMOTION_REQUIRED_FEATURE_STATUS,
        "rowCount": len(rows),
        "coveredCount": sum(1 for row in rows if row["status"] == PROMOTION_REQUIRED_FEATURE_STATUS),
        "blockers": blockers,
    }


def check_product_tiers(path: Path = FINAL_CONTRACT_PATH) -> dict[str, object]:
    documented = path.read_text(encoding="utf-8")
    actual_tiers = tuple(sorted(GLX_PRODUCT_TIERS))
    expected_tiers = tuple(sorted(PROMOTION_REQUIRED_TIERS))
    missing_code = sorted(set(expected_tiers).difference(actual_tiers))
    extra_code = sorted(set(actual_tiers).difference(expected_tiers))
    missing_docs = [tier for tier in expected_tiers if f"`{tier}`" not in documented]
    blockers = []
    if missing_code:
        blockers.append("missing code tier(s): " + ", ".join(missing_code))
    if extra_code:
        blockers.append("unexpected code tier(s): " + ", ".join(extra_code))
    if missing_docs:
        blockers.append("missing documented tier(s): " + ", ".join(missing_docs))
    return {
        "name": "five-product-tiers",
        "status": "passed" if not blockers else "blocked",
        "expectedTiers": list(expected_tiers),
        "actualTiers": list(actual_tiers),
        "blockers": blockers,
    }


def _regex_group(path: Path, pattern: str, default: str = "") -> str:
    match = re.search(pattern, path.read_text(encoding="utf-8"), re.MULTILINE | re.DOTALL)
    return match.group(1) if match else default


def check_renderer_source_policy(
    makefile_path: Path = MAKEFILE_PATH,
    meson_path: Path = MESON_PATH,
    meson_options_path: Path = MESON_OPTIONS_PATH,
) -> dict[str, object]:
    make_default = _regex_group(
        makefile_path,
        r"^RENDERER_DEFAULT\s*=\s*([A-Za-z0-9_]+)\s*$",
    )
    meson_default = _regex_group(
        meson_options_path,
        r"option\s*\(\s*'renderer-default'.*?value:\s*'([A-Za-z0-9_]+)'",
    )
    make_use_glx_default = _regex_group(
        makefile_path,
        r"^USE_GLX\s*=\s*([01])\s*$",
    )
    meson_use_glx_default = "glx" if re.search(
        r"option\s*\(\s*'renderers'.*?value:\s*\[[^\]]*'glx'",
        meson_options_path.read_text(encoding="utf-8"),
        re.MULTILINE | re.DOTALL,
    ) and "renderer_prefix + '_glx_'" in meson_path.read_text(encoding="utf-8") else ""
    promoted = any(default and default != "opengl" for default in (make_default, meson_default))
    blockers = []
    if not make_default:
        blockers.append("Makefile renderer default could not be read.")
    if not meson_default:
        blockers.append("Meson renderer default could not be read.")
    if make_use_glx_default != "1":
        blockers.append("Make modular builds must include GLx by default.")
    if meson_use_glx_default != "glx":
        blockers.append("Meson modular builds must include GLx by default.")
    return {
        "name": "renderer-source-policy",
        "status": "promoted" if promoted else ("passed" if not blockers else "blocked"),
        "promoted": promoted,
        "makeDefault": make_default,
        "mesonDefault": meson_default,
        "makeUseGlxDefault": make_use_glx_default,
        "mesonUseGlxDefault": meson_use_glx_default,
        "blockers": blockers,
    }


def renderer_legacy_sources(root: Path = ROOT) -> list[str]:
    return sorted(
        path.relative_to(root).as_posix()
        for path in (root / "code" / "renderer").glob("*.c")
    )


def makefile_glx_legacy_sources(makefile_path: Path = MAKEFILE_PATH) -> list[str]:
    text = makefile_path.read_text(encoding="utf-8")
    match = re.search(
        r"Q3RENDXOBJ\s*=\s*\\\n(?P<body>.*?)(?:\n\s*\n|\nifneq)",
        text,
        re.MULTILINE | re.DOTALL,
    )
    if not match:
        return []

    sources = []
    for stem in re.findall(r"\$\(B\)/rendx/([A-Za-z0-9_]+)\.o", match.group("body")):
        source = ROOT / "code" / "renderer" / f"{stem}.c"
        if source.exists():
            sources.append(source.relative_to(ROOT).as_posix())
    return sorted(set(sources))


def meson_glx_legacy_sources(meson_path: Path = MESON_PATH) -> list[str]:
    text = meson_path.read_text(encoding="utf-8")
    if (
        "renderer_gl_src = [" in text
        and "renderer_prefix + '_glx_'" in text
        and "renderer_gl_src + renderer_glx_src" in text
    ):
        return renderer_legacy_sources()
    return sorted(set(re.findall(r"code/renderer/[A-Za-z0-9_]+\.c", text)))


def msvc_glx_legacy_sources(project_path: Path = MSVC_GLX_PROJECT_PATH) -> list[str]:
    text = project_path.read_text(encoding="utf-8")
    sources = []
    for include in re.findall(
        r'<ClCompile Include="\.\.\\\.\.\\renderer\\([^"\\]+\.c)"',
        text,
    ):
        sources.append(f"code/renderer/{include}")
    return sorted(set(sources))


def parse_legacy_coupling_doc(path: Path = LEGACY_COUPLING_DOC_PATH) -> list[str]:
    if not path.exists():
        return []

    sources = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith("| ") or line.startswith("|---"):
            continue
        columns = [column.strip().strip("`") for column in line.strip().strip("|").split("|")]
        if not columns or columns[0] == "Source":
            continue
        if re.fullmatch(r"code/renderer/[A-Za-z0-9_]+\.c", columns[0]):
            sources.append(columns[0])
    return sorted(set(sources))


def check_legacy_coupling_inventory(
    makefile_path: Path = MAKEFILE_PATH,
    meson_path: Path = MESON_PATH,
    msvc_project_path: Path = MSVC_GLX_PROJECT_PATH,
    doc_path: Path = LEGACY_COUPLING_DOC_PATH,
) -> dict[str, object]:
    build_sources = {
        "makefile": makefile_glx_legacy_sources(makefile_path),
        "meson": meson_glx_legacy_sources(meson_path),
        "msvc": msvc_glx_legacy_sources(msvc_project_path),
    }
    documented_sources = parse_legacy_coupling_doc(doc_path)
    reference_sources = build_sources["meson"]
    blockers: list[str] = []

    if not reference_sources:
        blockers.append("Meson GLx legacy renderer source inventory is empty or unreadable.")

    for name, sources in build_sources.items():
        missing = sorted(set(reference_sources).difference(sources))
        extra = sorted(set(sources).difference(reference_sources))
        if missing:
            blockers.append(f"{name} GLx build is missing reference source(s): " + ", ".join(missing))
        if extra:
            blockers.append(f"{name} GLx build has extra legacy source(s): " + ", ".join(extra))

    undocumented = sorted(set(reference_sources).difference(documented_sources))
    stale = sorted(set(documented_sources).difference(reference_sources))
    if undocumented:
        blockers.append("Legacy coupling ledger is missing source(s): " + ", ".join(undocumented))
    if stale:
        blockers.append("Legacy coupling ledger contains stale source(s): " + ", ".join(stale))
    if len(reference_sources) > PROMOTION_LEGACY_RENDERER_SOURCE_BUDGET:
        blockers.append(
            "GLx legacy renderer source count "
            f"{len(reference_sources)} exceeds ratchet budget "
            f"{PROMOTION_LEGACY_RENDERER_SOURCE_BUDGET}."
        )

    return {
        "name": "legacy-coupling-inventory",
        "status": "passed" if not blockers else "blocked",
        "path": doc_path.relative_to(ROOT).as_posix() if doc_path.is_relative_to(ROOT) else str(doc_path),
        "ratchetBudget": PROMOTION_LEGACY_RENDERER_SOURCE_BUDGET,
        "remainingLegacyRendererSources": len(reference_sources),
        "sources": reference_sources,
        "builds": {
            name: {
                "count": len(sources),
                "sources": sources,
            }
            for name, sources in build_sources.items()
        },
        "documentedCount": len(documented_sources),
        "blockers": blockers,
    }


def _string_list(value: object) -> list[str]:
    if not isinstance(value, list):
        return []
    return [str(item).strip() for item in value if str(item).strip()]


def _safe_rollback_package_name(value: str, label: str, package_id: str) -> str | None:
    if not value:
        return None
    if not ROLLBACK_PACKAGE_ID_RE.fullmatch(value):
        return f"{package_id} has unsafe {label}: {value!r}."
    return None


def _report_relative_path(path: Path) -> str:
    try:
        if path.is_relative_to(ROOT):
            return path.relative_to(ROOT).as_posix()
    except ValueError:
        pass
    return str(path)


def check_rollback_package_metadata(
    metadata_path: Path | None = None,
    required_platforms: Iterable[str] = GLX_BLOCKING_RELEASE_PLATFORMS,
) -> dict[str, object]:
    required_platform_list = [
        normalize_proof_platform(platform_id)
        for platform_id in required_platforms
    ]
    blockers: list[str] = []
    package_records: list[dict[str, object]] = []
    platform_coverage: set[str] = set()

    if metadata_path is None:
        return {
            "name": "rollback-package-metadata",
            "status": "blocked",
            "required": True,
            "requiredPlatforms": required_platform_list,
            "requiredArtifacts": list(PROMOTION_ROLLBACK_REQUIRED_ARTIFACTS),
            "blockers": ["No GLx rollback package metadata was provided."],
        }

    try:
        metadata = load_json_file(metadata_path)
    except Exception as exc:
        return {
            "name": "rollback-package-metadata",
            "status": "blocked",
            "required": True,
            "path": _report_relative_path(metadata_path),
            "requiredPlatforms": required_platform_list,
            "requiredArtifacts": list(PROMOTION_ROLLBACK_REQUIRED_ARTIFACTS),
            "blockers": [f"Could not read GLx rollback package metadata: {exc}"],
        }

    if not isinstance(metadata, dict):
        return {
            "name": "rollback-package-metadata",
            "status": "blocked",
            "required": True,
            "path": _report_relative_path(metadata_path),
            "requiredPlatforms": required_platform_list,
            "requiredArtifacts": list(PROMOTION_ROLLBACK_REQUIRED_ARTIFACTS),
            "blockers": ["GLx rollback package metadata root must be a JSON object."],
        }

    if metadata.get("version") != PROMOTION_ROLLBACK_METADATA_VERSION:
        blockers.append(
            "GLx rollback package metadata version must be "
            f"{PROMOTION_ROLLBACK_METADATA_VERSION}."
        )

    status = str(metadata.get("status", "")).strip().lower()
    if status not in {"ready", "reviewed"}:
        blockers.append("GLx rollback package metadata status must be ready or reviewed.")

    promoted_renderer = str(metadata.get("promotedRenderer", "")).strip().lower()
    alias_renderer = str(metadata.get("aliasRenderer", "")).strip().lower()
    if promoted_renderer != "glx":
        blockers.append("GLx rollback package metadata must name promotedRenderer as glx.")
    if alias_renderer != "opengl":
        blockers.append("GLx rollback package metadata must name aliasRenderer as opengl.")

    migration_instructions = str(metadata.get("migrationInstructions", "")).strip()
    rollback_instructions = str(metadata.get("rollbackInstructions", "")).strip()
    if not migration_instructions:
        blockers.append("GLx rollback package metadata must include migrationInstructions.")
    if not rollback_instructions:
        blockers.append("GLx rollback package metadata must include rollbackInstructions.")

    artifact_flags = metadata.get("requiredArtifacts")
    if not isinstance(artifact_flags, dict):
        blockers.append("GLx rollback package metadata must include requiredArtifacts.")
        artifact_flags = {}
    for artifact_name in PROMOTION_ROLLBACK_REQUIRED_ARTIFACTS:
        if artifact_flags.get(artifact_name) is not True:
            blockers.append(
                "GLx rollback package metadata must confirm required artifact "
                f"{artifact_name}."
            )

    triggers = _string_list(metadata.get("rollbackTriggers"))
    if not triggers:
        blockers.append("GLx rollback package metadata must include rollbackTriggers.")
    trigger_text = "\n".join(trigger.lower() for trigger in triggers)
    for term in PROMOTION_ROLLBACK_REQUIRED_TRIGGER_TERMS:
        if term not in trigger_text:
            blockers.append(
                "GLx rollback package metadata must include a rollback trigger "
                f"covering {term} regressions."
            )

    packages = metadata.get("rollbackPackages", metadata.get("packages", []))
    if not isinstance(packages, list) or not packages:
        blockers.append("GLx rollback package metadata must include rollbackPackages.")
        packages = []

    for index, package in enumerate(packages, start=1):
        if not isinstance(package, dict):
            blockers.append(f"GLx rollback package entry {index} must be a JSON object.")
            continue

        artifact_dir = str(package.get("artifactDir", "")).strip()
        archive = str(package.get("archive", "")).strip()
        package_id = str(package.get("id") or artifact_dir or archive or f"package-{index}").strip()
        package_type = str(package.get("type", package.get("packageType", ""))).strip().lower()
        platforms = _string_list(package.get("platforms"))
        legacy_renderers = [
            renderer.lower()
            for renderer in _string_list(package.get("legacyRenderers"))
        ]
        instructions = str(
            package.get("selectionInstructions")
            or package.get("instructions")
            or ""
        ).strip()

        if package_type != "rollback":
            blockers.append(f"{package_id} must have type rollback.")
        if not artifact_dir and not archive:
            blockers.append(f"{package_id} must name artifactDir or archive.")
        for label, value in (("artifactDir", artifact_dir), ("archive", archive)):
            unsafe = _safe_rollback_package_name(value, label, package_id)
            if unsafe:
                blockers.append(unsafe)
        if "opengl" not in legacy_renderers:
            blockers.append(f"{package_id} must list the legacy opengl renderer.")
        if not instructions:
            blockers.append(f"{package_id} must include legacy renderer selection instructions.")

        normalized_platforms: list[str] = []
        if any(platform.lower() == "all" for platform in platforms):
            normalized_platforms = list(required_platform_list)
        elif not platforms:
            blockers.append(f"{package_id} must name covered platforms.")
        else:
            for platform in platforms:
                try:
                    normalized_platforms.append(normalize_proof_platform(platform))
                except ValueError as exc:
                    blockers.append(f"{package_id} has invalid platform '{platform}': {exc}")

        platform_coverage.update(normalized_platforms)
        package_records.append(
            {
                "id": package_id,
                "type": package_type,
                "artifactDir": artifact_dir,
                "archive": archive,
                "platforms": sorted(set(normalized_platforms)),
                "legacyRenderers": legacy_renderers,
            }
        )

    missing_platforms = sorted(set(required_platform_list).difference(platform_coverage))
    if missing_platforms:
        blockers.append(
            "GLx rollback package metadata does not cover platform(s): "
            + ", ".join(missing_platforms)
        )

    return {
        "name": "rollback-package-metadata",
        "status": "passed" if not blockers else "blocked",
        "required": True,
        "path": _report_relative_path(metadata_path),
        "metadataStatus": status,
        "requiredPlatforms": required_platform_list,
        "coveredPlatforms": sorted(platform_coverage),
        "requiredArtifacts": list(PROMOTION_ROLLBACK_REQUIRED_ARTIFACTS),
        "packageCount": len(package_records),
        "packages": package_records,
        "blockers": blockers,
    }


def check_release_proof_root(proof_root: Path | None) -> dict[str, object]:
    if proof_root is None:
        return {
            "name": "blocking-runtime-proof",
            "status": "blocked",
            "requiredPlatforms": list(GLX_BLOCKING_RELEASE_PLATFORMS),
            "blockers": ["No GLx proof root was provided."],
        }

    try:
        proof = validate_release_proof_root(proof_root)
    except Exception as exc:  # pragma: no cover - defensive CLI surface
        return {
            "name": "blocking-runtime-proof",
            "status": "blocked",
            "root": str(proof_root),
            "blockers": [str(exc)],
        }

    return {
        "name": "blocking-runtime-proof",
        "status": "passed" if proof.get("status") == "passed" else "blocked",
        "root": str(proof_root),
        "requiredPlatforms": proof.get("requiredPlatforms", []),
        "requiredGates": proof.get("requiredGates", []),
        "blockers": proof.get("failures", []),
        "proof": proof,
    }


def manifest_ownership_metrics(manifest: dict[str, object]) -> dict[str, object]:
    evidence = manifest.get("ownershipProofEvidence")
    evidence_found = isinstance(evidence, dict)
    evidence_version = evidence.get("version") if isinstance(evidence, dict) else None
    evidence_status = str(evidence.get("status", "")) if isinstance(evidence, dict) else ""
    evidence_failures = (
        [
            str(failure)
            for failure in evidence.get("failures", [])
            if str(failure).strip()
        ]
        if isinstance(evidence, dict)
        else []
    )

    found = False
    max_calls = 0
    max_items = 0
    diagnostic_failures = 0
    post_output_found = False
    post_output_modes: set[str] = set()
    post_output_post_nodes = 0
    post_output_outputs = 0
    post_output_executable_nodes = 0
    post_output_executable_outputs = 0
    post_output_executable_counts_found = False
    post_output_legacy_fallback = 0
    post_shader_direct_found = False
    post_shader_direct_bound = 0
    post_shader_direct_binds = 0
    post_shader_direct_reject = 0
    product_tiers: set[str] = set()
    modern_tier_diagnostics_found = False
    modern_tier_diagnostics_ok = False

    def int_metric(value: object, default: int = 0) -> int:
        if isinstance(value, bool):
            return default
        try:
            return int(value)
        except (TypeError, ValueError, OverflowError):
            return default

    if isinstance(evidence, dict):
        delegation = evidence.get("delegation")
        if isinstance(delegation, dict):
            found = bool(delegation.get("found", True))
            max_calls = int_metric(delegation.get("calls", 0))
            max_items = int_metric(delegation.get("items", 0))
        diagnostic_failures = int_metric(evidence.get("diagnosticFailures", 0))
        tiers = evidence.get("productTiers", [])
        if isinstance(tiers, list):
            for tier in tiers:
                tier_value = str(tier or "").strip().upper()
                if tier_value in GLX_PRODUCT_TIERS:
                    product_tiers.add(tier_value)
        post_output = evidence.get("postOutputOwnership")
        if isinstance(post_output, dict):
            post_output_found = bool(post_output.get("found"))
            modes = post_output.get("modes", [])
            if isinstance(modes, list):
                post_output_modes.update(str(mode).strip().lower() for mode in modes if str(mode).strip())
            post_output_post_nodes = int_metric(post_output.get("postNodes", 0))
            post_output_outputs = int_metric(post_output.get("outputs", 0))
            post_output_executable_counts_found = bool(post_output.get("executableCountsFound"))
            post_output_executable_nodes = int_metric(post_output.get("executableNodes", 0))
            post_output_executable_outputs = int_metric(post_output.get("executableOutputs", 0))
            post_output_legacy_fallback = int_metric(post_output.get("legacyFallback", 0))
        post_shader_direct = evidence.get("postShaderDirectFinal")
        if isinstance(post_shader_direct, dict):
            post_shader_direct_found = bool(post_shader_direct.get("found"))
            post_shader_direct_bound = int_metric(post_shader_direct.get("bound", 0))
            post_shader_direct_binds = int_metric(post_shader_direct.get("binds", 0))
            post_shader_direct_reject = int_metric(post_shader_direct.get("reject", 0))
        modern_tier_diagnostics_found = bool(evidence.get("modernTierDiagnosticsFound"))
        modern_tier_diagnostics_ok = bool(evidence.get("modernTierDiagnosticsOk"))

        modern_post_output_tier = bool(product_tiers.intersection(PROMOTION_MODERN_POST_OUTPUT_TIERS))
        modern_post_output = (
            post_output_found
            and post_output_modes == {"glx-owned"}
            and post_output_post_nodes > 0
            and post_output_outputs > 0
            and post_output_executable_counts_found
            and post_output_executable_nodes > 0
            and post_output_executable_outputs > 0
            and post_output_legacy_fallback == 0
            and modern_post_output_tier
        )
        zero_delegation = (
            found and max_calls == 0 and max_items == 0 and diagnostic_failures == 0
        )
        return {
            "found": found,
            "calls": max_calls,
            "items": max_items,
            "diagnosticFailures": diagnostic_failures,
            "zeroDelegation": zero_delegation,
            "postOutputFound": post_output_found,
            "postOutputMode": ",".join(sorted(post_output_modes)),
            "postOutputPostNodes": post_output_post_nodes,
            "postOutputOutputs": post_output_outputs,
            "postOutputExecutableCountsFound": post_output_executable_counts_found,
            "postOutputExecutableNodes": post_output_executable_nodes,
            "postOutputExecutableOutputs": post_output_executable_outputs,
            "postOutputLegacyFallback": post_output_legacy_fallback,
            "postShaderDirectFinalFound": post_shader_direct_found,
            "postShaderDirectFinalBound": post_shader_direct_bound,
            "postShaderDirectFinalBinds": post_shader_direct_binds,
            "postShaderDirectFinalReject": post_shader_direct_reject,
            "productTiers": ",".join(sorted(product_tiers)),
            "modernPostOutputTier": modern_post_output_tier,
            "modernTierDiagnosticsFound": modern_tier_diagnostics_found,
            "modernTierDiagnosticsOk": modern_tier_diagnostics_ok,
            "modernPostOutput": modern_post_output,
            "evidenceFound": evidence_found,
            "evidenceVersion": evidence_version,
            "evidenceStatus": evidence_status,
            "evidenceFailures": evidence_failures,
        }

    def record_post_output(
        mode: object,
        post_nodes: object,
        outputs: object,
        legacy_fallback: object,
        executable_nodes: object = None,
        executable_outputs: object = None,
    ) -> None:
        nonlocal post_output_found
        nonlocal post_output_post_nodes
        nonlocal post_output_outputs
        nonlocal post_output_executable_counts_found
        nonlocal post_output_executable_nodes
        nonlocal post_output_executable_outputs
        nonlocal post_output_legacy_fallback

        if (
            mode is None and post_nodes is None and outputs is None and
            legacy_fallback is None and executable_nodes is None and
            executable_outputs is None
        ):
            return
        post_output_found = True
        if mode is not None:
            post_output_modes.add(str(mode).strip().lower())
        post_output_post_nodes = max(post_output_post_nodes, int_metric(post_nodes))
        post_output_outputs = max(post_output_outputs, int_metric(outputs))
        if executable_nodes is not None or executable_outputs is not None:
            post_output_executable_counts_found = True
        post_output_executable_nodes = max(post_output_executable_nodes, int_metric(executable_nodes))
        post_output_executable_outputs = max(post_output_executable_outputs, int_metric(executable_outputs))
        post_output_legacy_fallback = max(post_output_legacy_fallback, int_metric(legacy_fallback))

    def record_post_shader_direct(
        found: object,
        bound: object,
        binds: object,
        reject: object,
    ) -> None:
        nonlocal post_shader_direct_found
        nonlocal post_shader_direct_bound
        nonlocal post_shader_direct_binds
        nonlocal post_shader_direct_reject

        if found is not None:
            post_shader_direct_found = post_shader_direct_found or bool(found)
        if bound is not None:
            post_shader_direct_bound = max(post_shader_direct_bound, int_metric(bound))
        if binds is not None:
            post_shader_direct_binds = max(post_shader_direct_binds, int_metric(binds))
        if reject is not None:
            post_shader_direct_reject = max(post_shader_direct_reject, int_metric(reject))

    def record_tier(value: object) -> None:
        tier = str(value or "").strip().upper()
        if tier in GLX_PRODUCT_TIERS:
            product_tiers.add(tier)

    def record_modern_tier_diagnostics(metrics: dict[str, object]) -> None:
        nonlocal modern_tier_diagnostics_found
        nonlocal modern_tier_diagnostics_ok

        product_tier = metrics.get("productTier")
        tier = ""
        if isinstance(product_tier, dict):
            tier = str(product_tier.get("tier", "")).strip().upper()
        diagnostics = PROMOTION_MODERN_TIER_DIAGNOSTICS.get(tier)
        if not diagnostics:
            return

        executor_name, support_name, require_fbo_post = diagnostics
        executor = metrics.get(executor_name)
        support = metrics.get(support_name)
        if not isinstance(executor, dict) and not isinstance(support, dict):
            return
        modern_tier_diagnostics_found = True

        active = isinstance(executor, dict) and int_metric(executor.get("active")) > 0
        fbo_post = (
            not require_fbo_post
            or (isinstance(executor, dict) and int_metric(executor.get("fboPostProcess")) > 0)
        )
        modern_post = isinstance(support, dict) and int_metric(support.get("modernPostChain")) > 0
        scene_linear = isinstance(support, dict) and int_metric(support.get("sceneLinearOutput")) > 0
        if active and fbo_post and modern_post and scene_linear:
            modern_tier_diagnostics_ok = True

    for run in manifest.get("runs", []):
        if not isinstance(run, dict):
            continue
        diagnostics = run.get("diagnostics")
        if not isinstance(diagnostics, dict):
            metrics = {}
        else:
            failures = diagnostics.get("failures", [])
            if isinstance(failures, list):
                diagnostic_failures += len(failures)
            metrics = diagnostics.get("metrics")
            if not isinstance(metrics, dict):
                metrics = {}
        ownership = metrics.get("ownership")
        if isinstance(ownership, dict):
            found = True
            max_calls = max(max_calls, int_metric(ownership.get("calls", 0)))
            max_items = max(max_items, int_metric(ownership.get("items", 0)))
        product_tier = metrics.get("productTier")
        if isinstance(product_tier, dict):
            record_tier(product_tier.get("tier"))
        record_modern_tier_diagnostics(metrics)
        post_output = metrics.get("postOutputOwnership")
        if isinstance(post_output, dict):
            record_post_output(
                post_output.get("mode"),
                post_output.get("postNodes"),
                post_output.get("outputs"),
                post_output.get("legacyFallback"),
                post_output.get("executableNodes"),
                post_output.get("executableOutputs"),
            )
        post_shader_direct = metrics.get("postShaderDirectFinal")
        if isinstance(post_shader_direct, dict):
            record_post_shader_direct(
                True,
                post_shader_direct.get("bound"),
                post_shader_direct.get("binds"),
                post_shader_direct.get("reject"),
            )
        performance = run.get("performance")
        if isinstance(performance, dict):
            latest = performance.get("latest")
            if isinstance(latest, dict):
                record_post_output(
                    latest.get("postOutputMode"),
                    latest.get("postOutputPostNodes"),
                    latest.get("postOutputOutputs"),
                    latest.get("postOutputLegacyFallback"),
                    latest.get("postOutputExecutableNodes"),
                    latest.get("postOutputExecutableOutputs"),
                )
                if any(
                    latest.get(key) is not None for key in (
                        "postShaderDirectFinalBound",
                        "postShaderDirectFinalBinds",
                        "postShaderDirectFinalReject",
                    )
                ):
                    record_post_shader_direct(
                        True,
                        latest.get("postShaderDirectFinalBound"),
                        latest.get("postShaderDirectFinalBinds"),
                        latest.get("postShaderDirectFinalReject"),
                    )
                record_tier(latest.get("productTier", latest.get("tier")))
    modern_post_output_tier = bool(product_tiers.intersection(PROMOTION_MODERN_POST_OUTPUT_TIERS))
    modern_post_output = (
        post_output_found
        and post_output_modes == {"glx-owned"}
        and post_output_post_nodes > 0
        and post_output_outputs > 0
        and post_output_executable_counts_found
        and post_output_executable_nodes > 0
        and post_output_executable_outputs > 0
        and post_output_legacy_fallback == 0
        and modern_post_output_tier
    )
    return {
        "found": found,
        "calls": max_calls,
        "items": max_items,
        "diagnosticFailures": diagnostic_failures,
        "zeroDelegation": found and max_calls == 0 and max_items == 0 and diagnostic_failures == 0,
        "postOutputFound": post_output_found,
        "postOutputMode": ",".join(sorted(post_output_modes)),
        "postOutputPostNodes": post_output_post_nodes,
        "postOutputOutputs": post_output_outputs,
        "postOutputExecutableCountsFound": post_output_executable_counts_found,
        "postOutputExecutableNodes": post_output_executable_nodes,
        "postOutputExecutableOutputs": post_output_executable_outputs,
        "postOutputLegacyFallback": post_output_legacy_fallback,
        "postShaderDirectFinalFound": post_shader_direct_found,
        "postShaderDirectFinalBound": post_shader_direct_bound,
        "postShaderDirectFinalBinds": post_shader_direct_binds,
        "postShaderDirectFinalReject": post_shader_direct_reject,
        "productTiers": ",".join(sorted(product_tiers)),
        "modernPostOutputTier": modern_post_output_tier,
        "modernTierDiagnosticsFound": modern_tier_diagnostics_found,
        "modernTierDiagnosticsOk": modern_tier_diagnostics_ok,
        "modernPostOutput": modern_post_output,
        "evidenceFound": evidence_found,
        "evidenceVersion": evidence_version,
        "evidenceStatus": evidence_status,
        "evidenceFailures": evidence_failures,
    }


def iter_manifest_paths(root: Path) -> Iterable[Path]:
    if not root.exists():
        return ()
    return root.rglob("manifest.json")


def check_ownership_proof(
    proof_root: Path | None,
    required_platforms: Iterable[str] = GLX_BLOCKING_RELEASE_PLATFORMS,
) -> dict[str, object]:
    required_platform_list = [
        normalize_proof_platform(platform_id)
        for platform_id in required_platforms
    ]
    if proof_root is None:
        return {
            "name": "ownership-proof",
            "status": "blocked",
            "requiredProfile": PROMOTION_OWNERSHIP_PROFILE,
            "requiredPlatforms": required_platform_list,
            "blockers": ["No GLx proof root was provided."],
        }

    platform_records: dict[str, dict[str, object]] = {}
    blockers: list[str] = []
    for path in iter_manifest_paths(proof_root):
        try:
            manifest = load_json_file(path)
        except Exception:
            continue
        if not isinstance(manifest, dict):
            continue
        if manifest.get("profile") != PROMOTION_OWNERSHIP_PROFILE:
            continue
        try:
            platform_id = proof_platform_for_manifest(manifest, path)
        except ValueError:
            continue
        if platform_id not in required_platform_list or manifest.get("dryRun"):
            continue
        if run_status(manifest) != "passed":
            continue
        gate_failures = evaluate_gate(manifest)
        if gate_failures:
            continue
        ownership = manifest_ownership_metrics(manifest)
        record = {
            "path": str(path),
            "runId": manifest.get("runId", ""),
            "ownership": ownership,
        }
        existing = platform_records.get(platform_id)
        if existing is None or str(record["runId"]) > str(existing.get("runId", "")):
            platform_records[platform_id] = record

    for platform_id in required_platform_list:
        record = platform_records.get(platform_id)
        if not record:
            blockers.append(
                f"Missing non-dry-run {PROMOTION_OWNERSHIP_PROFILE} proof for {platform_id}."
            )
            continue
        ownership = record.get("ownership", {})
        if not isinstance(ownership, dict) or not ownership.get("evidenceFound"):
            blockers.append(
                f"{PROMOTION_OWNERSHIP_PROFILE} proof for {platform_id} did not include versioned ownership proof evidence."
            )
            continue
        if isinstance(ownership, dict) and ownership.get("evidenceVersion") != GLX_OWNERSHIP_PROOF_VERSION:
            blockers.append(
                f"{PROMOTION_OWNERSHIP_PROFILE} proof for {platform_id} has unsupported ownership proof evidence version "
                f"{ownership.get('evidenceVersion')!r}."
            )
        if isinstance(ownership, dict) and ownership.get("evidenceStatus") != "passed":
            blockers.append(
                f"{PROMOTION_OWNERSHIP_PROFILE} proof for {platform_id} ownership proof evidence status is "
                f"{ownership.get('evidenceStatus') or '-'}, expected passed."
            )
        if isinstance(ownership, dict) and ownership.get("evidenceFailures"):
            blockers.append(
                f"{PROMOTION_OWNERSHIP_PROFILE} proof for {platform_id} ownership proof evidence failure(s): "
                + "; ".join(str(failure) for failure in ownership.get("evidenceFailures", []))
            )
        if not isinstance(ownership, dict) or not ownership.get("zeroDelegation"):
            blockers.append(
                f"{PROMOTION_OWNERSHIP_PROFILE} proof for {platform_id} did not report zero legacy delegation."
            )
        if not isinstance(ownership, dict) or not ownership.get("modernPostOutput"):
            blockers.append(
                f"{PROMOTION_OWNERSHIP_PROFILE} proof for {platform_id} did not prove executable GLx-owned modern post/output."
            )
        if not isinstance(ownership, dict) or not ownership.get("modernPostOutputTier"):
            blockers.append(
                f"{PROMOTION_OWNERSHIP_PROFILE} proof for {platform_id} did not prove a GL3X+ modern post/output tier."
            )
        if not isinstance(ownership, dict) or not ownership.get("modernTierDiagnosticsOk"):
            blockers.append(
                f"{PROMOTION_OWNERSHIP_PROFILE} proof for {platform_id} did not prove modern post-chain and scene-linear tier diagnostics."
            )

    return {
        "name": "ownership-proof",
        "status": "passed" if not blockers else "blocked",
        "requiredProfile": PROMOTION_OWNERSHIP_PROFILE,
        "requiredPlatforms": required_platform_list,
        "platforms": platform_records,
        "blockers": blockers,
    }


def check_migration_doc(path: Path = PROMOTION_DOC_PATH) -> dict[str, object]:
    blockers: list[str] = []
    if not path.exists():
        return {
            "name": "migration-and-rollback-doc",
            "status": "blocked",
            "path": str(path),
            "blockers": ["GLx promotion migration/rollback document is missing."],
        }
    text = path.read_text(encoding="utf-8")
    missing = [required for required in PROMOTION_DOC_REQUIRED_TEXT if required not in text]
    if missing:
        blockers.append("Missing section(s): " + ", ".join(missing))
    return {
        "name": "migration-and-rollback-doc",
        "status": "passed" if not blockers else "blocked",
        "path": path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path),
        "blockers": blockers,
    }


def promotion_report(
    proof_root: Path | None = None,
    rollback_metadata_path: Path | None = None,
) -> dict[str, object]:
    checks = [
        check_feature_matrix(),
        check_product_tiers(),
        check_legacy_coupling_inventory(),
        check_rollback_package_metadata(rollback_metadata_path),
        check_release_proof_root(proof_root),
        check_ownership_proof(proof_root),
        check_migration_doc(),
    ]
    source_policy = check_renderer_source_policy()
    ready = all(check.get("status") == "passed" for check in checks)
    source_promoted = bool(source_policy.get("promoted"))
    policy_violation = source_promoted and not ready
    status = "ready" if ready else "blocked"
    if policy_violation:
        status = "failed"

    blockers = [
        blocker
        for check in checks
        if check.get("status") != "passed"
        for blocker in check.get("blockers", [])  # type: ignore[union-attr]
    ]
    if policy_violation:
        blockers.append(
            "Renderer defaults were promoted before the GLx promotion gate passed."
        )

    return {
        "version": PROMOTION_CHECK_VERSION,
        "status": status,
        "ready": ready,
        "policyViolation": policy_violation,
        "sourcePolicy": source_policy,
        "checks": checks,
        "blockers": blockers,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Check whether GLx is ready for renderer promotion.")
    parser.add_argument(
        "--proof-root",
        type=Path,
        help="Directory containing reviewed GLx proof manifests.",
    )
    parser.add_argument(
        "--rollback-metadata",
        type=Path,
        help="Reviewed JSON metadata for the GLx legacy-renderer rollback package.",
    )
    parser.add_argument(
        "--require-ready",
        action="store_true",
        help="Return a failing exit code unless every promotion requirement passes.",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Print the full machine-readable promotion report.",
    )
    return parser.parse_args()


def print_text_report(report: dict[str, object]) -> None:
    def format_blocker(blocker: object) -> str:
        if isinstance(blocker, dict) and "id" in blocker:
            return (
                f"{blocker.get('id')}: {blocker.get('status', 'blocked')} "
                f"({blocker.get('feature', '-')})"
            )
        return str(blocker)

    print(f"GLx promotion status: {report['status']}")
    print(f"Policy violation: {str(bool(report.get('policyViolation'))).lower()}")
    for check in report.get("checks", []):
        if not isinstance(check, dict):
            continue
        print(f"- {check.get('name')}: {check.get('status')}")
        blockers = check.get("blockers", [])
        if isinstance(blockers, list):
            for blocker in blockers[:8]:
                print(f"  - {format_blocker(blocker)}")
    source = report.get("sourcePolicy", {})
    if isinstance(source, dict):
        print(
            "- renderer-source-policy: "
            f"{source.get('status')} "
            f"(make={source.get('makeDefault')}, meson={source.get('mesonDefault')}, "
            f"use_glx={source.get('makeUseGlxDefault')}/{source.get('mesonUseGlxDefault')})"
        )


def main() -> int:
    args = parse_args()
    report = promotion_report(args.proof_root, args.rollback_metadata)
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
