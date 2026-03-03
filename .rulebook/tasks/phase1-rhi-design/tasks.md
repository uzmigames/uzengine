## 1. RHI Header (`rhi.h`)

- [ ] 1.1 Create `src/engine/renderer/rhi/rhi.h` — handle typedefs: `RhiBuffer`, `RhiTexture`, `RhiPipeline`, `RhiCmdList`, `RhiFence` (all `uint64_t`)
- [ ] 1.2 Define `RhiFormat` enum — all formats used in Phase 1+2 (RGBA8, RGBA16F, R32F, D32F, BC7, BC5, BC6H)
- [ ] 1.3 Define `RhiBackend` enum — `DX12`, `VULKAN`, `METAL`
- [ ] 1.4 Define descriptor structs: `RhiTextureDesc`, `RhiBufferDesc`, `RhiShaderDesc`, `RhiPipelineDesc`
- [ ] 1.5 Define `rhi_backend_t` vtable — all lifecycle, resource, pipeline, command, barrier, swapchain, sync, and debug functions per spec
- [ ] 1.6 Declare `const rhi_backend_t *rhi(void)` and `void rhi_set_backend(const rhi_backend_t *b)` in `rhi.h`
- [ ] 1.7 All structs are C99 POD (no C++ in rhi.h — must be includable from pure C)

## 2. DX12 Backend (`rhi_dx12.c`)

- [ ] 2.1 Create `src/engine/renderer/rhi/rhi_dx12.h` — declares `extern const rhi_backend_t rhi_dx12_backend`
- [ ] 2.2 Create `src/engine/renderer/rhi/rhi_dx12.c` — move all DX12 code from `renderer.c`
- [ ] 2.3 Implement `init`: device, command queue, swapchain, descriptor heaps
- [ ] 2.4 Implement `buffer_create` / `buffer_destroy` / `buffer_map` / `buffer_unmap` / `buffer_upload` via D3D12 heap
- [ ] 2.5 Implement `texture_create` / `texture_destroy` / `texture_upload` via D3D12 heap + copy queue
- [ ] 2.6 Implement `pipeline_create` using PSO cache (via `rhi_common.c`)
- [ ] 2.7 Implement `cmd_begin` / `cmd_end` / `cmd_submit` via D3D12 command allocator + list
- [ ] 2.8 Implement all `cmd_set_*` and `cmd_draw*` / `cmd_dispatch` functions
- [ ] 2.9 Implement `cmd_barrier_texture` / `cmd_barrier_buffer` via `ResourceBarrier` D3D12 call
- [ ] 2.10 Implement `swapchain_backbuffer` / `swapchain_present`
- [ ] 2.11 Implement `fence_create` / `fence_signal` / `fence_wait` via `ID3D12Fence`
- [ ] 2.12 Implement `set_debug_name` via `SetName()` + `begin_event` / `end_event` via PIX annotations
- [ ] 2.13 `renderer.c` must include 0 DX12 headers after this task — only `rhi.h`

## 3. Stub Backends

- [ ] 3.1 Create `src/engine/renderer/rhi/rhi_vulkan.c` — all functions return `false` / `0` / null handle
- [ ] 3.2 Create `src/engine/renderer/rhi/rhi_metal.c` — `#if defined(__APPLE__)` guard, same stubs
- [ ] 3.3 Both stubs compile cleanly on Windows (zero warnings)

## 4. PSO Cache (`rhi_common.c`)

- [ ] 4.1 Create `src/engine/renderer/rhi/rhi_common.h` — `PsoCacheKey`, cache API declarations
- [ ] 4.2 Create `src/engine/renderer/rhi/rhi_common.c` — open-addressing hashmap from permanent arena
- [ ] 4.3 Implement `pso_cache_get(PsoCacheKey)` → `RhiPipeline` or `RHI_NULL`
- [ ] 4.4 Implement `pso_cache_insert(PsoCacheKey, RhiPipeline)`
- [ ] 4.5 Implement `pso_cache_invalidate_by_shader(uint64_t spirv_hash)` — used by hot-reload
- [ ] 4.6 Add `xxhash` to vcpkg.json and use `XXH3_64bits` for hash computation
- [ ] 4.7 Unit test: insert + get hit + get miss + invalidate

## 5. SPIRV-Cross Wrapper (`spirv_transpile.c`)

- [ ] 5.1 Add `spirv-cross` to `vcpkg.json`
- [ ] 5.2 Create `src/engine/renderer/rhi/spirv_transpile.h` — `spirv_to_msl`, `spirv_to_glsl_es` declarations
- [ ] 5.3 Create `src/engine/renderer/rhi/spirv_transpile.c` — thin C wrapper over `spirv_cross_c.h`
- [ ] 5.4 `spirv_to_msl`: takes SPIR-V bytecode, returns null-terminated MSL string (caller frees)
- [ ] 5.5 `spirv_to_glsl_es`: takes SPIR-V bytecode, returns GLSL ES 3.10 string (for Android debug tooling only)
- [ ] 5.6 Add `spirv-tools` to `vcpkg.json` for SPIR-V validation in debug builds

## 6. CMake Wiring

- [ ] 6.1 Add `UZ_BACKEND_DX12` compile definition for Windows in `CMakeLists.txt`
- [ ] 6.2 Add `UZ_BACKEND_VULKAN` definition for Linux/Android (conditional, future)
- [ ] 6.3 Add `UZ_BACKEND_METAL` definition for macOS/iOS (conditional, future)
- [ ] 6.4 `renderer_init` selects backend via `#if defined(UZ_BACKEND_*)` guards
- [ ] 6.5 Add `directx-dxc`, `spirv-cross`, `spirv-tools`, `xxhash` to `vcpkg.json`

## 7. Validation — Pyramid Still Works

- [ ] 7.1 After port, `UzEngine.exe` launches and shows spinning pyramid (DX12 backend, unchanged behavior)
- [ ] 7.2 All existing MCP/IPC calls work: `engine.ping`, `engine.status`, `engine.screenshot`, `scene.setRotation`, `scene.setBackground`
- [ ] 7.3 No DX12 includes in `renderer.c` or any file outside `rhi_dx12.c`
- [ ] 7.4 Compile with `/W4` zero warnings on MSVC

## 8. Documentation

- [ ] 8.1 Confirm `docs/specs/rendering/rhi.md` matches implementation (update if needed)
- [ ] 8.2 Confirm `docs/specs/rendering/shader-system.md` updated (no GLSL authoring language)
- [ ] 8.3 Update `docs/guides/shader-authoring.md` — add cross-platform binding rules section
- [ ] 8.4 Add RHI to `docs/engine/module-structure.md` under renderer module
