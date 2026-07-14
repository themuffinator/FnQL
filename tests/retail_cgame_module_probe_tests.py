from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path


RETAIL_PATH_ENV = "FNQL_RETAIL_QL_PATH"
EXECUTABLE_ENV = "FNQL_CGAME_RUNTIME_EXE"
RETAIL_CGAME_RELATIVE_PATH = Path("baseq3") / "cgamex86.dll"
LOG_RELATIVE_PATH = Path("baseq3") / "qconsole.log"
PROBE_TIMEOUT_SECONDS = 30.0
POLL_INTERVAL_SECONDS = 0.25
POST_INITIALIZATION_STABILITY_SECONDS = 1.0


class RetailCGameModuleProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if os.name != "nt":
            raise unittest.SkipTest("retail cgame module probes require Windows x86")

        executable_value = os.environ.get(EXECUTABLE_ENV, "").strip()
        if not executable_value:
            raise unittest.SkipTest(
                f"set {EXECUTABLE_ENV} to an x86 FnQL executable to enable the "
                "windowed retail cgame module probe"
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

        retail_cgame = cls.retail_root / RETAIL_CGAME_RELATIVE_PATH
        if not retail_cgame.is_file():
            raise AssertionError(
                f"retail install is missing {retail_cgame}; verify the Steam installation"
            )

    def test_windowed_local_map_reaches_retail_cgame_initialization(self) -> None:
        with tempfile.TemporaryDirectory(prefix="fnql-retail-cgame-") as temporary_directory:
            homepath = Path(temporary_directory) / "home"
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
                "+devmap",
                "campgrounds",
                "ffa",
            ]
            creationflags = subprocess.CREATE_NO_WINDOW
            process = subprocess.Popen(
                command,
                cwd=self.executable.parent,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                creationflags=creationflags,
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
                        if len(cgame_loads) >= 2 and "CL_InitCGame:" in log_text:
                            break

                    exit_code = process.poll()
                    if exit_code is not None:
                        self.fail(
                            f"retail cgame probe exited early with code {exit_code}; "
                            f"log tail:\n{log_text[-4000:]}"
                        )
                    time.sleep(POLL_INTERVAL_SECONDS)
                else:
                    self.fail(
                        "retail cgame probe did not reach two API-8 cgame loads and "
                        f"CL_InitCGame within {PROBE_TIMEOUT_SECONDS:.0f} seconds; "
                        f"log tail:\n{log_text[-4000:]}"
                    )

                stability_deadline = time.monotonic() + POST_INITIALIZATION_STABILITY_SECONDS
                while time.monotonic() < stability_deadline:
                    exit_code = process.poll()
                    if exit_code is not None:
                        self.fail(
                            "retail cgame probe exited during post-initialization stability "
                            f"window with code {exit_code}; log tail:\n{log_text[-4000:]}"
                        )
                    time.sleep(POLL_INTERVAL_SECONDS)

                if logfile.is_file():
                    log_text = logfile.read_text(encoding="utf-8", errors="replace")
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)

            self.assertNotIn("Native cgame import table validation failed", log_text)
            self.assertNotIn("retail cgame import slot", log_text)
            self.assertNotIn("retail cgame reserved import slot", log_text)
            self.assertNotIn("Bad cgame system trap", log_text)


def main() -> None:
    unittest.main()


if __name__ == "__main__":
    main()
