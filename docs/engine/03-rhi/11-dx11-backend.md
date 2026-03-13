# RHI — DX11 Backend

> **Status**: Planned
> **Source**: `runtime/engine/rhi/dx11/dx11_device.h/.cpp`, `dx11_command_list.h/.cpp`, `dx11_swapchain.h`, `dx11_fence.h`, `dx11_resources.h`
> **Depends on**: RHI interface (Done), Shader Pipeline (FXC compiler), Config system
> **References**: UE5 legacy support pattern

## Purpose

The DX11 backend provides legacy GPU support for hardware that lacks Direct3D 12. It implements `IRHIDevice`, `IRHICommandList`, `IRHISwapChain`, and `IRHIFence` using the Direct3D 11.1 API with Shader Model 5.0. This is the **lowest-priority fallback path** for minimum hardware requirements.

```
┌─────────────────────────────────────┐
│    RHI Interface Layer              │
│    (same for all backends)          │
├─────────────────────────────────────┤
│ DX12 (SM6.6) │ DX12 (SM5.0) │ DX11 │
└─────────────────────────────────────┘
        │           │          │
        ▼           ▼          ▼
    D3D12 Device  D3D12 Device  D3D11 Device
    (Feature 12) (Feature 12) (Feature 11.1)
```

## Architectural Differences: DX12 vs DX11

| Aspect | DX12 | DX11 |
|--------|------|------|
| **Memory management** | Explicit (D3D12MA allocator) | Implicit (driver manages) |
| **Descriptor heaps** | Explicit (CBV_SRV_UAV, RTV, DSV, Sampler) | None (views created per-resource) |
| **Root signature** | Explicit layout | None (register-based binding) |
| **Command model** | Explicit lists + queues | Immediate context + deferred contexts |
| **State management** | Monolithic PSOs | Individual state objects |
| **Barriers** | Explicit (`ResourceBarrier`) | Implicit (driver manages) |
| **Bindless shaders** | Native (ResourceDescriptorHeap[index]) | No (slot-based binding) |
| **Shader model** | SM6.6+ (DX12 primary), SM5.1+ fallback | SM5.0 only (via FXC) |

**Key insight**: DX11 is significantly simpler but less efficient. The RHI abstraction hides these differences so application code is backend-agnostic.

## Shader Model — SM5.0 Only

DX11 supports only Shader Model 5.0, compiled via **FXC** (`d3dcompiler_47.dll`). DXC cannot target SM5.0, so the shader pipeline routes SM5.0 targets to FXC:

```cpp
// In ShaderPipeline::CompileShader()
static bool RequiresDXC(ShaderModel model) {
    return model >= ShaderModel::SM6_0;  // SM6.0+ uses DXC
}

if (RequiresDXC(model)) {
    return m_compilerDXC->Compile(desc);
} else {
    return m_compilerFXC->Compile(desc);  // SM5.0 → FXC → DXBC
}
```

**No bindless rendering** on DX11. Shaders use traditional slot-based binding:
- `Texture2D g_textures[16] : register(t0, space0);`
- `SamplerState g_samplers[8] : register(s0, space0);`
- `Buffer<float4> g_buffers[16] : register(t16, space0);`

## Device Creation

The DX11Device initializes the D3D11.1 stack in this order:

### 1. Debug Layer (Optional)

```cpp
bool DX11Device::EnableDebugLayer() {
    ComPtr<ID3D11Debug> debug;
    if (SUCCEEDED(D3D11CreateDevice(..., D3D11_CREATE_DEVICE_DEBUG, ...))) {
        // Debug layer enabled
    }
}
```

- Enabled if `RHIDeviceDesc::validation == true`
- Reports DX11 errors to the debug output window

### 2. DXGI Factory & Adapter Selection

```cpp
bool DX11Device::CreateFactory() {
    HRESULT hr = CreateDXGIFactory2(
        DXGI_CREATE_FACTORY_DEBUG,  // Debug output
        IID_PPV_ARGS(&m_factory)
    );
    return SUCCEEDED(hr);
}

bool DX11Device::SelectAdapter(uint32_t adapterIndex) {
    // Use adapterIndex to enumerate adapters
    // Preference: high-performance GPU (NVIDIA > Intel iGPU)
}
```

