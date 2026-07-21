from __future__ import annotations

import argparse
import hashlib
import os
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Mapping, Sequence


ROOT = Path(__file__).resolve().parents[1]
SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from fnql_meta import base_metadata


APP_NAME = "FnQL.app"
APP_EXECUTABLE = "FnQL"
BUNDLE_IDENTIFIER = "org.fnql.fnql"
MINIMUM_MACOS_VERSION = "11.0"
ICON_NAME = "fnql.icns"
PACKAGE_SIDECAR = "FnQL-pkg.fnz"
WEB_SIDECAR = "fnql-web.pak"
SERVER_NAME = "fnql.ded"
TOOL_NAME = "fnql-audiozonesc"
SUPPORTED_ARCHITECTURES = ("x86_64", "arm64")
ARCH_ALIASES = {
    "x86_64": "x86_64",
    "arm64": "arm64",
    "aarch64": "arm64",
}
ARCH_FILE_SUFFIXES = {
    "x86_64": ("x86_64",),
    "arm64": ("aarch64", "arm64"),
}
RENDERER_RE = re.compile(
    r"^fnql_(?P<renderer>glx|vk|rtx)_(?P<arch>x86_64|aarch64|arm64)\.dylib$"
)
VERSION_RE = re.compile(r"^[0-9]+(?:\.[0-9]+){0,2}$")
DEFAULT_SOURCE_DATE_EPOCH = 946684800  # 2000-01-01T00:00:00Z
WINDOWS_DIRECTORY_REPLACE_ATTEMPTS = 6
WINDOWS_DIRECTORY_REPLACE_DELAY_SECONDS = 0.05
MACHO_MAGIC_64_ENDIANNESS = {
    b"\xcf\xfa\xed\xfe": "little",
    b"\xfe\xed\xfa\xcf": "big",
}
MACHO_CPU_TYPES = {
    "x86_64": 0x01000007,
    "arm64": 0x0100000C,
}
MACHO_HEADER_64_SIZE = 32
MAX_MACHO_LOAD_COMMAND_BYTES = 16 * 1024 * 1024
LC_VERSION_MIN_MACOSX = 0x24
LC_BUILD_VERSION = 0x32
PLATFORM_MACOS = 1

CommandRunner = Callable[[Sequence[str]], None]
ToolFinder = Callable[[str], str | None]


@dataclass(frozen=True)
class ThinInstall:
    architecture: str
    root: Path
    client: Path
    server: Path
    tool: Path
    package_sidecar: Path
    web_sidecar: Path
    renderers: Mapping[str, Path]


def _run_checked(argv: Sequence[str]) -> None:
    subprocess.run(list(argv), check=True)


def normalize_architecture(value: str) -> str:
    architecture = ARCH_ALIASES.get(value.strip().lower())
    if architecture is None:
        raise ValueError(
            f"Unsupported macOS architecture {value!r}; expected x86_64 or arm64"
        )
    return architecture


def parse_input_specs(specs: Iterable[str]) -> dict[str, Path]:
    inputs: dict[str, Path] = {}
    for spec in specs:
        if "=" not in spec:
            raise ValueError(f"Invalid --input {spec!r}; expected ARCH=PATH")
        raw_architecture, raw_path = spec.split("=", 1)
        architecture = normalize_architecture(raw_architecture)
        if architecture in inputs:
            raise ValueError(f"Duplicate input for architecture {architecture}")
        if not raw_path:
            raise ValueError(f"Input path for architecture {architecture} is empty")
        inputs[architecture] = Path(raw_path).expanduser()

    if not inputs:
        raise ValueError("At least one --input ARCH=PATH is required")
    if len(inputs) > 2:
        raise ValueError("At most one x86_64 and one arm64 input may be staged")
    if len(inputs) == 2 and set(inputs) != set(SUPPORTED_ARCHITECTURES):
        raise ValueError("Universal 2 staging requires both x86_64 and arm64 inputs")
    return inputs


def _is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def _require_regular_file(root: Path, path: Path, description: str) -> Path:
    if path.is_symlink():
        raise ValueError(f"{description} must not be a symbolic link: {path}")
    if not path.is_file():
        raise FileNotFoundError(f"Missing {description}: {path}")
    resolved = path.resolve()
    if not _is_relative_to(resolved, root):
        raise ValueError(f"{description} escapes its input tree: {path}")
    return resolved


