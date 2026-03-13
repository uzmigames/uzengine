# Point & Spot Light Shadows

## Priority: P1 — Required for believable indoor lighting

## Summary

Add shadow mapping for point lights (omnidirectional) and spot lights (perspective). Currently only the directional light casts shadows — point/spot lights illuminate through walls and objects. UE5 uses cubemap shadows for point lights (or dual-paraboloid for performance) and perspective shadow maps for spot lights.

## UE5 Reference

### Point Light Shadows
- **Method**: Cubemap shadow map (6 faces) or dual-paraboloid (2 hemispheres, cheaper)
- **Resolution**: 512×512 per face (UE5 default, configurable 256-2048)
- **Total VRAM**: 6×512² = 6MB per shadowed point light (D16_UNORM)
- **PCF**: 4-tap Poisson disk per face
- **Cubemap addressing**: Light-to-pixel direction → select face → project to face UV → sample depth
- **Max shadowed point lights**: 4-8 per frame (GPU budget limited)

### Spot Light Shadows
- **Method**: Single perspective shadow map (one frustum)
- **Resolution**: 1024×1024 default
- **VRAM**: 1MB per shadowed spot light
- **PCF**: Same as directional (Poisson or 3×3 grid)
- **Projection**: Perspective matrix from spot position, cone angle = outer angle × 2

### UE5 Shadow Atlas
- All local light shadows packed into a single large atlas (4096×4096 or 8192×8192)
- Each light claims a tile based on resolution quality
- Shadow importance: larger/closer lights get more resolution
- Cached shadows: static lights only re-render when objects move

### UE5 Parameters
| Parameter | UE5 Default | Description |
|-----------|-------------|-------------|
| `bCastShadows` | false | Per-light shadow toggle |
| `ShadowResolutionScale` | 1.0 | Resolution multiplier |
| `ShadowBias` | 0.5 | Depth bias |
| `ShadowSlopeBias` | 0.5 | Slope-scaled bias |
| `ContactShadowLength` | 0.0 | Screen-space contact shadow |
| `ShadowSharpen` | 0.0 | PCF filter sharpening |

## Impact

- **Shaders**: Shadow depth VS for local lights, cubemap/perspective shadow sampling
- **Render graph**: Per-light shadow passes (up to 6 faces for point, 1 for spot)
- **VRAM**: 6-48MB for shadow atlas (depends on shadowed light count)
- **Performance**: 0.5-2ms per shadowed light (geometry re-render per face)
- **Dependencies**: Light system, existing shadow pass infrastructure

## Acceptance Criteria

1. Point light in a room casts shadows from all directions (objects block light)
2. Spot light projects cone shadow (objects in cone path are shadowed)
3. Shadow quality comparable to directional CSM (no major aliasing)
4. Max 4 shadowed point lights at 60fps (performance budget)
5. Demo: room with point light, objects casting shadows on walls
