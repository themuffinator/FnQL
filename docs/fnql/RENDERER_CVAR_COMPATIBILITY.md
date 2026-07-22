# Renderer CVar Compatibility

## Scope

FnQL exposes the supported non-postprocess portion of the retail Quake Live
renderer and video cvar surface without replacing the renderer improvements
inherited from FnQ3. The compatibility contract is shared by the GLx, Vulkan, and RTX renderer
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

QLSRP's matrix records a separate retail framebuffer/postprocess pipeline,
including bloom, color correction, contrast, floating-point-FBO selection,
enable gates, and active-state mirrors. FnQL deliberately does not register,
implement, bridge, or report that pipeline. Retail modules may create their
own cvars, but the renderer never consumes or mirrors them. No runtime SMP path
is retained in FnQL either.

## Non-Regression Ownership

FnQL's established cvars remain canonical for its FnQ3 bloom, color-grade, HDR,
tone-map, mode, custom-size, stereo, and hardware-gamma paths. No retail
postprocess cvar is renderer-owned, and no runtime compatibility bridge writes
`r_bloom`, `r_bloom_*`, `r_colorGrade`, or final-output shader parameters. The
one-time obsolete-profile repair described below is the sole exception. This
prevents retail's lower extraction threshold from silently replacing FnQ3's
bloom threshold and keeps exactly one bloom owner across every backend.

## Capability and Fallback Rules

FnQ3's `r_hdr`, `r_hdrPrecision`, and `r_hdrBloomFormat` controls exclusively
select scene and bloom storage on GLx, Vulkan, and RTX. A retail profile can
still contain names from the unsupported QL postprocess family, but those names
remain unknown or module-owned and cannot affect renderer behavior.

One obsolete FnQL build could also persist its translated values under the
canonical FnQ3 names. FnQL repairs only the exact stale `0.25` threshold,
`0.5` intensity, one-pass, unmodulated tuple once, restoring the FnQ3 `0.75`
threshold and five-pass chain. The repair is versioned independently, reads no
QL postprocess cvar, and leaves normal FnQ3 tuning untouched.

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

`tests/renderer_cvar_compat_source_tests.py` guards supported registration,
video publication, operational renderer consumers, the complete absence of QL
postprocess controls and shader stages, and FnQ3 framebuffer ownership across
every retained renderer. `tests/fnq3_bloom_parity_source_tests.py` pins the
FnQ3 bloom implementation, exact GLx/Vulkan final-output contracts, the
QL-free RTX extension, and rejects QL postprocess pipeline fingerprints.
Compile validation must cover all three renderer modules plus the client
translation unit. Runtime retail validation should use legitimate Steam assets,
windowed mode, deterministic FnQ3 settings, and an isolated profile.
