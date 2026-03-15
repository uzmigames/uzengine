# Godot D3D12 Patterns: Tactical Adaptation for UzEngine

**Purpose**: Concrete code patterns extracted from Godot 4's D3D12 driver for direct application in UzEngine's RHI layer.

---

## Pattern 1: Per-Shader Root Signature Creation

### Godot's Approach

**Source**: `/e/godot/drivers/d3d12/rendering_shader_container_d3d12.cpp:557-811`

Godot compiles shaders (SPIR-V → DXIL via NIR), reflects the DXIL to discover resource bindings, then **generates a root signature tailored to that specific shader**.

### Step-by-Step Process

**1. Shader Reflection Phase** (NIR callbacks discover resources)

```cpp
// NIR compiler calls this callback for each resource
static void _nir_report_resource(uint32_t p_register, uint32_t p_space,
    uint32_t p_dxil_type, void *p_data) {

    // Map DXIL resource type to resource class
    static const ResourceClass DXIL_TYPE_TO_CLASS[] = {
        RES_CLASS_INVALID,        // DXIL_RES_INVALID
        RES_CLASS_INVALID,        // DXIL_RES_SAMPLER
        RES_CLASS_CBV,            // DXIL_RES_CBV
        RES_CLASS_SRV,            // DXIL_RES_SRV_TYPED
        RES_CLASS_SRV,            // DXIL_RES_SRV_RAW
        RES_CLASS_SRV,            // DXIL_RES_SRV_STRUCTURED
        RES_CLASS_UAV,            // DXIL_RES_UAV_TYPED
        RES_CLASS_UAV,            // DXIL_RES_UAV_RAW
        RES_CLASS_UAV,            // DXIL_RES_UAV_STRUCTURED
        RES_CLASS_INVALID,        // DXIL_RES_UAV_STRUCTURED_WITH_COUNTER
    };

    ResourceClass resource_class = DXIL_TYPE_TO_CLASS[p_dxil_type];

    // Determine which uniform set this belongs to
    uint32_t set = p_register / GODOT_NIR_DESCRIPTOR_SET_MULTIPLIER;
    uint32_t binding = (p_register % GODOT_NIR_DESCRIPTOR_SET_MULTIPLIER)
                       / GODOT_NIR_BINDING_MULTIPLIER;

    // Record this resource's class and stage usage
    ReflectionBindingDataD3D12 &uniform_d3d12 =
        user_data.container->reflection_binding_set_uniforms_data_d3d12[...];

    uniform_d3d12.dxil_stages |= (1 << user_data.stage);
    if (resource_class != RES_CLASS_INVALID) {
        uniform_d3d12.resource_class = resource_class;
    } else if (p_dxil_type == DXIL_RES_SAMPLER) {
        uniform_d3d12.has_sampler = true;
    }
}
```

**2. Root Signature Generation Phase**

