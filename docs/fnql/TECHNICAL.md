# Technical Notes

## Purpose

This file is the maintainer-facing companion to [`README.md`](../../README.md). Keep user installation guidance in the README and use this document for repo structure, release flow, and implementation conventions.

## Project Constraints

FnQL exists to modernize the Quake Live engine without losing retail Steam
compatibility or the useful FnQ3 modernization baseline:

1. Retail Quake Live compatibility is the primary target.
2. Demo, protocol, filesystem, pak/pk3, VM/native module ABI, renderer data,
   and platform-service behavior must be compared against QLSRP before
   compatibility-sensitive changes land.
3. Game-code reconstruction is out of scope for FnQL. Treat inherited
   `qagame`, `cgame`, and UI code as ABI/reference boundaries until a later
   isolation or removal pass proves they are unnecessary.
4. Performance regressions need a clear reason and measurement.
5. Platform additions should not silently narrow the supported matrix.

Compatibility-sensitive areas include:

- demo parsing and recording
- network protocol behavior
- filesystem search order and pak/pk3 loading
- VM ABI and bytecode execution
- native module ABI and loader behavior
- renderer defaults that affect demo output or deterministic behavior

## Reference Baselines

Local checkout and install paths are intentionally kept in [`AGENTS.md`](../../AGENTS.md)
so this maintainer document stays portable.

- FnQ3 import baseline: remote `https://github.com/themuffinator/FnQ3.git`,
  commit `91c28d77878302ae67119fc3a29643cc20ce8489`. The initial import copied
  the local working tree named in `AGENTS.md`, which had uncommitted changes.
- QLSRP reference baseline: remote `https://github.com/themuffinator/QL-SRP.git`,
  commit `94bdd7acdce0c90bf890416e23e704795eac716e`. Use this repository as the
  first stop for Quake Live behavior before inferring from Quake III code.
- Retail runtime target: a legitimate Steam install of Quake Live.

Current migration rule: compare first, reconstruct second, validate third.
Keep observed QLSRP or retail facts distinct from inferred design intent.

## Repository Layout

- [`code/`](../../code): engine and platform code.
- [`docs/`](../../docs): technical docs, upstream reference material, and README templates.
- [`version/`](../../version): shared project version metadata.
- [`scripts/`](../../scripts): repo-local automation for docs and release packaging.
- [`.install/`](../../.install): tracked distribution docs plus generated manifests and package archives.
- [`.tmp/`](../../.tmp): ignored scratch workspace for temporary outputs.

## Versioning

The canonical metadata lives in [`version/fnql_version.h`](../../version/fnql_version.h).

That header feeds:

- runtime version strings via [`code/qcommon/q_shared.h`](../../code/qcommon/q_shared.h)
- Windows resource metadata via [`code/win32/win_resource.rc`](../../code/win32/win_resource.rc)
- Meson and Make version reporting
- documentation rendering
- manual and tagged release archive naming

Current policy:

- Tagged releases use semantic version tags in the form `vX.Y.Z`.
- Manual release runs produce unique version/date/commit tags (e.g. `0.1.0.42-20240403-abc12345`) and matching package prefixes without a channel word.
- The base version in `fnql_version.h` should always represent the next intended stable release line.
- Pending release-note material lives in [`docs/fnql/CHANGELOG.md`](./CHANGELOG.md). Keep the `Unreleased` section current as work lands; GitHub release entries are the durable published history.
- Use [`scripts/changelog.py`](../../scripts/changelog.py) to extract, clean up, promote, or reset the pending changelog section.

Typical changelog helper usage:

```powershell
python scripts/changelog.py section --version Unreleased
python scripts/changelog.py section --version Unreleased --clean
python scripts/changelog.py cleanup --version Unreleased
python scripts/changelog.py prepare-release --version 0.1.0 --date 2026-04-25
python scripts/changelog.py clear-unreleased
```

## Docs Flow

The user-facing docs are generated from templates:

- [`docs/templates/README.md.in`](../templates/README.md.in)
- [`docs/templates/install-readme.html.in`](../templates/install-readme.html.in)

Refresh them with:

```powershell
python scripts/generate_docs.py
```

That command rewrites:

- [`README.md`](../../README.md)
- [`.install/README.html`](../../.install/README.html)

## Release Packaging

The packaging entry point is [`scripts/release.py`](../../scripts/release.py).
Manual release CI orchestration lives in [`scripts/manual_release.py`](../../scripts/manual_release.py).

Typical local usage:

```powershell
python scripts/manual_release.py summary
python scripts/release.py --channel manual --artifact-root <downloaded-artifacts-dir>
python scripts/release.py --channel release --artifact-root <downloaded-artifacts-dir> --ref-name v0.1.0 --glx-proof-root <reviewed-glx-proof-root>
```

The script:

