/// UzEngine — radiance cache probe side classifier (followup-01 candidate).
///
/// For every probe of the radiance cache grid, over the mesh instances the global distance field was
/// composited from:
///   * the back-face fraction of a fixed set of rays cast from the probe against the baked triangles
///     (the face orientation is the baker's, i.e. the vertex normals) — the classification;
///   * the nearest triangle: the direction from its nearest point to the probe and the distance —
///     where to move the trace origin of a probe too close to a surface, and the plane of its guard.
/// The distance field cannot answer either: it is unsigned for two-sided meshes and biased by half a
/// mesh voxel plus half a global voxel, so a probe 30 cm in front of a wall and one 30 cm behind it
/// read the same. The nearest face alone is not enough either: inside overlapping solids (a wall
/// embedded in a floor, abutting tiles) the nearest face can be an embedded one seen from its front
/// (followup-01 reference, 2026-09-21); the rays see the enclosing faces from behind.
///
/// @ref Majercik 2021 "Scaling Probe-Based Real-Time Dynamic Global Illumination for Production",
///      section 6.1 — a probe is classified inside geometry by the fraction of its rays that hit
///      back faces.
/// @ref Moller & Trumbore 1997 "Fast, Minimum Storage Ray/Triangle Intersection".
/// @ref Gonzalez 2010 "Measurement of Areas on a Sphere Using Fibonacci and Latitude-Longitude
///      Lattices" — the fixed ray set: deterministic, so the classification does not change from frame
///      to frame.
/// @constants 64 threads per group match the CPU dispatch. 64 rays = the cache's maximum rays per
///            probe (kMaxRaysPerProbe), a fraction resolution of 1/64. Rays reach the cache's trace
///            distance, passed in `maxDistance`.
/// @constants Output, two float4 per probe: [0] xyz = unit direction from the nearest face point to
///            the probe, w = distance to it (65504 = no mesh); [1] x = back-face fraction over the
///            rays that hit, y = rays that hit, z = side of the nearest face (+1 front, -1 behind),
///            w = fraction of ALL rays along which some mesh is exited more often than entered.
/// @constants Below 1e-3 cm the direction falls back to the face normal (the probe is on the face).

cbuffer RootConstants : register(b0) {
    float gridOriginX;
    float gridOriginY;
    float gridOriginZ;
    float probeSpacing;
    uint  probesPerAxis;
    uint  totalProbes;
    uint  outputUav;
    uint  meshCount;
    float maxDistance;
};

/// GdfMeshSdfGPU (global_distance_field.h), 128 bytes.
struct MeshSdf {
    float3 origin;
    float distanceScale;
    float3 extent;
    float surfaceBias;
    float4 worldToLocalX;
    float4 worldToLocalY;
    float4 worldToLocalZ;
    uint srvIndex;
    uint resolution;
    uint albedoSrv;
    uint metallicSrv;
    float3 baseColorTint;
    float metallicMultiplier;
    uint materialFlags;
    uint trianglesSrv;
    uint triangleCountAndFlags;
    uint padding2;
};

cbuffer MeshConstants : register(b2) {
    MeshSdf meshes[512];
};

#include "mesh_closest_triangle.hlsli"

static const float kNoSurface = 65504.0;
static const uint  kClassificationRays = 64;
static const float kGoldenAngle = 2.39996322972865332;   // pi (3 - sqrt 5)

float3 FibonacciDirection(uint i) {
    const float z = 1.0 - (2.0 * float(i) + 1.0) / float(kClassificationRays);
    const float r = sqrt(max(0.0, 1.0 - z * z));
    const float phi = kGoldenAngle * float(i);
    return float3(r * cos(phi), r * sin(phi), z);
}

bool HasTriangles(MeshSdf mesh) {
    return (mesh.triangleCountAndFlags & 0x7fffffffu) != 0 && mesh.trianglesSrv != 0xffffffffu;
}

float3 ToLocal(MeshSdf mesh, float4 p) {
    return float3(dot(mesh.worldToLocalX, p), dot(mesh.worldToLocalY, p), dot(mesh.worldToLocalZ, p));
}

/// Slab test of a ray against a box; returns the entry parameter or a value > tMax on a miss.
float RayBox(float3 origin, float3 inverseDirection, float3 lower, float3 upper, float tMax) {
    const float3 t0 = (lower - origin) * inverseDirection;
    const float3 t1 = (upper - origin) * inverseDirection;
    const float3 tNear = min(t0, t1), tFar = max(t0, t1);
    const float enter = max(max(tNear.x, tNear.y), max(tNear.z, 0.0));
    const float leave = min(min(tFar.x, tFar.y), min(tFar.z, tMax));
    return enter <= leave ? enter : tMax + 1.0;
}

