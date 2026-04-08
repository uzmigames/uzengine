/// UzEngine SSR Prepare Indirect Args — converts ray count to dispatch arguments.
/// @ref AMD 2020 "FidelityFX SSSR" — PrepareIndirectArgs pass
///
/// Single-thread dispatch [1,1,1] that reads the atomic ray count from
/// the tile classify pass and writes RHIDispatchIndirectArgs for the trace pass.
/// Also resets the counter to 0 for the next frame.

cbuffer SsrPrepareRootCB : register(b0) {
    uint counterArgsUavIdx;  // 0 — bindless UAV index for ray counter + args buffer
};

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    RWByteAddressBuffer counterBuf = ResourceDescriptorHeap[counterArgsUavIdx];

    uint rayCount = counterBuf.Load(0);

    // Write dispatch args: ceil(rayCount / 64) groups of 64 threads each.
    // The trace pass uses [numthreads(8,8,1)] with 2D pixel mapping, but when
    // using indirect dispatch from the ray list, it would use [numthreads(64,1,1)].
    // For now, keep the existing 8x8 dispatch pattern and use the ray count as info.
    uint groupCountX = (rayCount + 63) / 64;

    // RHIDispatchIndirectArgs at byte offset 4 (after the ray count at offset 0)
    counterBuf.Store(4, groupCountX);
    counterBuf.Store(8, 1);   // Y = 1
    counterBuf.Store(12, 1);  // Z = 1

    // Reset counter to 0 for next frame's classify pass.
    counterBuf.Store(0, 0);
}