```cpp
bool RenderingShaderContainerD3D12::_generate_root_signature(
    BitField<RenderingDeviceCommons::ShaderStage> p_stages_processed) {

    LocalVector<D3D12_ROOT_PARAMETER1> root_params;

    // Phase 1: Push constants
    if (reflection_data_d3d12.dxil_push_constant_stages) {
        CD3DX12_ROOT_PARAMETER1 push_constant;
        push_constant.InitAsConstants(
            reflection_data.push_constant_size / sizeof(uint32_t),
            ROOT_CONSTANT_REGISTER,    // b127 (reserved register)
            0,
            stages_to_d3d12_visibility(reflection_data_d3d12.dxil_push_constant_stages));
        root_params.push_back(push_constant);
    }

    // Phase 2: Collect descriptor ranges by set
    struct TraceableDescriptorTable {
        uint32_t stages_mask = {};
        Vector<D3D12_DESCRIPTOR_RANGE1> ranges;
        uint32_t set = UINT_MAX;
    };

    Vector<TraceableDescriptorTable> resource_tables_maps;
    Vector<TraceableDescriptorTable> sampler_tables_maps;

    // Phase 3: For each binding, determine if it's a root descriptor or table entry
    for (uint32_t i = 0; i < reflection_binding_set_uniforms_count.size(); i++) {
        for (uint32_t j = 0; j < reflection_binding_set_uniforms_count[i]; j++) {
            const ReflectionBindingData &uniform = ...;
            ReflectionBindingDataD3D12 &uniform_d3d12 = ...;

            // 3a. Dynamic buffers → root descriptors (direct GPU VA)
            if (uniform.type == RDC::UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC ||
                uniform.type == RDC::UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC) {

                CD3DX12_ROOT_PARAMETER1 root_param = {};
                D3D12_SHADER_VISIBILITY visibility =
                    stages_to_d3d12_visibility(uniform.stages);

                switch (range_type) {
                    case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                        root_param.InitAsConstantBufferView(
                            dxil_register, 0,
                            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE,
                            visibility);
                        break;
                    case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                        root_param.InitAsShaderResourceView(
                            dxil_register, 0,
                            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
                            visibility);
                        break;
                    case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                        root_param.InitAsUnorderedAccessView(
                            dxil_register, 0,
                            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
                            visibility);
                        break;
                }

                uniform_d3d12.root_param_idx = root_params.size();
                root_params.push_back(root_param);

            // 3b. Static resources → descriptor table entries
            } else {
                D3D12_DESCRIPTOR_RANGE_TYPE range_type = ...;

                CD3DX12_DESCRIPTOR_RANGE1 range;
                D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

                // Data volatility hints for optimization
                if (range_type == D3D12_DESCRIPTOR_RANGE_TYPE_SRV ||
                    range_type == D3D12_DESCRIPTOR_RANGE_TYPE_UAV) {
                    flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
                } else if (range_type == D3D12_DESCRIPTOR_RANGE_TYPE_CBV) {
                    flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
                }

                range.Init(range_type, num_descriptors, dxil_register, 0, flags,
                           descriptor_offset);

                // Accumulate in appropriate table
                TraceableDescriptorTable &table = ...;
                table.ranges.push_back(range);
            }
        }
    }

    // Phase 4: Create descriptor table root parameters
    for (const TraceableDescriptorTable &table : resource_tables_maps) {
        CD3DX12_ROOT_PARAMETER1 root_table = {};
        root_table.InitAsDescriptorTable(table.ranges.size(), table.ranges.ptr(),
                                         stages_to_d3d12_visibility(table.stages_mask));
        reflection_binding_set_data_d3d12.ptrw()[table.set].resource_root_param_idx
            = root_params.size();
        root_params.push_back(root_table);
    }

    for (const TraceableDescriptorTable &table : sampler_tables_maps) {
        CD3DX12_ROOT_PARAMETER1 root_table = {};
        root_table.InitAsDescriptorTable(table.ranges.size(), table.ranges.ptr(),
                                         stages_to_d3d12_visibility(table.stages_mask));
        reflection_binding_set_data_d3d12.ptrw()[table.set].sampler_root_param_idx
            = root_params.size();
        root_params.push_back(root_table);
    }

    // Phase 5: Serialize and create root signature
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_sig_desc = {};
    D3D12_ROOT_SIGNATURE_FLAGS root_sig_flags = 0;

    // Deny access to unused shader stages (performance optimization)
    if (!p_stages_processed.has_flag(RenderingDeviceCommons::SHADER_STAGE_VERTEX_BIT)) {
        root_sig_flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;
    }
    if (!p_stages_processed.has_flag(RenderingDeviceCommons::SHADER_STAGE_FRAGMENT_BIT)) {
        root_sig_flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
    }

    root_sig_desc.Init_1_1(root_params.size(), root_params.ptr(), 0, nullptr,
                           root_sig_flags);

    ID3DBlob *root_sig_blob = nullptr, *error_blob = nullptr;
    HRESULT res = D3DX12SerializeVersionedRootSignature(
        HMODULE(lib_d3d12), &root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1_1,
        &root_sig_blob, &error_blob);

    if (SUCCEEDED(res)) {
        // Store binary for later creation
        root_signature_bytes.resize(root_sig_blob->GetBufferSize());
        memcpy(root_signature_bytes.ptrw(), root_sig_blob->GetBufferPointer(),
               root_sig_blob->GetBufferSize());

        // CRC for fast comparison
        root_signature_crc = crc32(0, nullptr, 0);
        root_signature_crc = crc32(root_signature_crc,
                                   (const Bytef *)root_sig_blob->GetBufferPointer(),
                                   root_sig_blob->GetBufferSize());
        root_sig_blob->Release();
        return true;
    } else {
        // Log error, cleanup
        if (error_blob) error_blob->Release();
        return false;
    }
}
```

### For UzEngine

