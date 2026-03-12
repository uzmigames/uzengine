# RHI — DX12 Backend

> **Status**: Done (Milestone 1.2)
> **Source**: `runtime/core/rhi/dx12/dx12_device.h/.cpp`
> **Depends on**: RHI interface (`rhi.h`, `rhi_types.h`), D3D12MA, directx-headers (SDK 614)

## Purpose

The DX12 backend is the primary RHI implementation. It translates all `IRHIDevice`, `IRHICommandList`, `IRHISwapChain`, and `IRHIFence` calls into Direct3D 12 API calls. All DX12 headers are confined to files under `runtime/core/rhi/dx12/` — nothing else in the engine includes `d3d12.h` or `dxgi1_6.h`.

## Architecture

```
DX12Device (IRHIDevice)
├── IDXGIFactory6          — DXGI adapter enumeration
├── IDXGIAdapter4          — Selected GPU adapter
├── ID3D12Device           — Core D3D12 device (feature level 12_0+)
├── D3D12MA::Allocator     — Memory allocator (AMD D3D12MA v3.1.0)
├── Descriptor Heaps
│   ├── CBV_SRV_UAV (1M, shader-visible)  — Bindless resource access
│   ├── Sampler     (2048, shader-visible) — Texture samplers
│   ├── RTV         (256, CPU-only)        — Render target views
│   └── DSV         (64, CPU-only)         — Depth-stencil views
├── Command Queues
│   ├── Direct (Graphics + Compute + Copy)
│   └── Copy   (DMA transfers only)
├── Root Signature         — Global bindless root signature
├── Resource Pools
│   ├── ResourcePool<DX12Buffer>
│   ├── ResourcePool<DX12Texture>
│   ├── ResourcePool<DX12Shader>
│   ├── ResourcePool<DX12Sampler>
│   └── ResourcePool<DX12Pipeline>
└── Debug/Diagnostics
    ├── DX12GpuProfiler    — Timestamp queries
    └── DX12GpuBreadcrumbs — Crash-site markers
```

## Initialization Sequence

Device creation follows a strict order — each step depends on the previous:

```
1. Debug Layer         EnableDebugLayer(gpuValidation)
       │                └── ID3D12Debug::EnableDebugLayer()
       ▼                └── ID3D12Debug1::SetEnableGPUBasedValidation()
2. DXGI Factory        CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG)
       ▼
3. Adapter Selection   EnumAdapterByGpuPreference(DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE)
       ▼
4. Device Creation     D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0)
       ▼
5. Shader Model        NegotiateShaderModel()
       │                └── Probe SM6.7 → SM6.6 → ... → SM5.0
       ▼
6. D3D12MA Allocator   D3D12MA::CreateAllocator()
       ▼
7. Descriptor Heaps    CreateDescriptorHeap() × 4
       ▼
8. Command Queues      CreateCommandQueue(DIRECT) + CreateCommandQueue(COPY)
       ▼
9. Root Signature      CreateRootSignature() (global bindless layout)
       ▼
10. DRED (optional)    EnableDRED() for device-removal diagnostics
```

### Why This Order

- **Debug layer must be enabled before device creation** — D3D12 requires it
- **Adapter selection before device** — device is created on a specific adapter
- **Shader model after device** — probing requires a live device to query feature support
- **D3D12MA after device** — it wraps the ID3D12Device
- **Descriptor heaps before command queues** — the root signature references heap types
- **DRED last** — it's optional diagnostics, doesn't affect core functionality

## Third-Party Dependencies

| Library | Version | Purpose | Integration |
|---------|---------|---------|-------------|
| **directx-headers** | SDK 614 | D3D12/DXGI headers for Clang/MinGW compatibility | Header-only, `thirdparty/directx_headers/` |
| **D3D12MA** | v3.1.0 | GPU memory allocation (suballocation, defragmentation) | Source compiled in `dx12_d3d12ma.cpp` |
| **Agility SDK** | Latest | D3D12Core.dll for latest runtime features | Deployed to `zig-out/bin/D3D12/` |

### Why directx-headers Instead of Windows SDK

The standard Windows SDK headers have two issues for UzEngine's Zig/Clang build:

1. **Struct-return ABI mismatch**: MinGW/Clang use a different calling convention for struct returns than MSVC. The `directx-headers` package (SDK 614) handles this transparently with `#if defined(_MSC_VER)` guards.

2. **Version**: Zig's built-in MinGW headers are SDK 602 — too old for our SM6.6 features. `directx-headers` SDK 614 has full SM6.7 support.

### Why D3D12MA

DX12 requires manual memory management — `CreateCommittedResource` works but wastes memory on small allocations. D3D12MA provides:

- **Suballocation**: Many small resources share a single 64KB/4MB heap page
- **Budget tracking**: Respects OS-reported VRAM budget via `IDXGIAdapter3::QueryVideoMemoryInfo`
- **Defragmentation**: Compacts sparse allocations (planned, not yet used)
- **Named allocations**: Debug builds tag each allocation for memory budget tools

**Engine Comparison:**
- **UE5**: Custom `FD3D12ResourceAllocator` with manual block/pool management. More complex, more control, more code.
- **Godot**: Uses `CreateCommittedResource` for most allocations (simpler, less memory-efficient).
- **UzEngine**: D3D12MA — best of both worlds. Production-quality allocator, zero custom code to maintain.

## DX12 Compatibility Notes

### Base Interface Requirement

UzEngine uses only base D3D12 interfaces — never versioned ones:

```cpp
// ✅ CORRECT — works on all DX12 drivers
ID3D12Device*                device;
ID3D12GraphicsCommandList*   cmdList;
ID3D12CommandQueue*          queue;

// ❌ WRONG — requires specific Agility SDK version, breaks on older drivers
ID3D12Device10*              device;
ID3D12GraphicsCommandList7*  cmdList;
```

### GUID Validation (Critical Lesson)

During Milestone 1.2 development, a GUID typo in `dx12_guids.h` caused `QueryInterface` to fail silently. The IID for `ID3D12CommandAllocator` had `0x6102dee8` instead of the correct `0x6102dee4`. This was only caught by comparing byte-for-byte against `thirdparty/directx_headers/include/directx/d3d12.h`.

**Rule**: When adding new DX12 GUIDs, always validate against the `DEFINE_GUID` in the authoritative header. Never transcribe GUIDs manually.

## Performance

Benchmark on NVIDIA RTX 4090 (Milestone 1.2 clear screen demo):

| Metric | Value |
|--------|-------|
| Frame rate | 6069 FPS (0.16ms/frame) |
| Pipeline | Window → Device → SwapChain → CmdList → Fences → Render → Present |
| Validation | D3D12 Debug Layer + GPU Validation enabled during development |
| Test count | 183 tests passing |

## Rules

1. **NEVER** include `d3d12.h` or `dxgi1_6.h` outside `runtime/core/rhi/dx12/`
2. **NEVER** use versioned interfaces (`ID3D12Device10`, `ID3D12GraphicsCommandList7`) — use base interfaces only
3. **ALWAYS** validate new GUIDs against `thirdparty/directx_headers/include/directx/d3d12.h`
4. **ALWAYS** use `directx-headers` (SDK 614), never Zig's built-in MinGW headers (SDK 602)
5. **ALWAYS** use D3D12MA for resource allocation, never raw `CreateCommittedResource`
