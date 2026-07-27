from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RENDERERS = ("renderer", "renderervk", "rendererrtx")
VULKAN_RENDERERS = ("renderervk", "rendererrtx")


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str:
    signature = re.compile(
        rf"^[^\n;]*\b{re.escape(name)}\s*\([^;{{]*\)\s*\{{", re.MULTILINE | re.DOTALL
    )
    match = signature.search(source)
    if match is None:
        return ""
    open_brace = source.find("{", match.start())
    depth = 0
    for index in range(open_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start() : index + 1]
    return source[match.start() :]


def arb_program(source: str) -> str:
    start = source.index("static const char *underwaterFP = {")
    return source[start : source.index("};", source.index('"END \\n"', start))]


class UnderwaterViewModelTests(unittest.TestCase):
    def test_shared_model_is_renderer_independent_and_bounded(self) -> None:
        model = read("code/renderercommon/tr_underwater.h")

        self.assertIn('#include "../qcommon/q_shared.h"', model)
        self.assertNotIn("vk.h", model)
        self.assertNotIn("tr_local.h", model)

        self.assertIn(
            "#define UNDERWATER_CONTENTS_MASK "
            "( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA )",
            model,
        )
        self.assertIn("#define UNDERWATER_WAVE_OCTAVES 3", model)
        self.assertIn("#define UNDERWATER_REFERENCE_VIEW_HEIGHT 1080.0f", model)
        self.assertIn("#define UNDERWATER_WARP_BASE_PIXELS 11.0f", model)
        self.assertIn("#define UNDERWATER_WARP_SCALE_MAX 2.0f", model)
        self.assertIn("#define UNDERWATER_DISPERSION_MAX 0.22f", model)
        self.assertIn("#define UNDERWATER_FOG_MAX_OPACITY 0.92f", model)
        self.assertIn("#define UNDERWATER_VIGNETTE_INNER 0.55f", model)
        self.assertIn("#define UNDERWATER_VIGNETTE_OUTER 1.30f", model)
        self.assertIn("#define UNDERWATER_SAMPLE_MIN 0.002f", model)
        self.assertIn("#define UNDERWATER_SAMPLE_MAX 0.998f", model)
        self.assertIn("#define UNDERWATER_TRANSITION_MSEC 180.0f", model)
        self.assertIn("#define UNDERWATER_TRANSITION_RESET_MSEC 500", model)

    def test_octave_amplitudes_sum_to_one_and_directions_are_unit_length(self) -> None:
        """The displacement bound in every shader assumes exactly this."""
        model = read("code/renderercommon/tr_underwater.h")
        block = model[model.index("waveDirAmp[UNDERWATER_WAVE_OCTAVES]") :]
        block = block[: block.index("};")]
        rows = re.findall(r"\{\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f\s*\}", block)
        self.assertEqual(len(rows), 3)

        total = 0.0
        for x, y, z in rows:
            magnitude = (float(x) ** 2 + float(y) ** 2) ** 0.5
            total += magnitude
            self.assertEqual(float(z), 0.0)
        self.assertAlmostEqual(total, 1.0, places=5)

    def test_octave_speeds_share_the_wrapped_time_period(self) -> None:
        model = read("code/renderercommon/tr_underwater.h")
        block = model[model.index("waveK[UNDERWATER_WAVE_OCTAVES]") :]
        block = block[: block.index("};")]
        speeds = [
            float(row[3])
            for row in re.findall(
                r"\{\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f\s*\}",
                block,
            )
        ]
        self.assertEqual(len(speeds), 3)
        for speed in speeds:
            # Every speed must be a multiple of 0.05 rad/s so 40*pi is a common
            # period and the wrapped upload cannot introduce a phase jump.
            self.assertAlmostEqual(round(speed / 0.05), speed / 0.05, places=6)
        self.assertIn("#define UNDERWATER_WAVE_TIME_PERIOD 125.66370614", model)

    def test_densest_medium_wins_and_lava_warps_hardest(self) -> None:
        model = read("code/renderercommon/tr_underwater.h")
        classify = function_body(model, "R_UnderwaterClassifyContents")
        density = function_body(model, "R_UnderwaterContentsDensity")
        warp = function_body(model, "R_UnderwaterContentsWarpScale")

        self.assertLess(classify.index("CONTENTS_LAVA"), classify.index("CONTENTS_SLIME"))
        self.assertLess(classify.index("CONTENTS_SLIME"), classify.index("CONTENTS_WATER"))
        self.assertLess(density.index("CONTENTS_LAVA"), density.index("CONTENTS_SLIME"))

        # Absorption strictly increases with how thick the medium is.
        densities = [float(value) for value in re.findall(r"return ([\d.]+)f;", density)]
        self.assertEqual(len(densities), 3)
        self.assertGreater(densities[0], densities[1])
        self.assertGreater(densities[1], densities[2])

        # Warp is not monotonic: lava stands in for heat shimmer and moves most,
        # while slime is thicker than water and refracts less.
        warps = [float(value) for value in re.findall(r"return ([\d.]+)f;", warp)]
        self.assertEqual(len(warps), 3)
        self.assertEqual(max(warps), warps[0])
        self.assertLess(warps[1], warps[2])

    def test_ramp_is_scene_time_driven_and_resets_conservatively(self) -> None:
        model = read("code/renderercommon/tr_underwater.h")
        advance = function_body(model, "R_UnderwaterAdvanceView")

        # A backwards seek, a first sample, or an oversized gap snaps instead of
        # replaying a ramp from an unrelated moment.
        self.assertIn("!state->valid || elapsed < 0", advance)
        self.assertIn("elapsed > UNDERWATER_TRANSITION_RESET_MSEC", advance)
        # A repeated submission at the same scene time must not advance twice.
        self.assertIn("if ( elapsed > 0 ) {", advance)
        self.assertIn("UNDERWATER_TRANSITION_MSEC", advance)
        self.assertIn("Com_Clamp( 0.0f, 1.0f,", advance)

    def test_medium_color_leaves_the_output_transform_domain(self) -> None:
        model = read("code/renderercommon/tr_underwater.h")
        scene_color = function_body(model, "R_UnderwaterSceneColor")

        self.assertIn("scale = ( outputScale > 0.0f ) ? 1.0f / outputScale : 1.0f;", scene_color)
        self.assertIn("R_UnderwaterSrgbToLinear", scene_color)


