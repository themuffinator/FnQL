# GLx Visual Dossier

The GLx visual dossier is the human-review layer for runtime proof artifacts.
`scripts/glx_runtime_sweep.py` writes `glx-visual-dossier.md` beside every
`manifest.json`, including dry-run gate plans. The manifest records the dossier
path under `visualDossier`.

The dossier does not replace machine gates. It gives maintainers a compact way
to review the visual evidence that the gates already require: screenshot
captures, histogram sidecars, luma false-color sidecars, exposure false-color
sidecars, baseline comparisons, renderer-switch lifecycle state, output backend
state, color-contract metadata, world/static/lightmap/fog proof state, material
proof state, dynamic-scene proof state, postprocess greyscale/render-scale proof
state, and product-tier evidence.

## Sections

Each dossier contains:

- current and target GLx pipeline flowcharts;
- a renderer-switch lifecycle table with status, command, restart mode,
  `CL_Vid_Restart` path, planned/completed transitions, GLx entry/exit counts,
  diagnostics state, and performance sample count;
- a world-proof table for `rc-parity`, `rc-proof`, and `rc-stress` runs with
  required maps, found GLx screenshot maps, static-world draw/index/fallback
  counters, and lightmap/fog proof status;
- a material-proof table for `rc-proof` and `rc-stress` runs with required
  maps, renderer/program counters, material parameter-block fingerprints, and
  stream-material feature counters;
- a dynamic-proof table for `rc-proof` and `rc-stress` runs with required maps
  and demos, entity/particle/poly/mark/weapon/beam category counters,
  shadow/beam/dynamic-light stream counters, and fallback state;
- a post-proof table for `rc-proof` and `rc-stress` runs with required maps,
  FBO readiness, postprocess frames, screenshot frames, greyscale evidence,
  render-scale evidence, target-dimension evidence, output color-contract state,
  and minimized-output state;
- an ownership-proof table for `glx-ownership` runs with legacy delegation,
  product tiers, modern-tier diagnostics, post/output node and transform counts,
  and fallback state;
- a backend/state overlay table with scene color space, transfer, selected
  backend, HDR/headroom state, exposure, sRGB decode, framebuffer-sRGB,
  target-format, final-encode, and contract validity;
- a driver-tier matrix for `GL12`, `GL2X`, `GL3X`, `GL41`, and `GL46` showing
  observed tier evidence and modern post/output ownership signals;
- histogram, luma false-color, and exposure false-color evidence for every
  screenshot;
- a parity diff sheet with baseline, candidate, diff, RMS, PSNR, SSIM, and
  changed-pixel fields when baseline comparisons are active;
- an SDR/HDR color-sweep review table when the run includes the P0 color matrix;
- a short review checklist for visual sign-off.

## Review Rules

- Treat the dossier as an index into proof artifacts, not as proof by itself.
- Use the renderer-switch lifecycle table to confirm that the manifest matched
  every expected map/round/step/renderer transition before reviewing visual
  parity screenshots.
- Use the world-proof table on blocking RC artifacts to confirm that every
  required stock/high-geometry/lightmap/fog/visibility map has GLx screenshot
  and histogram evidence, and that static-world counters did not report packet
  misses, fallbacks, or errors.
- Use the material-proof table on hard RC artifacts to confirm that material
  renderer readiness, parameter fingerprints, tcgen/stage-flag corpus coverage,
  stream-material counters, and guarded high-risk screen/video material state
  are present with no unsafe fallback or failure counters.
- Use the dynamic-proof table on hard RC artifacts to confirm that required
  entity, weapon, particle, mark/decal, beam, dynamic-light stream/ownership,
  and shadow evidence is present with no stream or category fallbacks.
- Use the post-proof table on hard RC artifacts to confirm that greyscale and
  render-scale tagged maps have found GLx screenshots and histograms, the FBO is
  ready with no init failures, postprocess frames and screenshot frames are
  positive, render/capture dimensions prove the requested scale, minimized
  output is absent, and the output color contract remains valid.
- Use the ownership-proof table on `glx-ownership` artifacts to confirm that
  zero legacy delegation, executable GLx-owned post/output counts, and
  post/output fingerprints were captured before treating the artifact as
  promotion evidence.
- Use the backend/state overlay to confirm that `r_outputBackend`, scene color
  space, transfer, framebuffer-sRGB, and final-encode state match the intended
  run.
- Use the histogram table before visual inspection. Mid-gray placement and
  clipping percentages catch SDR/HDR mistakes that can look subtle in a single
  screenshot.
- Use luma false-color sidecars to inspect crushed shadows, clipped highlights,
  and unexpectedly flat tone mapping.
- Use exposure false-color sidecars to inspect stop-scale drift across exposure,
  tone-map, and HDR-output rows.
- Use parity diffs to distinguish acceptable threshold noise from structured
  image changes.
- Treat modern-tier promotion evidence as incomplete unless the tier matrix
  shows executable GLx-owned post/output products and no modern-tier fallback
  mask. Disabled, unbound, or implementation-not-ready post/output products are
  useful review evidence, not promotion evidence.

## Packaging

The verification workflow uploads generated dossiers with the GLx gate-plan and
runtime-sweep artifacts. Release packages include this document so downstream
maintainers can interpret archived proof roots without needing the source tree.
