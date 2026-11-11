#!/usr/bin/env python3
"""Prove that an FnQL dedicated server hosts FnQL-game's qagame, without retail assets.

FnQL-game's own hosting gate (its scripts/smoke_dedicated.py) runs on a retail
map with bots, so it needs a legitimate Quake Live install and cannot run in
CI.  This gate covers the part that does not need retail data: a dedicated
server loads ``fnql_qagame<ARCH_STRING>`` through ``vm_gameProvider fnql``,
the module identifies itself, initializes the game on a map, and the server
keeps running frames.

Every asset it uses is generated here, at test time, and none is derived from
retail files.  They sit in their own game directory (``fs_game``), not in
``baseq3``: FS_CheckIdPaks (code/qcommon/files.c) stops a ``baseq3`` server
that has no retail ``pak00.pk3``, and this gate does not fake that marker.  For
any other game directory the check only prints its missing-pak notice.

- ``default.cfg``: FS_Restart needs the file to exist and be non-empty
  ("Couldn't load default.cfg"); its content is a comment.
- ``scripts/fnql_hosting_gate.factories``: QL's ``map`` command needs a
  factory (code/server/sv_factory.cpp).  One FFA factory with no cvars.  A map
  with no arena record is allowed for any factory (ArenaSupport::Unknown).
- ``maps/fnql_hosting_box.bsp``: an IBSP v47 box room, built by
  build_box_bsp() below, with a worldspawn and two info_player_deathmatch
  spawn points.

``fs_steampath`` is cleared so a developer machine with Quake Live installed is
as hermetic as CI; the log check rejects any mounted pk3.  Bots stay off
(``bot_enable 0``): they need AAS files and bot scripts, and the retail-asset
gate already covers them.  Steam integration stays at its default, as
AGENTS.md requires; the server carries on without a Steam client.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

ROOT = Path(__file__).resolve().parents[1]
GAME_DIR = "fnql_hosting_gate"
MAP_NAME = "fnql_hosting_box"
FACTORY_ID = "ffa"
FACTORY_FILE = "scripts/fnql_hosting_gate.factories"
DONE_MARKER = "FnQL hosting gate: commands done"

# fnql_game.py's MODULE_SUFFIX, for the qagame slot: the engine loads
# fnql_qagame<ARCH_STRING><ext> (docs/fnql/GAME_MODULES.md).
MODULE_ARCH_BY_PLATFORM = {
    "windows": (("x86", "x86.dll"), ("x86_64", "x86_64.dll")),
    "linux": (("x86", "i386.so"), ("x86_64", "x86_64.so")),
}

# IBSP layout (code/qcommon/qfiles.h).  QL's v47 keeps the 17 v46 lumps and
# appends one advertisement lump descriptor (code/qcommon/ql_bsp.h).
BSP_IDENT = b"IBSP"
BSP_VERSION_QL = 47
HEADER_LUMPS = 17
LUMP_ENTITIES = 0
LUMP_SHADERS = 1
LUMP_PLANES = 2
LUMP_NODES = 3
LUMP_LEAFS = 4
LUMP_LEAFBRUSHES = 6
LUMP_MODELS = 7
LUMP_BRUSHES = 8
LUMP_BRUSHSIDES = 9
MAX_QPATH = 64
CONTENTS_SOLID = 1

# Interior of the room in world units; walls are WALL thick on every side.
ROOM_MINS = (-512, -512, 0)
ROOM_MAXS = (512, 512, 256)
WALL = 16
SPAWN_POINTS = ((-256, 0, 32, 0), (256, 0, 32, 180))

# Each line must appear in the server log; the hosted module's vminfo line is
# added by required_lines().
REQUIRED = (
    "VM_LoadDll(qagame): FnQL-game ",
    "------- Game Initialization -------",
    f"loaded factories from {FACTORY_FILE}, total 1",  # ours, and nothing else
    "map: " + MAP_NAME,
    DONE_MARKER,
)

# serverinfo prints "key<padding>value" lines; each of these must be present.
REQUIRED_SERVERINFO = (
    ("fnql_game", None),  # the module's identity key reached the serverinfo
    ("mapname", MAP_NAME),
    ("g_factory", FACTORY_ID),
    ("g_gametype", "0"),
)

# Any of these means the module, the ABI or the generated assets are broken.
FORBIDDEN = (
    "is not a usable FnQL-game qagame module",
    "loading the retail module instead",
    "Rejected DLL",
    "Invalid factory specified.",
    "Map not supported for this gametype.",
    "Couldn't load default.cfg",
    "********************",  # Com_Error banner
    "ERROR:",
    "Couldn't open logfile: \n",  # garbage cvar strings (vmCvar_t layout drift)
    "loaded factories from scripts/factories.txt",  # a retail factory file was mounted
)

# FS_Startup's "<n> files in <m> pk3 files" summary: the gate mounts no archives.
PK3_SUMMARY = re.compile(r"^\d+ files in (\d+) pk3 files$", re.MULTILINE)

# The server must keep running at least this long after the commands finish.
DEFAULT_HOLD_SECONDS = 5.0


def _lump_bytes(fmt: str, rows: Sequence[Sequence[object]]) -> bytes:
    return b"".join(struct.pack("<" + fmt, *row) for row in rows)


def _box_planes(mins: Sequence[int], maxs: Sequence[int]) -> List[Tuple[float, float, float, float]]:
    """The six axial planes of a box in the order CM_BoundBrush reads them.

    Side 0/1 bound x, 2/3 bound y and 4/5 bound z; the negative-facing side
    stores -mins so its distance is along its own normal.
    """
    planes = []
    for axis in range(3):
        negative = [0.0, 0.0, 0.0]
        positive = [0.0, 0.0, 0.0]
        negative[axis] = -1.0
        positive[axis] = 1.0
        planes.append((negative[0], negative[1], negative[2], float(-mins[axis])))
        planes.append((positive[0], positive[1], positive[2], float(maxs[axis])))
    return planes


def room_brushes() -> List[Tuple[Tuple[int, int, int], Tuple[int, int, int]]]:
    """Six solid slabs enclosing ROOM_MINS..ROOM_MAXS: floor, ceiling, four walls."""
    outer_mins = tuple(value - WALL for value in ROOM_MINS)
    outer_maxs = tuple(value + WALL for value in ROOM_MAXS)
    brushes = []
    for axis in range(3):
        low_maxs = list(outer_maxs)
        low_maxs[axis] = ROOM_MINS[axis]
        high_mins = list(outer_mins)
        high_mins[axis] = ROOM_MAXS[axis]
        brushes.append((tuple(outer_mins), tuple(low_maxs)))
        brushes.append((tuple(high_mins), tuple(outer_maxs)))
    return brushes


def entity_text() -> bytes:
    entities = ['{\n"classname" "worldspawn"\n"message" "FnQL hosting gate box"\n}\n']
    for x, y, z, angle in SPAWN_POINTS:
        entities.append(
            '{\n"classname" "info_player_deathmatch"\n'
            f'"origin" "{x} {y} {z}"\n"angle" "{angle}"\n}}\n'
        )
    return "".join(entities).encode("ascii") + b"\0"


def build_box_bsp() -> bytes:
    """A deterministic IBSP v47 holding one closed box room.

    The tree has a single node splitting the room at x = 0 into two leaves of
    one cluster and one area; each leaf lists the brushes that reach into its
    half.  There are no draw surfaces, lightmaps or visibility data: a
    dedicated server needs only the collision model and the entity string,
    and an absent visibility lump means "everything visible".
    """
    brushes = room_brushes()

    planes: List[Tuple[float, float, float, float]] = []
    brush_rows = []
    side_rows = []
    for mins, maxs in brushes:
        brush_rows.append((len(side_rows), 6, 0))
        for plane in _box_planes(mins, maxs):
            # q3map convention: plane x ^ 1 is the opposite of plane x.
            side_rows.append((len(planes), 0))
            planes.append(plane)
            planes.append((-plane[0], -plane[1], -plane[2], -plane[3]))

    split_plane = len(planes)
    planes.append((1.0, 0.0, 0.0, 0.0))
    planes.append((-1.0, 0.0, 0.0, 0.0))

    outer_mins = tuple(value - WALL for value in ROOM_MINS)
    outer_maxs = tuple(value + WALL for value in ROOM_MAXS)
    front = [index for index, (_mins, maxs) in enumerate(brushes) if maxs[0] > 0]
    back = [index for index, (mins, _maxs) in enumerate(brushes) if mins[0] < 0]
    leaf_brushes = front + back
    leaves = (
        (0, 0, 0, *outer_mins[1:], *outer_maxs, 0, 0, 0, len(front)),
        (0, 0, *outer_mins, 0, *outer_maxs[1:], 0, 0, len(front), len(back)),
    )
    # children: front (positive side) is leaf 0 = -1, back is leaf 1 = -2
    nodes = ((split_plane, -1, -2, *outer_mins, *outer_maxs),)
    models = ((*map(float, outer_mins), *map(float, outer_maxs), 0, 0, 0, len(brushes)),)
    shader = b"textures/fnql_hosting_gate/wall".ljust(MAX_QPATH, b"\0")

    lumps: Dict[int, bytes] = {
        LUMP_ENTITIES: entity_text(),
        LUMP_SHADERS: struct.pack(f"<{MAX_QPATH}sii", shader, 0, CONTENTS_SOLID),
        LUMP_PLANES: _lump_bytes("4f", planes),
        LUMP_NODES: _lump_bytes("9i", nodes),
        LUMP_LEAFS: _lump_bytes("12i", leaves),
        LUMP_LEAFBRUSHES: _lump_bytes("i", [(index,) for index in leaf_brushes]),
        LUMP_MODELS: _lump_bytes("6f4i", models),
        LUMP_BRUSHES: _lump_bytes("3i", brush_rows),
        LUMP_BRUSHSIDES: _lump_bytes("2i", side_rows),
    }

    header_size = 8 + (HEADER_LUMPS + 1) * 8
    body = bytearray()
    directory = []
    for index in range(HEADER_LUMPS + 1):  # the last one is QL's advertisement lump
        data = lumps.get(index, b"")
        offset = header_size + len(body)
        directory.append((offset, len(data)))
        body += data
        body += b"\0" * (-len(body) % 4)
    header = BSP_IDENT + struct.pack("<i", BSP_VERSION_QL)
    header += b"".join(struct.pack("<ii", offset, length) for offset, length in directory)
    return header + bytes(body)


def factory_text() -> bytes:
    return (
        '[\n  {\n'
        f'    "id": "{FACTORY_ID}",\n'
        '    "basegt": "ffa",\n'
        '    "title": "Free For All (FnQL hosting gate)",\n'
        '    "cvars": {}\n'
        '  }\n]\n'
    ).encode("ascii")


def default_cfg_text() -> bytes:
    return (
        b"// Generated by FnQL scripts/synthetic_hosting_gate.py for the CI\n"
        b"// module-hosting gate. Not a retail file.\n"
    )


def write_assets(base: Path) -> List[Path]:
    """Write the gate's game directory under base and return the files written."""
    files = {
        "default.cfg": default_cfg_text(),
        FACTORY_FILE: factory_text(),
        f"maps/{MAP_NAME}.bsp": build_box_bsp(),
    }
    written = []
    for name, data in files.items():
        path = base / GAME_DIR / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        written.append(path)
    return written


