# GLx Legacy Coupling Ledger

This document is the P2 source-coupling ledger for the GLx renderer. It names the
legacy `code/renderer` translation units that are still compiled into the GLx
module and classifies the reason each one remains in the compatibility substrate.

The ledger is intentionally conservative. A source listed here is not a removal
approval; it is a tracked dependency that must either shrink, move behind a
narrower bridge, or stay documented as retained compatibility work. The
promotion check compares this table with the Meson, Make, and MSVC GLx build
inputs and fails if the build files drift or if an unclassified legacy source is
added.

## Ratchet Rules

- The current ratchet budget is 24 legacy renderer C sources.
- New GLx work must not add another `code/renderer/*.c` source to the GLx module
  without updating this ledger and intentionally raising or rebalancing the
  budget.
- When a source is removed from all GLx build files, remove its row and lower the
  budget in `scripts/glx_promotion.py`.
- Runtime promotion still requires zero legacy draw delegation. This ledger is
  the source-level companion to that runtime ownership proof.

## Current Inventory

| Source | Compatibility role | Extraction target |
|---|---|---|
| `code/renderer/tr_animation.c` | Legacy model animation and tag interpolation shared by renderer modules. | Keep behind model/animation data API until GLx owns model submission. |
| `code/renderer/tr_arb.c` | ARB program, FBO, bloom, postprocess, render-scale, and final output substrate. | Continue moving post/output decisions into GLx-owned plans, then split FBO execution. |
| `code/renderer/tr_backend.c` | Backend command execution, screenshot/capture flow, swap, and frame finalization. | Narrow command ABI and move GLx-owned backend commands out of legacy swap flow. |
| `code/renderer/tr_bsp.c` | BSP/world loading and visibility data preparation. | Retain data loading compatibility; expose immutable world packets to GLx. |
| `code/renderer/tr_cmds.c` | Renderer command API, screenshots, and front-end command staging. | Keep engine ABI stable while command payloads become renderer-neutral. |
| `code/renderer/tr_curve.c` | Patch/tessellated surface preparation. | Retain surface generation until GLx owns curved-surface upload/submission. |
| `code/renderer/tr_flares.c` | Flare visibility, readback, and draw ordering. | Add flare proof coverage before extracting or retaining as explicit compatibility. |
| `code/renderer/tr_image.c` | Image loading, texture upload, color-space classification, and texture binding. | Keep asset compatibility; move upload policy through GLx texture/output contracts. |
| `code/renderer/tr_init.c` | OpenGL initialization, extension detection, cvars, and renderer lifecycle glue. | Split GLx bootstrap from legacy OpenGL startup once promotion aliasing is ready. |
| `code/renderer/tr_light.c` | Light grid and entity lighting preparation. | Retain compatibility math until GLx material/object constants own lighting inputs. |
| `code/renderer/tr_main.c` | View setup, culling helpers, and scene traversal support. | Split renderer-neutral scene prep from GLx submission ownership. |
| `code/renderer/tr_marks.c` | Impact mark generation and clipping. | Route prepared mark geometry through GLx dynamic categories. |
| `code/renderer/tr_mesh.c` | MD3/IQM mesh surface preparation and LOD selection. | Keep model compatibility, then feed GLx-native dynamic/model packets. |
| `code/renderer/tr_model.c` | Model registry, loaders, and legacy model handles. | Retain asset ABI and move draw payload handoff behind typed model packets. |
| `code/renderer/tr_model_iqm.c` | IQM loader and animation support. | Keep loader compatibility while separating renderer submission data. |
| `code/renderer/tr_scene.c` | Refdef scene ingestion, entities, polys, dlights, and draw-surface staging. | Preserve engine ABI and make staged scene products renderer-neutral. |
| `code/renderer/tr_shade.c` | Shader-stage iteration, prepared arrays, dynamic lights, fog, and GLx stream handoff. | Continue narrowing to GLx material/state blocks and streamed submissions. |
| `code/renderer/tr_shade_calc.c` | CPU color and texcoord generation for id Tech 3 shader stages. | Retain for compatibility until GLx-native material grammar covers these transforms. |
| `code/renderer/tr_shader.c` | id Tech 3 shader parser, sorting, and stage metadata. | Keep parser compatibility; consume stable GLx material descriptors downstream. |
| `code/renderer/tr_shadows.c` | Stencil shadow-volume preparation and submission bridge. | Keep parity path while GLx stream shadow proof remains tracked. |
| `code/renderer/tr_sky.c` | Sky portal and skybox surface preparation. | Preserve sky semantics while moving final submission to GLx packets. |
| `code/renderer/tr_surface.c` | Surface dispatch for world, entity, poly, sprite, and rail surfaces. | Split dispatch into renderer-neutral products and GLx-owned submissions. |
| `code/renderer/tr_vbo.c` | Legacy static-world VBO packing, queues, and GLx static-world handoff. | Shrink as GLx static arena, packet, and indirect submission paths take over. |
| `code/renderer/tr_world.c` | World surface traversal and visibility enqueueing. | Retain visibility compatibility, then hand typed static-world work to GLx. |
