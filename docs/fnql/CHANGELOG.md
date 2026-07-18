# Changelog

This is the pending release-note queue for the next FnQL release.

Keep short user-facing bullets under `Unreleased` as changes land. During release publishing, the workflow asks GitHub Copilot to dedupe and categorize the notes for the GitHub release details, then clears this section for the next cycle.

## [Unreleased]

### Highlights
- Enabled bidirectional retail protocol-91 interoperability: FnQL clients now
  use Quake Live's exact Steam-ticket challenge, correctly infer bare retail
  responses, retain ticket validity through authorization, and expose a real
  no-Steam fallback. A live windowed Win32 probe joined a retail-operated
  server through gamestate and active play.
- Expanded the retail Quake Live engine-compatibility foundation across native
  modules, protocol 91 and demos, BSP advertisements, WebUI resources, and
  optional dedicated-server services.

### Compatibility
- Completed the retail client `cl_*` contract: restored the missing console,
  timing, avidemo, demo-lifecycle, recording-HUD, platform, and download
  controls while preserving FnQL aliases and modern client extensions.
- Corrected native DLL entry-point selection so retail structured modules and
  legacy `vmMain` modules use their respective ABIs without an incompatible
  probe call.
- Added a browser-neutral, versioned WebUI backend host plus an independently
  written Windows-x86 adapter for the legitimately installed retail Awesomium
  runtime; other platforms retain deterministic null/native-UI fallback.
- Added explicit legacy, ioquake3, and retail Quake Live netchan profiles,
  bounded reliable-command XOR handling, and a checked `.dm_91` record reader,
  discovery, and input validation while retaining supported legacy paths.
- Centralized overflow-safe BSP v47 advertisement-lump validation across
  collision and renderer loaders, and hardened external retail `web.pak`
  DataPack and manifest parsing.
- Matched the retail protocol-91 bare `connectResponse` while preserving the
  extended FnQL handshake, and kept retail clients joinable under the explicit
  unverified Steamworks-stub policy.
- Made protocol 91 the canonical runtime/default demo identity, added bounded
  retail binary challenge-ticket capture with replay/expiry protection, and
  replaced signed acknowledgement arithmetic with wrap-safe sequence domains.
- Preserved the exact retail UI/cgame DLL bytes across pure-filesystem restarts
  with a bounded, process-private native-module pin; pure reloads cannot select
  a different on-disk DLL and still retain compiled-QVM fallback.
- Added the open, size-tagged FnQL Steam provider ABI and a secure default-off
  runtime loader. A separately versioned closed-source `FnQL-Steam` sibling can
  now be built and staged explicitly for identity/subscription, overlays,
  callbacks, lobbies, server browsing, Workshop, stats/auth, and dedicated
  GameServer services without importing private source or SDK files into FnQL.
- Added an external, default-off ZMQ server bridge with bounded authenticated
  RCON and stats publication, fail-closed authentication, correct IPv6 and
  REQ/DEALER framing, and explicit unavailable Steamworks capability reporting
  for the engine-only default build.
- Added engine-owned bounded stat/achievement sessions and strict depth-, size-,
  grammar-, and UTF-8-checked JSON publication input for optional stats sinks.
- Added retail-shaped player-event/report aggregation, exact match-summary and
  achievement gates, provider-baseline merging, and generation-aware async stat
  stores so late completions cannot discard newer counters or unlocks.
- Matched the retail Steam-user filesystem layout and priority: active-user
  profile files override retail packages, while managed `qzconfig.cfg` and
  `repconfig.cfg` live at the profile root and preserve QL aliases/bindings.
- Added the retail QL CPI/signed-acceleration/view-filter mouse profile and
  UTF-8 character path while preserving both FnQ3 mouse styles and SDL3
  gamepads; the non-SDL Windows joystick mapping is available as an opt-in.

### Rendering and Display
- Added an explicit-only fifth `rtx` renderer with a complete raster fallback,
  conservative Quake Live material ownership, default-off native ray tracing,
  and a strict windowed retail-Steam smoke gate for reviewed RT hardware.
- Added compatibility-preserving, opt-in camera motion blur, enhanced map lens
  flares, and liquid refraction/reflection with bounded visual ripple impulses;
  all remain disabled by default and do not alter BSP, VM, snapshot, protocol,
  prediction, or demo state.
- Fixed Vulkan enhanced-liquid state restoration and added six-face Vulkan
  cubemap capture with one batched readback and consistent square projections.
