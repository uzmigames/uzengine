# Resource System — Virtual File System (VFS)

> **Status**: Done (Milestone 1.2)
> **Source**: `core/asset/vfs.h/.cpp`
> **Depends on**: PakReader, filesystem

## Purpose

The VFS provides a uniform `Read(path)` interface that hides whether assets live on disk (development) or in a packed archive (shipping). Game code never knows or cares about the storage backend — it just reads logical asset paths.

## Architecture

```
Game Code
    │
    └── vfs->Read("shaders/pbr.uzshader")
              │
              ▼
        ┌───────────┐
        │ LayeredVFS │ ← queries backends by priority (high → low)
        │ ┌─────────┤
        │ │ 100: LooseFileVFS("mods/")       │ ← mod override
        │ │  50: LooseFileVFS("assets/")      │ ← dev loose files
        │ │   0: PakVFS("base.uzpak")         │ ← base game pak
        │ └─────────┘
        └───────────┘
```

## IVFS Interface

```cpp
class IVFS {
public:
    /// Reads a file by logical path. Returns empty on failure.
    virtual std::vector<uint8_t> Read(const std::string& path) = 0;

    /// Checks if a file exists (without loading it).
    virtual bool Exists(const std::string& path) = 0;

    /// Human-readable name for logging.
    virtual std::string Name() const = 0;
};
```

### Path Convention

- Forward slashes only: `"shaders/pbr_vs.uzshader"` (not `"shaders\\pbr_vs.uzshader"`)
- No leading slash: `"textures/brick.uztexture"` (not `"/textures/brick.uztexture"`)
- Relative to VFS root (not system path)
- Case-sensitive (matches the filesystem or pak hash)

## Backends

### LooseFileVFS (Development)

Reads files directly from a directory on disk. Path resolution is simple concatenation:

```cpp
LooseFileVFS vfs("E:/MyGame/assets/");
auto data = vfs.Read("shaders/pbr_vs.uzshader");
// Resolves to: E:/MyGame/assets/shaders/pbr_vs.uzshader
```

- Instant iteration — edit a file, reload, see the change
- No preprocessing needed (raw source files or pre-imported outputs)
- Logs `Asset::Error` on read failure with the full resolved path

### PakVFS (Shipping)

Reads assets from a loaded `.uzpak` archive:

```cpp
auto vfs = PakVFS::FromFile("build/game.uzpak");
auto data = vfs->Read("shaders/pbr_vs.uzshader");
// Hashes path, queries in-memory index, returns bytes
```

- O(1) lookup by path hash
- No filesystem overhead per read (data is in memory)
- Entire archive stays in memory (suitable for game-sized assets)

### LayeredVFS (Combined)

Stacks multiple backends with priority ordering:

```cpp
LayeredVFS layered;
layered.Mount(std::make_shared<LooseFileVFS>("mods/"),   100);  // Highest priority
layered.Mount(std::make_shared<LooseFileVFS>("assets/"),  50);  // Dev files
layered.Mount(PakVFS::FromFile("base.uzpak"),              0);  // Base game

// "shaders/pbr.uzshader" is searched in:
// 1. mods/ (priority 100) — if mod overrides the shader
// 2. assets/ (priority 50) — dev loose files
// 3. base.uzpak (priority 0) — shipped version
auto data = layered.Read("shaders/pbr.uzshader");
```

Priority rules:
- Higher numeric priority = queried first
- Equal priority = insertion order (first mounted wins)
- Re-sorted after every `Mount()` call

## Use Cases

### Development Mode

```cpp
LayeredVFS vfs;
vfs.Mount(std::make_shared<LooseFileVFS>("assets/"), 0);
// All reads go straight to disk — instant iteration
```

### Shipping Mode

```cpp
LayeredVFS vfs;
auto base = PakVFS::FromFile("game.uzpak");
vfs.Mount(base, 0);
// All reads come from the packed archive
```

### DLC / Patches

```cpp
LayeredVFS vfs;
vfs.Mount(PakVFS::FromFile("game.uzpak"), 0);        // Base
vfs.Mount(PakVFS::FromFile("dlc_winter.uzpak"), 10);  // DLC overrides base
vfs.Mount(PakVFS::FromFile("patch_1.2.uzpak"), 20);   // Patch overrides DLC + base
```

### Modding

```cpp
LayeredVFS vfs;
vfs.Mount(PakVFS::FromFile("game.uzpak"), 0);
vfs.Mount(std::make_shared<LooseFileVFS>("mods/cool_mod/"), 100);
// Mod's loose files override any packed asset
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| VFS interface | `IPlatformFile` (layered) | `FileAccess` + `res://` scheme | `IVFS` interface |
| Layering | `FPakPlatformFile` wraps `FPhysicalPlatformFile` | Single `res://` root | `LayeredVFS` with numeric priority |
| Mount/unmount | `FPakPlatformFile::Mount/Unmount` | Not dynamic — set at project load | `Mount()` / `UnmountAll()` |
| Mod support | Built-in (pak mount order) | PCK file loading | LayeredVFS with high-priority loose files |
| Async I/O | `IAsyncReadFileHandle` | `FileAccess::get_open_error()` callback | **Planned** (async VFS adapter) |

## Planned Enhancements

1. **Async Read** — non-blocking `ReadAsync()` returning a future, for background streaming
2. **Watch integration** — LooseFileVFS + FileWatcher for automatic hot-reload
3. **Memory-mapped I/O** — for large pak archives that shouldn't be fully loaded into RAM
4. **Path canonicalization** — normalize `../`, duplicate slashes, case folding

## Rules

1. **ALWAYS use `IVFS::Read()` to load assets** — never bypass with direct `std::ifstream` or `fopen`
2. **ALWAYS use forward slashes** in logical paths — no backslashes, no platform-native separators
3. **NEVER assume mount order is stable** — always use explicit priority values
4. **ALWAYS log the backend name on failures** — "LooseFileVFS(assets/): file not found: shaders/missing.hlsl"
5. **ALWAYS mount base pak at priority 0** — reserves higher priorities for patches, DLC, and mods
