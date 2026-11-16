# Game Modules and the FnQL-game Companion

FnQL stays engine-only. Clean-room game modules live in the companion
repository **FnQL-game** (`https://github.com/themuffinator/FnQL-game`,
checked out next to this one as `../FnQL-game`). This document is the engine
side of the contract between the two: how FnQL picks a module, how it tells
modules apart, and the rules that keep FnQL clients and servers able to play
with retail Quake Live in both directions. FnQL-game's
`docs/COMPATIBILITY.md` is the game side of the same contract, and its
`docs/PLAN.md` holds the milestone plan.

## Module providers

A *provider* is whoever supplies a native module for a VM slot.

| Provider | Files (Win32) | Selected by |
|---|---|---|
| retail | `qagamex86.dll`, `cgamex86.dll`, `uix86.dll` from the filesystem (retail `bin.pk3` or loose `baseq3`) | `vm_gameProvider`, `vm_cgameProvider`, `vm_uiProvider` set to `retail`, and the fallback of every other value |
| fnql | `fnql_qagamex86.dll`, `fnql_cgamex86.dll`, `fnql_uix86.dll` (Linux: `fnql_*i386.so`; x86_64: `fnql_*x86_64.dll` / `.so`) | the same cvars set to `fnql` or `auto` (the default since FnQL-game milestone 12) |

FnQL-game modules carry the `fnql_` prefix (`FNQL_GAME_MODULE_PREFIX`) so a
search path holding both sets can never shadow a retail module by accident.

Each slot has its own provider cvar, archived and protected:
`vm_gameProvider` applies on the next map load; `vm_cgameProvider` and
`vm_uiProvider` (client builds only) apply when that module next loads. The
defaults (`VM_GAME_PROVIDER_DEFAULT`, `VM_CLIENT_PROVIDER_DEFAULT`) are:

| Target | `vm_gameProvider` | `vm_cgameProvider`, `vm_uiProvider` |
|---|---|---|
| Win32 x86 | `auto` | `auto` |
| every other target | `auto` | `fnql` (no retail client modules exist there) |

They were `retail` on x86 until FnQL-game milestone 12 flipped them. The
flip rests on that milestone's gates: G2 (wire manifest), the G3 lockstep
differentials, G4 and G4v (prediction and cgame visual parity) and the G5
interop matrix, which together back the modules' `FNQL_MODULE_CAP_RETAIL_WIRE`
claim (FnQL-game `docs/PLAN.md`). Consequences:

- An install without the FnQL-game modules, or with a module that does not
  claim retail wire parity, loads the retail modules exactly as before. The
  `auto` fallback logs one plain line; only an explicit `fnql` warns.
- A pure server that does not list the FnQL-game client package (every
  retail server) still gets the retail cgame and ui (below).
- The cvars are `CVAR_ARCHIVE_ND`: a config records a value only when it
  differs from the default, so later defaults reach players who never chose.
  A config written before the flip holds `seta vm_gameProvider "retail"`,
  because the old flags archived the default too. That value is kept, since
  an explicit choice of retail looks the same; `reset vm_gameProvider` takes
  the new default.

The values are:

- `retail` takes the historical path unchanged: the engine loads the module
  exactly as it did before providers existed.
- `fnql` loads the FnQL-game module first. If it is missing, fails ABI
  validation, or does not identify itself (below), the engine unloads it, logs
  why, and loads the retail module instead.
- `auto` is `fnql` restricted to a module that declares
  `FNQL_MODULE_CAP_RETAIL_WIRE`; one without the claim is unloaded and the
  retail module loads. This is the value a default can safely use (FnQL-game
  milestone 12).
- Unknown values behave like `retail` and log a warning.
- Pinned native reloads of qagame never consult the provider. A cgame or ui
  loaded under a pure server does, under the purity rules below.
- `vm_game`, `vm_cgame` and `vm_ui` still choose native versus QVM first.
  Providers only apply to the native path.

`code/qcommon/module_provider_policy.h` holds the value and acceptance rules,
and `tests/module_provider_policy_tests.c` gates them.

### Client modules under a pure server

