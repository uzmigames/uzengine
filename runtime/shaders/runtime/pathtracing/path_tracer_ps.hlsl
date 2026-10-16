/// UzEngine Path Tracer — Step 7: Direct-sun shadow via CSM shadow atlas.
///
/// Shadows are sampled from the same Cascaded Shadow Map atlas used by the
/// deferred lighting pipeline. This gives view-independent, stable sun shadows
/// matching the rasterized path exactly.
///
/// @ref Engel 2006 "Cascaded Shadow Maps" (ShaderX5)
///
/// Specular reflection is importance-sampled from the GGX visible-normal
/// distribution (VNDF): a microfacet normal is sampled from the view, the view
/// is reflected about it and the bounce is traced against the scene TLAS with
/// throughput weight F*G2/G1. Replaces the former roughness-perturbed mirror.
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
#include "../include/envmap_sampling.hlsli"
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
    uint g_materialTableSrv; // StructuredBuffer<GIMeshMaterial> by InstanceID (sentinel = GDF albedo).
    uint g_skyCubemapSrv;    // Atmosphere sky cubemap for bounce misses (sentinel = flat skylight).
    uint g_maxBounces;
    uint g_samplesPerFrame;
    uint g_clampFireflyBits;
    uint g_envIntensityBits;
    uint g_rrEnabled;
    uint g_rrMinBounce;
    uint g_equirectSrv;      // Raw equirect HDRI (direct bilinear tap, higher-res than the cube capture); 0xFFFFFFFF = none.
    uint g_envAliasSrv;
    uint g_envLumMip0Srv;
    uint g_envTotalLumBits;
    uint g_envLumWidth;
    uint g_envLumHeight;
};
// b2: TraceConstants (GDF clipmaps + sun/sky, the RadianceCacheTraceConstants
// layout the DDGI writer binds) — provides hit normals + hit albedo for bounces.
#include "../gi/gi_trace_common.hlsli"
#else
cbuffer PathTracerRC : register(b0) {
    uint g_sampleCount;
    uint g_frameIndex;
    uint g_maxBounces;
    uint g_samplesPerFrame;
    uint g_clampFireflyBits;
    uint g_envIntensityBits;
    uint g_rrEnabled;
    uint g_rrMinBounce;
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

/// Sky radiance for a world-space direction. The atmosphere/HDRI capture is a DX (+Y up)
/// cubemap while the world is Z-up, so the read swizzles (x, z, y) exactly like the deferred
/// WorldToCubemap / GITrace_SkyRadiance / SDF-reflection paths. LOD 0: the capture has one
/// mip and the reference spreads roughness by jittering the direction, not by prefiltering.
/// A sentinel SRV (no capture yet) returns the flat skylight colour.
float3 PT_SkyRadiance(float3 dir) {
#ifdef UZ_BINDLESS
    // Prefer the raw equirect HDRI (4K source, one bilinear tap = Falcor-equivalent) over the
    // captured 512-face sky cubemap (an extra equirect->cube resample) when it is available:
    // this lifts the mirror / background resolution floor on smooth metals. The world is Z-up
    // while the equirect mapping is Y-up, so swizzle (x, z, y) exactly as the cube capture did
    // (DirectionToEquirect from equirect_to_cubemap_cs.hlsl, inlined).
    if (g_equirectSrv != 0xFFFFFFFFu) {
        Texture2D<float4> equirect = ResourceDescriptorHeap[g_equirectSrv];
        float3 d = float3(dir.x, dir.z, dir.y);
        float  u = atan2(d.z, d.x) * M_1_PI * 0.5 + 0.5;
        float  v = 1.0 - (asin(clamp(d.y, -1.0, 1.0)) * M_1_PI + 0.5);
        return equirect.SampleLevel(g_linearClamp, float2(u, v), 0.0).rgb
             * asfloat(g_envIntensityBits);
    }
    return GITrace_SkyRadiance(dir, g_skyCubemapSrv) * asfloat(g_envIntensityBits);
#else
    return t_skyCube.SampleLevel(g_linearClamp, float3(dir.x, dir.z, dir.y), 0.0).rgb
         * asfloat(g_envIntensityBits);
#endif
}

#ifdef UZ_BINDLESS
/// Cosine-weighted path from (rayO, rayDir); `throughput` holds the first vertex's lobe
/// weight. Sun and environment next-event estimation at hits, MIS sky radiance on miss. Shared by the
/// diffuse and specular lobes, so a mirrored surface receives sky and bounce light, not
/// only directly-sun-lit faces. @ref Kajiya 1986
float3 PT_TracePath(float3 rayO, float3 rayDir, float3 throughput, inout uint rngState) {
    RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[g_tlasSrv];
    float3 sunL  = normalize(lightDir.xyz);
    float3 sunCI = float3(lightColorR, lightColorG, lightColorB) * lightIntensity;
    float3 L = float3(0, 0, 0);
    const bool envNee = (g_envAliasSrv != 0xFFFFFFFFu);
    const uint envTexelCount = g_envLumWidth * g_envLumHeight;
    const float envTotalLum = asfloat(g_envTotalLumBits);
    // The incoming primary diffuse/GGX ray has no paired NEE strategy. Only cosine
    // continuations sampled at the vertices below receive an environment MIS weight.
    float missWeight = 1.0;
    [loop]
    for (uint b = 0; b < min(g_maxBounces, 16u); ++b) {
        RayDesc ray;
        ray.Origin    = rayO;
        ray.Direction = rayDir;
        ray.TMin      = 1.0;
        ray.TMax      = 1e7;
        RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
        q.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
        q.Proceed();
        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
            L += throughput * PT_SkyRadiance(rayDir) * missWeight;
            break;
        }
        float3 hitPos = rayO + rayDir * q.CommittedRayT();
        uint   meshId = q.CommittedInstanceID();
        uint primIndex = q.CommittedPrimitiveIndex();
        float2 bary = q.CommittedTriangleBarycentrics();
        float3 aHit, nHit;
        if (GITrace_HitSurface(meshId, primIndex, bary, g_materialTableSrv, aHit, nHit)) {
            // Row-vector multiplication applies the inverse transpose to the
            // object-space normal, including nonuniform instance scale.
            nHit = mul(nHit, (float3x3)q.CommittedWorldToObject3x4());
            if (dot(nHit, nHit) > 1e-12) {
                nHit = normalize(nHit);
                if (dot(nHit, rayDir) > 0.0) nHit = -nHit;
            } else {
                nHit = GITrace_GdfNormal(hitPos, rayDir);
            }
        } else {
            aHit = GITrace_HitAlbedo(meshId, hitPos, g_materialTableSrv);
            nHit = GITrace_GdfNormal(hitPos, rayDir);
        }
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
                L += throughput * aHit * sunCI * ndl / PI;
            }
        }
        // Preserve the existing finite-bounce estimator: the last vertex has no
        // continuation, so it must not gain a new environment-lighting contribution.
        if (envNee && b + 1u < min(g_maxBounces, 16u)) {
            StructuredBuffer<AliasEntry> aliasTable = ResourceDescriptorHeap[g_envAliasSrv];
            EnvSample es = SampleEnvironment(
                Rand2(rngState), aliasTable, envTexelCount,
                g_envLumMip0Srv, envTotalLum, g_skyCubemapSrv,
                g_envLumWidth, g_envLumHeight, g_linearClamp);
            float envNdl = dot(nHit, es.direction);
            if (envNdl > 0.0 && es.pdf > 0.0) {
                RayDesc eray;
                eray.Origin    = hitPos + nHit * 2.0;
                eray.Direction = es.direction;
                eray.TMin      = 1.0;
                eray.TMax      = 1e7;
                RayQuery<RAY_FLAG_FORCE_OPAQUE
                       | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> eq;
                eq.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, eray);
                eq.Proceed();
                if (eq.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
                    float pdfB = envNdl / PI;
                    float w = es.pdf / (es.pdf + pdfB);
                    // Evaluate the same radiance as the miss strategy, including the
                    // raw HDRI override and intensity, rather than mixing cube/raw reads.
                    L += throughput * (aHit / PI) * PT_SkyRadiance(es.direction)
                       * envNdl * w / es.pdf;
                }
            }
        }
        throughput *= aHit;
        if (g_rrEnabled != 0u && b >= g_rrMinBounce) {
            float p = saturate(max(throughput.r, max(throughput.g, throughput.b)));
            if (Rand01(rngState) > p) break;
            throughput /= max(p, 1e-3);
        }
        rayO   = hitPos + nHit * 2.0;
        rayDir = normalize(nHit + RandUnitVector(rngState));
        if (envNee) {
            float pdfB = max(dot(nHit, rayDir), 0.0) / PI;
            float pdfEnv = EvaluateEnvironmentPDF(
                rayDir, g_envLumMip0Srv, envTotalLum, envTexelCount);
            missWeight = pdfB / (pdfB + pdfEnv);
        }
    }
    return L;
}
#endif



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

    // Skylight colour × intensity applied to every cubemap read, as the raster
    // split-sum and the probe writers do: the capture holds the unit-intensity
    // sky and the SkyLight component carries the scene's intensity. The SM5
    // path binds no trace constants, so it reads the capture unscaled.
