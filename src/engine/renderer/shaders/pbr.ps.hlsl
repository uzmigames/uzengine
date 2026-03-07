// pbr.ps.hlsl — Physically-Based pixel shader
// BRDF: D_GGX + Vis_SmithJointApprox (UE5) + F_Schlick (f90 clamp) + Burley Diffuse
// IBL: Cubemap irradiance (diffuse) + Prefiltered env + BRDF LUT (specular split-sum)
// Displacement: Parallax Occlusion Mapping (POM) -- steep loop (8-32 layers) + linear interp
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
    float  parallaxScale;       // > 0 enables POM; 0 = no-op
    float  emissionEnergy;      // multiplier for emission color (0 = no emission)
    float3 emissionColor;       // HDR emission color
    float  envMultiplier;       // per-material IBL intensity (default 1.0)
    float  normalStrength;      // normal map intensity: 0=flat, 1=full (default 1.0)
    float  specularMultiplier;  // direct specular intensity (default 1.0)
    float2 uvTiling;            // UV tiling multiplier (default 1,1)
    // -- Extended params (DWORDs 16-19, fills root sig to 64/64) --
    float  selfShadowStrength;  // heightmap self-shadow intensity: 0=off, 1=default (default 1.0)
    float  cavityStrength;      // cavity ambient darkening: 0=off, 0.35=default (default 0.35)
    float  anisotropy;          // anisotropy strength: 0=isotropic, -1..1=anisotropic (default 0)
    float  anisotropyRotation;  // rotation angle in radians for anisotropy direction (default 0)
};

// Punctual light structure: 12 floats per light (3 float4 rows)
struct PunctualLight {
    float3 position;    float radius;
    float3 color;       float intensity;
    float3 direction;   float spotParams;  // packed: x=cosInner, encoded for spot; 0 for point
};

cbuffer LightFrameCB : register(b2) {
    float3 lightDir;
    float  lightIntensity;
    float3 lightColor;
    float  _lightPad;
    float3 cameraWorldPos;
    float  time;
    float  envIntensity;
    float  shadowBias;
    float  shadowOrthoSize;
    float  shadowLightDist;
    // Punctual lights (after 16 DWORDs of directional data)
    uint   punctualLightCount;  // 0..8
    float3 _plPad;
    PunctualLight punctualLights[8];
};

cbuffer FrameCB : register(b3) {
    float4x4 viewProj;
};

Texture2D    albedoTex      : register(t0);
Texture2D    normalTex      : register(t1);
Texture2D    metallicTex    : register(t2);
Texture2D    roughnessTex   : register(t3);
Texture2D    aoTex          : register(t4);
Texture2D    displacementTex: register(t5);  // Heightmap: 1=high, 0=low
Texture2D    shadowMap      : register(t6);  // Directional shadow depth map (R32_FLOAT)
Texture2D    brdfLUT        : register(t7);  // 256x256 RG16F split-sum BRDF integration LUT
TextureCube  irradianceMap  : register(t8);  // 32x32 diffuse irradiance cubemap
TextureCube  prefilteredMap : register(t9);  // 128x128 specular prefiltered cubemap (5 mips)

