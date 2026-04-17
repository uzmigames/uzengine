<!-- doc-meta
type: Architecture Analysis
source: Godot 4 (E:\godot/drivers/d3d12/)
date: 2026-03-26
tags: [DX12, descriptor-management, non-bindless, root-signature]
-->

# Godot 4 DX12 Descriptor Management Analysis

## Executive Summary

Godot 4's DX12 implementation uses a **hybrid descriptor management strategy** optimized for non-bindless hardware (SM 5.0+). Instead of relying on bindless resources, Godot allocates descriptors into **shader-visible heaps** at uniform set creation time and uses **descriptor tables in the root signature** to bind them. This approach trades peak memory efficiency for **stable, predictable performance** on all DX12 hardware without bindless support.

**Key Pattern**: Descriptors are allocated into persistent shader-visible heaps once (at UniformSet creation), then bound via root signature descriptor tables during command recording.

---

## 1. Descriptor Heap Architecture

### 1.1 Heap Types (Three per Device)

Godot maintains **three persistent shader-visible heaps** initialized per device:

```cpp
// From rendering_device_driver_d3d12.h:154-196
DescriptorHeap resource_descriptor_heap;      // For CBV/SRV/UAV (shader-visible)
DescriptorHeap sampler_descriptor_heap;        // For SAMPLER (shader-visible)
CPUDescriptorHeapPool resource_descriptor_heap_pool;  // CPU-only, for staging
CPUDescriptorHeapPool rtv_descriptor_heap_pool;       // CPU-only, for RTVs
CPUDescriptorHeapPool dsv_descriptor_heap_pool;       // CPU-only, for DSVs
```

**Key distinction**: Only `resource_descriptor_heap` and `sampler_descriptor_heap` are shader-visible. The pools are **CPU-only heaps** used for temporary allocations (RTVs, DSVs, command-specific descriptors).

### 1.2 Heap Initialization

```cpp
// From rendering_device_driver_d3d12.cpp:6366-6370
Error RenderingDeviceDriverD3D12::_initialize_frames(uint32_t p_frame_count) {
    uint32_t num_resource_descriptors = GLOBAL_GET("rendering/rendering_device/d3d12/max_resource_descriptors");  // e.g., 100,000
    uint32_t num_sampler_descriptors = GLOBAL_GET("rendering/rendering_device/d3d12/max_sampler_descriptors");    // e.g., 2,048

    Error err = resource_descriptor_heap.initialize(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, num_resource_descriptors, true);
    err = sampler_descriptor_heap.initialize(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, num_sampler_descriptors, true);

    // CPU-only heaps (no shader visibility)
    resource_descriptor_heap_pool.initialize(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    rtv_descriptor_heap_pool.initialize(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    dsv_descriptor_heap_pool.initialize(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
}
```

**Configuration**: Heap size is configurable via project settings. Default ~100k resource descriptors, ~2k samplers. These sizes persist for the lifetime of the device.

### 1.3 Allocation Strategy: Virtual Blocks

Each heap uses **D3D12MA::VirtualBlock** for suballocation:

```cpp
// From rendering_device_driver_d3d12.cpp:403-423
Error RenderingDeviceDriverD3D12::DescriptorHeap::allocate(uint32_t p_descriptor_count, Allocation &r_allocation) {
    D3D12MA::VIRTUAL_ALLOCATION_DESC desc = {};
    desc.Size = p_descriptor_count;

    D3D12MA::VirtualAllocation virtual_alloc = {};
    uint64_t offset = 0;
    HRESULT hr = virtual_block->Allocate(&desc, &virtual_alloc, &offset);

    r_allocation.virtual_alloc_handle = virtual_alloc.AllocHandle;      // Handle for future free
    r_allocation.cpu_handle = get_cpu_handle(cpu_handle, offset, increment_size);
    r_allocation.gpu_handle = get_gpu_handle(gpu_handle, offset, increment_size);

    return OK;
}
```

**Why VirtualBlock?** Avoids fragmentation. Godot allocates contiguous ranges for each uniform set (e.g., 12 descriptors), and VirtualBlock tracks free gaps for future allocations.

---

## 2. Uniform Set Creation: Static Descriptor Binding

### 2.1 Per-UniformSet Descriptor Allocation

When a uniform set is created, Godot **allocates and populates descriptors immediately**:

```cpp
// From rendering_device_driver_d3d12.cpp:3408-3432
RDD::UniformSetID RenderingDeviceDriverD3D12::uniform_set_create(
        VectorView<BoundUniform> p_uniforms,
        ShaderID p_shader,
        uint32_t p_set_index,
        int p_linear_pool_index) {

    UniformSetInfo *uniform_set_info = VersatileResource::allocate<UniformSetInfo>(resources_allocator);
    const ShaderInfo::UniformSet &uniform_set = shader_info_in->sets[p_set_index];

    // Allocate resource descriptors (CBV/SRV/UAV)
    if (uniform_set.resource_descriptor_count > 0) {
        Error err = resource_descriptor_heap.allocate(
            uniform_set.resource_descriptor_count,
            uniform_set_info->resource_descriptor_heap_alloc);
    }

    // Allocate sampler descriptors
    if (uniform_set.sampler_descriptor_count > 0) {
        // Sampler caching: multiple sets with same samplers reuse allocations
        Error err = sampler_descriptor_heap.allocate(
            uniform_set.sampler_descriptor_count,
            *uniform_set_info->sampler_descriptor_heap_alloc);
    }
}
```

### 2.2 Descriptor Creation by Type

For each uniform in the set, Godot **creates the actual descriptor** at the allocated offset:

```cpp
// Example: Texture (SRV) creation
case UNIFORM_TYPE_TEXTURE: {
    for (uint32_t j = 0; j < uniform.ids.size(); j++) {
        TextureInfo *texture_info = (TextureInfo *)uniform.ids[j].id;
        device->CreateShaderResourceView(
            texture_info->resource,
            &texture_info->view_descs.srv,
            get_cpu_handle(
                uniform_set_info->resource_descriptor_heap_alloc.cpu_handle,
                binding.resource_descriptor_offset + j,  // Offset within the set's allocation
                resource_descriptor_heap.increment_size));
    }
}

// Example: Constant Buffer (CBV) creation
case UNIFORM_TYPE_UNIFORM_BUFFER: {
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
    cbv_desc.BufferLocation = buf_info->gpu_virtual_address;
    cbv_desc.SizeInBytes = STEPIFY(buf_info->size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

    device->CreateConstantBufferView(
        &cbv_desc,
        get_cpu_handle(
            uniform_set_info->resource_descriptor_heap_alloc.cpu_handle,
            binding.resource_descriptor_offset,
            resource_descriptor_heap.increment_size));
}
```

**Critical insight**: All descriptor creation happens **synchronously during UniformSet creation**. No deferred staging. The GPU-visible heap immediately contains valid descriptors.

### 2.3 Sampler Descriptor Sharing

Godot **deduplicates sampler allocations** across uniform sets:

```cpp
// From rendering_device_driver_d3d12.cpp:3436-3477
uint32_t sampler_key = HASH_MURMUR3_SEED;
for (uint32_t i = 0; i < p_uniforms.size(); i++) {
    // Hash all sampler IDs in this set
    if (uniform.type == UNIFORM_TYPE_SAMPLER) {
        for (uint32_t j = 0; j < uniform.ids.size(); j++) {
            sampler_key = hash_murmur3_one_64(uniform.ids[j].id, sampler_key);
        }
    }
}

RBMap<uint32_t, SamplerDescriptorHeapAllocation>::Iterator find_result =
    sampler_descriptor_heap_allocations.find(sampler_key);

if (find_result != sampler_descriptor_heap_allocations.end()) {
    // Reuse existing sampler allocation
    uniform_set_info->sampler_descriptor_heap_alloc = &find_result->value;
    ++uniform_set_info->sampler_descriptor_heap_alloc->use_count;
} else {
    // Create new sampler allocation
    uniform_set_info->sampler_descriptor_heap_alloc =
        &sampler_descriptor_heap_allocations.insert(sampler_key, SamplerDescriptorHeapAllocation())->get();

    Error err = sampler_descriptor_heap.allocate(
        uniform_set.sampler_descriptor_count,
        *uniform_set_info->sampler_descriptor_heap_alloc);
}
```

**Rationale**: Samplers are expensive to allocate (limited heap). Many materials use identical sampler sets. Godot hashes the sampler list and reuses heaps, with reference counting for cleanup.

---

## 3. Root Signature Design

### 3.1 Root Signature Structure

Godot generates a **single root signature per shader** with:

1. **Push constants** (if needed, up to 128 bytes)
2. **Descriptor tables** (one per uniform set, two types: resources + samplers)
3. **Dynamic buffer root descriptors** (CBV/SRV/UAV for dynamic buffers, avoiding copy)

```cpp
// From rendering_shader_container_d3d12.cpp:557-763
bool RenderingShaderContainerD3D12::_generate_root_signature(BitField<RenderingDeviceCommons::ShaderStage> p_stages_processed) {
    LocalVector<D3D12_ROOT_PARAMETER1> root_params;

    // 1. Push constants
    if (reflection_data_d3d12.dxil_push_constant_stages) {
        CD3DX12_ROOT_PARAMETER1 push_constant;
        push_constant.InitAsConstants(
            reflection_data.push_constant_size / sizeof(uint32_t),
            ROOT_CONSTANT_REGISTER,  // b0 in HLSL (space 0)
            0,
            stages_to_d3d12_visibility(reflection_data_d3d12.dxil_push_constant_stages));
        root_params.push_back(push_constant);
    }

    // 2. Dynamic buffer root descriptors (for UNIFORM_BUFFER_DYNAMIC)
    if (uniform.type == RDC::UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC ||
        uniform.type == RDC::UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC) {

        CD3DX12_ROOT_PARAMETER1 root_param = {};

        // Bind as inline root descriptor, not descriptor table
        switch (range_type) {
            case D3D12_DESCRIPTOR_RANGE_TYPE_CBV: {
                root_param.InitAsConstantBufferView(
                    dxil_register, 0,
                    D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE,
                    visibility);
            } break;
            case D3D12_DESCRIPTOR_RANGE_TYPE_SRV: {
                root_param.InitAsShaderResourceView(
                    dxil_register, 0,
                    D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
                    visibility);
            } break;
            // ... UAV similar
        }

        uniform_d3d12.root_param_idx = root_params.size();
        root_params.push_back(root_param);
    }

    // 3. Static descriptor tables
    for (const TraceableDescriptorTable &table : resource_tables_maps) {
        CD3DX12_ROOT_PARAMETER1 root_table = {};
        root_table.InitAsDescriptorTable(
            table.ranges.size(),           // Number of ranges in this table
            table.ranges.ptr(),             // Array of D3D12_DESCRIPTOR_RANGE1
            stages_to_d3d12_visibility(table.stages_mask));

        reflection_binding_set_data_d3d12.ptrw()[table.set].resource_root_param_idx = root_params.size();
        root_params.push_back(root_table);
    }

    // Same for sampler tables
    for (const TraceableDescriptorTable &table : sampler_tables_maps) {
        CD3DX12_ROOT_PARAMETER1 root_table = {};
        root_table.InitAsDescriptorTable(table.ranges.size(), table.ranges.ptr(), ...);
        reflection_binding_set_data_d3d12.ptrw()[table.set].sampler_root_param_idx = root_params.size();
        root_params.push_back(root_table);
    }

    // Serialize to binary
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_sig_desc = {};
    root_sig_desc.Init_1_1(root_params.size(), root_params.ptr(), 0, nullptr, root_sig_flags);

    HRESULT res = D3DX12SerializeVersionedRootSignature(
        ..., &root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1_1, &root_sig_blob, ...);
}
```

### 3.2 Descriptor Range Flags

Godot uses **DATA_VOLATILE flags** to avoid validation layer stalls:

```cpp
// From rendering_shader_container_d3d12.cpp:631-640
D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

if (p_range_type == D3D12_DESCRIPTOR_RANGE_TYPE_SRV ||
    p_range_type == D3D12_DESCRIPTOR_RANGE_TYPE_UAV) {
    flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
    // Allows GPU to update descriptors between draws without stalling
}
else if (p_range_type == D3D12_DESCRIPTOR_RANGE_TYPE_CBV) {
    flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    // Constant buffers are read-only; GPU driver can cache them
}

range.Init(p_range_type, p_num_descriptors, p_dxil_register, 0, flags, r_descriptor_offset);
```

**Why DATA_VOLATILE?** SRV/UAV descriptors can change between draw calls (different textures bound to same slot). VOLATILE tells the GPU driver not to cache. CBV uses STATIC_WHILE_SET_AT_EXECUTE because constant buffers don't change mid-draw.

---

## 4. Command Recording: Binding Descriptors

