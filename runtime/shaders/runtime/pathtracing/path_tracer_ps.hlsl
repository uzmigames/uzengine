/// UzEngine Path Tracer — Step 7: Direct-sun shadow via screen-space depth march
///
/// Hybrid deviation from `docs/analysis/path-tracing/04-incremental-plan.md`:
/// the original Step 7 calls for analytic ECS primitives (PtPrimitiveComponent).
/// Authoring those requires a parallel asset pipeline + Cornell-style test
/// scene; for Sponza they would be invisible. We instead do a screen-space
/// depth-buffer ray march for the direct sun shadow term, which gives
/// immediate, visible contact shadows on the existing GBuffer geometry.
///
/// Indirect bounces still escape to the sky (no scene tracing yet) — true
/// multi-bounce against arbitrary geometry needs analytic ECS primitives or
/// a BVH builder, both deferred to a future milestone.
///
/// @ref Bavoil 2008 "Multi-Layer Dual-Resolution Screen-Space Ambient Occlusion"
///      — screen-space depth march pattern
///
/// Adds physically-correct specular reflection on top of Step 4. Uses GGX
/// VNDF importance sampling (Dupuy & Benyoub 2023) and the Smith correlated
/// height-correlated visibility term, both from `material/brdf.hlsli` so the
/// path-traced result matches the rasterized PBR pipeline mathematically.
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
    uint g_skyCubeSrv;
    uint g_accumPrevSrv;  // Step 6: history read for progressive accumulation
    uint g_sampleCount;   // Step 6: number of samples already accumulated
    uint g_frameIndex;
};
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

