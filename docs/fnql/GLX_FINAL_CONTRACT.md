# GLx Final Renderer Contract ADR

## Status

Accepted target contract, 2026-05-09.

This document defines the final GLx replacement contract. It is not a claim
that the current renderer already satisfies the contract. The current
implementation and release-candidate gates remain transitional until the work
below is implemented and proven.

Where this ADR conflicts with older GLx planning notes, this ADR governs new
GLx replacement work.

## Context

FnQL preserves retail Quake III Arena compatibility, demo compatibility,
hot-path efficiency, and cross-platform viability. GLx exists to consolidate
the OpenGL renderer lineage behind the existing renderer module boundary
without changing the game code, demo formats, protocol behavior, asset loading,
or VM behavior.

The current GLx work has useful bridge points, capability probing,
verification tooling, dynamic stream experiments, static-world packet work, and
material-key diagnostics. It is still a transitional renderer that can depend
on legacy OpenGL renderer ownership for final draw behavior. That is acceptable
for migration evidence, but not for the final replacement.

## Decision

The final GLx renderer has these non-negotiable properties:

- Versioned C ABI: the engine-facing boundary is `REF_API_VERSION 13`,
  `GetRefAPI`, `refimport_t`, and `refexport_t`.
- Modern C++ internals: renderer implementation code may use C++ ownership
  tools internally, but exceptions and C++ object ownership do not cross the C
  ABI.
- GLx-owned draw behavior: a GLx frame cannot depend on runtime delegation to
  legacy OpenGL renderer draw ownership for success. Shared renderer-common
  types, compatibility parsers, math helpers, asset loaders, and telemetry
  vocabulary may remain shared.
- One front end, tiered executors: the front end compiles Quake/FnQL render
  intent into explicit render products, then a GLx-owned executor for the
  selected tier submits them.
- Five product tiers: the active product tier is exactly one of `GL12`,
  `GL2X`, `GL3X`, `GL41`, or `GL46`. Feature flags live underneath that tier
  instead of replacing it.
- Deterministic pass order: GLx preserves the id Tech 3 / FnQL render order
  needed for compatibility instead of adopting an unconstrained frame graph.
- Scene-linear color pipeline: HDR means scene-linear rendering, exposure,
  bloom, grading, tone mapping, and explicit output transforms, not only
  framebuffer bit depth.
- Proof before promotion: GLx becomes the OpenGL-lineage default only after
  the feature matrix, tier matrix, versioned parity suites for screenshots,
  demos, HUD, shadows, bloom, cel-shading, greyscale, and render-scale, and
  performance gates are green.

## ABI Contract

The renderer module boundary remains deliberately boring:

| Surface | Contract |
|---|---|
| Export | `GetRefAPI` remains the only exported renderer entry point. |
| Version | `REF_API_VERSION` is bumped only when the whole engine ABI is deliberately versioned; the renderer-only liquid-interaction export moved it to `13` after the WebUI and QL host-font revisions. |
| Imports / exports | Engine communication stays in `refimport_t` and `refexport_t`. |
| Module identity | `cl_renderer glx` loads the GLx module without changing game VM, demo, protocol, or pak behavior. |
| Memory and errors | GLx uses engine-owned allocation/error surfaces at the ABI edge; C++ exceptions never cross the ABI. |
| Runtime switch | `vid_restart` and renderer switching must leave the next renderer with a clean engine-facing state. |

## Ownership Contract

GLx may share renderer-neutral code, but final draw behavior belongs to GLx.

Allowed shared surfaces:

- image, font, noise, math, pak/file, and renderer-common type helpers;
- shader/material parsers whose output is renderer-neutral;
- C ABI bridge declarations and small public telemetry structs;
- compatibility test fixtures and sweep tooling.

Disallowed final-state dependencies:

- a GLx path that succeeds only because a legacy OpenGL draw path renders the
  unsupported work;
- legacy OpenGL state setup as the final source of GLx material, fog, dynamic
  light, postprocess, or static-world draw semantics;
- silent fallback from GLx renderer selection to `opengl` or `opengl2`;
- default cvar behavior that changes demo-visible output without parity proof
  and migration notes.

During migration, compatibility fallbacks may remain only when they are
explicitly named transitional paths, counted in diagnostics, and blocked from
promotion by the GLx gates.

## Render Products

The front end emits these GLx-owned products before tier execution:

| Product | Purpose |
|---|---|
| `FramePass` | Deterministic top-level pass schedule and pass metadata. |
| `WorldPacket` | Static BSP/lightmap/fog/sky work grouped for tier execution. |
| `DynamicDraw` | Entities, marks, particles, beams, weapon, UI, and other transient geometry. |
| `MaterialIR` | id Tech 3 / FnQL shader intent with sort, blend, depth, alpha, fog, tcgen, texmod, and bundle semantics preserved. |
| `UploadPlan` | Static and transient buffer reservations, sync policy, and resource lifetime. |
| `PostNode` | Scene-linear postprocess, bloom, grading, tone map, and resolve steps. |
| `OutputTransform` | SDR/HDR/color-managed output target and screenshot/export conversion. |

