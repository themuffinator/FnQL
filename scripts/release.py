from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import os
import plistlib
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path
from typing import NamedTuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

from fnql_meta import ROOT, channel_metadata, package_archive_name
from build_webpak import build_webpak
from glx_runtime_sweep import (
    GLX_VISUAL_DOSSIER_VERSION,
    release_corpus_manifest,
    validate_release_proof_root,
)
from glx_promotion import (
    check_rollback_package_metadata,
    promotion_report,
)
from root_archive import (
    ROOT_ARCHIVE_NAME,
    archive_member_name,
    path_is_relative_to,
    validate_archive_member_names,
    validate_root_archive,
    validate_root_archive_names,
    write_root_archive,
)
from windows_pe import PE_I386, PeInfo, validate_windows_runtime_dependencies


DEFAULT_DOCS = [
    (ROOT / "LICENSE", Path("LICENSE")),
    (ROOT / "docs" / "fnql" / "TECHNICAL.md", Path("docs") / "fnql" / "TECHNICAL.md"),
    (
        ROOT / "docs" / "fnql" / "RTX_RENDERER.md",
        Path("docs") / "fnql" / "RTX_RENDERER.md",
    ),
    (
        ROOT / "docs" / "fnql" / "STEAM_PROVIDER_BINARY_NOTICE.txt",
        Path("docs") / "fnql" / "STEAM_PROVIDER_BINARY_NOTICE.txt",
    ),
    (
        ROOT / "docs" / "GLX.md",
        Path("docs") / "GLX.md",
    ),
    (
        ROOT / "docs" / "RTX.md",
        Path("docs") / "RTX.md",
    ),
    (ROOT / ".install" / "README.html", Path("README.html")),
]

FNQL_WEBPAK_NAME = "fnql-web.pak"
FNQL_WEBPAK_SOURCE_ROOT = ROOT / "code" / "client" / "webui"

REQUIRED_RELEASE_ARCHIVE_ENTRIES = [
    ROOT_ARCHIVE_NAME,
    FNQL_WEBPAK_NAME,
    *(destination.as_posix() for _source, destination in DEFAULT_DOCS),
]

LINUX_RELEASE_EXECUTABLES = ("fnql", "fnql.ded")
LINUX_RELEASE_RENDERER_MODULES = (
    "fnql_glx_x86.so",
    "fnql_vk_x86.so",
    "fnql_rtx_x86.so",
)
MACOS_APP_ROOT = "FnQL.app/Contents"
MACOS_MINIMUM_VERSION = "11.0"
MACOS_REQUIRED_UNSIGNED_APP_ENTRIES = (
    f"{MACOS_APP_ROOT}/Info.plist",
    f"{MACOS_APP_ROOT}/MacOS/FnQL",
    f"{MACOS_APP_ROOT}/MacOS/{ROOT_ARCHIVE_NAME}",
    f"{MACOS_APP_ROOT}/MacOS/{FNQL_WEBPAK_NAME}",
)
MACOS_SIGNATURE_ENTRY = f"{MACOS_APP_ROOT}/_CodeSignature/CodeResources"
MACOS_REQUIRED_APP_ENTRIES = (*MACOS_REQUIRED_UNSIGNED_APP_ENTRIES, MACOS_SIGNATURE_ENTRY)
MACOS_PREBUILT_PAYLOAD_NAME = "macos-payload.zip"
RELEASE_ZIP_SUFFIX = ".zip"
RELEASE_TAR_GZ_SUFFIX = ".tar.gz"
RELEASE_ARCHIVE_SUFFIXES = (RELEASE_TAR_GZ_SUFFIX, RELEASE_ZIP_SUFFIX)
RELEASE_ZIP_DATETIME = (1980, 1, 1, 0, 0, 0)
RELEASE_FILE_MODE = 0o644
RELEASE_EXECUTABLE_MODE = 0o755

PUBLIC_RENDERERS = frozenset({"glx", "vk", "rtx"})
RENDERER_MODULE_RE = re.compile(
    r"(?:^|/)fnql_(?P<renderer>[a-z0-9]+)_[^/]+\.(?:dll|so|dylib)$",
    re.IGNORECASE,
)
FORBIDDEN_RELEASE_ARCH_RE = re.compile(
    r"(?<![a-z0-9])(?:x86[_-]?64|x64|amd64|arm64|aarch64|mingw64)(?![a-z0-9])",
    re.IGNORECASE,
)
MACOS_X86_64_CPU = 0x01000007
MACOS_ARM64_CPU = 0x0100000C
MACOS_ALLOWED_CPUS = frozenset({MACOS_X86_64_CPU, MACOS_ARM64_CPU})
MACOS_SHORT_VERSION_RE = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)")
MACOS_BUNDLE_VERSION_RE = re.compile(r"(?:0|[1-9][0-9]*)(?:\.(?:0|[1-9][0-9]*)){0,2}")

GLX_RELEASE_EVIDENCE_DOCS = {
    "visualDossier": {
        "path": "docs/fnql/GLX_VISUAL_DOSSIER.md",
        "version": GLX_VISUAL_DOSSIER_VERSION,
    },
}

SKIP_ARTIFACT_DIR_NAMES = {
    ".git",
    ".github",
    ".pytest_cache",
    ".tmp",
    ".vs",
    ".vscode",
    "__pycache__",
    "CMakeFiles",
    "Debug",
    "RelWithDebInfo",
    "Release",
    "Testing",
    "build",
    "meson-info",
    "meson-logs",
    "meson-private",
}

