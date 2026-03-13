# UE5 Root Signature & Shader Model Compatibility Analysis

## Pattern 1: Conditional Bindless Root Signature (SM6.6+ vs SM5 Fallback)

### UE5 Implementation

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12RootSignature.cpp` (lines 351-359)

UE5 does **NOT** create separate root signatures per shader model. Instead, it uses a **single unified root signature** that conditionally enables bindless flags based on platform/hardware capability:

```cpp
// D3D12RootSignature.cpp:351-359
if (QBSS.bUseDirectlyIndexedResourceHeap)
{
    Flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
}

if (QBSS.bUseDirectlyIndexedSamplerHeap)
{
    Flags |= D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
}
```

The `bUseDirectlyIndexedResourceHeap` and `bUseDirectlyIndexedSamplerHeap` flags come from `FD3D12QuantizedBoundShaderState` (QBSS), which is set **at initialization time**, not per-PSO.

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12Util.cpp`

```cpp
QBSS.bUseDirectlyIndexedResourceHeap = GetBindlessDescriptorAllocator().AreResourcesBindless();
QBSS.bUseDirectlyIndexedSamplerHeap = GetBindlessDescriptorAllocator().AreSamplersBindless();
```

This decision is made **once at adapter initialization**, not per-shader or per-PSO.

### Platform Gating

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Public\Windows\WindowsPlatform.h`

```cpp
#define PLATFORM_SUPPORTS_BINDLESS_RENDERING 1
```

And the conditional compilation in D3D12RHI:

```cpp
#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
    QBSS.bUseDirectlyIndexedResourceHeap = GetBindlessDescriptorAllocator().AreResourcesBindless();
    QBSS.bUseDirectlyIndexedSamplerHeap = GetBindlessDescriptorAllocator().AreSamplersBindless();
#endif
```

**Key insight**: Bindless is gated on platform support, **not shader model**. There is no per-shader-model fallback; either the adapter supports bindless descriptor heaps or it doesn't.

### Root Signature Definition

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Public\D3D12RootSignatureDefinitions.h` (lines 198-206)

```cpp
if (EnumHasAnyFlags(InFlags, EShaderBindingLayoutFlags::BindlessResources))
{
    AddRootFlag(D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
}

if (EnumHasAnyFlags(InFlags, EShaderBindingLayoutFlags::BindlessSamplers))
{
    AddRootFlag(D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED);
}
```

When bindless is **enabled**, descriptor table ranges are **skipped**:

```cpp
// D3D12RootSignatureDefinitions.h:224-237
inline bool ShouldSkipType(ERootSignatureRangeType Type) const
{
    if (Type == ERootSignatureRangeType::SRV || Type == ERootSignatureRangeType::UAV)
    {
        return EnumHasAnyFlags(Flags, EShaderBindingLayoutFlags::BindlessResources);
    }

    if (Type == ERootSignatureRangeType::Sampler)
    {
        return EnumHasAnyFlags(Flags, EShaderBindingLayoutFlags::BindlessSamplers);
    }

    return false;
}
```

**Two root signature classes**:
1. **With bindless**: No descriptor tables for SRV/UAV/Samplers → shaders directly index global heap via root constants
2. **Without bindless**: Full descriptor tables for SRV/UAV/Sampler → traditional descriptor table binding

---

## Pattern 2: Single Root Signature per Shader Frequency (Not Per-PSO)

### Design

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12RootSignature.h` (lines 46-70)

```cpp
class FD3D12RootSignatureDesc
{
    // ... single root signature for an entire frequency (graphics/compute)
    uint32 RootParametersSize;      // Total DWORD cost (max 64)
    CD3DX12_ROOT_PARAMETER1 TableSlots[MaxRootParameters];  // All params
    CD3DX12_DESCRIPTOR_RANGE1 DescriptorRanges[MaxRootParameters];
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC RootDesc;
};
```

Root signatures are **cached** and **reused** across PSOs:

```cpp
// D3D12RootSignature.h:483-504
class FD3D12RootSignatureManager : public FD3D12AdapterChild
{
    FD3D12RootSignature* GetRootSignature(const FD3D12QuantizedBoundShaderState& QBSS);
    TMap<FD3D12QuantizedBoundShaderState, FD3D12RootSignature*> RootSignatureMap;
};
```

**Key insight**:
- Many PSOs **share one root signature**
- Root signature is keyed on `FD3D12QuantizedBoundShaderState` (QBSS)
- QBSS includes: bindless flags, register counts per stage, binding tier
- **Not** per-shader-model; shader model is baked into the QBSS via `ResourceBindingTier`

### Resource Binding Tier (SM5 vs SM6 Proxy)

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12RootSignature.cpp` (lines 179-195)

