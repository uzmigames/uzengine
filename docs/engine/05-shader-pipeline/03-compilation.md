# Shader Pipeline — Compilation

> **Status**: Done (Dual-compiler in 0.2.1)
> **Sources**: `runtime/core/shader/shader_compiler.h/.cpp`, `runtime/core/shader/shader_compiler_fxc.h/.cpp`
> **Depends on**: DXC (`dxcompiler.dll`) for SM6.0+, FXC (`d3dcompiler_47.dll`) for SM5.0

## Purpose

UzEngine uses a **dual-compiler pattern** to support a wide range of GPUs:

- **DXC (DirectX Shader Compiler)** for SM 6.0+ — modern DXIL output, fast compilation, full feature support
- **FXC (Effect Compiler)** for SM 5.0 — legacy DXBC output, wider GPU compatibility

Both compilers are dynamically loaded via `LoadLibrary`/`GetProcAddress` — they're not linked at build time. This allows shipping builds to run without compilers present (all shaders precompiled).

The `ShaderPipeline` class automatically routes shaders to the appropriate compiler based on target shader model.

## Supported Targets

| Output | Compiler | Flag | Target | Use Case |
|--------|----------|------|--------|----------|
| DXIL SM6.6 | DXC | `-T vs_6_6` | DX12 bindless | Modern GPU (NVIDIA RTX, AMD RDNA) |
| DXIL SM6.0 | DXC | `-T vs_6_0` | DX12 without bindless | Older GPU (NVIDIA Maxwell, AMD Polaris) |
| **DXBC SM5.0** | **FXC** | **`/T vs_5_0`** | **DX12 legacy** | **Pre-Turing GPU (RX 580, GTX 1060)** |
| DXIL SM5.1 | DXC | `-T vs_5_1` | DX12 (edge case) | Rarely used — prefer SM5.0 via FXC |
| SPIR-V 1.3 | DXC | `-spirv -fspv-target-env=vulkan1.3` | Vulkan | Cross-platform |

All shader stages are supported: Vertex, Pixel, Compute, Geometry, Hull, Domain, Amplification, Mesh.

## DXC Compiler (SM6.0+)

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
desc.model       = ShaderModelCompile::SM6_6;    // DXC target
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

## FXC Compiler (SM5.0)

```cpp
ShaderCompilerFXC fxc;
if (!fxc.Init("d3dcompiler_47.dll")) {
    UZ_LOG(Shader, Error, "Failed to load FXC");
    return;
}

ShaderCompileDesc desc;
desc.source      = hlslSourceCode;
desc.sourceName  = "legacy.hlsl";
desc.entryPoint  = "main";
desc.stage       = ShaderStageCompile::Pixel;
desc.model       = ShaderModelCompile::SM5_0;    // FXC target
desc.output      = ShaderOutputFormat::DXBC;     // FXC only produces DXBC
desc.defines     = {"LEGACY_GPU=1"};
desc.generateDebugInfo = true;

auto result = fxc.Compile(desc);
if (result.success) {
    // result.bytecode = compiled DXBC
} else {
    UZ_LOG(Shader, Error, "Compile failed: {}", result.errors);
}
```

## Automatic Compiler Routing (ShaderPipeline)

The `ShaderPipeline` class automatically selects the appropriate compiler:

```cpp
ShaderPipeline pipeline;
pipeline.SetCompilerDXC(dxcCompiler);       // Set DXC compiler
pipeline.SetCompilerFXC(fxcCompiler);       // Set FXC compiler

// Pipeline automatically routes based on target shader model:
auto result = pipeline.Compile(shaderDesc);  // Uses DXC if SM6.0+, FXC if SM5.0
```

The routing logic (`RequiresDXC()` in `ShaderPipeline`):

