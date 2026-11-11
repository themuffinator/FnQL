from __future__ import annotations

import json
import shutil
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
from release_packaging_tests import (
    job_needs,
    make_elf_i386,
    make_elf_x86_64,
    make_pe,
    make_pe_x86_64,
    populate_linux_release_stage,
    populate_windows_x86_64_release_stage,
    workflow_jobs,
)


EXPORTS = b"\0dllEntry\0FnQL_GetModuleInfo\0"
PIN = {"repository": "themuffinator/FnQL-game", "commit": "a" * 40}


def make_module(platform: str, arch: str = "x86") -> bytes:
    if arch == "x86":
        base = make_pe(dll=True) if platform == "windows" else make_elf_i386()
    else:
        base = make_pe_x86_64(dll=True) if platform == "windows" else make_elf_x86_64()
    return base + EXPORTS


def make_install(root: Path, platform: str, arch: str = "x86", module_arch: str | None = None) -> Path:
    install = root / "install"
    for name in fnql_game.module_names(platform, arch):
        path = install / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(make_module(platform, module_arch or arch))
    return install


def make_source(root: Path) -> Path:
    source = root / "FnQL-game"
    for source_name, _destination in fnql_game.LICENSE_DOCS:
        path = source / source_name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"{source_name}\n", encoding="utf-8")
    return source


