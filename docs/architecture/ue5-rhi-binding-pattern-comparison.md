# UE5 vs UzEngine RHI Binding Pattern Comparison

## Executive Summary

UE5 uses a **three-tier root signature abstraction** that separates concerns:
1. **Detection tier** (D3D12 capabilities)
2. **Declaration tier** (root sig definition, with feature flags)
3. **Binding tier** (shader parameter routing)

UzEngine currently **conflates all three tiers** in a single "fat root signature" with no fallback.

---

## Side-by-Side Architecture Comparison

### Resource Binding Flow

#### UE5 Approach (Multi-Stage)

```
┌──────────────────────────────────────────────┐
│ GPU INIT (FD3D12RHIModule::StartupModule)   │
├──────────────────────────────────────────────┤
│ 1. Detect D3D12_RESOURCE_BINDING_TIER        │
│ 2. Check PLATFORM_SUPPORTS_BINDLESS_RENDERING│
│ 3. Query MaxSupportedShaderModel             │
└────────┬─────────────────────────────────────┘
         │
         v
┌──────────────────────────────────────────────┐
│ ROOT SIGNATURE CREATION (Per QBSS Key)       │
├──────────────────────────────────────────────┤
│ 1. Build FD3D12QuantizedBoundShaderState     │
│    - Include: resourceBindingTier            │
│    - Include: bUseDirectlyIndexed flags      │
│ 2. Look up in RootSignatureManager cache     │
│ 3. If miss, create & cache new root sig      │
│ 4. Apply tier-appropriate descriptor flags   │
│ 5. Conditionally skip descriptor tables      │
│    (if BindlessResources flag set)           │
└────────┬─────────────────────────────────────┘
         │
         v
┌──────────────────────────────────────────────┐
│ PSO CREATION (Per Shader Combination)        │
├──────────────────────────────────────────────┤
│ 1. Pair PSO with cached root signature       │
│ 2. Compile shaders with feature flags        │
│ 3. Store pRootSignature* in PSO descriptor   │
└────────┬─────────────────────────────────────┘
         │
         v
┌──────────────────────────────────────────────┐
│ FRAME RENDERING (Per Draw Call)              │
├──────────────────────────────────────────────┤
│ 1. Set root signature (reused from cache)    │
│ 2. Route SetTextureParameter based on:       │
│    - EShaderParameterType::BindlessSRV       │
│    - If true → SetBindlessTexture()          │
│    - If false → SetShaderTexture()           │
│ 3. Update descriptor heap / root constants   │
└──────────────────────────────────────────────┘
```

**Key property**: Decisions flow downward; root sig knows how to configure based on tier.

---

#### UzEngine Current (Monolithic)

```
┌──────────────────────────────────────────────┐
│ GPU INIT (Initialize RHI)                    │
├──────────────────────────────────────────────┤
│ 1. Create ID3D12Device                       │
│ 2. ??? (No binding tier check)               │
│ 3. ??? (No bindless detection)               │
│ 4. ??? (No feature flag setup)               │
└────────┬─────────────────────────────────────┘
         │
         v
┌──────────────────────────────────────────────┐
│ ROOT SIGNATURE CREATION (Global, Static)     │
├──────────────────────────────────────────────┤
│ 1. Create single fat root signature          │
│    - b0: World (16 DWORD)                    │
│    - b1: Material (20 DWORD)                 │
│    - b2: Light (2 DWORD)                     │
│    - b3: ViewProj (16 DWORD)                 │
│    - t0-t9: SRV tables (10 slots)            │
│    - Total: 64 DWORD (at max)                │
│ 2. No caching (or single static instance)    │
│ 3. No tier-aware flags                       │
│ 4. Always include descriptor tables          │
└────────┬─────────────────────────────────────┘
         │
         v
┌──────────────────────────────────────────────┐
│ PSO CREATION (Per Shader + Root Sig Pair)    │
├──────────────────────────────────────────────┤
│ 1. Use global root signature for all PSOs    │
│ 2. Compile shaders (no feature flags)        │
│ 3. Store pRootSignature* in PSO descriptor   │
└────────┬─────────────────────────────────────┘
         │
         v
┌──────────────────────────────────────────────┐
│ FRAME RENDERING (Per Draw Call)              │
├──────────────────────────────────────────────┤
│ 1. Set root signature (same global instance) │
│ 2. SetTextureParameter:                      │
│    - ALWAYS update descriptor table entry    │
│    - No conditional bindless path            │
│ 3. Update descriptor heap only               │
└──────────────────────────────────────────────┘
```

