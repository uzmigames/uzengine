# RHI — DX11 Backend & FXC Compiler Integration

> **Status**: Planned
> **Source**: FXC integration plan, `runtime/core/shader/shader_compiler.h`, `runtime/core/shader/shader_pipeline.cpp`
> **Depends on**: RHI interface (Done), DX12 backend (Done), Shader Pipeline (Done)
> **References**: UE5 dual-compiler pattern (DXC for SM6+, FXC for SM5)

## Purpose

Support legacy GPUs that lack DX12 SM6.0+ capability. This involves two orthogonal changes:

1. **FXC compiler module** — `d3dcompiler_47.dll` as a second shader compiler for SM5.0 targets
2. **DX11 backend** — `rhi_dx11/` implementing IRHIDevice/IRHICommandList/IRHISwapChain/IRHIFence via D3D11

The FXC compiler also enables the DX12 SM5.0 fallback path — DXC cannot compile SM5.0 (only SM5.1+).

## The Problem

DXC (`dxcompiler.dll`) only supports SM 6.0+. When the engine detects an older GPU and falls back to SM 5.0, shader compilation fails with `"invalid profile vs_5_1"`. We need FXC (`d3dcompiler_47.dll`) as a second compiler for SM 5.0 targets — both for DX12 SM5 fallback and the future DX11 backend.

This follows UE5's dual-compiler pattern: **SM >= 6.0 → DXC, SM 5.0 → FXC**.

## Dual Compiler Architecture

```
HLSL Source (.hlsl)
       │
       ├─── DXC (SM6.0+) ──→ DXIL ──→ DX12 SM6.6 (bindless)
       │                             ──→ DX12 SM6.0 (wave ops)
       │
       ├─── FXC (SM5.0)  ──→ DXBC ──→ DX12 SM5.0 (legacy DX12)
       │                             ──→ DX11 SM5.0 (legacy backend)
       │
       ├─── DXC (-spirv)  ──→ SPIR-V ──→ Vulkan
       └─── DXC (-spirv)  ──→ SPIR-V ──→ SPIRV-Cross ──→ MSL ──→ Metal
```

### Compiler Routing

```cpp
static bool RequiresDXC(ShaderModelCompile model) {
    return model >= ShaderModelCompile::SM6_0;
}
```

In the shader pipeline:
- `RequiresDXC(desc.model)` → route to `ShaderCompiler` (DXC)
- Otherwise → route to `ShaderCompilerFXC` (FXC)

## FXC Module — `ShaderCompilerFXC`

### API

```cpp
/// Dynamically loads FXC (d3dcompiler_47.dll) and compiles HLSL to DXBC.
///
/// Same interface pattern as ShaderCompiler (DXC) for consistency.
/// Loads d3dcompiler_47.dll via LoadLibraryW + GetProcAddress("D3DCompile").
///
/// # Output Format
/// DXBC bytecode (ID3DBlob) — consumed by DX11 and DX12 SM5.0 paths.
///
/// # Thread Safety
/// Each instance is independent. Not safe for concurrent use within a single instance.
class ShaderCompilerFXC {
public:
    bool Init(const std::string& fxcPath = "d3dcompiler_47.dll");
    [[nodiscard]] bool IsInitialized() const;
    [[nodiscard]] ShaderCompileResult Compile(const ShaderCompileDesc& desc);
    void Shutdown();

private:
    /// Maps stages to FXC profile strings: vs_5_0, ps_5_0, cs_5_0, etc.
    static std::string GetTargetProfile(ShaderStageCompile stage, ShaderModelCompile model);

    void* m_fxcModule = nullptr;  // HMODULE for d3dcompiler_47.dll
    void* m_d3dCompileFn = nullptr;  // D3DCompile function pointer
};
```

### Key Differences from DXC

| Aspect | DXC (ShaderCompiler) | FXC (ShaderCompilerFXC) |
|--------|---------------------|------------------------|
| DLL | `dxcompiler.dll` | `d3dcompiler_47.dll` |
| API | COM-based (`IDxcCompiler3`) | C function (`D3DCompile`) |
| Output | DXIL (`IDxcBlob`) | DXBC (`ID3DBlob`) |
| SM range | 5.1–6.7 | 4.0–5.0 |
| Defines | `-D NAME=VALUE` args | `D3D_SHADER_MACRO` array |
| Include handler | `IDxcIncludeHandler` | `ID3DInclude` |

### FXC Compilation Flow

