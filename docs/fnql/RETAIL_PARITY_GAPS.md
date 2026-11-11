# Retail Functional Parity Gaps

## Scope

A whole-engine comparison of FnQL against retail Quake Live, taken on
2026-11-11 at FnQL `44fa44c`. It records what retail's engine does that FnQL
did not, which of those gaps this slice plugged, which differences are
deliberate, and which gaps remain open. Game rules, the HUD and menus are the
modules' business and are out of scope here.

Retail evidence is `quakelive_steam.exe` 0.1.0.739 (the build the other
compatibility notes pin), read through its disassembly in the QLSRP reference
corpus, plus the retail `cgamex86.dll`, `uix86.dll`, `pak00.pk3`, `web.pak` and
retail-written `qzconfig.cfg`/`repconfig.cfg` from the legitimate Steam
installation. Addresses below are retail addresses. QLSRP's reconstructed
source was used only to find things; where it and the binary disagreed, the
binary won. Nothing from QLSRP was copied.

## Method

1. **Command and cvar inventory.** Every name the retail executable passes to
   its command-registration and cvar functions (140 commands, 428 cvar names)
   was checked for an FnQL owner. 10 commands and 56 cvar names had none.
2. **Use in retail data.** Each unowned name was searched for in the retail
   menus, configs and `web.pak`, to separate names a player can reach from
   dead ones.
3. **Module import tables.** The cgame (128 slots), UI (99) and qagame (207)
   import tables were walked slot by slot against the retail tables at
   `0x565958`, `0x567338` and `0x56cf80`.
4. **WebUI bridge.** The 34 `qz_instance` methods, 37 event topics, and the
   `web_*` commands and cvars were compared, ranked by whether the retail page
   uses them.
5. **Network and operator surfaces.** Out-of-band packets, the per-client
   command table, the server console commands and the Steam game-server
   publication were compared.

Every claim acted on was re-read in the disassembly before code changed. Claims
that were only reported by the survey, not re-read, are marked *reported*.

## Plugged in this slice

### Server

| Gap | Retail evidence | FnQL now |
| --- | --- | --- |
| `sv_serverType` was registered but not enforced | Packets from anything but the in-process loopback client are ignored at 0 (`0x4e4518`); a non-LAN `getchallenge` is refused at 1 (`0x4df440`) | Both gates, the first ahead of every other packet consumer. A dedicated server warns at map start when it is 0 |
| qagame import 192 set the pure-check `cp` flag | It sets a ragequit flag read only by `SV_DropClient` (`0x4e26e0`, `0x4df830`, `0x4df726`) | New `rageQuit` flag; the drop is announced as `<name> ^1rage^7quits`. A client can no longer mark itself as having sent `cp` by sending `ragequit` |
| No reply to stray sequenced packets | Out-of-band `disconnect` to an address and qport that match no client (`0x4e463f`) | Sent, under a per-address rate limit |
| qagame import 200 returned a bot's anonymous SteamID | 0 for a slot with a `characterfile` userinfo key (`0x4e2710`) | 0 for bots |
| Steam server identity | Version `1069`, product `Quake Live`, description is the long gametype name, password flag from `g_needpass`, factory tags after `sv_tags` (`0x466fc1`, `0x467090`, `0x46650b`, `0x4662a7`, `0x466744`) | All five follow retail |
| Console `say` | `chat "%i Server: %s\n"` with sender 63 (`0x4def24`) | Retail form for native modules; classic modules keep `console:` |
| `kick` took no reason | `was kicked: <reason>` for a reason under 128 characters (`0x4dea7a`) | Optional reason |
| `arena` command missing | Launches `scripts/<name>.sp_arena` (`0x4de050`); retail ships easy, medium, hard, nightmare | Implemented with a standalone parser |

### Client and common