**Key issue**: No branching logic; all paths assume descriptor tables are present.

---

## Detailed Pattern Differences

### Pattern A: Root Signature Caching

#### UE5 Implementation

**File**: `D3D12RootSignature.h:496`

```cpp
class FD3D12RootSignatureManager : public FD3D12AdapterChild {
    FD3D12RootSignature* GetRootSignature(const FD3D12QuantizedBoundShaderState& QBSS)
    {
        FD3D12RootSignature** ExistingRootSig = RootSignatureMap.Find(QBSS);
        if (ExistingRootSig) {
            return *ExistingRootSig;  // HIT: Return cached
        }
        // MISS: Create new, add to cache
        return CreateRootSignature(QBSS);
    }

private:
    TMap<FD3D12QuantizedBoundShaderState, FD3D12RootSignature*> RootSignatureMap;
};
```

**Key metrics**:
- Cache key: `FD3D12QuantizedBoundShaderState` (~100 bytes)
- Expected cache hits: >95% of PSOs reuse 1-2 root signatures
- Memory cost: ~5-10 KiB (few root sig objects)

#### UzEngine Current

**Status**: Unknown (check `src/engine/renderer/rhi/rhi.h`)

Likely pattern:
```cpp
// UzEngine (SPECULATION)
ID3D12RootSignature* CreateRootSignature(...) {
    // Creates new root sig every call?
    // Or single global instance?
    return device->CreateRootSignature(...);
}
```

**Problem**: If new root sig created per PSO:
- Unbounded memory growth
- GPU command list rebinds same root sig repeatedly
- No deduplication of identical signatures

#### Required UzEngine Change

```cpp
// uz_rhi_d3d12.h
struct UzQuantizedBoundShaderState {
    uint32 resourceBindingTier;
    uint32 bUseBindlessResources : 1;
    uint32 bUseBindlessSamplers : 1;
    // ... other state

    bool operator==(const Other&) const;
};

// Hash for TMap
inline uint32 GetTypeHash(const UzQuantizedBoundShaderState& x) {
    return HashCombine(GetTypeHash(x.resourceBindingTier),
                      GetTypeHash(x.bUseBindlessResources));
}

// uz_rhi_d3d12.cpp
class UzRootSignatureManager {
    TMap<UzQuantizedBoundShaderState, ID3D12RootSignature*> cache;

    ID3D12RootSignature* GetOrCreate(const UzQuantizedBoundShaderState& qbss) {
        if (auto* existing = cache.Find(qbss))
            return *existing;
        auto* newSig = CreateRootSignature(qbss);
        cache.Add(qbss, newSig);
        return newSig;
    }
};
```

---

### Pattern B: Binding Tier Adaptation

#### UE5 Implementation

**File**: `D3D12RootSignature.cpp:162-195`

```cpp
FD3D12RootSignatureDesc::FD3D12RootSignatureDesc(
    const FD3D12QuantizedBoundShaderState& QBSS,
    const D3D12_RESOURCE_BINDING_TIER ResourceBindingTier)  // <-- PARAM
{
    // Tier-dependent flags
    const D3D12_DESCRIPTOR_RANGE_FLAGS SRVDescriptorRangeFlags =
        (ResourceBindingTier <= D3D12_RESOURCE_BINDING_TIER_1) ?
        D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE :
        D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE |
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;

    const D3D12_DESCRIPTOR_RANGE_FLAGS CBVDescriptorRangeFlags =
        (ResourceBindingTier <= D3D12_RESOURCE_BINDING_TIER_2) ?
        D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE :
        D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE |
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
    // ...
    DescriptorRanges[Index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                                 Count, 0, Space, SRVDescriptorRangeFlags);
}
```

