#include "rough_specular_common.hlsli"
#include "rough_specular_math.hlsli"
#ifdef UZ_ROUGH_MOTION_AWARE
#include "../common/velocity_common.hlsli"

// Motion-aware reconstruction (docs/analysis/lumen-radiance-cache-stability/09): the history is
// reprojected by the reflection's curvature-corrected virtual point, the cap follows the lobe's
// parallax, a short history clamps fireflies against the current neighbourhood and the spatial
// support is gated by curvature. Nothing in this block is reachable from the OFF permutation.
float InverseHitDistance(float hitDistance) {
    // The pass's "0 = no hit" convention maps to 1 / t = 0: an image at infinity.
    return hitDistance > 0.0 ? 1.0 / hitDistance : 0.0;
}

// Render-pixel position of a clip-space point: the velocity pass's convention (velocity_ps.hlsl).
float2 ClipToPixels(float4 clip) {
    return NdcToRenderPixels(clip.xy / clip.w, float2(width, height));
}

// Curvature along one screen axis: the angle between the neighbours' normals per world distance, convex
// positive (the normals diverge along the surface); one-sided when only one neighbour is the same
// surface, -1 when none is. worldStep receives the per-pixel world step along the axis.
float AxisCurvature(RoughReceiver r, RoughReceiver a, RoughReceiver b, out float worldStep) {
    bool useA = Compatible(r, a), useB = Compatible(r, b);
    worldStep = 0.0;
    if (!useA && !useB) return -1.0;
    float3 pa = useA ? a.position : r.position, pb = useB ? b.position : r.position;
    float3 na = useA ? a.normal : r.normal, nb = useB ? b.normal : r.normal;
    float3 d = pa - pb;
    float len = length(d);
    worldStep = len / ((useA && useB) ? 2.0 : 1.0);
    if (len < 1e-4) return -1.0;
    float angle = 2.0 * asin(saturate(0.5 * length(na - nb)));
    return (dot(na - nb, d) >= 0.0 ? 1.0 : -1.0) * angle / len;
}

// Receiver without the hit-distance load: curvature and the firefly reference never read it.
RoughReceiver GeometryReceiver(int2 p) {
    Texture2D<float> depth = ResourceDescriptorHeap[depthSrv];
    Texture2D<float4> normal = ResourceDescriptorHeap[normalSrv];
    float z = depth.Load(int3(p, 0));
    float4 n = normal.Load(int3(p, 0));
    RoughReceiver r;
    r.position = ReconstructWorldPos((float2(p) + 0.5) / float2(width, height), z, invViewProj);
    r.normal = OctDecode(n.rg);
    r.roughness = n.a;
    r.hitDistance = 0.0;
    r.linearDepth = LinearizeDepthReversedZ(z, nearPlane, farPlane);
    r.surface = !IsSkyDepth(z);
    return r;
}

// Luminance of a unit-stride neighbour for the firefly reference: the centre's when the tap is off
// screen, unwritten or another surface.
float NeighbourLuminance(Texture2D<float4> input, RoughReceiver r, RoughReceiver s, int2 q, float fallback) {
    if (!InBounds(q) || !Compatible(r, s)) return fallback;
    float4 c = input.Load(int3(q, 0));
    return c.a > 0.0 ? Luminance(c.rgb) : fallback;
}

// The diagonal taps of the firefly reference: one normal / roughness load decides the surface test (no
// plane distance), which is what a median of nine needs from its corners.
float DiagonalLuminance(Texture2D<float4> input, RoughReceiver r, int2 q, float fallback) {
    if (!InBounds(q)) return fallback;
    Texture2D<float4> normal = ResourceDescriptorHeap[normalSrv];
    float4 n = normal.Load(int3(q, 0));
    if (n.a <= 0.04 || abs(n.a - r.roughness) >= 0.05 || dot(OctDecode(n.rg), r.normal) <= 0.9) return fallback;
    float4 c = input.Load(int3(q, 0));
    return c.a > 0.0 ? Luminance(c.rgb) : fallback;
}

