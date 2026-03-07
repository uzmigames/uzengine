// ibl_prefilter.cs.hlsl -- GPU compute prefiltered environment map.
//
// GGX importance-sampled specular convolution for split-sum IBL.
// Dispatched once per mip level with roughness passed via root constants.
//
// Input:  TextureCube (t0) -- source environment cubemap
// Output: RWTexture2DArray (u0) -- one mip level of prefiltered cubemap (6 faces)
// Dispatch: (mipSize / 8, mipSize / 8, 6)

static const float PI = 3.14159265358979323846;
static const uint  SAMPLE_COUNT = 256;

cbuffer ComputeCB : register(b0) {
    uint  g_OutputSize;   // mip level face resolution
    float g_Roughness;    // roughness for this mip level
    uint  g_EnvSize;      // source cubemap resolution (for LOD calculation)
    uint  _pad;
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

// Importance-sample GGX NDF: returns half-vector H in tangent space.
float3 ImportanceSampleGGX(float2 Xi, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

// Convert cube face + UV to world direction.
float3 CubeUVToDir(uint face, float2 uv) {
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

// Build TBN from normal N.
void BuildTBN(float3 N, out float3 T, out float3 B) {
    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    T = normalize(cross(up, N));
    B = cross(N, T);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= g_OutputSize || dtid.y >= g_OutputSize) return;
    uint face = dtid.z;
    if (face >= 6) return;

    float2 uv = (float2(dtid.xy) + 0.5) / float(g_OutputSize);
    float3 N = CubeUVToDir(face, uv);

    // For prefilter, assume V = N (isotropic approximation, per Karis 2013).
    float3 R = N;
    float3 V = N;

    float3 T, B;
    BuildTBN(N, T, B);

    float roughness = max(g_Roughness, 0.01);

    float3  prefilteredColor = float3(0, 0, 0);
    float   totalWeight = 0.0;

    for (uint i = 0; i < SAMPLE_COUNT; ++i) {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H_local = ImportanceSampleGGX(Xi, roughness);

        // Transform H from tangent space to world space
        float3 H = T * H_local.x + B * H_local.y + N * H_local.z;
        float3 L = 2.0 * dot(V, H) * H - V;

        float NdotL = dot(N, L);
        if (NdotL > 0.0) {
            // Mip level selection based on PDF to reduce aliasing.
            float NdotH = max(dot(N, H), 0.0);
            float HdotV = max(dot(H, V), 0.0);
            float a2 = roughness * roughness * roughness * roughness;
            float D = a2 / (PI * pow(NdotH * NdotH * (a2 - 1.0) + 1.0, 2.0));
            float pdf = D * NdotH / (4.0 * HdotV + 0.0001);
            float saTexel  = 4.0 * PI / (6.0 * g_EnvSize * g_EnvSize);
            float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001);
            float mipLevel = (roughness == 0.0) ? 0.0 : 0.5 * log2(saSample / saTexel);

            prefilteredColor += g_EnvCube.SampleLevel(g_Sampler, L, mipLevel).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    if (totalWeight > 0.0)
        prefilteredColor /= totalWeight;

    g_Output[uint3(dtid.xy, face)] = float4(prefilteredColor, 1.0);
}
