# Steam Guide — "Installing and tuning FnQL"

**Where:** Quake Live Community Hub → Guides → Create Guide. Guides stay
discoverable far longer than discussions, rank in Steam search, and allow
images.

**Guide settings:**

| Field | Value |
| --- | --- |
| Title | FnQL: install the open-source Quake Live engine and tune it |
| Category | Multiplayer, Settings, Modding |
| Language | English |
| Guide icon | `images/logo/fnql-icon-256.png` |
| Description | Install FnQL into your Steam copy of Quake Live in two minutes, then set up Vulkan/HDR, raw input, HRTF audio and player highlighting. |

Steam guides are built from sections in the guide editor. Each `##` below
is one section; paste its body in Steam BBCode. Upload the listed image
into each section through the editor's image tool.

---

## Section 1: What FnQL is

*Image: `images/keyart/fnql-keyart-1920x1080.jpg`*

```
FnQL is an open-source replacement engine for the Steam release of Quake Live. It keeps retail compatibility: you join the same servers, retail players join yours, and your demos, maps, profile and Workshop content all keep working. On top of that it adds GLx and Vulkan renderers, HDR, raw input with Quake Live's mouse math, HRTF audio and a modern console.

FnQL is unofficial and not affiliated with id Software, Bethesda, ZeniMax or Valve. You need your own copy of Quake Live; FnQL ships no game content.
```

## Section 2: Install (Windows)

```
[olist]
[*]Launch retail Quake Live once, then quit. This creates your normal Steam files and profile.
[*]Download the [b]windows-msvc-x86.zip[/b] package from [url=https://github.com/themuffinator/FnQL/releases/latest]the latest FnQL release[/url].
[*]In your Steam library, right-click Quake Live → Manage → Browse local files.
[*]Extract the zip into that folder. Keep everything from the zip together beside fnql.exe.
[*]Leave Steam running and start fnql.exe. A desktop shortcut to it is handy.
[/olist]

Nothing in your retail install is replaced: quakelive_steam.exe still works exactly as before.
```

## Section 3: Pick a renderer

*Image: one GLx and one Vulkan shot of the same map side by side, e.g.
`images/screenshots/fnql-cure-glx.jpg` and `fnql-cure-vk.jpg`*

```
Open the console with ~ and enter one of:

[code]
seta cl_renderer "glx"
vid_restart
[/code]

[code]
seta cl_renderer "vk"
vid_restart
[/code]

GLx is the modern OpenGL path; Vulkan is the Vulkan raster renderer. Try both on your GPU and keep whichever runs better. For the full list of display options see the [url=https://github.com/themuffinator/FnQL/blob/main/docs/DISPLAY.md]Display Guide[/url].
```

## Section 4: A good-looking starting point

```
These settings are what the promo screenshots use. All of them are optional; drop any that cost you frames.

[code]
seta r_fbo "1"
seta r_hdr "1"
seta r_bloom "1"
seta r_tonemap "2"
seta r_ext_multisample "8"
seta r_ext_max_anisotropy "16"
vid_restart
[/code]

Lower r_ext_multisample to 4 or 2 on slower GPUs.
```

## Section 5: Mouse

```
FnQL's raw input uses Quake Live's own CPI scaling and acceleration, so your existing sensitivity settings carry over. The details, and the classic Quake3e/ioquake3 mouse styles if you prefer them, are in the [url=https://github.com/themuffinator/FnQL/blob/main/docs/fnql/INPUT_COMPATIBILITY.md]Input Compatibility Notes[/url].
```

## Section 6: See players better

*Image: `images/features/fnql-feature-highlight-modes.jpg`*

```
[code]
seta cl_playerHighlight "3"
seta cl_playerHighlightEnemyColor "255 32 0"
seta cl_playerHighlightTeammateColor "0 160 255"
[/code]

1 is rim light only, 2 is outline only, 3 is both, 0 turns it off. More in the [url=https://github.com/themuffinator/FnQL/blob/main/docs/VISUALS.md]Visuals Guide[/url].
```

## Section 7: Optional looks: shadows and cel shading

*Images: `images/features/fnql-feature-sun-shadows-action.jpg` and
`images/features/fnql-feature-celshading.jpg`*

```
Sun shadows from each map's own sky (players cast them too). Needs vid_restart:

[code]
seta r_csmShadows "1"
vid_restart
[/code]

Dynamic lights from weapons and projectiles cast shadow maps by default on GLx and Vulkan. For sharper ones:

[code]
seta r_dlightShadowResolution "1024"
seta r_dlightShadowMaxLights "2"
vid_restart
[/code]

Cel shading, for a stylised look:

[code]
seta r_celShading "1"
seta r_celShadingWorld "1"
[/code]

All of these are optional. The [url=https://github.com/themuffinator/FnQL/blob/main/docs/DISPLAY.md]Display Guide[/url] lists every setting.
```

## Section 8: Headphones? Turn on HRTF

```
FnQL's OpenAL backend supports HRTF headphone positioning, surround, reverb and occlusion. Setup and troubleshooting are in the [url=https://github.com/themuffinator/FnQL/blob/main/docs/AUDIO.md]Audio Guide[/url].
```

## Section 9: Going back to retail modules

```
FnQL runs the open-source FnQL-game modules by default. To use the retail ones instead:

[code]
seta vm_cgameProvider "retail"
seta vm_uiProvider "retail"
[/code]

On retail servers FnQL already loads the retail client modules automatically.
```

## Section 10: Problems?

```
[list]
[*]Run fnql.exe with Steam already running.
[*]Check the console for the first error message.
[*]Report bugs with steps and your console log on [url=https://github.com/themuffinator/FnQL/issues]GitHub[/url].
[/list]
```

---

## Before publishing

- Check each cvar in sections 4 and 6 against the current docs. The values
  above match release 0.1.0.158.
