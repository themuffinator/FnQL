# Screenshot Guide

FnQL keeps the classic screenshot commands, then extends them into a more flexible capture system for organized shot libraries, reproducible camera setups, and cube-map extraction. This guide covers the screenshot commands, naming rules, metadata sidecars, watermark options, and cube-map capture behavior.

All screenshot output is written under the active game directory in `screenshots/`.

## Color Capture Policy

Screenshots, levelshots, cube-map faces, clipboard BMP captures, and AVI frame capture default to SDR sRGB byte output. This remains true even when the renderer is using the scene-linear HDR pipeline internally: the final output transform is captured after tone mapping so existing PNG, TGA, JPG, BMP, baselines, and scripts keep their historical expectations.

`r_screenshotCaptureMode` records the explicit capture policy:

- `0`: SDR sRGB byte capture. This is the default.
- `1`: Reserved scene-linear HDR export request. It currently falls back to SDR sRGB byte output and prints a warning; use it only when a capture run needs to prove that an HDR-aware request was made explicitly.
- `2`: Reserved HDR-output export request. It currently falls back to SDR sRGB byte output and prints a warning; it is a forward-compatible slot for future output-encoded HDR screenshots.

GLx diagnostics report both the requested policy and the selected capture policy. Until float/HDR image export is implemented, HDR-aware requests are recorded as explicit but unsupported and the selected capture remains `sdr-srgb`.

When `r_screenshotWriteViewpos 1` is enabled, sidecars include `captureMode` and `captureColorSpace` lines so proof artifacts identify the capture policy without changing the image format.

## Overview

The standard commands remain familiar:

- `screenshot`: Save a PNG screenshot.
- `screenshotPNG`: Explicit PNG screenshot command.
- `screenshotTGA`: Save a TGA screenshot.
- `screenshotJPEG`: Save a JPG screenshot.
- `screenshotBMP`: Save a BMP screenshot.
- `screenshot levelshot`: Save a levelshot to `levelshots/<map>.tga` using the current levelshot sizing and crop settings.
- `screenshot cubemap`: Save a six-face PNG cube map on the OpenGL-lineage and Vulkan renderers.
- `screenshotPNG cubemap`: Explicit PNG cube-map command.
- `screenshotTGA cubemap`: Save a six-face TGA cube map.
- `screenshotJPEG cubemap`: Save a six-face JPG cube map.
- `screenshotBMP cubemap`: Save a six-face BMP cube map.
- `screenshot silent`: Save a screenshot without the normal confirmation message.
- `screenshot <name>`: Save to `screenshots/<name>.<ext>` instead of using the automatic naming pattern.
- `screenshotBMP clipboard`: Copy a BMP screenshot to the system clipboard.
- `screenshot cubemap silent`: Save the cube map without the normal confirmation message.
- `screenshot cubemap <basename>`: Save faces as `screenshots/<basename>-front.<ext>` and so on.

Cube-map face names are `front`, `back`, `left`, `right`, `top`, and `bottom`.

## Levelshots

`screenshot levelshot` still writes a map preview image under `levelshots/`, but it is no longer locked to a fixed `128x128` thumbnail.

Default behavior:

- The source is the current captured viewport.
- The output keeps that resolved source size by default.
- The file path remains `levelshots/<map>.tga`.

Levelshot control cvars:

- `r_levelshotSize`: Explicit output size. Leave it blank to keep the resolved source size. Use a single integer such as `512` for a square image, or use `WxH` such as `1024x512` for an explicit rectangle.
- `r_levelshotDownscale`: Downscale factor used only when `r_levelshotSize` is blank. `1` keeps full size, `2` halves the output dimensions, `4` quarters them, and so on.
- `r_levelshotSourceAspect`: Optional centered source crop aspect. Examples: `4:3`, `16:9`, `1:1`. Leave it blank to use the full viewport.
- `r_levelshotHideHud`: Hide the HUD during levelshot and cube-map capture frames. `1` hides it, `0` keeps it.
- `r_levelshotHideViewWeapon`: Hide first-person weapon models during levelshot and cube-map capture frames. `1` hides them, `0` keeps them.

