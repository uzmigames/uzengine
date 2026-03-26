# Material System Expansion (UE5 Parity)

## Priority: P2 — Required for real-world content

## Summary

Expand UzEngine's material system from 5 texture slots to 8+, adding emissive, opacity, and displacement (heightmap). Also add per-material parameter overrides (float/color uniforms artists can tweak per-instance without new textures). Currently missing emissive (no glowing surfaces), opacity (no transparency), and displacement is planned but not wired.

## Current vs UE5

| Slot | UzEngine | UE5 | Gap |
|------|----------|-----|-----|
| Albedo/BaseColor | ✅ t0 | ✅ | — |
| Normal | ✅ t1 | ✅ | — |
| Roughness | ✅ t2 | ✅ | — |
| Metallic | ✅ t3 | ✅ | — |
| AO | ✅ t4 | ✅ | — |
| **Emissive** | ❌ | ✅ t5 | ❌ No glowing |
| **Opacity** | ❌ | ✅ t6 | ❌ No transparency |
| **Displacement/Height** | ❌ (planned POM) | ✅ t7 | ❌ |
| **SubsurfaceColor** | ❌ | ✅ t8 | Phase 4 |
| Per-material floats | ❌ fixed indices only | ✅ Material instances | ❌ |

### UE5 Material Parameters (FMaterialInstanceResource)
| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `EmissiveColor` | float3 | (0,0,0) | Self-illumination color × intensity |
| `OpacityMask` | float | 1.0 | Alpha test threshold (masked) or blend factor |
| `BlendMode` | enum | Opaque | Opaque, Masked, Translucent, Additive |
| `TwoSided` | bool | false | Disable backface culling |
| `EmissiveIntensity` | float | 1.0 | Multiplier for emissive (can exceed 1.0 for bloom) |

## Impact

- **PerObjectCB**: Add emissiveIndex, opacityIndex, displacementIndex, emissiveIntensity, opacityThreshold, blendMode
- **Shaders**: mesh_ps reads 3 new textures, applies emissive to output, alpha test/blend for opacity
- **PSO**: New blend state variants (masked, translucent, additive)
- **Root signature**: May need additional SRV slots (t5-t7) or use bindless

## Acceptance Criteria

1. Emissive material glows and contributes to bloom
2. Masked material clips pixels below opacity threshold (alpha test)
3. Translucent material blends with background (sorted back-to-front)
4. Per-material parameters (emissive intensity, opacity threshold) work from CB
5. Demo: scene with emissive panels, masked foliage, translucent glass
