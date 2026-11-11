from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts import changelog
from scripts import discord_release
from scripts import manual_release


# Wrapped entries exactly as the Unreleased queue writes them. The 0.1.0.117
# and 0.1.0.134 releases published only the first line of each.
QUEUE = """# Changelog

## [Unreleased]

### Highlights
- Releases now include the clean-room
  [FnQL-game](https://github.com/themuffinator/FnQL-game) modules in
  `baseq3/`. Servers opt in with `+set vm_gameProvider fnql`.

### Compatibility
- Server snapshots now follow retail Quake Live's game-module visibility rules:
  Freeze Tag thaw ticks are hidden from opponents.
- The Steam server browser's player list shows bots as `(Bot) <name>`.

### Rendering and Display
- _None yet._

### Fixes
- Controllers work in the Windows client. Turn it on with
  `in_joystick 1`.

## [0.1.0] - 2026-01-01

- Older release entry.
"""

HIGHLIGHT_TEXT = (
    "Releases now include the clean-room "
    "[FnQL-game](https://github.com/themuffinator/FnQL-game) modules in "
    "`baseq3/`. Servers opt in with `+set vm_gameProvider fnql`."
)


def write_queue(root: Path, text: str = QUEUE) -> Path:
    path = root / "CHANGELOG.md"
    path.write_text(text, encoding="utf-8")
    return path


