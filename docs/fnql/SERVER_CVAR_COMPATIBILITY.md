# Server CVar Compatibility

## Scope

This note records the engine-owned retail Quake Live `sv_*` surface and its
boundaries. It does not reconstruct qagame behavior: retail modules continue
to own gameplay cvars such as `sv_warmupReadyPercentage` and their own
`sv_mapname` mirror. FnQL's existing server extensions remain supported.

## Retail Evidence

The static and windowed runtime probes used the user's legitimate Steam copy:

- executable: `quakelive_steam.exe`
- file version: `0.1.0.739`
- size: `2,107,904` bytes
- SHA-256: `C926FE9F6C851E00B3B9332E88903AD01F28FDD60454873891C0158F5DED1299`

The retail pre-map `cvarlist sv_*` surface contained:

```text
sv_altEntDir sv_cheats sv_cylinderScale sv_dumpEntities sv_errorExit
sv_floodProtect sv_fps sv_gtid sv_hostname sv_idleExit sv_idleRestart
sv_includeCurrentMapInVote sv_killserver sv_lanForceRate sv_mapChecksum
sv_mapPoolFile sv_master sv_maxclients sv_padPackets sv_pakNames sv_paks
sv_paused sv_privateClients sv_privatePassword sv_pure sv_quitOnEmpty
sv_quitOnExitLevel sv_reconnectlimit sv_referencedPakNames
sv_referencedPaks sv_referencedSteamworks sv_running sv_serverid
sv_serverType sv_showloss sv_tags sv_timeout sv_vac sv_zombietime
```

Static executable evidence additionally exposed `sv_setSteamAccount`; the
protected value is not printed by the normal retail listing. The retail
qagame DLL exposed `sv_warmupReadyPercentage` and `sv_mapname`, establishing
those as module boundaries rather than missing engine registrations.

A windowed listen-server probe loaded `campgrounds` with retail assets. With
`sv_dumpEntities=1`, retail wrote exactly `ents/campgrounds.ent` (18,528
bytes). `sv_altEntDir` selected a live virtual-filesystem override directory.
`sv_gtid` and the module's `sv_mapname` remained empty, so FnQL does not invent
an update rule for either. QLSRP was then used as behavioral and ownership
evidence only; all FnQL code is an independent implementation around its
existing typed server, filesystem, collision, and provider boundaries.

A final Win32 FnQL dedicated-server probe loaded the shipped retail
`qagamex86.dll`, started `campgrounds`, and exited normally. FnQL's entity dump
was byte-for-byte identical to retail (18,528 bytes, SHA-256
`1D6A1F5BD9CCD3AE8943789DF6BA741BC4D6A62445C55441AA6489DBDF62E992`). A
second launch loaded the same text through `customents/campgrounds.ent`, proving
the live `sv_altEntDir` path against the retail module.

## Resolved Engine Contracts

| Contract | FnQL behavior |
|---|---|
| Entity text | BSP text reaches retail qagame unchanged, including QL `advertisement` classnames. `sv_altEntDir` loads `<dir>/<map>.ent` through the virtual filesystem and falls back to BSP text. Absolute paths, traversal, platform separators, embedded NULs, and overlong paths are rejected. `sv_dumpEntities` writes the selected text to `ents/<map>.ent`. |
| Cylinder collision | `sv_cylinderScale=1.1f` scales only the vertical-cylinder radius at the collision owner. The value is bounded and has an early-tooling fallback. |
| Process lifecycle | `sv_idleExit`, `sv_quitOnEmpty`, `sv_idleRestart`, and `sv_quitOnExitLevel` use wrap-safe/one-shot timing and ignore bots when retail defines human activity. Map, map-restart, scheduled restart, idle restart, and inherited safety restarts share one exit-level policy. |
| Error lifecycle | `sv_errorExit=1` promotes recoverable server errors to fatal only while a server is running; mode `0` retains recovery and mode `2` always promotes. Common code still owns the unwind. |
| Publication | `sv_master` gates all configured legacy master heartbeats. FnQL still supports five `sv_masterN` endpoints and retains its cross-platform DNS/IPv4/IPv6 behavior. |
| Steam tags | `sv_tags` is appended to retail gametype and rules tags. Existing `sv_keywords` is retained as an additive FnQL input, and changed metadata is republished through the external provider. |
| Secure hosting | Retail `sv_vac` can veto secure mode. It is combined with the established opt-in `sv_steamSecure`, so this compatibility surface does not silently remove FnQL's unauthenticated fallback lane. |
| Server account | Protected `sv_setSteamAccount` crosses the size-tagged provider ABI only when configured. Providers advertise `FNQL_STEAM_CAP_GAME_SERVER_ACCOUNT` when they own login; otherwise FnQL reports `provider-unsupported` and never synthesizes success. |
| Identity/defaults | A provider persona replaces the inherited `noname` default with retail's `<persona>'s Match` form. Explicit non-default hostnames remain unchanged. `sv_gtid` remains a read-only empty engine publication until a compatible owner supplies evidence for a value. |
| Diagnostic compatibility | `sv_showloss` retains the retail registration. No packet behavior is invented because neither the shipped binary probe nor the reference evidence established a consumer. |

Factory/map-pool owners already cover `sv_mapPoolFile`,
`sv_includeCurrentMapInVote`, and `sv_serverType`. Common/filesystem owners
already cover the read-only running, pause, checksum, pak, and reference
publications. The engine continues to register `mapname`, rather than creating
a competing `sv_mapname` owner.

Names found only in evolving reconstruction notes, but not in the shipped
retail executable/runtime evidence, are not treated as retail requirements.
This prevents speculative cvars such as reconstructed service-replacement or
ping/auth toggles from displacing FnQL's existing, tested policy surfaces.

## Preserved FnQL Behavior

The compatibility slice does not remove or reset established extensions,
including `sv_allowDownload=1`, `sv_audioPVS`, `sv_autoRecordDemos`,
`sv_dlRate`, boolean `sv_floodProtect=1`, `sv_levelTimeReset`, rate controls,
per-IP limits, Steam secure opt-in, Workshop references, ZMQ, rankings status,
or the inherited empty-server time-maintenance restart. Retail defaults replace
only confirmed missing retail owners.

## Regression Gates

1. `server_cvar_compat_tests` exercises exit/error policy selection, one-shot
   inactivity and 32-bit clock wrap, cross-platform entity-path rejection, and
   bounded retail-plus-FnQL Steam tag construction.
2. `server_cvar_compat_source_tests.py` inventories observed registrations,
   defaults and sensitive flags; pins qagame ownership; checks every runtime
   integration point; and protects representative FnQL defaults/extensions.
3. Strict-warning MSVC x86 and x64 builds compile and link both the full client
   and true dedicated server. Runtime validation mounts legitimate retail
   assets and always uses `+set r_fullscreen 0`.
