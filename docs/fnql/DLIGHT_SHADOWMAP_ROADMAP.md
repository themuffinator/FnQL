# Dynamic-Light Shadow Mapping Roadmap

This document tracks the dynamic-light shadow mapping work for the GLx and
Vulkan renderers. Keep it current whenever shadow-map planning, allocation,
rendering, filtering, testing, or release evidence changes.

## Goals

- Preserve retail Quake III compatibility, demo behavior, and renderer module
  boundaries.
- Support high-quality shadows from point dynamic lights on world geometry,
  brush models, and entity models in both GLx and Vulkan.
- Keep the implementation efficient in hot paths: bounded light budgets,
  explicit culling, predictable atlas allocation, and renderer-local GPU work.
- Grow toward stable filtering and eventual cascaded shadow maps for directional
  light paths without mixing CSM concerns into the dlight milestone.

## Checklist Rules

- `[x]` means the item is implemented in both GLx and Vulkan unless the item
  names one renderer.
- `[ ]` means the item is not implemented yet.
- Items tagged `(partial)` are intentionally present but not complete enough to
  close the milestone.
- Every implementation step must update this checklist, the current snapshot,
  and the testing evidence in the same change.

## Current Snapshot

As of June 4, 2026:

- `[x]` Dynamic-light shadow cvars, planning counters, candidate filtering, and
  per-view prioritization are implemented.
- `[x]` Per-dlight six-face atlas slot metadata and shared atlas layout logic
  are implemented.
- `[x]` GLx allocates a depth-only shadow atlas texture/FBO.
- `[x]` Vulkan allocates a sampled depth atlas image/view/descriptor and a
  depth-only shadow atlas render pass/framebuffer.
- `[x]` GLx and Vulkan render planned dlight atlas tiles with opaque world,
  brush-model, and entity-model casters from existing lit-surface lists.
  World and brush-model casters use `SF_FACE`, `SF_GRID`, and `SF_TRIANGLES`;
  entity-model casters use `SF_MD3`, `SF_MDR`, and `SF_IQM` when
  `r_dlightMode 2` enables entity dynamic-light surfaces.
- `[x]` `r_dlightTest` injects repeatable test dynamic lights from the renderer.
- `[x]` `scripts/dlight_shadow_test.py` generates GLx/Vulkan shadow test
  launches, manifests, dry-runs, and optional RenderDoc wrapping.
- `[x]` `r_dlightShadowDebug 1` reports planning counters plus atlas render
  counters: `fill`, `render lights`, `faces`, `batches`, `draws`, `surfs`,
  atlas CPU time, and `lowvalue` overload skips.
- `[x]` GLx and Vulkan lighting sample the dlight shadow atlas with selectable
  `r_dlightShadowFilter` modes for per-face point-light lookups on world
  receivers: hard shadows, 2x2 PCF, and the default four-tap poisson-style PCF.
  The temporary screen-space fallback has been removed.
- `[x]` GLx shadow sampling uses ARB-fragment-program-safe scalar atlas
  coordinate clamps/scales so drivers that reject short source swizzles compile
  the dlight shadow programs instead of disabling `r_dlightShadows`.
- `[x]` Brush-model casters are rendered into the atlas from lit-surface brush
  model entries.
- `[x]` Entity-model casters are rendered into the atlas from lit-surface MD3,
  MDR, and IQM model entries when `r_dlightMode 2` is active.
- `[x]` Conservative per-face caster culling skips caster bounds that are fully
  outside each point-light cube face, and empty face tiles are preflighted and
  skipped before viewport/scissor setup or draw submission.
- `[x]` GLx and Vulkan apply tuned dlight-shadow bias: lower default
  slope/constant caster depth bias during atlas writes, angle-aware caster
  normal offset before CPU tessellation is submitted, and angle-aware,
  texel-scaled receiver bias during atlas sampling so wall-contact shadows do
  not detach at corners.
- `[x]` Basic 2x2 PCF filtering has been upgraded to selectable GLx and Vulkan
  dlight and sky-sun CSM shadow-map filters: hard shadows, 2x2 PCF, and
  default four-tap poisson-style PCF.
- `[x]` GLx and Vulkan cache atlas tiles for static world-only dlight shadow
  casters when the light parameters, receiver count, lit-surface set, atlas
  face size, and atlas resource generation match. Non-cacheable entity,
  brush-model, and deformed-shader caster tiles are invalidated and redrawn.
- `[x]` GLx and Vulkan cache the spotlight shadow atlas against a signature of
  every spot plan (origin, direction, radius, outer angle, atlas rect) plus
  the world-only caster set, skipping the per-tile clear and re-render on a
  match — including the repeated spot pass that per-view shadow planning
  schedules on portal/mirror frames. Entity, brush-model, and deformed-shader
  casters force a re-render, mirroring the dlight tile cache policy, and
  cache hit/miss/uncacheable counters report through `r_spotShadowDebug`.
- `[x]` GLx and Vulkan memoize per-light dlight shadow caster bounds across
  the six cube-face lit-surface walks: face-invariant local bounds (including
  MD3 vertex-walk bounds) are derived once on the first face and reused for
  the remaining faces, keyed by chain position and validated against the
  surface and entity that produced them.
- `[x]` When shadow candidates exceed the configured atlas light budget, GLx and
  Vulkan stop assigning atlas slots to candidates below 1/16th of the strongest
  candidate priority and report those skips separately from hard budget skips.
- `[x]` GLx and Vulkan batch shadow-map caster submission by the valid
  depth-only state boundary: one shadow material batch per world/entity
  transform for each rendered atlas face, instead of reopening the batch when
  the lit-surface list changes original materials.
- `[x]` Shadow atlas telemetry reports planned atlas fill, backend shadow draw
  counts, and pass timing. GLx exposes a `dlight-shadow-atlas` GPU pass timer
  through `r_speeds 7`/`r_glxGpuPassTiming`; Vulkan exposes the dlight atlas
  render-pass timestamp span through `r_speeds 7`.
- `[x]` Directional CSM is defined as a separate, disabled-by-default feature
  path with `r_csm*` cvars. GLx and Vulkan compute deterministic
  practical-split cascades, stable sphere-derived light-space bounds,
  atlas-facing texel snapping from the planned cascade resolution, expanded
  light-depth bounds for off-slice world casters, allocate sampled shadow
  atlases, and render sky-sun shadows for opaque BSP world geometry, entity
  models, and brush models.
- `[x]` GLx and Vulkan share shadow filter and bias utility policy between
  dlight shadows and CSM where practical. Dlight and CSM sampling/atlas
  rendering use the common filter/bias clamps, while CSM keeps separate
  receiver-bias, caster-bias, and strength settings in cvars and debug output.
- `[x]` Vulkan CSM caster normal bias now matches the GLx incoming-light
  convention. Both backends bias CSM casters with the negated
  `tr.csm.lightDirection`, so healthy CSM manager telemetry cannot be paired
  with a shadow atlas displaced by a renderer-specific light-vector sign drift.
