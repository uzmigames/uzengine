#ifndef UZ_PATHTRACER_VOLUME
#define UZ_PATHTRACER_VOLUME

#include "pathtracer_math.hlsli"
#ifdef __cplusplus
namespace uz {
#define VOLUME_INOUT uint&
#else
#define VOLUME_INOUT inout uint
#endif

// Scalar kernels are shared with the CPU proofs. q is the projection onto
// (1,1,0)/sqrt(2), normalized by the sphere radius: -1 <= q <= 1.
inline float PathVolumeDensity(float q, float variation) {
    return 1.0f + variation*q;
}

inline uint PathVolumeHash(uint value) {
    value = (value ^ (value >> 16u))*0x7feb352du;
    value = (value ^ (value >> 15u))*0x846ca68bu;
    return value ^ (value >> 16u);
}

inline float PathVolumeRandom(VOLUME_INOUT state) {
    state = state*747796405u+2891336453u;
    uint word = ((state >> ((state >> 28u)+4u)) ^ state)*277803737u;
    word = (word >> 22u) ^ word;
    // Open interval, so candidate distances always advance and never log(0).
    return (float(word >> 9u)+0.5f)/8388608.0f;
}

inline float PathVolumeDelta(float sigmaT, float variation, float q0, float dq,
                            float distance, float firstU, uint state) {
    if (variation == 0.0f) return PathVolumeDistance(sigmaT,firstU);
    float majorant = sigmaT*(1.0f+variation);
    if (majorant == 0.0f) return 1e30f;
    float t = PathVolumeDistance(majorant,firstU);
    // No fixed iteration cap: truncating null collisions biases transport.
    while (t < distance) {
        float acceptance = PathVolumeDensity(q0+dq*t,variation)/(1.0f+variation);
        if (PathVolumeRandom(state) < acceptance) return t;
        t += PathVolumeDistance(majorant,PathVolumeRandom(state));
    }
    return t;
}

inline float PathVolumeRatio(float sigmaT, float variation, float q0, float dq,
                            float distance, uint state) {
    if (variation == 0.0f) return PathVolumeTransmittance(sigmaT,distance);
    float majorant = sigmaT*(1.0f+variation);
    if (majorant == 0.0f || distance == 0.0f) return 1.0f;
    float weight = 1.0f;
    float t = PathVolumeDistance(majorant,PathVolumeRandom(state));
    while (t < distance) {
        weight *= 1.0f-PathVolumeDensity(q0+dq*t,variation)/(1.0f+variation);
        if (weight == 0.0f) break;
        t += PathVolumeDistance(majorant,PathVolumeRandom(state));
    }
    return weight;
}

#undef VOLUME_INOUT
#ifdef __cplusplus
} // namespace uz
#else
// Pass-private b3, in engine world units (centimeters). A finite sphere
// makes environment and directional-light optical depths well-defined.
cbuffer PathVolumeRoot : register(b3) {
    float volumeSigmaA;
    float volumeSigmaS;
    float volumeG;
    float volumeRadius;
    float volumeCenterX;
    float volumeCenterY;
    float volumeCenterZ;
    uint volumeEnabled;
    float volumeVariation;
    uint volumeGridSrv;
    uint volumeGridCount;
    float volumePadding2;
};

// Grid media replace the procedural sphere when a scene gathers VolumeComponents.
bool PathVolumeActive() {
    return volumeEnabled != 0u && (volumeGridCount != 0u || volumeSigmaA + volumeSigmaS > 0.0);
}

float2 PathVolumeInterval(float3 origin, float3 direction, float distance) {
    float3 offset = origin - float3(volumeCenterX,volumeCenterY,volumeCenterZ);
    float b = dot(offset,direction);
    float discriminant = b*b - dot(offset,offset) + volumeRadius*volumeRadius;
    if (discriminant <= 0.0) return 0.0;
    float root = sqrt(discriminant);
    float start = max(0.0,-b-root);
    float end = min(distance,-b+root);
    return float2(start,max(start,end));
}

uint PathVolumeSeed(uint sample, uint2 pixel, uint stream) {
    return PathVolumeHash(sample ^ PathVolumeHash(pixel.x+pixel.y*65536u)
        ^ PathVolumeHash(stream));
}

