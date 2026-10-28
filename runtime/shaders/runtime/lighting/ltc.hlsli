/// Linearly Transformed Cosines (LTC) for polygonal area lights — the rectangle case (phase3a 3.33.0).
///
/// @ref Heitz, Dupuy, Hill, Neubelt 2016 "Real-Time Polygonal-Light Shading with Linearly Transformed
///      Cosines" (SIGGRAPH 2016); the clipping and edge integration follow the authors' reference
///      shader (github.com/selfshadow/ltc_code, webgl/ltc_quad.html).
///
/// The GGX lobe at (roughness, NoV) is approximated by a clamped cosine transformed by a 3x3 matrix M
/// (fit offline by scripts/rendering/bake_ltc_lut.py, embedded in ltc_lut_data.inc). Transforming the
/// polygon's vertices by M^-1 turns the integral of the lobe over the polygon into the integral of a
/// clamped cosine over the transformed polygon, which is analytic: the polygon is clipped to the
/// horizon and summed edge by edge (Lambert / Arvo form-factor formula). The LTC tables:
///   texel (x = roughness, y = sqrt(1 - NoV)), 64x64, texel centres at both ends (LtcLutUV);
///   matrix table (RGBA32F): M^-1 / M^-1[1][1] packed as the reference ltc_1, rebuilt by
///   LtcInverseMatrix; magnitude table (RG32F): x = directional albedo of GGX without Fresnel,
///   y = its (1 - VoH)^5-weighted albedo, so the Schlick Fresnel of the whole lobe is
///   F0 * x + (F90 - F0) * y.
/// The diffuse term uses the identity matrix (a plain clamped cosine, i.e. Lambert).
///
/// Units: LtcEvaluate returns the clamped-cosine integral divided by pi — the "form factor",
/// 1 for the whole hemisphere — so radiance = lightRadiance * (albedo * ltcDiffuse + Fresnel * ltcSpec).
#ifndef UZ_LIGHTING_LTC_HLSLI
#define UZ_LIGHTING_LTC_HLSLI

static const float kLtcLutSize  = 64.0;
static const float kLtcLutScale = (kLtcLutSize - 1.0) / kLtcLutSize;
static const float kLtcLutBias  = 0.5 / kLtcLutSize;

/// Table coordinates for a (perceptual roughness, NoV) pair: texel centres at both ends.
float2 LtcLutUV(float roughness, float NoV)
{
    return float2(saturate(roughness), sqrt(saturate(1.0 - NoV))) * kLtcLutScale + kLtcLutBias;
}

/// M^-1 from a matrix-table texel (invM[0][0], invM[0][2], invM[2][0], invM[2][2] in column-major
/// indexing = row-major (m00, m20, m02, m22)); the middle entry is 1 and the y axis is uncoupled.
float3x3 LtcInverseMatrix(float4 t)
{
    return float3x3(t.x, 0.0, t.z,
                    0.0, 1.0, 0.0,
                    t.y, 0.0, t.w);
}

/// Edge term of the clamped-cosine integral over a spherical polygon: theta * n.z, with n the unit
/// normal of the edge's great-circle plane. @ref Arvo 1994, Heitz 2016 (IntegrateEdge).
float LtcIntegrateEdge(float3 v1, float3 v2)
{
    const float cosTheta = clamp(dot(v1, v2), -0.9999, 0.9999);
    const float theta = acos(cosTheta);
    return cross(v1, v2).z * ((theta > 0.001) ? theta / sin(theta) : 1.0);
}

