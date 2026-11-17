# r/QuakeLive (and Steam hub) — server operators post

**When:** about a week after launch. **Type:** text post. Cross-post the same
body to the Steam Quake Live hub's server/admin discussion area, converted to
Steam BBCode (headings become `[h2]`, lists `[list][*]`).

---

## Title

Running a Quake Live server? FnQL's dedicated server is open source, hardened, and retail clients join it as normal

---

## Body

If you host Quake Live servers, FnQL might be worth a look. It's an
open-source (GPL-2.0) engine for retail Quake Live, and its dedicated server
is the part I'd most like admins to try.

**Retail clients just join.** Players on the stock Steam client connect to an
FnQL server and receive exactly what a retail server sends. FnQL players can
also negotiate extras with each other that retail peers never see.

**What's in it for admins:**

- **Security fixes:** damaged or hostile maps, models, TGA images, RoQ videos
  and Ogg/WAV sounds in downloaded packages are refused at load instead of
  corrupting memory. Match statistics can no longer push script into the
  menu browser.
- **Stability:** no crash when `killserver` or a failed map change arrives
  over rcon; no ghost player after a refused Steam-ticket reconnect; Steam
  player listings and voice keepalives survive `sv_idleRestart`; changing
  `sv_maxclients` live no longer trips up the Steam integration.
- **Fair access lists:** players without a Steam identity each get their own
  anonymous id, so banning one no longer bans all of them, and nobody can
  spoof their SteamID through userinfo.
- **Stats without libzmq:** `+set sv_statsJournal matches.jsonl` appends every
  stats publication (kills, deaths, medals, player stats, match report) as
  one JSON document per line in the home game directory.
- **ZMQ rcon that doesn't lock you out:** monitoring tools on short-lived
  connections no longer exhaust the peer table.
- **Linux friendliness:** clean shutdown on SIGTERM/SIGINT with
  `sv_idleExit 0`; the release is i686, so it can load retail's
  `qagamei386.so` directly when you choose the retail module.
- **Your choice of game module:** the open-source FnQL-game module is the
  default; `+set vm_gameProvider retail` keeps retail's.
- **More hardware:** the dedicated server builds from source for Linux
  aarch64, armv7l (hard-float), ppc64le and ppc64 with FnQL-game's modules.
  No packages yet; Meson builds them.

**Quick start (Linux):**

```
tar -xzf fnql-*-linux-x86.tar.gz -C "<Quake Live folder>"
cd "<Quake Live folder>"
./fnql.ded +set fs_steampath "$PWD" +set dedicated 2 +map campgrounds ffa
```

Install the 32-bit runtime libraries `ldd ./fnql.ded` asks for first.

Download: https://github.com/themuffinator/FnQL/releases/latest
Server notes: https://github.com/themuffinator/FnQL/blob/main/docs/fnql/DEDICATED_SERVER_REPLACEMENT.md

If you run one publicly, tell me and I'll list it. Issues and logs welcome:
https://github.com/themuffinator/FnQL/issues

*Not affiliated with or endorsed by id Software, Bethesda, ZeniMax or Valve.
Retail game files come from your own Steam install.*
