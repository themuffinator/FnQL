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
        cls.contract = read("code/qcommon/cm_trace_contract.h")
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

    def test_capsule_sweep_keeps_both_cap_spheres(self) -> None:
        """Moving capsules keep a cylinder capped by a sphere at each end.

        Dropping either cap lets a moving capsule pass through the target
        from below or above, so both sphere sweeps have to stay.
        """
        capsule_trace = function_body(
            self.trace,
            "static void CM_TraceCapsuleThroughCapsule(",
        )
        self.assertIn("CM_BuildCapsuleTraceProfile(", capsule_trace)
        self.assertEqual(capsule_trace.count("CM_TraceThroughSphere("), 2)
        self.assertIn("profile.topSphere", capsule_trace)
        self.assertIn("profile.bottomSphere", capsule_trace)
        self.assertIn("startbottom", capsule_trace)
        self.assertIn("endtop", capsule_trace)
        # The cylinder section is only swept when it exists and can be entered.
        self.assertEqual(
            capsule_trace.count("CM_TraceThroughVerticalCylinder("), 1
        )
        self.assertIn("profile.cylinderHalfheight > 0", capsule_trace)
        self.assertIn(
            "tw->start[0] != tw->end[0] || tw->start[1] != tw->end[1]",
            capsule_trace,
        )
        # Retail head metadata is confined to point traces; movement keeps
        # the existing profile even though the retail executable differs.
        self.assertRegex(capsule_trace, r"if \( tw->isPoint \) \{\s*CM_TracePointThroughPlayer")

    def test_retail_cylinder_scale_is_preserved(self) -> None:
        cylinder_trace = function_body(
            self.trace,
            "static void CM_TraceThroughVerticalCylinder(",
        )
        self.assertIn('"sv_cylinderScale", "1.1f"', cylinder_trace)
        self.assertIn("radius *= cylinderScale->value", cylinder_trace)

    def test_plane_invariant_matches_the_producing_path(self) -> None:
        """Only stored BSP planes are unit.

        The analytic capsule plane is scaled by 1/(radius + RADIUS_EPSILON)
        in single precision, so a debug build must not assert unit length on
        that path.
        """
        core_trace = function_body(
            self.trace,
            "static void CM_Trace( trace_t *results,",
        )
        self.assertIn(
            "CM_ValidateTraceResult( &tw.trace, planeSource )",
            core_trace,
        )
        self.assertRegex(
            core_trace,
            re.compile(
                r"cm_tracePlaneSource_t\s+planeSource\s*=\s*"
                r"CM_TRACE_PLANE_STORED;"
            ),
        )
        # Every capsule-vs-capsule dispatch has to declare the analytic plane.
        analytic = re.findall(
            r"planeSource = CM_TRACE_PLANE_ANALYTIC;\s*\n\s*"
            r"(CM_TestCapsuleInCapsule|CM_TraceCapsuleThroughCapsule)\(",
            self.trace,
        )
        self.assertEqual(len(analytic), 4)
        for call in ("CM_TestCapsuleInCapsule(", "CM_TraceCapsuleThroughCapsule("):
            self.assertEqual(self.trace.count(call), 3)

        validator = function_body(
            self.trace,
            "static ID_INLINE void CM_ValidateTraceResult(",
        )
        self.assertIn(
            "CM_TraceResultHasValidPlaneContract( trace, source )",
            validator,
        )
        self.assertIn("assert( valid );", validator)
        self.assertIn("(void)source;", validator)
        # A bare assert dialog names no state, so report the trace first.
        self.assertIn("#ifndef NDEBUG", validator)
        self.assertIn("Com_Printf(", validator)


if __name__ == "__main__":
    unittest.main()
