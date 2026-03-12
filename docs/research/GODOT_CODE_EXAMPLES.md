# Godot Uniform Buffer Implementation — Concrete Code Examples

## Example 1: Ring Buffer with Triple-Buffering

**File Structure**:
- `/e/godot/servers/rendering/rendering_device.cpp:726` (decision point)
- `/e/godot/drivers/vulkan/rendering_device_driver_vulkan.cpp:2043` (Vulkan implementation)
- `/e/godot/drivers/d3d12/rendering_device_driver_d3d12.cpp:1042` (D3D12 implementation)

### Allocation Time (Vulkan)

```cpp
// From /e/godot/drivers/vulkan/rendering_device_driver_vulkan.cpp:1875

RDD::BufferID RenderingDeviceDriverVulkan::buffer_create(
        uint64_t p_size,
        BitField<BufferUsageBits> p_usage,
        MemoryAllocationType p_allocation_type,
        uint64_t p_frames_drawn) {

    // Alignment: some GPUs (NVIDIA) require 256-byte alignment for UBOs
    uint32_t alignment = 16u;
    if (p_usage.has_flag(BUFFER_USAGE_UNIFORM_BIT)) {
        alignment = MAX(alignment, physical_device_properties.limits.minUniformBufferOffsetAlignment);
    }

    // Align the requested size (critical for ring buffer)
    p_size = STEPIFY(p_size, alignment);  // Round up to alignment boundary

    const size_t original_size = p_size;

    // KEY INSIGHT: Dynamic buffers are allocated for multiple frames
    if (p_usage.has_flag(BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT)) {
        p_size = p_size * frame_count;  // frame_count = 2 or 3 (triple-buffering)
    }

    // Create VkBuffer
    VkBufferCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create_info.size = p_size;
    create_info.usage = p_usage & ~BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT;  // Remove custom flag
    create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Memory allocation strategy
    VmaMemoryUsage vma_usage = VMA_MEMORY_USAGE_UNKNOWN;
    VmaAllocationCreateInfo alloc_create_info = {};

    if (p_allocation_type == MEMORY_ALLOCATION_TYPE_GPU) {
        vma_usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        // For dynamic buffers: request host-visible, write-combined memory
        if (p_usage.has_flag(BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT)) {
            alloc_create_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        }

        alloc_create_info.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        if (p_size <= SMALL_ALLOCATION_MAX_SIZE) {
            // Use small-allocation pool if possible
            uint32_t mem_type_index = 0;
            vmaFindMemoryTypeIndexForBufferInfo(allocator, &create_info,
                                               &alloc_create_info, &mem_type_index);
            alloc_create_info.usage = vma_usage;
            alloc_create_info.memoryTypeIndex = mem_type_index;
        }
    }

    // Actually allocate
    VkBuffer vk_buffer;
    VmaAllocation allocation;
    vmaCreateBuffer(allocator, &create_info, &alloc_create_info,
                   &vk_buffer, &allocation, nullptr);

    // For dynamic buffers: map and store persistent pointer
    BufferDynamicInfo *buf_info = nullptr;
    if (p_usage.has_flag(BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT)) {
        buf_info = new BufferDynamicInfo();
        buf_info->size = original_size;  // Store original, not multiplied
        buf_info->frame_idx = 0;
        buf_info->last_frame_mapped = UINT64_MAX;

        // Map the entire ring buffer for persistent CPU access
        vmaMapMemory(allocator, allocation, (void**)&buf_info->persistent_ptr);
    }

    return BufferID((uint64_t)buf_info);
}
```

**Key observations**:
1. `p_size` is multiplied by `frame_count` for ring buffer
2. VMA allocation flags include `HOST_ACCESS_SEQUENTIAL_WRITE_BIT` (write-combined memory)
3. Persistent pointer is mapped once at creation (never unmapped)
4. `BufferDynamicInfo` stores `original_size` (not multiplied) for offset calculations

