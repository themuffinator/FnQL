# ModDB news article — launch / 0.1.0.158

**Type:** News. **Category:** Releases. **Header image:**
`images/keyart/fnql-keyart-1920x1080.jpg`. Publish on launch day; ModDB
articles are moderated too, so submit it early as a draft.

Reuse this structure for every release: a hook, three highlights, the full
notes link, the download, and the trademark line.

---

## Title

FnQL 0.1.0.158: open game modules on by default, and a hardened server

## Summary (the article teaser field)

FnQL, the open-source engine for retail Quake Live, now runs its own
open-source game modules by default, while still joining retail servers and
hosting retail players.

## Body

FnQL is an open-source engine for the retail Steam release of Quake Live.
It keeps everything that makes Quake Live *Quake Live* (retail servers and
clients, demos, maps, the WebUI, Workshop content) and adds modern
renderers, input, audio and quality-of-life on top. Today's release is the
biggest step yet toward a fully open Quake Live stack.

### Open game modules, on by default

FnQL now runs the companion **FnQL-game** modules out of the box. Servers
host its game module and players get its client game and UI. Interop with
retail holds in both directions: retail clients join FnQL servers, and FnQL
players join retail servers, where FnQL switches to the retail client
modules automatically and says so.

Prefer retail's modules? One cvar per slot brings them back:
`vm_gameProvider retail`, `vm_cgameProvider retail`, `vm_uiProvider retail`.

### A tougher dedicated server

- Damaged or hostile maps, models, images, videos and sounds in downloaded
  packages are refused at load instead of corrupting memory.
- Match statistics can no longer inject script into the menu browser.
- No crash from rcon `killserver` or failed map changes; no ghost players
  after a refused reconnect; Steam listings survive in-process restarts.
- Players without a Steam identity each get their own anonymous id, so one
  ban no longer hits them all.
- New `sv_statsJournal` writes every match-stats publication as JSON lines,
  no libzmq required.
- The server now builds from source for Linux aarch64, armv7l, ppc64le and
  ppc64, moving players exactly as a retail x86 server does.

### Fixes you'll feel

- No system-wide keyboard hook on Windows any more, so other apps no longer
  stutter while a map loads.
- 5.1 and 6.1 WAV music plays with the right channel order.
- Steam integration works from install folders with non-ASCII names.
- Colour codes in centred and right-aligned menu text render correctly.

### Get it

- Download: https://github.com/themuffinator/FnQL/releases/latest (or the
  mirror on this page)
- Full release notes: https://github.com/themuffinator/FnQL/releases/tag/0.1.0.158-20261116-28e575d7
- Source (GPL-2.0): https://github.com/themuffinator/FnQL

Install: extract the Windows package into your Quake Live folder, leave
Steam running and launch `fnql.exe`. You need your own Steam copy of Quake
Live.

Found a bug? Please open an issue with steps and your console log:
https://github.com/themuffinator/FnQL/issues

*FnQL is an independent project and is not affiliated with, endorsed by, or
sponsored by id Software, Bethesda, ZeniMax Media, or Valve.*
