# Quake Live Compatibility Porting Roadmap

## Purpose

This document is the execution plan for reconstructing Quake Live engine
behavior in FnQL. It covers the engine-facing portions of the native game API,
WebUI/browser host, network protocol, demos, BSP version 47 and advertisements,
ZMQ, Steamworks, and related Quake Live-only host features.

Retail Quake Live under a legitimate Steam installation is the compatibility
target. QLSRP is a high-value evidence corpus, but it is not the implementation
architecture for FnQL. The mandatory rewrite and non-regression rules live in
[`AGENTS.md`](../../AGENTS.md).

## Non-negotiable boundaries

1. FnQL remains engine-only. `qagame`, `cgame`, and UI implementations are not
   imported; only their documented ABI and host requirements belong here.
2. QLSRP-derived work is independently designed and implemented around FnQL's
   architecture. Mechanical transplantation, cosmetic rewriting, and importing
   QLSRP implementation structure are not acceptable.
3. Retail-observed behavior outranks QLSRP and inherited Quake III behavior.
4. Existing FnQ3/FnQL functionality, modernization, performance, diagnostics,
   build variants, and platform support must not regress.
5. Live proprietary or externally serviced features remain optional and
   default-off. Their disabled state must be explicit and deterministic rather
   than silently pretending to be functional.
6. FnQL must load the retail modules and assets, join retail-operated
   protocol-91 servers through the legitimate Steam session-ticket flow, and
   accept retail clients on an FnQL-hosted protocol-compatible server. A
   no-Steam client uses the retail-shaped fallback and surfaces the remote
   server's authorization result without claiming an authenticated session.

## Evidence model

Each compatibility change records evidence at one or more levels:

- **Retail observed:** a runtime capture, binary/interface observation, retail
  asset fixture, or repeatable probe from the legitimate Steam installation.
- **QLSRP observed:** a constant, table shape, call ordering, parser behavior,
  or subsystem boundary visible in the pinned/local QLSRP reference.
- **Inherited observed:** working FnQ3/FnQL behavior that must remain available.
- **Inference:** a proposed design or interpretation not yet proven by retail
  evidence. Inferences may guide an experiment but cannot silently become an
  immutable compatibility contract.

Compatibility-sensitive constants need a nearby source comment or a linked
test/fixture explaining which observation pins them. Tests should assert
behavior and byte layout rather than source spelling wherever practical.

## Starting inventory

The status below describes FnQL at the start of the 2026-07-09 porting wave.
It is deliberately conservative: source scaffolding without a runtime owner is
not classified as a complete feature.

