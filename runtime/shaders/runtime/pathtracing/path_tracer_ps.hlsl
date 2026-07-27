/// UzEngine Path Tracer — Step 7: Direct-sun shadow via CSM shadow atlas.
///
/// Shadows are sampled from the same Cascaded Shadow Map atlas used by the
/// deferred lighting pipeline. This gives view-independent, stable sun shadows
/// matching the rasterized path exactly.
///
/// @ref Engel 2006 "Cascaded Shadow Maps" (ShaderX5)
///
/// Specular reflection uses a roughness-perturbed mirror model: mirror direction
/// lerped toward a random hemisphere direction by roughness², then sampled from
/// the sky cubemap with LOD = sqrt(roughness)*5. Simple, stable, no screen-space
/// ray marching artifacts.
///
/// Per pixel:
///   1. Read GBuffer: normal, albedo, metallic, roughness, position.
///   2. Compute F0 from albedo+metallic.
///   3. Direct sun: Lambert diffuse · (1 - F) + GGX specular evaluated at the
///      sun direction.
///   4. Indirect environment: N samples per frame.
///      - Half the samples take a cosine-weighted hemisphere ray (diffuse).
///      - Half take a VNDF-sampled microfacet reflection (specular).
///      Each lobe weighted by Fresnel.
///   5. Sum, firefly clamp, output.
///
/// @ref Kajiya 1986 "The Rendering Equation"
/// @ref Walter 2007 "Microfacet Models for Refraction through Rough Surfaces"
/// @ref Heitz 2014 "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
/// @ref Karis 2013 "Real Shading in Unreal Engine 4" — split-sum + F_Schlick
/// @ref Dupuy 2023 "Sampling Visible GGX Normals with Spherical Caps"
/// @ref Wang 1997 "Integer Hash Function" — RNG
/// @ref Shirley 1992 "Nonuniform Random Point Sets via Warping" — cosine hemisphere

#include "../common/encoding.hlsli"
#include "../common/constants.hlsli"
#include "../common/math.hlsli"
#include "../common/sampling.hlsli"
#include "../material/brdf.hlsli"

#ifdef UZ_BINDLESS
cbuffer PathTracerRC : register(b0) {
    uint g_normalSrv;
    uint g_depthSrv;
    uint g_albedoSrv;
    uint g_positionSrv;
    uint g_emissiveSrv;   // GBuffer emissive HDR (RT3)
    uint g_skyCubeSrv;
    uint g_accumPrevSrv;  // Step 6: history read for progressive accumulation
    uint g_sampleCount;   // Step 6: number of samples already accumulated
    uint g_frameIndex;
    uint g_tlasSrv;       // Step 8: scene TLAS for real multi-bounce transport.
};
// b2: TraceConstants (GDF clipmaps + material table + sun/sky) shared with
// the DDGI atlas writer — provides hit normals + hit albedo for bounces.
#include "../gi/gi_trace_common.hlsli"
#else
cbuffer PathTracerRC : register(b0) {
    uint g_sampleCount;
    uint g_frameIndex;
};
Texture2D<float4>  t_normal   : register(t0);
Texture2D<float>   t_depth    : register(t1);
Texture2D<float4>  t_albedo   : register(t2);
Texture2D<float4>  t_position : register(t3);
TextureCube<float4> t_skyCube : register(t4);
Texture2D<float4>  t_emissive : register(t6); // t5 = shadowAtlasTexture (non-bindless)
#endif

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;     // c0-c3
    float4 cameraPos;                // c4
    float4 timeParams;               // c5
    float4 lightDir;                 // c6
    float4 lightColor;               // c7
    float4 ambientColor;             // c8
};

SamplerState g_linearClamp : register(s0);

#ifndef UZ_BINDLESS
/// SM5 path: shadow atlas at t5 (t0-t4 used by GBuffer + sky cube).
/// shadow.hlsli's CascadeShadow() macro-aliases this as `shadowAtlas`.
Texture2D<float> shadowAtlasTexture : register(t5);
#endif

// shadowPCFPenumbra is read from the full PerFrameCB in deferred_lighting_ps.
// The path tracer uses a trimmed CB — supply the default here so shadow.hlsli
// compiles without layout changes.
static const float shadowPCFPenumbra = 2.0;

// Must be included AFTER the PerFrameCB (or the shadowPCFPenumbra define above)
// because shadow.hlsli references that name for the Poisson penumbra radius.
#include "../lighting/shadow.hlsli"

#define PT_SAMPLES_PER_FRAME 4

