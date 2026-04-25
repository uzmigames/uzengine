#pragma once

/// VFX Spline Modules — three path-following modules sharing a `VfxSpline`.
///
/// All three modules consume the same per-emitter `VfxSpline` (control
/// points + tension + looping) and write its baked samples into a shared
/// `VfxSplineBuffer` once per frame. The HLSL snippets each module emits
/// reference the same cbuffer fields, set by `frame_vfx_inline.cpp` from
/// the resolved `VfxSplineBuffer::GetBindlessSrvIndex` /
/// `pointCount` / `totalLength`:
///
/// ```hlsl
/// uint  cbSplineSrvIndex;     // Bindless SRV — VfxSplineBuffer.
/// uint  cbSplinePointCount;   // Number of valid baked samples.
/// float cbSplineTotalLength;  // Total arc length in cm.
/// ```
///
/// # Modules
///
/// | Class                      | Stage         | Purpose                                          |
/// |----------------------------|---------------|--------------------------------------------------|
/// | `SpawnAlongSplineModule`   | ParticleSpawn | Distributes new particles uniformly along arc.   |
/// | `ForceTowardSplineModule`  | ParticleUpdate| Spring force pulling particles to nearest point. |
/// | `FollowSplineModule`       | ParticleUpdate| Per-particle `u` advances over lifetime.         |
///
/// The three modules can coexist on the same emitter and they all sample
/// the same baked buffer — the upload happens once per frame at the
/// emitter level, not per module.
///
/// # Hardware-pending integration
///
/// The spawn / update CS CB layouts do NOT yet expose the three fields
/// above; that wiring is tracked by `phase16_vfx-spline-data-interface-
/// hardware-validation`. The shader snippets ship today and will start
/// dispatching correctly the moment the CB plumbing lands — until then
/// the modules pass through `Execute` as a no-op, the GPU bakes the
/// `VfxSplineBuffer` correctly, and the editor can author the configs.
///
/// @ref UE5 Niagara Spline DI
/// @ref docs/engine/25-vfx/101-spline-data-interface.md

#include "vfx_module.h"
#include "engine/vfx/data_interfaces/vfx_spline_buffer.h"
#include "engine/vfx/data_interfaces/vfx_spline_di.h"

#include <cstdint>
#include <string>

namespace uz {

class IRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// SpawnAlongSplineConfig
// ─────────────────────────────────────────────────────────────────────────────

/// Author-facing configuration of `SpawnAlongSplineModule`. Kept as a
/// value-typed struct so the editor can clone / serialize without
/// instantiating the module's GPU buffer.
struct SpawnAlongSplineConfig {
    /// Whether spawn locations are arc-length-uniform (`true`, default —
    /// each newly-spawned particle is offset by `totalLength /
    /// (spawnCount + 1)` from its predecessor) or random along arc length
    /// (`false`).
    bool stratified = true;

    /// When non-zero, scatters spawn positions perpendicular to the
    /// tangent by up to `jitterRadius` centimetres. Applied uniformly in
    /// the disc orthogonal to the tangent at the sample point.
    float jitterRadius = 0.0f;

    /// Optional offset along the tangent at spawn. Negative values place
    /// the particle slightly before the sample, positive after — used to
    /// stagger streamers.
    float tangentOffset = 0.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// SpawnAlongSplineModule
// ─────────────────────────────────────────────────────────────────────────────

/// Module that distributes spawned particles along a `VfxSpline`.
///
/// The spawn CS reads the bindless `VfxSplineBuffer` and assigns each new
/// particle's position via `SplineSampleAtU(u)` where `u` is computed
/// from the particle's spawn slot index when stratified, or a hash of
/// the slot index when not.
class SpawnAlongSplineModule final : public IVFXModule {
public:
    SpawnAlongSplineModule() = default;
    explicit SpawnAlongSplineModule(const SpawnAlongSplineConfig& cfg)
        : config_(cfg) {}
    ~SpawnAlongSplineModule() override = default;

    [[nodiscard]] const char* GetName() const noexcept override {
        return "spawn_along_spline";
    }
    [[nodiscard]] std::string_view GetShaderCode() const override;
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;
    void ApplyJsonConfig(const nlohmann::json& config) override;
    void Execute(const ExecuteContext& /*ctx*/) override {}

    [[nodiscard]] const SpawnAlongSplineConfig& GetConfig() const noexcept {
        return config_;
    }

