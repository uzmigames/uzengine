#pragma once

/// VFX Constraint Module -- axis-aligned bounding box position clamping.
///
/// Clamps particle positions to an axis-aligned bounding box. Particles that
/// would exit the bounds are snapped back to the nearest boundary face.
/// Runs after all force/velocity modules in the update pass.
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.

#include "vfx_module.h"

namespace uz {

// ---------------------------------------------------------------------------
// ConstraintModule -- AABB position clamping
// ---------------------------------------------------------------------------

/// Constrains particle positions within an axis-aligned bounding box.
///
/// The HLSL snippet clamps each particle's position to the box defined by
/// [boundsMin, boundsMax]. This prevents particles from escaping a defined
/// volume -- useful for contained effects like fire in a lantern or smoke
/// in a room.
///
/// # Parameters
/// | Name       | Type | Default            | Description                    |
/// |------------|------|--------------------|--------------------------------|
/// | bounds_min | Vec3 | (-100, -100, -100) | AABB minimum corner            |
/// | bounds_max | Vec3 | (100, 100, 100)    | AABB maximum corner            |
class ConstraintModule final : public IVFXModule {
public:
    ConstraintModule() = default;
    ~ConstraintModule() override = default;

    // -- IVFXModule interface ------------------------------------------------

    /// Returns "constraint".
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns HLSL snippet for AABB position clamping.
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns parameter definitions for bounds configuration.
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// No-op. Compute shader handles all constraint logic on the GPU.
    void Execute(const ExecuteContext& ctx) override;

    // -- Configuration (public for editor/serialization) ---------------------

    /// Minimum corner of the bounding box.
    Vec3 boundsMin{-100.0f, -100.0f, -100.0f};

    /// Maximum corner of the bounding box.
    Vec3 boundsMax{100.0f, 100.0f, 100.0f};
};

} // namespace uz
