from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


class PlatformPortSafetyTests(unittest.TestCase):
    def test_linux_vulkan_and_x11_types_are_width_safe(self) -> None:
        qvk = read("code/unix/linux_qvk.cpp")
        x11 = read("code/unix/linux_glimp.cpp")
        randr = read("code/unix/x11_randr.cpp")

        self.assertIn("size_t i;", qvk)
        self.assertIn("static_cast<Atom>( event.xclient.data.l[0] )", x11)
        query_output = x11.index("void GLimp_QueryDisplayOutput")
        start_opengl = x11.index("static qboolean GLW_StartOpenGL", query_output)
        opengl_guard = x11.rfind("#ifdef USE_OPENGL_API", query_output, start_opengl)
        self.assertGreater(opengl_guard, query_output)
        self.assertIn("int64_t best_dist, best_rate;", randr)
        self.assertIn("int64_t dist, r;", randr)
        self.assertIn("x = (int64_t)*width - modeWidth;", randr)
        self.assertIn("r = (int64_t)*rate - getRefreshRate( mode_info );", randr)

    def test_retail_linux_x64_module_alias_is_dedicated_only(self) -> None:
        vm = read("code/qcommon/vm.c")

        standard_name = vm.index(
            'Com_sprintf( filename, sizeof( filename ), "%s" ARCH_STRING DLL_EXT, name );'
        )
        retail_alias = vm.index(
            'Com_sprintf( filename, sizeof( filename ), "%sx64%s", name, DLL_EXT );'
        )
        self.assertLess(standard_name, retail_alias)
        alias_guard = vm.rfind("#if defined( __linux__ ) && idx64", standard_name, retail_alias)
        self.assertGreater(alias_guard, standard_name)
        alias_block = vm[alias_guard : vm.index("#endif", retail_alias)]
        self.assertIn('index == VM_GAME && !Q_stricmp( name, "qagame" )', alias_block)
        self.assertNotIn("VM_CGAME", alias_block)
        self.assertNotIn("VM_UI", alias_block)

    def test_linux_steam_discovery_covers_xdg_snap_and_flatpak(self) -> None:
        unix = read("code/unix/unix_shared.cpp")

        explicit_override = unix.index('getenv( "STEAM_DIR" )')
        xdg = unix.index('getenv( "XDG_DATA_HOME" )')
        snap_environment = unix.index('getenv( "SNAP_USER_COMMON" )')
        snap_home = unix.index('"snap/steam/common/.local/share/Steam"')
        conventional_home = unix.index('".local/share/Steam"', snap_home)
        flatpak = unix.index(
            '".var/app/com.valvesoftware.Steam/.local/share/Steam"'
        )

        self.assertLess(explicit_override, xdg)
        self.assertLess(xdg, snap_environment)
        self.assertLess(snap_environment, snap_home)
        self.assertLess(snap_home, conventional_home)
        self.assertLess(conventional_home, flatpak)
        self.assertGreaterEqual(
            unix.count("platformDataRoot[0] == '/'"),
            2,
            "XDG and Snap roots must be absolute before they affect discovery",
        )

    def test_linux_app_local_runtime_discovery_does_not_depend_on_cwd(self) -> None:
        unix = read("code/unix/unix_main.cpp")
        qcommon = read("code/qcommon/qcommon.h")
        client = read("code/client/cl_main.cpp")
        files = read("code/qcommon/files.c")
        webpak = read("code/client/cl_webpak.cpp")
        steam = read("code/platform/fnql_steam.cpp")

        self.assertIn(
            "#if defined( __APPLE__ ) || defined( __linux__ )\n"
            "static char binaryPath",
            unix,
        )
        self.assertIn("#elif defined (__linux__)", unix)
        self.assertIn("Sys_SetBinaryPath( Sys_BinName( argv[ 0 ] ) );", unix)
        self.assertIn(
            "#if defined(__APPLE__) || defined(__linux__)\n"
            "char    *Sys_DefaultAppPath",
            qcommon,
        )

        loader_start = client.index("static void *CL_LoadRendererLibrary")
        loader_end = client.index("static void CL_InitRef", loader_start)
        loader = client[loader_start:loader_end]
        self.assertIn("defined( __APPLE__ ) || defined( __linux__ )", loader)
        self.assertLess(loader.index("Sys_DefaultAppPath()"), loader.index("Sys_DefaultBasePath()"))

        archive_paths = files[
            files.index("static int FS_RootArchiveBuildArchivePaths") :
            files.index("static int FS_ReadFileFromRootArchive")
        ]
        self.assertIn("defined( __APPLE__ ) || defined( __linux__ )", archive_paths)
        self.assertIn("Sys_DefaultAppPath()", archive_paths)
        self.assertLess(
            archive_paths.index("Sys_DefaultAppPath()"),
            archive_paths.index("Sys_Pwd()"),
        )
        self.assertLess(
            archive_paths.index("Sys_DefaultAppPath()"),
            archive_paths.index("fs_basepath->string"),
        )

        webpak_init = webpak[webpak.index("void CL_WebPak_Init") : webpak.index("void CL_WebPak_Shutdown")]
        self.assertLess(webpak_init.index("Sys_DefaultAppPath()"), webpak_init.index("Sys_Pwd()"))
        self.assertIn("defined( __APPLE__ ) || defined( __linux__ )", webpak_init)

        provider = steam[steam.index("void BuildCandidatePaths") : steam.index("void BuildSteamApiPath")]
        self.assertIn("defined(__APPLE__) || defined(__linux__)", provider)
        self.assertIn("const char *appRoot = Sys_DefaultAppPath()", provider)
        self.assertLess(provider.index("appRoot, PATH_SEP"), provider.index("baseRoot, PATH_SEP"))
        self.assertIn("std::strcmp(appRoot, baseRoot) != 0", provider)

    def test_unix_version_banner_uses_fnql_platform_identity(self) -> None:
        unix = read("code/unix/unix_main.cpp")

        banner_start = unix.index("void Sys_PrintBinVersion")
        banner_end = unix.index("#ifdef __APPLE__", banner_start)
        banner = unix[banner_start:banner_end]
        self.assertIn("FNQL_PROJECT_NAME", banner)
        self.assertIn("OS_STRING", banner)
        self.assertIn("ARCH_STRING", banner)
        self.assertNotIn("Quake3", banner)

    def test_linux_ansi_console_conversion_is_bounded(self) -> None:
        unix = read("code/unix/unix_main.cpp")

        colorify_start = unix.index("void Sys_ANSIColorify")
        colorify_end = unix.index("void Sys_Print", colorify_start)
        colorify = unix[colorify_start:colorify_end]
        self.assertIn("bufferSize <= 0", colorify)
        self.assertEqual(colorify.count("Q_strcat( buffer, bufferSize, tempBuffer )"), 3)
        self.assertNotIn("strncat", colorify)
        self.assertIn('write( STDOUT_FILENO, "\\033c", 2 );', unix)
        self.assertIn('write( STDOUT_FILENO, "]", 1 );', unix)

    def test_unix_command_line_preserves_argv_values_with_spaces(self) -> None:
        unix = read("code/unix/unix_main.cpp")

        builder_start = unix.index("static bool Sys_CommandLineArgNeedsQuotes")
        builder_end = unix.index("int main(", builder_start)
        builder = unix[builder_start:builder_end]
        self.assertIn("static_cast<unsigned char>( *cursor ) <= ' '", builder)
        self.assertIn("*cursor == ';'", builder)
        self.assertIn("*cursor == '+' && cursor != arg", builder)
        self.assertIn("strchr( arg, '\"' ) == nullptr", builder)
        self.assertEqual(builder.count("cmdline.push_back( '\"' );"), 2)

    def test_win32_display_query_is_available_without_opengl(self) -> None:
        win32 = read("code/win32/win_glimp.cpp")

        query_output = win32.index("void GLimp_QueryDisplayOutput")
        previous_opengl_guard = win32.rfind("#ifdef USE_OPENGL_API", 0, query_output)
        previous_guard_end = win32.rfind("#endif", 0, query_output)
        choose_pixel_format = win32.index("static int GLW_ChoosePFD", query_output)
        opengl_guard = win32.rfind(
            "#ifdef USE_OPENGL_API", query_output, choose_pixel_format
        )

        self.assertGreater(previous_guard_end, previous_opengl_guard)
        self.assertGreater(opengl_guard, query_output)
        self.assertEqual(win32.count("void GLimp_QueryDisplayOutput"), 1)

    def test_supported_renderer_contract_is_consistent(self) -> None:
        cmake = read("CMakeLists.txt")
        makefile = read("Makefile")
        msvc_driver = read("scripts/msvc_meson.py")
        vscode_build = read(".vscode/build-release.ps1")

        self.assertIn('OPTION(USE_RTX "Build the ray-tracing renderer module" ON)', cmake)
        self.assertIn("SET(RENDERER_DEFAULT glx CACHE STRING", cmake)
        self.assertIn("RENDERER_DEFAULT PROPERTY STRINGS glx vk rtx", cmake)
        self.assertIn("USE_RTX          = 1", makefile)
        self.assertIn("RENDERER_DEFAULT = glx", makefile)
        self.assertIn("TARGET_RENDRTX = $(RENDERER_PREFIX)_rtx_$(SHLIBNAME)", makefile)
        self.assertIn("RE_DrawScaledText=R_RTX_DrawScaledText", cmake)
        self.assertIn("RE_DrawScaledText=R_RTX_DrawScaledText", makefile)
        self.assertIn(
            'DEFAULT_RENDERERS = ("glx", "vk", "rtx")',
            msvc_driver,
        )
        self.assertIn("SUPPORTED_RENDERERS = DEFAULT_RENDERERS", msvc_driver)
        self.assertIn("'glx,vk,rtx'", vscode_build)
        self.assertIn("'glx', 'vk', 'rtx'", vscode_build)

    def test_alsa_preserves_signed_error_results(self) -> None:
        alsa = read("code/unix/linux_snd.cpp")

        self.assertIn("const snd_pcm_sframes_t written", alsa)
        self.assertIn("if ( written < 0 )", alsa)
        self.assertIn("static_cast<snd_pcm_uframes_t>( written )", alsa)
        self.assertIn("while ( ( avail = _snd_pcm_avail_update( handle ) ) >= 0", alsa)

    def test_glx_material_name_formatting_is_bounded(self) -> None:
        glx = read("code/rendererglx/glx_material.cpp")
        start = glx.index("static void GLX_Material_KeyName")
        end = glx.index("static qboolean GLX_Material_AppendSource", start)
        key_name = glx[start:end]

        self.assertIn("Q_strncpyz", key_name)
        self.assertEqual(key_name.count("Q_strcat"), 2)
        self.assertNotIn("snprintf", key_name)

    def test_meson_keeps_fallback_and_windows_policy_composable(self) -> None:
        meson = read("meson.build")

        self.assertIn(
            "quiet_fallback_defaults = fallback_defaults + ['warning_level=0']",
            meson,
        )
        self.assertNotIn("'c_args=-w'", meson)
        self.assertNotIn("'cpp_args=-w'", meson)
        self.assertIn("add_global_arguments(win32_compat_args, language: ['c', 'cpp'])", meson)
        self.assertIn("common_c_args += ['-DWIN32', '-D_WINDOWS', '-DNOMINMAX']", meson)

    def test_make_install_requires_an_explicit_destination(self) -> None:
        makefile = read("Makefile")

        self.assertIn("DESTDIR ?=", makefile)
        self.assertIn("ifneq ($(filter install,$(MAKECMDGOALS)),)", makefile)
        self.assertIn("ifeq ($(strip $(DESTDIR)),)", makefile)
        self.assertIn("DESTDIR is required", makefile)
        self.assertNotIn("DESTDIR=/usr/local/games/quake3", makefile)
        self.assertIn(
            'if [ -d "$(BUILD_DIR)" ]; then find "$(BUILD_DIR)" -type f',
            makefile,
        )
        self.assertNotIn("D_FILES=$(shell find .", makefile)

    def test_cmake_uses_target_width_for_linux_x86_names(self) -> None:
        cmake = read("CMakeLists.txt")

        self.assertIn(
            'STRING(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" FNQL_SYSTEM_PROCESSOR)',
            cmake,
        )
        self.assertIn(
            'FNQL_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|x64|x86|i[3-6]86)$"',
            cmake,
        )
        self.assertIn("IF(CMAKE_SIZEOF_VOID_P EQUAL 8)", cmake)
        self.assertNotIn("IF(CMAKE_SYSTEM_PROCESSOR MATCHES AMD64|x86|i*86)", cmake)

    def test_cmake_linux_client_enables_available_curl_and_vorbis(self) -> None:
        cmake = read("CMakeLists.txt")

        self.assertIn("OPTION(USE_CURL_DLOPEN", cmake)
        self.assertIn("find_package(CURL QUIET)", cmake)
        self.assertIn(
            "list(APPEND FNQL_CLIENT_COMPILE_DEFINITIONS USE_CURL)", cmake
        )
        self.assertIn(
            "list(APPEND FNQL_CLIENT_COMPILE_DEFINITIONS USE_CURL_DLOPEN)",
            cmake,
        )
        self.assertIn("pkg_check_modules(VORBISFILE QUIET IMPORTED_TARGET vorbisfile)", cmake)
        self.assertIn(
            "list(APPEND FNQL_CLIENT_COMPILE_DEFINITIONS USE_OGG_VORBIS)",
            cmake,
        )
        self.assertIn("PkgConfig::VORBISFILE", cmake)

    def test_cmake_unix_renderer_modules_link_their_math_dependency(self) -> None:
        cmake = read("CMakeLists.txt")

        self.assertIn("FOREACH(FNQL_RENDERER_KIND glx vk rtx)", cmake)
        self.assertIn(
            "TARGET_LINK_LIBRARIES(${FNQL_RENDERER_TARGET} PRIVATE m)", cmake
        )

    def test_linux_ci_builds_the_full_native_sdl_runtime(self) -> None:
        workflow = read(".github/workflows/linux-verification.yml")

        self.assertIn("make-server-i686:", workflow)
        self.assertIn("ARCH=x86", workflow)
        self.assertIn("COMPILE_ARCH=x86", workflow)
        self.assertIn("BUILD_CLIENT=0", workflow)
        self.assertIn("build/release-linux-x86/fnql.ded", workflow)
        self.assertIn("ELF 32-bit LSB", workflow)
        self.assertIn("meson-sdl-x86_64:", workflow)
        self.assertIn("--wrap-mode=nofallback", workflow)
        self.assertIn("-Dbuild-client=false", workflow)
        self.assertIn("meson-linux-server-only fnql.ded.x86_64", workflow)
        self.assertIn("meson-linux-curl-fallback", workflow)
        self.assertIn("--wrap-mode=forcefallback", workflow)
        self.assertIn("-Dcurl-dlopen=false", workflow)
        self.assertIn("meson-linux-curl-fallback fnql.x86_64", workflow)
        self.assertIn("--force-fallback-for=sdl3,fontstash", workflow)
        self.assertIn("-Dbuild-client=true", workflow)
        self.assertIn("-Dbuild-server=true", workflow)
        self.assertIn("-Drenderers=glx,vk,rtx", workflow)
        self.assertIn("-Dsdl=enabled", workflow)
        self.assertIn("meson test -C .tmp/meson-linux-x86_64", workflow)
        self.assertIn("fnql.ded.x86_64", workflow)
        self.assertIn("fnql_glx_x86_64.so", workflow)
        self.assertIn("fnql_vk_x86_64.so", workflow)
        self.assertIn("fnql_rtx_x86_64.so", workflow)
        cmake_targets = workflow.index("--target")
        target_listing = workflow[
            cmake_targets : workflow.index("ninja -C", cmake_targets)
        ]
        self.assertIn("fnql.x86_64", target_listing)
        self.assertIn("fnql.ded.x86_64", target_listing)
        self.assertIn("fnql_glx_x86_64", target_listing)
        self.assertIn("fnql_vk_x86_64", target_listing)
        self.assertIn("fnql_rtx_x86_64", target_listing)

    def test_meson_server_only_build_skips_client_dependencies(self) -> None:
        meson = read("meson.build")

        self.assertIn("if build_client\n  fontstash_dep = dependency('fontstash'", meson)
        self.assertIn("if build_client\n  freetype_dep = dependency('freetype2'", meson)
        self.assertIn("if build_client and not sdl_opt.disabled()", meson)
        self.assertIn("if build_client and not curl_opt.disabled()", meson)
        self.assertIn("jpeg_source = 'disabled'\nif build_client", meson)
        self.assertIn("if build_client and get_option('ogg-vorbis')", meson)
        self.assertIn("if build_client and renderer_dlopen", meson)
        self.assertIn("if build_client and not renderer_dlopen", meson)
        self.assertIn("'renderer-dlopen': build_client ? renderer_dlopen : false", meson)
        self.assertIn(
            "'renderer modules': build_client and renderer_dlopen ?",
            meson,
        )
        self.assertIn(
            "'static renderer': build_client and not renderer_dlopen ?",
            meson,
        )

    def test_zlib_wrap_is_self_contained_for_clean_fallback_builds(self) -> None:
        zlib_wrap = read("subprojects/zlib.wrap")

        self.assertIn("[wrap-file]", zlib_wrap)
        self.assertIn("directory = zlib-1.3.2", zlib_wrap)
        self.assertIn(
            "source_hash = d7a0654783a4da529d1bb793b7ad9c3318020af77667bcae35f95d0e42a792f3",
            zlib_wrap,
        )
        self.assertIn("dependency_names = zlib", zlib_wrap)
        self.assertNotIn("[wrap-redirect]", zlib_wrap)
        self.assertNotIn("freetype-", zlib_wrap)


if __name__ == "__main__":
    unittest.main()
