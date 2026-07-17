# Console Guide

FnQL treats the console as its own configurable subsystem rather than as an afterthought hanging off HUD or menu scaling. This guide walks through the settings that shape how the console looks, feels, and behaves in day-to-day use.

For menu and cinematic presentation controls, see the [Aspect Handling guide](ASPECT_CORRECTION.md). Retail cgame owns HUD projection and world FOV.

## Overview

If you just want the short version: you can choose whether the console stays full-width or centered, whether it scrolls instantly or smoothly, how it looks, and how much modern input help you want while typing.

Console-owned text uses Quake Live's bundled `fonts/droidsansmono.ttf` through the renderer's retail-compatible FontStash/STB path. This covers scrollback, input, completion results, notify lines, live chat, and the clock/version labels. FnQL uses retail QL's 12×24 base cell and half-size default geometry, exposed as the normalized `con_scale 1`; at the observed 768-pixel retail reference height this is a 6×12 cell. Font sizes, baselines, glyph bounds, mono advances, kerning, and ascender/descender handling come from the same historical rasterizer family as retail. If the host TTF lane is unavailable or no retail TTF can be mounted, FnQL falls back to the legacy bitmap charset so the console remains usable.

- `con_screenExtents 0`: Use the full screen width for the console display.
- `con_screenExtents 1`: Keep the entire console display in centered 4:3 space.
- `con_scaleUniform 0`: Keep the console font in native pixel sizing.
- `con_scaleUniform 1`: Scale the console font from centered 4:3 space and relayout it within the current console extents.
- `con_scale`: Set the base console font size before the active scaling mode is applied. The default is `1`; the archived FnQL scaling range remains supported.
- `con_speed`: Control how quickly the console opens and closes.
- `con_scrollLines`: Control how many lines a normal console scroll step moves.
- `con_scrollSmooth 0`: Keep console scrollback movement immediate.
- `con_scrollSmooth 1`: Smoothly animate scrollback movement and new-line pushes.
- `con_scrollSmoothSpeed`: Set the smooth-scroll speed in lines per second.
- `con_completionPopup 0`: Disable the live completion popup and keep classic `Tab` completion behavior.
- `con_completionPopup 1`: Show the live completion popup while typing. This is the default.
- `con_sayRaw 0`: Keep the legacy plain-text console chat path, which routes text through `cmd say`.
- `con_sayRaw 1`: Use the raw console input line for plain in-game `say` chat, matching the quoted chat-style path more closely. Disabled by default.
- `con_showClock 0`: Hide the console clock.
- `con_showClock 1`: Show the current system time in 24-hour format.
- `con_showClock 2`: Show the current system time in 12-hour AM/PM format.
- `con_showVersion 0`: Hide the console version string.
- `con_showVersion 1`: Show the console version string. This is the default.
- `con_fade 0`: Keep console open and close transitions fully opaque.
- `con_fade 1`: Fade console background, text, and accents in and out while it opens or closes.
- `con_backgroundStyle 0`: Use the classic textured console background.
- `con_backgroundStyle 1`: Use a flat shaded console background.
- `con_backgroundColor`: Override the console background RGB color with `R G B` values from `0-255`.
- `con_backgroundOpacity`: Set console background opacity from `0` to `1`.
- `con_lineColor`: Set the separator line and scrollback marker color with `R G B` values from `0-255`.
- `con_versionColor`: Set the version and clock text color with `R G B` values from `0-255`.

## Layout And Scaling

`con_screenExtents` and `con_scaleUniform` control different parts of the console.

- `con_screenExtents` chooses whether the whole console display uses full-screen width or centered 4:3 width.
- `con_scaleUniform` changes the console from native-pixel font sizing to retail QL's observed height-derived 768-reference scaling without forcing the console itself to use centered extents.
- Character width and height are derived from retail QL's 12×24 reference cell and the active console scaling mode instead of being treated as fixed screen pixels.
- `con_scale` affects console-owned TTF text only; client bitmap overlays retain their own retail/FnQ3 sizing contract.
- TTF baselines stay at the bottom of each cell, as in retail. The renderer applies retail FontStash's tenths truncation, STB pixel-height scaling, integer quad placement, and visual glyph bounds without ad-hoc vertical offsets.
- Console line width, prompt width, visible page size, scroll paging, and input field width are recomputed from the current character metrics and the active console extents.
- With `con_screenExtents 0`, uniform font scaling still uses the full console width.
- With `con_screenExtents 1`, the entire console display, including its background and text block, is centered in 4:3 space.
- Console relayout updates automatically when screen size, internal scaling, `con_scale`, `con_scaleUniform`, or `con_screenExtents` changes, so you do not need to reopen the console to see the result.

## Motion And Scrolling

These settings mostly come down to taste: some players want classic snap scrolling, others want the console to move with a bit more weight.

