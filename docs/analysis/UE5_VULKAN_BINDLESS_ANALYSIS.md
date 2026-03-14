# UE5 Vulkan Bindless Resources Analysis

**Target**: UzEngine C/C++ codebase
**Source**: UE5.6 Full Source Analysis (`W:\UE_5.6_Source\Engine\Source\Runtime\VulkanRHI`)
**Date**: 2026-03-14

---

## 1. Pattern Name

**Descriptor Buffer Bindless Architecture** — Vulkan equivalent to DirectX12's ResourceDescriptorHeap, but using VK_EXT_descriptor_buffer for GPU-accessible descriptor storage.

---

## 2. UE5 Implementation

### 2.1 Extension Requirements

UE5 requires three Vulkan extensions working in concert:

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\VulkanRHI\Private\VulkanExtensions.cpp` (lines 1-50)

```cpp
// VK_EXT_descriptor_buffer — the core bindless mechanism
class FVulkanEXTDescriptorBuffer : public FVulkanDeviceExtension {
    // Requires: VkPhysicalDeviceDescriptorBufferPropertiesEXT
    // Reports descriptor sizes per type
    // Flags: allowSamplerImageViewPostSubmitCreation
    //        maxDescriptorBufferBindings, maxSamplerDescriptorBufferBindings,
    //        maxResourceDescriptorBufferBindings
};

// VK_EXT_descriptor_indexing — capability foundation
class FVulkanEXTDescriptorIndexingExtension : public FVulkanDeviceExtension {
    // Requirements (ALL must be VK_TRUE):
    // - runtimeDescriptorArray: GPU-side indexing into arrays
    // - descriptorBindingPartiallyBound: sparse descriptor arrays
    // - descriptorBindingUpdateUnusedWhilePending: update in-flight descriptors
    // - descriptorBindingVariableDescriptorCount: variable-size arrays
};

// VK_KHR_buffer_device_address — GPU addresses for descriptor buffers
// (implicitly required by VK_EXT_descriptor_buffer)
```

**Verification** (`VulkanDescriptorSets.cpp:197-265`):

```cpp
bool FVulkanBindlessDescriptorManager::VerifySupport(FVulkanDevice& Device) {
    const bool bMeetsExtensionsRequirements =
        OptionalDeviceExtensions.HasEXTDescriptorIndexing &&
        OptionalDeviceExtensions.HasBufferDeviceAddress &&
        OptionalDeviceExtensions.HasEXTDescriptorBuffer;

    const bool bMeetsPropertiesRequirements =
        (GpuProps.limits.maxBoundDescriptorSets >= VulkanBindless::MaxNumSets) &&
        (DescriptorBufferProperties.maxDescriptorBufferBindings >= VulkanBindless::MaxNumSets) &&
        (DescriptorBufferProperties.maxResourceDescriptorBufferBindings >= VulkanBindless::NumBindlessSets) &&
        (DescriptorBufferProperties.maxSamplerDescriptorBufferBindings >= 1);
    return bMeetsExtensionsRequirements && bMeetsPropertiesRequirements;
}
```

### 2.2 Descriptor Set Organization

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\VulkanRHI\Public\VulkanCommon.h` (lines 102-141)

UE5 organizes descriptors into **8-9 descriptor sets** (one per descriptor type), or **6 sets** on platforms with `maxBoundDescriptorSets < 9` using mutable descriptors:

```cpp
namespace VulkanBindless {
    enum EDescriptorSets {
        BindlessSamplerSet = 0,

        // Or merged with mutable descriptors if platform limits
        BindlessStorageBufferSet,        // = 1 or 1 (merged)
        BindlessUniformBufferSet,        // = 2 or 1 (merged)
        BindlessStorageImageSet,         // = 3 or 2 (merged)
        BindlessSampledImageSet,         // = 4 or 2 (merged)
        BindlessStorageTexelBufferSet,   // = 5 or 3 (merged)
        BindlessUniformTexelBufferSet,   // = 6 or 3 (merged)
        BindlessAccelerationStructureSet,// = 7

        NumBindlessSets,                 // = 8 or 6
        BindlessSingleUseUniformBufferSet = NumBindlessSets,  // = 8 or 6
        MaxNumSets = NumBindlessSets + 1 // = 9 or 7
    };
};
```

