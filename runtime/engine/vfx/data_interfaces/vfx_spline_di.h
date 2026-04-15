#pragma once

/// Catmull-Rom spline data interface for VFX simulation shaders.
///
/// Path-following effects — an energy tether, homing missile trails,
/// ribbon-guided sparks — need to sample a spline from the simulation
/// shader. Niagara exposes `UNiagaraDataInterfaceSpline` with
/// `GetSplineLength` / `GetSplinePosition(distance)` /
/// `GetSplineTangent`. UzEngine's equivalent lives here.
///
/// # Representation
///
/// Centripetal Catmull-Rom splines through the control points. Each
/// segment has four control influences (prev, a, b, next); the spline
/// passes through every `a` / `b`. Segments are evaluated with parameter
/// `u` in `[0, 1]`; the global parameter `t` in `[0, 1]` distributes
/// across all N-1 segments uniformly in control-index space (not
/// arc-length — callers that need uniform-speed traversal use
/// `SampleUniform(t)` which walks the arc-length table).
///
/// # Control points
///
/// A valid spline needs at least 2 control points. The evaluator adds
/// phantom endpoints (mirroring the first/last segments) so the curve
/// starts at control[0] and ends at control[N-1] with finite tangents.
///
/// # Example
///
/// ```cpp
/// VfxSpline s;
/// s.controlPoints = { {0,0,0}, {100,0,50}, {200,0,0}, {300,0,50} };
/// const auto sample = s.Sample(0.5f); // mid-spline
/// // sample.position  — Vec3 on the curve
/// // sample.tangent   — unit tangent vector
/// ```
///
/// @ref UE5 UNiagaraDataInterfaceSpline
/// @ref Catmull & Rom 1974 "A class of local interpolating splines"
/// @ref Yuksel et al. 2011 "Parameterization and Applications of Catmull–Rom Curves"

#include "vector3.h"

#include <cstdint>
#include <vector>

namespace uz {

/// Single spline sample — position + unit tangent at a given parameter.
struct VfxSplineSample {
    Vec3 position {};
    Vec3 tangent  {};   ///< Unit-length when the curve has at least 2 points.
};

/// Catmull-Rom spline over a variable number of control points.
///
/// # Thread safety
/// Read-only access (Sample / SampleUniform) is thread-safe as long as no
/// writer mutates `controlPoints` concurrently. The arc-length table is
/// rebuilt on demand via `BuildArcLengthTable` and mutates internal state,
/// so its first call must be serialised.
struct VfxSpline {
    /// Authored control points in order. Must contain at least 2 entries
    /// for any evaluation to succeed.
    std::vector<Vec3> controlPoints;

    /// Catmull-Rom tension. 0.5 (default) matches Niagara. 0 = uniform
    /// Catmull-Rom (loose); 1 = chordal (tight corners).
    float tension = 0.5f;

    /// Whether the spline loops (last control connects back to first).
    /// When true the phantom-endpoint logic wraps instead of mirroring.
    bool looping = false;

    /// Cached arc-length table built by `BuildArcLengthTable`. Entries are
    /// cumulative lengths at uniformly-sampled parameter `t`. Used by
    /// `SampleUniform` to convert arc-length to parameter.
    mutable std::vector<float> arcLengthTable;

    /// Resolution of the arc-length table. Higher values = more accurate
    /// uniform-speed sampling at the cost of build time. 64 is usually enough.
    uint32_t arcLengthSamples = 64;

    /// Total arc length of the spline (sum of segment lengths). Valid only
    /// after `BuildArcLengthTable` has been called.
    mutable float totalLength = 0.0f;

    /// Evaluates the spline at parameter `t` in `[0, 1]` distributed
    /// uniformly across control indices (NOT arc-length).
    ///
    /// Out-of-range inputs clamp. Returns `{}` when the spline has fewer
    /// than 2 control points.
    [[nodiscard]] VfxSplineSample Sample(float t) const noexcept;

    /// Evaluates at arc-length-parameterised `t` in `[0, 1]`. Requires
    /// `BuildArcLengthTable` to have run at least once (build-on-demand
    /// if empty).
    [[nodiscard]] VfxSplineSample SampleUniform(float t) const;

    /// Rebuilds the arc-length table. Call after mutating `controlPoints`
    /// or `tension` for `SampleUniform` to reflect the new geometry.
    void BuildArcLengthTable() const;
};

} // namespace uz
