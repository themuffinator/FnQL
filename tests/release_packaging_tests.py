from __future__ import annotations

import argparse
import shutil
import io
import hashlib
import json
import plistlib
import re
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts import release
from scripts import fetch_steam_provider
from scripts import root_archive
from scripts import stock_ql_maps
from scripts import verify_release_layout
from scripts import windows_pe
from scripts import check_windows_runtime_deps


PE_AMD64 = 0x8664


def make_pe(
    *,
    dll: bool,
    imports: tuple[str, ...] = (),
    machine: int = windows_pe.PE_I386,
) -> bytes:
    """Build a minimal PE image: PE32 for i386 (the default), PE32+ for AMD64."""
    pe32_plus = machine == PE_AMD64
    binary = bytearray(0x600)
    pe_offset = 0x80
    optional_offset = pe_offset + 24
    optional_size = 240 if pe32_plus else 224
    directory_offset = optional_offset + (112 if pe32_plus else 96)
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
        machine,
        1,
        0,
        0,
        0,
        optional_size,
        (0x2022 if dll else 0x0022) if pe32_plus else (0x2102 if dll else 0x0102),
    )
    struct.pack_into("<H", binary, optional_offset, 0x20B if pe32_plus else 0x10B)
    struct.pack_into("<I", binary, directory_offset - 4, 16)
    struct.pack_into("<II", binary, directory_offset + 8, import_rva, import_size)
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



def make_pe_with_functions(
    imports: dict[str, tuple[str | int, ...]],
    *,
    lookup_table: bool = True,
    machine: int = windows_pe.PE_I386,
) -> bytes:
    """Build a PE (i386 by default, AMD64 PE32+ on request) with import thunk tables.

    String entries are imported by name; integers are imported by ordinal.
    With lookup_table=False only FirstThunk is populated, as some linkers emit.
    """
    pe32_plus = machine == PE_AMD64
    thunk_format = "<Q" if pe32_plus else "<I"
    thunk_size = struct.calcsize(thunk_format)
    ordinal_flag = (
        windows_pe.IMPORT_ORDINAL_FLAG_64 if pe32_plus else windows_pe.IMPORT_ORDINAL_FLAG_32
    )
    import_directory = 0x80 + 24 + (120 if pe32_plus else 104)
    binary = bytearray(make_pe(dll=False, machine=machine))
    raw_offset = 0x200
    section_rva = 0x1000

    def rva(offset: int) -> int:
        return section_rva + (offset - raw_offset)

    descriptor_offset = raw_offset
    cursor = raw_offset + (len(imports) + 1) * 20
    for dll, functions in imports.items():
        thunk_offset = cursor
        cursor += (len(functions) + 1) * thunk_size
        thunks: list[int] = []
        for function in functions:
            if isinstance(function, int):
                thunks.append(ordinal_flag | function)
                continue
            encoded = function.encode("ascii") + b"\0"
            thunks.append(rva(cursor))
            struct.pack_into("<H", binary, cursor, 0)
            binary[cursor + 2 : cursor + 2 + len(encoded)] = encoded
            cursor += 2 + len(encoded)
        for index, thunk in enumerate(thunks + [0]):
            struct.pack_into(thunk_format, binary, thunk_offset + index * thunk_size, thunk)
        encoded_dll = dll.encode("ascii") + b"\0"
        dll_rva = rva(cursor)
        binary[cursor : cursor + len(encoded_dll)] = encoded_dll
        cursor += len(encoded_dll)
        struct.pack_into(
            "<IIIII",
            binary,
            descriptor_offset,
            rva(thunk_offset) if lookup_table else 0,
            0,
            0,
            dll_rva,
            0 if lookup_table else rva(thunk_offset),
        )
        descriptor_offset += 20
    assert cursor <= raw_offset + 0x400, "fixture exceeds its section"
    struct.pack_into("<II", binary, import_directory, section_rva, (len(imports) + 1) * 20)
    return bytes(binary)


def make_pe_x86_64(*, dll: bool, imports: tuple[str, ...] = ()) -> bytes:
    return make_pe(dll=dll, imports=imports, machine=PE_AMD64)


def make_elf_i386() -> bytes:
    binary = bytearray(64)
    binary[0:4] = b"\x7fELF"
    binary[4] = 1
    binary[5] = 1
    binary[6] = 1
    struct.pack_into("<H", binary, 16, 3)
    struct.pack_into("<H", binary, 18, 3)
    return bytes(binary)


def make_elf_x86_64() -> bytes:
    binary = bytearray(64)
    binary[0:4] = b"\x7fELF"
    binary[4] = 2
    binary[5] = 1
    binary[6] = 1
    struct.pack_into("<H", binary, 16, 3)
    struct.pack_into("<H", binary, 18, 62)
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


def populate_linux_release_stage(
    stage_root: Path,
    *,
    include_server: bool = True,
    arch: str = "x86",
) -> None:
    if arch == "x86":
        elf = make_elf_i386()
        client, server = "fnql", "fnql.ded"
        renderer_arch = "x86"
    else:
        elf = make_elf_x86_64()
        client, server = release.LINUX_X86_64_RELEASE_EXECUTABLES
        renderer_arch = "x86_64"
    (stage_root / client).write_bytes(elf)
    if include_server:
        (stage_root / server).write_bytes(elf)
    for renderer in release.PUBLIC_RENDERERS:
        (stage_root / f"fnql_{renderer}_{renderer_arch}.so").write_bytes(elf)
    release.copy_docs(stage_root)
    release.build_fnql_webpak(stage_root)
    release.build_root_archive(stage_root)


