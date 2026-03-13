# UzEngine Resource Binding Migration Playbook

This document translates UE5's root signature patterns into concrete UzEngine migration steps.

## Current UzEngine Anti-Patterns (Must Fix)

### Anti-Pattern 1: "Always Create Root Signature, Never Cache"

**Current behavior** (likely):
```cpp
// Pseudo-UzEngine current code (WRONG)
void CreatePSO(...) {
    FD3D12RootSignature rootSig = CreateRootSignature(...);  // NEW every time!
    FD3D12PipelineState pso = CreatePSO(rootSig, shaders);
    return pso;
}
```

**Why it's wrong**:
- Creates duplicate root signature objects
- GPU command buffer might rebind same root sig multiple times per frame
- No deduplication = wasted GPU/CPU time

**UE5 pattern** (line 496 in D3D12RootSignature.h):
```cpp
FD3D12RootSignature* GetRootSignature(const FD3D12QuantizedBoundShaderState& QBSS)
{
    FD3D12RootSignature** ExistingRootSig = RootSignatureMap.Find(QBSS);
    if (ExistingRootSig)
        return *ExistingRootSig;  // REUSE
    return CreateRootSignature(QBSS);  // First time only
}
```

### Anti-Pattern 2: "No Binding Tier Detection"

**Current behavior**:
```cpp
// UzEngine likely assumes all GPUs are equal
// No check for D3D_RESOURCE_BINDING_TIER
```

**Why it's wrong**:
- SM5.0 hardware (Intel Integrated, some AMD) = Tier 1 (limited descriptor volatility)
- Modern discrete = Tier 2-3 (full descriptor flexibility)
- No tier check = missed optimization opportunity or compatibility bug

**UE5 pattern** (D3D12RootSignature.cpp:181-195):
```cpp
const D3D12_DESCRIPTOR_RANGE_FLAGS SRVDescriptorRangeFlags =
    (ResourceBindingTier <= D3D12_RESOURCE_BINDING_TIER_1) ?
    D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE :
    D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE |
    D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
```

Different tier → different flags → different binding performance characteristics.

### Anti-Pattern 3: "No Bindless Path, Only Descriptor Tables"

**Current behavior**:
```cpp
// UzEngine sets textures via descriptor table only
SetTextureInDescriptorTable(slotIndex, texture);
```

**Why it's wrong**:
- Descriptor heap updates are CPU-side (limited by PCIe bandwidth)
- Bindless: 1 uint32 root constant update vs 8-byte descriptor table entry update
- ~10x CPU bandwidth savings on high-texture-count frames

**UE5 pattern** (ShaderParameterUtils.h:77-92):
```cpp
if (Parameter.GetType() == EShaderParameterType::BindlessSRV)
{
    BatchedParameters.SetBindlessTexture(Parameter.GetBaseIndex(), TextureRHI);
}
else
{
    BatchedParameters.SetShaderTexture(Parameter.GetBaseIndex(), TextureRHI);
}
```

One shader parameter binding function, two runtime paths.

### Anti-Pattern 4: "No Feature Flag Abstraction"

**Current behavior**:
```cpp
// Hardcoded assumptions in shader and C++ code
if (IsMetallicTextureBound) {
    // Use descriptor table slot 5
}
```

**Why it's wrong**:
- Tight coupling between bindless/table modes
- Can't easily switch modes (or both modes coexist messily)
- No graceful fallback when bindless unavailable

**UE5 pattern** (D3D12RootSignatureDefinitions.h:189-212):
```cpp
struct FRootSignatureCreator {
    EShaderBindingLayoutFlags Flags;

    void SetFlags(EShaderBindingLayoutFlags InFlags) {
        if (EnumHasAnyFlags(InFlags, EShaderBindingLayoutFlags::BindlessResources))
            AddRootFlag(D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
        // ...
    }

    bool ShouldSkipType(ERootSignatureRangeType Type) const {
        if (Type == SRV || Type == UAV)
            return EnumHasAnyFlags(Flags, EShaderBindingLayoutFlags::BindlessResources);
        return false;
    }
};
```

Clear feature abstraction: descriptor tables added only when needed.

---

## Migration Roadmap

### Phase 1: Root Signature Caching (Week 1-2)

**Goal**: Reuse root signatures, detect binding tier.

