// taa.ps.hlsl -- Temporal Anti-Aliasing resolve (production quality)
//
// Techniques (matched to UE5 TemporalAA.usf patterns):
//   - Jitter-weighted Gaussian neighborhood filtering (sigma=0.47) [UE5 P0]
//   - Scaled YCoCg color space (UE5 Luma4-consistent)
//   - Variance AABB with current-pixel inclusion guarantee
//   - Simple clamp (not clip-to-center — less shimmer)
//   - HDR weight: rcp(Luma4 + 4.0) [UE5 HdrWeight4]
//   - Anti-flicker floor in blend factor [UE5 line 2246]
//   - Velocity dilation from closest depth in 3x3
//
// References:
//   [Karis 2014] "High Quality Temporal Supersampling" — SIGGRAPH
//   [Salvi 2016] "An Excursion in Temporal Supersampling" — GDC
//   UE5 — Engine/Shaders/Private/TemporalAA.usf (lines 597-2376)

cbuffer TAAParams : register(b0) {
    float2 resolution;         // render target dimensions
    float2 invResolution;      // 1.0 / resolution
    float2 jitterOffset;       // current frame sub-pixel jitter (UV space)
    float  blendFactor;        // base blend weight for current frame (0.04)
    float  _pad;
};

Texture2D         currentTex  : register(t0);   // current HDR scene (jittered)
Texture2D         historyTex  : register(t1);   // previous resolved frame
Texture2D<float2> velocityTex : register(t2);   // screen-space motion vectors
Texture2D<float>  depthTex    : register(t3);   // current depth buffer
Texture2D         aoTex       : register(t4);   // SSAO (prev frame, half-res bilinear)
SamplerState      linearClamp : register(s1);   // linear clamp (for history + AO bilinear)

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

// ---------------------------------------------------------------------------
// Scaled YCoCg (UE5 form) — Y = Luma4, consistent with HdrWeight4
// Forward: {1,2,1}, {2,0,-2}, {-1,2,-1}   Inverse: divide by 4
// ---------------------------------------------------------------------------

float3 RGBToYCoCg(float3 rgb) {
    return float3(
        dot(rgb, float3( 1, 2,  1)),   // Y  (4x luminance)
        dot(rgb, float3( 2, 0, -2)),   // Co
        dot(rgb, float3(-1, 2, -1))    // Cg
    );
}

float3 YCoCgToRGB(float3 ycocg) {
    float Y  = ycocg.x * 0.25;
    float Co = ycocg.y * 0.25;
    float Cg = ycocg.z * 0.25;
    return float3(Y + Co - Cg, Y + Cg, Y - Co - Cg);
}

// ---------------------------------------------------------------------------
// HDR weight: rcp(Luma4 + 4.0) [UE5 HdrWeight4]
// The constant 4.0 prevents over-attenuation of mid-range HDR values.
// Luma4(rgb) == RGBToYCoCg(rgb).x, so we can use Y directly.
// ---------------------------------------------------------------------------

float Luma4(float3 rgb) { return rgb.g * 2.0 + rgb.r + rgb.b; }
float HdrWeight4(float3 color) { return rcp(Luma4(color) + 4.0); }
float HdrWeightY(float luma4) { return rcp(luma4 + 4.0); }

// ---------------------------------------------------------------------------
// Closest depth in X-pattern at distance 2 — velocity dilation [UE5]
// Distance 2 because TAA itself dilates edges by ~1 pixel, so a wider
// dilation catches the correct foreground motion for silhouette pixels.
// ---------------------------------------------------------------------------

int2 ClosestDepthOffset(int2 coord) {
    float closestDepth = depthTex.Load(int3(coord, 0));
    int2 closestOffset = int2(0, 0);

    static const int2 offsets[4] = {
        int2(-2, -2), int2( 2, -2),
        int2(-2,  2), int2( 2,  2)
    };

    [unroll] for (int i = 0; i < 4; ++i) {
        float d = depthTex.Load(int3(coord + offsets[i], 0));
        if (d < closestDepth) {
            closestDepth = d;
            closestOffset = offsets[i];
        }
    }
    return closestOffset;
}

// ---------------------------------------------------------------------------
// UE5 WeightedLerpFactors — renormalized HDR-weighted blend
// ---------------------------------------------------------------------------

float2 WeightedLerpFactors(float wA, float wB, float blend) {
    float bA = (1.0 - blend) * wA;
    float bB =        blend  * wB;
    float rcpSum = rcp(bA + bB);
    return float2(bA * rcpSum, bB * rcpSum);
}

// ---------------------------------------------------------------------------
// Main TAA resolve
// ---------------------------------------------------------------------------