**Key insight**: Each descriptor type gets its own set to avoid type confusion and simplify descriptor buffer packing.

### 2.3 Descriptor Buffer Layout Creation

**File**: `VulkanDescriptorSets.cpp:326-577` (Init function)

For each descriptor type, a VkDescriptorSetLayout is created with:

```cpp
// Single binding with all descriptors as an array
VkDescriptorSetLayoutBinding DescriptorSetLayoutBinding;
DescriptorSetLayoutBinding.binding = 0;
DescriptorSetLayoutBinding.descriptorCount = State.MaxDescriptorCount;  // e.g., 256K
DescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_ALL;
DescriptorSetLayoutBinding.descriptorType =
    (State.DescriptorTypes.Num() == 1)
        ? State.DescriptorTypes[0]                    // Single type
        : VK_DESCRIPTOR_TYPE_MUTABLE_EXT;             // Multiple types (mutable)

// For mutable types: specify allowed types
VkMutableDescriptorTypeListEXT MutableDescriptorTypeList;
MutableDescriptorTypeList.descriptorTypeCount = State.DescriptorTypes.Num();
MutableDescriptorTypeList.pDescriptorTypes = State.DescriptorTypes.GetData();

VkDescriptorSetLayoutCreateInfo DescriptorSetLayoutCreateInfo;
DescriptorSetLayoutCreateInfo.flags =
    VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;  // KEY: enables descriptor buffer
DescriptorSetLayoutCreateInfo.pBindings = &DescriptorSetLayoutBinding;
DescriptorSetLayoutCreateInfo.bindingCount = 1;
DescriptorSetLayoutCreateInfo.pNext = &DescriptorSetLayoutBindingFlagsCreateInfo;
```

**Implicit binding flags** (from VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT):
- VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
- VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
- VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT

### 2.4 Descriptor Buffer Allocation

**File**: `VulkanDescriptorSets.cpp:443-517` (CreateDescriptorBuffer lambda)

```cpp
// Allocate GPU buffer for all descriptors of a type
const uint32 DescriptorBufferSize = State.DescriptorSize * State.MaxDescriptorCount;

VkBufferUsageFlags BufferUsageFlags =
    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
    VK_BUFFER_USAGE_TRANSFER_DST_BIT |
    (IsSamplerSet ? VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT
                  : VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT);

InOutState.BufferHandle = Device.CreateBuffer(DescriptorBufferSize, BufferUsageFlags);

// Map for CPU writes
VkMemoryAllocateFlagsInfo FlagsInfo;
FlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
VERIFYVULKANRESULT(VulkanRHI::vkAllocateMemory(..., &FlagsInfo, ...));
VERIFYVULKANRESULT(VulkanRHI::vkMapMemory(..., (void**)&InOutState.MappedPointer));

// Get GPU address for shader binding
VkBufferDeviceAddressInfo AddressInfo;
AddressInfo.buffer = InOutState.BufferHandle;
OutBufferBindingInfo.address = VulkanRHI::vkGetBufferDeviceAddressKHR(..., &AddressInfo);
OutBufferBindingInfo.usage = BufferUsageFlags;
```

**Descriptor sizes are device-specific** (from `VkPhysicalDeviceDescriptorBufferPropertiesEXT`):

```cpp
uint32 GetDescriptorTypeSize(FVulkanDevice& Device, VkDescriptorType DescriptorType) {
    const VkPhysicalDeviceDescriptorBufferPropertiesEXT& Props =
        Device.GetOptionalExtensionProperties().DescriptorBufferProps;

    switch (DescriptorType) {
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        return bRobustBufferAccess ? Props.robustSamplerDescriptorSize
                                   : Props.samplerDescriptorSize;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        return Props.sampledImageDescriptorSize;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        return Props.storageImageDescriptorSize;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        return bRobustBufferAccess ? Props.robustUniformBufferDescriptorSize
                                   : Props.uniformBufferDescriptorSize;
    // ... etc for all types
    }
}
```

### 2.5 Descriptor Updates

**File**: `VulkanDescriptorSets.cpp:691-800` (UpdateDescriptor variants)

