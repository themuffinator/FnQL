# ModDB mod page

Create at https://www.moddb.com/mods/add. ModDB moderates new pages, so
submit at least two days before launch. Field names below follow ModDB's
add-mod form; check them against the live form, which changes from time to
time.

## Fields

| Field | Value |
| --- | --- |
| Name | FnQL |
| Game | Quake Live (if ModDB has no Quake Live listing, add the mod under the id Tech 3 engine page and mention Quake Live in the summary) |
| Homepage | https://github.com/themuffinator/FnQL |
| Status | Released (early development) |
| Genre | First Person Shooter |
| Theme | Sci-Fi |
| Players | Single & Multiplayer |
| Project | Engine / sourceport (choose "Other" if there's no engine option) |
| Boxshot / logo | `images/logo/fnql-logo-1332.png` |
| Icon | `images/logo/fnql-icon-256.png` |
| Header image | `images/banners/fnql-moddb-header-1200x300.jpg` |
| Tags | quakelive, quake, idtech3, sourceport, opensource, vulkan, opengl, arena, multiplayer, engine |

## Summary

(short field, shown in listings)

> An open-source replacement engine for retail Quake Live on Steam. FnQL
> joins retail servers, hosts retail clients, plays your demos and adds
> GLx and Vulkan renderers, HDR, raw input, HRTF audio and a modern console.

## Description

(paste into the main description editor; headings as Heading 2, lists as
bullets)

---

**FnQL** (Fappin' Quake Live) is a modernized engine for the retail Steam
release of Quake Live. It follows the classic id Tech 3 family tree, from
id Software's GPL Quake III Arena source through ioquake3, Quake3e and FnQ3,
and points it at one target: **the Quake Live you already own.**

FnQL replaces only the engine. Your Steam install, profile, configs, maps and
Workshop content stay where they are. Extract FnQL into the Quake Live folder,
leave Steam running and launch `fnql.exe`.

## Retail compatibility comes first

- **Join retail servers.** FnQL speaks Quake Live's protocol 91.
- **Host retail players.** Retail clients join FnQL servers and receive
  exactly what a retail server sends.
- **Your content works:** `.dm_91` demos, BSP v47 maps, factories, player
  profiles, the retail WebUI, Steam integration and Workshop downloads.
- **Honest authentication:** Steam session tickets go through Steam's normal
  path. FnQL never fakes them.

## Modern rendering

- **GLx** (modern OpenGL) and **Vulkan** renderers with GPU-cached world
  geometry and better depth precision.
- HDR rendering and output, MSAA, supersampling, render scaling, anisotropic
  filtering, bloom, tone mapping and colour grading.
- High-quality dynamic lights and shadows, soft particles, enhanced liquids,
  per-map fog, lens flares and motion blur, all optional.
- Player visibility: rim lighting, stencil outlines or both, in your chosen
  enemy and team colours. Cel shading and model outlines too.
- An experimental RTX renderer module for adventurous testers.

## Input, audio and quality of life

- Raw mouse input with Quake Live-accurate CPI scaling, signed acceleration,
  sensitivity caps and angle-history filtering. Classic Quake3e and ioquake3
  mouse styles remain available.
- Controller support with Quake Live's analog mapping on Windows.
- OpenAL audio with HRTF headphone positioning, surround, reverb and
  geometry-based occlusion, with live device recovery and an automatic
  fallback to the classic mixer.
- Scalable TTF console with smooth scrolling, mouse selection, clipboard,
  drag-and-drop editing and live command, cvar and map completion.
- High-resolution and high-refresh displays, borderless and windowed modes,
  fast Alt+Enter, focus muting and lower background CPU use.
- PNG, TGA, JPEG and BMP screenshots at any size, cubemap capture, camera
  metadata and FFmpeg video piping.

## Open game modules: FnQL-game

FnQL ships with the open-source **FnQL-game** modules (server game, client
game and UI), rewritten from observed retail behaviour on an improved
Quake III 1.32 game-code baseline. A lockstep harness runs them beside the
retail server module and requires zero divergence before each milestone
closes. They're the default as of 0.1.0.158; one cvar per module switches
back to the retail ones.

## For server operators

- Hardened against hostile downloaded content, rcon crashes and spoofed
  identities.
- Match-stats journal in JSON lines, no libzmq required.
- Linux i686 dedicated server package; source builds for Linux aarch64,
  armv7l, ppc64le and ppc64.

## Requirements

- A legitimate copy of **Quake Live on Steam**, launched once.
- Windows (x86 client package) or Linux (i686 dedicated server package).
- Steam running for Steam features.

## Credits

id Software (Quake III Arena GPL source), ioquake3, Quake3e, FnQ3 and the
wider Quake and Quake Live communities. Full credits:
https://github.com/themuffinator/FnQL/blob/main/CREDITS.md

*FnQL is an independent project and is not affiliated with, endorsed by, or
sponsored by id Software, Bethesda, ZeniMax Media, or Valve. Quake, Quake III
Arena, and Quake Live are trademarks of their respective owners. FnQL ships no
retail game content.*