- `[x]` Vulkan CSM producer projection now converts the full OpenGL clip-Y row
  before MVP upload, including the orthographic translation term used by
  cascade atlas bounds. This keeps world-geometry receivers sampling
  a depth map rendered with the same light-space projection used for receiver
  lookup.
- `[x]` Vulkan CSM receiver sampling now accounts for the top-left Vulkan image
  origin by sampling `1.0 - light_coord.z` for atlas Y, while GLx keeps
  `light_coord.z`. The Vulkan receiver also clamps the PCF center within the
  cascade tile so filter taps cannot bleed into neighboring cascades.
- `[x]` CSM world-caster planning now snaps only the atlas-facing light-space
  axes, expands light-depth bounds, and pads cascade culling by two texels.
  This keeps static BSP caster coverage from partially clipping or shimmering
  while preserving the stable texel grid used by receiver sampling.
- `[x]` CSM atlas cache signatures quantize the unsnapped light-depth bounds
  to texel granularity. Stationary and sub-texel camera motion keep reusing
  the cached atlas despite continuous light depth, while the expanded depth
  extent keeps the bounded receiver staleness harmless.
- `[x]` Vulkan CSM caster and receiver passes now match GLx by walking the
  sorted draw-surface list once per cascade and switching entity transforms
  inline. This avoids per-entity rescans that could reorder state lifetime and
  leave static-world CSM output partial or unstable.
- `[x]` GLx `rc-parity` and Vulkan `vk-modern` runtime gates plan dedicated
  `dlight-shadow-scenes` runs. These runs launch with latched dlight shadow
  cvars, load retail maps with `devmap`, inject persistent `r_dlightTest`
  lights, capture `shadowScene` screenshots, and parse scene-marked dlight
  shadow planning/render log samples.
- `[x]` The dlight shadow release-evidence matrix now covers world geometry,
  brush models, entities, alpha-tested surfaces, portals/mirrors, and
  stress-light budgets. GLx and Vulkan gate evaluation require every category
  to appear in both screenshot metadata and scene-scoped shadow log samples;
  the stress row injects 16 test dlights against an 8-light atlas budget.
- `[x]` `scripts/dlight_shadow_release_gate.py` requires reviewed GLx and
  Vulkan build, shader validation, non-dry-run screenshot/log sweep, and
  RenderDoc inspection evidence before `r_dlightShadows` may be enabled by
  default. The gate reports a policy failure if either renderer's source
  default is promoted before the evidence is ready.
- `[x]` GLx and Vulkan runtime sweeps include the
  `surfacelight-large-planar` shadow scene on `q3dm6`, with surfacelight
  proxies, surfacelight proxy shadows, 2D spot shadows, and dynamic-light
  shadows enabled for large planar `q3map_surfaceLight` validation.
- `[x]` Runtime manifests now describe surfacelight validation through
  top-level dlight shadow scene metadata, per-screenshot scene/category
  metadata, shadow-manager spot atlas publication samples,
  `surfaceLightSpot` telemetry, and `surfaceLightSpotLod` smoke summaries.
- `[x]` The release gate requires `surfacelight-large-planar` category coverage
  for published surfacelight spot atlas manager logs, active surfacelight spot
  telemetry, and passing surfacelight spot LOD smoke.
- `[x]` GLx and Vulkan runtime sweeps include the `csm-shimmer-path` shadow
  scene on `q3dm17`, with deterministic `setviewpos` micro-movements for
  tiny origin and view-axis deltas.
- `[x]` CSM debug output reports light-depth centers per cascade, and runtime
  sweep manifests summarize CSM shimmer stability through `csmStability`
  sample counts, cache events, atlas generation delta, and light-depth center
  delta.
- `[x]` GLx and Vulkan runtime sweeps compare each `csm-shimmer-path` nudge
  and micro-yaw screenshot against the path baseline step. Dlight shadow scene
  runs now report `csmShimmerScreenshots` summaries, per-candidate
  `csmShimmerComparison` metrics, generated diff PNG paths, and gate failures
  when the bounded RMS or changed-pixel smoke thresholds are exceeded.
- `[x]` GLx and Vulkan runtime sweeps include the `combined-shadow-atlas`
  shadow scene on `q3dm6`, with CSM, point dynamic-light shadows, generated
  sidecar spot shadows, surfacelight spot proxy shadows, and the shared shadow
  manager schedule active in one frame.
- `[x]` Dlight shadow scene runs stage `maps/q3dm6.lights.json` under the
  sweep homepath for the combined smoke scene, report the generated sidecar in
  top-level `dlightShadowSidecars` metadata, and preserve per-run
  `sidecarLights` evidence.
- `[x]` Runtime manifests now report `combinedShadowAtlas` summaries that
  require one manager scene sample to schedule and publish point, spot, CSM
  atlas, and CSM receiver work, with active point, static-sidecar spot,
  surfacelight spot, and CSM atlas dimensions/generation counters. GLx
  `rc-parity` and Vulkan `vk-modern` gates fail when this smoke is missing or
  reports unpublished atlas lanes.
- `[x]` `r_csmDebugFallback` forces renderer-local CSM diagnostics for
  no-world, no-sky-sun, atlas-unavailable, and zero-cascade planner fallback
  paths without changing retail scene state.
- `[x]` GLx and Vulkan runtime sweeps include four `csm-fallback-*` shadow
  scenes on `q3dm17` and report `csmFallbacks` summaries that require the
  manager to avoid CSM atlas, receiver, and publication work when no valid
  cascade publication exists.
- `[x]` GLx `rc-parity`, Vulkan `vk-modern`, and the dlight shadow release
  gate require passing CSM fallback smoke before default-enable evidence can
  pass.

Next milestone: complete RenderDoc capture validation when a runtime asset
environment is available. Real shadow maps remain disabled by default until
reviewed evidence makes the default-enable release gate ready.

## Surfacelight Validation Artifacts

The surfacelight validation contract is tied to the `dlight-shadow-scenes`
runtime sweep path. This is visual-only renderer evidence; it must not affect
retail demos, protocol behavior, VM execution, or asset loading.

Required runtime manifest fields:

- Top-level manifest metadata must include `dlightShadowEvidenceCategories`
  with `surfacelight-large-planar`, `dlightShadowEvidenceScenes` with the scene
  definition, and `dlightShadowSceneCvars` with the cvars used by the generated
  shadow-scene config.
- Each representative screenshot entry in `runs[].screenshots[]` must include
  `shadowScene: true`, `scene: surfacelight-large-planar`, `map: q3dm6`, and
  `evidenceCategories` containing `surfacelight-large-planar`.
- `runs[].dlightShadow.shadowManager.scenes["surfacelight-large-planar"].max`
  must show published spot atlas manager evidence with non-zero
  `spotPublished`, `spotSurfaceCandidates`, and `spotSurfacePlans`, plus atlas
  dimensions through `spotAtlasWidth`, `spotAtlasHeight`, and
  `spotAtlasTileSize`.
- `runs[].dlightShadow.surfaceLightSpot.scenes["surfacelight-large-planar"].max`
  must show non-zero `surfaceSpotCandidates`, `surfaceSpotPlans`,
  `surfaceSpotAllocated`, `surfaceSpotFootprintMax`,
  `surfaceSpotCasterRadiusMax`, and `surfaceSpotTileMax`.
