// ---------------------------------------------------------------------------
// shader_library.cpp -- Named shader registry with PSO caching
//
// RULE CS-7:  No exceptions — returns RHI_NULL on failure
// RULE RHI-1: No backend types above rhi.h
// ---------------------------------------------------------------------------

#include "shader_library.h"

#include <algorithm>
#include <cstring>

#include "engine/core/diag.h"

// Embedded shader sources (generated at build-time from .hlsl files).
#include "shaders/basic_vs_hlsl.h"
#include "shaders/basic_ps_hlsl.h"
#include "shaders/pbr_vs_hlsl.h"
#include "shaders/pbr_ps_hlsl.h"
#include "shaders/sky_vs_hlsl.h"
#include "shaders/sky_ps_hlsl.h"
#include "shaders/shadow_depth_vs_hlsl.h"
#include "shaders/fullscreen_vs_hlsl.h"
#include "shaders/ssao_ps_hlsl.h"
#include "shaders/ssao_blur_ps_hlsl.h"
#include "shaders/bloom_down_ps_hlsl.h"
#include "shaders/bloom_up_ps_hlsl.h"
#include "shaders/composite_ps_hlsl.h"
#include "shaders/velocity_ps_hlsl.h"
#include "shaders/taa_ps_hlsl.h"
#include "shaders/gtao_ps_hlsl.h"
#include "shaders/gbuffer_write_ps_hlsl.h"
#include "shaders/deferred_lighting_ps_hlsl.h"
#include "shaders/auto_exposure_ps_hlsl.h"
#include "shaders/hiz_generate_ps_hlsl.h"
#include "shaders/ssr_ps_hlsl.h"
#include "shaders/color_grading_ps_hlsl.h"
#include "shaders/dof_coc_ps_hlsl.h"
#include "shaders/dof_blur_ps_hlsl.h"
#include "shaders/volumetric_apply_ps_hlsl.h"
#include "shaders/sss_blur_ps_hlsl.h"
#include "shaders/decal_ps_hlsl.h"
#include "shaders/clouds_ps_hlsl.h"
#include "shaders/pick_vs_hlsl.h"
#include "shaders/pick_ps_hlsl.h"

