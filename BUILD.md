## Build Instructions

Meson is the preferred build path. It builds a single client executable and
places external renderer modules beside it, so renderer selection stays a
runtime `\cl_renderer` choice instead of a reason to produce separate client
executables.

FnQL targets retail Quake Live compatibility. A successful build proves the
engine compiled, but compatibility-sensitive runtime work should still be
validated against a legitimate Steam installation.

Steam service integration is optional and is never required to build FnQL.
Developers with the separate `../FnQL-Steam` repository can pass `-WithSteam`
to `.vscode/build-release.ps1` to build its strict dynamic-library target and
stage only the resulting provider beside FnQL. See
[`docs/fnql/STEAM_PROVIDER.md`](docs/fnql/STEAM_PROVIDER.md).

FnQL is a **32-bit x86-only project**. Build, test, package, and run Win32 on
Windows or i686 on Linux; x86_64, ARM, and macOS targets are unsupported.

On Windows, use the **Win32/x86 client for retail Quake Live runtime tests**.
Retail `bin.pk3` ships `qagamex86.dll`, `cgamex86.dll`, and `uix86.dll`; the x64
ABI cannot load those x86 modules in-process and is not a FnQL target.

Official release support is limited to the Windows Win32/x86 retail client and
Linux i686 dedicated server. macOS is disabled and unsupported.

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
Ninja. Open an x86 Native Tools developer prompt, then build through Meson:

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
`-RunTests` or use Meson directly for tests.

Official Windows release archives include the compiled closed-source Win32
`fnql_steam.dll` provider. Release CI downloads the exact binary pinned in
`version/fnql_steam_provider.json`, verifies its SHA-256 digest and PE i386
header, and never downloads provider source or Valve's `steam_api.dll`. Local
Steam-enabled development continues to build the private `../FnQL-Steam`
sibling through the `-WithSteam` helper path documented in
`docs/fnql/STEAM_PROVIDER.md`.

Alternatively, open `code/win32/msvc2017/fnql.sln` and select a Win32
Debug/Release configuration. Its maintained project delegates to Meson with
strict warnings enabled and all renderer modules selected. Build directories
are isolated below `meson/build/vs/`. x64 and ARM64 are outside FnQL's
supported ABI.

---

### windows/msys2

Install the build dependencies first:

`MSYS2 MSYS`

* pacman -Syu
* pacman -S make mingw-w64-i686-gcc

Use `MSYS2 MINGW32`, then either copy the resulting binaries from the `build` directory or run:

`make install DESTDIR=<path_to_quake_live_install>`

---

### windows/mingw

All required build dependencies, including libraries and headers, are bundled in.

Build with `make ARCH=x86`, then either copy the resulting binaries from the `build` directory or run:

`make install DESTDIR=<path_to_quake_live_install>`

---

### linux

Linux has two distinct compatibility surfaces:

- Native dedicated servers load the retail `qagamei386.so` from
  `baseq3/bin.pk3`; FnQL does not build or validate the x86_64 alternative.
- Retail Quake Live does not ship native Linux `cgame` or UI modules; those
  modules and the retained Awesomium WebUI runtime are Win32-only. Native Linux
  client builds are maintained engine/platform development targets, but are not
  advertised as a retail-play path. Use the Win32/x86 release for retail client
  play.

Official Linux downloads are deterministic `.tar.gz` archives. Extract them
with `tar`, rather than copying individual files out of a browser, so the
executable modes on `fnql`, `fnql.ded`, and the renderer modules survive:

```sh
ql_dir="$HOME/.local/share/Steam/steamapps/common/Quake Live"
tar -xzf fnql-*-linux-x86.tar.gz -C "$ql_dir"
cd "$ql_dir"
ldd ./fnql.ded | grep 'not found' || true
./fnql.ded +set fs_steampath "$ql_dir" +set dedicated 2 +map campgrounds
```

Steam may instead use `~/.steam/steam/steamapps/common/Quake Live` or a custom
library. Pass that exact directory through `fs_steampath`; do not copy retail
assets into the repository or FnQL package.

#### Meson i686 development build

Meson is the preferred path. On Debian or Ubuntu, this baseline builds the
legacy X11 backend using system development packages while Meson fetches only
missing wrap fallbacks (including the required pinned FontStash source):

```sh
sudo apt update
sudo apt install gcc-multilib g++-multilib libc6-dev-i386 \
  linux-libc-dev:i386 git meson ninja-build pkg-config python3
meson setup meson/build-linux-x86 \
  --cross-file misc/meson/linux-x86.ini \
  --buildtype=debugoptimized -Dsdl=disabled -Dcurl=disabled \
  -Dogg-vorbis=false
meson compile -C meson/build-linux-x86
meson test -C meson/build-linux-x86 --print-errorlogs
```

For a complete i686 client build, install the corresponding `:i386` SDL3,
OpenAL, cURL, JPEG, Ogg/Vorbis, and X11 development packages. The release
workflow is the canonical dependency baseline when distribution package names
differ.

Stage a build without writing into the Steam library, verify it, and then copy
the whole flat runtime root together:

```sh
mkdir -p .tmp
install_root="$(mktemp -d "$PWD/.tmp/linux-install.XXXXXX")"
meson install -C meson/build-linux-x86 --destdir "$install_root"
python3 scripts/verify_release_layout.py \
  "$install_root/usr/local/FnQL-pkg.fnz"
```

The exact staging prefix follows Meson's configured `--prefix`. Keep
`FnQL-pkg.fnz`, `fnql-web.pak`, and all selected renderer modules beside the
client/server executables.

#### Official-compatible i686 build

A 64-bit Linux host needs multiarch development packages. Package names can
vary by Debian/Ubuntu release; this is the release-builder baseline:

```sh
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install gcc-multilib g++-multilib make python3 pkg-config \
  libasound2-dev:i386 libcurl4-openssl-dev:i386 libjpeg-dev:i386 \
  libogg-dev:i386 libopenal-dev:i386 libvorbis-dev:i386 libx11-dev:i386 \
  libxrandr-dev:i386 libxxf86dga-dev:i386 libxxf86vm-dev:i386 \
  mesa-common-dev:i386
meson subprojects download fontstash
make -j"$(nproc)" release ARCH=x86 COMPILE_ARCH=x86 USE_SDL=0 \
  USE_RENDERER_DLOPEN=1 USE_GLX=1 USE_VK=1 USE_RTX=1
make install ARCH=x86 COMPILE_ARCH=x86 USE_SDL=0 DESTDIR="$PWD/.tmp/linux-x86/"
python3 scripts/verify_release_layout.py .tmp/linux-x86/FnQL-pkg.fnz
file .tmp/linux-x86/fnql .tmp/linux-x86/fnql.ded
```

The release packager accepts a `linux-x86` artifact directory, restores
canonical executable modes after CI artifact transfer, writes a deterministic
`.tar.gz`, and reopens it to verify the client, dedicated server, all three
renderer modules, package sidecars, ELF i386 identity, and safe archive paths.

---

### Other platforms

FnQL does not build, test, package, or launch macOS, ARM, PowerPC, BSD, or
other non-x86 targets. Windows Win32/x86 and Linux i686 are the supported
platform/ABI combinations.

---
Several Makefile options are available for Linux i686 and MinGW32 builds:

`BUILD_CLIENT=1` - build unified client/server executable, enabled by default

`BUILD_SERVER=1` - build dedicated server executable, enabled by default

`USE_SDL=0` - disable the SDL3 backend for video, audio, and input and use the legacy non-SDL Unix backend instead

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
