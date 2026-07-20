# Changelog

This is the pending release-note queue for the next FnQL release.

Keep short user-facing bullets under `Unreleased` as changes land. During release publishing, the workflow asks GitHub Copilot to dedupe and categorize the notes for the GitHub release details, then clears this section for the next cycle.

## [Unreleased]

### Highlights
- Fixed the retail Start Match flow from menu selection through active gameplay.

### Compatibility
- Populate the retail WebUI map catalog from the validated map pool, including per-gametype availability instead of exposing only Campgrounds.
- Seed Start Match settings before the retail component mounts, eliminating `NaN` controls and preserving the expected defaults.
- Release and pause the WebUI before map/connect transitions so loading and in-game rendering regain ownership immediately.

### Rendering and Display
- Keep windowed gamma correction inside the renderer instead of changing the Windows desktop gamma ramp.

### Audio
- _None yet._

### Builds and Packaging
- Link zlib statically in MSVC release builds and reject packages that import `z-1.dll`.

### Fixes
- Correct WebUI pointer projection on scaled Windows desktops so dropdowns and other controls receive clicks at their drawn positions.
- Restore the native in-game menu on Escape after launching a match from the WebUI.

### Documentation and Tooling
- _None yet._
