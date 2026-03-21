## Phase 1: Master Index Format (.uzpakindex)
- [x] 1.1 Define `MasterIndexHeader` struct — 16 bytes packed, magic/version/pakCount/entryCount, static_assert verified.
- [x] 1.2 Define `MasterPakTableEntry` struct — 264 bytes packed, pakId + filename char[256] + checksum, static_assert verified.
- [x] 1.3 Define `MasterAssetEntry` struct — 48 bytes packed, pathHash/pakId/offset/compressedSize/rawSize/contentHash/typeId + alignment pads, static_assert verified.
- [x] 1.4 Define `MasterIndexTrailer` struct — 20 bytes packed, pakTableOffset/entryTableOffset/magic, static_assert verified.
- [x] 1.5 Implement `WriteMasterIndex()` — validates filenames/pakIds, sorts by pathHash, writes header+entries+pakTable+trailer via FileWriteAll.
- [x] 1.6 Implement `MasterIndex::LoadFile()` — reads file, validates header/trailer magic+version, bounds-checks tables, builds unordered_map for O(1) lookup.
- [x] 1.7 Implement `MasterIndex::Lookup()` + Contains/EntryCount/PakCount/PakFilenames/Unload — returns optional<AssetLocation> with full location info.
- [x] 1.8 Build verification — all new code appended to existing pak_format.h/cpp, no existing code modified.

## Phase 2: LZ4 Compression + XOR Scramble
- [ ] 2.1 Add LZ4 to the build: `runtime/thirdparty/lz4/` — lz4.h + lz4.c (single-file, BSD license, ~1600 lines). Update `scripts/setup.ps1` if needed.
- [x] 2.2 Implement `PakCompressBlob()` — XOR scramble with 8-byte key (pathHash ^ secret). LZ4 step deferred to 2.1 (lib not yet integrated).
- [x] 2.3 Implement `PakDecompressBlob()` — XOR descramble, size-mismatch error when no LZ4. Symmetric with PakCompressBlob.
- [x] 2.4 Add `kPakFlagCompressed = 0x1` and `kPakFlagScrambled = 0x2` constants to pak_format.h.
- [x] 2.5 Build verification — scramble/descramble functions compile and are ready for round-trip testing.

## Phase 3: PakStreamReader (Seek-Based Lazy Reader)
- [x] 3.1 Implement `PakStreamReader` class — Open/ReadBlob/Close/IsOpen/Path, RAII destructor, move semantics, copy deleted. FILE*-based seek+read.
- [x] 3.2 Platform-appropriate I/O — `_fseeki64` on Windows, `fseeko` on POSIX, via `#ifdef _WIN32`.
- [ ] 3.3 Add file handle pooling: max 8 simultaneous open pak files, LRU eviction of least-recently-used handles.
- [x] 3.4 Build verification — PakStreamReader class fully implemented in pak_format.h/cpp with error logging and profiling.

## Phase 4: LRU Cache
- [x] 4.1 Implement `LRUCache<Key, Value>` template in `runtime/engine/core/lru_cache.h`. Header-only, configurable maxBytes budget, Get/Put/Clear/Contains/CurrentBytes/MaxBytes/EntryCount API.
- [x] 4.2 Internal: `std::list<pair<Key, Entry>>` + `std::unordered_map<Key, list::iterator>` for O(1) get/put. Splice-to-front on hit, evict-from-back on budget overflow.
- [x] 4.3 Default budget: 256MB (configurable via `--pak-cache-mb` CLI flag or `AppConfig::pakCacheMB`). Constructor accepts arbitrary maxBytes; 256MB default applied at PakVFS integration layer.
- [x] 4.4 Build verification + unit test: 19 LRU tests (eviction order, budget enforcement, promotion, clear, update, oversized items) + 27 pak tests (MasterIndex round-trip, XOR scramble, PakStreamReader seek+read). All pass.

## Phase 5: Multi-Pak Writer (WritePakSplit)
- [x] 5.1 Implement `WritePakSplit(inputEntries, outputDir, baseName, maxPakSizeBytes, secret)` in `pak_format.cpp`. Full implementation: XOR scramble per blob, pak rollover on size threshold, in-memory buffer per pak (header+aligned blobs+trailer), master index via WriteMasterIndex(). PakSplitResult struct added to header.
- [x] 5.2 Naming convention: paks are `{baseName}_{NNN}.uzpak` (3-digit zero-padded via snprintf `%03u`).
- [x] 5.3 Default `maxPakSizeBytes` = kMaxPakSize (500MB). Min-clamped to header+trailer+alignment.
- [x] 5.4 Log summary: per-pak entry count + size, final summary with total assets, paks, raw bytes, packed bytes, index path.
- [x] 5.5 Build verification — declaration in pak_format.h, implementation in pak_format.cpp, no existing code modified.

