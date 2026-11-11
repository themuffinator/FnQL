from __future__ import annotations

import json
import sys
import tempfile
import unittest
import unittest.mock
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from scripts import fnql_game
from scripts import release
from release_packaging_tests import make_elf_i386, make_pe, populate_linux_release_stage


EXPORTS = b"\0dllEntry\0FnQL_GetModuleInfo\0"
PIN = {"repository": "themuffinator/FnQL-game", "commit": "a" * 40}


def make_module(platform: str) -> bytes:
    base = make_pe(dll=True) if platform == "windows" else make_elf_i386()
    return base + EXPORTS


def make_install(root: Path, platform: str) -> Path:
    install = root / "install"
    for name in fnql_game.module_names(platform):
        path = install / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(make_module(platform))
    return install


def make_source(root: Path) -> Path:
    source = root / "FnQL-game"
    for source_name, _destination in fnql_game.LICENSE_DOCS:
        path = source / source_name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"{source_name}\n", encoding="utf-8")
    return source


def stage_platform(root: Path, platform: str, pin: dict[str, str] = PIN) -> Path:
    output = root / "game" / release.FNQL_GAME_ARTIFACT_DIRS[platform]
    fnql_game.stage(
        platform=platform,
        install_root=make_install(root / platform, platform),
        source=make_source(root / platform),
        output=output,
        pin=pin,
        verify_checkout=False,
    )
    return root / "game"


