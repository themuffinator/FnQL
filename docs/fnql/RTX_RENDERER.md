# RTX Renderer Contract

## Status

FnQL carries the renderer introduced by
[FnQ3 commit `a78f78b`](https://github.com/themuffinator/FnQ3/commit/a78f78b7d8076311e96001ee47a2b064247b2c16)
as the ray-traced member of FnQL's **supported three-renderer set**, named
`rtx`. Default builds include `glx`, `vk`, and `rtx`; fresh configurations
select GLx while keeping RTX available for an explicit renderer switch.

The port has two useful operating states:

- `rtx_rt_mode 0` loads the RTX module but uses its complete raster path. This
  is the hardware-independent compatibility and module-lifecycle gate.
- `rtx_rt_mode 2` requests native ray-tracing-pipeline composition. With
  `rtx_rt_require 0`, an unsupported device retains the complete raster frame.
  With `rtx_rt_require 1`, capability or primary-dispatch failure is fatal; a
  raster-only frame is never reported as successful required-RT output.

The renderer module is supported, while native ray-tracing mode remains an
explicit capability-gated path. Release-quality native-RT claims still
require reviewed runtime evidence on the relevant operating-system, GPU, Vulkan
loader, and driver matrix using a legitimate retail Quake Live Steam install.

## FnQL Compatibility Boundary

The original renderer was built in FnQ3's Quake III-oriented tree. The FnQL
port rebases it on the engine contracts used by retail Quake Live rather than
freezing the original renderer API and asset assumptions.

The required boundary is:

- renderer API version 13, with the export table zero-initialized before
  optional entries are assigned;
- Quake Live BSP version 47 alongside the retained legacy BSP readers, with
  every lump range validated before use;
- Quake Live advertisement cells, including loading-view refresh, cell display
  state, labels, and per-frame world submission;
- the host WebUI upload/draw export and runtime-sized RGBA shader registration;
- the retained FontStash host-text exports for drawing, measurement, metrics,
  and atlas diagnostics;
- `RDF_NOFIRSTPERSON` behavior for retail capture and view composition;
- no renderer-side `r_fovCorrection` or `RDF_NOFOVCORRECTION` path. Retail
  Quake Live game modules already provide the compatible horizontal-FOV
  behavior, so applying an inherited renderer correction would distort it a
  second time.

The renderer remains an engine module. It does not include, reconstruct, or
replace retail `cgame`, UI, or `qagame` code.

## Hybrid Frame Ownership

Native RT is deliberately restricted to an eligible primary view: a full-size,
center-eye, non-portal world view with compatible history and output extent.
Portals, mirrors, stereo eyes, cube captures, partial viewports, HUD-only views,
and other secondary work retain complete raster behavior.

An eligible primary frame is composed in this order:

1. The raster base draws the RT-eligible opaque world subset. It supplies
   compatible depth and is also a complete fallback if tracing cannot run.
2. Primary rays replace the eligible base color only after a successful trace
   and scene-color transfer.
3. The complementary raster overlay draws everything not owned by the native
   subset: advertisements, entities, sky, portals, authored special stages,
   alpha-tested and translucent content, decals, particles, beams, marks,
   flares, fogged surfaces, and enhanced liquids.
4. World-space post effects, optional global fog, bloom, tone mapping, and the
   final SDR transform run in their defined order. Later HUD-only scenes remain
   raster-owned.

The base and overlay classifiers must remain complements. A surface may not be
silently omitted, and a blended or authored-special surface may not become an
ordinary opaque RT material merely because its texture name resembles one.
When fallback is permitted, a trace, copy, or capability failure preserves the
already complete raster result. When strict RT is requested, the same failure
must stop the run.

## Conservative Defaults

RTX and presentation features added by the upstream work are opt-in in FnQL.
This protects retail appearance, driver coverage, and deterministic comparison
against the existing renderers.

| Cvar | Default | Contract |
| --- | ---: | --- |
| `rtx_rt_mode` | `0` | Load the module in raster fallback; native RT requires an explicit request and `vid_restart`. |
| `rtx_rt_require` | `0` | Permit honest fallback unless the operator explicitly asks to fail closed. |
| `rtx_rt_async_overlap` | `0` | Keep experimental queue overlap disabled. |
| `rtx_rt_dynamic_blas` | `0` | Keep experimental dynamic acceleration-structure participation disabled. |
| `rtx_rt_material_heuristics` | `0` | Do not reinterpret retail surface flags or shader/image names as PBR material metadata. |
| `r_globalFog` | `0` | Do not load or apply optional map fog sidecars by default. |
| `r_fogMode` | `0` | Preserve the legacy BSP fog lookup unless analytic fog is selected explicitly. |
| `r_liquid` | `0` | Preserve authored liquid stages without the enhanced screen-space layer by default. |
| `r_hdr` | `0` | Preserve the SDR presentation path by default. |
| `r_surfaceLightProxies` | `0` | Do not infer analytic emitters from compiled surface-light metadata by default. |

`rtx_rt_mode 1` is a capability-development mode that requests ray-query
support while retaining raster output. It is not a hidden substitute for mode
2 and is not part of the ordinary compatibility smoke test.

## Capability and Safety Rules

Native activation is a negotiated device contract, not an extension-name
guess. The renderer must verify every required feature, entry point, limit,
format, descriptor layout, acceleration-structure property, shader-binding
table alignment, and output transfer path before it publishes an active mode.
Core Vulkan feature promotion and extension aliases must both be handled where
the supported loader permits them.

The renderer also treats retail content as untrusted input:

- BSP and advertisement lump offsets and sizes are validated before pointer
  arithmetic;
- geometry capacities use logical buffer sizes, not padded Vulkan allocation
  sizes, and byte/count arithmetic is checked before allocation or upload;
- indexed geometry is accepted or rejected as complete triangles so a bad
  index cannot reconnect unrelated vertices;
- optional fog and light sidecars have explicit byte, item, numeric, and
  nesting limits; non-finite values and trailing junk are rejected;
- WebUI dimensions are bounded before calculating RGBA upload bytes;
- material ownership remains conservative. Broad texture-name guesses and
  unrelated surface flags are not compatibility evidence;
- lightmapped surfaces stay raster-owned until native closest-hit shading can
  consume their authored lightmap semantics correctly;
- trace/copy failures leave render-pass state recoverable for fallback, or
  terminate a strict-RT run explicitly.

Debug and experimental paths must not weaken those checks. A diagnostic cvar
may expose more information, but it may not convert malformed content into a
trusted allocation or silently relax a strict capability request.

## Building

Meson is the canonical build graph. The default modular build includes RTX:

```text
meson setup meson/build-rtx -Drenderers=glx,vk,rtx
meson compile -C meson/build-rtx fnql_rtx_x86_64
```

The exact architecture suffix follows the configured target. A static renderer
build is useful as a separate linkage gate:

```text
meson setup meson/build-rtx-static -Drenderer-dlopen=false -Drenderer-default=rtx
meson compile -C meson/build-rtx-static
```

On Windows, retail module compatibility still requires the Win32/x86 FnQL
client. An x64 renderer build is valuable for engine and driver validation but
does not make x86 retail modules loadable in a 64-bit process.

Shader source and its generated C payload are one reviewed unit. After changing
an RTX shader, regenerate through the repository's shader tooling and run the
freshness/source tests; do not hand-edit a generated byte array.

## Runtime Smoke Probe

`scripts/rtx_runtime_smoke.py` launches a bounded windowed session against a
legitimate Quake Live Steam installation. The caller supplies the FnQL client
and a retail map name; the script intentionally does not guess a map or stage
sample assets.

The probe:

- verifies Steam app ID `282440` and retail Quake Live install markers;
- passes the retail root through `fs_basepath` and `fs_steampath` without
  writing beneath it;
- creates a unique per-profile `fs_homepath` below `.tmp`;
- forces `r_fullscreen 0` and a bounded custom window;
- disables downloads, sound, optional presentation effects, dynamic BLAS, and
  async overlap and material-name heuristics to isolate renderer activation;
- never connects to a server and never synthesizes a Steam ticket or successful
  authorization decision;
- requests renderer diagnostics, captures a PNG screenshot, and collects all
  discovered console logs;
- validates the complete bounded PNG structure, chunk CRCs, zlib stream,
  scanline filters, requested dimensions, luminance range/variance, and black
  or white clipping before accepting the capture as runtime evidence;
- rejects fatal/Vulkan/validation diagnostics and writes a JSON manifest with
  executable and screenshot sizes/SHA-256 identities plus platform metadata.

Inspect a launch without creating files or starting the engine:

```text
python scripts/rtx_runtime_smoke.py \
  --exe <path-to-fnql-client> \
  --retail-root <path-to-steam-quake-live> \
  --map <retail-map-name> \
  --profile all \
  --plan
```

Run the hardware-independent module/fallback gate first:

```text
python scripts/rtx_runtime_smoke.py \
  --exe <path-to-fnql-client> \
  --retail-root <path-to-steam-quake-live> \
  --map <retail-map-name> \
  --profile raster
```

Then run strict native RT only on an intended RT-capable test machine:

```text
python scripts/rtx_runtime_smoke.py \
  --exe <path-to-fnql-client> \
  --retail-root <path-to-steam-quake-live> \
  --map <retail-map-name> \
  --profile native
```

`--profile native` sets `rtx_rt_mode 2` and `rtx_rt_require 1`. Unsupported
hardware, a capability downgrade, a failed primary dispatch, missing mode
evidence, validation diagnostics, a missing, malformed, mis-sized, uniform, or
heavily clipped capture, timeout, or nonzero process exit all fail the profile.
This strict result must not be replaced by a raster success when reporting
native-RT coverage.

Set `FNQL_RETAIL_QL_PATH` instead of passing `--retail-root` when useful. The
default artifact root is `.tmp/rtx-runtime-smoke`; each invocation adds a UTC
run ID and never deletes or reuses an earlier run.

## Validation Matrix

Before treating an RTX slice as complete, keep these gates distinct:

1. Source-contract tests: API 13 exports, BSP 47/advertisements, conservative
   defaults, hybrid ownership, absence of duplicate FOV correction, and the
   isolated smoke-command contract.
2. Shader tests: every source variant compiles, generated payloads are fresh,
   descriptor bindings match host declarations, and all index/alpha paths are
   bounded.
3. Modular and static builds with strict warnings on each maintained compiler.
4. Raster smoke on non-RT and RT systems. This proves module loading and the
   complete fallback without claiming native coverage.
5. Strict native smoke on reviewed RT-capable hardware and drivers, retaining
   the manifest, console logs, screenshot, exact binary revision, loader, GPU,
   and driver versions.
6. Retail scene review covering BSP 47 advertisements, WebUI/host text, map
   transitions, portals, first-person suppression, entities, translucent and
   alpha-tested content, fog, liquids, resize, `vid_restart`, screenshots, and
   clean shutdown.
7. Regression runs for the GLx and VK selections. RTX must not change their
   output names, source lists, cvar defaults, or runtime behavior.

A plan-only manifest, a successful compile, or a raster-profile capture is not
native-RT runtime evidence.

## Known Limits

- Six-face `screenshot ... cubemap` export is not yet wired through the RTX
  readback path. The RTX renderer rejects that subcommand without writing files
  instead of emitting a misleading single-face image; ordinary screenshots,
  clipboard BMP capture, AVI frames, and levelshots remain available.
- Dynamic BLAS participation remains experimental and default-off. Dynamic
  entities remain raster-visible but do not automatically become native RT
  shadow or reflection geometry.
- Portals, mirrors, stereo/cube views, partial viewports, and later secondary
  scenes are deliberately raster-owned until they have independent history and
  composition contracts.
- Raster-overlay pixels are composited after native tracing and are not
  retroactively relit by the RT pass.
- Surface-light reconstruction is approximate renderer lighting metadata, not
  game state, and remains default-off.
- Optional global fog, enhanced liquid presentation, and scene-linear HDR are
  independent opt-ins. Enabling native RT does not silently enable them.
- A complete maintained GPU/driver promotion corpus has not been established.
  Keep native RT mode explicit and capability-gated until that evidence exists.
