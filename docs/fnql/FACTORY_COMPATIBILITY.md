# Retail Factory Compatibility

## Scope

FnQL owns the engine-side Quake Live factory contract: definition loading,
factory CVar overrides, the native qagame lookup import, `map` selection,
arena compatibility metadata, map-pool rotation, dedicated startup, and the
WebUI catalog. It does not implement or distribute qagame, cgame, or UI game
code.

The behavior below was reconstructed independently from static inspection of
the legitimate retail Steam executable and assets. QLSRP was used to locate
candidate ownership, but retail observations decide the compatibility
contract where the two differ.

## Retail-observed contract

- `scripts/factories.txt` loads first, followed by `scripts/*.factories`.
  Each source must be smaller than `0x8000` bytes and may contain one factory
  object or an array. A syntactically valid array keeps valid members when a
  malformed member is skipped. The retail JsonCpp reader treats `//` and
  `/* ... */` comments as JSON trivia, accepts its legacy permissive numeric
  grammar (including leading-zero integers), raw string bytes, and lax legacy
  surrogate handling, and does not require end-of-input after the first
  successfully parsed root. FnQL parses nested unused metadata with a
  heap-backed stack so hostile depth cannot exhaust the process stack.
- The registry retains at most 1,024 definitions. A definition requires exact
  `id`, `title`, `basegt`, and `cvars` fields. Factory ids and base-gametype
  tokens are case-sensitive; duplicate ids remain in load order and engine
  lookup returns the first.
- Base gametypes are the numeric sequence `ffa`, `duel`, `race`, `tdm`, `ca`,
  `ctf`, `oneflag`, `overload`, `har`, `ft`, `dom`, `ad`, and `rr`.
- JsonCpp replaces exact duplicate CVar member names, enumerates the remaining
  names in unsigned-byte lexical order, and retains the first 256. The cap is
  therefore independent of source order. String settings remain strings;
  booleans and integers use retail integer formatting, and real values use six
  fixed decimal places. A null, array, or object occupies its sorted slot with
  no value and terminates later runtime application. The first eight tag
  positions are examined.
- JSON strings cross the engine boundary as C strings: a decoded `\u0000`
  terminates ids, titles, optional text, tags, setting names, and setting
  values. JsonCpp also truncates decoded object member names during insertion,
  so keys equal before the NUL are one member and the last assignment wins.
  Raw file input likewise ends at its first embedded NUL.
- Applying a different definition restores the previous definition's saved
  CVar values, snapshots the new definition's existing CVars, applies its
  settings, and publishes `g_gametype`, `g_factory`, and `g_factoryTitle`.
  A previously nonexistent CVar has no synthetic backup and consequently
  retains its created value. Server shutdown consumes saved backups but keeps
  the selected factory identity, matching retail's pointer-lifetime behavior.
- Reload keeps the active definition in its original physical registry slot.
  Lookup still scans that slot even when the new load count does not reach it;
  logical enumeration and the WebUI expose it only when registration crosses
  that slot, matching the retail split between physical and logical state.
- `map <map> <factory>` requires a factory until one is active, then permits
  omission to reuse it. The argument-count check occurs before BSP probing;
  an explicitly present empty third argument reaches exact lookup and fails as
  an invalid factory. Hidden ids are omitted from usage and rejected when
  `sv_serverType` is nonzero. A normal map rejects an existing arena record
  whose selected base-gametype flag is clear; an absent arena record remains
  permissible. `devmap` and an explicitly supplied hidden id bypass that arena
  mismatch check.
- `g_ammoPack` is registered as a latched CVar with startup default `1` and is
  revisited with map-path default `0`, as in retail. A normal `map_restart`
  performs a full spawn after an ammo-pack or gametype change, resizes clients
  after `sv_maxclients` changes, and skips those transition checks when
  qagame has set `g_restarted`.
