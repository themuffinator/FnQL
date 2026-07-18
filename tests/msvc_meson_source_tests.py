import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class VisualStudioMesonBridgeTests(unittest.TestCase):
    def test_solution_builds_only_the_meson_bridge(self):
        solution = (ROOT / "code/win32/msvc2017/fnql.sln").read_text(encoding="utf-8")

        self.assertIn('"FnQL (Meson)", "fnql-meson.vcxproj"', solution)
        self.assertNotIn('"libjpeg",', solution)
        self.assertNotIn('"libogg",', solution)
        self.assertNotIn('"libvorbis",', solution)
        self.assertIn("Debug|Win64.Build.0 = Debug|x64", solution)

    def test_bridge_uses_canonical_strict_build(self):
        project = (ROOT / "code/win32/msvc2017/fnql-meson.vcxproj").read_text(
            encoding="utf-8"
        )
        driver = (ROOT / "scripts/msvc_meson.py").read_text(encoding="utf-8")

        self.assertIn("<ConfigurationType>Makefile</ConfigurationType>", project)
        self.assertIn("scripts\\msvc_meson.py", project)
        self.assertIn("$(SystemRoot)\\py.exe", project)
        self.assertNotIn("<PlatformToolset>", project)
        self.assertIn('find_spec("mesonbuild.mesonmain")', driver)
        self.assertIn('shutil.which("cl")', driver)
        self.assertIn('"-Dstrict-warnings=true"', driver)
        self.assertIn(
            'DEFAULT_RENDERERS = ("glx", "vk", "rtx")',
            driver,
        )
        self.assertIn("SUPPORTED_RENDERERS = DEFAULT_RENDERERS", driver)
        self.assertIn('f"-Drenderers={\',\'.join(renderers)}"', driver)
        self.assertIn('os.environ.get("FNQL_MESON_RENDERERS"', driver)
        self.assertNotIn("code/libjpeg", driver)
        self.assertNotIn("code/libogg", driver)

    def test_component_manifests_delegate_without_stale_vendor_projects(self):
        project_dir = ROOT / "code/win32/msvc2017"
        component_projects = (
            "botlib.vcxproj",
            "fnql.vcxproj",
            "fnql-ded.vcxproj",
            "rendererglx.vcxproj",
            "renderervk.vcxproj",
            "rendererrtx.vcxproj",
        )

        for name in component_projects:
            project = (project_dir / name).read_text(encoding="utf-8")
            self.assertIn('Import Project="fnql-meson-legacy.props"', project, name)
            self.assertNotIn("<ProjectReference", project, name)

        for removed in ("libjpeg.vcxproj", "libogg.vcxproj", "libvorbis.vcxproj"):
            self.assertFalse((project_dir / removed).exists(), removed)

        client_project = (project_dir / "fnql.vcxproj").read_text(encoding="utf-8")
        self.assertNotIn(r"..\..\libjpeg", client_project)
        self.assertNotIn(r"..\..\libogg", client_project)
        self.assertNotIn(r"..\..\libvorbis", client_project)


if __name__ == "__main__":
    unittest.main()
