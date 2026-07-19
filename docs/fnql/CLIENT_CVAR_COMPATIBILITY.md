# Client CVar Compatibility

## Scope

This note records the engine-owned `cl_*` compatibility surface. It does not
claim ownership of game-code CVars, and it does not turn UI string references
into authoritative engine state. Retail-compatible behavior remains available
without removing FnQL's established client extensions.

## Evidence

Observed retail facts in this audit came from the user's legitimate Steam
installation:

- executable: `quakelive_steam.exe`
- file version: `0.1.0.739`
- size: `2,107,904` bytes
- SHA-256: `C926FE9F6C851E00B3B9332E88903AD01F28FDD60454873891C0158F5DED1299`

The executable string inventory was classified against the reconstructed
client registration and behavior notes in QLSRP. QLSRP is behavioral evidence,
not implementation text: FnQL's timing and capture code is an independent,
tested implementation fitted to the existing FnQ3-derived client architecture.

The retail binary exposes these client names:

```text
cl_allowConsoleChat cl_anglespeedkey cl_anonymous cl_autoTimeNudge
cl_avidemo cl_avidemo_latch cl_avidemo_maxtime cl_avidemo_mintime
cl_cdkey cl_currentServerAddress cl_debugMove cl_demoRecordMessage
cl_downloadItem cl_downloadName cl_downloadTime cl_forceavidemo
cl_freelook cl_freezeDemo cl_maxpackets cl_maxPing cl_motd
cl_motdString cl_mouseAccel cl_mouseAccelDebug cl_mouseAccelOffset
cl_mouseAccelPower cl_mouseSensCap cl_nodelta cl_packetdup cl_paused
cl_pitchspeed cl_platform cl_quitOnDemoCompleted cl_run cl_running
cl_serverStatusResendTime cl_shownet cl_showSend cl_showTimeDelta
cl_timeNudge cl_timeout cl_viewAccel cl_yawspeed
```

This is not interpreted as a flat `CL_Init` registration list:

- `cl_cdkey` uses the legacy CD-key storage interface and is not a cvar.
- `cl_currentServerAddress` is published dynamically when connecting.
- `cl_paused` and `cl_running` are owned by qcommon, not the client table.
- `cl_downloadCount` and `cl_downloadSize` are retained, non-authoritative UI
  progress publications documented by QLSRP; they do not appear in this retail
  executable's literal-name inventory.

The source regression test fails if any remaining binary-observed cvar loses an
engine owner.

## Resolved Compatibility Gaps

| Contract | FnQL behavior |
|---|---|
| Console chat | `cl_allowConsoleChat` owns the retail bare-console-chat gate. Existing `con_autoSay` and raw-chat handling remain supported. |
| Console notifications | Retail registers no `con_notifytime`; `Con_DrawNotify` renders only the live chat-entry strip and never overlays general console print. The retained `con.times` / `[skipnotify]` bookkeeping remains intact because it is still present in the retail print pipeline. |
| Time nudge | `cl_timeNudge` uses the retail `[-20, 0]` bound; `cl_autoTimeNudge` applies the spectator/local-server gates and retained negative half-ping selection. |
| Demo capture | `cl_avidemo`, its latch, minimum time, and maximum time drive deterministic silent screenshot capture and fixed frame timing. This stays separate from FnQL's AVI/video-pipe recorder. |
| Demo lifecycle | `cl_quitOnDemoCompleted` queues a clean quit after the next-demo action; `cl_freezeDemo` behavior remains intact. |
| Recording HUD | Retail `cl_demoRecordMessage` modes are canonical. Existing `cl_drawRecording` configurations migrate and remain a synchronized alias. |
| Platform identity | Read-only `cl_platform=1` publishes the retail Steam-client contract without claiming that remote authorization succeeded. |
| Networking | Retail timeout, packet, delta, resend, and diagnostic controls keep their recovered defaults/bounds while retaining compatible FnQL persistence metadata. |
| Download UI | Retail name/item/time publications and the retained byte counters have stable, inert defaults and continue to be updated by existing download paths. |
| Input | Retail yaw, pitch, run, freelook, mouse-acceleration, sensitivity-cap, and debug controls retain their owners and runtime wiring. |

FnQL-only controls such as `cl_autoNudge`, `cl_aviFrameRate`,
`cl_allowDownload`, `cl_mouseAccelStyle`, and `cl_renderer` are deliberate
extensions. They remain available, and retail behavior takes ownership only
when its corresponding retail control is selected. Existing explicit user
values remain subject to the cvar system's normal archive/config lifecycle.
Changed retail defaults affect fresh or reset configurations rather than
silently rewriting config files, while recovered retail bounds still clamp
out-of-contract values during registration.

## Non-Regression Gates

The compatibility surface is checked at three levels:

1. `client_cvar_compat_tests` exercises retail time-nudge boundaries,
   spectator/local behavior, retained auto selection, the preserved FnQL auto
   mode, avidemo latch/start/stop boundaries, and invalid numeric inputs.
2. `client_cvar_compat_source_tests.py` inventories all observed retail names,
   their exceptional owners, every recovered default, important flags/ranges,
   behavior wiring, and representative FnQL extensions.
3. A strict-warning full client build compiles and links every affected client
   translation unit. Runtime probes must use `+set r_fullscreen 0`; x64 probes
   can mount and initialize retail assets but cannot load the retail install's
   x86-only native UI module, so complete retail runtime validation remains a
   Win32 gate.
