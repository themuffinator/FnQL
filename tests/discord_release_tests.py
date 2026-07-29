from __future__ import annotations

import io
import json
import sys
import tempfile
import unittest
import urllib.error
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts import discord_release


RELEASE_NOTES = """# FnQL Release

## Highlights

- Retail Steam session tickets now survive reconnects.
- Renderer module loading is deterministic.

- Fix menu input routing.

## Build details

- Channel: manual
- Build version: 0.1.0.58

<details>
<summary>Included commits</summary>

- f35445c9 Reset pending changelog after release

</details>
"""

MANIFEST = {
    "project": "FnQL",
    "channel": "manual",
    "version": "0.1.0.58",
    "release_tag": "0.1.0.58-20260728-f35445c9",
    "release_title": "FnQL 2026-07-28 (0.1.0.58)",
    "build_date": "2026-07-28",
    "commit": "f35445c9",
    "archives": [
        {"artifact_dir": "linux-x86", "archive": "fnql-0.1.0.58-linux-x86.tar.gz"},
        {"artifact_dir": "windows-mingw-x86", "archive": "fnql-0.1.0.58-windows-mingw-x86.zip"},
        {"artifact_dir": "windows-msvc-x86", "archive": "fnql-0.1.0.58-windows-msvc-x86.zip"},
    ],
}


def sample_payload(**overrides: object) -> dict[str, object]:
    arguments: dict[str, object] = {
        "repository": "themuffinator/FnQL",
        "release_tag": "0.1.0.58-20260728-f35445c9",
        "release_title": "FnQL 2026-07-28 (0.1.0.58)",
        "version_label": "0.1.0.58",
        "build_date": "2026-07-28",
        "commit": "f35445c9",
        "archives": MANIFEST["archives"],
        "notes": RELEASE_NOTES,
    }
    arguments.update(overrides)
    return discord_release.build_payload(**arguments)  # type: ignore[arg-type]


class DiscordReleaseNotesTests(unittest.TestCase):
    def test_highlights_stop_before_build_details_and_commit_list(self) -> None:
        highlights = discord_release.extract_highlights(RELEASE_NOTES)
        self.assertIn("- Retail Steam session tickets now survive reconnects.", highlights)
        self.assertIn("- Fix menu input routing.", highlights)
        self.assertNotIn("Build details", highlights)
        self.assertNotIn("<details>", highlights)
        self.assertNotIn("Channel: manual", highlights)

    def test_highlights_fall_back_to_notes_without_the_generated_heading(self) -> None:
        notes = (
            "# FnQL Release\n\n"
            "### Rendering and Display\n- Deterministic renderer module load order.\n\n"
            "## Build details\n\n- Channel: manual\n\n"
            "<details>\n<summary>Included commits</summary>\n\n- abc1234 commit\n\n</details>\n"
        )
        highlights = discord_release.extract_highlights(notes)
        self.assertEqual(
            highlights,
            "### Rendering and Display\n- Deterministic renderer module load order.",
        )

    def test_intro_uses_a_release_focused_topic_sentence(self) -> None:
        intro = discord_release.build_intro("FnQL", "0.1.0.58", discord_release.extract_highlights(RELEASE_NOTES))
        self.assertEqual(
            intro,
            "The new FnQL release is ready, with renderer and display work that needs real gameplay miles "
            "against retail Quake Live.",
        )

    def test_intro_falls_back_to_a_topic_sentence_without_bullets(self) -> None:
        intro = discord_release.build_intro("FnQL", "0.1.0.58", "### Rendering and Display\nRenderer work.")
        self.assertTrue(intro.startswith("The new FnQL release is ready, with renderer"))

    def test_discord_highlights_flatten_release_note_categories(self) -> None:
        formatted = discord_release.format_highlights(
            "### Highlights\n- A major compatibility improvement.\n\n### Fixes\n- A focused fix."
        )
        self.assertEqual(formatted, "- A major compatibility improvement.\n\n- A focused fix.")

    def test_long_notes_are_truncated_into_the_description_budget(self) -> None:
        notes = "## Highlights\n\n" + "\n".join(
            f"- Change number {index} with a long user-facing description." for index in range(400)
        )
        payload = sample_payload(notes=notes)
        description = payload["embeds"][0]["description"]  # type: ignore[index]
        self.assertLessEqual(len(description), discord_release.MAX_DESCRIPTION_CHARS)
        self.assertIn("...", description)
        self.assertTrue(
            description.endswith(
                "[Full release notes and downloads](https://github.com/themuffinator/FnQL/releases/tag/"
                "0.1.0.58-20260728-f35445c9)"
            )
        )


