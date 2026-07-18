from __future__ import annotations

import argparse
import os
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


DATAPACK_VERSION = 4
DATAPACK_ENCODING_BINARY = 0
MAX_RESOURCE_BYTES = 64 * 1024 * 1024
MAX_RESOURCE_COUNT = 0xFFFF
MAX_VIRTUAL_PATH = 255
PROHIBITED_SOURCE_NAMES = {"bundle.js", "web.pak", "fnql-web.pak"}


@dataclass(frozen=True)
class WebPakResource:
    resource_id: int
    virtual_path: str
    payload: bytes


def _checked_virtual_path(relative_path: Path) -> str:
    virtual_path = relative_path.as_posix()
    if (
        not virtual_path
        or virtual_path.startswith("/")
        or ".." in relative_path.parts
        or any(character in virtual_path for character in (":", "\\", "\0"))
        or len(virtual_path) > MAX_VIRTUAL_PATH
    ):
        raise ValueError(f"unsafe WebPak resource path: {virtual_path!r}")
    try:
        virtual_path.encode("ascii")
    except UnicodeEncodeError as exc:
        raise ValueError(f"WebPak resource paths must be ASCII: {virtual_path!r}") from exc
    if virtual_path != virtual_path.lower():
        raise ValueError(f"WebPak resource paths must be lowercase: {virtual_path!r}")
    if relative_path.name.casefold() in PROHIBITED_SOURCE_NAMES:
        raise ValueError(
            f"{virtual_path} is not a project-owned sparse overlay resource; "
            "retail web.pak content must remain external"
        )
    return virtual_path


def collect_resources(source_root: Path) -> list[WebPakResource]:
    source_root = source_root.expanduser().resolve()
    if not source_root.is_dir():
        raise NotADirectoryError(f"WebPak source root is not a directory: {source_root}")

    candidates: list[tuple[str, Path]] = []
    for source in source_root.rglob("*"):
        if source.is_symlink():
            raise ValueError(f"WebPak sources may not contain symbolic links: {source}")
        if not source.is_file():
            continue
        virtual_path = _checked_virtual_path(source.relative_to(source_root))
        candidates.append((virtual_path, source))

    candidates.sort(key=lambda item: item[0])
    if not candidates:
        raise ValueError(f"WebPak source root contains no files: {source_root}")
    if len(candidates) + 1 > MAX_RESOURCE_COUNT:
        raise ValueError("WebPak resource count exceeds the Chromium DataPack v4 limit")

    resources: list[WebPakResource] = []
    previous_path = ""
    for resource_id, (virtual_path, source) in enumerate(candidates, start=1):
        if virtual_path == previous_path:
            raise ValueError(f"duplicate WebPak resource path: {virtual_path}")
        previous_path = virtual_path
        payload = source.read_bytes()
        if len(payload) > MAX_RESOURCE_BYTES:
            raise ValueError(
                f"WebPak resource exceeds {MAX_RESOURCE_BYTES} bytes: {virtual_path}"
            )
        resources.append(WebPakResource(resource_id, virtual_path, payload))
    return resources


def build_ql_manifest(resources: Sequence[WebPakResource]) -> bytes:
    if not resources:
        raise ValueError("the QL WebPak manifest requires at least one resource")

    manifest = bytearray(struct.pack("<II", 0, 7))
    for index, resource in enumerate(resources):
        # Retail QL's launcher manifest associates each path with the resource
        # identifier stored in the following record; the final identifier is in
        # the trailer. The first link is validated but otherwise unused.
        linked_id = resource.resource_id if index == 0 else resources[index - 1].resource_id
        encoded_path = resource.virtual_path.encode("utf-16le")
        manifest.extend(
            struct.pack(
                "<III",
                1 if index == 0 else 3,
                linked_id,
                len(resource.virtual_path),
            )
        )
        manifest.extend(encoded_path)
        while len(manifest) % 4:
            manifest.append(0)

    manifest.extend(struct.pack("<II", 3, resources[-1].resource_id))
    struct.pack_into("<I", manifest, 0, len(manifest) - 4)
    return bytes(manifest)


def encode_webpak(resources: Sequence[WebPakResource]) -> bytes:
    if not resources:
        raise ValueError("cannot encode an empty WebPak")

    manifest = build_ql_manifest(resources)
    payloads = [manifest, *(resource.payload for resource in resources)]
    resource_count = len(payloads)
    if resource_count > MAX_RESOURCE_COUNT:
        raise ValueError("WebPak resource count exceeds the Chromium DataPack v4 limit")

    header_size = 9
    entry_table_size = (resource_count + 1) * 6
    next_offset = header_size + entry_table_size
    entries = bytearray()
    for resource_id, payload in enumerate(payloads):
        entries.extend(struct.pack("<HI", resource_id, next_offset))
        next_offset += len(payload)
    entries.extend(struct.pack("<HI", 0, next_offset))

    header = struct.pack(
        "<IIB", DATAPACK_VERSION, resource_count, DATAPACK_ENCODING_BINARY
    )
    return b"".join((header, bytes(entries), *payloads))


def build_webpak(source_root: Path, output_path: Path) -> tuple[int, int]:
    resources = collect_resources(source_root)
    encoded = encode_webpak(resources)
    output_path = output_path.expanduser().resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = output_path.with_name(f".{output_path.name}.{os.getpid()}.tmp")
    try:
        temporary_path.write_bytes(encoded)
        temporary_path.replace(output_path)
    finally:
        if temporary_path.exists():
            temporary_path.unlink()
    return len(resources), len(encoded)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build FnQL's deterministic sparse Chromium DataPack v4 WebUI overlay."
    )
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        resource_count, byte_count = build_webpak(args.source_root, args.output)
    except (OSError, ValueError) as exc:
        print(f"build_webpak.py: {exc}", file=sys.stderr)
        return 1
    print(
        f"built {args.output}: {resource_count} sparse resources, {byte_count} bytes"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
