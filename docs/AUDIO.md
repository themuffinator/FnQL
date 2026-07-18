# Audio Guide

FnQL keeps retail Quake Live's sound commands, voice chat, and music flow, then layers a more modern backend model on top. The default path uses OpenAL, while the original mixer remains available as a deterministic fallback.

This guide focuses on the player-facing audio controls: backend selection, device choice, master and music volume, focus muting, spatial audio, legacy mixer tuning, and the commands that help you verify what the engine actually started.

## Overview

If you want the short version first:

- `s_backend openal`: Use the default OpenAL backend with spatial audio support.
- `s_backend legacy`: Use the original software mixer and platform device path instead.
- `s_backendActive`: Read-only cvar that reports which backend actually started.
- `s_alDevice`: Pick a specific OpenAL playback device. Leave it blank for the system default device.
- `s_alHrtf`: Request OpenAL Soft HRTF mode: `auto`, `on`, or `off`. Requires `snd_restart`.
- `s_alHrtfId`: Request a preferred OpenAL Soft HRTF by name or numeric index. Requires `snd_restart`.
- `s_alOutputMode`: Request OpenAL Soft output mode: `auto`, `headphones`, `speakers`, `surround`, `quad`, `5.1`, `6.1`, or `7.1`. Requires `snd_restart`.
- `s_alDistanceModel`: Request the OpenAL world-sound distance model. Requires `snd_restart`.
- `s_alFrequency` / `s_alRefresh`: Request OpenAL mix frequency and refresh-rate hints. Requires `snd_restart`.
- `s_alMonoSources` / `s_alStereoSources`: Request OpenAL source-count hints. Requires `snd_restart`.
- `s_alOutputLimiter`: Request OpenAL Soft output limiting when supported. Requires `snd_restart`.
- `s_alSpatializeStereo`: Optionally route two-channel world samples through OpenAL positional rendering. Default `0`; requires `snd_restart`.
- `s_alAutoRecover 1`: Try OpenAL Soft live device recovery after a playback-device disconnect.
- `s_volume`: Set the master volume for all game audio.
- `s_musicVolume`: Set the music volume independently of gameplay sounds.
- `s_voiceVolume`: Set received Steam voice-chat volume independently of music and cinematics.
- `s_pvs`: Opt into retail QL's client-side PVS cull for positional sounds. Default `0`.
- `s_muteWhenUnfocused 1`: Mute audio when the game window loses focus.
- `s_muteWhenMinimized 1`: Mute audio when the game is minimized.
- `s_alReverb 1`: Enable OpenAL environmental reverb when the device supports EFX. Requires `snd_restart`.
- `s_alOcclusion 1`: Enable geometry-based occlusion on the OpenAL backend.
- `s_alReverbGain`: Scale reverb send level from `0` to `2`.
- `s_alOcclusionStrength`: Scale occlusion strength from `0` to `2`.
- `s_alAudioZones 1`: Use optional `maps/<map>.azb` audio-zone sidecars when present.
- `s_doppler 1`: Enable doppler shift on moving projectiles.
- `s_info`: Print the active backend, device, and runtime audio state.
- `s_alListDevices`: List OpenAL playback devices, marking the default, requested, and active devices when known.
- `s_alListHrtfs`: List OpenAL Soft HRTF specifiers for the active or requested device.
- `s_alRecoverDevice`: Manually ask OpenAL Soft to reopen or reset the active playback device.
- `s_alConfigHints`: Print OpenAL Soft configuration-file guidance and live capability hints.
- `snd_restart`: Reinitialize the sound system after backend or latched-device changes.

## Backend Selection

FnQL exposes two audio paths on the client side.

- `openal`: The default backend. This is the modern path and the only one that provides the current spatial audio controls such as reverb, occlusion, device selection, and the spatial debug tools.
- `legacy`: The original id Tech 3 software mixer and output-device path, extended with the retail QL controls described below. Use it when OpenAL is unavailable or when you need the classic fallback.

Behavior notes:

- `s_backend` is latched, so change it and then run `snd_restart`.
- `s_backendActive` tells you what actually initialized, not just what you asked for.
- If OpenAL fails to initialize, FnQL falls back to the legacy backend automatically instead of leaving you without sound.
- `s_info` is the quickest way to confirm the active backend and inspect the runtime state after a restart.

