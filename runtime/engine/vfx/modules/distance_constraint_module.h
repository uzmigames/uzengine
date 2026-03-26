#pragma once

/// VFX Distance Constraint Module -- radial range enforcement around a world-space origin.
///
/// Keeps particles within a spherical shell defined by a minimum and maximum
/// distance from a configurable world-space origin. Particles that violate
/// the inner boundary are pushed outward; particles that violate the outer
/// boundary are pulled inward. The restoring force is proportional to the
/// distance of the violation, scaled by `constraintStrength`.
///
/// A zero-length direction vector (particle exactly at the origin) is ignored
/// to avoid a division by zero -- the particle receives no impulse.
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.

#include "vfx_module.h"

namespace uz {

// ---------------------------------------------------------------------------
// DistanceConstraintModule -- spherical-shell distance constraint
// ---------------------------------------------------------------------------

/// Enforces a spherical shell around a world-space origin by applying
/// corrective velocity impulses to particles that leave the valid range.
///
/// The constraint evaluates per-particle on the GPU each tick. The CPU
/// side is a no-op -- all work happens inside the injected HLSL snippet.
///
/// # Parameters
/// | Name                | Type  | Default     | Description                                              |
/// |---------------------|-------|-------------|----------------------------------------------------------|
/// | constraint_origin   | Vec3  | {0, 0, 0}   | World-space center of the constraint sphere              |
/// | min_distance        | Float | 0.0         | Inner radius; particles closer are pushed outward        |
/// | max_distance        | Float | 50.0        | Outer radius; particles farther are pulled inward        |
/// | constraint_strength | Float | 10.0        | Force multiplier controlling how aggressively to correct |
class DistanceConstraintModule final : public IVFXModule {
public:
    DistanceConstraintModule() = default;
    ~DistanceConstraintModule() override = default;

    // -- IVFXModule interface ------------------------------------------------

    /// Returns "distance_constraint".
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns the HLSL snippet that applies radial constraint impulses.
    ///
    /// Expects the following constant buffer entries to be bound:
    /// - `cbConstraintOrigin`  (float3): world-space constraint center
    /// - `cbMinDistance`       (float):  inner radius
    /// - `cbMaxDistance`       (float):  outer radius
    /// - `cbConstraintStrength`(float):  force magnitude multiplier
    /// - `cbDt`                (float):  frame delta time in seconds
    ///
    /// Standard particle UAVs `Positions` and `Velocities` must be accessible.
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns the four parameter definitions for this module.
    ///
    /// Declaration order matches the expected constant buffer layout:
    /// constraint_origin (Vec3), min_distance (Float),
    /// max_distance (Float), constraint_strength (Float).
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// No-op. The distance constraint compute shader performs all work on
    /// the GPU; no CPU-side dispatch preparation is required.
    void Execute(const ExecuteContext& ctx) override;

    // -- Configuration (public for editor/serialization) ---------------------

    /// World-space origin for the distance check.
    Vec3 constraintOrigin = {0.0f, 0.0f, 0.0f};

    /// Minimum allowed distance from the origin.
    /// Particles closer than this value are pushed outward.
    float minDistance = 0.0f;

    /// Maximum allowed distance from the origin.
    /// Particles farther than this value are pulled inward.
    float maxDistance = 50.0f;

    /// Force applied to push or pull particles back into the valid range.
    /// Units: world-units per second per world-unit of violation (i.e., a
    /// spring-like coefficient).
    float constraintStrength = 10.0f;
};

} // namespace uz
