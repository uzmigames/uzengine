/// Screen-space ReSTIR GI for the surface final gather (Lumen-parity phase C, stage 1).
/// @ref Ouyang et al. 2021 "ReSTIR GI: Path Resampling for Real-Time Path Tracing".
/// @ref Lin et al. 2022 "Generalized Resampled Importance Sampling", Eqs. 4, 7, 22.
/// @ref Bitterli et al. 2020 "Spatiotemporal Reservoir Resampling", pairwise MIS (supplemental).
/// @ref Wright et al. 2022 "Lumen", surface final gather backed by a world radiance cache.
///
/// The real-time gather (ssgi_trace_cs, `sdfDiffuseSamples != 0`) traces N cosine rays per
/// half-resolution pixel against the global distance field and averages their radiance: E/pi.
/// Here the same N rays become the candidates of a per-pixel reservoir whose sample is the ray's
/// DIRECTION, kept together with the radiance that direction returned from the reservoir's own
/// surface. The domain of every reservoir is the gather's own: the hemisphere of its surface in
/// solid angle, cosine density cos/pi, integrand L(omega) cos/pi with L the gather's per-ray
/// function (TraceSdfSurfaceRadiance: the field trace, the hit shading, the sky read).
/// Reuse replays a direction at another surface by tracing it from there — the shift map is the
/// identity in direction space, Jacobian 1, and the evaluation is exactly what the plain gather
/// would have returned for that ray. (A hit POINT is not reusable with this tracer: it registers
/// hits on a distance-dependent expanded surface, so a neighbour's hit is not a point the
/// receiver's own rays can produce, and reconnection to it was measured biased by the footprint.)
/// Targets are lum(L) * cos/pi at EVERY source domain, generalized balance MIS in the temporal
/// pass and pairwise MIS in the spatial pass (2K evaluations instead of K(K+1)). The spatial pass
/// is itself opt-in (`r.GI.ReSTIR.Spatial`, default 0): replaying a neighbour's direction is
/// unbiased but, once the temporal reservoir has converged, noisier than not reusing it.
/// Opt-in: `r.GI.ReSTIR` (default 0); OFF dispatches the unchanged ssgi_trace_cs.
#ifndef UZ_SSGI_RESTIR_GI_HLSLI
#define UZ_SSGI_RESTIR_GI_HLSLI
#include "ssgi_world_gather.hlsli"
#include "../include/restir_math.hlsli"

/// 16 of the 24 root DWORDs; mirrored by `SSGIRestirRootConstants` (ssgi_renderer.h).
cbuffer SSGIRestirRoot : register(b0) {
    uint g_depthSrv;
    uint g_normalSrv;
    uint g_reservoirInUav;        ///< initial: unused; temporal: the initial reservoirs; spatial: its input
    uint g_reservoirOutUav;       ///< initial: the initial reservoirs; temporal: this frame's history
    uint g_historyUav;            ///< temporal: the previous frame's history
    uint g_outputUav;             ///< spatial: the half-resolution trace texture (E/pi * rcIntensity)
    uint g_aoOutputUav;           ///< spatial: the half-resolution AO texture (1.0 in world mode)
    uint g_packedDimensions;      ///< low 16 = width, high 16 = height (half resolution)
    uint g_frameIndex;
    uint g_flags;                 ///< UZ_SSGI_RESTIR_* bits
    uint g_candidates;            ///< N cosine rays per pixel (r.GI.FinalGather.Samples)
    uint g_maxHistory;            ///< M cap of the previous-frame reservoir (candidates x r.GI.ReSTIR.MaxHistory)
    uint g_spatialSamples;        ///< neighbours per pixel (r.GI.ReSTIR.Spatial)
    uint g_spatialRadiusBits;     ///< float: neighbour disk radius in half-resolution pixels
    uint g_reserved0;
    uint g_reserved1;
};
/// The previous jittered view-projection plus the distance-field change boxes, the same block
/// ssgi_temporal_accum_cs binds at b2 (`HistoryCB`, ssgi_renderer.cpp).
cbuffer SSGIRestirHistory : register(b2) {
    row_major float4x4 g_previousViewProj;
    uint   g_regionCount;
    uint3  _regionPad;
    float4 g_regionLo[16];
    float4 g_regionHi[16];
};

