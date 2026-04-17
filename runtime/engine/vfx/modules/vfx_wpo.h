#pragma once

/// VFX World Position Offset (WPO) — vertex-shader deformation pin.
///
/// Materials can author an HLSL expression that evaluates to a world-space
/// offset added to the particle's vertex position. Enables organic /
/// magical effects (breathing meshes, twisted magic, swimming sprites) by
/// deforming geometry at the vertex stage without CPU-side mesh editing.
///
/// # Splice contract
///
/// The material compiler produces an HLSL snippet of the form:
///
/// ```hlsl
/// float3 EvaluateWPO(float3 objectPos, float3 worldPos, float time, float normalizedAge) {
///     return <author expression>;  // e.g. sin(time + worldPos.x) * 0.1
/// }
/// ```
///
/// `VfxWPOSpliceVS` inlines a call to `EvaluateWPO` into the particle VS
/// immediately before the `viewProj` multiply, so the offset lands in
/// world space. `VfxWPOSplicePrevVS` emits the same call using the
/// previous-frame time and age so the motion-vector output (phase 8.1.0)
/// reprojects TAA correctly.
///
/// When the material has no WPO expression authored (empty source), the
/// splice emits a no-op (`float3 wpo = 0;`) so the codegen is idempotent
/// across materials with and without WPO.
///
/// @ref UE5 FMaterialAttributes::WorldPositionOffset
/// @ref docs/engine/25-vfx/35-world-position-offset.md

#include <cstdint>
#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Attributes
// ─────────────────────────────────────────────────────────────────────────────

/// Material WPO attribute. The compiler populates `hlslExpression` with
/// HLSL code that evaluates to a float3 world-space offset. Inputs are
/// all float/float3 with stable names:
///
/// | Name              | Type   | Source                                  |
/// |-------------------|--------|------------------------------------------|
/// | `objectPos`       | float3 | Vertex position in object space.         |
/// | `worldPos`        | float3 | Vertex position in world space.          |
/// | `time`            | float  | Global total time (seconds).             |
/// | `normalizedAge`   | float  | Particle age / lifetime, clamped [0, 1]. |
///
/// The expression must be pure (no side effects) and return a float3.
struct VfxWPOMaterialAttributes {
    std::string hlslExpression; ///< Empty = WPO disabled (identity).

    /// Returns true when the material authors a WPO expression. The
    /// compiler uses this to select the WPO-enabled shader permutation.
    [[nodiscard]] bool IsEnabled() const noexcept {
        return !hlslExpression.empty();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Splice helpers — generate HLSL snippets for VS codegen
// ─────────────────────────────────────────────────────────────────────────────

/// Emits the `EvaluateWPO` function body. When `attrs` is disabled the
/// returned snippet is `return float3(0, 0, 0);` so the caller can splice
/// unconditionally without branching on the attribute state.
[[nodiscard]] std::string VfxWPOEmitEvaluateBody(const VfxWPOMaterialAttributes& attrs);

/// Emits the VS splice that applies the WPO to the current-frame vertex
/// position. Call sites wrap the resulting snippet with the positional
/// variables the shader has in scope.
///
/// Output pattern:
/// ```hlsl
/// float3 wpo = EvaluateWPO(objectPos, worldPos, time, normalizedAge);
/// worldPos += wpo;
/// ```
///
/// Disabled materials emit a pure no-op (no `+=` at all) so the
/// optimiser elides the computation without relying on constant folding.
[[nodiscard]] std::string VfxWPOSpliceVS(const VfxWPOMaterialAttributes& attrs);

/// Emits the VS splice for the previous-frame position used by the
/// motion-vector output (phase 8.1.0). Same pattern as `VfxWPOSpliceVS`
/// but reads `prevTime` and `prevNormalizedAge` so TAA reprojection
/// matches the current frame's deformation.
[[nodiscard]] std::string VfxWPOSplicePrevVS(const VfxWPOMaterialAttributes& attrs);

// ─────────────────────────────────────────────────────────────────────────────
// CPU evaluator — used by tests to sanity-check the math without a GPU
// ─────────────────────────────────────────────────────────────────────────────

/// Minimal CPU WPO evaluator for a small closed set of built-in
/// expressions. Tests validate the plumbing; production uses HLSL.
///
/// | Expression ID        | Formula                                            |
/// |----------------------|----------------------------------------------------|
/// | `"zero"`             | `(0, 0, 0)`                                        |
/// | `"sin_time_x"`       | `(0, 0, sin(time + worldPos.x))`                   |
/// | `"breathe"`          | `worldPos · (1 + 0.1·sin(time))   − worldPos`      |
/// | `"twist"`            | axis-aligned z-twist by `normalizedAge · 2π`        |
///
/// Unknown ids return zero so the evaluator is safe for fuzz-like tests.
struct Vec3;
[[nodiscard]] Vec3 VfxEvaluateWPOBuiltin(const char* exprId,
                                           const Vec3& objectPos,
                                           const Vec3& worldPos,
                                           float time,
                                           float normalizedAge) noexcept;

} // namespace uz
