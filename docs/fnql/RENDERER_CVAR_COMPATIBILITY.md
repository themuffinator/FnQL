# Renderer CVar Compatibility

## Scope

FnQL exposes the retail Quake Live renderer and video cvar surface without
replacing the renderer improvements inherited from FnQ3. The compatibility
contract is shared by the GLx, Vulkan, and RTX renderer
modules. Platform-owned video aliases live in the client, where renderer
restarts and actual drawable dimensions are known.

This work is based on static observations from the QLSRP renderer cvar matrix,
its reconstructed engine call sites, and the retained FnQL renderer paths. The
implementation is an independent integration into FnQL's renderer architecture;
QLSRP implementation text is not used as the design structure.

## Observed Retail Contracts

Retail-specific behavior covered by the common renderer contract includes:

- `r_fastSkyColor`, `r_drawSkyFloor`, and `r_forceMergeEntities` for fast-sky
  color, bottom sky/cloud generation, and cross-entity batching;
- `r_skipSmallBatches`, `r_skipLargeBatches`, `r_debugShaderIndex`, and
  `r_debugSortExcept` for the observed diagnostic submission behavior;
- `r_debugAds`, which publishes a bounded snapshot of loaded advertisement
  state plus retail bridge labels consistently on every backend;
- `r_enablePostProcess`, the bloom and color-correction gates and status
  mirrors, retail bloom parameters, `r_contrast`, and
  `r_floatingPointFBOs`;
- `r_smp`, `r_showSmp`, `r_ignoreFastPath`, and `r_primitives`, whose retail
  selectors remain queryable even when a modern backend has only one safe
  implementation path;
- `r_gl_vendor`, `r_gl_renderer`, `r_gl_reserved`, and
  `r_lastValidRenderer` status publication; and
- `r_aspectRatio`, the windowed-mode aliases, `r_stereo`,
  `r_ext_gamma_control`, and `r_noFastRestart` at the client/video boundary.

The retail aspect classifier publishes `1` for ratios at least 16:9, `2` for
ratios at least 16:10 but below 16:9, `3` below 4:3, and `0` otherwise. The
classification uses the active drawable rather than assuming the requested
window size. Window resize synchronization preserves FnQL's existing HiDPI
logical-versus-drawable distinction.

QLSRP's matrix records no renderer consumer for the retail bloom blur scale,
radius, or falloff controls, and no runtime SMP path is retained in FnQL.
Those cvars are intentionally registered, bounded, persistent where retail
persists them, and safe to query; they do not invent effects or concurrency
that retail evidence does not establish.

## Non-Regression Ownership

FnQL's established cvars remain canonical for its modern bloom, color-grade,
HDR, tone-map, mode, custom-size, stereo, and hardware-gamma paths. Retail
post-process cvars remain registered and bounded because retail modules and
profiles query them, but they never write `r_bloom`, `r_bloom_*`, or
`r_colorGrade`. This prevents retail's lower `r_bloomBrightThreshold` default
from silently replacing FnQ3's bloom extraction threshold and keeps exactly
one bloom owner across every renderer backend.

`r_qlRetailPostProcessBridge` is a read-only zero status value. It also retires
the ownership marker written by older FnQL builds, so renderer restarts cannot
resume the former alias bridge. `r_contrast` remains queryable but neutral in
the FnQ3 final-output paths.
Active-state mirrors report the path actually running rather than merely
echoing requested values.

## Capability and Fallback Rules

`r_floatingPointFBOs` remains queryable for retail modules and profiles but is
not a second framebuffer-format owner. FnQ3's `r_hdr`, `r_hdrPrecision`, and
`r_hdrBloomFormat` controls exclusively select scene and bloom storage on GLx,
Vulkan, and RTX. This prevents a retail profile from preserving out-of-range
SDR highlights that FnQ3 bloom would normally clamp before extraction.

Profiles written by the short-lived retail post-process bridge are identified
by its ownership marker and migrated once to the FnQ3 bloom defaults. This
restores the FnQ3 `0.75` extraction threshold and five-pass GLx chain instead
of retaining the retail `0.25` threshold and one-pass setting. Profiles without
the marker retain their explicit user tuning.

`r_gl_reserved` is similarly capability-shaped: it publishes `1` only when the
active common renderer exposes the complete occlusion-query path, otherwise
`0`. The device strings and last-valid renderer are published only after
successful renderer registration.

The retained indexed draw path is the safe implementation of retail
`r_primitives`, and FnQL has no separate fixed-function fast tessellation path
for `r_ignoreFastPath` to select. `r_smp` therefore remains read-only `0`, and
`r_showSmp` has no stall events to print. Keeping these selectors visible but
inert is preferable to introducing a divergent submission path.

## Validation

`tests/renderer_cvar_compat_source_tests.py` guards registration, ownership,
video publication, operational renderer consumers, post-process shader wiring,
the one-time bridge migration, and FnQ3 framebuffer ownership across every
retained renderer. Compile validation must cover all three renderer modules
plus the client translation unit. Runtime retail validation should use
legitimate Steam assets, windowed mode, imported retail cvar presets, and a
fresh FnQL profile.
