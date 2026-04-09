/// UzEngine GTAO Temporal Accumulation — velocity-based reprojection + variance clipping.
/// @ref Jimenez 2016 "Practical Realtime Strategies for Accurate Indirect Occlusion"
/// @ref Salvi 2016 "An Excursion in Temporal Supersampling"
/// @constants blendWeight default = 0.1, variance clip sigma = 0.8

cbuffer GTAOTemporalRootCB : register(b0) {
#ifdef UZ_BINDLESS
    uint   currentAOSrvIndex;  // 0
    uint   historyAOSrvIndex;  // 1
    uint   velocitySrvIndex;   // 2
    uint   depthSrvIndex;      // 3
#else
    uint   _srvPad0;           // 0
    uint   _srvPad1;           // 1
    uint   _srvPad2;           // 2
    uint   _srvPad3;           // 3
#endif
    uint   outputWidth;        // 4
    uint   outputHeight;       // 5
    float  blendWeight;        // 6
    uint   _pad0;              // 7
};

RWTexture2D<float> outputAO : register(u0);
#ifndef UZ_BINDLESS
Texture2D<float>  t_currentAO : register(t0);
Texture2D<float>  t_historyAO : register(t1);
Texture2D<float2> t_velocity  : register(t2);
#endif
SamplerState linearClamp : register(s0);
SamplerState pointClamp  : register(s1);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= outputWidth || DTid.y >= outputHeight) return;

    float2 texelSize = 1.0 / float2(outputWidth, outputHeight);
    float2 uv = (float2(DTid.xy) + 0.5) * texelSize;

#ifdef UZ_BINDLESS
    Texture2D<float>  currentAOTex = ResourceDescriptorHeap[currentAOSrvIndex];
    Texture2D<float>  historyAOTex = ResourceDescriptorHeap[historyAOSrvIndex];
    Texture2D<float2> velocityTex  = ResourceDescriptorHeap[velocitySrvIndex];
#else
    Texture2D<float>  currentAOTex = t_currentAO;
    Texture2D<float>  historyAOTex = t_historyAO;
    Texture2D<float2> velocityTex  = t_velocity;
#endif

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

    m1 *= 0.2;
    m2 *= 0.2;

    float sigma = sqrt(max(m2 - m1 * m1, 0.0));
    float minAO = max(m1 - sigma * 0.8, 0.0);
    float maxAO = min(m1 + sigma * 0.8, 1.0);

    float historyAO = historyAOTex.SampleLevel(linearClamp, prevUV, 0).r;
    historyAO = clamp(historyAO, minAO, maxAO);

    float velMag = length(velocity * float2(outputWidth, outputHeight));
    float alpha  = lerp(blendWeight, 1.0, saturate(velMag * 0.5));

    outputAO[DTid.xy] = lerp(historyAO, currentAO, alpha);
}