| Subsystem | Existing FnQL foundation | Principal remaining work |
| --- | --- | --- |
| Native game API | Retail-shaped qagame, cgame, and UI import/export tables; native-first loading; pak materialization; ABI-focused source tests | Complete slot semantics, formalize version/layout validation, exercise legitimate retail modules, and keep legacy VM fallback intact |
| WebUI host | WebUI state machine, bounded resource resolver, `web.pak` bridge, input/compositor wiring, JavaScript/qz bridge, versioned browser-neutral backend, and dedicated arbitrary-size renderer surface | Extend visual/runtime stress coverage across renderer restarts, resize, device loss, and browser-child failure without weakening native UI fallback |
| Awesomium | Independent Windows-x86 adapter for the legitimately installed retail 1.7.4.2 runtime, child process, `QL` DataPak source, offscreen view, scripts, input, and surface copy | Keep the runtime external; expand forced-failure and version/export fixture coverage while preserving x64/non-Windows null-backend behavior |
| Network protocol | QL fields exist in shared structures and portions of message delta handling; retail protocol constant exists | A typed retail protocol profile, connectionless/reliable token ownership, protocol-91 server identity, strict packet bounds, challenge/auth separation, and interop fixtures |
| Demo support | Generic record/playback envelope and protocol-extension walking; WebUI demo catalog | Protocol-91 selection independent of legacy defaults, `.dm_91` fixture/transcript tests, truncation and oversize rejection, seek/timedemo behavior, and unchanged legacy demo handling |
| BSP v47 | Collision and renderer loaders recognize QL BSP version 47 | Central checked-lump validation, exact version-specific layout rules, malformed-file tests, and fixture coverage across collision and every enabled renderer |
| Advertisements | Version-47 lump structures, renderer loading/query paths, UI/cgame bridge diagnostics | Shared validated descriptor parsing, consistent behavior across OpenGL/OpenGL2/GLx/Vulkan, browser/service content ownership, and deterministic fallback shaders |
| Input | Nine-button QL key range, browser/UI/cgame catcher routing, raw/DirectInput/Win32 mouse backends, and SDL3 gamepad support | Retail gameplay mouse math/filtering, UTF-8 text dispatch, legacy WinMM joystick mapping, focused math fixtures, and windowed runtime probes without narrowing modern SDL input |
| Fonts | Retail TTF aliases in the legacy registration lane; retail UI/cgame host-text imports | Renderer-owned UTF-8 face cache and bounded multi-page atlas now cover normal/sans/mono plus bundled and optional Windows fallbacks; retain screenshot/metric comparison as a retail visual gate |
| Steam discovery | Cross-platform Steam library/install discovery and retail asset validation | Keep discovery independent from Steamworks runtime state and add platform fixture coverage |
| Steamworks | Versioned size-tagged C provider ABI; secure default-off loader; identity/subscription, callback, overlay/social/lobby, server-browser, UGC, stats/auth, and GameServer lanes; separately versioned closed-source sibling build/staging flow | Add live logged-in retail probes, complete end-to-end challenge-ticket validation, and expand callback/fault-injection stress without making the provider a default dependency |
| Workshop/UGC | At wave start: provider-owned subscription/install/download primitives, WebUI subscribed-item snapshots, progress, and completion events | The restart-safe filesystem, pure-reference, and required-download slice described below was completed during this wave. Live logged-in subscription/download probes, unattended dedicated UGC ownership, and cross-platform provider validation remain open without allowing provider state to alter retail basepath discovery |
| ZMQ | No active transport owner | Optional dynamically resolved server transport, typed stats publication, bounded RCON framing/authentication, fair nonblocking polling, and deterministic disabled/unavailable states |
| Authentication | Legacy challenge path and service stubs | Separate wire ticket encoding from provider acquisition, scrub credentials from logs/memory, and define local/open replacement policy without inventing retail-service success |
| Filesystem/paks | Retail Steam basepath discovery, Quake Live pak handling, native DLL extraction cache, WebUI datapak bridge | Harden mount ordering and path containment; add retail manifest and pure-check fixtures without shipping retail data |

## 2026-07-09 implementation wave

This wave establishes independently written compatibility contracts and safe
optional-service boundaries. The retail Awesomium runtime is adapted from the
legitimate installation; no proprietary runtime is implemented or bundled.

