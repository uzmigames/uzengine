## 1. DescriptorHeap Implementation

- [ ] 1.1 Create `src/engine/renderer/rhi/descriptor_heap.h` and `descriptor_heap.cpp` implementing `DescriptorHeap` with `Init(device, type, capacity)`, `Alloc()` returning a `uint32_t` heap index, and `Free(index)` for deferred GPU-safe release
- [ ] 1.2 Implement `DescriptorHeap::Alloc()` using an atomic CAS free-list: store freed indices in a lock-free stack (`std::atomic<uint32_t> free_stack[capacity]` + `std::atomic<uint32_t> free_top`); fall back to a monotonically incrementing counter when the free list is empty
- [ ] 1.3 Implement `DescriptorHeap::DeferredFree(index, frame_fence_value)` that queues freed indices in a per-frame ring buffer; process deferred frees in `DescriptorHeap::ProcessDeferredFrees(completed_fence_value)` called each frame to safely release indices only after the GPU has finished using them
- [ ] 1.4 Create one `DescriptorHeap` instance each for: `D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV` (65536 capacity, shader-visible), `D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER` (2048 capacity, shader-visible), `D3D12_DESCRIPTOR_HEAP_TYPE_RTV` (256 capacity, CPU-only), and `D3D12_DESCRIPTOR_HEAP_TYPE_DSV` (64 capacity, CPU-only)

## 2. BindlessManager Singleton

- [ ] 2.1 Create `src/engine/renderer/rhi/bindless_manager.h` and `bindless_manager.cpp` implementing `BindlessManager` singleton with `RegisterTexture(srv_desc, resource)` returning `uint32_t` heap index, `RegisterBuffer(cbv_desc, resource)` returning `uint32_t`, `RegisterUAV(uav_desc, resource)` returning `uint32_t`, and `Unregister(index)`
- [ ] 2.2 Implement `BindlessManager::RegisterTexture()` by calling `device->CreateShaderResourceView()` at the descriptor heap slot corresponding to the returned index; store a mapping from resource pointer to index for `Unregister` lookups
- [ ] 2.3 Implement `BindlessManager::Unregister(index)` calling `DescriptorHeap::DeferredFree()` with the current frame fence value
- [ ] 2.4 Expose `BindlessManager::GetSRVHeap()` and `BindlessManager::GetSamplerHeap()` returning `ID3D12DescriptorHeap*` for binding to the command list's `SetDescriptorHeaps` call

## 3. Bindless Root Signature

- [ ] 3.1 Create a new root signature in `src/engine/renderer/rhi/root_signature.cpp` with exactly 4 root parameters: `[0]` DrawConstants (4x uint32 root constants: `material_idx`, `transform_idx`, `instance_offset`, `flags`), `[1]` SceneCBV (root CBV at b0), `[2]` SRV heap table (descriptor table covering the entire `CBV_SRV_UAV` heap, space 0), `[3]` Sampler heap table (descriptor table covering the entire sampler heap)
- [ ] 3.2 Serialize and cache the new root signature; ensure it is set on every command list before any draw call via `SetGraphicsRootSignature`
- [ ] 3.3 Delete the old per-draw `SetGraphicsDescriptorTable` calls for material textures from `src/engine/renderer/draw_submitter.cpp` after migrating all shaders to the bindless pattern

## 4. Material Buffer

- [ ] 4.1 Create a `StructuredBuffer<MaterialData>` in `src/engine/renderer/material_buffer.cpp` holding `MAX_MATERIALS` (default 4096) entries; each `MaterialData` entry stores `uint32_t` heap indices for albedo, normal, metallic-roughness, emissive SRVs, and PBR factor floats (`base_color_factor[4]`, `metallic_factor`, `roughness_factor`, `emissive_factor[3]`)
- [ ] 4.2 Implement `MaterialBuffer::Upload(material_id, MaterialData)` that writes the entry into a CPU-side staging buffer and marks it dirty; upload dirty entries via `CopyBufferRegion` on the next frame before draws
- [ ] 4.3 Register the material buffer as a bindless SRV via `BindlessManager::RegisterBuffer()` and bind its index as part of `SceneCBV` so all shaders can access it via `g_Materials[material_idx]`

## 5. HLSL Bindless Pattern Migration

- [ ] 5.1 Add `ResourceDescriptorHeap` and `SamplerDescriptorHeap` declarations to a shared HLSL header `src/shaders/bindless.hlsli` used by all shaders; remove all `Texture2D : register(tN)` and `SamplerState : register(sN)` declarations from individual shader files
- [ ] 5.2 Migrate `src/shaders/pbr_lighting.hlsl` to use `ResourceDescriptorHeap[material.albedo_index]` syntax for all texture lookups; verify the shader compiles with `dxc -HV 2021`
- [ ] 5.3 Migrate all remaining Phase 1 shaders (shadow map, depth prepass, fullscreen blit) to the bindless pattern following the same `bindless.hlsli` approach
- [ ] 5.4 Add `DrawConstants` root constant struct to `bindless.hlsli` and update all shaders to read `material_idx`, `transform_idx`, `instance_offset`, and `flags` from it via `GetDrawConstants()`

## 6. Draw Submitter Integration

- [ ] 6.1 Update `src/engine/renderer/draw_submitter.cpp` to set `DrawConstants` via `SetGraphicsRoot32BitConstants` once per draw call instead of per-material descriptor table binds
- [ ] 6.2 Ensure `BindlessManager::GetSRVHeap()` and `GetSamplerHeap()` are bound with `SetDescriptorHeaps` once per command list at the start of each render pass, not per-draw

## 7. Testing

- [ ] 7.1 Create `tests/renderer/test_descriptor_heap.cpp` testing `DescriptorHeap::Alloc()` returns unique non-overlapping indices for 1000 sequential calls
- [ ] 7.2 Test `DescriptorHeap::Free(index)` followed by `Alloc()` returns the freed index (free list recycling works correctly)
- [ ] 7.3 Test `DeferredFree` + `ProcessDeferredFrees`: free 10 indices with fence value 5; call `ProcessDeferredFrees(4)` and verify no indices are recycled; call `ProcessDeferredFrees(5)` and verify all 10 are available via `Alloc()`
- [ ] 7.4 Test concurrent `Alloc()` from 8 threads simultaneously allocating 100 indices each produces 800 unique non-zero indices (verify atomicity via a set intersection check)
- [ ] 7.5 Create `tests/renderer/test_bindless_manager.cpp` testing `RegisterTexture()` returns a non-zero index and the same index is not returned by a subsequent `RegisterTexture()` before `Unregister()`
- [ ] 7.6 Test `Unregister(index)` followed by `RegisterTexture()` (after advancing the frame fence) recycles the index
- [ ] 7.7 Test the root signature has exactly 4 root parameters and that parameter 0 has 4 root constants of type `D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS`
- [ ] 7.8 Test `MaterialBuffer::Upload(id, data)` followed by a frame advance results in the GPU buffer containing the correct `material_idx` values at the expected offset
- [ ] 7.9 Test shader compilation: compile `pbr_lighting.hlsl` with `dxc` in the test environment and verify zero errors and zero warnings after the bindless migration
- [ ] 7.10 Test `DrawSubmitter` emits `SetGraphicsRoot32BitConstants` calls with `material_idx` values matching the registered material IDs
- [ ] 7.11 Run coverage -- all new files in `src/engine/renderer/rhi/` must reach >= 95% line and branch coverage before marking complete
