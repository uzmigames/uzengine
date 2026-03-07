// ibl_baker.cpp -- GPU compute IBL baking pipeline.
//
// Uses compute shaders to generate IBL cubemaps on the GPU,
// replacing the CPU-side sky_cubemap.cpp pipeline.

#include "ibl_baker.h"
#include "engine/core/diag.h"

// Auto-generated embedded shader headers (cmake embed_shaders.cmake)
#include "shaders/ibl_brdf_lut_cs_hlsl.h"
#include "shaders/ibl_irradiance_cs_hlsl.h"
#include "shaders/ibl_prefilter_cs_hlsl.h"

namespace uz {

// ---------------------------------------------------------------------------
// Constants matching the compute shaders
// ---------------------------------------------------------------------------

static constexpr uint32_t kBrdfLutSize    = 256;
static constexpr uint32_t kIrrSize        = 32;
static constexpr uint32_t kPrefiltSize    = 128;
static constexpr uint32_t kPrefiltMips    = 5;   // 128, 64, 32, 16, 8

// ---------------------------------------------------------------------------
// init / shutdown
// ---------------------------------------------------------------------------

bool IblBaker::init() {
    if (initialized_) return true;

    // BRDF LUT compute PSO
    {
        RhiComputePipelineDesc desc{};
        desc.compute_shader.spirv_data  = k_ibl_brdf_lut_cs_hlsl;
        desc.compute_shader.spirv_size  = sizeof(k_ibl_brdf_lut_cs_hlsl) - 1;
        desc.compute_shader.entry_point = "CSMain";
        desc.debug_name = "IBL_BRDF_LUT_CS";
        brdfLutPSO_ = rhi()->compute_pipeline_create(&desc);
        if (brdfLutPSO_ == RHI_NULL) {
            DIAG("WARNING: Failed to create BRDF LUT compute PSO");
            return false;
        }
    }

    // Irradiance convolution compute PSO
    {
        RhiComputePipelineDesc desc{};
        desc.compute_shader.spirv_data  = k_ibl_irradiance_cs_hlsl;
        desc.compute_shader.spirv_size  = sizeof(k_ibl_irradiance_cs_hlsl) - 1;
        desc.compute_shader.entry_point = "CSMain";
        desc.debug_name = "IBL_Irradiance_CS";
        irradiancePSO_ = rhi()->compute_pipeline_create(&desc);
        if (irradiancePSO_ == RHI_NULL) {
            DIAG("WARNING: Failed to create irradiance compute PSO");
            return false;
        }
    }

    // Prefilter GGX compute PSO
    {
        RhiComputePipelineDesc desc{};
        desc.compute_shader.spirv_data  = k_ibl_prefilter_cs_hlsl;
        desc.compute_shader.spirv_size  = sizeof(k_ibl_prefilter_cs_hlsl) - 1;
        desc.compute_shader.entry_point = "CSMain";
        desc.debug_name = "IBL_Prefilter_CS";
        prefilterPSO_ = rhi()->compute_pipeline_create(&desc);
        if (prefilterPSO_ == RHI_NULL) {
            DIAG("WARNING: Failed to create prefilter compute PSO");
            return false;
        }
    }

    initialized_ = true;
    DIAG("IblBaker: initialized (3 compute PSOs created)");
    return true;
}

void IblBaker::shutdown() {
    if (brdfLutPSO_ != RHI_NULL)    rhi()->pipeline_destroy(brdfLutPSO_);
    if (irradiancePSO_ != RHI_NULL) rhi()->pipeline_destroy(irradiancePSO_);
    if (prefilterPSO_ != RHI_NULL)  rhi()->pipeline_destroy(prefilterPSO_);
    brdfLutPSO_ = irradiancePSO_ = prefilterPSO_ = RHI_NULL;
    initialized_ = false;
}

// ---------------------------------------------------------------------------
// Dispatch helpers
// ---------------------------------------------------------------------------

static uint32_t div_ceil(uint32_t x, uint32_t y) {
    return (x + y - 1) / y;
}

// ---------------------------------------------------------------------------
// bake_brdf_lut — environment-independent, call once
// ---------------------------------------------------------------------------

RhiTexture IblBaker::bake_brdf_lut() {
    if (brdfLutPSO_ == RHI_NULL) return RHI_NULL;

    // Create output texture (RG16F, UAV + SRV)
    RhiTextureDesc texDesc{};
    texDesc.width      = kBrdfLutSize;
    texDesc.height     = kBrdfLutSize;
    texDesc.depth      = 1;
    texDesc.mip_levels = 1;
    texDesc.array_size = 1;
    texDesc.format     = RHI_FORMAT_RG16_FLOAT;
    texDesc.dimension  = RHI_TEXTURE_DIM_2D;
    texDesc.usage      = RHI_TEXTURE_USAGE_SHADER_READ | RHI_TEXTURE_USAGE_UNORDERED;
    texDesc.debug_name = "BRDF_LUT_Compute";

    RhiTexture output = rhi()->texture_create(&texDesc);
    if (output == RHI_NULL) return RHI_NULL;

    // Dispatch compute shader
    RhiCmdList cmd = rhi()->cmd_begin();
    if (cmd == RHI_NULL) {
        rhi()->texture_destroy(output);
        return RHI_NULL;
    }

    // Transition output to UAV state
    rhi()->cmd_barrier_texture(cmd, output,
        RHI_STATE_COMMON, RHI_STATE_UNORDERED_ACCESS);

    rhi()->cmd_set_pipeline(cmd, brdfLutPSO_);

    // Root constants: g_LutSize
    uint32_t constants[4] = { kBrdfLutSize, 0, 0, 0 };
    rhi()->cmd_set_root_constants(cmd, 0, constants, 4, 0);

    // Bind output UAV at u0 (mip 0)
    rhi()->cmd_set_uav(cmd, 0, output, 0);

    // Dispatch (LUT_SIZE/8, LUT_SIZE/8, 1)
    rhi()->cmd_dispatch(cmd, div_ceil(kBrdfLutSize, 8),
                             div_ceil(kBrdfLutSize, 8), 1);

    // Transition back to SRV for sampling
    rhi()->cmd_barrier_texture(cmd, output,
        RHI_STATE_UNORDERED_ACCESS, RHI_STATE_SHADER_READ);

    rhi()->cmd_end(cmd);
    rhi()->cmd_submit(cmd);

    DIAG("IblBaker: BRDF LUT baked (256x256 RG16F, GPU compute)");
    return output;
}

// ---------------------------------------------------------------------------
// bake — irradiance + prefiltered from source environment cubemap
// ---------------------------------------------------------------------------

IblBakeResult IblBaker::bake(RhiTexture env_cubemap, uint32_t env_size) {
    IblBakeResult result{};
    if (!initialized_ || env_cubemap == RHI_NULL) return result;

    // Create irradiance output cubemap (32x32, 6 faces, RGBA16F)
    {
        RhiTextureDesc desc{};
        desc.width      = kIrrSize;
        desc.height     = kIrrSize;
        desc.depth      = 1;
        desc.mip_levels = 1;
        desc.array_size = 6;
        desc.format     = RHI_FORMAT_RGBA16_FLOAT;
        desc.dimension  = RHI_TEXTURE_DIM_CUBE;
        desc.usage      = RHI_TEXTURE_USAGE_SHADER_READ | RHI_TEXTURE_USAGE_UNORDERED;
        desc.debug_name = "Irradiance_Compute";
        result.irradiance_cube = rhi()->texture_create(&desc);
    }

    // Create prefiltered output cubemap (128x128, 5 mips, 6 faces, RGBA16F)
    {
        RhiTextureDesc desc{};
        desc.width      = kPrefiltSize;
        desc.height     = kPrefiltSize;
        desc.depth      = 1;
        desc.mip_levels = kPrefiltMips;
        desc.array_size = 6;
        desc.format     = RHI_FORMAT_RGBA16_FLOAT;
        desc.dimension  = RHI_TEXTURE_DIM_CUBE;
        desc.usage      = RHI_TEXTURE_USAGE_SHADER_READ | RHI_TEXTURE_USAGE_UNORDERED;
        desc.debug_name = "Prefiltered_Compute";
        result.prefiltered_cube = rhi()->texture_create(&desc);
    }

    if (result.irradiance_cube == RHI_NULL || result.prefiltered_cube == RHI_NULL)
        return result;

    RhiCmdList cmd = rhi()->cmd_begin();
    if (cmd == RHI_NULL) return result;

    // --- Irradiance convolution ---
    rhi()->cmd_barrier_texture(cmd, result.irradiance_cube,
        RHI_STATE_COMMON, RHI_STATE_UNORDERED_ACCESS);

    rhi()->cmd_set_pipeline(cmd, irradiancePSO_);

    uint32_t irrConstants[4] = { kIrrSize, 0, 0, 0 };
    rhi()->cmd_set_root_constants(cmd, 0, irrConstants, 4, 0);
    rhi()->cmd_set_texture(cmd, 0, env_cubemap);          // t0 = source cubemap
    rhi()->cmd_set_uav(cmd, 0, result.irradiance_cube, 0); // u0 = output mip 0

    rhi()->cmd_dispatch(cmd, div_ceil(kIrrSize, 8),
                             div_ceil(kIrrSize, 8), 6);

    rhi()->cmd_barrier_texture(cmd, result.irradiance_cube,
        RHI_STATE_UNORDERED_ACCESS, RHI_STATE_SHADER_READ);

    // --- Prefiltered environment (one dispatch per mip level) ---
    rhi()->cmd_barrier_texture(cmd, result.prefiltered_cube,
        RHI_STATE_COMMON, RHI_STATE_UNORDERED_ACCESS);

    rhi()->cmd_set_pipeline(cmd, prefilterPSO_);
    rhi()->cmd_set_texture(cmd, 0, env_cubemap);  // t0 = source cubemap

    for (uint32_t mip = 0; mip < kPrefiltMips; ++mip) {
        uint32_t mipSize = kPrefiltSize >> mip;
        float roughness = static_cast<float>(mip) / static_cast<float>(kPrefiltMips - 1);

        struct { uint32_t outputSize; float roughness; uint32_t envSize; uint32_t pad; } cb;
        cb.outputSize = mipSize;
        cb.roughness  = roughness;
        cb.envSize    = env_size;
        cb.pad        = 0;
        rhi()->cmd_set_root_constants(cmd, 0, &cb, 4, 0);
        rhi()->cmd_set_uav(cmd, 0, result.prefiltered_cube, mip);

        rhi()->cmd_dispatch(cmd, div_ceil(mipSize, 8),
                                 div_ceil(mipSize, 8), 6);
    }

    rhi()->cmd_barrier_texture(cmd, result.prefiltered_cube,
        RHI_STATE_UNORDERED_ACCESS, RHI_STATE_SHADER_READ);

    rhi()->cmd_end(cmd);
    rhi()->cmd_submit(cmd);

    DIAG("IblBaker: irradiance + prefiltered cubemaps baked (GPU compute)");
    return result;
}

} // namespace uz
