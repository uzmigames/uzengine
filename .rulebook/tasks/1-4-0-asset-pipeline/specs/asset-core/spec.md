# Asset Pipeline — Technical Specification

## 1. IAssetImporter Interface

```cpp
/// Pure function that converts source assets to runtime-ready format.
class IAssetImporter {
public:
    virtual ~IAssetImporter() = default;

    /// Unique name (e.g., "shader", "texture", "mesh").
    virtual const char* Name() const = 0;

    /// Version GUID — bump when import logic changes to invalidate cache.
    virtual uint32_t Version() const = 0;

    /// File extensions this importer handles (e.g., {".hlsl", ".glsl"}).
    virtual std::span<const char* const> Extensions() const = 0;

    /// Import source data into runtime format.
    /// MUST be deterministic: same inputs → same outputs.
    virtual AssetImportResult Import(const AssetImportRequest& request) = 0;
};
```

### Scenario: Shader Import
- **GIVEN** a valid HLSL file with entry point "main" and target SM6.6
- **WHEN** `ShaderImporter::Import()` is called
- **THEN** output contains DXIL bytecode + SPIRV-Reflect metadata
- **AND** output is identical for identical inputs (deterministic)

### Scenario: Import Failure
- **GIVEN** an HLSL file with syntax errors
- **WHEN** `ShaderImporter::Import()` is called
- **THEN** result contains `success = false` and human-readable error string
- **AND** no cache entry is created

## 2. Content-Addressed Cache

### Cache Key Derivation
```
key = xxhash64(
    importer_name       +  // "shader"
    importer_version    +  // 1 (as 4 bytes LE)
    source_content_hash +  // xxhash64 of source file bytes
    options_hash           // xxhash64 of sorted options key=value pairs
)
```

### Storage Layout
```
.cache/
├── imported/
│   ├── a1b2c3d4e5f67890.uzshader    # cached DXIL bytecode
│   ├── b2c3d4e5f6789012.uztexture   # cached BC7 texture
│   └── ...
└── registry.json                     # optional: maps logical paths to cache entries
```

### Invariants
- MUST: Cache is safe to delete entirely — reimport regenerates everything
- MUST: No timestamps — content hash only
- MUST: Cache entries are immutable — never updated, only created or evicted
- SHALL: `.cache/` is gitignored

## 3. Import Sidecar (.uzimport)

### Schema
```json
{
    "importer": "shader",
    "importer_version": 1,
    "source_hash": "a1b2c3d4e5f67890",
    "options": {
        "entry_point": "main",
        "stage": "vertex",
        "model": "sm6_6"
    },
    "outputs": {
        "sm6_6": ".cache/imported/a1b2c3d4e5f67890.uzshader"
    },
    "dependencies": []
}
```

### Reimport Trigger
- IF `xxhash64(source_file_bytes) != sidecar.source_hash` → reimport
- IF current importer version != `sidecar.importer_version` → reimport
- IF sidecar file missing → reimport
- IF cached output file missing → reimport

## 4. .uzpak Archive Format

### Binary Layout
```
[Header]          16 bytes
  magic:          4 bytes   "UZPK"
  version:        4 bytes   uint32 (1)
  entry_count:    4 bytes   uint32
  flags:          4 bytes   uint32 (bit 0 = compressed)

[Data Region]     variable
  blob[0]:        aligned to 64 bytes, raw bytes
  blob[1]:        aligned to 64 bytes, raw bytes
  ...

[Index]           entry_count * 32 bytes
  per entry:
    path_hash:    8 bytes   uint64 (xxhash64 of logical path)
    offset:       8 bytes   uint64 (byte offset into data region)
    size:         4 bytes   uint32 (uncompressed size)
    packed_size:  4 bytes   uint32 (compressed size, 0 = uncompressed)
    content_hash: 8 bytes   uint64 (xxhash64 of blob for integrity)

[Trailer]         16 bytes
  index_offset:   8 bytes   uint64 (byte offset of Index start)
  index_checksum: 4 bytes   uint32 (CRC32 of Index region)
  magic:          4 bytes   "UZPK" (sentinel for trailer discovery)
```

### Invariants
- MUST: Index sorted by path_hash for binary search
- MUST: Data blobs aligned to 64 bytes (DX12 resource alignment)
- MUST: Trailer at end-of-file (append-friendly design)
- SHALL: O(1) lookup via hash + O(log n) fallback via binary search

## 5. Virtual File System

### Interface
```cpp
/// Transparent file access — loose files in dev, pak in ship.
class IVFS {
public:
    virtual ~IVFS() = default;

    /// Reads entire file into memory. Returns empty on failure.
    virtual std::vector<uint8_t> Read(const char* path) = 0;

    /// Checks if a file exists without reading it.
    virtual bool Exists(const char* path) = 0;

    /// Mounts a .uzpak archive with the given priority (higher overrides lower).
    virtual void Mount(const char* pakPath, int priority) = 0;
};
```

### Resolution Order
1. Highest-priority mounted pak that contains the path hash
2. Next-priority pak
3. Loose file on disk (dev mode only)
4. Failure

## 6. .uzshader Binary Format

```
[Header]          32 bytes
  magic:          4 bytes   "UZSH"
  version:        4 bytes   uint32 (1)
  stage:          4 bytes   uint32 (0=VS, 1=PS, 2=CS, 3=GS, 4=HS, 5=DS)
  model:          4 bytes   uint32 (0x0606 = SM6.6)
  bytecode_size:  4 bytes   uint32
  reflect_size:   4 bytes   uint32 (SPIRV-Reflect metadata, 0 if none)
  entry_hash:     4 bytes   uint32 (xxhash32 of entry point name)
  reserved:       4 bytes

[Bytecode]        bytecode_size bytes (DXIL or SPIR-V)

[Reflection]      reflect_size bytes (serialized binding info, optional)
```

### Invariants
- MUST: Bytecode is platform-native (DXIL for DX12, SPIR-V for Vulkan)
- MUST: Loading a .uzshader requires NO runtime shader compilation
- SHALL: Reflection data enables auto root-signature generation
