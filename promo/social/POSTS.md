# Short-form social posts

For Bluesky, X and Mastodon. Each post fits in 280 characters with its link.
Attach one image per post from `images/`; alt text is given for each one.
Use hashtags sparingly: `#QuakeLive` and `#Quake` are enough.

## Launch thread (4 posts)

**1/4** · image: `images/keyart/fnql-keyart-1920x1080.jpg`

> FnQL 0.1.0.158 is out: an open-source engine for the Quake Live you
> already own on Steam. Joins retail servers, hosts retail players, plays
> your demos, and adds GLx/Vulkan, HDR, raw input and HRTF audio.
> #QuakeLive
> https://github.com/themuffinator/FnQL/releases/latest

Alt: FnQL logo over a Quake Live arena rendered by FnQL.

**2/4** · image: `images/screenshots/fnql-cure-glx.jpg`

> Same game, same servers, better engine. GLx and Vulkan renderers with HDR,
> MSAA/supersampling, bloom and tone mapping, all optional. Shot on the GLx
> renderer.

Alt: Interior of the map Cure, warm window light blooming through a
timber-framed hall.

**3/4** · image: `images/screenshots/fnql-thunderstruck-action.jpg`

> New: FnQL now runs its own open-source game modules by default, and retail
> interop still works both ways. Don't want them? One cvar per module
> switches back to retail's.

Alt: Players trade rockets and a railgun beam in the open courtyard of
Thunderstruck.

**4/4** · no image

> Built on id's GPL Quake III source via ioquake3, Quake3e and FnQ3.
> GPL-2.0. Bug reports welcome.
> Source: https://github.com/themuffinator/FnQL
> Not affiliated with id Software, Bethesda or Valve.

## Feature spotlight posts (one every two weeks)

**Renderers**

> Which FnQL renderer suits your GPU? `cl_renderer glx` or `cl_renderer vk`,
> then `vid_restart`. Both do HDR, MSAA and supersampling.
> Display guide: https://github.com/themuffinator/FnQL/blob/main/docs/DISPLAY.md
> #QuakeLive

**Mouse**

> FnQL's raw input uses Quake Live's own CPI scaling and acceleration math, so
> your sens carries over. Prefer Quake3e/ioq3 mouse styles? They're still
> there.
> https://github.com/themuffinator/FnQL/blob/main/docs/fnql/INPUT_COMPATIBILITY.md

**Player highlighting** · image: `images/features/fnql-feature-highlight-modes.jpg`

> Same frame, five looks: `cl_playerHighlight` 0 / 1 rim light / 2 outline /
> 3 both, plus your own colour. Team modes highlight red and blue in their
> own colours.
> https://github.com/themuffinator/FnQL/blob/main/docs/VISUALS.md

Alt: Five crops of the same running player: unhighlighted, orange rim light,
orange outline, both, and a cyan custom colour.

**Cel shading** · image: `images/features/fnql-feature-celshading.jpg`

> Fancy a comic-book Quake Live? `r_celShading 1` bands model lighting and
> outlines players; `r_celShadingWorld 1` inks the map's edges. Off by
> default, entirely optional.
> https://github.com/themuffinator/FnQL/blob/main/docs/DISPLAY.md#cel-shading

Alt: Thunderstruck split down the middle: normal on the left, cel-shaded
with black outlines on the right.

**Dynamic-light shadows** · image: `images/features/fnql-feature-dlight-shadows.jpg`

> FnQL's dynamic lights cast shadow-mapped shadows on GLx and Vulkan. Here,
> two test lights throw the stage trusses' shadows across Campgrounds.
> https://github.com/themuffinator/FnQL/blob/main/docs/DISPLAY.md#dynamic-lighting-and-shadowing

Alt: Campgrounds stage lit by red and blue test lights, without and with
shadow maps; the right panel shows the truss shadows on the wall.

**Sun shadows** · image: `images/features/fnql-feature-sun-shadows-action.jpg`

> `r_csmShadows 1` adds sun shadows from each map's own sky, and players
> cast them too. Optional and off by default.

Alt: A player rocket-jumps over Thunderstruck's courtyard, their shadow
falling across the sunlit floor.

**HDR** · image: `images/features/fnql-feature-hdr.jpg`

> Same view, classic SDR vs FnQL's HDR pipeline with bloom. Light through
> Cure's windows finally glows.

Alt: Cure's hall split in two; the right half's windows glow with bloom.

**Audio**

> Headphones? FnQL's OpenAL backend does HRTF positioning, surround, reverb
> and geometry occlusion, and recovers live when you switch devices.
> https://github.com/themuffinator/FnQL/blob/main/docs/AUDIO.md

**Servers**

> Hosting QL? FnQL's dedicated server refuses hostile downloaded content,
> survives rcon `killserver`, and writes match stats as JSON lines with no
> libzmq. Retail clients join as normal.
> https://github.com/themuffinator/FnQL/releases/latest

**Capture**

> Every FnQL promo shot was taken with `+set com_headless 2`: a windowless
> client that still renders real frames. Screenshots at any size, cubemaps,
> FFmpeg video piping.
> https://github.com/themuffinator/FnQL/blob/main/docs/SCREENSHOTS.md

**Parity**

> How do you prove a rewritten Quake Live game module behaves like retail?
> FnQL-game runs it beside retail's in lockstep and requires zero divergence
> before a milestone closes.
> https://github.com/themuffinator/FnQL-game