```cpp
// uz_engine/src/engine/renderer/shader.h
struct ShaderReflection {
    // From NIR callbacks
    HashMap<uint32_t, ReflectedBinding> bindings;  // register → binding info
    uint32_t push_constant_size = 0;
    uint32_t stages = 0;  // Bitmask of active shader stages
};

struct ShaderD3D12 {
    ComPtr<ID3D12RootSignature> root_signature;
    Vector<uint8_t> root_signature_bytes;
    uint32_t root_signature_crc = 0;

    struct BindingLayout {
        uint32_t root_param_idx[MAX_BINDINGS];
        uint32_t descriptor_offset[MAX_BINDINGS];
        uint32_t descriptor_count;
    } resource_layout, sampler_layout;

    uint32_t push_constant_root_param = UINT32_MAX;
};

// Creation:
ShaderD3D12 shader = CreateShaderFromDXIL(dxil_bytes);
// → Automatically generates optimal root signature
```

---

## Pattern 2: Dynamic vs Static Descriptor Binding

### Dynamic Buffers (Root Descriptors)

**Use case**: Uniform buffers and storage buffers that change per-frame or per-draw

```cpp
// Creation (once per shader)
if (uniform.type == UNIFORM_BUFFER_DYNAMIC || uniform.type == STORAGE_BUFFER_DYNAMIC) {
    CD3DX12_ROOT_PARAMETER1 root_param = {};
    root_param.InitAsConstantBufferView(
        dxil_register, 0,
        D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE,
        D3D12_SHADER_VISIBILITY_ALL);
    root_params.push_back(root_param);
}

// On each draw (per-frame update)
if (binding.root_param_idx != UINT_MAX) {
    D3D12_GPU_VIRTUAL_ADDRESS buffer_location = buffer->GetGPUVirtualAddress() + offset;
    cmd_list->SetGraphicsRootConstantBufferView(binding.root_param_idx, buffer_location);
}
```

**Advantage**: Zero descriptor heap overhead for frequently-updated buffers.

### Static Resources (Descriptor Tables)

**Use case**: Textures, samplers, static buffers

```cpp
// Creation (once per shader)
CD3DX12_DESCRIPTOR_RANGE1 range;
range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
           num_descriptors,  // e.g., array of 16 textures
           dxil_register,
           0,  // space 0
           D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE,
           offset_in_heap);

CD3DX12_ROOT_PARAMETER1 root_table = {};
root_table.InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_ALL);
root_params.push_back(root_table);

// On descriptor set bind (per-draw, but typically cached)
cmd_list->SetGraphicsRootDescriptorTable(
    root_param_idx,
    descriptor_heap.GetGPUDescriptorHandleForHeapStart() + offset);
```

**Advantage**: Amortizes descriptor copy overhead across draws using same set.

---

## Pattern 3: Descriptor Heap Management

### Heap Allocation Strategy

**Source**: `/e/godot/drivers/d3d12/rendering_device_driver_d3d12.h:154-172`

```cpp
struct DescriptorHeap {
    ComPtr<ID3D12DescriptorHeap> heap;
    uint32_t descriptor_size = 0;
    ComPtr<D3D12MA::VirtualBlock> virtual_block;  // Virtual allocator

    struct Allocation {
        uint64_t virtual_handle = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = {};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = {};
    };

    Error allocate(uint32_t count, Allocation &out) {
        // Virtual allocator returns virtual handle
        Error err = virtual_block->Allocate(...);
        if (err) return err;

        // Compute CPU/GPU handles from virtual offset
        cpu_handle = heap->GetCPUDescriptorHandleForHeapStart();
        cpu_handle.ptr += virtual_offset * descriptor_size;

        gpu_handle = heap->GetGPUDescriptorHandleForHeapStart();
        gpu_handle.ptr += virtual_offset * descriptor_size;

        return OK;
    }

    void free(const Allocation &alloc) {
        virtual_block->Free(alloc.virtual_handle);
    }
};

// Per-device
DescriptorHeap resource_descriptor_heap;  // 16K CBV/SRV/UAV
DescriptorHeap sampler_descriptor_heap;   // 2K samplers
```

### For UzEngine

```cpp
// uz_engine/src/engine/renderer/descriptor_heap.h
class DescriptorHeap {
public:
    DescriptorHeap(ID3D12Device *device, uint32_t capacity,
                   D3D12_DESCRIPTOR_HEAP_TYPE type);

    struct Allocation {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
        uint32_t index;
    };

    Allocation allocate(uint32_t count);
    void free(const Allocation &alloc);

private:
    ComPtr<ID3D12DescriptorHeap> heap;
    D3D12MA::VirtualBlock* virtual_allocator;
    uint32_t increment_size;
};

// Global heaps
DescriptorHeap g_resource_heap(device, 16384, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
DescriptorHeap g_sampler_heap(device, 2048, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
```

