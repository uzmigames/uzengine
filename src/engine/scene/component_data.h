#pragma once

// component_data.h — Per-node-type component data.
//
// These structs hold type-specific properties attached to a SceneNode via
// ComponentStore. Query via component_has<T>() / component_get<T>().
// All fields have sensible defaults so nodes work out-of-the-box.

#include "engine/core/math.h"

namespace uz {

// ============================================================================
// CameraData — properties for "Camera" nodes
// ============================================================================

struct CameraData {
    float fov_y      = 45.0f;   // Vertical field of view (degrees)
    float near_plane = 0.1f;    // Near clipping plane distance
    float far_plane  = 500.0f;  // Far clipping plane distance
};

// ============================================================================
// DirectionalLightData — properties for "DirectionalLight" nodes
// ============================================================================

struct DirectionalLightData {
    Vec3  color     = {1.0f, 1.0f, 1.0f, 0.0f};  // Light color (RGB, _pad=0)
    float intensity = 1.0f;                        // Light intensity multiplier
};

// ============================================================================
// StaticMeshData — properties for "StaticMesh" nodes
// ============================================================================

struct StaticMeshData {
    char mesh_path[256]     = "";   // res://meshes/cube.glb
    char material_path[256] = "";   // res://materials/default.uzmat
    Aabb local_bounds       = Aabb::empty();
};

// ============================================================================
// SkyAtmosphereData — properties for "SkyAtmosphere" nodes
//
// Controls the sky background gradient and height fog per scene.
// Only the first SkyAtmosphere node in a scene is used.
// Defaults match the built-in UE5-inspired palette.
// ============================================================================

struct SkyAtmosphereData {
    float sky_zenith[3]  = {0.08f, 0.22f, 0.65f};  // deep azure zenith
    float sky_horizon[3] = {0.52f, 0.72f, 0.90f};  // pale bright sky-blue at horizon
    float sky_haze[3]    = {0.28f, 0.22f, 0.16f};  // warm atmospheric haze below horizon
    float sky_ground[3]  = {0.16f, 0.12f, 0.08f};  // muted earth well below horizon
    float fog_density    = 0.0f;                    // height fog density (0 = disabled)
};

} // namespace uz
