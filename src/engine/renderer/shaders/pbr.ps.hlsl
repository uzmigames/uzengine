// pbr.ps.hlsl -- Advanced PBR pixel shader
// Standard PBR metallic-roughness workflow with TBN normal mapping.
//
// Root constants / CBV:
//   b0: ObjectCB (world matrix)
//   b1: MaterialCB (albedo, metallic, roughness, ao, emission)
//   b2: LightFrameCB (lights, camera, time, env)
//   b3: FrameCB (viewProj)

cbuffer ObjectCB : register(b0) {
    float4x4 world;
};

struct Light {
    float3 position;
    float  intensity;
    float3 color;
    float  range;
    float3 direction;
    float  type; // 0=Dir, 1=Point, 2=Spot
};

cbuffer MaterialCB : register(b1) {
    float4 materialAlbedo;      // rgb=color, a=unused
    float4 materialParams;      // x=metallic, y=roughness, z=ao, w=emission_energy
    float3 materialEmission;    // rgb=emission color
    float  _pad0;
};

#define MAX_LIGHTS 8
cbuffer LightFrameCB : register(b2) {
    Light  lights[MAX_LIGHTS];
    uint   lightCount;
    float3 cameraWorldPos;
    float  time;
    float  envIntensity;
};

cbuffer FrameCB : register(b3) {
    float4x4 viewProj;
};

// Textures (t0-t4 match root signature SRV tables)
Texture2D    albedoTex    : register(t0);
Texture2D    normalTex    : register(t1);
Texture2D    metallicTex  : register(t2);
Texture2D    roughnessTex : register(t3);
Texture2D    aoTex        : register(t4);

// TODO Phase 2: Add emission (t5), IBL irradiance (t6), prefilter (t7), brdfLUT (t8)
// when root signature is expanded to support them.

SamplerState linearSampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 texcoord : TEXCOORD2;
    float3 tangent  : TEXCOORD3;
    float3 bitangent: TEXCOORD4;
};

static const float PI = 3.14159265359;

// ---------------------------------------------------------------------------
// PBR Math Functions
// ---------------------------------------------------------------------------

float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return num / max(denom, 0.0001);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return num / max(denom, 0.0001);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

float4 PSMain(PSInput input) : SV_TARGET {
    // 1. Unpack material properties
    float3 albedo   = materialAlbedo.rgb * albedoTex.Sample(linearSampler, input.texcoord).rgb;
    float  metallic = materialParams.x * metallicTex.Sample(linearSampler, input.texcoord).r;
    float  roughness = materialParams.y * roughnessTex.Sample(linearSampler, input.texcoord).r;
    float  ao       = materialParams.z * aoTex.Sample(linearSampler, input.texcoord).r;
    float3 emission = materialEmission * materialParams.w;

    // 2. TBN Normal Mapping
    float3 tangentNormal = normalTex.Sample(linearSampler, input.texcoord).xyz * 2.0 - 1.0;
    float3 N = normalize(input.normal);
    float3 T = normalize(input.tangent);
    float3 B = normalize(input.bitangent);
    float3x3 TBN = float3x3(T, B, N);
    float3 worldNormal = normalize(mul(tangentNormal, TBN));

    // 3. View Vector
    float3 V = normalize(cameraWorldPos - input.worldPos);
    float  NdotV = max(dot(worldNormal, V), 0.0);

    // 4. Fresnel F0 for dielectric/metallic
    float3 F0 = float3(0.04, 0.04, 0.04); 
    F0 = lerp(F0, albedo, metallic);

    // 5. Direct Lighting (Multiple Lights)
    float3 Lo = float3(0.0, 0.0, 0.0);
    for(uint i = 0; i < lightCount; ++i) {
        float3 L;
        float attenuation = 1.0;
        
        if (lights[i].type == 0.0) { // Directional
            L = normalize(-lights[i].direction);
        } else { // Point or Spot
            float3 lightVec = lights[i].position - input.worldPos;
            float distance = length(lightVec);
            L = normalize(lightVec);
            attenuation = 1.0 / (distance * distance);
            
            // Range cutoff
            if (lights[i].range > 0.0) {
                attenuation *= saturate(1.0 - pow(distance / lights[i].range, 4.0));
            }
            
            if (lights[i].type == 2.0) { // Spot
                float theta = dot(L, normalize(-lights[i].direction));
                // Basic spot falloff
                attenuation *= saturate((theta - 0.5) / (0.9 - 0.5));
            }
        }

        float3 H = normalize(V + L);
        float  NdotL = max(dot(worldNormal, L), 0.0);
        
        // Cook-Torrance BRDF
        float  D = DistributionGGX(worldNormal, H, roughness);
        float  G = GeometrySmith(worldNormal, V, L, roughness);
        float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

        float3 numerator  = D * G * F;
        float  denominator = 4.0 * NdotV * NdotL + 0.0001;
        float3 specular   = numerator / denominator;

        float3 kS = F;
        float3 kD = (float3(1.0, 1.0, 1.0) - kS) * (1.0 - metallic);
        
        Lo += (kD * albedo / PI + specular) * lights[i].color * lights[i].intensity * attenuation * NdotL;
    }

    // 6. Ambient / IBL (Phase 2 simplified)
    // For now, use a simple ambient with IBL placeholders logic
    float3 ambient;
    {
        float3 kS = FresnelSchlickRoughness(NdotV, F0, roughness);
        float3 kD = (1.0 - kS) * (1.0 - metallic);
        
        // Irradiance (ambient diffuse)
        float3 irradiance = float3(0.03, 0.03, 0.03); // Placeholder
        float3 diffuse = irradiance * albedo;
        
        // TODO: Prefiltered Environment + BRDF LUT for ambient specular
        float3 ambientSpecular = float3(0.01, 0.01, 0.01) * kS; // Placeholder
        
        ambient = (kD * diffuse + ambientSpecular) * ao * envIntensity;
    }

    // 7. Final Color Assembly
    float3 color = ambient + Lo + emission;

    // 8. Tonemapping (ACES approximation)
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    color = saturate((color * (a * color + b)) / (color * (c * color + d) + e));

    // 9. Gamma Correction
    color = pow(color, 1.0 / 2.2);

    return float4(color, 1.0);
}