- Added a strict, size-bounded `maps/<map>.fog` presentation sidecar for
  optional global fog on OpenGL-lineage and Vulkan renderers. It is disabled
  by default, preserves authored BSP fog, and ships without map-specific fog
  assets.
- Added a dedicated non-power-of-two RGBA WebUI surface to OpenGL, OpenGL2,
  GLx, and Vulkan without consuming cinematic handles; verified the complete
  retail menu through an engine renderer screenshot.

### Audio
- _None yet._

### Builds and Packaging
- Made `curl-dlopen` an `auto|true|false` Meson option so Windows links the
  bundled libcurl fallback by default while supported Unix builds retain
  runtime resolution; stale Meson option metadata now gets one `--wipe` retry.
- Added Meson/CMake compatibility contract tests and made the complete Python
  source-regression suite discoverable from the default test workflows.
- Cleared Clang and MSVC strict-warning builds across the client, dedicated
  server, all renderer variants, utilities, and tests; added a scoped Meson
  `strict-warnings` validation option and validated both x86 and x64 MSVC
  targets.
- Replaced the stale Visual Studio solution dependency graph with a maintained
  Meson frontend, removing references to deleted bundled library trees and
  inherited SDK/toolset pins; the bridge now tolerates Visual Studio PATH
  filtering and reports missing architecture toolchains directly.
- Rebranded the VS Code build, archive, launch, and runtime-sweep workflows to
  their actual FnQL artifact names and made the release task enforce strict
  warnings; legacy `FNQ3_*` environment overrides remain fallback aliases.
- Made retail-compatible VS Code launch profiles build and debug Win32 by
  default while keeping clearly labelled x64 engine-only configurations.

### Fixes
- Freed the host pointer for the windowed console and snapped its software
  cursor to absolute framebuffer coordinates across SDL, native Win32, and X11,
  while retaining Quake Live's existing raw-coordinate UI/cgame/browser lane.
- Hardened Linux Vulkan loading, ALSA signed-frame handling, XRandR mode
  arithmetic, X11 Vulkan-only output probing, and GLx material-key formatting.
- Unified every recoverable client disconnect path with retail QL: the engine
  now publishes `game.end`, restores the paused WebUI surface and input focus,
  retains native UI fallback, honors guarded out-of-band server disconnects,
  and sends the retail three-packet reliable disconnect burst.
- Corrected the Windows DirectInput mouse format to carry all eight advertised
  buttons and removed undefined OpenGL buffer-offset arithmetic.
- Made renderer2 HDR/light-grid and VBO byte calculations overflow-safe,
  corrected the light-grid element allocation size, and made native qagame
  import argument marshaling compile consistently on 32- and 64-bit targets.
- Prevented private/password cvars from crossing the WebUI event/cache boundary
  or being printed verbatim by cvar diagnostics.
- Restricted privileged WebUI navigation to `asset://ql/`, rejected ambiguous
  or oversized native requests, and added lossless checked UTF-8 conversion.
- Moved browser/DataPak startup ahead of renderer initialization with a bounded
  provisional surface, then resized it to the live renderer; this avoids the
  observed 32-bit Vulkan package-load failure. The full qz bridge now installs
  pre-document, lightweight retries cannot re-enter a page-owned main hook, and
  the legacy native `web_stopRefresh` action cannot abort the browser document.
- Rejected malformed BSP identifiers and advertisement record shapes in every
  loader, bounded all WebPak allocations, and made ZMQ multipart recovery and
  Windows runtime loading fail closed.
- Cleaned up partial DirectInput initialization and reacquire failures before
  falling back to the Win32 mouse path.
- Finished FnQL naming and legacy-variable fallbacks in the project-owned GLx
  verification and issue-triage workflows exposed by the full regression gate.
- Eliminated the retail UI startup recursion, Win32 client-state stack
  overflow, native cvar-layout mismatch, and config/path rename-buffer race;
  profile config replacement is now staged and atomically committed.

### Documentation and Tooling
- Added a subsystem-by-subsystem Quake Live porting roadmap and permanent
  independent-rewrite, retail-compatibility, and non-regression agent rules.
- Added windowed local probes for protocol-91 discovery/auth hardening, a full
  pure session with `.dm_91` finalization/replay, and authenticated ZMQ
  DEALER/PUB interoperability against administrator-supplied runtimes.
