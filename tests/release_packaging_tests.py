from __future__ import annotations

import shutil
import io
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts import release
from scripts import root_archive
from scripts import verify_release_layout


class ReleasePackagingTests(unittest.TestCase):
    def test_copy_release_artifact_contents_filters_build_garbage(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "artifact"
            target = root / "stage"
            source.mkdir()
            (source / "fnql.x86").write_text("binary", encoding="utf-8")
            (source / "README.txt").write_text("keep", encoding="utf-8")
            (source / "baseq3" / "maps").mkdir(parents=True)
            (source / "baseq3" / "maps" / "q3dm1.azb").write_bytes(b"zones")
            (source / "missionpack" / "vm").mkdir(parents=True)
            (source / "missionpack" / "vm" / "cgame.qvm").write_text(
                "mod data",
                encoding="utf-8",
            )
            (source / "renderer.pdb").write_text("debug", encoding="utf-8")
            (source / ".DS_Store").write_text("finder", encoding="utf-8")
            (source / "meson-info").mkdir()
            (source / "meson-info" / "intro-targets.json").write_text("{}", encoding="utf-8")
            (source / "__pycache__").mkdir()
            (source / "__pycache__" / "junk.pyc").write_bytes(b"\0")
            (source / "FnQL.dSYM" / "Contents").mkdir(parents=True)
            (source / "FnQL.dSYM" / "Contents" / "Info.plist").write_text("debug", encoding="utf-8")

            skipped = release.copy_release_artifact_contents(source, target)

            kept = sorted(path.relative_to(target).as_posix() for path in target.rglob("*") if path.is_file())

        self.assertEqual(
            kept,
            [
                "README.txt",
                "baseq3/maps/q3dm1.azb",
                "fnql.x86",
                "missionpack/vm/cgame.qvm",
            ],
        )
        self.assertIn("renderer.pdb", skipped)
        self.assertIn(".DS_Store", skipped)
        self.assertIn("meson-info", skipped)
        self.assertIn("__pycache__", skipped)
        self.assertIn("FnQL.dSYM", skipped)

    def test_packaged_docs_are_minimal_player_archive_docs(self) -> None:
        destinations = {destination.as_posix() for _source, destination in release.DEFAULT_DOCS}

        self.assertIn("LICENSE", destinations)
        self.assertIn("README.html", destinations)
        self.assertIn("docs/fnql/TECHNICAL.md", destinations)
        self.assertIn("docs/fnql/RTX_RENDERER.md", destinations)
        self.assertIn("docs/GLX.md", destinations)
        self.assertNotIn("docs/fnql/GLX_PROMOTION.md", destinations)
        self.assertNotIn("docs/fnql/GLX_ROLLBACK_PACKAGE.md", destinations)
        self.assertNotIn("docs/fnql/GLX_VISUAL_DOSSIER.md", destinations)

    def test_standard_q3a_audio_zone_assets_are_packaged_in_root_archive(self) -> None:
        destinations = {
            destination.as_posix()
            for _source, destination in release.DEFAULT_AUDIO_ZONE_ASSETS
        }
        required_destinations = {
            destination.as_posix()
            for _source, destination in root_archive.DEFAULT_ROOT_ARCHIVE_REQUIRED_ASSETS
        }
        sources = {
            source.relative_to(ROOT).as_posix()
            for source, _destination in release.DEFAULT_AUDIO_ZONE_ASSETS
        }

        self.assertEqual(len(destinations), 35)
        self.assertEqual(len(required_destinations), 38)
        self.assertIn("pkg/baseq3/maps/q3dm1.azb", sources)
        self.assertIn("baseq3/maps/q3dm1.azb", destinations)
        self.assertIn("baseq3/maps/q3dm17.azb", destinations)
        self.assertIn("baseq3/maps/q3tourney6_ctf.azb", destinations)
        self.assertIn("baseq3/maps/pro-q3dm6.azb", destinations)
        self.assertIn("baseq3/sound/fnql-weapon-sounds.sndshd", required_destinations)
        self.assertIn("missionpack/sound/fnql-weapon-sounds.sndshd", required_destinations)
        self.assertIn("baseq3/scripts/fnql.shader", required_destinations)
        self.assertNotIn("baseq3/maps/test_bigbox.azb", destinations)

        with tempfile.TemporaryDirectory() as tmp:
            archive_path = Path(tmp) / release.ROOT_ARCHIVE_NAME

            release.write_root_archive(archive_path)
            release.validate_root_archive(archive_path)

            with zipfile.ZipFile(archive_path) as archive:
                packaged = set(archive.namelist())

        expected_packaged = {
            destination.as_posix()
            for _source, destination in root_archive.iter_package_assets()
        }
        self.assertTrue(required_destinations.issubset(packaged))
        self.assertEqual(packaged, expected_packaged)
        self.assertNotIn("baseq3/fnql-hud.json", packaged)

    def test_meson_root_archive_inputs_include_non_map_pkg_assets(self) -> None:
        meson_build = (ROOT / "meson.build").read_text(encoding="utf-8")

        self.assertNotIn("'pkg/baseq3/fnql-hud.json'", meson_build)
        self.assertIn("'pkg/baseq3/scripts/fnql.shader'", meson_build)
        self.assertIn("'pkg/baseq3/sound/fnql-weapon-sounds.sndshd'", meson_build)
        self.assertIn("'pkg/missionpack/sound/fnql-weapon-sounds.sndshd'", meson_build)

    def test_root_archive_rejects_unsafe_custom_archive_names(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            package_root = root / "pkg"
            package_root.mkdir()
            source = package_root / "asset.txt"
            source.write_text("asset", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "unsafe path component"):
                root_archive.write_root_archive(
                    root / release.ROOT_ARCHIVE_NAME,
                    package_root=package_root,
                    assets=[(source, Path("..") / "asset.txt")],
                )

        unsafe_names = (
            "baseq3/maps/bad:name.azb",
            "baseq3/maps/bad\nname.azb",
        )
        for name in unsafe_names:
            with self.subTest(name=name):
                with self.assertRaisesRegex(ValueError, "unsafe|stream"):
                    root_archive.validate_archive_member_names([name], archive_name="pkg.fnz")

        with self.assertRaisesRegex(ValueError, "duplicate"):
            root_archive.validate_archive_member_names(
                ["baseq3/maps/q3dm1.azb", "baseq3/maps/Q3DM1.azb"],
                archive_name="pkg.fnz",
            )

    def test_root_archive_rejects_custom_sources_outside_package_root(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            package_root = root / "pkg"
            package_root.mkdir()
            outside = root / "outside.txt"
            outside.write_text("do not package", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "escapes package root"):
                root_archive.write_root_archive(
                    root / release.ROOT_ARCHIVE_NAME,
                    package_root=package_root,
                    assets=[(outside, Path("baseq3") / "outside.txt")],
                )

    def test_root_archive_packs_the_whole_pkg_tree(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            package_root = root / "pkg"
            (package_root / "baseq3" / "maps").mkdir(parents=True)
            (package_root / "baseq3" / "maps" / "q3dm1.azb").write_bytes(b"zone")
            (package_root / "baseq3" / "scripts" / "custom.shader").parent.mkdir(parents=True)
            (package_root / "baseq3" / "scripts" / "custom.shader").write_text(
                "textures/custom {}",
                encoding="utf-8",
            )
            (package_root / "missionpack" / "sound" / "custom.sndshd").parent.mkdir(parents=True)
            (package_root / "missionpack" / "sound" / "custom.sndshd").write_text(
                "custom {}",
                encoding="utf-8",
            )
            (package_root / "baseq2" / "maps").mkdir(parents=True)
            (package_root / "baseq2" / "maps" / "q2dm1.azb").write_bytes(b"q2 zone")
            archive_path = root / release.ROOT_ARCHIVE_NAME

            root_archive.write_root_archive(archive_path, package_root=package_root)
            with zipfile.ZipFile(archive_path) as archive:
                names = set(archive.namelist())

        self.assertIn("baseq3/maps/q3dm1.azb", names)
        self.assertIn("baseq3/scripts/custom.shader", names)
        self.assertIn("missionpack/sound/custom.sndshd", names)
        self.assertIn("baseq2/maps/q2dm1.azb", names)
        self.assertNotIn("pkg/baseq3/scripts/custom.shader", names)

    def test_release_layout_verifier_requires_root_package_archive(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)

            release.copy_docs(root)
            release.build_fnql_webpak(root)
            archive_path = release.build_root_archive(root)

            verify_release_layout.verify_release_layout(root)

            archive_path.unlink()
            with self.assertRaises(FileNotFoundError):
                verify_release_layout.verify_release_layout(root)

    def test_release_layout_verifier_requires_package_docs_for_directories(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)

            release.build_fnql_webpak(root)
            release.build_root_archive(root)

            with self.assertRaisesRegex(FileNotFoundError, "missing required release files"):
                verify_release_layout.verify_release_layout(root)

    def test_release_archive_keeps_game_dirs_at_archive_root(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage_root = root / "stage"
            stage_root.mkdir()
            (stage_root / "fnql.x86.exe").write_text("binary", encoding="utf-8")
            (stage_root / "missionpack" / "vm").mkdir(parents=True)
            (stage_root / "missionpack" / "vm" / "cgame.qvm").write_text(
                "mod data",
                encoding="utf-8",
            )
            release.copy_docs(stage_root)
            release.build_fnql_webpak(stage_root)
            release.build_root_archive(stage_root)
            archive_path = Path(
                shutil.make_archive(str(root / "fnql-root"), "zip", root_dir=stage_root)
            )

            release.validate_release_archive_contents(archive_path)
            with zipfile.ZipFile(archive_path) as archive:
                names = set(archive.namelist())
                root_archive_bytes = archive.read(release.ROOT_ARCHIVE_NAME)
            with zipfile.ZipFile(io.BytesIO(root_archive_bytes)) as root_archive:
                root_archive_names = set(root_archive.namelist())

        self.assertIn(release.ROOT_ARCHIVE_NAME, names)
        self.assertIn(release.FNQL_WEBPAK_NAME, names)
        self.assertIn("missionpack/vm/cgame.qvm", names)
        self.assertNotIn("baseq3/maps/q3dm1.azb", names)
        self.assertIn("baseq3/maps/q3dm1.azb", root_archive_names)
        self.assertIn("baseq3/maps/q3dm17.azb", root_archive_names)
        self.assertIn("baseq3/sound/fnql-weapon-sounds.sndshd", root_archive_names)
        self.assertIn("missionpack/sound/fnql-weapon-sounds.sndshd", root_archive_names)
        self.assertNotIn("maps/q3dm1.azb", names)

    def test_release_archive_validation_requires_package_docs(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage_root = root / "stage"
            stage_root.mkdir()
            (stage_root / "fnql.x86.exe").write_text("binary", encoding="utf-8")
            release.build_fnql_webpak(stage_root)
            release.build_root_archive(stage_root)
            archive_path = Path(
                shutil.make_archive(str(root / "fnql-missing-docs"), "zip", root_dir=stage_root)
            )

            with self.assertRaisesRegex(ValueError, "missing required release files"):
                release.validate_release_archive_contents(archive_path)

    def test_release_layout_rejects_renderer_modules_outside_public_three(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "fnql.x86.exe").write_text("binary", encoding="utf-8")
            (root / "fnql_opengl_x86.dll").write_text(
                "old renderer", encoding="utf-8"
            )

            with self.assertRaisesRegex(ValueError, "only glx, vk, and rtx"):
                release.validate_stage_tree(root)

    def test_release_layout_accepts_the_public_renderer_modules(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "fnql.x86.exe").write_text("binary", encoding="utf-8")
            for renderer in ("glx", "vk", "rtx"):
                (root / f"fnql_{renderer}_x86.dll").write_text(
                    renderer, encoding="utf-8"
                )

            release.validate_stage_tree(root)

    def test_copy_release_artifact_contents_rejects_symlinks(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "artifact"
            target = root / "stage"
            source.mkdir()
            outside = root / "outside.txt"
            outside.write_text("outside", encoding="utf-8")
            link = source / "outside-link.txt"
            try:
                link.symlink_to(outside)
            except (NotImplementedError, OSError) as exc:
                self.skipTest(f"symlink creation is unavailable: {exc}")

            with self.assertRaisesRegex(ValueError, "symbolic link"):
                release.copy_release_artifact_contents(source, target)

    def test_copy_release_artifact_contents_rejects_target_inside_source(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "artifact"
            source.mkdir()
            (source / "fnql.x86.exe").write_text("binary", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "inside artifact source"):
                release.copy_release_artifact_contents(source, source / "stage")

    def test_release_artifact_dirs_rejects_empty_roots(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)

            with self.assertRaisesRegex(ValueError, "does not contain any artifact directories"):
                release.release_artifact_dirs(root)

    def test_release_artifact_dirs_reject_64_bit_architecture_names(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "windows-msvc-x86").mkdir()
            (root / "linux-x86_64").mkdir()

            with self.assertRaisesRegex(ValueError, "32-bit x86 only"):
                release.release_artifact_dirs(root)

    def test_release_stage_rejects_a_64_bit_pe_even_without_an_arch_suffix(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            binary = bytearray(128)
            binary[0:2] = b"MZ"
            binary[60:64] = (64).to_bytes(4, "little")
            binary[64:68] = b"PE\0\0"
            binary[68:70] = (0x8664).to_bytes(2, "little")
            (root / "fnql.exe").write_bytes(binary)

            with self.assertRaisesRegex(ValueError, "PE machine 0x8664"):
                release.validate_stage_tree(root)

    def test_release_binary_header_gate_rejects_64_bit_elf_and_macho(self) -> None:
        elf = bytearray(64)
        elf[0:4] = b"\x7fELF"
        elf[4] = 2
        elf[5] = 1
        elf[18:20] = (62).to_bytes(2, "little")

        for name, binary in (
            ("fnql", bytes(elf)),
            ("fnql.dylib", b"\xcf\xfa\xed\xfe" + bytes(60)),
        ):
            with self.subTest(name=name):
                with self.assertRaisesRegex(ValueError, "32-bit x86 only"):
                    release.validate_release_binary_stream(name, io.BytesIO(binary))

    def test_release_workflow_builds_only_32_bit_x86_artifacts(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(
            encoding="utf-8"
        )

        self.assertIn("name: windows-mingw-x86", workflow)
        self.assertIn("name: windows-msvc-x86", workflow)
        self.assertIn("name: linux-x86", workflow)
        self.assertIn("ARCH=x86 COMPILE_ARCH=x86", workflow)
        self.assertNotIn("ubuntu-arm64:", workflow)
        self.assertNotIn("macos-x86:", workflow)
        self.assertNotIn("arch: [x86, x86_64]", workflow)
        self.assertNotIn("arch: [arm64, x86, x64]", workflow)

    def test_release_cli_parser_rejects_negative_build_numbers(self) -> None:
        with self.assertRaisesRegex(Exception, "non-negative"):
            release.non_negative_int("-1")


if __name__ == "__main__":
    unittest.main()
