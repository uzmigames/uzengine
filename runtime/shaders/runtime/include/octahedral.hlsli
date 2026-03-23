/// Octahedral normal encode/decode.
/// @ref Narkowicz 2014, Cigolle 2014
#ifndef OCTAHEDRAL_HLSLI
#define OCTAHEDRAL_HLSLI

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

#endif
