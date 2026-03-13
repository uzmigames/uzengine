# Resource System — Overview

> **Status**: Done (Milestone 1.2)
> **Source**: `runtime/engine/asset/` (VFS, cache, registry, importers, formats)
> **Depends on**: xxHash, nlohmann/json, filesystem

## Purpose

The resource system manages the entire lifecycle of game assets — from source files on disk to runtime-ready data in GPU memory. It consists of five cooperating subsystems:

```
Source Files (.hlsl, .png, .gltf)
      │
      ▼
┌─────────────────────────────────────────────────────────────────┐
│ 1. Asset Importer Registry                                       │
│    Maps file extensions to importers (e.g., .hlsl → ShaderImporter) │
│    Pure-function interface: source bytes → compiled bytes          │
│    Deterministic: same input → identical output                   │
├─────────────────────────────────────────────────────────────────┤
│ 2. Asset Cache                                                    │
│    Content-addressed disk storage (.cache/imported/{hash}.bin)    │
│    Key = xxHash64(importer + version + source hash + options)    │
│    Immutable entries — never updated, only created or evicted     │
├─────────────────────────────────────────────────────────────────┤
│ 3. Import Sidecars (.uzimport)                                    │
│    JSON metadata per source file — tracks hash, version, outputs  │
│    Triggers reimport on: hash change, version bump, missing output │
├─────────────────────────────────────────────────────────────────┤
│ 4. Pak Archives (.uzpak)                                          │
│    Flat binary archive for shipping — O(1) hash-based lookup      │
│    64-byte aligned data blobs (DX12 requirement)                  │
│    Header → Data → Index → Trailer                                │
├─────────────────────────────────────────────────────────────────┤
│ 5. Virtual File System (VFS)                                      │
│    Transparent access across backends:                             │
│    LooseFileVFS (dev) | PakVFS (ship) | LayeredVFS (both)        │
│    Priority-based mount stack — higher priority wins              │
└─────────────────────────────────────────────────────────────────┘
      │
      ▼
Runtime (GPU buffers, compiled shaders, textures)
```

## Design Principles

### 1. Content-Addressed Everything

Every cache key is derived from **content**, never timestamps:

```
key = xxHash64(importer_name + version + source_hash + options_hash)
```

This means:
- Renaming a file doesn't invalidate the cache (content unchanged)
- Touching a file without changing it doesn't trigger reimport
- Two identical source files share one cache entry
- The entire `.cache/` directory is safe to delete — reimport regenerates everything

### 2. Deterministic Imports

Importers are **pure functions**: same source bytes + same options = identical output bytes. No randomness, no timestamps, no environment-dependent behavior. This guarantees that the content-addressed cache produces correct results across machines and CI builds.

### 3. Layered File Access

The VFS decouples "what file to load" from "where it lives":

```
Development:  LooseFileVFS("assets/")           → reads from disk directly
Shipping:     PakVFS("game.uzpak")              → reads from archive
Modding:      LayeredVFS(mod.uzpak + base.uzpak) → mod overrides base
```

Code that loads assets uses `IVFS::Read(path)` without knowing the backend. Switching from dev to shipping mode is just a mount point change.

### 4. Explicit Invalidation

Import sidecars store the exact state that produced a cached output. Reimport triggers are explicit and auditable:

| Trigger | Mechanism |
|---------|-----------|
| Source modified | `xxHash64(source_bytes)` changed |
| Importer updated | `IAssetImporter::Version()` bumped |
| Output missing | Sidecar's output path doesn't exist on disk |
| Sidecar missing | No `.uzimport` file → always reimport |

## Data Flow: Import Pipeline

```
1. uzcook scans --source directory
2. For each source file:
   a. Read source bytes, compute xxHash64 → sourceHash
   b. Check .uzimport sidecar:
      - Missing? → reimport
      - Hash changed? → reimport
      - Version changed? → reimport
      - Output file missing? → reimport
      - All OK? → skip
   c. Find importer via registry.FindByExtension(".hlsl")
   d. Call importer.Import(sourceBytes, name, options) → compiled bytes
   e. Compute cache key, store in .cache/imported/{key}.bin
   f. Write .uzimport sidecar with hash, version, output path

3. If --pak specified:
   a. Collect all imported outputs
   b. Pack into .uzpak with WritePakFile()
   c. Generate .uzmanifest
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Import model | DerivedDataCache (DDC) + custom serialization | `.import` files + ResourceFormatLoader | IAssetImporter + .uzimport sidecars |
| Cache strategy | Derived Data Cache (DDC) — hash-based with shared network cache | `.godot/imported/` directory | `.cache/imported/` — xxHash64 keys |
| Archive format | `.pak` (UnrealPak) — compressed, encrypted, signed | `.pck` (Godot Pack) | `.uzpak` — flat binary, 64-byte aligned |
| VFS | `FPakPlatformFile` layered filesystem | `FileAccess` with `res://` / `user://` | `LayeredVFS` with priority mounts |
| Incremental cook | Content hash + dependency tracking | File modification time | **Content hash** (no timestamps) |

### UE5's Derived Data Cache (DDC)

UE5's DDC is the closest parallel. Key differences:
- **Network sharing**: UE5 DDC supports shared network/cloud backends for team builds. UzEngine is local-only (CI can share via artifact caching).
- **Derived data**: UE5 DDC caches any derived computation (LOD generation, physics cooking). UzEngine's cache is import-only for now.
- **Key format**: Both use content hashing. UE5 uses FIoHash (Blake3), UzEngine uses xxHash64.

### Godot's Import System

Godot uses file modification timestamps for incremental import, which fails when:
- Files are copied between machines (timestamps change)
- Version control checks out files (timestamps reset)
- CI builds start from clean checkout (all timestamps are "new")

UzEngine's content-hash approach avoids all these issues.

## Rules

1. **ALWAYS use content hashes, never timestamps** — timestamps are unreliable across machines and CI
2. **ALWAYS bump `IAssetImporter::Version()` when import logic changes** — ensures all cached outputs are invalidated
3. **NEVER modify cache entries** — cache is append-only. If an entry is wrong, invalidate and reimport
4. **ALWAYS use `IVFS::Read()` to load assets** — never bypass the VFS with direct filesystem calls
5. **ALWAYS 64-byte align data blobs in .uzpak** — DX12 requires this for direct GPU resource creation
