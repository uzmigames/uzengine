#pragma once

#include <string>
#include "engine/core/component_interface.h"
#include "engine/renderer/resources/material.h"
#include "engine/renderer/resources/mesh.h"

namespace uz {

// ============================================================================
// StaticMeshComponent — Game Thread component for rendering 3D meshes.
// ============================================================================

struct StaticMeshComponent : public IComponent, public IRenderable {
    static constexpr const char* kTypeName = "StaticMesh";

    StaticMeshComponent() = default;
    virtual ~StaticMeshComponent() = default;

    // IComponent implementation
    [[nodiscard]] StringId type_id() const override { return string_id(kTypeName); }
    [[nodiscard]] const char* type_name() const override { return kTypeName; }

    // IRenderable implementation
    [[nodiscard]] IRenderable* as_renderable() override { return this; }
    [[nodiscard]] ISceneProxy* create_proxy() override;

    // Component data
    std::string mesh_path;
    std::string material_path;
    
    // Loaded resources (optional, can be resolved by the proxy)
    GpuMesh*  mesh = nullptr;
    Material* material = nullptr;
};

} // namespace uz