| Gap | Retail evidence | FnQL now |
| --- | --- | --- |
| Native cgame could not write protected cvars | cgame import 7 is the forcing `Cvar_Set` (`0x565974` -> `0x4ea2a0`); retail cgame writes `cg_viewsize`, `cg_announcer` through it | Native cgame gets the UI write policy; a legacy VM cgame keeps `Cvar_SetSafe` |
| `com_allowConsole` missing | `0x4b49d0`, `0x4b7b5e`; written by `ui/ingame_options_advanced.menu` and the settings page | Implemented, see `CLIENT_CVAR_COMPATIBILITY.md` |
| `con_height` missing | `0x4b4a7e`, `0x4b480e` | Implemented |
| `clearcvar` missing | `0x4cd860`; run by the "clear" buttons of the retail weapon-config menu | Implemented |
| `postprocess_restart` unowned | `0x4b9060`; run by three retail menu toggles | Owned and inert, so it is no longer reported unknown or forwarded to the server |
| `cvarMult`, `listcvars`, `listcmds`, `writeClientConfig` missing | `0x4cd560`, `0x4ce578`, `0x4c88b3`, `0x4cb630` | Implemented |
| `cg_autoAction`, `cg_autoHop` not in the first userinfo | Registered in the client init with flags `0x80803` (`0x4bcbac`, `0x4bcbc0`) | Registered |
| `s_muteBackground` missing | 0 requests a global-focus sound buffer (`0x4efd99`, `0x4efed2`) | Master switch over the two finer FnQL controls |
| `s_announcerVolume` not engine-registered | `0x4db8a9`, so the settings page can edit it before cgame loads | Registered |
| `in_mouseMode` missing | `0x4ec179`, `0x4ea588`, `0x4ea67c`, `0x4ebb88` | Published by the Windows backend |
| `appendlogfile` missing | `0x4c99af` | Honored beside FnQL's `logfile` 3 and 4 |

### WebUI

| Gap | Retail evidence | FnQL now |
| --- | --- | --- |
| Five working retail settings rows were hidden | See `WEBUI_BACKEND.md` | Shown |
| `lobby_autoconnect` and `connect_lobby` missing | `0x465867`, `0x464aa0`; the page reads the cvar at start-up to join a lobby instead of creating one | Implemented; the cvar is in the page's snapshot |
| `web_skipLegals`, `ui_bgcolor` not in the snapshot | Read synchronously by the retail page while it mounts | In the snapshot |
| `lobby.<id>.left` never published | `0x464a5f` | Published |
| Member-change document lacked the counts | `{id, name, num_players, max_players}` (`0x4652e0`) | Both counts added |
| No `lobby.error` on a failed join | `{code, id, message}` (`0x464de6`) | Published with one generic message |
| Browser writes forced cheat-protected cvars | `SetCvar`/`ResetCvar` skip flag `0x200` (`0x432b2b`) | Skipped |

### Join path, both directions (second pass)

A second pass the same day compared the connection path itself: how FnQL's
server treats a connecting retail client, and how FnQL's client behaves
against a retail server. Both netfield tables (58 entity fields, 58 player
fields) were also read out of the retail binary and match FnQL's in name,
order and bit width.

| Gap | Retail evidence | FnQL now |
| --- | --- | --- |
| Listen servers never answered `getchallenge` | Quake III's single-player test (gametype 2, or `ui_singlePlayerActive`) is absent from retail's handler (`0x4df430`). In Quake Live gametype 2 is Race, and `map` sets `ui_singlePlayerActive` on every listen server | The test applies only to classic modules. A match hosted from the client can now be joined |
| Message limit was 16,384 bytes | Retail's gamestate, snapshot and fragment buffers are `0x8000` and only longer messages are refused (`0x4d7505`, `0x4d7858`, `0x4e5b0d`) | 32,768 for protocol 91 in both directions; a legacy Quake III client is still sent at most 16,384 |
| A repeated `connect` stalled the join | Retail keeps the ticket with the challenge and re-accepts (`0x4e0b53`-`0x4e0c97`) | A resend with the same challenge for a slot still in `CS_CONNECTED` gets `connectResponse` again; the slot and its consumed ticket are left alone |
| Client asked retail servers for paks | Retail has no in-band download; its client warns and continues (`0x4bc392`-`0x4bc3c8`) and its server has no `download` command | On protocol 91, when the server does not advertise `sv_allowDownload`, FnQL prints retail's warning and continues. FnQL servers advertise the key and keep the download path |
| Loading clients were sent every broadcast | Only commands beginning `chat ` reach a `CS_PRIMED` client (`0x4e42a2`-`0x4e430f`) | Same filter for protocol-91 clients, which stops a long load on a busy server ending in a command overflow |
| Steam rules went stale | Every serverinfo pair is republished when serverinfo changes (`0x4e4b32` -> `0x465a60`) | Republished from the same place |
| `status` layout | Fixed columns with state, last message, qport and SteamID (`0x4dec60`) | Retail's table for Quake Live modules; classic modules keep the Quake III one |
| Shutdown message | `print "<msg>"` then a bare `disconnect` (`0x4e3f15`) | Same for protocol-91 clients |
| Pure failure wording | `Unpure client detected. Invalid .PK3 files referenced!` | Same |
| Packet pacing while downloading | Keyed on the pending Workshop download (`0x4b5e7e`) | Workshop downloads count as downloading |
| Rail color defaults, `teamtask` | `color1` "7" and `color2` "25", each bounded to `[1, 26]`; `teamtask` "0" in userinfo (`0x4bcb44`, `0x4bcb62`) | Same defaults and bounds; `teamtask` registered |