#define UZ_SSGI_RESTIR_HISTORY_VALID 1u   ///< the previous-frame reservoirs were written by this path

/// Sample kinds (bits 0-1 of `flags`, diagnostic: what the direction found from its own surface);
/// bit 2 marks a zero geometric normal (an unsupported receiver: SurfaceGeometricNormal found no
/// valid derivative, the gather returns 0 there and so does this path).
#define UZ_SSGI_SAMPLE_NONE 0u
#define UZ_SSGI_SAMPLE_HIT  1u
#define UZ_SSGI_SAMPLE_SKY  2u
#define UZ_SSGI_SAMPLE_KIND_MASK 3u
#define UZ_SSGI_SURFACE_NO_GEOMETRIC_NORMAL 4u

/// 64 bytes; mirrored by `SSGIRestirReservoir` (ssgi_renderer.h). Normals are octahedral
/// 2x16 UNORM. `sampleRadiance` is what `sampleDirection` returned FROM `surfacePosition`.
struct SSGIReservoir {
    float3 sampleDirection; uint flags;
    float3 sampleRadiance; float sampleDistance;   ///< hit distance (cm), -1 escaped, -2 exhausted
    float3 surfacePosition; uint surfaceNormal;
    float weightSum; uint M; float W; uint surfaceGeometricNormal;
};

uint SsgiRestirPackNormal(float3 n) { return PackUnorm2x16(OctEncode(n)); }
float3 SsgiRestirUnpackNormal(uint packed) { return OctDecode(UnpackUnorm2x16(packed)); }
uint SsgiRestirSampleKind(SSGIReservoir r) { return r.flags & UZ_SSGI_SAMPLE_KIND_MASK; }
uint SsgiRestirKindOfDistance(float t) {
    return t >= 0.0 ? UZ_SSGI_SAMPLE_HIT : (t == -1.0 ? UZ_SSGI_SAMPLE_SKY : UZ_SSGI_SAMPLE_NONE);
}

/// The receiving surface of a reservoir: its ray origin, shading normal and geometric normal
/// (zero when the receiver had none, exactly as the gather traced it).
struct SsgiRestirSurface {
    float3 position;
    float3 normal;
    float3 geometricNormal;
};
SsgiRestirSurface SsgiRestirSource(SSGIReservoir r) {
    SsgiRestirSurface s;
    s.position = r.surfacePosition;
    s.normal = SsgiRestirUnpackNormal(r.surfaceNormal);
    s.geometricNormal = (r.flags & UZ_SSGI_SURFACE_NO_GEOMETRIC_NORMAL) != 0u
        ? 0.0 : SsgiRestirUnpackNormal(r.surfaceGeometricNormal);
    return s;
}
void SsgiRestirSetSurface(inout SSGIReservoir r, SsgiRestirSurface s) {
    r.surfacePosition = s.position;
    r.surfaceNormal = SsgiRestirPackNormal(s.normal);
    r.flags &= UZ_SSGI_SAMPLE_KIND_MASK;
    if (dot(s.geometricNormal, s.geometricNormal) > 0.0) r.surfaceGeometricNormal = SsgiRestirPackNormal(s.geometricNormal);
    else { r.surfaceGeometricNormal = 0u; r.flags |= UZ_SSGI_SURFACE_NO_GEOMETRIC_NORMAL; }
}
/// Bit-identical stored surfaces (same position, same packed normals): a ray traced from either
/// is the same ray, so a cached evaluation stands in for the trace (the temporal pass under a
/// static camera; the trace is deterministic).
bool SsgiRestirSameSurface(SSGIReservoir a, SSGIReservoir b) {
    return all(a.surfacePosition == b.surfacePosition) && a.surfaceNormal == b.surfaceNormal
        && a.surfaceGeometricNormal == b.surfaceGeometricNormal
        && (a.flags & UZ_SSGI_SURFACE_NO_GEOMETRIC_NORMAL) == (b.flags & UZ_SSGI_SURFACE_NO_GEOMETRIC_NORMAL);
}

/// Rec.709 luminance of a contribution, the committed reservoir target (restir_di.hlsli).
float RestirTarget(float3 contribution) {
    return dot(contribution, float3(0.2126, 0.7152, 0.0722));
}

