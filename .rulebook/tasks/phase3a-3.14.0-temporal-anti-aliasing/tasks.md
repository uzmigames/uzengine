## 1. Implementation

- [ ] 1.1 Implement Halton(2,3) jitter sequence generator — 8-sample cycle, returns float2 sub-pixel offset in [-0.5, 0.5] range. Apply as projection matrix offset each frame.
- [ ] 1.2 Implement velocity buffer generation shader (`velocity_ps.hlsl`) — depth reprojection using current/previous ViewProj matrices. Output RG16F motion vectors in screen-space pixels. Handle camera-only motion first.
- [ ] 1.3 Implement per-object velocity contribution — skeletal mesh and dynamic objects write object-space velocity to velocity buffer via vertex shader (prevWorldPos → currentWorldPos delta).
- [ ] 1.4 Implement TAA resolve shader (`taa_resolve_ps.hlsl`) — sample current frame (unjittered UV), reproject history via velocity buffer, bilinear sample history texture.
- [ ] 1.5 Implement YCoCg neighborhood clamping — convert current frame 3×3 neighborhood to YCoCg, compute min/max AABB, clamp reprojected history to this box. Use `varianceClipGamma` (1.0 default) for tightness control.
- [ ] 1.6 Implement temporal blend — `lerp(clampedHistory, current, currentFrameWeight)` with currentFrameWeight=0.04 (UE5 default). Increase weight when velocity magnitude is high (fast motion favors current frame).
- [ ] 1.7 Implement history ping-pong — double-buffer RGBA16F history textures, swap read/write each frame. Handle first frame (no history → output current directly).
- [ ] 1.8 Implement responsive AA stencil — mark UI/particle pixels with stencil bit, use faster convergence (weight=0.25) for marked pixels to reduce blur on alpha-tested content.
- [ ] 1.9 Add TAA pass to render graph — insert after DoF/motion blur, before tonemap. Read: scene color, velocity buffer, depth, history[N-1]. Write: history[N], resolved output.
- [ ] 1.10 Add TAAPassDesc to PostProcessStack — enabled/disabled toggle, quality enum (Off/Low/High), currentFrameWeight, varianceClipGamma parameters.
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
