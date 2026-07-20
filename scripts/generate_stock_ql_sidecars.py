from __future__ import annotations

import argparse
import math
import os
import shutil
import struct
import subprocess
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))

from fnql_meta import ROOT
from stock_ql_maps import STOCK_QL_MAPS


DEFAULT_WORK_ROOT = ROOT / ".tmp" / "stock-ql-sidecars"
DEFAULT_OUTPUT_ROOT = ROOT / "pkg" / "baseq3"
DEFAULT_MAX_ZONES = 512
DEFAULT_AUDIT_SAMPLES = 32768
SAFE_MAP_CHARS = frozenset(
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-"
)
BSP_IDENT = b"IBSP"
BSP_VERSION_QL = 47
BSP_HEADER_LUMPS = 17
BSP_HEADER_BYTES = 8 + BSP_HEADER_LUMPS * 8
LUMP_MODELS = 7
LUMP_FOGS = 12
LUMP_LIGHTMAPS = 14
DFOG_BYTES = 72
DMODEL_BYTES = 40


@dataclass(frozen=True)
class FogPreset:
    color: tuple[float, float, float]
    density: float
    start: int
    opacity: float
    native_fog_count: int


def safe_map_name(name: str) -> str:
    if not name or name[0] in ".-" or any(char not in SAFE_MAP_CHARS for char in name):
        raise ValueError(f"unsafe stock map name: {name!r}")
    return name.lower()


def resolve_retail_pak(path: Path) -> Path:
    candidate = path.expanduser().resolve()
    if candidate.is_file():
        if candidate.name.lower() != "pak00.pk3":
            raise ValueError(f"expected retail pak00.pk3, got {candidate.name}")
        return candidate
    direct = candidate / "pak00.pk3"
    nested = candidate / "baseq3" / "pak00.pk3"
    if direct.is_file():
        return direct
    if nested.is_file():
        return nested
    raise FileNotFoundError(f"retail Quake Live pak00.pk3 not found under {candidate}")


def archive_entry_map(archive: zipfile.ZipFile) -> dict[str, str]:
    entries: dict[str, str] = {}
    for entry in archive.namelist():
        key = entry.lower()
        previous = entries.get(key)
        if previous is not None and previous != entry:
            raise ValueError(
                f"{archive.filename} contains case-ambiguous entries: "
                f"{previous!r} and {entry!r}"
            )
        entries[key] = entry
    return entries


def discover_stock_bsp_entries(archive: zipfile.ZipFile) -> dict[str, str]:
    entries = archive_entry_map(archive)
    maps: dict[str, str] = {}
    for key, entry in entries.items():
        if not key.startswith("maps/") or not key.endswith(".bsp"):
            continue
        relative = key[len("maps/") : -len(".bsp")]
        if "/" in relative:
            continue
        map_name = safe_map_name(relative)
        maps[map_name] = entry
    if not maps:
        raise ValueError("retail package contains no maps/*.bsp entries")
    return maps


def validate_retail_inventory(entries: dict[str, str]) -> None:
    actual = set(entries)
    expected = set(STOCK_QL_MAPS)
    missing = sorted(expected - actual)
    unexpected = sorted(actual - expected)
    if missing or unexpected:
        details = []
        if missing:
            details.append("missing " + ", ".join(missing[:12]))
        if unexpected:
            details.append("unexpected " + ", ".join(unexpected[:12]))
        raise ValueError("retail stock-map inventory changed: " + "; ".join(details))


def path_is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
    except ValueError:
        return False
    return True


def reset_work_directory(path: Path) -> None:
    resolved = path.resolve()
    allowed = (ROOT / ".tmp").resolve()
    if resolved == allowed or not path_is_relative_to(resolved, allowed):
        raise ValueError(f"work directory must be a child of {allowed}: {resolved}")
    if resolved.exists():
        shutil.rmtree(resolved)
    resolved.mkdir(parents=True)


def extract_stock_bsps(
    archive: zipfile.ZipFile,
    entries: dict[str, str],
    source_root: Path,
) -> None:
    maps_dir = source_root / "maps"
    maps_dir.mkdir(parents=True, exist_ok=True)
    for map_name in STOCK_QL_MAPS:
        (maps_dir / f"{map_name}.bsp").write_bytes(archive.read(entries[map_name]))