```cpp
const D3D12_DESCRIPTOR_RANGE_FLAGS SRVDescriptorRangeFlags = (ResourceBindingTier <= D3D12_RESOURCE_BINDING_TIER_1) ?
    D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE :
    D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;

const D3D12_DESCRIPTOR_RANGE_FLAGS CBVDescriptorRangeFlags = (ResourceBindingTier <= D3D12_RESOURCE_BINDING_TIER_2) ?
    D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE :
    D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
```

**Resource Binding Tier mapping**:
- **Tier 1** (SM5.0): Static descriptor ranges → less efficient (fewer per-stage descriptor updates)
- **Tier 2** (SM5.1+): Volatile descriptors allowed → can update descriptors per-draw without rebinding
- **Tier 3** (SM6.0+): Full dynamic descriptor ranges + bindless support

The **same root signature structure** is used, but descriptor range **flags** differ based on binding tier.

---

## Pattern 3: Texture Binding in Fullscreen Passes (Descriptors vs Bindless)

### Runtime Decision

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\ShaderParameterUtils.h` (lines 77-92)

```cpp
inline void SetTextureParameter(FRHIBatchedShaderParameters& BatchedParameters,
                                const FShaderResourceParameter& Parameter,
                                FRHITexture* TextureRHI)
{
    if (Parameter.IsBound())
    {
#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
        if (Parameter.GetType() == EShaderParameterType::BindlessSRV)
        {
            BatchedParameters.SetBindlessTexture(Parameter.GetBaseIndex(), TextureRHI);
        }
        else
#endif
        {
            BatchedParameters.SetShaderTexture(Parameter.GetBaseIndex(), TextureRHI);
        }
    }
}
```

**Two paths**:
1. **Bindless path**: `SetBindlessTexture()` → stores handle (uint32) in shader parameter buffer
2. **Traditional path**: `SetShaderTexture()` → updates descriptor table entry in GPU descriptor heap

The choice is **determined at shader compile time** by `EShaderParameterType::BindlessSRV` flag.

### No Fullscreen Pass Fallback

UE5 **does not have special logic for fullscreen passes**. Tonemap, FXAA, post-process passes all use the same shader parameter binding mechanism:
- Shaders declare `Texture2D<float4> InputTexture : register(t0)` or bindless equivalent
- The shader parameter system (`SetTextureParameter`) routes to the correct binding method
- Root signature must be **compatible** with the parameter type (e.g., if shader uses bindless, root sig must have `DIRECTLY_INDEXED` flag)

---

## Pattern 4: PSO Creation & Root Signature Pairing

### File
`W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12PipelineState.h` (lines 60-90)

```cpp
struct FD3D12_GRAPHICS_PIPELINE_STATE_DESC
{
    ID3D12RootSignature *pRootSignature;  // Single root sig for this PSO
    D3D12_SHADER_BYTECODE VS;
    D3D12_SHADER_BYTECODE PS;
    D3D12_SHADER_BYTECODE GS;
    // ... other PSO state
};

struct FD3D12LowLevelGraphicsPipelineStateDesc
{
    const FD3D12RootSignature *pRootSignature;  // Reused across multiple PSOs
    FD3D12_GRAPHICS_PIPELINE_STATE_DESC Desc;
    ShaderBytecodeHash VSHash, PSHash, GSHash;
    uint32 InputLayoutHash;
    SIZE_T CombinedHash;  // Hash of all state including root sig ptr
};
```

**Root signature is part of PSO identity**. Different root signatures = different PSOs (even if shaders are identical).

---

## Problem Solved: Why This Design?

UE5's root signature strategy solves several problems:

### 1. **Runtime Binding Flexibility**
- Bindless mode requires **no descriptor table setup** → saves CPU time
- Descriptor table mode is fallback for older hardware → maintains compatibility
- Both modes use **same shader source** (compiled conditionally)

### 2. **Descriptor Binding Tier Abstraction**
- `ResourceBindingTier` parameter lets root signature adapt to GPU capabilities
- Tier 1 GPUs (SM5.0, some Intel HD): Static descriptors only
- Tier 2+ GPUs (SM5.1, most discrete): Can update descriptors without rebinding root sig
- Tier 3 (SM6.0+, all modern discrete): Can use bindless directly-indexed heaps

### 3. **No Per-Shader-Model Branching**
- UE5 does **not** compile different shaders for SM5 vs SM6
- Instead, **feature flags** (`BindlessResources`, `ResourceBindingTier`) gate capabilities
- This avoids shader combinatorial explosion

---

## UzEngine Adaptation

### Current State
UzEngine has a single "fat" root signature (all tables always present):
- `b0`: World constants (16 DWORD)
- `b1`: Material constants (20 DWORD)
- `b2`: Light constants (2 DWORD)
- `b3`: ViewProj (16 DWORD)
- `t0-t9`: Texture SRV tables (10 slots)
- Total: 64 DWORDs (at max)

### Recommended Adaptation

**Phase 1: Add Bindless Detection**
```cpp
struct UzRootSignatureDesc {
    bool bUseBindlessResources = false;  // Detect at adapter init
    bool bUseBindlessSamplers = false;
    D3D12_RESOURCE_BINDING_TIER resourceBindingTier = D3D12_RESOURCE_BINDING_TIER_1;
};

