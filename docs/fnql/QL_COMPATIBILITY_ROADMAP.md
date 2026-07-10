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
6. FnQL must eventually load the retail modules and assets, refuse attempts by
   its client to join retail-operated servers, and accept retail clients on an
   FnQL-hosted protocol-compatible server while Steamworks is stubbed or
   replaced by an explicitly selected service provider.

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
| WebUI host | WebUI state machine, bounded resource resolver, `web.pak` bridge, input/compositor wiring, JavaScript event and qz scaffolding, plus a versioned browser-neutral backend contract with null/fake lifecycle tests | Add a renderer-neutral surface presenter and an optional evidence-gated Windows-x86 adapter, then validate the legitimate retail datapak/runtime without weakening native UI fallback |
| Awesomium | Explicit default-off policy and backend-shaped entry points | Optional external-runtime adapter, child-process bootstrap, surface/callback ownership, crash recovery, and windowed retail validation without bundling the proprietary runtime |
| Network protocol | QL fields exist in shared structures and portions of message delta handling; retail protocol constant exists | A typed retail protocol profile, connectionless/reliable token ownership, protocol-91 server identity, strict packet bounds, challenge/auth separation, and interop fixtures |
| Demo support | Generic record/playback envelope and protocol-extension walking; WebUI demo catalog | Protocol-91 selection independent of legacy defaults, `.dm_91` fixture/transcript tests, truncation and oversize rejection, seek/timedemo behavior, and unchanged legacy demo handling |
| BSP v47 | Collision and renderer loaders recognize QL BSP version 47 | Central checked-lump validation, exact version-specific layout rules, malformed-file tests, and fixture coverage across collision and every enabled renderer |
| Advertisements | Version-47 lump structures, renderer loading/query paths, UI/cgame bridge diagnostics | Shared validated descriptor parsing, consistent behavior across OpenGL/OpenGL2/GLx/Vulkan, browser/service content ownership, and deterministic fallback shaders |
| Steam discovery | Cross-platform Steam library/install discovery and retail asset validation | Keep discovery independent from Steamworks runtime state and add platform fixture coverage |
| Steamworks | WebUI-facing unavailable stubs and explicit policy labels | Optional runtime/provider interface, identity/auth tickets, server lifecycle, callbacks, overlay/social/browser/UGC capabilities, and clean shutdown; never required for the default build |
| Workshop/UGC | WebUI request surface and filesystem foundations | Optional provider-owned subscription/install snapshots, validated mount paths, download progress, and restart-safe lifetime rules |
| ZMQ | No active transport owner | Optional dynamically resolved server transport, typed stats publication, bounded RCON framing/authentication, fair nonblocking polling, and deterministic disabled/unavailable states |
| Authentication | Legacy challenge path and service stubs | Separate wire ticket encoding from provider acquisition, scrub credentials from logs/memory, and define local/open replacement policy without inventing retail-service success |
| Filesystem/paks | Retail Steam basepath discovery, Quake Live pak handling, native DLL extraction cache, WebUI datapak bridge | Harden mount ordering and path containment; add retail manifest and pure-check fixtures without shipping retail data |

## 2026-07-09 implementation wave

This wave establishes independently written compatibility contracts and safe
optional-service boundaries. It does not claim that the proprietary
Awesomium or Steamworks runtimes are implemented.

| Subsystem | Implemented in FnQL | Evidence and remaining gate |
| --- | --- | --- |
| Native game API | Loader selects the structured retail `dllEntry` ABI from the export shape before invocation, keeps the legacy `vmMain` syscall ABI separate, and rejects a structured module that publishes no exports | The legitimate local retail x86 DLLs export `dllEntry` without `vmMain`; an x86 FnQL runtime load and per-module layout/slot fixture matrix remain required |
| WebUI and Awesomium | Versioned browser-neutral C++17 backend host, deterministic null backend, fake-backend lifecycle/crash/surface/input/script tests, and a bounded Chromium DataPack v4/v5 parser for external `web.pak` | The local retail `web.pak` validated as DataPack v4 with 1,024 resources; a version-gated external Windows-x86 adapter and renderer-neutral surface presentation remain open and default-off |
| Network protocol | Per-connection legacy Q3, ioquake3, and retail QL wire profiles now own qport, sequence-checksum, reliable-XOR, and QL sideband decisions; the XOR codec is bounded and has retail-derived golden vectors | Live retail-client-to-FnQL capture/interoperability, connectionless-profile consolidation, pure/auth transcript fixtures, and the explicit FnQL-client retail-service refusal probe remain open |
| QL demos | `.dm_91` discovery/selection, strict extension parsing, a bounded little-endian record reader, explicit EOF/truncation/negative/oversize states, and preserved supported legacy-demo ordering and termination behavior | No distributable retail `.dm_91` fixture was available; windowed playback, seek/timedemo, and malformed message-body corpus validation remain open |
| BSP v47 and advertisements | One overflow-safe v47 disk contract validates base/extra lump ranges, fixed record size, bounded model indices, and finite geometry across collision, OpenGL, OpenGL2, and Vulkan loaders | A read-only local retail scan validated 149/149 v47 maps and 535 advertisement records; live advertisement presentation/fallback-material probes and renderer performance comparison remain open |
| ZMQ | Optional external libzmq boundary with bounded ROUTER/PUB work, checked socket options, strict ZAP/PLAIN and RCON multipart framing, IPv4/IPv6 endpoint validation, explicit remote-PLAIN opt-in, secret suppression, and failure-safe teardown | Still default-off. Windows requires an administrator-supplied absolute `zmq_library` path; Unix-like builds may use the system loader's libzmq soname search. Live DEALER/REQ/SUB interoperability, owned stats serialization, secure remote CURVE/tunnel policy, a shared trusted-library loader, and stress/fault injection remain open |
| Steamworks/platform services | Typed capability/status owner with retail AppID 282440 and deterministic unavailable/default-off Steam GameServer, authentication, Workshop, and stats capabilities; active retail clients remain explicitly unverified instead of being rejected by the stub | No Steam SDK/runtime code is bundled or called; self-reported identities are bounded but are not authenticated. A provider interface, identity/ticket/server/callback/UGC implementations, and legitimate runtime probes remain open |
| Non-regression infrastructure | Focused Meson/CMake C++ contract tests, recursive pytest discovery, private-cvar browser suppression, build-thread dependencies, and corrected project-owned GLx/issue-triage workflow identity | The current source gate is green; Linux/macOS builds, x86 retail-module execution, live protocol/service probes, and release packaging remain required before promotion |

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

- Workshop mount and pure-manifest reporting
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