1. refreshes generated docs
2. stages each platform artifact under `.tmp/release/`
3. filters build-system byproducts, debug symbols, editor files, caches, and temporary files out of the staged package
4. injects only the shared package docs needed by players and maintainers
5. writes versioned `.zip` archives into `.install/packages/`
6. emits `.install/release-manifest.json` and `.install/SHA256SUMS.txt`

Manual release publishing builds GitHub release details from the pending changelog, commits, and changed-file summary. If `COPILOT_GITHUB_TOKEN` is configured, the workflow uses GitHub Copilot release-note cleanup with [`.github/release-notes-instructions.md`](../../.github/release-notes-instructions.md); otherwise it falls back to the repo-local GitHub Models prompt in [`scripts/manual_release.py`](../../scripts/manual_release.py). After a release is created from a branch, CI resets `docs/fnql/CHANGELOG.md` back to an empty categorized `Unreleased` template and commits that reset to the release branch.

## CI Notes

`.github/workflows/release.yml` owns main-branch build validation and manual release publishing.
`.github/workflows/issue-triage.yml` owns automated new-issue triage, with maintainer tuning documented in [`ISSUE_TRIAGE.md`](./ISSUE_TRIAGE.md).

Expected behavior:

- pull requests build only
- `main` pushes validate the main branch without publishing a release
- manual `workflow_dispatch` runs publish a new version/date/commit release for the selected ref
- published GitHub releases upload archives whose names match the release tag identity
- Linux release artifacts build inside an Ubuntu 20.04 userspace and run `scripts/check_elf_glibc.py --max-glibc 2.31` before upload so hosted runner image changes do not raise the packaged glibc requirement unexpectedly.

Renderer-focused verification lives beside the release packaging flow:

- [`docs/fnql/GLX_FINAL_CONTRACT.md`](./GLX_FINAL_CONTRACT.md) is the accepted target ADR for the final GLx replacement renderer: stable C ABI, GLx-owned draw behavior, five product tiers, deterministic pass order, and a scene-linear color pipeline.
- [`docs/fnql/GLX_COLORSPACE_AUDIT.md`](./GLX_COLORSPACE_AUDIT.md) records the audited sRGB/linear texture classes, framebuffer-sRGB policy, blending expectations, and screenshot capture color space for GLx color-pipeline work.
- [`docs/fnql/DLIGHT_SHADOWMAP_ROADMAP.md`](./DLIGHT_SHADOWMAP_ROADMAP.md) tracks the GLx/Vulkan dynamic-light shadow-map roadmap, current implementation status, test command, launch helper, and RenderDoc checkpoints.
- [`docs/fnql/GLX_PROOF_CORPUS.md`](./GLX_PROOF_CORPUS.md) is the official GLx screenshot/timedemo scene corpus referenced by gate manifests, performance baselines, CI gate-plan artifacts, and release manifests.
- [`docs/fnql/GLX_VISUAL_DOSSIER.md`](./GLX_VISUAL_DOSSIER.md) defines the generated review dossier written beside every GLx sweep manifest, including pipeline flowcharts, backend-state overlays, histograms, false-color sidecars, parity diffs, and driver-tier matrices.
- [`docs/fnql/GLX_PROMOTION.md`](./GLX_PROMOTION.md) defines the default-renderer promotion, migration-alias, legacy-OpenGL2, and rollback package policy. [`docs/fnql/GLX_ROLLBACK_PACKAGE.md`](./GLX_ROLLBACK_PACKAGE.md) defines the reviewed JSON metadata required for a promoted release rollback package. [`scripts/glx_promotion.py`](../../scripts/glx_promotion.py) is the machine-readable guard for that policy.
- [`.github/workflows/glx-verification.yml`](../../.github/workflows/glx-verification.yml) builds deterministic GLx logic tests, generates dry-run GLx RC gate artifacts, exposes manual self-hosted GLx runtime sweeps, and runs a scheduled mainline `rc-parity` sweep on configured self-hosted GPU runners.
- [`.github/workflows/vulkan-verification.yml`](../../.github/workflows/vulkan-verification.yml) builds the Vulkan renderer, generates dry-run Vulkan RC gate artifacts from [`scripts/vk_runtime_sweep.py`](../../scripts/vk_runtime_sweep.py), and exposes a manual self-hosted Vulkan runtime sweep.

