# Proposal: phase4-virtual-texturing

## Why

Open-world terrain, large architectural surfaces, and photogrammetry assets require extremely high-resolution textures (16K–64K). Storing these fully in GPU memory is impossible on any current hardware. Virtual texturing solves this by dividing textures into 128×128 tiles and streaming only the tiles visible in the current frame — similar to Virtual Shadow Maps but for albedo/normal/roughness textures. The result: effectively unlimited texture resolution with GPU memory proportional to visible tile count.

## What Changes

- **Offline**: `uzpak` asset pipeline extension — transcodes source textures into tiled virtual texture format (`.uvtex`)
- **`src/engine/renderer/virtual_texture_manager.h/cpp`** — tile cache, page table, feedback processing
- **`shaders/vt_feedback.hlsl`** — compute shader marking required tiles during geometry pass
- **`shaders/vt_resolve.hlsl`** — resolve virtual address to physical tile address in pixel shader
- **`shaders/include/virtual_texture.hlsli`** — helper functions for sampling virtual textures

## Impact

- Affected specs: `docs/specs/renderer.md` (virtual texturing section), `docs/specs/asset-pipeline.md` (`.uvtex` format)
- Dependencies: `phase2-source-asset-db` (asset cook pipeline), `phase0-async-streaming-design` (async tile streaming)
- Breaking change: NO — opt-in via `CVirtualTexture` component; regular textures still work
- User benefit: photorealistic terrain at any zoom level; no texture resolution budget; unique surface detail everywhere