Example:

```cfg
seta s_backend "openal"
snd_restart
```

If you want the classic mixer instead:

```cfg
seta s_backend "legacy"
snd_restart
```

## Defaults, Migration, And Fallback

The modern audio path is designed to be an additive default rather than a new content requirement. Existing retail Quake Live assets, modules, demos, and sound commands keep working; the OpenAL backend changes how the client renders audio after the game has already chosen which sounds to play.

Fresh configs start from these intended defaults:

- `s_backend openal`: Start the OpenAL backend first.
- `s_alDevice ""`: Use the system default playback device.
- `s_alHrtf auto`: Let OpenAL Soft decide whether HRTF fits the current output.
- `s_alHrtfId ""`: Use the runtime's default HRTF.
- `s_alOutputMode auto`: Let OpenAL Soft choose the active output mode.
- `s_alDistanceModel inverse_clamped`: Use the modern OpenAL world-sound falloff.
- `s_alFrequency 48000` and `s_alRefresh 100`: Request a stable low-latency context profile.
- `s_alMonoSources 64` and `s_alStereoSources 8`: Request enough sources for the normal arena mix without assuming unlimited hardware.
- `s_alOutputLimiter 1`: Request OpenAL Soft output limiting when supported.
- `s_alSpatializeStereo 0`: Keep authored stereo samples on the stable direct path.
- `s_alReverb 1` and `s_alOcclusion 1`: Enable the environmental layer on capable OpenAL devices.
- `s_alReverbGain 1.0` and `s_alOcclusionStrength 1.0`: Use the neutral spatial tuning scale.
- `s_alAutoRecover 1`: Let the OpenAL backend attempt a live device reopen if the runtime reports a disconnect.

If you are upgrading from an older config, keep your existing `s_volume`, `s_musicVolume`, focus muting, and `s_doppler` choices. Then change OpenAL startup requests one at a time and run `snd_restart` after each latched change. `s_info` is the source of truth for the active result, because several OpenAL settings are requests that a device or runtime may decline.

Fallback is intentionally layered:

- Invalid text values for HRTF, output mode, or distance model are normalized to safe defaults and reported in the console.
- FnQL tries the requested OpenAL device first, or the system default when `s_alDevice` is blank.
- If a saved OpenAL device name cannot be opened, startup prints a warning and tries the system default before falling back to the legacy backend.
- If a modern OpenAL context cannot be created, the engine retries with simpler attributes before giving up on OpenAL.
- If an active OpenAL Soft device later disconnects and the runtime supports live reopen, FnQL attempts a conservative reconnect before asking you to use `snd_restart`.
- If OpenAL still cannot start, the sound system falls back to the legacy backend instead of leaving the client silent.
- To force the old path from the start, set `s_backend legacy` and run `snd_restart`.

When troubleshooting an upgrade, check `s_backendActive` and the requested/active lines in `s_info` before editing a large config. That usually tells you whether the issue is a latched setting waiting for restart, a runtime capability mismatch, a device selection problem, or an intentional fallback to the legacy backend.

## OpenAL Device Selection

When `s_backend` is set to `openal`, `s_alDevice` lets you choose which playback device OpenAL should open.

- Leave `s_alDevice` blank to use the system default device.
- Set `s_alDevice` to a device name if you want FnQL to target a specific headset, speakers, or virtual audio device.
- `s_alDevice` is latched, so you must run `snd_restart` after changing it.
- `s_info` prints both the requested device and the active device, which is useful when the system falls back to a different output.
- `s_alListDevices` prints the devices reported by OpenAL and marks the default, requested, and active device when those names are known.
- `s_alRecoverDevice` asks OpenAL Soft to reopen or reset the active playback device without rebuilding the whole sound backend. This is most useful after a USB headset, Bluetooth device, HDMI output, or default-device route disappears and returns.
- If a saved device name stops working after hardware or driver changes, set `s_alDevice ""`, run `snd_restart`, and then use `s_alListDevices` to pick a current name.

Example:

```cfg
seta s_backend "openal"
seta s_alDevice ""
snd_restart
```

## OpenAL Startup Requests