```
ShaderCompileDesc (SM5.0)
    │
    ├── 1. Build D3D_SHADER_MACRO array from desc.defines
    ├── 2. Map stage → profile string (e.g., ShaderStageCompile::Vertex → "vs_5_0")
    ├── 3. Set compile flags:
    │      D3DCOMPILE_OPTIMIZATION_LEVEL3  (release)
    │      D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION  (debug)
    │      D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR
    ├── 4. Call D3DCompile(source, ...) → ID3DBlob* code, ID3DBlob* errors
    ├── 5. Copy ID3DBlob → ShaderCompileResult.bytecode (std::vector<uint8_t>)
    └── 6. Release ID3DBlobs
```

## DLL Deployment

### Vendoring

```
runtime/thirdparty/fxc/bin/x64/d3dcompiler_47.dll
```

Source: `C:\Program Files (x86)\Windows Kits\10\Redist\D3D\x64\d3dcompiler_47.dll`

### Build System (Zig)

Same pattern as DXC DLLs — `addInstallBinFile` in `runtime/build.zig`:

```zig
// Deploy FXC DLL to zig-out/bin/
const fxc_dll = b.path("thirdparty/fxc/bin/x64/d3dcompiler_47.dll");
b.installBinFile(fxc_dll, "d3dcompiler_47.dll");
```

At runtime, `ShaderCompilerFXC::Init()` loads from `uz::GetExeDir() / "d3dcompiler_47.dll"`.

## Shader Pipeline Changes

### ApplyBindlessConfig Update

```cpp
// Before (broken for SM5.0):
static void ApplyBindlessConfig(ShaderCompileDesc& desc, bool bindless) {
    if (bindless) {
        desc.defines.push_back("UZ_BINDLESS=1");
        desc.model = ShaderModelCompile::SM6_6;
    } else {
        desc.model = ShaderModelCompile::SM5_1;  // ← DXC can't compile SM5.0
    }
}

// After (correct routing):
static void ApplyBindlessConfig(ShaderCompileDesc& desc, bool bindless) {
    if (bindless) {
        desc.defines.push_back("UZ_BINDLESS=1");
        desc.model = ShaderModelCompile::SM6_6;
    } else {
        desc.model = ShaderModelCompile::SM5_0;  // ← FXC handles this
    }
}
```

### PIMPL Extension

```cpp
struct ShaderPipeline::Impl {
    ShaderVFS vfs;
    ShaderSourceManager sourceManager;
    ShaderDependencyGraph depGraph;
    ShaderCache cache;
    ShaderHotReload hotReload;
    ShaderCompiler* compiler = nullptr;      // DXC (SM6.0+)
    ShaderCompilerFXC* compilerFXC = nullptr; // FXC (SM5.0)
    Config config;
    // ...
};
```

### Compile Routing

```cpp
ShaderCompileResult ShaderPipeline::CompileShader(const ShaderCompileDesc& desc) {
    if (RequiresDXC(desc.model)) {
        return m_impl->compiler->Compile(desc);
    } else {
        return m_impl->compilerFXC->Compile(desc);
    }
}
```

## ShaderTarget Enum Update

```cpp
enum class ShaderTarget : uint8_t {
    DXIL_SM6_6,   // DX12 Shader Model 6.6 (bindless)
    DXIL_SM6_0,   // DX12 Shader Model 6.0 (wave ops)
    DXBC_SM5_1,   // DX12 Shader Model 5.1 (DXC legacy)
    DXBC_SM5_0,   // DX12 SM5.0 / DX11 SM5.0 (FXC)
    SPIRV_1_6,    // Vulkan SPIR-V 1.6
};
```

## DX11 Backend — `rhi_dx11/`

### Scope

Minimal fallback for GPUs without DX12 support. Feature-reduced path:

| Feature | DX12 SM6.6 | DX12 SM5.0 | DX11 SM5.0 |
|---------|-----------|-----------|-----------|
| Bindless | Yes | No | No |
| Compute shaders | Yes | Yes | Limited |
| UAV | Yes | Yes | Limited |
| Structured buffers | Yes | Yes | Limited |
| Async compute | Yes | Possible | No |
| Mesh shaders | Yes | No | No |
| Ray tracing | Yes | No | No |
| Input assembler | Optional | Required | Required |
| Shader format | DXIL | DXBC (via FXC) | DXBC (via FXC) |

### D3D11 Interface Mapping

| UzEngine RHI | DX12 | DX11 |
|-------------|------|------|
| `IRHIDevice` | `ID3D12Device` | `ID3D11Device` + `ID3D11DeviceContext` |
| `IRHICommandList` | `ID3D12GraphicsCommandList` | `ID3D11DeviceContext` (deferred) |
| `IRHISwapChain` | `IDXGISwapChain4` | `IDXGISwapChain1` |
| `IRHIFence` | `ID3D12Fence` | `ID3D11Query(EVENT)` + CPU sync |
| Descriptor heap | `ID3D12DescriptorHeap` | N/A (implicit binding) |
| Root signature | `ID3D12RootSignature` | N/A (register-based) |
| Resource barriers | `ResourceBarrier()` | N/A (driver-managed) |
| Memory allocator | D3D12MA | N/A (`CreateBuffer`/`CreateTexture2D` manage memory) |