- Arena sources use a per-file `0x4000`-byte limit and a shared 1,024-record
  registry. Map lookup is exact and first-match. The 13 flags use
  case-sensitive substring tests with the base-gametype tokens above; there
  are no host-side `tourney` or `hh` aliases. Parsing reproduces the legacy
  token and `0x400` info-string limits, including lossy pair rejection and the
  per-file intermediate quota consumed by syntactically valid blocks that do
  not contain a map.
- `sv_mapPoolFile` defaults to `mappool.txt`. A pool source is smaller than
  `0x8000` bytes and accepts at most 1,024 resolved `map|factory` rows; invalid
  rows do not consume that capacity. It splits only on LF, truncates a row at
  its first CR, stops at NUL, and treats `#` as a comment only in column zero.
  Factory validity is checked before the BSP and arena probes. Four bounded
  `0x400`-byte fields are recorded per accepted row. The three `nextmaps`
  slots preserve file order below four rows and otherwise select without
  replacement. The optional current-map slot is prepended when exact arena
  metadata exists and is not deduplicated from pool results.
- An empty pool makes `nextmap` exactly `map_restart 0`; `startRandomMap`
  instead uses retail's `campgrounds` record with an empty factory field.
  Reload commands affect only the named registry. Dedicated startup queues
  `vstr serverstartup` after common initialization.
- The WebUI factory list is an object keyed by visible factory id. Values
  contain `id`, `title`, numeric `basegt`, optional `author` and `description`,
  and lowercase string-valued `settings`. Assignment of a duplicate id makes
  the last definition visible in this object even though engine lookup remains
  first-match. The bridge sizes the payload dynamically, parses it as data,
  defines hostile property names such as `length`/`__proto__` safely, and
  invalidates document snapshots across reload and navigation.
- Rotation selection uses retail's two `rand()` draws XORed with engine
  milliseconds, followed by the same signed absolute-magnitude normalization.
  FnQL expresses the shift and `INT_MIN` case with defined unsigned arithmetic.
- Filesystem reloads and Workshop/download mounts refresh the arena, factory,
  and map-pool registries after the new search paths become visible.

## FnQL-preserved extensions

- `scripts/*.factory` is loaded last because earlier FnQ3/FnQL builds
  documented that singular suffix. It cannot replace a retail definition
  because runtime lookup remains first-match.
- Retail's WebUI path assumes every factory CVar has a scalar representation
  and dereferences the stored value unconditionally. A community definition
  with a null-valued setting would therefore fault there. FnQL ends safe WebUI
  serialization at that setting, matching the runtime's effective prefix while
  keeping malformed supplemental content from crashing the client.
- JsonCpp throws out of the retail loader when an unsigned integer cannot fit
  the signed `%i` conversion. FnQL retains that member as the same null-valued
  terminator used for unsupported kinds. It also rejects an in-memory NaN
  defensively, although the retail JSON lexer cannot produce one. These bounded
  failure paths preserve valid retail behavior without letting hostile loose
  factory content terminate the engine.

## Regression gates

The focused pure C++ catalog and rotation tests exercise malformed input,
bounds, duplicate behavior, scalar conversion, arena flags, resolution, and
rotation selection. Source-contract tests cover CVar snapshot semantics and
all engine/VM/WebUI/build-system wiring. The opt-in retail probe reads only the
user's legitimate installation and validates the stock 22-definition catalog
without copying or distributing it.

```powershell
ctest --test-dir <cmake-build-dir> -R "fnql_(factory|cvar).*"
meson test -C <meson-build-dir> fnql_factory_catalog fnql_factory_rotation
python tests/cvar_factory_snapshot_tests.py
python tests/factory_runtime_source_tests.py
python tests/webui_wiring_tests.py
python tests/retail_factory_probe_tests.py
```

Set `FNQL_RETAIL_QL_PATH` when the Steam installation is outside the standard
Windows location. The probe reports a skip when no legitimate install is
available.