class UnderwaterViewFeedTests(unittest.TestCase):
    def test_record_stays_outside_gameplay_state(self) -> None:
        types = read("code/renderercommon/tr_types.h")
        public = read("code/renderercommon/tr_public.h")

        self.assertIn("} underwaterView_t;", types)
        self.assertIn("void	(*SetUnderwaterView)( const underwaterView_t *view );", public)
        # The record must not gain an origin, a shader handle, or anything else
        # that would make it look like gameplay state.
        record = types[types.index("typedef struct {", types.index("underwaterView_t") - 400) :]
        record = record[: record.index("} underwaterView_t;")]
        self.assertEqual(record.count(";"), 2)
        self.assertIn("int		contents;", record)
        self.assertIn("int		time;", record)

    def test_ref_api_version_was_bumped_for_the_new_export(self) -> None:
        public = read("code/renderercommon/tr_public.h")
        self.assertIn("#define	REF_API_VERSION		14", public)

    def test_client_samples_the_eye_and_skips_non_world_scenes(self) -> None:
        client = read("code/client/cl_cgame.cpp")
        update = function_body(client, "CL_UpdateUnderwaterView")

        self.assertIn("CM_PointContents( refdef->vieworg, 0 ) & MASK_WATER", update)
        self.assertIn("refdef->rdflags & RDF_NOWORLDMODEL", update)
        self.assertIn("refdef->rdflags & RDF_HYPERSPACE", update)
        self.assertIn("CL_LiquidOriginValid( refdef->vieworg )", update)
        self.assertIn("re.SetUnderwaterView( &view );", update)
        # The feed is fed before the scene it describes is rendered.
        self.assertLess(
            client.index("CL_UpdateUnderwaterView( refdef );"),
            client.index("re.RenderScene( refdef );"),
        )
        # The feed is skipped entirely while the layer is off, so a disabled
        # renderer costs no collision queries.
        self.assertIn("if ( !CL_UnderwaterViewFeedEnabled() ) {", update)
        enabled = function_body(client, "CL_UnderwaterViewFeedEnabled")
        self.assertIn('Cvar_VariableIntegerValue( "r_underwater" ) > 0', enabled)
        self.assertIn("re.SetUnderwaterView &&", enabled)

    def test_every_renderer_exports_and_ages_the_sample(self) -> None:
        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                init = read(f"code/{renderer}/tr_init.c")
                scene = read(f"code/{renderer}/tr_scene.c")
                local = read(f"code/{renderer}/tr_local.h")

                self.assertIn("re.SetUnderwaterView = RE_SetUnderwaterView;", init)
                self.assertIn('#include "../renderercommon/tr_underwater.h"', local)
                self.assertIn("int			underwaterContents;", local)
                self.assertIn("underwaterStrength;", local)

                copy = function_body(scene, "R_CopyUnderwaterViewToRefdef")
                # Cleared first, so a scene that does not qualify can never
                # inherit the previous scene's medium.
                self.assertLess(
                    copy.index("tr.refdef.underwaterStrength = 0.0f;"),
                    copy.index("R_UnderwaterAdvanceView"),
                )
                self.assertIn("R_UnderwaterResetView( &r_underwaterState );", copy)
                self.assertIn("age <= UNDERWATER_TRANSITION_RESET_MSEC", copy)

                # A HUD/model scene gets no medium but must not touch the ramp:
                # cgame submits one after most world views, so resetting there
                # would invalidate the state every frame and make every world
                # scene snap to full strength instead of ramping.
                hud_guard = copy.index("tr.refdef.rdflags & RDF_NOWORLDMODEL")
                reset = copy.index("R_UnderwaterResetView( &r_underwaterState );")
                self.assertLess(reset, hud_guard)
                hud_block = copy[hud_guard : copy.index("}", hud_guard)]
                self.assertNotIn("R_UnderwaterResetView", hud_block)
                self.assertLess(
                    scene.index("R_CopyUnderwaterViewToRefdef( tr.refdef.time );"),
                    len(scene),
                )

                setter = function_body(scene, "RE_SetUnderwaterView")
                self.assertIn("view->contents & UNDERWATER_CONTENTS_MASK", setter)


