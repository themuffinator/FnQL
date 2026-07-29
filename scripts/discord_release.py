from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from fnql_meta import ROOT, base_metadata, normalize_date


DEFAULT_MANIFEST = ROOT / ".install" / "release-manifest.json"
DEFAULT_NOTES = ROOT / ".tmp" / "release-notes.md"
DEFAULT_REPOSITORY = "themuffinator/FnQL"
DEFAULT_USERNAME = "FnQL Releases"
DEFAULT_AVATAR_URL = (
    "https://raw.githubusercontent.com/themuffinator/FnQL/main/docs/assets/fnql-discord-avatar.png"
)
DEFAULT_RELEASE_EMOJI = "<:quakelive:1390987057454776481>"
DEFAULT_ROLE_MENTIONS = "<@&1424165541572120647> <@&1390287267276525628>"
DEFAULT_FEEDBACK_CHANNEL = "<#1528370439997362216>"
DEFAULT_EMBED_COLOR = 0xE60000  # brilliant red
WEBHOOK_ENV_VARS = ("FNQL_DISCORD_WEBHOOK_URL", "DISCORD_RELEASE_WEBHOOK")
ALLOWED_WEBHOOK_HOSTS = frozenset(
    {
        "discord.com",
        "www.discord.com",
        "canary.discord.com",
        "ptb.discord.com",
        "discordapp.com",
        "discord.gg",
    }
)

MAX_CONTENT_CHARS = 2000
MAX_DESCRIPTION_CHARS = 2200
MAX_FIELD_VALUE_CHARS = 1000
MAX_INTRO_CHARS = 420
MIN_HIGHLIGHTS_CHARS = 200

POST_ATTEMPTS = 3
RETRY_STATUSES = frozenset({429, 500, 502, 503, 504})
RETRY_BACKOFF_SECONDS = 2.0

ARTIFACT_LABELS = {
    "windows-msvc-x86": "Windows x86 (MSVC)",
    "windows-mingw-x86": "Windows x86 (MinGW)",
    "linux-x86": "Linux x86",
}
ARTIFACT_ORDER = ("windows-msvc-x86", "windows-mingw-x86", "linux-x86")


def positive_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc
    if parsed < 1:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return parsed


def parse_color(value: str | int | None) -> int:
    if value is None or value == "":
        return DEFAULT_EMBED_COLOR
    if isinstance(value, int):
        color = value
    else:
        text = str(value).strip()
        hexadecimal = text.startswith("#") or text.lower().startswith("0x")
        digits = text.lstrip("#")
        if not hexadecimal and not re.fullmatch(r"\d+", digits):
            hexadecimal = True
        try:
            color = int(digits, 16 if hexadecimal else 10)
        except ValueError as exc:
            raise ValueError(f"Invalid embed color: {value}") from exc
    if not 0 <= color <= 0xFFFFFF:
        raise ValueError(f"Embed color must be between 0x000000 and 0xFFFFFF: {value}")
    return color


def validated_webhook_url(url: str) -> str:
    cleaned = url.strip()
    parsed = urllib.parse.urlsplit(cleaned)
    if parsed.scheme != "https":
        raise ValueError("Discord webhook URL must use https")
    if parsed.hostname is None or parsed.hostname.lower() not in ALLOWED_WEBHOOK_HOSTS:
        raise ValueError("Discord webhook URL must point at an official Discord host")
    if not parsed.path.startswith("/api/webhooks/"):
        raise ValueError("Discord webhook URL must be an /api/webhooks/ endpoint")
    return cleaned


def resolved_webhook_url(explicit: str | None) -> str:
    if explicit:
        return explicit
    for name in WEBHOOK_ENV_VARS:
        value = os.environ.get(name, "").strip()
        if value:
            return value
    return ""


def wait_for_delivery(url: str) -> str:
    parsed = urllib.parse.urlsplit(url)
    query = dict(urllib.parse.parse_qsl(parsed.query, keep_blank_values=True))
    query["wait"] = "true"
    return urllib.parse.urlunsplit(parsed._replace(query=urllib.parse.urlencode(query)))


def read_manifest(path: Path | None) -> dict[str, object]:
    if path is None or not path.exists():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as exc:
        raise RuntimeError(f"Unable to read release manifest {path}: {exc}") from exc
    return data if isinstance(data, dict) else {}


def read_notes(path: Path | None) -> str:
    if path is None or not path.exists():
        return ""
    return path.read_text(encoding="utf-8").replace("\r\n", "\n").replace("\r", "\n").strip()


def limit_text(text: str, max_chars: int) -> str:
    if max_chars <= 0:
        return ""
    if len(text) <= max_chars:
        return text
    if max_chars <= 3:
        return text[:max_chars]
    return f"{text[: max_chars - 3].rstrip()}..."


