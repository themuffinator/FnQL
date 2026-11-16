"""Pin, stage and validate the FnQL-game modules that FnQL releases bundle.

FnQL stays engine-only: the game modules come from the companion repository
FnQL-game, built in CI from the commit pinned in version/fnql_game.json.  This
helper resolves that pin for the workflow, stages one platform and
architecture's installed modules together with FnQL-game's licensing
documents, and exposes the checks
release.py repeats on every packaged archive.
"""

from __future__ import annotations

import argparse
import io
import json
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from windows_pe import PE_I386, parse_pe


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PIN = ROOT / "version" / "fnql_game.json"

REPOSITORY_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9-]*/[A-Za-z0-9._-]+")
COMMIT_RE = re.compile(r"[0-9a-f]{40}")

# Module names carry the engine's ARCH_STRING (x86 on Windows and i386 on Linux
# for the retail-compatible 32-bit target, x86_64 on both for the 64-bit
# target); FnQL-game prefixes "fnql_" so its modules never shadow retail ones.
# Retail Quake Live ships modules only for 32-bit x86, so on x86_64 the
# FnQL-game set is the only game FnQL can load.
MODULE_SLOTS = ("qagame", "cgame", "ui")
PLATFORMS = ("linux", "windows")
ARCHITECTURES = ("x86", "x86_64")
DEFAULT_ARCH = "x86"
MODULE_SUFFIX = {
    ("windows", "x86"): "x86.dll",
    ("linux", "x86"): "i386.so",
    ("windows", "x86_64"): "x86_64.dll",
    ("linux", "x86_64"): "x86_64.so",
}
TARGETS = tuple(sorted(MODULE_SUFFIX))
# The x86 suffixes by platform, kept for callers that predate the arch dimension.
PLATFORM_MODULE_SUFFIX = {
    platform: suffix for (platform, arch), suffix in MODULE_SUFFIX.items() if arch == "x86"
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
# FnQL-game's meson build installs this package beside the loose modules
# (FnQL-game scripts/make_package.py): the cgame and ui at the archive root,
# every entry stamped 1980-01-01 so the same modules give the same checksum.
CLIENT_PACKAGE_NAME = "fnql_game_client.pk3"
CLIENT_PACKAGE_SLOTS = ("cgame", "ui")
PACKAGE_TIMESTAMP = (1980, 1, 1, 0, 0, 0)

ELF_CLASS_32 = 1
ELF_CLASS_64 = 2
ELF_DATA_LSB = 1
ELF_TYPE_DYN = 3
ELF_MACHINE_386 = 3
ELF_MACHINE_X86_64 = 62
PE_AMD64 = 0x8664

# Expected binary identity per architecture: PE machine, ELF class, ELF machine
# and the wording used when a module does not match.
ARCH_BINARY_IDENTITY = {
    "x86": {
        "pe_machine": PE_I386,
        "elf_class": ELF_CLASS_32,
        "elf_machine": ELF_MACHINE_386,
        "pe_label": "32-bit i386 PE DLL",
        "elf_label": "32-bit i386 ELF shared object",
    },
    "x86_64": {
        "pe_machine": PE_AMD64,
        "elf_class": ELF_CLASS_64,
        "elf_machine": ELF_MACHINE_X86_64,
        "pe_label": "64-bit x86_64 (AMD64) PE DLL",
        "elf_label": "64-bit x86_64 ELF shared object",
    },
}


def load_pin(path: Path = DEFAULT_PIN) -> dict[str, str]:
    data = json.loads(path.read_text(encoding="utf-8"))
    repository = data.get("repository")
    commit = data.get("commit")
    if not isinstance(repository, str) or not REPOSITORY_RE.fullmatch(repository):
        raise ValueError(f"{path} must name the FnQL-game repository as owner/name")
    if not isinstance(commit, str) or not COMMIT_RE.fullmatch(commit):
        raise ValueError(f"{path} must pin a full lowercase 40-character commit")
    return {"repository": repository, "commit": commit}


def check_target(platform: str, arch: str = DEFAULT_ARCH) -> tuple[str, str]:
    """Return (platform, arch) when FnQL-game is built for it, else raise ValueError."""
    if platform not in PLATFORMS:
        raise ValueError(f"FnQL-game modules are not built for platform {platform!r}")
    if arch not in ARCHITECTURES:
        raise ValueError(
            f"FnQL-game modules are not built for architecture {arch!r}; "
            f"expected one of {', '.join(ARCHITECTURES)}"
        )
    if (platform, arch) not in MODULE_SUFFIX:
        raise ValueError(f"FnQL-game modules are not built for {platform} {arch}")
    return platform, arch


def artifact_dir_name(platform: str, arch: str = DEFAULT_ARCH) -> str:
    """Name of the staged artifact directory release.py --game-root expects."""
    check_target(platform, arch)
    return f"{platform}-{arch}"


def module_names(platform: str, arch: str = DEFAULT_ARCH) -> tuple[str, ...]:
    suffix = MODULE_SUFFIX[check_target(platform, arch)]
    return tuple(f"{GAME_DIR}/fnql_{slot}{suffix}" for slot in MODULE_SLOTS)


def client_package_name() -> str:
    """The FnQL-game client package's path in a release (FnQL-game milestone 12).

    A pure FnQL server that has it loaded lists it in sv_fnqlClientPaks, and an
    FnQL client with the same package then runs its cgame and ui
    (docs/fnql/GAME_MODULES.md); without it a pure server always gets the
    retail client modules.  It is never referenced, so retail clients never
    need it.
    """
    return f"{GAME_DIR}/{CLIENT_PACKAGE_NAME}"


def client_package_members(platform: str, arch: str = DEFAULT_ARCH) -> tuple[str, ...]:
    """The archive-root entries the client package holds: the cgame and ui."""
    suffix = MODULE_SUFFIX[check_target(platform, arch)]
    return tuple(f"fnql_{slot}{suffix}" for slot in CLIENT_PACKAGE_SLOTS)


def validate_client_package(
    name: str,
    data: bytes,
    platform: str,
    arch: str = DEFAULT_ARCH,
    modules: dict[str, bytes] | None = None,
) -> None:
    """Check a client package: exactly this target's cgame and ui at the root,
    stamped as FnQL-game's make_package.py stamps them, and (when the loose
    modules are given by release path) byte-identical to them."""
    check_target(platform, arch)
    if not data or len(data) > MAX_MODULE_BYTES * len(CLIENT_PACKAGE_SLOTS):
        raise ValueError(f"FnQL-game client package {name} has an invalid size")
    try:
        archive = zipfile.ZipFile(io.BytesIO(data))
    except zipfile.BadZipFile as exc:
        raise ValueError(f"FnQL-game client package {name} is not a zip archive: {exc}") from exc
    with archive:
        entries = archive.infolist()
        expected = client_package_members(platform, arch)
        found = [entry.filename for entry in entries]
        if sorted(found) != sorted(expected):
            raise ValueError(
                f"FnQL-game client package {name} must hold exactly {', '.join(expected)}; "
                f"found {', '.join(found) or 'nothing'}"
            )
        for entry in entries:
            if entry.date_time != PACKAGE_TIMESTAMP:
                raise ValueError(
                    f"FnQL-game client package {name} entry {entry.filename} is not stamped "
                    f"{PACKAGE_TIMESTAMP}; it was not built by make_package.py"
                )
            if entry.file_size > MAX_MODULE_BYTES:
                raise ValueError(f"FnQL-game client package {name} entry {entry.filename} is too large")
            member = archive.read(entry)
            module_path = f"{GAME_DIR}/{entry.filename}"
            validate_module(f"{name}:{entry.filename}", member, platform, arch)
            if modules is not None and modules.get(module_path) != member:
                raise ValueError(
                    f"FnQL-game client package {name} entry {entry.filename} differs from "
                    f"{module_path}"
                )


def doc_names() -> tuple[str, ...]:
    return (
        *(f"{DOCS_DIR}/{destination}" for _source, destination in LICENSE_DOCS),
        f"{DOCS_DIR}/{BUILD_INFO_NAME}",
    )


def member_names(platform: str, arch: str = DEFAULT_ARCH) -> tuple[str, ...]:
    return (*module_names(platform, arch), client_package_name(), *doc_names())


def validate_module(name: str, data: bytes, platform: str, arch: str = DEFAULT_ARCH) -> None:
    check_target(platform, arch)
    identity = ARCH_BINARY_IDENTITY[arch]
    if not data or len(data) > MAX_MODULE_BYTES:
        raise ValueError(f"FnQL-game module {name} has an invalid size")
    if platform == "windows":
        try:
            info = parse_pe(data, name=name)
        except ValueError as exc:
            raise ValueError(
                f"FnQL-game module {name} must be a {identity['pe_label']}: {exc}"
            ) from exc
        if info is None or info.machine != identity["pe_machine"] or not info.is_dll:
            raise ValueError(f"FnQL-game module {name} must be a {identity['pe_label']}")
    else:
        if (
            len(data) < 20
            or not data.startswith(b"\x7fELF")
            or data[4] != identity["elf_class"]
            or data[5] != ELF_DATA_LSB
            or int.from_bytes(data[16:18], "little") != ELF_TYPE_DYN
            or int.from_bytes(data[18:20], "little") != identity["elf_machine"]
        ):
            raise ValueError(f"FnQL-game module {name} must be a {identity['elf_label']}")
    for symbol in REQUIRED_EXPORTS:
        if symbol.encode("ascii") + b"\0" not in data:
            raise ValueError(f"FnQL-game module {name} does not carry the {symbol} export")


def build_info_commit(text: str) -> str | None:
    match = re.search(r"^Commit: ([0-9a-f]{40})$", text, re.MULTILINE)
    return match.group(1) if match else None


def build_info_arch(text: str) -> str | None:
    match = re.search(r"^Architecture: (\S+)$", text, re.MULTILINE)
    return match.group(1) if match else None


def build_info_text(pin: dict[str, str], platform: str, arch: str = DEFAULT_ARCH) -> str:
    url = f"https://github.com/{pin['repository']}"
    modules = ", ".join(Path(name).name for name in module_names(platform, arch))
    package = (
        f"{CLIENT_PACKAGE_NAME} holds the same cgame and UI. A pure FnQL server\n"
        "that has it loaded lists it to its clients, and an FnQL client with the\n"
        "same package runs them there; retail clients never need it. On any\n"
        "other pure server, retail ones included, FnQL loads the retail client\n"
        "modules.\n"
    )
    if arch == "x86":
        selection = (
            "The modules sit in baseq3/. FnQL loads them by default\n"
            "(vm_gameProvider, vm_cgameProvider and vm_uiProvider auto), and falls\n"
            "back to the retail modules wherever these are missing or refused.\n"
            "Set a provider to retail to use the retail module instead.\n"
            f"{package}"
        )
    else:
        selection = (
            "The modules sit in baseq3/. FnQL loads them by default. Retail Quake\n"
            "Live ships its game modules only for 32-bit x86, so on "
            f"{arch} these\n"
            "are the only native game, cgame and UI modules available.\n"
            f"{package}"
        )
    return (
        "FnQL-game modules bundled with this FnQL release\n"
        "\n"
        f"Repository: {url}\n"
        f"Commit: {pin['commit']}\n"
        f"Source: {url}/tree/{pin['commit']}\n"
        f"Platform: {platform}\n"
        f"Architecture: {arch}\n"
        f"Modules: {modules}\n"
        "\n"
        f"{selection}"
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
    arch: str = DEFAULT_ARCH,
    verify_checkout: bool = True,
) -> list[str]:
    """Copy one platform+arch's modules and licensing documents into output."""
    check_target(platform, arch)
    if verify_checkout:
        actual = checkout_commit(source)
        if actual != pin["commit"]:
            raise ValueError(
                f"FnQL-game checkout is at {actual}, but version/fnql_game.json pins {pin['commit']}"
            )
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"FnQL-game staging directory must be empty: {output}")

    staged: list[str] = []
    for name in module_names(platform, arch):
        module = install_root / name
        if not module.is_file() or module.is_symlink():
            raise FileNotFoundError(f"FnQL-game install is missing {name}: {module}")
        validate_module(name, module.read_bytes(), platform, arch)
        destination = output / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(module, destination)
        staged.append(name)

    package_name = client_package_name()
    package = install_root / package_name
    if not package.is_file() or package.is_symlink():
        raise FileNotFoundError(f"FnQL-game install is missing {package_name}: {package}")
    validate_client_package(
        package_name,
        package.read_bytes(),
        platform,
        arch,
        modules={name: (output / name).read_bytes() for name in module_names(platform, arch)},
    )
    shutil.copyfile(package, output / package_name)
    staged.append(package_name)

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
    (docs / BUILD_INFO_NAME).write_bytes(
        build_info_text(pin, platform, arch).encode("utf-8")
    )
    staged.append(f"{DOCS_DIR}/{BUILD_INFO_NAME}")
    return staged


def parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--pin", type=Path, default=DEFAULT_PIN)
    commands = parser.add_subparsers(dest="command", required=True)

    commands.add_parser("github-output", help="print the pin as GitHub step outputs")

    stage_parser = commands.add_parser("stage", help="stage installed modules for packaging")
    stage_parser.add_argument("--platform", choices=PLATFORMS, required=True)
    stage_parser.add_argument(
        "--arch",
        choices=ARCHITECTURES,
        default=DEFAULT_ARCH,
        help=f"module architecture (default: {DEFAULT_ARCH})",
    )
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
        arch=args.arch,
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
