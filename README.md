<div align="center">

# FnQL
### Fappin' Quake Live

<p>
  <a href="https://github.com/themuffinator/FnQL/releases/latest"><img alt="Download Latest Release" src="https://img.shields.io/badge/Download-Latest%20Release-2563eb?style=for-the-badge&logo=github&logoColor=white"></a>
  <a href="#3-getting-started"><img alt="Quick Start" src="https://img.shields.io/badge/Quick-Start-16a34a?style=for-the-badge"></a>
  <a href="#4-documentation"><img alt="Player Documentation" src="https://img.shields.io/badge/Player-Docs-f59e0b?style=for-the-badge"></a>
  <a href="BUILD.md"><img alt="Build Guide" src="https://img.shields.io/badge/Build-Guide-7c3aed?style=for-the-badge"></a>
  <a href="LICENSE"><img alt="GPL-2.0 License" src="https://img.shields.io/badge/License-GPL--2.0-c2410c?style=for-the-badge"></a>
</p>

*Compatibility target: Retail Quake Live (Steam) · Base version: `0.1.0` · Current build: `0.1.0`*

</div>

## 1. Introduction

**FnQL** is a modernized Quake Live engine for players who
want the retail Steam game to retain its familiar behavior while gaining
faster rendering, modern audio and input, stronger platform support, and useful
quality-of-life improvements inherited from FnQ3.

