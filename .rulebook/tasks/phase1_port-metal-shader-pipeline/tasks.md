## 1. Implementation

### Phase 1 — Slang runtime integration (TDD)
- [x] 1.1 Write failing test: SlangCompiler::Init loads vendored libslang.dylib and compiles trivial HLSL vertex/pixel/compute shaders to MSL text — `runtime/tests/engine/shader/test_slang_compiler.cpp` (VS/PS/CS → `[[vertex]]`/`main_0`, `[[fragment]]`, `[[kernel]]` + invalid-HLSL diagnostics)
- [x] 1.2 Wire Slang into CMake (include path, rpath for executables, dylib deploy) so the test passes in-process — `runtime/CMakeLists.txt` (include path L775, `deploy_slang` target + `@executable_path/thirdparty/slang/bin` rpath L860-873); verified green: 4/4 SlangCompilerTest + 11 related shader tests pass on macOS

### Phase 2 — ShaderPipeline macOS via Slang (TDD)
- [x] 2.1 Write failing tests: LoadBytecodeFromSource returns non-empty MSL text containing the expected entry function; SM5 path (no UZ_BINDLESS define); failure on invalid HLSL reports errors
- [x] 2.2 Implement the Slang-backed pipeline for __APPLE__ in shader_pipeline.cpp (replacing the failure fallbacks), with an in-memory compiled-shader cache

### Phase 3 — ShaderManager macOS via Slang (TDD)
- [x] 3.1 Write failing tests: Init succeeds on Metal device; CreateShader(MeshVS) returns a valid handle; entry-point mapping main → main_0
- [x] 3.2 Implement the Slang-backed ShaderManager for __APPLE__ (replacing the failure fallbacks)

### Phase 4 — Metal compute PSO (TDD where testable)
- [x] 4.1 Write failing test: CreateComputePipeline on a trivial MSL kernel returns a valid pipeline handle (Metal device test, skips without GPU)
- [x] 4.2 Implement MetalDevice::CreateComputePipeline (kernel function + MTLComputePipelineState)

### Phase 5 — Sponza headless bring-up
- [ ] 5.1 Build uzengine tool for macOS (non-Windows RunWindowed/headless paths through the Application class)
- [ ] 5.2 Run Sponza scene, fix pass-by-pass Metal/shader issues until the frame renders
- [ ] 5.3 Capture screenshots at PhysCamera A/C/E and visually compare against Intel Render_Main_A/C/E

## 2. Tail (docs + tests — check or waive with tailWaiver)
- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