A pure server validates the pak a client reports for its cgame, not the
bytes the client runs: retail clients report the pak holding `cgamex86.dll`.
FnQL therefore uses an FnQL-game cgame or ui on a pure server only when the
server says it accepts the package it comes from, and then reports that
package. Observed retail behavior this keeps: the retail client's first `cp`
checksum is the `cgamex86.dll` pak (`CL_SendPureChecksums`), and FnQL
servers drop a mismatch with retail's "Unpure client detected" text.

- **Server.** On a pure server FnQL lists, in the `sv_fnqlClientPaks`
  systeminfo key, the pure checksums of the loaded paks holding
  `fnql_cgamex86.dll` (the FnQL-game client package,
  `fnql_game_client.pk3`). `SV_VerifyPaks_f` accepts such a checksum where
  retail expects the `cgamex86.dll` pak, and logs "<name> is pure with the
  FnQL-game client package". With no such pak, an unpure server or
  `sv_fnqlClientModules 0` (archived, latched, default 1), the key is empty and
  so absent from systeminfo: the gamestate is byte-for-byte what it was
  before, and only the retail checksum is accepted. Retail servers never send
  the key.
- **Client.** The client reads the key from each gamestate's systeminfo
  (never from a cvar a previous server left) and clears it on disconnect.
  Under a pure server a cgame or ui with provider `fnql` or `auto` loads only
  from the first pure-visible pak that holds it, and only when that pak's
  checksum is listed. The bytes are read from that pak itself
  (`FS_ReadFileInPurePak`): not a loose file, not the OS loader. Anything
  else logs why and loads the retail module, from the image pinned before the
  server's pure policy applied. When an FnQL-game module serves the client,
  that retail image is pinned too (read, not loaded), so a pure server that
  does not accept the package still gets the retail module.
- **Report.** `CL_SendPureChecksums` reports the accepted package's checksum
  for an FnQL-game cgame (`VM_NativeModulePureChecksum`) and never the retail
  one. A pure session whose FnQL-game cgame did not come from an accepted pak
  drops with an error instead of reporting anything. `vminfo` adds
  `pure pak <checksum>` for a module loaded that way.
- **References.** Reading `fnql_cgame*` or `fnql_ui*` never marks a pak
  referenced (`FS_GeneralRef`), just like the server module, so no server
  asks a retail client to download or hold the client package.

The UI is reloaded on a pure connect and follows the same rules, but no
server validates it: the `cp` command carries no ui checksum, in retail as
in FnQL.

## Module identity

