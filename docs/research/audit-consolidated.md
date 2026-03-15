# UzEngine 0.2 — Internal Audit Report

**Date**: 2026-03-13
**Auditors**: 6 specialist agents (code-reviewer, architect, security-reviewer, shader-engineer, researcher)

---

## Executive Summary

| Area | Critical | High | Medium | Low | Health |
|------|----------|------|--------|-----|--------|
| Code Quality | 2 | 6 | 4 | 2 | WARN |
| Architecture & SOLID | 3 | 5 | 3 | 4 | FAIL |
| Security & Memory Safety | 3 | 5 | 4 | 2 | WARN |
| Test Coverage | 4 | 2 | 1 | 0 | FAIL |
| Shaders HLSL | 3 | 3 | 3 | 6 | FAIL |
| Documentation | 2 | 4 | 3 | 2 | WARN |
| **Total** | **17** | **25** | **18** | **16** | **FAIL** |

**Overall verdict**: The engine has a solid DX12 foundation but critical gaps in RHI abstraction, shader correctness, and test coverage must be addressed before multi-backend or production work.

---

## 1. CODE QUALITY

### CRITICAL

- **CQ-01 — `dx12_resource_creation.cpp` at 1,427 lines** — approaching the 1,500 line hard limit. Must split by resource type (textures, buffers, PSOs, samplers).
- **CQ-02 — Forward PBR pass files deleted** but potentially referenced in build system (orphaned refs after `forward_pbr_pass.cpp/h` removal).

### HIGH

- **CQ-03** — `dx12_backend.cpp` ~1,100 lines, approaching split threshold (~1,000 rule).
- **CQ-04** — Raw `spdlog::` calls found instead of `UZ_LOG` macro in several files.
- **CQ-05** — Missing `UZ_PROFILE_SCOPE` in critical paths (shader compilation, resource creation).
- **CQ-06** — Inconsistent Rust-style `///` documentation across math headers.
- **CQ-07** — Magic numbers in `camera.h` (FOV, near/far planes without named constants).
- **CQ-08** — Several functions exceed the 40-line guideline in renderer passes.

### MEDIUM

- **CQ-09** — Demo files contain duplicated rendering setup code (boilerplate).
- **CQ-10** — Some log messages lack error context (`"Load failed"` instead of `"Failed to load X: reason=Y"`).
- **CQ-11** — Missing section separators (`// ───`) in several `.cpp` files.
- **CQ-12** — Inconsistent naming: mix of `camelCase` and `PascalCase` for methods.

---

## 2. ARCHITECTURE & SOLID

### CRITICAL

- **A-01 — `math_defs.h` leaks `using namespace DirectX` globally** — placed in a header transitively included by the entire engine. When Vulkan/Metal SDK headers are added, name collisions with DirectXMath symbols will break the build unpredictably.
- **A-02 — Pass descriptors hard-code single-mesh assumption** — `GBufferPassDesc`, `PBRPassDesc`, `ShadowPassDesc` each carry one `vertexBuffer`, `indexBuffer`, `indexCount`. Cannot render multi-mesh scenes without refactoring to `DrawCommand` list.
- **A-03 — `constant_buffers.h` encodes D3D12-specific root signature layout** — root parameter slots (0,1,2,3,4) are hardcoded integers in every pass. Vulkan/Metal use entirely different binding models. Needs `RootParams` named constants and `RootLayout` abstraction.

### HIGH

- **A-04** — `CBRingAllocator` lives in `constant_buffers.h` — mixed responsibilities.
- **A-05** — `shadow_pass.h` uses bare includes (`"rhi_types.h"`) while siblings use full paths.
- **A-06** — All demos call DirectXMath directly instead of engine `Mat4`/`Vec3` API.
- **A-07** — Pass descriptors carry both SM6.6 bindless and SM5 texture-handle fields with `bool bindless` — fat descriptor violating Interface Segregation.
- **A-08** — No `IRenderPass` interface. Passes are free functions with no composable registry.

### MEDIUM

- **A-09** — `IRHICommandList` mixes graphics, compute, and copy operations — no compile-time queue type enforcement.
- **A-10** — Root parameter indices are magic integers in pass `.cpp` files.
- **A-11** — `IRHIDevice` has doc block collision where `GetBackend()` description is in `IsBindless()` comment.

### LOW

- **A-12** — `IRHIDevice` promises thread-safe `Create*/Destroy*` but `DX12Device` explicitly says they are NOT synchronized — Liskov Substitution violation.

---

## 3. SECURITY & MEMORY SAFETY

### CRITICAL

- **S-01** — Raw pointers without RAII wrappers in parts of the RHI backend.
- **S-02** — Missing null checks after COM `QueryInterface` calls in some paths.
- **S-03** — Descriptor heap index not bounds-checked before use — overflow can corrupt GPU memory.

### HIGH

- **S-04** — Shader compilation error messages can leak internal filesystem paths.
- **S-05** — No validation of shader bytecode size before GPU upload.
- **S-06** — Some COM objects released manually instead of via `ComPtr`.
- **S-07** — Missing fence value overflow checks (theoretical in long sessions).
- **S-08** — File paths from shader `#include` directives not sanitized.

### MEDIUM

- **S-09** — Integer overflow potential in vertex/index buffer size calculations.
- **S-10** — Some error paths don't clean up all allocated resources (leak on failure).
- **S-11** — Debug layer messages written to stderr without rate limiting.
- **S-12** — Hardcoded viewport dimensions in some demo code.

---

## 4. TEST COVERAGE

### Coverage by Module

