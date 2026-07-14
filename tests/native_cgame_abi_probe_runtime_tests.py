from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RETAIL_PATH_ENV = "FNQL_RETAIL_QL_PATH"
EXECUTABLE_ENV = "FNQL_CGAME_RUNTIME_EXE"
VSDEVCMD_ENV = "FNQL_VSDEVCMD"
RETAIL_CGAME_RELATIVE_PATH = Path("baseq3") / "cgamex86.dll"
LOG_RELATIVE_PATH = Path("baseq3") / "qconsole.log"
FIXTURE_SOURCE = ROOT / "tests" / "fixtures" / "native_cgame_abi_probe.c"
PROBE_DLL_NAME = "cgamex86.dll"
PROBE_TIMEOUT_SECONDS = 30.0
POLL_INTERVAL_SECONDS = 0.25


def find_vsdevcmd() -> Path | None:
    configured_path = os.environ.get(VSDEVCMD_ENV, "").strip()
    if configured_path:
        candidate = Path(configured_path).expanduser()
        return candidate if candidate.is_file() else None

    install_root = os.environ.get("VSINSTALLDIR", "").strip()
    candidates: list[Path] = []
    if install_root:
        candidates.append(Path(install_root) / "Common7" / "Tools" / "VsDevCmd.bat")

    program_files_roots = (
        Path(os.environ.get("ProgramFiles", r"C:\Program Files")),
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")),
    )
    for program_files in program_files_roots:
        for edition in ("BuildTools", "Community", "Professional", "Enterprise"):
            candidates.append(
                program_files
                / "Microsoft Visual Studio"
                / "2022"
                / edition
                / "Common7"
                / "Tools"
                / "VsDevCmd.bat"
            )

    return next((candidate for candidate in candidates if candidate.is_file()), None)


class NativeCGameAbiProbeRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if os.name != "nt":
            raise unittest.SkipTest("native cgame ABI probes require Windows x86")

        executable_value = os.environ.get(EXECUTABLE_ENV, "").strip()
        if not executable_value:
            raise unittest.SkipTest(
                f"set {EXECUTABLE_ENV} to an x86 FnQL executable to enable the "
                "windowed native cgame ABI probe"
            )

        cls.executable = Path(executable_value).expanduser()
        if not cls.executable.is_file():
            raise AssertionError(
                f"{EXECUTABLE_ENV} points to {cls.executable}, but that file does not exist"
            )

        configured_root = os.environ.get(RETAIL_PATH_ENV, "").strip()
        if configured_root:
            cls.retail_root = Path(configured_root).expanduser()
        else:
            program_files_x86 = Path(
                os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
            )
            cls.retail_root = (
                program_files_x86 / "Steam" / "steamapps" / "common" / "Quake Live"
            )

        if not cls.retail_root.is_dir():
            raise unittest.SkipTest(
                f"retail Quake Live not found at {cls.retail_root}; set {RETAIL_PATH_ENV} "
                "to enable this probe"
            )
        if not (cls.retail_root / RETAIL_CGAME_RELATIVE_PATH).is_file():
            raise AssertionError(
                "retail install is missing baseq3/cgamex86.dll; verify the Steam installation"
            )
        if not FIXTURE_SOURCE.is_file():
            raise AssertionError(f"synthetic native cgame fixture is missing: {FIXTURE_SOURCE}")

        cls.vsdevcmd = find_vsdevcmd()
        if cls.vsdevcmd is None:
            raise unittest.SkipTest(
                f"set {VSDEVCMD_ENV} to VsDevCmd.bat to build the x86 native cgame fixture"
            )

    def build_fixture(self, output_path: Path) -> None:
        object_path = output_path.with_suffix(".obj")
        command = (
            f'call "{self.vsdevcmd}" -arch=x86 -host_arch=x64 >nul && '
            f'cl /nologo /TC /LD /O2 /Fo:"{object_path}" /Fe:"{output_path}" "{FIXTURE_SOURCE}"'
        )
        completed = subprocess.run(
            command,
            shell=True,
            cwd=output_path.parent,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            creationflags=subprocess.CREATE_NO_WINDOW,
            check=False,
            timeout=30,
        )
        if completed.returncode != 0:
            self.fail(
                "failed to build the x86 synthetic cgame fixture; compiler output:\n"
                f"{completed.stdout[-4000:]}"
            )
        if not output_path.is_file():
            self.fail(
                "the synthetic cgame fixture compiler reported success but did not produce "
                f"{output_path}"
            )

    def test_native_cgame_uses_float_import_and_active_frame_dispatch(self) -> None:
        with tempfile.TemporaryDirectory(prefix="fnql-native-cgame-abi-") as temporary_directory:
            temporary_root = Path(temporary_directory)
            fixture = temporary_root / PROBE_DLL_NAME
            self.build_fixture(fixture)

            homepath = temporary_root / "home"
            home_game_dir = homepath / "baseq3"
            home_game_dir.mkdir(parents=True)
            shutil.copy2(fixture, home_game_dir / PROBE_DLL_NAME)
            logfile = homepath / LOG_RELATIVE_PATH
            command = [
                str(self.executable),
                "+set",
                "fs_basepath",
                str(self.retail_root),
                "+set",
                "fs_homepath",
                str(homepath),
                "+set",
                "r_fullscreen",
                "0",
                "+set",
                "r_mode",
                "-1",
                "+set",
                "logfile",
                "2",
                "+set",
                "cl_allowDownload",
                "0",
                "+set",
                "fnql_cgame_probe_float",
                "13.5",
                "+devmap",
                "campgrounds",
                "ffa",
                "+wait",
                "10",
                "+fnql_cgame_probe_command",
            ]
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
                        cgame_loads = re.findall(
                            r"VM_LoadDll\(cgame\) found native exports .*\(api 8\)",
                            log_text,
                        )
                        if (
                            len(cgame_loads) >= 2
                            and "FnQL cgame ABI probe: native float import ok" in log_text
                            and "FnQL cgame ABI probe: native cvar range imports ok" in log_text
                            and "FnQL cgame ABI probe: native filesystem imports ok" in log_text
                            and "FnQL cgame ABI probe: native parser imports ok" in log_text
                            and "FnQL cgame ABI probe: native mute imports ok" in log_text
                            and "FnQL cgame ABI probe: native glconfig import ok" in log_text
                            and "FnQL cgame ABI probe: native gamestate import ok" in log_text
                            and "FnQL cgame ABI probe: native key imports ok" in log_text
                            and "FnQL cgame ABI probe: native usercmd imports ok" in log_text
                            and "FnQL cgame ABI probe: native snapshot imports ok" in log_text
                            and "FnQL cgame ABI probe: native draw dispatch ok" in log_text
                            and "FnQL cgame ABI probe: native console command dispatch ok" in log_text
                        ):
                            break

                    exit_code = process.poll()
                    if exit_code is not None:
                        self.fail(
                            f"native cgame ABI probe exited early with code {exit_code}; "
                            f"log tail:\n{log_text[-4000:]}"
                        )
                    time.sleep(POLL_INTERVAL_SECONDS)
                else:
                    self.fail(
                        "native cgame ABI probe did not reach its float, cvar-range, filesystem, parser, mute-import, glconfig, gamestate, key, usercmd, snapshot, active-frame, and console-command "
                        f"markers within {PROBE_TIMEOUT_SECONDS:.0f} seconds; "
                        f"log tail:\n{log_text[-4000:]}"
                    )
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)

            self.assertNotIn("FnQL cgame ABI probe: native float import failed", log_text)
            self.assertNotIn("FnQL cgame ABI probe: native cvar range imports failed", log_text)
            self.assertNotIn("FnQL cgame ABI probe: native filesystem imports failed", log_text)
            self.assertNotIn("FnQL cgame ABI probe: native parser imports failed", log_text)
            self.assertNotIn("FnQL cgame ABI probe: native mute imports failed", log_text)
            self.assertNotIn("FnQL cgame ABI probe: native glconfig import failed", log_text)
            self.assertNotIn("FnQL cgame ABI probe: native gamestate import failed", log_text)
            self.assertNotIn("FnQL cgame ABI probe: native key imports failed", log_text)
            self.assertNotIn("FnQL cgame ABI probe: native usercmd imports failed", log_text)
            self.assertNotIn("FnQL cgame ABI probe: native snapshot imports failed", log_text)
            self.assertNotIn("Native cgame import table validation failed", log_text)
            self.assertNotIn("Bad cgame system trap", log_text)
            self.assertNotIn("Error: file *extern", log_text)


def main() -> None:
    unittest.main()


if __name__ == "__main__":
    main()
