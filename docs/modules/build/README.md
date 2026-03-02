# Module: Build

> **Location**: `src/build/` + `tools/UzBuild/`
> **Output**: `UzBuild.exe` (standalone CLI tool)
> **Language**: C++23
> **GPU required**: No — headless. DXC used via API, not via D3D12 device.

---

## Responsibility

The Build module is the **offline packaging pipeline**. It transforms raw project files (source assets, shaders, scenes) into a self-contained, shippable game package. It runs as a separate CLI tool and is never part of the game process itself.

---

## Pipeline Stages

```
UzBuild.exe --project ./mygame --config Release --output ./dist

Stage 1: COOK    ─── import source assets → engine-native formats
Stage 2: PACK    ─── assemble all cooked outputs → game.uzpak
Stage 3: COMPILE ─── build UzEngine.exe with UZ_SHIPPING=1
Stage 4: STAGE   ─── assemble the release directory
Stage 5: PACKAGE ─── (optional) NSIS installer or zip archive
```

Each stage is incremental: files are skipped if their source hash matches the last cook. A full re-cook is triggered with `--force`.

---

## Stage Details

### Stage 1: Cook

Converts source assets into engine-native runtime formats. Output goes to `project/.import/`.

| Source | Output | Tool |
|--------|--------|------|
| `.glb` / `.gltf` | `.uzmesh` + `.uzmat` references | fastgltf + meshoptimizer LODs |
| `.fbx` | `.uzmesh` + `.uzskel` + `.uzanim` | ufbx |
| `.png` / `.jpg` | `.dds` (BC7 UNORM) | DirectXTex |
| `.hdr` | `.dds` (BC6H UF16) | DirectXTex |
| `.png` (normal map) | `.dds` (BC5 SNORM) | DirectXTex |
| `.hlsl` | `.dxil` | DXC (headless, no D3D12 device) |
| `.glsl` | `.spv` | glslang (future Vulkan) |
| `.wav` | `.ogg` | libvorbis |
| `.uzscene` | `.uzscene` (validated, path-normalized) | Engine scene loader (round-trip) |

**Incremental check**: each source file is hashed (xxhash3). If `hash == last_cook_hash`, skip.

```
src/build/cook/
├── cook_pipeline.h/cpp    — file discovery, dependency graph, incremental check
├── mesh_cook.h/cpp        — GLTF/FBX → .uzmesh, LOD chain
├── texture_cook.h/cpp     — PNG/HDR → BC7/BC5/BC6H DDS (DirectXTex)
├── shader_cook.h/cpp      — HLSL → DXIL (DXC headless API, no device needed)
├── audio_cook.h/cpp       — WAV → OGG (libvorbis)
└── scene_validate.h/cpp   — load + serialize .uzscene (catches parse errors early)
```

### Stage 2: Pack

Assembles all cooked outputs into a single `game.uzpak` archive.

```
src/build/pack/
├── pak_writer.h/cpp       — .uzpak assembler
└── pak_reader.h/cpp       — .uzpak reader (also used at runtime by UZ_SHIPPING)
```

**`.uzpak` format:**

```
[file_0_data compressed with zstd]
[file_1_data compressed with zstd]
...
[file_N_data compressed with zstd]
[directory: N entries × { path_hash:u64, offset:u64, size_compressed:u32, size_raw:u32 }]
[footer: directory_offset:u64, entry_count:u32, magic:u32=0x5A504B55 "UZPK"]
```

- Footer-at-end: entire file can be memory-mapped; directory is read from the end
- Entries sorted by `path_hash` for O(log n) binary search lookup
- No central metadata file needed — hash the `res://` path at runtime to look up the entry

### Stage 3: Compile

Build `UzEngine.exe` with shipping flags.

```cmake
cmake --build . --config Shipping --target UzEngine
# UZ_SHIPPING=1 strips: IPC, FileWatcher, WsServer, JpegEncoder, DXC, importers
# Links against uz_pak_vfs instead of filesystem asset loading
```

### Stage 4: Stage

Assemble the release directory with everything needed to run on a user machine.

```
dist/
├── UzGame.exe                  ← renamed from UzEngine.exe
├── game.uzpak
├── vcredist_x64/               ← MSVC runtime redistributable
└── install_manifest.sha256     ← SHA256 of every file (for update diffing)
```

```
src/build/manifest/
└── manifest_writer.h/cpp      — walks dist/, writes SHA256 per file
```

### Stage 5: Package (optional)

```cmake
cpack -G NSIS   # Windows installer
cpack -G ZIP    # Simple zip
```

---

## Asset Import Details (Phase 2+)

### Mesh Import (GLTF/FBX)

