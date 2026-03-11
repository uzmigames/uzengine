/// Mesh Test Demo
///
/// Renders a .uzmesh cube (Vertex format with normals/tangents) using the
/// standard mesh_vs/mesh_ps shaders with per-object transforms and directional
/// lighting. Tests MeshBuilder + Vertex format + PerObjectCB integration.
///
/// Supports `--screenshot <path>` and `--screenshot-frame <N>` for automated
/// visual testing.

#pragma once

#include <cstdint>
#include <string>

namespace uz {

/// Configuration for running the mesh test demo.
struct MeshTestConfig {
    uint32_t frameCount = 0;            ///< Number of frames (0 = infinite)
    std::string screenshotPath;         ///< If non-empty, save screenshot to this path
    uint32_t screenshotFrame = 5;       ///< Frame number to capture (default: 5)
};

/// Runs the mesh test demo.
///
/// Creates a cube from MeshBuilder, uploads as StructuredBuffer<Vertex>,
/// and renders using mesh_vs.hlsl + mesh_ps.hlsl with per-object transforms
/// and directional lighting.
///
/// # Arguments
/// * `config` — Demo configuration (frame count, screenshot settings).
///
/// # Returns
/// `true` if the demo ran successfully.
bool RunMeshTestDemo(const MeshTestConfig& config = {});

} // namespace uz
