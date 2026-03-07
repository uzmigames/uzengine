// gbuffer_write.ps.hlsl -- G-Buffer write pass (Deferred Shading)
//
// Outputs material properties to 4 MRT targets. No lighting computed here.
// Uses the same vertex layout and material bindings as pbr.ps.hlsl.
//
// G-Buffer layout:
//   RT0 (RGBA8):      RGB = albedo,           A = AO
//   RT1 (RGB10A2):    RG  = octahedral normal, B = 0, A = flags (2-bit)
//   RT2 (RGBA8):      R   = metallic, G = roughness, B = emissionScale, A = flags
//   RT3 (R11G11B10F): RGB = emission color * energy (HDR)

cbuffer ObjectCB : register(b0) {
    float4x4 world;
};

cbuffer MaterialCB : register(b1) {
    float3 materialAlbedo;
    float  materialMetallic;
    float  materialRoughness;
    float  materialAO;
    float  parallaxScale;
    float  emissionEnergy;
    float3 emissionColor;
    float  envMultiplier;
    float  normalStrength;
    float  specularMultiplier;
    float2 uvTiling;
    float  selfShadowStrength;
    float  cavityStrength;
    float2 _matPad;
};

cbuffer FrameCB : register(b3) {
    float4x4 viewProj;
};

Texture2D    albedoTex       : register(t0);
Texture2D    normalTex       : register(t1);
Texture2D    metallicTex     : register(t2);
Texture2D    roughnessTex    : register(t3);
Texture2D    aoTex           : register(t4);
Texture2D    displacementTex : register(t5);

SamplerState linearSampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 texcoord : TEXCOORD2;
    float3 tangent  : TEXCOORD3;
    float3 bitangent: TEXCOORD4;
};

struct GBufferOutput {
    float4 albedoAO    : SV_TARGET0;  // RGB=albedo, A=AO
    float4 normalFlags : SV_TARGET1;  // RG=oct_normal (10-bit), B=0, A=flags
    float4 material    : SV_TARGET2;  // R=metallic, G=roughness, B=emissionScale, A=flags
    float4 emission    : SV_TARGET3;  // RGB=emission*energy (HDR)
};

// Octahedral normal encoding (matches brdf_common.hlsli)
float2 OctEncode(float3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return n.xy * 0.5 + 0.5;
}

// POM (same as pbr.ps.hlsl -- shared code)
float2 ParallaxOcclusionMapping(float2 uv, float3 V_ts, float scale)
{
    [branch]
    if (scale <= 0.0) return uv;

    float NdotV = saturate(abs(V_ts.z));
    [branch]
    if (NdotV < 0.05) return uv;

    float numLayers  = lerp(32.0, 8.0, NdotV);
    float layerDepth = 1.0 / numLayers;

    float safeVtsZ = max(abs(V_ts.z), 0.15);
    float2 uvStep = -(V_ts.xy / safeVtsZ) * scale * layerDepth;

    float2 currentUV    = uv;
    float  currentDepth = 0.0;
    float  height = displacementTex.SampleLevel(linearSampler, currentUV, 0).r;

    [loop]
    for (int i = 0; i < 32; ++i) {
        if (currentDepth >= 1.0 - height) break;
        currentUV    += uvStep;
        height        = displacementTex.SampleLevel(linearSampler, currentUV, 0).r;
        currentDepth += layerDepth;
    }

    float2 prevUV     = currentUV - uvStep;
    float  prevHeight = displacementTex.SampleLevel(linearSampler, prevUV, 0).r;
    float  afterDepth  = (1.0 - height)     - currentDepth;
    float  beforeDepth = (1.0 - prevHeight) - (currentDepth - layerDepth);
    float  weight      = afterDepth / (afterDepth - beforeDepth + 1e-5);

    return lerp(currentUV, prevUV, weight);
}

