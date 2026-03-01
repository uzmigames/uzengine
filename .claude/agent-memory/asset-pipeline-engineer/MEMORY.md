# Asset Pipeline Engineer — Persistent Memory

## Project: UzEngine
CMake + vcpkg. Asset importers → .import/ artifacts. .uzpak for shipping.

## Key Paths
- `docs/specs/asset-pipeline.md` — complete importer spec
- `docs/specs/build-system.md` — UzBuild.exe, .uzpak format
- `vcpkg.json` — dependency manifest (always update here, not CMakeLists directly)
- `src/engine/assets/` — ResourceManager, IAssetImporter, importers/

## vcpkg Dependencies (current)
- fastgltf ≥ 0.7.1 (glTF import)
- ufbx ≥ 0.12.0 (FBX import)
- meshoptimizer ≥ 0.21 (mesh optimization + LODs)
- ispc-texcomp (BC7/BC5/BC4 texture compression)
- stb (image load)
- zstd ≥ 1.5.5 (.uzpak compression)

## Texture Formats
- Albedo/diffuse: BC7
- Normal maps: BC5 (RG, reconstruct Z)
- Single channel (roughness/metallic/AO): BC4
- HDR environment: BC6H
- Small UI (< 64x64): uncompressed RGBA8

## .import.json Rules
- Written atomically: write to .tmp then rename
- Source hash: FNV-1a of source file bytes
- Lives alongside source file
- .import/ directory is gitignored (always regeneratable)

## .uzmesh Format
- Magic: "UZMH", version 1
- Vertex streams: positions, normals, UVs, tangents, blend_weights
- LOD table with screen_coverage thresholds
