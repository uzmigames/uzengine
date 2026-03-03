## 1. Velocity Buffer and CPreviousTransform Component

- [ ] 1.1 Define `CPreviousTransform` struct in `src/ecs/components/CPreviousTransform.h` with fields: `world_matrix` (float4x4), `view_proj_matrix` (float4x4) — previous frame values copied from `CTransform` and the camera's view-projection
- [ ] 1.2 Auto-add `CPreviousTransform` to every entity that has a `CTransform` component in `TransformSystem::on_component_added()`; initialize both matrices to the entity's current-frame values on first-add to avoid a one-frame velocity spike
- [ ] 1.3 In `TransformSystem::update()`, at the end of the tick (after all transforms are final), copy current `CTransform::world_matrix` into `CPreviousTransform::world_matrix` and the camera's current view-projection into `CPreviousTransform::view_proj_matrix`
- [ ] 1.4 Allocate the velocity buffer render target (RG16F, same resolution as the GBuffer) in `src/renderer/passes/MotionBlurPass.cpp`; bind as a UAV during the GBuffer pass and as SRV during the motion blur pass

## 2. GBuffer Velocity Output

- [ ] 2.1 In the GBuffer vertex shader, pass both `current_clip_pos` (using current frame's MVP) and `prev_clip_pos` (using `CPreviousTransform::world_matrix` * previous view-projection) as interpolated vertex outputs
- [ ] 2.2 In the GBuffer pixel shader, compute `velocity = (current_clip_pos.xy / current_clip_pos.w) - (prev_clip_pos.xy / prev_clip_pos.w)` in NDC space, divide by `frame_delta_time_seconds`, and write to the RG16F velocity render target
- [ ] 2.3 Bind `CPreviousTransform::world_matrix` as an additional per-draw constant in the GBuffer pass constant buffer (alongside the current MVP); update `ClusteredLightPass.cpp` draw-call submission to include this value

## 3. Velocity Resolve Pass

- [ ] 3.1 Create `shaders/post/MotionBlurVelocityResolve.hlsl`: full-screen compute pass that combines the per-object velocity buffer with the camera velocity derived from the difference between the current and previous frame view-projection matrices
- [ ] 3.2 Compute camera velocity contribution per pixel: unproject the pixel to world space using the inverse current view-projection, reproject using the previous view-projection, compute NDC delta; this represents camera motion for static world geometry
- [ ] 3.3 For each pixel, use `max(length(object_velocity), length(camera_velocity))` direction — take the velocity with greater magnitude to prioritize object-blur over camera-blur at edges, then composite both contributions
- [ ] 3.4 Output unified velocity to a second full-screen RG16F texture used by the blur pass

## 4. Motion Blur Accumulation Pass

- [ ] 4.1 Create `shaders/post/MotionBlurPass.hlsl`: for each pixel, read the unified velocity vector, clamp its length to `r_motion_blur_max_radius` pixels, compute the sample step as `velocity_vector / N` where `N` is `r_motion_blur_sample_count` (default 8)
- [ ] 4.2 Accumulate `N` jittered HDR color samples along the velocity direction; jitter offsets use a Halton(2, 3) sequence to avoid aliasing at sample boundaries
- [ ] 4.3 Implement depth-aware weighting: for each sample, read its linear depth from the depth buffer; compute a soft weight `w = exp(-abs(sample_depth - center_depth) / depth_soft_threshold)` where `depth_soft_threshold = 0.05 * z_range`; multiply each color sample by its weight before accumulation
- [ ] 4.4 Handle the camera rotation blur contribution as a separate full-screen pass using the camera-only velocity component, applied after object blur and modulated by scene depth (near objects receive less camera blur than far objects)

## 5. CMotionBlurSettings Scene Component

- [ ] 5.1 Define `CMotionBlurSettings` ECS component in `src/ecs/components/CMotionBlurSettings.h` (attached to the scene root or a dedicated settings entity): `max_radius_px` (float, default 32), `sample_count` (int, default 8), `camera_blur_scale` (float, 0–2, default 1), `object_blur_scale` (float, 0–2, default 1)
- [ ] 5.2 Register `CMotionBlurSettings` in the component registry with serialization to `.uzscene` format
- [ ] 5.3 In `MotionBlurPass::setup()`, read `CMotionBlurSettings` fields and upload them to the motion blur cbuffer; if no `CMotionBlurSettings` entity exists in the scene, use hardcoded defaults

## 6. Console Variables

- [ ] 6.1 Register `r_motion_blur_scale` console variable (float, 0.0–2.0, default 1.0) in the console command system; when set to 0.0, skip both the velocity resolve and blur passes entirely (bind a null UAV and no draw calls)
- [ ] 6.2 Register `r_motion_blur_max_radius` console variable (int, pixels, default 32) and `r_motion_blur_samples` (int, default 8)
- [ ] 6.3 Expose `r_motion_blur_scale` as a scene settings slider in the Scene Settings Inspector panel under "Post Processing → Motion Blur"

## 7. Testing

- [ ] 7.1 Write `tests/renderer/test_motion_blur.cpp`: test `CPreviousTransform` auto-add — add `CTransform` to an entity; verify `CPreviousTransform` is also present with matching initial matrices
- [ ] 7.2 Test that `TransformSystem::update()` copies current transform to `CPreviousTransform` at end-of-tick; move an entity by (1, 0, 0), advance one tick, verify `CPreviousTransform::world_matrix` reflects the previous position
- [ ] 7.3 Test velocity computation: static camera, object moves from (0,0,0) to (1,0,0) in one frame at delta 1/60 s; verify GBuffer velocity pixel at the object's screen position is approximately (60 NDC-units/s in X, 0)
- [ ] 7.4 Test `max_radius` clamping: pass a velocity vector of magnitude 200 pixels to `MotionBlurPass` with `max_radius = 32`; verify the output sample stride is clamped to ≤32 pixels
- [ ] 7.5 Test depth-aware weight: sample at matching depth (weight ≈ 1.0) vs sample at very different depth (weight ≈ 0.0); verify suppression works within floating-point tolerance
- [ ] 7.6 Test `r_motion_blur_scale = 0.0` skips passes: verify that neither `MotionBlurVelocityResolve.hlsl` nor `MotionBlurPass.hlsl` dispatches are submitted to the command list when scale is 0
- [ ] 7.7 Test `CMotionBlurSettings` serialization round-trip for all four fields
- [ ] 7.8 Test that `CPreviousTransform` is initialized correctly on first-add (no one-frame velocity spike): verify velocity GBuffer output is zero on the first frame an entity is added to the scene
- [ ] 7.9 Verify ≥95% line and branch coverage on `MotionBlurPass.cpp`, `TransformSystem.cpp` (previous-transform copy path), and the depth-weighting logic

## 8. Documentation

- [ ] 8.1 Update `docs/specs/renderer.md`: document the velocity buffer RT format, GBuffer pass velocity output, and the two-pass motion blur pipeline order in the render graph
- [ ] 8.2 Update `docs/specs/screen-space-effects.md`: document `CMotionBlurSettings` fields, depth-aware sample weighting algorithm, and camera vs. object blur separation
- [ ] 8.3 Update `docs/specs/console-commands.md`: document `r_motion_blur_scale`, `r_motion_blur_max_radius`, `r_motion_blur_samples`
- [ ] 8.4 Update `docs/specs/camera-system.md`: document `CPreviousTransform` component lifecycle and how its `view_proj_matrix` field is updated by `TransformSystem`
