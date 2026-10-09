#pragma once

/// VFX Async GPU Trace Module -- depth-buffer raycasting for particle collision.
///
/// Performs GPU-based collision detection by tracing each alive particle's
/// velocity vector against the previous frame's depth buffer in a compute
/// shader. No CPU readback is required; collision events (position, normal,
/// reflected velocity) are written to a UAV event buffer and consumed by
/// a subsequent apply-collisions pass in the same frame.
///
/// The trace is a linear ray march: for each particle, the shader steps
/// from the particle's position along its velocity direction for up to
/// `maxSteps` steps (each `traceRadius / maxSteps` cm long), projects each
/// sample point to screen space, and compares its NDC depth against the
/// depth buffer. When the ray crosses behind the scene surface, a collision
/// event is recorded.
///
/// Surface normals are estimated from the depth buffer gradient via
/// finite differences (dL-dR, dD-dU), not from a normal GBuffer.
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.
///
/// @ref Laine & Karras 2010 "Efficient Sparse Voxel Octrees"
///      -- ray-depth intersection via linear depth march
/// @ref Lagarde & de Rousiers 2014 "Moving Frostbite to PBR"
///      -- depth buffer reprojection for screen-space effects

#include "vfx_module.h"

#include <cstdint>
#include <json.hpp>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// AsyncGPUTraceModule -- screen-space depth trace for particle collision
// ─────────────────────────────────────────────────────────────────────────────

/// GPU depth-buffer trace collision module for particles.
///
/// Implements `IVFXModule`. `GetShaderCode()` returns an empty string because
/// the depth trace runs as dedicated compute passes (trace + apply collisions)
/// orchestrated by the render graph, not as an inline HLSL snippet.
///
/// # Parameters
/// | Name              | Type  | Default | Description                              |
/// |-------------------|-------|---------|------------------------------------------|
/// | trace_radius      | Float | 30.0    | Max trace distance per particle (cm)     |
/// | max_steps         | Int   | 8       | Number of linear march steps             |
/// | restitution       | Float | 0.4     | Coefficient of restitution [0, 1]        |
/// | write_death_decals| Bool  | false   | Forward hits to decal system             |
///
/// # Known Limitations
/// - Screen-space tracing misses geometry behind other objects (standard SSR
///   limitation). Particles near occluded surfaces may tunnel through.
/// - Depth normal estimation via finite differences degrades at grazing
///   angles and silhouette edges.
class AsyncGPUTraceModule final : public IVFXModule {
public:
    AsyncGPUTraceModule() = default;
    ~AsyncGPUTraceModule() override = default;

    // -- IVFXModule interface ------------------------------------------------

    /// Returns `"async_gpu_trace"`.
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns an empty string -- trace and apply-collision passes are
    /// dedicated render graph compute passes, not inline HLSL snippets.
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns parameter definitions for trace configuration.
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// Applies JSON configuration to the module's public fields.
    void ApplyJsonConfig(const nlohmann::json& config) override;

    /// No-op. Depth trace and collision application run entirely on the GPU
    /// via dedicated compute passes orchestrated by the render graph.
    void Execute(const ExecuteContext& ctx) override;

    // -- Configuration (public for editor and serialization) -----------------

    /// Maximum trace distance per particle in centimetres.
    /// Each linear march step covers `traceRadius / maxSteps` cm.
    /// Default 30.0 cm with 8 steps = 3.75 cm per step.
    float traceRadius = 30.0f;

    /// Number of linear ray march steps per particle.
    /// Higher values improve accuracy for sharp-edged geometry but increase
    /// per-particle compute cost linearly. 8 is sufficient for smooth surfaces.
    uint32_t maxSteps = 8;

    /// Coefficient of restitution applied to reflected velocity on collision.
    /// 0 = completely inelastic (particle stops), 1 = perfectly elastic bounce.
    float restitution = 0.4f;

    /// When true, trace hit events are also forwarded to the decal system
    /// so that particles leave marks at collision points with correct normals.
    bool writeDeathDecals = false;
};

} // namespace uz