def module_arch(module_name: str, platform: Optional[str] = None) -> str:
    """The target arch of a qagame by its file name; SystemExit if no target loads it."""
    platform = platform or ("windows" if os.name == "nt" else "linux")
    for arch, suffix in MODULE_ARCH_BY_PLATFORM[platform]:
        if module_name == f"fnql_qagame{suffix}":
            return arch
    expected = "|".join(suffix for _arch, suffix in MODULE_ARCH_BY_PLATFORM[platform])
    raise SystemExit(f"{module_name} is not an FnQL-game qagame for {platform} "
                     f"(expected fnql_qagame<{expected}>)")


def required_lines(module_name: str) -> List[str]:
    return list(REQUIRED) + ["qagame : native " + module_name + " (FnQL-game "]


def server_command(engine: Path, base: Path, home: Path, port: int, frames: int) -> List[str]:
    return [
        str(engine),
        "+set", "com_headless", "1",
        "+set", "fs_basepath", str(base),
        "+set", "fs_homepath", str(home),
        # fs_steampath defaults to the auto-detected retail install; clearing it
        # keeps a developer machine as hermetic as CI (generated assets only)
        "+set", "fs_steampath", "",
        "+set", "fs_game", GAME_DIR,
        "+set", "logfile", "2",
        "+set", "net_port", str(port),
        "+set", "sv_pure", "0",
        "+set", "bot_enable", "0",
        "+set", "vm_gameProvider", "fnql",
        # a dedicated server starts its first map through serverstartup, after
        # the command line, so the whole sequence runs from there; "wait"
        # holds the buffer for that many server frames
        "+set", "serverstartup", "; ".join([
            f"map {MAP_NAME} {FACTORY_ID}", "vminfo", f"wait {frames}",
            "serverinfo", "status", f"echo {DONE_MARKER}"]),
    ]


