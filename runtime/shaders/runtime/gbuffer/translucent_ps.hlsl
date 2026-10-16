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
/// ThinTranslucent (smID 9) defaults to legacy sphere/IBL glass.
/// r.Glass.ThinTransmission=1 opts into the thin-slab contract below.
/// Negative glassThickness selects legacy glass; nonnegative selects thin glass.
/// Thin glass: any mesh, front faces only, treated as a thin slab.
/// Authored local thickness is scaled per model axis along the refracted ray.
/// Transmission samples the opaque scene copy; Fresnel reflection uses the IBL.
/// Unsupported: translucent-through-translucent refraction, true volumes, image
/// inversion/caustics, transmission roughness blur, thickness maps, glass shadows,
/// and back faces. Premultiplied (One, InvSrcAlpha) blend; transmission has 16 levels.

#include "../common/encoding.hlsli"
#include "../common/region_dump.hlsli"

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
    // Region dump (diagnostics) — the same PerFrameCB offsets the deferred lighting pass reads.
    uint     _debugRegionUav     : packoffset(c62.x); // byte 992
    uint     _debugRegionSrv     : packoffset(c62.y); // byte 996
    uint     _debugRegionCount   : packoffset(c62.z); // byte 1000
    uint     _debugRegionStamp   : packoffset(c62.w); // byte 1004
    uint     _debugRegionFields  : packoffset(c63.x); // byte 1008
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
    // PerObjectCB layout unchanged: field reuse for smID 9 (no POM here).
    float    glassThickness;
    uint     sceneColorSrvIndex;
    float    pomMinLayers;
    float    pomMaxLayers;
    float    pomFadeStart;
    float    pomFadeEnd;
    float    pomSelfShadow;
    uint     materialFlags;
};

SamplerState linearSampler : register(s0);
SamplerState clampSampler : register(s2);

/// Region dump (diagnostics): what this pass consumed and produced at a pixel, into LIGHTING_EXTRA
/// records +5..+10. The deferred pass cleared them earlier this frame, so a pixel this pass does not
/// draw keeps opacity -1 and zero layers. +5..+9 hold the last layer drawn; +10 counts the layers,
/// so overlap is recorded instead of assumed away.
void WriteRegionDumpTranslucent(uint2 rp, float fragDepth, float3 sourceColor, float opacity, float3 albedo,
                                uint albedoIdx, float3 iblDiffuse, uint irradianceIdx, float3 N,
                                uint shadingModel, float3 emission, float NdotL) {
#ifdef UZ_BINDLESS
    if (_debugRegionUav == 0xffffffffu || _debugRegionCount == 0u
        || (_debugRegionFields & UZ_REGION_FIELD_LIGHTING_EXTRA) == 0u) return;
    StructuredBuffer<uint4>    rects = ResourceDescriptorHeap[_debugRegionSrv];
    RWStructuredBuffer<float4> dump  = ResourceDescriptorHeap[_debugRegionUav];
    for (uint r = 0u; r < _debugRegionCount; ++r) {
        const uint4 a = rects[r * 2u];
        const uint4 b = rects[r * 2u + 1u];
        if (!UzRegionContains(a, rp)) continue;
        const uint lrec = UzRegionPixelBase(a, b, rp) + UzRegionLightingExtraOffset(_debugRegionFields);
        dump[lrec + 5] = float4(sourceColor, opacity);
        dump[lrec + 6] = float4(albedo, asfloat(albedoIdx));
        dump[lrec + 7] = float4(iblDiffuse, asfloat(irradianceIdx));
        dump[lrec + 8] = float4(N, asfloat(shadingModel));
        dump[lrec + 9] = float4(emission, NdotL);
        // Read-modify-write: draw calls run in order, but invocations inside one draw call may run
        // concurrently, so overlap between primitives of the same draw can be undercounted. It can
        // never be overcounted.
        // y: this fragment's depth (SV_Position.z), so the last layer can be matched to a surface.
        dump[lrec + 10] = float4(dump[lrec + 10].x + 1.0, fragDepth, dump[lrec + 10].z, 0.0);
    }
#endif
}

/// @ref Bruneton 2017 — cubemap direction conversion (engine Z-up to cubemap Y-up)
float3 WorldToCubemap(float3 dir) { return float3(dir.x, dir.z, dir.y); }

