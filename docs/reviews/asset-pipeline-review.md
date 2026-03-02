# Asset Pipeline — AAA Gap Analysis

**Author**: Asset Pipeline Engineer
**Date**: 2026-03-02
**Scope**: `docs/specs/asset-pipeline.md`, `skeletal-mesh.md`, `animation-system.md`, `terrain-system.md`, `build-system.md`, `docs/ROADMAP.md`
**Engine State Reviewed**: Phase 0 complete, Phase 1–4 in design

---

## Executive Summary

UzEngine's asset pipeline has a solid foundation: fastgltf/ufbx importers, meshoptimizer LOD generation, BC7/BC5/BC6H texture compression, zstd-packed `.uzpak` archives, and SHA-256 incremental cooking. These match the baseline of a mid-tier indie engine.

However, compared to the pipelines of Unreal Engine 5, Unity DOTS, O3DE, or id Tech 7, there are **24 critical gaps** spanning data architecture, runtime streaming, GPU memory management, collaborative authoring, and content validation. Several of these gaps are not cosmetic — they are architectural decisions that, if not corrected before Phase 2 ships, will require invasive refactors under production pressure.

The gaps are rated by complexity: **S** (1–3 days), **M** (1 week), **L** (2–3 weeks), **XL** (4+ weeks).

---

## Gap Index

| # | Feature | Priority | Complexity | Phase |
|---|---------|----------|------------|-------|
| 1 | Source Asset Database | CRITICAL | L | 2 |
| 2 | Asset Dependency Graph | CRITICAL | L | 2 |
| 3 | Incremental Import (Fine-Grained) | CRITICAL | M | 2 |
| 4 | Asset Redirectors | CRITICAL | M | 2 |
| 5 | Runtime Async Streaming | CRITICAL | XL | 3 |
| 6 | Virtual Texture Streaming (VT) | HIGH | XL | 4 |
| 7 | Mip Streaming with Distance Priority | HIGH | L | 3 |
| 8 | Asset Size Budget Enforcement | HIGH | M | 2 |
| 9 | Cross-Platform Asset Variants | HIGH | L | 3 |
| 10 | Texture Atlas Auto-Packing | HIGH | M | 3 |
| 11 | UDIM UV Tiling Support | MEDIUM | M | 3 |
| 12 | Material Instancing and Material Library | HIGH | M | 2 |
| 13 | LOD for Textures (Mip Bias Per Distance) | HIGH | M | 2 |
| 14 | Asset Tags and Metadata System | MEDIUM | S | 2 |
| 15 | Asset Validation Pipeline | HIGH | M | 2 |
| 16 | Procedural Asset Generation (PCG) | MEDIUM | XL | 4 |
| 17 | Collaborative Asset Locking | LOW | M | 4 |
| 18 | Source Control Integration (Git-LFS / Perforce) | MEDIUM | M | 3 |
| 19 | Asset Preview Thumbnails (Editor) | MEDIUM | M | 2 |
| 20 | Hot-Reload for Any Asset at Runtime | HIGH | L | 3 |
| 21 | Sprite Sheet Import and Animation Frames | MEDIUM | S | 2 |
| 22 | Audio Asset Processing Pipeline | HIGH | M | 2 |
| 23 | Video / Cutscene Asset Pipeline | LOW | L | 4 |
| 24 | Batch Processing and Distributed Cooking | MEDIUM | L | 3 |

---

## Detailed Gap Analysis

---

### 1. Source Asset Database (Asset Registry)

**Priority**: CRITICAL
**Complexity**: L
**Suggested Phase**: 2

#### Why It Is Critical

Every AAA engine — Unreal (AssetRegistry), Unity (AssetDatabase), O3DE (Asset Processor) — maintains a persistent, queryable database of all known assets. This database is the foundation for dependency tracking, search, thumbnail generation, batch reprocessing, and editor UI.

UzEngine currently has no such database. The `.import.json` sidecar approach stores metadata next to each source file, but there is no single authoritative record that answers questions like: "give me all meshes with more than 10,000 triangles", "list all textures that depend on `hero_albedo.png`", "which assets were imported in the last 24 hours?"

Without a database, every editor query requires a filesystem scan — O(N) where N is the number of project files. At 10,000 assets (a small AAA project), this becomes 10–30 seconds per query.

#### Technical Description

The Source Asset Database should be a persistent SQLite database (`assets/.assetdb`) built and maintained by the import pipeline. It stores:

```sql
CREATE TABLE assets (
    id          INTEGER PRIMARY KEY,
    res_path    TEXT NOT NULL UNIQUE,   -- res://textures/hero_albedo.png
    asset_type  TEXT NOT NULL,          -- "texture", "mesh", "audio", "anim"
    source_hash TEXT NOT NULL,          -- SHA-256 of source bytes
    output_path TEXT NOT NULL,          -- .import/textures/hero_albedo.dds
    import_time INTEGER NOT NULL,       -- Unix timestamp
    file_size   INTEGER NOT NULL,       -- source file bytes
    output_size INTEGER NOT NULL,       -- cooked artifact bytes
    metadata    TEXT                    -- JSON blob: width, height, format, poly_count, etc.
);

CREATE TABLE dependencies (
    from_asset  INTEGER REFERENCES assets(id),
    to_asset    INTEGER REFERENCES assets(id),
    dep_type    TEXT    -- "texture_ref", "material_ref", "mesh_ref", "anim_ref"
);

CREATE TABLE tags (
    asset_id    INTEGER REFERENCES assets(id),
    tag         TEXT NOT NULL
);

CREATE INDEX idx_asset_type ON assets(asset_type);
CREATE INDEX idx_dep_from   ON dependencies(from_asset);
CREATE INDEX idx_tag_name   ON tags(tag);
```

The IPC layer exposes SQL-like queries:

```
asset.query { type, tags, min_size, max_poly_count, ... } → [asset_records]
asset.getByHash { hash }                                  → asset_record | null
asset.getDependents { res_path }                          → [asset_records]
```

#### Impact if Absent

- Editor Asset Browser is either empty (no search) or slow (filesystem scan on every keystroke)
- "Find all assets that reference this texture" requires scanning every `.uzmat` file manually
- Batch reprocessing requires re-parsing all source files to detect what changed
- No foundation for thumbnail caching, LOD reporting, or size budget enforcement

#### Dependencies

None — can be implemented before other Phase 2 work begins.

---

### 2. Asset Dependency Graph

**Priority**: CRITICAL
**Complexity**: L
**Suggested Phase**: 2

#### Why It Is Critical

In AAA engines, assets form a directed acyclic graph: a `.uzscene` references `.uzmat` files, which reference `.dds` textures and compiled shaders; a `.uzmesh` references `.uzskel`; a `.uzskel` is referenced by multiple `.uzanim` files. Without tracking this graph explicitly, the following operations are impossible:

- **Incremental cooking**: if `hero_albedo.png` changes, which `.uzmat` files need to be re-evaluated? Which shader permutations? Which PAK files need partial rebuild?
- **Reference integrity**: renaming `hero_albedo.png` to `character_albedo.png` silently breaks all `.uzmat` files that reference it unless a redirector is in place.
- **Shipping PAK construction**: cannot determine the minimal set of assets required for a given scene — results in shipping the entire project or writing manual inclusion lists.
- **Memory budgeting**: cannot report "this scene will load X MB of textures because it pulls in these N assets."

#### Technical Description

The dependency graph is built during the import step. Each importer, when parsing a source file, records all `res://` references it encounters:

```cpp
// In MeshImporter::import():
for (auto& material : gltf_materials) {
    if (!material.base_color_texture.empty()) {
        db.record_dependency(
            source_path,           // from: res://meshes/hero.glb
            material.base_color_texture,  // to: res://textures/hero_albedo.png
            "texture_ref"
        );
    }
}

// In SceneImporter::import():
for (auto& node : scene_nodes) {
    if (node.has_mesh())
        db.record_dependency(scene_path, node.mesh_path, "mesh_ref");
    if (node.has_material())
        db.record_dependency(scene_path, node.material_path, "material_ref");
}
```

