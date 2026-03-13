/// GPU Frustum Culling Compute Shader
///
/// Tests per-instance bounding spheres against 6 frustum planes extracted from
/// the view-projection matrix. Visible instances are appended to an output
/// buffer for indirect indexed draw calls.
///
/// # Inputs
/// - `g_BoundingSpheres` (t0): StructuredBuffer<float4> — xyz = world-space
///   center, w = radius. One entry per instance.
/// - Root constants (b0): ViewProjection matrix (float4x4) + instance count (uint).
///
/// # Outputs
/// - `g_VisibleIndices` (u0): AppendStructuredBuffer<uint> — indices of visible
///   instances. Consumed by indirect draw calls.
/// - `g_IndirectArgs` (u1): RWByteAddressBuffer — indirect draw argument buffer.
///   Atomically increments `instanceCount` (offset 4) for each visible instance.
///
/// # Thread Group
/// [64, 1, 1] — one thread per instance. Dispatch ceil(instanceCount / 64) groups.

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants
// ─────────────────────────────────────────────────────────────────────────────

cbuffer CullConstants : register(b0) {
    float4x4 g_ViewProjection;
    uint     g_InstanceCount;
    uint3    _pad;
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

StructuredBuffer<float4>       g_BoundingSpheres : register(t0);
AppendStructuredBuffer<uint>   g_VisibleIndices  : register(u0);
RWByteAddressBuffer            g_IndirectArgs    : register(u1);

// ─────────────────────────────────────────────────────────────────────────────
// Frustum Plane Extraction (Gribb-Hartmann method)
// ─────────────────────────────────────────────────────────────────────────────

/// Extracts 6 frustum planes from a row-major view-projection matrix.
/// Each plane is stored as float4(A, B, C, D) where Ax+By+Cz+D >= 0 means inside.
/// Planes are normalized so that (A,B,C) has unit length.
void ExtractFrustumPlanes(float4x4 vp, out float4 planes[6]) {
    // Left:   row3 + row0
    planes[0] = float4(
        vp[0][3] + vp[0][0],
        vp[1][3] + vp[1][0],
        vp[2][3] + vp[2][0],
        vp[3][3] + vp[3][0]
    );
    // Right:  row3 - row0
    planes[1] = float4(
        vp[0][3] - vp[0][0],
        vp[1][3] - vp[1][0],
        vp[2][3] - vp[2][0],
        vp[3][3] - vp[3][0]
    );
    // Bottom: row3 + row1
    planes[2] = float4(
        vp[0][3] + vp[0][1],
        vp[1][3] + vp[1][1],
        vp[2][3] + vp[2][1],
        vp[3][3] + vp[3][1]
    );
    // Top:    row3 - row1
    planes[3] = float4(
        vp[0][3] - vp[0][1],
        vp[1][3] - vp[1][1],
        vp[2][3] - vp[2][1],
        vp[3][3] - vp[3][1]
    );
    // Near:   row2
    planes[4] = float4(
        vp[0][2],
        vp[1][2],
        vp[2][2],
        vp[3][2]
    );
    // Far:    row3 - row2
    planes[5] = float4(
        vp[0][3] - vp[0][2],
        vp[1][3] - vp[1][2],
        vp[2][3] - vp[2][2],
        vp[3][3] - vp[3][2]
    );

    // Normalize each plane so distance tests are in world units
    [unroll]
    for (uint i = 0; i < 6; ++i) {
        float len = length(planes[i].xyz);
        planes[i] /= len;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

[numthreads(64, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    uint instanceIdx = dtid.x;
    if (instanceIdx >= g_InstanceCount) return;

    float4 sphere = g_BoundingSpheres[instanceIdx];
    float3 center = sphere.xyz;
    float  radius = sphere.w;

    // Extract frustum planes from the view-projection matrix
    float4 planes[6];
    ExtractFrustumPlanes(g_ViewProjection, planes);

    // Test sphere against all 6 planes — if the sphere is entirely behind
    // any plane, it is outside the frustum.
    bool visible = true;
    [unroll]
    for (uint i = 0; i < 6; ++i) {
        float dist = dot(planes[i].xyz, center) + planes[i].w;
        if (dist < -radius) {
            visible = false;
            break;
        }
    }

    if (visible) {
        g_VisibleIndices.Append(instanceIdx);

        // Atomically increment the instance count in the indirect args buffer.
        // RHIDrawIndexedIndirectArgs layout: [indexCount, instanceCount, startIndex, baseVertex, startInstance]
        // instanceCount is at byte offset 4.
        g_IndirectArgs.InterlockedAdd(4, 1);
    }
}
