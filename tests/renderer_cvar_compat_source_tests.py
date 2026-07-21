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
            "r_enableColorCorrect",
            "r_postProcessActive",
            "r_colorCorrectActive",
            "r_contrast",
        )
        for name in retail_only:
            with self.subTest(name=name):
                self.assertIn(f'"{name}"', contract)

        self.assertIn('"r_fastSkyColor", "0x000000"', contract)
        self.assertIn('"r_drawSkyFloor", "1"', contract)
        self.assertIn('"r_showSmp", "0", CVAR_CHEAT', contract)
        self.assertIn('"r_enablePostProcess", "1"', contract)
        self.assertIn('"r_enableColorCorrect", "1"', contract)

    def test_retail_bloom_control_surface_is_not_renderer_owned(self) -> None:
        contract = read("code/renderercommon/tr_ql_cvars.h")
        retail_bloom_names = (
            "r_enableBloom",
            "r_bloomActive",
            "r_bloomPasses",
            "r_bloomIntensity",
            "r_bloomBrightThreshold",
            "r_bloomBlurScale",
            "r_bloomBlurRadius",
            "r_bloomBlurFalloff",
            "r_bloomSaturation",
            "r_bloomSceneIntensity",
            "r_bloomSceneSaturation",
        )

        for name in retail_bloom_names:
            with self.subTest(name=name):
                self.assertNotIn(f'"{name}"', contract)

        self.assertNotIn("cvar_t *enableBloom", contract)
        self.assertNotIn("cvar_t *bloomActive", contract)
        self.assertNotIn("qlRendererCvars.bloom", contract)

    def test_retail_postprocess_never_preempts_fnq3_renderer_controls(self) -> None:
        contract = read("code/renderercommon/tr_ql_cvars.h")
        self.assertNotIn("bridgeMarkerExisted", contract)
        self.assertNotIn("retailPostProcessExisted", contract)
        self.assertNotIn("useRetailPostProcess", contract)
        self.assertIn(
            '"r_qlRetailPostProcessBridge", "0",\n\t\tCVAR_ROM',
            contract,
        )
        self.assertIn('ri.Cvar_Set( "r_qlRetailPostProcessBridge", "0" )', contract)
        self.assertNotIn("R_QLPostParameterModificationCount", contract)
        self.assertNotIn("postParameterModificationCount", contract)
        self.assertNotIn("R_QLBridgeCvar", contract)
        migration_start = contract.index("if ( retiredBridgeOwnedPostProcess )")
        migration_end = contract.index("/* The ROM registration pins")
        normal_runtime = contract[:migration_start] + contract[migration_end:]
        self.assertNotIn('ri.Cvar_Set( "r_bloom"', normal_runtime)
        self.assertNotIn('ri.Cvar_Set( "r_bloom_', normal_runtime)
        self.assertNotIn('ri.Cvar_Set( "r_colorGrade"', normal_runtime)

        contrast = contract[
            contract.index("static ID_INLINE float R_QLRetailContrast") :
            contract.index("#endif /* TR_QL_CVARS_H */")
        ]
        self.assertIn("return 1.0f;", contrast)
        self.assertNotIn("qlRendererCvars.contrast->value", contrast)

    def test_retired_bridge_profiles_restore_fnq3_bloom_defaults_once(self) -> None:
        contract = read("code/renderercommon/tr_ql_cvars.h")

        self.assertIn("retiredBridgeOwnedPostProcess", contract)
        self.assertIn(
            'atoi( ri.Cvar_VariableString( "r_qlRetailPostProcessBridge" ) ) != 0',
            contract,
        )
        migration = contract[
            contract.index("if ( retiredBridgeOwnedPostProcess )") :
            contract.index("/* The ROM registration pins")
        ]
        self.assertIn(
            'ri.Cvar_Set( "r_bloom", backend == QL_CVAR_BACKEND_GLX ? "0" : "1" )',
            migration,
        )
        for name, value in (
            ("r_bloom_intensity", "0.5"),
            ("r_bloom_threshold", "0.75"),
            ("r_colorGrade", "0"),
            ("r_bloom_passes", "5"),
        ):
            self.assertIn(f'ri.Cvar_Set( "{name}", "{value}" )', migration)

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

    def test_retail_float_framebuffer_alias_cannot_change_fnq3_storage(self) -> None:
        classic = read("code/renderer/tr_arb.c")
        vulkan = read("code/renderervk/vk.c")
        rtx = read("code/rendererrtx/vk.c")

        contract = read("code/renderercommon/tr_ql_cvars.h")

        self.assertIn('"r_floatingPointFBOs", "0"', contract)
        self.assertIn("FnQ3 r_hdr and r_hdrPrecision own scene storage", contract)
        for source in (classic, vulkan, rtx):
            self.assertNotIn("qlRendererCvars.floatingPointFBOs", source)

        fnq3_defaults = read("code/renderer/tr_init.c")
        for registration in (
            'Cvar_Get( "r_bloom", "0"',
            'Cvar_Get( "r_bloom_threshold", "0.75"',
            'Cvar_Get( "r_bloom_intensity", "0.5"',
            'Cvar_Get( "r_bloom_passes", "5"',
        ):
            self.assertIn(registration, fnq3_defaults)


if __name__ == "__main__":
    unittest.main()
