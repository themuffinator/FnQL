# Promo images

All screenshots were rendered by FnQL itself on 2026-11-17, using the
windowless client (`com_headless 2`) on a legitimate Steam install of
Quake Live. Nothing was painted over or retouched; key art and banners add
only the FnQL logo, text and red accent stripes.

## Screenshots (`screenshots/`)

| File | Size | Renderer | Shows |
| --- | --- | --- | --- |
| `fnql-hiddenfortress-rail.jpg` | 1920×1080 | Vulkan | Hidden Fortress: a railgun shot cuts across the atrium (**hero**) |
| `fnql-thunderstruck-action.jpg` | 1720×968 | Vulkan | Thunderstruck: rockets and rails in the courtyard |
| `fnql-highlight.jpg` | 1720×968 | Vulkan | Player highlighting (`cl_playerHighlight 3`) on every enemy |
| `fnql-cure-glx.jpg` | 1920×1080 | GLx | Cure: HDR bloom through the hall windows |
| `fnql-cure-vk.jpg` | 1920×1080 | Vulkan | Cure, same camera, for renderer comparison |
| `fnql-overkill-glx.jpg` | 1920×1080 | GLx | Overkill: candlelit gothic arena |
| `fnql-japanesecastles-vk.jpg` | 1920×1080 | Vulkan | Japanese Castles: the dojo hall |
| `fnql-lostworld-vk.jpg` | 1920×1080 | Vulkan | Lost World: a rocket lights the lava pit |
| `fnql-furiousheights-vk.jpg` | 1920×1080 | Vulkan | Furious Heights |
| `fnql-elder-vk.jpg` | 1540×866 | Vulkan | Elder: tentacles and a waiting railgun |
| `fnql-campgrounds-glx.jpg` | 1920×1080 | GLx | Campgrounds: the arena stage |
| `fnql-almostlost-glx.jpg` | 1920×1080 | GLx | Almost Lost |

`fnql-gallery-overview.jpg` is a contact sheet of all twelve.

Some maps show the Quake Live logo on in-world banners. That's retail map
content and fine in screenshots. Per `../MESSAGING.md`, the key art and
banners don't use it.

## Feature showcase (`features/`)

Before/after comparisons of FnQL's optional visuals. Each pair is one moment
of play, captured again with only the feature's cvars changed (the game is
slowed to `timescale 0.1` and the variants are shot a few frames apart), so
poses and positions match. Latched features (sun shadows, HDR) come from two
runs of a still camera with no bots.

| File | Size | Feature | How it was shot |
| --- | --- | --- | --- |
| `fnql-feature-highlight-modes.jpg` | 1920×645 | Player highlighting: off, rim, outline, both, custom colour | Thunderstruck, Vulkan, `cl_playerHighlight 0/1/2/3`, `cl_playerHighlightEnemyColor "0 210 255"` |
| `fnql-feature-highlight-split.jpg` | 1920×1200 | Player highlighting, in play | Same match, split through the nearest player |
| `fnql-feature-highlight-team.jpg` | 1200×753 | Team-coloured highlighting | Thunderstruck TDM, blue team, `cl_playerHighlight 3` |
| `fnql-feature-celshading.jpg` | 1920×1200 | Cel shading: banded model light, model outlines, world edge lines | `r_celShading 1`, `r_celShadingWorld 1`, `r_celShadingSteps 3`, `r_celOutlineScale 1.05`, `r_celShadingWorldWidth 3.5`, `r_celShadingWorldDepthThreshold 0.0008` |
| `fnql-feature-celshading-closeup.jpg` | 1200×753 | Cel shading close-up | Same settings and frame |
| `fnql-feature-dlight-shadows.jpg` | 1920×806 | Shadow-mapped dynamic lights | Campgrounds, two **`r_dlightTest`** diagnostic lights, `r_dlightShadowResolution 1024`, `r_dlightShadowMaxLights 2`; off = `r_dlightShadowStrength 0` |
| `fnql-feature-sun-shadows.jpg` | 1920×1200 | Sun shadows (cascaded shadow maps) | Thunderstruck, `r_csmShadows 0` vs `1`, `r_csmResolution 2048` |
| `fnql-feature-sun-shadows-action.jpg` | 1920×1200 | Sun shadows in play: players cast shadows | Thunderstruck with bots, `r_csmShadows 1` |
| `fnql-feature-hdr.jpg` | 1920×1200 | HDR rendering and bloom | Cure, GLx, `r_hdr 0` + `r_bloom 0` vs the defaults |

Captioning rules for these:

- The dynamic-light shadow image uses **test lights** from the `r_dlightTest`
  diagnostic, not weapon fire. Always say so; the caption bar already does.
  In ordinary play, weapon and projectile lights are small and brief, and
  combat captures didn't show their shadows clearly enough to use.