// ─── RNG (Wang hash). @ref Wang 1997 ────────────────────────────────────────
uint WangHash(inout uint seed) {
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}
float Rand01(inout uint state) { return float(WangHash(state)) / 4294967296.0; }
float2 Rand2(inout uint state) { return float2(Rand01(state), Rand01(state)); }

float3 RandUnitVector(inout uint state) {
    float z = Rand01(state) * 2.0 - 1.0;
    float a = Rand01(state) * 6.28318530718;
    float r = sqrt(max(0.0, 1.0 - z * z));
    return float3(r * cos(a), r * sin(a), z);
}

// ─── Tangent ↔ world transforms ─────────────────────────────────────────────
float3 TangentToWorld(float3 v, float3 N, float3 T, float3 B) {
    return v.x * T + v.y * B + v.z * N;
}
float3 WorldToTangent(float3 v, float3 N, float3 T, float3 B) {
    return float3(dot(v, T), dot(v, B), dot(v, N));
}



struct PSOutput {
    float4 accum : SV_Target0;  ///< RGBA32F progressive accumulation (history).
    float4 hdr   : SV_Target1;  ///< RGBA16F HDR scene color consumed by post-process.
};

PSOutput main(float4 pos : SV_Position)
{
    int2 px = int2(pos.xy);

#ifdef UZ_BINDLESS
    Texture2D<float4>  normalTex   = ResourceDescriptorHeap[g_normalSrv];
    Texture2D<float>   depthTex    = ResourceDescriptorHeap[g_depthSrv];
    Texture2D<float4>  albedoTex   = ResourceDescriptorHeap[g_albedoSrv];
    Texture2D<float4>  positionTex = ResourceDescriptorHeap[g_positionSrv];
    Texture2D<float4>  emissiveTex = ResourceDescriptorHeap[g_emissiveSrv];
    TextureCube<float4> skyCube    = ResourceDescriptorHeap[g_skyCubeSrv];
#else
    #define normalTex   t_normal
    #define depthTex    t_depth
    #define albedoTex   t_albedo
    #define positionTex t_position
    #define emissiveTex t_emissive
    #define skyCube     t_skyCube
#endif

    float d = depthTex.Load(int3(px, 0));
    if (d <= 0.0) {
        // Sky pixel — write zero to both RTs. The sky atmosphere pass that
        // runs after the path tracer composites the actual sky into hdr.
        PSOutput skyOut;
        skyOut.accum = float4(0.0, 0.0, 0.0, 1.0);
        skyOut.hdr   = float4(0.0, 0.0, 0.0, 1.0);
        return skyOut;
    }

    float4 normalData = normalTex.Load(int3(px, 0));
    float4 albedoData = albedoTex.Load(int3(px, 0));
    float4 posData    = positionTex.Load(int3(px, 0));

    float3 N         = OctDecode(normalData.xy);
    float3 albedo    = albedoData.rgb;
    float  metallic  = normalData.b;
    float  roughness = max(0.045, normalData.a); // floor matches deferred lighting
    float3 P         = posData.xyz;

    // GGX needs alpha = roughness^2 (Disney/UE convention).
    // @ref Burley 2012 "Physically-Based Shading at Disney"
    float alpha = RoughnessToAlpha(roughness);

    // F0: 0.04 baseline for dielectrics, albedo for metals.
    // @ref Karis 2013 "Real Shading in Unreal Engine 4"
    float3 F0 = ComputeF0(albedo, metallic);

    // View direction (from surface to camera).
    float3 V = normalize(cameraPos.xyz - P);
    float  NdotV = max(1e-4, dot(N, V));

    // Tangent basis for microfacet sampling. @ref math.hlsli
    float3 T, B;
    BuildOrthonormalBasis(N, T, B);

    // RNG seed. @ref Wang 1997
    uint rngState = uint(px.x) * 1973u + uint(px.y) * 9277u + g_frameIndex * 26699u;
    rngState |= 1u;

    // ── Direct sun + CSM shadow (Step 7) ────────────────────────────────────
    // Shadow sampled from the same CSM atlas used by the deferred pipeline —
    // view-independent, stable, matches rasterized shadows exactly.
    // @ref Karis 2013, @ref Heitz 2014, @ref Engel 2006
    float3 directRadiance = float3(0, 0, 0);
    {
        float3 L     = normalize(lightDir.xyz);
        float  NdotL = max(0.0, dot(N, L));
        if (NdotL > 0.0) {
            float3 H     = normalize(V + L);
            float  NdotH = max(0.0, dot(N, H));
            float  VdotH = max(0.0, dot(V, H));

            float  D  = D_GGX(alpha, NdotH);
            float  G  = G_SmithGGXCorrelated(alpha, NdotV, NdotL);
            float3 F  = F_Schlick(F0, VdotH);

            float3 specular = (D * G) * F / max(1e-4, 4.0 * NdotV * NdotL);
            float3 diffuse  = (1.0 - F) * (1.0 - metallic) * albedo / PI;

            // Camera-to-surface distance used as view-depth proxy for cascade
            // selection. Cascade splits are in world-space units (cm for Sponza)
            // so the scale matches.
            float  viewDepth  = length(P - cameraPos.xyz);
            float  sunShadow  = CascadeShadow(P, viewDepth, N, NdotL);

            directRadiance = (diffuse + specular) * lightColor.rgb * NdotL * sunShadow;
        }
    }

    // ── Indirect transport (split lobes) ────────────────────────────────────
    // Half the samples: cosine-weighted diffuse hemisphere PATHS — real
    // multi-bounce transport against the scene TLAS (sun next-event
    // estimation at every hit, sky radiance only on miss). Half the samples:
    // roughness-perturbed mirror specular (one traced bounce).
    //
    // Cosine sampling: pdf = cosT/PI cancels the Lambert cosT and the BRDF's
    // 1/PI, so the per-bounce throughput multiplier is exactly the hit
    // albedo. @ref Kajiya 1986; @ref Shirley 1992
    //
    // Without a TLAS (r.RayTracing off / non-DXR device), falls back to the
    // occlusion-free sky sample — NOT a reference in that mode.
    float3 indirect = float3(0, 0, 0);

#ifdef UZ_BINDLESS
    const bool ptHasTlas = (g_tlasSrv != kGITraceSentinelSrv);
#else
    const bool ptHasTlas = false;
#endif

    // Diffuse half — path trace with sun NEE at each vertex.
    const int  kPTBounces = 4;
    [loop]
    for (int kd = 0; kd < (PT_SAMPLES_PER_FRAME / 2); ++kd) {
#ifdef UZ_BINDLESS
        if (ptHasTlas) {
            RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[g_tlasSrv];
            float3 throughput = (1.0 - F0) * (1.0 - metallic) * albedo;
            float3 rayO   = P + N * 2.0;
            float3 rayDir = normalize(N + RandUnitVector(rngState));
            float3 sunL   = normalize(lightDir.xyz);
            float3 sunCI  = float3(lightColorR, lightColorG, lightColorB)
                          * lightIntensity;

            [loop]
            for (int b = 0; b < kPTBounces; ++b) {
                RayDesc ray;
                ray.Origin    = rayO;
                ray.Direction = rayDir;
                ray.TMin      = 1.0;
                ray.TMax      = 1e7;
                RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
                q.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
                q.Proceed();

                if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
                    indirect += throughput * GITrace_SkyRadiance(rayDir);
                    break;
                }

                float3 hitPos = rayO + rayDir * q.CommittedRayT();
                uint   meshId = q.CommittedInstanceID();
                float3 nHit   = GITrace_GdfNormal(hitPos, rayDir);
                float3 aHit   = GITrace_HitAlbedo(meshId);

                // Sun next-event estimation at the hit (shadow ray).
                float ndl = max(dot(nHit, sunL), 0.0);
                if (ndl > 0.0) {
                    RayDesc sray;
                    sray.Origin    = hitPos + nHit * 2.0;
                    sray.Direction = sunL;
                    sray.TMin      = 1.0;
                    sray.TMax      = 1e7;
                    RayQuery<RAY_FLAG_FORCE_OPAQUE
                           | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> sq;
                    sq.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, sray);
                    sq.Proceed();
                    if (sq.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
                        indirect += throughput * aHit * sunCI * ndl / PI;
                    }
                }

                // Continue the path: cosine bounce, throughput *= albedo.
                throughput *= aHit;
                rayO   = hitPos + nHit * 2.0;
                rayDir = normalize(nHit + RandUnitVector(rngState));
            }
            continue;
        }
