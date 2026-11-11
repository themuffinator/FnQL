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
- _None yet._

### Rendering and Display
- _None yet._

### Audio
- _None yet._

### Builds and Packaging
- FnQL now also builds for x86_64 on Windows and Linux, with the FnQL-game
  modules for that target. The x86_64 build has no retail game modules, Steam
  or WebUI (all 32-bit only), so it runs FnQL-game; play with the retail
  modules on the x86 build.

### Fixes
- _None yet._

### Documentation and Tooling
- _None yet._