SKIP_ARTIFACT_FILE_NAMES = {
    ".DS_Store",
    ".ninja_deps",
    ".ninja_log",
    "Thumbs.db",
    "build.ninja",
    "cmake_install.cmake",
    "CMakeCache.txt",
    "compile_commands.json",
    "desktop.ini",
    "install.dat",
}

SKIP_ARTIFACT_SUFFIXES = {
    ".a",
    ".d",
    ".dSYM",
    ".exp",
    ".ilk",
    ".lastbuildstate",
    ".lib",
    ".log",
    ".obj",
    ".o",
    ".pdb",
    ".pyc",
    ".pyo",
    ".tmp",
    ".tlog",
}

SKIP_ARTIFACT_DIR_NAMES_LOWER = {name.lower() for name in SKIP_ARTIFACT_DIR_NAMES}
SKIP_ARTIFACT_FILE_NAMES_LOWER = {name.lower() for name in SKIP_ARTIFACT_FILE_NAMES}
SKIP_ARTIFACT_SUFFIXES_LOWER = {suffix.lower() for suffix in SKIP_ARTIFACT_SUFFIXES}


class ReleaseArchiveMember(NamedTuple):
    name: str
    data: bytes
    mode: int


def release_platform(name: str, names: list[str] | None = None) -> str:
    """Resolve the artifact policy without trusting executable suffixes alone."""
    lowered = name.replace("_", "-").casefold()
    if re.search(r"(?:^|-)macos(?:-|$)", lowered):
        return "macos"
    if re.search(r"(?:^|-)linux(?:-|$)", lowered):
        return "linux"
    if re.search(r"(?:^|-)windows(?:-|$)", lowered):
        return "windows"

    normalized = [entry.replace("\\", "/").casefold() for entry in (names or [])]
    if any(".app/contents/" in entry for entry in normalized):
        return "macos"
    if any(entry.endswith((".exe", ".dll")) for entry in normalized):
        return "windows"
    if any(entry.endswith(".so") for entry in normalized):
        return "linux"
    return "retail"


def macos_artifact_arch(name: str) -> str | None:
    lowered = name.replace("_", "-").casefold()
    if "universal2" in lowered or "universal-2" in lowered:
        return "universal2"
    if re.search(r"(?:^|-)x86-64(?:-|\.|$)", lowered):
        return "x86_64"
    if re.search(r"(?:^|-)(?:arm64|aarch64)(?:-|\.|$)", lowered):
        return "arm64"
    return None


def expected_macos_member_arch(name: str, artifact_arch: str | None) -> str | None:
    if artifact_arch != "universal2":
        return artifact_arch
    lowered = name.replace("\\", "/").casefold()
    if lowered.endswith("_x86_64.dylib"):
        return "x86_64"
    if lowered.endswith(("_aarch64.dylib", "_arm64.dylib")):
        return "arm64"
    return artifact_arch


