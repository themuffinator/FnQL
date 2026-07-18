# GLx Release Candidate Gates

## Status

This document preserves the initial GLx release-candidate target and its runtime evidence requirements. GLx is now the canonical OpenGL-lineage renderer and the default; the gates remain useful regression coverage after consolidation. Tagged release packaging continues to require a GLx proof root containing passing non-dry-run manifests for every blocking platform and release gate.

The gates are intentionally conservative. They prove that GLx can load through the existing renderer ABI, survive renderer switching and `vid_restart`-equivalent paths, preserve the current OpenGL display surface, and remain comparable with the independent `vk` backend. The retired pre-consolidation selectors are deliberately not accepted as aliases.

The final renderer replacement contract is [GLX_FINAL_CONTRACT.md](GLX_FINAL_CONTRACT.md). The gates in this document validate the transitional RC surface; they do not satisfy the final five-tier, GLx-owned draw, scene-linear color, and full feature-closure requirements by themselves.

The current feature-closure status is tracked in [GLX_FEATURE_MATRIX.md](GLX_FEATURE_MATRIX.md). A clean RC gate does not override rows that remain `partially covered` or `missing` in that matrix. The official screenshot and timedemo scene list is the [GLx proof corpus](GLX_PROOF_CORPUS.md); gate manifests, Markdown summaries, performance baselines, CI gate-plan artifacts, and release manifests all reference that same corpus version. The same corpus document also owns the named screenshot, demo-playback, HUD, shadow, bloom, cel-shading, greyscale, and render-scale parity suites required by `rc-proof`.

## Blocking Runtime Matrix

The first GLx RC requires runtime evidence on:

- Windows 10 or newer, x64, dynamic renderer build, retail `baseq3` assets.
- Linux x86_64, Mesa or vendor OpenGL driver, dynamic renderer build, retail `baseq3` assets.

Every blocking RC run must expose at least the `GL2X` product tier: OpenGL 2.x with GLSL-era program support. `GL12` exists as the final fixed-function compatibility floor, but the current conservative RC profile still exercises the programmable migration surface. `GL3X`, `GL41`, and `GL46` features are optional accelerators for this RC gate. Missing persistent mapping, sync objects, multidraw, indirect draw, direct-state-access, or debug-output support must select a fallback path rather than fail renderer initialization. A `GL2X` run must report the `GL2X programmable executor` contract: stream uploads, the GLSL material compiler, postprocess-lite behavior, common material coverage, dynamic entities, lightmaps, multitexture, fog, sprites, beams, screenshots, and demos are supported, while modern post-chain and scene-linear output are not required.

`GL12` is not a blocking RC profile target, but its diagnostics are still structured. A GL12 run must report the `GL12 fixed-function executor` contract, client-memory draw support, and the fixed-function coverage line for lightmaps, multitexture, fog, sprites, beams, dynamic lights, stencil shadows when available, screenshots, and demos. It must also report stream uploads, the GLSL material compiler, and the modern post chain as unavailable on that tier.

`GL3X` is likewise structured even though it is an accelerator for the conservative RC gate. A GL3X run must report the `GL3X performance executor` contract: FBO postprocess, UBO-style frame/object constants, timer queries, sync-aware uploads, static buffer ownership, dynamic buffer ownership, modern post-chain, scene-linear output, screenshots, and demos are supported, while persistent mapped uploads, indirect submission requirements, and direct state access requirements are not mandatory on that tier.

`GL41` runs must report the `GL41 mac-modern executor` contract. That line proves the macOS ceiling tier is treated as a supported modern product target, with FBO postprocess, UBO-style constants, timer queries, sync-aware uploads, static/dynamic buffer ownership, scene-linear post, high-quality SDR, optional hardware HDR output, screenshots, and demos. The paired GL4+ requirements line must keep debug output, buffer storage, direct state access, multi-draw indirect, and persistent uploads marked as non-required.

`GL46` runs must report the `GL46 high-end executor` contract. That line proves persistent uploads, buffer-storage upload policy, sync-heavy streaming, DSA, MDI, aggressive static-world submission, detailed GPU counters, hardware HDR output, screenshots, and demos are all part of the high-end tier. The compact `glx: GL46 high-end ...` line records persistent-upload, DSA-product, MDI-product, aggressive-static, backend GPU query, and static-world MDI counters so the tier can be compared against lower paths.

