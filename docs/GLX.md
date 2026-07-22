# GLx Renderer Guide

GLx is FnQL's canonical OpenGL-lineage renderer and the client default. It is the renderer path where OpenGL-family work lands: capability tiers, static-world acceleration, dynamic streaming, material execution, postprocess, output transforms, and GLx-specific diagnostics.

GLx is built in normal modular builds and can be selected explicitly with:

```cfg
seta cl_renderer "glx"
vid_restart
```

## Renderer Selection

`cl_renderer` is latched, so changing it requires `vid_restart`.

- `cl_renderer glx`: default OpenGL-lineage renderer module.
- `cl_renderer vk`: Vulkan raster renderer.
- `cl_renderer rtx`: Vulkan ray-tracing renderer.

Dynamic renderer builds can also use:

```cfg
renderer_switch glx fast
renderer_switch vk fast
```

That command exercises the same shutdown/startup surface used by `vid_restart`, which makes it useful for quick comparison runs.

## Build Availability

Normal Meson and Make modular builds include GLx by default alongside VK and RTX. GLx is the client default.

Useful build selections:

```sh
meson setup meson/build -Drenderer-dlopen=true -Drenderers=glx,vk,rtx
make BUILD_SERVER=0 USE_GLX=1
```

For single-renderer static test builds, select GLx explicitly:

```sh
meson setup meson/build-glx-static -Drenderer-dlopen=false -Drenderer-default=glx
make BUILD_SERVER=0 USE_RENDERER_DLOPEN=0 RENDERER_DEFAULT=glx
```

## Proof And Diagnostics

Use the runtime sweep for structured evidence:

```sh
python scripts/glx_runtime_sweep.py --gate rc-parity --exe path/to/fnql --basepath path/to/game
python scripts/glx_runtime_sweep.py --gate rc-proof --exe path/to/fnql --basepath path/to/game --proof-dir .tmp/glx-proof/windows-x86
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

If the screen is black or the image differs from `vk`, collect `glxinfo`, `glxpostprocess`, `r_speeds 7`, and matching `glx`/`vk` screenshots. Check whether FBO output is ready, whether bloom/output transforms match, and whether the active tier is lower than expected.

If performance regresses, run `rc-parity` or `rc-proof` and compare the generated performance aggregate. Useful counters include draw count, stream upload volume, material binds, static packet misses, same-frame stream wrap rejects, and GPU frame time.

If a driver-specific GLx issue blocks play, set `cl_renderer vk` and run `vid_restart` to use the Vulkan raster renderer.
