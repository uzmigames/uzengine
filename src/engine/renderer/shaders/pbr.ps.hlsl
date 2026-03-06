// pbr.ps.hlsl — Physically-Based pixel shader
// BRDF: D_GGX + Vis_SmithJointApprox (UE5) + F_Schlick (f90 clamp) + Burley Diffuse
// Ambient: EnvBRDFApprox (Lazarov 2013) + Hemisphere Sky + 3-Point Studio Lighting
//
// References:
//   UE5 — Engine/Shaders/Private/BRDF.ush, ShadingModels.ush
//   Godot — servers/rendering/renderer_rd/shaders/scene_forward_lights_inc.glsl
//   Filament — https://google.github.io/filament/Filament.md.html
//
// Root constants / CBV:
//   b0: ObjectCB (world matrix)
//   b1: MaterialCB (albedo, metallic, roughness, ao)
//   b2: LightFrameCB (lights, camera, time, env)
//   b3: FrameCB (viewProj)

cbuffer ObjectCB : register(b0) {
    float4x4 world;
};

cbuffer MaterialCB : register(b1) {
    float3 materialAlbedo;
    float  materialMetallic;
    float  materialRoughness;
    float  materialAO;
    float2 _matPad;
};

cbuffer LightFrameCB : register(b2) {
    float3 lightDir;
    float  lightIntensity;
    float3 lightColor;
    float  _lightPad;
    float3 cameraWorldPos;
    float  time;
    float  envIntensity;
    float3 _framePad;
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
    float3 tangent  : TEXCOORD3;
    float3 bitangent: TEXCOORD4;
};

static const float PI = 3.14159265359;

// ============================================================================
// BRDF Functions — matched to UE5 BRDF.ush and Godot's forward lights
// ============================================================================

// pow5 helper (UE5 Pow5)
float Pow5(float x) {
    float x2 = x * x;
    return x2 * x2 * x;
}

// GGX / Trowbridge-Reitz NDF
// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
// Source: UE5 BRDF.ush D_GGX
float D_GGX(float a2, float NoH) {
    float d = (NoH * a2 - NoH) * NoH + 1.0;  // 2 mad
    return a2 / (PI * d * d);                  // 4 mul, 1 rcp
}

// Approximation of joint Smith term for GGX
// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
// Source: UE5 BRDF.ush Vis_SmithJointApprox — returns G / (4*NoL*NoV) directly
float Vis_SmithJointApprox(float a2, float NoV, float NoL) {
    float a = sqrt(a2);
    float Vis_SmithV = NoL * (NoV * (1.0 - a) + a);
    float Vis_SmithL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / (Vis_SmithV + Vis_SmithL + 1e-5);
}

// Schlick Fresnel
// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
// Source: UE5 BRDF.ush F_Schlick — uses saturate(50*SpecularColor.g) as f90
// "Anything less than 2% is physically impossible and is instead considered to be shadowing"
float3 F_Schlick(float3 F0, float VoH) {
    float Fc = Pow5(1.0 - VoH);
    float f90 = saturate(50.0 * F0.g);  // UE5 f90 clamp
    return f90 * Fc + (1.0 - Fc) * F0;
}

// Fresnel with explicit roughness (for ambient/IBL)
float3 F_SchlickRoughness(float3 F0, float VoH, float roughness) {
    float Fc = Pow5(1.0 - VoH);
    float3 F90 = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0);
    return F0 + (F90 - F0) * Fc;
}

// Burley Diffuse
// [Burley 2012, "Physically-Based Shading at Disney"]
// Source: UE5 BRDF.ush Diffuse_Burley, Godot DIFFUSE_BURLEY
float3 Diffuse_Burley(float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH) {
    float FD90 = 0.5 + 2.0 * VoH * VoH * Roughness;
    float FdV = 1.0 + (FD90 - 1.0) * Pow5(1.0 - NoV);
    float FdL = 1.0 + (FD90 - 1.0) * Pow5(1.0 - NoL);
    return DiffuseColor * ((1.0 / PI) * FdV * FdL);
}

// EnvBRDF Approximation — analytical, no LUT texture needed
// [Lazarov 2013, "Getting More Physical in Call of Duty: Black Ops II"]
// Source: UE5 BRDF.ush EnvBRDFApproxLazarov
float3 EnvBRDFApprox(float3 SpecularColor, float Roughness, float NoV) {
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4( 1.0,  0.0425,  1.04, -0.04);
    float4 r = Roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    float2 AB = float2(-1.04, 1.04) * a004 + r.zw;

    // f90 for the environment: anything below 2% is treated as shadowing
    float f90 = saturate(50.0 * SpecularColor.g);
    return SpecularColor * AB.x + f90 * AB.y;
}

// ============================================================================
// Evaluate a single analytical light — full Cook-Torrance with Burley diffuse
// ============================================================================
float3 EvaluateDirectLight(
    float3 N, float3 V, float NoV,
    float3 DiffuseColor, float3 F0, float a2, float roughness,
    float3 lightDirection, float3 lColor, float lIntensity)
{
    float3 L = normalize(-lightDirection);
    float  NoL = max(dot(N, L), 0.0);
    if (NoL <= 0.0) return float3(0.0, 0.0, 0.0);

    float3 H = normalize(V + L);
    float  NoH = saturate(dot(N, H));
    float  VoH = saturate(dot(V, H));

    // Specular: D * Vis * F (UE5 pattern: f = D * Vis * F)
    float  D   = D_GGX(a2, NoH);
    float  Vis = Vis_SmithJointApprox(a2, NoV, NoL);
    float3 F   = F_Schlick(F0, VoH);
    float3 specular = D * Vis * F;

    // Diffuse: Burley (UE5/Godot default)
    float3 kS = F;
    float3 diffuse = Diffuse_Burley(DiffuseColor, roughness, NoV, NoL, VoH);

    // Energy split: diffuse is already modulated by (1-F) implicitly in Burley
    // but we still need the metallic gate
    return (diffuse + specular) * lColor * lIntensity * NoL;
}