This gives you three useful modes without changing the command itself:

- Full viewport export: leave all levelshot cvars at default values.
- Centered aspect crop: set `r_levelshotSourceAspect` and leave the size blank.
- Squashed or reshaped output: set `r_levelshotSize` to a square or custom rectangle.

Capture visibility rules:

- Levelshots are rendered on a dedicated capture frame so the hide settings above can take effect reliably.
- If `r_levelshotHideHud 0`, the levelshot includes the current HUD.
- If `r_levelshotHideViewWeapon 0`, the levelshot includes the current first-person weapon model.

Examples:

Keep the full viewport at its current capture size:

```cfg
seta r_levelshotSourceAspect ""
seta r_levelshotSize ""
seta r_levelshotDownscale "1"
screenshot levelshot
```

Crop a centered `4:3` block and keep it at native cropped size:

```cfg
seta r_levelshotSourceAspect "4:3"
seta r_levelshotSize ""
seta r_levelshotDownscale "1"
screenshot levelshot
```

Crop a centered `4:3` block and downscale it by half:

```cfg
seta r_levelshotSourceAspect "4:3"
seta r_levelshotSize ""
seta r_levelshotDownscale "2"
screenshot levelshot
```

Squash the full viewport into a square image:

```cfg
seta r_levelshotSourceAspect ""
seta r_levelshotSize "512"
screenshot levelshot
```

Crop a centered square block and keep it square:

```cfg
seta r_levelshotSourceAspect "1:1"
seta r_levelshotSize "512"
screenshot levelshot
```

## Automatic Naming

When you do not pass an explicit filename, FnQL builds one from `r_screenshotNameFormat`.

- `r_screenshotNameFormat "shot-{date}-{time}"`: Default pattern.
- File extensions still come from the command you used, so the pattern only controls the base name.
- Tokens are sanitized automatically so map names and command names stay filesystem-safe.
- If a generated name already exists, FnQL appends an iterator automatically unless your pattern already uses one.

Supported tokens:

- `{map}`: Current map name, sanitized for filenames.
- `{date}`: Current local date as `YYYYMMDD`.
- `{time}`: Current local time as `HHMMSS`.
- `{datetime}`: Combined local date and time as `YYYYMMDD-HHMMSS`.
- `{iter}`: Numeric collision iterator starting at `0`.
- `{iter:4}`: Iterator padded to a fixed width, such as `0007`.
- `{cmd}`: The capture command name, such as `screenshot`, `screenshotJPEG`, or `screenshot-cubemap`.
- `{face}`: Cube-map face name when a cube-map capture is generating the file.
- `{type}`: Output type, such as `tga`, `jpg`, or `bmp`.

Examples:

```cfg
seta r_screenshotNameFormat "shot-{map}-{datetime}"
```

```cfg
seta r_screenshotNameFormat "{map}-{cmd}-{iter:4}"
```

```cfg
seta r_screenshotNameFormat "{map}-{datetime}-{face}"
```

If a cube-map pattern does not include `{face}`, FnQL appends the face name automatically so the six outputs stay distinct.

## View Metadata Sidecars

Set `r_screenshotWriteViewpos 1` to make FnQL write a plain-text sidecar next to each saved screenshot.

- The sidecar uses the same base filename as the image and ends in `.txt`.
- Each file stores the current map name, view origin, view angles, and a ready-to-paste `setviewpos` command.
- This works for normal screenshots and for each cube-map face image.

Example output:

```text
map q3dm17
origin 128.000 -64.000 512.000
angles -10.000 90.000 0.000
captureMode sdr-srgb
captureColorSpace sdr-srgb
setviewpos 128.000 -64.000 512.000 -10.000 90.000 0.000
```

