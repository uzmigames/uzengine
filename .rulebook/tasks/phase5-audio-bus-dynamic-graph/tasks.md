## 1. AudioBusConfig Data Model
- [ ] 1.1 Define `AudioBusType` enum in `src/audio/audio_bus_config.h` with values `MASTER`, `SUBMIX`, `SEND`, `AUX`
- [ ] 1.2 Define `AudioBusDesc` struct in `src/audio/audio_bus_config.h` with fields: `const char* name`, `AudioBusType type`, `const char* parent_bus`, `float volume`, `float pan`
- [ ] 1.3 Define `AudioBusConfig` struct in `src/audio/audio_bus_config.h` containing a `std::vector<AudioBusDesc> buses`
- [ ] 1.4 Implement `.uzproj` TOML parser extension in `src/engine/ProjectConfig.cpp` to read `[[audio.buses]]` array entries into `AudioBusConfig`
- [ ] 1.5 Validate the parsed `AudioBusConfig` in `src/audio/audio_bus_config.cpp`: assert exactly one `MASTER` bus exists, detect cycles in the parent chain, and return an error string if validation fails

## 2. IAudioEngine Interface Extension
- [ ] 2.1 Add `reload_config(const AudioBusConfig& config)` virtual method to `IAudioEngine` in `src/audio/audio_engine.h`; the implementation must reload the bus graph without interrupting currently-playing voices
- [ ] 2.2 Add `add_bus(const AudioBusDesc& desc)` virtual method to `IAudioEngine` for runtime bus creation
- [ ] 2.3 Add `remove_bus(const char* name)` virtual method to `IAudioEngine` for runtime bus deletion; any voices routed to the removed bus are rerouted to its parent bus
- [ ] 2.4 Add `route_bus(const char* bus_name, const char* new_parent_name)` virtual method to `IAudioEngine` for runtime re-routing

## 3. XAudio2 Backend Implementation
- [ ] 3.1 Implement `AudioEngineXAudio2::reload_config(const AudioBusConfig& config)` in `src/audio/audio_engine_xaudio2.cpp`: destroy existing `IXAudio2SubmixVoice` instances and rebuild the chain from the new config without stopping the master voice
- [ ] 3.2 Implement `AudioEngineXAudio2::add_bus()`: create a new `IXAudio2SubmixVoice` with the correct `pSends` pointing to the parent bus's submix voice
- [ ] 3.3 Implement `AudioEngineXAudio2::remove_bus()`: destroy the `IXAudio2SubmixVoice` and update all child buses to re-send directly to the removed bus's parent
- [ ] 3.4 Implement `AudioEngineXAudio2::route_bus()`: call `IXAudio2SubmixVoice::SetOutputVoices` with the new parent's send descriptor
- [ ] 3.5 Ensure backwards compatibility: if `.uzproj` has no `[[audio.buses]]` section, auto-generate the legacy 4-bus layout (Master, Music, SFX, Voice) as `AudioBusConfig` at startup

## 4. IPC Handlers
- [ ] 4.1 Add IPC handler `audio.add_bus` in `src/common/ipc_shader_handlers.cpp` (or new `src/common/ipc_audio_handlers.cpp`) accepting `{name, type, parent, volume, pan}` JSON and calling `IAudioEngine::add_bus()`
- [ ] 4.2 Add IPC handler `audio.remove_bus` accepting `{name}` JSON and calling `IAudioEngine::remove_bus()`
- [ ] 4.3 Add IPC handler `audio.route_bus` accepting `{bus, new_parent}` JSON and calling `IAudioEngine::route_bus()`
- [ ] 4.4 Add IPC handler `audio.reload_config` accepting `{buses: [{name, type, parent, volume},...]}` JSON and calling `IAudioEngine::reload_config()`

## 5. Testing
- [ ] 5.1 Create `tests/core/test_audio_bus_config.cpp` with Google Test suite `AudioBusConfigTest`
- [ ] 5.2 Write test `ParsesUzprojBusesArray`: construct a mock TOML config with 3 buses; assert `AudioBusConfig` contains all 3 `AudioBusDesc` entries with correct names and types
- [ ] 5.3 Write test `ValidationRejectsMissingMaster`: pass a config with no `MASTER` bus; assert `validate()` returns a non-empty error string
- [ ] 5.4 Write test `ValidationRejectsCycle`: construct buses A -> B -> A cycle; assert `validate()` returns a non-empty error string
- [ ] 5.5 Write test `LegacyFourBusDefaultApplied`: parse a `.uzproj` with no `[[audio.buses]]` section; assert the generated `AudioBusConfig` contains exactly 4 buses named `Master`, `Music`, `SFX`, `Voice`
- [ ] 5.6 Write test `AddBusInsertedWithCorrectParent`: call `add_bus({name="Reverb", type=SUBMIX, parent="Master"})`; assert a subsequent `get_bus_names()` includes `"Reverb"`
- [ ] 5.7 Write test `RemoveBusReroutesChildren`: add bus chain Master -> SFX -> Reverb; remove `SFX`; assert `Reverb` is now a direct child of `Master`
- [ ] 5.8 Write test `RouteBusChangesParent`: route `Music` bus from `Master` to a new `Submix` bus; assert `Music` parent is now `Submix`
- [ ] 5.9 Write test `ReloadConfigPreservesPlayingVoices`: start a voice on the `SFX` bus; call `reload_config` with the same bus layout; assert the voice is still in `playing` state after reload
- [ ] 5.10 Verify C++ line coverage >= 95% and branch coverage >= 95% for `audio_bus_config.cpp` and `audio_engine_xaudio2.cpp` bus management paths

## 6. Documentation
- [ ] 6.1 Update `docs/specs/audio/audio-abstraction-layer.md` with the `AudioBusConfig` struct, the `IAudioEngine` bus management API, the `.uzproj` `[[audio.buses]]` TOML schema, and the three new bus types
