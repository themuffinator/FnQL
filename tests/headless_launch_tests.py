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
            self.assertLess(body.index("Com_IsWindowless()"), body.index(action))

    def test_unix_console_does_not_initialize_video(self):
        source = (ROOT / "code/unix/unix_syscon.cpp").read_text()
        body = source[source.index("static qboolean SysCon_EnsureCreated("):]
        self.assertLess(body.index("Com_IsWindowless()"), body.index("SDL_WasInit"))

    def test_prompts_and_windows_fatal_wait_are_guarded(self):
        for platform in ("win32/win_main.cpp", "unix/unix_main.cpp"):
            source = (ROOT / "code" / platform).read_text()
            body = source[source.index("sysStartupChoice_t Sys_PromptStartupChoice("):]
            self.assertIn("Com_IsWindowless() || !message", body)
        source = (ROOT / "code/win32/win_main.cpp").read_text()
        body = source[source.index("void NORETURN FORMAT_PRINTF"):]
        self.assertLess(body.index("exit( 1 )"), body.index("GetMessage("))
        common = (ROOT / "code/qcommon/common.c").read_text()
        body = common[common.index("void NORETURN FORMAT_PRINTF(2, 3) QDECL Com_Error("):]
        self.assertLess(body.index("!Com_IsWindowless()"), body.index("DebugBreak()"))
        self.assertIn("_set_error_mode( _OUT_TO_STDERR )", source)
        self.assertIn("_CrtSetReportMode( _CRT_ASSERT, _CRTDBG_MODE_FILE )", source)

    def test_windowless_client_level_keeps_the_client(self):
        # com_headless 2 is a rendering client that never shows a window; only
        # level 1 locks dedicated mode, and the crash handler still restores
        # the desktop gamma for any client.
        common = (ROOT / "code/qcommon/common.c").read_text()
        self.assertIn("headless = level == 2 ? 2 : level != 0 ? 1 : 0;", common)
        self.assertIn("return com_headless == 1 ? qtrue : qfalse;", common)
        self.assertIn("return com_headless != 0 ? qtrue : qfalse;", common)
        self.assertIn("if ( com_headless == 1 ) {\n\t\tif ( !com_dedicated->integer ) {", common)
        main = (ROOT / "code/win32/win_main.cpp").read_text()
        filter_body = main[main.index("static LONG WINAPI ExceptionFilter("):]
        self.assertIn("!Com_IsHeadless() && com_dedicated", filter_body[:600])

    def test_windowless_client_never_shows_or_changes_the_display(self):
        glimp = (ROOT / "code/win32/win_glimp.cpp").read_text()
        # Both the OpenGL and the Vulkan start-up refuse fullscreen.
        self.assertEqual(glimp.count("r_fullscreen->integer != 0 && !Com_IsWindowless() ? qtrue : qfalse"), 2)
        self.assertNotIn("cdsFullscreen = r_fullscreen->integer != 0 ? qtrue : qfalse", glimp)
        for init in ("void GLimp_Init(", "void VKimp_Init("):
            body = glimp[glimp.index(init):]
            body = body[:body.index("\n}\n")]
            self.assertLess(body.index("if ( !Com_IsWindowless() ) {"), body.index("ShowWindow( g_wv.hWnd, SW_SHOW )"))
        live = [line for line in glimp.splitlines()
                if "ShowWindow( g_wv.hWnd, SW_SHOW )" in line and not line.lstrip().startswith("//")]
        self.assertEqual(len(live), 2)
        self.assertTrue(all(line.startswith("\t\t") for line in live))
        gamma = (ROOT / "code/win32/win_gamma.cpp").read_text()
        body = gamma[gamma.index("void GLimp_InitGamma("):]
        self.assertLess(body.index("Com_IsWindowless()"), body.index("GLW_CaptureBaseline"))
        x11 = (ROOT / "code/unix/linux_glimp.cpp").read_text()
        self.assertIn("if ( !Com_IsWindowless() )\n\t\tXMapWindow( dpy, win );", x11)
        self.assertIn("while ( window_exposed == qfalse && !Com_IsWindowless() )", x11)
        body = x11[x11.index("void GLimp_InitGamma("):]
        self.assertLess(body.index("Com_IsWindowless()"), body.index("glw_state.randr_gamma"))
        self.assertIn('getenv( "FNQL_HEADLESS" )', (ROOT / "code/qcommon/common.c").read_text())
        sdl = (ROOT / "code/sdl/sdl_glimp.cpp").read_text()
        self.assertIn("SDL_WINDOW_HIDDEN ) && !Com_IsWindowless() )", sdl)


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
                    # Steam stays at its default (on), as in retail.
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

    def test_windowless_client_renders_without_showing_a_window(self):
        # Explicitly on the command line, and through FNQL_HEADLESS alone (the
        # default Meson test setup), which must reach the same policy.
        for variant, flag, extra_env in (("flag", ["+set", "com_headless", "2"], {}),
                                         ("env", [], {"FNQL_HEADLESS": "2"})):
            with self.subTest(variant=variant):
                self.run_windowless(flag, extra_env)

    def run_windowless(self, flag, extra_env):
        # com_headless 2 even with a fullscreen request: every window the
        # process owns stays hidden, the display mode and gamma stay put, and
        # the renderer still produces a real capture.
        scratch = (ROOT / ".tmp").resolve(strict=True)
        folder = Path(tempfile.mkdtemp(prefix="windowless-test-", dir=scratch))
        try:
            home = folder / "home"
            (home / "baseq3").mkdir(parents=True)
            (home / "baseq3" / "probe.cfg").write_text(
                "devmap campgrounds ffa\nwait 200\nscreenshotPNG windowless\nwait 10\n"
                "echo WINDOWLESS_OK\nquit\n", encoding="ascii")
            args = [str(self.build / "fnql.exe"), *flag,
                    "+set", "fs_basepath", str(self.retail), "+set", "fs_homepath", str(home),
                    "+set", "r_fullscreen", "1", "+set", "r_mode", "-1",
                    "+set", "r_customwidth", "640", "+set", "r_customheight", "480",
                    "+set", "s_initsound", "0", "+set", "in_mouse", "0", "+set", "sv_pure", "0",
                    "+set", "net_port", "0", "+set", "logfile", "2", "+set", "developer", "1",
                    "+exec", "probe.cfg"]
            user32 = ctypes.WinDLL("user32", use_last_error=True)
            callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
            user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
            user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
            user32.IsWindowVisible.argtypes = [wintypes.HWND]
            created, shown = set(), set()
            with (folder / "stdout.log").open("wb") as out, (folder / "stderr.log").open("wb") as err:
                process = subprocess.Popen(args, cwd=self.build, stdin=subprocess.DEVNULL,
                    stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW,
                    env={k: v for k, v in {**os.environ, **extra_env}.items()
                         if extra_env or k != "FNQL_HEADLESS"})

                @callback_type
                def collect(hwnd, _):
                    pid = wintypes.DWORD()
                    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                    if pid.value == process.pid:
                        created.add(hwnd)
                        if user32.IsWindowVisible(hwnd):
                            shown.add(hwnd)
                    return True

                try:
                    deadline = time.monotonic() + 90
                    while process.poll() is None and time.monotonic() < deadline:
                        user32.EnumWindows(collect, 0)
                        time.sleep(0.01)
                    self.assertIsNotNone(process.poll(), "windowless client hung")
                finally:
                    if process.poll() is None:
                        process.kill()
                    process.wait(timeout=5)
            log = (home / "baseq3" / "qconsole.log").read_text(errors="replace")
            self.assertTrue(created, "the client created no window to render into")
            self.assertFalse(shown, f"windowless client showed windows: {shown}")
            self.assertEqual(process.returncode, 0, log[-5000:])
            for needed in ("CM_LoadMap", "Wrote screenshots/windowless.png", "WINDOWLESS_OK"):
                self.assertIn(needed, log)
            self.assertNotIn("GAMMA: hardware", log)
            shot = (home / "baseq3" / "screenshots" / "windowless.png").read_bytes()
            self.assertEqual(shot[:8], b"\x89PNG\r\n\x1a\n")
            width, height = struct.unpack(">II", shot[16:24])
            self.assertEqual((width, height), (640, 480))
            self.assertGreater(len(shot), 50000, "the capture looks blank")
        finally:
            self.assertEqual(folder.resolve().parent, scratch)
            for path in [folder, *folder.rglob("*")]:
                self.assertFalse(path.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT,
                                 f"refusing cleanup through link: {path}")
            shutil.rmtree(folder)

    def test_early_filesystem_error_exits_without_ui_or_waiting(self):
        self.run_engine("fnql.ded.exe", ["+set", "fs_basegame", "fnql-missing-headless-fixture"],
                        [], code=1, marker="Couldn't load default.cfg")


if __name__ == "__main__":
    unittest.main()
