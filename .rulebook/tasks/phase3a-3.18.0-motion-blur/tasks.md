## 1. Implementation

- [ ] 1.1 Implement velocity buffer generation — reuse from TAA task (3.14.0). Camera reprojection via prev/current ViewProj. Per-object: vertex shader writes `currentPos - prevPos` scaled by frame time. Output RG16F.
- [ ] 1.2 Implement velocity scaling — `velocityScale = 0.5 * targetFPS / currentFPS * shutterAngle / 360.0`. Applied to velocity buffer before blur. UE5 defaults: targetFPS=30, shutter=180°.
- [ ] 1.3 Implement tile max compute shader (`motionblur_tilemax_cs.hlsl`) — 16×16 tiles, find max velocity magnitude in tile. Output RG16F (dominant velocity direction + magnitude).
- [ ] 1.4 Implement neighbor max compute shader (`motionblur_neighbormax_cs.hlsl`) — for each tile, take max velocity from 3×3 tile neighborhood. Ensures fast object blur extends beyond its tile.
- [ ] 1.5 Implement scatter-as-gather blur shader (`motionblur_gather_ps.hlsl`) — for each pixel: sample `sampleCount` points along velocity vector (jittered). Also sample along neighbor tile's dominant velocity. Weight by depth (foreground priority). Blend samples. Clamp to maxBlurRadius.
- [ ] 1.6 Implement depth-aware weighting — when foreground (closer depth) and background overlap, foreground velocity dominates. Prevents background bleeding through fast foreground objects.
- [ ] 1.7 Implement static skip — if pixel velocity < threshold (0.5 pixels) AND neighbor tile velocity < threshold, skip blur (passthrough). Saves cost on static regions.
- [ ] 1.8 Add MotionBlurPassDesc to PostProcessStack — enabled/disabled, quality (Off/Low/Medium/High/VeryHigh), blurAmount, maxBlurRadius, targetFPS, shutterAngle, perObjectScale.
- [ ] 1.9 Register motion blur passes in render graph — tile max → neighbor max → gather. After DoF, before TAA.
- [ ] 1.10 Create motion blur demo — scene with rotating objects + orbital camera, toggle on/off, adjustable blur amount and target FPS.

## 2. Visual Verification

- [ ] 2.1 Screenshot: camera pan — smooth directional streaks along pan direction
- [ ] 2.2 Screenshot: spinning object — radial blur on moving parts, sharp on static background
- [ ] 2.3 Screenshot: static scene — no blur when nothing moves
- [ ] 2.4 Screenshot: extreme velocity clamping — fast spin doesn't produce pixel soup
- [ ] 2.5 Regression: existing demos still work

## 3. Documentation

- [ ] 3.1 Document motion blur parameters with UE5 equivalents
- [ ] 3.2 Add shutter angle / target FPS explanation for artists
- [ ] 3.3 Document velocity buffer sharing with TAA
