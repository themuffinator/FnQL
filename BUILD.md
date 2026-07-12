## Build Instructions

Meson is the preferred build path. It builds a single client executable and
places external renderer modules beside it, so renderer selection stays a
runtime `\cl_renderer` choice instead of a reason to produce separate client
executables.

FnQL is currently an early Quake Live migration baseline. A successful build is
not yet proof of full retail Quake Live compatibility; runtime work should be
validated against a legitimate Steam installation.

Steam service integration is optional and is never required to build FnQL.
Developers with the separate `../FnQL-Steam` repository can pass `-WithSteam`
to `.vscode/build-release.ps1` to build its strict dynamic-library target and
stage only the resulting provider beside FnQL. See
[`docs/fnql/STEAM_PROVIDER.md`](docs/fnql/STEAM_PROVIDER.md).

On Windows, use the **Win32/x86 client for retail Quake Live runtime tests**.
Retail `bin.pk3` ships `qagamex86.dll`, `cgamex86.dll`, and `uix86.dll`; the x64
build remains useful for engine-only validation but cannot load those x86
modules in-process.

### meson/ninja

Install Meson, Ninja, a C/C++ toolchain, and the platform dependencies listed below. Configure the default build directory from the repository root:

`meson setup meson/build`

Meson checks for system libraries first, then uses the wrap files under `subprojects/` for SDL3, OpenAL headers, libcurl, libjpeg-turbo, and Ogg/Vorbis when fallback downloads are allowed. Use `--wrap-mode=nofallback` for a system-only build, or `--wrap-mode=forcefallback` when you want to exercise the subproject path explicitly.

Then build and run tests:

`meson compile -C meson/build`

`meson test -C meson/build`

The default Meson build produces one client executable (`fnql` with the platform suffix where applicable), one dedicated server executable, and renderer modules named `fnql_opengl_<arch>`, `fnql_glx_<arch>`, and `fnql_vulkan_<arch>`.

Project Meson options:

`-Dbuild-client=true|false` - build the unified FnQL client executable, enabled by default

`-Dbuild-server=true|false` - build the dedicated server executable, enabled by default

`-Drenderer-dlopen=true|false` - build external renderer modules and one unified client executable, enabled by default

`-Drenderers=opengl,glx,vulkan` - choose which external renderer modules are built; valid entries are `opengl`, `glx`, `vulkan`, and `opengl2`

`-Drenderer-default=opengl|glx|vulkan|opengl2` - set the default value for `\cl_renderer`, or the linked renderer when `renderer-dlopen=false`

`-Drenderer-dlopen=false -Drenderer-default=vulkan` - explicit compatibility/testing mode for linking one renderer into the client

`-Dsdl=auto|enabled|disabled` - enable, require, or disable the SDL3 video, input, and audio backend

`-Dcurl=auto|enabled|disabled` - enable, require, or disable cURL download support

`-Dcurl-dlopen=true|false` - resolve cURL at runtime instead of linking libcurl, enabled by default

`-Dsystem-jpeg=true|false` - require a system JPEG library instead of allowing the libjpeg-turbo subproject fallback, disabled by default

`-Dogg-vorbis=true|false` - enable Ogg Vorbis codec support through system `vorbisfile` or the subproject fallback, enabled by default

`-Dlocal-headers=true|false` - define `USE_LOCAL_HEADERS` for compatibility include conventions, enabled by default

`-Ddefault-basedir=<path>` - compile a `DEFAULT_BASEDIR` override, empty by default

`-Daudio-tests=true|false` - build audio tools and deterministic audio tests, enabled by default

`-Dglx-tests=true|false` - build deterministic GLx renderer logic tests, enabled by default

`-Dstrict-warnings=true|false` - treat diagnostics in FnQL-owned targets as errors without applying project warning policy to fallback dependencies, disabled by default

Legacy Make inputs remain available while CI and packaging finish migrating,
but new local work should prefer `meson/build`. The Visual Studio solution is
a maintained frontend for that same Meson graph rather than a separate source
and dependency manifest.

### windows/msvc

Install Visual Studio Community Edition 2017 or later, Python, Meson, and
Ninja. You can build through Meson from a Visual Studio developer prompt:

`meson setup meson/build-msvc --buildtype=release`

`meson compile -C meson/build-msvc`

`meson install -C meson/build-msvc --destdir dist`

VS Code's default build and the launch configurations labelled
`Retail QL / Win32` initialize the matching MSVC environment automatically and
use `meson/build/win32` (Release) or `meson/build/win32-debug` (Debug). The x64
tasks and launch entries are retained explicitly as engine-only checks.

Alternatively, open `code/win32/msvc2017/fnql.sln`. Its single maintained
project delegates the selected Debug/Release and Win32/x64/ARM64 configuration
to Meson with strict warnings enabled and all renderer modules selected. Build
directories are isolated below `meson/build/vs/`. The older engine and renderer
component projects remain alongside it for source-coupling/reference tooling;
building one delegates to the same Meson graph. Obsolete third-party projects
were removed rather than restoring deleted in-tree dependency sources. Install
the corresponding MSVC C++ build tools for each selected architecture; ARM64
requires the optional ARM64 compiler workload.

---

### windows/msys2

Install the build dependencies first:

`MSYS2 MSYS`

* pacman -Syu
* pacman -S make mingw-w64-x86_64-gcc mingw-w64-i686-gcc

Use `MSYS2 MINGW32` or `MSYS2 MINGW64` depending on your target system, then either copy the resulting binaries from the `build` directory or run:

`make install DESTDIR=<path_to_quake_live_install>`

---

### windows/mingw

All required build dependencies, including libraries and headers, are bundled in.

Build with either `make ARCH=x86` or `make ARCH=x86_64` depending on your target system, then either copy the resulting binaries from the `build` directory or run:

`make install DESTDIR=<path_to_quake_live_install>`

---

### generic/ubuntu linux/bsd

On a fresh Ubuntu-style install, you will likely need packages like these first:

* sudo apt install make gcc libcurl4-openssl-dev mesa-common-dev
* sudo apt install libxxf86dga-dev libxrandr-dev libxxf86vm-dev libasound2-dev
* sudo apt install libsdl3-dev

Then build with: `make`

After that, either copy the resulting binaries from the `build` directory or run:

`make install DESTDIR=<path_to_quake_live_install>`

Current SDL backend dependency baseline: `SDL3 >= 3.2.0`

---

### Arch Linux

Use the generic Linux instructions above. This repository does not currently document an official Arch package name under the FnQL branding.

---

### raspberry pi os

Install the build dependencies:

* apt install libsdl3-dev libxxf86dga-dev libcurl4-openssl-dev

Then build with: `make`

After that, either copy the resulting binaries from the `build` directory or run:

`make install DESTDIR=<path_to_quake_live_install>`

---

### macos

* install the official SDL3 framework to `/Library/Frameworks`
* run `brew install molten-vk`, or install the Vulkan SDK if you want to use the MoltenVK library

Then build with: `make`

Copy the resulting binaries from the `build` directory.

---

### ppc64le / ppc64 (PowerPC 64-bit)

Install the same build dependencies as the generic Linux section above, then build with:

`make`

The JIT compiler (`vm_powerpc.c`) supports optional ISA-level optimizations that are enabled automatically based on compiler target flags:

* **ISA 2.07 (POWER8)**: Uses direct-move instructions (`mtvsrwa`, `mfvsrwz`, `xscvdpsxws`) to eliminate memory round-trips in float/int conversions (`OP_CVIF`, `OP_CVFI`)
* **ISA 3.0 (POWER9)**: Uses hardware modulo instructions (`modsw`, `moduw`) to replace 3-instruction sequences for `OP_MODI` and `OP_MODU`

To enable these optimizations, pass the appropriate `-mcpu` flag:

`make CFLAGS='-mcpu=power8'` - enable ISA 2.07 optimizations

`make CFLAGS='-mcpu=power9'` - enable ISA 2.07 + ISA 3.0 optimizations

`make CFLAGS='-mcpu=native'` - auto-detect based on build machine (note: resulting binary may not be portable to older hardware)

Without an explicit `-mcpu`, those optimizations depend on the compiler and distro defaults. The JIT falls back cleanly to baseline instruction sequences when the target ISA level is not available.

---

Several Makefile options are available for Linux, MinGW, and macOS builds:

`BUILD_CLIENT=1` - build unified client/server executable, enabled by default

`BUILD_SERVER=1` - build dedicated server executable, enabled by default

`USE_SDL=0` - disable the SDL3 backend for video, audio, and input and use the legacy non-SDL Unix backend instead; SDL3 is enabled by default and enforced for macOS

`USE_VULKAN=1` - build vulkan modular renderer, enabled by default

`USE_GLX=1` - build the GLx modular renderer, enabled by default for dynamic renderer builds. GLx is the canonical OpenGL-lineage renderer path for capability tiers, debug callbacks, GPU timing, static-world acceleration, dynamic streaming, material execution, postprocess parity, and promotion proof.

`USE_OPENGL=1` - build opengl modular renderer, enabled by default

`USE_OPENGL2=0` - build opengl2 modular renderer, disabled by default

`USE_RENDERER_DLOPEN=1` - do not link a single renderer into the client binary; compile all enabled renderers as dynamic libraries and allow switching on the fly via the `\cl_renderer` cvar, enabled by default

`RENDERER_DEFAULT=opengl` - set the default value for `\cl_renderer`, or choose the renderer used for a static build when `USE_RENDERER_DLOPEN=0`; valid options are `opengl`, `glx`, `opengl2`, `vulkan`

Choosing `RENDERER_DEFAULT=glx` also enables `USE_GLX=1` so the selected renderer is actually included in the build.

Release builds must not promote GLx as the default or alias `opengl` to GLx until `python scripts/glx_promotion.py --proof-root <reviewed-glx-proof-root> --require-ready` passes. Local `RENDERER_DEFAULT=glx` builds remain useful for explicit developer testing, but the repository default stays `opengl` until the promotion gate is green.

`USE_SYSTEM_JPEG=1` - use the current system JPEG library for Makefile builds, enabled by default

`USE_SYSTEM_OGG=1` and `USE_SYSTEM_VORBIS=1` - use the current system Ogg/Vorbis libraries for Makefile builds, enabled by default

`USE_CURL=1` - use the current system cURL library for Makefile builds, enabled by default. Meson builds use the `subprojects/curl.wrap` fallback when a system libcurl is not available.

Example:

`make BUILD_SERVER=0 USE_RENDERER_DLOPEN=0 RENDERER_DEFAULT=vulkan` - build the client with a single static Vulkan renderer and skip the dedicated server binary

`make BUILD_SERVER=0 USE_GLX=1` - include the GLx renderer module so it can be selected with `\cl_renderer glx` after a `\vid_restart`