Instead of `vkUpdateDescriptorSets`, use `vkGetDescriptorEXT` to write inline descriptors:

```cpp
void UpdateDescriptor(FRHIDescriptorHandle Handle, VkDescriptorType Type,
                      VkDescriptorDataEXT DescriptorData, bool bImmediateUpdate) {
    BindlessSetState& State = BindlessSetStates[SetIndex];
    const uint32 ByteOffset = Handle.GetIndex() * State.DescriptorSize;

    VkDescriptorGetInfoEXT Info;
    Info.type = DescriptorType;
    Info.data = DescriptorData;  // Union: pSampler, pSampledImage, pBuffer, etc.

    // Write descriptor as inline data to mapped buffer
    vkGetDescriptorEXT(Device.GetInstanceHandle(), &Info,
                       State.DescriptorSize, &State.MappedPointer[ByteOffset]);

    if (bImmediateUpdate) {
        FMemory::Memcpy(&State.MappedPointer[ByteOffset], ..., State.DescriptorSize);
    } else {
        // Use staging buffer + copy for deferred updates
    }
}

// Update image descriptor
void UpdateImage(FRHIDescriptorHandle Handle, VkDescriptorType Type,
                 VkImageView ImageView, bool bIsDepthStencil) {
    VkDescriptorImageInfo DescriptorImageInfo;
    DescriptorImageInfo.sampler = VK_NULL_HANDLE;
    DescriptorImageInfo.imageView = ImageView;
    DescriptorImageInfo.imageLayout =
        (Type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) ? VK_IMAGE_LAYOUT_GENERAL
        : (bIsDepthStencil ? VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL
                           : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    VkDescriptorDataEXT DescriptorData;
    DescriptorData.pSampledImage = &DescriptorImageInfo;
    UpdateDescriptor(Handle, Type, DescriptorData, bImmediateUpdate);
}

// Update buffer descriptor (uses device address)
void UpdateBuffer(FRHIDescriptorHandle Handle, VkDescriptorType Type,
                  VkBuffer Buffer, VkDeviceSize Offset, VkDeviceSize Size) {
    VkBufferDeviceAddressInfo BufferInfo;
    BufferInfo.buffer = Buffer;
    const VkDeviceAddress BufferAddress =
        vkGetBufferDeviceAddressKHR(Device.GetInstanceHandle(), &BufferInfo);

    UpdateBuffer(Handle, Type, BufferAddress + Offset, Size, bImmediateUpdate);
}
```

### 2.6 Shader Binding at Draw Time

**File**: `VulkanDescriptorSets.cpp:579-599` (BindDescriptorBuffers)

```cpp
void BindDescriptorBuffers(VkCommandBuffer CommandBuffer, VkPipelineStageFlags SupportedStages) {
    // Bind all descriptor buffers in one call
    vkCmdBindDescriptorBuffersEXT(CommandBuffer, VulkanBindless::NumBindlessSets,
                                   BufferBindingInfo);

    // Set offsets (all zero for standard bindless)
    VkDeviceSize BufferOffsets[VulkanBindless::NumBindlessSets];
    FMemory::Memzero(BufferOffsets);

    // Set descriptor buffer offsets per shader stage
    if (SupportedStages & VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT) {
        vkCmdSetDescriptorBufferOffsetsEXT(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            BindlessPipelineLayout, 0,
                                            VulkanBindless::NumBindlessSets,
                                            BufferIndices, BufferOffsets);
    }
    // ... repeat for compute, ray tracing, etc.
}
```

### 2.7 Shader Compilation & Resource Tracking

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\VulkanRHI\Public\VulkanShaderResources.h` (lines 19-137)

Shader metadata (FVulkanShaderHeader) tracks all bindings:

```cpp
struct FVulkanShaderHeader {
    // Binding array indexed by binding slot
    struct FBindingInfo {
        uint32 DescriptorType;  // VkDescriptorType value
        FString DebugName;       // Optional: "ShadowDepthTexture", etc.
    };
    TArray<FBindingInfo> Bindings;

    // For uniform buffers: additional metadata
    struct FUniformBufferInfo {
        uint32 LayoutHash;
        uint8 bHasResources;
        uint8 BindlessCBIndex;   // Index in bindless sampled image set (if applicable)
    };
    TArray<FUniformBufferInfo> UniformBufferInfos;

