#include "rough_specular_common.hlsli"
#include "rough_specular_math.hlsli"

// Reuse the existing normalized, VNDF-throughput-weighted reflection
// signal before lighting/TAA. Spatial reuse is normalized and never recursive.
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 p = int2(id.xy);
    if (!InBounds(p)) return;
    Texture2D<float4> input = ResourceDescriptorHeap[inputSrv];
    Texture2D<float2> velocity = ResourceDescriptorHeap[velocitySrv];
    Texture2D<float4> history = ResourceDescriptorHeap[historySrv];
    Texture2D<float4> moments = ResourceDescriptorHeap[momentsSrv];
    Texture2D<float4> geometry = ResourceDescriptorHeap[geometrySrv];
    Texture2D<float4> guides = ResourceDescriptorHeap[guidesSrv];
    RWTexture2D<float4> colorOut = ResourceDescriptorHeap[colorUav];
    RWTexture2D<float4> momentsOut = ResourceDescriptorHeap[momentsUav];
    RWTexture2D<float4> geometryOut = ResourceDescriptorHeap[geometryUav];
    RWTexture2D<float4> guidesOut = ResourceDescriptorHeap[guidesUav];
    RoughReceiver r = Receiver(p);
    float4 current = input.Load(int3(p, 0));
    geometryOut[p] = float4(r.position, r.surface ? PackNormal(r.normal) : -1.0);
    guidesOut[p] = float4(r.roughness, r.hitDistance, r.linearDepth, current.a);
    if (!r.surface || !Reconstructs(r.roughness) || current.a <= 0.0) {
        colorOut[p] = 0.0;
        momentsOut[p] = 0.0;
        return;
    }

    float3 sum = 0.0;
    float first = 0.0, second = 0.0, weights = 0.0, squaredWeights = 0.0;
    float radius = SpatialRadius(r.roughness, r.hitDistance, r.linearDepth);
    [loop]
    for (int y = -2; y <= 2; ++y) {
        [loop]
        for (int x = -2; x <= 2; ++x) {
            int2 q = p + int2(round(float2(x, y) * radius));
            if (!InBounds(q)) continue;
            float4 c = input.Load(int3(q, 0));
            RoughReceiver s = Receiver(q);
            // Valid black (e.g. below-horizon VNDF draws) participates in the mean.
            if (c.a <= 0.0 || !Compatible(r, s)) continue;
            float hitScale = max(1.0, max(r.hitDistance, s.hitDistance));
            float hitWeight = exp(-abs(r.hitDistance - s.hitDistance) / hitScale
                                  * (1.0 - r.roughness));
            float w = exp(-float(x*x + y*y) * 0.5)
                * pow(saturate(dot(r.normal, s.normal)), NormalPower(r.roughness))
                * exp(-PlaneDistance(r, s) / PlaneTolerance(r))
                * exp(-abs(r.roughness - s.roughness) * 32.0) * hitWeight;
            float l = Luminance(c.rgb);
            sum += c.rgb * w;
            first += l * w;
            second += l * l * w;
            weights += w;
            squaredWeights += w * w;
        }
    }
    float3 estimate = sum / max(weights, 0.00001);
    first /= max(weights, 0.00001);
    second /= max(weights, 0.00001);
    float effectiveSamples = weights * weights / max(squaredWeights, 0.00001);
    float spatialVariance = max(second - first * first, 0.0) / max(effectiveSamples, 1.0);

    float2 motion = velocity.Load(int3(p, 0));
    float motionLength = length(motion);
    // Motion is in render pixels. Remove TAA lattice motion before reusing taps.
    float2 previousPixel = float2(p) + previousJitter - currentJitter - motion;
    int2 base = int2(floor(previousPixel));
    float2 fraction = frac(previousPixel);
    float4 old = 0.0, oldMoments = 0.0;
    float accepted = 0.0;
    if (historyValid != 0 && all(isfinite(previousPixel))) {
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
            int2 q = base + int2(x, y);
            if (!InBounds(q)) continue;
            float4 g = geometry.Load(int3(q, 0));
            float4 properties = guides.Load(int3(q, 0));
            RoughReceiver s;
            s.position = g.xyz;
            s.normal = UnpackNormal(max(g.w, 0.0));
            s.surface = g.w >= 0.0;
            s.roughness = properties.x;
            s.hitDistance = properties.y;
            s.linearDepth = properties.z;
            float4 h = history.Load(int3(q, 0));
            if (h.a <= 0.0 || !Compatible(r, s)) continue;
            float w = (x == 0 ? 1.0 - fraction.x : fraction.x)
                    * (y == 0 ? 1.0 - fraction.y : fraction.y);
            old += h * w;
            oldMoments += moments.Load(int3(q, 0)) * w;
            accepted += w;
        }
    }
    float count = 1.0;
    float2 updated = float2(first, second);
    float fast = first;
    float fastCount = 1.0;
    if (accepted > 0.00001) {
        old /= accepted;
        oldMoments /= accepted;
        fastCount = min(oldMoments.w + 1.0, motionLength > 0.5 ? 2.0 : 8.0);
        fast = lerp(oldMoments.z, first, 1.0 / fastCount);
        // Historical estimator variance, reduced by spatial effective samples,
        // stops a quiet instantaneous neighborhood from clipping a converged lobe.
        float variance = max(spatialVariance,
            max(oldMoments.y - oldMoments.x * oldMoments.x, 0.0) / max(effectiveSamples, 1.0));
        float oldL = Luminance(old.rgb);
        // Clamp against uncertainty of the fast MEAN, not variance of individual
        // estimates. Otherwise a converging producer's old dark history remains
        // inside an unnecessarily wide interval for dozens of frames.
        float clamped = ClampHistory(oldL, fast, variance / fastCount, motionLength);
        if (oldL > 0.0) old.rgb *= clamped / oldL;
        count = min(old.a + 1.0, HistoryCap(r.roughness, motionLength));
        float alpha = 1.0 / count;
        estimate = lerp(old.rgb, estimate, alpha);
        updated = lerp(oldMoments.xy, float2(first, second), alpha);
    }
    colorOut[p] = float4(estimate, count);
    momentsOut[p] = float4(updated, fast, fastCount);
}