## Deliberate differences

- **Retail post-process pipeline.** `r_enablePostProcess`, `r_enableBloom`,
  `r_enableColorCorrect`, the `r_bloom*` tuning cvars, `r_contrast`,
  `r_floatingPointFBOs` and the `*Active` mirrors stay unimplemented, as
  [`RENDERER_CVAR_COMPATIBILITY.md`](RENDERER_CVAR_COMPATIBILITY.md) records.
  Related and newly identified: cgame imports 114 and 115 are the two render
  commands retail cgame brackets its scene with (`0x43cba0`, `0x4384d0`), not
  the advertisement bookkeeping FnQL names them after. They do nothing useful
  without that pipeline, so they are left as they are.
- **`clearcvar` does not force.** Retail forces the write. FnQL uses the
  ordinary user path so a config or stuffed command cannot blank read-only,
  init-only or cheat-protected state.
- **`s_muteBackground` applies live.** Retail latches it to a sound restart
  because it is a buffer creation flag.
- **`arena` drops the active factory** before spawning, as FnQL's
  single-player map aliases do.
- **`setenv` is not implemented.** Retail keeps Quake III's command, which lets
  console text change the process environment (`0x4b8b30`). ioquake3 removed
  it for that reason; nothing in the retail data uses it.
- **Console look.** Retail `con_opacity`, `con_background`, `con_scale` and
  `con_speed` are covered by FnQL's own console cvars, and `com_idleSleep` by
  `com_yieldCPU`.
- **Crash prompt.** Retail tracks `com_crashed`/`com_pid` in `profile.pid` and
  offers safe mode in a modal dialog at start-up (`0x4cc1f9`). FnQL has
  `+safe` and never shows a blocking dialog.
- **Supersets.** The retail server answers only `getchallenge`, `connect` and
  `disconnect` out of band. FnQL's `getinfo`, `getstatus`, `rcon`, download
  commands and legacy master heartbeats stay.
- **No-Steam challenge.** Retail answers a `getchallenge` without a ticket
  with `print\nNo Steam auth token.\n` (`0x4df5af`). FnQL issues a challenge,
  because its no-Steam lane is a supported fallback, and refuses at `connect`
  only when a provider is present.
- **Server defaults kept from FnQ3.** `sv_floodProtect` stays a boolean with
  FnQL's burst limiter, where retail counts commands in a leaky bucket and
  drops the client past `sv_floodProtect` ("10") with "Flooding the server"
  (`0x4e020c`). `sv_timeout` stays 200 where retail uses 40 (`0x4e3d3a`), and
  `sv_maxclientsPerIP` (3) has no retail counterpart. All three are pinned by
  `server_cvar_compat_source_tests.py` as retained extensions.
- **Client extras a retail server can see but ignores.** The userinfo carries
  `snaps`, `team_model`, `team_headmodel`, `cl_guid` and, in the connect
  packet, `client`; retail sends none of them. `com_maxfps` allows more than
  retail's 30 to 250, so more usercmds per second can arrive. FnQL's own
  servers use `snaps`, `cl_guid` and `client`.

## Open gaps

Ordered by how much a player or operator would notice. None is plugged yet.

### Would be noticed

