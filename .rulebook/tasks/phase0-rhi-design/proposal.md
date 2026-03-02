# Phase 0 — RHI Design

## Context

UzEngine currently renders directly via DX12 with hard-coded API calls in `renderer.c`. This works for the current Windows-only PoC, but blocks cross-platform support.

Every future rendering system (PBR, shadows, post-process, UI, particles) would need to be written per-backend (DX12 / Vulkan / Metal) without a Hardware Interface layer. That multiplies implementation complexity by 3× and makes it impossible to share code between platforms.

Additionally, the existing shader system spec listed both HLSL and GLSL as authoring languages. This is wrong: it means game developers would need to write every shader twice. The correct approach is a single authoring language (HLSL) compiled to a canonical IR (SPIR-V), then transpiled per backend.

This is a **design task** — the output is an approved spec and a complete implementation plan. No production C code is written here, only the `rhi_dx12.c` port of the existing pyramid renderer to validate the API design.

## Goal

1. Finalize `docs/specs/rendering/rhi.md` — the RHI API specification
2. Define `rhi_backend_t` vtable in `src/engine/renderer/rhi/rhi.h`
3. Port the existing DX12 pyramid renderer to use the RHI (validates the design before any new rendering code is written)
4. Create stub backends for Vulkan and Metal (return false/0, compile cleanly)
5. Update `docs/specs/rendering/shader-system.md` to remove GLSL authoring, document HLSL → SPIR-V → backend pipeline

## Spec Reference

[RHI Spec](../../../docs/specs/rendering/rhi.md)
[Shader System Spec](../../../docs/specs/rendering/shader-system.md)

## Approach

### 1. Define `rhi.h` header

Create `src/engine/renderer/rhi/rhi.h` with:
- Handle typedefs (`RhiBuffer`, `RhiTexture`, `RhiPipeline`, `RhiCmdList`, `RhiFence`)
- `RhiFormat`, `RhiBackend` enums
- `RhiTextureDesc`, `RhiBufferDesc`, `RhiShaderDesc`, `RhiPipelineDesc` structs
- `rhi_backend_t` function pointer table (full vtable per spec)
- `const rhi_backend_t *rhi(void)` global accessor

### 2. Implement `rhi_dx12.c`

Move all current DX12 code from `renderer.c` into `rhi_dx12.c`, exposing it only through the vtable. `renderer.c` must call only `rhi()->*` — no direct DX12 includes outside `rhi_dx12.c`.

### 3. Create stub backends

`rhi_vulkan.c` and `rhi_metal.c` — all vtable functions return `false` or `0`. Must compile cleanly on Windows (Metal stub uses `#ifdef`).

### 4. PSO cache in `rhi_common.c`

- `PsoCacheKey` struct (VS hash + PS hash + RT formats hash + state hash)
- xxHash64 for key computation
- HashMap (open addressing, power-of-2 size) from permanent arena
- `pso_cache_get` / `pso_cache_insert` / `pso_cache_invalidate`

### 5. SPIRV-Cross wrapper `spirv_transpile.c`

- `spirv_to_msl(const void *spirv, size_t size, char **out_msl)` — used by Metal backend
- `spirv_to_glsl_es(const void *spirv, size_t size, char **out_glsl)` — used for Android debug tooling
- Thin C wrapper over the SPIRV-Cross C API (`spirv_cross_c.h`)

### 6. CMake wiring

- `UZ_BACKEND_DX12` / `UZ_BACKEND_VULKAN` / `UZ_BACKEND_METAL` defines based on platform
- vcpkg dependencies: `directx-dxc`, `spirv-cross`, `spirv-tools`

### 7. Validate with existing pyramid

After the port, running `UzEngine.exe` must still show the spinning pyramid. This confirms the RHI abstraction is correct before any new rendering code is written.

## Out of Scope

- Vulkan or Metal backend implementation (stubs only in this task)
- PBR renderer (Phase 2)
- Render Graph implementation (Phase 1, task `phase1-render-graph` — but it must be designed to use RHI handles, not DX12 directly)
- Shader variant permutation system (Phase 2)
