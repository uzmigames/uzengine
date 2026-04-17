<!-- doc-meta
{
  "author": "Claude Code",
  "date": "2026-03-16",
  "status": "maintained",
  "priority": "P0",
  "version": "1.0"
}
-->

# RHI Analysis Index — UE5 Patterns for UzEngine

Complete index of architectural analyses extracted from UE5 5.6 source code.

## Latest Analysis: SM5 (D3D11) Fallback Patterns

**Created**: 2026-03-16
**Focus**: How UE5 supports older hardware without compute shaders or bindless resources
**Status**: COMPLETE (5 documents, 4400+ lines)

### Read in This Order

1. **[ue5-sm5-summary.md](ue5-sm5-summary.md)** (8.4 KB)
   - Quick reference: 8 Q&A about SM5 support
   - Key statistics and default values
   - Architectural decisions for UzEngine
   - **TIME**: 5 minutes

2. **[ue5-sm5-fallback-patterns.md](ue5-sm5-fallback-patterns.md)** (18 KB)
   - Detailed architectural breakdown
   - Auto-exposure (histogram vs fallback)
   - BRDF LUT, irradiance SH, prefiltered specular
   - GBuffer binding strategies
   - Feature level detection
   - **TIME**: 20 minutes

3. **[ue5-sm5-code-patterns.md](ue5-sm5-code-patterns.md)** (20 KB)
   - Concrete C++ and HLSL code examples
   - Feature level gating patterns
   - Auto-exposure fallback computation
   - Offline BRDF baking tool
   - SH irradiance buffer management
   - Static register-based binding
   - **TIME**: 30 minutes (reference while coding)

4. **[ue5-sm5-decision-tree.md](ue5-sm5-decision-tree.md)** (10 KB)
   - Visual decision trees for each feature
   - Feature level decision matrix
   - Implementation checklist by subsystem
   - Platform-specific variations (D3D11/12, Vulkan)
   - Debug checklist
   - Validation checklist
   - **TIME**: Reference while implementing

5. **[README.md](README.md)** (7 KB)
   - Overview and cross-references
   - Design pattern summary
   - Implementation order
   - Source files analyzed

---

## Other RHI Analyses

- **[ue5-vulkan-bindless-analysis.md](ue5-vulkan-bindless-analysis.md)** (Vulkan specialization, created 2026-03-14)
- **[ue5-cbuffer-layout-dx12-vulkan-analysis.md](ue5-cbuffer-layout-dx12-vulkan-analysis.md)** (CB packing strategies, created 2026-03-14)

---

## Key Findings Summary

### SM5 Fallback Strategy: "Offline-First"

UE5 pre-computes all IBL data offline and loads at runtime. No runtime generation on SM5.

| Component | SM5 Approach | Value |
|-----------|-------------|-------|
| **Auto-Exposure** | Histogram CS (SM5+) or fixed fallback | 11.8× (default) |
| **BRDF LUT** | Pre-baked 256×256 RG16F | 512 KB |
| **Irradiance SH** | 9 coefficients → 7 float4s | 112 bytes/env |
| **Prefiltered** | 8-mip cubemap (128×128 base) | 4-8 MB |
| **GBuffer Binding** | 8 register-bound textures (t0-t7) | Fixed, no dynamic |
| **Root Signature** | Fixed descriptor allocation | D3D11 style |

### Auto-Exposure Fallback Calculation

```cpp
float exposure = (MinAverageLuminance + MaxAverageLuminance) * 0.5f / 0.18f;
// Default: (0.25 + 4.0) * 0.5 / 0.18 ≈ 11.8× brightness
```

### GBuffer Binding (No Bindless)

```hlsl
Texture2D SceneDepthTexture : register(t0);
Texture2D GBufferATexture : register(t1);
Texture2D GBufferBTexture : register(t2);
Texture2D GBufferCTexture : register(t3);
Texture2D GBufferDTexture : register(t4);
Texture2D GBufferETexture : register(t5);
Texture2D GBufferVelocityTexture : register(t6);
Texture2D<uint> SceneLightingChannels : register(t7);

SamplerState PointClampedSampler : register(s0);
```

