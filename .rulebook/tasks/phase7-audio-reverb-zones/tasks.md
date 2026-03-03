## 1. CAudioReverbZone ECS Component

- [ ] 1.1 Define `CAudioReverbZone` struct in `src/scene/components.h` with fields: `shape` (enum `ReverbZoneShape { Box, Sphere }`), `half_extents` (float3, for box), `radius` (float, for sphere), `preset` (enum `ReverbPreset`), `wet_mix` (float, 0.0–1.0), `dry_mix` (float, 0.0–1.0), `blend_distance` (float, meters from zone edge)
- [ ] 1.2 Define `ReverbPreset` enum: `Dry`, `SmallRoom`, `MediumRoom`, `LargeRoom`, `ConcertHall`, `Cave`, `Auditorium`, `Outdoor`, `Underwater` — nine named constants
- [ ] 1.3 Register `CAudioReverbZone` in the ECS component registry and implement serialization/deserialization to/from the `.uzscene` INI format for all fields
- [ ] 1.4 Validate `wet_mix` and `dry_mix` are clamped to [0, 1] on deserialization and on component mutation

## 2. Reverb Preset Parameter Library

- [ ] 2.1 Define a `static const XAUDIO2FX_REVERB_PARAMETERS kReverbPresets[]` lookup table in `src/audio/audio_reverb_system.cpp` with one entry per `ReverbPreset` enum value, using the XAudio2 XAPO reverb I3DL2 preset constants as starting values
- [ ] 2.2 Tune each preset's `ReflectionsDelay`, `ReverbDelay`, `RearDelay`, `RoomSize`, `DecayTime`, and `Density` to match the named environment type
- [ ] 2.3 Ensure the `Dry` preset sets `WetDryMix = 0` and `RoomSize = 0` to produce completely dry output when selected

## 3. AudioReverbSystem Implementation

- [ ] 3.1 Create `src/audio/audio_reverb_system.h` and `src/audio/audio_reverb_system.cpp` implementing `AudioReverbSystem` class with methods `init()`, `shutdown()`, `update(listener_position, active_reverb_zones)`, `set_preset(zone_entity, ReverbPreset)`
- [ ] 3.2 In `AudioReverbSystem::init()`, create one XAudio2 submix voice per active `CAudioReverbZone` entity at startup; attach the `XAUDIO2FX_REVERB` XAPO effect chain to each submix voice with the zone's initial preset parameters
- [ ] 3.3 Implement `AudioReverbSystem::update()`: iterate active `CAudioReverbZone` entities, compute the overlap factor for the listener position relative to each zone's shape (box: per-axis signed distance to edge; sphere: normalized radial distance from center to radius), clamp to [0, 1], scale by `wet_mix`
- [ ] 3.4 For box zones, compute `overlap_factor` as `clamp(1 - max_axis_signed_distance / blend_distance, 0, 1)` where `max_axis_signed_distance` is the maximum of the three per-axis distances from the listener to the box surface
- [ ] 3.5 For sphere zones, compute `overlap_factor` as `clamp(1 - (dist_to_center - radius + blend_distance) / blend_distance, 0, 1)`
- [ ] 3.6 Set each audio source's send level to its reverb submix voice to `wet_mix * overlap_factor`; simultaneously set the direct-path send level to `dry_mix + (1 - overlap_factor) * wet_mix`
- [ ] 3.7 Enforce the 2-simultaneous-zones cap: when more than two zones overlap the listener position, select the two with the highest `overlap_factor` values and zero-out all others for that frame

## 4. Zone Transition Blending

- [ ] 4.1 Implement smooth send-level interpolation in `AudioReverbSystem::update()`: rather than snapping submix send levels, lerp toward the target `wet_mix * overlap_factor` by `blend_speed = (1.0 / blend_distance) * delta_time * walk_speed_approx`; ensures sub-frame smoothing during zone boundary crossings
- [ ] 4.2 Use the existing `blend_distance` field as the spatial width of the crossfade region — a smaller value produces a sharper transition, zero means instant snap

## 5. Lua API

- [ ] 5.1 Add `ctx.audio:set_reverb_preset(entity, preset_name_string)` in `src/scripting/lua_audio.cpp`; convert the string to `ReverbPreset` enum via a lookup table and call `AudioReverbSystem::set_preset()`; throw a Lua error if the preset name is unrecognized
- [ ] 5.2 Add `ctx.audio:get_reverb_preset(entity)` returning the current preset name as a string
- [ ] 5.3 Add `ctx.audio:set_reverb_wet_dry(entity, wet, dry)` to adjust blend mix at runtime without changing preset

## 6. Editor Gizmo and Inspector

- [ ] 6.1 In the editor gizmo system, render `CAudioReverbZone` box shapes as a semi-transparent colored wireframe box (use a distinct per-preset color); render sphere shapes as a wireframe sphere; draw zone name as a billboard label at the zone origin
- [ ] 6.2 Draw the `blend_distance` boundary as a second slightly larger wireframe outline with dashed or alpha-reduced lines to indicate the transition region
- [ ] 6.3 Add `CAudioReverbZone` Inspector widget: shape selector (Box/Sphere), size controls (half_extents XYZ for box, radius for sphere), preset dropdown listing all nine presets, wet/dry mix sliders (0.0–1.0), blend distance slider

## 7. Testing

- [ ] 7.1 Write `tests/audio/test_audio_reverb_zones.cpp`: test `CAudioReverbZone` serialization round-trip for all nine preset values, both shape types, and boundary wet/dry values (0.0, 0.5, 1.0)
- [ ] 7.2 Test box overlap factor: listener at zone center → overlap = 1.0; at zone edge → overlap = blend edge value; outside zone + blend_distance → overlap = 0.0
- [ ] 7.3 Test sphere overlap factor: listener at sphere center → overlap = 1.0; at `radius - blend_distance/2` → overlap = 0.5; at `radius + blend_distance` → overlap = 0.0
- [ ] 7.4 Test 2-zone cap: create three overlapping zones with overlap factors 0.9, 0.7, 0.5; verify the third zone (0.5) has its send level zeroed and the other two retain their values
- [ ] 7.5 Test Lua `set_reverb_preset` with all nine valid preset name strings; verify no errors and correct enum stored
- [ ] 7.6 Test Lua `set_reverb_preset` with an invalid string `"InvalidPreset"`; verify a Lua error is thrown with message containing the invalid name
- [ ] 7.7 Test `set_preset()` with `Dry` preset: verify submix wet/dry mix is 0 on the XAudio2 voice (mock XAudio2 call recording)
- [ ] 7.8 Test that `AudioReverbSystem::update()` correctly zeroes send levels for all zones when the listener is outside all zone + blend_distance volumes
- [ ] 7.9 Verify ≥95% line and branch coverage on `audio_reverb_system.cpp` and the `CAudioReverbZone` serialization paths

## 8. Documentation

- [ ] 8.1 Update `docs/specs/audio-system.md`: document `CAudioReverbZone` component fields, all nine presets and their intended environments, 2-zone overlap cap, blend distance semantics, and Lua API
- [ ] 8.2 Update `docs/specs/scene-system.md`: document `CAudioReverbZone` in the component registry table and its `.uzscene` serialization keys
- [ ] 8.3 Document editor gizmo color-per-preset convention in `docs/specs/editor-gizmos.md`
