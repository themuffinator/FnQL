from __future__ import annotations

import os
import zipfile
from pathlib import Path
from typing import Iterable

from fnql_meta import ROOT
from stock_ql_maps import STOCK_QL_MAPS


ROOT_ARCHIVE_NAME = "FnQL-pkg.fnz"
PKG_ROOT = ROOT / "pkg"

DEFAULT_AUDIO_ZONE_ASSETS = [
    (
        PKG_ROOT / "baseq3" / "maps" / f"{map_name}.azb",
        Path("baseq3") / "maps" / f"{map_name}.azb",
    )
    for map_name in STOCK_QL_MAPS
]

DEFAULT_GLOBAL_FOG_ASSETS = [
    (
        PKG_ROOT / "baseq3" / "maps" / f"{map_name}.fog",
        Path("baseq3") / "maps" / f"{map_name}.fog",
    )
    for map_name in STOCK_QL_MAPS
]

DEFAULT_WEAPON_SOUND_SHADER_ASSETS = [
    (
        PKG_ROOT / "baseq3" / "sound" / "fnql-weapon-sounds.sndshd",
        Path("baseq3") / "sound" / "fnql-weapon-sounds.sndshd",
    ),
    (
        PKG_ROOT / "missionpack" / "sound" / "fnql-weapon-sounds.sndshd",
        Path("missionpack") / "sound" / "fnql-weapon-sounds.sndshd",
    ),
]

DEFAULT_RENDERER_SHADER_ASSETS = [
    (
        PKG_ROOT / "baseq3" / "scripts" / "fnql.shader",
        Path("baseq3") / "scripts" / "fnql.shader",
    ),
]

DEFAULT_ROOT_ARCHIVE_REQUIRED_ASSETS = [
    *DEFAULT_AUDIO_ZONE_ASSETS,
    *DEFAULT_GLOBAL_FOG_ASSETS,
    *DEFAULT_RENDERER_SHADER_ASSETS,
    *DEFAULT_WEAPON_SOUND_SHADER_ASSETS,
]


def iter_package_assets(package_root: Path = PKG_ROOT) -> list[tuple[Path, Path]]:
    package_root = package_root.expanduser()
    resolved_package_root = package_root.resolve()
    if not package_root.is_dir():
        raise FileNotFoundError(f"Missing root archive source directory: {package_root}")

    assets: list[tuple[Path, Path]] = []
    for source in sorted(package_root.rglob("*"), key=lambda path: path.as_posix().lower()):
        if not source.is_file():
            continue
        if source.is_symlink():
            raise ValueError(f"Root archive package asset must not be a symbolic link: {source}")

        resolved_source = source.resolve()
        if not path_is_relative_to(resolved_source, resolved_package_root):
            raise ValueError(f"Root archive package asset escapes package root: {source}")

        dest_relative = source.relative_to(package_root)
        if dest_relative.name == ".gitkeep":
            continue

        assets.append((source, dest_relative))

    if not assets:
        raise FileNotFoundError(f"No root archive assets found under {package_root}")

    return assets


def path_is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
    except ValueError:
        return False
    return True


def archive_member_name(dest_relative: Path | str) -> str:
    archive_name = (
        dest_relative.as_posix()
        if isinstance(dest_relative, Path)
        else str(dest_relative)
    )
    if not archive_name:
        raise ValueError("Root archive entry name must not be empty")
    if "\\" in archive_name:
        raise ValueError(f"Root archive entry must use forward slashes: {archive_name}")
    if archive_name.startswith("/"):
        raise ValueError(f"Root archive entry must be relative: {archive_name}")

    parts = archive_name.split("/")
    if any(part in {"", ".", ".."} for part in parts):
        raise ValueError(f"Root archive entry contains unsafe path component: {archive_name}")
    if any(":" in part for part in parts):
        raise ValueError(f"Root archive entry must not contain a drive prefix or stream name: {archive_name}")
    if any(any(ord(char) < 32 or ord(char) == 127 for char in part) for part in parts):
        raise ValueError(f"Root archive entry contains unsafe control character: {archive_name!r}")
    return archive_name


