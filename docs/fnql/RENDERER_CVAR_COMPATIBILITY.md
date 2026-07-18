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
HDR, tone-map, mode, custom-size, stereo, and hardware-gamma paths. A retail
alias takes ownership only when one of these conditions is true:

1. the alias existed before compatibility registration, normally because a
   retail-style configuration supplied it;
2. its value differs from the retail default; or
3. the user changes it after registration.

A protected ownership marker is persisted with the generated aliases so an
untouched default cannot masquerade as an imported retail setting on the next
process launch. Consequently, retail's default `r_enableBloom 1` cannot turn on
FnQL bloom in a fresh profile, while an imported QL configuration behaves as
requested. Live changes to every retail post-process parameter activate the
same bridge.

Where a modern backend supports the requested effect, the bridge maps retail
gates and the directly corresponding intensity/threshold controls into its
existing pipeline. `r_contrast` is applied as centered contrast in each final
output path. Each renderer family preserves its existing bloom and color-grade
implementation.
Active-state mirrors report the path actually running rather than merely
echoing requested values.

## Capability and Fallback Rules

`r_floatingPointFBOs` requests floating-point scene storage independently of
FnQL's HDR lighting mode. GLx uses RGBA16F when the existing texture capability
allows it. Vulkan and RTX validate the required color-attachment and
sampling features before selecting RGBA16F and warn before falling back to the
normal SDR format. A request never fabricates support.

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
and capability fallback across every retained renderer. Compile validation
must cover all three renderer modules plus the client translation unit. Runtime
retail validation should use legitimate Steam assets, windowed mode, imported
retail cvar presets, a fresh FnQL profile, and both supported and unsupported
floating-point framebuffer hardware paths.
