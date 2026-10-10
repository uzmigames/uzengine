/// UzEngine — radiance cache probe reachability (followup-01 candidate, classification mode 3).
///
/// A probe is valid when the air around it is connected to the camera. Every surface the camera can
/// see bounds air connected to the camera, so this is the region whose probes can light a visible
/// surface, and — unlike the face side (modes 1-2) — it needs neither closed meshes nor a consistent
/// orientation, which Sponza's meshes do not have (followup-01 H-69).
///
/// Four passes over one pipeline, selected by `pass` (host order 3, 0, 1 x N, 2):
///   3  RESET   zero the seed counter.
///   4  SEEDS   (optional, after 0) air voxels just in front of visible points become seeds.
///   0  AIR     every voxel of a 128^3 grid covering the probe grid (cell = spacing / 8): air when the
///              biased field at its centre is >= kAirThreshold; air voxels within kSeedRadius of the
///              camera are the seed (state 2). Counts the seeds.
///   1  FLOOD   6-connected propagation of state 2 through air, `localIterations` steps inside an
///              8^3 tile in groupshared memory per dispatch; the host repeats the dispatch.
///   2  PROBES  per probe: climb the field gradient from the probe until the clearance guarantees the
///              voxel reached is air (the gradient of an unsigned field points away from the nearest
///              surface on either side of it, so a probe in front of a wall climbs into the room and a
///              probe inside a wall climbs towards the wall's middle); valid when that voxel was
///              reached. Without a seed (camera inside the field's band) connectivity is not used and
///              the climb alone decides.
///
/// @ref Majercik 2021 "Scaling Probe-Based Real-Time Dynamic Global Illumination for Production",
///      section 6.1 — probes inside geometry are disabled; this replaces the per-probe test with
///      connectivity to the viewer.
/// @constants Output per probe, two float4 in the probe side buffer (the layout mode 1 uses, other
///            meanings): [0] xyz = unit direction from the probe to the end of the climb, w = how far
///            behind the probe along -xyz the field is smallest (the surface; the guard plane);
///            [1] x = 0 unreached / inside, 1 valid, 2 relocated (xyz of [0] = direction of the move,
///            y = its length, [0].w = guard plane distance behind the moved probe), y = climb length (cm),
///            z = field at the probe,
///            w = 1 when connectivity was used (a seed existed), 0 when the climb alone decided.
/// @constants kAirThreshold = half a voxel: a surface band thinner than a voxel cannot hold two
///            6-adjacent air centres on either side, so the fill does not leak through a wall; the
///            climb target adds half a voxel diagonal, so the voxel holding its end point is air
///            (the field is 1-Lipschitz). kSeedRadius 2.5 voxels tolerates a camera inside the band.
///            Climb: at most kClimbSteps steps and half a probe spacing (the relocation bound of
///            mode 1).

#include "../common/constants.hlsli"

SamplerState linearClamp : register(s2);

cbuffer RootConstants : register(b0) {
    uint  pass;
    uint  airUav;
    uint  sideUav;
    uint  statsUav;
    float cameraX;
    float cameraY;
    float cameraZ;
    uint  localIterations;
    uint  relocate;          // 1 = move invalid probes to the nearest reached air (r.GI.RadianceCache.ReachRelocate)
    uint  positionSrv;       // G-buffer world position (pass 4); cleared to 0 where nothing was drawn
    uint  viewWidth;
    uint  viewHeight;
};

// Prefix of RadianceCacheTraceConstants (radiance_cache_trace_cs.hlsl), bound at b2 by the host:
// the probe grid and the GDF clipmaps. The rest of the buffer is not read here.
cbuffer TraceConstants : register(b2) {
    float gridOriginX;
    float gridOriginY;
    float gridOriginZ;
    float probeSpacing;

    uint  probesPerAxis;
    uint  totalProbes;
    uint  raysPerProbe;
    uint  frameIndex;

    float maxTraceDistance;
    float temporalBlendAlpha;
    uint  shBufferUav;
    uint  firstWrite;

    float gdfCenter0X, gdfCenter0Y, gdfCenter0Z, gdfExtent0;
    float gdfCenter1X, gdfCenter1Y, gdfCenter1Z, gdfExtent1;
    float gdfCenter2X, gdfCenter2Y, gdfCenter2Z, gdfExtent2;
    float gdfCenter3X, gdfCenter3Y, gdfCenter3Z, gdfExtent3;

    uint  gdfClipmapSrv0;
    uint  gdfClipmapSrv1;
    uint  gdfClipmapSrv2;
    uint  gdfClipmapSrv3;
};

