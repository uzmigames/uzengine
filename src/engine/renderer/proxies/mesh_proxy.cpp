#include "engine/renderer/proxies/mesh_proxy.h"
#include "engine/renderer/resources/shader_library.h"
#include "engine/renderer/core/renderer.h"
#include "engine/renderer/components/static_mesh_component.h"

namespace uz {

void MeshProxy::setup(RgPassBuilder& builder) {
    (void)builder;
}

void MeshProxy::cache_material() {
    // Default values
    cached_albedo[0] = 0.8f; cached_albedo[1] = 0.8f; cached_albedo[2] = 0.8f;
    cached_metallic  = 0.0f;
    cached_roughness = 0.5f;
    cached_ao        = 1.0f;
    cached_emission[0] = 0.0f; cached_emission[1] = 0.0f; cached_emission[2] = 0.0f;
    cached_emission_energy = 1.0f;

    cached_albedo_tex    = RHI_NULL;
    cached_normal_tex    = RHI_NULL;
    cached_metallic_tex  = RHI_NULL;
    cached_roughness_tex = RHI_NULL;
    cached_ao_tex        = RHI_NULL;
    cached_emission_tex  = RHI_NULL;

    if (!material) return;

    // Helper for float params
    auto get_float = [&](const char* name, float& val) {
        auto it = material->parameters.find(name);
        if (it != material->parameters.end()) val = it->second;
    };

    // Helper for vec3 params
    auto get_vec3 = [&](const char* name, float* val) {
        auto it = material->vec_parameters.find(name);
        if (it != material->vec_parameters.end() && it->second.size() >= 3) {
            val[0] = it->second[0];
            val[1] = it->second[1];
            val[2] = it->second[2];
        }
    };

    // Helper for textures
    auto get_tex = [&](const char* name, RhiTexture& val) {
        auto it = material->texture_handles.find(name);
        if (it != material->texture_handles.end()) val = it->second;
    };

    get_vec3("albedo", cached_albedo);
    get_float("metallic", cached_metallic);
    get_float("roughness", cached_roughness);
    get_float("ao", cached_ao);
    get_vec3("emission", cached_emission);
    get_float("emission_energy", cached_emission_energy);

    get_tex("albedo_texture", cached_albedo_tex);
    get_tex("normal_texture", cached_normal_tex);
    get_tex("metallic_texture", cached_metallic_tex);
    get_tex("roughness_texture", cached_roughness_tex);
    get_tex("ao_texture", cached_ao_tex);
    get_tex("emission_texture", cached_emission_tex);
}

void MeshProxy::execute(RhiCmdList cmd, const FrameConstants& fc) {
    if (!mesh || mesh->vertex_buffer == RHI_NULL) return;

    // 1. Bind Mesh
    rhi()->cmd_set_vertex_buffer(cmd, 0, mesh->vertex_buffer, 0, mesh->vertex_stride);
    rhi()->cmd_set_index_buffer(cmd, mesh->index_buffer, 0, true);

    // 2. Slot b0: World Matrix
    rhi()->cmd_set_root_constants(cmd, 0, &transform, 16, 0);

    // 3. Slot b1: MaterialCB (12 floats)
    float matCB[12] = {
        cached_albedo[0], cached_albedo[1], cached_albedo[2], 1.0f,
        cached_metallic, cached_roughness, cached_ao, cached_emission_energy,
        cached_emission[0], cached_emission[1], cached_emission[2], 0.0f
    };
    rhi()->cmd_set_root_constants(cmd, 1, matCB, 12, 0);

    // 4. Slot b2: LightFrameCB (102 floats)
    float lightFrameCB[128] = { 0 }; 
    // Light 0: Directional light from FrameConstants
    lightFrameCB[0] = 0.0f; lightFrameCB[1] = 0.0f; lightFrameCB[2] = 0.0f; // pos
    lightFrameCB[3] = fc.light_intensity;
    lightFrameCB[4] = fc.light_color.x; lightFrameCB[5] = fc.light_color.y; lightFrameCB[6] = fc.light_color.z;
    lightFrameCB[7] = 0.0f; // range
    lightFrameCB[8] = fc.light_dir.x; lightFrameCB[9] = fc.light_dir.y; lightFrameCB[10] = fc.light_dir.z;
    lightFrameCB[11] = 0.0f; // type (0=Dir)
    
    uint32_t lightCount = 1;
    memcpy(&lightFrameCB[96], &lightCount, 4);
    memcpy(&lightFrameCB[97], &fc.camera_world_pos, 12);
    memcpy(&lightFrameCB[100], &fc.time, 4);
    float envInt = 1.0f;
    memcpy(&lightFrameCB[101], &envInt, 4);

    rhi()->cmd_set_root_constants(cmd, 2, lightFrameCB, 102, 0);

    // 5. Slot b3: FrameCB
    rhi()->cmd_set_root_constants(cmd, 3, &fc.view_proj, 16, 0);

    // 6. Textures t0-t5
    rhi()->cmd_set_texture(cmd, 0, (cached_albedo_tex != RHI_NULL) ? cached_albedo_tex : fc.white_texture);
    rhi()->cmd_set_texture(cmd, 1, (cached_normal_tex != RHI_NULL) ? cached_normal_tex : fc.normal_texture);
    rhi()->cmd_set_texture(cmd, 2, (cached_metallic_tex != RHI_NULL) ? cached_metallic_tex : fc.black_texture);
    rhi()->cmd_set_texture(cmd, 3, (cached_roughness_tex != RHI_NULL) ? cached_roughness_tex : fc.white_texture);
    rhi()->cmd_set_texture(cmd, 4, (cached_ao_tex != RHI_NULL) ? cached_ao_tex : fc.white_texture);
    // Slot 5 (emission_tex) removed — root signature Phase 1 only supports t0-t4

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