**Step 1a: Add QBSS Key Type**
```cpp
// uz_rhi_d3d12.h
struct UzQuantizedBoundShaderState {
    uint32 resourceBindingTier;  // D3D12_RESOURCE_BINDING_TIER
    bool bUseDirectlyIndexedResourceHeap = false;
    bool bUseDirectlyIndexedSamplerHeap = false;
    uint8 maxSRVCount[6];  // Per shader stage (VS/PS/GS/MS/AS/CS)
    uint8 maxCBVCount[6];
    uint8 maxSamplerCount[6];
    uint8 maxUAVCount[6];

    bool operator==(const UzQuantizedBoundShaderState& Other) const {
        return resourceBindingTier == Other.resourceBindingTier &&
               bUseDirectlyIndexedResourceHeap == Other.bUseDirectlyIndexedResourceHeap &&
               // ... compare counts
    }
};

// Hash function for TMap
inline uint32 GetTypeHash(const UzQuantizedBoundShaderState& QBSS) {
    uint32 hash = GetTypeHash(QBSS.resourceBindingTier);
    hash = HashCombine(hash, GetTypeHash(QBSS.bUseDirectlyIndexedResourceHeap));
    // ... combine counts
    return hash;
}
```

**Step 1b: Detect Binding Tier at RHI Init**
```cpp
// uz_rhi_d3d12.cpp::InitRHI()
D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));

if (options.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_1) {
    UZ_LOG(RHI, Warning, "GPU does not support D3D12 resource binding");
    return false;
}

RHIState.resourceBindingTier = options.ResourceBindingTier;
```

**Step 1c: Create Root Signature Manager**
```cpp
class UzRootSignatureManager {
    TMap<UzQuantizedBoundShaderState, UzRootSignature*> cache;
    FCriticalSection cacheLock;

    UzRootSignature* GetOrCreate(const UzQuantizedBoundShaderState& qbss) {
        FScopeLock lock(&cacheLock);
        if (auto* existing = cache.Find(qbss))
            return *existing;
        auto* newSig = CreateRootSignature(qbss);
        cache.Add(qbss, newSig);
        return newSig;
    }
};
```

**Test**:
- [ ] Root signature cache has 1-2 entries (not N where N = PSO count)
- [ ] Multiple PSOs with same root sig don't rebind in command list

---

### Phase 2: Binding Tier Adaptation (Week 3-4)

**Goal**: Adjust descriptor range flags based on tier, prepare for bindless.

**Step 2a: Tier-Aware Root Signature Creation**
```cpp
// uz_rhi_d3d12.cpp::CreateRootSignature()
void CreateRootSignature(const UzQuantizedBoundShaderState& qbss, UzRootSignature& outRootSig) {
    D3D12_DESCRIPTOR_RANGE_FLAGS srvFlags;
    if (qbss.resourceBindingTier <= D3D12_RESOURCE_BINDING_TIER_1) {
        srvFlags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    } else {
        srvFlags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE |
                   D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
    }
    // Apply srvFlags to all SRV descriptor ranges
}
```

**Step 2b: Add Feature Flag Enum**
```cpp
enum class EShaderBindingLayoutFlags : uint32 {
    None = 0,
    InputAssembler = 1 << 0,
    BindlessResources = 1 << 1,  // NEW
    BindlessSamplers = 1 << 2,   // NEW
    RootConstants = 1 << 3,
};
```

**Step 2c: Pass Flags to Root Signature Creator**
```cpp
class UzRootSignatureCreator {
    EShaderBindingLayoutFlags flags;

    void SetFlags(EShaderBindingLayoutFlags inFlags) {
        flags = inFlags;
        if (EnumHasAnyFlags(inFlags, EShaderBindingLayoutFlags::BindlessResources)) {
            AddRootFlag(D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
        }
    }

    bool ShouldSkipDescriptorTable(EResourceType type) {
        if (type == EResourceType::SRV || type == EResourceType::UAV)
            return EnumHasAnyFlags(flags, EShaderBindingLayoutFlags::BindlessResources);
        return false;
    }

    void AddTable(...) {
        if (!ShouldSkipDescriptorTable(type))
            // Add descriptor table
    }
};
```