def populate_windows_x86_64_release_stage(stage_root: Path) -> None:
    """Lay out what the Meson x64 install produces: no fnql_steam.dll."""
    for name in release.WINDOWS_X86_64_RELEASE_EXECUTABLES:
        (stage_root / name).write_bytes(make_pe_x86_64(dll=False, imports=("KERNEL32.dll",)))
    (stage_root / "fnql-audiozonesc.exe").write_bytes(make_pe_x86_64(dll=False))
    for name in release.WINDOWS_X86_64_RELEASE_RENDERER_MODULES:
        (stage_root / name).write_bytes(make_pe_x86_64(dll=True))
    release.copy_docs(stage_root)
    release.build_fnql_webpak(stage_root)
    release.build_root_archive(stage_root)


def workflow_jobs(workflow: str) -> dict[str, str]:
    """Split a GitHub workflow into its top-level jobs (two-space-indented keys)."""
    body = workflow.split("\njobs:\n", 1)[1]
    jobs: dict[str, str] = {}
    current: str | None = None
    lines: list[str] = []
    for line in body.splitlines(keepends=True):
        match = re.match(r"^  ([A-Za-z0-9_-]+):\s*$", line)
        if match:
            if current is not None:
                jobs[current] = "".join(lines)
            current = match.group(1)
            lines = []
        elif current is not None:
            lines.append(line)
    if current is not None:
        jobs[current] = "".join(lines)
    return jobs