static const float kSurfaceEpsilon = 5.0;   // cm — central-difference offset of GdfNormal
#include "gdf_surface.hlsli"

static const uint  kAirRes = 128;
static const uint  kTile = 8;
static const uint  kStateSolid = 0, kStateAir = 1, kStateReached = 2;
static const float kSeedRadiusVoxels = 2.5;
static const uint  kClimbSteps = 48;
static const float kClimbMinStep = 2.0;     // cm
static const uint  kGuardSearchSteps = 40;  // 2 cm apart: 80 cm behind the probe, beyond any GDF bias here
static const float kGuardSearchStep = 2.0;

float VoxelSize() { return probeSpacing * float(probesPerAxis) / float(kAirRes); }
/// Probe i sits at gridOrigin + i * spacing; the air grid spans half a spacing beyond the outer probes.
float3 AirOrigin() { return float3(gridOriginX, gridOriginY, gridOriginZ) - 0.5 * probeSpacing; }
float AirThreshold() { return 0.5 * VoxelSize(); }

uint VoxelIndex(uint3 v) { return v.x + v.y * kAirRes + v.z * kAirRes * kAirRes; }

float3 VoxelCentre(uint3 v) { return AirOrigin() + (float3(v) + 0.5) * VoxelSize(); }

/// Pass 4 (r.GI.RadianceCache.ReachVisibleSeeds): the stretch of a view ray just in front of a visible
/// point is air connected to the camera by definition, even where the biased field seals the opening
/// it was seen through (Sponza's arcade, followup-01 2026-09-25). Air voxels on that stretch become
/// seeds. Solid voxels are never carved: an air voxel's centre is at least AirThreshold + bias from
/// every surface, so it lies on the same side of any wall as the ray point inside it, while a carved
/// solid voxel could open a path through a thin wall.
static const uint  kSeedPixelStride = 4;
static const uint  kSeedSteps = 4;          // 1.5 .. 4.5 voxels back towards the camera

void PassVisibleSeeds(uint cellIndex) {
    const uint w = (viewWidth + kSeedPixelStride - 1) / kSeedPixelStride;
    const uint h = (viewHeight + kSeedPixelStride - 1) / kSeedPixelStride;
    if (cellIndex >= w * h) return;
    const uint2 pixel = min(uint2(cellIndex % w, cellIndex / w) * kSeedPixelStride + kSeedPixelStride / 2,
                            uint2(viewWidth - 1, viewHeight - 1));
    Texture2D<float4> positions = ResourceDescriptorHeap[positionSrv];
    const float3 p = positions.Load(int3(pixel, 0)).xyz;
    if (all(p == 0.0)) return;                  // nothing drawn
    const float3 toCamera = float3(cameraX, cameraY, cameraZ) - p;
    const float dist = length(toCamera);
    if (dist <= 0.0) return;
    RWStructuredBuffer<uint> air = ResourceDescriptorHeap[airUav];
    for (uint k = 0; k < kSeedSteps; ++k) {
        const float t = (float(k) + 1.5) * VoxelSize();
        if (t >= dist) break;
        const float3 vf = (p + toCamera / dist * t - AirOrigin()) / VoxelSize();
        if (any(vf < 0.0) || any(vf >= float(kAirRes))) continue;
        const uint index = VoxelIndex(uint3(vf));
        if (air[index] == kStateAir) air[index] = kStateReached;
    }
}

groupshared uint gs_state[(kTile + 2) * (kTile + 2) * (kTile + 2)];

uint SharedIndex(int3 s) { return uint(s.x + s.y * (kTile + 2) + s.z * (kTile + 2) * (kTile + 2)); }

