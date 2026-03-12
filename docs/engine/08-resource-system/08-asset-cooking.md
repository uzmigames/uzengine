# Asset Cooking — Overview

> **Status**: Planned (Phase 1.5 basic, Phase 2 incremental + parallel, Phase 3 patching)
> **Source**: `runtime/tools/uzcook/` (orchestrator), `core/asset/cook_pipeline.h`, `asset_dependency_graph.h`, `pak_archive.h`, `cook_manifest.h`
> **Depends on**: VFS, D3D12MA, zstd, xxHash, nlohmann/json

## Purpose

Asset cooking is the offline preprocessing pipeline that transforms authoring-friendly source assets (raw textures, uncompressed meshes, uncompiled shaders) into GPU-ready, platform-optimized, memory-efficient formats suitable for shipping builds. The **uzcook** command-line tool orchestrates the entire pipeline: discovery, import, processing, compression, packaging, and validation.

Asset cooking bridges development (immediate iteration via VFS) and shipping (sealed, optimized archives):

```
Development:  Source files → VFS read on-demand → instant iteration
              ↓ (for testing/preview)
Shipping:     Source files → uzcook → .uzpak archive → zero overhead at runtime
```

## Cook Pipeline: Five Stages

```
1. DISCOVER
   Scan --source directory for all cookable assets
   Identify asset types (mesh, texture, shader, scene, etc.)
   Build dependency graph (material → textures, mesh → materials, scene → all)
         │
         ▼
2. IMPORT
   For each source asset:
   - Hash source + options → cache key (xxHash64)
   - Check if already imported (content-hash cache)
   - If missing/changed: run importer (DXC for shaders, mesh compiler for glTF, etc.)
   - Store imported form in .cache/imported/{hash}.bin
         │
         ▼
3. PROCESS
   Per-platform processing:
   - Textures: compress BC7/BC5/BC4 (desktop), ASTC (mobile), resize mipmaps
   - Meshes: vertex/index optimization, LOD generation, tangent-space baking
   - Shaders: compile all permutations, serialize PSO cache
   - Scenes: reference resolution, serialize component hierarchy
         │
         ▼
4. PACKAGE
   - Collect all processed assets
   - Write .uzpak archive: Header → Data (64-byte aligned) → Index → Trailer
   - Optionally compress with zstd (container-level, not per-asset)
   - Generate .uzmanifest (JSON: asset → hash, for patching)
         │
         ▼
5. VALIDATE
   - Verify all scene/material references resolve
   - Check for missing assets, broken dependencies
   - Detect cycles in dependency graph
   - Report any asset not referenced by scenes/manifest
```

## Incremental Cooking

Content-hash invalidation enables incremental workflows:

```
uzcook --source assets/ --out game.uzpak --incremental

Previous manifest: {texture_01.png → hash_A, mesh_02.glb → hash_B, ...}
New manifest:     {texture_01.png → hash_A, mesh_02.glb → hash_C, ...}

Changes: mesh_02.glb (hash changed) → reprocess mesh_02 + all dependents
Result: Only 2 assets recooked, N-2 skipped (saves 95% of time)
```

**Dependency tracking**: The cook pipeline maintains a reverse-dependency graph. When an asset changes, only that asset and its transitive dependents are marked dirty. For example:

```
material.uzmat → {texture_diffuse.png, texture_normal.png}
mesh.uzmesh → {material.uzmat}
character.uzscene → {mesh.uzmesh, material.uzmat}

If texture_diffuse.png changes:
  1. material.uzmat marked dirty (depends on texture_diffuse.png)
  2. mesh.uzmesh marked dirty (depends on material.uzmat)
  3. character.uzscene marked dirty (depends on mesh.uzmesh)
```

## Platform Targets & Format Selection

The cook pipeline supports per-platform target selection:

| Platform | Texture Format | Shader Target | Mesh Format |
|----------|---|---|---|
| **Windows (DX12)** | BC7 (color), BC5 (normal), BC4 (grayscale) | SM6.6 bindless | glTF + Jolt physics |
| **Linux (Vulkan)** | ASTC (4x4), BC7 fallback | SPIRV (SM5.1 equivalent) | glTF + Jolt physics |
| **Console (future)** | Platform-native (GNM/GNMX for PS5, GCN for Xbox) | Custom permutation cache | Optimized mesh format |

Example:

```bash
uzcook --source assets/ \
       --out windows/ --target windows \
       --out linux/ --target linux
```

This generates two separate .uzpak files, one per platform.

## .uzpak Archive Format

The .uzpak archive is a flat binary format designed for O(1) lookup and memory-mapped playback:

