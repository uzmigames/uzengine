#pragma once

/// VFX Pin Module -- spring-force attraction to a fixed world-space point.
///
/// Applies Hooke's law to pull each particle toward a configurable pin
/// position. The restoring force is proportional to the particle's distance
/// from the pin (F = k * x), so particles that drift further are pulled back
/// harder, producing spring-like oscillation when combined with damping.
///
/// @ref Hooke 1678 "De Potentia Restitutiva" -- spring force law F = k * x
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.

#include "vfx_module.h"

namespace uz {

// ---------------------------------------------------------------------------
// PinModule -- Hooke's-law spring attraction to a world-space anchor point
// ---------------------------------------------------------------------------

/// Attracts particles toward a fixed world-space pin using Hooke's spring law.
///
/// The HLSL snippet computes a spring force vector pointing from each particle
/// toward `pinPosition`, with magnitude proportional to the distance between
/// them scaled by `springStrength`. Particles closer than 0.001 world units
/// to the pin are considered at rest and receive no impulse.
///
/// @ref Hooke 1678 "De Potentia Restitutiva" -- F = k * x, where k is the
///      spring constant and x is the displacement from the rest position.
///
/// # Parameters
/// | Name            | Type  | Default     | Description                         |
/// |-----------------|-------|-------------|-------------------------------------|
/// | pin_position    | Vec3  | {0, 0, 0}   | World-space anchor point            |
/// | spring_strength | Float | 5.0         | Spring constant k (Hooke's law)     |
class PinModule final : public IVFXModule {
public:
    PinModule() = default;
    ~PinModule() override = default;

    // -- IVFXModule interface ------------------------------------------------

    /// Returns "pin".
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns HLSL snippet for Hooke's-law spring attraction.
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns parameter definitions for pin position and spring strength.
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// No-op. Compute shader handles all spring force evaluation on the GPU.
    void Execute(const ExecuteContext& ctx) override;

    // -- Configuration (public for editor/serialization) ---------------------

    /// World-space position that particles are attracted toward.
    ///
    /// @ref Hooke 1678 "De Potentia Restitutiva" -- the fixed anchor (rest position).
    Vec3 pinPosition{0.0f, 0.0f, 0.0f};

    /// Spring force coefficient k in Hooke's law (F = k * x).
    ///
    /// Higher values produce stronger, stiffer springs. Typical range [0.1, 50].
    /// @ref Hooke 1678 "De Potentia Restitutiva" -- spring constant k.
    float springStrength = 5.0f;
};

} // namespace uz
