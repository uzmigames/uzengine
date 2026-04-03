/// UzEngine GTAO Temporal Accumulation — velocity-based reprojection + variance clipping.
/// @ref Jimenez 2016 "Practical Realtime Strategies for Accurate Indirect Occlusion"
/// @ref Salvi 2016 "An Excursion in Temporal Supersampling"
/// @constants blendWeight default = 0.1, variance clip sigma = 0.8

cbuffer GTAOTemporalRootCB : register(b0) {
    uint   currentAOSrvIndex;  // 0
    uint   historyAOSrvIndex;  // 1
    uint   velocitySrvIndex;   // 2
    uint   depthSrvIndex;      // 3
    uint   outputWidth;        // 4
    uint   outputHeight;       // 5
    float  blendWeight;        // 6
    uint   _pad0;              // 7
};

RWTexture2D<float> outputAO : register(u0);
SamplerState linearClamp : register(s0);
SamplerState pointClamp  : register(s1);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= outputWidth || DTid.y >= outputHeight) return;

    float2 texelSize = 1.0 / float2(outputWidth, outputHeight);
    float2 uv = (float2(DTid.xy) + 0.5) * texelSize;

    Texture2D<float>  currentAOTex = ResourceDescriptorHeap[currentAOSrvIndex];
    Texture2D<float>  historyAOTex = ResourceDescriptorHeap[historyAOSrvIndex];
    Texture2D<float2> velocityTex  = ResourceDescriptorHeap[velocitySrvIndex];

    float currentAO = currentAOTex.SampleLevel(pointClamp, uv, 0).r;

    // Reprojection: prevUV = currentUV - velocity
    float2 velocity = velocityTex.SampleLevel(pointClamp, uv, 0).rg;
    float2 prevUV   = uv - velocity;

    // Disocclusion: if prevUV is outside screen, use current frame only
    if (any(prevUV < 0.0) || any(prevUV > 1.0)) {
        outputAO[DTid.xy] = currentAO;
        return;
    }

    // Neighbourhood variance clipping (Karis 2014 / Salvi 2016)
    // 4 diagonal neighbours at ±1 texel
    static const float2 offsets[4] = {
        float2(-1.0, -1.0), float2(1.0, -1.0),
        float2(-1.0,  1.0), float2(1.0,  1.0)
    };

    float m1 = currentAO;
    float m2 = currentAO * currentAO;

    [unroll]
    for (uint i = 0; i < 4; i++) {
        float s = currentAOTex.SampleLevel(pointClamp, uv + offsets[i] * texelSize, 0).r;
        m1 += s;
        m2 += s * s;
    }

    m1 *= 0.2; // mean of 5 samples
    m2 *= 0.2;

    float sigma = sqrt(max(m2 - m1 * m1, 0.0));
    float minAO = max(m1 - sigma * 0.8, 0.0);
    float maxAO = min(m1 + sigma * 0.8, 1.0);

    // Sample history and clamp to neighbourhood AABB
    float historyAO = historyAOTex.SampleLevel(linearClamp, prevUV, 0).r;
    historyAO = clamp(historyAO, minAO, maxAO);

    // Velocity-based blend: faster motion → more current frame
    float velMag = length(velocity * float2(outputWidth, outputHeight));
    float alpha  = lerp(blendWeight, 1.0, saturate(velMag * 0.5));

    float result = lerp(historyAO, currentAO, alpha);
    outputAO[DTid.xy] = result;
}
