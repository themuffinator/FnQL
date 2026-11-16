# Changelog

This is the player-facing release-note queue for the next FnQL release.

Keep short user-facing bullets under `Unreleased` as changes land. Start with
completed work in [`RELEASE_COMPLETION.md`](./RELEASE_COMPLETION.md), then
distil it here without duplicating every implementation detail. The release
notes publish this queue as written: `Highlights` entries lead (and are all
the Discord announcement shows), then every other entry under its category.
Write each entry as a complete sentence for players and server operators;
wrapped lines must be indented so they stay part of the entry. If `Highlights`
is empty, the first entry of each category is promoted. When a release needs
editorial control, add curated notes under [`releases/`](./releases/README.md).
Generated highlights are used only when this queue is empty. After a
successful release, CI resets `Unreleased` for the next cycle.

## [Unreleased]

### Highlights
- FnQL-game is now on by default. FnQL runs the bundled clean-room
  [FnQL-game](https://github.com/themuffinator/FnQL-game) modules instead of
  the retail ones: servers host its game module, and players get its cgame
  and UI. Retail Quake Live clients can still join FnQL servers, and FnQL
  players can still join retail servers, where FnQL uses the retail cgame and
  UI. If the FnQL-game modules are missing, FnQL loads the retail ones. To
  keep a retail module, set its provider to `retail`, for example
  `+set vm_gameProvider retail`.
- Upgrading switches you to FnQL-game too: the `vm_gameProvider "retail"`
  that earlier releases saved as their default is moved to the new default
  once, and the console says so. Settings on the command line or in
  `server.cfg` are left alone. To stay on the retail game module, set
  `vm_gameProvider retail` again; it is kept from then on.
- New 64-bit builds for Windows and Linux (x86_64) run FnQL-game. They have no
  Steam, WebUI or retail modules, which exist only for 32-bit; use the x86
  build to play with those.

### Compatibility
- Every package now carries `baseq3/fnql_game_client.pk3`, so FnQL players
  keep FnQL-game's cgame and UI on pure FnQL servers that list it in
  `sv_fnqlClientPaks`. On any other pure server, retail ones included, FnQL
  loads the retail cgame and UI and says so. Retail clients never need the
  package, and `sv_fnqlClientModules 0` turns it off on a server. Each module
  slot has its own provider cvar (`vm_gameProvider`, `vm_cgameProvider`,
  `vm_uiProvider`) taking `auto` (the default), `fnql` or `retail`.
- Bots run by the retail game module now see what Quake Live's engine gives
  them about items, health and ammunition, so they hunt, pick goals and
  dodge as they do in Quake Live.
- Server operators can set `sv_statsJournal <name>.jsonl` to have every match
  statistics publication (kills, deaths, medals, player stats, the match
  report) appended to a file in the home game directory, one JSON document
  per line, without installing libzmq.

### Rendering and Display
- _None yet._

### Audio
- _None yet._

### Builds and Packaging
- Server operators can build the FnQL dedicated server for Linux on aarch64,
  armv7l (hard-float), ppc64le and big-endian ppc64 with Meson, and host
  FnQL-game's modules built for the same target. Players move exactly as
  on a retail x86 server, so retail clients predict them without error.
  There are no release packages for these targets yet.
- Windows release builds are no longer assertion-enabled debug builds: the
  `version` string loses its `-debug` suffix, as on Linux, and an internal
  consistency check can no longer stop the game with an assertion dialog.

### Fixes
- On Linux servers, FnQL-game's moving platforms, angle conversions and
  weapon spread now match retail Quake Live exactly instead of drifting
  slightly from it.
- A game server can no longer run script in the menu browser through the
  match statistics it sends; that text is now handled strictly as data.
- Damaged or hostile maps, models, TGA images, RoQ videos and Ogg or WAV
  sounds in a downloaded package are refused when they load instead of
  corrupting memory.
- A dedicated server no longer crashes when `killserver`, or a map change
  that fails, arrives through rcon.
- A reconnect that is refused for want of a valid Steam ticket no longer
  leaves a ghost player occupying the slot.
- Steam player listings and voice keepalives no longer go quiet after a
  server restarts inside the same process, such as after `sv_idleRestart`.
- A bot no longer appears in the Steam player list under the SteamID of the
  player who held its slot before it.
- Banning or promoting a player who has no Steam identity no longer bans or
  promotes every Steam-less player at once: each such connection now has its
  own anonymous id for the access list. A player can no longer change the
  SteamID the game sees by setting `steamid` or `characterfile` in userinfo.
- Changing `sv_maxclients` on a running server no longer makes the Steam
  integration read past the player list.
- Monitoring tools that poll ZMQ rcon over short-lived connections no longer
  use up the peer table and lock later connections out.
- An idle Linux dedicated server started with `sv_idleExit 0` now shuts down
  cleanly on SIGTERM or SIGINT instead of waiting to be killed.
- Steam integration now loads when FnQL is installed in a folder whose name
  contains non-ASCII characters.
- FnQL no longer installs a system-wide keyboard hook on Windows, which could
  make typing in other applications stutter while a map loaded.
- The game module copies FnQL writes under `.tmp/native` in the profile
  folder are removed when the modules unload instead of piling up with every
  launch.
- 5.1 and 6.1 WAV music no longer plays with its channels rotated.
- Colour codes in right-aligned or centred menu text no longer tint the start
  of the line.

### Documentation and Tooling
- The renderer sweep, smoke and protocol probe scripts start their clients
  windowless unless `FNQL_HEADLESS` says otherwise, and a windowless client
  now stays hidden through `vid_restart`, renderer start-up failures and SDL
  mode changes.
