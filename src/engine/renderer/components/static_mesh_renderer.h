#pragma once

// static_mesh_renderer.h -- Renders static meshes with PBR materials.
//
// Manages a collection of mesh instances, each with its own transform,
// material params, and optional texture. Draws using the PBR pipeline.

#include <vector>
#include <string>
#include "engine/core/math.h"
#include "engine/renderer/rhi/rhi.h"
#include "engine/renderer/resources/mesh.h"

namespace uz {

class ShaderLibrary;

// A renderable mesh instance with material properties.
struct MeshInstance {
    std::string name;
    Mat4        transform = Mat4::identity();
    GpuMesh     mesh;

    // PBR material params.
    float albedo[3]  = {0.8f, 0.8f, 0.8f};
    float metallic   = 0.0f;
    float roughness  = 0.5f;

    // Optional albedo texture (RHI_NULL = use solid albedo color).
    RhiTexture albedo_tex = RHI_NULL;
};

class StaticMeshRenderer {
public:
    StaticMeshRenderer() = default;
    ~StaticMeshRenderer();

    /// Initialize: create PBR pipeline and fallback texture.
    bool init(ShaderLibrary& shader_lib);

    /// Add a mesh instance. Takes ownership of the GpuMesh handles.
    void add_instance(MeshInstance instance);

    /// Draw all instances.
    void draw(RhiCmdList cmd, const Mat4& view_proj);

    /// Get mutable instances for scene setup.
    std::vector<MeshInstance>& instances() { return instances_; }

    /// Release all GPU resources.
    void shutdown();

private:
    RhiPipeline pbrPipeline_ = RHI_NULL;
    RhiTexture  whiteTex_    = RHI_NULL;
    std::vector<MeshInstance> instances_;
};

} // namespace uz
