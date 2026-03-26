#pragma once

/// VFX Spawn Module -- particle emission with shape-based positioning.
///
/// Controls how new particles enter the simulation: spawn rate, initial
/// velocity, lifespan range, and emission shape (point, sphere, or box).
/// The HLSL snippet atomically claims free-list slots and initializes
/// all per-particle attributes.
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.

#include "vfx_module.h"

namespace uz {

// ---------------------------------------------------------------------------
// SpawnShape -- emission volume geometry
// ---------------------------------------------------------------------------

/// Shape used for particle emission origin offset.
enum class SpawnShape : uint8_t {
    Point,   ///< All particles spawn at emitter origin (no offset).
    Sphere,  ///< Particles spawn at random positions within a sphere.
    Box,     ///< Particles spawn at random positions within an axis-aligned box.
};

// ---------------------------------------------------------------------------
// SpawnModule -- particle emission and initialization
// ---------------------------------------------------------------------------

/// Spawns new particles with configurable rate, shape, velocity, and lifespan.
///
/// Each frame, the GPU compute shader claims free-list slots via atomic
/// decrement and initializes position (based on spawn shape), velocity
/// (cone distribution with variation), age, lifespan, size, color, and rotation.
///
/// # Parameters
/// | Name               | Type  | Default        | Description                        |
/// |--------------------|-------|----------------|------------------------------------|
/// | rate               | Float | 100.0          | Particles emitted per second       |
/// | lifespan_min       | Float | 1.0            | Minimum particle lifetime (sec)    |
/// | lifespan_max       | Float | 5.0            | Maximum particle lifetime (sec)    |
/// | velocity           | Vec3  | (0, 10, 0)     | Base emission velocity direction   |
/// | velocity_variation | Float | 1.0            | Random velocity offset magnitude   |
/// | spread_angle       | Float | 30.0           | Emission cone half-angle (degrees) |
/// | spawn_radius       | Float | 1.0            | Sphere shape radius                |
class SpawnModule final : public IVFXModule {
public:
    SpawnModule() = default;
    ~SpawnModule() override = default;

    // -- IVFXModule interface ------------------------------------------------

    /// Returns "spawn".
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns HLSL snippet for free-list allocation and particle initialization.
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns parameter definitions for spawn configuration.
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// No-op. Compute shader handles all spawn logic on the GPU.
    void Execute(const ExecuteContext& ctx) override;

    // -- Configuration (public for editor/serialization) ---------------------

    /// Particles emitted per second.
    float rate = 100.0f;

    /// Minimum particle lifetime in seconds.
    float lifespanMin = 1.0f;

    /// Maximum particle lifetime in seconds.
    float lifespanMax = 5.0f;

    /// Base emission velocity direction and magnitude.
    Vec3 velocity{0.0f, 10.0f, 0.0f};

    /// Random velocity offset applied per-axis.
    float velocityVariation = 1.0f;

    /// Emission cone half-angle in degrees.
    float spreadAngle = 30.0f;

    /// Emission shape geometry.
    SpawnShape spawnShape = SpawnShape::Point;

    /// Radius for SpawnShape::Sphere.
    float spawnRadius = 1.0f;

    /// Half-extents for SpawnShape::Box.
    Vec3 spawnBoxExtents{1.0f, 1.0f, 1.0f};
};

} // namespace uz
