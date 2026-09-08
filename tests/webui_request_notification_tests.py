"""Exercise the shipped bridge JavaScript with deterministic browser events."""

from __future__ import annotations

import ast
import json
from pathlib import Path
import re
import shutil
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]
NODE = shutil.which("node")


def bridge_queue_script() -> str:
    source = (ROOT / "code/client/cl_webui.cpp").read_text(encoding="utf-8")
    wake_start = source.index('"var nativeQueue=')
    wake_end = source.index('"var fileExistsCache=', wake_start)
    queue_start = source.index('"var queue=function(', wake_end)
    queue_end = source.index('"var queueSocial=', queue_start)
    literals = re.findall(
        r'"(?:\\.|[^"\\])*"',
        source[wake_start:wake_end] + source[queue_start:queue_end],
    )
    return "".join(ast.literal_eval(literal) for literal in literals)


HARNESS = r"""
const assert = require('node:assert/strict');
const vm = require('node:vm');
const input = JSON.parse(require('node:fs').readFileSync(0, 'utf8'));
function browser(initialQueue = []) {
    let now = 0, nextTimer = 1;
    const timers = new Map(), requests = [];
    const controls = { failSend: false };
    class Request {
        constructor() { requests.push(this); this.aborted = false; }
        open(method, url, asynchronous) {
            this.method = method;
            this.url = url;
            this.asynchronous = asynchronous;
        }
        send(body) {
            this.body = body;
            if (controls.failSend) { throw new Error('transport unavailable'); }
        }
        abort() { this.aborted = true; if (this.onabort) { this.onabort(); } }
        complete(event = 'load') {
            const callback = this['on' + event];
            assert.equal(typeof callback, 'function');
            callback();
        }
    }
    const context = vm.createContext({
        window: { __qlr_native_requests: initialQueue.slice() },
        XMLHttpRequest: Request,
        setTimeout(fn, delay) {
            const id = nextTimer++;
            timers.set(id, { fn, when: now + delay });
            return id;
        },
        clearTimeout(id) { timers.delete(id); },
    });
    vm.runInContext(input.bridge, context);
    return {
        context, requests, timers, controls,
        queue: context.window.__qlr_native_requests,
        enqueue(kind, payload) { return context.queue(kind, payload); },
        drain() { return Array.from(context.nativeQueue.splice(0)); },
        advance(milliseconds) {
            const end = now + milliseconds;
            for (let calls = 0; ; calls++) {
                assert.ok(calls < 1000, 'unbounded retry timer loop');
                const pending = Array.from(timers.entries())
                    .filter(([, timer]) => timer.when <= end)
                    .sort((a, b) => a[1].when - b[1].when || a[0] - b[0]);
                if (!pending.length) { break; }
                const [id, timer] = pending[0];
                now = timer.when;
                timers.delete(id);
                timer.fn();
            }
            now = end;
        },
    };
}
eval(input.scenario);
"""


