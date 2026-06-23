# Audio Zone Compiler

`fnql-audiozonesc` compiles optional `maps/<map>.azb` sidecars for the OpenAL
environment system. The game ignores missing or invalid sidecars and keeps the
generic trace-based OpenAL environment heuristics.

Sidecars can be written by hand as `maps/<map>.audiozones`, or generated from an
existing Quake III `maps/<map>.bsp`. Generated zones are intended as a solid
first pass: they are derived from BSP leaves, clusters, areas, surfaces, brushes,
shader contents, and surface flags, then can be merged with small manual
overrides for places that need art-directed tuning.

Example:

```text
audiozones 1

zone "atrium" {
  bounds -512 -512 -64 512 512 384
  environment hall
  material stone
  flag outdoor
  reverbGain 1.10
  occlusionMultiplier 0.85
  lpfBias 0.95
  hpfBias 1.00
  transitionMs 900
  priority 10

  portal "hallway" {
    bounds 512 -128 -64 512 128 192
    openness 0.80
    blendDistance 128
    minBlend 0.03
    maxBlend 0.35
    curve ease-out
  }
}

zone "hallway" {
  bounds 512 -128 -64 896 128 192
  environment hallway
  material metal

  portal "atrium" {
    bounds 512 -128 -64 512 128 192
    openness 0.80
    blendDistance 128
    minBlend 0.03
    maxBlend 0.35
    curve ease-out
  }
}
```

Build and run:

```powershell
cmake --build .tmp/cmake-check --target fnql-audiozonesc --config Release
.tmp/cmake-check/fnql-audiozonesc.exe -o baseq3/maps/q3dm17.azb baseq3/maps/q3dm17.audiozones
.tmp/cmake-check/fnql-audiozonesc.exe --from-bsp -o baseq3/maps/q3dm17.azb baseq3/maps/q3dm17.bsp
.tmp/cmake-check/fnql-audiozonesc.exe --from-bsp --merge baseq3/maps/q3dm17.audiozones -o baseq3/maps/q3dm17.azb baseq3/maps/q3dm17.bsp
.tmp/cmake-check/fnql-audiozonesc.exe --from-bsp --material-map docs/audio-materials.txt -o baseq3/maps/q3dm17.azb baseq3/maps/q3dm17.bsp
.tmp/cmake-check/fnql-audiozonesc.exe --dump baseq3/maps/q3dm17.azb
.tmp/cmake-check/fnql-audiozonesc.exe --audit --samples 32768 baseq3/maps/q3dm17.azb
```

Supported environment names are `small-room`, `room`, `stone-room`, `hallway`,
`hall`, `outdoors`, and `underwater`. Supported material names are `unknown`,
`neutral`, `stone`, `metal`, `liquid`, `sky`, and `soft`. Bounds are
axis-aligned boxes in Quake world units. Higher `priority` wins when zones
overlap; equal priorities prefer the smaller box.

Version 2 metadata can be authored directly:

- `material <name>` stores the acoustic material class.
- `flag outdoor` and `flag underwater` set runtime environment flags. `outdoor
  true` and `underwater true` are equivalent.
- `portal "<target zone>" { bounds ... openness 0.0..1.0 }` adds an explicit
  cross-zone transition hint. `targetZone <index>` is also accepted for generated
  tooling, but named targets are preferred for hand-authored files. Reciprocal
  portals are recommended so `--audit --strict` can be used cleanly.
- Portal tuning is optional: `blendDistance` sets the listener distance in Quake
  units, `minBlend` sets the threshold below which the portal is ignored,
  `maxBlend` caps the crossfade, and `curve` accepts `smooth`, `linear`,
  `ease-in`, or `ease-out`.

Generated BSP zones use negative priorities, so normal hand-authored zones with
the default priority `0` override them naturally. Merged hand-authored zones keep
their material, outdoor/underwater flags, and explicit portals; only the internal
generated flag is stripped from overrides. The compiler writes version 3 sidecars
with material classes, portal hints, and per-portal blend tuning between adjacent
generated volumes. The runtime still accepts version 1 and version 2 sidecars;
with version 2+ files it uses the generated outdoor/underwater flags and applies
a bounded crossfade toward adjacent zone environments when the listener is near a
portal hint. Version 2 portals inherit the default 192-unit smooth blend,
0.02 minimum threshold, and 0.45 maximum crossfade.

