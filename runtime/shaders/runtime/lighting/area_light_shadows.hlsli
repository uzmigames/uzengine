// Separate permutation: OFF never changes the unshadowed LTC shader or its arithmetic.
#ifndef UZ_AREA_LIGHT_SHADOWS_HLSLI
#define UZ_AREA_LIGHT_SHADOWS_HLSLI

// Stratified low-discrepancy source quadrature (Hammersley), toroidally shifted per pixel
// and per frame (Cranley-Patterson rotation) so the existing TAA accumulates the samples:
// a shared fixed pattern bands the penumbra into contours. Each lobe gets its own
// visibility: an unweighted area fraction is wrong for a glossy lobe or a nearby emitter.
// Ratios are self-normalized (finite-sample bias), bounded in [0,1] (no fireflies), and
// exactly one for an unobstructed source.
static const uint kAreaShadowSamples = 16u;

// Per-pixel interleaved gradient noise (Jimenez 2014) plus an R2 step per frame.
float2 AreaShadowShift(float2 pixel, uint frame)
{
    const float2 ign = float2(frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715)))),
                              frac(52.9829189 * frac(dot(pixel + 17.0, float2(0.00583715, 0.06711056)))));
    return frac(ign + float(frame & 1023u) * float2(0.7548776662, 0.5698402910));
}

void AreaShadowSourceSample(RectLightData light, float2 u, out float3 p, out float3 n)
{
    n = normalize(cross(light.right, light.up));
    if ((light.flags & kRectLightFlag_Tube) != 0u) {
        // Uniform surface area over cylinder + two hemispheres. The cylinder
        // area fraction is halfLength / (halfLength + radius).
        const float radius = light.halfHeight;
        const float x = u.x * (light.halfWidth + radius);
        float s, c;
        sincos(2.0 * PI * u.y, s, c);
        const float3 radial = light.up * c + n * s;
        if (x < light.halfWidth) {
            n = radial;
            p = light.position + light.right * (2.0 * x - light.halfWidth) + radius * n;
        } else {
            const float mu = 2.0 * (x - light.halfWidth) / max(radius, 1e-6) - 1.0;
            n = light.right * mu + radial * sqrt(saturate(1.0 - mu * mu));
            p = light.position + light.right * (mu >= 0.0 ? light.halfWidth : -light.halfWidth) + radius * n;
        }
    } else if ((light.flags & kRectLightFlag_Disk) != 0u) {
        float s, c;
        sincos(2.0 * PI * u.y, s, c);
        p = light.position + sqrt(u.x) * (light.right * (light.halfWidth * c)
                                      + light.up * (light.halfHeight * s));
    } else {
        p = light.position + light.right * ((2.0 * u.x - 1.0) * light.halfWidth)
                           + light.up * ((2.0 * u.y - 1.0) * light.halfHeight);
    }
}

float2 AreaShadowVisibility(RectLightData light, float3 P, float3 geometricNormal,
                            float3 N, float3 V, float3x3 Minv, float2 shift)
{
    if (areaShadowTlasSrv == kSentinelSrvIndex) return 1.0;
    RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[areaShadowTlasSrv];
    float3 T1 = V - N * dot(V, N);
    if (dot(T1, T1) < 1e-8) {
        T1 = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
        T1 -= N * dot(T1, N);
    }
    T1 = normalize(T1);
    const float3x3 M = mul(Minv, float3x3(T1, cross(N, T1), N));
    float2 total = 0.0, visible = 0.0;
    [loop]
    for (uint i = 0u; i < kAreaShadowSamples; ++i) {
        const float2 u = frac(float2((float(i) + 0.5) / float(kAreaShadowSamples),
                                     float(reversebits(i)) * 2.3283064365386963e-10) + shift);
        float3 target, emitterNormal;
        AreaShadowSourceSample(light, u, target, emitterNormal);
        const float3 d = target - P;
        const float d2 = max(dot(d, d), 1e-8);
        const float3 L = d * rsqrt(d2);
        float cosEmitter = dot(emitterNormal, -L);
        if ((light.flags & kRectLightFlag_TwoSided) != 0u
            && (light.flags & kRectLightFlag_Tube) == 0u) cosEmitter = abs(cosEmitter);
        cosEmitter = max(cosEmitter, 0.0);
        const float3 q = mul(M, L);
        const float q2 = max(dot(q, q), 1e-12);
        // The determinant / pi and uniform source PDF cancel in each ratio.
        const float2 weight = float2(max(dot(N, L), 0.0), max(q.z, 0.0) / (q2 * q2))
                            * (cosEmitter / d2);
        if (!any(weight > 0.0)) continue;
        total += weight;
        // World units are centimetres. Stop before the sampled emitter to avoid
        // self-blocking on the coincident PT reference fixture.
        const float3 origin = P + geometricNormal * 0.05;
        const float3 toTarget = target - origin;
        const float distance = length(toTarget);
        if (distance <= 0.06) { visible += weight; continue; }
        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = toTarget / distance;
        ray.TMin = 0.01;
        ray.TMax = distance - 0.05;
        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
        query.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
        query.Proceed();
        if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) visible += weight;
    }
    return float2(total.x > 0.0 ? saturate(visible.x / total.x) : 1.0,
                  total.y > 0.0 ? saturate(visible.y / total.y) : 1.0);
}
#endif