- `runs[].dlightShadow.surfaceLightSpotLod.scenes["surfacelight-large-planar"]`
  must report `status: passed`, cover `requestedTiles.low`,
  `requestedTiles.nominal`, and `requestedTiles.promoted`, and keep
  `maxRequestedTile`, `maxEffectiveTile`, `maxAtlasTile`, and `maxFill` within
  the sweep's LOD bounds.
- `scripts/dlight_shadow_release_gate.py` must continue to include
  `REQUIRED_SURFACELIGHT_SPOT_CATEGORIES = ("surfacelight-large-planar",)` so
  GLx and Vulkan evidence cannot pass with only generic dlight-shadow samples.

Representative runtime scene:

- Scene id: `surfacelight-large-planar`.
- Map: `q3dm6`.
- Category: `surfacelight-large-planar`.
- Vulkan baseline key:
  `vk-modern-dlight-shadows-surfacelight-large-planar-q3dm6-vulkan`.
- GLx baseline key:
  `rc-parity-dlight-shadows-surfacelight-large-planar-q3dm6-glx`.

Required surfacelight debug cvars:

- `developer=1`, `logfile=2`, `r_dynamiclight=1`, `r_dlightMode=2`,
  `r_dlightShadows=1`, `r_dlightShadowDebug=1`, `r_dlightShadowFilter=2`,
  `r_dlightShadowMaxLights=8`, and `r_dlightShadowResolution=256`.
- `r_spotShadows=1`, `r_spotShadowDebug=1`, `r_spotShadowMaxLights=16`, and
  `r_spotShadowResolution=512`.
- `r_surfaceLightProxies=1`, `r_surfaceLightProxyDebug=1`, and
  `r_surfaceLightProxyShadows=1`.

Manual visual review notes for large planar emitters:

- Review both GLx and Vulkan `shadowScene` screenshots and diffs for broad
  planar emitters casting stable representative spot shadows near their
  receivers.
- Treat telemetry-only success as insufficient if the screenshot shows a
  missing surfacelight shadow, an inverted or badly rotated projection,
  unrelated geometry pulled into the cone, over-darkened receivers, obvious
  atlas flooding, or tile-edge artifacts.
- Confirm that `surfaceSpotFootprintMax` and `surfaceSpotCasterRadiusMax` are
  non-zero and plausible for the visible emitter, that `surfaceLightSpotLod`
  reports `passed`, that effective tiles do not exceed atlas tiles, and that
  atlas fill stays within the sweep's `85%` smoke bound.
- Keep the default-enable release gate blocked until these notes are recorded
  alongside reviewed GLx and Vulkan screenshots, plus RenderDoc inspection
  evidence.
  This is the RenderDoc inspection evidence checkpoint for surfacelight
  validation.

## Combined Atlas Smoke Artifacts

The combined atlas smoke contract exercises all shadow-atlas publications in a
single dlight-shadow runtime scene. It is renderer-local validation and must not
change demos, protocol behavior, VM execution, or retail asset loading.

Required runtime manifest fields:

- Top-level manifest metadata must include `combined-shadow-atlas` in
  `dlightShadowEvidenceCategories`, a `combined-shadow-atlas` scene definition
  in `dlightShadowEvidenceScenes`, and staged sidecar metadata in
  `dlightShadowSidecars`.
- The dlight-shadow run must preserve `sidecarLights` records for the generated
  static spot light and include a representative screenshot with
  `shadowScene: true`, `scene: combined-shadow-atlas`, `map: q3dm6`, and
  `evidenceCategories` containing `combined-shadow-atlas`.
- `runs[].combinedShadowAtlas` must report `status: passed` for scene
  `combined-shadow-atlas`.
- `runs[].combinedShadowAtlas.max` must show `scheduledPasses >= 4` and a
  `scheduledMask` with bits `0x0f`, plus non-zero `pointScheduled`,
  `spotScheduled`, `csmAtlasScheduled`, `csmReceiverScheduled`,
  `pointPublished`, `spotPublished`, and `csmPublished`.
- The same summary must show non-zero point atlas fields through
  `pointPlanned`, `pointRecords`, `pointAtlasWidth`, `pointAtlasHeight`, and
  `pointAtlasFaceSize`; spot atlas fields through `spotPlans`,
  `spotStaticPlans`, `spotSurfacePlans`, `spotAtlasWidth`, `spotAtlasHeight`,
  and `spotAtlasTileSize`; and CSM fields through `csmCascadeCount`,
  `csmAtlasWidth`, `csmAtlasHeight`, and `csmGeneration`.

Representative runtime scene:

- Scene id: `combined-shadow-atlas`.
- Map: `q3dm6`.
- Category: `combined-shadow-atlas`.
- Static sidecar: generated `maps/q3dm6.lights.json` containing
  `combined-sidecar-spot`.
- Vulkan baseline key:
  `vk-modern-dlight-shadows-combined-shadow-atlas-q3dm6-vulkan`.
- GLx baseline key:
  `rc-parity-dlight-shadows-combined-shadow-atlas-q3dm6-glx`.

Required combined-smoke debug cvars:

- `developer=1`, `logfile=2`, `r_dynamiclight=1`, `r_dlightMode=2`,
  `r_dlightShadows=1`, `r_dlightShadowDebug=1`,
  `r_dlightShadowMaxLights=8`, and `r_dlightShadowResolution=256`.
- `r_staticLights=1`, `r_staticLightDebug=1`,
  `r_staticLightMaxLights=8`, `r_staticLightShadows=1`, and
  `r_staticLightShadowMaxLights=2`.
- `r_spotShadows=1`, `r_spotShadowDebug=1`,
  `r_spotShadowMaxLights=16`, and `r_spotShadowResolution=512`.
- `r_surfaceLightProxies=1`, `r_surfaceLightProxyDebug=1`, and
  `r_surfaceLightProxyShadows=1`.
- `r_csmShadows=1`, `r_csmDebug=1`, and `r_csmResolution=512`.

Manual visual review notes for combined atlas smoke:

- Review the GLx and Vulkan combined-scene screenshots to confirm the frame is
  lit by the injected point lights, the generated sidecar spot, surfacelight
  spot proxies, and the sky-sun CSM path without obvious atlas cross-talk.
- Confirm the manager log reports all schedule lanes and publication counters
  in the same scene sample, because separate passing point/spot/CSM scenes do
  not prove the manager can publish all active atlas types together.
- Treat a `combinedShadowAtlas` pass as telemetry smoke only. It does not
  replace RenderDoc inspection of atlas resources, barriers, descriptors, tile
  contents, or receiver sampling.

## CSM Fallback Smoke Artifacts

The CSM fallback smoke contract verifies that planned receiver sampling stays
disabled whenever the shadow manager has no valid CSM publication. This is
renderer-local validation; it must not affect demos, protocol behavior, VM
execution, retail asset loading, or normal sky-sun discovery.

Required runtime manifest fields:

- Top-level manifest metadata must include `csm-fallback-no-world`,
  `csm-fallback-no-sun`, `csm-fallback-atlas-unavailable`, and
  `csm-fallback-zero-cascade` in `dlightShadowEvidenceCategories`, with
  matching scene definitions in `dlightShadowEvidenceScenes`.
