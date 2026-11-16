# Dedicated Server Replacement Plan

## Goal

An operator who runs Quake Live servers today has no reason to keep the
retail Quake Live Dedicated Server (QLDS, `qzeroded`), minqlx, or the tools
built around them. FnQL's dedicated server does everything they do, on more
platforms, with stronger safety and diagnostics. An existing deployment moves
over by changing the executable name.

The plan also covers what no Quake Live tool offers: multi-view demos (MVD),
a live spectator relay, and advanced playback for legacy and MVD demos. That
is the V track, modeled on q2repro, the Q2PRO lineage for the Quake II
rerelease.

This plan is section 8 of
[`QL_COMPATIBILITY_ROADMAP.md`](QL_COMPATIBILITY_ROADMAP.md). Its boundaries
and evidence rules apply here unchanged, and so do the
[`GAME_MODULES.md`](GAME_MODULES.md) interoperability rules. Nothing from
minqlx, shinqlx or QLSRP is copied.

## Why FnQL can do better, not just catch up

minqlx exists because `qzeroded` is closed. It preloads into the retail
process, detours functions at fixed addresses, and reads and writes qagame's
private structures at fixed offsets. Its events are partly scraped back from
the ZMQ stats socket. Every QLDS update risks breaking it, it runs only on
Linux, and nothing about it can be tested deterministically.

FnQL owns the whole server: the engine here, and the game module in
FnQL-game. So the plan follows six rules:

1. **Features live where the knowledge lives.**
   - What the engine sees is implemented in the engine, and works with retail
     qagame and FnQL-game alike: connections, client and server commands,
     chat, userinfo, configstrings, console, RCON, bans by identity or
     address, logging, scheduling and persistence.
   - What only the game knows is implemented in FnQL-game: spawns, kills
     with their means of death, inventory, items, votes, rounds and team
     moves.
   - Nothing is patched or poked.
2. **One typed interface, not memory offsets.** A versioned server extension
   ABI is the only way extensions see the server. Each engine and module
   publishes what it supports through capability bits, so an extension gets
   an honest "unsupported", never a crash or a silent no-op, and keeps
   working across updates.
3. **Common needs are first-party.** The jobs operators install the default
   minqlx plugins for ship as first-party extensions on that ABI: bans,
   permissions, silence, MOTD, name and clan rules, Workshop, logs. Most
   servers then need no scripting at all, and the ABI is proven by FnQL's own
   use.
4. **Compatibility is a bridge, not the core.** The minqlx-compatible Python
   layer exists so existing plugins move over unchanged. It sits on top of
   the native ABI and gains nothing the native ABI lacks.
5. **Everything is testable.** The lockstep harness
   ([`SERVER_LOCKSTEP.md`](SERVER_LOCKSTEP.md)) runs extensions
   deterministically. Their effects become reproducible recordings, which no
   QLDS tool has.
6. **Engine fixes replace hooks.** Each popular `LD_PRELOAD` hook (for
   example the 99k LAN-rate hook) becomes a documented engine setting, or is
   recorded as unnecessary with the reason.

## Compatibility guards

These hold at every milestone, not only at the end.

1. **Off means unchanged.** With no extension loaded and every new cvar at
   its default, the server behaves exactly as it does today. Every existing
   gate passes unchanged:
   - the lockstep differential and self-check with zero divergence
   - the retail-module provider tests
   - the protocol, demo and no-Steam fallback probes
   - the client paths
2. **Retail clients see nothing new.** Anything a feature or extension sends
   to a client uses retail's own server-command and configstring grammar,
   which the retail cgame already parses (`print`, `chat`, `cp`, `cs`,
   sounds by configstring). Anything beyond that goes only to peers that
   negotiated it, through FnQL-game's per-client channels (FnQL-game
   milestone M10). No feature requires a client change, and none adds a menu
   (`AGENTS.md`).
3. **FnQL-game keeps its retail profile.** New game-side features are
   default-off. With them off, `FNQL_MODULE_CAP_RETAIL_WIRE` still holds and
   G3 stays at zero divergence. A new game-side interface rides the
   compatibility slab and `FnQL_GetModuleInfo` capability bits. The same
   FnQL-game binary stays a drop-in for the retail engine, and the retail
   qagame needs nothing it lacks.
4. **Retail qagame is never modified.** Its tier is what the engine boundary,
   the shared structures (`playerState_t`, `entityState_t`) and qagame's own
   console commands provide. Writes into its private memory are out of
   scope.
5. **Identity stays honest.** Extensions see the identities of
   [`GAME_MODULES.md`](GAME_MODULES.md):
   - a ticketed SteamID when Steam authorized it
   - otherwise a per-connection anonymous id, marked as such

   A ban or privilege tied to an anonymous id lasts for that connection, and
   IP rules cover Steam-less servers. No extension can mint an
   authentication result.
