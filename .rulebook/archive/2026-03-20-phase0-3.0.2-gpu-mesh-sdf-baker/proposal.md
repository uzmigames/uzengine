# Proposal: GPU Mesh SDF Baker — Move SDF Generation from CPU to Compute Shader

## Why

The entire Mesh SDF baking pipeline currently runs on **CPU single-threaded** (`mesh_sdf_baker.cpp`). This includes:

1. **Triple-nested voxel loop** (32^3-64^3 iterations) computing closest-triangle distance per voxel — embarrassingly parallel but runs serially on one core
2. **Spatial hash construction** using `std::vector<std::vector<uint32_t>>` — CPU-only jagged data structure
3. **Float32-to-Float16 conversion** via software `FloatToHalf()` in another triple-nested loop
4. **`WaitIdle()` stall** after each mesh SDF upload — blocks the entire render thread waiting for GPU to finish a single buffer copy

UE5 does the entire mesh SDF generation on GPU via `GenerateMeshSDFCS.usf`. The per-voxel distance computation is embarrassingly parallel — each voxel is independent. A compute shader with 4x4x4 thread groups over a 64^3 grid would be ~1000x faster than the current CPU path.

### Performance Impact

| Metric | Current (CPU) | Target (GPU) |
|--------|---------------|--------------|
| 32^3 mesh SDF bake | ~50-200ms (single thread) | <1ms (compute shader) |
| 64^3 mesh SDF bake | ~400-1600ms (single thread) | <2ms (compute shader) |
| Render thread stall per mesh | Full `WaitIdle()` | Zero (async) |
| F32→F16 conversion | Software loop | Native GPU on R16F UAV write |

### What Stays on CPU

- GDF clipmap center snapping (4 iterations, trivial)
- RadianceCache grid snapping (trivial)
- Constant buffer fills (inherently CPU)
- Dispatch decisions (CPU must issue Dispatch calls)

## What Changes

### 1. New GPU Bake Pipeline

Replace the CPU `BakeMesh()` with a GPU compute path:

1. **Upload triangle data** — `StructuredBuffer<GPUTriangle>` with positions + normals
2. **Dispatch compute shader** — `mesh_sdf_bake_cs.hlsl` with `[numthreads(4,4,4)]`, one thread per voxel
3. **Output directly to R16F UAV** — no staging buffer, no F32→F16 conversion needed
4. **No WaitIdle()** — use fence-based async completion

### 2. Eliminate WaitIdle() Stall

Replace the synchronous `WaitIdle()` after SDF upload with:
- Fence per upload operation
- Check fence at next frame boundary
- SDF marked as "pending" until fence signals
- GDF composite skips pending SDFs

### 3. Precompute Inverse Transforms

Cache `Mat4::Inverse()` in `MeshSDF` struct, recomputed only when transform changes. Eliminates per-frame inverse computation in `CompositeFromMeshSDFs()`.

## Impact

- **Affected code**: `runtime/engine/renderer/gi/mesh_sdf_baker.h/.cpp`, `runtime/engine/renderer/gi/global_distance_field.cpp`
- **New shader**: `runtime/shaders/runtime/gi/mesh_sdf_bake_cs.hlsl` (already exists as stub from Phase A task 1.4)
- **Affected specs**: `docs/specs/rendering/gi-system.md` (pending creation)
- **Breaking change**: NO — same `MeshSDF` output struct, GPU bake is transparent to consumers
- **Risk**: Low — the existing CPU baker serves as reference/fallback for validation