**Binding tier mapping**:
- **Tier 1** (SM5.0, Intel HD): `DATA_STATIC` — descriptors can't change frequently
- **Tier 2** (SM5.1+): `DATA_STATIC + DESCRIPTORS_VOLATILE` — descriptors can change, data is static
- **Tier 3** (SM6.0+): Full dynamic + bindless support

#### UzEngine Current

**Status**: Likely hardcoded to single tier.

Probable code:
```cpp
// UzEngine
CD3DX12_DESCRIPTOR_RANGE1 range;
range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 0, 0,
           D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE);
```

**Problem**:
- No tier check
- Flags may be incorrect for SM5.0 hardware (too permissive)
- Or correct for Tier 1 but suboptimal for Tier 2/3

#### Required UzEngine Change

```cpp
// uz_rhi_d3d12.cpp
void CreateRootSignature(const UzQuantizedBoundShaderState& qbss,
                        D3D12_RESOURCE_BINDING_TIER tier) {
    D3D12_DESCRIPTOR_RANGE_FLAGS srvFlags;
    if (tier <= D3D12_RESOURCE_BINDING_TIER_1) {
        srvFlags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    } else {
        srvFlags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE |
                   D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
    }
    // Apply srvFlags to all SRV ranges
}
```

---

### Pattern C: Conditional Descriptor Table Skip (Bindless Gating)

#### UE5 Implementation

**File**: `D3D12RootSignatureDefinitions.h:224-237`

```cpp
inline bool ShouldSkipType(ERootSignatureRangeType Type) const
{
    if (Type == ERootSignatureRangeType::SRV ||
        Type == ERootSignatureRangeType::UAV)
    {
        return EnumHasAnyFlags(Flags, EShaderBindingLayoutFlags::BindlessResources);
    }

    if (Type == ERootSignatureRangeType::Sampler)
    {
        return EnumHasAnyFlags(Flags, EShaderBindingLayoutFlags::BindlessSamplers);
    }

    return false;
}

void AddTable(ERootSignatureVisibility Visibility,
              ERootSignatureRangeType Type,
              int32 NumDescriptors)
{
    if (!ShouldSkipType(Type)) {  // <-- Conditional
        // Add descriptor table
    }
}
```

**Logic**:
- If `BindlessResources` flag set: Don't add SRV/UAV descriptor table ranges
- If `BindlessSamplers` flag set: Don't add Sampler descriptor table range
- Else: Add tables (traditional path)

#### UzEngine Current

**Status**: Always includes descriptor tables.

Current code:
```cpp
// UzEngine (ASSUMED)
// In root signature creation:
AddTable(..., ERootSignatureRangeType::SRV, ...);   // ALWAYS
AddTable(..., ERootSignatureRangeType::Sampler, ...);  // ALWAYS
// No conditional skip
```

**Problem**:
- Root signature always has descriptor table space
- Even on SM6.6 with bindless support, descriptor tables are allocated but unused
- Wastes root signature DWORD budget

#### Required UzEngine Change

```cpp
// uz_rhi_d3d12.h
enum class EShaderBindingLayoutFlags : uint32 {
    None = 0,
    InputAssembler = 1 << 0,
    BindlessResources = 1 << 1,      // NEW
    BindlessSamplers = 1 << 2,       // NEW
};

// uz_rhi_d3d12.cpp
class UzRootSignatureCreator {
    EShaderBindingLayoutFlags flags;

    bool ShouldSkipDescriptorTable(EResourceType type) const {
        if ((type == SRV || type == UAV) &&
            EnumHasAnyFlags(flags, EShaderBindingLayoutFlags::BindlessResources))
            return true;

        if (type == Sampler &&
            EnumHasAnyFlags(flags, EShaderBindingLayoutFlags::BindlessSamplers))
            return true;

        return false;
    }

    void AddTable(EResourceType type, uint32 count) {
        if (!ShouldSkipDescriptorTable(type)) {
            // Add descriptor table
        }
    }
};
```

