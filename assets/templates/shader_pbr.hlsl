// {{NAME}} -- PBR shader with Cook-Torrance BRDF.
//
// Implements physically-based shading using:
//   - GGX (Trowbridge-Reitz) normal distribution function
//   - Smith height-correlated visibility / geometry term
//   - Fresnel-Schlick approximation
//
// cbuffer layout:
//   b0 PerFrame  -- camera matrices and position (updated once per frame)
//   b1 PerObject -- world matrix and material parameters (updated per draw call)
//   b2 PerLight  -- single directional light (updated per pass)

// ---------------------------------------------------------------------------
// Constant buffers
// ---------------------------------------------------------------------------

cbuffer PerFrame : register(b0) {
    float4x4 viewProjection;
    float3   cameraPos;
    float    _pad0;
};

cbuffer PerObject : register(b1) {
    float4x4 world;
    float4   albedoColor;   // RGB albedo, A unused
    float    metallic;      // 0 = dielectric, 1 = metallic
    float    roughness;     // 0 = mirror-smooth, 1 = fully rough
    float2   _pad1;
};

cbuffer PerLight : register(b2) {
    float3 lightDir;        // World-space direction TOWARD the light (normalized)
    float  _pad2;
    float3 lightColor;      // Linear RGB radiance
    float  lightIntensity;  // Scalar multiplier
};

// ---------------------------------------------------------------------------
// Vertex / pixel interface
// ---------------------------------------------------------------------------

struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

struct PSInput {
    float4 positionCS : SV_POSITION;  // Clip-space position
    float3 positionWS : TEXCOORD0;    // World-space position (for V vector)
    float3 normalWS   : TEXCOORD1;    // World-space normal
    float2 uv         : TEXCOORD2;
};

// ---------------------------------------------------------------------------
// Vertex shader
// ---------------------------------------------------------------------------

PSInput VSMain(VSInput input) {
    PSInput output;

    float4 worldPos    = mul(world, float4(input.position, 1.0f));
    output.positionCS  = mul(viewProjection, worldPos);
    output.positionWS  = worldPos.xyz;

    // Transform normal by the inverse-transpose of the world matrix.
    // For uniform scaling this equals the world matrix upper-left 3x3.
    float3x3 world3    = (float3x3)world;
    output.normalWS    = normalize(mul(world3, input.normal));

    output.uv          = input.uv;
    return output;
}

// ---------------------------------------------------------------------------
// PBR helper functions
// ---------------------------------------------------------------------------

static const float PI = 3.14159265358979323846f;

/// GGX normal distribution function D(h).
float D_GGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / max(PI * d * d, 1e-7f);
}

/// Smith geometry term for a single direction.
float G_SchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdotV / max(NdotV * (1.0f - k) + k, 1e-7f);
}

/// Height-correlated Smith geometry G(l, v, h).
float G_Smith(float NdotL, float NdotV, float roughness) {
    return G_SchlickGGX(NdotL, roughness) * G_SchlickGGX(NdotV, roughness);
}

/// Fresnel-Schlick approximation F(v, h).
float3 F_Schlick(float VdotH, float3 F0) {
    return F0 + (1.0f - F0) * pow(saturate(1.0f - VdotH), 5.0f);
}

// ---------------------------------------------------------------------------
// Pixel shader
// ---------------------------------------------------------------------------

float4 PSMain(PSInput input) : SV_TARGET {
    // Material parameters.
    float3 albedo    = albedoColor.rgb;
    float  metal     = saturate(metallic);
    float  rough     = max(roughness, 0.04f);   // Clamp to avoid NaN at 0

    // Base reflectance F0: blend dielectric (0.04) and metallic (albedo).
    float3 F0        = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metal);

    // Direction vectors.
    float3 N         = normalize(input.normalWS);
    float3 V         = normalize(cameraPos - input.positionWS);
    float3 L         = normalize(lightDir);
    float3 H         = normalize(V + L);

    float NdotL      = max(dot(N, L), 0.0f);
    float NdotV      = max(dot(N, V), 1e-4f);
    float NdotH      = max(dot(N, H), 0.0f);
    float VdotH      = max(dot(V, H), 0.0f);

    // Cook-Torrance specular BRDF.
    float  D         = D_GGX(NdotH, rough);
    float  G         = G_Smith(NdotL, NdotV, rough);
    float3 F         = F_Schlick(VdotH, F0);

    float3 numerator  = D * G * F;
    float  denominator = max(4.0f * NdotL * NdotV, 1e-7f);
    float3 specular    = numerator / denominator;

    // Diffuse: metals have no diffuse contribution.
    float3 kD          = (1.0f - F) * (1.0f - metal);
    float3 diffuse     = kD * albedo / PI;

    // Combined radiance from the directional light.
    float3 radiance    = lightColor * lightIntensity;
    float3 Lo          = (diffuse + specular) * radiance * NdotL;

    // Simple ambient term (IBL would replace this in a full pipeline).
    float3 ambient     = 0.03f * albedo;

    float3 color       = ambient + Lo;

    // Reinhard tone mapping + gamma correction.
    color = color / (color + 1.0f);
    color = pow(max(color, 0.0f), 1.0f / 2.2f);

    return float4(color, 1.0f);
}