#endif
        // Fallback (no TLAS): occlusion-free sky irradiance estimate.
        float3 dir = normalize(N + RandUnitVector(rngState));
        float3 env = skyCube.SampleLevel(g_linearClamp, dir, 0.0).rgb;
        indirect += (1.0 - F0) * (1.0 - metallic) * albedo * env;
    }

    // Specular half — roughness-perturbed mirror reflection, one traced
    // bounce (hit shaded with sun NEE + hit albedo; miss samples sky LOD).
    // @ref Shirley 1992 "Nonuniform Random Point Sets via Warping"
    [loop]
    for (int ks = 0; ks < (PT_SAMPLES_PER_FRAME / 2); ++ks) {
        float3 mirrorDir  = reflect(-V, N);
        float3 diffuseDir = normalize(N + RandUnitVector(rngState));
        // alpha = roughness² — low roughness → mirror, high roughness → diffuse spread.
        float3 Lr    = normalize(lerp(mirrorDir, diffuseDir, alpha));
        float  NdotL = max(0.0, dot(N, Lr));
        if (NdotL > 0.0) {
            float  VdotH = max(0.0, dot(V, normalize(V + Lr)));
            float3 F     = F_Schlick(F0, VdotH);
            float  skyLOD = sqrt(roughness) * 5.0;
#ifdef UZ_BINDLESS
            if (ptHasTlas) {
                RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[g_tlasSrv];
                RayDesc ray;
                ray.Origin    = P + N * 2.0;
                ray.Direction = Lr;
                ray.TMin      = 1.0;
                ray.TMax      = 1e7;
                RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
                q.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
                q.Proceed();
                if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
                    float3 hitPos = ray.Origin + Lr * q.CommittedRayT();
                    float3 nHit   = GITrace_GdfNormal(hitPos, Lr);
                    float3 aHit   = GITrace_HitAlbedo(q.CommittedInstanceID());
                    float3 sunL   = normalize(lightDir.xyz);
                    float3 sunCI  = float3(lightColorR, lightColorG, lightColorB)
                                  * lightIntensity;
                    float  ndlH   = max(dot(nHit, sunL), 0.0);
                    float  sunVis = 1.0;
                    if (ndlH > 0.0) {
                        RayDesc sray;
                        sray.Origin    = hitPos + nHit * 2.0;
                        sray.Direction = sunL;
                        sray.TMin      = 1.0;
                        sray.TMax      = 1e7;
                        RayQuery<RAY_FLAG_FORCE_OPAQUE
                               | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> sq;
                        sq.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, sray);
                        sq.Proceed();
                        if (sq.CommittedStatus() == COMMITTED_TRIANGLE_HIT) sunVis = 0.0;
                    }
                    float3 hitRad = aHit * sunCI * ndlH * sunVis / PI;
                    indirect += F * hitRad;
                    continue;
                }
            }
