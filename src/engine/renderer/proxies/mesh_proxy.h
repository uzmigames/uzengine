#pragma once

#include "engine/renderer/scene/scene_proxy.h"
#include "engine/renderer/resources/mesh.h"
#include "engine/renderer/resources/material.h"

namespace uz {

// ============================================================================
// MeshProxy — Render Thread representation of a StaticMeshComponent.
// ============================================================================

struct MeshProxy : public ISceneProxy {
    MeshProxy() = default;
    virtual ~MeshProxy() = default;

    // ISceneProxy implementation
    void setup(RgPassBuilder& builder) override;
    void execute(RhiCmdList cmd, const FrameConstants& fc) override;
    [[nodiscard]] uz::Aabb bounds() const override { return bounds_; }
    [[nodiscard]] uint32_t render_flags() const override { return render_flags_; }
    void sync_from_component(const void* component_data) override;

    /// Pre-cache flat material values from the Material* maps.
    /// Call once after setting `material`.
    void cache_material();

    // Proxy-local data
    GpuMesh*  mesh      = nullptr;
    Material* material  = nullptr;
    Mat4      transform = Mat4::identity();
    uz::Aabb  bounds_    = uz::Aabb::empty();
    uint32_t  render_flags_ = RENDER_LAYER_OPAQUE;

    // Pre-cached material values (set once via cache_material()).
    float cached_albedo[3]    = {0.8f, 0.8f, 0.8f};
    float cached_metallic     = 0.0f;
    float cached_roughness    = 0.5f;
    RhiTexture cached_albedo_tex = RHI_NULL;
};

} // namespace uz