def mentioned_role_ids(mentions: str) -> list[str]:
    # Only these explicit role mentions may notify. Generated release notes and
    # arbitrary text must remain unable to add an unexpected notification.
    return list(dict.fromkeys(re.findall(r"<@&(\d{17,20})>", mentions or "")))


def join_within_limit(lines: list[str], max_chars: int) -> str:
    # Masked links must never be cut in half, so drop whole entries instead of
    # truncating the joined text.
    kept: list[str] = []
    length = 0
    for line in lines:
        cost = len(line) + (1 if kept else 0)
        if length + cost > max_chars:
            continue
        kept.append(line)
        length += cost
    return "\n".join(kept)


def extract_highlights(notes: str) -> str:
    lines = notes.split("\n")
    start = next(
        (
            index
            for index, line in enumerate(lines)
            if re.fullmatch(r"##\s+(?:Changelog )?highlights\s*", line.strip(), flags=re.IGNORECASE)
        ),
        -1,
    )

    if start >= 0:
        section: list[str] = []
        for line in lines[start + 1 :]:
            if re.match(r"^##\s+\S", line):
                break
            section.append(line)
    else:
        # Hand-written or externally generated notes: drop the document title,
        # the build-details section, and any collapsed commit list.
        section = []
        skipping = False
        for line in lines:
            if re.match(r"^#\s+\S", line):
                continue
            if re.match(r"^##\s+Build details\s*$", line, flags=re.IGNORECASE):
                skipping = True
                continue
            if re.match(r"^##\s+\S", line):
                skipping = False
            if line.strip().startswith("<details"):
                skipping = True
            if line.strip().startswith("</details>"):
                skipping = False
                continue
            if not skipping:
                section.append(line)

    return re.sub(r"\n{3,}", "\n\n", "\n".join(section)).strip()


def format_highlights(highlights: str) -> str:
    # Release notes may retain categorized H3 headings for GitHub readers. The
    # Discord announcement mirrors openQ4's compact single Highlights block.
    lines = [line for line in highlights.splitlines() if not re.fullmatch(r"###\s+.+", line.strip())]
    return re.sub(r"\n{3,}", "\n\n", "\n".join(lines)).strip()


def build_intro(project_name: str, version_label: str, highlights: str) -> str:
    lowered = highlights.lower()
    if any(token in lowered for token in ("renderer", "glx", "vulkan", "rtx", "shader", "display")):
        subject = "renderer and display work that needs real gameplay miles against retail Quake Live"
    elif any(token in lowered for token in ("steam", "protocol", "server", "demo", "compatib")):
        subject = "retail Quake Live compatibility work that needs testing against real Steam installs and live servers"
    elif any(token in lowered for token in ("fix", "crash", "regression", "stability")):
        subject = "stability work that should be tested in longer sessions"
    else:
        return limit_text(
            f"The new {project_name} release is ready. Install it beside a legitimate retail Quake Live Steam "
            "installation and send back anything that looks off.",
            MAX_INTRO_CHARS,
        )

    return limit_text(f"The new {project_name} release is ready, with {subject}.", MAX_INTRO_CHARS)


def release_url(repository: str, release_tag: str) -> str:
    return f"https://github.com/{repository}/releases/tag/{urllib.parse.quote(release_tag)}"


def asset_url(repository: str, release_tag: str, asset_name: str) -> str:
    return (
        f"https://github.com/{repository}/releases/download/"
        f"{urllib.parse.quote(release_tag)}/{urllib.parse.quote(asset_name)}"
    )


def artifact_sort_key(entry: dict[str, object]) -> tuple[int, str]:
    artifact_dir = str(entry.get("artifact_dir", ""))
    try:
        return (ARTIFACT_ORDER.index(artifact_dir), artifact_dir)
    except ValueError:
        return (len(ARTIFACT_ORDER), artifact_dir)


def build_download_links(repository: str, release_tag: str, archives: list[dict[str, object]]) -> list[str]:
    links: list[str] = []
    for entry in sorted(archives, key=artifact_sort_key):
        archive = str(entry.get("archive", "")).strip()
        if not archive:
            continue
        artifact_dir = str(entry.get("artifact_dir", "")).strip()
        label = ARTIFACT_LABELS.get(artifact_dir, artifact_dir or archive)
        links.append(f"[{label}]({asset_url(repository, release_tag, archive)})")

    links.append(
        f"[Source Code](https://github.com/{repository}/archive/refs/tags/"
        f"{urllib.parse.quote(release_tag)}.zip)"
    )
    return links


