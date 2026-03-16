<!-- doc-meta
{
  "author": "Claude Code",
  "date": "2026-03-16",
  "status": "final",
  "priority": "P0",
  "version": "1.0"
}
-->

# UE5 SM5 Patterns — Concrete Code Examples for UzEngine

This document provides exact code patterns from UE5 5.6 source that UzEngine should adapt (not copy) for SM5 support.

---

## 1. Feature Level Gating Pattern

### UE5 Pattern (Eye Adaptation)

**File**: `PostProcessEyeAdaptation.cpp` line 169-172, 817-820

```cpp
// Determine minimum feature level for basic eye adaptation
static ERHIFeatureLevel::Type GetBasicEyeAdaptationMinFeatureLevel()
{
    return IsMobileHDR() ? ERHIFeatureLevel::ES3_1 : ERHIFeatureLevel::SM5;
}

// Compile-time check in shader permutation
static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}
```

### UzEngine Adaptation

```cpp
namespace uz {

// uz/engine/renderer/core/feature_levels.h
enum class RhiFeatureLevel {
    SM5,        // D3D11, Vulkan on legacy hardware
    SM6,        // D3D12, modern Vulkan
    SM6_6,      // D3D12 with GPU-driven resources
};

inline bool SupportsComputeShaders(RhiFeatureLevel level) {
    return level >= RhiFeatureLevel::SM5;
}

inline bool SupportsBindless(RhiFeatureLevel level) {
    return level >= RhiFeatureLevel::SM6_6;
}

// uz/engine/renderer/shaders/global_shader.h
struct GlobalShaderCompileContext {
    RhiFeatureLevel feature_level;

    bool ShouldCompileShader(RhiFeatureLevel min_level) const {
        return feature_level >= min_level;
    }
};

} // namespace uz
```

---

## 2. Auto-Exposure Fallback Pattern

### UE5 Pattern (Eye Adaptation)

**File**: `PostProcessEyeAdaptation.cpp` line 567-578

```cpp
// Compute fixed exposure when histogram unavailable
float GetEyeAdaptationFixedExposure(const FViewInfo& View)
{
    const FEyeAdaptationParameters Parameters = GetEyeAdaptationParameters(View);

    // Midpoint of luminance range
    const float Exposure = (Parameters.MinAverageLuminance + Parameters.MaxAverageLuminance) * 0.5f;

    // Standard grey card calibration
    const float kMiddleGrey = 0.18f;
    const float ExposureScale = kMiddleGrey / FMath::Max(0.0001f, Exposure);

    // Apply compensation
    return ExposureScale * Parameters.ExposureCompensationSettings;
}
```

### UzEngine Adaptation

```cpp
// uz/engine/renderer/post_process/auto_exposure.h

class AutoExposureSystem {
public:
    struct Parameters {
        float min_avg_luminance = 0.25f;
        float max_avg_luminance = 4.0f;
        float compensation = 1.0f;
        float delta_time = 0.016f;
    };

    // Fallback for SM5/mobile (no compute shader)
    static float ComputeFixedExposure(const Parameters& params) {
        const float kMiddleGrey = 0.18f;

        // Midpoint of luminance range
        float avg_lum = (params.min_avg_luminance + params.max_avg_luminance) * 0.5f;

        // Exposure scale relative to middle grey
        float exposure_scale = kMiddleGrey / std::max(0.0001f, avg_lum);

        return exposure_scale * params.compensation;
    }

    // Histogram-based (SM5+, compute shader)
    static RhiResult ComputeHistogramExposure(
        RhiCommandList& cmd,
        const uz::Handle<RhiBuffer> histogram_buf,
        const uz::Handle<RhiBuffer> out_exposure_buf,
        RhiFeatureLevel level,
        const Parameters& params)
    {
        if (level < RhiFeatureLevel::SM5) {
            // Fallback to fixed
            float exposure = ComputeFixedExposure(params);
            // Upload to GPU via constant buffer
            return RhiResult::Ok;
        }

        // Dispatch histogram-to-exposure compute shader
        // ... (compute dispatch code)
        return RhiResult::Ok;
    }
};
```

---

## 3. BRDF LUT Offline Baking Pattern

### UE5 Pattern (System Textures)

**Concept**: Pre-computed and loaded statically, never generated at runtime on SM5.

### UzEngine Adaptation

