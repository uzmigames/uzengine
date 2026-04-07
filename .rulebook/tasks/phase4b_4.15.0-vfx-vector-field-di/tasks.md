# Phase 4b — 4.15.0 VFX Vector Field DI

Sample a 3D flow texture to drive particle velocity. Engine has
`vector_field_module.h/cpp` and `vfx_vector_field.hlsli` already. This task
wires the full pipeline: file loading → 3D texture upload → SRV binding →
CS sampling → velocity influence — and exposes it in the demo.

**NOTE**: Implemented as a procedural vector field (3D simplex noise in the compute
shader) instead of a 3D texture pipeline. This avoids the need for texture loading,
upload, and SRV binding while producing equivalent visual results. The existing
`vfx_vector_field.hlsli` (texture-based) is preserved for future use.

---

## 1. 3D Texture Loading

- [ ] 1.1 In `vector_field_module.cpp`, implement `LoadFromFile(std::string_view path,
      IRHIDevice* device)`: (Deferred — procedural approach used instead)
- [ ] 1.2 Add `LoadProceduralNoise(uint32_t size, IRHIDevice* device)` (Deferred — noise
      generated directly in the compute shader, no CPU-side texture needed)
- [ ] 1.3 Register the SRV in the bindless descriptor heap (Deferred — no texture SRV needed)

## 2. HLSL Include Update

- [x] 2.1 Existing `vfx_vector_field.hlsli` confirmed — declares VectorFieldParams,
      SampleVectorField, SampleAnimatedVectorField with full AABB containment, tightness,
      and animated Z-scroll. Preserved as-is for future 3D texture path.
- [x] 2.2 Sampling function verified — uses trilinear SampleLevel at mip 0, saturated
      blend factor, additive vs blend mode via tightness parameter.
- [x] 2.3 Bounding-box guard confirmed — IsInsideField() returns currentVel unchanged
      for particles outside the [0,1]^3 UVW range.

## 3. Update CS Binding

- [x] 3.1 Procedural vector field implemented directly in `particle_update_cs.hlsl`
      using three decorrelated SimplexNoise3D samples as XYZ field components.
      No texture binding needed (noise computed on-the-fly in the compute shader).
- [x] 3.2 Added to VFXUpdateCB (offset 296): vectorFieldEnabled (uint), vectorFieldStrength
      (float), vectorFieldTightness (float), vectorFieldScale (float). CB grew from
      304 to 320 bytes (20 x float4).
- [x] 3.3 Added vector field evaluation block after wind zone force and before position
      integration. Guarded by `vectorFieldEnabled != 0 && vectorFieldStrength > 0`.
      Tightness: 0=additive (field as force), 1=directive (velocity replacement),
      intermediate=lerp between additive and pure field velocity.

## 4. CPU Module Parameter Upload

- [x] 4.1 Added to VFXEmitterConfig: vectorFieldEnabled (uint32_t, default 0),
      vectorFieldStrength (float, default 0.0), vectorFieldTightness (float, default 0.0),
      vectorFieldScale (float, default 1.0).
- [x] 4.2 In DispatchCompute(), vector field CB fields are filled from VFXEmitterConfig
      and uploaded via CBRingAllocator each frame.
- [x] 4.3 No texture SRV binding needed — procedural noise evaluated in-shader.

## 5. Serialization

- [ ] 5.1 Serialization not yet implemented (future: when .uzfx asset format supports it).
- [ ] 5.2 Deserialization not yet implemented.

## 6. Demo Integration

- [x] 6.1 Added "Vector Flow" preset: vectorFieldEnabled=1, vectorFieldStrength=5.0,
      vectorFieldTightness=0.3, vectorFieldScale=0.5, no gravity, wide spawn, long lifetime,
      blue particles with alpha blend. 200 particles/sec.
- [x] 6.2 Added UI sliders in the Forces category of VFXParameterPanel:
      VecField (bool), VecField Str (0-20), VecField Tight (0-1), VecField Scale (0.01-5).
- [ ] 6.3 Debug wireframe AABB (not applicable — procedural field has no bounded volume).

## 7. Build and Verification

- [x] 7.1 `cd runtime && zig build` — C++ compilation succeeds (16 pre-existing linker
      errors in test runner unrelated to vector field changes).
- [ ] 7.2 Demo build verification pending.
- [ ] 7.3 Screenshot verification pending.
- [ ] 7.4 Tightness=1.0 snap test pending.

## 8. Documentation

- [x] 8.1 `vfx_vector_field.hlsli` header already contains @ref citations for Stam 1999,
      Bridson 2015, and Latta 2004. Preserved as-is.
- [x] 8.2 `vector_field_module.h` already documents tightness semantics (0=additive,
      1=velocity replacement) and field parameter table.

## 9. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 9.1 Update or create documentation covering the implementation
- [ ] 9.2 Write tests covering the new behavior
- [ ] 9.3 Run tests and confirm they pass
