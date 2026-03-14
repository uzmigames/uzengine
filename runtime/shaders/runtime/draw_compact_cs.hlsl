/// GPU draw list compaction compute shader.
///
/// Takes a visibility bitmask (1 per draw) and source indirect draw arguments,
/// produces a compact output buffer containing only visible draws. Designed to
/// run after `gpu_cull_cs.hlsl` to convert sparse visibility results into a
/// dense indirect argument buffer for `ExecuteIndirect`.
///
/// # Inputs
/// - `g_VisibilityMask` (t0): StructuredBuffer<uint> — 1 = visible, 0 = culled.
///   One entry per source draw command.
/// - `g_SourceDrawArgs` (t1): StructuredBuffer<DrawIndexedIndirectArgs> — full
///   set of draw commands (both visible and culled).
/// - Root constants (b0): total draw count.
///
/// # Outputs
/// - `g_CompactedDrawArgs` (u0): RWStructuredBuffer<DrawIndexedIndirectArgs> —
///   dense output containing only visible draws, in arbitrary order.
/// - `g_DrawCount` (u1): RWByteAddressBuffer — atomic counter at byte offset 0.
///   Must be cleared to 0 before dispatch. After dispatch, contains the number
///   of visible draws written to `g_CompactedDrawArgs`.
///
/// # Thread Group
/// [64, 1, 1] — one thread per source draw. Dispatch ceil(totalDraws / 64) groups.

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants
// ─────────────────────────────────────────────────────────────────────────────

cbuffer CompactConstants : register(b0)
{
    uint g_TotalDraws;     // Total draw commands in source buffer
    uint3 _pad;
};

// ─────────────────────────────────────────────────────────────────────────────
// Draw Command Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Mirrors D3D12_DRAW_INDEXED_ARGUMENTS / VkDrawIndexedIndirectCommand.
/// 5 DWORDs = 20 bytes per draw command.
struct DrawIndexedIndirectArgs
{
    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int  baseVertexLocation;
    uint startInstanceLocation;
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

StructuredBuffer<uint>                       g_VisibilityMask   : register(t0);
StructuredBuffer<DrawIndexedIndirectArgs>    g_SourceDrawArgs   : register(t1);
RWStructuredBuffer<DrawIndexedIndirectArgs>  g_CompactedDrawArgs : register(u0);
RWByteAddressBuffer                          g_DrawCount        : register(u1);

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= g_TotalDraws)
        return;

    if (g_VisibilityMask[dtid.x] != 0)
    {
        // Atomically allocate an output slot
        uint outputIndex;
        g_DrawCount.InterlockedAdd(0, 1, outputIndex);

        // Copy all 5 DWORDs of the draw args to the compacted buffer
        g_CompactedDrawArgs[outputIndex] = g_SourceDrawArgs[dtid.x];
    }
}
