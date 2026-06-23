from __future__ import annotations

import argparse
import csv
import json
import math
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_ROOT = ROOT / ".tmp" / "audio-zone-sweeps"


@dataclass(frozen=True)
class SweepOptions:
    tool: Path
    inputs: tuple[Path, ...]
    output_root: Path
    relative_root: Path | None
    override_root: Path | None
    material_map: Path | None
    report_json: Path | None
    report_csv: Path | None
    dry_run: bool
    audit: bool
    strict: bool
    samples: int
    max_zones: int | None


def normalize_path(path: Path) -> Path:
    return path.expanduser().resolve()


def path_is_relative_to(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
    except ValueError:
        return False
    return True


def path_with_exe_suffix(path: Path) -> Path:
    if os.name == "nt" and path.suffix == "":
        return path.with_suffix(".exe")
    return path


def default_tool_path() -> Path:
    env_tool = os.environ.get("FNQL_AUDIOZONESC")
    if env_tool:
        return Path(env_tool)
    return Path("fnql-audiozonesc")


def resolve_tool(tool: Path, dry_run: bool) -> str:
    if tool.parent != Path("."):
        resolved = path_with_exe_suffix(normalize_path(tool))
        if dry_run or resolved.exists():
            return str(resolved)
        raise FileNotFoundError(f"audio zone compiler not found: {resolved}")

    name = path_with_exe_suffix(tool).name
    found = shutil.which(name)
    if found:
        return found
    if dry_run:
        return name
    raise FileNotFoundError(
        f"audio zone compiler '{name}' was not found; pass --tool or set FNQL_AUDIOZONESC"
    )


def discover_bsp_files(inputs: Sequence[Path]) -> list[Path]:
    maps: list[Path] = []
    seen: set[Path] = set()
    for raw in inputs:
        path = normalize_path(raw)
        if path.is_dir():
            candidates = sorted(path.rglob("*.bsp"))
        elif path.is_file() and path.suffix.lower() == ".bsp":
            candidates = [path]
        else:
            raise FileNotFoundError(f"input is not a BSP file or directory: {path}")

        for candidate in candidates:
            resolved = normalize_path(candidate)
            if resolved not in seen:
                seen.add(resolved)
                maps.append(resolved)
    return maps


def best_relative_path(path: Path, relative_root: Path | None) -> Path:
    if relative_root is not None:
        try:
            return path.relative_to(relative_root)
        except ValueError:
            pass
    return Path(path.name)


def output_path_for_bsp(bsp: Path, output_root: Path, relative_root: Path | None) -> Path:
    relative = best_relative_path(bsp, relative_root).with_suffix(".azb")
    return output_root / relative


def validate_sweep_paths(
    maps: Sequence[Path],
    output_root: Path,
    relative_root: Path | None,
    override_root: Path | None,
    material_map: Path | None,
) -> None:
    if relative_root is not None:
        if not relative_root.is_dir():
            raise FileNotFoundError(f"--relative-root is not a directory: {relative_root}")
        outside = [bsp for bsp in maps if not path_is_relative_to(bsp, relative_root)]
        if outside:
            raise ValueError(
                "BSP input is not under --relative-root: "
                + ", ".join(str(path) for path in outside[:4])
            )

    if override_root is not None and not override_root.is_dir():
        raise FileNotFoundError(f"--override-root is not a directory: {override_root}")
    if material_map is not None and not material_map.is_file():
        raise FileNotFoundError(f"--material-map is not a file: {material_map}")

    outputs: dict[Path, Path] = {}
    for bsp in maps:
        output = output_path_for_bsp(bsp, output_root, relative_root)
        previous = outputs.get(output)
        if previous is not None:
            raise ValueError(
                f"multiple BSP inputs would write the same output {output}: "
                f"{previous} and {bsp}; pass --relative-root to preserve subdirectories"
            )
        outputs[output] = bsp


def matching_override_path(
    bsp: Path,
    override_root: Path | None,
    relative_root: Path | None,
) -> Path | None:
    if override_root is None:
        return None

    relative = best_relative_path(bsp, relative_root).with_suffix(".audiozones")
    candidates = [
        override_root / relative,
        override_root / bsp.with_suffix(".audiozones").name,
    ]
    for candidate in candidates:
        if candidate.exists():
            return normalize_path(candidate)
    return None


def build_generate_command(
    tool: str,
    bsp: Path,
    output: Path,
    override: Path | None,
    material_map: Path | None,
    max_zones: int | None,
) -> list[str]:
    command = [tool, "--from-bsp", "-o", str(output)]
    if override is not None:
        command.extend(["--merge", str(override)])
    if material_map is not None:
        command.extend(["--material-map", str(material_map)])
    if max_zones is not None:
        command.extend(["--max-zones", str(max_zones)])
    command.append(str(bsp))
    return command


def build_audit_command(tool: str, azb: Path, strict: bool, samples: int) -> list[str]:
    command = [tool, "--audit"]
    if strict:
        command.append("--strict")
    command.extend(["--samples", str(samples), str(azb)])
    return command


def parse_count_line(text: str) -> dict[str, int]:
    _, _, payload = text.partition(":")
    counts: dict[str, int] = {}
    for part in payload.split():
        name, sep, value = part.partition("=")
        if sep and value.isdigit():
            counts[name] = int(value)
    return counts


def parse_key_values(text: str) -> dict[str, int | float | str]:
    values: dict[str, int | float | str] = {}
    for match in re.finditer(r"(?:^|\s)([A-Za-z][A-Za-z0-9]*)=([^,\s]+)", text):
        key = match.group(1)
        raw = match.group(2)
        values[key] = parse_audit_scalar(raw)
    return values


def parse_audit_scalar(raw: str) -> int | float | str:
    try:
        if any(marker in raw.lower() for marker in (".", "e")):
            parsed_float = float(raw)
            return parsed_float if math.isfinite(parsed_float) else raw
        return int(raw)
    except ValueError:
        return raw


def finite_float(raw: str) -> float | None:
    try:
        parsed = float(raw)
    except ValueError:
        return None
    return parsed if math.isfinite(parsed) else None


def parse_audit_output(stdout: str) -> dict[str, object]:
    summary: dict[str, object] = {
        "warnings": [],
        "presets": {},
        "materials": {},
        "flags": {},
        "portals": {},
        "portalTuning": {},
        "portalCurves": {},
        "overlaps": {},
        "lookup": {},
        "confidence": {},
    }

    for line in stdout.splitlines():
        line = line.strip()
        if line.startswith("version "):
            match = re.search(r"version\s+(\d+),\s+bytes\s+(\d+),\s+zones\s+(\d+)", line)
            if match:
                summary["version"] = int(match.group(1))
                summary["bytes"] = int(match.group(2))
                summary["zones"] = int(match.group(3))
        elif line.startswith("bounds "):
            match = re.search(r"summed volume\s+([0-9.+\-eE]+)", line)
            if match:
                value = finite_float(match.group(1))
                if value is not None:
                    summary["summedVolume"] = value
        elif line.startswith("presets:"):
            summary["presets"] = parse_count_line(line)
        elif line.startswith("materials:"):
            summary["materials"] = parse_count_line(line)
        elif line.startswith("flags "):
            summary["flags"] = parse_key_values(line)
        elif line.startswith("portals "):
            portal_values = parse_key_values(line)
            openness = re.search(r"openness min/avg/max=([0-9.+\-eE]+)/([0-9.+\-eE]+)/([0-9.+\-eE]+)", line)
            if openness:
                openness_values = [finite_float(openness.group(index)) for index in range(1, 4)]
                if all(value is not None for value in openness_values):
                    portal_values["opennessMin"] = openness_values[0]
                    portal_values["opennessAvg"] = openness_values[1]
                    portal_values["opennessMax"] = openness_values[2]
            summary["portals"] = portal_values
        elif line.startswith("portal tuning "):
            tuning_values = parse_key_values(line)
            distance = re.search(r"distance min/avg/max=([0-9.+\-eE]+)/([0-9.+\-eE]+)/([0-9.+\-eE]+)", line)
            max_blend = re.search(r"maxBlend min/avg/max=([0-9.+\-eE]+)/([0-9.+\-eE]+)/([0-9.+\-eE]+)", line)
            if distance:
                distance_values = [finite_float(distance.group(index)) for index in range(1, 4)]
                if all(value is not None for value in distance_values):
                    tuning_values["distanceMin"] = distance_values[0]
                    tuning_values["distanceAvg"] = distance_values[1]
                    tuning_values["distanceMax"] = distance_values[2]
            if max_blend:
                max_blend_values = [finite_float(max_blend.group(index)) for index in range(1, 4)]
                if all(value is not None for value in max_blend_values):
                    tuning_values["maxBlendMin"] = max_blend_values[0]
                    tuning_values["maxBlendAvg"] = max_blend_values[1]
                    tuning_values["maxBlendMax"] = max_blend_values[2]
            summary["portalTuning"] = tuning_values
        elif line.startswith("portal curves:"):
            summary["portalCurves"] = parse_count_line(line)
        elif line.startswith("overlaps "):
            summary["overlaps"] = parse_key_values(line)
        elif line.startswith("lookup profile "):
            summary["lookup"] = parse_key_values(line)
        elif line.startswith("confidence "):
            summary["confidence"] = parse_key_values(line)
        elif line == "warnings none":
            summary["warnings"] = []
        elif line.startswith("warning: "):
            warnings = summary["warnings"]
            assert isinstance(warnings, list)
            warnings.append(line[len("warning: ") :])

    return summary


def run_command(command: Sequence[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        list(command),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


def command_text(command: Sequence[str]) -> str:
    return subprocess.list2cmdline(list(command))


def run_sweep(options: SweepOptions) -> dict[str, object]:
    tool = resolve_tool(options.tool, options.dry_run)
    inputs = tuple(normalize_path(path) for path in options.inputs)
    output_root = normalize_path(options.output_root)
    relative_root = normalize_path(options.relative_root) if options.relative_root else None
    override_root = normalize_path(options.override_root) if options.override_root else None
    material_map = normalize_path(options.material_map) if options.material_map else None
    maps = discover_bsp_files(inputs)
    validate_sweep_paths(maps, output_root, relative_root, override_root, material_map)

    runs: list[dict[str, object]] = []
    for bsp in maps:
        output = output_path_for_bsp(bsp, output_root, relative_root)
        override = matching_override_path(bsp, override_root, relative_root)
        generate_command = build_generate_command(tool, bsp, output, override, material_map, options.max_zones)
        entry: dict[str, object] = {
            "map": str(bsp),
            "output": str(output),
            "override": str(override) if override else "",
            "generateCommand": generate_command,
            "status": "planned" if options.dry_run else "pending",
        }

        if options.dry_run:
            if options.audit:
                entry["auditCommand"] = build_audit_command(tool, output, options.strict, options.samples)
            runs.append(entry)
            continue

        output.parent.mkdir(parents=True, exist_ok=True)
        generated = run_command(generate_command)
        entry["generateReturnCode"] = generated.returncode
        entry["generateStdout"] = generated.stdout
        entry["generateStderr"] = generated.stderr
        if generated.returncode != 0:
            entry["status"] = "generate-failed"
            runs.append(entry)
            continue

        if not options.audit:
            entry["status"] = "generated"
            runs.append(entry)
            continue

        audit_command = build_audit_command(tool, output, options.strict, options.samples)
        audited = run_command(audit_command)
        entry["auditCommand"] = audit_command
        entry["auditReturnCode"] = audited.returncode
        entry["auditStdout"] = audited.stdout
        entry["auditStderr"] = audited.stderr
        entry["audit"] = parse_audit_output(audited.stdout)
        entry["status"] = "passed" if audited.returncode == 0 else "audit-failed"
        runs.append(entry)

    summary = summarize_runs(runs)
    manifest: dict[str, object] = {
        "tool": tool,
        "dryRun": options.dry_run,
        "audit": options.audit,
        "strict": options.strict,
        "samples": options.samples,
        "maxZones": options.max_zones,
        "inputCount": len(inputs),
        "mapCount": len(maps),
        "outputRoot": str(output_root),
        "relativeRoot": str(relative_root) if relative_root else "",
        "overrideRoot": str(override_root) if override_root else "",
        "materialMap": str(material_map) if material_map else "",
        "summary": summary,
        "runs": runs,
    }
    return manifest


def summarize_runs(runs: Sequence[dict[str, object]]) -> dict[str, int]:
    summary = {
        "total": len(runs),
        "planned": 0,
        "passed": 0,
        "generated": 0,
        "generateFailed": 0,
        "auditFailed": 0,
        "warnings": 0,
        "overrides": 0,
    }
    for run in runs:
        status = run.get("status")
        if status == "planned":
            summary["planned"] += 1
        elif status == "passed":
            summary["passed"] += 1
        elif status == "generated":
            summary["generated"] += 1
        elif status == "generate-failed":
            summary["generateFailed"] += 1
        elif status == "audit-failed":
            summary["auditFailed"] += 1
        if run.get("override"):
            summary["overrides"] += 1
        audit = run.get("audit")
        if isinstance(audit, dict):
            warnings = audit.get("warnings")
            if isinstance(warnings, list):
                summary["warnings"] += len(warnings)
    return summary


def compact_counts(counts: object) -> str:
    if not isinstance(counts, dict):
        return ""
    parts = [f"{key}={value}" for key, value in sorted(counts.items()) if value]
    return " ".join(parts)


def write_manifest_json(path: Path, manifest: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")


def csv_cell(value: object) -> object:
    if not isinstance(value, str):
        return value
    if value.lstrip().startswith(("=", "+", "-", "@")):
        return "'" + value
    return value


def write_manifest_csv(path: Path, manifest: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "map",
                "output",
                "override",
                "status",
                "zones",
                "warnings",
                "materials",
                "presets",
                "portals",
                "lookupHits",
                "nsPerSample",
                "confidenceOverall",
                "anomaly",
                "confidenceGrade",
            ],
        )
        writer.writeheader()
        for run in manifest.get("runs", []):
            if not isinstance(run, dict):
                continue
            audit = run.get("audit") if isinstance(run.get("audit"), dict) else {}
            assert isinstance(audit, dict)
            warnings = audit.get("warnings") if isinstance(audit.get("warnings"), list) else []
            lookup = audit.get("lookup") if isinstance(audit.get("lookup"), dict) else {}
            portals = audit.get("portals") if isinstance(audit.get("portals"), dict) else {}
            confidence = audit.get("confidence") if isinstance(audit.get("confidence"), dict) else {}
            row = {
                "map": run.get("map", ""),
                "output": run.get("output", ""),
                "override": run.get("override", ""),
                "status": run.get("status", ""),
                "zones": audit.get("zones", ""),
                "warnings": " | ".join(str(item) for item in warnings),
                "materials": compact_counts(audit.get("materials")),
                "presets": compact_counts(audit.get("presets")),
                "portals": portals.get("total", ""),
                "lookupHits": lookup.get("hits", ""),
                "nsPerSample": lookup.get("nsPerSample", ""),
                "confidenceOverall": confidence.get("overall", ""),
                "anomaly": confidence.get("anomaly", ""),
                "confidenceGrade": confidence.get("grade", ""),
            }
            writer.writerow({key: csv_cell(value) for key, value in row.items()})


def default_report_json(output_root: Path) -> Path:
    return output_root / "audio-zone-sweep.json"


def default_report_csv(output_root: Path) -> Path:
    return output_root / "audio-zone-sweep.csv"


def parse_args(argv: Sequence[str]) -> SweepOptions:
    parser = argparse.ArgumentParser(
        description="Batch-generate and audit FnQL audio-zone sidecars from BSP maps.",
    )
    parser.add_argument("inputs", nargs="+", type=Path, help="BSP files or directories to sweep")
    parser.add_argument(
        "--tool",
        type=Path,
        default=default_tool_path(),
        help="Path to fnql-audiozonesc. Defaults to FNQL_AUDIOZONESC or PATH lookup.",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=DEFAULT_OUTPUT_ROOT,
        help="Root for generated .azb sidecars and default reports.",
    )
    parser.add_argument(
        "--relative-root",
        type=Path,
        help="Root used to preserve map-relative output paths, e.g. baseq3.",
    )
    parser.add_argument(
        "--override-root",
        type=Path,
        help="Root containing matching .audiozones override files.",
    )
    parser.add_argument(
        "--material-map",
        type=Path,
        help="Optional shader material override map passed to BSP generation.",
    )
    parser.add_argument("--report-json", type=Path, help="Manifest JSON output path.")
    parser.add_argument("--report-csv", type=Path, help="Compact CSV report output path.")
    parser.add_argument("--dry-run", action="store_true", help="Plan commands without running the compiler.")
    parser.add_argument("--no-audit", action="store_true", help="Generate sidecars without running --audit.")
    parser.add_argument("--strict", action="store_true", help="Pass --strict to each audit.")
    parser.add_argument("--samples", type=int, default=32768, help="Audit sample count.")
    parser.add_argument("--max-zones", type=int, help="Pass --max-zones to BSP generation.")
    args = parser.parse_args(argv)

    if args.samples < 1 or args.samples > 1_000_000:
        parser.error("--samples must be between 1 and 1000000")
    if args.max_zones is not None and args.max_zones < 1:
        parser.error("--max-zones must be positive")

    return SweepOptions(
        tool=args.tool,
        inputs=tuple(args.inputs),
        output_root=args.output_root,
        relative_root=args.relative_root,
        override_root=args.override_root,
        material_map=args.material_map,
        report_json=args.report_json,
        report_csv=args.report_csv,
        dry_run=args.dry_run,
        audit=not args.no_audit,
        strict=args.strict,
        samples=args.samples,
        max_zones=args.max_zones,
    )


def main(argv: Sequence[str] | None = None) -> int:
    options = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        manifest = run_sweep(options)
    except (FileNotFoundError, ValueError, OSError) as exc:
        print(f"audio_zone_sweep.py: {exc}", file=sys.stderr)
        return 2

    report_json = options.report_json or default_report_json(options.output_root)
    report_csv = options.report_csv or default_report_csv(options.output_root)
    write_manifest_json(report_json, manifest)
    write_manifest_csv(report_csv, manifest)

    summary = manifest["summary"]
    assert isinstance(summary, dict)
    print(
        "audio-zone sweep: "
        f"{summary['total']} maps, "
        f"{summary['planned']} planned, "
        f"{summary['passed']} passed, "
        f"{summary['generated']} generated-only, "
        f"{summary['generateFailed']} generation failures, "
        f"{summary['auditFailed']} audit failures, "
        f"{summary['warnings']} warnings"
    )
    print(f"json: {report_json}")
    print(f"csv: {report_csv}")
    return 1 if summary["generateFailed"] or summary["auditFailed"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
