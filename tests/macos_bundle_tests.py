from __future__ import annotations

import os
import plistlib
import stat
import struct
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts import macos_bundle


MACHO_CPU_TYPES = {
    "x86_64": 0x01000007,
    "arm64": 0x0100000C,
}


def make_thin_macho(
    architecture: str,
    *,
    deployment_target: tuple[int, int, int] = (11, 0, 0),
    legacy_version_command: bool = False,
    byteorder: str = "little",
) -> bytes:
    prefix = "<" if byteorder == "little" else ">"
    major, minor, patch = deployment_target
    encoded_version = (major << 16) | (minor << 8) | patch
    if legacy_version_command:
        version_command = struct.pack(
            f"{prefix}IIII",
            macos_bundle.LC_VERSION_MIN_MACOSX,
            16,
            encoded_version,
            encoded_version,
        )
    else:
        version_command = struct.pack(
            f"{prefix}IIIIII",
            macos_bundle.LC_BUILD_VERSION,
            24,
            macos_bundle.PLATFORM_MACOS,
            encoded_version,
            encoded_version,
            0,
        )
    header = struct.pack(
        f"{prefix}IiiIIIII",
        0xFEEDFACF,
        MACHO_CPU_TYPES[architecture],
        0,
        2,
        1,
        len(version_command),
        0,
        0,
    )
    return header + version_command


def make_install(
    root: Path,
    architecture: str,
    *,
    renderers: tuple[str, ...] = ("glx", "vk"),
    deployment_target: tuple[int, int, int] = (11, 0, 0),
    legacy_version_command: bool = False,
    byteorder: str = "little",
) -> Path:
    root.mkdir(parents=True)
    suffix = "x86_64" if architecture == "x86_64" else "aarch64"
    binary = make_thin_macho(
        architecture,
        deployment_target=deployment_target,
        legacy_version_command=legacy_version_command,
        byteorder=byteorder,
    )
    (root / f"fnql.{suffix}").write_bytes(binary)
    (root / f"fnql.ded.{suffix}").write_bytes(binary)
    (root / "fnql-audiozonesc").write_bytes(binary)
    for renderer in renderers:
        (root / f"fnql_{renderer}_{suffix}.dylib").write_bytes(binary)
    (root / macos_bundle.PACKAGE_SIDECAR).write_bytes(b"package-sidecar")
    (root / macos_bundle.WEB_SIDECAR).write_bytes(b"web-sidecar")
    return root


def make_icon(root: Path) -> Path:
    icon = root / "test icon.icns"
    icon.write_bytes(b"icon")
    return icon


class FakeAppleRunner:
    def __init__(self) -> None:
        self.commands: list[list[str]] = []

    def __call__(self, argv) -> None:
        command = [str(value) for value in argv]
        self.commands.append(command)
        if Path(command[0]).name == "lipo" and "-create" in command:
            output_index = command.index("-output") + 1
            source_names = command[command.index("-create") + 1 : output_index - 1]
            Path(command[output_index]).write_bytes(
                b"UNIVERSAL\0" + b"\0".join(Path(name).read_bytes() for name in source_names)
            )
        elif Path(command[0]).name == "ditto":
            Path(command[-1]).write_bytes(b"notary archive")


def fake_finder(name: str) -> str:
    return f"/fake/apple tools/{name}"