class FnqlGamePinTests(unittest.TestCase):
    def test_pin_names_the_companion_repository_and_a_full_commit(self) -> None:
        pin = fnql_game.load_pin()
        self.assertEqual(pin["repository"], "themuffinator/FnQL-game")
        self.assertRegex(pin["commit"], r"^[0-9a-f]{40}$")

    def test_pin_rejects_short_commits_and_bad_repositories(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "pin.json"
            path.write_text(json.dumps({"repository": "FnQL-game", "commit": "a" * 40}))
            with self.assertRaisesRegex(ValueError, "owner/name"):
                fnql_game.load_pin(path)
            path.write_text(json.dumps({"repository": "a/b", "commit": "abc123"}))
            with self.assertRaisesRegex(ValueError, "40-character"):
                fnql_game.load_pin(path)

    def test_module_names_follow_the_engine_arch_strings(self) -> None:
        self.assertEqual(
            fnql_game.module_names("windows"),
            ("baseq3/fnql_qagamex86.dll", "baseq3/fnql_cgamex86.dll", "baseq3/fnql_uix86.dll"),
        )
        self.assertEqual(
            fnql_game.module_names("linux"),
            ("baseq3/fnql_qagamei386.so", "baseq3/fnql_cgamei386.so", "baseq3/fnql_uii386.so"),
        )
        with self.assertRaises(ValueError):
            fnql_game.module_names("macos")


class FnqlGameStageTests(unittest.TestCase):
    def test_stage_copies_modules_licensing_docs_and_build_info(self) -> None:
        for platform in ("windows", "linux"):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as tmp:
                game_root = stage_platform(Path(tmp), platform)
                staged = game_root / release.FNQL_GAME_ARTIFACT_DIRS[platform]
                found = sorted(
                    path.relative_to(staged).as_posix()
                    for path in staged.rglob("*")
                    if path.is_file()
                )
                self.assertEqual(found, sorted(fnql_game.member_names(platform)))
                info = (staged / fnql_game.DOCS_DIR / fnql_game.BUILD_INFO_NAME).read_text(
                    encoding="utf-8"
                )
                self.assertEqual(fnql_game.build_info_commit(info), PIN["commit"])
                self.assertIn("vm_gameProvider fnql", info)
                self.assertIn("two licenses", info)

    def test_stage_rejects_missing_wrong_arch_and_unidentified_modules(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            install = make_install(root, "linux")
            source = make_source(root)
            (install / fnql_game.module_names("linux")[1]).unlink()
            with self.assertRaisesRegex(FileNotFoundError, "fnql_cgamei386.so"):
                fnql_game.stage(
                    platform="linux", install_root=install, source=source,
                    output=root / "out1", pin=PIN, verify_checkout=False,
                )

        with self.assertRaisesRegex(ValueError, "32-bit i386 ELF"):
            fnql_game.validate_module("m.so", make_pe(dll=True) + EXPORTS, "linux")
        with self.assertRaisesRegex(ValueError, "32-bit i386 PE DLL"):
            fnql_game.validate_module("m.dll", make_pe(dll=False) + EXPORTS, "windows")
        with self.assertRaisesRegex(ValueError, "FnQL_GetModuleInfo"):
            fnql_game.validate_module("m.so", make_elf_i386() + b"\0dllEntry\0", "linux")

    def test_stage_requires_the_checkout_to_match_the_pin(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with unittest.mock.patch.object(fnql_game, "checkout_commit", return_value="b" * 40):
                with self.assertRaisesRegex(ValueError, "pins"):
                    fnql_game.stage(
                        platform="linux", install_root=make_install(root, "linux"),
                        source=make_source(root), output=root / "out", pin=PIN,
                    )


class FnqlGamePackagingTests(unittest.TestCase):
    def test_linux_package_carries_the_game_and_validates_against_the_pin(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game_root = stage_platform(root, "linux")
            stage_root = root / "stage"
            stage_root.mkdir()
            release.overlay_fnql_game(stage_root, game_root, "linux")
            populate_linux_release_stage(stage_root)
            archive = root / "fnql-linux-x86.tar.gz"
            release.write_release_archive(stage_root, archive)

            release.validate_release_archive_contents(
                archive, require_fnql_game=True, fnql_game_pin=PIN
            )
            members = {member.name: member for member in release.read_release_archive(archive)}
            self.assertEqual(members["baseq3/fnql_qagamei386.so"].mode, 0o755)
            self.assertIn("docs/fnql/FNQL_GAME_BINARY_NOTICE.txt", members)
            self.assertIn("docs/fnql-game/QIIIA-Game-Source-License.txt", members)

            with self.assertRaisesRegex(ValueError, "pins"):
                release.validate_release_archive_contents(
                    archive,
                    require_fnql_game=True,
                    fnql_game_pin={**PIN, "commit": "c" * 40},
                )

    def test_required_game_rejects_packages_without_it(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage_root = root / "stage"
            stage_root.mkdir()
            populate_linux_release_stage(stage_root)
            archive = root / "fnql-linux-x86.tar.gz"
            release.write_release_archive(stage_root, archive)

            release.validate_release_archive_contents(archive)
            with self.assertRaisesRegex(ValueError, "missing FnQL-game files"):
                release.validate_release_archive_contents(archive, require_fnql_game=True)

    def test_partial_game_sets_are_rejected_even_when_not_required(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage_root = root / "stage"
            (stage_root / "baseq3").mkdir(parents=True)
            (stage_root / "baseq3" / "fnql_qagamei386.so").write_bytes(make_module("linux"))
            populate_linux_release_stage(stage_root)
            archive = root / "fnql-linux-x86.tar.gz"
            release.write_release_archive(stage_root, archive)

            with self.assertRaisesRegex(ValueError, "missing FnQL-game files"):
                release.validate_release_archive_contents(archive)

    def test_windows_overlay_is_validated_as_pe(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game_root = stage_platform(root, "windows")
            stage_root = root / "stage"
            stage_root.mkdir()
            added = release.overlay_fnql_game(stage_root, game_root, "windows")
            self.assertIn("baseq3/fnql_uix86.dll", added)
            members = [
                release.ReleaseArchiveMember(
                    name=path.relative_to(stage_root).as_posix(),
                    data=path.read_bytes(),
                    mode=0o644,
                )
                for path in stage_root.rglob("*")
                if path.is_file()
            ]
            release.validate_fnql_game_members(
                members, platform="windows", archive_name="w.zip", pin=PIN
            )

            with self.assertRaisesRegex(ValueError, "already contains"):
                release.overlay_fnql_game(stage_root, game_root, "windows")

    def test_overlay_rejects_incomplete_or_padded_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game_root = stage_platform(root, "linux")
            staged = game_root / "linux-x86"
            (staged / "extra.txt").write_text("x", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unexpected: extra.txt"):
                release.overlay_fnql_game(root / "s1", game_root, "linux")
            (staged / "extra.txt").unlink()
            (staged / "baseq3" / "fnql_uii386.so").unlink()
            with self.assertRaisesRegex(ValueError, "missing: baseq3/fnql_uii386.so"):
                release.overlay_fnql_game(root / "s2", game_root, "linux")
            with self.assertRaisesRegex(FileNotFoundError, "windows"):
                release.overlay_fnql_game(root / "s3", game_root, "windows")

    def test_public_and_tagged_releases_require_the_game(self) -> None:
        local = SimpleNamespace(channel="manual", game_root=None, require_game=False)
        self.assertIsNone(release.resolve_game_root(local))
        for args in (
            SimpleNamespace(channel="manual", game_root=None, require_game=True),
            SimpleNamespace(channel="release", game_root=None, require_game=False),
        ):
            with self.assertRaisesRegex(ValueError, "must bundle the FnQL-game modules"):
                release.resolve_game_root(args)


class FnqlGameContainerPythonTests(unittest.TestCase):
    def test_stage_helper_runs_on_the_release_container_python(self) -> None:
        # fnql-game-linux runs these with Ubuntu 20.04's Python 3.8.
        import ast

        for name in ("fnql_game.py", "windows_pe.py", "check_elf_glibc.py"):
            with self.subTest(script=name):
                source = (ROOT / "scripts" / name).read_text(encoding="utf-8")
                ast.parse(source, feature_version=(3, 8))
                self.assertNotRegex(source, r"write_text\([^)]*newline=")


class FnqlGameWorkflowTests(unittest.TestCase):
    def test_release_workflow_builds_gates_and_packages_the_pinned_game(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(encoding="utf-8")

        self.assertIn("  fnql-game-windows:", workflow)
        self.assertIn("  fnql-game-linux:", workflow)
        self.assertEqual(workflow.count("scripts/fnql_game.py github-output"), 2)
        self.assertEqual(workflow.count("ref: ${{ steps.pin.outputs.commit }}"), 2)
        self.assertEqual(workflow.count("meson test -C game-build --print-errorlogs"), 2)
        self.assertIn("--cross-file FnQL-game/meson/linux-i686.ini", workflow)
        self.assertIn("-Dfnql-root=$PWD/FnQL", workflow)
        self.assertIn("-Dfnql-root=%CD%\\FnQL", workflow)
        self.assertIn("stage --platform windows", workflow)
        self.assertIn("stage --platform linux", workflow)
        self.assertIn(
            'check_elf_glibc.py --max-glibc "$FNQL_LINUX_GLIBC_MAX" --require-elf game-stage',
            workflow,
        )
        self.assertIn("check_windows_runtime_deps.py --require-pe game-stage", workflow)
        game_linux = workflow.split("  fnql-game-linux:", 1)[1].split("  push-build-validation:", 1)[0]
        self.assertIn("image: ubuntu:20.04", game_linux)
        publish = workflow.split("  publish:", 1)[1]
        self.assertIn("name: fnql-game-windows-x86", publish)
        self.assertIn("name: fnql-game-linux-x86", publish)
        self.assertIn("path: game-artifacts/windows-x86", publish)
        self.assertIn("path: game-artifacts/linux-x86", publish)
        self.assertIn("--game-root game-artifacts --require-game", publish)

    def test_licensing_notice_states_both_licenses_and_the_open_question(self) -> None:
        notice = (ROOT / "docs" / "fnql" / "FNQL_GAME_BINARY_NOTICE.txt").read_text(
            encoding="utf-8"
        )
        self.assertIn("version 2 or (at", notice)
        self.assertIn("Limited Use Software License Agreement", notice)
        self.assertIn("not been confirmed upstream", notice)
        self.assertIn("free of charge, for non-commercial use", notice)
        self.assertIn("docs/fnql-game/BUILD-INFO.txt", notice)


if __name__ == "__main__":
    unittest.main()
