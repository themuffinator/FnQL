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

- [x] Bundle the FnQL-game modules (qagame, cgame, ui) in every Windows and
  Linux package under `baseq3/`. The release workflow builds the commit pinned
  in `version/fnql_game.json` with MSVC Win32 and in an Ubuntu 20.04 i686
  container, runs FnQL-game's full `meson test` suite (locally 54 pass on
  each target; the retail-only gates skip without an install), and packages the
  modules with their dual-licence documents. Local probes: an x86
  `fnql.ded.exe` and an i686 Linux `fnql.ded` both loaded the staged qagame
  through `vm_gameProvider fnql` on `campgrounds` and hosted a bot.
  `tests/fnql_game_release_tests.py` gates the pin, staging and package
  checks.
- [x] Share snapshot history validation between retail native and legacy VM
  imports, preserve packet/entity counter rollover in client and demo paths,
  and reject malformed entity order and truncated areamask sizes. Native
  boundary tests and source integration gates cover the change; see the
  [snapshot history audit](QL_COMPATIBILITY_ROADMAP.md#2026-11-03-snapshot-history-audit).

- [x] Correct the engine's key-activity button to retail Quake Live's `0x1000`,
  preventing scoreboard key presses from emitting the separate `0x0800`
  stop-follow input while retaining Quake III wire-profile behavior.
  Retail executable/module inspection and ABI/usercmd
  regression tests cover the correction; interactive Proton validation is
  still pending. See [input compatibility](INPUT_COMPATIBILITY.md#key-activity-and-spectator-follow).
- [x] Make pending WebUI avatars use a steady-clock retry backoff and immediate
  availability hints from Steam, with four distinct image paths per frame.
  Duplicate requests share provider/PNG work and successful buffers; navigation
  clears abandoned requests and overflow completes with an unavailable image.
  See [avatar resource scheduling](WEBUI_BACKEND.md#pending-avatar-resources).
- [x] Remove idle synchronous WebUI request polling after an asynchronous
  notification handshake, and replace per-character browser IPC with one
  bounded request transfer. Preserve hidden lobby/connection commands, preload
  requests, per-frame limits, retries, and Unicode/buffer validation. An
  offscreen retail x86 probe verifies the callback and string-result ABI;
  deterministic scheduling and JavaScript tests cover idle and failure paths.
  See [the investigation and validation](WEBUI_BACKEND.md#native-request-scheduling-and-stutter-investigation).
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
- [x] Dedicated servers gain a default-off, command-line-only lockstep mode
  (`sv_lockstep`) for game-module differentials: fixed frames on a virtual
  clock, pinned engine seeds and clocks, networkless scripted clients driven
  by a scenario timeline, and a per-frame recording of playerStates,
  entities, configstrings and game server commands. Observed: two runs on
  the retail qagame record identically apart from the module's own CRT
  wall-clock read (`g_levelStartTime`). Source tests guard the default-off
  hooks. See [server lockstep](SERVER_LOCKSTEP.md).
- [x] Controller input is compiled into the Win32 native and SDL3 clients.
  `USE_JOYSTICK` had only been defined for Linux non-SDL builds, so the
  shipped Windows clients had no controller code and the `in_joystick*`
  settings did nothing. The Win32 and SDL backend headers now enable it for
  every build system; `in_joystick` stays off by default, so behavior is
  unchanged until a player enables it. Strict x86 native and SDL3 builds
  compile and link, source gates cover the enablement and default-off cvars,
  and both Windows release jobs fail unless `fnql.exe` imports WinMM's
  `joyGetPosEx`. See [input compatibility](INPUT_COMPATIBILITY.md#controller-backends).

## Carry Forward

- [ ] Windowed hardware probe of the Win32 WinMM controller backend in both
  `in_joystickProfile` modes, and of SDL3 gamepad hotplug, on the retail
  Steam install.
