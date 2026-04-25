## 1. Implementation
- [x] 1.1 Register CVars `r.VFX.MaxTotalParticles`, `r.VFX.AllowGPUSim`, `r.VFX.QualityLevel`, `r.VFX.SimulatePaused`, `r.VFX.TimeScale` in `VFXSystem::Initialize` (`engine/vfx/vfx_system.cpp`)
- [x] 1.2 `VFXSystem::Tick` samples all 5 CVars once per frame and caches them on the system; `MaxTotalParticles` propagates to `ParticleBudget::SetMax` immediately
- [x] 1.3 `DispatchVFXSimulation` early-outs with one-time warning when `r.VFX.AllowGPUSim = 0`; `r.VFX.SimulatePaused = 1` short-circuits the global dt to zero before per-emitter scaling
- [x] 1.4 Quality preset table (low/medium/high/cinematic) with spawn-rate + LOD-distance multipliers exposed via `GetQualitySpawnRateMultiplier()` / `GetQualityLodDistanceMultiplier()`
- [x] 1.5 `r.VFX.SimulatePaused` hook integrated into the inline sim path — when set, `globalDt` is forced to 0 before the per-emitter `isPaused` / `timeScale` filters apply
- [x] 1.6 New `ParticleEmitter::timeScale` field (default 1.0). `DispatchVFXSimulation` computes `emitterDt = isPaused ? 0 : (globalDt * timeScale)` and feeds it into both spawn and update CB
- [x] 1.7 `ParticleEmitter::Seek(targetTime)` API shipped: sets `pendingSeekTime`, resets `cpuAliveCount` + `spawnAccumulator`, flags `buffer.needsInitialization`. The on-frame warmup loop that consumes `pendingSeekTime` is moved to follow-up `phase16_vfx-scalability-hardware-validation` (multi-dispatch barrier verification needs DX12 hardware)
- [x] 1.8 C API exported in `engine/app/runtime.h`: `uz_vfx_seek`, `uz_vfx_pause`, `uz_vfx_resume`, `uz_vfx_set_time_scale`. All four resolve via `VFXSystem::FindEmitterByName` and return `0` / `1` for failure / success; null name + missing runtime are logged as warnings
- [x] 1.9 `VFXSystem::FindEmitterByName(std::string)` lookup added; new `ParticleEmitter::name` field (default empty) provides the lookup key. `SceneRenderer::GetVFXSystem()` accessor exposes the active VFX system to the C API
- [x] 1.10 Lua bindings (`engine/scripting/scene/vfx_lua_bindings.cpp`): `uz.vfx:freeze(handle, frozen)`, `uz.vfx:set_time_scale(handle, scale)`, `uz.vfx:seek(handle, t)`, `uz.vfx:find_by_name(name)` -> handle | nil. Distinct from existing `uz.vfx:pause` (which zeroes emissionRate); `freeze` sets `isPaused` so the sim freezes alive particles while emission state is preserved
- [x] 1.11 -> moved to follow-up `phase16_vfx-scalability-hardware-validation` section 2.1 (deterministic-seek integration test needs DX12 hardware to verify multi-dispatch barrier ordering)
- [x] 1.12 -> moved to follow-up `phase16_vfx-scalability-hardware-validation` section 2.2 (SimulatePaused screenshot equality requires real swapchain + GPU)
- [x] 1.13 -> moved to follow-up `phase16_vfx-scalability-hardware-validation` section 2.3 (MaxTotalParticles convergence test requires GPU readback ring)

## 2. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 2.1 Update or create documentation covering the implementation — `docs/engine/25-vfx/100-scalability-cvars.md` shipped (CVar table, quality multipliers, per-emitter state, Seek API, C / Lua surface, test coverage)
- [x] 2.2 Write tests covering the new behavior — `tests/engine/vfx/test_vfx_scalability_cvars.cpp` (19 tests across 4 suites: `VfxScalability`, `ParticleEmitterSeek`, `ParticleEmitterScalability`, `VfxFindByName`)
- [x] 2.3 Run tests and confirm they pass — `runtime/build/bin/test_runtime.exe --gtest_filter="VfxScalability*:ParticleEmitterSeek*:ParticleEmitterScalability*:VfxFindByName*"` -> 19/19 PASS
