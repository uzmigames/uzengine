# Proposal: SRP File Split — Codebase Cleanup (Phase 0)

## Why
16 engine source files violate the Single Responsibility Principle and/or exceed the 1500-line file size limit defined in `.claude/rules/editing.md`. The two worst offenders — `scene_lifecycle.cpp` (1789 lines, 12 responsibilities) and `pipeline_postprocess.cpp` (1560 lines, 11 PSO families) — are difficult to navigate, review, and maintain. Large headers like `scene_renderer.h` (1359 lines) and `rhi.h` (1012 lines) force unnecessary recompilation across the entire codebase. Splitting these files reduces cognitive load, improves incremental build times, and makes future feature work easier to scope and review.

## What Changes

### Refactoring

1. **RHI headers** — Split `rhi.h` (4 interfaces) into per-interface headers with an umbrella include
2. **Scene renderer header** — Extract FrameContext and private members from `scene_renderer.h` into internal headers
3. **Pipeline library header** — Move private members from `pipeline_library.h` to `pipeline_library_impl.h`
4. **RHI backends** — Split DX11 and Metal device files into init + resource creation; extract Vulkan format helpers
5. **Renderer core** — Split `scene_lifecycle.cpp` (5 files), `pipeline_postprocess.cpp` (4 files), `frame_lighting.cpp` (4 files), `frame_setup.cpp` (3 files)
6. **Asset & upload** — Split `texture_loader.cpp` (3 files), `mesh.cpp` (3 files), `scene_assets.cpp` (2 files)
7. **Application layer** — Split `application.cpp` (3 files) and `demo_application.cpp` (3 files)
8. **Shader & debug** — Split `shader_pipeline.cpp` (2 files) and `inspector_pass.cpp` (3 files)

### New Systems
None — this is a pure mechanical refactoring with zero behavioral changes.

## Impact
- Affected specs: None (no behavioral changes)
- Affected code: `runtime/engine/rhi/`, `runtime/engine/renderer/`, `runtime/engine/app/`, `runtime/engine/shader/`, `runtime/engine/renderer/mesh/`, `runtime/engine/renderer/upload/`, `runtime/engine/renderer/passes/debug/`, `runtime/demos/common/`
- Breaking change: NO — all public APIs remain identical
- User benefit: Faster incremental builds, easier code navigation, smaller review diffs for future features
- Enables: Cleaner ownership boundaries for parallel agent work on renderer subsystems
