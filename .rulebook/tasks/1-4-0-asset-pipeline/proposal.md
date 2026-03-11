# 1.4.0 — Asset Pipeline

## Why

Source assets (HLSL shaders, PNG textures, glTF meshes, WAV audio) MUST NEVER ship in the final build. Like Unreal's Cook pipeline and Godot's Import system, UzEngine needs an asset pipeline that transforms source files into optimized, platform-specific runtime formats. The game executable loads only pre-processed bytecode, compressed textures, and optimized meshes — never raw source.

Currently the triangle demo compiles HLSL at runtime via DXC. This is acceptable during early development but fundamentally wrong for any shipped build:
- Runtime compilation adds startup latency
- Source shaders expose implementation details
- No platform-variant support (SM6.6 vs SM5 fallback)
- No content-addressed caching — recompiles every launch

## What Changes

### 1. Asset Importer Framework
A registry of `IAssetImporter` implementations, each converting one source format into a runtime-ready format:
- **ShaderImporter**: HLSL → DXIL bytecode (`.uz_shader`)
- **TextureImporter**: PNG/JPG/TGA → BC7/BC1 compressed (`.uz_texture`) — future
- **MeshImporter**: glTF/FBX → optimized vertex/index buffers (`.uz_mesh`) — future
- **AudioImporter**: WAV/OGG → compressed audio (`.uz_audio`) — future

Each importer is a pure function: `(source_bytes, options) → (output_bytes, metadata)`. No global state. Deterministic output for identical inputs.

### 2. Content-Addressed Cache (Derived Data Cache)
Extends the existing shader content-hash cache to all asset types:
- **Cache key** = `xxhash64(importer_name + importer_version + source_content_hash + options_hash)`
- **Cache storage**: `.uz_cache/` directory with flat files named by hash
- **Invalidation**: Automatic when source changes or importer version bumps
- **No timestamps** — content hash only (already established convention)

### 3. Import Sidecar Files (`.uz_import`)
JSON metadata file next to each source asset (inspired by Godot's `.import` system):
```json
{
  "importer": "shader",
  "version": 1,
  "source_hash": "a1b2c3d4e5f67890",
  "options": { "model": "sm6_6", "entry": "main", "stage": "vertex" },
  "outputs": {
    "sm6_6": ".uz_cache/imported/simple_vs-a1b2c3d4.uz_shader"
  }
}
```
- Sidecar IS the cache invalidation mechanism
- Human-readable, LLM-friendly (JSON)
- Platform variants stored as separate output entries
- Sidecars committed to git; `.uz_cache/` is gitignored

### 4. Asset Archive (`.uzpak`)
Flat archive for distribution:
- **Header**: magic + version + file count
- **Index**: sorted array of `{ path_hash(xxhash64), offset, size, content_hash }` — binary-searchable
- **Data**: concatenated blobs, 64-byte aligned (DX12 texture alignment)
- **Trailer**: index offset + checksum (append-friendly, like UE5's `.pak`)

### 5. Virtual File System (VFS)
Transparent abstraction over loose files and archives:
- **Dev mode**: reads from disk (source → import on demand → cache → load)
- **Ship mode**: reads from `.uzpak` (pre-cooked, O(1) hash lookup)
- Same API for both: `vfs.Read("shaders/simple_vs.uz_shader")`
- Layered mounting with priority (base pak + DLC/patch paks)

### 6. Asset Registry
Build-time metadata index for fast asset discovery without loading:
- Generated during cook/pack step
- Flat binary: sorted `{ path_hash, type_id, pak_offset, size, dependency_count }`
- Loaded at startup in one read
- Enables "what assets exist?" queries without touching pak data

## Architecture Principles

1. **Dev vs Ship separation**: Dev mode is loose/lazy/on-demand. Ship mode is pre-cooked/packed/indexed. The VFS hides which mode.
2. **Pure build functions**: Importers are stateless. Same inputs → same outputs. This makes caching safe.
3. **Content-addressed everything**: No timestamps. xxHash64 of content determines cache validity.
4. **Platform variants in metadata**: Sidecar stores `outputs.sm6_6`, `outputs.sm5_0`, `outputs.bc7`, `outputs.astc`. Loader picks variant matching current GPU.
5. **Open formats**: All metadata is JSON. Archive index is a simple binary table. LLM-friendly by design.

## Reference Engines

- **Godot**: `.import` sidecar system, `ResourceImporter` registry, `.pck` flat archive, import-time shader compilation to SPIR-V
- **Unreal**: Derived Data Cache (content-addressed), Cook pipeline (platform-targeted), `.pak` archive (hash-indexed, trailer pattern), Asset Registry (metadata-only queries), PSO precache
