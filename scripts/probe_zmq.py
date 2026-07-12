#!/usr/bin/env python3
"""Live FnQL ZMQ RCON/authentication/PUB interoperability probe.

Requires pyzmq and an administrator-supplied or locally built libzmq DLL.
All generated server state is directed to the supplied disposable home path.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import socket
import subprocess
import sys
import time

try:
    import zmq
except ImportError as exc:  # pragma: no cover - host dependency
    raise SystemExit("probe_zmq.py requires pyzmq (python -m pip install pyzmq)") from exc


def wait_for_port(process: subprocess.Popen[bytes], port: int, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"FnQL exited before port {port} opened ({process.returncode})")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                return
        except OSError:
            time.sleep(0.1)
    raise TimeoutError(f"FnQL did not open port {port} within {timeout:g}s")


def req_socket(context: zmq.Context, endpoint: str, password: str, timeout: int) -> zmq.Socket:
    result = context.socket(zmq.REQ)
    result.linger = 0
    result.rcvtimeo = timeout
    result.sndtimeo = timeout
    result.plain_username = b"rcon"
    result.plain_password = password.encode("utf-8")
    result.connect(endpoint)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=pathlib.Path)
    parser.add_argument("--library", required=True, type=pathlib.Path)
    parser.add_argument("--basepath", required=True, type=pathlib.Path)
    parser.add_argument("--homepath", required=True, type=pathlib.Path)
    parser.add_argument("--map", default="campgrounds")
    parser.add_argument("--net-port", type=int, default=27979)
    parser.add_argument("--rcon-port", type=int, default=28979)
    parser.add_argument("--stats-port", type=int, default=28980)
    args = parser.parse_args()

    exe = args.exe.resolve(strict=True)
    library = args.library.resolve(strict=True)
    basepath = args.basepath.resolve(strict=True)
    homepath = args.homepath.resolve()
    homepath.mkdir(parents=True, exist_ok=True)
    rcon_password = "fnql-zmq-probe-rcon"
    stats_password = "fnql-zmq-probe-stats"

    command = [
        str(exe), "+set", "fs_basepath", str(basepath),
        "+set", "fs_homepath", str(homepath), "+set", "logfile", "2",
        "+set", "developer", "1", "+set", "net_port", str(args.net_port),
        "+set", "zmq_library", str(library),
        "+set", "zmq_rcon_enable", "1", "+set", "zmq_rcon_ip", "127.0.0.1",
        "+set", "zmq_rcon_port", str(args.rcon_port),
        "+set", "zmq_rcon_password", rcon_password,
        "+set", "zmq_stats_enable", "1", "+set", "zmq_stats_ip", "127.0.0.1",
        "+set", "zmq_stats_port", str(args.stats_port),
        "+set", "zmq_stats_password", stats_password,
        "+map", args.map,
    ]
    creationflags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
    process = subprocess.Popen(
        command, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL, creationflags=creationflags,
    )
    context = zmq.Context()
    sockets: list[zmq.Socket] = []
    try:
        wait_for_port(process, args.rcon_port, 20.0)
        rcon_endpoint = f"tcp://127.0.0.1:{args.rcon_port}"
        stats_endpoint = f"tcp://127.0.0.1:{args.stats_port}"

        rejected = req_socket(context, rcon_endpoint, "incorrect-password", 750)
        sockets.append(rejected)
        rejected.send_string("status")
        try:
            rejected.recv()
            raise AssertionError("incorrect PLAIN password was accepted")
        except zmq.Again:
            pass
        rejected.close()
        sockets.remove(rejected)

        rcon = req_socket(context, rcon_endpoint, rcon_password, 5000)
        sockets.append(rcon)
        rcon.send_string("status")
        assert rcon.recv_string() == "FnQL ZMQ RCON ready\n"
        rcon.send_string("status")
        status = rcon.recv_string()
        assert f"map: {args.map}" in status and "steamid" in status
        rcon.send_string("echo FNQL_ZMQ_RCON_ATOMIC")
        assert rcon.recv_string() == "FNQL_ZMQ_RCON_ATOMIC\n"

        stats = context.socket(zmq.SUB)
        sockets.append(stats)
        stats.linger = 0
        stats.rcvtimeo = 5000
        stats.plain_username = b"stats"
        stats.plain_password = stats_password.encode("utf-8")
        stats.setsockopt(zmq.SUBSCRIBE, b"")
        stats.connect(stats_endpoint)
        time.sleep(0.5)  # permit subscription propagation through PUB/SUB
        rcon.send_string("zmq_selftest")
        assert "queued" in rcon.recv_string()
        publication = json.loads(stats.recv_string())
        assert publication == {
            "TYPE": "FNQL_ZMQ_SELFTEST",
            "DATA": {"ok": True, "protocol": 91},
        }
        print("ZMQ probe passed: bad-auth rejection, authenticated atomic RCON, validated PUB JSON")
        return 0
    finally:
        for item in sockets:
            item.close()
        context.term()
        if process.poll() is None and process.stdin:
            try:
                process.stdin.write(b"quit\n")
                process.stdin.flush()
                process.wait(timeout=5)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                process.kill()
                process.wait()


if __name__ == "__main__":
    raise SystemExit(main())
