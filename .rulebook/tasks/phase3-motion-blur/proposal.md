# Proposal: phase3-motion-blur

## Why
At typical game frame rates (30-60 fps), fast-moving objects such as cars, swinging weapons, spinning propellers, and rotating camera views produce visible strobing because each frame captures a perfectly sharp snapshot that the human eye would never see. Motion blur simulates the finite exposure time of a real camera or eye, eliminating strobing and conveying a sense of speed. Per-object motion blur (separate from camera blur) is required for cinematic quality because camera-only blur leaves fast world objects unnaturally sharp. This is a standard post-process in every cinematic-grade renderer.

## What Changes
- Add a velocity buffer render target (RG16F): during the GBuffer pass, each object's pixel shader outputs `current_clip_pos - prev_clip_pos` (in NDC) divided by frame delta time, using the object's previous-frame transform stored in `CPreviousTransform` ECS component updated each tick
- `MotionBlurVelocityResolve.hlsl`: combine per-object velocity buffer with camera velocity (from prev/curr view-projection matrix difference) to produce a unified full-screen velocity texture
- `MotionBlurPass.hlsl`: for each pixel, sample the velocity vector, clamp magnitude to `r_motion_blur_max_radius` (default 32 pixels), and accumulate `N` (default 8) jittered samples along the velocity vector direction; weight by depth similarity to avoid background bleed
- Depth-aware sampling: each sample compares its depth against the center pixel's depth using a soft threshold — samples from foreground occluding objects are suppressed when they would bleed onto a distant background
- Camera rotation blur handled separately from per-object: camera velocity is derived purely from the view matrix delta and applied as a full-screen blur on top of the per-object result, respecting scene depth to avoid blurring nearby objects
- `r_motion_blur_scale` console variable (0.0-2.0, default 1.0) controls overall blur intensity; setting to 0 disables the pass entirely at zero GPU cost
- `CMotionBlurSettings` scene-level component: `max_radius_px`, `sample_count`, `camera_blur_scale`, `object_blur_scale` — all exposed in the Scene Settings Inspector panel
- `CPreviousTransform` component added to every entity that has a `CTransform`, auto-updated by the transform system each frame

## Impact
- Affected specs: `renderer.md`, `screen-space-effects.md`, `shader-library.md`, `camera-system.md`, `console-commands.md`
- Affected code: `src/renderer/passes/MotionBlurPass.cpp`, `shaders/post/MotionBlurPass.hlsl`, `shaders/post/MotionBlurVelocityResolve.hlsl`, `src/ecs/components/CPreviousTransform.h`, `src/ecs/systems/TransformSystem.cpp`
- Breaking change: NO
- User benefit: Fast-moving objects, weapons, and camera rotations render with natural cinematic motion blur, eliminating strobing and dramatically improving the perceived quality of action sequences and cutscenes.
