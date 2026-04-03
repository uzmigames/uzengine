# Proposal: Architecture & SOLID Fixes

## Why
Internal audit (2026-03-13) found 3 CRITICAL and 5 HIGH architecture violations. The most urgent:
`using namespace DirectX` leaked globally via header (blocks multi-backend), pass descriptors hardcode
single-mesh assumption, and root signature layout is DX12-specific with magic integers. These MUST be
fixed before Vulkan/Metal backend work begins.

## What Changes
- Remove `using namespace DirectX` from math_defs.h — use explicit `DirectX::` prefix (A-01)
- Refactor pass descriptors to support multi-mesh via DrawCommand list (A-02)
- Abstract root parameter layout into named constants / RootLayout abstraction (A-03)
- Split CBRingAllocator out of constant_buffers.h (A-04)
- Standardize include paths across passes (A-05)
- Migrate demos from raw DirectXMath to engine Mat4/Vec3 API (A-06)
- Split pass descriptors into bindless vs SM5 variants — ISP fix (A-07)
- Define IRenderPass interface for composable pass registry (A-08)
- Fix IRHIDevice thread-safety doc vs DX12Device reality — LSP fix (A-12)

## Impact
- Affected specs: RHI interface, renderer passes, math headers, demos
- Affected code: runtime/engine/math/math_defs.h, runtime/engine/renderer/passes/*.h, constant_buffers.h, all demos
- Breaking change: YES (pass descriptor API changes, include changes)
- User benefit: Multi-backend ready, multi-mesh scenes, cleaner API
