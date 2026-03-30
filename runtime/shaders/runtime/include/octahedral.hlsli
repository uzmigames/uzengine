/// Octahedral normal encode/decode.
/// @ref Narkowicz 2014, Cigolle 2014
/// @ref Meyer et al. 2010 — equal-area variant
#ifndef OCTAHEDRAL_HLSLI
#define OCTAHEDRAL_HLSLI

#include "math_util.hlsli"

float2 OctEncode(float3 n) {
    float3 a = abs(n);
    float t = a.x + a.y + a.z;
    float2 o = n.xy / t;
    if (n.z < 0.0) {
        float2 s = float2(o.x >= 0.0 ? 1.0 : -1.0, o.y >= 0.0 ? 1.0 : -1.0);
        o = (1.0 - abs(o.yx)) * s;
    }
    return o * 0.5 + 0.5;
}

float3 OctDecode(float2 f) {
    f = f * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    if (n.z < 0.0) {
        float2 s = float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(n.yx)) * s;
    }
    return normalize(n);
}

// ─────────────────────────────────────────────────────────────────────────────
// Equal-Area Octahedral Encoding
// ─────────────────────────────────────────────────────────────────────────────

/// Equal-area octahedral encode: sphere → [0,1]^2 with area preservation.
/// Produces less distortion at poles compared to the standard projection.
/// @ref Meyer et al. 2010 "A Simple, Efficient, and Robust Equal-Area Octahedral Map"
/// @ref Cigolle et al. 2014 — survey of octahedral mappings
float2 OctEncodeEqualArea(float3 n) {
    float r = sqrt(1.0f - abs(n.z));
    float phi = atan2(abs(n.y), abs(n.x));
    float2 p;
    p.y = r * phi * M_1_PI * 2.0f; // 2/PI * phi * r
    p.x = r - p.y;
    if (n.z < 0.0f) {
        p = 1.0f - p.yx;
    }
    p.x = (n.x >= 0.0f) ? p.x : -p.x;
    p.y = (n.y >= 0.0f) ? p.y : -p.y;
    return p * 0.5f + 0.5f;
}

/// Equal-area octahedral decode: [0,1]^2 → unit sphere.
/// Inverse of OctEncodeEqualArea.
/// @ref Meyer et al. 2010 "A Simple, Efficient, and Robust Equal-Area Octahedral Map"
float3 OctDecodeEqualArea(float2 e) {
    e = e * 2.0f - 1.0f;
    float2 ae = abs(e);
    float d = 1.0f - ae.x - ae.y;
    float r = 1.0f - abs(d);
    float phi = (r == 0.0f) ? 0.0f : (M_PI_4 * ((ae.y - ae.x) / r + 1.0f));
    float sinPhi, cosPhi;
    sincos(phi, sinPhi, cosPhi);
    float f = r * sqrt(2.0f - r * r);
    float3 n;
    n.x = f * cosPhi * ((e.x >= 0.0f) ? 1.0f : -1.0f);
    n.y = f * sinPhi * ((e.y >= 0.0f) ? 1.0f : -1.0f);
    n.z = (d >= 0.0f ? 1.0f : -1.0f) * (1.0f - r * r);
    return normalize(n);
}

#endif