Manual release packaging builds GLx wherever the repository enables `USE_GLX`, including Windows x86, macOS, Linux aarch64, and other packaged targets. Those platforms need at least manual smoke coverage even though they are not blockers for the first conservative RC unless maintainers add stable GPU runners for them.

## Canonical Gate Presets

`scripts/glx_runtime_sweep.py` owns the machine-readable gate presets and corpus scene selections. Use `--list-gates` and `--list-corpus` to print the current script view.

| Gate | Purpose | Profile | Scene Set | Automated Floor |
|---|---|---|---|---|
| `rc-smoke` | Renderer lifecycle smoke: module load, map load, repeated in-process renderer switches, screenshots, and GLx diagnostics. | `baseline` | `stock-q3dm1-hud` | All runs pass, all expected screenshots are written, the manifest records passing versioned `rendererSwitchEvidence`, and the manifest references the current corpus version. |
| `rc-parity` | Blocking conservative RC gate for world, ordered packet-batch static spans, stream paths including CPU-computed texmods, environment coordinates, state-only dynamic-scene draw arrays, material, bloom, and GPU-timing paths. | `glx-parity` / `r_glxProfile rc` | `stock-q3dm1-hud`, `stock-q3dm17-open`, `timedemo-demo1` | All runs pass, all screenshots are written, the versioned renderer-switch lifecycle records a `vk -> glx -> vk -> glx` round trip, versioned `worldProofEvidence` proves stock-map/lightmap GLx screenshots plus static-world counters with no packet misses or fallbacks, GLx timedemo FPS is at least 90% of `vk`, and the selected corpus covers stock maps, screenshots, HUD, bloom, demo playback, and performance comparison tags. |
| `rc-proof` | Blocking proof gate for the RC surface, requiring reviewed screenshot baselines, an approved performance baseline, and the full named parity-suite set in addition to the `rc-parity` checks. | `glx-parity` / `r_glxProfile rc` | `stock-q3dm1-hud`, `stock-q3dm17-open`, `stock-q3dm6-geometry`, `stock-q3dm11-shader`, `stock-q3dm15-fog`, `timedemo-demo1` | All parity checks pass, every screenshot compares within threshold, the renderer-switch lifecycle proves every expected map/round/step transition, `worldProofEvidence` covers stock/high-geometry/lightmap/fog/visibility maps, `materialProofEvidence` covers material-stage and tcgen runtime telemetry, `dynamicProofEvidence` covers entity/weapon categories, dynamic-light stream/ownership evidence, and shadow proof tags, `postProofEvidence` covers greyscale and render-scale postprocess proof, aggregate performance counters stay within the approved baseline growth budget, and the selected corpus carries the `screenshot`, `demo-playback`, `hud`, `shadow`, `bloom`, `cel-shading`, `greyscale`, and `render-scale` suite records. |
| `rc-stress` | Developer stress gate for compact static-world MDI command uploads and staged modern-map content before aggressive paths become defaults. | `glx-stress` / `r_glxProfile stress` | `rc-proof` set plus `modern-fnqlglx-heavy01`, `modern-fnqlglx-shader01`, `modern-fnqlglx-fog01`, `timedemo-fnqlglx-particles01` | All runs pass, screenshots are written, renderer-switch lifecycle evidence proves GLx entry and diagnostics, `worldProofEvidence` covers the stock and modern static/fog scene set, `materialProofEvidence` includes staged animated-image, vector-tcgen, screen-map, and video-map material tags, `dynamicProofEvidence` includes staged particle/poly/mark/beam category counters, `postProofEvidence` includes greyscale and render-scale counters/dimensions, timedemo metrics are captured, and the selected corpus covers modern-map, high-geometry, shader-heavy, fog-heavy, particle-heavy-demo, UI/HUD, performance tags, and the full named parity-suite set. |

The `retail-baseq3` subset is deliberately stock-data friendly. The `glx-proof-corpus` subset is reserved for staged project stress maps and demos; `rc-stress` uses those stable scene IDs so modern-map evidence is comparable across CI plans, local GPU runs, and release artifacts.

