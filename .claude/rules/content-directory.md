# Runtime content assets go in runtime/content/ — NEVER in source directories

## Rule

All runtime content assets (textures, meshes, noise volumes, LUTs, etc.) MUST be placed in:
```
runtime/content/<category>/
```

**NEVER** place assets in source code directories like `engine/renderer/passes/` or `engine/ecs/`.

## Directory Structure

```
runtime/content/
├── atmosphere/     # Cloud noise, weather maps, LUTs
├── textures/       # Default/fallback textures
├── meshes/         # Built-in meshes (sphere, cube, etc.)
└── fonts/          # SDF font atlases
```

## Demo Asset Deployment

Demos copy content from `runtime/content/` into their build output:
```zig
.{ .source = "../../content/atmosphere/shape.bin", .dest = "content/atmosphere/shape.bin" },
```

## Why

- Separates code from data (like UE5's Content/ directory)
- Cleaner source directories
- Assets can be shared across multiple demos
- Easier to manage, version, and replace assets independently of code

## When Loading Assets at Runtime

Use paths relative to the executable:
```cpp
const char* path = "content/atmosphere/shape.bin";
```