def build_payload(
    *,
    repository: str,
    release_tag: str,
    release_title: str,
    version_label: str,
    build_date: str,
    commit: str,
    archives: list[dict[str, object]],
    notes: str,
    html_url: str = "",
    username: str = DEFAULT_USERNAME,
    avatar_url: str = DEFAULT_AVATAR_URL,
    emoji: str = DEFAULT_RELEASE_EMOJI,
    color: int = DEFAULT_EMBED_COLOR,
    role_mentions: str = DEFAULT_ROLE_MENTIONS,
    feedback_channel: str = DEFAULT_FEEDBACK_CHANNEL,
) -> dict[str, object]:
    meta = base_metadata()
    project_name = str(meta["project_name"])
    url = html_url or release_url(repository, release_tag)

    highlights = format_highlights(extract_highlights(notes))
    intro = build_intro(project_name, version_label, highlights)
    details_link = f"[Full release notes and downloads]({url})"
    feedback = f"Feedback is welcome in {feedback_channel}." if feedback_channel else ""

    reserved = len(intro) + len(feedback) + len(details_link) + 8
    highlights_section = (
        limit_text(
            f"## Highlights\n\n{highlights}",
            max(MIN_HIGHLIGHTS_CHARS, MAX_DESCRIPTION_CHARS - reserved),
        )
        if highlights
        else "Release notes and downloads are available on GitHub."
    )
    description = limit_text(
        "\n\n".join(part for part in (intro, feedback, highlights_section, details_link) if part),
        MAX_DESCRIPTION_CHARS,
    )

    download_links = build_download_links(repository, release_tag, archives)
    downloads = join_within_limit(download_links, MAX_FIELD_VALUE_CHARS) or f"[Open release]({url})"

    headline = f"{project_name} {version_label} release published!"
    content = limit_text(
        " ".join(part for part in (emoji.strip(), headline, role_mentions.strip()) if part),
        MAX_CONTENT_CHARS,
    )

    embed: dict[str, object] = {
        "title": limit_text(f"{project_name} {version_label}", 256),
        "url": url,
        "description": description,
        "color": color,
        "fields": [
            {"name": "Version", "value": limit_text(version_label, MAX_FIELD_VALUE_CHARS), "inline": True},
            {"name": "State", "value": "Stable release", "inline": True},
            {"name": "Downloads", "value": downloads},
        ],
        "footer": {"text": f"{meta['display_name']} | Target: {meta['compatibility_target']}"},
    }

    timestamp = embed_timestamp(build_date)
    if timestamp:
        embed["timestamp"] = timestamp

    payload: dict[str, object] = {
        "username": username,
        "allowed_mentions": {"parse": [], "roles": mentioned_role_ids(role_mentions)},
        "content": content,
        "embeds": [embed],
    }
    if avatar_url:
        payload["avatar_url"] = avatar_url

    return payload


def embed_timestamp(build_date: str) -> str:
    if not build_date:
        return ""
    try:
        iso_date, _ = normalize_date(build_date)
    except ValueError:
        return ""
    return f"{iso_date}T00:00:00Z"


def payload_from_release_state(args: argparse.Namespace) -> dict[str, object]:
    manifest = read_manifest(args.manifest)
    archives = manifest.get("archives")
    archive_entries = [entry for entry in archives if isinstance(entry, dict)] if isinstance(archives, list) else []

    release_tag = args.release_tag or str(manifest.get("release_tag", "")).strip()
    if not release_tag:
        raise ValueError("A release tag is required: pass --release-tag or a release manifest")

    version_label = args.version or str(manifest.get("version", "")).strip() or release_tag
    release_title = args.release_title or str(manifest.get("release_title", "")).strip()
    build_date = args.build_date or str(manifest.get("build_date", "")).strip()
    commit = args.commit or str(manifest.get("commit", "")).strip()

    return build_payload(
        repository=args.repository,
        release_tag=release_tag,
        release_title=release_title,
        version_label=version_label,
        build_date=build_date,
        commit=commit,
        archives=archive_entries,
        notes=read_notes(args.notes_file),
        html_url=args.release_url,
        username=args.username,
        avatar_url=args.avatar_url,
        emoji=args.emoji,
        color=parse_color(args.color),
        role_mentions=args.mentions,
        feedback_channel=args.feedback_channel,
    )