/// Independent per-dimension hash, the same generator restir_di.hlsli uses for its reservoirs.
float SsgiRestirRandom(uint pixel, uint frame, uint dimension) {
    uint x = pixel * 1973u + frame * 9277u + dimension * 26699u + 911u;
    x = (x ^ (x >> 16u)) * 0x7feb352du;
    x = (x ^ (x >> 15u)) * 0x846ca68bu;
    x ^= x >> 16u;
    return float(x >> 8u) / 16777216.0;
}

/// True inside one of the change boxes (block B.2): a reservoir accumulated against a field that
/// no longer exists is not reused, as the gather's own history is not.
bool SsgiRestirInChangedRegion(float3 p) {
    [loop] for (uint r = 0; r < min(g_regionCount, 16u); ++r) {
        if (all(p >= g_regionLo[r].xyz) && all(p <= g_regionHi[r].xyz)) return true;
    }
    return false;
}

/// The cosine density of the gather's rays at `s` for direction `w` (solid angle), zero outside
/// the hemisphere. It is both the sampling density and the integrand's weight.
float SsgiRestirCosine(SsgiRestirSurface s, float3 w) {
    return max(dot(s.normal, w), 0.0) * M_1_PI;
}

/// The gather's per-ray function at surface `s`: what a cosine ray along `w` from there returns
/// (sdf_reflections.hlsli: the field trace with the surface's geometric-normal tolerance, the
/// Lambertian hit shading, the sky read; 0 when the trace exhausts its budget). `distance` is the
/// hit distance, -1 escaped, -2 exhausted. Skipped outside the hemisphere.
float3 SsgiRestirRadiance(SdfReflectionScene scene, SsgiRestirSurface s, float3 w, out float distance) {
    distance = -2.0;
    if (dot(s.normal, w) <= 0.0) return 0.0;
    return TraceSdfSurfaceRadianceIdentified(scene, s.position, w, 0u, s.geometricNormal, distance);
}

/// The target lum(L) * cos/pi of a reservoir's sample at its own surface, from the stored
/// radiance (traced from that surface when the sample was generated or replayed there).
float SsgiRestirOwnTarget(SsgiRestirSurface s, SSGIReservoir r) {
    return RestirTarget(r.sampleRadiance) * SsgiRestirCosine(s, r.sampleDirection);
}
/// The same target at another surface, by replaying the direction from there; `radiance` is the
/// replayed L (the sample's radiance should it be adopted by that surface).
float SsgiRestirReplayedTarget(SdfReflectionScene scene, SsgiRestirSurface s, float3 w,
                               out float3 radiance, out float distance) {
    radiance = SsgiRestirRadiance(scene, s, w, distance);
    return RestirTarget(radiance) * SsgiRestirCosine(s, w);
}

/// Receiver-plane compatibility of a source surface with the receiver, the gather's own rule
/// (SurfaceCompatible: normal cosine >= 0.9, plane distance within 0.1% of the camera distance).
bool SsgiRestirCompatible(SsgiRestirSurface receiver, SsgiRestirSurface source, float planeScale) {
    float3 delta = source.position - receiver.position;
    float planeDistance = max(abs(dot(delta, receiver.normal)), abs(dot(delta, source.normal)));
    float tolerance = max(1.0, length(receiver.position - cameraPos.xyz) * planeScale);
    return dot(receiver.normal, source.normal) >= 0.9 && planeDistance <= tolerance;
}

/// Half-resolution pixel of the previous frame that saw `position` (the gather's lattice:
/// full pixel 2*p+0.5 maps to half coordinate (uv*fullSize-0.5)/2), nearest tap.
bool SsgiRestirReproject(float3 position, uint2 size, uint2 fullSize, out uint2 previousPixel) {
    previousPixel = 0u;
    float4 clip = mul(float4(position, 1.0), g_previousViewProj);
    if (clip.w <= 0.0) return false;
    float2 uv = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
    if (any(uv < 0.0) || any(uv >= 1.0)) return false;
    float2 lattice = (uv * float2(fullSize) - 0.5) * 0.5;
    int2 p = clamp(int2(round(lattice)), int2(0, 0), int2(size) - 1);
    previousPixel = uint2(p);
    return true;
}
#endif