The named parity suites are gate data, not prose. `rc-proof` requires the versioned `screenshot`, `demo-playback`, `hud`, `shadow`, `bloom`, `cel-shading`, `greyscale`, and `render-scale` suite records in its manifest. The sweep verifies that every selected suite's absolute scene list is present in the gate scene set, that the selected tags match the suite requirements, and that suite cvar overrides such as `cg_shadows=2`, `r_celShading=1`, `r_greyscale=1`, or `r_renderScale=1` are written into the generated screenshot config before the relevant map loads.

The profile names are not just documentation labels. `glx-parity` and `glx-ownership` launch GLx with `r_glxProfile rc`, and `glx-stress` launches GLx with `r_glxProfile stress`, so startup-sensitive resources are built under the same profile that `glxprofile status` reports in the renderer. `glx-ownership` uses the RC cvar surface plus `r_glxRequireOwnership 1`, rejecting legacy-delegation draw submissions and turning any attempted delegation into a blocking diagnostic failure.

## Frozen RC Profile

The conservative RC profile is frozen as a cvar contract between the runtime renderer and the sweep harness. `code/rendererglx/glx_module.cpp` owns the `r_glxProfile rc` table, while `scripts/glx_runtime_sweep.py` owns the `glx-parity` launch profile. `tests/glx/glx_runtime_sweep_tests.py` parses the runtime table and fails if the script profile drifts from it.

Use `python scripts/glx_runtime_sweep.py --list-profiles` to print the exact profile values used by sweeps. The RC profile enables the compatibility-first GLx world renderer, GLx static arenas, GLx static device/soft draw dispatch, packet-batch static spans, same-state static multidraw, capability-gated high-end indirect buffer/single-draw/MDI span submission, guarded stream draw, the GL3X+ dynamic-light `auto` stream gate, projected-dlight scissor `auto`, material renderer/precache, final-pass bloom parity, GPU timing, and the state-only shadow/beam/postprocess dynamic draw-array submissions. The focused `glx-material` profile also enables material precache and runs the HUD plus shader-heavy stock scenes so shader compile-cache stress evidence is collected even outside the full RC profile. It intentionally keeps its isolated dynamic-light/screen-map/video-map material-stream overrides and compact per-batch MDI command uploads off; screen-map and video-map streams stay in the stress profile or explicit developer overrides until their gates have evidence.

## Exit Criteria

A GLx RC candidate must meet all of these conditions:

- `rc-smoke`, `rc-parity`, and `rc-proof` pass on every blocking runtime platform.
- The generated manifest, logs, screenshots, screenshot diffs, Markdown summary, timedemo metrics, and performance comparisons are archived with the candidate build.
- The generated manifest and any performance-baseline JSON identify the same GLx proof corpus version, parity suite version, selected scene IDs, and selected parity suite IDs used by the gate.
- The generated manifest contains `rendererSwitchEvidence` with the current lifecycle schema version, a `renderer_switch` command record, the `fast`/keep-window `CL_Vid_Restart` path, every expected map/round/step transition, at least one transition into GLx, and a round trip back out of GLx for blocking gates.
- The generated `rc-parity`, `rc-proof`, and `rc-stress` manifests contain `worldProofEvidence` with the current schema version, required world tags from the selected corpus, found GLx screenshots and histogram metadata for each required map, static-world draw/index counters, zero static packet misses/fallbacks/errors, lightmap evidence, and fog-heavy evidence whenever fog/visibility tags are required.
- The generated `rc-proof` and `rc-stress` manifests contain `materialProofEvidence` with the current schema version, required material/tcgen/stage-flag tags from the selected corpus, found GLx screenshots and histogram metadata for each required material map, material renderer readiness, compile/program activity, zero compile/link/precache/bind failures, zero unsupported compiler plans, non-zero parameter-block fingerprints, and the required stream-material feature plus screen/video guard evidence.
- The generated `rc-proof` and `rc-stress` manifests contain `dynamicProofEvidence` with the current schema version, required dynamic-scene tags from the selected corpus, found GLx screenshots or timedemo metrics for the required dynamic scenes, positive required stream-category and stream-feature counters including dynamic lights, required tier-support evidence, render-IR dlight ownership evidence, projected-dlight scissor active/computed/applied evidence, and zero stream/category fallbacks.
- The generated `rc-proof` and `rc-stress` manifests contain `postProofEvidence` with the current schema version, required greyscale/render-scale tags from the selected corpus, found GLx screenshots and histogram metadata for each required postprocess map, ready FBO evidence, zero FBO failures, positive postprocess frame/screenshot counters, greyscale control/frame evidence, render-scale control/frame evidence, render/capture dimensions proving a scaled output, non-minimized output, and a valid output color contract.
- The GLx timedemo result is at least 90% of the `vk` comparison result for each required demo on the same machine. A lower result needs a tracked waiver with the measured cause.
- Manual screenshot review finds no unexplained drift in world visibility, sky, fog, lightmaps, weapon placement, marks/decals, particles, HUD/2D, shadows, cel-shading/outline, bloom, gamma, or final output size.
- GLx diagnostic output shows no shader compile/link failures, material path `not-ready` state, GL errors, postprocess fallback output, streamed dynamic-scene fallback growth, or unexpected loss of the static-world/stream fallback guarantees.
- `renderer_switch vk,glx,vk,glx` loops do not leak state, fail screenshot capture, lose the cgame/UI, or leave the next renderer in a partially initialized state.
- `rc-stress` is clean before compact static-world MDI command uploads or other advanced GLx paths are promoted to default behavior.

