/// UzEngine — radiance cache probe guard (shared by every reader of the cache).
///
/// Under the candidate classification (r.GI.RadianceCache.ProbeClassification = 1, followup-01) a
/// probe that the biased field puts inside a surface's band is no longer discarded when its nearest
/// mesh face is in front of it: it is traced from an origin moved off that face and carries the
/// face's plane. Discarding it was what kept such a probe from lighting the other side of a thin wall
/// — by accident, at the cost of the near side — and the plane keeps that protection for exactly
/// these probes: a receiver behind the face the probe was moved off does not use it.
///
/// Record: the float4 at `probesPerAxis^3 * 7 + probe` of the SH buffer, xyz = the face's outward
/// direction (towards the probe), w = the probe's distance to the face; w = 0 means no guard, which is
/// every probe under the current rule.
///
/// @constants The 5 cm tolerance is the trace's surface epsilon (kSurfaceEpsilon): receivers lying on
/// the face itself, within rasterisation and field precision, are in front of it.

#ifndef UZ_RC_PROBE_GUARD_HLSLI
#define UZ_RC_PROBE_GUARD_HLSLI

static const float kProbeGuardTolerance = 5.0;

/// Index of a probe's guard record in the SH buffer (after the seven SH records of every probe).
uint ProbeGuardIndex(uint probesPerAxis, uint probeLinear) {
    return probesPerAxis * probesPerAxis * probesPerAxis * 7u + probeLinear;
}

/// Index of a probe's temporal-state record in the SH buffer (after the guard records; stage-1
/// stability, written and read by `radiance_cache_trace_cs.hlsl` alone): x = mean radiance luminance of
/// the stored SH, y = its per-update innovation variance, z = the change detector's signed drift,
/// w = update count + 256 * classification counter + 4096 * flags (bit 0: inside). Readers of the cache
/// never touch it; SH record 6.w stays their validity (1 valid, 0 none, -1 inside with the SH kept).
uint ProbeStateIndex(uint probesPerAxis, uint probeLinear) {
    return probesPerAxis * probesPerAxis * probesPerAxis * 8u + probeLinear;
}

/// Second encoding (classification mode 3 with `r.GI.RadianceCache.ReachRelocate`): w < 0 marks a
/// RELOCATED probe — one that was inside geometry or in air the camera cannot reach and was moved to
/// the nearest reached air within half a spacing. xyz is the offset from its grid position to where it
/// was traced; the plane's normal is that offset's direction and -w how far behind the moved probe
/// the surface lies. Readers weight and guard such a probe at its moved position (Majercik 2021,
/// section 6.2, probe relocation); trilinear weights stay on the grid.

/// Where the probe at grid position `probePosition` actually is.
float3 ProbeEffectivePosition(float4 guard, float3 probePosition) {
    return guard.w < 0.0 ? probePosition + guard.xyz : probePosition;
}

/// True when `receiver` lies behind the face the probe at `probePosition` (its GRID position) was
/// moved off.
bool ProbeGuardRejects(float4 guard, float3 probePosition, float3 receiver) {
    if (guard.w == 0.0) return false;
    if (guard.w < 0.0) {
        const float len = length(guard.xyz);
        if (len <= 0.0) return false;
        const float3 n = guard.xyz / len;
        const float3 facePoint = probePosition + guard.xyz + n * guard.w;   // w < 0: behind the moved probe
        return dot(receiver - facePoint, n) < -kProbeGuardTolerance;
    }
    const float3 facePoint = probePosition - guard.xyz * guard.w;
    return dot(receiver - facePoint, guard.xyz) < -kProbeGuardTolerance;
}

// Stage-2 reader helpers (near-field exclusion, Chebyshev visibility; the parameter record after the
// state records). Every reader of the cache includes this file, so they all get them here.
#include "rc_probe_visibility.hlsli"

#endif // UZ_RC_PROBE_GUARD_HLSLI