**Test**:
- [ ] Tier 1 GPU: descriptor range has `DATA_STATIC_WHILE_SET_AT_EXECUTE` only
- [ ] Tier 2+ GPU: descriptor range also has `DESCRIPTORS_VOLATILE`
- [ ] PSO creation still succeeds on both tiers

---

### Phase 3: Bindless Shader Parameter Routing (Week 5-6)

**Goal**: Enable conditional bindless texture binding in shaders.

**Step 3a: Add Bindless Capability Check**
```cpp
// uz_rhi_d3d12.cpp::InitRHI()
D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3 = {};
device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &options3, sizeof(options3));

bool supportsBindless = (RHIState.resourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3) &&
                       IsFeatureLevelSupported(D3D_FEATURE_LEVEL_12_1);
RHIState.bBindlessResourcesSupported = supportsBindless;
```

**Step 3b: Update Shader Parameter Type**
```cpp
enum class EShaderParameterType : uint8 {
    SRV,
    CBV,
    Sampler,
    UAV,
    BindlessSRV,        // NEW
    BindlessSampler,    // NEW
};
```

**Step 3c: Add Bindless Descriptor Allocator** (if not already present)
```cpp
class UzBindlessDescriptorAllocator {
    FD3D12DescriptorHeap* gpuHeap = nullptr;
    FRHIHeapDescriptorAllocator* allocator = nullptr;

    FRHIDescriptorHandle AllocateHandle() {
        return allocator->Allocate();
    }
};
```

**Step 3d: Dual-Path Texture Binding**
```cpp
// uz_rhi_device.cpp
inline void SetTextureParameter(FRHIBatchedShaderParameters& params,
                               const FShaderResourceParameter& param,
                               FRHITexture* texture) {
    if (!param.IsBound()) return;

#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
    if (param.GetType() == EShaderParameterType::BindlessSRV) {
        // Path 1: Bindless
        params.SetBindlessTexture(param.GetBaseIndex(), texture);
        return;
    }
#endif
    // Path 2: Descriptor table (fallback)
    params.SetShaderTexture(param.GetBaseIndex(), texture);
}
```

**Test**:
- [ ] Bindless mode: `SetBindlessTexture` is called (inspect command buffer or stats)
- [ ] Descriptor table mode: `SetShaderTexture` is called
- [ ] Both produce correct on-screen results

---

### Phase 4: Shader Compilation & Feature Flag Gating (Week 7-8)

**Goal**: Compile shaders with bindless feature flag when available.

**Step 4a: Add Shader Compiler Flag**
```cpp
// uz_shader_compiler.cpp::CompileShader()
void CompileShader(...) {
    DxcCompilerArgs args;
    // ... existing args

    if (RHI->SupportBindlessResources()) {
        args.Add(L"-D UZ_ENABLE_BINDLESS_RESOURCES=1");
        args.Add(L"-D UZ_MAX_RESOURCE_HEAP_SIZE=1000000");
    }
}
```

**Step 4b: Update Shader Includes**
```hlsl
// shaders/common/uz_common.hlsli
#ifndef UZ_ENABLE_BINDLESS_RESOURCES
    #define UZ_ENABLE_BINDLESS_RESOURCES 0
#endif

#if UZ_ENABLE_BINDLESS_RESOURCES
    #define BINDLESS_TEXTURE(name, slot) uint name##_bindless_handle : register(b0)
    #define LOAD_BINDLESS_TEXTURE(name) ResourceHeap[name##_bindless_handle]
#else
    #define BINDLESS_TEXTURE(name, slot)  // No-op
    #define LOAD_BINDLESS_TEXTURE(name)   name
#endif
```

**Step 4c: Example Shader Using Feature Flag**
```hlsl
// shaders/forward_pass.hlsl
#include "uz_common.hlsli"

Texture2D albedoTexture : register(t0);

#if UZ_ENABLE_BINDLESS_RESOURCES
    // Bindless path: texture handle comes as root constant
    GloballyCoherentRWByteAddressBuffer ResourceHeap : register(u0);
#else
    // Table path: traditional descriptor table
    SamplerState albedoSampler : register(s0);
#endif

float4 main(PS_INPUT input) : SV_Target {
    float4 albedo;

#if UZ_ENABLE_BINDLESS_RESOURCES
    // Load from bindless heap
    uint textureHandle = /* read from root constants */;
    Texture2D<float4> tex = ResourceHeap.Load<Texture2D<float4>>(textureHandle);
    albedo = tex.Sample(globalSampler, input.uv);
#else
    // Load from descriptor table (current path)
    albedo = albedoTexture.Sample(albedoSampler, input.uv);
#endif

    return albedo;
}
```