def bsp_lumps(data: bytes) -> tuple[tuple[int, int], ...]:
    if len(data) < BSP_HEADER_BYTES:
        raise ValueError("truncated BSP header")
    if data[:4] != BSP_IDENT:
        raise ValueError("invalid BSP magic")
    version = struct.unpack_from("<i", data, 4)[0]
    if version != BSP_VERSION_QL:
        raise ValueError(f"expected Quake Live BSP v47, got v{version}")
    values = struct.unpack_from(f"<{BSP_HEADER_LUMPS * 2}i", data, 8)
    lumps = tuple((values[index], values[index + 1]) for index in range(0, len(values), 2))
    for offset, length in lumps:
        if offset < 0 or length < 0 or offset > len(data) or length > len(data) - offset:
            raise ValueError("BSP lump range is outside the file")
    return lumps


def clamp(value: float, minimum: float, maximum: float) -> float:
    return max(minimum, min(maximum, value))


def lightmap_color(data: bytes, offset: int, length: int) -> tuple[float, float, float]:
    if length < 3:
        return (0.56, 0.58, 0.59)
    end = offset + length - (length % 3)
    pixel_count = (end - offset) // 3
    stride_pixels = max(1, pixel_count // 200000)
    totals = [0, 0, 0]
    accepted = 0
    for position in range(offset, end, stride_pixels * 3):
        red, green, blue = data[position : position + 3]
        if max(red, green, blue) <= 8:
            continue
        totals[0] += red
        totals[1] += green
        totals[2] += blue
        accepted += 1
    if not accepted:
        return (0.56, 0.58, 0.59)

    source = [total / accepted / 255.0 for total in totals]
    average = sum(source) / 3.0
    luminance = 0.2126 * source[0] + 0.7152 * source[1] + 0.0722 * source[2]
    target = clamp(0.52 + (luminance - 0.35) * 0.18, 0.48, 0.62)
    return tuple(clamp(target + (channel - average) * 0.16, 0.44, 0.68) for channel in source)


def fog_preset_from_bsp(data: bytes) -> FogPreset:
    lumps = bsp_lumps(data)
    model_offset, model_length = lumps[LUMP_MODELS]
    if model_length < DMODEL_BYTES:
        raise ValueError("BSP has no world model")
    bounds = struct.unpack_from("<6f", data, model_offset)
    if not all(math.isfinite(value) for value in bounds):
        raise ValueError("BSP world bounds are non-finite")
    extent = max(bounds[index + 3] - bounds[index] for index in range(3))
    extent = clamp(extent, 512.0, 8192.0)

    fog_offset, fog_length = lumps[LUMP_FOGS]
    del fog_offset
    if fog_length % DFOG_BYTES:
        raise ValueError("BSP fog lump has a partial record")
    native_fog_count = fog_length // DFOG_BYTES

    lightmap_offset, lightmap_length = lumps[LUMP_LIGHTMAPS]
    color = lightmap_color(data, lightmap_offset, lightmap_length)
    density = clamp(1.35 / extent, 0.00032, 0.00085)
    start = int(clamp(round((extent * 0.075) / 16.0) * 16.0, 96.0, 384.0))
    opacity = 0.14 if native_fog_count else 0.24
    if native_fog_count:
        density *= 0.65
    return FogPreset(color, density, start, opacity, native_fog_count)


def fog_sidecar_text(map_name: str, preset: FogPreset) -> str:
    native_note = (
        f"; {preset.native_fog_count} authored BSP fog volume(s) detected"
        if preset.native_fog_count
        else ""
    )
    return (
        f"// Subtle opt-in atmospheric depth derived from retail {map_name}.bsp{native_note}.\n"
        f"color {preset.color[0]:.3f} {preset.color[1]:.3f} {preset.color[2]:.3f}\n"
        "mode exp\n"
        f"density {preset.density:.6f}\n"
        f"start {preset.start}\n"
        f"opacity {preset.opacity:.2f}\n"
        "sky 1\n"
    )


def generate_fog_sidecars(source_root: Path, output_root: Path) -> None:
    output_maps = output_root / "maps"
    output_maps.mkdir(parents=True, exist_ok=True)
    for map_name in STOCK_QL_MAPS:
        bsp_path = source_root / "maps" / f"{map_name}.bsp"
        preset = fog_preset_from_bsp(bsp_path.read_bytes())
        (output_maps / f"{map_name}.fog").write_text(
            fog_sidecar_text(map_name, preset),
            encoding="ascii",
            newline="\n",
        )


def generate_audio_sidecars(
    tool: Path,
    source_root: Path,
    output_root: Path,
    work_root: Path,
    *,
    samples: int,
    max_zones: int,
    strict: bool,
    no_audit: bool,
) -> None:
    command = [
        sys.executable,
        str(ROOT / "scripts" / "audio_zone_sweep.py"),
        "--tool",
        str(tool),
        "--relative-root",
        str(source_root),
        "--output-root",
        str(output_root),
        "--report-json",
        str(work_root / "audio-zone-sweep.json"),
        "--report-csv",
        str(work_root / "audio-zone-sweep.csv"),
        "--samples",
        str(samples),
        "--max-zones",
        str(max_zones),
    ]
    if strict:
        command.append("--strict")
    if no_audit:
        command.append("--no-audit")
    command.append(str(source_root / "maps"))
    subprocess.run(command, cwd=ROOT, check=True)


def validate_staged_sidecars(stage_root: Path) -> None:
    maps_dir = stage_root / "maps"
    expected = set(STOCK_QL_MAPS)
    for extension in (".azb", ".fog"):
        actual = {path.stem for path in maps_dir.glob(f"*{extension}") if path.is_file()}
        if actual != expected:
            missing = sorted(expected - actual)
            unexpected = sorted(actual - expected)
            raise ValueError(
                f"staged {extension} coverage mismatch: missing={missing[:8]}, "
                f"unexpected={unexpected[:8]}"
            )


def publish_sidecars(stage_root: Path, output_root: Path, *, prune: bool) -> None:
    source_maps = stage_root / "maps"
    output_maps = output_root / "maps"
    output_maps.mkdir(parents=True, exist_ok=True)
    expected_names = {
        f"{map_name}{extension}"
        for map_name in STOCK_QL_MAPS
        for extension in (".azb", ".fog")
    }
    if prune:
        for extension in (".azb", ".fog"):
            for stale in output_maps.glob(f"*{extension}"):
                if stale.is_file() and stale.name not in expected_names:
                    stale.unlink()
    for name in sorted(expected_names):
        shutil.copy2(source_maps / name, output_maps / name)


def default_tool_path() -> Path:
    configured = os.environ.get("FNQL_AUDIOZONESC")
    if configured:
        return Path(configured)
    local = ROOT / "meson" / "build" / "win32" / "fnql-audiozonesc.exe"
    return local if local.is_file() else Path("fnql-audiozonesc")


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Generate tracked fog and audio-zone sidecars for every BSP in the "
            "legitimate retail Quake Live Steam package."
        )
    )
    parser.add_argument(
        "retail_path",
        type=Path,
        help="Quake Live root, baseq3 directory, or retail pak00.pk3.",
    )
    parser.add_argument("--tool", type=Path, default=default_tool_path())
    parser.add_argument("--work-root", type=Path, default=DEFAULT_WORK_ROOT)
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--samples", type=int, default=DEFAULT_AUDIT_SAMPLES)
    parser.add_argument("--max-zones", type=int, default=DEFAULT_MAX_ZONES)
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--no-audit", action="store_true")
    parser.add_argument(
        "--prune",
        action="store_true",
        help="Remove non-stock .azb/.fog files from the output maps directory.",
    )
    args = parser.parse_args(argv)
    if args.samples < 1 or args.samples > 1_000_000:
        parser.error("--samples must be between 1 and 1000000")
    if args.max_zones < 1 or args.max_zones > 1024:
        parser.error("--max-zones must be between 1 and 1024")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        pak_path = resolve_retail_pak(args.retail_path)
        work_root = args.work_root.expanduser().resolve()
        reset_work_directory(work_root)
        source_root = work_root / "source" / "baseq3"
        stage_root = work_root / "stage" / "baseq3"
        with zipfile.ZipFile(pak_path) as archive:
            entries = discover_stock_bsp_entries(archive)
            validate_retail_inventory(entries)
            extract_stock_bsps(archive, entries, source_root)

        generate_audio_sidecars(
            args.tool,
            source_root,
            stage_root,
            work_root,
            samples=args.samples,
            max_zones=args.max_zones,
            strict=args.strict,
            no_audit=args.no_audit,
        )
        generate_fog_sidecars(source_root, stage_root)
        validate_staged_sidecars(stage_root)
        publish_sidecars(stage_root, args.output_root.expanduser().resolve(), prune=args.prune)
    except (OSError, ValueError, zipfile.BadZipFile, subprocess.CalledProcessError) as exc:
        print(f"generate_stock_ql_sidecars.py: {exc}", file=sys.stderr)
        return 2

    print(
        f"generated {len(STOCK_QL_MAPS)} audio-zone and {len(STOCK_QL_MAPS)} "
        f"fog sidecars from {pak_path}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
