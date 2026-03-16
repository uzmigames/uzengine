<!-- doc-meta
{
  "author": "Claude Code",
  "date": "2026-03-16",
  "status": "final",
  "priority": "P0",
  "version": "1.0"
}
-->

# RHI Analysis: UE5 Patterns for UzEngine

This directory contains detailed architectural analyses of UE5 rendering systems, with focus on extracting patterns that UzEngine can adapt.

## SM5 (Feature Level 11_0 / DX11) Fallback Patterns

**Problem**: UE5 supports older hardware (D3D11, mobile) that lacks compute shaders and bindless resources. How?

**Answer**: Pre-compute everything offline, load at runtime. This directory documents the exact patterns UE5 uses.

### Documents

1. **ue5-sm5-summary.md** — START HERE
   - Answers to 8 key questions about SM5 support
   - Quick statistics and values
   - Critical design decisions for UzEngine
   - **Read this first (5 min)**

2. **ue5-sm5-fallback-patterns.md** — DETAILED ANALYSIS
   - Full architectural breakdown of each subsystem
   - Auto-exposure (histogram + fallback)
   - BRDF LUT, irradiance SH, prefiltered specular
   - Deferred lighting GBuffer binding
   - Feature level detection patterns
   - **Read this for deep understanding (20 min)**

3. **ue5-sm5-code-patterns.md** — CONCRETE CODE
   - C++ and HLSL code examples from UE5 source
   - Feature level gating patterns
   - Auto-exposure fallback computation
   - BRDF LUT offline baking
   - SH irradiance buffer management
   - GBuffer static binding patterns
   - Register allocation (SM5 vs SM6)
   - **Read this when implementing (30 min)**

4. **ue5-sm5-decision-tree.md** — IMPLEMENTATION GUIDE
   - Visual decision tree for each feature
   - Feature level decision matrix
   - Implementation checklist by subsystem
   - Platform-specific variations (D3D11, D3D12, Vulkan)
   - Debug checklist for common issues
   - Validation checklist before shipping
   - **Reference this while coding (ongoing)**

### Key Findings

#### Auto-Exposure
- **SM5+**: Histogram compute shader → structured buffer (single thread group, 1x1x1)
- **Fallback**: Fixed exposure = `(MinLum + MaxLum) / 2 / 0.18 ≈ 11.8×` brightness
- **Feature check**: `IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5)`

#### IBL Maps (BRDF, Irradiance, Prefiltered)
- **No runtime generation on SM5** — all pre-baked offline
- **BRDF LUT**: 256×256 RG16F, loaded from disk at startup
- **Irradiance**: 9 SH coefficients packed as 7 float4s, pre-computed at level load
- **Prefiltered**: 128×128 cubemap with 8 mips, sampled by roughness

#### Deferred Lighting
- **GBuffer binding**: 8 named textures (t0-t7) register-bound at once (D3D11 limitation)
- **Static samplers**: PointClamp (s0), no per-material variation
- **IBL textures**: BRDF (t8), reflection captures (t9), sky irradiance (t10)
- **No bindless indirection** — all registers allocated statically

#### Platform Support
| Feature | SM5 (D3D11) | SM6 (D3D12) | SM6.6 (Bindless) |
|---------|-----------|-----------|-----------------|
| Auto-exposure | Histogram CS | Histogram CS | Histogram CS |
| Fallback | Fixed 11.8× | Fixed 11.8× | Dynamic |
| BRDF LUT | Pre-baked | Pre-baked | Pre-baked |
| Irradiance | Buffer upload | Buffer upload | Dynamic compute |
| Binding | Register slots | Register slots | GPU-driven |

### Source Files Analyzed

**Auto-Exposure** (Eye Adaptation):
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\PostProcessEyeAdaptation.cpp` (1400+ lines)
  - `GetBasicEyeAdaptationMinFeatureLevel()` — line 169
  - `GetEyeAdaptationFixedExposure()` — line 567
  - `AddHistogramEyeAdaptationPass()` — line 825
  - `UpdatePreExposure()` — line 1197

**IBL / Reflection Environment**:
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ReflectionEnvironment.cpp` (715 lines)
  - `SetupSkyIrradianceEnvironmentMapConstantsFromSkyIrradiance()` — line 623
  - `FReflectionEnvironmentCubemapArray::InitRHI()` — line 148
  - `UpdateSkyIrradianceGpuBuffer()` — line 671

**Deferred Lighting**:
- `W:\UE_5.6_Source\Engine\Shaders\Private\DeferredShadingCommon.ush` (250+ lines)
  - GBuffer texture declarations — lines 32-51
  - Static sampler definitions — lines 44-51
- `W:\UE_5.6_Source\Engine\Shaders\Private\DeferredLightPixelShaders.usf`
  - Deferred light pass entry point — line 230
  - Shadow atlas access — lines 123-149

### Design Pattern: "Offline-First"

UE5 uses this architectural pattern for SM5 support:

```
┌─ Content Pipeline (Edit-Time)
│  ├─ Bake BRDF LUT (256×256)
│  ├─ Project environment to SH (9 coefficients)
│  └─ Prefilter cubemap (8 mips)
│
└─ Runtime (Engine)
   ├─ Load pre-baked textures from disk
   ├─ Upload SH coefficients to GPU buffer
   └─ Use fixed exposure fallback if no histogram
```

**Benefits**:
- Zero runtime overhead on SM5
- Same offline pipeline for all platforms
- Predictable, baked-in quality
- No dynamic generation bottlenecks

**Trade-offs**:
- Can't change lighting dynamically at runtime
- Requires offline pre-processing step
- Less flexible for content creators

### Recommendations for UzEngine

1. **Keep compute shader path** for histogram (currently working for SM6+)
2. **Add pixel shader fallback** for mobile/SM5 with single-value exposure readback
3. **Pre-bake BRDF LUT offline** using IblBaker compute shader on dev machine
4. **Store SH as 7 float4 structured buffer** (9 coefficients, pre-multiplied)
5. **Use fixed exposure fallback** = 11.8× when histogram unavailable
6. **Register-bind GBuffer textures** (no dynamic indirection on SM5)
7. **Load prefiltered cubemaps from disk** at level load (8 mips for 128×128)

### Implementation Order

For UzEngine SM5 support, implement in this order:

1. **Feature level detection** — `SupportsComputeShaders()`, `SupportsBindless()`
2. **Exposure fallback** — Fixed calculation, cache in View.PreExposure
3. **GBuffer binding** — Register-based slots, static samplers
4. **BRDF LUT loading** — Pre-baked texture from disk
5. **Irradiance SH** — Structured buffer upload at level load
6. **Prefiltered specular** — Cubemap mip sampling by roughness
7. **Visual validation** — Screenshot comparison against UE5

---

## Other RHI Analyses

(To be added as more analyses are completed)

### Related Documents

- `docs/architecture/` — Architectural decision records
- `docs/specs/rendering/` — Rendering subsystem specifications
- `docs/guides/shader-authoring.md` — Shader development guide

---

## Citation

When using patterns from these documents in UzEngine code, cite the UE5 source:

```cpp
// Adapted from UE5 5.6 source:
// W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\PostProcessEyeAdaptation.cpp
// GetEyeAdaptationFixedExposure() — line 567
float ComputeFixedExposure(float min_lum, float max_lum) {
    // ... implementation ...
}
```

---

## Questions?

Refer to the decision tree in `ue5-sm5-decision-tree.md` for quick answers while implementing.

For deep dives, start with `ue5-sm5-summary.md` then read `ue5-sm5-fallback-patterns.md`.

Code examples are in `ue5-sm5-code-patterns.md`.
