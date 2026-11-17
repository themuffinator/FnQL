from __future__ import annotations

import base64
import re
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

# The bundled HTML readme must work offline from inside a release package,
# so it carries the brand artwork inline instead of referencing files.
LOGO_SVG = ROOT / "misc" / "branding" / "fnql-logo.svg"
ICON_SVG = ROOT / "misc" / "branding" / "fnql-icon-small.svg"

_SVG_COMMENT = re.compile(r"<!--.*?-->", re.S)
_SVG_ROOT = re.compile(r"<svg\b([^>]*)>")
_SVG_SIZE = re.compile(r'\s(?:width|height)="[^"]*"')


def inline_svg(path: Path, label: str, css_class: str) -> str:
    """Return an SVG file as an accessible inline element sized by CSS."""
    text = _SVG_COMMENT.sub("", path.read_text(encoding="utf-8")).strip()
    if "$" in text:
        raise ValueError(f"{path} contains '$', which would break template rendering")
    match = _SVG_ROOT.search(text)
    if match is None or "viewBox" not in match.group(1):
        raise ValueError(f"{path} needs a root <svg> element with a viewBox")
    attributes = _SVG_SIZE.sub("", match.group(1))
    root = f'<svg{attributes} class="{css_class}" role="img" aria-label="{label}" focusable="false">'
    text = text[: match.start()] + root + text[match.end():]
    # Collapse indentation so the inline element stays compact.
    return "\n".join(line.strip() for line in text.splitlines() if line.strip())


def svg_data_uri(path: Path) -> str:
    data = _SVG_COMMENT.sub("", path.read_text(encoding="utf-8")).strip().encode("utf-8")
    return "data:image/svg+xml;base64," + base64.b64encode(data).decode("ascii")


def build_context() -> dict[str, object]:
    meta = base_metadata()
    return {
        **meta,
        "release_tag_example": f"{meta['tag_prefix']}{meta['version']}",
        "logo_svg": inline_svg(LOGO_SVG, meta["project_name_short"], "logo"),
        "icon_data_uri": svg_data_uri(ICON_SVG),
    }


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
    context = build_context()
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
