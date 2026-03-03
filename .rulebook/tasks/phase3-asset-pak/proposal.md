# Proposal: phase2-asset-pak

## Why

During development, UzEngine loads assets as loose files from the project directory. There is no container format for shipping a game: raw PNG textures, HLSL shaders, Lua scripts, and scene files are exposed directly on the player's filesystem, making them trivially extractable, easy to accidentally corrupt, and slow to load due to the high overhead of opening thousands of small individual files. A packaging step is required before any UzEngine title can be distributed.

A single seekable binary archive with LZ4 compression and CRC integrity checks addresses all three problems. LZ4 decompresses faster than any alternative at acceptable compression ratios, which is critical for load times. Per-entry CRC32 catches file corruption at runtime before bad data reaches the renderer or physics engine. A hash-based lookup table means the runtime never needs to iterate directory listings — asset lookups are O(1) regardless of archive size.

The same archive format also enables future patch layers: mounting a second `.uzpak` at higher priority than the base archive allows DLC and hotfix content to override base-game assets without modifying or re-shipping the original file.

## What Changes

- Define the `.uzpak` binary format: fixed 16-byte file header (magic `UZPK`, version, entry count, directory offset), followed by sequentially packed LZ4-compressed asset blobs, followed by a seekable directory table at end-of-file; each directory entry stores path hash (FNV-64), byte offset, compressed size, uncompressed size, CRC32, and flags
- Implement `src/vfs/pak_reader.cpp` — memory-maps the `.uzpak` file, parses the directory footer into a `std::unordered_map<uint64_t, PakEntry>`, decompresses entries on demand via lz4-single-file
- Implement `src/vfs/virtual_filesystem.cpp` — mount table with configurable priority order; `vfs::open(path)` hashes the path, walks mounts from highest to lowest priority, returns the first hit as a `VfsStream`; loose-file mounts always shadow pak entries at equal path when given higher priority
- Implement `tools/uzpak/pak_builder.cpp` — C++ CLI tool `uzpak build <project_dir> <output.uzpak>`; scans compiled asset outputs, compresses each with LZ4 level 9, computes CRC32 per entry, writes blobs sequentially, appends directory table, reports total ratio and per-asset stats
- Add `uzpak verify <file.uzpak>` subcommand that reads every entry, decompresses it, and validates CRC32 — used in CI to detect corrupted shipping archives
- Route all `AssetRegistry::load()` calls through `vfs::open()` so PAK and loose assets are interchangeable from game code with no source changes
- Add IPC command `vfs.listMounts` returning mount table as JSON (path, type: pak/loose, priority, entry count) for the VSCode Asset Browser debug panel
- Add unit tests in `tests/vfs/` covering: round-trip pack/read for binary files, CRC mismatch detection, priority override (loose file shadows pak entry), and hash collision handling
- Update `docs/specs/asset-pipeline.md` with full `.uzpak` binary layout diagram, directory entry field table, and VFS mount API reference

## Impact

- Affected specs: docs/specs/asset-pipeline.md
- Affected code: src/vfs/pak_reader.cpp, src/vfs/virtual_filesystem.cpp, src/assets/AssetRegistry.cpp, tools/uzpak/pak_builder.cpp, src/ipc/IpcServer.cpp
- Breaking change: NO for development (loose-file loading continues to work unchanged); YES for shipping workflow (release builds must be packaged via `uzpak build` before distribution)
- User benefit: Shipped games load assets from a single seekable compressed archive — faster startup, tamper-evident CRC checks, and a clear separation between development loose files and distribution-ready packages
