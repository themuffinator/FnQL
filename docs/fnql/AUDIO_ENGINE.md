# Modern Audio Engine

FnQL's modern audio engine is a client-side rendering upgrade around the
retail Quake Live sound contract. Game code, modules, demos, network protocol,
filesystem behavior, and accepted asset formats stay on the classic surface;
the modern path changes how the client renders sounds after the game has already
chosen what to play.

The default backend is OpenAL. The original software mixer remains available as
`s_backend legacy` and is still the deterministic fallback when OpenAL cannot
start.

## Design Goals

- Preserve retail Quake Live behavior first. Spatial features must be additive,
  optional where appropriate, and observable through diagnostics.
- Keep player-facing controls stable. `s_backend`, `s_backendActive`,
  `s_volume`, `s_musicVolume`, `s_doppler`, OpenAL device/HRTF/output cvars,
  and the `s_info`/`s_list`/`snd_restart` workflow are compatibility surfaces.
- Use OpenAL Soft capabilities when present, but treat HRTF, output mode,
  source counts, direct-channel routing, EFX, latency clocks, and live device
  recovery as runtime capabilities rather than guarantees.
- Keep server, protocol, VM, demo, and asset loading behavior out of scope for
  audio rendering changes.
- Make advanced map tuning data-only. `.azb` audio-zone sidecars refine
  environment rendering, but missing or invalid sidecars must never break a map.

## Source Layout

Audio is now organized as one client-owned module:

```text
code/client/audio/
    snd_public.h              public S_* sound API used by the client
    snd_local.h               private shared sound types and backend contract
    snd_main.cpp              cvars, commands, backend selection, fallback

    legacy/
        snd_dma.cpp           original software mixer backend
        snd_mix.cpp           paint/mix path
        snd_mem.cpp           sample cache and memory path
        snd_adpcm.cpp         legacy ADPCM helper
        snd_wavelet.cpp       legacy wavelet helper

    codecs/
        snd_codec.h           codec interface used by both backends
        snd_codec.cpp         codec registry and stream helpers
        snd_codec_wav.cpp     WAV loader
        snd_codec_ogg.cpp     optional Ogg Vorbis loader

    openal/
        AudioSystem.cpp       modern backend translation unit
        AudioSystem*.inl      private OpenAL implementation slices

    shared/
        AudioDeviceRecovery.h device-loss policy
        AudioOcclusion.h      occlusion smoothing policy
        AudioVoiceQueue.h     retail remote-voice lane policy and legacy SRC
        AudioZoneFormat.h     .azb sidecar format
        AudioZoneRuntime.h    sidecar parser and lookup runtime

code/tools/audiozones/        optional .audiozones to .azb compiler/auditor
tests/audio/                  deterministic policy, sidecar, and loopback tests
```

The high-level Quake sound API remains the `S_*` surface declared in
`code/client/audio/snd_public.h`. `S_Init` in
`code/client/audio/snd_main.cpp` owns player cvars, console commands, backend
selection, backend fallback, and `soundInterface_t` validation. Both the OpenAL
backend and the original mixer implement that same callback table, keeping
callers insulated from the rendering path.

## Layout Assessment

The old split left modern OpenAL files under `code/client/audio`, reusable
policy headers under `code/audio`, and classic `snd_*` files directly in
`code/client`. That made the ownership boundary harder to read than the code
really is.

The best-practice shape for this tree is a feature module under the subsystem
that owns it. Audio rendering is a client feature, not a qcommon/server feature,
so `code/client/audio/` is the natural home. Tools and tests still need the
sidecar format and deterministic policies, so those small headers live in
`code/client/audio/shared/` rather than in a separate top-level `code/audio/`
island.

This also avoids two misleading alternatives:

- A top-level `code/audio/` directory implies a reusable engine-wide subsystem,
  but the live backend depends on client state, client sound types, and client
  lifecycle.
- A sibling `code/client/audio-legacy/` directory overstates the split. The
  original mixer is a fallback backend, but the public sound facade, codecs,
  sample cache expectations, and mixer-era data types remain part of the active
  client sound contract.

The chosen layout keeps related files discoverable while keeping compatibility
risk low:

- `code/client/audio/` owns all client sound code.
- `code/client/audio/openal/` contains the modern backend.
- `code/client/audio/legacy/` contains the original mixer backend.
- `code/client/audio/codecs/` contains format loaders shared by both paths.
- `code/client/audio/shared/` contains small deterministic policies and sidecar
  formats used by the backend, tools, and tests.

The `snd_*` files were moved into this module, but they were not rewritten into
the OpenAL backend's single-translation-unit `.inl` style. That style is useful
for keeping OpenAL-private C++ types out of old C-facing headers; applying it to
the classic mixer would create a broad compatibility-sensitive rewrite without
changing the public boundary.

## Runtime Architecture

```text
client game and engine callers
        |
        v
S_* functions in code/client/audio/snd_public.h
        |
        v
code/client/audio/snd_main.cpp
        |
        v
soundInterface_t backend table
        |
        +-- openal: code/client/audio/openal/AudioSystem.cpp
        |
        +-- legacy: code/client/audio/legacy/snd_dma.cpp
```

