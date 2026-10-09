#include "vfx_spline_di.h"

#include <algorithm>
#include <cmath>

namespace uz {

namespace {

// ── Vec3 helpers (local only to keep the header minimal) ──────────────────

Vec3 VecAdd(const Vec3& a, const Vec3& b) noexcept {
    return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}
Vec3 VecSub(const Vec3& a, const Vec3& b) noexcept {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}
Vec3 VecScale(const Vec3& a, float s) noexcept {
    return Vec3{a.x * s, a.y * s, a.z * s};
}
float VecLength(const Vec3& v) noexcept {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
Vec3 VecNormalizeOr(const Vec3& v, const Vec3& fallback) noexcept {
    const float len = VecLength(v);
    if (!(len > 0.0f)) return fallback;
    return VecScale(v, 1.0f / len);
}

/// Resolves a control-point index for the phantom-endpoint logic.
/// For looping splines indices wrap; for open splines the end control
/// points are mirrored (reflected through p0 / pN-1).
Vec3 GetControl(const std::vector<Vec3>& pts, int index, bool looping) noexcept {
    const int n = static_cast<int>(pts.size());
    if (n <= 0) return Vec3{};
    if (looping) {
        int wrapped = index % n;
        if (wrapped < 0) wrapped += n;
        return pts[wrapped];
    }
    if (index < 0) {
        // Reflect through pts[0]: p_{-1} = pts[0] + (pts[0] - pts[1])
        if (n >= 2) return VecAdd(pts[0], VecSub(pts[0], pts[1]));
        return pts[0];
    }
    if (index >= n) {
        if (n >= 2) return VecAdd(pts[n - 1], VecSub(pts[n - 1], pts[n - 2]));
        return pts[n - 1];
    }
    return pts[index];
}

/// Catmull-Rom segment basis at local parameter `u` in [0, 1]. Tension
/// controls curvature: 0.5 = standard, 0 = loose, 1 = chordal.
Vec3 EvaluateSegment(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3,
                     float u, float tension) noexcept {
    const float u2 = u * u;
    const float u3 = u2 * u;
    // Tension-scaled Catmull-Rom basis (Niagara uses tension=0.5).
    const float s = tension;
    const Vec3 m1 = VecScale(VecSub(p2, p0), s);
    const Vec3 m2 = VecScale(VecSub(p3, p1), s);

    // Hermite blending: h00 p1 + h01 p2 + h10 m1 + h11 m2.
    const float h00 =  2.0f * u3 - 3.0f * u2 + 1.0f;
    const float h01 = -2.0f * u3 + 3.0f * u2;
    const float h10 =  u3 - 2.0f * u2 + u;
    const float h11 =  u3 - u2;

    Vec3 out = VecScale(p1, h00);
    out = VecAdd(out, VecScale(p2, h01));
    out = VecAdd(out, VecScale(m1, h10));
    out = VecAdd(out, VecScale(m2, h11));
    return out;
}

/// Analytical tangent of the Hermite blend (derivative w.r.t. u).
Vec3 EvaluateSegmentTangent(const Vec3& p0, const Vec3& p1,
                            const Vec3& p2, const Vec3& p3,
                            float u, float tension) noexcept {
    const float u2 = u * u;
    const float s = tension;
    const Vec3 m1 = VecScale(VecSub(p2, p0), s);
    const Vec3 m2 = VecScale(VecSub(p3, p1), s);

    // Derivatives of Hermite bases.
    const float dh00 =  6.0f * u2 - 6.0f * u;
    const float dh01 = -6.0f * u2 + 6.0f * u;
    const float dh10 =  3.0f * u2 - 4.0f * u + 1.0f;
    const float dh11 =  3.0f * u2 - 2.0f * u;

    Vec3 out = VecScale(p1, dh00);
    out = VecAdd(out, VecScale(p2, dh01));
    out = VecAdd(out, VecScale(m1, dh10));
    out = VecAdd(out, VecScale(m2, dh11));
    return out;
}

} // namespace

// ── Public API ────────────────────────────────────────────────────────────

VfxSplineSample VfxSpline::Sample(float t) const noexcept {
    const int n = static_cast<int>(controlPoints.size());
    if (n < 2) {
        VfxSplineSample s;
        if (n == 1) s.position = controlPoints[0];
        return s;
    }

    // Clamp open splines; wrap looping splines.
    if (looping) {
        t = t - std::floor(t);  // wrap to [0, 1)
    } else {
        t = std::clamp(t, 0.0f, 1.0f);
    }

    // Number of segments: open = N-1, looping = N.
    const int numSegments = looping ? n : (n - 1);
    // Scale t into segment space.
    const float tSeg = t * static_cast<float>(numSegments);
    int segIndex = static_cast<int>(std::floor(tSeg));
    if (segIndex >= numSegments) segIndex = numSegments - 1;
    const float u = tSeg - static_cast<float>(segIndex);

    const Vec3 p0 = GetControl(controlPoints, segIndex - 1, looping);
    const Vec3 p1 = GetControl(controlPoints, segIndex,     looping);
    const Vec3 p2 = GetControl(controlPoints, segIndex + 1, looping);
    const Vec3 p3 = GetControl(controlPoints, segIndex + 2, looping);

    VfxSplineSample out;
    out.position = EvaluateSegment(p0, p1, p2, p3, u, tension);
    const Vec3 rawTangent = EvaluateSegmentTangent(p0, p1, p2, p3, u, tension);
    // Fallback to a default forward vector if the tangent is degenerate
    // (e.g. tension=0 at coincident control points).
    out.tangent = VecNormalizeOr(rawTangent, Vec3{1.0f, 0.0f, 0.0f});
    return out;
}

void VfxSpline::BuildArcLengthTable() const {
    const uint32_t samples = (arcLengthSamples < 2u) ? 2u : arcLengthSamples;
    arcLengthTable.assign(samples + 1u, 0.0f);

    if (controlPoints.size() < 2) {
        totalLength = 0.0f;
        return;
    }

    Vec3 prev = Sample(0.0f).position;
    float accum = 0.0f;
    arcLengthTable[0] = 0.0f;
    for (uint32_t i = 1; i <= samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(samples);
        const Vec3 curr = Sample(t).position;
        accum += VecLength(VecSub(curr, prev));
        arcLengthTable[i] = accum;
        prev = curr;
    }
    totalLength = accum;
}

VfxSplineSample VfxSpline::SampleUniform(float t) const {
    if (arcLengthTable.empty()) {
        BuildArcLengthTable();
    }
    if (controlPoints.size() < 2 || !(totalLength > 0.0f)) {
        return Sample(t);
    }
    t = std::clamp(t, 0.0f, 1.0f);
    const float targetDist = t * totalLength;

    // Binary search for the bucket that brackets targetDist.
    const uint32_t buckets = static_cast<uint32_t>(arcLengthTable.size()) - 1u;
    uint32_t lo = 0;
    uint32_t hi = buckets;
    while (lo + 1 < hi) {
        const uint32_t mid = (lo + hi) / 2;
        if (arcLengthTable[mid] <= targetDist) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    // Linear interpolation inside the bucket to recover parameter t'.
    const float dLo   = arcLengthTable[lo];
    const float dHi   = arcLengthTable[lo + 1];
    const float frac  = (dHi > dLo) ? (targetDist - dLo) / (dHi - dLo) : 0.0f;
    const float tPrime = (static_cast<float>(lo) + frac) /
                        static_cast<float>(buckets);
    return Sample(tPrime);
}

} // namespace uz
