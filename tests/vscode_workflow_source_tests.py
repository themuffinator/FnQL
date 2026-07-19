import json
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
VSCODE = ROOT / ".vscode"


class VSCodeWorkflowSourceTests(unittest.TestCase):
    def test_release_helpers_expect_fnql_artifacts(self):
        build = (VSCODE / "build-release.ps1").read_text(encoding="utf-8")
        launch = (VSCODE / "launch-release.ps1").read_text(encoding="utf-8")

        self.assertIn('"fnql$suffix.exe"', build)
        self.assertIn('"fnql.ded$suffix.exe"', build)
        self.assertIn('"fnql_${rendererName}_${rendererArch}.dll"', build)
        self.assertIn("'FnQL-pkg.fnz'", build)
        self.assertIn("$canonicalRootArchiveName", build)
        self.assertIn("Copy-Item -LiteralPath $canonicalArchivePath", build)
        self.assertIn("Root archive name must be a .fnz file name", build)
        self.assertIn("'-Dstrict-warnings=true'", build)
        self.assertIn("[ValidateSet('Win32')]", build)
        self.assertIn("[ValidateSet('Win32')]", launch)
        self.assertIn("'meson\\build\\win32'", build)
        self.assertIn("'meson\\build\\win32'", launch)
        self.assertIn("if (-not $RunTests)", build)
        self.assertIn("$compileTargets.Add('fnql')", build)
        self.assertIn('$compileTargets.Add("fnql_${rendererName}_x86")', build)
        self.assertIn("$compileTargets.Add('fnql-web.pak')", build)
        self.assertIn("$compileTargets.Add('FnQL-pkg.fnz')", build)
        self.assertIn("function Remove-StaleNativeTestBinaries", build)
        self.assertIn("Remove-StaleNativeTestBinaries -SelectedBuildPath $buildPath", build)
        self.assertIn("'-DBUILD_TESTING=OFF'", build)
        self.assertIn("--target fnql_steam", build)
        provider_build = build[
            build.index("function Invoke-FnQLSteamBuild") :
            build.index("$workspaceRoot =", build.index("function Invoke-FnQLSteamBuild"))
        ]
        self.assertNotIn("fnql_steam_abi_tests", provider_build)
        self.assertNotIn("fnql_steam_provider_tests", provider_build)
        self.assertNotIn("ctest", provider_build.lower())
        self.assertIn("$env:FNQL_MESON_BUILD_DIR", build)
        self.assertIn("$env:FNQL_MESON_BUILD_DIR", launch)
        self.assertNotIn("fnquake3", build.lower())
        self.assertNotIn("fnquake3", launch.lower())

    def test_tasks_and_debuggers_use_fnql_artifacts(self):
        tasks = json.loads((VSCODE / "tasks.json").read_text(encoding="utf-8"))
        launch = json.loads((VSCODE / "launch.json").read_text(encoding="utf-8"))
        serialized = json.dumps((tasks, launch)).lower()

        self.assertIn("win32-debug", serialized)
        self.assertIn("fnql.exe", serialized)
        self.assertIn("retail ql / win32", serialized)
        self.assertIn("meson: build win32 debug", serialized)
        self.assertNotIn("fnql.x64.exe", serialized)
        self.assertNotIn("fnql.ded.x64.exe", serialized)
        self.assertNotIn("arm64", serialized)
        self.assertNotIn('"-platform", "x64"', serialized)
        self.assertNotIn("meson: test", serialized)
        for task in tasks["tasks"]:
            if not task.get("label", "").startswith("meson: build"):
                continue
            platform_index = task["args"].index("-Platform")
            self.assertEqual(task["args"][platform_index + 1], "Win32")
            self.assertNotIn("-RunTests", task["args"])
        self.assertNotIn("fnquake3", serialized)


if __name__ == "__main__":
    unittest.main()
