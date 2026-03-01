# Shader Variant / Permutation System Spec

## Overview

UzEngine shaders are uber-shaders using `#ifdef` to toggle features (texture presence, alpha modes, skinning, shadow quality). The **Shader Variant System** manages which combinations of defines are active for a given material, compiles the correct permutation, caches the resulting PSO, and handles hot-reload of all cached variants when a shader source file changes.

---

## Problem Statement

A single `pbr.hlsl` file must handle many configurations:

- Albedo texture present vs. flat color
- Normal map present vs. no normal mapping
- Alpha mode: OPAQUE, MASK, BLEND
- Skinned mesh vs. static mesh
- Instanced draw vs. per-object cbuffer
- Shadow quality: LOW (no PCF), MED (PCF 2x2), HIGH (PCF 3x3)
- Number of CSM cascades: 1, 2, or 3

Naively compiling every combination is a combinatorial explosion. The variant system provides a deterministic key and on-demand compilation so only the permutations actually used at runtime are compiled.

---

## ShaderVariantKey

A `ShaderVariantKey` is a 64-bit hash computed from the sorted list of active `#define` name-value pairs.

```cpp
// src/renderer/shader_compiler/shader_variant.h

struct ShaderVariantKey {
    uint64_t hash;  // FNV-1a over sorted define strings

    bool operator==(const ShaderVariantKey& other) const { return hash == other.hash; }
    bool operator<(const ShaderVariantKey& other) const  { return hash < other.hash; }
};

// Compute from a list of defines
// Each define is "NAME" or "NAME=VALUE"
ShaderVariantKey compute_variant_key(Span<const char*> defines);
```

### Hash Computation

1. Collect all active defines as `"NAME=VALUE"` strings (or `"NAME=1"` if no value).
2. Sort lexicographically.
3. Concatenate with `\n` separator.
4. Compute FNV-1a 64-bit hash.

This ensures the same set of defines always produces the same key regardless of insertion order.

---

## Define Categories

### Texture Presence (auto-detected from material)

| Material param        | Condition        | Define generated      |
|-----------------------|------------------|-----------------------|
| `albedo_texture`      | != null          | `HAS_ALBEDO_TEX`     |
| `normal_texture`      | != null          | `HAS_NORMAL_TEX`     |
| `roughness_texture`   | != null          | `HAS_ROUGHNESS_TEX`  |
| `metallic_texture`    | != null          | `HAS_METALLIC_TEX`   |
| `ao_texture`          | != null          | `HAS_AO_TEX`         |
| `emission_texture`    | != null          | `HAS_EMISSION_TEX`   |

### Alpha Mode (from material `alpha_mode` property)

| Value   | Define              |
|---------|---------------------|
| OPAQUE  | `ALPHA_MODE_OPAQUE` |
| MASK    | `ALPHA_MODE_MASK`   |
| BLEND   | `ALPHA_MODE_BLEND`  |

### Mesh Type (from ECS component)

| Condition                   | Define     |
|-----------------------------|------------|
| `CSkinnedMesh` present      | `SKINNED`  |
| Instanced draw batch         | `INSTANCED`|

### Shadow Quality (from `project.uzproj`)

| Setting    | Define                |
|------------|-----------------------|
| Low        | `SHADOW_QUALITY=0`    |
| Medium     | `SHADOW_QUALITY=1`    |
| High       | `SHADOW_QUALITY=2`    |

### Cascade Count (from `project.uzproj`)

| Setting | Define           |
|---------|------------------|
| 1       | `NUM_CASCADES=1` |
| 2       | `NUM_CASCADES=2` |
| 3       | `NUM_CASCADES=3` |

---

## ShaderVariantDef in .uzmat

A material file can optionally declare variant overrides. Most variants are auto-detected, but a `.uzmat` can force or add defines:

```ini
[uzmaterial version=1]
shader = "res://shaders/builtin/pbr.hlsl"

; Auto-detected: HAS_ALBEDO_TEX, HAS_NORMAL_TEX (from textures below)
albedo_texture = "res://textures/brick_albedo.png"
normal_texture = "res://textures/brick_normal.png"
roughness      = 0.6
metallic       = 0.0
alpha_mode     = OPAQUE

; Explicit variant overrides (optional, overrides auto-detection)
[variants]
CUSTOM_WIND = 1          ; custom define for vegetation shader
DETAIL_UV_SCALE = 4      ; custom define with value
```

---

## Auto-Detection Logic

When a material is loaded or modified, the engine builds the full define list:

```
variant_build_defines(material, mesh_component, project_settings) -> Array<const char*>
```

1. **Texture presence**: For each texture slot, if the material has a non-null texture path, add `HAS_<SLOT>_TEX`.
2. **Alpha mode**: Add `ALPHA_MODE_<mode>` based on `material.alpha_mode`.
3. **Skinning**: If the draw call comes from a `CSkinnedMesh` component, add `SKINNED`.
4. **Instancing**: If the draw call is part of an instanced batch, add `INSTANCED`.
5. **Shadow quality**: Read `renderer.shadow_quality` from project settings.
6. **Cascade count**: Read `renderer.shadow_cascades` from project settings.
7. **Custom defines**: Append any defines from `[variants]` section in `.uzmat`.
8. Compute `ShaderVariantKey` from the final define list.

