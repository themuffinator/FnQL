from __future__ import annotations

import os
import tempfile
import unittest
import sys
import urllib.error
from unittest import mock
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts import fnql_meta
from scripts import manual_release
from scripts import version


class VersionMetadataTests(unittest.TestCase):
    def test_normalize_commit_accepts_hex_ids_and_local_fallback(self) -> None:
        self.assertEqual(fnql_meta.normalize_commit(None), "local")
        self.assertEqual(fnql_meta.normalize_commit("ABCDEF1234567890"), "abcdef12")
        self.assertEqual(fnql_meta.normalize_commit("abc1234"), "abc1234")

    def test_normalize_commit_rejects_path_like_values(self) -> None:
        with self.assertRaisesRegex(ValueError, "hexadecimal"):
            fnql_meta.normalize_commit("../../escape")

    def test_manual_channel_rejects_negative_build_numbers(self) -> None:
        with self.assertRaisesRegex(ValueError, "non-negative"):
            fnql_meta.channel_metadata(
                "manual",
                build_number=-1,
                build_date="2026-06-20",
                commit="abcdef1234567890",
            )

    def test_release_channel_rejects_mismatched_ref_names(self) -> None:
        with self.assertRaisesRegex(ValueError, "Release ref name"):
            fnql_meta.channel_metadata(
                "release",
                ref_name="refs/tags/v0.1.0",
                build_date="2026-06-20",
                commit="abcdef1234567890",
            )

    def test_package_archive_name_rejects_unsafe_artifact_directory_names(self) -> None:
        meta = fnql_meta.channel_metadata(
            "manual",
            build_number=1,
            build_date="2026-06-20",
            commit="abcdef1234567890",
        )
        self.assertEqual(
            fnql_meta.package_archive_name(meta, "windows-x86_64"),
            "fnql-0.1.0.1-20260620-abcdef12-windows-x86_64.zip",
        )
        self.assertEqual(
            fnql_meta.package_archive_name(meta, "linux-x86"),
            "fnql-0.1.0.1-20260620-abcdef12-linux-x86.tar.gz",
        )
        for name in ("../windows", "linux/x64", "bad\nname"):
            with self.subTest(name=name):
                with self.assertRaisesRegex(ValueError, "safe path component"):
                    fnql_meta.package_archive_name(meta, name)

    def test_version_composers_reject_negative_components(self) -> None:
        with self.assertRaisesRegex(ValueError, "non-negative"):
            fnql_meta.compose_version_string(0, 1, 0, -1)
        with self.assertRaisesRegex(ValueError, "non-negative"):
            fnql_meta.compose_windows_version(0, 1, 0, -1)

    def test_release_cli_parsers_reject_negative_build_numbers(self) -> None:
        with self.assertRaisesRegex(Exception, "non-negative"):
            version.non_negative_int("-1")
        with self.assertRaisesRegex(Exception, "non-negative"):
            manual_release.non_negative_int("-1")

    def test_manual_release_rejects_non_positive_ai_timeouts(self) -> None:
        for value in ("0", "-1"):
            with self.subTest(value=value):
                with self.assertRaisesRegex(Exception, "positive integer"):
                    manual_release.positive_int(value)

    def test_github_models_choice_content_ignores_malformed_responses(self) -> None:
        self.assertEqual(manual_release.github_models_choice_content({}), "")
        self.assertEqual(manual_release.github_models_choice_content({"choices": ["bad"]}), "")
        self.assertEqual(
            manual_release.github_models_choice_content(
                {"choices": [{"message": {"content": "### Highlights\n- Fixed audio."}}]}
            ),
            "### Highlights\n- Fixed audio.",
        )

    def test_github_models_highlights_wraps_network_failures(self) -> None:
        with mock.patch.dict(os.environ, {"GITHUB_TOKEN": "token"}):
            with mock.patch.object(
                manual_release.urllib.request,
                "urlopen",
                side_effect=urllib.error.URLError("offline"),
            ):
                with self.assertRaisesRegex(RuntimeError, "GitHub Models request failed"):
                    manual_release.github_models_highlights(
                        "release context",
                        model="openai/gpt-4.1",
                        timeout=1,
                    )

    def test_explicit_release_highlights_file_must_exist_and_have_content(self) -> None:
        kwargs = {
            "build_number": 1,
            "build_date": "2026-06-21",
            "version_string": "0.1.0.1",
            "from_commit": None,
            "target_commit": "abcdef1234567890",
            "changelog": ROOT / "docs" / "fnql" / "CHANGELOG.md",
            "no_ai": True,
            "notes_model": "openai/gpt-4.1",
            "ai_timeout": 1,
        }
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            missing = root / "missing.md"
            with self.assertRaisesRegex(FileNotFoundError, "release highlights file"):
                manual_release.generated_highlights(
                    **kwargs,
                    highlights_file=missing,
                )

            empty = root / "empty.md"
            empty.write_text("```markdown\n# Release notes\n```\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "empty after sanitization"):
                manual_release.generated_highlights(
                    **kwargs,
                    highlights_file=empty,
                )


if __name__ == "__main__":
    unittest.main()
