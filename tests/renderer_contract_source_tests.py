from __future__ import annotations

import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RENDERERS = ("glx", "vk", "rtx")


class RendererContractSourceTests(unittest.TestCase):
    def test_meson_exposes_exactly_three_renderers(self) -> None:
        options = (ROOT / "meson_options.txt").read_text(encoding="utf-8")
        match = re.search(
            r"option\('renderers'.*?choices:\s*\[(?P<choices>[^]]+)\]",
            options,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        assert match is not None
        self.assertEqual(
            tuple(re.findall(r"'([^']+)'", match.group("choices"))), RENDERERS
        )
        self.assertIsNotNone(
            re.search(
                r"option\('renderer-default'.*?value:\s*'glx'", options, re.DOTALL
            )
        )

    def test_cmake_exposes_exactly_three_renderers(self) -> None:
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("set(FNQL_CMAKE_RENDERERS glx vk rtx)", cmake)
        for renderer in RENDERERS:
            self.assertIn(f"${{RENDERER_PREFIX}}_{renderer}${{RENDEXT}}", cmake)

    def test_make_exposes_exactly_three_renderers(self) -> None:
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertRegex(makefile, r"(?m)^RENDERER_DEFAULT\s*=\s*glx$")
        self.assertRegex(makefile, r"(?m)^USE_GLX\s*=\s*1$")
        self.assertRegex(makefile, r"(?m)^USE_VK\s*=\s*1$")
        self.assertRegex(makefile, r"(?m)^USE_RTX\s*=\s*1$")
        for renderer in RENDERERS:
            self.assertIn(f"ifeq ($(RENDERER_DEFAULT),{renderer})", makefile)

    def test_client_accepts_only_exact_renderer_selectors(self) -> None:
        client = (ROOT / "code" / "client" / "cl_main.cpp").read_text(
            encoding="utf-8"
        )
        allowlist = re.search(
            r"static bool isValidRenderer\( const char \*s \) \{(?P<body>.*?)\n\}",
            client,
            re.DOTALL,
        )
        self.assertIsNotNone(allowlist)
        assert allowlist is not None
        self.assertEqual(
            tuple(
                re.findall(
                    r'strcmp\( s, "([^"]+)" \) == 0', allowlist.group("body")
                )
            ),
            RENDERERS,
        )
        self.assertIn(
            'Cvar_Get( "cl_renderer", "glx", CVAR_ARCHIVE | CVAR_LATCH )', client
        )

    def test_vscode_entry_points_use_exact_renderer_selectors(self) -> None:
        build_script = (ROOT / ".vscode" / "build-release.ps1").read_text(
            encoding="utf-8"
        )
        launch_script = (ROOT / ".vscode" / "launch-release.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("ValidateSet('all', 'glx', 'vk', 'rtx')", build_script)
        self.assertIn("else { 'glx,vk,rtx' }", build_script)
        self.assertIn("ValidateSet('glx', 'vk', 'rtx')", launch_script)

        tasks = json.loads((ROOT / ".vscode" / "tasks.json").read_text(encoding="utf-8"))
        build_tasks = [
            task
            for task in tasks["tasks"]
            if "-Renderers" in task.get("args", [])
        ]
        self.assertTrue(build_tasks)
        for task in build_tasks:
            renderer_option = task["args"].index("-Renderers")
            self.assertEqual(task["args"][renderer_option + 1], ",".join(RENDERERS))

        launch = json.loads(
            (ROOT / ".vscode" / "launch.json").read_text(encoding="utf-8")
        )
        selectors = set()
        for configuration in launch["configurations"]:
            args = configuration.get("args", [])
            if "cl_renderer" in args:
                selectors.add(args[args.index("cl_renderer") + 1])
        self.assertEqual(selectors, set(RENDERERS))

    def test_web_settings_use_exact_renderer_selectors(self) -> None:
        settings = (ROOT / "code" / "client" / "webui" / "fnql-settings.js").read_text(
            encoding="utf-8"
        )
        renderer_setting = re.search(
            r"name: 'cl_renderer'.*?options:\s*\[(?P<options>.*?)\]\s*,\s*help:",
            settings,
            re.DOTALL,
        )
        self.assertIsNotNone(renderer_setting)
        assert renderer_setting is not None
        self.assertEqual(
            tuple(
                re.findall(
                    r"\['([^']+)',\s*'[^']+'\]",
                    renderer_setting.group("options"),
                )
            ),
            RENDERERS,
        )
        self.assertNotIn("experimental", renderer_setting.group(0).lower())

    def test_removed_renderer2_tree_is_absent(self) -> None:
        self.assertFalse((ROOT / "code" / "renderer2").exists())
        for path in (
            ROOT / "code" / "renderercommon" / "tr_font.c",
            ROOT / "code" / "renderercommon" / "tr_font_stash.c",
        ):
            source = path.read_text(encoding="utf-8")
            self.assertNotIn("RENDERER_OPENGL2", source)
            self.assertNotIn("../renderer2/", source)

    def test_visual_studio_surfaces_match_the_public_contract(self) -> None:
        legacy = ROOT / "code" / "win32" / "msvc2005"
        self.assertEqual(
            {path.stem for path in legacy.glob("renderer*.vcproj")},
            {"rendererglx", "renderervk", "rendererrtx"},
        )
        legacy_solution = (legacy / "fnql.sln").read_text(encoding="utf-8")
        for renderer in RENDERERS:
            self.assertIn(f'"renderer{renderer}"', legacy_solution)
            renderer_project = (legacy / f"renderer{renderer}.vcproj").read_text(
                encoding="utf-8"
            )
            self.assertIn("BUILD_FONTSTASH", renderer_project)
            self.assertIn("tr_font_stash.c", renderer_project)
        self.assertNotIn('"renderer"', legacy_solution)
        self.assertNotIn('"renderer2"', legacy_solution)

        modern = ROOT / "code" / "win32" / "msvc2017"
        project = (modern / "fnql.vcxproj").read_text(encoding="utf-8")
        self.assertNotIn("RENDERER_DEFAULT=opengl", project)
        self.assertGreaterEqual(project.count("RENDERER_DEFAULT=glx"), 6)
        self.assertEqual(
            {path.stem for path in modern.glob("renderer*.vcxproj")},
            {"rendererglx", "renderervk", "rendererrtx"},
        )

    def test_static_vulkan_family_renderers_keep_shared_display_queries(self) -> None:
        linux_glimp = (ROOT / "code" / "unix" / "linux_glimp.cpp").read_text(
            encoding="utf-8"
        )
        shared_start = linux_glimp.index("static void InitCvars")
        query = linux_glimp.index("void GLimp_QueryDisplayOutput")
        opengl_guard = linux_glimp.index("#ifdef USE_OPENGL_API", shared_start)
        self.assertLess(shared_start, query)
        self.assertLess(query, opengl_guard)

        win_glimp = (ROOT / "code" / "win32" / "win_glimp.cpp").read_text(
            encoding="utf-8"
        )
        query = win_glimp.index("void GLimp_QueryDisplayOutput")
        opengl_loader_guard = win_glimp.index(
            "#ifdef USE_OPENGL_API\n/*\n** GLW_LoadOpenGL"
        )
        self.assertLess(query, opengl_loader_guard)

        meson = (ROOT / "meson.build").read_text(encoding="utf-8")
        self.assertIn("static_library('renderer_static_vk'", meson)
        self.assertIn("static_library('renderer_static_rtx'", meson)

    def test_static_renderers_isolate_host_font_symbols(self) -> None:
        meson = (ROOT / "meson.build").read_text(encoding="utf-8")
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        for private_prefix in ("GLX", "VK", "RTX"):
            for symbol in ("DrawScaledText", "MeasureScaledText"):
                definition = f"RE_{symbol}=R_{private_prefix}_{symbol}"
                self.assertIn(definition, meson)
                self.assertIn(definition, cmake)
                self.assertIn(definition, makefile)

    def test_rtx_sources_follow_project_gplv2_license(self) -> None:
        license_text = (ROOT / "LICENSE").read_text(encoding="utf-8")
        self.assertIn("GNU GENERAL PUBLIC LICENSE", license_text)
        self.assertIn("Version 2, June 1991", license_text)
        rtx_license = (ROOT / "code" / "rendererrtx" / "LICENSE.md").read_text(
            encoding="utf-8"
        )
        self.assertIn("GNU General Public License, version 2", rtx_license)
        notices = "\n".join(
            path.read_text(encoding="utf-8", errors="ignore")
            for path in (ROOT / "code" / "rendererrtx").rglob("*")
            if path.is_file()
        )
        self.assertNotRegex(notices, r"GPL(?:-|\s).*?3|Version 3")


if __name__ == "__main__":
    unittest.main()