/// Nearest hit of a (mesh-local) ray over a mesh's BVH, culling nothing. The local direction is the
/// world direction mapped by the linear part, so `t` stays in world units. `backFace` is true when
/// the ray travels along the hit triangle's oriented normal.
void RayMesh(StructuredBuffer<Triangle> triangles, StructuredBuffer<BvhNode> nodes, uint countAndFlags,
             float3 origin, float3 direction, inout float tHit, inout bool backFace) {
    const uint count = countAndFlags & 0x7fffffffu;
    const float3 inverseDirection = 1.0 / direction;
    const bool bvh = (countAndFlags & 0x80000000u) != 0;
    const uint nodeEnd = bvh ? nodes[count].escapeIndex : 1u;
    uint nodeIndex = 0;
    [loop] while (nodeIndex < nodeEnd) {
        uint first = 0, triangleCount = count, next = nodeEnd;
        if (bvh) {
            const BvhNode node = nodes[count + nodeIndex];
            if (RayBox(origin, inverseDirection, node.boundsMin, node.boundsMax, tHit) > tHit) {
                nodeIndex = node.escapeIndex;
                continue;
            }
            if (node.triangleCount == 0) {
                ++nodeIndex;
                continue;
            }
            first = node.firstTriangle;
            triangleCount = node.triangleCount;
            next = node.escapeIndex;
        }
        [loop] for (uint i = 0; i < triangleCount; ++i) {
            const Triangle tri = triangles[first + i];
            const float3 e1 = tri.b - tri.a, e2 = tri.c - tri.a;
            const float3 pv = cross(direction, e2);
            const float det = dot(e1, pv);
            if (abs(det) < 1e-12) continue;
            const float invDet = 1.0 / det;
            const float3 tv = origin - tri.a;
            const float u = dot(tv, pv) * invDet;
            if (u < 0.0 || u > 1.0) continue;
            const float3 qv = cross(tv, e1);
            const float v = dot(direction, qv) * invDet;
            if (v < 0.0 || u + v > 1.0) continue;
            const float t = dot(e2, qv) * invDet;
            if (t <= 0.0 || t >= tHit) continue;
            tHit = t;
            backFace = dot(direction, tri.normal) > 0.0;
        }
        nodeIndex = next;
    }
}

