# Visuals Guide

FnQL keeps retail Quake Live visuals as the baseline, then adds a few optional engine-side presentation controls on top. This guide covers the current player highlight controls and points to the more focused guides for renderer display effects, menu/cinematic aspect handling, and console presentation.

## Cel Shading

Cel-shaded model lighting, model outlines, first-person weapon outline tuning, model shadow banding, and BSP world depth-edge outlines are renderer display effects. Their full cvar reference lives in the [Display Guide](DISPLAY.md#cel-shading).

## Player Highlighting

Player highlighting is off by default. When enabled, it draws an extra pass on player models using a rimlight, a stencil outline, or both.

- `cl_playerHighlight 0`: Disable player highlighting.
- `cl_playerHighlight 1`: Enable rimlight only.
- `cl_playerHighlight 2`: Enable stencil border only.
- `cl_playerHighlight 3`: Enable both rimlight and stencil border.

Behavior notes:

- Your own player model is excluded.
- HUD-head and other `RDF_NOWORLDMODEL` scenes are excluded.
- Corpses are excluded.
- In team modes, both teams can be highlighted.
- In non-team modes, highlighted players use the free-for-all color path.
- The stencil border only appears when the active renderer has stencil support.

## Highlight Colors

FnQL uses separate base colors for red team, blue team, and non-team modes.

- `cl_playerHighlightRedColor`: Base color for red-team players.
- `cl_playerHighlightBlueColor`: Base color for blue-team players.
- `cl_playerHighlightFreeColor`: Base color for non-team modes.

Each color cvar accepts `R G B` or `R G B A` values in `0-255` space.

Default values:

- `cl_playerHighlightRedColor "208 96 96"`
- `cl_playerHighlightBlueColor "96 144 224"`
- `cl_playerHighlightFreeColor "208 96 96"`

The same base color drives both passes. The rimlight and outline use different internal alpha strengths so they read as separate effects instead of the same shell drawn twice.

Overall pass intensity:

- `cl_playerHighlightRimIntensity`: Multiplies rimlight opacity.
- `cl_playerHighlightOutlineIntensity`: Multiplies stencil border opacity.

Defaults:

- `cl_playerHighlightRimIntensity "1.0"`
- `cl_playerHighlightOutlineIntensity "1.0"`

Use `0` to suppress that pass without changing the `cl_playerHighlight` bitmask. Values above `1` strengthen the pass until the final alpha reaches full opacity.

## Enemy And Teammate Overrides

If you want relationship-based coloring instead of fixed red/blue team colors, use the override cvars:

- `cl_playerHighlightEnemyColor`
- `cl_playerHighlightTeammateColor`

These are blank by default. When blank, FnQL uses the base red, blue, or free color cvars.

Override behavior:

- In team modes, `cl_playerHighlightEnemyColor` replaces the color on players who are not on your team.
- In team modes, `cl_playerHighlightTeammateColor` replaces the color on players who are on your team.
- In non-team modes, `cl_playerHighlightEnemyColor` replaces `cl_playerHighlightFreeColor`.
- In non-team modes, `cl_playerHighlightTeammateColor` is unused.

## Outline Thickness

- `cl_playerHighlightOutlineScale`: Controls stencil shell thickness.

Default:

- `cl_playerHighlightOutlineScale "1.01"`

Keep this close to `1.0` unless you intentionally want a thicker border. Small changes are usually enough.

## Examples

Use both effects with default team colors:

```cfg
seta cl_playerHighlight "3"
```

Use a single enemy override color in team games:

```cfg
seta cl_playerHighlight "3"
seta cl_playerHighlightEnemyColor "255 128 96"
seta cl_playerHighlightTeammateColor ""
```

Use flat relationship colors in team games:

```cfg
seta cl_playerHighlight "2"
seta cl_playerHighlightEnemyColor "224 96 96"
seta cl_playerHighlightTeammateColor "96 208 160"
```

Use a custom free-for-all color:

```cfg
seta cl_playerHighlight "1"
seta cl_playerHighlightFreeColor "224 112 112"
seta cl_playerHighlightEnemyColor ""
```

## Legacy Names

The old `cl_enemyHighlight*` names have been replaced by `cl_playerHighlight*`.

- Existing archived `cl_enemyHighlight*` values are migrated forward when the new cvars are still at default values.
- Use the `cl_playerHighlight*` names for new configs and documentation.

## Related Guides

- [Display Guide](DISPLAY.md) for renderer display effects such as cel shading, HDR, bloom, and render scaling.
- [Aspect Handling Guide](ASPECT_CORRECTION.md) for menu and cinematic layout.
- [Console Guide](CONSOLE.md) for console scaling, interaction, and appearance.
