# Trailer (60–90 seconds)

A short video does more than any post. FnQL can capture its own footage
without ever showing a window, using `com_headless 2` and FFmpeg video
piping (see `docs/SCREENSHOTS.md`).

## Recipe

1. **Record real play.** Record a demo in a normal session (`record
   trailer01`), ideally a lively FFA or CA match on a public server or with
   bots. Several 2–3 minute demos give the most choice.
2. **Render the demo headlessly** at 1920×1080, 60 fps, with the promo
   renderer settings (the same ones as the screenshots, listed in
   `images/README.md`). `video-pipe` runs `ffmpeg` from your `PATH` and
   only works during demo playback, so start it from a small cfg once the
   demo is running. Save this as `baseq3/trailer.cfg`:

   ```
   set cl_aviFrameRate 60
   set cl_aviPipeFormat "-preset slow -crf 16 -c:v libx264 -pix_fmt yuv420p -c:a aac -b:a 192k -movflags faststart"
   demo trailer01
   wait 120
   video-pipe trailer01
   ```

   and run:

   ```
   fnql.exe +set com_headless 2 +set r_mode -1 +set r_customWidth 1920 +set r_customHeight 1080 +set cl_renderer vk +set r_fbo 1 +set r_hdr 1 +set r_bloom 1 +set r_tonemap 2 +set r_ext_multisample 8 +set cg_draw2D 0 +exec trailer.cfg
   ```

   The file lands in `baseq3/videos/`. This recipe follows the `video-pipe`
   code (`code/client/cl_avi.cpp`) but hasn't been run end to end yet; try a
   short demo first. Drop `cg_draw2D 0` for shots that should show the HUD.
3. **Cut** in any editor. Keep shots to 2–4 seconds and cut on the beat.

## Shot list

| # | Time | Shot | On-screen text |
| --- | --- | --- | --- |
| 1 | 0:00–0:04 | Black, fn emblem animates in | — |
| 2 | 0:04–0:10 | Slow fly-through of Cure (GLx, HDR bloom on the windows) | **The Quake Live you own.** |
| 3 | 0:10–0:16 | Fast gameplay on Thunderstruck: rocket jumps, explosions | **Rebuilt engine.** |
| 4 | 0:16–0:22 | Hidden Fortress, Japanese Castles: architecture under Vulkan | **GLx · Vulkan · HDR · MSAA** |
| 5 | 0:22–0:30 | Gameplay with player highlighting on | **See every player** |
| 6 | 0:30–0:38 | Server browser → join a retail server → in game | **Joins retail servers. Hosts retail players.** |
| 7 | 0:38–0:46 | Console: completion, mouse selection, smooth scroll | **A console from this decade** |
| 8 | 0:46–0:54 | Demo playback of a `.dm_91` | **Your demos still play** |
| 9 | 0:54–1:04 | Montage of frags, cut on the beat | — |
| 10 | 1:04–1:12 | Logo lockup + URL | **FnQL · free & open source · github.com/themuffinator/FnQL** |
| 11 | 1:12–1:15 | Trademark card | *Not affiliated with id Software, Bethesda, ZeniMax or Valve. Requires Quake Live on Steam.* |

Shot 6 needs a session that shows the Steam server browser, so it has to be
recorded in a normal, windowed session. Record it yourself, since automated
runs stay windowless.

**Music:** use a track you have rights to, or a royalty-free one with a
licence that covers YouTube. Don't use Quake Live's soundtrack.

## YouTube description

```
FnQL is an open-source engine for the Steam release of Quake Live. It joins retail servers, hosts retail players, plays your demos and keeps the WebUI and Workshop content working, then adds GLx and Vulkan renderers, HDR, raw input with Quake Live's mouse math, HRTF audio and a modern console.

Download: https://github.com/themuffinator/FnQL/releases/latest
Source (GPL-2.0): https://github.com/themuffinator/FnQL
Game modules: https://github.com/themuffinator/FnQL-game

Requires your own copy of Quake Live on Steam. FnQL ships no game content.

All footage rendered by FnQL itself.

FnQL is an independent project and is not affiliated with, endorsed by, or sponsored by id Software, Bethesda, ZeniMax Media, or Valve. Quake and Quake Live are trademarks of their respective owners.

Music: <credit>
```

**Thumbnail:** `images/banners/fnql-youtube-thumb-1280x720.jpg`.