// [earlydepthstencil]: the region-dump records are a UAV write, and a pixel shader with UAV writes
// otherwise loses the implicit early depth test, so it ran and recorded glass fragments that the
// depth test then rejected (ledger H-44). This shader has no discard, clip or depth output, so forcing
// the early test restores exactly the visibility it had before it was instrumented.
[earlydepthstencil]
float4 main(PSInput input) : SV_Target {
    float3 N = normalize(input.normalWS);
    float3 V = normalize(cameraPos.xyz - input.positionWS);
    float  NoV = max(dot(N, V), 1e-4);

    // Extract shading model from materialFlags.
    uint smID = (materialFlags >> 24u) & 0xFu;

    // Extract specular from materialFlags bits[8:15].
    float specularVal = float((materialFlags >> 8u) & 0xFFu) / 255.0;

    // Emission. The forward translucent path ignored its emission inputs
    // entirely, so an authored glow on a translucent material (Sponza's window
    // glass carries a WindowSunGlow constant) never reached the image.
    float3 emission = emissiveColor * emissiveIntensity;
#ifdef UZ_BINDLESS
    if (emissiveIndex > 0 && emissiveIndex != kSentinelSrvIndex) {
        Texture2D<float4> emissiveTexture = ResourceDescriptorHeap[emissiveIndex];
        emission *= emissiveTexture.Sample(linearSampler, input.texcoord * uvScale + uvOffset).rgb;
    }
#endif

    // Legacy sphere/IBL glass, preserved from before F-007 for shipped content.
    // Only the branch condition differs; scene_sync also preserves old specular.
    if (smID == 9 && glassThickness < 0.0) {
        float f0_scalar = max(specularVal * 0.08, 0.001);
        float sqrtF0 = sqrt(f0_scalar);
        float ior = (1.0 + sqrtF0) / max(1.0 - sqrtF0, 0.001);
        ior = clamp(ior, 1.0, 2.5);
        float F0_glass = pow((ior - 1.0) / (ior + 1.0), 2.0);
        float fresnel = F0_glass + (1.0 - F0_glass) * pow(1.0 - NoV, 5.0);
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
        float3 entryRay = refract(-V, N, 1.0 / ior);
        float3 refractionColor = float3(0, 0, 0);
        if (dot(entryRay, entryRay) > 0.001) {
            float3 sphereCenter = float3(obj_model[3][0], obj_model[3][1], obj_model[3][2]);
            float sphereRadius = length(float3(obj_model[0][0], obj_model[0][1], obj_model[0][2]));
            float3 oc = input.positionWS - sphereCenter;
            float b = dot(oc, entryRay);
            float c = dot(oc, oc) - sphereRadius * sphereRadius;
            float disc = b * b - c;
            float exitDist = -b + sqrt(max(disc, 0.0));
            float3 exitPoint = input.positionWS + entryRay * exitDist;
            float3 exitNormal = normalize(exitPoint - sphereCenter);
            float3 exitRay = refract(entryRay, -exitNormal, ior);
            if (dot(exitRay, exitRay) < 0.001) {
                exitRay = reflect(entryRay, -exitNormal);
            }
#ifdef UZ_BINDLESS
            if (prefilteredSrvIndex != kSentinelSrvIndex) {
                TextureCube<float4> prefTex = ResourceDescriptorHeap[prefilteredSrvIndex];
                float3 skyTint = float3(skyLightColorR, skyLightColorG, skyLightColorB);
                float translucentMip = 1.0 + roughnessMultiplier * float((prefilteredMipLevels & 0xFFu) - 1u);
                refractionColor = prefTex.SampleLevel(linearSampler, WorldToCubemap(exitRay), translucentMip).rgb
                                * skyLightIntensity * skyTint;
            }
#endif
        }
        float3 color = refractionColor + surfaceReflection + emission;
        WriteRegionDumpTranslucent(uint2(input.positionCS.xy), input.positionCS.z, color, 1.0, float3(0, 0, 0), 0u,
                                   refractionColor, prefilteredSrvIndex, N, smID, emission,
                                   max(dot(N, normalize(lightDir.xyz)), 0.0));
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

    float3 color = albedo * (lightColor.xyz * NdotL + iblDiffuse + ambientColor.xyz) + emission;
    // Thin-surface transmission (Three.js getVolumeTransmissionRay). The standard
    // path above is unchanged, and transmission zero bypasses all glass arithmetic.
    float transmission = float((materialFlags >> 28u) & 0xFu) / 15.0;
    if (smID == 9 && transmission > 0.0) {
        float f0 = specularVal * 0.08;
        float sqrtF0 = sqrt(f0);
        float ior = (1.0 + sqrtF0) / max(1.0 - sqrtF0, 0.001);
        float fresnel = f0 + (1.0 - f0) * pow(1.0 - NoV, 5.0);
        float3 modelScale = float3(length(obj_model[0].xyz), length(obj_model[1].xyz), length(obj_model[2].xyz));
        float3 refr = refract(-V, N, 1.0 / ior);
        // TIR produces a zero refracted vector: keep the sample at this pixel.
        float refrLengthSq = dot(refr, refr);
        float3 offset = refrLengthSq > 0.0 ? refr * rsqrt(refrLengthSq) * glassThickness * modelScale : float3(0, 0, 0);
        float4 exitClip = mul(float4(input.positionWS + offset, 1.0), viewProj);
        float2 exitUV = saturate(exitClip.xy / max(exitClip.w, 1e-4) * float2(0.5, -0.5) + 0.5);
        float3 transmitted = float3(0, 0, 0);
        float3 reflection = float3(0, 0, 0);
#ifdef UZ_BINDLESS
        Texture2D<float4> sceneColor = ResourceDescriptorHeap[sceneColorSrvIndex];
        transmitted = sceneColor.SampleLevel(clampSampler, exitUV, 0).rgb * baseColorTint;
        if (prefilteredSrvIndex != kSentinelSrvIndex) {
            TextureCube<float4> prefTex = ResourceDescriptorHeap[prefilteredSrvIndex];
            float3 skyTint = float3(skyLightColorR, skyLightColorG, skyLightColorB);
            reflection = prefTex.SampleLevel(linearSampler, WorldToCubemap(reflect(-V, N)), 0.0).rgb
                       * skyLightIntensity * skyTint;
        }
#endif
        float3 glass = (1.0 - fresnel) * transmitted + fresnel * reflection + emission;
        float3 result = lerp(color * matOpacity, glass, transmission);
        float alpha = lerp(matOpacity, 1.0, transmission);
        WriteRegionDumpTranslucent(uint2(input.positionCS.xy), input.positionCS.z, result, alpha, albedo, albedoIndex,
                                   transmitted, sceneColorSrvIndex, N, smID, emission, NdotL);
        return float4(result, alpha);
    }

    WriteRegionDumpTranslucent(uint2(input.positionCS.xy), input.positionCS.z, color, matOpacity, albedo, albedoIndex,
                               iblDiffuse, irradianceSrvIndex, N, smID, emission, NdotL);
    // Premultiplied alpha — color already weighted by opacity for (One, InvSrcAlpha) blend.
    return float4(color * matOpacity, matOpacity);
}