- `con_speed` controls how fast the console opens and closes.
- `con_scrollLines` sets the default number of lines moved by normal scroll steps such as `PgUp`, `PgDn`, or mouse wheel scrolling.
- `Ctrl+PgUp` and `Ctrl+PgDn` still scroll by one visible console page.
- `con_scrollLines` is clamped to the current visible console page, so it automatically respects the current console height and text size.
- `con_scrollSmooth 1` interpolates scrollback movement instead of snapping it immediately.
- `con_scrollSmoothSpeed` defines the smooth-scroll speed in lines per second.
- Smooth scrolling applies both when you manually scroll the console and when new console output pushes older lines upward.

## Mouse Interaction

The console keeps mouse ownership while it is active instead of dropping you back onto the desktop cursor.

- Opening the console keeps the mouse captured by the game window.
- Closing the console returns mouse ownership to normal gameplay handling.
- A narrow vertical scrollbar appears when the console has scrollback beyond the visible page.
- The scrollbar thumb can be dragged directly to scroll through console history.
- The scrollbar height stays within the console log text area instead of creeping above the visible log bounds.
- Hovering the scrollbar smoothly widens both the track and the thumb.
- The thumb becomes lighter while hovered so the active target reads more clearly.
- The console uses the same in-game cursor shader as the rest of the game while active.

## Selection, Clipboard, And Drag And Drop

The console supports real text selection and clipboard workflows for both the input line and the log, which makes it much less painful to reuse commands, error output, and snippets from the scrollback.

- Drag with the mouse across the input line to select command text.
- Drag with the mouse across the console log to select console output.
- Drag selected text from either region into the input line to reposition or reuse it without bouncing through the clipboard first.
- `Ctrl+C` copies the active input or log selection.
- `Ctrl+V` and `Shift+Insert` paste clipboard text into the input line.
- `Ctrl+X` cuts the current input-line selection.
- `Ctrl+A` selects all text in the focused input line or, when the log is focused, the current console log history.
- Input-line keyboard selection supports `Shift+Left`, `Shift+Right`, `Shift+Home`, `Shift+End`, plus `Ctrl+Shift+Left` and `Ctrl+Shift+Right` for word-wise selection.
- Log keyboard selection is available after focusing the log and uses `Ctrl+Shift` with arrows, `PgUp`, `PgDn`, `Home`, and `End`.
- Clipboard copy and paste use the platform clipboard path, so the feature works across the supported SDL and Win32 builds.

## Live Completion

Console input now exposes live, tab-based completion instead of only dumping a static match list after a completion request.

- `con_completionPopup 1` keeps the live popup enabled and is the default setting.
- `con_completionPopup 0` disables the popup and falls back to the classic one-shot `Tab` completion flow.
- Matching updates as you type, so you can see where a command is going before you commit to it.
- `Tab` accepts the current match and cycles forward through alternatives.
- `Shift+Tab` cycles backward through alternatives.
- Left-clicking an item in the completion popup inserts that item immediately.
- Completion covers commands, cvars, map names for map-loading commands, and other command-specific argument completions exposed through the existing completion callback path.
- The completion popup stays tied to the current input line instead of replacing it with a one-shot printout in the log.

## Console Chat

When you press `Enter` on plain text in the console while connected, FnQL can either keep the legacy `cmd say` path or send the input more directly as quoted chat.

- `con_sayRaw 0` keeps the legacy behavior and remains the default.
- `con_sayRaw 1` sends plain console text through the raw quoted `say` path instead, which preserves the input line more closely than the tokenized `cmd say` route.
- Slash commands are unaffected. This only changes the plain-text-in-console-to-chat path.

## Appearance

The console background and accent colors can be customized directly, so you can keep the stock look or push it toward something cleaner and flatter.

- `con_showClock 1` or `2`: Show the current system time in the console header area. When the version string is also enabled, the clock sits directly above it.
- `con_showVersion 1`: Keep the console version string visible in the bottom-right header area.
- `con_showVersion 0`: Hide the version string entirely.
- `con_backgroundStyle 0`: Use the classic textured console background.
- `con_backgroundStyle 1`: Use a flat shaded console background.
- `con_backgroundColor`: Override the console background RGB color with `R G B` values from `0-255`.
- `con_backgroundOpacity`: Set console background opacity from `0` to `1`.
- `con_lineColor`: Set the separator line and scrollback marker color with `R G B` values from `0-255`.
- `con_versionColor`: Set the version and clock text color with `R G B` values from `0-255`.
- `con_fade 1`: Fade the console background, text, and accents in and out during open and close transitions.

Legacy compatibility notes:

- `cl_conColor` is still honored as a legacy fallback for background tinting.
- If `con_backgroundColor` is empty, the console uses the legacy `cl_conColor` value when one is set.
- If neither override is set, textured mode uses its default look and flat mode uses a plain black background.

## Recommended Starting Point

- `con_screenExtents 1` if you want the whole console centered in 4:3 space
- `con_scaleUniform 1`
- `con_scrollSmooth 1` if you want console scrollback to move instead of snap
- `con_fade 1` if you want softer console open and close transitions

That setup is a good default if you want the console to feel more polished on a modern display without giving up the classic full-width behavior as an option.