#define UZ_MA_SORT(a, b) { float lo = min(v[a], v[b]); v[b] = max(v[a], v[b]); v[a] = lo; }
// Median of nine (the 19-exchange network): robust to up to four fireflies in the 3x3.
float Median9(float v[9]) {
    UZ_MA_SORT(1, 2) UZ_MA_SORT(4, 5) UZ_MA_SORT(7, 8) UZ_MA_SORT(0, 1) UZ_MA_SORT(3, 4) UZ_MA_SORT(6, 7)
    UZ_MA_SORT(1, 2) UZ_MA_SORT(4, 5) UZ_MA_SORT(7, 8) UZ_MA_SORT(0, 3) UZ_MA_SORT(5, 8) UZ_MA_SORT(4, 7)
    UZ_MA_SORT(3, 6) UZ_MA_SORT(1, 4) UZ_MA_SORT(2, 5) UZ_MA_SORT(4, 7) UZ_MA_SORT(4, 2) UZ_MA_SORT(6, 4)
    UZ_MA_SORT(4, 2)
    return v[4];
}
#undef UZ_MA_SORT

// Today's history fetch: four bilinear taps, each accepted when its producer wrote it and its stored
// geometry is the receiver's surface, plus the stored lobe inverse hit distance (guides.y) alongside.
// Returns the accepted weight; the outputs are weighted sums.
float FetchHistory(float2 previousPixel, RoughReceiver r, out float4 old, out float4 oldMoments,
                   out float oldInverseHit) {
    Texture2D<float4> history = ResourceDescriptorHeap[historySrv];
    Texture2D<float4> moments = ResourceDescriptorHeap[momentsSrv];
    Texture2D<float4> geometry = ResourceDescriptorHeap[geometrySrv];
    Texture2D<float4> guides = ResourceDescriptorHeap[guidesSrv];
    old = 0.0; oldMoments = 0.0; oldInverseHit = 0.0;
    float accepted = 0.0;
    if (historyValid == 0 || !all(isfinite(previousPixel))) return 0.0;
    int2 base = int2(floor(previousPixel));
    float2 fraction = frac(previousPixel);
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
        int2 q = base + int2(x, y);
        if (!InBounds(q)) continue;
        float4 g = geometry.Load(int3(q, 0));
        float4 properties = guides.Load(int3(q, 0));
        RoughReceiver s;
        s.position = g.xyz;
        s.normal = UnpackNormal(max(g.w, 0.0));
        s.surface = g.w >= 0.0;
        s.roughness = properties.x;
        s.hitDistance = 0.0; // guides.y is the lobe inverse hit distance here; Compatible does not read it
        s.linearDepth = properties.z;
        float4 h = history.Load(int3(q, 0));
        if (h.a <= 0.0 || !Compatible(r, s)) continue;
        float w = (x == 0 ? 1.0 - fraction.x : fraction.x)
                * (y == 0 ? 1.0 - fraction.y : fraction.y);
        old += h * w;
        oldMoments += moments.Load(int3(q, 0)) * w;
        oldInverseHit += properties.y * w;
        accepted += w;
    }
    return accepted;
}
#endif

// Reuse the existing normalized, VNDF-throughput-weighted reflection
// signal before lighting/TAA. Spatial reuse is normalized and never recursive.
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 p = int2(id.xy);
    if (!InBounds(p)) return;
    Texture2D<float4> input = ResourceDescriptorHeap[inputSrv];
    Texture2D<float2> velocity = ResourceDescriptorHeap[velocitySrv];
    Texture2D<float4> history = ResourceDescriptorHeap[historySrv];
    Texture2D<float4> moments = ResourceDescriptorHeap[momentsSrv];
    Texture2D<float4> geometry = ResourceDescriptorHeap[geometrySrv];
    Texture2D<float4> guides = ResourceDescriptorHeap[guidesSrv];
    RWTexture2D<float4> colorOut = ResourceDescriptorHeap[colorUav];
    RWTexture2D<float4> momentsOut = ResourceDescriptorHeap[momentsUav];
    RWTexture2D<float4> geometryOut = ResourceDescriptorHeap[geometryUav];
    RWTexture2D<float4> guidesOut = ResourceDescriptorHeap[guidesUav];
    RoughReceiver r = Receiver(p);
    float4 current = input.Load(int3(p, 0));
    geometryOut[p] = float4(r.position, r.surface ? PackNormal(r.normal) : -1.0);
