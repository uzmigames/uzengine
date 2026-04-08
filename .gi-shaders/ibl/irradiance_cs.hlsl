/// UzEngine irradiance convolution — cosine-weighted hemisphere integration.
/// @ref Ramamoorthi & Hanrahan 2001 "Efficient Irradiance Normal Mapping"
///
/// Root constants at b0: { envCubemapSrvIndex, faceSize }
/// Output: RWTexture2DArray<float4> at u0

#include "../common/constants.hlsli"
#include "../common/sampling.hlsli"

cbuffer RootConstants : register(b0) {
    uint envCubemapSrvIndex;
    uint faceSize;
};

#ifdef UZ_BINDLESS
#else
TextureCube<float4> envMap : register(t0);
#endif

RWTexture2DArray<float4> outputIrradiance : register(u0);
SamplerState linearSampler : register(s0);

float3 CubeFaceDirection(uint face, float2 uv) {
    float u = uv.x * 2.0 - 1.0;
    float v = uv.y * 2.0 - 1.0;
    switch (face) {
        case 0: return normalize(float3( 1, -v, -u));
        case 1: return normalize(float3(-1, -v,  u));
        case 2: return normalize(float3( u,  1,  v));
        case 3: return normalize(float3( u, -1, -v));
        case 4: return normalize(float3( u, -v,  1));
        case 5: return normalize(float3(-u, -v, -1));
        default: return float3(0, 0, 1);
    }
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= faceSize || DTid.y >= faceSize) return;

    float2 uv = (float2(DTid.xy) + 0.5) / float(faceSize);
    float3 N = CubeFaceDirection(DTid.z, uv);

    float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);

    float3 irradiance = float3(0, 0, 0);
    const uint SAMPLE_COUNT = 256;
    for (uint i = 0; i < SAMPLE_COUNT; ++i) {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float pdf;
        float3 localDir = SampleCosineHemisphere(Xi, pdf);
        float3 sampleDir = localDir.x * T + localDir.y * B + localDir.z * N;
#ifdef UZ_BINDLESS
        TextureCube<float4> envTex = ResourceDescriptorHeap[envCubemapSrvIndex];
        irradiance += envTex.SampleLevel(linearSampler, sampleDir, 0).rgb;
#else
        irradiance += envMap.SampleLevel(linearSampler, sampleDir, 0).rgb;
#endif
    }
    irradiance /= float(SAMPLE_COUNT);
    outputIrradiance[DTid] = float4(irradiance, 1.0);
}
