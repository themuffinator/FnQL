# GLx Tests

`fnql_glx_logic_tests` covers renderer-independent GLx classification logic. It does not create an OpenGL context, include `code/renderer`, or require game assets.

The harness currently checks:

- GLSL material permutation key selection for RC-supported single-texture, multitexture, texmod, environment, depth-fragment, and fog shapes.
- Compilation from prepared shader-stage language into `MaterialIR` and material state plans, including preserved sort values and named unsupported-reason bits.
- Full prepared id Tech 3 stage-language keys for `rgbGen`, `alphaGen`, their wave functions, `tcGen`, ordered `tcMod` chains, `tcMod stretch` wave functions, detail stages, fog color adjustment, blend/depth/alpha-test state flags, dynamic-light, screen-map, and video-map cases, including proof that compact-key matches still produce distinct stage-language permutations.
- Rejection of unsupported multitexture combine modes.
- Stream material-gate behavior for the RC allowlist, including explicit multitexture and depth-fragment gates.
- The legacy shader collapse path not blocking compatible `depthFragment` base stages from becoming multitexture stages.
- Explicit dynamic-light, screen-map, and video-map stream gate behavior, including RC-profile dynamic-light/scissor `auto` promotion and screen/video guard coverage.
- Explicit shadow-volume, beam, and fullscreen postprocess draw-array stream gate behavior outside the material-key system.
- Dynamic-scene stream category normalization for entity, particle, poly, mark, weapon, UI, beam, and special-pass metrics.
- RC runtime-sweep profiles enabling state-only dynamic submission gates and the dynamic-light `auto` gate while keeping screen-map and video-map material gates off.
- The frozen RC/stress sweep profiles matching the runtime `r_glxProfile` table in `code/rendererglx/glx_module.cpp`.
- The RC profile promoting shipped static-world arena, dispatcher, packet-batch, multidraw, and capability-gated indirect/MDI span paths while leaving compact MDI uploads in the stress profile.
- The checked-in GLx feature-closure matrix using only exact `covered`, `partially covered`, and `missing` statuses with no ambiguous rows.
- The official GLx proof corpus covering stock maps, high-geometry maps, shader-heavy maps, fog-heavy maps, modern-map stress scenes, particle-heavy demos, UI/HUD-sensitive scenes, and named screenshot, demo-playback, HUD, shadow, bloom, and cel-shading parity suites.
- Hard proof-gate policy requiring reviewed screenshot baselines and compared performance baselines for `rc-proof`.
- Release-proof validation requiring passing non-dry-run `rc-smoke`, `rc-parity`, and `rc-proof` manifests for the blocking Windows/Linux runtime matrix before tagged release packaging.
- Promotion-gate validation that keeps `opengl` from becoming a GLx alias until the feature matrix, runtime proof, ownership proof, migration notes, and rollback policy are all green.
- Built-in global and per-tier performance budgets for draw pressure, upload volume, fallback/error counters, shader binds, static packet misses, same-frame stream wrap rejects, and GPU frame time.
- Generated post-shader feature keys and final-pass eligibility covering both scene-linear output and display-referred SDR legacy gamma.
- The broad `r_glxStreamDrawKeyMode 2` developer escape hatch staying behind hard multitexture and depth-fragment gates.
- Capability version/extension parsing and tier selection.
- Dynamic-stream strategy selection across persistent, map-range, and orphan/subdata fallbacks.
- Static-world packet run classification and draw-policy gating.

`fnql_glx_header_boundary` scans the pure GLx headers, the renderer-common GLx API/forwarding bridge, and the renderer facade. It fails if pure logic picks up legacy renderer includes, GL object typedefs, `qgl` references, renderer ABI types, or module/local implementation headers. It also keeps the bridge headers from growing a `tr_public.h`, `qgl`, GL typedef, or shutdown-enum dependency, and keeps `tr_glx_compat.h` from including back into `code/rendererglx`.

Build and run it with Meson:

```sh
meson compile -C meson/build fnql_glx_logic_tests
meson test -C meson/build fnql_glx_logic fnql_glx_header_boundary --print-errorlogs
```
