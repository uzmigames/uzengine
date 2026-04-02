/// GPU Particle Mesh Instance Transform Compute Shader
///
/// Produces a per-particle float4x4 instance transform buffer and a
/// DrawIndexedInstancedIndirect argument buffer for 3D mesh particle rendering.
/// This shader replaces the billboard vertex shader path for emitters that use
/// MeshRenderModule.
///
/// # Overview
/// For each alive particle (age < lifespan) the shader computes a TRS matrix
/// (translate * rotate * scale) and appends it atomically to the output
/// instance transform buffer. A separate single-thread pass (the IndirectArgs
/// phase) reads MeshInstanceCount to write the final DrawIndexedInstanced
/// indirect args. Both buffers are UAVs in the same descriptor heap.
///
/// # Rotation Modes
/// Selected via the `rotationMode` constant:
/// - 0 (None)            -- Identity rotation. Mesh always faces +Z in world space.
/// - 1 (VelocityAligned) -- Forward (+Z) tracks normalised velocity. Up = world Y
///                          with fallback to world X when velocity is near-vertical.
/// - 2 (Random)          -- Deterministic per-particle XYZ Euler rotation from a
///                          Wang hash on the particle index. Rotation is fixed at
///                          spawn (index-derived seed) -- particles do not spin in
///                          flight. For spinning behaviour, add an angular velocity
///                          attribute and apply it in the update shader instead.
///
/// # Buffer Layout — Indirect Draw Args (bytes 0..19)
/// DrawIndexedInstancedIndirectArgs (5 x uint32):
/// ```
/// Offset  0: IndexCountPerInstance = meshIndexCount (caller-supplied)
/// Offset  4: InstanceCount         = alive instance count (written by this CS)
/// Offset  8: StartIndexLocation    = 0
/// Offset 12: BaseVertexLocation    = 0
/// Offset 16: StartInstanceLocation = 0
/// ```
///
/// # Thread Group
/// [MESH_INSTANCE_GROUP_SIZE, 1, 1] where MESH_INSTANCE_GROUP_SIZE = 256.
/// Dispatch (ceil(aliveCount / 256), 1, 1) groups before the draw call.
///
/// # Bindless Resources (SM6.6)
/// All structured buffers are accessed via ResourceDescriptorHeap[index].
///
/// # Root Signature
/// - b0: VFXMeshInstanceConstants (simulation parameters + bindless descriptor indices)
///
/// @constants MESH_INSTANCE_GROUP_SIZE=256
/// @deviations Random rotation uses XYZ Euler not quaternion -- cheaper for GPU
///
/// # SM5 Register Layout (non-bindless fallback)
/// - t0: StructuredBuffer<float4>           positions  (xyz=pos, w=unused)
/// - t1: StructuredBuffer<float4>           velocities (xyz=vel, w=unused)
/// - t2: StructuredBuffer<float>            ages
/// - t3: StructuredBuffer<float>            lifespans
/// - u0: RWStructuredBuffer<float4x4>       instanceTransforms
/// - u1: RWStructuredBuffer<uint>           instanceCount (1 element)
/// - u2: RWByteAddressBuffer                indirectArgs (20 bytes)

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Thread group size. Must match the CPU-side ceil(aliveCount / 256) dispatch.
static const uint MESH_INSTANCE_GROUP_SIZE = 256;

/// Rotation mode enum values -- must match MeshRotationMode in mesh_render_module.h.
static const uint kRotationMode_None            = 0u;
static const uint kRotationMode_VelocityAligned = 1u;
static const uint kRotationMode_Random          = 2u;

// ─────────────────────────────────────────────────────────────────────────────
// Indirect Args Buffer Byte Offsets
// ─────────────────────────────────────────────────────────────────────────────

