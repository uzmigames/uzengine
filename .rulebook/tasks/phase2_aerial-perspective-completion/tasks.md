## 1. Implementation

### 1.1 Aerial Perspective Volume Verification
- [ ] 1.1.1 Verify `AerialPerspectiveVolumePass` output: 3D texture dimensions, depth slice distribution (exponential vs linear), and format (RGBA16F for inscattering RGB + transmittance A).
- [ ] 1.1.2 Verify depth slicing follows Hillaire 2020: exponential distribution mapping world-space distance to slice index. Fix if linear.
- [ ] 1.1.3 Add `--vis aerial_perspective` target registration so the volume can be visualized with diagnostic tools.

### 1.2 Deferred Lighting Integration
- [ ] 1.2.1 Add `SampleAerialPerspective(float3 worldPos, float3 cameraPos)` function to a new `aerial_perspective.hlsli` header. Returns `float4(inscattering.rgb, transmittance)`.
- [ ] 1.2.2 In `deferred_lighting_ps.hlsl`, after direct + IBL lighting, apply aerial perspective: `color.rgb = color.rgb * aerialTransmittance + aerialInscattering`.
- [ ] 1.2.3 Bind the 3D aerial perspective texture as SRV in the deferred lighting pass setup (C++ side).

### 1.3 Height Fog Coordination
- [ ] 1.3.1 When aerial perspective is active, height fog should blend with (not replace) the aerial contribution. Adjust `height_fog_ps.hlsl` to account for this.
- [ ] 1.3.2 Add CVar `r.AerialPerspective` (0=off, 1=on, default=1) for toggling.

## 2. Testing

### 2.1 Visual Regression (Tier 2)
- [ ] 2.1.1 Capture outdoor scene at multiple camera distances (near=100, mid=1000, far=5000 units). Verify smooth atmospheric fade.
- [ ] 2.1.2 Compare aerial perspective ON vs OFF — verify distant objects blend naturally with sky.
- [ ] 2.1.3 Verify no artifacts at close range (aerial perspective contribution should be near-zero for nearby objects).
- [ ] 2.1.4 Verify height fog + aerial perspective combination looks correct (no double-fog).

### 2.2 Diagnostics
- [ ] 2.2.1 Run `--pass-dump` and verify aerial_perspective volume slices look reasonable (gradient from clear to foggy).
- [ ] 2.2.2 Run `--diag` and verify aerial perspective target status is OK (no NaN/Inf/zero).

### 2.3 Coverage
- [ ] 2.3.1 Verify 95%+ coverage on new C++ code.

## 3. Documentation
- [ ] 3.1 Update `docs/specs/rendering/shader-library.md` with `aerial_perspective.hlsli` and `SampleAerialPerspective()`.
- [ ] 3.2 Add `@ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"` citations.
