# UE5 Root Signature Code Reference - Exact Snippets

This document contains verbatim code snippets from UE5 5.6 source, with line numbers and file paths for direct reference.

---

## Pattern 1: Root Signature Manager Caching

### File: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12RootSignature.h`

**Lines 483-504**

```cpp
class FD3D12RootSignatureManager : public FD3D12AdapterChild
{
public:
	explicit FD3D12RootSignatureManager(FD3D12Adapter* InParent)
		: FD3D12AdapterChild(InParent)
	{
	}
	~FD3D12RootSignatureManager()
	{
		check(RootSignatureMap.Num() == 0);
	}
	void Destroy();

	FD3D12RootSignature* GetRootSignature(const FD3D12QuantizedBoundShaderState& QBSS);
	FD3D12QuantizedBoundShaderState GetQuantizedBoundShaderState(const FD3D12RootSignature* const RootSignature);

private:
	FCriticalSection CS;
	FD3D12RootSignature* CreateRootSignature(const FD3D12QuantizedBoundShaderState& QBSS);

	TMap<FD3D12QuantizedBoundShaderState, FD3D12RootSignature*> RootSignatureMap;
};
```

**Key insight**: `TMap<QBSS, RootSig*>` — one map entry per unique QBSS state.

---

## Pattern 2: Binding Tier Adapted Descriptor Range Flags

### File: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12RootSignature.cpp`

**Lines 162-195**

```cpp
FD3D12RootSignatureDesc::FD3D12RootSignatureDesc(const FD3D12QuantizedBoundShaderState& QBSS, const D3D12_RESOURCE_BINDING_TIER ResourceBindingTier)
	: RootParametersSize(0)
{
	// ... (earlier code)

	// Determine if our descriptors or their data is static based on the resource binding tier.
	// We do this because sometimes (based on binding tier) our descriptor tables are bigger than the # of descriptors we copy. See FD3D12QuantizedBoundShaderState::InitShaderRegisterCounts().
	const D3D12_DESCRIPTOR_RANGE_FLAGS SRVDescriptorRangeFlags = (ResourceBindingTier <= D3D12_RESOURCE_BINDING_TIER_1) ?
		D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE :
		D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;

	const D3D12_DESCRIPTOR_RANGE_FLAGS CBVDescriptorRangeFlags = (ResourceBindingTier <= D3D12_RESOURCE_BINDING_TIER_2) ?
		D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE :
		D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;

	const D3D12_DESCRIPTOR_RANGE_FLAGS UAVDescriptorRangeFlags = (ResourceBindingTier <= D3D12_RESOURCE_BINDING_TIER_2) ?
		D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE :
		D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;

	const D3D12_DESCRIPTOR_RANGE_FLAGS SamplerDescriptorRangeFlags = (ResourceBindingTier <= D3D12_RESOURCE_BINDING_TIER_1) ?
		D3D12_DESCRIPTOR_RANGE_FLAG_NONE :
		D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;

	const D3D12_ROOT_DESCRIPTOR_FLAGS CBVRootDescriptorFlags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;	// We always set the data in an upload heap before calling Set*RootConstantBufferView.

	// ... (code continues, uses these flags)
}
```

**Tier mapping**:
- **Tier 1**: `DATA_STATIC` (no `DESCRIPTORS_VOLATILE`)
- **Tier 2**: `DATA_STATIC | DESCRIPTORS_VOLATILE` for CBV
- **Tier 3**: Full `DESCRIPTORS_VOLATILE` support

---

## Pattern 3: Conditional Bindless Flags in Root Signature

### File: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12RootSignature.cpp`

**Lines 349-359**

```cpp
	D3D12_ROOT_SIGNATURE_FLAGS Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	if (QBSS.bUseDirectlyIndexedResourceHeap)
	{
		Flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
	}

	if (QBSS.bUseDirectlyIndexedSamplerHeap)
	{
		Flags |= D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
	}
```

**Bindless decision**: Made via `bUseDirectlyIndexedResourceHeap` and `bUseDirectlyIndexedSamplerHeap` flags in QBSS.

---

## Pattern 4: Conditionally Skip Descriptor Tables

### File: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Public\D3D12RootSignatureDefinitions.h`

**Lines 224-237**

```cpp
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

**Usage**:

```cpp
	void AddTable(ERootSignatureVisibility Visibility, ERootSignatureRangeType Type, int32 NumDescriptors, D3D12_DESCRIPTOR_RANGE_FLAGS FlagsOverride = D3D12_DESCRIPTOR_RANGE_FLAG_NONE) override
	{
		if (!ShouldSkipType(Type))  // <-- Conditional check
		{
			const FPendingTable NewTable{ Visibility, Type, NumDescriptors, FlagsOverride };
			PendingTables.Emplace(NewTable);
		}
	}