#ifndef UZ_ROUGH_MOTION_AWARE
    guidesOut[p] = float4(r.roughness, r.hitDistance, r.linearDepth, current.a);
#endif
    if (!r.surface || !Reconstructs(r.roughness) || current.a <= 0.0) {
#ifdef UZ_ROUGH_MOTION_AWARE
        // guides.y carries the lobe's inverse hit distance under the permutation (section 4.6).
        guidesOut[p] = float4(r.roughness, InverseHitDistance(r.hitDistance), r.linearDepth, current.a);
#endif
        colorOut[p] = 0.0;
        momentsOut[p] = 0.0;
        return;
    }

#ifdef UZ_ROUGH_MOTION_AWARE
    // 1. Axis neighbours: receiver curvature and the world step per pixel (4.2.3, 4.5). Concave
    //    receivers are treated as planes.
    RoughReceiver nL = GeometryReceiver(p + int2(-1, 0)), nR = GeometryReceiver(p + int2(1, 0));
    RoughReceiver nU = GeometryReceiver(p + int2(0, -1)), nD = GeometryReceiver(p + int2(0, 1));
    float stepX, stepY;
    float kappaX = AxisCurvature(r, nR, nL, stepX);
    float kappaY = AxisCurvature(r, nD, nU, stepY);
    float kappa = max(0.0, max(kappaX, kappaY));
    float worldStep = max(stepX, stepY);

    // 2. Firefly reference from the CURRENT neighbourhood (4.4): the 3x3 median luminance (unconditional:
    //    computing it only on short histories measured slower on Sponza, the branch diverges).
    float centreL = Luminance(current.rgb);
    float v[9];
    v[0] = DiagonalLuminance(input, r, p + int2(-1, -1), centreL);
    v[1] = NeighbourLuminance(input, r, nU, p + int2(0, -1), centreL);
    v[2] = DiagonalLuminance(input, r, p + int2(1, -1), centreL);
    v[3] = NeighbourLuminance(input, r, nL, p + int2(-1, 0), centreL);
    v[4] = centreL;
    v[5] = NeighbourLuminance(input, r, nR, p + int2(1, 0), centreL);
    v[6] = DiagonalLuminance(input, r, p + int2(-1, 1), centreL);
    v[7] = NeighbourLuminance(input, r, nD, p + int2(0, 1), centreL);
    v[8] = DiagonalLuminance(input, r, p + int2(1, 1), centreL);
    float neighbourhoodL = Median9(v);

    // 3. Virtual reflected-ray reprojection (4.2). Motion is in render pixels; the TAA lattice motion is
    //    removed exactly as on the surface path.
    float2 motion = velocity.Load(int3(p, 0));
    float motionLength = length(motion);
    float2 surfacePixel = float2(p) + previousJitter - currentJitter - motion;
    float3 incident = normalize(r.position - cameraPos.xyz);
    // Object-motion guard (4.2.7): the camera-only motion of this surface point, the velocity pass's
    // arithmetic; a receiver whose velocity differs moved itself, which the virtual point does not model.
    float4 surfaceNow = mul(float4(r.position, 1.0), unjitteredViewProj);
    float4 surfaceBefore = mul(float4(r.position, 1.0), prevViewProj);
    bool cameraOnly = surfaceNow.w > 0.0 && surfaceBefore.w > 0.0
        && length(motion - (ClipToPixels(surfaceNow) - ClipToPixels(surfaceBefore))) <= 1.0;
    // Lobe inverse hit distance for the lens (4.6.4): the previous frame's smoothed value at this surface
    // point when the surface-reprojected texel is the same surface, else this pixel's own sample.
    float lensInverseHit = InverseHitDistance(r.hitDistance);
    if (historyValid != 0 && all(isfinite(surfacePixel))) {
        int2 q = int2(round(surfacePixel));
        if (InBounds(q)) {
            float4 g = geometry.Load(int3(q, 0));
            float4 properties = guides.Load(int3(q, 0));
            RoughReceiver s;
            s.position = g.xyz;
            s.normal = UnpackNormal(max(g.w, 0.0));
            s.surface = g.w >= 0.0;
            s.roughness = properties.x;
            s.hitDistance = 0.0;
            s.linearDepth = properties.z;
            if (properties.w > 0.0 && Compatible(r, s)) lensInverseHit = properties.y;
        }
    }
    bool virtualPath = false;
    float2 previousPixel = surfacePixel;
    float2 used = motion;
    // Thin lens (4.2.4), astigmatic: a spherical mirror at oblique incidence i images a far object at
    // (rho / 2) cos i along the plane of incidence (tangential) and at (rho / 2) / cos i across it
    // (sagittal); the paraxial rho / 2 of the spec under-moves the reflection by 0.15-0.3 px/frame at
    // the report's window highlight (incidence 25-35 degrees), which a 60-frame history turns into a
    // trail. Both images are projected and the motion composed along / across the screen direction of
    // the normal; a plane (kappa 0) has one image at t, a plane reflecting a miss the eye direction
    // itself (w = 0). The tangential distance is the one the diagnostics report.
    float cosIncidence = max(dot(r.normal, -incident), 0.05);
    float imageDistance = ThinLensDistance(kappa / cosIncidence, lensInverseHit);
    float sagittalDistance = ThinLensDistance(kappa * cosIncidence, lensInverseHit);
    if (cameraOnly) {
        float4 tangentialPoint = imageDistance > 0.0
            ? float4(r.position + incident * imageDistance, 1.0) : float4(incident, 0.0);
        float4 sagittalPoint = sagittalDistance > 0.0
            ? float4(r.position + incident * sagittalDistance, 1.0) : float4(incident, 0.0);
        float4 tangentialNow = mul(tangentialPoint, unjitteredViewProj);
        float4 tangentialBefore = mul(tangentialPoint, prevViewProj);
        float4 sagittalNow = mul(sagittalPoint, unjitteredViewProj);
        float4 sagittalBefore = mul(sagittalPoint, prevViewProj);
        float4 normalTip = mul(float4(r.position + r.normal, 1.0), unjitteredViewProj);
        if (tangentialNow.w > 0.0 && tangentialBefore.w > 0.0 && sagittalNow.w > 0.0 && sagittalBefore.w > 0.0
            && normalTip.w > 0.0) {
            float2 tangentialMotion = ClipToPixels(tangentialNow) - ClipToPixels(tangentialBefore);
            float2 sagittalMotion = ClipToPixels(sagittalNow) - ClipToPixels(sagittalBefore);
            // Screen direction of the plane of incidence: the projected normal (zero at normal incidence,
            // where the two images coincide).
            float2 radial = ClipToPixels(normalTip) - ClipToPixels(surfaceNow);
            float radialLength = length(radial);
            float2 virtualMotion = tangentialMotion;
            if (radialLength > 1e-4) {
                radial /= radialLength;
                float2 across = float2(-radial.y, radial.x);
                virtualMotion = radial * dot(radial, tangentialMotion) + across * dot(across, sagittalMotion);
            }
            // The lobe's dominant direction tilts toward the normal as it widens (nrd_reproject_cs), but a
            // far reflection (a miss, a hit far beyond the receiver) moves like its virtual image at every
            // roughness: measured on the report's sphere, the plain roughness^2 share of the surface motion
            // is a 0.3 px/frame bias that a 60-frame history turns into a 20-40 px trail. The surface share
            // is therefore scaled by how near-field the lobe is (its hit distance against the receiver depth).
            float nearField = r.linearDepth * lensInverseHit / (1.0 + r.linearDepth * lensInverseHit);
            used = lerp(virtualMotion, motion, r.roughness * r.roughness * nearField);
            previousPixel = float2(p) + previousJitter - currentJitter - used;
            virtualPath = true;
        }
    }
    float4 old, oldMoments;
    float oldInverseHit;
    float accepted = FetchHistory(previousPixel, r, old, oldMoments, oldInverseHit);
    bool trusted = virtualPath && accepted >= 0.5;
    if (virtualPath && accepted < 0.5) {
        // The reflection crossed onto another surface or left the screen: today's surface history, untrusted.
        accepted = FetchHistory(surfacePixel, r, old, oldMoments, oldInverseHit);
    }
    if (accepted > 0.00001) {
        old /= accepted;
        oldMoments /= accepted;
        oldInverseHit /= accepted;
    }
    // 4. Short-history firefly limit (4.4): referenced to the current neighbourhood and the fetched history,
    //    never to the carried moments; off once the history is longer than shortHistory.
    float historyCount = accepted > 0.00001 ? old.a : 0.0;
    float reference = max(accepted > 0.00001 ? Luminance(old.rgb) : 0.0, neighbourhoodL);
    float limit = FireflyLimit(reference, historyCount, fireflyK, shortHistory);

    // 5. Spatial estimate with the firefly limit on every tap and the lobe inverse hit distance (4.2.2);
    //    the support is curvature-gated (4.5) and never widened by the history length.
    float3 sum = 0.0;
    float first = 0.0, second = 0.0, weights = 0.0, squaredWeights = 0.0;
    float inverseHit = 0.0, inverseHitWeights = 0.0;
    float radius = SpatialRadius(r.roughness, r.hitDistance, r.linearDepth);
    if (curvatureGate > 0.5) radius = max(1.0, min(radius, CurvatureRadiusLimit(r.roughness, kappa, worldStep)));
    [loop]
    for (int y = -2; y <= 2; ++y) {
        [loop]
        for (int x = -2; x <= 2; ++x) {
            int2 q = p + int2(round(float2(x, y) * radius));
            if (!InBounds(q)) continue;
            float4 c = input.Load(int3(q, 0));
            RoughReceiver s = Receiver(q);
            // Valid black (e.g. below-horizon VNDF draws) participates in the mean.
            if (c.a <= 0.0 || !Compatible(r, s)) continue;
            float hitScale = max(1.0, max(r.hitDistance, s.hitDistance));
            float hitWeight = exp(-abs(r.hitDistance - s.hitDistance) / hitScale
                                  * (1.0 - r.roughness));
            float w = exp(-float(x*x + y*y) * 0.5)
                * pow(saturate(dot(r.normal, s.normal)), NormalPower(r.roughness))
                * exp(-PlaneDistance(r, s) / PlaneTolerance(r))
                * exp(-abs(r.roughness - s.roughness) * 32.0) * hitWeight;
            float l = Luminance(c.rgb);
            if (l > limit) { c.rgb *= limit / l; l = limit; } // hue-preserving
            sum += c.rgb * w;
            first += l * w;
            second += l * l * w;
            weights += w;
            squaredWeights += w * w;
            // A miss is an image at infinity (1 / t = 0); the zero-throughput black (rgb 0, t 0) says
            // nothing about where the lobe lands.
            if (s.hitDistance > 0.0 || any(c.rgb > 0.0)) {
                inverseHit += w * InverseHitDistance(s.hitDistance);
                inverseHitWeights += w;
            }
        }
    }
    float3 estimate = sum / max(weights, 0.00001);
    first /= max(weights, 0.00001);
    second /= max(weights, 0.00001);
    float effectiveSamples = weights * weights / max(squaredWeights, 0.00001);
    float spatialVariance = max(second - first * first, 0.0) / max(effectiveSamples, 1.0);
    float meanInverseHit = inverseHitWeights > 0.00001
        ? inverseHit / inverseHitWeights : InverseHitDistance(r.hitDistance);

    // 6. Cap and clamp (4.3): a trusted virtual history gets the parallax-driven cap and the static
    //    clamp; anything else keeps today's three motion rules.
    float count = 1.0;
    float2 updated = float2(first, second);
    float fast = first;
    float fastCount = 1.0;
    float storedInverseHit = meanInverseHit;
    if (accepted > 0.00001) {
        float clampMotion = trusted ? 0.0 : motionLength;
        float cap = HistoryCap(r.roughness, motionLength);
        if (trusted) {
            float3 reflected = reflect(incident, r.normal);
            float3 reflectedBefore = reflect(normalize(r.position - prevCameraPos.xyz), r.normal);
            float parallax = 2.0 * asin(saturate(0.5 * length(reflected - reflectedBefore)));
            cap = MotionCap(r.roughness, parallax, parallaxPower);
        }
        fastCount = min(oldMoments.w + 1.0, clampMotion > 0.5 ? 2.0 : 8.0);
        fast = lerp(oldMoments.z, first, 1.0 / fastCount);
        float variance = max(spatialVariance,
            max(oldMoments.y - oldMoments.x * oldMoments.x, 0.0) / max(effectiveSamples, 1.0));
        float oldL = Luminance(old.rgb);
        float clamped = ClampHistory(oldL, fast, variance / fastCount, clampMotion);
        if (oldL > 0.0) old.rgb *= clamped / oldL;
        count = min(old.a + 1.0, cap);
        float alpha = 1.0 / count;
        estimate = lerp(old.rgb, estimate, alpha);
        updated = lerp(oldMoments.xy, float2(first, second), alpha);
        // The lens depth follows the lobe within ~8 frames (the fast mean's horizon), not the full cap.
        storedInverseHit = lerp(oldInverseHit, meanInverseHit, max(alpha, 0.125));
    }
    // Diagnostics (r.RayTracing.Reflections.RoughReconstruct.MotionAware.Debug): the reconstruction's
    // decisions in place of the colour. 1 = (trusted, camera-only, accepted weight); 2 = (curvature x 100,
    // lens image distance / 100 cm, lobe 1/t x 1000); 3 = (used - surface motion + 4 px, surface motion x + 8 px).
    if (debugMode > 0.5) {
        estimate = debugMode < 1.5 ? float3(trusted ? 1.0 : 0.0, cameraOnly ? 1.0 : 0.0, accepted)
                 : debugMode < 2.5 ? float3(kappa * 100.0, max(imageDistance, 0.0) * 0.01, lensInverseHit * 1000.0)
                 : float3(used - motion + 4.0, motion.x + 8.0);
    }
    colorOut[p] = float4(estimate, count);
    momentsOut[p] = float4(updated, fast, fastCount);
    guidesOut[p] = float4(r.roughness, storedInverseHit, r.linearDepth, current.a);
