from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
QFILES = ROOT / "code" / "qcommon" / "qfiles.h"
RENDERER_DIRS = ("renderer", "renderervk", "rendererrtx")

# Retail Quake Live doubled Quake III's per-surface tessellator budget; QLSRP
# qcommon/qfiles.h carries SHADER_MAX_VERTEXES 2000 and retail's R_LoadMD3
# rejects on `>`, so a surface sitting exactly on the cap still loads there.
RETAIL_SHADER_MAX_VERTEXES = 2000


def _define(source: str, name: str) -> str:
    match = re.search(rf"(?m)^#define\s+{name}\s+(?P<value>\S+)", source)
    assert match is not None, f"{name} is not defined"
    return match.group("value")


class SurfaceGeometryLimitsSourceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.qfiles = QFILES.read_text(encoding="utf-8")

    def test_batch_covers_every_surface_retail_accepts(self) -> None:
        verts = int(_define(self.qfiles, "SHADER_MAX_VERTEXES"))
        self.assertEqual(verts, RETAIL_SHADER_MAX_VERTEXES)
        self.assertEqual(
            _define(self.qfiles, "SHADER_MAX_INDEXES"), "(6*SHADER_MAX_VERTEXES)"
        )
        # 4000 triangles is the retail per-surface companion cap.
        self.assertEqual(6 * verts, 3 * 4000)

    def test_batch_capacity_preserves_simd_member_alignment(self) -> None:
        verts = int(_define(self.qfiles, "SHADER_MAX_VERTEXES"))
        indexes = 6 * verts
        # shaderCommands_t starts with the index array, followed by vec4
        # vertex arrays. Every variable-sized member must end on a 16-byte
        # boundary on 32-bit builds; an odd spare vertex breaks this contract.
        self.assertEqual((indexes * 4) % 16, 0)
        self.assertEqual((verts * 16) % 16, 0)
        self.assertEqual((verts * 8) % 16, 0)
        self.assertEqual((verts * 4) % 16, 0)

    def test_batch_stays_within_the_md3_format_caps(self) -> None:
        verts = int(_define(self.qfiles, "SHADER_MAX_VERTEXES"))
        self.assertLessEqual(verts, int(_define(self.qfiles, "MD3_MAX_VERTS")) + 1)

    def test_renderers_derive_batch_arrays_from_the_shared_limits(self) -> None:
        for renderer in RENDERER_DIRS:
            local = (ROOT / "code" / renderer / "tr_local.h").read_text(
                encoding="utf-8"
            )
            with self.subTest(renderer=renderer):
                self.assertIn("indexes[SHADER_MAX_INDEXES]", local)
                self.assertIn("xyz[SHADER_MAX_VERTEXES*2]", local)
                self.assertIn("normal[SHADER_MAX_VERTEXES]", local)
                self.assertIn("vertexColors[SHADER_MAX_VERTEXES]", local)
                # a literal budget here would silently desync from qfiles.h
                self.assertNotRegex(local, r"\[\s*(?:1000|2000|2001)\s*\]")

    def test_overflow_predicates_allow_the_exact_retail_capacity(self) -> None:
        for renderer in RENDERER_DIRS:
            surface = (ROOT / "code" / renderer / "tr_surface.c").read_text(
                encoding="utf-8"
            )
            model = (ROOT / "code" / renderer / "tr_model.c").read_text(
                encoding="utf-8"
            )
            with self.subTest(renderer=renderer):
                self.assertIn(
                    "tess.numVertexes + verts <= SHADER_MAX_VERTEXES", surface
                )
                self.assertIn(
                    "tess.numIndexes + indexes <= SHADER_MAX_INDEXES", surface
                )
                self.assertIn("surf->numVerts > SHADER_MAX_VERTEXES", model)
                self.assertIn(
                    "surf->numTriangles*3 > SHADER_MAX_INDEXES", model
                )

    def test_iqm_influence_scratch_stays_off_the_stack(self) -> None:
        for renderer in RENDERER_DIRS:
            iqm = (ROOT / "code" / renderer / "tr_model_iqm.c").read_text(
                encoding="utf-8"
            )
            with self.subTest(renderer=renderer):
                self.assertIn(
                    "static float\tinfluenceVtxMat[SHADER_MAX_VERTEXES * 12];", iqm
                )
                self.assertIn(
                    "static float\tinfluenceNrmMat[SHADER_MAX_VERTEXES * 9];", iqm
                )


if __name__ == "__main__":
    unittest.main()