Dry-run renderer gate artifacts are planning evidence only. Blocking release evidence requires non-dry-run runtime artifacts on the documented platform matrix with retail `baseq3` assets. Tagged release packaging requires `--glx-proof-root`; the release script revalidates passing `rc-smoke`, `rc-parity`, and `rc-proof` manifests for `windows-x64` and `linux-x86_64` before it writes the release manifest. Each blocking manifest must carry passing versioned `rendererSwitchEvidence` for the generated `renderer_switch` run, including the keep-window `CL_Vid_Restart` path, every expected map/round/step screenshot transition, GLx diagnostics, and GLx performance samples. The `rc-parity` and `rc-proof` manifests must also carry passing versioned `worldProofEvidence` proving the selected stock/high-geometry/lightmap/fog/visibility world maps, GLx screenshot histograms, static-world draw/index counters, zero static packet misses/fallbacks/errors, and lightmap/fog path evidence. The `rc-proof` manifest must carry passing versioned `materialProofEvidence` proving material-stage/tcgen corpus tags, GLx screenshot histograms, material renderer readiness, compile/program activity, zero material failures or unsupported plans, parameter-block fingerprints, required stream-material feature counters, and forbidden screen-map/video-map guards for the conservative proof surface. The `rc-proof` manifest must also carry passing versioned `dynamicProofEvidence` proving dynamic entity, first-person weapon, dynamic-light stream/ownership, and planar-shadow corpus coverage with required stream-category/feature counters, tier-support evidence, screenshots or timedemos for the selected dynamic scenes, and zero stream/category fallbacks. The `rc-proof` manifest must also carry passing versioned `postProofEvidence` proving greyscale and render-scale corpus coverage with found GLx screenshots, histograms, ready FBO state, positive postprocess frame/screenshot counters, render-scale dimension evidence, no minimized output, direct-final post shader diagnostics, and a valid color contract. The staged `rc-stress` material proof additionally covers animated-image, screen-map, and video-map stage flags, staged `rc-stress` dynamic proof covers particle, transient-poly, mark/decal, and beam counters, and staged `rc-stress` post proof keeps greyscale/render-scale evidence active before those content-sensitive paths can be considered for conservative defaults. Promotion proof roots must also carry passing versioned `ownershipProofEvidence` in the non-dry-run `glx-ownership` manifests for both blocking platforms, including executable GLx-owned post/output counts and post shader direct-final diagnostics rather than planned-only post/output diagnostics. The `rc-proof` manifest must also carry the current proof-corpus version, parity-suite version, and the screenshot, demo-playback, HUD, shadow, bloom, cel-shading, greyscale, and render-scale suite records. Release packaging records the GLx promotion report and refuses a source tree that has promoted renderer defaults before `scripts/glx_promotion.py --require-ready --proof-root <dir> --rollback-metadata <json>` can pass. For promoted releases, pass the same reviewed rollback metadata to `scripts/release.py --glx-rollback-metadata <json>` so `.install/release-manifest.json` records the matched rollback archive and checksum. The GLx runtime sweep applies built-in global and per-tier performance budgets by default; use `--performance-budget` only to add reviewed runner-specific thresholds.

## Shadowmapping

Shadowmapping in FnQL is a renderer-local lighting layer. It may improve
how maps, effects, and authored light cues read on screen, but it must not
change retail demo playback, protocol behavior, VM execution, filesystem
search order, map loading, or game-side entity state. Treat every
shadowmapping change as visual and compatibility-sensitive at the same time:
the visuals are optional, while the render order, asset acceptance, and
fallback behavior must stay predictable.

The subsystem is organized around three atlas families:

- Point-light cubemap atlas: transient gameplay lights, renderer-injected test
  lights, and point-capable sidecar/static lights. Each selected light owns six
  atlas faces, and the planner trades light count against face resolution.
- Directional CSM atlas: sky-sun shadows derived from the active world's
  `q3map_sun`, `q3map_sunExt`, or `q3map_sunExt2` shader metadata. Cascades
  are split, laterally snapped, depth-expanded, rendered, cached where valid,
  and sampled by a receiver pass after the atlas is published.
- 2D spotlight atlas: sidecar spot lights and surfacelight proxy lights,
  including large planar emitters that need representative cone projection,
  per-light tile sizing, and bounded atlas fill. The backend caches the
  rendered atlas against a plan/world-caster signature so unchanged frames —
  and the repeated per-view spot pass on portal/mirror frames — skip the
  clear and re-render, while entity casters force a redraw.

`shadowManager_t` is the per-view owner for shadow planning. Front-end scene
work collects candidates, rejects invalid or low-value work, assigns atlas
regions, records publication state, and writes an ordered pass schedule into
the draw command. GLx and Vulkan backends then consume that schedule for
depth-only atlas producer passes and sample only manager-published atlas
generations in lighting or receiver passes. Backend-global atlas readiness is
a compatibility fallback, not the canonical source of truth.

`r_shadowCorrectness 1` is the first-pass diagnostic mode for shadow-map
contract work. It is intentionally default-off and cheat-gated. When enabled,
GLx and Vulkan force the minimal correctness path: point-light shadows only,
one selected dynamic light, hard filtering, no spot atlas, no CSM producer or
receiver pass, and no alpha-tested shadow casters. Use it to validate raw
light-space projection, depth output, compare direction, bias separation, and
backend resource readiness before reintroducing filtering, spot/CSM work, or
multi-light atlas pressure. While enabled, the end-of-frame diagnostics emit
`shadow correctness` records for the point-shadow atlas: backend depth
convention, clear/compare state, publication generation, face viewports and
scissors, API-space viewports and scissors, cache/render status, and the full
projection/model matrices used to write each face.

