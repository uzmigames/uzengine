# Proposal: Shader-Slang MSL Compilation Pipeline

## Why

The Metal backend needs MSL (Metal Shading Language) shader bytecode. UzEngine uses single-source HLSL for all backends. The existing pipeline uses DXC for DXIL (DX12) and DXC -spirv for SPIR-V (Vulkan). For Metal, we adopt **Shader-Slang** to compile HLSL directly to MSL — replacing the originally planned SPIRV-Cross pipeline (HLSL → SPIR-V → MSL).

### Why Shader-Slang over SPIRV-Cross

| Approach | Pros | Cons |
|---|---|---|
| **SPIRV-Cross** (HLSL→SPIR-V→MSL) | Mature, battle-tested | Extra step, lossy translation, binding remapping issues |
| **Shader-Slang** (HLSL→MSL direct) | Single tool, high quality MSL output, actively maintained by NVIDIA/Khronos, supports HLSL semantics natively | Newer, experimental Metal target |

Shader-Slang is chosen because:
1. **Single compilation step**: HLSL → MSL directly, no intermediate SPIR-V
2. **Better HLSL compatibility**: Slang understands HLSL semantics natively (SV_Position, register bindings, etc.)
3. **Unified compiler**: Can also replace DXC for DXIL and SPIR-V output in the future, simplifying the entire shader pipeline
4. **Active development**: NVIDIA-backed with Khronos involvement, Metal target improving rapidly

## What Changes

### New/Modified Files

1. **`runtime/engine/shader/slang_compiler.h`** — SlangCompiler class wrapping the Slang API
2. **`runtime/engine/shader/slang_compiler.cpp`** — Implementation of HLSL → MSL compilation
3. **Modified: `runtime/engine/shader/shader_compiler.cpp`** — Route Metal target to SlangCompiler

### Shader Pipeline (Updated)

```
HLSL Source (.hlsl)
       │
       ├─── DXC (SM6.6) ──→ DXIL ──→ DX12 SM6.6 (bindless)
       ├─── DXC (SM6.0) ──→ DXIL ──→ DX12 SM6.0
       ├─── FXC (SM5.0) ──→ DXBC ──→ DX12 SM5.0 / DX11
       ├─── DXC (-spirv) ──→ SPIR-V ──→ Vulkan
       └─── Slang (metal) ──→ MSL ──→ Metal  ← NEW
```

### Slang API Usage

```cpp
// Pseudo-code: HLSL → MSL via Slang
SlangSession* session = spCreateSession();
SlangCompileRequest* request = spCreateCompileRequest(session);

spSetCodeGenTarget(request, SLANG_METAL);              // Target MSL
spAddSearchPath(request, shaderIncludePath);
spAddTranslationUnit(request, SLANG_SOURCE_LANGUAGE_HLSL, "shader");
spAddTranslationUnitSourceString(request, 0, "shader.hlsl", hlslSource);
spAddEntryPoint(request, 0, "VSMain", SLANG_STAGE_VERTEX);
spAddEntryPoint(request, 0, "PSMain", SLANG_STAGE_FRAGMENT);

int result = spCompile(request);
const char* mslCode = spGetEntryPointSource(request, 0, 0);
// → Feed MSL to MTLDevice::newLibraryWithSource()
```

### Design Decisions

- **Runtime compilation**: Slang compiles HLSL → MSL at asset cook time (offline) or at runtime for hot-reload. MSL source is then compiled to `MTLLibrary` by the Metal runtime.
- **Binding translation**: Slang automatically maps HLSL register bindings (`register(t0)`, `register(b0)`) to Metal argument buffer indices.
- **Thirdparty integration**: Slang distributed as a shared library (`libslang.dylib`) + headers.

## Impact

- Affected specs: `docs/engine/05-shader-pipeline/07-slang-metal-compilation.md` (new)
- Affected code: `runtime/engine/shader/` (new files), `thirdparty/slang/` (new)
- Breaking change: NO — new compilation target, existing DXC/FXC paths unchanged
- User benefit: HLSL shaders compile to MSL automatically, no manual shader porting
- Dependencies: Slang compiler library (`thirdparty/slang/`)