### Update Time (RenderingDevice)

```cpp
// From /e/godot/servers/rendering/rendering_device.cpp:710

Error RenderingDevice::buffer_update(RID p_buffer, uint32_t p_offset, uint32_t p_size,
                                     const void *p_data, bool p_skip_check) {
    ERR_RENDER_THREAD_GUARD_V(ERR_UNAVAILABLE);

    // Safety: can't update while recording draw/compute/raytracing lists
    ERR_FAIL_COND_V_MSG(draw_list.active && !p_skip_check, ERR_INVALID_PARAMETER,
        "Updating buffers is forbidden during creation of a draw list.");
    ERR_FAIL_COND_V_MSG(compute_list.active && !p_skip_check, ERR_INVALID_PARAMETER,
        "Updating buffers is forbidden during creation of a compute list.");

    Buffer *buffer = _get_buffer_from_owner(p_buffer);
    ERR_FAIL_NULL_V_MSG(buffer, ERR_INVALID_PARAMETER,
        "Buffer argument is not a valid buffer of any type.");
    ERR_FAIL_COND_V_MSG(p_offset + p_size > buffer->size, ERR_INVALID_PARAMETER,
        "Attempted to write buffer past the end.");

    // FAST PATH: Dynamic persistent buffer (ring buffer)
    // This is the happy path for per-frame constants
    if (buffer->usage.has_flag(RDD::BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT)) {
        // Get pointer to current frame's slot in the ring
        uint8_t *dst_data = driver->buffer_persistent_map_advance(
            buffer->driver_id, frames_drawn);

        // Direct CPU copy (no GPU stall, no staging buffer)
        memcpy(dst_data + p_offset, p_data, p_size);
        direct_copy_count++;

        // Flush if needed (Vulkan only; D3D12 is no-op)
        buffer_flush(p_buffer);
        return OK;
    }

    // SLOW PATH: Non-dynamic buffer (uses staging buffer)
    // ... this handles partial updates, static uploads, etc. ...
}
```

**Key observations**:
1. The flag check (`BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT`) gates the fast path
2. Call to `buffer_persistent_map_advance()` returns pointer to current frame's slot
3. Simple `memcpy()` — no complex staging logic
4. `buffer_flush()` for Vulkan non-coherent memory; D3D12 has no-op

### Frame Advancement (Vulkan)

```cpp
// From /e/godot/drivers/vulkan/rendering_device_driver_vulkan.cpp:2043

uint8_t *RenderingDeviceDriverVulkan::buffer_persistent_map_advance(
        BufferID p_buffer, uint64_t p_frames_drawn) {

    BufferDynamicInfo *buf_info = (BufferDynamicInfo *)p_buffer.id;

    // Safety guard: prevent GPU read-write race condition
    #ifdef DEBUG_ENABLED
    ERR_FAIL_COND_V_MSG(buf_info->last_frame_mapped == p_frames_drawn, nullptr,
        "Buffers with BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT must only be mapped once per frame. "
        "Otherwise there could be race conditions with the GPU.");
    buf_info->last_frame_mapped = p_frames_drawn;
    #endif

    // RING BUFFER ADVANCEMENT: move to next frame
    buf_info->frame_idx = (buf_info->frame_idx + 1u) % frame_count;

    // Return pointer to this frame's slot
    // Arithmetic: persistent_ptr + (frame_idx * original_size)
    return buf_info->persistent_ptr + buf_info->frame_idx * buf_info->size;
}
```

**Ring buffer arithmetic**:
- Frame 0 writes to: `persistent_ptr + 0 * original_size`
- Frame 1 writes to: `persistent_ptr + 1 * original_size`
- Frame 2 writes to: `persistent_ptr + 2 * original_size`
- Frame 3 wraps back: `persistent_ptr + 0 * original_size` (GPU done reading frame 0)