#else
    float3 sum = 0.0;
    float first = 0.0, second = 0.0, weights = 0.0, squaredWeights = 0.0;
    float radius = SpatialRadius(r.roughness, r.hitDistance, r.linearDepth);
    [loop]
    for (int y = -2; y <= 2; ++y) {
        [loop]
        for (int x = -2; x <= 2; ++x) {
            int2 q = p + int2(round(float2(x, y) * radius));
            if (!InBounds(q)) continue;
            float4 c = input.Load(int3(q, 0));
            RoughReceiver s = Receiver(q);
            // Valid black (e.g. below-horizon VNDF draws) participates in the mean.
            if (c.a <= 0.0 || !Compatible(r, s)) continue;
            float hitScale = max(1.0, max(r.hitDistance, s.hitDistance));
            float hitWeight = exp(-abs(r.hitDistance - s.hitDistance) / hitScale
                                  * (1.0 - r.roughness));
            float w = exp(-float(x*x + y*y) * 0.5)
                * pow(saturate(dot(r.normal, s.normal)), NormalPower(r.roughness))
                * exp(-PlaneDistance(r, s) / PlaneTolerance(r))
                * exp(-abs(r.roughness - s.roughness) * 32.0) * hitWeight;
            float l = Luminance(c.rgb);
            sum += c.rgb * w;
            first += l * w;
            second += l * l * w;
            weights += w;
            squaredWeights += w * w;
        }
    }
    float3 estimate = sum / max(weights, 0.00001);
    first /= max(weights, 0.00001);
    second /= max(weights, 0.00001);
    float effectiveSamples = weights * weights / max(squaredWeights, 0.00001);
    float spatialVariance = max(second - first * first, 0.0) / max(effectiveSamples, 1.0);

    float2 motion = velocity.Load(int3(p, 0));
    float motionLength = length(motion);
    // Motion is in render pixels. Remove TAA lattice motion before reusing taps.
    float2 previousPixel = float2(p) + previousJitter - currentJitter - motion;
    int2 base = int2(floor(previousPixel));
    float2 fraction = frac(previousPixel);
    float4 old = 0.0, oldMoments = 0.0;
    float accepted = 0.0;
    if (historyValid != 0 && all(isfinite(previousPixel))) {
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
            int2 q = base + int2(x, y);
            if (!InBounds(q)) continue;
            float4 g = geometry.Load(int3(q, 0));
            float4 properties = guides.Load(int3(q, 0));
            RoughReceiver s;
            s.position = g.xyz;
            s.normal = UnpackNormal(max(g.w, 0.0));
            s.surface = g.w >= 0.0;
            s.roughness = properties.x;
            s.hitDistance = properties.y;
            s.linearDepth = properties.z;
            float4 h = history.Load(int3(q, 0));
            if (h.a <= 0.0 || !Compatible(r, s)) continue;
            float w = (x == 0 ? 1.0 - fraction.x : fraction.x)
                    * (y == 0 ? 1.0 - fraction.y : fraction.y);
            old += h * w;
            oldMoments += moments.Load(int3(q, 0)) * w;
            accepted += w;
        }
    }
    float count = 1.0;
    float2 updated = float2(first, second);
    float fast = first;
    float fastCount = 1.0;
    if (accepted > 0.00001) {
        old /= accepted;
        oldMoments /= accepted;
        fastCount = min(oldMoments.w + 1.0, motionLength > 0.5 ? 2.0 : 8.0);
        fast = lerp(oldMoments.z, first, 1.0 / fastCount);
        // Historical estimator variance, reduced by spatial effective samples,
        // stops a quiet instantaneous neighborhood from clipping a converged lobe.
        float variance = max(spatialVariance,
            max(oldMoments.y - oldMoments.x * oldMoments.x, 0.0) / max(effectiveSamples, 1.0));
        float oldL = Luminance(old.rgb);
        // Clamp against uncertainty of the fast MEAN, not variance of individual
        // estimates. Otherwise a converging producer's old dark history remains
        // inside an unnecessarily wide interval for dozens of frames.
        float clamped = ClampHistory(oldL, fast, variance / fastCount, motionLength);
        if (oldL > 0.0) old.rgb *= clamped / oldL;
        count = min(old.a + 1.0, HistoryCap(r.roughness, motionLength));
        float alpha = 1.0 / count;
        estimate = lerp(old.rgb, estimate, alpha);
        updated = lerp(oldMoments.xy, float2(first, second), alpha);
    }
    colorOut[p] = float4(estimate, count);
    momentsOut[p] = float4(updated, fast, fastCount);
#endif
}
