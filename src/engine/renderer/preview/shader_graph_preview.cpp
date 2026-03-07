// shader_graph_preview.cpp -- Preview environment implementation.
//
// Creates a viewport, preview mesh, and material. Applies compiled shader graphs
// as dynamic materials with fallback retention on compilation failure.

#include "engine/renderer/preview/shader_graph_preview.h"
#include "engine/renderer/resources/shader_library.h"
#include "engine/renderer/core/viewport_registry.h"

#include <cstdio>

namespace uz::sg {

ShaderGraphPreview::~ShaderGraphPreview() {
    if (active_) shutdown();
}

bool ShaderGraphPreview::init(const PreviewConfig& config,
                              ShaderLibrary& shader_lib,
                              ViewportRegistry& viewport_reg) {
    if (active_) return false;

    config_ = config;
    shader_lib_ = &shader_lib;
    viewport_reg_ = &viewport_reg;

    // Allocate a Material viewport.
    ViewportDesc desc{};
    desc.width = config.width;
    desc.height = config.height;
    desc.target_fps = config.target_fps;
    desc.jpeg_quality = config.jpeg_quality;
    desc.vram_bytes = 32 * 1024 * 1024;  // 32 MB estimate
    snprintf(desc.debug_name, sizeof(desc.debug_name), "ShaderGraphPreview");

    viewport_id_ = viewport_reg_->create(desc);
    if (viewport_id_ == 0) return false;

    (void)viewport_reg_->set_active(viewport_id_, true);

    total_time_ = 0.0;
    active_ = true;
    return true;
}

void ShaderGraphPreview::shutdown() {
    if (!active_) return;

    if (viewport_reg_ && viewport_id_ != 0) {
        (void)viewport_reg_->set_active(viewport_id_, false);
    }

    material_inst_ = nullptr;
    viewport_id_ = 0;
    active_ = false;
    shader_lib_ = nullptr;
    viewport_reg_ = nullptr;
}

CameraState ShaderGraphPreview::update(float dt, float aspect) {
    total_time_ += dt;

    if (config_.auto_rotate) {
        return camera_update_orbit(static_cast<float>(total_time_), aspect);
    }

    // Manual camera: convert yaw to a fixed orbit time so we can reuse
    // camera_update_orbit. The orbit speed is 0.4 rad/s, so time = yaw / 0.4.
    float fixed_time = manual_yaw_ / 0.4f;
    return camera_update_orbit(fixed_time, aspect);
}

void ShaderGraphPreview::set_preview_mesh(PreviewMesh mesh) {
    config_.mesh = mesh;
    // Actual mesh swap happens at render time via the renderer's mesh management.
    // The renderer checks current_mesh() and uses the cached GPU mesh.
}

void ShaderGraphPreview::set_camera_orbit(float yaw, float pitch, float distance) {
    manual_yaw_ = yaw;
    manual_pitch_ = pitch;
    manual_distance_ = distance;
    config_.auto_rotate = false;
}

bool ShaderGraphPreview::apply_compiled_shader(const std::string& vertex_hlsl,
                                                const std::string& pixel_hlsl) {
    if (!shader_lib_ || !active_) return false;

    // Register the shader with a versioned name.
    shader_version_++;
    char name[128];
    snprintf(name, sizeof(name), "shader_graph_preview_v%d", shader_version_);

    ShaderProgram program{};
    program.vs_source = vertex_hlsl.c_str();
    program.vs_size = vertex_hlsl.size();
    program.ps_source = pixel_hlsl.c_str();
    program.ps_size = pixel_hlsl.size();
    program.vs_entry = "main";
    program.ps_entry = "main";

    shader_lib_->register_shader(name, program);

    // Store as current. The previous current becomes fallback.
    if (!current_shader_name_.empty()) {
        fallback_shader_name_ = current_shader_name_;
    }
    current_shader_name_ = name;
    return true;
}

void ShaderGraphPreview::set_scalar_param(const std::string& name, float value) {
    if (material_inst_) {
        material_inst_->set_scalar(name, value);
    }
}

void ShaderGraphPreview::set_vector_param(const std::string& name,
                                           const float* value, int count) {
    if (material_inst_) {
        std::vector<float> v(value, value + count);
        material_inst_->set_vector(name, std::move(v));
    }
}

void ShaderGraphPreview::set_texture_param(const std::string& name,
                                            const std::string& /*asset_path*/) {
    // Texture loading from path requires the asset pipeline.
    // For now, this is a stub — the renderer integration will handle
    // texture resolution via AssetRegistry.
    (void)name;
}

} // namespace uz::sg
