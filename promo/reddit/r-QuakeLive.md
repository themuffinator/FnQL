# r/QuakeLive — launch post

**When:** launch day, 15:00–18:00 UTC. **Flair:** whatever the sub uses for
news/tools (check the flair list). **Type:** image post with the hero
screenshot, or a text post with the gallery link in the first line. Image
posts get more reach; text posts allow the full body. Recommended: **gallery
post**, in this order:

1. `images/screenshots/fnql-hiddenfortress-rail.jpg` (hero)
2. `images/features/fnql-feature-highlight-modes.jpg`
3. `images/features/fnql-feature-sun-shadows-action.jpg`
4. `images/features/fnql-feature-celshading.jpg`
5. `images/features/fnql-feature-hdr.jpg`

Then paste the body below as the first comment straight away. Give each
gallery image the caption from `moddb/MEDIA.md`.

---

## Title (pick one)

1. FnQL: an open-source engine for Quake Live that still joins retail servers (Vulkan, HDR, raw input, HRTF)
2. I've been building a modern engine for the Quake Live you already own: FnQL 0.1.0.158 is out
3. FnQL: Quake Live on a modern open-source engine. Same servers, same demos, better everything else

Title 2 works best if you are comfortable posting as the developer; it
invites questions. Use 1 for a neutral announcement.

---

## Body

Hey all. FnQL is an open-source replacement engine for **retail Quake Live on
Steam**. You keep your install, your profile, your configs and your Workshop
content; you drop FnQL into the Quake Live folder and launch `fnql.exe`
instead of the retail exe.

The one rule the whole project follows: **retail compatibility comes first.**

- **FnQL players join retail servers** (protocol 91), and **retail players
  join FnQL servers** and get exactly what a retail server would send them.
- `.dm_91` demos, BSP v47 maps, factories, the WebUI menus, Steam integration and
  Workshop downloads all work.
- Steam authentication goes through Steam's normal session tickets. Nothing
  is faked.

**What you get on top:**

- **Renderers:** GLx (modern OpenGL) and Vulkan, with HDR, MSAA and
  supersampling, render scaling, bloom, tone mapping, soft particles and
  better liquids and fog. All of it optional.
- **Input:** raw mouse input with Quake Live's own CPI scaling and accel
  math, so your sens carries over. Mouse side buttons and the wheel bind
  properly, plus controller support.
- **Visibility:** optional enemy/team rim lighting and outlines, with your
  own colours.
- **Audio:** OpenAL with HRTF for headphones, surround, reverb and occlusion,
  plus output-device selection and live device recovery.
- **Console and QoL:** scalable TTF console, mouse selection and clipboard,
  live cvar/map completion, high-resolution and high-refresh displays, fast
  Alt+Enter, lower background CPU.
- **Capture:** PNG/JPEG screenshots at any size, cubemaps, FFmpeg video
  piping.

**New in 0.1.0.158:**

- The open-source **FnQL-game** modules are now on by default. Servers host
  its game module, players get its cgame and UI, and retail interop still
  works both ways. Any slot can be switched back to the retail module with
  one cvar (`vm_gameProvider retail`, and the `vm_cgameProvider`/`vm_uiProvider`
  equivalents).
- A batch of server hardening: hostile downloaded content is refused instead
  of corrupting memory, rcon crash fixes, no more ghost players after refused
  reconnects, and a JSON-lines match stats journal that needs no libzmq.
- No more system-wide keyboard hook on Windows (the "typing stutters while a
  map loads" bug), correct 5.1 WAV channel order, and Steam now works from
  install paths with non-ASCII characters.

**Download:** https://github.com/themuffinator/FnQL/releases/latest
(Windows x86 client, Linux i686 dedicated server). Install is "extract into
your Quake Live folder, leave Steam running, run `fnql.exe`."

**Source (GPL-2.0):** https://github.com/themuffinator/FnQL

It's built on id's GPL Quake III source through ioquake3, Quake3e and FnQ3,
so a lot of credit goes to those projects.

Bug reports with steps (and your `qconsole.log` if you can) are hugely
appreciated: https://github.com/themuffinator/FnQL/issues

*FnQL is an independent project, not affiliated with or endorsed by id
Software, Bethesda, ZeniMax or Valve. You need your own Steam copy of Quake
Live; FnQL ships no game content.*

---

## Prepared replies

**"Will I get banned / is this a cheat?"**
> FnQL is an engine replacement, not a cheat: no wallhacks, no aim
> assistance. It logs in
> through Steam's normal session tickets and never fakes them. It is
> unofficial, though, so server admins decide which clients they accept.

**"Why not just use the retail client?"**
> If retail works for you, great. FnQL is for people who want Vulkan/HDR,
> high-refresh displays, better audio and a modern console without leaving
> the retail servers and community.

**"Linux / Mac client?"**
> The supported packages are the Windows x86 client and the Linux i686
> dedicated server. Retail's client-side modules and WebUI are Win32-only, so
> a native Linux client can't run the retail game path yet. Proton/Wine
> users: the Windows build's Steam integration starts under Proton.

**"Ray tracing?"**
> There's an experimental RTX renderer module, but it's not the headline.
> Its native RT path covers a conservative subset of world geometry, and the
> 32-bit driver on our test machine didn't expose the ray-tracing
> extensions. GLx
> and Vulkan are the renderers to use.

**"What's FnQL-game?"**
> Open-source game modules (server game, client game and UI) rewritten from
> observed retail behaviour. A lockstep harness runs them side by side with
> retail's server module and requires zero divergence before a milestone
> closes. It's the default now, and one cvar per module brings back retail's.

**"Is it open source? What's fnql_steam.dll?"**
> The engine is GPL-2.0. The Windows package also includes a small compiled
> Steam bridge (`fnql_steam.dll`) that isn't open source; it talks to the
> `steam_api.dll` from your own retail install. Without it, FnQL runs in
> non-Steam mode.