// ─── Screen-space sun shadow (Step 7 hybrid) ────────────────────────────────
// March a ray in world space, project each step to screen, compare to depth.
// Returns 0 if occluded, 1 if visible. Uses a small step count to keep cost low.
//
// THICKNESS CHECK is critical: without it, the ray can falsely flag a pixel
// as shadowed when its screen projection lands on distant geometry that is
// in front of the sample but is NOT actually occluding the sun ray (the
// distant pixel is just BEHIND the screen-space ray in 3D). This is the
// classic SSCS false-occlusion bug. Mitigated by rejecting depth samples
// whose linear-depth difference exceeds a thickness window.
//
// @ref Bavoil 2008 — depth-buffer comparison shadow pattern
// @ref UE5 ContactShadows — UAttachedScreenSpaceShadowSettings::ContactShadowLength + thickness
float ScreenSpaceSunShadow(float3 worldPos, float3 sunDir, Texture2D<float> depthTex,
                           int2 screenSize)
{
    const int   kSteps     = 16;
    const float kStepWorld = 4.0;     // ~4 cm per step (Sponza is in cm)
    const float kBias      = 1.0;
    // Maximum world-space depth difference to count as a real occlusion.
    // Anything farther than this is treated as "not the same surface" and
    // skipped — avoids false shadows from distant background geometry.
    const float kMaxThickness = 25.0;

    float3 P = worldPos + sunDir * kBias;

    [loop]
    for (int i = 0; i < kSteps; ++i) {
        P += sunDir * kStepWorld;
        float4 clip = mul(viewProj, float4(P, 1.0));
        if (clip.w <= 0.0) return 1.0;
        float3 ndc = clip.xyz / clip.w;
        if (any(abs(ndc.xy) > 1.0)) return 1.0;
        float2 uv = ndc.xy * float2(0.5, -0.5) + 0.5;
        int2 px = int2(uv * float2(screenSize));
        float sceneZ = depthTex.Load(int3(px, 0));
        // Reversed-Z: depth==1 near, depth==0 far.
        // sceneZ > rayZ → scene point is in FRONT of (closer than) the ray.
        // That's only a real occluder if the depth difference is small —
        // i.e. the scene point is on the SAME surface, not a background object.
        // Convert to linear depth via the projection's inverse to test thickness.
        if (sceneZ > ndc.z) {
            // Compute world position of the depth-buffer sample at this UV.
            float4 sampleClip = float4(ndc.xy, sceneZ, 1.0);
            // Linear depth thickness in world space: project the ray-pos and
            // depth-sample back to view space and compare distances along the
            // sun ray. Cheap approximation: use 1/clip.w as a proxy for view-z.
            float rayLinear   = clip.w;       // view-space depth of ray sample
            float sceneLinear = clip.w * (ndc.z / max(sceneZ, 1e-6));
            float thickness   = abs(rayLinear - sceneLinear);
            if (thickness < kMaxThickness) {
                return 0.0; // real occluder
            }
            // Otherwise: distant background, ignore and keep marching.
        }
    }
    return 1.0;
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
    TextureCube<float4> skyCube    = ResourceDescriptorHeap[g_skyCubeSrv];
#else
    #define normalTex   t_normal
    #define depthTex    t_depth
    #define albedoTex   t_albedo
    #define positionTex t_position
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

    // ── Direct sun ─────────────────────────────────────────────────────────
    // Lambert diffuse weighted by (1 - F) energy conservation, plus GGX
    // specular evaluated at the sun direction.
    // @ref Karis 2013, @ref Heitz 2014
    // ── Direct sun (no shadow occlusion in this milestone) ────────────────
    // Step 7's screen-space shadow ray was view-dependent — the sun shadow
    // appeared/disappeared as the camera moved because depth-buffer projection
    // is camera-relative by definition. A real sun-relative shadow needs the
    // CSM shadow atlas, which is wired in a future step (deferred to keep this
    // path tracer milestone honest about what it does and doesn't simulate).
    // For now: unconditional sun term, matching the early-Shadertoy behaviour.
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

            directRadiance = (diffuse + specular) * lightColor.rgb * NdotL;
        }
    }

    // ── Indirect environment (split lobes) ─────────────────────────────────
    // Half the samples sample the diffuse cosine hemisphere; half sample the
    // GGX VNDF specular lobe. Each weighted by its respective response.
    // For diffuse the cosine PDF cancels (1/π) so the integrand is
    // (1-F)*(1-metallic)*albedo*L_env. For specular the VNDF cancels D and
    // the masking term to leave F*G2/G1 * L_env (Heitz 2018 simplification).
    // @ref Heitz 2018 "Sampling the GGX Distribution of Visible Normals"
    float3 indirect = float3(0, 0, 0);

    // Diffuse half
    [unroll]
    for (int kd = 0; kd < (PT_SAMPLES_PER_FRAME / 2); ++kd) {
        float3 dir = normalize(N + RandUnitVector(rngState));
        float3 env = skyCube.SampleLevel(g_linearClamp, dir, 0.0).rgb;
        // Average Fresnel ≈ F0 for the diffuse-vs-specular split — cheap and
        // matches the split-sum approximation used in IBL.
        // @ref Karis 2013
        indirect += (1.0 - F0) * (1.0 - metallic) * albedo * env;
    }

    // Specular half via GGX VNDF
    float3 Vt = WorldToTangent(V, N, T, B);
    [unroll]
    for (int ks = 0; ks < (PT_SAMPLES_PER_FRAME / 2); ++ks) {
        float2 u = Rand2(rngState);
        float3 Ht = SampleGGX_VNDF(alpha, Vt, u);
        float3 H  = TangentToWorld(Ht, N, T, B);
        float3 Lr = reflect(-V, H);
        float  NdotL = dot(N, Lr);
        if (NdotL > 0.0) {
            float  VdotH = max(0.0, dot(V, H));
            float3 F     = F_Schlick(F0, VdotH);
            // Heitz 2018 simplification: VNDF importance-sampled estimator
            // for specular = F * G2/G1 (no D, no PDF division).
            float G1 = G1_SmithGGX(alpha * alpha, NdotV);
            float G2 = G_SmithGGXCorrelated(alpha, NdotV, NdotL);
            float weight = (G1 > 1e-6) ? (G2 / G1) : 0.0;
            float3 env = skyCube.SampleLevel(g_linearClamp, Lr, 0.0).rgb;
            indirect += F * weight * env;
        }
    }
    indirect /= float(PT_SAMPLES_PER_FRAME / 2);

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
    o.accum = float4(blended, 1.0); // RGBA32F — full-precision history.
    o.hdr   = float4(blended, 1.0); // RGBA16F — consumed by tonemap downstream.
    return o;
}