**Concurrency model**:
- CPU writes frame N+1 while GPU reads frame N (safe)
- GPU cannot read frame N+1 until next frame boundary

---

## Example 2: MultiUmaBuffer in Practice

**Location**: `/e/godot/servers/rendering/multi_uma_buffer.h:283`

### Template Definition

```cpp
template <uint32_t NUM_BUFFERS, uint32_t MAX_EXTRA_BUFFERS = UINT32_MAX>
class MultiUmaBuffer : public MultiUmaBufferBase {
    struct BufferInfo {
        uint32_t size_bytes = 0;
        MultiUmaBufferType type = MultiUmaBufferType::UNIFORM;
    };

    BufferInfo buffer_info[NUM_BUFFERS];
    #ifdef DEV_ENABLED
    bool can_upload[NUM_BUFFERS] = {};
    #endif

public:
    MultiUmaBuffer(const char *p_debug_name) :
            MultiUmaBufferBase(MAX_EXTRA_BUFFERS, p_debug_name) {}

    /// Set size for a slot. Must be called before first use.
    void set_uniform_size(uint32_t p_idx, uint32_t p_size_bytes) {
        set_size(p_idx, p_size_bytes, MultiUmaBufferType::UNIFORM);
    }

    void set_storage_size(uint32_t p_idx, uint32_t p_size_bytes) {
        set_size(p_idx, p_size_bytes, MultiUmaBufferType::STORAGE);
    }

    /// Prepare for next upload.
    /// Allocates new buffers if this frame's pass count exceeds capacity.
    void prepare_for_upload() {
        prepare_for_map(false);  // false = not appending, new buffer
    }

    /// Get raw pointer to persistent mapping.
    void *map_raw_for_upload(uint32_t p_idx) {
        #ifdef DEV_ENABLED
        DEV_ASSERT(can_upload[p_idx] && "Forgot to prepare_for_upload first!");
        can_upload[p_idx] = false;
        #endif

        RenderingDevice *rd = RD::RenderingDevice::get_singleton();
        return rd->buffer_persistent_map_advance(buffers[curr_idx * NUM_BUFFERS + p_idx]);
    }

    /// Get RID for current buffer in slot.
    RID _get(uint32_t p_idx) {
        return buffers[curr_idx * NUM_BUFFERS + p_idx];
    }

    /// Upload data to slot.
    void upload(uint32_t p_idx, const void *p_src_data, uint32_t p_size_bytes) {
        #ifdef DEV_ENABLED
        DEV_ASSERT(can_upload[p_idx] && "Forgot to prepare_for_upload first!");
        can_upload[p_idx] = false;
        #endif

        RenderingDevice *rd = RD::RenderingDevice::get_singleton();
        rd->buffer_update(buffers[curr_idx * NUM_BUFFERS + p_idx], 0, p_size_bytes,
                         p_src_data, true);  // true = skip list-active check
    }

private:
    bool prepare_for_map(bool p_append) {
        RenderingDevice *rd = RD::RenderingDevice::get_singleton();
        const uint64_t frames_drawn = rd->get_frames_drawn();

        if (last_frame_mapped == frames_drawn) {
            // Same frame — allocate new buffer for next pass
            if (!p_append) {
                ++curr_idx;
            }
        } else {
            // New frame — reset to start of ring
            p_append = false;
            curr_idx = 0u;
            if (max_extra_buffers != UINT32_MAX) {
                shrink_to_max_extra_buffers();  // Cleanup spikes
            }
        }

        last_frame_mapped = frames_drawn;

        // Allocate more buffers if needed
        if (curr_idx * NUM_BUFFERS >= buffers.size()) {
            push();  // Create new set of NUM_BUFFERS
        }

        #ifdef DEV_ENABLED
        if (!p_append) {
            for (size_t i = 0u; i < NUM_BUFFERS; ++i) {
                can_upload[i] = true;
            }
        }
        #endif

        return !p_append;
    }

    void push() {
        RenderingDevice *rd = RD::RenderingDevice::get_singleton();
        for (uint32_t i = 0u; i < NUM_BUFFERS; ++i) {
            const BufferInfo &info = buffer_info[i];
            RID buffer;
            switch (info.type) {
                case MultiUmaBufferType::STORAGE:
                    buffer = rd->storage_buffer_create(info.size_bytes, Vector<uint8_t>(),
                        BitField<RenderingDevice::StorageBufferUsage>(),
                        RD::BUFFER_CREATION_DYNAMIC_PERSISTENT_BIT);
                    break;
                case MultiUmaBufferType::VERTEX:
                    buffer = rd->vertex_buffer_create(info.size_bytes, Vector<uint8_t>(),
                        RD::BUFFER_CREATION_DYNAMIC_PERSISTENT_BIT);
                    break;
                case MultiUmaBufferType::UNIFORM:
                default:
                    buffer = rd->uniform_buffer_create(info.size_bytes, Vector<uint8_t>(),
                        RD::BUFFER_CREATION_DYNAMIC_PERSISTENT_BIT);
                    break;
            }
            buffers.push_back(buffer);
        }
    }
};
```