def _select_binary(
    root: Path,
    names: Iterable[str],
    description: str,
) -> Path:
    for name in names:
        candidate = root / name
        if candidate.exists() or candidate.is_symlink():
            return _require_regular_file(root, candidate, description)
    expected = ", ".join(names)
    raise FileNotFoundError(f"Missing {description} in {root}; expected one of: {expected}")


def _read_macho_u32(data: bytes, offset: int, byteorder: str) -> int:
    return int.from_bytes(data[offset : offset + 4], byteorder=byteorder, signed=False)


def _decode_macos_version(encoded: int) -> tuple[int, int, int]:
    return encoded >> 16, (encoded >> 8) & 0xFF, encoded & 0xFF


def _format_macos_version(encoded: int) -> str:
    major, minor, patch = _decode_macos_version(encoded)
    return f"{major}.{minor}.{patch}" if patch else f"{major}.{minor}"


def _maximum_macos_deployment_target() -> tuple[int, int, int]:
    parts = [int(part) for part in MINIMUM_MACOS_VERSION.split(".")]
    return tuple((parts + [0, 0, 0])[:3])


def _validate_thin_macho(path: Path, architecture: str, description: str) -> None:
    file_size = path.stat().st_size
    with path.open("rb") as handle:
        header = handle.read(MACHO_HEADER_64_SIZE)
        if len(header) != MACHO_HEADER_64_SIZE:
            raise ValueError(
                f"{description} must be a complete thin 64-bit Mach-O file: {path}"
            )

        byteorder = MACHO_MAGIC_64_ENDIANNESS.get(header[:4])
        if byteorder is None:
            raise ValueError(f"{description} must be a thin 64-bit Mach-O file: {path}")

        cpu_type = _read_macho_u32(header, 4, byteorder)
        expected_cpu_type = MACHO_CPU_TYPES[architecture]
        if cpu_type != expected_cpu_type:
            actual_architecture = next(
                (name for name, value in MACHO_CPU_TYPES.items() if value == cpu_type),
                f"CPU type 0x{cpu_type:08x}",
            )
            raise ValueError(
                f"{description} has architecture {actual_architecture}; "
                f"expected {architecture}: {path}"
            )

        command_count = _read_macho_u32(header, 16, byteorder)
        command_bytes = _read_macho_u32(header, 20, byteorder)
        if command_bytes > MAX_MACHO_LOAD_COMMAND_BYTES:
            raise ValueError(
                f"{description} has an unreasonably large Mach-O load-command table: {path}"
            )
        if command_bytes > file_size - MACHO_HEADER_64_SIZE:
            raise ValueError(f"{description} has truncated Mach-O load commands: {path}")
        if command_count > command_bytes // 8:
            raise ValueError(f"{description} has an invalid Mach-O load-command count: {path}")
        commands = handle.read(command_bytes)
        if len(commands) != command_bytes:
            raise ValueError(f"{description} has truncated Mach-O load commands: {path}")

    deployment_targets: list[int] = []
    offset = 0
    for command_index in range(command_count):
        if offset + 8 > len(commands):
            raise ValueError(
                f"{description} has a truncated Mach-O load command "
                f"at index {command_index}: {path}"
            )
        command = _read_macho_u32(commands, offset, byteorder)
        command_size = _read_macho_u32(commands, offset + 4, byteorder)
        if command_size < 8 or command_size % 8 != 0:
            raise ValueError(
                f"{description} has an invalid Mach-O load-command size "
                f"at index {command_index}: {path}"
            )
        command_end = offset + command_size
        if command_end > len(commands):
            raise ValueError(
                f"{description} has a truncated Mach-O load command "
                f"at index {command_index}: {path}"
            )

        if command == LC_BUILD_VERSION:
            if command_size < 24:
                raise ValueError(f"{description} has an invalid LC_BUILD_VERSION: {path}")
            target_platform = _read_macho_u32(commands, offset + 8, byteorder)
            tool_count = _read_macho_u32(commands, offset + 20, byteorder)
            if 24 + tool_count * 8 > command_size:
                raise ValueError(f"{description} has an invalid LC_BUILD_VERSION: {path}")
            if target_platform != PLATFORM_MACOS:
                raise ValueError(
                    f"{description} targets Mach-O platform {target_platform}, not macOS: {path}"
                )
            deployment_targets.append(_read_macho_u32(commands, offset + 12, byteorder))
        elif command == LC_VERSION_MIN_MACOSX:
            if command_size < 16:
                raise ValueError(
                    f"{description} has an invalid LC_VERSION_MIN_MACOSX: {path}"
                )
            deployment_targets.append(_read_macho_u32(commands, offset + 8, byteorder))

        offset = command_end

    if offset != len(commands):
        raise ValueError(f"{description} has an inconsistent Mach-O load-command table: {path}")
    if not deployment_targets:
        raise ValueError(f"{description} does not declare a macOS deployment target: {path}")
    if any(_decode_macos_version(target)[0] == 0 for target in deployment_targets):
        raise ValueError(f"{description} declares an invalid macOS deployment target: {path}")
    if len(set(deployment_targets)) != 1:
        versions = ", ".join(
            _format_macos_version(target) for target in sorted(set(deployment_targets))
        )
        raise ValueError(
            f"{description} declares conflicting macOS deployment targets ({versions}): {path}"
        )

    deployment_target = deployment_targets[0]
    if _decode_macos_version(deployment_target) > _maximum_macos_deployment_target():
        raise ValueError(
            f"{description} requires macOS {_format_macos_version(deployment_target)}, "
            f"newer than the bundle minimum {MINIMUM_MACOS_VERSION}: {path}"
        )