FnQL exposes modern OpenAL startup controls as latched request cvars. They describe what the engine should ask the OpenAL runtime for during initialization. The active result can differ when the device, driver, or OpenAL implementation does not support a request, so `s_info` prints requested and active values side by side.

Headphone and HRTF controls:

- `s_alHrtf auto`: Let OpenAL decide whether HRTF is appropriate for the current device and output.
- `s_alHrtf on`: Request HRTF rendering and report if the device denies or cannot provide it.
- `s_alHrtf off`: Request non-HRTF rendering.
- `s_alHrtfId`: Optional HRTF name or numeric index. Leave blank for the runtime default.
- `s_alOutputMode auto`: Let OpenAL choose the output mode.
- `s_alOutputMode headphones`: Request headphone/stereo-HRTF-oriented output where supported.
- `s_alOutputMode speakers`: Request ordinary speaker output where supported.
- `s_alOutputMode surround` or `5.1`: Request 5.1 surround output where supported.
- `s_alOutputMode quad`, `6.1`, or `7.1`: Request those speaker layouts where supported.

Distance and context hints:

- `s_alDistanceModel`: Defaults to `inverse_clamped`. Accepted names are `none`, `inverse`, `inverse_clamped`, `linear`, `linear_clamped`, `exponent`, and `exponent_clamped`.
- `s_alFrequency`: Requested mix frequency in Hz. Default `48000`.
- `s_alRefresh`: Requested context refresh rate in Hz. Default `100`.
- `s_alMonoSources`: Requested mono/3D source count. Default `64`.
- `s_alStereoSources`: Requested stereo source count. Default `8`.
- `s_alOutputLimiter`: Request OpenAL Soft output limiting. Default `1`.
- `s_alSpatializeStereo`: Optional two-channel world-sample positional routing when `AL_SOFT_source_spatialize` is available. Default `0`; stereo UI, announcer, music, raw audio, and surround samples remain direct.

These settings are intentionally requests, not guarantees. For example, an OpenAL runtime may use a different source count or disable HRTF for a device format it cannot render with HRTF. If a requested OpenAL context cannot be created, FnQL retries with safer context attributes before falling back to the legacy backend. Run `s_info` after `snd_restart` to see the actual state.

### OpenAL Latency Diagnostics

`s_alFrequency` and `s_alRefresh` are FnQL's standard OpenAL context hints for mix rate and refresh cadence. The defaults, `48000` and `100`, are intended to be a stable low-latency starting point rather than an aggressive tuning profile.

Run `s_info` after `snd_restart` to compare requested and active context values. On OpenAL Soft devices that expose `ALC_SOFT_device_clock`, `s_info` also reports a live device-clock snapshot and output latency in milliseconds. If latency is unavailable, FnQL reports that explicitly instead of guessing.

For OpenAL Soft installations, deeper latency tuning is usually done in the OpenAL Soft config rather than through game cvars. Smaller `period_size` or `periods` values can reduce latency, but they can also cause crackle on unstable devices, so change them gradually. Run `s_alConfigHints` to print the relevant config-file locations, useful option names, and the live OpenAL capabilities FnQL detected.

Example OpenAL Soft test profile:

```ini
[general]
frequency = 48000
period_size = 256
periods = 3
output-limiter = true
```

OpenAL Soft options worth knowing about include `stereo-mode`, `stereo-encoding`, `hrtf-mode`, `hrtf-size`, `default-hrtf`, `resampler`, `channels`, `sample-type`, and decoder options such as `hq-mode`, `distance-comp`, `nfc`, and `speaker-dist`. FnQL does not need to own every one of those settings; it reports the active HRTF/output/limiter/device state and leaves advanced library-global policy in the OpenAL Soft config.

## Volume, Music, And Focus Muting

The main day-to-day controls are intentionally simple.

- `s_volume`: Master volume for game audio. Retail range `0` to `2`. Default `0.8`.
- `s_musicVolume`: Music-only volume. Retail range `0` to `2`. Default `0.25`.
- `s_voiceVolume`: Received Steam voice-chat volume. Retail range `0` to `2`. Default `1.0`.
- `s_muteWhenUnfocused`: Mute audio when the window is no longer focused. Default `1`.
- `s_muteWhenMinimized`: Mute audio when the game is minimized. Default `1`.