    // Packed globals size in bytes (uniform buffer at binding 0)
    uint32 PackedGlobalsSize;

    // Number of uniform buffers requiring bindings
    uint32 NumBoundUniformBuffers;
};
```

**Shader code compilation** (from `VulkanShaders.cpp`):
- HLSL shaders compiled via **DXC** (DirectX Shader Compiler)
- DXC targets **SPIR-V** output with `-spirv` flag
- SPIR-V preserved in binary form (`FVulkanShader::FSpirvContainer`)
- No ResourceDescriptorHeap indexing in shader code; uses standard `Texture2D<>` + `register(t0)` syntax
- Descriptor binding resolved at compile time via shader reflection

---

## 3. Problem Solved

### 3.1 DX12 ResourceDescriptorHeap → Vulkan Gap

**DX12 approach**:
```cpp
// DX12: descriptor heap with integer indexing
uint32 textureIndex = GetTextureIndex(...);
uint32 descriptorOffset = textureIndex * descriptorSize;
ID3D12DescriptorHeap* heap = cmd->GetDescriptorHeap();
D3D12_GPU_DESCRIPTOR_HANDLE handle = heap->GetGPUDescriptorHandleForHeapStart();
// Shader accesses as: texture_array[textureIndex]
```

**Vulkan limitations**:
- No concept of "descriptor heap" with opaque handles
- Descriptors are first-class GPU resources, not indirection pointers
- Descriptor Sets have hard limits (often 4-8 sets max)
- No direct integer indexing into a flat array

**UE5 solution**:
1. **Descriptor buffer per type** — One contiguous GPU buffer per descriptor type (samplers, sampled images, storage buffers, etc.)
2. **Inline descriptor storage** — Descriptors stored as inline data structures, not opaque handles
3. **Virtual address binding** — Pass GPU buffer addresses to shaders via descriptor set binding + device address
4. **Set-per-type organization** — Reduces descriptor set count to 8-9 (well within limits)
5. **Mutable descriptors** — Fallback on platforms with tighter limits; packs multiple types into fewer sets

### 3.2 Sparse Descriptor Allocation

**Problem**: Textures, buffers, and samplers are allocated dynamically at runtime. Not all 256K potential samplers are used.

**Solution**: `FVulkanBindlessDescriptorManager` maintains:
- Free list per descriptor type
- Peak usage tracking (stats)
- Reuse of freed indices
- No pre-allocation waste

### 3.3 Cross-Frame Descriptor Lifetime

**Problem**: Descriptors must remain valid across multiple frames; GPU is reading while CPU updates.

**Solution**:
- `descriptorBindingUpdateUnusedWhilePending` — allows updates while in-flight
- `descriptorBindingPartiallyBound` — unused slots don't require valid descriptors
- Staging buffer copies for non-immediate updates (avoids data races)

---

## 4. UzEngine Adaptation

### 4.1 RHI Abstraction Layer Design

Add to `src/engine/renderer/rhi/rhi.h` (or new file `rhi_bindless.h`):

```cpp
/// Bindless descriptor types (Vulkan: one set per type)
enum class RhiDescriptorType {
    Sampler,
    SampledImage,
    StorageImage,
    UniformBuffer,
    StorageBuffer,
    UniformTexelBuffer,
    StorageTexelBuffer,
    AccelerationStructure,
    Count
};

/// Opaque handle to a bindless descriptor slot
struct RhiDescriptorHandle {
    uint16 set_index;     // RhiDescriptorType
    uint32 slot_index;    // Index within descriptor buffer
};

/// Descriptor buffer configuration (device-dependent)
struct RhiDescriptorBufferConfig {
    uint32 sampler_descriptor_size;
    uint32 sampled_image_descriptor_size;
    uint32 storage_image_descriptor_size;
    uint32 uniform_buffer_descriptor_size;
    uint32 storage_buffer_descriptor_size;
    uint64 max_descriptor_buffer_size;
    // ... etc
};

/// Bindless manager interface
struct RhiBindlessManager {
    virtual RhiDescriptorHandle reserve_descriptor(RhiDescriptorType type) = 0;

