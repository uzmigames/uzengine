// ibl_brdf_lut.cs.hlsl -- GPU compute BRDF integration LUT.
//
// Precomputes the split-sum BRDF integral (Karis 2013).
// Output: RG16F texture where R = scale, G = bias.
//   F_env = F0 * scale + bias
//
// Dispatch: (LUT_SIZE / 8, LUT_SIZE / 8, 1)

static const float PI = 3.14159265358979323846;
static const uint SAMPLE_COUNT = 1024;

cbuffer ComputeCB : register(b0) {
    uint  g_LutSize;
    uint3 _pad;
};

RWTexture2D<float2> g_Output : register(u0);

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

// Importance-sample GGX: returns half-vector H in tangent space (N = 0,0,1).
float3 ImportanceSampleGGX(float2 Xi, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

// Smith GGX geometry for IBL (k = a^2 / 2).
float GeometrySchlickGGX_IBL(float NdotV, float roughness) {
    float a = roughness;
    float k = (a * a) / 2.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith_IBL(float NdotV, float NdotL, float roughness) {
    return GeometrySchlickGGX_IBL(NdotV, roughness) *
           GeometrySchlickGGX_IBL(NdotL, roughness);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= g_LutSize || dtid.y >= g_LutSize) return;

    float NdotV     = (float(dtid.x) + 0.5) / float(g_LutSize);
    float roughness = (float(dtid.y) + 0.5) / float(g_LutSize);
    NdotV     = max(NdotV, 0.001);
    roughness = max(roughness, 0.01);

    // V in tangent space where N = (0, 0, 1)
    float3 V = float3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);

    float A = 0.0; // Scale (F0-independent)
    float B = 0.0; // Bias

    for (uint i = 0; i < SAMPLE_COUNT; ++i) {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H  = ImportanceSampleGGX(Xi, roughness);
        float3 L  = 2.0 * dot(V, H) * H - V;

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0) {
            float G  = GeometrySmith_IBL(NdotV, NdotL, roughness);
            float Gc = (G * VdotH) / (NdotH * NdotV + 1e-7);
            float Fc = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * Gc;
            B += Fc * Gc;
        }
    }

    A /= float(SAMPLE_COUNT);
    B /= float(SAMPLE_COUNT);

    g_Output[dtid.xy] = float2(saturate(A), saturate(B));
}