Practical guidance:

- Lower `s_musicVolume` first if the soundtrack is stepping on match clarity.
- Use `s_volume` when the whole mix is too loud or too quiet.
- If the game seems to "lose" audio after task switching, check the two focus-muting cvars before assuming the backend is broken.
- Focus muting applies to both backends.
- Remote speakers use five independent retail-style queues, so simultaneous
  speakers do not overwrite one another or the raw PCM stream used by
  cinematics. `s_voiceStep` controls the small packet prebuffer and is normally
  best left at its `0.02` default.

Examples:

```cfg
seta s_volume "0.65"
seta s_musicVolume "0.15"
```

Keep audio playing while alt-tabbed:

```cfg
seta s_muteWhenUnfocused "0"
seta s_muteWhenMinimized "0"
```

## Spatial Audio

Spatial audio lives on the OpenAL backend. It is enabled by default, but the individual features remain adjustable so you can tune the mix instead of treating it as all-or-nothing.

Mono world sounds are submitted to OpenAL as true positional sources, using the current listener position and orientation so HRTF-capable devices can render head turns and source movement naturally. These sources also use the selected OpenAL distance model with Quake-scaled reference, max-distance, and rolloff parameters. Local sounds, announcer/UI sounds, music, raw samples, and authored multi-channel assets stay on the non-spatial path to avoid shifting centered or speaker-authored content. When OpenAL exposes `AL_SOFT_direct_channels`, stereo and surround samples plus music/raw streams request direct channel routing so HRTF processing does not image-shift authored channels. When `AL_SOFT_direct_channels_remix` is also available, unmatched speaker channels can be remixed instead of dropped on narrower output layouts.

`s_alSpatializeStereo 1` is an opt-in diagnostic/compatibility escape hatch for mods with two-channel world samples that were authored expecting positional movement. It only applies to two-channel world samples on OpenAL runtimes with `AL_SOFT_source_spatialize`; UI, announcer, music, raw audio, and authored surround layouts remain direct. Leave it at `0` for retail-style content.

Use `s_alDistanceModel inverse_clamped` for the default modern falloff. `linear_clamped` is closest to the old Quake-style full-volume/max-distance shape, while `none` disables OpenAL distance attenuation and leaves the compatibility gain path in place.

### HRTF And Output Mode

`s_alHrtf`, `s_alHrtfId`, and `s_alOutputMode` control the HRTF/output mode requested from OpenAL Soft at startup. Use `auto` unless you are deliberately testing a headset, speaker path, or specific HRTF.

Important details:

- HRTF availability depends on the OpenAL runtime and the active output device.
- Surround playback is optional and capability-gated. Authored quad/5.1/6.1/7.1 PCM is submitted natively when `AL_EXT_MCFORMATS` is available; otherwise it is mixed down to stereo so the asset still plays.
- Advanced immersive assets are opt-in by filename tag. WAV files tagged with `uhj`, `uhj2`, `uhj3`, or `uhj4` use OpenAL Soft UHJ formats when `AL_SOFT_UHJ` is available. Files tagged with `bformat2d`, `bformat3d`, or `ambisonic` use first-order B-Format when `AL_EXT_BFORMAT` is available. Untagged files keep the ordinary PCM interpretation.
- If UHJ or B-Format support is unavailable, FnQL falls back to a safe stereo render path instead of failing the sound. Two-channel UHJ remains stereo-compatible; B-Format uses its omni channel for fallback.
- `s_info` reports the requested HRTF mode, active HRTF status, output-mode request, and active output mode when the runtime exposes that information.
- `s_alListHrtfs` prints the HRTF names/indices exposed by OpenAL Soft. If OpenAL is active, it uses the live device; otherwise it opens the requested/default device temporarily for diagnostics.
- If HRTF sounds worse on a particular headset or listener, use `s_alHrtf off` and run `snd_restart`.

### Reverb

- `s_alReverb 1`: Enable environmental reverb sends on OpenAL devices that expose EFX support.
- `s_alReverb 0`: Disable the OpenAL reverb path.
- `s_alReverbGain`: Scale the wet reverb level from `0` to `2`. Default `1.0`.

Important details:

