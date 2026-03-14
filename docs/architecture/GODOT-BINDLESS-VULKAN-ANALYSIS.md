# Godot 4 Vulkan Bindless/Descriptor Indexing Analysis

**Date**: 2026-03-14
**Engine**: Godot 4 (4.x branch)
**Focus**: How Godot achieves dynamic texture access on Vulkan without true bindless descriptors

---

## Executive Summary

**Critical finding**: Godot **does NOT use bindless descriptors** (VK_EXT_descriptor_indexing or VK_EXT_descriptor_buffer). Instead, Godot uses **fixed descriptor set layout with descriptor arrays** — a simpler approach that still enables dynamic texture access via proper allocation strategy.

- **Descriptor arrays**: Textures are bound as `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE` with `descriptorCount = N`
- **No VK_EXT_descriptor_indexing**: No descriptor indexing extension employed
- **No VK_EXT_descriptor_buffer**: No newer buffer-based descriptor model
- **Shader compilation**: **glslang** (GLSL to SPIR-V 1.4), NOT dxc
- **Descriptor set layout**: Multiple descriptor sets (set 0, 1, 2+), managed per-shader
- **Descriptor pool allocation**: Linear pools + per-pool-type hash map (prevents fragmentation)

---

## 1. Descriptor Architecture — No True Bindless

### 1.1 What Godot Does NOT Use

**No VK_EXT_descriptor_indexing**:
```cpp
// Searched /e/godot/drivers/vulkan — ZERO occurrences found
VK_EXT_descriptor_indexing     // NOT enabled
VK_EXT_descriptor_buffer       // NOT enabled
descriptor_buffer              // NOT in codebase
nonuniformEXT                  // NOT in shaders
variable_descriptor_count      // NOT used
```

Source: `/e/godot/drivers/vulkan/rendering_device_driver_vulkan.h` + `.cpp` (7418 lines) — no bindless patterns.

### 1.2 What Godot DOES Use — Fixed Descriptor Arrays

Godot uses **static descriptor set layouts with descriptor arrays** per binding:

```cpp
// From rendering_device_driver_vulkan.cpp, ~line 4125
case UNIFORM_TYPE_TEXTURE: {
    layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    layout_binding.descriptorCount = uniform.length;  // <-- Array size baked into layout
} break;

case UNIFORM_TYPE_IMAGE: {
    layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    layout_binding.descriptorCount = uniform.length;
} break;
```

**Key insight**: `uniform.length` is the **static array size** declared at shader compile time. This array is allocated once in the descriptor set layout, not dynamically resized.

### 1.3 Descriptor Pool Strategy — Prevent Fragmentation

Godot uses a **per-pool-type hash map** to avoid descriptor pool fragmentation (lines 530-537):

```cpp
// rendering_device_driver_vulkan.h, lines 520-537
static const uint32_t MAX_UNIFORM_POOL_ELEMENT = 65535;

struct DescriptorSetPoolKey {
    uint16_t uniform_type[UNIFORM_TYPE_MAX] = {};

    bool operator<(const DescriptorSetPoolKey &p_other) const {
        return memcmp(uniform_type, p_other.uniform_type, sizeof(uniform_type)) < 0;
    }
};

using DescriptorSetPools = RBMap<DescriptorSetPoolKey, HashMap<VkDescriptorPool, uint32_t>>;
DescriptorSetPools descriptor_set_pools;
uint32_t max_descriptor_sets_per_pool = 0;

HashMap<int, DescriptorSetPools> linear_descriptor_set_pools;
bool linear_descriptor_pools_enabled = true;  // <-- Separate linear pools for per-frame resets
```

**Strategy**:
- Each `DescriptorSetPoolKey` groups descriptors by type/count signature
- Each key has its own `HashMap<VkDescriptorPool, uint32_t>` (pool → free count)
- Linear pools (frame-scoped) can be reset without freeing individual sets

---

## 2. Descriptor Set Layout — Multi-Set Model

### 2.1 Set Structure

