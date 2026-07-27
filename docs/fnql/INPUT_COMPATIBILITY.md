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

Character input keeps each platform producer intact. The shared client lane
accepts Unicode scalar values directly and combines valid UTF-16 surrogate
pairs from Win32 before encoding one-to-four UTF-8 bytes. Invalid scalars and
unmatched low surrogates are ignored; pending surrogate state is cleared with
the normal held-key state on focus changes. ASCII and control characters
remain byte-for-byte compatible with FnQ3.

The default SDL3 gamepad implementation and its hotplug, named-button, analog,
and configurable-axis support remain unchanged. The non-SDL Windows backend
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

| Owner | Absolute | Relative | Confined | OS cursor | Re-centred |
| --- | --- | --- | --- | --- | --- |
| Gameplay | no | yes | yes | hidden | on entry |
| Menu | yes | no | fullscreen only | visible | no |
| Console | yes | no | fullscreen only | hidden | no |

An unfocused or minimized window drives no pointer input at all: it holds no
confinement, hides no cursor, and reports no positions.

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

`in_nograb` keeps its per-backend meaning and is applied outside the shared
policy: it only suppresses the relative gameplay pointer, since overlay owners
already run unconfined.

Backend notes:

- SDL keeps one owner-keyed absolute-position cache for the console, retail
  UI/cgame, and the browser, so a cached sample from one coordinate space
  cannot suppress the first sample of the next. The applied mode is latched, so
  a steady state issues no SDL calls. Drag capture now covers every absolute
  owner, matching Win32 and X11, so a menu drag that leaves the window still
  delivers its release.
- Win32 confines with `ClipCursor` and re-asserts it when the window rect moves,
  because Windows drops the clip region on deactivation. `win_wndproc.cpp`
  routes mouse messages through the same `WIN_ResolvePointerOwner` the frame
  update presents for.
- X11 shares its single client pointer grab between confinement and drag
  capture through a reason mask, and latches the window cursor attribute so the
  per-frame evaluation does not cost an X round trip per frame. A fullscreen
  X11 window on a multi-monitor desktop still leaves the rest of the desktop
  reachable, so the console keeps its absolute cursor there; that established
  accommodation is preserved as a backend input.

## Validation

`tests/input_compat_tests.cpp` covers:

- pointer ownership for every catcher combination, including console-over-menu
  and the console that cannot present an absolute cursor;
- pointer presentation for each owner across windowed/fullscreen, focus loss,
  minimization, and `in_mouse 0`, plus the mode equality the backends latch on;
- absolute-position projection: identity when the spaces match, both scaling
  directions, edge coordinates staying strictly inside the drawable, negative
  coordinates surviving for the owner to clamp, and unknown geometry passing
  through rather than collapsing to zero;
- linear, CPI-normalized, positive/negative accelerated, capped, and
  non-finite mouse inputs;
- QL view-angle history initialization, averaging, wraparound, and reset;
- WinMM axis normalization, movement deadzones, look acceleration, and
  inversion;
- ASCII, BMP, supplementary-plane, invalid-scalar, and UTF-16 surrogate input.

`tests/windowed_mouse_source_tests.py` gates the structure: that every backend
resolves ownership through the shared policy rather than a private predicate,
that the policy keeps confinement, relative motion, and cursor visibility
separate, that each backend applies `confineToWindow` and stops driving input
when the window is unusable, that SDL uses one owner-keyed dedup cache and
captures drags for every absolute owner, that Win32 shares one resolver between
its message pump and its frame update and invalidates the clip latch on
deactivation, and that X11 shares one grab and latches its cursor.

Both SDL3 and non-SDL Windows client object builds compile the shared mouse and
character consumers. The non-SDL build additionally compiles the QL WinMM
profile; the SDL3 build compiles its existing input backend unchanged.

Runtime promotion still requires a windowed retail-asset probe covering raw
mouse input with CPI off/on, a representative acceleration configuration,
console/UI/browser text entry, focus loss, and (where hardware is available)
the opt-in WinMM joystick profile. Never run that probe fullscreen. The
fullscreen confinement path is the one behavior this slice adds that a windowed
probe cannot exercise; it needs a separate multi-monitor fullscreen check of
opening an in-game menu, moving the pointer toward the second display, and
confirming the game keeps focus.