- Reverb requires an OpenAL device with EFX support.
- `s_alReverb` is latched and requires `snd_restart` to apply cleanly.
- `s_info` reports whether EFX support is available and whether the reverb send is active on the current device.
- FnQL smooths environment changes instead of hard-switching the active EFX preset. The debug overlay and `s_alDebugDump` show the active-to-target environment, transition blend, wet send, and direct/wet high-frequency values.

### Occlusion

- `s_alOcclusion 1`: Enable world-geometry occlusion checks.
- `s_alOcclusion 0`: Disable occlusion.
- `s_alOcclusionStrength`: Scale how strongly occluded sounds are muffled. Range `0` to `2`. Default `1.0`.

Occlusion is useful when you want walls, doors, and arena structure to affect how remote sounds read. FnQL smooths occlusion changes over time and applies separate direct-path attenuation and tone filtering, so moving behind a wall should sound like a transition rather than a hard step. The guaranteed dry-path gain change is intentionally audible even on devices without EFX filters; EFX-capable devices add the stronger low-pass/band-pass tone shift on top. If the result feels too dull, reducing `s_alOcclusionStrength` is usually a better first move than disabling the feature entirely.

### Audio Visibility And Culling

FnQL plays the transient and looping sounds requested by the client module, then applies source budgets, distance attenuation, and optional trace-based occlusion. For retail QL parity, `s_pvs 1` additionally mutes positional sounds whose origins are outside the listener's current BSP PVS. The check is off by default and fails open when no valid collision world is loaded; it does not affect local/UI audio, music, raw/cinematic PCM, or remote voice chat.

- Snapshot visibility is handled before audio sees most world entities. The server builds snapshots from `CM_ClusterPVS`, area-portal connectivity, broadcast/single-client flags, and the portal-style second-view PVS paths (`SVF_PORTAL` and `SVF_SELF_PORTAL2`).
- There is no engine-wide `CM_ClusterPHS` sound visibility path in this tree. The only PHS reference is in bot AAS helpers, not in client sound dispatch.
- One-shot sounds are limited by duplicate suppression, per-entity concurrency caps, and voice-source eviction. Looping sounds must be refreshed by the client game each frame; unrefreshed transient loops are stopped.
- Once a voice reaches OpenAL, audibility is mainly distance gain and source availability. Occlusion is a separate `CM_BoxTrace` test against solid/slime/lava contents between listener and source, with a small source-side probe fan so narrow or edge obstructions become partial occlusion instead of a binary on/off gate.

Servers can opt into a compatibility-preserving audio visibility expansion without changing the protocol or relaxing visual PVS:

- `sv_audioPVS 0`: Disabled. This is the default and preserves retail snapshot behavior.
- `sv_audioPVS 1`: Add nearby sound-only emitters, such as speaker entities, audio-only general sound events, and non-visual `loopSound` entities, when they are outside visual PVS but still area-connected.
- `sv_audioPVS 2`: Also add nearby non-visually culled entities that carry `loopSound`. This can make more gameplay sounds audible through walls, but it intentionally remains an explicit server choice.
- `sv_audioPVSRange`: Maximum expansion distance in game units. Default `1024`.
- `sv_audioPVSMaxEntities`: Maximum extra sound emitters per client snapshot. Default `16`.

The expansion is deliberately server-side, capped, and off by default. It sends ordinary `entityState_t` records that existing clients already understand, then the client-side occlusion path handles the muffling.

### Tone Shaping

FnQL uses a small set of EFX filter presets for source classes rather than a full user-facing EQ. The goal is clarity and plausibility with no custom sound assets:

- World sounds use low-pass shaping from the current environment and occlusion state.
- Heavily occluded and underwater sounds can use band-pass shaping so blocked sounds lose some low-end weight as well as high-frequency detail.
- Local UI, chat, and announcer-style sounds can use light high-pass shaping so they stay separate from the arena mix.
- Stereo samples, music, and raw audio keep their authored direct path by default; they are not spatialized or re-EQed for HRTF.

### Optional Audio Zone Sidecars

FnQL can read optional compiled audio-zone sidecars named `maps/<mapname>.azb`. These files are maintainer-authored tuning data for map-specific reverb, tone, occlusion multiplier, and transition behavior. They are never required for normal gameplay: if a sidecar is missing, invalid, disabled, or does not contain the listener's current position, the OpenAL backend keeps using the generic trace-based environment heuristics described above.