---

### Pattern D: Shader Parameter Routing (Dual-Path Binding)

#### UE5 Implementation

**File**: `ShaderParameterUtils.h:77-92`

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
1. **Bindless path**: `SetBindlessTexture()` → copies handle to root constants or shader buffer
2. **Table path**: `SetShaderTexture()` → updates descriptor table entry in GPU heap

Same function signature, two backends based on `EShaderParameterType`.

#### UzEngine Current

**Status**: Single path only (descriptor table).

Likely code:
```cpp
// UzEngine
void SetTextureParameter(..., FRHITexture* texture) {
    // Only option: update descriptor table
    descriptorHeap[slotIndex] = texture->GetDescriptor();
}
```

**Problem**:
- No conditional bindless support
- Can't exploit bindless performance on SM6.0+
- Tightly coupled to descriptor table implementation

#### Required UzEngine Change

```cpp
// uz_rhi_device.h
enum class EShaderParameterType : uint8 {
    SRV,
    Sampler,
    CBV,
    UAV,
    BindlessSRV,        // NEW
    BindlessSampler,    // NEW
};

// uz_rhi_device.cpp
inline void SetTextureParameter(UzBatchedShaderParams& params,
                               const UzShaderResourceParameter& param,
                               UzTexture* texture) {
    if (!param.IsBound()) return;

    if (param.GetType() == EShaderParameterType::BindlessSRV) {
        // Path A: Bindless (store handle in root constants)
        params.SetBindlessTexture(param.GetBaseIndex(), texture);
    } else {
        // Path B: Descriptor table (traditional)
        params.SetShaderTexture(param.GetBaseIndex(), texture);
    }
}
```

---

## Shader Compilation Integration

### UE5 Approach

**File**: `GlobalShader.h:116`

```cpp
void SetupCompileEnvironment(EShaderPlatform Platform,
                             int32 PermutationId,
                             EShaderPermutationFlags Flags,
                             FShaderCompilerEnvironment& Environment) const;
```

Shaders compiled with feature flags:
```cpp
// In DXC command line:
-D SHADER_PLATFORM=1
-D BINDLESS_RESOURCES=1   // If supported
-D RESOURCE_BINDING_TIER=3 // Actual tier
```

Shader code branches:
```hlsl
#if BINDLESS_RESOURCES
    // Use global descriptor buffers
    Buffer<uint> ResourceHandles : register(b15);
#else
    // Use descriptor table
    Texture2D albedo : register(t0);
#endif
```

### UzEngine Current

**Status**: Likely no feature flag branching.

Shaders compiled once, no `BINDLESS` define.

### Required UzEngine Change

```cpp
// uz_shader_compiler.cpp
void CompileShader(const UzShaderDesc& desc, ...) {
    DxcCompilerArgs args;

    if (RHI->SupportsBindlessResources()) {
        args.Add(L"-D UZ_ENABLE_BINDLESS_RESOURCES=1");
    } else {
        args.Add(L"-D UZ_ENABLE_BINDLESS_RESOURCES=0");
    }

    if (RHI->GetResourceBindingTier() >= 2) {
        args.Add(L"-D UZ_RESOURCE_BINDING_TIER=2");
    }
}
```

---

## Summary: Five Key Gaps