class UnderwaterViewControlTests(unittest.TestCase):
    def test_cvars_are_registered_default_off_and_range_checked(self) -> None:
        expected = (
            ('ri.Cvar_Get( "r_underwater", "0", CVAR_ARCHIVE_ND | CVAR_LATCH )',
             'ri.Cvar_CheckRange( r_underwater, "0", "1", CV_INTEGER );'),
            ('ri.Cvar_Get( "r_underwaterWarp", "1.0", CVAR_ARCHIVE_ND )',
             'ri.Cvar_CheckRange( r_underwaterWarp, "0", "2", CV_FLOAT );'),
            ('ri.Cvar_Get( "r_underwaterDispersion", "0.35", CVAR_ARCHIVE_ND )',
             'ri.Cvar_CheckRange( r_underwaterDispersion, "0", "1", CV_FLOAT );'),
            ('ri.Cvar_Get( "r_underwaterFog", "1.0", CVAR_ARCHIVE_ND )',
             'ri.Cvar_CheckRange( r_underwaterFog, "0", "1", CV_FLOAT );'),
            ('ri.Cvar_Get( "r_underwaterVignette", "0.35", CVAR_ARCHIVE_ND )',
             'ri.Cvar_CheckRange( r_underwaterVignette, "0", "1", CV_FLOAT );'),
        )
        for renderer in RENDERERS:
            init = read(f"code/{renderer}/tr_init.c")
            for registration, check in expected:
                with self.subTest(renderer=renderer, cvar=registration):
                    self.assertIn(registration, init)
                    self.assertIn(check, init)

    def test_settings_overlay_exposes_the_controls(self) -> None:
        script = read("code/client/webui/fnql-settings.js")
        snapshot = read("code/client/cl_webui.cpp")
        for name in (
            "r_underwater",
            "r_underwaterWarp",
            "r_underwaterDispersion",
            "r_underwaterFog",
            "r_underwaterVignette",
        ):
            with self.subTest(cvar=name):
                self.assertIn(f"'{name}'", script)
                self.assertIn(f'"{name}"', snapshot)
        self.assertIn("{ name: 'r_underwater', title: 'Underwater View', type: 'bool', restart: 'video'", script)


