/// UzEngine GTAO — Ground Truth Ambient Occlusion (compute shader).
/// @ref Jimenez 2016 "Practical Realtime Strategies for Accurate Indirect Occlusion"
/// @constants worldRadius (cm), attenFactor = 2.0/R², aoPower (default 1.0),
///            thicknessBlend (default 0.5), offsetAdvance = 0.617

// ── Root constants at b0 (16 DWORDs = 64 bytes) ─────────────────────────────
cbuffer GTAORootCB : register(b0) {
#ifdef UZ_BINDLESS
    uint   depthSrvIndex;     // 0
    uint   normalSrvIndex;    // 1
#else
    uint   _srvPad0;          // 0 (unused in SM5 — textures bound via register)
    uint   _srvPad1;          // 1
#endif
    uint   outputWidth;       // 2
    uint   outputHeight;      // 3
    float  invTanHalfFov;     // 4
    float  nearPlane;         // 5
    float  farPlane;          // 6
    uint   frameAndHiz;       // 7  bits[0:15]=frameIndex, bits[16:31]=hizSrvIndex
    uint   numAngles;         // 8
    uint   numTaps;           // 9
    float  worldRadius;       // 10
    float  aoIntensity;       // 11
    float  thicknessBlend;    // 12
    float  fadeStart;         // 13
    float  fadeEnd;           // 14
    float  aoPower;           // 15
};

RWTexture2D<float> outputAO : register(u0);
#ifndef UZ_BINDLESS
Texture2D<float>  t_depth  : register(t0);
Texture2D<float4> t_normal : register(t1);
#endif
SamplerState pointClamp : register(s0);

static const float PI       = 3.14159265359;
static const float PI_HALF  = 1.57079632679;

// ── Depth helpers (reversed-Z: 1.0=near, 0.0=far) ──────────────────────────

float LinearizeDepth(float deviceZ) {
    return nearPlane * farPlane / (nearPlane + deviceZ * (farPlane - nearPlane));
}

float3 ScreenToViewPos(float2 uv, float linearZ) {
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float aspect = float(outputWidth) / float(outputHeight);
    return float3(ndc.x * linearZ / (invTanHalfFov / aspect),
                  ndc.y * linearZ / invTanHalfFov,
                  linearZ);
}

float SampleDepth(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float> depthTex = ResourceDescriptorHeap[depthSrvIndex];
#else
    Texture2D<float> depthTex = t_depth;
#endif
    return depthTex.SampleLevel(pointClamp, uv, 0).r;
}

// ── Normal reconstruction from depth (TakeSmallerAbsDelta) ──────────────────
/// @ref Jimenez 2016 — reconstructs view-space normal from depth derivatives,
/// choosing the smaller delta at each axis to avoid silhouette artifacts.

float3 ReconstructNormal(float2 uv, float2 texelSize, float centerZ) {
    float zL = SampleDepth(uv + float2(-texelSize.x, 0));
    float zR = SampleDepth(uv + float2( texelSize.x, 0));
    float zU = SampleDepth(uv + float2(0, -texelSize.y));
    float zD = SampleDepth(uv + float2(0,  texelSize.y));

    // TakeSmallerAbsDelta — pick derivative with smaller depth change
    float ddxZ = (abs(centerZ - zL) < abs(zR - centerZ))
               ? (centerZ - zL) : (zR - centerZ);
    float ddyZ = (abs(centerZ - zU) < abs(zD - centerZ))
               ? (centerZ - zU) : (zD - centerZ);

    float linC = LinearizeDepth(centerZ);
    float3 posC = ScreenToViewPos(uv, linC);
    float3 posR = ScreenToViewPos(uv + float2(texelSize.x, 0), LinearizeDepth(centerZ + ddxZ)) - posC;
    float3 posD = ScreenToViewPos(uv + float2(0, texelSize.y), LinearizeDepth(centerZ + ddyZ)) - posC;

    return normalize(cross(posR, posD));
}

