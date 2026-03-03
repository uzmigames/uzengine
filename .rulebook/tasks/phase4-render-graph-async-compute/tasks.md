## 1. RHI Compute Queue
- [ ] 1.1 Create `src/rhi/rhi_compute_queue.h` declaring `RHIComputeQueue` struct with `Submit`, `Signal`, and `Wait` method signatures matching the graphics queue interface
- [ ] 1.2 Create `src/rhi/rhi_compute_queue.cpp` implementing `RHIComputeQueue` wrapping an `ID3D12CommandQueue` created with `D3D12_COMMAND_LIST_TYPE_COMPUTE`
- [ ] 1.3 Add `RHIComputeQueue* g_compute_queue` global instance initialized alongside the graphics queue in `src/rhi/rhi_dx12.c` device creation
- [ ] 1.4 Implement `RHIComputeQueue::Signal(ID3D12Fence*, uint64_t value)` calling `ID3D12CommandQueue::Signal` on the compute queue
- [ ] 1.5 Implement `RHIComputeQueue::Wait(ID3D12Fence*, uint64_t value)` calling `ID3D12CommandQueue::Wait` on the compute queue

## 2. RenderPassDesc Queue Annotation
- [ ] 2.1 Add `ExecutionQueue` enum to `src/renderer/render_graph.h` with values `GRAPHICS` and `ASYNC_COMPUTE`
- [ ] 2.2 Add `ExecutionQueue queue = ExecutionQueue::GRAPHICS` field to `RenderPassDesc` struct in `src/renderer/render_graph.h`
- [ ] 2.3 Verify all existing `RenderPassDesc` instantiations compile without change (default field ensures backwards compatibility)

## 3. Cross-Queue Dependency Resolution in Compile()
- [ ] 3.1 Extend `RenderGraph::Compile()` in `src/renderer/render_graph.cpp` to detect dependency edges that cross queue boundaries (producer queue != consumer queue)
- [ ] 3.2 Allocate a dedicated `ID3D12Fence` per cross-queue dependency edge during `Compile()`, storing it in a `CrossQueueFence` struct alongside the signal and wait values
- [ ] 3.3 Insert `Signal` call on the producing queue and `Wait` call on the consuming queue for each `CrossQueueFence` detected during compilation
- [ ] 3.4 Ensure WAR (write-after-read) hazards on shared resources are prevented: consuming-queue pass waits on the fence before reading output written by the producing-queue pass

## 4. Async Overlap Scheduler
- [ ] 4.1 Implement `RenderGraph::ScheduleAsyncOverlap()` in `src/renderer/render_graph.cpp` as a post-topological-sort phase
- [ ] 4.2 In `ScheduleAsyncOverlap()`, identify passes eligible for async overlap: passes with `queue == ASYNC_COMPUTE` that have no shared resource writes with any concurrently-executing graphics pass in the same frame window
- [ ] 4.3 Assign identified eligible passes to the compute queue submission list; leave ineligible passes on the graphics queue

## 5. Pass Migration to Async Compute
- [ ] 5.1 Update `SsaoPass::Register()` in `src/renderer/passes/ssao_pass.cpp` to set `desc.queue = ExecutionQueue::ASYNC_COMPUTE`
- [ ] 5.2 Update resource transition states in `SsaoPass` to use `D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE` on the compute queue path
- [ ] 5.3 Update `TaaResolvePass::Register()` in `src/renderer/passes/taa_pass.cpp` to set `desc.queue = ExecutionQueue::ASYNC_COMPUTE`
- [ ] 5.4 Update resource transition states in `TaaResolvePass` to use `D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE` on the compute queue path
- [ ] 5.5 Update `GpuSkinningPass::Register()` in `src/renderer/skinning_pass.cpp` to set `desc.queue = ExecutionQueue::ASYNC_COMPUTE`
- [ ] 5.6 Update resource transition states in `GpuSkinningPass` to use `D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE` on the compute queue path

## 6. Double-Buffer Fence Synchronisation
- [ ] 6.1 Implement per-frame double-buffer fence logic: async compute dispatch signals a frame-indexed fence on completion
- [ ] 6.2 Insert graphics queue `Wait` on the async compute completion fence before consuming outputs of any async compute pass within the same frame

## 7. Tracy GPU Profiling
- [ ] 7.1 Create a separate `TracyD3D12Ctx` for the compute queue in `src/renderer/render_graph.cpp` so Tracy frame captures show both queues as parallel timelines
- [ ] 7.2 Wrap each async compute pass dispatch with `TracyD3D12Zone` markers using the compute queue Tracy context

## 8. CVars and Console Commands
- [ ] 8.1 Register `r.async_compute` CVar (int, default 1) in the CVar system; when set to 0, `ScheduleAsyncOverlap()` skips all queue reassignments and submits all passes on the graphics queue
- [ ] 8.2 Register `r.async_compute_debug` CVar (int, default 0); when set to 1, log per-frame which passes execute on each queue and which cross-queue fence insertions occurred
- [ ] 8.3 Register `r.async_compute_passes` console command that prints the current frame's full pass schedule with queue assignments and dependency edges to the console

## 9. Testing
- [ ] 9.1 Create `tests/renderer/render_graph_async_test.cpp` with Google Test suite `RenderGraphAsyncComputeTest`
- [ ] 9.2 Write test `FenceInsertedAtCrossQueueEdge`: build a render graph with one GRAPHICS pass and one ASYNC_COMPUTE pass with a dependency between them; verify `Compile()` generates exactly one `CrossQueueFence`
- [ ] 9.3 Write test `NoFenceForSameQueueEdge`: build a graph with two GRAPHICS passes with a dependency; verify `Compile()` generates zero cross-queue fences
- [ ] 9.4 Write test `AsyncComputeDisabledCVar`: set `r.async_compute = 0`, build graph with ASYNC_COMPUTE passes, run `ScheduleAsyncOverlap()`; verify all passes are assigned to the graphics queue
- [ ] 9.5 Write test `QueueAssignmentPropagation`: verify that a pass explicitly set to `ASYNC_COMPUTE` retains that assignment after `Compile()` when `r.async_compute = 1`
- [ ] 9.6 Write test `WARHazardFenceOrdering`: verify that the cross-queue fence signal value on the compute queue is lower than the wait value on the graphics queue (correct ordering)
- [ ] 9.7 Verify line coverage >= 95% and branch coverage >= 95% for `render_graph.cpp` and `rhi_compute_queue.cpp`

## 10. Documentation
- [ ] 10.1 Update `docs/specs/renderer.md` with the `ExecutionQueue` enum, `ScheduleAsyncOverlap()` algorithm description, and cross-queue fence protocol
- [ ] 10.2 Update `docs/specs/debug-profiling.md` with the separate Tracy compute queue context and how to read async overlap in frame captures
