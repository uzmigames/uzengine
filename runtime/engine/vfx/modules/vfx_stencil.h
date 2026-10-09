#pragma once

/// VFX per-emitter stencil + CustomDepth output.
///
/// Particles currently cannot participate in post-process masking (heat
/// haze gated by fire, outline-only-on-hero) or reveal effects (wallhack,
/// x-ray). Niagara exposes two authoring flags that this module mirrors:
///
/// - `stencilRef` (0..255) — the billboard PS writes this value to stencil
///   via `SV_StencilRef` so PP passes can sample the stencil RT and mask
///   their effect to regions covered by specific particles.
/// - `customDepthEnabled` — an additional depth-only prepass renders the
///   emitter into the `CustomDepth` RT. Reveal / outline / DoF passes
///   sample that RT as a depth-per-object mask.
///
/// Both features are off by default. Authors enable them via the VFX
/// emitter inspector; the renderer reads the config each frame and
/// selects the matching PSO permutation.
///
/// # Thread safety
/// Config is value-typed. Load on the main thread; the renderer reads
/// read-only during Tick.
///
/// @ref UE5 UNiagaraRendererProperties::bRenderCustomDepth
/// @ref docs/engine/25-vfx/34-stencil-customdepth.md

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Config
// ─────────────────────────────────────────────────────────────────────────────

/// Stencil write mode. Matches the three common DX12 `D3D12_STENCIL_OP`
/// behaviours particles actually need — `Replace` (write fixed ref),
/// `Keep` (disable stencil writes entirely), and `Increment` (for
/// count-based masks).
enum class VfxStencilWriteMode : uint8_t {
    Disabled  = 0, ///< No stencil writes. `stencilRef` is ignored.
    Replace   = 1, ///< Stencil = `stencilRef` (most common).
    Increment = 2, ///< Stencil = clamp(stencil + 1, 255).
};

/// Per-emitter stencil + CustomDepth configuration.
struct VfxStencilConfig {
    /// Stencil reference value, 0..255. The emitter's billboard PS writes
    /// this via `SV_StencilRef` when `writeMode != Disabled`.
    uint8_t stencilRef = 0;

    /// Stencil write mode (see enum).
    VfxStencilWriteMode writeMode = VfxStencilWriteMode::Disabled;

    /// When true, the emitter renders into the `CustomDepth` RT through
    /// an additional depth-only prepass. Consumers (reveal, outline, DoF)
    /// sample the CustomDepth RT as a per-object mask.
    bool customDepthEnabled = false;

    /// Returns true when the emitter has any stencil / CustomDepth
    /// output enabled. The renderer uses this to decide whether to
    /// include the emitter in the corresponding passes.
    [[nodiscard]] bool IsEnabled() const noexcept {
        return writeMode != VfxStencilWriteMode::Disabled || customDepthEnabled;
    }

    /// Returns true when the config is legal. Invalid configs are:
    ///   - `Replace` mode with `stencilRef == 0` (writing 0 is a no-op —
    ///     emit a warning at the authoring site).
    ///
    /// Other combinations are always valid, including `stencilRef != 0` +
    /// `writeMode == Disabled` (authors can stage changes without taking
    /// effect).
    [[nodiscard]] bool IsValid() const noexcept;
};

/// Returns a static-storage string for `mode`.
[[nodiscard]] const char* VfxStencilWriteModeToString(VfxStencilWriteMode mode) noexcept;

/// Parses a mode string (`"disabled"`, `"replace"`, `"increment"`).
/// Returns true on success; leaves `out` untouched on failure.
[[nodiscard]] bool VfxStencilWriteModeFromString(const char* name,
                                                   VfxStencilWriteMode& out) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Post-process stencil mask helper
// ─────────────────────────────────────────────────────────────────────────────

/// Evaluates whether a PP pass should run at a pixel given the stencil
/// sample and the emitter's target ref. Mirrors the shader-side compare.
///
/// - `mode == Replace`  → `sampledStencil == stencilRef`
/// - `mode == Increment`→ `sampledStencil >= stencilRef` (approximate —
///   reveals cumulative emitter coverage).
/// - `mode == Disabled` → `false` (no mask, PP should skip).
[[nodiscard]] bool VfxStencilMaskMatches(uint8_t sampledStencil,
                                           const VfxStencilConfig& cfg) noexcept;

} // namespace uz
