# Module: Assets

**Location:** `src/engine/assets/`
**Output:** Part of `uz_engine.lib`; UzBuild.exe uses import tools
**Language:** C++23
**Role:** Asset import pipeline (glTF/FBX/PNG/WAV), ResourceManager, .uzpak build output.

---

## Responsibility

The Assets module manages the complete asset lifecycle:

1. **Import**: Convert source formats (glTF, FBX, PNG, WAV, OGG) to optimized engine formats
2. **Manage**: ResourceManager with LRU cache, hot-reload notifications, Handle<T> system
3. **Query**: res:// path system for asset identity (no GUIDs, all plain text)
4. **Package**: UzBuild.exe compiles all assets into single .uzpak distribution file

The module ensures assets are optimized (compressed, LOD-generated, GPU-ready) without coupling the engine to source format complexity.

---

## res:// Path System

All assets are referenced by **plain-text paths relative to project root**:

```
res://models/player.uzmesh
res://textures/pbr/metal.dds
res://audio/sfx/footstep.uzaudio
res://scenes/levels/level01.uzscene
```

**Never use GUIDs** — paths are human-readable, merge-friendly, AI-editable.

The ResourceManager resolves `res://` paths by mapping to `.import/` artifacts:

```
Source:   assets/player.glb
Artifact: .import/assets/player.uzmesh
```

---

## Source Structure

```
src/engine/assets/
├── resource_manager.h/cpp       — load<T>(path), Handle<T> system, LRU cache, hot-reload
├── resource_handle.h            — Handle<T>: 24-bit index + 8-bit generation, stale → nullptr
├── resource_types.h             — CMesh, CTexture, CAudioClip, CSkeletalMesh typedefs
├── formats/
│   ├── uz_mesh.h/cpp            — .uzmesh binary format read/write
│   ├── uz_mat.h/cpp             — .uzmat INI format read/write
│   ├── uz_audio.h/cpp           — .uzaudio binary format read/write
│   ├── uz_texture.h/cpp         — .dds binary format read/write
│   └── uz_scene.h/cpp           — .uzscene INI format read/write (parser)
└── importers/
    ├── mesh_importer.h/cpp      — glTF (fastgltf) + FBX (ufbx) → .uzmesh with LODs
    ├── texture_importer.h/cpp   — PNG/JPG/HDR → BC7/BC5/BC4/BC6H (ispc_texcomp) → .dds
    ├── audio_importer.h/cpp     — WAV/MP3/OGG → PCM16 → .uzaudio (or streaming ref)
    └── skeletal_importer.h/cpp  — FBX/glTF skeleton + skin → .uzskin (Phase 3)
```

---

## Import Artifact Rule

**Source artifacts are regenerated, never committed.**

```
.gitignore:
    .import/
    *.uzmesh
    *.uzaudio
    *.dds

Project layout:
assets/
├── player.glb              ← Source (committed)
├── player.png              ← Source (committed)
.import/
├── assets/player.uzmesh    ← Artifact (regenerated, not committed)
├── assets/player.dds       ← Artifact (regenerated, not committed)
```

---

## .import.json Sidecar Metadata

Each source file has a `.import.json` sidecar describing the import process:

```json
{
  "source": "assets/player.glb",
  "source_hash": "a1b2c3d4e5f6...",
  "importer_version": 2,
  "import_time": "2026-03-01T12:34:56Z",
  "settings": {
    "mesh_import": {
      "optimize": true,
      "generate_lods": true,
      "lod_count": 4
    }
  },
  "artifacts": [
    { "path": ".import/assets/player.uzmesh", "size": 2048576 }
  ]
}
```

**Re-import only when**:
- Source hash changes (file modified)
- Importer version changes (new importer code)
- Import settings change (user config update)

---

## Mesh Import Pipeline

glTF and FBX → optimized .uzmesh with automatic LOD generation:

