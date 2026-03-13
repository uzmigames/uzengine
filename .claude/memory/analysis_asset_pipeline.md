---
name: Asset Pipeline Build Order Analysis for Roadmap
description: Asset and resource pipeline implementation order for UzEngine roadmap
type: project
---

# Asset Pipeline Build Order Analysis

## 1. Dependency Chain Map

```
VFS (core/asset/) [DONE, Milestone 1.2]
    │
    ├── AssetCache (xxHash64 content-addressed, .cache/)     [DONE, 1.2]
    ├── AssetImporterRegistry + IAssetImporter interface     [DONE, 1.2]
    ├── ImportSidecar (.uzimport JSON)                       [DONE, 1.2]
    │
    ▼
uzcook CLI (batch orchestrator)                             [Scaffold done, 1.2]
    │
    ├── ShaderImporter (.hlsl → .uzshader via DXC)          [DONE, 1.5.0]
    │
    ├── TextureImporter (.png/.jpg/.tga → .uztexture)       [Planned, 1.5.1]
    │   ├── stb_image (decode)
    │   ├── Mipmap generation (box filter → Kaiser)
    │   └── ispc_texcomp (BC1/BC7/BC5/BC6H)
    │
    ├── MeshImporter (.gltf/.glb/.fbx → .uzmesh)           [Planned, 1.5.1]
    │   ├── fastgltf / ufbx (parse)
    │   ├── meshoptimizer (cache + overdraw + LOD)
    │   └── mikktspace (tangent generation)
    │
    ├── AudioImporter (.wav/.ogg/.mp3 → .uzaudio)          [Planned, 1.5.1]
    │
    └── Packager (.uzpak via uzcook --pak)                 [Planned, 1.5.2]
        ├── .uzpak (flat binary, 64-byte aligned, zstd opt)
        └── .uzmanifest (JSON, for patching)
```

## 2. When Downstream Systems Need Each Pipeline Stage

### Milestone 1.3.0 — Triangle to Screen

- **Needs TODAY**: A way to load a `.uzmesh` and upload it to the GPU.
- The roadmap's Milestone 1.3.0 task "Mesh loading" says:
  > Load `.uzmesh` (JSON header + binary vertex data)
- This is the MINIMUM: even a hand-authored or code-generated `.uzmesh` file suffices.
- TextureImporter is needed for "Texture sampling: Load PNG/JPG → upload → sample in shader".
- **Minimum viable asset pipeline for 1.3.0**:
  - `.uzmesh` loader (runtime deserialization of the binary format, no import pipeline needed)
  - PNG/JPG → raw RGBA upload (stb_image inline, no BC7 needed yet)
  - No uzcook, no .uzpak, no cache required

### Milestone 1.3.1 — Deferred Pipeline

- Needs textured materials (albedo, normal, roughness maps) in G-Buffer.
- **Needs**: TextureImporter with BC5 for normals and BC7 for albedo, or uncompressed upload as fallback.
- MeshImporter with LOD support not yet critical but useful.

### Milestone 1.3.2 — Post-Processing

- Needs HDR textures (LUTs, environment cubemaps).
- **Needs**: BC6H support in TextureImporter.

### Milestone 1.4 — Scene System

- Needs fully cooked asset references via `res://` paths.
- Hot-reload: `FileWatcher` → detect source change → re-import via uzcook or inline re-import.
- **Needs**: Import sidecars (.uzimport) and the incremental cook logic.
- `.uzscene` JSON is human-authored — no cook step for scenes themselves at this stage.

### Milestone 1.5.1 — Animation

- Needs `.uzskeleton` and `.uzanim` importers — both are extensions of the MeshImporter (glTF animation track extraction).
- **Needs**: MeshImporter Phase 2 (skinned mesh support, bone weight normalization).

### Milestone 1.5.2 — Audio

- Needs `.uzaudio` runtime loader.
- Small sounds (<2 MB): fully decoded PCM.
- Large music (>2 MB): streaming reference.
- **Needs**: AudioImporter for WAV/OGG/MP3 → .uzaudio conversion.

### Phase 1.5 Build Pipeline / Milestone 1.5.2 — Packager

- Needs ALL importers complete.
- **Needs**: uzcook with full importer registry, .uzpak writer, .uzmanifest, delta-pak for patching.

## 3. Early vs Late Asset Cooking

### Foundation (Already Done — Milestone 1.2)
The core framework is already in place:
- IAssetImporter interface, AssetImporterRegistry, AssetCache, ImportSidecar.
- VFS (LooseFileVFS for dev, PakVFS for shipping).
- uzcook scaffold with ShaderImporter registered.

This was correctly done early — it unblocked shader hot-reload and all future importers slot in without redesign.

### Should Cooking Be Early or Late?

**Cooking infrastructure = early (done).** The importer framework and cache are foundational.