- **Listen-server achievements.** With `sv_serverType 0` retail stores
  achievements through the local user's stats interface (`0x467ee9`). FnQL
  requires a game-server ticket session, which the loopback client never
  has, so offline and listen-server achievements are not stored. *Reported.*
- **`vid_restart fast`.** Retail re-modes in place and calls cgame export
  `EVENT_HANDLING(4)` and UI export 10 (`0x4bb7e0`); FnQL reloads both
  modules. Costs a reload and cgame state on every fast restart. *Reported.*
- **View listener.** The Awesomium adapter wires no view callbacks, so
  tooltips (`0x434450`), cursor changes (`0x431670`) and JavaScript console
  output under `web_console` (`0x434520`) never arrive. *Reported.*
- **Synchronous `FileExists`.** Retail answers at once; FnQL answers from a
  cache and then asks, so the first call for a match's screenshot or demo is
  always false and the page caches it. *Reported.*
- **`asset://ql/screenshot/<file>`.** Retail serves the screenshots folder to
  the page (`0x434620`); FnQL does not, so match screenshot previews are
  missing. *Reported.*
- **Steam rich presence and history.** `game.start` in retail also sets the
  `status` and `lanIp` rich-presence keys and adds the server to Steam's
  history (`0x4f38f0`). FnQL only publishes the event. *Reported.*
- **Steam heartbeats.** Retail calls `EnableHeartbeats( sv_master == 1 )`
  when the server starts and `EnableHeartbeats( 0 )` at shutdown
  (`0x465db0`, called from `0x4e32f7`, `0x4e3aac`, `0x4e402b`), so
  `sv_master 0` keeps a server off Steam's list. The provider ABI has no
  heartbeat switch and always advertises; this needs a provider capability.

### Join path leftovers

None of these stops a join. All are *reported* by the second pass and were
not re-read unless an address is given as verified above.

- **Steam auth failure handling.** Retail drops with
  `Failed to authenticate with Steam: <reason>` from a nine-entry table
  (`0x465ceb`, `0x560e28`), ignores one response code, and has no timeout.
  FnQL says "Platform authentication failed" and drops after ten seconds
  without an answer. The reason needs a provider field.
- **Connect-time wording.** A bad challenge is "Incorrect challenge, please
  reconnect." where retail says `No or bad challenge for address.`; the
  reconnect limit prints "Reconnecting, please wait" where retail is silent;
  the protocol mismatch text adds the client's version. Retail's own strings
  are stored obfuscated in a table at `0x564a68`.
- **Challenge lifetime.** Retail keeps one challenge per address in 1,024
  slots and re-sends it while the address is unconnected; FnQL's stateless
  challenge expires after 16 to 32 seconds.
- **Snapshot pacing.** Retail sends a non-active client at most one message a
  second, never reads `snaps`, and clamps `rate` to 4,000-50,000 (`0x4e5a60`,
  `0x4df369`). FnQL sends loading and zombie clients at `sv_fps`, honours
  `snaps`, and leaves `rate` unclamped.
- **Slot choice.** Retail takes the first free slot; FnQL the least recently
  used, so client numbers differ after churn.
- **P2P greeting.** Retail sends a reliable `hello` on Steam channel 16 right
  after starting the auth session (`0x467d2b`); FnQL first contacts the client
  with its keepalive once it is active, so voice may start later.
- **Name buffer.** Retail holds 40 bytes for a client name, FnQL 32, so drop
  and kick notices truncate a long name sooner.
- **`cp` order.** Retail runs its download step before the filesystem restart
  and sends `cp` again after a restart, so a fresh join sends two; it also
  sends `cp` when `sv_pure` is 0. FnQL sends one, and none when `sv_pure` is
  0. The 2026-07-17 live join passed with FnQL's order.
- **Local refusals.** Retail refuses locally with "Base game files differ from
  server." when `pak00` or `bin` is not in the server's pak list, and with
  "cannot connect to server with unpure binaries". FnQL connects and is
  dropped as unpure instead.
- **Client message flag byte.** Its low five bits are, in retail, a count of
  depth-hacked entities drawn; FnQL sends a leaf count from three renderers
  and zero from GLx. The server code inside the retail client discards it.
- **Entity parse ring.** FnQL keeps 8,192 parsed entities where retail keeps
  2,048, so it accepts deltas retail would discard.

