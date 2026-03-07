# Asset Pipeline Spec

## Overview

The asset pipeline converts source files (FBX, GLTF, PNG, WAV) into engine-optimized binary formats, packages them into PAK files for distribution, and manages the import metadata for incremental rebuilds.

---

## 1. Supported Source Formats

| Source Format | Import Type | Output |
|--------------|------------|--------|
| `.fbx` | Mesh, Skeleton, Animation | `.uzmesh`, `.uzskel`, `.uzanim` |
| `.glb` / `.gltf` | Mesh, Skeleton, Animation | `.uzmesh`, `.uzskel`, `.uzanim` |
| `.png` / `.jpg` / `.tga` | Texture | `.dds` (BC7/BC5/BC1) |
| `.hdr` | HDR Skybox | `.dds` (BC6H) |
| `.wav` / `.ogg` | Audio | `.ogg` (streamed) or inline |
| `.hlsl` / `.glsl` | Shader | Compiled at runtime (not imported) |
| `.uzscene` | Scene | Used directly, no import |
| `.uzmat` | Material | Used directly, no import |

---

## 1b. Editor Importer (`uz::Importer`)

The editor-time importer (`src/engine/editor/importer.h`) provides fast preview-quality imports during development. It does NOT perform compression (BC7, meshoptimizer LOD) — that is deferred to UzBuild offline.

### Supported Operations

| Source | Output Dir | Sidecar Settings |
|--------|-----------|-----------------|
| `.glb` / `.gltf` | `.import/meshes/` | `scale`, `generate_normals`, `generate_tangents` |
| `.png` | `.import/textures/` | `width`, `height`, `channels`, `srgb`, `generate_mipmaps` |

### Usage

```cpp
uz::Importer importer("/path/to/project");
auto result = importer.import_asset("assets/hero.glb", uz::AssetType::Mesh);
if (result.success) {
    // result.output_path → ".import/meshes/hero.glb"
    // Sidecar at ".import/meshes/hero.glb.import.json"
}
```

### PNG Header Parsing

`Importer::read_png_header()` extracts width, height, and channel count from the first 33 bytes of a PNG file (the IHDR chunk). Color type mapping:

| PNG Color Type | Value | Channels |
|---------------|-------|----------|
| Greyscale | 0 | 1 |
| RGB | 2 | 3 |
| Indexed | 3 | 3 |
| Greyscale+Alpha | 4 | 2 |
| RGBA | 6 | 4 |

### Sidecar Format (Mesh)

```json
{
  "source": "/absolute/path/to/mesh.glb",
  "type": "Mesh",
  "settings": {
    "scale": 1.0,
    "generate_normals": true,
    "generate_tangents": true
  }
}
```

### Sidecar Format (Texture)

```json
{
  "source": "/absolute/path/to/texture.png",
  "type": "Texture",
  "settings": {
    "width": 1024,
    "height": 1024,
    "channels": 4,
    "srgb": true,
    "generate_mipmaps": true
  }
}
```

### Atomic Writes

Sidecars are written atomically: data is written to a `.tmp` file first, then renamed to the final `.import.json` path. This prevents corrupt sidecars from a crash during write.

---

## 2. FBX Import (ufbx)

Library: **ufbx** (MIT/public domain) — two-file, C99/C++11, 5–20× faster than Autodesk FBX SDK. Adopted by Blender in 2025.

### Import Pipeline

```
.fbx file
    ↓
ufbx_load_file()
    ├── Coordinate system normalization (Y-up right-hand, 1 unit = 1 cm)
    ├── Extract meshes → interleaved vertex buffer (pos, normal, tangent, uv0)
    ├── Extract skeleton (bone hierarchy + bind pose)
    ├── Extract skin weights (up to 4 influences per vertex)
    ├── Extract morph targets (blend shapes)
    ├── Extract materials → .uzmat generation
    ├── ufbx_bake_anim() → baked keyframe tracks
    └── meshoptimizer LOD generation (3 levels)
    ↓
.uzmesh binary (vertices + indices + LODs + material slots)
.uzskel text   (bone hierarchy, bind pose)
.uzanim binary (baked animation keyframes)
.uzmat text    (per-material file)
.import.json   (import manifest with hash, settings, outputs)
```

### FBX vs GLTF

Use **ufbx** for `.fbx` and **fastgltf** for `.glb`/`.gltf`. Both normalize to the same internal representation. FBX covers features GLTF lacks:

