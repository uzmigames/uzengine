# Shader Pipeline — Reflection

> **Status**: Done
> **Source**: `runtime/core/shader/shader_reflection.h/.cpp`
> **Depends on**: SPIRV-Reflect (`thirdparty/spirv-reflect/`)

## Purpose

Shader reflection extracts resource binding metadata (what registers a shader uses, what types, which stages) from compiled bytecode. This data drives automatic root signature / descriptor set layout generation — no manual binding tables needed.

## How It Works

```
Compiled SPIR-V bytecode
       │
       ▼
SPIRV-Reflect library
       │
       ▼
ShaderReflectionResult
  ├── bindings: [
  │     { name="g_SceneConstants", type=CBV, set=0, binding=0 },
  │     { name="g_AlbedoTexture",  type=SRV, set=0, binding=1 },
  │     { name="g_Sampler",        type=Sampler, set=0, binding=2 },
  │   ]
  ├── entryPoint: "main"
  └── stage: kStagePixel
```

## API

```cpp
// Reflect a single shader stage
auto result = ReflectSPIRV(spirvData.data(), spirvData.size());
if (ReflectionSucceeded(result)) {
    for (auto& b : result.bindings) {
        UZ_LOG(Shader, Debug, "  {} (set={}, binding={}, type={})",
            b.name, b.set, b.binding, ShaderResourceTypeName(b.type));
    }
}

// Merge bindings across vertex + pixel stages
auto merged = MergeBindings({vsResult, psResult});
SortBindings(merged);  // Deterministic order: (set, binding)
```

## Resource Types

| Type | DX12 Register | HLSL Example |
|------|--------------|-------------|
| `ConstantBuffer` | `b` registers | `cbuffer SceneConstants : register(b0)` |
| `ShaderResource` | `t` registers | `Texture2D albedo : register(t0)` |
| `UnorderedAccess` | `u` registers | `RWTexture2D<float4> output : register(u0)` |
| `Sampler` | `s` registers | `SamplerState linearSampler : register(s0)` |
| `PushConstant` | Root constants | `[[vk::push_constant]] struct PushData { ... }` |

## Stage Merging

A graphics pipeline typically has both vertex and pixel shaders. Each stage may reference different (or overlapping) resource bindings. `MergeBindings()` combines them:

```cpp
// VS uses: SceneConstants (b0), VertexBuffer (t0)
// PS uses: SceneConstants (b0), AlbedoTexture (t1), Sampler (s0)

auto merged = MergeBindings({vsResult, psResult});
// Result:
//   SceneConstants  (b0) — stageFlags = kStageVertex | kStagePixel
//   VertexBuffer    (t0) — stageFlags = kStageVertex
//   AlbedoTexture   (t1) — stageFlags = kStagePixel
//   Sampler         (s0) — stageFlags = kStagePixel
```

Deduplication is by `(set, binding)` key. Stage flags are ORed together so the merged result shows all stages that reference each binding.

## Integration with Root Signature

The reflected bindings feed directly into root signature / pipeline layout generation:

1. Reflect VS + PS → merged bindings
2. For each binding, allocate a root parameter or descriptor range
3. SM6.6 bindless: most resources use the global heap, only push constants need root parameters
4. SM5: each binding maps to a descriptor table entry or root CBV

This is why reflection is critical for the SM6.6 → SM5 fallback: the SM5 path needs explicit binding layouts that the SM6.6 path doesn't.

## Rules

1. **ALWAYS** re-reflect after recompilation — stale reflection data causes PSO creation failures
2. **ALWAYS** merge bindings from all stages before generating the pipeline layout
3. **ALWAYS** use `SortBindings()` for deterministic output — hash-based merging has non-deterministic order
4. **NEVER** hardcode binding indices — always derive them from reflection