Failing any blocking criterion keeps GLx out of default-renderer promotion. The fix may be renderer code, a narrower default GLx profile, a documented waiver, or a larger test corpus, but it should not be a silent default promotion. The stricter promotion decision is tracked by [GLX_PROMOTION.md](GLX_PROMOTION.md) and `python scripts/glx_promotion.py --require-ready --proof-root <dir>`.

## Typical Commands

From the repository root:

```sh
python scripts/glx_runtime_sweep.py --list-gates
python scripts/glx_runtime_sweep.py --list-profiles
python scripts/glx_runtime_sweep.py --list-corpus
python scripts/glx_runtime_sweep.py --gate rc-smoke --exe path/to/fnql.x64.exe --basepath path/to/game/root
python scripts/glx_runtime_sweep.py --gate rc-parity --exe path/to/fnql.x64.exe --basepath path/to/game/root
python scripts/glx_runtime_sweep.py --gate rc-proof --exe path/to/fnql.x64.exe --basepath path/to/game/root --proof-dir .tmp/glx-proof/windows-x64
python scripts/glx_runtime_sweep.py --gate rc-stress --exe path/to/fnql.x64.exe --basepath path/to/game/root
```

Use `--dry-run` to generate the configs and manifest without requiring a built executable or retail assets. Dry runs are useful for reviewing the expanded cvars, startup cvars, corpus scene IDs, maps, demos, and commands, but they do not count as gate evidence.

## Screenshot Baselines

The sweep can compare captured PNG screenshots against an approved baseline directory without external Python packages. Baseline filenames use stable screenshot keys derived from the profile, map, switch round, switch step, and renderer, while the live capture filenames keep the unique run id.

To deliberately approve a new local baseline set:

```sh
python scripts/glx_runtime_sweep.py --gate rc-parity --exe path/to/fnql.x64.exe --basepath path/to/game/root --screenshot-baseline-dir .tmp/glx-baselines/windows-x64 --approve-screenshot-baselines
```

To compare a candidate run against that baseline and write difference PNGs:

```sh
python scripts/glx_runtime_sweep.py --gate rc-parity --exe path/to/fnql.x64.exe --basepath path/to/game/root --screenshot-baseline-dir .tmp/glx-baselines/windows-x64 --screenshot-diff-dir .tmp/glx-diffs/windows-x64 --screenshot-max-rms 2.0 --screenshot-max-pixel-ratio 0.005
```

The initial thresholds are intentionally tight and should be adjusted only with reviewed evidence. Missing baselines or failed comparisons fail a non-dry-run gate when a baseline directory is supplied.

For hard RC proof, prefer `--gate rc-proof --proof-dir <dir>`. The proof directory defaults screenshot baselines to `<dir>/screenshots`, performance baselines to `<dir>/performance-baseline.json`, screenshot diffs to the run artifact directory, and the Markdown summary to the run artifact directory. `rc-proof` rejects baseline-approval mode before launching runtime work; approve refreshed visual and performance baselines in a separate reviewed `rc-parity` run before using them as proof inputs.