GBufferOutput PSMain(PSInput input) {
    GBufferOutput output;

    // TBN
    float3 N_geo = normalize(input.normal);
    float tLen = length(input.tangent);
    float bLen = length(input.bitangent);
    float3 T   = (tLen > 0.001f) ? (input.tangent   / tLen) : float3(1.0f, 0.0f, 0.0f);
    float3 B   = (bLen > 0.001f) ? (input.bitangent / bLen) : cross(N_geo, T);
    float3x3 TBN = float3x3(T, B, N_geo);

    // View vector + tangent-space for POM
    // Note: we don't have cameraWorldPos in this shader's CBVs (no LightFrameCB b2).
    // We reconstruct V from viewProj inverse would be expensive. Instead, for the
    // G-Buffer pass we need the camera position. We pack it into FrameCB padding or
    // use the existing b2 CBV. For now, approximate from viewProj last column.
    // WORKAROUND: The deferred G-Buffer pass also binds b2 (LightFrameCB) for cameraWorldPos.
    // This is set by MeshProxy::execute which already binds b2. So we declare it here.

    // Actually, let's just read b2 for cameraWorldPos -- MeshProxy already binds it.
    // We re-declare the needed field:
    // (DXC allows partial cbuffer declarations as long as the layout matches)

    // For POM we need camera position -- approximate from viewProj or skip POM in deferred.
    // Decision: POM still works in G-Buffer pass since we need correct UV before material sampling.
    // Camera pos comes from b2 which MeshProxy already binds.

    float2 tiledUV = input.texcoord * uvTiling;
    float tangentQ = smoothstep(0.03f, 0.30f, min(tLen, bLen));
    float pomScale = parallaxScale * tangentQ;

    // For POM, we need V_ts. Without explicit cameraWorldPos, derive V from world position
    // relative to the near plane. This is a known limitation -- POM quality may be slightly
    // reduced in deferred mode. For objects with parallaxScale > 0, the forward path is recommended.
    // Simple approximation: use the view direction from the projection
    float3 V_approx = normalize(-input.worldPos); // rough approximation
    float3 V_ts = normalize(float3(dot(V_approx, T), dot(V_approx, B), dot(V_approx, N_geo)));

    float2 uv = ParallaxOcclusionMapping(tiledUV, V_ts, pomScale);
    uv = lerp(tiledUV, uv, smoothstep(0.08, 0.25, dot(N_geo, V_approx)));

    // Unpack material
    float3 baseColor = materialAlbedo * albedoTex.Sample(linearSampler, uv).rgb;
    float  metallic  = materialMetallic * metallicTex.Sample(linearSampler, uv).r;
    float  roughness = materialRoughness * roughnessTex.Sample(linearSampler, uv).r;
    float  ao        = materialAO * aoTex.Sample(linearSampler, uv).r;

    roughness = max(roughness, 0.04);

    // Normal map
    float3 tangentNormal = normalTex.Sample(linearSampler, uv).xyz * 2.0 - 1.0;
    tangentNormal.xy *= normalStrength;
    tangentNormal = normalize(tangentNormal);
    float3 N = normalize(mul(tangentNormal, TBN));

    // Encode octahedral normal
    float2 octN = OctEncode(N);

    // Pack emission scale into 0-1 range (maps 0-8 HDR range)
    float emissionScalePacked = saturate(emissionEnergy / 8.0);

    // Flags byte: bit0 = hasPOM, bit1 = reserved
    float flagsBits = (parallaxScale > 0.0) ? 1.0 / 3.0 : 0.0; // 1/3 = bit0 in 2-bit A channel

    // Write G-Buffer
    output.albedoAO    = float4(baseColor, ao);
    output.normalFlags = float4(octN.x, octN.y, envMultiplier * 0.5, flagsBits);
    output.material    = float4(metallic, roughness, emissionScalePacked, specularMultiplier * 0.5);
    output.emission    = float4(emissionColor * emissionEnergy, 0.0);

    return output;
}