| Subsystem | Implemented in FnQL | Evidence and remaining gate |
| --- | --- | --- |
| Native game API | Loader selects the structured retail `dllEntry` ABI from the export shape before invocation, keeps the legacy `vmMain` syscall ABI separate, rejects a structured module that publishes no exports, and pins the exact bounded UI/cgame bytes across pure restarts in a process-private cache | A windowed x86 session loaded the legitimate retail qagame, UI, and cgame modules. The pure restart reloaded only pinned bytes and reached active play; synthetic per-module rejection fixtures and legacy VM fallbacks remain regression gates |
| WebUI and Awesomium | Versioned browser-neutral C++17 host; independently written Windows-x86 adapter for the installed retail runtime; pre-renderer DataPak reservation with renderer resize; pre-document qz bridge and lightweight retries; child-process, input, script, diagnostics, and bitmap lifecycle; dedicated arbitrary-size presentation in every renderer; deterministic null fallback elsewhere. Recoverable disconnects now enter retail fullscreen-UI mode, publish `game.end`, resume/focus the retained WebView without destroying its route state, and select native UI only when WebUI ownership is unavailable | The local retail Awesomium 1.7.4.2 runtime and DataPack v4 with 1,024 resources produced clean live 1280×720 full-menu screenshots under OpenGL, OpenGL2, GLx, and Vulkan. Disconnect restoration has source gates for server commands, timeout, local command, recoverable errors, Escape, and guarded out-of-band drops. A 2026-07-17 windowed probe joined retail endpoint `168.100.161.119:27076`, emitted `game.end`, reacquired `web_browserActive`, retained live friend/server publication through the filesystem/renderer/UI restart, and captured the restored retail WebUI. Resize, device-loss, child-crash, and repeated renderer-switch stress coverage remain open |
| Network protocol | Per-connection legacy Q3, ioquake3, and retail QL wire profiles own qport, sequence checksum, reliable XOR, fragmentation, usercmd/entity/playerstate layouts, download blocks, and QL sideband decisions; protocol 91 is canonical. The client emits retail's exact binary `getchallenge` SteamID/ticket layout, infers protocol 91 from its bare response, keeps the ticket valid through asynchronous server authorization, and falls back to retail's bare text challenge when Steam is unavailable. FnQL's server continues accepting retail and older marked FnQL challenge shapes | Focused byte-layout and UDP probes verify discovery, retail-binary challenges, bare protocol-91 response selection, no-Steam fallback, malformed-ticket silence, and same-host handoff constraints. On 2026-07-17 a windowed Steam-enabled Win32 client connected to retail endpoint `168.100.161.119:27076`, received its bare challenge and connect response, completed pure/Workshop setup, loaded the legitimate retail cgame, entered `campgrounds`, exchanged live commands/scores, and disconnected cleanly. With Steam explicitly disabled, FnQL repeatedly sent the bare fallback without any local refusal; that retail host correctly remained silent rather than admitting an unauthenticated client. Retail-executable packet capture and broader server coverage remain useful promotion evidence |
| QL demos | `.dm_91` discovery/selection, strict extension parsing, bounded little-endian records, explicit EOF/truncation/negative/oversize states, canonical protocol-91 recording, and preserved supported legacy ordering/termination | The automated windowed session finalized a non-empty server `.dm_91`, verified its sequence/length terminator, copied it to an isolated homepath, and replayed it through the legitimate retail cgame without protocol errors. No retail asset or generated demo is distributed; seek/timedemo and a larger malformed message-body corpus remain useful stress gates |
| BSP v47 and advertisements | One overflow-safe v47 disk contract validates base/extra lump ranges, fixed record size, bounded model indices, and finite geometry across collision, OpenGL, OpenGL2, and Vulkan loaders | A read-only local retail scan validated 149/149 v47 maps and 535 advertisement records; live advertisement presentation/fallback-material probes and renderer performance comparison remain open |
| Input | Additive QL mouse style with CPI normalization, signed power acceleration, cap, and bounded view-angle filtering; UTF-8/surrogate-safe character dispatch; opt-in QL WinMM joystick profile; preserved FnQ3 mouse styles and SDL3 gamepads | Retail executable constants and QLSRP owner mapping back the focused C++ fixtures. Windowed hardware/runtime probes and Linux/macOS build coverage remain required |
| ZMQ | Optional external libzmq boundary with bounded ROUTER/PUB work, checked socket options, strict ZAP/PLAIN and RCON multipart framing, IPv4/IPv6 endpoint validation, explicit remote-PLAIN opt-in, constant-time password checks, secret suppression, failure-safe teardown, atomic command capture, and bounded validated JSON publication | An opt-in pyzmq/libzmq probe verifies bad-auth rejection, authenticated DEALER RCON with atomic output, and SUB receipt of validated JSON. It remains default-off; Windows requires an administrator-supplied absolute `zmq_library`. Unencrypted remote PLAIN remains rejected unless explicitly allowed; production remote use still requires an external secure tunnel/CURVE policy, plus deployment-specific load/fault testing |
| Stats and achievements | Fixed-size engine-owned sessions use the retail field and achievement maps, load provider baselines before deltas, enforce training/practice/game-state gates, retain dirty data across failures, and match asynchronous store completions by generation. A strict report accumulator normalizes player deaths, caches player stats, and publishes original `MATCH_REPORT` plus eligible merged `MATCH_SUMMARY` JSON | C++ contract tests cover counters, achievements, late store completions, retry retention, malformed/duplicate/oversized reports, summary eligibility, and normalized event shape. Provider 0.3.0 adds a Win32-only, read-only serializer for the observed VC10/JsonCpp value/tree layout with page, pointer, tag, tree, depth, node, cycle, UTF-8, and output validation. Other architectures and every malformed input retain the safe deferral path; a live retail match-report probe remains an evidence gate |
| Workshop/UGC | Provider-neutral transactional snapshots feed validated read-only filesystem registrations bounded to 256 subscribed and 256 process-lifetime transient installs. Registration distinguishes rejected, unchanged, and changed paths; rejects nonterminated/root paths; and preserves the last authoritative snapshot across provider failures. Startup accepts item-root and item-local `baseq3` layouts ahead of normal roots, retains normal-root priority, propagates exact item identity through pak references, and publishes `sv_referencedSteamworks` plus configstring `0x2cb` independently of GameServer identity. Clients strictly parse the bounded required list, arbitrate manual versus connection-owned progress, download missing items sequentially at high priority, defer unrelated reloads, expose retail UI-import-96 progress, restart the filesystem once, and retain the existing pak/download fallback. Independent bounded state tracks concurrent subscribe/unsubscribe actions; `fs_skipWorkshop` and `com_buildScript` provide explicit mount gates | Static QLSRP comparison and read-only retail inspection pin the limits, mixed root layout, ordering, trailing-space reference list, configstring, sequential queue, and UI-progress contract. FnQL bootstraps the GameServer before Workshop enumeration and refreshes dynamic capabilities. Provider 0.3.0 selects `SteamGameServerUGC` for a dedicated owner after server initialization, routes its callbacks and call results through the GameServer pump, and removes UGC before shutdown; deterministic Win32, Windows x64, and Linux fake-runtime tests cover the owner transition. No live logged-in retail Workshop download is claimed. Provider loss/fault injection, a live unattended dedicated download, and platform-appropriate live runtimes remain promotion gates |
| Steamworks/platform services | Open, versioned provider contract and secure loader with retail AppID 282440; external sibling 0.3.0 resolves the legitimate retail flat API and implements all 25 gated ABI capabilities on retail Win32 (100%), covering identity, subscription, overlays, callbacks, lobbies/chat/invites, five server-list modes, client/listen/dedicated UGC ownership, stats, auth, dedicated GameServer publication, and bounded retail-JSON conversion. Non-Win32 providers leave the VC10-specific bit clear; missing/disabled/failing providers retain deterministic legacy fallbacks | Strict fake-runtime integration proves the ABI, native server-list callback lane, dynamic GameServer UGC ownership, bounded/adversarial JSON conversion on Win32, and explicit unsupported behavior on Windows x64 and Linux. The 2026-07-17 live retail-server join validates logged-in identity, challenge-ticket acquisition, and end-to-end admission. Live match-report conversion, unattended Workshop operation, provider fault injection, sustained GameServer callback stress, and platform-appropriate live runtimes remain open |
| Non-regression infrastructure | Focused C++/source contract tests, recursive Python discovery, live protocol/ZMQ scripts, private-cvar browser suppression, build-thread dependencies, and corrected project-owned workflow identity | Current MSVC x86/x64 client/server builds and the 17 focused tests pass; the complete x86 suite passes 42 with one environment-dependent OpenAL skip. Windowed retail-module protocol/demo and live ZMQ probes pass. Linux/macOS builds, retail-executable packet capture, and release packaging remain broader promotion gates |