Godot supports **multiple descriptor sets per shader** (typically 0, 1, 2+):

```cpp
// From canvas_uniforms_inc.glsl (canvas shader)
layout(set = 0, binding = 1, std140) uniform CanvasData {
    mat4 canvas_transform;
    mat4 screen_transform;
    vec4 canvas_modulation;
    // ... 10+ members
} canvas_data;

#ifdef MATERIAL_UNIFORMS_USED
layout(set = 1, binding = 0, std140) uniform MaterialUniforms {
    #MATERIAL_UNIFORMS
} material;
#endif
```

**Set 0**: Global per-frame data (canvas, view, lights)
**Set 1**: Per-material parameters (can vary per draw call)

### 2.2 Layout Binding Creation

From `/e/godot/drivers/vulkan/rendering_device_driver_vulkan.cpp` (~line 4115):

```cpp
for (uint32_t i = 0; i < shader_refl.uniform_sets.size(); i++) {
    for (uint32_t j = 0; j < shader_refl.uniform_sets[i].size(); j++) {
        const ShaderUniform &uniform = shader_refl.uniform_sets[i][j];

        VkDescriptorSetLayoutBinding layout_binding = {};
        layout_binding.binding = uniform.binding;
        layout_binding.descriptorCount = 1;  // Default

        switch (uniform.type) {
            case UNIFORM_TYPE_SAMPLER:
                layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                layout_binding.descriptorCount = uniform.length;  // <-- Array!
                break;
            case UNIFORM_TYPE_SAMPLER_WITH_TEXTURE:
                layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                layout_binding.descriptorCount = uniform.length;
                break;
            case UNIFORM_TYPE_TEXTURE:
                layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                layout_binding.descriptorCount = uniform.length;
                break;
            case UNIFORM_TYPE_IMAGE:
                layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                layout_binding.descriptorCount = uniform.length;
                break;
            // ... other types (buffers, etc.)
        }
    }

    VkDescriptorSetLayoutCreateInfo layout_create_info = {};
    layout_create_info.bindingCount = vk_set_bindings[i].size();
    layout_create_info.pBindings = vk_set_bindings[i].ptr();

    // Create layout for set i
    vkCreateDescriptorSetLayout(vk_device, &layout_create_info, ...);
}
```

**Result**: Each shader creates **N descriptor set layouts** (one per set), each with its own bindings and array sizes.

---

## 3. Uniform Set Creation — BoundUniform Pattern

### 3.1 High-Level API

```cpp
// From rendering_device_driver.h
struct BoundUniform {
    UniformType type = UNIFORM_TYPE_MAX;
    uint32_t binding = 0xffffffff;
    LocalVector<ID> ids;  // <-- Array of texture/buffer IDs
    bool immutable_sampler = false;
};

virtual UniformSetID uniform_set_create(
    VectorView<BoundUniform> p_uniforms,
    ShaderID p_shader,
    uint32_t p_set_index,
    int p_linear_pool_index
) = 0;
```

### 3.2 Example Usage (from Godot's own code)

```cpp
// From servers/rendering/renderer_rd/cluster_builder_rd.cpp
Vector<RD::Uniform> uniforms;
{
    RD::Uniform u;
    u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
    u.binding = 1;
    u.append_id(cluster_render_buffer);
    uniforms.push_back(u);
}
{
    RD::Uniform u;
    u.uniform_type = RD::UNIFORM_TYPE_SAMPLER;
    u.binding = 4;
    u.append_id(p_depth_buffer_sampler);
    uniforms.push_back(u);
}

cluster_render_uniform_set = RD::get_singleton()->uniform_set_create(
    uniforms,
    shared->cluster_render.shader,
    0  // set index
);
```

### 3.3 Implementation (Vulkan)