SamplerState linearSampler : register(s0);
SamplerState clampSampler  : register(s1);   // Linear clamp for BRDF LUT / env maps

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
// Parallax Occlusion Mapping
// ============================================================================
// V_ts : view vector in tangent space (z > 0 toward camera)
// scale: height scale (0.02-0.08 for stone; 0 = no-op, skips the loop)
// Layer count: 8 at NdotV=1 (normal incidence), 32 at NdotV~0 (grazing angle)
// ============================================================================
float2 ParallaxOcclusionMapping(float2 uv, float3 V_ts, float scale)
{
    [branch]
    if (scale <= 0.0) return uv;

    float NdotV = saturate(abs(V_ts.z));

    // Skip POM at extreme grazing angles and degenerate tangent space (sphere poles).
    // At poles, UV derivatives converge → V_ts is garbage → ray marching explodes.
    // NdotV < 0.05 means view is nearly tangent to surface; no perceptible parallax.
    [branch]
    if (NdotV < 0.05) return uv;

    float numLayers  = lerp(32.0, 8.0, NdotV);
    float layerDepth = 1.0 / numLayers;

    // Clamp V_ts.z to prevent UV step explosion at sphere poles / grazing angles.
    // Without this, V_ts.z≈0 at poles makes uvStep → ∞, causing black artifact patches.
    float safeVtsZ = max(abs(V_ts.z), 0.15);
    float2 uvStep = -(V_ts.xy / safeVtsZ) * scale * layerDepth;

    float2 currentUV    = uv;
    float  currentDepth = 0.0;
    // height: 1 = raised (brick face), 0 = recessed (mortar joint)
    float  height = displacementTex.SampleLevel(linearSampler, currentUV, 0).r;

    // Steep parallax: march until ray depth >= surface depth (1 - height)
    [loop]
    for (int i = 0; i < 32; ++i) {
        if (currentDepth >= 1.0 - height) break;
        currentUV    += uvStep;
        height        = displacementTex.SampleLevel(linearSampler, currentUV, 0).r;
        currentDepth += layerDepth;
    }

    // Linear interpolation between last two steps for accurate intersection
    float2 prevUV     = currentUV - uvStep;
    float  prevHeight = displacementTex.SampleLevel(linearSampler, prevUV, 0).r;
    float  afterDepth  = (1.0 - height)     - currentDepth;
    float  beforeDepth = (1.0 - prevHeight) - (currentDepth - layerDepth);
    float  weight      = afterDepth / (afterDepth - beforeDepth + 1e-5);

    return lerp(currentUV, prevUV, weight);
}


// ============================================================================
// Heightmap Self-Shadow — trace light direction on displacement for contact shadows
// ============================================================================
// [Policarpo 2005, "Real-Time Relief Mapping on Arbitrary Polygonal Surfaces"]
// Traces 4 steps along the light direction in UV space. If a neighboring
// heightmap sample is above the light ray, the current pixel is shadowed.
// This creates the visible ridge→valley shadows that sand ripples, brick
// mortar, and woven fabric need — far too small for the shadow map to resolve.
//
// L_ts : light direction in tangent space (z > 0 = light above surface)
// scale: same parallaxScale used for POM (> 0 enables; 0 = no-op)
// h0   : heightmap value at current (POM-offset) UV
// ============================================================================
float HeightmapSelfShadow(float2 uv, float3 L_ts, float scale, float h0)
{
    [branch]
    if (scale <= 0.0) return 1.0;
    [branch]
    if (L_ts.z < 0.01) return 0.3;  // nearly tangent light → partially shadowed

    float safeLz = max(L_ts.z, 0.12);
    float2 uvStep = L_ts.xy / safeLz * scale * 0.25;

    // Accumulate occlusion (soft average) instead of max (hard binary).
    // This produces smooth gradients instead of harsh dark patches.
    float shadow = 0.0;
    float2 sUV = uv;
    [unroll]
    for (int i = 1; i <= 6; ++i) {
        sUV += uvStep;
        float h = displacementTex.SampleLevel(linearSampler, sUV, 0).r;
        float rayH = h0 + float(i) * 0.08;
        // Closer taps weighted more (1.0 → 0.4 across 6 steps).
        float weight = 1.0 - float(i - 1) * 0.12;
        shadow += max(0.0, (h - rayH) * weight);
    }
    shadow /= 3.0;  // normalize: divide by half the tap count for gentle curve

    // Floor at 0.25 so recesses are never pitch-black; smooth falloff.
    return lerp(0.25, 1.0, saturate(1.0 - shadow * 8.0));
}


