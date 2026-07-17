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
- Steam integration is enabled by default. Use
  `+set com_steamIntegration 0` to explicitly select the provider-free
  fallback; this is an initialization cvar and must be set on the command line.
- FnQL loads the provider only from an absolute path or from a bare filename in
  the executable directory. The provider, in turn, loads the exact retail
  `steam_api` path supplied by FnQL. Neither layer uses the current directory or
  a broad DLL search for its primary library.
- ABI structs are fixed-width, size-tagged, bounded, and C-compatible. Every
  capability is checked against its required function table before FnQL exposes
  it.
- Failure is deterministic. The existing legacy server browser, local identity,
  master publication, ordinary pak validation, and unsupported WebUI event
  paths remain available when a provider operation is absent. A failed or
  temporarily unavailable Workshop refresh does not erase the last snapshot
  that the engine registered successfully during the current process.
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
`meson: build Win32 debug (Steam)` performs the same operation. The standard
`Retail QL / Win32` launch entries run the matching release build directory
(`meson/build/win32`); every launch keeps the game windowed and explicitly
enables Steam, but never starts a build task. Run `meson: build (Steam)` before
those launch entries. Use `meson: build Win32 debug (Steam)` only when manually
launching the separate debug build directory.
Only `fnql_steam.dll` is staged into the build tree; private source is never
copied into FnQL.

Runtime controls and diagnostics:

- `com_steamIntegration`: initialization policy, enabled by default; set `0`
  explicitly to disable the provider.
- `com_steamProvider`: protected provider filename or absolute path.
- `com_steamApi`: protected absolute Steam API override for controlled tests;
  blank uses the API in the detected retail Quake Live install.
- `steam_status`: reports the provider, ABI status, roles, capabilities, local
  identity, overlay state, and GameServer state without exposing tickets or
  private data.
- `sv_steamSecure`: opt-in authenticated-and-secure GameServer mode. The default
  remains the existing unauthenticated compatibility lane until ticket handling
  is proven end to end.

## Workshop filesystem, server, and client wiring

The compatibility contract in this section comes from static comparison with
the QLSRP corpus and retail-observed Quake Live behavior. FnQL's implementation
is an independent engine-side rewrite around the provider ABI; it does not
import a Steamworks implementation or reconstruct game code. The observation
and the implementation are kept separate below so an unperformed live probe is
not presented as retail evidence.

### Observed retail contract

- Retail bounds both its subscribed-item snapshot and a server-required item
  list at `0x100` (256) entries. Item identities are complete unsigned 64-bit
  decimal values.
- Workshop sources enter the filesystem before the normal install, base, and
  profile roots. Since later sources are prepended to the Quake search path,
  ordinary FnQL/retail roots keep priority over Workshop content. Later
  Workshop items keep priority over earlier items, and later-sorted archives
  within a source keep the usual pak priority.
- Installed retail items use both item-root and item-local `baseq3` layouts. If
  both exist, the item-local `baseq3` content has priority over the same item's
  root content.
- A server reports only Workshop items whose archives were actually referenced,
  in filesystem search order, as exact de-duplicated decimal IDs with retail's
  trailing-space spelling. Retail carries this list in configstring `0x2cb`.
- A client handles required Workshop content before ordinary pak comparison,
  requests missing items one at a time with high priority, restarts the
  filesystem once after the Workshop pass, and then resumes the normal download
  path. A server-required item need not be one of the user's subscriptions.
- Retail UI import 96 reports live per-item downloaded and total byte counts.

### FnQL filesystem ownership and ordering

The provider supplies a bounded subscription snapshot and install metadata; it
never supplies filesystem policy. FnQL transactionally validates and registers
the snapshot, then the filesystem owns all mount order, path containment,
reference tracking, and restart decisions. A successful empty snapshot clears
the subscribed set. An unavailable or failed provider call leaves the previous
successful set intact instead of treating a service outage as an unsubscribe.

Core filesystem initialization remains independent of Steam. Immediately after
provider initialization and the early GameServer bootstrap, the common Workshop
adapter captures its first usable snapshot and reloads the filesystem if the
registered set changed; every such startup then adds the registered Workshop
sources before the normal roots. Failed snapshots and subscribed entries that
are not installed remain pending instead of being mistaken for a completed
empty set. Authoritative snapshots retain a slow periodic poll even when event
delivery is available; callbacks only accelerate that check. Provider
reconfiguration resets an owned manual transfer and schedules a fresh snapshot,
the normal callback pump precedes per-frame Workshop polling, and shutdown
removes the Workshop observer before unloading the provider.