```cpp
// From rendering_device_driver_vulkan.cpp, line 4574
RDD::UniformSetID RenderingDeviceDriverVulkan::uniform_set_create(
    VectorView<BoundUniform> p_uniforms,
    ShaderID p_shader,
    uint32_t p_set_index,
    int p_linear_pool_index
) {
    DescriptorSetPoolKey pool_key;

    // Build pool key from uniforms
    for (const BoundUniform &uniform : p_uniforms) {
        pool_key.uniform_type[uniform.type]++;
    }

    // Allocate from pool for this key
    VkDescriptorSet vk_descriptor_set = _get_descriptor_set_from_pool(
        pool_key,
        shader_info->vk_descriptor_set_layouts[p_set_index]
    );

    // Write descriptors
    for (const BoundUniform &uniform : p_uniforms) {
        switch (uniform.type) {
            case UNIFORM_TYPE_TEXTURE: {
                uint32_t num_descriptors = uniform.ids.size();
                VkDescriptorImageInfo *vk_img_infos = ALLOCA_ARRAY(
                    VkDescriptorImageInfo,
                    num_descriptors
                );

                for (uint32_t j = 0; j < num_descriptors; j++) {
                    vk_img_infos[j] = {};
                    vk_img_infos[j].imageView =
                        ((const TextureInfo *)uniform.ids[j].id)->vk_view;
                    vk_img_infos[j].imageLayout =
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }

                vk_writes[writes_amount].descriptorType =
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                vk_writes[writes_amount].descriptorCount = num_descriptors;
                vk_writes[writes_amount].pImageInfo = vk_img_infos;
                vk_writes[writes_amount].dstArrayElement = 0;
                vk_writes[writes_amount].dstBinding = uniform.binding;

                vkUpdateDescriptorSets(vk_device, writes_amount, vk_writes, ...);
            } break;
            // ... other types
        }
    }

    return new_uniform_set_id;
}
```

**Key pattern**:
1. All textures for a binding are passed as `uniform.ids` (a vector)
2. A **single `vkUpdateDescriptorSets` write** updates all array elements
3. The descriptors are **static** — no dynamic indexing in the shader

---

## 4. Shader Compilation — glslang to SPIR-V 1.4

### 4.1 Compilation Pipeline

```cpp
// From modules/glslang/shader_compile.h
Vector<uint8_t> compile_glslang_shader(
    RenderingDeviceCommons::ShaderStage p_stage,
    const String &p_source_code,
    RenderingDeviceCommons::ShaderLanguageVersion p_language_version,
    RenderingDeviceCommons::ShaderSpirvVersion p_spirv_version,
    String *r_error
);
```

**Godot uses**:
- **glslang** (GLSL compiler) — NOT dxc/DirectXShaderCompiler
- **SPIR-V 1.4** output (supports spirv 1.4 features)
- **smolv compression** (optional, for storage)

### 4.2 Shader Container

From `/e/godot/drivers/vulkan/rendering_shader_container_vulkan.cpp`:

```cpp
bool RenderingShaderContainerVulkan::_set_code_from_spirv(
    const ReflectShader &p_shader
) {
    const LocalVector<ReflectShaderStage> &p_spirv = p_shader.shader_stages;

    PackedByteArray code_bytes;
    shaders.resize(p_spirv.size());
    for (uint64_t i = 0; i < p_spirv.size(); i++) {
        RenderingShaderContainer::Shader &shader = shaders.ptrw()[i];

        if (debug_info_enabled) {
            // Store SPIR-V as-is
            shader.code_compressed_bytes = p_spirv[i].spirv_data();
            shader.code_compression_flags = 0;
        } else {
            // Compress with smolv
            Span<uint8_t> spirv = p_spirv[i].spirv().reinterpret<uint8_t>();
            smolv::ByteArray smolv_bytes;
            bool smolv_encoded = smolv::Encode(
                spirv.ptr(),
                spirv.size(),
                smolv_bytes,
                smolv::kEncodeFlagStripDebugInfo
            );

            // Then compress further (zstd)
            // ...
        }

        shader.shader_stage = p_spirv[i].shader_stage;
    }

    return true;
}
```

**Storage format**:
1. GLSL source → glslang → SPIR-V 1.4
2. SPIR-V → smolv (lossy compression)
3. smolv → zstd (additional compression)
4. Store binary in shader container

