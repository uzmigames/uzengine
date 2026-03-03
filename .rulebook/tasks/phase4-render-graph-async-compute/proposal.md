# Proposal: phase2-render-graph-async-compute

## Why
The render graph currently submits all GPU work (SSAO, TAA, skinning compute) onto the graphics queue sequentially, leaving the async compute queue idle. Modern discrete GPUs expose a dedicated async compute queue that can overlap compute workloads with graphics rasterization, recovering 30-50% of GPU time that is otherwise wasted waiting. Without async compute, the engine cannot saturate GPU resources on any title that mixes compute-heavy post-processing with rasterization-heavy draw calls.

DX12 exposes this capability through separate `ID3D12CommandQueue` instances with `D3D12_COMMAND_LIST_TYPE_COMPUTE`. Overlapping a GBuffer rasterisation pass (which is heavily bandwidth-limited on the ROPs) with an SSAO compute dispatch (which is compute-limited and uses the same bandwidth to read depth) is the canonical use case: the two workloads compete for different GPU resources and execute simultaneously with zero interference. On RDNA2 and later AMD GPUs and on NVIDIA Ampere and later, async overlap for this pairing typically recovers 20-40% of the SSAO cost.

The render graph DAG already tracks resource lifetimes and pass dependencies — this is the exact information needed to schedule async compute correctly. Extending the graph with queue annotations and cross-queue fence synchronisation at dependency edges means the scheduler can automatically determine when two passes can overlap and when they must serialize, without pass authors needing to reason about queue synchronisation directly. This makes async compute an engine capability rather than a per-feature engineering effort.

## What Changes
- Add `RHIComputeQueue` in `src/rhi/rhi_compute_queue.h` wrapping an `ID3D12CommandQueue` of type `D3D12_COMMAND_LIST_TYPE_COMPUTE`, with `Submit`, `Signal`, and `Wait` methods mirroring the graphics queue interface
- Extend `RenderPassDesc` in `src/renderer/render_graph.h` with `ExecutionQueue queue = ExecutionQueue::GRAPHICS` field; valid values are `GRAPHICS` and `ASYNC_COMPUTE`
- Implement cross-queue dependency resolution in `RenderGraph::Compile()`: when an edge connects passes on different queues, insert a `Signal` on the producing queue and a `Wait` on the consuming queue using a dedicated `ID3D12Fence` per dependency edge
- Add `RenderGraph::ScheduleAsyncOverlap()` — a scheduling phase after topological sort that identifies passes eligible for async overlap (no shared resource writes in the same frame window) and assigns them to the compute queue
- Move `SsaoPass`, `TaaResolvePass`, and `GpuSkinningPass` to `ExecutionQueue::ASYNC_COMPUTE` in their respective `Register()` calls; update resource transition states to `D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE` on the compute queue
- Add `ID3D12Fence` double-buffer synchronisation: each async compute dispatch signals a fence on completion that the graphics queue waits on before consuming its outputs, preventing WAR hazards on shared resources
- Add Tracy GPU zone markers for the async compute queue using `TracyD3D12Zone` with a separate `TracyD3D12Ctx` so frame captures show both queues as parallel timelines
- Expose `r.async_compute 0/1` CVar to disable async compute globally for driver bug workarounds; expose `r.async_compute_debug 1` to log which passes are executing on each queue each frame
- Add `r.async_compute_passes` console command that prints the current frame's pass schedule with queue assignments and dependency edges for debugging the scheduler
- Write tests in `tests/renderer/render_graph_async_test.cpp` that verify fence insertion at cross-queue edges, correct queue assignment propagation, and that disabling the CVar collapses all passes to the graphics queue

## Impact
- Affected specs: `docs/specs/renderer.md`, `docs/specs/screen-space-effects.md`, `docs/specs/skeletal-mesh.md`, `docs/specs/debug-profiling.md`
- Affected code: `src/rhi/rhi_compute_queue.h`, `src/rhi/rhi_compute_queue.cpp`, `src/renderer/render_graph.h`, `src/renderer/render_graph.cpp`, `src/renderer/passes/ssao_pass.cpp`, `src/renderer/passes/taa_pass.cpp`, `src/renderer/skinning_pass.cpp`
- Breaking change: NO — existing passes default to `GRAPHICS` queue and require no changes; async compute is opt-in per pass
- User benefit: Scenes with SSAO and skinned characters recover measurable GPU frame time at no CPU cost; Tracy profiling captures show async overlap confirming the GPU is more fully utilised; the feature can be disabled per-driver with a single CVar
