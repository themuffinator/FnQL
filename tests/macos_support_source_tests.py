from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


class MacOSSupportSourceTests(unittest.TestCase):
    def test_meson_requires_sdl_for_a_macos_client(self) -> None:
        meson = read("meson.build")
        self.assertIn("if darwin and build_client and not sdl_dep.found()", meson)
        self.assertIn("macOS client builds require SDL3", meson)
        self.assertIn("common_c_args += ['-DMACOS_X']", meson)

    def test_make_uses_external_sdl_and_modular_renderers(self) -> None:
        makefile = read("Makefile")
        darwin_start = makefile.index("ifeq ($(COMPILE_PLATFORM),darwin)")
        darwin_end = makefile.index("endif", darwin_start)
        defaults = makefile[darwin_start:darwin_end]
        self.assertIn("USE_LOCAL_HEADERS=0", defaults)
        self.assertIn("USE_RENDERER_DLOPEN=1", defaults)
        link_start = makefile.index("BASE_CFLAGS += -DMACOS_X")
        link_end = makefile.index("ifeq ($(USE_SYSTEM_JPEG),1)", link_start)
        darwin_link = makefile[link_start:link_end]
        self.assertIn("-framework SDL3", darwin_link)
        self.assertNotIn("libsdl/macosx", darwin_link)

    def test_cmake_has_real_apple_architecture_and_bundle_contracts(self) -> None:
        cmake = read("CMakeLists.txt")
        self.assertIn('FNQL_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|x64|x86|i[3-6]86)$"', cmake)
        self.assertIn('FNQL_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$"', cmake)
        self.assertIn("ADD_COMPILE_DEFINITIONS(MACOS_X)", cmake)
        self.assertIn("macOS client builds require SDL3", cmake)
        self.assertIn("code/asm/snd_mix_x86_64_preprocessed.S", cmake)
        self.assertIn("SET(CLIENT_EXE_TYPE MACOSX_BUNDLE)", cmake)
        self.assertIn("ADD_EXECUTABLE(${DNAME}${BINEXT} ${SERVER_EXE_TYPE}", cmake)
        self.assertIn("misc/macos/Info.plist.in", cmake)

    def test_app_local_code_and_finder_paths_are_used(self) -> None:
        client = read("code/client/cl_main.cpp")
        steam = read("code/platform/fnql_steam.cpp")
        unix = read("code/unix/unix_main.cpp")
        self.assertIn("CL_LoadRendererLibrary", client)
        self.assertIn("Sys_DefaultAppPath()", client)
        self.assertIn("const char *appRoot = Sys_DefaultAppPath()", steam)
        self.assertLess(
            steam.index("appRoot, PATH_SEP"),
            steam.index("baseRoot, PATH_SEP"),
        )
        self.assertIn("_NSGetExecutablePath", unix)
        self.assertIn("realpath( dst, resolved )", unix)
        self.assertIn("Sys_SetBinaryPath( Sys_BinName( argv[ 0 ] ) )", unix)

    def test_macos_runtime_edge_cases_have_regression_guards(self) -> None:
        input_source = read("code/sdl/sdl_input.cpp")
        network = read("code/qcommon/net_ip.c")
        vm = read("code/qcommon/vm.c")
        self.assertIn("keys[K_COMMAND].down = (mod & SDL_KMOD_GUI)", input_source)
        self.assertIn("if ( search->ifa_flags & IFF_UP )", network)
        self.assertNotIn("if ( ifap->ifa_flags & IFF_UP )", network)
        self.assertIn("Retail Quake Live ships no macOS game modules", vm)

    def test_moltenvk_portability_subset_is_enabled_if_advertised(self) -> None:
        header = read("code/renderercommon/vulkan/vulkan_core.h")
        self.assertIn('VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME "VK_KHR_portability_subset"', header)
        for source_name in ("code/renderervk/vk.c", "code/rendererrtx/vk.c"):
            source = read(source_name)
            self.assertIn("qboolean portabilitySubset = qfalse", source)
            self.assertIn("portabilitySubset = qtrue", source)
            self.assertIn(
                "device_extension_list[ device_extension_count++ ] = VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME",
                source,
            )

    def test_release_ci_covers_both_native_apple_architectures(self) -> None:
        workflow = read(".github/workflows/release.yml")
        self.assertIn("macos-15-intel", workflow)
        self.assertIn("runner: macos-15", workflow)
        self.assertIn("artifact_arch: x86_64", workflow)
        self.assertIn("artifact_arch: aarch64", workflow)
        self.assertIn("scripts/macos_bundle.py", workflow)
        self.assertIn("Stage without project signing", workflow)
        self.assertNotIn("--sign-identity -", workflow)
        self.assertIn("macos-release-sign:", workflow)
        self.assertIn("Developer ID sign, notarize, and staple", workflow)
        self.assertIn("name: unsigned-apple-${{ matrix.artifact_arch }}", workflow)
        self.assertIn("MACOS_DEVELOPER_ID_P12_BASE64", workflow)
        self.assertIn("MACOS_NOTARY_KEY_BASE64", workflow)
        self.assertIn('--keychain "$FNQL_MACOS_KEYCHAIN_PATH"', workflow)
        self.assertIn("codesign --verify --deep --strict", workflow)
        self.assertIn("com.apple.security.cs.allow-unsigned-executable-memory", workflow)
        self.assertIn("otool -L", workflow)
        self.assertIn("release.validate_stage_tree", workflow)
        self.assertIn("ditto -c -k --norsrc bin macos-payload.zip", workflow)
        self.assertIn("ditto -c -k --sequesterRsrc bin macos-payload.zip", workflow)
        self.assertIn("path: macos-payload.zip", workflow)
        self.assertIn("needs: [prepare, windows-msys32, windows-msvc, source-validation, macos, ubuntu-x86]", workflow)
        self.assertIn("macos-release-sign, ubuntu-x86]", workflow)
        self.assertIn("name: macos-x86_64", workflow)
        self.assertIn("name: macos-aarch64", workflow)
        signing_job = workflow.split("  macos-release-sign:", 1)[1].split(
            "\n  ubuntu-x86:", 1
        )[0]
        self.assertNotIn("actions/checkout", signing_job)
        self.assertLess(
            signing_job.index("Extract and validate unsigned payload"),
            signing_job.index("Configure isolated Developer ID credentials"),
        )

    def test_signing_assets_use_hardened_runtime_entitlement(self) -> None:
        entitlements = read("misc/macos/fnql.entitlements")
        self.assertIn("com.apple.security.cs.allow-unsigned-executable-memory", entitlements)
        self.assertNotIn("<!--", entitlements)
        attributes = read(".gitattributes")
        self.assertIn("misc/macos/*.entitlements text eol=lf", attributes)
        plist = read("misc/macos/Info.plist.in")
        self.assertIn("NSMicrophoneUsageDescription", plist)
        self.assertIn("NSHighResolutionCapable", plist)
        self.assertIn("LSMinimumSystemVersion", plist)

    def test_deprecated_entry_points_delegate_and_reject_legacy_syntax(self) -> None:
        for path in ("make-macosx-app.sh", "make-macosx-ub2.sh"):
            script = read(path)
            self.assertIn("scripts/macos_bundle.py", script)
            self.assertIn("Legacy syntax is no longer supported", script)
            self.assertNotIn("altool", script)
            self.assertNotIn("code/libsdl", script)


if __name__ == "__main__":
    unittest.main()