| Input | Processing | Output |
|-------|-----------|--------|
| `.glb` / `.gltf` | Parse with fastgltf; extract meshes, bones, animations | `.uzmesh` (vertex/index buffers), `.uzskel` (bone hierarchy), `.uzanim` (clips) |
| `.fbx` | Parse with ufbx; extract meshes, armatures, takes | Same as above |
| LOD generation | Run meshoptimizer: simplify mesh at 50% / 25% / 12.5% detail | LOD0..LOD3 in `.uzmesh` file, selectable by distance |

**Output `.uzmesh` binary format**:
```
[HEADER]
  version: u32
  lod_count: u32
[LOD_0]
  vertex_count: u32
  vertex_data: Vertex[] (position, normal, uv, color, bone_indices, bone_weights)
  index_count: u32
  index_data: u32[]
  bounds: AABB
[LOD_1..N] (lower detail)
[BONE_PALETTE] (if skinned)
  bone_count: u32
  bone_names: string[]
  bone_parents: u32[]
  bone_local_transforms: mat4[]
```

### Texture Import (PNG/JPG/HDR)

| Input | Format | Processing | Output |
|-------|--------|-----------|--------|
| `.png` (sRGB color) | RGBA/RGB | Resize to POT, mipmap, compress | `.dds` BC7_UNORM |
| `.jpg` (sRGB color) | JPEG | Decompress, convert to PNG workflow | `.dds` BC7_UNORM |
| `.hdr` (linear HDR) | OpenEXR/Radiance | Tonemap to LDR or keep compressed | `.dds` BC6H_UF16 (or RG16F for normals) |
| `.png` (normal map, LINEAR) | RG/RGB (normal XY in RG) | No sRGB, preserve linear | `.dds` BC5_SNORM (RG channels) |

**Mipmap generation**: DirectXTex automatic; filtering for color = linear, for normals = box (to preserve detail).

### Shader Compilation (HLSL)

| Source | Compilation | Output | Use case |
|--------|-------------|--------|----------|
| `shaders/pbr.hlsl` (VS+PS) | DXC with `/O3` + variant flags | `.dxil` bytecode | Deferred PBR pass |
| `shaders/particle_sim.hlsl` (CS) | DXC with `/O3` | `.dxil` bytecode | Particle compute simulation |
| `.hlsl` (pre-compiled) | DXC flags: `/fspv-reflect`, `/Zi` for debug info | `.dxil` + reflection blob | Later PSO cache key generation |

**Variant compilation**: If a shader has `#if SKINNED`, `#if INSTANCED`, etc., all combinations are pre-compiled and indexed by variant flags. Offline variant list stored in `.uzpak` for build validation.

### Audio Import (WAV → OGG)

| Input | Processing | Output |
|-------|-----------|--------|
| `.wav` (any sample rate/channels) | Decode, optional resample to 44.1 kHz, re-encode to OGG Vorbis Q6 | `.uzaudio` (ogg data + metadata) |
| `.mp3` | Decode with libmpg123, resample, re-encode to OGG | `.uzaudio` |
| `.ogg` | Re-encode if quality differs from game standard | `.uzaudio` |

**`.uzaudio` format**:
```
[HEADER]
  version: u32
  sample_rate: u32
  channel_count: u8
  duration_samples: u32
[AUDIO_DATA]
  ogg_blob: bytes (variable length)
```

---

## Shader Variant System (Phase 2)

During cook, all shader variants are pre-compiled and embedded in `.uzpak` with a variant lookup table.

**Variant flags** defined in `src/renderer/shader_variants/variant_flags.h`:
```cpp
enum ShaderVariant : u32 {
    SKINNED    = 1 << 0,    // Skeletal animation
    INSTANCED  = 1 << 1,    // GPU instancing
    EMISSIVE   = 1 << 2,    // Self-emissive material
    PARALLAX   = 1 << 3,    // Parallax occlusion mapping
    // ... more variants
};
```

At build time:
1. Scan all `.hlsl` files for conditional `#if SKINNED`, `#if INSTANCED`, etc.
2. Generate all combinations: 2^N pre-compiled variants per shader
3. Store in `.uzpak` with indexing by (shader_path, variant_flags) → `.dxil` blob

At runtime (editor or shipping):
- Material specifies variant flags
- PSO cache key = (shader_path, variant_flags, blend_state, depth_state)
- Lookup variant `.dxil` from `.uzpak` and compile PSO on-demand (editor) or pre-load (shipping)

---

## `.uzpak` Format Deep Dive

The `.uzpak` is a **footer-first** archive, allowing the entire file to be memory-mapped with the directory read from the end.