**Test**:
- [ ] Bindless shader compiled with `-D UZ_ENABLE_BINDLESS_RESOURCES=1`
- [ ] Table shader compiled without flag
- [ ] Both shaders render identical results

---

## Validation Checklist

### Phase 1 Validation
- [ ] `RootSignatureManager::cache` size stays ≤ 2 (bindless + non-bindless)
- [ ] `GetOrCreate()` returns cached instance on second call
- [ ] No memory leaks in root signature cache (check on shutdown)

### Phase 2 Validation
- [ ] Query GPU binding tier at adapter init
- [ ] Create root signature with tier-aware descriptor range flags
- [ ] PSO creation succeeds on Tier 1, Tier 2, Tier 3 GPUs
- [ ] No descriptor volatility issues on Tier 1 (should use static flags)

### Phase 3 Validation
- [ ] `bBindlessResourcesSupported` true on modern GPU, false on SM5 hardware
- [ ] `SetBindlessTexture()` called only when feature enabled
- [ ] `SetShaderTexture()` called as fallback
- [ ] No undefined behavior mixing both paths in single frame

### Phase 4 Validation
- [ ] Shader compiles with `-D UZ_ENABLE_BINDLESS_RESOURCES=1` when supported
- [ ] Shader compiles without flag on SM5 fallback
- [ ] Frame renders identically in both modes
- [ ] Memory alignment correct (uint32 handles in root constants)

---

## Migration Success Metrics

| Metric | Current | Target | Benefit |
|--------|---------|--------|---------|
| Root signature cache hits | ~0% (no cache) | >95% | Faster PSO binding |
| CPU cycles/texture bind | ~50 (table update) | ~10 (bindless) | 5x CPU savings on texture-heavy frames |
| GPU feature parity | SM5 only | SM5 + SM6 | Future-proof, bindless when available |
| Shader variant count | 1 per shader | 1 per shader | No combinatorial explosion |

---

## Quick Reference: UE5 Patterns by File

| Problem | UE5 File | UE5 Lines | Pattern Name |
|---------|----------|-----------|--------------|
| Root sig caching | D3D12RootSignature.h | 483-504 | FD3D12RootSignatureManager |
| Binding tier check | D3D12RootSignature.cpp | 181-195 | Conditional descriptor range flags |
| Feature gating | D3D12RootSignatureDefinitions.h | 189-212 | `ShouldSkipType()` / `SetFlags()` |
| Shader parameter routing | ShaderParameterUtils.h | 77-92 | `SetTextureParameter()` dual path |
| Bindless detection | D3D12Util.cpp | Line ~400 | `AreResourcesBindless()` check |
| Platform gating | WindowsPlatform.h | Line ~140 | `PLATFORM_SUPPORTS_BINDLESS_RENDERING` |

---

## Known Pitfalls (Avoid!)

1. **Mixing bindless and table modes in same frame**:
   - If shader expects bindless but root sig has tables, undefined behavior
   - **Solution**: PSO is paired with root sig; shader is compiled for root sig mode

2. **Descriptor volatility on Tier 1 GPUs**:
   - Tier 1 doesn't support `DESCRIPTORS_VOLATILE` flag
   - Setting it anyway = driver error
   - **Solution**: Check `ResourceBindingTier` before adding flag

3. **Forgetting to cache root signatures**:
   - Creates duplicate root sigs = memory leak + GPU command buffer bloat
   - **Solution**: Always use `RootSignatureManager::GetOrCreate()`

4. **Shader not matching root signature mode**:
   - Shader compiled bindless, but root sig in table mode = mismatched descriptors
   - **Solution**: Compile shader with feature flag matching root sig flags

5. **Root constants overflow**:
   - Bindless mode stores handles in root constants (32 bits per resource)
   - Root constants are limited to 64 DWORDs per shader stage
   - **Solution**: Cap bindless resources per shader, use register space if needed