**Actual importers (texture, mesh, audio) = targeted to when rendering needs them.**

The decision tree:
```
1.3.0 needs textured meshes
  → minimal TextureImporter (PNG → uncompressed RGBA8 is acceptable for MVP)
  → minimal MeshImporter (parse glTF, write .uzmesh, no LOD needed yet)
  → These belong BEFORE 1.3.0 completion, not in Phase 1.5

1.3.1 deferred pipeline needs compressed textures
  → BC7 for albedo, BC5 for normals needed here
  → ispc_texcomp integration needed by mid-1.3

1.5.0 Physics / 1.5.1 Animation needs MeshImporter Phase 2
  → Convex hull generation (VHACD) for mesh colliders
  → Skinned mesh import for skeletal animation
  → These belong in Phase 1.5 timeframe

Phase 1.5 Packager (.uzpak) belongs LAST
  → Only needed for shipping builds
  → Development uses LooseFileVFS without packing
```

### Revised Assessment

The roadmap places "1.5.1 Asset Processor" very late (parallel with 1.5.5-1.5.7). This is TOO LATE for basic texture/mesh importers. The correct split is:

| Stage | When | What |
|-------|------|------|
| **Early (before 1.3.0)** | Now | Minimal TextureImporter (stb_image → raw RGBA8), MeshImporter (fastgltf → .uzmesh, no LOD) |
| **Mid-renderer (1.3.1)** | During deferred pipeline | BC7/BC5/BC6H compression (ispc_texcomp), mipmap chain (Kaiser) |
| **1.5 systems** | After 1.4 | LOD generation (meshoptimizer), tangent baking (mikktspace), skinned mesh, AudioImporter |
| **Late (1.5.2 Packager)** | Shipping prep | .uzpak, .uzmanifest, delta patching, parallel cook jobs |

## 4. .uz* Format Design — When Stabilization Matters

### Current Format Status

| Format | Status | Stability |
|--------|--------|-----------|
| `.uzshader` | Done (1.5.0) | Stable — DXC output, reflection data |
| `.uzmesh` | Spec written, not implemented | Needs stabilization before 1.3.0 |
| `.uztexture` | Spec written, not implemented | Needs stabilization before 1.3.1 |
| `.uzaudio` | Planned | Can wait until 1.5.2 audio milestone |
| `.uzpak` | Spec written, not implemented | Can wait until Phase 1.5 packager |
| `.uzscene` | Planned for 1.4 | Scene serialization, not import pipeline |

### Stabilization Strategy

**Format version field is the critical design choice.** Each format has `uint32_t version` in its header and `IAssetImporter::Version()` on the importer side. Together these ensure:
- Any format change bumps version → all cached .uzimport sidecars invalidate → full reimport
- Old .uzmesh files are cleanly rejected by runtime loader

**Recommendation**: Stabilize .uzmesh and .uztexture formats BEFORE writing the GPU upload code in 1.3.0, not after. The cost of changing the binary format after the GPU loader exists is higher than getting it right upfront. The specs in `08-resource-system/06-mesh-pipeline.md` and `07-texture-pipeline.md` are detailed enough to implement directly.

The `.uzpak` format can remain provisional until Phase 1.5.2 — no runtime code depends on it until shipping builds are needed.

## 5. Minimum Viable Asset Pipeline for "Load and Render a Textured Mesh"

### What Is Strictly Required

```
1. MeshImporter (minimal)
   - fastgltf: parse .glb → extract vertex positions, normals, UVs, indices
   - meshoptimizer: vertex cache optimization (single pass, no LOD required)
   - Write .uzmesh header + vertex streams + index buffer
   - No LOD generation, no tangent generation, no skinning needed for MVP

2. TextureImporter (minimal)
   - stb_image: decode PNG/JPG → RGBA8 pixels
   - Box-filter mip generation (2 passes minimum: base + mip0 only is acceptable)
   - Write .uztexture header + uncompressed RGBA8 data
   - BC7 compression NOT required for first-render MVP (adds minutes of build time)

3. Runtime .uzmesh loader
   - Read header, validate magic + version
   - Upload vertex buffer + index buffer to GPU via RHI
   - No LOD selection logic required for MVP

4. Runtime .uztexture loader
   - Read header, validate magic + version
   - Create D3D12 texture, upload RGBA8 data via UpdateSubresources
   - Register descriptor in bindless heap

5. uzcook (already scaffolded)
   - Register TextureImporter + MeshImporter
   - Run incremental import on assets/ directory
   - No --pak flag needed for MVP
```

### What Is NOT Required for MVP

- BC7/BC5/BC6H compression (ispc_texcomp) — add for 1.3.1 deferred pipeline
- LOD generation (meshoptimizer simplify) — add for performance optimization
- Tangent generation (mikktspace) — add when normal mapping is needed
- AudioImporter — add at 1.5.2 audio milestone
- .uzpak packager — add at Phase 1.5.2 shipping milestone
- Virtual texturing — Phase 2.5+
- FBX support (ufbx) — Phase 2.5 (glTF covers most artist workflows)