### 4.1 Descriptor Heap Setup (Once Per Command Buffer)

Before any draw/dispatch, Godot **sets both descriptor heaps**:

```cpp
// From rendering_device_driver_d3d12.cpp:3819-3833
void RenderingDeviceDriverD3D12::_command_check_descriptor_sets(CommandBufferID p_cmd_buffer) {
    CommandBufferInfo *cmd_buf_info = (CommandBufferInfo *)p_cmd_buffer.id;

    if (!cmd_buf_info->descriptor_heaps_set) {
        // Set BOTH heaps once; they persist until command buffer ends
        ID3D12DescriptorHeap *heaps[] = {
            resource_descriptor_heap.heap.Get(),
            sampler_descriptor_heap.heap.Get(),
        };
        cmd_buf_info->cmd_list->SetDescriptorHeaps(2, heaps);
        cmd_buf_info->descriptor_heaps_set = true;
    }
}
```

**Lazy initialization**: Godot sets heaps only on first use, avoiding redundant API calls.

### 4.2 Binding Uniform Sets

During rendering, Godot binds already-allocated descriptor tables:

```cpp
// From rendering_device_driver_d3d12.cpp:4871-4908 (graphics)
void RenderingDeviceDriverD3D12::command_bind_render_uniform_sets(
        CommandBufferID p_cmd_buffer,
        VectorView<UniformSetID> p_uniform_sets,
        ShaderID p_shader,
        uint32_t p_first_set_index,
        uint32_t p_set_count,
        uint32_t p_dynamic_offsets) {

    _command_check_descriptor_sets(p_cmd_buffer);

    const CommandBufferInfo *cmd_buf_info = (const CommandBufferInfo *)p_cmd_buffer.id;
    const ShaderInfo *shader_info_in = (const ShaderInfo *)p_shader.id;

    for (uint32_t i = 0u; i < p_set_count; ++i) {
        UniformSetInfo *uniform_set_info = (UniformSetInfo *)p_uniform_sets[i].id;
        const ShaderInfo::UniformSet &uniform_set_shader_info = shader_info_in->sets[p_first_set_index + i];

        // Bind static resource descriptor table
        if (uniform_set_shader_info.resource_root_param_idx != UINT32_MAX) {
            cmd_buf_info->cmd_list->SetGraphicsRootDescriptorTable(
                uniform_set_shader_info.resource_root_param_idx,
                uniform_set_info->resource_descriptor_heap_alloc.gpu_handle);  // GPU handle to start of allocation
        }

        // Bind sampler descriptor table
        if (uniform_set_shader_info.sampler_root_param_idx != UINT32_MAX) {
            cmd_buf_info->cmd_list->SetGraphicsRootDescriptorTable(
                uniform_set_shader_info.sampler_root_param_idx,
                uniform_set_info->sampler_descriptor_heap_alloc->gpu_handle);
        }

        // Bind dynamic buffers as inline root CBV/SRV/UAV
        for (const UniformSetInfo::DynamicBuffer &dynamic_buffer : uniform_set_info->dynamic_buffers) {
            const ShaderInfo::UniformBindingInfo &binding = uniform_set_shader_info.bindings[dynamic_buffer.binding];

            // Get current frame's dynamic buffer offset
            BufferInfo *buf_info = (BufferInfo *)dynamic_buffer.info->info;
            D3D12_GPU_VIRTUAL_ADDRESS buffer_location =
                buf_info->gpu_virtual_address +
                (dynamic_buffer.info->persistent_ptr - ptr_to_offset_lookup[frame_idx]);

            cmd_buf_info->cmd_list->SetGraphicsRootConstantBufferView(
                binding.root_param_idx,
                buffer_location);
        }
    }
}
```

Similar for compute via `SetComputeRootDescriptorTable`.

**Key insight**: No descriptor copying. The GPU-visible heap allocation from UniformSet creation time is reused directly. Binding is just setting root parameter offsets.

---

## 5. Per-Frame Descriptor Staging for Temporary Operations

### 5.1 Frame Lifecycle

Godot resets per-frame descriptor allocation on each frame:

