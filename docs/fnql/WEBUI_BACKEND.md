# WebUI runtime backend boundary

FnQL keeps the retail Quake Live WebUI path default-off and preserves the
native UI as the fallback. The engine does not bundle Awesomium, load an
unverified SDK DLL, or reproduce Awesomium C++ object layouts.

## Static evidence

The following are source observations, not claims from a live retail probe:

- FnQL already exposes the retail-shaped `CL_Awesomium_*` host façade and the
  `asset://ql/` resource resolver, but its runtime methods were deterministic
  stubs.
- The QLSRP `cl_awesomium_win32.cpp` reference models a core/session/view
  lifecycle followed by URL navigation, per-frame pumping, software-surface
  queries and copies, input injection, JavaScript calls, and reverse-order
  shutdown.
- The QLSRP reference mixes documented-style C exports with decorated MSVC C++
  imports, generated director callbacks, object upcasts, and inferred vtable
  slots. Those boundaries are compiler-, architecture-, and SDK-version-
  sensitive. They are not sufficient evidence for FnQL to call them safely.
- The QLSRP host reference starts the runtime from the home/base paths, player
  identity, render dimensions, and initial configuration/map/factory
  snapshots. FnQL can prepare those values without importing the reference
  implementation structure.

Retail behavior remains authoritative. A future adapter must record the exact
retail executable and external runtime versions used by its validation probes;
QLSRP evidence alone is not enough to declare compatibility.

## Implemented boundary

`code/client/webui_backend.hpp` defines a versioned, browser-neutral C++17
contract with:

- explicit non-owning backend installation and allocator ownership;
- a guarded dormant/starting/running/failed lifecycle;
- typed startup identity, surface size/format, input, script, and status data;
- balanced request/release callbacks for bounded launcher resources, without
  exposing the engine allocator to an adapter;
- an `asset://ql/` navigation origin lock, exact request-size handling, and
  checked UTF-16-to-UTF-8 decoding before privileged native dispatch;
- 512 MiB archive and 64 MiB per-resource limits with checked filesystem reads;
- checked row strides, capacities, dimensions, and surface-format matching;
- bounded copied diagnostics rather than borrowed error pointers;
- capability discovery for software surfaces and integer script results;
- cleanup of partial startup failures and explicit failed-state recovery; and
- a deterministic null backend on every platform.

The existing C-shaped `CL_Awesomium_*` façade delegates to this host. Runtime
startup remains opt-in through both `cl_webuiEnable` and explicit backend
installation. With no adapter installed, all availability cvars and native UI
fallback behavior remain unchanged.

Backend installation is intentionally a source/build integration seam, not a
binary C++ plugin ABI. A Windows adapter may contain the unsafe SDK-facing
details while presenting only this typed contract to the rest of the client.
If a binary plugin is later required, it needs a separately versioned C ABI;
the C++ interface must not be exported directly across arbitrary toolchains.

## Non-regression gate

The deterministic fake backend tests cover:

- default null-backend behavior and explicit availability;
- interface-version rejection;
- startup, repeated start/resize, shutdown, replacement, crash, and retry;
- partial-start cleanup;
- navigation and script forwarding;
- rejection of foreign navigation origins and ambiguous native request text;
- integer script results;
- typed mouse, wheel, keyboard, focus, pause, cache, and reload operations;
- malformed, undersized, mismatched, padded, and successful surface copies; and
- balanced browser resource request/release callbacks.

The client currently refuses to report a drawable browser overlay even when a
backend exposes a surface. This is deliberate: the existing cinematic scratch
API has power-of-two and shared-slot assumptions, so reusing it would risk
renderer and cinematic regressions. Browser input and overlay ownership remain
with the native UI until a dedicated presentation path exists.

## Remaining runtime work

1. Establish a legitimate retail probe matrix: retail executable hash,
   Awesomium SDK/runtime version, helper executable, exported symbol set, pixel
   format, load callbacks, and shutdown behavior on Windows x86.
2. Design an optional Windows-x86 adapter target that is excluded by default
   and discovers only an explicitly configured external runtime. It must fail
   closed on version/export mismatch and never search arbitrary working
   directories.
3. Prefer evidenced C exports. Any unavoidable decorated C++ import needs an
   exact version/toolchain gate, isolated ownership, and focused failure tests;
   inferred vtable offsets are not an acceptable general ABI.
4. Add a renderer-neutral dynamic RGBA upload/presentation export with its own
   resource ownership. Validate non-power-of-two dimensions, resize, alpha,
   row padding, device loss, renderer restart, and simultaneous cinematics in
   every supported renderer.
5. Project load, window-object, cursor, tooltip, dialog, and resource callbacks
   into browser-neutral events. Keep filesystem requests bounded by the
   existing launcher resolver and keep Steam-backed resources unavailable
   until the Steamworks compatibility slice provides them.
6. Validate startup, navigation, script bridge, resize, focus, crash recovery,
   cache clearing, and teardown against retail assets in windowed mode. Add
   regression probes for the native UI, renderer switching, cinematics, and
   platforms where no browser backend exists.

Until those gates pass, the runtime is a tested integration boundary rather
than a claim of working Awesomium support.