The OpenAL backend uses the same sound registration, start, loop, raw stream,
remote voice, music, respatialization, update, list, info, and shutdown
semantics expected by the rest of the client. OpenAL does not create a second
game-facing sound API.

Startup is request-oriented:

1. `S_Init` registers sound cvars and commands.
2. If `s_initsound` is enabled, codecs are initialized and the requested backend
   is tried.
3. `s_backend openal` calls `S_OpenAL_Init`, which fills `soundInterface_t`
   through `AudioSystem::Init`.
4. If OpenAL cannot initialize, startup reports the failure and calls
   `S_Base_Init` for the legacy software mixer.
5. `s_backendActive` is set to `openal`, `legacy`, or `none` based on the backend
   that actually started.
6. Later callers keep using classic `S_*` functions without knowing which
   backend is active.

## Retail Quake Live Compatibility Layer

Retail voice chat is deliberately not sent through `S_RawSamples`. That stream
is shared by cinematics and other raw PCM producers, so using it for network
voice causes speakers and cinematics to overwrite one another. `S_AddVoiceSamples`
instead routes each remote client into one of five retail-sized voice lanes.
Both backends preserve speaker identity, never steal a busy lane, apply
`s_voiceVolume` independently, and use `s_voiceStep` as a small underrun buffer.
The legacy mixer converts the Steam decoder's reported sample rate to its DMA
rate; OpenAL queues the original mono PCM rate and lets the runtime resample it.
OpenAL reserves these lanes without crossing its protected gameplay-source
floor, degrading to fewer simultaneous speakers on unusually constrained
devices rather than suppressing weapon or announcer sounds.

The public facade also retains retail-shaped controls that differ from the old
Quake III defaults: master, music, and voice gain accept `0..2`; `s_mixPreStep`
provides the retail legacy-mixer cursor lead; and opt-in `s_pvs` culls
positional sounds outside the listener's BSP PVS. PVS lookup is fail-open when
no valid collision world is loaded, so menus, cinematics, and malformed custom
maps do not become silently muted. The default remains `s_pvs 0`, preserving
FnQL's established audio reach unless the compatibility switch is requested.
Retail native cgame's frame-clear import also has its own
`S_ClearLoopingSoundsFrame` path. This expires every loop not refreshed by the
new frame without conflating that operation with the older selective/kill-all
loop clear; OpenAL defers release until the frame update so refreshed loops keep
their playback position.

WAV ingestion accepts the formats needed by the retail and modern asset paths:
8/16/24-bit integer PCM, 32-bit IEEE float, and the corresponding extensible
WAV declarations. Higher-precision sources are converted through a bounded
16-bit path shared by streaming and whole-file loads; unsupported or malformed
layouts fail with a diagnostic rather than being reinterpreted.

## OpenAL Backend Slices

`AudioSystem.cpp` includes private implementation slices inside an anonymous
namespace. They are not public headers.

- `AudioSystemShared.inl` defines cvar pointers, math helpers, sample format
  classification, extension helpers, source-class/tone policy, environment
  state, audio-zone loading, and shared formatting.
- `AudioSystemOpenAL.inl` owns dynamic OpenAL loading, device/context creation,
  extension discovery, HRTF/output-mode requests, source and buffer helpers,
  EFX setup, direct-channel routing, timing queries, and recovery hooks.
- `AudioSystemWorld.inl` owns registered samples, world voices, looping sound
  state, listener state, positional updates, occlusion smoothing, tone
  application, source budgeting, and spatial debug snapshots.
- `AudioSystemStreams.inl` owns OpenAL buffer queues for music, raw samples,
  and isolated remote-voice lanes.
- `AudioSystemBackend.inl` owns the `soundInterface_t` facade, backend lifetime,
  diagnostics, sample registration, music/raw handling, and per-frame service
  flow.

The `.inl` split keeps the OpenAL backend in one translation unit so private C++
types do not leak into the old C-facing client sound headers, while still
keeping device, world, stream, and facade code readable.

## Spatial Rendering

Mono world sounds are true OpenAL positional sources. Listener origin,
orientation, source origin, source velocity, distance model, reference distance,
max distance, and rolloff feed the OpenAL source model. This lets HRTF-capable
output render source direction through head turns and movement.

Direct-path content stays direct by default:

- local UI and announcer-style sounds
- raw samples and background music
- authored stereo samples
- authored surround samples
- explicitly tagged UHJ or B-Format assets

When `AL_SOFT_direct_channels` is available, stereo/surround samples and streams
request direct channel routing so authored speaker channels are not image-shifted
by HRTF. When `AL_SOFT_direct_channels_remix` is also available, unmatched
speaker channels can be folded into narrower output layouts. Two-channel world
samples only enter positional routing through the opt-in
`s_alSpatializeStereo` compatibility switch and only on runtimes with
`AL_SOFT_source_spatialize`.

Native listener and source velocities drive Doppler on capable OpenAL
runtimes. Listener velocity is smoothed and teleport-clamped, distant one-shots
are rejected before consuming a source, and inaudible loops remain virtual
until they return inside the audibility horizon. These policies preserve loop
continuity while protecting source capacity during dense matches.

