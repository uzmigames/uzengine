## 1. OpenAL Soft Backend Integration

- [ ] 1.1 Add `openal-soft` as a vcpkg dependency in `vcpkg.json` and update `CMakeLists.txt` to link `OpenAL::OpenAL` into the audio module target
- [ ] 1.2 Create `src/audio/openal_backend.h` declaring `OpenALBackend` class with interface methods `init()`, `shutdown()`, `create_source(entity_id)`, `destroy_source(entity_id)`, `update_3d(entity_id, position, velocity, azimuth_deg, elevation_deg)`, `set_hrtf_enabled(entity_id, bool)`
- [ ] 1.3 Implement `src/audio/openal_backend.cpp`: initialize OpenAL context with `ALC_HRTF_SOFT` extension requested; enumerate HRTF datasets using `alcGetStringiSOFT(ALC_HRTF_SPECIFIER_SOFT, i)` and select the first available dataset
- [ ] 1.4 Load HRTF dataset from `assets/audio/hrtf/default.mhr` (MIT KEMAR format) via `alcResetDeviceSOFT` with `ALC_HRTF_ID_SOFT` pointing to the dataset index; log the selected HRTF name at engine startup
- [ ] 1.5 Support user-provided datasets: scan `assets/audio/hrtf/` directory for all `.mhr` files at startup and register them as selectable options in the audio config

## 2. Engine Config and Backend Selection

- [ ] 2.1 Add `audio_backend` key to the engine config INI parser in `src/audio/audio_system.cpp` with accepted values `xaudio2` (default) and `openal_hrtf`
- [ ] 2.2 Add `hrtf_dataset` key to engine config accepting a filename stem (e.g., `default`) mapping to `assets/audio/hrtf/{name}.mhr`
- [ ] 2.3 Implement fallback logic in `AudioSystem::init()`: if `openal_hrtf` is requested but `ALC_HRTF_SOFT` is unavailable, or output device reports fewer than 2 channels, log `[AudioSystem] HRTF unavailable, falling back to XAudio2 X3DAudio` and activate the XAudio2 path

## 3. CAudioSource Component Changes

- [ ] 3.1 Add `hrtf_enabled` (bool, default `false`) field to `CAudioSource` in `src/scene/components.h`
- [ ] 3.2 Update `AudioSystem::update_3d()` to detect `hrtf_enabled == true` on a source and route it through `OpenALBackend::update_3d()` instead of XAudio2 X3DAudio when the OpenAL backend is active
- [ ] 3.3 Compute HRTF azimuth and elevation angles in `AudioSystem::update_3d()`: transform the world-space source position into listener-relative space, convert to spherical coordinates `(azimuth_deg, elevation_deg)`, and pass to `OpenALBackend::update_3d()`

## 4. HRTF Convolution Pipeline

- [ ] 4.1 In `OpenALBackend::update_3d()`, set `AL_POSITION` from listener-relative Cartesian coordinates so OpenAL Soft applies the HRTF convolution filter automatically; OpenAL Soft's HRTF path computes azimuth/elevation internally from the source position
- [ ] 4.2 Set `AL_SOURCE_RELATIVE` to `AL_FALSE` for HRTF sources so positions are in world space relative to the listener
- [ ] 4.3 Set listener orientation each frame using `alListenerfv(AL_ORIENTATION, ...)` derived from the listener entity's `CTransform` forward and up vectors
- [ ] 4.4 For non-HRTF sources sharing the OpenAL backend, disable HRTF per-source using `AL_DIRECT_FILTER` or the `AL_SOURCE_HRTF_SOFT` source attribute to avoid unnecessary convolution overhead

## 5. Lua API

- [ ] 5.1 Add `ctx.audio:set_hrtf(source_entity, enabled)` Lua binding in `src/scripting/lua_audio.cpp` that sets `CAudioSource::hrtf_enabled` on the target entity
- [ ] 5.2 Add `ctx.audio:get_hrtf(source_entity)` Lua binding returning the current `hrtf_enabled` bool value
- [ ] 5.3 Validate that `source_entity` has a `CAudioSource` component in both Lua bindings and throw a Lua error with message `"entity {id} does not have CAudioSource"` if the component is missing

## 6. Inspector Panel

- [ ] 6.1 Add a `Use HRTF` checkbox to the `CAudioSource` Inspector widget in the editor; the checkbox is only enabled (not greyed out) when the active audio backend is `openal_hrtf`
- [ ] 6.2 Display a tooltip on the disabled checkbox reading `"HRTF requires audio_backend = openal_hrtf in engine config"` when the checkbox is greyed out

## 7. Testing

- [ ] 7.1 Write `tests/audio/test_audio_hrtf.cpp`: mock the OpenAL Soft device using a stub `alcOpenDevice`/`alcCreateContext` pair; test that `OpenALBackend::init()` returns `false` and logs a warning when `ALC_HRTF_SOFT` is not in the extension string
- [ ] 7.2 Test the azimuth/elevation calculation: provide listener at origin facing +Z and source at position (1, 0, 0); assert azimuth = 90 degrees, elevation = 0 degrees within 0.5 degrees tolerance
- [ ] 7.3 Test elevation calculation: source directly above listener (0, 1, 0); assert elevation = 90 degrees
- [ ] 7.4 Test `AudioSystem::init()` fallback path: configure `audio_backend = openal_hrtf` in a mock config, stub OpenAL to report 1-channel device, verify `AudioSystem` logs the fallback warning and activates XAudio2 backend
- [ ] 7.5 Test `CAudioSource::hrtf_enabled` serialization round-trip: set `hrtf_enabled = true`, serialize to `.uzscene`, deserialize, verify field is `true`
- [ ] 7.6 Test Lua binding `ctx.audio:set_hrtf` with valid and invalid entity IDs; verify error message matches expected format for invalid entity
- [ ] 7.7 Test that HRTF is disabled per-source when `hrtf_enabled = false` even when the OpenAL backend is active; verify `AL_SOURCE_HRTF_SOFT` attribute is set to disable for that source
- [ ] 7.8 Verify all tests achieve ≥95% line and branch coverage on `openal_backend.cpp` and the HRTF path in `audio_system.cpp`

## 8. Documentation

- [ ] 8.1 Update `docs/specs/audio-system.md`: document `audio_backend` and `hrtf_dataset` config keys, `CAudioSource::hrtf_enabled` field, OpenAL Soft backend initialization sequence, and fallback behavior
- [ ] 8.2 Document the `ctx.audio:set_hrtf` and `ctx.audio:get_hrtf` Lua API signatures with parameter descriptions and error conditions
- [ ] 8.3 Add a "HRTF Binaural Audio" section describing supported HRTF dataset file format (`.mhr`), dataset directory structure, and how to provide a custom HRTF dataset
