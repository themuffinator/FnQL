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
- Keep the complete decorated game window reachable across monitor, work-area,
  and DPI changes on supported desktop window systems.
- Soften the scene and HUD behind an open in-game menu, so the menu reads as the
  foreground instead of competing with the live view. Set `cl_menuBlur 0` to
  keep the frame sharp, or a value between 0 and 1 for a lighter effect.
  Gameplay and the scoreboard are never softened. Available on all three
  renderers; requires the framebuffer path (`r_fbo 1`).

### Audio
- _None yet._

### Builds and Packaging
- Allow the Make cleanup goals to run on a fresh clone, before any Meson
  subproject has been downloaded or any compiler has been installed.

### Fixes
- Keep the mouse cursor usable in the in-game menu, scoreboard, and other
  cgame/UI overlays while supersampling is enabled.

### Documentation and Tooling
- _None yet._