## Phase 6: Refactor PakVFS to Lazy Load
- [x] 6.1 Refactor `PakVFS` in vfs.h/vfs.cpp: added `FromIndexFile()` static factory, private default ctor, MasterIndex + PakStreamReader map + LRUCache (unique_ptr) members. Legacy PakReader ctor and FromFile preserved for backward compat.
- [x] 6.2 Implement `PakVFS::Read(path)`: hash->cache check->index lookup->lazy PakStreamReader open->ReadBlob->PakDecompressBlob->cache Put->return. Legacy mode delegates to PakReader::Lookup.
- [x] 6.3 Implement `PakVFS::Exists(path)`: lazy mode uses XXH64 + MasterIndex::Contains (no I/O). Legacy mode delegates to PakReader::Contains.
- [x] 6.4 Implement `PakVFS::ResolvePath(path)`: returns empty string (pak assets have no filesystem path). Override added to both header and implementation.
- [x] 6.5 Keep existing `LooseFileVFS` unchanged — zero modifications to LooseFileVFS or LayeredVFS.
- [x] 6.6 Build verification — all 21 compilation steps pass (test_runtime.exe locked by external process, not a compile error). Added #include for lru_cache.h and xxhash.h.

## Phase 7: uzpack CLI Tool
- [x] 7.1 Create `runtime/tools/uzpack.cpp` — standalone CLI with --input, --output, --name, --max-pak-mb, --secret, --verbose, --help flags.
- [x] 7.2 Implementation: recursive directory scan, PakWriteEntry creation with forward-slash relative paths, WritePakSplit call, summary printout.
- [x] 7.3 Added `uzpack` to `tool_names` array in `runtime/build.zig` + "pack" run step. Builds as part of normal `zig build`.
- [x] 7.4 Build + run test: packed 95 shader files into 1 pak (1.14 MB), index + pak produced correctly.

## Phase 8: Build System Integration
- [ ] 8.1 Modify `runtime/demos/common/build_common.zig`:
  - Add a `RunStep` that executes uzpack after compilation
  - Input: demo's `assets/` directory
  - Output: `zig-out/bin/game.uzpakindex` + `zig-out/bin/game_NNN.uzpak`
  - Remove all `extra_deploy` file copy entries from demos
- [ ] 8.2 Keep a dev mode flag: `zig build -Ddev=true` → copies loose files (current behavior, for hot-reload). Default (`-Ddev=false`) → runs uzpack.
- [x] 8.3 Added `uzpack` to build.zig tool_names array and "pack" run step with arg passthrough. Demo integration deferred to 8.1.
- [ ] 8.4 Build verification: `cd demos/pbr_test && zig build` → produces `game.uzpakindex` + `game_001.uzpak` instead of `assets/` directory.

## Phase 9: Application Auto-Detection
- [x] 9.1 Modified `Application::InitVFS()`: auto-detects `game.uzpakindex` next to exe, uses `PakVFS::FromIndexFile()` with LRU cache. Falls back to LooseFileVFS on failure or absence.
- [x] 9.2 `kPakSecret` defined as `constexpr uint64_t = 0` locally in InitVFS(). Will be moved to a non-header file when demos start using pak mode with a real secret.
- [x] 9.3 Logs VFS mode: "VFS: pak mode (<path>)" or "VFS: loose file mode (<path>)" via UZ_LOG(App, Info).
- [x] 9.4 Added `--force-loose` CLI flag to ParseArgs, sets `AppConfig::forceLooseVFS`. Also added `--pak-cache-mb` flag and `AppConfig::pakCacheMB` (default 256).

## Phase 10: Validation
- [ ] 10.1 End-to-end test: build pbr_test with pak mode → run → verify all 9 spheres render correctly, all textures load, shaders compile.
- [ ] 10.2 Visual verification: `pbr_test --screenshot-stdout --screenshot-frame 5 --frames 8` — compare with loose-file screenshot (must be pixel-identical).
- [ ] 10.3 Verify no `assets/` directory in `zig-out/bin/` when building in pak mode.
- [ ] 10.4 Verify protection: try to open .uzpak with 7-Zip, WinRAR, binwalk — all must fail to extract usable files.
- [ ] 10.5 Performance test: measure VFS::Read() latency for cold load (first access) and hot load (LRU cache hit). Cold < 5ms for 1MB asset, hot < 0.01ms.
- [ ] 10.6 Update `docs/specs/asset-pipeline.md` with multi-pak architecture, .uzpakindex format, and lazy load design.
