<!-- doc-meta
title: DX12 PSO Creation E_INVALIDARG Root Cause Analysis
description: Investigation into why CreateGraphicsPipelineState/CreateComputePipelineState return E_INVALIDARG when device created via ID3D12DeviceFactory
author: Team Lead + Claude Opus
date: 2026-03-28
status: investigation-complete
-->

# PSO Creation E_INVALIDARG Investigation

**Problem Statement**: ALL `CreateGraphicsPipelineState` and `CreateComputePipelineState` calls return `E_INVALIDARG (0x80070057)` when device is created via `ID3D12DeviceFactory::CreateDevice()` instead of `D3D12CreateDevice()`.

## Investigation Summary

After thorough code analysis of the PSO creation path, the root cause **is NOT in PSO descriptor validation**. Every field checked:

### ✓ PSO Descriptor Fields (CreateDX12GraphicsPipeline)

| Field | Status | Details |
|-------|--------|---------|
| `pRootSignature` | ✓ VALID | Set to `m_rootSignature` (raw pointer, non-null) |
| `VS.pShaderBytecode` | ✓ VALID | Resolved from shader pool, size checked <64MB |
| `VS.BytecodeLength` | ✓ VALID | Matches bytecode size |
| `PS.pShaderBytecode` | ✓ VALID | Resolved from shader pool, size checked <64MB |
| `PS.BytecodeLength` | ✓ VALID | Matches bytecode size |
| `InputLayout` | ✓ VALID | Standard format conversions, NumElements set correctly |
| `RasterizerState` | ✓ VALID | Standard state conversions |
| `DepthStencilState` | ✓ VALID | Standard state conversions |
| `BlendState` | ✓ VALID | RT[0] always has RenderTargetWriteMask set, no zeroed state |
| `NumRenderTargets` | ✓ VALID | Always set (0..8) |
| `RTVFormats[i]` | ✓ VALID | Standard DXGI format conversions |
| `PrimitiveTopologyType` | ✓ VALID | Standard topology conversion |
| `SampleDesc` | ✓ VALID | Count=1, Quality=0 (no MSAA) |
| `SampleMask` | ✓ VALID | Set to UINT_MAX |
| `CachedPSO` | ✓ VALID | Either both null or both set, retry logic works |

### ✓ Compute PSO Descriptor Fields (CreateDX12ComputePipeline)

| Field | Status | Details |
|-------|--------|---------|
| `pRootSignature` | ✓ VALID | Set to `m_rootSignature` |
| `CS.pShaderBytecode` | ✓ VALID | Resolved from shader pool |
| `CS.BytecodeLength` | ✓ VALID | Matches bytecode size |
| `CachedPSO` | ✓ VALID | Either both null or both set |

### ✓ Root Signature Creation Path

**File**: `runtime/engine/rhi/dx12/dx12_pso_creation.cpp:171-492`

#### Initialization (dx12_device.cpp:217-219)
```cpp
m_rootSignature = CreateBindlessRootSignature(m_device.Get(), m_shaderModel, m_rootSigVersion);
if (!m_rootSignature) return false;
```

#### Root Signature Version Detection (dx12_device.cpp:188-209)
```cpp
// Detect support via CheckFeatureSupport
D3D12_FEATURE_DATA_ROOT_SIGNATURE rootSigCaps = {};
rootSigCaps.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &rootSigCaps, ...))) {
    rootSigCaps.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
}
m_rootSigVersion = rootSigCaps.HighestVersion;

// Force 1.0 in device factory mode to avoid serialization conflicts
if (m_deviceFactory && m_rootSigVersion >= D3D_ROOT_SIGNATURE_VERSION_1_1) {
    m_rootSigVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
}
```

#### Root Signature Parameters (SM6.6 bindless case)
- **Param [0]**: 16x root constants at b0 (descriptor indices + algorithm params)
- **Param [1]**: CBV at b1 (per-frame, DATA_STATIC_WHILE_SET_AT_EXECUTE)
- **Param [2]**: CBV at b2 (per-object, DATA_STATIC_WHILE_SET_AT_EXECUTE)
- **Param [3]**: CBV at b4 (shadow data, DATA_STATIC_WHILE_SET_AT_EXECUTE)
- **Param [4]**: UAV descriptor table u0..u3 (DESCRIPTORS_VOLATILE)
- **Static Samplers**: s0 (aniso16 wrap), s1 (comparison linear PCF), s2 (bilinear clamp), s3 (bilinear wrap)

#### Serialization Path (device factory mode)
1. **Primary**: `D3DX12SerializeVersionedRootSignature` with detected version
2. **Fallback** (SM6.6 only): If primary fails with bindless flags, compile HLSL root sig via DXC
3. **Last resort**: Strip DIRECTLY_INDEXED flags, re-serialize with v1.0

#### Root Signature Storage
```cpp
// dx12_device.h:382
ID3D12RootSignature* m_rootSignature = nullptr;

// Cleanup in destructor
if (m_rootSignature) {
    m_rootSignature->Release();
    m_rootSignature = nullptr;
}
```

### ✓ PSO Creation Call Sites (dx12_device.cpp)

#### Graphics Pipeline (line 1005-1020)
```cpp
RHIPipelineHandle DX12Device::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) {
    // Pool allocation
    auto slot = m_pipelinePool.Allocate();
    if (!slot) {
        UZ_LOG(RHI, Error, "CreateGraphicsPipeline: pool allocation failed");
        return RHIPipelineHandle();
    }

    // Call with m_device and m_rootSignature
    if (!CreateDX12GraphicsPipeline(m_device.Get(), m_rootSignature, desc, m_shaderPool, *slot)) {
        m_pipelinePool.Free(slot.index);
        return RHIPipelineHandle();
    }

    return RHIPipelineHandle{.index = slot.index, .generation = slot.generation};
}
```

