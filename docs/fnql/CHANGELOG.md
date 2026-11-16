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
- _None yet._

### Compatibility
- Players can run the FnQL-game cgame and UI with `vm_cgameProvider fnql` and
  `vm_uiProvider fnql` (`auto` takes them only if they declare retail wire
  parity, and the same value works for `vm_gameProvider`). On a pure server
  they run only from an FnQL-game client package (`fnql_game_client.pk3`) the
  server has loaded and lists in `sv_fnqlClientPaks`; on any other pure
  server, retail ones included, FnQL loads the retail modules and reports
  them. Retail clients never need the package, and `sv_fnqlClientModules 0`
  turns it off on a server.
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
- FnQL now also builds for x86_64 on Windows and Linux, with the FnQL-game
  modules for that target. The x86_64 build has no retail game modules, Steam
  or WebUI (all 32-bit only), so it runs FnQL-game; play with the retail
  modules on the x86 build.
- Windows release builds are no longer assertion-enabled debug builds: the
  `version` string loses its `-debug` suffix, as on Linux, and an internal
  consistency check can no longer stop the game with an assertion dialog.

### Fixes
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
