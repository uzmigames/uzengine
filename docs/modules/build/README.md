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
- [../../specs/asset-pipeline.md](../../specs/asset-pipeline.md) — full asset pipeline spec
- [../../specs/build-system.md](../../specs/build-system.md) — full build system spec
