"""Pin, stage and validate the FnQL-game modules that FnQL releases bundle.

FnQL stays engine-only: the game modules come from the companion repository
FnQL-game, built in CI from the commit pinned in version/fnql_game.json.  This
helper resolves that pin for the workflow, stages one platform's installed
modules together with FnQL-game's licensing documents, and exposes the checks
release.py repeats on every packaged archive.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from windows_pe import PE_I386, parse_pe


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PIN = ROOT / "version" / "fnql_game.json"

REPOSITORY_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9-]*/[A-Za-z0-9._-]+")
COMMIT_RE = re.compile(r"[0-9a-f]{40}")

# Retail module names carry the engine's ARCH_STRING (x86 on Windows, i386 on
# Linux); FnQL-game prefixes "fnql_" so its modules never shadow retail ones.
MODULE_SLOTS = ("qagame", "cgame", "ui")
PLATFORM_MODULE_SUFFIX = {
    "windows": "x86.dll",
    "linux": "i386.so",
}
GAME_DIR = "baseq3"
DOCS_DIR = "docs/fnql-game"
BUILD_INFO_NAME = "BUILD-INFO.txt"
LICENSE_DOCS = (
    ("docs/LICENSING.md", "LICENSING.md"),
    ("docs/baseline/QIIIA-Game-Source-License.txt", "QIIIA-Game-Source-License.txt"),
)
# FnQL-game exports only the retail structured entry point and its identity
# record (docs/fnql/GAME_MODULES.md).  Both names must be in the binary.
REQUIRED_EXPORTS = ("dllEntry", "FnQL_GetModuleInfo")
MAX_MODULE_BYTES = 64 * 1024 * 1024

ELF_CLASS_32 = 1
ELF_DATA_LSB = 1
ELF_TYPE_DYN = 3
ELF_MACHINE_386 = 3


def load_pin(path: Path = DEFAULT_PIN) -> dict[str, str]:
    data = json.loads(path.read_text(encoding="utf-8"))
    repository = data.get("repository")
    commit = data.get("commit")
    if not isinstance(repository, str) or not REPOSITORY_RE.fullmatch(repository):
        raise ValueError(f"{path} must name the FnQL-game repository as owner/name")
    if not isinstance(commit, str) or not COMMIT_RE.fullmatch(commit):
        raise ValueError(f"{path} must pin a full lowercase 40-character commit")
    return {"repository": repository, "commit": commit}


def module_names(platform: str) -> tuple[str, ...]:
    try:
        suffix = PLATFORM_MODULE_SUFFIX[platform]
    except KeyError as exc:
        raise ValueError(f"FnQL-game modules are not built for platform {platform!r}") from exc
    return tuple(f"{GAME_DIR}/fnql_{slot}{suffix}" for slot in MODULE_SLOTS)


def doc_names() -> tuple[str, ...]:
    return (
        *(f"{DOCS_DIR}/{destination}" for _source, destination in LICENSE_DOCS),
        f"{DOCS_DIR}/{BUILD_INFO_NAME}",
    )


def member_names(platform: str) -> tuple[str, ...]:
    return (*module_names(platform), *doc_names())


def validate_module(name: str, data: bytes, platform: str) -> None:
    if not data or len(data) > MAX_MODULE_BYTES:
        raise ValueError(f"FnQL-game module {name} has an invalid size")
    if platform == "windows":
        info = parse_pe(data, name=name)
        if info is None or info.machine != PE_I386 or not info.is_dll:
            raise ValueError(f"FnQL-game module {name} must be a 32-bit i386 PE DLL")
    elif platform == "linux":
        if (
            len(data) < 20
            or not data.startswith(b"\x7fELF")
            or data[4] != ELF_CLASS_32
            or data[5] != ELF_DATA_LSB
            or int.from_bytes(data[16:18], "little") != ELF_TYPE_DYN
            or int.from_bytes(data[18:20], "little") != ELF_MACHINE_386
        ):
            raise ValueError(f"FnQL-game module {name} must be a 32-bit i386 ELF shared object")
    else:
        raise ValueError(f"FnQL-game modules are not built for platform {platform!r}")
    for symbol in REQUIRED_EXPORTS:
        if symbol.encode("ascii") + b"\0" not in data:
            raise ValueError(f"FnQL-game module {name} does not carry the {symbol} export")


def build_info_commit(text: str) -> str | None:
    match = re.search(r"^Commit: ([0-9a-f]{40})$", text, re.MULTILINE)
    return match.group(1) if match else None


def build_info_text(pin: dict[str, str], platform: str) -> str:
    url = f"https://github.com/{pin['repository']}"
    modules = ", ".join(Path(name).name for name in module_names(platform))
    return (
        "FnQL-game modules bundled with this FnQL release\n"
        "\n"
        f"Repository: {url}\n"
        f"Commit: {pin['commit']}\n"
        f"Source: {url}/tree/{pin['commit']}\n"
        f"Platform: {platform}\n"
        f"Modules: {modules}\n"
        "\n"
        "The modules sit in baseq3/. FnQL loads the qagame only when asked:\n"
        "+set vm_gameProvider fnql. The cgame and UI modules are included for\n"
        "completeness; FnQL cannot select them yet and keeps using the retail\n"
        "client modules.\n"
        "\n"
        "Licensing: the modules contain code under two licenses. Read\n"
        "LICENSING.md and QIIIA-Game-Source-License.txt beside this file, and\n"
        "docs/fnql/FNQL_GAME_BINARY_NOTICE.txt.\n"
    )


def checkout_commit(source: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def stage(
    *,
    platform: str,
    install_root: Path,
    source: Path,
    output: Path,
    pin: dict[str, str],
    verify_checkout: bool = True,
) -> list[str]:
    """Copy one platform's modules and licensing documents into output."""
    if verify_checkout:
        actual = checkout_commit(source)
        if actual != pin["commit"]:
            raise ValueError(
                f"FnQL-game checkout is at {actual}, but version/fnql_game.json pins {pin['commit']}"
            )
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"FnQL-game staging directory must be empty: {output}")

    staged: list[str] = []
    for name in module_names(platform):
        module = install_root / name
        if not module.is_file() or module.is_symlink():
            raise FileNotFoundError(f"FnQL-game install is missing {name}: {module}")
        validate_module(name, module.read_bytes(), platform)
        destination = output / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(module, destination)
        staged.append(name)

    docs = output / DOCS_DIR
    docs.mkdir(parents=True, exist_ok=True)
    for source_name, destination_name in LICENSE_DOCS:
        document = source / source_name
        if not document.is_file():
            raise FileNotFoundError(f"FnQL-game checkout is missing {source_name}")
        shutil.copyfile(document, docs / destination_name)
        staged.append(f"{DOCS_DIR}/{destination_name}")
    # write_bytes keeps LF endings on every host. The text API's newline
    # argument needs Python 3.10; the Ubuntu 20.04 release container has 3.8.
    (docs / BUILD_INFO_NAME).write_bytes(build_info_text(pin, platform).encode("utf-8"))
    staged.append(f"{DOCS_DIR}/{BUILD_INFO_NAME}")
    return staged


def parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--pin", type=Path, default=DEFAULT_PIN)
    commands = parser.add_subparsers(dest="command", required=True)

    commands.add_parser("github-output", help="print the pin as GitHub step outputs")

    stage_parser = commands.add_parser("stage", help="stage installed modules for packaging")
    stage_parser.add_argument("--platform", choices=sorted(PLATFORM_MODULE_SUFFIX), required=True)
    stage_parser.add_argument("--install-root", type=Path, required=True, help="Meson install prefix")
    stage_parser.add_argument("--source", type=Path, required=True, help="FnQL-game checkout")
    stage_parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    pin = load_pin(args.pin)
    if args.command == "github-output":
        print(f"repository={pin['repository']}")
        print(f"commit={pin['commit']}")
        return 0
    staged = stage(
        platform=args.platform,
        install_root=args.install_root,
        source=args.source,
        output=args.output,
        pin=pin,
    )
    for name in staged:
        print(name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
