// taa.ps.hlsl -- Temporal Anti-Aliasing resolve
//
// Reprojects history using velocity buffer, clamps to neighborhood AABB
// in YCoCg color space, and blends with current jittered frame.
//
// References:
//   [Karis 2014] "High Quality Temporal Supersampling" — SIGGRAPH
//   [Salvi 2016] "An Excursion in Temporal Supersampling" — GDC
//   [Jimenez 2016] "Filmic SMAA" — temporal filter details
//   UE5 — Engine/Shaders/Private/TemporalAA.usf

cbuffer TAAParams : register(b0) {
    float2 resolution;         // render target dimensions
    float2 invResolution;      // 1.0 / resolution
    float2 jitterOffset;       // current frame sub-pixel jitter (UV space)
    float  blendFactor;        // base blend weight for current frame (0.05)
    float  _pad;
};

Texture2D    currentTex  : register(t0);   // current HDR scene (jittered)
Texture2D    historyTex  : register(t1);   // previous resolved frame
Texture2D<float2> velocityTex : register(t2);   // screen-space motion vectors
SamplerState linearClamp : register(s1);   // linear clamp (for history bilinear)

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

// RGB <-> YCoCg conversion for neighborhood clamping
// YCoCg gives tighter AABB than RGB, reducing ghosting
float3 RGBToYCoCg(float3 rgb) {
    return float3(
         0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b,   // Y  (luma)
         0.5  * rgb.r                - 0.5  * rgb.b,    // Co (orange)
        -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b     // Cg (green)
    );
}

float3 YCoCgToRGB(float3 ycocg) {
    float y  = ycocg.x;
    float co = ycocg.y;
    float cg = ycocg.z;
    return float3(
        y + co - cg,
        y      + cg,
        y - co - cg
    );
}

float4 PSMain(PSInput input) : SV_TARGET {
    float2 uv = input.texcoord;

    int2 pixelCoord = int2(input.position.xy);

    // Sample velocity at this pixel (point read)
    float2 velocity = velocityTex.Load(int3(pixelCoord, 0));

    // Reproject: find where this pixel was in the previous frame
    float2 historyUV = uv - velocity;

    // Sample current frame (point read — this is the raw jittered render)
    float3 current = currentTex.Load(int3(pixelCoord, 0)).rgb;

    // Sample history (bilinear at reprojected position for sub-pixel quality)
    float3 history = historyTex.SampleLevel(linearClamp, historyUV, 0).rgb;

    // 3x3 neighborhood min/max in YCoCg for AABB clamping
    float3 neighborMin = float3( 1e10,  1e10,  1e10);
    float3 neighborMax = float3(-1e10, -1e10, -1e10);

    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            float3 s = currentTex.Load(int3(pixelCoord + int2(x, y), 0)).rgb;
            float3 sYCoCg = RGBToYCoCg(s);
            neighborMin = min(neighborMin, sYCoCg);
            neighborMax = max(neighborMax, sYCoCg);
        }
    }

    // Clamp history to neighborhood AABB in YCoCg space
    float3 historyYCoCg = RGBToYCoCg(history);
    historyYCoCg = clamp(historyYCoCg, neighborMin, neighborMax);
    history = YCoCgToRGB(historyYCoCg);

    // Velocity rejection: increase blend toward current for disoccluded pixels
    float velocityLen = length(velocity * resolution);  // velocity in pixels
    float blend = blendFactor;

    // Large velocity = likely disocclusion, use more of current frame
    blend = lerp(blend, 0.5, saturate(velocityLen / 20.0));

    // Out-of-screen rejection: if history UV is outside [0,1], use current
    if (historyUV.x < 0.0 || historyUV.x > 1.0 ||
        historyUV.y < 0.0 || historyUV.y > 1.0) {
        blend = 1.0;
    }

    // Final blend: lerp(history, current, blend)
    // Low blend = mostly history (stable), high = mostly current (reactive)
    float3 result = lerp(history, current, blend);

    return float4(max(result, 0.0), 1.0);
}