- Each representative screenshot entry in `runs[].screenshots[]` must include
  `shadowScene: true`, one of the `csm-fallback-*` scene ids, `map: q3dm17`,
  and the matching fallback category in `evidenceCategories`.
- `runs[].csmFallbacks` or `runs[].dlightShadow.csmFallbacks` must report
  `status: passed`, list reason coverage for `no-world`, `no-sky-sun`,
  `atlas`, and `zero-cascade`, and include scene-scoped skip samples for every
  fallback category.
- The fallback summary must keep CSM publication and scheduling fields at zero:
  `csmAtlasScheduled`, `csmReceiverScheduled`, `csmPublished`,
  `csmCascadeCount`, `csmAtlasWidth`, `csmAtlasHeight`, `csmGeneration`, and
  `csmFallbackCascades`.
- The same summary must show non-zero reason counters through
  `csmFallbackNoWorld`, `csmFallbackNoSun`, `csmFallbackAtlasUnavailable`, and
  `csmFallbackZeroCascade`; the no-world scene must also prove `noworld` is
  non-zero in the matching manager sample.
- `scripts/dlight_shadow_release_gate.py` must continue to require passing
  `csmFallbacks` smoke and the four fallback categories so GLx and Vulkan
  evidence cannot pass with only happy-path sky-sun CSM samples.

Representative runtime scenes:

- Scene id: `csm-fallback-no-world`; map: `q3dm17`; reason: `no-world`.
- Scene id: `csm-fallback-no-sun`; map: `q3dm17`; reason: `no-sky-sun`.
- Scene id: `csm-fallback-atlas-unavailable`; map: `q3dm17`; reason:
  `atlas`.
- Scene id: `csm-fallback-zero-cascade`; map: `q3dm17`; reason:
  `zero-cascade`.
- Vulkan baseline key example:
  `vk-modern-dlight-shadows-csm-fallback-no-world-q3dm17-vulkan`.
- GLx baseline key example:
  `rc-parity-dlight-shadows-csm-fallback-no-world-q3dm17-glx`.

Required CSM fallback debug cvars:

- `developer=1`, `logfile=2`, `r_dynamiclight=1`, `r_dlightMode=2`,
  `r_dlightShadows=1`, `r_dlightShadowDebug=1`,
  `r_dlightShadowMaxLights=8`, and `r_dlightShadowResolution=256`.
- `r_csmShadows=1`, `r_csmDebug=1`, `r_csmResolution=512`, and
  `r_csmDebugFallback=1`, `r_csmDebugFallback=2`,
  `r_csmDebugFallback=3`, or `r_csmDebugFallback=4` for the four forced
  fallback scenes.

Manual review notes for CSM fallback smoke:

- Treat fallback smoke as telemetry validation. The screenshots prove the
  sweep path and scene metadata; they are not expected to show visible CSM
  shadows.
- Confirm each fallback scene logs `csm plan cascades:0 skip ...` with the
  expected reason and never schedules or publishes CSM atlas or receiver work.
- Confirm the no-world fallback also records a non-zero `noworld` manager
  sample, because the receiver path must stay disabled before any world-model
  assumptions leak into CSM sampling.
- This smoke does not replace RenderDoc inspection of the normal sky-sun CSM
  atlas, barriers, descriptors, or receiver sampling path.

## Roadmap

### Phase 1: Planning And Budgets

- `[x]` Select shadow-casting point lights from visible dlights only.
- `[x]` Skip linear lights until a dedicated representation exists.
- `[x]` Prioritize by brightness, radius, distance to the camera, and receiver
  count.
- `[x]` Expose counters for considered, candidate, planned, and skipped lights.
- `[x]` Maintain stable per-light `shadowIndex` and atlas slots for the backend.

### Phase 2: Atlas Resources And Validation

- `[x]` Allocate depth atlas resources in GLx and Vulkan.
- `[x]` Keep atlas sizing deterministic from `r_dlightShadowMaxLights`,
  `r_dlightShadowResolution`, and the renderer texture-size limit.
- `[x]` Maintain the test command and scripts as new renderer paths come online.
- `[ ]` Use RenderDoc captures to confirm resource lifetime, layout transitions,
  framebuffer/render-pass contents, and descriptor binding.

### Phase 3: Caster Collection

- `[x]` Reuse existing lit-surface receiver data for first-pass world casters.
- `[x]` Render opaque world `SF_FACE`, `SF_GRID`, and `SF_TRIANGLES` casters.
- `[x]` Add brush-model caster collection.
- `[x]` Add entity-model caster collection.
- `[x]` Add conservative per-face culling against point-light cube faces.
- `[x]` Do not render sky, nodlight, translucent-only, flare, or screen-space
  surfaces into shadow maps.

### Phase 4: Shadow Atlas Rendering

- `[x]` Render six cube faces per planned dlight into its atlas tiles.
- `[x]` GLx: add a depth-only FBO path with viewport/scissor per tile.
- `[x]` Vulkan: add a depth-only render pass/framebuffer path or equivalent
  rendering path when available.
- `[x]` Keep color writes disabled and depth state explicit.
- `[x]` Add debug names, pass labels, and counters so RenderDoc captures are
  readable.
- `[x]` Avoid rendering empty faces.
- `[x]` Add slope/normal-aware depth bias for the atlas render path.

### Phase 5: Sampling And Filtering

- `[x]` Replace the screen-space fallback with real shadow-map sampling.
- `[x]` Start with hard shadows and a stable depth bias.
- `[x]` Add per-light atlas tile lookup and cube-face selection in GLx.
- `[x]` Add per-light atlas tile lookup and cube-face selection in Vulkan.
- `[x]` Add 2x2 PCF.
- `[x]` Add a small rotated or poisson-style PCF kernel.
- `[x]` Tune normal/slope bias and receiver bias to reduce acne without
  peter-panning, including texel-scaled receiver bias for contact corners.
- `[x]` Make filtering selectable without changing compatibility defaults.

### Phase 6: Efficiency

- `[x]` Cache shadow maps for static lights when the light, view relevance, and
  caster set allow it.
- `[x]` Avoid rendering empty faces and low-value lights under load.
- `[x]` Batch shadow-map draws by shader/material state where renderer
  architecture allows it.
- `[x]` Track planned light count, face count, and submitted caster surface
  count.
- `[x]` Track atlas fill, shadow draw counts, and GPU time.
- `[x]` Draw static-world casters from the world VBO in the dlight/spot atlas
  passes (June 11, 2026): a depth-only ARB caster vertex program
  (`dlightShadowCasterVP`) applies the point-light normal bias on the GPU, so
  moving (uncacheable) weapon/projectile lights no longer force per-face CPU
  re-tessellation of world geometry. Measured on q3dm0 (GL46, 1080p, plasma
  fire, `r_dynamiclight 1`): firing frame-time delta dropped from +2.3 ms to
  +0.9 ms on GLx and to ~0 ms on the opengl renderer; entity casters keep the
  CPU tess path and `RB_ApplyDlightShadowCasterNormalBias`. A/B screenshots
  (`r_vbo 1` fast path vs `r_vbo 0` CPU path, stationary `r_dlightTest` light)
  show no shadow-region differences.

