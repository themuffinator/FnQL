from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


class X86BuildConstraintTests(unittest.TestCase):
    def test_meson_rejects_every_non_x86_target_before_naming_outputs(self) -> None:
        meson = read("meson.build")

        guard = meson.index("if cpu_family != 'x86'")
        output_names = meson.index("client_binary_name =")
        self.assertLess(guard, output_names)
        self.assertIn("cc.compiles(fnql_x86_target_probe", meson[:guard])
        self.assertIn("sizeof(void *) == 4", meson[:guard])
        self.assertIn("FnQL supports only 32-bit x86 targets", meson[guard:output_names])
        self.assertIn("renderer_arch = 'x86'", meson[guard:output_names])
        self.assertIn("binary_suffix = ''", meson[guard:output_names])
        self.assertNotIn("elif cpu_family", meson[guard:output_names])

    def test_cmake_compiler_probe_rejects_non_x86_or_non_32_bit_targets(self) -> None:
        cmake = read("CMakeLists.txt")

        project = cmake.index("PROJECT(FnQL")
        guard = cmake.index("IF(NOT FNQL_TARGET_IS_32BIT_X86)")
        dependency_search = cmake.index("find_package(Threads REQUIRED)")
        self.assertLess(project, guard)
        self.assertLess(guard, dependency_search)
        self.assertIn("TRY_COMPILE(FNQL_TARGET_IS_32BIT_X86", cmake)
        self.assertNotIn("CHECK_C_SOURCE_COMPILES", cmake[:guard])
        self.assertIn("CMAKE_REQUIRED_FLAGS are", cmake[:guard])
        self.assertNotIn("SET(CMAKE_REQUIRED_FLAGS", cmake[project:guard])
        self.assertNotIn("UNSET(CMAKE_REQUIRED_FLAGS", cmake[project:guard])
        self.assertIn("!defined(_M_IX86) && !defined(__i386__)", cmake)
        self.assertIn("sizeof(void *) == 4", cmake)
        self.assertIn("UNSET(FNQL_TARGET_IS_32BIT_X86 CACHE)", cmake)
        self.assertIn("FnQL supports only 32-bit x86 targets", cmake)
        self.assertIn("SET(RENDEXT _x86)", cmake)

    def test_cmake_probe_ignores_check_only_architecture_flags(self) -> None:
        cmake = shutil.which("cmake")
        ninja = shutil.which("ninja")
        compiler = shutil.which("clang") or shutil.which("gcc")
        if cmake is None or ninja is None or compiler is None:
            self.skipTest("CMake, Ninja, or a native C compiler is unavailable")

        compiler_name = Path(compiler).stem.lower()
        cxx_compiler = (
            shutil.which("clang++") if "clang" in compiler_name else shutil.which("g++")
        )
        if cxx_compiler is None:
            self.skipTest("a matching native C++ compiler is unavailable")

        with tempfile.TemporaryDirectory(prefix="fnql-cmake-x86-guard-") as temp:
            target_probe = Path(temp) / "native_target_probe.c"
            target_probe.write_text(
                "#if defined(_M_IX86) || defined(__i386__)\n"
                "#error selected compiler already targets 32-bit x86\n"
                "#endif\n"
                "typedef char fnql_test_requires_64_bit_pointer"
                "[(sizeof(void *) == 8) ? 1 : -1];\n"
            )
            native = subprocess.run(
                [compiler, "-fsyntax-only", str(target_probe)],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
            if native.returncode != 0:
                self.skipTest("selected compiler does not target a 64-bit ABI")

            build_dir = Path(temp) / "build"
            configured = subprocess.run(
                [
                    cmake,
                    "-S",
                    str(ROOT),
                    "-B",
                    str(build_dir),
                    "-G",
                    "Ninja",
                    f"-DCMAKE_MAKE_PROGRAM={ninja}",
                    f"-DCMAKE_C_COMPILER={compiler}",
                    f"-DCMAKE_CXX_COMPILER={cxx_compiler}",
                    "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
                    "-DCMAKE_REQUIRED_FLAGS=-m32",
                ],
                cwd=ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
                timeout=60,
            )
            cache = (build_dir / "CMakeCache.txt").read_text(
                encoding="utf-8", errors="replace"
            )

        self.assertNotEqual(configured.returncode, 0, configured.stdout)
        self.assertIn("FnQL supports only 32-bit x86 targets", configured.stdout)
        self.assertIn("CMAKE_REQUIRED_FLAGS:UNINITIALIZED=-m32", cache)

    def test_make_rejects_builds_but_allows_cleanup_from_other_hosts(self) -> None:
        makefile = read("Makefile")

        self.assertIn("override FNQL_ARCH_NEUTRAL_GOALS :=", makefile)
        self.assertIn("override FNQL_ARCH_SENSITIVE_GOALS :=", makefile)
        self.assertIn("override FNQL_ENFORCE_X86 := 1", makefile)
        self.assertIn("ifeq ($(FNQL_ENFORCE_X86),1)", makefile)
        self.assertIn("FnQL supports only 32-bit x86 targets", makefile)
        self.assertIn("MAKECMDGOALS is managed by GNU Make", makefile)
        for variable in (
            "BASE_CFLAGS",
            "CFLAGS",
            "CXXFLAGS",
            "LDFLAGS",
            "SHLIBLDFLAGS",
        ):
            self.assertIn(f"override {variable} += $(FNQL_REQUIRED_ARCH_", makefile)

        make = shutil.which("make")
        if make is None and (candidate := Path(r"C:\msys64\usr\bin\make.exe")).is_file():
            make = str(candidate)
        if make is None:
            self.skipTest("GNU Make is unavailable")

        empty_build = "BUILD_DIR=.tmp/x86-make-regression-nonexistent"
        # Cleanup goals demand no build inputs, and the goal/architecture guards
        # run before toolchain detection, so these three probes hold on any host.
        cleanup = subprocess.run(
            [make, "-n", "clean", "ARCH=x86_64", "FNQL_ENFORCE_X86=", empty_build],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        self.assertEqual(cleanup.returncode, 0, cleanup.stdout)

        build = subprocess.run(
            [
                make,
                "-n",
                "release",
                "ARCH=x86_64",
                "FNQL_ENFORCE_X86=",
                empty_build,
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        self.assertNotEqual(build.returncode, 0, build.stdout)
        self.assertIn("FnQL supports only 32-bit x86 targets", build.stdout)

        disguised = subprocess.run(
            [
                make,
                "-n",
                "release",
                "ARCH=x86_64",
                "MAKECMDGOALS=clean",
                empty_build,
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        self.assertNotEqual(disguised.returncode, 0, disguised.stdout)
        self.assertIn("MAKECMDGOALS is managed by GNU Make", disguised.stdout)

        # The remaining probes evaluate an accepted x86 configuration all the
        # way through toolchain detection, so they need a Make-visible 32-bit
        # x86 compiler and the downloaded Meson subprojects. Hosts without them
        # cannot exercise the accepted path at all; the rejection probes above
        # already proved the policy.
        toolchain = subprocess.run(
            [
                make,
                "-n",
                "fnql-arch-toolchain-probe",
                "ARCH=x86",
                empty_build,
                "--eval=fnql-arch-toolchain-probe: ; @:",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        if toolchain.returncode != 0:
            self.skipTest(
                "an x86 Make build cannot be configured on this host: "
                f"{toolchain.stdout.strip().splitlines()[-1] if toolchain.stdout.strip() else ''}"
            )

        default_goal = subprocess.run(
            [make, "-q", "ARCH=x86", empty_build],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        self.assertIn(default_goal.returncode, (0, 1), default_goal.stdout)
        self.assertNotIn("MAKECMDGOALS is managed by GNU Make", default_goal.stdout)

        flag_probe = subprocess.run(
            [
                make,
                "-n",
                "fnql-arch-flag-probe",
                "ARCH=x86",
                "BASE_CFLAGS=",
                "CFLAGS=",
                "CXXFLAGS=",
                "LDFLAGS=",
                "SHLIBLDFLAGS=",
                empty_build,
                "--eval=fnql-arch-flag-probe: ; @echo "
                "FNQL_ARCH_FLAGS "
                "BASE=$(BASE_CFLAGS) C=$(CFLAGS) CXX=$(CXXFLAGS) "
                "LINK=$(LDFLAGS) SHLIB=$(SHLIBLDFLAGS)",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        self.assertEqual(flag_probe.returncode, 0, flag_probe.stdout)
        probe_line = next(
            (line for line in flag_probe.stdout.splitlines() if "FNQL_ARCH_FLAGS" in line),
            "",
        )
        self.assertTrue(probe_line, flag_probe.stdout)
        required_flag = "-arch i386" if "-arch i386" in probe_line else "-m32"
        self.assertGreaterEqual(probe_line.count(required_flag), 5, probe_line)

    def test_central_platform_header_rejects_a_native_64_bit_compiler(self) -> None:
        platform = read("code/qcommon/q_platform.h")

        self.assertIn("!defined( _M_IX86 ) && !defined( __i386__ )", platform)
        self.assertIn("sizeof( void * ) == 4", platform)
        self.assertIn("FnQL supports only 32-bit x86 engine targets", platform)
        compiler = shutil.which("clang") or shutil.which("gcc")
        if compiler is None:
            self.skipTest("a native C compiler is unavailable")

        with tempfile.TemporaryDirectory(prefix="fnql-x86-guard-") as temp:
            target_probe = Path(temp) / "target_probe.c"
            target_probe.write_text(
                "#if defined(_M_IX86) || defined(__i386__)\n"
                "#error selected compiler targets 32-bit x86\n"
                "#endif\n"
                "typedef char fnql_test_requires_64_bit_pointer"
                "[(sizeof(void *) == 8) ? 1 : -1];\n"
            )
            probed = subprocess.run(
                [compiler, "-fsyntax-only", str(target_probe)],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
            if probed.returncode != 0:
                self.skipTest("selected compiler does not target a 64-bit ABI")

            source = Path(temp) / "guard_probe.c"
            source.write_text('#include "q_platform.h"\nint main(void) { return 0; }\n')
            compiled = subprocess.run(
                [
                    compiler,
                    "-fsyntax-only",
                    "-I",
                    str(ROOT / "code/qcommon"),
                    str(source),
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
        self.assertNotEqual(compiled.returncode, 0, compiled.stdout)
        self.assertIn("FnQL supports only 32-bit x86 engine targets", compiled.stdout)

    def test_visual_studio_direct_targets_reject_unsupported_platforms(self) -> None:
        if os.name != "nt":
            self.skipTest("MSBuild probe is Windows-only")

        candidates = []
        if found := shutil.which("msbuild"):
            candidates.append(Path(found))
        for program_files in (
            os.environ.get("ProgramFiles"),
            os.environ.get("ProgramFiles(x86)"),
        ):
            if program_files:
                candidates.extend(
                    Path(program_files).glob(
                        "Microsoft Visual Studio/*/*/MSBuild/Current/Bin/MSBuild.exe"
                    )
                )
        msbuild = next((candidate for candidate in candidates if candidate.is_file()), None)
        if msbuild is None:
            self.skipTest("MSBuild is unavailable")

        cases = (
            ("code/win32/msvc2017/fnql.vcxproj", "ClCompile", "x64", None),
            ("code/win32/msvc2017/fnql.vcxproj", "Link", "ARM64", None),
            ("code/win32/msvc2017/fnql.vcxproj", "ClCompile", "Win32", "x64"),
            (
                "code/win32/msvc2017/fnql-meson.vcxproj",
                "PrepareForNMakeBuild",
                "x64",
                None,
            ),
        )
        for relative_project, target, platform, platform_target in cases:
            command = [
                str(msbuild),
                str(ROOT / relative_project),
                "/nologo",
                "/nodeReuse:false",
                "/v:minimal",
                f"/t:{target}",
                "/p:Configuration=Release",
                f"/p:Platform={platform}",
                "/p:CLToolExe=fnql-probe-must-not-compile.exe",
                "/p:LinkToolExe=fnql-probe-must-not-link.exe",
            ]
            if platform_target is not None:
                command.append(f"/p:PlatformTarget={platform_target}")
            rejected = subprocess.run(
                command,
                cwd=ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
                timeout=30,
            )
            self.assertNotEqual(rejected.returncode, 0, rejected.stdout)
            self.assertIn(
                "FnQL supports only the Win32 (32-bit x86) platform",
                rejected.stdout,
            )

    def test_compatibility_roadmap_does_not_claim_unsupported_builds(self) -> None:
        roadmap = read("docs/fnql/QL_COMPATIBILITY_ROADMAP.md")

        for stale_claim in (
            "MSVC x86/x64",
            "Native Intel and Apple-Silicon CI",
            "native x86_64/arm64 CI",
        ):
            self.assertNotIn(stale_claim, roadmap)
        self.assertIn("x86-only configuration gates", roadmap)
        self.assertIn("macOS is not a current FnQL build or runtime target", roadmap)


if __name__ == "__main__":
    unittest.main()
