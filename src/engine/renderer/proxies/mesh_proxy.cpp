#include "engine/renderer/proxies/mesh_proxy.h"
#include "engine/renderer/resources/shader_library.h"
#include "engine/renderer/resources/graph_material_loader.h"
#include "engine/renderer/core/renderer.h"
#include "engine/renderer/components/static_mesh_component.h"

#include <cstring>

namespace uz {

MeshProxy::~MeshProxy() {
    for (auto& slot : material_slots_) {
        if (slot.cbv != RHI_NULL) {
            rhi()->buffer_destroy(slot.cbv);
            slot.cbv = RHI_NULL;
        }
    }
}

void MeshProxy::setup(RgPassBuilder& builder) {
    (void)builder;
}

// ---------------------------------------------------------------------------
// cache_slot — resolve one MaterialSlot from instance/material/defaults.
// ---------------------------------------------------------------------------

void MeshProxy::cache_slot(MaterialSlot& slot, const DefaultMaterialSlot* defaults) {
    // 1. Start with engine defaults.
    slot.albedo[0] = 0.8f; slot.albedo[1] = 0.8f; slot.albedo[2] = 0.8f;
    slot.metallic  = 0.0f;
    slot.roughness = 0.5f;
    slot.ao        = 1.0f;
    slot.emission[0] = 0.0f; slot.emission[1] = 0.0f; slot.emission[2] = 0.0f;
    slot.emission_energy = 1.0f;

    slot.albedo_tex       = RHI_NULL;
    slot.normal_tex       = RHI_NULL;
    slot.metallic_tex     = RHI_NULL;
    slot.roughness_tex    = RHI_NULL;
    slot.ao_tex           = RHI_NULL;
    slot.emission_tex     = RHI_NULL;
    slot.displacement_tex = RHI_NULL;
    slot.parallax_scale   = 0.0f;
    slot.env_multiplier   = 1.0f;
    slot.normal_strength  = 1.0f;
    slot.specular_multiplier = 1.0f;
    slot.uv_tiling[0]     = 1.0f;
    slot.uv_tiling[1]     = 1.0f;
    slot.self_shadow_strength = 1.0f;
    slot.cavity_strength  = 0.35f;

    // 2. Apply mesh default materials (from glTF import).
    if (defaults) {
        slot.albedo[0] = defaults->albedo[0];
        slot.albedo[1] = defaults->albedo[1];
        slot.albedo[2] = defaults->albedo[2];
        slot.metallic  = defaults->metallic;
        slot.roughness = defaults->roughness;
        slot.emission[0] = defaults->emission[0];
        slot.emission[1] = defaults->emission[1];
        slot.emission[2] = defaults->emission[2];
        slot.emission_energy = defaults->emission_energy;

        // Queue textures from mesh defaults (async load).
        if (!defaults->albedo_texture.empty())
            slot.albedo_tex = texture_get_or_queue(defaults->albedo_texture);
        if (!defaults->normal_texture.empty())
            slot.normal_tex = texture_get_or_queue(defaults->normal_texture);
        if (!defaults->metallic_roughness_texture.empty()) {
            // glTF packs metallic (B) + roughness (G) in one texture.
            // We bind to both slots — the shader samples the correct channel.
            slot.metallic_tex  = texture_get_or_queue(defaults->metallic_roughness_texture);
            slot.roughness_tex = texture_get_or_queue(defaults->metallic_roughness_texture);
        }
        if (!defaults->ao_texture.empty())
            slot.ao_tex = texture_get_or_queue(defaults->ao_texture);
        if (!defaults->emission_texture.empty())
            slot.emission_tex = texture_get_or_queue(defaults->emission_texture);
    }

    // 3. MaterialInstance path: override -> parent fallback.
    if (slot.instance) {
        auto* mi = slot.instance;

        auto get_float = [&](const char* name, float def) -> float {
            return mi->get_scalar(name, def);
        };

        auto get_vec3 = [&](const char* name, float* val) {
            const auto* v = mi->get_vector(name);
            if (v && v->size() >= 3) {
                val[0] = (*v)[0]; val[1] = (*v)[1]; val[2] = (*v)[2];
            }
        };

        auto get_tex = [&](const char* name) -> RhiTexture {
            return mi->get_texture(name);
        };

        get_vec3("albedo", slot.albedo);
        slot.metallic       = get_float("metallic", slot.metallic);
        slot.roughness      = get_float("roughness", slot.roughness);
        slot.ao             = get_float("ao", slot.ao);
        get_vec3("emission", slot.emission);
        slot.emission_energy    = get_float("emission_energy", slot.emission_energy);

        RhiTexture t;
        t = get_tex("albedo_texture");       if (t != RHI_NULL) slot.albedo_tex = t;
        t = get_tex("normal_texture");       if (t != RHI_NULL) slot.normal_tex = t;
        t = get_tex("metallic_texture");     if (t != RHI_NULL) slot.metallic_tex = t;
        t = get_tex("roughness_texture");    if (t != RHI_NULL) slot.roughness_tex = t;
        t = get_tex("ao_texture");           if (t != RHI_NULL) slot.ao_tex = t;
        t = get_tex("emission_texture");     if (t != RHI_NULL) slot.emission_tex = t;
        t = get_tex("displacement_texture"); if (t != RHI_NULL) slot.displacement_tex = t;
        slot.parallax_scale      = get_float("parallax_scale", slot.parallax_scale);
        slot.env_multiplier      = get_float("env_multiplier", slot.env_multiplier);
        slot.normal_strength     = get_float("normal_strength", slot.normal_strength);
        slot.specular_multiplier = get_float("specular_multiplier", slot.specular_multiplier);
        slot.self_shadow_strength = get_float("self_shadow_strength", slot.self_shadow_strength);
        slot.cavity_strength     = get_float("cavity_strength", slot.cavity_strength);
        slot.anisotropy          = get_float("anisotropy", slot.anisotropy);
        slot.anisotropy_rotation = get_float("anisotropy_rotation", slot.anisotropy_rotation);

        // UV tiling
        slot.uv_tiling[0] = get_float("uv_tiling", slot.uv_tiling[0]);
        slot.uv_tiling[1] = slot.uv_tiling[0];
        const auto* tv = mi->get_vector("uv_tiling");
        if (tv && tv->size() >= 2) {
            slot.uv_tiling[0] = (*tv)[0];
            slot.uv_tiling[1] = (*tv)[1];
        }
    }
    // 4. Raw Material path (no instance) — overrides mesh defaults.
    else if (slot.material) {
        auto get_float = [&](const char* name, float& val) {
            auto it = slot.material->parameters.find(name);
            if (it != slot.material->parameters.end()) val = it->second;
        };

        auto get_vec3 = [&](const char* name, float* val) {
            auto it = slot.material->vec_parameters.find(name);
            if (it != slot.material->vec_parameters.end() && it->second.size() >= 3) {
                val[0] = it->second[0];
                val[1] = it->second[1];
                val[2] = it->second[2];
            }
        };

        auto get_tex = [&](const char* name, RhiTexture& val) {
            auto it = slot.material->texture_handles.find(name);
            if (it != slot.material->texture_handles.end()) val = it->second;
        };

        get_vec3("albedo", slot.albedo);
        get_float("metallic", slot.metallic);
        get_float("roughness", slot.roughness);
        get_float("ao", slot.ao);
        get_vec3("emission", slot.emission);
        get_float("emission_energy", slot.emission_energy);

        get_tex("albedo_texture", slot.albedo_tex);
        get_tex("normal_texture", slot.normal_tex);
        get_tex("metallic_texture", slot.metallic_tex);
        get_tex("roughness_texture", slot.roughness_tex);
        get_tex("ao_texture", slot.ao_tex);
        get_tex("emission_texture", slot.emission_tex);
        get_tex("displacement_texture", slot.displacement_tex);
        get_float("parallax_scale", slot.parallax_scale);
        get_float("env_multiplier", slot.env_multiplier);
        get_float("normal_strength", slot.normal_strength);
        get_float("specular_multiplier", slot.specular_multiplier);
        get_float("self_shadow_strength", slot.self_shadow_strength);
        get_float("cavity_strength", slot.cavity_strength);
        get_float("anisotropy", slot.anisotropy);
        get_float("anisotropy_rotation", slot.anisotropy_rotation);

        // UV tiling: single float sets both U and V; vec2 sets independently
        auto tiling_it = slot.material->parameters.find("uv_tiling");
        if (tiling_it != slot.material->parameters.end()) {
            slot.uv_tiling[0] = tiling_it->second;
            slot.uv_tiling[1] = tiling_it->second;
        }
        auto tiling_vec_it = slot.material->vec_parameters.find("uv_tiling");
        if (tiling_vec_it != slot.material->vec_parameters.end() && tiling_vec_it->second.size() >= 2) {
            slot.uv_tiling[0] = tiling_vec_it->second[0];
            slot.uv_tiling[1] = tiling_vec_it->second[1];
        }
    }
    // else: use defaults from mesh or engine defaults.

    upload_slot_cbv(slot);
}

// ---------------------------------------------------------------------------
// upload_slot_cbv — upload MaterialCB to GPU for a single slot.
// ---------------------------------------------------------------------------

void MeshProxy::upload_slot_cbv(MaterialSlot& slot) {
    if (slot.cbv == RHI_NULL) {
        RhiBufferDesc cbvDesc{};
        cbvDesc.size       = 256;
        cbvDesc.usage      = RHI_BUFFER_USAGE_CONSTANT | RHI_BUFFER_USAGE_UPLOAD;
        cbvDesc.debug_name = "MaterialCBV";
        slot.cbv = rhi()->buffer_create(&cbvDesc);
    }

    // Graph material: fill cbuffer from compiled layout instead of PBR layout.
    if (slot.material && slot.material->graph_compiled) {
        alignas(16) uint8_t buf[256] = {};
        slot.material->graph_compiled->fill_cbuffer(*slot.material, buf, 256);
        if (slot.cbv != RHI_NULL)
            rhi()->buffer_upload(slot.cbv, buf, 256);
        return;
    }

    // PBR layout (20 floats = 80 bytes).
    float matCB[20] = {
        slot.albedo[0], slot.albedo[1], slot.albedo[2], slot.metallic,
        slot.roughness, slot.ao, slot.parallax_scale, slot.emission_energy,
        slot.emission[0], slot.emission[1], slot.emission[2], slot.env_multiplier,
        slot.normal_strength, slot.specular_multiplier, slot.uv_tiling[0], slot.uv_tiling[1],
        slot.self_shadow_strength, slot.cavity_strength, slot.anisotropy, slot.anisotropy_rotation
    };
    if (slot.cbv != RHI_NULL)
        rhi()->buffer_upload(slot.cbv, matCB, sizeof(matCB));
}

// ---------------------------------------------------------------------------
// cache_materials — build all material slots.
// ---------------------------------------------------------------------------

void MeshProxy::cache_materials() {
    // Determine slot count from mesh submeshes.
    uint32_t max_slot = 0;
    if (mesh) {
        for (const auto& sub : mesh->submeshes) {
            if (sub.material_index > max_slot) max_slot = sub.material_index;
        }
    }

    uint32_t slot_count = max_slot + 1;

    // Ensure we have enough slots.
    if (material_slots_.size() < slot_count) {
        material_slots_.resize(slot_count);
    }

    // If no slots at all, create one default.
    if (material_slots_.empty()) {
        material_slots_.resize(1);
    }

    // Cache each slot.
    for (uint32_t i = 0; i < material_slots_.size(); ++i) {
        const DefaultMaterialSlot* defaults = nullptr;
        if (mesh && i < mesh->default_materials.size()) {
            defaults = &mesh->default_materials[i];
        }
        cache_slot(material_slots_[i], defaults);
    }
}

// ---------------------------------------------------------------------------
// execute — draw all submeshes, rebinding material per slot.
// ---------------------------------------------------------------------------

void MeshProxy::execute(RhiCmdList cmd, const FrameConstants& fc) {
    if (!mesh || mesh->vertex_buffer == RHI_NULL) return;

    // 1. Bind Mesh
    rhi()->cmd_set_vertex_buffer(cmd, 0, mesh->vertex_buffer, 0, mesh->vertex_stride);
    rhi()->cmd_set_index_buffer(cmd, mesh->index_buffer, 0, true);

    // 2. Slot b0: World Matrix
    rhi()->cmd_set_root_constants(cmd, 0, &transform, 16, 0);

    // 3. Slot b2: LightFrameCB (root CBV -- data uploaded once per frame by Renderer)
    if (fc.light_frame_cbv != RHI_NULL)
        rhi()->cmd_set_constant_buffer(cmd, 2, fc.light_frame_cbv);

    // 4. Slot b3: FrameCB
    rhi()->cmd_set_root_constants(cmd, 3, &fc.view_proj, 16, 0);

    // 5. Draw each submesh with its own material slot.
    uint32_t last_bound_slot = UINT32_MAX;

    for (const auto& sub : mesh->submeshes) {
        uint32_t slot_idx = sub.material_index;
        if (slot_idx >= material_slots_.size()) slot_idx = 0;

        // Only rebind material when the slot changes.
        if (slot_idx != last_bound_slot) {
            const auto& slot = material_slots_[slot_idx];

            // b1: MaterialCB
            if (slot.cbv != RHI_NULL)
                rhi()->cmd_set_constant_buffer(cmd, 1, slot.cbv);

            // Textures t0-t9
            rhi()->cmd_set_texture(cmd, 0, (slot.albedo_tex != RHI_NULL)       ? slot.albedo_tex       : fc.white_texture);
            rhi()->cmd_set_texture(cmd, 1, (slot.normal_tex != RHI_NULL)       ? slot.normal_tex       : fc.normal_texture);
            rhi()->cmd_set_texture(cmd, 2, (slot.metallic_tex != RHI_NULL)     ? slot.metallic_tex     : fc.white_texture);
            rhi()->cmd_set_texture(cmd, 3, (slot.roughness_tex != RHI_NULL)    ? slot.roughness_tex    : fc.white_texture);
            rhi()->cmd_set_texture(cmd, 4, (slot.ao_tex != RHI_NULL)           ? slot.ao_tex           : fc.white_texture);
            rhi()->cmd_set_texture(cmd, 5, (slot.displacement_tex != RHI_NULL) ? slot.displacement_tex : fc.black_texture);
            rhi()->cmd_set_texture(cmd, 6, (fc.shadow_map != RHI_NULL)         ? fc.shadow_map         : fc.white_texture);
            rhi()->cmd_set_texture(cmd, 7, (fc.brdf_lut != RHI_NULL)           ? fc.brdf_lut           : fc.white_texture);
            rhi()->cmd_set_texture(cmd, 8, (fc.irradiance_cube != RHI_NULL)    ? fc.irradiance_cube    : fc.white_texture);
            rhi()->cmd_set_texture(cmd, 9, (fc.prefiltered_cube != RHI_NULL)   ? fc.prefiltered_cube   : fc.white_texture);

            last_bound_slot = slot_idx;
        }

        rhi()->cmd_draw_indexed(cmd, sub.index_count, 1, sub.index_offset, 0, 0);
    }
}

void MeshProxy::sync_from_component(const void* component_data) {
    const auto* comp = static_cast<const StaticMeshComponent*>(component_data);
    if (!comp) return;

    mesh = comp->mesh;

    // Set slot 0 material from the component.
    if (material_slots_.empty()) material_slots_.resize(1);
    material_slots_[0].material = comp->material;

    cache_materials();
}

} // namespace uz
