# Godot 4 Uniform Buffer Binding Architecture
## Cross-Backend Pattern for Per-Frame Constant Buffer Management

---

## Executive Summary

Godot 4 solves the **per-frame constant buffer synchronization problem** via:

1. **MultiUmaBuffer<N, MAX_EXTRA>** — template that auto-rings frame buffers at RenderingDevice API level
2. **Persistent Mapping with Frame Indexing** — both Vulkan and D3D12 use identical `buffer_persistent_map_advance()` interface
3. **Ring Buffer at Allocation Time** — multiplies buffer size by `frame_count` (2-3 frames) and advances ring on each `prepare_for_upload()`
4. **Offset Encoding** — dynamic offsets packed as 2-bit frame_idx per buffer in a 64-bit mask

The elegance: **game code never sees the ring logic**. It calls `rd->buffer_update()` and the driver handles frame-aware CPU→GPU synchronization internally.

---

## Pattern 1: Ring Buffer Abstraction Layer

**Location**: `/e/godot/servers/rendering/multi_uma_buffer.h`

### Problem Solved

DX11/OpenGL have explicit Map/Unmap per-frame. Vulkan has persistent mapping but requires CPU-visible pooling to avoid GPU read-write hazards. Godot needed one API that:
- Works with UMA (Unified Memory Architecture) iGPUs (iOS, Android, PS4/PS5)
- Handles ReBAR on desktop (PCIe coherency)
- Prevents CPU writes to buffers GPU is reading from the same frame
- Auto-grows extra buffers for spiky frames, shrinks after

### Implementation: MultiUmaBuffer Template

```cpp
template <uint32_t NUM_BUFFERS, uint32_t MAX_EXTRA_BUFFERS = UINT32_MAX>
class MultiUmaBuffer : public MultiUmaBufferBase {
    struct BufferInfo {
        uint32_t size_bytes = 0;
        MultiUmaBufferType type = MultiUmaBufferType::UNIFORM;  // or STORAGE, VERTEX
    };

    LocalVector<RID> buffers;  // Flat ring: size = (NUM_BUFFERS per frame) * (num_frames)
    uint32_t curr_idx = UINT32_MAX;
    uint64_t last_frame_mapped = UINT64_MAX;
};
```

**Key State Variables**:
- `buffers` — all RIDs in ring (indexed as `buffers[curr_idx * NUM_BUFFERS + slot]`)
- `curr_idx` — current frame position in ring (increments per `prepare_for_upload()`)
- `last_frame_mapped` — frame number of last map (prevents double-mapping in frame)

### Usage Pattern

**Example: Per-Frame Light Data**

```cpp
// Setup (once)
MultiUmaBuffer<1> light_params("Shadow directional lights");  // 1 buffer slot
light_params.set_uniform_size(0, sizeof(DirectionalLightParams) * num_lights);

// Per-frame (every frame, possibly multiple passes)
for (uint32_t pass = 0; pass < num_shadow_passes; ++pass) {
    light_params.prepare_for_upload();  // Advance ring if new frame

    // Write data directly to GPU-visible memory
    DirectionalLightParams *dst = (DirectionalLightParams *)light_params.map_raw_for_upload(0);
    memcpy(dst, light_data, sizeof(DirectionalLightParams) * num_lights);

    // Or simpler: let RenderingDevice handle the copy
    // light_params.upload(0, light_data, size_bytes);

    // Create/update uniform set (once per pass, or reuse)
    if (!uniform_set[pass]) {
        RD::Uniform u;
        u.binding = BINDING_LIGHT_PARAMS;
        u.uniform_type = RD::UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC;
        u.append_id(light_params._get(0));  // _get returns current buffer RID
        uniform_set[pass] = rd->uniform_set_create(...);
    }
}

// Cleanup
light_params.uninit();  // Free all buffers in ring
```

**Lifecycle Hooks**:

| Method | Behavior |
|--------|----------|
| `set_uniform_size(idx, bytes)` | Define buffer size (before first use) |
| `prepare_for_upload()` | Check frame boundary; allocate new buffer if needed; increment `curr_idx` |
| `upload(idx, src, size)` | Copy via `buffer_persistent_map_advance()` + `memcpy()` + `buffer_flush()` |
| `map_raw_for_upload(idx)` | Return direct pointer to CPU-visible memory (caller's responsibility to not use after GPU submit) |
| `_get(idx)` | Return RID of current buffer in ring |

### Auto-Scaling with MAX_EXTRA_BUFFERS

```cpp
bool prepare_for_map(bool p_append) {
    const uint64_t frames_drawn = rd->get_frames_drawn();

    if (last_frame_mapped == frames_drawn) {
        if (!p_append) {
            ++curr_idx;  // Same frame, new pass — advance ring
        }
    } else {
        p_append = false;
        curr_idx = 0;   // New frame — reset to start of ring
        if (max_extra_buffers != UINT32_MAX) {
            shrink_to_max_extra_buffers();  // Deallocate extras from spike
        }
    }

    last_frame_mapped = frames_drawn;

    if (curr_idx * NUM_BUFFERS >= buffers.size()) {
        push();  // Allocate more buffers if this pass exceeds capacity
    }

    return !p_append;  // true = new buffer allocated
}
```

**Example: Multi-Pass Shadow Mapping**

Suppose normal frames render 6 shadow passes, but rare HDR moments spike to 24:
```cpp
MultiUmaBuffer<1, 6> shadow_buffer;  // Keep up to 6 buffers, shrink extras
shadow_buffer.set_uniform_size(0, cascade_size);

// Normal frame: 6 passes
for (int i = 0; i < 6; ++i) {
    shadow_buffer.prepare_for_upload();
    // On frame change, shrink_to_max_extra_buffers() deallocates pass 7-24
}

// Spike frame: 24 passes
for (int i = 0; i < 24; ++i) {
    shadow_buffer.prepare_for_upload();
    // Allocates buffers 7-24 on demand
}

// After spike: MAX_EXTRA_BUFFERS cleans up 7-24, keeps 1-6
```

---

## Pattern 2: Backend-Agnostic Dynamic Buffer Interface

**Location**: `/e/godot/servers/rendering/rendering_device.cpp:710` (RenderingDevice) + backend drivers

### The Problem: Map/Unmap Overhead

- **DX11**: Every frame: `Map(DISCARD_PREVIOUS)` → write → `Unmap()` (GPU stall if GPU still reading previous frame)
- **Vulkan**: Persistent mapping available, but requires frame-aware pooling (what MultiUmaBuffer does)
- **D3D12**: Supports persistent mapping, but still needs ring buffer to avoid hazards

Godot abstracts this with `BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT`.

### RenderingDevice::buffer_update() Flow

```cpp
Error RenderingDevice::buffer_update(RID p_buffer, uint32_t p_offset, uint32_t p_size,
                                     const void *p_data, bool p_skip_check) {
    // Safety: can't update while recording draw/compute lists
    ERR_FAIL_COND_V_MSG(draw_list.active && !p_skip_check, ERR_INVALID_PARAMETER, ...);

    Buffer *buffer = _get_buffer_from_owner(p_buffer);

    // FAST PATH: Dynamic persistent buffer (ring buffer)
    if (buffer->usage.has_flag(RDD::BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT)) {
        uint8_t *dst_data = driver->buffer_persistent_map_advance(buffer->driver_id, frames_drawn);
        memcpy(dst_data + p_offset, p_data, p_size);  // Direct copy, no staging
        direct_copy_count++;
        buffer_flush(p_buffer);
        return OK;
    }

    // SLOW PATH: Non-dynamic buffer (uses staging buffer)
    // Chunks large writes, defends to RDG::RecordedBufferCopy
    _check_transfer_worker_buffer(buffer);
    // ... staging buffer logic ...
}
```

**Key Insight**: The flag `BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT` tells RenderingDevice: "This buffer is CPU-written every frame, GPU-read same frame. Use frame-aware pooling."

### Vulkan Backend: buffer_persistent_map_advance()

**Location**: `/e/godot/drivers/vulkan/rendering_device_driver_vulkan.cpp:2043`

```cpp
uint8_t *RenderingDeviceDriverVulkan::buffer_persistent_map_advance(BufferID p_buffer, uint64_t p_frames_drawn) {
    BufferDynamicInfo *buf_info = (BufferDynamicInfo *)p_buffer.id;

    // Safety guard: only map once per frame
    #ifdef DEBUG_ENABLED
    ERR_FAIL_COND_V_MSG(buf_info->last_frame_mapped == p_frames_drawn, nullptr,
        "Buffers with BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT must only be mapped once per frame. "
        "Otherwise there could be race conditions with the GPU.");
    buf_info->last_frame_mapped = p_frames_drawn;
    #endif

    // Advance ring: move to next frame slot
    buf_info->frame_idx = (buf_info->frame_idx + 1u) % frame_count;  // frame_count = 2 or 3

    // Return pointer to this frame's slot in the ring
    return buf_info->persistent_ptr + buf_info->frame_idx * buf_info->size;
}
```

**Buffer Allocation with Frame Multiplying**:
```cpp
RDD::BufferID RenderingDeviceDriverVulkan::buffer_create(uint64_t p_size,
                                                         BitField<BufferUsageBits> p_usage,
                                                         MemoryAllocationType p_allocation_type,
                                                         uint64_t p_frames_drawn) {
    // ... alignment logic (UNIFORM buffer: respect minUniformBufferOffsetAlignment, etc.) ...

    const size_t original_size = p_size;

    // CRITICAL: Dynamic buffers are allocated 3x larger
    if (p_usage.has_flag(BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT)) {
        p_size = p_size * frame_count;  // frame_count is 2-3 typically
    }

    // Allocate with persistent mapping
    VkBufferCreateInfo create_info = {};
    create_info.size = p_size;
    create_info.usage = p_usage & ~BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT;
    create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // For dynamic buffers, request host-visible, host-coherent memory
    if (p_usage.has_flag(BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT)) {
        alloc_create_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        alloc_create_info.requiredFlags = (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    }

    // VMA allocates and returns persistent_ptr (always mapped)
    // ...
}
```

**Memory Flushing** (non-coherent memory):
```cpp
void RenderingDeviceDriverVulkan::buffer_flush(BufferID p_buffer) {
    BufferDynamicInfo *buf_info = (BufferDynamicInfo *)p_buffer.id;

    // Check if memory is host-coherent (doesn't need flush)
    vmaGetAllocationMemoryProperties(allocator, buf_info->allocation.handle, &mem_props_flags);
    const bool needs_flushing = !(mem_props_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (needs_flushing && buf_info->is_dynamic()) {
        pending_flushes.allocations.push_back(buf_info->allocation.handle);
        pending_flushes.offsets.push_back(buf_info->frame_idx * buf_info->size);  // Flush only this frame's slot
        pending_flushes.sizes.push_back(buf_info->size);
    }
}
```

### D3D12 Backend: Identical Algorithm, Different Memory Model

**Location**: `/e/godot/drivers/d3d12/rendering_device_driver_d3d12.cpp:1042`

```cpp
uint8_t *RenderingDeviceDriverD3D12::buffer_persistent_map_advance(BufferID p_buffer,
                                                                     uint64_t p_frames_drawn) {
    BufferDynamicInfo *buf_info = (BufferDynamicInfo *)p_buffer.id;

    // Same safety guard as Vulkan
    #ifdef DEBUG_ENABLED
    ERR_FAIL_COND_V_MSG(buf_info->last_frame_mapped == p_frames_drawn, nullptr, ...);
    buf_info->last_frame_mapped = p_frames_drawn;
    #endif

    // Same ring advancement
    buf_info->frame_idx = (buf_info->frame_idx + 1u) % frames.size();
    return buf_info->persistent_ptr + buf_info->frame_idx * buf_info->size;
}
```

**D3D12 Persistent Mapping** (no `buffer_flush()` call needed):
- D3D12 uses write-combined memory for persistent CPU-visible buffers
- No manual flush required — GPU sees writes coherently
- Ring buffer still avoids hazards: each frame writes to its own slot

---

## Pattern 3: Dynamic Offset Encoding

**Location**: Both backends implement `buffer_get_dynamic_offsets()`

### Problem: Multiple Dynamic Buffers Per Shader

A shader might have:
- Uniform buffer 0: per-material constants (64 bytes)
- Uniform buffer 1: per-object transform (256 bytes)
- Storage buffer 0: particle data (4 KB)

Each needs its own frame_idx to avoid hazards. Godot packs these into one 64-bit mask:

```cpp
uint64_t RenderingDeviceDriver::buffer_get_dynamic_offsets(Span<BufferID> p_buffers) {
    uint64_t mask = 0u;
    uint64_t shift = 0u;

    for (const BufferID &buf : p_buffers) {
        const BufferInfo *buf_info = (const BufferInfo *)buf.id;
        if (!buf_info->is_dynamic()) {
            continue;  // Skip static buffers
        }

        // Encode frame_idx as 2 bits (supports 4 frame slots max)
        const BufferDynamicInfo *dyn_buf = (const BufferDynamicInfo *)buf.id;
        mask |= dyn_buf->frame_idx << shift;
        shift += 2UL;
    }

    return mask;  // e.g., 0b...0011_0001_0000 for 8 dynamic buffers
}
```

**Decoding at Draw Time** (D3D12):
```cpp
void RenderingDeviceDriverD3D12::_execute_dynamic_buffer_bindings(
        const UniformSetInfo::DynamicBuffer &dynamic_buffer,
        uint64_t p_dynamic_offsets) {

    // Extract frame_idx for this buffer from bitmask
    uint64_t buffer_frame_idx = (p_dynamic_offsets >> (buffer_idx * 2)) & 0x3;

    // Compute offset for this frame's slot
    uint64_t dynamic_offset = buffer_frame_idx * buffer_info->size;

    // Bind with offset
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
    cbv_desc.BufferLocation = buffer_info->gpu_virtual_address + dynamic_offset;
    cbv_desc.SizeInBytes = buffer_info->size;
    device->CreateConstantBufferView(&cbv_desc, descriptor_cpu_handle);
}
```

**Vulkan does similar at shader compile time** (frame_idx baked into descriptor offset).

---

## Pattern 4: Root Signature + Descriptor Binding (D3D12)

**Location**: `/e/godot/drivers/d3d12/rendering_shader_container_d3d12.cpp:557`

### Root Signature Generation Flow

When shader is compiled (at scene load or first use):

```
Shader source → DXC compile → DXIL bytecode + reflection
                                 ↓
                            Analyze uniforms per set
                                 ↓
                            Generate D3D12 root signature
                            (CBV/SRV/UAV descriptor tables)
                                 ↓
                            Serialize + store in shader cache
```

### Descriptor Layout for Constants

```cpp
bool RenderingShaderContainerD3D12::_generate_root_signature(...) {
    LocalVector<D3D12_ROOT_PARAMETER1> root_params;

    // 1. Push constants (if used)
    if (reflection_data_d3d12.dxil_push_constant_stages) {
        CD3DX12_ROOT_PARAMETER1 push_constant;
        push_constant.InitAsConstants(
            reflection_data.push_constant_size / sizeof(uint32_t),
            ROOT_CONSTANT_REGISTER,  // b0 register space 0
            0,
            stages_to_d3d12_visibility(reflection_data_d3d12.dxil_push_constant_stages));
        root_params.push_back(push_constant);
    }

    // 2. Descriptor tables (resources + samplers per uniform set)
    struct TraceableDescriptorTable {
        uint32_t stages_mask = {};
        Vector<D3D12_DESCRIPTOR_RANGE1> ranges;  // CBV/SRV/UAV ranges
        uint32_t set = UINT_MAX;
    };

    Vector<TraceableDescriptorTable> resource_tables_maps;
    Vector<TraceableDescriptorTable> sampler_tables_maps;

    for (uint32_t i = 0; i < reflection_binding_set_uniforms_count.size(); i++) {
        for (uint32_t j = 0; j < uniform_count; j++) {
            const ReflectionBindingData &uniform = reflection_binding_set_uniforms_data[...];

            if (uniform.type == UNIFORM_TYPE_UNIFORM_BUFFER) {
                // Create CBV range with DATA_STATIC_WHILE_SET_AT_EXECUTE flag
                CD3DX12_DESCRIPTOR_RANGE1 range;
                range.Init(
                    D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
                    1,  // num_descriptors
                    dxil_register,
                    0,
                    D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE,
                    descriptor_offset);
                table.ranges.push_back(range);
            } else if (uniform.type == UNIFORM_TYPE_TEXTURE) {
                // SRV with DATA_VOLATILE (can change per draw)
                CD3DX12_DESCRIPTOR_RANGE1 range;
                range.Init(
                    D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                    num_descriptors,
                    dxil_register,
                    0,
                    D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE,
                    descriptor_offset);
                table.ranges.push_back(range);
            }
            // ... IMAGE → UAV, SAMPLER → sampler table, etc. ...
        }
    }

    // 3. Convert tables to root parameters
    for (const TraceableDescriptorTable &table : resource_tables_maps) {
        CD3DX12_ROOT_PARAMETER1 param;
        param.InitAsDescriptorTable(table.ranges.size(), table.ranges.ptr(),
                                    stages_to_d3d12_visibility(table.stages_mask));
        root_params.push_back(param);
    }

    // Serialize root signature
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_sig_desc(
        root_params.size(), root_params.ptr(), ...);

    ComPtr<ID3DBlob> root_sig_blob, error_blob;
    D3D12SerializeVersionedRootSignature(&root_sig_desc, &root_sig_blob, &error_blob);

    // Store for later device creation
    root_signature_bytes.resize(root_sig_blob->GetBufferSize());
    memcpy(root_signature_bytes.ptrw(), root_sig_blob->GetBufferPointer(), ...);
}
```

### uniform_set_create: Descriptor Allocation

When a shader is used in a scene (e.g., rendering a mesh):

```cpp
RDD::UniformSetID RenderingDeviceDriverD3D12::uniform_set_create(
        VectorView<BoundUniform> p_uniforms, ShaderID p_shader, uint32_t p_set_index) {

    UniformSetInfo *uniform_set_info = VersatileResource::allocate<UniformSetInfo>(...);
    const ShaderInfo::UniformSet &uniform_set = shader_info_in->sets[p_set_index];

    // Allocate descriptors from heaps
    if (uniform_set.resource_descriptor_count > 0) {
        Error err = resource_descriptor_heap.allocate(
            uniform_set.resource_descriptor_count,
            uniform_set_info->resource_descriptor_heap_alloc);
        // ... error handling ...
    }

    if (uniform_set.sampler_descriptor_count > 0) {
        // Allocate or reuse sampler heap (samplers are often shared)
        // ...
    }

    // Populate descriptors
    for (uint32_t i = 0; i < uniform_set.bindings.size(); ++i) {
        const ShaderInfo::UniformBindingInfo &binding = uniform_set.bindings[i];

        switch (binding.type) {
            case UNIFORM_TYPE_UNIFORM_BUFFER: {
                // Bind constant buffer
                const BufferInfo *buf_info = (const BufferInfo *)p_uniforms[...].id;
                D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
                cbv_desc.BufferLocation = buf_info->gpu_virtual_address;
                cbv_desc.SizeInBytes = buf_info->size;

                device->CreateConstantBufferView(&cbv_desc,
                    get_cpu_handle(uniform_set_info->resource_descriptor_heap_alloc.cpu_handle,
                                   binding.resource_descriptor_offset, ...));
            } break;

            case UNIFORM_TYPE_TEXTURE: {
                // Bind SRV
                const TextureInfo *tex_info = (const TextureInfo *)p_uniforms[...].id;
                device->CreateShaderResourceView(
                    tex_info->resource,
                    &tex_info->view_descs.srv,
                    get_cpu_handle(uniform_set_info->resource_descriptor_heap_alloc.cpu_handle,
                                   binding.resource_descriptor_offset, ...));
            } break;

            // ... SAMPLER, IMAGE (UAV), STORAGE_BUFFER, etc. ...
        }
    }

    // Store dynamic buffer info for draw-time offset resolution
    uniform_set_info->dynamic_buffers.resize(num_dynamic_buffers);
    for (uint32_t i = 0; i < num_dynamic_buffers; ++i) {
        uniform_set_info->dynamic_buffers[i] = dynamic_buffers[i];
    }

    return UniformSetID(uniform_set_info);
}
```

**Key Point**: At this stage, we're creating static descriptors. The GPU virtual address is fixed, but for dynamic buffers, the offset will be adjusted at draw time based on current frame_idx.

---

## Pattern 5: Draw-Time Offset Application

**Location**: Draw list submission phase

### D3D12 Draw Execution

```cpp
void RenderingDeviceDriverD3D12::_draw_list_end(RenderingDeviceDriver::DrawListID p_draw_list) {
    const DrawList &draw_list = draw_lists[p_draw_list];

    // ... state validation ...

    // For each draw call in the list
    for (const DrawListCommand &command : draw_list.commands) {
        // Bind uniform sets
        for (uint32_t i = 0; i < command.uniform_sets.size(); ++i) {
            const UniformSetInfo *uniform_set = command.uniform_sets[i];
            uint64_t dynamic_offsets = command.dynamic_offsets;  // Bitmask from buffer_get_dynamic_offsets()

            // Resolve dynamic offsets for this uniform set's buffers
            uint32_t buffer_idx = 0;
            for (const UniformSetInfo::DynamicBuffer &dyn_buf : uniform_set->dynamic_buffers) {
                uint64_t frame_idx = (dynamic_offsets >> (buffer_idx * 2)) & 0x3;
                uint64_t offset = frame_idx * dyn_buf.info->size;

                // Re-bind CBV with offset
                D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
                cbv_desc.BufferLocation = dyn_buf.info->gpu_virtual_address + offset;
                cbv_desc.SizeInBytes = dyn_buf.info->size;

                device->CreateConstantBufferView(&cbv_desc,
                    get_cpu_handle(uniform_set->resource_descriptor_heap_alloc.cpu_handle,
                                   binding.resource_descriptor_offset, ...));

                buffer_idx++;
            }

            // Bind descriptor heap
            cmd_list->SetDescriptorHeaps(1, &resource_descriptor_heap);
            cmd_list->SetGraphicsRootDescriptorTable(root_param_idx,
                uniform_set->resource_descriptor_heap_alloc.gpu_handle);
        }

        // Draw
        cmd_list->DrawInstanced(command.vertex_count, command.instance_count, ...);
    }
}
```

**Key Insight**: D3D12 dynamic offsets are resolved **by re-binding descriptors** at draw time. This is different from Vulkan's `vkCmdBindDescriptorSets(..., pDynamicOffsets)`.

---

## Cross-Backend Abstraction Boundary

### What Game Code Sees

```cpp
// Create a dynamic buffer
RID light_params = rd->uniform_buffer_create(
    sizeof(LightData) * 256,
    Vector<uint8_t>(),
    RD::BUFFER_CREATION_DYNAMIC_PERSISTENT_BIT);

// Upload data each frame
rd->buffer_update(light_params, 0, sizeof(LightData) * light_count, light_data_ptr);

// Create uniform set once
RD::Uniform u;
u.binding = 0;
u.uniform_type = RD::UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC;
u.append_id(light_params);
RID uniform_set = rd->uniform_set_create(Vector<RD::Uniform> { u }, shader, 0);

// Draw (many times per frame with same uniform set, different data)
rd->draw_list_bind_uniform_set(draw_list, uniform_set, 0);
rd->draw_list_draw(...);
```

**Game code never sees**:
- `frame_idx`
- Ring buffer advancement
- Offset calculation
- Descriptor rebinding

### Backend Implementation Boundary

Both D3D12 and Vulkan implement:
```cpp
virtual uint8_t *buffer_persistent_map_advance(BufferID p_buffer, uint64_t p_frames_drawn);
virtual uint64_t buffer_get_dynamic_offsets(Span<BufferID> p_buffers);
virtual uint64_t buffer_get_dynamic_offsets(Span<BufferID> p_buffers);
```

Everything else is backend-specific:
- Memory model (Vulkan: UMA + manual flush; D3D12: write-combined WC)
- Descriptor management (D3D12: CPU-side descriptor heaps; Vulkan: GPU-side descriptor sets)
- Draw submission (D3D12: re-bind descriptors; Vulkan: VkCmdBindDescriptorSets with dynamic offsets)

---

## Leanness Lessons for UzEngine

### 1. Explicit Frame-Aware Pooling Beats Implicit

**Godot's approach**: `BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT` flag + explicit `prepare_for_upload()` API
- Pros: Game code is aware of frame boundaries; ring logic is predictable
- Cons: Slightly more verbose API; requires discipline (can't map twice per frame)

**UE5's approach**: Implicit double/triple buffering in RHI
- Pros: Simpler API
- Cons: Hidden overhead; harder to debug frame stalls

**UzEngine lesson**: Use Godot's explicit model. It's harder to misuse.

### 2. Ring Buffers Are Better Than Staging Buffers for Per-Frame Data

**Staging buffer (non-dynamic)**: Write to staging → copy to GPU → wait
- Many copies per frame = memory bandwidth waste
- Synchronization complexity (fences, readback)

**Ring buffer (dynamic persistent)**: Write directly to CPU-visible slot → GPU reads same frame
- One allocation, zero copies
- Simpler synchronization (just avoid double-map)

**UzEngine lesson**: For constant buffers (< 4 KB per draw), ring buffers are optimal. Use staging only for bulk asset uploads.

### 3. Offset Encoding Avoids Descriptor Heap Churn

**Alternative**: Create new descriptor for each frame's offset
- Wastes descriptor heap space
- Requires descriptor allocation per frame per uniform set

**Godot's approach**: Encode frame_idx in a 64-bit mask, decode at draw time
- One descriptor per binding, reused across frames
- Offset math is cheap (single shift+mask per buffer)

**UzEngine lesson**: Use offset encoding for dynamic buffers. Save descriptor space.

### 4. Template-Based MultiUmaBuffer is More Flexible Than Fixed-Size Ring

**Fixed ring**: 3-frame buffer, always
- Simple but inflexible (spikes require fallback)

**Template**: `MultiUmaBuffer<N, MAX_EXTRA>`
- N = number of slots per frame (can split one CBV into multiple passes)
- MAX_EXTRA = cap on emergency buffers from spikes

**UzEngine lesson**: If implementing ring buffers, template them. Worth the complexity.

### 5. Safety Guards Are Non-Negotiable

**Godot enforces at DEBUG_ENABLED**:
- Can't map buffer twice in same frame
- Can't update buffer while recording draw list
- Can't exceed buffer bounds

**UzEngine lesson**: Frame synchronization bugs are subtle. Invest in assertions and logging. Make them always-on in dev builds, not just debug.

---

## Adaptation for UzEngine

### Recommended Design

1. **Ring Buffer Allocator** (in `engine/rhi/buffer.h`):
   ```cpp
   struct DynamicBufferSlot {
       RID buffer_rid;
       uint32_t frame_idx;
       uint8_t *persistent_ptr;
   };

   class DynamicBuffer {
       Vector<RID> ring;
       uint32_t curr_idx = 0;
       uint64_t last_frame = 0;

       uint8_t *map_for_frame(uint64_t frame_number);
       void advance_frame();
   };
   ```

2. **Per-Backend Implementation** (in `engine/rhi/backends/dx12/dx12_buffer.cpp`):
   - Allocate `size * frame_count` on creation
   - `persistent_map_advance()` returns pointer to current frame slot
   - No explicit `Unmap()` needed (persistent mapping)

3. **Draw-Time Offset Resolution** (in `engine/rhi/backends/dx12/dx12_draw.cpp`):
   - Collect frame_idx bitmask for all dynamic buffers in uniform set
   - Decode and apply offsets when binding descriptors

4. **Safety** (everywhere):
   - Add `UZ_ENSURE(last_frame != current_frame)` to catch double-maps
   - Log at INFO level when ring expands beyond expected size
   - Profile descriptor rebinding cost (should be negligible)

---

## References

**Godot Source Locations**:
- MultiUmaBuffer template: `/e/godot/servers/rendering/multi_uma_buffer.h`
- RenderingDevice API: `/e/godot/servers/rendering/rendering_device.cpp:710`
- D3D12 backend: `/e/godot/drivers/d3d12/rendering_device_driver_d3d12.cpp`
- Vulkan backend: `/e/godot/drivers/vulkan/rendering_device_driver_vulkan.cpp`
- Root signature generation: `/e/godot/drivers/d3d12/rendering_shader_container_d3d12.cpp:557`

**Key Takeaway**: Godot's uniform buffer abstraction is a masterclass in cross-API design. The ring buffer idea is simple but solves real synchronization problems elegantly. The offset encoding avoids descriptor bloat. Most importantly, the explicit API (prepare_for_upload, buffer_update with frame awareness) makes frame synchronization a first-class concern, not an afterthought.