---

## 5. Descriptor Binding Patterns — How Shadow Maps Work

### 5.1 Fixed Binding Pattern (No Dynamic Indexing)

Unlike UE5 (which can use `Texture2DArray<>` with index), Godot uses **fixed array bindings**:

**Godot shader** (conceptual):
```glsl
layout(set = 0, binding = 5) uniform sampler2D shadow_maps[8];  // Fixed!

void main() {
    vec4 shadow = texture(shadow_maps[0], uv);  // Index must be constant!
}
```

**Not possible in Godot** (would require VK_EXT_descriptor_indexing + nonuniformEXT):
```glsl
layout(set = 0, binding = 5) uniform sampler2D all_textures[];  // Variable array
vec4 val = texture(all_textures[dynamic_index], uv);  // Dynamic indexing
```

### 5.2 Pool Allocation for Multiple Sets

When a shader uses multiple sets, Godot allocates them **independently**:

```cpp
// Multiple sets can be bound separately
command_bind_render_uniform_sets(
    cmd_buffer,
    {set0_uniform_set, set1_uniform_set, set2_uniform_set},
    shader,
    0,  // first_set_index
    3   // set_count
);
```

Each set can have a different lifecycle:
- **Set 0** (globals): Updated once per frame
- **Set 1** (material params): Updated per draw call
- **Set 2** (per-instance): Updated per instance (rare)

---

## 6. Descriptor Limits and Capabilities

Godot queries these limits at runtime (from `limit_get`):

```cpp
case LIMIT_MAX_BOUND_UNIFORM_SETS:
    return limits.maxBoundDescriptorSets;  // Typically 8

case LIMIT_MAX_TEXTURES_PER_UNIFORM_SET:
    return limits.maxDescriptorSetSampledImages;  // Typically 16-1000K

case LIMIT_MAX_TEXTURES_PER_SHADER_STAGE:
    return limits.maxPerStageDescriptorSampledImages;  // Typically 16

case LIMIT_MAX_SAMPLERS_PER_UNIFORM_SET:
    return limits.maxDescriptorSetSamplers;

case LIMIT_MAX_STORAGE_BUFFERS_PER_UNIFORM_SET:
    return limits.maxDescriptorSetStorageBuffers;
```

**Result**: Godot shaders are limited by:
1. **Per-set limits** (typically 16 textures/stage, 8 UBOs/stage)
2. **Per-shader limits** (multiple sets accumulate)
3. **Per-device limits** (maxBoundDescriptorSets = 8)

---

## 7. Adreno Workaround (Mobile)

```cpp
// rendering_device_driver_vulkan.h, line 551
bool adreno_5xx_empty_descriptor_set_layout_workaround = false;

// rendering_device_driver_vulkan.cpp, ~line 4160
if (adreno_5xx_empty_descriptor_set_layout_workaround &&
    layout_create_info.bindingCount == 0) {
    layout_create_info.bindingCount = 1;
    layout_create_info.pBindings = &placeholder_binding;
}
```

**Mobile quirk**: Adreno 5xx GPUs fail if a descriptor set layout has zero bindings. Godot adds a placeholder.

---

## 8. UzEngine Adaptation — Lessons Learned

### 8.1 Key Insight: Bindless is NOT Required

Godot proves that a **modern AAA renderer** (full PBR, shadows, particles, UI, etc.) does NOT require bindless descriptors. Instead:

✓ **Use fixed descriptor set layouts**
✓ **Pre-allocate array sizes** (e.g., `shadow_maps[8]`, `lights[64]`)
✓ **Update descriptor sets** per frame (not ultra-cheap, but manageable)
✓ **Use linear pools** for per-frame allocation (fast reset)

### 8.2 UzEngine Opportunities

**Adopt Godot's strategy for Phase 1**:

1. **Define per-set array sizes** in shader specs:
   ```cpp
   // From docs/specs/rendering/shader-library.md
   // Max textures per material set: 16
   // Max point lights: 256
   // Max cascades per directional: 4
   ```

