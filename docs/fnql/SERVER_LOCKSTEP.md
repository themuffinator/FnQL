# Dedicated-Server Lockstep

`sv_lockstep` turns an FnQL dedicated server into a reproducible test rig for
game-module differentials. FnQL-game's full-server differential (its gate G3,
`scripts/server_lockstep.py`) runs the same scenario twice, once on the retail
qagame and once on FnQL-game's, and diffs the two recordings frame by frame.

The mode is engine tooling. It is off by default, it can only be enabled from
the command line of a dedicated server, and every hook it adds to the engine is
a guarded call into `code/server/sv_lockstep.cpp` that does nothing while it is
off. A listen server or client ignores it and prints a warning.

## Cvars

All five are `CVAR_INIT | CVAR_PROTECTED`: set them with `+set` on the command
line; they cannot change at runtime.

| Cvar | Default | Meaning |
|---|---|---|
| `sv_lockstep` | `0` | `1` enables the mode on a dedicated server. |
| `sv_lockstepSeed` | `1` | Unsigned 32-bit decimal seed for every pinned random source. |
| `sv_lockstepScript` | empty | Scenario script, read through the game filesystem (for example `lockstep/ffa.txt` from `<fs_homepath>/baseq3/`). Empty runs with no scripted clients. |
| `sv_lockstepRecord` | empty | Recording path under `fs_homepath` (`FS_SV_FOpenFileWrite` rules). Empty records nothing. |
| `sv_lockstepFrames` | `0` | Quit after this many recorded frames; `0` runs until the server stops. |
| `sv_lockstepTraceRand` | `0` | Windows only: log every `rand()` the game module takes from a shared C runtime (the retail qagame imports it from `MSVCR100.dll`), with the frame and the caller's offset in the module, to find where two modules' random streams part. Every game init re-arms it, so a `map_restart` that loads a fresh image of the module stays traced and the draw count carries on. A module with a static runtime is not seen; FnQL-game has its own trace (`meson -Dtrace-rand=true`). |

