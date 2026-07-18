from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RENDERERS = {
    "renderer": "GLX",
    "renderervk": "VULKAN",
    "rendererrtx": "RTX",
}


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


class RendererCvarCompatibilitySourceTests(unittest.TestCase):
    def test_shared_contract_contains_every_retail_only_renderer_control(self) -> None:
        contract = read("code/renderercommon/tr_ql_cvars.h")
        retail_only = (
            "r_fastSkyColor",
            "r_drawSkyFloor",
            "r_forceMergeEntities",
            "r_ignoreFastPath",
            "r_primitives",
            "r_smp",
            "r_showSmp",
            "r_uifullscreen",
            "r_skipSmallBatches",
            "r_skipLargeBatches",
            "r_debugShaderIndex",
            "r_debugSortExcept",
            "r_debugAds",
            "r_floatingPointFBOs",
            "r_enablePostProcess",
            "r_enableBloom",
            "r_enableColorCorrect",
            "r_postProcessActive",
            "r_bloomActive",
            "r_colorCorrectActive",
            "r_bloomPasses",
            "r_bloomIntensity",
            "r_bloomBrightThreshold",
            "r_bloomBlurScale",
            "r_bloomBlurRadius",
            "r_bloomBlurFalloff",
            "r_bloomSaturation",
            "r_bloomSceneIntensity",
            "r_bloomSceneSaturation",
            "r_contrast",
        )
        for name in retail_only:
            with self.subTest(name=name):
                self.assertIn(f'"{name}"', contract)

        self.assertIn('"r_fastSkyColor", "0x000000"', contract)
        self.assertIn('"r_drawSkyFloor", "1"', contract)
        self.assertIn('"r_showSmp", "0", CVAR_CHEAT', contract)
        self.assertIn('"r_enableBloom", "1"', contract)
        self.assertIn('"r_bloomIntensity", "0.5"', contract)
        self.assertIn('"r_bloomBrightThreshold", "0.25"', contract)
        self.assertIn(
            "const int boundedProfileFlags = profileFlags | CVAR_PROTECTED | CVAR_VM_CREATED",
            contract,
        )
        self.assertIn('"r_bloomSceneIntensity", "1.0"', contract)
        self.assertIn('"r_bloomSceneSaturation", "1.0"', contract)

    def test_retail_postprocess_defaults_do_not_preempt_fnql_defaults(self) -> None:
        contract = read("code/renderercommon/tr_ql_cvars.h")
        self.assertIn("bridgeMarkerExisted", contract)
        self.assertIn("retailPostProcessExisted", contract)
        self.assertIn("useRetailPostProcess", contract)
        self.assertIn(
            '"r_qlRetailPostProcessBridge", "0",\n\t\tCVAR_ARCHIVE | CVAR_PROTECTED',
            contract,
        )
        self.assertIn('ri.Cvar_Set( "r_qlRetailPostProcessBridge", "1" )', contract)
        self.assertIn("R_QLPostParameterModificationCount", contract)
        self.assertIn("postParameterModificationCount", contract)
        self.assertIn('R_QLBridgeCvar( "r_bloom_intensity"', contract)
        self.assertIn('R_QLBridgeCvar( "r_bloom_threshold"', contract)
        self.assertNotIn('R_QLBridgeCvar( "r_hdr"', contract)

    def test_all_renderers_register_update_and_consume_operational_controls(self) -> None:
        for renderer, backend in RENDERERS.items():
            with self.subTest(renderer=renderer):
                local = read(f"code/{renderer}/tr_local.h")
                init = read(f"code/{renderer}/tr_init.c")
                main = read(f"code/{renderer}/tr_main.c")
                commands = read(f"code/{renderer}/tr_cmds.c")
                shade = read(f"code/{renderer}/tr_shade.c")
                sky = read(f"code/{renderer}/tr_sky.c")
                backend_source = read(f"code/{renderer}/tr_backend.c")
                world = read(f"code/{renderer}/tr_world.c")

                self.assertIn('renderercommon/tr_ql_cvars.h"', local)
                self.assertIn(f"R_QLRegisterRendererCvars( QL_CVAR_BACKEND_{backend} )", init)
                self.assertIn("qlRendererCvars_t qlRendererCvars;", main)
                self.assertIn("R_QLUpdateRendererCvars(", commands)
                self.assertIn("R_QLSkipBatch( tess.numIndexes )", shade)
                self.assertIn("qlRendererCvars.debugSortExcept", shade)
                self.assertIn("qlRendererCvars.debugShaderIndex", shade)
                self.assertIn("qlRendererCvars.drawSkyFloor", sky)
                self.assertIn("R_QLFastSkyColor( clearColor )", backend_source)
                self.assertIn("qlRendererCvars.forceMergeEntities", backend_source)
                self.assertIn('renderercommon/tr_ql_ad_debug.h"', world)
                self.assertIn("R_QLDebugAdvertisements();", world)
                self.assertIn(
                    '"r_teleporterFlash", "1", CVAR_ARCHIVE | CVAR_PROTECTED | CVAR_CLOUD',
                    init,
                )
                self.assertIn('Cvar_CheckRange( r_teleporterFlash, "0", "1", CV_INTEGER )', init)

        advert_debug = read("code/renderercommon/tr_ql_ad_debug.h")
        self.assertIn("AdvertisementBridge_GetCellDisplayState", advert_debug)
        self.assertIn("AdvertisementBridge_GetCellLabel", advert_debug)
        self.assertIn("Com_Clamp( 0, 128", advert_debug)

    def test_client_owns_retail_video_aliases_and_publishes_real_status(self) -> None:
        client = read("code/client/cl_main.cpp")
        for registration in (
            'Cvar_Get( "r_aspectRatio", "0"',
            'Cvar_Get( "r_windowedMode", "12"',
            'Cvar_Get( "r_windowedWidth", "1600"',
            'Cvar_Get( "r_windowedHeight", "1024"',
            'Cvar_Get( "r_stereo", "0"',
            'Cvar_Get( "r_noFastRestart", "0"',
            'Cvar_Get( "r_ext_gamma_control", "1"',
            'Cvar_Get( "r_gl_vendor", ""',
            'Cvar_Get( "r_gl_renderer", ""',
            'Cvar_Get( "r_gl_reserved", "0"',
            'Cvar_Get( "r_lastValidRenderer", ""',
        ):
            self.assertIn(registration, client)

        self.assertIn("hadRetailVideoBridgeMarker", client)
        self.assertIn("&& !hadRetailVideoBridgeMarker", client)
        self.assertIn("hadRetailVideoConfig", client)
        self.assertIn("CL_UpdateRetailVideoBridgeOwnership();", client)
        self.assertIn("static void CL_UpdateRetailVideoBridgeOwnership", client)
        self.assertIn(
            'Cvar_Get( "r_qlRetailVideoBridge", "0", CVAR_ARCHIVE | CVAR_PROTECTED )',
            client,
        )
        self.assertIn('Cvar_Set2( "r_qlRetailVideoBridge", "1", qtrue )', client)
        self.assertIn('Cvar_Set( "r_mode", r_windowedMode->string )', client)
        self.assertIn('Cvar_Set( "r_stereoEnabled", r_stereo->string )', client)
        self.assertIn('Cvar_Set2( "r_aspectRatio"', client)
        self.assertIn('Cvar_Set2( "r_gl_vendor", cls.glconfig.vendor_string', client)
        self.assertIn('Cvar_VariableIntegerValue( "r_qlOcclusionQueries" )', client)
        self.assertIn("!r_noFastRestart->integer", client)
        self.assertIn('Cvar_Set2( "r_windowedMode", "-1", qtrue )', client)

    def test_retail_contrast_reaches_every_final_output_shader(self) -> None:
        classic = read("code/renderer/tr_arb.c")
        vulkan_shader = read("code/renderervk/shaders/gamma.frag")
        vulkan = read("code/renderervk/vk.c")
        rtx_shader = read("code/rendererrtx/shaders/gamma.frag")
        rtx = read("code/rendererrtx/vk.c")

        self.assertIn("R_QLRetailContrast()", classic)
        self.assertIn("retailContrast", vulkan_shader)
        self.assertIn("frag_spec_data.retail_contrast = R_QLRetailContrast()", vulkan)
        self.assertIn("retailContrast", rtx_shader)
        self.assertIn("frag_spec_data.retail_contrast = R_QLRetailContrast()", rtx)

    def test_float_framebuffer_alias_has_capability_checked_fallbacks(self) -> None:
        classic = read("code/renderer/tr_arb.c")
        vulkan = read("code/renderervk/vk.c")
        rtx = read("code/rendererrtx/vk.c")

        self.assertIn("qlRendererCvars.floatingPointFBOs->integer", classic)
        self.assertIn("GL_RGBA16F", classic)
        self.assertIn("VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT", vulkan)
        self.assertIn("VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT", vulkan)
        self.assertIn("using the SDR base format", vulkan)
        self.assertIn("VK_FORMAT_R16G16B16A16_SFLOAT", rtx)
        self.assertIn("floating-point scene storage", rtx)


if __name__ == "__main__":
    unittest.main()
