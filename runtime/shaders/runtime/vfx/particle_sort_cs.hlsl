/// GPU Particle Sort Compute Shader
///
/// Bitonic merge sort for depth-ordering particles for correct alpha blending.
/// Particles are sorted back-to-front (descending distance from camera) so that
/// the farthest particle is drawn first and the nearest particle is drawn last,
/// producing correct alpha compositing.
///
/// # Algorithm
/// Two-phase bitonic merge sort:
///   Phase 0 (sortPhase == 0): Key generation + local bitonic sort within each
///     thread group. Each thread generates sort keys from particle positions,
///     then the group cooperatively sorts up to SORT_GROUP_SIZE elements using
///     shared memory.
///   Phase 1 (sortPhase == 1): Global bitonic merge across thread groups. The
///     CPU dispatches this repeatedly with increasing block/sub-block sizes
///     until the full array is sorted.
///
/// # Sort Key
/// `dot(particlePos - cameraPos, cameraForward)` — signed distance along the
/// camera's view direction. Converted to a sortable uint via sign-bit flip so
/// that unsigned integer comparison produces correct ordering of negative floats.
///
/// # Sort Order
/// Descending: largest key (farthest particle) at index 0, smallest key
/// (nearest particle) at the end. This gives back-to-front draw order for
/// correct alpha blending when iterating indices 0..N-1.
///
/// # Dead Particles
/// Particles beyond `particleCount` are assigned key 0x00000000 after the
/// sign-bit flip of the sentinel float +Inf (0x7F800000). Since the sort is
/// descending, these sort to the end of the array and are never drawn.
///
/// # Dispatch Strategy
///   Phase 0: ceil(paddedCount / SORT_GROUP_SIZE) groups — one dispatch.
///   Phase 1: ceil(paddedCount / SORT_GROUP_SIZE) groups per dispatch,
///     repeated for each (blockSize, subBlockSize) pair in the global merge.
///     Total Phase 1 dispatches: sum_{k=1}^{log2(paddedCount/SORT_GROUP_SIZE)} k.
///
/// # Bindless Resource Access (SM6.6)
/// Particle positions (SRV) and sort output buffers (UAV) are accessed via
/// ResourceDescriptorHeap[index] using descriptor indices in the constant buffer.
///
/// # Thread Group
/// [SORT_GROUP_SIZE, 1, 1] — one thread per element during local sort,
/// one thread per comparison pair during global merge.
///
/// # Root Signature
/// - b0: VFXSortConstants (camera params + bindless descriptor indices + merge params)
///
/// @ref Batcher 1968 "Sorting Networks and Their Applications" — bitonic merge sort
/// @ref Peters & Schulz-Hildebrandt 2023 "Fast GPU-based sorting"
/// @constants SORT_GROUP_SIZE=256
///
/// # SM5 Register Layout (non-bindless fallback)
/// - t0: StructuredBuffer<float4>           positions (xyz=pos, w=unused)
/// - u0: RWStructuredBuffer<uint>           sortKeys  (sort keys, read/write)
/// - u1: RWStructuredBuffer<uint>           sortIndices (sorted particle indices, read/write)

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Thread group size. Each thread handles one element during local sort and
/// one comparison-swap pair during global merge. Must be a power of two.
/// 256 provides good occupancy on all modern GPUs (GCN, RDNA, Turing, Ampere).
static const uint SORT_GROUP_SIZE = 256;

