# FnQL Release Notes Instructions

Use `docs/fnql/CHANGELOG.md`, merged PRs, commit messages, and relevant diffs
as raw material. Write for players, server operators, mod users, and testers.

## Categories

Use third-level Markdown headings and omit empty categories:

- `### Highlights` for the most important user-facing changes.
- `### Compatibility` for retail Quake Live, demo, protocol, VM/native module, pak/pk3, Steam install, or asset-loading behavior.
- `### Rendering and Display` for renderer, display, screenshot, texture, lighting, shader, GLx, OpenGL, or Vulkan changes.
- `### Audio` for audio backend, device, codec, HRTF, EFX, or streaming changes.
- `### Builds and Packaging` for release archives, platform builds, dependencies, CI outputs, and install layout.
- `### Fixes` for bugs, crashes, regressions, and stability work.
- `### Documentation and Tooling` only for user-visible docs or tools.

## Cleanup Rules

- Remove duplicates and merge near-duplicates.
- Prefer the changelog entry when it is clearer than the raw commit or PR title.
- Keep the final notes under 12 bullets unless the release genuinely needs more.
- Skip internal-only refactors, test reshuffling, generated-file churn, and maintainer planning docs unless they change a player-visible result or release package.
- Do not invent features, fixes, platforms, compatibility claims, or performance claims.
- Use concise present-tense bullets with no author attributions.
- Do not include a top-level release title; the release workflow adds build details separately.