FnQL retains at most 256 subscribed installs and, independently, at most 256
transient installs. Transient entries are downloaded because a server required
them while they were not subscribed; they survive provider snapshot refreshes
and filesystem restarts for the lifetime of the process. This prevents the
post-download restart from losing the content before ordinary pak validation.
Neither registry is serialized by FnQL.

Every registered source is read-only and must have a nonzero item ID, a bounded
and terminated absolute install folder below a filesystem root, no traversal
component or control character, and no folder already owned by a different
item. A rejected registration is distinct from an unchanged valid row, so a
bad provider path cannot be reported as a cache hit. FnQL mounts the validated
item root and its `baseq3` child when present. It does not scan arbitrary
Workshop directories, change `fs_basepath` or `fs_homepath`, or use a Workshop
folder as a write target. The normal Steam/base/profile sources are added
afterwards and therefore retain higher priority.

Retail probes the exact readable marker `fs_basepath/baseq3/pak00.pk3` before
the `fs_skipWorkshop` and build-script gates. When that marker is absent, it
keeps registered Workshop roots available for loose-file reads but skips their
entire archive-enumeration branch. FnQL mirrors that order and behavior for the
item root and item-local `baseq3` root. A `pak00.pk3` supplied by a Workshop
item is tagged with its nonzero item ID and cannot satisfy the later retail-base
identity check; Workshop content therefore cannot authorize its own archive
mounting or masquerade as the legitimate Steam installation.

`fs_skipWorkshop` is a default-zero `CVAR_INIT` control for intentionally
suppressing registered Workshop mounts; set it on the command line when needed.
`com_buildScript` also suppresses those mounts so documentation, packaging, and
other deterministic build-script runs cannot absorb account-specific content.
These gates affect filesystem mounting, not provider initialization or the
subscription/download API itself.

### Reference publication and required downloads

Each mounted Workshop archive carries its item identity through the normal pak
reference machinery. Once map and module references have settled, a server
publishes the exact referenced list to read-only cvar
`sv_referencedSteamworks` and configstring `0x2cb`. Publication occurs after
server spawn and is repeated when the Steam GameServer connects, but it does
not depend on that connection or on configstring `0x2ca` having a GameServer
SteamID. A provider-free server can therefore publish correct Workshop
dependencies for content that the filesystem has already registered and
mounted.

On a non-demo connection whose negotiated protocol contract advertises
Workshop content, the client parses `0x2cb` as whitespace-separated, nonzero
decimal IDs. Legacy Quake III and ioquake3 connections never interpret that
configstring as Workshop metadata. The parser ignores exact duplicates and
rejects malformed, overflowing, and excess entries without accepting partial
numeric tokens. When the provider exposes the UGC capability,
already-installed items are registered immediately and missing items enter a
single high-priority download queue.
Completion callbacks are wake-up hints; polled item state and install metadata
remain authoritative, which covers providers that coalesce callbacks. A failed
item advances the bounded queue. Provider loss or an unavailable UGC lane falls
back to ordinary pak validation instead of treating the content as present.
The connection-required queue explicitly takes ownership from an outstanding
manual download before it changes the shared progress cvars. Subscription
snapshot reloads are deferred while that queue owns connection bootstrap, so a
late install callback cannot restart the filesystem mid-connection.

The automatic queue exposes `cl_workshopDownloadActive` and the retail-style
`cl_downloadItem`, `cl_downloadName`, `cl_downloadCount`, `cl_downloadSize`, and
`cl_downloadTime` progress values. UI import 96 queries live provider progress
first and retains those cvars as the deterministic fallback. After cached or
new installs have been registered, FnQL performs one checksum-feed-preserving
filesystem restart and resumes the pre-existing pak/download flow.

The operator commands accept exactly one nonzero decimal item ID:

- `steam_downloadugc <itemid>` requests one high-priority download, publishes
  the same progress cvars, applies a bounded timeout, registers the completed
  install, and reloads the filesystem when the registration changed.
- `steam_subscribeugc <itemid>` subscribes through the provider, then refreshes
  the subscribed snapshot only after Steam reports both subscribed and
  installed, avoiding a stale transient registration while state settles.
