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
    // PBR pipeline: POSITION + NORMAL + TEXCOORD.
    RhiPipelineDesc desc{};
    desc.vertex_layout.attrib_count = 3;
    desc.vertex_layout.attribs[0] = {"POSITION", 0, RHI_VERTEX_FORMAT_FLOAT3,  0, 0};
    desc.vertex_layout.attribs[1] = {"NORMAL",   0, RHI_VERTEX_FORMAT_FLOAT3, 12, 0};
    desc.vertex_layout.attribs[2] = {"TEXCOORD", 0, RHI_VERTEX_FORMAT_FLOAT2, 24, 0};

    desc.rt_count      = 1;
    desc.rt_formats[0] = RHI_FORMAT_RGBA8_UNORM;
    desc.ds_format     = RHI_FORMAT_D32_FLOAT;
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

    // 1x1 white fallback texture.
    uint8_t white[4] = {255, 255, 255, 255};
    RhiTextureDesc td{};
    td.width = 1; td.height = 1; td.depth = 1;
    td.mip_levels = 1; td.array_size = 1;
    td.format    = RHI_FORMAT_RGBA8_UNORM;
    td.dimension = RHI_TEXTURE_DIM_2D;
    td.usage     = RHI_TEXTURE_USAGE_SHADER_READ | RHI_TEXTURE_USAGE_COPY_DST;
    td.debug_name = "WhiteFallback";
    whiteTex_ = rhi()->texture_create(&td);
    if (whiteTex_ != RHI_NULL)
        rhi()->texture_upload(whiteTex_, white, 4);

    DIAG("StaticMeshRenderer: init OK");
    return true;
}

void StaticMeshRenderer::add_instance(MeshInstance instance) {
    instances_.push_back(std::move(instance));
}

void StaticMeshRenderer::draw(RhiCmdList cmd, const Mat4& view_proj) {
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

        // Slot 1: Material
        float matCB[8] = {
            inst.albedo[0], inst.albedo[1], inst.albedo[2], inst.metallic,
            inst.roughness, 1.0f, 0.0f, 0.0f // rough, ao, pad, pad
        };
        rhi()->cmd_set_root_constants(cmd, 1, matCB, 8, 0);

        // Slot 2: Light + Camera (partial data since we don't have FrameConstants here)
        float lightCamCB[16] = {
            0.3f, -0.8f, 0.5f,  3.0f,  // dir, intensity
            1.0f, 0.98f, 0.95f, 0.0f,  // color, pad
            0.0f, 0.0f,  0.0f,  0.0f,  // camPos (unknown)
            0.0f, 0.0f,  0.0f,  0.0f   // time, pad
        };
        rhi()->cmd_set_root_constants(cmd, 2, lightCamCB, 16, 0);

        // Slot 3: ViewProj
        rhi()->cmd_set_root_constants(cmd, 3, &view_proj, 16, 0);

        // Slot 4: Albedo Texture (t0)
        RhiTexture tex = (inst.albedo_tex != RHI_NULL) ? inst.albedo_tex : whiteTex_;
        rhi()->cmd_set_texture(cmd, 0, tex);
        
        // Slots 5-8: Dummy textures for Normal, Metallic, Rough, AO
        for (int i = 1; i < 5; i++) {
            rhi()->cmd_set_texture(cmd, i, whiteTex_);
        }

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

    if (whiteTex_ != RHI_NULL) {
        rhi()->texture_destroy(whiteTex_);
        whiteTex_ = RHI_NULL;
    }
    pbrPipeline_ = RHI_NULL; // Owned by ShaderLibrary.
}

} // namespace uz