def inspect_thin_install(architecture: str, input_root: Path) -> ThinInstall:
    architecture = normalize_architecture(architecture)
    if input_root.is_symlink():
        raise ValueError(f"Input tree must not be a symbolic link: {input_root}")
    if not input_root.is_dir():
        raise NotADirectoryError(f"macOS input is not a directory: {input_root}")
    root = input_root.resolve()
    suffixes = ARCH_FILE_SUFFIXES[architecture]

    client_names = [f"fnql.{suffix}" for suffix in suffixes] + ["fnql"]
    server_names = [f"fnql.ded.{suffix}" for suffix in suffixes] + ["fnql.ded"]
    tool_names = [f"fnql-audiozonesc.{suffix}" for suffix in suffixes] + [TOOL_NAME]

    renderers: dict[str, Path] = {}
    accepted_suffixes = set(suffixes)
    for candidate in sorted(root.iterdir(), key=lambda path: path.name.casefold()):
        match = RENDERER_RE.fullmatch(candidate.name)
        if not match or match.group("arch") not in accepted_suffixes:
            continue
        renderer = match.group("renderer")
        if renderer in renderers:
            raise ValueError(
                f"Multiple {renderer} renderer libraries found for {architecture} in {root}"
            )
        renderers[renderer] = _require_regular_file(
            root, candidate, f"{architecture} {renderer} renderer"
        )
    if not renderers:
        raise FileNotFoundError(
            f"No architecture-named renderer dylibs found for {architecture} in {root}"
        )

    install = ThinInstall(
        architecture=architecture,
        root=root,
        client=_select_binary(root, client_names, f"{architecture} client"),
        server=_select_binary(root, server_names, f"{architecture} dedicated server"),
        tool=_select_binary(root, tool_names, f"{architecture} audio-zone tool"),
        package_sidecar=_require_regular_file(
            root, root / PACKAGE_SIDECAR, "FnQL package sidecar"
        ),
        web_sidecar=_require_regular_file(
            root, root / WEB_SIDECAR, "FnQL WebUI sidecar"
        ),
        renderers=renderers,
    )
    _validate_thin_macho(install.client, architecture, f"{architecture} client")
    _validate_thin_macho(
        install.server, architecture, f"{architecture} dedicated server"
    )
    _validate_thin_macho(install.tool, architecture, f"{architecture} audio-zone tool")
    for renderer, renderer_path in sorted(install.renderers.items()):
        _validate_thin_macho(
            renderer_path, architecture, f"{architecture} {renderer} renderer"
        )
    return install


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _validate_inputs(installs: Mapping[str, ThinInstall]) -> None:
    roots = [install.root for install in installs.values()]
    if len(set(roots)) != len(roots):
        raise ValueError("Architecture inputs must resolve to different directories")
    if len(installs) != 2:
        return

    x86 = installs["x86_64"]
    arm = installs["arm64"]
    if set(x86.renderers) != set(arm.renderers):
        raise ValueError(
            "Universal 2 inputs must contain the same renderer set; "
            f"x86_64 has {sorted(x86.renderers)}, arm64 has {sorted(arm.renderers)}"
        )
    for name, left, right in (
        (PACKAGE_SIDECAR, x86.package_sidecar, arm.package_sidecar),
        (WEB_SIDECAR, x86.web_sidecar, arm.web_sidecar),
    ):
        if _sha256(left) != _sha256(right):
            raise ValueError(f"Universal 2 inputs contain different {name} files")


