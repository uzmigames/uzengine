/// UzEngine GGX prefiltered environment map — split-sum specular IBL.
/// @ref Karis 2013 "Real Shading in Unreal Engine 4" — prefiltered environment
/// @ref Lagarde 2014 "Moving Frostbite to PBR" — mip-roughness mapping
///
/// Root constants at b0: { envSrvIndex, faceSize, roughnessBits, sampleCount }
/// Output: RWTexture2DArray<float4> per-mip cubemap face at u0

#include "../common/constants.hlsli"
#include "../common/sampling.hlsli"

cbuffer RootConstants : register(b0) {
    uint envCubemapSrvIndex;
    uint faceSize;
    uint roughnessBits;    // float roughness reinterpreted as uint
    uint sampleCount;
};

#ifdef UZ_BINDLESS
#else
TextureCube<float4> envMap : register(t0);
#endif

RWTexture2DArray<float4> outputPrefiltered : register(u0);
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

    float roughness = asfloat(roughnessBits);
    float2 uv = (float2(DTid.xy) + 0.5) / float(faceSize);
    float3 N = CubeFaceDirection(DTid.z, uv);
    float3 R = N; // Assumption: V = N for prefiltering (isotropic)
    float3 V = R;

    float3 prefilteredColor = float3(0, 0, 0);
    float totalWeight = 0.0;

    // NOTE: NDF sampling (ImportanceSampleGGX) is kept here intentionally.
    // The prefilter assumes V=N=R, so the view direction is the normal itself.
    // When V=N=(0,0,1) in tangent space, VNDF degenerates to NDF (all upper-
    // hemisphere normals are visible from directly above). Switching to
    // SampleGGX_VNDF would produce identical results with no quality gain.
    // @ref Dupuy & Benyoub 2023, Section 3.2 — σ_std(i) = (1+z_i)/2 → 1 when z_i=1
    for (uint i = 0; i < sampleCount; ++i) {
        float2 Xi = Hammersley(i, sampleCount);
        float3 H  = ImportanceSampleGGX(Xi, roughness);

        // Transform H from tangent space to world space
        float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
        float3 T = normalize(cross(up, N));
        float3 B = cross(N, T);
        H = normalize(H.x * T + H.y * B + H.z * N);

        float3 L = 2.0 * dot(V, H) * H - V;
        float NdotL = max(dot(N, L), 0.0);

        if (NdotL > 0.0) {
            // @ref Lagarde 2014 "Moving Frostbite to PBR" — mip selection from PDF
            // Without mip bias, high-roughness mips retain HDR hotspots (sun in HDRI)
            // because limited samples can't integrate them. Sampling a higher source mip
            // pre-filters the hotspot, eliminating fireflies and false reflections.
            float NdotH = max(dot(N, H), 0.0);
            float HdotV = max(dot(H, V), 0.0);
            float a2 = roughness * roughness;
            a2 = a2 * a2; // alpha = roughness^2, a2 = alpha^2
            float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
            float D = a2 / (PI * denom * denom + 0.0001);
            float pdf = D * NdotH / (4.0 * HdotV) + 0.0001;
            float saTexel  = 4.0 * PI / (6.0 * faceSize * faceSize);
            float saSample = 1.0 / (float(sampleCount) * pdf + 0.0001);
            float mipBias = roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);
            mipBias = max(mipBias, 0.0);

#ifdef UZ_BINDLESS
            TextureCube<float4> envTex = ResourceDescriptorHeap[envCubemapSrvIndex];
            prefilteredColor += envTex.SampleLevel(linearSampler, L, mipBias).rgb * NdotL;
#else
            prefilteredColor += envMap.SampleLevel(linearSampler, L, mipBias).rgb * NdotL;
#endif
            totalWeight += NdotL;
        }
    }

    prefilteredColor /= max(totalWeight, 0.001);
    outputPrefiltered[DTid] = float4(prefilteredColor, 1.0);
}
