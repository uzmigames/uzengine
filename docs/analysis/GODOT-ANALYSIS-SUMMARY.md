# Godot 4 RenderingDevice Analysis: Summary

**Date**: 2026-03-13
**Source**: Godot 4.2 engine (`/e/godot/drivers/d3d12/` + `servers/rendering/`)
**Analyst**: Claude
**Files Created**: 3 detailed analysis documents

---

## Key Findings

### 1. Root Signature Strategy: Per-Shader, Not Universal

Godot creates **one root signature per shader** (NIR reflection → D3D12 serialization), not a universal one shared by all shaders.

- **File**: `/e/godot/drivers/d3d12/rendering_shader_container_d3d12.cpp:557-811`
- **Why**: Leaner DWORD budget (uses only what's needed)
- **Pattern**: Reflection → generate descriptor ranges → serialize root signature → CRC cache

**For UzEngine**: Adopt per-shader root signatures instead of UE5's universal approach.

### 2. Dynamic Buffers Use Root Descriptors, Not Descriptor Tables

Dynamic uniform/storage buffers pass GPU virtual address directly via `SetGraphicsRootConstantBufferView()`, not via descriptor heap.

- **File**: `/e/godot/drivers/d3d12/rendering_shader_container_d3d12.cpp:700-720`
- **Why**: Zero descriptor copy overhead (root descriptors are free)
- **Pattern**: Detect `UNIFORM_BUFFER_DYNAMIC` → create root descriptor parameter (direct VA binding)

**For UzEngine**: Use root descriptors for all dynamic buffers.

### 3. Descriptor Heap Allocation: Virtual Block Strategy

Godot uses `D3D12MA::VirtualBlock` for sub-allocation within a large GPU-visible heap (16K+ descriptors).

- **File**: `/e/godot/drivers/d3d12/rendering_device_driver_d3d12.h:154-172`
- **Why**: Better fragmentation handling than static pre-allocation
- **Pattern**: Virtual allocator + GPU descriptor handles calculated from virtual offsets

**For UzEngine**: Use virtual block allocator instead of static heaps.

### 4. Fullscreen Passes Are Fully Abstracted

Tone mapper (fullscreen pass example) never mentions D3D12. It builds abstract `RD::Uniform` objects and calls `UniformSetCacheRD::get_cache()` which auto-creates descriptor sets.

- **File**: `/e/godot/servers/rendering/renderer_rd/effects/tone_mapper.cpp:116-214`
- **Why**: True backend independence (same code works on Vulkan/Metal/D3D12)
- **Pattern**: Effect code → RD abstraction → RenderingDeviceDriver implementation

**For UzEngine**: Don't expose D3D12 types above the RHI boundary.

### 5. Specialization Constants: Bytecode Patching, Not Variants

Instead of creating shader variants (2^N explosion), Godot:
1. Compiles shader once
2. Records bytecode offsets of specialization constants
3. Patches bytecode at pipeline creation
4. Creates PSO from patched bytecode (no recompilation)

- **File**: `/e/godot/drivers/d3d12/rendering_shader_container_d3d12.cpp:62-70` (patch code)
- **Why**: One compile per shader, not N compiles per variant
- **Cost**: Bytecode patch << shader recompilation

**For UzEngine**: Use bytecode patching instead of shader variants.

### 6. No Bindless Descriptors (SM6.6 Descriptor Indexing)

Godot uses **traditional descriptor tables**, not SM6.6 descriptor indexing. All resources are:
1. Pre-allocated in descriptor heaps
2. Bound via root signature descriptor tables (2 per uniform set: resources + samplers)
3. Copied into uniform sets as needed

**Implication**: Godot achieves AAA results without bindless. Bindless is a future optimization, not critical path.

---

## Direct Patterns for UzEngine

### Pattern 1: Per-Shader Root Signature

```cpp
struct ShaderD3D12 {
    ComPtr<ID3D12RootSignature> root_signature;
    Vector<uint8_t> root_signature_bytes;
    uint32_t root_signature_crc;

    struct {
        uint32_t push_constant_root_param;
        uint32_t resource_table_root_param[MAX_SETS];
        uint32_t sampler_table_root_param[MAX_SETS];
    } binding_layout;
};
```

### Pattern 2: Descriptor Heap with Virtual Allocation

```cpp
class DescriptorHeap {
    ComPtr<ID3D12DescriptorHeap> heap;
    D3D12MA::VirtualBlock* virtual_allocator;

    Allocation allocate(uint32_t count) {
        uint64_t offset = virtual_allocator->Allocate(count);
        return {
            cpu_handle = heap->GetCPUHandle() + offset * increment_size,
            gpu_handle = heap->GetGPUHandle() + offset * increment_size
        };
    }
};

// Global heaps
DescriptorHeap g_resource_heap(device, 16384, CBV_SRV_UAV);
DescriptorHeap g_sampler_heap(device, 2048, SAMPLER);
```

### Pattern 3: Root Signature CRC Caching

```cpp
struct CommandBuffer {
    ID3D12RootSignature* current_root_sig;
    uint32_t current_root_sig_crc;

    void set_root_signature(ID3D12RootSignature *sig, uint32_t crc) {
        if (current_root_sig_crc == crc) return;  // Cache hit
        cmd_list->SetGraphicsRootSignature(sig);
        current_root_sig = sig;
        current_root_sig_crc = crc;
    }
};
```

### Pattern 4: Dynamic Buffer Root Descriptors

```cpp
// At shader creation: identify dynamic buffers
if (uniform.type == UNIFORM_BUFFER_DYNAMIC) {
    CD3DX12_ROOT_PARAMETER1 root_param;
    root_param.InitAsConstantBufferView(
        register, 0,
        D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE,
        visibility);
    root_params.push_back(root_param);
}

// At draw time: bind directly
uint64_t buffer_va = buffer->GetGPUVirtualAddress() + offset;
cmd_list->SetGraphicsRootConstantBufferView(root_param_idx, buffer_va);
```

### Pattern 5: Bytecode Patching for Specialization Constants

```cpp
// At shader compilation: record offsets
struct SpecializationConstant {
    uint32_t id;
    uint64_t bytecode_bit_offset[3];  // vs, fs, cs
};

// At pipeline creation: patch and create PSO
Vector<uint8_t> patched_bytecode = original_bytecode.duplicate();
for (const auto &spec : specialization_constants) {
    uint32_t *ptr = (uint32_t *)(patched_bytecode.data() + spec.bytecode_bit_offset[stage_idx] / 8);
    *ptr = actual_specialization_value;
}
PSO pso = device->CreateGraphicsPipelineState(patched_bytecode);
```

---

## Godot vs UE5 Comparison

| Feature | Godot | UE5 | Winner |
|---------|-------|-----|--------|
| Root Signature | Per-shader | Universal | Godot (leaner) |
| Dynamic Buffers | Root descriptors (direct VA) | Descriptor copy | Godot (zero-copy) |
| Heap Allocation | Virtual block | Static pre-alloc | Godot (fragmentation) |
| Specialization | Bytecode patch | Shader variants | Godot (1 vs 2^N compiles) |
| PSO Caching | Per-effect, lazy | Global, eager | Godot (no upfront hitch) |
| Abstraction | Opaque RIDs | Type-wrapped | Godot (cleaner) |
| Bindless Support | No (phase 4) | Yes (SM6.6) | UE5 (but not critical) |

**Recommendation**: **Adopt Godot's patterns** for UzEngine (leaner, more efficient).

---

## Documents Created

1. **`godot-renderingdevice-architecture.md`** (2500+ lines)
   - Deep dive: How Godot's RenderingDevice abstracts D3D12
   - Root signature generation algorithm
   - Descriptor heap management
   - Fullscreen pass binding
   - Cross-backend abstraction layers

2. **`godot-d3d12-patterns-uzengine.md`** (1500+ lines)
   - Tactical patterns with code examples
   - 6 concrete patterns ready for copy/paste adaptation
   - File paths + line numbers for reference
   - Step-by-step implementation guide

3. **`godot-vs-ue5-renderingdevice.md`** (1200+ lines)
   - Side-by-side comparison of architecture choices
   - Why Godot is leaner (not just different)
   - UzEngine decision matrix
   - Cost/benefit analysis

---

## Key Code Locations

| Concept | File | Lines |
|---------|------|-------|
| Root signature generation | `rendering_shader_container_d3d12.cpp` | 557-811 |
| Descriptor binding | `rendering_device_driver_d3d12.cpp` | 4841-4908 |
| Descriptor heap management | `rendering_device_driver_d3d12.h` | 154-172 |
| NIR resource reflection | `rendering_shader_container_d3d12.cpp` | 813-875 |
| Specialization constant patching | `rendering_shader_container_d3d12.cpp` | 62-70 |
| Fullscreen pass example (tonemap) | `effects/tone_mapper.cpp` | 116-214 |
| RenderingDevice interface | `rendering_device.h` | 1-1500 |

---

## Actionable Next Steps

1. **Create RHI layer** (`uz_engine/src/engine/renderer/rhi/`)
   - Opaque RID-based interface (no D3D12 types exposed)
   - Backend drivers for D3D12, Vulkan (future)

2. **Implement ShaderD3D12**
   - NIR→DXIL compilation
   - Root signature generation (per-shader)
   - CRC caching

3. **Implement descriptor management**
   - Virtual block allocator
   - Per-frame cleanup

4. **Migrate effects**
   - Remove D3D12 calls from game code
   - Use RID-based bindings only

5. **Deprecate src/renderer/**
   - Move to src/engine/renderer/
   - Follow Godot's abstraction patterns

---

## References

- Godot 4.2 source: `/e/godot/`
- Specific analysis:
  - Root signatures: `/e/godot/drivers/d3d12/rendering_shader_container_d3d12.cpp:557`
  - Binding: `/e/godot/drivers/d3d12/rendering_device_driver_d3d12.cpp:4841`
  - Effects: `/e/godot/servers/rendering/renderer_rd/effects/tone_mapper.cpp:116`

---

**Note**: All analysis performed on Godot 4.2 source (publicly available). UzEngine can adopt these patterns without licensing concerns (Godot uses MIT license).
