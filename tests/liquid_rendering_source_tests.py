from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


class LiquidRenderingSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.gl = read("code/renderer/tr_shade.c")
        cls.vk = read("code/renderervk/tr_shade.c")
        cls.gl_init = read("code/renderer/tr_init.c")
        cls.vk_init = read("code/renderervk/tr_init.c")
        cls.gl_shader = read("code/rendererglx/glx_material.cpp")
        gl_liquid_start = cls.gl_shader.index("static const char kLiquidFragmentSource[]")
        gl_liquid_end = cls.gl_shader.index("static GLuint GLX_Material_CompileShader", gl_liquid_start)
        cls.gl_liquid_shader = cls.gl_shader[gl_liquid_start:gl_liquid_end]
        cls.vk_shader = read("code/renderervk/shaders/liquid.frag")
        cls.vk_vertex_shader = read("code/renderervk/shaders/liquid.vert")
        cls.gl_backend = read("code/renderer/tr_backend.c")
        cls.vk_backend = read("code/renderervk/tr_backend.c")
        cls.vk_impl = read("code/renderervk/vk.c")
        cls.gl_shader_system = read("code/renderer/tr_shader.c")
        cls.vk_shader_system = read("code/renderervk/tr_shader.c")
        cls.liquid_header = read("code/renderercommon/tr_liquid.h")
        cls.gl_arb = read("code/renderer/tr_arb.c")

    def test_refraction_is_drawn_before_authored_stages_in_both_backends(self) -> None:
        gl_pre = self.gl.index("RB_DrawLiquidPass( &tess, qtrue )")
        gl_authored = self.gl.index("RB_IterateStagesGeneric( input )", gl_pre)
        gl_post = self.gl.index("RB_DrawLiquidPass( &tess, qfalse )", gl_authored)
        self.assertLess(gl_pre, gl_authored)
        self.assertLess(gl_authored, gl_post)

        vk_pre = self.vk.index("VK_DrawLiquidPass( &tess, qtrue )")
        vk_authored = self.vk.index("RB_IterateStagesGeneric( &tess, fogCollapse )", vk_pre)
        vk_post = self.vk.index("VK_DrawLiquidPass( &tess, qfalse )", vk_authored)
        self.assertLess(vk_pre, vk_authored)
        self.assertLess(vk_authored, vk_post)

    def test_liquid_cvar_surface_is_consistent_across_backends(self) -> None:
        expectations = (
            ('Cvar_Get( "r_liquid", "0", CVAR_ARCHIVE_ND | CVAR_LATCH )',
             'Cvar_CheckRange( r_liquid, "0", "2", CV_INTEGER )'),
            ('Cvar_Get( "r_liquidResolution", "1.0", CVAR_ARCHIVE_ND | CVAR_LATCH )',
             'Cvar_CheckRange( r_liquidResolution, "0.25", "1.0", CV_FLOAT )'),
            ('Cvar_Get( "r_liquidRefraction", "0.65", CVAR_ARCHIVE_ND )',
             'Cvar_CheckRange( r_liquidRefraction, "0.0", "1.0", CV_FLOAT )'),
            ('Cvar_Get( "r_liquidWarpScale", "1.0", CVAR_ARCHIVE_ND )',
             'Cvar_CheckRange( r_liquidWarpScale, "0.0", "2.0", CV_FLOAT )'),
            ('Cvar_Get( "r_liquidReflection", "0.65", CVAR_ARCHIVE_ND )',
             'Cvar_CheckRange( r_liquidReflection, "0.0", "1.0", CV_FLOAT )'),
            ('Cvar_Get( "r_liquidRipples", "1.0", CVAR_ARCHIVE_ND )',
             'Cvar_CheckRange( r_liquidRipples, "0.0", "2.0", CV_FLOAT )'),
        )
        for source in (self.gl_init, self.vk_init):
            for registration, range_check in expectations:
                self.assertIn(registration, source)
                self.assertIn(range_check, source)
        # The retired names must not linger anywhere in renderer code.
        for source in (self.gl_init, self.vk_init, self.gl, self.vk,
                       self.gl_backend, self.vk_backend, self.gl_arb):
            for retired in ("r_liquidReflections", "r_liquidReflectionScale",
                            "r_liquidFresnel", "r_liquidRippleStrength",
                            '"r_liquidWarp"'):
                self.assertNotIn(retired, source)

    def test_ambient_warp_is_pixel_scaled_and_live(self) -> None:
        self.assertIn("(ambientPixels + ripplePixels) * u_TargetInverse.xy", self.gl_shader)
        self.assertIn("(ambient_pixels + ripple_pixels) * inverse_view", self.vk_shader)
        # Both backends resolve the warp multiplier through the shared
        # view-height-scaled helper instead of a raw pixel constant.
        self.assertIn("R_LiquidWarpPixels(", self.gl)
        self.assertIn("R_LiquidWarpPixels(", self.vk)
        self.assertIn("LIQUID_WARP_BASE_PIXELS", self.liquid_header)
        self.assertIn("LIQUID_WARP_SCALE_MAX", self.liquid_header)
        self.assertIn("LIQUID_REFERENCE_VIEW_HEIGHT", self.liquid_header)

    def test_refraction_rejects_foreground_depth_samples(self) -> None:
        # Warped samples that land on opaque geometry nearer than the liquid
        # surface fall back to the unwarped coordinate, on every per-fragment
        # backend, using the shared epsilon.
        self.assertIn("LIQUID_DEPTH_REJECT_EPSILON", self.liquid_header)
        self.assertIn("0.00003", self.liquid_header)
        self.assertIn("u_Texture1", self.gl_liquid_shader)
        self.assertIn("gl_FragCoord.z - 0.00003", self.gl_liquid_shader)
        self.assertIn("scene_depth", self.vk_shader)
        # Vulkan uses reversed depth, so nearer means larger stored values.
        self.assertIn("gl_FragCoord.z + 0.00003", self.vk_shader)
        arb = self.gl_arb
        refraction_start = arb.index("liquidRefractionFP")
        refraction = arb[refraction_start:arb.index('"END', refraction_start)]
        self.assertIn("TEX depth, uv, texture[1], 2D;", refraction)
        self.assertIn("0.00003", refraction)
        self.assertIn("CMP uv.xy, depth.y, baseUv, uv;", refraction)
        # Depth is copied at the liquid boundary on both backends.
        self.assertIn("FBO_CopyLiquidDepth", self.gl_backend)
        self.assertIn("vk_copy_depth_fade", self.vk_backend)

    def test_warp_fades_at_grazing_angles(self) -> None:
        # The compressed wave field near the horizon would alias; every
        # per-fragment backend fades it with the same slope.
        self.assertIn("LIQUID_GRAZING_FADE_SCALE", self.liquid_header)
        self.assertIn("abs(dot(normal, viewDirection)) * 4.0", self.gl_shader)
        self.assertIn("abs(dot(normal, view_dir)) * 4.0", self.vk_shader)
        self.assertIn("LIQUID_GRAZING_FADE_SCALE", self.gl)

    def test_wave_gradient_constants_are_shared_across_backends(self) -> None:
        # The per-fragment ambient wave model must stay numerically identical
        # in the shared header (C fallback and ARB upload), the GLx GLSL, and
        # the Vulkan GLSL. Octave rows: wave vector xyz, angular speed.
        octaves = (
            ("0.0096", "0.0109", "0.0051", "0.85"),
            ("-0.0269", "0.0196", "0.0116", "1.40"),
            ("0.0320", "-0.0693", "0.0266", "2.30"),
        )
        directions = (
            ("0.66", "0.75", "0.42"),
            ("-0.81", "0.59", "0.33"),
            ("0.42", "-0.91", "0.25"),
        )
        for source in (self.liquid_header, self.gl_liquid_shader, self.vk_shader):
            for row in octaves + directions:
                for constant in row:
                    self.assertIn(constant, source)
        # Scene time is wrapped to the octaves' common period before upload so
        # low-precision fragment paths keep stable trigonometric arguments.
        self.assertIn("LIQUID_WAVE_TIME_PERIOD", self.liquid_header)
        self.assertIn("R_LiquidWaveTime(", self.gl)
        self.assertIn("R_LiquidWaveTime(", self.vk)

    def test_refraction_uses_projective_face_coordinates(self) -> None:
        self.assertIn("v_ScreenPosition = clipPosition", self.gl_shader)
        self.assertIn(
            "v_ScreenPosition.xy / v_ScreenPosition.w * 0.5 + 0.5",
            self.gl_liquid_shader,
        )
        # Screen UVs must come from the projective varying; gl_FragCoord is
        # reserved for the depth comparison.
        self.assertNotIn("gl_FragCoord.xy", self.gl_liquid_shader)

        self.assertIn("frag_screen = vec4(clip.xy * 0.5 + clip.ww * 0.5", self.vk_vertex_shader)
        self.assertIn("frag_screen.xy / frag_screen.w", self.vk_shader)
        self.assertNotIn("gl_FragCoord.xy", self.vk_shader)

    def test_subrect_views_keep_authored_liquid_materials(self) -> None:
        helper = "R_LiquidViewportCoversTarget"
        self.assertIn(helper, self.liquid_header)
        for shade in (self.gl, self.vk):
            self.assertIn(helper, shade)
        for backend in (self.gl_backend, self.vk_backend):
            self.assertIn(helper, backend)

    def test_original_liquid_contents_survive_shader_remaps(self) -> None:
        combined = "shader->contentFlags | state->contentFlags"
        self.assertGreaterEqual(self.gl.count(combined), 2)
        self.assertGreaterEqual(self.vk.count(combined), 2)
        self.assertIn("tess.liquidContentFlags = " + combined, self.gl)
        self.assertIn("tess.liquidContentFlags = " + combined, self.vk)

    def test_gl_fallback_does_not_overwrite_authored_stage_colors(self) -> None:
        begin = self.gl.index("static void RB_DrawLiquidPass")
        end = self.gl.index("#endif /* USE_FBO */", begin)
        liquid_pass = self.gl[begin:end]
        self.assertIn("rb_liquidColors[i].rgba", liquid_pass)
        self.assertNotIn("input->svars.colors[i].rgba", liquid_pass)

    def test_shaders_have_distinct_refraction_and_fresnel_alpha_paths(self) -> None:
        self.assertIn("u_Params.w < 0.0", self.gl_shader)
        self.assertIn("typeScale * clamp(u_Params.z, 0.0, 1.0)", self.gl_shader)
        self.assertIn("gl_FragColor = vec4(sheenColor, alpha)", self.gl_liquid_shader)
        self.assertIn("liquid_info.w > 0.5", self.vk_shader)
        self.assertIn("liquid_info.x * clamp(liquid_params.z, 0.0, 1.0)", self.vk_shader)
        self.assertIn("out_color = vec4(sheen_color, alpha)", self.vk_shader)

    def test_reflection_samples_only_the_pretransparency_snapshot(self) -> None:
        # Each shader samples the snapshot exactly twice: once for the warped
        # refraction underlay and once for the mirrored reflection tap. The
        # sheen pass never reads the live color attachment, so no feedback
        # loop with the authored stages is possible.
        self.assertEqual(self.gl_liquid_shader.count("texture2D(u_Texture0"), 2)
        self.assertEqual(self.vk_shader.count("texture(scene_color"), 2)
        self.assertIn("reflectedUv", self.gl_liquid_shader)
        self.assertIn("reflected_uv", self.vk_shader)
        # The reflection weight collapses to the flat material sheen when the
        # snapshot was never captured for this view.
        expected = "backEnd.liquidScreenMapDone ? LIQUID_REFLECT_INTENSITY : 0.0f"
        self.assertIn(expected, self.gl)
        self.assertIn(expected, self.vk)
        # The ARB assembly tier mirrors the same reflection fallback contract.
        self.assertIn("liquidSheenFP", self.gl_arb)
        self.assertIn("LIQUID_REFLECT_PROXY_BASE", self.gl_arb)

    def test_snapshot_capture_is_at_a_deterministic_sort_boundary(self) -> None:
        trigger = "liquidSnapshotPending && shader->sort >= SS_FOG"
        lookahead = "RB_DrawSurfListNeedsLiquidSnapshot"
        for source in (self.gl_backend, self.vk_backend):
            self.assertIn(lookahead, source)
            self.assertIn(trigger, source)
            # A reflection-only configuration still captures: the mirrored
            # tap samples the same snapshot as the refraction underlay.
            self.assertIn("r_liquidReflection", source)

    def test_early_or_masked_liquids_keep_the_authored_path(self) -> None:
        for shade in (self.gl, self.vk):
            self.assertIn("input->liquidSort >= SS_FOG", shade)
            self.assertIn("R_LiquidShaderSupported( input->shader )", shade)
        for shader_system in (self.gl_shader_system, self.vk_shader_system):
            self.assertIn("GLS_ATEST_BITS | GLS_DEPTHTEST_DISABLE", shader_system)
            self.assertIn("GLS_DEPTHFUNC_EQUAL", shader_system)
            self.assertIn("GLS_POLYMODE_LINE", shader_system)
            self.assertIn("materialShader->dfType != DFT_NONE", shader_system)

    def test_vulkan_snapshot_downsample_has_a_dedicated_linear_sampler(self) -> None:
        self.assertIn("vk.liquidSnapshot.source_descriptor", self.vk_impl)
        self.assertIn("sd.gl_mag_filter = sd.gl_min_filter = GL_LINEAR", self.vk_impl)
        self.assertIn("&vk.liquidSnapshot.source_descriptor, 0, NULL", self.vk_impl)

    def test_vulkan_snapshot_dependencies_are_full_image(self) -> None:
        self.assertIn("liquidDeps[0].dependencyFlags = 0", self.vk_impl)
        self.assertIn("liquidDeps[1].dependencyFlags = 0", self.vk_impl)
        capture = self.vk_impl.index("qboolean vk_capture_liquid_scene")
        resume = self.vk_impl.index("vk_begin_main_render_pass_load();", capture)
        self.assertNotIn("VK_DEPENDENCY_BY_REGION_BIT", self.vk_impl[capture:resume])

    def test_liquid_passes_preserve_destination_alpha(self) -> None:
        self.assertIn("GL_ColorMask( previousColorMask[0]", self.gl)
        self.assertIn("def->shader_type == TYPE_LIQUID", self.vk_impl)
        self.assertIn("VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT", self.vk_impl)


if __name__ == "__main__":
    unittest.main()
