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
- _None yet._

### Rendering and Display
- _None yet._

### Audio
- _None yet._

### Builds and Packaging
- _None yet._

### Fixes
- Fixed scoreboard key presses unexpectedly leaving spectator follow mode.
- Fixed bots on servers running the retail game module passing an invalid
  target to the engine when predicting where an enemy went out of view.
- Fixed retail game modules receiving a corrupted upper half of each
  player's SteamID.

### Documentation and Tooling
- Servers can load the clean-room FnQL-game server module with
  `vm_gameProvider fnql`; the retail module stays the default, and `vminfo`
  now shows which module is running.
- Dedicated servers gain an off-by-default lockstep test mode
  (`+set sv_lockstep 1`) that replays scripted players deterministically and
  records every frame, for comparing game modules.