## Renderer Switch Lifecycle Evidence

Every named non-dry-run RC gate records a versioned `rendererSwitchEvidence`
object in the sweep manifest. The evidence is derived from the generated
switch-screenshot run instead of hand-authored metadata: it expands the selected
maps, `--switch-rounds`, and `--switch-sequence`, then checks that each expected
map/round/step/renderer tuple produced a found screenshot.

The blocking gates require that evidence to pass before other visual proof can
count. The record must show the `renderer_switch` command path, a
vid-restart-equivalent restart mode, both legacy and GLx renderer legs, at
least one transition into GLx, a transition back out of GLx for
`rc-smoke`/`rc-parity`/`rc-proof`, GLx diagnostics collected after a GLx leg,
and at least one GLx performance sample when performance samples are required.
Dry-run gate plans write the same object with `status: planned`, but dry-run
evidence remains planning output only.

## World Proof Evidence

`rc-parity`, `rc-proof`, and `rc-stress` also write a versioned
`worldProofEvidence` object. The sweep derives this object from the selected
proof-corpus scene IDs, found GLx screenshots, GLx diagnostics, and `r_speeds 7`
performance counters. It is intentionally stricter than a screenshot count:
required world tags expand to required maps, each required map must have a found
GLx screenshot with histogram metadata, and the static-world path must report
enabled/ready draw evidence without packet misses, fallbacks, GL errors, static
failures, or static MDI errors.

The `rc-parity` requirement covers the conservative stock-map/lightmap floor:
`q3dm1` plus `q3dm17` screenshots and static-world evidence. The `rc-proof`
requirement expands that contract to high-geometry, fog-heavy, and visibility
coverage through `q3dm6` and `q3dm15`. `rc-stress` keeps the same proof shape
for the staged modern-map stress scenes. Lightmap proof can come from tier
support diagnostics or material parameter-block evidence; fog proof can come
from tier support diagnostics or fog stream-draw counters, but fog-heavy maps
must still have screenshot parity evidence.

## Material Proof Evidence

`rc-proof` and `rc-stress` write a versioned `materialProofEvidence` object.
The sweep derives it from the selected proof-corpus material tags, found GLx
screenshots, `glxmaterial` diagnostics, stream-material gate diagnostics, and
`r_speeds 7` material counters. This object is the blocking proof that the
shader-stage surface is not only compiled in logic tests, but also active in a
runtime proof manifest.

For `rc-proof`, the required tags cover the shader-heavy/material-stage scene
plus lightmap, environment, and fog tcgen proof tags. The evidence requires the
material renderer to be enabled and ready, material compile/program activity to
be present, compile/link/precache/bind failure counters to stay at zero,
unsupported compiler plans and material fallbacks to stay at zero, material
parameter blocks to carry a non-zero fingerprint, and the RC stream-material
features (`multitexture`, `depthFragment`, `texMod`, and `environment`) to have
draw or gate-acceptance evidence. Screen-map and video-map stream guards must
also be present, but those features remain forbidden in the RC proof surface and
continue to fail the diagnostic gate if any accepted draw evidence appears
there. Dynamic-light stream evidence now belongs to `dynamicProofEvidence`,
where it must carry render-IR dlight role/pass ownership diagnostics.

For `rc-stress`, the material proof expands to the staged
`modern-fnqlglx-shader01` material surface. That gate requires the
`animated-image`, `screen-map`, and `video-map` tags, non-zero stage-flag
evidence for `animatedImage`, `screenMap`, and `videoMap`, and positive
screen/video stream-material evidence. This proves the content-sensitive
material keys and stream gates without silently making them part of the
conservative RC profile.

## Dynamic Proof Evidence

`rc-proof` and `rc-stress` write a versioned `dynamicProofEvidence` object.
The sweep derives it from selected proof-corpus dynamic tags, found GLx
screenshots, GLx timedemo metrics, stream-category diagnostics, tier-support
diagnostics, stream guard diagnostics, and `r_speeds 7` dynamic stream counters.

