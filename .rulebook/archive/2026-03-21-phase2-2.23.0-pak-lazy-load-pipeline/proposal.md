# Proposal: phase2-2.23.0-pak-lazy-load-pipeline

## Priority: P0 — HIGH

## Why

The current demo build copies 70+ individual asset files to `zig-out/bin/assets/`, exposing raw original files that any user can browse and copy. Additionally, the existing `PakReader` loads the entire `.uzpak` into RAM — making it unusable for games with 5-20GB of assets.

**Problems solved:**
1. **Asset protection**: Raw files (.dds, .uzmesh, .ogg) are visible on disk → anyone can extract them
2. **Build bloat**: 70+ individual file copies per demo build, fragile and slow
3. **Memory**: `PakReader::Load()` copies the entire archive into `m_data` → 20GB game = 20GB RAM at boot
4. **Scalability**: Single .uzpak has no size limit → filesystem issues with multi-GB files

## What Changes

### New: Multi-pak architecture with master index
- `.uzpakindex` — small master index (~2MB for 50k assets), loaded entirely in RAM
- `.uzpak` files — data-only paks, max 500MB each, NEVER loaded entirely
- `PakStreamReader` — seek-based lazy reader, reads individual blobs on demand
- `LRUCache` — configurable memory budget (~256MB default) for recently loaded assets

### New: LZ4 compression + XOR scramble
- All blobs LZ4-compressed before packing (30-50% size reduction)
- XOR scramble with per-asset key derived from `pathHash ^ compiledSecret`
- Custom binary format — not openable by any standard tool (not ZIP/7z/tar)
- No path strings stored — only xxHash64 hashes (impossible to enumerate asset names)

### New: `uzpack` CLI tool
- Scans input directory recursively → LZ4 compress + XOR scramble → split into 500MB paks → write master index
- Integrated as Zig build step, replacing 70+ individual file copy operations

### Changed: `Application::InitVFS()` auto-detection
- If `game.uzpakindex` exists next to .exe → `PakVFS` (shipping mode)
- Otherwise → `LooseFileVFS("assets/")` (development mode)
- Zero config change needed — just presence of .uzpakindex triggers pak mode

## Impact
- Affected specs: `docs/specs/asset-pipeline.md`, `docs/engine/08-resource-system/`
- Affected code:
  - `runtime/engine/asset/formats/pak_format.h/cpp` — add PakStreamReader, MasterIndex, LZ4+XOR
  - `runtime/engine/asset/vfs.h/cpp` — refactor PakVFS to lazy load
  - `runtime/engine/core/lru_cache.h` — new generic LRU cache template
  - `runtime/tools/uzpack.cpp` — new CLI tool
  - `runtime/demos/common/build_common.zig` — replace file copies with uzpack step
  - `runtime/engine/app/application.cpp` — auto-detect VFS mode
- Breaking change: NO — development mode (LooseFileVFS) unchanged, pak mode is additive
- User benefit: Assets protected from extraction, faster builds (1 pak vs 70+ copies), games can ship 20GB+ without RAM issues