class ChangelogEntryTests(unittest.TestCase):
    def test_wrapped_entries_are_read_whole(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            items = changelog.section_items(write_queue(Path(tmp)), "Unreleased")

        self.assertEqual(
            items,
            [
                ("Highlights", HIGHLIGHT_TEXT),
                (
                    "Compatibility",
                    "Server snapshots now follow retail Quake Live's game-module visibility "
                    "rules: Freeze Tag thaw ticks are hidden from opponents.",
                ),
                ("Compatibility", "The Steam server browser's player list shows bots as `(Bot) <name>`."),
                ("Fixes", "Controllers work in the Windows client. Turn it on with `in_joystick 1`."),
            ],
        )

    def test_cleanup_keeps_entries_whole_and_rewraps_them(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = write_queue(Path(tmp))
            changelog.cleanup_section(path, "Unreleased")
            cleaned = path.read_text(encoding="utf-8")
            reread = changelog.section_items(path, "Unreleased")

        self.assertTrue(all(len(line) <= changelog.WRAP_WIDTH for line in cleaned.splitlines()))
        self.assertEqual(reread[0], ("Highlights", HIGHLIGHT_TEXT))
        self.assertIn("in_joystick 1", cleaned)
        self.assertIn("## [0.1.0] - 2026-01-01\n\n- Older release entry.", cleaned)

    def test_prepare_release_moves_whole_entries(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = write_queue(Path(tmp))
            changelog.prepare_release(path, "0.2.0", "2026-11-12")
            released = changelog.section_items(path, "0.2.0")
            pending = changelog.section_items(path, "Unreleased")

        self.assertEqual(released[0], ("Highlights", HIGHLIGHT_TEXT))
        self.assertEqual(len(released), 4)
        self.assertEqual(pending, [])


class ReleaseNotesBodyTests(unittest.TestCase):
    def test_queue_is_published_whole_and_categorized(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            body = manual_release.changelog_release_body(write_queue(Path(tmp)))

        self.assertTrue(body.startswith(f"## Highlights\n\n- {HIGHLIGHT_TEXT}\n\n## All changes"))
        self.assertIn("### Compatibility\n\n- Server snapshots now follow", body)
        self.assertIn("Freeze Tag thaw ticks are hidden from opponents.", body)
        self.assertIn("### Fixes\n\n- Controllers work in the Windows client. Turn it on with `in_joystick 1`.", body)
        self.assertNotIn("Rendering and Display", body)
        self.assertNotIn("None yet", body)
        self.assertNotIn("Older release entry", body)

    def test_discord_announces_only_the_highlights(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            body = manual_release.changelog_release_body(write_queue(Path(tmp)))

        highlights = discord_release.format_highlights(discord_release.extract_highlights(body))
        self.assertEqual(highlights, f"- {HIGHLIGHT_TEXT}")

    def test_queue_without_highlights_promotes_one_entry_per_category(self) -> None:
        queue = QUEUE.replace(
            "### Highlights\n- Releases now include the clean-room\n"
            "  [FnQL-game](https://github.com/themuffinator/FnQL-game) modules in\n"
            "  `baseq3/`. Servers opt in with `+set vm_gameProvider fnql`.\n",
            "### Highlights\n- _None yet._\n",
        )
        with tempfile.TemporaryDirectory() as tmp:
            body = manual_release.changelog_release_body(write_queue(Path(tmp), queue))

        highlights, rest = body.split("## All changes", 1)
        self.assertIn("- Server snapshots now follow", highlights)
        self.assertIn("- Controllers work in the Windows client.", highlights)
        self.assertNotIn("Server snapshots", rest)
        self.assertIn("### Compatibility\n\n- The Steam server browser", rest)
        self.assertNotIn("### Fixes", rest)

    def test_empty_queue_yields_no_body(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "CHANGELOG.md"
            path.write_text("# Changelog\n\n## [Unreleased]\n", encoding="utf-8")
            changelog.clear_unreleased(path)
            self.assertEqual(manual_release.changelog_release_body(path), "")
            self.assertEqual(manual_release.changelog_release_body(Path(tmp) / "missing.md"), "")


class ReleaseNotesRenderTests(unittest.TestCase):
    def render(self, changelog_path: Path, **kwargs: object) -> str:
        target = manual_release.git("rev-parse", "HEAD")
        return manual_release.render_release_notes(
            build_number=1,
            build_date="2026-06-21",
            from_commit=manual_release.git("rev-parse", f"{target}~1"),
            to_commit=target,
            changelog=changelog_path,
            no_ai=True,
            curated_notes_dir=changelog_path.parent / "no-curated-notes",
            **kwargs,
        )

    def test_rendered_notes_lead_with_the_queue_and_have_no_title(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            generated = root / "generated.md"
            generated.write_text("- Generated text that must not win.\n", encoding="utf-8")
            rendered = self.render(write_queue(root), highlights_file=generated)

        self.assertTrue(rendered.startswith("## Highlights\n\n- Releases now include"))
        self.assertNotIn("Generated text", rendered)
        self.assertNotRegex(rendered, r"(?m)^# ")
        self.assertIn("## Build details\n\n- Version: 0.1.0.1\n", rendered)
        self.assertIn("- FnQL-game modules: [", rendered)
        self.assertNotIn("- Channel: manual", rendered)

    def test_empty_queue_falls_back_to_generated_highlights(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / "CHANGELOG.md"
            path.write_text("# Changelog\n\n## [Unreleased]\n", encoding="utf-8")
            changelog.clear_unreleased(path)
            generated = root / "generated.md"
            generated.write_text("- Generated highlight.\n", encoding="utf-8")
            rendered = self.render(path, highlights_file=generated)

        self.assertTrue(rendered.startswith("## Highlights\n\n- Generated highlight."))

    def test_housekeeping_commits_are_left_out(self) -> None:
        def entry(subject: str, *, author: str = "Dev", parents: str = "1") -> dict[str, str]:
            return {"subject": subject, "author": author, "parents": parents, "short": "abc1234"}

        self.assertTrue(manual_release.is_release_housekeeping(entry("Reset pending changelog after release")))
        self.assertTrue(manual_release.is_release_housekeeping(entry("Anything", author="github-actions[bot]")))
        self.assertTrue(manual_release.is_release_housekeeping(entry("Merge topic work", parents="2")))
        self.assertTrue(manual_release.is_release_housekeeping(entry("Merge pull request #6 from x/y")))
        self.assertFalse(manual_release.is_release_housekeeping(entry("Fix the retail join path")))

        commits = [entry("Merge topic work", parents="2"), entry("fix the retail join path")]
        self.assertEqual(
            manual_release.fallback_highlights(commits, ROOT / "docs" / "fnql" / "CHANGELOG.md"),
            "- Fix the retail join path.",
        )


if __name__ == "__main__":
    unittest.main()