### 3. D3D11 Device Creation

```cpp
bool DX11Device::CreateDevice() {
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    HRESULT hr = D3D11CreateDevice(
        m_adapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,  // No software device
        flags,    // D3D11_CREATE_DEVICE_DEBUG if validation enabled
        featureLevels, ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &m_device,
        &m_featureLevel,
        &m_immediateContext
    );

    // Query ID3D11Device1 / ID3D11DeviceContext1 for modern features
    if (m_device) {
        m_device->QueryInterface(IID_PPV_ARGS(&m_device1));
        m_immediateContext->QueryInterface(IID_PPV_ARGS(&m_immediateContext1));
    }

    return SUCCEEDED(hr);
}
```

### Feature Level vs Shader Model

| D3D_FEATURE_LEVEL | Shader Model | GPU Requirements |
|-----------------|---------|--|
| 11_1 | SM5.0+ | Windows 8.1+ |
| 11_0 | SM5.0+ | Windows Vista+ with WDDM 1.2 |
| 10_1 | SM4.x | Windows Vista+ with WDDM 1.1 |
| 10_0 | SM4.0 | Windows Vista+ |

The DX11Device always negotiates to **SM5.0** (hard-coded), regardless of feature level.

## Resource Management

Unlike DX12's explicit memory allocator, DX11 resources are created directly and managed implicitly by the driver.

### Buffers

```cpp
RHIBufferHandle DX11Device::CreateBuffer(const RHIBufferDesc& desc) {
    D3D11_BUFFER_DESC bd = {
        .ByteWidth = (UINT)desc.size,
        .Usage = D3D11_USAGE_DEFAULT,  // GPU read/write
        .BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER
                   | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
        .CPUAccessFlags = 0,
        .StructureByteStride = desc.stride,
    };

    if (desc.flags & RHIBufferFlags::Upload) {
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    } else if (desc.flags & RHIBufferFlags::Readback) {
        bd.Usage = D3D11_USAGE_STAGING;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    }

    ID3D11Buffer* buffer = nullptr;
    HRESULT hr = m_device->CreateBuffer(&bd, nullptr, &buffer);

    // Assign SRV index for slot-based shader binding
    uint32_t srvIndex = m_nextSrvIndex++;

    // Create SRV if needed
    if (desc.flags & RHIBufferFlags::ShaderResource) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = { /* ... */ };
        ID3D11ShaderResourceView* srv = nullptr;
        m_device->CreateShaderResourceView(buffer, &srvDesc, &srv);
        // Store srv in DX11Buffer
    }

    // Return handle to slot in resource pool
    return m_bufferPool.Add(DX11Buffer{buffer, srv, ...});
}
```

### Textures

```cpp
RHITextureHandle DX11Device::CreateTexture(const RHITextureDesc& desc) {
    D3D11_TEXTURE2D_DESC td = {
        .Width = desc.width,
        .Height = desc.height,
        .MipLevels = desc.mipLevels,
        .ArraySize = 1,
        .Format = ToD3D11Format(desc.format),
        .SampleDesc = {1, 0},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = GetBindFlags(desc.flags),
        .CPUAccessFlags = 0,
    };

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = m_device->CreateTexture2D(&td, nullptr, &texture);

    // Create views based on flags
    ID3D11ShaderResourceView* srv = nullptr;
    if (desc.flags & RHITextureFlags::ShaderResource) {
        m_device->CreateShaderResourceView(texture, nullptr, &srv);
    }

    ID3D11RenderTargetView* rtv = nullptr;
    if (desc.flags & RHITextureFlags::RenderTarget) {
        m_device->CreateRenderTargetView(texture, nullptr, &rtv);
    }

    ID3D11DepthStencilView* dsv = nullptr;
    if (desc.flags & RHITextureFlags::DepthStencil) {
        m_device->CreateDepthStencilView(texture, nullptr, &dsv);
    }

    // Assign SRV index
    uint32_t srvIndex = m_nextSrvIndex++;

    return m_texturePool.Add(DX11Texture{texture, srv, rtv, dsv, srvIndex, ...});
}
```

