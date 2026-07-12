#!/usr/bin/env python3
"""Read-only UDP probe for a running FnQL protocol-91 server."""

from __future__ import annotations

import argparse
import socket
import struct


OOB = b"\xff" * 4
PROBE_STEAM_ID = 76_561_198_000_000_001


def request(sock: socket.socket, address: tuple[str, int], payload: bytes) -> bytes | None:
    sock.sendto(OOB + payload, address)
    try:
        packet, _ = sock.recvfrom(65_535)
    except TimeoutError:
        return None
    return packet


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", nargs="?", default="127.0.0.1")
    parser.add_argument("port", nargs="?", type=int, default=27_960)
    parser.add_argument("--timeout", type=float, default=1.0)
    args = parser.parse_args()
    if not 1 <= args.port <= 65_535 or not 0 < args.timeout <= 30:
        parser.error("port or timeout is outside the safe probe range")

    address = (args.host, args.port)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(args.timeout)
        info = request(sock, address, b"getinfo fnql-protocol91-probe")
        challenge = request(sock, address, b"getchallenge 12345 FnQL")
        ticket = bytes(range(1, 17))
        retail = request(
            sock,
            address,
            b"getchallenge " + struct.pack("<Q", PROBE_STEAM_ID) + ticket,
        )
        malformed = request(
            sock,
            address,
            b"getchallenge " + struct.pack("<Q", PROBE_STEAM_ID) + b"\x01\x02\x03",
        )

    checks = {
        "protocol-91 info": bool(info and b"\\protocol\\91" in info),
        "FnQL nonce echo": bool(challenge and b" 12345 91 FnQL" in challenge),
        "retail binary challenge": bool(
            retail
            and retail.startswith(OOB + b"challengeResponse ")
            and retail.count(b" ") == 1
        ),
        "malformed ticket silence": malformed is None,
    }
    for name, passed in checks.items():
        print(f"{'PASS' if passed else 'FAIL'}: {name}")
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
