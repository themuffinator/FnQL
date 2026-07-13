# Quake Live Font Compatibility

FnQL keeps two font paths because retail modules use both contracts:

- the inherited `fontInfo_t` registration ABI used by source-era menus; and
- the Quake Live host `DrawScaledText` / `MeasureText` imports used by retail
  UI and cgame modules.

## Evidence

Observed in the legitimate Steam `baseq3/pak00.pk3` fixture on 2026-07-11:

- `fonts/handelgothic.ttf` (54,852 bytes)
- `fonts/notosans-regular.ttf` (305,800 bytes)
- `fonts/droidsansmono.ttf` (117,072 bytes)
- `fonts/droidsansfallbackfull.ttf` (4,033,420 bytes)

Static retail executable comparison shows that host text uses the historical
FontStash/STB rasterizer, treats font handles 0, 1, and 2 as the normal, sans,
and mono face slots, decodes UTF-8, consumes only `^0` through `^7` as color
controls, truncates requested sizes to tenths, and probes sans, bundled Unicode,
and Windows Unicode fallbacks after the requested face. The retail import's
integer text argument is a glyph-count limit: `-1` is unlimited, `0` draws no
glyphs, and positive values count decoded glyphs. A non-null float pointer is
always an in/out clip boundary. Measurement returns the exact five-float visual
bounds packet: left, top, right, bottom, and ascent.

## FnQL implementation

The renderer owns the pinned 2014 FontStash implementation and its bundled
`stb_truetype`, independently adapted to FnQL's renderer interfaces. It uses
retail's 128 KiB STB scratch buffer and lazy one-byte alpha atlas, beginning at
512x512, doubling both axes through 1024x1024 and 2048x1024, then resetting at
the cap. Each backend receives an unscaled RGBA subimage at its existing texture
boundary, retaining the retail alpha values while preserving FnQL's classic
OpenGL, OpenGL2, GLx, and Vulkan paths. FontStash owns the retail skyline
packing, glyph padding, integer quad placement, kerning, and face metrics.
Malformed UTF-8 is replaced while parsing always progresses. Missing faces,
glyphs, atlas capacity, and optional Windows fonts fail locally and retain a
charset fallback instead of aborting renderer startup. The inherited
`fontInfo_t` registration ABI remains on FreeType as a separate classic lane.

The engine console selects retail host font handle 2
(`fonts/droidsansmono.ttf`) for scrollback, input, completion, notify, live
chat, clock, and version text. It retains retail QL's 12x24 base cell and
half-size default geometry, exposed as normalized `con_scale 1` (a 6x12 cell
at the observed 768-pixel retail reference height), bottom-of-cell baseline, FontStash mono advances, and text-run color
handling. Editable console and chat windows count Unicode characters while
retaining the retail byte-offset `field_t` ABI; cursor placement, mouse hit
testing, selections, and drag/drop use measured TTF prefixes and never split a
UTF-8 sequence. Host sizes use retail's STB pixel-height scale, tenths
truncation, baseline, and real glyph bounds; renderer metrics are not fed back
into console row geometry. FnQL's selection, mouse hit testing, wrapping,
completion popup, smooth scroll, fade, clock, configurable extents, and larger
archived scales remain layered on that retail contract. The bitmap charset
remains an availability fallback only. Console cell metrics are private to the
console; inherited `smallchar_*` metrics used by unrelated overlays retain
their existing FnQL scaling.

Client-owned large strings use the same retail mono host lane after conversion
from 640x480 virtual coordinates to screen pixels. The explicit small-string
API remains on the legacy bitmap charset, matching retail's two-path client
contract. FnQL's literal-color-code option is preserved by decoding and drawing
UTF-8 scalars individually without consuming `^0` through `^7`.

The Windows-only fallback probes `arialuni.ttf`, `segoeui.ttf`, then
`l_10646.ttf`. Reads are capped at 32 MiB. Other platforms use the retail
bundled fallback without assuming a system font layout.

## Regression gates

- `fnql_ql_font_text` validates bounded UTF-8 decoding, retail color controls,
  and the exact five-float bounds bridge.
- `fnql_ql_font_source` validates face order, renderer exports, ABI semantics,
  retained atlas growth/reset/debug configuration, bounded fallback behavior,
  and the console's mono-TTF preference.
- Native validation builds the client plus classic OpenGL, OpenGL2, GLX, and
  Vulkan renderer modules.
- The retail smoke probe must always use `+set r_fullscreen 0`; successful
  initialization logs `QL fonts: retail FontStash/STB host text enabled` after
  mounting the four Steam fonts from `pak00.pk3`. The retail
  `r_debugFontAtlas 1` diagnostic is available through the same client draw
  boundary for classic OpenGL, OpenGL2, GLX, and Vulkan.

The 2026-07-11 Windows x86 probe loaded the legitimate retail qagame, cgame,
and UI modules and allocated atlas page 0 during the windowed `campgrounds`
loading path. The subsequent local-connect access violation occurs after the
font allocation in the independently modified protocol/server path and is not
treated as font validation success or failure.