The shadow coordinate contract is intentionally explicit:

- Shadow atlas plans use a top-left tile origin. `viewParms_t` stores the
  producer viewport in the renderer's historical lower-left convention by
  writing `atlasHeight - atlasY - tileSize`.
- GLx consumes that lower-left `viewParms_t` viewport directly. Dynamic-light
  and CSM sampling compensate by addressing atlas rows from the lower-left
  texture convention.
- Vulkan converts the same lower-left `viewParms_t` viewport back to top-left
  `VkViewport`/`VkRect2D` coordinates, negates the clip-space Y projection row
  before MVP upload, and samples dynamic-light atlas rows directly. Vulkan CSM
  still samples `1.0 - light_coord.z` because the receiver reconstructs a
  light-space atlas coordinate from world space rather than from the producer
  viewport.
- GLx point and spot shadow depths are forward OpenGL depth (`[-1, 1]` clip,
  clear `1.0`, `GL_LEQUAL`). Vulkan point and spot shadow depths are
  zero-to-one clip depths and currently follow the backend's reversed-depth
  build (`clear 0.0`, `VK_COMPARE_OP_GREATER_OR_EQUAL`). Vulkan CSM remains a
  forward-depth producer pass (`clear 1.0`, `VK_COMPARE_OP_LESS_OR_EQUAL`) and
  carries the compare direction as a receiver shader mode.

Bias is also a four-part contract, not one interchangeable tuning knob.
Receiver bias (`r_dlightShadowBias` / `r_csmShadowBias`) is applied only while
sampling and is clamped by angle and texel footprint where the shader has that
information. Constant caster depth bias and slope-scaled caster bias are
applied only while rendering depth producers (`glPolygonOffset` on GLx,
`vkCmdSetDepthBias` on Vulkan). Caster normal bias is a separate vertex offset
before rasterization, using the point/spot light-to-vertex vector or the
negated CSM sunlight direction. `r_shadowCorrectness 1` prints all four
dynamic-light bias values separately so a capture can distinguish receiver
compare tuning from producer raster tuning.

Filtering is reintroduced only after the hard-shadow path is known-good. The
shared filter contract is: hard mode is one effective center sample represented
by four zero-offset taps in the receiver shader, 2x2 PCF uses four half-texel
taps, and four-tap Poisson PCF uses the shared `0.25/0.75` inner/outer offset
pair. GLx and Vulkan both derive dynamic-light and CSM filter offsets from the
same helper, while `r_shadowCorrectness 1` still forces the effective filter to
hard and reports the requested filter, effective filter, tap count, and offsets
in the correctness summary. Ordinary dynamic-light shadow debug output reports
the same effective filter fields for production captures.

Keep the GLx and Vulkan paths mechanically aligned. Shared rules include
angle-aware caster normal bias, texel-limited receiver bias, bounded filter
selection, cascade atlas snapping, atlas publication before sampling, and
explicit fallbacks when there is no world, no active sky sun, no atlas, or no
valid cascade. For CSM, the light direction is an incoming sunlight vector for
planning, while the caster normal-bias helper needs the vector from light to
vertex; GLx and Vulkan must both use the negated `tr.csm.lightDirection` in
that path. A sign drift here can produce healthy-looking planner telemetry and
still leave the final scene visually unshadowed.

CSM cascade planning intentionally snaps only the atlas-facing light-space
axes, not the light-depth axis. Quantizing depth can make static world shadows
appear to shimmer or partially remap even when model shadows are aligned. The
depth bounds are expanded beyond the receiver slice so world geometry just
outside the visible split can still cast into it, while the X/Y atlas footprint
stays texel-snapped for stable sampling. Surface culling also keeps a two-texel
margin around cascade bounds so edge casters do not pop as the camera crosses
snap thresholds.

CSM caster and receiver passes must walk the sorted draw-surface list in the
same order as the main backend, changing entity state inline when the sort key
changes. Do not regroup CSM work by first collecting entities and then
rescanning surfaces per entity: that can change batch/state lifetime from the
main draw order and has caused partial or unstable Vulkan world-shadow output
even when individual model shadows appear aligned.

`r_csmDebug 1` emits both the compact `csm shadows` summary and one
`csm cascade` line per active cascade. The cascade lines are the Round 6
coordinate evidence: split near/far, atlas tile, renderer-space viewport,
API-space viewport, sample-Y rule, clip-Y rule, depth convention, compare
rule, light-space bounds, origin, and texel size. GLx reports native sample Y,
native clip Y, OpenGL `[-1, 1]` clip depth, and `lequal` forward-depth CSM.
Vulkan reports inverted CSM sample Y, flipped clip Y, `[0, 1]` clip depth, and
the same forward `lequal` CSM producer/receiver compare. Keep these lines in
sync with the CSM producer and receiver shader contract when changing cascade
math.

