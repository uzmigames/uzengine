# Godot 4 RenderingDevice Architecture Analysis

**Goal**: Extract Godot's patterns for cross-backend descriptor binding, specifically D3D12 root signature generation and fullscreen pass texture binding.

**Key Files**:
- `/e/godot/drivers/d3d12/rendering_device_driver_d3d12.h` (6000+ lines)
- `/e/godot/drivers/d3d12/rendering_shader_container_d3d12.cpp` (1000+ lines, root signature generation)
- `/e/godot/servers/rendering/renderer_rd/effects/tone_mapper.cpp` (fullscreen pass example)
- `/e/godot/servers/rendering/rendering_device.h` (abstract RenderingDevice interface)

---

## 1. Root Signature Strategy: Per-Pipeline, Not Universal

### Godot's Approach

Godot creates **one root signature per shader**, not a universal root signature. This is a crucial design choice:

**File**: `/e/godot/drivers/d3d12/rendering_shader_container_d3d12.cpp:557-811`

```cpp
bool RenderingShaderContainerD3D12::_generate_root_signature(
    BitField<RenderingDeviceCommons::ShaderStage> p_stages_processed)
{
    LocalVector<D3D12_ROOT_PARAMETER1> root_params;

    // 1. Push constants (if needed by shader)
    if (reflection_data_d3d12.dxil_push_constant_stages) {
        CD3DX12_ROOT_PARAMETER1 push_constant;
        push_constant.InitAsConstants(
            reflection_data.push_constant_size / sizeof(uint32_t),
            ROOT_CONSTANT_REGISTER, 0,
            stages_to_d3d12_visibility(reflection_data_d3d12.dxil_push_constant_stages));
        root_params.push_back(push_constant);
    }

    // 2. NIR-DXIL runtime data (vertex shader only, one-time per compilation)
    if (reflection_data_d3d12.nir_runtime_data_root_param_idx == 1) {
        reflection_data_d3d12.nir_runtime_data_root_param_idx = root_params.size();
        CD3DX12_ROOT_PARAMETER1 nir_runtime_data;
        nir_runtime_data.InitAsConstants(
            sizeof(dxil_spirv_vertex_runtime_data) / sizeof(uint32_t),
            RUNTIME_DATA_REGISTER, 0,
            D3D12_SHADER_VISIBILITY_VERTEX);
        root_params.push_back(nir_runtime_data);
    }

    // 3. Descriptor tables (one or two per uniform set)
    // Resource table (CBV/SRV/UAV) + optional Sampler table
    for (const TraceableDescriptorTable &table : resource_tables_maps) {
        CD3DX12_ROOT_PARAMETER1 root_table = {};
        root_table.InitAsDescriptorTable(
            table.ranges.size(), table.ranges.ptr(),
            stages_to_d3d12_visibility(table.stages_mask));
        reflection_binding_set_data_d3d12.ptrw()[table.set].resource_root_param_idx
            = root_params.size();
        root_params.push_back(root_table);
    }

    // 4. Sampler tables (separate from resources)
    for (const TraceableDescriptorTable &table : sampler_tables_maps) {
        CD3DX12_ROOT_PARAMETER1 root_table = {};
        root_table.InitAsDescriptorTable(
            table.ranges.size(), table.ranges.ptr(),
            stages_to_d3d12_visibility(table.stages_mask));
        reflection_binding_set_data_d3d12.ptrw()[table.set].sampler_root_param_idx
            = root_params.size();
        root_params.push_back(root_table);
    }

    // 5. Serialize to D3D12 binary format
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_sig_desc = {};
    D3D12_ROOT_SIGNATURE_FLAGS root_sig_flags =
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    // Deny unused shader stages to optimize performance
    if (!p_stages_processed.has_flag(RenderingDeviceCommons::SHADER_STAGE_VERTEX_BIT)) {
        root_sig_flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;
    }
    if (!p_stages_processed.has_flag(RenderingDeviceCommons::SHADER_STAGE_FRAGMENT_BIT)) {
        root_sig_flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
    }
    if (reflection_data.vertex_input_mask) {
        root_sig_flags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    }

    root_sig_desc.Init_1_1(root_params.size(), root_params.ptr(),
                           0, nullptr, root_sig_flags);

    HRESULT res = D3DX12SerializeVersionedRootSignature(
        HMODULE(lib_d3d12), &root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1_1,
        &root_sig_blob, &error_blob);

    // Store bytes + CRC32 for caching and reuse
    root_signature_bytes.resize(root_sig_blob->GetBufferSize());
    memcpy(root_signature_bytes.ptrw(), root_sig_blob->GetBufferPointer(),
           root_sig_blob->GetBufferSize());

    root_signature_crc = crc32(0, nullptr, 0);
    root_signature_crc = crc32(root_signature_crc,
        (const Bytef *)root_sig_blob->GetBufferPointer(),
        root_sig_blob->GetBufferSize());

    return true;
}
```