### IBL Textures in Deferred Lighting

```hlsl
Texture2D BRDFLut : register(t8);
TextureCubeArray ReflectionCaptures : register(t9);
StructuredBuffer<float4> SkyIrradianceEnvironmentMap : register(t10);
```

---

## Feature Level Support Matrix

```
┌─────────────────────┬──────────┬──────────┬───────────────┐
│ Feature             │ SM5 (11) │ SM6 (12) │ SM6.6 (Bindless)
├─────────────────────┼──────────┼──────────┼───────────────┤
│ Compute Shaders     │ YES      │ YES      │ YES           │
│ Bindless Resources  │ NO       │ PARTIAL  │ YES           │
│ Static Samplers     │ YES      │ YES      │ YES           │
│ Texture Arrays      │ YES      │ YES      │ YES           │
│ Structured Buffers  │ YES      │ YES      │ YES           │
│ Root Signature Type │ Fixed    │ Dynamic  │ GPU-Driven    │
│ Descriptor Indexing │ Register │ Register │ Bindless Table│
└─────────────────────┴──────────┴──────────┴───────────────┘
```

---

## For UzEngine Implementation

### Immediate Priority
1. Feature level detection (SM5 vs SM6 vs SM6.6)
2. Auto-exposure fallback (11.8× default)
3. GBuffer register binding (static, no dynamic)

### Phase 1 (Core IBL)
1. Load pre-baked BRDF LUT
2. Structured buffer for SH irradiance
3. Cubemap with mip sampling

### Phase 2 (Offline Tools)
1. BRDF LUT baking tool
2. SH projection tool
3. Cubemap prefiltering pipeline

### Phase 3 (Validation)
1. Visual comparison against UE5
2. Debug checklist (PIX/RenderDoc)
3. Performance profiling

---

## Source Analysis

**Files Analyzed**: 6 UE5 source files (2000+ lines total)
- PostProcessEyeAdaptation.cpp (eye adaptation system)
- ReflectionEnvironment.cpp (IBL baking)
- DeferredShadingCommon.ush (GBuffer definitions)
- DeferredLightPixelShaders.usf (deferred lighting pass)
- SystemTextures.cpp (static system resources)
- EyeAdaptationCommon.ush (exposure math)

**UE5 Version**: 5.6 (W:\UE_5.6_Source)
**Analysis Date**: 2026-03-16
**Total Documentation**: 4400+ lines

---

## Related Documentation

- `docs/architecture/` — Architectural decision records
- `docs/specs/rendering/` — Rendering subsystem specifications
- `docs/guides/` — Developer guides and tutorials

---

## Document Status

| Document | Size | Status | Last Updated |
|----------|------|--------|--------------|
| ue5-sm5-summary.md | 8.4 KB | ✅ Complete | 2026-03-16 |
| ue5-sm5-fallback-patterns.md | 18 KB | ✅ Complete | 2026-03-16 |
| ue5-sm5-code-patterns.md | 20 KB | ✅ Complete | 2026-03-16 |
| ue5-sm5-decision-tree.md | 10 KB | ✅ Complete | 2026-03-16 |
| ue5-vulkan-bindless-analysis.md | 27 KB | ✅ Complete | 2026-03-14 |
| ue5-cbuffer-layout-dx12-vulkan-analysis.md | 8.3 KB | ✅ Complete | 2026-03-14 |

---

## Quick Links

- **Implementation Guide**: [ue5-sm5-decision-tree.md](ue5-sm5-decision-tree.md)
- **Code Examples**: [ue5-sm5-code-patterns.md](ue5-sm5-code-patterns.md)
- **Architecture Deep Dive**: [ue5-sm5-fallback-patterns.md](ue5-sm5-fallback-patterns.md)
- **Quick Reference**: [ue5-sm5-summary.md](ue5-sm5-summary.md)

---

**Last Updated**: 2026-03-16
**Maintainer**: Claude Code (Unreal Engine Architecture Analysis)
