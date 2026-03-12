# Shader Pipeline — Virtual File System

> **Status**: Done
> **Source**: `runtime/core/shader/shader_vfs.h/.cpp`
> **Depends on**: Core paths

## Purpose

The Shader VFS maps virtual paths (e.g., `/engine/pbr.hlsl`) to real filesystem paths (e.g., `shaders/engine/pbr.hlsl`). All shader loading goes through the VFS — no raw filesystem access in shader code. This enables:

- **Mount-based overrides**: Later mounts override earlier ones (mod systems, debug shaders)
- **Platform independence**: Virtual paths work the same on all platforms
- **Testability**: Mock the VFS in tests without touching the filesystem

## API

```cpp
ShaderVFS vfs;
vfs.Mount("/engine", "shaders/engine");   // Engine built-in shaders
vfs.Mount("/game", "shaders/game");       // Game project shaders
vfs.Mount("/engine", "shaders/override"); // Override engine shaders (last mount wins)

// Resolve virtual → real path
auto path = vfs.Resolve("/engine/pbr.hlsl");
// path == "shaders/override/pbr.hlsl" (override mount takes priority)

// Check existence
if (vfs.Exists("/engine/common.hlsli")) { ... }

// Read source
auto source = vfs.ReadFile("/engine/pbr.hlsl");
if (source) {
    // *source is the HLSL source text
}
```

## Mount Resolution

Mounts are searched in **reverse order** (last mount wins):

```
Mount order:
1. vfs.Mount("/engine", "shaders/engine")
2. vfs.Mount("/game", "shaders/game")
3. vfs.Mount("/engine", "shaders/override")

Resolve("/engine/pbr.hlsl"):
  Try mount 3: "shaders/override/pbr.hlsl" → file exists? YES → return
  (If not): Try mount 1: "shaders/engine/pbr.hlsl" → return

Resolve("/game/player.hlsl"):
  Try mount 2: "shaders/game/player.hlsl" → return
```

This last-wins ordering enables shader overrides without modifying the original files — useful for debugging, A/B testing, and modding.

## Rules

1. **ALWAYS** use virtual paths (starting with `/`) in shader code and `#include` directives
2. **NEVER** use raw filesystem paths in shader loading — everything goes through `ShaderVFS`
3. **ALWAYS** mount engine shaders before game shaders — game mounts should be able to override
