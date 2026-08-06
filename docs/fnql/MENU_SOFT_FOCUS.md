# In-Game Menu Soft Focus

## Scope

While an in-game menu owns the screen, FnQL replaces the frame behind it with a soft-focus copy of itself, so the menu reads as the foreground instead of competing with a sharp, still-animating scene. The effect exists only while a menu is open: gameplay is never softened, and nothing about the frame the player shoots into changes.

It touches no BSP data, collision, visibility, prediction, snapshots, protocol data, demo data, or VM/native-module interfaces. Retail Quake Live has no equivalent layer, so enabling it cannot change how a retail server evaluates a client.

This replaces an earlier `cl_menuDepthOfField` effect that was implemented only for the OpenGL lineage, was never observed working, and failed silently when it declined. That cvar and its `cl_menuDepthOfFieldTime` companion are gone; a config still setting them will simply have two unknown cvars.

## Cvar

| Cvar | Default | Meaning |
| --- | --- | --- |
| `cl_menuBlur` | `1` | `0` leaves the scene sharp. `0..1` scales both the blur radius and how far the softened copy replaces the sharp frame. |

One cvar gates the whole feature. The fade in and out is a fixed 140 ms and is not configurable: shorter reads as a flicker when a menu is toggled, longer starts to feel like input lag on the menu itself.

Because strength scales radius *and* composite weight together, a partial value is a gently softened frame rather than a cross-fade between a sharp and a blurred copy of the same image, which would read as a ghosted double exposure.

## Trigger

The client owns the decision, in `SCR_DrawScreenField`. The effect is requested when all of the following hold:

- `KEYCATCH_UI` is set and a UI module is loaded;
- the menu is not fullscreen — a fullscreen menu has no scene behind it to soften;
- `cls.state == CA_ACTIVE` — the connect and level-loading screens are not in-game menus.

Note what is *not* included. `KEYCATCH_CGAME` overlays such as the scoreboard are drawn over live gameplay that the player is still reading, so they stay sharp. The WebUI browser draws its own full-surface overlay and is handled separately.

`SCR_UpdateMenuBlurStrength` ramps toward the requested strength on wall-clock time rather than per frame, so the pull into focus takes the same 140 ms at 60 and at 250 fps. A negative `cls.realtime` delta is a timer reset and a delta over a second is a hitch or a restored window; neither counts as elapsed fade time.

The request is issued after the scene *and* the cgame HUD have been drawn and before `UI_REFRESH`, so the HUD is softened along with the world and only the menu itself stays sharp.

## Sampling plan

`code/renderercommon/tr_menu_blur.h` owns the plan. The three renderer backends drive it rather than each inventing a blur, which is what keeps them looking the same.

The plan is a small Gaussian pyramid, not one wide kernel:

1. The finished frame is box-downsampled to 1/2 and then to 1/4. Going straight to 1/4 in a single bilinear step discards three of every four texels, and *which* three changes as the scene animates, so the backdrop crawls. Two exact halvings average every texel instead. On every backend the halving is a plain linear resample, which at an exact 2:1 reduction lands each destination texel centre between four source texels and therefore averages all of them.
2. 1/4 resolution carries four separable horizontal+vertical Gaussian iterations. A texel of spacing there covers four screen pixels, so a wide blur needs few passes.
3. Per-pass sigmas grow as 1:2:3:4 and combine in quadrature to the requested total. The tight first pass removes the high frequencies that would otherwise show the later, sparser taps as banding; the later passes run on already-smooth data, where wider gaps cost nothing.
4. The level is bilinearly resampled back to full resolution and composited with the plan's alpha.

Total sigma is `MENU_BLUR_SIGMA_FRACTION` (1.3%) of render-target *height*, so the softness covers the same share of the screen at 720p and at 4K, and width does not enter it. The pass count is constant, so a fade cannot pop as the ramp crosses a threshold.

### Why the backends exchange sigma, not offsets

The kernels differ. The OpenGL lineage iterates the existing 6-tap binomial ARB program (`BLUR2_FRAGMENT`); Vulkan and RTX iterate the 3-tap linear-sampling kernel that emulates a 5-tap binomial. Their unit-spacing variances are tabulated as `MENU_BLUR_KERNEL_VARIANCE_BINOMIAL6` and `MENU_BLUR_KERNEL_VARIANCE_LINEAR3`, and each backend converts the plan's per-pass sigma into its own tap spacing with `R_MenuBlur_TapSpacing`. Matching sigma is what makes the renderers agree; matching tap offsets would not.

