# Menu And Cinematic Aspect Handling

Retail Quake Live modules already own the HUD coordinate transform and the
world-view field of view. FnQL therefore passes native cgame HUD geometry and
scene FOV through unchanged. It does not apply the inherited FnQ3 HUD expansion
rules, load a HUD aspect file, or perform a second FOV correction.

Two engine-owned presentation controls remain:

- `cl_menuAspect 0`: Stretch menu widgets to the framebuffer.
- `cl_menuAspect 1`: Keep menu widgets in centered 4:3 space, including native
  UI model-preview viewports. This is the default and matches retail QL.
- `cl_cinematicAspect 0`: Stretch UI and fullscreen cinematics.
- `cl_cinematicAspect 1`: Keep cinematics in centered 4:3 space.

## Menus And 3D Widgets

With `cl_menuAspect 1`, traditional menu art and UI-rendered model scenes use
the same centered 4:3 transform. The UI module remains responsible for the
scene projection; the engine changes only the destination viewport.

## Cinematics

`cl_cinematicAspect` is independent of menu presentation. Use `1` for
pillarboxed 4:3 video or `0` for fullscreen stretching.

Console layout and scaling are documented in the [Console Guide](CONSOLE.md).
