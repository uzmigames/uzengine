# Shader Pipeline — Slang MSL Compilation

> **Status**: Planned (Milestone 2.0)
> **Source**: `runtime/core/shader/shader_compiler_slang.h/.cpp`
> **Depends on**: Shader-Slang, DXC (`dxcompiler.dll`), Metal backend

## Purpose

Metal requires shader bytecode in **MSL (Metal Shading Language)** format. Unlike DX12 (DXIL) and Vulkan (SPIR-V) which are bytecode formats, MSL is a high-level language similar to GLSL. UzEngine generates MSL via a two-stage pipeline:

1. **HLSL → SPIR-V** (DXC compiler, already available)
2. **SPIR-V → MSL** (Shader-Slang transpiler)

This design keeps shaders in single-source HLSL while supporting cross-platform compilation to all target formats.

## Updated Shader Pipeline Architecture

The complete shader pipeline now supports **five compilation paths**:

```
HLSL Source (.hlsl)
       │
       ├─── DXC (SM6.6) ──────────────→ DXIL ──────────────→ DX12 SM6.6 (bindless)
       │
       ├─── DXC (SM6.0) ──────────────→ DXIL ──────────────→ DX12 SM6.0
       │
       ├─── FXC (SM5.0) ──────────────→ DXBC ──────────────→ DX12 SM5.0 (legacy)
       │
       ├─── DXC (-spirv) ─────────────→ SPIR-V ────────────→ Vulkan (cross-platform)
       │
       └─── DXC (-spirv) → SPIR-V ──→ Shader-Slang ──────→ MSL ──────────→ Metal
                                         (SPIRV→MSL)          (text)
```

**Key insight**: DXC produces SPIR-V as an intermediate format. Shader-Slang then translates SPIR-V to MSL source code, which is compiled at runtime by the Metal compiler.

## Shader-Slang API Overview

Shader-Slang is invoked as a library:

```cpp
// Pseudo-code: SPIR-V → MSL compilation
#include <slang.h>

// 1. Create Slang session
slang::IGlobalSession* globalSession = slang::createGlobalSession();
slang::ISession* session = globalSession->createSession(nullptr);

// 2. Load SPIR-V bytecode
std::vector<uint8_t> spirvBytecode = ...;  // From DXC

// 3. Create Slang compile request
slang::ICompileRequest* request = session->createCompileRequest();
request->addSourceBlob(spirvBytecode.data(), spirvBytecode.size(), "input.spv");

// 4. Set target to MSL
request->setTarget(SLANG_METAL);  // Generate MSL
request->setTargetProfile(SLANG_PROFILE_METAL_3_0);  // Metal 3.0+

// 5. Compile
int resultCode = request->compile();
if (resultCode != 0) {
    const char* diagnostics = request->getDiagnosticOutput();
    UZ_LOG(Shader, Error, "Slang MSL compilation failed: {}", diagnostics);
    return;
}

// 6. Extract MSL source
slang::IBlob* mslBlob = nullptr;
request->getTargetCode(0, &mslBlob);
std::string mslSource = std::string((const char*)mslBlob->getBufferPointer(), mslBlob->getBufferSize());
```

## Compilation Flow with Binding Translation

The Metal backend automatically translates HLSL register semantics to Metal buffer indices:

```cpp
/// Compile HLSL → SPIR-V → MSL, with binding translation
ShaderCompileResultSlang ShaderCompilerSlang::Compile(const ShaderCompileDesc& desc) {
    // Step 1: Compile HLSL to SPIR-V (reuse existing DXC compiler)
    ShaderCompileResult dxcResult = dxcCompiler.Compile({
        .source = desc.source,
        .stage = desc.stage,
        .model = ShaderModelCompile::SM6_6,  // Always use SM6.6 for SPIRV
        .output = ShaderOutputFormat::SPIRV,
        .defines = desc.defines,
    });

    if (!dxcResult.success) {
        return { false, "", dxcResult.errors };
    }

    // Step 2: Translate SPIR-V → MSL via Shader-Slang
    slang::IGlobalSession* slangSession = GetSlangSession();

    // Load SPIR-V bytecode
    slang::ICompileRequest* request = slangSession->createCompileRequest();
    request->addSourceBlob(
        dxcResult.bytecode.data(),
        dxcResult.bytecode.size(),
        desc.sourceName.c_str()
    );

    // Set target and binding translation
    request->setTarget(SLANG_METAL);
    request->setTargetProfile(SLANG_PROFILE_METAL_3_0);

    // Binding translation: HLSL register → Metal buffer index
    request->setMetalArgumentBufferBindingOffset(0);  // Start at buffer 0

    // Compile
    int resultCode = request->compile();
    if (resultCode != 0) {
        return {
            false,
            "",
            std::string(request->getDiagnosticOutput())
        };
    }

    // Step 3: Extract MSL source
    slang::IBlob* mslBlob = nullptr;
    request->getTargetCode(0, &mslBlob);
    std::string mslSource(
        (const char*)mslBlob->getBufferPointer(),
        mslBlob->getBufferSize()
    );

    // Step 4: Compile MSL → Metal bytecode (at runtime, via MTLLibrary)
    // Note: MTLLibrary compilation happens lazily when shader is first used

    return {
        true,
        mslSource,  // Return MSL source for caching + runtime compilation
        ""
    };
}
```