// ============================================================================
// PCF Shadow Sampling — 3×3 kernel, orthographic light-space reconstruction
// ============================================================================
// Returns shadow factor: 1.0 = fully lit, 0.0 = fully shadowed.
// Reconstructs light-space from lightDir + shadowOrthoSize + shadowLightDist
// (matches the CPU computation in renderer.cpp — no matrix needed in root sig).
//
// worldNormal: surface normal, used for normal-offset anti self-shadow bias.
// Low-angle surfaces (floor at grazing light) need a larger offset than
// steep surfaces (sphere top) — proportional to sqrt(1 - NdotL²) / NdotL.
float SampleShadow(float3 worldPos, float3 worldNormal) {
    // Normal-offset bias: push the query point along the surface normal to prevent
    // self-shadowing on flat surfaces at grazing light angles (e.g. floor).
    // Scale: larger offset when NdotL is small (grazing angle).
    float3 lightFwd = normalize(lightDir);
    float3 L_dir    = normalize(-lightFwd);
    float  NdotL_s  = saturate(dot(worldNormal, L_dir));
    // UE5-style normal offset: offsetScale grows as sin(theta)/cos(theta) = tan(theta)
    float  sinTheta = sqrt(saturate(1.0 - NdotL_s * NdotL_s));
    float  tanTheta = sinTheta / max(NdotL_s, 1e-3);
    float  normalOffsetScale = clamp(tanTheta * 0.015, 0.0, 0.04);
    worldPos = worldPos + worldNormal * normalOffsetScale;
    float3 refUp    = (abs(lightFwd.y) > 0.99) ? float3(1, 0, 0) : float3(0, 1, 0);
    float3 lRight   = normalize(cross(refUp, lightFwd));  // matches CPU lightRight
    float3 lUp      = cross(lightFwd, lRight);             // matches CPU lightUp

    // Light eye: sceneCenter(0) - lightFwd * dist  (matches CPU: -ld * shadowLightDist)
    float3 lightEye = -lightFwd * shadowLightDist;
    float3 relPos   = worldPos - lightEye;

    // Project into light space (matches CPU view matrix dot products)
    float lx = dot(relPos, lRight);
    float ly = dot(relPos, lUp);
    float lz = dot(relPos, lightFwd);

    // Orthographic UV: shadowOrthoSize = half-extent → maps [-size,+size] to [0,1]
    float2 shadowUV = float2(lx, -ly) / shadowOrthoSize * 0.5 + 0.5;
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 ||
        shadowUV.y < 0.0 || shadowUV.y > 1.0)
        return 1.0;  // Outside shadow frustum — fully lit

    // Linear depth in [0,1] (near=0.1, far=shadowLightDist*2)
    float shadowFar    = shadowLightDist * 2.0;
    float currentDepth = (lz - 0.1) / (shadowFar - 0.1);

    // CSM: map shadow UV into cascade 0 quadrant (top-left 1024x1024 of 4096 atlas)
    // Scale = kCascadeSize / kShadowAtlasSize = 1024 / 4096 = 0.25
    shadowUV = shadowUV * 0.25;
    // texel size relative to cascade region within the atlas
    float shadow = 0.0;
    float2 texelSize = 1.0 / float2(4096.0, 4096.0);

    // Poisson disk offsets (unit circle, 16 taps, hand-picked for good distribution)
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

    // Penumbra size: proportional to how far receiver is from caster in shadow space.
    // Clamped to [0.5, 2.5] texels to avoid over-blurring or aliasing.
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

