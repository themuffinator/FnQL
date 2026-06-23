# Vulkan Verification Tests

`scripts/vk_runtime_sweep.py` owns the Vulkan runtime gate presets and the log parser used by CI dry runs and self-hosted GPU sweeps. The Python tests in this directory do not create a Vulkan device or require game assets; they validate the deterministic parts of the sweep harness.

The harness currently checks:

- `vkinfo` parsing for pipeline-cache, HDR, tone-map, post-gamma, bloom, sync2/dynamic-rendering, barrier, descriptor, command-pool, memory, and GPU-timing lines.
- Gate failure behavior for missing sync2 barrier evidence, missing timedemo metrics, missing GPU timing samples, and invisible HDR requests.
- Dry-run behavior so hosted CI can publish gate plans without retail data.
- Generated map-sweep configs for `vkinfo`, `r_speeds 7`, and stable screenshot keys.
- Vulkan post-gamma source coverage for display-referred SDR gamma/overbright, scene-linear output, HDR output selection, and cvar-driven pipeline rebuilds.

Run the script tests directly:

```sh
python tests/vulkan/vk_runtime_sweep_tests.py
```

Generate dry-run gate artifacts from the repository root:

```sh
python scripts/vk_runtime_sweep.py --gate vk-smoke --dry-run
python scripts/vk_runtime_sweep.py --gate vk-modern --dry-run
python scripts/vk_runtime_sweep.py --gate vk-hdr --dry-run
```