def validate_archive_member_names(archived_names: Iterable[str], *, archive_name: str) -> None:
    seen: set[str] = set()
    for name in archived_names:
        try:
            safe_name = archive_member_name(name)
        except ValueError as exc:
            raise ValueError(f"{archive_name} contains unsafe package asset path: {name}") from exc
        key = safe_name.lower()
        if key in seen:
            raise ValueError(f"{archive_name} contains duplicate package asset path: {safe_name}")
        seen.add(key)


def required_root_archive_names(
    assets: Iterable[tuple[Path, Path]] = DEFAULT_ROOT_ARCHIVE_REQUIRED_ASSETS,
) -> list[str]:
    return [archive_member_name(dest_relative) for _source, dest_relative in assets]


def required_audio_zone_archive_names(
    assets: Iterable[tuple[Path, Path]] = DEFAULT_AUDIO_ZONE_ASSETS,
) -> list[str]:
    return required_root_archive_names(assets)


def required_global_fog_archive_names(
    assets: Iterable[tuple[Path, Path]] = DEFAULT_GLOBAL_FOG_ASSETS,
) -> list[str]:
    return required_root_archive_names(assets)


def validate_root_archive_names(
    archived_names: Iterable[str],
    *,
    archive_name: str = ROOT_ARCHIVE_NAME,
    assets: Iterable[tuple[Path, Path]] = DEFAULT_ROOT_ARCHIVE_REQUIRED_ASSETS,
) -> None:
    archived_name_list = list(archived_names)
    validate_archive_member_names(archived_name_list, archive_name=archive_name)
    present_names = set(archived_name_list)
    missing_assets = [
        name
        for name in required_root_archive_names(assets)
        if name not in present_names
    ]
    if missing_assets:
        raise ValueError(
            f"{archive_name} is missing required package assets: "
            + ", ".join(missing_assets[:8])
        )


def validate_root_archive(archive_path: Path) -> None:
    with zipfile.ZipFile(archive_path) as archive:
        archived_names = [
            info.filename
            for info in archive.infolist()
            if not info.is_dir()
        ]
    validate_root_archive_names(archived_names, archive_name=archive_path.name)


def write_root_archive(
    archive_path: Path,
    *,
    package_root: Path = PKG_ROOT,
    assets: Iterable[tuple[Path, Path]] | None = None,
) -> None:
    archive_path = archive_path.expanduser()
    package_root = package_root.expanduser()
    resolved_package_root = package_root.resolve()
    if path_is_relative_to(archive_path.resolve(), resolved_package_root):
        raise ValueError(f"Archive output must not be inside package source tree: {archive_path}")

    archive_assets = list(iter_package_assets(package_root) if assets is None else assets)
    archive_path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = archive_path.with_name(f"{archive_path.name}.tmp")

    if temp_path.exists():
        temp_path.unlink()

    try:
        with zipfile.ZipFile(temp_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archived_names: set[str] = set()
            for source, dest_relative in archive_assets:
                source = source.expanduser()
                if not source.is_file():
                    raise FileNotFoundError(f"Missing root archive asset: {source}")
                if source.is_symlink():
                    raise ValueError(f"Root archive package asset must not be a symbolic link: {source}")
                if not path_is_relative_to(source.resolve(), resolved_package_root):
                    raise ValueError(f"Root archive package asset escapes package root: {source}")

                archive_name = archive_member_name(dest_relative)
                archive_key = archive_name.lower()
                if archive_key in archived_names:
                    raise ValueError(f"Duplicate root archive entry: {archive_name}")
                archived_names.add(archive_key)

                info = zipfile.ZipInfo(archive_name)
                info.compress_type = zipfile.ZIP_DEFLATED
                info.date_time = (1980, 1, 1, 0, 0, 0)
                info.external_attr = 0o644 << 16
                archive.writestr(info, source.read_bytes())

        os.replace(temp_path, archive_path)
    finally:
        if temp_path.exists():
            temp_path.unlink()