### No Descriptor Heaps

DX11 has no descriptor heaps. Instead, views are created per-resource and stored in the resource itself. This trades memory efficiency (per-resource views) for simplicity (no heap management).

## Command Recording & Execution

DX11 uses a deferred-context pattern rather than explicit command lists.

### Begin() → Record → End() → Execute()

```cpp
void DX11CommandList::Begin() {
    // Create fresh deferred context
    HRESULT hr = m_device->GetDevice()->CreateDeferredContext1(
        0,
        reinterpret_cast<ID3D11DeviceContext1**>(&m_deferredCtx)
    );
    if (FAILED(hr)) {
        // Fallback to base interface
        m_device->GetDevice()->CreateDeferredContext(0, &m_deferredCtx);
    }
}

void DX11CommandList::End() {
    // Finalize deferred context into command list
    m_deferredCtx->FinishCommandList(FALSE, &m_pendingCmdList);
    // FinishCommandList() doesn't execute — it just serializes commands
}

void DX11CommandList::Execute() {
    // Submit finalized command list to immediate context
    m_device->GetImmediateContext()->ExecuteCommandList(m_pendingCmdList, FALSE);
    m_pendingCmdList->Release();
    m_pendingCmdList = nullptr;
}
```

**Key difference from DX12:**
- DX12: Record commands on any thread, submit via command queue
- DX11: Record on deferred context, execute via immediate context (main thread only)

### Recording Commands

Commands are recorded the same way as DX12 — via the `IRHICommandList` interface. The deferred context mirrors the immediate context's API:

```cpp
void DX11CommandList::SetPipeline(RHIPipelineHandle pipeline) {
    auto* pso = m_device->GetPipelinePool().Get(pipeline);

    // Bind individual state objects
    m_deferredCtx->IASetInputLayout(pso->inputLayout);
    m_deferredCtx->IASetPrimitiveTopology(pso->topology);
    m_deferredCtx->VSSetShader(pso->vertexShader, nullptr, 0);
    m_deferredCtx->PSSetShader(pso->pixelShader, nullptr, 0);
    m_deferredCtx->RSSetState(pso->rasterizer);
    m_deferredCtx->OMSetDepthStencilState(pso->depthStencil, 0);

    float blendFactor[4] = {1, 1, 1, 1};
    m_deferredCtx->OMSetBlendState(pso->blend, blendFactor, 0xFFFFFFFF);
}

void DX11CommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                   uint32_t firstIndex, int32_t vertexOffset,
                                   uint32_t firstInstance) {
    m_deferredCtx->DrawIndexedInstanced(indexCount, instanceCount, firstIndex,
                                         vertexOffset, firstInstance);
}
```

## Barriers — No-Op

DX11 has no explicit resource barriers. State transitions are implicit:

```cpp
void DX11CommandList::ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) {
    // Intentional no-op
    // Driver automatically inserts hazard barriers
}
```

The RHI interface requires this method for DX12 compatibility, but on DX11 it does nothing. The driver silently manages state transitions.

## Fence Emulation

DX11 has no native timeline fences. The `DX11Fence` emulates them using event queries:

```cpp
class DX11Fence : public IRHIFence {
    struct PendingQuery {
        ID3D11Query* query;  // D3D11_QUERY_EVENT
        uint64_t value;      // Associated fence value
    };

    std::vector<PendingQuery> m_pending;
    uint64_t m_completedValue = 0;
    uint64_t m_lastSignaledValue = 0;
};

void DX11Fence::Signal(uint64_t value) {
    // Must be monotonically increasing
    assert(value > m_lastSignaledValue);
    m_lastSignaledValue = value;

    // Create event query on immediate context
    D3D11_QUERY_DESC qd = {D3D11_QUERY_EVENT, 0};
    ID3D11Query* query = nullptr;
    m_device->GetDevice()->CreateQuery(&qd, &query);

    // Insert marker: GPU sets query when all preceding work completes
    m_device->GetImmediateContext()->End(query);

    m_pending.push_back({query, value});
}

void DX11Fence::WaitCPU(uint64_t value) {
    while (m_completedValue < value) {
        PollPending();  // Check query results
        if (m_completedValue < value) {
            Sleep(0);  // Yield CPU
        }
    }
}

void DX11Fence::PollPending() {
    for (auto& pending : m_pending) {
        HRESULT hr = m_device->GetImmediateContext()->GetData(
            pending.query, nullptr, 0, 0);  // Non-blocking check
        if (hr == S_OK) {
            // Query complete — GPU reached this point
            m_completedValue = pending.value;
        }
    }
}
```

**Performance note**: This is much slower than DX12's native fences. Typical usage: once per frame for frame pacing. Do not use for intra-frame synchronization.

## Swapchain

The DX11SwapChain wraps `IDXGISwapChain1` with FLIP_DISCARD presentation:

```cpp
class DX11SwapChain : public IRHISwapChain {
    ComPtr<IDXGISwapChain1> m_swapChain;
    RHITextureHandle m_backBufferHandles[3];  // Triple-buffered
    uint32_t m_currentBackBuffer = 0;
};

bool DX11SwapChain::Init(DX11Device* device, const RHISwapChainDesc& desc) {
    DXGI_SWAP_CHAIN_DESC1 scd = {
        .Width = desc.width,
        .Height = desc.height,
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
        .Stereo = FALSE,
        .SampleDesc = {1, 0},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = 3,  // Triple-buffered
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,  // Modern presentation
        .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
        .Flags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0,
    };

    IDXGISwapChain1* swapChain = nullptr;
    HRESULT hr = device->GetFactory()->CreateSwapChainForHwnd(
        device->GetImmediateContext(),  // Queue (not used on DX11)
        hwnd, &scd, nullptr, nullptr, &swapChain);

    m_swapChain = swapChain;
    AcquireBackBuffers();  // Create RTVs for back buffers
    return SUCCEEDED(hr);
}

void DX11SwapChain::AcquireBackBuffers() {
    for (uint32_t i = 0; i < 3; ++i) {
        ID3D11Texture2D* backBuffer = nullptr;
        m_swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer));

        ID3D11RenderTargetView* rtv = nullptr;
        m_device->GetDevice()->CreateRenderTargetView(backBuffer, nullptr, &rtv);

        // Register with texture pool
        m_backBufferHandles[i] = m_device->GetTexturePool().Add(DX11Texture{
            backBuffer, nullptr, rtv, nullptr, UINT32_MAX, ...
        });
    }
}

void DX11SwapChain::Present(bool vsync) {
    UINT flags = 0;
    if (!vsync && m_tearingSupported) {
        flags |= DXGI_PRESENT_ALLOW_TEARING;
    }
    UINT syncInterval = vsync ? 1 : 0;

    m_swapChain->Present(syncInterval, flags);
    m_currentBackBuffer = (m_currentBackBuffer + 1) % 3;
}
```

## Input Layout & Vertex Binding

Unlike DX12 bindless, DX11 requires explicit Input Assembler layout:

```cpp
// In pipeline creation
D3D11_INPUT_ELEMENT_DESC layout[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

ID3D11InputLayout* inputLayout = nullptr;
m_device->CreateInputLayout(layout, 3, vsBlob->GetBufferPointer(),
                            vsBlob->GetBufferSize(), &inputLayout);

// Commands reference this layout
m_deferredCtx->IASetInputLayout(inputLayout);
```

## Backend Selection

Users can force DX11 backend via CLI flag:

```bash
uz-engine --rhi dx11 --shader-model sm5.0
```

Or in `.uzproject`:

```toml
[render]
rhi = "dx11"
shader_model = "sm5.0"
```

### Auto-Fallback (Not Yet Implemented)

