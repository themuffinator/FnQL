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

### linux

Linux has two distinct compatibility surfaces:

- Native dedicated servers can load the retail `qagamei386.so` or
  `qagamex64.so` from `baseq3/bin.pk3`. The official release is i686 and uses
  the former so its ABI matches the other retail-facing release artifacts.
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

#### Native development build

Meson is the preferred path. On Debian or Ubuntu, this baseline builds the
legacy X11 backend using system development packages while Meson fetches only
missing wrap fallbacks (including the required pinned FontStash source):

```sh
sudo apt update
sudo apt install build-essential git meson ninja-build pkg-config python3 \
  libasound2-dev libcurl4-openssl-dev libfreetype6-dev libjpeg-dev \
  libogg-dev libopenal-dev libvorbis-dev libx11-dev
meson setup meson/build-linux --buildtype=debugoptimized -Dsdl=disabled
meson compile -C meson/build-linux
meson test -C meson/build-linux
```

For the SDL path, install SDL3 3.2.0 or newer and configure with
`-Dsdl=enabled`. If the distribution does not yet package SDL3, the existing
`subprojects/sdl3.wrap` fallback is the supported bundled source boundary; use
`--wrap-mode=forcefallback` to exercise it explicitly. Meson prints whether
SDL3, OpenAL, cURL, JPEG, and Ogg/Vorbis came from the system or a subproject at
the end of configuration.

Stage a build without writing into the Steam library, verify it, and then copy
the whole flat runtime root together:

```sh
mkdir -p .tmp
install_root="$(mktemp -d "$PWD/.tmp/linux-install.XXXXXX")"
meson install -C meson/build-linux --destdir "$install_root"
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

### bsd

Use the native Meson workflow above as a starting point, substituting the
platform package manager and dependencies. BSD remains a source-build target;
the project does not currently publish BSD release archives or claim the Linux
retail-module boundary there.

---

### arch linux

Use the native Linux Meson workflow above with Arch package names. FnQL does
not currently publish distro-native Arch package metadata; install into a
staging directory and keep the flat runtime root together.

---

### raspberry pi os

Install the build dependencies:

* apt install libsdl3-dev libxxf86dga-dev libcurl4-openssl-dev

Then build with: `make`

After that, either copy the resulting binaries from the `build` directory or run:

`make install DESTDIR=<path_to_quake_live_install>`

---

### macos

FnQL supports native Intel (`x86_64`) and Apple Silicon (`arm64`) engine builds
on macOS 11 or newer. Meson is the maintained build path. It builds SDL3 and
the other portable dependencies from the pinned wrap definitions, so a release
does not acquire Homebrew or MacPorts library paths:

```sh
python3 -m pip install meson==1.9.1 ninja
arch="$(uname -m)"
test "$arch" = arm64 && input_arch=arm64 || input_arch=x86_64
export MACOSX_DEPLOYMENT_TARGET=11.0
meson setup ".tmp/meson-macos-$input_arch" \
  --buildtype=release \
  --wrap-mode=forcefallback \
  --prefix "$PWD/.tmp/macos-install-$input_arch" \
  -Ddefault_library=static \
  -Dsdl=enabled \
  -Dcurl=enabled \
  -Dcurl-dlopen=false \
  -Drenderer-dlopen=true \
  -Drenderers=glx,vk,rtx \
  -Drenderer-default=glx
meson compile -C ".tmp/meson-macos-$input_arch"
meson test -C ".tmp/meson-macos-$input_arch" --print-errorlogs
meson install -C ".tmp/meson-macos-$input_arch" --no-rebuild --skip-subprojects
python3 scripts/macos_bundle.py \
  --input "$input_arch=$PWD/.tmp/macos-install-$input_arch" \
  --output .tmp/FnQL-macOS
```

The staged distribution contains `FnQL.app`, `fnql.ded`, and
`fnql-audiozonesc`. The app owns its renderer dylibs, `FnQL-pkg.fnz`, and
`fnql-web.pak` under `Contents/MacOS`. CI verifies the plist, canonical layout,
executable modes, and every Mach-O dependency on both native runner families;
the explicit manual-release lane additionally verifies distribution signatures.
Install MoltenVK (for example from the Vulkan SDK) only when testing the
optional `vk` renderer; the default `glx` renderer uses Apple's OpenGL path.
Meson install trees are the only supported input to the distribution packager.
The Make and CMake paths remain useful compile-time developer checks, but they
do not stage the audio-zone tool and complete relocatable app contract.
Local and normal CI builds receive no project-applied app-bundle or Developer
ID signature by default. Apple Silicon still carries the ad-hoc Mach-O
signature that `clang` applies because arm64 code must be signed. Pass
`--sign-identity -` with `--entitlements misc/macos/fnql.entitlements` only
when an explicit ad-hoc app signature is specifically useful.

To build Universal 2, first produce one staged Meson install on each native
architecture, then pass both inputs to the packager. Client, server, and tool
executables are merged with `lipo`; both architecture-named renderer dylibs are
retained because renderer lookup is slice-specific:

```sh
python3 scripts/macos_bundle.py \
  --input x86_64=/path/to/x86_64/install \
  --input arm64=/path/to/arm64/install \
  --output .tmp/FnQL-macOS-universal2
```

Signing is opt-in. For a public distribution, import a Developer ID Application
certificate, create an App Store Connect profile with
`xcrun notarytool store-credentials fnql-notary`, and replace the ad-hoc
arguments with:

```sh
--sign-identity "Developer ID Application: Your Name (TEAMID)" \
--entitlements misc/macos/fnql.entitlements \
--notary-profile fnql-notary
```

The tool signs nested code inside-out with the hardened runtime, submits with
`notarytool --wait`, staples the app, and verifies it with `codesign`,
`stapler`, and Gatekeeper. It does not put credentials on the command line.

Important retail boundary: the legitimate Steam `baseq3/bin.pk3` contains
Win32 client/UI/game DLLs and Linux game SOs, but no macOS dylibs or QVMs.
Consequently the native Mac engine, renderer modules, tools, bundle, input,
audio, filesystem, networking, and fallback paths have native compile, unit,
package, signature, and dependency coverage, but native retail client or
dedicated gameplay cannot load the retail game modules. Apple-hardware,
windowed renderer smoke testing remains required before claiming runtime
renderer promotion. FnQL reports the missing module before checking for a
mod-provided QVM. The Windows x86 package remains the retail client-play path;
FnQL does not reconstruct or distribute replacement game code.

The macOS package intentionally contains neither a Steam provider nor Valve's
`libsteam_api.dylib`; platform authentication therefore remains unavailable
and is reported honestly. A future production provider must be separately
licensed, architecture-matched, signed with a compatible Team ID, and bundled
before hardened-runtime library validation will load it. The existing
`~/Library/Application Support/Quake3` home path is retained as a compatibility
path rather than silently moving established user data.

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
