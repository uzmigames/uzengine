# 3. RHI Vtable Abstraction with DX12-First Multi-Backend

**Status**: proposed
**Date**: 2026-03-15

## Context

UzEngine targets Windows (DX12/DX11), macOS/iOS (Metal), and Linux (Vulkan). Need a clean abstraction that prevents graphics API types from leaking above the RHI layer while supporting 4 backends with minimal overhead.

## Decision

C-style vtable (rhi_backend_t) with function pointers for all GPU operations. Each backend (rhi_dx12.c, rhi_vulkan.c, rhi_metal.c, rhi_dx11.c) implements the vtable. No DX12/Vulkan/Metal types above rhi.h — all handles are opaque. DX12 is the primary development target. PSO cache in rhi_common.c shared across backends. SPIRV-Cross for shader transpilation (HLSL → SPIR-V → MSL).

## Alternatives Considered

- Abstract C++ class hierarchy (virtual overhead, complex diamond inheritance)
- Template-based compile-time dispatch (no runtime backend switching)
- Single-backend DX12-only (limits platform reach)
- WebGPU/wgpu (immature, performance overhead)

## Consequences

Pros: Zero-cost abstraction (function pointer = 1 indirect call), clean boundary (no type leakage), Liskov substitution (any backend swappable), C ABI compatible. Cons: Must maintain 4 backend implementations, C-style API less ergonomic than C++. CRITICAL RULE: No ID3D12*, VkBuffer, MTLTexture types above rhi_dx12.c/rhi_vulkan.c/rhi_metal.c respectively.