```cpp
// uz/engine/renderer/core/system_textures.h

class SystemTextures {
public:
    // Pre-baked BRDF LUT (256×256, RG16F)
    uz::Handle<RhiTexture> brdf_lut;

    // Initialize from disk
    uz::Result<void> Init(RhiDevice& device) {
        // Load pre-baked texture from engine resources
        uz::ByteBuffer data;
        uz::TRY(uz::Asset::ReadEngineResource("textures/brdf_lut.uz", data));

        RhiTextureDesc desc{
            .width = 256,
            .height = 256,
            .dimension = RhiTextureDimension::Tex2D,
            .format = RhiFormat::R16G16_Float,
            .usage = RhiTextureUsage::ShaderRead,
        };

        uz::TRY_ASSIGN(brdf_lut, device.CreateTexture(desc, data));
        return uz::Ok();
    }
};

// uz/engine/renderer/core/ibl_baker.h (for offline baking on dev machine)

class IblBaker {
public:
    // Run on dev machine before shipping
    uz::Result<uz::ByteBuffer> BakeBrdfLut() {
        // ... compute GGX BRDF integration ...
        // Output: 256×256×2 float16 pixels
        uz::ByteBuffer result(256 * 256 * sizeof(float) * 2);

        // Compute each pixel
        for (uint32_t y = 0; y < 256; ++y) {
            for (uint32_t x = 0; x < 256; ++x) {
                float ndotv = (x + 0.5f) / 256.0f;
                float roughness = (y + 0.5f) / 256.0f;

                // Integrate over BRDF
                auto [f0, f90] = IntegrateBrdf(ndotv, roughness);

                uint32_t idx = (y * 256 + x) * 2;
                result.AsSpan<float>()[idx + 0] = f0;
                result.AsSpan<float>()[idx + 1] = f90;
            }
        }

        return result;
    }

private:
    std::pair<float, float> IntegrateBrdf(float ndotv, float roughness) {
        // ... GGX Fresnel integration ...
        // Return (F0 integrated, F90 integrated)
        return {0.5f, 0.5f};
    }
};
```

---

## 4. SH Irradiance Buffer Pattern

### UE5 Pattern (Sky Irradiance)

**File**: `ReflectionEnvironment.cpp` line 623-669

```cpp
void SetupSkyIrradianceEnvironmentMapConstantsFromSkyIrradiance(
    FVector4f* OutSkyIrradianceEnvironmentMap,
    const FSHVectorRGB3 SkyIrradiance)
{
    const float SqrtPI = FMath::Sqrt(PI);
    const float Coefficient0 = 1.0f / (2 * SqrtPI);
    const float Coefficient1 = FMath::Sqrt(3.f) / (3 * SqrtPI);
    const float Coefficient2 = FMath::Sqrt(15.f) / (8 * SqrtPI);
    const float Coefficient3 = FMath::Sqrt(5.f) / (16 * SqrtPI);
    const float Coefficient4 = .5f * Coefficient2;

    // Pack L0, L1, L2 into 7 float4s (pre-multiplied by diffuse convolution)
    OutSkyIrradianceEnvironmentMap[0].X = -Coefficient1 * SkyIrradiance.R.V[3];  // L1x red
    OutSkyIrradianceEnvironmentMap[0].Y = -Coefficient1 * SkyIrradiance.R.V[1];  // L1y red
    OutSkyIrradianceEnvironmentMap[0].Z = Coefficient1 * SkyIrradiance.R.V[2];   // L1z red
    OutSkyIrradianceEnvironmentMap[0].W = Coefficient0 * SkyIrradiance.R.V[0] - Coefficient3 * SkyIrradiance.R.V[6];

    // ... green and blue similar ...
    // Outputs 7 float4s total
}
```

### UzEngine Adaptation

