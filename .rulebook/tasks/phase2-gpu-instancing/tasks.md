## 1. Data Structures

- [ ] 1.1 Create `src/renderer/instancing/instanced_draw.h` -- `InstancedDrawCall` struct (mesh, material, variant_key, instance_count, transform_buffer, transform_offset)
- [ ] 1.2 Create `BatchKey` struct: `{Handle<MeshBuffer>, Handle<Material>, ShaderVariantKey}` with hash and equality
- [ ] 1.3 Ensure BatchKey hash has good distribution (test with 1000+ unique keys)

## 2. BatchBuilder

- [ ] 2.1 Create `src/renderer/instancing/batch_builder.h/cpp` -- `BatchBuilder` class
- [ ] 2.2 `build(Span<const VisibleEntity>, Arena* frame_arena) -> Array<InstancedDrawCall>`
- [ ] 2.3 Grouping: HashMap<BatchKey, Array<mat4>> cleared each frame
- [ ] 2.4 For each visible entity: compute BatchKey, append world mat4 to group
- [ ] 2.5 For each group: allocate InstancedDrawCall, set instance_count = group.size()
- [ ] 2.6 Sort output by PSO to minimize state changes between draw calls
- [ ] 2.7 Respect `min_instances_to_batch` config (default 2): single entities use non-instanced path
- [ ] 2.8 Respect `max_instances_per_batch` config (default 4096): split oversized batches

## 3. GPU Transform Upload

- [ ] 3.1 For each batch: allocate from upload ring buffer (`instance_count * 64 bytes`)
- [ ] 3.2 `memcpy` mat4 array to mapped upload buffer
- [ ] 3.3 Create SRV descriptor for `StructuredBuffer<float4x4>` at register t7
- [ ] 3.4 Verify upload ring buffer has sufficient budget (check `transform_buffer_budget_mb` config)
- [ ] 3.5 If budget exceeded: log warning, fall back to non-instanced draws for remaining entities

## 4. Shader INSTANCED Variant

- [ ] 4.1 Add `INSTANCED` to shader variant define categories (auto-detected from batch)
- [ ] 4.2 Update `shaders/builtin/pbr.hlsl`: `#ifdef INSTANCED` path reads `StructuredBuffer<float4x4>` at t7 via `SV_InstanceID`
- [ ] 4.3 Non-instanced path: reads `cbuffer PerObject : register(b0)` as before
- [ ] 4.4 Update `shaders/builtin/shadow_depth.hlsl` with same `INSTANCED` branching
- [ ] 4.5 Verify both paths produce identical visual output

## 5. Draw Call Submission

- [ ] 5.1 Create `submit_instanced_draw()` function: bind VB/IB, set INSTANCED PSO, bind transform SRV at t7, call `DrawIndexedInstanced(index_count, instance_count, ...)`
- [ ] 5.2 Create `submit_single_draw()` function: bind VB/IB, set standard PSO, set b0 cbuffer, call `DrawIndexedInstanced(index_count, 1, ...)`
- [ ] 5.3 Branching: if `instance_count == 1`, use single draw; otherwise use instanced draw

## 6. GBuffer Pass Integration

- [ ] 6.1 In GBuffer pass: call `batch_builder.build(visible_opaque)` after frustum culling
- [ ] 6.2 Submit all instanced draw calls
- [ ] 6.3 Submit remaining single draws
- [ ] 6.4 Verify GBuffer output is identical with and without instancing enabled

## 7. Shadow Pass Integration

- [ ] 7.1 In shadow passes: call `batch_builder.build(shadow_casters)` per cascade/light
- [ ] 7.2 Use depth-only PSO with `INSTANCED` variant
- [ ] 7.3 Submit instanced shadow draws

## 8. Configuration (project.uzproj)

- [ ] 8.1 `renderer.instancing.enabled` (default true) -- global toggle
- [ ] 8.2 `renderer.instancing.min_instances_to_batch` (default 2)
- [ ] 8.3 `renderer.instancing.max_instances_per_batch` (default 4096)
- [ ] 8.4 `renderer.instancing.transform_buffer_budget_mb` (default 4)

## 9. IPC Handler

- [ ] 9.1 `renderer.batchStats` -- return total_entities, instanced_batches, instanced_entities, single_draws, draw_call_reduction percentage

## 10. Testing

- [ ] 10.1 Unit test: BatchBuilder groups 100 entities with same mesh+material into 1 batch
- [ ] 10.2 Unit test: BatchBuilder creates separate batches for different materials
- [ ] 10.3 Unit test: BatchBuilder respects max_instances_per_batch split
- [ ] 10.4 Integration test: 1000 identical cubes render correctly with instancing (visual comparison)
- [ ] 10.5 Integration test: single-instance fallback produces identical output to non-instanced path
- [ ] 10.6 Integration test: shadow pass with instanced batches (no shadow artifacts)
- [ ] 10.7 Performance test: 1000 cubes instanced < 1ms GPU vs. ~5ms without instancing on RTX 3070
- [ ] 10.8 Performance test: BatchBuilder::build() < 0.1ms CPU for 10000 entities
- [ ] 10.9 Stress test: upload ring buffer budget exceeded gracefully (fallback + warning)

## 11. Documentation

- [ ] 11.1 Update `docs/specs/gpu-instancing.md` with implementation changes
- [ ] 11.2 Update `docs/modules/renderer/README.md` instancing subsystem
- [ ] 11.3 Update `docs/specs/shader-variants.md` to list INSTANCED define
- [ ] 11.4 Add inline comments in batch_builder.cpp explaining grouping algorithm
