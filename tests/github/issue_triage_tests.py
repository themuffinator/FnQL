from __future__ import annotations

import importlib.util
import json
import os
import sys
import tempfile
import unittest
import urllib.error
from unittest import mock
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = ROOT / ".github" / "scripts" / "issue_triage.py"
CONFIG_PATH = ROOT / ".github" / "issue-triage-config.json"

spec = importlib.util.spec_from_file_location("issue_triage", SCRIPT_PATH)
assert spec is not None
issue_triage = importlib.util.module_from_spec(spec)
assert spec.loader is not None
sys.modules[spec.name] = issue_triage
spec.loader.exec_module(issue_triage)


class IssueTriageTests(unittest.TestCase):
    def setUp(self) -> None:
        self.config = issue_triage.read_json(CONFIG_PATH)

    def test_parse_model_json_strips_code_fence(self) -> None:
        payload = "```json\n{\"summary\":\"ok\",\"issueType\":\"bug\"}\n```"
        parsed = issue_triage.parse_model_json(payload)
        self.assertEqual(parsed["summary"], "ok")
        self.assertEqual(parsed["issueType"], "bug")

    def test_similarity_score_prefers_near_duplicates(self) -> None:
        left = {
            "title": "screenshot cubemap produces 6 almost identical images",
            "body": "screenshot cubemap saves six front images on Linux Mint with r_fullscreen 0",
        }
        right = {
            "title": "cubemap screenshots save the same face six times",
            "body": "Using screenshot cubemap on Linux produces six front shots instead of front/back/left/right/top/bottom.",
        }
        other = {
            "title": "OpenAL device recovery request",
            "body": "Audio device reconnect should not require snd_restart.",
        }
        self.assertGreater(issue_triage.similarity_score(left, right), issue_triage.similarity_score(left, other))

    def test_validate_model_result_only_closes_high_confidence_duplicates(self) -> None:
        result = {
            "summary": "duplicate",
            "issueType": "duplicate",
            "componentLabel": "component:screenshots",
            "severity": "",
            "detectedPoints": ["same cubemap behavior"],
            "missingInfo": [],
            "answers": [],
            "needsHumanReview": False,
            "needsInfo": False,
            "appearsActionable": False,
            "shouldSplit": False,
            "fullDuplicate": True,
            "duplicateConfidence": 0.95,
            "relatedIssues": [
                {
                    "issueNumber": 3,
                    "relation": "full duplicate",
                    "sharedPoints": ["cubemap saves the same face six times"],
                    "reason": "The reproduction and outcome match.",
                    "confidence": 0.95,
                }
            ],
            "suggestedLabels": [{"name": "duplicate", "reason": "same report"}],
            "planSteps": [],
        }
        validated = issue_triage.validate_model_result(
            result,
            config=self.config,
            allowed_labels={"duplicate", "component:screenshots", "needs-human-review"},
            duplicate_candidates=[{"number": 3}],
        )
        self.assertTrue(validated["shouldCloseDuplicate"])
        self.assertFalse(validated["needsHumanReview"])

    def test_validate_model_result_refuses_low_duplicate_close_thresholds(self) -> None:
        config = dict(self.config)
        config["duplicate_close_confidence"] = 0.1
        result = {
            "summary": "duplicate",
            "issueType": "duplicate",
            "componentLabel": "component:screenshots",
            "severity": "",
            "detectedPoints": ["same cubemap behavior"],
            "missingInfo": [],
            "answers": [],
            "needsHumanReview": False,
            "needsInfo": False,
            "appearsActionable": False,
            "shouldSplit": False,
            "fullDuplicate": True,
            "duplicateConfidence": 0.5,
            "relatedIssues": [
                {
                    "issueNumber": 3,
                    "relation": "full duplicate",
                    "sharedPoints": ["cubemap saves the same face six times"],
                    "reason": "The reproduction appears similar.",
                    "confidence": 0.5,
                }
            ],
            "suggestedLabels": [{"name": "duplicate", "reason": "same report"}],
            "planSteps": [],
        }

        validated = issue_triage.validate_model_result(
            result,
            config=config,
            allowed_labels={"duplicate", "component:screenshots", "needs-human-review"},
            duplicate_candidates=[{"number": 3}],
        )

        self.assertFalse(validated["shouldCloseDuplicate"])
        self.assertTrue(validated["needsHumanReview"])

    def test_validate_model_result_treats_non_finite_confidence_as_zero(self) -> None:
        result = {
            "summary": "duplicate",
            "issueType": "duplicate",
            "componentLabel": "component:screenshots",
            "severity": "",
            "detectedPoints": ["same cubemap behavior"],
            "missingInfo": [],
            "answers": [],
            "needsHumanReview": False,
            "needsInfo": False,
            "appearsActionable": False,
            "shouldSplit": False,
            "fullDuplicate": True,
            "duplicateConfidence": "nan",
            "relatedIssues": [
                {
                    "issueNumber": 3,
                    "relation": "full duplicate",
                    "sharedPoints": ["cubemap saves the same face six times"],
                    "reason": "The reproduction appears similar.",
                    "confidence": "inf",
                }
            ],
            "suggestedLabels": [{"name": "duplicate", "reason": "same report"}],
            "planSteps": [],
        }

        validated = issue_triage.validate_model_result(
            result,
            config=self.config,
            allowed_labels={"duplicate", "component:screenshots", "needs-human-review"},
            duplicate_candidates=[{"number": 3}],
        )

        self.assertEqual(validated["duplicateConfidence"], 0.0)
        self.assertEqual(validated["relatedIssues"][0]["confidence"], 0.0)
        self.assertFalse(validated["shouldCloseDuplicate"])
        self.assertTrue(validated["needsHumanReview"])

    def test_validate_model_result_skips_malformed_list_items(self) -> None:
        result = {
            "summary": "triage",
            "issueType": "bug",
            "componentLabel": "component:screenshots",
            "severity": "",
            "detectedPoints": [],
            "missingInfo": [],
            "answers": [],
            "needsHumanReview": False,
            "needsInfo": False,
            "appearsActionable": True,
            "shouldSplit": False,
            "fullDuplicate": False,
            "duplicateConfidence": 0.2,
            "relatedIssues": [
                "not an object",
                {
                    "issueNumber": 3,
                    "relation": "partial overlap",
                    "sharedPoints": [],
                    "reason": "Shares the screenshot command.",
                    "confidence": 0.2,
                },
            ],
            "suggestedLabels": [
                "not an object",
                {"name": "component:screenshots", "reason": "Screenshot command."},
            ],
            "planSteps": [],
        }

        validated = issue_triage.validate_model_result(
            result,
            config=self.config,
            allowed_labels={"component:screenshots"},
            duplicate_candidates=[{"number": 3}],
        )

        self.assertEqual(len(validated["relatedIssues"]), 1)
        self.assertEqual(validated["relatedIssues"][0]["issueNumber"], 3)
        self.assertEqual(validated["suggestedLabels"], [{"name": "component:screenshots", "reason": "Screenshot command."}])

    def test_non_finite_duplicate_threshold_env_falls_back_to_config(self) -> None:
        config = dict(self.config)
        config["duplicate_close_confidence"] = 0.93
        with mock.patch.dict(
            os.environ,
            {"FNQL_ISSUE_TRIAGE_DUPLICATE_CLOSE_THRESHOLD": "nan"},
        ):
            self.assertEqual(issue_triage.duplicate_close_threshold(config), 0.93)

    def test_parse_repo_full_name_rejects_invalid_repository_names(self) -> None:
        self.assertEqual(issue_triage.parse_repo_full_name("owner/repo"), ("owner", "repo"))
        for value in ("owner", "/repo", "owner/", "owner/repo/extra"):
            with self.subTest(value=value):
                with self.assertRaisesRegex(ValueError, "owner/repo"):
                    issue_triage.parse_repo_full_name(value)

    def test_parse_args_rejects_non_positive_issue_numbers(self) -> None:
        with self.assertRaisesRegex(Exception, "positive integer"):
            issue_triage.positive_int("0")

    def test_read_json_requires_top_level_object(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.json"
            path.write_text(json.dumps(["not", "an", "object"]), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "JSON object"):
                issue_triage.read_json(path)

    def test_github_client_request_wraps_api_network_failures(self) -> None:
        client = issue_triage.GitHubClient("owner", "repo", token="token")
        with mock.patch.object(
            issue_triage.urllib.request,
            "urlopen",
            side_effect=urllib.error.URLError("offline"),
        ):
            with self.assertRaisesRegex(RuntimeError, r"GitHub API request GET /issues/1 failed"):
                client.request("GET", "/issues/1")

    def test_issue_triage_numeric_limits_are_bounded_and_safe(self) -> None:
        malformed_config = {
            "max_open_issues": "not-an-int",
            "max_duplicate_candidates": True,
        }
        self.assertEqual(issue_triage.max_open_issues(malformed_config), 50)
        self.assertEqual(issue_triage.max_duplicate_candidates(malformed_config), 8)
        self.assertEqual(issue_triage.max_duplicate_candidates({"max_duplicate_candidates": 500}), 20)

        with mock.patch.dict(
            os.environ,
            {"FNQL_ISSUE_TRIAGE_MAX_OPEN_ISSUES": "500"},
        ):
            self.assertEqual(issue_triage.max_open_issues({}), 200)

    def test_close_duplicate_uses_not_planned_state_reason(self) -> None:
        class RecordingClient(issue_triage.GitHubClient):
            def __init__(self) -> None:
                super().__init__("owner", "repo", token=None)
                self.calls: list[tuple[str, str, object]] = []

            def request(self, method: str, path: str, **kwargs: object) -> object:
                self.calls.append((method, path, kwargs.get("payload")))
                return {}

        client = RecordingClient()
        client.close_issue(17)

        self.assertEqual(
            client.calls,
            [
                (
                    "PATCH",
                    "/issues/17",
                    {"state": "closed", "state_reason": "not_planned"},
                )
            ],
        )

    def test_managed_label_config_validation_rejects_bad_records(self) -> None:
        with self.assertRaisesRegex(ValueError, "invalid GitHub label color"):
            issue_triage.managed_labels_from_config(
                {
                    "managed_labels": [
                        {
                            "name": "type:bug",
                            "color": "not-hex",
                            "description": "Bug reports.",
                        }
                    ]
                }
            )

        with self.assertRaisesRegex(ValueError, "duplicate label"):
            issue_triage.managed_labels_from_config(
                {
                    "managed_labels": [
                        {"name": "duplicate", "color": "ffffff", "description": "One."},
                        {"name": "duplicate", "color": "eeeeee", "description": "Two."},
                    ]
                }
            )

    def test_build_comment_uses_required_sections(self) -> None:
        analysis = {
            "summary": "Cube-map screenshots appear to repeat the front face.",
            "issueType": "bug",
            "componentLabel": "component:screenshots",
            "severity": "",
            "detectedPoints": ["`screenshot cubemap` saves repeated front images."],
            "missingInfo": ["A matching screenshot set from the current build."],
            "answers": ["The screenshot guide says cube-map captures should write six named faces."],
            "needsHumanReview": False,
            "needsInfo": True,
            "appearsActionable": True,
            "shouldSplit": False,
            "fullDuplicate": False,
            "duplicateConfidence": 0.0,
            "shouldCloseDuplicate": False,
            "relatedIssues": [],
            "suggestedLabels": [],
            "planSteps": ["Reproduce the cubemap capture.", "Inspect face naming and capture rotation.", "Add or update a focused regression check."],
        }
        comment = issue_triage.build_comment(
            analysis,
            {
                "type:bug": "Detected main issue type: bug.",
                "component:screenshots": "Issue text and repository context point to this component.",
                "needs-info": "Critical reproduction or environment details are still missing.",
            },
        )
        self.assertIn("## Summary", comment)
        self.assertIn("## Detected points", comment)
        self.assertIn("## Status", comment)
        self.assertIn("needs more information", comment.lower())

    def test_github_models_triage_wraps_network_failures(self) -> None:
        issue = {
            "number": 1,
            "title": "Cubemap screenshots repeat one face",
            "body": "The cubemap command writes six front images.",
            "user": {"login": "player"},
        }

        with mock.patch.object(
            issue_triage.urllib.request,
            "urlopen",
            side_effect=urllib.error.URLError("offline"),
        ):
            with self.assertRaisesRegex(RuntimeError, "GitHub Models request failed"):
                issue_triage.github_models_triage(
                    repo_full_name="owner/repo",
                    model="openai/gpt-4.1",
                    token="token",
                    timeout=1,
                    issue=issue,
                    labels=[],
                    duplicate_candidates=[],
                    repo_context="",
                    config=self.config,
                    maintainer_style_hint="",
                )


if __name__ == "__main__":
    unittest.main()
