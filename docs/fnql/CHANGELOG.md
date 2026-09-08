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
- Reduce avatar-loading work with Steam event-driven retries, timed backoff,
  shared image fetches, and a per-frame budget.
- Remove recurring synchronous WebUI request polling during gameplay and
  transfer queued menu commands in one bounded read, addressing a remaining
  source of stutter with the WebUI enabled.

### Documentation and Tooling
- _None yet._
