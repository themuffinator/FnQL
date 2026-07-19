from __future__ import annotations

import argparse
import hashlib
import io
import json
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

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
    DEFAULT_AUDIO_ZONE_ASSETS,
    ROOT_ARCHIVE_NAME,
    STANDARD_Q3A_AUDIO_ZONE_MAPS,
    path_is_relative_to,
    validate_archive_member_names,
    validate_root_archive,
    validate_root_archive_names,
    write_root_archive,
)


DEFAULT_DOCS = [
    (ROOT / "LICENSE", Path("LICENSE")),
    (ROOT / "docs" / "fnql" / "TECHNICAL.md", Path("docs") / "fnql" / "TECHNICAL.md"),
    (
        ROOT / "docs" / "fnql" / "RTX_RENDERER.md",
        Path("docs") / "fnql" / "RTX_RENDERER.md",
    ),
    (
        ROOT / "docs" / "GLX.md",
        Path("docs") / "GLX.md",
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

PUBLIC_RENDERERS = frozenset({"glx", "vk", "rtx"})
RENDERER_MODULE_RE = re.compile(
    r"(?:^|/)fnql_(?P<renderer>[a-z0-9]+)_[^/]+\.(?:dll|so|dylib)$",
    re.IGNORECASE,
)
FORBIDDEN_RELEASE_ARCH_RE = re.compile(
    r"(?<![a-z0-9])(?:x86[_-]?64|x64|amd64|arm64|aarch64|mingw64)(?![a-z0-9])",
    re.IGNORECASE,
)

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


def copy_docs(stage_root: Path) -> None:
    for source, dest_relative in DEFAULT_DOCS:
        destination = stage_root / dest_relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def copy_standard_audio_zone_assets(stage_root: Path) -> None:
    for source, dest_relative in DEFAULT_AUDIO_ZONE_ASSETS:
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


def validate_release_architecture_names(names: list[str]) -> None:
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


def validate_release_binary_stream(name: str, handle: object) -> None:
    header = handle.read(64)
    if header.startswith(b"MZ") and len(header) >= 64:
        pe_offset = int.from_bytes(header[60:64], "little")
        handle.seek(pe_offset)
        pe_header = handle.read(6)
        if pe_header.startswith(b"PE\0\0"):
            machine = int.from_bytes(pe_header[4:6], "little")
            if machine != 0x014C:
                raise ValueError(
                    f"release artifacts are 32-bit x86 only; {name} has PE machine 0x{machine:04x}"
                )
        return

    if header.startswith(b"\x7fELF") and len(header) >= 20:
        elf_class = header[4]
        byte_order = "little" if header[5] == 1 else "big"
        machine = int.from_bytes(header[18:20], byte_order)
        if elf_class != 1 or machine != 3:
            raise ValueError(
                f"release artifacts are 32-bit x86 only; {name} has ELF class {elf_class} "
                f"and machine {machine}"
            )
        return

    macho_magic = header[:4]
    if macho_magic in (b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf"):
        raise ValueError(
            f"release artifacts are 32-bit x86 only; {name} is a 64-bit Mach-O binary"
        )
    if macho_magic in (
        b"\xca\xfe\xba\xbe",
        b"\xbe\xba\xfe\xca",
        b"\xca\xfe\xba\xbf",
        b"\xbf\xba\xfe\xca",
    ):
        raise ValueError(
            f"release artifacts are 32-bit x86 only; {name} is a universal Mach-O binary"
        )
    if macho_magic in (b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xce") and len(header) >= 8:
        byte_order = "little" if macho_magic == b"\xce\xfa\xed\xfe" else "big"
        cpu_type = int.from_bytes(header[4:8], byte_order)
        if cpu_type != 7:
            raise ValueError(
                f"release artifacts are 32-bit x86 only; {name} has Mach-O CPU type {cpu_type}"
            )


def validate_release_archive_contents(archive_path: Path) -> None:
    with zipfile.ZipFile(archive_path) as archive:
        archived_names = [
            info.filename
            for info in archive.infolist()
            if not info.is_dir()
        ]
        validate_archive_member_names(archived_names, archive_name=archive_path.name)
        validate_release_architecture_names(archived_names)
        validate_renderer_module_names(archived_names)
        for info in archive.infolist():
            if info.is_dir():
                continue
            with archive.open(info) as member:
                validate_release_binary_stream(info.filename, member)
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
        if ROOT_ARCHIVE_NAME not in archived_name_set:
            raise ValueError(f"{archive_path.name} is missing {ROOT_ARCHIVE_NAME}")

        root_archive_bytes = archive.read(ROOT_ARCHIVE_NAME)

    with zipfile.ZipFile(io.BytesIO(root_archive_bytes)) as root_archive:
        root_archive_names = [
            info.filename
            for info in root_archive.infolist()
            if not info.is_dir()
        ]
    validate_root_archive_names(root_archive_names)


def validate_stage_tree(stage_root: Path) -> None:
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
    validate_release_architecture_names(staged_names)
    validate_renderer_module_names(staged_names)
    for relative_name in staged_names:
        with (stage_root / Path(relative_name)).open("rb") as handle:
            validate_release_binary_stream(relative_name, handle)


def release_artifact_dirs(artifact_root: Path) -> list[Path]:
    if not artifact_root.exists():
        raise FileNotFoundError(f"Artifact root does not exist: {artifact_root}")
    if not artifact_root.is_dir():
        raise NotADirectoryError(f"Artifact root is not a directory: {artifact_root}")

    artifact_dirs = sorted(path for path in artifact_root.iterdir() if path.is_dir())
    if not artifact_dirs:
        raise ValueError(f"Artifact root does not contain any artifact directories: {artifact_root}")
    validate_release_architecture_names([path.name for path in artifact_dirs])
    return artifact_dirs


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
        archive_base = packages_dir / archive_name[:-4]
        stage_root = temp_dir / archive_name[:-4]

        if stage_root.exists():
            shutil.rmtree(stage_root)
        stage_root.mkdir(parents=True, exist_ok=True)
        skipped_files = copy_release_artifact_contents(artifact_dir, stage_root)
        copy_docs(stage_root)
        build_fnql_webpak(stage_root)
        build_root_archive(stage_root)
        validate_stage_tree(stage_root)

        archive_path = Path(shutil.make_archive(str(archive_base), "zip", root_dir=stage_root))
        validate_release_archive_contents(archive_path)
        checksum = sha256sum(archive_path)
        archives.append(
            {
                "artifact_dir": artifact_dir.name,
                "archive": archive_path.name,
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
