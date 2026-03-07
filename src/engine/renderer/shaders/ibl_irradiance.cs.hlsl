// ibl_irradiance.cs.hlsl -- GPU compute irradiance cubemap convolution.
//
// Convolves an environment cubemap into a diffuse irradiance cubemap
// by integrating the cosine-weighted hemisphere.
//
// Input:  TextureCube (t0) -- source environment cubemap
// Output: RWTexture2DArray (u0) -- irradiance cubemap (6 faces)
// Dispatch: (IRR_SIZE / 8, IRR_SIZE / 8, 6)

static const float PI = 3.14159265358979323846;
static const uint  SAMPLE_COUNT = 1024;

cbuffer ComputeCB : register(b0) {
    uint  g_OutputSize;   // irradiance cubemap face resolution
    uint3 _pad;
};

TextureCube<float4>         g_EnvCube : register(t0);
SamplerState                g_Sampler : register(s0);
RWTexture2DArray<float4>    g_Output  : register(u0);

// Van der Corput radical inverse (base 2).
float RadicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N) {
    return float2(float(i) / float(N), RadicalInverseVdC(i));
}

// Convert cube face + UV to world direction.
float3 CubeUVToDir(uint face, float2 uv) {
    // Map [0,1] -> [-1,1]
    float u = uv.x * 2.0 - 1.0;
    float v = uv.y * 2.0 - 1.0;
    switch (face) {
        case 0: return normalize(float3( 1.0,   -v,   -u)); // +X
        case 1: return normalize(float3(-1.0,   -v,    u)); // -X
        case 2: return normalize(float3(   u,  1.0,    v)); // +Y
        case 3: return normalize(float3(   u, -1.0,   -v)); // -Y
        case 4: return normalize(float3(   u,   -v,  1.0)); // +Z
        case 5: return normalize(float3(  -u,   -v, -1.0)); // -Z
        default: return float3(0, 1, 0);
    }
}

// Build TBN from normal N (tangent space where N = up).
void BuildTBN(float3 N, out float3 T, out float3 B) {
    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    T = normalize(cross(up, N));
    B = cross(N, T);
}

// Cosine-weighted hemisphere sample direction.
float3 CosineSampleHemisphere(float2 Xi) {
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt(1.0 - Xi.y);
    float sinTheta = sqrt(Xi.y);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= g_OutputSize || dtid.y >= g_OutputSize) return;
    uint face = dtid.z;
    if (face >= 6) return;

    float2 uv = (float2(dtid.xy) + 0.5) / float(g_OutputSize);
    float3 N = CubeUVToDir(face, uv);

    float3 T, B;
    BuildTBN(N, T, B);

    float3 irradiance = float3(0, 0, 0);

    for (uint i = 0; i < SAMPLE_COUNT; ++i) {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 localDir = CosineSampleHemisphere(Xi);

        // Transform from tangent space to world space
        float3 worldDir = T * localDir.x + B * localDir.y + N * localDir.z;

        // Cosine-weighted: PDF = cos(theta)/PI, so integral = (1/N) * sum(Li)
        // (the cosine is already baked into the PDF of CosineSampleHemisphere)
        irradiance += g_EnvCube.SampleLevel(g_Sampler, worldDir, 0).rgb;
    }

    irradiance *= PI / float(SAMPLE_COUNT);

    g_Output[uint3(dtid.xy, face)] = float4(irradiance, 1.0);
}
