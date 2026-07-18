import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_text(relative_path):
    return (ROOT / relative_path).read_text(encoding="utf-8")


def section(text, start, end):
    start_index = text.index(start)
    end_index = text.index(end, start_index)
    return text[start_index:end_index]


class ShadowCorrectnessSourceTests(unittest.TestCase):
    RENDERERS = ("code/renderer", "code/renderervk", "code/rendererrtx")

    def test_correctness_cvar_is_registered_in_both_renderers(self):
        for renderer in self.RENDERERS:
            with self.subTest(renderer=renderer):
                header = read_text(f"{renderer}/tr_local.h")
                init = read_text(f"{renderer}/tr_init.c")

                self.assertIn("extern cvar_t\t*r_shadowCorrectness;", header)
                self.assertIn("cvar_t\t*r_shadowCorrectness;", init)
                self.assertIn(
                    'r_shadowCorrectness = ri.Cvar_Get( "r_shadowCorrectness", "0", CVAR_CHEAT );',
                    init,
                )
                self.assertIn(
                    "Forces the minimal shadow-map correctness path", init
                )

    def test_planner_forces_minimal_point_shadow_mode(self):
        for renderer in self.RENDERERS:
            with self.subTest(renderer=renderer):
                main = read_text(f"{renderer}/tr_main.c")
                csm = section(main, "static void R_PlanCascadedShadows", "/*\n=============\nR_PlaneForSurface")
                spot = section(main, "static void R_PlanSpotShadows", "static void R_PlanDlightShadows")
                dlight = section(main, "static void R_PlanDlightShadows", "#endif // USE_PMLIGHT")

                self.assertIn("r_shadowCorrectness && r_shadowCorrectness->integer", csm)
                self.assertIn("tr.pc.c_csmSkippedDisabled++;", csm)
                self.assertIn("r_shadowCorrectness && r_shadowCorrectness->integer", spot)
                self.assertIn("qboolean correctnessMode;", dlight)
                self.assertIn(
                    "correctnessMode = ( r_shadowCorrectness && r_shadowCorrectness->integer ) ? qtrue : qfalse;",
                    dlight,
                )
                self.assertIn("if ( correctnessMode ) {\n\t\tmaxLights = 1;\n\t}", dlight)
                self.assertIn(
                    "( !correctnessMode && ( !r_dlightShadows || !r_dlightShadows->integer ) )",
                    dlight,
                )

    def test_backend_rejects_alpha_test_casters_only_in_correctness_mode(self):
        for renderer in self.RENDERERS:
            with self.subTest(renderer=renderer):
                backend = read_text(f"{renderer}/tr_backend.c")
                dlight_needed = section(backend, "static qboolean RB_DlightShadowsNeeded", "static const float rb_dlightShadowFlipMatrix")
                dlight_allowed = section(backend, "static qboolean RB_DlightShadowCasterAllowed", "static qboolean RB_DlightShadowMD3Bounds")
                csm_allowed = section(backend, "static qboolean RB_CSMShadowSurfaceAllowed", "static qboolean RB_CSMShadowSurfaceCulledForCascade")

                self.assertIn("r_shadowCorrectness || !r_shadowCorrectness->integer", dlight_needed)
                self.assertIn("static qboolean RB_ShadowCorrectnessRejectsAlphaTest", dlight_needed)
                self.assertIn("stage->stateBits & GLS_ATEST_BITS", dlight_needed)
                self.assertIn("RB_ShadowCorrectnessRejectsAlphaTest( shader )", dlight_allowed)
                self.assertIn("RB_ShadowCorrectnessRejectsAlphaTest( shader )", csm_allowed)

    def test_correctness_mode_dumps_depth_pass_contract(self):
        for renderer in self.RENDERERS:
            with self.subTest(renderer=renderer):
                header = read_text(f"{renderer}/tr_local.h")
                backend = read_text(f"{renderer}/tr_backend.c")
                cmds = read_text(f"{renderer}/tr_cmds.c")
                main = read_text(f"{renderer}/tr_main.c")

                self.assertIn("shadowCorrectnessFaceDebug_t", header)
                self.assertIn("shadowCorrectnessDebug_t", header)
                self.assertIn("projectionMatrix[16]", header)
                self.assertIn("modelMatrix[16]", header)
                self.assertIn("apiViewportX", header)
                self.assertIn("apiViewportTopLeft", header)
                self.assertIn("samplerTopLeft", header)
                self.assertIn("clipYFlipped", header)
                self.assertIn("receiverBias", header)
                self.assertIn("casterDepthBias", header)
                self.assertIn("casterSlopeBias", header)
                self.assertIn("casterNormalBias", header)
                self.assertIn("requestedFilterMode", header)
                self.assertIn("effectiveFilterMode", header)
                self.assertIn("filterSampleCount", header)
                self.assertIn("filterInnerOffset", header)
                self.assertIn("filterOuterOffset", header)
                self.assertIn("shadowCorrectnessDebug_t shadowCorrectnessDebug;", header)

                self.assertIn("static void RB_RecordShadowCorrectnessFace", backend)
                self.assertIn("shadowParmsBuilt", backend)
                self.assertIn("RB_RecordShadowCorrectnessFace( dl, plan, face", backend)
                self.assertIn("qtrue, qfalse", backend)
                self.assertIn("qfalse, qtrue", backend)
                self.assertIn("atlasPublished =", backend)
                self.assertIn("record->apiViewportY", backend)
                self.assertIn("debug->receiverBias = R_ShadowClampReceiverBias", backend)
                self.assertIn("debug->casterDepthBias = R_ShadowClampCasterDepthBias", backend)
                self.assertIn("debug->casterSlopeBias = R_ShadowClampCasterSlopeBias", backend)
                self.assertIn("debug->casterNormalBias = R_ShadowClampCasterNormalBias", backend)
                self.assertIn("debug->requestedFilterMode =", backend)
                self.assertIn("debug->effectiveFilterMode = SHADOW_FILTER_HARD;", backend)
                self.assertIn("debug->filterSampleCount = R_ShadowFilterSampleCount", backend)
                self.assertIn("R_ShadowFilterOffsets( debug->effectiveFilterMode", backend)

                self.assertIn("static void R_PrintShadowCorrectnessDebug", cmds)
                self.assertIn("shadow correctness mode:1 backend:%s active:1", cmds)
                self.assertIn("view-origin:lower-left api-origin:%s sample-origin:%s clip-y:%s", cmds)
                self.assertIn("bias receiver:%.2f caster-depth:%.2f caster-slope:%.2f caster-normal:%.2f", cmds)
                self.assertIn("filter requested:%s effective:%s taps:%i offsets:%.2f/%.2f", cmds)
                self.assertIn("shadow correctness face light:%i", cmds)
                self.assertIn("api-viewport:%i,%i", cmds)
                self.assertIn("shadow correctness projection light:%i", cmds)
                self.assertIn("shadow correctness model light:%i", cmds)
                self.assertIn("depthZeroToOne", cmds)
                self.assertIn("Com_Memset( &tr.shadowCorrectnessDebug", cmds)
                self.assertIn("Com_Memset( &tr.shadowCorrectnessDebug", main)

    def test_implementation_tracking_checklist_is_current(self):
        plan = read_text("docs-dev/plans/2026-06-15-shadowmapping.md")

        self.assertIn("### Implementation tracking checklist", plan)
        self.assertIn("- [x] Round 1:", plan)
        self.assertIn("- [x] Round 2:", plan)
        self.assertIn("- [x] Round 3:", plan)
        self.assertIn("- [x] Round 4:", plan)
        self.assertIn("- [x] Round 5:", plan)
        self.assertIn("- [x] Round 6:", plan)
        self.assertIn("- [x] Round 7:", plan)
        self.assertIn("- [x] Round 8:", plan)

    def test_filter_mode_offsets_are_shared_and_debugged(self):
        for renderer in self.RENDERERS:
            with self.subTest(renderer=renderer):
                header = read_text(f"{renderer}/tr_local.h")
                cmds = read_text(f"{renderer}/tr_cmds.c")

                self.assertIn("static ID_INLINE void R_ShadowFilterOffsets", header)
                self.assertIn("static ID_INLINE int R_ShadowFilterPadTexels", header)
                self.assertIn("static ID_INLINE int R_ShadowAtlasClampTexels", header)
                self.assertIn("case SHADOW_FILTER_HARD:", header)
                self.assertIn("*inner = 0.0f;", header)
                self.assertIn("*outer = 0.0f;", header)
                self.assertIn("case SHADOW_FILTER_PCF_2X2:", header)
                self.assertIn("*inner = 0.5f;", header)
                self.assertIn("*outer = 0.5f;", header)
                self.assertIn("*inner = 0.25f;", header)
                self.assertIn("*outer = 0.75f;", header)
                self.assertIn("static ID_INLINE int R_ShadowFilterSampleCount", header)
                self.assertIn("SHADOW_FILTER_HARD ? 1 : 4", header)

                self.assertIn("filter:%s taps:%i offsets:%.2f/%.2f", cmds)
                self.assertIn("R_ShadowFilterOffsets( effectiveFilter", cmds)
                self.assertIn("R_ShadowFilterSampleCount( effectiveFilter )", cmds)

    def test_glx_correctness_mode_forces_hard_filter_and_one_point_atlas(self):
        source = read_text("code/renderer/tr_arb.c")
        dlight_filter = section(source, "static void ARB_DlightShadowFilterOffsets", "#ifdef RENDERER_GLX")
        csm_filter = section(source, "static void ARB_CSMShadowFilterOffsets", "void ARB_CSMShadowPass")
        update = section(source, "qboolean ARB_UpdatePrograms", "static GLuint FBO_CreateDepthFadeTexture")
        dlight_layout = section(source, "static qboolean FBO_DlightShadowAtlasWanted", "static qboolean FBO_CreateDlightShadowAtlas")
        spot_layout = section(source, "static qboolean FBO_SpotShadowAtlasWanted", "static qboolean FBO_CreateSpotShadowAtlas")
        csm_layout = section(source, "static qboolean FBO_CSMShadowAtlasWanted", "static qboolean FBO_CreateCSMShadowAtlas")
        available = section(source, "qboolean FBO_DlightShadowsAvailable", "qboolean FBO_DlightShadowAtlasAvailable")

        self.assertIn("filter = SHADOW_FILTER_HARD;", dlight_filter)
        self.assertIn("R_ShadowFilterOffsets( filter, inner, outer );", dlight_filter)
        self.assertIn("filter = SHADOW_FILTER_HARD;", csm_filter)
        self.assertIn("R_ShadowFilterOffsets( filter, inner, outer );", csm_filter)
        self.assertIn("r_shadowCorrectness && r_shadowCorrectness->integer", update)
        self.assertIn("qboolean correctnessMode", dlight_layout)
        self.assertIn("R_DlightShadowAtlasLayout( correctnessMode ? 1 : r_dlightShadowMaxLights->integer", dlight_layout)
        self.assertIn("r_shadowCorrectness && r_shadowCorrectness->integer", spot_layout)
        self.assertIn("r_shadowCorrectness && r_shadowCorrectness->integer", csm_layout)
        self.assertIn("r_shadowCorrectness && r_shadowCorrectness->integer", available)

    def test_vulkan_correctness_mode_forces_hard_filter_and_one_point_atlas(self):
        for renderer in ("code/renderervk", "code/rendererrtx"):
            with self.subTest(renderer=renderer):
                shade = read_text(f"{renderer}/tr_shade.c")
                vk = read_text(f"{renderer}/vk.c")
                dlight_params = section(shade, "static qboolean VK_DlightShadowParams", "static void VK_DlightShadowFilterOffsets")
                dlight_filter = section(shade, "static void VK_DlightShadowFilterOffsets", "static qboolean VK_SpotShadowParams")
                csm_filter = section(shade, "static void VK_CSMShadowFilterOffsets", "void VK_CSMShadowPass")
                pipelines_end = ("static void vk_alloc_attachment_batch" if renderer == "code/renderervk"
                                 else "static void vk_alloc_attachments")
                pipelines = section(vk, "static void vk_alloc_persistent_pipelines", pipelines_end)
                dlight_layout = section(vk, "static qboolean vk_dlight_shadow_atlas_layout", "static qboolean vk_spot_shadow_atlas_layout")
                spot_layout = section(vk, "static qboolean vk_spot_shadow_atlas_layout", "static qboolean vk_csm_shadow_atlas_layout")
                csm_layout = section(vk, "static qboolean vk_csm_shadow_atlas_layout", "static void vk_store_dlight_shadow_atlas_layout")

                self.assertIn("!r_shadowCorrectness || !r_shadowCorrectness->integer", dlight_params)
                self.assertIn("filter = SHADOW_FILTER_HARD;", dlight_filter)
                self.assertIn("R_ShadowFilterOffsets( filter, inner, outer );", dlight_filter)
                self.assertIn("filter = SHADOW_FILTER_HARD;", csm_filter)
                self.assertIn("R_ShadowFilterOffsets( filter, inner, outer );", csm_filter)
                self.assertIn("r_shadowCorrectness && r_shadowCorrectness->integer", pipelines)
                self.assertIn("qboolean correctnessMode", dlight_layout)
                self.assertIn("R_DlightShadowAtlasLayout( correctnessMode ? 1 : r_dlightShadowMaxLights->integer", dlight_layout)
                self.assertIn("r_shadowCorrectness && r_shadowCorrectness->integer", spot_layout)
                self.assertIn("r_shadowCorrectness && r_shadowCorrectness->integer", csm_layout)


if __name__ == "__main__":
    unittest.main()
