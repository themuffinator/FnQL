# WebUI runtime backend boundary

FnQL presents the retail Quake Live WebUI on Windows x86 by adapting the
Awesomium runtime from the user's legitimate retail installation. FnQL does
not bundle Awesomium, copy its runtime into packages, import proprietary SDK
headers, or reproduce C++ object layouts. Other platforms and x64 builds keep
the deterministic native-UI fallback.

The Windows x86 default is enabled because retail QL ships only 32-bit game
modules and Awesomium. `cl_webuiEnable 0` remains an explicit opt-out. Runtime
discovery is rooted in the configured QL base/home and executable paths and
fails closed if `awesomium.dll`, `awesomium_process.exe`, `web.pak`, or a
required generated C export is unavailable.

## Verified retail runtime observations

The following facts were observed in windowed 2026-07-10/11 probes against the
user's legitimate Steam installation; they are not inferred from QLSRP:

- `awesomium.dll` reports Awesomium 1.7.4.2 and starts the retail
  `awesomium_process.exe` helper.
- The local retail `web.pak` is Chromium DataPack v4 with 1,024 resources.
- A `QL` DataPak source resolves `asset://ql/index.html` into a live offscreen
  view and exposes a non-power-of-two 1280×720 software surface.
- The engine-owned pre-document bridge makes `qz_instance` available during
  page bootstrap. The live page subsequently issued native bridge requests.
- The retail Match Browser subscribes to `servers.details.*.response` for
  each server row; `servers.refresh.start` and `servers.refresh.end` only
  bracket the refresh. Its row identity is `<network-order IPv4>_<port>` and
  includes `numPlayers`, `maxPlayers`, `botPlayers`, `password`, `vac`,
  `steam_id`, `tags`, `gametype`, and `gamedir`. The browser's Friends filter
  is request value `2`, while the Steam matchmaking request selector uses
  value `5` for Friends.
- The Match Browser restores `browser_filters` independently from its
  session-cached `browser_data`. On mount it sorts cached rows with the restored
  filters but does not recompute each row's `filtered` member. Its live row
  handler also computes that member before subtracting `botPlayers` from
  `numPlayers`, so the saved empty/full filters can disagree with the displayed
  human player count until any filter control changes.
- Engine renderer screenshots showed the complete retail menu surface under
  OpenGL, OpenGL2, GLx, and Vulkan,
  including Play, Statistics, Steam Workshop, Steam Community, Settings,
  friends/lobby, and the retail background.
- A Vulkan `vid_restart` rebuilt the renderer while retaining the browser
  document and reproduced the identical full-menu screenshot without an engine
  or Awesomium diagnostic.
- A 2026-07-19 windowed GLx quit probe under a first/second-chance exception
  tracer reproduced an `0xC0000005` access violation after the adapter released
  its WebSession before WebCore shutdown. Retail static evidence shows only
  WebView destruction followed by `WebCore::Shutdown`; assigning session
  ownership to WebCore produced a clean traced exit with code `0` and no access
  violation.
- The packaged retail top-right power control sends
  `qz_instance.SendGameCommand("quit")`. Native JavaScript requests are drained
  while the document is loading so that command cannot be stranded during the
  preload-to-retail-page handoff; heavier snapshot and live-event work remains
  loading-gated.
- The packaged retail Start Match component takes a synchronous copy of
  `GetConfig().cvars` when it mounts. Its four numeric range rows parse missing
  values into `NaN`; the package identifies those rows as
  `sv_maxclients`, `bot_minplayers`, `teamsize`, and
  `sv_warmupReadyPercentage`.
- A 2026-07-19 windowed 1280x720 GLx probe displayed those four rows as `8`,
  `0`, `0`, and `0.51`. Sending the same `map campgrounds ffa` command used by
  the Launch Match button then reached the retail cgame Join Match view with no
  WebUI connection/loading document composited over it.
- Starting the legacy DataPak loader after Vulkan initialization made the same
  verified absolute `web.pak` path fail with `DataPak.cc(102)` in the 32-bit
  process. Starting WebCore and the DataPak source during the existing
  pre-renderer client bootstrap loaded the same package successfully; the
  provisional 1280x720 view then resized to the renderer dimensions.
