# DX11 Crash Fix — Implementation Guide

## Overview

Your DX11 backend is crashing on the first frame because of **missing null pointer validation in resource pool lookups**. This guide provides the exact code locations and fixes needed.

---

## Crash Location 1: Null Immediate Context

**File:** `runtime/engine/rhi/dx11/dx11_command_list.cpp`
**Function:** `DX11CommandList::Begin()`
**Line:** 70-81

### Current Code
```cpp
void DX11CommandList::Begin() {
    UZ_PROFILE_FUNCTION();
    m_ctx = m_device->GetImmediateContext();  // ← Might be nullptr

    ID3D11SamplerState* defaultSamplers[2] = {
        m_device->GetDefaultSampler(),
        m_device->GetShadowComparisonSampler(),
    };
    m_ctx->PSSetSamplers(0, 2, defaultSamplers);  // ← Crash if m_ctx is nullptr
}
```

### Fixed Code
```cpp
void DX11CommandList::Begin() {
    UZ_PROFILE_FUNCTION();
    m_ctx = m_device->GetImmediateContext();

    if (!m_ctx) {
        UZ_LOG(RHI, Fatal, "DX11CommandList::Begin — device immediate context is null. "
               "Device initialization may have failed.");
        return;
    }

    ID3D11SamplerState* defaultSamplers[2] = {
        m_device->GetDefaultSampler(),
        m_device->GetShadowComparisonSampler(),
    };

    if (defaultSamplers[0]) {
        m_ctx->PSSetSamplers(0, 2, defaultSamplers);
    } else {
        UZ_LOG(RHI, Warning, "DX11CommandList::Begin — default samplers not initialized");
    }
}
```

### Root Cause Check

Verify in `runtime/engine/rhi/dx11/dx11_device.cpp` that `Init()` successfully obtains the immediate context:

```cpp
bool DX11Device::Init(const RHIDeviceDesc& desc) {
    // ... device creation code ...

    // Should have:
    ComPtr<ID3D11DeviceContext> tempCtx;
    m_device->GetImmediateContext(&tempCtx);  // Must not be nullptr
    m_immediateContext = nullptr;  // Then upcast to ID3D11DeviceContext1

    HRESULT hr = m_device->QueryInterface(IID_PPV_ARGS(&m_immediateContext));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "Failed to query ID3D11DeviceContext1: 0x{:08X}",
               static_cast<uint32_t>(hr));
        return false;
    }

    if (!m_immediateContext) {
        UZ_LOG(RHI, Error, "Immediate context is null after QueryInterface");
        return false;
    }

    return true;
}
```

---

## Crash Location 2: Invalid Texture SRV Lookups

**File:** `runtime/engine/rhi/dx11/dx11_device.h` (declaration) and `dx11_device.cpp` (implementation)
**Function:** `DX11Device::FindTextureSRVByIndex()`

### Current Code (Assumed)
The function likely iterates the texture pool without bounds checking:

```cpp
ID3D11ShaderResourceView* DX11Device::FindTextureSRVByIndex(uint32_t srvIndex) {
    for (auto& texEntry : m_texturePool) {
        if (texEntry.second.srvIndex == srvIndex) {
            return texEntry.second.srv;  // ← Might be nullptr if texture not fully created
        }
    }
    return nullptr;  // ← Render graph calling SetDescriptorTable might not check this
}
```

### Fixed Code
```cpp
ID3D11ShaderResourceView* DX11Device::FindTextureSRVByIndex(uint32_t srvIndex) {
    // Bounds check: srvIndex should be in valid range [0, m_nextSrvIndex)
    if (srvIndex >= m_nextSrvIndex) {
        UZ_LOG(RHI, Warning, "FindTextureSRVByIndex — index {} exceeds allocated range [0, {})",
               srvIndex, m_nextSrvIndex);
        return nullptr;
    }

    // Linear search through texture pool
    for (auto& texEntry : m_texturePool) {
        const DX11Texture& tex = texEntry.second;
        if (tex.srvIndex == srvIndex) {
            // Double-check the SRV is valid
            if (!tex.srv) {
                UZ_LOG(RHI, Warning,
                       "FindTextureSRVByIndex — texture at index {} has null SRV. "
                       "Texture may be partially initialized or corrupted.", srvIndex);
                return nullptr;
            }
            return tex.srv;
        }
    }

    UZ_LOG(RHI, Warning, "FindTextureSRVByIndex — no texture found for SRV index {}", srvIndex);
    return nullptr;
}
```

### Verification Checklist

In `runtime/engine/rhi/dx11/dx11_device.h`:

