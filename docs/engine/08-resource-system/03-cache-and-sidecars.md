# Resource System — Cache & Import Sidecars

> **Status**: Done (Milestone 1.2)
> **Source**: `runtime/engine/asset/asset_cache.h/.cpp`, `runtime/engine/asset/import_sidecar.h/.cpp`
> **Depends on**: xxHash, nlohmann/json, filesystem

## Purpose

The asset cache stores compiled import results on disk, keyed by content hash. Import sidecars (`.uzimport` JSON files) track what was imported and when, enabling incremental re-importing — only assets whose source, options, or importer version changed get reprocessed.

## Asset Cache

### Key Derivation

Cache keys are computed from four inputs, ensuring automatic invalidation when anything changes:

```
key = xxHash64(
    importer_name       +   // "shader" — different importers can't collide
    importer_version    +   // 4 bytes LE — version bump invalidates
    source_content_hash +   // 8 bytes LE — xxHash64 of source bytes
    options_hash            // 8 bytes LE — xxHash64 of sorted options
)
```

```cpp
uint64_t key = AssetCache::ComputeKey(
    "shader",               // importer name
    1,                      // importer version
    sourceHash,             // xxHash64 of .hlsl source
    optionsHash             // xxHash64 of compile options
);
```

### Storage Layout

```
.cache/
└── imported/
    ├── a1b2c3d4e5f67890.bin    — compiled shader bytecode
    ├── f0e1d2c3b4a59678.bin    — compressed texture data
    └── ...
```

Files are named by their 16-character hex key. The directory structure is flat — no subdirectories per type. This simplifies cleanup (delete the entire directory) and avoids deep nesting.

### API

```cpp
AssetCache cache(".cache");

// Store
uint64_t key = AssetCache::ComputeKey("shader", 1, srcHash, optsHash);
cache.Store(key, compiledBytes);

// Lookup
auto result = cache.Lookup(key);
if (result.has_value()) {
    // Cache hit — use result.value()
} else {
    // Cache miss — reimport needed
}

// Diagnostics
size_t count = cache.EntryCount();      // Number of cached files
size_t bytes = cache.TotalSizeBytes();  // Total disk usage

// Nuclear option
cache.Clear();  // Delete everything — safe, reimport regenerates
```

### Hashing Utilities

```cpp
// Hash raw bytes
uint64_t hash = AssetCache::HashBytes(data.data(), data.size());

// Hash a string
uint64_t hash = AssetCache::HashString("vertex shader options");
```

All hashing uses xxHash64 with seed 0 for consistency.

## Import Sidecars (.uzimport)

### Purpose

While the cache stores compiled output, sidecars store the **metadata** that describes the relationship between a source file and its cached output. This enables three critical operations:

1. **Skip up-to-date assets** — compare current source hash against sidecar's stored hash
2. **Invalidate on version bump** — compare current importer version against sidecar's version
3. **Locate output files** — sidecar stores the path to each compiled variant

### Schema

```json
{
    "importer": "shader",
    "version": 1,
    "source_hash": "a1b2c3d4e5f67890",
    "options": {
        "entry_point": "main",
        "stage": "vertex",
        "model": "sm6_6"
    },
    "outputs": {
        "sm6_6": ".cache/imported/a1b2c3d4e5f67890.bin"
    },
    "dependencies": [
        "common.hlsli",
        "lighting_utils.hlsli"
    ]
}
```

### Reimport Detection

```cpp
auto result = ReadSidecarFile("shaders/pbr.hlsl.uzimport");
if (!result.success) {
    // Sidecar missing or corrupt → reimport
}

uint64_t currentHash = AssetCache::HashBytes(sourceBytes.data(), sourceBytes.size());
std::string currentHashHex = HashToHex(currentHash);

if (NeedsReimport(result.sidecar, currentHashHex, currentImporterVersion)) {
    // One of three triggers fired:
    // 1. Source hash changed
    // 2. Importer version bumped
    // 3. Output file missing from disk
}
```

### `NeedsReimport` Triggers

| Condition | Check | Logs |
|-----------|-------|------|
| Source modified | `currentHash != sidecar.sourceHash` | "Source hash changed" |
| Importer updated | `currentVersion != sidecar.version` | "Importer version changed" |
| Output missing | Any output path in `sidecar.outputs` doesn't exist | "Output file missing: {path}" |

### Hash Conversion Utilities

```cpp
// uint64 → hex string (16 chars, zero-padded)
std::string hex = HashToHex(0xDEADBEEFCAFE1234);  // "deadbeefcafe1234"

// hex string → uint64
uint64_t val = HexToHash("deadbeefcafe1234");       // 0xDEADBEEFCAFE1234
```

## Full Import Flow Example

```cpp
AssetCache cache(".cache");
AssetImporterRegistry registry;
registry.Register(std::make_unique<ShaderImporter>());

// For each source file:
std::string sourcePath = "shaders/pbr_vs.hlsl";
auto sourceBytes = ReadFile(sourcePath);
uint64_t sourceHash = AssetCache::HashBytes(sourceBytes.data(), sourceBytes.size());

// 1. Check sidecar
auto sidecarResult = ReadSidecarFile(sourcePath + ".uzimport");
if (sidecarResult.success &&
    !NeedsReimport(sidecarResult.sidecar, HashToHex(sourceHash), 1)) {
    continue;  // Skip — already up to date
}

// 2. Import
auto* importer = registry.FindByExtension(".hlsl");
AssetImportOptions opts{.entryPoint = "main", .stage = "vertex", .model = "sm6_6"};
auto result = importer->Import(sourceBytes, "pbr_vs.hlsl", opts);

if (!result.success) {
    UZ_LOG(Asset, Error, "Import failed: {}", result.error);
    continue;
}

// 3. Cache
uint64_t optsHash = AssetCache::HashString("vertex_sm6_6");
uint64_t cacheKey = AssetCache::ComputeKey("shader", 1, sourceHash, optsHash);
cache.Store(cacheKey, result.data);

// 4. Write sidecar
ImportSidecar sidecar;
sidecar.importer = "shader";
sidecar.version = 1;
sidecar.sourceHash = HashToHex(sourceHash);
sidecar.outputs = {{"sm6_6", cache.CacheDirectory() + "/imported/" + HashToHex(cacheKey) + ".bin"}};
WriteSidecarFile(sourcePath + ".uzimport", sidecar);
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Cache key | FIoHash (Blake3) | File modification time | xxHash64 (content-based) |
| Sidecar format | FAssetRegistryState (binary) | `.import` (INI-style) | `.uzimport` (JSON) |
| Incremental check | Hash comparison (DDC) | mtime comparison | **Content hash + version + output existence** |
| Network sharing | Yes (shared DDC, Zeus/Horde) | No | No (local only, CI artifact caching) |

## Rules

1. **NEVER use timestamps for cache invalidation** — content hashes only
2. **ALWAYS write sidecars atomically** — write to temp file, then rename (prevents partial writes on crash)
3. **ALWAYS treat .cache/ as deletable** — the entire directory is regenerable
4. **NEVER modify cached entries** — cache is append-only. Wrong entry? Delete and reimport
5. **ALWAYS store dependencies in sidecars** — enables transitive invalidation (e.g., changed .hlsli triggers reimport of all .hlsl that include it)
