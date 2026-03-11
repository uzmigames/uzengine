# 1.4.0 — Asset Pipeline

## Phase 1: Importer Framework & Shader Importer
- [x] 1.1 Define `IAssetImporter` interface (`Import()`, `Name()`, `Version()`, `Extensions()`)
- [x] 1.2 Implement `AssetImporterRegistry` (register/lookup importers by extension)
- [x] 1.3 Implement `ShaderImporter` — HLSL → DXIL bytecode via DXC
- [x] 1.4 Define `.uzshader` binary format (header + DXIL blob + reflection metadata)
- [x] 1.5 Tests: importer registry (15 tests), .uzshader format (17 tests)

## Phase 2: Content-Addressed Cache (DDC)
- [x] 2.1 Implement `AssetCache` — put/get by xxHash64 key, `.cache/` storage
- [x] 2.2 Cache key derivation: `xxhash64(importer_name + version + source_hash + options_hash)`
- [x] 2.3 Automatic invalidation on source change or importer version bump
- [x] 2.4 Tests: cache hit/miss, invalidation, store/lookup, statistics (26 tests)

## Phase 3: Import Sidecar System
- [x] 3.1 Define `.uzimport` JSON schema (importer, version, source_hash, options, outputs)
- [x] 3.2 Implement sidecar read/write with automatic reimport on hash mismatch
- [x] 3.3 Platform variant support in sidecar (`outputs.sm6_6`, `outputs.sm5_0`)
- [ ] 3.4 Wire ShaderImporter to produce sidecars
- [x] 3.5 Tests: sidecar JSON round-trip, file I/O, reimport detection (16 tests)

## Phase 4: Asset Archive (.uzpak)
- [x] 4.1 Define `.uzpak` binary format (header + data blobs + index trailer)
- [x] 4.2 Implement `PakWriter` — pack imported assets into archive
- [x] 4.3 Implement `PakReader` — O(1) hash lookup, memory-mapped reads
- [x] 4.4 64-byte alignment for GPU resources (DX12 texture requirement)
- [x] 4.5 Tests: pack/unpack round-trip, hash collision handling, alignment

## Phase 5: Virtual File System (VFS)
- [x] 5.1 Define `IVFS` interface (`Read()`, `Exists()`, `Name()`)
- [x] 5.2 Implement `LooseFileVFS` — dev mode, reads from disk
- [x] 5.3 Implement `PakVFS` — ship mode, reads from `.uzpak`
- [x] 5.4 Implement `LayeredVFS` — priority-based mount stack (base + patch paks)
- [ ] 5.5 Wire engine ResourceLoader to use VFS transparently
- [x] 5.6 Tests: loose vs pak reads, layered override, missing file handling (31 tests)

## Phase 6: Asset Manifest (.uzmanifest)
- [x] 6.1 Define manifest binary format (sorted entries by pathHash, dep table)
- [x] 6.2 `AssetManifestWriter` — generate at pack time (type, size, dependencies)
- [x] 6.3 `AssetManifest` — runtime loader, single read, O(log n) binary search
- [x] 6.4 Tests: serialization, round-trip, queries, error handling, file I/O (26 tests)

## Phase 7: Integration & Migration
- [ ] 7.1 Migrate triangle demo from runtime HLSL compilation to pre-imported .uzshader
- [x] 7.2 Add `uzcook` command — batch import + pack for target platform
- [ ] 7.3 FileWatcher integration — auto-reimport on source change (dev mode)
- [ ] 7.4 Update demos to load via VFS
- [ ] 7.5 Tests: end-to-end cook pipeline, hot-reload, demo validation

## Success Criteria
- [ ] No source assets (HLSL, PNG, etc.) in build output — only .uzshader, .uztexture, etc.
- [ ] Content-hash cache eliminates redundant imports (cache hit = skip)
- [ ] `.uzpak` archive loads all assets via O(1) hash lookup
- [ ] Dev mode: loose files with auto-reimport on change
- [ ] Ship mode: single `.uzpak` with pre-cooked content
- [ ] 95%+ test coverage on new code
