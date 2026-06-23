from __future__ import annotations

import sys
from pathlib import Path
from string import Template

sys.path.insert(0, str(Path(__file__).resolve().parent))

from fnql_meta import base_metadata


ROOT = Path(__file__).resolve().parents[1]

README_TEMPLATE = ROOT / "docs" / "templates" / "README.md.in"
INSTALL_TEMPLATE = ROOT / "docs" / "templates" / "install-readme.html.in"

README_OUTPUT = ROOT / "README.md"
INSTALL_OUTPUT = ROOT / ".install" / "README.html"


def render(template_path: Path, context: dict[str, object]) -> str:
    template = Template(template_path.read_text(encoding="utf-8"))
    rendered_context = {key: str(value) for key, value in context.items()}
    try:
        rendered = template.substitute(rendered_context)
    except KeyError as exc:
        missing = str(exc.args[0])
        raise ValueError(f"{template_path} references undefined template key: {missing}") from exc
    except ValueError as exc:
        raise ValueError(f"{template_path} contains an invalid template placeholder: {exc}") from exc
    return rendered.strip() + "\n"


def write_if_changed(path: Path, content: str) -> bool:
    path.parent.mkdir(parents=True, exist_ok=True)
    current = path.read_text(encoding="utf-8") if path.exists() else None
    if current == content:
        return False
    path.write_text(content, encoding="utf-8", newline="\n")
    return True


def main() -> int:
    meta = base_metadata()
    context = {
        **meta,
        "release_tag_example": f"{meta['tag_prefix']}{meta['version']}",
    }
    changed = [
        path
        for path, content in (
            (README_OUTPUT, render(README_TEMPLATE, context)),
            (INSTALL_OUTPUT, render(INSTALL_TEMPLATE, context)),
        )
        if write_if_changed(path, content)
    ]

    for path in changed:
        print(path.relative_to(ROOT).as_posix())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
