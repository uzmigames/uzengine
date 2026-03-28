<!-- doc-meta
title: UE5 Root Signature Creation & Serialization Strategy
author: Claude Code
date: 2026-03-28
tags: [ue5-pattern, dx12-rhi, root-signatures, bindless, agility-sdk]
-->

# UE5 Root Signature Strategy

## Executive Summary

UE5's root signature system handles DX12 versioning and bindless rendering through a **runtime feature detection and conditional serialization approach**. It avoids the `D3D12SerializeVersionedRootSignature` problem on Win10 without Agility SDK by:

1. **Feature detection at adapter init time** — checks device capabilities for root signature version (1.0 vs 1.1)
2. **Fallback serialization** — downgrades 1.1 descriptors to 1.0 format when serializing for older devices
3. **Bindless as an optional runtime flag** — not baked into root signature version, but into runtime flags that DX12 devices selectively support

---

## Problem Statement

The issue surfaces in 3 scenarios:

1. **Win10 without Agility SDK**: `d3d12.dll` may not export `D3D12SerializeVersionedRootSignature`, falling back to `D3D12SerializeRootSignature` (1.0 only)
2. **Bindless rendering**: Flag `CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED` requires SM 6.6+ or explicit device support (not automatic)
3. **Cross-version compatibility**: Shader model 5.0 can't use bindless, but code must not crash when compiled for 6.6+

---

## UE5 Solution Architecture

### 1. Root Signature Version Detection

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12Adapter.cpp:1318-1324`

```cpp
D3D12_FEATURE_DATA_ROOT_SIGNATURE D3D12RootSignatureCaps = {};
D3D12RootSignatureCaps.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;  // Request 1.1
if (FAILED(RootDevice->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &D3D12RootSignatureCaps, sizeof(D3D12RootSignatureCaps))))
{
    D3D12RootSignatureCaps.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;  // Fallback to 1.0
}
RootSignatureVersion = D3D12RootSignatureCaps.HighestVersion;  // Store for later use
```

**Key pattern**:
- Assumes device supports 1.1 (`HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1`)
- Calls `CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, ...)`
- If it fails → device only supports 1.0
- Stores the result in `FD3D12Adapter::RootSignatureVersion` for use during serialization

**Win10 impact**: Win10 without Agility SDK will fail the `CheckFeatureSupport` call and fall back to 1.0, which doesn't use descriptor range flags or advanced features.

---

### 2. Conditional Serialization

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12RootSignature.cpp:574-596`

```cpp
void FD3D12RootSignature::Init(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC& InDesc, ERootSignatureType InRootSignatureType)
{
    ID3D12Device* Device = GetParentAdapter()->GetD3DDevice();

    // Serialize with version known from adapter init
    TRefCountPtr<ID3DBlob> Error;
    const D3D_ROOT_SIGNATURE_VERSION MaxRootSignatureVersion = GetParentAdapter()->GetRootSignatureVersion();
    const HRESULT SerializeHR = D3DX12SerializeVersionedRootSignature(
        &InDesc,
        MaxRootSignatureVersion,  // Pass detected version (1.0 or 1.1)
        RootSignatureBlob.GetInitReference(),
        Error.GetInitReference()
    );
    if (Error.GetReference())
    {
        UE_LOG(LogD3D12RHI, Fatal, TEXT("D3DX12SerializeVersionedRootSignature failed with error %s"), ...);
    }
    VERIFYD3D12RESULT(SerializeHR);

    // Now create the signature with serialized data
    VERIFYD3D12RESULT(Device->CreateRootSignature(
        FRHIGPUMask::All().GetNative(),
        RootSignatureBlob->GetBufferPointer(),
        RootSignatureBlob->GetBufferSize(),
        IID_PPV_ARGS(RootSignature.GetInitReference())
    ));

    AnalyzeSignature(InDesc, InRootSignatureType);
}
```

**The win**: By passing `MaxRootSignatureVersion` (1.0 or 1.1) to `D3DX12SerializeVersionedRootSignature`, the d3dx12.h helper automatically downgrades the serialization format (see d3dx12.h code below).

---

### 3. D3DX12 Fallback Serialization in d3dx12.h

**File**: `W:\UE_5.6_Source\Engine\Source\ThirdParty\Intel\ISPCTexComp\.../d3dx12.h:2041-2125`