- `s_alAudioZones 1`: Enable `.azb` sidecars when present. This is the default.
- `s_alAudioZones 0`: Ignore sidecars and use generic environment heuristics only.
- `s_info`: Reports whether audio zones are disabled, missing, loaded, and which zone is currently active.
- `s_alDebugOverlay 2` and `s_alDebugDump`: Include active zone name, material metadata, portal blend target when one is active, plus the zone-adjusted wet, low/high-frequency, occlusion, and transition values.
- FnQL checks `FnQL-pkg.fnz` next to the executable before normal game data for sidecars, using game-dir-prefixed entries such as `baseq3/maps/q3dm1.azb` or `missionpack/maps/<mapname>.azb`.

Sidecars are compiled with the repo tool target `fnql-audiozonesc`. Maintainers can write `maps/<mapname>.audiozones` by hand or generate a first-pass sidecar from an existing `.bsp`, then layer small overrides on top. Current sidecars can carry material metadata, portal hints, and optional per-portal blend tuning while older version 1 and version 2 sidecars remain readable. The compiler workflow and authoring syntax are documented in the maintainer notes and in `code/tools/audiozones/README.md`.

### Weapon Sound Shaders

The OpenAL backend also reads a small FnQL sound shader file, `sound/fnql-weapon-sounds.sndshd`, from `FnQL-pkg.fnz`. The format intentionally follows the idTech4/Quake 4 declaration style: `sound <name> { minDistance ... maxDistance ... volumeDb ... shakes ... sample }`. The shipped `baseq3` shader covers the standard Quake III Arena weapon effects, while the shipped `missionpack` shader covers Team Arena weapon firing and impacts. Both give the original retail samples a little more attack, longer distance throw, and modest reverb send without replacing them.

Like audio zones, the root package stores this with a game-dir prefix, for example `baseq3/sound/fnql-weapon-sounds.sndshd` or `missionpack/sound/fnql-weapon-sounds.sndshd`. Mods can ship their own game-dir entry in the root package source tree when they need different tuning.

### Doppler

- `s_doppler 1`: Enable doppler shift on moving projectiles.
- `s_doppler 0`: Disable doppler shift.

This setting affects both backends, but it matters most when the OpenAL path is doing the rest of the spatial work.

### Recommended OpenAL Starting Point

This is a solid default if you want FnQL's modern audio path without pushing the mix into something exaggerated:

```cfg
seta s_backend "openal"
seta s_alDevice ""
seta s_alHrtf "auto"
seta s_alOutputMode "auto"
seta s_alDistanceModel "inverse_clamped"
seta s_alFrequency "48000"
seta s_alRefresh "100"
seta s_alMonoSources "64"
seta s_alStereoSources "8"
seta s_alOutputLimiter "1"
seta s_alSpatializeStereo "0"
seta s_volume "0.8"
seta s_musicVolume "0.25"
seta s_doppler "1"
seta s_alReverb "1"
seta s_alReverbGain "1.0"
seta s_alOcclusion "1"
seta s_alOcclusionStrength "1.0"
seta s_alAudioZones "1"
snd_restart
```

## Legacy Backend Tuning

The legacy mixer remains available when you want the original software path or when OpenAL is not appropriate for a given machine.

The main legacy-specific controls are:

- `s_khz`: Output sampling rate for the legacy backend. Valid values are `8`, `11`, `22`, `44`, and `48`. Default `22`. Requires `snd_restart`.
- `s_mixAhead`: Amount of audio to pre-mix ahead of playback. Default `0.2`.
- `s_mixPreStep`: Retail QL-compatible DMA cursor lead. Default `0.05`.
- `s_mixOffset`: Developer-facing timing offset for the legacy mixer. Range `0` to `0.5`.
- `s_device`: ALSA output device selector on Linux builds that use the non-SDL ALSA path.

Practical guidance:

- Higher `s_khz` values can improve fidelity, but they are not automatically the best choice on every machine.
- `s_mixAhead` is a stability-vs-latency control. Higher values can help with crackle or starvation on unstable systems, but they also push the mixer further ahead.
- `s_mixOffset` is an advanced knob and is usually best left at `0` unless you are deliberately testing mixer timing behavior.