// ── Fast acos approximation ─────────────────────────────────────────────────
/// @ref Penner 2011 polynomial: max error ~0.02 radians
float acosFast(float x) {
    float ax = abs(x);
    float res = (-0.156583 * ax + PI_HALF) * sqrt(1.0 - ax);
    return (x >= 0.0) ? res : PI - res;
}

// ── Interleaved Gradient Noise ──────────────────────────────────────────────
/// @ref Jimenez 2014 "Next Generation Post Processing in Call of Duty"
float IGN(float2 pixelCoord) {
    return frac(52.9829189 * frac(pixelCoord.x * 0.06711056 + pixelCoord.y * 0.00583715));
}

// ── Horizon search in one direction ─────────────────────────────────────────
/// @ref Jimenez 2016 — marches numTaps steps in both +/- screenDir,
/// tracking the maximum horizon angle (as cosine) with thickness heuristic.

float2 FindMaxHorizonAngle(uint steps, float2 baseUV, float2 screenDir,
                            float searchRadius, float initialOffset,
                            float3 viewPos, float3 viewDir,
                            float attenFactor, float2 texelSize)
{
    float2 bestAng = float2(-1.0, -1.0);

    // @ref UE5 PostProcessAmbientOcclusion.usf — StepSize = PixelRadius / NumTaps.
    // Samples march within [0, searchRadius] in `steps` increments. Previous
    // formulation `searchRadius * (i + offset)` overshot the radius from the
    // second sample onward, causing nearly all samples to fall outside the
    // attenuation falloff and produce a near-constant flat AO output.
    float stepSize = searchRadius / float(steps);

    for (uint i = 0; i < steps; i++) {
        float stepOffset = max(stepSize * (float(i) + initialOffset), float(i) + 1.0);

        // Marching happens in UV space (+V = screen down) while the slice frame
        // built by IntegrateHorizonArc lives in view space (+Y = up, because
        // ScreenToViewPos negates ndc.y). Marching `screenDir` unflipped samples
        // slice azimuth -phi while gamma / |n_p| are evaluated for +phi: the two
        // horizons come back swapped about the view vector, the visible arc
        // collapses from PI to a wedge of half-width (PI/2 - |gamma|), and any
        // unoccluded plane integrates to (N.V)^2 instead of 1 — radius-independent.
        // @ref Jimenez et al. 2016 "Practical Realtime Strategies for Accurate
        //      Indirect Occlusion" — dual-direction horizon search
        float2 uvOfsPos = float2(screenDir.x, -screenDir.y) * stepOffset * texelSize;
        float2 uvOfsNeg = -uvOfsPos;

        float2 uvPos = baseUV + uvOfsPos;
        float2 uvNeg = baseUV + uvOfsNeg;

        // Clamp to screen
        uvPos = clamp(uvPos, 0.0, 1.0);
        uvNeg = clamp(uvNeg, 0.0, 1.0);

        float depthPos = LinearizeDepth(SampleDepth(uvPos));
        float depthNeg = LinearizeDepth(SampleDepth(uvNeg));

        // Positive direction
        {
            float3 V = ScreenToViewPos(uvPos, depthPos) - viewPos;
            float lenSq = dot(V, V) + 0.0001;
            float angPos = dot(V, viewDir) * rsqrt(lenSq);
            float falloff = saturate(lenSq * attenFactor);
            angPos = lerp(angPos, bestAng.x, falloff);
            bestAng.x = (angPos > bestAng.x)
                       ? angPos
                       : lerp(angPos, bestAng.x, thicknessBlend);
        }

        // Negative direction
        {
            float3 V = ScreenToViewPos(uvNeg, depthNeg) - viewPos;
            float lenSq = dot(V, V) + 0.0001;
            float angNeg = dot(V, viewDir) * rsqrt(lenSq);
            float falloff = saturate(lenSq * attenFactor);
            angNeg = lerp(angNeg, bestAng.y, falloff);
            bestAng.y = (angNeg > bestAng.y)
                       ? angNeg
                       : lerp(angNeg, bestAng.y, thicknessBlend);
        }
    }

    bestAng.x = acosFast(clamp(bestAng.x, -1.0, 1.0));
    bestAng.y = acosFast(clamp(bestAng.y, -1.0, 1.0));
    return bestAng;
}

