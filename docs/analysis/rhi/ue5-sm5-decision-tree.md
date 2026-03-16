<!-- doc-meta
{
  "author": "Claude Code",
  "date": "2026-03-16",
  "status": "final",
  "priority": "P0",
  "version": "1.0"
}
-->

# UE5 SM5 Decision Tree for UzEngine Implementation

Use this decision tree when implementing SM5 support for IBL and lighting.

---

## Quick Lookup: "How does UE5 handle X on SM5?"

```
┌─ "BRDF LUT Generation"
│  ├─ "Can we compute it at runtime on SM5?"
│  │  └─ NO → Pre-bake offline
│  │
│  └─ "How?" → Load 256×256 RG16F texture from disk at startup

┌─ "Irradiance Computation"
│  ├─ "Can we project cubemap to SH at runtime on SM5?"
│  │  └─ NO → Pre-bake offline during level load
│  │
│  └─ "How?" → Pack SH L0/L1/L2 into 7 float4s, upload to GPU buffer

┌─ "Prefiltered Specular"
│  ├─ "Can we filter cubemap at runtime?"
│  │  └─ NO → Pre-bake offline with mip chain
│  │
│  └─ "How?" → Load 128×128 cubemap with 8 mips from disk

┌─ "Auto-Exposure"
│  ├─ "Can we compute histogram on SM5?"
│  │  ├─ YES → Use compute shader (histogram → exposure)
│  │  └─ NO → Use fixed fallback
│  │
│  └─ "Fallback?" → exposure = (MinLum + MaxLum) / 2 / 0.18 ≈ 11.8×

┌─ "GBuffer Binding"
│  ├─ "Can we use bindless descriptors?"
│  │  └─ NO → Register-bind all textures at once
│  │
│  └─ "How?" → PSSetShaderResources(0, 8, &gbufs[0])

┌─ "Shadow Atlas Access"
│  ├─ "Dynamic descriptor indexing?"
│  │  └─ NO → Static texture slot
│  │
│  └─ "How?" → Texture2D ShadowAtlas : register(t3)

┌─ "IBL in Deferred Lighting"
│  ├─ "Bindless table lookups?"
│  │  └─ NO → Pre-bind all IBL textures
│  │
│  ├─ BRDF LUT: register(t8)
│  ├─ Reflection captures: register(t9)
│  └─ Sky irradiance: register(t10)
```

---

## Feature Level Decision Matrix

| Feature | SM5 (D3D11) | SM6 (D3D12) | SM6.6 (Bindless) |
|---------|-----------|-----------|-----------------|
| **Auto-Exposure** | Histogram CS | Histogram CS | Histogram CS |
| **Fallback Exposure** | Fixed (11.8×) | Fixed (11.8×) | Dynamic |
| **BRDF LUT** | Pre-baked load | Pre-baked load | Pre-baked load |
| **Irradiance SH** | Buffer upload | Buffer upload | Dynamic compute |
| **Prefiltered** | Disk load | Disk load | Runtime filter |
| **GBuffer Binding** | Register slots | Register slots | Bindless table |
| **Register Limit** | ~14 (SM5 limit) | Unlimited | Unbounded |
| **Root Signature** | Fixed | Dynamic | GPU-driven |

---

## Implementation Checklist by Feature

### Auto-Exposure

```
□ Check feature level → SupportsComputeShaders()
  ├─ YES → Dispatch histogram compute shader
  │        └─ Output: RWStructuredBuffer<float4> (1 float per component)
  │
  └─ NO → Compute fixed exposure
           ├─ Formula: (MinLum + MaxLum) / 2 / 0.18
           ├─ Default value: ~11.8×
           └─ Cache in View.PreExposure
```

**Code locations**:
- Feature check: `PostProcessEyeAdaptation.cpp:169`
- Fixed formula: `PostProcessEyeAdaptation.cpp:567-578`
- Upload: ViewConstants.PreExposure

### BRDF LUT

```
□ Check if texture exists in system resources
  ├─ YES → Bind to deferred lighting shader (t8)
  │
  └─ NO → ERROR: pre-baking failed
           └─ Run offline baker on dev machine first
```