**Key Decision**: **One root signature per shader** means each shader's root signature is tightly optimized to its own needs. This is more memory-efficient than UE5's universal root signature approach.

### Root Signature Layout Strategy

Godot's layout is **dynamic per-shader**, but follows a consistent pattern:

1. **Push constants** (if used by shader) — root DWORD constants
2. **NIR runtime data** (vertex only) — root DWORD constants
3. **Resource descriptor tables** — one per uniform set (CBV/SRV/UAV mixed)
4. **Sampler descriptor tables** — one per uniform set (separate from resources)

**Register naming scheme** (from reflection):
```cpp
uint32_t dxil_register =
    set * GODOT_NIR_DESCRIPTOR_SET_MULTIPLIER +
    binding * GODOT_NIR_BINDING_MULTIPLIER;
```

Each uniform set gets a unique register space (space 0, implicit), avoiding conflicts.

---

## 2. D3D12 Descriptor Binding Strategy

### ShaderInfo Structure (Per-Shader Metadata)

**File**: `/e/godot/drivers/d3d12/rendering_device_driver_d3d12.h:622-663`

```cpp
struct ShaderInfo {
    uint32_t dxil_push_constant_size = 0;
    uint32_t nir_runtime_data_root_param_idx = UINT32_MAX;
    PipelineType pipeline_type = PIPELINE_TYPE_RASTERIZATION;

    struct UniformBindingInfo {
        uint32_t stages = 0;  // Actual shader stages using this uniform
        ResourceClass res_class = RES_CLASS_INVALID;  // CBV/SRV/UAV
        UniformType type = UNIFORM_TYPE_MAX;
        uint32_t length = UINT32_MAX;
        bool writable = false;
        uint32_t resource_descriptor_offset = UINT32_MAX;  // Offset within heap
        uint32_t sampler_descriptor_offset = UINT32_MAX;
        uint32_t root_param_idx = UINT32_MAX;  // For dynamic buffers only
    };

    struct UniformSet {
        TightLocalVector<UniformBindingInfo> bindings;
        uint32_t resource_root_param_idx = UINT32_MAX;  // Root param index in root sig
        uint32_t resource_descriptor_count = 0;
        uint32_t sampler_root_param_idx = UINT32_MAX;
        uint32_t sampler_descriptor_count = 0;
    };

    TightLocalVector<UniformSet> sets;

    // ... specialization constants, bytecode, root signature ...

    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
    Microsoft::WRL::ComPtr<ID3D12RootSignatureDeserializer> root_signature_deserializer;
    const D3D12_ROOT_SIGNATURE_DESC *root_signature_desc = nullptr;
    uint32_t root_signature_crc = 0;
};
```

**Strategy**:
- Root signatures are **serialized to bytes** and **CRC'd** for fast cache lookups
- Each shader's root signature is **deserialized once** at creation time
- The `root_signature_desc` pointer is kept in memory for introspection

### Binding During Draw Calls

**File**: `/e/godot/drivers/d3d12/rendering_device_driver_d3d12.cpp:4841-4908`