def job_needs(job: str) -> list[str]:
    match = re.search(r"^    needs: \[([^\]]*)\]", job, re.MULTILINE)
    if match is None:
        raise AssertionError("job has no needs list")
    return [entry.strip() for entry in match.group(1).split(",") if entry.strip()]


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
            (source / "cpma" / "vm").mkdir(parents=True)
            (source / "cpma" / "vm" / "cgame.qvm").write_text(
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
                "cpma/vm/cgame.qvm",
                "fnql.x86",
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

    def test_release_proof_runtime_identities_come_from_exact_archive_members(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage_root = root / "stage"
            stage_root.mkdir()
            populate_linux_release_stage(stage_root)
            archive_path = root / "fnql-linux-x86.tar.gz"
            release.write_release_archive(stage_root, archive_path)

            identities = release.release_archive_runtime_identities(
                archive_path,
                "linux-x86",
            )

            self.assertEqual(identities["client"]["member"], "fnql")
            self.assertEqual(identities["glxModule"]["member"], "fnql_glx_x86.so")
            self.assertEqual(
                identities["client"]["sha256"],
                hashlib.sha256((stage_root / "fnql").read_bytes()).hexdigest(),
            )
            self.assertEqual(
                identities["glxModule"]["sha256"],
                hashlib.sha256((stage_root / "fnql_glx_x86.so").read_bytes()).hexdigest(),
            )

            windows_stage = root / "windows-stage"
            windows_stage.mkdir()
            windows_client = make_pe(dll=False)
            windows_glx = make_pe(dll=True)
            (windows_stage / "fnql.exe").write_bytes(windows_client)
            (windows_stage / "fnql_glx_x86.dll").write_bytes(windows_glx)
            windows_archive = root / "fnql-windows-x86.zip"
            release.write_release_archive(windows_stage, windows_archive)

            for variant in ("windows-mingw-x86", "windows-msvc-x86"):
                windows_identities = release.release_archive_runtime_identities(
                    windows_archive,
                    variant,
                )
                self.assertEqual(windows_identities["client"]["member"], "fnql.exe")
                self.assertEqual(
                    windows_identities["glxModule"]["member"],
                    "fnql_glx_x86.dll",
                )
                self.assertEqual(
                    windows_identities["client"]["sha256"],
                    hashlib.sha256(windows_client).hexdigest(),
                )
                self.assertEqual(
                    windows_identities["glxModule"]["sha256"],
                    hashlib.sha256(windows_glx).hexdigest(),
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
        self.assertEqual(len(required_destinations), 300)
        self.assertIn("pkg/baseq3/maps/campgrounds.azb", sources)
        self.assertIn("baseq3/maps/campgrounds.azb", audio_destinations)
        self.assertIn("baseq3/maps/campgrounds.fog", fog_destinations)
        self.assertIn("baseq3/maps/bloodrun.azb", audio_destinations)
        self.assertIn("baseq3/maps/siberia.fog", fog_destinations)
        self.assertNotIn("baseq3/maps/q3dm1.azb", audio_destinations)
        self.assertIn("baseq3/sound/fnql-weapon-sounds.sndshd", required_destinations)
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
        self.assertNotIn("pkg/missionpack/", meson_build)

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
            (package_root / "cpma" / "sound" / "custom.sndshd").parent.mkdir(parents=True)
            (package_root / "cpma" / "sound" / "custom.sndshd").write_text(
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
        self.assertIn("cpma/sound/custom.sndshd", names)
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
            (stage_root / "cpma" / "vm").mkdir(parents=True)
            (stage_root / "cpma" / "vm" / "cgame.qvm").write_text(
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
        self.assertIn("cpma/vm/cgame.qvm", names)
        self.assertNotIn("baseq3/maps/q3dm1.azb", names)
        self.assertIn("baseq3/maps/campgrounds.azb", root_archive_names)
        self.assertIn("baseq3/maps/campgrounds.fog", root_archive_names)
        self.assertIn("baseq3/maps/bloodrun.azb", root_archive_names)
        self.assertIn("baseq3/maps/bloodrun.fog", root_archive_names)
        self.assertNotIn("baseq3/maps/q3dm1.azb", root_archive_names)
        self.assertIn("baseq3/sound/fnql-weapon-sounds.sndshd", root_archive_names)
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
        for runtime in ("libgcc_s_dw2-1.dll", "libgcc_s_seh-1.dll", "z-1.dll"):
            with self.subTest(runtime=runtime), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / "fnql.exe").write_bytes(
                    make_pe(dll=False, imports=("KERNEL32.dll", runtime))
                )
                (root / "fnql_steam.dll").write_bytes(make_pe(dll=True))

                with self.assertRaisesRegex(ValueError, "unshipped Windows runtime DLLs"):
                    release.validate_stage_tree(root)

    def test_pe_named_imports_skip_ordinals_and_fold_dll_names(self) -> None:
        for lookup_table in (True, False):
            with self.subTest(lookup_table=lookup_table):
                binary = make_pe_with_functions(
                    {
                        "KERNEL32.dll": ("GetTickCount",),
                        "WINMM.dll": ("joyGetNumDevs", 42, "joyGetPosEx"),
                    },
                    lookup_table=lookup_table,
                )

                functions = windows_pe.parse_pe_imported_functions(binary, name="fixture")

                self.assertEqual(
                    functions,
                    {
                        "kernel32.dll": ("GetTickCount",),
                        "winmm.dll": ("joyGetNumDevs", "joyGetPosEx"),
                    },
                )
                info = windows_pe.parse_pe(binary, name="fixture")
                self.assertEqual(info.imports, ("KERNEL32.dll", "WINMM.dll"))

    def test_pe_named_imports_tolerate_descriptors_without_thunks(self) -> None:
        binary = make_pe(dll=False, imports=("KERNEL32.dll",))

        self.assertEqual(
            windows_pe.parse_pe_imported_functions(binary, name="fixture"),
            {"kernel32.dll": ()},
        )
        self.assertIsNone(windows_pe.parse_pe_imported_functions(b"not a pe", name="text"))

    def test_pe_named_imports_reject_unterminated_thunk_tables(self) -> None:
        binary = bytearray(make_pe_with_functions({"WINMM.dll": ("joyGetPosEx",)}))
        (thunk_rva,) = struct.unpack_from("<I", binary, 0x200)
        thunk_offset = 0x200 + (thunk_rva - 0x1000)
        # Replace the terminator and everything after it with ordinal thunks
        # until the end of the image, so no terminating zero is ever found.
        for offset in range(thunk_offset, len(binary) - 3, 4):
            struct.pack_into("<I", binary, offset, windows_pe.IMPORT_ORDINAL_FLAG_32 | 1)

        with self.assertRaises(ValueError):
            windows_pe.parse_pe_imported_functions(bytes(binary), name="fixture")

    def test_required_import_gate_detects_compiled_out_features(self) -> None:
        requirement = check_windows_runtime_deps.parse_required_import(
            "fnql.exe=winmm.dll!joyGetPosEx"
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            client = root / "fnql.exe"
            client.write_bytes(make_pe_with_functions({"WINMM.dll": ("joyGetPosEx",)}))
            files = [client]

            check_windows_runtime_deps.check_required_imports(root, files, [requirement])
            check_windows_runtime_deps.check_required_imports(
                client, files, [requirement]
            )

            client.write_bytes(make_pe_with_functions({"WINMM.dll": ("timeGetTime",)}))
            with self.assertRaisesRegex(ValueError, "does not import joyGetPosEx"):
                check_windows_runtime_deps.check_required_imports(root, files, [requirement])

            with self.assertRaisesRegex(ValueError, "was not found"):
                check_windows_runtime_deps.check_required_imports(root, [], [requirement])

            client.write_text("not a pe", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "is not a PE image"):
                check_windows_runtime_deps.check_required_imports(root, files, [requirement])

    def test_required_import_specs_are_validated(self) -> None:
        parsed = check_windows_runtime_deps.parse_required_import(
            r"sub\fnql.exe = WINMM.dll!joyGetPosEx"
        )
        self.assertEqual(parsed.path, "sub/fnql.exe")
        self.assertEqual(parsed.dll, "WINMM.dll")
        self.assertEqual(parsed.function, "joyGetPosEx")
        for malformed in ("fnql.exe", "fnql.exe=winmm.dll", "=winmm.dll!f", "fnql.exe=!f", "a=b!"):
            with self.subTest(malformed=malformed), self.assertRaises(
                argparse.ArgumentTypeError
            ):
                check_windows_runtime_deps.parse_required_import(malformed)

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

    def test_release_artifact_dirs_require_supported_windows_linux_set(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in release.PUBLISHED_RELEASE_ARTIFACTS:
                (root / name).mkdir()

            self.assertEqual(
                [path.name for path in release.release_artifact_dirs(root)],
                sorted(release.PUBLISHED_RELEASE_ARTIFACTS),
            )

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in release.PUBLISHED_RELEASE_ARTIFACTS:
                (root / name).mkdir()
            (root / "macos-x86_64").mkdir()

            with self.assertRaisesRegex(
                ValueError,
                "supported Windows/Linux set.*unsupported: macos-x86_64",
            ):
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

    def test_unsigned_prebuilt_macos_payload_requires_explicit_policy(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage = root / "stage"
            artifact = root / "macos-x86_64"
            artifact.mkdir()
            populate_macos_release_stage(stage)
            shutil.rmtree(stage / "FnQL.app" / "Contents" / "_CodeSignature")
            payload = artifact / release.MACOS_PREBUILT_PAYLOAD_NAME
            release.write_release_archive(stage, payload)
            destination = root / "fnql-test-macos-x86_64.zip"

            with self.assertRaisesRegex(ValueError, "_CodeSignature/CodeResources"):
                release.publish_prebuilt_macos_payload(artifact, destination)

            self.assertTrue(
                release.publish_prebuilt_macos_payload(
                    artifact,
                    destination,
                    require_macos_signature=False,
                )
            )
            self.assertEqual(destination.read_bytes(), payload.read_bytes())

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
        with self.assertRaisesRegex(ValueError, "must be an unsigned macOS app bundle"):
            release.validate_macos_distribution_files(
                make_macos_release_members(),
                archive_name="macos-x86_64.zip",
                require_signature=False,
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

    def test_release_workflow_publishes_only_supported_windows_and_linux(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(
            encoding="utf-8"
        )

        jobs = workflow_jobs(workflow)
        x86_windows_jobs = ("windows-msys32", "windows-msvc")
        x86_64_windows_jobs = ("windows-msys64", "windows-msvc-x64")
        x86_jobs = (*x86_windows_jobs, "ubuntu-x86")
        x86_64_jobs = (*x86_64_windows_jobs, "ubuntu-x86_64")

        self.assertIn("name: windows-mingw-x86\n", workflow)
        self.assertIn("name: windows-msvc-x86\n", workflow)
        self.assertIn("name: linux-x86\n", workflow)
        self.assertIn("name: windows-mingw-x86_64\n", workflow)
        self.assertIn("name: windows-msvc-x86_64\n", workflow)
        self.assertIn("name: linux-x86_64\n", workflow)
        self.assertIn("ARCH=x86 COMPILE_ARCH=x86 ", jobs["ubuntu-x86"])
        self.assertIn("ARCH=x86_64 COMPILE_ARCH=x86_64 ", jobs["ubuntu-x86_64"])
        for job in (*x86_jobs, *x86_64_jobs):
            with self.subTest(job=job):
                self.assertEqual(
                    jobs[job].count("verify_release_layout.py bin/FnQL-pkg.fnz"), 1
                )
        self.assertEqual(
            workflow.count("verify_release_layout.py bin/FnQL-pkg.fnz"),
            6,
        )
        for job in (*x86_windows_jobs, *x86_64_windows_jobs):
            with self.subTest(job=job):
                self.assertEqual(jobs[job].count("--skip-subprojects"), 1)
                self.assertEqual(jobs[job].count("-Dzlib:default_library=static"), 1)
                self.assertEqual(jobs[job].count("--wrap-mode=forcefallback"), 1)
        # The pinned Steam provider is a Win32 build: only the x86 Windows
        # jobs stage it, and the x86_64 jobs never fetch it.
        self.assertEqual(workflow.count("fetch_steam_provider.py --output bin/fnql_steam.dll"), 2)
        for job in x86_windows_jobs:
            self.assertIn("fetch_steam_provider.py --output bin/fnql_steam.dll", jobs[job])
        for job in x86_64_jobs:
            self.assertNotIn("fetch_steam_provider", jobs[job])
        # Every Windows job also requires the WinMM controller backend, so a
        # build that compiles USE_JOYSTICK out fails before packaging.
        self.assertEqual(
            workflow.count(
                "check_windows_runtime_deps.py --require-pe "
                '--require-import "fnql.exe=winmm.dll!joyGetPosEx" bin'
            ),
            2,
        )
        for job in x86_windows_jobs:
            self.assertIn('--require-import "fnql.exe=winmm.dll!joyGetPosEx" bin', jobs[job])
        for job in x86_64_windows_jobs:
            self.assertIn(
                "check_windows_runtime_deps.py --require-pe "
                '--require-import "fnql.x64.exe=winmm.dll!joyGetPosEx" bin',
                jobs[job],
            )
        self.assertIn("-Dc_link_args=-static", jobs["windows-msys32"])
        self.assertIn("-Dcpp_link_args=-static", jobs["windows-msys32"])
        self.assertIn("-Dc_link_args=-static", jobs["windows-msys64"])
        self.assertIn("-Dcpp_link_args=-static", jobs["windows-msys64"])
        self.assertEqual(workflow.count("-Dzlib:default_library=static"), 4)
        self.assertIn("-Db_vscrt=static_from_buildtype", jobs["windows-msvc"])
        self.assertIn("-Db_vscrt=static_from_buildtype", jobs["windows-msvc-x64"])
        self.assertEqual(workflow.count("--wrap-mode=forcefallback"), 4)
        meson = (ROOT / "meson.build").read_text(encoding="utf-8")
        self.assertIn("'zstd=disabled'", meson)
        self.assertIn("'brotli=disabled'", meson)
        self.assertIn("'ssh=disabled'", meson)
        self.assertIn("if cc.get_argument_syntax() == 'gcc'", meson)
        self.assertIn("find_program('windres', 'rc', 'llvm-rc'", meson)
        self.assertNotIn("verify_release_layout.py bin\n", workflow)
        self.assertNotIn("FNQ3_", workflow)
        self.assertNotIn("docs/fnquake3/", workflow)
        self.assertIn('gh release create "${FNQL_RELEASE_TAG}"', workflow)
        self.assertNotIn("ubuntu-arm64:", workflow)
        self.assertNotIn("sign_macos:", workflow)
        self.assertNotIn("  macos:", workflow)
        self.assertNotIn("  macos-release-sign:", workflow)
        validation_needs = job_needs(jobs["push-build-validation"])
        self.assertEqual(validation_needs[0], "prepare")
        for job in (
            "windows-msys32",
            "windows-msvc",
            "source-validation",
            "linux-x86-regression",
            "ubuntu-x86",
            "fnql-game-windows",
            "fnql-game-linux",
            "windows-msys64",
            "windows-msvc-x64",
            "ubuntu-x86_64",
            "fnql-game-windows-x86_64",
            "fnql-game-linux-x86_64",
        ):
            self.assertIn(job, validation_needs)
        self.assertEqual(len(validation_needs), len(set(validation_needs)))
        self.assertIn(
            "needs: [prepare, push-build-validation]",
            workflow,
        )
        self.assertIn("needs.push-build-validation.result == 'success'", workflow)
        publish_job = workflow.split("  publish:", 1)[1]
        self.assertNotIn("Download Intel macOS artifact", publish_job)
        self.assertNotIn("Download Apple Silicon macOS artifact", publish_job)
        self.assertNotIn("allow-unsigned-macos", publish_job)
        self.assertNotIn("arch: [x86, x86_64]", workflow)
        self.assertNotIn("arch: [arm64, x86, x64]", workflow)

    def test_release_workflow_runs_complete_regressions(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(
            encoding="utf-8"
        )

        self.assertEqual(
            workflow.count("meson test -C meson/build-x86 --print-errorlogs")
            + workflow.count("meson test -C meson\\build-x86 --print-errorlogs"),
            2,
        )
        self.assertNotIn("meson test -C meson/build-x86 fnql_factory_rotation", workflow)
        self.assertNotIn("meson test -C meson\\build-x86 fnql_factory_rotation", workflow)
        self.assertIn("run: python -m pytest", workflow)
        self.assertIn("linux-x86-regression:", workflow)
        self.assertIn(
            "meson test -C .tmp/meson-linux-release-tests --print-errorlogs",
            workflow,
        )
        self.assertIn(
            "source-validation, linux-x86-regression, ubuntu-x86",
            workflow,
        )
        # The GLx release-proof matrix gated publishing on self-hosted runners
        # labelled glx that are not registered, plus a retail basepath and a
        # reviewed baseline directory that no repository variable supplies, so
        # every release queued forever against labels nothing could satisfy.
        # Publishing must not depend on runners this repository does not have.
        self.assertNotIn("glx-release-proof:", workflow)
        self.assertNotIn("self-hosted", workflow)
        self.assertNotIn("--require-glx-proof", workflow)
        self.assertIn("needs: [prepare, push-build-validation]", workflow)

    def test_public_manual_proof_flag_preserves_local_snapshot_exemption(self) -> None:
        local_args = SimpleNamespace(
            channel="manual",
            glx_proof_root=None,
            require_glx_proof=False,
        )
        local_proof = release.resolve_glx_runtime_proof(local_args)
        self.assertFalse(local_proof["required"])
        self.assertEqual(local_proof["status"], "not-required")

        public_args = SimpleNamespace(
            channel="manual",
            glx_proof_root=None,
            require_glx_proof=True,
        )
        with self.assertRaisesRegex(ValueError, "public and tagged releases"):
            release.resolve_glx_runtime_proof(public_args)

        tagged_args = SimpleNamespace(
            channel="release",
            glx_proof_root=None,
            require_glx_proof=False,
        )
        with self.assertRaisesRegex(ValueError, "public and tagged releases"):
            release.resolve_glx_runtime_proof(tagged_args)

        with tempfile.TemporaryDirectory() as tmp:
            tagged_args.glx_proof_root = Path(tmp)
            with self.assertRaisesRegex(ValueError, "numeric --glx-proof-build-run-id"):
                release.resolve_glx_runtime_proof(tagged_args, "a" * 40, {})

    def test_public_manual_proof_flag_revalidates_supplied_root(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            proof_root = Path(tmp)
            args = SimpleNamespace(
                channel="manual",
                glx_proof_root=proof_root,
                require_glx_proof=True,
                glx_proof_build_run_id="1234",
                glx_proof_build_run_attempt="1",
            )
            runtime_identities = {"linux-x86": {"client": {}, "glxModule": {}}}
            with mock.patch.object(
                release,
                "validate_release_proof_root",
                return_value={"status": "passed", "failures": []},
            ) as validate:
                proof = release.resolve_glx_runtime_proof(
                    args,
                    "a" * 40,
                    runtime_identities,
                )

        validate.assert_called_once_with(
            proof_root,
            expected_source_commit="a" * 40,
            expected_build_run_id="1234",
            expected_build_run_attempt="1",
            expected_runtime_identities=runtime_identities,
        )
        self.assertTrue(proof["required"])
        self.assertEqual(proof["status"], "passed")

    def test_release_cli_parser_rejects_negative_build_numbers(self) -> None:
        with self.assertRaisesRegex(Exception, "non-negative"):
            release.non_negative_int("-1")


class X86_64ReleasePackagingTests(unittest.TestCase):
    """x86_64 (Windows and Linux) packages: only x86_64 binaries, no Steam provider."""

    def write_stage(self, root: Path, name: str, populate) -> Path:
        stage_root = root / name
        stage_root.mkdir()
        populate(stage_root)
        return stage_root

    def test_published_set_maps_every_artifact_to_its_platform_and_arch(self) -> None:
        self.assertEqual(
            release.PUBLISHED_RELEASE_ARTIFACTS,
            {
                "linux-x86",
                "windows-mingw-x86",
                "windows-msvc-x86",
                "linux-x86_64",
                "windows-mingw-x86_64",
                "windows-msvc-x86_64",
            },
        )
        meta = {"archive_prefix": "fnql-0.1.0.7-20261111-aaaaaaaa"}
        archive_names = set()
        for name, (platform, arch) in release.RELEASE_ARTIFACT_TARGETS.items():
            with self.subTest(artifact=name):
                self.assertEqual(release.release_platform(name), platform)
                self.assertEqual(release.release_arch(name), arch)
                archive = release.package_archive_name(meta, name)
                archive_names.add(archive)
                self.assertTrue(archive.endswith(f"-{name}" + (".tar.gz" if platform == "linux" else ".zip")))
                self.assertEqual(release.release_platform(archive), platform)
                self.assertEqual(release.release_arch(archive), arch)
                release.validate_release_architecture_names([name], platform=platform, arch=arch)
        self.assertEqual(len(archive_names), 6)

    def test_arch_resolution_rejects_unsupported_targets(self) -> None:
        self.assertEqual(release.release_arch("fnql-root.zip"), "x86")
        self.assertEqual(release.release_arch("fnql-windows-x64.zip"), "x86_64")
        self.assertEqual(release.release_arch("fnql-linux-amd64.tar.gz"), "x86_64")
        for name in ("linux-arm64", "windows-aarch64", "fnql-linux-armv7l.tar.gz", "linux-ppc64le"):
            with self.subTest(name=name), self.assertRaisesRegex(
                ValueError, "unsupported architecture"
            ):
                release.release_arch(name)
        with self.assertRaisesRegex(ValueError, "unsupported release architecture"):
            release.validate_release_architecture_names(["fnql"], arch="arm64")
        with self.assertRaisesRegex(ValueError, "unsupported release architecture"):
            release.validate_release_binary_stream("fnql", io.BytesIO(make_elf_i386()), arch="arm64")

    def test_artifact_root_requires_the_x86_64_set_too(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ("linux-x86", "windows-mingw-x86", "windows-msvc-x86"):
                (root / name).mkdir()
            with self.assertRaisesRegex(
                ValueError,
                "missing: linux-x86_64, windows-mingw-x86_64, windows-msvc-x86_64",
            ):
                release.release_artifact_dirs(root)

            for name in ("linux-x86_64", "windows-mingw-x86_64", "windows-msvc-x86_64"):
                (root / name).mkdir()
            (root / "linux-arm64").mkdir()
            with self.assertRaisesRegex(ValueError, "unsupported: linux-arm64"):
                release.release_artifact_dirs(root)

    def test_architecture_markers_are_checked_per_target(self) -> None:
        x86_64_names = [
            "fnql.x64.exe",
            "fnql.ded.x64.exe",
            "fnql.x64",
            "fnql_glx_x86_64.dll",
            "fnql_vk_x86_64.so",
            "baseq3/fnql_qagamex86_64.dll",
            "baseq3/fnql_uix86_64.so",
            "fnql-amd64-notes.txt",
            "docs/fnql/STEAM_PROVIDER_BINARY_NOTICE.txt",
            "fnql-audiozonesc.exe",
            release.ROOT_ARCHIVE_NAME,
            release.FNQL_WEBPAK_NAME,
        ]
        release.validate_release_architecture_names(x86_64_names, platform="windows", arch="x86_64")
        for offender in (
            "fnql_glx_x86.dll",
            "fnql.x86.exe",
            "fnql_vk_i386.so",
            "fnql-i686.so",
            "fnql-win32.exe",
            "mingw32/fnql.exe",
            "fnql_glx_arm64.dll",
            "fnql_glx_aarch64.so",
            "fnql_glx_armv7l.so",
            "fnql_glx_ppc64le.so",
        ):
            with self.subTest(offender=offender), self.assertRaisesRegex(
                ValueError, "x86_64 release artifacts must contain only x86_64 files"
            ):
                release.validate_release_architecture_names(
                    [*x86_64_names, offender], platform="linux", arch="x86_64"
                )
        # The x86 policy is unchanged: 64-bit and ARM markers stay forbidden.
        for offender in (
            "fnql.x64.exe",
            "fnql_glx_x86_64.dll",
            "fnql_glx_x86-64.so",
            "fnql-amd64.exe",
            "fnql_glx_arm64.dll",
            "fnql_glx_aarch64.so",
            "mingw64/fnql.exe",
        ):
            with self.subTest(x86_offender=offender), self.assertRaisesRegex(
                ValueError, "32-bit x86 only; found a 64-bit architecture marker"
            ):
                release.validate_release_architecture_names([offender], platform="windows")
        release.validate_release_architecture_names(
            ["fnql.exe", "fnql_glx_x86.dll", "baseq3/fnql_qagamex86.dll", "fnql_steam.dll"],
            platform="windows",
            arch="x86",
        )

    def test_renderer_gate_exempts_only_the_exact_game_module_paths(self) -> None:
        release.validate_renderer_module_names(
            [
                *release.WINDOWS_X86_64_RELEASE_RENDERER_MODULES,
                *release.LINUX_X86_64_RELEASE_RENDERER_MODULES,
                "baseq3/fnql_qagamex86_64.dll",
                "baseq3/fnql_cgamex86_64.so",
                "baseq3/fnql_uix86_64.so",
            ]
        )
        for offender in (
            "fnql_cgamex86_64.so",
            "missionpack/fnql_uix86_64.dll",
            "fnql_opengl_x86_64.dll",
            "baseq3/fnql_opengl_x86_64.so",
        ):
            with self.subTest(offender=offender), self.assertRaisesRegex(
                ValueError, "only glx, vk, and rtx"
            ):
                release.validate_renderer_module_names([offender])

    def test_x86_64_binary_gate_accepts_only_x86_64(self) -> None:
        release.validate_release_binary_stream(
            "fnql.x64.exe", io.BytesIO(make_pe_x86_64(dll=False)), platform="windows", arch="x86_64"
        )
        release.validate_release_binary_stream(
            "fnql.x64", io.BytesIO(make_elf_x86_64()), platform="linux", arch="x86_64"
        )
        big_endian = bytearray(make_elf_x86_64())
        big_endian[5] = 2
        arm_pe = bytearray(make_pe_x86_64(dll=True))
        struct.pack_into("<H", arm_pe, 0x84, 0xAA64)
        arm_elf = bytearray(make_elf_x86_64())
        struct.pack_into("<H", arm_elf, 18, 183)
        for name, binary, detail in (
            ("fnql.exe", make_pe(dll=False), "PE machine 0x014c"),
            ("fnql_glx_x86_64.dll", bytes(arm_pe), "PE machine 0xaa64"),
            ("fnql", make_elf_i386(), "ELF class 1"),
            ("fnql.x64", bytes(arm_elf), "machine 183"),
            ("fnql.x64", bytes(big_endian), "data encoding 2"),
            ("fnql.dylib", make_macho64(release.MACOS_X86_64_CPU), "Mach-O"),
            ("fnql32.dylib", b"\xce\xfa\xed\xfe" + (7).to_bytes(4, "little") + bytes(56), "Mach-O"),
        ):
            with self.subTest(name=name, detail=detail), self.assertRaisesRegex(
                ValueError, f"x86_64 release artifacts must contain only x86_64 binaries.*{detail}"
            ):
                release.validate_release_binary_stream(
                    name, io.BytesIO(binary), platform="linux", arch="x86_64"
                )
        # The x86 gate still rejects the x86_64 binaries the x86_64 gate accepts.
        with self.assertRaisesRegex(ValueError, "32-bit x86 only.*PE machine 0x8664"):
            release.validate_release_binary_stream("fnql.x64.exe", io.BytesIO(make_pe_x86_64(dll=False)))
        with self.assertRaisesRegex(ValueError, "32-bit x86 only.*ELF class 2"):
            release.validate_release_binary_stream("fnql.x64", io.BytesIO(make_elf_x86_64()))

    def test_linux_x86_64_package_is_accepted_and_executable(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage_root = self.write_stage(
                root, "linux-x86_64", lambda path: populate_linux_release_stage(path, arch="x86_64")
            )
            release.validate_stage_tree(stage_root, artifact_name="linux-x86_64")
            archive = root / "fnql-0.1.0.7-linux-x86_64.tar.gz"
            release.write_release_archive(stage_root, archive)

            release.validate_release_archive_contents(archive)
            release.validate_release_archive_contents(archive, artifact_name="linux-x86_64")
            verify_release_layout.verify_release_layout(archive)
            members = {member.name: member for member in release.read_release_archive(archive)}

            for name in (
                *release.LINUX_X86_64_RELEASE_EXECUTABLES,
                *release.LINUX_X86_64_RELEASE_RENDERER_MODULES,
            ):
                self.assertEqual(members[name].mode, 0o755)
            self.assertNotIn("fnql", members)
            # The same package fails the x86 policy it was not built for.
            with self.assertRaisesRegex(ValueError, "32-bit x86 only"):
                release.validate_release_archive_contents(archive, artifact_name="linux-x86")

    def test_linux_x86_64_package_requires_its_own_executable_names(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage_root = self.write_stage(
                root,
                "stage",
                lambda path: populate_linux_release_stage(path, arch="x86_64", include_server=False),
            )
            archive = root / "fnql-linux-x86_64.tar.gz"
            release.write_release_archive(stage_root, archive)
            with self.assertRaisesRegex(ValueError, "missing required Linux executables: fnql.ded.x64"):
                release.validate_release_archive_contents(archive)

            (stage_root / "fnql.ded.x64").write_bytes(make_elf_x86_64())
            (stage_root / "fnql_vk_x86_64.so").unlink()
            release.write_release_archive(stage_root, archive)
            with self.assertRaisesRegex(
                ValueError, "missing required Linux renderer modules: fnql_vk_x86_64.so"
            ):
                release.validate_release_archive_contents(archive)

    def test_x86_64_packages_reject_i386_binaries(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            linux = self.write_stage(
                root, "linux-stage", lambda path: populate_linux_release_stage(path, arch="x86_64")
            )
            (linux / "fnql_rtx_x86_64.so").write_bytes(make_elf_i386())
            with self.assertRaisesRegex(ValueError, "fnql_rtx_x86_64.so has ELF class 1"):
                release.validate_stage_tree(linux, artifact_name="linux-x86_64")
            archive = root / "fnql-linux-x86_64.tar.gz"
            release.write_release_archive(linux, archive)
            with self.assertRaisesRegex(ValueError, "only x86_64 binaries; fnql_rtx_x86_64.so"):
                release.validate_release_archive_contents(archive)

            windows = self.write_stage(root, "windows-stage", populate_windows_x86_64_release_stage)
            (windows / "fnql.ded.x64.exe").write_bytes(make_pe(dll=False))
            with self.assertRaisesRegex(ValueError, "fnql.ded.x64.exe has PE machine 0x014c"):
                release.validate_stage_tree(windows, artifact_name="windows-msvc-x86_64")

    def test_windows_x86_64_package_ships_without_the_steam_provider(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage_root = self.write_stage(root, "stage", populate_windows_x86_64_release_stage)
            for artifact in ("windows-mingw-x86_64", "windows-msvc-x86_64"):
                release.validate_stage_tree(stage_root, artifact_name=artifact)
                archive = root / f"fnql-0.1.0.7-{artifact}.zip"
                release.write_release_archive(stage_root, archive)
                release.validate_release_archive_contents(archive)
                release.validate_release_archive_contents(archive, artifact_name=artifact)
                verify_release_layout.verify_release_layout(archive)
            # The x86 policy rejects this package outright, and x86 Windows
            # packages still require the provider.
            with self.assertRaisesRegex(ValueError, "64-bit architecture marker"):
                release.validate_stage_tree(stage_root, artifact_name="windows-msvc-x86")
            with self.assertRaisesRegex(ValueError, "missing fnql_steam.dll"):
                release.validate_windows_distribution_files(["fnql.exe"], name="w.zip")
            with self.assertRaisesRegex(ValueError, "missing fnql_steam.dll"):
                release.validate_windows_distribution_files(
                    ["fnql.exe"], name="w.zip", arch="x86"
                )

    def test_windows_x86_64_package_rejects_the_steam_provider(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for location in ("fnql_steam.dll", "nested/FNQL_STEAM.DLL"):
                with self.subTest(location=location):
                    stage_root = root / f"stage-{location.count('/')}"
                    stage_root.mkdir()
                    populate_windows_x86_64_release_stage(stage_root)
                    provider = stage_root / location
                    provider.parent.mkdir(parents=True, exist_ok=True)
                    # Even an AMD64 build of the provider must not ship.
                    provider.write_bytes(make_pe_x86_64(dll=True))
                    with self.assertRaisesRegex(ValueError, "must not ship fnql_steam.dll"):
                        release.validate_stage_tree(stage_root, artifact_name="windows-mingw-x86_64")
                    archive = root / f"fnql-windows-mingw-x86_64-{location.count('/')}.zip"
                    release.write_release_archive(stage_root, archive)
                    with self.assertRaisesRegex(ValueError, "must not ship fnql_steam.dll"):
                        release.validate_release_archive_contents(archive)
                    with self.assertRaisesRegex(ValueError, "must not ship fnql_steam.dll"):
                        release.validate_windows_distribution_files(
                            [location], name="w.zip", arch="x86_64"
                        )

    def test_windows_x86_64_package_requires_the_x64_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage_root = self.write_stage(root, "stage", populate_windows_x86_64_release_stage)
            (stage_root / "fnql_rtx_x86_64.dll").unlink()
            (stage_root / "fnql.x64.exe").unlink()
            archive = root / "fnql-windows-msvc-x86_64.zip"
            release.write_release_archive(stage_root, archive)
            with self.assertRaisesRegex(
                ValueError,
                "missing required Windows x86_64 executables/modules: fnql.x64.exe, "
                "fnql_rtx_x86_64.dll",
            ):
                release.validate_release_archive_contents(archive)

    def test_windows_x86_64_package_rejects_unshipped_runtime_imports(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage_root = self.write_stage(root, "stage", populate_windows_x86_64_release_stage)
            (stage_root / "fnql.x64.exe").write_bytes(
                make_pe_x86_64(dll=False, imports=("KERNEL32.dll", "vcruntime140.dll"))
            )
            with self.assertRaisesRegex(ValueError, "unshipped Windows runtime DLLs"):
                release.validate_stage_tree(stage_root, artifact_name="windows-msvc-x86_64")

    def test_x86_64_required_import_gate_reads_pe32_plus_thunks(self) -> None:
        requirement = check_windows_runtime_deps.parse_required_import(
            "fnql.x64.exe=winmm.dll!joyGetPosEx"
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            client = root / "fnql.x64.exe"
            client.write_bytes(
                make_pe_with_functions(
                    {"KERNEL32.dll": ("GetTickCount",), "WINMM.dll": ("joyGetNumDevs", 7, "joyGetPosEx")},
                    machine=PE_AMD64,
                )
            )
            self.assertEqual(
                windows_pe.parse_pe_imported_functions(client.read_bytes(), name="fixture"),
                {"kernel32.dll": ("GetTickCount",), "winmm.dll": ("joyGetNumDevs", "joyGetPosEx")},
            )
            self.assertEqual(windows_pe.parse_pe(client.read_bytes()).machine, PE_AMD64)
            check_windows_runtime_deps.check_required_imports(root, [client], [requirement])

            client.write_bytes(
                make_pe_with_functions({"WINMM.dll": ("timeGetTime",)}, machine=PE_AMD64)
            )
            with self.assertRaisesRegex(ValueError, "does not import joyGetPosEx"):
                check_windows_runtime_deps.check_required_imports(root, [client], [requirement])
            # The x86 file name is not present in an x86_64 build directory.
            with self.assertRaisesRegex(ValueError, "fnql.exe was not found"):
                check_windows_runtime_deps.check_required_imports(
                    root,
                    [client],
                    [check_windows_runtime_deps.parse_required_import("fnql.exe=winmm.dll!joyGetPosEx")],
                )

    def test_glx_proof_binding_stays_on_the_x86_variants(self) -> None:
        for variant, proof_platform in release.GLX_RELEASE_PROOF_VARIANTS.items():
            with self.subTest(variant=variant):
                platform, arch = release.RELEASE_ARTIFACT_TARGETS[variant]
                self.assertEqual(arch, "x86")
                self.assertEqual(proof_platform, f"{platform}-x86")
                self.assertIn(variant, release.RELEASE_PROOF_RUNTIME_MEMBERS)
        self.assertEqual(
            set(release.RELEASE_PROOF_RUNTIME_MEMBERS), set(release.PUBLISHED_RELEASE_ARTIFACTS)
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage_root = self.write_stage(
                root, "stage", lambda path: populate_linux_release_stage(path, arch="x86_64")
            )
            archive = root / "fnql-linux-x86_64.tar.gz"
            release.write_release_archive(stage_root, archive)
            with self.assertRaisesRegex(ValueError, "Unsupported GLx proof artifact variant"):
                release.release_archive_runtime_identities(archive, "linux-x86_64")

            # Once the sweep defines an x86_64 variant, the binding checks 64-bit members.
            with mock.patch.dict(
                release.GLX_RELEASE_PROOF_VARIANTS, {"linux-x86_64": "linux-x86_64"}
            ):
                identities = release.release_archive_runtime_identities(archive, "linux-x86_64")
                self.assertEqual(identities["client"]["member"], "fnql.x64")
                self.assertEqual(identities["client"]["bits"], 64)
                self.assertEqual(identities["glxModule"]["member"], "fnql_glx_x86_64.so")
                self.assertEqual(identities["glxModule"]["machine"], "x86_64")

            # An x86 variant whose proof members are 64-bit is still rejected.
            (stage_root / "fnql").write_bytes(make_elf_x86_64())
            (stage_root / "fnql_glx_x86.so").write_bytes(make_elf_x86_64())
            release.write_release_archive(stage_root, archive)
            with self.assertRaisesRegex(ValueError, "expected elf 32-bit x86"):
                release.release_archive_runtime_identities(archive, "linux-x86")


if __name__ == "__main__":
    unittest.main()