| Feature | FBX | GLTF 2.0 |
|---------|-----|---------|
| Multiple animation takes in one file | ✓ | ✗ |
| Pre/post rotation compensation pivots | ✓ | ✗ |
| Constraint objects (IK, orient, aim) | ✓ | ✗ |
| PBR metallic-roughness material | ✗ | ✓ (native) |
| Draco mesh compression | ✗ | ✓ (extension) |

### Import Settings (`.import.json`)

```json
{
  "source":    "assets/meshes/character.fbx",
  "type":      "SkeletalMesh",
  "hash":      "sha256:a3f4...",
  "imported":  "2026-03-01T22:00:00Z",
  "settings": {
    "scale":            0.01,
    "up_axis":          "Y",
    "generate_lods":    true,
    "lod_ratios":       [1.0, 0.5, 0.25, 0.1],
    "lod_errors":       [0.0, 0.01, 0.05, 0.15],
    "auto_lod":         true,
    "lod_screen_sizes": [0.50, 0.25, 0.10, 0.02],
    "nanite_fallback":  false,
    "nanite_threshold_triangles": 100000,
    "max_bone_influences": 4,
    "import_animations": true,
    "animation_bake_fps": 30
  },
  "outputs": [
    "assets/.import/character.uzmesh",
    "assets/.import/character.uzskel",
    "assets/.import/character_run.uzanim",
    "assets/.import/character_idle.uzanim"
  ]
}
```

---

## 3. LOD Generation (meshoptimizer v1.0)

**Library**: meshoptimizer (MIT, vcpkg) — industry standard, used in Blender, O3DE, Unreal.

### Standard LOD Chain

```cpp
// Generate 4 LOD levels
float ratios[] = { 1.0f, 0.5f, 0.25f, 0.10f };
float errors[] = { 0.0f, 0.01f, 0.05f, 0.15f };

for (int i = 1; i < 4; i++) {
    size_t target = (size_t)(base_index_count * ratios[i]);
    float result_error;
    lod_indices.resize(base_index_count);
    size_t count = meshopt_simplify(
        lod_indices.data(), base_indices, base_index_count,
        positions, vertex_count, sizeof(float)*3,
        target, errors[i], 0, &result_error);
    lod_indices.resize(count);
    meshopt_optimizeVertexCache(lod_indices.data(), lod_indices.data(), count, vertex_count);
}
```

### Nanite-style Cluster LOD (Phase 4+)

meshoptimizer v1.0 ships `clusterlod.h` — a cluster DAG for continuous LOD (same architecture as UE5 Nanite). Phase 4 milestone. For now, standard 4-level LOD is sufficient.

### LOD Distance Switching

LOD selected by projected screen coverage (not world distance):

```
screen_coverage = mesh_radius_world / (distance * tan(fov * 0.5))
if   coverage > 0.50  → LOD0
elif coverage > 0.20  → LOD1
elif coverage > 0.05  → LOD2
else                  → LOD3 (or cull)
```

---

## 4. Texture Compression (BC7 / BC5 / BC6H)

**Library**: DirectXTex (MIT, vcpkg) for DDS I/O and mip generation + **bc7enc_rdo** for fast BC7 encoding with RDO (better PAK compression ratios).

### sRGB Auto-Detection

The importer automatically determines the correct sRGB setting by matching the source filename (case-insensitive) against known texture-type patterns. This prevents the common bug where roughness/normal maps are incorrectly marked sRGB, which destroys PBR lighting values.

**sRGB = TRUE** (gamma-encoded color data): patterns `albedo`, `diffuse`, `basecolor`, `base_color`, `col`, `color`, `_d` (suffix).

**sRGB = FALSE** (linear data, must not be gamma-expanded): patterns `normal`, `nrm`, `nor`, `normalmap`, `bump`, `roughness`, `rough`, `rgh`, `metallic`, `metal`, `met`, `ao`, `ambient_occlusion`, `occlusion`, `cavity`, `height`, `displacement`, `disp`, `emissive`, `emit`, `glow`, `mask`, `opacity`, `alpha`, `packed`, `orm`, `rma`.

When no pattern matches, the importer defaults to sRGB=true and records `srgb_source: "fallback"` in `.import.json`. A VSCode linter warning is emitted for all fallback-detected textures so artists can audit and confirm. Explicit override: set `srgb: false` and `srgb_source: "user_override"` in `.import.json`.

Substance Painter convention mapping (auto-detected by filename patterns above):

