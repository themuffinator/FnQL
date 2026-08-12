# Quake Live Input Compatibility

This note records the engine-owned Quake Live input behavior implemented by
FnQL and keeps retail observations separate from FnQL design choices. Game,
cgame, and UI input consumers remain ABI boundaries; this slice does not
reconstruct module code.

## Evidence and scope

The static comparison used the legitimate retail executable evidence indexed
by QLSRP and the reconstructed QLSRP input audit. In particular, the recovered
retail `CL_MouseMove` owner at `0x004B5800` establishes the mouse formula, and
the adjacent `0x004B5640`/`0x004B5710` owners establish the angle-history
filter. The WinMM owner band establishes the optional X/Y movement and R/U
view-axis joystick mapping. These are observations. The C++ types, state
ownership, validation, fallback choices, and profile selection in FnQL are
independent implementations designed around the existing FnQ3 engine.

Observed retail mouse behavior:

- `m_cpi > 0` scales raw counts by `2.54 / m_cpi` before acceleration.
- CPI mode multiplies motion rate by `1000` and multiplies the final yaw/pitch
  axis factor by `45.45454545454546`.
- `cl_mouseAccel` is signed. Its magnitude multiplies rate above
  `cl_mouseAccelOffset`; the result is raised to
  `max(cl_mouseAccelPower - 1, 0)` and added to or subtracted from base
  sensitivity.
- A positive `cl_mouseSensCap` limits the resulting upper sensitivity.
- `m_filter` is a 1-31-sample moving average of completed yaw/pitch angles. It
  is not the inherited two-frame average of raw deltas.
- Character input is an already-shifted text lane and retail module/edit-field
  consumers receive UTF-8 bytes rather than platform UTF-16 units.

Observed retail legacy-Windows joystick behavior:

- X/Y become bounded `AXIS_SIDE`/`AXIS_FORWARD` values with independent
  movement deadzones.
- R/U become mouse-like view deltas with independent sensitivity/deadzone,
  `cl_viewAccel`, and optional vertical inversion.
- Buttons, remaining direction axes, POV input, and MIDI remain key-event
  producers.

## FnQL profiles and non-regression

`cl_mouseAccelStyle` is the compatibility selector:

| Value | Behavior |
| --- | --- |
| `0` | Existing classic FnQ3/ioquake3 acceleration and two-delta filter |
| `1` | Existing ioquake3 power acceleration and two-delta filter |
| `2` | Retail Quake Live CPI, signed acceleration, cap, and angle-history filter |

New installations default to style `2`, matching the project compatibility
target. Existing archived style `0`/`1` configurations continue to select the
unchanged FnQ3 paths. With the default `cl_mouseAccel 0`, `m_cpi 0`, and
`m_filter 0`, style `2` reduces to the established sensitivity/yaw/pitch path.
`cl_mouseAccelDebug 1` writes bounded transform diagnostics to `mouse.log`
through the engine filesystem and closes the handle when disabled or during
input shutdown.

The retail filter owns an unfiltered view-angle base while presenting an
average. FnQL preserves that mouse transform but synchronizes its private base
after keyboard look, joystick look, centerview, cgame adjustments, finite-angle
reduction, and the per-command pitch guard. This is a deliberate reliability
choice: without synchronization, the next mouse sample can restore a view that
one of those owners already replaced, making a key look change disappear. If
the pitch guard rejects an excessive sample, FnQL resets the filter instead of
preserving its hidden overshoot; otherwise the rejected motion can return as
another 90-degree step on following commands. Normal mouse-only output is
unchanged when neither correction applies.

## Usercmd sampling and stateful commands

Relative mouse deltas do not carry timestamps in the engine ABI. When usercmd
generation is suspended before a gamestate, while disconnected, or by a local
pause, FnQL therefore discards deltas collected during that unsampleable gap
and rebases the input frame clock. Held keyboard state and persistent joystick
axes remain intact. Replaying the accumulated mouse gap in the first resumed
command previously produced an artificial view kick which the pitch guard then
clipped to exactly up or down.

Engine-owned `+`/`-` transitions use the normal FIFO command stream and retain
retail `wait` ordering, but have a 64 KiB reserved tail beyond the historical
64 KiB general command-text limit. A large script or console burst can no
longer consume the storage needed by a matching movement/button press or
release. Arbitrary module and console commands retain the historical capacity
and text format.

