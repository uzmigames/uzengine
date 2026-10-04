/// UzEngine Debug View Pixel Shader
/// Visualizes G-Buffer channels based on the debug mode root constant.

#include "../common/constants.hlsli"
#include "../common/encoding.hlsli"

#ifdef UZ_BINDLESS
cbuffer RootConstants : register(b0) {
    uint g_mode;
    uint g_albedoSrv;
    uint g_normalSrv;
    uint g_depthSrv;
    uint g_positionSrv;
    uint g_hdrSrv;      // HDR scene color (LightingOnly / Reflections); UINT_MAX = unused
    uint g_ssrSrv;      // SSR resolve result (Reflections); UINT_MAX = unused
    uint g_directSrv;   // Deferred lighting SV_Target1 (Lighting / LightingOnly); UINT_MAX = unused
    uint g_indirectSrv; // Deferred lighting SV_Target2 (LightingOnly); UINT_MAX = unused
    uint g_shadowSrv;   // Deferred lighting SV_Target3, R = shadow, G = AO (Shadows); UINT_MAX = unused
    uint g_gtaoSrv;     // GTAO filtered AO (AO view); UINT_MAX = fall back to the G-buffer alpha
};
#else
cbuffer RootConstants : register(b0) {
    uint g_mode;
};
Texture2D<float4> t_albedo   : register(t0);
Texture2D<float4> t_normal   : register(t1);
Texture2D<float>  t_depth    : register(t2);
Texture2D<float4> t_position : register(t3);
Texture2D<float4> t_hdr      : register(t4);
Texture2D<float4> t_ssr      : register(t5);
#endif

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4 cameraPos;
    float4 timeParams;
    float4 lightDir;
    float4 lightColor;
    float4 ambientColor;
    row_major float4x4 invViewProj;
    float nearPlane, farPlane;
};

SamplerState pointSampler : register(s0);

