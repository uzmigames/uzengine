/// VFX Kill Volumes — AABB and Sphere Containment Tests
///
/// Defines the KillVolume struct and containment test functions. Particles that
/// are found inside any active kill volume are instantly killed (age set to lifespan)
/// and returned to the free list.
///
/// Two shapes are supported:
///   - AABB  (shape == 0): Axis-aligned bounding box. Uses half-extents per axis.
///   - Sphere (shape == 1): Sphere. Uses halfExtents.x as the radius.
///
/// @ref Ericson 2005 "Real-Time Collision Detection" Ch.4 — AABB/sphere point containment
/// @constants kMaxKillVolumes=16 (matches C++ kMaxKillVolumes)
/// @deviations NONE

#ifndef VFX_KILL_VOLUMES_HLSLI
#define VFX_KILL_VOLUMES_HLSLI

/// GPU mirror of the CPU KillVolume struct (32 bytes = 2 float4).
/// Must match the C++ struct in runtime/engine/vfx/modules/kill_volume.h exactly.
struct KillVolume {
    float3 position;     ///< World-space center of the volume.
    uint   shape;        ///< 0 = AABB, 1 = Sphere.
    float3 halfExtents;  ///< AABB: half-size per axis. Sphere: halfExtents.x = radius.
    uint   _pad;         ///< Alignment padding to 32 bytes.
};

/// Tests whether a world-space point is inside a single kill volume.
///
/// # Parameters
/// - `pos` : Particle world-space position.
/// - `vol` : Kill volume descriptor.
///
/// # Returns
/// `true` if `pos` is inside (or on the boundary of) `vol`, `false` otherwise.
///
/// @ref Ericson 2005 Ch.4 — AABB point containment: all(abs(p-c) <= h)
/// @ref Ericson 2005 Ch.4 — sphere point containment: dot(p-c,p-c) <= r*r
bool IsInsideKillVolume(float3 pos, KillVolume vol)
{
    if (vol.shape == 0u) {
        // AABB containment: the signed distance from center is within half-extents
        // on every axis. A particle exactly on a face is considered inside.
        // @ref Ericson 2005 — AABB point containment: all(abs(p-c) <= h)
        float3 d = abs(pos - vol.position) - vol.halfExtents;
        return all(d <= 0.0f);
    }

    // Sphere containment: squared distance to center vs squared radius.
    // halfExtents.x stores the sphere radius.
    // @ref Ericson 2005 — sphere point containment: dot(p-c,p-c) <= r*r
    float3 delta = pos - vol.position;
    float  r     = vol.halfExtents.x;
    return dot(delta, delta) <= r * r;
}

/// Tests a particle position against all active kill volumes.
///
/// Returns `true` as soon as the particle is found inside any volume,
/// short-circuiting the remaining tests (early exit).
///
/// # Parameters
/// - `pos`     : Particle world-space position.
/// - `volumes` : Structured buffer of KillVolume descriptors.
/// - `count`   : Number of active kill volumes [0..kMaxKillVolumes].
///
/// # Returns
/// `true` if `pos` is inside at least one kill volume.
bool TestAllKillVolumes(float3 pos, StructuredBuffer<KillVolume> volumes, uint count)
{
    for (uint i = 0u; i < count; ++i) {
        if (IsInsideKillVolume(pos, volumes[i])) {
            return true;
        }
    }
    return false;
}

#endif // VFX_KILL_VOLUMES_HLSLI