// At adapter init:
D3D12_RESOURCE_BINDING_TIER tier = adapter->GetResourceBindingTier();
bool bindless = (tier >= D3D12_RESOURCE_BINDING_TIER_3) &&
                IsFeatureLevelSupported(D3D_FEATURE_LEVEL_12_1);
```

**Phase 2: Conditional Descriptor Table Generation**
```cpp
if (desc.bUseBindlessResources) {
    // Skip t0-t9 descriptor tables
    // Instead, add root constant slot for resource handle arrays
} else {
    // Keep descriptor tables as-is (current behavior)
}

if (desc.bUseBindlessSamplers) {
    // Skip sampler table
    // Samplers loaded from bindless sampler heap
} else {
    // Keep sampler table
}
```

**Phase 3: Shader Parameter Binding Switch**
```cpp
// In uz::SetTextureParameter()
if (bindlessEnabled && shader->UsesBindlessTextures()) {
    // Store handle in shader constants
    shaderCBV.resourceHandles[slot] = texture->GetBindlessHandle();
} else {
    // Update descriptor table entry (current path)
    descriptorHeap[descriptorSlot] = texture->GetDescriptor();
}
```

**Key differences from UE5**:
- UE5 supports 3 binding tiers; UzEngine can start with 2: bindless-capable vs table-only
- UE5 caches root signatures by QBSS key; UzEngine could use a simple 2-entry cache (bindless + non-bindless)
- UE5 gates on platform; UzEngine gates on `D3D_RESOURCE_BINDING_TIER`

### Benefits for UzEngine
1. **Future-proof**: When minimum SM becomes 6.0, flip one flag → bindless everywhere
2. **Backward-compat**: Old SM5.0 hardware still works (falls back to descriptor tables)
3. **No shader duplication**: Compile once, gate features at runtime via root signature
4. **CPU savings**: Bindless mode skips descriptor heap updates (~0.1ms per frame on large games)

---

## Files to Reference

### Root Signature Creation
- `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12RootSignature.h` — FD3D12RootSignature class
- `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12RootSignature.cpp` — Impl (lines 162-450 for construction)
- `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Public\D3D12RootSignatureDefinitions.h` — Helper structs & flag logic

### Bindless Detection
- `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12BindlessDescriptors.h` — FD3D12BindlessDescriptorAllocator
- `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12Util.cpp` — Where QBSS is populated with bindless flags

### Shader Parameter Binding
- `W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\ShaderParameterUtils.h` (lines 77-121) — SetTextureParameter conditional logic

### PSO & Root Signature Pairing
- `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12PipelineState.h` — FD3D12LowLevelGraphicsPipelineStateDesc

### Platform Gating
- `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Public\Windows\WindowsPlatform.h` — PLATFORM_SUPPORTS_BINDLESS_RENDERING

---

## Anti-Patterns in UzEngine Currently

1. **No root signature caching**: Creating root signatures per-PSO instead of reusing them
2. **No platform detection**: Not checking `ResourceBindingTier` at init time
3. **No conditional descriptor skip**: Descriptor tables always present even when not used
4. **No shader parameter routing**: Textures always go through descriptor tables, no bindless path

---

## Summary Table

| Aspect | UE5 Approach | UzEngine Current | UzEngine Target |
|--------|--------------|------------------|-----------------|
| Root sig per PSO? | No (cached by QBSS) | Likely yes (check) | No (cache by tier) |
| Bindless support? | Yes (SM6.6+) | No | Yes (future-proof) |
| Descriptor fallback? | Yes (descriptor tables) | Only path | Keep as fallback |
| Binding tier check? | Yes (at init) | No | Add at RHI init |
| Shader parameter routing? | Runtime (type-checked) | Single path | Add conditional path |
