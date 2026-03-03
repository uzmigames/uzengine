# Proposal: phase3-ktx2-basis-universal

## Why
Current texture compression uses platform-specific formats — BC7 for DX12/PC and ASTC for mobile — which requires running the asset import pipeline separately per target platform and storing duplicate texture assets in `.uzpak` archives. This doubles texture storage requirements for cross-platform games and complicates the build pipeline. Basis Universal / KTX2 is a universally supercompressed intermediate format that transcodes to any GPU-native block compression format (BC7, ASTC, ETC2, BC1/BC3) at load time using a small, fast software transcoder, enabling a single texture asset to serve all platforms.

## What Changes
- Integrate `basisu` library (Basis Universal encoder + transcoder) as a vcpkg dependency
- Asset import pipeline: source PNG/TGA/HDR textures are encoded to `.ktx2` (Basis ETC1S or UASTC supercompression depending on quality setting) during import; raw platform-specific DDS/KTX output is no longer generated at import time
- `.uzpak` stores `.ktx2` files only; platform-specific format selection is deferred to runtime
- Runtime texture loader: at first access, `TextureLoader::load_ktx2()` calls `basisu_transcoder::transcode_image_level()` selecting the target format based on GPU capability query (`D3D12_FEATURE_DATA_FORMAT_SUPPORT`): BC7 for DX12, ASTC for hypothetical mobile backends, BC1/BC3 as fallback
- Transcoded texture data is cached in a per-session GPU-resident cache; re-transcoding on second access is avoided
- `.uzproj` `[asset.texture]` section: `compression = "etc1s"` (smaller, faster) or `"uastc"` (higher quality for normal maps and HDR); per-texture override via metadata sidecar `.uzasset`
- Import pipeline CLI flag `--texture-quality` accepts `low / medium / high / lossless` which maps to ETC1S / UASTC quality presets
- Existing BC7 DDS textures in legacy projects are still loadable via a compatibility path; migration tool `uzpak migrate-textures` re-encodes them to KTX2

## Impact
- Affected specs: `docs/specs/asset-pipeline.md`, `docs/specs/renderer.md`, `docs/specs/build-system.md`
- Affected code: `src/assets/texture_loader.cpp`, `src/assets/texture_importer.cpp`, `src/assets/texture_importer.h`, `src/assets/uzpak.cpp`, `tools/uzpak/migrate_textures.cpp`, `CMakeLists.txt` (basisu vcpkg dep)
- Breaking change: YES — `.uzpak` format changes; existing paks must be rebuilt via `uzpak migrate-textures`; BC7 DDS loose files are still supported via compatibility path
- User benefit: One texture asset works on all target platforms; `.uzpak` size is reduced by up to 40% compared to storing both BC7 and ASTC variants; import pipeline runs once instead of once per platform
