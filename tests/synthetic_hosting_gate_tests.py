"""Source and fixture tests for the retail-free module-hosting gate.

scripts/synthetic_hosting_gate.py generates every asset it serves; these tests
check that the generated BSP satisfies CM_LoadMap's IBSP v47 contract, that
the gate keeps the AGENTS.md launch rules, and that every FnQL-game CI job
runs it against the matching dedicated server.
"""

from __future__ import annotations

import ast
import json
import re
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from scripts import synthetic_hosting_gate as gate
from release_packaging_tests import workflow_jobs

SCRIPT = ROOT / "scripts" / "synthetic_hosting_gate.py"
WORKFLOW = ROOT / ".github" / "workflows" / "release.yml"

# code/qcommon/qfiles.h record sizes for the lumps CM_LoadMap reads.
RECORD_SIZES = {
    gate.LUMP_SHADERS: 72,
    gate.LUMP_PLANES: 16,
    gate.LUMP_NODES: 36,
    gate.LUMP_LEAFS: 48,
    5: 4,  # LUMP_LEAFSURFACES
    gate.LUMP_LEAFBRUSHES: 4,
    gate.LUMP_MODELS: 40,
    gate.LUMP_BRUSHES: 12,
    gate.LUMP_BRUSHSIDES: 8,
    10: 44,  # LUMP_DRAWVERTS
    11: 4,  # LUMP_DRAWINDEXES
    12: 72,  # LUMP_FOGS
    13: 104,  # LUMP_SURFACES
}
ADVERTISEMENT_RECORD = 128  # code/qcommon/ql_bsp.h


def parse_bsp(data: bytes) -> dict:
    ident, version = data[:4], struct.unpack_from("<i", data, 4)[0]
    lumps = [struct.unpack_from("<ii", data, 8 + index * 8) for index in range(gate.HEADER_LUMPS + 1)]

    def rows(index: int, fmt: str) -> list:
        offset, length = lumps[index]
        size = struct.calcsize("<" + fmt)
        return [struct.unpack_from("<" + fmt, data, offset + at) for at in range(0, length, size)]

    offset, length = lumps[gate.LUMP_ENTITIES]
    return {
        "ident": ident,
        "version": version,
        "lumps": lumps,
        "entities": data[offset:offset + length],
        "shaders": rows(gate.LUMP_SHADERS, f"{gate.MAX_QPATH}sii"),
        "planes": rows(gate.LUMP_PLANES, "4f"),
        "nodes": rows(gate.LUMP_NODES, "9i"),
        "leafs": rows(gate.LUMP_LEAFS, "12i"),
        "leafbrushes": [row[0] for row in rows(gate.LUMP_LEAFBRUSHES, "i")],
        "models": rows(gate.LUMP_MODELS, "6f4i"),
        "brushes": rows(gate.LUMP_BRUSHES, "3i"),
        "sides": rows(gate.LUMP_BRUSHSIDES, "2i"),
    }


def brush_bounds(bsp: dict, brush: tuple) -> tuple:
    """CM_BoundBrush: sides 0..5 are -x, +x, -y, +y, -z, +z."""
    first, count, _shader = brush
    planes = [bsp["planes"][bsp["sides"][first + side][0]] for side in range(count)]
    mins = tuple(-planes[axis * 2][3] for axis in range(3))
    maxs = tuple(planes[axis * 2 + 1][3] for axis in range(3))
    return mins, maxs


def parse_entities(text: bytes) -> list:
    body = text.rstrip(b"\0").decode("ascii")
    return [dict(re.findall(r'"([^"]*)" "([^"]*)"', block))
            for block in re.findall(r"\{([^{}]*)\}", body)]


class GeneratedBspTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.data = gate.build_box_bsp()
        cls.bsp = parse_bsp(cls.data)

    def test_header_is_quake_live_ibsp_v47_with_every_lump_in_range(self) -> None:
        self.assertEqual(self.bsp["ident"], b"IBSP")
        self.assertEqual(self.bsp["version"], 47)
        header_size = 8 + (gate.HEADER_LUMPS + 1) * 8
        for index, (offset, length) in enumerate(self.bsp["lumps"]):
            with self.subTest(lump=index):
                self.assertGreaterEqual(offset, header_size)
                self.assertGreaterEqual(length, 0)
                self.assertLessEqual(offset + length, len(self.data))
                self.assertEqual(offset % 4, 0)
                if index in RECORD_SIZES:
                    self.assertEqual(length % RECORD_SIZES[index], 0, "funny lump size")
        # QL's extra lump: present, empty, and a whole number of 128-byte records
        self.assertEqual(self.bsp["lumps"][gate.HEADER_LUMPS][1] % ADVERTISEMENT_RECORD, 0)

    def test_lumps_cm_load_map_requires_are_populated(self) -> None:
        for key in ("shaders", "planes", "nodes", "leafs", "models"):
            with self.subTest(lump=key):
                self.assertGreaterEqual(len(self.bsp[key]), 1)
        name, surface_flags, contents = self.bsp["shaders"][0]
        self.assertTrue(name.rstrip(b"\0").startswith(b"textures/"))
        self.assertEqual(contents, gate.CONTENTS_SOLID)
        self.assertEqual(surface_flags, 0)

    def test_references_stay_in_bounds(self) -> None:
        planes = len(self.bsp["planes"])
        brushes = len(self.bsp["brushes"])
        for first, count, shader in self.bsp["brushes"]:
            self.assertEqual(count, 6)
            self.assertLessEqual(first + count, len(self.bsp["sides"]))
            self.assertLess(shader, len(self.bsp["shaders"]))
        for plane, shader in self.bsp["sides"]:
            self.assertLess(plane, planes)
            self.assertLess(shader, len(self.bsp["shaders"]))
        for index in self.bsp["leafbrushes"]:
            self.assertLess(index, brushes)
        for leaf in self.bsp["leafs"]:
            first, count = leaf[10], leaf[11]
            self.assertLessEqual(first + count, len(self.bsp["leafbrushes"]))
            self.assertEqual(leaf[8:10], (0, 0), "no draw surfaces")
            self.assertEqual(leaf[0:2], (0, 0), "one cluster and one area")
        for node in self.bsp["nodes"]:
            self.assertLess(node[0], planes)
            for child in node[1:3]:
                self.assertLess(child, 0, "the single node's children are leaves")
                self.assertLess(-(child + 1), len(self.bsp["leafs"]))
        world = self.bsp["models"][0]
        self.assertEqual(world[6:], (0, 0, 0, brushes))

    def test_planes_come_in_opposite_pairs(self) -> None:
        planes = self.bsp["planes"]
        self.assertEqual(len(planes) % 2, 0)
        for even in range(0, len(planes), 2):
            self.assertEqual(planes[even + 1], tuple(-value for value in planes[even]))
        for plane, _shader in self.bsp["sides"]:
            self.assertEqual(plane % 2, 0)

    def test_brushes_close_the_room_and_leave_the_spawns_free(self) -> None:
        bounds = [brush_bounds(self.bsp, brush) for brush in self.bsp["brushes"]]
        self.assertEqual(bounds, [tuple(tuple(map(float, corner)) for corner in brush)
                                  for brush in gate.room_brushes()])
        inner_mins, inner_maxs = gate.ROOM_MINS, gate.ROOM_MAXS
        for axis in range(3):
            # one slab caps each side of each axis across the whole room
            low = [b for b in bounds if b[1][axis] == inner_mins[axis]]
            high = [b for b in bounds if b[0][axis] == inner_maxs[axis]]
            self.assertEqual(len(low), 1)
            self.assertEqual(len(high), 1)
            for mins, maxs in low + high:
                for other in range(3):
                    if other != axis:
                        self.assertLessEqual(mins[other], inner_mins[other])
                        self.assertGreaterEqual(maxs[other], inner_maxs[other])
        for x, y, z, _angle in gate.SPAWN_POINTS:
            point = (x, y, z)
            self.assertTrue(all(inner_mins[a] < point[a] < inner_maxs[a] for a in range(3)))
            for mins, maxs in bounds:
                self.assertFalse(all(mins[a] <= point[a] <= maxs[a] for a in range(3)))

    def test_each_leaf_lists_the_brushes_reaching_its_half(self) -> None:
        bounds = [brush_bounds(self.bsp, brush) for brush in self.bsp["brushes"]]
        (plane_index, front, back, *_box), = self.bsp["nodes"]
        normal = self.bsp["planes"][plane_index]
        self.assertEqual(normal, (1.0, 0.0, 0.0, 0.0))
        for child, side in ((front, 1), (back, -1)):
            leaf = self.bsp["leafs"][-(child + 1)]
            listed = set(self.bsp["leafbrushes"][leaf[10]:leaf[10] + leaf[11]])
            expected = {index for index, (mins, maxs) in enumerate(bounds)
                        if (maxs[0] > 0 if side > 0 else mins[0] < 0)}
            self.assertEqual(listed, expected)

    def test_entities_hold_a_worldspawn_and_deathmatch_spawns(self) -> None:
        self.assertTrue(self.bsp["entities"].endswith(b"\0"))
        entities = parse_entities(self.bsp["entities"])
        self.assertEqual(entities[0]["classname"], "worldspawn")
        spawns = [e for e in entities if e.get("classname") == "info_player_deathmatch"]
        self.assertEqual(len(spawns), len(gate.SPAWN_POINTS))
        for entity, (x, y, z, angle) in zip(spawns, gate.SPAWN_POINTS):
            self.assertEqual(entity["origin"], f"{x} {y} {z}")
            self.assertEqual(entity["angle"], str(angle))

    def test_generation_is_deterministic(self) -> None:
        self.assertEqual(gate.build_box_bsp(), self.data)


