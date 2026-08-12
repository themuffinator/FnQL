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

- [x] Usercmd and input delivery is hardened against the intermittent lost
  transitions and vertical view kicks found in the end-to-end audit. Relative
  mouse motion collected while usercmd sampling is suspended is discarded
  instead of replayed as one artificial command; the retail angle-history
  filter now retains keyboard, joystick, centerview, and cgame changes made
  outside its mouse step while clearing any hidden overshoot rejected by the
  pitch guard; engine-owned `+`/`-` bindings have
  reserved FIFO command capacity; event-time parsing and frame accounting are
  checked and bounded; and client/server usercmd counters and time deltas have
  defined 32-bit wrap behavior without changing protocol bytes. Protocol 91
  also uses retail Quake Live's unmodified server-command hash on both peers;
  its command-string transport preserves high-bit bytes as retail does. Raw
  wire text is retained only for the checksum; game/UI execution keeps the
  `%` safety rewrite. The inherited Quake III hash and ASCII-only transport
  produced a different XOR key after UTF-8 chat/name commands and
  intermittently decoded pitch, movement, and buttons as unrelated values.
  Native DirectInput uses retail's 0x200-element buffer to tolerate
  high-polling bursts and frame hitches. Focused regression tests and a
  strict-warning MSVC x86 client/server build cover the corrected and
  unaffected paths.
- [x] FnQL-hosted servers keep a client's real view angles as the spawn anchor.
  The game re-anchors `ps.delta_angles` on every spawn against the usercmd the
  engine returns from `trap_GetUsercmd()`, which the server serves from
  `client_t::lastUsercmd`. `SV_ClientEnterWorld()` had stopped recording the
  command that brings a client into the world (Quake III passes it), and the
  two acceptance-gate resets cleared the whole command instead of only its
  `serverTime`, leaving all-zero angles as the anchor across the frames after a
  `map_restart` in which the game respawns everyone. The server now anchors on
  the entering command and moves only the gate, without ever rewinding it.
  Server-side only: this does not affect an FnQL client connected to a
  retail-hosted server.
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
