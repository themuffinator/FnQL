import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_text(relative_path):
    return (ROOT / relative_path).read_text(encoding="utf-8")


class DlightShadowBiasTests(unittest.TestCase):
    def assert_cvar_default(self, source, name, value):
        self.assertRegex(
            source,
            rf'ri\.Cvar_Get\(\s*"{re.escape(name)}",\s*"{re.escape(value)}",',
        )

    def test_dlight_shadow_bias_defaults_match_between_renderers(self):
        for path in (
            "code/renderer/tr_init.c",
            "code/renderervk/tr_init.c",
            "code/rendererrtx/tr_init.c",
        ):
            with self.subTest(path=path):
                source = read_text(path)
                self.assert_cvar_default(source, "r_dlightShadowBias", "4")
                self.assert_cvar_default(source, "r_dlightShadowCasterDepthBias", "1")
                self.assert_cvar_default(source, "r_dlightShadowCasterSlopeBias", "1")
                self.assert_cvar_default(source, "r_dlightShadowCasterNormalBias", "0.25")
                self.assertIn("angle-aware, texel-aware dynamic-light shadow-map sampling", source)

    def test_csm_shadow_bias_defaults_match_between_renderers(self):
        for path in (
            "code/renderer/tr_init.c",
            "code/renderervk/tr_init.c",
            "code/rendererrtx/tr_init.c",
        ):
            with self.subTest(path=path):
                source = read_text(path)
                self.assert_cvar_default(source, "r_csmShadowBias", "8")
                self.assert_cvar_default(source, "r_csmCasterDepthBias", "1.5")
                self.assert_cvar_default(source, "r_csmCasterSlopeBias", "1.5")
                self.assert_cvar_default(source, "r_csmCasterNormalBias", "0.5")
                self.assertIn("Maximum receiver bias in world units for directional sky-sun shadow-map sampling", source)

    def test_correctness_mode_reports_separated_bias_classes(self):
        for renderer in ("code/renderer", "code/renderervk", "code/rendererrtx"):
            with self.subTest(renderer=renderer):
                header = read_text(f"{renderer}/tr_local.h")
                backend = read_text(f"{renderer}/tr_backend.c")
                cmds = read_text(f"{renderer}/tr_cmds.c")

                self.assertIn("float\t\treceiverBias;", header)
                self.assertIn("float\t\tcasterDepthBias;", header)
                self.assertIn("float\t\tcasterSlopeBias;", header)
                self.assertIn("float\t\tcasterNormalBias;", header)
                self.assertIn("debug->receiverBias = R_ShadowClampReceiverBias", backend)
                self.assertIn("debug->casterDepthBias = R_ShadowClampCasterDepthBias", backend)
                self.assertIn("debug->casterSlopeBias = R_ShadowClampCasterSlopeBias", backend)
                self.assertIn("debug->casterNormalBias = R_ShadowClampCasterNormalBias", backend)
                self.assertIn("bias receiver:%.2f caster-depth:%.2f caster-slope:%.2f caster-normal:%.2f", cmds)

    def test_vulkan_receiver_bias_is_angle_aware_and_texel_limited(self):
        for path in (
            "code/renderervk/shaders/light_frag.tmpl",
            "code/rendererrtx/shaders/light_frag.tmpl",
        ):
            with self.subTest(path=path):
                source = read_text(path)

                self.assertIn("float receiverSlope = 1.0 - receiverNDotL;", source)
                self.assertIn("float receiverBias = depthFadeInfo.z * (0.125 + 0.375 * receiverSlope);", source)
                self.assertIn("float texelWorldBias = max(2.0 * faceDist / faceSize, 0.125);", source)
                self.assertIn("receiverBias = min(receiverBias, texelWorldBias);", source)

    def test_glx_receiver_bias_matches_vulkan_policy(self):
        source = read_text("code/renderer/tr_arb.c")

        self.assertIn("shadowReceiverBiasScale =", source)
        self.assertIn("r_dlightShadowBias ? r_dlightShadowBias->value : 4.0f ) / shadowAtlas[0]", source)
        self.assertIn('"MAD local.x, local.x, -half.x, one.x; \\n"', source)
        self.assertIn('"MUL local.x, local.x, half.x; \\n"', source)
        self.assertIn('"MUL local.x, local.x, dlightShadow.w; \\n"', source)
        self.assertIn('"MUL local.x, local.x, faceInfo.x; \\n"', source)
        self.assertNotIn("biasShape", source)

    def test_glx_shadow_program_avoids_short_source_swizzles(self):
        source = read_text("code/renderer/tr_arb.c")

        self.assertIn('"MAX tile.x, tile.x, local.z; \\n"', source)
        self.assertIn('"MAX tile.y, tile.y, local.z; \\n"', source)
        self.assertIn('"MIN tile.x, tile.x, local.w; \\n"', source)
        self.assertIn('"MIN tile.y, tile.y, local.w; \\n"', source)
        self.assertIn('"MUL tile.x, tile.x, dlightShadow.x; \\n"', source)
        self.assertIn('"MUL tile.y, tile.y, dlightShadow.y; \\n"', source)
        self.assertNotIn('"MAX tile.xy, tile.xy, local.zzzz; \\n"', source)
        self.assertNotIn('"MIN tile.xy, tile.xy, local.wwww; \\n"', source)
        self.assertNotIn('"MUL tile.xy, tile.xy, dlightShadow.xy; \\n"', source)

    def test_caster_bias_fallbacks_are_contact_preserving(self):
        expected_normal_scale = (
            "normalScale = 0.25f + 0.50f * ( 1.0f - "
            "Com_Clamp( 0.0f, 1.0f, fabsf( lightSide ) ) );"
        )
        for path in (
            "code/renderer/tr_shade.c",
            "code/renderervk/tr_shade.c",
            "code/rendererrtx/tr_shade.c",
        ):
            with self.subTest(path=path):
                source = read_text(path)
                self.assertIn("r_dlightShadowCasterNormalBias->value : 0.25f", source)
                self.assertIn(expected_normal_scale, source)
                self.assertIn(
                    "VectorScale( tr.csm.lightDirection, -1.0f, lightToVertex );",
                    source,
                )
                self.assertNotIn(
                    "VectorCopy( tr.csm.lightDirection, lightToVertex );",
                    source,
                )

        glx_backend = read_text("code/renderer/tr_backend.c")
        self.assertIn("r_dlightShadowCasterSlopeBias->value : 1.0f", glx_backend)
        self.assertIn("r_dlightShadowCasterDepthBias->value : 1.0f", glx_backend)
        self.assertIn("r_csmCasterSlopeBias ? r_csmCasterSlopeBias->value : 1.5f", glx_backend)
        self.assertIn("r_csmCasterDepthBias ? r_csmCasterDepthBias->value : 1.5f", glx_backend)

        for path in ("code/renderervk/vk.c", "code/rendererrtx/vk.c"):
            with self.subTest(path=path):
                vk_backend = read_text(path)
                self.assertIn("r_dlightShadowCasterDepthBias->value : 1.0f", vk_backend)
                self.assertIn("r_dlightShadowCasterSlopeBias->value : 1.0f", vk_backend)
                self.assertIn("r_csmCasterDepthBias ? r_csmCasterDepthBias->value : 1.5f", vk_backend)
                self.assertIn("r_csmCasterSlopeBias ? r_csmCasterSlopeBias->value : 1.5f", vk_backend)

    def test_glx_shadow_atlas_avoids_duplicate_caster_probe(self):
        source = read_text("code/renderer/tr_backend.c")

        self.assertNotIn("RB_DlightShadowFaceHasCasters", source)
        self.assertIn("surfaces = RB_RenderDlightShadowCasters( dl );", source)

    def test_glx_shadow_passes_avoid_per_entity_rescans(self):
        source = read_text("code/renderer/tr_backend.c")

        dlight_start = source.index("static int RB_RenderDlightShadowCasters(")
        dlight_end = source.index("static void RB_RenderDlightShadowAtlas(", dlight_start)
        dlight_block = source[dlight_start:dlight_end]
        self.assertNotIn("RB_CollectDlightShadowCasterEntities", dlight_block)
        self.assertIn("if ( entityNum != currentEntityNum )", dlight_block)

        for path in (
            "code/renderer/tr_backend.c",
            "code/renderervk/tr_backend.c",
            "code/rendererrtx/tr_backend.c",
        ):
            with self.subTest(path=path):
                source = read_text(path)
                self.assertIn("static int RB_RenderCSMShadowCascade(", source)
                self.assertIn("static void RB_CSMShadowReceiverPass(", source)
                self.assertIn("static void RB_SetDlightShadowCasterEntity(", source)
                csm_cascade_start = source.index("static int RB_RenderCSMShadowCascade(")
                csm_cascade_end = source.index("static void RB_RenderCSMShadowAtlas(", csm_cascade_start)
                csm_cascade_block = source[csm_cascade_start:csm_cascade_end]
                self.assertNotIn("entityQueued[MAX_REFENTITIES]", csm_cascade_block)
                self.assertNotIn("RB_RenderCSMShadowEntityCasters", csm_cascade_block)
                self.assertIn("if ( entityNum != currentEntityNum )", csm_cascade_block)
                self.assertIn("RB_SetCSMShadowEntity( entityNum, &backEnd.viewParms, originalTime );", csm_cascade_block)

                csm_receiver_start = source.index("static void RB_CSMShadowReceiverPass(")
                csm_receiver_end = source.index("static void RB_SetDlightShadowCasterEntity(", csm_receiver_start)
                csm_receiver_block = source[csm_receiver_start:csm_receiver_end]
                self.assertNotIn("entityQueued[MAX_REFENTITIES]", csm_receiver_block)
                self.assertNotIn("RB_RenderCSMShadowEntityReceivers", csm_receiver_block)
                self.assertNotIn("RB_RenderCSMShadowWorldReceivers", csm_receiver_block)
                self.assertIn("if ( entityNum != currentEntityNum )", csm_receiver_block)


if __name__ == "__main__":
    unittest.main()
