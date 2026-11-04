/// Environment MIS spatial resolve + temporal accumulation (r.EnvMIS.Denoise).
///
/// @ref Stachowiak & Uludag 2015 "Stochastic Screen-Space Reflections" (SIGGRAPH Advances course) — the spatial
///      resolve of a stochastic specular estimate over neighbouring pixels before the temporal filter
/// @ref Schied et al. 2017 "Spatiotemporal Variance-Guided Filtering" — the edge-stopping normal weight
/// @ref Karis 2014 "High Quality Temporal Supersampling" — temporal accumulation
///
/// env_mis_temporal_cs.hlsl estimates the specular environment term of smooth metals with 8 samples per pixel
/// per frame and accumulates it with an exponential moving average whose floor (g_temporalAlpha = 0.05) keeps a
/// steady-state noise of sqrt(alpha / (2 - alpha)) = 16 % of the single-frame noise: on a rough metal a fine
/// speckle that never converges and boils from frame to frame, invisible at a distance, plain in a close-up.
///
/// With r.EnvMIS.Denoise that shader writes its estimate alone (no history: rgb the estimate, a the coverage) into
/// a raw texture, and this pass resolves each frame's estimate over the neighbouring pixels whose lobes see the
/// same environment before accumulating it. A tap is weighted by a Gaussian footprint (g_radius), by the angle
/// between the shading normals against the lobe's own width (sigma = g_lobeFraction x the GGX alpha: the median
/// half-vector angle of the lobe is ~alpha, so a neighbour whose normal lies within a fraction of it reflects the
/// same part of the environment through a lobe that already blurs it), and by the material (roughness sets the
/// lobe, the base colour the F0 the estimate carries). A near-mirror accepts no neighbour (its lobe is narrow, its
/// estimate clean anyway), a rough metal accepts the whole kernel, and a silhouette, where the normal turns fast,
/// keeps its Fresnel rim. The resolved estimate then goes through the reprojected, count-based accumulation of
/// the UZ_ENVMIS_REPROJECT permutation (env_mis_reproject.hlsli), and the history, geometry and output layouts are
/// the same, so the two paths can be switched at any frame.
///
/// r.EnvMIS.Temporal (the UZ_ENVMIS_TEMPORAL=1 permutation) deepens that accumulation where the surface is stable:
/// the resolve leaves a residual at the 0.05 floor (16 % of the resolved estimate's noise, a slow mottle at the
/// kernel's scale), and a lower floor alone would lag every change by as many frames. Two histories are kept in an
/// RGBA32F accumulator (fp32: the running average's per-frame step at 256 samples is a tenth of an fp16 ulp, which
/// an fp16 store would round away). The FAST reference is the accumulation above, on the resolved luminance (w, a
/// packed half), next to the accumulated noise standard deviation of one resolved estimate (w, the other half),
/// measured each frame from the footprint itself (the weighted variance of the independent 8-spp estimates the
/// kernel averaged, divided by the effective tap count; a spatial measure, so a changing signal cannot inflate it).
/// The SLOW history (xyz), the output, is the plain running average 1/count with the count cap raised to
/// g_temporalParams' MaxCount, and its luminance is clamped every frame to the fast reference +- Clamp x the fast
/// reference's own noise (sigma x sqrt(w / (2 - w)), the steady state of an EMA of weight w): the output can never
/// sit farther from the r.EnvMIS.Denoise result than that reference's noise band, so a light step or a sliding
/// reflection is followed as before. The band widens with the count (full at the 64 samples of the old cap): a
/// pixel that just restarted or was just corrected is held close to the reference while the reference itself is
/// still converging, a settled one is left to its depth. A clamp halves the count (never below the 1/alpha the fast
/// path is worth), so a real change collapses the slow path onto the fast one within a few frames and the deep
/// average restarts cleanly once the surface is still again; the rare clamp of a static pixel (a 2.5-sigma
/// excursion of the reference) costs it half its depth, which it regains in as many frames. The fp16 history
/// receives the same output for the lighting pass, so its layout and the geometry history are those of the
/// permutation without this.
/// @ref Karis 2014 (neighbourhood clamping of the history), Schied et al. 2017 (luminance variance as the gate)
///
/// Root constants at b0 (21; 24 with UZ_ENVMIS_TEMPORAL): see RootConstants. No PerFrameCB: the camera position
/// comes with the constants.

