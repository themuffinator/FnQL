from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts import check_elf_glibc


class CheckElfGlibcTests(unittest.TestCase):
    def test_candidate_files_rejects_missing_scan_path(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            missing = Path(tmp) / "missing"

            with self.assertRaisesRegex(FileNotFoundError, "scan path does not exist"):
                check_elf_glibc.candidate_files([missing])

    def test_candidate_files_ignores_existing_non_elf_files(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            text_file = Path(tmp) / "readme.txt"
            text_file.write_text("not an ELF file\n", encoding="utf-8")

            self.assertEqual(check_elf_glibc.candidate_files([text_file]), [])

    def test_candidate_files_deduplicates_overlapping_scan_paths(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            elf = root / "fnql"
            elf.write_bytes(b"\x7fELFsynthetic")

            candidates = check_elf_glibc.candidate_files([root, elf])

        self.assertEqual(candidates, [elf.resolve()])

    def test_parse_version_rejects_malformed_glibc_versions(self) -> None:
        self.assertEqual(check_elf_glibc.parse_version("2.31"), (2, 31))
        for value in ("", "2.", "2..31", "2.-1", "GLIBC_2.31"):
            with self.subTest(value=value):
                with self.assertRaisesRegex(ValueError, "invalid GLIBC version"):
                    check_elf_glibc.parse_version(value)


if __name__ == "__main__":
    unittest.main()
