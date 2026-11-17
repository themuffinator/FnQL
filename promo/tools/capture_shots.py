#!/usr/bin/env python3
"""Capture promo screenshots of retail Quake Live maps with a windowless FnQL client.

Each map is shot from its own info_player_intermission camera (read from the
retail pak), optionally with bots running around for action shots. The client
always runs windowless (com_headless 2) with Steam and the WebUI at their
defaults, per AGENTS.md.

Example:
    python promo/tools/capture_shots.py --client meson/build-x86-nosdl/fnql.exe \
        --renderer vk --maps cure,overkill,hiddenfortress --bots 7 --shots 10 --gap 110

Feature comparisons: --variants slows the game almost to a stop at each shot
and saves one image per variant a few milliseconds of game time apart, e.g.

    --variants "off:cl_playerHighlight=0|rim:cl_playerHighlight=1|both:cl_playerHighlight=3"

Shots land in <home>/baseq3/screenshots as fnql-<tag>-<map>-<letter>[-<variant>].png,
each with a .txt sidecar recording the camera. Pick the best and convert them
into promo/images/screenshots (see promo/images/README.md).
"""
from __future__ import annotations

import argparse
import math
import os
import re
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
BOT_NAMES = ("sarge", "anarki", "slash", "keel", "ranger", "visor", "xaero")
ENT_RE = re.compile(r"\{([^{}]*)\}", re.S)
KV_RE = re.compile(r'"([^"]*)"\s+"([^"]*)"')

# In noclip, setviewpos's teleport velocity carries the player ~77 units forward
# before friction stops it (measured 2026-11-17); start that far behind the camera.
NOCLIP_DRIFT = 77.0
VIEW_HEIGHT = 26.0
# The engine keeps only the first 32 "+" commands (MAX_CONSOLE_LINES).
MAX_PLUS_COMMANDS = 32

# Latched video cvars must be on the command line; the rest go in the cfg.
PROFILES = {
    "modern": {
        "r_fbo": "1", "r_hdr": "1", "r_bloom": "1", "r_bloom_soft_knee": "0.5",
        "r_ext_multisample": "8", "r_tonemap": "2", "r_tonemapExposure": "1.0",
        "r_ext_max_anisotropy": "16", "r_ext_texture_filter_anisotropic": "1",
        "r_picmip": "0", "r_lodbias": "-2", "r_subdivisions": "1",
        "r_dynamiclight": "1", "r_dlightMode": "2",
    },
    "retail": {},
}
HIGHLIGHT = {
    "cl_playerHighlight": "3",
    "cl_playerHighlightRimIntensity": "2.0",
    "cl_playerHighlightOutlineIntensity": "2.0",
}


def map_entities(pak: zipfile.ZipFile, name: str) -> list[dict[str, str]]:
    data = pak.read(f"maps/{name}.bsp")
    if data[:4] != b"IBSP":
        raise ValueError(f"{name}: not an IBSP file")
    offset, length = struct.unpack_from("<ii", data, 8)  # lump 0: entities
    if offset < 0 or length < 0 or offset + length > len(data):
        raise ValueError(f"{name}: entity lump out of bounds")
    text = data[offset:offset + length].decode("latin-1", "replace")
    return [dict(KV_RE.findall(block)) for block in ENT_RE.findall(text)]


def intermission_camera(ents) -> tuple[list[float], float, float] | None:
    """Origin, yaw and pitch (positive = down) of the map's intermission camera."""
    by_name = {e["targetname"]: e for e in ents if "targetname" in e}
    for e in ents:
        if e.get("classname") != "info_player_intermission" or "origin" not in e:
            continue
        origin = [float(v) for v in e["origin"].split()[:3]]
        pitch = yaw = 0.0
        if "angles" in e:
            pitch, yaw = (float(v) for v in e["angles"].split()[:2])
        elif "angle" in e:
            yaw = float(e["angle"])
        target = by_name.get(e.get("target", ""))
        if target and "origin" in target:
            t = [float(v) for v in target["origin"].split()[:3]]
            d = [t[i] - origin[i] for i in range(3)]
            yaw = math.degrees(math.atan2(d[1], d[0]))
            pitch = -math.degrees(math.atan2(d[2], math.hypot(d[0], d[1])))
        return origin, yaw, pitch
    return None


def parse_pairs(items: list[str], what: str) -> dict[str, str]:
    out = {}
    for item in items:
        key, sep, value = item.partition("=")
        if not sep or not key or any(c in key + value for c in '";\n'):
            raise SystemExit(f"bad {what} '{item}': expected name=value without quotes or ';'")
        out[key] = value
    return out