The graph must support:
- Forward traversal: "what does this asset depend on?" (for PAK inclusion)
- Reverse traversal: "what depends on this asset?" (for incremental cook invalidation)
- Cycle detection (asset A references B which references A — should be a cook error)
- Transitive closure: "if I load this scene, what is the complete set of assets?"

Graph queries drive selective PAK building:

```bash
# Build a PAK containing only assets reachable from level_01.uzscene
uzengine pack --entry-scene=res://scenes/level_01.uzscene --output=level01.uzpak
```

#### Impact if Absent

- Every PAK build must include all project assets — no per-level streaming or DLC splitting
- Renaming a texture breaks all references silently
- Incremental cook invalidation is based only on direct source hash, not transitive changes
- No foundation for memory budget reporting

#### Dependencies

Gap 1 (Source Asset Database) — the dependency graph is stored in the `dependencies` table.

---

### 3. Incremental Import — Fine-Grained Invalidation

**Priority**: CRITICAL
**Complexity**: M
**Suggested Phase**: 2

#### Why It Is Critical

The current spec uses SHA-256 of the source file to decide whether to re-import. This is correct for single-output imports (one PNG → one DDS). However, it fails for multi-output imports and for settings-driven invalidation:

1. **FBX → multiple outputs**: `hero.fbx` produces `hero.uzmesh`, `hero.uzskel`, `hero_run.uzanim`, `hero_idle.uzanim`. If only the animation data inside the FBX changes, the mesh and skeleton should not be reimported. The current design reimports everything from the FBX unconditionally when the file hash changes.

2. **Settings change invalidation**: if the user changes `lod_count` from 4 to 3 in `project.uzproj`, all meshes must be re-cooked even if no source files changed. The current `.import.json` records settings, but there is no mechanism to detect when project-level cook settings have changed.

3. **Transitive invalidation**: if `hero_albedo.png` changes and `hero.uzmat` references it, the material's cooked output is unchanged (it's a text file), but the DDS needs regeneration and the PAK entry for that DDS needs updating. The current build system does not propagate invalidation through the dependency graph.

4. **Output existence check**: the current system checks hash match AND output exists. However, if the cook produced a corrupt or truncated output (disk full, power loss), the next cook will not re-run because the hash matches. There is no output integrity verification (e.g., checking the output magic bytes).

#### Technical Description

Fine-grained invalidation requires:

```cpp
struct CookKey {
    std::string source_hash;        // SHA-256 of source file
    std::string settings_hash;      // SHA-256 of relevant cook settings JSON
    std::string importer_version;   // "mesh_importer_v3" — bump on logic change
};

bool needs_recook(const std::string& res_path, const CookKey& key) {
    auto record = db.get_cook_record(res_path);
    if (!record) return true;
    if (record->cook_key != key) return true;
    // Verify output integrity
    if (!verify_output_magic(record->output_path)) return true;
    return false;
}
```

The `importer_version` field is critical: when the mesh importer logic changes (e.g., meshoptimizer is updated from 0.21 to 0.22), all meshes must be re-imported. Bumping the version string automatically triggers a full recook without needing `--force`.

#### Impact if Absent

- Artist changes one animation in an FBX → entire character mesh, skeleton, and all animations are reimported (10–60 seconds per character, multiplied by 20 characters = 20+ minutes wasted)
- Project cook settings change → no assets are recoooked (stale artifacts used in shipping)
- Corrupt output artifacts silently shipped

#### Dependencies

Gap 1 (Source Asset Database) for storing cook records; Gap 2 (Dependency Graph) for transitive invalidation.

---

### 4. Asset Redirectors

**Priority**: CRITICAL
**Complexity**: M
**Suggested Phase**: 2

#### Why It Is Critical

In any non-trivial project, assets get renamed, moved between directories, and reorganized. In Unreal Engine, a "redirector" is a tombstone record that maps an old asset path to its new location. Without redirectors:

- Renaming `res://textures/hero_albedo.png` to `res://characters/hero/albedo.png` immediately breaks every `.uzmat` file that references the old path.
- Scenes stop loading.
- The error message is "asset not found" with no hint of the new location.
- In a team of 5 artists, every rename triggers a manual find-and-replace across all text files.

At AAA scale (hundreds of thousands of assets), this is not a workflow inconvenience — it is a correctness problem. Teams that cannot safely rename assets stop reorganizing their project, leading to chaos (the "assets dump" anti-pattern where everything lives in one flat directory).

#### Technical Description

A redirector is a record in the Asset Database mapping old path → new path:

```sql
CREATE TABLE redirectors (
    old_path    TEXT NOT NULL UNIQUE,   -- res://textures/hero_albedo.png
    new_path    TEXT NOT NULL,          -- res://characters/hero/albedo.png
    created_at  INTEGER NOT NULL,
    reason      TEXT                    -- "renamed by artist JohnDoe on 2026-03-02"
);
```

The ResourceManager resolves redirectors transparently:

```cpp
std::string ResourceManager::resolve_path(const std::string& res_path) {
    // Follow redirector chain (max depth 3 to detect cycles)
    std::string current = res_path;
    for (int i = 0; i < 3; ++i) {
        auto redir = db.get_redirector(current);
        if (!redir) break;
        LOG_WARN("Following redirector: {} → {}", current, redir->new_path);
        current = redir->new_path;
    }
    return current;
}
```

The editor's rename operation in Asset Browser:
1. Physically moves/renames the source file
2. Writes the redirector record to the database
3. Offers to "fix up references" — scans all text assets and replaces the old path
4. After fixup, marks the redirector as "pending deletion" (retained for one major version for backward compatibility with external references)

#### Impact if Absent

- Renaming any asset breaks all references silently at runtime
- Teams cannot reorganize folder structure without manual find-and-replace
- External tools (content pipelines, CI/CD) that reference assets by path break on any rename
- DLC / patch PAKs that reference assets from base game by path break if base game reorganized

#### Dependencies

Gap 1 (Source Asset Database) for the redirector table; Gap 2 (Dependency Graph) for the "fix up references" scan.

---

### 5. Runtime Async Asset Streaming

**Priority**: CRITICAL
**Complexity**: XL
**Suggested Phase**: 3

#### Why It Is Critical