UHJ and B-Format are explicit filename-tag features, not automatic
reinterpretations of ordinary WAV files. If the runtime does not accept the
encoded OpenAL format, the backend falls back to a safe stereo-compatible path
instead of failing the sound.

## Environment Layer

The OpenAL backend adds an environmental layer on top of the normal source
selection path.

- Reverb prefers EAXREVERB when the EFX runtime accepts it and falls back to
  basic EFX reverb. `s_alReverb` is latched because the effect slot is created
  at backend init.
- Occlusion uses conservative collision traces between listener and source,
  including a small source-side probe fan so edge cases can become partial
  occlusion instead of a binary mute.
- Tone shaping uses low-pass, high-pass, and band-pass policies by source class,
  environment, and occlusion state.
- Environment transitions are smoothed so moving through thresholds does not
  zipper.
- Positional sources use capability-gated air absorption, and a liquid boundary
  between listener and source contributes a conservative occlusion floor.
- `s_alReverbGain` and `s_alOcclusionStrength` scale feature strengths without
  changing asset data.

Audio-zone sidecars can override generic environment heuristics for a map. The
runtime looks for `maps/<mapname>.azb` through normal filesystem search
semantics, parses the sidecar with `code/client/audio/shared/AudioZoneRuntime.h`,
and uses the current listener position to choose a zone. Zones may carry reverb,
occlusion, LF/HF tone multipliers, transition time, priority, material metadata,
outdoor/underwater flags, and bounded portal blend hints. Missing, disabled, or
invalid sidecars are harmless.

## Device Handling

The backend tries the requested OpenAL device first. If that device cannot open,
it tries the system default before falling back to the legacy backend. Context
creation is similarly layered: requested modern attributes first, simpler
context attributes next, then the outer backend fallback.

Runtime device recovery is conservative:

- `s_alAutoRecover` enables automatic live recovery attempts when supported.
- `s_alRecoverDevice` manually asks OpenAL Soft to reopen or reset the active
  device.
- `s_info` reports connection/recovery state when the runtime exposes it.
- `snd_restart` remains the deterministic full rebuild path when live recovery
  is unsupported or unsuccessful.

## Diagnostics

The main inspection commands are:

- `s_info`: active backend, device, requested-vs-active OpenAL settings, source
  counts, EFX state, audio-zone status, latency/clock data when available,
  music/raw state, and remote-voice lane allocation.
- `s_list`: registered sample list and load state.
- `s_alListDevices`: OpenAL playback device list.
- `s_alListHrtfs`: HRTF specifier list for the active or requested device.
- `s_alConfigHints`: OpenAL Soft config-file guidance and live capability hints.
- `s_alDebugDump`: current spatial environment and selected voice details.
- `s_alDebugOverlay`: in-game summary and detailed spatial debug overlay.

These diagnostics are part of the support surface. Prefer expanding them over
adding hidden behavior when new OpenAL features need to be explained.

## Validation

For source-layout, backend, or documentation changes, run at least a normal
client build. For policy or sidecar changes, run the deterministic tests that
match the touched area.

Meson:

```powershell
meson compile -C meson/build fnql.x64 fnql-audiozonesc fnql_audio_zone_tests fnql_audio_recovery_tests fnql_audio_occlusion_tests fnql_audio_voice_queue_tests fnql_audio_loopback_tests
meson test -C meson/build fnql_audio_zones fnql_audio_zone_authoring_audit fnql_audio_zone_sweep_script fnql_audio_zone_material_map fnql_audio_recovery fnql_audio_occlusion fnql_audio_voice_queue fnql_audio_loopback --print-errorlogs
```

CMake:

```powershell
cmake --build <build-dir> --target fnql-audiozonesc fnql_audio_zone_tests fnql_audio_recovery_tests fnql_audio_occlusion_tests fnql_audio_voice_queue_tests fnql_audio_loopback_tests
ctest --test-dir <build-dir> -R "fnql_audio" --output-on-failure
```

`fnql_audio_loopback_tests` may skip when OpenAL Soft loopback is unavailable.
Zone runtime, recovery, occlusion, and voice-lane policy tests do not need real
audio hardware.

## Maintainer Checklist

- Keep OpenAL changes behind `soundInterface_t`; do not add a second public
  sound API.
- Keep dedicated-server builds free of OpenAL runtime requirements.
- Keep OpenAL startup cvars latched and request-oriented.
- Keep ordinary stereo and authored surround content direct unless the user
  explicitly opts into stereo world-source spatialization.
- Keep remote voice isolated from raw/cinematic PCM and preserve the five-lane
  retail concurrency bound in both backends.
- Keep `.azb` files optional, data-only, and compatible across sidecar versions.
- Keep audio source ownership under `code/client/audio/`; avoid recreating a
  top-level `code/audio/` unless audio becomes a true qcommon/server subsystem.
- Update [`docs/AUDIO.md`](../AUDIO.md) for player-facing controls and this file
  for architecture or source-layout changes.
- Update `docs/templates/README.md.in`, then run
  `python scripts/generate_docs.py`, whenever README-facing audio text changes.
