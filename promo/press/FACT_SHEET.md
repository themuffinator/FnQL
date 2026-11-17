# FnQL fact sheet

| | |
| --- | --- |
| **Name** | FnQL (Fappin' Quake Live) |
| **What** | Open-source replacement engine for the retail Steam release of Quake Live |
| **Latest release** | 0.1.0.158, 16 November 2026 |
| **Platforms (official packages)** | Windows x86 client; Linux i686 dedicated server |
| **Also builds from source** | Windows and Linux x86_64; Linux dedicated server on aarch64, armv7l, ppc64le, ppc64 (packages withheld or not yet published) |
| **Price** | Free |
| **Licence** | GPL-2.0 engine. The Windows package also contains a compiled, closed-source Steam provider (`fnql_steam.dll`). The bundled FnQL-game modules combine GPLv2-or-later code with id Software's Quake III Arena game-source licence. |
| **Requires** | A legitimate copy of Quake Live on Steam (app 282440) |
| **Ships game content?** | No. No retail assets, no Valve redistributables |
| **Lineage** | Quake III Arena GPL source → ioquake3 → Quake3e → FnQ3 → FnQL |
| **Companion project** | FnQL-game: open-source game, client-game and UI modules for Quake Live |
| **Source** | https://github.com/themuffinator/FnQL |
| **Downloads** | https://github.com/themuffinator/FnQL/releases/latest |
| **Release history** | First public build 0.1.0.17 on 19 July 2026; recent: 0.1.0.117 (3 Nov), 0.1.0.134 (11 Nov), 0.1.0.158 (16 Nov). Full list: https://github.com/themuffinator/FnQL/releases |
| **Developer** | themuffinator |
| **Contact** | `TODO: press contact address` |

## In one paragraph

FnQL is a modernized engine for the retail Steam release of Quake Live. It
keeps retail compatibility as its first rule: FnQL players join retail
protocol-91 servers, retail players join FnQL servers, and demos, maps,
factories, the WebUI, profiles and Workshop content keep working. On top, it
offers GLx and Vulkan renderers with HDR and modern anti-aliasing, raw mouse
input with Quake Live's own CPI scaling, OpenAL audio with HRTF, a modern
console, flexible capture tools and a hardened dedicated server. Since
0.1.0.158 it runs the open-source FnQL-game modules by default.

## Feature list

- Retail compatibility: protocol 91, `.dm_91` demos, BSP v47, factories,
  advertisements, profiles, retail WebUI, Steam integration, Workshop.
- Two-way interop: FnQL ↔ retail clients and servers.
- Renderers: GLx (OpenGL), Vulkan raster, experimental RTX module.
- Visuals: HDR rendering and output, MSAA, supersampling, render scaling,
  anisotropic filtering, bloom, tone mapping, colour grading, dynamic
  lights and shadows, soft particles, enhanced liquids, per-map fog, lens
  flares, motion blur.
- Visibility: player rim lighting and stencil outlines with custom colours;
  cel shading; model and world-edge outlines.
- Input: raw mouse, QL-accurate CPI scaling, signed acceleration,
  sensitivity caps, angle-history filtering, classic Quake3e/ioq3 styles,
  controllers with QL's analog mapping on Windows, Unicode text.
- Audio: OpenAL, HRTF, surround, reverb, geometry occlusion, device
  selection, live recovery, software-mixer fallback.
- Console and QoL: TTF console, smooth scrolling, mouse selection,
  clipboard, live completion, minimize hotkeys, focus muting.
- Capture: PNG/TGA/JPEG/BMP, clipboard, custom names, camera metadata,
  levelshots, watermarks, cubemaps, any resolution, FFmpeg piping,
  windowless client (`com_headless 2`).
- Server: hostile-content rejection, rcon and identity fixes, JSON-lines
  stats journal, ZMQ rcon fixes, clean signal shutdown.

## FAQ

**Is FnQL official?**
No. It is an independent project, not affiliated with or endorsed by id
Software, Bethesda, ZeniMax Media or Valve.

**Do I need to own Quake Live?**
Yes. FnQL uses the game files from your Steam installation and ships none.

**Can I still play on normal servers?**
Yes. FnQL joins retail protocol-91 servers, and retail players can join FnQL
servers.

**Is it a cheat? Can I be banned?**
FnQL is an engine replacement with no cheating features. It authenticates
through Steam's normal session tickets and never fakes them. It is
unofficial, so server operators decide which clients they accept, and the
project can't speak for Valve or Bethesda.

**Which platforms?**
Official packages: Windows x86 client and Linux i686 dedicated server.
Retail's client modules and WebUI are 32-bit Windows binaries, so there is no
supported native Linux or macOS client for retail play. The Windows build's
Steam integration starts under Proton/Wine.

**Ray tracing?**
There is an experimental RTX renderer module. Its native ray-tracing path
covers a conservative subset of world geometry, and 32-bit drivers may not
expose ray-tracing extensions. GLx and Vulkan are the renderers to use.

**Is it open source?**
The engine is GPL-2.0 and on GitHub. The Windows package's Steam provider is
a separately distributed compiled binary; FnQL runs without it in non-Steam
mode.

**What is FnQL-game?**
Open-source replacements for Quake Live's game, client-game and UI modules,
rewritten from observed retail behaviour on the baseq3a game-code baseline
and checked against retail with lockstep and pixel-comparison gates. FnQL
uses them by default; one cvar per module switches back to retail's.

## Assets

- Logo, icon, banners and key art: `../images/`
- Screenshots (1920×1080, rendered by FnQL): `../images/screenshots/`
- Feature comparisons (highlighting, cel shading, shadows, HDR): `../images/features/`
- All images are © their respective owners: FnQL artwork by the FnQL
  project; in-game content in screenshots is from Quake Live, © id Software /
  ZeniMax Media.