/// Clips the quad L[0..3] (already in the transformed local frame, z up) to the z > 0 half space.
/// The result has n vertices in L[0..n-1] (3 to 5) and L[n] closes the loop (n == 0: all clipped).
/// The sixteen cases are indexed by which vertices are above the horizon (bit i = L[i].z > 0).
void LtcClipQuadToHorizon(inout float3 L[5], out int n)
{
    int config = 0;
    if (L[0].z > 0.0) config += 1;
    if (L[1].z > 0.0) config += 2;
    if (L[2].z > 0.0) config += 4;
    if (L[3].z > 0.0) config += 8;

    n = 0;
    if (config == 0) {
        // all below
    } else if (config == 1) { // V1 clip V2 V3 V4
        n = 3;
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
        L[2] = -L[3].z * L[0] + L[0].z * L[3];
    } else if (config == 2) { // V2 clip V1 V3 V4
        n = 3;
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
    } else if (config == 3) { // V1 V2 clip V3 V4
        n = 4;
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
        L[3] = -L[3].z * L[0] + L[0].z * L[3];
    } else if (config == 4) { // V3 clip V1 V2 V4
        n = 3;
        L[0] = -L[3].z * L[2] + L[2].z * L[3];
        L[1] = -L[1].z * L[2] + L[2].z * L[1];
    } else if (config == 5) { // V1 V3 clip V2 V4: impossible for a convex quad
        n = 0;
    } else if (config == 6) { // V2 V3 clip V1 V4
        n = 4;
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
        L[3] = -L[3].z * L[2] + L[2].z * L[3];
    } else if (config == 7) { // V1 V2 V3 clip V4
        n = 5;
        L[4] = -L[3].z * L[0] + L[0].z * L[3];
        L[3] = -L[3].z * L[2] + L[2].z * L[3];
    } else if (config == 8) { // V4 clip V1 V2 V3
        n = 3;
        L[0] = -L[0].z * L[3] + L[3].z * L[0];
        L[1] = -L[2].z * L[3] + L[3].z * L[2];
        L[2] =  L[3];
    } else if (config == 9) { // V1 V4 clip V2 V3
        n = 4;
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
        L[2] = -L[2].z * L[3] + L[3].z * L[2];
    } else if (config == 10) { // V2 V4 clip V1 V3: impossible for a convex quad
        n = 0;
    } else if (config == 11) { // V1 V2 V4 clip V3
        n = 5;
        L[4] = L[3];
        L[3] = -L[2].z * L[3] + L[3].z * L[2];
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
    } else if (config == 12) { // V3 V4 clip V1 V2
        n = 4;
        L[1] = -L[1].z * L[2] + L[2].z * L[1];
        L[0] = -L[0].z * L[3] + L[3].z * L[0];
    } else if (config == 13) { // V1 V3 V4 clip V2
        n = 5;
        L[4] = L[3];
        L[3] = L[2];
        L[2] = -L[1].z * L[2] + L[2].z * L[1];
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
    } else if (config == 14) { // V2 V3 V4 clip V1
        n = 5;
        L[4] = -L[0].z * L[3] + L[3].z * L[0];
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
    } else { // config == 15: all above
        n = 4;
    }

    if (n == 3) L[3] = L[0];
    if (n == 4) L[4] = L[0];
}

/// Integral over the quad (p0, p1, p2, p3) of the cosine lobe transformed by Minv, at the point P with
/// normal N seen from V, divided by pi (the form factor). The vertex order is p0 = c - ex - ey,
/// p1 = c + ex - ey, p2 = c + ex + ey, p3 = c - ex + ey with the emission normal cross(ex, ey): seen
/// from the emitting side the edge sum comes out NEGATIVE (the reference's "negated due to winding
/// order"), so one-sided lights take max(0, -sum) and two-sided ones abs(sum).
float LtcEvaluate(float3 N, float3 V, float3 P, float3x3 Minv,
                  float3 p0, float3 p1, float3 p2, float3 p3, bool twoSided)
{
    // Orthonormal frame around N with T1 along the projection of V (the frame the tables were fit in:
    // V in the xz plane). At normal incidence the lobe is symmetric, so any tangent works.
    float3 T1 = V - N * dot(V, N);
    const float t1Len2 = dot(T1, T1);
    if (t1Len2 < 1e-8) {
        T1 = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
        T1 = T1 - N * dot(T1, N);
        T1 = normalize(T1);
    } else {
        T1 = T1 * rsqrt(t1Len2);
    }
    const float3 T2 = cross(N, T1);

    // rows (T1, T2, N) map world offsets into the local frame; then M^-1.
    const float3x3 M = mul(Minv, float3x3(T1, T2, N));

    float3 L[5];
    L[0] = mul(M, p0 - P);
    L[1] = mul(M, p1 - P);
    L[2] = mul(M, p2 - P);
    L[3] = mul(M, p3 - P);
    L[4] = L[0];

    int n;
    LtcClipQuadToHorizon(L, n);
    if (n == 0) return 0.0;

    L[0] = normalize(L[0]);
    L[1] = normalize(L[1]);
    L[2] = normalize(L[2]);
    L[3] = normalize(L[3]);
    L[4] = normalize(L[4]);

    float sum = 0.0;
    sum += LtcIntegrateEdge(L[0], L[1]);
    sum += LtcIntegrateEdge(L[1], L[2]);
    sum += LtcIntegrateEdge(L[2], L[3]);
    if (n >= 4) sum += LtcIntegrateEdge(L[3], L[4]);
    if (n == 5) sum += LtcIntegrateEdge(L[4], L[0]);

    sum = twoSided ? abs(sum) : max(0.0, -sum);
    return sum / (2.0 * 3.14159265358979);
}

#ifdef UZ_SHAPED_LIGHTS
// New shapes stay out of both the stock and rectangle-only shader permutations.
// A disk is a 32-gon; a capsule is its convex visible contour, not a rectangle
// billboard. Radiance is constant, so integrating this contour integrates the
// complete visible emitter without tessellating its surface or counting hidden faces.
static const uint kLtcShapeSegments = 32u;
static const uint kLtcShapeVertices = 66u;

