/// Vertex shader for instanced mesh particle rendering.
///
/// Transforms mesh vertices into clip space using per-instance world-space TRS
/// matrices produced by the particle_mesh_instance_cs compute shader. Each
/// instance represents one alive particle; the instance transform buffer is a
/// StructuredBuffer<float4x4> accessed via SM6.6 bindless (ResourceDescriptorHeap).
///
/// # Pipeline Integration
/// The mesh particle render pass binds actual vertex/index buffers from a mesh
/// asset and issues a DrawIndexedInstanced (or ExecuteIndirect) call. The
/// instance count comes from the indirect args buffer written by the instance CS.
///
/// # Coordinate Convention
/// Instance transforms are row-major with translation in column 3:
/// ```
///   [Rx  Ux  Fx  Tx]
///   [Ry  Uy  Fy  Ty]
///   [Rz  Uz  Fz  Tz]
///   [ 0   0   0   1]
/// ```
/// Multiplication order: position * instanceWorld * viewProjection
/// (vector-on-left, row-major storage, consistent with all UzEngine shaders).
///
/// # Root Signature
/// - b0: ParticleMeshPassCB (viewProjection + bindless descriptor indices)
/// - Vertex buffer: POSITION (float3) from mesh asset
/// - SV_InstanceID: indexes into the instance transform buffer
///
/// @ref "GPU-Based Particle Simulation and Rendering" -- instanced mesh
///      particles with pre-computed transforms
/// @constants NONE
/// @deviations NONE

// ─────────────────────────────────────────────────────────────────────────────
// Per-Pass Constant Buffer (b0)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-pass constants uploaded by the CPU before the mesh particle draw call.
/// Contains the camera view-projection matrix and bindless SRV indices for
/// the instance transform and particle color structured buffers.
///
/// Total size: 80 bytes (5 x 16-byte aligned rows).
cbuffer ParticleMeshPassCB : register(b0) {
    row_major float4x4 viewProjection;       ///< 0:  View * Projection matrix.
    uint instanceTransformsSrvIndex;         ///< 64: SRV heap index for StructuredBuffer<float4x4>.
    uint colorsSrvIndex;                     ///< 68: SRV heap index for StructuredBuffer<float4>.
    uint _pad0;                              ///< 72: Padding for 16-byte alignment.
    uint _pad1;                              ///< 76: Padding for 16-byte alignment.
};

// ─────────────────────────────────────────────────────────────────────────────
// Output Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Interpolants passed from the vertex shader to the pixel shader.
struct VSOutput {
    float4 position   : SV_Position; ///< Clip-space position for rasterization.
    float3 worldPos   : WORLD_POS;   ///< World-space position (for future lighting/fog).
    float4 color      : COLOR;       ///< Per-instance particle color (RGBA).
    uint   instanceId : INSTANCE_ID; ///< Instance index (for debug visualization).
};

// ─────────────────────────────────────────────────────────────────────────────
// Vertex Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Transforms a mesh vertex by the per-instance particle world transform, then
/// projects into clip space via the view-projection matrix.
///
/// # Parameters
/// - localPos:   Object-space vertex position from the mesh vertex buffer.
/// - instanceID: Hardware instance index mapping to the transform buffer slot.
///
/// # Returns
/// Clip-space position, world position, per-instance color, and instance ID.
VSOutput main(float3 localPos : POSITION, uint instanceID : SV_InstanceID) {
    VSOutput output;

    // ── Fetch per-instance transform from bindless StructuredBuffer ──────────
    // The instance CS wrote one row-major float4x4 per alive particle into this
    // buffer. The order matches the draw's instance indexing (0..instanceCount-1).
    StructuredBuffer<float4x4> instanceTransforms =
        ResourceDescriptorHeap[instanceTransformsSrvIndex];
    row_major float4x4 instanceWorld = instanceTransforms[instanceID];

    // ── Transform to world space ────────────────────────────────────────────
    // Vector-on-left multiplication: float4(pos, 1) * row_major_matrix
    float4 worldPos4 = mul(float4(localPos, 1.0f), instanceWorld);
    output.worldPos  = worldPos4.xyz;

    // ── Transform to clip space ─────────────────────────────────────────────
    output.position = mul(worldPos4, viewProjection);

    // ── Fetch per-instance particle color ───────────────────────────────────
    // The color buffer is written by the simulation (spawn/update) shaders and
    // indexed identically to the instance transform buffer (same alive-particle
    // compaction order from the instance CS's InterlockedAdd).
    StructuredBuffer<float4> particleColors =
        ResourceDescriptorHeap[colorsSrvIndex];
    output.color = particleColors[instanceID];

    // ── Pass through instance ID for debug / per-instance PS logic ──────────
    output.instanceId = instanceID;

    return output;
}