float PathVolumeCoordinate(float3 P) {
    return ((P.x-volumeCenterX)+(P.y-volumeCenterY))*(0.707106781187/volumeRadius);
}

float PathVolumeSlope(float3 direction) {
    return (direction.x+direction.y)*(0.707106781187/volumeRadius);
}

// Heterogeneous grid media: a VolumeComponent's oriented box filled with a
// trilinear density grid (VolumeAsset). Mirrors PathVolumeGridDesc in
// pipeline_path_tracer_pass.h: 96 bytes per volume, up to kMaxPathVolumes.
struct PathVolumeGrid {
    float4 worldToLocal[3]; // affine rows: local = rows . (P,1), in [-1,1]^3 inside the box
    float sigmaA;           // per cm at unit density
    float sigmaS;
    float phaseG;
    float maxDensity;       // largest sampled density including densityScale: the majorant factor
    float3 emission;        // radiance per unit absorption: L_e(x) = sigmaA*density(x)*emission
    uint densitySrv;        // Texture3D<float>
    float densityScale;
    float3 halfTexel;       // 0.5/resolution: the UVW clamp that emulates clamp addressing
};

float3 PathVolumeGridLocal(PathVolumeGrid grid, float3 P) {
    float4 h = float4(P,1.0);
    return float3(dot(grid.worldToLocal[0],h),dot(grid.worldToLocal[1],h),dot(grid.worldToLocal[2],h));
}

// Slab test in the box's normalized frame. The direction is mapped by the
// linear part only, so the returned distances are world ray parameters.
// Returns (enter, exit) clipped to [0, distance]; empty when exit <= enter.
float2 PathVolumeGridInterval(PathVolumeGrid grid, float3 origin, float3 direction, float distance) {
    float3 o = PathVolumeGridLocal(grid,origin);
    float3 d = float3(dot(grid.worldToLocal[0].xyz,direction),dot(grid.worldToLocal[1].xyz,direction),
        dot(grid.worldToLocal[2].xyz,direction));
    // Axis-parallel rays: a tiny slope keeps every slab distance finite and the
    // outside-the-slab case correctly empty.
    d.x = abs(d.x) < 1e-12 ? 1e-12 : d.x;
    d.y = abs(d.y) < 1e-12 ? 1e-12 : d.y;
    d.z = abs(d.z) < 1e-12 ? 1e-12 : d.z;
    float3 inverse = 1.0/d;
    float3 t0 = (-1.0-o)*inverse, t1 = (1.0-o)*inverse;
    float3 tmin = min(t0,t1), tmax = max(t0,t1);
    float enter = max(max(tmin.x,tmin.y),max(tmin.z,0.0));
    float exit = min(min(tmax.x,tmax.y),min(tmax.z,distance));
    return float2(enter,max(enter,exit));
}

float PathVolumeGridDensity(PathVolumeGrid grid, float3 P) {
    float3 uvw = clamp(PathVolumeGridLocal(grid,P)*0.5+0.5,grid.halfTexel,1.0-grid.halfTexel);
    Texture3D<float> density = ResourceDescriptorHeap[NonUniformResourceIndex(grid.densitySrv)];
    return density.SampleLevel(linearWrap,uvw,0)*grid.densityScale;
}

// Woodcock/delta tracking on [start,end): majorant-Poisson candidates thinned
// with probability density(x)/maxDensity, so the accepted distance has the
// exact heterogeneous free-flight law. No fixed null-collision cap: truncation
// would bias transport. Returns the collision distance, or >= end for none.
// @ref Novak 2018 Eq. 13
float PathVolumeGridDelta(PathVolumeGrid grid, float3 origin, float3 direction, float start, float end,
                          float firstU, uint state) {
    float majorant = (grid.sigmaA+grid.sigmaS)*grid.maxDensity;
    if (majorant <= 0.0) return end;
    float t = start+PathVolumeDistance(majorant,firstU);
    while (t < end) {
        float acceptance = PathVolumeGridDensity(grid,origin+direction*t)/grid.maxDensity;
        if (PathVolumeRandom(state) < acceptance) return t;
        t += PathVolumeDistance(majorant,PathVolumeRandom(state));
    }
    return t;
}