| Substance Painter Suffix | sRGB | Format |
|--------------------------|------|--------|
| `_BaseColor` | YES | BC7 UNORM_SRGB |
| `_Normal` | NO | BC5 UNORM |
| `_Roughness` | NO | BC4 UNORM |
| `_Metallic` | NO | BC4 UNORM |
| `_AmbientOcclusion` | NO | BC4 UNORM |
| `_Emissive` | NO | BC7 UNORM |
| `_Opacity` | NO | BC4 UNORM |
| `_Height` | NO | BC4 UNORM |

Additionally, the importer scans the alpha channel. If all pixels have alpha=255, `has_alpha: false` is recorded in `.import.json`, allowing the compressor to skip the alpha channel and use a more efficient format.

### Compression Rules

| Texture Type | Format | Why |
|-------------|--------|-----|
| Albedo / diffuse (sRGB) | BC7 `UNORM_SRGB` | Best quality, 4-channel |
| Normal map (tangent-space) | BC5 `UNORM` (RG only) | Z reconstructed in shader: `z = sqrt(1 - r² - g²)` |
| Roughness / Metallic (1-channel) | BC4 `UNORM` | 6:1 ratio, single channel |
| HDR Skybox / IBL | BC6H `UF16` | Half-float HDR |
| Emissive / Mask | BC7 `UNORM` | General RGBA |

### Mip Generation

All textures generate full mip chains (down to 1×1). Count: `floor(log2(max(width, height))) + 1`.

Filter: `TEX_FILTER_BOX` for albedo, `TEX_FILTER_LINEAR` for normal maps (preserves small normal details better than box).

### Cross-Platform (Future)

When targeting Android/iOS: transcode BC7 → ASTC 4×4 at runtime via **Basis Universal / KTX-Software**. No change to asset pipeline — KTX2 container wraps the Basis intermediate at build time.

---

## 5. Binary Asset Formats

### `.uzmesh` Binary Format

```
Header (64 bytes):
  magic          uint32  = 0x4D45535A  ('ZMESH' reversed: 'ZSEM')
  version        uint32  = 1
  vertex_count   uint32
  index_count    uint32
  lod_count      uint32  (1–4)
  submesh_count  uint32
  flags          uint32  (bit0=skinned, bit1=morphs, bit2=has_skeleton)
  skeleton_path  char[32] (res:// relative path, zero-terminated, or empty)

Vertex data: vertex_count * stride bytes
  Static:  float3 pos, float3 normal, float4 tangent, float2 uv0
  Skinned: + uint8x4 bone_indices, uint8x4 bone_weights (normalized 0..255)

Index data: index_count * 4 bytes (uint32)

LOD table: lod_count * 16 bytes
  For each LOD: index_offset(uint32), index_count(uint32), screen_coverage_threshold(float), reserved(uint32)

Submesh table: submesh_count * 32 bytes
  For each submesh: index_offset(uint32), index_count(uint32), material_slot(uint32), reserved(20 bytes)
```

### `.uzskel` Text Format (Human/AI Readable)

```ini
[uzskeleton version=1 bone_count=67]

[bone id=0 name=pelvis parent=-1]
bind_translation = [0.0, 0.0, 95.0]
bind_rotation    = [0.0, 0.0, 0.0, 1.0]
bind_scale       = [1.0, 1.0, 1.0]

[bone id=1 name=spine_01 parent=0]
bind_translation = [0.0, 0.0, 10.5]
bind_rotation    = [0.0, 0.0, 0.0, 1.0]
bind_scale       = [1.0, 1.0, 1.0]
```

### `.uzanim` Binary Format

```
Header:
  magic           uint32
  version         uint32
  duration        float   (seconds)
  ticks_per_sec   float
  bone_channel_count uint32
  notify_count    uint32
  flags           uint32  (bit0=looping, bit1=root_motion, bit2=additive)

Per BoneChannel:
  bone_index      uint16
  pos_key_count   uint16
  rot_key_count   uint16
  scl_key_count   uint16
  [pos keys: float time + float3 value] * pos_key_count
  [rot keys: float time + float4 quat]  * rot_key_count
  [scl keys: float time + float3 value] * scl_key_count

Per AnimNotify:
  time            float
  event_name      char[32]
  params_json     char[128]
```

---

## 6. Asset PAK Format (`.uzpak`)

### Design Goals

- **Memory-mappable**: entire file can be `MapViewOfFile`'d; directory at end (footer), no seek for finding a file
- **Per-file zstd compression**: each asset compressed independently for random access
- **Hash-indexed**: lookup by `xxhash3(path)` in O(log N) time (sorted by hash)
- **Patchable**: new PAK with higher priority can override entries