// ── Inner integral — visible arc against projected normal ───────────────────
/// @ref Jimenez 2016 — integrates the unoccluded arc in the slice plane,
/// weighted by the surface normal projected onto the slice.

float IntegrateHorizonArc(float2 angles, float2 screenDir,
                           float3 viewDir, float3 viewNormal)
{
    float3 planeNormal = normalize(cross(float3(screenDir, 0.0), viewDir));
    float3 perp        = cross(viewDir, planeNormal);

    float3 projNormal  = viewNormal - planeNormal * dot(viewNormal, planeNormal);
    float  lenProj     = length(projNormal) + 1e-6;
    float  recipMag    = 1.0 / lenProj;

    float cosAng   = dot(projNormal, perp) * recipMag;
    float gamma    = acosFast(cosAng) - PI_HALF;
    float cosGamma = dot(projNormal, viewDir) * recipMag;
    float sinGamma = cosAng * -2.0;

    // Clamp horizon angles relative to gamma
    angles.x = gamma + max(-angles.x - gamma, -PI_HALF);
    angles.y = gamma + min( angles.y - gamma,  PI_HALF);

    float ao = lenProj * 0.25 *
        ((angles.x * sinGamma + cosGamma - cos(2.0 * angles.x - gamma)) +
         (angles.y * sinGamma + cosGamma - cos(2.0 * angles.y - gamma)));
    return ao;
}