```cpp
// When binding uniform sets for a render pass:
void command_bind_render_uniform_sets(CommandBufferID p_cmd_buffer,
    VectorView<UniformSetID> p_uniform_sets, ShaderID p_shader,
    uint32_t p_first_set_index, uint32_t p_set_count, uint32_t p_dynamic_offsets) {

    CommandBufferInfo *cmd_buf_info = ...;
    const ShaderInfo *shader_info = ...;

    // 1. Set root signature (only if changed)
    if (cmd_buf_info->graphics_root_signature_crc != shader_info->root_signature_crc) {
        cmd_buf_info->cmd_list->SetGraphicsRootSignature(
            shader_info->root_signature.Get());
        cmd_buf_info->graphics_root_signature_crc = shader_info->root_signature_crc;
    }

    // 2. For each uniform set:
    for (uint32_t i = 0; i < p_set_count; i++) {
        const UniformSetInfo *uniform_set_info = ...;
        const ShaderInfo::UniformSet &uniform_set_shader_info =
            shader_info->sets[p_first_set_index + i];

        // 3a. Bind individual root descriptors (for dynamic buffers)
        for (const auto &binding : uniform_set_shader_info.bindings) {
            if (binding.root_param_idx != UINT_MAX) {
                // Dynamic buffers use root descriptors (direct GPU VA)
                switch (binding.type) {
                    case UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC:
                        cmd_buf_info->cmd_list->SetGraphicsRootConstantBufferView(
                            binding.root_param_idx, buffer_location);
                        break;
                    case UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC:
                        cmd_buf_info->cmd_list->SetGraphicsRootShaderResourceView(
                            binding.root_param_idx, buffer_location);
                        break;
                }
            }
        }

        // 3b. Bind descriptor tables (for static resources/samplers)
        if (uniform_set_shader_info.resource_root_param_idx != UINT_MAX) {
            cmd_buf_info->cmd_list->SetGraphicsRootDescriptorTable(
                uniform_set_shader_info.resource_root_param_idx,
                uniform_set_info->resource_descriptor_heap_alloc.gpu_handle);
        }

        if (uniform_set_shader_info.sampler_root_param_idx != UINT_MAX) {
            cmd_buf_info->cmd_list->SetGraphicsRootDescriptorTable(
                uniform_set_shader_info.sampler_root_param_idx,
                uniform_set_info->sampler_descriptor_heap_alloc->gpu_handle);
        }
    }
}
```

**Key Pattern**:
- **Root signature is cached by CRC** — only set when shader changes
- **Dynamic buffers use root descriptors** (GPU virtual address directly) — no heap overhead
- **Static resources use descriptor tables** — heap-allocated descriptors grouped by set
- **Resources and samplers are separated** — two descriptor tables per set (if both needed)

---

## 3. Fullscreen Passes: Tone Mapper Example

### How Fullscreen Passes Bind Input Textures

**File**: `/e/godot/servers/rendering/renderer_rd/effects/tone_mapper.cpp:116-214`

```cpp
void ToneMapper::tonemapper(RID p_source_color, RID p_dst_framebuffer,
    const TonemapSettings &p_settings) {

    UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();

    // 1. Define what textures to bind (abstracted from shader)
    RD::Uniform u_source_color;
    u_source_color.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE;
    u_source_color.binding = 0;  // Binding index in shader
    u_source_color.ids.push_back(p_source_color);

    RD::Uniform u_exposure_texture;
    u_exposure_texture.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE;
    u_exposure_texture.binding = 0;
    u_exposure_texture.ids.push_back(p_settings.exposure_texture);

    // ... more uniforms ...

    // 2. Get or create pipeline
    RD::get_singleton()->draw_list_bind_render_pipeline(draw_list,
        tonemap.pipelines[mode].get_render_pipeline(
            RD::INVALID_ID,
            RD::get_singleton()->framebuffer_get_format(p_dst_framebuffer),
            false,
            RD::get_singleton()->draw_list_get_current_pass()));

    // 3. Bind descriptor sets (shader-agnostic)
    RD::get_singleton()->draw_list_bind_uniform_set(draw_list,
        uniform_set_cache->get_cache(shader, 0, u_source_color), 0);
    RD::get_singleton()->draw_list_bind_uniform_set(draw_list,
        uniform_set_cache->get_cache(shader, 1, u_exposure_texture), 1);
    RD::get_singleton()->draw_list_bind_uniform_set(draw_list,
        uniform_set_cache->get_cache(shader, 2, u_glow_texture, u_glow_map), 2);
    RD::get_singleton()->draw_list_bind_uniform_set(draw_list,
        uniform_set_cache->get_cache(shader, 3, u_color_correction_texture), 3);

    // 4. Set push constants (for tone mapping parameters)
    RD::get_singleton()->draw_list_bind_push_constants(draw_list,
        shader, 0, push_constant_data);

    // 5. Draw full-screen quad (2 triangles, no vertex buffer)
    RD::get_singleton()->draw_list_draw(draw_list, false, 1, 0);
}
```