class DiscordReleasePayloadTests(unittest.TestCase):
    def test_payload_uses_the_brilliant_red_highlight_and_release_identity(self) -> None:
        payload = sample_payload()
        embed = payload["embeds"][0]  # type: ignore[index]

        self.assertEqual(payload["username"], "FnQL Releases")
        self.assertEqual(payload["avatar_url"], discord_release.DEFAULT_AVATAR_URL)
        self.assertEqual(
            payload["allowed_mentions"],
            {"parse": [], "roles": ["1424165541572120647", "1390287267276525628"]},
        )
        self.assertEqual(
            payload["content"],
            "<:quakelive:1390987057454776481> FnQL 0.1.0.58 release published! "
            "<@&1424165541572120647> <@&1390287267276525628>",
        )
        self.assertEqual(embed["color"], 0xE60000)
        self.assertEqual(embed["title"], "FnQL 0.1.0.58")
        self.assertEqual(
            embed["url"],
            "https://github.com/themuffinator/FnQL/releases/tag/0.1.0.58-20260728-f35445c9",
        )
        self.assertEqual(embed["timestamp"], "2026-07-28T00:00:00Z")
        self.assertEqual(embed["footer"], {"text": "Fappin' Quake Live | Target: Retail Quake Live (Steam)"})

    def test_payload_fields_report_version_state_and_ordered_downloads(self) -> None:
        fields = sample_payload()["embeds"][0]["fields"]  # type: ignore[index]
        self.assertEqual(fields[0], {"name": "Version", "value": "0.1.0.58", "inline": True})
        self.assertEqual(fields[1], {"name": "State", "value": "Stable release", "inline": True})

        downloads = fields[2]["value"].splitlines()
        self.assertEqual(
            [line.split("]", 1)[0].lstrip("[") for line in downloads],
            ["Windows x86 (MSVC)", "Windows x86 (MinGW)", "Linux x86", "Source Code"],
        )
        self.assertIn(
            "https://github.com/themuffinator/FnQL/releases/download/"
            "0.1.0.58-20260728-f35445c9/fnql-0.1.0.58-windows-msvc-x86.zip",
            downloads[0],
        )
        self.assertLessEqual(len(fields[2]["value"]), discord_release.MAX_FIELD_VALUE_CHARS)

    def test_payload_without_archives_still_links_the_release(self) -> None:
        fields = sample_payload(archives=[])["embeds"][0]["fields"]  # type: ignore[index]
        self.assertIn("Source Code", fields[2]["value"])

    def test_download_links_are_dropped_whole_rather_than_truncated(self) -> None:
        payload = sample_payload(
            archives=[
                {"artifact_dir": f"linux-x86-{index}", "archive": f"fnql-{'a' * 200}-{index}.tar.gz"}
                for index in range(12)
            ]
        )
        downloads = payload["embeds"][0]["fields"][2]["value"]  # type: ignore[index]
        self.assertLessEqual(len(downloads), discord_release.MAX_FIELD_VALUE_CHARS)
        for line in downloads.splitlines():
            self.assertRegex(line, r"^\[[^\]]+\]\(https://\S+\)$")

    def test_payload_only_allows_its_explicit_role_mentions(self) -> None:
        payload = sample_payload(notes="## Highlights\n\n- @everyone please test this build.")
        self.assertEqual(
            payload["allowed_mentions"],
            {"parse": [], "roles": ["1424165541572120647", "1390287267276525628"]},
        )
        self.assertIn("@everyone", payload["embeds"][0]["description"])  # type: ignore[index]

    def test_role_mentions_are_deduplicated_and_can_be_disabled(self) -> None:
        payload = sample_payload(role_mentions="<@&12345678901234567> <@&12345678901234567>")
        self.assertEqual(payload["allowed_mentions"], {"parse": [], "roles": ["12345678901234567"]})
        self.assertIn("<@&12345678901234567>", payload["content"])

        without_mentions = sample_payload(role_mentions="")
        self.assertEqual(without_mentions["allowed_mentions"], {"parse": [], "roles": []})
        self.assertNotIn("<@&", without_mentions["content"])

    def test_unknown_artifact_directories_keep_a_readable_label(self) -> None:
        payload = sample_payload(
            archives=[{"artifact_dir": "linux-arm64", "archive": "fnql-0.1.0.58-linux-arm64.tar.gz"}]
        )
        downloads = payload["embeds"][0]["fields"][2]["value"]  # type: ignore[index]
        self.assertIn("[linux-arm64](", downloads)

    def test_feedback_channel_defaults_to_fnql_and_can_be_overridden(self) -> None:
        self.assertIn(
            "Feedback is welcome in <#1528370439997362216>.",
            sample_payload()["embeds"][0]["description"],  # type: ignore[index]
        )
        payload = sample_payload(feedback_channel="<#123456789>")
        self.assertIn(
            "Feedback is welcome in <#123456789>.",
            payload["embeds"][0]["description"],  # type: ignore[index]
        )

    def test_invalid_build_dates_do_not_emit_a_broken_timestamp(self) -> None:
        self.assertNotIn("timestamp", sample_payload(build_date="not-a-date")["embeds"][0])  # type: ignore[index]