- The cel-shading settings are stronger than the defaults, so the look reads
  at thumbnail size. The caption lists them.
- Sun shadows are off by default and tint with each map's sun colour (green
  on Thunderstruck).
- Enhanced liquids aren't shown: from the retail maps' cameras the effect was
  too faint to make a convincing comparison. Lens flares aren't shown because
  no retail map contains flare surfaces.

## Key art and banners

| File | Size | Use |
| --- | --- | --- |
| `keyart/fnql-keyart-1920x1080.jpg` | 1920×1080 | Hero image: ModDB article header, Reddit, press kit, Steam guide |
| `keyart/fnql-keyart-1280x720.jpg` | 1280×720 | Smaller hero for social posts |
| `banners/fnql-moddb-header-1200x300.jpg` | 1200×300 | ModDB page header |
| `banners/fnql-x-header-1500x500.jpg` | 1500×500 | X / Bluesky / Mastodon profile header |
| `banners/fnql-og-1200x630.jpg` | 1200×630 | Link-preview image (Open Graph) for a website or press kit |
| `banners/fnql-discord-banner-960x540.jpg` | 960×540 | Discord server banner / invite splash |
| `banners/fnql-steam-guide-1024x512.jpg` | 1024×512 | Steam Guide header |
| `banners/fnql-youtube-thumb-1280x720.jpg` | 1280×720 | Trailer thumbnail |

Platforms change their recommended sizes from time to time; check the
upload form, and recrop from the 1920×1080 key art if needed.

## Logo (`logo/`)

Copies of the committed branding in `misc/branding/` and `.github/`, renamed
for promo use: `fnql-logo-1332.png` (transparent), `fnql-logo.svg`,
`fnql-icon.svg`, `fnql-icon-256/512/1024.png`, and
`fnql-social-preview-1280x640.png`. Edit the originals, not these copies.

## How the screenshots were made

`../tools/capture_shots.py` reproduces the captures, and
`../tools/compose_art.py` rebuilds the key art and banners from them. For
example, the action pass was:

```bash
python promo/tools/capture_shots.py --client meson/build-x86-nosdl/fnql.exe --retail "<Quake Live folder>" --renderer vk --maps thunderstruck,hiddenfortress --bots 7 --shots 10 --gap 110 --tag act
```

Add `--highlight` for player highlighting and `--renderer glx` for GLx.
Output goes to `.tmp/promo-capture/`. A feature comparison adds
`--variants`, for example:

```bash
python promo/tools/capture_shots.py --client meson/build-x86-nosdl/fnql.exe --retail "<Quake Live folder>" --maps thunderstruck --bots 7 --shots 12 --gap 90 --fov 60 --pitch-offset 4 --tag hl --variants "off:cl_playerHighlight=0|rim:cl_playerHighlight=1|outline:cl_playerHighlight=2|both:cl_playerHighlight=3"
```

`../tools/compose_compare.py` turns the variant frames into the `split`,
`panels` and `card` layouts used in `features/`. The method:

- **Engine:** FnQL dev build (`win_msvc-x86`, 15 Nov 2026), from the source
  that became release 0.1.0.158. The renderer code is the same as the
  release's.
- **Launch:** `fnql.exe +set com_headless 2 +set fs_basepath <QL> +set
  fs_steampath <QL> +set fs_homepath <scratch>` with Steam and the WebUI at
  their defaults.
- **Render settings:** 1920×1080, `cg_fov 90`, `r_fbo 1`, `r_hdr 1`,
  `r_bloom 1`, `r_bloom_soft_knee 0.5`, `r_tonemap 2`, `r_ext_multisample 8`,
  `r_ext_max_anisotropy 16`, `r_picmip 0`, `r_lodbias -2`,
  `r_subdivisions 1`, `r_dlightMode 2`, `cg_draw2D 0`, `cg_drawGun 0`.
- **Cameras:** each map's own `info_player_intermission` (its designed
  overview shot), reached with `devmap <map> ffa`, `noclip`, `setviewpos`
  and a pitch adjustment.
- **Action:** seven bots (`addbot`, `sv_maxclients 8`) with the camera in
  `god` mode, and bursts of screenshots about a second apart.
- **Highlight shot:** `cl_playerHighlight 3`,
  `cl_playerHighlightRimIntensity 2.0`,
  `cl_playerHighlightOutlineIntensity 2.0`, set in game.
- Keep the launch line to 32 or fewer `+` commands. The engine silently
  drops any after the 32nd (`MAX_CONSOLE_LINES`), so put extra cvars in an
  exec'd cfg.

## Rights

FnQL artwork © the FnQL project. In-game content in the screenshots is from
Quake Live, © id Software / ZeniMax Media, shown to demonstrate FnQL
running the retail game.
