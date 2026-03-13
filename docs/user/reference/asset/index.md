# Asset Module Reference

Asset pipeline: VFS, importers, cache, registry, and archive formats.

> **Headers**: `runtime/engine/asset/` (VFS, importers, formats, cache, registry) | **Guide**: [Architecture](../../guides/architecture.md)

---

## VFS (`runtime/engine/asset/vfs.h`)

Virtual filesystem abstracting loose-file and packed-archive access.

### Backends

| Class | Mode | Description |
|-------|------|-------------|
| `LooseFileVFS` | Development | Reads files from a root directory on disk |
| `PakVFS` | Shipping | Reads from a loaded `.uzpak` archive in memory (O(1) lookup) |
| `LayeredVFS` | Both | Priority-based mount stack — higher-priority shadows lower |

### IVFS Interface

```cpp
class IVFS {
    virtual std::vector<uint8_t> ReadFile(const std::string& path) = 0;
    virtual bool Exists(const std::string& path) = 0;
};
```

### Path Convention

- Forward slashes only: `"shaders/simple_vs.uzshader"`
- No leading slash, no Windows backslashes
- Relative to VFS root

---

## AssetImporter (`runtime/engine/asset/importers/`)

Pure-function interface for converting source assets to runtime formats.

### Contract

- `Import()` MUST be deterministic (no RNG, no timestamps, no side effects)
- `Import()` MUST NOT access the cache — caching is external
- `Version()` MUST be bumped when import logic changes

### AssetImportResult

```cpp
struct AssetImportResult {
    bool success;                   // true if import succeeded
    std::vector<uint8_t> data;     // Runtime-ready bytes (empty on failure)
    std::string error;              // Human-readable error (empty on success)
};
```

### IAssetImporter Interface

| Method | Description |
|--------|-------------|
| `Name()` | Importer identifier (e.g., `"shader"`) |
| `Version()` | Monotonic version — bump on logic change |
| `SupportedExtensions()` | File extensions this importer handles |
| `Import(path, source, options)` | Convert source bytes to runtime format |

---

## AssetCache (`runtime/engine/asset/asset_cache.h`)

Content-addressed disk cache for imported assets.

### Cache Key Derivation

```
key = xxhash64(
    importer_name       +
    importer_version    +  (4 bytes LE)
    source_content_hash +  (8 bytes LE)
    options_hash           (8 bytes LE)
)
```

### Storage Layout

```
.cache/
  imported/
    <hex16>.bin    — cached asset bytes
```

### Methods

| Method | Description |
|--------|-------------|
| `Lookup(key)` | Returns cached bytes or empty vector on miss |
| `Store(key, data)` | Writes data to cache (immutable entry) |
| `Contains(key)` | Check if entry exists |
| `Clear()` | Evict all cached entries |

### Invariants

- Safe to delete entirely — reimport regenerates everything
- No timestamps — content hash only
- Entries are immutable — never updated, only created or evicted

---

## AssetImporterRegistry (`runtime/engine/asset/importers/asset_registry.h`)

Maps file extensions to importers. Register at startup, then lookup by extension.

```cpp
AssetImporterRegistry registry;
registry.Register(std::make_unique<ShaderImporter>());
auto* importer = registry.FindByExtension(".hlsl");
```

| Method | Description |
|--------|-------------|
| `Register(importer)` | Add importer (takes ownership) |
| `FindByExtension(ext)` | Lookup importer for extension (nullptr if none) |
| `GetAll()` | List all registered importers |

**Thread Safety**: Registration is NOT thread-safe (startup only). Lookup is safe after registration completes.

---

## AssetManifest (`runtime/engine/asset/asset_manifest.h`)

Build manifest tracking all imported assets and their metadata.

---

## ImportSidecar (`runtime/engine/asset/import_sidecar.h`)

`.import.json` sidecar files storing per-asset import settings alongside source files.

---

## PakFormat (`runtime/engine/asset/formats/pak_format.h`)

`.uzpak` flat binary archive for shipping precompiled assets.

### Binary Layout (little-endian)

```
[Header]     16 bytes
  magic:      "UZPK" (0x4B505A55)
  version:    uint32 (1)
  entryCount: uint32
  flags:      uint32

[Data Region] variable
  blob[N]:    64-byte aligned raw bytes

[Index]       entryCount × 40 bytes
  pathHash:     uint64 (xxhash64 of logical path)
  offset:       uint64 (byte offset into data region)
  size:         uint64 (uncompressed size)
  contentHash:  uint64 (xxhash64 of blob)
  typeId:       uint32 (asset type identifier)
  reserved:     uint32

[Trailer]     16 bytes
  indexOffset:   uint64
  indexChecksum: uint32 (reserved)
```

### Functions

| Function | Description |
|----------|-------------|
| `PakWriter::AddEntry(path, data, typeId)` | Add asset to archive |
| `PakWriter::Finalize(outputPath)` | Write `.uzpak` file to disk |
| `PakReader::Open(path)` | Open archive for reading |
| `PakReader::Read(pathHash)` | O(1) hash-table lookup |

---

## UzShaderFormat (`runtime/engine/asset/formats/uzshader_format.h`)

`.uzshader` binary format for precompiled shader bytecode.

---

## ShaderImporter (`runtime/engine/asset/importers/shader_importer.h`)

HLSL-specific importer that compiles `.hlsl` source files to `.uzshader` format via DXC.

---

## Related Pages

- [Shader Pipeline Guide](../../guides/rendering/shader-pipeline.md) — Shader compilation workflow
- [Architecture](../../guides/architecture.md) — System dependencies
- [Build System](../../guides/build-system/zig-build.md) — How assets are deployed