def parse_variants(spec: str | None) -> list[tuple[str, dict[str, str]]]:
    variants = []
    for chunk in filter(None, (spec or "").split("|")):
        name, sep, body = chunk.partition(":")
        if not sep or not re.fullmatch(r"[A-Za-z0-9_]+", name):
            raise SystemExit(f"bad variant '{chunk}': expected name:cvar=value,cvar=value")
        variants.append((name, parse_pairs([kv for kv in body.split(",") if kv], "variant cvar")))
    return variants


def apply_camera_offsets(cam, args):
    (x, y, z), yaw, pitch = cam
    yaw += args.yaw_offset
    pitch += args.pitch_offset
    r, p = math.radians(yaw), math.radians(pitch)
    # Dolly along the view direction (pitch positive looks down).
    x += args.dolly * math.cos(p) * math.cos(r)
    y += args.dolly * math.cos(p) * math.sin(r)
    z += -args.dolly * math.sin(p) + args.raise_
    return [x, y, z], yaw, pitch


def shot_lines(base: str, args, variants) -> list[str]:
    if not variants:
        return [f"screenshotPNG {base}", f"wait {args.gap}"]
    # Slow the game as far as it goes (Quake Live clamps timescale to >= 0.1;
    # cl_paused is read-only) and render as fast as possible, then shoot each
    # variant only a few frames apart. Successive variants are a few
    # milliseconds of game time apart: in practice the same frame.
    lines = ["set timescale 0.1", "set com_maxfps 1000", "wait 3"]
    for name, cvars in variants:
        lines += [f'set {k} "{v}"' for k, v in cvars.items()]
        lines += ["wait 2", f"screenshotPNG {base}-{name}", "wait 1"]
    lines += ["set com_maxfps 125", "set timescale 1", f"wait {args.gap}"]
    return lines