This is useful when you want to recreate a shot later, compare captures across builds, or keep precise camera notes alongside exported images.

## Watermarks

FnQL can composite an image watermark directly into saved screenshots.

- `r_screenshotWatermark`: Path to the watermark image. Supported formats are `png`, `tga`, `jpg`, `jpeg`, and `bmp`.
- `r_screenshotWatermarkAlignment`: Alignment of the watermark inside the 4:3 safe area.
- `r_screenshotWatermarkScreenAlignment`: Alignment of that 4:3 safe area inside the full screenshot.
- `r_screenshotWatermarkMargin`: Margin in pixels from the aligned edges.

Supported alignment values for both alignment cvars:

- `top-left`
- `top`
- `top-right`
- `left`
- `center`
- `right`
- `bottom-left`
- `bottom`
- `bottom-right`

Behavior notes:

- The watermark image is not rescaled by the engine. FnQL draws it at its source size.
- Placement is computed against a 4:3 safe area first, then that safe area is placed inside the actual screenshot using `r_screenshotWatermarkScreenAlignment`.
- This makes it practical to keep watermark placement stable across 4:3 and widescreen captures.
- If the watermark image cannot be loaded, the screenshot is still saved and FnQL prints a warning.
- Watermarks are disabled for cube-map capture so every face remains a clean 1:1 tile.

Example:

```cfg
seta r_screenshotWatermark "gfx/watermarks/fnql-mark.png"
seta r_screenshotWatermarkAlignment "bottom-right"
seta r_screenshotWatermarkScreenAlignment "center"
seta r_screenshotWatermarkMargin "24"
```

## Cube Maps

The `glx` and `vk` renderers can capture a six-face cube map from the current camera position through the `screenshot ... cubemap` subcommand.

- Each face is saved as a square image.
- The renderer captures the five non-front faces from one frozen scene state inside the same scene submission.
- The front face is taken from that same capture frame after the normal front view finishes drawing.
- That keeps world time, entity state, and shader animation state aligned across the set instead of advancing between faces.
- Every face uses an exact square 90-degree projection and the same camera-relative face orientation on GLx and Vulkan.
- Faces receive the SDR output transform and color grading, while view-dependent screen effects such as bloom, CRT distortion, and camera motion blur are excluded to keep cube edges consistent.
- Cube-map watermarks are always disabled.
- If `r_levelshotHideHud 0`, the HUD appears only on the `front` face.
- If `r_levelshotHideViewWeapon 0`, the first-person weapon appears only on the `front` face.
- Vulkan batches all six GPU face copies into one host readback after the frame instead of stalling once per face.

Example:

```cfg
screenshot cubemap env/q3dm1-sky
```

That produces:

- `screenshots/env/q3dm1-sky-front.png`
- `screenshots/env/q3dm1-sky-back.png`
- `screenshots/env/q3dm1-sky-left.png`
- `screenshots/env/q3dm1-sky-right.png`
- `screenshots/env/q3dm1-sky-top.png`
- `screenshots/env/q3dm1-sky-bottom.png`

Renderer status:

- GLx renderer: full cube-map capture support.
- Vulkan renderer: full cube-map capture support with the same face names, orientation, visibility controls, and SDR output contract.

## Recommended Starting Points

For an organized screenshot archive:

```cfg
seta r_screenshotNameFormat "{map}-{datetime}-{iter:3}"
seta r_screenshotWriteViewpos "1"
```

For a capture setup with a logo watermark:

```cfg
seta r_screenshotNameFormat "{map}-{datetime}"
seta r_screenshotWatermark "gfx/watermarks/fnql-mark.png"
seta r_screenshotWatermarkAlignment "bottom-right"
seta r_screenshotWatermarkScreenAlignment "center"
seta r_screenshotWatermarkMargin "16"
```

For cube-map export on an OpenGL-lineage renderer:

```cfg
seta r_screenshotNameFormat "{map}-{datetime}-{face}"
screenshot cubemap
```
