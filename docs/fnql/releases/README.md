# Curated FnQL Release Notes

Use this directory only when a release needs reviewed prose beyond the normal
generated highlights. Name the file either `<release-tag>.md` (preferred) or
`<version>.md`, for example `0.1.0.58-20260728-f35445c9.md` or `0.1.0.58.md`.

The manual release workflow chooses the tag-specific file first, then the
version file. A top-level title is optional and is removed from the published
body. Start the actual content with this format:

```markdown
## Highlights

- The player-visible change and its compatibility impact.
- Another concise change worth testing.

## Known limitations

- A concrete limitation, scope, and safe fallback where relevant.
```

Keep claims grounded in validated retail Quake Live behavior. Curated notes are
published on GitHub, while the Discord announcement presents their Highlights
section in the compact release embed.