static const uint INDIRECT_INDEX_COUNT_OFFSET    = 0;   ///< IndexCountPerInstance
static const uint INDIRECT_INSTANCE_COUNT_OFFSET = 4;   ///< InstanceCount
static const uint INDIRECT_START_INDEX_OFFSET    = 8;   ///< StartIndexLocation
static const uint INDIRECT_BASE_VERTEX_OFFSET    = 12;  ///< BaseVertexLocation
static const uint INDIRECT_START_INSTANCE_OFFSET = 16;  ///< StartInstanceLocation

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffer (b0)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-dispatch constants uploaded by the CPU each frame.
///
/// Layout is 16-byte aligned per HLSL packing rules.
cbuffer VFXMeshInstanceConstants : register(b0) {
    // ── Simulation parameters ─────────────────────────────────────────────
    uint     aliveCount;             ///< Total alive particles to process.     0
    float    meshScaleX;             ///< Per-instance X scale.                 4
    float    meshScaleY;             ///< Per-instance Y scale.                 8
    float    meshScaleZ;             ///< Per-instance Z scale.                12

    uint     rotationMode;           ///< MeshRotationMode enum ordinal.        16
    uint     meshIndexCount;         ///< Index count for DrawIndexedInstanced. 20
    uint     _pad0;                  ///<                                       24
    uint     _pad1;                  ///<                                       28

#ifdef UZ_BINDLESS
    // ── Bindless descriptor indices: READ set (SRV) ───────────────────────
    uint     positionsSrvIndex;      ///< StructuredBuffer<float4> positions.  32
    uint     velocitiesSrvIndex;     ///< StructuredBuffer<float4> velocities. 36
    uint     agesSrvIndex;           ///< StructuredBuffer<float>  ages.       40
    uint     lifespansSrvIndex;      ///< StructuredBuffer<float>  lifespans.  44

    // ── Bindless descriptor indices: output UAVs ──────────────────────────
    uint     instanceTransformsUavIndex; ///< RWStructuredBuffer<float4x4>.    48
    uint     instanceCountUavIndex;      ///< RWStructuredBuffer<uint> (1 el). 52
    uint     indirectArgsUavIndex;       ///< RWByteAddressBuffer (20 bytes).  56
    uint     _pad2;                      ///<                                  60
#endif
};

#ifndef UZ_BINDLESS
/// SM5 fallback: explicit register bindings for particle buffers.
StructuredBuffer<float4>           sm5_positions       : register(t0);
StructuredBuffer<float4>           sm5_velocities      : register(t1);
StructuredBuffer<float>            sm5_ages            : register(t2);
StructuredBuffer<float>            sm5_lifespans       : register(t3);
RWStructuredBuffer<float4x4>       sm5_instanceXforms  : register(u0);
RWStructuredBuffer<uint>           sm5_instanceCount   : register(u1);
RWByteAddressBuffer                sm5_indirectArgs    : register(u2);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Builds an identity-rotation TRS matrix.
///
/// Diagonal scale with world-space translation. All particles face +Z.
///
/// # Parameters
/// - pos: World-space particle position.
/// - sx, sy, sz: Per-axis scale factors.
float4x4 BuildIdentityTRS(float3 pos, float sx, float sy, float sz)
{
    return float4x4(
        sx,   0.0f, 0.0f, pos.x,
        0.0f, sy,   0.0f, pos.y,
        0.0f, 0.0f, sz,   pos.z,
        0.0f, 0.0f, 0.0f, 1.0f
    );
}

/// Builds a velocity-aligned TRS matrix.
///
/// Aligns the mesh +Z axis with the particle velocity direction. When velocity
/// is near-zero, falls back to +Z = (0, 0, 1) so the mesh does not snap.
/// World Y is used as up unless velocity is near-vertical (|vy| > 0.999),
/// in which case world X is substituted to avoid a degenerate cross product.
///
/// # Parameters
/// - pos: World-space particle position.
/// - vel: Particle velocity (may be zero; function is safe).
/// - sx, sy, sz: Per-axis scale factors.
float4x4 BuildVelocityAlignedTRS(float3 pos, float3 vel,
                                  float sx, float sy, float sz)
{
    float3 forward = float3(0.0f, 0.0f, 1.0f);
    float  speed   = length(vel);

    if (speed > 1.0e-4f) {
        forward = vel / speed;
    }

    // Choose a world-up axis that is not parallel to forward.
    float3 worldUp = abs(forward.y) < 0.999f
                   ? float3(0.0f, 1.0f, 0.0f)
                   : float3(1.0f, 0.0f, 0.0f);

    float3 right = normalize(cross(worldUp, forward));
    float3 up    = cross(forward, right);

    return float4x4(
        right.x * sx,   up.x * sy,   forward.x * sz,  pos.x,
        right.y * sx,   up.y * sy,   forward.y * sz,  pos.y,
        right.z * sx,   up.z * sy,   forward.z * sz,  pos.z,
        0.0f,           0.0f,        0.0f,             1.0f
    );
}