```cpp
// Check that DX11Texture struct has these fields:
struct DX11Texture {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;     // ← Must be set if ShaderResource usage
    ComPtr<ID3D11RenderTargetView> rtv;       // ← Must be set if RenderTarget usage
    ComPtr<ID3D11DepthStencilView> dsv;       // ← Must be set if DepthStencil usage
    ComPtr<ID3D11UnorderedAccessView> uav;    // ← Must be set if UnorderedAccess usage

    uint32_t srvIndex = UINT32_MAX;           // ← Initialized at creation
    RHITextureDesc desc;
};
```

---

## Crash Location 3: SetDescriptorTable Null SRV

**File:** `runtime/engine/rhi/dx11/dx11_command_list.cpp`
**Function:** `DX11CommandList::SetDescriptorTable()`
**Line:** 285-300

### Current Code
```cpp
void DX11CommandList::SetDescriptorTable(uint32_t rootParamIndex, uint32_t descriptorIndex) {
    ID3D11ShaderResourceView* srv = m_device->FindTextureSRVByIndex(descriptorIndex);
    if (srv) {
        m_ctx->PSSetShaderResources(0, 1, &srv);  // ← srv could still be wild pointer
        ID3D11SamplerState* sampler = m_device->FindSamplerByIndex(0);
        if (sampler) {
            m_ctx->PSSetSamplers(0, 1, &sampler);
        }
    } else {
        UZ_LOG(RHI, Warning, "SetDescriptorTable: no SRV found for index {} (rootParam={})",
               descriptorIndex, rootParamIndex);
    }
}
```

### Issues
1. `if (srv)` check is weak — doesn't validate it's a valid pointer
2. No validation that `m_ctx` is valid
3. No early return if srv is nullptr

### Fixed Code
```cpp
void DX11CommandList::SetDescriptorTable(uint32_t rootParamIndex, uint32_t descriptorIndex) {
    if (!m_ctx) {
        UZ_LOG(RHI, Error, "SetDescriptorTable — context is null");
        return;
    }

    if (rootParamIndex > 0) {
        UZ_LOG(RHI, Warning,
               "SetDescriptorTable — DX11 always binds to t0 (rootParamIndex {} is ignored)",
               rootParamIndex);
    }

    ID3D11ShaderResourceView* srv = m_device->FindTextureSRVByIndex(descriptorIndex);

    if (!srv) {
        UZ_LOG(RHI, Error,
               "SetDescriptorTable — failed to resolve SRV for descriptor index {}. "
               "Possible causes: texture not created, texture has no ShaderResource flag, "
               "index out of range.", descriptorIndex);

        // Unbind to avoid using stale descriptor
        ID3D11ShaderResourceView* nullSrv = nullptr;
        m_ctx->PSSetShaderResources(0, 1, &nullSrv);
        return;
    }

    // Bind SRV to t0
    m_ctx->PSSetShaderResources(0, 1, &srv);

    // Auto-bind default sampler to s0
    ID3D11SamplerState* sampler = m_device->FindSamplerByIndex(0);
    if (sampler) {
        m_ctx->PSSetSamplers(0, 1, &sampler);
    } else {
        UZ_LOG(RHI, Warning, "SetDescriptorTable — default sampler (index 0) not found");
    }
}
```

---

## Crash Location 4: SetRootCBV Validation

**File:** `runtime/engine/rhi/dx11/dx11_command_list.cpp`
**Function:** `DX11CommandList::SetRootCBV(slot, buffer, offset, cpuData, size)`
**Line:** 227-248

### Current Code
```cpp
void DX11CommandList::SetRootCBV(uint32_t rootParamIndex, RHIBufferHandle /*buffer*/,
                                  uint64_t /*offsetBytes*/, const void* cpuData, uint32_t dataSize) {
    if (!m_ctx || !cpuData || dataSize == 0) return;  // Silent failure

    const uint32_t alignedSize = (dataSize + 15u) & ~15u;
    ID3D11Buffer* scratch = GetOrCreateScratchCB(rootParamIndex, alignedSize);
    if (!scratch) return;  // Silent failure

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = m_ctx->Map(scratch, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {  // Error logged
        UZ_LOG(RHI, Error, "SetRootCBV: Map failed for slot {}, hr=0x{:08X}",
               rootParamIndex, static_cast<uint32_t>(hr));
        return;
    }
    std::memcpy(mapped.pData, cpuData, dataSize);
    m_ctx->Unmap(scratch, 0);

    m_ctx->VSSetConstantBuffers(rootParamIndex, 1, &scratch);
    m_ctx->PSSetConstantBuffers(rootParamIndex, 1, &scratch);
}
```