class GeneratedAssetTests(unittest.TestCase):
    def test_write_assets_writes_only_the_generated_game_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            written = gate.write_assets(base)
            names = sorted(path.relative_to(base).as_posix() for path in written)
            game = gate.GAME_DIR
            self.assertEqual(names, sorted([
                f"{game}/default.cfg",
                f"{game}/{gate.FACTORY_FILE}",
                f"{game}/maps/{gate.MAP_NAME}.bsp",
            ]))
            files = sorted(p.relative_to(base).as_posix() for p in base.rglob("*") if p.is_file())
            self.assertEqual(files, names)
            self.assertGreater((base / game / "default.cfg").stat().st_size, 0)
            self.assertEqual((base / game / "maps" / f"{gate.MAP_NAME}.bsp").read_bytes(),
                             gate.build_box_bsp())

    def test_game_directory_is_not_baseq3(self) -> None:
        # FS_CheckIdPaks stops a baseq3 server without retail pak00.pk3; the
        # gate uses its own game directory instead of faking that file.
        self.assertNotEqual(gate.GAME_DIR.lower(), "baseq3")
        self.assertNotIn("pak00", SCRIPT.read_text(encoding="utf-8").replace(
            "retail ``pak00.pk3``", ""))

    def test_factory_file_is_one_ffa_factory(self) -> None:
        factories = json.loads(gate.factory_text())
        self.assertEqual(len(factories), 1)
        factory = factories[0]
        self.assertEqual(factory["id"], gate.FACTORY_ID)
        self.assertEqual(factory["basegt"], "ffa")
        self.assertEqual(factory["cvars"], {})
        self.assertTrue(gate.FACTORY_FILE.startswith("scripts/"))
        self.assertTrue(gate.FACTORY_FILE.endswith(".factories"))