float LtcEvaluatePolygon(float3 N, float3 V, float3 P, float3x3 Minv,
                         float3 vertices[66], uint count, bool twoSided)
{
    if (count < 3u) return 0.0;
    float3 T1 = V - N * dot(V, N);
    if (dot(T1, T1) < 1e-8) {
        T1 = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
        T1 -= N * dot(T1, N);
    }
    T1 = normalize(T1);
    const float3x3 M = mul(Minv, float3x3(T1, cross(N, T1), N));
    float sum = 0.0;
    float3 entry = 0.0, exitPoint = 0.0;
    bool crossed = false;
    float3 a = mul(M, vertices[count - 1u] - P);
    [loop]
    for (uint i = 0u; i < count; ++i) {
        const float3 b = mul(M, vertices[i] - P);
        const bool aboveA = a.z > 0.0, aboveB = b.z > 0.0;
        if (aboveA && aboveB) {
            sum += LtcIntegrateEdge(normalize(a), normalize(b));
        } else if (aboveA != aboveB) {
            const float3 h = normalize(lerp(a, b, a.z / (a.z - b.z)));
            if (aboveA) {
                sum += LtcIntegrateEdge(normalize(a), h);
                exitPoint = h;
            } else {
                sum += LtcIntegrateEdge(h, normalize(b));
                entry = h;
            }
            crossed = true;
        }
        a = b;
    }
    // Convexity guarantees one entry and one exit. The horizon closing edge is
    // essential: simply discarding below-horizon edges loses its projected area.
    if (crossed) sum += LtcIntegrateEdge(exitPoint, entry);
    return (twoSided ? abs(sum) : max(0.0, -sum)) / (2.0 * 3.14159265358979);
}

uint LtcDiskVertices(float3 center, float3 ex, float3 ey, out float3 vertices[66])
{
    [loop]
    for (uint i = 0u; i < kLtcShapeSegments; ++i) {
        float s, c;
        sincos(float(i) * (2.0 * 3.14159265358979 / float(kLtcShapeSegments)), s, c);
        vertices[i] = center + ex * c + ey * s;
    }
    return kLtcShapeSegments;
}

// Tangent circle of a sphere as seen from P. Only the arc whose outward normal
// points away from the other endpoint belongs to the capsule silhouette. The two
// arcs are joined by the cylinder's two straight tangent generators.
void LtcCapsuleArc(float3 center, float3 P, float3 axis, float radius, bool firstEnd,
                   inout float3 vertices[66], inout uint count)
{
    const float3 offset = center - P;
    const float d2 = dot(offset, offset);
    const float invD = rsqrt(d2);
    const float3 W = offset * invD;
    const float axial = dot(W, axis);
    const float3 projectedAxis = axis - W * axial;
    const float projectedLen = length(projectedAxis);
    const float s = sqrt(max(0.0, 1.0 - radius * radius / d2));
    float3 U;
    float angle;
    if (projectedLen < 1e-6) {
        // End-on: exactly one complete sphere contour is exposed.
        if ((firstEnd && axial < 0.0) || (!firstEnd && axial > 0.0)) return;
        U = abs(W.z) < 0.999 ? normalize(cross(W, float3(0, 0, 1)))
                                : normalize(cross(W, float3(0, 1, 0)));
        angle = firstEnd ? 0.0 : 3.14159265358979;
    } else {
        U = projectedAxis / projectedLen;
        const float k = radius * invD * axial / (s * projectedLen);
        if ((firstEnd && k <= -1.0) || (!firstEnd && k >= 1.0)) return;
        angle = acos(clamp(k, -1.0, 1.0));
    }
    const float3 T = cross(W, U);
    const float start = firstEnd ? angle : -angle;
    const float sweep = firstEnd ? 2.0 * (3.14159265358979 - angle) : 2.0 * angle;
    const float3 circleCenter = center - W * (radius * radius * invD);
    [loop]
    for (uint i = 0u; i <= kLtcShapeSegments; ++i) {
        float sn, cs;
        sincos(start + sweep * (float(i) / float(kLtcShapeSegments)), sn, cs);
        vertices[count++] = circleCenter + (U * cs + T * sn) * (radius * s);
    }
}

uint LtcCapsuleVertices(float3 center, float3 axis, float halfLength, float radius,
                        float3 P, out float3 vertices[66])
{
    if (radius <= 0.0) return 0u;
    const float3 d = P - center;
    const float3 closest = center + axis * clamp(dot(d, axis), -halfLength, halfLength);
    if (dot(P - closest, P - closest) <= radius * radius) return 0u;
    uint count = 0u;
    LtcCapsuleArc(center - axis * halfLength, P, axis, radius, true, vertices, count);
    LtcCapsuleArc(center + axis * halfLength, P, axis, radius, false, vertices, count);
    return count;
}
#endif

#endif // UZ_LIGHTING_LTC_HLSLI
