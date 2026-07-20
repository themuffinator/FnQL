# FnQL Documentation

FnQL is a modernized, engine-only Quake Live client for use with a legitimate
retail Steam installation. Start with the project [README](../README.md) for a
feature overview, installation steps, and the complete documentation index.

## Player Guides

- [Display](DISPLAY.md) — renderers, video modes, HDR, render scaling,
  anti-aliasing, lighting, bloom, fog, liquids, and post-processing.
- [RTX Renderer](RTX.md) — requirements, selection, fallback policy, and
  troubleshooting for the Vulkan ray-tracing renderer.
- [Visuals](VISUALS.md) — player highlighting, colours, cel shading, and
  outlines.
- [Aspect Handling](ASPECT_CORRECTION.md) — retail WebUI, menus, and
  cinematics.
- [Audio](AUDIO.md) — OpenAL, HRTF, surround sound, spatial effects, device
  selection, and fallback behavior.
- [Console](CONSOLE.md) — scalable text, scrolling, completion, clipboard, and
  mouse interaction.
- [Screenshots](SCREENSHOTS.md) — capture formats, filenames, metadata,
  levelshots, watermarks, and cubemaps.

## Building And Maintainer Documentation

- [Build Guide](../BUILD.md) — supported build systems, toolchains, options,
  and platform notes.
- [Technical Notes](fnql/TECHNICAL.md) — repository structure, release flow,
  compatibility workflow, and maintainer conventions.
- [Compatibility Roadmap](fnql/QL_COMPATIBILITY_ROADMAP.md) — subsystem status
  and retail validation boundaries.

FnQL does not ship Quake Live assets or reconstructed `qagame`, `cgame`, or UI
game code. Those files remain part of the user's legitimate Steam installation.