Input event times are parsed as checked wrapping 32-bit millisecond values and
bounded to the current command interval. Malformed, stale, or future text can
no longer become an unbounded movement fraction. Usercmd indices and
`serverTime` deltas use explicit modulo-32-bit arithmetic on the client,
message codec, and server acceptance gate. The protocol representation is
unchanged; the defined arithmetic only removes signed-overflow failure at the
clock and command-number boundary.

Protocol 91's keyed usercmd codec hashes the last acknowledged server command
exactly as retail Quake Live does. In particular, it hashes `%` and signed
high-bit bytes directly. The inherited Quake III message hash substitutes `.`
for those bytes because Quake III's legacy string reader can rewrite them;
using that hash for a retail connection gives the two peers different XOR keys
and silently turns otherwise valid pitch, movement, and button changes into
unrelated values. FnQL selects the retail hash only for the Quake Live wire
profile, preserving the established Quake III and ioquake3 codecs.
The protocol-91 string writer and reader likewise preserve high-bit bytes,
matching retail and keeping UTF-8 command text identical to the bytes that
feed the checksum. For reliable server commands, the client hashes the raw
wire text and keeps a separate `%`-sanitized copy for game/UI execution.
FnQL servers sanitize `%` before both storage and transmission, which is what
retail clients expose, so FnQL-to-retail and FnQL-to-FnQL checksum keys remain
aligned without weakening format-string hardening.

Character input keeps each platform producer intact. The shared client lane
accepts Unicode scalar values directly and combines valid UTF-16 surrogate
pairs from Win32 before encoding one-to-four UTF-8 bytes. Invalid scalars and
unmatched low surrogates are ignored; pending surrogate state is cleared with
the normal held-key state on focus changes. Retail modules keep their UTF-8
byte-stream ABI. Engine-owned console, chat, and disconnected edit fields
insert a complete UTF-8 scalar or nothing, so a full field cannot be left with
a truncated character.
Clipboard input is decoded strictly, replaces malformed sequences, and treats
bytes as data rather than editor commands; C0 controls and DEL are discarded,
which also prevents a pasted Ctrl-V byte from recursively invoking paste.
SDL text input and X11 XIM focus are enabled only while the console, UI, chat,
browser, or disconnected console can consume characters. Both are reconciled
before each native event drain as well as on focus changes, so an immediate
same-frame catcher transition cannot leave an IME filtering gameplay keys.
AltGr remains usable for layout-specific console characters without exposing
its synthetic Ctrl transition as a bindable key on Windows.

The default SDL3 gamepad mapping, named-button, analog, and configurable-axis
behavior remains intact. Hotplug and global reset barriers now also clear
persistent axes and balance retained transitions. The non-SDL Windows backend
keeps its historical direction-key/U-V-trackball behavior by default. Set
`in_joystickProfile 1` and restart input to select the QL WinMM mapping; retail
movement scaling also expects `in_joyBallScale 1`. The profile is latched so a
live switch cannot leave direction keys or analog axes stuck.

## Absolute pointer coordinate space

Observed in the reconstructed retail UI and cgame modules: both project the
coordinates the engine hands them by the renderer's framebuffer size, not by the
host window.

- `_UI_MouseEvent` computes `x * SCREEN_WIDTH / uiInfo.uiDC.glconfig.vidWidth`
  (or the `bias`/`xscale` widescreen form) and `y * SCREEN_HEIGHT /
  glconfig.vidHeight`, then calls `Display_MouseMove` **only** when the result
  is inside 640x480. Out-of-range input is discarded, so a menu given the wrong
  space does not merely track inaccurately: it stops responding entirely.
- `CG_MouseEvent` performs the same division against `cgs.glconfig` and clamps.

FnQL therefore projects every absolute position into renderer drawable pixels
before queueing `SE_MOUSE_ABSOLUTE`, in `fnql::input::ProjectPointerToDrawable`.
The console and the WebUI browser already consumed that space; native UI and
cgame previously received raw host-window coordinates, which is why in-game
menus were unusable whenever the two differed — a scaled desktop under SDL,
where motion arrives in logical window coordinates, or any `r_mode` whose
resolution is not the window size. Truncation is deliberate: a host coordinate
strictly inside the window stays strictly inside the drawable, which keeps the
retail UI's upper-bound test from rejecting the last row and column.

Backends supply the host geometry: SDL uses `glw_state.window_width/height`,
Win32 the client rect (shared with `win_wndproc.cpp` through
`WIN_ProjectClientPointerToDrawable` so the message pump and the frame poll
cannot diverge), and X11 `window_width/height`. When the two spaces match the
projection is an identity, so setups that already worked are unchanged.

