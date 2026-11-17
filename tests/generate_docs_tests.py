from __future__ import annotations

import re
import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from html.parser import HTMLParser

from scripts import generate_docs

HTML_VOID_ELEMENTS = frozenset(
    ("area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr")
)
# SVG elements the inline logo writes as self-closing tags.
SVG_SELF_CLOSING = frozenset(
    ("stop", "circle", "path", "rect", "line", "polygon", "polyline", "ellipse", "use")
)


class _NestingChecker(HTMLParser):
    """Records tag-nesting errors and every href/src attribute."""

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.stack: list[str] = []
        self.errors: list[str] = []
        self.urls: list[tuple[str, str]] = []

    def handle_starttag(self, tag, attrs):
        for name, value in attrs:
            if name in ("href", "src") and value is not None:
                self.urls.append((tag, value))
        if tag not in HTML_VOID_ELEMENTS:
            self.stack.append(tag)

    def handle_startendtag(self, tag, attrs):
        for name, value in attrs:
            if name in ("href", "src") and value is not None:
                self.urls.append((tag, value))

    def handle_endtag(self, tag):
        if tag in HTML_VOID_ELEMENTS:
            return
        if not self.stack or self.stack[-1] != tag:
            self.errors.append(f"unexpected </{tag}> with open {self.stack[-3:]}")
            if tag in self.stack:
                while self.stack and self.stack.pop() != tag:
                    pass
            return
        self.stack.pop()


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
        context = generate_docs.build_context()

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

    def test_install_readme_is_well_formed_html(self) -> None:
        html = generate_docs.INSTALL_OUTPUT.read_text(encoding="utf-8")
        self.assertTrue(html.startswith("<!DOCTYPE html>"))
        checker = _NestingChecker()
        checker.feed(html)
        checker.close()
        self.assertEqual([], checker.errors)
        self.assertEqual([], checker.stack, "unclosed elements")
        self.assertNotIn("${", html, "unrendered template placeholder")

    def test_install_readme_works_offline_from_a_release_package(self) -> None:
        # The HTML ships inside release archives, so it may not load scripts,
        # stylesheets, fonts or images from anywhere; links are fine.
        html = generate_docs.INSTALL_OUTPUT.read_text(encoding="utf-8")
        self.assertNotIn("<script", html.lower())
        self.assertNotRegex(html, r"(?i)<link[^>]+rel=\"stylesheet\"")
        self.assertNotRegex(html, r"(?i)url\(\s*['\"]?(?:https?:)?//")
        self.assertNotRegex(html, r"(?i)@import")
        checker = _NestingChecker()
        checker.feed(html)
        for tag, url in checker.urls:
            if tag in ("img", "link", "source"):
                self.assertTrue(url.startswith("data:"), f"<{tag}> must be inline: {url}")

        # Brand artwork comes from misc/branding, rendered inline.
        self.assertIn('class="logo" role="img" aria-label="FnQL"', html)
        self.assertIn('rel="icon" type="image/svg+xml" href="data:image/svg+xml;base64,', html)
        self.assertEqual(
            generate_docs.inline_svg(generate_docs.LOGO_SVG, "FnQL", "logo"),
            generate_docs.build_context()["logo_svg"],
        )

    def test_install_readme_local_links_point_at_files_the_release_ships(self) -> None:
        from scripts import release

        shipped = {destination.as_posix() for _source, destination in release.DEFAULT_DOCS}
        html = generate_docs.INSTALL_OUTPUT.read_text(encoding="utf-8")
        checker = _NestingChecker()
        checker.feed(html)
        local = [
            url
            for tag, url in checker.urls
            if tag == "a" and not url.startswith(("http://", "https://", "#", "data:", "mailto:"))
        ]
        self.assertTrue(local, "expected links to bundled docs")
        for url in local:
            self.assertIn(url.split("#", 1)[0], shipped, f"README.html links {url}, which releases do not ship")

    def test_inline_svg_rejects_template_sigils(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            svg = Path(tmp) / "bad.svg"
            svg.write_text('<svg viewBox="0 0 1 1"><text>$x</text></svg>', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "would break template rendering"):
                generate_docs.inline_svg(svg, "x", "logo")

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