### Real-World Usage in Godot

**File**: `/e/godot/drivers/d3d12/rendering_device_driver_d3d12.cpp:3408`

```cpp
// Shadow mapping code (simplified)
class ShadowRenderer {
    MultiUmaBuffer<1> shadow_params;  // 1 buffer slot per frame

    void init() {
        shadow_params.set_uniform_size(0, sizeof(ShadowRenderParams) * MAX_CASCADES);
    }

    void render_shadows(const Scene &scene) {
        // Multiple passes (one per cascade)
        for (uint32_t cascade = 0; cascade < scene.shadow_cascades.size(); ++cascade) {
            // Prepare for this pass (allocate new buffer if spike)
            shadow_params.prepare_for_upload();

            // Compute per-cascade data
            ShadowRenderParams params = compute_shadow_params(scene, cascade);

            // Upload to current buffer in ring
            shadow_params.upload(0, &params, sizeof(ShadowRenderParams));

            // Create/reuse uniform set
            if (!shadow_sets[cascade]) {
                RD::Uniform u;
                u.binding = 0;
                u.uniform_type = RD::UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC;
                u.append_id(shadow_params._get(0));  // Get current RID
                shadow_sets[cascade] = rd->uniform_set_create({u}, shader, 0);
            }

            // Render shadow map pass
            render_shadow_pass(cascade, shadow_sets[cascade]);
        }
    }

    void cleanup() {
        shadow_params.uninit();
    }
};
```

**Key behavior**:
- Frame 1, cascade 0: allocate buffer 0, upload, render
- Frame 1, cascade 1: allocate buffer 1, upload, render
- Frame 1, cascade 2: allocate buffer 2, upload, render
- Frame 2 (new frame): reset to buffer 0, upload new data
- Spike frame with 24 cascades: allocate buffers 3-23 on demand

---

## Example 3: D3D12 Offset Encoding

**Location**: `/e/godot/drivers/d3d12/rendering_device_driver_d3d12.cpp:1053-3684`

### Computing Dynamic Offsets

```cpp
uint64_t RenderingDeviceDriverD3D12::buffer_get_dynamic_offsets(Span<BufferID> p_buffers) {
    uint64_t mask = 0u;
    uint64_t shift = 0u;

    for (const BufferID &buf : p_buffers) {
        const BufferInfo *buf_info = (const BufferInfo *)buf.id;
        if (!buf_info->is_dynamic()) {
            continue;  // Skip static buffers (no offset needed)
        }

        const BufferDynamicInfo *dyn_buf = (const BufferDynamicInfo *)buf.id;

        // Encode frame_idx as 2 bits (0-3 = 4 frames max)
        mask |= dyn_buf->frame_idx << shift;
        shift += 2UL;  // Next buffer's bits start 2 bits later
    }

    return mask;  // Example: 0b...0011_0001_0000
}
```