def _validate_output_path(output: Path, installs: Mapping[str, ThinInstall]) -> Path:
    output = output.expanduser().resolve(strict=False)
    if os.path.lexists(output):
        raise FileExistsError(f"Output already exists: {output}")
    for install in installs.values():
        if (
            output == install.root
            or _is_relative_to(output, install.root)
            or _is_relative_to(install.root, output)
        ):
            raise ValueError(
                f"Output must be separate from every input tree: {output} and {install.root}"
            )
    return output


def _copy_file(source: Path, destination: Path, *, executable: bool) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    destination.chmod(0o755 if executable else 0o644)


def _write_file(destination: Path, data: bytes, *, executable: bool = False) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)
    destination.chmod(0o755 if executable else 0o644)


def _bundle_plist(*, short_version: str, bundle_version: str) -> bytes:
    if not VERSION_RE.fullmatch(short_version):
        raise ValueError(
            "macOS short version must contain one to three dot-separated integers"
        )
    if not VERSION_RE.fullmatch(bundle_version):
        raise ValueError(
            "macOS bundle version must contain one to three dot-separated integers"
        )
    values = {
        "CFBundleDevelopmentRegion": "en",
        "CFBundleDisplayName": "FnQL",
        "CFBundleExecutable": APP_EXECUTABLE,
        "CFBundleIconFile": ICON_NAME,
        "CFBundleIdentifier": BUNDLE_IDENTIFIER,
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": "FnQL",
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": short_version,
        "CFBundleSupportedPlatforms": ["MacOSX"],
        "CFBundleVersion": bundle_version,
        "LSApplicationCategoryType": "public.app-category.games",
        "LSMinimumSystemVersion": MINIMUM_MACOS_VERSION,
        "NSHighResolutionCapable": True,
        "NSMicrophoneUsageDescription": (
            "FnQL uses microphone input for optional in-game voice communication."
        ),
        "NSPrincipalClass": "NSApplication",
    }
    return plistlib.dumps(values, fmt=plistlib.FMT_XML, sort_keys=True)


def default_bundle_versions(metadata: Mapping[str, object]) -> tuple[str, str]:
    """Map semantic display version and monotonic CI build to Apple's fields."""
    short_version = str(metadata["base_version"])
    build_number = int(metadata["version_tweak"])
    return short_version, str(build_number if build_number > 0 else 1)


def _require_tool(name: str, finder: ToolFinder) -> str:
    result = finder(name)
    if not result:
        raise FileNotFoundError(f"Required Apple developer tool was not found: {name}")
    return result


def _lipo_create(
    lipo: str,
    sources: Sequence[Path],
    destination: Path,
    *,
    runner: CommandRunner,
) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    runner([lipo, "-create", *(str(source) for source in sources), "-output", str(destination)])
    if not destination.is_file():
        raise RuntimeError(f"lipo did not create its expected output: {destination}")
    destination.chmod(0o755)
    runner([lipo, "-verify_arch", "x86_64", "arm64", str(destination)])