    virtual void update_sampler(RhiDescriptorHandle handle, const RhiSampler& sampler) = 0;
    virtual void update_image(RhiDescriptorHandle handle, const RhiTexture& texture,
                              RhiImageLayout layout) = 0;
    virtual void update_buffer(RhiDescriptorHandle handle, const RhiBuffer& buffer,
                               uint64 offset, uint64 size) = 0;

    virtual void bind_descriptor_buffers(RhiCommandBuffer* cmd,
                                         RhiPipelineBindPoint bind_point) = 0;

    virtual RhiDescriptorBufferConfig get_config() const = 0;
};
```

### 4.2 Vulkan Backend Implementation

**File**: `src/engine/renderer/vulkan/vulkan_bindless.h/cpp`

Structure mirrors UE5's `FVulkanBindlessDescriptorManager`:

```cpp
class VulkanBindlessManager : public RhiBindlessManager {
private:
    struct DescriptorBufferState {
        VkBuffer buffer;
        VkDeviceMemory memory;
        uint8* mapped_pointer;
        uint32 descriptor_size;
        uint32 max_count;

        // Free list management
        std::vector<uint32> free_indices;
        uint32 peak_used = 0;
    };

    DescriptorBufferState sets[static_cast<int>(RhiDescriptorType::Count)];

    VkDescriptorSetLayout descriptor_set_layouts[static_cast<int>(RhiDescriptorType::Count)];
    VkPipelineLayout pipeline_layout;
};
```

### 4.3 Shader Code Generation

**Key difference from UE5**: UzEngine uses **Zig** for game code, not C++.

For shaders:
- Compile HLSL/GLSL via **DXC** (SPIR-V output)
- Store SPIR-V bytecode (like UE5 does)
- Bind at load time using descriptor buffer GPU addresses
- **No changes to shader syntax** — standard `Texture2D`, `SamplerState` declarations work as-is

Example HLSL (unchanged from DX12):

```hlsl
// In shader: standard texture array pattern
Texture2D textures[256] : register(t0, space0);
SamplerState samplers[256] : register(s0, space0);