All product formats must be deterministic and testable without requiring a live
OpenGL context wherever possible.

## Pass Order

GLx keeps one compatibility-safe pass schedule:

1. Frame setup, visibility, and clear policy.
2. Sky and opaque static world.
3. Opaque entities and model surfaces.
4. Native dynamic-light accumulation, including future shaderized light-list
   work, while the compatibility renderer still preserves current visuals.
5. Marks, decals, particles, beams, and other ordered dynamic scene draws.
6. Transparent and sorted material layers in legacy-compatible order.
7. First-person weapon and viewmodel effects.
8. Cinematics, 2D UI, console, and HUD.
9. Scene-linear postprocess, bloom, grading, tone map, and resolve.
10. Output transform, screenshot, video, cube-map, and export work.

Postprocess internals may use a small dependency graph, but top-level frame
order is emitted once and validated by tests and capture logs.

## Product Tier Matrix

Tier selection happens once at OpenGL initialization. Hot paths consume the
selected tier and a frozen feature table rather than scattering extension
policy through draw submission.

| Tier | OpenGL target | Product role | Required executor behavior | Explicit limits |
|---|---|---|---|---|
| `GL12` | OpenGL 1.2-class compatibility context, with extensions only as optional accelerators | Compatibility floor for stock gameplay and demos on old hardware | GLx-owned fixed-function executor for lightmaps, multitexture composition where available, fog, sprites, beams, basic dynamic lights, 2D/HUD, cinematics, screenshots, and demo-safe rendering | SDR only; no material compiler requirement; no modern HDR/post chain; no heavy-map acceleration promise |
| `GL2X` | OpenGL 2.x with GLSL-era programmability | Baseline programmable renderer | GLSL material execution for common stage shapes, VBO use where available, dynamic entities, postprocess-lite, and explicit unsupported-material reporting | No dependence on UBOs, sync objects, indirect draw, DSA, or buffer storage |
| `GL3X` | OpenGL 3.x feature family | First fully modern-feeling shipped tier | FBO-backed postprocess, structured render targets, UBO-style frame/object state where available, timer queries, sync-aware uploads, static/dynamic buffer ownership, and robust screenshot/export paths | No architectural dependence on 4.1+ or 4.3+ features |
| `GL41` | OpenGL 4.1 core-compatible ceiling | Named macOS ceiling and strong cross-platform modern tier | Full modern GLx path without requiring 4.3 debug output, 4.4 buffer storage, 4.5 DSA, or MDI; high-quality SDR and supported scene-linear post chain | 4.3-4.6 features remain optional accelerators only |
| `GL46` | OpenGL 4.6-class Windows/Linux target | High-end path | Persistent mapped uploads, buffer storage, sync-heavy streaming, direct state access, multi-draw indirect, aggressive static-world submission, and detailed GPU counters when exposed | Must remain a tier executor for the same render products, not a separate renderer |

## Tier Feature Matrix

The current implementation status for this surface is tracked in
[GLX_FEATURE_MATRIX.md](GLX_FEATURE_MATRIX.md). That matrix is the living
closure ledger; this section defines the target contract.

Legend: `Required` means the tier cannot ship without it. `Optional` means the
tier may use it when feature probing says it is present. `Explicitly absent`
means GLx must degrade deterministically, report the limit, and never delegate
the missing behavior to legacy OpenGL ownership.

| Feature surface | `GL12` | `GL2X` | `GL3X` | `GL41` | `GL46` |
|---|---|---|---|---|---|
| Stable C renderer ABI | Required | Required | Required | Required | Required |
| GLx-owned draw submission | Required | Required | Required | Required | Required |
| Retail/demo-safe pass order | Required | Required | Required | Required | Required |
| World, lightmaps, fog, sky, UI, screenshots | Required | Required | Required | Required | Required |
| Dynamic entities, marks, particles, beams, weapon | Required basic path | Required | Required | Required | Required |
| MaterialIR consumption | State-plan subset | Required common programmable path | Required | Required | Required |
| GLSL material programs | Explicitly absent | Required | Required | Required | Required |
| FBO-backed postprocess | Explicitly absent | Optional or postprocess-lite | Required | Required | Required |
| Scene-linear exposure/bloom/grading pipeline | Explicitly absent | Optional limited path | Required | Required | Required |
| SDR sRGB output transform | Required fixed-output path | Required | Required | Required | Required |
| Hardware HDR output transform | Explicitly absent | Explicitly absent | Optional | Optional platform path | Required where platform support is proven |
| Static-world GPU cache | Optional simple cache | Required where VBOs are present | Required | Required | Required |
| Transient upload ring | Optional orphan/subdata | Required fallback ladder | Required sync-aware path | Required | Required persistent path when supported |
| Uniform-buffer style frame/object state | Explicitly absent | Optional emulation | Required where available | Required | Required |
| Timer-query GPU profiling | Explicitly absent | Optional | Required where available | Required where available | Required |
| Debug labels/groups | Explicitly absent | Optional | Optional | Optional | Required where available |
| Multi-draw / packet batching | Explicitly absent | Optional narrow path | Required for stable shipped acceleration where available | Required where available without 4.3+ assumptions | Required |
| Multi-draw indirect | Explicitly absent | Explicitly absent | Optional extension path | Optional only if exposed | Required where exposed |
| Direct state access | Explicitly absent | Explicitly absent | Optional extension path | Optional extension path | Required where exposed |

