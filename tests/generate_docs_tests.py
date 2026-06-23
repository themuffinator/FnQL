from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts import generate_docs


class GenerateDocsTests(unittest.TestCase):
    def test_render_reports_missing_template_keys_with_template_path(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            template = Path(tmp) / "README.md.in"
            template.write_text("FnQL $version $missing_key\n", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "missing_key"):
                generate_docs.render(template, {"version": "0.1.0"})


if __name__ == "__main__":
    unittest.main()