#ifndef UZ_BINDLESS
[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {}
#else

#include "../common/constants.hlsli"
#include "../common/encoding.hlsli"

// ─── Root Constants (b0) ──────────────────────────────────────────────────
cbuffer RootConstants : register(b0) {
    uint  g_albedoIdx;
    uint  g_normalIdx;
    uint  g_positionIdx;
    uint  g_rawSrvIdx;              ///< This frame's estimate (rgb) + coverage (a), env_mis_temporal_cs.hlsl without a history
    uint  g_width;
    uint  g_height;
    uint  g_historyReadSrvIdx;      ///< Previous frame's accumulated result (UINT32_MAX = none)
    uint  g_outputUavIdx;           ///< This frame's accumulated result (rgb) + coverage (a)
    uint  g_velocitySrvIdx;         ///< Velocity buffer (RG16F, pixels, this frame minus the previous; TAA's convention)
    uint  g_historyGeometrySrvIdx;  ///< The previous frame's geometry history (UINT32_MAX = none: restart)
    uint  g_geometryUavIdx;         ///< This frame's geometry history UAV
    uint  g_historyReusable;        ///< 1 when the history was written on the previous frame under a continuous camera
    float g_prevCameraPosX;         ///< The previous frame's camera position (cm): the view distance the taps were stored with
    float g_prevCameraPosY;
    float g_prevCameraPosZ;
    float g_cameraPosX;             ///< This frame's camera position (cm): the view distance stored for the next frame
    float g_cameraPosY;
    float g_cameraPosZ;
    float g_temporalAlpha;          ///< EMA floor (0.05 = 95% history, 5% new) once the count has grown past 1/alpha
    uint  g_radius;                 ///< Kernel radius in pixels (r.EnvMIS.Denoise.Radius)
    float g_lobeFraction;           ///< Normal-weight sigma as a fraction of the GGX alpha (r.EnvMIS.Denoise.LobeFraction)
#if UZ_ENVMIS_TEMPORAL
    // r.EnvMIS.Temporal (the UZ_ENVMIS_TEMPORAL=1 permutation): the fast-reference history and the two parameters,
    // packed because the root signature holds 24 constants.
    uint  g_accumSrvIdx;            ///< The previous frame's accumulator (RGBA32F; UINT32_MAX = none: start from the fp16 history)
    uint  g_accumUavIdx;            ///< This frame's accumulator UAV
    uint  g_temporalParams;         ///< Bits 0-15 MaxCount (r.EnvMIS.Temporal.MaxCount), bits 16-31 Clamp x 256 (r.EnvMIS.Temporal.Clamp)
#endif
};

#include "env_mis_reproject.hlsli"

/// The lighting pass clamps the roughness to sqrt(r.Material.MinGGXAlpha) = 0.04 by default; the same floor the
/// estimate uses (env_mis_temporal_cs.hlsl kMinRoughness).
static const float kMinRoughness   = 0.04;
/// Material weights: a roughness step changes the lobe the tap integrated, a base-colour step the F0 it carries.
static const float kRoughnessSigma = 0.05;
static const float kAlbedoSigma    = 0.1;
#if UZ_ENVMIS_TEMPORAL
/// Rec. 709 luminance: the fast reference and the clamp work on luminance; the slow history keeps its chroma.
static const float3 kLuminance = float3(0.2126, 0.7152, 0.0722);
#endif

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= g_width || DTid.y >= g_height) return;

    int2 px = int2(DTid.xy);

    Texture2D<float4>   rawTex      = ResourceDescriptorHeap[g_rawSrvIdx];
    RWTexture2D<float4> outputTex   = ResourceDescriptorHeap[g_outputUavIdx];
    RWTexture2D<float4> geometryOut = ResourceDescriptorHeap[g_geometryUavIdx];

    float4 raw      = rawTex.Load(int3(px, 0));
    float  coverage = raw.a;

    // Sky, a dielectric, a rough surface: no estimate, nothing to accumulate; count 0 so a metal that moves over
    // this pixel cannot inherit anything (the accumulation permutation writes the same).
    if (coverage <= 0.0) {
        outputTex[px]   = float4(0, 0, 0, 0);
        geometryOut[px] = float4(0, 0, 0, 0);
        return;
    }

    Texture2D<float4> gbAlbedo = ResourceDescriptorHeap[g_albedoIdx];
    Texture2D<float4> gbNormal = ResourceDescriptorHeap[g_normalIdx];
    Texture2D<float4> gbPos    = ResourceDescriptorHeap[g_positionIdx];

    float4 rt1       = gbNormal.Load(int3(px, 0));
    float3 N         = OctDecode(rt1.rg);
    float  roughness = max(rt1.a, kMinRoughness);
    float  alpha     = roughness * roughness;
    float3 baseColor = gbAlbedo.Load(int3(px, 0)).rgb;
    float3 worldPos  = gbPos.Load(int3(px, 0)).xyz;
    float3 cameraPos = float3(g_cameraPosX, g_cameraPosY, g_cameraPosZ);

    // ── Spatial resolve of this frame's estimate ─────────────────────────
    // Normal weight exp(-theta^2 / (2 sigma^2)) with theta^2 / 2 ~= 1 - N.Nq for the small angles that pass.
    const float sigmaN     = max(g_lobeFraction * alpha, 1e-3);
    const float invSigmaN2 = 1.0 / (sigmaN * sigmaN);
    const int   radius     = int(g_radius);
    const float sigmaPx    = max(0.6 * float(radius), 0.5);
    const float invSigmaPx = 1.0 / (2.0 * sigmaPx * sigmaPx);

    float3 colorSum  = float3(0, 0, 0);
    float  weightSum = 0.0;