def post_payload(
    webhook_url: str,
    payload: dict[str, object],
    *,
    timeout: int = 20,
    attempts: int = POST_ATTEMPTS,
    sleep=time.sleep,
) -> str:
    target = wait_for_delivery(validated_webhook_url(webhook_url))
    body = json.dumps(payload).encode("utf-8")

    for attempt in range(1, attempts + 1):
        request = urllib.request.Request(
            target,
            data=body,
            headers={
                "Content-Type": "application/json",
                "User-Agent": "FnQL-Release-Announcer/1.0 (+https://github.com/themuffinator/FnQL)",
            },
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                return response.read().decode("utf-8", errors="replace")
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace").strip()
            if exc.code in RETRY_STATUSES and attempt < attempts:
                sleep(retry_delay(exc, detail, attempt))
                continue
            raise RuntimeError(f"Discord webhook rejected the announcement: HTTP {exc.code}: {detail}") from None
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            reason = getattr(exc, "reason", exc)
            if attempt < attempts:
                sleep(RETRY_BACKOFF_SECONDS * attempt)
                continue
            raise RuntimeError(f"Discord webhook request failed: {reason}") from None

    raise RuntimeError("Discord webhook request failed after retries")


def retry_delay(exc: urllib.error.HTTPError, detail: str, attempt: int) -> float:
    header = exc.headers.get("Retry-After") if exc.headers else None
    for candidate in (header, retry_after_from_body(detail)):
        try:
            if candidate is not None:
                return max(0.0, min(30.0, float(candidate)))
        except (TypeError, ValueError):
            continue
    return RETRY_BACKOFF_SECONDS * attempt


def retry_after_from_body(detail: str) -> float | None:
    try:
        data = json.loads(detail)
    except (json.JSONDecodeError, TypeError):
        return None
    value = data.get("retry_after") if isinstance(data, dict) else None
    return float(value) if isinstance(value, (int, float)) else None


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Announce an FnQL release on Discord")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--notes-file", type=Path, default=DEFAULT_NOTES)
    parser.add_argument("--release-tag")
    parser.add_argument("--release-title")
    parser.add_argument("--version", dest="version")
    parser.add_argument("--build-date")
    parser.add_argument("--commit")
    parser.add_argument("--release-url", default="")
    parser.add_argument(
        "--repository",
        default=os.environ.get("GITHUB_REPOSITORY") or DEFAULT_REPOSITORY,
    )
    parser.add_argument("--webhook-url")
    parser.add_argument("--username", default=os.environ.get("FNQL_DISCORD_USERNAME") or DEFAULT_USERNAME)
    parser.add_argument("--avatar-url", default=os.environ.get("FNQL_DISCORD_AVATAR_URL") or DEFAULT_AVATAR_URL)
    parser.add_argument("--emoji", default=os.environ.get("FNQL_DISCORD_RELEASE_EMOJI") or DEFAULT_RELEASE_EMOJI)
    parser.add_argument("--mentions", default=os.environ.get("FNQL_DISCORD_RELEASE_MENTIONS") or DEFAULT_ROLE_MENTIONS)
    parser.add_argument("--color", default=os.environ.get("FNQL_DISCORD_RELEASE_COLOR") or DEFAULT_EMBED_COLOR)
    parser.add_argument(
        "--feedback-channel",
        default=os.environ.get("FNQL_DISCORD_FEEDBACK_CHANNEL") or DEFAULT_FEEDBACK_CHANNEL,
    )
    parser.add_argument("--timeout", type=positive_int, default=20)
    parser.add_argument("--attempts", type=positive_int, default=POST_ATTEMPTS)
    parser.add_argument("--output", type=Path, help="Write the webhook payload to this file")
    parser.add_argument("--dry-run", action="store_true", help="Build the payload without posting it")
    parser.add_argument(
        "--require-webhook",
        action="store_true",
        help="Fail instead of skipping when no webhook URL is configured",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)

    try:
        payload = payload_from_release_state(args)
    except (ValueError, RuntimeError) as exc:
        print(f"discord_release.py: {exc}", file=sys.stderr)
        return 2

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("w", encoding="utf-8", newline="\n") as handle:
            handle.write(json.dumps(payload, indent=2) + "\n")

    if args.dry_run:
        print(json.dumps(payload, indent=2))
        return 0

    webhook_url = resolved_webhook_url(args.webhook_url)
    if not webhook_url:
        message = (
            "No Discord webhook configured; set FNQL_DISCORD_WEBHOOK_URL or pass --webhook-url."
        )
        if args.require_webhook:
            print(f"discord_release.py: {message}", file=sys.stderr)
            return 2
        print(f"discord_release.py: {message} Skipping the release announcement.", file=sys.stderr)
        return 0

    try:
        post_payload(webhook_url, payload, timeout=args.timeout, attempts=args.attempts)
    except (ValueError, RuntimeError) as exc:
        print(f"discord_release.py: {exc}", file=sys.stderr)
        return 1

    print("Discord release announcement posted.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
