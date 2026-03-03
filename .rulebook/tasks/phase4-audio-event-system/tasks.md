## 1. Prerequisites

- [ ] 1.1 Confirm `phase2-audio-system` is complete — `CAudioSource`, voice pool, bus routing exist
- [ ] 1.2 Confirm `phase0-data-driven-framework` spec is complete — `.uzevent` format and `IAssetImporter` defined
- [ ] 1.3 Read `docs/specs/audio-system.md` — understand `CAudioSource`, bus system, voice API
- [ ] 1.4 Read `docs/specs/data-driven-framework.md` — understand `[meta]`, `IAssetImporter::parse` interface

## 2. .uzevent Format

- [ ] 2.1 Define `[meta]` block: `type = uzevent`, `version = 1`, `uuid`
- [ ] 2.2 Define `[event]` section: `name`, `bus` (`sfx`/`music`/`ambient`/`ui`), `polyphony` (max simultaneous), `cooldown_ms`
- [ ] 2.3 Define `[variation]` sections (multiple allowed): `asset = "res://sfx/..."`, `weight` (int), `pitch_min`, `pitch_max`, `volume_min`, `volume_max`
- [ ] 2.4 Define `[3d]` section (optional): `min_distance`, `max_distance`, `falloff_curve` (`linear`/`inverse`/`none`)
- [ ] 2.5 Write example `.uzevent` file: `sfx/gunshot.uzevent` with 3 variations, random pitch 0.9–1.1

## 3. AudioEvent Data Struct

- [ ] 3.1 Create `src/engine/audio/audio_event.h`
- [ ] 3.2 Define `AudioVariation` struct: `asset_path`, `weight`, `pitch_min`, `pitch_max`, `volume_min`, `volume_max`
- [ ] 3.3 Define `AudioEvent3dParams` struct: `min_distance`, `max_distance`, `falloff_curve`
- [ ] 3.4 Define `AudioEvent` struct: `name`, `bus_id`, `polyphony`, `cooldown_ms`, `variations[]`, `params_3d?`
- [ ] 3.5 Create `AudioEventRegistry` singleton: `register(path, AudioEvent)`, `get(name) -> const AudioEvent*`, `reload(path)`

## 4. AudioEventImporter

- [ ] 4.1 Create `src/engine/assets/audio_event_importer.h/cpp`
- [ ] 4.2 Implement `AudioEventImporter` implementing `IAssetImporter`
- [ ] 4.3 Implement `parse(path, ParsedDoc&)` — fill `AudioEvent` from parsed sections
- [ ] 4.4 Implement `reimport(path)` — re-parse and update `AudioEventRegistry`
- [ ] 4.5 Implement `get_dependencies(ParsedDoc&, deps)` — add all variation `asset` paths as dependencies
- [ ] 4.6 Register for `.uzevent` extension in `AssetImporterRegistry`

## 5. Trigger Logic

- [ ] 5.1 Create `src/engine/audio/audio_event_player.h/cpp` — `AudioEventPlayer` utility
- [ ] 5.2 Implement `AudioEventPlayer::trigger(event_name) -> VoiceHandle` — top-level trigger
- [ ] 5.3 Implement `AudioEventPlayer::trigger(event_name, Vec3 position) -> VoiceHandle` — 3D positional trigger
- [ ] 5.4 Implement variation selection: weighted random selection from `variations[]` using `std::discrete_distribution`
- [ ] 5.5 Implement polyphony limit: count active voices for this event; if at limit, steal oldest voice
- [ ] 5.6 Implement cooldown enforcement: reject trigger if `now - last_trigger < cooldown_ms`; log at `TRACE` level
- [ ] 5.7 Apply random pitch: `voice.set_pitch(uniform_real(pitch_min, pitch_max))`
- [ ] 5.8 Apply random volume: `voice.set_volume(uniform_real(volume_min, volume_max))`
- [ ] 5.9 Route to bus: `voice.set_bus(event.bus_id)`

## 6. Lua API

- [ ] 6.1 Implement `ctx.audio:trigger(name) -> voice_handle` — trigger event by name
- [ ] 6.2 Implement `ctx.audio:trigger(name, options_table) -> voice_handle` — options: `position`, `pitch_override`, `volume_override`
- [ ] 6.3 Implement `ctx.audio:trigger_at(name, x, y, z) -> voice_handle` — 3D shorthand
- [ ] 6.4 Define `voice_handle` Lua userdata: `stop()`, `set_volume(v)`, `set_pitch(p)`, `is_playing() -> bool`

## 7. Hot-Reload Integration

- [ ] 7.1 Register `AudioEventImporter` as `IAssetReloadSubscriber`
- [ ] 7.2 On `.uzevent` file change: `AudioEventRegistry::reload(path)` — update entry; in-flight voices unaffected
- [ ] 7.3 Log: `[AudioEvent] reloaded: sfx/gunshot.uzevent`

## 8. Testing

- [ ] 8.1 Unit test: parse `.uzevent` with 3 variations → `AudioEvent.variations.size() == 3`
- [ ] 8.2 Unit test: weighted random selection — run 1000 triggers, verify weight distribution within ±5%
- [ ] 8.3 Unit test: cooldown — second trigger within cooldown window returns null handle, logs trace
- [ ] 8.4 Unit test: polyphony limit — 4th trigger with `polyphony = 3` steals oldest voice
- [ ] 8.5 Unit test: pitch applied — triggered voice pitch within [pitch_min, pitch_max]
- [ ] 8.6 Integration test: `ctx.audio:trigger("sfx/gunshot")` plays audio without error
- [ ] 8.7 Integration test: hot-reload `.uzevent` — subsequent trigger uses new variation list

## 9. Documentation

- [ ] 9.1 Update `docs/specs/audio-system.md` — "Audio Event System" section with `.uzevent` format and trigger API
- [ ] 9.2 Create `docs/specs/audio-event-system.md` — full format spec with annotated example
- [ ] 9.3 Update `docs/guides/lua-scripting-guide.md` — "Triggering Audio Events" section
