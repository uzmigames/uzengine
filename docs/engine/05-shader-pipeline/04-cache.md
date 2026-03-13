# Shader Pipeline — Content-Hash Cache

> **Status**: Done
> **Source**: `runtime/engine/shader/shader_cache.h/.cpp`, `shader_source_manager.h/.cpp`
> **Depends on**: xxHash (`thirdparty/xxhash/`)

## Purpose

Shader compilation is expensive (100ms–2s per shader). The cache stores compiled bytecode on disk, keyed by an xxHash64 identity hash that captures everything about the compilation: source code, all includes, defines, target backend, and compiler version. If any of these change, the hash changes and the shader is recompiled. If nothing changed, the cached bytecode is loaded instantly.

## Identity Hash

```
identityHash = xxHash64(
    source text            // The HLSL source code
    + all included files   // Transitive #include content
    + defines              // Preprocessor definitions ("USE_PBR=1")
    + target backend       // "dx12_sm66", "vulkan_spirv16"
    + DXC version          // "1.7.2308"
)
```

**Why not timestamps?** Timestamps are fragile — they change on `git checkout`, copy, touch, and CI builds. Content hashing is deterministic: same input always produces the same hash, regardless of when or where it was compiled.

## Cache Layout

```
cache/shaders/
├── dx12_sm66/
│   ├── a1b2c3d4e5f6.bin     ← compiled DXIL bytecode
│   ├── f7e8d9c0b1a2.bin
│   └── ...
├── dx12_sm60/
│   └── ...
├── vulkan_spirv16/
│   └── ...
└── manifest.json              ← debug-only: hash → source path mapping
```

Each backend+shader-model combination gets its own subdirectory. The filename is the hex representation of the identity hash. `manifest.json` maps hashes to source paths for debugging ("which shader is this cache entry?").

## API

```cpp
ShaderCache cache("cache/shaders");
std::string subdir = ShaderCache::GetSubdirectory("dx12", "sm66");
// subdir = "dx12_sm66"

// Lookup
auto cached = cache.Lookup(subdir, identityHash);
if (cached) {
    // *cached = std::vector<uint8_t> of compiled bytecode
    // Skip compilation entirely
} else {
    // Compile via ShaderCompiler
    auto result = compiler.Compile(desc);
    cache.Store(subdir, identityHash, result.bytecode, "/engine/pbr.hlsl");
}

// Diagnostics
size_t entries = cache.GetEntryCount();    // Total cached shaders
size_t bytes = cache.GetTotalSizeBytes();  // Total disk usage
cache.Clear();                              // Wipe everything
```

## Cache Flow

```
ShaderSourceManager computes identityHash
        │
        ▼
ShaderCache::Lookup(subdir, hash)
        │
        ├── File exists → Read bytecode → DONE (cache hit)
        │
        └── File missing → Compile (cache miss)
                │
                ▼
        ShaderCompiler::Compile(desc)
                │
                ▼
        ShaderCache::Store(subdir, hash, bytecode, sourcePath)
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Cache type | DDC (Derived Data Cache) — key/value store | None (recompiles on load) | Content-hash disk cache (xxHash64) |
| Key | Custom hash of material graph + defines | N/A | `xxHash64(source + includes + defines + target + version)` |
| Invalidation | Explicit key versioning | N/A | Implicit — hash changes when input changes |
| Distributed | Yes (shared DDC servers for teams) | No | No (local only, planned network cache) |
| Size management | LRU eviction, configurable limits | N/A | No eviction (manual `Clear()`) |

## Rules

1. **ALWAYS** include DXC version in the identity hash — compiler updates change output
2. **NEVER** use timestamps for cache invalidation — content hash is deterministic
3. **ALWAYS** separate cache directories per backend+SM — prevents bytecode mismatches
4. **NEVER** delete the cache directory during development unless something is broken — rebuilding is expensive
