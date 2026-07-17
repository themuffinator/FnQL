from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CLIENT_CGAME = ROOT / "code" / "client" / "cl_cgame.cpp"


def source_section(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


class LiquidInteractionSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = CLIENT_CGAME.read_text(encoding="utf-8")

    def test_feed_is_renderer_optional_and_cvar_guarded(self) -> None:
        feed = source_section(
            self.source,
            "static qboolean CL_LiquidInteractionFeedEnabled",
            "static void CL_RebuildLiquidEntityKinds",
        )

        self.assertIn("re.AddLiquidInteractionToScene", feed)
        self.assertIn('Cvar_VariableIntegerValue( "r_liquid" ) > 0', feed)
        self.assertIn('Cvar_VariableValue( "r_liquidRipples" ) > 0.0f', feed)
        self.assertIn("cl_liquidFeedCheckFrame != cls.framecount", feed)
        self.assertIn("CL_ResetLiquidMotionTracks", feed)

    def test_only_players_and_missiles_are_classified(self) -> None:
        classify = source_section(
            self.source,
            "static void CL_RebuildLiquidEntityKinds",
            "static qboolean CL_TraceLiquidHit",
        )

        self.assertIn("state->eType != ET_PLAYER && state->eType != ET_MISSILE", classify)
        self.assertIn("cl.snap.ps.clientNum", classify)
        self.assertIn("SNAPFLAG_SERVERCOUNT", classify)

    def test_crossings_use_world_liquid_collision_and_reject_teleports(self) -> None:
        motion = source_section(
            self.source,
            "static qboolean CL_TraceLiquidHit",
            "static void CL_UpdateLiquidEntityPosition",
        )

        self.assertIn("CM_PointContents", self.source)
        self.assertIn("CM_BoxTrace", motion)
        self.assertIn("MASK_WATER", motion)
        self.assertIn("CL_TraceLiquidHit( track->origin, origin, boundary )", motion)
        self.assertIn("CL_TraceLiquidHit( origin, track->origin, exitBoundary )", motion)
        self.assertIn("EF_TELEPORT_BIT", motion)

    def test_equal_time_samples_are_idempotent_for_stereo_rendering(self) -> None:
        motion = source_section(
            self.source,
            "static void CL_UpdateLiquidMotionTrack",
            "static void CL_UpdateLiquidEntityPosition",
        )

        self.assertIn("time < track->time", motion)
        self.assertIn("if ( time == track->time )", motion)
        self.assertLess(
            motion.index("if ( time == track->time )"),
            motion.index("elapsed = (int64_t)time - (int64_t)track->time"),
        )
        self.assertIn("(int64_t)time - (int64_t)track->wakeTime", motion)

    def test_existing_cgame_calls_feed_demo_stable_times(self) -> None:
        sound_case = source_section(
            self.source,
            "case CG_S_UPDATEENTITYPOSITION:",
            "case CG_S_RESPATIALIZE:",
        )
        render_case = source_section(
            self.source,
            "case CG_R_RENDERSCENE:",
            "case CG_R_SETCOLOR:",
        )

        self.assertLess(sound_case.index("S_UpdateEntityPosition"), sound_case.index("CL_UpdateLiquidEntityPosition"))
        self.assertIn("origin, cl.serverTime, kind->eFlags", self.source)
        self.assertIn("CL_UpdateLocalViewLiquidInteraction( refdef )", render_case)
        self.assertIn("refdef->time", self.source)
        self.assertLess(
            render_case.index("CL_UpdateLocalViewLiquidInteraction"),
            render_case.index("re.RenderScene"),
        )

    def test_cgame_lifecycle_resets_visual_tracking(self) -> None:
        self.assertGreaterEqual(self.source.count("CL_ClearLiquidInteractionState();"), 2)

    def test_renderer_ripple_age_is_checked_before_widened_subtraction(self) -> None:
        for relative_path in ("code/renderer/tr_shade.c", "code/renderervk/tr_shade.c"):
            shade = (ROOT / relative_path).read_text(encoding="utf-8")
            ripple = source_section(
                shade,
                "const liquidInteraction_t *interaction =",
                "life = 1.0f - age",
            )

            active_check = "R_LiquidInteractionActive( interaction, backEnd.refdef.time )"
            age_assignment = "age = (float)( (int64_t)backEnd.refdef.time -"
            self.assertIn("float age;", ripple)
            self.assertIn(active_check, ripple)
            self.assertIn(age_assignment, ripple)
            self.assertIn("(int64_t)interaction->time", ripple)
            self.assertLess(ripple.index(active_check), ripple.index(age_assignment))
            self.assertNotIn("backEnd.refdef.time - interaction->time", ripple)


if __name__ == "__main__":
    unittest.main()