---

## PSO Cache Key

Pipeline State Objects are cached with a composite key:

```cpp
// src/renderer/shader_compiler/pso_cache.h

struct PsoCacheKey {
    StringId         shader_path;      // "res://shaders/builtin/pbr.hlsl"
    ShaderVariantKey variant_key;      // hash of active defines
    VertexLayoutId   vertex_layout;    // position/normal/tangent/uv/etc.
    BlendStateId     blend_state;      // OPAQUE, ALPHA_BLEND, ADDITIVE
    DepthStateId     depth_state;      // LESS_EQUAL, EQUAL, DISABLED

    uint64_t hash() const;             // combined hash for HashMap lookup
};
```

When a draw call needs a PSO:

1. Build `PsoCacheKey` from material + mesh + render pass state.
2. Look up in `HashMap<uint64_t, ID3D12PipelineState*>`.
3. If **cache hit**: use cached PSO.
4. If **cache miss**: compile shader variant on background thread, use fallback PSO (solid magenta error shader) for this frame, swap in correct PSO next frame.

---

## DXC Compilation

Defines are passed to DXC on the command line via `-D` flags:

```
dxc.exe -T vs_6_0 -E VSMain -D HAS_ALBEDO_TEX -D ALPHA_MODE_OPAQUE -D NUM_CASCADES=3 shaders/pbr.hlsl
```

### ShaderCompiler API

```cpp
// src/renderer/shader_compiler/shader_compiler.h

struct CompileResult {
    bool                ok;
    ComPtr<IDxcBlob>    bytecode;       // DXIL blob on success
    Array<Diagnostic>   diagnostics;    // errors/warnings
    Array<StringId>     includes;       // dependency tracking
};

CompileResult compile_variant(
    const char*        path,           // shader file path
    const char*        entry_point,    // "VSMain", "PSMain", "CSMain"
    const char*        target,         // "vs_6_0", "ps_6_0", "cs_6_0"
    Span<const char*>  defines,        // active defines for this variant
    Arena*             scratch         // scratch allocator for temp strings
);
```

### Compilation Flags

| Build mode  | DXC flags                    |
|-------------|------------------------------|
| Debug       | `-Od -Zi -Qembed_debug`     |
| Release     | `-O3`                        |
| Hot-reload  | `-Od` (fast compile)         |

---

## Hot-Reload

When a `.hlsl` file changes (detected by file watcher or `shader.reload` IPC):

1. Find all `PsoCacheKey` entries whose `shader_path` matches the changed file.
2. For each cached entry, recompile with the same defines.
3. On success: atomically swap the `ID3D12PipelineState*` pointer (after `WaitForGpu()`).
4. On failure: keep old PSO, report errors via IPC.

This means a single shader file change may trigger recompilation of N variants (one per unique define combination that was used). Recompilation happens on a background thread; the main thread swaps PSOs between frames.

---

## Shipping (Offline Compilation)

In the build pipeline (`UzBuild.exe` cook stage):

1. Scan all `.uzmat` files in the project.
2. For each material, compute the define list (same logic as runtime auto-detection).
3. Compile all variants for all materials.
4. Also compile project-global variants (shadow quality settings, cascade counts).
5. Store compiled DXIL blobs in `.uzpak` keyed by `{shader_path, variant_key}`.
6. At runtime in a shipping build, the PSO cache loads pre-compiled blobs from `.uzpak` instead of invoking DXC.
7. If a variant is requested that was not pre-compiled, log an error and use the fallback magenta shader.

---

## Fallback Shader

When a variant fails to compile (either at runtime or during hot-reload), the engine uses a **fallback error shader** that renders solid magenta (`#FF00FF`). This makes missing/broken shaders immediately visible without crashing.

```hlsl
// shaders/builtin/error.hlsl
float4 PSMain() : SV_TARGET {
    return float4(1.0, 0.0, 1.0, 1.0); // magenta = broken shader
}
```

---

## IPC Handlers (shader.*)

New IPC handlers for variant inspection:

```json
shader.listVariants  { "path": "res://shaders/pbr.hlsl" }
                 --> { "variants": [
                         { "key": "0xA1B2C3D4...", "defines": ["HAS_ALBEDO_TEX", "ALPHA_MODE_OPAQUE"] },
                         { "key": "0xE5F6A7B8...", "defines": ["HAS_ALBEDO_TEX", "HAS_NORMAL_TEX", "SKINNED"] }
                       ]}

shader.compileVariant { "path": "res://shaders/pbr.hlsl", "defines": ["HAS_ALBEDO_TEX"] }
                  --> { "ok": true, "elapsed_ms": 38, "variant_key": "0xA1B2C3D4..." }
```

---

## Performance Notes

- **Variant count per shader**: Typically 4-16 active variants per shader in a real scene. Worst-case theoretical maximum is large, but auto-detection ensures only needed variants are compiled.
- **Compilation latency**: DXC compiles a single variant in 20-80ms. Background thread prevents stalls.
- **Memory**: Each compiled PSO is ~1-4 KB. 100 variants = ~400 KB. Negligible.
- **Cache warm-up**: First frame of a scene may show magenta on some objects while variants compile. Subsequent frames are fully cached.
