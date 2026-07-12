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

## Validation

`tests/input_compat_tests.cpp` covers:

- linear, CPI-normalized, positive/negative accelerated, capped, and
  non-finite mouse inputs;
- QL view-angle history initialization, averaging, wraparound, and reset;
- WinMM axis normalization, movement deadzones, look acceleration, and
  inversion;
- ASCII, BMP, supplementary-plane, invalid-scalar, and UTF-16 surrogate input.

Both SDL3 and non-SDL Windows client object builds compile the shared mouse and
character consumers. The non-SDL build additionally compiles the QL WinMM
profile; the SDL3 build compiles its existing input backend unchanged.

Runtime promotion still requires a windowed retail-asset probe covering raw
mouse input with CPI off/on, a representative acceleration configuration,
console/UI/browser text entry, focus loss, and (where hardware is available)
the opt-in WinMM joystick profile. Never run that probe fullscreen.