```
┌─ Header (256 bytes)
│  - Magic: "UZPK" (4 bytes)
│  - Version: 1 (4 bytes)
│  - DataOffset: position of data section (8 bytes)
│  - IndexOffset: position of index section (8 bytes)
│  - AssetCount: number of assets in archive (4 bytes)
│  - CompressionType: none | zstd | (4 bytes)
├─ Data Section (variable, 64-byte aligned)
│  - Asset 1 data (exactly DataSize[0] bytes)
│  - Asset 2 data (exactly DataSize[1] bytes)
│  - ... (all data blobs concatenated)
├─ Index Section (AssetCount * 32 bytes)
│  Per asset:
│  - PathHash: xxHash64(asset path) — O(1) lookup
│  - DataOffset: byte offset in Data section
│  - DataSize: size of this asset's data
│  - ContentHash: xxHash64 of uncompressed data (for validation)
└─ Trailer (16 bytes)
   - IndexSize: size of index section (8 bytes)
   - Reserved: 8 bytes
```

**64-byte alignment**: DX12 requires GPU resource buffers to be 64-byte aligned. All data blobs are padded to alignment boundaries, allowing GPU drivers to create Direct3D resources directly from mapped archive memory.

## Cook Manifest & Patching

The .uzmanifest is a JSON file generated after each cook, tracking all assets:

```json
{
  "version": 1,
  "timestamp": "2026-03-12T10:30:45Z",
  "platform": "windows",
  "assets": {
    "textures/pbr_diffuse.png": {
      "dataSize": 8388608,
      "contentHash": "0x1a2b3c4d5e6f7a8b",
      "sourceHash": "0x0102030405060708",
      "compressionRatio": 0.65
    },
    "meshes/character.glb": {
      "dataSize": 2097152,
      "contentHash": "0x9a8b7c6d5e4f3a2b",
      "sourceHash": "0x1112131415161718",
      "compressionRatio": 0.42
    }
  }
}
```

**Patching workflow**:

```bash
# Ship game with base.uzpak + base.uzmanifest
# Later, update texture_diffuse.png

uzcook --source assets/ --out patch.uzpak \
       --previous-manifest base.uzmanifest \
       --delta-only

# Result: patch.uzpak contains ONLY texture_diffuse.png (not entire archive)
# Client downloads 5 MB patch instead of 2 GB full pak

# On load, runtime mounts: patch.uzpak (priority) → base.uzpak
# Lookup prioritizes patch assets, falls back to base
```

## Parallel Cooking

Cooking large projects is parallelized via the job system:

```cpp
// Cook N assets concurrently (worker thread count = hardware_concurrency)
cook_pipeline.CookAssetsParallel(
    assets,
    num_workers: 8,
    progress_callback: [](int done, int total) {
        printf("Cooking: %d/%d\n", done, total);
    }
);
```

Dependencies are honored: if mesh depends on material, material is cooked before mesh (topological sort).

## Cook Report & Validation

After cooking completes, uzcook generates a detailed report:

```
=== UzEngine Asset Cook Report ===
Platform: windows
Start time: 2026-03-12 10:30:45
End time: 2026-03-12 10:35:22
Duration: 4m 37s

Assets by type:
  Textures: 156 (2.3 GB → 1.5 GB, 65% ratio)
  Meshes: 48 (1.8 GB → 0.9 GB, 50% ratio)
  Shaders: 24 permutations (156 MB)
  Scenes: 8 (2.3 MB)
  Total: 256 assets → 2.5 GB pak (71% of source)

Validation:
  ✓ All asset references resolve
  ✓ No cycles in dependency graph
  ✓ No unreferenced assets
  ✓ No missing dependencies

Timing by type:
  Import:  1m 10s (28%)
  Process: 2m 45s (59%)
  Package: 0m 30s (11%)
```

If validation fails, uzcook reports specific errors and exits with non-zero status.

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Cook tool | UnrealAutomationTool (UAT) | Built-in export | uzcook CLI |
| Cache strategy | Derived Data Cache (DDC) + Cook-On-The-Fly | PCK serializer | Content-hash (xxHash64) + incremental |
| Archive format | PAK (encrypted, signed, custom compression) | PCK (flat binary) | UZPAK (64-byte aligned, zstd optional) |
| Parallelization | CookWorker pool + load balancing | Single-threaded | Job system (topological sort) |
| Patching | Binary patching (pak patch format) | Full re-export | Delta .uzpak (manifest diff) |
| Incremental | Content hash + dependency tracking | File timestamps | **Content hash** (reliable across CI) |

## Rules

1. **NEVER reuse cache entries for different platforms** — platform-specific processing must invalidate cache key
2. **ALWAYS include all transitive dependencies in pak** — if scene references mesh, pak includes mesh + all mesh materials + all material textures
3. **NEVER cook without validation** — broken references should fail the entire cook, not silently skip assets
4. **ALWAYS use content hashes, never timestamps** — timestamps fail on CI and multi-developer teams
5. **ALWAYS 64-byte align data blobs** — DX12 requires this for GPU resource creation
6. **ALWAYS report detailed cook timing** — identifies bottlenecks (e.g., "shader compilation takes 80% of time")