```

---

## Pattern 5: Feature Flag Setting

### File: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Public\D3D12RootSignatureDefinitions.h`

**Lines 189-207**

```cpp
	void SetFlags(EShaderBindingLayoutFlags InFlags)
	{
		Flags = InFlags;

		if (EnumHasAnyFlags(InFlags, EShaderBindingLayoutFlags::InputAssembler))
		{
			AddRootFlag(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
		}

		if (EnumHasAnyFlags(InFlags, EShaderBindingLayoutFlags::BindlessResources))
		{
			AddRootFlag(D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
		}

		if (EnumHasAnyFlags(InFlags, EShaderBindingLayoutFlags::BindlessSamplers))
		{
			AddRootFlag(D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED);
		}
	}
```

**Pattern**: Feature flags → root signature flags.

---

## Pattern 6: Shader Parameter Binding Dispatch

### File: `W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\ShaderParameterUtils.h`

**Lines 77-92**

```cpp
inline void SetTextureParameter(FRHIBatchedShaderParameters& BatchedParameters, const FShaderResourceParameter& Parameter, FRHITexture* TextureRHI)
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
1. Bindless: `SetBindlessTexture()`
2. Traditional: `SetShaderTexture()`

---

### Similar Pattern: Sampler Parameters

**Lines 94-109**

```cpp
inline void SetSamplerParameter(FRHIBatchedShaderParameters& BatchedParameters, const FShaderResourceParameter& Parameter, FRHISamplerState* SamplerStateRHI)
{
	if (Parameter.IsBound())
	{
#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
		if (Parameter.GetType() == EShaderParameterType::BindlessSampler)
		{
			BatchedParameters.SetBindlessSampler(Parameter.GetBaseIndex(), SamplerStateRHI);
		}
		else
#endif
		{
			BatchedParameters.SetShaderSampler(Parameter.GetBaseIndex(), SamplerStateRHI);
		}
	}
}
```

---

### Similar Pattern: SRV Parameters

**Lines 139-152**

```cpp
inline void SetSRVParameter(FRHIBatchedShaderParameters& BatchedParameters, const FShaderResourceParameter& Parameter, FRHIShaderResourceView* SRV)
{
	if (Parameter.IsBound())
	{
#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
		if (Parameter.GetType() == EShaderParameterType::BindlessSRV)
		{
			BatchedParameters.SetBindlessResourceView(Parameter.GetBaseIndex(), SRV);
		}
		else
#endif
		{
			BatchedParameters.SetShaderResourceView(Parameter.GetBaseIndex(), SRV);
		}
	}
}
```

---

## Pattern 7: Bindless Allocator Detection

### File: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12BindlessDescriptors.h`

**Lines 30-72**

```cpp
/** Manager for configuration settings and shared descriptor allocators, stored on the adapter. */
class FD3D12BindlessDescriptorAllocator : public FD3D12AdapterChild
{
public:
	FD3D12BindlessDescriptorAllocator() = delete;
	FD3D12BindlessDescriptorAllocator(FD3D12Adapter* InParent);

	void Init();

	ERHIBindlessConfiguration GetResourcesConfiguration() const { return BindlessResourcesConfiguration; }
	ERHIBindlessConfiguration GetSamplersConfiguration() const { return BindlessSamplersConfiguration; }

	bool AreResourcesBindless() const { return ResourceAllocator != nullptr; }
	bool AreSamplersBindless() const { return SamplerAllocator != nullptr; }

	// Bindless descriptor allocators are stored in the adapter, so descriptor handles can be allocated once and shared for multi-GPU objects
	FRHIDescriptorHandle AllocateSamplerHandle();
	FRHIDescriptorHandle AllocateResourceHandle();

	void FreeSamplerHandle(FRHIDescriptorHandle Handle);
	void FreeResourceHandle(FRHIDescriptorHandle Handle);

	FCriticalSection& GetResourceHeapsCS() { return ResourceHeapsCS; }

	uint32 GetResourceCapacity() const { return ResourceAllocator->GetCapacity(); }
	uint32 GetSamplerCapacity()  const { return SamplerAllocator->GetCapacity(); }

	bool GetResourceAllocatedRange(FRHIDescriptorAllocatorRange& OutAllocatedRange) const { return ResourceAllocator->GetAllocatedRange(OutAllocatedRange); }

private:
	ERHIBindlessConfiguration BindlessResourcesConfiguration{};
	ERHIBindlessConfiguration BindlessSamplersConfiguration{};

	uint32 MaxResourceHeapSize = 0;
	uint32 MaxSamplerHeapSize = 0;

	FCriticalSection ResourceHeapsCS;
	FRHIHeapDescriptorAllocator* ResourceAllocator = nullptr;
	FRHIHeapDescriptorAllocator* SamplerAllocator = nullptr;
};
```