class MacOSBundleTests(unittest.TestCase):
    def test_windows_publish_retries_transient_directory_access_denial(self) -> None:
        attempts: list[tuple[Path, Path]] = []
        delays: list[float] = []
        source = Path("source")
        destination = Path("destination")

        def replace(left: Path, right: Path) -> None:
            attempts.append((left, right))
            if len(attempts) < 3:
                raise PermissionError("transient scanner lock")

        macos_bundle._replace_staged_directory(
            source,
            destination,
            platform_name="nt",
            replacer=replace,
            sleeper=delays.append,
        )

        self.assertEqual(attempts, [(source, destination)] * 3)
        self.assertEqual(
            delays,
            [
                macos_bundle.WINDOWS_DIRECTORY_REPLACE_DELAY_SECONDS,
                macos_bundle.WINDOWS_DIRECTORY_REPLACE_DELAY_SECONDS * 2,
            ],
        )

    def test_non_windows_publish_does_not_retry_permission_errors(self) -> None:
        attempts = 0

        def replace(_source: Path, _destination: Path) -> None:
            nonlocal attempts
            attempts += 1
            raise PermissionError("real permission failure")

        with self.assertRaises(PermissionError):
            macos_bundle._replace_staged_directory(
                Path("source"),
                Path("destination"),
                platform_name="posix",
                replacer=replace,
                sleeper=lambda _delay: None,
            )

        self.assertEqual(attempts, 1)

    def test_default_bundle_versions_keep_three_part_display_and_build_number(self) -> None:
        self.assertEqual(
            macos_bundle.default_bundle_versions(
                {"base_version": "1.2.3", "version_tweak": 42}
            ),
            ("1.2.3", "42"),
        )
        self.assertEqual(
            macos_bundle.default_bundle_versions(
                {"base_version": "1.2.3", "version_tweak": 0}
            ),
            ("1.2.3", "1"),
        )

    def test_parse_input_specs_normalizes_aliases_and_preserves_equals_in_path(self) -> None:
        parsed = macos_bundle.parse_input_specs(
            ["aarch64=/tmp/arm=build", "x86_64=/tmp/x86 build"]
        )

        self.assertEqual(list(parsed), ["arm64", "x86_64"])
        self.assertEqual(parsed["arm64"], Path("/tmp/arm=build"))
        with self.assertRaisesRegex(ValueError, "Duplicate input"):
            macos_bundle.parse_input_specs(["arm64=a", "aarch64=b"])
        with self.assertRaisesRegex(ValueError, "ARCH=PATH"):
            macos_bundle.parse_input_specs(["arm64"])
        with self.assertRaisesRegex(ValueError, "Unsupported"):
            macos_bundle.parse_input_specs(["ppc64=old"])

    def test_thin_stage_has_canonical_layout_and_plist(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            install = make_install(root / "input tree", "arm64")
            icon = make_icon(root)
            output = root / "distribution"
            runner = FakeAppleRunner()

            result = macos_bundle.stage_bundle(
                {"arm64": install},
                output,
                icon=icon,
                short_version="1.2.3",
                bundle_version="7.8.9",
                source_date_epoch=macos_bundle.DEFAULT_SOURCE_DATE_EPOCH,
                runner=runner,
            )

            files = sorted(
                path.relative_to(output).as_posix()
                for path in output.rglob("*")
                if path.is_file()
            )
            plist_path = output / "FnQL.app" / "Contents" / "Info.plist"
            plist = plistlib.loads(plist_path.read_bytes())
            main = output / "FnQL.app" / "Contents" / "MacOS"

            self.assertEqual(result, output.resolve())
            self.assertEqual(
                files,
                [
                    "FnQL.app/Contents/Info.plist",
                    "FnQL.app/Contents/MacOS/FnQL",
                    "FnQL.app/Contents/MacOS/FnQL-pkg.fnz",
                    "FnQL.app/Contents/MacOS/fnql-web.pak",
                    "FnQL.app/Contents/MacOS/fnql_glx_aarch64.dylib",
                    "FnQL.app/Contents/MacOS/fnql_vk_aarch64.dylib",
                    "FnQL.app/Contents/PkgInfo",
                    "FnQL.app/Contents/Resources/fnql.icns",
                    "fnql-audiozonesc",
                    "fnql.ded",
                ],
            )
            self.assertEqual(
                (main / "FnQL").read_bytes(),
                (install / "fnql.aarch64").read_bytes(),
            )
            self.assertEqual((main / macos_bundle.PACKAGE_SIDECAR).read_bytes(), b"package-sidecar")
            self.assertEqual(plist["CFBundleExecutable"], "FnQL")
            self.assertEqual(plist["CFBundleIdentifier"], macos_bundle.BUNDLE_IDENTIFIER)
            self.assertEqual(plist["CFBundleShortVersionString"], "1.2.3")
            self.assertEqual(plist["CFBundleVersion"], "7.8.9")
            self.assertEqual(plist["LSMinimumSystemVersion"], "11.0")
            self.assertIs(plist["NSHighResolutionCapable"], True)
            self.assertTrue(plist["NSMicrophoneUsageDescription"].strip())
            self.assertEqual((output / "FnQL.app" / "Contents" / "PkgInfo").read_bytes(), b"APPLFNQL")
            self.assertFalse(
                any(Path(command[0]).name == "codesign" for command in runner.commands)
            )
            if os.name != "nt":
                self.assertEqual(stat.S_IMODE((main / "FnQL").stat().st_mode), 0o755)
                self.assertEqual(stat.S_IMODE(plist_path.stat().st_mode), 0o644)

    def test_failure_is_atomic_and_output_must_be_separate(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            install = make_install(root / "input", "x86_64")
            icon = make_icon(root)
            missing = install / macos_bundle.WEB_SIDECAR
            missing.unlink()
            output = root / "output"

            with self.assertRaisesRegex(FileNotFoundError, "WebUI sidecar"):
                macos_bundle.stage_bundle({"x86_64": install}, output, icon=icon)
            self.assertFalse(output.exists())

            missing.write_bytes(b"web-sidecar")
            with self.assertRaisesRegex(ValueError, "separate"):
                macos_bundle.stage_bundle(
                    {"x86_64": install}, install / "nested-output", icon=icon
                )
            self.assertFalse((install / "nested-output").exists())

            output.mkdir()
            sentinel = output / "sentinel"
            sentinel.write_text("keep", encoding="utf-8")
            with self.assertRaisesRegex(FileExistsError, "already exists"):
                macos_bundle.stage_bundle({"x86_64": install}, output, icon=icon)
            self.assertEqual(sentinel.read_text(encoding="utf-8"), "keep")

    def test_universal_requires_matching_inputs_and_darwin(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            x86 = make_install(root / "x86", "x86_64")
            arm = make_install(root / "arm", "arm64", renderers=("glx",))
            icon = make_icon(root)

            with self.assertRaisesRegex(ValueError, "same renderer set"):
                macos_bundle.stage_bundle(
                    {"x86_64": x86, "arm64": arm}, root / "mismatch", icon=icon
                )
            self.assertFalse((root / "mismatch").exists())

            (arm / "fnql_vk_aarch64.dylib").write_bytes(make_thin_macho("arm64"))
            with self.assertRaisesRegex(RuntimeError, "requires macOS"):
                macos_bundle.stage_bundle(
                    {"x86_64": x86, "arm64": arm},
                    root / "not-darwin",
                    icon=icon,
                    platform_name="Windows",
                )
            self.assertFalse((root / "not-darwin").exists())

            (arm / macos_bundle.WEB_SIDECAR).write_bytes(b"different")
            with self.assertRaisesRegex(ValueError, "different fnql-web.pak"):
                macos_bundle.stage_bundle(
                    {"x86_64": x86, "arm64": arm}, root / "sidecar-mismatch", icon=icon
                )

    def test_universal_lipo_order_is_stable_and_renderers_keep_both_names(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            x86 = make_install(root / "x86 input", "x86_64")
            arm = make_install(root / "arm input", "arm64")
            icon = make_icon(root)
            output = root / "Universal Output"
            runner = FakeAppleRunner()

            macos_bundle.stage_bundle(
                {"arm64": arm, "x86_64": x86},
                output,
                icon=icon,
                platform_name="Darwin",
                runner=runner,
                finder=fake_finder,
            )

            create_commands = [command for command in runner.commands if "-create" in command]
            self.assertEqual(len(create_commands), 3)
            self.assertEqual(Path(create_commands[0][2]), (x86 / "fnql.x86_64").resolve())
            self.assertEqual(Path(create_commands[0][3]), (arm / "fnql.aarch64").resolve())
            self.assertIn("x86 input", create_commands[0][2])
            self.assertIn("arm input", create_commands[0][3])
            self.assertTrue(any("-verify_arch" in command for command in runner.commands))

            macos = output / "FnQL.app" / "Contents" / "MacOS"
            self.assertTrue((macos / "FnQL").read_bytes().startswith(b"UNIVERSAL\0"))
            self.assertEqual(
                (macos / "fnql_glx_x86_64.dylib").read_bytes(),
                (x86 / "fnql_glx_x86_64.dylib").read_bytes(),
            )
            self.assertEqual(
                (macos / "fnql_glx_aarch64.dylib").read_bytes(),
                (arm / "fnql_glx_aarch64.dylib").read_bytes(),
            )

    def test_thin_macho_validation_accepts_current_legacy_and_big_endian_headers(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cases = (
                ("current", False, "little", (11, 0, 0)),
                ("legacy", True, "little", (10, 15, 7)),
                ("big-endian", False, "big", (11, 0, 0)),
            )
            for name, legacy, byteorder, deployment_target in cases:
                with self.subTest(name=name):
                    install = make_install(
                        root / name,
                        "arm64",
                        deployment_target=deployment_target,
                        legacy_version_command=legacy,
                        byteorder=byteorder,
                    )
                    inspected = macos_bundle.inspect_thin_install("arm64", install)
                    self.assertEqual(inspected.architecture, "arm64")

    def test_thin_macho_validation_rejects_wrong_architecture(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            install = make_install(root / "arm", "arm64")
            (install / "fnql.aarch64").write_bytes(make_thin_macho("x86_64"))

            with self.assertRaisesRegex(
                ValueError, "client has architecture x86_64; expected arm64"
            ):
                macos_bundle.inspect_thin_install("arm64", install)

    def test_thin_macho_validation_rejects_newer_deployment_target(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            install = make_install(
                root / "future", "x86_64", deployment_target=(12, 0, 0)
            )

            with self.assertRaisesRegex(
                ValueError, "requires macOS 12.0, newer than the bundle minimum 11.0"
            ):
                macos_bundle.inspect_thin_install("x86_64", install)

    def test_thin_macho_validation_rejects_truncated_load_command(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            install = make_install(root / "malformed", "x86_64")
            malformed = bytearray(make_thin_macho("x86_64"))
            malformed[36:40] = (32).to_bytes(4, byteorder="little")
            (install / "fnql.ded.x86_64").write_bytes(malformed)

            with self.assertRaisesRegex(ValueError, "truncated Mach-O load command"):
                macos_bundle.inspect_thin_install("x86_64", install)

    def test_developer_id_signing_is_inside_out_then_notarized(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            install = make_install(root / "input", "arm64")
            icon = make_icon(root)
            output = root / "signed"
            notary_keychain = root / "notary.keychain-db"
            notary_keychain.write_bytes(b"test keychain")
            runner = FakeAppleRunner()

            macos_bundle.stage_bundle(
                {"arm64": install},
                output,
                icon=icon,
                sign_identity="Developer ID Application: FnQL Test (ABCDE12345)",
                notary_profile="fnql-notary",
                notary_keychain=notary_keychain,
                platform_name="Darwin",
                runner=runner,
                finder=fake_finder,
            )

            signing = [
                command
                for command in runner.commands
                if Path(command[0]).name == "codesign" and "--sign" in command
            ]
            app_sign_index = next(
                index for index, command in enumerate(signing) if command[-1].endswith("FnQL.app")
            )
            self.assertEqual(app_sign_index, len(signing) - 1)
            self.assertTrue(all("--deep" not in command for command in signing))
            self.assertTrue(any(command[-1].endswith(".dylib") for command in signing[:app_sign_index]))
            self.assertIn("--entitlements", signing[app_sign_index])
            self.assertFalse(
                any(command[-1].endswith("Contents/MacOS/FnQL") for command in signing)
            )

            notary_index = next(
                index for index, command in enumerate(runner.commands) if "notarytool" in command
            )
            last_sign_index = max(
                index
                for index, command in enumerate(runner.commands)
                if Path(command[0]).name == "codesign" and "--sign" in command
            )
            staple_index = next(
                index
                for index, command in enumerate(runner.commands)
                if len(command) > 2 and command[1:3] == ["stapler", "staple"]
            )
            self.assertLess(last_sign_index, notary_index)
            self.assertLess(notary_index, staple_index)
            submit = runner.commands[notary_index]
            self.assertIn("--keychain-profile", submit)
            self.assertEqual(submit[submit.index("--keychain-profile") + 1], "fnql-notary")
            self.assertIn("--keychain", submit)
            self.assertEqual(
                Path(submit[submit.index("--keychain") + 1]),
                notary_keychain.resolve(),
            )
            self.assertFalse((output / ".fnql-signing-entitlements.plist").exists())

    def test_notarization_rejects_adhoc_signing_without_creating_output(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            install = make_install(root / "input", "x86_64")
            output = root / "output"

            with self.assertRaisesRegex(ValueError, "Developer ID"):
                macos_bundle.stage_bundle(
                    {"x86_64": install},
                    output,
                    icon=make_icon(root),
                    sign_identity="-",
                    notary_profile="profile",
                    platform_name="Darwin",
                    runner=FakeAppleRunner(),
                    finder=fake_finder,
                )
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
