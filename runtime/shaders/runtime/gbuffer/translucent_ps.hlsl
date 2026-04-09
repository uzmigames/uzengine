/// UzEngine forward translucent pixel shader.
///
/// @ref Schlick 1994 "An Inexpensive BRDF Model for Physically-Based Rendering"
/// @ref Walter 2007 "Microfacet Models for Refraction" — Snell's law
/// @ref Three.js MeshPhysicalMaterial.transmission — forward pass glass rendering
///
/// Uses the same CB layout as gbuffer_ps.hlsl (PerObjectCB) and the full
/// PerFrameCB layout (matching per_frame_params.h) for IBL access.
/// Outputs to a single HDR render target (RGBA16F) with alpha blending.
///
/// For glass materials (smID==9, ThinTranslucent): Fresnel-weighted IBL
/// cubemap reflection at rim + transparency at center. Alpha = Fresnel,
/// so SrcAlpha blend makes center transparent, rim reflective.

#include "../common/encoding.hlsli"

struct PSInput {
    float4 positionCS : SV_Position;
    float3 normalWS   : NORMAL;
    float2 texcoord   : TEXCOORD0;
    float4 tangentWS  : TANGENT;
    float3 positionWS : TEXCOORD1;
};

// ── PerFrameCB — MUST match per_frame_params.h layout exactly ────────────
// Expanded to include IBL SRV indices at c13-c14.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj  : packoffset(c0);   // bytes 0-63
    float4   cameraPos           : packoffset(c4);   // bytes 64-79
    float4   timeParams          : packoffset(c5);   // bytes 80-95
    float4   lightDir            : packoffset(c6);   // bytes 96-111
    float4   lightColor          : packoffset(c7);   // bytes 112-127
    float4   ambientColor        : packoffset(c8);   // bytes 128-143
    row_major float4x4 invViewProj : packoffset(c9); // bytes 144-207
    float    nearPlane           : packoffset(c13.x); // byte 208
    float    farPlane            : packoffset(c13.y); // byte 212
    uint     irradianceSrvIndex  : packoffset(c13.z); // byte 216
    uint     prefilteredSrvIndex : packoffset(c13.w); // byte 220
    uint     brdfLutSrvIndex     : packoffset(c14.x); // byte 224
    uint     prefilteredMipLevels: packoffset(c14.y); // byte 228
    float    skyLightIntensity   : packoffset(c14.z); // byte 232
    float    skyLightColorR      : packoffset(c14.w); // byte 236
    float    skyLightColorG      : packoffset(c15.x); // byte 240
    float    skyLightColorB      : packoffset(c15.y); // byte 244
};

// ── PerObjectCB — matches gbuffer_ps.hlsl and constant_buffers.h ─────────
cbuffer PerObjectCB : register(b2) {
    uint     albedoIndex;
    uint     normalIndex;
    uint     roughnessIndex;
    uint     metallicIndex;
    uint     aoIndex;
    uint     emissiveIndex;
    float    alphaClipThreshold;
    uint     opacitySrvIndex;
    row_major float4x4 obj_model;
    row_major float4x4 obj_normalMatrix;
    float3   emissiveColor;
    float    emissiveIntensity;
    float3   baseColorTint;
    float    roughnessMultiplier;
    float    metallicMultiplier;
    float    normalIntensity;
    float2   uvScale;
    float2   uvOffset;
    float    aoIntensity;
    float    matOpacity;
    float    displacementScale;
    uint     heightmapSrvIndex;
    float    pomMinLayers;
    float    pomMaxLayers;
    float    pomFadeStart;
    float    pomFadeEnd;
    float    pomSelfShadow;
    uint     materialFlags;
};

SamplerState linearSampler : register(s0);

/// @ref Bruneton 2017 — cubemap direction conversion (engine Z-up to cubemap Y-up)
float3 WorldToCubemap(float3 dir) { return float3(dir.x, dir.z, dir.y); }

