# Issue Triage Automation

`.github/workflows/issue-triage.yml` runs once when a new GitHub Issue is opened.

## What It Does

The workflow checks out the repository and runs `.github/scripts/issue_triage.py` against the newly opened Issue.

The script:

1. reads the new Issue title, body, author, and current labels
2. fetches the repository's existing labels
3. fetches open Issues for duplicate comparison
4. reads repository context from `README.md`, `BUILD.md`, `docs/fnql/TECHNICAL.md`, and issue-relevant docs such as `docs/GLX.md`, `docs/AUDIO.md`, `docs/SCREENSHOTS.md`, and `docs/CONSOLE.md`
5. sends a constrained JSON-only triage prompt to GitHub Models
6. validates the returned classification before applying labels, posting a comment, or closing anything
7. closes an Issue only when the model marks it as a full duplicate and the confidence still passes the code-side close threshold

The automation is conservative by design:

- it never removes human-applied labels
- it only creates labels that are explicitly defined in `.github/issue-triage-config.json` and only when they are actually needed
- it adds `needs-human-review` when the result is ambiguous, security-sensitive, policy-sensitive, split-worthy, or not confident enough to close as duplicate
- if the model call fails, it falls back to a neutral maintainer-review outcome instead of posting a fabricated answer

## Permissions

The workflow uses the minimum permissions it needs:

- `contents: read`
- `issues: write`
- `models: read`

No extra secret is required when GitHub-hosted Actions can call GitHub Models with the default `GITHUB_TOKEN`.

## Model Selection

The script resolves the model in this order:

1. repository variable `FNQL_ISSUE_TRIAGE_MODEL`
2. repository variable `FNQL_RELEASE_NOTES_MODEL`
3. built-in fallback `openai/gpt-4.1`

The fallback keeps the existing repository convention used by `scripts/manual_release.py`, but maintainers should set `FNQL_ISSUE_TRIAGE_MODEL` to the latest approved GPT model available in the repository's GitHub Models environment.

## Maintainer Tuning

Main tuning lives in `.github/issue-triage-config.json`.

Useful settings:

- `duplicate_close_confidence`: minimum confidence required before the script can close a full duplicate
- `duplicate_review_confidence`: lower duplicate threshold that keeps the Issue open but adds `needs-human-review`
- `max_open_issues`: cap on open Issues fetched for duplicate comparison
- `max_duplicate_candidates`: cap on candidates passed to the model
- `managed_labels`: labels the automation is allowed to create lazily
- `issue_type_to_label`: mapping from issue classification to applied label

Optional repository variables:

- `FNQL_ISSUE_TRIAGE_MODEL`
- `FNQL_RELEASE_NOTES_MODEL`
- `FNQL_ISSUE_TRIAGE_DUPLICATE_CLOSE_THRESHOLD`
- `FNQL_ISSUE_TRIAGE_DUPLICATE_REVIEW_THRESHOLD`
- `FNQL_ISSUE_TRIAGE_MAX_OPEN_ISSUES`
- `FNQL_ISSUE_TRIAGE_DRY_RUN`

`FNQL_ISSUE_TRIAGE_DRY_RUN=true` keeps the workflow read-only apart from normal API fetches, which is useful while tuning prompts and thresholds.

## Labels

The repository currently does not expose an established issue-label taxonomy through live issue usage, so the automation defines a small managed set for:

- issue type labels such as `type:bug`, `type:feature-request`, `type:build-install`, and `type:question`
- safety labels such as `needs-info`, `needs-human-review`, `duplicate`, and `invalid`
- a small component set covering current documented user-facing surfaces, such as `component:screenshots`, `component:audio`, `component:renderer-glx`, and `component:build`

If maintainers later add their own labels, the automation will reuse them when the model selects them. It still will not remove them.

## Testing

Local validation:

```sh
python3 -m unittest tests.github.issue_triage_tests
python3 .github/scripts/issue_triage.py triage --repo themuffinator/FnQL --issue-number 3 --dry-run
```

The dry-run mode prints the intended labels, comment body, and duplicate decision without mutating the Issue.
