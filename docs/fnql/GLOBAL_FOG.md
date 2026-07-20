# Global Fog Sidecars

FnQL supports an optional, visual-only global-fog layer for map authors. It is
disabled by default and does not alter BSP fog volumes, visibility, collision,
network state, demos, or the VM/native-module ABI. The feature therefore does
not reinterpret retail Quake Live map data when it is off.

## Availability and fallback

Set `r_globalFog 1` and restart the renderer with `vid_restart` to enable
sidecar discovery and the fog compositor. `r_globalFogStrength` is a live
multiplier in the range 0 through 1; its default is 1.

The GLx, VK, and RTX renderers implement the effect. A missing sidecar,
malformed input, unavailable depth texture, or failure to compile an optional
fog shader disables only this
effect. The map and renderer continue without global fog.

## File lookup

For `maps/example.bsp`, the renderer requests `maps/example.fog` through the
normal engine filesystem API only when `r_globalFog` is enabled. The
engine-owned root `FnQL-pkg.fnz` archive may provide allowlisted `maps/*.fog`
files with the same deterministic source-port precedence already used for
`maps/*.azb`. If the root archive has no matching entry, the active game,
package, and loose-file search order remains the fallback.

`FnQL-pkg.fnz` ships one conservative fog sidecar for each of the 149 BSP maps
actually present in the retail Steam `baseq3/pak00.pk3`. The presets are
deterministically derived from each map's world bounds, average lightmap colour,
and authored BSP-fog count. They remain opt-in because `r_globalFog` defaults to
`0`, and maps with authored BSP fog receive a lower blend cap so the optional
layer does not replace or double the map's native fog.

Arena metadata for optional and holiday Workshop maps is not treated as stock
map content when the corresponding BSP is absent from the retail package. Those
maps, other Workshop maps, and custom maps retain the normal missing-sidecar
fallback described above. The allowlist does not expose arbitrary root-archive
files, and it does not change retail package names or paths.

## Format

The UTF-8/ASCII-compatible text file is limited to 16 KiB. Keywords and values
are separated by ASCII whitespace; `//` starts a comment. Directives may appear
in any order, but each may appear at most once. Unknown directives, overlong
tokens, embedded NUL bytes, non-finite numbers, missing required values, and
out-of-range values reject the whole file.

```text
// required normalized RGB and density
color 0.45 0.55 0.65
density 0.0015

// optional values shown with their defaults
mode exp2
start 0
opacity 1
sky true
```

The directives are:

- `color <r> <g> <b>`: required; each component is from 0 through 1.
- `density <value>`: required; greater than 0 and no greater than 0.1.
- `mode <exp|exp2|linear>`: optional; defaults to `exp2`.
- `start <distance>`: optional non-negative world-unit distance; defaults to 0.
- `end <distance>`: required only for `linear`, and must be greater than `start`.
- `opacity <value>`: optional final blend cap from 0 through 1; defaults to 1.
- `sky <0|1|true|false|yes|no>`: optional; defaults to true. When false,
  clear-depth sky pixels are excluded.

For distance `d` after subtracting `start`, exponential mode uses
`1 - exp(-density * d)`, squared-exponential mode uses
`1 - exp(-(density * d)^2)`, and linear mode uses
`clamp(d / (end - start), 0, 1)`. The selected amount is multiplied by
`opacity` and `r_globalFogStrength` before blending toward `color`.

## Validation contract

The parser receives the exact byte count returned by the filesystem and never
scans beyond it. Sidecars are loaded only after the BSP world load succeeds,
and invalid state is cleared before rendering. The compositor runs after the
world scene and authored BSP fog, but before motion blur, bloom/gamma, and
later HUD or console scenes. This ordering keeps the feature isolated from
compatibility-sensitive map, protocol, and module behavior.
