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
- Fixed ghost mouse input on the native Windows build: closing an in-game menu with a click, or clicking back into the game window, could kick the view (for example pitching it up, or spinning it far enough that forward briefly felt like backward) without any physical mouse movement. Stale legacy mouse messages queued around menu and focus transitions were being converted into gameplay deltas measured from the window centre; they are now discarded while raw input or DirectInput owns the mouse.
- Fullscreen at a resolution other than the desktop's no longer drops the monitor to its default refresh rate (typically 60Hz, with far more visible tearing). The display mode change now requests the desktop refresh rate, and falls back to the driver default only when the chosen resolution cannot support it. An explicit `r_displayRefresh` still wins.

### Documentation and Tooling
- _None yet._