### Phase 7: Directional Shadows And CSM

- `[x]` Treat cascaded shadow maps as a separate directional-light feature.
- `[x]` Define cascade split policy, texel snapping, and stable light-space
  bounds.
- `[x]` Render sky-sun CSM atlases and receiver passes on GLx and Vulkan for
  opaque BSP world geometry, entity models, and brush models, sourced from
  parsed sky shader sun parms.
- `[x]` Share filtering, bias, visualization, and RenderDoc validation utilities
  with dlight shadows where practical.
- `[x]` Keep CSM cvars and defaults separate from dlight shadow cvars.

### Phase 8: Release Evidence

- `[x]` Add dlight shadow scenes to GLx and Vulkan runtime sweeps.
- `[x]` Capture screenshots and logs for world geometry, brush models, entities,
  alpha-tested surfaces, portals/mirrors, and stress-light budgets.
- `[x]` Require both renderers to pass build, shader, screenshot, and RenderDoc
  inspection checks before enabling real shadow maps by default.

## Testing Tools

`r_dlightTest <count> [intensity] [distance] [height] [seconds]` injects a ring
of colored point dynamic lights in front of the current camera. Use
`r_dlightTest off` to disable it. A `seconds` value of `0` keeps the test active
until disabled.

`scripts/dlight_shadow_test.py` writes a repeatable launch config and manifest
under `.tmp/dlight-shadow-tests/`:

```powershell
python scripts/dlight_shadow_test.py --renderer vulkan --dry-run
python scripts/dlight_shadow_test.py --renderer glx --exe .tmp\clean-rebuild\fnql.x64.exe --renderdoc
```

Use `--extra-set NAME=VALUE` for one-off renderer cvar experiments. Use
`--renderdoc-arg` when a local RenderDoc version needs additional capture
options before the executable path.

RenderDoc capture checkpoints:

- `[ ]` GLx: confirm the dlight shadow atlas FBO is depth-only, complete, and
  sized as reported by `r_speeds 4`.
- `[ ]` Vulkan: confirm the dlight shadow atlas image uses a depth format, has
  sampled and depth-attachment usage, and has a descriptor set.
- `[ ]` Confirm every planned dlight enters the GLx atlas FBO or Vulkan
  `dlight shadow atlas render pass`, preserves cached atlas depth, clears only
  uncached or invalid per-face tile scissors, preflights six cube faces, and
  renders only non-empty atlas tiles using the reported `render lights`,
  `faces`, `batches`, `draws`, `surfs`, fill, budget, and `lowvalue` counters
  from `r_dlightShadowDebug 1`.
- `[ ]` After Phase 5: confirm lighting passes bind the atlas, use the intended
  tile, and sample the expected filter kernel.

Default-enable release gate:

```powershell
python scripts/dlight_shadow_release_gate.py --print-template
python scripts/dlight_shadow_release_gate.py --evidence .tmp\dlight-shadow-release-gate.json --require-ready
```

The evidence manifest must name passed GLx and Vulkan builds, GLx and Vulkan
shader validation, non-dry-run `rc-parity`/`vk-modern` runtime sweep manifests
with dlight shadow screenshot/log coverage, and reviewed RenderDoc inspection
checks. Without `--require-ready`, the script still fails if source defaults
enable `r_dlightShadows` before the evidence is complete.

Build and script evidence:

- `[x]` `python tests\vulkan\vk_runtime_sweep_tests.py` passed after CSM
  fallback smoke runtime parsing and gate coverage on June 4, 2026.
- `[x]` `python tests\glx\glx_runtime_sweep_tests.py` passed after CSM
  fallback smoke runtime parsing and gate coverage on June 4, 2026.
- `[x]` `python tests\dlight_shadow_release_gate_tests.py` passed after CSM
  fallback smoke release-gate coverage on June 4, 2026.
- `[x]` `python tests\shadow_manager_source_tests.py` passed after CSM
  fallback diagnostics source-contract coverage on June 4, 2026.
- `[x]` `python tests\surfacelight_validation_docs_tests.py` passed after
  CSM fallback smoke artifact documentation on June 4, 2026.
- `[x]` `python tests\vulkan\vk_runtime_sweep_tests.py` passed after
  combined shadow atlas runtime smoke and gate coverage on June 4, 2026.
- `[x]` `python tests\glx\glx_runtime_sweep_tests.py` passed after
  combined shadow atlas runtime smoke and gate coverage on June 4, 2026.
- `[x]` `python tests\surfacelight_validation_docs_tests.py` passed after
  combined atlas smoke artifact documentation on June 4, 2026.
- `[x]` `python tests\dlight_shadow_release_gate_tests.py` passed after
  combined atlas smoke gate fixture coverage on June 4, 2026.
- `[x]` `python tests\csm_shadow_cache_source_tests.py` passed after
  combined atlas smoke runtime schedule coverage on June 4, 2026.
- `[x]` `python tests\shadow_manager_source_tests.py` passed after
  combined atlas smoke runtime schedule coverage on June 4, 2026.
- `[x]` `python tests\vulkan\vk_runtime_sweep_tests.py` passed after CSM
  shimmer screenshot diff smoke coverage on June 4, 2026.
- `[x]` `python tests\glx\glx_runtime_sweep_tests.py` passed after CSM
  shimmer screenshot diff smoke coverage on June 4, 2026.
- `[x]` `python tests\surfacelight_validation_docs_tests.py` passed after
  surfacelight validation artifact documentation on June 4, 2026.
- `[x]` `python tests\dlight_shadow_release_gate_tests.py` passed after
  surfacelight validation artifact documentation on June 4, 2026.
- `[x]` `git diff --check -- docs\fnql\DLIGHT_SHADOWMAP_ROADMAP.md
  docs-dev\plans\2026-06-03-vk-shadowmapping.md
  tests\surfacelight_validation_docs_tests.py` passed after surfacelight
  validation artifact documentation on June 4, 2026; Git reported only
  existing LF-to-CRLF working-copy warnings.
- `[x]` `python tests\csm_shadow_cache_source_tests.py` passed after CSM
  shimmer camera-path instrumentation on June 4, 2026.
- `[x]` `python tests\vulkan\vk_runtime_sweep_tests.py` passed after CSM
  shimmer camera-path instrumentation on June 4, 2026.
- `[x]` `python tests\glx\glx_runtime_sweep_tests.py` passed after CSM
  shimmer camera-path instrumentation on June 4, 2026.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed on May 22, 2026.
- `[x]` `python -m py_compile scripts\dlight_shadow_test.py` passed on
  May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --dry-run`
  passed on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --dry-run`
  passed on May 22, 2026.
- `[x]` `glslangValidator -S frag -V` passed for the Vulkan dlight lighting
  fragment template variants: base, fog, line, and line+fog on May 22, 2026.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after initial hard atlas sampling on
  May 22, 2026.
