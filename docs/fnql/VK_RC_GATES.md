# Vulkan Release Candidate Gates

## Status

The Vulkan renderer remains compatibility-sensitive even as it adopts modern Vulkan features. These gates turn the renderer roadmap work into repeatable release evidence: the renderer must load through the existing module boundary, render stock scenes, expose the expected diagnostics, keep screenshots working, and report timedemo data before Vulkan-specific defaults are promoted further.

The hosted CI jobs are planning and build checks. Blocking runtime evidence still requires a machine with a Vulkan driver and retail `baseq3` assets.

## Runtime Matrix

The first Vulkan RC evidence should cover:

- Windows 10 or newer, Win32/x86 dynamic renderer build, retail `baseq3` assets.
- Linux i686, Mesa or vendor Vulkan driver, dynamic renderer build, retail `baseq3` assets.

Additional packaged platforms should keep building Vulkan where the project already enables `USE_VK`, but they are not blocking runtime platforms until maintainers add stable GPU runners for them.

## Canonical Gate Presets

`scripts/vk_runtime_sweep.py` owns the machine-readable presets. Use `--list-gates` to print the script view.

| Gate | Purpose | Profile | Scene Set | Automated Floor |
|---|---|---|---|---|
| `vk-smoke` | Renderer lifecycle smoke for module load, map load, screenshot output, and `vkinfo`. | `baseline` | `q3dm1` | Runtime exits cleanly, expected screenshot is written, and required `vkinfo` fields are present. |
| `vk-modern` | Modern Vulkan path for FBO/HDR/bloom/MSAA, display-referred SDR shader gamma/overbright, ACES tone mapping, identity color-grading pass, exposure-aware bloom extraction, sync2/dynamic-rendering reporting, GPU timings, and timedemo data. | `vk-modern` | `q3dm1`, `q3dm17`, `demo1` | Screenshots are written, `vkinfo` is clean, `r_speeds 7` produces GPU timing spans, and timedemo metrics are captured. |
| `vk-hdr` | Native-HDR request path for HDR10 swapchain negotiation on capable display paths, using the same ACES tone scale, identity color-grading pass, bloom extraction profile, and `r_outputBackend 3` request as the modern SDR gate. | `vk-hdr` | `q3dm1` | The HDR10 output-backend request is visible in `vkinfo` as active or requested-unavailable, with screenshot output intact. |

The scene set is deliberately small and stock-data friendly. It may grow as regressions are found, but should not shrink during an RC cycle.

## Exit Criteria

A Vulkan RC candidate must meet all of these conditions:

- `vk-smoke` and `vk-modern` pass on every blocking runtime platform.
- Generated manifests, logs, screenshots, `vkinfo` output, GPU timing spans, and timedemo metrics are archived with the candidate build.
- `vkinfo` reports pipeline-cache state, HDR state, output-backend request/selection/native display state, tone-map, post-gamma, and bloom state, sync2/dynamic-rendering state, barrier counts, descriptor counters, command-pool resets, and memory counters.
- If sync2 is enabled, at least one sync2 barrier must be observed during a map gate.
- Manual screenshot review finds no unexplained drift in world visibility, sky, fog, lightmaps, weapon placement, marks/decals, particles, HUD/2D, bloom, gamma, or final output size.
- `vk-hdr` evidence is required before native HDR output is treated as a release feature rather than an opportunistic capability.

Failing any blocking criterion keeps the candidate in investigation. The fix may be renderer code, a narrower Vulkan profile, a documented waiver, or a larger test corpus, but not a silent default promotion.

## Typical Commands

From the repository root:

```sh
python scripts/vk_runtime_sweep.py --list-gates
python scripts/vk_runtime_sweep.py --gate vk-smoke --exe path/to/fnql.exe --basepath path/to/game/root
python scripts/vk_runtime_sweep.py --gate vk-modern --exe path/to/fnql.exe --basepath path/to/game/root
python scripts/vk_runtime_sweep.py --gate vk-hdr --exe path/to/fnql.exe --basepath path/to/game/root
```

Use `--dry-run` to generate configs, manifests, and Markdown summaries without requiring a built executable or retail assets. Dry runs are useful for reviewing expanded cvars, maps, demos, and commands, but they do not count as runtime evidence.

## Automated Verification

`.github/workflows/vulkan-verification.yml` provides the renderer-focused automation surface:

- `Vulkan renderer build` configures a focused Meson build and compiles the Vulkan renderer target on hosted Ubuntu.
- `Vulkan gate plans` runs the sweep parser tests, lists gate presets, generates dry-run artifacts for every named gate, and uploads them for review.
- `Vulkan runtime sweep` is a manual `workflow_dispatch` job for self-hosted Vulkan runners. It requires a built client executable and a retail `baseq3` basepath, writes a Markdown summary, and uploads the full sweep output.

Hosted dry-run gate artifacts are planning evidence only. Blocking RC evidence still requires non-dry-run runtime artifacts from the blocking Windows and Linux matrix.