// ============================================================================
// Main
// ============================================================================

float4 PSMain(PSInput input) : SV_TARGET {
    // 1. Unpack material properties
    float3 baseColor = materialAlbedo * albedoTex.Sample(linearSampler, input.texcoord).rgb;
    float  metallic  = materialMetallic * metallicTex.Sample(linearSampler, input.texcoord).r;
    float  roughness = materialRoughness * roughnessTex.Sample(linearSampler, input.texcoord).r;
    float  ao        = materialAO * aoTex.Sample(linearSampler, input.texcoord).r;

    // Clamp roughness to avoid singularities (UE5 does min 0.002)
    roughness = max(roughness, 0.04);
    float alpha = roughness * roughness;  // Both UE5 and Godot use alpha = roughness²
    float a2    = alpha * alpha;          // a² for D_GGX

    // 2. TBN Normal Mapping
    float3 tangentNormal = normalTex.Sample(linearSampler, input.texcoord).xyz * 2.0 - 1.0;
    float3 N = normalize(input.normal);
    float3 T = normalize(input.tangent);
    float3 B = normalize(input.bitangent);
    float3x3 TBN = float3x3(T, B, N);
    N = normalize(mul(tangentNormal, TBN));

    // 3. View vector
    float3 V    = normalize(cameraWorldPos - input.worldPos);
    float  NoV  = max(dot(N, V), 1e-4);  // UE5/Godot: abs(NoV) + 1e-5 or max(dot, 1e-4)

    // 4. F0 — specular reflectance at normal incidence
    // UE5: F0 = lerp(DielectricSpecular, BaseColor, Metallic)  (DielectricSpecular = 0.04)
    // Godot: F0 = mix(vec3(0.16 * specular²), albedo, metallic) — with specular=0.5 → 0.04
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);

    // DiffuseColor: non-metals reflect diffusely, metals do not
    float3 DiffuseColor = baseColor * (1.0 - metallic);

    // ===================================================================
    // 5. Direct Lighting — 3-Point Studio Setup
    // ===================================================================
    float3 Lo = float3(0.0, 0.0, 0.0);

    // Key light (scene-driven directional)
    Lo += EvaluateDirectLight(N, V, NoV, DiffuseColor, F0, a2, roughness,
                              lightDir, lightColor, lightIntensity);

    // Fill light — cool, from opposite side (15% of key)
    Lo += EvaluateDirectLight(N, V, NoV, DiffuseColor, F0, a2, roughness,
                              normalize(float3(0.6, -0.2, -0.75)),
                              float3(0.50, 0.55, 0.65), lightIntensity * 0.15);

    // Rim / back light — warm, from behind (10% of key)
    Lo += EvaluateDirectLight(N, V, NoV, DiffuseColor, F0, a2, roughness,
                              normalize(float3(-0.3, -0.6, 0.7)),
                              float3(0.85, 0.70, 0.50), lightIntensity * 0.10);

    // ===================================================================
    // 6. Ambient / IBL — EnvBRDFApprox + Hemisphere Sky
    // ===================================================================
    float3 ambient;
    {
        // Hemisphere sky irradiance — warmer tones for stone/outdoor
        float3 skyColor    = float3(0.20, 0.22, 0.30);
        float3 groundColor = float3(0.12, 0.10, 0.08);
        float3 equatorColor = float3(0.16, 0.14, 0.12);
        float  skyBlend = N.y * 0.5 + 0.5;

        float3 irradiance;
        if (skyBlend < 0.5) {
            irradiance = lerp(groundColor, equatorColor, skyBlend * 2.0);
        } else {
            irradiance = lerp(equatorColor, skyColor, (skyBlend - 0.5) * 2.0);
        }

        // Diffuse ambient
        float3 kS_env = F_SchlickRoughness(F0, NoV, roughness);
        float3 kD_env = (1.0 - kS_env) * (1.0 - metallic);
        float3 diffuseAmbient = kD_env * irradiance * baseColor;

        // Specular ambient — EnvBRDFApprox (UE5 Lazarov 2013)
        // Modulated by approximate environment reflection color
        float3 R = reflect(-V, N);
        float  rBlend = saturate(R.y * 0.5 + 0.5);
        float3 envColor = lerp(groundColor * 1.5, skyColor * 1.5, rBlend);
        float  horizonFade = saturate(1.0 + dot(R, N));

        float3 envBRDF = EnvBRDFApprox(F0, roughness, NoV);
        float3 specularAmbient = envColor * envBRDF * horizonFade;

        // Strong AO for deep crevice shadows (cubed for stone materials)
        float aoFinal = ao * ao * ao;
        ambient = (diffuseAmbient + specularAmbient) * aoFinal * envIntensity;
    }

    // 7. Final assembly — apply AO to direct lighting for micro-occlusion
    float microAO = lerp(1.0, ao, 0.5); // subtle direct light AO
    float3 color = ambient + Lo * microAO;

    // 8. Tonemapping — ACES (Narkowicz fit)
    {
        float a = 2.51;
        float b = 0.03;
        float c = 2.43;
        float d = 0.59;
        float e = 0.14;
        color = saturate((color * (a * color + b)) / (color * (c * color + d) + e));
    }

    // 9. Gamma correction (linear → sRGB)
    color = pow(color, 1.0 / 2.2);

    return float4(color, 1.0);
}