from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts import changelog


class ChangelogTests(unittest.TestCase):
    def test_clean_section_dedupes_and_sorts_legacy_headings(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "CHANGELOG.md"
            path.write_text(
                "\n".join(
                    [
                        "# Changelog",
                        "",
                        "## [Unreleased]",
                        "",
                        "### Added",
                        "- Added `r_picmipFilter` to control texture picmip.",
                        "- Added `r_picmipFilter` to control texture picmip.",
                        "",
                        "### Changed",
                        "- Release archives no longer include debug build products.",
                        "",
                        "### Fixed",
                        "- Fixed a crash when loading a renderer.",
                    ]
                )
                + "\n",
                encoding="utf-8",
            )

            cleaned = changelog.clean_section_text(path, "Unreleased")

        self.assertIn("### Rendering and Display", cleaned)
        self.assertIn("- Added `r_picmipFilter` to control texture picmip.", cleaned)
        self.assertEqual(cleaned.count("r_picmipFilter"), 1)
        self.assertIn("### Builds and Packaging", cleaned)
        self.assertIn("### Fixes", cleaned)

    def test_clear_unreleased_resets_to_empty_template(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "CHANGELOG.md"
            path.write_text(
                "\n".join(
                    [
                        "# Changelog",
                        "",
                        "## [Unreleased]",
                        "",
                        "### Highlights",
                        "- Something ready for release.",
                    ]
                )
                + "\n",
                encoding="utf-8",
            )

            changelog.clear_unreleased(path)
            cleared = path.read_text(encoding="utf-8")

        self.assertNotIn("Something ready for release", cleared)
        for category in changelog.CHANGELOG_CATEGORIES:
            self.assertIn(f"### {category}", cleared)
        self.assertEqual(cleared.count("- _None yet._"), len(changelog.CHANGELOG_CATEGORIES))

    def test_prepare_release_rejects_unsafe_version_label(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "CHANGELOG.md"
            path.write_text(
                "# Changelog\n\n## [Unreleased]\n\n- Ready.\n",
                encoding="utf-8",
            )

            with self.assertRaisesRegex(ValueError, "single safe label"):
                changelog.prepare_release(path, "0.1.0\n## [Injected]", "2026-06-21")

    def test_prepare_release_rejects_invalid_date(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "CHANGELOG.md"
            path.write_text(
                "# Changelog\n\n## [Unreleased]\n\n- Ready.\n",
                encoding="utf-8",
            )

            with self.assertRaisesRegex(ValueError, "YYYY-MM-DD"):
                changelog.prepare_release(path, "0.1.0", "2026-99-99")

    def test_prepare_release_rejects_duplicate_version_with_different_date(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "CHANGELOG.md"
            path.write_text(
                "\n".join(
                    [
                        "# Changelog",
                        "",
                        "## [Unreleased]",
                        "",
                        "- Ready.",
                        "",
                        "## [0.1.0] - 2026-06-20",
                        "",
                        "- Existing release.",
                    ]
                )
                + "\n",
                encoding="utf-8",
            )

            with self.assertRaisesRegex(ValueError, "already exists"):
                changelog.prepare_release(path, "0.1.0", "2026-06-21")


if __name__ == "__main__":
    unittest.main()
