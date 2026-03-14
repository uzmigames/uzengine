## 1. Implementation

- [ ] 1.1 Define ReflectionProbeComponent — capturePosition (Vec3), influenceRadius (float, default 10m), blendDistance (float, default 1m), resolution (uint, 128 default), priority (int), cubemapSrvIndex (uint).
- [ ] 1.2 Implement cubemap capture — render scene from probe position into 6 faces (RGBA16F), using existing GBuffer+lighting pipeline at reduced resolution. One probe per frame to amortize cost.
- [ ] 1.3 Implement probe prefiltering — run existing prefilter_cs.hlsl on captured cubemap to generate roughness mip chain (5 mips). Reuse IBL baker infrastructure.
- [ ] 1.4 Implement ReflectionProbeArray — allocate TextureCubeArray (max 16 probes) or individual cubemaps in bindless heap. Track active probe count.
- [ ] 1.5 Define ProbeData structured buffer — per-probe: position, radius, blendDistance, priority, cubemapSrvIndex. Upload as StructuredBuffer<ProbeData> to bindless heap.
- [ ] 1.6 Implement probe selection in deferred_lighting_ps.hlsl — reconstruct world position from depth, iterate active probes, find up to 2 overlapping probes by distance < influenceRadius. Blend by normalized distance with blendDistance fade.
- [ ] 1.7 Implement probe/sky IBL blending — inside probe influence: use probe cubemap. Outside all probes: fallback to sky IBL. In overlap zone: lerp between probe and sky by blend factor.
- [ ] 1.8 Add probe capture scheduling — capture one probe per frame round-robin. Mark probes dirty when scene changes. Support manual recapture trigger.
- [ ] 1.9 Create reflection_probes demo — room with 2 probes (interior hallway + outdoor), metallic spheres showing different reflections per location.

## 2. Visual Verification

- [ ] 2.1 Screenshot: metallic sphere inside room — must reflect room geometry, not sky
- [ ] 2.2 Screenshot: metallic sphere outside probe influence — must fallback to sky IBL
- [ ] 2.3 Screenshot: probe overlap zone — smooth blend between two probes, no hard edge
- [ ] 2.4 Screenshot: rough vs smooth surfaces — rough surfaces show blurred probe, smooth show sharp
- [ ] 2.5 Regression: existing IBL demos still work (sky IBL unchanged when no probes active)

## 3. Documentation

- [ ] 3.1 Update `docs/engine/12-rendering/06-lighting.md` with reflection probe system
- [ ] 3.2 Document probe placement guidelines (spacing, influence radius, priority rules)
