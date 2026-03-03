# Proposal: phase2-occlusion-culling-hzb

## Why
There is currently no occlusion culling in the engine. Every entity in the frustum is submitted for rendering regardless of whether it is behind walls, terrain, or other opaque geometry. In complex scenes (indoor environments, dense cities) this causes 30-70% of draw calls to shade pixels that are never visible. Hierarchical Z-Buffer (HZB) occlusion culling is the standard AAA technique that solves this on the GPU without expensive CPU readbacks per entity.

The HZB approach is preferred over CPU software rasterization because it avoids round-trip latency: the depth mip chain is built entirely on the GPU from the previous frame's depth buffer, AABB tests run as a compute dispatch, and the results feed directly into the GPU-driven indirect draw arguments — all without any CPU-GPU synchronisation stall. The accepted one-frame temporal lag means a newly visible object appears one frame late, which is imperceptible in practice and the standard trade-off used by Unreal Engine, Frostbite, and every major AAA renderer.

Combining HZB culling with the existing frustum cull reduces visible draw calls in interior-heavy maps by 60-80%. For a scene with 5000 draw calls this can collapse the GBuffer pass from a bottleneck to a non-issue, enabling richer geometry budgets without hitting GPU time limits. The system also lays the groundwork for GPU-driven rendering where the entire draw list is generated on the GPU without any CPU iteration over entities.

## What Changes
- Add `HzbPass` in `src/renderer/passes/hzb_pass.cpp` that samples the resolved depth buffer and produces a full mip chain (`log2(max(w,h))` levels) using a single compute dispatch with 8x8 thread groups
- Use `SPD` (Single Pass Downsampler from AMD GPUOpen, available via vcpkg) for the mip chain generation to avoid multiple dispatches; expose a `r.hzb_spd 0/1` CVar to fall back to a naive multi-pass downsample for debugging
- Write `shaders/hzb_occlusion_test.hlsl`: a compute shader that reads each entity's world-space AABB from a `StructuredBuffer<HzbInstance>`, reprojects the 8 corners into NDC using the previous frame's view-projection, selects the appropriate HZB mip level by AABB screen-space footprint, and writes 0/1 into a `RWStructuredBuffer<uint> g_OcclusionResults`
- Store per-entity occlusion results in a persistent GPU `StructuredBuffer<uint>` indexed by entity draw ID, double-buffered to avoid read/write hazards between frames
- Modify `DrawSubmitter` in `src/renderer/draw_submitter.cpp` to consume the previous frame's occlusion buffer when building `DrawIndexedIndirect` argument buffers; culled entities produce a draw call with zero instance count rather than being omitted, avoiding index remapping
- Populate `HzbInstance` structs in `src/renderer/culling.cpp` during the existing frustum cull pass, reusing the AABB data already computed for frustum rejection
- Integrate AABB data into the GPU-driven indirect draw path so the test runs entirely on the GPU with no CPU stall; entities not yet present in the AABB buffer (first-frame case) default to visible
- Add a two-phase occlusion test: phase 1 tests with last frame's HZB, phase 2 (optional, `r.hzb_two_phase 1`) re-renders newly visible objects in a second pass to eliminate one-frame pop-in on fast camera cuts
- Add Tracy GPU zone markers for the HZB build pass and occlusion test pass; add CPU-side Tracy counters for `hzb_culled_objects`, `hzb_visible_objects`, and `hzb_cull_ratio` updated each frame
- Add a debug overlay (`r.hzb_debug 1`) that visualises the HZB mip 0 as a screen-space overlay for shader debugging

## Impact
- Affected specs: `docs/specs/renderer.md`, `docs/specs/gpu-instancing.md`, `docs/specs/debug-profiling.md`
- Affected code: `src/renderer/draw_submitter.cpp`, `src/renderer/render_graph.cpp`, `src/renderer/culling.cpp`, `src/renderer/passes/hzb_pass.cpp`, `src/renderer/passes/hzb_pass.h`, `src/shaders/hzb_occlusion_test.hlsl`, `src/rhi/rhi_buffers.cpp`
- Breaking change: NO — HZB culling is additive to the existing frustum cull; it can be disabled with `r.hzb_enabled 0`
- User benefit: Complex indoor scenes and dense open worlds render significantly fewer draw calls per frame; developers get per-frame culled/visible object counters and cull ratio in Tracy profiling; no CPU-GPU sync stall means the gain is pure GPU time savings
