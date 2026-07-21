from __future__ import annotations

import shutil
import io
import hashlib
import json
import plistlib
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts import release
from scripts import fetch_steam_provider
from scripts import root_archive
from scripts import stock_ql_maps
from scripts import verify_release_layout
from scripts import windows_pe


def make_pe(*, dll: bool, imports: tuple[str, ...] = ()) -> bytes:
    binary = bytearray(0x600)
    pe_offset = 0x80
    optional_offset = pe_offset + 24
    optional_size = 224
    section_offset = optional_offset + optional_size
    raw_offset = 0x200
    import_rva = 0x1000 if imports else 0
    import_size = (len(imports) + 1) * 20 if imports else 0

    binary[0:2] = b"MZ"
    struct.pack_into("<I", binary, 60, pe_offset)
    binary[pe_offset : pe_offset + 4] = b"PE\0\0"
    struct.pack_into(
        "<HHIIIHH",
        binary,
        pe_offset + 4,
        windows_pe.PE_I386,
        1,
        0,
        0,
        0,
        optional_size,
        0x2102 if dll else 0x0102,
    )
    struct.pack_into("<H", binary, optional_offset, 0x10B)
    struct.pack_into("<I", binary, optional_offset + 92, 16)
    struct.pack_into("<II", binary, optional_offset + 104, import_rva, import_size)
    binary[section_offset : section_offset + 8] = b".rdata\0\0"
    struct.pack_into("<IIII", binary, section_offset + 8, 0x400, 0x1000, 0x400, raw_offset)

    string_offset = raw_offset + 0x200
    for index, dependency in enumerate(imports):
        encoded = dependency.encode("ascii") + b"\0"
        name_rva = 0x1000 + (string_offset - raw_offset)
        struct.pack_into("<IIIII", binary, raw_offset + index * 20, 0, 0, 0, name_rva, 0)
        binary[string_offset : string_offset + len(encoded)] = encoded
        string_offset += len(encoded)
    return bytes(binary)


def make_elf_i386() -> bytes:
    binary = bytearray(64)
    binary[0:4] = b"\x7fELF"
    binary[4] = 1
    binary[5] = 1
    binary[6] = 1
    struct.pack_into("<H", binary, 16, 3)
    struct.pack_into("<H", binary, 18, 3)
    return bytes(binary)


def make_macho64(cpu_type: int) -> bytes:
    return b"\xcf\xfa\xed\xfe" + cpu_type.to_bytes(4, "little") + bytes(56)


def make_universal2_macho() -> bytes:
    header = bytearray(b"\xca\xfe\xba\xbe" + (2).to_bytes(4, "big"))
    for cpu_type in (release.MACOS_X86_64_CPU, release.MACOS_ARM64_CPU):
        header.extend(cpu_type.to_bytes(4, "big"))
        header.extend(bytes(16))
    return bytes(header)


def make_macos_release_members(
    *,
    minimum_version: str = release.MACOS_MINIMUM_VERSION,
    short_version: str = "0.1.0",
    bundle_version: str = "1",
    renderer_parent: str = f"{release.MACOS_APP_ROOT}/MacOS",
    include_signature: bool = True,
    extra_names: tuple[str, ...] = (),
) -> list[release.ReleaseArchiveMember]:
    macho = make_macho64(release.MACOS_X86_64_CPU)
    info = plistlib.dumps(
        {
            "CFBundleExecutable": "FnQL",
            "CFBundleIdentifier": "org.fnql.fnql",
            "CFBundlePackageType": "APPL",
            "CFBundleShortVersionString": short_version,
            "CFBundleVersion": bundle_version,
            "LSMinimumSystemVersion": minimum_version,
            "NSHighResolutionCapable": True,
            "NSMicrophoneUsageDescription": "Optional in-game voice capture.",
        }
    )
    member = release.ReleaseArchiveMember
    members = [
        member("FnQL.app/Contents/Info.plist", info, 0o644),
        member("FnQL.app/Contents/MacOS/FnQL", macho, 0o755),
        member("FnQL.app/Contents/MacOS/FnQL-pkg.fnz", b"package", 0o644),
        member("FnQL.app/Contents/MacOS/fnql-web.pak", b"web", 0o644),
        member("fnql.ded", macho, 0o755),
        member("fnql-audiozonesc", macho, 0o755),
        *(
            member(f"{renderer_parent}/fnql_{renderer}_x86_64.dylib", macho, 0o755)
            for renderer in release.PUBLIC_RENDERERS
        ),
        *(member(name, macho, 0o755) for name in extra_names),
    ]
    if include_signature:
        members.append(
            member("FnQL.app/Contents/_CodeSignature/CodeResources", b"signature", 0o644)
        )
    return members