| Module | Est. Coverage | Status |
|--------|--------------|--------|
| RHI/DX12 | ~70% | Good base, gaps in swapchain/cmd list/PSO cache/destruction |
| Math | ~20% | **BROKEN** — tests reference pre-split API (`q.X()` vs `q.x`) |
| Renderer | ~5% | **Near zero** — no pass tests at all |
| Core | ~40% | Logging/assert ok, paths/config/profiling untested |
| Shaders | **0%** | No compilation tests, no output validation |

### CRITICAL Gaps

- **T-01** — Renderer passes have ZERO test coverage (GBuffer, Lighting, Shadow, PBR).
- **T-02** — Math library tests are broken — 6 test files use old API, need complete rewrite.
- **T-03** — No integration tests for full render pipeline.
- **T-04** — No shader compilation tests — a shader typo is only caught at runtime.

### Infrastructure Missing

- No test fixtures for common DX12 setup/teardown.
- No mock RHI backend for testing renderer without GPU.
- No utilities for comparing rendered output (golden image tests).
- **95% target is far from current ~30% reality.**

---

## 5. SHADERS (HLSL)

### CRITICAL

- **F-01 — LightingCB register mismatch**: `constant_buffers.h` documents buffer at `b3`, but `deferred_lighting_ps.hlsl` declares it at `b2` and `lighting_pass.cpp` binds at root slot 2. The C++ header comment is wrong.
- **F-02 — ShadowCB register mismatch**: `shadow_depth_vs.hlsl` declares `ShadowCB` at `b4`, but `shadow_pass.cpp` binds at root slot 3. Undocumented discrepancy.
- **F-03 — Cascade selection loop confusing logic**: Dual `cascadeIdx = i` assignment pattern that is "correct by accident" — will cause real bugs when maintained.

### HIGH

- **F-04 — Euclidean viewDepth vs view-space Z**: Cascade splits use camera Z axis, but `viewDepth = length(positionWS - cameraPos)` is spherical distance. Wrong cascade selected at screen edges.
- **F-05 — Albedo not sRGB-decoded**: Albedo textures sampled as `R8G8B8A8_UNORM` (no automatic decode). PBR math operates on gamma-encoded values, violating energy conservation. Produces washed-out results.
- **F-06 — Wrong geometry term k**: Uses `k = alpha / 2` (IBL formula). Karis 2013 specifies `k = (roughness + 1)^2 / 8` for direct analytic lights. Comment says "for direct lights" but uses wrong formula.

### MEDIUM

- **F-07** — All PBR/shadow functions copy-pasted between `mesh_ps.hlsl` and `deferred_lighting_ps.hlsl`. No shared `.hlsli` includes.
- **F-08** — Per-channel Reinhard tonemapping causes hue shifts on colored specular highlights.
- **F-09** — Debug depth shader has hardcoded `kNearPlane = 0.1f` / `kFarPlane = 1000.0f`.

### LOW

- **F-10** — `fullscreen_vs.hlsl` header comment vertex table doesn't match actual bit-trick.
- **F-11** — No BC5/two-channel normal map reconstruction guard.
- **F-12** — `grid_ps.hlsl` uses `b1` for `GridCB`, colliding with engine-standard `b1 = PerFrameCB`.
- **F-13** — `overlay_vs.hlsl` and `overlay_text_ps.hlsl` name fourth root constant field differently.
- **F-14** — Tangent transformed by `model` instead of `normalMatrix` — incorrect under non-uniform scale.
- **F-15** — 3x3 PCF kernel can stray outside cascade atlas quadrant into adjacent tile.

---

## 6. DOCUMENTATION

### CRITICAL

- **D-01** — Modules with NO documentation: `renderer/passes/`, `renderer/shadows/`, `scene_lighting.h`, `vertex_layout.h`.
- **D-02** — Docs reference deleted files (`forward_pbr_pass` renamed to `pbr_pass`, not updated in docs).

### HIGH

- **D-03** — `engine_math.h` (old unified) has stale docs vs new split headers.
- **D-04** — AGENTS.md references outdated file paths and module names.
- **D-05** — `docs/engine/12-rendering/` incomplete — missing shadow system and deferred pipeline docs.
- **D-06** — Camera docs (`14-camera/`) started but very sparse.

### MEDIUM

- **D-07** — Some headers have `///` docs but too terse (one-liners without param/return docs).
- **D-08** — Several `.cpp` files lack section-level `// ───` documentation.
- **D-09** — Demo code has minimal comments explaining rendering setup.

---

## Top 10 Priority Actions

| # | Action | IDs | Effort |
|---|--------|-----|--------|
| 1 | **Fix shader register mismatches** (F-01, F-02) | CRITICAL | Low |
| 2 | **Fix PBR math** — sRGB decode (F-05), geometry k (F-06), NDF clamping | CRITICAL | Low |
| 3 | **Remove `using namespace DirectX` from headers** (A-01) | CRITICAL | Low |
| 4 | **Split `dx12_resource_creation.cpp`** (CQ-01) | CRITICAL | Medium |
| 5 | **Add descriptor heap bounds checking** (S-03) | CRITICAL | Low |
| 6 | **Create shared `.hlsli` includes** for PBR/shadow functions (F-07) | HIGH | Medium |
| 7 | **Abstract root parameter layout** into named constants (A-03, A-10) | CRITICAL | Medium |
| 8 | **Rewrite math tests** for new split API (T-02) | CRITICAL | Medium |
| 9 | **Replace raw spdlog with UZ_LOG** everywhere (CQ-04) | HIGH | Low |
| 10 | **Add renderer pass tests** with mock RHI (T-01) | CRITICAL | High |

---

## Detailed Reports

Individual audit reports from specialist agents:
- [Architecture Audit](audit_architecture.md)
- [Shader Audit](audit_shaders.md)
- Code quality, security, test coverage, and documentation reports available in agent transcripts.
