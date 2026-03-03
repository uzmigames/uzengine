# Proposal: phase2-texture-tools

## Why

Raw PNG, TGA, HDR, and EXR images cannot be used as GPU textures efficiently. They are either uncompressed or use CPU-side compression formats the GPU cannot sample natively, they have no mipmap chain, and they consume 4x to 8x more VRAM than a block-compressed equivalent. Without a texture import pipeline, every texture loaded by the engine wastes VRAM and memory bandwidth, and the absence of mipmaps causes visible aliasing artifacts when surfaces are viewed at distance or at a glancing angle.

Block compression formats supported natively by modern GPUs — BC1 through BC7 for LDR content, BC6H for HDR — reduce texture memory to 4–8 bits per pixel while preserving perceptual quality. Generating these with ispc_texcomp (Intel's ISPC-based compressor) is significantly faster than CPU-fallback encoders and produces quality on par with squish. The compressed textures are stored in DDS containers, which DX12 can load and bind without any runtime decompression.

Import settings must be per-texture and persistent. A `.import.json` sidecar file next to each source image stores the chosen format, mip filter, and sRGB flag so that re-importing after edits produces deterministic output. The asset registry monitors source file modification times and triggers automatic reimport, keeping the asset cache consistent without manual intervention.

## What Changes

- Integrate ispc_texcomp (via vcpkg or bundled) as the block compression backend; implement `src/assets/texture_importer.cpp` that reads PNG, TGA, HDR, and EXR source files using stb_image and tinyexr
- Compress albedo and base color textures to BC7 (4bpp RGBA, high quality); compress normal maps to BC5 (RG-only 4bpp, reconstructs Z in shader, preserves precision); compress roughness/metallic/AO packed channels to BC4/BC5; compress HDR environment maps and emissive textures to BC6H (unsigned, 8bpp half-float); fall back to BC1 for opaque diffuse-only textures when quality allows
- Generate a full mipmap chain before compression using a Kaiser filter (configurable to box/triangle via import settings); store all mip levels in a single DDS container so DX12 `CreateCommittedResource` + `D3D12_SUBRESOURCE_DATA` can upload all mips in one call
- Implement `.import.json` sidecar schema per source texture: fields `format` (auto/bc1/bc3/bc4/bc5/bc7/bc6h/rgba8), `srgb` (bool, controls linearization before compression), `generate_mipmaps` (bool), `mip_filter` (kaiser/box/triangle), `max_dimension` (power-of-two cap)
- Asset registry watches source file mtime; on change, re-runs importer and invalidates any GPU handle referencing the old DDS; hot-reload notifies the renderer to rebind the updated texture
- Add a texture preview panel in the VSCode extension: shows the compressed texture rendered into a JPEG frame, with channel isolation toggles (R, G, B, A, normal XY reconstruction), a mip level slider (0 to max mip), and a metadata readout (source dimensions, output format, mip count, compressed size in bytes)
- Expose `asset.reimport` IPC command accepting a source path; runs the importer synchronously and returns the output DDS path and stats
- Add unit tests in `tests/assets/` covering: correct format selection per texture type (sRGB albedo → BC7, normal map → BC5, HDR → BC6H), mip count formula, sidecar round-trip (write then read back), and mtime-triggered invalidation
- Update `docs/specs/asset-pipeline.md` with the texture import flow diagram, `.import.json` schema reference, format selection heuristic table, and DDS layout description

## Impact

- Affected specs: docs/specs/asset-pipeline.md, docs/specs/renderer.md (texture binding in GBuffer pass)
- Affected code: src/assets/texture_importer.cpp, src/assets/AssetRegistry.cpp, src/renderer/TextureManager.cpp, vscode-extension/src/panels/TexturePreviewPanel.ts
- Breaking change: NO for development workflow (importer runs transparently on first asset use); scene files that reference raw PNG paths must be updated to use `res://` asset paths pointing to registered textures
- User benefit: Textures are automatically compressed to GPU-native formats on import, cutting VRAM usage by 4–8x versus raw PNG, eliminating mipmap aliasing, and letting artists verify compression quality in the VSCode preview panel before committing assets