A future feature may auto-fallback to DX11 if DX12 device creation fails, but this is **not** in the current plan.

## Limitations vs DX12

| Feature | DX12 | DX11 |
|---------|------|------|
| **Bindless shaders** | Yes (SM6.6) | No |
| **Compute dispatch** | Full (CS 5.0+) | Limited (CS 5.0) |
| **UAVs** | Yes | Limited |
| **Async compute** | Yes | No |
| **Root constants** | Yes (native) | Emulated (constant buffers) |
| **Resource barriers** | Explicit (required) | Implicit (no-op) |
| **Timeline fences** | Native (efficient) | Emulated (slow) |
| **DRED diagnostics** | Full (GPU page faults) | Minimal |
| **Multithreaded recording** | Yes (parallel command lists) | No (immediate context only) |

## Implementation Files

```
runtime/engine/rhi/dx11/
├── dx11_device.h/.cpp         — IRHIDevice via ID3D11Device
├── dx11_command_list.h/.cpp   — IRHICommandList via ID3D11DeviceContext (deferred)
├── dx11_swapchain.h           — IRHISwapChain via IDXGISwapChain1
├── dx11_fence.h               — IRHIFence via D3D11_QUERY_EVENT emulation
└── dx11_resources.h           — DX11Buffer, DX11Texture, DX11Pipeline structs
```

## Configuration & Compilation

### Prerequisites

- **OS**: Windows 10+ (DX11.1 runtime)
- **Compiler**: MSVC or Clang-CL (Windows SDK 10.0+)
- **Dependencies**: directx-headers (GUID definitions)

### Header Dependencies

```cpp
#include <d3d11_1.h>       // Core D3D11 interface
#include <dxgi1_6.h>       // DXGI factory/swapchain
#include <wrl/client.h>    // WRL::ComPtr smart pointers
```

### Build (Zig)

In `runtime/build.zig`, DX11 backend is compiled when `target.os.tag == .windows`:

```zig
// DX11 backend sources
const dx11_src = [_][]const u8{
    "engine/rhi/dx11/dx11_device.cpp",
    "engine/rhi/dx11/dx11_command_list.cpp",
    "engine/rhi/dx11/dx11_swapchain.cpp",
    "engine/rhi/dx11/dx11_fence.cpp",
};
```

## Phase Roadmap

| Phase | Milestone | What | Priority |
|-------|-----------|------|----------|
| 1.5 | FXC Compiler | SM5.0 compilation via FXC | High (enables DX12 SM5.0 fallback) |
| 2.0 | DX11 Device | Device creation, buffer/texture/pipeline | Medium (legacy GPU support) |
| 2.0 | DX11 Command Lists | Deferred context recording + barriers | Medium |
| 2.0 | DX11 Swapchain | FLIP_DISCARD presentation | Medium |
| 3.0+ | DX11 Diagnostics | Debug layer, shader validation | Low |

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| **DX11 support** | Full backend (`D3D11RHI`) with async compute, texture streaming | Old GLES3 renderer (not DX11) | Minimal fallback (SM5.0 only) |
| **Deferred contexts** | Used for async compute | N/A | Used for command recording |
| **Fence emulation** | Native (DX11 provides `ID3D11Query` helpers) | N/A | Manual event query polling |
| **Descriptor views** | Per-resource (like our approach) | N/A | Per-resource |
| **State delta optimization** | Yes (tracks state) | N/A | No (resends all state) |

## Rules

1. **ALWAYS route SM5.0 to FXC** — DXC cannot compile SM5.0
2. **NEVER use DX12 API calls** in DX11 backend — keep them isolated
3. **ALWAYS check `generation` field** when accessing pooled resources
4. **NEVER assume async compute** — DX11 compute is limited compared to DX12
5. **ALWAYS poll fences** before blocking — the emulation is slow, use sparingly
6. **NEVER mix deferred/immediate context** — each command list has one deferred context
7. **ALWAYS destroy resources explicitly** before device destruction
8. **Resource barriers are no-ops** — don't rely on them for correctness, the driver manages state
