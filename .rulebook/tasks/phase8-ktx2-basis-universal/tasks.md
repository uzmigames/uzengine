## 1. basisu Dependency Integration

- [ ] 1.1 Add `basisu` (Basis Universal) as a vcpkg dependency in `vcpkg.json` with a pinned version
- [ ] 1.2 Update `CMakeLists.txt` to find and link `basisu` encoder into the asset pipeline target and `basisu` transcoder into the runtime engine target
- [ ] 1.3 Verify `#include <basisu_comp.h>` resolves for the import pipeline and `#include <transcoder/basisu_transcoder.h>` resolves for the runtime loader

## 2. Texture Importer Changes

- [ ] 2.1 Modify `src/assets/texture_importer.cpp` to encode PNG/TGA/HDR source textures to `.ktx2` using the basisu encoder instead of generating DDS/KTX platform-specific output
- [ ] 2.2 Add `enum class TextureCompressionMode { ETC1S, UASTC }` to `src/assets/texture_importer.h`; default `ETC1S` for color textures, `UASTC` for normal maps and HDR
- [ ] 2.3 Read `compression` setting from `.uzproj` `[asset.texture]` section; apply as the default `TextureCompressionMode` for all textures
- [ ] 2.4 Support per-texture override via `.uzasset` metadata sidecar file; a `compression = "uastc"` entry in the sidecar overrides the project default
- [ ] 2.5 Map `--texture-quality low/medium/high/lossless` CLI flag to ETC1S/UASTC quality presets in `tools/uzpak/texture_importer_cli.cpp`

## 3. Runtime Texture Loader

- [ ] 3.1 Create `src/assets/texture_loader_ktx2.cpp` implementing `TextureLoader::load_ktx2(const char* path)` using `basisu_transcoder::transcode_image_level()`
- [ ] 3.2 At load time, query GPU format support via `D3D12_FEATURE_DATA_FORMAT_SUPPORT`; select BC7 for DX12, BC1/BC3 as fallback for GPUs without BC7 support
- [ ] 3.3 Allocate a GPU-resident transcoded texture in the D3D12 descriptor heap and upload via `UpdateSubresources()`
- [ ] 3.4 Cache the transcoded GPU texture handle keyed by asset path in `TextureLoader`; return the cached handle on subsequent `load_ktx2()` calls to avoid re-transcoding

## 4. uzpak Format Update

- [ ] 4.1 Update `src/assets/uzpak.cpp` to store `.ktx2` files instead of `.dds`/`.ktx` files; update the pak entry format version constant
- [ ] 4.2 Maintain a legacy compatibility path in `uzpak` that can still read and serve old BC7 DDS entries; log a deprecation warning when a legacy entry is served

## 5. Migration Tool

- [ ] 5.1 Create `tools/uzpak/migrate_textures.cpp` implementing `uzpak migrate-textures <pak_file>` command
- [ ] 5.2 The migration tool reads each DDS texture from the pak, encodes it to `.ktx2` via basisu, and writes a new pak replacing the DDS entry with the `.ktx2` entry
- [ ] 5.3 Print a progress line per texture processed: `Migrating <name>... done (X KB -> Y KB)`

## 6. Testing

- [ ] 6.1 Create `tests/assets/test_ktx2_import.cpp` testing `texture_importer` encodes a 4x4 RGBA PNG to a valid `.ktx2` file; verify the output file parses without error using `basisu_transcoder`
- [ ] 6.2 Test ETC1S encoding: verify output `.ktx2` has supercompression scheme `KTX_SS_BASIS_LZ`
- [ ] 6.3 Test UASTC encoding: verify output `.ktx2` has supercompression scheme `KTX_SS_BASIS_UASTC`
- [ ] 6.4 Test `load_ktx2()` transcodes to BC7 format and returns a non-null texture handle
- [ ] 6.5 Test cache hit: calling `load_ktx2()` twice with the same path returns the same GPU handle and does not call `transcode_image_level()` a second time
- [ ] 6.6 Test BC1/BC3 fallback path: simulate a GPU without BC7 support; verify the transcoder falls back to BC1
- [ ] 6.7 Test legacy DDS compatibility path: load a pak containing a DDS entry and verify a valid texture is returned with a deprecation warning logged
- [ ] 6.8 Test `migrate_textures` tool: run on a test pak containing a DDS entry; verify the output pak contains a `.ktx2` entry and the DDS entry is removed
- [ ] 6.9 Test per-texture sidecar override: texture with `.uzasset` specifying `compression = "uastc"` is encoded as UASTC even when project default is ETC1S
- [ ] 6.10 Achieve >= 95% line and branch coverage across `texture_importer.cpp`, `texture_loader_ktx2.cpp`, and `migrate_textures.cpp`

## 7. Documentation

- [ ] 7.1 Update `docs/specs/asset-pipeline.md` documenting `.ktx2` as the new texture storage format, compression modes, and `.uzasset` sidecar override syntax
- [ ] 7.2 Update `docs/specs/renderer.md` documenting runtime KTX2 transcoding, format selection logic, and the GPU texture cache
- [ ] 7.3 Update `docs/specs/build-system.md` documenting the breaking change to `.uzpak` format, migration instructions using `uzpak migrate-textures`, and the legacy compatibility path
