# Proposal: Global Illumination System — Indirect Bounce Lighting

## Why

UzEngine currently has ZERO indirect bounced light. The only ambient contribution comes from the SkyLight SH irradiance (sky cubemap projection). This means:

- Shadowed areas are too dark compared to UE5
- No color bleeding (red wall doesn't tint nearby white wall)
- Indoor scenes are pitch black without explicit lights
- The visual gap with UE5 is ~30-40% attributable to missing GI

In UE5, Lumen (their proprietary GI system) provides real-time bounced light via software ray tracing against signed distance fields and screen-space traces. Without an equivalent system, UzEngine cannot match UE5's visual quality regardless of how perfect our direct lighting, SkyLight, and fog systems are.

## Scope

This task covers the design and implementation of a real-time Global Illumination system for UzEngine. The system will be called **UzGI** (or similar non-proprietary name).

## Technical Approaches (ordered by implementation complexity)

### Tier 1: Screen-Space GI (SSGI) — Recommended First Step
- Reuse existing depth + normal GBuffer to trace short-range bounces
- Similar to UE5's SSGI fallback (used when Lumen is off)
- Quality: decent for nearby surfaces, fails for off-screen/occluded geometry
- Complexity: Medium (2-3 compute shaders, ~1 week)
- UE5 ref: `ScreenSpaceDiffuseIndirect.usf`, `IndirectLightRendering.cpp`

### Tier 2: Irradiance Probes (Light Probe Grid)
- 3D grid of SH probes baked or updated in real-time
- Similar to UE5's Irradiance Volume, Unity's Light Probe Group
- Quality: smooth low-frequency GI, good for large environments
- Complexity: Medium-High (probe placement, octahedral encoding, blending, ~2 weeks)
- UE5 ref: `IrradianceCacheComposite.usf`, Radiance Caching in Lumen

### Tier 3: Voxel Cone Tracing (VXGI)
- Voxelize scene into 3D texture, trace cones for diffuse/specular GI
- Higher quality than probes, handles dynamic scenes well
- Complexity: High (voxelization pass, 3D mip-chain, cone tracing, ~3-4 weeks)
- Not used in UE5 (they chose SDF tracing instead) but well-documented in literature

### Tier 4: Software Ray Tracing against Distance Fields
- Closest to UE5 Lumen's approach: trace rays against mesh distance fields
- Highest quality for real-time GI
- Complexity: Very High (SDF generation, BVH, ray march, radiance cache, ~6-8 weeks)
- UE5 ref: `LumenRadianceCache.usf`, `LumenScreenProbeGather.usf`

## Recommended Implementation Order

1. **Phase A**: SSGI (Tier 1) — immediate visual improvement, uses existing GBuffer
2. **Phase B**: Irradiance Probes (Tier 2) — stable base for off-screen GI
3. **Phase C**: SSGI + Probes hybrid — SSGI for near, probes for far (UE5 pattern)
4. **Phase D**: (Future) SDF tracing or VXGI for highest quality

## Impact
- Affected code: `runtime/engine/renderer/` (new GI subsystem), `runtime/shaders/runtime/` (new compute shaders)
- Affected passes: deferred lighting (add GI term), render graph (new passes)
- Breaking change: NO (additive — GI is a new lighting term added to existing ambient)
- Performance budget: target <2ms at 1080p on RTX 3060

## Non-Goals
- Baked lightmaps (separate system, not real-time GI)
- Hardware ray tracing (DXR) — keep as optional enhancement, not requirement
- Path tracing — reference mode only, not real-time