class DiscordReleaseColorTests(unittest.TestCase):
    def test_color_parsing_accepts_hex_and_decimal_forms(self) -> None:
        self.assertEqual(discord_release.parse_color(None), 0xE60000)
        self.assertEqual(discord_release.parse_color(""), 0xE60000)
        self.assertEqual(discord_release.parse_color("#E60000"), 0xE60000)
        self.assertEqual(discord_release.parse_color("0xE60000"), 0xE60000)
        self.assertEqual(discord_release.parse_color("E60000"), 0xE60000)
        self.assertEqual(discord_release.parse_color("15073280"), 0xE60000)
        self.assertEqual(discord_release.parse_color(0xE60000), 0xE60000)

    def test_color_parsing_rejects_junk_and_out_of_range_values(self) -> None:
        for value in ("wine-red", "#GGGGGG", "0x1000000", "-1"):
            with self.assertRaises(ValueError):
                discord_release.parse_color(value)


class DiscordWebhookTransportTests(unittest.TestCase):
    def test_webhook_urls_must_be_official_discord_endpoints(self) -> None:
        self.assertEqual(
            discord_release.validated_webhook_url("https://discord.com/api/webhooks/1/token"),
            "https://discord.com/api/webhooks/1/token",
        )
        for value in (
            "http://discord.com/api/webhooks/1/token",
            "https://discord.com.evil.example/api/webhooks/1/token",
            "https://example.com/api/webhooks/1/token",
            "https://discord.com/api/channels/1/messages",
        ):
            with self.assertRaises(ValueError):
                discord_release.validated_webhook_url(value)

    def test_delivery_is_confirmed_with_the_wait_query(self) -> None:
        self.assertEqual(
            discord_release.wait_for_delivery("https://discord.com/api/webhooks/1/token"),
            "https://discord.com/api/webhooks/1/token?wait=true",
        )

    def test_post_sends_the_payload_as_json(self) -> None:
        response = mock.MagicMock()
        response.read.return_value = b'{"id":"1"}'
        response.__enter__.return_value = response

        with mock.patch.object(discord_release.urllib.request, "urlopen", return_value=response) as urlopen:
            body = discord_release.post_payload(
                "https://discord.com/api/webhooks/1/token",
                {"content": "hello"},
                sleep=lambda _seconds: None,
            )

        request = urlopen.call_args.args[0]
        self.assertEqual(body, '{"id":"1"}')
        self.assertEqual(request.method, "POST")
        self.assertEqual(request.full_url, "https://discord.com/api/webhooks/1/token?wait=true")
        self.assertEqual(request.headers["Content-type"], "application/json")
        self.assertEqual(json.loads(request.data.decode("utf-8")), {"content": "hello"})

    def test_rate_limited_posts_retry_with_the_requested_delay(self) -> None:
        rate_limited = urllib.error.HTTPError(
            "https://discord.com/api/webhooks/1/token",
            429,
            "Too Many Requests",
            {},
            io.BytesIO(b'{"retry_after": 1.5}'),
        )
        response = mock.MagicMock()
        response.read.return_value = b""
        response.__enter__.return_value = response
        delays: list[float] = []

        with mock.patch.object(
            discord_release.urllib.request,
            "urlopen",
            side_effect=[rate_limited, response],
        ):
            discord_release.post_payload(
                "https://discord.com/api/webhooks/1/token",
                {"content": "hello"},
                sleep=delays.append,
            )

        self.assertEqual(delays, [1.5])

    def test_rejected_posts_report_the_status_without_leaking_the_webhook(self) -> None:
        rejected = urllib.error.HTTPError(
            "https://discord.com/api/webhooks/1/secret-token",
            401,
            "Unauthorized",
            {},
            io.BytesIO(b'{"message": "Invalid Webhook Token"}'),
        )

        with mock.patch.object(discord_release.urllib.request, "urlopen", side_effect=rejected):
            with self.assertRaises(RuntimeError) as raised:
                discord_release.post_payload(
                    "https://discord.com/api/webhooks/1/secret-token",
                    {"content": "hello"},
                    sleep=lambda _seconds: None,
                )

        message = str(raised.exception)
        self.assertIn("HTTP 401", message)
        self.assertIn("Invalid Webhook Token", message)
        self.assertNotIn("secret-token", message)


