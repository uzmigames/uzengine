#pragma once

/// VFX Update Module -- core particle physics integration.
///
/// Applies gravity, drag, and angular velocity to all living particles.
/// This is the standard Euler integration step that runs every frame in the
/// update compute pass, after spawn and before decay.
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.

#include "vfx_module.h"

namespace uz {

// ---------------------------------------------------------------------------
// UpdateModule -- gravity, drag, and rotation integration
// ---------------------------------------------------------------------------

/// Integrates particle velocity and position each frame.
///
/// Applies constant acceleration (gravity), multiplicative drag, and angular
/// velocity to every living particle. The HLSL snippet reads/writes the
/// Positions, Velocities, and Rotations UAVs.
///
/// # Parameters
/// | Name             | Type  | Default         | Description                        |
/// |------------------|-------|-----------------|------------------------------------|
/// | gravity          | Vec3  | (0, -9.81, 0)   | Constant acceleration (m/s^2)      |
/// | drag             | Float | 0.98            | Velocity multiplier per frame      |
/// | angular_velocity | Float | 0.0             | Rotation speed (radians/sec)       |
class UpdateModule final : public IVFXModule {
public:
    UpdateModule() = default;
    ~UpdateModule() override = default;

    // -- IVFXModule interface ------------------------------------------------

    /// Returns "update".
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns HLSL snippet for Euler integration with gravity and drag.
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns parameter definitions for physics configuration.
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// No-op. Compute shader handles all integration on the GPU.
    void Execute(const ExecuteContext& ctx) override;

    // -- Configuration (public for editor/serialization) ---------------------

    /// Constant acceleration applied each frame (m/s^2).
    Vec3 gravity{0.0f, -9.81f, 0.0f};

    /// Velocity multiplier per frame (1.0 = no drag, 0.0 = instant stop).
    float drag = 0.98f;

    /// Rotation speed in radians per second.
    float angularVelocity = 0.0f;
};

} // namespace uz