Invalid values, script errors, a rejected scripted client and recording
write failures stop the server with an `sv_lockstep:` fatal error. As for any
fatal error, the Windows dedicated console then stays open until it is
closed; automation should watch `qconsole.log` for `Server fatal crashed`
(FnQL-game's runner does).

## Frame model and determinism

While the mode is on:

- `Com_Frame` never sleeps. It polls the sockets (`NET_Sleep( 0 )`) and runs
  the next frame at once.
- `com_frameTime` is a virtual clock. It starts at 1000000 and advances by
  exactly `1000 / sv_fps` msec per `Com_Frame`; `Com_FrameInit` (also called
  after every map load) never rewinds it. `realMsec` and the game msec passed
  to `SV_Frame` are that same frame length, so `svs.time` and `sv.time`
  advance identically on every run, and `timescale` and `fixedtime` do not
  apply.
- `SV_Frame` runs exactly one game frame of `1000 / sv_fps` msec per call
  (`sv.timeResidual` is set, not accumulated). A changed `sv_fps` takes effect
  on the next frame.
- These game-visible values are pinned:

  | Source | Normal value | Lockstep value |
  |---|---|---|
  | `GAME_INIT` random seed | `Com_Milliseconds()` | `sv_lockstepSeed + n` for the n-th `GAME_INIT` of the process |
  | engine `rand()` seed and `sv.checksumFeed` (`SV_SpawnServer`) | wall clock and OS random bytes | mixed from `sv_lockstepSeed` and the spawn count |
  | `trap_Milliseconds` (`G_MILLISECONDS`) | `Sys_Milliseconds()` | the virtual `com_frameTime` |
  | `trap_RealTime` (`G_REAL_TIME`) | local time | UTC 2026-01-01T00:00:00 plus elapsed virtual seconds, `tm_isdst` 0 |
  | match GUID import (retail slot `GENERATE_MATCH_GUID`) | `CoCreateGuid` or OS random bytes | a version-4-shaped GUID mixed from the seed and a counter |
  | `sv_serverid`, `map_restart` ids | `com_frameTime` | the virtual `com_frameTime` |
  | map pool draws (`nextmap`, the `nextmaps` vote choices) | `rand()` mixed with `Com_Milliseconds()` | `rand()` (seeded) mixed with the virtual `com_frameTime` |

What the engine cannot pin, and how the differ treats it:

- A module can read the wall clock itself. Observed (2026-10-29, retail
  qagame 1069): retail `G_InitGame` stores the CRT's `_time64()` in the
  `g_levelStartTime` serverinfo cvar (qagamex86 0x100551b6). FnQL-game's
  differ compares that key by shape (decimal digits) only.
- `sv_floodProtect` measures wall-clock time, so a burst of scripted client
  commands could be dropped on one run and not the other. The session start
  warns when the script has client commands and the cvar is on; the FnQL-game
  runner sets `sv_floodProtect 0`.
- Real network clients may still connect to a lockstep server; their packet
  timing is not deterministic. Lockstep runs use only scripted clients and
  bots.
- Run without the Steam provider (`com_steamIntegration 0`). With platform
  authentication available, the retail qagame's connect-time auth query
  (import slot 205) is answered from the provider session, which scripted
  clients do not have.

Observed (2026-10-29, campgrounds FFA, seed 1, two scripted players, 400
frames): two runs on the retail qagame produce identical recordings apart from
`g_levelStartTime`.

## Scripted clients

A scenario declares clients that are ordinary, non-bot clients to the game but
have no network channel. When the first map is running (`sv.state == SS_GAME`)
the session starts and each declared client, in declaration order:

1. takes the lowest free slot;
2. gets a zeroed `client_t` with an `NA_BAD` address, so `NET_SendPacket`
   drops anything addressed to it, and the retail protocol-91 wire profile;
3. gets its userinfo from the script (`name` defaults to the client id); the
   engine adds `ip` (the script's `address`, default the never-routed
   RFC 5737 address `192.0.2.<n>` for the n-th client) and `tld` as it does
   for every client;
4. passes the ordinary `GAME_CLIENT_CONNECT( clientNum, qtrue, qfalse )`. The
   game sets no `SVF_BOT`. A rejection is a fatal error.

At its first frame the client receives its gamestate instantly
(`CS_PRIMED`, state only, nothing is built or sent) and its first usercmd
enters it into the world through `SV_ClientEnterWorld`, which anchors the
spawn view like a real client's first command; that command is not executed
as a move. From the next frame on, each frame's usercmd goes through
`SV_ClientThink` before `GAME_RUN_FRAME`. Its `serverTime` is that frame's
`sv.time`.

Observed (retail qagame 1069): a connecting non-bot client starts on
`TEAM_SPECTATOR`; scenarios join with `clientcmd 1 <id> team f`.

Every frame the engine also:

- refreshes the client's `lastPacketTime`, so it never times out;
- reports the script's `ping` (default 0) in `ps.ping` instead of the 999 an
  unacknowledged client gets;
- acknowledges every reliable command after the frame is recorded, so the
  reliable queue never overflows;
- skips the client in `SV_SendClientMessages`: no snapshots are built for it.

A dropped scripted client (a `kick`, a game drop) is released once the engine
frees its slot. A map change puts scripted clients back to `CS_CONNECTED` like
every other client, and they re-enter at the next frame.

## Per-frame order

Inside one server frame:

1. `SV_LockstepBeginFrame`: start the session if it has not started, then run
   this frame's `cmd`, `clientcmd` and `userinfo` entries, at the point where
   console and rcon commands would run. `map` and `map_restart` work as usual.
2. The usual `SV_Frame` work: cvar-driven configstrings, `SV_CalcPings`,
   `SV_BotFrame`.
3. `sv.time += 1000 / sv_fps`.
4. `SV_LockstepClientThink`: apply this frame's `at` entries, build each
   scripted client's usercmd and run it (or enter the client into the world).
5. `GAME_RUN_FRAME`.
6. `SV_LockstepEndFrame`: record the frame, acknowledge reliable commands,
   and quit through the normal `quit` command once `sv_lockstepFrames` frames
   are recorded. The process exits 0.

Frame numbers count recorded game frames from 0. `SV_Shutdown` closes the
recording first; the session does not restart after a shutdown.

## Scenario scripts

A UTF-8/ASCII text file of at most 4 MiB and lines of at most 4095
characters. Blank lines and lines whose first non-blank character is `#` are
ignored; there are no trailing comments. Tokens are separated by spaces or
tabs; a token may be double-quoted to contain spaces (no escapes). Control
characters are rejected. Every error names the file and line and stops the
server.

```
fnql-lockstep 1                      # required first directive: format version

client <id> [<key> <value>]...       # declare scripted client <id>, with userinfo
ping <id> <msec>                     # reported ping, 0-999 (default 0)
address <id> <text>                  # the "ip" userinfo value (default 192.0.2.<n>)

at <frame> <id> <field>=<value>...   # input from <frame> on (values are sticky)
cmd <frame> <console command>        # run a server console command before <frame>
clientcmd <frame> <id> <command>     # run a client command as <id> before <frame>
userinfo <frame> <id> <key> <value>  # change one userinfo key before <frame>
```

- `<id>` is 1-31 characters of `[A-Za-z0-9_-]`. Clients are declared before
  any timeline entry, at most `sv_maxclients` of them.
- `<frame>` is 0 to 100000000. Entries for one frame run in file order.
- Userinfo keys and values may not be empty or contain `\`, `"`, `;` or
  control characters, and `ip` and `tld` belong to the engine. The userinfo
  must fit `MAX_INFO_STRING` with room for those two keys.
- `cmd` and `clientcmd` take the rest of the line as one command (`;` is
  rejected). A `clientcmd` or `userinfo` for a client that is not in the game
  yet (before frame 1) or any more is skipped with a warning. Client commands
  go through `SV_ExecuteClientCommand`, the normal path. `userinfo` changes go
  through `SV_UserinfoChanged` and `GAME_CLIENT_USERINFO_CHANGED` without the
  wall-clock userinfo rate limit.

`at` fields (each at most once per line):

| Field | Value | Default |
|---|---|---|
| `pitch`, `yaw`, `roll` | usercmd angles in degrees (`ANGLE2SHORT`), within +-1000000 | 0 |
| `dpitch`, `dyaw`, `droll` | degrees added after every frame, within +-360 | 0 |
| `buttons` | 0-65535, `none`, or names joined by `+`: `attack talk use gesture walking affirmative negative getflag guardbase patrol followme any` | 0 |
| `anykey` | `auto` sets `BUTTON_ANY` (4096, retail key activity) while a button or move is held; `0`; `1` | `auto` |
| `weapon` | 0-255, or `auto` for the playerState's current weapon | `auto` |
| `weaponprimary`, `fov` | 0-255 | 0 |
| `forward`, `right`, `up` | -128 to 127 | 0 |

Usercmd angles are what a client sends: the game adds `ps.delta_angles`,
which the spawn sets so that the spawning command faces the spawn point. A
script that starts at `yaw=0` therefore starts facing the spawn direction, and
`yaw=-90` turns right.

Example (FnQL-game `tests/lockstep/server/ffa_two_players.txt`):

```
fnql-lockstep 1
client alpha name Alpha cg_autoHop 1
client bravo name Bravo
clientcmd 1 alpha team f
clientcmd 1 bravo team f
at 0   alpha weapon=2
at 20  alpha forward=127
at 60  alpha right=127 dyaw=1.5
at 150 alpha buttons=attack
```

## Recording format, version 1

Integers are little-endian. `str` is a `u32` byte length followed by that many
bytes (no terminator). Structure payloads are the engine's raw in-memory
`playerState_t`, `entityState_t` and `usercmd_t` (32-bit x86 layout, shared
with the game modules and held by FnQL-game's gate G0); the header gives their
sizes so a decoder can refuse a mismatch.

Header:

| Field | Type |
|---|---|
| magic `FNQLLSTP` | 8 bytes |
| format version (1) | u32 |
| `sizeof( playerState_t )`, `sizeof( entityState_t )`, `sizeof( usercmd_t )` | 3 x u32 |
| `MAX_CONFIGSTRINGS`, `MAX_GENTITIES`, `sv_maxclients` | 3 x u32 |
| `sv_fps` at session start, seed, frame limit | 3 x u32 |
| `mapname`, `vm_gameProvider`, script path | 3 x str |
| scripted client count N, then N x (slot i32, id str) | u32 + records |

Then tagged records, one tag byte each:

| Tag | Payload | Meaning |
|---|---|---|
| `F` | u32 frame, i32 `sv.time`, i32 frame msec | opens a frame |
| `K` | u8 kind (0 `cmd`, 1 `clientcmd`, 2 `userinfo`), i32 client (-1 for `cmd`), str text (`userinfo`: `\key\value`) | timeline command executed for this frame |
| `U` | i32 client, `usercmd_t` | the usercmd built for a scripted client |
| `P` | i32 client, `playerState_t` | an active scripted client's playerState after the game frame |
| `E` | i32 entity number, i32 `r.svFlags`, `entityState_t` | a linked entity without `SVF_NOCLIENT` that appeared or changed |
| `R` | i32 entity number | an entity that stopped being linked and client-visible |
| `C` | i32 index, str text | a configstring that changed; frame 0 lists every non-empty one |
| `S` | i32 target (-1 broadcast, otherwise the client number the game passed), str text | a reliable server command the game issued (`trap_SendServerCommand`) |
| `O` | u32 count | game server commands dropped (more than 65536 in one frame) |
| `X` | u32 frame | closes the frame |
| `Q` | u32 frames recorded, u32 reason (0 frame limit, 1 server shutdown) | end of the recording |

A frame's records appear in the order `F`, `K`, `U`, `P`, `E`/`R`, `C`, `S`,
`O`, `X`. Entities and configstrings are deltas against the previous frame, so
a decoder keeps the current set. Server commands issued while the session
starts (the scripted clients connecting) belong to frame 0. Engine-generated
commands (`cs`, `disconnect`, engine prints) are not recorded: configstrings
are recorded as such, and the rest does not depend on the game module. A
recording without `Q` was cut short.

## Hooks

Each is a no-op while `sv_lockstep` is off:

- `code/qcommon/common.c`: `Com_Frame` (no wait, virtual clock, fixed game
  msec) and `Com_FrameInit` (virtual clock origin).
- `code/server/sv_main.cpp`: `SV_Frame` (one fixed frame, the three lockstep
  phases) and `SV_CalcPings` (scripted ping).
- `code/server/sv_game.cpp`: `GAME_INIT` seed, `G_MILLISECONDS`,
  `G_REAL_TIME`, the match GUID import, and server-command capture in
  `SV_GameSendServerCommand`.
- `code/server/sv_init.cpp`: `SV_Init` (cvar registration), `SV_SpawnServer`
  (`rand()` seed and checksum feed), `SV_Shutdown` (close the recording).
- `code/server/sv_client.cpp`: `SV_UserinfoChanged` (scripted `ip`).
- `code/server/sv_factory.cpp`: `RuntimeRandomIndex` (map pool draws).
- `code/server/sv_snapshot.cpp`: `SV_SendClientMessages` (no snapshots for
  scripted clients).

## Validation

- `tests/server_lockstep_source_tests.py`: default-off cvars, the
  dedicated-only latch, every hook gated, the build registrations, and this
  document.
- FnQL-game `scripts/server_lockstep.py --self-check`: two retail-qagame runs
  of one scenario must not diverge. Without it the differential means nothing.
- FnQL-game `scripts/server_lockstep.py`: the retail-versus-FnQL-game
  differential itself (Meson suite `differential`).
