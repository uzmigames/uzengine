#pragma once

/// VFX Turbulence Module -- fractal noise-based velocity perturbation.
///
/// Applies FBM (Fractional Brownian Motion) noise to particle velocities,
/// creating organic, turbulent motion patterns. Strength, frequency, and
/// octave count are configurable. Octaves are clamped to [1, 8] to bound
/// GPU cost.
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.

#include "vfx_module.h"

namespace uz {

// ---------------------------------------------------------------------------
// TurbulenceModule -- FBM noise velocity perturbation
// ---------------------------------------------------------------------------

/// Perturbs particle velocity using 3D fractal Brownian motion noise.
///
/// The HLSL snippet evaluates FBM at each particle's position (scaled by
/// frequency) and adds the resulting force vector (scaled by strength) to
/// the particle's velocity. Higher octave counts produce finer detail at
/// increased GPU cost.
///
/// # Parameters
/// | Name      | Type  | Default | Description                              |
/// |-----------|-------|---------|------------------------------------------|
/// | strength  | Float | 1.0     | Force magnitude multiplier               |
/// | frequency | Float | 0.5     | Spatial frequency (higher = tighter noise)|
/// | octaves   | Int   | 3       | FBM octave count (clamped to [1, 8])     |
/// | use_curl_mode | Bool | false | Curl (divergence-free) noise instead of FBM |
class TurbulenceModule final : public IVFXModule {
public:
    TurbulenceModule() = default;
    ~TurbulenceModule() override = default;

    // -- IVFXModule interface ------------------------------------------------

    /// Returns "turbulence".
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns HLSL snippet for FBM noise velocity perturbation.
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns parameter definitions for turbulence configuration.
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// No-op. Compute shader handles all turbulence on the GPU.
    void Execute(const ExecuteContext& ctx) override;

    // -- Configuration (public for editor/serialization) ---------------------

    /// Force magnitude multiplier for turbulence.
    float strength = 1.0f;

    /// Spatial frequency -- higher values produce tighter noise patterns.
    float frequency = 0.5f;

    /// Number of FBM octaves (clamped to [1, 8] to bound GPU cost).
    int32_t octaves = 3;

    /// When true, uses curl noise (divergence-free) instead of raw FBM.
    /// Curl noise produces swirling, volume-preserving motion ideal for
    /// smoke, fire, and fluid-like effects.
    /// @ref Bridson 2007 "Curl-Noise for Procedural Fluid Flow"
    bool useCurlMode = false;

    /// Minimum allowed octave count.
    static constexpr int32_t kMinOctaves = 1;

    /// Maximum allowed octave count (bounds GPU cost).
    static constexpr int32_t kMaxOctaves = 8;
};

} // namespace uz
