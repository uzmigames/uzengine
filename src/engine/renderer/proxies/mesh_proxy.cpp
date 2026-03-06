#include "engine/renderer/proxies/mesh_proxy.h"
#include "engine/renderer/resources/shader_library.h"
#include "engine/renderer/core/renderer.h"
#include "engine/renderer/components/static_mesh_component.h"

namespace uz {

void MeshProxy::setup(RgPassBuilder& builder) {
    (void)builder;
}

void MeshProxy::cache_material() {
    // Extract values from Material's unordered_maps ONCE so that
    // execute() never touches a hash-map.
    cached_albedo[0] = 0.8f; cached_albedo[1] = 0.8f; cached_albedo[2] = 0.8f;
    cached_metallic  = 0.0f;
    cached_roughness = 0.5f;
    cached_albedo_tex = RHI_NULL;

    if (!material) {
        fprintf(stderr, "[MeshProxy] cache_material: NO MATERIAL\n");
        return;
    }

    auto it_alb = material->vec_parameters.find("albedo");
    if (it_alb != material->vec_parameters.end() && it_alb->second.size() >= 3) {
        cached_albedo[0] = it_alb->second[0];
        cached_albedo[1] = it_alb->second[1];
        cached_albedo[2] = it_alb->second[2];
    }

    auto it_met = material->parameters.find("metallic");
    if (it_met != material->parameters.end()) {
        cached_metallic = it_met->second;
    }

    auto it_rou = material->parameters.find("roughness");
    if (it_rou != material->parameters.end()) {
        cached_roughness = it_rou->second;
    }

    auto it_tex = material->texture_handles.find("albedo_texture");
    if (it_tex != material->texture_handles.end()) {
        cached_albedo_tex = it_tex->second;
    }
}

void MeshProxy::execute(RhiCmdList cmd, const FrameConstants& fc) {
    if (!mesh || mesh->vertex_buffer == RHI_NULL) return;

    // 1. Bind Mesh
    rhi()->cmd_set_vertex_buffer(cmd, 0, mesh->vertex_buffer, 0, mesh->vertex_stride);
    rhi()->cmd_set_index_buffer(cmd, mesh->index_buffer, 0, true);

    // 2. Slot b0: World Matrix (16 DWORDs) — raw transform, NOT pre-multiplied
    rhi()->cmd_set_root_constants(cmd, 0, &transform, 16, 0);

    // 3. Slot b1: MaterialCB (8 DWORDs) — albedo(3) + metallic + roughness + ao + pad(2)
    float matCB[8] = {
        cached_albedo[0], cached_albedo[1], cached_albedo[2], cached_metallic,
        cached_roughness, 1.0f, 0.0f, 0.0f  // roughness, ao, pad, pad
    };
    rhi()->cmd_set_root_constants(cmd, 1, matCB, 8, 0);

    // 4. Slot b2: LightCamCB (16 DWORDs) — dir(3)+intensity + color(3)+pad + camPos(3)+time
    float lightCamCB[16] = {
        fc.light_dir.x, fc.light_dir.y, fc.light_dir.z, fc.light_intensity,
        fc.light_color.x, fc.light_color.y, fc.light_color.z, 0.0f,
        fc.camera_world_pos.x, fc.camera_world_pos.y, fc.camera_world_pos.z, fc.time,
        0.0f, 0.0f, 0.0f, 0.0f
    };
    rhi()->cmd_set_root_constants(cmd, 2, lightCamCB, 16, 0);

    // 5. Slot b3: FrameCB (16 DWORDs) — viewProj matrix
    rhi()->cmd_set_root_constants(cmd, 3, &fc.view_proj, 16, 0);

    // 6. Textures t0-t4: Albedo + fallback white for Normal/Metallic/Roughness/AO
    RhiTexture albedoTex = (cached_albedo_tex != RHI_NULL)
                               ? cached_albedo_tex : fc.white_texture;
    rhi()->cmd_set_texture(cmd, 0, albedoTex);
    for (int i = 1; i < 5; i++) {
        rhi()->cmd_set_texture(cmd, i, fc.white_texture);
    }

    // 7. Draw Submeshes
    for (const auto& sub : mesh->submeshes) {
        rhi()->cmd_draw_indexed(cmd, sub.index_count, 1, sub.index_offset, 0, 0);
    }
}

void MeshProxy::sync_from_component(const void* component_data) {
    const auto* comp = static_cast<const StaticMeshComponent*>(component_data);
    if (!comp) return;

    mesh         = comp->mesh;
    material     = comp->material;
    cache_material();
}

} // namespace uz