Example legacy profile:

```cfg
seta s_backend "legacy"
seta s_khz "44"
seta s_mixAhead "0.2"
snd_restart
```

## Useful Commands

FnQL keeps the classic sound commands and adds a few OpenAL-oriented inspection tools.

- `s_info`: Print the current backend, device, EFX support, reverb state, occlusion state, source counts, weapon sound shader rule count, sample counts, and background-track state.
- On OpenAL, `s_info` also compares requested and active HRTF, output mode, distance model, mix frequency, refresh rate, source counts, output limiter state, and the stereo-spatialization request. It reports device clock and latency when OpenAL Soft exposes that timing extension.
- `s_alListDevices`: List OpenAL playback devices. This can still be useful if the legacy backend is active and you are preparing an OpenAL device setting.
- `s_alListHrtfs`: List HRTFs for the active OpenAL device, or for the requested/default OpenAL device when OpenAL is not active.
- `s_list`: List registered samples and whether each one is loaded, unloaded, missing, or using the generated fallback tone. Loaded OpenAL samples also show channel layout, sample rate, and duration.
- `s_stop`: Stop active sounds.
- `play <soundfile>`: Play one or more sound files locally for a quick spot check.
- `music <intro> [loop]`: Start background music playback.
- `stopmusic`: Stop the current background track.
- `snd_restart`: Restart the whole sound system after backend or latched setting changes.
- `s_alDebugDump`: Print a spatial-audio debug snapshot on the OpenAL backend.

Examples:

```cfg
play sound/player/jump1.wav
```

```cfg
music music/fla22k_02.wav
```

## Spatial Debug Tools

If you are tuning the OpenAL path and want more than a yes-or-no answer, FnQL exposes both console and overlay debug tools.

- `s_alDebugDump`: Prints a spatial snapshot for the current environment transition and inspected voice. When `AL_SOFT_source_latency` is available, the selected source also reports its playback offset and latency.
- `s_alDebugOverlay 0`: Disable the OpenAL debug overlay.
- `s_alDebugOverlay 1`: Show summary environment transition and selected voice state.
- `s_alDebugOverlay 2`: Add environment wet/filter values, active audio-zone information, plus selected-voice sample, gain, and tone-filter information.
- `s_alDebugVoice -1`: Automatically inspect the nearest active voice.
- `s_alDebugVoice <entityNum>`: Lock the debug tools to a specific entity when you want to inspect a known source.
- `s_alSourceClassDebug 1`: In developer mode, add source-class aggregate routing and gain summaries to `s_alDebugDump`.

These tools are only meaningful on the OpenAL backend.

## Troubleshooting

### I changed a setting and nothing happened

Some audio cvars are latched. If you change any of the following, run `snd_restart` before judging the result:

- `s_backend`
- `s_alDevice`
- `s_alHrtf`
- `s_alHrtfId`
- `s_alOutputMode`
- `s_alDistanceModel`
- `s_alFrequency`
- `s_alRefresh`
- `s_alMonoSources`
- `s_alStereoSources`
- `s_alOutputLimiter`
- `s_alSpatializeStereo`
- `s_alReverb`
- `s_khz`

`s_alAudioZones` is not latched. Toggle it and use `s_info` or the spatial debug overlay to confirm whether the active map's sidecar is loaded or ignored.

### Spatial audio sounds flat

Check the basics in this order:

- Run `s_info` and confirm `s_backendActive` is `openal`.
- Compare the requested and active HRTF/output/distance lines in `s_info`.
- If you expect reverb, confirm that `s_info` reports `EFX support: enabled`.
- Confirm `s_alReverb 1` and `s_alOcclusion 1`.
- If the mix is too subtle, try increasing `s_alReverbGain` or `s_alOcclusionStrength` in small steps instead of jumping straight to extremes.

### The wrong output device started

- Run `s_alListDevices` and copy the device name exactly as OpenAL reports it.
- Set `s_alDevice` explicitly.
- Run `snd_restart`.
- Run `s_info` again and compare the requested device against the active device.
- If the requested device is no longer listed or startup warns that it could not be opened, set `s_alDevice ""` to return to the system default.

