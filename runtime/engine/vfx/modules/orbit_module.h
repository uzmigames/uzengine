#pragma once

/// VFX Orbit Module -- circular orbital motion around a world-space point.
///
/// Applies a tangential velocity impulse each frame that steers particles
/// around a configurable center point.  An additional radial spring force
/// pulls particles toward the desired orbit radius, keeping orbits stable
/// without requiring analytically exact initial conditions.
///
/// All computation runs on the GPU as an HLSL snippet.  The CPU-side
/// Execute() is a no-op; every configurable value is uploaded through the
/// standard VFX constant buffer layout.
///
/// # Thread Safety
/// Not thread-safe.  Owned and invoked by VFXSystem on the main/render thread.

#include "vfx_module.h"

namespace uz {

// ---------------------------------------------------------------------------
// OrbitModule -- circular orbital motion around a configurable center point
// ---------------------------------------------------------------------------

/// Steers particles into a circular orbit around a world-space center.
///
/// Two forces are applied each frame:
///
/// 1. **Tangential impulse** -- a velocity increment along the cross product
///    of the orbit axis and the radial direction.  Magnitude is
///    `speed * dt`, producing constant angular velocity.
///
/// 2. **Radial spring** -- pulls the particle toward `orbitRadius` along the
///    in-plane radial direction.  Magnitude is
///    `(orbitRadius - currentRadius) * speed * dt`, which damps excursions
///    from the desired radius.
///
/// Vectors shorter than 1e-6 are skipped to avoid NaN from normalize().
///
/// # Parameters
/// | Name         | Type  | Default      | Description                            |
/// |--------------|-------|--------------|----------------------------------------|
/// | center       | Vec3  | {0, 0, 0}   | World-space center of the orbit        |
/// | speed        | Float | 1.0          | Angular speed in radians per second    |
/// | orbit_radius | Float | 5.0          | Desired orbit radius (cm)              |
/// | axis         | Vec3  | {0, 0, 1}   | Orbit axis, Z-up by default            |
class OrbitModule final : public IVFXModule {
public:
    OrbitModule() = default;
    ~OrbitModule() override = default;

    // -- IVFXModule interface ------------------------------------------------

    /// Returns "orbit".
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns the HLSL snippet that applies orbital velocity each tick.
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns parameter definitions for center, speed, orbit_radius, and axis.
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// No-op. All orbit computation is performed on the GPU.
    void Execute(const ExecuteContext& ctx) override;

    // -- Configuration (public for editor / serialization) -------------------

    /// World-space point that particles orbit around.
    Vec3 center = {0.0f, 0.0f, 0.0f};

    /// Angular speed in radians per second.  Positive values rotate
    /// counter-clockwise when viewed from the positive-axis direction.
    float speed = 1.0f;

    /// Desired orbit radius in centimetres.  The radial spring force drives
    /// particles toward this distance from the orbit plane projection of center.
    float orbitRadius = 5.0f;

    /// Axis of rotation (Z-up coordinate system by default).  The orbit plane
    /// is perpendicular to this vector.  Need not be unit length -- the shader
    /// normalises it.
    Vec3 axis = {0.0f, 0.0f, 1.0f};
};

} // namespace uz