class UnderwaterViewBackendParityTests(unittest.TestCase):
    def test_vulkan_shader_is_identical_across_both_vulkan_renderers(self) -> None:
        first = read("code/renderervk/shaders/underwater.frag")
        second = read("code/rendererrtx/shaders/underwater.frag")
        self.assertEqual(first, second)

    def test_vulkan_shader_carries_the_shared_constants(self) -> None:
        shader = read("code/renderervk/shaders/underwater.frag")

        self.assertIn("uniform sampler2D scene_color", shader)
        self.assertIn("uniform sampler2D scene_depth", shader)
        self.assertIn("UnderwaterPushConstants", shader)
        # Sample bounds, edge falloff thresholds, and the smoothstep curve must
        # match renderercommon/tr_underwater.h literally.
        self.assertIn("min(uv, vec2(0.002))", shader)
        self.assertIn("max(uv, vec2(0.998))", shader)
        self.assertIn("(radius - 0.55) / (1.30 - 0.55)", shader)
        self.assertIn("edge * edge * (3.0 - 2.0 * edge)", shader)
        self.assertIn("1.0 - exp(-pc.depth.x * scene_distance)", shader)
        self.assertIn("min(amount * pc.tune.w, pc.fog.w)", shader)
        self.assertIn("mix(color, pc.fog.rgb, alpha)", shader)
        # Three separate colour taps are what produces the dispersion split.
        self.assertEqual(shader.count("texture(scene_color,"), 3)

    def test_arb_program_carries_the_same_constants(self) -> None:
        program = arb_program(read("code/renderer/tr_arb.c"))

        self.assertIn('"PARAM consts = { 0.0, 1.0, 0.002, 0.998 }; \\n"', program)
        self.assertIn('"PARAM vignette = { 0.55, 1.30, 0.0, 0.0 }; \\n"', program)
        self.assertIn('"PARAM curve = { -2.0, 3.0, 2.0, -1.0 }; \\n"', program)
        # exp2 with -log2(e) reproduces the GLSL exp() absorption exactly.
        self.assertIn('"PARAM expScale = { -1.442695, 0.0001, 0.0, 0.0 }; \\n"', program)
        self.assertEqual(program.count("texture[0], 2D"), 3)
        self.assertEqual(program.count("texture[1], 2D"), 1)
        # The bounds widen to the fragment's own coordinate inside the margin so
        # a zero displacement is preserved exactly.
        self.assertIn('"MIN lo.xy, uv, consts.z; \\n"', program)
        self.assertIn('"MAX hi.xy, uv, consts.w; \\n"', program)
        # ARB fragment programs guarantee only 16 temporaries.
        temps = re.search(r'"TEMP ([^"]*); \\n"', program)
        self.assertIsNotNone(temps)
        assert temps is not None
        self.assertLessEqual(len(temps.group(1).split(",")), 16)

    def test_opengl_pass_is_ordered_bounded_and_failure_local(self) -> None:
        arb = read("code/renderer/tr_arb.c")
        backend = read("code/renderer/tr_backend.c")
        draw = function_body(arb, "FBO_DrawUnderwater")

        # An unavailable program disables only this layer for the session rather
        # than failing the whole renderer's program set.
        compile_start = arb.index("ARB_CompileProgramInternal( Fragment, underwaterFP")
        block_start = arb.rindex("underwaterProgramCompiled = qfalse;", 0, compile_start)
        compile_block = arb[block_start : arb.index("\n\t}", compile_start) + 3]
        self.assertIn("if ( r_underwater && r_underwater->integer ) {", compile_block)
        self.assertIn("programs[ UNDERWATER_FRAGMENT ], qfalse )", compile_block)
        self.assertNotIn("return qfalse", compile_block)
        self.assertIn("r_underwater is disabled for this renderer session", compile_block)
        self.assertIn("!underwaterProgramCompiled", arb)
        # r_underwater must keep the depth attachment alive under multisampling.
        self.assertIn("( r_underwater && r_underwater->integer ) ||\n\t\t\tR_CelShadingWorldActive()", arb)

        for guard in (
            "R_ViewPassIsPortal( &backEnd.viewParms )",
            "backEnd.screenshotCubeActive",
            "glConfig.stereoEnabled",
            "R_LiquidViewportCoversTarget",
            "underwaterFrame == tr.frameCount",
            "RDF_NOWORLDMODEL",
        ):
            with self.subTest(guard=guard):
                self.assertIn(guard, draw)
        # Ping-pong, never sampling the attachment being written.
        self.assertIn("FBO_BlitMS( qfalse );", draw)
        self.assertIn("destinationIndex = sourceIndex == 0 ? 1 : 0;", draw)
        # The multisample resolve is gated on the pending-resolve flag, not on
        # the multisample mode: the global-fog sidecar composites immediately
        # before this and leaves its result in the resolved buffer, so an
        # unconditional re-resolve would overwrite that layer.
        self.assertIn("if ( blitMSfbo ) {", draw)
        self.assertNotIn("if ( frameBufferMultiSampling ) {", draw)
        # Depth is optional: a missing copy zeroes the density instead of
        # dropping the whole layer.
        self.assertIn("density = depthReady ? R_UnderwaterContentsDensity( contents ) : 0.0f;", draw)
        self.assertIn("R_UnderwaterSceneColor( contents, outputScale, sceneLinear, sceneColor );", draw)

        debug = backend.rindex("RB_DebugGraphics();")
        fog = backend.index("FBO_DrawGlobalFog();", debug)
        underwater = backend.index("FBO_DrawUnderwater();", fog)
        motion = backend.index("FBO_MotionBlur();", underwater)
        self.assertLess(fog, underwater)
        self.assertLess(underwater, motion)

    def test_vulkan_backends_have_complete_resources_and_ordering(self) -> None:
        for renderer in VULKAN_RENDERERS:
            with self.subTest(renderer=renderer):
                header = read(f"code/{renderer}/vk.h")
                vk = read(f"code/{renderer}/vk.c")
                backend = read(f"code/{renderer}/tr_backend.c")
                local = read(f"code/{renderer}/tr_local.h")
                shader_data = read(f"code/{renderer}/shaders/spirv/shader_data.c")
                draw = function_body(vk, "vk_draw_underwater")

                self.assertIn("underwater_frag_spv", shader_data)
                self.assertIn('#include "../renderercommon/tr_underwater.h"', vk)
                self.assertIn("qboolean vk_draw_underwater( void );", header)
                self.assertIn("qboolean doneUnderwater;", local)
                for member in (
                    "VkRenderPass underwater;",
                    "VkFramebuffer underwater;",
                    "VkImage underwater_image;",
                    "VkImageView underwater_image_view;",
                    "VkDescriptorSet underwater_descriptor;",
                    "VkShaderModule underwater_fs;",
                    "VkPipeline underwater_pipeline;",
                    "VkPipeline underwater_copy_pipeline;",
                ):
                    self.assertIn(member, header)

                # The push-constant range has to hold eight vec4s.
                self.assertIn("float constants[32];", draw)
                self.assertIn("sizeof( constants )", draw)
                # Effect draw into the scratch attachment, then copy back: the
                # pass must never sample the attachment it is writing.
                effect_bind = draw.index("vk.underwater_pipeline );")
                copy_bind = draw.index("vk.underwater_copy_pipeline );")
                scratch_pass = draw.index("vk.render_pass.underwater, vk.framebuffers.underwater")
                self.assertLess(scratch_pass, effect_bind)
                self.assertLess(effect_bind, copy_bind)
                self.assertLess(draw.index("vk_end_render_pass();"), copy_bind)
                self.assertIn("sets[0] = vk.underwater_descriptor;", draw)
                for guard in (
                    "backEnd.doneUnderwater",
                    "glConfig.stereoEnabled",
                    "RDF_NOWORLDMODEL",
                    "vk.underwater_pipeline == VK_NULL_HANDLE",
                ):
                    self.assertIn(guard, draw)
                # The composite runs from inside RB_DrawSurfs, before the
                # doneSurfaces flag is raised for bloom, so that flag is still
                # clear here and must not be a precondition: requiring it
                # skipped the layer on every frame whose primary view was not
                # preceded by another 3D pass.
                self.assertNotIn("!backEnd.doneSurfaces", draw)
                self.assertLess(
                    backend.index("vk_draw_underwater();"),
                    backend.index("backEnd.doneSurfaces = qtrue;"),
                )
                # Post-process binds bypass the descriptor and pipeline caches.
                self.assertIn("vk.cmd->last_pipeline = VK_NULL_HANDLE;", draw)
                self.assertIn("Com_Memset( &vk.cmd->scissor_rect, 0xff,", draw)

                fog = backend.index("vk_draw_global_fog();")
                underwater = backend.index("vk_draw_underwater();", fog)
                motion = backend.index("vk_motion_blur();", underwater)
                self.assertLess(fog, underwater)
                self.assertLess(underwater, motion)

    def test_vulkan_post_process_push_range_covers_the_layer(self) -> None:
        self.assertIn("post_push_range.size = 128;", read("code/renderervk/vk.c"))
        self.assertIn("32 * sizeof( float ),", read("code/rendererrtx/vk.c"))

    def test_vulkan_depth_copy_is_requested_for_the_layer(self) -> None:
        """Without this the absorption tint silently vanishes on maps with no fog sidecar."""
        backend = read("code/renderervk/tr_backend.c")
        request = backend[backend.index("!vk_depth_fade_ready() &&") :]
        request = request[: request.index("vk_copy_depth_fade();")]
        self.assertIn("r_underwater && r_underwater->integer", request)
        self.assertIn("cmd->refdef.underwaterStrength > 0.0f", request)
        self.assertIn("( r_underwater && r_underwater->integer ) ||",
                      read("code/renderervk/vk.c"))

    def test_rtx_depth_attachment_is_sampleable_for_the_layer(self) -> None:
        """RTX samples the scene depth attachment directly rather than a private
        copy, so the layer has to claim the sampled usage and the post-main store
        on its own account. Deriving both from the global-fog sidecar alone left
        the absorption uploading a zero density on every map with no fog file:
        the warp and the edge falloff still ran, the medium tint never did."""
        vk = read("code/rendererrtx/vk.c")

        start = vk.index("&vk.depth_image, &vk.depth_image_view,")
        call = vk[vk.rindex("create_depth_attachment(", 0, start) :]
        call = call[: call.index(");") + 2]
        self.assertIn("vk_global_fog_enabled() || vk_underwater_depth_enabled()", call)
        # The transient hint has to drop for the layer too, not just for fog.
        transient = call[: call.index("? qfalse : qtrue")]
        self.assertIn("vk_underwater_depth_enabled()", transient)

        needs = vk[vk.index("const qboolean needsPostMainPass =") :]
        needs = needs[: needs.index(";")]
        self.assertIn("vk_underwater_depth_enabled()", needs)

    def test_rtx_transitions_the_sampled_depth_attachment(self) -> None:
        """RTX samples the depth attachment directly rather than a private copy."""
        draw = function_body(read("code/rendererrtx/vk.c"), "vk_draw_underwater")

        self.assertEqual(draw.count("record_image_layout_transition"), 2)
        self.assertLess(
            draw.index("VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,\n\t\t\tVK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL"),
            draw.index("VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,\n\t\t\tVK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL"),
        )
        # A successful ray-traced frame must not be suppressed by the resume.
        self.assertIn("vk_begin_post_bloom_render_pass();", draw)


class UnderwaterViewDocumentationTests(unittest.TestCase):
    def test_contract_is_documented(self) -> None:
        technical = read("docs/fnql/TECHNICAL.md")
        doc = read("docs/fnql/UNDERWATER_VIEW.md")
        display = read("docs/DISPLAY.md")

        self.assertIn("UNDERWATER_VIEW.md", technical)
        for expected in (
            "visual-only",
            "REF_API_VERSION",
            "r_underwater",
            "r_underwaterWarp",
            "r_underwaterDispersion",
            "r_underwaterFog",
            "r_underwaterVignette",
            "MASK_WATER",
            "RDF_NOWORLDMODEL",
        ):
            with self.subTest(expected=expected):
                self.assertIn(expected, doc)

        self.assertIn("## Underwater View", display)
        self.assertIn("fnql/UNDERWATER_VIEW.md", display)
        # Latched controls belong in the vid_restart list.
        restart = display[display.index("## When To Use `vid_restart`") :]
        self.assertIn("- `r_underwater`", restart)


if __name__ == "__main__":
    unittest.main()
