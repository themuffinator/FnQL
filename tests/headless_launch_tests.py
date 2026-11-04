"""Headless platform guards and opt-in Win32 retail runtime probes."""
from __future__ import annotations

import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HeadlessBoundaryTests(unittest.TestCase):
    def test_windows_ui_entry_points_are_guarded(self):
        source = (ROOT / "code/win32/win_syscon.cpp").read_text()
        for name, action in (("Sys_CreateConsole", "RegisterClass"),
                             ("Sys_ShowConsole", "ShowWindow"),
                             ("Sys_SetErrorText", "CreateWindow"),
                             ("Conbuf_AppendText", "SetTimer")):
            body = source[source.index("void " + name + "("):]
            self.assertLess(body.index("Com_IsHeadless()"), body.index(action))

    def test_unix_console_does_not_initialize_video(self):
        source = (ROOT / "code/unix/unix_syscon.cpp").read_text()
        body = source[source.index("static qboolean SysCon_EnsureCreated("):]
        self.assertLess(body.index("Com_IsHeadless()"), body.index("SDL_WasInit"))

    def test_prompts_and_windows_fatal_wait_are_guarded(self):
        for platform in ("win32/win_main.cpp", "unix/unix_main.cpp"):
            source = (ROOT / "code" / platform).read_text()
            body = source[source.index("sysStartupChoice_t Sys_PromptStartupChoice("):]
            self.assertIn("Com_IsHeadless() || !message", body)
        source = (ROOT / "code/win32/win_main.cpp").read_text()
        body = source[source.index("void NORETURN FORMAT_PRINTF"):]
        self.assertLess(body.index("exit( 1 )"), body.index("GetMessage("))
        common = (ROOT / "code/qcommon/common.c").read_text()
        body = common[common.index("void NORETURN FORMAT_PRINTF(2, 3) QDECL Com_Error("):]
        self.assertLess(body.index("!Com_IsHeadless()"), body.index("DebugBreak()"))
        self.assertIn("_set_error_mode( _OUT_TO_STDERR )", source)
        self.assertIn("_CrtSetReportMode( _CRT_ASSERT, _CRTDBG_MODE_FILE )", source)


@unittest.skipUnless(os.name == "nt" and os.environ.get("FNQL_HEADLESS_RUNTIME_DIR"),
                     "set FNQL_HEADLESS_RUNTIME_DIR to an x86 Windows build")
class HeadlessRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = Path(os.environ["FNQL_HEADLESS_RUNTIME_DIR"]).resolve(strict=True)
        cls.server = Path(os.environ.get("FNQL_HEADLESS_SERVER_EXE",
                                        str(cls.build / "fnql.ded.exe"))).resolve(strict=True)
        cls.retail = Path(os.environ.get("FNQL_RETAIL_QL_PATH",
            r"C:\Program Files (x86)\Steam\steamapps\common\Quake Live")).resolve(strict=True)
        for executable in (cls.build / "fnql.exe", cls.server):
            data = executable.read_bytes()
            pe = struct.unpack_from("<I", data, 0x3c)[0]
            if data[pe:pe + 4] != b"PE\0\0" or struct.unpack_from("<H", data, pe + 4)[0] != 0x14c:
                raise AssertionError(f"{executable} must be x86")

    def run_engine(self, name, options, commands, code=0, marker="HEADLESS_OK"):
        scratch = (ROOT / ".tmp").resolve(strict=True)
        folder = Path(tempfile.mkdtemp(prefix="headless-test-", dir=scratch))
        try:
            args = [str(self.server if name == "fnql.ded.exe" else self.build / name),
                    "+set", "fs_basepath", str(self.retail),
                    "+set", "fs_homepath", str(folder / "home"),
                    "+set", "r_fullscreen", "0",
                    "+set", "com_steamIntegration", "0",
                    "+set", "net_ip", "127.0.0.1",
                    "+set", "net_port", "0",
                    "+set", "developer", "1", *options, *commands]
            observed = set()
            user32 = ctypes.WinDLL("user32", use_last_error=True)
            callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
            user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
            user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
            with (folder / "stdout.log").open("wb") as out, (folder / "stderr.log").open("wb") as err:
                process = subprocess.Popen(args, cwd=self.build, stdin=subprocess.DEVNULL,
                    stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)

                @callback_type
                def collect(hwnd, _):
                    pid = wintypes.DWORD()
                    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                    if pid.value == process.pid:
                        observed.add(hwnd)
                    return True

                try:
                    deadline = time.monotonic() + 45
                    while process.poll() is None and time.monotonic() < deadline:
                        user32.EnumWindows(collect, 0)
                        time.sleep(0.01)
                    self.assertIsNotNone(process.poll(), "headless process hung")
                finally:
                    if process.poll() is None:
                        process.kill()
                    process.wait(timeout=5)
            output = (folder / "stdout.log").read_text(errors="replace")
            errors = (folder / "stderr.log").read_text(errors="replace")
            self.assertFalse(observed, f"process created windows: {observed}")
            self.assertEqual(process.returncode, code, (output + errors)[-5000:])
            self.assertIn(marker, output + errors)
            self.assertNotIn("Initializing OpenGL", output)
            self.assertNotIn("Initializing Vulkan", output)
            self.assertNotIn("SDL_InitSubSystem(SDL_INIT_AUDIO)", output)
            return output, errors
        finally:
            # Only remove this probe's independent files inside the project.
            self.assertEqual(folder.resolve().parent, scratch)
            for path in [folder, *folder.rglob("*")]:
                self.assertFalse(path.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT,
                                 f"refusing cleanup through link: {path}")
            shutil.rmtree(folder)

    def test_dedicated_defaults_to_headless_and_keeps_retail_map_hosting(self):
        output, _ = self.run_engine("fnql.ded.exe", [],
            ["+map", "campgrounds", "ffa", "+wait", "30", "+status", "+echo", "HEADLESS_OK", "+quit"])
        self.assertIn("map: campgrounds", output)

    def test_client_dedicated_launch_stays_headless_despite_console_requests(self):
        self.run_engine("fnql.exe", ["+set", "dedicated", "2", "+set", "viewlog", "1"],
            ["+wait", "10", "+dedicated", "0", "+com_headless", "0",
             "+echo", "HEADLESS_OK", "+quit"])

    def test_explicit_headless_forces_dedicated_mode(self):
        output, _ = self.run_engine("fnql.exe",
            ["+set", "com_headless", "0", "+set", "COM_HEADLESS", "1", "+set", "dedicated", "0"],
            ["+dedicated", "+wait", "10", "+echo", "HEADLESS_OK", "+quit"])
        self.assertIn('"dedicated" is:"1', output)

    def test_fatal_error_exits_without_ui_or_waiting(self):
        for name, options in (("fnql.ded.exe", []),
                              ("fnql.exe", ["+set", "dedicated", "1"])):
            with self.subTest(executable=name):
                _, errors = self.run_engine(name, options, ["+error"], code=1,
                                            marker="Testing fatal error")
                self.assertIn("Testing fatal error", errors)

    def test_early_filesystem_error_exits_without_ui_or_waiting(self):
        self.run_engine("fnql.ded.exe", ["+set", "fs_basegame", "fnql-missing-headless-fixture"],
                        [], code=1, marker="Couldn't load default.cfg")


if __name__ == "__main__":
    unittest.main()
