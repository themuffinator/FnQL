# Changelog

This is the pending release-note queue for the next FnQL release.

Keep short user-facing bullets under `Unreleased` as changes land. During release publishing, the workflow asks GitHub Copilot to dedupe and categorize the notes for the GitHub release details, then clears this section for the next cycle.

## [Unreleased]

### Highlights
- Startup now checks for a signed-in Steam account alongside the existing Steam provider and `steam_api` library checks, and offers a proceed, retry, or quit notification when none is found. Set `com_steamLoginPrompt 0` to continue silently instead.

### Compatibility
- Your player name now comes from your Steam profile like retail Quake Live, and
  your Steam account country is published alongside it. Rename through Steam and
  the change is picked up while you play. A local `\name` write is reverted before
  it reaches a server, since the retail UI has no rename control; set
  `\cl_steamNameLock 0` to manage `\name` yourself instead.

### Rendering and Display
- New optional underwater view. While the camera is under water, slime, or lava,
  the OpenGL-lineage, GLx, Vulkan, and RTX renderers can composite an animated
  screen warp with a slight colour separation, a darkened periphery, and a medium
  tint that deepens with distance. Set `r_underwater 1` and `vid_restart` to opt
  in; `r_underwaterWarp`, `r_underwaterDispersion`, `r_underwaterFog`, and
  `r_underwaterVignette` tune it live. Entering and leaving a liquid ramps the
  layer instead of popping, the HUD and console stay sharp, and nothing about it
  touches collision, movement, networking, game logic, or demo state.
- The Quake Live settings menu now carries FnQL's engine settings in the retail
  sections themselves instead of a separate FnQL tab. Video gained renderer and
  display selection plus framebuffer, texture and geometry, lighting and shadow,
  color and tone, bloom, scene effect, and cel shading groups; Team gained
  player highlighting beside the teammate and opponent rows; Input, Basic, Game,
  Weapons, and Gamepad gained the controls that belong with them.
- Retail settings rows now show your actual configuration. They read their
  values from the engine's settings snapshot, which previously carried only a
  fraction of the cvars they bind to, so most rows displayed defaults.
- Settings rows for Quake Live engine-managed cvars now apply. Mouse
  acceleration, mouse DPI, windowed resolution, console chat, recording message,
  and time nudge were being silently refused by the menu bridge.
- `com_maxfps` and `r_displayRefresh` are archived, so a frame cap or refresh
  rate chosen in the menu survives a restart.
- Retail rows FnQL does not implement are hidden instead of shown inert: the
  legacy post-processing column, the retail resolution rows, and the debug-only
  lightmap, fullbright, and ambient-scale rows.
- Fullscreen honours the resolution you pick again. `r_modeFullscreen`, `r_mode`,
  and `r_displayRefresh` were being discarded on every fullscreen switch, so
  fullscreen always came up at desktop resolution and refresh rate. A
  desktop-sized request still uses borderless desktop fullscreen, which needs no
  mode switch; anything else now takes an exclusive mode. The console reports the
  size, refresh rate, and which of the two you got.
- Windows builds now declare Per-Monitor V2 DPI awareness. The previous V1
  declaration blocked the awareness level SDL asks for and its window sizing
  assumes, which left window frames and system dialogs scaled for the wrong
  monitor on mixed-DPI desktops.
- Meson now rebuilds the Windows resource when the DPI manifest, icon,
  `resource.h`, or the version header changes. Neither resource compiler reports
  what the script pulls in, so those edits used to leave the previously compiled
  resource linked into the executable and appear to do nothing.
- Global fog reaches the screen at its authored brightness. The layer is
  composited into the scene colour buffer, which the output transform still
  multiplies by the overbright scale — and by the tone-map exposure in the
  scene-linear HDR mode — so an authored mid-grey arrived about twice as bright
  and read as a flat wash instead of distance fog. All three renderers now
  remove that scale before blending, and linearize the authored sRGB value in
  scene-linear mode. The 149 stock Quake Live fog sidecars are retuned against
  the corrected output: blend strength roughly doubles, and the largest arenas
  get a thicker falloff where the previous values were effectively invisible.

### Audio
- The settings menu now exposes the OpenAL backend, device, output mode, HRTF,
  mix frequency, voice counts, reverb, occlusion, air absorption, doppler, and
  audio-zone controls, plus the legacy mixer's sample rate and latency.
- The retail announcer volume, kill beep volume, mute-in-background, and ambient
  sound rows are hidden; FnQL's separate unfocused and minimized muting controls
  take their place.
- Environment reverb on the stock maps is rebuilt. Audio zones are now merged
  along the real openings between spaces instead of by comparing bounding boxes,
  so a zone matches a place in the map: 59 of the 149 maps previously ended up
  with a handful of level-sized boxes covering everything, and now only the
  genuinely open space maps do. Reverb also follows the size and shape of the
  space you are in rather than what the walls are made of, which had been
  labelling roughly two thirds of every map a stone room.
- Sounds coming from your own player are muffled under water like everything
  else. Held-weapon loops such as the gauntlet and lightning gun, and any other
  looping sound carried by your own entity, were being treated as UI audio and
  came through unfiltered while you were submerged. Announcer, menu, and hit
  feedback audio stays dry as before.
- Rooms next to water no longer sound submerged. The underwater environment was
  being applied to any space that merely bordered a water, slime, or lava brush,
  which affected around 40% of the volumes flagged as liquid.
- Transitions between spaces now blend over a distance that matches the size of
  the opening between them, so a wide archway bleeds the next room in gradually
  while a vent-sized gap only registers up close.