/// Sentinel key for dead/out-of-bounds particles. After FloatToSortableUint,
/// this value (derived from +Inf = 0x7F800000 -> 0xFF800000, but we want dead
/// particles at the END of a descending sort, so they need the SMALLEST key)
/// maps to 0x00000000 which is the smallest uint, sorting to the tail end in
/// a descending sort.
static const uint SORT_KEY_DEAD = 0x00000000;

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffer (b0) — Camera, Bindless Indices, Merge Parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Sort constants uploaded by the CPU each frame. Contains camera parameters
/// for sort key computation, descriptor indices for bindless buffer access,
/// and merge parameters for the global bitonic merge phase.
///
/// Layout is 16-byte aligned per HLSL packing rules. Total: 80 bytes.
cbuffer VFXSortConstants : register(b0) {
    // ── Particle and Phase Control ────────────────────────────────────────
    uint     particleCount;       ///< Total alive particles to sort.          0
    uint     sortPhase;           ///< 0 = local sort + keygen, 1 = global     4
                                  ///< merge.
    uint     paddedCount;         ///< particleCount rounded up to next        8
                                  ///< power of two (bitonic requires
                                  ///< power-of-two array size).
    uint     _pad0;               ///<                                         12

    // ── Camera Parameters for Sort Key ────────────────────────────────────
    float3   cameraPos;           ///< World-space camera position.            16
    float    _pad1;               ///<                                         28

    float3   cameraForward;       ///< World-space camera forward direction    32
                                  ///< (normalized).
    float    _pad2;               ///<                                         44

#ifdef UZ_BINDLESS
    // ── Bindless Descriptor Indices ───────────────────────────────────────
    uint     positionsSrvIndex;   ///< SRV: StructuredBuffer<float4>           48
                                  ///< particle positions (read only).
    uint     sortKeysUavIndex;    ///< UAV: RWStructuredBuffer<uint>           52
                                  ///< sort keys (read/write).
    uint     sortIndicesUavIndex; ///< UAV: RWStructuredBuffer<uint>           56
                                  ///< sorted particle indices (read/write).
    uint     _pad3;               ///<                                         60
#else
    uint     _pad3a;              ///<                                         48
    uint     _pad3b;              ///<                                         52
    uint     _pad3c;              ///<                                         56
    uint     _pad3d;              ///<                                         60
#endif

    // ── Global Merge Parameters (phase 1 only) ───────────────────────────
    uint     bitonicBlockSize;    ///< Current bitonic merge block size.       64
                                  ///< Power of two, starts at
                                  ///< 2 * SORT_GROUP_SIZE and doubles.
    uint     bitonicSubBlockSize; ///< Current sub-block size within the       68
                                  ///< merge block. Starts at
                                  ///< bitonicBlockSize / 2 and halves.
    uint     _pad4;               ///<                                         72
    uint     _pad5;               ///<                                         76
};                                ///<                                   Total: 80

#ifndef UZ_BINDLESS
/// SM5 fallback: explicit register bindings for sort buffers.
StructuredBuffer<float4>           sm5_positions    : register(t0);
RWStructuredBuffer<uint>           sm5_sortKeys     : register(u0);
RWStructuredBuffer<uint>           sm5_sortIndices  : register(u1);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Groupshared Memory (Phase 0 only)
// ─────────────────────────────────────────────────────────────────────────────

/// Shared memory for local bitonic sort. Each element stores a (key, index)
/// pair. Using two separate arrays avoids bank conflicts from struct padding.
/// Total shared memory: 256 * 4 * 2 = 2048 bytes.
groupshared uint gs_keys[SORT_GROUP_SIZE];
groupshared uint gs_indices[SORT_GROUP_SIZE];

// ─────────────────────────────────────────────────────────────────────────────
// Utility: Float-to-Sortable-Uint Conversion
// ─────────────────────────────────────────────────────────────────────────────