```
Input: assets/player.glb (source glTF)
  ↓
glTF parser (fastgltf)
  ├─ Extract vertices, indices, bones, animations
  ├─ Validate normals, tangents (generate if missing)
  └─ Merge materials
  ↓
meshoptimizer (3 passes)
  ├─ Vertex cache optimization (reorder for GPU prefetch)
  ├─ Overdraw optimization (sort triangles)
  └─ Fetch optimization (compress index buffer)
  ↓
LOD generation (4 levels)
  ├─ Level 0 (100% detail) — full vertex count
  ├─ Level 1 (~70% vertices) — coarse details merged
  ├─ Level 2 (~40% vertices) — simplified shapes
  ├─ Level 3 (~20% vertices) — distant silhouette only
  └─ Store screen coverage thresholds (switch LOD at 30px, 15px, 8px width)
  ↓
Serialize to .uzmesh binary
  ├─ LOD 0-3 vertex/index buffers
  ├─ Bounding sphere
  ├─ Skeleton references (if skinned)
  └─ Animation clip metadata
  ↓
Output: .import/assets/player.uzmesh
```

---

## Texture Import Pipeline

PNG/JPG/HDR → BC-compressed .dds (DirectDraw Surface):

```
Input: assets/textures/metal_albedo.png
  ↓
Image decode (libpng, libjpeg-turbo)
  └─ Validate SRGB vs linear color space
  ↓
Mipmap generation (ispc_texcomp)
  └─ Progressive downsampling: 1024x1024 → 512 → 256 → 128 → 64 → 32 → 16 → 8 → 4 → 2 → 1
  ↓
Compression (BC-format selection)
  ├─ Color (Albedo, Emissive) → BC7 (RGB + quality)
  ├─ Normals (Normal maps) → BC5 (RG normal XY)
  ├─ Grayscale (AO, Roughness, Metallic) → BC4 (single channel)
  ├─ HDR (Radiance) → BC6H (floating-point)
  └─ Encode with ispc_texcomp (GPU-accelerated)
  ↓
Serialize to .dds binary
  ├─ DDS header (format, mipmap count, dimensions)
  ├─ Compressed mipmap levels
  └─ Metadata (colorspace, compression format)
  ↓
Output: .import/assets/textures/metal_albedo.dds
```

Benefits:
- **BC7 compression**: ~6:1 ratio (RGB 24-bit → 4KB/mipmap)
- **GPU-native format**: No decompression overhead; upload directly to VRAM
- **Mipmaps pre-generated**: Sampling at distance is fast, no GPU-side generation lag

---

## Audio Import Pipeline

WAV/MP3/OGG → .uzaudio with optional streaming:

```
Input: assets/sfx/footstep.wav (or .mp3, .ogg)
  ↓
Decode to PCM16 linear (44.1 kHz, stereo)
  ├─ MP3/OGG: Use ffmpeg or libvorbis
  └─ WAV: Validate PCM16 format
  ↓
Size decision:
  ├─ < 2 MB: Load entirely into memory
  └─ ≥ 2 MB: Mark as streaming (reference .uzaudio, stream from disk)
  ↓
Serialize to .uzaudio binary
  ├─ Metadata: channels, sample rate, duration, streaming flag
  ├─ PCM16 data (if < 2 MB)
  └─ Streaming metadata (if ≥ 2 MB)
  ↓
Output: .import/assets/sfx/footstep.uzaudio
```

---

## ResourceManager

Runtime asset loading and caching:

```cpp
// Load mesh (synchronous)
ResourceHandle<CMesh> mesh_handle = resource_manager.load<CMesh>("res://models/player.uzmesh");

// Use via Handle<T> (auto-converts to pointer)
if (mesh_handle) {
    CMesh* mesh = mesh_handle.get();
    int vertex_count = mesh->vertex_count;
    // ...
}

// Handle<T> features:
// - Auto-null if resource freed
// - Prevents use-after-free
// - LRU cache keeps hot assets loaded
// - On hot-reload: notifies all Handle<T> holders
```

