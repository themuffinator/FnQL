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
| retail | `qagamex86.dll`, `cgamex86.dll`, `uix86.dll` from the filesystem (retail `bin.pk3` or loose `baseq3`) | default |
| fnql | `fnql_qagamex86.dll` (Linux: `fnql_qagamei386.so`) | `vm_gameProvider fnql` |

FnQL-game modules carry the `fnql_` prefix (`FNQL_GAME_MODULE_PREFIX`) so a
search path holding both sets can never shadow a retail module by accident.

`vm_gameProvider` (archived, protected, default `retail`) applies on the next
map load:

- `retail` takes the historical path unchanged: the engine loads `qagame`
  exactly as it did before providers existed.
- `fnql` loads the FnQL-game qagame first. If it is missing, fails ABI
  validation, or does not identify itself (below), the engine unloads it, logs
  why, and loads the retail module instead. Unknown values behave like
  `retail` and log a warning.
- Pinned native reloads (pure restarts) never consult the provider.
- `vm_game` still chooses native versus QVM first. Providers only apply to the
  native path.

Only qagame has a provider today. cgame and ui get `vm_cgameProvider` and
`vm_uiProvider` once the purity rules below are implemented for them; until
then FnQL always loads the retail client modules.

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
  that retail clients may see inconsistent game state. Automatic provider
  selection (a future `auto` value) will require this bit.

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

## Interoperability rules

The engine owns the wire (protocol 91 field tables in `msg.c`). A game module
owns what the fields mean. The rules that make FnQL-game invisible to retail
peers:

1. **Retail profile by default.** FnQL-game's default behavior matches retail
   on the wire: item, weapon, event and means-of-death numbering, configstring
   indices and payloads, server command grammar, and prediction results.
   Retail HLIL is the authority for these values; QLSRP's configstring layout
   is known to diverge.
2. **Negotiated extensions only.** FnQL-game announces itself through a
   `fnql_game` serverinfo key. FnQL-game cgame will answer with a userinfo
   key. Anything beyond retail behavior flows only between peers that both
   announced, and only through per-client channels; retail peers never
   receive an unknown server command or a reinterpreted field.
3. **Server modules stay server-side.** Reading `FNQL_GAME_SERVER_MODULE_FILE`
   never marks its pak as referenced (`FS_GeneralRef`), just as
   `vm/qagame.qvm` never did. Retail clients are never asked to download or
   hold the package that carries the FnQL-game qagame. The retail
   `qagamex86.dll` keeps its historical reference behavior.
4. **Honest purity for client modules (pending, FnQL-game milestone 10).** A
   pure server validates the pak that holds `cgamex86.dll`, not the bytes a
   client runs. FnQL will select an FnQL-game cgame/ui on a pure server only
   when that server lists the FnQL-game client package. In that case the client
   reports that package's checksum, and an FnQL server accepts it. On every
   other pure server FnQL clients use the retail modules. The engine must never
   report a retail checksum for an FnQL-game module.
5. **No synthesized success.** Steam authorization, purity and capability
   answers always reflect what actually ran.

## Validation

- `tests/game_module_provider_source_tests.py`: provider selection order,
  identity checks, the server-module reference exemption, and the
  `BotPredictVisiblePosition` import arity.
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

## Retail evidence recorded here

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
