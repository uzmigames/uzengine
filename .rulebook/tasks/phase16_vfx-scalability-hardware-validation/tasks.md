## 1. Seek warmup loop
- [ ] 1.1 Add `kSeekFixedDt = 1.0f / 60.0f` constant near existing group-size constants in `frame_vfx_inline.cpp`
- [ ] 1.2 In `DispatchVFXSimulation`, after `gpuBuf.needsInitialization` reinit + before the regular spawn dispatch, detect `emitter->GetPendingSeekTime() > 0` and run the warmup loop
- [ ] 1.3 Loop dispatches `numTicks = ceil(pendingSeekTime / kSeekFixedDt)` iterations of spawn + update + indirect-args CS with `dt = kSeekFixedDt` and incrementing `frameNumber` for spawn-time RNG variation
- [ ] 1.4 Call `emitter->ClearPendingSeek()` once the loop completes
- [ ] 1.5 Register `r.VFX.Seek.MaxTicksPerFrame` (default 256) and split long warmups across frames when the budget is exceeded

## 2. GPU integration tests
- [ ] 2.1 `test_vfx_seek_determinism.cpp` — continuous 2.5s sim vs `Seek(2.5)` -> identical alive counts + sample positions within bitwise tolerance
- [ ] 2.2 `test_vfx_simulate_paused_freezes_particles.cpp` — frame N vs frame N+10 with `r.VFX.SimulatePaused=1` -> PSNR > 50 dB
- [ ] 2.3 `test_vfx_max_total_particles_caps_alive_count.cpp` — 20k pre-seeded particles, set CVar=1000, alive count converges <=1000 within 5 frames

## 3. Documentation
- [ ] 3.1 Append measured `Seek` warmup timings (1k / 4k / 16k particle capacities) to `docs/engine/25-vfx/100-scalability-cvars.md`

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
- [ ] 4.2 Write tests covering the new behavior
- [ ] 4.3 Run tests and confirm they pass
