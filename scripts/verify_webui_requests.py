#!/usr/bin/env python3
"""Opt-in x86 offscreen WebUI request check against an external retail install.

Run from an x86 Visual Studio developer prompt with --retail-path. No game,
window, input events, or screen capture are involved. The probe compiles the
current public adapter and extracts the current startup queue/wakeup JavaScript.
It also checks delayed avatar resources, readiness notifications, duplicate
requests, retry budgets, and navigation cleanup using a generated one-pixel PNG
and local fake host services; no Steam account or live avatar request is used.
Profiles, generated source, and binaries remain under .tmp/ by default.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import uuid
from pathlib import Path

from windows_pe import PE_I386, parse_pe


ROOT = Path(__file__).resolve().parents[1]


def request_bridge(source: str) -> str:
    """Use the production wake/queue definitions, without the retail menu API."""
    first = source.index('"var nativeQueue=')
    last = source.index('"var queueSocial=', first)
    lines = source[first:last].splitlines()
    literals = [line.strip() for line in lines if line.strip()]
    bridge = "".join(json.loads(literal) for literal in literals)
    # A single deliberately stalled notification exercises the production
    # watchdog. All normal notifications go through the real retail XHR API.
    return (
        "(function(){"
        "window.__probeStallNext=false;window.__probeAborts=0;"
        "var realXHR=window.XMLHttpRequest;window.XMLHttpRequest=function(){"
        "if(window.__probeStallNext){window.__probeStallNext=false;return {"
        "open:function(){},send:function(){},abort:function(){"
        "window.__probeAborts++;if(this.onabort){this.onabort();}}};}"
        "return new realXHR();};"
        + bridge
        + "window.__probeQueue=queue;"
        "window.__probeWakeCount=function(){return nativeWakeSequence;};"
        "})();"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--retail-path", type=Path,
                        help="Legitimate Quake Live directory containing the external runtime")
    parser.add_argument("--work-dir", type=Path, default=ROOT / ".tmp" / "webui-request-check")
    parser.add_argument("--compiler", default="cl", help="MSVC compiler in an x86 developer environment")
    parser.add_argument("--timeout", type=int, default=90, help="Maximum probe runtime in seconds")
    parser.add_argument("--benchmark", action="store_true",
                        help="Also compare legacy per-character IPC with bulk requests (not gameplay frame timings)")
    args = parser.parse_args()
    if args.retail_path is None:
        print("SKIP: provide --retail-path to run the external retail WebUI request probe.")
        return 0
    if sys.platform != "win32":
        parser.error("The retail adapter probe requires Windows and an x86 MSVC environment.")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    retail = args.retail_path.resolve()
    for name in ("awesomium.dll", "awesomium_process.exe", "web.pak"):
        if not (retail / name).is_file():
            parser.error(f"Missing external retail fixture: {retail / name}")
    compiler = shutil.which(args.compiler)
    if compiler is None:
        parser.error("MSVC cl was not found. Run from an x86 Visual Studio developer prompt.")

    work = args.work_dir.resolve() / uuid.uuid4().hex
    profile = work / "profile"
    profile.mkdir(parents=True)
    bridge = work / "request-bridge.js"
    bridge.write_text(request_bridge((ROOT / "code/client/cl_webui.cpp").read_text(encoding="utf-8")),
                      encoding="utf-8")
    binary = work / "webui_retail_request_probe.exe"
    command = [compiler, "/nologo", "/O2", "/MT", "/EHsc", "/std:c++17", "/W4",
               f"/I{ROOT / 'code/client'}", str(ROOT / "tests/webui_retail_request_probe.cpp"),
               str(ROOT / "code/client/awesomium_backend_win32.cpp"), f"/Fe{binary}",
               "/link", "/MACHINE:X86", "user32.lib"]
    print(f"Probe artifacts: {work}", flush=True)
    subprocess.run(command, cwd=work, check=True)
    metadata = parse_pe(binary.read_bytes(), name=binary.name)
    if metadata is None or metadata.machine != PE_I386:
        raise ValueError("The WebUI probe must be an x86 executable")
    probe_command = [str(binary), str(retail), str(profile), str(bridge)]
    if args.benchmark:
        probe_command.append("--benchmark")
    subprocess.run(probe_command,
                   cwd=work, check=True, timeout=args.timeout)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"verify_webui_requests.py: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
