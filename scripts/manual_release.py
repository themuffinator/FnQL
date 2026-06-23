from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import textwrap
import urllib.error
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from fnql_meta import (
    ROOT,
    VERSION_HEADER,
    base_metadata,
    compose_version_string,
    compose_windows_version,
    normalize_commit,
    normalize_date,
)
from changelog import DEFAULT_CHANGELOG, clean_section_text, clear_unreleased


GITHUB_MODELS_ENDPOINT = "https://models.github.ai/inference/chat/completions"
DEFAULT_RELEASE_NOTES_MODEL = "openai/gpt-4.1"
MAX_RELEASE_NOTES_CONTEXT_CHARS = 60000
FIELD_SEPARATOR = "\x1f"
RECORD_SEPARATOR = "\x1e"


def non_negative_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be non-negative")
    return parsed


def positive_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc
    if parsed < 1:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return parsed


def write_text_lf(path: Path, content: str) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(content)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="FnQL manual release build helper")
    subparsers = parser.add_subparsers(dest="command", required=True)

    for command in ("summary", "github-output"):
        subparser = subparsers.add_parser(command)
        subparser.add_argument("--build-date")
        subparser.add_argument("--head-commit")

    stamp = subparsers.add_parser("stamp-version")
    stamp.add_argument("--build-number", required=True, type=non_negative_int)
    stamp.add_argument("--header", type=Path, default=VERSION_HEADER)

    notes = subparsers.add_parser("release-notes")
    notes.add_argument("--build-number", required=True, type=non_negative_int)
    notes.add_argument("--build-date")
    notes.add_argument("--from-commit")
    notes.add_argument("--to-commit")
    notes.add_argument("--changelog", type=Path, default=DEFAULT_CHANGELOG)
    notes.add_argument("--output", type=Path)
    notes.add_argument(
        "--highlights-file",
        type=Path,
        help="Use pre-generated AI release-note highlights instead of generating them here",
    )
    notes.add_argument(
        "--clear-changelog",
        action="store_true",
        help="Reset the Unreleased changelog section after release notes are written",
    )
    notes.add_argument("--no-ai", action="store_true", help="Skip built-in GitHub Models release-note generation")
    notes.add_argument(
        "--notes-model",
        default=os.environ.get("FNQL_RELEASE_NOTES_MODEL", DEFAULT_RELEASE_NOTES_MODEL),
        help="GitHub Models model id used for generated release-note highlights",
    )
    notes.add_argument("--ai-timeout", type=positive_int, default=45)

    return parser.parse_args()


def git(*args: str, check: bool = True) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=ROOT,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        check=False,
    )
    if check and result.returncode != 0:
        message = result.stderr.strip() or result.stdout.strip() or "git command failed"
        raise RuntimeError(message)
    return result.stdout.strip()


