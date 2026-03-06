// pbr.ps.hlsl -- PBR pixel shader (Cook-Torrance BRDF)
// Physically-based rendering with metallic-roughness workflow.
//
// Root constants:
//   b0: ObjectCB (world matrix)
//   b1: MaterialCB (albedo, metallic, roughness, ao)
//   b2: LightCamCB (dir, intensity, color, cameraPos, time)
//   b3: FrameCB (viewProj matrix)
//
// Textures (Descriptor Tables at Root Params 4..8):
//   t0: albedo texture
//   t1: normal texture
//   t2: metallic texture
//   t3: roughness texture
//   t4: ao texture
//
// Sampler:
//   s0: linear wrap (static sampler from root signature)

cbuffer ObjectCB : register(b0) {
    float4x4 world;
};

cbuffer MaterialCB : register(b1) {
    float4 materialColor; // rgb=albedo, a=metallic
    float4 pbrParams;     // x=roughness, y=ao, zw=unused
};

cbuffer LightCamCB : register(b2) {
    float3 lightDir;
    float  lightIntensity;
    float3 lightColor;
    float  _unused0;
    float3 cameraWorldPos;
    float  time;
};

cbuffer FrameCB : register(b3) {
    float4x4 viewProj;
};

Texture2D    albedoTex    : register(t0);
Texture2D    normalTex    : register(t1);
Texture2D    metallicTex  : register(t2);
Texture2D    roughnessTex : register(t3);
Texture2D    aoTex        : register(t4);

SamplerState linearSampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 texcoord : TEXCOORD2;
};

static const float PI = 3.14159265359;

// ---------------------------------------------------------------------------
// PBR Functions
// ---------------------------------------------------------------------------

float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float k) {
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return num / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float k) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, k);
    float ggx1 = GeometrySchlickGGX(NdotL, k);
    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

float4 PSMain(PSInput input) : SV_TARGET {
    // Material properties
    float3 albedo   = materialColor.rgb;
    float  metallic = materialColor.a;
    float  rough    = max(pbrParams.x, 0.04);
    float  ao       = pbrParams.y;

    // Sample textures if they were bound (RHI binds white/dummy if null)
    float4 texAlbedo = albedoTex.Sample(linearSampler, input.texcoord);
    albedo *= texAlbedo.rgb;

    float4 texMet = metallicTex.Sample(linearSampler, input.texcoord);
    metallic *= texMet.r;

    float4 texRough = roughnessTex.Sample(linearSampler, input.texcoord);
    rough = saturate(rough * texRough.r);

    float4 texAo = aoTex.Sample(linearSampler, input.texcoord);
    ao *= texAo.r;

    // TODO: Normal mapping with tangent space normals
    float3 N = normalize(input.normal);

    // Vectors
    float3 V = normalize(cameraWorldPos - input.worldPos);
    float3 L = normalize(-lightDir);
    float3 H = normalize(V + L);

    // F0: dielectric=0.04, metallic=albedo
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    // Cook-Torrance BRDF
    float  D = DistributionGGX(N, H, rough);
    float  k = (rough + 1.0) * (rough + 1.0) / 8.0;
    float  G = GeometrySmith(N, V, L, k);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator  = D * G * F;
    float  denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    float3 specular   = numerator / max(denominator, 0.001);

    // Energy conservation
    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);
    float3 Lo = (kD * albedo / PI + specular) * lightColor * lightIntensity * NdotL;

    // Ambient with AO
    float3 ambient = 0.03 * albedo * ao;

    float3 color = ambient + Lo;

    // Tone mapping (simple Reinhard)
    color = color / (color + 1.0);
    // Gamma correction
    color = pow(color, 1.0 / 2.2);

    return float4(color, 1.0);
}
