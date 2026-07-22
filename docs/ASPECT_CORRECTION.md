# Menu And Cinematic Aspect Handling

Retail Quake Live modules already own the HUD coordinate transform and the
world-view field of view. FnQL therefore passes native cgame HUD geometry and
scene FOV through unchanged. It does not apply the inherited FnQ3 HUD expansion
rules, load a HUD aspect file, or perform a second FOV correction.

Two engine-owned presentation controls remain:

- `cl_menuAspect 0`: Stretch ordinary menu widgets to the framebuffer.
- `cl_menuAspect 1`: Keep menu widgets in centered 4:3 space, including native
  UI model-preview viewports. This is the default and matches retail QL.
- `cl_cinematicAspect 0`: Stretch UI and fullscreen cinematics.
- `cl_cinematicAspect 1`: Keep cinematics in centered 4:3 space.

## Menus And 3D Widgets

With `cl_menuAspect 1`, traditional menu art and UI-rendered model scenes use
the same centered 4:3 transform. The UI module remains responsible for the
scene projection; the engine changes only the destination viewport.

Profiles created by early FnQL builds may contain the former archived default,
`cl_menuAspect 0`. FnQL migrates that legacy value to `1` once so upgraded
profiles receive retail drawing. Setting it back to `0` after that migration
remains an explicit full-frame stretch opt-out.

## Connecting Screen

Retail `ui/connect.menu` declares `ui/assets/backscreen_smoke` as a 1920x1080
background. FnQL preserves that authored 16:9 aspect with a centered cover
crop: narrower displays crop the sides, while displays wider than 16:9 crop
the top and bottom. The image is never stretched, and ultrawide modes do not
smear its outermost texture columns across the extra width. The complete
connection screen keeps the retail UI's centered coordinates regardless of
the optional `cl_menuAspect` override used by ordinary menus.

The retail `backgroundSize` path submits this backdrop in framebuffer pixels,
unlike traditional 640x480 menu quads. FnQL recognizes that exact draw before
applying `cl_menuAspect 0`; treating it as a traditional quad would widen the
already-native geometry a second time on widescreen displays.

The retail cgame takes over after the initial connection dialog and uses the
same authored 16:9 crop for its smoke and map-levelshot loading backgrounds.
FnQL recognizes only that full-frame loading draw signature during
`CA_LOADING` and `CA_PRIMED`, and applies the same centered cover crop. HUD and
other cgame draws are not modified.

## Cinematics

`cl_cinematicAspect` is independent of menu presentation. Use `1` for
pillarboxed 4:3 video or `0` for fullscreen stretching.

Console layout and scaling are documented in the [Console Guide](CONSOLE.md).
