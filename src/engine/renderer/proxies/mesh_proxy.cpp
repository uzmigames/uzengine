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

    cached_albedo_tex       = RHI_NULL;
    cached_normal_tex       = RHI_NULL;
    cached_metallic_tex     = RHI_NULL;
    cached_roughness_tex    = RHI_NULL;
    cached_ao_tex           = RHI_NULL;
    cached_emission_tex     = RHI_NULL;
    cached_displacement_tex = RHI_NULL;
    cached_parallax_scale   = 0.0f;
    cached_env_multiplier   = 1.0f;
    cached_normal_strength  = 1.0f;
    cached_specular_multiplier = 1.0f;
    cached_uv_tiling[0]     = 1.0f;
    cached_uv_tiling[1]     = 1.0f;
    cached_self_shadow_strength = 1.0f;
    cached_cavity_strength  = 0.35f;

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
    get_tex("displacement_texture", cached_displacement_tex);
    get_float("parallax_scale", cached_parallax_scale);
    get_float("env_multiplier", cached_env_multiplier);
    get_float("normal_strength", cached_normal_strength);
    get_float("specular_multiplier", cached_specular_multiplier);
    get_float("self_shadow_strength", cached_self_shadow_strength);
    get_float("cavity_strength", cached_cavity_strength);

    // UV tiling: single float sets both U and V; vec2 sets independently
    auto tiling_it = material->parameters.find("uv_tiling");
    if (tiling_it != material->parameters.end()) {
        cached_uv_tiling[0] = tiling_it->second;
        cached_uv_tiling[1] = tiling_it->second;
    }
    auto tiling_vec_it = material->vec_parameters.find("uv_tiling");
    if (tiling_vec_it != material->vec_parameters.end() && tiling_vec_it->second.size() >= 2) {
        cached_uv_tiling[0] = tiling_vec_it->second[0];
        cached_uv_tiling[1] = tiling_vec_it->second[1];
    }
}

void MeshProxy::execute(RhiCmdList cmd, const FrameConstants& fc) {
    if (!mesh || mesh->vertex_buffer == RHI_NULL) return;

    // 1. Bind Mesh
    rhi()->cmd_set_vertex_buffer(cmd, 0, mesh->vertex_buffer, 0, mesh->vertex_stride);
    rhi()->cmd_set_index_buffer(cmd, mesh->index_buffer, 0, true);

    // 2. Slot b0: World Matrix
    rhi()->cmd_set_root_constants(cmd, 0, &transform, 16, 0);

    // 3. Slot b1: MaterialCB (20 DWORDs — fills root sig to 64/64)
    float matCB[20] = {
        cached_albedo[0], cached_albedo[1], cached_albedo[2], cached_metallic,
        cached_roughness, cached_ao, cached_parallax_scale, cached_emission_energy,
        cached_emission[0], cached_emission[1], cached_emission[2], cached_env_multiplier,
        cached_normal_strength, cached_specular_multiplier, cached_uv_tiling[0], cached_uv_tiling[1],
        cached_self_shadow_strength, cached_cavity_strength, 0.0f, 0.0f  // pad to 20
    };
    rhi()->cmd_set_root_constants(cmd, 1, matCB, 20, 0);

    // 4. Slot b2: LightFrameCB (root CBV -- data uploaded once per frame by Renderer)
    if (fc.light_frame_cbv != RHI_NULL)
        rhi()->cmd_set_constant_buffer(cmd, 2, fc.light_frame_cbv);

    // 5. Slot b3: FrameCB
    rhi()->cmd_set_root_constants(cmd, 3, &fc.view_proj, 16, 0);

    // 6. Textures t0-t9
    rhi()->cmd_set_texture(cmd, 0, (cached_albedo_tex != RHI_NULL) ? cached_albedo_tex : fc.white_texture);
    rhi()->cmd_set_texture(cmd, 1, (cached_normal_tex != RHI_NULL) ? cached_normal_tex : fc.normal_texture);
    rhi()->cmd_set_texture(cmd, 2, (cached_metallic_tex != RHI_NULL) ? cached_metallic_tex : fc.white_texture);
    rhi()->cmd_set_texture(cmd, 3, (cached_roughness_tex != RHI_NULL) ? cached_roughness_tex : fc.white_texture);
    rhi()->cmd_set_texture(cmd, 4, (cached_ao_tex != RHI_NULL) ? cached_ao_tex : fc.white_texture);
    rhi()->cmd_set_texture(cmd, 5, (cached_displacement_tex != RHI_NULL) ? cached_displacement_tex : fc.black_texture);
    rhi()->cmd_set_texture(cmd, 6, (fc.shadow_map != RHI_NULL) ? fc.shadow_map : fc.white_texture);
    rhi()->cmd_set_texture(cmd, 7, (fc.brdf_lut != RHI_NULL) ? fc.brdf_lut : fc.white_texture);
    rhi()->cmd_set_texture(cmd, 8, (fc.irradiance_cube != RHI_NULL) ? fc.irradiance_cube : fc.white_texture);
    rhi()->cmd_set_texture(cmd, 9, (fc.prefiltered_cube != RHI_NULL) ? fc.prefiltered_cube : fc.white_texture);

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