- A 2026-07-25 windowed 1920x1080 GLx probe walked the retail settings routes
  with the section overlay in place. `web_status` reported 97 injected rows in 7
  groups plus 15 hidden retail rows on Video, 41 rows in 5 groups on Game, 21
  rows in 2 groups plus 5 hidden rows on Sound, 9 rows in 1 group on Team, 5
  rows on Input, 3 rows in 1 group on Weapons, and 2 rows plus 2 hidden rows on
  Basic, with `nanOutputs` zero on every route except the two hidden retail
  sound sliders. Screenshots showed the retail rows carrying the player's own
  values rather than defaults.
- Before the settings snapshot covered them, the retail settings rows read
  `undefined` from `GetConfig().cvars`: the retail `Cvar` component only falls
  back to `GetCvar` when its `value` prop is exactly `null`, and the settings
  component seeds `state.cvars` from the snapshot alone.
- A 2026-07-19 windowed GLx probe at 2560x1440 showed a complete Awesomium CPU
  surface but an all-black renderer screenshot. The identical launch at
  640x480 presented the CPU surface exactly in the renderer screenshot.

Static inspection explained the resolution-dependent result: the inherited
dynamic-image path reports a 2048-pixel texture limit, reduces the initial
upload to that limit, then attempts later sub-image updates with the original
larger dimensions. Those updates cannot fit the allocated renderer image.

QLSRP was used separately as behavioral and ABI evidence for the expected
core/session/view lifecycle and generated export names. The FnQL adapter is an
independent typed implementation. Retail behavior remains authoritative.

## Implemented architecture

`code/client/webui_backend.hpp` defines a versioned browser-neutral C++17
contract with explicit lifecycle, surface, input, navigation, script, resource,
status, and ownership rules. `awesomium_backend_win32.cpp` implements the
Windows x86 adapter around the runtime's generated stdcall C exports:

- absolute-path DLL loading with dependency search rooted beside the selected
  runtime and a legacy-Windows fallback that still uses an absolute path;
- exact required-export validation before any runtime object is created;
- owned UTF-8/UTF-16 conversion and copied diagnostics;
- reverse-order cleanup of partial or complete startup;
- WebCore, bitmap factory, WebSession, authoritative native retail `QL`
  DataPak source, and offscreen WebView ownership;
- pre-renderer WebUI startup with a bounded provisional surface, followed by
  a retail-height surface whose width matches the viewport aspect and whose
  final dimensions are constrained together to the renderer's texture limit,
  so 4:3 and widescreen views remain undistorted and the 32-bit Vulkan path
  does not contend with the legacy package loader during startup;
- pre-document and post-navigation qz bridge injection;
- bounded script results and software-surface copies;
- resize, focus, pause, mouse, wheel, keyboard, cache, reload, crash, and
  loading-state operations; and
- no Steamworks success emulation. Unavailable social/online operations remain
  explicit and do not prevent the offline retail menu from rendering.

### Keyboard and text input

The generated C ABI publishes one WebKeyboardEvent constructor,
`_Awe_new_WebKeyboardEvent_1@12`. Its twelve-byte stdcall frame and overload
index identify the runtime's Win32 message constructor — `(UINT, WPARAM,
LPARAM)` — which derives the event type, the typed text, the key identifier,
and the live modifier state from real message parameters. The runtime exposes
no constructor taking a type/virtual-key/native-key triple, so passing the
browser-neutral event type where the message id belongs matches no message
case: the event stays untyped, its text stays empty, and retail input fields
such as the Match Browser filter never receive characters even though the page
composites and responds to the mouse normally.

The adapter therefore presents each event as the message triple Windows itself
would deliver, rebuilding the lParam — repeat count, layout scancode, extended
bit, and the previous-state/transition bits of a release — when the caller does
not assert a specific hardware key.

The browser-neutral contract keeps the two halves separate the way browsers
do. `KeyDown`/`KeyUp` carry a platform virtual-key code, so the client restores
retail's Windows virtual keys from the Quake keynums its platform layers
produced; keynums no keyboard key produces are not forwarded. `Character`
carries one UTF-16 code unit of already-composed text, delivered ahead of the
UTF-8 split the legacy modules and edit fields require, with a non-BMP code
point sent as its surrogate pair.

### Sparse FnQL settings overlay

FnQL builds and ships `fnql-web.pak`, a deterministic Chromium DataPack v4
sidecar containing only three project-owned resources: `index.html`,
`fnql-settings.js`, and `css/fnql-settings.css`. It does not contain retail
`bundle.js`, fonts, images, or styles.

The live browser keeps Awesomium's native retail DataPak source authoritative
for `asset://ql/`; replacing that reserved host with a generic DataSource was
observed to leave navigation at `about:blank` without dispatching a resource
request. Once the retail document is interactive, FnQL reads the project-owned
settings script and stylesheet through its bounded engine resolver and injects
their contents directly. The resolver applies:

