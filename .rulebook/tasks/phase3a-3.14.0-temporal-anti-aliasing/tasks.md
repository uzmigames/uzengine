## 1. Implementation

- [x] 1.1 Implement Halton(2,3) jitter sequence generator — 8-sample cycle, centered [-0.5,0.5] range. Applied as projection matrix offset via `m[2][0]`/`m[2][1]` following UE5 `HackAddTemporalAAProjectionJitter` (SceneVisibility.cpp:5243). Jitter disabled when TAA off.
- [ ] 1.2 Implement velocity buffer generation shader (`velocity_ps.hlsl`) — depth reprojection using current/previous ViewProj matrices. Output RG16F motion vectors in screen-space pixels. Handle camera-only motion first.
- [ ] 1.3 Implement per-object velocity contribution — skeletal mesh and dynamic objects write object-space velocity to velocity buffer via vertex shader (prevWorldPos → currentWorldPos delta).
- [x] 1.4 Implement TAA resolve compute shader (`taa_resolve_cs.hlsl`) — SM6.6 bindless compute. Samples current frame, reprojects history (camera-only for now), bilinear history sampling. Entry point CSMain, 8x8 thread groups.
- [x] 1.5 Implement YCoCg neighborhood clamping — 3x3 VGPR cache in YCoCg space, plus+corner min/max AABB (UE5 HISTORY_CLAMPING_BOX_MIN_MAX), clamp history to box. All functions cite UE5 TemporalAA.usf line numbers.
- [x] 1.6 Implement temporal blend — HDR-weighted WeightedLerpFactors (UE5 TemporalAA.usf:2283-2293) with currentFrameWeight=0.04 (UE5 TemporalAA.cpp:41). Velocity modulation: lerp(blend, 0.2, saturate(v/40)) (usf:2233). Anti-flicker guard (usf:2246). NaN guard.
- [x] 1.7 Implement history ping-pong — double-buffer RGBA16F history textures (m_taaHistory[2]), swap read/write each frame via m_taaHistoryIndex. First frame: blendWeight=1.0 (use current only). CreateTaaResources/DestroyTaaResources in Init/Resize/Shutdown.
- [ ] 1.8 Implement responsive AA stencil — mark UI/particle pixels with stencil bit, use faster convergence (weight=0.25) for marked pixels to reduce blur on alpha-tested content.
- [x] 1.9 Add TAA pass to render graph — inserted after bloom, before tonemap. Reads: HDR scene color (SRV), depth (SRV), history[prev] (SRV). Writes: history[current] (UAV). Tonemap reads TAA output when active. Proper UAV/SRV barriers.
- [x] 1.10 Add TAA toggle — SetTaaEnabled()/IsTaaEnabled() on SceneRenderer. TAA auto-disabled on SM5 (requires bindless). Pipeline created lazily via PipelineLibrary::GetTaaResolve().
- [ ] 1.11 Wire FXAA as fallback — when TAA disabled, FXAA runs instead. When TAA enabled, FXAA is skipped.
- [ ] 1.12 Create TAA demo — scene with thin geometry (railings, wires, foliage), split-screen FXAA vs TAA comparison, camera orbit to show temporal stability.

## 2. Visual Verification

- [ ] 2.1 Screenshot comparison: FXAA vs TAA on thin geometry — TAA must show smoother edges without blur
- [ ] 2.2 Screenshot: fast camera rotation — no ghosting trails on static objects
- [ ] 2.3 Screenshot: moving object (rotating cube) — no trailing ghosts behind moving edges
- [ ] 2.4 Screenshot: TAA OFF showing jitter shimmer vs TAA ON showing stable image
- [ ] 2.5 Regression: existing demos (pbr_test, deferred_test, postprocess_test) still work

## 3. Documentation

- [ ] 3.1 Document TAA parameters and quality levels in post-processing docs
- [ ] 3.2 Add velocity buffer format and generation reference
- [ ] 3.3 Add TAA tuning guide (ghosting vs blur tradeoff, responsive AA usage)