- Explosions and other weapon impacts are as loud as they should be again.
  Because they go off flush against the wall, floor, or corner they hit, the
  occlusion check was measuring them against the very surface they were resting
  on: part of the probe fan is buried in that surface by definition, and an
  impact origin that snapped a fraction of a unit inside it read as fully
  blocked. Rockets, grenades, and plasma landing in plain sight were losing
  several dB of direct level and being pushed into the reverb send, and in the
  worst case dropped to the fully occluded floor. Occlusion now samples from
  just clear of the impact surface and discards probes that land inside
  geometry, while sounds genuinely emitted inside a wall stay occluded.

### Builds and Packaging
- _None yet._

### Fixes
- Transparent surfaces behind an enhanced liquid (`r_liquid 1`) no longer vanish when you look through it. Fog volumes, items sorted to show through the water plane, and other transparent shaders drawn before the liquid were being erased by the refraction underlay, which replaces the background with a scene copy that was taken too early to contain them. The copy is now taken immediately before the first liquid surface, so that work is refracted along with the rest of the scene.
- Enhanced liquid refraction (`r_liquid`) no longer leaves a faint double image of the scene behind the surface. The warped view now replaces the background instead of being blended over the unwarped copy of it, and `r_liquidRefraction` scales how far the view bends.
- Liquid reflections use a proper Schlick falloff, so water is nearly clear when you look straight down at it and only turns mirror-like toward grazing angles, instead of carrying the same milky sheen from every direction.
- Enhanced liquid refraction now keeps its crisp waterline on the Vulkan renderer when soft particles are disabled (`r_depthFade 0`); the depth used to reject foreground samples was previously never captured in that configuration.
- Liquid ripple rings on the legacy OpenGL tier now expand with the same widening band the shader tiers draw, instead of staying at the impulse's original thickness.
- Sloped and vertical liquid surfaces now catch the wave shimmer at full strength; the highlight was being partly cancelled on anything that was not a flat pool.
- Escape no longer gets stuck on the browser overlay after the overlay has lost the surface it was drawing, so a menu you cannot see cannot keep swallowing the key.
- Detailed models that retail Quake Live loads no longer fail with "has more than 999 verts on a surface". The per-surface geometry budget was still Quake III's 1000 vertices rather than retail's 2000, which rejected stock content such as the heavy machine gun. All three renderers now accept every surface retail does.
- Leaving a game returns you to the Quake Live menu instead of the plain fallback screen. Ending a local match shuts the server down and drops straight to the disconnected state without ever running the disconnect path that resumes the menu, so the browser stayed paused and the engine brought up the basic native menu in its place. Every route back out of a game — quitting a local match, disconnecting from a server, an error drop, the end of a demo — now restores the real menu, and falls back to the native one only when the menu runtime genuinely is not available.
- In-game menus respond to the mouse again. The engine was handing the retail UI and cgame raw window coordinates, but both divide what they receive by the renderer resolution to reach their cursor space — and the UI throws the event away when the result lands outside it. So on a scaled desktop, or with any resolution that is not the window size, the menu cursor was either offset from your pointer or the menu ignored the mouse completely. Every absolute position is now converted once, the same way the console and the browser already were.
- Opening an in-game menu in fullscreen no longer lets the mouse wander onto another display, where a stray click would drop the game out of focus in the middle of the menu. The pointer is now held inside a fullscreen window while a menu or the console owns it; windowed menus still leave it free so the desktop stays reachable.
- An in-game menu no longer follows the desktop mouse while the game is in the background on the non-SDL Windows build, so alt-tabbing away and moving the mouse no longer leaves a different item highlighted when you come back.
- Dragging inside a menu now keeps working if the mouse leaves the window mid-drag on the default SDL build, matching the other platform backends; the release used to be lost and the control stayed stuck to the pointer.
- The console cursor no longer jitters between two positions when the console is opened on top of an in-game menu on a scaled desktop or a non-native renderer resolution.
- Hardened the loaders that read untrusted content. Malformed maps, models, and images from a downloaded pk3 or workshop item are now rejected with a clear message instead of corrupting memory: BSP visibility and patch lumps, MD3 and MDR models, and the PCX and BMP image readers all validate their declared sizes against the data actually present. A stock, well-formed asset is unaffected.
- A broken or hostile ROQ cinematic can no longer overrun the video decoder's buffers; an unusable header now stops playback instead of starting it.
- A server no longer crashes when a client stops acknowledging reliable commands. The overflow path used to recurse into itself while announcing the disconnect until the stack ran out.
- Recording a demo from a hostile server or replaying a crafted one no longer overflows the client's saved-snapshot buffer.
- Shutting down a server now actually releases each client's download buffers, queued network fragments, demo handles, and Steam auth sessions; the cleanup loop was running zero times because the client count had already been cleared.
- Fixed a shader referencing a lightmap the current map does not have; it now falls back to white instead of reading a stale pointer from the previously loaded map.
- RTX: compacting the world acceleration structure no longer drops the world out of the ray-traced scene and forces a full rebuild every frame.
- Vulkan: depth/stencil barriers now cover both aspects, clearing a stream of validation errors, and the descriptor pool accounts for every bloom pass so a full texture set can no longer exhaust it.
- Server traces no longer clear a 4 KB scratch buffer on every call, removing a large amount of pointless work from the busiest server path.
- Loading a map no longer prints a line per curved surface to the console and log.

### Documentation and Tooling
- _None yet._