`r_dlightShadowDebug 1`, `r_spotShadowDebug 1`, or `r_csmDebug 1` also emits
`shadow atlas contract` lines for the point, spot, and CSM atlases. These
Round 7 lines report whether the atlas is active, tile size, atlas dimensions,
record count, fill pressure, filter padding in texels, UV clamp inset, sampler
wrap policy, and deterministic allocation policy. Point lights allocate by
priority with `dlightIndex` tie-breaks, spot lights allocate by priority with
source/source-index tie-breaks, and CSM allocates by cascade index. Current PCF
kernels never sample beyond one texel, and the receiver shaders clamp UVs
inside the tile before applying taps, so the logged clamp must remain at least
as large as the logged filter pad.

The GLx and Vulkan runtime sweep analyzers derive a `shadowProfile` object
from the same debug stream only after the correctness contracts above are
present and passing. It records raster work, sampler pressure, pass
orchestration, and CPU timing buckets from the dlight, spot, CSM, atlas, and
manager samples. Treat `profileReady: true` as permission to start looking for
cost reductions in captures or vendor profilers; a failed profile means the
correctness evidence is still incomplete or unstable and should be fixed first.

CSM producer and receiver projection must stay a single contract. GLx writes
OpenGL-style atlas depth directly from the planned light-space bounds; Vulkan
converts the same projection to Vulkan clip space by negating the full clip-Y
row (`projectionMatrix[1]`, `[5]`, `[9]`, and `[13]`) before MVP upload. The
`[13]` term is important because CSM orthographic projections store the
atlas-vertical light-space offset there. Receiver shaders then sample the
published atlas with `light_coord.y` as atlas X. GLx samples
`light_coord.z` as atlas Y because its FBO texture coordinates use the same
bottom-left convention as the producer viewport. Vulkan samples
`1.0 - light_coord.z` because the rendered depth image is addressed with a
top-left image origin after the clip-Y conversion. Keep that difference local
to the Vulkan CSM receiver shader, and clamp the PCF center inside the cascade
tile to avoid cross-cascade taps.

