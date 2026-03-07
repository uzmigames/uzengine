// ---------------------------------------------------------------------------
// static_mesh_renderer.cpp -- PBR mesh rendering
// ---------------------------------------------------------------------------

#include "static_mesh_renderer.h"

#include <cstring>
#include "engine/core/diag.h"
#include "engine/renderer/resources/shader_library.h"

namespace uz {

StaticMeshRenderer::~StaticMeshRenderer() {
    shutdown();
}

bool StaticMeshRenderer::init(ShaderLibrary& shader_lib) {
    // PBR pipeline: POSITION(12) + NORMAL(12) + TEXCOORD(8) + TANGENT(16)
    RhiPipelineDesc desc{};
    desc.vertex_layout.attrib_count = 4;
    desc.vertex_layout.attribs[0] = {"POSITION", 0, RHI_VERTEX_FORMAT_FLOAT3,  0, 0};
    desc.vertex_layout.attribs[1] = {"NORMAL",   0, RHI_VERTEX_FORMAT_FLOAT3, 12, 0};
    desc.vertex_layout.attribs[2] = {"TEXCOORD", 0, RHI_VERTEX_FORMAT_FLOAT2, 24, 0};
    desc.vertex_layout.attribs[3] = {"TANGENT",  0, RHI_VERTEX_FORMAT_FLOAT4, 32, 0};

    desc.rt_count      = 1;
    desc.rt_formats[0] = RHI_FORMAT_RGBA8_UNORM;
    desc.ds_format     = RHI_FORMAT_D24_UNORM_S8_UINT;
    desc.topology      = RHI_TOPOLOGY_TRIANGLE_LIST;
    desc.rasterizer.fill_mode = RHI_FILL_SOLID;
    desc.rasterizer.cull_mode = RHI_CULL_BACK;
    desc.depth_stencil.depth_enable = true;
    desc.depth_stencil.depth_write  = true;
    desc.depth_stencil.depth_func   = RHI_COMPARE_LESS;
    desc.blend.render_targets[0].write_mask = 0xF;
    desc.debug_name = "PBR_PSO";

    pbrPipeline_ = shader_lib.get_or_create_pipeline("pbr", desc);
    if (pbrPipeline_ == RHI_NULL) {
        DIAG("StaticMeshRenderer: PBR pipeline creation failed");
        return false;
    }

    // Fallback textures.
    RhiTextureDesc td{};
    td.mip_levels = 1; td.array_size = 1;
    td.format    = RHI_FORMAT_RGBA8_UNORM;
    td.dimension = RHI_TEXTURE_DIM_2D;
    td.usage     = RHI_TEXTURE_USAGE_SHADER_READ | RHI_TEXTURE_USAGE_COPY_DST;

    // 1x1 white
    uint8_t white[4] = {255, 255, 255, 255};
    td.width = 1; td.height = 1; td.debug_name = "Fallback_White";
    whiteTex_ = rhi()->texture_create(&td);
    if (whiteTex_ != RHI_NULL) rhi()->texture_upload(whiteTex_, white, 4);

    // 1x1 black
    uint8_t black[4] = {0, 0, 0, 255};
    td.debug_name = "Fallback_Black";
    blackTex_ = rhi()->texture_create(&td);
    if (blackTex_ != RHI_NULL) rhi()->texture_upload(blackTex_, black, 4);

    // 1x1 flat normal (0.5, 0.5, 1.0) -> mapped to [0, 255]: 128, 128, 255
    uint8_t normal[4] = {128, 128, 255, 255};
    td.debug_name = "Fallback_Normal";
    normalTex_ = rhi()->texture_create(&td);
    if (normalTex_ != RHI_NULL) rhi()->texture_upload(normalTex_, normal, 4);

    DIAG("StaticMeshRenderer: init OK");
    return true;
}

void StaticMeshRenderer::add_instance(MeshInstance instance) {
    instances_.push_back(std::move(instance));
}

void StaticMeshRenderer::draw(RhiCmdList cmd, const Mat4& view_proj, RhiBuffer light_cbv) {
    if (pbrPipeline_ == RHI_NULL || instances_.empty()) return;

    rhi()->cmd_set_pipeline(cmd, pbrPipeline_);

    for (const auto& inst : instances_) {
        if (inst.mesh.vertex_buffer == RHI_NULL) continue;

        // Bind mesh.
        const uint32_t stride = inst.mesh.vertex_stride;
        rhi()->cmd_set_vertex_buffer(cmd, 0, inst.mesh.vertex_buffer, 0, stride);
        if (inst.mesh.index_buffer != RHI_NULL)
            rhi()->cmd_set_index_buffer(cmd, inst.mesh.index_buffer, 0, true);

        // Slot 0: World Matrix
        rhi()->cmd_set_root_constants(cmd, 0, &inst.transform, 16, 0);

        // Slot 1: MaterialCB (20 floats — must match pbr.ps.hlsl layout)
        float matCB[20] = {
            inst.albedo[0], inst.albedo[1], inst.albedo[2], inst.metallic,
            inst.roughness, inst.ao, 0.0f /*parallaxScale*/, inst.emission_energy,
            inst.emission[0], inst.emission[1], inst.emission[2], 1.0f /*envMul*/,
            1.0f /*normalStr*/, 1.0f /*specMul*/, 1.0f /*uvTileU*/, 1.0f /*uvTileV*/,
            1.0f /*selfShadowStr*/, 0.35f /*cavityStr*/, 0.0f, 0.0f
        };
        rhi()->cmd_set_root_constants(cmd, 1, matCB, 20, 0);

        // Slot 2: LightFrameCB (root CBV -- data uploaded once per frame by Renderer)
        if (light_cbv != RHI_NULL)
            rhi()->cmd_set_constant_buffer(cmd, 2, light_cbv);

        // Slot 3: ViewProj
        rhi()->cmd_set_root_constants(cmd, 3, &view_proj, 16, 0);

        // Textures
        rhi()->cmd_set_texture(cmd, 0, (inst.albedo_tex != RHI_NULL) ? inst.albedo_tex : whiteTex_);
        rhi()->cmd_set_texture(cmd, 1, (inst.normal_tex != RHI_NULL) ? inst.normal_tex : normalTex_);
        rhi()->cmd_set_texture(cmd, 2, (inst.metallic_tex != RHI_NULL) ? inst.metallic_tex : whiteTex_);
        rhi()->cmd_set_texture(cmd, 3, (inst.roughness_tex != RHI_NULL) ? inst.roughness_tex : whiteTex_);
        rhi()->cmd_set_texture(cmd, 4, (inst.ao_tex != RHI_NULL) ? inst.ao_tex : whiteTex_);
        // Slot 5 (emission_tex) removed — root signature Phase 1 only supports t0-t4

        // Draw.
        if (inst.mesh.index_buffer != RHI_NULL)
            rhi()->cmd_draw_indexed(cmd, inst.mesh.index_count, 1, 0, 0, 0);
        else
            rhi()->cmd_draw(cmd, inst.mesh.vertex_count, 1, 0, 0);
    }
}

void StaticMeshRenderer::shutdown() {
    for (auto& inst : instances_)
        destroy_gpu_mesh(inst.mesh);
    instances_.clear();

    if (whiteTex_ != RHI_NULL) { rhi()->texture_destroy(whiteTex_); whiteTex_ = RHI_NULL; }
    if (blackTex_ != RHI_NULL) { rhi()->texture_destroy(blackTex_); blackTex_ = RHI_NULL; }
    if (normalTex_ != RHI_NULL) { rhi()->texture_destroy(normalTex_); normalTex_ = RHI_NULL; }
    
    pbrPipeline_ = RHI_NULL; // Owned by ShaderLibrary.
}

} // namespace uz
