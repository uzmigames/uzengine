/// UzEngine auto-exposure histogram — 64-bin log2 luminance histogram.
/// @ref Falcor ToneMapping.ps.slang — auto-exposure via average luminance
/// @ref Karis 2013 — histogram-based auto-exposure with temporal adaptation
///
/// Each thread group (16x16=256 threads) processes a tile of HDR pixels.
/// Uses groupshared LDS for fast local accumulation, then merges into global UAV.
///
/// Root constants b0: { histLogMin, histLogMax, screenWidth, screenHeight, hdrSrvIndex }
/// UAV u0: RWStructuredBuffer<uint> histogram[64]

cbuffer ExposureParams : register(b0) {
    float g_HistLogMin;
    float g_HistLogMax;
    uint  g_ScreenWidth;
    uint  g_ScreenHeight;
    uint  g_HdrSrvIndex;
};

RWStructuredBuffer<uint> g_HistogramUAV : register(u0);

#ifndef UZ_BINDLESS
Texture2D<float4> g_HdrColorTex : register(t0);
#endif

#define NUM_BINS   64
#define GROUP_SIZE 256

static const float kLuminanceEpsilon = 1e-5;

groupshared uint g_LocalHistogram[NUM_BINS];

float Luminance(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

uint LogLumToBin(float logLum) {
    float t = saturate((logLum - g_HistLogMin) / (g_HistLogMax - g_HistLogMin));
    return min(uint(t * float(NUM_BINS - 1) + 0.5), NUM_BINS - 1);
}

[numthreads(16, 16, 1)]
void main(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    if (gi < NUM_BINS)
        g_LocalHistogram[gi] = 0;
    GroupMemoryBarrierWithGroupSync();

    if (dtid.x < g_ScreenWidth && dtid.y < g_ScreenHeight) {
#ifdef UZ_BINDLESS
        Texture2D<float4> hdr = ResourceDescriptorHeap[g_HdrSrvIndex];
#else
        Texture2D<float4> hdr = g_HdrColorTex;
#endif
        float3 color = hdr.Load(int3(dtid.xy, 0)).rgb;
        float lum = Luminance(color);
        if (lum > kLuminanceEpsilon) {
            uint bin = LogLumToBin(log2(lum));
            InterlockedAdd(g_LocalHistogram[bin], 1);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (gi < NUM_BINS && g_LocalHistogram[gi] > 0) {
        uint unused;
        InterlockedAdd(g_HistogramUAV[gi], g_LocalHistogram[gi], unused);
    }
}