## Binding Translation Details

Shader-Slang automatically translates HLSL register semantics to Metal buffer indices:

| HLSL Semantic | Register | Slang Output | Metal Binding |
|---|---|---|---|
| Texture | `t0-t999` | `texture(0, 0)` | Argument buffer slot 0-999 |
| Sampler | `s0-s99` | `sampler(1000, 0)` | Argument buffer slot 1000-1099 |
| Constant Buffer | `b0-b99` | `constant* (1100)` | Argument buffer slot 1100-1199 |
| RW Texture | `u0-u99` | `texture(1200, 0)` rw | Argument buffer slot 1200-1299 |

**Example HLSL → MSL translation:**

```hlsl
// Input HLSL
cbuffer PushConstants : register(b0) {
    uint meshDataIndex;
};

Texture2D albedoMap : register(t0);
SamplerState albedoSampler : register(s0);

float4 main(float2 uv : TEXCOORD0) : SV_TARGET {
    return albedoMap.Sample(albedoSampler, uv);
}
```

```cpp
// Output MSL (generated by Slang)
struct PushConstants {
    uint meshDataIndex;
};

using namespace metal;

fragment float4 main(
    float2 uv [[user(locn0)]],
    constant PushConstants* pushConstants [[buffer(1100)]],
    texture2d<float> albedoMap [[texture(0)]],
    sampler albedoSampler [[sampler(1000)]]
) {
    return albedoMap.sample(albedoSampler, uv);
}
```

**Key observation**: Metal buffer indices in generated MSL match HLSL register semantics:
- `t0` → `buffer(0)` (texture)
- `s0` → `buffer(1000)` (sampler)
- `b0` → `buffer(1100)` (constant buffer)

This mapping is **compile-time deterministic** — no runtime translation needed.

## Error Handling

Slang compilation failures are logged with line numbers and descriptive messages:

```cpp
ShaderCompileResultSlang ShaderCompilerSlang::Compile(const ShaderCompileDesc& desc) {
    slang::ICompileRequest* request = slangSession->createCompileRequest();
    request->addSourceBlob(spirv.data(), spirv.size(), desc.sourceName.c_str());
    request->setTarget(SLANG_METAL);
    request->setTargetProfile(SLANG_PROFILE_METAL_3_0);

    int resultCode = request->compile();
    if (resultCode != 0) {
        const char* diagnostics = request->getDiagnosticOutput();
        std::string error = std::string(diagnostics);

        // Log with context
        UZ_LOG(Shader, Error,
            "Slang MSL compilation failed for {}:\n{}",
            desc.sourceName,
            error);

        return { false, "", error };
    }

    // ... extract MSL ...
}
```

**Diagnostic output includes:**
- Line numbers (from SPIR-V source mapping)
- Error codes (e.g., unsupported instruction)
- Suggestions (e.g., "Wave ops not supported on Metal 3.0")

## Shader Cache

The shader pipeline caches both **SPIR-V bytecode** and **MSL source code**:

```
HLSL Source
    ↓
Cache lookup (hash = xxH64(source + defines + SM6.6 + "spirv" + dxc_version))
    ├── HIT → use cached SPIR-V
    └── MISS → compile with DXC → cache SPIR-V
    ↓
Slang MSL translation
    ↓
Cache lookup (hash = xxH64(spirv_bytecode + "msl" + slang_version))
    ├── HIT → use cached MSL
    └── MISS → compile with Slang → cache MSL
    ↓
MSL Runtime Compilation (MTLLibrary)
    ↓
Cached MTL::Function (final bytecode)
```

**Why two caches?**

1. **SPIR-V cache** (from DXC): Reusable for Vulkan (no Slang needed). Isolates Slang from Vulkan changes.
2. **MSL cache** (from Slang): Caches transpilation result. If Slang version updates, only MSL cache invalidates (SPIR-V unchanged).

**Cache keys:**

```cpp
// SPIR-V cache key (shared with Vulkan)
uint64_t spirvHash = xxH64(source + defines + "SM6.6" + "spirv" + dxcVersion);

// MSL cache key (Metal-specific)
uint64_t mslHash = xxH64(spirvBytecode + "msl" + slangVersion);

// MTL::Function cache key (final)
uint64_t functionHash = xxH64(mslSource + mtlCompilerVersion);
```

## Hot-Reload Support

During development, shaders are reloaded on file change:

