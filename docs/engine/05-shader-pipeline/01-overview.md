# Shader Pipeline — Overview

> **Status**: Done (Milestone 1.5)
> **Source**: `runtime/core/shader/shader_pipeline.h` (orchestrator), `shader_compiler.h`, `shader_vfs.h`, `shader_cache.h`, `shader_dependency_graph.h`, `shader_hot_reload.h`, `shader_reflection.h`
> **Depends on**: DXC (dxcompiler.dll), xxHash, SPIRV-Reflect, core paths

## Purpose

Shaders are authored in HLSL and compiled to backend-specific bytecode. The shader pipeline handles everything between source code and GPU-ready bytecode: virtual filesystem resolution, compilation via DXC, content-addressed caching, dependency tracking, hot-reload, and reflection for automatic binding layout generation.

## Two Modes

The pipeline operates in two mutually exclusive modes, selected at compile time:

| Mode | Define | When | How |
|------|--------|------|-----|
| **Dev** | `UZ_SHADER_DEV` | Development builds | Load `.hlsl` from disk via VFS, compile on-demand, content-hash cache, FileWatcher hot-reload |
| **Ship** | `UZ_SHADER_SHIP` | Shipping builds | All bytecode precompiled into `shaders.uzpak` archive, indexed by hash. No DXC, no VFS, no FileWatcher |

This dual-mode design was born from POC 0.1's lesson: **never embed shaders as C header strings**. That approach caused stale cache bugs during rapid iteration. Dev mode loads from disk for instant iteration; shipping mode uses a sealed archive for zero overhead.

## Pipeline Architecture

```
HLSL Source Files (.hlsl, .hlsli)
       │
       ▼
┌──────────────────────────────┐
│  ShaderVFS                   │  Virtual path → real filesystem path
│  /engine → shaders/engine    │  Last mount wins (override support)
│  /game → shaders/game        │
└──────────────┬───────────────┘
               │
               ▼
┌──────────────────────────────┐
│  ShaderSourceManager         │  Reads source, parses #include,
│                              │  computes identity hash (xxHash64)
│  Hash = xxH64(source         │
│    + all includes            │
│    + defines                 │
│    + target SM/backend       │
│    + DXC version)            │
└──────────────┬───────────────┘
               │
               ├──→ ShaderCache.Lookup(hash)
               │      │
               │      ├── Cache HIT → return bytecode (no compile)
               │      │
               │      └── Cache MISS ──▼
               │
               ▼
┌──────────────────────────────┐
│  ShaderCompiler (DXC)        │  HLSL → DXIL (DX12)
│  Dynamic load dxcompiler.dll │  HLSL → SPIRV (Vulkan)
│                              │  Supports SM5.1 through SM6.7
└──────────────┬───────────────┘
               │
               ├──→ ShaderCache.Store(hash, bytecode)
               │
               ├──→ ShaderReflection (SPIRV-Reflect)
               │      Extract bindings → auto root signature
               │
               ▼
┌──────────────────────────────┐
│  ShaderDependencyGraph       │  Forward: file → includes
│  + ShaderHotReload           │  Reverse: file → dependents
│  + FileWatcher               │  Dirty propagation on change
└──────────────────────────────┘
```

## Key Design Decisions

### 1. Content-Addressed Cache (xxHash64)

```
hash = xxHash64(source + all_includes + defines + target_backend + compiler_version)
```

No timestamps. Changing any included file automatically produces a different hash. Stale cache entries are simply never referenced — no explicit invalidation needed.

**Why xxHash64?** 64-bit hash, ~30 GB/s throughput, zero collisions in practice for our use case. Used by the Linux kernel, Clang, and many game engines.

### 2. Reverse-Dependency Graph

When `common.hlsli` changes, the dependency graph instantly finds every shader that includes it (transitively). Only those shaders are marked dirty — no full-rebuild needed.

```
common.hlsli changed
    ↓ reverse graph BFS
pbr.hlsl       ← includes common.hlsli
shadow.hlsl    ← includes common.hlsli
post_tonemap.hlsl ← includes common.hlsli via color.hlsli
```

### 3. Lazy Recompilation

Shaders are NOT recompiled immediately on file change. Instead:
1. FileWatcher detects change → marks shader dirty
2. On next use (PSO bind), cache checks dirty flag
3. If dirty → recompile, store, clear dirty
4. If not dirty → use cached bytecode

This avoids compilation storms when saving frequently or doing batch edits.

### 4. HLSL as Single Source Language

All shaders are authored in HLSL. DXC produces all needed output formats:

```
HLSL → DXC (SM6.6) → DXIL     → DX12 backend
HLSL → DXC (-spirv) → SPIR-V  → Vulkan backend
HLSL → DXC (-spirv) → SPIR-V → SPIRV-Cross → MSL → Metal backend
```

No GLSL, no shader DSL, no macro-heavy abstraction layer. One language, one set of shaders.

### 5. Reflection-Driven Binding

SPIRV-Reflect extracts resource bindings from compiled SPIR-V. This data auto-generates:
- Root signature (DX12): which resources go in which root parameter slots
- Descriptor set layouts (Vulkan): which bindings in which sets
- Pipeline layout validation: ensure shader and pipeline agree on resource usage

No manual binding tables. Change the shader, re-reflect, auto-update the layout.

## Dependencies

| Library | Purpose | Integration |
|---------|---------|-------------|
| **DXC** | HLSL → DXIL/SPIRV compilation | Dynamic load (`dxcompiler.dll` at runtime) |
| **SPIRV-Reflect** | Binding extraction from SPIR-V | Source compiled (`thirdparty/spirv-reflect/`) |
| **xxHash** | Content-addressed cache keys | Header-only (`thirdparty/xxhash/`) |
| **FXC** | HLSL → DXBC (SM5 legacy, planned) | Dynamic load (`d3dcompiler_47.dll`) |
| **SPIRV-Cross** | SPIRV → MSL/GLSL transpilation (planned) | TBD |

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Source language | HLSL (custom USF macros) | GLSL (custom preprocessor) | HLSL (standard DXC) |
| Compilation | Custom ShaderCompileWorker (distributed) | Runtime GLSL compile | DXC (single-threaded per instance, parallelizable) |
| Cache | DDC (Derived Data Cache) + PSO cache | No shader cache | Content-hash disk cache (xxHash64) |
| Hot-reload | Yes (editor-only, UShaderCompilingManager) | Yes (GDScript only, not shaders) | Yes (FileWatcher + dependency graph) |
| Permutations | Massive (~millions for complex materials) | Moderate | Capped at 256 per shader |
| Reflection | Custom DXC wrapper + FXC | Built into GLSL compiler | SPIRV-Reflect (auto binding layout) |

## Rules

1. **NEVER** embed shaders as C strings or header includes — POC 0.1 proved this causes stale cache
2. **ALWAYS** use the ShaderVFS for path resolution — no raw filesystem access in shader code
3. **ALWAYS** include DXC version in cache hash — compiler updates change output
4. **NEVER** exceed 256 permutations per shader — prevents combinatorial explosion
5. **ALWAYS** re-reflect bindings after recompilation — stale layouts cause PSO mismatches
