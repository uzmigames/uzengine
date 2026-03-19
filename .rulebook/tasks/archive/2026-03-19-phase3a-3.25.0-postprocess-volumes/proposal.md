# Post-Process Volumes

## Summary

Implement PostProcessVolume system matching UE5's PostProcessVolume actor. Invisible volumes (box, sphere, or infinite) that modify post-process settings when the camera is inside them. Multiple volumes blend by priority and distance, enabling per-region visual customization (dark dungeon, bright outdoors, underwater blue tint).

## UE5 Reference Implementation

### Architecture (UE5 PostProcessVolume.h / SceneView.cpp)
- **PostProcessVolume**: Actor with shape (box/sphere/infinite), priority, blend radius, and full PostProcessSettings.
- **Infinite volume**: Affects entire level. Typically one per level as "default" settings. Priority=0.
- **Bounded volume**: Box or sphere shape. Camera inside = full weight. Camera outside = distance-based fade over `BlendRadius`.
- **Priority stacking**: Higher priority volumes override lower. Multiple overlapping volumes blend additively by weight.
- **Per-frame gather**: Each frame, iterate all active volumes. For each: compute camera distance, compute blend weight, accumulate weighted settings.
- **Blend formula**: `finalSettings = Σ(volume.settings * volume.weight) / Σ(volume.weight)` for each parameter independently.

### UE5 Parameters (APostProcessVolume)
| Parameter | UE5 Default | Description |
|-----------|-------------|-------------|
| `bEnabled` | true | Volume active |
| `bUnbound` | false | True = infinite (affects whole level) |
| `BlendRadius` | 100.0 | Distance over which volume fades in (cm) |
| `BlendWeight` | 1.0 | Max influence weight (0-1) |
| `Priority` | 0.0 | Higher overrides lower |
| `PostProcessSettings` | {} | ALL post-process parameters (exposure, bloom, DoF, color grading, etc.) |
| Override flags | per-param | Each parameter has `bOverride_X` bool — only overridden params are blended |

### UE5 Override System
- Each PostProcessSettings parameter has a corresponding `bOverride_ParameterName` bool.
- Only parameters with override=true participate in blending. Unoverridden parameters are inherited from lower-priority volumes or global defaults.
- This lets a volume say "only change color grading" without affecting DoF, bloom, etc.

## UzEngine Implementation Plan

### Data Structure
```cpp
struct PostProcessVolume {
    AABB     bounds;            // World-space box (or sphere radius)
    VolumeShape shape;          // Box, Sphere, Infinite
    float    priority;          // Higher = stronger override
    float    blendRadius;       // Fade distance outside volume (UE5: 100cm)
    float    blendWeight;       // Max weight (0-1)
    bool     enabled;

    PostProcessSettings settings;      // All PP parameters
    PostProcessOverrides overrides;    // Per-param override flags
};
```

### Blend Algorithm
1. Sort active volumes by priority (ascending)
2. For each volume: compute camera-to-volume distance
3. If camera inside: weight = blendWeight
4. If outside and within blendRadius: weight = blendWeight * (1 - dist/blendRadius)
5. If outside blendRadius: weight = 0 (skip)
6. For each overridden parameter: accumulate weighted value
7. Normalize by total weight per parameter

## Impact

- **C++ code**: PostProcessVolume component, volume gatherer system, settings blender
- **ECS**: New CPostProcessVolume component
- **Scene**: Volumes are scene nodes with transform + shape
- **Performance**: O(N) per frame where N = active volumes (typically 3-10)
- **Dependencies**: All post-process effects must read blended settings

## Breaking Changes

None — volumes are additive. Without any volumes, global default settings are used.

## Acceptance Criteria

1. Infinite volume sets global defaults for all effects
2. Box volume overrides settings when camera is inside
3. BlendRadius produces smooth transition when entering/exiting volume
4. Higher priority volume overrides lower priority
5. Override flags work — volume can change only color grading without affecting DoF
6. Demo: room with volume (warm+bloom) → corridor → outdoor volume (cool+SSAO), smooth transitions
