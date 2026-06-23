# GLx Renderer Guide

GLx is FnQL's canonical OpenGL-lineage renderer. It is the renderer path where new OpenGL-family work lands: capability tiers, renderer switching proof, static-world acceleration, dynamic streaming, material execution, postprocess parity, output transforms, and GLx-specific diagnostics.

The release default still remains `opengl` until the promotion gate passes. That is deliberate compatibility policy, not a statement that GLx is a side experiment. GLx is built in normal modular builds and can be selected explicitly with:

```cfg
seta cl_renderer "glx"
vid_restart
```

Use `opengl` when you need the current legacy default for comparison or rollback. Use `glx` when validating the OpenGL-lineage replacement path, collecting proof artifacts, or testing the renderer that is intended to become the migration target after the promotion gate is green.

## Renderer Selection

`cl_renderer` is latched, so changing it requires `vid_restart`.

- `cl_renderer opengl`: legacy OpenGL renderer and current compatibility default.
- `cl_renderer glx`: canonical OpenGL-lineage renderer module.
- `cl_renderer vulkan`: Vulkan backend.
- `cl_renderer opengl2`: legacy renderer2 path when the build includes it.

Dynamic renderer builds can also use:

```cfg
renderer_switch glx fast
renderer_switch opengl fast
```

That command exercises the same shutdown/startup surface used by `vid_restart`, which makes it useful for quick comparison runs.

## Build Availability

Normal Meson modular builds include GLx by default, alongside the single client executable. Legacy Make modular builds also include GLx by default. The renderer remains selectable rather than forced as the default until `python scripts/glx_promotion.py --proof-root <reviewed-glx-proof-root> --require-ready` reports `ready`.

Useful build selections:

```sh
meson setup meson/build -Drenderer-dlopen=true -Drenderers=opengl,glx,vulkan
make BUILD_SERVER=0 USE_GLX=1
```

For single-renderer static test builds, select GLx explicitly:

```sh
meson setup meson/build-glx-static -Drenderer-dlopen=false -Drenderer-default=glx
make BUILD_SERVER=0 USE_RENDERER_DLOPEN=0 RENDERER_DEFAULT=glx
```

## Migration Notes

The intended migration is staged:

1. Build GLx everywhere the OpenGL-lineage renderer is supported.
2. Keep `opengl` as the current default while GLx proof artifacts are reviewed.
3. Use `glx` explicitly for parity, performance, ownership, and driver validation.
4. After the promotion gate passes, allow `opengl` to become a migration alias for GLx.
5. Keep a rollback package with the legacy OpenGL renderer for at least one release cycle.

Saved configs using `opengl` stay valid throughout the migration. Before promotion they select the legacy renderer. After promotion they may select GLx through the alias, with rollback packages preserving the old renderer for driver-specific regressions.

## Proof And Diagnostics

Use the runtime sweep for structured evidence:

```sh
python scripts/glx_runtime_sweep.py --gate rc-parity --exe path/to/fnql --basepath path/to/game
python scripts/glx_runtime_sweep.py --gate rc-proof --exe path/to/fnql --basepath path/to/game --proof-dir .tmp/glx-proof/windows-x64
```

Inside the game, these commands are the first things to capture in a renderer bug report:

```cfg
glxinfo
glxmaterial
glxpostprocess
glxstaticworld 8
seta r_speeds "7"
```

`glxinfo` identifies the active product tier and ownership counters. `glxmaterial` reports material compiler and bind state. `glxpostprocess` records FBO, bloom, tone/output, screenshot, and capture state. `glxstaticworld 8` prints static packet, arena, draw, and MDI evidence.

## Troubleshooting

If GLx does not load, confirm the build produced the GLx module for your platform and architecture. A requested GLx load fails closed, so a missing GLx library should be fixed as a packaging/build issue instead of silently falling back to legacy OpenGL.

If the screen is black or the image differs from `opengl`, collect `glxinfo`, `glxpostprocess`, `r_speeds 7`, and a matching `opengl` screenshot. Check whether FBO output is ready, whether bloom/output transforms match, and whether the active tier is lower than expected.

If performance regresses, run `rc-parity` or `rc-proof` and compare the generated performance aggregate. Useful counters include draw count, stream upload volume, material binds, static packet misses, same-frame stream wrap rejects, and GPU frame time.

If a driver-specific issue blocks play, set `cl_renderer opengl` and run `vid_restart` to return to the legacy OpenGL renderer in current builds. In a promoted release, use the rollback package described in [GLX_PROMOTION.md](fnql/GLX_PROMOTION.md).