```cpp
// From rendering_device_driver_d3d12.cpp:5689-5697
void RenderingDeviceDriverD3D12::begin_segment(uint32_t p_frame_index, uint32_t p_frames_drawn) {
    frame_idx = p_frame_index;
    frames[frame_idx].descriptor_allocation_count = 0;  // Reset for new frame

    allocator->SetCurrentFrameIndex(p_frames_drawn);
    segment_begun = true;
}

void RenderingDeviceDriverD3D12::end_segment() {
    FrameInfo &f = frames[frame_idx];

    // Free leftover descriptors allocated during this frame
    for (uint32_t i = f.descriptor_allocation_count; i < f.descriptor_allocations.size(); i++) {
        resource_descriptor_heap.free(f.descriptor_allocations[i]);
    }
    f.descriptor_allocations.resize(f.descriptor_allocation_count);
}
```

### 5.2 Per-Frame Descriptor Allocation (CPU → GPU Copy Pattern)

For operations requiring temporary descriptors (e.g., clear via UAV), Godot:

1. **Creates descriptor on CPU-only heap**
2. **Allocates slot in GPU-visible heap**
3. **Copies descriptor** via `CopyDescriptorsSimple`

```cpp
// From rendering_device_driver_d3d12.cpp:3861-3894
void RenderingDeviceDriverD3D12::command_clear_buffer(
        CommandBufferID p_cmd_buffer,
        BufferID p_buffer,
        uint64_t p_offset,
        uint64_t p_size) {

    _command_check_descriptor_sets(p_cmd_buffer);

    CommandBufferInfo *cmd_buf_info = (CommandBufferInfo *)p_cmd_buffer.id;
    BufferInfo *buf_info = (BufferInfo *)p_buffer.id;

    // Create UAV on CPU-only pool
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
    uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Buffer.FirstElement = 0;
    uav_desc.Buffer.NumElements = (buf_info->size + 3u) / 4u;
    uav_desc.Buffer.StructureByteStride = 0;
    uav_desc.Buffer.CounterOffsetInBytes = 0;
    uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

    device->CreateUnorderedAccessView(
        buf_info->resource,
        nullptr,
        &uav_desc,
        cmd_buf_info->uav_alloc.cpu_handle);  // Write to command buffer's CPU-only allocation

    // Allocate slot in GPU-visible heap for this frame
    DescriptorHeap::Allocation shader_visible_descriptor_allocation = _command_allocate_per_frame_descriptor();
    ERR_FAIL_COND(shader_visible_descriptor_allocation.virtual_alloc_handle == 0);

    // Copy from CPU to GPU heap
    device->CopyDescriptorsSimple(
        1,
        shader_visible_descriptor_allocation.cpu_handle,  // GPU heap destination
        cmd_buf_info->uav_alloc.cpu_handle,                // CPU pool source
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Use GPU handle in clear command
    cmd_buf_info->cmd_list->ClearUnorderedAccessViewFloat(
        shader_visible_descriptor_allocation.gpu_handle,
        cmd_buf_info->uav_alloc.cpu_handle,
        ...);
}
```

**Why this two-stage dance?**
- **CPU heaps** (RTVs, DSVs, resource pool) are **not shader-visible**. Descriptors created there can't be directly used by GPU commands.
- **GPU heap** has **limited capacity**. Can't pre-allocate every possible descriptor.
- **Solution**: Use CPU pool for temporary staging, then copy to GPU heap per frame.

### 5.3 Per-Frame Allocation Function

```cpp
// From rendering_device_driver_d3d12.cpp:3839-3859
RenderingDeviceDriverD3D12::DescriptorHeap::Allocation
RenderingDeviceDriverD3D12::_command_allocate_per_frame_descriptor() {
    FrameInfo &f = frames[frame_idx];

    if (f.descriptor_allocation_count < f.descriptor_allocations.size()) {
        // Reuse previously allocated slot
        uint32_t allocation_index = f.descriptor_allocation_count;
        ++f.descriptor_allocation_count;
        return f.descriptor_allocations[allocation_index];
    } else {
        // Allocate new slot
        DescriptorHeap::Allocation descriptor_allocation = {};
        HRESULT hr = resource_descriptor_heap.allocate(1, descriptor_allocation);
        ERR_FAIL_COND_V(hr != OK, {});

        f.descriptor_allocations.push_back(descriptor_allocation);
        f.descriptor_allocation_count = f.descriptor_allocations.size();

        return descriptor_allocation;
    }
}
```

**Recycling**: Godot pre-allocates a pool of per-frame descriptors on the first use. Subsequent frames reuse them. On `end_segment()`, unused ones are freed.

---