```cpp
// Pseudo-code: Hot-reload flow
class ShaderHotReloadManager {
    void OnFileChanged(const std::string& path) {
        // Mark shader dirty (don't recompile yet)
        shader->markDirty();
    }

    void Tick() {
        // Before each frame, check dirty shaders
        for (auto& shader : dirtyShaders) {
            // Invalidate caches
            spirvCache->Remove(shader->GetCacheKey());
            mslCache->Remove(shader->GetCacheKey());

            // Recompile: HLSL → SPIR-V → MSL → MTL::Function
            shader->Recompile();

            // Rebind any pipelines using this shader
            RebindPipelinesUsing(shader);
        }
    }
};
```

**Hot-reload steps:**

1. FileWatcher detects `.hlsl` change
2. Mark shader dirty (dependency graph propagates)
3. On next frame, invalidate SPIR-V + MSL caches
4. Recompile: DXC + Slang (same as first compile)
5. Recreate `MTL::Function` from new MSL source
6. Rebind pipelines (PSOs reference the function)

**Performance**: Hot-reload takes ~50-100ms on modern hardware (DXC + Slang compilation). Development builds accept this latency; shipping builds have all shaders precompiled.

## Offline Cook Integration

For shipping builds, shaders are compiled offline via `uzcook` tool:

```bash
# Offline shader cooking (Milestone 2.0+ feature)
$ uzcook cook --shaders-only
  ✓ Discovered 47 .hlsl files
  ✓ Compiled HLSL → SPIR-V (DXC)
  ✓ Transpiled SPIR-V → MSL (Slang)
  ✓ Compiled MSL → MTL bytecode (Metal compiler)
  ✓ Packaged into game.uzpak
```

Output `.uzshader` archive contains:

```
game.uzpak
├── shaders/
│   ├── pbr.fs.spirv          (intermediate SPIR-V)
│   ├── pbr.fs.msl            (intermediate MSL source)
│   ├── pbr.fs.metal          (final Metal bytecode)
│   ├── pbr.fs.metadata.json   (reflection data)
│   └── ... (other shaders)
└── shaders.manifest.json      (index)
```

Shipping code loads `.metal` files directly — no compilation, no DXC, no Slang, no SPIR-V needed.

## Metal Compiler Integration

MSL source code is compiled to Metal bytecode via `MTLLibrary`:

```cpp
/// Compile MSL source → MTL::Function
MTL::Function* CompileMSLToMetal(
    MTL::Device* device,
    const std::string& mslSource,
    const std::string& entryPoint
) {
    // Create library from source
    NS::String* nsSource = NS::String::string(mslSource.c_str(), NS::StringEncoding::UTF8);
    NS::Error* error = nullptr;

    MTL::Library* library = device->newLibrary(nsSource, nullptr, &error);
    if (!library) {
        UZ_LOG(Shader, Error, "MTL::Library compilation failed: {}",
            error->localizedDescription()->utf8String());
        return nullptr;
    }

    // Extract function
    NS::String* nsEntry = NS::String::string(entryPoint.c_str(), NS::StringEncoding::UTF8);
    MTL::Function* function = library->newFunction(nsEntry);

    return function;
}
```

**Compilation timing:**

- **Dev builds**: MSL compiled at runtime (per shader, ~5-20ms on M3)
- **Ship builds**: All `.metal` pre-compiled offline (zero runtime cost)

## Thread Safety

Shader-Slang sessions are NOT thread-safe:

```cpp
// ❌ WRONG — shared session from multiple threads
slang::ISession* slangSession = CreateSlangSession();
parallel_for(shaders, [slangSession](auto& shader) {
    slangSession->createCompileRequest();  // RACE CONDITION
});

// ✅ CORRECT — one session per thread
std::vector<slang::ISession*> sessionPerThread(threadCount);
parallel_for(shaders, [&sessionPerThread](auto& shader, int threadIdx) {
    sessionPerThread[threadIdx]->createCompileRequest();  // Safe
});
```

For now, Slang compilation is single-threaded (same as DXC). Future optimization could parallelize via thread-local sessions.

## Rules

1. **NEVER** use Shader-Slang to compile HLSL directly — always use DXC first (DXC → SPIR-V → Slang → MSL)
2. **ALWAYS** cache both SPIR-V and MSL** — avoid redundant Slang transpilation on identical SPIR-V
3. **ALWAYS** validate Slang compilation errors before caching MSL** — corrupted MSL in cache breaks subsequent loads
4. **NEVER** hardcode Metal version** — use dynamic feature detection (`MTL::GPU::supportsFamily()`) and compile to compatible Metal version
5. **ALWAYS** support hot-reload** — dev builds must invalidate both SPIR-V and MSL caches on shader change
6. **ALWAYS** include Slang version in cache key** — Slang updates may change MSL output for identical SPIR-V
7. **NEVER** manually author MSL** — all MSL must be generated from SPIR-V via Slang