No retail asset, proprietary runtime, SDK binary, reconstructed game module, or
credential is stored in the repository. The retail observations above were
read-only probes against the user's legitimate local Steam installation.

## Architecture and execution order

### 1. Compatibility descriptors and safety rails

- Introduce narrow, immutable descriptors for protocol, module ABI, BSP layout,
  and optional service capabilities.
- Keep parsing/serialization decisions out of scattered cvar checks.
- Make unsupported and disabled states queryable by the client, server, native
  modules, WebUI, and diagnostics.
- Add bounds-first helpers and compile-time layout assertions at every retail
  ABI boundary.
- Establish one full source regression command and register it with Meson.

Promotion gate: descriptors reproduce current behavior for existing profiles;
the new retail profile has focused unit/fixture tests and cannot accidentally
enable a live external service.

### 2. Native module bridge

- Treat qagame, cgame, and UI as three separately versioned ABI contracts.
- Validate module API version, export count, required slots, pointer width, and
  structure layout before invoking module code.
- Use typed host adapters internally and keep vararg/syscall shims only at the
  legacy boundary.
- Preserve VM and legacy native-module fallbacks where they already work.
- Reject an incompatible module with a precise diagnostic and no partially
  initialized state.

Promotion gate: source/table tests, synthetic DLL fixtures for success and each
rejection class, legitimate retail-module load probes, and unchanged legacy VM
tests.

The opt-in cgame module probe starts a local `campgrounds ffa` session with a
temporary homepath and `+set r_fullscreen 0`. It requires a user-built Win32
FnQL executable and the user's legitimate retail installation; it verifies the
startup registration load and active cgame load both report API 8, then waits
for `CL_InitCGame` without an import-table or syscall failure. It neither
copies retail files nor contacts retail servers.