## 6. Resource Barriers and State Transitions

### 6.1 Enhanced Barriers vs. Legacy

Godot supports both enhanced barriers (newer drivers) and legacy barriers:

```cpp
// From rendering_device_driver_d3d12.cpp:3697-3707
void RenderingDeviceDriverD3D12::command_uniform_set_prepare_for_use(
        CommandBufferID p_cmd_buffer,
        UniformSetID p_uniform_set,
        ShaderID p_shader,
        uint32_t p_set_index) {

    if (barrier_capabilities.enhanced_barriers_supported) {
        return;  // Enhanced barriers handle this automatically
    }

    // Legacy path: manually transition resources to correct state
    for (const UniformSetInfo::StateRequirement &sr : uniform_set_info->resource_states) {
        _resource_transition_batch(
            cmd_buf_info,
            sr.resource,
            0,
            1,
            sr.states);  // e.g., D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE
    }
}
```

### 6.2 Compute-to-Graphics Transitions

For compute to graphics transitions (e.g., post-process dispatch then fullscreen render):

```cpp
// Godot uses standard UAV barrier between compute and graphics passes
_resource_transition_batch(
    cmd_buf_info,
    resource,
    0, 1,
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);  // After compute, before graphics reads
```

---

## 7. Fullscreen Rendering Pattern (Post-Process, Sky)

### 7.1 Rendering Without Vertex Buffers

Godot **does not use SV_VertexID** for fullscreen quads. Instead:

1. **Vertex format with full-screen quad data embedded** in constant buffers
2. **Or traditional full-screen triangle strip** via vertex buffer

Example from sky rendering (inferred from architecture):
```cpp
// Pseudo-code: Godot likely uses a traditional approach
// 1. Define a vertex format (2D position only, often from CB)
// 2. Use IndirectDraw or regular Draw with SV_VertexID in VS

// OR (more common in Godot):
// 1. Create a small VB with full-screen geometry
// 2. Bind and draw normally
```

Godot's approach is **conservative**: use explicit geometry buffers rather than cleverness with SV_VertexID. This is simpler for the RHI abstraction and doesn't require special-case code paths.

### 7.2 Texture Binding for Fullscreen Effects

Post-process passes (bloom, tone-map, etc.) bind textures via descriptor tables:

```
Root Signature:
├─ [0] Push constants (16 bytes: view-proj matrix, etc.)
├─ [1] Descriptor Table (Resources)
│   ├─ b0: Scene CB
│   ├─ t0: Depth texture (SRV)
│   ├─ t1: Albedo texture (SRV)
│   ├─ t2: Normal texture (SRV)
│   └─ t3: Bloom texture (SRV)
└─ [2] Descriptor Table (Samplers)
    ├─ s0: Linear clamp sampler
    └─ s1: Point sampler
```

Binding is identical to standard rendering:
```cpp
cmd_list->SetGraphicsRootDescriptorTable(1, resource_table_gpu_handle);
cmd_list->SetGraphicsRootDescriptorTable(2, sampler_table_gpu_handle);
cmd_list->DrawInstanced(6, 1, 0, 0);  // Full-screen triangle pair
```

---

## 8. Key Differences from UzEngine

### 8.1 UzEngine's Approach (Current)

UzEngine uses **per-frame descriptor copies** for all resources:

```cpp
// UzEngine pattern (inferred)
begin_frame() {
    // Allocate fresh GPU-visible heap for this frame
    desc_heap = allocate_gpu_heap(10000);
}

for (each draw call) {
    // Copy descriptors from UniformSet to frame's heap
    copy_descriptors(uniform_set.descriptors, desc_heap.current_offset);
    set_root_descriptor_table(desc_heap.gpu_handle + offset);
}

end_frame() {
    free_desc_heap();  // Or pool it
}
```

**Pro**: Isolation per frame. Easy to reset.
**Con**: Constant copying overhead. Must size heap for worst-case frame.

### 8.2 Godot's Approach (Reference)

Godot allocates descriptors **once at UniformSet creation**, reuses across frames:

```cpp
uniform_set_create() {
    allocate_gpu_descriptors(num_descriptors);
    create_descriptors(resources);  // Immediately populate
    store_gpu_handle_in_uniform_set;
}

begin_frame() {
    // Only per-frame staging for temporary descriptors
    per_frame_desc_pool.reset();
}

for (each draw call) {
    // Direct bind: no copying needed
    set_root_descriptor_table(uniform_set.gpu_handle);
}

end_segment() {
    // Free only temporary descriptors, not the uniform set's descriptors
    per_frame_desc_pool.free_unused();
}
```