- `[x]` `python -m py_compile scripts\dlight_shadow_test.py` passed after
  initial hard atlas sampling on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --dry-run`
  passed after initial hard atlas sampling on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --dry-run`
  passed after initial hard atlas sampling on May 22, 2026.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after brush-model caster collection on
  May 22, 2026.
- `[x]` `python -m py_compile scripts\dlight_shadow_test.py` passed after
  brush-model caster collection on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --dry-run`
  passed after brush-model caster collection on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --dry-run`
  passed after brush-model caster collection on May 22, 2026.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after entity-model caster collection on
  May 22, 2026.
- `[x]` `python -m py_compile scripts\dlight_shadow_test.py` passed after
  entity-model caster collection on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --extra-set
  r_dlightMode=2 --dry-run` passed after entity-model caster collection on
  May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --extra-set
  r_dlightMode=2 --dry-run` passed after entity-model caster collection on
  May 22, 2026.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after per-face caster culling on
  May 22, 2026.
- `[x]` `python -m py_compile scripts\dlight_shadow_test.py` passed after
  per-face caster culling on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --extra-set
  r_dlightMode=2 --dry-run` passed after per-face caster culling on
  May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --extra-set
  r_dlightMode=2 --dry-run` passed after per-face caster culling on
  May 22, 2026.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after empty-face atlas skipping on
  May 22, 2026.
- `[x]` `python -m py_compile scripts\dlight_shadow_test.py` passed after
  empty-face atlas skipping on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --extra-set
  r_dlightMode=2 --dry-run` passed after empty-face atlas skipping on
  May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --extra-set
  r_dlightMode=2 --dry-run` passed after empty-face atlas skipping on
  May 22, 2026.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after slope/normal-aware atlas caster bias on
  May 22, 2026.
- `[x]` `python -m py_compile scripts\dlight_shadow_test.py` passed after
  slope/normal-aware atlas caster bias on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --extra-set
  r_dlightMode=2 --dry-run` passed after slope/normal-aware atlas caster bias
  on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --extra-set
  r_dlightMode=2 --dry-run` passed after slope/normal-aware atlas caster bias
  on May 22, 2026.
- `[x]` `git diff --check` passed after slope/normal-aware atlas caster bias
  on May 22, 2026; Git reported only existing LF-to-CRLF working-copy
  warnings.
- `[x]` `glslangValidator -S frag -V` passed for the Vulkan dlight lighting
  fragment template variants: base, fog, line, and line+fog after 2x2 PCF on
  May 22, 2026.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after 2x2 PCF on May 22, 2026.
- `[x]` `python -m py_compile scripts\dlight_shadow_test.py` passed after
  2x2 PCF on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --extra-set
  r_dlightMode=2 --dry-run` passed after 2x2 PCF on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --extra-set
  r_dlightMode=2 --dry-run` passed after 2x2 PCF on May 22, 2026.
- `[x]` `git diff --check` passed after 2x2 PCF on May 22, 2026; Git reported
  only existing LF-to-CRLF working-copy warnings.
- `[x]` `glslangValidator -S frag -V` passed for the Vulkan dlight lighting
  fragment template variants: base, fog, line, and line+fog after four-tap
  poisson-style PCF on May 22, 2026.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after four-tap poisson-style PCF on
  May 22, 2026.
- `[x]` `python -m py_compile scripts\dlight_shadow_test.py` passed after
  four-tap poisson-style PCF on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --extra-set
  r_dlightMode=2 --dry-run` passed after four-tap poisson-style PCF on
  May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --extra-set
  r_dlightMode=2 --dry-run` passed after four-tap poisson-style PCF on
  May 22, 2026.
- `[x]` `git diff --check` passed after four-tap poisson-style PCF on
  May 22, 2026; Git reported only existing LF-to-CRLF working-copy warnings.
- `[x]` `glslangValidator -S frag -V` passed for the Vulkan dlight lighting
  fragment template variants: base, fog, line, and line+fog after
  normal/slope and receiver-bias tuning on May 22, 2026.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after normal/slope and receiver-bias tuning
  on May 22, 2026.
- `[x]` `python -m py_compile scripts\dlight_shadow_test.py` passed after
  normal/slope and receiver-bias tuning on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --extra-set
  r_dlightMode=2 --dry-run` passed after normal/slope and receiver-bias tuning
  on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --extra-set
  r_dlightMode=2 --dry-run` passed after normal/slope and receiver-bias tuning
  on May 22, 2026.
- `[x]` `git diff --check` passed after normal/slope and receiver-bias tuning
  on May 22, 2026; Git reported only existing LF-to-CRLF working-copy warnings.
- `[x]` `glslangValidator -S frag -V` passed for the Vulkan dlight lighting
  fragment template variants: base, fog, line, and line+fog after selectable
  filtering on May 22, 2026.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after selectable filtering on May 22, 2026.
- `[x]` `python -m py_compile scripts\dlight_shadow_test.py` passed after
  selectable filtering on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=0 --dry-run` passed after
  selectable filtering on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=1 --dry-run` passed after
  selectable filtering on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=2 --dry-run` passed after
  selectable filtering on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=2 --dry-run` passed after
  selectable filtering on May 22, 2026.
- `[x]` `git diff --check` passed after selectable filtering on
  May 22, 2026; Git reported only existing LF-to-CRLF working-copy warnings.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after static-light shadow-map caching on
  May 22, 2026.
- `[x]` `python -m py_compile scripts\dlight_shadow_test.py` passed after
  static-light shadow-map caching on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=2 --dry-run` passed after
  static-light shadow-map caching on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=2 --dry-run` passed after
  static-light shadow-map caching on May 22, 2026.
- `[x]` `git diff --check` passed after static-light shadow-map caching on
  May 22, 2026; Git reported only existing LF-to-CRLF working-copy warnings.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after low-value dlight shadow throttling on
  May 22, 2026.
- `[x]` `python -m py_compile scripts\dlight_shadow_test.py` passed after
  low-value dlight shadow throttling on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=2 --extra-set
  r_dlightShadowMaxLights=1 --dry-run` passed after low-value dlight shadow
  throttling on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=2 --extra-set
  r_dlightShadowMaxLights=1 --dry-run` passed after low-value dlight shadow
  throttling on May 22, 2026.
- `[x]` `git diff --check` passed after low-value dlight shadow throttling on
  May 22, 2026; Git reported only existing LF-to-CRLF working-copy warnings.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after depth-only shadow caster batching on
  May 22, 2026.
- `[x]` `python -m py_compile scripts\dlight_shadow_test.py` passed after
  depth-only shadow caster batching on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=2 --dry-run` passed after
  depth-only shadow caster batching on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=2 --dry-run` passed after
  depth-only shadow caster batching on May 22, 2026.
- `[x]` `git diff --check` passed after depth-only shadow caster batching on
  May 22, 2026; Git reported only existing LF-to-CRLF working-copy warnings.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after shadow atlas telemetry on
  May 22, 2026.
- `[x]` `python -m py_compile scripts\dlight_shadow_test.py` passed after
  shadow atlas telemetry on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=2 --dry-run` passed after
  shadow atlas telemetry on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=2 --dry-run` passed after
  shadow atlas telemetry on May 22, 2026.
