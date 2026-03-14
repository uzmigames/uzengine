# Render Hardware Interface (RHI)

Multi-backend GPU abstraction layer. DX12 and Vulkan backends are implemented; DX11 and Metal are planned.

> **Status**: Done (DX12 SM6.6 + Vulkan 1.3)
> **Source**: `runtime/engine/rhi/`

| Document | Topic |
|----------|-------|
| [01-overview.md](01-overview.md) | RHI design philosophy, backend architecture, handle types |
| [02-dx12-backend.md](02-dx12-backend.md) | DX12 backend implementation, SM6.6 bindless, shader model negotiation |
| [03-device-resources.md](03-device-resources.md) | Device creation, adapter selection, resource creation |
| [04-pipeline-state.md](04-pipeline-state.md) | PSO management, graphics and compute pipeline descriptors |
| [05-descriptors.md](05-descriptors.md) | Descriptor heaps, CBV/SRV/UAV/sampler management |
| [06-barriers.md](06-barriers.md) | Resource state transitions and synchronization barriers |
| [07-command-lists.md](07-command-lists.md) | Command list recording, submission, and synchronization |
| [08-swap-chain.md](08-swap-chain.md) | Swap chain creation, presentation, and resize handling |
| [09-debug-diagnostics.md](09-debug-diagnostics.md) | DRED breadcrumbs, validation layers, GPU debugging |
| [10-future-backends.md](10-future-backends.md) | Planned Vulkan, DX11, and Metal backend notes |
| [11-dx11-backend.md](11-dx11-backend.md) | DX11 compatibility backend design |
| [12-gpu-memory.md](12-gpu-memory.md) | GPU memory management, D3D12MA integration, heap types |
| [13-vulkan-backend.md](13-vulkan-backend.md) | Vulkan 1.3 backend implementation |
| [14-spirv-compilation.md](14-spirv-compilation.md) | SPIR-V compilation pipeline for Vulkan |
| [15-metal-backend.md](15-metal-backend.md) | Metal backend design (planned) |