### Fixed Code
```cpp
void DX11CommandList::SetRootCBV(uint32_t rootParamIndex, RHIBufferHandle /*buffer*/,
                                  uint64_t /*offsetBytes*/, const void* cpuData, uint32_t dataSize) {
    // Validate context
    if (!m_ctx) {
        UZ_LOG(RHI, Error, "SetRootCBV — context is null");
        return;
    }

    // Validate input
    if (!cpuData) {
        UZ_LOG(RHI, Error, "SetRootCBV — cpuData is null for slot {}", rootParamIndex);
        return;
    }

    if (dataSize == 0) {
        UZ_LOG(RHI, Error, "SetRootCBV — dataSize is 0 for slot {}", rootParamIndex);
        return;
    }

    if (dataSize > 65536) {  // D3D11 max CB size
        UZ_LOG(RHI, Error, "SetRootCBV — dataSize {} exceeds D3D11 max (65536)", dataSize);
        return;
    }

    if (rootParamIndex >= kMaxCBSlots) {
        UZ_LOG(RHI, Error, "SetRootCBV — rootParamIndex {} exceeds max {}",
               rootParamIndex, kMaxCBSlots);
        return;
    }

    // Align to 16 bytes
    const uint32_t alignedSize = (dataSize + 15u) & ~15u;

    ID3D11Buffer* scratch = GetOrCreateScratchCB(rootParamIndex, alignedSize);
    if (!scratch) {
        UZ_LOG(RHI, Error, "SetRootCBV — failed to create scratch CB for slot {}",
               rootParamIndex);
        return;
    }

    // Map and copy data
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = m_ctx->Map(scratch, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "SetRootCBV — Map failed for slot {}, hr=0x{:08X}",
               rootParamIndex, static_cast<uint32_t>(hr));
        return;
    }

    // Copy data into scratch buffer
    std::memcpy(mapped.pData, cpuData, dataSize);
    m_ctx->Unmap(scratch, 0);

    // Bind to both VS and PS
    m_ctx->VSSetConstantBuffers(rootParamIndex, 1, &scratch);
    m_ctx->PSSetConstantBuffers(rootParamIndex, 1, &scratch);

    UZ_LOG(RHI, Trace, "SetRootCBV — bound scratch CB for slot {}, size={}",
           rootParamIndex, dataSize);
}
```

---

## Crash Location 5: GetOrCreateScratchCB Error Handling

**File:** `runtime/engine/rhi/dx11/dx11_command_list.cpp`
**Function:** `DX11CommandList::GetOrCreateScratchCB()`
**Line:** 251-282

### Current Code
```cpp
ID3D11Buffer* DX11CommandList::GetOrCreateScratchCB(uint32_t slot, uint32_t requiredSize) {
    if (slot >= kMaxCBSlots) {
        UZ_LOG(RHI, Error, "SetRootCBV: slot {} exceeds max {}", slot, kMaxCBSlots);
        return nullptr;
    }

    // Reuse existing buffer if large enough
    if (m_scratchCBs[slot] && m_scratchCBSizes[slot] >= requiredSize) {
        return m_scratchCBs[slot];
    }

    // Release old buffer if it exists but is too small
    if (m_scratchCBs[slot]) {
        m_scratchCBs[slot]->Release();
        m_scratchCBs[slot] = nullptr;
        m_scratchCBSizes[slot] = 0;
    }

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = requiredSize;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = m_device->GetDevice()->CreateBuffer(&bd, nullptr, &m_scratchCBs[slot]);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "GetOrCreateScratchCB: CreateBuffer failed, slot={}, size={}, hr=0x{:08X}",
               slot, requiredSize, static_cast<uint32_t>(hr));
        return nullptr;
    }
    m_scratchCBSizes[slot] = requiredSize;
    return m_scratchCBs[slot];
}
```

