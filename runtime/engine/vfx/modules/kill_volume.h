/// VFX Kill Volume — CPU-side kill volume descriptor
///
/// A KillVolume defines a world-space region that instantly destroys particles
/// entering it during the update compute pass. Both AABB and sphere shapes are
/// supported. Multiple volumes can be active simultaneously; the shader tests all
/// active volumes and kills the particle on first hit (early exit).
///
/// Layout: 32 bytes (2 float4) to satisfy GPU structured-buffer alignment.
///
/// @ref Ericson 2005 "Real-Time Collision Detection" Ch.4 —
///      AABB and sphere point containment tests

#pragma once

#include <cstdint>

namespace uz {

/// Maximum number of simultaneously active kill volumes uploaded to the GPU.
/// Matches `kMaxKillVolumes` implied by the GPU structured buffer allocation.
static constexpr uint32_t kMaxKillVolumes = 16u;

/// Shape selector for KillVolume containment tests.
/// Must stay in sync with the HLSL `shape` field comparisons in vfx_kill_volumes.hlsli.
enum class KillVolumeShape : uint32_t {
    AABB   = 0u, ///< Axis-aligned bounding box. Uses `halfExtents` per axis.
    Sphere = 1u, ///< Sphere. Uses `halfExtents[0]` as radius.
};

/// CPU-side kill volume descriptor. Uploaded to a GPU StructuredBuffer each frame
/// for consumption by particle_update_cs.hlsl via `TestAllKillVolumes()`.
///
/// Layout: 32 bytes (2 float4) to satisfy GPU structured-buffer alignment.
/// Must match the HLSL `KillVolume` struct in vfx_kill_volumes.hlsli exactly.
///
/// @ref Ericson 2005 "Real-Time Collision Detection" Ch.4 — AABB/sphere containment
struct KillVolume {
    /// World-space center of the volume.
    float position[3]   = {0.0f, 0.0f, 0.0f};

    /// Volume shape (see KillVolumeShape enum).
    /// 0 = AABB, 1 = Sphere.
    uint32_t shape      = static_cast<uint32_t>(KillVolumeShape::AABB);

    /// For AABB: half-size in world units along each axis (x, y, z).
    /// For Sphere: halfExtents[0] is the sphere radius; y and z are ignored.
    float halfExtents[3] = {1.0f, 1.0f, 1.0f};

    /// Alignment padding to reach 32 bytes (2 x float4).
    uint32_t _pad       = 0u;
};
static_assert(sizeof(KillVolume) == 32u, "KillVolume must be 32 bytes (2 x float4)");

} // namespace uz
