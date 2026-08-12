"""Spawn/teleport view-angle anchor gates.

A client's `cl.viewangles` is a free-running accumulator: it only describes the
player's actual view relative to `ps.delta_angles`, which the game re-anchors on
every spawn from the usercmd the engine hands back through `trap_GetUsercmd()`.
That call is served from `client_t::lastUsercmd`, so any engine path that spawns
a client against a cleared or pre-gamestate command throws the view onto the raw
accumulator, and pmove clamps whatever falls outside +/-90 degrees of pitch. The
visible result is a player who spawns or teleports staring straight up.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str:
    signature = re.compile(
        rf"^[^\n;]*\b{re.escape(name)}\s*\([^;{{]*\)\s*\{{", re.MULTILINE | re.DOTALL
    )
    match = signature.search(source)
    if match is None:
        return ""
    open_brace = source.find("{", match.start())
    depth = 0
    for index in range(open_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start() : index + 1]
    return source[match.start() :]


class SpawnViewAngleAnchorTests(unittest.TestCase):
    def test_entering_the_world_takes_the_command_that_brought_the_client_in(self) -> None:
        self.assertIn(
            "void SV_ClientEnterWorld( client_t *client, const usercmd_t *cmd );",
            read("code/server/server.h"),
        )

    def test_anchor_is_in_place_before_the_game_spawns_the_client(self) -> None:
        body = function_body(read("code/server/sv_client.cpp"), "SV_ClientEnterWorld")

        self.assertIn("if ( cmd != nullptr ) {", body)
        self.assertIn("client->lastUsercmd = *cmd;", body)
        # GAME_CLIENT_BEGIN is what spawns the client, and the spawn reads the
        # anchor back out through trap_GetUsercmd().
        self.assertLess(
            body.index("client->lastUsercmd = *cmd;"),
            body.index("VM_Call( gvm, 1, GAME_CLIENT_BEGIN, clientNum );"),
        )

    def test_adopting_the_anchor_never_rewinds_the_acceptance_gate(self) -> None:
        """Angles come from the entering command; the serverTime gate does not,
        so a lagged client cannot replay commands from before the gamestate."""
        body = function_body(read("code/server/sv_client.cpp"), "SV_ClientEnterWorld")

        self.assertIn("const int acceptFrom = client->lastUsercmd.serverTime;", body)
        self.assertIn(
            "if ( fnql::net::IsNewerCounter( acceptFrom, cmd->serverTime ) ) {",
            body,
        )
        self.assertIn("client->lastUsercmd.serverTime = acceptFrom;", body)

    def test_first_move_packet_supplies_the_anchor(self) -> None:
        body = function_body(read("code/server/sv_client.cpp"), "SV_UserMove")

        self.assertIn("SV_ClientEnterWorld( cl, &cmds[0] );", body)
        # The packet is decoded first, so the anchor is a command this client
        # really sent rather than whatever the gate reset left behind.
        self.assertLess(
            body.index("MSG_ReadDeltaUsercmdKey( msg, key, oldcmd, cmd );"),
            body.index("SV_ClientEnterWorld( cl, &cmds[0] );"),
        )
        self.assertLess(
            body.index("SV_ClientEnterWorld( cl, &cmds[0] );"),
            body.index("SV_ClientThink( cl, &cmds[ i ] );"),
        )

    def test_acceptance_gate_resets_do_not_clear_the_anchor(self) -> None:
        """Both resets exist only to drop commands from before a gamestate or a
        map_restart. Clearing the whole command cleared the angles with it, and
        the game respawns players inside exactly that window."""
        cases = (
            (
                "SV_SendClientGameState",
                function_body(read("code/server/sv_client.cpp"), "SV_SendClientGameState"),
                "client->lastUsercmd.serverTime =",
                "fnql::net::CounterSubtract( sv.time, 1u );",
            ),
            (
                "SV_MapRestart_f",
                function_body(read("code/server/sv_ccmds.cpp"), "SV_MapRestart_f"),
                "client.lastUsercmd.serverTime =",
                "fnql::net::CounterSubtract( sv.time, 1u );",
            ),
        )
        for name, body, assignment, gate in cases:
            with self.subTest(function=name):
                self.assertIn(assignment, body)
                self.assertIn(gate, body)
                self.assertNotIn("lastUsercmd = {}", body)


class SpawnViewAngleUnaffectedPathTests(unittest.TestCase):
    def test_restart_and_bot_entries_keep_the_anchor_they_already_had(self) -> None:
        for source in ("code/server/sv_ccmds.cpp", "code/server/sv_init.cpp"):
            with self.subTest(source=source):
                self.assertIn(
                    "SV_ClientEnterWorld( &slot.client, nullptr );", read(source)
                )

    def test_map_restart_spawns_before_it_moves_the_gate(self) -> None:
        body = function_body(read("code/server/sv_ccmds.cpp"), "SV_MapRestart_f")
        self.assertLess(
            body.index("SV_ClientEnterWorld( &slot.client, nullptr );"),
            body.index("client.lastUsercmd.serverTime ="),
        )

    def test_every_executed_command_still_becomes_the_anchor(self) -> None:
        body = function_body(read("code/server/sv_client.cpp"), "SV_ClientThink")
        self.assertIn("cl->lastUsercmd = *cmd;", body)
        self.assertLess(
            body.index("cl->lastUsercmd = *cmd;"),
            body.index("VM_Call( gvm, 1, GAME_CLIENT_THINK, SV_ClientIndex( cl ) );"),
        )

    def test_command_acceptance_gate_retains_order_across_signed_wrap(self) -> None:
        body = function_body(read("code/server/sv_client.cpp"), "SV_UserMove")
        self.assertIn(
            "fnql::net::IsNewerCounter(", body
        )
        self.assertIn(
            "cmds[i].serverTime, cmds[cmdCount - 1].serverTime", body
        )
        self.assertIn(
            "cmds[i].serverTime, cl->lastUsercmd.serverTime", body
        )

    def test_trap_getusercmd_is_still_served_from_the_anchor(self) -> None:
        body = function_body(read("code/server/sv_game.cpp"), "SV_GetUsercmd")
        self.assertIn("*cmd = SV_ClientForIndex( clientNum ).lastUsercmd;", body)


if __name__ == "__main__":
    unittest.main()
