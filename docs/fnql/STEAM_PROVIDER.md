# FnQL Steam provider boundary

FnQL integrates Steam through an optional dynamic provider. The provider source
is maintained in a separate, closed-source sibling repository, normally
`../FnQL-Steam`; no provider implementation, Steamworks SDK file, Steam
redistributable, credential, ticket, or retail asset belongs in this repository.
FnQL contains only the versioned C ABI, its secure loader, engine-facing
adapters, tests, and documentation.

## Compatibility and policy

- Retail Quake Live AppID `282440` is the compatibility target.
- Steam install discovery and retail filesystem mounting do not depend on the
  provider. A missing, disabled, mismatched, or failed provider cannot change
  `fs_basepath`, the active-user homepath, pak ordering, native-module lookup,
  or WebUI datapak mounting.
- The provider is default-off. Opt in with
  `+set com_steamIntegration 1`; this is an initialization cvar and must be set
  on the command line.
- FnQL loads the provider only from an absolute path or from a bare filename in
  the executable directory. The provider, in turn, loads the exact retail
  `steam_api` path supplied by FnQL. Neither layer uses the current directory or
  a broad DLL search for its primary library.
- ABI structs are fixed-width, size-tagged, bounded, and C-compatible. Every
  capability is checked against its required function table before FnQL exposes
  it.
- Failure is deterministic. The existing legacy server browser, local identity,
  asset-only Workshop behavior, master publication, and unsupported WebUI event
  paths remain available when a provider operation is absent.
- When Steam was explicitly enabled but its provider or API cannot load, FnQL
  prints one non-fatal explanation and continues in non-Steam mode. Retail asset
  mounting, local play, legacy server discovery, and non-Steam hosting remain
  available; repeated service refreshes do not spam the message.

## Development workflow

Create or obtain the private sibling at `../FnQL-Steam`. The repository builds
without Steamworks headers by resolving the flat exports of the user's
legitimate Steam redistributable. To build and stage it beside a Win32 FnQL
build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .vscode/build-release.ps1 `
  -Platform Win32 -Configuration Debug -BuildDir meson/build/win32-debug `
  -Target both -Renderers opengl,glx,vulkan,opengl2 -WithSteam