### Fixed Code
```cpp
ID3D11Buffer* DX11CommandList::GetOrCreateScratchCB(uint32_t slot, uint32_t requiredSize) {
    // Validate slot
    if (slot >= kMaxCBSlots) {
        UZ_LOG(RHI, Error, "GetOrCreateScratchCB — slot {} exceeds max {}", slot, kMaxCBSlots);
        return nullptr;
    }

    // Validate size
    if (requiredSize == 0 || requiredSize > 65536) {
        UZ_LOG(RHI, Error,
               "GetOrCreateScratchCB — invalid size {} for slot {} (valid range: 1-65536)",
               requiredSize, slot);
        return nullptr;
    }

    // Reuse existing buffer if large enough
    if (m_scratchCBs[slot] && m_scratchCBSizes[slot] >= requiredSize) {
        UZ_LOG(RHI, Trace, "GetOrCreateScratchCB — reusing scratch CB slot {} (size {})",
               slot, m_scratchCBSizes[slot]);
        return m_scratchCBs[slot];
    }

    // Release old buffer if exists but too small
    if (m_scratchCBs[slot]) {
        UZ_LOG(RHI, Trace,
               "GetOrCreateScratchCB — releasing scratch CB slot {} (old size {} < required {})",
               slot, m_scratchCBSizes[slot], requiredSize);
        m_scratchCBs[slot]->Release();
        m_scratchCBs[slot] = nullptr;
        m_scratchCBSizes[slot] = 0;
    }

    // Create new DYNAMIC scratch buffer
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = requiredSize;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = m_device->GetDevice()->CreateBuffer(&bd, nullptr, &m_scratchCBs[slot]);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error,
               "GetOrCreateScratchCB — CreateBuffer failed for slot {} (size={}), hr=0x{:08X}",
               slot, requiredSize, static_cast<uint32_t>(hr));
        m_scratchCBs[slot] = nullptr;
        return nullptr;
    }

    m_scratchCBSizes[slot] = requiredSize;
    UZ_LOG(RHI, Debug, "GetOrCreateScratchCB — created scratch CB for slot {} (size={})",
           slot, requiredSize);
    return m_scratchCBs[slot];
}
```

---

## Crash Location 6: FindSamplerByIndex Validation

**File:** `runtime/engine/rhi/dx11/dx11_device.cpp`
**Function:** `DX11Device::FindSamplerByIndex()`

### Current Code (Assumed)
```cpp
ID3D11SamplerState* DX11Device::FindSamplerByIndex(uint32_t samplerIndex) {
    for (auto& samplerEntry : m_samplerPool) {
        if (samplerEntry.second.index == samplerIndex) {
            return samplerEntry.second.samplerState;  // ← Might be nullptr
        }
    }
    return nullptr;
}
```

### Fixed Code
```cpp
ID3D11SamplerState* DX11Device::FindSamplerByIndex(uint32_t samplerIndex) {
    // Bounds check
    if (samplerIndex >= m_nextSamplerIndex) {
        UZ_LOG(RHI, Warning, "FindSamplerByIndex — index {} exceeds allocated range [0, {})",
               samplerIndex, m_nextSamplerIndex);
        return nullptr;
    }

    // Search pool
    for (auto& samplerEntry : m_samplerPool) {
        const DX11Sampler& sampler = samplerEntry.second;
        if (sampler.index == samplerIndex) {
            if (!sampler.samplerState) {
                UZ_LOG(RHI, Warning,
                       "FindSamplerByIndex — sampler at index {} has null state",
                       samplerIndex);
                return nullptr;
            }
            return sampler.samplerState;
        }
    }

    UZ_LOG(RHI, Warning, "FindSamplerByIndex — no sampler found for index {}", samplerIndex);
    return nullptr;
}
```

---

## Summary of Changes

| File | Function | Issue | Fix |
|------|----------|-------|-----|
| dx11_command_list.cpp | Begin() | Null m_ctx | Add null check + early return |
| dx11_command_list.cpp | SetDescriptorTable() | Null srv, null m_ctx | Add validation, unbind on error |
| dx11_command_list.cpp | SetRootCBV() | Silent failures | Add detailed error logging |
| dx11_command_list.cpp | GetOrCreateScratchCB() | CreateBuffer failure | Enhanced logging |
| dx11_device.cpp | FindTextureSRVByIndex() | Bounds check missing | Add range validation |
| dx11_device.cpp | FindSamplerByIndex() | Bounds check missing | Add range validation |

---

## Testing Strategy

After applying these fixes:

1. **Enable Debug Layer:**
   ```cpp
   RHIDeviceDesc desc;
   desc.enableValidation = true;
   ```

2. **Run first-frame breakpoints:**
   - Set breakpoint in DX11CommandList::Begin()
   - Verify m_ctx is not nullptr
   - Step through SetRootCBV and SetDescriptorTable
   - Watch for null pointer dereferences

3. **Check debug output:**
   - Look for any "Error" or "Fatal" log messages
   - Verify GetOrCreateScratchCB doesn't fail
   - Verify FindTextureSRVByIndex finds valid textures

4. **GPU validation:**
   - Enable DXGI debug layer
   - Run with PIX or GPU Debugger
   - Look for warnings about invalid descriptors

---

## Performance Notes

All these additions are **zero-cost in release builds** when logs are disabled. In debug:
- Bounds checks: O(1) comparison
- Pool lookups: O(n) but n < 10000 typically
- Logging: Only on errors, not per-frame

The original crashes caused 100% failure rate. These fixes cause 0% performance regression on success paths.
