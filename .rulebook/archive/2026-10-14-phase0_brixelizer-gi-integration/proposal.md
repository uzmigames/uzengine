# Proposal: AMD FidelityFX Brixelizer GI Integration (Phase 0)

## Why

UzEngine's indoor lighting lacks spatially-varying indirect illumination. The current RadianceCache (16^3 SH probes + SDF ray march) provides basic probe-based GI but has limited quality — low probe density, no specular GI, and slow convergence. The Sponza atrium renders with uniform ambient that doesn't vary between sunlit windows and deep corridors.

AMD FidelityFX Brixelizer GI (MIT license, SDK 1.1.4) provides production-quality real-time GI via sparse SDF voxelization with cascaded resolution. It outputs both diffuse and specular indirect lighting, includes a built-in denoiser, and works on any DX12 GPU without requiring DXR hardware. The SDK ships with all HLSL compute shaders and a C API — we integrate it as a library, not rewrite it.

The SDK is already downloaded at `runtime/thirdparty/amd-fidelityfx-1.1/` (3 C++ source files, 50 HLSL shaders, DX12 backend).

## What Changes

### New Systems

1. **FFX SDK Build Integration** — compile `ffx_dx12.cpp`, `ffx_brixelizer.cpp`, `ffx_brixelizer_raw.cpp`, `ffx_brixelizergi.cpp` as part of the runtime static library. Pre-compile 50 Brixelizer HLSL shaders into shader blobs via DXC.

2. **BrixelizerGIPass** (`engine/renderer/gi/brixelizer_gi_pass.h/.cpp`) — owns `FfxBrixelizerContext` + `FfxBrixelizerGIContext`. Manages lifecycle (create/destroy), per-frame geometry registration, SDF bake, and GI dispatch. Outputs two textures: `diffuseGI` (RGBA16F) and `specularGI` (RGBA16F).

3. **Geometry Registration Bridge** — translates UzEngine's `DrawList` mesh entries (vertex buffer SRV, index buffer SRV, transform, AABB) into `FfxBrixelizerInstanceDescription` structs for Brixelizer's SDF voxelizer.

4. **GI Composite** — wire `diffuseGI` and `specularGI` outputs into `deferred_lighting_ps.hlsl` as additive indirect terms, gated by `r.BrixelizerGI` CVar.

### Refactoring

- `build.zig` — add FFX SDK source files to the compile unit, add include paths
- `deferred_lighting_ps.hlsl` — add Brixelizer GI sampling (diffuseGI SRV + specularGI SRV)
- `frame_constants.cpp` — populate Brixelizer GI SRV indices in PerFrameCB
- `frame_gi_passes.cpp` — dispatch BrixelizerGIPass in the render frame sequence

## Impact
- Affected specs: `docs/specs/rendering/renderer.md`, `docs/specs/rendering/ibl-pipeline.md`
- Affected code: `runtime/engine/renderer/gi/`, `runtime/shaders/runtime/lighting/`, `runtime/build.zig`
- Breaking change: NO — additive feature, gated by CVar (default off)
- User benefit: Production-quality diffuse + specular indirect lighting in indoor/outdoor scenes
- Enables: Baked lightmap replacement, dynamic GI for moving objects, specular GI reflections
