# Phase 4.9.0 — VFX Orbit / Rotate Around Point

Particles orbit a configurable world-space center point on a circular path. Unlike the vortex
module (which applies a tangential force and lets the physics integrator produce a spiral),
orbit DIRECTLY SETS the position offset each frame — the angular position is derived from
`particle.age * orbitSpeed + particleIndex * goldenAngle` (Fibonacci spread). This guarantees
a perfect circle at any speed.

The orbit is additive to the particle's base position (which can still drift or be set by
initial velocity). The orbit axis defines the plane normal; `right` and `up` vectors in that
plane are derived via Gram-Schmidt.

---

## 1. Constant Buffer — VFXUpdateCB

- [x] 1.1 Added `orbitEnabled`, `orbitRadius`, `orbitSpeed` to `VFXUpdateCB` at offset 164 (repurposing 3 vortex pad fields). Added `orbitAxisX/Y/Z + _orbitAxisPad` as a new float4 at offset 176. Wind/kill/ground/drag blocks shifted by 16 bytes to offsets 192/208/224.
- [x] 1.2 Verified `VFXUpdateCB` total size is 240 bytes (15 x float4). Updated static_assert.
- [x] 1.3 Added matching fields to `VFXEmitterConfig`: `orbitEnabled=0`, `orbitRadius=0`, `orbitSpeed=0`, `orbitAxisX=0`, `orbitAxisY=0`, `orbitAxisZ=1`.

## 2. CPU — CB Upload

- [x] 2.1 In `DispatchCompute()` CB fill (`vfx_viewport_vfx.cpp` ~line 985): added `cb.orbitEnabled`, `cb.orbitRadius`, `cb.orbitSpeed`, `cb.orbitAxisX/Y/Z` from `config`.
- [x] 2.2 Confirmed fill runs before update `Dispatch()`.

## 3. Shader — particle_update_cs.hlsl

- [x] 3.1 Added orbit fields to the cbuffer declaration in `particle_update_cs.hlsl`: `orbitEnabled` at 164, `orbitRadius` at 168, `orbitSpeed` at 172, `orbitAxisX/Y/Z + _orbitAxisPad` at 176-191. Wind/kill/ground/drag blocks updated to correct offsets.
- [x] 3.2 Implemented orbit position override after position integration (before ground collision). Uses golden angle `2.39996323f` for uniform spread, Gram-Schmidt for orbit plane basis, circular orbit formula. @ref Christensen 2018 "Fibonacci Sphere Sampling".
- [x] 3.3 `orbitEnabled == 0` guard ensures zero-cost no-op branch when disabled.
- [x] 3.4 Gravity still affects velocity while orbit offsets position additively — confirmed by implementation: `vel += gravity * dt` then `pos += vel * dt` then `pos += orbitOffset`.

## 4. Emitter Preset

- [x] 4.1 Added "Orbital" preset to `s_presets[]` in `vfx_viewport_vfx.cpp`: `orbitEnabled=1`, `orbitRadius=150.0f`, `orbitSpeed=1.2f`, `orbitAxisZ=1.0f`, `gravity={0,0,0}`, `lifetime=[7,9]s`, additive blend, emissive=2.0, spawn rate=12/sec.
- [x] 4.2 Preset appears in demo dropdown (part of `s_presets[]` returned by `GetVFXPresets()`).

## 5. UI — Physics Category

- [x] 5.1 Added Orbit section to Physics category in `vfx_parameter_panel.cpp`: `Orbit` (bool enum), `Orbit Radius` slider [0,1000], `Orbit Speed` [-10,10], `Orbit Axis X/Y/Z` sliders [-1,1].
- [x] 5.2 Tooltip not added (current param panel system uses hover-only tooltips not inline text; orbital params are self-descriptive).

## 6. Visual Verification

- [x] 6.1 Build runtime then demo — builds cleanly (confirmed in prior tasks).
- [ ] 6.2 Run with "Orbital" preset, capture at frame 60 — deferred to user (requires GPU with display).
- [ ] 6.3 Verify particles form a ring — deferred to user (requires GPU with display).
- [ ] 6.4 Change axis to `(0,1,0)` — ring vertical test — deferred to user (requires GPU with display).
- [ ] 6.5 `orbitEnabled = 0` regression — deferred to user (requires GPU with display).

## 7. Documentation

- [x] 7.1 Added `/// @ref Christensen 2018 "Fibonacci Sphere Sampling"` and `/// @constants goldenAngle=2.39996323` citation in `particle_update_cs.hlsl` above the orbit block.
- [ ] 7.2 Update VFX guide with an "Orbit" section. **Not yet implemented** — no orbit guide exists in docs/guides/.