// ── Entry point ─────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= outputWidth || DTid.y >= outputHeight) return;

    // Hard culling when intensity is effectively zero. Prevents any sub-pixel
    // horizon-search artifacts from leaking into the final composite when GTAO
    // is disabled via CVar. Writes the neutral value (1.0 = no occlusion) so
    // the deferred lighting min(meshAO, gtao) branch becomes a no-op.
    if (aoIntensity < 0.001) {
        outputAO[DTid.xy] = 1.0;
        return;
    }

    float2 texelSize = 1.0 / float2(outputWidth, outputHeight);
    float2 uv = (float2(DTid.xy) + 0.5) * texelSize;

    float deviceZ = SampleDepth(uv);

    // Sky: reversed-Z far = 0
    if (deviceZ <= 0.0001) {
        outputAO[DTid.xy] = 1.0;
        return;
    }

    float linearZ = LinearizeDepth(deviceZ);

    // Distance fade — lerp toward 1.0 (unoccluded) beyond fadeStart
    float fadeRange = max(fadeEnd - fadeStart, 1.0);
    float distFade  = saturate((linearZ - fadeStart) / fadeRange);
    if (distFade >= 1.0) {
        outputAO[DTid.xy] = 1.0;
        return;
    }

    float3 viewPos    = ScreenToViewPos(uv, linearZ);
    float3 viewDir    = normalize(-viewPos);
    float3 viewNormal = ReconstructNormal(uv, texelSize, deviceZ);

    // Per-pixel noise: IGN for spatial, temporal rotation/offset
    uint frameIdx   = frameAndHiz & 0xFFFFu;
    float noise     = IGN(float2(DTid.xy));

    // Temporal rotation: 6 distinct angles per 6-frame cycle
    float temporalAngle = float(frameIdx % 6) * (PI / 3.0);
    // Temporal offset: 4 phases per 4-frame cycle
    float temporalOffset = float((frameIdx / 6) % 4) * 0.25;

    float baseAngle  = noise * PI + temporalAngle;
    float stepOffset = frac(noise + temporalOffset);

    // Screen-space search radius: world-space radius → NDC → pixels.
    // @ref UE5 PostProcessAmbientOcclusion.usf:870 — FOVScale = ViewportHeight * InvTanHalfFov.
    // invTanHalfFov is the vertical FoV cotangent (proj[1][1]); in NDC [-1,+1] the
    // Y axis maps to `height/2` pixels, NOT `width/2`. Using width here overestimates
    // the pixel radius by the aspect ratio (1.78× at 16:9) and mismatches the
    // horizontal/vertical sampling isotropy the horizon integral assumes.
    float screenRadius = (worldRadius * invTanHalfFov) / linearZ;
    screenRadius *= 0.5 * float(outputHeight); // NDC → pixels (vertical axis)
    // Step budget: numTaps taps must cover the whole radius, so a large cap
    // means the search steps *over* geometry between taps. At 1080p with
    // r.GTAO.Radius=200 and 8 taps a mid-distance pixel already marches 352 px
    // (44 px/tap). 256 px matches r.GTAO.MaxScreenRadius (currently declared
    // in gtao_pass.cpp but not yet uploaded to the root CB, so this literal
    // shadows it until a CB slot is freed for it).
    screenRadius = min(screenRadius, 256.0);

    // Clamp the pixel radius up to the tap count rather than discarding the
    // pixel. bestAng = -1 -> acosFast(-1) = PI is the fully OPEN horizon
    // (theta = 0 is fully occluded, theta = PI fully open), so an undersized
    // radius degrades gracefully to "no occluder found", never to false
    // darkening. Discarding instead puts a hard AO discontinuity at
    // linearZ = worldRadius * invTanHalfFov * height / (2 * numTaps)
    // (~88 m at r.GTAO.Radius=100), far inside r.GTAO.FadeStart (500 m).
    // @ref Jimenez et al. 2016 — screen-space step budget
    screenRadius = max(screenRadius, float(numTaps));

    // Attenuation factor: inverse-square falloff from worldRadius
    float attenFactor = 2.0 / (worldRadius * worldRadius);

    float aoSum = 0.0;

    for (uint a = 0; a < numAngles; a++) {
        float angle = baseAngle + float(a) * (PI / float(numAngles));
        float2 screenDir = float2(cos(angle), sin(angle));

        float2 horizonAngles = FindMaxHorizonAngle(
            numTaps, uv, screenDir, screenRadius, stepOffset,
            viewPos, viewDir, attenFactor, texelSize);

        aoSum += IntegrateHorizonArc(horizonAngles, screenDir, viewDir, viewNormal);

        // Advance step offset for next angle (golden-ratio-like spacing)
        stepOffset = frac(stepOffset + 0.617);
    }

    // The slice mean IS the cosine-weighted visibility in [0,1]: Eq. 7 already
    // carries the 1/4 and the ||n_p|| weight. For an unoccluded plane facing the
    // camera (gamma = 0, theta = +-PI/2) it evaluates to 0.25*((1+1)+(1+1)) = 1
    // exactly, and the azimuthal mean of ||n_p||*(gamma*sin gamma + cos gamma)
    // is 1 for any plane orientation. An extra 2/PI would cap fully open
    // surfaces at 0.6366 (uniform 1.571x over-darkening).
    // @ref Jimenez et al. 2016 — Eq. 7, inner (arc) integral
    float ao = aoSum / float(numAngles);

    // With numAngles = 2 the estimator is unbiased but noisy and can overshoot
    // 1.0 on tilted surfaces (single-slice value reaches ~1.5); clamp before the
    // curve so aoPower stays a well-defined [0,1] gamma.
    ao = saturate(ao);

    // Power curve + intensity
    ao = 1.0 - (1.0 - pow(ao, aoPower)) * aoIntensity;
    ao = saturate(ao);

    // Distance fade
    ao = lerp(ao, 1.0, distFade);

    outputAO[DTid.xy] = ao;
}
