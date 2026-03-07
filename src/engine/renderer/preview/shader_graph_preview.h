// shader_graph_preview.h -- Preview environment for shader graph live editing.
//
// Creates an in-memory preview scene with a procedural mesh (sphere/cube/plane),
// orbit camera, and IBL lighting. Receives compiled HLSL from the shader graph
// system and applies it as a dynamic material.
//
// This class depends on the renderer (ShaderLibrary, MaterialInstance, RHI).
// It is created and owned by the Renderer when shader graph editing starts.

#pragma once

#include "engine/renderer/resources/material_instance.h"
#include "engine/renderer/resources/mesh_primitives.h"
#include "engine/renderer/components/camera_component.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations to avoid pulling in full renderer headers.
namespace uz {
class ShaderLibrary;
class ViewportRegistry;
class StaticMeshRenderer;
struct GpuMesh;
} // namespace uz

namespace uz::sg {

enum class PreviewMesh : uint8_t {
    Sphere = 0,
    Cube,
    Plane,
};

struct PreviewConfig {
    uint32_t width       = 1280;
    uint32_t height      = 720;
    uint32_t target_fps  = 60;
    uint8_t  jpeg_quality = 85;
    PreviewMesh mesh     = PreviewMesh::Sphere;
    bool auto_rotate     = true;
};

class ShaderGraphPreview {
public:
    ShaderGraphPreview() = default;
    ~ShaderGraphPreview();

    // Lifecycle.
    bool init(const PreviewConfig& config,
              ShaderLibrary& shader_lib,
              ViewportRegistry& viewport_reg);
    void shutdown();
    bool is_active() const { return active_; }

    // Per-frame update: advances camera, returns view_proj matrix.
    CameraState update(float dt, float aspect);

    // Mesh selection.
    void set_preview_mesh(PreviewMesh mesh);
    PreviewMesh current_mesh() const { return config_.mesh; }

    // Camera control.
    void set_auto_rotate(bool enabled) { config_.auto_rotate = enabled; }
    void set_camera_orbit(float yaw, float pitch, float distance);

    // Shader application: receive compiled HLSL, create PSO, apply to preview.
    // Returns true if the shader was applied successfully.
    // On failure, keeps the previous (fallback) shader active.
    bool apply_compiled_shader(const std::string& vertex_hlsl,
                               const std::string& pixel_hlsl);

    // Material parameter tweaking (no recompilation needed).
    void set_scalar_param(const std::string& name, float value);
    void set_vector_param(const std::string& name, const float* value, int count);
    void set_texture_param(const std::string& name, const std::string& asset_path);

    // Viewport ID (for streaming to editor).
    uint32_t viewport_id() const { return viewport_id_; }

    // Access the current material instance (for renderer integration).
    MaterialInstance* material_instance() { return material_inst_; }

private:
    bool active_ = false;
    PreviewConfig config_;

    // External references (not owned).
    ShaderLibrary* shader_lib_ = nullptr;
    ViewportRegistry* viewport_reg_ = nullptr;

    // Preview resources.
    uint32_t viewport_id_ = 0;
    MaterialInstance* material_inst_ = nullptr;

    // Camera state.
    double total_time_ = 0.0;
    float manual_yaw_ = 0.f;
    float manual_pitch_ = 0.3f;
    float manual_distance_ = 3.0f;

    // Fallback shader name (last successful compilation).
    std::string current_shader_name_;
    std::string fallback_shader_name_;
    int32_t shader_version_ = 0;
};

} // namespace uz::sg
