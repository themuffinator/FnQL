<div align="center">

# FnQL
### Fappin' Quake Live

<p>
  <a href="https://github.com/themuffinator/FnQL"><img alt="Project Home" src="https://img.shields.io/badge/Project-FnQL-2563eb?style=for-the-badge&logo=github&logoColor=white"></a>
  <a href="#3-development-status"><img alt="Status" src="https://img.shields.io/badge/Status-Early%20Migration-f59e0b?style=for-the-badge"></a>
  <a href="#4-reference-workflow"><img alt="Reference Workflow" src="https://img.shields.io/badge/Workflow-QLSRP-16a34a?style=for-the-badge"></a>
  <a href="BUILD.md"><img alt="Build Guide" src="https://img.shields.io/badge/Build-Guide-7c3aed?style=for-the-badge"></a>
  <a href="LICENSE"><img alt="GPL-2.0 License" src="https://img.shields.io/badge/License-GPL--2.0-c2410c?style=for-the-badge"></a>
</p>

*Compatibility target: Retail Quake Live (Steam) · Base version: `0.1.0` · Current build: `0.1.0`*

</div>

## 1. Introduction

**FnQL** is the Quake Live branch of FnQ3: a modernized engine
for retail *Quake Live* under Steam, using FnQ3 as the imported technology
baseline and QLSRP as the reconstruction reference.

The goal is not to rebuild Quake Live game code. FnQL is an engine project. It
should load and serve retail-compatible content and module boundaries, preserve
Quake Live behavior where the engine differs from Quake III Arena, and carry
FnQ3's modern renderer, audio, platform, and tooling work forward where it
remains compatible.

## 2. Scope

### In scope

- Retail Quake Live Steam installation compatibility, including practical
  default-path discovery and `baseq3` data expectations.
- Engine compatibility reconstructed from QLSRP evidence: filesystem behavior,
  pak/pk3 loading, protocol and demo paths, VM/native module ABI, server and
  client glue, renderer data formats, platform services, and runtime identity.
- Modernized engine features inherited from FnQ3, including SDL3, OpenAL,
  modular OpenGL-lineage/GLx/Vulkan renderers, console and screenshot
  improvements, and deterministic release tooling.
- Compatibility validation against a legitimate retail Quake Live installation.

### Out of scope

- Reconstructing or shipping Quake Live `qagame`, `cgame`, or UI game code in
  FnQL.
- Bundling retail Quake Live assets.
- Claiming live Quake Live online-service replacement before an explicit,
  documented, default-off compatibility path exists.

## 3. Development Status

FnQL is at the initial migration stage:

- FnQ3 has been imported and rebranded to FnQL.
- The canonical project identity now targets retail Quake Live on Steam.
- Maintainer docs identify QLSRP and FnQ3 as the two primary reference
  repositories.
- The next development phase is subsystem comparison against QLSRP, followed by
  focused compatibility reconstruction in modern C++.

For day-to-day development instructions, see [AGENTS.md](AGENTS.md). For build
commands, see [BUILD.md](BUILD.md).

## 4. Reference Workflow

Use QLSRP before making compatibility claims:

1. Locate the owning subsystem in `E:\Repositories\QuakeLive-SRP`.
2. Compare FnQL's inherited FnQ3 implementation against QLSRP source and
   reference evidence.
3. Record observed behavior separately from inferred intent.
4. Reconstruct the engine-side compatibility needed by retail Quake Live.
5. Validate against a legitimate Steam install, normally
   `C:\Program Files (x86)\Steam\steamapps\common\Quake Live`.

## 5. Documentation

- [Build Guide](BUILD.md) for compiling FnQL locally.
- [Technical Notes](docs/fnql/TECHNICAL.md) for repository structure, release
  flow, and maintainer conventions.
- [GLx Renderer Guide](docs/GLX.md) for the canonical OpenGL-lineage renderer,
  migration notes, and troubleshooting.
- [Display Guide](docs/DISPLAY.md), [Audio Guide](docs/AUDIO.md),
  [Console Guide](docs/CONSOLE.md), and [Screenshot Guide](docs/SCREENSHOTS.md)
  for inherited FnQ3 feature documentation that will be updated as Quake Live
  compatibility work lands.

## 6. Credits

FnQL follows a clear upstream lineage from the
[Quake III Arena SDK](https://github.com/id-Software/Quake-III-Arena), through
[ioquake3](https://github.com/ioquake/ioq3), [Quake3e](https://github.com/ec-/Quake3e),
[FnQ3](https://github.com/themuffinator/FnQ3), and
[QL-SRP](https://github.com/themuffinator/QL-SRP).

See [CREDITS.md](CREDITS.md) for the fuller credits list, project
acknowledgements, and trademark note.
