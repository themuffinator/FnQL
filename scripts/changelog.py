from __future__ import annotations

import argparse
import datetime as _dt
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CHANGELOG = ROOT / "docs" / "fnql" / "CHANGELOG.md"


SECTION_RE = re.compile(r"^##\s+\[(?P<label>[^\]]+)\](?:\s+-\s+(?P<date>\d{4}-\d{2}-\d{2}))?\s*$")
CATEGORY_RE = re.compile(r"^###\s+(?P<label>.+?)\s*$")
BULLET_RE = re.compile(r"^\s*[-*]\s+(?P<text>.+?)\s*$")
RELEASE_LABEL_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]*$")

CHANGELOG_CATEGORIES = [
    "Highlights",
    "Compatibility",
    "Rendering and Display",
    "Audio",
    "Builds and Packaging",
    "Fixes",
    "Documentation and Tooling",
]

LEGACY_CATEGORY_MAP = {
    "added": "Highlights",
    "changed": "Highlights",
    "fixed": "Fixes",
    "removed": "Compatibility",
    "security": "Fixes",
}

NONE_MARKERS = {
    "_none yet._",
    "none yet.",
    "none yet",
    "no changes documented yet.",
    "no documented changes.",
}

CATEGORY_KEYWORDS = [
    (
        "Compatibility",
        (
            "compat",
            "demo",
            "protocol",
            "pak",
            "pk3",
            "vm",
            "qvm",
            "asset",
            "quake live",
            "quake ii",
            "retail",
        ),
    ),
    (
        "Fixes",
        (
            "fix",
            "fixed",
            "crash",
            "regression",
            "bug",
            "failure",
            "leak",
            "overflow",
            "assert",
        ),
    ),
    (
        "Rendering and Display",
        (
            "renderer",
            "rendering",
            "display",
            "opengl",
            "glx",
            "vulkan",
            "shader",
            "texture",
            "picmip",
            "hud",
            "screenshot",
            "bloom",
            "shadow",
            "lightmap",
            "hdr",
            "wal",
        ),
    ),
    (
        "Audio",
        (
            "audio",
            "sound",
            "openal",
            "hrtf",
            "efx",
            "ogg",
            "vorbis",
            "music",
        ),
    ),
    (
        "Builds and Packaging",
        (
            "release",
            "package",
            "archive",
            "artifact",
            "installer",
            "workflow",
            "build",
            "ci",
            "meson",
            "makefile",
            "msvc",
            "mingw",
            "linux",
            "macos",
            "windows",
        ),
    ),
    (
        "Documentation and Tooling",
        (
            "doc",
            "documentation",
            "readme",
            "tool",
            "script",
            "test",
            "docs",
        ),
    ),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="FnQL changelog helper")
    subparsers = parser.add_subparsers(dest="command", required=True)

    section = subparsers.add_parser("section")
    section.add_argument("--version", default="Unreleased", help="Section label such as Unreleased or 0.1.0")
    section.add_argument("--changelog", type=Path, default=DEFAULT_CHANGELOG)
    section.add_argument("--clean", action="store_true", help="Dedupe and categorize section output")

    cleanup = subparsers.add_parser("cleanup")
    cleanup.add_argument("--version", default="Unreleased", help="Section label to clean up in place")
    cleanup.add_argument("--changelog", type=Path, default=DEFAULT_CHANGELOG)

    clear = subparsers.add_parser("clear-unreleased")
    clear.add_argument("--changelog", type=Path, default=DEFAULT_CHANGELOG)

    release = subparsers.add_parser("prepare-release")
    release.add_argument("--version", required=True, type=release_label_arg, help="Release version to stamp")
    release.add_argument("--date", default=_dt.date.today().isoformat(), type=release_date_arg)
    release.add_argument("--changelog", type=Path, default=DEFAULT_CHANGELOG)

    return parser.parse_args()


def validate_release_label(value: str) -> str:
    label = value.strip()
    if not RELEASE_LABEL_RE.fullmatch(label):
        raise ValueError(
            "Release version must be a single safe label using letters, digits, '.', '_', '+', or '-'."
        )
    return label


