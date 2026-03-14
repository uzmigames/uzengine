# Godot Backend Compatibility Analysis: Vulkan vs OpenGL vs DX11/DX12

## Executive Summary

Godot's RenderingDevice abstraction successfully isolates game code from backend-specific APIs (Vulkan, D3D12, Metal) through a **type-erased handler pattern** combined with **Descriptor Set abstraction**. Key architectural patterns:

1. **Opaque Resource IDs** — RID (uint64_t) wraps backend pointers, preventing direct API access
2. **Descriptor Set Pattern** — Samplers, textures, and buffers are bound as sets at uniform_set_create time, not per-frame
3. **Immediate Context for DX11** — DX11 has no command buffers; immediate context receives commands directly
4. **Barrier Abstraction** — Translates generic barrier descriptions to backend-specific state models
5. **Immutable Samplers** — Samplers can be embedded in pipeline layout (Vulkan) or cached in descriptor heap (DX12/DX11)

The lesson for UzEngine: **resource binding must happen at descriptor set creation, not at draw time**. DX11's per-frame `SetFragmentTexture()` calls break this pattern and cause crashes during render graph execution when resources are not properly tracked.

---

## 1. How RenderingDevice Abstracts Backend Differences

### 1.1 Design Principles (from rendering_device_driver.h)

```cpp
// Very little validation is done, and normally only in dev or debug builds.
// Error reporting is generally simple: returning an id of 0 or a false boolean.
// Certain enums/constants/structs follow Vulkan values/layout. That makes
// things easier for RDDVulkan (it asserts compatibility).
// We allocate as little as possible in functions expected to be quick.
// We try to back opaque ids with the native ones or memory addresses.
```

### 1.2 Opaque Resource IDs (ID Pattern)

All backends use the same ID type hierarchy:

```cpp
struct ID {
    uint64_t id = 0;  // Can hold pointer or native handle
};

DEFINE_ID(Buffer);     // BufferID
DEFINE_ID(Texture);    // TextureID
DEFINE_ID(Sampler);    // SamplerID
DEFINE_ID(Shader);     // ShaderID
DEFINE_ID(UniformSet); // UniformSetID
DEFINE_ID(Pipeline);   // PipelineID
```

**Vulkan implementation** — Cast to pointer directly:
```cpp
TextureInfo* tex_info = (TextureInfo*)texture_id.id;
// TextureInfo wraps: VkImage, VkImageView, metadata
```

**DX12 implementation** — Cast to pointer (with bookkeeping):
```cpp
TextureInfo* tex_info = (TextureInfo*)texture_id.id;
// TextureInfo wraps: ID3D12Resource*, descriptor view SRV/UAV, state tracking
```

**DX11 implementation** — Cast to pointer:
```cpp
TextureInfo* tex_info = (TextureInfo*)texture_id.id;
// TextureInfo wraps: ID3D11Texture*, ID3D11ShaderResourceView*, etc.
```

**Key insight**: IDs are **backend-agnostic opaque handles**. Game code never touches native D3D11/Vulkan pointers.

---

## 2. Descriptor Set Pattern (The Core Abstraction)

### 2.1 What is a Descriptor Set?

A **descriptor set** (Godot terminology) is a **snapshot of resource bindings** created at `uniform_set_create()` time and reused across frames. It encapsulates:
- Sampler IDs (references to SamplerID)
- Texture IDs (references to TextureID)
- Buffer IDs (references to BufferID)