def stage_platform(
    root: Path, platform: str, pin: dict[str, str] = PIN, arch: str = "x86"
) -> Path:
    output = root / "game" / release.FNQL_GAME_ARTIFACT_DIRS[(platform, arch)]
    work = root / f"{platform}-{arch}"
    fnql_game.stage(
        platform=platform,
        arch=arch,
        install_root=make_install(work, platform, arch),
        source=make_source(work),
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

    def test_x86_64_module_names_use_the_x86_64_arch_string_on_both_platforms(self) -> None:
        self.assertEqual(
            fnql_game.module_names("windows", "x86_64"),
            (
                "baseq3/fnql_qagamex86_64.dll",
                "baseq3/fnql_cgamex86_64.dll",
                "baseq3/fnql_uix86_64.dll",
            ),
        )
        self.assertEqual(
            fnql_game.module_names("linux", "x86_64"),
            (
                "baseq3/fnql_qagamex86_64.so",
                "baseq3/fnql_cgamex86_64.so",
                "baseq3/fnql_uix86_64.so",
            ),
        )
        self.assertEqual(fnql_game.module_names("windows"), fnql_game.module_names("windows", "x86"))
        self.assertEqual(fnql_game.artifact_dir_name("linux", "x86_64"), "linux-x86_64")
        self.assertEqual(fnql_game.artifact_dir_name("windows"), "windows-x86")
        self.assertEqual(
            release.FNQL_GAME_ARTIFACT_DIRS,
            {
                ("linux", "x86"): "linux-x86",
                ("windows", "x86"): "windows-x86",
                ("linux", "x86_64"): "linux-x86_64",
                ("windows", "x86_64"): "windows-x86_64",
            },
        )
        # Every published engine target has a matching FnQL-game set.
        self.assertEqual(
            set(release.RELEASE_ARTIFACT_TARGETS.values()), set(release.FNQL_GAME_ARTIFACT_DIRS)
        )
        for platform, arch in (("windows", "arm64"), ("linux", "aarch64"), ("macos", "x86_64")):
            with self.subTest(platform=platform, arch=arch), self.assertRaisesRegex(
                ValueError, "not built for"
            ):
                fnql_game.module_names(platform, arch)
            with self.assertRaisesRegex(ValueError, "not built for"):
                fnql_game.validate_module("m", make_module("linux", "x86_64"), platform, arch)

    def test_stage_cli_defaults_to_x86_and_accepts_x86_64(self) -> None:
        common = ["stage", "--platform", "linux", "--install-root", "i", "--source", "s", "--output", "o"]
        self.assertEqual(fnql_game.parse_args(common).arch, "x86")
        self.assertEqual(fnql_game.parse_args([*common, "--arch", "x86_64"]).arch, "x86_64")
        with unittest.mock.patch("sys.stderr"), self.assertRaises(SystemExit):
            fnql_game.parse_args([*common, "--arch", "arm64"])
        with unittest.mock.patch("sys.stderr"), self.assertRaises(SystemExit):
            fnql_game.parse_args(["stage", "--platform", "macos", *common[3:]])


class FnqlGameStageTests(unittest.TestCase):
    def test_stage_copies_modules_licensing_docs_and_build_info(self) -> None:
        for platform in ("windows", "linux"):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as tmp:
                game_root = stage_platform(Path(tmp), platform)
                staged = game_root / release.FNQL_GAME_ARTIFACT_DIRS[(platform, "x86")]
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
                self.assertIn(f"Platform: {platform}\n", info)
                self.assertIn("Architecture: x86\n", info)
                self.assertEqual(fnql_game.build_info_arch(info), "x86")
                self.assertIn("keeps using the retail\nclient modules", info)

    def test_stage_x86_64_copies_modules_and_names_the_architecture(self) -> None:
        for platform in ("windows", "linux"):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as tmp:
                game_root = stage_platform(Path(tmp), platform, arch="x86_64")
                staged = game_root / f"{platform}-x86_64"
                found = sorted(
                    path.relative_to(staged).as_posix()
                    for path in staged.rglob("*")
                    if path.is_file()
                )
                self.assertEqual(found, sorted(fnql_game.member_names(platform, "x86_64")))
                for name in fnql_game.module_names(platform, "x86_64"):
                    fnql_game.validate_module(
                        name, (staged / name).read_bytes(), platform, "x86_64"
                    )
                info = (staged / fnql_game.DOCS_DIR / fnql_game.BUILD_INFO_NAME).read_text(
                    encoding="utf-8"
                )
                self.assertEqual(fnql_game.build_info_commit(info), PIN["commit"])
                self.assertEqual(fnql_game.build_info_arch(info), "x86_64")
                self.assertIn(f"Platform: {platform}\n", info)
                self.assertIn("fnql_qagamex86_64", info)
                self.assertIn("only for 32-bit x86", info)
                self.assertNotIn("retail\nclient modules", info)
                self.assertIn("two licenses", info)

    def test_stage_x86_64_rejects_wrong_arch_modules(self) -> None:
        for platform in ("windows", "linux"):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                install = make_install(root, platform, "x86_64", module_arch="x86")
                with self.assertRaisesRegex(ValueError, "must be a 64-bit x86_64"):
                    fnql_game.stage(
                        platform=platform, arch="x86_64", install_root=install,
                        source=make_source(root), output=root / "out", pin=PIN,
                        verify_checkout=False,
                    )
                # An x86_64 install staged as x86 is missing the x86 names.
                with self.assertRaisesRegex(FileNotFoundError, "fnql_qagame"):
                    fnql_game.stage(
                        platform=platform, install_root=install, source=make_source(root),
                        output=root / "out2", pin=PIN, verify_checkout=False,
                    )

    def test_module_validation_is_per_arch(self) -> None:
        cases = (
            ("windows", "x86_64", make_pe(dll=True), "64-bit x86_64 \\(AMD64\\) PE DLL"),
            ("windows", "x86_64", make_pe_x86_64(dll=False), "64-bit x86_64 \\(AMD64\\) PE DLL"),
            ("windows", "x86_64", make_elf_x86_64(), "64-bit x86_64 \\(AMD64\\) PE DLL"),
            ("linux", "x86_64", make_elf_i386(), "64-bit x86_64 ELF shared object"),
            ("linux", "x86_64", make_pe_x86_64(dll=True), "64-bit x86_64 ELF shared object"),
            ("windows", "x86", make_pe_x86_64(dll=True), "32-bit i386 PE DLL"),
            ("linux", "x86", make_elf_x86_64(), "32-bit i386 ELF shared object"),
        )
        for platform, arch, base, message in cases:
            with self.subTest(platform=platform, arch=arch, message=message), self.assertRaisesRegex(
                ValueError, message
            ):
                fnql_game.validate_module("m", base + EXPORTS, platform, arch)
        executable_elf = bytearray(make_elf_x86_64())
        executable_elf[16] = 2  # ET_EXEC
        with self.assertRaisesRegex(ValueError, "64-bit x86_64 ELF shared object"):
            fnql_game.validate_module("m", bytes(executable_elf) + EXPORTS, "linux", "x86_64")
        with self.assertRaisesRegex(ValueError, "FnQL_GetModuleInfo"):
            fnql_game.validate_module("m", make_elf_x86_64() + b"\0dllEntry\0", "linux", "x86_64")
        fnql_game.validate_module("m", make_module("windows", "x86_64"), "windows", "x86_64")
        fnql_game.validate_module("m", make_module("linux", "x86_64"), "linux", "x86_64")

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

    def test_x86_64_packages_carry_their_own_game_set(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game_root = stage_platform(root, "linux", arch="x86_64")
            stage_platform(root, "windows", arch="x86_64")

            linux = root / "linux-stage"
            linux.mkdir()
            added = release.overlay_fnql_game(linux, game_root, "linux", "x86_64")
            self.assertIn("baseq3/fnql_qagamex86_64.so", added)
            populate_linux_release_stage(linux, arch="x86_64")
            release.validate_stage_tree(linux, artifact_name="linux-x86_64")
            archive = root / "fnql-linux-x86_64.tar.gz"
            release.write_release_archive(linux, archive)
            release.validate_release_archive_contents(
                archive, artifact_name="linux-x86_64", require_fnql_game=True, fnql_game_pin=PIN
            )
            members = {member.name: member for member in release.read_release_archive(archive)}
            self.assertEqual(members["baseq3/fnql_cgamex86_64.so"].mode, 0o755)

            windows = root / "windows-stage"
            windows.mkdir()
            added = release.overlay_fnql_game(windows, game_root, "windows", "x86_64")
            self.assertIn("baseq3/fnql_uix86_64.dll", added)
            populate_windows_x86_64_release_stage(windows)
            archive = root / "fnql-windows-msvc-x86_64.zip"
            release.write_release_archive(windows, archive)
            release.validate_release_archive_contents(
                archive, require_fnql_game=True, fnql_game_pin=PIN
            )
            with self.assertRaisesRegex(ValueError, "already contains"):
                release.overlay_fnql_game(windows, game_root, "windows", "x86_64")

    def test_x86_64_package_requires_and_validates_the_x86_64_game_set(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage_root = root / "stage"
            stage_root.mkdir()
            populate_linux_release_stage(stage_root, arch="x86_64")
            archive = root / "fnql-linux-x86_64.tar.gz"
            release.write_release_archive(stage_root, archive)
            release.validate_release_archive_contents(archive)
            with self.assertRaisesRegex(
                ValueError, "missing FnQL-game files: baseq3/fnql_qagamex86_64.so"
            ):
                release.validate_release_archive_contents(archive, require_fnql_game=True)

            # The x86 game set does not satisfy, and is rejected in, an x86_64 package.
            game_root = stage_platform(root, "linux")
            wrong = root / "wrong"
            wrong.mkdir()
            release.overlay_fnql_game(wrong, game_root, "linux", "x86")
            populate_linux_release_stage(wrong, arch="x86_64")
            with self.assertRaisesRegex(ValueError, "only x86_64 binaries; baseq3/fnql_"):
                release.validate_stage_tree(wrong, artifact_name="linux-x86_64")
            members = [
                release.ReleaseArchiveMember(
                    name=path.relative_to(wrong).as_posix(), data=path.read_bytes(), mode=0o755
                )
                for path in wrong.rglob("*")
                if path.is_file()
            ]
            with self.assertRaisesRegex(ValueError, "missing FnQL-game files"):
                release.validate_fnql_game_members(
                    members, platform="linux", archive_name="l.tar.gz", pin=PIN, arch="x86_64"
                )

            # Matching names with wrong-arch bytes are rejected by module validation.
            mislabeled = root / "mislabeled"
            (mislabeled / "baseq3").mkdir(parents=True)
            stage_platform(root / "x64", "linux", arch="x86_64")
            for item in (root / "x64" / "game" / "linux-x86_64").rglob("*"):
                if item.is_file():
                    target = mislabeled / item.relative_to(root / "x64" / "game" / "linux-x86_64")
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(item.read_bytes())
            (mislabeled / "baseq3" / "fnql_uix86_64.so").write_bytes(make_module("linux", "x86"))
            members = [
                release.ReleaseArchiveMember(
                    name=path.relative_to(mislabeled).as_posix(), data=path.read_bytes(), mode=0o755
                )
                for path in mislabeled.rglob("*")
                if path.is_file()
            ]
            members.append(
                release.ReleaseArchiveMember(
                    name=release.FNQL_GAME_NOTICE[1].as_posix(), data=b"notice", mode=0o644
                )
            )
            with self.assertRaisesRegex(ValueError, "fnql_uix86_64.so must be a 64-bit x86_64 ELF"):
                release.validate_fnql_game_members(
                    members, platform="linux", archive_name="l.tar.gz", pin=PIN, arch="x86_64"
                )

    def test_game_build_info_must_name_the_package_architecture(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game_root = stage_platform(root, "windows", arch="x86_64")
            stage_root = root / "stage"
            stage_root.mkdir()
            release.overlay_fnql_game(stage_root, game_root, "windows", "x86_64")
            info_path = stage_root / fnql_game.DOCS_DIR / fnql_game.BUILD_INFO_NAME
            original = info_path.read_text(encoding="utf-8")

            def members() -> list[release.ReleaseArchiveMember]:
                return [
                    release.ReleaseArchiveMember(
                        name=path.relative_to(stage_root).as_posix(),
                        data=path.read_bytes(),
                        mode=0o644,
                    )
                    for path in stage_root.rglob("*")
                    if path.is_file()
                ]

            release.validate_fnql_game_members(
                members(), platform="windows", archive_name="w.zip", pin=PIN, arch="x86_64"
            )
            for text, detail in (
                (original.replace("Architecture: x86_64", "Architecture: x86"), "for x86"),
                (original.replace("Architecture: x86_64\n", ""), "unnamed architecture"),
            ):
                with self.subTest(detail=detail):
                    info_path.write_text(text, encoding="utf-8", newline="\n")
                    with self.assertRaisesRegex(ValueError, f"x86_64 release but bundles.*{detail}"):
                        release.validate_fnql_game_members(
                            members(), platform="windows", archive_name="w.zip", pin=PIN,
                            arch="x86_64",
                        )
            # x86 build info from before the arch dimension (no Architecture line) is accepted.
            x86_root = stage_platform(root / "x86", "windows")
            x86_stage = root / "x86-stage"
            x86_stage.mkdir()
            release.overlay_fnql_game(x86_stage, x86_root, "windows")
            x86_info = x86_stage / fnql_game.DOCS_DIR / fnql_game.BUILD_INFO_NAME
            x86_info.write_text(
                x86_info.read_text(encoding="utf-8").replace("Architecture: x86\n", ""),
                encoding="utf-8",
                newline="\n",
            )
            x86_members = [
                release.ReleaseArchiveMember(
                    name=path.relative_to(x86_stage).as_posix(), data=path.read_bytes(), mode=0o644
                )
                for path in x86_stage.rglob("*")
                if path.is_file()
            ]
            release.validate_fnql_game_members(
                x86_members, platform="windows", archive_name="w.zip", pin=PIN
            )
            with self.assertRaisesRegex(ValueError, "cannot carry FnQL-game modules"):
                release.validate_fnql_game_members(
                    x86_members, platform="windows", archive_name="w.zip", arch="arm64"
                )

    def test_overlay_requires_the_x86_64_artifact_dirs(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game_root = stage_platform(root, "linux")
            with self.assertRaisesRegex(FileNotFoundError, "linux x86_64"):
                release.overlay_fnql_game(root / "s1", game_root, "linux", "x86_64")
            with self.assertRaisesRegex(ValueError, "not packaged for linux arm64"):
                release.overlay_fnql_game(root / "s2", game_root, "linux", "arm64")

    def test_required_game_packaging_covers_every_published_target(self) -> None:
        """End to end: six engine artifacts, four game sets, --require-game."""
        scratch_parent = ROOT / ".tmp"
        scratch_parent.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch_parent, prefix="release-x86_64-test-") as tmp:
            root = Path(tmp)
            artifacts = root / "artifacts"
            for name, (platform, arch) in release.RELEASE_ARTIFACT_TARGETS.items():
                artifact = artifacts / name
                artifact.mkdir(parents=True)
                if platform == "windows" and arch == "x86":
                    (artifact / "fnql.exe").write_bytes(make_pe(dll=False))
                    (artifact / "fnql_steam.dll").write_bytes(make_pe(dll=True))
                    for renderer in release.PUBLIC_RENDERERS:
                        (artifact / f"fnql_{renderer}_x86.dll").write_bytes(make_pe(dll=True))
                elif platform == "windows":
                    for executable in release.WINDOWS_X86_64_RELEASE_EXECUTABLES:
                        (artifact / executable).write_bytes(make_pe_x86_64(dll=False))
                    for module in release.WINDOWS_X86_64_RELEASE_RENDERER_MODULES:
                        (artifact / module).write_bytes(make_pe_x86_64(dll=True))
                else:
                    executables, renderers = release.LINUX_RELEASE_FILES[arch]
                    elf = make_elf_i386() if arch == "x86" else make_elf_x86_64()
                    for item in (*executables, *renderers):
                        (artifact / item).write_bytes(elf)
            game_root = root / "game"
            for platform, arch in release.FNQL_GAME_ARTIFACT_DIRS:
                stage_platform(root / "game-work" / f"{platform}-{arch}", platform, arch=arch)
                shutil.copytree(
                    root / "game-work" / f"{platform}-{arch}" / "game" / f"{platform}-{arch}",
                    game_root / f"{platform}-{arch}",
                )
            args = SimpleNamespace(
                channel="manual",
                artifact_root=artifacts,
                output_dir=root / "out",
                temp_dir=root / "temp",
                build_date="2026-11-11",
                build_number=7,
                commit="b" * 40,
                ref_name=None,
                glx_proof_root=None,
                require_glx_proof=False,
                glx_proof_build_run_id=None,
                glx_proof_build_run_attempt=None,
                game_root=game_root,
                require_game=True,
                glx_rollback_metadata=None,
            )
            with unittest.mock.patch.object(
                release.subprocess, "run"
            ) as run, unittest.mock.patch.object(
                release.fnql_game, "load_pin", return_value=PIN
            ), unittest.mock.patch("builtins.print"):
                manifest = release.build_archives(args)
                first_bytes = {
                    entry["archive"]: (root / "out" / "packages" / entry["archive"]).read_bytes()
                    for entry in manifest["archives"]
                }
                second = release.build_archives(args)
            run.assert_called()

            self.assertEqual(manifest, second)
            entries = {entry["artifact_dir"]: entry for entry in manifest["archives"]}
            self.assertEqual(
                [entry["artifact_dir"] for entry in manifest["archives"]],
                sorted(release.PUBLISHED_RELEASE_ARTIFACTS),
            )
            self.assertEqual(manifest["fnql_game"], {"included": True, **PIN})
            for name, (platform, arch) in release.RELEASE_ARTIFACT_TARGETS.items():
                with self.subTest(artifact=name):
                    entry = entries[name]
                    self.assertEqual((entry["platform"], entry["arch"]), (platform, arch))
                    suffix = ".tar.gz" if platform == "linux" else ".zip"
                    self.assertTrue(entry["archive"].endswith(f"-{name}{suffix}"))
                    path = root / "out" / "packages" / entry["archive"]
                    self.assertEqual(path.read_bytes(), first_bytes[entry["archive"]])
                    names = {member.name for member in release.read_release_archive(path)}
                    for module in fnql_game.module_names(platform, arch):
                        self.assertIn(module, names)
                    self.assertEqual(
                        "fnql_steam.dll" in names, platform == "windows" and arch == "x86"
                    )
            self.assertEqual(len({entry["archive"] for entry in manifest["archives"]}), 6)
            checksums = (root / "out" / "SHA256SUMS.txt").read_text(encoding="utf-8")
            self.assertEqual(len(checksums.splitlines()), 6)

            # --require-game: a missing x86_64 game set fails the whole release.
            shutil.rmtree(game_root / "windows-x86_64")
            with unittest.mock.patch.object(release.subprocess, "run"), unittest.mock.patch.object(
                release.fnql_game, "load_pin", return_value=PIN
            ), unittest.mock.patch("builtins.print"):
                with self.assertRaisesRegex(FileNotFoundError, "windows x86_64"):
                    release.build_archives(args)

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

        jobs = workflow_jobs(workflow)
        game_jobs = {
            # job: (platform, arch, upload name)
            "fnql-game-windows": ("windows", "x86", "fnql-game-windows-x86"),
            "fnql-game-linux": ("linux", "x86", "fnql-game-linux-x86"),
            "fnql-game-windows-x86_64": ("windows", "x86_64", "fnql-game-windows-x86_64"),
            "fnql-game-linux-x86_64": ("linux", "x86_64", "fnql-game-linux-x86_64"),
        }
        self.assertEqual(workflow.count("scripts/fnql_game.py github-output"), len(game_jobs))
        self.assertEqual(workflow.count("ref: ${{ steps.pin.outputs.commit }}"), len(game_jobs))
        self.assertEqual(
            workflow.count("meson test -C game-build --print-errorlogs"), len(game_jobs)
        )
        for job, (platform, arch, upload) in game_jobs.items():
            with self.subTest(job=job):
                body = jobs[job]
                self.assertIn("scripts/fnql_game.py github-output", body)
                self.assertIn("ref: ${{ steps.pin.outputs.commit }}", body)
                self.assertIn("meson test -C game-build --print-errorlogs", body)
                stage = f"fnql_game.py stage --platform {platform} "
                if arch == "x86":
                    # x86 jobs rely on the --arch default so they stay unchanged.
                    self.assertIn(stage + "--install-root", body)
                    self.assertNotIn("--arch", body)
                else:
                    self.assertIn(stage + f"--arch {arch} --install-root", body)
                self.assertIn(f"name: {upload}\n", body)
                if platform == "linux":
                    self.assertIn("image: ubuntu:20.04", body)
                    self.assertIn("-Dfnql-root=$PWD/FnQL", body)
                    self.assertIn(
                        'check_elf_glibc.py --max-glibc "$FNQL_LINUX_GLIBC_MAX" '
                        "--require-elf game-stage",
                        body,
                    )
                else:
                    self.assertIn("-Dfnql-root=%CD%\\FnQL", body)
                    self.assertIn("check_windows_runtime_deps.py --require-pe game-stage", body)
        self.assertIn("--cross-file FnQL-game/meson/linux-i686.ini", jobs["fnql-game-linux"])
        self.assertNotIn("linux-i686.ini", jobs["fnql-game-linux-x86_64"])
        self.assertIn("-arch=x64", jobs["fnql-game-windows-x86_64"])
        validation_needs = job_needs(jobs["push-build-validation"])
        for job in game_jobs:
            self.assertIn(job, validation_needs)
        publish = jobs["publish"]
        for _platform, _arch, upload in game_jobs.values():
            self.assertIn(f"name: {upload}\n", publish)
        for directory in ("windows-x86", "linux-x86", "windows-x86_64", "linux-x86_64"):
            self.assertIn(f"path: game-artifacts/{directory}\n", publish)
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
