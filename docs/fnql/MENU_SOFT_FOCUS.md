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

It is reachable without the console: the WebUI settings overlay carries it as a slider in the **Game → Interface** group (`SECTION_GROUPS.game` in `code/client/webui/fnql-settings.js`), and `CL_WebHost_BuildConfigCvarJson` publishes it so the overlay can read the current value back. The slider's `min`/`max` mirror the cvar's `Cvar_CheckRange` bounds and its `0.05` step resolves to the two decimals `formatRange` writes; a source gate holds those three in agreement, since a slider that cannot reach the cvar's bounds is the failure that would otherwise go unnoticed.

Because strength scales radius *and* composite weight together, a partial value is a gently softened frame rather than a cross-fade between a sharp and a blurred copy of the same image, which would read as a ghosted double exposure.

## Trigger

The client owns the decision, in `SCR_DrawScreenField`. The effect is requested when all of the following hold:

- `KEYCATCH_UI` is set and a UI module is loaded;
- the menu is not fullscreen — a fullscreen menu has no scene behind it to soften;
- `cls.state == CA_ACTIVE` — the connect and level-loading screens are not in-game menus.

The request also carries the same `browserSuppressUiRefresh` term that gates the menu's own draw. Without it the whole pyramid ran every frame while the browser owned the surface, with nothing sharp ever drawn over the softened frame.

Note what is *not* included. `KEYCATCH_CGAME` overlays such as the scoreboard are drawn over live gameplay that the player is still reading, so they stay sharp. cgame draws the scene, the HUD and its overlays in a single `CG_DRAW_ACTIVE_FRAME` call, so a request queued after it would soften the overlay itself rather than the frame behind it. Softening the scene under the *intermission* scoreboard is possible in principle, but only from a composite issued between the 3D pass and the HUD — the slot `FBO_MotionBlur` / `vk_motion_blur` already occupy at the tail of `RB_DrawSurfs`. That needs the strength plumbed through `trRefdef_t` and five guards: worldless views, portal sub-views, cubemap-screenshot views, the Vulkan screenmap command duplicate, and a once-per-frame latch. Not implemented.

### Known limitation: inert on Vulkan at default settings

**On `code/renderervk` and `code/rendererrtx` the composite cannot run in-game under the shipped default `r_bloom 1`.** `r_bloom` defaults to `1` on both Vulkan backends and `0` on the OpenGL lineage. `RB_StretchPic` calls `vk_bloom()` on *every* 2D quad, so the cgame HUD's first quad runs bloom, which finishes by entering `RENDER_PASS_POST_BLOOM`; nothing puts the index back. `RC_MENU_BLUR` is queued after the HUD, so `vk_menu_blur` reaches its `renderPassIndex != RENDER_PASS_MAIN` guard and returns.

Fixing it means letting the composite resume whichever pass it interrupted, which needs a second composite pipeline built against `vk.render_pass.post_bloom` and interacts with that pass declaring its MSAA colour attachment `loadOp = LOAD` / `storeOp = DONT_CARE`. Not done. Until it is, `r_bloom 0` is the only Vulkan configuration in which the effect is visible.

### Why `backEnd.doneSurfaces` is not the right guard

Dropping that test so 2D-only screens could be softened faulted the device inside the ICD on the first connect screen. The flag was not protecting what it appeared to.

`vk.renderPassIndex` is **sticky state, not a recording flag**: `vk_end_render_pass` deliberately leaves it set, and `vk_end_frame` re-arms it to `RENDER_PASS_MAIN` *after* `qvkEndCommandBuffer` and `qvkQueueSubmit`. So it reads `MAIN` in exactly the state where recording anything is undefined. `backEnd.doneSurfaces` is raised in `RB_DrawSurfs` and cleared only once the frame has been presented, so it doubled *by accident* as "a frame is open" — and removing it removed the only thing noticing the frame had already ended.

Two mid-client-frame drains are live in this tree and both produce that state: the one level of re-entrant `SCR_UpdateScreen` the client permits (the UI VM can call it from inside `UI_DRAW_CONNECT_SCREEN`), and `R_IssuePendingRenderCommands` from the retained host font atlas. Either ends and submits the command buffer mid-frame; the composite then ends a render pass on a dead command buffer.

`vk_menu_blur` now tests liveness directly — `vk.frame_count`, plus the recording pass itself on `renderervk` — the precondition `vk_capture_liquid_scene` already uses for the same end-detour-resume trick. Correct by construction, with no assumption about driver behaviour.

The Vulkan composite also re-pushes the MVP before returning. `vk_menu_blur_draw` binds through `vk.pipeline_layout_post_process`, which is not push-constant compatible with `vk.pipeline_layout`, so the 64-byte MVP is undefined afterwards. This is the only post-process detour that runs while `backEnd.projection2D` is already set, so the next `RB_StretchPic` skips `RB_SetGL2D` and nothing else would restore it.

`SCR_UpdateMenuBlurStrength` ramps toward the requested strength on wall-clock time rather than per frame, so the pull into focus takes the same 140 ms at 60 and at 250 fps. A negative `cls.realtime` delta is a timer reset and a delta over a second is a hitch or a restored window; neither counts as elapsed fade time.

The request is issued after the scene *and* the cgame HUD have been drawn and before `UI_REFRESH`, so the HUD is softened along with the world and only the menu itself stays sharp.

### What cannot be softened, and why