**Complete layout**:
```
Offset 0x00000000  [File 0 compressed with zstd]
Offset 0x00001234  [File 1 compressed with zstd]
...
Offset 0x00ABCDEF  [File N compressed with zstd]
Offset 0x00XXXXXX  [Directory Table]
                   [Entry 0: {path_hash, offset, size_compressed, size_raw, crc32}]
                   [Entry 1: ...]
                   [Entry N: ...]
                   [Sort order: path_hash ascending for binary search]
Offset 0x00YYYYYY  [Footer: 16 bytes]
                   - directory_offset: u64
                   - entry_count: u32
                   - magic: u32 = 0x5A504B55 ("UZPK")
```

**Lookup algorithm**:
1. Open `.uzpak`, seek to file end
2. Read footer (16 bytes): extract `directory_offset` and `entry_count`
3. Seek to `directory_offset`, load directory table into memory
4. To find `res://meshes/player.uzmesh`:
   - Compute path_hash = xxhash3("meshes/player.uzmesh")
   - Binary search directory by path_hash → get offset + size
   - Seek to offset, read and decompress zstd blob
5. Verify CRC32 of decompressed data

**Advantages**:
- Single file (no manifest, no external index)
- Entire archive memory-mappable
- Constant-time lookup by hash (no sequential scanning)
- Transparent zstd compression (typically 60-70% of uncompressed size)
- Can be streamed: footer can be downloaded first to determine chunk sizes

---

## CLI Reference

```
UzBuild.exe [options]

  --project <path>     Root of the game project (contains project.uzproj)
  --config <cfg>       Build configuration: Debug | Release | Shipping (default: Shipping)
  --output <path>      Output directory for staged release (default: ./dist)
  --stage <n>          Run only stages 1-N (1=cook, 2=pack, 3=compile, 4=stage, 5=package)
  --force              Force re-cook all assets (ignore incremental cache)
  --jobs <n>           Parallel cook jobs (default: CPU core count)
  --verbose            Print per-file cook actions
```

---

## Source Structure

```
src/build/
├── cook/
│   ├── cook_pipeline.h/cpp
│   ├── mesh_cook.h/cpp
│   ├── texture_cook.h/cpp
│   ├── shader_cook.h/cpp
│   ├── audio_cook.h/cpp
│   └── scene_validate.h/cpp
├── pack/
│   ├── pak_writer.h/cpp
│   └── pak_reader.h/cpp       ← shared with Runtime (UZ_SHIPPING reads .uzpak)
├── manifest/
│   └── manifest_writer.h/cpp
└── stage/
    └── stage_builder.h/cpp    — copies exe, uzpak, vcredist to output dir

tools/UzBuild/
└── main.cpp                   — CLI entry point, argument parsing, stage orchestration
```

---

## Key Invariants

1. **Build never runs at game launch.** It is a separate process (`UzBuild.exe`), never called by Runtime.
2. **`ShaderCook` uses DXC API directly, no D3D12 device.** Shaders can be compiled on a machine without a GPU.
3. **`.import/` is the only output of Cook.** It is gitignored. Deleting it and re-running Build is always safe.
4. **`pak_reader.h/cpp` is the only Build file linked into the shipping Runtime.** Everything else in `src/build/` is UzBuild-only.
5. **Incremental cook is hash-based, not timestamp-based.** Moving or copying a file does not trigger a re-cook if its content is unchanged.

---

## Related Docs

- [../renderer/README.md](../renderer/README.md) — `ShaderCompiler` (the only Renderer subsystem used by Build)
- [../runtime/README.md](../runtime/README.md) — shipping Runtime reads `.uzpak` via `pak_reader`
- [../../specs/asset-pipeline.md](../../specs/asset-pipeline.md) — full asset pipeline spec, import formats
- [../../specs/build-system.md](../../specs/build-system.md) — full build system spec, CLI reference
- [../../specs/shader-variants.md](../../specs/shader-variants.md) — variant flag system, PSO caching
- [../../specs/particle-system.md](../../specs/particle-system.md) — `.uzfx` effect format cooking

## Rulebook Tasks

Phase 2 asset import: [phase2-gltf-import](../../.rulebook/tasks/phase2-gltf-import/), [phase2-fbx-import](../../.rulebook/tasks/phase2-fbx-import/), [phase2-texture-tools](../../.rulebook/tasks/phase2-texture-tools/), [phase2-asset-pak](../../.rulebook/tasks/phase2-asset-pak/), [phase2-shader-variants](../../.rulebook/tasks/phase2-shader-variants/)

Phase 4 build infrastructure: [phase4-build-pipeline](../../.rulebook/tasks/phase4-build-pipeline/)
