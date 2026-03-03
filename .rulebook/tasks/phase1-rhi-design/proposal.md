# Proposal: phase0-rhi-design

## Why

UzEngine currently calls DX12 directly in `renderer.c` with no abstraction layer. Every new rendering subsystem — PBR, shadows, post-processing, UI, particles — would need to be written once per backend (DX12, Vulkan, Metal) without a common interface, multiplying implementation work by 3x and making it structurally impossible to share renderer code across platforms.

The absence of an RHI also forces the existing shader system spec to list both HLSL and GLSL as authoring languages, meaning game developers would need to write every shader twice. The correct architecture uses a single authoring language (HLSL), compiled to SPIR-V as a canonical intermediate representation, then transpiled per backend by SPIRV-Cross. This is how Unreal Engine 5 and modern cross-platform engines solve the same problem.

This task is a design-and-validation task, not a full implementation task. The deliverable is a finalized `rhi.h` vtable API, the existing DX12 pyramid renderer ported to use it (proving the design is workable), and stub Vulkan and Metal backends that compile cleanly. All Phase 1 and later rendering work (Render Graph, PBR, shadow maps) must be written against RHI handles, never against DX12 types directly.

## What Changes

- Create `src/engine/renderer/rhi/rhi.h` — defines opaque handle typedefs (`RhiBuffer`, `RhiTexture`, `RhiPipeline`, `RhiCmdList`, `RhiFence`), enums (`RhiFormat`, `RhiBackend`), descriptor structs (`RhiTextureDesc`, `RhiBufferDesc`, `RhiShaderDesc`, `RhiPipelineDesc`), the full `rhi_backend_t` function pointer vtable, and a `const rhi_backend_t *rhi(void)` global accessor; no platform headers included in this file
- Move all DX12 code from `renderer.c` into `src/engine/renderer/rhi/rhi_dx12.c` — fills out every function pointer in `rhi_backend_t`; DX12 headers (`d3d12.h`, `dxgi1_6.h`) are included only in this translation unit; `renderer.c` calls only `rhi()->*` after the port
- Create `src/engine/renderer/rhi/rhi_vulkan.c` — stub backend, all vtable entries return `false` or `0`; compiles cleanly on Windows; `#define UZ_BACKEND_VULKAN` guard in CMake controls whether it is linked
- Create `src/engine/renderer/rhi/rhi_metal.c` — stub backend, same pattern; entire file wrapped in `#ifdef UZ_PLATFORM_MACOS` so it compiles cleanly on Windows as an empty translation unit
- Implement PSO cache in `src/engine/renderer/rhi/rhi_common.c` — `PsoCacheKey` struct hashes VS blob + PS blob + render target formats + rasterizer/blend state with xxHash64; open-addressing hash map allocated from the permanent arena; functions `pso_cache_get`, `pso_cache_insert`, `pso_cache_invalidate`
- Create `src/engine/renderer/rhi/spirv_transpile.c` — thin C wrapper over SPIRV-Cross C API (`spirv_cross_c.h`): `spirv_to_msl(spirv, size, out_msl)` for the Metal backend, `spirv_to_glsl_es(spirv, size, out_glsl)` for Android debug tooling; links against `spirv-cross-c` from vcpkg
- Update CMakeLists to add `UZ_BACKEND_DX12`, `UZ_BACKEND_VULKAN`, `UZ_BACKEND_METAL` defines selected by platform; add vcpkg dependencies `directx-dxc`, `spirv-cross`, `spirv-tools`
- Update `docs/specs/rendering/shader-system.md` — remove GLSL as an authoring language; document the canonical pipeline: HLSL source → DXC → SPIR-V → SPIRV-Cross transpile per backend; update all shader authoring examples to HLSL only
- Validate by running `UzEngine.exe` after the port — the spinning DX12 pyramid must render identically to before; this is the acceptance criterion proving the RHI vtable design is correct before any new rendering code is written

## Impact

- Affected specs: `docs/specs/rendering/rhi.md`, `docs/specs/rendering/shader-system.md`
- Affected code: `src/engine/renderer/renderer.c`, `src/engine/renderer/rhi/rhi.h`, `src/engine/renderer/rhi/rhi_dx12.c`, `src/engine/renderer/rhi/rhi_vulkan.c`, `src/engine/renderer/rhi/rhi_metal.c`, `src/engine/renderer/rhi/rhi_common.c`, `src/engine/renderer/rhi/spirv_transpile.c`, `CMakeLists.txt`
- Breaking change: YES — `renderer.c` is refactored; any code that includes DX12 headers directly above the RHI layer must be updated
- User benefit: All future rendering systems (PBR, shadows, particles, UI pass) are written once against the RHI vtable and work on DX12 today, with Vulkan and Metal backends unlocked later without touching renderer code; game developers write shaders in HLSL only
