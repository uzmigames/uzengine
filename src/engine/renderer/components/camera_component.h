#pragma once

// camera_component.h — Camera update logic (RHI-agnostic).
//
// Uses engine math types (Mat4, Vec3) — no DirectX/Vulkan/Metal dependencies.
// The graphics backend converts at the boundary.
//
// Two modes:
//   1. Fly-cam (editor): WASD/mouse/scroll from SceneParams::CameraInput
//   2. Scene-driven: reads Camera node transform + CameraData from ComponentStore

#include "engine/core/math.h"

namespace uz {

struct SceneParams;
struct Scene;

// ============================================================================
// CameraState — output of the camera update step (RHI-agnostic)
// ============================================================================

struct CameraState {
    Mat4 view_proj;       // Combined view × projection matrix
    Vec3 eye_pos;         // World-space eye position
    Vec3 forward;         // World-space forward direction (normalized)
};

// ============================================================================
// Camera update functions
// ============================================================================

/// Editor fly-camera — reads input from SceneParams::CameraInput,
/// updates the camera position/rotation, and produces the CameraState.
CameraState camera_update_fly(SceneParams* params, float aspect, float dt);

/// Scene-driven camera — finds the first active Camera node in the scene,
/// reads its world transform and CameraData from the ComponentStore.
CameraState camera_update_from_scene(const Scene* scene, float aspect);

} // namespace uz
