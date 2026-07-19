# Changelog

This is the pending release-note queue for the next FnQL release.

Keep short user-facing bullets under `Unreleased` as changes land. During release publishing, the workflow asks GitHub Copilot to dedupe and categorize the notes for the GitHub release details, then clears this section for the next cycle.

## [Unreleased]

### Highlights
- Official Windows packages now include the compiled Win32 FnQL Steam provider,
  enabling the legitimate retail Steam service path without publishing its
  proprietary source or Valve's Steam redistributable.

### Compatibility
- _None yet._

### Rendering and Display
- _None yet._

### Audio
- _None yet._

### Builds and Packaging
- Made MSYS2, MSVC, and provider binaries self-contained with static compiler
  runtimes, and added a PE import audit for unshipped runtime dependencies.
- Pinned the binary-only Steam provider release by version, SHA-256, and PE i386
  identity in both Windows release jobs.

### Fixes
- Fixed released MinGW executables failing at startup because
  `libgcc_s_dw2-1.dll` and related runtime DLLs were not packaged.

### Documentation and Tooling
- Documented that official compiled provider binaries may be released while
  FnQL-Steam source remains closed and non-redistributable.
