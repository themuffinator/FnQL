from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


class RendererPortSafetyTests(unittest.TestCase):
    def test_capture_hud_guard_covers_vm_native_text_and_3d_hud_scenes(self) -> None:
        client = read("code/client/cl_cgame.cpp")
        render_case = client[client.index("case CG_R_RENDERSCENE:") : client.index("case CG_R_SETCOLOR:")]
        stretch_case = client[client.index("case CG_R_DRAWSTRETCHPIC:") : client.index("case CG_R_MODELBOUNDS:")]
        draw_text = client[client.index("static void QDECL QL_CG_trap_DrawScaledText") : client.index("static uint64_t QDECL QL_CG_trap_MeasureText")]
        measure_text = client[client.index("static uint64_t QDECL QL_CG_trap_MeasureText") : client.index("static int QDECL QL_CG_trap_IsClientMuted")]

        self.assertIn("CL_CaptureHidesHud()", render_case)
        self.assertIn("RDF_NOWORLDMODEL", render_case)
        self.assertIn("cl_enemyHighlightNumPending = 0;", render_case)
        self.assertLess(render_case.index("RDF_NOWORLDMODEL"), render_case.index("RDF_NOFIRSTPERSON"))
        self.assertIn("!CL_CaptureHidesHud()", stretch_case)
        self.assertIn("!CL_CaptureHidesHud()", draw_text)
        self.assertIn("RE_MeasureScaledText", measure_text)
        self.assertNotIn("CL_CaptureHidesHud", measure_text)

    def test_motion_blur_ignores_portals_without_resetting_primary_history(self) -> None:
        checks = (
            ("code/renderer/tr_arb.c", "void FBO_MotionBlur", "motionBlurFrame == tr.frameCount"),
            ("code/renderervk/vk.c", "qboolean vk_motion_blur", "backEnd.doneMotionBlur"),
        )
        for path, start_marker, latch_marker in checks:
            source = read(path)
            block = source[source.index(start_marker) : source.index(latch_marker, source.index(start_marker))]
            portal = block.index("R_ViewPassIsPortal( &backEnd.viewParms )")
            portal_branch_end = block.index("}", portal)
            portal_branch = block[portal:portal_branch_end]
            self.assertIn("return", portal_branch)
            self.assertNotIn("ResetView", portal_branch)

    def test_all_retained_renderers_share_bounded_levelshot_validation(self) -> None:
        helper = read("code/renderercommon/tr_levelshot.h")
        self.assertIn("LEVELSHOT_TGA_MAX_DIMENSION 65535", helper)
        self.assertIn("LEVELSHOT_MAX_RGB_BYTES ( (size_t)64 * 1024 * 1024 )", helper)
        self.assertIn("R_LevelshotCheckedRgbBytes", helper)
        self.assertIn("strtod", helper)
        self.assertIn("R_LevelshotFloatIsFinite", helper)

        for renderer in ("renderer", "renderervk", "rendererrtx"):
            source = read(f"code/{renderer}/tr_init.c")
            levelshot = source[source.index("void RB_TakeLevelShot") : source.index("static void R_ScheduleLevelShot")]
            self.assertIn('renderercommon/tr_levelshot.h"', source)
            self.assertIn("R_LevelshotCheckedRgbBytes", source)
            self.assertIn("R_LevelshotFloatIsFinite( r_levelshotDownscale->value )", source)
            self.assertIn("const size_t pixel", source)
            self.assertNotIn("params.outputWidth * params.outputHeight * 3", source)
            self.assertLess(levelshot.index("allsource = RB_ReadPixels"), levelshot.index("rgb = ri.Hunk_AllocateTempMemory"))
            self.assertLess(levelshot.index("rgb = ri.Hunk_AllocateTempMemory"), levelshot.index("buffer = ri.Hunk_AllocateTempMemory"))
            self.assertLess(levelshot.index("Hunk_FreeTempMemory(buffer)"), levelshot.index("Hunk_FreeTempMemory(rgb)"))
            self.assertLess(levelshot.index("Hunk_FreeTempMemory(rgb)"), levelshot.index("Hunk_FreeTempMemory(allsource)"))

if __name__ == "__main__":
    unittest.main()
