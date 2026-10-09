#pragma once

/// Tangent-mode Hermite curve — phase 7.16.0 foundation for the curve editor.
///
/// The existing `VFXCurve` (this directory's `vfx_curve.h`) delivers float +
/// Vec4 curves with Linear / Catmull-Rom / Smoothstep modes for in-engine
/// evaluation and GPU texture baking. It does NOT carry per-key in/out
/// tangents — a curve-editor UX requirement for expressive authoring
/// (pulses, cooldowns, broken-tangent corners).
///
/// `VfxTangentCurve` ships that richer authoring model alongside the
/// existing evaluator. Tangent-mode keys coexist in the `.uzcurve` JSON
/// asset so the editor widget (phase 7.16.1 follow-up) can round-trip
/// authored shapes without the legacy Linear/Catmull-Rom enum.
///
/// # Evaluation
///
/// Adjacent keys define a segment with local parameter `u = (t - t0) / (t1 - t0)`
/// in `[0, 1]`. The Hermite basis uses:
///   - `P0, P1` = segment endpoints
///   - `M0 = outTangent[i]   * (t1 - t0)`
///   - `M1 = inTangent [i+1] * (t1 - t0)`
///
/// Tangents carry the same units as `dValue/dTime` expressed in the
/// normalised curve domain — the `(t1 - t0)` scaling is applied here so
/// authored tangent values remain legible regardless of key spacing.
///
/// Out-of-range `t` clamps to the first/last key value (no linear
/// extrapolation). Empty curves evaluate to 0. Single-key curves evaluate
/// to that key's value.
///
/// # Legacy triplet migration
///
/// `SetFromLegacyTriplet(start, peak, end, peakTime)` produces a 3-key
/// tangent curve with auto-smoothed continuity — used by the preset
/// loader when a `.uzfx` file stores `sizeOverLifeStart/Peak/End` but no
/// `.uzcurve` reference.
///
/// @ref UE5 SNiagaraCurveEditor (UI port deferred to 7.16.1)
/// @ref Hermite basis derivation: Foley/vanDam/Feiner/Hughes, Computer
///      Graphics — Principles and Practice, Ch 11.

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

class IVFS;

/// Per-key tangent semantics.
enum class VfxTangentMode : uint8_t {
    /// Automatically smoothed across neighbours (Catmull-Rom-style
    /// average of adjacent segment slopes). Default for new keys.
    Auto    = 0,

    /// Straight-line segments: in/outTangent forced to 0 at evaluate time.
    Linear  = 1,

    /// `inTangent == outTangent`; editor shows one handle per key.
    User    = 2,

    /// `inTangent != outTangent`; editor shows two handles per key.
    /// Produces sharp corners.
    Broken  = 3,
};

/// Single curve keyframe.
struct VfxTangentKey {
    float time        = 0.0f;
    float value       = 0.0f;
    float inTangent   = 0.0f;
    float outTangent  = 0.0f;
    VfxTangentMode mode = VfxTangentMode::Auto;
};

/// Tangent-mode Hermite curve.
struct VfxTangentCurve {
    std::vector<VfxTangentKey> keys;

    /// Inserts a key preserving the sort-by-time invariant.
    uint32_t AddKey(const VfxTangentKey& key);

    /// Convenience: append a linear key (zero tangents, Linear mode).
    void AddLinearKey(float time, float value);

    /// Empties every key.
    void Clear() noexcept { keys.clear(); }

    /// Evaluates the curve at time `t`. Clamps to edge keys outside the
    /// authored range; returns 0 for empty curves.
    [[nodiscard]] float Evaluate(float t) const noexcept;

    [[nodiscard]] bool IsEmpty() const noexcept { return keys.empty(); }

    /// Promotes legacy `start / peak / end` triplet fields into a 3-key
    /// auto-tangent curve with peak at `peakTime`. Replaces existing keys.
    void SetFromLegacyTriplet(float startValue, float peakValue, float endValue,
                              float peakTime) noexcept;
};

/// `.uzcurve` JSON asset operating on `VfxTangentCurve`.
///
/// Format:
/// ```json
/// {
///   "format":  "uzcurve",
///   "version": 1,
///   "keys": [
///     {"t": 0.0, "v": 0.0, "mode": "auto"},
///     {"t": 0.5, "v": 1.0, "mode": "user",   "in":  2.0, "out":  2.0},
///     {"t": 1.0, "v": 0.0, "mode": "broken", "in": -3.0, "out":  0.0}
///   ]
/// }
/// ```
struct VfxTangentCurveAsset {
    [[nodiscard]] static bool LoadFromString(const std::string& jsonStr,
                                             VfxTangentCurve& outCurve);
    [[nodiscard]] static bool Load(const std::string& path,
                                   VfxTangentCurve& outCurve,
                                   IVFS* vfs = nullptr);

    [[nodiscard]] static std::string SaveToString(const VfxTangentCurve& curve);
    [[nodiscard]] static bool SaveToFile(const VfxTangentCurve& curve,
                                         const std::string& path);
};

/// Tangent-mode string round-trip helpers.
[[nodiscard]] const char* VfxTangentModeToString(VfxTangentMode mode) noexcept;
[[nodiscard]] bool VfxTangentModeFromString(const std::string& s,
                                             VfxTangentMode& out) noexcept;

} // namespace uz
