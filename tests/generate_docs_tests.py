from __future__ import annotations

import re
import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts import generate_docs
from scripts.fnql_meta import base_metadata


FEATURE_HEADINGS = (
    "Extensive Quake Live compatibility",
    "Fast, modern hardware rendering",
    "Modern visual upgrades",
    "Improved player visibility",
    "Better modern hardware and OS support",
    "Low-latency, configurable controls",
    "Modern spatial audio",
    "Major quality-of-life improvements",
    "Better screenshots, video and streaming tools",
    "Faster content handling",
    "Greater stability and security",
)

USER_GUIDES = (
    "docs/DISPLAY.md",
    "docs/RTX.md",
    "docs/VISUALS.md",
    "docs/ASPECT_CORRECTION.md",
    "docs/AUDIO.md",
    "docs/CONSOLE.md",
    "docs/SCREENSHOTS.md",
)

LINKED_USER_DOCS = (
    "README.md",
    "BUILD.md",
    "CREDITS.md",
    "docs/fnql.md",
    *USER_GUIDES,
    "docs/GLX.md",
)


class GenerateDocsTests(unittest.TestCase):
    def test_render_reports_missing_template_keys_with_template_path(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            template = Path(tmp) / "README.md.in"
            template.write_text("FnQL $version $missing_key\n", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "missing_key"):
                generate_docs.render(template, {"version": "0.1.0"})

    def test_generated_landing_docs_match_their_canonical_templates(self) -> None:
        meta = base_metadata()
        context = {
            **meta,
            "release_tag_example": f"{meta['tag_prefix']}{meta['version']}",
        }

        self.assertEqual(
            generate_docs.render(generate_docs.README_TEMPLATE, context),
            generate_docs.README_OUTPUT.read_text(encoding="utf-8"),
        )
        self.assertEqual(
            generate_docs.render(generate_docs.INSTALL_TEMPLATE, context),
            generate_docs.INSTALL_OUTPUT.read_text(encoding="utf-8"),
        )

    def test_landing_docs_use_the_player_facing_fnq3_structure(self) -> None:
        documents = (
            generate_docs.README_TEMPLATE.read_text(encoding="utf-8"),
            generate_docs.README_OUTPUT.read_text(encoding="utf-8"),
            generate_docs.INSTALL_TEMPLATE.read_text(encoding="utf-8"),
            generate_docs.INSTALL_OUTPUT.read_text(encoding="utf-8"),
        )

        for document in documents:
            for heading in FEATURE_HEADINGS:
                self.assertIn(heading, document)
            self.assertIn("Getting Started", document)
            self.assertIn("Player guides", document)
            self.assertNotIn("initial migration stage", document)
            self.assertNotIn("Inherited feature guides", document)

    def test_documented_player_guides_exist_and_local_links_resolve(self) -> None:
        for relative in USER_GUIDES:
            guide = ROOT / relative
            self.assertTrue(guide.is_file(), relative)
            self.assertTrue(guide.read_text(encoding="utf-8").startswith("# "))

        for relative in LINKED_USER_DOCS:
            document = ROOT / relative
            text = document.read_text(encoding="utf-8")
            for link in re.findall(r"\[[^]]+\]\(([^)]+)\)", text):
                if link.startswith(("http://", "https://", "#")):
                    continue
                target = link.split("#", 1)[0]
                self.assertTrue(
                    (document.parent / target).resolve().is_file(),
                    f"{relative}: broken local link {link}",
                )


if __name__ == "__main__":
    unittest.main()