Retail Quake Live compatibility comes first. FnQL is engine-only: it does not
reconstruct or distribute Quake Live game code or assets, so a legitimate
[Quake Live installation on Steam](https://store.steampowered.com/app/282440/Quake_Live/)
is required. The official Windows package includes FnQL's open-source engine,
its WebUI overlay, and the compiled closed-source Steam provider; it does not
include Valve's `steam_api.dll` or any retail game content.

## 2. Features

- **Extensive Quake Live compatibility** — built around the retail Steam
  release, with support for official assets and game modules, protocol 91
  networking, `.dm_91` demos, BSP v47 maps, advertisements, factories, player
  profiles, the retail WebUI, Steam integration and Workshop content.
- **Fast, modern hardware rendering** — choose between optimized **GLx**,
  **Vulkan raster** and **Vulkan RTX** renderers, with GPU-cached world
  geometry, optimized lightmaps, reduced rendering overhead, improved depth
  precision and better performance on large or demanding maps.
- **Modern visual upgrades** — optional HDR rendering and output, MSAA,
  supersampling, render scaling, anisotropic filtering, bloom, tone mapping,
  colour grading, high-quality dynamic lighting and shadows, motion blur, lens
  flares, soft particles, enhanced liquids and per-map fog.
- **Improved player visibility** — optional player rim lighting, stencil
  outlines or both, with configurable enemy, teammate, team and free-for-all
  colours, intensity and outline thickness. Cel shading, model outlines and
  world-edge outlines are also available.
- **Better modern hardware and OS support** — SDL3-based video and input,
  OpenAL audio, modern graphics APIs, high-resolution and high-refresh
  displays, borderless/windowed/fullscreen modes, fast Alt+Enter switching,
  controller hotplugging and improved compatibility with current desktop
  systems.
- **Low-latency, configurable controls** — raw mouse input, Quake
  Live-accurate CPI scaling, signed acceleration, sensitivity caps and
  angle-history filtering, while retaining classic Quake3e/ioquake3 mouse
  styles. Modern gamepads and Unicode text input are also supported.
- **Modern spatial audio** — OpenAL and HRTF headphone positioning, surround
  output, reverb, geometry-based occlusion, output-device selection, live
  device recovery and automatic fallback to the classic software mixer.
- **Major quality-of-life improvements** — scalable TTF console text, smooth
  scrolling, mouse selection, clipboard support, drag-and-drop editing, live
  command/CVar/map completion, minimize hotkeys, focus muting, lower background
  CPU usage and more flexible player configuration.
- **Better screenshots, video and streaming tools** — PNG, TGA, JPEG and BMP
  capture, clipboard screenshots, custom filenames, camera metadata,
  configurable levelshots, watermarks, cubemap capture, arbitrary-resolution
  output and FFmpeg video piping.
- **Faster content handling** — quicker startup and map restarts with large PK3
  libraries, raised filesystem and map limits, fast redirected downloads and
  automatic handling of server-required Workshop content.
- **Greater stability and security** — extensive Quake3e and FnQ3 bug fixes,
  improved memory management, lower memory usage, stronger network protection
  and graceful fallbacks when optional rendering, audio, WebUI or Steam
  functionality is unavailable.

## 3. Getting Started

1. Install [Quake Live through Steam](https://store.steampowered.com/app/282440/Quake_Live/)
   and launch the retail game once so Steam creates its normal files and player
   profile.
2. Download the current package from the
   [latest FnQL release](https://github.com/themuffinator/FnQL/releases/latest).
3. Extract the archive into the Quake Live installation folder. Keep
   `fnql-web.pak`, the renderer modules, and the packaged runtime libraries
   beside the FnQL executable.
4. Leave Steam running and launch `fnql.exe`. The official Windows build is
   Win32/x86 so it can load the retail game modules and WebUI.
5. Open Settings in the retail WebUI or use the guides below to tune rendering,
   audio, controls, screenshots, and console behavior.

FnQL locates the normal Steam installation automatically. If yours is in an
unusual location, start FnQL with `+set fs_steampath "<Quake Live folder>"`.
Steam and WebUI features fail safely when their optional runtime components are
unavailable. To compile FnQL yourself, use the [Build Guide](BUILD.md).

## 4. Documentation

### Player guides

- [Display Guide](docs/DISPLAY.md) for renderer selection, display modes, HDR,
  render scaling, anti-aliasing, bloom, lighting, shadows, fog, liquids, lens
  flares, motion blur, and other presentation controls.
- [RTX Renderer Guide](docs/RTX.md) for ray-tracing requirements, renderer
  selection, fallback behavior, safe starting settings, and troubleshooting.
- [Visuals Guide](docs/VISUALS.md) for player highlighting, team and enemy
  colours, cel shading, and outlines.
- [Aspect Handling Guide](docs/ASPECT_CORRECTION.md) for retail WebUI, menu,
  and cinematic layout behavior.
- [Audio Guide](docs/AUDIO.md) for backends, devices, HRTF, spatial effects,
  fallback behavior, and troubleshooting.
- [Console Guide](docs/CONSOLE.md) for console layout, scaling, completion,
  clipboard use, and mouse interaction.
- [Screenshot Guide](docs/SCREENSHOTS.md) for capture commands, formats,
  filenames, metadata, levelshots, watermarks, and cubemaps.
- [Release History](https://github.com/themuffinator/FnQL/releases) for
  published downloads and changelogs.

### Technical and build docs

- [Build Guide](BUILD.md) for compiling FnQL locally.
- [GLx Renderer Guide](docs/GLX.md) for GLx availability, diagnostics, and
  troubleshooting.
- [Input Compatibility Notes](docs/fnql/INPUT_COMPATIBILITY.md) for retail mouse
  math, text input, controllers, and preserved legacy input profiles.
- [WebUI Backend Notes](docs/fnql/WEBUI_BACKEND.md) for retail resources,
  platform boundaries, overlay order, and fallback rules.
- [Steam Provider Notes](docs/fnql/STEAM_PROVIDER.md) for the open loader ABI,
  separately distributed provider, and runtime policy.
- [RTX Renderer Contract](docs/fnql/RTX_RENDERER.md) for the renderer's Quake
  Live ABI boundary, conservative defaults, build gates, and runtime validation.
- [Technical Notes](docs/fnql/TECHNICAL.md) for repository structure, release
  flow, compatibility workflow, and maintainer conventions.

## 5. Credits

FnQL follows a clear upstream lineage from the
[Quake III Arena SDK](https://github.com/id-Software/Quake-III-Arena), through
[ioquake3](https://github.com/ioquake3/ioq3),
[Quake3e](https://github.com/ec-/Quake3e),
[FnQ3](https://github.com/themuffinator/FnQ3), and
[QL-SRP](https://github.com/themuffinator/QL-SRP).

See [CREDITS.md](CREDITS.md) for the full credits, acknowledgements, and
trademark notice.
