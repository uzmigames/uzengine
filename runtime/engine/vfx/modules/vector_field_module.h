#pragma once

/// VFX Vector Field Module -- 3D texture-driven velocity perturbation.
///
/// Samples a 3D texture (vector field) at each particle's local position
/// to apply directional forces. The field is mapped to a world-space
/// bounding box centred at `fieldOffset`. Inside the box, field vectors
/// are scaled by `intensity` and blended with the particle's existing
/// velocity via `tightness`:
///
///   tightness == 0  →  purely additive force (velocity += field * dt)
///   tightness == 1  →  velocity is fully replaced by the field each frame
///   intermediate    →  lerp(velocity, fieldVelocity, tightness * dt)
///
/// Particles outside the bounding box are unaffected (no force applied).
///
/// # Reference
/// @ref Stam 1999 "Stable Fluids" -- vector field advection fundamentals
/// @ref Bridson 2007 "Fluid Simulation for Computer Graphics" -- practical
///      advection, boundary conditions, and velocity field sampling.
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.

#include "vfx_module.h"
#include "engine/rhi/rhi_types.h"

namespace uz {

// ---------------------------------------------------------------------------
// VectorFieldModule -- 3D texture-driven velocity perturbation
// ---------------------------------------------------------------------------

/// Applies directional forces derived from a 3D (volume) vector field texture.
///
/// The module maps each particle's world-space position into the unit cube of
/// the 3D texture using the configured bounding box and field offset. Particles
/// inside the box sample the field and receive a force; particles outside are
/// unaffected.
///
/// The 3D texture is expected to store unit-length-ish direction vectors in
/// RGB channels, encoded in the range [-1, 1] (i.e. the texture stores
/// `(dir * 0.5 + 0.5)` and the shader decodes back to `[-1, 1]`). Use a
/// `RGBA16Float` or `RGBA32Float` texture if the field carries magnitude in
/// the alpha channel.
///
/// # Parameters
/// | Name         | Type  | Default       | Description                                     |
/// |--------------|-------|---------------|-------------------------------------------------|
/// | field_texture| Int   | -1 (invalid)  | Bindless SRV index of the 3D field texture.     |
/// | intensity    | Float | 1.0           | Force magnitude multiplier.                     |
/// | tightness    | Float | 0.0           | 0=additive force, 1=replace velocity with field.|
/// | bounds       | Vec3  | {10, 10, 10}  | World-space half-extents of the field volume.   |
/// | field_offset | Vec3  | {0, 0, 0}     | World-space centre of the field volume.         |
class VectorFieldModule final : public IVFXModule {
public:
    VectorFieldModule() = default;
    ~VectorFieldModule() override = default;

    // -- IVFXModule interface ------------------------------------------------

    /// Returns "vector_field".
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns HLSL snippet that samples a 3D vector field and perturbs
    /// particle velocities.
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns the five configurable parameter definitions for this module.
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// No-op. Vector field sampling is fully evaluated on the GPU each dispatch.
    void Execute(const ExecuteContext& ctx) override;

    // -- Configuration (public for editor and serialization) -----------------

    /// Bindless SRV index of the 3D vector field texture.
    ///
    /// The texture must be a Texture3D resource bound to the bindless SRV heap.
    /// When invalid (index == UINT32_MAX), the module emits no force.
    RHITextureHandle fieldTexture = RHITextureHandle::Invalid();

    /// Force magnitude multiplier applied to sampled field vectors.
    ///
    /// Scales the field velocity before blending or adding to the particle
    /// velocity. A value of 1.0 applies the field at face value.
    float intensity = 1.0f;

    /// Blending factor between additive force and full velocity replacement.
    ///
    /// At 0.0, the field vector is added as an acceleration each frame
    /// (`velocity += field * intensity * dt`). At 1.0, the particle velocity
    /// is fully replaced by the field value (`velocity = field * intensity`).
    /// Intermediate values lerp between the two behaviours scaled by dt.
    float tightness = 0.0f;

    /// World-space half-extents of the field volume bounding box (cm).
    ///
    /// A particle at world position P is in the field when:
    ///   abs(P - fieldOffset) <= bounds  (component-wise)
    /// The default {10, 10, 10} represents a 20x20x20 cm cube.
    Vec3 bounds{10.0f, 10.0f, 10.0f};

    /// World-space centre of the field volume (cm).
    ///
    /// Defines the origin of the 3D texture's coordinate space. Shifting this
    /// moves the field without re-uploading texture data.
    Vec3 fieldOffset{0.0f, 0.0f, 0.0f};

    /// Time scale for animated vector fields (UVW scroll speed).
    ///
    /// When non-zero, the field's UVW coordinates are scrolled along the
    /// W axis by `timeScale * elapsedTime`, creating an animated flow effect.
    /// A value of 0.0 disables animation (static field).
    ///
    /// @ref Stam 1999 "Stable Fluids" — time-varying velocity fields
    float timeScale = 0.0f;
};

} // namespace uz