def resolve_tag_commit(tag_name: str) -> str:
    result = subprocess.run(
        ["git", "rev-parse", f"refs/tags/{tag_name}^{{commit}}"],
        cwd=ROOT,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        return ""
    return result.stdout.strip()


def latest_tag(pattern: str) -> str:
    output = git("tag", "--list", pattern, "--sort=-creatordate", check=False)
    tags = [line.strip() for line in output.splitlines() if line.strip()]
    return tags[0] if tags else ""


def latest_stable_tag(tag_prefix: str) -> str:
    pattern = f"{tag_prefix}[0-9]*"
    output = git("tag", "--list", pattern, "--sort=-version:refname", check=False)
    tags = [line.strip() for line in output.splitlines() if line.strip()]
    return tags[0] if tags else ""


def manual_release_tag_pattern(base_version: str) -> str:
    return f"{base_version}.*-????????-*"


def commit_count_since(stable_tag: str, head_commit: str) -> int:
    range_spec = head_commit if not stable_tag else f"{stable_tag}..{head_commit}"
    count = int(git("rev-list", "--count", range_spec) or "0")
    return max(1, count)


def manual_release_context(
    *,
    build_date: str | None = None,
    head_commit: str | None = None,
) -> dict[str, object]:
    meta = base_metadata()
    head_sha = (head_commit or git("rev-parse", "HEAD")).strip()
    iso_date, _ = normalize_date(build_date)
    latest_release = latest_tag(manual_release_tag_pattern(str(meta["base_version"])))
    previous_release_commit = resolve_tag_commit(latest_release) if latest_release else ""
    stable_tag = latest_stable_tag(str(meta["tag_prefix"]))
    build_number = commit_count_since(stable_tag, head_sha)
    version_string = compose_version_string(
        int(meta["version_major"]),
        int(meta["version_minor"]),
        int(meta["version_patch"]),
        build_number,
    )

    return {
        "project_name": str(meta["project_name"]),
        "base_version": str(meta["base_version"]),
        "version_string": version_string,
        "build_number": build_number,
        "build_date": iso_date,
        "head_commit": head_sha,
        "head_commit_short": normalize_commit(head_sha),
        "latest_release_tag": latest_release,
        "stable_tag": stable_tag,
        "previous_release_commit": previous_release_commit,
    }


def print_mapping(data: dict[str, object]) -> None:
    for key, value in data.items():
        if isinstance(value, bool):
            rendered = str(value).lower()
        else:
            rendered = str(value)
        print(f"{key}={rendered}")


def stamp_version(header: Path, build_number: int) -> dict[str, object]:
    meta = base_metadata(header)
    version_string = compose_version_string(
        int(meta["version_major"]),
        int(meta["version_minor"]),
        int(meta["version_patch"]),
        build_number,
    )
    windows_version = compose_windows_version(
        int(meta["version_major"]),
        int(meta["version_minor"]),
        int(meta["version_patch"]),
        build_number,
    )

    replacements = {
        "FNQL_VERSION_TWEAK": str(build_number),
        "FNQL_VERSION_STRING": f'"{version_string}"',
        "FNQL_WINDOWS_FILE_VERSION": windows_version,
        "FNQL_WINDOWS_PRODUCT_VERSION": windows_version,
    }

    lines = header.read_text(encoding="utf-8").splitlines()
    seen: set[str] = set()
    rewritten: list[str] = []

    for line in lines:
        parts = line.strip().split(maxsplit=2)
        if len(parts) >= 3 and parts[0] == "#define" and parts[1] in replacements:
            key = parts[1]
            rewritten.append(f"#define {key} {replacements[key]}")
            seen.add(key)
        else:
            rewritten.append(line)

    missing = sorted(set(replacements) - seen)
    if missing:
        raise KeyError(f"Missing version defines in {header}: {', '.join(missing)}")

    write_text_lf(header, "\n".join(rewritten) + "\n")
    return {
        "version_string": version_string,
        "build_number": build_number,
    }


def release_range_spec(from_commit: str | None, to_commit: str) -> str:
    if from_commit and from_commit != to_commit:
        return f"{from_commit}..{to_commit}"
    if from_commit == to_commit:
        return f"{to_commit}^!"

    stable_tag = latest_stable_tag(str(base_metadata()["tag_prefix"]))
    return f"{stable_tag}..{to_commit}" if stable_tag else to_commit


def release_diff_spec(from_commit: str | None, to_commit: str) -> str:
    if from_commit and from_commit != to_commit:
        return f"{from_commit}..{to_commit}"
    if from_commit == to_commit:
        return f"{to_commit}^!"

    stable_tag = latest_stable_tag(str(base_metadata()["tag_prefix"]))
    if stable_tag:
        return f"{stable_tag}..{to_commit}"

    parent = git("rev-parse", f"{to_commit}^", check=False)
    return f"{parent}..{to_commit}" if parent else f"{to_commit}^!"


def commit_entries(from_commit: str | None, to_commit: str) -> list[dict[str, str]]:
    range_spec = release_range_spec(from_commit, to_commit)
    pretty = f"%H%x1f%h%x1f%an%x1f%ad%x1f%s%x1f%b%x1e"
    result = subprocess.run(
        ["git", "log", "--reverse", "--date=short", f"--pretty=format:{pretty}", range_spec],
        cwd=ROOT,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        message = result.stderr.strip() or result.stdout.strip() or "git log failed"
        raise RuntimeError(message)
    log = result.stdout
    entries: list[dict[str, str]] = []

    for record in log.split(RECORD_SEPARATOR):
        record = record.strip("\n")
        if not record:
            continue
        fields = record.split(FIELD_SEPARATOR, 5)
        if len(fields) != 6:
            continue
        commit_hash, short_hash, author, date, subject, body = fields
        entries.append(
            {
                "hash": commit_hash.strip(),
                "short": short_hash.strip(),
                "author": author.strip(),
                "date": date.strip(),
                "subject": subject.strip(),
                "body": body.strip(),
            }
        )

    return entries


def commit_lines(from_commit: str | None, to_commit: str) -> list[str]:
    return [f"- {entry['short']} {entry['subject']}" for entry in commit_entries(from_commit, to_commit)]


def truncate_context(value: str, max_chars: int = MAX_RELEASE_NOTES_CONTEXT_CHARS) -> str:
    if len(value) <= max_chars:
        return value
    return value[:max_chars].rstrip() + "\n...[truncated for release-note generation]\n"


def meaningful_changelog_section(changelog: Path) -> str:
    try:
        text = clean_section_text(changelog, "Unreleased").strip()
    except Exception:
        return ""

    has_real_bullet = False
    for line in text.splitlines():
        stripped = line.strip().lower()
        if stripped.startswith("- ") and "none yet" not in stripped and "no documented changes" not in stripped:
            has_real_bullet = True
            break
    return text if has_real_bullet else ""


def changed_file_summary(from_commit: str | None, to_commit: str) -> tuple[str, str]:
    range_spec = release_diff_spec(from_commit, to_commit)
    stat = git("diff", "--stat", "--find-renames", range_spec, check=False)
    names = git("diff", "--name-status", "--find-renames", range_spec, check=False)
    return stat.strip(), names.strip()


def release_note_generation_context(
    *,
    build_number: int,
    build_date: str,
    version_string: str,
    from_commit: str | None,
    target_commit: str,
    changelog: Path,
) -> str:
    commits = commit_entries(from_commit, target_commit)
    stat, names = changed_file_summary(from_commit, target_commit)
    manual_changelog = meaningful_changelog_section(changelog)

    commit_lines_for_prompt: list[str] = []
    for entry in commits:
        commit_lines_for_prompt.append(
            f"- {entry['short']} {entry['date']} {entry['subject']} (author: {entry['author']})"
        )
        if entry["body"]:
            body = "\n".join(f"  {line}" for line in entry["body"].splitlines()[:8] if line.strip())
            if body:
                commit_lines_for_prompt.append(body)

    lines = [
        f"Project: FnQL, a modernized Quake Live engine branch derived from FnQ3.",
        f"Release version: {version_string}",
        f"Build number: {build_number}",
        f"Build date: {build_date}",
        f"Target commit: {target_commit}",
        f"Previous release commit: {from_commit or 'none'}",
        "",
        "Project priorities:",
        "- Preserve retail Quake Live Steam compatibility.",
        "- Keep game-code reconstruction out of scope for FnQL releases.",
        "- Keep hot paths and tooling fast.",
        "- Improve modern platform support without regressing existing targets.",
        "- Keep cross-platform viability visible.",
        "",
    ]

    if manual_changelog:
        lines.extend(["Manual Unreleased changelog context:", manual_changelog, ""])

    lines.extend(
        [
            "Commits in range:",
            "\n".join(commit_lines_for_prompt) or "- No commits found.",
            "",
            "Diff stat:",
            stat or "(empty)",
            "",
            "Changed files:",
            names or "(empty)",
        ]
    )
    return truncate_context("\n".join(lines))


def sanitize_generated_notes(text: str) -> str:
    stripped = text.strip()
    fence = re.fullmatch(r"```(?:markdown|md)?\s*(.*?)\s*```", stripped, flags=re.IGNORECASE | re.DOTALL)
    if fence:
        stripped = fence.group(1).strip()

    sanitized: list[str] = []
    for line in stripped.splitlines():
        clean = line.rstrip()
        if re.match(r"^#{1,2}\s+(fnql|release notes|changelog highlights|what'?s changed)\b", clean, flags=re.IGNORECASE):
            continue
        if clean.startswith("::"):
            clean = "\\" + clean
        sanitized.append(clean)

    return "\n".join(sanitized).strip()


def github_models_choice_content(data: object) -> str:
    if not isinstance(data, dict):
        return ""
    choices = data.get("choices")
    if not isinstance(choices, list) or not choices:
        return ""
    first = choices[0]
    if not isinstance(first, dict):
        return ""
    message = first.get("message")
    if not isinstance(message, dict):
        return ""
    content = message.get("content")
    return content if isinstance(content, str) else ""


def github_models_highlights(context: str, *, model: str, timeout: int) -> str:
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("FNQL_GITHUB_TOKEN")
    if not token:
        return ""

    system_prompt = textwrap.dedent(
        """
        You write user-facing release notes for FnQL.
        Audience: players, server operators, mod users, and testers.
        Produce concise Markdown with only useful highlights. Avoid duplicated ideas,
        merge noise, raw commit lists, tiny refactor details, and maintainer-only trivia.
        Mention compatibility, renderer, audio, platform, and packaging impact when relevant.
        Use third-level headings such as "### Highlights", "### Rendering and Display",
        "### Audio", "### Builds and Packaging", and "### Fixes"; omit empty headings.
        Keep the whole answer under 12 bullets. Do not invent changes.
        """
    ).strip()

    payload = {
        "model": model,
        "temperature": 0.2,
        "max_tokens": 900,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": context},
        ],
    }
    request = urllib.request.Request(
        GITHUB_MODELS_ENDPOINT,
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
            "X-GitHub-Api-Version": "2022-11-28",
        },
        method="POST",
    )

    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            data = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"GitHub Models request failed with HTTP {exc.code}: {detail}") from exc
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, OSError) as exc:
        raise RuntimeError(f"GitHub Models request failed: {exc}") from exc

    return sanitize_generated_notes(github_models_choice_content(data))


