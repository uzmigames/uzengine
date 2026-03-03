## 1. Dependencies

- [ ] 1.1 Add `zstd` to vcpkg.json
- [ ] 1.2 Add `xxhash` to vcpkg.json (path hashing for directory)

## 2. PAK Writer (`src/assets/pak_writer.h/cpp`)

- [ ] 2.1 `PakWriter::begin(output_path)` — open output file, stream write
- [ ] 2.2 `PakWriter::add_file(vpath, data, size)` — zstd-compress + write data section
- [ ] 2.3 `PakWriter::finish()` — write directory table (sorted by xxhash3 of vpath) + footer
- [ ] 2.4 Footer: directory_offset, entry_count, version=1, magic=0x4B50555A
- [ ] 2.5 Per-entry: path_hash(uint64), offset(uint64), compressed_size(uint32), raw_size(uint32), asset_type(uint16), flags(uint16)
- [ ] 2.6 Compression level: configurable (default zstd level 9)
- [ ] 2.7 Progress callback for build tool progress reporting

## 3. PAK Reader (`src/assets/pak_reader.h/cpp`)

- [ ] 3.1 `PakReader::open(path)` — memory-map via CreateFileMappingW
- [ ] 3.2 `PakReader::find(vpath)` → `PakEntry*` or null (binary search by hash)
- [ ] 3.3 `PakReader::read(entry, Arena& arena)` → decompressed bytes in arena
- [ ] 3.4 Validate footer magic on open, error if mismatch
- [ ] 3.5 `PakReader::close()` — UnmapViewOfFile

## 4. Virtual Filesystem (`src/assets/vfs.h/cpp`)

- [ ] 4.1 `VFS::mount(path, priority)` — mounts PAK or loose directory
- [ ] 4.2 `VFS::unmount(path)` — remove a mounted source
- [ ] 4.3 `VFS::open(vpath)` → `VfsFile` with `.read(buf)`, `.size()`
- [ ] 4.4 Lookup order: highest priority source first (patch PAK overrides base)
- [ ] 4.5 Loose file mount: falls back to real filesystem (dev mode)
- [ ] 4.6 `res://` prefix resolution: `res://textures/wood.dds` → strip prefix, lookup in VFS

## 5. CLI Tool (`tools/pak_tool/pak_tool.cpp`)

- [ ] 5.1 `uzpak create --project=./ --output=game.uzpak` — pack all cooked assets
- [ ] 5.2 `uzpak list game.uzpak` — list entries with size and type
- [ ] 5.3 `uzpak extract game.uzpak textures/wood.dds out/` — extract single file
- [ ] 5.4 `uzpak diff base.uzpak new.uzpak --output=patch.uzpak` — generate patch PAK

## 6. Integration

- [ ] 6.1 Engine startup: mount project.uzpak if found, else mount loose files directory
- [ ] 6.2 All asset loaders use VFS instead of direct fopen
- [ ] 6.3 `asset.pack` IPC handler calls PakWriter internally
- [ ] 6.4 Test: pack a small project, run from PAK, all assets load correctly
