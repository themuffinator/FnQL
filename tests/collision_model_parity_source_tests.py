from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class CollisionModelParitySourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.load = read("code/qcommon/cm_load.c")
        cls.trace = read("code/qcommon/cm_trace.c")
        cls.surfaceflags = read("code/qcommon/surfaceflags.h")
        cls.world = read("code/server/sv_world.cpp")
        cls.game = read("code/server/sv_game.cpp")
        cls.server_header = read("code/server/server.h")

    def test_both_retail_temporary_handles_reach_the_shared_model(self) -> None:
        resolver = function_body(
            self.load,
            "cmodel_t *CM_ClipHandleToModel( clipHandle_t handle )",
        )
        self.assertIn("CM_IsTemporaryModelHandle( handle )", resolver)
        self.assertIn("return &box_model;", resolver)

    def test_trace_shape_gates_the_entity_capsule(self) -> None:
        selector = function_body(
            self.world,
            "clipHandle_t SV_ClipHandleForEntity(",
        )
        self.assertIn("SV_AsBool( capsule )", selector)
        self.assertRegex(
            selector,
            re.compile(r"UseCapsuleEntityModel\(\s*SV_AsBool\( capsule \),"
                       r"\s*\( ent->r\.svFlags & SVF_CAPSULE \) != 0 \)"),
        )

    def test_every_server_query_supplies_its_trace_shape(self) -> None:
        self.assertIn(
            "SV_ClipHandleForEntity( const sharedEntity_t *ent, qboolean capsule );",
            self.server_header,
        )
        self.assertIn("SV_ClipHandleForEntity( touch, capsule )", self.world)
        self.assertIn(
            "SV_ClipHandleForEntity( touch, SV_QBool( clip.capsule ) )",
            self.world,
        )
        self.assertIn("SV_ClipHandleForEntity( hit, qfalse )", self.world)
        self.assertIn(
            "SV_ClipHandleForEntity( gEnt, SV_QBool( capsule ) )",
            self.game,
        )
        combined = self.world + self.game
        self.assertNotRegex(combined, r"SV_ClipHandleForEntity\(\s*\w+\s*\)")

    def test_retail_capsule_body_and_head_trace_are_preserved(self) -> None:
        capsule_trace = function_body(
            self.trace,
            "static void CM_TraceCapsuleThroughCapsule(",
        )
        self.assertIn("CM_BuildRetailCapsuleProfile(", capsule_trace)
        self.assertIn("CM_TraceThroughVerticalCylinder(", capsule_trace)
        self.assertIn("CM_TraceThroughSphere(", capsule_trace)
        self.assertIn(
            "tw->trace.contents = CM_RetailCapsuleHitContents(",
            capsule_trace,
        )
        self.assertNotIn("startbottom", capsule_trace)
        self.assertRegex(
            self.surfaceflags,
            re.compile(r"#define CONTENTS_HEAD\s+0x0400"),
        )

    def test_retail_cylinder_scale_and_zero_fraction_contract_are_preserved(
        self,
    ) -> None:
        cylinder_trace = function_body(
            self.trace,
            "static void CM_TraceThroughVerticalCylinder(",
        )
        self.assertIn('"sv_cylinderScale", "1.1f"', cylinder_trace)
        self.assertIn("radius *= cylinderScale->value", cylinder_trace)
        core_trace = function_body(
            self.trace,
            "static void CM_Trace( trace_t *results,",
        )
        self.assertIn(
            "CM_TraceResultHasValidPlaneContract( &tw.trace )",
            core_trace,
        )


if __name__ == "__main__":
    unittest.main()
