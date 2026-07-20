from __future__ import annotations

import argparse
import sys
import zipfile
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))

from release import REQUIRED_RELEASE_ARCHIVE_ENTRIES, validate_release_archive_contents
from root_archive import (
    DEFAULT_AUDIO_ZONE_ASSETS,
    DEFAULT_GLOBAL_FOG_ASSETS,
    DEFAULT_WEAPON_SOUND_SHADER_ASSETS,
    ROOT_ARCHIVE_NAME,
    validate_root_archive,
)


def verify_release_layout(root: Path) -> None:
    if root.is_file() and root.suffix.lower() == ".fnz":
        validate_root_archive(root)
        return

    if root.is_file() and root.suffix.lower() == ".zip":
        validate_release_archive_contents(root)
        return

    missing_release_entries = [
        entry
        for entry in REQUIRED_RELEASE_ARCHIVE_ENTRIES
        if not (root / entry).is_file()
    ]
    if missing_release_entries:
        raise FileNotFoundError(
            f"{root} is missing required release files: "
            + ", ".join(missing_release_entries)
        )

    root_archive = root / ROOT_ARCHIVE_NAME
    if not root_archive.is_file():
        raise FileNotFoundError(
            f"{root} is missing required root package archive: {ROOT_ARCHIVE_NAME}"
        )
    validate_root_archive(root_archive)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verify that a FnQL install/release root has required data layout.",
    )
    parser.add_argument(
        "root",
        type=Path,
        help="FnQL install or release root, for example a CI bin directory.",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    root = args.root.expanduser().resolve()
    try:
        verify_release_layout(root)
    except (OSError, zipfile.BadZipFile, ValueError) as exc:
        print(f"verify_release_layout.py: {exc}", file=sys.stderr)
        return 1

    if root.is_file():
        archive_kind = "root package archive" if root.suffix.lower() == ".fnz" else "release archive"
        print(
            f"{archive_kind} layout ok: "
            f"{len(DEFAULT_AUDIO_ZONE_ASSETS)} audio-zone sidecars under "
            f"{ROOT_ARCHIVE_NAME}/baseq3/maps, "
            f"{len(DEFAULT_GLOBAL_FOG_ASSETS)} fog sidecars, and "
            f"{len(DEFAULT_WEAPON_SOUND_SHADER_ASSETS)} sound shaders under "
            f"{ROOT_ARCHIVE_NAME}/<game>/sound"
        )
    else:
        print(
            "release layout ok: "
            f"{ROOT_ARCHIVE_NAME} contains {len(DEFAULT_AUDIO_ZONE_ASSETS)} "
            "audio-zone sidecars under baseq3/maps, "
            f"{len(DEFAULT_GLOBAL_FOG_ASSETS)} fog sidecars, and "
            f"{len(DEFAULT_WEAPON_SOUND_SHADER_ASSETS)} sound shaders under <game>/sound"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