Drawable pixels are the renderer's *private* target, and `r_ext_supersample`
doubles it. The engine console and the WebUI browser address that private target
directly, but `CL_CopyRetailGlconfig` deliberately reports the public capture
dimensions to native retail modules, because those modules derive UI-scene FOV
and entity placement from their glconfig. `CL_ProjectDrawableToRetailModule` in
`cl_input.cpp` therefore applies the same projection a second time — private
drawable in, public capture space out — immediately before `UI_MOUSE_EVENT` and
`CG_MOUSE_EVENT`, matching the public-to-private scaling the retail draw traps
apply in the opposite direction. Without it a supersampled in-game menu tracks
at double speed and then stops responding entirely once `_UI_MouseEvent`'s
640x480 test starts rejecting the doubled position. Bytecode modules read the
private dimensions through `CL_GetGlconfig` and draw through the unscaled
syscall lane, so `vm->dllExports` gates the conversion and they keep the
drawable position unchanged.

## Pointer ownership and grabbing

Menus, the engine console, and gameplay each want different pointer handling.
Every platform backend used to derive that decision with its own predicate, and
the predicates had drifted: the SDL backend resolved ownership twice with two
different expressions and kept two unsynchronised absolute-position caches, the
native Win32 backend kept a third copy in its message pump, and the X11 backend
kept a fourth. `fnql::input::ResolvePointerOwner` and
`fnql::input::ResolvePointerMode` in `code/client/input_compat.hpp` are now the
single owner of that decision; SDL, Win32, and X11 supply platform facts and
apply the result.

Ownership. The console is an overlay that preserves any underlying menu
catcher, so it is resolved first and takes ownership from the menu beneath it
while it can present an absolute cursor. Backends that cannot present one for
the current display mode report `consoleUsesAbsolutePointer = false`, which
leaves the pointer in its established relative gameplay mode.

| Catcher | Owner |
| --- | --- |
| none, or `KEYCATCH_MESSAGE` / `KEYCATCH_RETAIL_MOUSEPASS` only | Gameplay |
| `KEYCATCH_UI`, `KEYCATCH_CGAME`, or `KEYCATCH_BROWSER` | Menu |
| `KEYCATCH_CONSOLE`, with an absolute console cursor available | Console |
| `KEYCATCH_CONSOLE`, without one | Gameplay |

Presentation. Confinement, relative motion, and OS cursor visibility are
independent axes; binding them to one "grabbed" flag is what produced the
inconsistencies. `PointerMode` reports them separately:

| Owner/source | Absolute | Relative | Confined | OS cursor | Re-centred |
| --- | --- | --- | --- | --- | --- |
| Gameplay, relative source enabled | no | yes | yes | hidden | on entry |
| Gameplay, `in_mouse 0` | no | no | no | visible | no |
| Menu | yes | no | fullscreen only | visible | no |
| Console | yes | no | fullscreen only | hidden over the client | no |

An unfocused or minimized window drives no pointer input at all: it holds no
confinement, hides no cursor, and reports no positions.

`in_mouse 0` disables only relative gameplay motion. Absolute UI, cgame,
browser, and console pointer input remains available, matching retail-shaped
behavior and keeping menus recoverable without an input restart.

This is a deliberate FnQL design choice, not a retail observation. Two behaviors
change relative to the previous FnQL baseline, both narrowed to cases that were
already broken:

- A fullscreen menu now confines the pointer to the window. Previously all
  three backends released confinement for any absolute owner, so on a
  multi-monitor desktop the pointer left the fullscreen window and a click
  there dropped the game out of focus mid-menu. Windowed menus keep the free
  pointer retail exposes, because there the desktop has to stay reachable.
- The native Win32 backend no longer polls the desktop pointer into a menu
  while the window is unfocused or minimized. The SDL and X11 backends already
  gated on focus; Win32 did not, so an in-game menu tracked the pointer while
  another application had focus.

`in_nograb` keeps its developer/streaming meaning and is applied outside the
shared policy: it suppresses relative gameplay capture only. It does not
disable an absolute owner or override the fullscreen confinement that prevents
a menu click from escaping to another monitor.

Backend notes:

- SDL keeps one owner-keyed absolute-position cache for the console, retail
  UI/cgame, and the browser, so a cached sample from one consumer cannot
  suppress the first sample of the next. Requested and applied pointer/capture
  state are tracked separately and failed SDL transitions are retried without
  log flooding. Drag capture covers every absolute owner, matching Win32 and
  X11, so a menu drag that leaves the window still delivers its release.
  Subpixel SDL coordinates are scaled before their one deliberate truncation,
  preserving high-DPI positions. Window-leave polling is retained only while
  an active drag capture is verified; a failed or lost capture instead queues
  narrow mouse recovery and stops reporting outside positions.
  High-resolution wheel fractions are bounded per device and consumer; button
  IDs 1-25 map uniquely through `K_MOUSE1..9` and `K_AUX1..16`, while invalid
  or unrepresentable higher IDs are ignored instead of aliased. Events for a
  replaced game window are rejected only after the optional SDL console has
  had a chance to consume its own window events. SDL owns explicit joystick
  and gamepad subsystem references, refreshes the UI device list even while
  controller input is disabled, and collapses the paired topology events SDL
  emits for recognized gamepads.
- Win32 confines with `ClipCursor` and re-asserts it when the window rect moves,
  because Windows drops the clip region on deactivation. `win_wndproc.cpp`
  routes mouse messages through the same `WIN_ResolvePointerOwner` the frame
  update presents for. Legacy `WM_MOUSEMOVE`/button messages feed the gameplay
  delta lane only while the legacy Win32 mouse is the active source
  (`IN_LegacyMouseDrivesInput`): while raw input or DirectInput owns the device,
  any legacy message still in the queue predates the (re)registration — a stale
  position from the click that closed a menu or from a focus change — and
  converting it into a delta kicks the view by (position − window centre) with
  no physical motion. The SDL backend gobbles queued motion on mode changes and
  X11 re-bases its warp origin under a reset delay; this gate is the Win32
  equivalent. Raw Input and DirectInput activation failures unwind partial
  ownership before falling back to a usable Win32 source. DirectInput uses the
  retail-observed 0x200-element device buffer, reducing recoverable high-polling
  bursts from becoming buffer-loss resets during a frame hitch. Raw Input device
  removal, temporary-capture loss, minimization, and hidden-window transitions
  recover held state through ordered barriers. Cursor display-count changes are
  transition-only, temporary capture is checked, and fractional or
  compatibility-mode wheel events always emit balanced key pairs. WinMM MIDI
  data is delivered through the window message queue, keeping key-event
  production on the main thread while still rejecting stale-device callbacks.
  The game window and clipboard use Unicode Win32 APIs; `WM_UNICHAR` accepts
  only Unicode scalar values, and the native keyboard path suppresses the
  synthetic left-Ctrl half of AltGr without suppressing a real Ctrl+RightAlt
  chord.
- X11 shares its single client pointer grab between confinement and drag
  capture through a reason mask, and latches the window cursor attribute so the
  per-frame evaluation does not cost an X round trip per frame. A fullscreen
  X11 window on a multi-monitor desktop still leaves the rest of the desktop
  reachable, so the console keeps its absolute cursor there; that established
  accommodation is preserved as a backend input. XIM/XIC supplies UTF-8 text
  independently of physical Quake key translation, with a legacy
  `XLookupString` fallback, clean focus/lifecycle handling, and recovery when
  the input-method server disappears. Linux non-SDL builds now include the
  native joystick backend in Meson, CMake, and Make; it probes modern and
  legacy device paths, retries bounded hotplug discovery, and recovers cleanly
  from disconnects and short or interrupted reads.

## Event, focus, and device-loss recovery

Focus changes, window recreation, input restart, and device loss are ordered
through the common event queue. A full `SE_INPUT_RESET` barrier releases held
bindings and clears client mouse deltas, joystick axes, and filter history only
after every older retained transition has been consumed. When the triggering
platform transition requires producer-cache invalidation, that happens when
the barrier is *queued*, not when it is consumed: a native event drain can
already have processed newer input by consumption time, and clearing then
would lose a new drag, wheel fraction, modifier identity, or button source
state.

Exact engine-owned stateful bindings carry a reserved, per-logical-key
generation tag in their deferred command text. Every logical
`Key_ClearStates` boundary first queues releases, then advances every generation
and synchronously clears movement, button, mouselook, and generated
push-to-talk ownership. An old command delayed by `wait` or retained around
command-buffer pressure therefore cannot resurrect engine input after a
console, catcher, disconnect, or full input reset. Only canonical engine
commands are tagged: arbitrary cgame, UI, server, and manual `+` commands keep
their retail-shaped text and behavior. An explicitly issued untagged
`steam_voice_start` survives an ordinary console/menu catcher clear, but a full
focus reset or disconnect clears that manual owner and stops recording
immediately rather than relying on a later deferred `-voice`.