#if UZ_ENVMIS_TEMPORAL
    // The footprint's weighted second moment and the sum of squared weights: the noise of one resolved estimate.
    float  lum2Sum    = 0.0;
    float  weight2Sum = 0.0;
#endif
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int2 q = px + int2(dx, dy);
            if (any(q < 0) || any(q >= int2(g_width, g_height))) continue;
            float4 rq = rawTex.Load(int3(q, 0));
            if (rq.a <= 0.0) continue;
            float4 rt1q   = gbNormal.Load(int3(q, 0));
            float3 Nq     = OctDecode(rt1q.rg);
            float  roughq = max(rt1q.a, kMinRoughness);
            float3 albq   = gbAlbedo.Load(int3(q, 0)).rgb;
            float3 dAlb   = abs(albq - baseColor);
            float  w = exp(-float(dx * dx + dy * dy) * invSigmaPx
                           - (1.0 - saturate(dot(N, Nq))) * invSigmaN2
                           - abs(roughq - roughness) / kRoughnessSigma
                           - max(dAlb.r, max(dAlb.g, dAlb.b)) / kAlbedoSigma);
            colorSum  += rq.rgb * w;
            weightSum += w;
#if UZ_ENVMIS_TEMPORAL
            float lq = dot(rq.rgb, kLuminance);
            lum2Sum    += lq * lq * w;
            weight2Sum += w * w;
#endif
        }
    }
    // The centre tap has weight 1, so the sum is never empty.
    float3 newSample = colorSum / weightSum;