**Key Pattern**: **Descriptor sets are ephemeral and unified**
- Tone mapper doesn't care about root signatures or D3D12 details
- It builds `RD::Uniform` objects (abstract descriptors)
- `UniformSetCacheRD` auto-generates the right descriptor set for the shader + bindings
- **Same code path works on Vulkan, Metal, and D3D12**

### UniformSetCacheRD (Backend-Agnostic)

The `uniform_set_cache->get_cache(shader, set_index, uniforms...)` function:
1. **Hashes** the shader + uniforms
2. **Returns cached descriptor set** if hit
3. **Creates new descriptor set** if miss

This is Godot's key abstraction: **descriptor sets are ephemeral and auto-created on demand**.

---

## 4. Descriptor Heap Management

### Multi-Heap Strategy

**File**: `/e/godot/drivers/d3d12/rendering_device_driver_d3d12.h:154-172`

```cpp
struct DescriptorHeap {
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = {};
    uint32_t increment_size = 0;

    Microsoft::WRL::ComPtr<D3D12MA::VirtualBlock> virtual_block;  // Virtual memory allocator

    Error initialize(ID3D12Device *p_device, D3D12_DESCRIPTOR_HEAP_TYPE p_type,
        uint32_t p_num_descriptors, bool p_shader_visible);
    Error allocate(uint32_t p_descriptor_count, Allocation &r_allocation);
    void free(const Allocation &p_allocation);
};

// Two GPU-visible heaps (per-device)
DescriptorHeap resource_descriptor_heap;  // CBV/SRV/UAV
DescriptorHeap sampler_descriptor_heap;   // Samplers

// CPU-visible heap pools (for RTV/DSV which aren't GPU-visible)
CPUDescriptorHeapPool rtv_descriptor_heap_pool;
CPUDescriptorHeapPool dsv_descriptor_heap_pool;
```

**Strategy**:
- **Two GPU-visible heaps** (resources + samplers) — Godot binds entire heaps once
- **Per-frame descriptor allocations** — frame info tracks which descriptors are in flight
- **Virtual allocation** (`D3D12MA::VirtualBlock`) — efficient sub-allocation within heap

### No Bindless Implementation

**Important**: Godot does **not** use SM6.6 bindless (descriptor indexing). All resources are:
1. Pre-allocated in descriptor heaps
2. Copied to uniform sets
3. Bound via root signature descriptor tables

**Consequence for UzEngine**: SM6.6 descriptor indexing would require significant changes to Godot's binding model.

---

## 5. Specialization Constants in D3D12

Godot handles specialization constants via **bytecode patching** (not shader variants):

**File**: `/e/godot/drivers/d3d12/rendering_shader_container_d3d12.h:105-107`

```cpp
struct ReflectionSpecializationDataD3D12 {
    uint64_t stages_bit_offsets[D3D12_BITCODE_OFFSETS_NUM_STAGES] = {};
};
```

For each specialization constant:
1. **Compile shader** with placeholder value
2. **Record byte offsets** in DXIL bytecode
3. **At pipeline creation**, patch bytecode with actual values
4. **Recompile bytecode** with new values

This avoids creating multiple root signatures for different specialization states.

---