class GateLaunchTests(unittest.TestCase):
    def test_module_names_map_to_enabled_targets(self) -> None:
        self.assertEqual(gate.module_arch("fnql_qagamex86.dll", "windows"), "x86")
        self.assertEqual(gate.module_arch("fnql_qagamex86_64.dll", "windows"), "x86_64")
        self.assertEqual(gate.module_arch("fnql_qagamei386.so", "linux"), "x86")
        self.assertEqual(gate.module_arch("fnql_qagamex86_64.so", "linux"), "x86_64")
        for name, platform in (("qagamex86.dll", "windows"), ("fnql_cgamex86.dll", "windows"),
                               ("fnql_qagamex86.dll", "linux"), ("fnql_qagamei386.so", "windows")):
            with self.subTest(name=name, platform=platform), self.assertRaises(SystemExit):
                gate.module_arch(name, platform)

    def test_server_command_is_headless_and_keeps_steam_defaults(self) -> None:
        command = gate.server_command(Path("fnql.ded"), Path("base"), Path("home"), 27996, 200)
        settings = {}
        for index, token in enumerate(command):
            if token == "+set":
                settings[command[index + 1]] = command[index + 2]
        self.assertEqual(settings["com_headless"], "1")
        self.assertEqual(settings["vm_gameProvider"], "fnql")
        self.assertEqual(settings["bot_enable"], "0")
        self.assertEqual(settings["fs_game"], gate.GAME_DIR)
        self.assertEqual(settings["fs_steampath"], "")
        for untouched in ("com_steamIntegration", "cl_webuiEnable", "dedicated"):
            self.assertNotIn(untouched, settings)
        startup = settings["serverstartup"].split("; ")
        self.assertEqual(startup[0], f"map {gate.MAP_NAME} {gate.FACTORY_ID}")
        self.assertIn("wait 200", startup)
        self.assertEqual(startup[-1], f"echo {gate.DONE_MARKER}")

    def good_log(self, module: str) -> str:
        return "\n".join([
            "0 files in 0 pk3 files",
            f"^7loaded factories from {gate.FACTORY_FILE}, total 1",
            "VM_LoadDll(qagame): FnQL-game 0.1.0 module (capabilities 0x0)",
            "------- Game Initialization -------",
            f"qagame : native {module} (FnQL-game 0.1.0, capabilities 0x0)",
            "g_gametype           0",
            f"mapname              {gate.MAP_NAME}",
            "fnql_game            0.1.0",
            f"g_factory            {gate.FACTORY_ID}",
            f"map: {gate.MAP_NAME}",
            gate.DONE_MARKER,
            "",
        ])

    def test_check_log_accepts_a_hosted_module(self) -> None:
        log = self.good_log("fnql_qagamex86_64.so")
        self.assertEqual(gate.check_log(log, "fnql_qagamex86_64.so"), [])
        self.assertNotEqual(gate.check_log(log, "fnql_qagamei386.so"), [])

    def test_check_log_rejects_failures_and_retail_assets(self) -> None:
        module = "fnql_qagamex86.dll"
        log = self.good_log(module)
        bad_logs = {
            "error": log + "********************\nERROR: CM_LoadMap: bad\n",
            "fallback": log + "loading the retail module instead\n",
            "pk3": log.replace("0 files in 0 pk3 files", "9290 files in 2 pk3 files"),
            "no pk3 summary": log.replace("0 files in 0 pk3 files\n", ""),
            "retail factories": log + "loaded factories from scripts/factories.txt, total 22\n",
            "other map": log.replace(f"mapname              {gate.MAP_NAME}", "mapname campgrounds"),
            "no identity": log.replace("fnql_game            0.1.0\n", ""),
            "unfinished": log.replace(gate.DONE_MARKER, ""),
        }
        for name, bad in bad_logs.items():
            with self.subTest(case=name):
                self.assertNotEqual(gate.check_log(bad, module), [])

    def test_script_runs_on_the_release_container_python(self) -> None:
        # fnql-game-linux runs it with Ubuntu 20.04's Python 3.8.
        source = SCRIPT.read_text(encoding="utf-8")
        ast.parse(source, feature_version=(3, 8))
        self.assertNotRegex(source, r"write_text\([^)]*newline=")
        self.assertNotRegex(source, r"\b(?:list|dict|tuple)\[[^]]*\]\s*(?:=|\()")
        # no retail install lookup: the gate must never depend on retail files
        for retail in ("FNQL_RETAIL_QL_PATH", "steamapps", "pak00.pk3\""):
            self.assertNotIn(retail, source)