- `steam_unsubscribeugc <itemid>` unsubscribes and refreshes the future
  snapshot. As in retail, an existing mount remains valid until a later
  filesystem restart.

Subscribe and unsubscribe requests for different item IDs retain independent,
bounded settle state. These commands and automatic server-required downloads require
`FNQL_STEAM_CAP_UGC`; their presence in the console is not evidence that a live
provider supports them.

### Provider and platform limits

FnQL's ABI can host a UGC implementation on any supported platform, but it does
not synthesize Steam subscription or download success. The provider must expose
subscription enumeration, item state, install metadata, byte progress,
download, subscribe, unsubscribe, and bounded callback/event delivery for AppID
`282440`. FnQL starts the Steam GameServer before its initial Workshop snapshot
and revalidates the provider's dynamic status after that start and callback
pumping. Provider version 0.3.0 uses the Steam client UGC owner for client and
listen-server sessions, then mirrors retail's dedicated-server split by
acquiring `SteamGameServerUGC` only after successful GameServer startup.
Dedicated install/download callbacks and all-UGC call results use the
GameServer callback lane. Shutdown cancels any owned query, unregisters those
callbacks, clears UGC from the dynamic capability mask, and only then releases
the GameServer interfaces. A missing or null optional GameServer UGC export
leaves a GameServer-only process without UGC instead of initializing a hidden
client lane; ordinary filesystem and pak fallback remains available.

The legitimate retail Windows `steam_api.dll` exports both `SteamUGC` and
`SteamGameServerUGC`; a read-only audit of the user's local retail
redistributable reconfirmed both exports on 2026-07-17. The QLSRP ownership
mapping selects the latter for a dedicated process after GameServer
initialization. FnQL's provider and
fake-runtime regression now implement and exercise that inferred ownership
contract, including dynamic add/remove of the UGC capabilities and distinct
callback ownership. The provider-info mask still describes only what is usable
immediately after startup; each successful status snapshot is
runtime-authoritative as role ownership changes. FnQL validates the complete
UGC function table before exposing either mask. A live unattended retail
enumeration/download has not yet been performed, so that remains the promotion
gate rather than an observed success claim.

The same read-only export audit found all 142 flat symbols currently resolved
by provider 0.3.0 in the retail x86 redistributable. This proves symbol
availability only; it did not initialize Steam, log on a GameServer, enumerate
account content, or call a live service.

The legitimate retail Windows redistributable is x86, so Win32 remains the
validated retail provider lane. Linux, macOS, Windows x64, and unattended
dedicated UGC require a platform-appropriate administrator-supplied Steam
runtime/provider and their own live validation. No local subscription IDs,
install folders, account data, or retail assets are recorded in this document
or repository.

## Implemented service surface

The versioned capability matrix has 25 independently gated entries. Provider
version 0.3.0 implements and fake-runtime tests all 25 on the
retail-compatible Win32 lane, for **100% ABI capability completion**. The final
entry, `FNQL_STEAM_CAP_RETAIL_JSON`, is architecture-gated: Windows x64 and
portable builds keep it clear and return an explicit unsupported result while
retaining the deterministic engine fallback. This percentage measures the
implemented retail-Win32 provider surface; it does not turn the remaining
logged-in retail, unattended Workshop, or cross-platform live probes into
completed evidence.

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
in-match direct invites are allowed only after the active server's Steam
identity configstring has been validated. This supports both authenticated
FnQL hosts and retail-operated servers without allowing an unverified endpoint
to become a P2P target.

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
alive. P2P callbacks remain bound to the Steam identity published by the active
authenticated server and cannot redirect the ordinary UDP connection
handshake.

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
requests. The sibling filters Workshop install/download callbacks against AppID
`282440` before they reach the engine. Its snapshots expose Steam's item-state
bitmask plus bounded install metadata and progress, while FnQL retains sole
ownership of path validation and mounting as described above. Callback shutdown
unregisters every object before unloading the retail API, and restart tests
guard against duplicate registration. The WebUI can therefore distinguish
content that is subscribed, installed, stale, downloading, or pending without
treating a subscription alone as permission to access its files.

The retail `getallugc` bridge is a genuine asynchronous UGC query rather than a
subscribed-item alias. FnQL passes the retail numeric filter through the open
ABI, the provider creates and sends an all-UGC request scoped to AppID `282440`,
and a call-result object owns the native query until completion. At most 50
title/description/preview records are copied into provider-owned, size-tagged
storage. On a terminal callback the provider first captures the native query,
detaches its call-result, and clears the old shared in-flight ownership. Host
event delivery is synchronous and may start a replacement query re-entrantly,
so everything after that point refers only to the captured local handle.

