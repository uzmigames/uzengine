# Cascaded Shadow Maps — UE5 Exact Parity

## Priority: P0 — Foundation system with 15+ parameter/calculation divergences from UE5

## Summary

Deep audit revealed 15+ differences between UzEngine's CSM implementation and UE5 5.6. This task brings our shadow system to exact UE5 parity: same split formula, same bias model, same filtering, same fade behavior. Every number must come from UE5 source with file:line reference.

## Current State (UzEngine)

- Nvidia GPU Gems 3 practical split scheme (`lerp(log, linear, 0.75)`)
- Fixed depth bias (0.005 NDC) and normal bias (2cm) — no slope bias, no per-cascade scaling
- 3x3 PCF (9 taps) — hard shadows
- No shadow distance fade — hard cutoff at maxDistance (200m)
- No cascade far extension for blending overlap
- Standard Z depth [0,1] (not reversed-Z)
- Texel snap uses `2/res` grid — UE5 uses `8/res` (4x coarser)
- Bounding sphere center = centroid — UE5 uses optimal diagonal offset
- No per-light bias override — artists can't tune per light
- No far shadow cascades — shadows end at 200m

## Target State (UE5 5.6 Parity)

All values from UE5 source at `W:\UE_5.6_Source`:

### Split Scheme — Exponential Accumulation
**Source**: `DirectionalLightComponent.cpp:753-814`
```
ComputeAccumulatedScale(Exponent=3.0, CascadeIndex, CascadeCount):
  CurrentScale = 1
  Ret = sum of CurrentScale for i in [0, CascadeIndex) where CurrentScale *= Exponent
  TotalScale = sum across [0, CascadeCount)
  return Ret / TotalScale

SplitDistance = ShadowNear + AccumulatedScale * (MaxDistance - ShadowNear)
```

### Shadow Distance
**Source**: `DirectionalLightComponent.cpp:1017-1023`
| Parameter | UE5 Default | Our Current | Fix |
|-----------|-------------|-------------|-----|
| DynamicShadowDistanceMovableLight | 40000 cm (400m) | 20000 cm (200m) | → 40000 cm |
| FarShadowDistance | 300000 cm (3000m) | N/A | Add far cascade support |
| DistanceFieldShadowDistance | 51200 cm (512m) | N/A | Future (needs SDF) |

### Bias Model
**Source**: `ShadowRendering.cpp:30-78`, `ShadowSetup.cpp:1844-1846`
| Parameter | UE5 Default | Our Current | Fix |
|-----------|-------------|-------------|-----|
| CSMDepthBias | 10.0 (normalized by Z range) | 0.005 (raw NDC) | Change to normalized model |
| CSMSlopeScaleDepthBias | 3.0 | **MISSING** | Add slope bias |
| CSMReceiverBias | 0.9 | **MISSING** | Add receiver bias |
| ShadowBias (per-light) | 0.5 | **MISSING** | Add per-light override |
| ShadowSlopeBias (per-light) | 0.5 | **MISSING** | Add per-light override |
| CascadeBiasDistribution | 1.0 | **MISSING** | Lerp(const, worldScaled) per cascade |

### Cascade Blending & Fade
**Source**: `DirectionalLightComponent.cpp:921-966`
| Parameter | UE5 Default | Our Current | Fix |
|-----------|-------------|-------------|-----|
| CascadeTransitionFraction | 0.1 (range ε–0.3) | 0.1 hardcoded shader | Make configurable, extend splitFar on CPU |
| ShadowDistanceFadeoutFraction | 0.1 | **MISSING** | Add fade-to-nothing on last cascade |
| Far plane extension for blend | Yes (CPU side) | No (shader only) | Extend splitFar += fadeExtension |

### Frustum Fitting
**Source**: `DirectionalLightComponent.cpp:816-899`
| Aspect | UE5 | Our Current | Fix |
|--------|-----|-------------|-----|
| Sphere center | Optimal offset: `(diagB²-diagA²)/(2*len) + len*0.5` | Mean of 8 corners | Use optimal offset |
| Radius quantization | `max(radius, 1.0)` (clamp only) | `ceil(radius*16)/16` | Remove quantization, add min clamp |
| Culling volume | Separate tight frustum for culling | Same sphere for everything | Add separate cull volume (future) |

### Texel Snapping
**Source**: `ShadowSetup.cpp:1020-1036`
| Aspect | UE5 | Our Current | Fix |
|--------|-----|-------------|-----|
| Snap grid | `2.0 * MaxDownsampleFactor(4) / res` = `8/res` | `2/res` | Use factor 4 |
| Snap space | World-space (inverse transform back) | NDC-space (direct VP snap) | Change to world-space snap |

### Depth Projection
| Aspect | UE5 | Our Current | Fix |
|--------|-----|-------------|-----|
| Depth direction | Reversed-Z (near=1, far=0) | Standard Z (near=0, far=1) | **Deferred** — requires RHI-wide change |

### Filtering
**Source**: `ShadowRendering.cpp:180-190`
| Aspect | UE5 | Our Current | Fix |
|--------|-----|-------------|-----|
| Filter method | Uniform PCF (configurable kernel) | 3x3 PCF (9 taps) | Upgrade to larger configurable kernel |
| Max kernel size | 40px | 3px | Increase to 5x5 minimum |
| PCSS support | Optional (FilterMethod=1) | No | Add as optional mode |
| Filter sharpening | r.Shadow.FilterSharpen CVar | No | Add sharpen parameter |

## Impact

- **Files modified**: cascade.h, cascade.cpp, shadow_common.hlsli, shadow_pass.cpp, scene_renderer.cpp, constant_buffers.h, mesh_ps.hlsl, deferred_lighting_ps.hlsl
- **New files**: None (all changes to existing files)
- **Performance**: Negligible CPU impact. Larger PCF kernel adds ~0.05ms GPU.
- **Visual**: Dramatically better shadow quality — proper fade, no acne on slopes, smoother transitions, longer shadow distance
- **Risk**: Low — shadow system is self-contained, no architectural changes needed

## Acceptance Criteria

1. Split distances match UE5 exponential accumulation formula exactly
2. Shadow distance = 400m (40000 cm) by default
3. Slope scale depth bias = 3.0, depth bias normalized by Z range
4. Per-cascade bias distribution scales with cascade index
5. Last cascade fades to nothing (10% fade region)
6. Cascade blend extends splitFar on CPU (real overlap, not shader-only)
7. Bounding sphere uses optimal center offset (diagonal formula)
8. Texel snap uses MaxDownsampleFactor=4 (grid = 8/res)
9. PCF kernel upgradeable to 5x5 (25 taps)
10. Cascade transition fraction configurable (default 0.1, range ε–0.3)
11. All parameters exposed in ShadowConfig with UE5 default values
12. Demo: visual comparison before/after showing improved shadow quality
13. No regressions in existing demos