float LinearizeDepth(float d, float n, float f) {
    // Reversed-Z: near=1, far=0
    return n / max(d, 1e-7);
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    int2 px = int2(pos.xy);

#ifdef UZ_BINDLESS
    Texture2D<float4> albedoTex   = ResourceDescriptorHeap[g_albedoSrv];
    Texture2D<float4> normalTex   = ResourceDescriptorHeap[g_normalSrv];
    Texture2D<float>  depthTex    = ResourceDescriptorHeap[g_depthSrv];
    Texture2D<float4> positionTex = ResourceDescriptorHeap[g_positionSrv];
    Texture2D<float4> hdrTex      = ResourceDescriptorHeap[g_hdrSrv];
    Texture2D<float4> ssrTex      = ResourceDescriptorHeap[g_ssrSrv];
#else
    #define albedoTex   t_albedo
    #define normalTex   t_normal
    #define depthTex    t_depth
    #define positionTex t_position
    #define hdrTex      t_hdr
    #define ssrTex      t_ssr
#endif

    float4 albedoData = albedoTex.Load(int3(px, 0));
    float4 normalData = normalTex.Load(int3(px, 0));
    float  depthRaw   = depthTex.Load(int3(px, 0));
    float4 posData    = positionTex.Load(int3(px, 0));

    float3 N = OctDecode(normalData.xy);

    switch (g_mode) {
    case 1: // Albedo
        return float4(albedoData.rgb, 1);

    case 2: // WorldNormal
        return float4(N * 0.5 + 0.5, 1);

    case 3: // Metallic
        return float4(normalData.bbb, 1);

    case 4: // Roughness
        return float4(normalData.aaa, 1);

    case 5: // AO — the GTAO result when there is one, the G-buffer alpha otherwise.
    {
#ifdef UZ_BINDLESS
        if (g_gtaoSrv != 0xFFFFFFFFu) {
            Texture2D<float4> gtaoTex = ResourceDescriptorHeap[g_gtaoSrv];
            return float4(gtaoTex.Load(int3(px, 0)).rrr, 1);
        }
#endif
        // Material AO from the G-buffer: flat wherever the material does not author it.
        return float4(albedoData.aaa, 1);
    }

    case 6: // Depth
    {
        float linDepth = LinearizeDepth(depthRaw, nearPlane, farPlane);
        float d = saturate(linDepth / 5000.0); // Normalize to 50m range
        return float4(d, d, d, 1);
    }

    case 7: // WorldPosition
        return float4(frac(posData.xyz * 0.01), 1); // Color grid at 1m intervals

    case 11: // Unlit (same as Albedo)
        return float4(albedoData.rgb, 1);

    case 13: // Tangent (derived from normal — approximate)
    {
        // Derive tangent direction from normal using cross with least-aligned axis
        float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
        float3 T = normalize(cross(up, N));
        return float4(T * 0.5 + 0.5, 1);
    }

    case 14: // Faceted (detect normal discontinuities via screen-space derivatives)
    {
        // Use ddx/ddy of world normal to detect hard edges
        float3 dNdx = ddx(N);
        float3 dNdy = ddy(N);
        float edgeStrength = length(dNdx) + length(dNdy);
        // Threshold: values > 0.1 indicate a hard edge
        float edge = smoothstep(0.05, 0.2, edgeStrength);
        // Show mesh with red highlights on hard edges
        float3 baseColor = albedoData.rgb * 0.5;
        float3 edgeColor = float3(1, 0, 0); // Red for hard edges
        return float4(lerp(baseColor, edgeColor, edge), 1);
    }

    case 15: // DetailLighting — directional NdotL × light color, no albedo, no IBL
    {
        // @ref Pharr 2023 "Physically Based Rendering" — diffuse BRDF decomposition.
        // Shows only the shading term contributed by the primary directional light.
        // lightDir in PerFrameCB is the direction FROM the surface TO the light (world space).
        float NdotL = saturate(dot(N, -lightDir.xyz));
        float3 detail = NdotL * lightColor.rgb;
        return float4(detail, 1);
    }

    // Cases 8, 9 and 16 read the deferred pass's lighting split targets (SV_Target1..3). They are
    // bindless-only: the SM5 path binds t0..t5, so there they fall through to the default branch
    // rather than display another texture as if it were lighting.
#ifdef UZ_BINDLESS
    case 8: // Lighting — direct light (Lo), no albedo
    {
        if (g_directSrv == 0xFFFFFFFFu) break;
        Texture2D<float4> directTex = ResourceDescriptorHeap[g_directSrv];
        return float4(directTex.Load(int3(px, 0)).rgb, 1);
    }

    case 9: // Shadows — the shadow mask (R), 0 = shadowed, 1 = lit
    {
        if (g_shadowSrv == 0xFFFFFFFFu) break;
        Texture2D<float4> shadowTex = ResourceDescriptorHeap[g_shadowSrv];
        return float4(shadowTex.Load(int3(px, 0)).rrr, 1);
    }

    case 16: // LightingOnly — direct + indirect, without the albedo multiply
    {
        // @ref Lagarde 2014 "Moving Frostbite to PBR" — lighting buffer extraction. Read from the
        // lighting split targets, not from the HDR scene colour: this pass writes (and clears) the
        // HDR resource, so reading it back rendered black.
        if (g_directSrv == 0xFFFFFFFFu || g_indirectSrv == 0xFFFFFFFFu) break;
        Texture2D<float4> directTex   = ResourceDescriptorHeap[g_directSrv];
        Texture2D<float4> indirectTex = ResourceDescriptorHeap[g_indirectSrv];
        const float3 lighting = directTex.Load(int3(px, 0)).rgb + indirectTex.Load(int3(px, 0)).rgb;
        return float4(lighting, 1);
    }
#endif

    case 18: // Reflections — SSR contribution only
    {
        // @ref McGuire 2014 "Efficient GPU Screen-Space Ray Tracing".
        // Displays the raw SSR resolve result (rgb=hit color, a=confidence).
        // Falls back to zero when SSR result is unavailable.
        float4 ssr = ssrTex.Load(int3(px, 0));
        return float4(ssr.rgb, 1);
    }

    default: // Lit or unknown — should not reach here
        return float4(1, 0, 1, 1); // Magenta = error
    }

    // Reached when a case breaks out: a mode whose source texture is not bound this frame (its SRV
    // index is the sentinel). Magenta says "no data for this mode" rather than showing another
    // channel as if it were the one asked for.
    return float4(1, 0, 1, 1);
}
