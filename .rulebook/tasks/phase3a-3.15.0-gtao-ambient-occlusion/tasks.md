## 1. Implementation

- [ ] 1.1 Implement depth mip chain generation — compute shader that generates min-depth mips from full-res depth buffer (4 levels). Used for hierarchical horizon search.
- [x] 1.2 Implement GTAO main compute shader (`gtao_cs.hlsl`) — full-res, 12 taps, 2 directions, UE5 GTAO algorithm (SearchForLargestAngleDual + ComputeInnerIntegral). Output R8_UNORM AO.
- [ ] 1.3 Implement view-space normal reconstruction — derive normals from depth buffer gradients (dFdx/dFdy on depth). No G-Buffer normals needed for basic GTAO.
- [x] 1.4 Implement GTAO bilateral denoise (`gtao_denoise_cs.hlsl`) — 4×4 bilateral blur with depth similarity weighting. Edge-preserving.
- [ ] 1.5 Implement GTAO bilateral upsample (`gtao_upsample_ps.hlsl`) — upsample half-res AO to full-res using depth-aware bilateral weights. Sample 4 nearest half-res texels, weight by depth match to full-res depth.
- [ ] 1.6 Implement GTAO temporal accumulation (`gtao_temporal_cs.hlsl`) — reproject previous frame AO using velocity buffer, blend `lerp(history, current, 0.1)`. Reject history when velocity exceeds threshold.
- [x] 1.7 Implement distance fade — precomputed fadeMul/fadeAdd, applied in GTAO shader. Defaults: fadeStart=50000cm, fadeEnd=100000cm.
- [ ] 1.8 Implement AO power curve — `finalAO = pow(rawAO, aoPower)` with aoPower=2.0 (UE5 default). Artistic control over AO contrast.
- [ ] 1.9 Implement multi-bounce approximation — `ao_multibounce = ao / (1 - albedo * (1 - ao))` for colored ambient occlusion bounce. Optional, read albedo from G-Buffer.
- [ ] 1.10 Add GTAOPassDesc to PostProcessStack — enabled/disabled, quality (Low/Medium/High), radius, intensity, power, bias, fadeStart, fadeEnd parameters.
- [x] 1.11 Register GTAO passes in render graph — GTAO compute → denoise, after GBuffer, before lighting. AO SRV index passed via PerFrameCB::gtaoSrvIndex.
- [x] 1.12 Integrate AO into lighting — GTAO multiplied with material AO in deferred_lighting_ps.hlsl. Affects ambient only (direct light/shadows unaffected).
- [ ] 1.13 Create GTAO demo — scene with complex geometry (furniture, architectural corners, props), toggle AO on/off, adjustable radius/intensity sliders.

## 2. Visual Verification

- [ ] 2.1 Screenshot: GTAO ON vs OFF — clear contact darkening at object bases and corners
- [ ] 2.2 Screenshot: close-up of corner/crevice — smooth AO gradient without banding
- [ ] 2.3 Screenshot: half-res upsample quality — no visible block artifacts at depth edges
- [ ] 2.4 Screenshot: distance fade — near objects have AO, distant objects fade to 1.0
- [ ] 2.5 Screenshot: AO only view (debug output) — white=unoccluded, black=fully occluded
- [ ] 2.6 Regression: existing demos still work

## 3. Documentation

- [ ] 3.1 Document GTAO parameters and quality levels with UE5 equivalents
- [ ] 3.2 Add GTAO algorithm overview and tuning guide
- [ ] 3.3 Document AO integration in lighting pipeline (ambient only, not direct)
