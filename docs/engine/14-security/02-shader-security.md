# Security — Shader Compilation & Bytecode Safety

> **Status**: Reference
> **Source**: `runtime/engine/renderer/core/dx12_pso_creation.cpp`, `runtime/engine/renderer/core/shader_compiler.cpp`, `runtime/engine/renderer/core/shader/shader_manager.cpp`
> **Related**: [Shader Pipeline — Compilation](../05-shader-pipeline/03-compilation.md), [Asset Pipeline — Importers & Cooking](../08-resource-system/08-asset-cooking.md)

## Purpose

Shader compilation involves invoking DirectX Shader Compiler (DXC) and handling shader bytecode. This document defines the security boundaries, validation rules, and error handling patterns that prevent malicious or malformed shaders from crashing the engine or consuming excessive resources.

## Part 1: Include Path Safety

### Trusted Input Boundary

Shader include paths (`ShaderCompileDesc.includeDirs`) are **internal build system input only**. They are never derived from user-supplied content:

```cpp
/// Shader compilation request.
struct ShaderCompileDesc {
    std::string sourceFile;  // Resolved absolute path from asset system
    std::string entryPoint;  // e.g., "PSMain", "CSMain"
    std::string profile;     // e.g., "ps_6_0", "cs_6_0"
    std::vector<std::string> defines;  // Compiler -D flags (validated)
    std::vector<std::string> includeDirs;  // Include paths (TRUSTED ONLY)
};
```

### Path Resolution Flow

```
Asset system:
  "shaders/pbr.hlsl" (user-authored, on disk)
        ↓
  ResolveEngineResource() → "C:\Projects\UzEngine\runtime\shaders\pbr.hlsl"
        ↓
Shader compiler:
  sourceFile = "C:\Projects\UzEngine\runtime\shaders\pbr.hlsl"
  includeDirs = ["C:\Projects\UzEngine\runtime\shaders\include"]
                (hardcoded, from build config)
        ↓
  DXC processes includes relative to includeDirs
```

### DXC Include Handler

DXC's default include handler resolves `#include "file.hlsli"` relative to `includeDirs`:

```cpp
void CompileShaderToDXIL(const ShaderCompileDesc& desc,
                         std::vector<uint8_t>& outBytecode) {
    // DXC compiler with default include handler
    ComPtr<IDxcCompiler3> compiler = GetDXCCompiler();
    ComPtr<IDxcIncludeHandler> includeHandler = GetDefaultIncludeHandler();

    // Configure include directories from build config (trusted)
    IDxcIncludeHandler* handlers[] = { includeHandler.Get() };

    // Compile — DXC will resolve relative includes using trusted paths
    ComPtr<IDxcResult> result = compiler->Compile(
        sourceCode.data(), sourceCode.size(),
        sourceFilePath.c_str(),
        entryPoint.c_str(),
        profile.c_str(),
        defines.data(), defines.size(),
        options.data(), options.size(),
        includeHandler.Get(),  // ← Uses trusted include dirs
        nullptr
    );
}
```

### No Symlink Traversal

The build system ensures that shader paths cannot traverse outside the `shaders/` directory:

```cpp
// In build config / asset system:
bool IsValidShaderPath(const std::string& path) {
    // Resolve to absolute path
    std::string resolved = ResolveEngineResource(path);

    // Check that resolved path is within shaders directory
    std::string shadersRoot = uz::GetExeDir() / "runtime" / "shaders";
    return resolved.find(shadersRoot) == 0;  // Starts with shaders root
}
```

### Rule: Include Paths Are Hardcoded

```cpp
// ✅ CORRECT — include paths from build config
const std::vector<std::string> kIncludeDirs = {
    uz::GetExeDir() + "/runtime/shaders/include",
};

// ❌ WRONG — user-supplied include path
// std::string customIncludePath = GetUserInput();
// compiler.SetIncludeDir(customIncludePath);  // NEVER
```

## Part 2: Bytecode Size Validation

### Max Bytecode Size

Before creating a PSO from shader bytecode, the engine enforces a maximum bytecode size:

```cpp
/// Maximum shader bytecode size — prevents OOM from huge compiled shaders.
/// Typical shaders: 10–500 KB. 64 MB is extremely generous.
constexpr size_t kMaxShaderBytecodeSize = 64 * 1024 * 1024;  // 64 MB
```