- `[x]` `git diff --check` passed after shadow atlas telemetry on
  May 22, 2026; Git reported only existing LF-to-CRLF working-copy warnings.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after directional CSM split-policy planning
  on May 22, 2026.
- `[x]` `python -m py_compile scripts\dlight_shadow_test.py` passed after
  directional CSM split-policy planning on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=2 --extra-set
  r_csmShadows=1 --extra-set r_csmDebug=1 --dry-run` passed after directional
  CSM split-policy planning on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=2 --extra-set
  r_csmShadows=1 --extra-set r_csmDebug=1 --dry-run` passed after directional
  CSM split-policy planning on May 22, 2026.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after shared shadow filter/bias utilities and
  separate CSM policy cvars on May 22, 2026.
- `[x]` `python -m py_compile scripts\dlight_shadow_test.py` passed after
  shared shadow filter/bias utilities and separate CSM policy cvars on
  May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=2 --extra-set
  r_csmShadows=1 --extra-set r_csmDebug=1 --extra-set r_csmShadowFilter=1
  --extra-set r_csmShadowBias=6 --extra-set r_csmCasterSlopeBias=2 --dry-run`
  passed after shared shadow filter/bias utilities and separate CSM policy
  cvars on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=2 --extra-set
  r_csmShadows=1 --extra-set r_csmDebug=1 --extra-set r_csmShadowFilter=1
  --extra-set r_csmShadowBias=6 --extra-set r_csmCasterSlopeBias=2 --dry-run`
  passed after shared shadow filter/bias utilities and separate CSM policy
  cvars on May 22, 2026.
- `[x]` `git diff --check` passed after shared shadow filter/bias utilities
  and separate CSM policy cvars on May 22, 2026; Git reported only existing
  LF-to-CRLF working-copy warnings.
- `[x]` `python -m py_compile scripts\vk_runtime_sweep.py
  scripts\glx_runtime_sweep.py` passed after adding dlight shadow scenes to the
  runtime sweeps on May 22, 2026.
- `[x]` `python tests\vulkan\vk_runtime_sweep_tests.py` passed after adding
  dlight shadow scenes to the Vulkan runtime sweep on May 22, 2026.
- `[x]` `python tests\glx\glx_runtime_sweep_tests.py` passed after adding
  dlight shadow scenes to the GLx runtime sweep on May 22, 2026.
- `[x]` `python scripts\vk_runtime_sweep.py --gate vk-modern --dry-run --exe
  .tmp\vk-gate-plans\fnql --basepath .tmp\vk-gate-plans\basepath
  --output-dir .tmp\vk-dlight-sweep-test` passed on May 22, 2026 and planned
  a dedicated Vulkan `dlight-shadow-scenes` run with `shadowScene`
  screenshots.
- `[x]` `python scripts\glx_runtime_sweep.py --gate rc-parity --dry-run --exe
  .tmp\glx-gate-plans\fnql --basepath .tmp\glx-gate-plans\basepath
  --output-dir .tmp\glx-dlight-sweep-test` passed on May 22, 2026 and planned
  a dedicated GLx `dlight-shadow-scenes` run with `shadowScene` screenshots.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after adding runtime sweep dlight shadow
  scenes on May 22, 2026; Ninja reported no work to do.
- `[x]` `git diff --check` passed after adding runtime sweep dlight shadow
  scenes on May 22, 2026; Git reported only existing LF-to-CRLF working-copy
  warnings.
- `[x]` `python -m py_compile scripts\vk_runtime_sweep.py
  scripts\glx_runtime_sweep.py` passed after adding dlight shadow screenshot
  and log evidence categories on May 22, 2026.
- `[x]` `python tests\vulkan\vk_runtime_sweep_tests.py` passed after requiring
  dlight shadow evidence categories and scene-marked log samples in the Vulkan
  runtime gate on May 22, 2026.
- `[x]` `python tests\glx\glx_runtime_sweep_tests.py` passed after requiring
  dlight shadow evidence categories and scene-marked log samples in the GLx
  runtime gate on May 22, 2026.
- `[x]` `python scripts\vk_runtime_sweep.py --gate vk-modern --dry-run --exe
  .tmp\vk-gate-plans\fnql --basepath .tmp\vk-gate-plans\basepath
  --output-dir .tmp\vk-dlight-evidence-test` passed on May 22, 2026 and
  planned six Vulkan dlight shadow evidence screenshots covering world
  geometry, brush models, entities, alpha-tested surfaces, portals/mirrors,
  and stress-light budgets.
- `[x]` `python scripts\glx_runtime_sweep.py --gate rc-parity --dry-run --exe
  .tmp\glx-gate-plans\fnql --basepath .tmp\glx-gate-plans\basepath
  --output-dir .tmp\glx-dlight-evidence-test` passed on May 22, 2026 and
  planned six GLx dlight shadow evidence screenshots covering world geometry,
  brush models, entities, alpha-tested surfaces, portals/mirrors, and
  stress-light budgets.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after adding dlight shadow screenshot/log
  evidence categories on May 22, 2026; Ninja reported no work to do.
- `[x]` `git diff --check` passed after adding dlight shadow screenshot/log
  evidence categories on May 22, 2026; Git reported only existing LF-to-CRLF
  working-copy warnings.
- `[x]` `python -m py_compile scripts\dlight_shadow_release_gate.py` passed
  after adding the dlight shadow default-enable release gate on May 22, 2026.
- `[x]` `python tests\dlight_shadow_release_gate_tests.py` passed after adding
  the dlight shadow default-enable release gate on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_release_gate.py --print-template` passed
  after adding the dlight shadow default-enable release gate on May 22, 2026.
- `[x]` `python tests\vulkan\vk_runtime_sweep_tests.py` and
  `python tests\glx\glx_runtime_sweep_tests.py` passed after adding the
  dlight shadow default-enable release gate on May 22, 2026.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after adding the dlight shadow
  default-enable release gate on May 22, 2026; Ninja reported no work to do.
- `[x]` `git diff --check` passed after adding the dlight shadow
  default-enable release gate on May 22, 2026; Git reported only existing
  LF-to-CRLF working-copy warnings.
- `[x]` `python tests\dlight_shadow_bias_tests.py` passed after
  contact-preserving dlight shadow bias revision on May 22, 2026.
- `[x]` `glslangValidator -S frag -V` passed for the Vulkan dlight lighting
  fragment template variants: base, fog, line, and line+fog after
  contact-preserving dlight shadow bias revision on May 22, 2026.
- `[x]` `python -m py_compile scripts\dlight_shadow_test.py` passed after
  contact-preserving dlight shadow bias revision on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=2 --dry-run` passed after
  contact-preserving dlight shadow bias revision on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=2 --dry-run` passed after
  contact-preserving dlight shadow bias revision on May 22, 2026.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after contact-preserving dlight shadow bias
  revision on May 22, 2026.
- `[x]` `git diff --check` passed after contact-preserving dlight shadow bias
  revision on May 22, 2026; Git reported only existing LF-to-CRLF
  working-copy warnings.
- `[x]` `python tests\dlight_shadow_bias_tests.py` passed after the GLx
  ARB-safe texel-scaled receiver-bias simplification on May 22, 2026.
- `[x]` `python -m py_compile scripts\dlight_shadow_test.py` passed after the
  GLx ARB-safe texel-scaled receiver-bias simplification on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=2 --dry-run` passed after
  the GLx ARB-safe texel-scaled receiver-bias simplification on
  May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=2 --dry-run` passed after
  the GLx ARB-safe texel-scaled receiver-bias simplification on
  May 22, 2026.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after the GLx ARB-safe texel-scaled
  receiver-bias simplification on May 22, 2026.
- `[x]` `git diff --check` passed after the GLx ARB-safe texel-scaled
  receiver-bias simplification on May 22, 2026; Git reported only existing
  LF-to-CRLF working-copy warnings.
- `[x]` `python tests\dlight_shadow_bias_tests.py` passed after the GLx ARB
  shadow-program scalar swizzle compatibility fix on May 22, 2026.
- `[x]` `python -m py_compile scripts\dlight_shadow_test.py` passed after the
  GLx ARB shadow-program scalar swizzle compatibility fix on May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer glx --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=2 --dry-run` passed after
  the GLx ARB shadow-program scalar swizzle compatibility fix on
  May 22, 2026.
