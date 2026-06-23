from __future__ import annotations

import re
import sys
from pathlib import Path


def shader_symbol_name(input_path: Path) -> str:
    stem = re.sub(r"[^A-Za-z0-9_]", "_", input_path.stem)
    if not stem:
        stem = "shader"
    if not (stem[0].isalpha() or stem[0] == "_"):
        stem = "_" + stem
    return f"fallbackShader_{stem}"


def stringify_shader(input_path: Path, output_path: Path) -> None:
    if input_path.expanduser().resolve() == output_path.expanduser().resolve():
        raise ValueError("shader input and output paths must be different")
    symbol = shader_symbol_name(input_path)
    with input_path.open("r", encoding="utf-8") as src, output_path.open(
        "w", encoding="utf-8", newline="\n"
    ) as dst:
        dst.write(f"const char *{symbol} =\n")
        for line in src:
            escaped = line.rstrip("\r\n").replace("\\", "\\\\").replace('"', '\\"')
            dst.write(f'"{escaped}\\n"\n')
        dst.write(";\n")


def main(argv: list[str] | None = None) -> int:
    args = sys.argv[1:] if argv is None else argv
    if len(args) != 2:
        print("usage: stringify_shader.py <input.glsl> <output.c>", file=sys.stderr)
        return 1

    try:
        stringify_shader(Path(args[0]), Path(args[1]))
    except OSError as exc:
        print(f"stringify_shader.py: {exc}", file=sys.stderr)
        return 1
    except ValueError as exc:
        print(f"stringify_shader.py: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