### File Layout

```
┌──────────────────────────────────────────────┐
│  FILE DATA SECTION                           │
│  [zstd-compressed asset 0]                   │
│  [zstd-compressed asset 1]                   │
│  ...                                         │
├──────────────────────────────────────────────┤
│  DIRECTORY TABLE                             │
│  entry_count (uint32)                        │
│  For each entry (32 bytes, sorted by hash):  │
│    path_hash       uint64                    │
│    offset          uint64                    │
│    compressed_size uint32                    │
│    raw_size        uint32                    │
│    asset_type      uint16                    │
│    flags           uint16 (compression etc)  │
├──────────────────────────────────────────────┤
│  FOOTER (32 bytes)                           │
│    directory_offset  uint64                  │
│    directory_size    uint32                  │
│    entry_count       uint32                  │
│    version           uint32                  │
│    magic             uint32 = 0x4B50555A     │
└──────────────────────────────────────────────┘
```

### C++ Reader

```cpp
// Memory-map the PAK
HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, ...);
HANDLE m = CreateFileMappingW(h, nullptr, PAGE_READONLY, 0, 0, nullptr);
auto* base = (uint8_t*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);

// Read footer from end of file
auto* footer = (UzPakFooter*)(base + file_size - sizeof(UzPakFooter));
assert(footer->magic == 0x4B50555A);

// Read directory
auto* dir = (UzPakEntry*)(base + footer->directory_offset);

// Lookup by hash
uint64_t hash = xxhash3_string("textures/hero_diffuse.dds");
auto* entry = std::lower_bound(dir, dir + footer->entry_count, hash,
    [](const UzPakEntry& e, uint64_t h){ return e.path_hash < h; });

// Decompress
ZSTD_decompress(out, entry->raw_size, base + entry->offset, entry->compressed_size);
```

### Virtual Filesystem

Multiple PAKs can be mounted. Lookup order: highest priority first. Enables base PAK + patch PAK:

```cpp
VirtualFS vfs;
vfs.mount("base.uzpak",  priority=0);
vfs.mount("patch1.uzpak", priority=1);  // overrides base entries

// Load any asset
auto data = vfs.load("textures/hero_diffuse.dds");
```

---

## 7. Texture Preview Tool (VSCode Editor Panel)

Exposed as a custom editor for `.dds` / `.png` / `.ktx2` files:

```
Texture Preview Panel
┌───────────────────────────────────────────────────┐
│  [R] [G] [B] [A] [RGB]    Mip: [▼ 0]   Zoom: 1x  │
├───────────────────────────────────────────────────┤
│                                                   │
│   [texture displayed on canvas via WebGL]         │
│                                                   │
│   checkerboard alpha pattern background           │
├───────────────────────────────────────────────────┤
│  Format: BC7_UNORM  |  1024×1024  |  11 mips      │
│  Size: 2.7 MB raw, 0.5 MB BC7    |  sRGB: yes     │
└───────────────────────────────────────────────────┘
```

Implemented as a VSCode `CustomEditorProvider`:
- Registered for `.dds`, `.png`, `.ktx2` extensions
- Webview uses WebGL to decode+display the texture
- Channel isolation shader: R/G/B/A/RGB buttons
- Mip selector: shows individual mip levels
- Alpha checkerboard background
- Calls `asset.getInfo` IPC for metadata display

---

## 8. Model Preview (VSCode Editor Panel)

Preview for `.fbx`, `.glb`, `.uzmesh` files:

```
Model Preview Panel
┌────────────────────────────────────────────────────┐
│  [Wireframe] [Solid] [UV] [Bones] [Normals]         │
├────────────────────────────────────────────────────┤
│                                                    │
│   [3D viewport — orbit camera, live JPEG stream]   │
│   (engine renders the model in isolation)          │
│                                                    │
├────────────────────────────────────────────────────┤
│  LOD: [▼ Auto]   Triangles: 12,480 → 6,240 → 3,120 │
│  Bones: 67   Materials: 2   Animations: run/idle   │
└────────────────────────────────────────────────────┘
```

Implementation:
- Calls `asset.preview` IPC — engine loads the mesh in an isolated preview scene
- Returns JPEG stream showing the model with a turntable orbit camera
- LOD selector, wireframe/solid toggle, bone display overlay
- Animation playback controls if the mesh has animations

---

