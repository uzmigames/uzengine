## 1. Implementation

- [x] 1.1 Implement velocity buffer generation — fullscreen depth-reprojection pass (velocity_ps.hlsl + velocity_pass.cpp). VelocityCB at b2 carries prevViewProj + screenWidth/screenHeight. Pass wired in render graph after GBuffer, before GTAO. PSO added to PipelineLibrary::GetVelocityPass(). prevViewProj tracking moved outside TAA block to run every frame.
- [x] 1.2 Implement velocity scaling — CPU-side `motionBlurScale = (1/targetFPS) / dt * amount * 0.5` (UE5 SceneVisibility.cpp:5428-5431 + PostProcessMotionBlur.cpp:613). Replaces `motionBlurAmount` root constant with pre-computed `motionBlurScale` in motion_blur_cs.hlsl cbuffer. Defaults: targetFPS=30, amount=0.5.
- [x] 1.3 Implement tile max compute shader (`motionblur_tilemax_cs.hlsl`) — 16x16 tiles, parallel reduction in groupshared to find max velocity magnitude. Output RG16F at (width/16)x(height/16). Pipeline registered in PipelineLibrary::GetMotionBlurTileMax(). UE5 source: MotionBlurVelocityFlatten.usf:79-165.
- [x] 1.4 Implement neighbor max compute shader (`motionblur_neighbormax_cs.hlsl`) — 3x3 tile neighborhood max velocity. Output RG16F same tile dimensions. Pipeline registered in PipelineLibrary::GetMotionBlurNeighborMax(). UE5 source: MotionBlurTileGather.usf:24-91. Both passes wired in render graph before gather pass with proper UAV barriers.
- [x] 1.5 Implement per-pixel motion blur compute shader (`motion_blur_cs.hlsl`) — bidirectional gather along velocity vector with IGN jitter, depth-aware weighting, velocity clamping, adaptive sample count (4-16). Based on UE5 MotionBlurApply.usf fast path (lines 432-454). 8 DWORD root constants, UZ_BINDLESS support, entry point `main`.
- [x] 1.6 Implement depth-aware weighting — ComputeDepthWeight using UE5's center-vs-sample comparison (MotionBlurApply.usf:235-238) with SOFT_Z_EXTENT=1. Foreground priority prevents background bleeding.
- [x] 1.7 Implement static skip — velocity < 0.5 pixels (CONFIG_MINIMAL_PIXEL_VELOCITY from MotionBlurCommon.ush:34) passthrough to center color. Saves cost on static regions.
- [ ] 1.8 Add MotionBlurPassDesc to PostProcessStack — enabled/disabled, quality (Off/Low/Medium/High/VeryHigh), blurAmount, maxBlurRadius, targetFPS, shutterAngle, perObjectScale.
- [x] 1.9 Register motion blur pass in render graph — single gather pass wired between TAA and tonemap. Reads HDR scene color (or TAA output), velocity buffer, depth. Writes to RGBA16F temp texture. Motion blur output SRV passed to tonemap. Tile max/neighbor max passes not yet implemented (simplified single-pass gather). Pipeline created in PipelineLibrary::GetMotionBlur(). Resources created/destroyed in CreateMotionBlurResources/DestroyMotionBlurResources.
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