6. **Steam and the WebUI stay on, and services stay explicit.** Gates run
   with `com_steamIntegration` at its default. Optional external pieces
   (libzmq, Python, Redis, metrics) are default-off and report "unavailable"
   deterministically. Extensions load only from an administrator-configured
   directory, never from a pk3, a Workshop item, a download or any path a
   client can influence.
7. **Platforms stay inside constraint 9.** Win32, Linux i686, Windows x64 and
   Linux x86_64. Each target is validated with its own artifacts. x86_64
   Windows has no retail qagame, so its full tier is FnQL-game.

## What operators use today

Observed means read from the project's own repository or a retail binary.
Reported means taken from third-party hosting guides and not yet checked
against a QLDS install. D0 turns each reported row into an observed one or
corrects it.

### QLDS (`qzeroded`)

| Capability | Status of the claim |
| --- | --- |
| Free Steam app 349090, installed anonymously with SteamCMD | Reported (hosting guides) |
| Linux `qzeroded.x86` and `qzeroded.x64`, started by `run_server_x86.sh` / `run_server_x64.sh` | Reported (minqlx README, Pterodactyl egg) |
| A Windows `qzeroded` build | Unknown |
| Steam GameServer: anonymous logon, ticket auth, VAC, Steam listing, rules and player list | Observed in `quakelive_steam.exe`, which carries the same server code; not yet in `qzeroded` |
| `server.cfg`, `access.txt`, `mappool.txt`, `workshop.txt` and factory files in `baseq3/` | Reported (QLSM's editor list). `access.txt` belongs to qagame (`g_accessFile`; FnQL-game `g_access.c`). Who owns `workshop.txt` is unknown |
| Unattended Workshop downloads for the items in `workshop.txt` | Reported |
| ZMQ stats (`zmq_stats_*`) and ZMQ RCON (`zmq_rcon_*`) | Observed in retail; FnQL implements both |
| `serverstartup`, `sv_tags`, `sv_idleExit`, `sv_quitOnEmpty`, `sv_master`, `sv_setSteamAccount` | Observed; FnQL owns all six ([`SERVER_CVAR_COMPATIBILITY.md`](SERVER_CVAR_COMPATIBILITY.md)) |

### minqlx

Observed from its public repository on 2026-11-16:

- **Platforms.** Linux only, x86 and x64, loaded into `qzeroded` by its own
  launch script, embedding Python 3. Its README describes core development
  as "pretty much dead".
- **Configuration.** `qlx_owner`, `qlx_plugins`, `qlx_pluginsPath`,
  `qlx_database` (Redis), `qlx_commandPrefix` (`!`), the four
  `qlx_redis*` cvars, `qlx_logs` and `qlx_logsSize`.
- **Events, 32 of them.** `console_print`, `command`, `client_command`,
  `server_command`, `frame`, `set_configstring`, `chat`, `unload`,
  `player_connect`, `player_loaded`, `player_disconnect`, `player_spawn`,
  `stats`, `vote_called`, `vote_started`, `vote_ended`, `vote`,
  `game_countdown`, `game_start`, `game_end`, `round_countdown`,
  `round_start`, `round_end`, `team_switch`, `team_switch_attempt`, `map`,
  `new_game`, `kill`, `death`, `userinfo`, `kamikaze_use` and
  `kamikaze_explode`.
  - Handlers can stop a dispatch.
  - Handlers can rewrite printed text, commands, configstrings and userinfo.
  - Handlers can reject a connection with a message.
  - The stats-shaped events need `zmq_stats_enable`.
- **Native surface, about 45 functions.** Player info and state; userinfo;
  server, client and console commands; cvars; configstrings; setting a
  player's position, velocity, health, armor, weapons, ammo, powerups,
  holdable, flight, invulnerability, score and privileges; noclip; spawning
  a player; killing with a chosen means of death; votes; spawning, replacing
  and removing items; weapon respawn overrides; kamikaze timers.
- **Python API.** `Plugin`, `Player` and `Game` classes, permission levels 0
  to 5, a Redis `db` handle, and constants.
- **Default plugin set.** `plugin_manager`, `essentials`, `motd`,
  `permission`, `ban`, `silence`, `clan`, `names`, `log` and `workshop`.
  Community sets add team balance (from QLStats ratings), queues and many
  more.

shinqlx is a Rust reimplementation of minqlx with the same plugin API.

### Tools built around them

| Tool | What it depends on |
| --- | --- |
| QLStats (PredatH0r's XonStat fork; its node.js feeder) | The ZMQ stats JSON stream, byte for byte |
| QLSM (web dashboard, Ansible over SSH) | QLDS layout, minqlx plugin management, `LD_PRELOAD` hooks, ZMQ RCON, the four `baseq3` text files |
| LinuxGSM `qlserver`, the Pterodactyl egg, Docker images | SteamCMD app 349090, the `qzeroded` names and flags, server queries for monitoring |
| ZMQ RCON and stats clients (tjone270's `qlds-scripts` and others) | ZMQ RCON framing and PLAIN auth, stats JSON |
| `LD_PRELOAD` hooks such as the 99k LAN-rate hook | `qzeroded` internals |
| Steam and A2S server browsers and monitors | Steam GameServer publication and A2S replies |

## Where FnQL stands (2026-11-16)

FnQL already has:

- Retail-module hosting on Linux i686 and Win32, and FnQL-game on every
  enabled target.
- Protocol 91 in both directions.
- The engine-owned retail `sv_*` surface.
- Factories and map pools with `reload_mappool`.
- ZMQ RCON and stats with strict framing, plus `sv_statsJournal`, which
  keeps the stats stream without libzmq.
- Steam GameServer publication on Win32.
- Server demos and lockstep recording.
- UDP `rcon`, `getstatus`, downloads and legacy masters, which retail lacks.
- FnQL-game's retail-faithful admin and access commands
  (FnQL-game `docs/SERVER_COMMANDS.md`).

The gaps that decide this plan:

1. **No Steam on the Linux server.** The provider has a validated Win32 lane
   only ([`STEAM_PROVIDER.md`](STEAM_PROVIDER.md)). A Linux FnQL server has
   no GameServer logon, tickets, VAC, Steam listing or unattended Workshop.
2. **No retail match stats on Linux.** The reader for retail qagame's match
   documents (a VC10 JsonCpp value) exists only in the Win32 provider. With
   retail `qagamei386.so` or `qagamex64.so`, ZMQ stats, QLStats and the
   journal get no match events. FnQL-game is unaffected, because it passes
   JSON text.
3. **No extension host,** so nothing replaces minqlx.
4. **No `qzeroded` evidence.** Every retail observation so far comes from
   `quakelive_steam.exe`.
5. **Operator gaps from the parity audit**
   ([`RETAIL_PARITY_GAPS.md`](RETAIL_PARITY_GAPS.md)): Steam heartbeats
   ignore `sv_master`, and Steam auth failures don't give retail's reason.
6. **Not drop-in.** The SteamCMD install, the launch names and the hosting
   tools do not find FnQL.

## Capability tiers

What an extension can do depends on the game module, and the server says so
through capability bits.

| minqlx surface | With retail qagame | With FnQL-game |
| --- | --- | --- |
| Connect (with rejection), loaded, disconnect, client and server commands, chat, userinfo, configstrings, console print, frame, RCON, map, new game | Engine | Engine |
| Messages, center prints, sounds, kick, cvars, configstrings, commands as a player | Engine | Engine |
| Match controls: `put`, `lock`, `mute`, `timeout`, `allready`, `abort`, `addscore`, `forceshuffle`, `opsay`, `addadmin` and the rest, and `teamsize` (a cvar) | qagame's own console commands (FnQL-game `docs/SERVER_COMMANDS.md` lists the retail set) | The same commands, plus typed calls |
| Game and round start and end, kill, death, team switch, medals | From the match stats imports: Win32 now, Linux after D3 | Typed events from the module |
| Spawn, votes, team-switch attempts, countdowns, kamikaze | Where the wire shows them (vote configstrings and commands); otherwise unsupported | Typed events |
| Player position, velocity, weapons, ammo, powerups, health, armor, holdable, flight | Read-only, from `playerState_t` | Read and write |
| Spawn player, kill with a means of death, item spawn, replace and remove, respawn overrides, invulnerability, privileges, server-called and forced votes | Unsupported | Game-side calls |

## Milestones

Each milestone ships on its own, behind defaults that change nothing, and
lists what it depends on. Engine work starts without waiting for QLDS
evidence. Only the wire-parity items wait for it.

| Milestone | Depends on | Ships |
| --- | --- | --- |
| D0 QLDS evidence | none; read-only, runs alongside D1 and D2 | `QLDS_EVIDENCE.md`, transcript schemas |
| D1 Extension host | none | Native extension ABI and SDK, default-off |
| D2 First-party operator extensions | D1 | Bans, permissions, silence, MOTD, names, logs, scheduler, store |
| D3 Steam and stats on Linux | D0 (Steam runtime), decision 2 | Linux provider, retail stats reader, heartbeats, auth reasons |
| D4 QLDS drop-in | D0, D3 | QLDS basepath, `workshop.txt`, ZMQ wire parity, launch compatibility, tool interop |
| D5 FnQL-game full tier | D1 | Game-side extension interface and typed game events (both repositories) |
| D6 minqlx bridge | D1, D2, D5, D0 census | Python runtime with the minqlx-compatible API |
| D7 Beyond parity | as listed | Operations, security, packaging, performance |
| D8 Migration | D4, D6 | Guide, config validator, recommended defaults |

The demo and relay milestones V0 to V5 have their own table in the V track
below. V1 and V2 can start now, beside D1.

### D0. QLDS evidence

Owner: FnQL. Read-only; nothing from QLDS is committed except hashes and
schemas.

- Install app 349090 with SteamCMD (anonymous), on Linux and, if it exists,
  Windows. Inventory each `qzeroded`:
  - command and cvar registrations
  - the files it opens
  - Steam GameServer calls
  - ZMQ setup
  - bundled libraries (Steam runtime, libzmq)

  Diff the inventory against `quakelive_steam.exe` and FnQL, in the format of
  [`RETAIL_PARITY_GAPS.md`](RETAIL_PARITY_GAPS.md).
- Decide who owns `workshop.txt`, and its semantics.
- Capture transcripts from a running `qzeroded`:
  - ZMQ stats for every event type, across FFA, duel, TDM, CA, CTF, FT and
    race, with bots
  - ZMQ RCON exchanges
  - A2S `INFO`/`RULES`/`PLAYER` replies
  - Steam rules and tags
- Record the JsonCpp layout in the Linux retail qagames (`qagamei386.so`,
  `qagamex64.so`) for D3.
- Census:
  - which minqlx events, native functions and `db` calls the default plugin
    set and the most-used community plugins use, ranked
  - the exact interface each hosting tool needs
  - what each popular `LD_PRELOAD` hook changes

Gate: `docs/fnql/QLDS_EVIDENCE.md` with hashes, the inventory diff and the
census; fixtures hold FnQL-generated or synthetic data only.

### D1. Server extension host

Owner: FnQL.

- **ABI.** A versioned, size-tagged C ABI with capability negotiation, in the
  style of the Steam provider's.
  - **Engine-side events,** all from boundaries the engine already owns:
    - vmMain calls (connect, begin, disconnect, client command, frame,
      init and shutdown)
    - syscalls (configstring, server command, print, the stats imports)
    - console, UDP RCON and ZMQ RCON input
  - Handlers can stop or rewrite exactly the things minqlx lets them.
  - **Engine-side actions:** messages, sounds by configstring, kick,
    cvars, configstrings, console commands and commands as a player.
  - **State reads** from the shared `playerState_t`/`entityState_t`.
- **Host services.** Extensions use these instead of rolling their own:
  - A scheduler (delays, next frame).
  - An async job queue for blocking work (HTTP, databases, files) whose
    results come back on the frame thread.
  - A persistent key-value store. SQLite is the default; it is an inference
    to verify in D0 that the census needs nothing more.
  - Rotated, structured logging.
  - Identity lookups that follow [`GAME_MODULES.md`](GAME_MODULES.md).
- **Safety.**
  - Callbacks run on the server frame thread within an enforced budget.
  - An extension that faults or overruns is disabled with a diagnostic, and
    the server and its clients carry on. In-process native code cannot be
    sandboxed, and the documentation says so.
  - Loading is restricted to the administrator-configured directory.
  - Extensions load, unload and reload at runtime.
- **Lockstep support.** The harness can load extensions, so their events
  and effects are recorded and diffed like any other server output.

Gate:

- ABI fixtures for success and every rejection class
- an event-order transcript per module
- a recorder extension in lockstep on retail qagame and FnQL-game, with zero
  divergence apart from its own actions
- budget and fault-injection tests
- no measurable frame-time change with nothing loaded

### D2. First-party operator extensions

Owner: FnQL. Each is a separate extension on the D1 ABI, off until enabled,
and works with both modules.

- **Bans.**
  - Duration, reason and an audit trail.
  - SteamID and address rules, refused at `connect` before qagame sees the
    client.
  - Coexists with qagame's `access.txt` bans instead of replacing them.
- **Permissions.** minqlx's levels 0 to 5, an owner, and per-command
  overrides. They map onto qagame's admin and mod privileges through its own
  `addadmin`/`addmod`/`demote` commands.
- **Moderation.**
  - Persistent silence, re-applied with qagame's `mute` on each join.
  - Name and clan rules enforced on userinfo.
  - MOTD and join messages.
- **Workshop list management** (with D4).
- **Logging.** Chat, command and connection logs with rotation.
- **Scheduled commands** and command aliases.
- **Chat commands** with a configurable prefix (`!` by default), shared by
  all extensions.
- **Engine-level filters,** after Q2PRO's server commands, which work the
  same way with retail qagame:
  - Client-command filters that ignore, log, print or kick (Q2PRO's
    `addfiltercmd`).
  - Userinfo-key bans (`adduserinfoban`).
  - Blackholes that drop an address silently (`addblackhole`).
  - A limited RCON password that may run only listed commands
    (`addlrconcmd`).

  Q2PRO's cvar bans query client cvars, which retail clients cannot answer,
  so they are left out.

Gate: each extension has lockstep scenarios on both modules. With it
disabled, the scenarios show zero divergence from today.

### D3. Steam and stats on Linux

Owner: the Steam provider sibling and FnQL. Needs decision 2.

- **The provider for Linux i686 and x86_64,** loaded by path against the
  Steam runtime that the QLDS or Steam install ships, and validated by
  symbol set. It covers:
  - anonymous and `sv_setSteamAccount` logon
  - tickets and secure/VAC mode
  - Steam listing, rules and player list
  - P2P and voice
  - GameServer UGC
- **`EnableHeartbeats`.** A provider capability, so `sv_master 0` keeps a
  server off Steam's list, as retail does.
- **Steam auth failure reasons.** Retail's reason text, carried in a provider
  field. FnQL's 10-second unanswered-auth timeout stays, because it is more
  robust than retail's open wait.
- **Retail match documents on Linux.** A read-only reader for the GCC
  JsonCpp layout recorded in D0, with the same validation and fail-closed
  rules as the Win32 one, so ZMQ stats, the journal and Steam stats work
  with the Linux retail qagames. Where it lives (provider or engine) is
  decision 7.

Gate:

- a retail client finds and joins an FnQL server on Linux i686 and Linux
  x86_64 with a ticket
- the server is listed with correct rules and tags
- `sv_master 0` hides it
- a retail-qagame match on Linux produces the same stats documents as on
  Win32
- the no-Steam fallback passes unchanged

### D4. QLDS drop-in

Owner: FnQL, with FnQL-game where D0 puts a file in qagame.

- **QLDS install as basepath (decision 1).** Discover SteamCMD installs of
  app 349090 as FnQL discovers 282440, validate the asset set, and mount
  without copying.
- **`workshop.txt`.**
  - Retail's semantics, from D0.
  - Unattended download through GameServer UGC before the first map: bounded,
    resumable and restart-safe, feeding the existing transactional Workshop
    mount.
  - A failed item is reported and skipped; the server does not hang on it.
- **ZMQ wire parity.**
  - Stats JSON and RCON match the D0 schemas, with golden tests.
  - libzmq through a Meson wrap so packages can ship it (decision 6). It
    stays default-off, and an administrator-supplied library still
    overrides it.
- **Launch compatibility.**
  - `run_server_x86.sh`/`run_server_x64.sh` equivalents.
  - Every QLDS launch flag from D0 accepted.
  - A cvar mapping table.
  - Tools that look for `qzeroded` get a documented launcher; FnQL's own
    binaries are not renamed.
- **The 99k LAN-rate hook and other hooks** from the D0 census become
  engine settings, or are recorded as unnecessary.

Gate: QLStats' feeder ingests an FnQL match. QLSM, LinuxGSM and the
Pterodactyl egg run FnQL in place of `qzeroded` with only the executable
changed. Every RCON and stats client from D0 works.

### D5. FnQL-game full tier

Owner: FnQL-game (game logic) and FnQL (ABI plumbing). Lands in both
repositories, and is checked by FnQL-game's `check_engine_abi.py`.

- **A game-side extension interface,** found through the compatibility slab
  and announced by a new `FnQL_GetModuleInfo` capability bit:
  - typed events for spawn, kill and death (with means of death), round and
    countdowns, votes, team-switch attempts, kamikaze and medals
  - calls for:
    - inventory, health and armor
    - position and velocity
    - noclip, flight and invulnerability
    - spawning a player
    - killing with a means of death
    - item spawn, replace and remove
    - respawn overrides
    - privileges
    - server-called and forced votes

  The engine relays the interface to extensions behind the same capability
  bits.
- **Game-native helpers** that community plugins rebuild badly:
  - team balance from supplied ratings
  - spectator queues and rotation

  Both are default-off, and team moves use retail's wire grammar.
- FnQL-game's `docs/PLAN.md` milestone M13 holds the game side of D5 and of
  the V track, after M10's capability negotiation.

Gate: G3 at zero divergence with everything off. Scenarios with the features
on, against retail behavior where retail has an equivalent command,
otherwise against a self-check. G0 passes, and the module is still a drop-in
for the retail engine.

### D6. minqlx bridge

Owner: FnQL.

- **Runtime.** An optional, dynamically resolved CPython 3 runtime behind a
  typed adapter, following the libzmq pattern (decision 3). Win32, Linux
  i686, Windows x64 and Linux x86_64.
- **A clean-room `minqlx`-compatible package on the D1 ABI:**
  - all 32 events with their stop and rewrite rules
  - `Plugin`, `Player` and `Game`, the constants, the command prefix and
    permissions
  - delays, `next_frame` and threads on D1's job queue
  - the `qlx_*` cvars

  Functions a tier does not support raise minqlx's own error types with a
  clear message instead of misbehaving.
- **Database.** Real Redis through `redis-py`, or the subset of the Redis API
  that the D0 census found in use, backed by D1's store, so no Redis daemon
  is needed.
- **Plugins.** `plugin_manager` behavior (load, unload, reload), and a
  `requirements.txt` check that reports and never installs.
- Licensing position confirmed first (decision 4).

Gate:

- the default minqlx plugin set and the D0 community corpus run unmodified
  on every enabled target, with FnQL-game (full API) and retail qagame (its
  tier)
- each plugin is exercised by scripted lockstep scenarios
- a hostile plugin cannot crash the server

### D7. Beyond parity

Owner: FnQL. Independent, default-off slices, each with its own gate.

- **A2S without Steam.** A native A2S responder when no Steam GameServer is
  running. It yields to the provider when one is.
- **Operations.**
  - A health and metrics endpoint (players, frame time, loss, Steam and
    Workshop state).
  - Live reload of `access.txt`, pools, factories and extensions.
  - A watchdog-friendly exit-code contract built on `sv_errorExit`.
- **Security.**
  - ZMQ CURVE, so remote RCON needs no tunnel.
  - RCON audit logs with secrets scrubbed.
  - Per-address RCON limits.
- **Disputes.** Demos (`sv_autoRecordDemos`) and lockstep recordings
  exposed to extensions and RCON.
- **Packaging:**
  - an official Docker image
  - systemd units
  - a Windows service wrapper
  - upstreamable LinuxGSM and Pterodactyl definitions
  - a one-command SteamCMD bootstrap
- **Performance.** A published comparison against `qzeroded` on one
  scenario (bots, `sv_fps 40`, full server). FnQL may not be slower.

### D8. Migration

Owner: FnQL docs.

- A "Moving from QLDS and minqlx" guide: layout, flags, cvar mapping,
  plugins, Redis (keep it or migrate), and what behaves differently.
- A configuration validator for `server.cfg`, the four `baseq3` files,
  factories and extension dependencies. It reports and never edits.
- Recommended operator profiles. They are documentation, not changed
  defaults.

## V track: multi-view demos, live relay and advanced playback

### What exists

- **Retail.** Retail Quake Live records and plays single-POV `.dm_91`
  demos. Playback control is `timescale` and `cl_freezeDemo`, driven by the
  retail cgame's demo HUD: Play/Pause [SPACE], Advance [RIGHT] while frozen,
  speed - [LEFT] and + [RIGHT], 1.0x [DOWN], Hide [DEL]. FnQL-game's
  `cg_demohud.c` records the retail evidence for that HUD. There is no
  seeking and no rewind.
- **FnQL** adds per-client server demos (`sv_autoRecordDemos`), legacy
  playback (`.dm_66` to `.dm_68` and the other `demo_protocols`, plus
  `.dm3`), timedemo and FFmpeg capture.
- **q2repro / Q2PRO**, observed in the local q2repro checkout's
  `doc/server.asciidoc` and `doc/client.asciidoc`:
  - Client demos:
    - `seek` with absolute, relative, percent and timespec targets
    - in-memory snapshots every `cl_demosnaps` seconds for backward seeks
    - a demo bar (`scr_demobar`)
    - `cl_demowait`, gzip demos, and recording while playing
  - MVDs, recorded by the game server:
    - `sv_mvd_enable`, `mvdrecord`
    - limits by time, size and map count
    - bandwidth filters (`sv_mvd_nogun`, `sv_mvd_noblend`)
    - a dummy observer
  - A GTV relay mode with no game module. It takes live MVD streams and
    local MVD files and serves them to ordinary clients as spectators, with
    a delay buffer (`mvd_wait_delay`), channels, relay chaining, host lists
    and passwords, and `mvdseek`.
- **CPMA's MVDs** show the Quake III approach: the game module records every
  player's state, and its cgame switches POV.

Q2PRO and CPMA are design precedents only. Nothing is copied, and Quake
Live's split between engine and modules is different.

### Guards for this track

On top of the compatibility guards above:

1. **Existing demos are untouched.** `.dm_91` recording, the per-client server
   demos, every legacy playback path, timedemo, and the retail demo HUD and
   its keys behave exactly as now unless a new command is used. The legacy
   ordering and termination rules stay.
2. **Unmodified retail cgame.** Everything played back (seeks, a POV taken
   from an MVD, relay streams) reaches the unmodified retail cgame as an
   ordinary protocol-91 stream it already accepts. New controls are commands
   and cvars that players bind. Neither a menu nor a change to retail key
   handling is added, and any engine overlay stays clear of the retail demo
   HUD.
3. **Demos are untrusted input,** as in roadmap section 3. Parsers are bounded
   and fuzzed, and playback never loads a native module from demo content.
4. **No cost when off.** With MVD recording off, the server's per-frame cost
   is unchanged. With it on, writing happens off the frame thread inside a
   measured budget.
5. **Playback can't touch online play.** Playback features run only in demo
   playback or relay mode, never on a live connection.

### Milestones

| Milestone | Depends on | Ships |
| --- | --- | --- |
| V0 Contract and format | none | Retail cgame playback contract, MVD format spec |
| V1 Advanced playback for existing demos | V0 | Seek, rewind, stepping, speed, event index, cutting |
| V2 MVD recording | V0 | Server records the whole match, retail qagame or FnQL-game |
| V3 MVD playback | V1, V2 | POV switching, free camera, POV extraction to `.dm_91` |
| V4 Live relay | V2, V3; D3 for Steam-listed Linux relays | QL TV: delayed spectating of live matches for retail clients |
| V5 Beyond | V3, V4; D5 for typed events | Director, highlights, and an FnQL-game cgame multiview |

V1 and V2 can start now, in parallel with D1.

#### V0. Playback contract and MVD format

Owner: FnQL, with FnQL-game supplying retail-module evidence.

- **Retail cgame contract.** Record what the retail cgame needs from a demo
  stream, by retail probes:
  - how it handles time going backwards
  - whether a cgame restart is enough to seek, with the same path a
    non-fast `vid_restart` takes
  - snapshot gaps, and `cl_freezeDemo`/`timescale` with its HUD
  - which spectator and follow `playerState_t` fields the relay must
    produce (`PMF_FOLLOW`, team, scores)
- **The MVD format specification**, written down before any code:
  - versioned, chunked and self-describing
  - compressed with the existing zlib wrap
  - an index for seeking
- **The MVD format's contents:**
  - the gamestate and every configstring change
  - every entity's state, delta-coded with the protocol-91 field tables
  - every active client's `playerState_t`
  - every reliable server command with its recipients
  - for each client, the set of entities it was actually sent
  - the stats documents, and markers extensions add
  - the file name and extension (decision 8)

Gate: `docs/fnql/MVD_FORMAT.md` and a retail-cgame contract note, each with
its probes recorded.

#### V1. Advanced playback for existing demos

Owner: FnQL (client).

- **Seeking.** `seek` to an absolute or relative time, a percentage or a
  frame:
  - Forward seeks run fast without rendering or sound.
  - Backward seeks use keyframes taken while playing: the parse position,
    the gamestate and configstrings, the server-command ring and recent
    snapshots. The engine restores the keyframe, restarts cgame and
    fast-forwards.
- **Control.**
  - Pause and frame stepping, forward and back.
  - Speed control that cooperates with `timescale` and the retail HUD.
  - An option to pause at the last frame.
- **An event index** built while the demo loads, from what the stream
  carries: obituary and medal events (numbering from FnQL-game's G2 wire
  manifest), flag and round changes, chat. It powers next and previous frag,
  round or flag, and named bookmarks.
- **Tools.**
  - `demoinfo` (map, players, duration, gametype).
  - Cutting a range into a new `.dm_91` that retail can play.
  - Capture of a range through the existing FFmpeg path.
  - An optional engine-drawn timeline bar, default off.
- Works for `.dm_91` and every legacy format FnQL already plays.

Gate:

- A seek lands on the same state as linear playback: snapshot contents and
  a headless screenshot hash at the target frame.
- The retail cgame plays through repeated seeks with no error.
- The malformed corpus is rejected cleanly.
- Legacy playback and timedemo results are unchanged with no new command
  used.

#### V2. MVD recording

Owner: FnQL (server). Works with retail qagame and FnQL-game, because
everything it records crosses the engine.

- **Controls.** `mvdrecord` and `mvdstop`; automatic recording per map or per
  match (like `sv_autoRecordDemos` 3); limits by time, size and map count;
  control from RCON and the extension ABI.
- **Filters.** Optional bandwidth and size filters in the manner of
  `sv_mvd_nogun`.
- **The writer.** Writing is asynchronous and bounded. A write failure stops
  the recording, never the server.

Gate:

- With recording off, lockstep shows zero divergence and frame time is
  unchanged.
- With it on, the measured frame cost stays inside its budget.
- The format round-trips.
- The MVD reproduces each player's POV: a POV taken from it matches, in
  snapshot content, the per-client server demo recorded in the same match.

#### V3. MVD playback

Owner: FnQL (client).

- **Loading.** `demo` opens an MVD as it opens any demo. The engine builds an
  ordinary protocol-91 stream for the chosen viewpoint.
- **Viewpoints.**
  - **A player's POV** uses that player's recorded entity set and commands,
    so it is exactly what they received.
  - **Free camera** chooses entities by the PVS from the camera's position
    (an approximation, documented as one).
  - **Switching** goes to the next or previous player, the killer, or the
    flag carrier.
  - The scoreboard comes from the recorded score replies.
- **V1 controls** all apply to MVDs.
- **Extraction.** `mvdextract` writes any player's POV as a `.dm_91` that
  retail can play.

Gate: the retail cgame plays each POV, free camera and switches with no
error. An extracted POV matches that player's server demo.

#### V4. Live relay (QL TV)

Owner: FnQL (server).

- **The source.** A game server streams its MVD to a relay over an
  authenticated, bounded channel, gated like ZMQ RCON (host allow and deny
  lists, a password).
- **Relay mode** is a dedicated server with no game module. Ordinary retail
  clients join it as spectators:
  - each picks a player POV or a free camera
  - a delay buffer prevents ghosting in tournaments
  - several matches appear as channels
  - relays can chain
  - local MVD files can be served for watch parties
  - spectator commands (`follow`, `team s`, `score`) are answered by the
    engine
- With D3, a Linux relay is listed on Steam like any server.

Gate:

- A retail client joins a relay, follows each player, and uses the
  scoreboard.
- The delay is honored.
- A chained relay works.
- When the source drops, spectators get a clean intermission, not an
  error.
- Every existing server gate still passes.

#### V5. Beyond

Each item is optional and default-off.

- **An auto-director** follows the action from the event index and D5's
  typed events.
- **Highlight reels** are cut from the index.
- **Stats can be derived again from an MVD,** so QLStats can be fed after
  the fact.
- **An FnQL-game cgame multiview** shows picture-in-picture POVs. It runs in
  local playback or on relays that negotiated it, never toward retail
  peers. It lands in FnQL-game.

## Decisions needed

1. **QLDS installs as basepath.** `AGENTS.md` names the retail client
   install. A free SteamCMD install of app 349090 is the normal hosting path
   and carries retail content legitimately. Should it be accepted?
2. **Steam on Linux and x86_64.** Constraint 9 says x86_64 has no Steam
   provider, which is true on Windows. Linux x86_64 QLDS appears to ship a
   64-bit Steam runtime and hosts `qagamex64.so` (inference, until D0
   confirms it). Should constraint 9 be amended for Linux?
3. **Python runtime.** Administrator-supplied libpython only (recommended,
   matching libzmq), or a bundled one through a Meson wrap.
4. **minqlx API compatibility.** The bridge is a clean-room rewrite of a
   GPLv3 project's public API. minqlx plugins (GPLv3) stay separate,
   administrator-installed programs. Confirm the licensing position before
   D6.
5. **Retail qagame state writes.** Recommended: none (guard 4). Full control
   comes with FnQL-game, which is what FnQL-game exists for. The
   alternative, an offset adapter pinned to one retail build, is the minqlx
   approach this plan sets out to retire.
6. **libzmq in packages.** Ship it through a Meson wrap, still default-off.
7. **Where the Linux retail-JSON reader lives.** In the closed provider
   beside the Win32 reader, or in the open engine, so stats work with retail
   qagame even without Steam.
8. **MVD files.** Their extension, and whether they appear in the retail
   WebUI demo list. Retail cgame can't play them directly; FnQL's `demo`
   command can. The safe default is to list them only for FnQL clients that
   can open them.
9. **Relay transport.** A TCP stream like Q2PRO's GTV, or a reliable channel
   over the existing UDP netchan.

## Exit criteria

Each criterion is backed by a recorded run.

1. On Linux i686 and Linux x86_64, an FnQL server on a QLDS install:
   - is listed in the Steam browser
   - takes retail clients with tickets, under VAC/secure mode when enabled
   - downloads `workshop.txt` items unattended
   - publishes the same stats as `qzeroded`
   - runs under QLStats, QLSM, LinuxGSM, Pterodactyl and the ZMQ clients
     with only the executable changed
2. The default minqlx plugin set and the D0 community corpus run unmodified
   on Windows and Linux, x86 and x86_64, with or without a Redis daemon.
   With FnQL-game they get the full API; with retail qagame, the documented
   tier.
3. A server needing only the default plugin set's features runs with no
   scripting at all, on D2's extensions.
4. No D0 inventory item is missing without a recorded, evidence-backed
   deliberate difference.
5. Every compatibility guard holds, and every existing FnQL and FnQL-game
   gate passes.
6. The D7 performance comparison shows FnQL no slower than `qzeroded`.
7. Every demo FnQL plays can be seeked backward and forward, stepped, cut
   and indexed through the retail cgame. FnQL servers record MVDs with
   either game module. A POV taken from an MVD matches that player's own
   demo. Retail clients can spectate a delayed live match through a relay.
