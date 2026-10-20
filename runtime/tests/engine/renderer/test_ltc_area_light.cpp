/// Rect area lights (phase3a 3.33.0 stage 1): the embedded LTC tables, a C++ port of the HLSL quad
/// evaluator (ltc.hlsli: LtcIntegrateEdge / LtcClipQuadToHorizon / LtcEvaluate) checked against the
/// analytic form factor and brute-force integration, and the LightingCB / RectLightData layout.
///
/// @ref Heitz, Dupuy, Hill, Neubelt 2016 "Real-Time Polygonal-Light Shading with Linearly Transformed Cosines"

#include <gtest/gtest.h>

#include "ltc_lut.h"
#include "constant_buffers.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

using namespace uz;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct D3 {
    double x = 0.0, y = 0.0, z = 0.0;
};
D3 operator+(D3 a, D3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
D3 operator-(D3 a, D3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
D3 operator*(D3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
D3 operator-(D3 a) { return {-a.x, -a.y, -a.z}; }
double Dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
D3 Cross(D3 a, D3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double Length(D3 a) { return std::sqrt(Dot(a, a)); }
D3 Normalize(D3 a) { const double l = Length(a); return l > 0.0 ? a * (1.0 / l) : a; }

/// Row-major 3x3.
struct M3 {
    double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    D3 operator*(D3 v) const {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
    M3 operator*(const M3& o) const {
        M3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j];
        return r;
    }
    double Det() const {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
             - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
             + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    }
};

/// ltc.hlsli LtcInverseMatrix: (invM[0][0], invM[0][2], invM[2][0], invM[2][2]) column-major = (m00, m20, m02, m22).
M3 LtcInverseMatrix(const float* t) {
    M3 r;
    r.m[0][0] = t[0]; r.m[0][1] = 0.0; r.m[0][2] = t[2];
    r.m[1][0] = 0.0;  r.m[1][1] = 1.0; r.m[1][2] = 0.0;
    r.m[2][0] = t[1]; r.m[2][1] = 0.0; r.m[2][2] = t[3];
    return r;
}

const float* MatrixCell(uint32_t t, uint32_t a) { return &kLtcMatrixTable[(t * kLtcLutSize + a) * 4]; }
const float* MagnitudeCell(uint32_t t, uint32_t a) { return &kLtcMagnitudeTable[(t * kLtcLutSize + a) * 2]; }

// ── Port of ltc.hlsli ─────────────────────────────────────────────────────────

double LtcIntegrateEdge(D3 v1, D3 v2) {
    double cosTheta = Dot(v1, v2);
    cosTheta = cosTheta < -0.9999 ? -0.9999 : (cosTheta > 0.9999 ? 0.9999 : cosTheta);
    const double theta = std::acos(cosTheta);
    return Cross(v1, v2).z * ((theta > 0.001) ? theta / std::sin(theta) : 1.0);
}

void LtcClipQuadToHorizon(std::array<D3, 5>& L, int& n) {
    int config = 0;
    if (L[0].z > 0.0) config += 1;
    if (L[1].z > 0.0) config += 2;
    if (L[2].z > 0.0) config += 4;
    if (L[3].z > 0.0) config += 8;
    n = 0;
    switch (config) {
    case 0: break;
    case 1:  n = 3; L[1] = -L[0] * L[1].z + L[1] * L[0].z; L[2] = -L[0] * L[3].z + L[3] * L[0].z; break;
    case 2:  n = 3; L[0] = -L[1] * L[0].z + L[0] * L[1].z; L[2] = -L[1] * L[2].z + L[2] * L[1].z; break;
    case 3:  n = 4; L[2] = -L[1] * L[2].z + L[2] * L[1].z; L[3] = -L[0] * L[3].z + L[3] * L[0].z; break;
    case 4:  n = 3; L[0] = -L[2] * L[3].z + L[3] * L[2].z; L[1] = -L[2] * L[1].z + L[1] * L[2].z; break;
    case 5:  n = 0; break;
    case 6:  n = 4; L[0] = -L[1] * L[0].z + L[0] * L[1].z; L[3] = -L[2] * L[3].z + L[3] * L[2].z; break;
    case 7:  n = 5; L[4] = -L[0] * L[3].z + L[3] * L[0].z; L[3] = -L[2] * L[3].z + L[3] * L[2].z; break;
    case 8:  n = 3; L[0] = -L[3] * L[0].z + L[0] * L[3].z; L[1] = -L[3] * L[2].z + L[2] * L[3].z; L[2] = L[3]; break;
    case 9:  n = 4; L[1] = -L[0] * L[1].z + L[1] * L[0].z; L[2] = -L[3] * L[2].z + L[2] * L[3].z; break;
    case 10: n = 0; break;
    case 11: n = 5; L[4] = L[3]; L[3] = -L[2] * L[3].z + L[3] * L[2].z; L[2] = -L[1] * L[2].z + L[2] * L[1].z; break;
    case 12: n = 4; L[1] = -L[2] * L[1].z + L[1] * L[2].z; L[0] = -L[3] * L[0].z + L[0] * L[3].z; break;
    case 13: n = 5; L[4] = L[3]; L[3] = L[2]; L[2] = -L[1] * L[2].z + L[2] * L[1].z; L[1] = -L[0] * L[1].z + L[1] * L[0].z; break;
    case 14: n = 5; L[4] = -L[3] * L[0].z + L[0] * L[3].z; L[0] = -L[1] * L[0].z + L[0] * L[1].z; break;
    default: n = 4; break;
    }
    if (n == 3) L[3] = L[0];
    if (n == 4) L[4] = L[0];
}

struct Quad {
    D3 p[4];
};

/// Vertex order of ShadeRectLight: c - ex - ey, c + ex - ey, c + ex + ey, c - ex + ey (emits along cross(ex, ey)).
Quad MakeQuad(D3 c, D3 right, double hw, D3 up, double hh) {
    const D3 ex = right * hw, ey = up * hh;
    return {{c - ex - ey, c + ex - ey, c + ex + ey, c - ex + ey}};
}

double LtcEvaluate(D3 N, D3 V, D3 P, const M3& Minv, const Quad& q, bool twoSided) {
    D3 T1 = V - N * Dot(V, N);
    if (Dot(T1, T1) < 1e-8) {
        T1 = std::fabs(N.z) < 0.999 ? D3{0, 0, 1} : D3{1, 0, 0};
        T1 = Normalize(T1 - N * Dot(T1, N));
    } else {
        T1 = Normalize(T1);
    }
    const D3 T2 = Cross(N, T1);
    M3 toLocal;
    toLocal.m[0][0] = T1.x; toLocal.m[0][1] = T1.y; toLocal.m[0][2] = T1.z;
    toLocal.m[1][0] = T2.x; toLocal.m[1][1] = T2.y; toLocal.m[1][2] = T2.z;
    toLocal.m[2][0] = N.x;  toLocal.m[2][1] = N.y;  toLocal.m[2][2] = N.z;
    const M3 M = Minv * toLocal;

    std::array<D3, 5> L{};
    for (int i = 0; i < 4; ++i) L[i] = M * (q.p[i] - P);
    L[4] = L[0];
    int n = 0;
    LtcClipQuadToHorizon(L, n);
    if (n == 0) return 0.0;
    for (auto& v : L) v = Normalize(v);
    double sum = LtcIntegrateEdge(L[0], L[1]) + LtcIntegrateEdge(L[1], L[2]) + LtcIntegrateEdge(L[2], L[3]);
    if (n >= 4) sum += LtcIntegrateEdge(L[3], L[4]);
    if (n == 5) sum += LtcIntegrateEdge(L[4], L[0]);
    sum = twoSided ? std::fabs(sum) : std::fmax(0.0, -sum);
    return sum / (2.0 * kPi);
}

// ── References ───────────────────────────────────────────────────────────────

/// Form factor from a point directly below the centre of a parallel rectangle of half extents hw x hh
/// at height h (four corner quadrants of the standard differential-element-to-rectangle view factor).
double AnalyticCentreFormFactor(double hw, double hh, double h) {
    const double A = hw / h, B = hh / h;
    const double f = (A / std::sqrt(1 + A * A) * std::atan(B / std::sqrt(1 + A * A))
                    + B / std::sqrt(1 + B * B) * std::atan(A / std::sqrt(1 + B * B))) / (2.0 * kPi);
    return 4.0 * f;
}

/// Brute-force integral over the one-sided quad of the lobe transformed by Minv in the (T1, T2, N) frame
/// of the receiver: (1/pi) * sum max(0, cos_o) * |det Minv| / |Minv w|^3 * cos_e / d^2 dA. With Minv = I this
/// is the plain form factor.
double BruteForce(D3 N, D3 V, D3 P, const M3& Minv, D3 c, D3 right, double hw, D3 up, double hh, int res) {
    D3 T1 = Normalize(V - N * Dot(V, N));
    const D3 T2 = Cross(N, T1);
    const D3 emitN = Cross(right, up);
    const double dA = (2.0 * hw / res) * (2.0 * hh / res);
    const double det = std::fabs(Minv.Det());
    double sum = 0.0;
    for (int i = 0; i < res; ++i) {
        for (int j = 0; j < res; ++j) {
            const double u = (i + 0.5) / res * 2.0 - 1.0, v = (j + 0.5) / res * 2.0 - 1.0;
            const D3 pt = c + right * (u * hw) + up * (v * hh);
            const D3 d = pt - P;
            const double dist2 = Dot(d, d);
            const D3 w = d * (1.0 / std::sqrt(dist2));
            const double cosE = std::fmax(0.0, Dot(-w, emitN));
            if (cosE <= 0.0) continue;
            const D3 wl{Dot(T1, w), Dot(T2, w), Dot(N, w)};
            const D3 wo = Minv * wl;
            const double lo = Length(wo);
            const double cosO = std::fmax(0.0, wo.z / lo);
            const double density = cosO / kPi * det / (lo * lo * lo);
            sum += density * cosE / dist2 * dA;
        }
    }
    return sum;
}

const M3 kIdentity{};

} // namespace

// ── Tables ───────────────────────────────────────────────────────────────────

TEST(LtcLutTest, TablesAreFiniteAndMagnitudeIsBounded) {
    for (uint32_t i = 0; i < kLtcLutSize * kLtcLutSize * 4; ++i) {
        ASSERT_TRUE(std::isfinite(kLtcMatrixTable[i])) << "matrix entry " << i;
    }
    for (uint32_t t = 0; t < kLtcLutSize; ++t) {
        for (uint32_t a = 0; a < kLtcLutSize; ++a) {
            const float* m = MagnitudeCell(t, a);
            ASSERT_TRUE(std::isfinite(m[0]) && std::isfinite(m[1])) << "t=" << t << " a=" << a;
            // The directional albedo of GGX (no Fresnel) is at most 1 and never below 1 - ln 2 (its value at
            // alpha = 1, normal incidence) within sampling noise; the Schlick-weighted albedo cannot exceed it.
            EXPECT_GT(m[0], 0.30f) << "t=" << t << " a=" << a;
            EXPECT_LE(m[0], 1.0001f) << "t=" << t << " a=" << a;
            EXPECT_GE(m[1], 0.0f) << "t=" << t << " a=" << a;
            EXPECT_LE(m[1], m[0] + 1e-5f) << "t=" << t << " a=" << a;
        }
    }
}

TEST(LtcLutTest, MatrixIsOrientationPreservingInTheUsableDomain) {
    // roughness >= 0.2 (column 13 on): the engine clamps roughness to sqrt(r.Material.MinGGXAlpha).
    for (uint32_t t = 0; t < kLtcLutSize; ++t) {
        for (uint32_t a = 13; a < kLtcLutSize; ++a) {
            const M3 inv = LtcInverseMatrix(MatrixCell(t, a));
            EXPECT_GT(inv.Det(), 0.0) << "t=" << t << " a=" << a;
            EXPECT_GT(inv.m[0][0], 0.0) << "t=" << t << " a=" << a;
            EXPECT_GT(inv.m[2][2], 0.0) << "t=" << t << " a=" << a;
        }
    }
}

TEST(LtcLutTest, KnownCellsMatchTheory) {
    // alpha = 1 at normal incidence: D = 1/pi, G2 = 2 mu / (1 + mu) -> albedo = integral mu/(1+mu) = 1 - ln 2.
    EXPECT_NEAR(MagnitudeCell(0, 63)[0], 1.0 - std::log(2.0), 2e-3);
    // alpha -> 0 at normal incidence: a mirror, albedo 1 and no Schlick term.
    EXPECT_NEAR(MagnitudeCell(0, 0)[0], 1.0, 1e-3);
    EXPECT_NEAR(MagnitudeCell(0, 0)[1], 0.0, 1e-3);
    // Normal incidence (row 0): rotationally symmetric lobes, no x/z shear, m00 normalised to 1.
    for (uint32_t a = 0; a < kLtcLutSize; ++a) {
        const float* m = MatrixCell(0, a);
        EXPECT_FLOAT_EQ(m[0], 1.0f) << "a=" << a;
        EXPECT_FLOAT_EQ(m[1], 0.0f) << "a=" << a;
        EXPECT_FLOAT_EQ(m[2], 0.0f) << "a=" << a;
    }
    // Lobes narrow with decreasing roughness: at normal incidence the stored z entry is the lobe scale
    // m11 of M (M^-1 = diag(1/m11, 1/m11, 1) normalised), which shrinks towards the mirror end.
    EXPECT_GT(MatrixCell(0, 63)[3], MatrixCell(0, 32)[3]);
    EXPECT_GT(MatrixCell(0, 32)[3], MatrixCell(0, 13)[3]);
}

// ── Evaluator ────────────────────────────────────────────────────────────────

TEST(LtcEvaluateTest, NormalIncidenceRectMatchesAnalyticFormFactor) {
    // The test scene's light: 120 x 80 cm, 200 cm above the floor, facing down (right = +Y, up = +X).
    const D3 right{0, 1, 0}, up{1, 0, 0}, c{0, 0, 200};
    const double hw = 60.0, hh = 40.0;
    const Quad q = MakeQuad(c, right, hw, up, hh);
    const D3 N{0, 0, 1}, P{0, 0, 0};
    const D3 V = Normalize(D3{0.3, -0.5, 0.8});
    const double analytic = AnalyticCentreFormFactor(hw, hh, 200.0);
    const double ltc = LtcEvaluate(N, V, P, kIdentity, q, false);
    EXPECT_NEAR(ltc, analytic, 1e-6);
    EXPECT_NEAR(analytic, 0.070344, 1e-5);   // the value the scene calibration was derived from
    EXPECT_NEAR(BruteForce(N, V, P, kIdentity, c, right, hw, up, hh, 400), analytic, 1e-4);
}

TEST(LtcEvaluateTest, HorizonClippingMatchesBruteForce) {
    const D3 right{0, 1, 0}, up{1, 0, 0}, c{0, 0, 100};
    const double hw = 60.0, hh = 40.0;
    const Quad q = MakeQuad(c, right, hw, up, hh);
    const D3 P{0, 0, 0};
    struct Case { D3 n; int expectAbove; };
    const Case cases[] = {
        {{1.0, 0.5, 0.2}, 3}, {{1.0, 0.0, 0.2}, 2}, {{1.0, 0.5, -0.2}, 1}, {{0.0, 1.0, 0.3}, 2}, {{0.3, 0.2, 1.0}, 4},
    };
    for (const Case& cs : cases) {
        const D3 N = Normalize(cs.n);
        const D3 V = Normalize(N + D3{0.2, -0.1, 0.3});
        int above = 0;
        for (const D3& p : q.p) above += Dot(p - P, N) > 0.0 ? 1 : 0;
        ASSERT_EQ(above, cs.expectAbove);
        const double ltc = LtcEvaluate(N, V, P, kIdentity, q, false);
        const double ref = BruteForce(N, V, P, kIdentity, c, right, hw, up, hh, 800);
        EXPECT_GT(ltc, 0.005);
        EXPECT_NEAR(ltc, ref, 1e-4) << "vertices above the horizon: " << above;
    }
}

TEST(LtcEvaluateTest, OneSidedEmitsOnlyTowardsItsNormal) {
    const D3 right{0, 1, 0}, up{1, 0, 0}, c{0, 0, 200};
    const Quad q = MakeQuad(c, right, 60.0, up, 40.0);
    // Receiver above the rect, looking down at its back face.
    const D3 P{10, 5, 400}, N{0, 0, -1};
    const D3 V = Normalize(D3{0.1, 0.0, -1.0});
    EXPECT_DOUBLE_EQ(LtcEvaluate(N, V, P, kIdentity, q, false), 0.0);
    const double twoSided = LtcEvaluate(N, V, P, kIdentity, q, true);
    // Mirror configuration below the rect sees the same solid angle.
    const D3 Pm{10, 5, 0}, Nm{0, 0, 1};
    const D3 Vm = Normalize(D3{0.1, 0.0, 1.0});
    EXPECT_NEAR(twoSided, LtcEvaluate(Nm, Vm, Pm, kIdentity, q, false), 1e-9);
    EXPECT_GT(twoSided, 0.05);
}

TEST(LtcEvaluateTest, HemisphereIntegratesToOne) {
    const D3 right{0, 1, 0}, up{1, 0, 0}, c{0, 0, 10};
    const Quad q = MakeQuad(c, right, 1e7, up, 1e7);
    const D3 N{0, 0, 1}, P{0, 0, 0};
    const D3 V = Normalize(D3{0.3, -0.5, 0.8});
    EXPECT_NEAR(LtcEvaluate(N, V, P, kIdentity, q, false), 1.0, 1e-5);
    // Any axis scaling keeps the horizon in place, so the transformed lobe still integrates to one.
    M3 scaled;
    scaled.m[0][0] = 0.5; scaled.m[2][2] = 2.0;
    EXPECT_NEAR(LtcEvaluate(N, V, P, scaled, q, false), 1.0, 1e-5);
    // Normal incidence takes the degenerate-tangent path.
    EXPECT_NEAR(LtcEvaluate(N, N, P, scaled, q, false), 1.0, 1e-5);
}

TEST(LtcEvaluateTest, TableLobeMatchesBruteForceOverTheRect) {
    // A real table cell (theta = 42 deg, roughness 0.51): the sheared lobe integrated over the rect
    // both analytically (the shader path) and by brute force.
    const M3 Minv = LtcInverseMatrix(MatrixCell(32, 32));
    const D3 right{0, 1, 0}, up{1, 0, 0}, c{0, 0, 150};
    const double hw = 80.0, hh = 50.0;
    const Quad q = MakeQuad(c, right, hw, up, hh);
    const D3 N{0, 0, 1}, P{40, -30, 0};
    const double theta = std::acos(1.0 - (32.0 / 63.0) * (32.0 / 63.0));
    const D3 V = Normalize(D3{std::sin(theta), 0.0, std::cos(theta)});
    const double ltc = LtcEvaluate(N, V, P, Minv, q, false);
    const double ref = BruteForce(N, V, P, Minv, c, right, hw, up, hh, 1200);
    EXPECT_GT(ltc, 0.01);
    EXPECT_NEAR(ltc, ref, 2e-3 * ref + 1e-5);
}

// ── Layout ───────────────────────────────────────────────────────────────────

TEST(RectLightLayoutTest, RectLightDataIsFiveFloat4Rows) {
    EXPECT_EQ(sizeof(RectLightData), 80u);
    EXPECT_EQ(offsetof(RectLightData, position), 0u);
    EXPECT_EQ(offsetof(RectLightData, range), 12u);
    EXPECT_EQ(offsetof(RectLightData, right), 16u);
    EXPECT_EQ(offsetof(RectLightData, halfWidth), 28u);
    EXPECT_EQ(offsetof(RectLightData, up), 32u);
    EXPECT_EQ(offsetof(RectLightData, halfHeight), 44u);
    EXPECT_EQ(offsetof(RectLightData, color), 48u);
    EXPECT_EQ(offsetof(RectLightData, flags), 60u);
    EXPECT_EQ(offsetof(RectLightData, specularScale), 64u);
    EXPECT_EQ(offsetof(RectLightData, diffuseScale), 68u);
    EXPECT_EQ(offsetof(RectLightData, shadowIndex), 72u);
    EXPECT_EQ(kRectLightFlag_TwoSided, 1u);
}

TEST(RectLightLayoutTest, LightingCBKeepsItsSizeAndPutsTheRectFieldsInTheFormerPad) {
    EXPECT_EQ(kMaxRectLights, 16u);
    EXPECT_EQ(kRectLightBufferFrames, 3u);
    EXPECT_EQ(offsetof(LightingCB, clusterViewHeight), 43084u);
    EXPECT_EQ(offsetof(LightingCB, rectLightCount), 43088u);
    EXPECT_EQ(offsetof(LightingCB, rectLightsSrv), 43092u);
    EXPECT_EQ(offsetof(LightingCB, ltcMatrixSrv), 43096u);
    EXPECT_EQ(offsetof(LightingCB, ltcMagnitudeSrv), 43100u);
    // The CB did not grow (scenes without a rect light keep their CB ring layout): block B.7's size.
    EXPECT_EQ(sizeof(LightingCB), 43264u);
    EXPECT_EQ(sizeof(LightingCB) % 256u, 0u);
    // The pre-existing fields did not move.
    EXPECT_EQ(offsetof(LightingCB, pointLightCount), 32768u);
    EXPECT_EQ(offsetof(LightingCB, spotLights), 32784u);
    EXPECT_EQ(offsetof(LightingCB, clusterForwardX), 43024u);
    LightingCB cb{};
    EXPECT_EQ(cb.rectLightCount, 0u);
    EXPECT_EQ(cb.rectLightsSrv, UINT32_MAX);
    EXPECT_EQ(cb.ltcMatrixSrv, UINT32_MAX);
    EXPECT_EQ(cb.ltcMagnitudeSrv, UINT32_MAX);
    // The per-frame buffer holds kMaxRectLights 80-byte records (the StructuredBuffer stride).
    EXPECT_EQ(sizeof(RectLightData) * kMaxRectLights, 1280u);
}