1. matching resources from `fnql-web.pak`;
2. all remaining resources from the user's external retail `web.pak`; and
3. the established bounded loose-file fallback.

The packaged `index.html` remains part of the deterministic sparse-overlay
fixture, but the live runtime never replaces the retail navigation document
with it. This keeps retail bootstrap behavior authoritative while preserving
the FnQL settings extension.

#### Settings placement

Engine-owned controls live in the retail settings section they belong to; there
is no separate FnQL tab and the retail navigation is left untouched.

| Retail section | FnQL content |
| --- | --- |
| Input | free look, mouse input source, acceleration style and power, mouse smoothing, appended to the retail Mouse Settings columns |
| Gamepad | gamepad enable and profile, movement threshold and scale |
| Basic | windowed and fullscreen resolution, replacing the retail mode rows |
| Game | interface, console, capture, network, and frame-pacing groups |
| Team | teammate and opponent highlight color overrides appended to the retail team and enemy columns, plus a Player Highlighting group for the mode, pass intensities, outline thickness, and the red/blue/free colors |
| Weapons | rail trail geometry |
| Video | renderer and display in the retail second column, then framebuffer and anti-aliasing, texture and geometry detail, lighting and shadows, color and tone, bloom, scene effects, and cel shading |
| Sound | unfocused/minimized muting appended to the retail volume column, then audio backend and spatial audio groups |

Rows are built only for cvars present in the engine's allowlisted configuration
snapshot, so backend-specific controls disappear on their own: the GLx-only
bloom tuning cvars are absent under Vulkan and RTX, `s_al*` rows are absent
under the legacy mixer, `s_khz` and `s_mixAhead` only appear under it, and the
WinMM gamepad rows only appear on Windows. When that set changes the section is
rebuilt rather than patched, so a `snd_restart` between backends does not leave
stale rows behind.

Retail rows FnQL cannot honor are hidden rather than left inert: the entire
legacy post-processing column on Video, its `r_mode`/`r_windowedmode` rows
because FnQL's video mode table uses different indices, the `r_lightmap`,
`r_fullbright` and `r_ambientscale` debug/cheat rows, `com_allowconsole`, and
the `s_announcervolume`, `s_killbeepvolume`, `s_mutebackground` and `s_ambient`
rows that have no engine lane. Video and Sound carry a short note explaining the
removal, and take over the retail Apply button so it stays at the end of the
page.

#### Injection rules

The retail page is React 0.13, which reconciles a container's children by index
and inserts with `parentNode.childNodes[index]`. Injected nodes are therefore
only ever appended: at the tail of a retail column, of the section, or of a
heading. React's own children keep the leading indices its reconciler assumes,
and an insert at its own child count lands immediately before the first injected
node, which is the correct position. Inserting ahead of a React child would make
its later inserts and moves land in the wrong place.

Rows reuse retail's own `.cvar` markup and stylesheet, including its react-select
DOM. Awesomium's offscreen view does not composite native `<select>` popups,
which is why retail uses react-select at all, so the overlay reproduces that
markup and drives it directly rather than using a `<select>` element. The
project stylesheet only adds what retail has no equivalent for: the hidden-row
rule, the per-row cvar name, restart tag, and help line, and the section note.