For `rc-proof`, the required tags cover the conservative retail surface:
`dynamic-entity`, `weapon-model`, `dynamic-light`, and `planar-shadow`.
The evidence requires positive entity and first-person weapon category counters,
positive shadow and dynamic-light stream evidence, dynamic-entity/sprite/dynamic-light/stencil
shadow support, render-IR dlight role/pass ownership diagnostics, projected-dlight
scissor active/computed/applied counters, found screenshots for required map scenes,
and timedemo metrics for required demo scenes.

For `rc-stress`, the dynamic proof expands to staged particles, transient
polys, marks/decals, and beams. That gate requires the particle-heavy timedemo
plus positive particle/poly/mark/beam category counters and positive beam stream
evidence while preserving the same dynamic-light ownership checks. This lets staged
content prove transient draw pressure without losing the dlight proof surface.

## Post Proof Evidence

`rc-proof` and `rc-stress` write a versioned `postProofEvidence` object. The
sweep derives it from selected proof-corpus postprocess tags, found GLx
screenshots, histogram metadata, `glxpostprocess` diagnostics, target-format
diagnostics, output-contract diagnostics, and `r_speeds 7` postprocess samples.

The proof is intentionally narrower than the full output contract. For
`rc-proof`, it requires the greyscale suite on `stock-q3dm1-hud` and the
render-scale suite on `stock-q3dm17-open`. The object fails if the proof corpus
does not select the required tags, if the tagged maps lack found GLx screenshots
or histograms, if the postprocess FBO is not ready, if FBO init failures are
reported, if no postprocess frames or screenshot frames are observed, if
minimized output is reported, if the color contract is invalid, if greyscale
control/frame evidence is absent, or if render-scale control/frame evidence
does not include render/capture dimensions that differ from the window size.
`rc-stress` keeps the same shape while adding staged content pressure around it.

## Diagnostic Gate Analysis

Non-dry-run gate manifests now include structured analysis of the GLx diagnostic commands emitted during screenshot sweeps. The analyzer reads the `glxmaterial`, `glxpostprocess`, and `glxstaticworld`/stream sections from the run log and fails the gate on release-blocking renderer states:

- material renderer enabled but not ready under the RC/stress profiles;
- material compile, link, precache, bind, not-ready, or program-limit failures;
- requested FBO output that is not ready, FBO init failures, bloom create failures, bloom pass failures, or minimized final output;
- dynamic stream readiness loss under the RC/stress profiles, missing stream-category diagnostics, sync/upload/reservation failures, same-frame wrap rejects, streamed draw fallbacks, per-category stream fallbacks, material-program stream skips, or streamed dlight draws that do not appear in the render-IR dlight role and `dynamic-lights` pass counters; screen-map and video-map draws remain forbidden in `rc-proof` but are budgeted explicitly in `rc-stress` for staged material proof;
- static-world renderer or packet batching disabled under the RC/stress profiles, static arena/indirect-buffer failures, or static-world GL errors.
- a `GL12` diagnostic that does not expose the fixed-function executor contract or that claims stream uploads, the GLSL material compiler, or the modern post chain are supported on the GL12 tier.
- a `GL2X` diagnostic that does not expose the programmable executor contract or that treats persistent/modern post/HDR requirements as mandatory on the GL2X tier.
- a `GL3X` diagnostic that does not expose the performance executor contract, omits FBO/UBO/timer/sync/static-buffer/dynamic-buffer ownership, or treats GL4-only persistent upload, indirect submission, or DSA requirements as mandatory on the GL3X tier.
- a `GL41` diagnostic that does not expose the mac-modern executor contract, omits the modern macOS ceiling feature surface, or treats GL4.3 debug output, GL4.4 buffer storage, GL4.5 DSA, MDI, or persistent uploads as mandatory on the GL41 tier.
- a `GL46` diagnostic that does not expose the high-end executor contract or omits persistent uploads, buffer storage uploads, sync-heavy streaming, DSA, MDI, aggressive static-world submission, detailed GPU counters, hardware HDR output, or the required high-end driver feature requirements.

