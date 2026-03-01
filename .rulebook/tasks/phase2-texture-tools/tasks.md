## 1. Dependencies

- [ ] 1.1 Add `directxtex` to vcpkg.json
- [ ] 1.2 Add `stb` to vcpkg.json (stb_image for PNG/JPG decode)
- [ ] 1.3 Confirm libjpeg-turbo already in vcpkg.json (for JPEG in viewport stream)

## 2. Texture Importer (`src/assets/texture_importer.h/cpp`)

- [ ] 2.1 `TextureImporter::import(source_path, settings)` → outputs `.dds`
- [ ] 2.2 Load source: stb_image for PNG/JPG/TGA, DirectXTex LoadFromHDRFile for .hdr
- [ ] 2.3 Detect texture type from filename convention: `*_normal.*`, `*_roughness.*`, `*_metallic.*`
- [ ] 2.4 Albedo / generic: BC7 `UNORM_SRGB` if sRGB, `UNORM` otherwise
- [ ] 2.5 Normal map: BC5 `UNORM` (RG channels only — Z reconstructed in shader)
- [ ] 2.6 Roughness / Metallic / AO: BC4 `UNORM` (single channel)
- [ ] 2.7 HDR: BC6H `UF16`
- [ ] 2.8 Generate full mip chain: `DirectX::GenerateMipMaps()` with box filter
- [ ] 2.9 Compress: `DirectX::Compress()` — CPU BC7 encoder (parallel, TEX_COMPRESS_PARALLEL)
- [ ] 2.10 Save `.dds` via `DirectX::SaveToDDSFile()`
- [ ] 2.11 Write `.import.json` with: source_hash, format, width, height, mip_count, size_bytes

## 3. GPU Texture Upload (`src/renderer/texture_manager.h/cpp`)

- [ ] 3.1 `TextureManager::load(dds_path)` → uploads to DX12 DEFAULT heap
- [ ] 3.2 Staging: UPLOAD heap → copy command → DEFAULT heap → resource barrier
- [ ] 3.3 SRV descriptor registered in CBV_SRV_UAV heap
- [ ] 3.4 Texture dedup by res:// path — no duplicate GPU uploads
- [ ] 3.5 Texture unload: decrements refcount, releases DX12 resources when zero

## 4. Normal Map Shader Fix

- [ ] 4.1 Update PBR fragment shader: reconstruct Z from BC5 RG normal map
  ```hlsl
  float3 n  = NormalTex.Sample(s, uv).rgb * 2.0 - 1.0;
  n.z       = sqrt(max(0.0, 1.0 - dot(n.xy, n.xy)));  // BC5 has only RG
  ```
- [ ] 4.2 Test with a baked normal map asset

## 5. Texture Preview Panel (VSCode)

- [ ] 5.1 Create `src/texture-preview-panel.ts` — CustomEditorProvider for .dds/.png/.ktx2
- [ ] 5.2 Webview: WebGL canvas renders the texture with preview shader
- [ ] 5.3 Channel buttons: R / G / B / A / RGB (toggle which channels are shown)
- [ ] 5.4 Mip selector: dropdown `[Mip 0] [Mip 1] ... [Mip N]`
- [ ] 5.5 Alpha checkerboard background when A channel or transparency visible
- [ ] 5.6 Zoom: scroll to zoom, drag to pan
- [ ] 5.7 Info bar: format, resolution, mip count, compressed size
- [ ] 5.8 Register as custom editor for `.dds` files in package.json

## 6. Testing

- [ ] 6.1 Import wood_albedo.png → BC7 DDS, correct colors in viewport
- [ ] 6.2 Import wood_normal.png → BC5 DDS, correct normal map shading
- [ ] 6.3 Import sky.hdr → BC6H DDS, skybox rendered correctly
- [ ] 6.4 Texture preview: channel isolation R only shows red channel
- [ ] 6.5 Mip selector: mip 3 shows correct downscaled version
- [ ] 6.6 Incremental import: change PNG → re-import triggered, hash updated
