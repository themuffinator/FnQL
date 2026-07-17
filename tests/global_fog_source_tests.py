from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


class GlobalFogSourceTests(unittest.TestCase):
    def test_parser_is_bounded_strict_and_renderer_independent(self) -> None:
        parser = read("code/renderercommon/tr_global_fog.h")

        self.assertIn("GLOBAL_FOG_SIDECAR_MAX_BYTES 16384", parser)
        self.assertIn("GLOBAL_FOG_TOKEN_MAX_BYTES 64", parser)
        self.assertIn("const char *end", parser)
        self.assertIn("textLength > GLOBAL_FOG_SIDECAR_MAX_BYTES", parser)
        self.assertIn("*p == '\\0'", parser)
        self.assertIn("R_GlobalFogSetError", parser)
        self.assertIn("fog->mode = GLOBAL_FOG_EXP2;", parser)
        self.assertIn("fog->opacity = 1.0f;", parser)
        self.assertIn("fog->sky = qtrue;", parser)
        for directive in ("color", "mode", "density", "start", "end", "opacity", "sky"):
            self.assertIn(f'Q_stricmp( token, "{directive}" )', parser)
        self.assertIn("color and density are required", parser)
        self.assertIn("linear fog requires end greater than start", parser)
        self.assertIn("unknown directive", parser)

    def test_sidecar_loading_is_opt_in_bounded_and_post_success(self) -> None:
        for renderer in ("renderer", "renderervk"):
            bsp = read(f"code/{renderer}/tr_bsp.c")
            loader_start = bsp.index("static void R_LoadGlobalFogForWorld")
            loader_end = bsp.index("static void HSVtoRGB", loader_start)
            loader = bsp[loader_start:loader_end]

            self.assertLess(loader.index("R_GlobalFogClear"), loader.index("r_globalFog->integer"))
            self.assertLess(loader.index("r_globalFog->integer"), loader.index("ri.FS_ReadFile"))
            self.assertIn('"maps/%s.fog"', loader)
            self.assertIn("GLOBAL_FOG_SIDECAR_MAX_BYTES", loader)
            self.assertIn("R_GlobalFogParse", loader)
            self.assertIn("ri.FS_FreeFile", loader)

            preflight = loader.index("ri.FS_ReadFile( filename, NULL )")
            buffered = loader.index("ri.FS_ReadFile( filename, &buffer.v )")
            first_limit = loader.index("size > GLOBAL_FOG_SIDECAR_MAX_BYTES")
            second_limit = loader.index(
                "size > GLOBAL_FOG_SIDECAR_MAX_BYTES", first_limit + 1
            )
            self.assertLess(preflight, first_limit)
            self.assertLess(first_limit, buffered)
            self.assertLess(buffered, second_limit)

            world_assignment = bsp.rindex("tr.world = &s_worldData;")
            load_call = bsp.rindex("R_LoadGlobalFogForWorld();")
            self.assertLess(world_assignment, load_call)

    def test_cvars_default_off_and_leave_renderer2_unchanged(self) -> None:
        registration = (
            'ri.Cvar_Get( "r_globalFog", "0", CVAR_ARCHIVE_ND | CVAR_LATCH )'
        )
        for renderer in ("renderer", "renderervk"):
            init = read(f"code/{renderer}/tr_init.c")
            local = read(f"code/{renderer}/tr_local.h")
            self.assertIn(registration, init)
            self.assertIn('ri.Cvar_CheckRange( r_globalFog, "0", "1", CV_INTEGER );', init)
            self.assertIn('ri.Cvar_Get( "r_globalFogStrength", "1.0", CVAR_ARCHIVE_ND )', init)
            self.assertIn('ri.Cvar_CheckRange( r_globalFogStrength, "0", "1", CV_FLOAT );', init)
            self.assertIn("globalFog_t\tglobalFog;", local)

        for relative_path in (
            "code/renderer2/tr_init.c",
            "code/renderer2/tr_local.h",
            "code/renderer2/tr_bsp.c",
        ):
            self.assertNotIn("globalFog", read(relative_path))

    def test_root_archive_allowlist_is_narrow_and_preserves_normal_fallback(self) -> None:
        files = read("code/qcommon/files.c")
        helper_start = files.index("static qboolean FS_RootArchiveAllowsMapSidecar")
        helper_end = files.index("static qboolean FS_RootArchiveAllowsFile", helper_start)
        helper = files[helper_start:helper_end]
        read_start = files.index("int FS_ReadFile( const char *qpath")
        read_end = files.index("int FS_ReadProfileFile", read_start)
        read_file = files[read_start:read_end]

        self.assertIn('static const char mapsPrefix[] = "maps/";', helper)
        self.assertIn('COM_CompareExtension( qpath, ".azb" )', helper)
        self.assertIn('COM_CompareExtension( qpath, ".fog" )', helper)
        self.assertNotIn("FS_RootArchiveAllowsAudioZoneSidecar", files)
        self.assertLess(
            read_file.index("FS_ReadFileFromRootArchive"),
            read_file.index("FS_FOpenFileRead"),
        )

        shipped_fog = [path for path in (ROOT / "pkg").rglob("*.fog") if path.is_file()]
        self.assertEqual(shipped_fog, [])

    def test_arb_program_failure_is_feature_local_and_compositor_is_ordered(self) -> None:
        arb = read("code/renderer/tr_arb.c")
        backend = read("code/renderer/tr_backend.c")

        compile_block = re.search(
            r"if \( r_globalFog && r_globalFog->integer \) \{(?P<body>.*?)\n\t\}",
            arb[arb.index("globalFogProgramCompiled = qfalse;", 1000):],
            re.DOTALL,
        )
        self.assertIsNotNone(compile_block)
        assert compile_block is not None
        self.assertIn("ARB_CompileProgramInternal", compile_block.group("body"))
        self.assertIn("qfalse", compile_block.group("body"))
        self.assertNotIn("return qfalse", compile_block.group("body"))
        self.assertNotIn("ARB_CompileProgram( Fragment, globalFogFP", arb)
        self.assertIn("!globalFogProgramCompiled", arb)
        self.assertIn("FBO_DepthTextureAvailable()", arb)
        self.assertIn("FBO_CopyDepthTexture();", arb)
        self.assertIn("FBO_BlitMS( qfalse );", arb)
        self.assertIn("destinationIndex = sourceIndex == 0 ? 1 : 0;", arb)
        self.assertIn('"PARAM epsilon = { 0.0001, 0.0001, 0.0001, 0.0001 }; \\n"', arb)
        self.assertIn('"MAX linear.x, linear.x, epsilon.x; \\n"', arb)

        debug = backend.rindex("RB_DebugGraphics();")
        fog = backend.index("FBO_DrawGlobalFog();", debug)
        motion = backend.index("FBO_MotionBlur();", fog)
        self.assertLess(debug, fog)
        self.assertLess(fog, motion)

    def test_vulkan_shader_and_failure_paths_are_optional(self) -> None:
        shader = read("code/renderervk/shaders/global_fog.frag")
        shader_data = read("code/renderervk/shaders/spirv/shader_data.c")
        vk = read("code/renderervk/vk.c")
        backend = read("code/renderervk/tr_backend.c")

        self.assertIn("uniform sampler2D depth_texture", shader)
        self.assertIn("GlobalFogPushConstants", shader)
        self.assertIn("exp(-pc.fog_params.z * distance)", shader)
        self.assertIn("fog_distance * fog_distance", shader)
        self.assertIn("global_fog_frag_spv", shader_data)
        self.assertIn("SHADER_MODULE_OPTIONAL( global_fog_frag_spv", vk)
        self.assertIn("optional = qtrue;", vk)
        self.assertIn("Vulkan optional global fog pipeline failed", vk)
        self.assertIn("vk.global_fog_pipeline == VK_NULL_HANDLE", vk)
        self.assertIn("float constants[12];", vk)
        self.assertIn("sizeof( constants )", vk)
        self.assertIn("vk_copy_depth_fade();", backend)

        debug = backend.rindex("RB_DebugGraphics();")
        copy = backend.index("vk_copy_depth_fade();", debug)
        fog = backend.index("vk_draw_global_fog();", copy)
        motion = backend.index("vk_motion_blur();", fog)
        self.assertLess(debug, copy)
        self.assertLess(copy, fog)
        self.assertLess(fog, motion)

    def test_technical_contract_is_documented(self) -> None:
        technical = read("docs/fnql/TECHNICAL.md")
        fog = read("docs/fnql/GLOBAL_FOG.md")

        self.assertIn("GLOBAL_FOG.md", technical)
        for expected in (
            "visual-only",
            "r_globalFog",
            "r_globalFogStrength",
            "FnQL-pkg.fnz",
            "renderer2",
            "16 KiB",
        ):
            self.assertIn(expected, fog)


if __name__ == "__main__":
    unittest.main()
