from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import os
import tempfile
import urllib.request
from pathlib import Path

from windows_pe import PE_I386, parse_pe


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "version" / "fnql_steam_provider.json"
MAX_PROVIDER_BYTES = 16 * 1024 * 1024


def load_manifest(path: Path) -> dict[str, str]:
    data = json.loads(path.read_text(encoding="utf-8"))
    required = ("version", "tag", "asset", "url", "sha256", "pe_machine")
    for key in required:
        if not isinstance(data.get(key), str) or not data[key]:
            raise ValueError(f"{path} has an invalid {key!r} field")
    digest = data["sha256"].casefold()
    if len(digest) != 64 or any(character not in "0123456789abcdef" for character in digest):
        raise ValueError(f"{path} has an invalid SHA-256 digest")
    if data["asset"] != "fnql_steam.dll" or data["pe_machine"] != "i386":
        raise ValueError(f"{path} must pin the retail-compatible Win32 provider")
    return data


def read_provider(*, source: Path | None, url: str) -> bytes:
    if source is not None:
        data = source.read_bytes()
    else:
        request = urllib.request.Request(url, headers={"User-Agent": "FnQL-release-builder"})
        with urllib.request.urlopen(request, timeout=60) as response:
            length = response.headers.get("Content-Length")
            if length is not None and int(length) > MAX_PROVIDER_BYTES:
                raise ValueError("pinned Steam provider is unexpectedly large")
            data = response.read(MAX_PROVIDER_BYTES + 1)
    if not data or len(data) > MAX_PROVIDER_BYTES:
        raise ValueError("pinned Steam provider has an invalid size")
    return data


def validate_provider(data: bytes, *, expected_sha256: str) -> str:
    digest = hashlib.sha256(data).hexdigest()
    if not hmac.compare_digest(digest, expected_sha256.casefold()):
        raise ValueError(
            f"Steam provider SHA-256 mismatch: expected {expected_sha256}, got {digest}"
        )
    info = parse_pe(data, name="fnql_steam.dll")
    if info is None or info.machine != PE_I386 or not info.is_dll:
        raise ValueError("Steam provider must be a 32-bit i386 PE DLL")
    return digest


def stage_provider(
    output: Path,
    *,
    manifest_path: Path = DEFAULT_MANIFEST,
    source: Path | None = None,
) -> str:
    manifest = load_manifest(manifest_path)
    data = read_provider(source=source, url=manifest["url"])
    digest = validate_provider(data, expected_sha256=manifest["sha256"])

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", prefix=f".{output.name}.", dir=output.parent, delete=False
        ) as handle:
            handle.write(data)
            temporary_path = Path(handle.name)
        os.replace(temporary_path, output)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)
    return digest


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Fetch, verify, and stage the pinned binary-only FnQL Steam provider."
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument(
        "--source",
        type=Path,
        help="Use a local provider binary while retaining all manifest checks.",
    )
    args = parser.parse_args()

    digest = stage_provider(
        args.output.resolve(),
        manifest_path=args.manifest.resolve(),
        source=args.source.resolve() if args.source is not None else None,
    )
    print(f"Staged verified Win32 Steam provider: {args.output} (sha256:{digest})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
