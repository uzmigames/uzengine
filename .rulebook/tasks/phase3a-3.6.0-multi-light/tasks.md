## 1. Implementation

- [ ] 1.1 Define LightGPU struct (type, position, direction, color, intensity, range, innerCone, outerCone)
- [ ] 1.2 Expand PerFrameCB with light count + LightGPU array (max 8), maintain 256B alignment
- [ ] 1.3 Add HLSL LightData struct and cbuffer array matching C++ layout
- [ ] 1.4 Implement point light evaluation in mesh_ps.hlsl (inverse-square falloff, smooth boundary)
- [ ] 1.5 Implement spot light evaluation in mesh_ps.hlsl (inner/outer cone angle, directional falloff)
- [ ] 1.6 Add N-light accumulation loop in pixel shader (directional + point + spot)
- [ ] 1.7 Implement CPU light culling (distance sort, select top-N by screen contribution)
- [ ] 1.8 Collect active lights from RenderWorld proxies and upload to LightBuffer each frame
- [ ] 1.9 Update deferred lighting pass to iterate light array (blocked on 3.3.0)
- [ ] 1.10 Create multi-light demo (sun + 4 colored point lights + 2 spot lights)

## 2. Testing

- [ ] 2.1 Test LightGPU struct size and alignment
- [ ] 2.2 Test PerFrameCB layout with light array (offsets, total size 256B or multiple)
- [ ] 2.3 Test point light attenuation math (inverse-square, zero at range boundary)
- [ ] 2.4 Test spot light cone falloff (full intensity inside inner cone, zero outside outer cone)
- [ ] 2.5 Test CPU light culling (nearest N selected, distance-sorted)
- [ ] 2.6 Test light count = 0 produces same result as ambient-only
- [ ] 2.7 Verify 95%+ coverage on all new code

## 3. Documentation

- [ ] 3.1 Update `docs/engine/12-rendering/06-lighting.md` with multi-light implementation details
- [ ] 3.2 Add light buffer layout and culling strategy to rendering docs
