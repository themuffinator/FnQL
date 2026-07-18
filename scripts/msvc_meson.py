#!/usr/bin/env python3
"""Drive FnQL's canonical Meson build from the Visual Studio solution."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


SOURCE_ROOT = Path(__file__).resolve().parents[1]
PLATFORM_CPU_FAMILIES = {
    "Win32": "x86",
    "x64": "x86_64",
    "ARM64": "aarch64",
}
DEFAULT_RENDERERS = ("glx", "vk", "rtx")
SUPPORTED_RENDERERS = DEFAULT_RENDERERS


def run(command: list[str]) -> None:
    printable = subprocess.list2cmdline(command)
    print(f"[FnQL Visual Studio] {printable}", flush=True)
    subprocess.run(command, cwd=SOURCE_ROOT, check=True)


def find_meson() -> list[str] | None:
    executable = shutil.which("meson")
    if executable is not None:
        return [executable]
    try:
        module = importlib.util.find_spec("mesonbuild.mesonmain")
    except (ImportError, AttributeError, ValueError):
        module = None
    if module is not None:
        return [sys.executable, "-m", "mesonbuild.mesonmain"]
    return None


def parse_renderers(value: str) -> tuple[str, ...]:
    renderers: list[str] = []
    for item in value.split(","):
        renderer = item.strip()
        if not renderer:
            continue
        if renderer not in SUPPORTED_RENDERERS:
            supported = ", ".join(SUPPORTED_RENDERERS)
            raise argparse.ArgumentTypeError(
                f"unsupported renderer {renderer!r}; choose from {supported}"
            )
        if renderer not in renderers:
            renderers.append(renderer)
    if not renderers:
        raise argparse.ArgumentTypeError("at least one renderer must be selected")
    return tuple(renderers)


def configure(
    meson: list[str], build_dir: Path, buildtype: str, renderers: tuple[str, ...]
) -> None:
    command = [*meson, "setup"]
    if (build_dir / "meson-private" / "coredata.dat").is_file():
        command.append("--reconfigure")
    command.extend(
        [
            str(build_dir),
            str(SOURCE_ROOT),
            f"--buildtype={buildtype}",
            "-Dstrict-warnings=true",
            f"-Drenderers={','.join(renderers)}",
        ]
    )
    run(command)


def validate_machine(build_dir: Path, platform: str) -> None:
    expected = PLATFORM_CPU_FAMILIES[platform]
    machine_file = build_dir / "meson-info" / "intro-machines.json"
    try:
        machines = json.loads(machine_file.read_text(encoding="utf-8"))
        actual = machines["host"]["cpu_family"]
    except (OSError, KeyError, TypeError, ValueError) as error:
        raise RuntimeError(f"could not read Meson machine metadata: {error}") from error

    if actual != expected:
        raise RuntimeError(
            f"Visual Studio platform {platform} requires {expected}, but Meson "
            f"configured {actual}; select the matching Visual Studio platform "
            "or use an architecture-specific developer prompt"
        )


def validate_toolchain(platform: str) -> None:
    if shutil.which("cl") is None:
        raise RuntimeError(
            f"the MSVC compiler for Visual Studio platform {platform} is not "
            "available; install that architecture's MSVC build tools and use "
            "a matching developer environment"
        )


def build(
    meson: list[str],
    build_dir: Path,
    buildtype: str,
    platform: str,
    renderers: tuple[str, ...],
) -> None:
    validate_toolchain(platform)
    configure(meson, build_dir, buildtype, renderers)
    validate_machine(build_dir, platform)
    run([*meson, "compile", "-C", str(build_dir)])


def clean(meson: list[str], build_dir: Path) -> None:
    if (build_dir / "meson-private" / "coredata.dat").is_file():
        run([*meson, "compile", "-C", str(build_dir), "--clean"])


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build FnQL through Meson from the maintained Visual Studio solution"
    )
    parser.add_argument("action", choices=("build", "clean", "rebuild"))
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--buildtype", choices=("debug", "release"), required=True)
    parser.add_argument("--platform", choices=tuple(PLATFORM_CPU_FAMILIES), required=True)
    parser.add_argument(
        "--renderers",
        type=parse_renderers,
        default=os.environ.get("FNQL_MESON_RENDERERS", ",".join(DEFAULT_RENDERERS)),
        help=(
            "comma-separated renderer modules; defaults to the supported three-module "
            "set, or FNQL_MESON_RENDERERS when set"
        ),
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    meson = find_meson()
    if meson is None:
        print(
            "error: Meson was not found as an executable or Python module; "
            "install it for the selected Python environment",
            file=sys.stderr,
        )
        return 2

    build_dir = args.build_dir.resolve()
    try:
        if args.action in ("clean", "rebuild"):
            clean(meson, build_dir)
        if args.action in ("build", "rebuild"):
            build(meson, build_dir, args.buildtype, args.platform, args.renderers)
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
