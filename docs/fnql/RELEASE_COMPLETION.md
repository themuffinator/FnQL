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
- [x] Native Windows input no longer converts stale legacy mouse messages into
  gameplay view deltas. While raw input or DirectInput owns the device, a
  legacy `WM_MOUSEMOVE`/button message can only be one queued before the
  (re)registration — the click that closed an in-game menu, or crossing the
  window on a focus change — and feeding it into the delta path kicked the
  view by (position − window centre) without any physical mouse motion
  (reported as ghost pitch flicks and the view spinning so movement felt
  reversed). The message pump now feeds the legacy lane only while the legacy
  Win32 mouse is the active source, matching the transition protection the SDL
  and X11 backends already had.
- [x] Native Windows fullscreen mode changes request the desktop refresh rate
  when `r_displayRefresh` is 0, instead of leaving the frequency unspecified
  and letting the driver drop to the mode default (typically 60Hz with heavy
  tearing) for any non-desktop resolution. If the resolution cannot support
  the desktop rate the mode set retries at the driver default, and an explicit
  `r_displayRefresh` is honored unchanged, so previously working setups keep
  their behavior.

## Carry Forward

- [ ] _None yet._
