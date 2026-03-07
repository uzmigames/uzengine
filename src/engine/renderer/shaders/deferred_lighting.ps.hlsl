// deferred_lighting.ps.hlsl -- Fullscreen deferred lighting pass
//
// Reads G-Buffer targets + depth, reconstructs world position,
// applies the same PBR lighting model as the forward path (pbr.ps.hlsl).
//
// G-Buffer layout (inputs):
//   t0 (RGBA8):      RGB = albedo,           A = AO
//   t1 (RGB10A2):    RG  = octahedral normal, B = envMultiplier*0.5, A = flags
//   t2 (RGBA8):      R   = metallic, G = roughness, B = emissionScale, A = specMul*0.5
//   t3 (R11G11B10F): RGB = emission color * energy (HDR)
//   t4 (D24S8):      depth buffer
//   t5:              shadow map (D32)
//   t6:              BRDF LUT (RG16F)
//   t7:              irradiance cubemap
//   t8:              prefiltered specular cubemap

// b0: DeferredLightingCB (16 root constants)
cbuffer DeferredLightingCB : register(b0) {
    float4x4 invViewProj;
};

// b3: LightParamsCB (16 root constants)
cbuffer LightParamsCB : register(b3) {
    float3 lightDir;
    float  lightIntensity;
    float3 lightColor;
    float  _lightPad;
    float3 cameraWorldPos;
    float  envIntensity;
    float  shadowBias;
    float  shadowOrthoSize;
    float  shadowLightDist;
    float  _pad2;
};

Texture2D    gbuffer0       : register(t0);  // Albedo + AO
Texture2D    gbuffer1       : register(t1);  // Normal (oct) + envMul + flags
Texture2D    gbuffer2       : register(t2);  // Metallic + Roughness + emissionScale + specMul
Texture2D    gbuffer3       : register(t3);  // Emission (HDR)
Texture2D    depthTex       : register(t4);  // Scene depth
Texture2D    shadowMap      : register(t5);  // Shadow depth map
Texture2D    brdfLUT        : register(t6);  // BRDF integration LUT
TextureCube  irradianceMap  : register(t7);  // Diffuse irradiance
TextureCube  prefilteredMap : register(t8);  // Specular prefiltered env

SamplerState linearSampler : register(s0);
SamplerState clampSampler  : register(s1);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

static const float PI = 3.14159265359;

// ============================================================================
// BRDF functions (inlined to avoid #include in embedded shaders)
// ============================================================================

float Pow5(float x) {
    float x2 = x * x;
    return x2 * x2 * x;
}

float D_GGX(float a2, float NoH) {
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * d * d);
}

float Vis_SmithJointApprox(float a2, float NoV, float NoL) {
    float a = sqrt(a2);
    float Vis_SmithV = NoL * (NoV * (1.0 - a) + a);
    float Vis_SmithL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / (Vis_SmithV + Vis_SmithL + 1e-5);
}

float3 F_Schlick(float3 F0, float VoH) {
    float Fc = Pow5(1.0 - VoH);
    float f90 = saturate(50.0 * F0.g);
    return f90 * Fc + (1.0 - Fc) * F0;
}

float3 F_SchlickRoughness(float3 F0, float VoH, float roughness) {
    float Fc = Pow5(1.0 - VoH);
    float3 F90 = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0);
    return F0 + (F90 - F0) * Fc;
}

float3 Diffuse_Burley(float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH) {
    float FD90 = 0.5 + 2.0 * VoH * VoH * Roughness;
    float FdV = 1.0 + (FD90 - 1.0) * Pow5(1.0 - NoV);
    float FdL = 1.0 + (FD90 - 1.0) * Pow5(1.0 - NoL);
    return DiffuseColor * ((1.0 / PI) * FdV * FdL);
}

float SpecularOcclusion(float NoV, float ao, float roughness) {
    return saturate(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}

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

    float  D   = D_GGX(a2, NoH);
    float  Vis = Vis_SmithJointApprox(a2, NoV, NoL);
    float3 F   = F_Schlick(F0, VoH);
    float3 specular = D * Vis * F;

    float3 diffuse = Diffuse_Burley(DiffuseColor, roughness, NoV, NoL, VoH);

    return (diffuse + specular) * lColor * lIntensity * NoL;
}

// Octahedral decode
float3 OctDecode(float2 f) {
    f = f * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}