def _stage_payload(
    payload_root: Path,
    installs: Mapping[str, ThinInstall],
    *,
    icon: Path,
    short_version: str,
    bundle_version: str,
    platform_name: str,
    runner: CommandRunner,
    finder: ToolFinder,
) -> None:
    app_root = payload_root / APP_NAME
    contents = app_root / "Contents"
    macos = contents / "MacOS"
    resources = contents / "Resources"
    macos.mkdir(parents=True)
    resources.mkdir(parents=True)

    ordered = [installs[arch] for arch in SUPPORTED_ARCHITECTURES if arch in installs]
    source = ordered[0]
    _copy_file(source.package_sidecar, macos / PACKAGE_SIDECAR, executable=False)
    _copy_file(source.web_sidecar, macos / WEB_SIDECAR, executable=False)
    _copy_file(icon, resources / ICON_NAME, executable=False)
    _write_file(contents / "Info.plist", _bundle_plist(
        short_version=short_version,
        bundle_version=bundle_version,
    ))
    _write_file(contents / "PkgInfo", b"APPLFNQL")

    if len(ordered) == 1:
        install = ordered[0]
        _copy_file(install.client, macos / APP_EXECUTABLE, executable=True)
        _copy_file(install.server, payload_root / SERVER_NAME, executable=True)
        _copy_file(install.tool, payload_root / TOOL_NAME, executable=True)
        renderer_suffix = "x86_64" if install.architecture == "x86_64" else "aarch64"
        for renderer, renderer_path in sorted(install.renderers.items()):
            _copy_file(
                renderer_path,
                macos / f"fnql_{renderer}_{renderer_suffix}.dylib",
                executable=True,
            )
        return

    if platform_name != "Darwin":
        raise RuntimeError("Universal 2 staging requires macOS and Apple's lipo tool")
    lipo = _require_tool("lipo", finder)
    x86 = installs["x86_64"]
    arm = installs["arm64"]
    _lipo_create(lipo, [x86.client, arm.client], macos / APP_EXECUTABLE, runner=runner)
    _lipo_create(lipo, [x86.server, arm.server], payload_root / SERVER_NAME, runner=runner)
    _lipo_create(lipo, [x86.tool, arm.tool], payload_root / TOOL_NAME, runner=runner)

    # Renderer lookup remains compile-time architecture-named in each client
    # slice. Keep both names rather than inventing an unsuffixed module ABI.
    for renderer in sorted(x86.renderers):
        x86_destination = macos / f"fnql_{renderer}_x86_64.dylib"
        arm_destination = macos / f"fnql_{renderer}_aarch64.dylib"
        _copy_file(x86.renderers[renderer], x86_destination, executable=True)
        _copy_file(arm.renderers[renderer], arm_destination, executable=True)
        runner([lipo, "-verify_arch", "x86_64", str(x86_destination)])
        runner([lipo, "-verify_arch", "arm64", str(arm_destination)])


def _default_entitlements() -> bytes:
    return plistlib.dumps(
        {
            "com.apple.security.cs.allow-unsigned-executable-memory": True,
        },
        fmt=plistlib.FMT_XML,
        sort_keys=True,
    )


def _sign_distribution(
    payload_root: Path,
    identity: str,
    *,
    entitlements: Path | None,
    runner: CommandRunner,
    finder: ToolFinder,
) -> None:
    codesign = _require_tool("codesign", finder)
    app_root = payload_root / APP_NAME
    macos = app_root / "Contents" / "MacOS"
    generated_entitlements = payload_root / ".fnql-signing-entitlements.plist"
    if entitlements is None:
        _write_file(generated_entitlements, _default_entitlements())
        entitlement_path = generated_entitlements
    else:
        entitlement_path = entitlements

    common = [codesign, "--force", "--sign", identity, "--options", "runtime"]
    common += ["--timestamp=none"] if identity == "-" else ["--timestamp"]

    nested_code = sorted(macos.glob("*.dylib"), key=lambda path: path.name.casefold())
    for path in nested_code:
        runner([*common, str(path)])
    runner([*common, "--entitlements", str(entitlement_path), str(payload_root / SERVER_NAME)])
    runner([*common, str(payload_root / TOOL_NAME)])
    # Signing the outer bundle signs its main executable. Apply the VM
    # executable-memory entitlement here so the final signature retains it;
    # separately signing the main executable first would be overwritten.
    runner([*common, "--entitlements", str(entitlement_path), str(app_root)])
    if generated_entitlements.exists():
        generated_entitlements.unlink()
    runner([codesign, "--verify", "--deep", "--strict", "--verbose=2", str(app_root)])
    for path in (payload_root / SERVER_NAME, payload_root / TOOL_NAME):
        runner([codesign, "--verify", "--strict", "--verbose=2", str(path)])


