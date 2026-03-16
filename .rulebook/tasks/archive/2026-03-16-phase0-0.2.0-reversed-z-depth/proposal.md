# Reversed-Z Depth Buffer — UE5 Parity

## Priority: P1 — RHI-wide architectural change, prerequisite for precision parity

## Summary

UE5 uses reversed-Z depth (near=1.0, far=0.0) globally across the entire rendering pipeline. UzEngine currently uses standard Z (near=0.0, far=1.0). Reversed-Z provides 100x better depth precision near the camera, eliminating Z-fighting artifacts on characters, weapons, and close-up geometry. This is a compile-time, RHI-wide change that affects every pass, every shader, and every depth comparison.

## UE5 Reference

### Configuration (compile-time, not runtime)
**Source**: `RHIDefinitions.h:278-287`
```cpp
enum class ERHIZBuffer {
    FarPlane  = 0,    // depth 0.0 = farthest
    NearPlane = 1,    // depth 1.0 = nearest
    IsInverted = 1    // compile-time flag: reversed-Z is always on
};
```

### Depth Clear Values
**Source**: `RHI.cpp:131-132`
| Value | Standard Z | Reversed-Z (UE5) |
|-------|-----------|-------------------|
| Clear depth (far) | 1.0 | **0.0** |
| Near depth | 0.0 | **1.0** |

### Depth Comparison
**Source**: `RHIDefinitions.h`
| Operation | Standard Z | Reversed-Z (UE5) |
|-----------|-----------|-------------------|
| "Closer" test | LessEqual | **GreaterEqual** |
| "Farther" test | GreaterEqual | **LessEqual** |
| Hi-Z downsample | max() | **min()** |

### Projection Matrix
**Source**: `SceneView.cpp`, `PerspectiveMatrix.h`
- Standard: `A = far/(far-near)`, `B = -near*far/(far-near)`
- Reversed: `A = near/(near-far)`, `B = -far*near/(near-far)`
- Ortho: Z scale negated, offset = `1.0 - offset*scale`

### Precision Improvement
| Distance | Standard Z precision | Reversed-Z precision | Improvement |
|----------|---------------------|---------------------|-------------|
| 1 cm | 0.0001 units | 0.000001 units | **100x** |
| 100 cm | 0.001 units | 0.00001 units | **100x** |
| 10000 cm | 0.1 units | 0.001 units | **100x** |
| Far plane | 0.000001 units | 0.000001 units | Same |

### Shadow Depth
**Source**: `ShadowSetup.cpp:3165`
- Shadow projection also uses `FReversedZPerspectiveMatrix`
- Shadow comparison uses `CF_DepthNearOrEqual` → resolves to `GreaterEqual`

## Scope of Changes

This is an **RHI-wide** change. Every system that touches depth is affected:

### C++ (RHI / Renderer)
1. **Projection matrices** — `Mat4::Perspective()`, `Mat4::Orthographic()` must output reversed-Z
2. **Depth clear** — All `ClearDepthStencilView()` calls change from 1.0 to 0.0
3. **Depth comparison** — All `D3D12_COMPARISON_FUNC_LESS_EQUAL` → `GREATER_EQUAL` in PSO states
4. **Shadow projection** — Cascade ortho projection reversed
5. **Hi-Z pyramid** — Downsample uses min() instead of max()
6. **Depth linearization** — CPU-side depth readback formulas inverted

### Shaders (HLSL)
1. **All depth comparisons** — `depth < sceneDepth` → `depth > sceneDepth`
2. **Shadow sampling** — `SampleCmpLevelZero` comparison direction flipped
3. **SSAO** — Depth sampling and comparison reversed
4. **SSR** — Ray march depth test reversed
5. **Depth-of-field** — Depth-based blur circle calculation
6. **Atmospheric scattering** — Depth-based distance calculation
7. **Global define** — `#define UZ_REVERSED_Z 1` for all shaders

### Render Passes Affected
- GBuffer pass (depth write)
- Shadow depth pass (depth write + comparison)
- Depth prepass (depth write)
- Hi-Z pyramid (downsample direction)
- SSAO (depth read)
- SSR (depth read + ray march)
- Deferred lighting (depth read)
- Forward pass (depth read/write)
- Sky pass (depth test)
- Post-process (depth read)
- Debug visualization (depth read)

## Impact

- **Files modified**: ~20-30 files (all depth-touching passes + shaders)
- **Risk**: HIGH — one missed comparison = broken rendering. Requires systematic audit.
- **Testing**: Every pass must be visually verified after change
- **Performance**: Zero cost (same GPU operations, different values)
- **Visual**: Eliminates Z-fighting on close geometry permanently

## Acceptance Criteria

1. All depth clears use 0.0 (reversed far plane)
2. All depth comparisons use GreaterEqual (reversed near test)
3. Projection matrices output reversed-Z values
4. Shadow depth rendering and sampling work correctly with reversed-Z
5. Hi-Z pyramid downsamples with min() operator
6. `#define UZ_REVERSED_Z 1` available in all shaders
7. No Z-fighting visible on close-up geometry that was previously fighting
8. All existing demos render identically (no visual regression except precision improvement)
9. Depth readback utilities updated for reversed-Z decode
