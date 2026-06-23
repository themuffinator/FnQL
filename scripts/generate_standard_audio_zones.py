from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import zipfile
from pathlib import Path
from typing import Sequence


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WORK_ROOT = ROOT / ".tmp" / "audio-zone-standard-q3a"
DEFAULT_OUTPUT_ROOT = ROOT / "pkg" / "baseq3"
DEFAULT_REPORT_NAME = "audio-zone-sweep"
DEFAULT_MAX_ZONES = 512
MAX_AUDIT_SAMPLES = 1_000_000
OFFICIAL_PAK_RE = re.compile(r"pak[0-9]+\.pk3$", re.IGNORECASE)
ARENA_MAP_RE = re.compile(r'(?:^|\s)map\s+"([^"]+)"')
SAFE_ARENA_MAP_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]*$")


def default_tool_path() -> Path:
    return Path(os.environ.get("FNQL_AUDIOZONESC", "fnql-audiozonesc"))


def discover_official_paks(pak_root: Path) -> tuple[Path, ...]:
    paks = tuple(
        sorted(
            (
                path
                for path in pak_root.iterdir()
                if path.is_file() and OFFICIAL_PAK_RE.fullmatch(path.name)
            ),
            key=lambda path: path.name.lower(),
        )
    )
    if not paks:
        raise FileNotFoundError(f"no official pak*.pk3 files found under {pak_root}")
    return paks


def arena_map_names(text: str) -> tuple[str, ...]:
    names: list[str] = []
    seen: set[str] = set()
    for match in ARENA_MAP_RE.finditer(text):
        name = match.group(1)
        key = name.lower()
        if key not in seen:
            seen.add(key)
            names.append(name)
    return tuple(names)


def validate_arena_map_name(name: str) -> str:
    if not SAFE_ARENA_MAP_RE.fullmatch(name):
        raise ValueError(f"unsafe arena map name: {name!r}")
    return name


def archive_entry_map(archive: zipfile.ZipFile) -> dict[str, str]:
    entries: dict[str, str] = {}
    for entry in archive.namelist():
        key = entry.lower()
        previous = entries.get(key)
        if previous is not None and previous != entry:
            raise ValueError(
                f"{archive.filename} contains case-ambiguous entries: {previous!r} and {entry!r}"
            )
        entries[key] = entry
    return entries


def discover_standard_map_names(paks: Sequence[Path]) -> tuple[str, ...]:
    names: list[str] = []
    seen: set[str] = set()
    for pak in paks:
        with zipfile.ZipFile(pak) as archive:
            entries = archive_entry_map(archive)
            arena_entries = sorted(
                (
                    entry
                    for key, entry in entries.items()
                    if key == "scripts/arenas.txt"
                    or (key.startswith("scripts/") and key.endswith(".arena"))
                ),
                key=str.lower,
            )
            for entry in arena_entries:
                text = archive.read(entry).decode("utf-8", errors="replace")
                for name in arena_map_names(text):
                    validate_arena_map_name(name)
                    key = name.lower()
                    if key not in seen:
                        seen.add(key)
                        names.append(name)
    if not names:
        raise ValueError("official pak metadata did not list any arena maps")
    return tuple(names)


def extract_standard_bsp_files(
    paks: Sequence[Path],
    map_names: Sequence[str],
    source_root: Path,
) -> None:
    maps_dir = source_root / "maps"
    maps_dir.mkdir(parents=True, exist_ok=True)
    for stale in maps_dir.glob("*.bsp"):
        stale.unlink()

    wanted = {name.lower(): validate_arena_map_name(name) for name in map_names}
    found: set[str] = set()
    for pak in paks:
        with zipfile.ZipFile(pak) as archive:
            entries = archive_entry_map(archive)
            for lower_name, name in wanted.items():
                entry = entries.get(f"maps/{lower_name}.bsp")
                if entry is None:
                    continue
                (maps_dir / f"{name}.bsp").write_bytes(archive.read(entry))
                found.add(lower_name)

    missing = [name for name in map_names if name.lower() not in found]
    if missing:
        raise FileNotFoundError("missing BSPs for arena maps: " + ", ".join(missing))


def build_sweep_command(args: argparse.Namespace, source_root: Path) -> list[str]:
    report_json = args.report_json or args.work_root / f"{DEFAULT_REPORT_NAME}.json"
    report_csv = args.report_csv or args.work_root / f"{DEFAULT_REPORT_NAME}.csv"
    command = [
        sys.executable,
        str(ROOT / "scripts" / "audio_zone_sweep.py"),
        "--tool",
        str(args.tool),
        "--relative-root",
        str(source_root),
        "--output-root",
        str(args.output_root),
        "--report-json",
        str(report_json),
        "--report-csv",
        str(report_csv),
        "--samples",
        str(args.samples),
        "--max-zones",
        str(args.max_zones),
    ]
    if args.strict:
        command.append("--strict")
    if args.no_audit:
        command.append("--no-audit")
    command.append(str(source_root / "maps"))
    return command


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Generate tracked FnQL .azb sidecars for standard Quake III "
            "Arena baseq3 maps from official pak*.pk3 files."
        )
    )
    parser.add_argument(
        "pak_root",
        type=Path,
        help="Retail baseq3 directory containing official pak0.pk3 through pak8.pk3.",
    )
    parser.add_argument(
        "--tool",
        type=Path,
        default=default_tool_path(),
        help="Path to fnql-audiozonesc. Defaults to FNQL_AUDIOZONESC or PATH lookup.",
    )
    parser.add_argument(
        "--work-root",
        type=Path,
        default=DEFAULT_WORK_ROOT,
        help="Scratch root for extracted BSPs and default reports.",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=DEFAULT_OUTPUT_ROOT,
        help="Repo output root for generated sidecars.",
    )
    parser.add_argument("--report-json", type=Path, help="Sweep JSON report path.")
    parser.add_argument("--report-csv", type=Path, help="Sweep CSV report path.")
    parser.add_argument("--samples", type=int, default=32768, help="Audit sample count.")
    parser.add_argument(
        "--max-zones",
        type=int,
        default=DEFAULT_MAX_ZONES,
        help="Maximum generated zones per map before compiler coarsening.",
    )
    parser.add_argument("--strict", action="store_true", help="Pass --strict to audits.")
    parser.add_argument("--no-audit", action="store_true", help="Generate without auditing.")
    args = parser.parse_args(argv)
    if args.samples < 1 or args.samples > MAX_AUDIT_SAMPLES:
        parser.error(f"--samples must be between 1 and {MAX_AUDIT_SAMPLES}")
    if args.max_zones < 1:
        parser.error("--max-zones must be positive")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    pak_root = args.pak_root.expanduser().resolve()
    work_root = args.work_root.expanduser().resolve()
    source_root = work_root / "baseq3"

    try:
        paks = discover_official_paks(pak_root)
        map_names = discover_standard_map_names(paks)
        extract_standard_bsp_files(paks, map_names, source_root)
    except (OSError, ValueError, zipfile.BadZipFile) as exc:
        print(f"generate_standard_audio_zones.py: {exc}", file=sys.stderr)
        return 2

    print(f"standard arena maps: {len(map_names)}")
    for name in sorted(map_names):
        print(name)
    sys.stdout.flush()

    command = build_sweep_command(args, source_root)
    return subprocess.call(command, cwd=ROOT)


if __name__ == "__main__":
    raise SystemExit(main())
