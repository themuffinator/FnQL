from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RENDERERS = ("renderer", "renderervk", "rendererrtx")


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


class RendererCvarCompatibilitySourceTests(unittest.TestCase):
    def test_shared_contract_contains_supported_retail_renderer_controls(self) -> None:
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
        )
        for name in retail_only:
            with self.subTest(name=name):
                self.assertIn(f'"{name}"', contract)

        self.assertIn('"r_fastSkyColor", "0x000000"', contract)
        self.assertIn('"r_drawSkyFloor", "1"', contract)
        self.assertIn('"r_showSmp", "0", CVAR_CHEAT', contract)

    def test_legacy_ql_postprocess_control_surface_is_absent(self) -> None:
        forbidden = (
            "RBPP_",
            "RC_BLOOM_POST_PROCESS",
            "RetailBloomPostProcessCommand",
            "bloomPostProcessCommand_t",
            "R_QLUpdateRendererCvars",
            "R_QLRetailContrast",
            "retailContrast",
            "retail_contrast",
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
            "r_qlRetailPostProcessBridge",
        )
        source_suffixes = {
            ".c", ".cc", ".cpp", ".cxx", ".h", ".hpp",
            ".frag", ".vert", ".comp", ".geom", ".glsl",
            ".rgen", ".rchit", ".rmiss",
        }

        for path in (ROOT / "code").rglob("*"):
            if not path.is_file() or path.suffix not in source_suffixes:
                continue
            source = path.read_text(encoding="utf-8")
            for fingerprint in forbidden:
                with self.subTest(path=path.relative_to(ROOT), fingerprint=fingerprint):
                    self.assertNotIn(fingerprint, source)

    def test_all_renderers_register_and_consume_operational_controls(self) -> None:
        for renderer in RENDERERS:
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
                self.assertIn("R_QLRegisterRendererCvars();", init)
                self.assertIn("qlRendererCvars_t qlRendererCvars;", main)
                self.assertNotIn("R_QLUpdateRendererCvars", commands)
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
        self.assertNotIn('Cvar_Set( "r_mode", r_windowedMode->string )', client)
        self.assertIn("CL_GetRequestedMode( qboolean fullscreen )", client)
        self.assertIn('Cvar_Set( "r_stereoEnabled", r_stereo->string )', client)
        self.assertIn('Cvar_Set2( "r_aspectRatio"', client)
        self.assertIn('Cvar_Set2( "r_gl_vendor", cls.glconfig.vendor_string', client)
        self.assertIn('Cvar_VariableIntegerValue( "r_qlOcclusionQueries" )', client)
        self.assertIn("!r_noFastRestart->integer", client)
        self.assertIn('Cvar_Set2( "r_windowedMode", "-1", qtrue )', client)

    def test_final_output_shaders_have_no_ql_postprocess_stage(self) -> None:
        classic = read("code/renderer/tr_arb.c")
        vulkan_shader = read("code/renderervk/shaders/gamma.frag")
        vulkan = read("code/renderervk/vk.c")
        rtx_shader = read("code/rendererrtx/shaders/gamma.frag")
        rtx = read("code/rendererrtx/vk.c")

        for source in (classic, vulkan_shader, vulkan, rtx_shader, rtx):
            self.assertNotIn("R_QLRetailContrast", source)
            self.assertNotIn("retailContrast", source)
            self.assertNotIn("retail_contrast", source)

    def test_stale_fnq3_bloom_profile_repair_is_narrow_and_versioned(self) -> None:
        migration = read("code/renderercommon/tr_fnq3_bloom_config.h")

        self.assertIn('"r_fnq3BloomConfigVersion", "0"', migration)
        self.assertIn('CVAR_ARCHIVE | CVAR_PROTECTED', migration)
        self.assertIn('atof( threshold ) - 0.25f', migration)
        self.assertIn('atof( intensity ) - 0.5f', migration)
        self.assertIn('atoi( passes ) == 1', migration)
        self.assertIn('ri.Cvar_Set( "r_bloom_threshold", "0.75" )', migration)
        self.assertIn('ri.Cvar_Set( "r_bloom_passes", "5" )', migration)

        for forbidden in (
            "r_enablePostProcess",
            "r_enableBloom",
            "r_bloomBrightThreshold",
            "r_qlRetailPostProcessBridge",
        ):
            self.assertNotIn(forbidden, migration)

        for renderer in RENDERERS:
            init = read(f"code/{renderer}/tr_init.c")
            self.assertIn('renderercommon/tr_fnq3_bloom_config.h"', init)
            self.assertLess(
                init.index("R_QLRegisterRendererCvars();"),
                init.index("R_MigrateFnQ3BloomConfig();"),
            )

    def test_fnq3_controls_exclusively_own_framebuffer_and_bloom_storage(self) -> None:
        classic = read("code/renderer/tr_arb.c")
        vulkan = read("code/renderervk/vk.c")
        rtx = read("code/rendererrtx/vk.c")

        contract = read("code/renderercommon/tr_ql_cvars.h")

        self.assertNotIn("r_floatingPointFBOs", contract)
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