**Code locations**:
- Load: `SystemTextures.cpp`
- Bind: Deferred lighting root signature
- Offline baker: `IblBaker::BakeBrdfLut()`

### Irradiance SH

```
□ At level load:
  ├─ Load pre-baked SH coefficients from disk
  ├─ OR compute via CPU SH projection (one-time)
  └─ Pack into 7 float4s
      ├─ [0]: L1x, L1y, L1z, L0-L2zz
      ├─ [1-6]: L2 and auxiliary
      └─ Format: sRGB or linear (pre-multiplied by diffuse convolution)

□ Upload to GPU
  └─ StructuredBuffer<float4> SkyIrradianceEnvironmentMap

□ In deferred lighting shader
  ├─ Unpack 7 float4s
  ├─ Evaluate SH basis functions for surface normal
  └─ Output diffuse contribution
```

**Code locations**:
- Packing: `ReflectionEnvironment.cpp:623-669`
- Upload: `ReflectionEnvironment.cpp:690-714`
- Evaluation: `SHCommon.ush` or custom implementation

### Prefiltered Specular

```
□ At level load:
  ├─ Load pre-baked mip-mapped cubemap from disk
  │  ├─ Resolution: 128×128 base
  │  └─ Mips: 8 (1×1 final)
  │
  └─ Bind as TextureCube or TextureCubeArray (t9)

□ In deferred lighting shader
  ├─ Sample by roughness-derived mip level
  ├─ Format: roughnessMip = roughness * MaxMip
  └─ Access: CubeMap.SampleLevel(Sampler, reflectDir, roughnessMip)
```

**Code locations**:
- Load: Asset manager (cubemap importer)
- Bind: Deferred lighting root signature (t9)
- Sample: Deferred lighting shader

### GBuffer Binding (SM5)

```
□ Bind all at once (D3D11 requirement)
  ├─ t0: SceneDepth
  ├─ t1: GBufferA (diffuse + AO)
  ├─ t2: GBufferB (normal + metallic)
  ├─ t3: GBufferC (roughness + cavity)
  ├─ t4: GBufferD (custom data)
  ├─ t5: GBufferE (precomputed shadow)
  ├─ t6: GBufferVelocity
  └─ t7: SceneLightingChannels

□ Set sampler
  └─ s0: PointClamp (static, shared)

□ C++ binding
  └─ cmd.PSSetShaderResources(0, 8, &gbuffers[0]);
```

**Code locations**:
- Shader declares: `DeferredShadingCommon.ush:32-51`
- C++ binding: Device context method (platform-specific)

---

## Fallback Decision Tree: "What if compute shaders unavailable?"

```
Auto-Exposure:
├─ Histogram compute available?
│  ├─ YES → Use it
│  └─ NO ──┐
│          └─ Use fixed: exposure = (0.25 + 4.0) / 2 / 0.18
│             └─ Cache in View.PreExposure
│
BRDF LUT:
├─ Pre-baked texture loaded?
│  ├─ YES → Use it
│  └─ NO ──┐
│          └─ ERROR: Must bake offline first
│
Irradiance:
├─ Pre-baked SH available?
│  ├─ YES → Use it
│  └─ NO ──┐
│          └─ Compute once at startup via CPU SH projection
│             └─ Cache for entire level
│
Prefiltered:
├─ Pre-baked cubemap available?
│  ├─ YES → Use it
│  └─ NO ──┐
│          └─ ERROR: Must bake offline first
```

---

## Platform-Specific Variations

### DirectX 11 (SM5)

```cpp
// Register-based binding (D3D11 style)
Texture2D SceneDepth : register(t0);
Texture2D GBufferA : register(t1);
// ... t2-t7
Texture2D BrdfLut : register(t8);
TextureCubeArray ReflCaptures : register(t9);
StructuredBuffer<float4> SkyIrradiance : register(t10);

SamplerState PointClampSampler : register(s0);

// All textures bound upfront
pDeviceContext->PSSetShaderResources(0, 8, &gbuffers[0]);
pDeviceContext->PSSetShaderResources(8, 1, &brdfLut);
pDeviceContext->PSSetShaderResources(9, 1, &reflCaptures);
pDeviceContext->PSSetShaderResources(10, 1, &skyIrrSRV);
```

