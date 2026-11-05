/// UzEngine SSR Temporal Accumulation — virtual hit reprojection + YCoCg variance clip.
/// @ref AMD 2020 "FidelityFX SSSR" — virtual hit point reprojection via prevVP matrix
/// @ref Karis 2014 "High Quality Temporal Supersampling" — variance clip
/// @ref Schied 2017 "Spatiotemporal Variance-Guided Filtering" — SVGF
/// @constants VARIANCE_CLIP_GAMMA = 1.25, VELOCITY_THRESHOLD = 0.05

#include "../common/encoding.hlsli"
#ifdef UZ_SSR_TEMPORAL_FIX
#include "../denoising/rough_specular_math.hlsli"   // ThinLensDistance / LobeHalfAngle: the motion-aware reconstruction's lens
#endif

cbuffer SsrTemporalRootCB : register(b0) {
#ifdef UZ_BINDLESS
    uint   currentSsrSrvIdx;  // 0
    uint   historySsrSrvIdx;  // 1
    uint   velocitySrvIdx;    // 2
    uint   depthSrvIdx;       // 3
#else
    uint   _srvPad0;          // 0
    uint   _srvPad1;          // 1
    uint   _srvPad2;          // 2
    uint   _srvPad3;          // 3
#endif
    uint   outputWidth;       // 4
    uint   outputHeight;      // 5
    uint   firstFrame;        // 6: 1 = no valid history
    uint   hitDistSrvIdx;     // 7: SRV index for hit distance texture
};

/// Previous frame's view-projection matrix for virtual hit reprojection.
/// Bound at b2 via kSlotPerObject CBV.
/// @ref AMD FidelityFX SSSR — prevVP needed for hit point reprojection.
cbuffer SsrTemporalPrevVP : register(b2) {
    row_major float4x4 prevViewProj;
#ifdef UZ_SSR_TEMPORAL_FIX
    // r.RayTracing.Reflections.TemporalFix (the UZ_SSR_TEMPORAL_FIX=1 permutation) only; the OFF text declares
    // the matrix alone. Mirrors the tail of C++ SsrTemporalPrevVP (frame_gi_passes.cpp).
    float4 prevCameraPos;      // xyz = the previous frame's camera position (the disocclusion test, the lobe parallax)
    uint   normalSrvIdx;       // g-buffer normal (rg = octahedral) + roughness (a)
    uint   momentsSrvIdx;      // last frame's temporal moments / guide (RGBA32F: Y mean, Y^2 mean, sample count, surface distance)
    uint   momentsUavIdx;      // this frame's
    uint   resetHistory;       // 1 = the lighting / reflection-mode epoch changed or the moments are stale: restart
    float  clipFloorK;         // r.RayTracing.Reflections.TemporalFix.ClipFloor
    float  roughnessFull;      // r.RayTracing.Reflections.TemporalFix.RoughnessFull
    uint   lensSrvIdx;         // last frame's lens guide (R32F: the smoothed inverse hit distance of the lobe)
    uint   lensUavIdx;         // this frame's
#endif
};

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4 cameraPos;
    float4 timeParams;
    float4 lightDir;
    float4 lightColor;
    float4 ambientColor;
    row_major float4x4 invViewProj;
    float  nearPlane, farPlane;
};

RWTexture2D<float4> outputSSR : register(u0);
#ifndef UZ_BINDLESS
Texture2D<float4> t_currentSsr : register(t0);
Texture2D<float4> t_historySsr : register(t1);
Texture2D<float2> t_velocity   : register(t2);
#endif
SamplerState linearClamp : register(s0);
SamplerState pointClamp  : register(s1);

static const float VELOCITY_THRESHOLD   = 0.05;   // UV space — 5% of screen ≈ 54px at 1080p
static const float VARIANCE_CLIP_GAMMA  = 1.25;   // @ref Karis 2014 — 1.25σ for reflections

