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

All maintained build graphs generate `fnql-web.pak` beside the client. Keep
that sidecar with the executable when copying a build manually. It contains
only FnQL's settings overrides and still requires the legitimate retail
`web.pak` for every untouched launcher resource. To build only the sidecar,
run `python scripts/build_webpak.py --source-root code/client/webui --output
.tmp/fnql-web.pak`.

### meson/ninja

Install Meson, Ninja, a C/C++ toolchain, and the platform dependencies listed below. Configure the default build directory from the repository root:

`meson setup meson/build`

Meson checks for system libraries first, then uses the wrap files under `subprojects/` for SDL3, OpenAL headers, libcurl, libjpeg-turbo, and Ogg/Vorbis when fallback downloads are allowed. Use `--wrap-mode=nofallback` for a system-only build, or `--wrap-mode=forcefallback` when you want to exercise the subproject path explicitly.

Then build and run tests:

`meson compile -C meson/build`

`meson test -C meson/build`

The default Meson build produces one client executable (`fnql` with the platform suffix where applicable), one dedicated server executable, and exactly three renderer modules: `fnql_glx_<arch>`, `fnql_vk_<arch>`, and `fnql_rtx_<arch>`.

Project Meson options:

`-Dbuild-client=true|false` - build the unified FnQL client executable, enabled by default

`-Dbuild-server=true|false` - build the dedicated server executable, enabled by default

`-Drenderer-dlopen=true|false` - build external renderer modules and one unified client executable, enabled by default

`-Drenderers=glx,vk,rtx` - choose which external renderer modules are built; valid entries are exactly `glx`, `vk`, and `rtx`

`-Drenderer-default=glx|vk|rtx` - set the default value for `\cl_renderer`, or the linked renderer when `renderer-dlopen=false`; the default is `glx`

`-Drenderer-dlopen=false -Drenderer-default=vk` - explicit compatibility/testing mode for linking one renderer into the client

`-Drenderer-dlopen=false -Drenderer-default=rtx` - explicit static RTX linkage and initialization gate

`-Dsdl=auto|enabled|disabled` - enable, require, or disable the SDL3 video, input, and audio backend

`-Dcurl=auto|enabled|disabled` - enable, require, or disable cURL download support

`-Dcurl-dlopen=auto|true|false` - resolve cURL at runtime instead of linking libcurl; `auto` (the default) links libcurl on Windows and resolves at runtime elsewhere

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

Make and CMake use the same pinned FontStash source owned by
`subprojects/fontstash.wrap`. Before the first Make or CMake build from a clean
checkout, fetch it with `meson subprojects download fontstash`. Client builds
fail at configuration time if that retail host-font dependency is unavailable;
they never silently substitute the incomplete host-font stubs.

### windows/msvc

Install Visual Studio Community Edition 2017 or later, Python, Meson, and
Ninja. You can build through Meson from a Visual Studio developer prompt:

`meson setup meson/build-msvc --buildtype=release`

`meson compile -C meson/build-msvc`

`meson install -C meson/build-msvc --destdir dist`

VS Code's default Steam build and the launch configurations labelled
`Retail QL / Win32` both use `meson/build/win32` (Release), so a manual launch
cannot accidentally run an older Debug binary. The separate
`meson: build Win32 debug (Steam)` task remains available for source-level
debugging. All VS Code build and launch entry points are intentionally Win32
only because retail Quake Live modules and Awesomium are 32-bit. The build
helper compiles an explicit runtime-product list, so normal VS Code builds do
not compile Meson's native test executables. Invoke the helper explicitly with
`-RunTests` or use Meson directly for tests; use the maintained solution below
for non-retail architecture work.

Official Windows release archives include the compiled closed-source Win32
`fnql_steam.dll` provider. Release CI downloads the exact binary pinned in
`version/fnql_steam_provider.json`, verifies its SHA-256 digest and PE i386
header, and never downloads provider source or Valve's `steam_api.dll`. Local
Steam-enabled development continues to build the private `../FnQL-Steam`
sibling through the `-WithSteam` helper path documented in
`docs/fnql/STEAM_PROVIDER.md`.

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

`USE_VK=1` - build the Vulkan raster renderer module (`vk`), enabled by default

`USE_RTX=1` - build the Vulkan ray-tracing renderer module (`rtx`), enabled by default

`USE_GLX=1` - build the GLx modular renderer, enabled by default. GLx is the canonical OpenGL-lineage renderer path for capability tiers, debug callbacks, GPU timing, static-world acceleration, dynamic streaming, material execution, postprocess, and output.

`USE_RENDERER_DLOPEN=1` - do not link a single renderer into the client binary; compile all enabled renderers as dynamic libraries and allow switching on the fly via the `\cl_renderer` cvar, enabled by default

`RENDERER_DEFAULT=glx` - set the default value for `\cl_renderer`, or choose the renderer used for a static build when `USE_RENDERER_DLOPEN=0`; valid options are exactly `glx`, `vk`, and `rtx`

Choosing `RENDERER_DEFAULT=glx` also enables `USE_GLX=1` so the selected renderer is actually included in the build.

`USE_SYSTEM_JPEG=1` - use the current system JPEG library for Makefile builds, enabled by default

`USE_SYSTEM_OGG=1` and `USE_SYSTEM_VORBIS=1` - use the current system Ogg/Vorbis libraries for Makefile builds, enabled by default

`USE_CURL=1` - use the current system cURL library for Makefile builds, enabled by default. Meson builds use the `subprojects/curl.wrap` fallback when a system libcurl is not available.

Example:

`make BUILD_SERVER=0 USE_RENDERER_DLOPEN=0 RENDERER_DEFAULT=vk` - build the client with a single static Vulkan renderer and skip the dedicated server binary

`make BUILD_SERVER=0 USE_RENDERER_DLOPEN=0 RENDERER_DEFAULT=rtx` - build an explicit static RTX validation client

`make BUILD_SERVER=0 USE_GLX=1` - include the GLx renderer module so it can be selected with `\cl_renderer glx` after a `\vid_restart`
