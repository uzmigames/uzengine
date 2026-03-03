# Proposal: phase4-gpu-driven-rendering

## Why
The current renderer submits one draw call per visible mesh entity from the CPU. At 10,000 scene entities this becomes 10,000 CPU-side draw calls per frame, saturating the CPU and leaving the GPU starved. Modern hardware can process GPU-driven indirect draws at 100,000+ objects with minimal CPU overhead because the GPU generates its own draw call arguments in a compute shader. This is how AAA open-world games achieve large scene scales and is a prerequisite for Nanite-like virtual geometry in a later phase.

## What Changes
- Per-frame `InstanceDataBuffer` (StructuredBuffer) populated by the CPU with all entity world transforms and mesh IDs
- GPU occlusion culling compute shader reading `InstanceDataBuffer` and writing surviving draw arguments into `DrawIndirectBuffer`
- Hierarchical Z-Buffer (HZB) generation pass to feed the occlusion cull compute shader
- Replace per-entity draw loop with a single `ExecuteIndirect` call consuming `DrawIndirectBuffer`
- Bindless resource heap required: all textures and vertex/index buffers registered at startup for GPU-side access
- Mesh draw argument packing: `D3D12_DRAW_INDEXED_ARGUMENTS` emitted per surviving instance
- Debug counter readback to display culled vs. drawn object counts in the profiler overlay
- `gpu_instancing.md` spec prerequisite must be complete before this task begins

## Impact
- Affected specs: `docs/specs/renderer.md`, `docs/specs/gpu-instancing.md`, `docs/specs/debug-profiling.md`
- Affected code: `src/renderer/renderer.cpp`, `src/renderer/cull_pass.comp.hlsl`, `src/rhi/dx12/dx12_command_list.cpp`, `src/scene/scene_system.cpp`
- Breaking change: NO
- User benefit: Scene capacity scales from ~1,000 to 100,000+ entities with stable frame times, enabling large open worlds and dense environments on mid-range GPU hardware.