#### Settings snapshot

Both the retail settings component and the overlay read their initial values
from `GetConfig().cvars` alone; neither falls back to a per-cvar `GetCvar`. A
cvar missing from the snapshot therefore renders its row with a default instead
of the player's value, and a truncated snapshot does the same for every row
after the cut. The allowlist consequently covers the whole settings surface -
the engine-owned cvars the retail sections bind rows to, the archived
cgame-owned rows, and the FnQL rows - the buffer is sized for it, and a
truncation prints a developer diagnostic naming the cvar it stopped at.
`tests/webui_settings_overlay_tests.py` keeps the overlay, the allowlist, and
the engine's registrations from drifting apart.

Browser-originated cvar writes are refused for `CVAR_PRIVATE` and `CVAR_ROM`.
`CVAR_PROTECTED` is deliberately writable: it marks Quake Live engine-managed
cvars, several of which the retail settings page owns - `r_windowedMode`,
`m_cpi`, `cl_mouseAccel`, `cl_mouseAccelOffset`, `cl_mouseAccelPower`,
`cl_allowConsoleChat`, `cl_demoRecordMessage`, `cl_timeNudge`, `com_maxfps` -
and `Cvar_WriteVariables` routes exactly that flag to `repconfig.cfg`. Refusing
those writes left the corresponding rows present and inert, and the flag never
provided a boundary here because a page can already run console commands through
the `cmd` request.

The startup bridge also closes the retail Match Browser's filter-order gap
without replacing its list or filter policy. When the browser table mounts, and
again after `servers.refresh.end`, it performs a paired click through the retail
empty-filter control. The pair ends on the original value while making that
control clone the already-restored complete filter object, which makes the
retail component recompute every row after player-count normalization and
re-sort the list while preserving the saved values. This covers both
session-cached rows and newly received server rows.

Build systems generate the sidecar alongside the executable, and the runtime
checks the executable directory first so VS Code/Meson build trees discover it
on Windows even though `fs_apppath` is not registered there. Installation and
release-layout checks require it, and release packaging rebuilds it from source
instead of trusting an artifact copy. Maintainers can reproduce it directly:

```powershell
python scripts/build_webpak.py --source-root code/client/webui --output .tmp/fnql-web.pak
```

If the overlay is absent or rejected, FnQL preserves the unmodified retail
menu. If retail `web.pak` is absent, the browser backend still fails through
the existing native-UI fallback rather than treating the sparse overlay as a
standalone copy of the Quake Live launcher.

The legacy native-UI `web_stopRefresh` verb is deliberately non-destructive
when the live browser owns the document. Retail does not register it as an
Awesomium navigation-stop command, and aborting an in-flight `index.html`
load can leave Chromium's `chrome://chromewebdata/` error document active.
Bridge retries use a private lightweight qz synchronizer rather than calling a
page-owned `main_hook_v2` repeatedly after startup.

The initial qz snapshot includes the complete retail Start Match setting set.
Existing engine values win; defaults are supplied only for module-owned cvars
that do not exist before qagame loads, so the client does not steal their
registration or flag ownership. Local map loads use a non-menu disconnect and
keep the earlier browser pause/unfocus in force through connection, loading,
and active play. Terminal disconnects still resume the retained document and
restore WebUI input, with the native menu as fallback.

The renderer API owns a dedicated bounded RGBA WebUI image and shader in
OpenGL, OpenGL2, GLx, and Vulkan. It does not consume cinematic handles or
inherit the cinematic power-of-two restriction. The offscreen document keeps
retail's 1080-line logical height and derives its width from the viewport
aspect before both axes are constrained together to the renderer's maximum
texture dimension. It can therefore fill 4:3, 16:10, 16:9, and ultrawide
viewports without stretching; browser input uses the same coordinate mapping.
Native UI ownership transfers only after a live Awesomium bitmap surface and
renderer presenter both exist.