    /// Parses a `SpawnAlongSplineConfig` from JSON. Fields fall back to
    /// struct defaults when missing. See header `# Modules` table for
    /// recognised keys (`stratified`, `jitter_radius`, `tangent_offset`).
    [[nodiscard]] static SpawnAlongSplineConfig ParseConfig(const nlohmann::json& j);

private:
    SpawnAlongSplineConfig config_{};
};

// ─────────────────────────────────────────────────────────────────────────────
// ForceTowardSplineConfig
// ─────────────────────────────────────────────────────────────────────────────

struct ForceTowardSplineConfig {
    /// Spring stiffness. Force = stiffness * (nearestSplinePoint - pos).
    /// 0.0 disables the force; 5.0..50.0 covers most natural-feeling
    /// "tether" effects at typical 60 Hz simulation.
    float stiffness = 10.0f;

    /// Hard cap on per-frame force magnitude (cm/s^2). When the
    /// computed spring force would exceed this, the direction is kept
    /// and the magnitude clamped — prevents explosive blow-up when a
    /// particle spawns far from a short spline.
    float maxAcceleration = 2000.0f;

    /// When `> 0`, particles closer than `attractDistance` cm are pulled
    /// toward the spline; particles further away receive zero force.
    /// `0` disables the cull (every particle pulls regardless of
    /// distance — useful for guaranteed homing).
    float attractDistance = 0.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// ForceTowardSplineModule
// ─────────────────────────────────────────────────────────────────────────────

/// Spring force pulling particles toward the nearest baked spline point.
class ForceTowardSplineModule final : public IVFXModule {
public:
    ForceTowardSplineModule() = default;
    explicit ForceTowardSplineModule(const ForceTowardSplineConfig& cfg)
        : config_(cfg) {}
    ~ForceTowardSplineModule() override = default;

    [[nodiscard]] const char* GetName() const noexcept override {
        return "force_toward_spline";
    }
    [[nodiscard]] std::string_view GetShaderCode() const override;
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;
    void ApplyJsonConfig(const nlohmann::json& config) override;
    void Execute(const ExecuteContext& /*ctx*/) override {}

    [[nodiscard]] const ForceTowardSplineConfig& GetConfig() const noexcept {
        return config_;
    }
    [[nodiscard]] static ForceTowardSplineConfig ParseConfig(const nlohmann::json& j);

private:
    ForceTowardSplineConfig config_{};
};

// ─────────────────────────────────────────────────────────────────────────────
// FollowSplineConfig
// ─────────────────────────────────────────────────────────────────────────────

struct FollowSplineConfig {
    /// Speed scale applied to the per-particle `u` advance. 1.0 means
    /// the particle traverses the full spline over its lifetime; 2.0
    /// traverses twice; 0.5 covers half. Negative values reverse.
    float speed = 1.0f;

    /// When `true`, particles past `u = 1.0` wrap back to 0 (loop the
    /// path). When `false`, particles freeze at the endpoint and continue
    /// to age normally.
    bool loop = false;

    /// Initial offset along the spline at spawn. `0` means every particle
    /// starts at the spline's t=0; `0.5` means everyone starts mid-path.
    /// Useful for seeding a "river" that doesn't begin at one end.
    float startU = 0.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// FollowSplineModule
// ─────────────────────────────────────────────────────────────────────────────

/// Per-particle u-parameter advances by lifetime fraction; position
/// becomes `SplineSampleAtU(u)`. Replaces velocity-driven motion for the
/// affected particles — typically used in combination with a tiny
/// damping update so any residual velocity drains out.
class FollowSplineModule final : public IVFXModule {
public:
    FollowSplineModule() = default;
    explicit FollowSplineModule(const FollowSplineConfig& cfg)
        : config_(cfg) {}
    ~FollowSplineModule() override = default;

    [[nodiscard]] const char* GetName() const noexcept override {
        return "follow_spline";
    }
    [[nodiscard]] std::string_view GetShaderCode() const override;
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;
    void ApplyJsonConfig(const nlohmann::json& config) override;
    void Execute(const ExecuteContext& /*ctx*/) override {}

    [[nodiscard]] const FollowSplineConfig& GetConfig() const noexcept {
        return config_;
    }
    [[nodiscard]] static FollowSplineConfig ParseConfig(const nlohmann::json& j);

private:
    FollowSplineConfig config_{};
};

} // namespace uz