def populate_linux_release_stage(stage_root: Path, *, include_server: bool = True) -> None:
    elf = make_elf_i386()
    (stage_root / "fnql").write_bytes(elf)
    if include_server:
        (stage_root / "fnql.ded").write_bytes(elf)
    for renderer in release.PUBLIC_RENDERERS:
        (stage_root / f"fnql_{renderer}_x86.so").write_bytes(elf)
    release.copy_docs(stage_root)
    release.build_fnql_webpak(stage_root)
    release.build_root_archive(stage_root)


def populate_macos_release_stage(stage_root: Path) -> None:
    macos = stage_root / "FnQL.app" / "Contents" / "MacOS"
    macos.mkdir(parents=True)
    macho = make_macho64(release.MACOS_X86_64_CPU)
    (macos / "FnQL").write_bytes(macho)
    (stage_root / "fnql.ded").write_bytes(macho)
    (stage_root / "fnql-audiozonesc").write_bytes(macho)
    for renderer in release.PUBLIC_RENDERERS:
        (macos / f"fnql_{renderer}_x86_64.dylib").write_bytes(macho)
    info = {
        "CFBundleExecutable": "FnQL",
        "CFBundleIdentifier": "org.fnql.fnql",
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": "0.1.0",
        "CFBundleVersion": "1",
        "LSMinimumSystemVersion": release.MACOS_MINIMUM_VERSION,
        "NSHighResolutionCapable": True,
        "NSMicrophoneUsageDescription": "Optional in-game voice capture.",
    }
    (stage_root / "FnQL.app" / "Contents" / "Info.plist").write_bytes(
        plistlib.dumps(info)
    )
    signature = stage_root / "FnQL.app" / "Contents" / "_CodeSignature"
    signature.mkdir()
    (signature / "CodeResources").write_bytes(b"test signature envelope")
    release.copy_docs(stage_root)
    webpak = release.build_fnql_webpak(stage_root)
    package = release.build_root_archive(stage_root)
    (macos / release.FNQL_WEBPAK_NAME).write_bytes(webpak.read_bytes())
    (macos / release.ROOT_ARCHIVE_NAME).write_bytes(package.read_bytes())


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
        self.assertIn("docs/fnql/STEAM_PROVIDER_BINARY_NOTICE.txt", destinations)
        self.assertIn("docs/GLX.md", destinations)
        self.assertIn("docs/RTX.md", destinations)
        self.assertNotIn("docs/fnql/GLX_PROMOTION.md", destinations)
        self.assertNotIn("docs/fnql/GLX_ROLLBACK_PACKAGE.md", destinations)
        self.assertNotIn("docs/fnql/GLX_VISUAL_DOSSIER.md", destinations)

    def test_linux_release_tarball_is_deterministic_and_executable(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage_root = root / "stage"
            stage_root.mkdir()
            populate_linux_release_stage(stage_root)
            first = root / "fnql-linux-x86.tar.gz"
            second = root / "fnql-linux-x86-copy.tar.gz"

            release.write_release_archive(stage_root, first)
            for path in stage_root.rglob("*"):
                if path.is_file():
                    path.touch()
            release.write_release_archive(stage_root, second)

            self.assertEqual(first.read_bytes(), second.read_bytes())
            release.validate_release_archive_contents(first)
            verify_release_layout.verify_release_layout(first)
            members = {member.name: member for member in release.read_release_archive(first)}

        self.assertEqual(members["fnql"].mode, 0o755)
        self.assertEqual(members["fnql.ded"].mode, 0o755)
        self.assertEqual(members[release.FNQL_WEBPAK_NAME].mode, 0o644)
        for renderer in release.PUBLIC_RENDERERS:
            self.assertEqual(members[f"fnql_{renderer}_x86.so"].mode, 0o755)

    def test_linux_release_tarball_requires_client_server_and_renderers(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage_root = root / "stage"
            stage_root.mkdir()
            populate_linux_release_stage(stage_root, include_server=False)
            archive_path = root / "fnql-linux-x86.tar.gz"
            release.write_release_archive(stage_root, archive_path)

            with self.assertRaisesRegex(ValueError, "missing required Linux executables"):
                release.validate_release_archive_contents(archive_path)

    def test_linux_release_tarball_accepts_exact_root_renderer_names(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage_root = root / "stage"
            stage_root.mkdir()
            populate_linux_release_stage(stage_root)
            archive_path = root / "fnql-linux-x86.tar.gz"

            release.write_release_archive(stage_root, archive_path)
            release.validate_release_archive_contents(archive_path)

            archived_names = {
                member.name for member in release.read_release_archive(archive_path)
            }

        self.assertTrue(
            set(release.LINUX_RELEASE_RENDERER_MODULES).issubset(archived_names)
        )

    def test_linux_release_tarball_rejects_nested_renderer_modules(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage_root = root / "stage"
            stage_root.mkdir()
            populate_linux_release_stage(stage_root)
            nested = stage_root / "nested"
            nested.mkdir()
            for name in release.LINUX_RELEASE_RENDERER_MODULES:
                (stage_root / name).rename(nested / name)
            archive_path = root / "fnql-linux-x86.tar.gz"

            release.write_release_archive(stage_root, archive_path)

            with self.assertRaisesRegex(
                ValueError,
                "missing required Linux renderer modules",
            ):
                release.validate_release_archive_contents(archive_path)

    def test_linux_release_tarball_rejects_misnamed_renderer_modules(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage_root = root / "stage"
            stage_root.mkdir()
            populate_linux_release_stage(stage_root)
            for name in release.LINUX_RELEASE_RENDERER_MODULES:
                source = stage_root / name
                source.rename(stage_root / name.replace("_x86.so", "_i386.so"))
            archive_path = root / "fnql-linux-x86.tar.gz"

            release.write_release_archive(stage_root, archive_path)

            with self.assertRaisesRegex(
                ValueError,
                "missing required Linux renderer modules",
            ):
                release.validate_release_archive_contents(archive_path)

    def test_release_zip_writer_normalizes_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage_root = root / "stage"
            stage_root.mkdir()
            (stage_root / "fnql.exe").write_bytes(b"not-a-real-binary")
            first = root / "fnql-windows-x86.zip"
            second = root / "fnql-windows-x86-copy.zip"

            release.write_release_archive(stage_root, first)
            (stage_root / "fnql.exe").touch()
            release.write_release_archive(stage_root, second)

            self.assertEqual(first.read_bytes(), second.read_bytes())
            with zipfile.ZipFile(first) as archive:
                info = archive.getinfo("fnql.exe")
                self.assertEqual(info.date_time, release.RELEASE_ZIP_DATETIME)
                self.assertEqual((info.external_attr >> 16) & 0o7777, 0o644)

    def test_stock_ql_sidecars_are_packaged_in_root_archive(self) -> None:
        audio_destinations = {
            destination.as_posix()
            for _source, destination in root_archive.DEFAULT_AUDIO_ZONE_ASSETS
        }
        fog_destinations = {
            destination.as_posix()
            for _source, destination in root_archive.DEFAULT_GLOBAL_FOG_ASSETS
        }
        required_destinations = {
            destination.as_posix()
            for _source, destination in root_archive.DEFAULT_ROOT_ARCHIVE_REQUIRED_ASSETS
        }
        sources = {
            source.relative_to(ROOT).as_posix()
            for source, _destination in root_archive.DEFAULT_AUDIO_ZONE_ASSETS
        }

        self.assertEqual(len(stock_ql_maps.STOCK_QL_MAPS), 149)
        self.assertEqual(len(audio_destinations), 149)
        self.assertEqual(len(fog_destinations), 149)
        self.assertEqual(len(required_destinations), 301)
        self.assertIn("pkg/baseq3/maps/campgrounds.azb", sources)
        self.assertIn("baseq3/maps/campgrounds.azb", audio_destinations)
        self.assertIn("baseq3/maps/campgrounds.fog", fog_destinations)
        self.assertIn("baseq3/maps/bloodrun.azb", audio_destinations)
        self.assertIn("baseq3/maps/siberia.fog", fog_destinations)
        self.assertNotIn("baseq3/maps/q3dm1.azb", audio_destinations)
        self.assertIn("baseq3/sound/fnql-weapon-sounds.sndshd", required_destinations)
        self.assertIn("missionpack/sound/fnql-weapon-sounds.sndshd", required_destinations)
        self.assertIn("baseq3/scripts/fnql.shader", required_destinations)
        self.assertNotIn("baseq3/maps/test_bigbox.azb", audio_destinations)

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
        self.assertIn("files('scripts/stock_ql_maps.py')", meson_build)
        self.assertIn("map_name + '.azb'", meson_build)
        self.assertIn("map_name + '.fog'", meson_build)
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
            (stage_root / "fnql_steam.dll").write_bytes(make_pe(dll=True))
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
        self.assertIn("baseq3/maps/campgrounds.azb", root_archive_names)
        self.assertIn("baseq3/maps/campgrounds.fog", root_archive_names)
        self.assertIn("baseq3/maps/bloodrun.azb", root_archive_names)
        self.assertIn("baseq3/maps/bloodrun.fog", root_archive_names)
        self.assertNotIn("baseq3/maps/q3dm1.azb", root_archive_names)
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
            (root / "fnql_steam.dll").write_bytes(make_pe(dll=True))
            for renderer in ("glx", "vk", "rtx"):
                (root / f"fnql_{renderer}_x86.dll").write_text(
                    renderer, encoding="utf-8"
                )

            release.validate_stage_tree(root)

    def test_windows_release_requires_the_closed_provider_binary(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "fnql.exe").write_text("binary", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "missing fnql_steam.dll"):
                release.validate_stage_tree(root)

    def test_windows_release_forbids_valve_redistributable(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "fnql.exe").write_text("binary", encoding="utf-8")
            (root / "fnql_steam.dll").write_bytes(make_pe(dll=True))
            (root / "steam_api.dll").write_bytes(make_pe(dll=True))

            with self.assertRaisesRegex(ValueError, "must not redistribute"):
                release.validate_stage_tree(root)

    def test_windows_release_rejects_unshipped_runtime_imports(self) -> None:
        for runtime in ("libgcc_s_dw2-1.dll", "z-1.dll"):
            with self.subTest(runtime=runtime), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / "fnql.exe").write_bytes(
                    make_pe(dll=False, imports=("KERNEL32.dll", runtime))
                )
                (root / "fnql_steam.dll").write_bytes(make_pe(dll=True))

                with self.assertRaisesRegex(ValueError, "unshipped Windows runtime DLLs"):
                    release.validate_stage_tree(root)

    def test_pinned_provider_stager_checks_digest_and_i386_dll_identity(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "provider.dll"
            output = root / "out" / "fnql_steam.dll"
            manifest = root / "provider.json"
            provider = make_pe(dll=True, imports=("KERNEL32.dll",))
            source.write_bytes(provider)
            manifest.write_text(
                json.dumps(
                    {
                        "version": "test",
                        "tag": "test",
                        "asset": "fnql_steam.dll",
                        "url": "https://invalid.example/fnql_steam.dll",
                        "sha256": hashlib.sha256(provider).hexdigest(),
                        "pe_machine": "i386",
                    }
                ),
                encoding="utf-8",
            )

            digest = fetch_steam_provider.stage_provider(
                output, manifest_path=manifest, source=source
            )

            self.assertEqual(digest, hashlib.sha256(provider).hexdigest())
            self.assertEqual(output.read_bytes(), provider)

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

    def test_macos_policy_accepts_only_the_declared_native_architecture(self) -> None:
        x86 = make_macho64(release.MACOS_X86_64_CPU)
        arm = make_macho64(release.MACOS_ARM64_CPU)

        release.validate_release_binary_stream(
            "FnQL",
            io.BytesIO(x86),
            platform="macos",
            expected_macos_arch="x86_64",
        )
        with self.assertRaisesRegex(ValueError, "architecture mismatch"):
            release.validate_release_binary_stream(
                "FnQL",
                io.BytesIO(arm),
                platform="macos",
                expected_macos_arch="x86_64",
            )

        release.validate_release_binary_stream(
            "FnQL",
            io.BytesIO(make_universal2_macho()),
            platform="macos",
            expected_macos_arch="universal2",
        )
        with self.assertRaisesRegex(ValueError, "Universal 2 artifact contains a thin binary"):
            release.validate_release_binary_stream(
                "FnQL",
                io.BytesIO(x86),
                platform="macos",
                expected_macos_arch="universal2",
            )

    def test_macos_zip_preserves_code_modes_and_validates_the_app(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage = root / "stage"
            macos = stage / "FnQL.app" / "Contents" / "MacOS"
            macos.mkdir(parents=True)
            macho = make_macho64(release.MACOS_X86_64_CPU)
            (macos / "FnQL").write_bytes(macho)
            (stage / "fnql.ded").write_bytes(macho)
            (stage / "fnql-audiozonesc").write_bytes(macho)
            for renderer in release.PUBLIC_RENDERERS:
                (macos / f"fnql_{renderer}_x86_64.dylib").write_bytes(macho)
            info = {
                "CFBundleExecutable": "FnQL",
                "CFBundleIdentifier": "org.fnql.fnql",
                "CFBundlePackageType": "APPL",
                "CFBundleShortVersionString": "0.1.0",
                "CFBundleVersion": "1",
                "LSMinimumSystemVersion": "11.0",
                "NSHighResolutionCapable": True,
                "NSMicrophoneUsageDescription": "Optional in-game voice capture.",
            }
            (stage / "FnQL.app" / "Contents" / "Info.plist").write_bytes(
                plistlib.dumps(info)
            )
            signature = stage / "FnQL.app" / "Contents" / "_CodeSignature"
            signature.mkdir()
            (signature / "CodeResources").write_bytes(b"test signature envelope")
            release.copy_docs(stage)
            webpak = release.build_fnql_webpak(stage)
            package = release.build_root_archive(stage)
            (macos / release.FNQL_WEBPAK_NAME).write_bytes(webpak.read_bytes())
            (macos / release.ROOT_ARCHIVE_NAME).write_bytes(package.read_bytes())
            archive = root / "fnql-test-macos-x86_64.zip"

            release.write_release_archive(stage, archive)
            release.validate_release_archive_contents(archive)
            members = {member.name: member for member in release.read_release_archive(archive)}

        self.assertEqual(members["FnQL.app/Contents/MacOS/FnQL"].mode, 0o755)
        self.assertEqual(
            members[f"FnQL.app/Contents/MacOS/{release.ROOT_ARCHIVE_NAME}"].mode,
            0o644,
        )

    def test_prebuilt_macos_payload_is_published_byte_for_byte(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage = root / "stage"
            artifact = root / "macos-x86_64"
            artifact.mkdir()
            populate_macos_release_stage(stage)
            payload = artifact / release.MACOS_PREBUILT_PAYLOAD_NAME
            release.write_release_archive(stage, payload)
            expected = payload.read_bytes()
            destination = root / "fnql-test-macos-x86_64.zip"

            self.assertTrue(
                release.publish_prebuilt_macos_payload(artifact, destination)
            )
            self.assertEqual(destination.read_bytes(), expected)

            (artifact / "unexpected.txt").write_text("reject", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "must contain only"):
                release.publish_prebuilt_macos_payload(
                    artifact,
                    root / "second.zip",
                )

    def test_macos_policy_requires_supported_minimum_and_signed_app(self) -> None:
        with self.assertRaisesRegex(ValueError, "LSMinimumSystemVersion"):
            release.validate_macos_distribution_files(
                make_macos_release_members(minimum_version="12.0"),
                archive_name="macos-x86_64.zip",
            )

        with self.assertRaisesRegex(ValueError, "_CodeSignature/CodeResources"):
            release.validate_macos_distribution_files(
                make_macos_release_members(include_signature=False),
                archive_name="macos-x86_64.zip",
            )
        release.validate_macos_distribution_files(
            make_macos_release_members(include_signature=False),
            archive_name="macos-x86_64.zip",
            require_signature=False,
        )

        with self.assertRaisesRegex(ValueError, "CFBundleShortVersionString"):
            release.validate_macos_distribution_files(
                make_macos_release_members(short_version="0.1.0.42"),
                archive_name="macos-x86_64.zip",
            )
        with self.assertRaisesRegex(ValueError, "CFBundleVersion"):
            release.validate_macos_distribution_files(
                make_macos_release_members(bundle_version="0.1.0.42"),
                archive_name="macos-x86_64.zip",
            )

    def test_macos_policy_requires_app_local_renderer_modules(self) -> None:
        with self.assertRaisesRegex(ValueError, "renderer modules outside"):
            release.validate_macos_distribution_files(
                make_macos_release_members(renderer_parent="renderers"),
                archive_name="macos-x86_64.zip",
            )

    def test_macos_policy_forbids_valve_steam_runtime(self) -> None:
        with self.assertRaisesRegex(ValueError, "must not redistribute.*libsteam_api.dylib"):
            release.validate_macos_distribution_files(
                make_macos_release_members(
                    extra_names=("FnQL.app/Contents/MacOS/libsteam_api.dylib",)
                ),
                archive_name="macos-x86_64.zip",
            )

    def test_cmake_sets_macos_deployment_target_before_project(self) -> None:
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        before_project = cmake[: cmake.index("\nPROJECT(")]
        self.assertIn(
            "IF(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)",
            before_project,
        )
        self.assertIn(
            'SET(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING',
            before_project,
        )
        self.assertNotIn(
            "IF(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)",
            before_project,
        )
        self.assertIn("FNQL_MACOS_SHORT_VERSION", cmake)
        self.assertIn("FNQL_MACOS_BUNDLE_VERSION", cmake)
        self.assertNotIn(
            'MACOSX_BUNDLE_SHORT_VERSION_STRING "${FNQL_VERSION_STRING}"',
            cmake,
        )

    def test_release_workflow_keeps_retail_x86_and_adds_native_macos(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(
            encoding="utf-8"
        )

        self.assertIn("name: windows-mingw-x86", workflow)
        self.assertIn("name: windows-msvc-x86", workflow)
        self.assertIn("name: linux-x86", workflow)
        self.assertIn("ARCH=x86 COMPILE_ARCH=x86", workflow)
        self.assertEqual(
            workflow.count("verify_release_layout.py bin/FnQL-pkg.fnz"),
            3,
        )
        self.assertEqual(workflow.count("--skip-subprojects"), 3)
        self.assertEqual(workflow.count("fetch_steam_provider.py --output bin/fnql_steam.dll"), 2)
        self.assertEqual(workflow.count("check_windows_runtime_deps.py --require-pe bin"), 2)
        self.assertIn("-Dc_link_args=-static", workflow)
        self.assertIn("-Dcpp_link_args=-static", workflow)
        self.assertEqual(workflow.count("-Dzlib:default_library=static"), 3)
        self.assertIn("-Db_vscrt=static_from_buildtype", workflow)
        self.assertEqual(workflow.count("--wrap-mode=forcefallback"), 3)
        meson = (ROOT / "meson.build").read_text(encoding="utf-8")
        self.assertIn("'zstd=disabled'", meson)
        self.assertIn("'brotli=disabled'", meson)
        self.assertIn("'ssh=disabled'", meson)
        self.assertNotIn("verify_release_layout.py bin\n", workflow)
        self.assertNotIn("FNQ3_", workflow)
        self.assertNotIn("docs/fnquake3/", workflow)
        self.assertIn('gh release create "${FNQL_RELEASE_TAG}"', workflow)
        self.assertNotIn("ubuntu-arm64:", workflow)
        self.assertIn("  macos:", workflow)
        self.assertIn("macos-15-intel", workflow)
        self.assertIn("runner: macos-15", workflow)
        self.assertIn("name: macos-${{ matrix.artifact_arch }}", workflow)
        self.assertIn("name: Stage without project signing", workflow)
        self.assertNotIn("--sign-identity -", workflow)
        self.assertIn("if: github.event_name == 'workflow_dispatch'", workflow)
        self.assertIn("MACOS_DEVELOPER_ID_P12_BASE64", workflow)
        self.assertIn("  macos-release-sign:", workflow)
        self.assertIn("needs: [macos]", workflow)
        self.assertIn("name: unsigned-apple-${{ matrix.artifact_arch }}", workflow)
        self.assertIn(
            "needs: [prepare, windows-msys32, windows-msvc, source-validation, macos-release-sign, ubuntu-x86]",
            workflow,
        )
        self.assertIn("name: macos-x86_64", workflow)
        self.assertIn("name: macos-aarch64", workflow)
        self.assertIn("ditto -c -k --sequesterRsrc bin macos-payload.zip", workflow)
        self.assertIn("path: macos-payload.zip", workflow)
        self.assertNotIn("arch: [x86, x86_64]", workflow)
        self.assertNotIn("arch: [arm64, x86, x64]", workflow)

    def test_release_cli_parser_rejects_negative_build_numbers(self) -> None:
        with self.assertRaisesRegex(Exception, "non-negative"):
            release.non_negative_int("-1")


if __name__ == "__main__":
    unittest.main()
