## 1. Implementation

- [x] 1.1 Implement PostProcessSettings struct — 24 float/int fields covering Bloom, AutoExposure, DoF, MotionBlur, ColorGrading, Vignette, SSAO, SSR. All defaults cited from UE5 Scene.cpp. Override bitmask (PostProcessOverride enum, 28 bits) replaces per-bool overrides. Lerp() method blends floats linearly, snaps ints at alpha>0.5, OR's override flags. File: runtime/engine/renderer/postprocess/post_process_volume.h/.cpp.
- [x] 1.2 Implement PostProcessOverrides struct — implemented as uint32_t bitmask with PostProcessOverride enum (28 flags). SetOverride/ClearOverride/HasOverride/SetAllOverrides/ClearAllOverrides helpers. OR-merge during blending (once overridden, stays overridden). Part of PostProcessSettings struct.
- [ ] 1.3 Implement VolumeShape enum and intersection — Box (AABB), Sphere (center + radius), Infinite. Compute signed distance from camera to shape boundary. Inside = negative distance, outside = positive.
- [x] 1.4 Implement CPostProcessVolume ECS component — implemented as PostProcessVolume struct with AABB bounds, priority, blendRadius (100cm default), blendWeight (1.0), enabled flag, isUnbound, PostProcessSettings. File: post_process_volume.h.
- [x] 1.5 Implement PostProcessVolumeGatherer system — implemented as PostProcessVolumeManager::ComputeBlendedSettings(). Sort by priority ascending, compute unsigned AABB distance, blend factor = saturate(1-dist/blendRadius)*blendWeight, lerp into accumulated result. File: post_process_volume.cpp.
- [ ] 1.6 Implement settings blending — for each parameter with override=true in any active volume: accumulate `value * weight`. After all volumes: normalize by total weight per parameter. Parameters with no override use global default.
- [x] 1.7 Implement infinite volume support — PostProcessVolume::isUnbound=true skips AABB distance check, blend factor = blendWeight directly. Used as global base layer (priority=0).
- [x] 1.8 Implement blended settings output — ComputeBlendedSettings() returns single PostProcessSettings struct. Starts from engine defaults, accumulates all active volumes by priority order.
- [ ] 1.9 Wire all existing post-process passes to read from blended settings — ToneMap, Bloom, FXAA (and future TAA/GTAO/SSR/DoF/etc.) read parameters from the blended PostProcessSettings instead of hardcoded values.
- [ ] 1.10 Implement volume debug visualization — render semi-transparent wireframe boxes/spheres for each active volume. Color by priority (low=blue, high=red). Show current blend weight.
- [ ] 1.11 Create PostProcessVolume demo — 3 rooms connected by corridors. Room 1: warm + high bloom (cozy interior). Room 2: cool + desaturated (sci-fi). Room 3: outdoor with default settings. Smooth transitions as camera moves between rooms.

## 2. Visual Verification

- [ ] 2.1 Screenshot: inside warm volume — visible warm tint + bloom
- [ ] 2.2 Screenshot: transitioning between volumes — smooth blend visible
- [ ] 2.3 Screenshot: outside all volumes — global defaults applied
- [ ] 2.4 Screenshot: overlapping volumes — higher priority wins
- [ ] 2.5 Screenshot: volume debug wireframes visible
- [ ] 2.6 Regression: existing demos still work

## 3. Documentation

- [ ] 3.1 Document PostProcessVolume system with UE5 equivalent reference
- [ ] 3.2 Add volume placement and blending guide for artists
- [ ] 3.3 Document per-parameter override system
- [ ] 3.4 Add volume priority and blending formula reference