```powershell
$env:FNQL_CGAME_RUNTIME_EXE = "<path-to-x86-fnql.exe>"
python tests/retail_cgame_module_probe_tests.py
```

Set `FNQL_RETAIL_QL_PATH` when the Steam installation is outside the standard
Windows location.

The companion synthetic cgame probe builds a temporary x86 test DLL, places it
only in a temporary homepath, and loads it through the same native module path.
It proves cvar registration/range/update plus the host-table float return at
retail slot 10, temporary-homepath filesystem I/O plus BotLib parser handles, local mute-state, memory/key-query pointer buffers, and native-pointer `GETGLCONFIG`/`GETGAMESTATE` imports,
`SETUSERCMDVALUE`/`GETUSERCMD` state, a live `GETSNAPSHOT` copy, and
engine-driven `CG_DRAW_ACTIVE_FRAME`/`CG_CONSOLE_COMMAND` calls; it does not replace or modify any
retail file.

```powershell
$env:FNQL_CGAME_RUNTIME_EXE = "<path-to-x86-fnql.exe>"
python tests/native_cgame_abi_probe_runtime_tests.py
```

It discovers Visual Studio 2022 automatically when installed in a standard
location. Set `FNQL_VSDEVCMD` to a `VsDevCmd.bat` path when needed.

### 3. Protocol 91 and demo 91

- Model the Quake Live retail wire contract as a protocol profile rather than
  changing unrelated Quake III constants in place.
- Route handshake tokens, info keys, qport/challenge fields, reliable command
  codec policy, pure checks, snapshot layouts, and demo extension selection
  through that descriptor.
- Keep the client-side policy that prevents joining retail-operated services
  separate from the ability to parse the retail protocol.
- Make the FnQL dedicated server advertise and enforce the intended retail
  protocol profile so retail clients can connect to it.
- Treat demos as untrusted input: checked little-endian envelope reads, bounded
  messages, deterministic EOF/truncation errors, and no native-module loading
  from untrusted demo content.

Promotion gate: golden packet and `.dm_91` transcripts, malformed corpus,
FnQL-to-FnQL checks, a retail-client-to-FnQL-server probe, an FnQL-client retail
server refusal probe, and unchanged explicitly supported legacy demo playback.

### 4. BSP v47 and advertisements

- Parse the header and every lump through a shared overflow-safe range checker.
- Isolate version-47 extensions from version-46 and IHV conversion paths.
- Validate element size, count, model/surface references, cell identifiers,
  finite geometry, and renderer resource limits before allocation or upload.
- Convert validated disk records into renderer-neutral host descriptors.
- Feed all renderers from the same validated records and provide a deterministic
  fallback material when browser/service advertisement content is unavailable.

Promotion gate: synthetic v46/v47/malformed fixtures, collision plus all
renderer loader tests, retail map smoke probes, advertisement visibility/query
checks, and unchanged v46 map behavior and load performance.

### 5. Browser-neutral WebUI host and optional Awesomium adapter

The implemented boundary, static evidence, validation coverage, and adapter
blockers are tracked in [`WEBUI_BACKEND.md`](./WEBUI_BACKEND.md).

- Keep URL normalization, datapak lookup, event queues, input translation,
  frame composition, and game/UI bridge logic browser-neutral.
- Express the backend as a small capability/lifecycle interface with explicit
  start, navigate, update, resize, surface-copy, script, input, crash, and stop
  operations.
- Resolve an externally supplied backend/runtime only when explicitly enabled;
  never vendor the retail Awesomium binaries or assume they exist.
- Bound all JS/resource payloads, serialize callbacks on the engine thread, and
  retain a functional native UI fallback after startup or runtime failure.
- Preserve the retail `web.pak` resource names and observable qz bridge shape
  where evidence requires them, while independently implementing host logic.

Promotion gate: fake-backend lifecycle tests, loose-file and datapak resource
tests, input/compositor regressions, forced crash/restart tests, and windowed
retail datapak/runtime probes where the user supplies the legitimate files.

### 6. Optional platform services, Steamworks, and ZMQ

- Define provider-neutral capability snapshots; consumers request a capability
  and never infer it from a DLL being present.