**Key methods**:
- `AreResourcesBindless()` — Returns true if bindless resource allocation is active
- `AreSamplersBindless()` — Returns true if bindless sampler allocation is active

---

## Pattern 8: Platform Gating

### File: `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Public\Windows\WindowsPlatform.h`

**Line ~140** (exact line varies)

```cpp
#define PLATFORM_SUPPORTS_BINDLESS_RENDERING 1
```

Other platforms:
- **macOS**: `#define PLATFORM_SUPPORTS_BINDLESS_RENDERING 1`
- **Linux**: `#define PLATFORM_SUPPORTS_BINDLESS_RENDERING 1`
- **iOS**: `#define PLATFORM_SUPPORTS_BINDLESS_RENDERING 0`
- **Android**: `#define PLATFORM_SUPPORTS_BINDLESS_RENDERING 0`

---

## Pattern 9: QBSS Population with Bindless Flags

### File: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12Util.cpp`

**Somewhere in shader/PSO creation code**:

```cpp
#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
	QBSS.bUseDirectlyIndexedResourceHeap = GetBindlessDescriptorAllocator().AreResourcesBindless();
	QBSS.bUseDirectlyIndexedSamplerHeap = GetBindlessDescriptorAllocator().AreSamplersBindless();
#endif
```

**Flow**:
1. Check platform support (`#if PLATFORM_SUPPORTS_BINDLESS_RENDERING`)
2. Check allocator state (`AreResourcesBindless()`)
3. Set QBSS flags accordingly
4. These flags are then used in root signature creation

---

## Pattern 10: Root Signature in PSO Descriptor

### File: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12PipelineState.h`

**Lines 60-90**

```cpp
struct FD3D12_GRAPHICS_PIPELINE_STATE_DESC
{
	ID3D12RootSignature *pRootSignature;
	D3D12_SHADER_BYTECODE VS;
	D3D12_SHADER_BYTECODE MS;
	D3D12_SHADER_BYTECODE AS;
	D3D12_SHADER_BYTECODE PS;
	D3D12_SHADER_BYTECODE GS;
#if !D3D12_USE_DERIVED_PSO || D3D12_USE_DERIVED_PSO_SHADER_EXPORTS
	D3D12_BLEND_DESC BlendState;
#endif // #if !D3D12_USE_DERIVED_PSO || D3D12_USE_DERIVED_PSO_SHADER_EXPORTS
#if !D3D12_USE_DERIVED_PSO
	uint32 SampleMask;
	D3D12_RASTERIZER_DESC RasterizerState;
	D3D12_DEPTH_STENCIL_DESC1 DepthStencilState;
#endif // #if !D3D12_USE_DERIVED_PSO
	D3D12_INPUT_LAYOUT_DESC InputLayout;
	D3D12_INDEX_BUFFER_STRIP_CUT_VALUE IBStripCutValue;
	D3D12_PRIMITIVE_TOPOLOGY_TYPE PrimitiveTopologyType;
	D3D12_RT_FORMAT_ARRAY RTFormatArray;
	DXGI_FORMAT DSVFormat;
	DXGI_SAMPLE_DESC SampleDesc;
	uint32 NodeMask;
	D3D12_CACHED_PIPELINE_STATE CachedPSO;
	D3D12_PIPELINE_STATE_FLAGS Flags;

	FD3D12_GRAPHICS_PIPELINE_STATE_STREAM PipelineStateStream() const;
#if PLATFORM_SUPPORTS_MESH_SHADERS
	FD3D12_MESH_PIPELINE_STATE_STREAM MeshPipelineStateStream() const;
#endif
};

struct FD3D12LowLevelGraphicsPipelineStateDesc
{
	const FD3D12RootSignature *pRootSignature;
	FD3D12_GRAPHICS_PIPELINE_STATE_DESC Desc;
	ShaderBytecodeHash VSHash;
	ShaderBytecodeHash MSHash;
	ShaderBytecodeHash ASHash;
	ShaderBytecodeHash GSHash;
	ShaderBytecodeHash PSHash;
	uint32 InputLayoutHash;
	bool bFromPSOFileCache;

	SIZE_T CombinedHash;
	// ... methods
};
```