Retail control flow then branches solely on the I/O-failure flag. A non-I/O
completion publishes `FNQL_STEAM_EVENT_UGC_QUERY_COMPLETE` while the captured
native query remains live, then releases that exact handle after event delivery
returns. A null callback payload is terminal but is not a failure by itself: in
the non-I/O lane it produces a zero-row successful completion. A non-OK raw
native result without I/O failure retains the same event-then-release ordering.
Only the I/O-failure lane releases the captured query first and then emits the
failed completion. This split preserves retail ordering without allowing
post-event cleanup to clear or release a re-entrantly installed replacement.

The copied snapshot remains available for the engine's bounded count-then-fetch
calls until replacement or shutdown. A replacement request and shutdown
likewise unregister and release the old request before changing ownership; the
canceled request emits no ambiguous completion. Providers without this appended
capability retain the older subscribed-item snapshot as a deterministic
fallback. The all-UGC query bit is not implied by GameServer download support
and stays client-owned unless a provider implements GameServer-owned call-result
registration and pumping too.

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
An FnQL protocol-91 client sends its provider ticket in the retail-observed
binary challenge layout: the out-of-band marker and `getchallenge ` command,
followed immediately by a little-endian 64-bit SteamID and the opaque session
ticket. There is no FnQL marker or client nonce in this retail wire form. A
bare `challengeResponse` is classified from the recorded request mode as
protocol 91, and the raw ticket bytes are erased once that response is accepted.
The provider ticket handle remains live until disconnect so asynchronous
server validation cannot be invalidated prematurely.

When identity or ticket acquisition is unavailable, FnQL sends retail's bare
text `getchallenge` fallback. It still negotiates protocol 91 from the request
mode and proceeds if the server permits unauthenticated clients; a server that
requires Steam remains authoritative and its refusal is displayed unchanged.
FnQL never fabricates a SteamID, ticket, or successful authorization. The
server retains its parser for the older marked FnQL extension so older FnQL
clients do not regress, but new clients use the retail packet shape.

The live Windows-x86 validation on 2026-07-17 loaded provider 0.3.0 with all
25 Win32 capabilities, sent the legitimate account's session ticket to retail
endpoint `168.100.161.119:27076`, accepted its bare challenge/connect responses,
completed pure and required-Workshop setup, loaded the retail cgame, and entered
the active match. The same endpoint did not respond to repeated bare challenges
when `com_steamIntegration=0`, demonstrating that the no-Steam path reaches the
remote authorization boundary without FnQL fabricating or locally rejecting a
session.

The target retail `steam_api.dll` does not export the later Web API auth-ticket
entry point. FnQL therefore does not advertise or synthesize that capability;
session tickets remain the supported retail authentication contract. Likewise,
the GameServer float and average-rate stat overloads are not exercised by the
observed Quake Live table: all 88 mapped fields are integer descriptors. They
are deliberately not substituted into the active integer update lane.

Retail qagame passes report/event data as an opaque `Json::Value` owned by its
VC10/JsonCpp runtime. The executable and qagame import `MSVCP100`/`MSVCR100`,
and the committed retail corpus shows a 16-byte tagged value backed by a
16-byte VC10 tree object with 48-byte nodes. Provider 0.3.0 independently
implements a read-only Win32 view of that observed layout instead of compiling
against or invoking the foreign C++ ABI. It copies input through checked
process-memory reads, validates every tag, pointer, tree relation, node count,
depth, cycle, string termination, and UTF-8 sequence, and writes compact JSON
only into the caller's bounded buffer. It never mutates or frees retail-owned
storage. Malformed input, inaccessible pages, overflow, and short output all
clear the destination and fail closed.

The adapter advertises `FNQL_STEAM_CAP_RETAIL_JSON` only in a 32-bit Windows
provider. Windows x64, Linux, and other architectures expose the function as an
explicit unsupported operation without claiming the capability. FnQL then
uses its existing safe deferral path. Fake-runtime fixtures exercise nested and
sparse containers, all scalar kinds, escaping and UTF-8, unreadable pointers,
unknown tags, cycles, and output exhaustion; the engine independently validates
the completed document before applying stats or publication side effects.

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