// PCF Shadow
float SampleShadow(float3 worldPos, float3 worldNormal) {
    float3 lightFwd = normalize(lightDir);
    float3 L_dir    = normalize(-lightFwd);
    float  NdotL_s  = saturate(dot(worldNormal, L_dir));
    float  sinTheta = sqrt(saturate(1.0 - NdotL_s * NdotL_s));
    float  tanTheta = sinTheta / max(NdotL_s, 1e-3);
    float  normalOffsetScale = clamp(tanTheta * 0.015, 0.0, 0.04);
    worldPos = worldPos + worldNormal * normalOffsetScale;

    float3 refUp  = (abs(lightFwd.y) > 0.99) ? float3(1, 0, 0) : float3(0, 1, 0);
    float3 lRight = normalize(cross(refUp, lightFwd));
    float3 lUp    = cross(lightFwd, lRight);

    float3 lightEye = -lightFwd * shadowLightDist;
    float3 relPos   = worldPos - lightEye;

    float lx = dot(relPos, lRight);
    float ly = dot(relPos, lUp);
    float lz = dot(relPos, lightFwd);

    float2 shadowUV = float2(lx, -ly) / shadowOrthoSize * 0.5 + 0.5;
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 ||
        shadowUV.y < 0.0 || shadowUV.y > 1.0)
        return 1.0;

    float shadowFar    = shadowLightDist * 2.0;
    float currentDepth = (lz - 0.1) / (shadowFar - 0.1);

    float shadow = 0.0;
    float2 texelSize = 1.0 / float2(2048.0, 2048.0);

    static const float2 poissonDisk[16] = {
        float2(-0.94201624, -0.39906216), float2( 0.94558609, -0.76890725),
        float2(-0.09418410, -0.92938870), float2( 0.34495938,  0.29387760),
        float2(-0.91588581,  0.45771432), float2(-0.81544232, -0.87912464),
        float2(-0.38277543,  0.27676845), float2( 0.97484398,  0.75648379),
        float2( 0.44323325, -0.97511554), float2( 0.53742981, -0.47373420),
        float2(-0.26496911, -0.41893023), float2( 0.79197514,  0.19090188),
        float2(-0.24188840,  0.99706507), float2(-0.81409955,  0.91437590),
        float2( 0.19984126,  0.78641367), float2( 0.14383161, -0.14100790)
    };

    float penumbra = clamp(currentDepth * 3.5, 0.5, 2.5);

    [unroll]
    for (int i = 0; i < 16; ++i) {
        float closestDepth = shadowMap.Sample(linearSampler,
            shadowUV + poissonDisk[i] * texelSize * penumbra).r;
        shadow += (currentDepth - shadowBias > closestDepth) ? 0.0 : 1.0;
    }
    return shadow / 16.0;
}

// ============================================================================
// Main -- Reconstruct world pos from depth, read G-Buffer, apply lighting
// ============================================================================

float4 PSMain(PSInput input) : SV_TARGET {
    float2 uv = input.texcoord;
    int2 pixelCoord = int2(input.position.xy);

    // Read G-Buffer
    float4 gb0 = gbuffer0.Load(int3(pixelCoord, 0));
    float4 gb1 = gbuffer1.Load(int3(pixelCoord, 0));
    float4 gb2 = gbuffer2.Load(int3(pixelCoord, 0));
    float4 gb3 = gbuffer3.Load(int3(pixelCoord, 0));
    float  rawDepth = depthTex.Load(int3(pixelCoord, 0)).r;

    // Skip sky pixels (depth == 1.0)
    if (rawDepth >= 0.9999)
        discard;

    // Unpack G-Buffer
    float3 baseColor = gb0.rgb;
    float  ao        = gb0.a;
    float3 N         = OctDecode(gb1.rg);
    float  envMul    = gb1.b * 2.0;  // was packed as * 0.5
    float  metallic  = gb2.r;
    float  roughness = gb2.g;
    float  specMul   = gb2.a * 2.0;  // was packed as * 0.5
    float3 emission  = gb3.rgb;

    roughness = max(roughness, 0.04);
    float alpha = roughness * roughness;
    float a2    = alpha * alpha;

    // Reconstruct world position from depth + invViewProj
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 clipPos = float4(ndc, rawDepth, 1.0);
    float4 worldH  = mul(invViewProj, clipPos);
    float3 worldPos = worldH.xyz / worldH.w;

    // View vector
    float3 V   = normalize(cameraWorldPos - worldPos);
    float  NoV = max(dot(N, V), 1e-4);

    // F0 and diffuse color
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float3 DiffuseColor = baseColor * (1.0 - metallic);

    // ===================================================================
    // Direct Lighting
    // ===================================================================
    float3 Lo = float3(0.0, 0.0, 0.0);
    float shadowFactor = SampleShadow(worldPos, N);

    {
        float3 directLight = EvaluateDirectLight(N, V, NoV, DiffuseColor, F0, a2, roughness,
                                                  lightDir, lightColor, lightIntensity);
        float sScale = lerp(1.0, specMul, metallic * 0.5 + 0.5);
        float NoL_direct = saturate(dot(N, normalize(-lightDir)));
        float microShadow = saturate(NoL_direct + 2.0 * ao * ao - 1.0);
        Lo += directLight * sScale * shadowFactor * microShadow;
    }

    // ===================================================================
    // Ambient / IBL
    // ===================================================================
    float3 ambient;
    {
        float3 irradiance = irradianceMap.Sample(clampSampler, N).rgb;

        float3 kS_env = F_SchlickRoughness(F0, NoV, roughness);
        float3 kD_env = (1.0 - kS_env) * (1.0 - metallic);
        float3 diffuseAmbient = kD_env * irradiance * baseColor;

        float3 R = reflect(-V, N);
        float  mipLevel = roughness * 4.0;
        float3 prefilteredColor = prefilteredMap.SampleLevel(clampSampler, R, mipLevel).rgb;

        float2 envBRDF = brdfLUT.Sample(clampSampler, float2(NoV, roughness)).rg;
        float3 specularAmbient = prefilteredColor * (F0 * envBRDF.x + envBRDF.y);

        float horizon = saturate(1.0 + dot(R, N));
        specularAmbient *= horizon * horizon;

        // Multi-scattering [Kulla & Conty 2017]
        float3 FssEss = F0 * envBRDF.x + envBRDF.y;
        float  Ess    = envBRDF.x + envBRDF.y;
        float  Ems    = 1.0 - Ess;
        float3 Favg   = F0 + (1.0 - F0) / 21.0;
        float3 FmsEms = Ems * FssEss * Favg / (1.0 - Favg * Ems);
        specularAmbient += FmsEms * irradiance * 0.5;

        float specOcc = SpecularOcclusion(NoV, ao, roughness);
        specularAmbient *= specOcc;

        ambient = (diffuseAmbient * ao + specularAmbient) * envIntensity * envMul;
    }

    // Final color = ambient + direct + emission
    float3 color = ambient + Lo + emission;

    return float4(max(color, 0.0), 1.0);
}
