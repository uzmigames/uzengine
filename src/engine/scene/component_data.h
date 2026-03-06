#pragma once

// component_data.h — Per-node-type component data.
//
// These structs hold type-specific properties that augment SceneNode.
// Only meaningful when the node's NodeType matches the struct type.
// All fields have sensible defaults so nodes work out-of-the-box.

#include "engine/core/math.h"

namespace uz {

// ============================================================================
// CameraData — properties for NodeType::Camera
// ============================================================================

struct CameraData {
    float fov_y      = 45.0f;   // Vertical field of view (degrees)
    float near_plane = 0.1f;    // Near clipping plane distance
    float far_plane  = 500.0f;  // Far clipping plane distance
};

// ============================================================================
// DirectionalLightData — properties for NodeType::DirectionalLight
// ============================================================================

struct DirectionalLightData {
    Vec3  color     = {1.0f, 1.0f, 1.0f, 0.0f};  // Light color (RGB, _pad=0)
    float intensity = 1.0f;                        // Light intensity multiplier
};

// ============================================================================
// StaticMeshData — properties for NodeType::StaticMesh
// ============================================================================

struct StaticMeshData {
    char mesh_path[256]     = "";   // res://meshes/cube.glb
    char material_path[256] = "";   // res://materials/default.uzmat
    Aabb local_bounds       = Aabb::empty();
};

} // namespace uz