### Key Architectural Differences

**Command Model**: DX12 uses explicit command lists that are recorded and submitted. DX11 uses an immediate context (single-threaded) or deferred contexts. The `IRHICommandList` wrapper for DX11 uses a deferred context, flushed to the immediate context on "submit".

**State Management**: DX12 has pipeline state objects (PSOs) that bundle all state. DX11 sets state individually (`IASetInputLayout`, `VSSetShader`, `PSSetShader`, `OMSetRenderTargets`). The DX11 backend tracks current state and applies delta changes.

**Resource Barriers**: DX12 requires explicit state transitions. DX11 has none — the driver manages resource states automatically. The DX11 `ResourceBarrier()` implementation is a no-op.

**Memory**: DX12 requires manual memory management (D3D12MA). DX11 manages GPU memory internally — `CreateBuffer`/`CreateTexture2D` handle allocation.

### Implementation Files

```
runtime/core/rhi/dx11/
├── dx11_device.h/.cpp         — IRHIDevice via ID3D11Device
├── dx11_command_list.h/.cpp   — IRHICommandList via ID3D11DeviceContext (deferred)
├── dx11_swapchain.h/.cpp      — IRHISwapChain via IDXGISwapChain1
├── dx11_fence.h/.cpp          — IRHIFence via ID3D11Query(EVENT)
├── dx11_resource_creation.h/.cpp — Buffer/Texture/Shader/Pipeline creation helpers
└── dx11_types.h               — DX11-specific internal types (not exposed to RHI)
```

### Priority

The DX11 backend is **lowest priority** and may be skipped entirely if minimum hardware requirements don't demand it. The FXC compiler integration (for DX12 SM5.0 fallback) is the immediate priority.

## Implementation Order

| Step | What | Why |
|------|------|-----|
| 1 | Vendor `d3dcompiler_47.dll` + deploy in `build.zig` | DLL must exist before compiler can load it |
| 2 | Add `DXBC_SM5_0` to `ShaderTarget` enum | Cache key differentiation |
| 3 | Implement `ShaderCompilerFXC` class | The compiler itself |
| 4 | Add FXC routing in `ShaderPipeline` | Pipeline uses correct compiler per SM |
| 5 | Fix `ApplyBindlessConfig` (SM5_1 → SM5_0) | Non-bindless path targets FXC-compatible SM |
| 6 | Remove debug SM5.1 force override in `dx12_device.cpp` | Clean up dev hack |
| 7 | Update demo `targetBackend` strings (`dx12_sm51` → `dx12_sm50`) | Demos use correct fallback target |
| 8 | (Later) Implement `rhi_dx11/` backend | Full DX11 support |

## Verification

1. `cd runtime && zig build` — must compile with FXC DLL deployed to `zig-out/bin/`
2. Force SM5.0 in `dx12_device.cpp` temporarily, run demos — shaders compile via FXC
3. Remove force override, run on SM6.6 GPU — DXC path still works
4. Verify `zig-out/bin/d3dcompiler_47.dll` exists after build

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Dual compiler | DXC (SM6+) + FXC (SM5) | FXC only (DX11 renderer) | **DXC (SM6+) + FXC (SM5)** |
| DX11 support | Full backend (`D3D11RHI`) | Full backend (`RenderingDeviceD3D12` is DX12 only, old `GLES3` covers DX11-era) | **Planned minimal fallback** |
| SM5 compilation | FXC via `D3DCompile` | FXC via `D3DCompile` | **FXC via `D3DCompile` (dynamic load)** |
| Shader bytecode | DXBC (SM5) / DXIL (SM6+) | DXBC only | **DXBC (SM5) / DXIL (SM6+)** |

## Rules

1. **ALWAYS route SM5.0 to FXC, SM6.0+ to DXC** — DXC cannot compile SM5.0
2. **NEVER use FXC for SM6.0+** — FXC doesn't understand DXIL, wave intrinsics, or bindless
3. **ALWAYS deploy `d3dcompiler_47.dll`** alongside the engine executable
4. **ALWAYS use the same `ShaderCompileDesc` struct** for both compilers — the pipeline routes transparently
5. **NEVER include `d3dcompiler.h`** above the shader compiler module — dynamic loading only
