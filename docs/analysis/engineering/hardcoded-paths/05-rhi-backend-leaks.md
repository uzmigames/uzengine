# 05 — RHI Backend Type Leaks (DX12 Above `rhi.h`)

**Severity:** HIGH
**File focus:** `runtime/engine/renderer/rhi/`, any callsite of `rhi.h`

Per AGENTS.override.md and the RHI design: **no DX12/Vulkan/Metal types may appear above the RHI layer.** The engine code must only see opaque handles (`RHITexture*`, `RHIBuffer*`) and the `rhi_backend_t` vtable. Currently `d3d12.h` is pulled in via `dx12_device.h`, which is part of the public RHI surface.

## Findings

| # | Category | File:Line | Snippet | Why Bad | Fix |
|---|----------|-----------|---------|---------|-----|
| 5.1 | DX12 headers in public header | [dx12_device.h:28-31](../../../runtime/engine/renderer/rhi/dx12_device.h#L28-L31) | `#include <d3d12.h>` + `#include <dxgi1_6.h>` | Any TU including `dx12_device.h` pulls in all D3D12 types | Move to `.cpp`; use opaque handles in `.h` |
| 5.2 | `ComPtr` using declaration | [dx12_device.h:40](../../../runtime/engine/renderer/rhi/dx12_device.h#L40) | `using Microsoft::WRL::ComPtr;` | `ComPtr` template leaks `<wrl.h>` + ATL to consumers | Hide in `.cpp` impl |
| 5.3 | Descriptor heap config public | [dx12_device.h:47-56](../../../runtime/engine/renderer/rhi/dx12_device.h#L47-L56) | `kMaxCbvSrvUavDescriptors`, `kMaxSamplerDescriptors`, etc. | Public constants tie consumers to DX12 mental model | Move behind `RHIDeviceLimits` opaque struct |
| 5.4 | Format fallback hardcoded | [dx12_device.cpp:620-632](../../../runtime/engine/renderer/rhi/dx12_device.cpp#L620-L632) | Descriptor heap creation hardcodes sizes | No capability query if GPU reports < requested | `GetDescriptorHeapProperties()` check |
| 5.5 | PSO cache hot-path | [dx12_pso_creation.cpp:386](../../../runtime/engine/renderer/rhi/dx12_pso_creation.cpp#L386) | `"\\dxcompiler.dll"` string literal | See 03-content-paths.md; backend-specific DLL loading inline | Centralize in `rhi_dx12_loader.cpp` |
| 5.6 | DRED breadcrumb naming | Multiple locations | PIX labels may include sample-specific text | PIX marker labels should describe RHI concepts, not content | Generic pass names only |

## Boundary Audit

Run this grep across the codebase (excluding `runtime/engine/renderer/rhi/dx12_*`):

```
grep -rn "ID3D12\|D3D12_\|DXGI_\|ComPtr<" runtime/engine runtime/game
```

Any match above the RHI boundary is a violation.

## Current Public Surface (problematic)

File `dx12_device.h` currently exposes:
```cpp
#include <d3d12.h>
#include <dxgi1_6.h>
using Microsoft::WRL::ComPtr;

class DX12Device : public RHIDevice {
    ComPtr<ID3D12Device> m_device;     // LEAK — public member type is DX12
    // ...
};
```

Any file that `#includes "dx12_device.h"` (directly or transitively through `scene_renderer.h`, for example) now sees all of `<d3d12.h>`, meaning:

1. Engine code could write `ID3D12Device*` directly — bypassing the RHI
2. Cross-compilation to Vulkan/Metal is impossible without refactoring every consumer
3. ADR-style "no backend types above RHI" cannot be enforced

## Target Public Surface (clean)

### `rhi/rhi.h` — only engine-code-facing header
```cpp
#pragma once
#include <cstdint>

struct RHITexture;   // opaque
struct RHIBuffer;    // opaque
struct RHIPipeline;  // opaque

struct RHIDeviceLimits {
    uint32_t maxBindlessDescriptors;
    uint32_t maxSamplers;
    uint32_t maxRenderTargets;
    uint32_t maxDepthStencils;
};

struct rhi_backend_t {
    RHITexture* (*create_texture)(const RHITextureDesc&);
    void        (*destroy_texture)(RHITexture*);
    // ... vtable ...
};

rhi_backend_t* rhi();
```

### `rhi/dx12/dx12_device.cpp` — internal, never included above RHI
```cpp
#include <d3d12.h>          // OK here — this is the backend TU
#include <dxgi1_6.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

struct DX12Device {
    ComPtr<ID3D12Device> device;
    // ...
};

static DX12Device g_dx12;
static rhi_backend_t g_backend = { /* fill vtable */ };

rhi_backend_t* rhi_dx12_init() {
    // ...
    return &g_backend;
}
```

Consumers (`scene_renderer.cpp`, `gbuffer_pass.cpp`, etc.) never include DX12 headers. They only `#include "rhi/rhi.h"`.

## Migration Steps

1. **Audit** — `grep -rln 'ID3D12\|D3D12_\|ComPtr' runtime/` excluding `rhi/dx12_*`. Log all violations.
2. **Forward declare** — for each public RHI struct, forward-declare instead of including `<d3d12.h>`.
3. **Move implementation** — make `DX12Device` an opaque struct in the `.cpp`.
4. **Update consumers** — replace direct `ID3D12Device*` usage with RHI API calls.
5. **Enforce in CI** — add a lint step that fails the build if DX12 types leak above `rhi/dx12_*`.

## Descriptor Heap Sizes — Backend-Agnostic Config

The fixed heap sizes (`kMaxCbvSrvUavDescriptors = 1'000'000`) are a DX12-specific abstraction. Vulkan uses `VkDescriptorPool` with different sizing semantics; Metal uses `MTLArgumentBuffer`.

### Proposed abstraction
```cpp
// rhi.h
struct RHIDeviceDesc {
    uint32_t maxBindlessResources;   // SRV+UAV+CBV combined (DX12 merges, Vulkan separates)
    uint32_t maxSamplers;
    uint32_t maxRenderTargets;
    uint32_t maxDepthStencils;
};

RHIDevice* rhi_create_device(const RHIDeviceDesc& desc);
```

Each backend translates `RHIDeviceDesc` to its native allocation scheme.
