# Far Shadow Cascades — UE5 Parity

## Priority: P2 — Extends shadow coverage from 400m to 3km

## Summary

UE5 supports a separate tier of "far shadow cascades" that extend shadow coverage beyond the main CSM distance (400m) out to 3000m (3km). These use the same shadow atlas, same filtering, and same projection math as main cascades but cover a much larger range with lower effective resolution. The primary optimization is LOD bias — far cascades render coarser mesh LODs to reduce geometry cost.

## UE5 Reference

### Parameters
**Source**: `DirectionalLightComponent.cpp:1023`, `DirectionalLightComponent.h:112`

| Parameter | Default | Range | Source |
|-----------|---------|-------|--------|
| `FarShadowDistance` | 300000 cm (3km) | 0–∞ | `DLC.cpp:1023` |
| `FarShadowCascadeCount` | 0 (disabled) | 0–10 | `DLC.h:112`, clamped at `DLC.cpp:1076` |
| `GFarShadowStaticMeshLODBias` | 0 | 0–∞ | `DLC.cpp:68` |
| `r.Shadow.FarShadowDistanceOverride` | -1 (no override) | -1–∞ | CVar |

### Architecture
- **Range**: Far cascades cover `[MainCSMDistance, FarShadowDistance]` — completely separate range from main cascades
- **Split distribution**: Same exponential `ComputeAccumulatedScale()` formula, applied to the far range
- **Atlas layout**: Sequential after main cascades — `[Main0..MainN][Far0..FarM]` in same atlas
- **Index**: Far cascade 0 = `cascadeIndex = mainCascadeCount`, Far cascade 1 = `mainCascadeCount + 1`, etc.
- **Flag**: `CascadeSettings.bFarShadowCascade = true` controls LOD selection in rendering
- **Activation**: Only if `FarShadowCascadeCount > 0` AND `(FarShadowDistance - MainShadowDistance) > 100cm`

### LOD Bias
**Source**: `ShadowSetup.cpp:1798-1814`, `ShadowDepthRendering.cpp:1197`
- Far cascades apply `GFarShadowStaticMeshLODBias` to static meshes — renders lower-detail LODs
- Reduces geometry cost by 50-80% (typical LOD reduction)
- Only affects static meshes, not skeletal/dynamic

### Performance
- Near-zero additional CPU cost (same pipeline as main cascades)
- GPU cost: ~0.5-1.0ms per far cascade (lower geometry from LOD bias)
- Memory: Shared atlas — requires expanding atlas or reducing per-cascade resolution

## Impact

- **Files modified**: cascade.h, cascade.cpp, shadow_pass.h, shadow_pass.cpp, scene_renderer.cpp, constant_buffers.h, shadow_common.hlsli
- **Atlas change**: Atlas must accommodate `mainCascades + farCascades` tiles. May need to increase atlas size or use variable-resolution tiles.
- **Performance**: +0.5-2.0ms GPU for 2-4 far cascades
- **Visual**: Shadows visible on distant terrain, mountains, large structures out to 3km
- **Dependency**: Benefits from phase0-0.1.0-csm-ue5-parity (same split formula)

## Acceptance Criteria

1. Far shadow cascades render beyond main CSM distance
2. Split distances use same exponential formula over `[mainDistance, farDistance]` range
3. Far cascades share same shadow atlas with main cascades
4. LOD bias reduces geometry for far cascades
5. `bFarShadowCascade` flag controls rendering behavior
6. `FarShadowCascadeCount=0` completely disables far shadows (no perf cost)
7. Demo: terrain/large scene with shadows visible at 1km+
8. No regression in main cascade shadow quality