float4 PSMain(PSInput input) : SV_TARGET {
    int2 pixelCoord = int2(input.position.xy);
    float2 uv = input.texcoord;

    // -----------------------------------------------------------------------
    // 1. Velocity dilation from closest depth in 3x3
    // -----------------------------------------------------------------------
    int2 closestOff = ClosestDepthOffset(pixelCoord);
    float2 velocity = velocityTex.Load(int3(pixelCoord + closestOff, 0));
    float2 historyUV = uv - velocity;
    float velocityLenPx = length(velocity * resolution);

    // -----------------------------------------------------------------------
    // 2. Jitter-weighted Gaussian neighborhood filtering [UE5 sigma=0.47]
    //    Samples closer to the jitter position get more weight.
    //    This is the PRIMARY anti-boiling mechanism — without this,
    //    equal weights cause the neighborhood to shift each frame.
    // -----------------------------------------------------------------------
    float2 jitterPx = jitterOffset * resolution; // sub-pixel jitter in [-0.5, 0.5)

    float3 filtered = float3(0, 0, 0);       // weighted filtered color (RGB)
    float  totalWeight = 0.0;
    float3 m1 = float3(0, 0, 0);             // uniform sum (YCoCg, for variance)
    float3 m2 = float3(0, 0, 0);             // uniform sum of squares

    [unroll] for (int sy = -1; sy <= 1; ++sy) {
        [unroll] for (int sx = -1; sx <= 1; ++sx) {
            float3 s = currentTex.Load(int3(pixelCoord + int2(sx, sy), 0)).rgb;

            // Bake SSAO into the HDR signal so TAA accumulates AO temporally.
            // aoTex is half-res from the previous frame — bilinear sampled.
            float2 sampleUV = (float2(pixelCoord + int2(sx, sy)) + 0.5) * invResolution;
            float ao = aoTex.SampleLevel(linearClamp, sampleUV, 0).r;
            s *= ao;

            // Gaussian weight: exp(-1/(2*sigma^2) * d^2), sigma=0.47 → coeff=2.266
            float dx = float(sx) - jitterPx.x;
            float dy = float(sy) - jitterPx.y;
            float spatialW = exp(-2.29 * (dx * dx + dy * dy));

            // HDR weight: prevents bright samples from dominating
            float hdrW = HdrWeight4(s);
            float w = spatialW * hdrW;

            filtered += s * w;
            totalWeight += w;

            // Variance stats: uniform weights (jitter-independent → stable AABB)
            float3 sYCoCg = RGBToYCoCg(s);
            m1 += sYCoCg;
            m2 += sYCoCg * sYCoCg;
        }
    }

    filtered /= totalWeight;

    // -----------------------------------------------------------------------
    // 3. Variance AABB in YCoCg [Salvi 2016]
    //    Uniform-weight stats → AABB stable across jitter positions.
    //    CRITICAL: widen AABB to always include the filtered pixel [UE5 line 1738]
    // -----------------------------------------------------------------------
    float3 mean   = m1 * (1.0 / 9.0);
    float3 stddev = sqrt(abs(m2 * (1.0 / 9.0) - mean * mean));

    float gamma = lerp(1.25, 0.75, smoothstep(0.0, 8.0, velocityLenPx));
    float3 aabbMin = mean - stddev * gamma;
    float3 aabbMax = mean + stddev * gamma;

    // Widen to always include the filtered pixel — prevents oscillation [UE5]
    float3 filteredYCoCg = RGBToYCoCg(filtered);
    aabbMin = min(aabbMin, filteredYCoCg);
    aabbMax = max(aabbMax, filteredYCoCg);

    // -----------------------------------------------------------------------
    // 4. Sample and clamp history (simple clamp, not clip-to-center)
    //    Simple clamp is UE5's default — less shimmer than clip-to-center.
    // -----------------------------------------------------------------------
    float3 history = historyTex.SampleLevel(linearClamp, historyUV, 0).rgb;
    float3 historyYCoCg = RGBToYCoCg(history);
    historyYCoCg = clamp(historyYCoCg, aabbMin, aabbMax);
    history = max(YCoCgToRGB(historyYCoCg), 0.0);

    // -----------------------------------------------------------------------
    // 5. Blend factor [UE5 pattern]
    //    Base: 0.04 (96% history)
    //    Velocity ramp: 0.04 → 0.2 over 40px [UE5 line 2234]
    //    Anti-flicker floor: dampens shimmer on static camera [UE5 line 2246]
    // -----------------------------------------------------------------------
    float blend = blendFactor;  // base: 0.04

    // Velocity rejection: ramp toward 0.2 over 40 pixels
    blend = lerp(blend, 0.2, saturate(velocityLenPx / 40.0));

    // Out-of-screen rejection
    if (historyUV.x < 0.0 || historyUV.x > 1.0 ||
        historyUV.y < 0.0 || historyUV.y > 1.0) {
        blend = 1.0;
    }

    // Anti-flicker: Godot luminance-diff suppression [Godot taa_resolve.glsl].
    // When luma nearly matches (static scene), drive blend toward base value
    // so that history dominates → kills sub-pixel jitter trembling.
    float lumaFiltered = Luma4(filtered);
    float lumaHistory  = Luma4(history);

    // Normalized luminance difference, squared falloff
    float lumaDiff = abs(lumaFiltered - lumaHistory) / max(lumaFiltered, max(lumaHistory, 4.0));
    float antiFlicker = 1.0 - lumaDiff;
    antiFlicker *= antiFlicker;  // squared — aggressive suppression
    blend *= antiFlicker;        // drives blend → base when luma matches (static)

    // Clamp blend to sane range: never below 1% (temporal dithering) or above 50%
    blend = clamp(blend, 0.01, 0.5);

    // -----------------------------------------------------------------------
    // 6. HDR-weighted temporal blend [UE5 WeightedLerpFactors]
    // -----------------------------------------------------------------------
    float wFiltered = HdrWeight4(filtered);
    float wHistory  = HdrWeight4(history);
    float2 weights  = WeightedLerpFactors(wHistory, wFiltered, blend);

    float3 result = history * weights.x + filtered * weights.y;

    return float4(max(result, 0.0), 1.0);
}