### Audio does not return after unplugging a device

- Run `s_info` and check the `Device state` line.
- Leave `s_alAutoRecover 1` enabled if your OpenAL Soft runtime supports live reopen.
- Run `s_alRecoverDevice` after reconnecting the headset, HDMI output, or Bluetooth device.
- Use `s_alRecoverDevice force` if the device reports connected but you want OpenAL Soft to reopen it anyway.
- If live reopen is unavailable or fails, run `snd_restart`; if the device name changed, set `s_alDevice ""` first.

### HRTF stays disabled or sounds wrong

- Run `s_info` and compare the requested and active HRTF/output lines.
- Run `s_alListHrtfs` to see whether the active or requested OpenAL device exposes any HRTF specifiers.
- If you are testing headphones, try `s_alOutputMode headphones`, leave `s_alHrtfId ""`, and run `snd_restart`.
- If HRTF still reports disabled, the active runtime or output path probably declined the request. Use `s_alHrtf auto` or `s_alHrtf off` for that device.
- If front/back cues feel worse on your setup, `s_alHrtf off` is a valid preference rather than a failure.

### `s_info` shows different active values than I requested

That usually means OpenAL accepted the context but adjusted one or more requested attributes. This is normal for HRTF, output mode, source counts, output limiting, and timing hints.

- Prefer `auto` for HRTF and output mode unless you are testing a specific device.
- Leave `s_alHrtfId` blank if a named or numeric HRTF no longer appears in `s_alListHrtfs`.
- Use the default `s_alMonoSources 64` and `s_alStereoSources 8` unless you are diagnosing source starvation.
- Treat `s_alFrequency` and `s_alRefresh` as hints; use OpenAL Soft config files for deeper latency tuning.

### OpenAL keeps falling back to legacy

- Run `s_info` and confirm whether `s_backendActive` is `legacy`.
- Set `s_alDevice ""`, `s_alHrtf auto`, `s_alHrtfId ""`, and `s_alOutputMode auto`, then run `snd_restart`.
- Check that the packaged OpenAL runtime is present on Windows builds, or that a system OpenAL runtime is available on non-Windows builds.
- If the machine still cannot start OpenAL cleanly, keep `s_backend legacy` for that install.

### A map sounds different than expected

- Run `s_info` and check the `Audio zones` line.
- If a sidecar is loaded, use `s_alDebugOverlay 2` or `s_alDebugDump` to see the active zone name, material, portal blend target, and tuned environment values.
- Set `s_alAudioZones 0` to compare the same location against the generic environment heuristics.
- Invalid `.azb` files are ignored with a console warning; missing files are harmless.

### The game goes silent after alt-tabbing

That may be intentional rather than a failure:

- `s_muteWhenUnfocused 1` mutes audio when the window is not focused.
- `s_muteWhenMinimized 1` mutes audio when the game is minimized.

If you want audio to continue while the game is in the background, disable one or both.

### I want the safest fallback path

Use the legacy backend:

```cfg
seta s_backend "legacy"
snd_restart
```

## Recommended Starting Points

### Modern Default

Use this if you want FnQL's intended current audio path:

- `s_backend openal`
- `s_alDevice ""`
- `s_alHrtf auto`
- `s_alOutputMode auto`
- `s_alDistanceModel inverse_clamped`
- `s_alSpatializeStereo 0`
- `s_alReverb 1`
- `s_alOcclusion 1`
- `s_volume 0.8`
- `s_musicVolume 0.25`

### Competitive And Dry

Use this if you want less ambience and more direct positional cues:

- `s_backend openal`
- `s_alReverb 0`
- `s_alOcclusion 1`
- `s_alOcclusionStrength 0.5`
- `s_musicVolume 0`

### Conservative Compatibility

Use this if you want the original mixer path:

- `s_backend legacy`
- `s_khz 22`
- `s_mixAhead 0.2`

## Related Guides

- [Console Guide](CONSOLE.md) for command entry, completion, and console-side workflow.
- [Modern Audio Engine Notes](fnql/AUDIO_ENGINE.md) for architecture, source layout, compatibility boundaries, and validation expectations.
- [Technical Notes](fnql/TECHNICAL.md) for repository and release documentation rather than player settings.