**Encoding example**:
- Buffer 0 (frame_idx=0): `0b00` at bits [1:0]
- Buffer 1 (frame_idx=1): `0b01` at bits [3:2]
- Buffer 2 (frame_idx=2): `0b10` at bits [5:4]
- Buffer 3 (frame_idx=3): `0b11` at bits [7:6]
- ...
- Result: `mask = 0b11_10_01_00` (3 dynamic buffers, frame indices 3,2,1,0)

### Applying Dynamic Offsets at Draw Time

```cpp
void RenderingDeviceDriverD3D12::_execute_dynamic_buffer_bindings(
        const UniformSetInfo::DynamicBuffer &dynamic_buffer,
        uint64_t p_dynamic_offsets) {

    // Extract frame_idx for this buffer from bitmask
    // If dynamic_buffer is the 2nd buffer in the set:
    // frame_idx = (p_dynamic_offsets >> (1 * 2)) & 0x3
    uint64_t buffer_frame_idx = (p_dynamic_offsets >> (buffer_index * 2)) & 0x3;

    // Compute GPU address for this frame's slot
    uint64_t dynamic_offset = buffer_frame_idx * dynamic_buffer.info->size;
    D3D12_GPU_VIRTUAL_ADDRESS buffer_location =
        dynamic_buffer.info->gpu_virtual_address + dynamic_offset;

    // Create CBV pointing to this frame's slot
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
    cbv_desc.BufferLocation = buffer_location;
    cbv_desc.SizeInBytes = dynamic_buffer.info->size;

    // Bind to descriptor heap
    device->CreateConstantBufferView(&cbv_desc, descriptor_cpu_handle);
}
```

**Complexity analysis**:
- Encoding: O(num_dynamic_buffers) — one shift+mask per buffer
- Decoding: O(num_dynamic_buffers) — one shift+mask per buffer at draw time
- Space: 2 bits per dynamic buffer (supports 32 in 64 bits)

---

## Example 4: Root Signature Generation

**Location**: `/e/godot/drivers/d3d12/rendering_shader_container_d3d12.cpp:557`

### Building Descriptor Ranges

