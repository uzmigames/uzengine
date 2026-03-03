## 1. RHI Header (`rhi.h`)

- [x] 1.1 Create `src/engine/renderer/rhi/rhi.h` — handle typedefs: `RhiBuffer`, `RhiTexture`, `RhiPipeline`, `RhiCmdList`, `RhiFence` (all `uint64_t`)
- [x] 1.2 Define `RhiFormat` enum — RGBA8, RGBA16F, R32F, D32F, BC7, BC5, BC6H, and more
- [x] 1.3 Define `RhiBackend` enum — DX12, VULKAN, METAL
- [x] 1.4 Define descriptor structs: `RhiTextureDesc`, `RhiBufferDesc`, `RhiShaderDesc`, `RhiPipelineDesc`
- [x] 1.5 Define `rhi_backend_t` vtable — all lifecycle, resource, pipeline, command, barrier, swapchain, sync, debug functions
- [x] 1.6 Declare `const rhi_backend_t *rhi(void)` and `void rhi_set_backend(const rhi_backend_t *b)`
- [x] 1.7 All structs are C99 POD (no C++ in rhi.h — includable from pure C)

## 2. DX12 Backend (`rhi_dx12.c`)

- [x] 2.1 Create `src/engine/renderer/rhi/rhi_dx12.h` — declares `extern const rhi_backend_t rhi_dx12_backend`
- [x] 2.2 Create `src/engine/renderer/rhi/rhi_dx12.c` — full DX12 implementation (2106 lines)
- [x] 2.3 Implement `init`: device, command queue, swapchain, descriptor heaps
- [x] 2.4 Implement `buffer_create` / `buffer_destroy` / `buffer_map` / `buffer_unmap` / `buffer_upload`
- [x] 2.5 Implement `texture_create` / `texture_destroy` / `texture_upload`
- [x] 2.6 Implement `pipeline_create` using PSO cache (via `rhi_common.c`)
- [x] 2.7 Implement `cmd_begin` / `cmd_end` / `cmd_submit`
- [x] 2.8 Implement all `cmd_set_*` and `cmd_draw*` / `cmd_dispatch` functions
- [x] 2.9 Implement `cmd_barrier_texture` / `cmd_barrier_buffer` via `ResourceBarrier`
- [x] 2.10 Implement `swapchain_backbuffer` / `swapchain_present`
- [x] 2.11 Implement `fence_create` / `fence_signal` / `fence_wait` via `ID3D12Fence`
- [x] 2.12 Implement `set_debug_name` / `begin_event` / `end_event`
- [ ] 2.13 `renderer.c` refactored to include only `rhi.h` (DX12 isolation not yet enforced)

## 3. Stub Backends

- [ ] 3.1 `rhi_vulkan.c` — not yet created (deferred to Phase 4 async compute)
- [ ] 3.2 `rhi_metal.c` — not yet created (deferred to macOS platform support)

## 4. PSO Cache (`rhi_common.c`)

- [x] 4.1 Create `src/engine/renderer/rhi/rhi_common.c` — PSO cache with open-addressing hashmap
- [x] 4.2 Implement `pso_cache_get()` / `pso_cache_insert()` / `pso_cache_invalidate_by_shader()`
- [x] 4.3 Hash computation using xxhash3 (64-bit)
- [x] 4.4 Unit tests: `tests/renderer/test_rhi_common.cpp` — 261 tests covering insert, get hit, miss, invalidate
- [ ] 4.5 `xxhash` added to vcpkg.json — deferred (using inline implementation for now)

## 5. SPIRV-Cross Wrapper

- [ ] 5.1 `spirv_transpile.h/c` — not yet created (deferred to Phase 3 when Metal/Vulkan backends needed)
- [ ] 5.2 `spirv-cross`, `spirv-tools` not yet in vcpkg.json (deferred)

## 6. CMake Wiring

- [x] 6.1 `src/engine/renderer/rhi/rhi_common.c` added to `UzEngineTests` (Linux-compatible)
- [x] 6.2 `rhi_dx12.c` conditionally compiled (Windows only, excluded on Linux dev container)
- [ ] 6.3 `UZ_BACKEND_DX12` / `UZ_BACKEND_VULKAN` / `UZ_BACKEND_METAL` compile definitions — deferred
- [ ] 6.4 `directx-dxc`, `spirv-cross`, `spirv-tools` in vcpkg.json — deferred

## 7. Validation

- [x] 7.1 `rhi.h` + `rhi_common.c` build cleanly on Linux with Clang (zero warnings)
- [x] 7.2 261 PSO cache tests pass in `UzEngineTests` binary
- [ ] 7.3 Windows pyramid validation (requires DX12 environment) — deferred to Windows build

## 8. Documentation

- [x] 8.1 `docs/specs/rendering/rhi.md` exists (453 lines) — spec covers all RHI API decisions
- [ ] 8.2 `docs/guides/shader-authoring.md` cross-platform binding rules — deferred
- [ ] 8.3 RHI module structure doc — deferred