```

`FNQL_STEAM_REPO` may name a different sibling path. The VS Code task
`meson: build Win32 debug + Steam` performs the same operation, and the launch
configuration `OpenGL + Steam (Retail QL / Win32)` keeps the game windowed and
opts in to Steam. Only `fnql_steam.dll` is staged into the build tree; private
source is never copied into FnQL.

Runtime controls and diagnostics:

- `com_steamIntegration`: explicit `0`/`1` initialization policy.
- `com_steamProvider`: protected provider filename or absolute path.
- `com_steamApi`: protected absolute Steam API override for controlled tests;
  blank uses the API in the detected retail Quake Live install.
- `steam_status`: reports the provider, ABI status, roles, capabilities, local
  identity, overlay state, and GameServer state without exposing tickets or
  private data.
- `sv_steamSecure`: opt-in authenticated-and-secure GameServer mode. The default
  remains the existing unauthenticated compatibility lane until ticket handling
  is proven end to end.

## Implemented service surface

The first ABI covers client identity and subscription state, overlay URLs and
users, rich presence, Steam callbacks, friends lobbies, lobby chat and invites,
Internet/LAN/favorites/history/friends server lists, Workshop subscription and
install snapshots, download progress/actions and asynchronous all-UGC queries,
user stats requests, auth tickets and sessions, all three friend-avatar sizes,
and dedicated GameServer
initialization/publication/shutdown.

Client `SteamUserStats` readback is a separate capability from authoritative
GameServer stat updates. A successful per-user callback rebuilds the retail
`users.stats.<steamid>.received` document from the shared 88-field/59-
achievement contract, including bounded display names, descriptions, unlock
state, and unlock timestamps. `stats_clear` retains the explicit retail reset
command; it is never invoked automatically. Role-specific request entry points
prevent a listen server from accidentally routing a browser request through
the GameServer stats interface.

Lobby entry projects the bounded owner, member limit, member identities and
persona names, plus indexed key/value metadata into retail's
`lobby.<id>.enter` document. Chat and membership events resolve persona names
instead of emitting anonymous placeholders. Menu invites use the active lobby;
in-match direct invites are allowed only after the current server's FnQL Steam
identity configstring has been validated, so this path cannot advertise a
retail-operated server.

The legacy GameServer master-server UDP bridge is capability-gated separately
from P2P. Every IPv4 server packet is offered to Steam without consuming the
normal FnQL packet path, while outgoing Steam datagrams use caller-owned
1 KiB buffers and a 64-packet per-frame ceiling. Missing exports or an
uninitialized GameServer leave the portable socket path unchanged.

GameServer metadata is independently gated as well. On startup/connect FnQL
publishes bounded `CVAR_SERVERINFO` key/value pairs; once per second it refreshes
team scores, active player name/score rows, and the live bot-player count. Bots
receive Steam's local
unauthenticated identities only after the GameServer interface is initialized,
and those identities never satisfy FnQL's authenticated-client admission test.

Steam also owns account-level favorites and native server details when their
separate capabilities are present. Favorite add/remove preserves the retail
AppID, connection/query ports, favorite flag, and last-played timestamp, then
mirrors the result into FnQL's local cache so offline browsing remains useful.
Detail requests retain three independently terminal channels: ping metadata,
player rows, and rule key/value pairs. All callback strings are copied into
bounded event storage, replacement and timeout cancel outstanding query
handles, and a failed Steam ping falls back to the legacy Quake status query.
No detail callback is allowed to redirect or connect the client.

Steam purchase creation remains outside the engine, but the provider owns the
retail `MicroTxnAuthorizationResponse_t` callback. It preserves the AppID and
full 64-bit order ID, normalizes the authorization byte, and publishes the
retail-shaped `microtxn.authorization` WebUI object with `appid`, string-valued
`orderid`, and integer `authorized` fields. This callback lane is advertised
separately so a provider cannot imply purchase-result support merely by loading
the client core.

Legacy retail P2P is isolated behind distinct client, GameServer, and voice
capabilities. The provider exposes bounded send/peek/read operations and
role-tagged request/failure callbacks, but never accepts a peer automatically.
FnQL publishes the logged-on GameServer SteamID in retail configstring `0x2ca`.
The client accepts a request only when its full 64-bit peer ID matches that
active-server configstring; the server accepts only a `CS_ACTIVE` client whose
Steam ticket has completed successfully. The server rechecks that identity on
every channel-1 packet, caps work per frame, tags and relays compressed voice
through the qagame suppression predicate, and closes unknown senders. FnQL
clients decode bounded mono PCM only from the tracked server and still apply
the local SteamID mute set. Channel 16 keeps authenticated retail sessions
alive. This preserves the retail voice lane without allowing P2P callbacks to
become an alternate route into retail-operated servers.

Voice capture is user-controlled through `+voice` and `-voice`; the provider
also mirrors that state to Steam Friends' in-game speaking indicator. Channel 0 is
independent of the optional voice capability: it accepts at most 1 MiB of
zlib-compressed JSON from the authenticated FnQL server and publishes the
decoded document as `game.stats.report`. The protocol-session probe's
`--expect-steam-fixture` mode regression-tests deferred peer admission, the
stats sidecar, and compressed voice transport with the deterministic fake API.

The social snapshot uses Steam's immediate-friend enumeration with a consistent
flag mask and returns bounded, size-tagged records containing the full 64-bit
identity, persona state, relationship, optional nickname, current game/lobby,
server endpoint, and the retail `status`, `lanIp`, and `connect` rich-presence
keys. Persona and friend-rich-presence callbacks refresh a single record and
publish the retail-shaped `users.persona.<id>.change` and
`users.presence.<id>.change` events. When the capability is absent, the prior
in-server identity list remains the deterministic fallback.
The provider forwards callback data through bounded event records on FnQL's
calling thread.

Lobby receive coverage includes create/enter/leave, member state, bounded chat
retrieval, metadata changes, game-server creation, kicks, and friend join
requests. Workshop install/download callbacks are filtered against AppID
`282440` before they reach the engine and include bounded install metadata when
Steam reports it. Callback shutdown unregisters every object before unloading
the retail API, and restart tests guard against duplicate registration.
Subscribed-item snapshots also expose Steam's item-state bitmask, installed
folder, size, and update timestamp. The WebUI can therefore distinguish content
that is subscribed, installed, stale, downloading, or pending without treating
a subscription alone as permission to access its files.

The retail `getallugc` bridge is a genuine asynchronous UGC query rather than a
subscribed-item alias. FnQL passes the retail numeric filter through the open
ABI, the provider creates and sends an all-UGC request scoped to AppID `282440`,
and a call-result object owns the native query until completion. At most 50
title/description/preview records are copied into provider-owned, size-tagged
storage before the native query is released. The engine then builds bounded
retail-shaped JSON and publishes `web.ugc.results` followed by
`web.ugc.complete`. Replacement requests and shutdown unregister and release a
pending query; providers without this appended capability retain the older
subscribed-item snapshot as a deterministic fallback.

Avatar transfer is also bounded and ownership-safe. The provider resolves
small, medium, and large avatar handles, reports asynchronous
`AvatarImageLoaded_t` completion,
and copies RGBA pixels into an engine-owned buffer after validating dimensions
and required size. FnQL registers that buffer through the common renderer API
implemented by the OpenGL, GLX, Vulkan, and OpenGL2 modules, caches the shader
by full 64-bit SteamID, and invalidates the cache on avatar callbacks and
renderer shutdown. Both `asset://steam/avatar/large/<id>` and the retail-style
`steam://avatar/large/<id>` resource spellings use the same bridge.