void PassAir(uint3 v) {
    RWStructuredBuffer<uint> air = ResourceDescriptorHeap[airUav];
    const float3 c = VoxelCentre(v);
    uint state = SampleGdfDistance(c) >= AirThreshold() ? kStateAir : kStateSolid;
    if (state == kStateAir && distance(c, float3(cameraX, cameraY, cameraZ)) <= kSeedRadiusVoxels * VoxelSize()) {
        state = kStateReached;
        RWStructuredBuffer<uint> stats = ResourceDescriptorHeap[statsUav];
        InterlockedAdd(stats[0], 1u);
    }
    air[VoxelIndex(v)] = state;
}

void PassFlood(uint3 groupId, uint3 local) {
    RWStructuredBuffer<uint> air = ResourceDescriptorHeap[airUav];
    const int3 tileOrigin = int3(groupId * kTile) - 1;
    // Load the tile and its one-voxel halo (10^3 entries by 512 threads).
    const uint flat = local.x + local.y * kTile + local.z * kTile * kTile;
    for (uint i = flat; i < (kTile + 2) * (kTile + 2) * (kTile + 2); i += kTile * kTile * kTile) {
        const int3 s = int3(i % (kTile + 2), (i / (kTile + 2)) % (kTile + 2), i / ((kTile + 2) * (kTile + 2)));
        const int3 g = tileOrigin + s;
        const bool inside = all(g >= 0) && all(g < int(kAirRes));
        gs_state[i] = inside ? air[VoxelIndex(uint3(g))] : kStateSolid;
    }
    GroupMemoryBarrierWithGroupSync();

    const int3 s = int3(local) + 1;
    const uint me = SharedIndex(s);
    // `localIterations` is a root constant, so every thread runs the same number of barriers.
    for (uint it = 0; it < localIterations; ++it) {
        bool next = false;
        if (gs_state[me] == kStateAir) {
            next = gs_state[SharedIndex(s + int3(1, 0, 0))] == kStateReached
                || gs_state[SharedIndex(s - int3(1, 0, 0))] == kStateReached
                || gs_state[SharedIndex(s + int3(0, 1, 0))] == kStateReached
                || gs_state[SharedIndex(s - int3(0, 1, 0))] == kStateReached
                || gs_state[SharedIndex(s + int3(0, 0, 1))] == kStateReached
                || gs_state[SharedIndex(s - int3(0, 0, 1))] == kStateReached;
        }
        GroupMemoryBarrierWithGroupSync();
        if (next) gs_state[me] = kStateReached;
        GroupMemoryBarrierWithGroupSync();
    }
    air[VoxelIndex(groupId * kTile + local)] = gs_state[me];
}