def map_script(name: str, cam, args, ingame: dict[str, str], variants) -> list[str]:
    (x, y, z), yaw, pitch = apply_camera_offsets(cam, args)
    r = math.radians(yaw)
    x -= NOCLIP_DRIFT * math.cos(r)
    y -= NOCLIP_DRIFT * math.sin(r)
    look = "+lookdown" if pitch > 0 else "+lookup"
    lines = [f"devmap {name} {args.factory}", "wait 400"]
    lines += [f'set {k} "{v}"' for k, v in ingame.items()]
    if args.team:
        lines += [f"team {args.team}", "wait 100"]
    lines += ["noclip", "god", "set bot_nochat 1"]
    if args.bots:
        lines += [f"addbot {b} 3" for b in BOT_NAMES[:args.bots]] + ["wait 200"]
    lines += [c for c in args.cmd]
    lines += [
        f"setviewpos {x:.1f} {y:.1f} {z - VIEW_HEIGHT:.1f} {yaw:.1f}", "wait 300",
        # At com_maxfps 125 and cl_pitchspeed 125, each frame of +look turns 1 degree.
        "centerview", "wait 10", "set cl_pitchspeed 125",
        look, f"wait {max(1, round(abs(pitch)))}", "-" + look[1:], "wait 200",
    ]
    for i in range(args.shots):
        lines += shot_lines(f"fnql-{args.tag}-{name}-{chr(97 + i)}", args, variants)
    return lines


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--client", type=Path, required=True, help="fnql.exe (x86 build for retail)")
    p.add_argument("--retail", type=Path, default=os.environ.get("FNQL_RETAIL_QL_PATH"),
                   help="Quake Live install folder (default: $FNQL_RETAIL_QL_PATH)")
    p.add_argument("--home", type=Path, default=REPO / ".tmp" / "promo-capture" / "home")
    p.add_argument("--renderer", default="vk", choices=("glx", "vk", "rtx", "opengl"))
    p.add_argument("--profile", default="modern", choices=sorted(PROFILES))
    p.add_argument("--maps", required=True, help="comma-separated retail map names")
    p.add_argument("--shots", type=int, default=3, help="screenshots per map")
    p.add_argument("--gap", type=int, default=250, help="frames between shots (125 fps)")
    p.add_argument("--bots", type=int, default=0, choices=range(0, len(BOT_NAMES) + 1))
    p.add_argument("--highlight", action="store_true", help="enable player highlighting")
    p.add_argument("--fov", default="90")
    p.add_argument("--size", default="1920x1080")
    p.add_argument("--tag", help="filename tag (default: renderer)")
    p.add_argument("--set", action="append", default=[], metavar="CVAR=VALUE",
                   help="cvar set in game after the map loads (repeatable)")
    p.add_argument("--startup", action="append", default=[], metavar="CVAR=VALUE",
                   help="latched cvar set on the command line (repeatable)")
    p.add_argument("--cmd", action="append", default=[],
                   help="console command run after the map loads, e.g. 'r_dlightTest 2 600 160 40 0'")
    p.add_argument("--factory", default="ffa", help="map factory for devmap (ffa, tdm, ca, ...)")
    p.add_argument("--team", choices=("r", "b"), help="join a team (team factories)")
    p.add_argument("--variants", help="name:cvar=v,cvar=v|name2:... shot per variant on a frozen frame")
    p.add_argument("--dolly", type=float, default=0.0, help="move the camera forward (units)")
    p.add_argument("--raise", dest="raise_", type=float, default=0.0, help="move the camera up (units)")
    p.add_argument("--yaw-offset", type=float, default=0.0)
    p.add_argument("--pitch-offset", type=float, default=0.0, help="positive looks further down")
    p.add_argument("--timeout", type=float, default=1800)
    args = p.parse_args()
    args.tag = args.tag or args.renderer

    if not args.retail or not (args.retail / "baseq3" / "pak00.pk3").is_file():
        p.error("--retail (or FNQL_RETAIL_QL_PATH) must point at a Quake Live install")
    if not args.client.is_file():
        p.error(f"client not found: {args.client}")
    width, height = (int(v) for v in args.size.lower().split("x"))

    with zipfile.ZipFile(args.retail / "baseq3" / "pak00.pk3") as pak:
        cams = {}
        for name in filter(None, args.maps.split(",")):
            cam = intermission_camera(map_entities(pak, name))
            if cam:
                cams[name] = cam
            else:
                print(f"skipping {name}: no info_player_intermission")
    if not cams:
        return 1

    ingame = {"cg_draw2D": "0", "cg_drawGun": "0", "cg_fov": args.fov,
              **(HIGHLIGHT if args.highlight else {}), **parse_pairs(args.set, "--set")}
    variants = parse_variants(args.variants)
    for c in args.cmd:
        if any(ch in c for ch in '";'):
            p.error(f"--cmd must not contain quotes or ';': {c}")
    if not re.fullmatch(r"[a-z]+", args.factory):
        p.error("--factory must be a factory name such as ffa or tdm")
    lines = ["wait 60"]
    for name, cam in cams.items():
        lines += map_script(name, cam, args, ingame, variants)
    lines += ["disconnect", "wait 30", "quit", ""]

    # The home is a scratch profile owned by this tool (marked on creation).
    # Reset its archived configs so cvars from an earlier run can't leak in.
    marker = args.home / ".promo-capture"
    if args.home.exists() and not marker.exists() and any(args.home.iterdir()):
        p.error(f"{args.home} is not a capture profile made by this tool; pick an empty --home")
    args.home.mkdir(parents=True, exist_ok=True)
    marker.touch()
    for stale in ("qzconfig.cfg", "repconfig.cfg"):
        for where in (args.home, args.home / "baseq3"):
            (where / stale).unlink(missing_ok=True)

    cfg = args.home / "baseq3" / f"promo-{args.tag}.cfg"
    cfg.parent.mkdir(parents=True, exist_ok=True)
    cfg.write_text("\n".join(lines), encoding="ascii")

    startup = {
        "com_headless": "2", "fs_homepath": str(args.home), "fs_basepath": str(args.retail),
        "fs_steampath": str(args.retail), "cl_renderer": args.renderer,
        "r_fullscreen": "0", "r_mode": "-1", "r_customWidth": str(width),
        "r_customHeight": str(height), "r_swapInterval": "0", "com_maxfps": "125",
        "r_screenshotWriteViewpos": "1", "sv_maxclients": "8", "bot_minplayers": "0",
        **PROFILES[args.profile],
    }
    if args.renderer == "rtx":
        startup.update({"rtx_rt_mode": "2", "rtx_rt_require": "0"})
    startup.update(parse_pairs(args.startup, "--startup"))
    cmd = [str(args.client.resolve())]
    for k, v in startup.items():
        cmd += ["+set", k, v]
    cmd += ["+exec", cfg.name]
    if cmd.count("+set") + 1 > MAX_PLUS_COMMANDS:
        p.error("too many startup cvars: the engine drops '+' commands after the 32nd")

    env = dict(os.environ, FNQL_HEADLESS="2")
    log = args.home.parent / f"run-{args.tag}.log"
    print(f"capturing {len(cams)} map(s); log: {log}")
    with log.open("w", encoding="utf-8", errors="replace") as fh:
        try:
            rc = subprocess.run(cmd, cwd=args.client.resolve().parent, env=env, stdout=fh,
                                stderr=subprocess.STDOUT, timeout=args.timeout).returncode
        except subprocess.TimeoutExpired:
            print("client timed out; check the log")
            return 1
    shots = sorted((args.home / "baseq3" / "screenshots").glob(f"fnql-{args.tag}-*.png"))
    print(f"client exit {rc}; {len(shots)} screenshot(s) in {args.home / 'baseq3' / 'screenshots'}")
    return 0 if shots else 1


if __name__ == "__main__":
    sys.exit(main())
