from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RENDERER_ROOTS = (
    Path("code/renderer"),
    Path("code/renderervk"),
    Path("code/rendererrtx"),
)


class LightmapParitySourceTests(unittest.TestCase):
    def test_vertex_collapse_marks_the_retail_lightmap_diagnostic_stage(self) -> None:
        for renderer_root in RENDERER_ROOTS:
            with self.subTest(renderer=str(renderer_root)):
                local = (ROOT / renderer_root / "tr_local.h").read_text(
                    encoding="utf-8"
                )
                shader = (ROOT / renderer_root / "tr_shader.c").read_text(
                    encoding="utf-8"
                )
                collapse_start = shader.index("static void VertexLightingCollapse")
                collapse_end = shader.index("static void InitShader", collapse_start)
                collapse = shader[collapse_start:collapse_end]

                self.assertIn("qboolean\t\tvertexLightmap;", local)
                self.assertIn(
                    "stages[0].bundle[0].vertexLightmap = qtrue;", collapse
                )

    def test_lightmap_only_backend_handles_vertex_lit_world_stages(self) -> None:
        for renderer_root in RENDERER_ROOTS:
            with self.subTest(renderer=str(renderer_root)):
                shade = (ROOT / renderer_root / "tr_shade.c").read_text(
                    encoding="utf-8"
                )
                generic_start = shade.index("RB_IterateStagesGeneric")
                generic_end = shade.index("RB_StageIteratorGeneric", generic_start)
                generic = shade[generic_start:generic_end]

                self.assertIn("pStage->bundle[0].vertexLightmap", generic)
                self.assertIn("r_lightmap->integer", generic)
                self.assertIn("tr.whiteImage", generic)
                self.assertIn("!qlRendererCvars.uiFullscreen->integer", generic)
                self.assertIn(
                    "pStage->bundle[1].lightmap != LIGHTMAP_INDEX_NONE || "
                    "pStage->bundle[0].vertexLightmap",
                    generic,
                )

        gl_vbo = (ROOT / "code" / "renderer" / "tr_vbo.c").read_text(
            encoding="utf-8"
        )
        vbo_start = gl_vbo.index("static void RB_IterateStagesVBO")
        vbo_end = gl_vbo.index("void RB_StageIteratorVBO", vbo_start)
        vbo = gl_vbo[vbo_start:vbo_end]
        self.assertIn("pStage->bundle[0].vertexLightmap", vbo)
        self.assertIn("GL_Bind( tr.whiteImage );", vbo)
        self.assertIn(
            "pStage->bundle[1].lightmap != LIGHTMAP_INDEX_NONE || "
            "pStage->bundle[0].vertexLightmap",
            vbo,
        )

    def test_intensity_mode_is_live_and_works_with_merged_lightmaps(self) -> None:
        for renderer_root in RENDERER_ROOTS:
            with self.subTest(renderer=str(renderer_root)):
                bsp = (ROOT / renderer_root / "tr_bsp.c").read_text(encoding="utf-8")
                process_start = bsp.index("static float R_ProcessLightmap")
                process_end = bsp.index("return maxIntensity;", process_start)
                process = bsp[process_start:process_end]

                self.assertIn("r_lightmap->integer == 2", process)
                self.assertNotIn("0 && r_lightmap", process)
                self.assertIn("tr.mergeLightmaps ? LIGHTMAP_LEN", process)
                self.assertIn("tr.mergeLightmaps ? LIGHTMAP_BORDER", process)
                self.assertIn("HSVtoRGB", process)
                self.assertGreaterEqual(
                    bsp.count("maxIntensity = R_ProcessLightmap"), 2
                )
                self.assertIn("Brightest lightmap value:", bsp)


if __name__ == "__main__":
    unittest.main()