### DirectX 12 (SM6+)

```cpp
// Still register-based, but descriptors are GPU-allocated
// Can use descriptor arrays (non-bindless variant)
RootSignature:
  ├─ Root constants (exposure)
  ├─ CBV for view (b0)
  ├─ CBV for light (b1)
  ├─ Descriptor table [t0-t10] (GBuffers + IBL)
  └─ Static sampler (s0)
```

### Vulkan (SM5 equivalent)

```cpp
// Similar to D3D11: fixed descriptor layout
// Descriptor set 0:
//   ├─ Binding 0-7: GBuffer textures
//   ├─ Binding 8: BRDF LUT
//   ├─ Binding 9: Reflection captures
//   └─ Binding 10: Sky irradiance
```

---

## Debug Checklist: "Why is IBL dark on SM5?"

```
□ Is pre-exposure applied?
  └─ Check: View.PreExposure > 0 in deferred lighting shader

□ Is BRDF LUT bound?
  └─ Verify: GBuffer binding includes register(t8)

□ Is reflection capture bound?
  └─ Verify: TextureCubeArray at register(t9) is valid

□ Is SH irradiance buffer bound?
  └─ Verify: StructuredBuffer at register(t10) has 7 float4s

□ Is SH evaluation correct?
  └─ Check: SH basis function computation matches L0/L1/L2 unpacking

□ Is exposure fallback being used?
  └─ If no histogram compute: exposure = (MinLum + MaxLum) / 2 / 0.18
     └─ Should be ~11.8× for default settings

□ Are reflections sampling wrong mip?
  └─ Check: roughnessMip = roughness * MaxMip (for 8 mips, max=7)

□ Is shadow atlas accessible?
  └─ Verify: ShadowAtlas bound to fixed register slot
```

---

## Performance Characteristics

| Feature | SM5 Cost | Notes |
|---------|----------|-------|
| Fixed exposure | ~0 | Single multiply per pixel |
| BRDF LUT lookup | 1 texture fetch | Cached effectively |
| SH evaluation | 7 dot products | ~21 MADs (light) |
| Reflect sample | 1 cubemap sample + mip select | GPU cached |
| Total IBL (per pixel) | ~10 µs | Amortized across deferred lights |

**Memory**:
- BRDF LUT: 256×256×8 bytes = 512 KB
- Irradiance buffer: 7 × 16 bytes = 112 bytes (per environment)
- Prefiltered: 128³ × 8 bytes (mips) ≈ 4-8 MB

---

## Validation Checklist Before Shipping SM5 Support

```
□ Auto-Exposure
  ├─ Histogram compute available? → Verify output texture
  └─ Fixed fallback working? → Test on SM5 hardware/emulator

□ IBL Quality
  ├─ BRDF LUT loaded? → Inspect texture in GPU debugger
  ├─ SH coefficients valid? → Check ranges (0-1 typical)
  └─ Prefiltered mips correct? → Visual inspection (smooth to blurry)

□ Deferred Lighting
  ├─ All 8 GBuffers bound? → Verify register slots
  ├─ No register conflicts? → t0-t7 unique, no overwrites
  └─ Shadow atlas accessible? → Test shadow rendering

□ Performance
  ├─ Memory budget OK? → BRDF (512KB) + cubemaps (4-8MB)
  ├─ Frame time acceptable? → Profile on target SM5 device
  └─ No GPU stalls? → Use GPU timer (PIX/RenderDoc)

□ Correctness
  ├─ Visual match UE5? → Screenshot comparison
  ├─ No black screens? → Check exposure fallback
  └─ No artifacts? → Look for mipping issues, SH banding
```

---

## See Also

- `ue5-sm5-summary.md` — Quick reference guide
- `ue5-sm5-fallback-patterns.md` — Detailed analysis
- `ue5-sm5-code-patterns.md` — Code examples
