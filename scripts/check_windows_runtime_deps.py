from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from windows_pe import parse_pe_imported_functions, validate_windows_runtime_dependencies


@dataclass(frozen=True)
class RequiredImport:
    """A named function a packaged PE file must import from a DLL."""

    path: str
    dll: str
    function: str

    def __str__(self) -> str:
        return f"{self.path}={self.dll}!{self.function}"


def parse_required_import(text: str) -> RequiredImport:
    path, separator, remainder = text.partition("=")
    dll, bang, function = remainder.partition("!")
    if not separator or not bang or not path.strip() or not dll.strip() or not function.strip():
        raise argparse.ArgumentTypeError(
            f"expected FILE=DLL!FUNCTION, for example fnql.exe=winmm.dll!joyGetPosEx: {text!r}"
        )
    return RequiredImport(path.strip().replace("\\", "/"), dll.strip(), function.strip())


def _relative_key(root: Path, path: Path) -> str:
    return (path.name if root.is_file() else path.relative_to(root).as_posix()).casefold()


def check_required_imports(
    root: Path, files: list[Path], required: list[RequiredImport]
) -> None:
    """Fail when a required file is missing or does not import a required function.

    Import names are matched case-insensitively for the DLL, as Windows resolves
    them, and exactly for the function, as the PE name table stores them.
    """
    by_key = {_relative_key(root, path): path for path in files}
    for requirement in required:
        path = by_key.get(requirement.path.casefold())
        if path is None:
            raise ValueError(f"Required PE file {requirement.path} was not found below {root}")
        imported = parse_pe_imported_functions(path.read_bytes(), name=str(path))
        if imported is None:
            raise ValueError(f"Required PE file {path} is not a PE image")
        if requirement.function not in imported.get(requirement.dll.casefold(), ()):
            raise ValueError(
                f"{path} does not import {requirement.function} from {requirement.dll} "
                f"(required by {requirement})"
            )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Reject packaged PE files that depend on unshipped compiler runtimes."
    )
    parser.add_argument("root", type=Path)
    parser.add_argument("--require-pe", action="store_true")
    parser.add_argument(
        "--require-import",
        action="append",
        default=[],
        type=parse_required_import,
        metavar="FILE=DLL!FUNCTION",
        help="Require FILE (relative to root) to import FUNCTION from DLL. "
        "Gates platform features that a build can silently compile out.",
    )
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
    check_required_imports(root, files, args.require_import)
    print(f"Validated Windows runtime dependencies for {pe_count} PE file(s) below {root}")
    for requirement in args.require_import:
        print(f"Validated required import {requirement}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
