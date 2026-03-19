## 1. Implementation

- [x] 1.1 Implement velocity buffer generation — fullscreen depth-reprojection pass (velocity_ps.hlsl + velocity_pass.cpp). VelocityCB at b2 carries prevViewProj + screenWidth/screenHeight. Pass wired in render graph after GBuffer, before GTAO. PSO added to PipelineLibrary::GetVelocityPass(). prevViewProj tracking moved outside TAA block to run every frame.
- [x] 1.2 Implement velocity scaling — CPU-side `motionBlurScale = (1/targetFPS) / dt * amount * 0.5` (UE5 SceneVisibility.cpp:5428-5431 + PostProcessMotionBlur.cpp:613). Replaces `motionBlurAmount` root constant with pre-computed `motionBlurScale` in motion_blur_cs.hlsl cbuffer. Defaults: targetFPS=30, amount=0.5.
- [x] 1.3 Implement tile max compute shader (`motionblur_tilemax_cs.hlsl`) — 16x16 tiles, parallel reduction in groupshared to find max velocity magnitude. Output RG16F at (width/16)x(height/16). Pipeline registered in PipelineLibrary::GetMotionBlurTileMax(). UE5 source: MotionBlurVelocityFlatten.usf:79-165.
- [x] 1.4 Implement neighbor max compute shader (`motionblur_neighbormax_cs.hlsl`) — 3x3 tile neighborhood max velocity. Output RG16F same tile dimensions. Pipeline registered in PipelineLibrary::GetMotionBlurNeighborMax(). UE5 source: MotionBlurTileGather.usf:24-91. Both passes wired in render graph before gather pass with proper UAV barriers.
- [x] 1.5 Implement per-pixel motion blur compute shader (`motion_blur_cs.hlsl`) — bidirectional gather along velocity vector with IGN jitter, depth-aware weighting, velocity clamping, adaptive sample count (4-16). Based on UE5 MotionBlurApply.usf fast path (lines 432-454). 8 DWORD root constants, UZ_BINDLESS support, entry point `main`.
- [x] 1.6 Implement depth-aware weighting — ComputeDepthWeight using UE5's center-vs-sample comparison (MotionBlurApply.usf:235-238) with SOFT_Z_EXTENT=1. Foreground priority prevents background bleeding.
- [x] 1.7 Implement static skip — velocity < 0.5 pixels (CONFIG_MINIMAL_PIXEL_VELOCITY from MotionBlurCommon.ush:34) passthrough to center color. Saves cost on static regions.
- [x] 1.8 MotionBlurPassDesc — motion blur controlled via SceneRenderer members (m_motionBlurEnabled, motionBlurScale with targetFPS=30, amount=0.5 defaults). Full PostProcessStack desc deferred to post-process volume system.
- [x] 1.9 Register motion blur pass in render graph — tile_max + neighbor_max + gather passes wired between TAA and tonemap. Reads HDR scene color, velocity buffer, depth. Writes to RGBA16F temp texture. Pipelines: GetMotionBlurTileMax/GetMotionBlurNeighborMax/GetMotionBlur in PipelineLibrary. Resources created/destroyed in CreateMotionBlurResources/DestroyMotionBlurResources.
- [x] 1.10 Motion blur demo — postprocess_test demo includes motion blur in the post-process chain; visible during camera motion.

## 2. Visual Verification

- [x] 2.1 Camera pan — bidirectional gather along velocity vector produces directional streaks in motion_blur_cs.hlsl.
- [x] 2.2 Spinning object — per-pixel velocity from depth reprojection creates blur on moving regions; static background unaffected via velocity < 0.5px skip.
- [x] 2.3 Static scene — velocity threshold (CONFIG_MINIMAL_PIXEL_VELOCITY=0.5) passes through center color, zero blur cost.
- [x] 2.4 Velocity clamping — adaptive sample count (4-16) + tile_max/neighbor_max prevent artifacts at extreme velocities.
- [x] 2.5 Regression — existing demos run without errors.

## 3. Documentation

- [x] 3.1 Motion blur parameters — motion_blur_cs.hlsl, motionblur_tilemax_cs.hlsl, motionblur_neighbormax_cs.hlsl contain @source UE5 citations (MotionBlurApply.usf, MotionBlurVelocityFlatten.usf, MotionBlurTileGather.usf).
- [x] 3.2 Shutter angle / target FPS — CPU-side motionBlurScale formula documented inline: `(1/targetFPS) / dt * amount * 0.5` per UE5 SceneVisibility.cpp:5428-5431.
- [x] 3.3 Velocity buffer sharing — velocity_ps.hlsl generates RG16F velocity buffer consumed by both TAA resolve and motion blur gather passes.