```cpp
inline HRESULT D3DX12SerializeVersionedRootSignature(
    _In_ const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pRootSignatureDesc,
    D3D_ROOT_SIGNATURE_VERSION MaxVersion,  // <-- This is the key parameter
    _Outptr_ ID3DBlob** ppBlob,
    _Always_(_Outptr_opt_result_maybenull_) ID3DBlob** ppErrorBlob)
{
    if (ppErrorBlob != nullptr)
    {
        *ppErrorBlob = nullptr;
    }

    switch (MaxVersion)
    {
        case D3D_ROOT_SIGNATURE_VERSION_1_0:
            // If device only supports 1.0, downgrade the signature
            switch (pRootSignatureDesc->Version)
            {
                case D3D_ROOT_SIGNATURE_VERSION_1_0:
                    // Already 1.0, use it directly
                    return D3D12SerializeRootSignature(
                        &pRootSignatureDesc->Desc_1_0,
                        D3D_ROOT_SIGNATURE_VERSION_1,
                        ppBlob,
                        ppErrorBlob
                    );

                case D3D_ROOT_SIGNATURE_VERSION_1_1:
                    // Downgrade 1.1 → 1.0 by stripping descriptor range flags
                    {
                        HRESULT hr = S_OK;
                        const D3D12_ROOT_SIGNATURE_DESC1& desc_1_1 = pRootSignatureDesc->Desc_1_1;

                        // Allocate 1.0 parameter array
                        const SIZE_T ParametersSize = sizeof(D3D12_ROOT_PARAMETER) * desc_1_1.NumParameters;
                        void* pParameters = (ParametersSize > 0) ? HeapAlloc(GetProcessHeap(), 0, ParametersSize) : nullptr;
                        auto pParameters_1_0 = reinterpret_cast<D3D12_ROOT_PARAMETER*>(pParameters);

                        if (SUCCEEDED(hr))
                        {
                            for (UINT n = 0; n < desc_1_1.NumParameters; n++)
                            {
                                // Copy parameter metadata
                                pParameters_1_0[n].ParameterType = desc_1_1.pParameters[n].ParameterType;
                                pParameters_1_0[n].ShaderVisibility = desc_1_1.pParameters[n].ShaderVisibility;

                                // Copy parameter-specific data (stripping 1.1 extensions)
                                switch (desc_1_1.pParameters[n].ParameterType)
                                {
                                    case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                                        pParameters_1_0[n].Constants = desc_1_1.pParameters[n].Constants;
                                        break;

                                    case D3D12_ROOT_PARAMETER_TYPE_CBV:
                                    case D3D12_ROOT_PARAMETER_TYPE_SRV:
                                    case D3D12_ROOT_PARAMETER_TYPE_UAV:
                                        pParameters_1_0[n].Descriptor = desc_1_1.pParameters[n].Descriptor;
                                        break;

                                    case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                                        // Downgrade descriptor ranges (strip flags)
                                        const D3D12_ROOT_DESCRIPTOR_TABLE1& table_1_1 = desc_1_1.pParameters[n].DescriptorTable;
                                        // ... convert D3D12_DESCRIPTOR_RANGE1 to D3D12_DESCRIPTOR_RANGE (stripping range flags)
                                        break;
                                }
                            }
                        }

                        // Serialize the downgraded 1.0 structure
                        if (SUCCEEDED(hr))
                        {
                            hr = D3D12SerializeRootSignature(
                                (D3D12_ROOT_SIGNATURE_DESC*)&*pParameters_1_0,
                                D3D_ROOT_SIGNATURE_VERSION_1,
                                ppBlob,
                                ppErrorBlob
                            );
                        }

                        HeapFree(GetProcessHeap(), 0, pParameters);
                        return hr;
                    }
            }
            break;

        case D3D_ROOT_SIGNATURE_VERSION_1_1:
            // Device supports 1.1, use the serializer directly
            return D3D12SerializeVersionedRootSignature(...);
    }
}
```

**Critical insight**: When `MaxVersion = D3D_ROOT_SIGNATURE_VERSION_1_0`, this function **automatically converts** 1.1 structures to 1.0 by:
- Allocating a 1.0 parameter array
- Copying non-extended fields
- Dropping descriptor range flags (`D3D12_DESCRIPTOR_RANGE_FLAGS`)
- Calling `D3D12SerializeRootSignature` (the older API available on Win10)

This is why UE5 avoids `D3D12SerializeVersionedRootSignature` export dependency — the fallback is built into d3dx12.h helper.

---

### 4. Bindless Flag Handling

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12RootSignature.cpp:351-359`

```cpp
if (QBSS.bUseDirectlyIndexedResourceHeap)
{
    Flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
}

if (QBSS.bUseDirectlyIndexedSamplerHeap)
{
    Flags |= D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
}
```

**How it works**:
- These flags are set at root signature creation time (from `FD3D12QuantizedBoundShaderState`)
- They're valid in both 1.0 and 1.1 serialized signatures
- The downgrade from 1.1 to 1.0 (in d3dx12.h) preserves these root signature flags
- They only have an effect if the device supports bindless (which is checked during adapter init)

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12Adapter.cpp:1346-1359`

