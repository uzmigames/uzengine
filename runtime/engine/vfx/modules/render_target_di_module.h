#pragma once

/// VFX Render Target Data Interface Module -- persistent GPU feedback loop texture.
///
/// Manages a persistent 2D accumulation texture (RGBA16F) that serves as both
/// a write target (via compute shader paint pass) and a read source (via
/// billboard pixel shader feedback). This enables persistent trail effects,
/// screen-space paint, and GPU feedback loops without CPU readback.
///
/// Each frame:
/// 1. Decay pass fades the accumulation texture by `decayRate`.
/// 2. Paint pass writes particle contributions (color * opacity * size) at
///    each alive particle's projected screen position.
/// 3. Billboard PS optionally reads the accumulation texture and blends it
///    with particle color using `feedbackStrength`.
///
/// The texture persists across frames unless explicitly cleared (via `clearOnReset`
/// when the emitter resets, or manually via the Clear() API).
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.
///
/// @ref Shiffman 2012 "The Nature of Code" -- particle trail accumulation pattern

#include "vfx_module.h"

#include <cstdint>
#include <json.hpp>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// RenderTargetDIModule -- persistent GPU feedback loop for particles
// ─────────────────────────────────────────────────────────────────────────────

/// Persistent GPU feedback loop module for GPU particles.
///
/// Implements `IVFXModule`. `GetShaderCode()` returns an empty string because
/// this module hooks into the render graph directly with dedicated decay and
/// paint compute passes, plus a feedback read in the billboard pixel shader.
///
/// # Parameters
/// | Name              | Type   | Default | Description                              |
/// |-------------------|--------|---------|------------------------------------------|
/// | tex_width         | Int    | 1280    | Accumulation texture width (pixels)      |
/// | tex_height        | Int    | 720     | Accumulation texture height (pixels)     |
/// | decay_rate        | Float  | 0.02    | Per-frame fade (0 = persistent)          |
/// | paint_opacity     | Float  | 0.05    | Per-particle contribution per frame      |
/// | feedback_strength | Float  | 0.3     | Blend weight for billboard PS feedback   |
/// | screen_space      | Bool   | true    | Map particle XY to screen coordinates    |
/// | clear_on_reset    | Bool   | true    | Clear texture when emitter resets        |
class RenderTargetDIModule final : public IVFXModule {
public:
    RenderTargetDIModule() = default;
    ~RenderTargetDIModule() override = default;

    // -- IVFXModule interface ------------------------------------------------

    /// Returns `"render_target_di"`.
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns an empty string -- this module uses dedicated render graph
    /// passes (decay CS + paint CS) rather than an inline HLSL snippet.
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns parameter definitions for feedback loop configuration.
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// Applies JSON configuration to the module's public fields.
    void ApplyJsonConfig(const nlohmann::json& config) override;

    /// No-op. Decay and paint passes are dispatched by the render graph.
    void Execute(const ExecuteContext& ctx) override;

    // -- Configuration (public for editor and serialization) -----------------

    /// Accumulation texture width in pixels.
    uint32_t texWidth  = 1280;

    /// Accumulation texture height in pixels.
    uint32_t texHeight = 720;

    /// Per-frame decay rate applied to the accumulation texture.
    /// Each texel is multiplied by `(1 - decayRate)` every frame.
    /// 0 = fully persistent (no decay), 1 = cleared each frame.
    /// Typical values: 0.01-0.05 for visible trails.
    float decayRate = 0.02f;

    /// Per-particle paint contribution per frame [0, 1].
    /// Higher values produce brighter, more opaque paint strokes.
    float paintOpacity = 0.05f;

    /// Blend weight for billboard PS feedback read [0, 1].
    /// Controls how much the accumulation texture influences rendered
    /// particle color. 0 = no feedback, 1 = full feedback.
    float feedbackStrength = 0.3f;

    /// When true, particle world positions are projected to screen
    /// coordinates for paint UV lookup. When false, particle XY world
    /// coordinates are mapped directly to texture UV.
    bool screenSpace = true;

    /// When true, the accumulation texture is cleared to zero when the
    /// emitter resets (e.g., loop restart, hot reload).
    bool clearOnReset = true;
};

} // namespace uz
