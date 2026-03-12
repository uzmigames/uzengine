## 1. Implementation

### Thirdparty Setup
- [ ] 1.1 Download Slang compiler release (libslang + headers) to `thirdparty/slang/`
- [ ] 1.2 Add Slang include path to build.zig and CMakeLists.txt
- [ ] 1.3 Link `libslang.dylib` (macOS) / `slang.dll` (Windows for offline cook)
- [ ] 1.4 Deploy `libslang.dylib` to build output directory

### SlangCompiler Class
- [ ] 1.5 Create `slang_compiler.h` — `SlangCompiler` class with `CompileToMSL()` method
- [ ] 1.6 Implement `Init()` — create `SlangSession` with default options
- [ ] 1.7 Implement `CompileToMSL(hlslSource, entryPoint, stage)` → returns MSL source string
- [ ] 1.8 Implement `CompileToSPIRV(hlslSource, entryPoint, stage)` → returns SPIR-V blob (future DXC replacement)
- [ ] 1.9 Configure Slang target — `SLANG_METAL` for MSL output
- [ ] 1.10 Configure HLSL source language — `SLANG_SOURCE_LANGUAGE_HLSL`
- [ ] 1.11 Handle compilation errors — extract diagnostics via `spGetDiagnosticOutput()`
- [ ] 1.12 Log compilation errors with `UZ_LOG(Shader, Error, ...)` including line numbers

### Binding Translation
- [ ] 1.13 Map HLSL `register(t0-t15)` → Metal argument buffer texture indices
- [ ] 1.14 Map HLSL `register(s0-s15)` → Metal argument buffer sampler indices
- [ ] 1.15 Map HLSL `register(b0-b15)` → Metal buffer indices
- [ ] 1.16 Map HLSL push constants → Metal `[[buffer(N)]]` binding
- [ ] 1.17 Verify SV_Position, SV_VertexID, SV_InstanceID map correctly to Metal attributes

### Shader Pipeline Integration
- [ ] 1.18 Update `shader_compiler.cpp` — detect `RHIBackend::Metal` and route to `SlangCompiler::CompileToMSL()`
- [ ] 1.19 Feed MSL output to `MTL::Device::newLibrary(source:)` in MetalDevice::CreateShader()
- [ ] 1.20 Implement shader cache — cache compiled MSL by hash to avoid recompilation
- [ ] 1.21 Support hot-reload — recompile on file change, recreate MTLLibrary + MTLFunction

### Offline Cook
- [ ] 1.22 Update `uzcook` tool — add Metal MSL output format
- [ ] 1.23 Store pre-compiled MSL in `.uzshader` asset alongside DXIL and SPIR-V variants

## 2. Testing

- [ ] 2.1 Test basic vertex shader compilation (HLSL → MSL)
- [ ] 2.2 Test basic fragment/pixel shader compilation (HLSL → MSL)
- [ ] 2.3 Test compute shader compilation (HLSL → MSL)
- [ ] 2.4 Test compilation error handling — invalid HLSL produces diagnostic with line numbers
- [ ] 2.5 Test register binding translation (t0 → texture index, s0 → sampler index, b0 → buffer index)
- [ ] 2.6 Test SV_Position/SV_VertexID semantic mapping to Metal attributes
- [ ] 2.7 Test shader cache — second compilation of same source returns cached result
- [ ] 2.8 Test compiled MSL can be loaded as MTLLibrary without errors
- [ ] 2.9 Verify 95%+ coverage on SlangCompiler

## 3. Documentation

- [ ] 3.1 Create `docs/engine/05-shader-pipeline/07-slang-metal-compilation.md`
- [ ] 3.2 Document HLSL → MSL compilation pipeline with Shader-Slang
- [ ] 3.3 Document binding translation table (HLSL registers → Metal indices)
- [ ] 3.4 Document why Slang was chosen over SPIRV-Cross
- [ ] 3.5 Update `docs/engine/03-rhi/10-future-backends.md` shader pipeline diagram
- [ ] 3.6 Update `docs/engine/06-build-toolchain/02-thirdparty-catalog.md` with Slang entry