| Gap | UE5 | UzEngine | Fix Priority |
|-----|-----|----------|--------------|
| **1. Root sig caching** | TMap-based manager | Likely per-PSO | P0 (memory + perf) |
| **2. Binding tier check** | At adapter init | None | P0 (correctness on SM5) |
| **3. Descriptor table skip** | ShouldSkipType() | Always included | P1 (future-proof) |
| **4. Shader param routing** | Dual-path dispatch | Single path | P1 (bindless readiness) |
| **5. Shader feature flags** | Compile-time define | None | P2 (after impl) |

---

## Testing Strategy

### Test 1: Root Signature Caching
```cpp
// uz_test_root_sig_cache.cpp
TEST(RootSignatureCache, ReusesIdenticalSignatures) {
    UzQuantizedBoundShaderState qbss1 = CreateQBSS(/* tier 2, bindless=true */);
    UzQuantizedBoundShaderState qbss2 = CreateQBSS(/* tier 2, bindless=true */);

    auto* sig1 = manager->GetOrCreate(qbss1);
    auto* sig2 = manager->GetOrCreate(qbss2);

    EXPECT_EQ(sig1, sig2);  // Same object
    EXPECT_EQ(manager->GetCacheSize(), 1);
}
```

### Test 2: Binding Tier Flags
```cpp
// uz_test_binding_tier.cpp
TEST(BindingTierFlags, Tier1UsesStaticFlags) {
    D3D12_DESCRIPTOR_RANGE_FLAGS flags = GetSRVDescriptorRangeFlags(D3D12_RESOURCE_BINDING_TIER_1);
    EXPECT_FALSE(flags & D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
}

TEST(BindingTierFlags, Tier2UsesVolatileFlags) {
    D3D12_DESCRIPTOR_RANGE_FLAGS flags = GetSRVDescriptorRangeFlags(D3D12_RESOURCE_BINDING_TIER_2);
    EXPECT_TRUE(flags & D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
}
```

### Test 3: Descriptor Table Skip
```cpp
// uz_test_bindless_skip.cpp
TEST(BindlessSkip, SkipsDescriptorTablesWhenBindless) {
    UzRootSignatureCreator creator;
    creator.SetFlags(EShaderBindingLayoutFlags::BindlessResources);

    // Verify no SRV/UAV tables added
    creator.AddTable(EResourceType::SRV, 16);
    // (Should be no-op due to ShouldSkipDescriptorTable)

    auto desc = creator.Finalize();
    AssertNoSRVDescriptorTables(desc);
}
```

### Test 4: Shader Parameter Routing
```cpp
// uz_test_shader_param_routing.cpp
TEST(ShaderParamRouting, UsesBindlessPath) {
    UzShaderResourceParameter param;
    param.SetType(EShaderParameterType::BindlessSRV);

    UzBatchedShaderParams params;
    SetTextureParameter(params, param, texture);

    // Verify SetBindlessTexture was called, not SetShaderTexture
    EXPECT_EQ(params.GetBindlessTextureCount(), 1);
    EXPECT_EQ(params.GetDescriptorTableUpdateCount(), 0);
}

TEST(ShaderParamRouting, FallsBackToTablePath) {
    UzShaderResourceParameter param;
    param.SetType(EShaderParameterType::SRV);  // Traditional

    UzBatchedShaderParams params;
    SetTextureParameter(params, param, texture);

    // Verify SetShaderTexture was called
    EXPECT_EQ(params.GetBindlessTextureCount(), 0);
    EXPECT_EQ(params.GetDescriptorTableUpdateCount(), 1);
}
```

---

## Conclusion

UE5's root signature pattern is elegant because it **separates concerns**:
1. Detection (What hardware are we on?)
2. Declaration (How do we declare resources given these constraints?)
3. Binding (How do we update resources at runtime?)

UzEngine must adopt this separation to:
- Support multiple shader models (SM5 vs SM6)
- Prepare for bindless rendering (performance + future-proof)
- Maintain clean architecture (no feature-specific hacks)

The migration is phased and non-breaking:
- Phase 1-2: Cache + tier adaptation (behavioral changes only, same output)
- Phase 3-4: Bindless support (opt-in feature, fallback always works)
