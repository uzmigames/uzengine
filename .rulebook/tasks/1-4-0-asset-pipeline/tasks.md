# 1.4.0 — Asset Pipeline

## Phase 1: Importer Framework & Shader Importer
- [ ] 1.1 Define `IAssetImporter` interface (`import()`, `name()`, `version()`, `extensions()`)
- [ ] 1.2 Implement `AssetImporterRegistry` (register/lookup importers by extension)
- [ ] 1.3 Implement `ShaderImporter` — HLSL → DXIL bytecode via DXC
- [ ] 1.4 Define `.uz_shader` binary format (header + DXIL blob + reflection metadata)
- [ ] 1.5 Tests: importer registry, shader import round-trip, format validation

## Phase 2: Content-Addressed Cache (DDC)
- [ ] 2.1 Implement `AssetCache` — put/get by xxHash64 key, `.uz_cache/` storage
- [ ] 2.2 Cache key derivation: `xxhash64(importer_name + version + source_hash + options_hash)`
- [ ] 2.3 Automatic invalidation on source change or importer version bump
- [ ] 2.4 Tests: cache hit/miss, invalidation, concurrent access

## Phase 3: Import Sidecar System
- [ ] 3.1 Define `.uz_import` JSON schema (importer, version, source_hash, options, outputs)
- [ ] 3.2 Implement sidecar read/write with automatic reimport on hash mismatch
- [ ] 3.3 Platform variant support in sidecar (`outputs.sm6_6`, `outputs.sm5_0`)
- [ ] 3.4 Wire ShaderImporter to produce sidecars
- [ ] 3.5 Tests: sidecar creation, reimport trigger, variant selection

## Phase 4: Asset Archive (.uzpak)
- [ ] 4.1 Define `.uzpak` binary format (header + data blobs + index trailer)
- [ ] 4.2 Implement `PakWriter` — pack imported assets into archive
- [ ] 4.3 Implement `PakReader` — O(1) hash lookup, memory-mapped reads
- [ ] 4.4 64-byte alignment for GPU resources (DX12 texture requirement)
- [ ] 4.5 Tests: pack/unpack round-trip, hash collision handling, alignment

## Phase 5: Virtual File System (VFS)
- [ ] 5.1 Define `IVFS` interface (`Read()`, `Exists()`, `Mount()`)
- [ ] 5.2 Implement `LooseFileVFS` — dev mode, reads from disk
- [ ] 5.3 Implement `PakVFS` — ship mode, reads from `.uzpak`
- [ ] 5.4 Implement `LayeredVFS` — priority-based mount stack (base + patch paks)
- [ ] 5.5 Wire engine ResourceLoader to use VFS transparently
- [ ] 5.6 Tests: loose vs pak reads, layered override, missing file handling

## Phase 6: Asset Registry
- [ ] 6.1 Define registry binary format (sorted entries by path_hash)
- [ ] 6.2 Generate registry at pack time (asset type, size, dependencies)
- [ ] 6.3 Runtime loader — single read at startup, binary search queries
- [ ] 6.4 Tests: registry generation, query performance, dependency traversal

## Phase 7: Integration & Migration
- [ ] 7.1 Migrate triangle demo from runtime HLSL compilation to pre-imported .uz_shader
- [ ] 7.2 Add `uz_cook` command — batch import + pack for target platform
- [ ] 7.3 FileWatcher integration — auto-reimport on source change (dev mode)
- [ ] 7.4 Update demos to load via VFS
- [ ] 7.5 Tests: end-to-end cook pipeline, hot-reload, demo validation

## Success Criteria
- [ ] No source assets (HLSL, PNG, etc.) in build output — only .uz_shader, .uz_texture, etc.
- [ ] Content-hash cache eliminates redundant imports (cache hit = skip)
- [ ] `.uzpak` archive loads all assets via O(1) hash lookup
- [ ] Dev mode: loose files with auto-reimport on change
- [ ] Ship mode: single `.uzpak` with pre-cooked content
- [ ] 95%+ test coverage on new code
