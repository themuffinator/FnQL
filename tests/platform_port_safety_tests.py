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


if __name__ == "__main__":
    unittest.main()