## 9. IPC Handlers (asset.*)

```
asset.list         { type? }             → [{ path, type, status, size }]
asset.import       { path, settings? }   → { ok, errors?, outputs }
asset.reimport     { path }              → { ok, errors? }
asset.getInfo      { path }              → .import.json contents
asset.preview      { path }              → engine loads model in preview scene
asset.getThumb     { path }              → 128×128 JPEG thumbnail
asset.createMaterial { name, shader? }  → creates .uzmat, returns path
asset.createScene  { name }             → creates .uzscene, returns path
asset.createShader { name, type? }      → creates .hlsl, returns path
asset.pack         { output, paths[] }  → builds .uzpak from listed assets
```

---

## 10. Source Asset Database

All source assets are tracked in a SQLite database (`{project_dir}/.uz/asset_db.sqlite`) with the following schema:

```sql
CREATE TABLE assets (
    id           TEXT PRIMARY KEY,   -- content-addressable SHA-256 of source
    source_path  TEXT NOT NULL,
    output_path  TEXT NOT NULL,
    importer     TEXT NOT NULL,
    cook_key     TEXT NOT NULL,      -- hash(source) + hash(settings) + importer_version
    last_cooked  INTEGER,            -- Unix timestamp
    status       TEXT                -- ok | error | stale
);

CREATE TABLE dependencies (
    asset_id     TEXT REFERENCES assets(id),
    depends_on   TEXT REFERENCES assets(id)
);
```

The database enables:
- **Incremental import**: only re-cook if `cook_key` changes
- **Dependency tracking**: changing a texture triggers re-cook of all materials that reference it
- **Asset search**: `SELECT * FROM assets WHERE source_path LIKE '%.fbx'`
- **Budget reporting**: `SELECT SUM(output_size) FROM assets GROUP BY importer`

---

## 11. Asset Dependency Graph

The dependency graph tracks which assets reference which other assets:

```
PlayerMesh.fbx
  └── PlayerSkin.png   (texture reference in FBX material)
  └── PlayerNormal.png
  └── PlayerRig.uzanim
        └── Walk.uzanim
        └── Run.uzanim
```

When `PlayerSkin.png` changes:
1. DB query finds all assets with `depends_on = PlayerSkin.png`
2. All dependent assets are marked `stale`
3. Next cook processes only stale assets in dependency order (topological sort)

---

## 12. Incremental Import — Fine-Grained

`CookKey` combines all inputs that affect the output:

```cpp
CookKey = SHA256(
    source_file_content +
    importer_version +
    serialized_import_settings +
    engine_version
)
```

This means:
- Changing import settings (e.g., toggle normal map compression) re-cooks only that asset
- Upgrading the importer version re-cooks all assets of that type
- Unchanged assets are never re-cooked regardless of modification time

---

## 13. Asset Redirectors

When an asset is moved or renamed, a tombstone record is written to the database:

```sql
CREATE TABLE redirectors (
    old_path TEXT PRIMARY KEY,
    new_path TEXT NOT NULL,
    created  INTEGER
);
```

At load time, if an asset path is not found, the redirector table is checked. If a redirect exists, the new path is used transparently and a warning is logged. Stale redirectors (pointing to deleted assets) are reported in the asset validation pass.

---

## 14. Runtime Async Streaming

Assets are loaded asynchronously using a handle-based API:

```cpp
// Request async load
AssetHandle<Mesh> handle = ResourceManager::load_async<Mesh>("res://meshes/player.uzpak#mesh0");

// Check state in game update
if (handle.state() == AssetState::Ready) {
    mesh_component.mesh = handle.get();
} else if (handle.state() == AssetState::Error) {
    LOG_ERROR("Failed to load mesh: {}", handle.error());
}
```

Lua:
```lua
local handle = ctx.assets:load_async("res://meshes/player.uzpak")
ctx:start_coroutine(function()
    ctx.coroutine.wait_until(function() return handle:is_ready() end)
    self:set_mesh(handle:get())
end)
```

IO threads: configurable via `.uzproj`:
```toml
[streaming]
io_thread_count = 2
max_in_flight_requests = 16
priority_levels = 3   ; high (HUD), normal (near geometry), low (distant LOD)
```

---

## 15. Cook Order and Circular Dependency Detection

Assets are cooked in topological order derived from the dependency graph. If a cycle is detected (A → B → A), an error is logged and the cycle is broken at the alphabetically-first edge. Cycle detection uses Kahn's algorithm on the dependency graph at cook start.