/// Converts an IEEE 754 float to a uint that preserves order under unsigned
/// integer comparison. Handles negative floats correctly by flipping all bits
/// for negatives (making them sort below positives) and flipping only the sign
/// bit for positives (shifting them above the flipped negatives).
///
/// The transformation:
///   - Positive float: flip sign bit (0x80000000 XOR)
///   - Negative float: flip ALL bits (0xFFFFFFFF XOR)
///
/// This ensures: -inf < -1.0 < -0.0 < +0.0 < +1.0 < +inf in uint domain.
///
/// @ref Herf 2001 "Radix Tricks" — IEEE float to sortable integer mapping
///
/// # Arguments
/// - `f`: input float value. May be any finite float, +/-inf, or +/-0.
///        NaN inputs produce undefined ordering (but will not crash).
///
/// # Returns
/// 32-bit unsigned integer that sorts in the same order as the input float.
uint FloatToSortableUint(float f)
{
    uint bits = asuint(f);
    // If the sign bit is set (negative), flip all bits.
    // If positive, flip only the sign bit.
    // This maps the entire float range to a monotonically increasing uint range.
    uint mask = -int(bits >> 31) | 0x80000000;
    return bits ^ mask;
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 0: Key Generation + Local Bitonic Sort
// ─────────────────────────────────────────────────────────────────────────────

/// Generates a sort key for a single particle by computing the signed distance
/// along the camera view direction, then converts to sortable uint. For
/// descending order (back-to-front), the key is bitwise-inverted so that
/// larger distances produce larger uint keys.
///
/// # Arguments
/// - `particleIdx`:   index into the positions buffer.
/// - `positionsBuf`:  bindless SRV for particle positions.
///
/// # Returns
/// Sortable uint key. Descending: largest distance -> largest key value.
uint GenerateSortKey(uint particleIdx, StructuredBuffer<float4> positionsBuf)
{
    float3 pos = positionsBuf[particleIdx].xyz;
    float viewDist = dot(pos - cameraPos, cameraForward);
    uint key = FloatToSortableUint(viewDist);
    // Invert for descending order: farthest particle gets the largest uint key.
    // After inversion, unsigned sort (ascending) produces descending distance order.
    return ~key;
}

/// Performs a compare-and-swap in groupshared memory, caching both key and
/// index values into registers before any writes to avoid read-after-write
/// hazards on groupshared memory.
///
/// # Arguments
/// - `idxA`: first element index in groupshared arrays.
/// - `idxB`: second element index in groupshared arrays. Must be > idxA.
/// - `ascending`: true to sort so that gs_keys[idxA] <= gs_keys[idxB].
void CompareAndSwapLocal(uint idxA, uint idxB, bool ascending)
{
    uint keyA = gs_keys[idxA];
    uint keyB = gs_keys[idxB];
    uint valA = gs_indices[idxA];
    uint valB = gs_indices[idxB];

    // Swap if the pair is out of order relative to the desired direction.
    // For ascending: we want keyA <= keyB. If keyA > keyB, swap.
    if ((keyA > keyB) == ascending)
    {
        gs_keys[idxA]    = keyB;
        gs_keys[idxB]    = keyA;
        gs_indices[idxA] = valB;
        gs_indices[idxB] = valA;
    }
}

/// Phase 0 entry: generates sort keys from particle positions and performs
/// a complete bitonic sort within the thread group using groupshared memory.
///
/// Each thread loads one (key, index) pair into shared memory, then
/// participates in log2(SORT_GROUP_SIZE) stages of bitonic sorting,
/// each stage consisting of multiple sub-passes with decreasing stride.
///
/// After sorting, results are written to the output UAV buffers.
///
/// # Arguments
/// - `groupIdx`:     SV_GroupID.x — identifies which block of particles.
/// - `localIdx`:     SV_GroupIndex — thread index within the group [0..255].
/// - `globalIdx`:    SV_DispatchThreadID.x — global thread/element index.
void LocalSortPhase(uint groupIdx, uint localIdx, uint globalIdx)
{
    // ── Bind buffers ──────────────────────────────────────────────────────
#ifdef UZ_BINDLESS
    StructuredBuffer<float4> positions = ResourceDescriptorHeap[positionsSrvIndex];
    RWStructuredBuffer<uint> outKeys   = ResourceDescriptorHeap[sortKeysUavIndex];
    RWStructuredBuffer<uint> outVals   = ResourceDescriptorHeap[sortIndicesUavIndex];
#else
    #define positions sm5_positions
    #define outKeys   sm5_sortKeys
    #define outVals   sm5_sortIndices
#endif

    // ── Load: generate sort key or assign dead sentinel ───────────────────
    if (globalIdx < particleCount)
    {
        gs_keys[localIdx]    = GenerateSortKey(globalIdx, positions);
        gs_indices[localIdx] = globalIdx;
    }
    else
    {
        // Out-of-bounds elements get the smallest possible key so they sort
        // to the end in our ascending-uint (descending-distance) order.
        gs_keys[localIdx]    = SORT_KEY_DEAD;
        gs_indices[localIdx] = 0xFFFFFFFF;
    }

    GroupMemoryBarrierWithGroupSync();

    // ── Bitonic sort within thread group ──────────────────────────────────
    // log2(SORT_GROUP_SIZE) = 8 stages for SORT_GROUP_SIZE = 256.
    // Stage k: blocks of size 2^(k+1), with sub-passes of stride 2^k down to 1.
    //
    // The bitonic sorting network compares element pairs at specific offsets.
    // In each stage, the sort direction alternates per block to build bitonic
    // sequences, then each sub-pass merges pairs with decreasing stride.
    for (uint blockSize = 2; blockSize <= SORT_GROUP_SIZE; blockSize <<= 1)
    {
        for (uint stride = blockSize >> 1; stride > 0; stride >>= 1)
        {
            // Compute the partner index for this thread's comparison
            uint pairIdx = localIdx ^ stride;

            // Only the thread with the lower index in each pair does the swap
            // to avoid double-swapping.
            if (pairIdx > localIdx)
            {
                // Determine sort direction for this block.
                // Within a block of size `blockSize`, ascending if the element's
                // block position (localIdx / blockSize) is even.
                bool ascending = ((localIdx & blockSize) == 0);

                CompareAndSwapLocal(localIdx, pairIdx, ascending);
            }

            GroupMemoryBarrierWithGroupSync();
        }
    }

    // ── Store sorted results to output buffers ────────────────────────────
    outKeys[globalIdx] = gs_keys[localIdx];
    outVals[globalIdx] = gs_indices[localIdx];

#ifndef UZ_BINDLESS
    #undef positions
    #undef outKeys
    #undef outVals
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 1: Global Bitonic Merge
// ─────────────────────────────────────────────────────────────────────────────

/// Phase 1 entry: performs one compare-and-swap step of the global bitonic
/// merge network. The CPU dispatches this for each (blockSize, subBlockSize)
/// pair that operates across thread group boundaries.
///
/// Each thread handles one comparison pair. The pair indices are determined
/// by the current bitonicBlockSize and bitonicSubBlockSize parameters set
/// by the CPU.
///
/// # Arguments
/// - `globalIdx`: SV_DispatchThreadID.x — identifies one element of a
///                comparison pair.
void GlobalMergePhase(uint globalIdx)
{
    // ── Bind buffers (read-modify-write) ──────────────────────────────────
#ifdef UZ_BINDLESS
    RWStructuredBuffer<uint> keys = ResourceDescriptorHeap[sortKeysUavIndex];
    RWStructuredBuffer<uint> vals = ResourceDescriptorHeap[sortIndicesUavIndex];
#else
    #define keys sm5_sortKeys
    #define vals sm5_sortIndices
#endif

    // ── Compute comparison pair ───────────────────────────────────────────
    // For a sub-block of size `bitonicSubBlockSize`, each thread computes
    // which element it should compare with. The XOR trick from bitonic sort:
    // partner = element ^ stride, where stride = bitonicSubBlockSize.
    uint halfBlock = bitonicSubBlockSize;
    uint pairIdx   = globalIdx ^ halfBlock;

    // Only process if partner is within bounds and this thread is the lower index
    if (pairIdx > globalIdx && pairIdx < paddedCount)
    {
        // Sort direction: ascending within even-numbered blocks of bitonicBlockSize
        bool ascending = ((globalIdx & bitonicBlockSize) == 0);

        uint keyA = keys[globalIdx];
        uint keyB = keys[pairIdx];
        uint valA = vals[globalIdx];
        uint valB = vals[pairIdx];

        // Swap if out of order for the desired direction
        if ((keyA > keyB) == ascending)
        {
            keys[globalIdx] = keyB;
            keys[pairIdx]   = keyA;
            vals[globalIdx] = valB;
            vals[pairIdx]   = valA;
        }
    }

#ifndef UZ_BINDLESS
    #undef keys
    #undef vals
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Unified entry point. Behavior selected by `sortPhase` constant:
///   0 = Local sort + key generation (Phase 0)
///   1 = Global bitonic merge (Phase 1)
///
/// # Dispatch (CPU side)
/// Phase 0: Dispatch(ceil(paddedCount / SORT_GROUP_SIZE), 1, 1)
/// Phase 1: Dispatch(ceil(paddedCount / SORT_GROUP_SIZE), 1, 1)
///   — one thread per element; half early-exit via pairIdx > globalIdx check.
///   Repeated for each (blockSize, subBlockSize) global merge step.
///
/// # CPU Dispatch Loop for Full Sort
/// ```
/// // Phase 0: local sort within each thread group
/// sortCB.sortPhase = 0;
/// Dispatch(ceil(paddedCount / 256), 1, 1);
/// UAVBarrier();
///
/// // Phase 1: global merge passes across thread groups
/// sortCB.sortPhase = 1;
/// uint groupCount = ceil(paddedCount / 256);
/// for (blockSize = 512; blockSize <= paddedCount; blockSize <<= 1) {
///     for (subBlockSize = blockSize >> 1; subBlockSize > 0; subBlockSize >>= 1) {
///         sortCB.bitonicBlockSize = blockSize;
///         sortCB.bitonicSubBlockSize = subBlockSize;
///         Dispatch(groupCount, 1, 1);
///         UAVBarrier();
///     }
/// }
/// ```
[numthreads(SORT_GROUP_SIZE, 1, 1)]
void main(
    uint3 groupId  : SV_GroupID,
    uint  localIdx : SV_GroupIndex,
    uint3 dtid     : SV_DispatchThreadID)
{
    uint globalIdx = dtid.x;

    if (sortPhase == 0)
    {
        LocalSortPhase(groupId.x, localIdx, globalIdx);
    }
    else
    {
        GlobalMergePhase(globalIdx);
    }
}
