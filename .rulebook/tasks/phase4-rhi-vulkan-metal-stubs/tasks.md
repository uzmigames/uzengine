## Context

Deferred from `phase1-rhi-design`. Adds Vulkan and Metal backend stubs,
SPIRV-Cross wrapper for shader transpilation, and enforces the RHI isolation rule
(no DX12 headers outside rhi_dx12.c).

## 1. Vulkan Backend Stub (`rhi_vulkan.c`)

- [ ] 1.1 Create `src/engine/renderer/rhi/rhi_vulkan.c` — all `rhi_backend_t` functions return `false`/`0`/`RHI_NULL`
- [ ] 1.2 Guard with `#if defined(UZ_PLATFORM_LINUX) || defined(UZ_PLATFORM_ANDROID)`
- [ ] 1.3 `extern const rhi_backend_t rhi_vulkan_backend` — declared in `rhi_vulkan.h`
- [ ] 1.4 Compiles cleanly on Linux with zero warnings
- [ ] 1.5 Tests: backend selectable, all functions callable, return expected null values

## 2. Metal Backend Stub (`rhi_metal.c`)

- [ ] 2.1 Create `src/engine/renderer/rhi/rhi_metal.c` — all functions return `false`/`0`/`RHI_NULL`
- [ ] 2.2 Guard with `#if defined(UZ_PLATFORM_MACOS) || defined(UZ_PLATFORM_IOS)`
- [ ] 2.3 `extern const rhi_backend_t rhi_metal_backend` — declared in `rhi_metal.h`
- [ ] 2.4 Compiles cleanly on macOS with zero warnings
- [ ] 2.5 Tests: backend selectable, all functions callable

## 3. Backend Selection (`UZ_BACKEND_*` Compile Definitions)

- [ ] 3.1 CMakeLists.txt: `target_compile_definitions` for `UZ_BACKEND_DX12` on Windows
- [ ] 3.2 CMakeLists.txt: `UZ_BACKEND_VULKAN` on Linux (when Vulkan SDK present)
- [ ] 3.3 CMakeLists.txt: `UZ_BACKEND_METAL` on macOS
- [ ] 3.4 `renderer_init()` selects backend via `#if defined(UZ_BACKEND_*)` — calls `rhi_set_backend()`
- [ ] 3.5 Tests: correct backend selected per platform in CI

## 4. SPIRV-Cross Wrapper (`spirv_transpile.c`)

- [ ] 4.1 Add `spirv-cross` to `vcpkg.json`
- [ ] 4.2 Add `spirv-tools` to `vcpkg.json` (SPIR-V validation in debug builds)
- [ ] 4.3 Create `src/engine/renderer/rhi/spirv_transpile.h` — declare `spirv_to_msl()`, `spirv_to_glsl_es()`
- [ ] 4.4 Create `src/engine/renderer/rhi/spirv_transpile.c` — thin C wrapper over `spirv_cross_c.h`
- [ ] 4.5 `spirv_to_msl(bytecode, len)` → null-terminated MSL string (caller frees with `spirv_free_string()`)
- [ ] 4.6 `spirv_to_glsl_es(bytecode, len)` → GLSL ES 3.10 string (Android debug tooling)
- [ ] 4.7 Tests: valid SPIR-V bytecode → MSL output non-null, GLSL ES output non-null

## 5. RHI Isolation Enforcement

- [ ] 5.1 Audit all files: no `d3d12.h`/`vulkan.h`/`Metal.h` includes outside `rhi_dx12.c`/`rhi_vulkan.c`/`rhi_metal.c`
- [ ] 5.2 Add CMake check or CI lint step to enforce isolation
- [ ] 5.3 `renderer.cpp` includes only `rhi.h` — no backend-specific headers

## 6. Documentation

- [ ] 6.1 Update `docs/specs/rendering/rhi.md` — add backend selection section
- [ ] 6.2 `docs/guides/shader-authoring.md` — add cross-platform binding rules (register slots, cbuffer b15 reserved for GlobalParams)
- [ ] 6.3 Add RHI to `docs/engine/module-structure.md`

## 7. Tests

- [ ] 7.1 ≥95% line + branch coverage on spirv_transpile.c
- [ ] 7.2 Vulkan/Metal stub tests run on Linux CI (stub returns null, no crash)
- [ ] 7.3 No regressions in existing 261 rhi_common tests
