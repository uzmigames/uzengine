## 1. LOD Tier Definitions and Configuration

- [ ] 1.1 Define `AudioLODTier` enum in `src/components/audio_source.h`: `LOD0 = 0` (0–20 m), `LOD1 = 1` (20–50 m), `LOD2 = 2` (50–100 m), `LOD3 = 3` (>100 m, virtual voice)
- [ ] 1.2 Add per-source LOD threshold fields to `CAudioSource`: `lod0_max_dist` (float, default 20), `lod1_max_dist` (float, default 50), `lod2_max_dist` (float, default 100); values of -1 mean "use global override from `audio_config.uzini`"
- [ ] 1.3 Add global LOD threshold overrides to `audio_config.uzini` parser: `lod0_max_dist`, `lod1_max_dist`, `lod2_max_dist` keys under `[AudioLOD]` section; per-source values take priority over globals when set to values other than -1
- [ ] 1.4 Add `current_lod_tier` (AudioLODTier, transient, not serialized) to `CAudioSource` runtime state so the scheduler can read it without recomputing distance each tick

## 2. Per-Tier DSP Chain Configuration

- [ ] 2.1 Implement `AudioLODConfig` struct in `src/audio/audio_lod.h` defining `dsp_flags` bitmask per tier: `LOD0` = `DSP_3D_PAN | DSP_REVERB_SEND | DSP_HQ_RESAMPLE | DSP_LPF`; `LOD1` = `DSP_3D_PAN | DSP_REVERB_SEND_REDUCED | DSP_LPF`; `LOD2` = `DSP_MONO_DOWNMIX | DSP_DISTANCE_ATTENUATION`; `LOD3` = `DSP_VIRTUAL`
- [ ] 2.2 Implement `AudioLODSystem::apply_tier(entity_id, AudioLODTier old_tier, AudioLODTier new_tier)` in `src/audio/audio_lod.cpp`: apply DSP chain changes, start crossfade timer (50 ms) when transitioning between LOD0–LOD2, set XAudio2 submix route accordingly
- [ ] 2.3 Implement the 50 ms crossfade on tier transition: linearly ramp outgoing tier submix volume from 1.0 to 0.0 and incoming tier submix volume from 0.0 to 1.0 over `crossfade_duration_ms = 50` milliseconds using per-frame XAudio2 volume updates
- [ ] 2.4 Implement LOD2 mono downmix: route the source voice through a mono submix node (1 input channel) before the distance attenuation stage; disable reverb and LPF send nodes for this voice

## 3. Virtual Voice Scheduler

- [ ] 3.1 Implement `VirtualVoiceScheduler` in `src/audio/audio_lod.cpp`: maintain a `std::unordered_map<entity_id_t, VirtualVoiceState>` tracking position, velocity, volume, and play cursor for LOD3 sources
- [ ] 3.2 On LOD3 entry: record the current XAudio2 voice play-cursor position and release the voice slot; store it in `VirtualVoiceState`
- [ ] 3.3 On LOD3 exit (source moves within range): on the next scheduler tick, allocate a new XAudio2 voice slot, seek to the computed cursor position (play cursor advanced by elapsed time since virtualization), and begin playback with the LOD2 DSP chain before transitioning to LOD1/LOD0
- [ ] 3.4 Virtual voice promotion priority: when multiple LOD3 sources become eligible simultaneously, promote the one with the lowest listener distance first; if no voice slots are available, defer promotion to the next tick

## 4. Per-Frame LOD Tier Recomputation

- [ ] 4.1 In `AudioSystem::update_3d()`, compute listener-to-source distance for all active `CAudioSource` components each frame using squared-distance comparison against threshold squares to avoid `sqrt` calls
- [ ] 4.2 When computed tier differs from `CAudioSource::current_lod_tier`, call `AudioLODSystem::apply_tier()` to initiate the tier transition
- [ ] 4.3 Ensure LOD tier recomputation runs after the transform system tick so positions are up-to-date within the same frame

## 5. Debug Console Overlay

- [ ] 5.1 Implement `debug.audio_lod_overlay` console command in `src/audio/audio_lod.cpp` toggling a per-source LOD tier color overlay in the editor viewport
- [ ] 5.2 Color coding: LOD0 = green label, LOD1 = yellow label, LOD2 = orange label, LOD3 (virtual) = red label; render as 2D billboard text at the world-space source position using the editor debug draw API
- [ ] 5.3 Display distance value and current tier name alongside each label: e.g., `"LOD1 | 34.2m"`

## 6. Testing

- [ ] 6.1 Write `tests/audio/test_audio_lod.cpp`: test `AudioLODSystem::apply_tier()` — move a source from LOD0 to LOD3 and verify the XAudio2 voice slot is released within one tick; verify `VirtualVoiceState` is populated with play cursor and timestamp
- [ ] 6.2 Test distance thresholds: place source at 19 m and verify tier is LOD0; move to 21 m and verify tier is LOD1; at 51 m verify LOD2; at 101 m verify LOD3
- [ ] 6.3 Test per-source threshold override: set `lod2_max_dist = 200` on a specific source; place at 150 m and verify tier is LOD2 (not LOD3)
- [ ] 6.4 Test global threshold from `audio_config.uzini`: set `lod0_max_dist = 10` globally; source at 15 m should be LOD1 when source has default threshold (-1)
- [ ] 6.5 Test crossfade timer: trigger LOD0→LOD1 transition and advance time by 25 ms; verify outgoing volume is approximately 0.5 and incoming volume is approximately 0.5
- [ ] 6.6 Test LOD3 virtual voice promotion: virtualize a source, advance playback time by 2000 ms, bring source into range; verify new voice starts at ~2000 ms cursor position (within one scheduler-tick margin)
- [ ] 6.7 Test mono downmix activation: verify LOD2 source outputs a 1-channel submix node and that reverb send level is 0.0
- [ ] 6.8 Test squared-distance comparison correctness against reference `sqrt` implementation for a set of 100 random positions with threshold 50 m; assert no disagreements
- [ ] 6.9 Verify all tests achieve ≥95% line and branch coverage on `audio_lod.cpp`

## 7. Documentation

- [ ] 7.1 Update `docs/specs/audio-system.md`: document the four LOD tiers, DSP chain per tier, crossfade behavior, virtual voice scheduler, and `audio_config.uzini` LOD keys
- [ ] 7.2 Document `CAudioSource` LOD threshold fields and per-source override semantics
- [ ] 7.3 Update `docs/specs/debug-profiling.md`: document `debug.audio_lod_overlay` console command syntax and color-coding legend