class DiscordReleaseCommandTests(unittest.TestCase):
    def setUp(self) -> None:
        for name in discord_release.WEBHOOK_ENV_VARS:
            patcher = mock.patch.dict(discord_release.os.environ, {name: ""}, clear=False)
            patcher.start()
            self.addCleanup(patcher.stop)

    def test_dry_run_writes_the_payload_without_posting(self) -> None:
        with tempfile.TemporaryDirectory() as raw_temp:
            temp = Path(raw_temp)
            manifest = temp / "release-manifest.json"
            manifest.write_text(json.dumps(MANIFEST), encoding="utf-8")
            notes = temp / "release-notes.md"
            notes.write_text(RELEASE_NOTES, encoding="utf-8")
            output = temp / "payload.json"

            with mock.patch.object(discord_release, "post_payload") as post:
                with mock.patch("sys.stdout", new_callable=io.StringIO):
                    status = discord_release.main(
                        [
                            "--manifest",
                            str(manifest),
                            "--notes-file",
                            str(notes),
                            "--output",
                            str(output),
                            "--dry-run",
                        ]
                    )

            payload = json.loads(output.read_text(encoding="utf-8"))

        self.assertEqual(status, 0)
        post.assert_not_called()
        self.assertEqual(payload["embeds"][0]["color"], 0xE60000)
        self.assertEqual(payload["embeds"][0]["title"], "FnQL 0.1.0.58")

    def test_missing_release_identity_fails_before_posting(self) -> None:
        with mock.patch.object(discord_release, "post_payload") as post:
            with mock.patch("sys.stderr", new_callable=io.StringIO) as stderr:
                status = discord_release.main(["--manifest", "does-not-exist.json", "--dry-run"])

        self.assertEqual(status, 2)
        post.assert_not_called()
        self.assertIn("release tag is required", stderr.getvalue())

    def test_unconfigured_webhook_skips_quietly_by_default(self) -> None:
        with mock.patch.object(discord_release, "post_payload") as post:
            with mock.patch("sys.stderr", new_callable=io.StringIO) as stderr:
                status = discord_release.main(["--release-tag", "0.1.0.58-20260728-f35445c9"])

        self.assertEqual(status, 0)
        post.assert_not_called()
        self.assertIn("Skipping the release announcement", stderr.getvalue())

    def test_unconfigured_webhook_fails_when_required(self) -> None:
        with mock.patch.object(discord_release, "post_payload") as post:
            with mock.patch("sys.stderr", new_callable=io.StringIO):
                status = discord_release.main(
                    ["--release-tag", "0.1.0.58-20260728-f35445c9", "--require-webhook"]
                )

        self.assertEqual(status, 2)
        post.assert_not_called()

    def test_webhook_environment_variables_are_honored(self) -> None:
        with mock.patch.dict(
            discord_release.os.environ,
            {"FNQL_DISCORD_WEBHOOK_URL": "https://discord.com/api/webhooks/1/token"},
            clear=False,
        ):
            with mock.patch.object(discord_release, "post_payload") as post:
                with mock.patch("sys.stdout", new_callable=io.StringIO):
                    status = discord_release.main(["--release-tag", "0.1.0.58-20260728-f35445c9"])

        self.assertEqual(status, 0)
        post.assert_called_once()
        self.assertEqual(post.call_args.args[0], "https://discord.com/api/webhooks/1/token")

    def test_failed_delivery_fails_the_release_step(self) -> None:
        with mock.patch.object(
            discord_release,
            "post_payload",
            side_effect=RuntimeError("Discord webhook rejected the announcement: HTTP 404: not found"),
        ):
            with mock.patch("sys.stderr", new_callable=io.StringIO) as stderr:
                status = discord_release.main(
                    [
                        "--release-tag",
                        "0.1.0.58-20260728-f35445c9",
                        "--webhook-url",
                        "https://discord.com/api/webhooks/1/token",
                    ]
                )

        self.assertEqual(status, 1)
        self.assertIn("HTTP 404", stderr.getvalue())


class DiscordReleaseWorkflowTests(unittest.TestCase):
    def test_release_workflow_announces_as_the_final_publish_stage(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(encoding="utf-8")
        publish_job = workflow.split("  publish:", 1)[1]

        self.assertIn("HAS_DISCORD_WEBHOOK: ${{ secrets.DISCORD_RELEASE_WEBHOOK != '' }}", publish_job)
        self.assertIn("id: create_release", publish_job)
        self.assertIn("- name: Announce release on Discord", publish_job)
        self.assertIn("FNQL_DISCORD_WEBHOOK_URL: ${{ secrets.DISCORD_RELEASE_WEBHOOK }}", publish_job)
        self.assertIn("python scripts/discord_release.py", publish_job)
        self.assertIn("--require-webhook", publish_job)
        self.assertIn("steps.create_release.outcome == 'success'", publish_job)
        self.assertNotIn(
            "https://discord.com/api/webhooks/",
            workflow,
            "the release webhook must stay in repository secrets",
        )

        steps = [line for line in publish_job.splitlines() if line.startswith("      - name: ")]
        self.assertEqual(steps[-1], "      - name: Announce release on Discord")


if __name__ == "__main__":
    unittest.main()