### Smaller

- `find <substring>` console search with `con_matchlimit` (`0x4b3630`), and
  `con_noprint`.
- `ListInputDevices` for raw input (`0x4eab90`).
- User scripts: retail runs every `js/*.js` from the game filesystem once the
  page is ready (`0x4317f0`). *Reported.*
- `GetDemoList` is bounded to a 4096-byte document; retail's is unbounded.
  *Reported.*
- Mouse 4 and 5 as browser back and forward (`0x4f34f7`). *Reported.*
- Lobby join errors use one message; retail picks one of ten by Steam's
  enter response (table `0x55fb30`), which the provider event does not carry.
- cgame `EVENT_HANDLING(5)` when the browser hides (`0x4f255b`). *Reported.*
- `winkey_disable`: a low-level keyboard hook that swallows the Windows keys
  (`0x4ec580`). Nothing in the retail data sets it.
- UI imports 5, 97 and 98 have retail shapes (`Cvar_RegisterRange`,
  `IsClientMuted`, `ToggleClientMute`) that differ from FnQL's. The retail
  UI never calls them; FnQL-game's UI must be checked before they move.
  *Reported.*
- Small `web_*` differences: `web_showBrowser` without an argument resets the
  route, `web_showError` does not show the browser, `web_reload` does not
  clear the cache first. `IsGameRunning` is false while connecting and during
  cinematics, where retail's is true whenever the client is not disconnected
  (`0x43292b`).
- The client keeps newlines in a stored out-of-band `print` message; retail
  zeroes them (`0x4bc0c9`). *Reported.*

### Needs a decision rather than code

- On a server with no Steam provider, the SteamID query trusts the
  `steamid`/`steam` userinfo keys. Retail never reads them. With in-band
  admin `rcon` this is a spoofable identity on a Steam-less server. *Reported.*
- Whether to send a retail server only retail's userinfo keys (see "Client
  extras" above). It would hide the fingerprint, at the cost of dropping keys
  FnQL's own servers use unless the two can be told apart before `connect`.
- A `%` in a reliable command. Retail's client turns it into `.` when it
  reads the string back and then hashes that copy for the usercmd key, while
  the server hashes the raw text (`0x4d5075`, `0x4b618f`, `0x4e03e1`), so
  retail garbles its own usercmds after such a command. FnQL hashes the raw
  text and does not. *Reported.*

### Not behavior

`fs_cdpath`, `fs_copypath`, `net_noudp`, `net_strict`, `win_hinstance`,
`win_wndproc`, `r_logFile`, `r_measureOverdraw`, `r_ignore`, `in_debugMouse`,
`in_raw_useWindowHandle`, `sys_cpuid`, `username`, `net_fakevacban`,
`com_dropsim` and `bot_teamkill` are legacy or debug names with no observable
effect worth owning. `g_levelStartTime` is also set by the engine at spawn
(`0x4de854`) but qagame overwrites it.

## Validation

- `tests/retail_parity_gap_source_tests.py` pins every plugged item;
  `fnql_arena_script`, `fnql_client_cvar_compat` and `fnql_server_cvar_compat`
  cover the new policies.
- MSVC Win32 builds of `meson/build/win32` (SDL) and `meson/build-x86-nosdl`
  (native backend) completed without warnings.
- `python -m pytest`: 1,287 passed, 25 environment skips. Meson: 81 passed,
  one retail-probe skip. (After the first pass: 1,277.)
- Headless runtime probes against the retail installation passed: the qagame
  provider probe, the headless launch tests, the retail factory probe, the
  retail cgame module probe on a default (offline) listen server, and the
  synthetic cgame ABI probe.

Not run: FnQL-game's lockstep differential suite, a live retail client joining
an FnQL server, an FnQL client joining a live retail server after these
changes, and a Steam server-browser check of the new identity strings. The
join-path fixes are therefore verified by source gates, the unit tests and the
loopback runtime probes, not by a networked session: on the development
machine a networked client cannot obtain a Steam ticket, and an FnQL server
with a Steam identity rightly refuses it.
The lockstep recordings will change where a scenario uses `ragequit`, console
`say` or a bot SteamID; both modules run on the same engine, so they move
together.