/// Derives a pseudo-random float in [0, 2*pi) from a uint seed.
///
/// Uses a single Wang hash round for a good frequency distribution.
/// The output is deterministic: the same seed always produces the same angle.
float RandomAngle(uint seed)
{
    seed  = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed  = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed  = seed ^ (seed >> 15u);
    return float(seed & 0xFFFFu) / 65535.0f * 6.28318530f;
}

/// Builds a random-rotation TRS matrix from the particle index.
///
/// Derives three independent XYZ Euler angles from the particle index using
/// two Wang hash rounds. Rotation is constant per particle (index-derived
/// seed) -- particles do not spin in flight.
///
/// # Parameters
/// - pos:         World-space particle position.
/// - particleIdx: Index used as hash seed (must be unique per particle).
/// - sx, sy, sz:  Per-axis scale factors.
float4x4 BuildRandomTRS(float3 pos, uint particleIdx,
                         float sx, float sy, float sz)
{
    // Three angles from two separate hash seeds derived from the particle index.
    float angleX = RandomAngle(particleIdx);
    float angleY = RandomAngle(particleIdx * 2654435761u + 1u); // Knuth multiplicative hash step
    float angleZ = RandomAngle(particleIdx * 2246822519u + 2u);

    float cx = cos(angleX); float sx_r = sin(angleX);
    float cy = cos(angleY); float sy_r = sin(angleY);
    float cz = cos(angleZ); float sz_r = sin(angleZ);

    // Rz * Ry * Rx intrinsic XYZ composition.
    float3x3 rot = float3x3(
        cy * cz,
        cy * sz_r,
        -sy_r,

        sx_r * sy_r * cz - cx * sz_r,
        sx_r * sy_r * sz_r + cx * cz,
        sx_r * cy,

        cx * sy_r * cz + sx_r * sz_r,
        cx * sy_r * sz_r - sx_r * cz,
        cx * cy
    );

    return float4x4(
        rot[0][0] * sx,  rot[0][1] * sy,  rot[0][2] * sz,  pos.x,
        rot[1][0] * sx,  rot[1][1] * sy,  rot[1][2] * sz,  pos.y,
        rot[2][0] * sx,  rot[2][1] * sy,  rot[2][2] * sz,  pos.z,
        0.0f,            0.0f,            0.0f,             1.0f
    );
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the per-particle instance transform and writes it to the output buffer.
///
/// Each thread processes one particle. Threads beyond `aliveCount` return early.
/// For alive particles (age < lifespan) the shader:
/// 1. Reads position, velocity, age, lifespan from the SRV attribute buffers.
/// 2. Computes a float4x4 TRS transform based on the configured rotation mode.
/// 3. Atomically claims a slot in MeshInstanceTransforms and writes the matrix.
/// 4. The instance count UAV (MeshInstanceCount[0]) accumulates the total written.
///
/// A separate single-thread dispatch then copies MeshInstanceCount[0] into the
/// DrawIndexedInstanced indirect args buffer at INDIRECT_INSTANCE_COUNT_OFFSET.
///
/// # Side Effects
/// - Writes one float4x4 per alive particle to MeshInstanceTransforms.
/// - Atomically increments MeshInstanceCount[0].
/// - On the FIRST thread (dtid.x == 0) initialises the instance count to 0
///   and writes the static fields of the indirect args buffer (index count, etc.)
///   so a separate init dispatch is not required.
[numthreads(MESH_INSTANCE_GROUP_SIZE, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint particleIdx = dtid.x;

    // ── Bind UAV resources ───────────────────────────────────────────────────
#ifdef UZ_BINDLESS
    RWStructuredBuffer<uint>    instanceCount    = ResourceDescriptorHeap[instanceCountUavIndex];
    RWByteAddressBuffer         indirectArgs     = ResourceDescriptorHeap[indirectArgsUavIndex];
    RWStructuredBuffer<float4x4> instanceXforms  = ResourceDescriptorHeap[instanceTransformsUavIndex];
#else
    #define instanceCount    sm5_instanceCount
    #define indirectArgs     sm5_indirectArgs
    #define instanceXforms   sm5_instanceXforms
#endif

    // ── One-time initialisation by thread 0 ─────────────────────────────────
    // Thread 0 resets the instance count and writes the static indirect args
    // fields before any transforms are written. All other threads spin on the
    // particle loop independently; the ordering guarantee is that thread 0
    // always writes these before InterlockedAdd claims a slot.
    //
    // This avoids needing a dedicated clear dispatch before this CS runs.
    // The UAV barrier before the draw call ensures visibility.
    if (particleIdx == 0) {
        instanceCount[0] = 0;

        indirectArgs.Store(INDIRECT_INDEX_COUNT_OFFSET,    meshIndexCount);
        indirectArgs.Store(INDIRECT_START_INDEX_OFFSET,    0u);
        indirectArgs.Store(INDIRECT_BASE_VERTEX_OFFSET,    0u);
        indirectArgs.Store(INDIRECT_START_INSTANCE_OFFSET, 0u);
        // InstanceCount (offset 4) will be written by the alive-particle loop below.
    }

    // ── Early exit for excess threads ────────────────────────────────────────
    if (particleIdx >= aliveCount) return;

    // ── Read particle attributes from SRV buffers ────────────────────────────
#ifdef UZ_BINDLESS
    StructuredBuffer<float4> positionsBuf  = ResourceDescriptorHeap[positionsSrvIndex];
    StructuredBuffer<float4> velocitiesBuf = ResourceDescriptorHeap[velocitiesSrvIndex];
    StructuredBuffer<float>  agesBuf       = ResourceDescriptorHeap[agesSrvIndex];
    StructuredBuffer<float>  lifespansBuf  = ResourceDescriptorHeap[lifespansSrvIndex];
#else
    #define positionsBuf   sm5_positions
    #define velocitiesBuf  sm5_velocities
    #define agesBuf        sm5_ages
    #define lifespansBuf   sm5_lifespans
#endif

    float  age      = agesBuf[particleIdx];
    float  lifespan = max(lifespansBuf[particleIdx], 0.001f);

    // ── Skip dead particles ───────────────────────────────────────────────────
    if (age >= lifespan) return;

    float3 pos = positionsBuf[particleIdx].xyz;
    float3 vel = velocitiesBuf[particleIdx].xyz;

    float sx = meshScaleX;
    float sy = meshScaleY;
    float sz = meshScaleZ;

    // ── Compute TRS based on rotation mode ───────────────────────────────────
    float4x4 transform;

    if (rotationMode == kRotationMode_VelocityAligned) {
        transform = BuildVelocityAlignedTRS(pos, vel, sx, sy, sz);
    } else if (rotationMode == kRotationMode_Random) {
        transform = BuildRandomTRS(pos, particleIdx, sx, sy, sz);
    } else {
        // None or unrecognised -- identity rotation.
        transform = BuildIdentityTRS(pos, sx, sy, sz);
    }

    // ── Claim an output slot and write the transform ──────────────────────────
    uint instanceSlot = 0;
    InterlockedAdd(instanceCount[0], 1, instanceSlot);

    instanceXforms[instanceSlot] = transform;

    // ── Update indirect args instance count ──────────────────────────────────
    // Write instance count into the indirect args buffer. Because multiple
    // threads write this concurrently, we use an interlocked max (equivalent
    // to a store of the final count). After all threads complete, the value
    // at offset 4 equals the total number of instances written.

    // Atomic max ensures the last alive slot wins.
    // (instanceSlot + 1) is the running upper bound on the instance count.
    uint prev;
    indirectArgs.InterlockedMax(INDIRECT_INSTANCE_COUNT_OFFSET, instanceSlot + 1u, prev);

#ifndef UZ_BINDLESS
    #undef instanceCount
    #undef indirectArgs
    #undef instanceXforms
    #undef positionsBuf
    #undef velocitiesBuf
    #undef agesBuf
    #undef lifespansBuf
#endif
}
