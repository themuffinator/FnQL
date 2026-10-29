from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import time
import unittest
import zipfile
from pathlib import Path

from native_cgame_abi_probe_runtime_tests import RETAIL_PATH_ENV, find_vsdevcmd


ROOT = Path(__file__).resolve().parents[1]
EXECUTABLE_ENV = "FNQL_DEDICATED_RUNTIME_EXE"
FIXTURE_SOURCE = ROOT / "tests" / "fixtures" / "fnql_game_provider_probe.c"
FNQL_MODULE_NAME = "fnql_qagamex86.dll"
LOG_RELATIVE_PATH = Path("baseq3") / "qconsole.log"
PROBE_TIMEOUT_SECONDS = 45.0
POLL_INTERVAL_SECONDS = 0.25


class GameModuleProviderRuntimeTests(unittest.TestCase):
    """Drives vm_gameProvider through a real x86 dedicated server.

    Opt-in: set FNQL_DEDICATED_RUNTIME_EXE to an x86 fnql.ded.exe and make a
    retail Quake Live install reachable (FNQL_RETAIL_QL_PATH).  The fixture
    stands in for an FnQL-game qagame so the engine is tested on its own.
    """

    @classmethod
    def setUpClass(cls) -> None:
        if os.name != "nt":
            raise unittest.SkipTest("the qagame provider probe requires Windows x86")

        executable_value = os.environ.get(EXECUTABLE_ENV, "").strip()
        if not executable_value:
            raise unittest.SkipTest(
                f"set {EXECUTABLE_ENV} to an x86 fnql.ded.exe to enable the qagame provider probe"
            )
        cls.executable = Path(executable_value).expanduser()
        if not cls.executable.is_file():
            raise AssertionError(f"{EXECUTABLE_ENV} points to {cls.executable}, which does not exist")

        configured_root = os.environ.get(RETAIL_PATH_ENV, "").strip()
        if configured_root:
            cls.retail_root = Path(configured_root).expanduser()
        else:
            program_files_x86 = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
            cls.retail_root = program_files_x86 / "Steam" / "steamapps" / "common" / "Quake Live"
        if not (cls.retail_root / "baseq3" / "pak00.pk3").is_file():
            raise unittest.SkipTest(
                f"retail Quake Live not found at {cls.retail_root}; set {RETAIL_PATH_ENV}"
            )

        cls.vsdevcmd = find_vsdevcmd()
        if cls.vsdevcmd is None:
            raise unittest.SkipTest("VsDevCmd.bat is required to build the x86 qagame fixture")

        # One developer-environment call builds every fixture variant.
        cls.fixture_root = tempfile.TemporaryDirectory(prefix="fnql-game-provider-fixtures-")
        cls.fixtures = cls.build_fixtures(Path(cls.fixture_root.name))

    @classmethod
    def tearDownClass(cls) -> None:
        fixture_root = getattr(cls, "fixture_root", None)
        if fixture_root is not None:
            fixture_root.cleanup()

    @classmethod
    def build_fixtures(cls, root: Path) -> dict[str, Path]:
        variants = {
            "identified": "",
            "unidentified": "/DPROBE_NO_IDENTITY",
            "wrong-module": "/DPROBE_WRONG_MODULE",
        }
        steps = []
        outputs: dict[str, Path] = {}
        for variant, define in variants.items():
            variant_dir = root / variant
            variant_dir.mkdir()
            output = variant_dir / FNQL_MODULE_NAME
            outputs[variant] = output
            steps.append(
                f'cl /nologo /TC /LD /O2 {define} /Fo:"{output.with_suffix(".obj")}" '
                f'/Fe:"{output}" "{FIXTURE_SOURCE}"'
            )
        command = f'call "{cls.vsdevcmd}" -arch=x86 -host_arch=x64 >nul && ' + " && ".join(steps)
        completed = subprocess.run(
            command,
            shell=True,
            cwd=root,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            creationflags=subprocess.CREATE_NO_WINDOW,
            check=False,
            timeout=300,
        )
        missing = [str(path) for path in outputs.values() if not path.is_file()]
        if completed.returncode != 0 or missing:
            raise AssertionError(
                f"failed to build the x86 qagame fixtures {missing}:\n{completed.stdout[-4000:]}"
            )
        return outputs

    def run_server(self, homepath: Path, provider: str | None, wanted: list[str],
            extra_commands: tuple[str, ...] = ()) -> str:
        logfile = homepath / LOG_RELATIVE_PATH
        command = [
            str(self.executable),
            "+set", "fs_basepath", str(self.retail_root),
            "+set", "fs_homepath", str(homepath),
            "+set", "logfile", "2",
            "+set", "net_port", "27999",
            "+set", "sv_pure", "0",
        ]
        if provider is not None:
            command += ["+set", "vm_gameProvider", provider]
        command += ["+map", "campgrounds", "ffa", "+vminfo", *extra_commands]

        process = subprocess.Popen(
            command,
            cwd=self.executable.parent,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            creationflags=subprocess.CREATE_NO_WINDOW,
        )
        log_text = ""
        try:
            deadline = time.monotonic() + PROBE_TIMEOUT_SECONDS
            while time.monotonic() < deadline:
                if logfile.is_file():
                    log_text = logfile.read_text(encoding="utf-8", errors="replace")
                    if all(line in log_text for line in wanted):
                        break
                if process.poll() is not None:
                    break
                time.sleep(POLL_INTERVAL_SECONDS)
        finally:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=10)
            if logfile.is_file():
                log_text = logfile.read_text(encoding="utf-8", errors="replace")

        missing = [line for line in wanted if line not in log_text]
        if missing:
            self.fail(f"server log is missing {missing!r}; log tail:\n{log_text[-6000:]}")
        return log_text

    def make_home(self, root: Path, variant: str) -> Path:
        homepath = root / "home"
        game_dir = homepath / "baseq3"
        game_dir.mkdir(parents=True)
        shutil.copy2(self.fixtures[variant], game_dir / FNQL_MODULE_NAME)
        return homepath

    def test_fnql_provider_loads_identified_module(self) -> None:
        with tempfile.TemporaryDirectory(prefix="fnql-game-provider-") as temporary:
            homepath = self.make_home(Path(temporary), "identified")
            log = self.run_server(homepath, "fnql", [
                "VM_LoadDll(qagame): FnQL-game 0.0.0-probe module (capabilities 0x0)",
                "FnQL game provider probe: init ok",
                "FnQL game provider probe: frames ok",
                "qagame : native fnql_qagamex86.dll (FnQL-game 0.0.0-probe, capabilities 0x0)",
            ])
            self.assertIn("does not declare retail wire parity yet", log)
            self.assertNotIn("loading the retail module instead", log)

    def test_fnql_provider_rejects_unidentified_module_and_falls_back(self) -> None:
        with tempfile.TemporaryDirectory(prefix="fnql-game-provider-") as temporary:
            homepath = self.make_home(Path(temporary), "unidentified")
            log = self.run_server(homepath, "fnql", [
                "'fnql_qagamex86.dll' is not a usable FnQL-game qagame module; ignoring it.",
                "vm_gameProvider fnql: no usable FnQL-game qagame module; loading the retail module instead.",
                "qagame : native qagamex86.dll (unidentified)",
            ])
            self.assertNotIn("FnQL game provider probe: init ok", log)

    def test_fnql_provider_rejects_mismatched_identity(self) -> None:
        with tempfile.TemporaryDirectory(prefix="fnql-game-provider-") as temporary:
            homepath = self.make_home(Path(temporary), "wrong-module")
            log = self.run_server(homepath, "fnql", [
                "has a malformed or mismatched FnQL_GetModuleInfo record.",
                "qagame : native qagamex86.dll (unidentified)",
            ])
            self.assertNotIn("FnQL game provider probe: init ok", log)

    def test_default_provider_keeps_the_retail_module(self) -> None:
        with tempfile.TemporaryDirectory(prefix="fnql-game-provider-") as temporary:
            homepath = self.make_home(Path(temporary), "identified")
            log = self.run_server(homepath, None, [
                "Loading dll file qagame.",
                "qagame : native qagamex86.dll (unidentified)",
            ])
            self.assertNotIn("Loading dll file fnql_qagame.", log)
            self.assertNotIn("FnQL game provider probe", log)

    def test_packaged_server_module_is_never_referenced_to_clients(self) -> None:
        with tempfile.TemporaryDirectory(prefix="fnql-game-provider-") as temporary:
            homepath = Path(temporary) / "home"
            game_dir = homepath / "baseq3"
            game_dir.mkdir(parents=True)
            # Served only from a package: there is no loose module to fall back on.
            with zipfile.ZipFile(game_dir / "zz-fnql-game-server.pk3", "w") as package:
                package.write(self.fixtures["identified"], FNQL_MODULE_NAME)

            log = self.run_server(homepath, "fnql", [
                "VM_LoadDll(qagame): FnQL-game 0.0.0-probe module (capabilities 0x0)",
                "FnQL game provider probe: init ok",
                '"sv_referencedPakNames" is:',
            ], extra_commands=("+sv_referencedPakNames",))

            referenced = next(
                line for line in log.splitlines() if '"sv_referencedPakNames" is:' in line
            )
            self.assertIn("baseq3/bin", referenced)
            self.assertNotIn("zz-fnql-game-server", referenced)

    def test_packaged_retail_named_module_keeps_historical_reference(self) -> None:
        # Control for the test above: the exemption covers only the FnQL-game
        # server module, so the same package under the retail module name is
        # still advertised exactly as before providers existed.
        with tempfile.TemporaryDirectory(prefix="fnql-game-provider-") as temporary:
            homepath = Path(temporary) / "home"
            game_dir = homepath / "baseq3"
            game_dir.mkdir(parents=True)
            with zipfile.ZipFile(game_dir / "zz-retail-named.pk3", "w") as package:
                package.write(self.fixtures["identified"], "qagamex86.dll")

            log = self.run_server(homepath, None, [
                "FnQL game provider probe: init ok",
                '"sv_referencedPakNames" is:',
            ], extra_commands=("+sv_referencedPakNames",))

            referenced = next(
                line for line in log.splitlines() if '"sv_referencedPakNames" is:' in line
            )
            self.assertIn("zz-retail-named", referenced)


if __name__ == "__main__":
    unittest.main()
