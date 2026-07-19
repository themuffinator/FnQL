from __future__ import annotations

import argparse
from pathlib import Path

from windows_pe import validate_windows_runtime_dependencies


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Reject packaged PE files that depend on unshipped compiler runtimes."
    )
    parser.add_argument("root", type=Path)
    parser.add_argument("--require-pe", action="store_true")
    args = parser.parse_args()

    root = args.root.resolve()
    files = [root] if root.is_file() else sorted(path for path in root.rglob("*") if path.is_file())
    pe_count = 0
    for path in files:
        info = validate_windows_runtime_dependencies(path.read_bytes(), name=str(path))
        if info is not None:
            pe_count += 1
    if args.require_pe and pe_count == 0:
        raise ValueError(f"No PE files found below {root}")
    print(f"Validated Windows runtime dependencies for {pe_count} PE file(s) below {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