### Validation on PSO Creation

Every shader bytecode is validated before PSO creation:

```cpp
HRESULT CreatePSO(const DX12ShaderBytecode& vs,
                  const DX12ShaderBytecode& ps) {
    // Validate bytecode sizes
    UZ_ENSURE(vs.bytecode.size() <= kMaxShaderBytecodeSize,
        "Vertex shader bytecode too large ({})",
        vs.bytecode.size());
    UZ_ENSURE(ps.bytecode.size() <= kMaxShaderBytecodeSize,
        "Pixel shader bytecode too large ({})",
        ps.bytecode.size());

    // Create PSO — DX12 will parse and validate bytecode
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {
        .VS = { vs.bytecode.data(), vs.bytecode.size() },
        .PS = { ps.bytecode.data(), ps.bytecode.size() },
        // ... rest of PSO description ...
    };

    ComPtr<ID3D12PipelineState> pso;
    HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(pso.GetAddressOf()));
    return hr;
}
```

### Why 64 MB?

- **Typical shader**: 10–500 KB (even complex PBR with parallax mapping)
- **Pathological case**: A heavily unrolled shader (100+ loops) might reach 10 MB
- **64 MB limit**: ~100× the worst case, catches accidental pathological unrolling or compiler bugs

If a legitimate shader exceeds 64 MB, the build system should detect it and log a warning, but the engine will reject it to prevent memory exhaustion.

## Part 3: Debug Layer Filtering & Message Limits

### Debug Layer Configuration

The D3D12 Debug Layer can generate thousands of informational messages. To prevent memory exhaustion, the engine configures message filters:

```cpp
void ConfigureDebugLayer(ID3D12Device* device) {
    ComPtr<ID3D12InfoQueue> infoQueue;
    device->QueryInterface(IID_PPV_ARGS(infoQueue.GetAddressOf()));
    if (!infoQueue) {
        return;  // Not in debug layer, OK
    }

    // Set message count limit — stop processing after 1024 messages
    infoQueue->SetBreakOnCategory(D3D12_MESSAGE_CATEGORY_STATE_SETTING, TRUE);
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

    // Suppress known noisy message categories
    D3D12_MESSAGE_ID suppressedIds[] = {
        D3D12_MESSAGE_ID_COMMAND_LIST_DRAW_RENDER_TARGET_DESCRIPTOR_HEAP_FROM_CPU_WRITE,
        // ... more as needed
    };
    D3D12_INFO_QUEUE_FILTER filter = {
        .DenyList = {
            .NumIDs = _countof(suppressedIds),
            .pIDList = suppressedIds,
        },
    };
    infoQueue->AddStorageFilterEntries(&filter);

    // Enforce message limit
    if (infoQueue->GetNumStoredMessages() > kMaxDebugMessages) {
        UZ_LOG(Render, Warning,
            "Debug layer message queue exceeded {} messages, clearing oldest",
            kMaxDebugMessages);
        infoQueue->ClearStoredMessages();
    }
}
```

### Message Count Limit

```cpp
/// Maximum debug layer messages before clearing.
/// Prevents unbounded queue growth during development.
constexpr UINT64 kMaxDebugMessages = 1024;
```

### Why This Matters

The D3D12 Debug Layer's message queue can grow unbounded if not controlled. In a long render loop with thousands of draw calls, noisy state warnings can fill the queue, consuming GBs of RAM and slowing down development builds.

The solution:
1. **Suppress known-safe warnings** — state transitions that are intentional
2. **Enforce message limit** — if we hit 1024 messages, clear and continue
3. **Log-and-move-on** — don't stall the render loop on debug messages

## Part 4: Error Message Paths & End-User Exposure

### Shader Compilation Errors

When shader compilation fails, DXC provides detailed error messages including file paths and line numbers:

```cpp
bool CompileShaderFile(const std::string& filePath,
                       const std::string& entryPoint,
                       const std::string& profile,
                       std::vector<uint8_t>& outBytecode,
                       std::string& outErrors) {
    // ... compile ...

    if (FAILED(result->GetStatus(&status))) {
        ComPtr<IDxcBlobEncoding> errors;
        result->GetErrorBuffer(errors.GetAddressOf());

        outErrors = std::string(
            static_cast<const char*>(errors->GetBufferPointer()),
            errors->GetBufferSize());

        // ✅ CORRECT — log full path and error
        UZ_LOG(Asset, Error,
            "Failed to compile shader: file='{}'\nerrors:\n{}",
            filePath, outErrors);

        return false;
    }

    return true;
}
```

### Absolute Paths in Logs

Error messages may contain absolute filesystem paths:

```
Failed to compile shader: file='C:\Projects\UzEngine\runtime\shaders\pbr.hlsl'
errors:
pbr.hlsl:123: error X1234: Undefined variable 'invalidVar'
```

**This is intentional** — developers need to know which file and line failed during development.

### Don't Expose Logs to End Users

**Critical rule**: Shader compilation error logs should NEVER be sent to players or end users. These logs contain:
- Absolute filesystem paths (leaks development machine information)
- Source code file structure (reveals shader organization)
- Compiler internals (can reveal optimization strategies)

### Handling Compilation Failures Safely

If a shader fails to compile at runtime (e.g., dynamic shader generation, hot-reload), the engine should:

1. **Log full details** (with absolute paths) to the debug log
2. **Return a safe fallback** shader to the renderer
3. **Display a user-facing message** without revealing paths

```cpp
RHIPipelineHandle LoadShaderSafe(const std::string& shaderName) {
    std::string errors;
    std::vector<uint8_t> bytecode;

    if (!CompileShaderFile(path, "PSMain", "ps_6_0", bytecode, errors)) {
        // Log detailed error for developers
        UZ_LOG(Asset, Error,
            "Shader compilation failed for '{}': {}",
            shaderName, errors);

        // Return fallback shader (magenta/error visualization)
        return GetFallbackShader();
    }

    return CreatePipelineFromBytecode(bytecode);
}
```

The fallback shader typically renders in a diagnostic color (magenta, checkerboard) to make missing shaders visually obvious in the editor.

## Part 5: Validation Checklist

Before shipping a shader to the GPU, verify:

### Pre-Compilation

- ✓ Shader file exists and is readable
- ✓ Include paths are within the trusted `shaders/` directory
- ✓ Define names are alphanumeric (no command injection via -D flags)
- ✓ Entry point name is alphanumeric (no shell injection)

### Post-Compilation

- ✓ Bytecode size <= 64 MB
- ✓ Bytecode is valid DXIL (DXC output format)
- ✓ All entry points in PSO are present and have matching signatures

### PSO Creation

- ✓ Vertex and pixel shader bytecodes are provided (for graphics pipelines)
- ✓ Root signature is initialized and matches bytecode expectations
- ✓ Blend state, rasterizer state, and depth stencil state are valid

## Rules

1. **NEVER** allow user-supplied include paths — include paths come from build configuration only
2. **NEVER** skip bytecode size validation — malformed or pathological shaders can consume unbounded memory
3. **NEVER** expose shader error logs to end users — logs contain absolute paths and source structure information
4. **NEVER** send raw error messages to players — use fallback shaders and safe user-facing error text
5. **ALWAYS** validate bytecode size before PSO creation — catch size issues early, before GPU driver parsing
6. **ALWAYS** suppress known-safe debug layer warnings — prevents message queue exhaustion in development builds
7. **ALWAYS** log detailed error information to the debug log — developers need absolute paths and line numbers to fix shader bugs
8. **ALWAYS** return a fallback shader on compilation failure — don't crash the renderer, use diagnostic visualization
9. **ALWAYS** check DXC results with `FAILED(hr)` — compilation errors must be detected and handled
10. **NEVER** cache bytecode without size validation — invalid cache data can be loaded on restart

## See Also

- [Shader Pipeline — Compilation](../05-shader-pipeline/03-compilation.md) — detailed compilation process
- [Asset Pipeline — Cooking](../08-resource-system/08-asset-cooking.md) — how shaders are stored and versioned
- [Security — RAII Ownership Conventions](./01-raii-ownership.md) — COM object lifetime management in shader compilation
- [Observability — Log System](../04-observability/01-log-system.md) — what to log and what not to expose
