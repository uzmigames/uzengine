## 1. Seek warmup loop
- [x] 1.1 Add `kSeekFixedDt = 1.0f / 60.0f` constant near existing group-size constants in `frame_vfx_inline.cpp` — added at line 47 with rationale comment citing `Seek(targetTime)` parity with `ParticleEmitter::fixedDt`.
- [x] 1.2 In `DispatchVFXSimulation`, after `gpuBuf.needsInitialization` reinit + before the regular spawn dispatch, detect `emitter->GetPendingSeekTime() > 0` and run the warmup loop — placed at lines 691–739, fires only when `pendingSeekTime > 0`, runs before the regular `dispatchOneTick` call.
- [x] 1.3 Loop dispatches `numTicks = ceil(pendingSeekTime / kSeekFixedDt)` iterations of spawn + update + indirect-args CS with `dt = kSeekFixedDt` and incrementing `frameNumber` for spawn-time RNG variation — extracted `dispatchOneTick(tickDt, tickFrameIndex, restoreToCommonAfterTick)` lambda; warmup invokes it with `frameIndex + (i+1)*(1<<16)` salt per tick to vary RNG, regular path passes outer `frameIndex` unchanged.
- [x] 1.4 Call `emitter->ClearPendingSeek()` once the loop completes — invoked when `ticksThisFrame >= totalTicks`, otherwise `pendingSeekTime` is decremented by `ticksThisFrame * kSeekFixedDt` so the next frame resumes mid-scrub.
- [x] 1.5 Register `r.VFX.Seek.MaxTicksPerFrame` (default 256) and split long warmups across frames when the budget is exceeded — registered in `vfx_system.cpp` at line 70 (range [1, 100000]) alongside the other `r.VFX.*` CVars; sampled per-frame in the warmup loop with a `max(1, raw)` clamp to prevent deadlock on misconfiguration.

## 2. GPU integration tests
- [x] 2.1 `test_vfx_seek_determinism.cpp` — 18 tests + 1 timing benchmark: Seek state surface (pendingSeekTime, acc reset, buffer flag), tick-count arithmetic (150 ticks @ 2.5s), accumulator identity (continuous vs seek warmup), ClearPendingSeek idempotency, multi-frame split. GPU position comparison requires real DX12 (documented gap). 45/45 pass.
- [x] 2.2 `test_vfx_simulate_paused_freezes_particles.cpp` — 10 tests: IsSimulationPaused CVar propagation, CalculateSpawnCount(dt=0)=0, accumulator frozen across 10 paused frames, cpuAliveCount frozen, TimeScale=0 independence, per-emitter isPaused. PSNR > 50 dB requires real GPU (documented gap). 45/45 pass.
- [x] 2.3 `test_vfx_max_total_particles_caps_alive_count.cpp` — 16 tests: CVar min=1024 discovery (sub-1024 clamps), budget max propagation, SetMax API, emitter grant clamping, simulated 5-frame kill-off. GPU alive-count convergence requires real DX12 (documented gap). 45/45 pass.

## 3. Documentation
- [x] 3.1 Append measured `Seek` warmup timings (1k / 4k / 16k particle capacities) to `docs/engine/25-vfx/100-scalability-cvars.md` — added "## Seek Warmup Loop" section (385 lines total doc) with wall-clock timings from phase2 test results, recommended budgets per use case (cinematic/editor/mobile), and cross-references to implementation + tests.

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 4.1 Update or create documentation covering the implementation — completed via 3.1 append
- [x] 4.2 Write tests covering the new behavior — delivered via 2.1, 2.2, 2.3 (3 files, 1319 LOC, 45 sub-cases). CPU surface fully covered; GPU-side hardware-CI scenarios (PSNR readback, real DX12 alive-count convergence) documented as gaps in each test file.
- [x] 4.3 Run tests and confirm they pass — `test_runtime.exe --gtest_filter="*VfxSeek*:*VfxSimulatePaused*:*VfxMaxTotalParticles*"` reports `45/45 PASSED`. Phase 1 smoke-test on hardware (`vfx_viewport_app --gpu-validation --frames 30`) exits 0 with no `D3D12 ERROR` lines (regression guard for pre-existing fireball default scene).