namespace uz {

// ===========================================================================
// Lifecycle
// ===========================================================================

ShaderLibrary::~ShaderLibrary() {
    shutdown();
}

void ShaderLibrary::shutdown() {
    for (auto& [name, entry] : shaders_) {
        for (auto& [hash, pso] : entry.pipelines) {
            if (pso != RHI_NULL) {
                rhi()->pipeline_destroy(pso);
            }
        }
        entry.pipelines.clear();
    }
}

// ===========================================================================
// Registration
// ===========================================================================

void ShaderLibrary::register_builtin_shaders() {
    // basic — editor geometry (cubes, grid, gizmos)
    register_shader("basic", ShaderProgram{
        .vs_source = k_basic_vs_hlsl,
        .vs_size   = sizeof(k_basic_vs_hlsl) - 1,
        .ps_source = k_basic_ps_hlsl,
        .ps_size   = sizeof(k_basic_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // pbr — PBR lit rendering (Cook-Torrance BRDF)
    register_shader("pbr", ShaderProgram{
        .vs_source = k_pbr_vs_hlsl,
        .vs_size   = sizeof(k_pbr_vs_hlsl) - 1,
        .ps_source = k_pbr_ps_hlsl,
        .ps_size   = sizeof(k_pbr_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // pick — GPU pick buffer (node ID as RGB)
    register_shader("pick", ShaderProgram{
        .vs_source = k_pick_vs_hlsl,
        .vs_size   = sizeof(k_pick_vs_hlsl) - 1,
        .ps_source = k_pick_ps_hlsl,
        .ps_size   = sizeof(k_pick_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // shadow_depth — depth-only pass for directional shadow map (VS only, no PS)
    register_shader("shadow_depth", ShaderProgram{
        .vs_source = k_shadow_depth_vs_hlsl,
        .vs_size   = sizeof(k_shadow_depth_vs_hlsl) - 1,
        .ps_source = nullptr,
        .ps_size   = 0,
        .vs_entry  = "VSMain",
        .ps_entry  = nullptr,
    });

    // sky — fullscreen atmospheric gradient sky (no vertex buffer)
    register_shader("sky", ShaderProgram{
        .vs_source = k_sky_vs_hlsl,
        .vs_size   = sizeof(k_sky_vs_hlsl) - 1,
        .ps_source = k_sky_ps_hlsl,
        .ps_size   = sizeof(k_sky_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // ssao — screen-space ambient occlusion (fullscreen VS + SSAO PS)
    register_shader("ssao", ShaderProgram{
        .vs_source = k_fullscreen_vs_hlsl,
        .vs_size   = sizeof(k_fullscreen_vs_hlsl) - 1,
        .ps_source = k_ssao_ps_hlsl,
        .ps_size   = sizeof(k_ssao_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // ssao_blur — bilateral blur for SSAO
    register_shader("ssao_blur", ShaderProgram{
        .vs_source = k_fullscreen_vs_hlsl,
        .vs_size   = sizeof(k_fullscreen_vs_hlsl) - 1,
        .ps_source = k_ssao_blur_ps_hlsl,
        .ps_size   = sizeof(k_ssao_blur_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // bloom_down — bloom downsample (13-tap + Karis)
    register_shader("bloom_down", ShaderProgram{
        .vs_source = k_fullscreen_vs_hlsl,
        .vs_size   = sizeof(k_fullscreen_vs_hlsl) - 1,
        .ps_source = k_bloom_down_ps_hlsl,
        .ps_size   = sizeof(k_bloom_down_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // bloom_up — bloom upsample (tent filter)
    register_shader("bloom_up", ShaderProgram{
        .vs_source = k_fullscreen_vs_hlsl,
        .vs_size   = sizeof(k_fullscreen_vs_hlsl) - 1,
        .ps_source = k_bloom_up_ps_hlsl,
        .ps_size   = sizeof(k_bloom_up_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // composite — final post-process (SSAO + bloom + tonemap + gamma)
    register_shader("composite", ShaderProgram{
        .vs_source = k_fullscreen_vs_hlsl,
        .vs_size   = sizeof(k_fullscreen_vs_hlsl) - 1,
        .ps_source = k_composite_ps_hlsl,
        .ps_size   = sizeof(k_composite_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // velocity — depth-based per-pixel motion vectors (TAA/GTAO/SSR)
    register_shader("velocity", ShaderProgram{
        .vs_source = k_fullscreen_vs_hlsl,
        .vs_size   = sizeof(k_fullscreen_vs_hlsl) - 1,
        .ps_source = k_velocity_ps_hlsl,
        .ps_size   = sizeof(k_velocity_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // taa — temporal anti-aliasing resolve (reproject + YCoCg clamp + blend)
    register_shader("taa", ShaderProgram{
        .vs_source = k_fullscreen_vs_hlsl,
        .vs_size   = sizeof(k_fullscreen_vs_hlsl) - 1,
        .ps_source = k_taa_ps_hlsl,
        .ps_size   = sizeof(k_taa_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // gtao — ground truth ambient occlusion (replaces hemisphere SSAO)
    register_shader("gtao", ShaderProgram{
        .vs_source = k_fullscreen_vs_hlsl,
        .vs_size   = sizeof(k_fullscreen_vs_hlsl) - 1,
        .ps_source = k_gtao_ps_hlsl,
        .ps_size   = sizeof(k_gtao_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // gbuffer_write — G-Buffer write pass (deferred shading, 4 MRT outputs)
    register_shader("gbuffer_write", ShaderProgram{
        .vs_source = k_pbr_vs_hlsl,
        .vs_size   = sizeof(k_pbr_vs_hlsl) - 1,
        .ps_source = k_gbuffer_write_ps_hlsl,
        .ps_size   = sizeof(k_gbuffer_write_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // deferred_lighting — Fullscreen deferred lighting pass (reads G-Buffer)
    register_shader("deferred_lighting", ShaderProgram{
        .vs_source = k_fullscreen_vs_hlsl,
        .vs_size   = sizeof(k_fullscreen_vs_hlsl) - 1,
        .ps_source = k_deferred_lighting_ps_hlsl,
        .ps_size   = sizeof(k_deferred_lighting_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // auto_exposure — scene luminance metering + temporal adaptation
    register_shader("auto_exposure", ShaderProgram{
        .vs_source = k_fullscreen_vs_hlsl,
        .vs_size   = sizeof(k_fullscreen_vs_hlsl) - 1,
        .ps_source = k_auto_exposure_ps_hlsl,
        .ps_size   = sizeof(k_auto_exposure_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // hiz_generate — Hi-Z depth mip chain (min reduction)
    register_shader("hiz_generate", ShaderProgram{
        .vs_source = k_fullscreen_vs_hlsl,
        .vs_size   = sizeof(k_fullscreen_vs_hlsl) - 1,
        .ps_source = k_hiz_generate_ps_hlsl,
        .ps_size   = sizeof(k_hiz_generate_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // ssr — screen-space reflections (Hi-Z ray march)
    register_shader("ssr", ShaderProgram{
        .vs_source = k_fullscreen_vs_hlsl,
        .vs_size   = sizeof(k_fullscreen_vs_hlsl) - 1,
        .ps_source = k_ssr_ps_hlsl,
        .ps_size   = sizeof(k_ssr_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // color_grading — 3D LUT + temp/tint/contrast/saturation
    register_shader("color_grading", ShaderProgram{
        .vs_source = k_fullscreen_vs_hlsl,
        .vs_size   = sizeof(k_fullscreen_vs_hlsl) - 1,
        .ps_source = k_color_grading_ps_hlsl,
        .ps_size   = sizeof(k_color_grading_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // dof_coc — circle of confusion from depth
    register_shader("dof_coc", ShaderProgram{
        .vs_source = k_fullscreen_vs_hlsl,
        .vs_size   = sizeof(k_fullscreen_vs_hlsl) - 1,
        .ps_source = k_dof_coc_ps_hlsl,
        .ps_size   = sizeof(k_dof_coc_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // dof_blur — separable DoF Gaussian blur
    register_shader("dof_blur", ShaderProgram{
        .vs_source = k_fullscreen_vs_hlsl,
        .vs_size   = sizeof(k_fullscreen_vs_hlsl) - 1,
        .ps_source = k_dof_blur_ps_hlsl,
        .ps_size   = sizeof(k_dof_blur_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // volumetric_apply — height fog + inscattering application
    register_shader("volumetric_apply", ShaderProgram{
        .vs_source = k_fullscreen_vs_hlsl,
        .vs_size   = sizeof(k_fullscreen_vs_hlsl) - 1,
        .ps_source = k_volumetric_apply_ps_hlsl,
        .ps_size   = sizeof(k_volumetric_apply_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // sss_blur — separable subsurface scattering blur
    register_shader("sss_blur", ShaderProgram{
        .vs_source = k_fullscreen_vs_hlsl,
        .vs_size   = sizeof(k_fullscreen_vs_hlsl) - 1,
        .ps_source = k_sss_blur_ps_hlsl,
        .ps_size   = sizeof(k_sss_blur_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // decal — screen-space decal projection
    register_shader("decal", ShaderProgram{
        .vs_source = k_fullscreen_vs_hlsl,
        .vs_size   = sizeof(k_fullscreen_vs_hlsl) - 1,
        .ps_source = k_decal_ps_hlsl,
        .ps_size   = sizeof(k_decal_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    // clouds — volumetric cloud raymarching (Beer-Lambert + HG phase + procedural noise)
    register_shader("clouds", ShaderProgram{
        .vs_source = k_fullscreen_vs_hlsl,
        .vs_size   = sizeof(k_fullscreen_vs_hlsl) - 1,
        .ps_source = k_clouds_ps_hlsl,
        .ps_size   = sizeof(k_clouds_ps_hlsl) - 1,
        .vs_entry  = "VSMain",
        .ps_entry  = "PSMain",
    });

    DIAG_FMT("ShaderLibrary: registered %zu builtin shaders", shaders_.size());
}

void ShaderLibrary::register_shader(const std::string& name,
                                     const ShaderProgram& program) {
    shaders_[name] = Entry{program, {}};
}

// ===========================================================================
// Lookup
// ===========================================================================

const ShaderProgram* ShaderLibrary::get(const std::string& name) const {
    auto it = shaders_.find(name);
    if (it == shaders_.end()) return nullptr;
    return &it->second.program;
}

std::vector<std::string> ShaderLibrary::list() const {
    std::vector<std::string> result;
    result.reserve(shaders_.size());
    for (const auto& [name, _] : shaders_) {
        result.push_back(name);
    }
    std::sort(result.begin(), result.end());
    return result;
}

// ===========================================================================
// Pipeline creation + caching
// ===========================================================================

uint64_t ShaderLibrary::hash_pipeline_desc(const RhiPipelineDesc& desc) {
    // Hash everything in the desc EXCEPT the shader fields (vs/ps source).
    // This ensures same shader + different rasterizer state = different PSO.
    uint64_t h = 0;

    // Vertex layout
    h ^= rhi_hash_bytes(&desc.vertex_layout,
                         sizeof(RhiVertexLayout));

    // Render target formats
    h ^= rhi_hash_bytes(desc.rt_formats,
                         sizeof(RhiFormat) * desc.rt_count);
    h ^= static_cast<uint64_t>(desc.rt_count) * 0x9e3779b97f4a7c15ULL;

    // Depth format
    h ^= static_cast<uint64_t>(desc.ds_format) * 0x517cc1b727220a95ULL;

    // Rasterizer state
    h ^= rhi_hash_bytes(&desc.rasterizer, sizeof(RhiRasterizerState));

    // Blend state
    h ^= rhi_hash_bytes(&desc.blend, sizeof(RhiBlendState));

    // Depth-stencil state
    h ^= rhi_hash_bytes(&desc.depth_stencil, sizeof(RhiDepthStencilState));

    // Topology
    h ^= static_cast<uint64_t>(desc.topology) * 0xd6e8feb86659fd93ULL;

    return h;
}

RhiPipeline ShaderLibrary::get_or_create_pipeline(const std::string& shader_name,
                                                    RhiPipelineDesc desc) {
    auto it = shaders_.find(shader_name);
    if (it == shaders_.end()) {
        DIAG_FMT("ShaderLibrary: unknown shader '%s'", shader_name.c_str());
        return RHI_NULL;
    }

    Entry& entry = it->second;

    // Fill shader fields from the registered program.
    desc.vertex_shader.spirv_data  = entry.program.vs_source;
    desc.vertex_shader.spirv_size  = entry.program.vs_size;
    desc.vertex_shader.entry_point = entry.program.vs_entry;
    desc.pixel_shader.spirv_data   = entry.program.ps_source;
    desc.pixel_shader.spirv_size   = entry.program.ps_size;
    desc.pixel_shader.entry_point  = entry.program.ps_entry;

    // Check cache.
    const uint64_t key = hash_pipeline_desc(desc);
    auto cached = entry.pipelines.find(key);
    if (cached != entry.pipelines.end() && cached->second != RHI_NULL) {
        return cached->second;
    }

    // Create new PSO.
    DIAG_FMT("ShaderLibrary: creating PSO for '%s' (hash=%llx)",
             shader_name.c_str(), (unsigned long long)key);
    RhiPipeline pso = rhi()->pipeline_create(&desc);
    if (pso == RHI_NULL) {
        DIAG_FMT("ShaderLibrary: pipeline_create FAILED for '%s'",
                 shader_name.c_str());
        return RHI_NULL;
    }

    entry.pipelines[key] = pso;
    return pso;
}

} // namespace uz