```cpp
// Returns true if DXC is required, false to use FXC
bool ShaderPipeline::RequiresDXC(ShaderModelCompile model) {
    return model != ShaderModelCompile::SM5_0;
    // SM5.0 → FXC (DXBC)
    // SM5.1, SM6.0, SM6.6 → DXC (DXIL)
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

## FXC Implementation Details

The FXC compiler is loaded and used internally by `ShaderCompilerFXC`. Key implementation notes:

### FXC Type Definitions

FXC types (IFxcBlob, FxcShaderMacro) are defined inline in `shader_compiler_fxc.h` to avoid conflicts with Windows SDK headers. This allows clean DX12 headers isolation — no `d3dcompiler.h` included anywhere except in `shader_compiler_fxc.cpp`.

### FXC Loading

```cpp
// Load d3dcompiler_47.dll via LoadLibrary
typedef HRESULT (*D3DCompileFunc)(
    LPCVOID pSrcData,
    SIZE_T SrcDataLen,
    LPCSTR pSourceName,
    const FxcShaderMacro* pDefines,
    LPCSTR pIncludeFileName,
    LPCSTR pEntrypoint,
    LPCSTR pTarget,
    UINT Flags1,
    UINT Flags2,
    IFxcBlob** ppCode,
    IFxcBlob** ppErrorMsgs
);

HMODULE fxcModule = LoadLibraryW(L"d3dcompiler_47.dll");
D3DCompileFunc D3DCompile = (D3DCompileFunc)GetProcAddress(fxcModule, "D3DCompile");
```

### SM5.0 Shader Format

DXBC bytecode (SM5.0) has different capabilities than DXIL:

| Feature | DXBC (SM5.0) | DXIL (SM6.0+) |
|---------|------------|--------------|
| Descriptor Heap Binding | ❌ No | ✅ Yes (Bindless) |
| Vertex Input Assembler | ✅ Yes | ❌ No (SV_VertexID) |
| Register-Bound Resources | ✅ Yes | ⚠️ Limited |
| Dynamic Indexing | ⚠️ Limited | ✅ Full |

## HLSL Dual-Path Pattern

All runtime shaders that support SM5.0 must use `#ifdef UZ_BINDLESS` to provide two code paths:

```hlsl
// shader.hlsl

#ifdef UZ_BINDLESS
// SM6.6 bindless path (DXC, DXIL)
cbuffer PushConstants : register(b0) {
    uint meshDataIndex;
    uint materialDataIndex;
};

[RootSignature("DescriptorTable(SRV(t0, space=0, numDescriptors=1000000))")]
StructuredBuffer<MeshData> g_MeshDataBuffer : register(t0);

float4 main(uint vertexID : SV_VertexID) : SV_POSITION {
    MeshData data = g_MeshDataBuffer[meshDataIndex];
    // ...
}

#else
// SM5.0 non-bindless path (FXC, DXBC)
cbuffer PerObject : register(b0) {
    float4x4 worldViewProj;
};

Buffer<float4> g_VertexPositions : register(t0);
Buffer<float2> g_VertexUVs : register(t1);

float4 main(uint vertexID : SV_VertexID) : SV_POSITION {
    float4 pos = g_VertexPositions[vertexID];
    // Fixed descriptor binding — no bindless indexing
    // ...
}
#endif
```

### Why Both Paths Needed

- **SM6.6 (bindless)**: Leverages modern GPU descriptor heaps for maximum flexibility. Single root signature, unlimited resource binding.
- **SM5.0 (Input Assembler)**: Pre-Turing GPUs don't support descriptor heaps. Must use Input Assembler pipeline with fixed POSITION/TEXCOORD layout or register-bound resources.

Rendering subsystem detects bindless support during device init and passes a `bool bindless` flag to all systems that render (TextRenderer, StatsOverlay, etc.).

## Rules

1. **ALWAYS** dynamically load both DXC and FXC — never link statically (shipping builds don't need them)
2. **ALWAYS** validate IIDs against `dxcapi.h` — never transcribe GUIDs manually
3. **ALWAYS** include `-Zi` (debug info) in dev builds — enables PIX/RenderDoc shader debugging
4. **NEVER** use a single `ShaderCompiler` or `ShaderCompilerFXC` instance from multiple threads
5. **Dual-path shaders MUST use `#ifdef UZ_BINDLESS`** — separate SM6.6 and SM5.0 code paths
6. **SM5.0 shaders cannot use descriptor heaps** — use Input Assembler or register-bound resources only
7. **Cache differentiation**: Content hash includes target shader model — SM5.0 and SM6.6 versions of the same shader are cached separately