Once play is active, Escape transfers ownership from the browser/cgame catcher
to the retail native UI and activates its in-game menu. If cgame owns a command
overlay, the engine first sends retail event 5 (`CGAME_EVENT_CLOSECOMMANDOVERLAY`)
and releases that catcher. It does not replace the in-game Escape menu with
`UIMENU_TEAM` for spectators: that team command activates `joingame_menu`, and
stacking it over the in-game root leaves both menus' content and positioning
mixed together. The native import slab
matches retail `uix86.dll` at this boundary: sound-registration slot 35 accepts
only the sample path. Treating it as the legacy two-argument QVM call corrupts
the call boundary and stops `ui/ingame.menu` parsing at `itemFocusSound`, which
leaves Escape with no visible menu. Native key-event slot 2 likewise translates
the legacy `(key | K_CHAR_FLAG, down, time)` tuple to retail's observed
`(key, characterEvent, down)` ABI. Passing the timestamp as retail's `down`
argument makes Escape's key-up close the menu immediately after it opens.

`KEYCATCH_UI` is claimed unconditionally once `UI_SET_ACTIVE_MENU` has been
sent, as retail does. It must not be made conditional on the module's own
`UI_MENUS_ANY_VISIBLE` menu-stack query: that slot was recovered from
`uix86.dll` without a signature and is only established for the fullscreen main
menu while disconnected, which is the only place the engine consumes it
(`CL_UIMenusAreVisible`). The in-game menu's draw pass in `SCR_DrawScreenField`,
its absolute-pointer cursor, and its mouse grab all key off that catcher bit, so
gating the claim on an unestablished predicate removes the in-game menu
entirely. Confirming the slot's retail semantics is a prerequisite for any
verification built on it.

`KEYCATCH_BROWSER` is different: it is engine-owned, and
`CL_WebHost_HasDrawableSurface` is the same predicate that already decides
whether the overlay is drawn and whether it owns input. Escape therefore stays
with the browser only while it can actually present a surface; a claim that
outlived its surface is released rather than swallowing the key.

That in-game menu routes its Main Menu and Settings buttons back to the WebUI:
retail `ui/ingame.menu` runs `exec "web_changeHash /"` and
`exec "web_changeHash /settings"` before opening the native `ingame_about`
page, so the route is carried entirely by the browser verb. Because the
connection transition already paused rendering and unfocused the retained
document, changing its hash is not sufficient to present it again. A paused
view keeps compositing the last pre-game bitmap -- the main menu route, which
made Settings look like it opened the main menu -- and an unfocused Awesomium
view discards the injected mouse and key events the overlay is already
receiving, so the presented menu accepted no clicks. Both verbs therefore
resume rendering and restore focus on the retained document before claiming the
overlay, and fall through to the navigating path when that fails.

## Safety and non-regression

- Privileged navigation remains locked to `asset://ql/`.
- Browser resources remain bounded by the checked WebPak/launcher resolver.
- The retail runtime and assets remain external and are never release inputs;
  only the small project-owned settings overlay is packaged.
- x64, Linux, and macOS retain the null backend and native UI fallback.
- A missing, incompatible, or crashed browser yields to the native UI instead
  of making the engine unusable.
- `web_status` reports bounded document/backend state and `web_dumpSurface`
  captures the copied CPU bitmap for renderer-independent diagnosis. Its
  `fnqlRows`, `fnqlGroups`, and `fnqlHiddenRetailRows` counters report what the
  settings overlay actually placed in the live document.
- `FNQL_WEBUI_VERBOSE_LOG=1` enables the retail runtime's verbose log level for
  an explicit diagnostic launch; normal launches retain the runtime default.
- The typed fake-backend suite continues to cover interface mismatch, lifecycle,
  crash/retry, navigation, scripts, input, surface bounds, and balanced resource
  ownership without requiring the proprietary runtime.

## Runtime validation

Promotion still requires the proportionate non-regression matrix: strict x86
and x64 builds, all source/contract tests, native fallback probes, and windowed
retail launches. Full-menu screenshots now cover OpenGL, OpenGL2, GLx, and
Vulkan, including a Vulkan `vid_restart` survival probe. Resize,
cross-renderer switching, device-loss, simultaneous cinematic, and forced
child-process failure probes remain useful follow-up stress gates.