## 6. Cross-Backend Abstraction Layers

### RenderingDeviceDriver (Abstract Interface)

**File**: `/e/godot/servers/rendering/rendering_device_driver.h`

All D3D12-specific code is **encapsulated within the driver**:
- Root signatures are never exposed to the caller
- Descriptor sets are opaque `RDD::UniformSetID` handles
- Push constants are set via `command_bind_push_constants()` (abstracted)

### No Direct D3D12 Types Above RHI Boundary

Godot's strength: **No DX12Renderer class in the rendering server**. All rendering is abstracted via `RenderingDevice`.

UzEngine's current violation:
- `src/renderer/renderer.h` has `DX12Renderer` with raw ComPtrs
- `src/engine/renderer/rhi/rhi.h` exists but is not used

---

## 7. Pipeline State Object (PSO) Caching

### PipelineCacheRD

**File**: `/e/godot/servers/rendering/renderer_rd/pipeline_cache_rd.h`

```cpp
class PipelineCacheRD {
    RID get_render_pipeline(RID p_framebuffer_format, ...);
    void clear();
};
```

**Strategy**:
- Each effect class (ToneMapper, SSAO, etc.) owns its own pipeline cache
- PSO is created on-demand (first use)
- No global PSO map — effects manage their own pipelines

**Key insight**: Godot doesn't try to create all PSOs upfront. It uses lazy evaluation.

---

## 8. Summary: Godot's Leanness vs UE5

| Aspect | Godot | UE5 | UzEngine Target |
|--------|-------|-----|-----------------|
| **Root Signature Count** | Per-shader | Universal (shared) | Per-shader (leaner) |
| **Descriptor Heaps** | 2 GPU-visible (res + sampler) | 2-4 heaps (varies) | 2-3 GPU-visible |
| **Dynamic Buffers** | Root descriptors | Root descriptors | Root descriptors |
| **Bindless Support** | No (traditional descriptors) | Yes (SM6.6 descriptor indexing) | No (phase 4 future) |
| **Specialization Constants** | Bytecode patching | Shader variants | Bytecode patching (cheaper) |
| **PSO Caching** | Per-effect, lazy | Global, eager | Per-effect, lazy |
| **Abstraction Layer** | `RenderingDevice` + `RenderingDeviceDriver` | `FRHICommandList` + `FRHI*` | `RhiCommandBuffer` + `RhiBackend` |

### Why Godot is Leaner

1. **No universal root signature** — saves DWORD count, avoids "one-size-fits-all" waste
2. **No bindless** — simpler descriptor management, no descriptor indexing overhead
3. **Ephemeral descriptor sets** — `UniformSetCacheRD` auto-creates on demand (no pre-allocation)
4. **Per-effect pipelines** — effects own their own PSO caches (better cache locality)

### Pattern for UzEngine

**Adaptation**:
```cpp
// UzEngine: Per-shader root signature (Godot style)
struct ShaderMetadata {
    DxcCompilationResult dxil_bytecode;
    ID3D12RootSignature* root_signature;  // One per shader
    uint32_t root_sig_crc;

    // Metadata for binding
    struct {
        uint32_t push_const_root_param;
        uint32_t resource_set_root_param[MAX_SETS];
        uint32_t sampler_set_root_param[MAX_SETS];
    } binding_layout;
};

// On draw call:
cmd_list->SetGraphicsRootSignature(shader_metadata->root_signature);
cmd_list->SetGraphicsRootDescriptorTable(
    shader_metadata->binding_layout.resource_set_root_param[0],
    resource_heap->GetGPUDescriptorHandleForHeapStart() + set0_offset);
```

---

## References

- Godot 4.2 source: `/e/godot/drivers/d3d12/`
- NIR-to-DXIL compilation: `/e/godot/drivers/d3d12/d3d12_godot_nir_bridge.h`
- Tone mapper effect: `/e/godot/servers/rendering/renderer_rd/effects/tone_mapper.cpp`
- RenderingDevice abstraction: `/e/godot/servers/rendering/rendering_device.h` (1500+ lines)