- Keep Steam install discovery usable when Steamworks is absent.
- Dynamically resolve optional runtimes behind typed adapters with all-or-
  nothing symbol validation, retry policy, and idempotent teardown.
- Separate Steam identity/ticket, server, browser, workshop, overlay/social,
  stats, and callback capabilities so partial runtimes fail honestly.
- Separate ZMQ transport from stats/RCON business logic. Validate multipart
  frames, cap peers and payloads, use nonblocking work budgets, authenticate
  before command dispatch, and scrub secrets.
- Keep every online-service path build-time and runtime default-off until an
  open, documented, testable replacement is selected.

Promotion gate: no-provider/default-disabled tests, fake-provider tests,
missing/partial runtime tests, callback and shutdown stress, RCON auth/framing
tests, stats schema fixtures, and explicit opt-in integration runs.

### 7. Other substantial Quake Live host features

After the primary lanes are structurally stable, audit and migrate these
related engine-owned features as independent slices:

- Steam server browser metadata and lobby/overlay event plumbing
- Steam authentication ticket fields in the challenge/connect path
- screenshot/file/cursor bridges used by retail UI modules
- voice/speaking/mute identity bridges
- renderer and audio data extensions referenced by retail modules/assets
- server-side stats/achievement publication boundaries
- platform callback pumping and dedicated-server lifecycle
- protocol-aware console completion, server cache, and WebUI catalogs
- deterministic retail fixture collection and transcript tooling

Game rules, weapons, movement, gametypes, HUD implementation, and other game
module behavior remain out of scope even when QLSRP contains reconstructed
implementations for them.

## Non-regression matrix

Every slice must cover the affected cells; `not applicable` requires a written
reason in the change record.

| Dimension | Required comparison |
| --- | --- |
| Retail compatibility | New behavior versus recorded retail evidence and legitimate retail fixtures |
| Existing compatibility | Before/after behavior for supported Q3/FnQ3/FnQL paths |
| Security/robustness | Bounds, malformed input, path containment, partial initialization, shutdown, and secret handling |
| Performance | Relevant allocation, frame, packet, map-load, and polling budgets; no unbounded per-frame work |
| Determinism | Stable parsing, serialization, ordering, fallback state, and reproducible fixture output |
| Windows | MSVC/MinGW compile paths, DLL loading conventions, Steam path handling, and optional runtime absence |
| Linux/BSD | dynamic loader names, filesystem case/path behavior, dedicated server, and no Windows-only assumptions |
| macOS | build guards, bundle/runtime paths, and deterministic unsupported-provider behavior |
| Legacy builds | Meson first; Make/CMake/project-file ownership stays coherent or explicitly delegates |
| Packaging | no retail/proprietary assets, credentials, SDK binaries, or fetched dependency build trees |

## Validation commands

Fast source regression gate:

```powershell
python -m pytest
```

Preferred configured build gate:

```powershell
meson compile -C <build-dir>
meson test -C <build-dir> --print-errorlogs
```

Runtime probes must use a legitimate retail installation, run windowed with
`+set r_fullscreen 0`, and select the cheapest probe that answers the open
question. Runtime evidence must record executable/build identity, asset path,
profile, command line, outcome, and logs without copying retail assets into the
repository.

The protocol-91 session probe launches only windowed clients and uses isolated
homepaths. It verifies pure activation, snapshot traffic, server-demo
finalization, and playback:

```powershell
python scripts/probe_protocol91_session.py --client <fnql-x86> --server <fnql-ded-x86> --basepath <retail-ql-path>
```

The ZMQ probe requires pyzmq and an administrator-supplied libzmq. It verifies
authentication failure/success, atomic RCON, and validated publication:

```powershell
python scripts/probe_zmq.py --exe <fnql-ded> --library <libzmq> --basepath <retail-ql-path> --homepath <scratch-home>
```

## Definition of done for a porting slice

A slice is complete only when:

1. its motivating observations and remaining inferences are written down;
2. the FnQL implementation is independently structured and scoped to the
   engine-owned boundary;
3. failure, disabled, and fallback behavior is explicit;
4. focused compatibility tests pass;
5. the full source/build regression gates pass in proportion to risk;
6. platform and packaging effects are accounted for;
7. any intentionally changed legacy behavior is backed by retail evidence and
   a regression test; and
8. the roadmap status and remaining risks are updated honestly.