Every FnQL-game module exports `FnQL_GetModuleInfo` next to its retail-ABI
`dllEntry` (contract: `code/qcommon/fnql_module.h`, mirrored byte-for-byte by
FnQL-game's `code/game/fnql_module.h`). The record is append-only and carries
the product (`FnQL-game`), the module slot name, the version, a build string,
capability bits, and the native API version the module publishes.

- A provider-selected module must present a well-formed record naming its own
  slot and the engine's expected native API, and must use the structured
  ABI. Otherwise it is rejected and the retail fallback runs.
- A module loaded under its retail name is never rejected for lacking a
  record. If it has one, the engine reports its real provenance.
- `vminfo` prints each native module's file and provider, for example
  `qagame : native fnql_qagamex86.dll (FnQL-game 0.1.0, capabilities 0x0)`.
  An unidentified module reads `qagame : native qagamex86.dll (unidentified)`.
- `FNQL_MODULE_CAP_RETAIL_WIRE` is the module's claim that its default profile
  is bit-for-bit retail on the wire. A qagame without it loads with a warning
  that retail clients may see inconsistent game state, and the `auto`
  provider refuses a module without it. `FNQL_MODULE_CAP_EXTENSIONS` says the
  module negotiates FnQL extensions (below). FnQL-game claims both for its
  qagame and cgame and the first for its ui since its milestone 10.
- `FNQL_MODULE_CAP_QL_BOT_ENTITIES` says the module exchanges Quake Live's
  botlib entity structures (below). The engine uses the Quake III sizes for
  an FnQL-game module without it, so older FnQL-game builds keep working.

The retail engine never looks the symbol up, so the same FnQL-game binary is a
drop-in module for retail Quake Live as well. That is a verification tool, not
a product path.

## ABI rules

- FnQL-game uses the retail structured ABI only:
  `dllEntry( void **exports, void *imports, int *apiVersion )`, retail export
  slot order, retail import slots. A module that also exported `vmMain` would
  be loaded as a classic Quake III module and lose every QL-only import, so
  FnQL-game exports `dllEntry` and `FnQL_GetModuleInfo` only.
- Engine extensions are opt-in. A module may touch the qagame compatibility
  slab (`G_QL_IMPORT_COMPAT_BASE` onward) only after `//trap_GetValue` reports
  `COM_TRAP_GETVALUE`. Retail engines have no such cvar, so the same module
  stays retail-safe.
- Structures shared by pointer (`playerState_t`, `entityState_t`,
  `trajectory_t`, `usercmd_t`, `vmCvar_t`, `trace_t`, `entityShared_t`,
  `sharedEntity_t`, `fnqlModuleInfo_t`) are defined here, in the engine.
  FnQL-game mirrors them, and its `scripts/check_engine_abi.py` compiles one
  layout probe against both trees and fails on any size, offset or constant
  difference. Change these structures only with a matching FnQL-game change.
- UI and native cgame cvar writes (imports 7 and 8) go through
  `Cvar_SetFromUI`, not `Cvar_SetSafe`. Retail modules register their player
  settings with `CVAR_PROTECTED` (0x800), the installed menus edit those
  cvars, and retail cgame writes its own (`cg_viewsize` from
  `sizeup`/`sizedown`, `cg_announcer`). Retail UI import 7 and cgame import 7
  are one thunk onto the engine's forcing `Cvar_Set` (`0x4ea2a0` ->
  `0x4cd250`). Such a write may not change a `CVAR_PRIVATE` cvar or a
  protected cvar that is also `CVAR_ROM` or `CVAR_INIT` (engine identity and
  startup state such as `version`, `fs_basegame`, `cl_guid`). Servers, qagame
  and legacy VM cgames keep the `Cvar_SetSafe` rule.
  `code/qcommon/cvar_ui_policy.h` holds the rule and
  `tests/cvar_ui_policy_tests.c` gates it.
- qagame import 192 marks a client as a ragequit: `SV_DropClient` then
  announces `<name> ^1rage^7quits` instead of the drop reason (retail
  `0x4e26e0` -> `0x4df830`, read at `0x4df726`). Retail qagame calls it from
  its `ragequit` client command. FnQL had bound it to the pure-check `cp`
  flag until the 2026-11-11 audit.
- The botlib entity structures have Quake Live's layout for native modules:
  `bot_entitystate_t` is 0xd0 bytes and `aas_entityinfo_t` 0xec, each the
  Quake III structure followed by the same extension (the entity's nextthink
  in whole seconds, its item's quantity, type and tag, its health, a
  client's max health, sixteen full-ammo flags, a training flag and whether
  a client holds the red or blue flag). `AAS_UpdateEntity` copies the
  extension into the entity info, as the retail engine does. The retail
  qagame always uses this layout, an FnQL-game module when it declares
  `FNQL_MODULE_CAP_QL_BOT_ENTITIES`; QVMs, legacy `vmMain` modules and older
  FnQL-game builds exchange the Quake III prefix (0x70 and 0x8c bytes) and
  read the extension as zero. `code/botlib/be_aas_entity.cpp` asserts both
  sizes; FnQL-game asserts its copies.
- qagame import 200 (SteamID query) answers 0 for a bot. Retail recognises a
  bot by the `characterfile` userinfo key (`0x4e2710`); the anonymous SteamID
  the engine gives a bot for the Steam player list is never handed to qagame.

## Match statistics

Retail qagame describes a match to the engine as events and one closing
report, through imports 185 (`MATCH_REPORT`) and 186 (player and round
events). Their payload is a JsonCpp `Json::Value` built by the retail module's
own VC10 runtime, which only that module can produce; the Steam provider's
retail JSON adapter reads it (Win32 only, see `STEAM_PROVIDER.md`).

A source-built module hands over the same documents as JSON text instead,
through two compatibility-slab imports found with `trap_GetValue`:

| `trap_GetValue` key | Import | Signature |
|---|---|---|
| `trap_StatsReportJson_FnQL` | `G_FNQL_STATS_REPORT_JSON` | `void ( const char *json )` |
| `trap_StatsEventJson_FnQL` | `G_FNQL_STATS_EVENT_JSON` | `void ( unsigned steamIdLow, unsigned steamIdHigh, const char *eventName, const char *json )` |

Both answer a number above `COM_TRAP_GETVALUE`; the function is at that number
in the slab. Rules:

- The text is untrusted. It must be terminated, at most 32768 bytes and
  well-formed JSON (`fnql::server::json::DocumentIsValid`), and an event name
  is `[A-Z0-9_]{1,64}`. Anything else is refused whole and logged; nothing is
  published for it.
- An accepted document takes exactly the path a serialized retail one takes:
  publication, the bounded match summary, and the Steam statistics and
  achievements the engine derives from player events
  (`SV_SteamStats_ProcessEvent`).
- Retail's third event argument (the player's stats key) is not passed: the
  engine never read it.
- Slots 185/186 are unchanged and remain the retail module's path. A module
  must not pass text to them.

`sv_statsJournal <name>.jsonl` (default empty, `CVAR_INIT`) appends every
stats publication to that file in the home game directory, one
`{"TYPE":...,"DATA":...}` envelope per line, exactly as the ZMQ publisher
sends it. It needs no libzmq, so a server without the library can keep its
match statistics, and FnQL-game's `stats-events` gate reads it to check a
module's documents against the retail module's. Only a bare `*.jsonl` file
name of letters, digits, `_`, `-` and `.` is accepted.

## Interoperability rules

The engine owns the wire (protocol 91 field tables in `msg.c`). A game module
owns what the fields mean. The rules that make FnQL-game invisible to retail
peers:

1. **Retail profile by default.** FnQL-game's default behavior matches retail
   on the wire: item, weapon, event and means-of-death numbering, configstring
   indices and payloads, server command grammar, and prediction results.
   Retail HLIL is the authority for these values; QLSRP's configstring layout
   is known to diverge.