Mouse-only loss uses `SE_MOUSE_RESET`. It releases mouse buttons, wheel keys,
mouse-backed auxiliary keys named by that backend's exact source mask, deltas,
and filter state. It does not broadly clear keyboard state, joystick axes, or
auxiliary keys outside that mask. SDL device removal carries the exact
mouse-owned auxiliary-key mask; DirectInput buffer loss uses the same narrow
barrier for its representable mouse range. Per-key generations and source
ownership let this narrow barrier remove only the affected mouse contribution
from a shared movement/button/mouselook/voice command. An unrelated keyboard
holder or explicit manual voice owner remains active; recording stops only
when the last owner is gone.

The system and pushed-event queues insert a full reset before the newest event
if overload forces an older transition to be discarded. Those overload
barriers leave backend caches as current producer truth while repairing the
ordered client state. Relative deltas saturate instead of overflowing signed
integers, queue sequence counters wrap with defined unsigned arithmetic,
non-finite platform samples are rejected or neutralized, and the queue capacity
is 256 events. Duplicate or invalid key releases no longer
corrupt `anykeydown` or emit redundant module releases. Left/right Shift,
Ctrl, and Alt are aggregated into their logical family; SDL and X11 likewise
aggregate Super and level-five/Mode modifiers where the platform exposes them.
Focus regain reconstructs families that remain physically held.

## Validation

`tests/input_compat_tests.cpp` covers:

- pointer ownership for every catcher combination, including console-over-menu
  and the console that cannot present an absolute cursor;
- pointer presentation for each owner across windowed/fullscreen, focus loss,
  minimization, and `in_mouse 0`, plus the mode equality the backends latch on;
- absolute-position projection: identity when the spaces match, both scaling
  directions, edge coordinates staying strictly inside the drawable, negative
  coordinates surviving for the owner to clamp, and unknown geometry passing
  through rather than collapsing to zero (the same helper performs the second,
  drawable-to-retail-module projection, so supersampled menus are covered by
  the identical cases);
- linear, CPI-normalized, positive/negative accelerated, capped, and
  non-finite mouse inputs;
- QL view-angle history initialization, averaging, wraparound, reset, and
  synchronization with non-mouse view changes;
- WinMM axis normalization, movement deadzones, look acceleration, and
  inversion;
- canonical command matching, checked event-time/source parsing, wrapping and
  bounded time accounting, reserved generation-tag parsing, and selective
  removal from a two-source engine button;
- ASCII, BMP, supplementary-plane, invalid-scalar, UTF-16 surrogate, strict
  UTF-8 decode, and saturating arithmetic.

`tests/windowed_mouse_source_tests.py` gates the structure: that every backend
resolves ownership through the shared policy rather than a private predicate,
that the policy keeps confinement, relative motion, and cursor visibility
separate, that each backend applies `confineToWindow` and stops driving input
when the window is unusable, that SDL uses one owner-keyed dedup cache and
captures drags for every absolute owner, that Win32 shares one resolver between
its message pump and its frame update and invalidates the clip latch on
deactivation, that X11 shares one grab and latches its cursor, and that the
retail UI and cgame dispatch converts the drawable position into the public
capture space while the console and browser keep the private one.

`tests/input_system_source_tests.py` additionally gates reset ordering and
scope, suspended-mouse disposal, reserved stateful-command capacity, wrap-safe
usercmd production/acceptance, DirectInput buffer depth, deferred per-key
command invalidation, source-aware push-to-talk
shutdown, queue-overflow recovery, atomic UTF-8 field capacity and completion
safety, protocol-91 usercmd hashing on both peers, modifier-family and AltGr
handling, Win32 source fallback and wheel balance, SDL
text/focus/device/window routing, and X11 XIM, minimize-property,
native-joystick, and fallback behavior.

Strict-warning x86 builds compile and link both the SDL3 and native Win32
clients plus the focused compatibility executable. Native X11 validation
compiles and links an ELF32 i386 client including native joystick input; source
gates cover XIC lifecycle, bounded minimization-property parsing, joystick
recovery/build wiring, and the legacy text fallback.

Runtime promotion still requires a windowed retail-asset probe covering raw
mouse input with CPI off/on, a representative acceleration configuration,
console/UI/browser multilingual text entry, focus loss, device hotplug, and
(where hardware is available) the opt-in WinMM joystick profile. Never run
that automated probe fullscreen. Fullscreen confinement still needs a separate
manual multi-monitor check of opening an in-game menu, moving the pointer
toward the second display, and confirming the game keeps focus.
