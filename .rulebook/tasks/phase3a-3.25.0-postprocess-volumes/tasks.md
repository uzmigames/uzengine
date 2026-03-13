## 1. Implementation

- [ ] 1.1 Implement PostProcessSettings struct — contains ALL post-process parameters: ToneMapSettings, BloomSettings, DoFSettings, MotionBlurSettings, TAASettings, GTAOSettings, SSRSettings, AutoExposureSettings, ColorGradingSettings, FilmGrainSettings, VignetteSettings, ChromaticAberrationSettings, LensFlareSettings. Each field has default value matching UE5 defaults.
- [ ] 1.2 Implement PostProcessOverrides struct — per-parameter `bool bOverride_X` flags. Only overridden parameters participate in blending. Unoverridden parameters inherit from lower-priority volume or global defaults.
- [ ] 1.3 Implement VolumeShape enum and intersection — Box (AABB), Sphere (center + radius), Infinite. Compute signed distance from camera to shape boundary. Inside = negative distance, outside = positive.
- [ ] 1.4 Implement CPostProcessVolume ECS component — shape, priority, blendRadius (100cm UE5 default), blendWeight (1.0), enabled flag, PostProcessSettings + PostProcessOverrides.
- [ ] 1.5 Implement PostProcessVolumeGatherer system — each frame: iterate all CPostProcessVolume entities. For each: compute camera distance, compute blend weight = `blendWeight * saturate(1 - max(0, signedDist) / blendRadius)`. Skip if weight ≈ 0. Sort by priority.
- [ ] 1.6 Implement settings blending — for each parameter with override=true in any active volume: accumulate `value * weight`. After all volumes: normalize by total weight per parameter. Parameters with no override use global default.
- [ ] 1.7 Implement infinite volume support — infinite volume: weight = blendWeight regardless of camera position. Used as level-wide default. Typically priority=0 (lowest).
- [ ] 1.8 Implement blended settings output — produce single `PostProcessSettings` struct consumed by all post-process passes each frame. This replaces hardcoded per-effect settings.
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
