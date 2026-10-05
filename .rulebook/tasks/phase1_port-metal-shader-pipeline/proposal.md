# Proposal: Metal Shader Pipeline Port (phase1_port-metal-shader-pipeline)

## Why

The Sponza Intel-parity work (phase1_fix-sponza-intel-lighting-parity) needs visual
verification, but the full scene renderer cannot run on macOS: runtime shader
compilation (ShaderCompiler/ShaderCompilerFXC/ShaderPipeline) is entirely
`_WIN32` — the Metal backend compiles MSL source (`newLibraryWithSource`), and
nothing produces MSL from the engine's 145 embedded HLSL shaders.

The repo already contains a complete `SlangCompiler` wrapper (HLSL→MSL via the
Slang C API, dlopen-loaded) and the vendored Slang runtime for macOS arm64 is now
in place (`thirdparty/slang/bin/libslang.dylib`, verified: slangc compiles
HLSL→MSL locally). The port wires that path into the renderer.

## What Changes

### New Systems
1. **Slang build integration** — CMake include/rpath/deploy for the vendored
   Slang dylibs; smoke test proving HLSL→MSL compilation in-process.
2. **ShaderPipeline macOS backend** — real `LoadBytecode*`/`CompileShader` via
   `SlangCompiler` (output = MSL source text carried in the existing
   `RHIShaderDesc.bytecode` field, which the Metal backend already interprets as
   MSL UTF-8 source); SM5 register-bound shader path (UZ_BINDLESS undefined).
3. **ShaderManager macOS backend** — Init/CreateShader/GetBytecode via the Slang
   pipeline; entry-point mapping `main` → `main_0` (Slang MSL rename).
4. **Metal compute PSO** — `MetalDevice::CreateComputePipeline` (currently
   "not yet implemented"; most engine passes are compute).

### Refactoring
- Replaces the temporary non-Windows failure fallbacks in `shader_pipeline.cpp` /
  `shader_manager.cpp` with the real Slang-backed implementation.
- Entry-point name standardization between Slang MSL output and Metal
  `newFunctionWithName`.

## Impact
- Affected specs: `/.rulebook/specs/` (new delta: metal-shader-pipeline)
- Affected code: `runtime/engine/shader/shader_pipeline.cpp`,
  `runtime/engine/renderer/core/shader/shader_manager.{h,cpp}`,
  `runtime/engine/rhi/metal/*`, `runtime/CMakeLists.txt`, `runtime/build.zig`
- Breaking change: NO — Windows path untouched; macOS gains functionality
- User benefit: Sponza sample renders on macOS/Metal; enables the Intel-parity
  visual comparison (phase1_fix-sponza-intel-lighting-parity items 5.2–5.4)
- Enables: full renderer on macOS, future Vulkan-via-Slang path