class HostingGateWorkflowTests(unittest.TestCase):
    JOBS = {
        # job: (engine, module)
        "fnql-game-windows": ("hosting-ded/fnql.ded.exe", "fnql_qagamex86.dll"),
        "fnql-game-linux": ("hosting-ded/fnql.ded", "fnql_qagamei386.so"),
        "fnql-game-windows-x86_64": ("hosting-ded/fnql.ded.x64.exe", "fnql_qagamex86_64.dll"),
        "fnql-game-linux-x86_64": ("hosting-ded/fnql.ded.x86_64", "fnql_qagamex86_64.so"),
    }

    @classmethod
    def setUpClass(cls) -> None:
        cls.jobs = workflow_jobs(WORKFLOW.read_text(encoding="utf-8"))

    def test_every_fnql_game_job_builds_a_server_and_runs_the_gate(self) -> None:
        for job, (engine, module) in self.JOBS.items():
            with self.subTest(job=job):
                body = self.jobs[job]
                gate_command = (f"FnQL/scripts/synthetic_hosting_gate.py run --engine {engine} "
                                f"--module game-stage/baseq3/{module}")
                self.assertIn(gate_command, body)
                self.assertIn("meson setup hosting-ded FnQL ", body)
                self.assertIn("meson compile -C hosting-ded", body)
                setup = re.search(r"meson setup hosting-ded FnQL [^\n]*", body).group(0)
                for option in ("-Dbuild-client=false", "-Daudio-tests=false",
                               "-Dglx-tests=false", "-Dcurl=disabled", "-Dsdl=disabled"):
                    self.assertIn(option, setup)
                # the gate runs on the staged module, before the upload
                self.assertLess(body.index("fnql_game.py stage"), body.index(gate_command))
                self.assertLess(body.index(gate_command), body.index("actions/upload-artifact"))
                gate_step = body[body.index("- name: Run FnQL-game module-hosting gate"):]
                gate_step = gate_step[:gate_step.index("\n\n")]
                self.assertIn('FNQL_HEADLESS: "1"', gate_step)
                self.assertNotIn("com_steamIntegration", body)
                self.assertNotIn("cl_webuiEnable", body)

    def test_server_builds_match_each_job_target(self) -> None:
        windows_x86 = self.jobs["fnql-game-windows"]
        windows_x64 = self.jobs["fnql-game-windows-x86_64"]
        linux_x86 = self.jobs["fnql-game-linux"]
        linux_x64 = self.jobs["fnql-game-linux-x86_64"]
        build_step = "- name: Build FnQL dedicated server for the hosting gate"
        x86_build = windows_x86[windows_x86.index(build_step):]
        self.assertIn("VsDevCmd.bat\" -arch=x86 -host_arch=x64", x86_build)
        x64_build = windows_x64[windows_x64.index(build_step):]
        self.assertIn("VsDevCmd.bat\" -arch=x64 -host_arch=x64", x64_build)
        self.assertIn("--cross-file FnQL/misc/meson/linux-x86.ini", linux_x86)
        self.assertIn("apt-get -y install g++-multilib", linux_x86)
        self.assertNotIn("misc/meson/linux-x86.ini", linux_x64)
        self.assertIn("apt-get -y install g++\n", linux_x64)
        for body in (linux_x86, linux_x64):
            self.assertIn("python3 FnQL/scripts/synthetic_hosting_gate.py", body)


if __name__ == "__main__":
    unittest.main()
