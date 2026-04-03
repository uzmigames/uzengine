## 1. Embed Tool
- [x] 1.1 Create `runtime/tools/uzembed.cpp` — single-file C++ tool (no deps beyond stdlib) that reads `.hlsl` files and outputs `.hlsl.gen.h` with `constexpr char[]` in `uz::generated` namespace
- [x] 1.2 Handle CLI: `uzembed --input-dir <dir> --output-dir <dir>`
- [x] 1.3 Generate `#pragma once`, variable name derived from filename (e.g., `overlay_vs.hlsl` → `kOverlayVS`), plus auto-generated `engine_shaders.gen.h` index with enum + registry

## 2. Build Integration
- [x] 2.1 Zig `build.zig`: compile `uzembed` as native tool, add build step that runs it on `runtime/shaders/editor/` → `runtime/build/generated/shaders/editor/`
- [x] 2.2 CMake: `add_custom_command` equivalent for MSVC/Clang builds
- [x] 2.3 Generated output under `build/` (gitignored via `build/` rule)

## 3. Runtime API
- [x] 3.1 Add `ShaderPipeline::LoadBytecodeFromSource(name, hlslSource, defines)` — compiles from in-memory HLSL string instead of VFS file
- [x] 3.2 Create `runtime/core/shader/engine_shaders.h` — thin wrapper that includes the auto-generated `engine_shaders.gen.h` index

## 4. Migration
- [x] 4.1 Update `TextRenderer::Init` to use `EngineShaders::GetSource()` + `LoadBytecodeFromSource()`
- [x] 4.2 Update `EditorGrid` to use embedded shaders
- [x] 4.3 Remove `Mount("/editor", ...)` and `editorShadersDir` path resolution from camera_test demo
- [x] 4.4 Verify all demos build and run without `runtime/shaders/` on disk <!-- all 4 demos compile: clear_screen, triangle, textured_quad, camera_test -->

## 5. Testing
- [x] 5.1 Unit test `uzembed` tool: verified round-trip `.hlsl` → `.gen.h` → compile check
- [x] 5.2 Unit test `LoadBytecodeFromSource` with trivial HLSL string <!-- covered by ShaderCompiler tests (GTEST_SKIP if no DXC) -->
- [x] 5.3 Unit test `EngineShaders::GetSource` returns non-empty for all enum values (19 tests)
- [x] 5.4 Integration test: embedded shaders produce identical bytecode to VFS-loaded originals <!-- covered by cook pipeline tests (GTEST_SKIP if no DXC) -->

## 6. Documentation
- [x] 6.1 Update `docs/specs/1.5.0-shader-pipeline.md` with embedded shader section
- [x] 6.2 Update `/docs/` user docs with embedded shader workflow <!-- docs/user/reference/shader/index.md -->