float4 main(PSInput input) : SV_Target {
    float3 N = normalize(input.normalWS);
    float3 V = normalize(cameraPos.xyz - input.positionWS);
    float  NoV = max(dot(N, V), 1e-4);

    // Extract shading model from materialFlags.
    uint smID = (materialFlags >> 24u) & 0xFu;

    // Extract specular from materialFlags bits[8:15].
    float specularVal = float((materialFlags >> 8u) & 0xFFu) / 255.0;

    // ── Glass / ThinTranslucent (smID == 9) ─────────────────────────────
    // @ref Shadertoy XdVfDd — analytical sphere ray tracing with refraction.
    // @ref Walter 2007 "Microfacet Models for Refraction" — Snell's law
    // @ref Falcor Fresnel.slang — dielectric Fresnel equations
    //
    // Traces the view ray through a glass sphere analytically:
    //   1. Refract at entry (air → glass)
    //   2. Trace inside sphere to back face
    //   3. Refract at exit (glass → air), handling total internal reflection
    //   4. Sample IBL cubemap in the exit ray direction
    //   5. Add surface reflection weighted by Fresnel
    if (smID == 9) {
        // @ref Three.js — specular=0.5 → F0=0.04 → IOR=1.5
        // @constants F0_dielectric = specular * 0.08, IOR = (1+√F0)/(1-√F0)
        float f0_scalar = max(specularVal * 0.08, 0.001);
        float sqrtF0 = sqrt(f0_scalar);
        float ior = (1.0 + sqrtF0) / max(1.0 - sqrtF0, 0.001);
        ior = clamp(ior, 1.0, 2.5);

        // ── Surface Fresnel (entry: air → glass) ────────────────────────
        // @ref Schlick 1994 — Fresnel approximation for dielectric interface
        // @constants F0_glass = ((ior-1)/(ior+1))^2 = 0.04 for IOR=1.5
        float F0_glass = pow((ior - 1.0) / (ior + 1.0), 2.0);
        float fresnel = F0_glass + (1.0 - F0_glass) * pow(1.0 - NoV, 5.0);

        // ── Surface reflection (weighted by Fresnel) ────────────────────
        // @ref Shadertoy XdVfDd — surface_color = sky_sample(reflect) * (1 - |dot(ray,N)|)
        float3 R = reflect(-V, N);
        float3 surfaceReflection = float3(0, 0, 0);
#ifdef UZ_BINDLESS
        if (prefilteredSrvIndex != kSentinelSrvIndex) {
            TextureCube<float4> prefTex = ResourceDescriptorHeap[prefilteredSrvIndex];
            float3 skyTint = float3(skyLightColorR, skyLightColorG, skyLightColorB);
            surfaceReflection = prefTex.SampleLevel(linearSampler, WorldToCubemap(R), 0.0).rgb
                              * skyLightIntensity * skyTint;
        }
#endif
        surfaceReflection *= fresnel * 0.5;

        // ── Refraction: trace ray through the glass sphere ──────────────
        // @ref Shadertoy XdVfDd — analytical sphere ray tracing with refract()
        // @ref Walter 2007 — Snell's law: refract at entry, trace to back face,
        //   refract at exit. Handle total internal reflection via reflect+loop.
        //
        // Step 1: Refract at entry (air → glass, eta = 1/IOR)
        float3 entryRay = refract(-V, N, 1.0 / ior);
        float3 refractionColor = float3(0, 0, 0);

        if (dot(entryRay, entryRay) > 0.001) {
            // Step 2: Analytically find where the refracted ray exits the sphere.
            // For a unit sphere centered at C with radius r, the ray enters at P
            // (the current pixel worldPos). The exit point is where the ray from P
            // in direction entryRay hits the back face of the sphere.
            //
            // Sphere center approximation: obj_model[3].xyz (translation column).
            // Radius: approximated from scale (obj_model[0] length).
            float3 sphereCenter = float3(obj_model[3][0], obj_model[3][1], obj_model[3][2]);
            float sphereRadius = length(float3(obj_model[0][0], obj_model[0][1], obj_model[0][2]));

            // Ray-sphere intersection from inside (always hits back face).
            float3 oc = input.positionWS - sphereCenter;
            float b = dot(oc, entryRay);
            float c = dot(oc, oc) - sphereRadius * sphereRadius;
            float disc = b * b - c;
            float exitDist = -b + sqrt(max(disc, 0.0));

            float3 exitPoint = input.positionWS + entryRay * exitDist;
            float3 exitNormal = normalize(exitPoint - sphereCenter);

            // Step 3: Refract at exit (glass → air, eta = IOR)
            // The normal points outward; for inside→outside, use -exitNormal.
            float3 exitRay = refract(entryRay, -exitNormal, ior);

            // Total internal reflection check
            if (dot(exitRay, exitRay) < 0.001) {
                // @ref Shadertoy XdVfDd — TIR fallback: reflect inside and try again
                exitRay = reflect(entryRay, -exitNormal);
            }

            // Step 4: Sample IBL cubemap in exit ray direction.
            // Use a mip level > 0 to simulate translucent/frosted glass.
            // @ref Three.js applyIorToRoughness — blur based on material roughness.
            // For translucent glass, sample higher mips for internal scattering.
#ifdef UZ_BINDLESS
            if (prefilteredSrvIndex != kSentinelSrvIndex) {
                TextureCube<float4> prefTex = ResourceDescriptorHeap[prefilteredSrvIndex];
                float3 skyTint = float3(skyLightColorR, skyLightColorG, skyLightColorB);

                // Translucent blur: sample at higher mip for internal light scattering.
                // @constants mipLevels-1 = max mip, translucentBlur = 2.0 base + roughness
                float translucentMip = 1.0 + roughnessMultiplier * float(prefilteredMipLevels - 1);
                refractionColor = prefTex.SampleLevel(linearSampler, WorldToCubemap(exitRay), translucentMip).rgb
                                * skyLightIntensity * skyTint;
            }
#endif
        }

        // @ref Shadertoy XdVfDd — color = refraction + surface_reflection
        // Refraction is dominant (1-F ≈ 0.96 at center), reflection adds Fresnel rim.
        float3 color = refractionColor + surfaceReflection;
        return float4(color, 1.0);
    }

    // ── Standard translucent (non-glass) ────────────────────────────────
    // Forward lighting for semi-transparent materials.
    float3 albedo = baseColorTint;
#ifdef UZ_BINDLESS
    if (albedoIndex > 0) {
        Texture2D<float4> tex = ResourceDescriptorHeap[albedoIndex];
        albedo *= tex.Sample(linearSampler, input.texcoord).rgb;
    }
#endif

    float3 L = normalize(lightDir.xyz);
    float  NdotL = max(dot(N, L), 0.0);

    // IBL diffuse for standard translucent.
    float3 iblDiffuse = float3(0, 0, 0);
#ifdef UZ_BINDLESS
    if (irradianceSrvIndex != kSentinelSrvIndex) {
        TextureCube<float4> irrTex = ResourceDescriptorHeap[irradianceSrvIndex];
        float3 skyTint = float3(skyLightColorR, skyLightColorG, skyLightColorB);
        iblDiffuse = irrTex.SampleLevel(linearSampler, WorldToCubemap(N), 0.0).rgb
                   * skyLightIntensity * skyTint;
    }
#endif

    float3 color = albedo * (lightColor.xyz * NdotL + iblDiffuse + ambientColor.xyz);
    // Premultiplied alpha — color already weighted by opacity for (One, InvSrcAlpha) blend.
    return float4(color * matOpacity, matOpacity);
}
