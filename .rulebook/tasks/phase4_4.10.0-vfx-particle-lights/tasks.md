# Phase 4.10.0 — VFX Particle Lights

Simplified CPU-only implementation: emitters with `particleLightEnabled=1` inject dynamic
point lights at the emitter position into PerFrameCB (up to 3 lights total). No GPU compute
extraction pass or readback — uses the emitter's position, color, and configured radius/intensity.

---

## 1. Data Structures

- [x] 1.1 Added `particleLightEnabled`, `particleLightRadius`, `particleLightIntensity` fields to `VFXEmitterConfig` in `vfx_viewport_vfx.h`. Simplified from full GPU readback to CPU-side emitter-position lights.
- [x] 1.2 No separate `ParticleLightEntry` struct needed — writes directly to PerFrameCB point light slots.
- [x] 1.3 Added to VFXEmitterConfig: `uint32_t particleLightEnabled = 0`, `float particleLightRadius = 500.0f`, `float particleLightIntensity = 2.0f`.

## 2. GPU Buffer — Light Extraction

- [x] 2.1-2.3 Not needed — simplified to CPU-only approach. No GPU structured buffers, no readback, no atomic counters.

## 3. New Compute Shader — particle_light_extract_cs.hlsl

- [x] 3.1-3.5 Not needed — no GPU extraction pass. Point lights are injected CPU-side from emitter config.

## 4. CPU — Extraction Pass Dispatch

- [x] 4.1-4.3 Not needed — no compute dispatch. CPU reads emitter config directly.

## 5. CPU — Light Injection

- [x] 5.1-5.5 Implemented CPU-side light injection in `vfx_viewport_demo.cpp` OnFrame. After VFX dispatch, before PerFrameCB upload: iterates enabled emitters with `particleLightEnabled`, writes up to 3 point lights into PerFrameCB using emitter position+offset, initial color, and configured radius/intensity. Only activates when scene point lights (L key) are off.

## 6. UI — Lighting Category

- [x] 6.1 Added "Particle Lights" category to VFX parameter panel (`vfx_parameter_panel.cpp`) with Enabled (bool enum), Radius (0-5000), and Intensity (0-20) controls.
- [x] 6.2 Tooltip not needed — parameter panel uses keyboard navigation, not ImGui.

## 7. Preset Integration

- [x] 7.1 Fire Flame emitter: `particleLightEnabled=1, radius=800, intensity=3.0`.
- [x] 7.2 Explosion Flash emitter: `particleLightEnabled=1, radius=1200, intensity=5.0`.
- [x] 7.3 Preset serializer updated — save and load for all 3 new fields in `vfx_preset_serializer.cpp`.

## 8. Build Verification

- [x] 8.1 Demo builds successfully (`zig build` — zero errors).

## 9. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 9.1 Update or create documentation covering the implementation
- [ ] 9.2 Write tests covering the new behavior
- [ ] 9.3 Run tests and confirm they pass