---

## Pattern 4: Root Signature CRC Caching

### Why CRC?

Godot caches root signature pointers via CRC to avoid redundant `SetGraphicsRootSignature()` calls:

```cpp
struct CommandBufferInfo {
    ID3D12GraphicsCommandList *cmd_list;
    uint32_t graphics_root_signature_crc = 0;  // Last bound signature CRC
};

// On bind:
if (cmd_buf_info->graphics_root_signature_crc != shader_info->root_signature_crc) {
    cmd_buf_info->cmd_list->SetGraphicsRootSignature(
        shader_info->root_signature.Get());
    cmd_buf_info->graphics_root_signature_crc = shader_info->root_signature_crc;
}
```

**Cost**: CRC comparison is much cheaper than issuing a D3D12 command.

### For UzEngine

```cpp
// uz_engine/src/engine/renderer/command_buffer.h
struct CommandBuffer {
    ComPtr<ID3D12GraphicsCommandList> cmd_list;

    // Caching
    ID3D12RootSignature* current_graphics_root_sig = nullptr;
    uint32_t current_graphics_root_sig_crc = 0;

    void set_root_signature(ID3D12RootSignature *sig, uint32_t sig_crc) {
        if (current_graphics_root_sig_crc == sig_crc) return;  // Cache hit
        cmd_list->SetGraphicsRootSignature(sig);
        current_graphics_root_sig = sig;
        current_graphics_root_sig_crc = sig_crc;
    }
};
```

---

## Pattern 5: Fullscreen Pass Binding (Tone Mapper)

### Abstraction Levels

**Level 1 (Game Code)** — High-level effect
```cpp
void apply_tonemap(RID source, RID target) {
    tone_mapper->tonemapper(source, target, settings);
}
```

**Level 2 (Effect Code)** — Abstract resource bindings
```cpp
void ToneMapper::tonemapper(RID source, RID target, const Settings &s) {
    RD::Uniform source_color;
    source_color.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE;
    source_color.binding = 0;
    source_color.ids.push_back(source);

    RD::get_singleton()->draw_list_bind_uniform_set(
        draw_list,
        uniform_set_cache->get_cache(shader, 0, source_color),  // Auto-creates
        0);
}
```

**Level 3 (RenderingDevice)** — Backend-agnostic
```cpp
void RenderingDevice::draw_list_bind_uniform_set(
    DrawListID list, RID uniform_set, uint32_t index) {
    // Vulkan: vkCmdBindDescriptorSets()
    // Metal: [render_cmd setBuffer:... atIndex:...]
    // D3D12: cmd_list->SetGraphicsRootDescriptorTable(...)
}
```

**Level 4 (RenderingDeviceDriver D3D12)** — Backend-specific
```cpp
void RenderingDeviceDriverD3D12::command_bind_render_uniform_sets(...) {
    // Set root signature
    // For each uniform set:
    //   - Bind root descriptors (dynamic buffers)
    //   - Bind descriptor tables (static resources)
}
```

### For UzEngine

**Don't expose D3D12 details above the RHI boundary**:

```cpp
// GOOD: uz_engine/src/engine/renderer/tonemap.cpp
void TonemapPass::render(RhiCommandBuffer *cmd, RhiTexture *source,
                         RhiFramebuffer *target) {
    cmd->bind_pipeline(tonemap_pipeline);  // Abstract

    RhiBoundUniform uniforms[] = {
        { 0, source },  // set, texture
        { 1, target_lut }
    };
    cmd->bind_uniforms(uniforms, 2);  // Abstract

    cmd->draw(3, 0);  // Full-screen triangle
}

// BAD: Don't do this
void TonemapPass::render_d3d12(ID3D12GraphicsCommandList *cmd, ...) {
    cmd->SetGraphicsRootSignature(...);  // WRONG: D3D12 leaks out
    cmd->SetGraphicsRootDescriptorTable(...);  // WRONG
}
```

---

## Pattern 6: Shader Specialization Constants (Bytecode Patching)

### Godot's Method: Patch, Don't Re-Reflect

**Instead of**:
1. Create shader variant A (specialization = 1)
2. Create shader variant B (specialization = 0)
3. Choose variant at pipeline creation