@unittest.skipUnless(NODE, "Node.js is required to execute the bridge JavaScript")
class WebUiRequestNotificationTests(unittest.TestCase):
    def run_scenario(self, scenario: str) -> None:
        result = subprocess.run(
            [NODE, "-e", HARNESS],
            input=json.dumps({"bridge": bridge_queue_script(), "scenario": scenario}),
            text=True,
            capture_output=True,
            check=False,
            timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_startup_handshake_is_async_and_idle_has_no_repeating_work(self) -> None:
        self.run_scenario(r"""
            const b = browser();
            assert.equal(b.requests.length, 1);
            const request = b.requests[0];
            assert.equal(request.method, 'GET');
            assert.equal(request.asynchronous, true);
            assert.match(request.url, /^asset:\/\/fnqlbridge\/pending\/\d+-\d+$/);
            assert.equal(request.body, null);
            request.complete();
            assert.equal(b.timers.size, 0);
            b.advance(60000);
            assert.equal(b.requests.length, 1);
            assert.equal(b.timers.size, 0);
        """)

    def test_burst_coalesces_while_retaining_commands_in_order(self) -> None:
        self.run_scenario(r"""
            const b = browser(['gamecommand\npreload']);
            assert.equal(b.enqueue('gamecommand', 'connect 127.0.0.1'), true);
            assert.equal(b.enqueue('gamecommand', 'quit'), true);
            assert.equal(b.requests.length, 1);
            assert.deepEqual(b.drain(), [
                'gamecommand\npreload', 'gamecommand\nconnect 127.0.0.1',
                'gamecommand\nquit',
            ]);
            assert.ok(!b.requests[0].url.includes('quit'));
            b.requests[0].complete();
            assert.equal(b.timers.size, 0);
        """)

    def test_failed_notification_retries_without_consuming_commands(self) -> None:
        self.run_scenario(r"""
            const b = browser();
            b.requests[0].complete();
            b.enqueue('gamecommand', 'connect localhost');
            b.requests[1].complete('error');
            assert.deepEqual(Array.from(b.queue), ['gamecommand\nconnect localhost']);
            b.advance(99);
            assert.equal(b.requests.length, 2);
            b.advance(1);
            assert.equal(b.requests.length, 3);
            assert.notEqual(b.requests[2].url, b.requests[1].url);
            assert.deepEqual(b.drain(), ['gamecommand\nconnect localhost']);
            b.requests[2].complete();
            assert.equal(b.timers.size, 0);
        """)

    def test_arrival_after_completion_cancels_the_old_retry(self) -> None:
        self.run_scenario(r"""
            const b = browser();
            b.enqueue('gamecommand', 'first');
            b.requests[0].complete();
            b.enqueue('gamecommand', 'second');
            assert.equal(b.requests.length, 2);
            assert.equal(b.timers.size, 1, 'only the in-flight watchdog remains');
            b.advance(100);
            assert.equal(b.requests.length, 2);
            assert.deepEqual(b.drain(), ['gamecommand\nfirst', 'gamecommand\nsecond']);
            b.requests[1].complete();
            b.enqueue('gamecommand', 'third');
            assert.equal(b.requests.length, 3);
            assert.deepEqual(b.drain(), ['gamecommand\nthird']);
            b.requests[2].complete();
            assert.equal(b.timers.size, 0);
        """)

    def test_stalled_notification_aborts_and_late_events_do_not_duplicate_retry(self) -> None:
        self.run_scenario(r"""
            const b = browser();
            b.enqueue('gamecommand', 'queued');
            b.advance(1000);
            assert.equal(b.requests[0].aborted, true);
            assert.equal(b.timers.size, 1);
            b.requests[0].complete('error');
            b.requests[0].complete();
            assert.equal(b.timers.size, 1);
            b.advance(100);
            assert.equal(b.requests.length, 2);
            assert.deepEqual(b.drain(), ['gamecommand\nqueued']);
            b.requests[1].complete();
            assert.equal(b.timers.size, 0);
        """)

    def test_synchronous_send_failure_clears_watchdog_and_preserves_queue(self) -> None:
        self.run_scenario(r"""
            const b = browser();
            b.requests[0].complete();
            b.controls.failSend = true;
            assert.equal(b.enqueue('gamecommand', 'retry'), true);
            assert.equal(b.timers.size, 1, 'the failed send leaves one retry');
            b.controls.failSend = false;
            b.advance(100);
            assert.equal(b.requests.length, 3);
            assert.deepEqual(b.drain(), ['gamecommand\nretry']);
            b.requests[2].complete();
            assert.equal(b.timers.size, 0);
        """)

    def test_draining_before_a_scheduled_retry_returns_to_idle(self) -> None:
        self.run_scenario(r"""
            const b = browser();
            b.enqueue('gamecommand', 'preload');
            b.requests[0].complete('error');
            assert.deepEqual(b.drain(), ['gamecommand\npreload']);
            b.advance(60000);
            assert.equal(b.requests.length, 1);
            assert.equal(b.timers.size, 0);
        """)


if __name__ == "__main__":
    unittest.main()
