# RHI — Future Backends (Vulkan, Metal, DX11)

> **Status**: Planned
> **Depends on**: RHI interface (Done), DX12 backend (Done, reference implementation)

## Purpose

The RHI interface layer is designed to support multiple backends. DX12 SM6.6 is the primary implementation. Three additional backends are planned to expand platform support and hardware compatibility.

## Backend Roadmap

| Priority | Backend | Target Platform | Shader Format | Status |
|----------|---------|----------------|---------------|--------|
| 1 | **DX12 SM6.6** | Windows (modern GPUs) | DXIL | **Done** |
| 2 | **DX12 SM5** | Windows (older DX12 GPUs) | DXIL (SM5.x) | Planned |
| 3 | **DX11** | Windows (legacy GPUs) | DXBC | Planned |
| 4 | **Vulkan** | Linux, Steam Deck, Windows | SPIR-V | Planned |
| 5 | **Metal** | macOS, iOS | MSL | Planned |

## DX12 SM5 Fallback

For GPUs that support DX12 but not SM6.6 (e.g., NVIDIA GTX 900 series, AMD RX 400 series):

**Key Differences from SM6.6 Path:**
- **No bindless**: Traditional descriptor tables + root CBV binding instead of `ResourceDescriptorHeap[]`
- **Input Assembler**: Vertex layout described via `RHIInputElementDesc[]` in pipeline descriptors
- **Root signature**: Per-material root signatures (possibly) instead of a single global one
- **Shader variants**: SM5.x DXIL bytecode compiled alongside SM6.6 variants

The device's `GetShaderModel()` and `IsBindless()` queries determine which path to use. The renderer branches once at the frame level, not per-draw-call.

## Vulkan Backend

Primary target: Linux, Steam Deck, and cross-platform Windows development.

**Implementation Plan:**
- `rhi_vulkan.cpp` implementing `IRHIDevice`, `IRHICommandList`, `IRHISwapChain`, `IRHIFence`
- SPIR-V shader bytecode (compiled from HLSL via DXC's `-spirv` flag)
- `VMA` (Vulkan Memory Allocator) for memory management — AMD's allocator, same author as D3D12MA
- Descriptor set layout matching UzEngine's bindless model (descriptor indexing extension)
- Vulkan 1.3 minimum — for dynamic rendering, synchronization2, maintenance4

**Vulkan Equivalents:**

| UzEngine RHI | DX12 | Vulkan |
|-------------|------|--------|
| `IRHIDevice` | `ID3D12Device` | `VkDevice` + `VkInstance` + `VkPhysicalDevice` |
| `IRHICommandList` | `ID3D12GraphicsCommandList` | `VkCommandBuffer` |
| `IRHISwapChain` | `IDXGISwapChain4` | `VkSwapchainKHR` |
| `IRHIFence` | `ID3D12Fence` | `VkFence` + `VkSemaphore` (timeline) |
| `RHIResourceState` | `D3D12_RESOURCE_STATES` | `VkImageLayout` / `VkAccessFlags2` |
| Descriptor heap | `ID3D12DescriptorHeap` | `VkDescriptorPool` + descriptor indexing |
| Root signature | `ID3D12RootSignature` | `VkPipelineLayout` + `VkDescriptorSetLayout` |

**Key Challenge:** Vulkan requires explicit synchronization via pipeline barriers (`VkPipelineBarrier2`) with stage masks and access masks — more granular than DX12's `ResourceBarrier`. The `RHIResourceState` enum maps to Vulkan access/stage pairs internally.

## Metal Backend

Target: macOS and iOS.

**Implementation Plan:**
- `rhi_metal.cpp` implementing all four RHI interfaces
- MSL shader bytecode (transpiled from SPIR-V via SPIRV-Cross)
- `MTKView` for swap chain, `MTLDevice` for resource creation
- Metal 3 minimum — for mesh shaders, ray tracing (when needed)

**Shader Pipeline:**
```
HLSL → DXC (-spirv) → SPIR-V → SPIRV-Cross → MSL → Metal compiler
```

## DX11 Backend

Absolute minimum fallback for very old hardware. Full specification including FXC compiler integration, D3D11 interface mapping, and implementation files is in [11-dx11-backend](11-dx11-backend.md).

**Summary:**
- `rhi/dx11/` implementing RHI interfaces via D3D11 API
- DXBC shader bytecode compiled via FXC (`d3dcompiler_47.dll`) — **not DXC** (DXC can't compile SM5.0)
- SM5.0 shaders only — no bindless, no compute dispatch (limited), no async compute
- FXC compiler module (`ShaderCompilerFXC`) is also used by the DX12 SM5.0 fallback path
- Feature-reduced: no UAV (limited), no structured buffers (limited), no mesh shaders, no ray tracing

This backend is lowest priority and may be skipped. The FXC compiler integration (step 1-7 in the implementation order) is the immediate priority as it also enables DX12 SM5.0 fallback.

## Cross-Compilation Shader Pipeline

All backends share a single HLSL source. The shader pipeline produces backend-specific bytecode:

```
HLSL Source (.hlsl)
       │
       ├─── DXC (SM6.6) ──→ DXIL ──→ DX12 SM6.6 (bindless)
       ├─── DXC (SM6.0) ──→ DXIL ──→ DX12 SM6.0 (wave ops, no bindless)
       ├─── FXC (SM5.0) ──→ DXBC ──→ DX12 SM5.0 (legacy DX12)
       ├─── FXC (SM5.0) ──→ DXBC ──→ DX11 SM5.0 (legacy backend)
       ├─── DXC (-spirv) ──→ SPIR-V ──→ Vulkan
       └─── DXC (-spirv) ──→ SPIR-V ──→ SPIRV-Cross ──→ MSL ──→ Metal
```

**Compiler routing rule:** `SM >= 6.0 → DXC`, `SM 5.0 → FXC`. See [11-dx11-backend](11-dx11-backend.md) for the full FXC integration plan.

Shader permutation management and the compilation cache are documented in [05-shader-pipeline](../05-shader-pipeline/).

## Rules

1. **NEVER** add platform-specific code to the RHI interface layer — all `#ifdef` blocks go inside backend implementations
2. **ALWAYS** implement the full `IRHIDevice` / `IRHICommandList` / `IRHISwapChain` / `IRHIFence` interface — partial implementations break the abstraction
3. **ALWAYS** use the DX12 SM6.6 backend as the reference implementation when writing new backends
4. **ALWAYS** run the full RHI test suite against new backends — same tests, different backend