The current spec loads all assets synchronously at scene load time. This is architecturally incompatible with open-world games, streaming levels, or any title where:
- The world is larger than GPU VRAM (even modest open worlds exceed 4 GB of textures)
- Level transitions must be seamless (no loading screen)
- Assets must be loaded based on player position (far-away objects don't need full-res textures)

Unreal's `UStreamingManager` / `FAsyncTask`, Unity's `Addressables`, O3DE's `AZ::IO::Streamer` — all AAA engines invest heavily in async I/O infrastructure. UzEngine's ResourceManager has no async load path.

#### Technical Description

The streaming system requires three cooperating components:

**1. IO Thread Pool**

A dedicated thread pool (2–4 threads) for disk reads, separate from the game thread:

```cpp
class AsyncIOQueue {
public:
    struct Request {
        std::string      path;
        uint32_t         priority;   // 0=highest (visible, near), 255=lowest (preload)
        LoadCallback     callback;   // called on main thread when complete
    };

    void enqueue(Request req);      // thread-safe
    void pump_completions();        // called on main thread each frame
private:
    std::priority_queue<Request> queue_;
    std::mutex mutex_;
    std::counting_semaphore worker_sem_{0};
    std::vector<std::thread> workers_;
};
```

**2. Handle-Based Loading States**

Resource handles must carry load state:

```cpp
enum class LoadState { Unloaded, Pending, Streaming, Loaded, Failed };

template<typename T>
struct ResourceHandle {
    uint32_t  index;
    uint32_t  gen;
    LoadState state;     // atomic — readable from any thread
};
```

The game code queries state without blocking:

```cpp
auto mesh_handle = resource_manager.load_mesh_async("res://meshes/building.uzmesh", priority=64);
// Next frame:
if (resource_manager.get_state(mesh_handle) == LoadState::Loaded) {
    // safe to call resource_manager.get(mesh_handle)
}
```

**3. Placeholder / Fallback Resources**

While a high-LOD mesh streams in, render its LOD3 placeholder (a low-poly version always kept in memory) or a white sphere. Never render nothing — that causes visible pop-in.

```cpp
// Renderer queries the mesh, gets placeholder if not loaded yet
Mesh* mesh = resource_manager.get_or_placeholder(mesh_handle);
```

**Priority System**

Streaming priority is computed per asset per frame:

```
priority = f(distance_to_camera, asset_type, last_request_time)
```

Nearby, visible, foreground assets = highest priority. Background ambient audio = low priority. Pre-cache prefetch = lowest priority.

#### Impact if Absent

- All level assets must fit in RAM simultaneously — limits world size to ~2–4 GB
- Level transitions require a loading screen (halts gameplay)
- No streaming open-world possible
- Memory spikes on level load cause hitching on all platforms

#### Dependencies

Gap 1 (Asset Database) for lookup; Gap 2 (Dependency Graph) for preloading reachable assets; the `.uzpak` format already supports random access per entry (zstd per-file) so no format changes needed.

---

### 6. Virtual Texture Streaming (Sparse Virtual Textures)

**Priority**: HIGH
**Complexity**: XL
**Suggested Phase**: 4

#### Why It Is Critical

UE5's Virtual Shadow Maps and Virtual Textures, id Tech 7's MegaTexture, and Frostbite's Virtual Texture system all solve the same problem: terrain, buildings, and characters in AAA titles require texture resolutions (16K–32K per asset) that far exceed GPU VRAM. A 32K×32K texture at BC7 would be 512 MB — impossible to keep resident.

Virtual Texturing (VT) treats the physical GPU texture cache as a page table: only the mip tiles that are actually visible in the current frame are allocated in physical VRAM. The rest live on disk and stream in on demand.

UzEngine's terrain spec already notes this for Phase 5, but it is actually required in Phase 4 for the terrain system to scale beyond demo-quality resolution. A 1024×1024 meter terrain with 10 cm/texel resolution requires a 10K×10K splatmap — not viable without VT.

#### Technical Description

VT implementation has several discrete components:

**1. Virtual Texture Atlas**

A fixed-size physical texture cache in GPU VRAM (e.g., 4K×4K BC7 = 8 MB). Divided into 128×128 pixel pages. A page table texture (indirection texture) maps virtual UV coordinates to physical atlas coordinates.

**2. Feedback Buffer**

A GPU compute shader writes, for each rendered pixel, the virtual texture page it sampled. The feedback buffer is read back on the CPU (with one frame delay) to determine which pages are needed:

```hlsl
// In terrain pixel shader:
uint2 page_id = compute_vt_page(world_uv, mip_level);
feedback_uav[dispatch_id] = pack_page_request(page_id, mip_level);
```

**3. Page Uploader**

The CPU processes the feedback buffer, determines which pages are missing from the atlas, and streams them from disk into the atlas via a staging buffer.

**4. Asset Pipeline Side: Tiled DDS**

The texture importer must produce tiled DDS files where each page (128×128 pixels, per mip level) is independently addressable — not a standard linear DDS layout. This requires changes to the texture importer.

```cpp
// Texture importer: when output format is VT
void write_tiled_dds(const TextureData& data, const std::string& output_path) {
    // Each mip level is divided into 128x128 tiles
    // Tiles are stored in a flat array at the end of the file
    // A tile index table maps (mip, tile_x, tile_y) → byte offset
}
```

#### Impact if Absent

- Terrain texture resolution limited to 4K–8K (visually noticeable up close)
- Large characters / vehicles cannot have 8K skin textures
- No foundation for decal system (decals require VT for seamless blending)
- Competing engines (UE5, Unity) support VT; absence is a competitive disadvantage

#### Dependencies

Gap 5 (Async Streaming) for the page upload IO path; Gap 7 (Mip Streaming) for the priority system; significant GPU architecture work.

---

### 7. Mip Streaming with Distance Priority

**Priority**: HIGH
**Complexity**: L
**Suggested Phase**: 3

#### Why It Is Critical

The current spec generates full mip chains for all textures and loads them entirely into GPU VRAM. In a scene with 500 unique textures at 2K average resolution (BC7 = ~2.7 MB each), the total resident texture memory is 1.35 GB — before meshes, shadow maps, or any render targets. At 4K textures, this becomes 5.4 GB, exceeding the VRAM of most consumer GPUs.

Mip streaming loads only the mip levels required for the current camera position. A texture 500m away only needs mip 5 (64×64); a texture 2m away needs mip 0 (full-res). This reduces resident texture memory by 60–80% in typical scenes.

Unreal Engine's texture streaming system is one of its most complex subsystems for this reason — it is a hard requirement for memory-constrained shipping.

#### Technical Description

**Mip streaming requires split DDS storage**: each mip level (or small groups of mip levels) is stored as a separate addressable region in the `.uzpak`, not as one contiguous blob.

The current `.uzpak` format stores each asset as a single compressed blob. For mip streaming, the asset importer must split DDS mips:

```
PAK entry: "textures/hero_albedo_mip0.dds"  → 4K mip (2.7 MB)
PAK entry: "textures/hero_albedo_mip1.dds"  → 2K mip (0.67 MB)
PAK entry: "textures/hero_albedo_mip2-N.dds" → 1K and smaller mips (0.22 MB) ← always resident
```

The "tail mips" (mip4 and below, ≤64px) are always loaded into a separate small texture atlas. The head mips (mip0–mip3) stream in/out based on camera distance.

The streaming manager computes a "wanted mip level" per texture per frame:

```cpp
int compute_wanted_mip(const Texture& tex, float distance_meters, float fov_rad) {
    // Texel density: how many texels cover one pixel at this distance
    float screen_size = (tex.width_meters / distance_meters) / tan(fov_rad * 0.5f);
    float wanted_mip = log2f(tex.width_pixels / (screen_size * screen_width));
    return std::clamp((int)wanted_mip, 0, tex.mip_count - 1);
}
```

#### Impact if Absent

- Resident texture memory is 3–10× higher than necessary
- VRAM exhaustion causes driver-forced eviction and stuttering on mid-range hardware
- Game cannot target GPUs with less than 8 GB VRAM
- No foundation for VT (Gap 6)

#### Dependencies

Gap 5 (Async Streaming) for the IO path; `.uzpak` format extension for split-mip storage.

---

### 8. Asset Size Budget Enforcement

**Priority**: HIGH
**Complexity**: M
**Suggested Phase**: 2

#### Why It Is Critical

Without budget enforcement, teams consistently ship levels that are 3–5× over memory budget. This is the most common cause of "game runs fine in development but crashes / hitches on target hardware." Unreal Engine has "Size Map" and "Memory Report" tools. id Software enforced hard budgets at cook time — assets that exceed budget fail the build.

#### Technical Description

Budget definitions live in `project.uzproj`:

```toml
[build.budgets]
# Per-asset type limits
max_texture_size_pixels = 4096      # fail cook if any texture > 4096x4096
max_mesh_poly_count     = 500000    # fail cook if any mesh > 500K triangles
max_audio_duration_sec  = 120       # stream rather than embed if > 2 min

# Per-level limits (sum of all assets referenced by a scene)
max_level_texture_memory_mb = 512   # warn if level pulls > 512 MB of textures
max_level_mesh_memory_mb    = 256
max_level_audio_memory_mb   = 64

# Per-asset size limits
max_single_asset_mb = 50            # error if any cooked artifact > 50 MB
```

The cook stage reports budget violations:

```
[BUDGET ERROR]   hero_4k.png → hero_4k.dds: 8192x8192 exceeds max_texture_size_pixels (4096)
[BUDGET WARN]    level_01.uzscene: texture memory = 687 MB (budget: 512 MB)
[BUDGET OK]      Total shipped size: 2.3 GB
```

Budget violations at `ERROR` level fail the cook (`exit code 1`), blocking CI/CD pipelines. `WARN` level is logged but does not fail. This distinction is configurable per budget rule.

The VSCode Build Panel shows a budget summary after every cook:

```
Asset Budget Report
Textures:  687 / 512 MB  [OVER BUDGET]
Meshes:    134 / 256 MB  [OK]
Audio:      42 / 64 MB   [OK]
```

#### Impact if Absent

- Shipped builds regularly exceed VRAM/RAM budgets on target hardware
- No CI/CD gate prevents budget regressions between check-ins
- Artists have no visibility into the memory cost of their assets until the build breaks

#### Dependencies

Gap 1 (Asset Database) for aggregating memory usage per scene via the dependency graph; Gap 2 (Dependency Graph).

---

### 9. Cross-Platform Asset Variants

**Priority**: HIGH
**Complexity**: L
**Suggested Phase**: 3

#### Why It Is Critical

The build spec mentions Android as a Phase 5 target, but the asset pipeline has no mechanism for platform-specific cook variants. The problems this creates:

- **Texture format**: BC7 is not supported on Android/iOS. Mobile requires ASTC 4×4. The spec mentions "transcode BC7 → ASTC at runtime via Basis Universal" — but runtime transcoding wastes CPU time every load and requires shipping both the BC7 source and the ASTC transcoding tables. The correct approach is to cook separate ASTC variants at build time.
- **Texture resolution**: a 4K texture for PC becomes a 1K or 2K texture for mobile without degrading visual quality (smaller screen, shorter view distance).
- **Mesh detail**: mobile may need LOD1 instead of LOD0 as the base mesh. The `.uzmesh` format supports LOD tables, but the cook settings cannot specify "for mobile, use LOD1 as runtime LOD0."
- **Audio quality**: 48 kHz stereo WAV → 48 kHz OGG on PC, but 22 kHz mono OGG on mobile (file size / CPU decode budget).

#### Technical Description

Platform variants are defined in `project.uzproj`:

```toml
[platform.windows]
texture_format   = "BC7"
max_texture_size = 4096
mesh_lod_bias    = 0       # use LOD0 as base
audio_sample_rate = 48000

[platform.android]
texture_format   = "ASTC_4x4"
max_texture_size = 2048
mesh_lod_bias    = 1       # use LOD1 as base on mobile
audio_sample_rate = 22050
audio_channels   = 1       # mono

[platform.ios]
texture_format   = "ASTC_4x4"
max_texture_size = 2048
mesh_lod_bias    = 1
audio_sample_rate = 44100
```

The cook tool receives a `--platform` argument:

```bash
uzengine cook --platform=android --output=.import-android/
uzengine pack --platform=android --output=build/android/game.uzpak
```

Per-asset overrides are possible via `.import.json` settings:

```json
{
  "platform_overrides": {
    "android": { "max_size": 1024, "format": "ASTC_4x4" },
    "windows": { "max_size": 4096, "format": "BC7" }
  }
}
```

#### Impact if Absent

- Mobile port requires re-authoring all assets or writing custom transcoding code
- Shipping one PAK that must serve all platforms forces carrying all format variants (wasted download size)
- No foundation for console platforms (PS5 uses BC7/ASTC hybrid; Switch uses ASTC)

#### Dependencies

Gap 3 (Incremental Import) for per-platform cook caching; build system's `--platform` flag.

---

### 10. Texture Atlas Auto-Packing

**Priority**: HIGH
**Complexity**: M
**Suggested Phase**: 3

#### Why It Is Critical

Small textures (UI icons, particle sprites, decals, weapon attachments) each consume a full draw call's texture slot if bound individually. In a UI with 200 icons at 64×64, binding each separately would mean 200 distinct texture descriptor heap slots and frequent GPU descriptor updates.

Unreal's "UDIM" and texture array systems, Unity's `SpriteAtlas`, and O3DE's `TextureAtlasTool` all solve this: pack small textures into a single large atlas texture, then store UV offsets in a table. The result is one texture bind for 200 icons.

The terrain splatmap (4 materials) is the only atlas-like structure currently in the spec, but it's manually authored.

#### Technical Description

The texture atlas packer runs at cook time:

```cpp
struct AtlasPackRequest {
    std::vector<std::string> source_textures;   // res:// paths
    uint32_t                 atlas_size;         // max 4096x4096
    uint32_t                 padding;            // 2px between sprites (prevent bleeding)
    bool                     generate_mips;
};

struct AtlasResult {
    std::string              atlas_dds_path;     // output DDS
    std::vector<UVRect>      uv_rects;           // one per input texture
    // UVRect: {u_min, v_min, u_max, v_max} in normalized [0,1] space
};
```

A shelf-packing algorithm (Guillotine or MaxRects) is used. The atlas importer produces a JSON manifest alongside the DDS:

```json
// hero_icons.atlas.json
{
  "atlas": "res://textures/ui/hero_icons.dds",
  "sprites": {
    "res://textures/ui/sword_icon.png":    { "u0": 0.00, "v0": 0.00, "u1": 0.25, "v1": 0.25 },
    "res://textures/ui/shield_icon.png":   { "u0": 0.25, "v0": 0.00, "u1": 0.50, "v1": 0.25 },
    "res://textures/ui/potion_icon.png":   { "u0": 0.50, "v0": 0.00, "u1": 0.75, "v1": 0.25 }
  }
}
```

`.uzmat` files can reference sprite names; the material system resolves to atlas UV at load.

#### Impact if Absent

- UI systems bind hundreds of individual textures — descriptor heap pressure, poor batching
- Particle systems that use many small sprites cannot batch draw calls
- No foundation for decal atlas (used in terrain/level decoration systems)

#### Dependencies

Gap 1 (Asset Database) for sprite manifest lookup; Gap 2 (Dependency Graph) to track which materials reference which atlas entries.

---

### 11. UDIM UV Tiling Support

**Priority**: MEDIUM
**Complexity**: M
**Suggested Phase**: 3

#### Why It Is Critical

UDIM (U-Dimension) is the industry standard for high-resolution character and vehicle textures at AAA studios. Instead of squishing an entire character onto one 4K texture, UDIM places different body parts in different UV tiles (1001, 1002, 1003, ...) each with their own 4K texture. A human character might use 10–15 UDIM tiles = effectively a 40K equivalent resolution with excellent texel density.

Blender, Maya, Substance Painter, Houdini all export UDIM natively. The FBX importer (ufbx) and glTF importer (fastgltf) parse UDIM UVs correctly, but UzEngine has no way to represent, import, or render UDIM assets.

#### Technical Description

UDIM requires changes in three places:

**1. Mesh Importer**: detect UVs with values > 1.0 (UDIM UVs extend beyond [0,1] into integer tile space). Record the UDIM tile index per face: tile = `floor(u) + 10 * floor(v) + 1001`.

**2. Asset Database**: a UDIM mesh references multiple textures — one per tile per channel:

```json
// hero.import.json (UDIM)
{
  "udim_tiles": {
    "1001": { "albedo": "res://textures/hero_1001_albedo.png", "normal": "..." },
    "1002": { "albedo": "res://textures/hero_1002_albedo.png", "normal": "..." }
  }
}
```

**3. Renderer**: UDIM rendering requires a texture array or bindless textures. The HLSL shader samples `Texture2DArray` indexed by the UDIM tile stored per-vertex (or per-face) in the mesh.

```hlsl
Texture2DArray AlbedoTiles : register(t0);   // one slice per UDIM tile

float4 PS_UDIM(PSInput input) : SV_Target {
    // tile_index stored in vertex UV.z (integer)
    float3 albedo = AlbedoTiles.Sample(Sampler, float3(input.uv.xy, input.tile_index)).rgb;
    ...
}
```

#### Impact if Absent

- AAA-quality characters with high texel density cannot be imported
- Substance Painter UDIM exports are unsupported — artists must manually re-UV assets
- Competing with Unreal/Unity/O3DE is not possible for character-heavy titles

#### Dependencies

Mesh importer extension; texture importer extension; bindless texture / texture array renderer support.

---

### 12. Material Instancing and Material Library

**Priority**: HIGH
**Complexity**: M
**Suggested Phase**: 2

#### Why It Is Critical

The current `.uzmat` format is a per-asset text file. There is no concept of a "master material" with instance overrides. In AAA pipelines:

- A "Master_M_Character" material defines all the shader logic (PBR + subsurface + cloth blend)
- An "MI_Hero" material instance inherits the master but overrides: albedo texture, roughness value, tint color
- A "MI_NPC_Variant_3" overrides only the tint — inheriting everything else from MI_Hero

Without this hierarchy, every material variant requires duplicating shader references and all shared parameters. 500 character variants = 500 complete `.uzmat` files that must all be updated when the base character shader changes.

Additionally, there is no centralized Material Library — no way to query "show all materials that use the skin shader" or "find all materials with roughness > 0.9."

#### Technical Description

Material instancing requires a two-layer system:

```ini
; materials/base/master_character.uzmat
[uzmat version=1 type=master]
shader = res://shaders/pbr_character.hlsl

[param albedo_texture]
type = texture2d
default = res://textures/dev/white.dds

[param roughness]
type = float
default = 0.5
range = [0.0, 1.0]

[param tint_color]
type = float3
default = [1.0, 1.0, 1.0]
```

```ini
; materials/characters/hero.uzmat
[uzmat version=1 type=instance]
parent = res://materials/base/master_character.uzmat

; Only override what differs:
[override albedo_texture]
value = res://textures/characters/hero_albedo.dds

[override roughness]
value = 0.35
```

The material system resolves the full parameter set at load time by merging parent chain parameters. Parent lookup is cached after first resolution.

The Material Library is a view in the Asset Browser filtered to `.uzmat` files, with search by shader, parameter name, or value range. Powered by Gap 1 (Asset Database).

#### Impact if Absent

- Shader updates require editing every material file that uses that shader
- No way to propagate a global parameter change (e.g., adjust all character roughness) without scripting
- Material complexity explodes — maintenance burden prevents shader iteration in production

#### Dependencies

Gap 1 (Asset Database) for the Material Library query capability.

---

### 13. Texture LOD — Per-Asset Mip Bias

**Priority**: HIGH
**Complexity**: M
**Suggested Phase**: 2

#### Why It Is Critical

The current spec sets a single global `mip_filter` in `project.uzproj`. There is no per-asset mip configuration:
- An albedo texture for a nearby prop should use mip0 within 5m, mip1 at 15m, etc.
- A background skybox texture should always use mip2 (the full-res mip adds no perceptible quality at that distance)
- A UI texture should never mip below a certain level (blurry UI is visually unacceptable)
- A normal map for terrain should have anisotropic filtering at 8× minimum

Without per-asset LOD bias and filter settings, texture quality is either globally over-sampled (VRAM waste) or globally under-sampled (blurry close-up textures).

#### Technical Description

Per-asset texture settings in `.import.json`:

```json
{
  "settings": {
    "format": "BC7",
    "mip_filter": "box",
    "anisotropy": 8,
    "lod_bias": -0.5,         // negative = sharper (load earlier mips), positive = blurrier
    "mip_clamp_min": 0,       // never drop below mip0 (always full-res)
    "mip_clamp_max": 8,       // never go above mip8 (min 4x4 pixels)
    "never_stream": false,    // true for small UI textures (<64px): always resident
    "srgb": true,
    "generate_mips": true
  }
}
```

The compiled DDS stores `lod_bias` and `anisotropy` in the texture metadata block. The ResourceManager passes these to `D3D12_SAMPLER_DESC` when creating samplers.

#### Impact if Absent

- Uniform mip settings cause over-sampling for background assets and under-sampling for hero assets
- UI textures may be blurred at runtime
- Normal maps lose precision at grazing angles without anisotropic filtering

#### Dependencies

None — purely additive to existing texture importer.

---

### 14. Asset Tags and Metadata System

**Priority**: MEDIUM
**Complexity**: S
**Suggested Phase**: 2

#### Why It Is Critical

As project asset counts grow past a few hundred, discoverability becomes a problem. On a team of 10 people working on 5,000 assets, finding "all environment textures that are flagged as needing review" requires either a shared spreadsheet or a tagging system.

Unreal's Asset Tags and O3DE's Asset System both support user-defined tags on assets. This enables:
- "Show me all `hero_costume` tagged textures"
- "Mark this mesh as `wip` — filter it out of shipping builds"
- "Find all assets tagged `needs_art_review`"

#### Technical Description

Tags are stored in the Asset Database (Gap 1) in the `tags` table. The `.import.json` sidecar optionally carries tags:

```json
{
  "tags": ["character", "hero", "reviewed", "lod-ready"],
  "metadata": {
    "author": "jane_doe",
    "status": "final",
    "department": "character-art"
  }
}
```

Tags can be added/removed from the Asset Browser UI without re-importing the asset (they live in the database, not in the source file). The cook tool respects a `--exclude-tag=wip` flag to skip WIP assets from shipping builds.

#### Impact if Absent

- Asset discovery requires filesystem search (slow, no metadata)
- No way to filter WIP assets from shipping
- Art review workflow requires external tooling (spreadsheets)

#### Dependencies

Gap 1 (Asset Database) for tag storage.

---

### 15. Asset Validation Pipeline

**Priority**: HIGH
**Complexity**: M
**Suggested Phase**: 2

#### Why It Is Critical

AAA pipelines run automatic validation checks on every imported asset. These catch art mistakes before they reach production:

- Mesh has non-manifold geometry (holes) → physics hull generation will fail
- Normal map has values outside [-1, 1] after BC5 decode (precision loss in very saturated normals)
- Texture resolution is not power-of-two (DX12 hardware mipmapping requires POT for some formats)
- Skinned mesh has vertices with no bone weights (will collapse to origin at runtime)
- Animation clip has bone channels for bones that don't exist in the referenced skeleton
- Audio file has clipping (peak amplitude > 0 dBFS) — will distort on all speakers

Without validation, these bugs reach playtesters or worse — shipped gold builds.

#### Technical Description

The validation pipeline is a set of pluggable `IAssetValidator` implementations, run after import:

```cpp
class IAssetValidator {
public:
    virtual std::string_view asset_type() const = 0;  // "mesh", "texture", "audio"
    struct Issue {
        enum Severity { INFO, WARN, ERROR } level;
        std::string message;
        std::string fix_hint;  // suggested remediation
    };
    virtual std::vector<Issue> validate(const ImportedAsset& asset) = 0;
};

// Example: mesh validator
class MeshValidator : public IAssetValidator {
    std::vector<Issue> validate(const ImportedAsset& asset) override {
        std::vector<Issue> issues;
        auto& mesh = asset.as_mesh();

        if (mesh.vertex_count > 500'000)
            issues.push_back({ WARN, "High poly count: " + std::to_string(mesh.vertex_count),
                               "Use meshoptimizer to reduce, or split into LODs" });

        if (mesh.is_skinned && has_unweighted_vertices(mesh))
            issues.push_back({ ERROR, "Skinned mesh has vertices with zero bone weight",
                               "Assign bone weights in DCC tool before re-exporting" });

        if (mesh.uv_max > 1.0f && !mesh.is_udim)
            issues.push_back({ WARN, "UV values outside [0,1] without UDIM flag",
                               "Enable UDIM mode in import settings, or re-unwrap in DCC" });

        return issues;
    }
};
```

Validation results are:
1. Stored in the Asset Database alongside the import record
2. Shown in the VSCode Asset Browser as colored badges (green = OK, yellow = warnings, red = errors)
3. Included in build reports (`uzengine cook --report=validation.json`)
4. ERROR-level issues optionally fail the cook (configurable per severity)

#### Impact if Absent

- Art mistakes (zero bone weights, clipping audio, non-manifold meshes) reach production
- Physics ragdoll or convex hull generation fails silently at runtime on bad geometry
- Audio distortion shipped to players

#### Dependencies

Gap 1 (Asset Database) for storing validation results per asset.

---

### 16. Procedural Asset Generation (PCG Assets)

**Priority**: MEDIUM
**Complexity**: XL
**Suggested Phase**: 4

#### Why It Is Critical

UE5's PCG (Procedural Content Generation) framework, Houdini Engine integration, and SpeedTree all allow runtime or cook-time generation of assets from procedural rules. Examples:

- A city block where building facades are generated from a grammar rule given a plot polygon
- Foliage scattering where trees are placed by a Poisson disk sampler respecting slope/altitude rules
- Road splines that tessellate their geometry at cook time based on control points

The terrain spec currently bakes foliage positions deterministically at cook time (from density + seed). This is PCG but it is hardcoded — there is no extensible PCG framework.

#### Technical Description

PCG assets are defined by a `.uzpcg` script that runs at cook time to produce standard asset types:

```ini
[uzpcg version=1 name="CityBlock"]
generator = "res://scripts/pcg/city_block.lua"  ; Lua script runs at cook time

[input]
plot_polygon = polygon   ; provided at scene placement time
building_catalog = "res://assets/buildings/"

[output]
mesh = "res://generated/city_block_<hash>.uzmesh"
```

The PCG system is an extension to the build pipeline, not the runtime engine. Generated assets are cached in `.import/` with a hash of the input parameters. If parameters change, the asset is regenerated.

This is a Phase 4 concern — it requires the full dependency graph (Gap 2) and async streaming (Gap 5) to be in place first.

#### Impact if Absent

- Large open worlds require fully hand-authored environments (not scalable)
- Terrain foliage is limited to the hardcoded density/scatter rules in terrain spec
- Cannot integrate Houdini Engine procedural assets

#### Dependencies

Gaps 1–3 (Database, Dependency Graph, Incremental Import); Lua scripting system (Phase 1).

---

### 17. Collaborative Asset Locking

**Priority**: LOW
**Complexity**: M
**Suggested Phase**: 4

#### Why It Is Critical

In any team larger than 2–3 people, multiple artists can attempt to edit the same source asset simultaneously. Without locking:

- Artist A and Artist B both open `hero.fbx` in Maya
- Both export updated versions
- The second export silently overwrites the first — one artist's work is lost

Perforce has exclusive checkout. Git does not, but Git-LFS can be paired with a locking server. UzEngine's build system has no awareness of concurrent asset editing.

#### Technical Description

Asset locking is implemented as a lightweight lock server (a small HTTP service or a simple file-based lock registry checked into version control alongside the assets):

```json
// .assetlocks.json (checked into git, updated atomically via lock file)
{
  "res://meshes/hero.fbx": {
    "locked_by": "jane_doe",
    "locked_at": "2026-03-02T14:22:00Z",
    "machine":   "JANE-WORKSTATION"
  }
}
```

The VSCode Asset Browser shows a lock icon on locked assets. Attempting to edit a locked asset shows a warning: "Locked by jane_doe since 14:22. Contact jane_doe to unlock or steal the lock."

This is a low-priority gap because it requires team coordination infrastructure beyond the engine. For solo/small-team projects it is not needed.

#### Impact if Absent

- Teams of 3+ people experience regular asset overwrites
- No awareness in the editor of who is editing which asset

#### Dependencies

Gap 1 (Asset Database) for lock state storage; Source control integration (Gap 18).

---

### 18. Source Control Integration (Git-LFS / Perforce Awareness)

**Priority**: MEDIUM
**Complexity**: M
**Suggested Phase**: 3

#### Why It Is Critical

Binary assets (FBX, PNG, WAV) should not be stored in standard Git — they bloat history exponentially. Git-LFS (Large File Storage) is the standard solution: binary files are replaced by small text pointers in Git, with the actual binaries stored on an LFS server.

UzEngine's cook tool must be LFS-aware:
- The `.import.json` sidecar records the source hash. If the source file is an LFS pointer rather than the actual binary, the hash will be that of the pointer text (48 bytes) — not the actual asset. Cook will always think the asset is unchanged.
- `uzengine cook` should detect LFS pointers and either resolve them (pull the actual file) or report a clear error.
- The `.import/` directory should never be committed to Git or LFS (it's in `.gitignore`). The build system should verify this.

#### Technical Description

LFS detection in the cook tool:

```cpp
bool is_lfs_pointer(const std::filesystem::path& path) {
    // LFS pointers are text files < 200 bytes starting with "version https://git-lfs.github.com"
    auto size = std::filesystem::file_size(path);
    if (size > 512) return false;
    std::ifstream f(path);
    std::string first_line;
    std::getline(f, first_line);
    return first_line.starts_with("version https://git-lfs.github.com");
}

// In the cook loop:
if (is_lfs_pointer(source_path)) {
    LOG_ERROR("LFS pointer found: {}. Run 'git lfs pull' before cooking.", source_path);
    return CookResult::Error;
}
```

The cook tool should also emit a `.gitattributes` template:

```bash
uzengine init-gitlfs   # writes .gitattributes for all binary extensions
```

Perforce integration is simpler — Perforce always materializes actual files; the cook tool just needs to be aware that `.import/` should have a Perforce typemap entry of `binary+w` (writable, not version-controlled).

#### Impact if Absent

- Git-LFS pointers silently cooked as "unchanged" → wrong output artifacts shipped
- Binary assets committed to standard Git bloat repository to multi-GB
- CI/CD pipelines fail to cook because LFS files not pulled before cook

#### Dependencies

None — standalone cook tool extension.

---

### 19. Asset Preview Thumbnails (Editor Cache)

**Priority**: MEDIUM
**Complexity**: M
**Suggested Phase**: 2

#### Why It Is Critical

The current spec describes `asset.getThumb` returning a 128×128 JPEG thumbnail via IPC. However, the IPC handler is defined but there is no specification for:
1. How thumbnails are generated (which system renders them)
2. How they are cached (re-rendering 1,000 thumbnails on every editor launch is 15–30 seconds)
3. How they are invalidated (thumbnail must update when asset changes)
4. What thumbnails look like for different asset types (mesh, texture, audio, animation)

Without a thumbnail cache, the Asset Browser is either empty (no thumbnails) or slow (renders on demand).

#### Technical Description

Thumbnail generation strategy per asset type:

| Asset Type | Thumbnail Method |
|-----------|-----------------|
| Texture (`.png`, `.dds`) | Decode mip5 or mip6, scale to 128×128 (CPU, no GPU needed) |
| Mesh (`.uzmesh`, `.glb`) | Engine renders mesh with turntable camera, captures 128×128 JPEG |
| Material (`.uzmat`) | Engine renders a material ball (sphere) with the material applied |
| Audio (`.ogg`, `.wav`) | Waveform visualization drawn as a 128×128 PNG on CPU |
| Animation (`.uzanim`) | Render the first pose of the animation on the default skeleton |
| Scene (`.uzscene`) | Engine renders a top-down thumbnail of the scene |

Thumbnail cache lives at `.thumbnails/<asset_hash>.jpg` (gitignored). Cache key = `source_hash + thumbnail_version`. A global `thumbnail_version` integer is incremented when the thumbnail rendering code changes, invalidating all cached thumbnails.

```cpp
std::string ThumbnailCache::get_or_generate(const std::string& res_path) {
    std::string source_hash = db.get_source_hash(res_path);
    std::string cache_key = source_hash + "_v" + THUMBNAIL_VERSION;
    std::string cache_path = ".thumbnails/" + cache_key + ".jpg";
    if (std::filesystem::exists(cache_path)) return cache_path;
    return generate_thumbnail(res_path, cache_path);
}
```

#### Impact if Absent

- Asset Browser shows no thumbnails (all file icons)
- Artists cannot identify assets visually — slows content discovery
- Generating thumbnails on-demand causes editor stutters on large projects

#### Dependencies

Gap 1 (Asset Database) for source hash lookup; engine IPC for mesh/material rendering.

---

### 20. Hot-Reload for Any Asset at Runtime

**Priority**: HIGH
**Complexity**: L
**Suggested Phase**: 3

#### Why It Is Critical

The current spec supports shader hot-reload (Phase 1). Lua script hot-reload is specified (Phase 1). But there is no generalized hot-reload for:
- Textures: change albedo texture → update immediately in viewport without restart
- Meshes: update a model's geometry → reflects in editor in real time
- Materials: edit a `.uzmat` text file → material updates without reloading scene
- Animations: tweak animation clip → plays new version immediately
- Audio: replace a sound file → plays new version on next trigger

Without generalized hot-reload, artists must restart the editor (20–60 seconds) after every asset change. In an iterative art workflow, this is the difference between "can make 10 iterations per hour" and "can make 60 iterations per hour."

#### Technical Description

Hot-reload requires three components:

**1. File Watcher Extension**

The existing shader file watcher (Phase 1) must be generalized to all asset types:

```cpp
class AssetFileWatcher {
public:
    void watch(const std::string& directory);  // recursive watch on assets/
    void on_change(const std::string& path, ChangeType type);
    // Debounce: wait 100ms after last change before triggering re-import
};
```

**2. Per-Asset-Type Hot-Reload Handler**

Each importer registers a hot-reload handler:

```cpp
class TextureImporter : public IAssetImporter {
    void hot_reload(const std::string& res_path) override {
        // Re-import source → new DDS
        auto new_dds = reimport(res_path);
        // Upload new texture data to GPU (no descriptor heap change needed)
        auto handle = resource_manager.find_handle(res_path);
        gpu.upload_texture(handle, new_dds);
        // Notify all materials that reference this texture to rebind
        for (auto& mat : resource_manager.get_dependents(res_path))
            mat.mark_dirty();
    }
};
```

**3. Reference Invalidation**

The ResourceHandle pattern must support "invalidation on hot-reload." When a mesh hot-reloads, all scene nodes holding a handle to that mesh must receive the new version — without the game code explicitly requesting a reload.

This requires a generation counter bump on the handle — existing handles become stale and resolve to the new version on next `resource_manager.get(handle)` call.

#### Impact if Absent

- Art iteration cycle: change asset → restart engine → 30–60 second wait → see result
- Shader hot-reload works but texture/mesh/audio changes require restart — inconsistent UX
- Artists working in the editor spend 50% of time waiting for restarts

#### Dependencies

Gap 2 (Dependency Graph) for invalidating dependents on hot-reload; existing file watcher infrastructure from shader hot-reload.

---

### 21. Sprite Sheet Import and Animation Frames

**Priority**: MEDIUM
**Complexity**: S
**Suggested Phase**: 2

#### Why It Is Critical

2D sprite sheets are used for:
- Particle flipbook textures (fire, smoke, explosion frames — standard in every particle system)
- UI animated icons (progress spinners, character portraits with blink animations)
- 2D game characters (if UzEngine targets 2D games, which the vision does not exclude)

The particle system spec (Phase 4) references `.uzparticles` with no mention of flipbook texture support. A particle "fire" effect without flipbook animation is just a static sprite — not a fire.

#### Technical Description

The texture importer is extended with a `sprite_sheet` mode:

```json
{
  "importer": "texture",
  "settings": {
    "format": "BC7",
    "sprite_sheet": {
      "mode": "grid",          // "grid" or "packed"
      "cols": 8,
      "rows": 4,               // 32 frames total
      "frame_rate": 24,        // fps for animation playback
      "output": "atlas"        // output as texture atlas with manifest
    }
  }
}
```

The output is a standard texture atlas DDS (compatible with Gap 10) plus a flipbook manifest:

```json
// explosion_frames.flipbook.json
{
  "atlas": "res://textures/vfx/explosion_frames.dds",
  "frame_count": 32,
  "frame_rate": 24,
  "frames": [
    { "u0": 0.000, "v0": 0.000, "u1": 0.125, "v1": 0.250 },
    ...
  ]
}
```

The particle system and UI system consume the `.flipbook.json` manifest. No GPU-side changes needed — it is purely an import-time decomposition.

#### Impact if Absent

- Particle flipbook textures not supported → VFX limited to static billboards
- Animated UI icons require custom sprite animation code

#### Dependencies

Gap 10 (Texture Atlas) for the output format.

---

### 22. Audio Asset Processing Pipeline

**Priority**: HIGH
**Complexity**: M
**Suggested Phase**: 2

#### Why It Is Critical

The current audio importer is described as: WAV → OGG (normalized). This is a bare minimum. AAA audio pipelines include:

1. **Sample rate conversion**: source audio from studios arrives at various sample rates (44.1 kHz, 96 kHz, 192 kHz). Playback target is usually 48 kHz. SRC must use a high-quality algorithm (libsamplerate's `SRC_SINC_BEST_QUALITY`), not a simple interpolation.

2. **Loudness normalization**: the spec mentions `-16 LUFS` target in `project.uzproj`, but does not specify the measurement algorithm. LUFS (Loudness Units Full Scale, ITU-R BS.1770) is the correct algorithm. Simple peak normalization is incorrect for perceived loudness.

3. **True peak limiting**: after LUFS normalization, inter-sample true peaks can still exceed 0 dBFS. A true-peak limiter (not a brick-wall clipper) must be applied to prevent digital distortion.

4. **Silence trimming**: source audio often has 100–500ms of silence at head and tail (from studio recording). Automatic silence trimming reduces file size and prevents timing issues in sound effects.

5. **Streaming threshold**: the spec says "inline if < 2MB, streaming reference if > 2MB". This should be configurable per audio type: music is always streamed regardless of size; short SFX are always inlined; ambient loops may vary.

6. **Audio metadata**: BPM, key, loop points. Music tracks should carry loop point metadata so the audio system can seamlessly loop without the game code knowing exact sample positions.

#### Technical Description

Audio processing chain in the importer:

```cpp
AudioProcessResult AudioImporter::import(const std::string& source_path,
                                          const AudioImportSettings& settings)
{
    auto audio = load_audio(source_path);  // stb_vorbis / libsnd for WAV

    // 1. Sample rate conversion
    if (audio.sample_rate != settings.target_sample_rate)
        audio = resample(audio, settings.target_sample_rate, SRC_SINC_BEST_QUALITY);

    // 2. Silence trimming
    if (settings.trim_silence)
        audio = trim_silence(audio, threshold_db=-60.0f);

    // 3. LUFS normalization (ITU-R BS.1770-4)
    float current_lufs = measure_integrated_loudness(audio);
    float gain = settings.target_lufs - current_lufs;
    apply_gain(audio, gain);

    // 4. True-peak limiting
    if (settings.true_peak_limit)
        audio = apply_true_peak_limiter(audio, ceiling_dbtp=-1.0f);

    // 5. Channel fold (stereo → mono for mobile if configured)
    if (settings.force_mono && audio.channels == 2)
        audio = fold_to_mono(audio);

    // 6. Encode to OGG Vorbis
    float quality = (audio.duration_sec > settings.stream_threshold_sec)
                  ? settings.streaming_quality : settings.sfx_quality;
    auto ogg_data = encode_ogg(audio, quality);

    // 7. Write .uzaudio with header
    write_uzaudio(ogg_data, audio.metadata, output_path);
}
```

The `.uzaudio` format (currently not fully specified) should carry:

```
Header:
  magic            uint32
  version          uint32
  sample_rate      uint32
  channels         uint8
  duration_samples uint64
  loop_start       uint64    // sample offset for loop start (0 = no loop)
  loop_end         uint64    // sample offset for loop end (0 = play to end)
  stream_mode      uint8     // 0=inline, 1=streaming
  peak_lufs        float     // measured loudness after normalization
  true_peak_dbfs   float     // measured true peak
```

#### Impact if Absent

- Sample rate mismatches cause pitch shift on playback (audio at 44.1kHz played at 48kHz runs 9% fast)
- Loudness inconsistency between SFX and music (some assets too quiet, others too loud)
- No loop point metadata forces all music loops to jump to sample 0 (audible click)
- True peak clipping causes distortion on speakers and headphones

#### Dependencies

`libsamplerate` (vcpkg: `libsamplerate`) for SRC; `libebur128` for LUFS measurement. Both are MIT/LGPL licensed.

---

### 23. Video / Cutscene Asset Pipeline

**Priority**: LOW
**Complexity**: L
**Suggested Phase**: 4

#### Why It Is Critical

Most AAA and AA titles include pre-rendered cutscenes (intro movies, story beats, credits). The standard solutions are:

- **Bink Video** (RAD Game Tools): proprietary, used by virtually every AAA title. Very high quality at low bitrate.
- **WebM / VP9**: open, good quality, hardware-accelerated decode on modern GPUs
- **AV1**: next-generation, better compression than VP9, increasingly hardware-accelerated

The UzEngine asset pipeline has no video import whatsoever. The build system has no mechanism to include video files in `.uzpak`.

#### Technical Description

Video assets are handled differently from other assets — they are not decompressed at load time but decoded frame-by-frame at playback time.

**Import pipeline**: transcode source video (ProRes, MXF, H.264) to engine-target format:

```bash
# Using FFmpeg (LGPL, subprocess from cook tool)
ffmpeg -i source_cinematic.mov \
    -c:v libvpx-vp9 -crf 31 -b:v 0 \     # VP9 variable bitrate
    -c:a libopus -b:a 192k \               # Opus audio
    output_cinematic.webm
```

**PAK inclusion**: video files are not compressed again in `.uzpak` (they are already compressed internally). The PAK entry stores them with `flags |= NO_RECOMPRESS`.

**Runtime playback**: the engine uses `libvpx` (vcpkg) for VP9 software decode, or `DXVA2`/`Media Foundation` for hardware-accelerated decode on Windows. Decoded frames are uploaded as `RGBA8` textures to a fullscreen quad.

A `CinematicPlayer` node type in `.uzscene` references the video asset:

```ini
[node id=10 type=CinematicPlayer name="IntroMovie"]
video = "res://video/intro.webm"
auto_play  = true
loop       = false
on_end     = scene:load res://scenes/main_menu.uzscene
```

#### Impact if Absent

- No in-engine cutscene playback — requires external media player
- Intro/outro movies common in shipping titles cannot be included
- Story-driven games cannot use pre-rendered cinematics

#### Dependencies

`libvpx` (vcpkg) for VP9 decode; FFmpeg subprocess for transcoding at cook time (FFmpeg is LGPL — distribute as a separate binary, do not link statically).

---

### 24. Batch Processing and Distributed Cooking

**Priority**: MEDIUM
**Complexity**: L
**Suggested Phase**: 3

#### Why It Is Critical

The current build spec states `cook_threads = 8` — parallel cooking on a single machine. At AAA scale:

- A single project may have 50,000 textures. At 2 seconds per BC7 compression on a 16-core machine, that is 50,000 / 8 = 6,250 seconds = 1.7 hours just for textures on a single machine.
- Unreal's Derived Data Cache (DDC) distributes cooked artifacts across a shared network cache. If artist A cooks `hero.png` on their workstation, artist B's cook can fetch the result from the shared cache instead of recomputing.
- id Software's cook farm uses tens of machines in parallel, reducing full project cook from hours to minutes.

#### Technical Description

Distributed cooking has two components:

**1. Shared Cook Cache (Network DDC)**

Cooked artifacts are keyed by `(source_hash, settings_hash, importer_version)` — identical to the incremental cook key (Gap 3). If the key is present in a shared cache, the result is downloaded instead of computed:

```cpp
class CookCache {
public:
    // Attempt to fetch from cache; returns false if cache miss
    bool fetch(const CookKey& key, std::filesystem::path& output_path);
    // Upload completed cook result to cache
    void store(const CookKey& key, const std::filesystem::path& artifact_path);
private:
    // Local disk cache + optional network cache (HTTP PUT/GET to a simple file server)
    LocalDiskCache  local_cache_;   // .uzcache/ directory, max 20 GB, LRU eviction
    NetworkCache*   network_cache_; // optional: configure in project.uzproj
};
```

The network cache is a simple HTTP file server — no exotic infrastructure required. Even a Samba share or S3 bucket works.

**2. Distributed Cook Workers**

For projects that need sub-10-minute full cook times, the cook tool supports a worker protocol:

```bash
# On the build server — start cook coordinator
uzengine cook-server --port=7777 --workers=16

# On each worker machine
uzengine cook-worker --server=192.168.1.100:7777
```

The coordinator splits asset cook tasks across workers. Each worker fetches the source asset from a shared network path, cooks it, and uploads the result to the shared cook cache.

This is a Phase 3–4 concern but the **foundation** (consistent CookKey, shared cache protocol) should be designed in Phase 2 when Gap 3 is implemented — retrofitting a cache protocol later is significantly more expensive.

#### Impact if Absent

- Full project cook scales linearly with asset count — 1.7 hours for 50,000 textures
- CI/CD pipelines on asset-heavy branches take 2–4 hours
- No way to share cook results between team members (each person recooks locally)

#### Dependencies

Gap 3 (Incremental Import / CookKey) for the cache key design; network infrastructure (out of engine scope — just needs an HTTP endpoint).

---

## Summary: Recommended Phase Assignments

### Phase 2 (alongside gltf-import, fbx-import, texture-tools)

These gaps are **architectural prerequisites** — implementing them later requires breaking changes:

| Gap | Reason for Phase 2 |
|-----|--------------------|
| 1. Source Asset Database | Foundation for Gaps 2, 8, 12, 14, 15, 19 |
| 2. Asset Dependency Graph | Foundation for Gaps 3, 5, 7, 20 |
| 3. Fine-Grained Incremental Import | Required for team-scale projects |
| 4. Asset Redirectors | Prevents rename-induced breakage from day one |
| 8. Asset Size Budget Enforcement | Catches overruns before they ship |
| 12. Material Instancing | Prevents maintenance debt explosion |
| 13. Per-Asset Mip Bias | Additive, no dependencies |
| 14. Asset Tags | Trivial with the database in place |
| 15. Asset Validation | Catches art mistakes early |
| 19. Thumbnail Cache | UX requirement for Asset Browser |
| 21. Sprite Sheet Import | Required for particle flipbooks (Phase 4) |
| 22. Audio Processing Pipeline | Correctness issue (sample rate, LUFS) |

### Phase 3 (alongside skeletal-mesh, animation, input, camera)

| Gap | Reason for Phase 3 |
|-----|--------------------|
| 5. Runtime Async Streaming | Required for open-world content |
| 7. Mip Streaming | Required for VRAM budget compliance |
| 9. Cross-Platform Variants | Required before Android targeting |
| 10. Texture Atlas Packing | Required for particle and UI systems |
| 11. UDIM Support | Required for AAA-quality characters |
| 18. Source Control Integration | Required for team workflows |
| 20. Hot-Reload Any Asset | Critical for art iteration velocity |
| 24. Distributed Cooking | Required for large teams / CI speed |

### Phase 4 (alongside build-pipeline, navmesh, terrain, plugin)

| Gap | Reason for Phase 4 |
|-----|--------------------|
| 6. Virtual Texture Streaming | Required for large terrain at quality |
| 16. Procedural Asset Generation | Depends on full pipeline |
| 17. Collaborative Asset Locking | Team infrastructure, low urgency |
| 23. Video / Cutscene Pipeline | Story-driven game requirement |

---

## Critical Path Warning

Gaps 1–4 form a **critical dependency chain**. If the Source Asset Database (Gap 1) and Asset Dependency Graph (Gap 2) are not built at the start of Phase 2, every subsequent system — streaming, budget enforcement, redirectors, hot-reload, distributed cooking — must be retrofitted on top of the existing flat `.import.json` sidecar architecture.

This retrofit is not impossible, but it is expensive: it requires migrating all existing sidecar data into the database, updating all IPC handlers, and changing the cook tool's architecture midway through Phase 3 or Phase 4 when the team is under shipping pressure.

**Recommendation**: treat Gaps 1 and 2 as the first two tasks of Phase 2, before any new importers are implemented.

---

*Review generated by Asset Pipeline Engineer agent — UzEngine project, 2026-03-02*