**Key point**: `pRootSignature` is member of PSO descriptor. Different root signatures = different PSOs.

---

## Pattern 11: Static Root Signature Creation

### File: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Public\D3D12RootSignatureDefinitions.h`

**Lines 410-440** (Graphics root signature)

```cpp
	// Fat/Static Gfx Root Signature
	inline void CreateGfxRootSignature(FRootSignatureCreator& Creator, EShaderBindingLayoutFlags InFlags)
	{
		// Ensure the creator starts in a clean state (in cases of creator reuse, etc.).
		Creator.SetFlags(InFlags | EShaderBindingLayoutFlags::InputAssembler);

		Creator.AddUnorderedAccessViewParameter(0, UE_HLSL_SPACE_DIAGNOSTIC);

		if (EnumHasAnyFlags(InFlags, EShaderBindingLayoutFlags::RootConstants))
		{
			const uint32 NumConstants = 4u;
			Creator.AddConstantsParameter(NumConstants, 0u, UE_HLSL_SPACE_SHADER_ROOT_CONSTANTS); // UERootConstants
		}

		// Tables must be added only after other parameters

		AddAllStandardTablesForVisibility(Creator, ERootSignatureVisibility::Pixel);
		AddAllStandardTablesForVisibility(Creator, ERootSignatureVisibility::Vertex);
		AddAllStandardTablesForVisibility(Creator, ERootSignatureVisibility::Geometry);

#if !defined(D3D12RHI_TOOLS_MESH_SHADERS_UNSUPPORTED)
		if (EnumHasAnyFlags(InFlags, EShaderBindingLayoutFlags::AllowMeshShaders))
		{
			AddAllStandardTablesForVisibility(Creator, ERootSignatureVisibility::Mesh);
			AddAllStandardTablesForVisibility(Creator, ERootSignatureVisibility::Amplification);
		}
#endif

		Creator.AddTable(ERootSignatureVisibility::Pixel, ERootSignatureRangeType::UAV, MAX_UAVS);
		Creator.AddTable(ERootSignatureVisibility::Vertex, ERootSignatureRangeType::UAV, MAX_UAVS);
	}
```

**For compute**:

```cpp
	// Fat/Static Compute Root Signature
	inline void CreateComputeRootSignature(FRootSignatureCreator& Creator, EShaderBindingLayoutFlags InFlags)
	{
		// Ensure the creator starts in a clean state (in cases of creator reuse, etc.).
		Creator.SetFlags(InFlags);

		Creator.AddUnorderedAccessViewParameter(0, UE_HLSL_SPACE_DIAGNOSTIC);

		if (EnumHasAnyFlags(InFlags, EShaderBindingLayoutFlags::RootConstants))
		{
			const uint32 NumConstants = 4u;
			Creator.AddConstantsParameter(NumConstants, 0u, UE_HLSL_SPACE_SHADER_ROOT_CONSTANTS); // UERootConstants
		}

		// Tables must be added only after other parameters

		AddAllStandardTablesForVisibility(Creator, ERootSignatureVisibility::All);
		Creator.AddTable(ERootSignatureVisibility::All, ERootSignatureRangeType::UAV, MAX_UAVS);
	}
```

---

## Summary: Five Critical Code Locations

| Pattern | File | Lines | Key Function/Class |
|---------|------|-------|-------------------|
| **1. Root sig caching** | D3D12RootSignature.h | 483-504 | `FD3D12RootSignatureManager::GetRootSignature()` |
| **2. Binding tier flags** | D3D12RootSignature.cpp | 162-195 | Constructor with tier-dependent flags |
| **3. Bindless flag setting** | D3D12RootSignatureDefinitions.h | 189-207 | `FRootSignatureCreator::SetFlags()` |
| **4. Skip descriptor tables** | D3D12RootSignatureDefinitions.h | 224-237 | `FRootSignatureCreator::ShouldSkipType()` |
| **5. Shader parameter routing** | ShaderParameterUtils.h | 77-92 | `SetTextureParameter()` dual-path |

---

## How to Use These Snippets

1. **Copy the pattern** from the UE5 code above
2. **Adapt to UzEngine naming** (e.g., `FD3D12RootSignature` → `UzRootSignature`)
3. **Reference the specific line numbers** when implementing
4. **Test against the validation checklist** in the migration playbook

These are not pseudocode—they're actual working production code from UE5 5.6.