### Estimated Implementation Size

| Component | Complexity | Files |
|-----------|-----------|-------|
| MeshImporter (minimal) | Medium | `core/asset/importers/mesh_importer.h/cpp` |
| TextureImporter (minimal) | Small | `core/asset/importers/texture_importer.h/cpp` |
| .uzmesh runtime loader | Small | `resource_system/mesh/mesh_loader.h/cpp` |
| .uztexture runtime loader | Small | `resource_system/texture/texture_loader.h/cpp` |
| uzcook registration | Trivial | Add 2 Register() calls to uzcook main |

## 6. Recommended Asset Pipeline Milestone Order

```
[DONE] 1.2  RHI + IAssetImporter framework + ShaderImporter + uzcook scaffold
[DONE] 1.5.0 ShaderImporter complete (DXC, cache, hot-reload)

[NOW]  Pre-1.3.0 Sprint:
         - MeshImporter v1 (fastgltf, basic .uzmesh, cache optimization only)
         - TextureImporter v1 (stb_image, uncompressed RGBA8, box mips)
         - .uzmesh runtime GPU loader
         - .uztexture runtime GPU loader

[1.3.1] Deferred Pipeline Sprint:
         - TextureImporter v2: BC7 (albedo), BC5 (normals), BC6H (HDR)
         - ispc_texcomp integration
         - Kaiser filter for sRGB mipmaps
         - MeshImporter v1.5: tangent generation (mikktspace)

[1.4]   Scene System Sprint:
         - .uzimport hot-reload (FileWatcher → uzcook re-import on source change)
         - Import dependency graph (texture → material → mesh → scene)
         - .uzmat and .uzscene importers/loaders

[1.5.1] Animation Sprint:
         - MeshImporter v2: LOD generation (meshoptimizer simplify), skinned mesh
         - AnimationImporter (.gltf animations → .uzanim)
         - SkeletonImporter (.gltf skeleton → .uzskeleton)

[1.5.2] Audio Sprint:
         - AudioImporter (WAV/OGG/MP3 → .uzaudio, streaming for >2MB)

[1.5-Build] Packager Sprint:
         - .uzpak writer in uzcook (--pak flag)
         - .uzmanifest generation
         - Parallel cook jobs (job system integration)
         - Delta pak patching (--previous-manifest --delta-only)
         - PakVFS mount + runtime load from .uzpak

[2.5+]  Advanced:
         - FBX support (ufbx)
         - Virtual texturing
         - Normal map baking (xatlas + GPU rasterization)
         - Texture atlasing for bindless
```

## 7. What Each Rendering Milestone Needs from Asset Pipeline

| Rendering Milestone | Minimum Asset Pipeline |
|--------------------|----------------------|
| 1.3.0 Triangle to Screen | MeshImporter v1 (.uzmesh load) + TextureImporter v1 (RGBA8) |
| 1.3.1 Deferred Pipeline | TextureImporter v2 (BC7 albedo, BC5 normals) + mip chain |
| 1.3.2 Post-Processing | TextureImporter v2 + BC6H for HDR LUTs/cubemaps |
| 1.3.3 Advanced (DLSS, VSM) | MeshImporter v1.5 (tangents for normal mapping) |
| 1.4 Scene System | .uzimport hot-reload + import dependency graph |
| 1.5.1 Animation | MeshImporter v2 (skinned mesh, bone weights) |
| 1.5.2 Audio | AudioImporter v1 |
| Phase 1.5 Packager | ALL importers complete + .uzpak + .uzmanifest |

## 8. Key Risks and Observations

1. **Format stabilization risk**: The .uzmesh spec in the docs uses `magic = 'UZMS'` (0x5A5B4D53) while the system prompt spec says `magic = "UZMH"`. These must be reconciled before writing the binary reader/writer pair. Pick one and commit.

2. **ispc_texcomp build complexity**: ISPC texcomp requires ISPC compiler in the build. This is a non-trivial build dependency — budget extra time for vcpkg integration. Consider using DirectXTex as fallback for BC7 in early stages.

3. **fastgltf vs assimp**: The specs consistently say fastgltf (0.7.1+). Do not use assimp — it's slower and adds a large dependency. FBX via ufbx comes in Phase 2.5 only.

4. **meshoptimizer simplify vs encode**: Two separate meshoptimizer calls are needed — `meshopt_optimizeVertexCache` (always, even for MVP) and `meshopt_simplify` (LOD generation, Phase 2+). Don't conflate them.

5. **Audio streaming threshold**: The 2MB threshold (in-memory vs streaming) is from the system prompt spec. Verify this is appropriate for the target platform memory budget once audio is implemented.