#endif
            float3 env = skyCube.SampleLevel(g_linearClamp, Lr, skyLOD).rgb;
            indirect += F * env;
        }
    }
    indirect /= float(PT_SAMPLES_PER_FRAME / 2);

    // ── Emissive (GBuffer RT3) ─────────────────────────────────────────────
    // Emissive is additive over the path-traced lighting, exactly as the
    // deferred pipeline does. Not accumulated — read fresh each frame so
    // emissive changes take effect immediately without needing a reset.
    float3 emissive = emissiveTex.Load(int3(px, 0)).rgb;

    float3 newSample = directRadiance + indirect;

    // Firefly clamp. @ref Jensen 2001
    newSample = clamp(newSample, 0.0, 64.0);

    // ── Progressive accumulation (Step 6) ──────────────────────────────────
    // Read this pixel's history from the previous-frame ping-pong texture
    // and blend with the new sample using the running mean estimator
    //   accum_N = (accum_{N-1} * N + sample) / (N + 1)
    // Camera-cut detection on the CPU resets sampleCount to 0 each cut.
    // @ref Kajiya 1986 "The Rendering Equation"
    float3 blended;
    if (g_sampleCount == 0) {
        blended = newSample;
    } else {
#ifdef UZ_BINDLESS
        Texture2D<float4> prevTex = ResourceDescriptorHeap[g_accumPrevSrv];
        float3 prev = prevTex.Load(int3(px, 0)).rgb;
#else
        float3 prev = newSample; // SM5 path: no history binding (Step 6 bindless-only).
#endif
        float n = float(g_sampleCount);
        blended = (prev * n + newSample) / (n + 1.0);
    }

    PSOutput o;
    // Emissive added AFTER accumulation blend so it is never averaged into the
    // history — it's a stable, noise-free additive term.
    float3 finalColor = blended + emissive;

    o.accum = float4(blended, 1.0);     // RGBA32F — history without emissive (keeps accumulation clean).
    o.hdr   = float4(finalColor, 1.0);  // RGBA16F — with emissive, consumed by tonemap.
    return o;
}