**Godot does**:
1. Compile once with placeholder specialization
2. **Record bytecode offsets** for all specialization constants
3. **Patch bytecode** at pipeline creation with actual values
4. **Create PSO** from patched bytecode

**File**: `/e/godot/drivers/d3d12/rendering_shader_container_d3d12.cpp:877-911`

```cpp
void RenderingShaderContainerD3D12::_nir_report_sc_bit_offset(
    uint32_t p_sc_id, uint64_t p_bit_offset, void *p_data) {

    const GodotNirCallbackUserData &user_data = *(GodotNirCallbackUserData *)p_data;

    for (int64_t i = 0; i < user_data.container->reflection_specialization_data.size(); i++) {
        const ReflectionSpecializationData &sc =
            user_data.container->reflection_specialization_data[i];
        ReflectionSpecializationDataD3D12 &sc_d3d12 =
            user_data.container->reflection_specialization_data_d3d12.ptrw()[i];

        if (sc.constant_id != p_sc_id) {
            continue;
        }

        // Record bit offset in bytecode for this SC and stage
        uint32_t offset_idx = SHADER_STAGES_BIT_OFFSET_INDICES[user_data.stage];
        sc_d3d12.stages_bit_offsets[offset_idx] = p_bit_offset;
        found = true;
        break;
    }
}

// At pipeline creation:
static uint32_t patch_specialization_constant(
    const void *p_value,
    const uint64_t (&p_stages_bit_offsets)[D3D12_BITCODE_OFFSETS_NUM_STAGES],
    HashMap<ShaderStage, Vector<uint8_t>> &r_stages_bytecodes,
    bool p_is_first_patch) {

    for (uint32_t i = 0; i < D3D12_BITCODE_OFFSETS_NUM_STAGES; i++) {
        if (p_stages_bit_offsets[i] == 0) continue;

        Vector<uint8_t> &bytecode = r_stages_bytecodes[...];
        uint8_t *byte_ptr = bytecode.ptrw() + p_stages_bit_offsets[i] / 8;

        // Patch the constant value into bytecode
        *(uint32_t *)byte_ptr = *(uint32_t *)p_value;
    }

    return 0;
}
```

### For UzEngine

This pattern is valuable for **performance tuning** (e.g., enable/disable features without root signature changes):

```cpp
// uz_engine/src/engine/renderer/shader_specialization.h
struct SpecializationConstant {
    uint32_t id;
    uint64_t bytecode_bit_offset[3];  // vs, fs, cs
    uint32_t default_value;
};

// At pipeline creation:
Vector<uint8_t> dxil_bytecode = read_cached_bytecode();

for (const auto &spec : specialization_constants) {
    uint32_t *ptr = (uint32_t *)(dxil_bytecode.data() + spec.bytecode_bit_offset[vs_idx] / 8);
    *ptr = actual_specialization_value;
}

// Now create PSO from patched bytecode
```

---

## Summary Table: Godot Patterns → UzEngine

| Godot Pattern | File | UzEngine Adaptation |
|---------------|------|-------------------|
| **Per-shader root signature** | `rendering_shader_container_d3d12.cpp:557-811` | `ShaderD3D12::create_root_signature()` |
| **Dynamic root descriptors** | `rendering_shader_container_d3d12.cpp:700-720` | `ShaderMetadata::root_param_idx[binding]` |
| **Descriptor table ranges** | `rendering_shader_container_d3d12.cpp:629-642` | `DescriptorTableLayout` |
| **Heap virtual allocation** | `rendering_device_driver_d3d12.h:154-172` | `DescriptorHeap + D3D12MA::VirtualBlock` |
| **Root sig CRC caching** | `rendering_device_driver_d3d12.cpp:4841-4843` | `CommandBuffer::current_root_sig_crc` |
| **Fullscreen pass abstraction** | `tone_mapper.cpp:211-213` | `RhiCommandBuffer::bind_uniforms()` |
| **Bytecode patching for spec constants** | `rendering_shader_container_d3d12.cpp:62-70` | `SpecializationConstant::bytecode_bit_offset[]` |

---

## References

All line numbers refer to Godot 4.2 source code in `/e/godot/`:

- Root signature generation: `drivers/d3d12/rendering_shader_container_d3d12.cpp:557`
- Descriptor binding: `drivers/d3d12/rendering_device_driver_d3d12.cpp:4841`
- Heap management: `drivers/d3d12/rendering_device_driver_d3d12.h:154`
- Effect example: `servers/rendering/renderer_rd/effects/tone_mapper.cpp:116`
