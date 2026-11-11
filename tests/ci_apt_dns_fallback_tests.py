from __future__ import annotations

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github" / "workflows" / "release.yml"
APT_JOBS = (
    "linux-x86-regression",
    "ubuntu-x86",
    "fnql-game-linux",
    "linux-x86_64-regression",
    "ubuntu-x86_64",
    "fnql-game-linux-x86_64",
)
FALLBACK_STEP = "printf '%s\\n' \"$FNQL_APT_DNS_FALLBACK\" | sh -e"


def job_text(workflow: str, job: str) -> str:
    body = workflow.split(f"\n  {job}:\n", 1)[1]
    next_job = body.find("\n  ", 1)
    while next_job != -1 and body[next_job + 3] == " ":
        next_job = body.find("\n  ", next_job + 1)
    return body if next_job == -1 else body[:next_job]


class AptDnsFallbackTests(unittest.TestCase):
    def test_every_apt_job_resolves_mirrors_before_apt_get(self) -> None:
        workflow = WORKFLOW.read_text(encoding="utf-8")
        for job in APT_JOBS:
            with self.subTest(job=job):
                text = job_text(workflow, job)
                self.assertIn(FALLBACK_STEP, text)
                self.assertLess(text.index(FALLBACK_STEP), text.index("apt-get"))
                self.assertIn("-o Acquire::Retries=3 update", text)

    def test_fallback_is_a_no_op_while_runner_dns_works(self) -> None:
        workflow = WORKFLOW.read_text(encoding="utf-8")
        script = workflow.split("  FNQL_APT_DNS_FALLBACK: |\n", 1)[1].split("\njobs:", 1)[0]
        self.assertIn('getent hosts "$host"', script)
        self.assertLess(script.index("exit 0"), script.index("/etc/resolv.conf"))
        self.assertIn("azure.archive.ubuntu.com", script)
        self.assertIn('echo "::error::$host still does not resolve"; exit 1', script)


if __name__ == "__main__":
    unittest.main()