#ifdef UZ_BINDLESS
    const float3 skyTint = GITrace_SkyTint();
#else
    const float3 skyTint = float3(1.0, 1.0, 1.0);
#endif

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
    // VNDF-sampled GGX microfacet specular (one traced bounce).
    //
    // Cosine sampling: pdf = cosT/PI cancels the Lambert cosT and the BRDF's
    // 1/PI, so the per-bounce throughput multiplier is exactly the hit
    // albedo. @ref Kajiya 1986; @ref Shirley 1992
    //
    // Without a TLAS (r.RayTracing off / non-DXR device), falls back to the
    // occlusion-free sky sample — NOT a reference in that mode.
    float3 indirect = float3(0, 0, 0);
    // Preserve the paired-lobe estimator; budgets below two still sample both lobes.
    const uint samplesPerLobe = max(1u, min(g_samplesPerFrame, 16u) / 2u);

#ifdef UZ_BINDLESS
    const bool ptHasTlas = (g_tlasSrv != kGITraceSentinelSrv);
#else
    const bool ptHasTlas = false;
#endif

    // Diffuse lobe: cosine-weighted hemisphere paths (sun NEE + sky on miss). Zero for pure
    // metals ((1-metallic)=0); their energy comes from the specular lobe below.
    [loop]
    for (uint kd = 0; kd < samplesPerLobe; ++kd) {
        float3 w   = (1.0 - F0) * (1.0 - metallic) * albedo;
        float3 dir = normalize(N + RandUnitVector(rngState));
#ifdef UZ_BINDLESS
        if (ptHasTlas) { indirect += PT_TracePath(P + N * 2.0, dir, w, rngState); continue; }
#endif
        // No TLAS: unoccluded sky irradiance — a smoke test, NOT a reference.
        indirect += w * PT_SkyRadiance(dir);
    }

    // Specular lobe: GGX microfacet reflection, importance-sampled from the visible normal
    // distribution (VNDF) so only microfacets visible from V are sampled — low variance at
    // every roughness, the same estimator the deferred split-sum, SSR and env-MIS passes use.
    // The VNDF reflection estimator's D(H) and cos cancel the sampling pdf exactly, leaving
    // the throughput weight F(V,H) * G2(V,L) / G1(V) (height-correlated Smith). This replaces
    // the old lerp(mirror, cosine) heuristic, which was only exact at alpha~0.
    // @ref Dupuy & Benyoub 2023 "Sampling Visible GGX Normals with Spherical Caps"
    // @ref Heitz 2018 "Sampling the GGX Distribution of Visible Normals"
    const float3 Vt = WorldToTangent(V, N, T, B);  // view direction, tangent space (Z along N)
    [loop]
    for (uint ks = 0; ks < samplesPerLobe; ++ks) {
        // Sample a visible microfacet normal H, then reflect V about it.
        float3 Ht = SampleGGX_VNDF(alpha, Vt, Rand2(rngState));
        float3 H  = normalize(TangentToWorld(Ht, N, T, B));
        float3 Lr = reflect(-V, H);
        float  NdotLr = dot(N, Lr);
        if (NdotLr <= 0.0) continue;
        float  VdotH = max(1e-4, dot(V, H));
        float3 F  = F_Schlick(F0, VdotH);
        float  g2 = G_SmithGGXCorrelated(alpha, NdotV, max(1e-4, NdotLr));
        float  g1 = G1_SmithGGX(alpha * alpha, NdotV);
        float3 w  = F * (g2 / max(g1, 1e-4));  // VNDF reflection weight (D and cos cancelled)
#ifdef UZ_BINDLESS
        if (ptHasTlas) { indirect += PT_TracePath(P + N * 2.0, Lr, w, rngState); continue; }
#endif
        indirect += w * PT_SkyRadiance(Lr);
    }
    indirect /= float(samplesPerLobe);

    // ── Emissive (GBuffer RT3) ─────────────────────────────────────────────
    // Emissive is additive over the path-traced lighting, exactly as the
    // deferred pipeline does. Not accumulated — read fresh each frame so
    // emissive changes take effect immediately without needing a reset.
    float3 emissive = emissiveTex.Load(int3(px, 0)).rgb;

    float3 newSample = directRadiance + indirect;

    // Firefly clamp. @ref Jensen 2001
    const float clampFirefly = asfloat(g_clampFireflyBits);
    if (clampFirefly > 0.0) newSample = clamp(newSample, 0.0, clampFirefly);

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
