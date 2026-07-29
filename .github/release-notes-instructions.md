# FnQL Release Notes Instructions

Use `docs/fnql/RELEASE_COMPLETION.md`, `docs/fnql/CHANGELOG.md`, merged PRs,
commit messages, and relevant diffs as raw material. Write for players, server
operators, mod users, and testers. A tracked release note in
`docs/fnql/releases/` takes precedence over this generated fallback.

## Categories

Return 3-12 plain Markdown bullets without a title or headings. Prioritize the
most important player-visible changes across compatibility, rendering and
display, audio, builds and packaging, fixes, and user-facing tools.

## Cleanup Rules

- Remove duplicates and merge near-duplicates.
- Prefer the changelog entry when it is clearer than the raw commit or PR title.
- Keep the final notes under 12 bullets unless the release genuinely needs more.
- Skip internal-only refactors, test reshuffling, generated-file churn, and maintainer planning docs unless they change a player-visible result or release package.
- Do not invent features, fixes, platforms, compatibility claims, or performance claims.
- Use concise present-tense bullets with no author attributions.
- Do not include a release title or any headings; the release workflow adds the
  `Highlights` and build-details sections separately.