`KEYCATCH_CGAME` overlays — the scoreboard and the spectator join page — stay sharp. They are drawn over live gameplay the player is still reading, and more decisively, cgame draws the scene, the HUD, and its overlays in a single `CG_DRAW_ACTIVE_FRAME` call, so a request queued after it would soften the overlay itself rather than the frame behind it. That would need a cgame-side hook that does not exist.

**The connection dialog and the level-loading screen** were tried and withdrawn. Both are 2D-only frames, and every backend gates the composite on `backEnd.doneSurfaces` — "a 3D pass has run this frame". Dropping that gate so those screens could be softened faulted the device inside the ICD on the first such frame, before the loading screen was even reached. The flag is doing more than naming a scene: it is the only signal a backend has that the render target holds something this composite may read back, and nothing else in any of the three renderers post-processes a frame without one. Re-enabling it needs that invariant established with the Vulkan validation layers on, not inferred from reading.

**The console** was tried and withdrawn as well. It is the only layer that finishes the frame's own post-processing, calling `re.FinishBloom` from inside `Con_DrawSolidConsole` so it is not itself bloomed, and that call cannot be moved out of the way:

- With the request issued **before** it, bloom runs between the softening and the console. The descriptor sets its blend pass leaves bound made the console's own draw read the wrong descriptors, and the console did not appear at all.
- With bloom finished **first**, the Vulkan backends are left in `RENDER_PASS_POST_BLOOM`, which targets an attachment the pyramid does not sample.
- Either ordering also composites over live gameplay for the 140 ms the request takes to fade out after the console closes, which reads as the HUD — the warmup ready-up prompt most visibly — briefly losing opacity for no reason the player can see.

It cannot simply share the menu's insertion point either: it draws over the menu and the WebUI browser surface, both of which land after that point, so the layers underneath it would stay sharp. Softening only the console's own rectangle would avoid the gameplay-dimming problem, but `re.DrawMenuBlur` composites the whole frame and the Vulkan composite pipelines bake their scissor, so the rect would have to become dynamic state in both of them.

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

`vk_menu_blur` ends by setting `vk.cmd->last_pipeline` to `VK_NULL_HANDLE`, and that is load-bearing rather than a hint. `vk_menu_blur_draw` binds descriptor set 0 through `vk.pipeline_layout_post_process`, and binding through a layout incompatible with `vk.pipeline_layout` disturbs the sets bound for it. Nulling the cache is what forces the next draw to rebind them. Handing the frame's pipeline back instead — which looks like the tidier mirror of what `vk_bloom` does for its own binds — lets that draw proceed with disturbed descriptor sets and faults the device inside the ICD.

The Vulkan attachments are allocated unconditionally whenever the FBO path is active, not gated on `cl_menuBlur`. The attachment pool is fixed-size and populated at renderer init, where the renderer cannot usefully consult a client cvar that the player may change at any moment. The cost is roughly 3 MB at 1080p and 12 MB at 4K.

Both SPIR-V blobs are checked in beside the shader source in `shaders/spirv/shader_data.c`, generated with the same layout `bin2hex` produces.

## Declining

Every backend reports why it declined, once per distinct reason, at `PRINT_DEVELOPER`. Reasons are configuration rather than failure: no framebuffer path, pyramid not allocated, pipelines not created, or a pyramid that no longer matches the render target. This is a deliberate response to the predecessor effect, which failed silently and so made an unsupported configuration indistinguishable from a disabled cvar.

The effect requires the framebuffer post-processing path. With `r_fbo 0` there is no sampleable copy of the finished frame and the menu stays sharp.

## Validation

`tests/menu_blur_tests.cpp` covers the plan: inert disabled plans for every rejected input including NaN strength and undersized targets; two halvings with truncation that never collapses to zero; total sigma tracking target height and not width; the 1:2:3:4 ramp; strength scaling sigma and alpha together with a constant pass count and clamping above 1; tap-spacing conversion round-tripping through each kernel variance and collapsing to zero on degenerate input; and the property that both kernels reach the same total sigma from the same plan.

`tests/menu_blur_source_tests.py` gates the structure: that all three backends include the shared header and call both plan functions; that each passes its own kernel variance and not the other's; that a single archived cvar gates the effect and the binned depth-of-field path is gone from every file that referenced it; that the in-game menu trigger excludes fullscreen menus and non-active states, that no trigger consults `KEYCATCH_CGAME`, and that each layer has its own ramp; that exactly one layer is requested, with no connect, loading or console layer and no client-side `re.FinishBloom` call crept back in; that all three backends still gate on `backEnd.doneSurfaces` and both Vulkan backends null `last_pipeline` after their post-process binds; that each backend queues a render command and ends its surface first; that the GL path performs exactly two linear blits inside `FBO_MenuBlur`; that both Vulkan backends allocate, budget, and release every object they create; that the shader's weights and tap positions still match the tabulated kernel variance; and that the two Vulkan shader sources are byte-identical.

Runtime promotion still needs a windowed retail-asset check on each renderer: open an in-game menu during live play and confirm the scene and HUD soften while the menu stays sharp, that the fade is smooth in both directions, that `cl_menuBlur 0` leaves the frame untouched, and that intermediate values look softened rather than double-exposed. Confirm too that the layers deliberately left out stay out: connecting to a server and dropping the console during live play must both leave the frame completely untouched, and the warmup ready-up prompt must hold full opacity throughout. `r_fbo 0` and a `vid_restart` with the menu open are the two configuration cases worth checking explicitly.