/// Signed crossings of a (mesh-local) ray with one mesh up to `tMax`: +1 per back-face hit (the ray
/// leaves the mesh's solid), -1 per front-face hit (it enters). Over a closed mesh the sum is the
/// number of times the ray origin is inside that mesh, whatever faces lie in between — which is what
/// the first hit cannot tell when the origin is inside one solid and the first face met belongs to
/// another embedded in it (followup-01 R3, 2026-09-21). A ray cut short by `tMax` can only lose an
/// exit, so truncation never turns a free origin into an inside one. A ray through a shared edge
/// counts both triangles; the fixed Fibonacci set makes that a measure-zero case.
int RayMeshCrossings(StructuredBuffer<Triangle> triangles, StructuredBuffer<BvhNode> nodes, uint countAndFlags,
                     float3 origin, float3 direction, float tMax) {
    const uint count = countAndFlags & 0x7fffffffu;
    const float3 inverseDirection = 1.0 / direction;
    const bool bvh = (countAndFlags & 0x80000000u) != 0;
    const uint nodeEnd = bvh ? nodes[count].escapeIndex : 1u;
    int crossings = 0;
    uint nodeIndex = 0;
    [loop] while (nodeIndex < nodeEnd) {
        uint first = 0, triangleCount = count, next = nodeEnd;
        if (bvh) {
            const BvhNode node = nodes[count + nodeIndex];
            if (RayBox(origin, inverseDirection, node.boundsMin, node.boundsMax, tMax) > tMax) {
                nodeIndex = node.escapeIndex;
                continue;
            }
            if (node.triangleCount == 0) {
                ++nodeIndex;
                continue;
            }
            first = node.firstTriangle;
            triangleCount = node.triangleCount;
            next = node.escapeIndex;
        }
        [loop] for (uint i = 0; i < triangleCount; ++i) {
            const Triangle tri = triangles[first + i];
            const float3 e1 = tri.b - tri.a, e2 = tri.c - tri.a;
            const float3 pv = cross(direction, e2);
            const float det = dot(e1, pv);
            if (abs(det) < 1e-12) continue;
            const float invDet = 1.0 / det;
            const float3 tv = origin - tri.a;
            const float u = dot(tv, pv) * invDet;
            if (u < 0.0 || u > 1.0) continue;
            const float3 qv = cross(tv, e1);
            const float v = dot(direction, qv) * invDet;
            if (v < 0.0 || u + v > 1.0) continue;
            const float t = dot(e2, qv) * invDet;
            if (t <= 0.0 || t >= tMax) continue;
            crossings += dot(direction, tri.normal) > 0.0 ? 1 : -1;
        }
        nodeIndex = next;
    }
    return crossings;
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint probe = id.x;
    if (probe >= totalProbes) return;
    RWStructuredBuffer<float4> output = ResourceDescriptorHeap[outputUav];

    const uint3 cell = uint3(probe % probesPerAxis, (probe / probesPerAxis) % probesPerAxis,
                             probe / (probesPerAxis * probesPerAxis));
    const float3 p = float3(gridOriginX, gridOriginY, gridOriginZ) + float3(cell) * probeSpacing;
    const float4 wp = float4(p, 1.0);

    // ── Nearest face ──────────────────────────────────────────────────────
    float4 nearestRecord = float4(0.0, 0.0, 1.0, kNoSurface);
    float nearestSide = 1.0;
    for (uint i = 0; i < meshCount; ++i) {
        const MeshSdf mesh = meshes[i];
        if (!HasTriangles(mesh)) continue;
        const float3 local = ToLocal(mesh, wp);
        // The SDF volume contains the mesh and `distanceScale` bounds the world distance per local
        // unit from below (global_distance_field.cpp), so this skips only instances that cannot win.
        const float3 outside = max(abs(local - mesh.origin) - mesh.extent, 0.0);
        if (length(outside) * mesh.distanceScale >= nearestRecord.w) continue;

        StructuredBuffer<Triangle> triangles = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.trianglesSrv)];
        StructuredBuffer<BvhNode> nodes = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.trianglesSrv)];
        float3 nearestLocal, faceNormalLocal;
        const float2 closest = ClosestMeshSurface(local, triangles, nodes, mesh.triangleCountAndFlags,
                                                  nearestLocal, faceNormalLocal);
        if (closest.x >= kNoSurface * kNoSurface) continue;

        // local = M world + t, so world = M^-1 (local - t); M^-1's columns are the rows' cross
        // products over the determinant. A face normal is a covector: world normal ~ M^T n.
        const float3 m0 = mesh.worldToLocalX.xyz, m1 = mesh.worldToLocalY.xyz, m2 = mesh.worldToLocalZ.xyz;
        const float3 c0 = cross(m1, m2), c1 = cross(m2, m0), c2 = cross(m0, m1);
        const float determinant = dot(m0, c0);
        if (abs(determinant) < 1e-20) continue;
        const float3 r = nearestLocal - float3(mesh.worldToLocalX.w, mesh.worldToLocalY.w, mesh.worldToLocalZ.w);
        const float3 nearest = (c0 * r.x + c1 * r.y + c2 * r.z) / determinant;
        const float3 delta = p - nearest;
        const float distance = length(delta);
        if (distance >= nearestRecord.w) continue;

        const float3 faceNormal = normalize(m0 * faceNormalLocal.x + m1 * faceNormalLocal.y + m2 * faceNormalLocal.z);
        nearestRecord = float4(distance > 1e-3 ? delta / distance : faceNormal * closest.y, distance);
        nearestSide = closest.y;
    }

    // ── Back-face fraction of the fixed rays ─────────────────────────────
    // Also counted per ray: whether some mesh is left more often than it is entered along the whole
    // ray (RayMeshCrossings) — the ray's origin is inside that mesh even when its first hit is a
    // front face. Counted per mesh, not summed over meshes, so an unmatched exit through one open
    // mesh cannot be cancelled or inflated by another's faces.
    uint hits = 0, backHits = 0, insideRays = 0;
    [loop] for (uint ray = 0; ray < kClassificationRays; ++ray) {
        const float3 direction = FibonacciDirection(ray);
        const float4 wd = float4(direction, 0.0);
        float tHit = maxDistance;
        bool backFace = false;
        bool insideMesh = false;
        for (uint i = 0; i < meshCount; ++i) {
            const MeshSdf mesh = meshes[i];
            if (!HasTriangles(mesh)) continue;
            const float3 origin = ToLocal(mesh, wp);
            const float3 localDirection = ToLocal(mesh, wd);
            const float3 inverseDirection = 1.0 / localDirection;
            const float3 lower = mesh.origin - mesh.extent, upper = mesh.origin + mesh.extent;
            if (RayBox(origin, inverseDirection, lower, upper, maxDistance) > maxDistance) continue;
            StructuredBuffer<Triangle> triangles = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.trianglesSrv)];
            StructuredBuffer<BvhNode> nodes = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.trianglesSrv)];
            if (!insideMesh)
                insideMesh = RayMeshCrossings(triangles, nodes, mesh.triangleCountAndFlags, origin, localDirection, maxDistance) > 0;
            if (RayBox(origin, inverseDirection, lower, upper, tHit) > tHit) continue;
            RayMesh(triangles, nodes, mesh.triangleCountAndFlags, origin, localDirection, tHit, backFace);
        }
        if (tHit < maxDistance) {
            ++hits;
            backHits += backFace ? 1u : 0u;
        }
        insideRays += insideMesh ? 1u : 0u;
    }

    output[probe * 2] = nearestRecord;
    output[probe * 2 + 1] = float4(hits > 0 ? float(backHits) / float(hits) : 0.0, float(hits), nearestSide,
                                   float(insideRays) / float(kClassificationRays));
}
