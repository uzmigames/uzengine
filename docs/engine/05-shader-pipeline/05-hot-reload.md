# Shader Pipeline — Hot-Reload

> **Status**: Done
> **Source**: `runtime/engine/shader/shader_hot_reload.h/.cpp`
> **Depends on**: ShaderDependencyGraph, FileWatcher, ShaderCache

## Purpose

Edit a shader file, save, see the result instantly in the running engine — no restart, no manual rebuild. The hot-reload system watches shader directories for file changes, propagates dirty flags through the dependency graph, and triggers lazy recompilation on next use.

## Architecture

```
FileWatcher (timestamp polling, per-frame)
    │
    │ changed files: ["shaders/engine/common.hlsli"]
    ▼
ShaderHotReload::Poll()
    │
    ├── MarkDirty("common.hlsli")
    │       │
    │       ▼ dependency graph reverse BFS
    │   MarkDirty("pbr.hlsl")       ← includes common.hlsli
    │   MarkDirty("shadow.hlsl")    ← includes common.hlsli
    │   MarkDirty("tonemap.hlsl")   ← includes common.hlsli via color.hlsli
    │
    ▼
Dirty set: {common.hlsli, pbr.hlsl, shadow.hlsl, tonemap.hlsl}
    │
    │  (later, when shader is next used)
    ▼
IsDirty("pbr.hlsl") == true → recompile → cache → ClearDirty("pbr.hlsl")
```

### Why Lazy Recompilation?

Compiling all dirty shaders immediately on file change would cause frame hitches during rapid iteration (save frequently, many dependents). Lazy recompilation spreads the cost: each shader recompiles only when it's actually needed for rendering, amortized across frames.

## FileWatcher

Polls watched directories for file modifications using filesystem timestamps:

```cpp
FileWatcher watcher;
watcher.Watch("shaders/engine");   // Recursive scan
watcher.Watch("shaders/game");

// Per-frame (called from ShaderHotReload::Poll)
auto changed = watcher.PollChanges();
// changed = ["shaders/engine/common.hlsli"] (normalized paths)
```

**Tracked extensions**: `.hlsl`, `.hlsli`, `.h`, `.fx`, `.fxh`

**Path normalization**: Forward slashes, lowercase on Windows — ensures consistent keys across the dependency graph and dirty set.

**Future**: Replace timestamp polling with `ReadDirectoryChangesW` (Windows) or `inotify` (Linux) for instant notification instead of polling latency.

## Dependency Graph

Bidirectional graph tracking `#include` relationships:

```cpp
ShaderDependencyGraph graph;

// Built during compilation (ShaderSourceManager parses #include directives)
graph.SetDependencies("/engine/pbr.hlsl",
    {"/engine/common.hlsli", "/engine/brdf.hlsli"});
graph.SetDependencies("/engine/shadow.hlsl",
    {"/engine/common.hlsli"});

// Reverse query: "who depends on common.hlsli?"
auto dependents = graph.GetDependents("/engine/common.hlsli");
// dependents = {"/engine/pbr.hlsl", "/engine/shadow.hlsl"}
```

The graph stores both forward edges (file → includes) and reverse edges (file → includers). Reverse queries use BFS to find all transitive dependents — if `A` includes `B` which includes `C`, changing `C` marks both `B` and `A` dirty.

## Hot-Reload API

```cpp
ShaderHotReload hotReload;
hotReload.SetDependencyGraph(&depGraph);
hotReload.Start({
    std::filesystem::path("shaders/engine"),
    std::filesystem::path("shaders/game"),
});

// Per-frame:
auto dirty = hotReload.Poll();
if (!dirty.empty()) {
    UZ_LOG(Shader, Info, "Shader files changed: {}", dirty.size());
}

// Before using a shader:
if (hotReload.IsDirty("/engine/pbr.hlsl")) {
    auto result = compiler.Compile(pbrDesc);
    cache.Store(subdir, hash, result.bytecode, "/engine/pbr.hlsl");
    hotReload.ClearDirty("/engine/pbr.hlsl");
}
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Hot-reload scope | Material graph + shaders (editor only) | GDScript only, not shaders | HLSL source files (dev mode) |
| Change detection | DirectoryWatcher (OS notifications) | File polling | Timestamp polling (OS notifications planned) |
| Dependency tracking | Shader permutation graph in FMaterialShaderMap | None | Bidirectional include graph with BFS propagation |
| Recompile timing | Background threads (async compilation) | Immediate | **Lazy** (recompile on next use) |
| Scope of impact | All materials using the changed shader | N/A | Transitive dependents via reverse graph |

## Rules

1. **ALWAYS** call `Poll()` once per frame — missed polls delay change detection
2. **ALWAYS** set the dependency graph before starting — without it, only directly changed files are marked dirty
3. **ALWAYS** call `ClearDirty()` after successful recompilation — otherwise the shader recompiles every frame
4. **NEVER** recompile all dirty shaders in one frame — lazy recompilation avoids frame hitches
