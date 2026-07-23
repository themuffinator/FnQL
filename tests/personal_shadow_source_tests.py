import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_text(relative_path):
    return (ROOT / relative_path).read_text(encoding="utf-8")


class PersonalShadowSourceTests(unittest.TestCase):
    RENDERERS = ("code/renderer", "code/renderervk", "code/rendererrtx")
    MODEL_FILES = ("tr_mesh.c", "tr_animation.c", "tr_model_iqm.c")

    def test_surface_lists_have_shadow_caster_only_flags(self):
        for renderer in self.RENDERERS:
            with self.subTest(renderer=renderer):
                header = read_text(f"{renderer}/tr_local.h")
                main = read_text(f"{renderer}/tr_main.c")
                backend = read_text(f"{renderer}/tr_backend.c")

                self.assertIn("#define DSF_SHADOW_CASTER_ONLY 0x00000001u", header)
                self.assertIn("#define LSF_SHADOW_CASTER_ONLY 0x00000001u", header)
                self.assertIn("unsigned int\t\tflags;", header)
                self.assertIn("void R_AddDrawSurfFlags(", header)
                self.assertIn("void R_AddLitSurfFlags(", header)
                self.assertIn("tr.refdef.drawSurfs[index].flags = flags;", main)
                self.assertIn("litsurf->flags = flags;", main)
                self.assertIn("drawSurf->flags & DSF_SHADOW_CASTER_ONLY", backend)
                self.assertIn("litSurf->flags & LSF_SHADOW_CASTER_ONLY", backend)

    def test_personal_models_keep_draw_casters_but_not_camera_lit_casters(self):
        for renderer in self.RENDERERS:
            for filename in self.MODEL_FILES:
                path = f"{renderer}/{filename}"
                with self.subTest(path=path):
                    source = read_text(path)

                    self.assertRegex(source, r"qboolean\s+personalShadowCaster;")
                    self.assertIn(
                        "personalShadowCaster = personalModel && !(ent->e.renderfx & ( RF_NOSHADOW | RF_DEPTHHACK ));",
                        source,
                    )
                    self.assertIn(
                        "R_AddDrawSurfFlags( (void *)surface, shader, fogNum, 0, DSF_SHADOW_CASTER_ONLY );",
                        source,
                    )
                    self.assertRegex(
                        source,
                        r"R_AddLitSurf(?:Flags)?\(\s*\(void \*\)surface,\s*"
                        r"shader,\s*fogNum",
                    )
                    self.assertNotIn("LSF_SHADOW_CASTER_ONLY", source)
                    self.assertIn("numDlights = 0;", source)
                    self.assertRegex(
                        source,
                        r"if\s*\([^)]*!personalModel[^)]*\)\s*\{",
                    )
                    self.assertRegex(source, r"if\s*\(\s*!personalModel\s*\)")

    def test_canonical_point_caster_pass_handles_third_person_models(self):
        source = read_text(
            "code/renderercommon/tr_dlight_shadow_casters.h"
        )
        canonical = source[
            source.index("static qboolean R_CollectDlightShadowEntityCasters("):
            source.index("void R_AddPlannedDlightShadowCasters(")
        ]

        self.assertIn("ent->e.reType != RT_MODEL", canonical)
        self.assertIn(
            "RF_NOSHADOW | RF_FIRST_PERSON | RF_DEPTHHACK",
            canonical,
        )
        self.assertNotIn("RF_THIRD_PERSON", canonical)
        self.assertIn(
            "!R_DlightShadowEntityTransformValid( ent )",
            canonical,
        )
        self.assertIn("R_AddMD3Surfaces( ent, context );", canonical)
        self.assertIn("R_MDRAddAnimSurfaces( ent, context );", canonical)
        self.assertIn("R_AddIQMSurfaces( ent, context );", canonical)
        self.assertIn("R_AddBrushModelSurfaces( ent, context );", canonical)


if __name__ == "__main__":
    unittest.main()
