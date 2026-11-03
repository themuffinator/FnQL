from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RENDERERS = ("renderer", "renderervk", "rendererrtx")


def read_source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class RendererProfileClockTests(unittest.TestCase):
    """Issue #7: com_speeds rf/bk always read 0.

    ri.Milliseconds is the client's per-frame virtual clock (cls.realtime), so
    a start/end pair inside one frame never differs. Profiling must read the
    real clock, while time that feeds rendering must stay on the virtual clock
    so cl_avidemo, timescale, and sv_lockstep captures stay deterministic.
    """

    def test_renderer_import_clock_stays_the_virtual_frame_clock(self) -> None:
        main = read_source("code/client/cl_main.cpp")
        start = main.index("int CL_ScaledMilliseconds( void ) {")
        body = main[start : main.index("\n}", start)]
        self.assertIn("return cls.realtime;", body)
        self.assertIn("rimp.Milliseconds = CL_ScaledMilliseconds;", main)
        self.assertIn("rimp.Microseconds = Sys_Microseconds;", main)

    def test_profile_clock_reads_the_real_clock(self) -> None:
        header = read_source("code/renderercommon/tr_profile_clock.h")
        self.assertIn("static ID_INLINE int R_ProfileMilliseconds( void )", header)
        self.assertIn("ri.Microseconds() / 1000", header)
        for renderer in RENDERERS:
            local = read_source(f"code/{renderer}/tr_local.h")
            self.assertLess(
                local.index('#include "../renderercommon/tr_public.h"'),
                local.index('#include "../renderercommon/tr_profile_clock.h"'),
                renderer,
            )

    def test_frame_timings_use_the_profile_clock(self) -> None:
        for renderer in RENDERERS:
            scene = read_source(f"code/{renderer}/tr_scene.c")
            backend = read_source(f"code/{renderer}/tr_backend.c")
            self.assertIn("startTime = R_ProfileMilliseconds();", scene, renderer)
            self.assertIn(
                "tr.frontEndMsec += R_ProfileMilliseconds() - startTime;", scene, renderer
            )
            self.assertIn("backEnd.pc.msec = R_ProfileMilliseconds()", backend, renderer)
            self.assertIn(
                "c_csmShadowAtlasMsec += R_ProfileMilliseconds() - startMsec;", backend, renderer
            )
            self.assertIn(
                "c_dlightShadowAtlasMsec += R_ProfileMilliseconds() - startMsec;", backend, renderer
            )
            # No profiling delta may fall back to the frame clock.
            for source in (scene, backend):
                self.assertIsNone(
                    re.search(r"ri\.Milliseconds\(\)\s*-", source), renderer
                )
                self.assertNotIn("Msec += ri.Milliseconds()", source, renderer)
        for path in ("code/renderervk/vk.c", "code/rendererrtx/vk.c"):
            self.assertIn(
                "backEnd.pc.msec = R_ProfileMilliseconds() - backEnd.pc.msec;",
                read_source(path),
                path,
            )

    def test_rendered_time_stays_on_the_virtual_clock(self) -> None:
        for renderer in RENDERERS:
            backend = read_source(f"code/{renderer}/tr_backend.c")
            self.assertIn("backEnd.refdef.time = ri.Milliseconds();", backend, renderer)
        for path in (
            "code/renderer/tr_arb.c",
            "code/renderervk/vk.c",
            "code/rendererrtx/vk.c",
        ):
            source = read_source(path)
            self.assertRegex(
                source, r"r_motionBlurStrength->value, ri\.Milliseconds\(\)", path
            )


if __name__ == "__main__":
    unittest.main()