// Ratio tracking: the product of null-collision probabilities estimates
// exp(-integral sigmaT(x) dx) without bias.
float PathVolumeGridRatio(PathVolumeGrid grid, float3 origin, float3 direction, float start, float end, uint state) {
    float majorant = (grid.sigmaA+grid.sigmaS)*grid.maxDensity;
    if (majorant <= 0.0 || end <= start) return 1.0;
    float weight = 1.0;
    float t = start+PathVolumeDistance(majorant,PathVolumeRandom(state));
    while (t < end) {
        weight *= 1.0-PathVolumeGridDensity(grid,origin+direction*t)/grid.maxDensity;
        if (weight == 0.0) break;
        t += PathVolumeDistance(majorant,PathVolumeRandom(state));
    }
    return weight;
}

// Extinction is additive, so the shadow transmittance factorizes into one
// ratio-tracking estimate per box, exact even where boxes overlap.
float PathVolumeGridShadow(float3 origin, float3 direction, float distance, uint seed) {
    StructuredBuffer<PathVolumeGrid> grids = ResourceDescriptorHeap[volumeGridSrv];
    float weight = 1.0;
    for (uint i = 0u; i < volumeGridCount; ++i) {
        PathVolumeGrid grid = grids[i];
        float2 interval = PathVolumeGridInterval(grid,origin,direction,distance);
        if (interval.y <= interval.x) continue;
        weight *= PathVolumeGridRatio(grid,origin,direction,interval.x,interval.y,seed ^ PathVolumeHash(i+1u));
        if (weight == 0.0) break;
    }
    return weight;
}

// Camera/continuation free flight through the boxes in nearest-entry order
// until the surface. Absorption-only, non-emissive boxes apply their
// ratio-tracking weight (same expectation, lower variance); the others use
// delta tracking and report a real collision. Exact for disjoint boxes;
// overlaps are tracked one box at a time.
bool PathVolumeGridFreeFlight(float3 origin, float3 direction, float surfaceDistance, float firstU, uint seed,
                              inout float3 throughput, out uint index, out float t) {
    StructuredBuffer<PathVolumeGrid> grids = ResourceDescriptorHeap[volumeGridSrv];
    float cursor = 0.0;
    index = 0u;
    t = surfaceDistance;
    for (uint step = 0u; step < 4u; ++step) {
        float bestEnter = surfaceDistance, bestExit = surfaceDistance;
        uint best = 0xffffffffu;
        for (uint i = 0u; i < volumeGridCount; ++i) {
            float2 interval = PathVolumeGridInterval(grids[i],origin,direction,surfaceDistance);
            float enter = max(interval.x,cursor);
            if (interval.y <= enter || enter >= bestEnter) continue;
            bestEnter = enter; bestExit = interval.y; best = i;
        }
        if (best == 0xffffffffu) return false;
        PathVolumeGrid grid = grids[best];
        uint state = PathVolumeHash(seed ^ PathVolumeHash(step+1u));
        if (grid.sigmaS <= 0.0 && all(grid.emission <= 0.0)) {
            throughput *= PathVolumeGridRatio(grid,origin,direction,bestEnter,bestExit,state);
        } else {
            float u = step == 0u ? firstU : PathVolumeRandom(state);
            float collision = PathVolumeGridDelta(grid,origin,direction,bestEnter,bestExit,u,state);
            if (collision < bestExit) { index = best; t = collision; return true; }
        }
        cursor = bestExit;
    }
    return false;
}

float PathVolumeShadow(float3 origin, float3 direction, float distance, uint seed) {
    if (!PathVolumeActive()) return 1.0;
    if (volumeGridCount != 0u) return PathVolumeGridShadow(origin,direction,distance,seed);
    float2 interval = PathVolumeInterval(origin,direction,distance);
    return PathVolumeRatio(volumeSigmaA+volumeSigmaS,volumeVariation,
        PathVolumeCoordinate(origin+direction*interval.x),PathVolumeSlope(direction),
        interval.y-interval.x,seed);
}
#endif
#endif