def check_log(log: str, module_name: str) -> List[str]:
    failures = [f"missing: {line!r}" for line in required_lines(module_name) if line not in log]
    for key, value in REQUIRED_SERVERINFO:
        pattern = rf"^{re.escape(key)}[ 	]+" + (re.escape(value) if value else r"\S+") + r"[ 	]*$"
        if not re.search(pattern, log, re.MULTILINE):
            failures.append(f"missing serverinfo: {key} {value or '<any>'}")
    failures += [f"forbidden: {line!r}" for line in FORBIDDEN if line in log]
    archives = [int(count) for count in PK3_SUMMARY.findall(log)]
    if not archives:
        failures.append("missing: the filesystem's pk3 summary")
    elif any(archives):
        failures.append(f"mounted {max(archives)} pk3 file(s); the gate must use generated assets only")
    return failures


def run_gate(engine: Path, module: Path, *, seconds: float, hold: float, port: int,
             frames: int, scratch_root: Path, keep: bool) -> int:
    module_name = module.name
    arch = module_arch(module_name)
    scratch_root.mkdir(parents=True, exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix="hosting-gate-", dir=str(scratch_root)))
    try:
        base = scratch / "base"
        home = scratch / "home"
        write_assets(base)
        (home / GAME_DIR).mkdir(parents=True)
        shutil.copyfile(module, home / GAME_DIR / module_name)
        log_path = home / GAME_DIR / "qconsole.log"

        popen_options = {}
        if sys.platform == "win32":
            startup = subprocess.STARTUPINFO()
            startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0  # SW_HIDE
            popen_options = {"startupinfo": startup, "creationflags": subprocess.CREATE_NO_WINDOW}
        env = dict(os.environ, FNQL_HEADLESS="1")
        process = subprocess.Popen(server_command(engine, base, home, port, frames),
                                   cwd=str(engine.parent), stdin=subprocess.DEVNULL,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   env=env, **popen_options)
        started = time.monotonic()
        exited_early = False
        done_at = None
        while True:
            elapsed = time.monotonic() - started
            if process.poll() is not None:
                exited_early = True
                break
            if done_at is None and log_path.is_file():
                if DONE_MARKER in log_path.read_text(encoding="utf-8", errors="replace"):
                    done_at = elapsed
            if done_at is not None and elapsed >= done_at + hold:
                break
            if done_at is None and elapsed >= seconds:
                break
            time.sleep(0.25)
        if process.poll() is None:
            process.kill()
        process.wait(timeout=10)

        log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
        failures = []
        if exited_early:
            failures.append(f"server exited after {time.monotonic() - started:.1f}s "
                            f"(code {process.returncode})")
        if done_at is None:
            failures.append(f"commands did not finish within {seconds:.0f}s")
        failures += check_log(log, module_name)
        if failures:
            print(f"FnQL hosting gate FAILED ({engine.name}, {module_name}):")
            for failure in failures:
                print(f"  - {failure}")
            print("--- log tail ---")
            print(log[-8000:])
            return 1
        print(f"FnQL hosting gate OK: {engine.name} hosted {module_name} ({arch}) on generated "
              f"{MAP_NAME} ({FACTORY_ID}), ran {frames} frames and {hold:.0f}s more")
        return 0
    finally:
        if keep:
            print(f"kept {scratch}")
        else:
            shutil.rmtree(scratch, ignore_errors=True)


