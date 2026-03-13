# Proposal: Cross-Backend Shader Compatibility (DX11 + Vulkan)

## Why
All runtime shaders (forward PBR, deferred G-Buffer, deferred lighting, shadow depth, fullscreen) currently only compile and run on the DX12 SM6.6 bindless path. The SM5 fallback (`#ifndef UZ_BINDLESS`) exists in most shaders but has never been validated. Vulkan SPIR-V output has never been tested. This blocks multi-backend support — a core engine requirement for broad hardware coverage.

## What Changes
1. **Validate and fix all shaders** for 3 compilation targets: DXIL (SM6.6), DXBC (SM5.0/FXC), SPIR-V (Vulkan 1.3/DXC)
2. **Fix SM5 fallback paths** — ensure `#ifndef UZ_BINDLESS` code compiles and produces correct output with register-bound textures (t0-t5, s0-s1)
3. **Fix SPIR-V compilation** — ensure DXC `-spirv` output is valid for all shaders
4. **Add shader compilation tests** — automated tests that compile every shader to all 3 targets and verify success
5. **Ensure constant buffer layouts** are compatible across all backends (PerFrameCB, PerObjectCB, ShadowCB)

## Impact
- Affected specs: rendering pipeline, RHI shader interface
- Affected code: `runtime/shaders/runtime/*.hlsl`, `runtime/engine/renderer/passes/*`, shader compiler
- Breaking change: NO — DX12 primary path unchanged
- User benefit: Engine runs on DX11 and Vulkan hardware, not just DX12 SM6.6