The analyzer also records `glx: ownership legacy delegation ...`, post/output ownership diagnostics, and post shader direct-final diagnostics. Transitional RC gates keep those counters as review evidence, including the split between planned post/output products, executable GLx-owned products, and the opt-in GLSL binder for display-referred SDR legacy gamma, scene-linear SDR, bloom, greyscale, screenshot/blit, and hardware-HDR output shapes. The ownership-proof `glx-ownership` profile writes versioned `ownershipProofEvidence` and fails when any legacy draw delegation remains or when modern tiers do not report executable GLx-owned `PostNode`, `OutputTransform`, and post/output plan evidence with non-zero fingerprints and a zero fallback mask. Promotion validation requires that versioned evidence object instead of relying on scattered raw counters alone.

Ordinary compatibility counters, unsupported capability fallbacks, packet-shape data, skipped material keys, and other tuning metrics remain in the manifest and Markdown summary for review, but they are not treated as blocking failures by themselves.

## Performance Samples

During GLx screenshot captures the sweep briefly enables `r_speeds 7`, waits a small number of frames, captures the screenshot, and disables `r_speeds` again. The compact `glx:` frame-counter lines are parsed into the manifest and Markdown summary as performance samples. They include the five-value product tier, locked pass-schedule text/hash, draw/index pressure, stream strategy/readiness, backend GPU timer text, material renderer failure counts, postprocess output state, capture-policy request/selected/support metadata, stream draw pressure, material-shape stream draw counts, state-only dynamic stream draw counts, dynamic-scene category counts, and static-world draw/MDI counters.

Named RC gates require at least one GLx frame-counter sample in a non-dry-run screenshot sweep. `--perf-sample-wait` controls the number of frames sampled around each GLx capture, and `--no-perf-samples` is available only for focused local experiments that should not count as RC gate evidence.

Named gates also apply a built-in performance budget to the aggregate sample maxima. The default budget blocks stream rejects, material shader failures, material bind/precache failures, streamed/static draw fallbacks, high-risk screen-map/video-map material stream draws, post/output fallback masks on modern tiers, and static MDI errors from silently entering RC evidence. `GLX_POST_OUTPUT_FALLBACK_EXECUTOR_DISABLED` and `GLX_POST_OUTPUT_FALLBACK_EXECUTOR_NOT_BOUND` are intentionally visible as proof gaps while the generated post/output executor remains experimental. `rc-stress` deliberately overrides the screen-map and video-map maxima so staged material proof can exercise those content-sensitive gates; `rc-proof` keeps them forbidden. Dynamic-light stream draws and projected-dlight scissor rectangles are allowed by budget, but dlights still fail diagnostics if they have fallbacks, sync/reservation failures, category fallbacks, missing render-IR dlight role/pass ownership, or missing projected-dlight scissor active/computed/applied proof. Modern-tier samples must also carry shader cache counters, post shader direct-final eligibility/bind counters, material parameter-block counters/fingerprints, stream binding-cache counters, and post/output ownership counters/fingerprints, with positive post/output nodes/fingerprints/plan fingerprints, material programs/binds/parameter blocks/fingerprints, and stream binding restores, so P1 proof cannot omit the hot-path evidence. GL12/GL2X post/output fallback remains recorded compatibility evidence rather than a budget failure. For local experiments use `--no-performance-budget`; for runner-specific limits add a JSON file with `--performance-budget`:

```json
{
  "max": {
    "streamDrawFallbacks": 0,
    "staticDrawFallbacks": 0
  },
  "min": {
    "sampleCount": 1
  }
}
```

The built-in budget also carries per-tier ceilings. The active product tier reported by `r_speeds 7` selects the matching `GL12`, `GL2X`, `GL3X`, `GL41`, or `GL46` budget for draw calls, submitted indexes, stream upload volume, material/shader binds and switches, static packet misses, static queue packet misses, static packet-lookup misses, and GPU frame time. Global zero-tolerance limits still apply to fallback/error counters such as material failures, stream fallbacks, same-frame stream wrap rejects, static draw fallbacks, and static MDI errors. `GL3X`, `GL41`, and `GL46` runs must provide a numeric GPU frame-time sample because those tiers advertise timer-query backed proof.