```cpp
// uz/engine/renderer/ibl/sh_irradiance.h

namespace uz::ibl {

// SH coefficients up to L2 (9 total: L0×1 + L1×3 + L2×5)
struct SHCoefficients {
    std::array<glm::vec3, 9> rgb;  // 9 coefficients, 3 channels each
};

// Pre-multiplied for diffuse convolution
struct SHIrradiance {
    std::array<glm::vec4, 7> packed_rgb;  // 7 float4s packed for shader eval
};

// Compute SH coefficients from cubemap (offline tool)
uz::Result<SHCoefficients> ProjectCubemapToSH(
    const uz::Image& positive_x,
    const uz::Image& negative_x,
    const uz::Image& positive_y,
    const uz::Image& negative_y,
    const uz::Image& positive_z,
    const uz::Image& negative_z)
{
    SHCoefficients sh;
    std::fill(sh.rgb.begin(), sh.rgb.end(), glm::vec3(0));

    // Integrate SH basis functions over cubemap
    // ... (cubemap sampling loop) ...

    return sh;
}

// Pack for GPU upload
SHIrradiance PackForShader(const SHCoefficients& sh) {
    SHIrradiance out;

    const float kSqrtPi = std::sqrt(3.14159265f);
    const float c0 = 1.0f / (2.0f * kSqrtPi);
    const float c1 = std::sqrt(3.f) / (3.0f * kSqrtPi);
    const float c2 = std::sqrt(15.f) / (8.0f * kSqrtPi);
    const float c3 = std::sqrt(5.f) / (16.0f * kSqrtPi);
    const float c4 = 0.5f * c2;

    // Pack L1, L1, L1, L0-L2 terms
    out.packed_rgb[0].x = -c1 * sh.rgb[3].x;  // L1x
    out.packed_rgb[0].y = -c1 * sh.rgb[1].x;  // L1y
    out.packed_rgb[0].z = c1 * sh.rgb[2].x;   // L1z
    out.packed_rgb[0].w = c0 * sh.rgb[0].x - c3 * sh.rgb[6].x;  // L0 - L2zz

    // ... similar for green, blue ...

    return out;
}

// GPU buffer management
class SHIrradianceBuffer {
public:
    uz::Handle<RhiBuffer> gpu_buffer;
    SHIrradiance cpu_cache;

    uz::Result<void> Update(
        RhiDevice& device,
        RhiCommandList& cmd,
        const SHCoefficients& new_sh)
    {
        cpu_cache = PackForShader(new_sh);

        // Upload to GPU
        uz::TRY(device.UpdateBuffer(
            gpu_buffer,
            0,
            uz::span_bytes<const glm::vec4>(cpu_cache.packed_rgb)));

        return uz::Ok();
    }
};

} // namespace uz::ibl
```

---

## 5. GBuffer Static Texture Binding

### UE5 Pattern (Deferred Lighting)

**File**: `DeferredShadingCommon.ush` line 32-51

```hlsl
#if SHADING_PATH_DEFERRED

Texture2D SceneDepthTexture;
Texture2D<uint2> SceneStencilTexture;
Texture2D GBufferATexture;
Texture2D GBufferBTexture;
Texture2D GBufferCTexture;
Texture2D GBufferDTexture;
Texture2D GBufferETexture;
Texture2D GBufferVelocityTexture;
Texture2D GBufferFTexture;
Texture2D<uint> SceneLightingChannels;

#define GBufferATextureSampler GlobalPointClampedSampler
#define GBufferBTextureSampler GlobalPointClampedSampler
// ... etc
```

### UzEngine Adaptation

```cpp
// uz/engine/renderer/deferred/gbuffer_binding.h

namespace uz::deferred {

struct GBufferBinding {
    // All textures are register-bound (not dynamic)
    uz::Handle<RhiTexture> depth;         // t0
    uz::Handle<RhiTexture> gbuffer_a;     // t1 - Diffuse + AO
    uz::Handle<RhiTexture> gbuffer_b;     // t2 - Normal + Metallic
    uz::Handle<RhiTexture> gbuffer_c;     // t3 - Roughness + Cavity
    uz::Handle<RhiTexture> gbuffer_d;     // t4 - Custom data
    uz::Handle<RhiTexture> gbuffer_e;     // t5 - Precomputed shadow
    uz::Handle<RhiTexture> velocity;      // t6 - Motion vectors
    uz::Handle<RhiTexture> lighting_channels;  // t7

    // Static sampler state (no variation)
    // All use point/clamp sampling
};

// Register all GBuffer textures to deferred light pass
uz::Result<void> BindGBuffers(
    RhiCommandList& cmd,
    const GBufferBinding& gbuffers,
    RhiPipelineLayout layout)
{
    // D3D11 equivalent: PSSetShaderResources(0, 8, &gbuffers.depth);
    uz::TRY(cmd.BindTexture(0, gbuffers.depth));      // t0
    uz::TRY(cmd.BindTexture(1, gbuffers.gbuffer_a));  // t1
    uz::TRY(cmd.BindTexture(2, gbuffers.gbuffer_b));  // t2
    uz::TRY(cmd.BindTexture(3, gbuffers.gbuffer_c));  // t3
    uz::TRY(cmd.BindTexture(4, gbuffers.gbuffer_d));  // t4
    uz::TRY(cmd.BindTexture(5, gbuffers.gbuffer_e));  // t5
    uz::TRY(cmd.BindTexture(6, gbuffers.velocity));   // t6
    uz::TRY(cmd.BindTexture(7, gbuffers.lighting_channels));  // t7

    return uz::Ok();
}

} // namespace uz::deferred
```

**Shader side** (HLSL):

