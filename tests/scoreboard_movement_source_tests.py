from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def function_body(source: str, name: str) -> str:
    match = re.search(
        rf"(?:static\s+)?(?:void|qboolean|bool|int)\s+{name}\s*\([^)]*\)\s*\{{",
        source,
    )
    if not match:
        raise AssertionError(f"Missing function {name}")

    depth = 1
    for index in range(match.end(), len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.end() : index]
    raise AssertionError(f"Unterminated function {name}")


class ScoreboardMovementSourceTests(unittest.TestCase):
    def test_retail_mousepass_does_not_emit_the_movement_blocking_talk_button(self) -> None:
        source = (ROOT / "code" / "client" / "cl_input.cpp").read_text(
            encoding="utf-8"
        )
        body = function_body(source, "CL_CmdButtons")

        self.assertIn("CatcherBlocksGameplayInput", body)
        self.assertIn("KEYCATCH_RETAIL_MOUSEPASS", body)
        self.assertIn("if ( gameplayInputCaptured )", body)
        self.assertIn("cmd->buttons |= BUTTON_TALK;", body)
        self.assertIn("if ( anykeydown && !gameplayInputCaptured )", body)
        self.assertNotIn("if ( Key_GetCatcher() )", body)

    def test_retail_mousepass_transition_preserves_already_held_movement(self) -> None:
        source = (ROOT / "code" / "client" / "cl_keys.cpp").read_text(
            encoding="utf-8"
        )
        body = function_body(source, "Key_SetCatcher")

        self.assertIn("GameplayCatcherStateChanged", body)
        self.assertIn("KEYCATCH_RETAIL_MOUSEPASS", body)
        self.assertIn("Key_ClearStates();", body)
        self.assertLess(body.index("GameplayCatcherStateChanged"), body.index("Key_ClearStates"))


if __name__ == "__main__":
    unittest.main()
