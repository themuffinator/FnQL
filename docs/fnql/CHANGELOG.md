# Changelog

This is the pending release-note queue for the next FnQL release.

Keep short user-facing bullets under `Unreleased` as changes land. During release publishing, the workflow asks GitHub Copilot to dedupe and categorize the notes for the GitHub release details, then clears this section for the next cycle.

## [Unreleased]

### Highlights
- _None yet._

### Compatibility
- Add native Intel and Apple-Silicon macOS engine coverage with app-local
  renderers, Command-key input, MoltenVK portability, and explicit diagnostics
  for the retail package's absent macOS game modules.

### Rendering and Display
- _None yet._

### Audio
- _None yet._

### Builds and Packaging
- Publish Linux releases as deterministic permission-preserving tarballs and
  validate their i686 client, dedicated server, renderer modules, sidecars,
  archive paths, and executable modes before release.
- Build and test native macOS x86_64/arm64 artifacts, stage a canonical app,
  validate Mach-O architecture and dependencies, and support Universal 2,
  development output without project-applied signing, and isolated opt-in
  hardened-runtime signing, modern notarization, and stapling for public releases.

### Fixes
- _None yet._

### Documentation and Tooling
- Document native Linux build, multiarch release, extraction, dependency, and
  dedicated-server workflows together with the retail client-module boundary.