## Color Pipeline Contract

GLx color handling is renderer-owned and explicit:

- texture and framebuffer formats declare their color space;
- sRGB decode/encode behavior is audited and tested;
- scene lighting, bloom thresholding, exposure, and grading happen in
  scene-linear space once the relevant tier supports the post chain;
- tone mapping and output transforms are separate from internal precision;
- SDR sRGB is always supported;
- Windows scRGB/HDR10, macOS extended-linear-sRGB/EDR, and Linux HDR are
  platform output targets behind explicit display capability checks;
- screenshots and video exports state whether they capture scene-linear,
  display-referred SDR, or HDR-transformed output.

## Promotion Rules

GLx cannot become the OpenGL-lineage default, and `opengl` cannot become a
migration alias to GLx, until all of these are true:

- the five product tiers are represented in capability policy, diagnostics,
  tests, and documentation;
- the checked-in [feature matrix](GLX_FEATURE_MATRIX.md) has no ambiguous rows;
- runtime GLx rendering no longer depends on legacy OpenGL draw ownership;
- screenshot, demo, HUD, fog, bloom, shadow, cel-shading, greyscale,
  render-scale, cinematic, and screenshot/export parity evidence is archived
  for the release candidate, with `rc-proof` carrying the current proof-corpus
  and parity-suite versions;
- `rc-parity` and `rc-proof` manifests carry current passing `worldProofEvidence`
  for the selected stock/high-geometry/lightmap/fog/visibility world maps,
  GLx screenshot histograms, static-world draw/index counters, and zero
  static packet misses, fallbacks, or errors;
- `rc-proof` manifests carry current passing `materialProofEvidence` for
  selected material-stage/tcgen corpus maps, GLx screenshot histograms,
  material renderer readiness, compile/program activity, zero material failures
  or unsupported plans, parameter-block fingerprints, required stream-material
  feature counters, positive dynamic-light stream evidence, and RC guards that
  keep screen-map and video-map material streams out of the conservative proof
  surface;
- `rc-proof` manifests carry current passing `dynamicProofEvidence` for
  selected dynamic entity, first-person weapon, dynamic-light, and planar-shadow
  corpus coverage, with required stream-category/feature counters, render-IR
  dlight ownership evidence, tier-support evidence, screenshots or timedemos for
  the selected dynamic scenes, and zero stream/category fallbacks;
- `rc-proof` manifests carry current passing `postProofEvidence` for selected
  greyscale and render-scale corpus maps, GLx screenshot histograms, ready FBO
  state, zero FBO failures, positive postprocess frame/screenshot counters,
  render-scale target-dimension evidence, non-minimized output, and a valid
  output color contract;
- `rc-stress` material proof keeps staged animated-image, screen-map, and
  video-map stage-flag evidence positive before those content-sensitive paths
  can be considered for conservative defaults;
- `rc-stress` dynamic proof keeps staged particle, transient-poly, mark/decal,
  and beam category evidence positive before those transient-scene paths can be
  considered for conservative defaults;
- tagged release packaging has revalidated non-dry-run `rc-smoke`,
  `rc-parity`, and `rc-proof` manifests for the blocking Windows and Linux
  runtime platforms through the GLx proof-root validator;
- per-tier performance budgets for draw pressure, upload volume, fallback
  counts, shader binds, static packet misses, stream wrap rejects, and GPU
  frame time pass on the blocking runtime matrix;
- migration notes, rollback instructions, and reviewed rollback package metadata
  exist before default changes ship.

The machine-readable promotion guard is `python scripts/glx_promotion.py --require-ready --proof-root <dir> --rollback-metadata <json>`. It must report `ready` before build defaults, renderer aliases, or legacy OpenGL packaging rules are changed for promotion.

## Consequences

Existing GLx RC gates remain useful for proving the transitional surface, but
they are not sufficient for final renderer promotion. Future GLx work should
reference this ADR when changing ownership boundaries, capability tiers,
material execution, pass scheduling, color output, runtime gates, or product
documentation.
