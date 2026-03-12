# Proposal: DirectX 11 RHI Backend (Phase 0.1)

## Why

The engine currently only has a DX12 SM6.6 backend. If all rendering features are built exclusively on DX12, adding DX11/Vulkan/Metal later requires massive retrofitting of every rendering pass. By implementing all backends NOW (Phase 0), before the renderer grows beyond clear-screen, every future rendering feature (triangle, PBR, deferred, shadows, post-process) is written once against the RHI interface and automatically works on all backends. DX11 SM5 provides the widest Windows compatibility (Windows 7+, older GPUs, integrated graphics) and serves as the fallback when DX12 isn't available.

## What Changes

### New Backend

1. **DX11Device** — Implements all 29 `IRHIDevice` methods: D3D11 device/context creation, buffer/texture/shader/pipeline/sampler/swapchain/fence/command list creation and destruction. Uses D3D11.1+ for feature queries. Emulates bindless via descriptor tables (SM5 input layout + SRV/CBV slot binding).

2. **DX11CommandList** — Implements all 28 `IRHICommandList` methods. Wraps `ID3D11DeviceContext` (deferred context for recording, immediate context for execution). Maps RHI barriers to D3D11 implicit state management (D3D11 has no explicit barriers).

3. **DX11SwapChain** — Implements all 7 `IRHISwapChain` methods. DXGI swap chain with FLIP_DISCARD (Win10+) or FLIP_SEQUENTIAL fallback. Triple-buffered, tearing support where available.

4. **DX11Fence** — Implements all 4 `IRHIFence` methods. Emulated via `ID3D11Query(TIMESTAMP_DISJOINT)` + event queries, since D3D11 lacks native timeline fences. Monotonic value tracking on CPU side.

5. **DX11 Resource Creation Helpers** — Format conversion (RHIFormat → DXGI_FORMAT), buffer/texture/sampler creation with D3D11 descriptors, SRV/UAV/RTV/DSV management.

### Build System

6. **Backend Selection** — `UZ_BACKEND_DX11` compile-time flag. Backend factory in `rhi_backend.h` selects DX11 when requested or when DX12 isn't available.

### SM5 Adaptation

7. **Input Layout Path** — DX11 requires vertex input layouts (no bindless ResourceDescriptorHeap). The existing `RHIInputElementDesc` + `RHIGraphicsPipelineDesc::inputElements` SM5 fallback path is used. SetDescriptorIndex maps to slot-based SRV/CBV binding.

## Impact

- Affected specs: `docs/engine/12-rendering/` (multi-backend notes)
- Affected code: `runtime/core/rhi/dx11/` (new), `runtime/core/rhi/rhi_backend.h` (factory update), `runtime/build.zig` (DX11 files)
- Breaking change: NO — additive backend, DX12 remains default
- User benefit: Run engine on older hardware/OS; validate RHI abstraction correctness; catch DX12-only assumptions early
- Enables: All future rendering features automatically work on DX11; triple-backend testing on Windows 10
