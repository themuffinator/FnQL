# FnQL Messaging Guide

Every post, page and caption in this folder draws on this file. When a
release changes the facts, update this file first, then the copy.

Facts here are pinned to release **0.1.0.158** (published 2026-11-16, tag
`0.1.0.158-20261116-28e575d7`).

## Positioning

**One line (10 words):** A modern, open-source engine for the Quake Live you
already own.

**Short pitch (about 30 words):** FnQL is an open-source replacement engine for
retail Quake Live on Steam. It keeps the game you know (servers, demos, maps,
WebUI, Workshop) and adds modern renderers, input, audio and quality-of-life.

**Paragraph (about 90 words):** FnQL (Fappin' Quake Live) is a modernized engine
for the retail Steam release of Quake Live. It descends from id Software's
GPL Quake III source through ioquake3, Quake3e and FnQ3, and it is built
around one rule: retail compatibility comes first. FnQL players join retail
protocol-91 servers, retail players join FnQL servers, and your demos, maps,
profile and Workshop content keep working. On top of that you get GLx and
Vulkan renderers, HDR and anti-aliasing, raw input with Quake Live's own mouse
math, HRTF audio and a far better console. It is free, GPL-2.0, and needs
only your own Steam copy of Quake Live.

## Taglines

Pick one per surface; don't stack them.

- Quake Live, rebuilt for today. Still Quake Live.
- Same game. Same servers. Better engine.
- The engine Quake Live deserves, for the copy you already own.
- Modern engine. Retail rules.

## Key messages

1. **It is still Quake Live.** Retail compatibility is the target, not a side
   goal. Retail servers, retail clients, `.dm_91` demos, BSP v47 maps, the
   WebUI, factories, profiles and Workshop content all keep working.
2. **It plays both ways.** FnQL clients join retail servers; retail clients
   join FnQL servers and see exactly what a retail server sends.
3. **It looks and feels modern.** GLx and Vulkan renderers, HDR, MSAA and
   supersampling, bloom, tone mapping, high-refresh and high-DPI displays,
   raw mouse input with QL-accurate CPI scaling, HRTF and surround audio.
4. **It is open.** GPL-2.0 engine with a public lineage from id's Quake III
   source. The game modules it ships by default come from the companion
   open-source FnQL-game project.
5. **It respects your copy.** You need a legitimate Steam install. FnQL ships
   no retail assets, no Valve redistributables, and never fakes Steam
   authentication.

## What's new in 0.1.0.158 (launch hook)

- **FnQL-game is on by default.** Servers host the clean-room FnQL-game game
  module, and players get its cgame and UI, while retail interop holds in
  both directions. Missing modules fall back to retail ones; any slot can be
  switched back with `vm_gameProvider retail` (and the cgame/UI equivalents).
- **Server hardening.** Hostile downloaded maps, models, images, videos and
  sounds are refused instead of corrupting memory; match stats can no longer
  inject script into the menu browser; rcon `killserver` crashes, ghost
  players, Steam-less ban collisions and ZMQ rcon lockouts are fixed.
- **Stats without libzmq.** `sv_statsJournal <name>.jsonl` appends every
  match-stats publication to a JSON-lines file.
- **More hardware for hosts.** The dedicated server builds from source for
  Linux aarch64, armv7l, ppc64le and ppc64 and moves players exactly as a
  retail x86 server does. (No release packages for those targets yet.)
- **Fixes players will notice.** No more system-wide keyboard hook stutter on
  Windows, correct 5.1/6.1 WAV channel order, Steam working from install paths
  with non-ASCII characters, and colour codes in centred menu text fixed.

## Proof points (safe to quote)

| Claim | Source |
| --- | --- |
| Lineage: Quake III SDK → ioquake3 → Quake3e → FnQ3 → FnQL | `CREDITS.md`, `README.md` |
| Protocol 91, `.dm_91` demos, BSP v47, factories, WebUI, Workshop | `README.md` Features |
| Renderers: GLx, Vulkan raster, Vulkan RTX module | `README.md`, `docs/DISPLAY.md`, `docs/RTX.md` |
| Player highlighting: rim light, stencil outline or both, in team or custom colours | `docs/VISUALS.md` |
| Cel shading: banded model lighting, model outlines, world depth-edge outlines | `docs/DISPLAY.md` (Cel Shading) |
| Shadow-mapped dynamic lights (weapon, projectile and muzzle-flash lights cast shadows) | `docs/DISPLAY.md` (Dynamic Lighting And Shadowing) |
| Sun shadows: cascaded shadow maps driven by each map's `q3map_sun` sky | `docs/DISPLAY.md` (`r_csmShadows`) |
| Enhanced liquids: refraction, reflection and ripples on water, slime and lava | `docs/DISPLAY.md` (Enhanced Liquids) |
| Audio: OpenAL, HRTF, surround, reverb, occlusion, software fallback | `docs/AUDIO.md` |
| Screenshots: PNG/TGA/JPEG/BMP, cubemaps, arbitrary-size levelshots, FFmpeg video piping | `docs/SCREENSHOTS.md`, `README.md` |
| Windowless capture mode (`com_headless 2`) renders real frames | `README.md` Getting Started |
| FnQL-game parity gates: lockstep harness requires zero divergence from retail qagame | FnQL-game `README.md` |
| Official packages: Windows x86 client, Linux i686 dedicated server | release 0.1.0.158 assets |

## Guardrails (don't say these)

- **Don't call FnQL official** or imply endorsement by id Software, Bethesda,
  ZeniMax or Valve. Every long-form post ends with the trademark note below.
- **Don't use the Quake Live logo** as FnQL branding. In-game screenshots
  that happen to show map banners are fine; key art and thumbnails use the
  fn emblem and FnQL wordmark only.
- **Don't headline ray tracing.** The RTX module's native ray-tracing path
  covers a conservative opaque world subset and keeps raster for the rest.
  On our test machine (RTX 4060 Laptop, driver 591.74) the 32-bit driver did
  not expose the ray-tracing extensions, so the Win32 release fell back to
  raster. Describe it as "an experimental RTX renderer module", never as
  "ray-traced Quake Live".
- **Don't promise platforms that don't ship.** Official packages are the
  Windows x86 client and the Linux i686 dedicated server. There is no
  supported Linux or macOS client for retail play, and x86_64 packages are
  withheld until Steam, WebUI and the retail-equivalent game path work there.
- **Don't promise account safety or anti-cheat outcomes.** Say what is true:
  FnQL is unofficial, authenticates through Steam's normal session tickets,
  never synthesizes a ticket, and server operators decide which clients they
  accept.
- **Don't hide the closed piece.** The Windows package includes a compiled,
  closed-source Steam provider (`fnql_steam.dll`). The engine is GPL-2.0; the
  provider is a separate binary that talks to Steam. Say so when asked.
- **Don't call FnQL-game "finished".** Its milestones are tracked publicly;
  bots in some modes still part from retail after 20–40 seconds, and a few
  cgame cvars remain open.

## Name and tone

- Lead with **FnQL**. Spell out "Fappin' Quake Live" once, in long-form copy,
  where the venue's culture allows it. Some subreddits and moderators may
  read the word as crude; the acronym alone is always safe.
- Voice: confident, technical, plain. Show, don't hype. Quake players spot
  marketing fluff instantly; specific facts (protocol 91, CPI scaling, demo
  compatibility) earn more trust than adjectives.
- Credit upstream generously (id Software's GPL release, ioquake3, Quake3e,
  FnQ3). The lineage is a feature.
- Answer criticism with facts and a link to the relevant doc or issue.

## Links

| What | URL |
| --- | --- |
| Latest release | https://github.com/themuffinator/FnQL/releases/latest |
| Source | https://github.com/themuffinator/FnQL |
| Game modules | https://github.com/themuffinator/FnQL-game |
| Quake Live on Steam | https://store.steampowered.com/app/282440/Quake_Live/ |
| Issues / bug reports | https://github.com/themuffinator/FnQL/issues |
| Project homepage (repo setting) | https://www.darkmatter-quake.com |
| Community Discord | `TODO: add a permanent invite before launch` |

## Trademark note (append to long-form posts)

> FnQL is an independent project and is not affiliated with, endorsed by, or
> sponsored by id Software, Bethesda, ZeniMax Media, or Valve. Quake, Quake III
> Arena, and Quake Live are trademarks of their respective owners. A legitimate
> Steam copy of Quake Live is required; FnQL ships no retail game content.