2. **Negotiated extensions only.** FnQL-game's qagame announces itself through
   a `fnql_game` serverinfo key. FnQL-game's cgame answers it, and only it,
   with a `fnql_cg` userinfo key, which it clears again on shutdown so no
   other server receives it. Anything beyond retail behavior flows only
   between peers that both announced, and only through per-client channels
   (one client's reliable commands); retail peers never receive an unknown
   server command or a reinterpreted field. The protocol is the game's
   (FnQL-game `code/game/bg_fnql_ext.h`); the engine carries it like any
   server command and userinfo key.
3. **Server modules stay server-side.** Reading `FNQL_GAME_SERVER_MODULE_FILE`
   never marks its pak as referenced (`FS_GeneralRef`), just as
   `vm/qagame.qvm` never did. Retail clients are never asked to download or
   hold the package that carries the FnQL-game qagame. The retail
   `qagamex86.dll` keeps its historical reference behavior.
4. **Honest purity for client modules.** An FnQL-game cgame or ui runs on a
   pure server only when that server lists the FnQL-game client package in
   `sv_fnqlClientPaks`; the client then reports that package's checksum, and
   the FnQL server accepts it. On every other pure server, retail servers
   included, FnQL clients use the retail modules. The engine never reports a
   retail checksum for an FnQL-game module ("Client modules under a pure
   server" above). Client packages are optional and never required of retail
   clients.
5. **No synthesized success.** Steam authorization, purity and capability
   answers always reflect what actually ran.

## Query exports

Native export slots 12-18 are predicates the engine asks the game module.
Retail's engine and qagame define their meaning; FnQL's helpers in
`code/server/sv_game.cpp` are the only callers, are native-only, and answer
the neutral value for a missing slot.

| Slot | Export | Retail engine caller | Engine use in FnQL |
|---|---|---|---|
| 12 | `CanClientSeeClient( viewer, target )` | snapshot builder `0x4e54c8` | true includes the target player regardless of PVS |
| 13 | `FreezeCanSeeThawProgressEvent( viewer, ent )` | snapshot builder `0x4e54a0` | true **drops** the entity for that viewer (the name is historical) |
| 14 | `IsObjectiveEntity( ent )` | snapshot builder `0x4e54eb` | true includes a non-client entity regardless of PVS |
| 15 | `ShouldSuppressVoiceToClient( sender, recipient )` | Steam voice relay `0x466a07` | true withholds the voice packet |
| 16 | `IsClientAdmin( client )` | client command `rcon` `0x4dffef` | true lets the client run console commands in-band |
| 17 | `AreEnemyClients( a, b )` | none | lockstep query record only |
| 18 | `GetClientScore( client )` | Steam player list `0x466440` | the published score; also `status` |

The snapshot builder follows retail's order (`0x4e5471`-`0x4e54f0`):
broadcast entities first, then slot 13 for every other entity, then slot 12
for clients or slot 14 for everything else, then the area and PVS pass. The
viewer is the snapshot owner's own slot (`0x4e5789`), not the followed
`ps.clientNum`; an `SVF_SINGLECLIENT` entity is likewise compared with that
slot while the viewer's playerState carries `EF_SPECTATOR` (`0x4000`,
`0x4e53d2`). Legacy modules keep the Quake III rules for both.

The in-band `rcon` runs from `SV_Frame` (with ZMQ's rcon) instead of in the
middle of the client's message as retail does, so a `map` or `kill` cannot
free state the parser still uses. Output still returns to the admin as
out-of-band `print` packets.

## Release packaging

FnQL releases bundle the FnQL-game modules built from the commit pinned in
`version/fnql_game.json`: `baseq3/fnql_qagamex86.dll`, `fnql_cgamex86.dll`
and `fnql_uix86.dll` in the Windows x86 packages, the `i386.so` set in the
Linux i686 package, and the `x86_64.dll` / `x86_64.so` sets in the x86_64
packages. On x86_64 there are no retail Windows modules, so FnQL-game is the
game there (a Linux x86_64 server can also fall back to retail's
`qagamex64.so`). Extracting a package into the Quake Live folder puts them in
`<Quake Live>/baseq3`, which is on the search path, so the default `auto`
providers find them without further setup. Loose modules serve unpure servers
and local play. Since FnQL-game milestone 12 every package also carries the
client package `baseq3/fnql_game_client.pk3`, which FnQL-game builds
deterministically from the same cgame and ui. A pure FnQL server from a
release therefore lists it in `sv_fnqlClientPaks`, and a client from the same
release runs FnQL-game's cgame and ui there. `scripts/fnql_game.py` and
`release.py` check that the package holds exactly that target's cgame and ui,
byte for byte the loose modules, with FnQL-game's fixed entry stamps. The
server package `fnql_game_server.pk3` is not shipped: the loose qagame is
never referenced, so a package adds nothing for it.

The modules contain code under two licenses. FnQL-game's own code is GPLv2 or
later; its baseline from baseq3a is under id Software's Limited Use agreement,
and whether baseq3a's changes may be relicensed under the GPL is still open.
Each package therefore carries `docs/fnql/FNQL_GAME_BINARY_NOTICE.txt`,
FnQL-game's `LICENSING.md`, id's agreement and a `BUILD-INFO.txt` naming the
source commit, all under `docs/fnql-game/` except the notice.
[`TECHNICAL.md`](TECHNICAL.md) describes the CI jobs and package checks.

## Validation

- `tests/fnql_game_release_tests.py`: the pin, staging, and package checks for
  the bundled modules, and the release workflow jobs that build and gate them.
- `tests/game_module_provider_source_tests.py`: provider selection order for
  every slot, identity checks, the pure-server rules for client modules (the
  accepted-pak load, the `cp` report, the server's publication and
  acceptance), the server- and client-module reference exemptions, and the
  `BotPredictVisiblePosition` import arity.
- `tests/module_provider_policy_tests.c`: provider values, `auto`'s
  capability rule and the `sv_fnqlClientPaks` parser.
- FnQL-game's `interop-fnql` suite (`scripts/client_package_probe.py`): an
  FnQL client joins an FnQL dedicated server over UDP, pure and unpure, with
  the client package, loose modules or none, an accepting or refusing server,
  and the FnQL-game or retail qagame. Its opt-in `retail-client` suite adds
  the retail client beside the package, FnQL-game's cgame and ui and qagame
  as drop-ins in the retail engine, and FnQL clients on a retail-engine
  server.
- `tests/game_module_provider_runtime_tests.py` (opt-in): builds
  `tests/fixtures/fnql_game_provider_probe.c` in three variants and drives a
  real x86 `fnql.ded.exe` on a retail map. It checks that:
  - an identified module loads;
  - an unidentified module and a mismatched one both fall back to retail;
  - the default provider keeps the retail module;
  - a pk3-served FnQL-game qagame never appears in `sv_referencedPakNames`;
  - the same pk3 serving a retail-named module still does (the control case).

  Enable it with `FNQL_DEDICATED_RUNTIME_EXE=<fnql.ded.exe>` and
  `FNQL_RETAIL_QL_PATH=<Quake Live install>`.
- FnQL-game ships its own gates (ABI layout parity, dedicated-server boot
  smoke) and the parity gates its plan adds per milestone.
- `tests/qagame_query_export_bridge_tests.py`: the query export order,
  viewer and native-only rules above, the queued admin `rcon`, and the Steam
  player publication.
- FnQL-game's G3 differential records every query export answer each frame
  (`sv_lockstepQueries`, [`SERVER_LOCKSTEP.md`](SERVER_LOCKSTEP.md)) and
  requires FnQL-game's to match the retail qagame's.
- `tests/server_lockstep_source_tests.py`: the dedicated-server lockstep mode
  ([`SERVER_LOCKSTEP.md`](SERVER_LOCKSTEP.md)) stays default-off and every
  engine hook stays gated. FnQL-game's `scripts/server_lockstep.py` (gate G3)
  drives it to diff the retail qagame against FnQL-game's frame by frame.

## Retail evidence recorded here

- The retail engine's `AAS_EntityInfo` (`quakelive_steam.exe` `0x004851b0`)
  copies 0xec bytes and its `AAS_UpdateEntity` (`0x00484e20`) copies state
  fields 0x1c-0x33 into entity info fields 0x23-0x3a. The retail qagame
  clears a 0xd0-byte state before filling it (`qagamex86.dll` `0x10023773`)
  and reads the extension back, for example the goal item's type and
  quantity at `0x10020c10`. FnQL copied 0x8c bytes, so the retail qagame
  read stack contents there and FnQL-game, built with the Quake III sizes,
  had the retail engine write past its entity info.

- Retail qagame calls import slot 173 (`BotPredictVisiblePosition`) with five
  arguments: origin, areanum, goal, travelflags and target (QLSRP HLIL
  `qagamex86.dll` `0x1001ae1d`–`0x1001ae2b`). The engine adapter used to
  forward only three, which handed botlib a NULL `target` to write through.
  The adapter now forwards all five, matching the legacy syscall path.
- Retail qagame reads import slot 200 (`STEAMID_QUERY`) as a 64-bit return
  in EDX:EAX (`0x1003adc4`: `call [import+0x320]; mov [esi+0x2c0], eax;
  mov [esi+0x2c4], edx`). The value feeds the `st` key of `CS_PLAYERS`. The
  adapter returned 32 bits and left EDX undefined; it now returns the whole
  SteamID.
- Retail cgame registers its settings with flag 0x800 (QLSRP
  `cg_main.c` cvar table: `cg_autoswitch`, `cg_bob`, `cg_kickScale` and the
  rest carry `CVAR_PROTECTED`). The installed menus bind items to those cvars
  and to engine-protected ones (`r_windowedMode`, `cl_allowConsoleChat`,
  `cl_demoRecordMessage`). Observed in FnQL before the fix, with both the
  retail `uix86.dll` and FnQL-game's UI: every such click or edit logged
  "Restricted source tried to set" and changed nothing, so the options menus
  and the weapon-config fields were inert. FnQL-game's
  `fnql-ui-interaction-*` gates now require those writes to land and to match
  retail's stored values.