// Anisotropic GGX NDF
// [Burley 2012, "Physically-Based Shading at Disney"]
// Source: UE5 ShadingModels.ush D_GGXaniso, Filament material_anisotropy.glsl
float D_GGX_Anisotropic(float ax, float ay, float NoH, float XoH, float YoH) {
    float d = (XoH * XoH) / (ax * ax) + (YoH * YoH) / (ay * ay) + NoH * NoH;
    return 1.0 / (PI * ax * ay * d * d);
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

// Anisotropic Smith-GGX visibility (height-correlated)
// [Heitz 2014] — anisotropic extension of Vis_SmithJoint
float Vis_SmithJoint_Anisotropic(float ax, float ay, float NoV, float NoL,
                                  float XoV, float XoL, float YoV, float YoL) {
    float lambdaV = NoL * sqrt(ax * ax * XoV * XoV + ay * ay * YoV * YoV + NoV * NoV);
    float lambdaL = NoV * sqrt(ax * ax * XoL * XoL + ay * ay * YoL * YoL + NoL * NoL);
    return 0.5 / (lambdaV + lambdaL + 1e-5);
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

// Specular Occlusion — [Lagarde 2014, Jimenez 2016]
// Used by UE5 (GetSpecularOcclusion) and Filament.
// Dims specular IBL at grazing angles and in occluded crevices.
// Prevents the bright white fresnel ring around smooth spheres.
float SpecularOcclusion(float NoV, float ao, float roughness) {
    return saturate(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}

// ============================================================================
// Evaluate a single analytical light — full Cook-Torrance with Burley diffuse
// ============================================================================
float3 EvaluateDirectLight(
    float3 N, float3 V, float NoV,
    float3 DiffuseColor, float3 F0, float a2, float roughness,
    float3 lightDirection, float3 lColor, float lIntensity,
    float3 anisoT, float3 anisoB, float anisoAx, float anisoAy)
{
    float3 L = normalize(-lightDirection);
    float  NoL = max(dot(N, L), 0.0);
    if (NoL <= 0.0) return float3(0.0, 0.0, 0.0);

    float3 H = normalize(V + L);
    float  NoH = saturate(dot(N, H));
    float  VoH = saturate(dot(V, H));

    // Specular: D * Vis * F — anisotropic path when ax != ay
    float  D, Vis;
    [branch]
    if (abs(anisoAx - anisoAy) > 0.001) {
        float XoH = dot(anisoT, H);
        float YoH = dot(anisoB, H);
        float XoV = dot(anisoT, V);
        float YoV = dot(anisoB, V);
        float XoL = dot(anisoT, L);
        float YoL = dot(anisoB, L);
        D   = D_GGX_Anisotropic(anisoAx, anisoAy, NoH, XoH, YoH);
        Vis = Vis_SmithJoint_Anisotropic(anisoAx, anisoAy, NoV, NoL, XoV, XoL, YoV, YoL);
    } else {
        D   = D_GGX(a2, NoH);
        Vis = Vis_SmithJointApprox(a2, NoV, NoL);
    }
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
    // 1. TBN -- built first so V_ts is available for POM
    float3 N_geo = normalize(input.normal);

    // Degenerate tangent space check: at UV sphere poles and mesh UV seams,
    // adjacent triangles have tangents pointing in opposite directions. After
    // rasterizer linear interpolation the tangent length collapses toward zero
    // → normalizing gives garbage TBN.
    // Use a SMOOTH FADE rather than a hard cutoff so the seam transition is invisible:
    // smoothstep(0.05, 0.25) gives a gradual POM fade-in/out at seams/poles.
    float tLen = length(input.tangent);
    float bLen = length(input.bitangent);
    float3 T   = (tLen > 0.001f) ? (input.tangent   / tLen) : float3(1.0f, 0.0f, 0.0f);
    float3 B   = (bLen > 0.001f) ? (input.bitangent / bLen) : cross(N_geo, T);
    float3x3 TBN = float3x3(T, B, N_geo);

    // 2. View vector + tangent-space view for POM, light for self-shadow
    float3 V    = normalize(cameraWorldPos - input.worldPos);
    float3 V_ts = normalize(float3(dot(V, T), dot(V, B), dot(V, N_geo)));
    float3 L_world = normalize(-lightDir);
    float3 L_ts = normalize(float3(dot(L_world, T), dot(L_world, B), dot(L_world, N_geo)));

    // 3. Parallax Occlusion Mapping -- offset UV before ALL texture samples.
    // tangentQ smoothly fades POM to 0 near UV seams instead of a hard cutoff,
    // hiding the seam transition that would otherwise appear as a sharp line.
    float tangentQ = smoothstep(0.03f, 0.30f, min(tLen, bLen));
    // UV tiling: scale texture coordinates before POM and all texture samples.
    float2 tiledUV = input.texcoord * uvTiling;

    float pomScale = parallaxScale * tangentQ;
    float2 uv = ParallaxOcclusionMapping(tiledUV, V_ts, pomScale);
    // Fade POM offset at grazing view angles to prevent artifacts at sphere silhouette edges.
    // When geometric NdotV is low, the POM ray march is unreliable — blend back to flat UV.
    uv = lerp(tiledUV, uv, smoothstep(0.08, 0.25, dot(N_geo, V)));

    // 4. Unpack material properties (POM-offset UV)
    float3 baseColor = materialAlbedo * albedoTex.Sample(linearSampler, uv).rgb;
    float  metallic  = materialMetallic * metallicTex.Sample(linearSampler, uv).r;
    float  roughness = materialRoughness * roughnessTex.Sample(linearSampler, uv).r;
    float  ao        = materialAO * aoTex.Sample(linearSampler, uv).r;

    // Clamp roughness to avoid singularities (UE5 does min 0.002)
    roughness = max(roughness, 0.04);
    float alpha = roughness * roughness;  // Both UE5 and Godot use alpha = roughness²
    float a2    = alpha * alpha;          // a² for D_GGX

    // 5. Normal map (POM-offset UV; TBN already built in step 1)
    //    normalStrength: 0=flat (use geometric normal), 1=full normal map
    float3 tangentNormal = normalTex.Sample(linearSampler, uv).xyz * 2.0 - 1.0;
    tangentNormal.xy *= normalStrength;
    tangentNormal = normalize(tangentNormal);
    float3 N = normalize(mul(tangentNormal, TBN));
    float  NoV = max(dot(N, V), 1e-4);

    // 4. F0 — specular reflectance at normal incidence
    // UE5: F0 = lerp(DielectricSpecular, BaseColor, Metallic)  (DielectricSpecular = 0.04)
    // Godot: F0 = mix(vec3(0.16 * specular²), albedo, metallic) — with specular=0.5 → 0.04
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);

    // DiffuseColor: non-metals reflect diffusely, metals do not
    float3 DiffuseColor = baseColor * (1.0 - metallic);

    // Anisotropy: rotate tangent/bitangent and compute per-axis roughness
    // [Burley 2012] anisotropic roughness: ax = alpha*(1+aniso), ay = alpha*(1-aniso)
    float3 anisoT = T;
    float3 anisoB = B;
    float anisoAx = alpha;
    float anisoAy = alpha;
    [branch]
    if (abs(anisotropy) > 0.001) {
        float cosRot = cos(anisotropyRotation);
        float sinRot = sin(anisotropyRotation);
        anisoT = cosRot * T + sinRot * B;
        anisoB = -sinRot * T + cosRot * B;
        anisoAx = max(alpha * (1.0 + anisotropy), 0.001);
        anisoAy = max(alpha * (1.0 - anisotropy), 0.001);
    }

    // ===================================================================
    // 5. Direct Lighting — scene directional sun only (outdoor PBR)
    //    Studio fill/rim lights removed: UE5 and Godot use sky ambient
    //    instead of artificial fill lights for outdoor scenes.
    // ===================================================================
    float3 Lo = float3(0.0, 0.0, 0.0);

    // PCF shadow factor for sun (light-space reconstructed in SampleShadow)
    // Pass surface normal for normal-offset bias (prevents floor self-shadow).
    // NOTE: Screen-space contact shadows are computed in the GTAO pass (has depth buffer access)
    // and folded into the AO term, which modulates the composite pass output.
    float shadowFactor = SampleShadow(input.worldPos, N);

    // Sun / directional light — modulated by PCF shadow, micro-shadow, and specular multiplier
    float specScale = lerp(1.0, specularMultiplier, metallic * 0.5 + 0.5);
    {
        float3 directLight = EvaluateDirectLight(N, V, NoV, DiffuseColor, F0, a2, roughness,
                                                  lightDir, lightColor, lightIntensity,
                                                  anisoT, anisoB, anisoAx, anisoAy);

        // Micro-shadow [Jimenez 2016]: AO modulates direct light for sub-texel self-shadowing
        float NoL_direct = saturate(dot(N, normalize(-lightDir)));
        float microShadow = saturate(NoL_direct + 2.0 * ao * ao - 1.0);

        Lo += directLight * specScale * shadowFactor * microShadow;
    }

    // Punctual lights (point + spot) — inverse-square attenuation with radius falloff
    [loop]
    for (uint li = 0; li < min(punctualLightCount, 8u); ++li) {
        PunctualLight pl = punctualLights[li];
        float3 toLight = pl.position - input.worldPos;
        float dist2 = dot(toLight, toLight);
        float dist  = sqrt(dist2);
        float3 L_pl = toLight / max(dist, 0.001);

        // UE5-style inverse-square attenuation with smooth radius falloff
        // GetLocalLightAttenuation: 1/(1+d²) * saturate(1 - (d/r)^4)^2
        float distRatio = dist / max(pl.radius, 0.001);
        float falloff4  = saturate(1.0 - distRatio * distRatio * distRatio * distRatio);
        float atten = (falloff4 * falloff4) / (dist2 + 1.0);

        // Spot cone attenuation (smooth between inner and outer cone)
        if (pl.spotParams > 0.001) {
            float cosAngle = dot(-L_pl, normalize(pl.direction));
            // spotParams encodes cosOuter + (cosInner - cosOuter) range
            // We use a simpler approach: spotParams = cosOuter, compute smoothstep
            float cosOuter = pl.spotParams;
            float cosInner = cosOuter + 0.1; // ~6 degree inner cone offset
            atten *= saturate((cosAngle - cosOuter) / max(cosInner - cosOuter, 0.001));
        }

        if (atten > 0.0001) {
            // Evaluate BRDF for this punctual light (direction is from surface TO light)
            float3 plDir = -L_pl; // EvaluateDirectLight expects lightDir (from light TO surface)
            float3 plResult = EvaluateDirectLight(N, V, NoV, DiffuseColor, F0, a2, roughness,
                                                   plDir, pl.color, pl.intensity * atten,
                                                   anisoT, anisoB, anisoAx, anisoAy);
            Lo += plResult * specScale;
        }
    }

    // ===================================================================
    // 6. Ambient / IBL — Cubemap irradiance + split-sum specular
    //    Reference: UE5 SkyLightCommon.ush, Karis 2013 "Real Shading in UE4"
    //    Diffuse: sample irradiance cubemap with surface normal
    //    Specular: sample prefiltered env with reflection + BRDF LUT
    // ===================================================================
    float3 ambient;
    {
        // Diffuse IBL: irradiance cubemap pre-convolved with cosine lobe
        float3 irradiance = irradianceMap.Sample(clampSampler, N).rgb;

        float3 kS_env = F_SchlickRoughness(F0, NoV, roughness);
        float3 kD_env = (1.0 - kS_env) * (1.0 - metallic);
        float3 diffuseAmbient = kD_env * irradiance * baseColor;

        // Specular IBL: prefiltered env cubemap + BRDF LUT (split-sum)
        // Mip level = roughness * (maxMipLevels - 1); prefiltered has 5 mips (0..4)
        float3 R = reflect(-V, N);
        float  mipLevel = roughness * 4.0;
        float3 prefilteredColor = prefilteredMap.SampleLevel(clampSampler, R, mipLevel).rgb;

        float2 envBRDF = brdfLUT.Sample(clampSampler, float2(NoV, roughness)).rg;
        float3 specularAmbient = prefilteredColor * (F0 * envBRDF.x + envBRDF.y);

        // Horizon occlusion: fade specular IBL when reflection points below surface.
        // [Marmoset 2016] — prevents bright specular leaking from below the hemisphere.
        float horizon = saturate(1.0 + dot(R, N));
        specularAmbient *= horizon * horizon;

        // Multi-scattering energy compensation [Kulla & Conty 2017, Fdez-Aguera 2019].
        // The single-scattering split-sum loses energy at high roughness. Recover it:
        // Ems = 1 - (scale + bias) from BRDF LUT, FmsEms approximates the missing bounces.
        // Scaled by 0.5 to avoid over-brightening with a simple procedural sky (no HDR HDRI).
        float3 FssEss = F0 * envBRDF.x + envBRDF.y;
        float  Ess    = envBRDF.x + envBRDF.y;
        float  Ems    = 1.0 - Ess;
        float3 Favg   = F0 + (1.0 - F0) / 21.0;  // average Fresnel
        float3 FmsEms = Ems * FssEss * Favg / (1.0 - Favg * Ems);
        float3 multiScatter = FmsEms * irradiance * 0.5;
        specularAmbient += multiScatter;

        // Specular occlusion [Lagarde 2014]: dims IBL specular at grazing angles
        // and in occluded crevices. Prevents the bright white fresnel ring around
        // smooth spheres that the basic kS_env * prefilteredColor path produces.
        float specOcc = SpecularOcclusion(NoV, ao, roughness);
        specularAmbient *= specOcc;

        // AO for ambient: linear for diffuse (specular already has specOcc applied).
        float3 ambientOccluded = diffuseAmbient * ao + specularAmbient;
        ambient = ambientOccluded * envIntensity * envMultiplier;
    }

    // 7. Final assembly.
    //    AO is applied ONLY to indirect/ambient (step 6) — this matches UE5 ShadingModels.ush:
    //    GBufferAO modulates SkyLightDiffuse, never AccumulateDynamicLighting (direct light).
    //    Direct light (Lo) is modulated only by shadow maps (already in shadowFactor).
    //
    //    Heightmap self-shadow: traces light direction on displacement map to create
    //    contact shadows in ripple valleys, mortar joints, fabric weave — detail too
    //    fine for the shadow map. Subsumes the old simple cavity term.
    //    Cavity (height-based darkening) is blended in for ambient micro-contact shadow.
    //    GUARD: without displacement (parallaxScale=0), both are 1.0 (no-op).
    float height = displacementTex.Sample(linearSampler, uv).r;
    float selfShadowRaw = HeightmapSelfShadow(uv, L_ts, pomScale, height);
    // Auto-scale: parallaxScale drives intensity (0.03→0.6, 0.08→1.0).
    // Small displacement = subtle shadow, large displacement = strong shadow.
    // selfShadowStrength is the per-material override (default 1.0).
    float ssAuto = saturate(parallaxScale * 20.0) * selfShadowStrength;
    float selfShadow = lerp(1.0, selfShadowRaw, ssAuto);
    // cavityStrength: per-material control of ambient cavity darkening (0=off, 0.35=default)
    float cavity = (parallaxScale > 0.0) ? lerp(1.0, height, cavityStrength) : 1.0;
    // Self-shadow: full on direct, subtle 20% on ambient (avoids over-darkening valleys).
    float3 color = ambient * cavity * lerp(1.0, selfShadow, 0.2) + Lo * selfShadow;

    // 7b. Emission — additive HDR, unaffected by AO or shadows (self-illuminating).
    color += emissionColor * emissionEnergy;

    // Output linear HDR — tonemap + gamma are applied in the composite post-process pass.
    return float4(max(color, 0.0), 1.0);
}