**Cache policy**: LRU (Least Recently Used) with configurable size limit:
```
Max size: 512 MB
When full: Evict least-recently-used asset
Hot assets stay loaded; unused assets auto-unload
```

---

## .uzpak Distribution Format

Single-file shipping format combining all assets:

```
.uzpak (zstd-compressed archive)
├─ manifest.json
│  ├─ File list (path, offset, size, hash)
│  └─ Metadata (version, build date, checksum)
├─ assets/
│  ├─ scenes/
│  │  ├─ level01.uzscene
│  │  └─ level02.uzscene
│  ├─ models/
│  │  ├─ player.uzmesh
│  │  └─ enemy.uzmesh
│  ├─ textures/
│  │  └─ pbr/
│  │      ├─ metal.dds
│  │      └─ wood.dds
│  └─ audio/
│      └─ sfx/
│          └─ footstep.uzaudio
└─ shaders/
   ├─ pbr.hlsl.compiled
   └─ ui.hlsl.compiled
```

**Built by UzBuild.exe**:
1. Import all source assets
2. Compile all shaders with DXC
3. Zstd-compress entire tree
4. Output single `game.uzpak` (typically 50-200 MB)
5. Ship with game binary; game.exe mounts .uzpak at startup

---

## Hot-Reload in Editor

File save → auto-re-import → instant viewport update:

```
1. Editor saves assets/player.glb
2. FileWatcher emits on_file_changed event
3. AssetManager::on_file_changed()
4. MeshImporter::import() runs in background
5. Generates .import/assets/player.uzmesh
6. ResourceManager notifies all Handle<CMesh> holders
7. Renderer receives reload notification
8. Viewport re-renders with new mesh
9. Developer sees change instantly (< 200ms)
```

---

## Using Assets in .uzscene

Scene files reference assets by res:// path:

```ini
[Node]
type = MeshNode
name = Player
mesh = res://models/player.uzmesh
material = res://materials/player.uzmat

[Node.Physics]
type = RigidBody
shape = res://physics/player_shape.jlt
```

When scene loads, ResourceManager loads all referenced assets on-demand.

---

## Using Assets in Lua

Lua scripts can load assets at runtime:

```lua
function M.on_start(self, ctx)
    -- Load mesh
    local mesh = ctx.assets:load_mesh("res://models/explosion.uzmesh")

    -- Load texture
    local texture = ctx.assets:load_texture("res://textures/fire.dds")

    -- Load audio
    local sound = ctx.assets:load_audio("res://audio/sfx/boom.uzaudio")

    -- All return Handle<T> — auto-null if unloaded
    if mesh then
        print("Mesh loaded, vertices:", mesh:vertex_count())
    end
end
```

---

## Phase Roadmap

**Phase 1**:
- ResourceManager with LRU cache
- res:// path system
- .uzmat (material) INI format
- Basic PNG import (no compression)

**Phase 2**:
- glTF import (fastgltf) with LOD generation (meshoptimizer)
- FBX import (ufbx)
- Texture compression (BC7/BC5/BC4, ispc_texcomp)
- PNG/JPG/HDR import
- Audio import (WAV/MP3/OGG)
- .uzpak build format

**Phase 3**:
- Skeletal mesh import (.uzskin)
- Animation clip import
- Physics shape import

**Phase 4**:
- Full UzBuild.exe pipeline (cook, pack, stage, package)
- Asset streaming (large meshes, audio)
- Asset bundles (grouping by level)

---

## Related Docs

- [Asset Pipeline Spec](../../specs/asset-pipeline.md) — Full design, format specifications, CLI tools
- [Build System Spec](../../specs/build-system.md) — UzBuild.exe, packaging, platform distribution
- [Guides: Asset Pipeline Guide](../../guides/asset-pipeline-guide.md) — Practical workflows, import settings, hot-reload
- [Engine: Hot-Reload](../../engine/hot-reload.md) — Editor-mode hot-reload mechanism