void PassProbes(uint probe) {
    if (probe >= totalProbes) return;
    RWStructuredBuffer<float4> output = ResourceDescriptorHeap[sideUav];
    RWStructuredBuffer<uint> air = ResourceDescriptorHeap[airUav];
    RWStructuredBuffer<uint> stats = ResourceDescriptorHeap[statsUav];
    const bool connectivity = stats[0] > 0;

    const uint3 cell = uint3(probe % probesPerAxis, (probe / probesPerAxis) % probesPerAxis,
                             probe / (probesPerAxis * probesPerAxis));
    const float3 p = float3(gridOriginX, gridOriginY, gridOriginZ) + float3(cell) * probeSpacing;
    const float target = AirThreshold() + 0.8660254 * VoxelSize();
    const float fieldAtProbe = SampleGdfDistance(p);

    float3 q = p;
    float d = fieldAtProbe;
    float travelled = 0.0;
    bool climbed = d >= target;
    [loop] for (uint step = 0; step < kClimbSteps && !climbed; ++step) {
        const float3 g = GdfNormal(q);
        if (dot(g, g) == 0.0) break;
        // Never step further than the free ball around q allows plus a little: the climb must not
        // jump across a surface it has not left yet.
        const float s = max(kClimbMinStep, min(target - d, max(d, 0.0) + 8.0));
        if (travelled + s > 0.5 * probeSpacing) break;
        q += g * s;
        travelled += s;
        d = SampleGdfDistance(q);
        climbed = d >= target;
    }

    bool valid = false;
    if (climbed) {
        const float3 vf = (q - AirOrigin()) / VoxelSize();
        if (all(vf >= 0.0) && all(vf < float(kAirRes))) {
            const uint state = air[VoxelIndex(uint3(vf))];
            valid = connectivity ? state == kStateReached : state != kStateSolid;
        } else {
            valid = !connectivity;      // beyond the air grid: no connectivity information
        }
    }
    const float3 offset = q - p;
    const float len = length(offset);
    const float3 direction = len > 1e-3 ? offset / len : float3(0.0, 0.0, 1.0);
    // Guard plane: where the surface behind the probe is. The biased field at the probe cannot say
    // (it reads <= 0 up to the bias away from a face — putting the plane on the probe rejected the
    // probe's own wall, followup-01 2026-09-24), but the unsigned field is smallest AT the surface,
    // so the plane goes to the field's minimum along -direction.
    float guardDistance = 0.0, guardField = fieldAtProbe;
    [loop] for (uint k = 1; k <= kGuardSearchSteps; ++k) {
        const float t = float(k) * kGuardSearchStep;
        const float f = SampleGdfDistance(p - direction * t);
        if (f < guardField) {
            guardField = f;
            guardDistance = t;
        }
    }
    // Relocation (Majercik 2021, section 6.2): an invalid probe moves to the nearest air voxel
    // reached from the camera within half a spacing, so a wall face whose cell corners are all
    // inside the wall or behind it still has a probe on its visible side. The reached voxel's centre
    // has at least half a voxel of clearance, so the trace can start there. The surface the probe
    // crossed lies between its grid position and the new one; the guard plane goes to the field
    // minimum on that segment.
    if (!valid && relocate != 0u && connectivity) {
        const int radius = int(floor(0.5 * probeSpacing / VoxelSize()));
        const int3 centre = int3(floor((p - AirOrigin()) / VoxelSize()));
        float bestDistance = 1e30;
        float3 best = p;
        for (int dz = -radius; dz <= radius; ++dz)
        for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx) {
            const int3 v = centre + int3(dx, dy, dz);
            if (any(v < 0) || any(v >= int(kAirRes))) continue;
            if (air[VoxelIndex(uint3(v))] != kStateReached) continue;
            const float3 c = VoxelCentre(uint3(v));
            const float dist = distance(c, p);
            if (dist <= 0.5 * probeSpacing && dist < bestDistance) {
                bestDistance = dist;
                best = c;
            }
        }
        if (bestDistance < 1e29) {
            const float3 moved = best - p;
            const float movedLength = length(moved);
            const float3 n = movedLength > 1e-3 ? moved / movedLength : float3(0.0, 0.0, 1.0);
            float relocatedGuard = 0.0, relocatedField = SampleGdfDistance(best);
            [loop] for (float t = kGuardSearchStep; t <= movedLength; t += kGuardSearchStep) {
                const float f = SampleGdfDistance(best - n * t);
                if (f < relocatedField) {
                    relocatedField = f;
                    relocatedGuard = t;
                }
            }
            output[probe * 2] = float4(n, relocatedGuard);
            output[probe * 2 + 1] = float4(2.0, movedLength, fieldAtProbe, 1.0);
            return;
        }
    }
    output[probe * 2] = float4(direction, guardDistance);
    output[probe * 2 + 1] = float4(valid ? 1.0 : 0.0, len, fieldAtProbe, connectivity ? 1.0 : 0.0);
}

[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint3 local : SV_GroupThreadID,
          uint groupIndex : SV_GroupIndex) {
    if (pass == 3u) {
        // Reset the seed counter (the RHI has no UAV clear); dispatched as (1,1,1) before pass 0.
        if (groupIndex == 0u) {
            RWStructuredBuffer<uint> stats = ResourceDescriptorHeap[statsUav];
            stats[0] = 0u;
        }
    } else if (pass == 4u) {
        PassVisibleSeeds(groupId.x * 512u + groupIndex);
    } else if (pass == 0u) {
        PassAir(id);
    } else if (pass == 1u) {
        PassFlood(groupId, local);
    } else {
        // 4096 probes over dispatch (1,1,8): 512 threads per group.
        PassProbes(groupId.z * 512u + groupIndex);
    }
}