def normalized_subject_key(subject: str) -> str:
    return re.sub(r"[^a-z0-9]+", " ", subject.lower()).strip()


def is_merge_subject(subject: str) -> bool:
    return subject.lower().startswith(("merge branch ", "merge pull request ", "merge remote-tracking "))


def fallback_section_for_subject(subject: str) -> str:
    lowered = subject.lower()
    if re.search(r"\b(fix|fixed|repair|crash|regression|stability|bug)\b", lowered):
        return "Fixes"
    if any(token in lowered for token in ("glx", "renderer", "opengl", "vulkan", "bloom", "display", "screenshot", "cubemap", "wal")):
        return "Rendering and Display"
    if any(token in lowered for token in ("audio", "sound", "openal", "wasapi", "ogg", "vorbis")):
        return "Audio"
    if any(token in lowered for token in ("release", "package", "artifact", "workflow", "build", "meson", "ninja", "makefile", "msvc")):
        return "Builds and Packaging"
    return "Highlights"


def humanize_subject(subject: str) -> str:
    cleaned = re.sub(r"\s+\(#\d+\)$", "", subject.strip())
    if not cleaned:
        return cleaned
    cleaned = cleaned[0].upper() + cleaned[1:]
    if cleaned[-1] not in ".!?":
        cleaned += "."
    return cleaned