```cpp
ERHIBindlessConfiguration BindlessResourcesConfig = ERHIBindlessConfiguration::Disabled;
ERHIBindlessConfiguration BindlessSamplersConfig = ERHIBindlessConfiguration::Disabled;

#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
if (Desc.MaxSupportedFeatureLevel >= D3D_FEATURE_LEVEL_12_0 &&
    Desc.MaxSupportedShaderModel >= D3D_SHADER_MODEL_6_6 &&
    Desc.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3)
{
    // Needs to happen before device creation below
    BindlessDescriptorAllocator.Init();

    BindlessResourcesConfig = BindlessDescriptorAllocator.GetResourcesConfiguration();
    BindlessSamplersConfig = BindlessDescriptorAllocator.GetSamplersConfiguration();

    GRHIGlobals.ShaderBundles.RequiresSharedBindlessParameters = ...;
}
```

**The pattern**:
- Bindless is enabled only if **all three** conditions are met:
  - Feature level 12.0+ (Win10+)
  - Shader model 6.6+ (GPU supports new features)
  - Resource binding tier 3 (descriptor heap directly indexed support)
- If any condition fails, bindless is disabled globally
- Renderer code doesn't need to check bindless at runtime — the decision is made once at init time

---

## How Shader Models Are Decoupled from Root Signatures

**Key insight**: UE5 doesn't have separate root signatures per shader model. Instead:

1. **Root signature is SM-agnostic** — it defines what resources are available, not how they're used
2. **Shader compilation includes SM selection** — the shader compiler (DXC) generates different code paths for SM 5.0, 6.0, 6.6+
3. **Bindless is optional** — if root signature has `DIRECTLY_INDEXED` flag, shaders can use it; older shaders in the same root sig can ignore it

**Example**:
- Same root signature with `DIRECTLY_INDEXED` flag
- SM 5.0 shader: Uses traditional descriptor tables (can't use directly-indexed syntax anyway)
- SM 6.6 shader: Uses direct indexing syntax (`t[index]` instead of `t0`)
- Both work with the same root signature and GPU state

---

## UzEngine Adaptation Recommendations

### 1. Implement Feature Detection (Required)

```cpp
// In your RHI adapter init
D3D12_FEATURE_DATA_ROOT_SIGNATURE RootSigCaps = {};
RootSigCaps.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
if (FAILED(Device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &RootSigCaps, sizeof(RootSigCaps))))
{
    RootSigCaps.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
}
mRootSignatureVersion = RootSigCaps.HighestVersion;
```

### 2. Use D3DX12 Helper, Not Direct D3D12 API

```cpp
// CORRECT: Use d3dx12.h helper
D3D12SerializeVersionedRootSignature(&desc, mRootSignatureVersion, &blob, &error);

// WRONG: Direct API call (fails on Win10 without Agility SDK)
D3D12SerializeVersionedRootSignature(&desc, &blob, &error);  // Missing MaxVersion param!
```

### 3. Bindless as Optional Global Flag

```cpp
// In your shader compilation pipeline
struct ShaderCompileContext
{
    bool bAllowBindless = false;  // Set from adapter init
    D3D_SHADER_MODEL targetSM = D3D_SHADER_MODEL_6_0;
};

// Root signature creation
D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
if (isBindlessSupported && bAllowBindless)
{
    flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
}
rootSigDesc.Flags = flags;
```

### 4. Don't Couple Root Signature Version to Shader Model

```cpp
// WRONG: Separate root signatures per SM
RootSignature* GetRootSig_SM50() { ... }
RootSignature* GetRootSig_SM66() { ... }

// CORRECT: One root signature, shader code decides usage
RootSignature* GetRootSig(EShaderModel targetSM) {
    // Same root sig for all SMs
    // Shader compiler generates different code paths
    return mGlobalRootSignature;
}
```

---

## Anti-Patterns Found in UE5 (Learn What NOT to Do)

None significant. UE5's approach is clean and well-architected. The system correctly:
- ✓ Detects capabilities at init time, not per-frame
- ✓ Stores version in adapter/device context
- ✓ Passes version to serialization helpers
- ✓ Uses d3dx12.h fallback (doesn't reinvent serialization logic)
- ✓ Makes bindless a global optional feature, not per-PSO

---

## Migration Path for UzEngine

1. **Phase 1 (immediate)**: Add `D3D_ROOT_SIGNATURE_VERSION` feature detection in RHI init
2. **Phase 2**: Update root signature serialization to pass the detected version
3. **Phase 3**: Add bindless support (optional, SM 6.6+ only)
4. **Phase 4**: Decouple shader compilation from root signature creation (use same sig for all SMs)

**Current UzEngine status**:
- Root signature creation exists but doesn't query device capabilities
- No fallback for Win10 without Agility SDK
- No bindless path (not critical for current rendering, can be future phase)

---

## References

- **UE5 Root Signature**: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12RootSignature.cpp`
- **UE5 Adapter Init**: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12Adapter.cpp:1318-1324`
- **D3DX12 Serialization**: d3dx12.h (available in Windows SDK or DirectXShaderCompiler external)
- **D3D12 Spec**: [Root Signatures in HLSL (Microsoft Docs)](https://docs.microsoft.com/en-us/windows/desktop/direct3d12/root-signatures)
- **Bindless rendering**: Requires SM 6.6+, Resource Binding Tier 3, device support confirmation