2. **Allocate descriptor sets per draw call or material**:
   ```cpp
   struct MaterialDescriptorSet {
       RhiDescriptorSet vk_set;
       uint32_t last_frame_updated;
   };
   ```

3. **Use linear pools for temp allocations**:
   ```cpp
   RhiDescriptorSet temp_shadow_set =
       rhi->descriptor_set_create_linear(shadow_layout, frame_index);
   // Auto-freed at end of frame
   ```

4. **Avoid true bindless until Phase 3+**:
   - VK_EXT_descriptor_indexing adds complexity
   - Not available on older mobile
   - Godot ships without it — proven unnecessary

### 8.3 Shader Compilation for UzEngine

**Current status** (from MEMORY.md):
- UzEngine uses **glslang** (same as Godot) ✓
- Target **SPIR-V 1.4** ✓
- Support **multiple descriptor sets** ✓

**What UzEngine SHOULD do**:
1. **Reuse Godot's descriptor layout pattern** — simple, works
2. **Add constraint checking in shader compiler**:
   ```cpp
   // Validate at compile time:
   if (texture_count_per_set > 16) {
       error("Shader uses {} textures in set {}, max 16",
             texture_count, set_index);
   }
   ```
3. **Document binding limits in shader spec**:
   ```
   - Per-set textures: 16 (AMD spec)
   - Per-stage textures: 16 (GPU spec)
   - Multiple sets: up to 8
   ```

---

## 9. Comparison to UE5

| Aspect | Godot | UE5 |
|--------|-------|-----|
| **Bindless model** | Fixed descriptor arrays (no extension) | Optional: VK_EXT_descriptor_indexing (Phase 2+) |
| **Texture indexing** | Static array indices only | Dynamic via nonuniformEXT |
| **Descriptor pools** | Per-type hash map + linear pools | Per-pool-type preallocated |
| **Shader compiler** | glslang → SPIR-V 1.4 | DXC → SPIR-V + DXIL hybrid |
| **Multi-set support** | Yes (0, 1, 2+) | Yes (0, 1, 2+) |
| **Maximum sets** | 8 (Vulkan limit) | 8 (Vulkan limit) |

**UE5 bindless advantages** (if/when adopted):
- Fewer descriptor set updates per frame
- Larger texture pools without pre-allocation
- More flexible asset streaming

**Godot simplicity advantages**:
- Works on all mobile devices
- Simpler to debug
- Lower CPU overhead for descriptor management
- Proven sufficient for shipping games

---

## 10. Files and Locations

**Key files for reference**:
- `/e/godot/drivers/vulkan/rendering_device_driver_vulkan.h` — Header (850 lines)
- `/e/godot/drivers/vulkan/rendering_device_driver_vulkan.cpp` — Implementation (7418 lines)
  - Lines ~4115-4180: Layout creation
  - Lines ~4574-4700: Uniform set creation
  - Lines ~4400-4550: Descriptor pool management
- `/e/godot/drivers/vulkan/rendering_shader_container_vulkan.h` — Shader container
- `/e/godot/servers/rendering/rendering_device_driver.h` — Base RDD class
- `/e/godot/servers/rendering/rendering_device_commons.h` — UniformType enum
- `/e/godot/servers/rendering/renderer_rd/shaders/canvas_uniforms_inc.glsl` — Multi-set example
- `/e/godot/modules/glslang/shader_compile.h` — Shader compilation interface

---

## 11. Conclusion

**Godot's approach is pragmatic and production-proven:**
- ✓ No bindless extensions required
- ✓ Simple descriptor pool management
- ✓ Multiple sets enable flexible per-draw updates
- ✓ Works on all Vulkan devices (desktop + mobile)
- ✓ Sufficient for AAA-quality rendering

**For UzEngine**, this validates the architecture decision to **NOT require bindless in Phase 1**. Phase 1 can ship with Godot's fixed-array model, and Phase 3 can optionally add VK_EXT_descriptor_indexing if texture streaming analysis shows it's beneficial.