```cpp
bool RenderingShaderContainerD3D12::_generate_root_signature(...) {
    LocalVector<D3D12_ROOT_PARAMETER1> root_params;

    // 1. Push constants (if shader uses)
    if (reflection_data_d3d12.dxil_push_constant_stages) {
        CD3DX12_ROOT_PARAMETER1 push_constant;
        push_constant.InitAsConstants(
            reflection_data.push_constant_size / sizeof(uint32_t),
            ROOT_CONSTANT_REGISTER,
            0,
            stages_to_d3d12_visibility(reflection_data_d3d12.dxil_push_constant_stages));
        root_params.push_back(push_constant);
    }

    // 2. Descriptor tables for each uniform set
    struct TraceableDescriptorTable {
        uint32_t stages_mask = {};
        Vector<D3D12_DESCRIPTOR_RANGE1> ranges;
        uint32_t set = UINT_MAX;
    };

    Vector<TraceableDescriptorTable> resource_tables_maps;
    uint32_t binding_start = 0;

    // Iterate over uniform sets and bindings
    for (uint32_t set_idx = 0; set_idx < reflection_binding_set_uniforms_count.size(); set_idx++) {
        bool first_resource_in_set = true;
        uint32_t uniform_count = reflection_binding_set_uniforms_count[set_idx];

        for (uint32_t binding_idx = 0; binding_idx < uniform_count; binding_idx++) {
            const ReflectionBindingData &uniform =
                reflection_binding_set_uniforms_data[binding_start + binding_idx];

            ReflectionBindingDataD3D12 &uniform_d3d12 =
                reflection_binding_set_uniforms_data_d3d12.ptrw()[binding_start + binding_idx];

            // Skip unused bindings
            if ((ResourceClass)(uniform_d3d12.resource_class) == RES_CLASS_INVALID &&
                !uniform_d3d12.has_sampler) {
                continue;
            }

            // Lambda to insert descriptor ranges
            auto insert_range = [set_idx](
                D3D12_DESCRIPTOR_RANGE_TYPE p_range_type,
                uint32_t p_num_descriptors,
                uint32_t p_dxil_register,
                uint32_t p_dxil_stages_mask,
                uint32_t &r_descriptor_offset,
                uint32_t &r_descriptor_count,
                bool &r_first_in_set,
                Vector<TraceableDescriptorTable> &r_tables) {

                r_descriptor_offset = r_descriptor_count;

                if (r_first_in_set) {
                    r_tables.resize(r_tables.size() + 1);
                    r_first_in_set = false;
                }

                TraceableDescriptorTable &table = r_tables.write[r_tables.size() - 1];
                table.stages_mask |= p_dxil_stages_mask;
                table.set = set_idx;

                CD3DX12_DESCRIPTOR_RANGE1 range;

                // Determine flags based on descriptor type
                D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
                if (p_range_type == D3D12_DESCRIPTOR_RANGE_TYPE_SRV ||
                    p_range_type == D3D12_DESCRIPTOR_RANGE_TYPE_UAV) {
                    // Textures/UAVs can change per draw
                    flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
                } else if (p_range_type == D3D12_DESCRIPTOR_RANGE_TYPE_CBV) {
                    // Constants are stable across draws (unless dynamic offset changes)
                    flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
                }

                range.Init(p_range_type, p_num_descriptors, p_dxil_register, 0, flags,
                          r_descriptor_offset);
                r_descriptor_count += p_num_descriptors;
                table.ranges.push_back(range);
            };

            // Process each uniform binding type
            switch (uniform.type) {
                case RDC::UNIFORM_TYPE_UNIFORM_BUFFER: {
                    insert_range(
                        D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
                        1,  // One buffer per binding
                        uniform_d3d12.dxil_register,
                        uniform_d3d12.dxil_stages,
                        uniform_d3d12.resource_descriptor_offset,
                        resource_descriptor_count,
                        first_resource_in_set,
                        resource_tables_maps);
                } break;

                case RDC::UNIFORM_TYPE_TEXTURE: {
                    insert_range(
                        D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                        MAX(1u, uniform.length),  // Array of textures
                        uniform_d3d12.dxil_register,
                        uniform_d3d12.dxil_stages,
                        uniform_d3d12.resource_descriptor_offset,
                        resource_descriptor_count,
                        first_resource_in_set,
                        resource_tables_maps);
                } break;

                case RDC::UNIFORM_TYPE_IMAGE: {
                    insert_range(
                        D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
                        MAX(1u, uniform.length),  // Array of RW textures
                        uniform_d3d12.dxil_register,
                        uniform_d3d12.dxil_stages,
                        uniform_d3d12.resource_descriptor_offset,
                        resource_descriptor_count,
                        first_resource_in_set,
                        resource_tables_maps);
                } break;

                // ... Storage buffers, samplers, etc. ...
            }
        }

        binding_start += uniform_count;
    }

    // 3. Convert descriptor table maps to root parameters
    for (const TraceableDescriptorTable &table : resource_tables_maps) {
        CD3DX12_ROOT_PARAMETER1 param;
        param.InitAsDescriptorTable(
            table.ranges.size(),
            table.ranges.ptr(),
            stages_to_d3d12_visibility(table.stages_mask));
        root_params.push_back(param);
    }

    // 4. Serialize root signature
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_sig_desc(
        root_params.size(),
        root_params.ptr(),
        0,  // samplers (separate root parameters)
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> root_sig_blob, error_blob;
    D3D12SerializeVersionedRootSignature(&root_sig_desc, &root_sig_blob, &error_blob);

    if (!root_sig_blob) {
        // Error handling
        const char *error_msg = (const char *)error_blob->GetBufferPointer();
        ERR_FAIL_V_MSG(false, "D3D12 Root Signature compilation failed: " + String(error_msg));
    }

    // 5. Store for later device creation
    root_signature_bytes.resize(root_sig_blob->GetBufferSize());
    memcpy(root_signature_bytes.ptrw(), root_sig_blob->GetBufferPointer(),
          root_sig_blob->GetBufferSize());

    return true;
}
```

