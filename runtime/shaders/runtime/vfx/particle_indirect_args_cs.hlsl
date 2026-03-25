/// GPU Particle Indirect Arguments Compute Shader
///
/// Single-thread shader that reads the alive particle count from a GPU counter
/// buffer and writes both DrawIndexedIndirectArgs and DispatchIndirectArgs for
/// the next frame's rendering and simulation dispatches.
///
/// This shader runs after the update pass completes and before the draw call.
/// It bridges the GPU-side alive count to the indirect argument buffers consumed
/// by ExecuteIndirect (draw) and DispatchIndirect (next frame's update).
///
/// # Why a Separate Dispatch?
/// The alive count is only known on the GPU (written by atomic increments in
/// the spawn and update shaders). Rather than reading it back to the CPU
/// (which would stall the pipeline for 1+ frames), this shader writes the
/// indirect args directly on the GPU. Zero CPU readback required.
///
/// # Output Buffer Layout (single RWByteAddressBuffer)
/// Both argument structs are packed into a single buffer:
/// ```
/// Bytes [0..19]:   DrawIndexedInstancedIndirectArgs (5 x uint32)
///   Offset 0:  IndexCountPerInstance    = 6  (two triangles per billboard)
///   Offset 4:  InstanceCount            = aliveCount
///   Offset 8:  StartIndexLocation       = 0
///   Offset 12: BaseVertexLocation       = 0
///   Offset 16: StartInstanceLocation    = 0
///
/// Bytes [20..31]:  DispatchIndirectArgs (3 x uint32)
///   Offset 20: ThreadGroupCountX        = ceil(aliveCount / 256)
///   Offset 24: ThreadGroupCountY        = 1
///   Offset 28: ThreadGroupCountZ        = 1
/// ```
/// Total: 32 bytes.
///
/// # Bindless Resource Access (SM6.6)
/// Alive count buffer and indirect args buffer are accessed via
/// ResourceDescriptorHeap[index] using descriptor indices in the cbuffer.
///
/// # Thread Group
/// [1, 1, 1] -- single thread. Dispatch exactly (1, 1, 1) groups.
///
/// # Root Signature
/// - b0: VFXIndirectArgsConstants (bindless descriptor indices)
///
/// @ref D3D12 ExecuteIndirect argument buffer layout
/// @constants VERTICES_PER_PARTICLE=6, UPDATE_GROUP_SIZE=256
/// @deviations NONE

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Index count per billboard quad: 2 triangles x 3 vertices = 6.
/// Must match the billboard vertex shader's VERTICES_PER_PARTICLE.
static const uint VERTICES_PER_PARTICLE = 6;

/// Thread group size used by the update compute shader. Used to compute
/// the dispatch thread group count for next frame's simulation.
static const uint UPDATE_GROUP_SIZE = 256;

// ─────────────────────────────────────────────────────────────────────────────
// Byte Offsets into the Indirect Args Buffer
// ─────────────────────────────────────────────────────────────────────────────

/// DrawIndexedInstancedIndirectArgs field offsets (5 x uint32 = 20 bytes).
static const uint DRAW_INDEX_COUNT_OFFSET        = 0;   ///< IndexCountPerInstance
static const uint DRAW_INSTANCE_COUNT_OFFSET     = 4;   ///< InstanceCount
static const uint DRAW_START_INDEX_OFFSET        = 8;   ///< StartIndexLocation
static const uint DRAW_BASE_VERTEX_OFFSET        = 12;  ///< BaseVertexLocation
static const uint DRAW_START_INSTANCE_OFFSET     = 16;  ///< StartInstanceLocation

/// DispatchIndirectArgs field offsets, packed after draw args at byte 20.
static const uint DISPATCH_GROUP_X_OFFSET        = 20;  ///< ThreadGroupCountX
static const uint DISPATCH_GROUP_Y_OFFSET        = 24;  ///< ThreadGroupCountY
static const uint DISPATCH_GROUP_Z_OFFSET        = 28;  ///< ThreadGroupCountZ

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffer (b0) -- Bindless Descriptor Indices
// ─────────────────────────────────────────────────────────────────────────────

/// Constants for the indirect args shader. Contains descriptor indices to
/// read the alive count and write the indirect argument buffer.
///
/// Total: 16 bytes (1 x float4).
cbuffer VFXIndirectArgsConstants : register(b0) {
    uint     aliveCountSrvIndex;   ///< SRV index: StructuredBuffer<uint> (1 elem). 0
    uint     indirectArgsUavIndex; ///< UAV index: RWByteAddressBuffer (32 bytes).  4
    uint     _pad0;                ///<                                              8
    uint     _pad1;                ///<                                              12
};                                 ///<                                        Total: 16

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Reads the GPU-side alive particle count and writes indirect draw/dispatch
/// arguments for rendering and next-frame simulation.
///
/// # Side Effects
/// - Writes 8 uint32 values (32 bytes) to the indirect args RWByteAddressBuffer.
/// - No atomic operations needed (single thread, no contention).
[numthreads(1, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    // ── Read alive count from GPU counter ───────────────────────────────
    StructuredBuffer<uint> aliveCountBuf = ResourceDescriptorHeap[aliveCountSrvIndex];
    uint aliveCount = aliveCountBuf[0];

    // ── Write DrawIndexedInstancedIndirectArgs ──────────────────────────
    RWByteAddressBuffer indirectArgs = ResourceDescriptorHeap[indirectArgsUavIndex];

    indirectArgs.Store(DRAW_INDEX_COUNT_OFFSET,    VERTICES_PER_PARTICLE);
    indirectArgs.Store(DRAW_INSTANCE_COUNT_OFFSET,  aliveCount);
    indirectArgs.Store(DRAW_START_INDEX_OFFSET,     0);
    indirectArgs.Store(DRAW_BASE_VERTEX_OFFSET,     0);
    indirectArgs.Store(DRAW_START_INSTANCE_OFFSET,  0);

    // ── Write DispatchIndirectArgs for next frame's update pass ─────────
    // Ceiling division: (aliveCount + GROUP_SIZE - 1) / GROUP_SIZE.
    // Produces 0 groups when no particles are alive (no wasted dispatch).
    uint threadGroupCountX = (aliveCount + UPDATE_GROUP_SIZE - 1) / UPDATE_GROUP_SIZE;

    indirectArgs.Store(DISPATCH_GROUP_X_OFFSET, threadGroupCountX);
    indirectArgs.Store(DISPATCH_GROUP_Y_OFFSET, 1);
    indirectArgs.Store(DISPATCH_GROUP_Z_OFFSET, 1);
}