- `[x]` `python scripts\dlight_shadow_test.py --renderer vulkan --extra-set
  r_dlightMode=2 --extra-set r_dlightShadowFilter=2 --dry-run` passed after
  the GLx ARB shadow-program scalar swizzle compatibility fix on
  May 22, 2026.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after the GLx ARB shadow-program scalar
  swizzle compatibility fix on May 22, 2026.
- `[x]` `meson compile -C meson\build fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after the GLx ARB shadow-program scalar
  swizzle compatibility fix on May 22, 2026.
- `[x]` A non-dry-run GLx renderer-init smoke launch against the local Q3Test
  assets no longer logs `FP Compile Error` or `WARNING: ARB dynamic light
  shadow programs failed` after the GLx ARB shadow-program scalar swizzle
  compatibility fix on May 22, 2026. The launch still stops at the known
  Q3Test 1.09 UI VM version mismatch before gameplay evidence can be captured.
- `[x]` `git diff --check` passed after the GLx ARB shadow-program scalar
  swizzle compatibility fix on May 22, 2026; Git reported only existing
  LF-to-CRLF working-copy warnings.
- `[x]` `python tests\dlight_shadow_bias_tests.py` passed after the Vulkan CSM
  caster-bias direction fix on June 4, 2026.
- `[x]` `python tests\shadow_manager_source_tests.py` passed after the Vulkan
  CSM caster-bias direction fix on June 4, 2026.
- `[x]` `python tests\vulkan\vk_runtime_sweep_tests.py` passed after the
  Vulkan CSM caster-bias direction fix on June 4, 2026.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_vulkan_x86_64` passed
  after the Vulkan CSM caster-bias direction fix on June 4, 2026.
- `[x]` `python tests\shadow_projection_source_tests.py` passed after the
  Vulkan CSM producer/receiver projection contract fix on June 5, 2026.
- `[x]` `python tests\csm_plan_source_tests.py` passed after the Vulkan CSM
  producer/receiver projection contract fix on June 5, 2026.
- `[x]` `python tests\dlight_shadow_bias_tests.py` passed after the Vulkan CSM
  producer/receiver projection contract fix on June 5, 2026.
- `[x]` `python tests\shadow_manager_source_tests.py` passed after the Vulkan
  CSM producer/receiver projection contract fix on June 5, 2026.
- `[x]` `python tests\vulkan\vk_runtime_sweep_tests.py` passed after the
  Vulkan CSM producer/receiver projection contract fix on June 5, 2026.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_vulkan_x86_64` passed
  after the Vulkan CSM producer/receiver projection contract fix on June 5,
  2026.
- `[x]` `python tests\csm_plan_source_tests.py` passed after the CSM
  world-caster projection/stability fix on June 5, 2026.
- `[x]` `python tests\csm_shadow_cache_source_tests.py` passed after the CSM
  world-caster projection/stability fix on June 5, 2026.
- `[x]` `python tests\shadow_projection_source_tests.py` passed after the CSM
  world-caster projection/stability fix on June 5, 2026.
- `[x]` `python tests\vulkan\vk_runtime_sweep_tests.py` passed after the CSM
  world-caster projection/stability fix on June 5, 2026.
- `[x]` `python tests\glx\glx_runtime_sweep_tests.py` passed after the CSM
  world-caster projection/stability fix on June 5, 2026.
- `[x]` `python tests\dlight_shadow_bias_tests.py` passed after the CSM
  world-caster projection/stability fix on June 5, 2026.
- `[x]` `python tests\shadow_manager_source_tests.py` passed after the CSM
  world-caster projection/stability fix on June 5, 2026.
- `[x]` `python tests\personal_shadow_source_tests.py` passed after the CSM
  world-caster projection/stability fix on June 5, 2026.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after the CSM world-caster
  projection/stability fix on June 5, 2026.
- `[x]` `python tests\dlight_shadow_bias_tests.py` passed after the Vulkan CSM
  sorted traversal parity fix on June 5, 2026.
- `[x]` `python tests\csm_shadow_cache_source_tests.py` passed after the
  Vulkan CSM sorted traversal parity fix on June 5, 2026.
- `[x]` `python tests\shadow_manager_source_tests.py` passed after the Vulkan
  CSM sorted traversal parity fix on June 5, 2026.
- `[x]` `python tests\vulkan\vk_runtime_sweep_tests.py` passed after the
  Vulkan CSM sorted traversal parity fix on June 5, 2026.
- `[x]` `python tests\glx\glx_runtime_sweep_tests.py` passed after the Vulkan
  CSM sorted traversal parity fix on June 5, 2026.
- `[x]` `python tests\csm_plan_source_tests.py` passed after the Vulkan CSM
  sorted traversal parity fix on June 5, 2026.
- `[x]` `python tests\shadow_projection_source_tests.py` passed after the
  Vulkan CSM sorted traversal parity fix on June 5, 2026.
- `[x]` `meson compile -C .tmp\meson-dlight fnql_glx_x86_64
  fnql_vulkan_x86_64` passed after the Vulkan CSM sorted traversal parity
  fix on June 5, 2026.
- `[x]` `meson test -C .tmp\meson-glx-verification-local --print-errorlogs`
  (11 tests), `python tests\dlight_shadow_bias_tests.py`,
  `python tests\glx\glx_runtime_sweep_tests.py`,
  `python tests\shadow_projection_source_tests.py`,
  `python tests\personal_shadow_source_tests.py`, and
  `meson compile -C .tmp\meson-glx-verification-local fnql_glx_x86_64
  fnql_opengl_x86_64` passed after the VBO shadow-caster fast path on
  June 11, 2026, with live q3dm0 perf/parity runs recorded in Phase 6.

## Maintenance Rule

Every shadow-map implementation step must update this document's current
snapshot, checklists, next milestone, and testing notes in the same change. If a
step intentionally defers part of the roadmap, record the deferral here instead
of leaving it only in chat or commit notes.