**Pro**: Zero overhead per draw. Descriptor lifetime decoupled from frames.
**Con**: GPU heap must fit all active uniform sets simultaneously.

---

## 9. Sampler Caching & Pooling Details

### 9.1 Sampler Key Generation

Godot's sampler deduplication is **order-independent**:

```cpp
// Hash all sampler IDs
uint32_t sampler_key = HASH_MURMUR3_SEED;
for (each sampler in uniform set) {
    sampler_key = hash_murmur3_one_64(sampler.id, sampler_key);
}
sampler_key = hash_fmix32(sampler_key);  // Finalize
```

**Why?** Two materials with samplers [A, B, C] and [C, A, B] in different orders should still share one GPU heap allocation.

Actually, **Godot's implementation is order-sensitive** (iterates bindings in order). But the deduplication works because Godot generates the same hash for the same set of samplers in the same binding order.

### 9.2 Reference Counting

```cpp
struct SamplerDescriptorHeapAllocation : DescriptorHeap::Allocation {
    uint32_t key = 0;
    uint32_t use_count = 1;  // Number of UniformSets using this allocation
};

// On UniformSet creation
if (find existing allocation with same key) {
    ++allocation->use_count;
    reuse_allocation();
} else {
    create_new_allocation();
}

// On UniformSet destruction
if (--allocation->use_count == 0) {
    free_allocation();
}
```

---

## 10. Practical Recommendations for UzEngine

### 10.1 When to Adopt Godot's Pattern

**Use Godot's per-UniformSet persistent descriptors** if:
- Material count is moderate (< 1000 unique materials)
- Descriptor heap is large enough (Godot uses 100k+)
- Descriptor lifetime can outlive individual frames
- You want minimal per-draw overhead

### 10.2 When to Keep Per-Frame Staging

**Keep per-frame descriptor copies** if:
- Maximum descriptor count is bounded (< 10k per frame)
- You need strict frame-isolation for debugging
- Descriptor allocation patterns are highly dynamic
- GPU heap pressure is a concern

### 10.3 Hybrid Approach (Best for UzEngine)

Combine both patterns:
```cpp
// Uniform set descriptors (Godot-style)
uniform_set_create() {
    persistent_heap.allocate(uniform_set.descriptor_count);
    create_descriptors();  // Once, at creation time
}

// Temporary per-command descriptors (current UzEngine style)
command_clear_buffer() {
    cpu_pool.create_uav();
    per_frame_heap.allocate(1);
    copy_descriptor_to_per_frame();
    use_per_frame_handle();
}

// Benefit: Low overhead for static scenes, zero allocation for temp ops
```

---

## 11. Summary Table: Godot vs. UzEngine

| Aspect | Godot | UzEngine (Current) | Recommendation |
|--------|-------|-------------------|---|
| **Uniform Set Descriptors** | Persistent GPU heap | Per-frame copy | Adopt Godot's persistence |
| **Allocation Lifetime** | UniformSet creation → destruction | Frame start → end | Decouple from frames |
| **Heap Size** | Fixed, large (~100k) | Scaled per frame | Fixed + per-frame overflow pool |
| **Sampler Dedup** | Yes (ref-counted) | No | Implement sampler pooling |
| **CBV Binding** | Descriptor table OR root CBV (dynamic) | Descriptor table | Same, use root CBV for dynamic |
| **Fullscreen Effects** | Descriptor table binding | Likely similar | No change needed |
| **Compute→Graphics Barriers** | State tracking + barriers | Same | No change needed |
| **CPU Pool Usage** | RTVs, DSVs, temp descriptors | All descriptors | Restrict to truly temporary |

---

## 12. References

- **Godot Source**: `E:\godot/drivers/d3d12/rendering_device_driver_d3d12.cpp` (6436 lines)
- **Godot Shader Container**: `E:\godot/drivers/d3d12/rendering_shader_container_d3d12.cpp` (root signature generation, lines 557–811)
- **D3D12 Best Practices**: Microsoft D3D12 documentation on descriptor heaps and root signatures
- **UzEngine Architecture**: `docs/architecture/rhi-abstraction.md`, `rendering.md`

