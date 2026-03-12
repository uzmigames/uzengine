# Resource System — .uzpak Archives

> **Status**: Done (Milestone 1.2)
> **Source**: `core/asset/pak_format.h/.cpp`
> **Depends on**: xxHash

## Purpose

The `.uzpak` format is UzEngine's shipping archive — a flat binary file containing precompiled assets indexed by path hash for O(1) lookup. During development, assets are loaded from loose files on disk. For shipping, `uzcook` packs everything into `.uzpak` archives.

## Binary Layout

```
Offset  Size       Content
──────  ─────────  ─────────────────────────────────
0       16 bytes   Header
                     magic:      "UZPK" (0x4B505A55)
                     version:    uint32 (1)
                     entryCount: uint32
                     flags:      uint32 (reserved)

16      variable   Data Region
                     blob[0]:    padded to 64-byte boundary
                     blob[1]:    padded to 64-byte boundary
                     ...

?       N×40       Index (sorted by pathHash for binary search)
                     per entry (40 bytes):
                       pathHash:    uint64 (xxHash64 of logical path)
                       offset:      uint64 (byte offset into data region)
                       size:        uint64 (uncompressed size)
                       contentHash: uint64 (xxHash64 of blob, integrity)
                       typeId:      uint32 (asset type, 0 = generic)
                       reserved:    uint32

?       16 bytes   Trailer
                     indexOffset:   uint64 (byte offset of Index)
                     indexChecksum: uint32 (reserved, 0)
                     magic:        "UZPK" (sentinel for trailer discovery)
```

### Why 64-Byte Alignment?

DX12 requires buffer and texture resources to be aligned to specific boundaries for GPU access. 64-byte alignment satisfies the strictest DX12 alignment requirement, allowing pak data to be mapped directly to GPU memory without copies in future optimizations.

### Why Sorted Index?

The index is sorted by `pathHash` (ascending) enabling:
1. **O(1) hash map lookup** — primary access path via `std::unordered_map` built at load time
2. **Binary search fallback** — useful for debugging or when the hash map isn't available
3. **Sequential scan** — for iteration and diagnostics

## Writing Archives

```cpp
std::vector<PakWriteEntry> entries;
entries.push_back({
    .path = "shaders/simple_vs.uzshader",
    .data = compiledVertexShader,
    .typeId = 0
});
entries.push_back({
    .path = "textures/brick.uztexture",
    .data = compressedTexture,
    .typeId = 1
});

// Write to memory
std::vector<uint8_t> archive = WritePak(entries);

// Or write directly to file
WritePakFile("build/game.uzpak", entries);
```

`WritePak()` handles:
1. Writing the 16-byte header
2. Writing each blob with 64-byte alignment padding
3. Computing xxHash64 for each path (index key) and blob (integrity)
4. Sorting the index by pathHash
5. Writing the index and trailer

## Reading Archives

```cpp
PakReader reader;
if (reader.LoadFile("build/game.uzpak")) {
    // O(1) lookup by path
    auto data = reader.Lookup("shaders/simple_vs.uzshader");
    if (!data.empty()) {
        // Use the asset bytes
    }

    // Check existence without loading
    bool exists = reader.Contains("textures/brick.uztexture");

    // Diagnostics
    size_t count = reader.EntryCount();
    auto hashes = reader.GetPathHashes();
}
```

### Load Process

1. Read entire file into memory
2. Validate header (magic, version, minimum size)
3. Read trailer (last 16 bytes) to find index offset
4. Validate trailer magic
5. Parse index entries into `unordered_map<uint64_t pathHash, LoadedEntry>`
6. Lookups slice into the in-memory data buffer

### PakVFS Integration

The `PakVFS` wraps `PakReader` into the `IVFS` interface:

```cpp
auto pakVfs = PakVFS::FromFile("build/game.uzpak");
if (pakVfs) {
    auto data = pakVfs->Read("shaders/simple_vs.uzshader");
    bool exists = pakVfs->Exists("textures/brick.uztexture");
}
```

This allows pak archives to participate in the `LayeredVFS` mount stack alongside loose files.

## Engine Comparison

| Aspect | UE5 (.pak) | Godot (.pck) | UzEngine (.uzpak) |
|--------|-----------|-------------|-------------------|
| Index format | Encrypted SHA1 path hash | String path index | xxHash64 path hash |
| Compression | Per-entry (zlib, oodle) | Optional (zstd) | **None** (planned: LZ4) |
| Encryption | Yes (AES-256 per entry) | Optional (AES) | **No** (planned) |
| Alignment | 2KB (default) | None | **64 bytes** (DX12 compatible) |
| Lookup | Hash table + mount point | Linear scan + cache | **Hash map** (O(1)) |
| Signing | Yes (RSA) | No | **No** (planned: integrity via contentHash) |
| Max archive size | 4GB (32-bit offsets in some versions) | 4GB (32-bit) | **16 EB** (64-bit offsets) |

### Why Not Use UE5's Format?

UE5's `.pak` format is production-proven but includes features UzEngine doesn't need yet (encryption, signing, compression) and lacks features UzEngine does need (64-byte GPU alignment, 64-bit offsets). The custom format is also significantly simpler — the entire reader/writer fits in ~400 lines.

## Planned Enhancements

1. **LZ4 compression** — per-entry with stored/compressed sizes in the index
2. **Streaming support** — load index first, then demand-page data blobs
3. **Patching** — delta archives that override entries in a base archive via LayeredVFS
4. **Integrity verification** — validate `contentHash` on load (opt-in for debug builds)

## Rules

1. **ALWAYS use 64-byte alignment** for data blobs — future GPU direct-load depends on it
2. **ALWAYS sort the index by pathHash** — enables binary search and consistent output
3. **NEVER store paths in the archive** — only hashes. Path → hash mapping lives in `.uzmanifest`
4. **ALWAYS validate magic and version on load** — reject unknown versions early
5. **ALWAYS use `WritePakFile` for disk writes** — handles atomicity and error reporting
