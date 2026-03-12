# Shader Pipeline — Compilation

> **Status**: Done
> **Source**: `runtime/core/shader/shader_compiler.h/.cpp`
> **Depends on**: DXC (`dxcompiler.dll`, dynamically loaded)

## Purpose

The `ShaderCompiler` dynamically loads DXC (DirectX Shader Compiler) and compiles HLSL source to DXIL or SPIR-V bytecode. DXC is NOT linked at build time — it's loaded via `LoadLibrary`/`GetProcAddress`, which allows the engine to run even if DXC is missing (shipping builds don't need it).

## Supported Targets

| Output | DXC Flag | Target |
|--------|----------|--------|
| DXIL SM6.6 | `-T vs_6_6` | DX12 bindless (primary) |
| DXIL SM6.0 | `-T vs_6_0` | DX12 without bindless |
| DXIL SM5.1 | `-T vs_5_1` | DX12 legacy |
| SPIR-V | `-spirv -fspv-target-env=vulkan1.3` | Vulkan |

All shader stages are supported: Vertex, Pixel, Compute, Geometry, Hull, Domain, Amplification, Mesh.

## API

```cpp
ShaderCompiler compiler;
if (!compiler.Init("dxcompiler.dll")) {
    UZ_LOG(Shader, Error, "Failed to load DXC");
    return;
}

ShaderCompileDesc desc;
desc.source      = hlslSourceCode;
desc.sourceName  = "pbr.hlsl";
desc.entryPoint  = "main";
desc.stage       = ShaderStageCompile::Pixel;
desc.model       = ShaderModelCompile::SM6_6;
desc.output      = ShaderOutputFormat::DXIL;
desc.defines     = {"USE_PBR=1", "HAS_NORMAL_MAP=1"};
desc.generateDebugInfo = true;  // -Zi for PIX/RenderDoc

auto result = compiler.Compile(desc);
if (result.success) {
    // result.bytecode = compiled DXIL
} else {
    UZ_LOG(Shader, Error, "Compile failed: {}", result.errors);
}
```

## Thread Safety

Each `ShaderCompiler` instance owns its own DXC COM objects (`IDxcCompiler3`, `IDxcUtils`). Multiple instances can compile in parallel on different threads. A single instance must NOT be used concurrently.

```cpp
// Parallel compilation:
std::vector<ShaderCompiler> compilers(threadCount);
for (auto& c : compilers) c.Init();

// Each thread uses its own compiler instance
parallel_for(shaders, [&](auto& shader, int threadIdx) {
    auto result = compilers[threadIdx].Compile(shader.desc);
    // ...
});
```

## DXC Loading

DXC is loaded from exe-relative path (via `uz::GetExeDir()` / `uz::ResolveEngineResource()`). The build system deploys `dxcompiler.dll` and `dxil.dll` to `zig-out/bin/` during build.

### Critical IID Bug (Lesson Learned)

During Milestone 1.2, `DxcCreateInstance` returned `E_NOINTERFACE` because the IID for `IDxcUtils` was wrong — the code used the CLSID value `{6245D6AF-...}` instead of the correct IID `{4605C4CB-2019-492A-ADA4-65F20BB7D67F}`. Always validate IIDs against `CROSS_PLATFORM_UUIDOF` declarations in `dxcapi.h`.

## Rules

1. **ALWAYS** dynamically load DXC — never link it statically (shipping builds don't need it)
2. **ALWAYS** validate IIDs against `dxcapi.h` — never transcribe GUIDs manually
3. **ALWAYS** include `-Zi` (debug info) in dev builds — enables PIX/RenderDoc shader debugging
4. **NEVER** use a single `ShaderCompiler` instance from multiple threads