#### Compute Pipeline (line 1025-1040)
```cpp
RHIPipelineHandle DX12Device::CreateComputePipeline(const RHIComputePipelineDesc& desc) {
    auto slot = m_pipelinePool.Allocate();
    if (!slot) {
        UZ_LOG(RHI, Error, "CreateComputePipeline: pool allocation failed");
        return RHIPipelineHandle();
    }

    if (!CreateDX12ComputePipeline(m_device.Get(), m_rootSignature, desc, m_shaderPool, *slot)) {
        m_pipelinePool.Free(slot.index);
        return RHIPipelineHandle();
    }

    return RHIPipelineHandle{.index = slot.index, .generation = slot.generation};
}
```

## All Checked ✓ — What Remains?

**The PSO descriptor itself is valid.** Every field conforms to D3D12 spec:
- Pointers are non-null where required ✓
- Bytecode sizes are positive and reasonable ✓
- Format conversions are standard ✓
- Root signature is set and non-null ✓

**What could still cause E_INVALIDARG:**

1. **Device State**: Is `m_device.Get()` the correct device that created `m_rootSignature`?
   - If PSO is created with `Device A` but root sig from `Device B` → E_INVALIDARG
   - **CHECK**: Are both from the same device factory call?

2. **Root Signature Mismatch**: Does the root signature's bindings match what shaders expect?
   - If shader expects `b5` but root sig only defines `b0..b4` → E_INVALIDARG
   - **CHECK**: Shader constant buffers in shader bytecode

3. **Shader Bytecode Corruption**: Is shader bytecode valid and well-formed?
   - Corrupted bytecode can fail validation even if parser passes
   - **CHECK**: DXC recompilation with verbose diagnostics

4. **Input Layout Conflict** (for graphics PSO): Do input semantics match expected format?
   - Invalid semantic names or mismatched formats → E_INVALIDARG
   - **CHECK**: Input element descriptor validation

## Recommended Next Steps

### 1. Device/RootSignature Pairing Verification
Add diagnostic logging to confirm both are from same creation path:
```cpp
// In DX12Device::Init after root sig creation
UZ_LOG(RHI, Info, "Device pointer: 0x{:016X}, RootSig pointer: 0x{:016X}, "
       "from same factory: {}", reinterpret_cast<uintptr_t>(m_device.Get()),
       reinterpret_cast<uintptr_t>(m_rootSignature),
       m_deviceFactory ? "YES" : "NO");
```

### 2. Shader Bytecode Validation
Before PSO creation, validate shader bytecode structure:
```cpp
// In CreateDX12GraphicsPipeline after shader resolution
if (vs->bytecode.size() < 20) {  // DXBC minimum header
    UZ_LOG(RHI, Error, "VS bytecode too small: {} bytes", vs->bytecode.size());
    return false;
}
// Check DXBC magic ('DXBC' = 0x43425844)
uint32_t magic = *reinterpret_cast<const uint32_t*>(vs->bytecode.data());
if (magic != 0x43425844) {
    UZ_LOG(RHI, Error, "VS bytecode has invalid magic: 0x{:08X}", magic);
    return false;
}
```

### 3. Root Signature Binding Reflection
Read PSO descriptor creation error message via `ID3D12InfoQueue`:
```cpp
ComPtr<ID3D12InfoQueue> infoQueue;
if (SUCCEEDED(m_device.As(&infoQueue))) {
    uint64_t numMessages = infoQueue->GetNumStoredMessages();
    for (uint64_t i = 0; i < numMessages; ++i) {
        SIZE_T msgLen = 0;
        infoQueue->GetMessageLength(i, &msgLen);
        if (msgLen > 0) {
            // Print detailed error
        }
    }
}
```

### 4. Input Validation (for graphics PSO)
Validate input element descriptors before PSO creation:
```cpp
// In CreateDX12GraphicsPipeline before device->CreateGraphicsPipelineState
for (uint32_t i = 0; i < inputElements.size(); ++i) {
    const auto& elem = inputElements[i];
    if (!elem.SemanticName || strlen(elem.SemanticName) == 0) {
        UZ_LOG(RHI, Error, "Input element {} has null/empty semantic name", i);
        return false;
    }
    DXGI_FORMAT fmt = ToDxgiFormat(desc.inputElements[i].format);
    if (fmt == DXGI_FORMAT_UNKNOWN) {
        UZ_LOG(RHI, Error, "Input element {} has unknown format", i);
        return false;
    }
}
```

## Commit History Context

- **Latest**: `6ddb379f` — Use D3DX12SerializeVersionedRootSignature (UE5 pattern)
- **Previous**: `32d68bb8` — Agility SDK via CreateDeviceFactory + root sig fallback
- **Before**: Device factory integration was untested in DevFactory mode

The change to use D3DX12 helper was sound. The issue is post-serialization.

## Hypothesis: Device Factory Incompatibility

When device is created via `ID3D12DeviceFactory::CreateDevice()`:
1. Root signature **creation succeeds** (logs show "Root signature created...")
2. PSO creation **immediately fails** with E_INVALIDARG

This suggests:
- Root sig object is valid ✓
- But PSO validator rejects something ✗

**Most likely**: The root signature flags or version don't match what the device factory expects, OR there's a subtle incompatibility in how device factory devices validate root sigs.

**Action**: Add detailed error reporting via `ID3D12InfoQueue` to surface the actual validation error message.