def _notarize_distribution(
    payload_root: Path,
    keychain_profile: str,
    *,
    keychain: Path | None,
    runner: CommandRunner,
    finder: ToolFinder,
) -> None:
    ditto = _require_tool("ditto", finder)
    xcrun = _require_tool("xcrun", finder)
    codesign = _require_tool("codesign", finder)
    spctl = _require_tool("spctl", finder)
    app_root = payload_root / APP_NAME

    with tempfile.TemporaryDirectory(prefix="fnql-notary-") as temporary:
        archive = Path(temporary) / "FnQL-macOS.zip"
        runner([
            ditto,
            "-c",
            "-k",
            "--sequesterRsrc",
            "--keepParent",
            str(payload_root),
            str(archive),
        ])
        submit_command = [
            xcrun,
            "notarytool",
            "submit",
            str(archive),
            "--keychain-profile",
            keychain_profile,
        ]
        if keychain is not None:
            submit_command += ["--keychain", str(keychain)]
        submit_command += ["--wait"]
        runner(submit_command)
    runner([xcrun, "stapler", "staple", str(app_root)])
    runner([xcrun, "stapler", "validate", str(app_root)])
    runner([codesign, "--verify", "--deep", "--strict", "--verbose=2", str(app_root)])
    runner([spctl, "--assess", "--type", "execute", "--verbose=2", str(app_root)])


def _normalize_tree_timestamps(root: Path, epoch: int) -> None:
    for path in sorted(
        (entry for entry in root.rglob("*") if entry.is_file()),
        key=lambda entry: entry.as_posix(),
    ):
        os.utime(path, (epoch, epoch))
    directories = [root, *(entry for entry in root.rglob("*") if entry.is_dir())]
    for path in sorted(directories, key=lambda entry: len(entry.parts), reverse=True):
        path.chmod(0o755)
        os.utime(path, (epoch, epoch))


def _replace_staged_directory(
    source: Path,
    destination: Path,
    *,
    platform_name: str = os.name,
    replacer: Callable[[Path, Path], None] = os.replace,
    sleeper: Callable[[float], None] = time.sleep,
) -> None:
    """Publish a completed tree, tolerating transient Windows file scanners."""
    for attempt in range(WINDOWS_DIRECTORY_REPLACE_ATTEMPTS):
        try:
            replacer(source, destination)
            return
        except PermissionError:
            if (
                platform_name != "nt"
                or attempt + 1 == WINDOWS_DIRECTORY_REPLACE_ATTEMPTS
            ):
                raise
            sleeper(WINDOWS_DIRECTORY_REPLACE_DELAY_SECONDS * (attempt + 1))