def non_negative_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be non-negative")
    return parsed


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Package FnQL manual or tagged release artifacts")
    parser.add_argument("--channel", choices=("manual", "release"), required=True)
    parser.add_argument("--artifact-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".install")
    parser.add_argument("--temp-dir", type=Path, default=ROOT / ".tmp" / "release")
    parser.add_argument("--build-date")
    parser.add_argument("--build-number", type=non_negative_int)
    parser.add_argument("--commit")
    parser.add_argument("--ref-name")
    parser.add_argument(
        "--glx-proof-root",
        type=Path,
        help=(
            "Directory containing non-dry-run GLx runtime proof manifests. "
            "Required for tagged release packaging."
        ),
    )
    parser.add_argument(
        "--glx-rollback-metadata",
        type=Path,
        help=(
            "Reviewed JSON metadata describing the promoted-release rollback "
            "package that keeps the legacy OpenGL renderer available."
        ),
    )
    return parser.parse_args()


def sha256sum(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def should_skip_artifact_path(relative_path: Path, *, is_dir: bool) -> bool:
    parts = relative_path.parts
    if any(
        part.lower() in SKIP_ARTIFACT_DIR_NAMES_LOWER or part.lower().endswith(".dsym")
        for part in parts
    ):
        return True

    name = relative_path.name
    if is_dir:
        return False

    if name.lower() in SKIP_ARTIFACT_FILE_NAMES_LOWER:
        return True

    suffixes = {suffix.lower() for suffix in relative_path.suffixes}
    if suffixes.intersection(SKIP_ARTIFACT_SUFFIXES_LOWER):
        return True

    return False


def copy_release_artifact_contents(source: Path, target: Path) -> list[str]:
    source = source.expanduser()
    if not source.is_dir():
        raise NotADirectoryError(f"Release artifact source is not a directory: {source}")
    resolved_source = source.resolve()
    resolved_target = target.expanduser().resolve()
    if path_is_relative_to(resolved_target, resolved_source):
        raise ValueError(f"Release staging target must not be inside artifact source: {target}")
    target.mkdir(parents=True, exist_ok=True)
    skipped: list[str] = []

    for item in sorted(source.rglob("*")):
        if item.is_symlink():
            raise ValueError(f"Release artifact contains unsupported symbolic link: {item}")
        if not path_is_relative_to(item.resolve(), resolved_source):
            raise ValueError(f"Release artifact entry escapes source root: {item}")
        relative = item.relative_to(source)
        if should_skip_artifact_path(relative, is_dir=item.is_dir()):
            skipped.append(relative.as_posix())
            if item.is_dir():
                continue
            continue
        destination = target / relative
        if item.is_dir():
            destination.mkdir(parents=True, exist_ok=True)
        else:
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, destination)

    return skipped


def release_archive_suffix(archive_name: str) -> str:
    lowered = archive_name.casefold()
    for suffix in RELEASE_ARCHIVE_SUFFIXES:
        if lowered.endswith(suffix):
            return suffix
    raise ValueError(
        f"Unsupported release archive extension for {archive_name}; "
        f"expected {RELEASE_ZIP_SUFFIX} or {RELEASE_TAR_GZ_SUFFIX}"
    )


def is_macho_data(data: bytes) -> bool:
    return data[:4] in {
        b"\xcf\xfa\xed\xfe",
        b"\xfe\xed\xfa\xcf",
        b"\xca\xfe\xba\xbe",
        b"\xbe\xba\xfe\xca",
        b"\xca\xfe\xba\xbf",
        b"\xbf\xba\xfe\xca",
    }


def canonical_release_mode(
    data: bytes,
    *,
    linux: bool = False,
    macos: bool = False,
    name: str = "",
) -> int:
    if linux and data.startswith(b"\x7fELF"):
        return RELEASE_EXECUTABLE_MODE
    normalized = name.replace("\\", "/")
    if macos and (
        is_macho_data(data)
        or normalized.casefold().endswith(".dylib")
        or normalized == f"{MACOS_APP_ROOT}/MacOS/FnQL"
    ):
        return RELEASE_EXECUTABLE_MODE
    return RELEASE_FILE_MODE


def iter_release_stage_files(stage_root: Path) -> list[tuple[Path, str, bytes]]:
    stage_root = stage_root.expanduser()
    if not stage_root.is_dir():
        raise NotADirectoryError(f"Release stage root is not a directory: {stage_root}")

    resolved_root = stage_root.resolve()
    staged: list[tuple[Path, str, bytes]] = []
    for path in sorted(
        stage_root.rglob("*"),
        key=lambda item: item.relative_to(stage_root).as_posix().casefold(),
    ):
        if path.is_symlink():
            raise ValueError(f"Release stage contains unsupported symbolic link: {path}")
        if not path.is_file():
            continue
        if not path_is_relative_to(path.resolve(), resolved_root):
            raise ValueError(f"Release stage entry escapes stage root: {path}")
        archive_name = path.relative_to(stage_root).as_posix()
        staged.append((path, archive_name, path.read_bytes()))
    return staged


def write_release_zip(stage_root: Path, archive_path: Path) -> None:
    staged = iter_release_stage_files(stage_root)
    macos = release_platform(archive_path.name) == "macos"
    with zipfile.ZipFile(
        archive_path,
        "w",
        compression=zipfile.ZIP_DEFLATED,
        compresslevel=9,
    ) as archive:
        for _path, archive_name, data in staged:
            info = zipfile.ZipInfo(archive_name)
            info.create_system = 3
            info.compress_type = zipfile.ZIP_DEFLATED
            info.date_time = RELEASE_ZIP_DATETIME
            mode = canonical_release_mode(
                data,
                macos=macos,
                name=archive_name,
            )
            info.external_attr = (stat.S_IFREG | mode) << 16
            archive.writestr(info, data)


def write_release_tar_gz(stage_root: Path, archive_path: Path) -> None:
    staged = iter_release_stage_files(stage_root)
    with archive_path.open("wb") as raw_archive:
        with gzip.GzipFile(
            filename="",
            mode="wb",
            fileobj=raw_archive,
            compresslevel=9,
            mtime=0,
        ) as compressed:
            with tarfile.open(
                fileobj=compressed,
                mode="w",
                format=tarfile.GNU_FORMAT,
            ) as archive:
                for _path, archive_name, data in staged:
                    info = tarfile.TarInfo(archive_name)
                    info.size = len(data)
                    info.mode = canonical_release_mode(data, linux=True)
                    info.mtime = 0
                    info.uid = 0
                    info.gid = 0
                    info.uname = ""
                    info.gname = ""
                    archive.addfile(info, io.BytesIO(data))


def write_release_archive(stage_root: Path, archive_path: Path) -> None:
    archive_path = archive_path.expanduser()
    archive_path.parent.mkdir(parents=True, exist_ok=True)
    suffix = release_archive_suffix(archive_path.name)
    temp_path = archive_path.with_name(f"{archive_path.name}.tmp")
    if temp_path.exists():
        temp_path.unlink()

    try:
        if suffix == RELEASE_TAR_GZ_SUFFIX:
            write_release_tar_gz(stage_root, temp_path)
        else:
            write_release_zip(stage_root, temp_path)
        os.replace(temp_path, archive_path)
    finally:
        if temp_path.exists():
            temp_path.unlink()


def copy_docs(stage_root: Path) -> None:
    for source, dest_relative in DEFAULT_DOCS:
        destination = stage_root / dest_relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def build_root_archive(stage_root: Path) -> Path:
    archive_path = stage_root / ROOT_ARCHIVE_NAME
    write_root_archive(archive_path)
    validate_root_archive(archive_path)
    return archive_path


def build_fnql_webpak(stage_root: Path) -> Path:
    webpak_path = stage_root / FNQL_WEBPAK_NAME
    build_webpak(FNQL_WEBPAK_SOURCE_ROOT, webpak_path)
    return webpak_path


def validate_renderer_module_names(names: list[str]) -> None:
    unsupported = []
    for name in names:
        match = RENDERER_MODULE_RE.search(name.replace("\\", "/"))
        if match and match.group("renderer").lower() not in PUBLIC_RENDERERS:
            unsupported.append(name)
    if unsupported:
        raise ValueError(
            "release package contains unsupported renderer modules; "
            "only glx, vk, and rtx are allowed: " + ", ".join(unsupported[:12])
        )


def validate_release_architecture_names(
    names: list[str],
    *,
    platform: str = "retail",
) -> None:
    if platform == "macos":
        return
    offenders = [
        name
        for name in names
        if FORBIDDEN_RELEASE_ARCH_RE.search(name.replace("\\", "/"))
    ]
    if offenders:
        raise ValueError(
            "release artifacts are 32-bit x86 only; found a 64-bit architecture marker: "
            + ", ".join(offenders[:12])
        )


def macho_cpu_name(cpu_type: int) -> str:
    return {
        MACOS_X86_64_CPU: "x86_64",
        MACOS_ARM64_CPU: "arm64",
    }.get(cpu_type, f"CPU type 0x{cpu_type:08x}")


def validate_macos_macho_header(
    name: str,
    header: bytes,
    *,
    expected_arch: str | None,
) -> None:
    magic = header[:4]
    thin_orders = {
        b"\xcf\xfa\xed\xfe": "little",
        b"\xfe\xed\xfa\xcf": "big",
    }
    if magic in thin_orders:
        if len(header) < 8:
            raise ValueError(f"macOS release has a truncated Mach-O binary: {name}")
        cpu_types = {int.from_bytes(header[4:8], thin_orders[magic])}
        is_universal = False
    else:
        fat_layouts = {
            b"\xca\xfe\xba\xbe": ("big", 20),
            b"\xbe\xba\xfe\xca": ("little", 20),
            b"\xca\xfe\xba\xbf": ("big", 32),
            b"\xbf\xba\xfe\xca": ("little", 32),
        }
        layout = fat_layouts.get(magic)
        if layout is None:
            raise ValueError(f"macOS release contains an unsupported Mach-O header: {name}")
        byte_order, entry_size = layout
        if len(header) < 8:
            raise ValueError(f"macOS release has a truncated universal binary: {name}")
        count = int.from_bytes(header[4:8], byte_order)
        if count < 1 or count > 32 or len(header) < 8 + count * entry_size:
            raise ValueError(f"macOS release has an invalid universal binary table: {name}")
        cpu_types = {
            int.from_bytes(header[8 + index * entry_size : 12 + index * entry_size], byte_order)
            for index in range(count)
        }
        is_universal = True

    unsupported = sorted(cpu_types - MACOS_ALLOWED_CPUS)
    if unsupported:
        description = ", ".join(macho_cpu_name(cpu) for cpu in unsupported)
        raise ValueError(f"macOS release contains unsupported architecture(s) in {name}: {description}")

    if expected_arch == "universal2" and not is_universal:
        raise ValueError(f"macOS Universal 2 artifact contains a thin binary: {name}")

    expected_cpus = {
        "x86_64": {MACOS_X86_64_CPU},
        "arm64": {MACOS_ARM64_CPU},
        "universal2": set(MACOS_ALLOWED_CPUS),
    }.get(expected_arch)
    if expected_cpus is not None and cpu_types != expected_cpus:
        actual = ", ".join(sorted(macho_cpu_name(cpu) for cpu in cpu_types))
        expected = ", ".join(sorted(macho_cpu_name(cpu) for cpu in expected_cpus))
        raise ValueError(
            f"macOS artifact architecture mismatch for {name}: expected {expected}, found {actual}"
        )


def validate_release_binary_stream(
    name: str,
    handle: object,
    *,
    platform: str = "retail",
    expected_macos_arch: str | None = None,
) -> None:
    header = handle.read(4096)
    if header.startswith(b"MZ") and len(header) >= 64:
        pe_offset = int.from_bytes(header[60:64], "little")
        handle.seek(pe_offset)
        pe_header = handle.read(6)
        if pe_header.startswith(b"PE\0\0"):
            machine = int.from_bytes(pe_header[4:6], "little")
            if platform == "macos":
                raise ValueError(f"macOS release contains a PE binary: {name}")
            if machine != 0x014C:
                raise ValueError(
                    f"release artifacts are 32-bit x86 only; {name} has PE machine 0x{machine:04x}"
                )
        return

    if header.startswith(b"\x7fELF") and len(header) >= 20:
        elf_class = header[4]
        byte_order = "little" if header[5] == 1 else "big"
        machine = int.from_bytes(header[18:20], byte_order)
        if platform == "macos":
            raise ValueError(f"macOS release contains an ELF binary: {name}")
        if elf_class != 1 or machine != 3:
            raise ValueError(
                f"release artifacts are 32-bit x86 only; {name} has ELF class {elf_class} "
                f"and machine {machine}"
            )
        return

    macho_magic = header[:4]
    if macho_magic in (
        b"\xcf\xfa\xed\xfe",
        b"\xfe\xed\xfa\xcf",
        b"\xca\xfe\xba\xbe",
        b"\xbe\xba\xfe\xca",
        b"\xca\xfe\xba\xbf",
        b"\xbf\xba\xfe\xca",
    ):
        if platform == "macos":
            validate_macos_macho_header(
                name,
                header,
                expected_arch=expected_macos_arch,
            )
            return
        raise ValueError(
            f"release artifacts are 32-bit x86 only; {name} is a 64-bit or universal Mach-O binary"
        )
    if macho_magic in (b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xce") and len(header) >= 8:
        if platform == "macos":
            raise ValueError(f"macOS release contains an unsupported 32-bit Mach-O binary: {name}")
        byte_order = "little" if macho_magic == b"\xce\xfa\xed\xfe" else "big"
        cpu_type = int.from_bytes(header[4:8], byte_order)
        if cpu_type != 7:
            raise ValueError(
                f"release artifacts are 32-bit x86 only; {name} has Mach-O CPU type {cpu_type}"
            )


def validate_windows_distribution_files(names: list[str], *, name: str) -> None:
    normalized = [entry.replace("\\", "/").casefold() for entry in names]
    if any(Path(entry).name.casefold() == "steam_api.dll" for entry in normalized):
        raise ValueError(f"{name} must not redistribute Valve's steam_api.dll")
    if any(entry.endswith(".exe") for entry in normalized) and "fnql_steam.dll" not in normalized:
        raise ValueError(f"{name} is a Windows release but is missing fnql_steam.dll")


def validate_steam_provider_identity(name: str, info: PeInfo | None) -> None:
    if name.replace("\\", "/").casefold() != "fnql_steam.dll":
        return
    if info is None or info.machine != PE_I386 or not info.is_dll:
        raise ValueError("fnql_steam.dll must be a 32-bit i386 PE DLL")


def read_release_zip(archive_path: Path) -> list[ReleaseArchiveMember]:
    members: list[ReleaseArchiveMember] = []
    with zipfile.ZipFile(archive_path) as archive:
        for info in archive.infolist():
            unix_mode = (info.external_attr >> 16) & 0o177777
            if stat.S_IFMT(unix_mode) == stat.S_IFLNK:
                raise ValueError(
                    f"{archive_path.name} contains unsupported symbolic link: {info.filename}"
                )
            path_to_validate = info.filename[:-1] if info.is_dir() else info.filename
            try:
                archive_member_name(path_to_validate)
            except ValueError as exc:
                raise ValueError(
                    f"{archive_path.name} contains unsafe release path: {info.filename}"
                ) from exc
            if info.is_dir():
                continue
            members.append(
                ReleaseArchiveMember(
                    name=info.filename,
                    data=archive.read(info),
                    mode=unix_mode & 0o7777,
                )
            )
    return members


def read_release_tar_gz(archive_path: Path) -> list[ReleaseArchiveMember]:
    members: list[ReleaseArchiveMember] = []
    with tarfile.open(archive_path, mode="r:gz") as archive:
        for info in archive.getmembers():
            try:
                archive_member_name(info.name)
            except ValueError as exc:
                raise ValueError(
                    f"{archive_path.name} contains unsafe release path: {info.name}"
                ) from exc
            if info.isdir():
                continue
            if not info.isfile():
                raise ValueError(
                    f"{archive_path.name} contains unsupported non-file entry: {info.name}"
                )
            source = archive.extractfile(info)
            if source is None:
                raise ValueError(f"Unable to read {info.name} from {archive_path.name}")
            members.append(
                ReleaseArchiveMember(
                    name=info.name,
                    data=source.read(),
                    mode=info.mode & 0o7777,
                )
            )
    return members


def read_release_archive(archive_path: Path) -> list[ReleaseArchiveMember]:
    suffix = release_archive_suffix(archive_path.name)
    if suffix == RELEASE_TAR_GZ_SUFFIX:
        return read_release_tar_gz(archive_path)
    return read_release_zip(archive_path)


def validate_linux_distribution_files(
    members: list[ReleaseArchiveMember],
    *,
    archive_name: str,
) -> None:
    by_name = {member.name: member for member in members}
    required_elf_names = set(LINUX_RELEASE_EXECUTABLES)
    missing_executables = [
        name
        for name in LINUX_RELEASE_EXECUTABLES
        if name not in by_name
    ]
    if missing_executables:
        raise ValueError(
            f"{archive_name} is missing required Linux executables: "
            + ", ".join(missing_executables)
        )

    missing_renderers = [
        name
        for name in LINUX_RELEASE_RENDERER_MODULES
        if name not in by_name
    ]
    if missing_renderers:
        raise ValueError(
            f"{archive_name} is missing required Linux renderer modules: "
            + ", ".join(missing_renderers)
        )
    required_elf_names.update(LINUX_RELEASE_RENDERER_MODULES)

    for member in members:
        expected_mode = canonical_release_mode(member.data, linux=True)
        if member.mode != expected_mode:
            raise ValueError(
                f"{archive_name} has non-canonical mode {member.mode:04o} for "
                f"{member.name}; expected {expected_mode:04o}"
            )
        if member.name in required_elf_names and not member.data.startswith(b"\x7fELF"):
            raise ValueError(
                f"{archive_name} contains a non-ELF Linux executable/module: {member.name}"
            )


def validate_macos_distribution_files(
    members: list[ReleaseArchiveMember],
    *,
    archive_name: str,
    require_signature: bool = True,
) -> None:
    by_name = {member.name: member for member in members}
    required_entries = (
        MACOS_REQUIRED_APP_ENTRIES
        if require_signature
        else MACOS_REQUIRED_UNSIGNED_APP_ENTRIES
    )
    missing = [name for name in required_entries if name not in by_name]
    if missing:
        raise ValueError(
            f"{archive_name} is missing required macOS app files: " + ", ".join(missing)
        )

    bundled_steam_api = sorted(
        member.name
        for member in members
        if member.name.replace("\\", "/").rsplit("/", 1)[-1].casefold()
        == "libsteam_api.dylib"
    )
    if bundled_steam_api:
        raise ValueError(
            f"{archive_name} must not redistribute Valve's libsteam_api.dylib: "
            + ", ".join(bundled_steam_api)
        )

    dedicated = [
        member.name
        for member in members
        if "/" not in member.name and member.name.casefold().startswith("fnql.ded")
    ]
    if not dedicated:
        raise ValueError(f"{archive_name} is missing the macOS dedicated-server executable")
    tool_name = "fnql-audiozonesc"
    if tool_name not in by_name:
        raise ValueError(f"{archive_name} is missing the macOS audio-zone tool")

    renderer_names = {renderer: [] for renderer in PUBLIC_RENDERERS}
    misplaced_renderers: list[str] = []
    renderer_parent = f"{MACOS_APP_ROOT}/MacOS"
    for member in members:
        match = RENDERER_MODULE_RE.search(member.name)
        if match and member.name.casefold().endswith(".dylib"):
            if member.name.rpartition("/")[0] != renderer_parent:
                misplaced_renderers.append(member.name)
                continue
            renderer = match.group("renderer").casefold()
            if renderer in renderer_names:
                renderer_names[renderer].append(member.name)
    if misplaced_renderers:
        raise ValueError(
            f"{archive_name} has macOS renderer modules outside {renderer_parent}: "
            + ", ".join(sorted(misplaced_renderers))
        )
    missing_renderers = [
        renderer for renderer, names in sorted(renderer_names.items()) if not names
    ]
    if missing_renderers:
        raise ValueError(
            f"{archive_name} is missing required macOS renderer modules: "
            + ", ".join(missing_renderers)
        )

    required_code = {
        f"{MACOS_APP_ROOT}/MacOS/FnQL",
        *dedicated,
        tool_name,
        *(name for names in renderer_names.values() for name in names),
    }
    for name in sorted(required_code):
        member = by_name[name]
        if not is_macho_data(member.data):
            raise ValueError(f"{archive_name} contains a non-Mach-O macOS executable/module: {name}")
        expected_mode = canonical_release_mode(member.data, macos=True, name=name)
        if member.mode != expected_mode:
            raise ValueError(
                f"{archive_name} has non-canonical mode {member.mode:04o} for "
                f"{name}; expected {expected_mode:04o}"
            )

    try:
        info = plistlib.loads(by_name[f"{MACOS_APP_ROOT}/Info.plist"].data)
    except Exception as exc:
        raise ValueError(f"{archive_name} contains an invalid macOS Info.plist") from exc
    expected_plist = {
        "CFBundleExecutable": "FnQL",
        "CFBundleIdentifier": "org.fnql.fnql",
        "CFBundlePackageType": "APPL",
        "LSMinimumSystemVersion": MACOS_MINIMUM_VERSION,
        "NSHighResolutionCapable": True,
    }
    for key, expected in expected_plist.items():
        if info.get(key) != expected:
            raise ValueError(
                f"{archive_name} has invalid Info.plist {key}: expected {expected!r}"
            )
    if not info.get("NSMicrophoneUsageDescription"):
        raise ValueError(f"{archive_name} Info.plist is missing NSMicrophoneUsageDescription")
    short_version = info.get("CFBundleShortVersionString")
    if not isinstance(short_version, str) or not MACOS_SHORT_VERSION_RE.fullmatch(short_version):
        raise ValueError(
            f"{archive_name} Info.plist has invalid CFBundleShortVersionString"
        )
    bundle_version = info.get("CFBundleVersion")
    if not isinstance(bundle_version, str) or not MACOS_BUNDLE_VERSION_RE.fullmatch(bundle_version):
        raise ValueError(f"{archive_name} Info.plist has invalid CFBundleVersion")


def validate_release_archive_contents(
    archive_path: Path,
    *,
    artifact_name: str | None = None,
    require_macos_signature: bool = True,
) -> None:
    members = read_release_archive(archive_path)
    archived_names = [member.name for member in members]
    policy_name = artifact_name or archive_path.name
    platform = release_platform(policy_name, archived_names)
    expected_macos_arch = macos_artifact_arch(policy_name) if platform == "macos" else None
    validate_archive_member_names(archived_names, archive_name=archive_path.name)
    validate_release_architecture_names(archived_names, platform=platform)
    validate_renderer_module_names(archived_names)
    for member in members:
        validate_release_binary_stream(
            member.name,
            io.BytesIO(member.data),
            platform=platform,
            expected_macos_arch=expected_macos_member_arch(
                member.name, expected_macos_arch
            ),
        )
        pe_info = validate_windows_runtime_dependencies(member.data, name=member.name)
        validate_steam_provider_identity(member.name, pe_info)
    archived_name_set = set(archived_names)
    missing_release_entries = [
        name
        for name in REQUIRED_RELEASE_ARCHIVE_ENTRIES
        if name not in archived_name_set
    ]
    if missing_release_entries:
        raise ValueError(
            f"{archive_path.name} is missing required release files: "
            + ", ".join(missing_release_entries)
        )
    validate_windows_distribution_files(archived_names, name=archive_path.name)
    if platform == "linux":
        validate_linux_distribution_files(members, archive_name=archive_path.name)
    elif platform == "macos":
        validate_macos_distribution_files(
            members,
            archive_name=archive_path.name,
            require_signature=require_macos_signature,
        )
    root_archive_bytes = next(
        member.data for member in members if member.name == ROOT_ARCHIVE_NAME
    )

    with zipfile.ZipFile(io.BytesIO(root_archive_bytes)) as root_archive:
        root_archive_names = [
            info.filename
            for info in root_archive.infolist()
            if not info.is_dir()
        ]
    validate_root_archive_names(root_archive_names)


def validate_stage_tree(stage_root: Path, *, artifact_name: str | None = None) -> None:
    offenders: list[str] = []
    staged_names: list[str] = []
    for item in sorted(stage_root.rglob("*")):
        relative = item.relative_to(stage_root)
        if item.is_file():
            staged_names.append(relative.as_posix())
        if should_skip_artifact_path(relative, is_dir=item.is_dir()):
            offenders.append(relative.as_posix())
    if offenders:
        raise ValueError(
            "release package contains filtered build byproducts: "
            + ", ".join(offenders[:12])
        )
    policy_name = artifact_name or stage_root.name
    platform = release_platform(policy_name, staged_names)
    expected_macos_arch = macos_artifact_arch(policy_name) if platform == "macos" else None
    validate_release_architecture_names(staged_names, platform=platform)
    validate_renderer_module_names(staged_names)
    for relative_name in staged_names:
        member_data = (stage_root / Path(relative_name)).read_bytes()
        validate_release_binary_stream(
            relative_name,
            io.BytesIO(member_data),
            platform=platform,
            expected_macos_arch=expected_macos_member_arch(
                relative_name, expected_macos_arch
            ),
        )
        pe_info = validate_windows_runtime_dependencies(member_data, name=relative_name)
        validate_steam_provider_identity(relative_name, pe_info)
    validate_windows_distribution_files(staged_names, name=stage_root.name)


def release_artifact_dirs(artifact_root: Path) -> list[Path]:
    if not artifact_root.exists():
        raise FileNotFoundError(f"Artifact root does not exist: {artifact_root}")
    if not artifact_root.is_dir():
        raise NotADirectoryError(f"Artifact root is not a directory: {artifact_root}")

    artifact_dirs = sorted(path for path in artifact_root.iterdir() if path.is_dir())
    if not artifact_dirs:
        raise ValueError(f"Artifact root does not contain any artifact directories: {artifact_root}")
    for path in artifact_dirs:
        platform = release_platform(path.name)
        if platform == "macos" and macos_artifact_arch(path.name) is None:
            raise ValueError(
                "macOS artifact names must declare x86_64, arm64/aarch64, or universal2: "
                + path.name
            )
        validate_release_architecture_names([path.name], platform=platform)
    return artifact_dirs


def publish_prebuilt_macos_payload(
    artifact_dir: Path,
    archive_path: Path,
) -> bool:
    """Copy a macOS-created payload ZIP without discarding stapled metadata."""
    if release_platform(artifact_dir.name) != "macos":
        return False
    payload = artifact_dir / MACOS_PREBUILT_PAYLOAD_NAME
    if not payload.exists():
        return False
    if payload.is_symlink() or not payload.is_file():
        raise ValueError(f"macOS prebuilt payload must be a regular file: {payload}")

    artifact_files: list[Path] = []
    for entry in artifact_dir.rglob("*"):
        if entry.is_symlink():
            raise ValueError(f"macOS artifact contains a symbolic link: {entry}")
        if entry.is_file():
            artifact_files.append(entry)
    if artifact_files != [payload]:
        unexpected = sorted(
            entry.relative_to(artifact_dir).as_posix()
            for entry in artifact_files
            if entry != payload
        )
        raise ValueError(
            f"{artifact_dir.name} must contain only {MACOS_PREBUILT_PAYLOAD_NAME}; "
            "unexpected files: " + ", ".join(unexpected)
        )

    validate_release_archive_contents(payload, artifact_name=artifact_dir.name)
    temp_path = archive_path.with_name(f"{archive_path.name}.tmp")
    if temp_path.exists():
        temp_path.unlink()
    try:
        shutil.copyfile(payload, temp_path)
        os.replace(temp_path, archive_path)
    finally:
        if temp_path.exists():
            temp_path.unlink()
    return True


def resolve_glx_runtime_proof(args: argparse.Namespace) -> dict[str, object]:
    if args.glx_proof_root is None:
        if args.channel == "release":
            raise ValueError(
                "--glx-proof-root is required for --channel release; "
                "tagged releases need reviewed non-dry-run GLx runtime proof."
            )
        return {
            "required": False,
            "status": "not-required",
            "reason": "manual release packaging records the corpus but does not promote GLx.",
        }

    proof = validate_release_proof_root(args.glx_proof_root)
    proof["required"] = args.channel == "release"
    if proof.get("status") != "passed":
        failures = proof.get("failures", [])
        detail = "; ".join(str(item) for item in failures[:8]) if isinstance(failures, list) else ""
        raise ValueError(
            "GLx runtime proof validation failed"
            + (f": {detail}" if detail else ".")
        )
    return proof


def resolve_glx_rollback_package(
    args: argparse.Namespace,
    glx_promotion: dict[str, object],
) -> dict[str, object]:
    source_policy = glx_promotion.get("sourcePolicy", {})
    promoted_source = (
        isinstance(source_policy, dict)
        and bool(source_policy.get("promoted"))
    )
    required = args.channel == "release" and promoted_source

    if args.glx_rollback_metadata is None:
        return {
            "required": required,
            "status": "missing" if required else "not-required",
            "reason": (
                "promoted GLx release packaging requires rollback metadata"
                if required
                else "current source tree has not promoted GLx as the renderer default"
            ),
        }

    rollback = check_rollback_package_metadata(args.glx_rollback_metadata)
    rollback["required"] = required
    if rollback.get("status") != "passed":
        blockers = rollback.get("blockers", [])
        detail = "; ".join(str(item) for item in blockers[:8]) if isinstance(blockers, list) else ""
        raise ValueError(
            "GLx rollback package metadata validation failed"
            + (f": {detail}" if detail else ".")
        )
    return rollback


def attach_glx_rollback_archives(
    glx_rollback_package: dict[str, object],
    archives: list[dict[str, object]],
) -> dict[str, object]:
    if glx_rollback_package.get("status") != "passed":
        return glx_rollback_package

    archives_by_artifact_dir = {
        str(archive.get("artifact_dir", "")): archive
        for archive in archives
    }
    archives_by_name = {
        str(archive.get("archive", "")): archive
        for archive in archives
    }
    matched_archives: list[dict[str, object]] = []
    blockers: list[str] = []

    for package in glx_rollback_package.get("packages", []):
        if not isinstance(package, dict):
            continue
        package_id = str(package.get("id", "rollback-package"))
        archive = None
        artifact_dir = str(package.get("artifactDir", ""))
        archive_name = str(package.get("archive", ""))
        if artifact_dir:
            archive = archives_by_artifact_dir.get(artifact_dir)
        if archive is None and archive_name:
            archive = archives_by_name.get(archive_name)
        if archive is None:
            blockers.append(
                f"{package_id} did not match a staged release archive."
            )
            continue
        matched_archives.append(
            {
                "package": package_id,
                "artifact_dir": archive.get("artifact_dir", ""),
                "archive": archive.get("archive", ""),
                "path": archive.get("path", ""),
                "sha256": archive.get("sha256", ""),
            }
        )

    if blockers:
        raise ValueError(
            "GLx rollback package archive validation failed: "
            + "; ".join(blockers)
        )

    glx_rollback_package = dict(glx_rollback_package)
    glx_rollback_package["matchedArchives"] = matched_archives
    return glx_rollback_package


def build_archives(args: argparse.Namespace) -> dict[str, object]:
    subprocess.run([sys.executable, str(ROOT / "scripts" / "generate_docs.py")], check=True)

    meta = channel_metadata(
        args.channel,
        build_number=args.build_number,
        build_date=args.build_date,
        commit=args.commit,
        ref_name=args.ref_name,
    )
    glx_runtime_proof = resolve_glx_runtime_proof(args)
    glx_promotion = promotion_report(args.glx_proof_root, args.glx_rollback_metadata)
    glx_rollback_package = resolve_glx_rollback_package(args, glx_promotion)
    if glx_promotion.get("policyViolation"):
        raise ValueError(
            "GLx promotion policy failed: renderer defaults were promoted "
            "before the promotion gate passed."
        )

    artifact_root = args.artifact_root.resolve()
    artifact_dirs = release_artifact_dirs(artifact_root)

    output_dir = args.output_dir.resolve()
    packages_dir = output_dir / "packages"
    temp_dir = args.temp_dir.resolve() / args.channel

    packages_dir.mkdir(parents=True, exist_ok=True)
    temp_dir.mkdir(parents=True, exist_ok=True)

    archives: list[dict[str, object]] = []

    for artifact_dir in artifact_dirs:
        archive_name = package_archive_name(meta, artifact_dir.name)
        archive_suffix = release_archive_suffix(archive_name)
        archive_path = packages_dir / archive_name
        stage_root = temp_dir / archive_name[: -len(archive_suffix)]

        if stage_root.exists():
            shutil.rmtree(stage_root)
        if publish_prebuilt_macos_payload(artifact_dir, archive_path):
            skipped_files = []
        else:
            stage_root.mkdir(parents=True, exist_ok=True)
            skipped_files = copy_release_artifact_contents(artifact_dir, stage_root)
            copy_docs(stage_root)
            build_fnql_webpak(stage_root)
            build_root_archive(stage_root)
            validate_stage_tree(stage_root, artifact_name=artifact_dir.name)
            write_release_archive(stage_root, archive_path)
        validate_release_archive_contents(archive_path, artifact_name=artifact_dir.name)
        checksum = sha256sum(archive_path)
        archives.append(
            {
                "artifact_dir": artifact_dir.name,
                "archive": archive_path.name,
                "format": archive_suffix[1:],
                "path": archive_path.relative_to(ROOT).as_posix(),
                "sha256": checksum,
                "skipped_artifact_file_count": len(skipped_files),
                "skipped_artifact_file_examples": skipped_files[:12],
            }
        )
        print(archive_path.relative_to(ROOT).as_posix())

    glx_rollback_package = attach_glx_rollback_archives(glx_rollback_package, archives)

    manifest = {
        "project": meta["project_name"],
        "channel": meta["channel"],
        "base_version": meta["base_version"],
        "version": meta["version"],
        "version_label": meta["version_label"],
        "release_tag": meta["release_tag"],
        "release_title": meta["release_title"],
        "build_date": meta["build_date"],
        "commit": meta["commit"],
        "glx_proof_corpus": release_corpus_manifest(),
        "glx_release_evidence_docs": GLX_RELEASE_EVIDENCE_DOCS,
        "glx_runtime_proof": glx_runtime_proof,
        "glx_promotion": glx_promotion,
        "glx_rollback_package": glx_rollback_package,
        "archives": archives,
    }

    (output_dir / "release-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    checksum_lines = [f"{archive['sha256']}  {Path(archive['path']).name}" for archive in archives]
    (output_dir / "SHA256SUMS.txt").write_text(
        "\n".join(checksum_lines) + ("\n" if checksum_lines else ""),
        encoding="utf-8",
        newline="\n",
    )
    return manifest


def main() -> int:
    args = parse_args()
    build_archives(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