**Key points**:
1. Root signature is generated once during shader compilation
2. Serialized to blob and stored in shader cache
3. Device creates root signature from blob at shader bind time (cheap, no serialization)
4. CBV ranges marked `DATA_STATIC_WHILE_SET_AT_EXECUTE` (safe for rebinding)
5. SRV/UAV ranges marked `DATA_VOLATILE` (can change per draw)

---

## Example 5: Safety Guards

**Debug assertions from `/e/godot/drivers/vulkan/rendering_device_driver_vulkan.cpp:2046`**:

```cpp
#ifdef DEBUG_ENABLED
ERR_FAIL_COND_V_MSG(buf_info->last_frame_mapped == p_frames_drawn, nullptr,
    "Buffers with BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT must only be mapped once per frame. "
    "Otherwise there could be race conditions with the GPU. "
    "Amalgamate all data uploading into one map(), use an extra buffer or remove the bit.");

buf_info->last_frame_mapped = p_frames_drawn;
#endif
```

**When this fires**:
```cpp
// Frame N
rd->buffer_update(dynamic_cbuffer, 0, size1, data1);  // OK, frame N = last_frame_mapped
rd->buffer_update(dynamic_cbuffer, 0, size2, data2);  // ERROR! frame N == last_frame_mapped
```

**Fix**: Merge into one call:
```cpp
struct CombinedData {
    Data1 d1;
    Data2 d2;
};
CombinedData combined = {data1, data2};
rd->buffer_update(dynamic_cbuffer, 0, sizeof(CombinedData), &combined);
```

**Or** use separate buffers:
```cpp
MultiUmaBuffer<2> buffers;
buffers.set_uniform_size(0, sizeof(Data1));
buffers.set_uniform_size(1, sizeof(Data2));
buffers.prepare_for_upload();
buffers.upload(0, data1, sizeof(Data1));
buffers.upload(1, data2, sizeof(Data2));
```

---

## Summary Table

| Concept | Vulkan Code | D3D12 Code | Purpose |
|---------|-------------|-----------|---------|
| Allocation | `vmaCreateBuffer(..., size * frame_count, ...)` | `CreateCommittedResource(..., size * frames.size())` | Ring buffer: 3x size for triple-buffering |
| Persistent ptr | `vmaMapMemory(..., &persistent_ptr)` | `Map(..., (void**)&ptr); never Unmap()` | CPU-visible memory always mapped |
| Ring advance | `frame_idx = (frame_idx + 1) % frame_count` | `frame_idx = (frame_idx + 1) % frames.size()` | Identical algorithm both backends |
| Update copy | `memcpy(persistent_ptr + frame_idx * size, data, size)` | Same | Direct write, no staging |
| Flush | `vmaFlushAllocations(..., offsets[frame_idx * size])` | No-op (WC memory) | Non-coherent memory needs flush |
| Descriptor binding | Baked at compile time | `CreateConstantBufferView(gpu_address + offset, ...)` | Frame offset applied at draw time |
| Offset encoding | Via descriptor `VkDescriptorBufferInfo` | Via 2-bit bitmask | Multiple buffers per uniform set |

All roads lead to the same goal: **update GPU data CPU-side without GPU stall, with frame-aware ring pooling**.
