#pragma once

#include "engine/renderer/scene/scene_proxy.h"
#include "engine/renderer/resources/mesh.h"
#include "engine/renderer/resources/material.h"
#include "engine/renderer/resources/material_instance.h"

namespace uz {

// ============================================================================
// MaterialSlot — cached PBR parameters + GPU CBV for one material assignment.
//
// Each submesh references a slot via SubMesh::material_index.
// ============================================================================

struct MaterialSlot {
    Material*         material = nullptr;
    MaterialInstance* instance = nullptr;

    // Cached PBR scalar values.
    float albedo[3]            = {0.8f, 0.8f, 0.8f};
    float metallic             = 0.0f;
    float roughness            = 0.5f;
    float ao                   = 1.0f;
    float emission[3]          = {0.0f, 0.0f, 0.0f};
    float emission_energy      = 1.0f;
    float parallax_scale       = 0.0f;
    float env_multiplier       = 1.0f;
    float normal_strength      = 1.0f;
    float specular_multiplier  = 1.0f;
    float uv_tiling[2]        = {1.0f, 1.0f};
    float self_shadow_strength = 1.0f;
    float cavity_strength      = 0.35f;
    float anisotropy           = 0.0f;
    float anisotropy_rotation  = 0.0f;

    // Cached texture handles.
    RhiTexture albedo_tex       = RHI_NULL;
    RhiTexture normal_tex       = RHI_NULL;
    RhiTexture metallic_tex     = RHI_NULL;
    RhiTexture roughness_tex    = RHI_NULL;
    RhiTexture ao_tex           = RHI_NULL;
    RhiTexture emission_tex     = RHI_NULL;
    RhiTexture displacement_tex = RHI_NULL;

    // GPU constant buffer for MaterialCB (b1 root CBV, 256 bytes upload heap).
    RhiBuffer  cbv              = RHI_NULL;
};

// ============================================================================
// MeshProxy — Render Thread representation of a StaticMeshComponent.
//
// Supports multi-material meshes: each SubMesh::material_index maps into
// material_slots_. If only one material is used, material_slots_ has one entry.
// ============================================================================

struct MeshProxy : public ISceneProxy {
    MeshProxy() = default;
    ~MeshProxy() override;

    // ISceneProxy implementation
    void setup(RgPassBuilder& builder) override;
    void execute(RhiCmdList cmd, const FrameConstants& fc) override;
    [[nodiscard]] uz::Aabb bounds() const override { return bounds_; }
    [[nodiscard]] uint32_t render_flags() const override { return render_flags_; }
    void sync_from_component(const void* component_data) override;

    /// Build cached material data for all slots.
    /// Call after populating material_slots_.
    void cache_materials();

    // Proxy-local data
    GpuMesh*          mesh              = nullptr;
    Mat4              transform         = Mat4::identity();
    uz::Aabb          bounds_           = uz::Aabb::empty();
    uint32_t          render_flags_     = RENDER_LAYER_OPAQUE;
    uint32_t          node_id           = 0;

    // Multi-material slots. Index matches SubMesh::material_index.
    // Slot 0 is the "primary" material (backward compatible with single-material).
    std::vector<MaterialSlot> material_slots_;

private:
    /// Cache a single slot from its Material/MaterialInstance/DefaultMaterialSlot.
    void cache_slot(MaterialSlot& slot, const DefaultMaterialSlot* defaults);

    /// Upload MaterialCB to GPU for a single slot.
    void upload_slot_cbv(MaterialSlot& slot);
};

} // namespace uz