Server stat ownership is asynchronous. FnQL loads provider baselines before
applying retail-mapped event deltas, submits dirty fields and achievements, and
keeps them dirty until a successful `FNQL_STEAM_EVENT_USER_STATS_STORED`
completion for the same generation. Failed or stale completions retain newer
changes for retry. Match reports are strictly validated and can be published to
the optional ZMQ sink independently of provider availability.

Authentication is asynchronous as well. A successful `BeginAuthSession` only
accepts a ticket for validation; the client remains unverified until the
provider forwards Steam's matching `ValidateAuthTicketResponse_t`. Rejections
drop the client and responses that never arrive hit a bounded server timeout.
An opted-in FnQL client sends its provider ticket in a bounded binary challenge
extension carrying the normal FnQL nonce. The server echoes the FnQL handshake
marker before the client proceeds, so adding authenticated FnQL-to-FnQL play
does not create a route for FnQL clients to join retail-operated servers.

The target retail `steam_api.dll` does not export the later Web API auth-ticket
entry point. FnQL therefore does not advertise or synthesize that capability;
session tickets remain the supported retail authentication contract. Likewise,
the GameServer float and average-rate stat overloads are not exercised by the
observed Quake Live table: all 88 mapped fields are integer descriptors. They
are deliberately not substituted into the active integer update lane.

Retail qagame passes report/event data as an opaque `Json::Value` owned by its
MSVC/JsonCpp runtime. FnQL never dereferences that foreign C++ object. JSON side
effects require an optional provider-side serializer compiled for the matching
ABI; without one, the engine logs the unavailable capability and safely defers
those side effects.

The retail Windows Quake Live redistributable is x86. Win32 is therefore the
retail-compatible provider lane. The sibling also compiles x64 to keep the ABI
and implementation portable, but it requires an administrator-supplied x64
Steam redistributable; an x64 process cannot load the retail x86 DLL.

An active Steam user is required for live client initialization. A running Steam
process with no logged-in `ActiveUser` is an expected unavailable state, not an
engine or filesystem failure. The sibling's strict tests use an original fake
flat-API library to verify ABI negotiation, capability filtering, identity,
subscription, overlay forwarding, and clean teardown without credentials or
network access.