def parse_args(argv: Optional[Sequence[str]]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)

    generate = commands.add_parser("generate", help="write the generated game directory")
    generate.add_argument("--output", type=Path, required=True,
                          help=f"base directory; files go under <output>/{GAME_DIR}/")

    run = commands.add_parser("run", help="run the hosting gate")
    run.add_argument("--engine", type=Path, required=True, help="FnQL dedicated server")
    run.add_argument("--module", type=Path, required=True,
                     help="FnQL-game qagame, e.g. game-stage/baseq3/fnql_qagamex86.dll")
    run.add_argument("--seconds", type=float, default=60.0,
                     help="time allowed for startup and commands (default: 60)")
    run.add_argument("--frames", type=int, default=200,
                     help="server frames to run after the map loads (default: 200)")
    run.add_argument("--hold", type=float, default=DEFAULT_HOLD_SECONDS,
                     help="seconds the server must keep running after the commands "
                          f"(default: {DEFAULT_HOLD_SECONDS:.0f})")
    run.add_argument("--port", type=int, default=27996)
    run.add_argument("--scratch", type=Path, default=ROOT / ".tmp",
                     help="parent of the scratch directory (default: <repo>/.tmp)")
    run.add_argument("--keep", action="store_true", help="keep the scratch directory")
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    if args.command == "generate":
        for path in write_assets(args.output):
            print(path)
        return 0
    if not args.engine.is_file():
        raise SystemExit(f"dedicated server not found: {args.engine}")
    if not args.module.is_file():
        raise SystemExit(f"module not found: {args.module}")
    if args.frames < 1:
        raise SystemExit("--frames must be positive")
    if args.hold < 0 or args.seconds <= 0:
        raise SystemExit("--seconds must be positive and --hold non-negative")
    return run_gate(args.engine.resolve(), args.module.resolve(), seconds=args.seconds,
                    hold=args.hold, port=args.port, frames=args.frames,
                    scratch_root=args.scratch.resolve(), keep=args.keep)


if __name__ == "__main__":
    sys.exit(main())
