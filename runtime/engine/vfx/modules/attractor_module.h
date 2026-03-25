#pragma once

/// VFX Attractor Module -- multi-attractor velocity force.
///
/// Applies a directional force toward (or away from) up to kMaxAttractors
/// world-space attractor points per emitter. The module's own fields
/// (attractorPosition, strength, radius, etc.) define the primary attractor
/// and are automatically registered as the first entry in the AttractorBuffer
/// each frame via UploadAttractors(). Additional attractors can be pushed
/// directly via the buffer's AddAttractor() API.
///
/// Three falloff modes control how force scales with distance:
/// Constant (uniform force), Linear (linearly decreasing to zero at radius),
/// and InverseSquare (physically based gravitational falloff).
///
/// An optional kill-on-arrival flag expires particles that reach the attractor
/// by setting their age equal to their lifespan, causing the emit/expire system
/// to recycle them on the next frame.
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.

#include "vfx_module.h"
#include "engine/vfx/core/attractor_buffer.h"

#include <cstdint>

namespace uz {

class IRHIDevice;

// ---------------------------------------------------------------------------
// AttractorFalloff -- distance-based force attenuation mode
// ---------------------------------------------------------------------------

/// Controls how attractor force scales with distance from the attractor point.
///
/// | Value        | Behavior                                                   |
/// |--------------|-------------------------------------------------------------|
/// | Constant     | Force is uniform regardless of distance (simple magnet).   |
/// | Linear       | Force decreases linearly to zero at `radius`.              |
/// | InverseSquare| Force follows 1/(d^2), physically based (gravity, charge). |
enum class AttractorFalloff : uint8_t {
    Constant      = 0, ///< Uniform force magnitude, ignores radius.
    Linear        = 1, ///< Force = strength * (1 - dist/radius), zero beyond radius.
    InverseSquare = 2, ///< Force = strength / max(dist^2, kMinRadius^2).
};

// ---------------------------------------------------------------------------
// AttractorModule -- point attractor velocity force
// ---------------------------------------------------------------------------

/// Applies multi-attractor forces to particle velocities each simulation tick.
///
/// Each active attractor in the AttractorBuffer is iterated by the compute
/// shader. For each attractor, the shader computes a normalized direction from
/// the particle's position to the attractor, evaluates the configured falloff,
/// and accumulates the resulting acceleration into the particle's velocity.
///
/// The module's own fields define the primary attractor (always slot 0). Extra
/// attractors can be added to `attractorBuffer` directly before dispatch.
///
/// # Parameters
/// | Name               | Type  | Default           | Description                          |
/// |--------------------|-------|-------------------|--------------------------------------|
/// | attractor_position | Vec3  | {0, 0, 0}         | World-space primary attractor point. |
/// | strength           | Float | 10.0              | Peak force magnitude (units/s^2).    |
/// | radius             | Float | 5.0               | Effective radius for Linear falloff. |
/// | falloff            | Int   | InverseSquare (1) | Force attenuation mode (0/1/2).      |
/// | kill_on_arrival    | Bool  | false             | Expire particles that reach point.   |
class AttractorModule final : public IVFXModule {
public:
    AttractorModule() = default;
    ~AttractorModule() override = default;

    // -- IVFXModule interface ------------------------------------------------

    /// Returns "attractor".
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns HLSL snippet that applies multi-attractor forces to particle velocities.
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns the five configurable parameter definitions for this module.
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// No-op. Attractor force is fully evaluated on the GPU each dispatch.
    void Execute(const ExecuteContext& ctx) override;

    // -- Per-frame GPU upload ------------------------------------------------

    /// Uploads attractor data to the GPU structured buffer.
    ///
    /// Clears the buffer, adds the module's own fields as the primary attractor
    /// (slot 0), then calls AttractorBuffer::UploadToGPU() to map-copy the data
    /// to the GPU. Call once per frame before the simulation compute dispatch.
    ///
    /// # Parameters
    /// - `device`: RHI device providing MapBuffer/UnmapBuffer. Must not be null.
    ///
    /// # Preconditions
    /// - `attractorBuffer.IsValid()` must be true.
    void UploadAttractors(IRHIDevice* device);

    // -- Configuration (public for editor and serialization) -----------------

    /// World-space position of the primary attractor point.
    Vec3 attractorPosition{0.0f, 0.0f, 0.0f};

    /// Peak force magnitude in world-space units per second squared.
    float strength = 10.0f;

    /// Effective radius used by the Linear falloff mode.
    /// Force reaches zero at this distance. Ignored by Constant and
    /// InverseSquare modes.
    float radius = 5.0f;

    /// Distance falloff mode for force attenuation.
    AttractorFalloff falloff = AttractorFalloff::InverseSquare;

    /// When true, particles whose distance to the primary attractor fall below
    /// `kKillDistance` are expired by setting Age = Lifespan.
    bool killOnArrival = false;

    /// GPU structured buffer holding up to kMaxAttractors attractor entries.
    ///
    /// Must be created (Create()) before use and destroyed (Destroy()) on shutdown.
    /// UploadAttractors() manages its contents each frame.
    AttractorBuffer attractorBuffer;

    /// Minimum radius squared used to prevent division by zero in
    /// InverseSquare falloff. Applied in both CPU validation and shader.
    static constexpr float kMinRadius = 0.01f;

    /// Distance threshold below which kill-on-arrival expires a particle.
    static constexpr float kKillDistance = 0.05f;
};

} // namespace uz
