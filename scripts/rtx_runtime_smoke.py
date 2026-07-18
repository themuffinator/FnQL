#!/usr/bin/env python3
"""Run a bounded, windowed FnQL RTX smoke test against retail Quake Live.

The probe is deliberately asset-agnostic: the caller supplies a retail map
name and the engine resolves it through the legitimate Steam installation.
Nothing is copied into or written beneath that installation.  Each profile
gets a new ``fs_homepath`` below ``.tmp`` (or ``--output-dir``), and the probe
never connects to an online server or manufactures Steam authentication.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shlex
import struct
import subprocess
import sys
import time
import zlib
from datetime import datetime, timezone
from pathlib import Path
from typing import Sequence


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_DIR = ROOT / ".tmp" / "rtx-runtime-smoke"
RETAIL_PATH_ENV = "FNQL_RETAIL_QL_PATH"
QUAKE_LIVE_APP_ID = "282440"
MAP_NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]*$")
GAME_TYPE_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]*$")
RTX_MODE_RE = re.compile(
    r"rtx_rt_mode:\s*requested=(?P<requested>\d+)\s+"
    r"active=(?P<active>\d+)\s+require=(?P<require>\d+)",
    re.IGNORECASE,
)
RTX_CAPABILITY_RE = re.compile(
    r"RTX capability gate:\s*requested=[A-Za-z0-9_]+\s+"
    r"\((?P<requested>\d+)\),\s*active=[A-Za-z0-9_]+\s+"
    r"\((?P<active>\d+)\),\s*require=(?P<require>\d+)",
    re.IGNORECASE,
)
FAILURE_PATTERNS = (
    ("fatal", re.compile(r"\bERR_FATAL\b|\bfatal(?: error)?\b|\brecursive error\b", re.I)),
    ("vulkan", re.compile(r"\bVK_ERROR_[A-Z0-9_]+\b|\bdevice[- ]?lost\b", re.I)),
    ("validation", re.compile(r"\bVUID-[A-Za-z0-9_.-]+\b|\bvalidation error\b", re.I)),
    ("renderer", re.compile(r"failed to load.*(?:rtx|renderer)|no renderer loaded", re.I)),
)
PROFILE_SPECS = {
    "raster": {
        "description": "RTX module with hardware ray tracing disabled",
        "requested": 0,
        "require": 0,
    },
    "native": {
        "description": "strict native ray-tracing pipeline",
        "requested": 2,
        "require": 1,
    },
}
PROFILE_CHOICES = ("raster", "native", "all")
MAX_STARTUP_COMMANDS = 32
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PNG_MAX_FILE_BYTES = 64 * 1024 * 1024
PNG_MAX_PIXELS = 64 * 1024 * 1024
PNG_MAX_CHUNKS = 4096
PNG_MIN_LUMINANCE_RANGE = 8.0
PNG_MIN_LUMINANCE_VARIANCE = 4.0
PNG_MAX_NEAR_WHITE_FRACTION = 0.25
PNG_MAX_NEAR_BLACK_FRACTION = 0.85
PNG_COLOR_CHANNELS = {
    0: 1,  # grayscale
    2: 3,  # RGB
    4: 2,  # grayscale + alpha
    6: 4,  # RGBA
}
HASH_BLOCK_BYTES = 1024 * 1024


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Run FnQL's optional RTX renderer against a legitimate retail "
            "Quake Live Steam installation."
        )
    )
    parser.add_argument("--exe", type=Path, help="FnQL client executable to launch")
    parser.add_argument(
        "--retail-root",
        type=Path,
        help=f"Quake Live Steam installation (or set {RETAIL_PATH_ENV})",
    )
    parser.add_argument(
        "--map",
        dest="map_name",
        help="retail Quake Live map name to load; intentionally has no implicit default",
    )
    parser.add_argument("--gametype", default="ffa", help="retail game type for devmap")
    parser.add_argument("--profile", choices=PROFILE_CHOICES, default="raster")
    parser.add_argument("--width", type=int, default=960)
    parser.add_argument("--height", type=int, default=540)
    parser.add_argument("--map-wait", type=int, default=600, help="wait frames before capture")
    parser.add_argument("--capture-wait", type=int, default=30, help="wait frames after capture")
    parser.add_argument("--timeout", type=float, default=180.0, help="seconds per profile")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument(
        "--plan",
        action="store_true",
        help="print the launch plan without creating files or starting FnQL",
    )
    return parser


def conventional_retail_roots() -> list[Path]:
    candidates: list[Path] = []
    for variable in ("ProgramFiles(x86)", "ProgramFiles"):
        value = os.environ.get(variable, "").strip()
        if value:
            candidates.append(Path(value) / "Steam" / "steamapps" / "common" / "Quake Live")

    home = Path.home()
    candidates.extend(
        (
            home / ".local" / "share" / "Steam" / "steamapps" / "common" / "Quake Live",
            home / ".steam" / "steam" / "steamapps" / "common" / "Quake Live",
        )
    )
    return candidates


def resolve_retail_root(explicit: Path | None) -> Path:
    if explicit is not None:
        return explicit.expanduser().resolve()

    configured = os.environ.get(RETAIL_PATH_ENV, "").strip()
    if configured:
        return Path(configured).expanduser().resolve()

    for candidate in conventional_retail_roots():
        if candidate.is_dir():
            return candidate.resolve()

    raise ValueError(
        "retail Quake Live was not found; pass --retail-root or set "
        f"{RETAIL_PATH_ENV}"
    )


def validate_retail_install(root: Path) -> None:
    if not root.is_dir():
        raise ValueError(f"retail root does not exist or is not a directory: {root}")

    app_id_path = root / "steam_appid.txt"
    if not app_id_path.is_file():
        raise ValueError(f"retail root is missing Steam identity file: {app_id_path}")
    if app_id_path.stat().st_size > 64:
        raise ValueError(f"retail Steam identity file is unexpectedly large: {app_id_path}")
    app_id = app_id_path.read_text(encoding="ascii", errors="replace").strip()
    if app_id != QUAKE_LIVE_APP_ID:
        raise ValueError(
            f"Steam identity mismatch at {app_id_path}: expected {QUAKE_LIVE_APP_ID}, got {app_id!r}"
        )

    required_markers = (root / "quakelive_steam.exe", root / "web.pak")
    missing = [str(path) for path in required_markers if not path.is_file()]
    if missing:
        raise ValueError(
            "the selected path is not a complete retail Quake Live Steam install; "
            "missing: " + ", ".join(missing)
        )


def validate_options(args: argparse.Namespace) -> None:
    if args.exe is None:
        raise ValueError("--exe is required")
    if not args.plan and not args.exe.expanduser().is_file():
        raise ValueError(f"FnQL executable does not exist: {args.exe}")
    if not args.map_name:
        raise ValueError("--map is required; the smoke test never guesses a retail asset")
    if not MAP_NAME_RE.fullmatch(args.map_name):
        raise ValueError("--map must be a simple retail map name, not a path or command")
    if not GAME_TYPE_RE.fullmatch(args.gametype):
        raise ValueError("--gametype must be a simple game-type name")
    if not 320 <= args.width <= 16384 or not 240 <= args.height <= 16384:
        raise ValueError("--width/--height must describe a bounded window")
    if args.width * args.height > PNG_MAX_PIXELS:
        raise ValueError(
            f"--width/--height exceed the {PNG_MAX_PIXELS}-pixel evidence limit"
        )
    if args.map_wait < 1 or args.capture_wait < 1:
        raise ValueError("wait counts must be positive")
    if not 1.0 <= args.timeout <= 3600.0:
        raise ValueError("--timeout must be between 1 and 3600 seconds")


def selected_profiles(choice: str) -> tuple[str, ...]:
    return tuple(PROFILE_SPECS) if choice == "all" else (choice,)


def profile_cvars(profile_name: str, width: int, height: int) -> dict[str, str]:
    spec = PROFILE_SPECS[profile_name]
    return {
        "cl_allowDownload": "0",
        "cl_renderer": "rtx",
        "com_introplayed": "1",
        "developer": "1",
        "logfile": "2",
        "r_customHeight": str(height),
        "r_customWidth": str(width),
        "r_fogMode": "0",
        "r_fullscreen": "0",
        "r_globalFog": "0",
        "r_hdr": "0",
        "r_liquid": "0",
        "r_mode": "-1",
        "r_surfaceLightProxies": "0",
        "rtx_rt_async_overlap": "0",
        "rtx_rt_dynamic_blas": "0",
        "rtx_rt_material_heuristics": "0",
        "rtx_rt_mode": str(spec["requested"]),
        "rtx_rt_require": str(spec["require"]),
        "s_initsound": "0",
    }


def build_launch_command(
    exe: Path,
    retail_root: Path,
    homepath: Path,
    profile_name: str,
    map_name: str,
    gametype: str,
    width: int,
    height: int,
    map_wait: int,
    capture_wait: int,
) -> tuple[list[str], str]:
    screenshot_name = f"fnql-rtx-{profile_name}-{map_name}"
    command = [
        str(exe),
        "+set", "fs_basepath", str(retail_root),
        "+set", "fs_steampath", str(retail_root),
        "+set", "fs_homepath", str(homepath),
    ]
    for name, value in profile_cvars(profile_name, width, height).items():
        command.extend(("+set", name, value))
    command.extend(
        (
            "+devmap", map_name, gametype,
            "+wait", str(map_wait),
            "+gfxinfo",
            "+vkinfo",
            "+screenshotPNG", screenshot_name,
            "+wait", str(capture_wait),
            "+quit",
        )
    )
    command_count = sum(argument.startswith("+") for argument in command)
    if command_count > MAX_STARTUP_COMMANDS:
        raise ValueError(
            f"startup command count {command_count} exceeds engine limit {MAX_STARTUP_COMMANDS}"
        )
    return command, screenshot_name


def command_to_string(command: Sequence[str]) -> str:
    return subprocess.list2cmdline(command) if os.name == "nt" else shlex.join(command)


def file_evidence(path: Path) -> dict[str, object]:
    """Return stable, streaming file identity without loading the file into memory."""
    resolved = path.expanduser().resolve()
    evidence: dict[str, object] = {
        "path": str(resolved),
        "exists": False,
        "fileBytes": None,
        "sha256": None,
    }
    try:
        before = resolved.stat()
        if not resolved.is_file():
            evidence["error"] = "path is not a regular file"
            return evidence
        digest = hashlib.sha256()
        bytes_read = 0
        with resolved.open("rb") as stream:
            while block := stream.read(HASH_BLOCK_BYTES):
                digest.update(block)
                bytes_read += len(block)
        after = resolved.stat()
    except OSError as error:
        evidence["error"] = f"{type(error).__name__}: {error}"
        return evidence

    if (
        before.st_size != after.st_size
        or before.st_mtime_ns != after.st_mtime_ns
        or bytes_read != after.st_size
    ):
        evidence["error"] = "file changed while it was being hashed"
        return evidence
    evidence.update(
        {
            "exists": True,
            "fileBytes": bytes_read,
            "sha256": digest.hexdigest(),
        }
    )
    return evidence


def platform_evidence() -> dict[str, object]:
    """Capture useful, non-secret host facts for reproducible smoke evidence."""
    return {
        "osName": os.name,
        "sysPlatform": sys.platform,
        "system": platform.system(),
        "release": platform.release(),
        "version": platform.version(),
        "machine": platform.machine(),
        "architecture": platform.architecture()[0],
        "pythonImplementation": platform.python_implementation(),
        "pythonVersion": platform.python_version(),
    }


def find_logs(homepath: Path) -> list[Path]:
    return sorted(path for path in homepath.rglob("qconsole.log") if path.is_file())


def find_screenshots(homepath: Path, screenshot_name: str) -> list[Path]:
    return sorted(
        path
        for path in homepath.rglob(f"{screenshot_name}*")
        if path.is_file() and path.suffix.lower() == ".png"
    )


def _paeth_predictor(left: int, above: int, upper_left: int) -> int:
    estimate = left + above - upper_left
    left_distance = abs(estimate - left)
    above_distance = abs(estimate - above)
    upper_left_distance = abs(estimate - upper_left)
    if left_distance <= above_distance and left_distance <= upper_left_distance:
        return left
    if above_distance <= upper_left_distance:
        return above
    return upper_left


def _decode_png(path: Path) -> dict[str, object]:
    """Decode the bounded screenshot subset of PNG and collect evidence metrics."""
    resolved = path.expanduser().resolve()
    base: dict[str, object] = {
        "path": str(resolved),
        "structurallyValid": False,
    }

    def fail(message: str) -> dict[str, object]:
        return {**base, "error": message}

    try:
        before = resolved.stat()
        if not resolved.is_file():
            return fail("path is not a regular file")
        if before.st_size > PNG_MAX_FILE_BYTES:
            return fail(
                f"PNG exceeds the {PNG_MAX_FILE_BYTES}-byte smoke-evidence limit"
            )
        data = resolved.read_bytes()
        after = resolved.stat()
    except OSError as error:
        return fail(f"{type(error).__name__}: {error}")

    base["fileBytes"] = len(data)
    if (
        before.st_size != after.st_size
        or before.st_mtime_ns != after.st_mtime_ns
        or len(data) != after.st_size
    ):
        return fail("PNG changed while it was being read")
    base["sha256"] = hashlib.sha256(data).hexdigest()
    if not data.startswith(PNG_SIGNATURE):
        return fail("invalid PNG signature")

    offset = len(PNG_SIGNATURE)
    chunk_count = 0
    chunk_names: list[str] = []
    ihdr: tuple[int, int, int, int, int, int, int] | None = None
    idat_parts: list[bytes] = []
    saw_idat = False
    ended_idat = False
    saw_iend = False
    while offset < len(data):
        if chunk_count >= PNG_MAX_CHUNKS:
            return fail(f"PNG exceeds the {PNG_MAX_CHUNKS}-chunk limit")
        if len(data) - offset < 12:
            return fail("truncated PNG chunk framing")
        chunk_length = struct.unpack_from(">I", data, offset)[0]
        chunk_type = data[offset + 4 : offset + 8]
        chunk_end = offset + 12 + chunk_length
        if chunk_end > len(data):
            return fail("truncated PNG chunk payload")
        if not all(65 <= value <= 90 or 97 <= value <= 122 for value in chunk_type):
            return fail("invalid PNG chunk type")
        chunk_data = data[offset + 8 : offset + 8 + chunk_length]
        stored_crc = struct.unpack_from(">I", data, offset + 8 + chunk_length)[0]
        computed_crc = zlib.crc32(chunk_data, zlib.crc32(chunk_type)) & 0xFFFFFFFF
        chunk_name = chunk_type.decode("ascii")
        if stored_crc != computed_crc:
            return fail(f"CRC mismatch in {chunk_name} chunk")
        if chunk_count == 0 and chunk_type != b"IHDR":
            return fail("IHDR is not the first PNG chunk")

        chunk_names.append(chunk_name)
        if chunk_type == b"IHDR":
            if ihdr is not None or chunk_length != 13:
                return fail("invalid or duplicate IHDR chunk")
            ihdr = struct.unpack(">IIBBBBB", chunk_data)
        elif chunk_type == b"IDAT":
            if ihdr is None:
                return fail("IDAT precedes IHDR")
            if ended_idat:
                return fail("PNG IDAT chunks are not consecutive")
            saw_idat = True
            idat_parts.append(chunk_data)
        elif chunk_type == b"IEND":
            if chunk_length != 0:
                return fail("IEND chunk is not empty")
            saw_iend = True
            offset = chunk_end
            chunk_count += 1
            if offset != len(data):
                return fail("trailing bytes after IEND")
            break
        elif (chunk_type[0] & 0x20) == 0 and chunk_type != b"PLTE":
            return fail(f"unsupported critical PNG chunk {chunk_name}")
        elif saw_idat:
            ended_idat = True
        offset = chunk_end
        chunk_count += 1

    if ihdr is None:
        return fail("IHDR chunk was not found")
    if not idat_parts:
        return fail("IDAT chunk was not found")
    if not saw_iend:
        return fail("IEND chunk was not found")

    width, height, bit_depth, color_type, compression, filter_method, interlace = ihdr
    base.update(
        {
            "width": width,
            "height": height,
            "bitDepth": bit_depth,
            "colorType": color_type,
            "compressionMethod": compression,
            "filterMethod": filter_method,
            "interlaceMethod": interlace,
            "chunkCount": chunk_count,
            "chunks": chunk_names,
        }
    )
    if width <= 0 or height <= 0:
        return fail("PNG dimensions must be positive")
    if width * height > PNG_MAX_PIXELS:
        return fail(f"PNG exceeds the {PNG_MAX_PIXELS}-pixel smoke-evidence limit")
    if bit_depth != 8:
        return fail(f"unsupported PNG bit depth {bit_depth}; expected 8")
    channels = PNG_COLOR_CHANNELS.get(color_type)
    if channels is None:
        return fail(f"unsupported PNG color type {color_type}")
    if compression != 0 or filter_method != 0 or interlace != 0:
        return fail("unsupported PNG compression, filter, or interlace method")

    stride = width * channels
    expected_decoded_bytes = height * (stride + 1)
    compressed = b"".join(idat_parts)
    try:
        decompressor = zlib.decompressobj()
        decoded = decompressor.decompress(compressed, expected_decoded_bytes + 1)
        if decompressor.unconsumed_tail:
            return fail("PNG decompressed data exceeds the expected dimensions")
        decoded += decompressor.flush()
    except zlib.error as error:
        return fail(f"invalid PNG zlib stream: {error}")
    if not decompressor.eof or decompressor.unused_data:
        return fail("PNG contains an incomplete or trailing zlib stream")
    if len(decoded) != expected_decoded_bytes:
        return fail(
            "PNG decoded byte count does not match IHDR dimensions "
            f"({len(decoded)} != {expected_decoded_bytes})"
        )

    previous = bytearray(stride)
    decoded_offset = 0
    filters: set[int] = set()
    luminance_min = 255.0
    luminance_max = 0.0
    luminance_sum = 0.0
    luminance_square_sum = 0.0
    near_black_count = 0
    near_white_count = 0
    for _ in range(height):
        filter_type = decoded[decoded_offset]
        decoded_offset += 1
        if filter_type > 4:
            return fail(f"unsupported PNG scanline filter {filter_type}")
        filters.add(filter_type)
        filtered = decoded[decoded_offset : decoded_offset + stride]
        decoded_offset += stride
        reconstructed = bytearray(stride)
        for index, value in enumerate(filtered):
            left = reconstructed[index - channels] if index >= channels else 0
            above = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            if filter_type == 0:
                predictor = 0
            elif filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = above
            elif filter_type == 3:
                predictor = (left + above) // 2
            else:
                predictor = _paeth_predictor(left, above, upper_left)
            reconstructed[index] = (value + predictor) & 0xFF

        for pixel_offset in range(0, stride, channels):
            if color_type in (0, 4):
                red = green = blue = reconstructed[pixel_offset]
                alpha = reconstructed[pixel_offset + 1] if color_type == 4 else 255
            else:
                red, green, blue = reconstructed[pixel_offset : pixel_offset + 3]
                alpha = reconstructed[pixel_offset + 3] if color_type == 6 else 255
            luminance = (0.2126 * red + 0.7152 * green + 0.0722 * blue)
            if alpha != 255:
                luminance *= alpha / 255.0
            luminance_min = min(luminance_min, luminance)
            luminance_max = max(luminance_max, luminance)
            luminance_sum += luminance
            luminance_square_sum += luminance * luminance
            if luminance <= 5.0:
                near_black_count += 1
            if luminance >= 250.0:
                near_white_count += 1
        previous = reconstructed

    pixel_count = width * height
    luminance_mean = luminance_sum / pixel_count
    luminance_variance = max(
        0.0, luminance_square_sum / pixel_count - luminance_mean * luminance_mean
    )
    return {
        **base,
        "structurallyValid": True,
        "error": None,
        "channels": channels,
        "crcValidated": True,
        "compressedBytes": len(compressed),
        "decodedBytes": len(decoded),
        "pixelCount": pixel_count,
        "scanlineFilters": sorted(filters),
        "luminanceMin": round(luminance_min, 6),
        "luminanceMax": round(luminance_max, 6),
        "luminanceMean": round(luminance_mean, 6),
        "luminanceVariance": round(luminance_variance, 6),
        "luminanceRange": round(luminance_max - luminance_min, 6),
        "nearBlackPixels": near_black_count,
        "nearBlackFraction": round(near_black_count / pixel_count, 6),
        "nearWhitePixels": near_white_count,
        "nearWhiteFraction": round(near_white_count / pixel_count, 6),
    }


def inspect_png(
    path: Path,
    expected_width: int | None = None,
    expected_height: int | None = None,
) -> dict[str, object]:
    metrics = _decode_png(path)
    metrics["expectedWidth"] = expected_width
    metrics["expectedHeight"] = expected_height
    structural = bool(metrics.get("structurallyValid"))
    dimensions_match = (
        structural
        and (expected_width is None or metrics.get("width") == expected_width)
        and (expected_height is None or metrics.get("height") == expected_height)
    )
    luminance_range = float(metrics.get("luminanceRange", 0.0))
    luminance_variance = float(metrics.get("luminanceVariance", 0.0))
    nontrivial = (
        structural
        and luminance_range >= PNG_MIN_LUMINANCE_RANGE
        and luminance_variance >= PNG_MIN_LUMINANCE_VARIANCE
    )
    near_black = float(metrics.get("nearBlackFraction", 0.0))
    near_white = float(metrics.get("nearWhiteFraction", 0.0))
    not_clipped = (
        structural
        and near_black <= PNG_MAX_NEAR_BLACK_FRACTION
        and near_white <= PNG_MAX_NEAR_WHITE_FRACTION
    )
    metrics.update(
        {
            "dimensionsMatch": dimensions_match,
            "nontrivial": nontrivial,
            "notClipped": not_clipped,
            "minimumLuminanceRange": PNG_MIN_LUMINANCE_RANGE,
            "minimumLuminanceVariance": PNG_MIN_LUMINANCE_VARIANCE,
            "maximumNearBlackFraction": PNG_MAX_NEAR_BLACK_FRACTION,
            "maximumNearWhiteFraction": PNG_MAX_NEAR_WHITE_FRACTION,
            "valid": structural and dimensions_match and nontrivial and not_clipped,
        }
    )
    if structural and not dimensions_match:
        metrics["error"] = (
            "PNG dimensions do not match the requested render size "
            f"({metrics.get('width')}x{metrics.get('height')} != "
            f"{expected_width}x{expected_height})"
        )
    elif structural and not nontrivial:
        metrics["error"] = (
            "PNG luminance is too uniform for runtime evidence "
            f"(range {luminance_range:.3f}, variance {luminance_variance:.3f})"
        )
    elif structural and not not_clipped:
        metrics["error"] = (
            "PNG has excessive clipping for runtime evidence "
            f"(black {near_black:.3%}, white {near_white:.3%})"
        )
    return metrics


def collect_evidence(process_log: Path, homepath: Path) -> tuple[str, list[Path]]:
    pieces: list[str] = []
    if process_log.is_file():
        pieces.append(process_log.read_text(encoding="utf-8", errors="replace"))
    logs = find_logs(homepath)
    for path in logs:
        pieces.append(path.read_text(encoding="utf-8", errors="replace"))
    return "\n".join(pieces), logs


def detect_failures(text: str) -> list[dict[str, str]]:
    failures: list[dict[str, str]] = []
    for line in text.splitlines():
        for kind, pattern in FAILURE_PATTERNS:
            if pattern.search(line):
                failures.append({"kind": kind, "line": line.strip()[:1000]})
                break
    return failures


def parse_mode_evidence(text: str) -> dict[str, int] | None:
    matches = list(RTX_MODE_RE.finditer(text)) or list(RTX_CAPABILITY_RE.finditer(text))
    if not matches:
        return None
    match = matches[-1]
    return {name: int(match.group(name)) for name in ("requested", "active", "require")}


def run_process(command: list[str], cwd: Path, log_path: Path, timeout: float) -> dict[str, object]:
    started = time.monotonic()
    status = "failed"
    return_code: int | None = None
    cleanup = "not-started"
    launch_error: str | None = None
    try:
        with log_path.open("w", encoding="utf-8", errors="replace", newline="\n") as stream:
            process = subprocess.Popen(
                command,
                cwd=cwd,
                stdin=subprocess.DEVNULL,
                stdout=stream,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
            )
            try:
                return_code = process.wait(timeout=timeout)
                status = "exited"
                cleanup = "natural-exit"
            except subprocess.TimeoutExpired:
                status = "timeout"
                process.terminate()
                try:
                    return_code = process.wait(timeout=5.0)
                    cleanup = "terminated"
                except subprocess.TimeoutExpired:
                    process.kill()
                    return_code = process.wait(timeout=5.0)
                    cleanup = "killed"
    except OSError as error:
        launch_error = f"{type(error).__name__}: {error}"
        log_path.write_text(f"LAUNCH ERROR: {launch_error}\n", encoding="utf-8")

    result: dict[str, object] = {
        "status": status,
        "returnCode": return_code,
        "cleanup": cleanup,
        "elapsedSeconds": round(time.monotonic() - started, 3),
    }
    if launch_error:
        result["launchError"] = launch_error
    return result


def evaluate_profile(
    profile_name: str,
    process_result: dict[str, object],
    text: str,
    screenshots: list[Path],
    expected_width: int | None = None,
    expected_height: int | None = None,
    screenshot_evidence: list[dict[str, object]] | None = None,
) -> tuple[bool, list[str], dict[str, int] | None]:
    failures: list[str] = []
    errors = detect_failures(text)
    if process_result.get("status") != "exited":
        failures.append(f"process status was {process_result.get('status')}")
    if process_result.get("returnCode") != 0:
        failures.append(f"process return code was {process_result.get('returnCode')}")
    if errors:
        failures.append(f"runtime diagnostics contained {len(errors)} fatal/validation error(s)")
    if not screenshots:
        failures.append("expected windowed screenshot was not produced")
    else:
        evidence = screenshot_evidence or [
            inspect_png(path, expected_width, expected_height) for path in screenshots
        ]
        invalid = [
            f"{Path(str(item.get('path', 'unknown'))).name}: "
            f"{item.get('error', 'validation failed')}"
            for item in evidence
            if not item.get("valid")
        ]
        if invalid:
            failures.append("invalid screenshot evidence: " + "; ".join(invalid))

    expected = PROFILE_SPECS[profile_name]
    mode = parse_mode_evidence(text)
    if mode is None:
        failures.append("RTX capability/mode evidence was not found in the logs")
    else:
        for field in ("requested", "require"):
            if mode[field] != expected[field]:
                failures.append(
                    f"RTX {field} mode was {mode[field]}, expected {expected[field]}"
                )
        if mode["active"] != expected["requested"]:
            failures.append(
                f"RTX active mode was {mode['active']}, expected {expected['requested']}"
            )
    return not failures, failures, mode


def run_smoke(args: argparse.Namespace, retail_root: Path) -> tuple[dict[str, object], Path]:
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ") + f"-{os.getpid()}"
    run_root = args.output_dir.expanduser().resolve() / run_id
    run_root.mkdir(parents=True, exist_ok=False)
    executable = args.exe.expanduser().resolve()
    executable_evidence = file_evidence(executable)
    manifest_failures: list[str] = []
    if not executable_evidence.get("exists") or not executable_evidence.get("sha256"):
        manifest_failures.append(
            "FnQL executable identity could not be recorded: "
            f"{executable_evidence.get('error', 'hash unavailable')}"
        )
    manifest: dict[str, object] = {
        "schemaVersion": 1,
        "createdUtc": datetime.now(timezone.utc).isoformat(),
        "executable": str(executable),
        "executableEvidence": executable_evidence,
        "platform": platform_evidence(),
        "retailRoot": str(retail_root),
        "map": args.map_name,
        "gametype": args.gametype,
        "window": {"width": args.width, "height": args.height, "fullscreen": False},
        "profiles": [],
        "failures": manifest_failures,
    }

    for profile_name in selected_profiles(args.profile):
        profile_root = run_root / profile_name
        homepath = profile_root / "home"
        homepath.mkdir(parents=True)
        process_log = profile_root / "process.log"
        command, screenshot_name = build_launch_command(
            executable,
            retail_root,
            homepath,
            profile_name,
            args.map_name,
            args.gametype,
            args.width,
            args.height,
            args.map_wait,
            args.capture_wait,
        )
        result = run_process(command, executable.parent, process_log, args.timeout)
        text, qconsole_logs = collect_evidence(process_log, homepath)
        screenshots = find_screenshots(homepath, screenshot_name)
        screenshot_evidence = [
            inspect_png(path, args.width, args.height) for path in screenshots
        ]
        passed, failures, mode = evaluate_profile(
            profile_name,
            result,
            text,
            screenshots,
            args.width,
            args.height,
            screenshot_evidence,
        )
        result.update(
            {
                "name": profile_name,
                "description": PROFILE_SPECS[profile_name]["description"],
                "passed": passed,
                "failures": failures,
                "modeEvidence": mode,
                "command": command,
                "commandLine": command_to_string(command),
                "homepath": str(homepath),
                "processLog": str(process_log),
                "qconsoleLogs": [str(path) for path in qconsole_logs],
                "screenshots": [str(path) for path in screenshots],
                "screenshotEvidence": screenshot_evidence,
                "diagnostics": detect_failures(text),
            }
        )
        profiles = manifest["profiles"]
        assert isinstance(profiles, list)
        profiles.append(result)

    profiles = manifest["profiles"]
    assert isinstance(profiles, list)
    manifest["passed"] = not manifest_failures and all(
        bool(profile.get("passed")) for profile in profiles
    )
    manifest_path = run_root / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest, manifest_path


def planned_manifest(args: argparse.Namespace, retail_root: Path) -> dict[str, object]:
    profiles: list[dict[str, object]] = []
    for profile_name in selected_profiles(args.profile):
        placeholder_home = (
            args.output_dir.expanduser().resolve()
            / "planned-run-id"
            / profile_name
            / "home"
        )
        command, screenshot_name = build_launch_command(
            args.exe.expanduser().resolve(),
            retail_root,
            placeholder_home,
            profile_name,
            args.map_name,
            args.gametype,
            args.width,
            args.height,
            args.map_wait,
            args.capture_wait,
        )
        profiles.append(
            {
                "name": profile_name,
                "cvars": profile_cvars(profile_name, args.width, args.height),
                "command": command,
                "commandLine": command_to_string(command),
                "screenshotName": screenshot_name,
            }
        )
    return {
        "schemaVersion": 1,
        "planned": True,
        "executable": str(args.exe.expanduser().resolve()),
        "executableEvidence": file_evidence(args.exe),
        "platform": platform_evidence(),
        "retailRoot": str(retail_root),
        "map": args.map_name,
        "gametype": args.gametype,
        "profiles": profiles,
    }


def main(argv: Sequence[str] | None = None) -> int:
    parser = create_parser()
    args = parser.parse_args(argv)
    try:
        validate_options(args)
        retail_root = resolve_retail_root(args.retail_root)
        validate_retail_install(retail_root)
        if args.plan:
            print(json.dumps(planned_manifest(args, retail_root), indent=2))
            return 0
        manifest, manifest_path = run_smoke(args, retail_root)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.error(str(error))

    print(f"RTX smoke manifest: {manifest_path}")
    return 0 if bool(manifest.get("passed")) else 1


if __name__ == "__main__":
    sys.exit(main())
