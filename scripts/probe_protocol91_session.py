#!/usr/bin/env python3
"""Run a local retail-asset protocol-91 session and replay its server demo.

The probe never launches fullscreen. It requires the Win32 FnQL binaries when
using the final retail Quake Live native modules.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import subprocess
import tempfile
import time


def process_kwargs() -> dict[str, object]:
    if os.name == "nt":
        return {"creationflags": subprocess.CREATE_NO_WINDOW}
    return {}


def launch(executable: pathlib.Path, arguments: list[str]) -> subprocess.Popen[bytes]:
    return subprocess.Popen(
        [str(executable), *arguments],
        cwd=str(executable.parent),
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        **process_kwargs(),
    )


def stop(process: subprocess.Popen[bytes] | None) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def read_log(home: pathlib.Path) -> str:
    path = home / "baseq3" / "qconsole.log"
    return path.read_text(encoding="utf-8", errors="replace") if path.is_file() else ""


def require(log: str, marker: str, lane: str) -> None:
    if marker not in log:
        raise RuntimeError(f"{lane} log did not contain {marker!r}")


def reject(log: str, marker: str, lane: str) -> None:
    if marker in log:
        raise RuntimeError(f"{lane} log contained failure marker {marker!r}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", type=pathlib.Path, required=True)
    parser.add_argument("--server", type=pathlib.Path, required=True)
    parser.add_argument("--basepath", type=pathlib.Path, required=True)
    parser.add_argument("--work-root", type=pathlib.Path, default=pathlib.Path(".tmp"))
    parser.add_argument("--port", type=int, default=27988)
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument("--wait-frames", type=int, default=900,
                        help="engine frames to remain connected before cleanly quitting")
    parser.add_argument("--steam-provider", type=pathlib.Path,
                        help="optional FnQL Steam provider used by both peers")
    parser.add_argument("--steam-api", type=pathlib.Path,
                        help="optional Steam API or deterministic fake used by both peers")
    parser.add_argument("--expect-steam-fixture", action="store_true",
                        help="assert deterministic fake-provider P2P, voice and stats sidecars")
    args = parser.parse_args()

    client = args.client.resolve(strict=True)
    server = args.server.resolve(strict=True)
    basepath = args.basepath.resolve(strict=True)
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    if not 1 <= args.wait_frames <= 10_000:
        parser.error("--wait-frames must be between 1 and 10000")
    if (args.steam_provider is None) != (args.steam_api is None):
        parser.error("--steam-provider and --steam-api must be supplied together")
    if args.expect_steam_fixture and args.steam_provider is None:
        parser.error("--expect-steam-fixture requires --steam-provider and --steam-api")
    steam_provider = args.steam_provider.resolve(strict=True) if args.steam_provider else None
    steam_api = args.steam_api.resolve(strict=True) if args.steam_api else None

    def steam_arguments(server_lane: bool) -> list[str]:
        if steam_provider is None or steam_api is None:
            return []
        values = [
            "+set", "com_steamIntegration", "1",
            "+set", "com_steamProvider", str(steam_provider),
            "+set", "com_steamApi", str(steam_api),
        ]
        if server_lane:
            values.extend(["+set", "sv_steamSecure", "1"])
        return values

    args.work_root.mkdir(parents=True, exist_ok=True)
    session_root = pathlib.Path(tempfile.mkdtemp(prefix="protocol91-session-",
                                                  dir=args.work_root.resolve()))
    server_home = session_root / "server"
    client_home = session_root / "client"
    playback_home = session_root / "playback"
    for home in (server_home, client_home, playback_home):
        home.mkdir()

    server_process: subprocess.Popen[bytes] | None = None
    client_process: subprocess.Popen[bytes] | None = None
    playback_process: subprocess.Popen[bytes] | None = None
    try:
        server_process = launch(server, [
            "+set", "fs_basepath", str(basepath),
            "+set", "fs_homepath", str(server_home),
            "+set", "net_port", str(args.port),
            "+set", "sv_pure", "1",
            "+set", "sv_autoRecordDemos", "1",
            "+set", "logfile", "2",
            "+set", "developer", "1",
            *steam_arguments(True),
            "+map", "campgrounds",
        ])
        time.sleep(3)
        if server_process.poll() is not None:
            raise RuntimeError("dedicated server exited during startup")

        client_process = launch(client, [
            "+set", "fs_basepath", str(basepath),
            "+set", "fs_homepath", str(client_home),
            "+set", "r_fullscreen", "0",
            "+set", "r_mode", "3",
            "+set", "s_initsound", "0",
            "+set", "logfile", "2",
            "+set", "developer", "1",
            *steam_arguments(False),
            "+connect", f"127.0.0.1:{args.port}",
            "+wait", str(args.wait_frames),
            *(["+steam_voice_start", "+wait", "30", "+steam_voice_stop"]
              if args.expect_steam_fixture else []),
            "+quit",
        ])
        client_process.wait(timeout=args.timeout)
        time.sleep(2)

        server_log = read_log(server_home)
        client_log = read_log(client_home)
        require(client_log, " 91 FnQL", "client")
        require(client_log, "Connected to a pure server", "client")
        require(client_log, "reloaded pinned native module", "client")
        require(client_log, "CL_InitCGame:", "client")
        require(server_log, "Going from CS_PRIMED to CS_ACTIVE", "server")
        require(server_log, "Stopped server demo recording:", "server")
        reject(client_log, "ERROR: VM_Create", "client")
        if steam_provider is not None:
            require(client_log, "Steam provider FnQL-Steam ", "client")
            require(client_log, " active (capabilities ", "client")
            require(server_log, "Steam provider FnQL-Steam ", "server")
            require(server_log, " active (capabilities ", "server")
            require(server_log, "Platform authentication validated client", "server")
        if args.expect_steam_fixture:
            require(server_log, "Steam GameServer identity published in configstring 714", "server")
            require(client_log, "Accepted deferred Steam P2P request from active FnQL server", "client")
            require(client_log, "Steam stats P2P report decoded: 11 bytes", "client")
            require(client_log, "Steam voice P2P packet sent: 4 bytes", "client")

        demos = sorted((server_home / "baseq3" / "demos" / "server").glob("*.dm_91"))
        if len(demos) != 1 or demos[0].stat().st_size <= 8:
            raise RuntimeError("session did not produce exactly one non-empty .dm_91")
        trailer = demos[0].read_bytes()[-8:]
        if trailer != b"\xff" * 8:
            raise RuntimeError(".dm_91 is missing the sequence/length terminator")

        playback_demo = playback_home / "baseq3" / "demos" / "server" / "live.dm_91"
        playback_demo.parent.mkdir(parents=True)
        shutil.copyfile(demos[0], playback_demo)
        playback_process = launch(client, [
            "+set", "fs_basepath", str(basepath),
            "+set", "fs_homepath", str(playback_home),
            "+set", "r_fullscreen", "0",
            "+set", "r_mode", "3",
            "+set", "s_initsound", "0",
            "+set", "logfile", "2",
            "+set", "developer", "1",
            "+demo", "server/live.dm_91",
            "+wait", str(args.wait_frames),
            "+quit",
        ])
        playback_process.wait(timeout=args.timeout)
        playback_log = read_log(playback_home)
        require(playback_log, "CL_InitCGame:", "playback")
        require(playback_log, "Client Shutdown (Client quit)", "playback")
        reject(playback_log, "ERROR: VM_Create", "playback")
        reject(playback_log, "Protocol ", "playback")

        steam_label = ", authenticated Steam ticket" if steam_provider is not None else ""
        print(f"Protocol-91 session probe passed: pure CS_ACTIVE{steam_label}, snapshots, dm_91 finalize and replay")
        print(f"Artifacts: {session_root}")
        return 0
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(f"process did not finish within {args.timeout:.1f}s") from error
    finally:
        stop(playback_process)
        stop(client_process)
        stop(server_process)


if __name__ == "__main__":
    raise SystemExit(main())