def stage_bundle(
    inputs: Mapping[str, Path],
    output: Path,
    *,
    icon: Path = ROOT / "code" / "unix" / ICON_NAME,
    short_version: str | None = None,
    bundle_version: str | None = None,
    sign_identity: str | None = None,
    entitlements: Path | None = None,
    notary_profile: str | None = None,
    notary_keychain: Path | None = None,
    source_date_epoch: int | None = None,
    platform_name: str | None = None,
    runner: CommandRunner = _run_checked,
    finder: ToolFinder = shutil.which,
) -> Path:
    normalized_inputs: dict[str, Path] = {}
    for raw_architecture, input_root in inputs.items():
        architecture = normalize_architecture(raw_architecture)
        if architecture in normalized_inputs:
            raise ValueError(f"Duplicate input for architecture {architecture}")
        normalized_inputs[architecture] = Path(input_root)
    if not normalized_inputs or len(normalized_inputs) > 2:
        raise ValueError("Staging requires one thin input or an x86_64/arm64 pair")
    if len(normalized_inputs) == 2 and set(normalized_inputs) != set(SUPPORTED_ARCHITECTURES):
        raise ValueError("Universal 2 staging requires both x86_64 and arm64 inputs")

    installs = {
        architecture: inspect_thin_install(architecture, input_root)
        for architecture, input_root in normalized_inputs.items()
    }
    _validate_inputs(installs)
    output = _validate_output_path(Path(output), installs)

    icon = Path(icon).expanduser()
    if icon.is_symlink() or not icon.is_file():
        raise FileNotFoundError(f"Missing regular macOS application icon: {icon}")
    icon = icon.resolve()

    metadata = base_metadata()
    default_short_version, default_bundle_version = default_bundle_versions(metadata)
    short_version = short_version or default_short_version
    bundle_version = bundle_version or default_bundle_version
    _bundle_plist(short_version=short_version, bundle_version=bundle_version)

    host = platform_name or platform.system()
    if sign_identity is not None:
        if not sign_identity or "\0" in sign_identity or "\n" in sign_identity:
            raise ValueError("Signing identity must be a non-empty single-line value")
        if host != "Darwin":
            raise RuntimeError("macOS code signing is only available on macOS")
    if entitlements is not None:
        entitlements = Path(entitlements).expanduser()
        if entitlements.is_symlink() or not entitlements.is_file():
            raise FileNotFoundError(f"Missing regular entitlements file: {entitlements}")
        entitlements = entitlements.resolve()
    if notary_profile is not None:
        if not sign_identity or sign_identity == "-":
            raise ValueError("Notarization requires a Developer ID signing identity")
        if not notary_profile or "\0" in notary_profile or "\n" in notary_profile:
            raise ValueError("Notary keychain profile must be a non-empty single-line value")
        if host != "Darwin":
            raise RuntimeError("Apple notarization is only available on macOS")
    if notary_keychain is not None:
        if not notary_profile:
            raise ValueError("A notary keychain requires --notary-profile")
        notary_keychain = Path(notary_keychain).expanduser()
        if notary_keychain.is_symlink() or not notary_keychain.is_file():
            raise FileNotFoundError(
                f"Missing regular notary keychain file: {notary_keychain}"
            )
        notary_keychain = notary_keychain.resolve()

    if source_date_epoch is None:
        raw_epoch = os.environ.get("SOURCE_DATE_EPOCH", str(DEFAULT_SOURCE_DATE_EPOCH))
        try:
            source_date_epoch = int(raw_epoch)
        except ValueError as error:
            raise ValueError("SOURCE_DATE_EPOCH must be an integer") from error
    if source_date_epoch < 0:
        raise ValueError("SOURCE_DATE_EPOCH must be non-negative")

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{output.name}.", dir=output.parent) as temporary:
        payload_root = Path(temporary) / "payload"
        payload_root.mkdir()
        _stage_payload(
            payload_root,
            installs,
            icon=icon,
            short_version=short_version,
            bundle_version=bundle_version,
            platform_name=host,
            runner=runner,
            finder=finder,
        )
        _normalize_tree_timestamps(payload_root, source_date_epoch)
        if sign_identity is not None:
            _sign_distribution(
                payload_root,
                sign_identity,
                entitlements=entitlements,
                runner=runner,
                finder=finder,
            )
        if notary_profile is not None:
            _notarize_distribution(
                payload_root,
                notary_profile,
                keychain=notary_keychain,
                runner=runner,
                finder=finder,
            )
        _replace_staged_directory(payload_root, output)
    return output


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Stage one or two Meson macOS install trees as a canonical FnQL.app "
            "distribution. Two inputs create Universal 2 executables with lipo."
        )
    )
    parser.add_argument(
        "--input",
        action="append",
        required=True,
        metavar="ARCH=PATH",
        help="Thin Meson install tree; ARCH is x86_64 or arm64 (repeat for Universal 2)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="New distribution directory that will contain FnQL.app and standalone tools",
    )
    parser.add_argument("--icon", type=Path, default=ROOT / "code" / "unix" / ICON_NAME)
    parser.add_argument("--short-version", help="CFBundleShortVersionString (defaults to project base version)")
    parser.add_argument(
        "--bundle-version",
        help="CFBundleVersion (defaults to the stamped build number, or 1)",
    )
    parser.add_argument(
        "--sign-identity",
        help="codesign identity; use '-' for ad-hoc signing or a Developer ID Application identity",
    )
    parser.add_argument(
        "--entitlements",
        type=Path,
        help="Optional compiled-VM executable-memory entitlements plist; a safe default is generated when omitted",
    )
    parser.add_argument(
        "--notary-profile",
        help="notarytool keychain profile; requires a non-ad-hoc --sign-identity",
    )
    parser.add_argument(
        "--notary-keychain",
        type=Path,
        help="Optional file keychain containing the notarytool profile",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)
    try:
        inputs = parse_input_specs(args.input)
        output = stage_bundle(
            inputs,
            args.output,
            icon=args.icon,
            short_version=args.short_version,
            bundle_version=args.bundle_version,
            sign_identity=args.sign_identity,
            entitlements=args.entitlements,
            notary_profile=args.notary_profile,
            notary_keychain=args.notary_keychain,
        )
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(2, f"macos_bundle.py: error: {error}\n")
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