/// Reconstruct world position from UV + device Z.
float3 ReconstructWorldPos(float2 uv, float deviceZ) {
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 clipPos  = float4(ndc, deviceZ, 1.0);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

/// Project a world-space point through a VP matrix → screen UV + depth.
float3 ProjectToScreenVP(float3 worldPos, float4x4 vp) {
    float4 clipPos = mul(float4(worldPos, 1.0), vp);
    clipPos.xyz /= clipPos.w;
    float2 uv = clipPos.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    return float3(uv, clipPos.z);
}

/// RGB → YCoCg conversion.
/// @ref Karis 2014 — tighter bounding volume in YCoCg than RGB.
float3 RGBToYCoCg(float3 rgb) {
    return float3(
        0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b,
        0.5 * rgb.r - 0.5 * rgb.b,
       -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b
    );
}

/// YCoCg → RGB conversion.
float3 YCoCgToRGB(float3 ycocg) {
    return float3(
        ycocg.x + ycocg.y - ycocg.z,
        ycocg.x + ycocg.z,
        ycocg.x - ycocg.y - ycocg.z
    );
}

#ifdef UZ_SSR_TEMPORAL_FIX
/// r.RayTracing.Reflections.TemporalFix — the corrected temporal filter (motion-aware lane report section 7, doc 09
/// section 8.1). Everything below runs only in the UZ_SSR_TEMPORAL_FIX=1 permutation; the default permutation is the
/// text under #else in main, unchanged.
///  (1) the velocity buffer is in pixels (velocity_ps.hlsl): converted to UV before reprojection, as taa_resolve does;
///  (2) the virtual hit is the thin-lens image of the hit point: behind the surface along the eye ray at
///      1 / (2 kappa + 1 / t) (a plane: t itself), the motion-aware reconstruction's astigmatic lens with the
///      receiver curvature from the g-buffer normals, not hitDist in front of the surface; a receiver that moved
///      itself (velocity != its camera-only motion) keeps the surface reprojection. The virtual image's offset is
///      added to the velocity pass's surface reprojection, so this pass's jittered matrices cancel out;
///  (3) the history cap follows the g-buffer roughness (normal.a) instead of the blend weight: 4 frames at the
///      mirror threshold, 32 from roughnessFull up, scaled down by the lobe's parallax since the previous frame
///      relative to its half angle (rough_specular_math MotionCap's keep, squared as the reconstruction's default
///      ParallaxPower 2 does);
///  (4) the 3x3 variance clip is widened by the sampling noise of a 9-texel mean of a signal with the pixel's own
///      temporal variance (gamma x clipFloorK x sigma_T / 3), so a stationary 1-spp signal keeps its mean instead of
///      being clamped into the dark box whenever the 3x3 window misses the bright tail; the chroma half-widths get
///      half of that floor. The moments forget at 1/4 per frame at least, so a trail of reflected content that moved
///      while the surface did not (which no camera-only reprojection models) is clamped away within a few frames.
/// A history that now actually reprojects also needs: a surface-distance disocclusion test (the guide stores the
/// distance from the camera; the previous camera position closes the test), a sample count (running mean while the
/// history is short, the EMA at the cap), a smoothed lens hit distance (the 1-spp hit distance is too noisy to focus
/// the lens) and a lighting / reflection-mode epoch reset (the C++ side), so a light step restarts the history
/// instead of trailing through it.
static const float FIX_MAX_SAMPLES        = 32.0;   // today's cap for every texel
static const float FIX_MIN_SAMPLES        = 4.0;    // the mirror threshold's cap (the comment of the OFF text)
static const float FIX_CHROMA_FLOOR       = 0.5;    // chroma half-width floor as a share of the luminance floor
static const float FIX_MOMENTS_RATE       = 0.25;   // the moments' EMA floor: a passing highlight is forgotten in ~4 frames
static const float FIX_DISOCCLUSION_CM    = 2.0;    // absolute tolerance of the distance test (+ 2 % relative)
static const float FIX_LENS_DISTANCE_TOL  = 0.25;   // the virtual fetch must land within 25 % of this surface's distance
static const float FIX_RECEIVER_MOTION_PX = 1.5;    // velocity vs camera-only motion: 1 px + the TAA jitter

struct FixReceiver {
    float3 position;
    float3 normal;
    float  roughness;
    bool   surface;
};

FixReceiver FixReceiverAt(Texture2D<float> depthTex, Texture2D<float4> normalTex, int2 p, float2 texelSize) {
    FixReceiver r;
    float  z = depthTex.Load(int3(p, 0)).r;
    float4 n = normalTex.Load(int3(p, 0));
    r.position  = ReconstructWorldPos((float2(p) + 0.5) * texelSize, z);
    r.normal    = OctDecode(n.rg);
    r.roughness = n.a;
    r.surface   = z > 0.00001;   // reversed Z: the far plane is the sky (depth_common IsSkyDepth)
    return r;
}

// Same surface: both geometry, normals within 25 degrees, within a plane tolerance of each other
// (rough_specular_common Compatible without the roughness terms).
bool FixCompatible(FixReceiver a, FixReceiver b, float tolerance) {
    if (!a.surface || !b.surface || dot(a.normal, b.normal) <= 0.9) return false;
    float3 d = a.position - b.position;
    return max(abs(dot(d, a.normal)), abs(dot(d, b.normal))) < tolerance;
}

// Curvature along one screen axis (rough_specular_reconstruct AxisCurvature): the angle between the neighbours'
// normals per world distance, convex positive (the normals diverge along the surface); one-sided when only one
// neighbour is the same surface, 0 (a plane) when none is.
float FixAxisCurvature(FixReceiver r, FixReceiver a, FixReceiver b, float tolerance) {
    bool useA = FixCompatible(r, a, tolerance), useB = FixCompatible(r, b, tolerance);
    if (!useA && !useB) return 0.0;
    float3 pa = useA ? a.position : r.position, pb = useB ? b.position : r.position;
    float3 na = useA ? a.normal : r.normal,   nb = useB ? b.normal : r.normal;
    float3 d = pa - pb;
    float len = length(d);
    if (len < 1e-4) return 0.0;
    float angle = 2.0 * asin(saturate(0.5 * length(na - nb)));
    return (dot(na - nb, d) >= 0.0 ? 1.0 : -1.0) * angle / len;
}

// Screen UV of a world point (w = 1) or a direction (w = 0) through a view-projection; false behind the camera.
bool FixProject(float4 p, float4x4 vp, out float2 uvOut) {
    float4 c = mul(p, vp);
    uvOut = 0.0;
    if (c.w <= 1e-6) return false;
    float2 ndc = c.xy / c.w;
    uvOut = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    return true;
}

void MainTemporalFix(uint3 DTid)
{
    if (DTid.x >= outputWidth || DTid.y >= outputHeight) return;

    float2 texelSize = 1.0 / float2(outputWidth, outputHeight);
    float2 uv = (float2(DTid.xy) + 0.5) * texelSize;
    int2   p  = int2(DTid.xy);

    Texture2D<float4>   currentTex  = ResourceDescriptorHeap[currentSsrSrvIdx];
    Texture2D<float4>   historyTex  = ResourceDescriptorHeap[historySsrSrvIdx];
    Texture2D<float2>   velocityTex = ResourceDescriptorHeap[velocitySrvIdx];
    Texture2D<float>    depthTex    = ResourceDescriptorHeap[depthSrvIdx];
    Texture2D<float>    hitDistTex  = ResourceDescriptorHeap[hitDistSrvIdx];
    Texture2D<float4>   normalTex   = ResourceDescriptorHeap[normalSrvIdx];
    Texture2D<float4>   momentsTex  = ResourceDescriptorHeap[momentsSrvIdx];
    RWTexture2D<float4> momentsOut  = ResourceDescriptorHeap[momentsUavIdx];
    Texture2D<float>    lensTex     = ResourceDescriptorHeap[lensSrvIdx];
    RWTexture2D<float>  lensOut     = ResourceDescriptorHeap[lensUavIdx];

    float4 currentSSR = currentTex.SampleLevel(pointClamp, uv, 0);
    FixReceiver r = FixReceiverAt(depthTex, normalTex, p, texelSize);
    float  hitDist   = hitDistTex.SampleLevel(pointClamp, uv, 0).r;
    float  invHitNow = hitDist > 0.001 ? 1.0 / hitDist : 0.0;   // the pass's 0 = no hit is an image at infinity
    // The guides every path writes: the distance next frame's disocclusion test compares against (-1 = no geometry)
    // and the lens hit distance.
    float  surfaceDist = r.surface ? length(r.position - cameraPos.xyz) : -1.0;

    // No reflection this frame (roughness cutoff, sky, no hit): zero, no history (as the OFF text).
    if (currentSSR.a <= 0.001) {
        outputSSR[p]  = float4(0, 0, 0, 0);
        momentsOut[p] = float4(0.0, 0.0, 0.0, surfaceDist);
        lensOut[p]    = invHitNow;
        return;
    }

    float3 currentYCC   = RGBToYCoCg(currentSSR.rgb);
    float  curY         = currentYCC.x;
    float4 freshMoments = float4(curY, curY * curY, 1.0, surfaceDist);

    // First frame, epoch change, stale moments: the current frame starts the history.
    if (firstFrame != 0 || resetHistory != 0) {
        outputSSR[p]  = currentSSR;
        momentsOut[p] = freshMoments;
        lensOut[p]    = invHitNow;
        return;
    }

    // ── Surface reprojection + disocclusion ───────────────────────────────
    // (1) pixels -> UV; the surface reprojection (prevUV = uv - velocity) is the velocity pass's convention.
    float2 velocityPx    = velocityTex.SampleLevel(pointClamp, uv, 0).rg;
    float2 velocityUV    = velocityPx * texelSize;
    float2 surfacePrevUV = uv - velocityUV;
    // The surface this texel shows must have been visible at its surface-reprojected texel last frame: the guide
    // there holds that texel's distance from the previous camera, compared with this surface point's distance
    // from the previous camera. Off-screen history counts as a disocclusion.
    bool   surfaceOffScreen = any(surfacePrevUV < 0.0) || any(surfacePrevUV > 1.0);
    float  guideDist    = momentsTex.SampleLevel(pointClamp, surfacePrevUV, 0).w;
    float  expectedDist = length(r.position - prevCameraPos.xyz);
    bool   disoccluded  = surfaceOffScreen || guideDist < 0.0
                        || abs(guideDist - expectedDist) > max(FIX_DISOCCLUSION_CM, 0.02 * expectedDist);
    if (disoccluded) {
        outputSSR[p]  = currentSSR;
        momentsOut[p] = freshMoments;
        lensOut[p]    = invHitNow;
        return;
    }
    // The lens hit distance: the previous frame's smoothed inverse hit distance at this surface point (the
    // surface-reprojected texel is the same surface, the test above), blended with this frame's sample below.
    float invHitLens = max(lensTex.SampleLevel(pointClamp, surfacePrevUV, 0).r, 0.0);

    // ── Virtual reprojection ──────────────────────────────────────────────
    // (2) The camera-only motion of this surface point through prevViewProj (the velocity pass's arithmetic); a
    // receiver whose velocity differs moved itself, which the virtual point does not model: it keeps the surface
    // reprojection and the OFF text's velocity-driven blend.
    float3 V        = normalize(cameraPos.xyz - r.position);
    float3 incident = -V;
    float2 surfacePrevProj;
    bool   cameraOnly = FixProject(float4(r.position, 1.0), prevViewProj, surfacePrevProj)
                     && length((uv - surfacePrevProj) / texelSize - velocityPx) <= FIX_RECEIVER_MOTION_PX;
    float2 prevUV   = surfacePrevUV;
    float  parallax = 0.0;
    if (cameraOnly) {
        // Receiver curvature from the axis neighbours (one-sided at silhouettes, a plane where none match).
        float tolerance = max(0.5, surfaceDist * 0.001);
        FixReceiver nL = FixReceiverAt(depthTex, normalTex, p + int2(-1, 0), texelSize);
        FixReceiver nR = FixReceiverAt(depthTex, normalTex, p + int2( 1, 0), texelSize);
        FixReceiver nU = FixReceiverAt(depthTex, normalTex, p + int2( 0,-1), texelSize);
        FixReceiver nD = FixReceiverAt(depthTex, normalTex, p + int2( 0, 1), texelSize);
        float kappa = max(0.0, max(FixAxisCurvature(r, nR, nL, tolerance), FixAxisCurvature(r, nD, nU, tolerance)));
        // Astigmatic thin lens (rough_specular_reconstruct): a spherical mirror at oblique incidence i images a far
        // object at (rho / 2) cos i along the plane of incidence (tangential) and (rho / 2) / cos i across it
        // (sagittal); a plane (kappa 0) has one image at t, a plane reflecting a miss the eye direction itself
        // (w = 0). Both images' previous positions are composed along / across the screen direction of the normal.
        float cosI       = max(dot(r.normal, V), 0.05);
        float tangential = ThinLensDistance(kappa / cosI, invHitLens);
        float sagittal   = ThinLensDistance(kappa * cosI, invHitLens);
        float4 tangentialPoint = tangential > 0.0 ? float4(r.position + incident * tangential, 1.0) : float4(incident, 0.0);
        float4 sagittalPoint   = sagittal   > 0.0 ? float4(r.position + incident * sagittal,   1.0) : float4(incident, 0.0);
        float2 tangentialPrev, sagittalPrev, normalTipNow;
        if (FixProject(tangentialPoint, prevViewProj, tangentialPrev) && FixProject(sagittalPoint, prevViewProj, sagittalPrev)
            && FixProject(float4(r.position + r.normal, 1.0), viewProj, normalTipNow)) {
            // The virtual image's offset from the surface point's own previous position, in pixels; added to the
            // velocity pass's surface reprojection (jitter-free), so the jitter of viewProj / prevViewProj cancels.
            float2 tangentialDelta = (tangentialPrev - surfacePrevProj) / texelSize;
            float2 sagittalDelta   = (sagittalPrev   - surfacePrevProj) / texelSize;
            float2 radial          = (normalTipNow - uv) / texelSize;
            float  radialLength    = length(radial);
            float2 virtualDelta    = tangentialDelta;
            if (radialLength > 1e-4) {
                radial /= radialLength;
                float2 across = float2(-radial.y, radial.x);
                virtualDelta = radial * dot(radial, tangentialDelta) + across * dot(across, sagittalDelta);
            }
            prevUV = surfacePrevUV + virtualDelta * texelSize;
        }
        // The lobe's rotation since the previous frame (zero for a pure camera rotation): the history cap below.
        parallax = acos(saturate(dot(normalize(prevCameraPos.xyz - r.position), V)));
        // The virtual fetch must land on this receiver (not on what lies beyond a silhouette): the guide's distance
        // there within a quarter of this surface point's; else the surface reprojection, whose box rejects what
        // does not match.
        bool offScreen = any(prevUV < 0.0) || any(prevUV > 1.0);
        float lensGuideDist = offScreen ? -1.0 : momentsTex.SampleLevel(pointClamp, prevUV, 0).w;
        if (lensGuideDist < 0.0 || abs(lensGuideDist - expectedDist) > FIX_LENS_DISTANCE_TOL * expectedDist)
            prevUV = surfacePrevUV;
    }

    // The history and its moments travel with the reflected content (both bilinear at prevUV).
    float4 historySSR = historyTex.SampleLevel(linearClamp, prevUV, 0);
    float4 moments    = momentsTex.SampleLevel(linearClamp, prevUV, 0);

    // ── 3x3 neighbourhood variance clip in YCoCg (the OFF text's box) ─────
    float3 moment1 = currentYCC;
    float3 moment2 = currentYCC * currentYCC;
    float  alphaM1 = currentSSR.a;
    float  alphaM2 = currentSSR.a * currentSSR.a;

    [unroll]
    for (int y = -1; y <= 1; y++) {
        [unroll]
        for (int x = -1; x <= 1; x++) {
            if (x == 0 && y == 0) continue;
            float2 sampleUV = uv + float2(x, y) * texelSize;
            float4 s = currentTex.SampleLevel(pointClamp, sampleUV, 0);
            float3 sYCC = RGBToYCoCg(s.rgb);
            moment1 += sYCC;
            moment2 += sYCC * sYCC;
            alphaM1 += s.a;
            alphaM2 += s.a * s.a;
        }
    }

    float3 mean   = moment1 / 9.0;
    float3 stddev = sqrt(max(moment2 / 9.0 - mean * mean, 0.0));
    float  aMean  = alphaM1 / 9.0;
    float  aStd   = sqrt(max(alphaM2 / 9.0 - aMean * aMean, 0.0));

    // (4) the box, widened by the sampling noise of a 9-texel mean of this pixel's signal (its temporal variance):
    // a stationary sparse-bright signal is no longer clamped into the dark box of a window that missed its tail.
    float  sigmaT    = sqrt(max(moments.y - moments.x * moments.x, 0.0));
    float  floorY    = VARIANCE_CLIP_GAMMA * clipFloorK * sigmaT / 3.0;
    float3 halfWidth = VARIANCE_CLIP_GAMMA * stddev + floorY * float3(1.0, FIX_CHROMA_FLOOR, FIX_CHROMA_FLOOR);
    float3 clipMin   = mean - halfWidth;
    float3 clipMax   = mean + halfWidth;
    float  aClipMin  = aMean - VARIANCE_CLIP_GAMMA * aStd;
    float  aClipMax  = aMean + VARIANCE_CLIP_GAMMA * aStd;

    float3 histYCC    = RGBToYCoCg(historySSR.rgb);
    float3 clampedYCC = clamp(histYCC, clipMin, clipMax);
    float  clampedA   = clamp(historySSR.a, aClipMin, aClipMax);
    historySSR = float4(max(YCoCgToRGB(clampedYCC), 0.0), clampedA);

    // ── Blend ─────────────────────────────────────────────────────────────
    // (3) the cap follows the surface roughness: 4 frames at the mirror threshold (a sharp reflection has nothing
    // to average and every reprojection error shows), 32 from roughnessFull up (the 1-spp lobe needs them). Under
    // camera-only motion the cap shrinks with the lobe's parallax relative to its half angle (a mirror forgets
    // within 4 frames of any orbit, a rough lobe keeps its history through a slow one); a receiver that moved
    // itself gets the OFF text's velocity-driven alpha (5 % of the screen per frame -> 0.5). While the history is
    // shorter than the cap the blend is a running mean (1 / count), so a reset reconverges in count frames.
    float cap = lerp(FIX_MIN_SAMPLES, FIX_MAX_SAMPLES, saturate(r.roughness / roughnessFull));
    float alphaCap;
    if (cameraOnly) {
        float keep = saturate(1.0 - parallax / max(LobeHalfAngle(r.roughness), 1e-4));
        keep *= keep;   // the motion-aware reconstruction's default ParallaxPower 2
        alphaCap = 1.0 / max(FIX_MIN_SAMPLES, cap * keep);
    } else {
        alphaCap = lerp(1.0 / cap, 0.5, saturate(length(velocityUV) / VELOCITY_THRESHOLD));
    }
    float count = max(moments.z, 0.0) + 1.0;
    float alpha = max(1.0 / count, alphaCap);

    outputSSR[p] = lerp(historySSR, currentSSR, alpha);
    // The moments accumulate the unclamped sample at the blend rate, 1/4 per frame at least (the floor must see the
    // signal's own spread, and must forget a trail); the stored count is the one the blend represents. The lens
    // guide follows the history's rate.
    float  beta        = max(alpha, FIX_MOMENTS_RATE);
    float2 nextMoments = lerp(moments.xy, float2(curY, curY * curY), beta);
    momentsOut[p] = float4(nextMoments, 1.0 / alpha, surfaceDist);
    lensOut[p]    = lerp(invHitLens, invHitNow, alpha);
}
#endif // UZ_SSR_TEMPORAL_FIX

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
#ifdef UZ_SSR_TEMPORAL_FIX
    MainTemporalFix(DTid);
#else
    if (DTid.x >= outputWidth || DTid.y >= outputHeight) return;

    float2 texelSize = 1.0 / float2(outputWidth, outputHeight);
    float2 uv = (float2(DTid.xy) + 0.5) * texelSize;

#ifdef UZ_BINDLESS
    Texture2D<float4> currentTex  = ResourceDescriptorHeap[currentSsrSrvIdx];
    Texture2D<float4> historyTex  = ResourceDescriptorHeap[historySsrSrvIdx];
    Texture2D<float2> velocityTex = ResourceDescriptorHeap[velocitySrvIdx];
    Texture2D<float>  hitDistTex  = ResourceDescriptorHeap[hitDistSrvIdx];
#else
    Texture2D<float4> currentTex  = t_currentSsr;
    Texture2D<float4> historyTex  = t_historySsr;
    Texture2D<float2> velocityTex = t_velocity;
    Texture2D<float>  hitDistTex  = ResourceDescriptorHeap[hitDistSrvIdx]; // always bindless
#endif

    float4 currentSSR = currentTex.SampleLevel(pointClamp, uv, 0);

    // If current frame has no SSR data (roughness > MaxRoughness, sky, no hit),
    // output zero immediately — don't let stale history bleed through.
    if (currentSSR.a <= 0.001) {
        outputSSR[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    // First frame or no history — use current directly
    if (firstFrame != 0) {
        outputSSR[DTid.xy] = currentSSR;
        return;
    }

    // ── Virtual hit reprojection ────────────────────────────────────────
    // @ref AMD FidelityFX SSSR — reproject via hit point, not surface motion vectors.
    // Surface motion vectors move the reflected image incorrectly because the
    // reflection content moves with the reflected geometry, not the surface.
    //
    // Algorithm:
    // 1. Read hit distance from trace pass
    // 2. Reconstruct surface world pos from depth + invVP
    // 3. Compute reflection direction R = normalize(hitWorldPos - surfaceWorldPos)
    // 4. Reproject the hit world pos through prevVP to get previous frame UV
    // 5. Sample history at that UV
    //
    // Fallback: if hit distance is 0 (no hit), fall back to surface velocity.

    float hitDist = hitDistTex.SampleLevel(pointClamp, uv, 0).r;
    float2 prevUV;

    if (hitDist > 0.001) {
        // Virtual reprojection: reproject the world-space hit point
        // Read depth for surface world pos reconstruction
        Texture2D<float> depthTex = ResourceDescriptorHeap[depthSrvIdx];
        float deviceZ = depthTex.SampleLevel(pointClamp, uv, 0).r;
        float3 surfaceWorldPos = ReconstructWorldPos(uv, deviceZ);

        // Read normal for reflection direction
        // (We don't have normal SRV in this pass, so use the hit distance directly)
        // Reconstruct hit world position from the current frame's trace:
        // hitWorldPos = surfaceWorldPos + R * hitDist
        // But we don't have R here. Instead, use the screen-space hit UV from the trace
        // to reconstruct the hit world pos directly from the depth buffer at the hit location.
        //
        // Alternative (AMD SSSR pattern): store hit world pos or use per-pixel velocity.
        // We approximate by using the hit distance + view direction to estimate the hit point.
        float3 V = normalize(cameraPos.xyz - surfaceWorldPos);
        float3 N = float3(0, 0, 1); // Approximate — actual normal would be better

        // For virtual reprojection, we need the actual hit world position.
        // The hit distance is in world-space units from surface to hit.
        // Since we don't store the ray direction, use the reflection of V around
        // the approximate normal. For better accuracy, we'd store the hit UV.
        // For now, use the surface velocity as fallback + blend with virtual reproject.
        float2 surfaceVelocity = velocityTex.SampleLevel(pointClamp, uv, 0).rg;

        // Virtual reprojection: project the estimated hit point through prevVP.
        // The hit point moves with the world geometry it reflects, not with the surface.
        // Use surface position + hitDist along view direction as an approximation.
        // This works well when the camera moves (which is the main cause of temporal noise).
        float3 hitWorldApprox = surfaceWorldPos + V * hitDist;
        float3 prevHitScreen = ProjectToScreenVP(hitWorldApprox, prevViewProj);
        float2 virtualPrevUV = prevHitScreen.xy;

        // Blend virtual and surface reprojection based on hit distance confidence.
        // Short hits = more confident virtual reproject, long hits = fall back to surface.
        float virtualConfidence = saturate(1.0 - hitDist / 5000.0);
        prevUV = lerp(uv - surfaceVelocity, virtualPrevUV, virtualConfidence);
    } else {
        // No hit — use surface velocity
        float2 velocity = velocityTex.SampleLevel(pointClamp, uv, 0).rg;
        prevUV = uv - velocity;
    }

    // Disocclusion: outside screen → use current
    if (any(prevUV < 0.0) || any(prevUV > 1.0)) {
        outputSSR[DTid.xy] = currentSSR;
        return;
    }

    // ── 3×3 neighbourhood variance clip in YCoCg color space ──────────
    // @ref Karis 2014 — variance clip for tighter bounding volume
    // @ref Schied 2017 SVGF — variance-based history rejection

    float3 currentYCC = RGBToYCoCg(currentSSR.rgb);

    float3 moment1 = currentYCC;
    float3 moment2 = currentYCC * currentYCC;
    float  alphaM1 = currentSSR.a;
    float  alphaM2 = currentSSR.a * currentSSR.a;

    [unroll]
    for (int y = -1; y <= 1; y++) {
        [unroll]
        for (int x = -1; x <= 1; x++) {
            if (x == 0 && y == 0) continue;
            float2 sampleUV = uv + float2(x, y) * texelSize;
            float4 s = currentTex.SampleLevel(pointClamp, sampleUV, 0);
            float3 sYCC = RGBToYCoCg(s.rgb);
            moment1 += sYCC;
            moment2 += sYCC * sYCC;
            alphaM1 += s.a;
            alphaM2 += s.a * s.a;
        }
    }

    float3 mean   = moment1 / 9.0;
    float3 stddev = sqrt(max(moment2 / 9.0 - mean * mean, 0.0));
    float  aMean  = alphaM1 / 9.0;
    float  aStd   = sqrt(max(alphaM2 / 9.0 - aMean * aMean, 0.0));

    float3 clipMin = mean - VARIANCE_CLIP_GAMMA * stddev;
    float3 clipMax = mean + VARIANCE_CLIP_GAMMA * stddev;
    float  aClipMin = aMean - VARIANCE_CLIP_GAMMA * aStd;
    float  aClipMax = aMean + VARIANCE_CLIP_GAMMA * aStd;

    // Sample history and clamp in YCoCg space
    float4 historySSR = historyTex.SampleLevel(linearClamp, prevUV, 0);
    float3 histYCC = RGBToYCoCg(historySSR.rgb);
    float3 clampedYCC = clamp(histYCC, clipMin, clipMax);
    float  clampedA   = clamp(historySSR.a, aClipMin, aClipMax);

    // YCoCg → RGB
    float3 clampedRGB = YCoCgToRGB(clampedYCC);
    historySSR = float4(max(clampedRGB, 0.0), clampedA);

    // ── Adaptive blend weight ──────────────────────────────────────────
    // @ref AMD FidelityFX SSSR — roughness-driven temporal accumulation.
    // Smooth surfaces: fast response (alpha ~0.2, few samples needed).
    // Rough surfaces: slower accumulation (alpha ~0.05, needs many samples to converge).
    float roughnessProxy = saturate(currentSSR.a);
    float targetSamples = lerp(4.0, 32.0, roughnessProxy * roughnessProxy);
    float baseAlpha = 1.0 / targetSamples;

    // Velocity-based blend: faster motion → more current frame
    float2 velocity = velocityTex.SampleLevel(pointClamp, uv, 0).rg;
    float velMag = length(velocity);
    float alpha = lerp(baseAlpha, 0.5, saturate(velMag / VELOCITY_THRESHOLD));

    outputSSR[DTid.xy] = lerp(historySSR, currentSSR, alpha);
#endif // UZ_SSR_TEMPORAL_FIX
}