def fallback_highlights(commits: list[dict[str, str]], changelog: Path) -> str:
    manual = meaningful_changelog_section(changelog)
    if manual:
        return manual

    sections: dict[str, list[str]] = {}
    seen: set[str] = set()

    for entry in commits:
        subject = entry["subject"]
        if not subject or is_merge_subject(subject):
            continue
        lowered = subject.lower()
        if lowered.startswith(("docs:", "doc:")) or "reset unreleased changelog" in lowered:
            continue
        key = normalized_subject_key(subject)
        if key in seen:
            continue
        seen.add(key)
        section = fallback_section_for_subject(subject)
        sections.setdefault(section, []).append(f"- {humanize_subject(subject)}")

    if not sections:
        return "### Highlights\n- No user-facing changes were detected in this release range."

    preferred_order = ["Highlights", "Rendering and Display", "Audio", "Builds and Packaging", "Fixes"]
    rendered: list[str] = []
    for section in preferred_order:
        bullets = sections.get(section, [])
        if not bullets:
            continue
        rendered.extend([f"### {section}", *bullets[:6], ""])

    return "\n".join(rendered).strip()


def generated_highlights(
    *,
    build_number: int,
    build_date: str,
    version_string: str,
    from_commit: str | None,
    target_commit: str,
    changelog: Path,
    no_ai: bool,
    notes_model: str,
    ai_timeout: int,
    highlights_file: Path | None = None,
) -> str:
    if highlights_file is not None:
        if not highlights_file.exists():
            raise FileNotFoundError(f"release highlights file does not exist: {highlights_file}")
        highlights = sanitize_generated_notes(highlights_file.read_text(encoding="utf-8"))
        if not highlights:
            raise ValueError(f"release highlights file is empty after sanitization: {highlights_file}")
        return highlights

    commits = commit_entries(from_commit, target_commit)
    if not no_ai:
        context = release_note_generation_context(
            build_number=build_number,
            build_date=build_date,
            version_string=version_string,
            from_commit=from_commit,
            target_commit=target_commit,
            changelog=changelog,
        )
        try:
            generated = github_models_highlights(context, model=notes_model, timeout=ai_timeout)
            if generated:
                return generated
        except Exception as exc:
            print(f"warning: AI release-note generation unavailable: {exc}", file=sys.stderr)

    return fallback_highlights(commits, changelog)


