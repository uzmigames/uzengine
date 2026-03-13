# Multiple Shading Models (UE5 Parity)

## Priority: P3 — Required for diverse content (skin, cloth, foliage, hair)

## Summary

Add multiple BRDF shading models beyond Default Lit, matching UE5's ShadingModelID system. Each material selects a shading model that changes how lighting is evaluated. Critical models: Subsurface (skin/wax), Cloth (velvet/fabric), Foliage (two-sided translucent), Clear Coat (car paint/lacquer), Unlit (UI/emissive only).

## UE5 Reference (ShadingModels.ush — 14 models)

| ID | Model | UE5 Name | Use Case |
|----|-------|----------|----------|
| 0 | Unlit | MSM_Unlit | UI, emissive-only surfaces |
| 1 | Default Lit | MSM_DefaultLit | Standard PBR (what UzEngine has now) |
| 2 | Subsurface | MSM_Subsurface | Wax, jade, thick skin |
| 3 | Preintegrated Skin | MSM_PreintegratedSkin | Realistic skin (SSS profile) |
| 4 | Clear Coat | MSM_ClearCoat | Car paint, lacquered wood, wet surfaces |
| 5 | Subsurface Profile | MSM_SubsurfaceProfile | Per-material SSS profile |
| 6 | Two-Sided Foliage | MSM_TwoSidedFoliage | Leaves, thin cloth (light passes through) |
| 7 | Hair | MSM_Hair | Marschner hair model |
| 8 | Cloth | MSM_Cloth | Fabric, velvet (Ashikhmin sheen) |
| 9 | Eye | MSM_Eye | Realistic eye rendering |

### Priority for UzEngine (Phase 1)
1. **Unlit** — trivial, just skip lighting
2. **Subsurface** — wrap lighting + subsurface color
3. **Clear Coat** — dual-layer specular (base + coat)
4. **Cloth** — Ashikhmin sheen BRDF for fabric
5. **Two-Sided Foliage** — transmittance through thin surfaces

### How UE5 Stores Shading Model
- G-Buffer: 4 bits of `ShadingModelID` packed into unused channel
- Deferred lighting: switch on `ShadingModelID` per pixel → different BRDF evaluation
- No per-pixel branching penalty (GPU coherency: usually tiles share same model)

## Impact

- **G-Buffer**: Pack shadingModelID (4 bits) into unused alpha or reserved channel
- **Deferred lighting**: Branch per shading model (5 models = 5 BRDF paths)
- **Forward pass**: Same model selection
- **Material system**: New `shadingModel` parameter per material
- **Textures**: Subsurface needs subsurface color map, clear coat needs coat roughness map

## Acceptance Criteria

1. Unlit material shows emissive color only, no lighting
2. Subsurface skin shows soft diffuse wrap and colored SSS
3. Clear coat shows two-layer specular (glossy top coat + rough base)
4. Cloth shows soft sheen at grazing angles (velvet look)
5. Foliage shows back-lit translucency when light is behind leaf
6. Demo: material ball rendered with each shading model side-by-side