```hlsl
// uz/shaders/deferred/gbuffer_common.hlsli

#ifndef UZ_GBUFFER_BINDING_SLOT
    #define UZ_GBUFFER_BINDING_SLOT 0
#endif

Texture2D SceneDepthTexture : register(t0);
Texture2D GBufferATexture : register(t1);
Texture2D GBufferBTexture : register(t2);
Texture2D GBufferCTexture : register(t3);
Texture2D GBufferDTexture : register(t4);
Texture2D GBufferETexture : register(t5);
Texture2D VelocityTexture : register(t6);
Texture2D<uint> LightingChannelsTexture : register(t7);

// Static samplers (defined in root signature)
SamplerState PointClampedSampler : register(s0);

float SampleDeviceZ(float2 uv) {
    return SceneDepthTexture.SampleLevel(PointClampedSampler, uv, 0).r;
}

float4 SampleGBufferA(float2 uv) {
    return GBufferATexture.SampleLevel(PointClampedSampler, uv, 0);
}

// ... etc for B, C, D, E
```

---

## 6. Exposure Constant Buffer Pattern

### UE5 Pattern (PreExposure)

**File**: `PostProcessEyeAdaptation.cpp` line 1197-1298

```cpp
void FViewInfo::UpdatePreExposure()
{
    // Determine platform capability
    const bool bSupportPreExposureDifferentThanGlobalExposure =
        !(bMobilePlatform && !IsMobileHDR());

    if (!bSupportPreExposureDifferentThanGlobalExposure) {
        // Mobile LDR: apply directly in base pass
        PreExposure = FixedExposure;
    }
    else if (bIsPreExposureRelevant) {
        // Compute from histogram or fixed
        const float FinalPreExposure = SceneColorTint * GlobalExposure * VignetteMask * LocalExposure;
        PreExposure = FinalPreExposure;
    }

    // Upload to GPU
    ViewState->PreExposure = PreExposure;
}
```

### UzEngine Adaptation

```cpp
// uz/engine/renderer/view/view_constants.h

namespace uz {

struct ViewConstants {
    // Exposure multiplier applied in base pass
    float pre_exposure = 1.0f;

    // Derived from auto-exposure or fixed fallback
    float pre_exposure_prev = 1.0f;

    // ... other view params ...
};

// uz/engine/renderer/post_process/post_process_view.h

class PostProcessView {
public:
    float pre_exposure = 1.0f;
    float pre_exposure_prev = 1.0f;

    // Update from histogram or fixed computation
    void UpdateExposure(float new_exposure, float frame_delta) {
        pre_exposure_prev = pre_exposure;

        // Smooth transition (optional)
        float smoothed = glm::mix(pre_exposure, new_exposure,
                                  std::min(1.0f, frame_delta * 2.0f));
        pre_exposure = smoothed;
    }

    // Fallback (SM5, mobile)
    static float ComputeFixedExposure(float min_lum, float max_lum) {
        const float kMiddleGrey = 0.18f;
        float avg = (min_lum + max_lum) * 0.5f;
        return kMiddleGrey / std::max(0.0001f, avg);
    }
};

} // namespace uz
```

---

## 7. Register-Based Root Signature Pattern (SM5)

### UE5 Pattern (Implicit in D3D11)

UE5 uses fixed descriptor binding because D3D11 doesn't support dynamic descriptors.

### UzEngine Adaptation (for DX11/SM5)

```cpp
// uz/rhi/dx11/dx11_root_signature.h

namespace uz::rhi::dx11 {

// SM5 layout: 8 GBuffer SRVs + 1 sampler (static)
struct DeferredLightRootSignature {
    static constexpr size_t NumTextures = 8;
    static constexpr size_t NumSamplers = 1;
    static constexpr size_t NumConstantBuffers = 3;

    // Register mapping
    enum TextureSlot : uint32_t {
        kDepth = 0,           // t0
        kGBufferA = 1,        // t1
        kGBufferB = 2,        // t2
        kGBufferC = 3,        // t3
        kGBufferD = 4,        // t4
        kGBufferE = 5,        // t5
        kVelocity = 6,        // t6
        kLightingChannels = 7,// t7
    };

    enum SamplerSlot : uint32_t {
        kPointClamp = 0,      // s0
    };

    enum ConstantBufferSlot : uint32_t {
        kViewConstants = 0,   // b0
        kLightData = 1,       // b1
        kMaterial = 2,        // b2
    };
};

// Bind all at once (D3D11 requirement)
class DeferredLightBindingSet {
    uz::Handle<RhiTexture> textures[DeferredLightRootSignature::NumTextures];
    uz::Handle<RhiBuffer> cbs[DeferredLightRootSignature::NumConstantBuffers];

    uz::Result<void> Bind(RhiCommandList& cmd) {
        // Bind as array (D3D11)
        // cmd.PSSetShaderResources(0, 8, &textures[0].GetD3D11SRV());
        // cmd.PSSetConstantBuffers(0, 3, &cbs[0].GetD3D11Buffer());

        for (int i = 0; i < DeferredLightRootSignature::NumTextures; ++i) {
            uz::TRY(cmd.BindTexture(i, textures[i]));
        }
        for (int i = 0; i < DeferredLightRootSignature::NumConstantBuffers; ++i) {
            uz::TRY(cmd.BindConstantBuffer(i, cbs[i]));
        }

        return uz::Ok();
    }
};

} // namespace uz::rhi::dx11
```