def render_release_notes(
    *,
    build_number: int,
    build_date: str | None = None,
    from_commit: str | None = None,
    to_commit: str | None = None,
    changelog: Path = DEFAULT_CHANGELOG,
    no_ai: bool = False,
    notes_model: str = DEFAULT_RELEASE_NOTES_MODEL,
    ai_timeout: int = 45,
    highlights_file: Path | None = None,
) -> str:
    meta = base_metadata()
    target_commit = (to_commit or git("rev-parse", "HEAD")).strip()
    iso_date, _ = normalize_date(build_date)
    version_string = compose_version_string(
        int(meta["version_major"]),
        int(meta["version_minor"]),
        int(meta["version_patch"]),
        build_number,
    )

    highlights = generated_highlights(
        build_number=build_number,
        build_date=iso_date,
        version_string=version_string,
        from_commit=from_commit,
        target_commit=target_commit,
        changelog=changelog,
        no_ai=no_ai,
        notes_model=notes_model,
        ai_timeout=ai_timeout,
        highlights_file=highlights_file,
    )

    previous_line: str
    if from_commit:
        previous_line = f"- Previous release commit: {normalize_commit(from_commit)} ({from_commit})"
    else:
        stable_tag = latest_stable_tag(str(meta["tag_prefix"]))
        if stable_tag:
            previous_line = f"- Previous stable tag: {stable_tag}"
        else:
            previous_line = "- Previous release commit: none"

    lines = [
        f"# {meta['project_name']} Release",
        "",
        "## Changelog highlights",
        "",
        highlights,
        "",
        "## Build details",
        "",
        "- Channel: manual",
        f"- Base version line: {meta['base_version']}",
        f"- Build version: {version_string}",
        f"- Build date: {iso_date}",
        f"- Commit: {normalize_commit(target_commit)} ({target_commit})",
        previous_line,
    ]

    commits = commit_lines(from_commit, target_commit)
    lines.extend(["", "<details>", "<summary>Included commits</summary>", ""])
    if commits:
        lines.extend(commits)
    else:
        lines.append(f"- {normalize_commit(target_commit)} no new commits were found for the requested range")
    lines.extend(["", "</details>"])

    return "\n".join(lines) + "\n"


def main() -> int:
    args = parse_args()

    if args.command in {"summary", "github-output"}:
        info = manual_release_context(
            build_date=args.build_date,
            head_commit=args.head_commit,
        )
        print_mapping(info)
        return 0

    if args.command == "stamp-version":
        info = stamp_version(args.header, args.build_number)
        print_mapping(info)
        return 0

    notes = render_release_notes(
        build_number=args.build_number,
        build_date=args.build_date,
        from_commit=args.from_commit,
        to_commit=args.to_commit,
        changelog=args.changelog,
        no_ai=args.no_ai,
        notes_model=args.notes_model,
        ai_timeout=args.ai_timeout,
        highlights_file=args.highlights_file,
    )
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        write_text_lf(args.output, notes)
    else:
        sys.stdout.write(notes)
    if args.clear_changelog:
        clear_unreleased(args.changelog)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
