# r/quake — second-wave post

**When:** about four days after the r/QuakeLive post, 15:00–18:00 UTC.
**Angle:** lineage and engine craft, for sourceport fans who may not play QL
daily. Don't reuse the r/QuakeLive text.
**Type:** gallery post (4–6 images: two or three screenshots from
`images/screenshots/` plus the cel-shading, dynamic-light-shadow and HDR
comparisons from `images/features/`), body as the first comment.

---

## Title (pick one)

1. From id's GPL Quake III source to Quake Live: FnQL is a Quake3e-lineage engine for retail QL
2. FnQL: what happens when you take Quake3e's lineage and aim it at retail Quake Live
3. Quake Live on a modern id Tech 3 engine: GLx and Vulkan shots from FnQL

---

## Body

Quake Live's engine never got the sourceport treatment Quake III did. FnQL
is my attempt at that: an engine for **retail Quake Live on Steam** that
follows the family tree

**Quake III Arena GPL source → ioquake3 → Quake3e → FnQ3 → FnQL**

and then does the Quake Live-specific work: protocol 91, `.dm_91` demos,
BSP v47 maps, factories, Steam integration, Workshop content and the retail
Awesomium WebUI.

What it is **not**: a remake or an asset pack. You need your own Steam copy
of Quake Live, and FnQL ships no game content.

A few things sourceport people might find interesting:

- **Two renderers you'd actually use:** GLx (an OpenGL path with GPU-cached
  world geometry, HDR, MSAA/supersampling, bloom and tone mapping) and
  Vulkan raster. An RTX module exists, but it's experimental.
- **Retail-accurate input:** raw mouse with QL's CPI scaling, signed accel
  and sensitivity caps, while the classic Quake3e/ioq3 mouse styles remain
  available.
- **Proof, not vibes, for game code:** the default game modules come from the
  companion **FnQL-game** project (baseq3a baseline, Quake Live behaviour
  rewritten from observed retail behaviour). A lockstep harness runs the
  retail server module and FnQL-game's side by side, and milestones only
  close at zero divergence. FnQL clients still join retail servers and
  retail clients still join FnQL servers.
- **A windowless client:** `+set com_headless 2` renders real frames without
  ever opening a window, which is how every screenshot in this post was
  captured.
- **Dedicated servers beyond x86:** the server builds from source for Linux
  aarch64, armv7l, ppc64le and ppc64 and moves players exactly as a retail
  x86 server does, so retail clients predict them without error.

Latest release (0.1.0.158) and downloads:
https://github.com/themuffinator/FnQL/releases/latest

Source (GPL-2.0): https://github.com/themuffinator/FnQL
Game modules: https://github.com/themuffinator/FnQL-game

Huge thanks to id Software for the GPL release and to the ioquake3,
Quake3e and FnQ3 maintainers; FnQL wouldn't exist without them.

*Not affiliated with or endorsed by id Software, Bethesda, ZeniMax or Valve.*

---

## Prepared replies

**"How is this different from Quake3e / ioq3?"**
> Those target Quake III Arena. FnQL targets the retail Quake Live
> runtime: its protocol, demo format, BSP version, module ABI, WebUI and
> Steam services, so it runs the game QL players actually play and talks to
> retail servers and clients.

**"Can I use it for Quake III?"**
> It keeps much of the Q3 heritage, but its target and testing are retail
> Quake Live. For Q3A, Quake3e or FnQ3 are the right tools.

**"Why 32-bit?"**
> Retail Quake Live's modules and WebUI are 32-bit Windows binaries, and
> loading them needs a 32-bit process. FnQL builds for x86_64 too, but those
> packages are withheld until Steam, WebUI and the retail-equivalent game
> path work there.
