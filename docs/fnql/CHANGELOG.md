# Changelog

This is the player-facing release-note queue for the next FnQL release.

Keep short user-facing bullets under `Unreleased` as changes land. Start with
completed work in [`RELEASE_COMPLETION.md`](./RELEASE_COMPLETION.md), then
distil it here without duplicating every implementation detail. When a release
needs editorial control, add curated notes under
[`releases/`](./releases/README.md); otherwise the workflow turns this queue,
commits, and diffs into a compact `Highlights` section. After a successful
release, CI resets `Unreleased` for the next cycle.

## [Unreleased]

### Highlights
- _None yet._

### Compatibility
- Server snapshots now follow retail Quake Live's game-module visibility rules:
  Freeze Tag thaw ticks are hidden from opponents instead of being sent to
  everyone, and spectators following a player keep their own visibility.
- Server admins (as the game module defines them, for example through
  `access.txt`) can run console commands in-game with `rcon <command>`, as on
  retail servers.
- The Steam server browser's player list shows bots as `(Bot) <name>` and uses
  the game module's score.

### Rendering and Display
- _None yet._

### Audio
- _None yet._

### Builds and Packaging
- Windows release builds now fail if the client is built without controller
  support.

### Fixes
- Controllers work in the Windows client. Controller support had been left out
  of the Windows build, so `in_joystick` and the Gamepad settings did nothing.
  Turn it on with `in_joystick 1`; `in_joystickProfile 1` (with
  `in_joyBallScale 1`) selects Quake Live's analog movement and look mapping.
- Keep snapshot history and demo recording safe across sequence-counter rollover,
  and reject stale snapshot slots and malformed entity ordering.

### Documentation and Tooling
- `sv_lockstepQueries 1` records the game module's query answers in lockstep
  recordings, so game-module differentials can compare them.
