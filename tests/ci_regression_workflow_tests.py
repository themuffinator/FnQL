from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class CiRegressionWorkflowTests(unittest.TestCase):
    def test_meson_source_gate_keeps_nested_suites_discoverable(self) -> None:
        meson = (ROOT / "meson.build").read_text(encoding="utf-8")

        self.assertIn("test('fnql_python_source_regressions'", meson)
        self.assertIn("test('fnql_frame_clock_source'", meson)
        self.assertIn("'-m', 'unittest', 'discover'", meson)
        self.assertIn("'-p', '*_tests.py'", meson)
        aggregate = meson.split("test('fnql_python_source_regressions'", 1)[1].split(
            "\n)", 1
        )[0]
        self.assertRegex(aggregate, r"timeout:\s*(?:[3-9]\d{2}|\d{4,})")
        for relative in (
            "tests/audio/__init__.py",
            "tests/github/__init__.py",
            "tests/glx/__init__.py",
            "tests/vulkan/__init__.py",
        ):
            with self.subTest(relative=relative):
                self.assertEqual((ROOT / relative).read_bytes(), b"")

    def test_every_pull_request_runs_the_complete_i686_meson_suite(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "linux-verification.yml").read_text(
            encoding="utf-8"
        )
        pull_request_block = workflow.split("  pull_request:", 1)[1].split(
            "  workflow_dispatch:", 1
        )[0]

        self.assertNotIn("paths:", pull_request_block)
        self.assertIn("-Daudio-tests=true", workflow)
        self.assertIn("-Dglx-tests=true", workflow)
        self.assertIn(
            "meson test -C .tmp/meson-linux-renderers --print-errorlogs",
            workflow,
        )

    def test_release_source_validation_uses_recursive_pytest_discovery(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(
            encoding="utf-8"
        )
        source_job = workflow.split("  source-validation:", 1)[1].split(
            "  ubuntu-x86:", 1
        )[0]

        self.assertIn("python -m pytest", source_job)
        self.assertNotIn("python tests/", source_job)


if __name__ == "__main__":
    unittest.main()