Performance baselines are separate from hard budgets. Approve a reviewed aggregate sample with `--performance-baseline path/to/glx-performance.json --approve-performance-baseline`, then compare future candidates with `--performance-baseline path/to/glx-performance.json`. Counter growth is checked for draw/index pressure, upload volume, shader/material bind pressure, static packet misses, stream pressure, material-shape and state-only stream draw counts, fallback counters, GPU frame time, and static-world counters; `--performance-max-growth-ratio` defaults to 20%. The baseline JSON embeds the selected GLx proof corpus object, including parity suite version and suite IDs, so comparisons can reject evidence that drifted away from the approved content contract. `rc-proof` requires a compared performance baseline rather than an approval run.

## Automated Verification

`.github/workflows/glx-verification.yml` provides the first renderer-focused automation surface for these gates:

- `GLx logic and boundary tests` builds the deterministic `fnql_glx_logic_tests` target on hosted Ubuntu, then runs both the pure logic tests and the GLx header-boundary scan through CTest.
- `GLx RC gate plans` prints the proof corpus, runs every named gate in `--dry-run` mode, writes the generated configs/manifests/Markdown summaries under `.tmp/glx-gate-plans`, copies the corpus document beside those artifacts, runs the sweep image/diagnostic/performance unit tests, and uploads the gate-plan artifacts. This catches drift between the documented gates and the script-owned cvar/corpus presets without requiring retail assets.
- `GLx runtime sweep` runs on self-hosted GPU runners labeled for GLx validation. Manual `workflow_dispatch` runs can choose the gate and proof platform, while the weekly scheduled mainline run executes `rc-parity` using repository variables for the executable, retail `baseq3` path, optional proof/baseline inputs, and proof-platform id. Both paths write Markdown summaries and upload the full sweep output for review.

The pure logic target intentionally covers GLx decisions that do not need a driver: capability-tier and extension parsing, stream strategy fallback selection, material-key allowlists, static-world packet classification, and static draw-policy gating. Runtime sweeps remain responsible for the parts that require a real OpenGL context, retail assets, and screenshots.

Hosted dry-run gate artifacts are planning evidence only. Blocking RC evidence requires non-dry-run runtime artifacts from the blocking Windows and Linux matrix, and release packaging refuses tagged releases until those artifacts validate.

## Release Proof Root

The release proof root is a reviewed artifact directory containing the runtime-sweep `manifest.json` files for each blocking platform. The validator accepts manifests anywhere below the root, but the recommended layout is:

```text
proof-root/
  windows-x64/
    rc-smoke/<run-id>/manifest.json
    rc-parity/<run-id>/manifest.json
    rc-proof/<run-id>/manifest.json
  linux-x86_64/
    rc-smoke/<run-id>/manifest.json
    rc-parity/<run-id>/manifest.json
    rc-proof/<run-id>/manifest.json
```

Each manifest must be non-dry-run, must carry `proofPlatform` as `windows-x64` or `linux-x86_64`, must carry passing `rendererSwitchEvidence`, must pass the gate when re-evaluated by `scripts/glx_runtime_sweep.py`, and must reference the current proof-corpus and parity-suite versions. `rc-proof` additionally must compare against reviewed screenshot baselines and a reviewed performance baseline; approval-mode manifests do not count as release proof.

Each runtime sweep also writes `glx-visual-dossier.md` beside the manifest and
records it under the manifest `visualDossier` field. The dossier is the review
index for visual artifacts: pipeline flowcharts, renderer-switch lifecycle,
backend state, driver-tier coverage, histograms, luma/exposure false-color
sidecars, PSNR/SSIM comparison metrics, and parity diffs. See
[GLX_VISUAL_DOSSIER.md](GLX_VISUAL_DOSSIER.md).

Tagged release packaging uses the same validator:

```sh
python scripts/release.py --channel release --artifact-root artifacts --ref-name v0.1.0 --glx-proof-root proof-root
```

Because the tagged source tree has GLx as the default, the same release command must also pass
`--glx-rollback-metadata <json>`. That metadata is checked against the staged
archives so the rollback package named by the promotion plan is actually present
in `.install/packages/`.

Manual release packaging records the proof-corpus metadata but does not require a proof root because it is not a GLx promotion event.

## Promotion Boundary

These gates now protect the completed consolidation. GLx remains the only OpenGL-lineage public selector, `vk` remains the independent raster comparison backend, and `rtx` remains the Vulkan ray-tracing backend. Adding aliases or another public renderer requires an explicit contract change and matching regression gates.
