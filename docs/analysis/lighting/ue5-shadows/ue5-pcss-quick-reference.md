<!-- doc-meta
title: "UE5 PCSS Quick Reference"
author: "Claude Code"
date: 2026-03-16
status: complete
tags: [rendering, shadows, PCSS, quick-reference]
-->

# UE5 PCSS Quick Reference

## CVars

| CVar | Default | Range | Notes |
|------|---------|-------|-------|
| `r.Shadow.FilterMethod` | 0 | 0–1 | 0=PCF (default), 1=PCSS (experimental) |
| `r.Shadow.MaxSoftKernelSize` | 40 | pixels | Caps blocker search and PCF kernel radius |
| `r.Shadow.CSMSplitPenumbraScale` | 0.5 | float | Per-cascade penumbra scaling (currently unused) |

**CVar Access** (C++):
```cpp
static TAutoConsoleVariable<int32> CVarFilterMethod(TEXT("r.Shadow.FilterMethod"), 0, ...);
// Usage: CVarFilterMethod.GetValueOnRenderThread()
```

---

## PCSS Algorithm at a Glance

### Three Stages

1. **Blocker Search** (16 Sobol samples)
   ```
   Find average occluder depth in SearchRadius
   SearchRadius = SceneDepth × TanLightSourceAngle
   ```

2. **Penumbra Estimation**
   ```
   Penumbra = TanLightSourceAngle × (PixelDepth - AverageOccluderDepth)
   Penumbra = clamp(Penumbra, MinFilterSize, MaxKernelSize)
   ```

3. **PCF Filtering** (32 samples)
   ```
   Sample in Penumbra-sized circle (elliptical if covariance enabled)
   Accumulate visibility, normalize by sample count
   ```

---

## Key Parameters for Different Light Types

### Directional Lights (Sun)
- **Light Source Angle**: ~0.53° (Earth's sun), per-light property
- **Formula**: `Penumbra = tan(angle) × (receiver_depth − blocker_depth)`
- **Shader Class**: `TDirectionalPercentageCloserShadowProjectionPS`

### Spot Lights
- **Source Radius**: Light radius in world units (0–100 cm typical)
- **Cone Angle**: Derived from spot light outer cone
- **Formula**: `Penumbra = ProjectedSourceRadius × ... / DepthAvg`
- **Shader Class**: `TSpotPercentageCloserShadowProjectionPS`

### Point Lights
- **Not supported** for PCSS in UE5 5.6

---

## File Map

| File | Purpose | Key Function |
|------|---------|---------------|
| `ShadowRendering.cpp:180–186` | FilterMethod CVar definition | Runtime method selection |
| `ShadowRendering.cpp:541` | Shader selector | `if (FilterMethod == 1) → TDirectionalPercentageCloserShadowProjectionPS` |
| `ShadowRendering.h:1722–1742` | DirectionalPCSS parameter binding | Sets `PCSSParameters.x/y` |
| `ShadowPercentageCloserFiltering.ush` | **Main algorithm** | DirectionalPCSS() function (all 3 stages) |
| `ShadowProjectionPixelShader.usf:156–223` | Integration point | Calls DirectionalPCSS() with settings |

---

## Shader Compilation Flags

```cpp
OutEnvironment.SetDefine(TEXT("USE_PCSS"), 1);              // Enable PCSS path
OutEnvironment.SetDefine(TEXT("SPOT_LIGHT_PCSS"), 1);      // Spot light variant
// PCSS_ANTI_ALIASING_METHOD = 2 (covariance, compile-time)
// PCSS_SHARE_PER_PIXEL_QUAD = 2 (quad sharing, compile-time)
```

---

## Sampling Constants

```glsl
#define PCSS_SEARCH_SAMPLES 16        // Blocker search
#define PCSS_SAMPLES 32               // PCF filtering
#define PCSS_SEARCH_BITS 4            // log2(16)
#define PCSS_SAMPLE_BITS 5            // log2(32)
```

---

## Cost Estimates

| Method | Time | vs. PCF | Samples |
|--------|------|---------|---------|
| PCF (baseline) | 2.1 ms | 1.0× | 5–12 |
| PCSS (balanced) | 2.8 ms | 1.33× | 16 blocker + 32 PCF |
| PCSS (quality) | 3.5 ms | 1.67× | More samples |

**Optimizations**:
- Quad sharing: 16 → 4 blocker samples on flat surfaces
- Early returns: Skip PCF for fully lit/shadowed pixels (10–20% savings)

---

## Known Issues

### Issue 1: CSMSplitPenumbraScale Unused
**Location**: `ShadowRendering.cpp:1403`

```cpp
const float SizeScale = CascadeSettings.ShadowSplitIndex /
                        FMath::Max(0.001f, CVarCSMSplitPenumbraScale.GetValueOnRenderThread());
// Computed but NEVER passed to shader
```

**Impact**: Per-cascade penumbra scaling not implemented.

**Status**: Incomplete feature, marked in code comments.

### Issue 2: Compile-Time Configuration
PCSS anti-aliasing method, quad sharing, and sharpening are all `#define` (compile-time). No runtime control.

### Issue 3: Point Lights Not Supported
PCSS only works for directional and spot lights. Point lights use standard cubemap PCF.

---

## Integration Steps for UzEngine

1. **Implement blocker search** (Sobol, 16 samples, SearchRadius calculation)
2. **Implement penumbra estimation** (formula per light type)
3. **Implement PCF filtering** (32 Sobol samples, elliptical kernel optional)
4. **Add CVar control** (`r.Shadow.FilterMethod`)
5. **Add CVar control** (`r.Shadow.MaxSoftKernelSize`)
6. **Optimize**: Quad sharing, early returns
7. **Enhance**: Covariance-based anisotropic kernel (advanced)

---

## See Also

- Full analysis: `docs/analysis/rendering/ue5-pcss-implementation-analysis.md`
- UE5 source: `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowPercentageCloserFiltering.ush`