For generated BSP zones, `--material-map <path>` lets maintainers override weak
shader-name heuristics without editing the map. Each non-comment line is:

```text
shader/pattern material [preset name] [flag outdoor] [weight N]
```

Patterns are case-insensitive path substrings unless they contain `*` or `?`, in
which case they are matched as simple wildcards. Materials use the same names as
hand-authored zones; optional `preset`, `flag`, and `weight` fields make a rule
more authoritative. For example:

```text
textures/custom/pipe_* metal preset hallway weight 8
textures/custom/canopy sky preset outdoors flag outdoor
textures/custom/slosh liquid preset underwater flag underwater weight 12
```

BSP generation now weights material votes by shader reference type: visible draw
surfaces count more than brush bodies, and brush sides count as supporting
evidence. When generated zones are coarsened to meet `--max-zones`, the dominant
material metadata is recomputed from accumulated material weights instead of
keeping whichever leaf happened to merge first.

Use `--audit` on generated sidecars before listening passes. It runs the same
runtime parser used by the client, prints preset/material/flag/portal coverage,
reports suspicious overlap or portal patterns, summarizes portal tuning, and
performs a deterministic zone lookup/portal-blend profile across the sidecar
bounds. The audit also emits material, portal, lookup, overlap, overall
confidence, an anomaly score, and a grade so generated maps can be triaged before
listening. `--samples N` controls the profile grid size; `--strict` returns a
non-zero exit code when warnings are emitted, which is useful for CI experiments
or large-map sweeps.

## Bulk migration sweeps

Large map sets can be migrated with `scripts/audio_zone_sweep.py`. The script
discovers `.bsp` files, generates matching `.azb` sidecars, merges any
corresponding `.audiozones` overrides, audits every result, and writes both JSON
and CSV reports with warning, confidence, and anomaly fields for CI artifacts or
listening-triage notes.

```powershell
python scripts/audio_zone_sweep.py `
  --tool .tmp/cmake-check/Release/fnql-audiozonesc.exe `
  --relative-root baseq3 `
  --override-root baseq3 `
  --material-map docs/audio-materials.txt `
  --output-root .tmp/audio-zone-sweeps/baseq3 `
  --strict `
  baseq3/maps
```

Use `--dry-run` to review the planned compiler and audit commands without
touching generated sidecars. `--samples N` controls the per-map audit grid, and
`--max-zones N` is forwarded to BSP generation for conservative fallback passes
on unusually fragmented legacy maps. The default reports are
`audio-zone-sweep.json` and `audio-zone-sweep.csv` under the output root.

## Standard Q3A sidecars

FnQL ships generated `.azb` sidecars for the standard Quake III Arena
`baseq3` arena maps. The tracked package source sidecars live under
`pkg/baseq3/maps/`, and release/install builds pack them into
`FnQL-pkg.fnz` under `baseq3/maps/` archive paths. The same package source
tree also carries other data-only OpenAL tuning files, such as the standard
weapon sound shaders under `pkg/baseq3/sound/` and `pkg/missionpack/sound/`.
Regenerate the sidecars from a local retail `baseq3` install with:

```powershell
python scripts/generate_standard_audio_zones.py `
  --tool meson/build/fnql-audiozonesc.exe `
  "C:/Program Files (x86)/Steam/steamapps/common/Quake 3 Arena/baseq3"
```

The helper reads only official `pak0.pk3` through `pak8.pk3` style archives,
derives the map set from shipped arena metadata, extracts BSPs into `.tmp/`,
and writes the compiled sidecars back to `pkg/baseq3/maps/`. Its default
`--max-zones 512` keeps generated lookup cost bounded while preserving detailed
coverage for maps that stay below the cap.
