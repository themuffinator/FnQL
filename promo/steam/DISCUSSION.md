# Steam — Quake Live Community Hub discussion post

**Where:** https://steamcommunity.com/app/282440/discussions/ → General
Discussions → Start a New Discussion. Read the pinned rules first; if they
forbid project announcements, ask a moderator, or publish only the Steam Guide.

**When:** launch day, after the Reddit post.

Steam discussions use Steam's own BBCode, not Markdown. Paste everything
between the lines as-is. Steam may hold posts containing links for review.

---

**Title:**

FnQL: open-source engine for Quake Live (joins retail servers, Vulkan/HDR, raw input, HRTF)

**Body:**

```
[h1]FnQL 0.1.0.158 is out[/h1]

FnQL is an open-source replacement engine for the Steam release of Quake Live. You keep your install, profile, configs and Workshop content; FnQL just swaps the engine.

[h2]Still Quake Live[/h2]
[list]
[*][b]Join retail servers[/b]: FnQL speaks protocol 91.
[*][b]Host retail players[/b]: retail clients join FnQL servers and see exactly what a retail server sends.
[*][b]Your content works[/b]: .dm_91 demos, maps, factories, the WebUI menus, Steam integration and Workshop downloads.
[*][b]Real Steam authentication[/b]: session tickets go through Steam's normal path and are never faked.
[/list]

[h2]Better engine[/h2]
[list]
[*][b]GLx and Vulkan renderers[/b] with HDR, MSAA and supersampling, bloom, tone mapping, soft particles and better liquids and fog.
[*][b]Raw mouse input[/b] with Quake Live's own CPI scaling and acceleration, so your sensitivity carries over.
[*][b]Optional player highlighting[/b]: rim light and/or outlines in your own enemy and team colours.
[*][b]OpenAL audio[/b] with HRTF for headphones, surround, reverb and occlusion.
[*][b]Modern console[/b]: scalable text, mouse selection, clipboard, live completion.
[*][b]High-resolution and high-refresh displays[/b], borderless mode, fast Alt+Enter, lower background CPU.
[*][b]Capture tools[/b]: PNG/JPEG screenshots at any size, cubemaps, FFmpeg video piping.
[/list]

[h2]New in this release[/h2]
[list]
[*]The open-source FnQL-game modules are now the default. One cvar per module switches back to retail's (vm_gameProvider retail, and the vm_cgameProvider / vm_uiProvider equivalents).
[*]Server hardening: hostile downloaded content is refused, rcon crash fixes, no ghost players, JSON match-stats journal without libzmq.
[*]No more system-wide keyboard hook on Windows (other apps no longer stutter while a map loads).
[/list]

[h2]Install[/h2]
[olist]
[*]Launch retail Quake Live once so Steam creates your profile.
[*]Download the Windows package from [url=https://github.com/themuffinator/FnQL/releases/latest]the latest release on GitHub[/url].
[*]Extract it into your Quake Live folder (right-click Quake Live in your library → Manage → Browse local files).
[*]Leave Steam running and launch fnql.exe.
[/olist]

Source code (GPL-2.0): [url=https://github.com/themuffinator/FnQL]github.com/themuffinator/FnQL[/url]
Bug reports: [url=https://github.com/themuffinator/FnQL/issues]GitHub issues[/url]

[i]FnQL is an independent project and is not affiliated with, endorsed by, or sponsored by id Software, Bethesda, ZeniMax Media, or Valve. You need your own copy of Quake Live; FnQL ships no game content.[/i]
```

---

## Replies

Reuse the prepared replies in `../reddit/r-QuakeLive.md`. On Steam,
"will I get VAC banned?" may come up. Answer with what is true and nothing
more:

> FnQL contains no cheats and doesn't touch other games or Steam itself. It
> authenticates through Steam's normal session tickets. It is an unofficial
> client, so I can't speak for Valve, Bethesda or individual server admins;
> admins decide which clients they accept.