float4 ps_main(float2 uv : TEXCOORD0, uint texIdx : TEXCOORD1) : SV_Target {
    return textures[texIdx].Sample(samplers[0], uv);
}
```

The DXC compiler + Vulkan backend automatically map this to descriptor buffer access. **Shaders are portable across DX12 and Vulkan without modification.**

### 4.4 Configuration & Tuning

Add to engine config (`engine.toml` or equivalent):

```toml
[renderer.vulkan.bindless]
enabled = true
max_sampler_descriptors = 2048
max_sampled_image_descriptors = 262144  # 256K
max_storage_image_descriptors = 65536   # 64K
max_uniform_buffer_descriptors = 32768  # 32K
max_storage_buffer_descriptors = 65536  # 64K
```

### 4.5 Memory Layout

Each descriptor buffer layout:

```
Descriptor Buffer (e.g., Sampled Images):
┌──────────────────┐
│ Descriptor[0]    │ ← 40 bytes (typical sampledImageDescriptorSize)
│ Descriptor[1]    │
│ Descriptor[2]    │
│      ...         │
│ Descriptor[N]    │
└──────────────────┘
Total: N * descriptor_size bytes
```

Access from shader:
```cpp
VkDescriptorAddressInfoEXT imgDescriptor;
imgDescriptor.address = descriptor_buffer_base_address + (texIdx * descriptor_size);
// GPU loads descriptor inline, returns VkImageView equivalent
```

### 4.6 Deferred Update Strategy

For CPU→GPU descriptor updates not in render pass:

```cpp
void update_image_deferred(RhiDescriptorHandle handle, const RhiTexture& texture) {
    // 1. Build descriptor data in staging buffer
    VkDescriptorImageInfo img_info = {
        .imageView = texture.vk_image_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };

    // 2. Call vkGetDescriptorEXT with staging buffer
    vkGetDescriptorEXT(device, &info, descriptor_size, staging_ptr);

    // 3. Copy from staging to descriptor buffer
    cmd->copy_buffer(staging_buffer, descriptor_buffer,
                     descriptor_offset, descriptor_size);

    // 4. Barrier: ensure descriptor buffer read-before-write
}
```

---

## 5. Anti-patterns Found in UzEngine

### 5.1 Legacy Pattern: Direct DX12 in Renderer Code

**Current violation** (if any):
```cpp
// WRONG: DX12 directly in renderer, bypassing RHI
ID3D12DescriptorHeap* heap = device->GetDescriptorHeap();  // ← Violates RHI boundary
```

**Correction**: All descriptor management goes through RhiBindlessManager interface.

### 5.2 Missing Descriptor Lifecycle Management

**Anti-pattern**: Assuming infinite descriptor slots.

```cpp
// WRONG: No free list
uint32 next_free_slot = 0;
handle = allocate_descriptor(++next_free_slot);  // ← Unbounded growth
```

**Correction**: Implement free list + peak tracking:

```cpp
uint32 slot = get_free_resource_index(descriptor_set_state);
descriptor_set_state.peak_descriptor_count = max(peak, slot + 1);
```

### 5.3 Missing Cross-Frame Synchronization

**Anti-pattern**: Updating descriptor without checking pending GPU access.

```cpp
// WRONG: Immediate update while descriptor might be in-flight
descriptor_buffer.data[offset] = new_descriptor;
```

**Correction**: UE5's approach with staging buffers + barriers (Section 4.6).

### 5.4 Hardcoded Descriptor Set Counts

**Anti-pattern**:
```cpp
// WRONG: Assumes 4 descriptor sets
VkDescriptorSet sets[4];
vkAllocateDescriptorSets(..., sets);
```

**Correction**: Query device properties and adapt:

```cpp
uint32 max_sets = device_props.limits.maxBoundDescriptorSets;
if (max_sets < 9) {
    // Use mutable descriptors (reduce to 6 sets)
    use_mutable_descriptors = true;
}
```

### 5.5 No Descriptor Size Querying

**Anti-pattern**:
```cpp
// WRONG: Assumes descriptor size = 16 bytes
uint32 descriptor_size = 16;
uint32 buffer_size = 256K * descriptor_size;
```

**Correction**: Query from device:

```cpp
const VkPhysicalDeviceDescriptorBufferPropertiesEXT& props =
    device->GetOptionalExtensionProperties().DescriptorBufferProps;
uint32 descriptor_size = props.sampledImageDescriptorSize;  // Device-dependent!
```

---

## 6. Concrete Findings for UzEngine

### 6.1 Vulkan Features Required

**Feature Set (all required)**:
- `runtimeDescriptorArray` = VK_TRUE
- `descriptorBindingPartiallyBound` = VK_TRUE
- `descriptorBindingUpdateUnusedWhilePending` = VK_TRUE
- `descriptorBindingVariableDescriptorCount` = VK_TRUE

**Extensions (all required)**:
- `VK_EXT_descriptor_buffer`
- `VK_EXT_descriptor_indexing`
- `VK_KHR_buffer_device_address` (implicit)
- `VK_EXT_mutable_descriptor_type` (if maxBoundDescriptorSets < 9)

### 6.2 Descriptor Set Layout Template

```cpp
// Single descriptor set layout per type
VkDescriptorSetLayoutBinding binding = {
    .binding = 0,
    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  // or other type
    .descriptorCount = 256 * 1024,  // e.g., 256K for sampled images
    .stageFlags = VK_SHADER_STAGE_ALL,
};

VkDescriptorBindingFlagsEXT binding_flags = 0;  // Implicitly set by DESCRIPTOR_BUFFER_BIT_EXT

VkDescriptorSetLayoutCreateInfo layout_info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,  // KEY
    .bindingCount = 1,
    .pBindings = &binding,
};

vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &layout);
```

### 6.3 Descriptor Buffer Allocation

```cpp
// Compute buffer size from device properties
const uint32 descriptor_count = 256 * 1024;
const uint32 descriptor_size = device_props.sampledImageDescriptorSize;
const uint64 buffer_size = descriptor_count * descriptor_size;

// Allocate with correct usage flags
VkBufferCreateInfo buffer_info = {
    .size = buffer_size,
    .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
           | VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT
           | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
};

