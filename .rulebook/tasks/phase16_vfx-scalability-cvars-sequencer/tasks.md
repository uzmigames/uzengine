## 1. Implementation
- [ ] 1.1 Register CVars `r.VFX.MaxTotalParticles`, `r.VFX.AllowGPUSim`, `r.VFX.QualityLevel`, `r.VFX.SimulatePaused`, `r.VFX.TimeScale` in cvar_registry
- [ ] 1.2 VFXSystem reads CVars at frame start and propagates to the budget allocator (MaxTotalParticles as hard ceiling)
- [ ] 1.3 Wire `AllowGPUSim=0` to force-disable emitters that require compute sim (log one-time warning per emitter)
- [ ] 1.4 Define quality preset table (0=low, 1=medium, 2=high, 3=cinematic) with spawn-rate + LOD-distance multipliers
- [ ] 1.5 Hook `SimulatePaused` into the emitter update — short-circuit dt advance but still run render
- [ ] 1.6 Implement per-emitter `timeScale` in the runtime `ParticleEmitter` — multiplies dt passed to Update CS
- [ ] 1.7 Implement deterministic `Seek(timeSeconds)` on ParticleEmitter via fixed-rate warmup from t=0 to target
- [ ] 1.8 Export C API: `uz_vfx_seek`, `uz_vfx_pause`, `uz_vfx_resume`, `uz_vfx_set_time_scale`
- [ ] 1.9 Add emitter lookup by name so external callers can find emitters by string ID
- [ ] 1.10 Lua bindings wrapping the C API
- [ ] 1.11 Integration test: seek deterministic fire effect to 2.5s, compare alive counts + positions against continuous sim
- [ ] 1.12 Test: `r.VFX.SimulatePaused=1` freezes particles (screenshot before + after should be identical)
- [ ] 1.13 Test: `r.VFX.MaxTotalParticles=1000` reduces alive count within 1 frame when starting at 20000

## 2. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