**NOT** per-frame updates. Once created, a uniform set is immutable (in Godot's model).

### 2.2 Vulkan: Descriptor Sets (Native Match)

```cpp
// Vulkan has native descriptor sets — Godot maps directly
RDD::UniformSetID RenderingDeviceDriverVulkan::uniform_set_create(
    VectorView<BoundUniform> p_uniforms,
    ShaderID p_shader,
    uint32_t p_set_index,
    int p_linear_pool_index)
{
    // For each uniform (sampler, texture, buffer):
    for (uint32_t i = 0; i < p_uniforms.size(); i++) {
        const BoundUniform &uniform = p_uniforms[i];

        switch (uniform.type) {
            case UNIFORM_TYPE_SAMPLER: {
                // Extract sampler handle
                for (uint32_t j = 0; j < uniform.ids.size(); j++) {
                    vk_img_infos[j].sampler = (VkSampler)uniform.ids[j].id;
                    vk_img_infos[j].imageView = VK_NULL_HANDLE;  // Separate
                    vk_img_infos[j].imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                }
                vk_writes[writes_amount].descriptorType =
                    VK_DESCRIPTOR_TYPE_SAMPLER;
                vk_writes[writes_amount].pImageInfo = vk_img_infos;
            } break;

            case UNIFORM_TYPE_SAMPLER_WITH_TEXTURE: {
                // Both sampler and image in single descriptor
                for (uint32_t j = 0; j < uniform.ids.size() / 2; j++) {
                    vk_img_infos[j].sampler = (VkSampler)uniform.ids[j * 2 + 0].id;
                    vk_img_infos[j].imageView =
                        ((TextureInfo *)uniform.ids[j * 2 + 1].id)->vk_view;
                    vk_img_infos[j].imageLayout =
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;  // Assumed
                }
                vk_writes[writes_amount].descriptorType =
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            } break;
        }
    }

    // Execute: vkUpdateDescriptorSets() binds all at once
    VkUpdateDescriptorSetWrite write = {...};
    vkUpdateDescriptorSets(vk_device, writes_amount, vk_writes, ...);
}
```

**Key points**:
- Samplers and textures are separate or combined at creation time
- Layout is hardcoded (SHADER_READ_ONLY for sampled, GENERAL for storage)
- **IMMUTABLE**: Once created, the descriptor set is never modified

### 2.3 DX12: Descriptor Heap Ranges (Emulated Sets)

DX12 has no descriptor sets — Godot emulates them by allocating **ranges in descriptor heaps**:

```cpp
RDD::UniformSetID RenderingDeviceDriverD3D12::uniform_set_create(
    VectorView<BoundUniform> p_uniforms,
    ShaderID p_shader,
    uint32_t p_set_index,
    int p_linear_pool_index)
{
    UniformSetInfo *uniform_set_info = allocate<UniformSetInfo>();

    // Allocate descriptors in RESOURCE heap (textures, buffers)
    if (uniform_set.resource_descriptor_count > 0) {
        resource_descriptor_heap.allocate(
            uniform_set.resource_descriptor_count,
            uniform_set_info->resource_descriptor_heap_alloc);
    }

    // Allocate descriptors in SAMPLER heap
    if (uniform_set.sampler_descriptor_count > 0) {
        // OPTIMIZATION: Cache samplers by hash
        uint32_t sampler_key = hash_samplers(p_uniforms);

        RBMap<uint32_t, SamplerAllocation>::Iterator find =
            sampler_descriptor_heap_allocations.find(sampler_key);
        if (find != sampler_descriptor_heap_allocations.end()) {
            // Reuse existing heap allocation
            uniform_set_info->sampler_descriptor_heap_alloc =
                &find->value;
            ++uniform_set_info->sampler_descriptor_heap_alloc->use_count;
        } else {
            // Create new allocation
            sampler_descriptor_heap.allocate(
                uniform_set.sampler_descriptor_count,
                *uniform_set_info->sampler_descriptor_heap_alloc);
        }
    }

    // Create actual descriptors at the allocated CPU handles
    for (uint32_t i = 0; i < p_uniforms.size(); i++) {
        const BoundUniform &uniform = p_uniforms[i];
        const ShaderInfo::UniformBindingInfo &binding =
            uniform_set.bindings[i];

        switch (uniform.type) {
            case UNIFORM_TYPE_SAMPLER: {
                for (uint32_t j = 0; j < uniform.ids.size(); j++) {
                    const D3D12_SAMPLER_DESC &sampler_desc =
                        samplers[uniform.ids[j].id];
                    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle =
                        get_cpu_handle(
                            uniform_set_info->sampler_descriptor_heap_alloc->cpu_handle,
                            binding.sampler_descriptor_offset + j,
                            sampler_descriptor_heap.increment_size);
                    device->CreateSampler(&sampler_desc, cpu_handle);
                }
            } break;

            case UNIFORM_TYPE_SAMPLER_WITH_TEXTURE: {
                for (uint32_t j = 0; j < uniform.ids.size() / 2; j++) {
                    // Create sampler
                    if (create_samplers) {
                        const D3D12_SAMPLER_DESC &sampler_desc =
                            samplers[uniform.ids[j].id];
                        device->CreateSampler(&sampler_desc,
                            get_cpu_handle(..., binding.sampler_descriptor_offset + (j/2), ...));
                    }

                    // Create SRV for texture
                    TextureInfo *texture_info =
                        (TextureInfo *)uniform.ids[j + 1].id;
                    device->CreateShaderResourceView(
                        texture_info->resource,
                        &texture_info->view_descs.srv,
                        get_cpu_handle(..., binding.resource_descriptor_offset + (j/2), ...));
                }
            } break;
        }
    }

    // Return opaque handle
    return UniformSetID(uniform_set_info);
}
```

**Key points**:
- Samplers and textures allocated to separate heaps
- **Sampler caching**: Multiple uniform sets can share sampler descriptors (optimization)
- Descriptors created via `device->CreateSampler()`, `device->CreateShaderResourceView()`
- Layout is **implicit** — no explicit state transitions

### 2.4 DX11: No Descriptor Sets (Immediate Context Problem)

DX11 **has no descriptor sets or heaps**. Bindings are made via immediate context:

```cpp
// DX11 does NOT support uniform_set_create() properly!
// Instead, bindings happen per-draw:
context->PSSetShaderResources(slot, 1, &srv);
context->PSSetSamplers(slot, 1, &sampler);
context->PSSetConstantBuffers(slot, 1, &cbuffer);
```

**This is why UzEngine's DX11 backend crashes**: It tries to use `SetFragmentTexture()` per-draw, but the render graph expects **immutable descriptor sets**.

---

## 3. Sampler Binding Differences

### 3.1 Vulkan: Combined Sampler + Texture Descriptor

Vulkan allows **combined image samplers** — one descriptor holds both texture and sampler:

```cpp
case UNIFORM_TYPE_SAMPLER_WITH_TEXTURE: {
    VkDescriptorImageInfo vk_img_infos[];
    for (uint32_t j = 0; j < num_descriptors; j++) {
        vk_img_infos[j] = {
            .sampler = (VkSampler)uniform.ids[j * 2 + 0].id,     // Sampler
            .imageView = ((TextureInfo *)uniform.ids[j * 2 + 1].id)->vk_view,  // Texture
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
    }
    vk_writes[writes_amount].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}
```

This is efficient because Vulkan descriptor sets bind samplers and textures together.

### 3.2 DX12: Separate Sampler and Texture Heaps

DX12 requires **separate heaps** for samplers and textures:

```cpp
// Two separate descriptor heaps:
// - Sampler heap (D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
// - Resource heap (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)

// Sampler binding happens in sampler heap
device->CreateSampler(&sampler_desc, sampler_cpu_handle);

// Texture binding happens in resource heap
device->CreateShaderResourceView(texture_resource, &srv_desc, resource_cpu_handle);

// At draw time, both heaps must be bound:
command_list->SetDescriptorHeaps(2, heaps);  // [resource_heap, sampler_heap]
```

**DX12 Optimization**: Samplers are cached by hash. Multiple uniform sets can reuse the same sampler descriptors if they use identical samplers.

```cpp
uint32_t sampler_key = hash_murmur3(all_sampler_ids);
if (sampler_descriptor_heap_allocations.find(sampler_key) != end()) {
    // Reuse existing allocation
    uniform_set_info->sampler_descriptor_heap_alloc =
        &sampler_descriptor_heap_allocations[sampler_key];
    ++sampler_descriptor_heap_alloc->use_count;
} else {
    // Create new allocation
    sampler_descriptor_heap.allocate(...);
}
```

### 3.3 DX11: Per-Frame Binding (No Descriptors)

DX11 has no samplers as separate objects in descriptors. Samplers are bound per-frame:

```cpp
// DX11 Command List
void SetFragmentTexture(uint32_t slot, RHITextureHandle texture) {
    // Problem: This is called per-draw, not at descriptor creation
    TextureInfo* tex_info = (TextureInfo*)texture.id;
    context->PSSetShaderResources(slot, 1, &tex_info->srv);
    context->PSSetSamplers(slot, 1, &sampler);  // ???
}
```

**Critical Issue**: Where does the sampler come from in DX11? The current design:
1. **Expected**: Sampler is embedded in the descriptor set (created once)
2. **Actual**: Sampler must be passed to `SetFragmentTexture()` OR inferred from shader state

DX11 backends in production (UE5, Godot) use **immutable samplers in shader state** or **deferred context trickery**.

---

## 4. Constant Buffer (Uniform Buffer) Differences

### 4.1 Vulkan: Descriptor Set Binding

```cpp
case UNIFORM_TYPE_UNIFORM_BUFFER: {
    const BufferInfo *buf_info = (const BufferInfo *)uniform.ids[0].id;
    VkDescriptorBufferInfo vk_buf_info = {
        .buffer = buf_info->vk_buffer,
        .offset = 0,  // Dynamic offset applied at bind time
        .range = buf_info->size
    };
    vk_writes[writes_amount].descriptorType =
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    vk_writes[writes_amount].pBufferInfo = &vk_buf_info;
}

case UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC: {
    // Same as above, but layout dynamic
    vk_writes[writes_amount].descriptorType =
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    // Dynamic offsets passed at command_bind_render_uniform_sets()
}
```

Constant buffers are stored **as descriptors in descriptor sets** at creation time.

### 4.2 DX12: Constant Buffer Views (CBV) in Resource Heap

```cpp
case UNIFORM_TYPE_UNIFORM_BUFFER:
case UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC: {
    BufferInfo *buf_info = (BufferInfo *)uniform.ids[0].id;

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {
        .BufferLocation = buf_info->gpu_virtual_address,
        .SizeInBytes = STEPIFY(buf_info->size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)
    };

    device->CreateConstantBufferView(
        &cbv_desc,
        get_cpu_handle(resource_descriptor_heap, binding.resource_descriptor_offset, ...));
}
```

CBVs are created as **descriptors in the resource heap**. The GPU virtual address is captured at descriptor set creation time.

### 4.3 DX11: Constant Buffer Slot Binding (Immediate Context)

```cpp
// DX11 has no descriptor sets for CBVs
// Instead, bind at draw time:
void SetRootCBV(uint32_t rootParamIndex, RHIBufferHandle buffer,
                uint64_t offsetBytes, const void* cpuData, uint32_t dataSize) {
    // Create a SCRATCH buffer with cpuData
    // Copy to GPU buffer
    context->VSSetConstantBuffers(rootParamIndex, 1, &buffer);
    context->PSSetConstantBuffers(rootParamIndex, 1, &buffer);
}
```

**Problem**: DX11 `SetRootCBV()` tries to use **scratch buffers** for dynamic constant data. This is not how render graphs work — render graphs expect:
1. Descriptor sets created once with immutable buffer references
2. Dynamic data updated via `buffer_update()` calls
3. Same descriptor set used across frames

---

## 5. Texture Binding (Bindless vs Register-Bound)

### 5.1 Vulkan: Descriptor Set Textures (Bindless via Indices)

```cpp
case UNIFORM_TYPE_TEXTURE: {
    for (uint32_t j = 0; j < uniform.ids.size(); j++) {
        vk_img_infos[j] = {
            .sampler = VK_NULL_HANDLE,  // No sampler
            .imageView = ((TextureInfo *)uniform.ids[j].id)->vk_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
    }
    vk_writes[writes_amount].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
}
```

Shader accesses via **descriptor set index**:
```glsl
layout(set = 1, binding = 0) uniform sampler2D textures[];
vec4 color = texture(textures[index], uv);
```

### 5.2 DX12: Resource Descriptor Heap (Bindless via Descriptors)

```cpp
case UNIFORM_TYPE_TEXTURE: {
    for (uint32_t j = 0; j < uniform.ids.size(); j++) {
        TextureInfo *texture_info = (TextureInfo *)uniform.ids[j].id;
        device->CreateShaderResourceView(
            texture_info->resource,
            &texture_info->view_descs.srv,
            get_cpu_handle(resource_descriptor_heap,
                binding.resource_descriptor_offset + j, ...));
    }
}
```

Shader accesses via **heap index**:
```hlsl
Texture2D textures[] : register(t0, space1);
float4 color = textures[index].Sample(sampler, uv);
```

### 5.3 DX11: Per-Slot Binding (Not Bindless)

DX11 has a **fixed number of texture slots** (16 on most hardware for PS):

```cpp
void SetFragmentTexture(uint32_t slot, RHITextureHandle texture) {
    TextureInfo* tex_info = (TextureInfo*)texture.id;
    context->PSSetShaderResources(slot, 1, &tex_info->srv);

    // Shader hardcodes:
    // Texture2D textures[16] : register(t0);
    // textures[slot] in shader
}
```

This is **not bindless**. If you have more than 16 textures, you must:
1. Shuffle them at draw time (expensive)
2. Use atlasing (manual)
3. Use another render pass (pipeline stalls)

**This is UzEngine's critical problem**: Render graph assumes bindless texture access via descriptor indices, but DX11 requires per-slot binding with fixed slot count.

---

## 6. Resource Barriers and State Transitions

### 6.1 Vulkan: Explicit Barriers (Full Model)

```cpp
void RenderingDeviceDriverVulkan::command_pipeline_barrier(
    CommandBufferID p_cmd_buffer,
    BitField<PipelineStageBits> p_src_stages,
    BitField<PipelineStageBits> p_dst_stages,
    VectorView<MemoryAccessBarrier> p_memory_barriers,
    VectorView<BufferBarrier> p_buffer_barriers,
    VectorView<TextureBarrier> p_texture_barriers,
    VectorView<AccelerationStructureBarrier> p_acceleration_structure_barriers)
{
    // Convert RDD barriers to Vulkan barriers
    VkImageMemoryBarrier vk_image_barriers[] = {
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = _rd_to_vk_access_flags(src_access),
            .dstAccessMask = _rd_to_vk_access_flags(dst_access),
            .oldLayout = RD_TO_VK_LAYOUT[prev_layout],     // State before
            .newLayout = RD_TO_VK_LAYOUT[next_layout],     // State after
            .image = tex_info->vk_view_create_info.image,
            .subresourceRange = {
                .aspectMask = (VkImageAspectFlags)subresources.aspect,
                .baseMipLevel = subresources.base_mipmap,
                .levelCount = subresources.mipmap_count,
                .baseArrayLayer = subresources.base_layer,
                .layerCount = subresources.layer_count
            }
        }
    };

    // Execute barrier
    vkCmdPipelineBarrier(cmd_buffer,
        src_stage_flags,
        dst_stage_flags,
        0,  // dependencyFlags
        p_memory_barriers.size(), vk_memory_barriers,
        p_buffer_barriers.size(), vk_buffer_barriers,
        p_texture_barriers.size(), vk_image_barriers);
}
```

**Key concepts**:
- **Pipeline stages**: Specifies when barrier executes (TOP_OF_PIPE, FRAGMENT_SHADER, COLOR_ATTACHMENT_OUTPUT, etc.)
- **Access modes**: Specifies what operations (SHADER_READ, COLOR_ATTACHMENT_WRITE, etc.)
- **Image layouts**: Specifies texture state (SHADER_READ_ONLY, COLOR_ATTACHMENT, TRANSFER_DST, etc.)

### 6.2 DX12: Enhanced Barriers (Sync2 Style, if supported)

```cpp
void RenderingDeviceDriverD3D12::command_pipeline_barrier(
    CommandBufferID p_cmd_buffer,
    BitField<PipelineStageBits> p_src_stages,
    BitField<PipelineStageBits> p_dst_stages,
    VectorView<RDD::MemoryAccessBarrier> p_memory_barriers,
    VectorView<RDD::BufferBarrier> p_buffer_barriers,
    VectorView<RDD::TextureBarrier> p_texture_barriers,
    VectorView<AccelerationStructureBarrier> p_acceleration_structure_barriers)
{
    if (!barrier_capabilities.enhanced_barriers_supported) {
        // Fallback: No explicit barriers needed
        return;
    }

    D3D12_TEXTURE_BARRIER texture_barriers[] = {
        {
            .SyncBefore = _rd_to_d3d12_sync(src_stages, src_access),
            .SyncAfter = _rd_to_d3d12_sync(dst_stages, dst_access),
            .AccessBefore = _rd_to_d3d12_access(src_access),
            .AccessAfter = _rd_to_d3d12_access(dst_access),
            .LayoutBefore = _rd_texture_layout_to_d3d12(prev_layout),
            .LayoutAfter = _rd_texture_layout_to_d3d12(next_layout),
            .pResource = texture_info->resource,
            .Subresources = {
                .IndexOrFirstMipLevel = subresources.base_mipmap,
                .NumMipLevels = subresources.mipmap_count,
                .FirstArraySlice = subresources.base_layer,
                .NumArraySlices = subresources.layer_count
            }
        }
    };

    D3D12_BARRIER_GROUP barrier_groups[] = {
        {
            .Type = D3D12_BARRIER_TYPE_TEXTURE,
            .NumBarriers = 1,
            .pTextureBarriers = texture_barriers
        }
    };

    cmd_list_7->Barrier(1, barrier_groups);
}
```

**Key differences from Vulkan**:
- DX12 has no explicit "stages" — uses `SyncBefore`/`SyncAfter` (bit masks)
- Layouts are explicit: COLOR, DEPTH_WRITE, SHADER_RESOURCE, etc.
- `SIMULTANEOUS_ACCESS` flag allows skipping barriers entirely

### 6.3 DX11: Implicit State Management (No Barriers)

```cpp
void DX11CommandList::ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count)
{
    // DX11 manages state automatically — this is a NO-OP
    // The driver inserts hazard barriers when needed
}
```

**Why DX11 doesn't need explicit barriers**:
1. The driver tracks resource state implicitly
2. Driver inserts synchronization automatically
3. No layout transitions — D3D11 has no texture layouts (all are implicitly compatible)

**The cost**:
- Driver overhead for implicit tracking
- Potential GPU stalls if driver can't predict usage
- No control over synchronization granularity

---

## 7. Framebuffer and Render Target Setup Differences

### 7.1 Vulkan: Render Pass + Framebuffer Model

```cpp
// 1. Create render pass (describes attachments, load/store ops, layouts)
struct Attachment {
    DataFormat format;
    AttachmentLoadOp load_op;    // LOAD, CLEAR, or DONT_CARE
    AttachmentStoreOp store_op;  // STORE or DONT_CARE
    TextureLayout initial_layout;
    TextureLayout final_layout;
};

RenderPassID render_pass = render_pass_create({
    {color_format, LOAD_OP_CLEAR, STORE_OP_STORE, ...},
    {depth_format, LOAD_OP_CLEAR, STORE_OP_STORE, ...}
});

// 2. Create framebuffer (binds textures to attachment slots)
FramebufferID framebuffer = framebuffer_create(render_pass, {
    color_texture,
    depth_texture
});

// 3. Begin render pass (specifies clear values, layout transitions)
command_begin_render_pass(cmd_buffer, render_pass, framebuffer, {
    {color_clear_value},
    {depth_clear_value}
});
```

**Vulkan semantics**:
- Render pass defines **optimization barriers** between subpasses
- Framebuffer pins textures to attachment slots
- `initial_layout` / `final_layout` provide layout transitions for free

### 7.2 DX12: Render Target View (RTV) + Depth Stencil View (DSV)

```cpp
// 1. Create views (once)
D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {...};
device->CreateRenderTargetView(color_resource, &rtv_desc, rtv_cpu_handle);

D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {...};
device->CreateDepthStencilView(depth_resource, &dsv_desc, dsv_cpu_handle);

// 2. Bind at draw time
D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles[] = {rtv_handle};
cmd_list->OMSetRenderTargets(1, rtv_handles, FALSE, &dsv_handle);

// 3. Clear (optional)
cmd_list->ClearRenderTargetView(rtv_handle, clear_color, ...);
cmd_list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH, depth, ...);
```

**DX12 semantics**:
- No render pass object — layouts are implicit
- RTV/DSV views created to resource descriptors
- Clear and render target binding are separate commands

### 7.3 DX11: Render Target View (RTV) + Depth Stencil View (DSV)

```cpp
// 1. Create views (once)
ComPtr<ID3D11RenderTargetView> rtv;
device->CreateRenderTargetView(color_resource, nullptr, &rtv);

ComPtr<ID3D11DepthStencilView> dsv;
device->CreateDepthStencilView(depth_resource, nullptr, &dsv);

// 2. Bind at draw time (immediate context)
ID3D11RenderTargetView* rtvs[] = {rtv.Get()};
context->OMSetRenderTargets(1, rtvs, dsv.Get());

// 3. Clear (optional)
context->ClearRenderTargetView(rtv.Get(), clear_color);
context->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, depth, stencil);
```

**DX11 semantics** (very similar to DX12):
- No render pass object
- RTV/DSV views created to resource descriptors
- Immediate context commands execute synchronously

---

## 8. Root Signature / Root Parameter Differences

### 8.1 Vulkan: Descriptor Set Layouts + Push Constants

```glsl
// Shader defines layout explicitly
layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 viewProj;
} camera;

layout(set = 1, binding = 0) uniform sampler2D textures[];
layout(set = 1, binding = 1) uniform sampler samplers[];

layout(push_constant) uniform PushConstants {
    vec4 data;
} push;
```

Godot's RHI doesn't expose push constants at the uniform set level. Push constants are separate.

### 8.2 DX12: Root Signature (Immutable)

```cpp
struct RootParameter {
    enum Type { DESCRIPTOR_TABLE, ROOT_CBV, ROOT_SRV, ROOT_UAV, ROOT_CONSTANTS };

    Type type;
    union {
        D3D12_ROOT_DESCRIPTOR_TABLE descriptor_table;
        D3D12_ROOT_DESCRIPTOR root_descriptor;
        D3D12_ROOT_CONSTANTS root_constants;
    };
};

// Example root signature:
// [0] Descriptor table (CBV): camera buffer
// [1] Descriptor table (CBV/SRV/UAV): resources (textures, buffers)
// [2] Descriptor table (SAMPLER): samplers
// [3] Root constants: push constants
```

At draw time:
```cpp
cmd_list->SetGraphicsRootDescriptorTable(0, camera_descriptor_gpu_handle);
cmd_list->SetGraphicsRootDescriptorTable(1, resource_descriptor_gpu_handle);
cmd_list->SetGraphicsRootDescriptorTable(2, sampler_descriptor_gpu_handle);
cmd_list->SetGraphicsRoot32BitConstants(3, constant_count, constant_data, 0);
```

**Key differences**:
- **Vulkan**: Descriptor set layouts are compiled into shader, immutable
- **DX12**: Root signature is separate object, must match shader layout

### 8.3 DX11: Register Slots (No Root Signature)

DX11 has no root signature. Instead, register slots are hardcoded in HLSL and bound per-frame:

```hlsl
cbuffer Camera : register(b0) {
    float4x4 viewProj;
};

Texture2D textures[16] : register(t0);
SamplerState samplers[16] : register(s0);
```

At draw time:
```cpp
context->VSSetConstantBuffers(0, 1, &camera_buffer);
context->PSSetShaderResources(0, 16, texture_srv_array);
context->PSSetSamplers(0, 16, sampler_array);
```

**Problem for UzEngine render graphs**:
- Texture slots are fixed (16 textures max per stage)
- Sampler slots are fixed (typically 16 per stage)
- No descriptor heaps or tables to batch bindings
- Each binding is a separate API call

---

## 9. UzEngine DX11 Backend Issues (Root Causes)

### 9.1 Problem 1: Scratch Constant Buffers

**Issue**: `SetRootCBV()` tries to create scratch buffers for dynamic data:

```cpp
void SetRootCBV(uint32_t rootParamIndex, RHIBufferHandle buffer,
                uint64_t offsetBytes, const void* cpuData, uint32_t dataSize) {
    // Creates a temporary D3D11 buffer, copies cpuData
    // Binds to context
    // Crashes because render graph expects immutable descriptor set
}
```

**Why it crashes**:
- Render graph creates descriptor sets once at pass creation
- Dynamic data is updated via `buffer_update()` calls
- Scratch buffers break this model — each call creates new state

**Godot/UE5 solution**:
- Use persistent mapped buffers for dynamic constants
- Update CPU-side buffer copy
- Flush to GPU via `D3D11_MAP_WRITE_NO_OVERWRITE`
- Never create scratch buffers

### 9.2 Problem 2: SetFragmentTexture() Per-Draw

**Issue**: `SetFragmentTexture()` is called per-draw, but render graph expects immutable descriptor sets:

```cpp
void SetFragmentTexture(uint32_t slot, RHITextureHandle texture) {
    TextureInfo* tex_info = (TextureInfo*)texture.id;
    context->PSSetShaderResources(slot, 1, &tex_info->srv);
}
```

**Why it crashes**:
- Render graph records texture bindings at descriptor set creation time
- Per-frame `SetFragmentTexture()` calls are not made by render graph
- Sampler is missing (where does it come from?)

**Godot/UE5 solution**:
- Pre-bind all textures at descriptor set creation
- Use texture arrays with dynamic indexing (if DX11 supports it)
- Or: Fetch sampler from shader constant or pass via root constant

### 9.3 Problem 3: No Descriptor Caching

**Issue**: DX11 implementation creates new SRVs per draw instead of reusing:

```cpp
// Bad: Creates new view every time
context->PSSetShaderResources(slot, 1, &new_srv);
```

**Good**: Cache SRVs at descriptor set creation:

```cpp
// Create once per descriptor set
TextureInfo* tex_info = (TextureInfo*)texture.id;
if (!tex_info->srv_cached) {
    device->CreateShaderResourceView(tex_info->resource, nullptr, &tex_info->srv);
    tex_info->srv_cached = true;
}
context->PSSetShaderResources(slot, 1, &tex_info->srv);
```

### 9.4 Problem 4: Resource Barriers as No-Ops

**Issue**: `ResourceBarrier()` is a no-op, but render graph assumes barriers work:

```cpp
void ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) override {
    // Do nothing — relies on driver to infer barriers
}
```

**Problem**: Driver can't infer all hazards. Render graph may:
- Write to texture
- Read from texture (same pass, implicit hazard)
- Crash or produce garbage

**Solution**:
- Track resource state manually in DX11CommandList
- Emit `D3D11_RESOURCE_MISC_GENERATE_MIPS` or similar when needed
- For now: Trust driver, but add UAV/SRV transition tracking

---

## 10. Architecture Lessons for UzEngine

### 10.1 Lesson 1: Descriptor Sets as Immutable Snapshots

**Principle**: Resource bindings are created ONCE and reused across frames.

```cpp
// NOT like this (per-draw):
void Draw(...) {
    SetFragmentTexture(0, tex);
    SetSampler(0, sampler);
    DrawIndexed(...);
}

// Like this (per-pass):
DescriptorSet descriptors = CreateDescriptorSet({
    {binding: 0, texture: tex, sampler: sampler}
});

void Draw(...) {
    BindDescriptorSet(0, descriptors);
    DrawIndexed(...);
}
```

**For DX11**: Map to `ID3D11ShaderResourceView[]` array created at descriptor set time, bound once before draw.

### 10.2 Lesson 2: Samplers Are Part of Descriptor Sets

**Principle**: Samplers are not separate runtime state. They're baked into descriptor sets.

**Godot pattern**:
- `UNIFORM_TYPE_SAMPLER_WITH_TEXTURE` combines sampler + texture
- Created together in `uniform_set_create()`
- Never separate sampler binding per-draw

**For UzEngine DX11**:
```cpp
// At descriptor set creation:
struct SampledTextureBinding {
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11SamplerState> sampler;  // Baked into descriptor set
};

// At draw time:
context->PSSetShaderResources(slot, 1, &binding.srv);
context->PSSetSamplers(slot, 1, &binding.sampler);
```

### 10.3 Lesson 3: Constant Buffers via Persistent Mapping

**Principle**: Dynamic constants use persistent mapped buffers, not scratch allocations.

**Pattern**:
1. Create `BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT` buffer at descriptor set creation
2. CPU maps buffer once (persistent mapping)
3. Game code writes to CPU-side pointer
4. GPU reads from GPU buffer (GPU-visible after `UnmapBuffer()`)

**For DX11**:
```cpp
// At descriptor set creation:
D3D11_BUFFER_DESC desc = {
    .ByteWidth = size,
    .Usage = D3D11_USAGE_DYNAMIC,
    .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
    .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE
};
device->CreateBuffer(&desc, nullptr, &cbuffer);

// At game code (frame update):
void* mapped = MapBuffer(cbuffer_handle);
memcpy(mapped, &camera_data, sizeof(camera_data));
UnmapBuffer(cbuffer_handle);

// At draw time (via descriptor set):
context->VSSetConstantBuffers(slot, 1, &cbuffer);
```

### 10.4 Lesson 4: Manual State Tracking for DX11 Barriers

**Principle**: DX11 doesn't require explicit barriers, but render graph does. Track state manually.

```cpp
struct TextureStateTracker {
    std::map<TextureInfo*, D3D11_RESOURCE_DIMENSION> current_state;

    void ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) {
        for (uint32_t i = 0; i < count; i++) {
            TextureInfo* tex = GetTextureInfo(barriers[i].texture);

            // Infer transition (SRV -> UAV, etc.)
            if (barriers[i].src_access & RHI_ACCESS_SHADER_READ) {
                // Was SRV
            }
            if (barriers[i].dst_access & RHI_ACCESS_SHADER_WRITE) {
                // Becomes UAV
            }

            current_state[tex] = new_state;
        }
    }
};
```

### 10.5 Lesson 5: Vulkan Is the "Reference" Model

**Principle**: Implement Vulkan first (or reference Godot's Vulkan backend), then adapt.

- Vulkan matches Godot's abstraction closely
- D3D12 requires some emulation (descriptor heaps for descriptor sets)
- DX11 requires the most emulation (everything is per-frame state)

For UzEngine:
1. Ensure RHI abstraction is Vulkan-compatible
2. Add DX12 optimization layer (descriptor heap caching)
3. Add DX11 compat layer (immutable SRV/sampler arrays at descriptor set creation)

---

## 11. Implementation Checklist for UzEngine DX11 Fix

### Step 1: Refactor Descriptor Sets (NEW)

```cpp
// In DX11CommandList or DX11Device:
struct DX11DescriptorSet {
    std::vector<ComPtr<ID3D11ShaderResourceView>> textures;
    std::vector<ComPtr<ID3D11SamplerState>> samplers;
    std::map<uint32_t, ComPtr<ID3D11Buffer>> cbuffers;
    // Recreate this at descriptor set creation time, NOT per-draw
};
```

### Step 2: Remove Per-Frame Binding Methods

```cpp
// DELETE OR MARK DEPRECATED:
void SetFragmentTexture(uint32_t slot, RHITextureHandle texture);
void SetSampler(uint32_t slot, RHISamplerHandle sampler);

// KEEP (maps to descriptor set):
void SetDescriptorTable(uint32_t rootParamIndex, uint32_t descriptorIndex);
void BindDescriptorSet(uint32_t set, const DX11DescriptorSet& descriptors);
```

### Step 3: Persistent Mapped Buffers for Constants

```cpp
// Refactor SetRootCBV to use persistent buffers:
void SetRootCBV(uint32_t rootParamIndex, RHIBufferHandle buffer,
                uint64_t offsetBytes, const void* cpuData, uint32_t dataSize) {
    // Map persistent buffer
    BufferInfo* buf = GetBufferInfo(buffer);
    uint8_t* mapped = (uint8_t*)buf->persistent_map + offsetBytes;
    memcpy(mapped, cpuData, dataSize);
    // No UnmapBuffer — buffer is persistent
}
```

### Step 4: Manual State Tracking for Barriers

```cpp
struct DX11CommandListState {
    std::map<TextureInfo*, RHIResourceState> texture_states;

    void ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) {
        for (uint32_t i = 0; i < count; i++) {
            TextureInfo* tex = GetTextureInfo(barriers[i].texture);
            tex_states[tex] = barriers[i].dst_state;
        }
    }
};
```

### Step 5: Test Against Render Graph

- Create descriptor set (immutable)
- Use same descriptor set across 100+ draws
- Verify no crashes, correct textures bound

---

## 12. References

**Godot Source Code**:
- `/e/godot/servers/rendering/rendering_device_driver.h` — Abstract interface (133 KB, 800+ lines)
- `/e/godot/drivers/vulkan/rendering_device_driver_vulkan.cpp` — Vulkan impl (5000+ lines)
- `/e/godot/drivers/d3d12/rendering_device_driver_d3d12.cpp` — DX12 impl (4000+ lines)

**UzEngine Files**:
- `/e/UzEngine/runtime/engine/rhi/dx11/dx11_device.h` — DX11 device
- `/e/UzEngine/runtime/engine/rhi/dx11/dx11_command_list.h` — DX11 command list
- `/e/UzEngine/docs/architecture/rhi-abstraction.md` — UzEngine RHI spec (to be updated)

**Key Takeaway**:
> Godot's **descriptor set pattern** works because descriptors (samplers + textures + buffers) are created **once per material/pass**, not per-frame. DX11's immediate context model can be adapted by creating immutable `SRV[]` and `SamplerState[]` arrays at descriptor set creation time, then binding them as fixed arrays at draw time. The current per-draw binding approach is incompatible with deferred render graphs.