Documentation and evidence are split by audience. Player-facing controls live
in [`docs/DISPLAY.md`](../DISPLAY.md#dynamic-lighting-and-shadowing). The
maintainer implementation ledger lives in
[`DLIGHT_SHADOWMAP_ROADMAP.md`](./DLIGHT_SHADOWMAP_ROADMAP.md), with the
broader historical plan in
[`docs-dev/plans/2026-06-03-vk-shadowmapping.md`](../../docs-dev/plans/2026-06-03-vk-shadowmapping.md).
Before enabling real shadow maps by default, require non-dry-run GLx and
Vulkan runtime evidence with retail assets, passing release-gate summaries,
reviewed screenshots or diffs, and RenderDoc inspection notes for atlas
contents, receiver sampling, and combined point/spot/CSM scheduling.

Useful focused checks for ordinary shadowmapping changes:

```powershell
python tests\shadow_correctness_source_tests.py
python tests\dlight_shadow_bias_tests.py
python tests\shadow_manager_source_tests.py
python tests\vulkan\vk_runtime_sweep_tests.py
python tests\glx\glx_runtime_sweep_tests.py
meson compile -C .tmp\meson-dlight fnql_glx_x86_64 fnql_vulkan_x86_64
```

## Audio Backend Notes

- [`AUDIO_ENGINE.md`](./AUDIO_ENGINE.md) is the detailed architecture and source-layout reference for the modern audio engine.
- The default client audio path is the OpenAL backend selected by `s_backend openal`.
- `s_backend legacy` keeps the original Quake III mixer/device backend available as a fallback path.
- Client audio now lives under [`code/client/audio`](../../code/client/audio): the stable `S_*` facade is at the module root, the original mixer is in `legacy/`, codecs are in `codecs/`, the OpenAL backend is in `openal/`, and reusable policy/sidecar headers used by tools and tests are in `shared/`.
- OpenAL headers are provided through the Meson `openal-soft` subproject fallback or a system OpenAL development package. The client still loads the OpenAL runtime dynamically so startup can fall back to the legacy mixer when the runtime is unavailable.
- The runtime reporting cvar is `s_backendActive`. Device selection for the OpenAL backend uses `s_alDevice`.
- The OpenAL backend also exposes `s_alReverb`, `s_alOcclusion`, `s_alReverbGain`, and `s_alOcclusionStrength` for the environmental spatial layer. Reverb enablement is latched because the EFX reverb slot is created at backend init. Listener environment changes blend EFX preset parameters and per-source wet/tone values over a short transition, with the active-to-target environment visible in the spatial debug overlay and `s_alDebugDump`. Occlusion traces feed a smoothed per-voice target; direct-path attenuation is kept separate from tone-filter sweeps so wall transitions do not zipper. The per-voice EFX filters are intentionally limited to low-pass, high-pass, and band-pass presets chosen by source class and occlusion/environment state.
- Modern OpenAL startup requests are exposed as latched cvars: `s_alHrtf`, `s_alHrtfId`, `s_alOutputMode`, `s_alDistanceModel`, `s_alFrequency`, `s_alRefresh`, `s_alMonoSources`, `s_alStereoSources`, `s_alOutputLimiter`, and `s_alSpatializeStereo`. Context creation first tries requested modern attributes, then standard source/frequency hints, then default attributes before the outer backend fallback can select legacy. Keep `s_info` as the canonical place to compare requested values against active runtime/device values. When `ALC_SOFT_device_clock` is available, `s_info` should refresh the clock/latency query live so latency diagnostics are current instead of just an init-time snapshot.
- Runtime OpenAL device ergonomics are intentionally conservative. The backend polls `ALC_CONNECTED` when `ALC_EXT_disconnect` is available, reports disconnect state in `s_info`, and uses `ALC_SOFT_reopen_device` for `s_alRecoverDevice` and `s_alAutoRecover` live recovery attempts. If live reopen/reset is unsupported or fails, `snd_restart` remains the deterministic full rebuild path. `s_alConfigHints` is a diagnostic command only; it should point users at OpenAL Soft config-file options such as stereo/HRTF mode, resampler, period, limiter, and surround decoder settings without moving those library-global policies into engine cvars.
- OpenAL enumeration is available through `s_alListDevices` and `s_alListHrtfs`. The HRTF command uses the live OpenAL device when possible and otherwise opens the requested/default device temporarily for diagnostics.
- Mono world sounds use true OpenAL positional sources driven by Quake listener/source coordinates. They use the active standard OpenAL distance model with reference distance `80`, max distance `1330`, and rolloff `1`. Keep local/UI/announcer, raw/music streams, and authored multi-channel samples non-spatial. Two-channel world samples also stay direct by default and may only enter positional routing through the opt-in `s_alSpatializeStereo` compatibility switch when `AL_SOFT_source_spatialize` is available. Stereo and surround samples/streams should request `AL_DIRECT_CHANNELS_SOFT` when `AL_SOFT_direct_channels` is available, and prefer `AL_REMIX_UNMATCHED_SOFT` when `AL_SOFT_direct_channels_remix` is available so unmatched authored channels are folded into narrower output layouts. `AL_EXT_MCFORMATS` gates native quad/5.1/6.1/7.1 PCM submission; runtimes without it must keep playing authored surround content through the stereo downmix fallback.
- The OpenAL backend reads `sound/fnql-weapon-sounds.sndshd` as a small idTech4-style sound shader subset. Supported tuning keys include `minDistance`, `maxDistance`, `volume`, `volumeDb`, `shakes`, `reverb`, `wetLevel`, `frequencyShift`, and raw `sound/...` sample entries. The shipped `baseq3` and `missionpack` files live under `pkg/<game>/sound/`, are packed into `FnQL-pkg.fnz`, and cover standard Quake III Arena plus Team Arena weapon samples with slightly stronger gain, longer falloff, and modest wet-send scaling.
- UHJ and B-Format are an opt-in authoring ceiling, not a reinterpretation of normal assets. Registered WAV samples with delimited filename tags such as `uhj`, `uhj3`, `bformat2d`, `bformat3d`, or `ambisonic` use `AL_SOFT_UHJ` or `AL_EXT_BFORMAT` buffer formats when available. Encoded sound-field samples do not request `AL_DIRECT_CHANNELS_SOFT`, because their channels are encoded sound-field components rather than authored speaker feeds. Unsupported encoded samples fall back to stereo: UHJ keeps the stereo-compatible first two channels, while B-Format uses the W/omni channel.
- World voice property updates are batched with `AL_SOFT_deferred_updates` when available. Keep streaming queue updates outside that batch so music/raw buffer progress remains straightforward to reason about.
- `FNQL_AUDIO_LOOPBACK_TESTS` builds the deterministic audio test targets under [`tests/audio`](../../tests/audio). `fnql_audio_zone_tests` validates `.azb` runtime parsing, v1/v2 compatibility, zone priority selection, portal blend bounds, and invalid sidecar rejection; CTest registers it as `fnql_audio_zones`. `fnql_audio_recovery_tests` validates the device-loss policy without real hardware disconnects: poll cadence, retry suppression, one-shot warnings, reconnect notification, disabled auto-recovery, and force/skip decisions; CTest registers it as `fnql_audio_recovery`. `fnql_audio_loopback_tests` is a headless OpenAL Soft loopback harness that dynamically loads OpenAL, skips with exit code `77` when `ALC_SOFT_loopback` is unavailable, and otherwise verifies HRTF status visibility and mode switching, distance attenuation, direct-channel isolation, stereo/quad/5.1/6.1/7.1 speaker-layout routing where supported, UHJ/B-Format buffer acceptance where supported, idle silence, and EFX low-/high-/band-pass filters; CTest registers it as `fnql_audio_loopback`.
- `AL_SOFT_source_latency` is optional. When present, `s_alDebugDump` should use `AL_SEC_OFFSET_LATENCY_SOFT` for the selected OpenAL source so voice-level offset/latency diagnostics line up with the device-level clock/latency values printed by `s_info`.
- `s_alSourceClassDebug` is a developer cvar for dump-only source-class aggregation. It should not affect source allocation, routing, filters, or playback state.
- `fnql-audiozonesc` builds the optional audio-zone sidecar compiler under [`code/tools/audiozones`](../../code/tools/audiozones). It compiles `maps/<mapname>.audiozones` text files into little-endian `maps/<mapname>.azb` files with AABB zones, preset index, reverb gain, occlusion multiplier, LF/HF tone multipliers, transition time, priority, and a short debug name. It can also generate a first-pass sidecar directly from an IBSP v46/v47 map with `--from-bsp`, using BSP leaves, clusters, areas, draw surfaces, brushes, shader contents, and surface flags to classify room bounds, environment presets, material class metadata, generated portal hints, and per-portal blend tuning. `--material-map <path>` layers maintainer shader-pattern overrides into BSP classification; material votes are weighted by whether evidence came from visible draw surfaces, brush bodies, or brush sides, and coarsened zones recompute dominant material metadata from accumulated weights. Use `--audit [--samples N]` on generated sidecars before listening passes; it runs the client runtime parser, summarizes preset/material/portal/tuning coverage, warns about suspicious overlaps or portal patterns, reports deterministic lookup/profile timing, and emits confidence/anomaly scores for triage. Runtime loading uses normal `FS_ReadFile` search semantics, so sidecars can live loose or in packages without creating a new asset path.
- Keep dedicated-server builds free of the OpenAL runtime dependency.

### Audio Zone Sidecars

Audio zones are an optional polishing path, not a map requirement. Missing files, invalid files, disabled zones, and listener positions outside every authored zone must fall back to the generic listener-probe environment heuristics.

Runtime behavior:

- Current map `maps/foo.bsp` maps to sidecar `maps/foo.azb`.
- `FnQL-pkg.fnz` next to the executable has priority for packaged FnQL data-only sidecars. The loader tries game-dir-prefixed archive entries first, such as `baseq3/maps/foo.azb`, `baseq3/sound/fnql-weapon-sounds.sndshd`, `missionpack/maps/foo.azb`, or `missionpack/sound/fnql-weapon-sounds.sndshd`, then falls back to normal filesystem and pak search.
- `s_alAudioZones 1` enables sidecar loading; `s_alAudioZones 0` forces generic heuristics.
- The sidecar is rechecked when the active map path or the cvar state changes.
- Higher `priority` wins for overlapping zones. Equal priorities prefer the smaller AABB so nested rooms override broader area zones naturally.
- The selected zone overrides only the audio environment values. It does not affect collision, visibility, demos, protocol, VM behavior, entity state, or asset compatibility.
- If a selected zone uses the `outdoors` or `underwater` preset, or a version 2 sidecar marks it with the matching flag, the corresponding environment flag is set so the existing tone-class logic keeps behaving consistently.
- `s_info`, `s_alDebugOverlay 2`, and `s_alDebugDump` expose whether zones are active, which material metadata they carried, and which values they contributed.
- The runtime accepts version 1, version 2, and version 3 `.azb` sidecars. Version 2 adds material class and portal metadata for generated maps; version 3 adds per-portal blend distance, minimum threshold, maximum crossfade, and blend-curve metadata. Material tuning is baked into the zone values by the compiler, and portal hints provide a bounded crossfade toward adjacent zones near generated boundaries. Version 2 portals inherit the default 192-unit smooth blend, 0.02 minimum threshold, and 0.45 maximum crossfade.
- Generated BSP zones use negative priorities. Hand-authored overrides merged with `fnql-audiozonesc --from-bsp --merge maps/foo.audiozones maps/foo.bsp` therefore win with their default priority `0`, while still allowing broader generated fallback coverage.
- `fnql-audiozonesc --audit --samples 32768 maps/foo.azb` is the maintainer-facing preflight for large generated sidecars. Treat warnings, low confidence, and high anomaly scores as prompts for manual listening, merge overrides, or compiler tuning; `--strict` can make warnings fail in temporary sweep jobs.
- `python scripts/audio_zone_sweep.py --tool path/to/fnql-audiozonesc --relative-root baseq3 --override-root baseq3 --output-root .tmp/audio-zone-sweeps/baseq3 --strict baseq3/maps` is the bulk migration path for map estates. It preserves map-relative output names, merges matching `.audiozones` overrides, runs `--audit` on each generated sidecar, and emits JSON/CSV reports for review or CI artifacts. Use `--dry-run` first when validating a new map tree.
- Standard Quake III Arena `baseq3` arena-map sidecars are tracked under `pkg/baseq3/maps/` and packed into `FnQL-pkg.fnz` as `baseq3/maps/` entries for installs/releases. The standard and Team Arena weapon sound shaders are tracked under `pkg/baseq3/sound/` and `pkg/missionpack/sound/`, and packed as `baseq3/sound/fnql-weapon-sounds.sndshd` and `missionpack/sound/fnql-weapon-sounds.sndshd`. Regenerate audio zones from a local retail `baseq3` pak directory with `python scripts/generate_standard_audio_zones.py --tool path/to/fnql-audiozonesc path/to/Quake3/baseq3`; the helper extracts BSPs into `.tmp/` and writes only `.azb` sidecars back to the package source tree.
- Material maps use one rule per line: `shader/pattern material [preset name] [flag outdoor] [weight N]`. Patterns are case-insensitive substrings unless they contain `*` or `?`, where they become simple wildcards. Use them for custom shader packs whose names do not advertise their acoustic material.

The source format intentionally stays small, but it can express the version 2
and version 3 metadata needed for production overrides:

```text
audiozones 1

zone "atrium" {
  bounds -512 -512 -64 512 512 384
  environment hall
  material stone
  flag outdoor
  reverbGain 1.10
  occlusionMultiplier 0.85
  lpfBias 0.95
  hpfBias 1.00
  transitionMs 900
  priority 10

  portal "hallway" {
    bounds 512 -128 -64 512 128 192
    openness 0.80
    blendDistance 128
    minBlend 0.03
    maxBlend 0.35
    curve ease-out
  }
}
```

Accepted environment names are `small-room`, `room`, `stone-room`, `hallway`, `hall`, `outdoors`, and `underwater`. `bounds` may be replaced by separate `mins` and `maxs` properties. `directHF`/`wetHF` and `directLF`/`wetLF` are available when a zone needs separate low-pass or high-pass bias instead of the combined `lpfBias`/`hpfBias` shortcuts. `material` accepts `unknown`, `neutral`, `stone`, `metal`, `liquid`, `sky`, or `soft`; `flag outdoor`, `flag underwater`, `outdoor true`, and `underwater true` set runtime environment flags. `portal "<target>" { bounds ... openness ... }` defines an explicit transition surface. Optional portal tuning accepts `blendDistance`, `minBlend`, `maxBlend`, and `curve`; curves are `smooth`, `linear`, `ease-in`, or `ease-out`. Merged override files keep authored materials, flags, portals, and portal tuning while clearing only the internal generated flag.

### Audio Migration Expectations

- Treat the modern audio work as client render-side only. It must not alter demo formats, network protocol behavior, filesystem search order, VM ABI behavior, or which sound assets/mods are accepted.
- Preserve existing player controls and config behavior when adding OpenAL features. `s_backend`, `s_backendActive`, `s_alDevice`, `s_alReverb`, `s_alOcclusion`, `s_alReverbGain`, `s_alOcclusionStrength`, `s_alAutoRecover`, `s_doppler`, `s_info`, `s_alDebugDump`, `s_alRecoverDevice`, and `s_alConfigHints` are compatibility surfaces now, not throwaway diagnostics.
- Keep new OpenAL startup cvars latched and request-oriented. The runtime may legitimately choose a different active HRTF state, output mode, source count, limiter state, frequency, or refresh rate; `s_info` should remain the canonical requested-vs-active report.
- Keep fallback deterministic and observable: requested device first, system default device if the requested device cannot be opened, safer OpenAL context attributes next, then legacy backend fallback. Console warnings should explain denied or unsupported requests without treating normal OpenAL capability differences as fatal errors.
- Keep mono world sounds positional and keep local/UI/announcer, raw/music streams, and stereo samples on the direct non-spatial path by default. `s_alSpatializeStereo` is an opt-in compatibility escape hatch for two-channel world samples only, and only on runtimes with `AL_SOFT_source_spatialize`; authored surround layouts must remain direct. UHJ/B-Format tags must stay explicit and additive so existing multichannel content is never silently reclassified.
- Keep audio-zone sidecars optional and data-only. They may refine environmental rendering, but must not become required content or a gameplay contract.
- For large map collections, prefer the audio-zone sweep script over one-off shell loops so generation, material-map usage, strict audit status, override usage, warnings, confidence/anomaly scores, and lookup-profile metrics land in reproducible JSON/CSV reports.
- Update [`docs/AUDIO.md`](../AUDIO.md) for player-facing defaults and troubleshooting, and update the README templates rather than hand-editing generated README outputs. After template changes, run `python scripts/generate_docs.py`.
- Validate audio-facing migration changes with a normal client build. Run `fnql_audio_zones` for sidecar parser/runtime changes and `fnql_audio_recovery` for device-loss policy changes; when OpenAL Soft loopback is available, also run `fnql_audio_loopback` so HRTF reporting, distance gain, direct stereo routing, idle silence, and EFX filters stay covered.

## Naming

Active build, packaging, and distribution surfaces should use `FnQL` naming consistently.
Historical upstream references should only remain where they are part of provenance, copyright notices, or archived material.