def validate_release_date(value: str) -> str:
    date_value = value.strip()
    try:
        parsed = _dt.date.fromisoformat(date_value)
    except ValueError as exc:
        raise ValueError("Release date must use YYYY-MM-DD format.") from exc
    if parsed.isoformat() != date_value:
        raise ValueError("Release date must use YYYY-MM-DD format.")
    return date_value


def release_label_arg(value: str) -> str:
    try:
        return validate_release_label(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc


def release_date_arg(value: str) -> str:
    try:
        return validate_release_date(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc


def read_sections(path: Path) -> list[tuple[str, str | None, list[str]]]:
    lines = path.read_text(encoding="utf-8").splitlines()
    sections: list[tuple[str, str | None, list[str]]] = []
    current_label: str | None = None
    current_date: str | None = None
    current_lines: list[str] = []

    for line in lines:
        match = SECTION_RE.match(line)
        if match:
            if current_label is not None:
                sections.append((current_label, current_date, current_lines))
            current_label = match.group("label")
            current_date = match.group("date")
            current_lines = []
            continue

        if current_label is not None:
            current_lines.append(line)

    if current_label is not None:
        sections.append((current_label, current_date, current_lines))
    return sections


def section_text(path: Path, target: str) -> str:
    for label, _date, lines in read_sections(path):
        if label.lower() == target.lower():
            body = "\n".join(lines).strip()
            if not body:
                return "- No changes documented yet.\n"
            return f"{body}\n"
    raise KeyError(f"Section [{target}] was not found in {path}")


def is_placeholder_bullet(text: str) -> bool:
    return text.strip().lower() in NONE_MARKERS


def normalize_bullet_text(text: str) -> str:
    cleaned = text.strip()
    cleaned = re.sub(r"\s+", " ", cleaned)
    return cleaned


def normalized_bullet_key(text: str) -> str:
    cleaned = re.sub(r"`([^`]+)`", r"\1", text.lower())
    cleaned = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", cleaned)
    cleaned = re.sub(r"[^a-z0-9]+", " ", cleaned)
    return cleaned.strip()


def canonical_category(label: str | None) -> str | None:
    if not label:
        return None
    cleaned = label.strip()
    for category in CHANGELOG_CATEGORIES:
        if cleaned.lower() == category.lower():
            return category
    return None


def infer_category(text: str, current_label: str | None) -> str:
    explicit = canonical_category(current_label)
    if explicit:
        return explicit

    lowered = text.lower()
    for category, keywords in CATEGORY_KEYWORDS:
        if any(keyword_matches(lowered, keyword) for keyword in keywords):
            return category

    if current_label:
        legacy = LEGACY_CATEGORY_MAP.get(current_label.strip().lower())
        if legacy:
            return legacy
    return "Highlights"


def keyword_matches(text: str, keyword: str) -> bool:
    if " " in keyword:
        return keyword in text
    return re.search(rf"\b{re.escape(keyword)}\b", text) is not None


def change_items(section_lines: list[str]) -> list[tuple[str, str]]:
    items: list[tuple[str, str]] = []
    seen: set[str] = set()
    current_label: str | None = None

    for line in section_lines:
        heading = CATEGORY_RE.match(line)
        if heading:
            current_label = heading.group("label")
            continue

        bullet = BULLET_RE.match(line)
        if not bullet:
            continue

        text = normalize_bullet_text(bullet.group("text"))
        if not text or is_placeholder_bullet(text):
            continue

        key = normalized_bullet_key(text)
        if not key or key in seen:
            continue
        seen.add(key)
        items.append((infer_category(text, current_label), text))

    return items


def render_items(items: list[tuple[str, str]], *, include_empty: bool) -> list[str]:
    by_category: dict[str, list[str]] = {category: [] for category in CHANGELOG_CATEGORIES}
    for category, text in items:
        by_category.setdefault(category, []).append(text)

    rendered: list[str] = []
    for category in CHANGELOG_CATEGORIES:
        bullets = by_category.get(category, [])
        if not include_empty and not bullets:
            continue
        if rendered:
            rendered.append("")
        rendered.append(f"### {category}")
        if bullets:
            rendered.extend(f"- {bullet}" for bullet in bullets)
        else:
            rendered.append("- _None yet._")

    return rendered


def empty_unreleased_lines() -> list[str]:
    return ["", *render_items([], include_empty=True)]


def clean_section_lines(section_lines: list[str], *, include_empty: bool = False) -> list[str]:
    items = change_items(section_lines)
    if not items and not include_empty:
        return ["- No documented changes."]
    return render_items(items, include_empty=include_empty)


def clean_section_text(path: Path, target: str) -> str:
    for label, _date, lines in read_sections(path):
        if label.lower() == target.lower():
            return "\n".join(clean_section_lines(lines)).strip() + "\n"
    raise KeyError(f"Section [{target}] was not found in {path}")


def rewrite_section(path: Path, target: str, body_lines: list[str]) -> None:
    lines = path.read_text(encoding="utf-8").splitlines()
    section_index = -1
    for index, line in enumerate(lines):
        match = SECTION_RE.match(line)
        if match and match.group("label").lower() == target.lower():
            section_index = index
            break
    if section_index < 0:
        raise ValueError(f"Missing section header: ## [{target}]")

    next_section_index = len(lines)
    for index in range(section_index + 1, len(lines)):
        if SECTION_RE.match(lines[index]):
            next_section_index = index
            break

    updated_lines = lines[: section_index + 1] + body_lines + lines[next_section_index:]
    path.write_text("\n".join(updated_lines).rstrip() + "\n", encoding="utf-8", newline="\n")


def cleanup_section(path: Path, target: str) -> None:
    for label, _date, lines in read_sections(path):
        if label.lower() == target.lower():
            rewrite_section(path, target, ["", *clean_section_lines(lines, include_empty=True)])
            return
    raise KeyError(f"Section [{target}] was not found in {path}")


def clear_unreleased(path: Path) -> None:
    rewrite_section(path, "Unreleased", empty_unreleased_lines())


def prepare_release(path: Path, version: str, date_value: str) -> str:
    version = validate_release_label(version)
    date_value = validate_release_date(date_value)
    lines = path.read_text(encoding="utf-8").splitlines()
    unreleased_header = "## [Unreleased]"
    release_header = f"## [{version}] - {date_value}"
    for label, _date, _lines in read_sections(path):
        if label.lower() == version.lower():
            raise ValueError(f"Release section already exists for version: {version}")

    try:
        unreleased_index = lines.index(unreleased_header)
    except ValueError as exc:
        raise ValueError(f"Missing section header: {unreleased_header}")

    next_section_index = len(lines)
    for index in range(unreleased_index + 1, len(lines)):
        if SECTION_RE.match(lines[index]):
            next_section_index = index
            break

    unreleased_body = lines[unreleased_index + 1 : next_section_index]
    trimmed_body = [line for line in unreleased_body]
    while trimmed_body and not trimmed_body[0].strip():
        trimmed_body = trimmed_body[1:]
    while trimmed_body and not trimmed_body[-1].strip():
        trimmed_body = trimmed_body[:-1]

    refreshed_unreleased = empty_unreleased_lines()
    release_block = ["", release_header, ""]
    release_block.extend(clean_section_lines(trimmed_body) if trimmed_body else ["- No documented changes."])
    updated_lines = (
        lines[: unreleased_index + 1]
        + refreshed_unreleased
        + release_block
        + lines[next_section_index:]
    )
    path.write_text("\n".join(updated_lines).rstrip() + "\n", encoding="utf-8", newline="\n")
    return release_header


def main() -> int:
    args = parse_args()
    if args.command == "section":
        if args.clean:
            sys.stdout.write(clean_section_text(args.changelog, args.version))
        else:
            sys.stdout.write(section_text(args.changelog, args.version))
        return 0

    if args.command == "cleanup":
        cleanup_section(args.changelog, args.version)
        return 0

    if args.command == "clear-unreleased":
        clear_unreleased(args.changelog)
        return 0

    header = prepare_release(args.changelog, args.version, args.date)
    print(header)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