A plan is reported disabled — never an error — for a non-positive or non-finite strength, a strength below the visible-composite floor, a non-positive target size, or a target too small to hold a quarter-resolution level. Every backend treats a disabled plan as "leave the finished frame alone".

## Backends

The effect samples the finished frame, so it cannot run from the frontend. Each renderer queues an `RC_MENU_BLUR` command that executes in draw order and calls `RB_EndSurface` first, so queued geometry has landed on the target before it is read back.

**OpenGL lineage** (`code/renderer`). `FBO_MenuBlur` allocates three framebuffers — one half-resolution step and a ping-pong pair at the level — sized from the plan and rebuilt when the render target changes. The two halvings are `glBlitFramebuffer` with `GL_LINEAR`; the iterations reuse the ARB blur program through `ARB_BlurParams`, which gained a spacing argument for this (bloom passes pass `1.0` and are unchanged). The composite is a fixed-function modulated quad whose vertex alpha carries the strength.

**Vulkan and RTX** (`code/renderervk`, `code/rendererrtx`). Both use one new fragment shader, `menu_blur.frag`. Its tap offset is a push constant, so a zero offset collapses the three taps onto one coordinate — the weights still sum to one — and the same shader becomes the plain bilinear resample the downsample steps and the composite need. The pyramid therefore needs no copy shader of its own.

Three attachments, one render pass, three framebuffers, three descriptors, and three pipelines are created. One render pass object serves all three targets because render-pass compatibility depends on attachment format and sample count, not extent; three *pipelines* are still needed because viewport and scissor are baked into a pipeline in this codebase. `VK_NUM_MENU_BLUR_IMAGES` is budgeted into both `MAX_ATTACHMENTS_IN_POOL` and the combined-image-sampler descriptor pool.

The Vulkan attachments are allocated unconditionally whenever the FBO path is active, not gated on `cl_menuBlur`. The attachment pool is fixed-size and populated at renderer init, where the renderer cannot usefully consult a client cvar that the player may change at any moment. The cost is roughly 3 MB at 1080p and 12 MB at 4K.

Both SPIR-V blobs are checked in beside the shader source in `shaders/spirv/shader_data.c`, generated with the same layout `bin2hex` produces.

## Declining

Every backend reports why it declined, once per distinct reason, at `PRINT_DEVELOPER`. Reasons are configuration rather than failure: no framebuffer path, pyramid not allocated, pipelines not created, or a pyramid that no longer matches the render target. This is a deliberate response to the predecessor effect, which failed silently and so made an unsupported configuration indistinguishable from a disabled cvar.

The effect requires the framebuffer post-processing path. With `r_fbo 0` there is no sampleable copy of the finished frame and the menu stays sharp.

## Validation

`tests/menu_blur_tests.cpp` covers the plan: inert disabled plans for every rejected input including NaN strength and undersized targets; two halvings with truncation that never collapses to zero; total sigma tracking target height and not width; the 1:2:3:4 ramp; strength scaling sigma and alpha together with a constant pass count and clamping above 1; tap-spacing conversion round-tripping through each kernel variance and collapsing to zero on degenerate input; and the property that both kernels reach the same total sigma from the same plan.

`tests/menu_blur_source_tests.py` gates the structure: that all three backends include the shared header and call both plan functions; that each passes its own kernel variance and not the other's; that a single archived cvar gates the effect and the binned depth-of-field path is gone from every file that referenced it; that the trigger excludes fullscreen menus, non-active states, and cgame overlays; that each backend queues a render command and ends its surface first; that the GL path performs exactly two linear blits inside `FBO_MenuBlur`; that both Vulkan backends allocate, budget, and release every object they create; that the shader's weights and tap positions still match the tabulated kernel variance; and that the two Vulkan shader sources are byte-identical.

Runtime promotion still needs a windowed retail-asset check on each renderer: open an in-game menu during live play and confirm the scene and HUD soften while the menu stays sharp, that the fade is smooth in both directions, that `cl_menuBlur 0` leaves the frame untouched, and that intermediate values look softened rather than double-exposed. `r_fbo 0` and a `vid_restart` with the menu open are the two configuration cases worth checking explicitly.
