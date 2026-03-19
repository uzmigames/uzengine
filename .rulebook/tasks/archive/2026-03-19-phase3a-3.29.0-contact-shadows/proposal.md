# Contact Shadows

## Priority: P1 — High visual impact, low cost

## Summary

Implement screen-space contact shadows matching UE5's ContactShadows pass. Short-range ray march from each pixel along the light direction through the depth buffer to detect fine shadow detail that cascade shadow maps miss (crevices, small objects, folds in clothing). Adds the "grounded" look — objects feel like they sit in the world.

## UE5 Reference (UE5 ShadowRendering.cpp / DeferredLightPixelShaders.usf)

### Algorithm
1. For each lit pixel, reconstruct view-space position from depth
2. March a short ray (0.2 world units, ~2cm) in view-space along the light direction
3. At each step, project to screen UV and sample depth buffer
4. If ray depth > scene depth (ray is behind something), pixel is in contact shadow
5. Fade shadow at ray end (soft termination, not hard cutoff)

### UE5 Parameters
| Parameter | UE5 Default | Description |
|-----------|-------------|-------------|
| `ContactShadowLength` | 0.0 (disabled) | Ray length in screen fraction (0.01 = 1% screen) |
| `ContactShadowLengthInWS` | false | If true, length is in world units instead of screen |
| `r.ContactShadows.Steps` | 8 | Number of ray march steps |
| `r.ContactShadows.NonShadowCastingIntensity` | 0.5 | Shadow strength on non-shadowcasters |
| Applied per-light | Yes | Each light can have different contact shadow length |

### UE5 Pipeline Position
Integrated into deferred lighting pass. Contact shadow computed per directional/point/spot light, combined with cascade shadow result: `finalShadow = min(cascadeShadow, contactShadow)`.

## Impact

- **Shaders**: ~30 lines added to deferred_lighting_ps.hlsl (or separate include)
- **Performance**: ~0.1ms at 1080p (8 steps, depth-only reads)
- **Visual**: Dramatic — adds fine shadow detail everywhere CSM can't resolve
- **VRAM**: 0 (reads existing depth buffer)

## Acceptance Criteria

1. Small objects cast visible contact shadows on surfaces they sit on
2. Crevices and folds show fine shadow detail not present in CSM
3. Contact shadow fades smoothly at ray end (no hard edge)
4. No false shadows on flat surfaces (depth bias prevents self-shadowing)
5. Demo: objects on a surface with/without contact shadows comparison
