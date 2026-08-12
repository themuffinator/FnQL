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
- Matched retail protocol-91 usercmd checksum hashing and preserved UTF-8 bytes
  in Quake Live command strings. FnQL clients no longer send retail servers
  corrupted look, movement, or button fields after non-ASCII chat/name
  commands, and FnQL servers now decode retail clients the same way as Quake
  Live.

### Rendering and Display
- _None yet._

### Audio
- _None yet._

### Builds and Packaging
- _None yet._

### Fixes
- Fixed intermittent missed movement/button input and sudden up/down view kicks
  around pauses, connection transitions, command-buffer pressure, and filtered
  keyboard/joystick look changes.
- FnQL-hosted servers no longer discard a player's view angles when spawning
  them, which could throw the spawn view to an unrelated pitch after a
  `map_restart`.

### Documentation and Tooling
- Added `cl_debugViewAngles`, which prints one line whenever the server
  re-anchors the view (spawn, teleport, or a pitch clamp) for diagnosing spawn
  view-angle reports, including the server-implied command and a `DESYNC`
  marker when it differs from the matching local command.