// Memory must be mappable for CPU updates
VkMemoryAllocateFlagsInfo mem_flags = {
    .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
};

// Get GPU address for shader binding
VkBufferDeviceAddressInfo addr_info = {
    .buffer = descriptor_buffer,
};
VkDeviceAddress gpu_address = vkGetBufferDeviceAddressKHR(device, &addr_info);
```

### 6.4 Descriptor Updates (GPU-Accessible)

```cpp
// For sampled image descriptor
VkDescriptorImageInfo img_info = {
    .imageView = texture.vk_image_view,
    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
};

VkDescriptorDataEXT descriptor_data = {
    .pSampledImage = &img_info,
};

VkDescriptorGetInfoEXT info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
    .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
    .data = descriptor_data,
};

uint8* slot = mapped_buffer + (slot_index * descriptor_size);
vkGetDescriptorEXT(device, &info, descriptor_size, slot);
```

### 6.5 Shader Binding at Draw Time

```cpp
// Bind all descriptor buffers
VkDescriptorBufferBindingInfoEXT binding_infos[8] = {
    { .address = sampler_buffer_gpu_address, ... },
    { .address = sampled_image_buffer_gpu_address, ... },
    // ... rest of descriptor buffers
};

vkCmdBindDescriptorBuffersEXT(cmd, 8, binding_infos);

// Set descriptor buffer offsets (all zero for single-array design)
uint32 buffer_indices[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
VkDeviceSize offsets[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout, 0, 8, buffer_indices, offsets);
```

### 6.6 Shader Compilation (No Changes Required)

HLSL shaders compile unchanged:

```hlsl
Texture2D textures[256] : register(t0, space0);
SamplerState samplers[4] : register(s0, space0);

float4 main(PixelInput pin) : SV_Target {
    return textures[pin.textureIdx].Sample(samplers[0], pin.uv);
}
```

DXC with `-spirv` flag outputs SPIR-V that accesses descriptors inline from the descriptor buffer at GPU runtime.

---

## 7. Summary

| Aspect | DX12 ResourceDescriptorHeap | Vulkan Descriptor Buffer | UzEngine Strategy |
|--------|---------------------------|------------------------|--------------------|
| **Type** | Opaque GPU resource | Mapped GPU buffer with inline descriptors | RhiBindlessManager abstraction |
| **Allocation** | Heap allocator | Free list per descriptor type | Query device → configure per type |
| **Updates** | `UpdateDescriptorHeaps()` | `vkGetDescriptorEXT()` to mapped buffer | Staging buffer + GPU copy (deferred) |
| **Indexing** | Integer offset into heap | Set + binding (Vulkan semantics) | RhiDescriptorHandle (set_index + slot) |
| **Capacity** | Device-dependent (4K-1M) | Device-dependent (256K typical) | Tunable via config + queries device |
| **Shader Access** | ResourceDescriptorHeap[idx] | Normal `Texture2D[idx]` (compiled to inline access) | Unchanged HLSL; DXC handles translation |
| **Cross-Frame** | Automatic | Requires barriers + deferred updates | Use RhiBindlessManager API |

---

## 8. File References

| File | Purpose | Key Lines |
|------|---------|-----------|
| `W:\UE_5.6_Source\Engine\Source\Runtime\VulkanRHI\Private\VulkanDescriptorSets.h` | Bindless manager class + descriptor set layouts | 849-924, 252-289 |
| `W:\UE_5.6_Source\Engine\Source\Runtime\VulkanRHI\Private\VulkanDescriptorSets.cpp` | Implementation: allocation, updates, binding | 12-96, 197-265, 326-577, 579-657, 691-800 |
| `W:\UE_5.6_Source\Engine\Source\Runtime\VulkanRHI\Private\VulkanExtensions.cpp` | Extension support verification | VK_EXT_descriptor_buffer, VK_EXT_descriptor_indexing classes |
| `W:\UE_5.6_Source\Engine\Source\Runtime\VulkanRHI\Public\VulkanCommon.h` | Bindless enum definitions | 102-141 |
| `W:\UE_5.6_Source\Engine\Source\Runtime\VulkanRHI\Public\VulkanShaderResources.h` | FVulkanShaderHeader (binding metadata) | 19-137 |