#if UZ_ENVMIS_TEMPORAL
    // ── r.EnvMIS.Temporal: the deep accumulation clamped to the fast reference ──
    const uint  maxCount   = g_temporalParams & 0xffffu;
    const float clampSigma = float(g_temporalParams >> 16u) * (1.0 / 256.0);
    const float lumNew     = dot(newSample, kLuminance);
    // The taps are independent 8-spp estimates of (nearly) one integral, so the weighted sample variance of their
    // luminance over the effective tap count n = (sum w)^2 / sum w^2 is the variance of the resolved estimate; with
    // Bessel's correction, var / (n - 1). One tap alone measures nothing: the accumulated value is kept.
    const float nEff   = weightSum * weightSum / max(weight2Sum, 1e-8);
    const float varRaw = max(lum2Sum / weightSum - lumNew * lumNew, 0.0);
    const bool  hasSigma = nEff > 1.5;
    float sigmaNew = hasSigma ? sqrt(varRaw / (nEff - 1.0)) : 0.0;

    RWTexture2D<float4> accumOut = ResourceDescriptorHeap[g_accumUavIdx];

    float3 result = newSample;
    float  count  = 1.0;
    float2 moments = float2(lumNew, sigmaNew);
    if (g_historyReusable != 0u && g_historyReadSrvIdx != kSentinelSrvIndex
        && g_historyGeometrySrvIdx != kSentinelSrvIndex) {
        Texture2D<float4> geometryTex = ResourceDescriptorHeap[g_historyGeometrySrvIdx];
        Texture2D<float2> velocityTex = ResourceDescriptorHeap[g_velocitySrvIdx];
        float3 historyColor;
        float  historyCount;
        float2 historyMoments;
        bool   valid;
        if (g_accumSrvIdx != kSentinelSrvIndex) {
            Texture2D<float4> accumTex = ResourceDescriptorHeap[g_accumSrvIdx];
            valid = ReprojectEnvMisAccumulator(px, worldPos, N, accumTex, geometryTex, velocityTex,
                                               historyColor, historyCount, historyMoments);
        } else {
            // The first frame of this permutation: the r.EnvMIS.Denoise history is the slow history and the fast
            // reference so far.
            Texture2D<float4> historyTex = ResourceDescriptorHeap[g_historyReadSrvIdx];
            valid = ReprojectEnvMisHistory(px, worldPos, N, historyTex, geometryTex, velocityTex, historyColor, historyCount);
            historyMoments = float2(dot(historyColor, kLuminance), sigmaNew);
        }
        if (valid) {
            count = min(historyCount + 1.0, float(maxCount));
            // The fast reference: the r.EnvMIS.Denoise weights (1/count down to the alpha floor; the count only
            // matters below 1/alpha, so the raised cap leaves them as they were).
            const float wFast = max(1.0 / count, g_temporalAlpha);
            if (!hasSigma) sigmaNew = historyMoments.y;
            moments = float2(lerp(historyMoments.x, lumNew, wFast), lerp(historyMoments.y, sigmaNew, wFast));
            // The slow history: the running average up to the raised cap.
            result = lerp(historyColor, newSample, 1.0 / count);
            // The clamp: the slow luminance may stray from the fast reference by Clamp x the reference's own noise,
            // and the band widens with the count: a pixel that just restarted or was just corrected is held close to
            // the reference while that reference is still converging, one that has been still for the old cap's 64
            // frames is left to its depth. Up to 1/alpha samples the two paths share their weights, so no test.
            if (count > 1.0 / g_temporalAlpha + 0.5) {
                const float sigmaFast  = moments.y * sqrt(wFast / (2.0 - wFast));
                const float band       = clampSigma * sigmaFast * saturate(count / kEnvMisMaxCount);
                const float lumSlow    = dot(result, kLuminance);
                const float lumClamped = clamp(lumSlow, moments.x - band, moments.x + band);
                if (lumClamped != lumSlow) {
                    result = lumSlow > 1e-8 ? result * (lumClamped / lumSlow) : newSample;
                    // Half the depth, never below what the fast path is worth. (Holding the count while the two
                    // still disagree was tried and traps the pixel: a clamp parks the value on the band's edge, a
                    // held count holds the band, and the pixel never deepens again.)
                    count  = max(0.5 * count, 1.0 / g_temporalAlpha);
                }
            }
        }
    }
    geometryOut[px] = float4(OctEncode(N), length(cameraPos - worldPos) * 0.01, count);
    accumOut[px]    = float4(result, PackEnvMisMoments(moments));
    outputTex[px]   = float4(result, coverage);
#else
    // ── Temporal accumulation (env_mis_temporal_cs.hlsl, UZ_ENVMIS_REPROJECT permutation) ──
    float3 result = newSample;
    float  count  = 1.0;
    if (g_historyReusable != 0u && g_historyReadSrvIdx != kSentinelSrvIndex
        && g_historyGeometrySrvIdx != kSentinelSrvIndex) {
        Texture2D<float4> historyTex  = ResourceDescriptorHeap[g_historyReadSrvIdx];
        Texture2D<float4> geometryTex = ResourceDescriptorHeap[g_historyGeometrySrvIdx];
        Texture2D<float2> velocityTex = ResourceDescriptorHeap[g_velocitySrvIdx];
        float3 historyColor;
        float  historyCount;
        if (ReprojectEnvMisHistory(px, worldPos, N, historyTex, geometryTex, velocityTex, historyColor, historyCount)) {
            count  = min(historyCount + 1.0, kEnvMisMaxCount);
            result = lerp(historyColor, newSample, max(1.0 / count, g_temporalAlpha));
        }
    }
    geometryOut[px] = float4(OctEncode(N), length(cameraPos - worldPos) * 0.01, count);
    outputTex[px]   = float4(result, coverage);
#endif // UZ_ENVMIS_TEMPORAL
}

#endif // UZ_BINDLESS
