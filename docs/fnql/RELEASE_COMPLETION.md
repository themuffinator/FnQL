# FnQL Release Completion List

Use this file as the source list for release changelog entries.

Process:

1. Add completed, user-visible work under **Ready For Changelog**.
2. Distil it into short player-facing bullets in [`CHANGELOG.md`](./CHANGELOG.md)
   as work lands.
3. When a release needs curated notes, create
   [`releases/<release-tag>.md`](./releases/README.md) or
   `releases/<version>.md`. The manual release workflow uses that tracked file
   before generated notes.
4. If no curated file exists, the workflow builds the `Highlights` section
   from the Unreleased queue, commits, and relevant diffs.
5. After publication, remove or move shipped items from this working list and
   keep unfinished work in **Carry Forward**. CI clears the Changelog's
   `Unreleased` queue separately.

Keep observed retail Quake Live behavior separate from inference. Do not add
game-code reconstruction, unverified compatibility claims, or work that is
only planned.

## Ready For Changelog

- [x] Manual releases publish a Quake Live-styled Discord announcement with
  release highlights, the `@quake-live` and playtester roles, a `#fnql`
  feedback link, and platform download links.

## Carry Forward

- [ ] _None yet._