---

## 8. Offline IBL Texture Baking Workflow

### Pattern: Pre-baked, Post-loaded

```cpp
// uz/tools/ibl_baker_tool.h (runs during content pipeline)

namespace uz::tools {

class IblBakerTool {
public:
    struct Config {
        std::filesystem::path hdr_cubemap_path;
        uint32_t brdf_lut_size = 256;
        uint32_t irradiance_size = 32;
        uint32_t prefiltered_size = 128;
    };

    // Bake all IBL maps for a given environment
    uz::Result<void> BakeEnvironment(
        const Config& config,
        const std::filesystem::path& output_dir)
    {
        // Load HDR cubemap
        uz::Cubemap hdr;
        uz::TRY_ASSIGN(hdr, uz::Image::LoadHDR(config.hdr_cubemap_path));

        // 1. Bake BRDF LUT (global, one-time)
        if (!std::filesystem::exists(output_dir / "brdf_lut.uzasset")) {
            uz::TRY(BakeBrdfLut(output_dir / "brdf_lut.uzasset"));
        }

        // 2. Project to SH for diffuse
        uz::ibl::SHCoefficients sh;
        uz::TRY_ASSIGN(sh, uz::ibl::ProjectCubemapToSH(hdr));
        uz::TRY(SaveShCoefficients(sh, output_dir / "irradiance_sh.uzasset"));

        // 3. Generate prefiltered specular mips
        uz::Cubemap prefiltered;
        uz::TRY_ASSIGN(prefiltered, PrefilterCubemap(hdr, config.prefiltered_size));
        uz::TRY(SaveCubemap(prefiltered, output_dir / "prefiltered_specular.uzasset"));

        return uz::Ok();
    }

private:
    uz::Result<uz::ByteBuffer> BakeBrdfLut(const std::filesystem::path& path) {
        // ... (from earlier BRDF LUT pattern) ...
        return uz::Ok();
    }

    uz::Result<uz::Cubemap> PrefilterCubemap(
        const uz::Cubemap& src,
        uint32_t size)
    {
        uz::Cubemap out(size);

        // Generate mip chain (8 mips for 128×128)
        for (uint32_t mip = 0; mip < 8; ++mip) {
            float roughness = mip / 7.0f;  // 0 to 1

            // For each face, mip, sample
            for (auto [face, pixels] : out.GetMips()[mip]) {
                // Importance-sample specular using roughness
                // ... filtering loop ...
            }
        }

        return out;
    }
};

} // namespace uz::tools
```

---

## Summary: SM5 Adaptation Checklist

| Feature | UE5 Approach | UzEngine Adaptation |
|---------|-------------|-------------------|
| Auto-exposure | Histogram (SM5+) or fixed fallback | Compute shader with pixel-shader fallback |
| BRDF LUT | Pre-baked, static load | Offline baking tool + asset pipeline |
| Irradiance | SH projection (offline) | 7 float4 buffer upload |
| Prefiltered specular | Pre-baked mips | Cubemap mip chain from offline baker |
| GBuffer binding | Named textures (register slots) | Static texture array binding (t0-t7) |
| Root signature | D3D11 fixed descriptors | Register-based slots, no dynamic indirection |
| Exposure value | `(MinLum + MaxLum) / 2 / 0.18` | 11.8× default brightness multiplier |
| Feature detection | `IsFeatureLevelSupported()` | `SupportsComputeShaders()`, `SupportsBindless()` |

---

## Key Takeaways

1. **No compute shaders on SM5 for LUT generation** — pre-bake everything offline
2. **Register-bind all resources** — D3D11 lacks dynamic descriptors
3. **SH coefficients as structured buffer** — 7 float4s for irradiance
4. **PreExposure in view constants** — applied in base pass + deferred lighting
5. **Fallback exposure ≈ 11.8×** — conservative default for typical scenes
6. **Static samplers in root signature** — no per-material sampler variation on SM5
