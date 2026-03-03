# Proposal: phase2-pbr-renderer

## Why

The current UzEngine renderer draws a hard-coded single-material pyramid using a placeholder solid-color shader. There is no material system, no lighting model, and no shadow support. Every mesh imported via phase2-fbx-import or phase2-gltf-import would render as a flat untextured triangle soup, making the engine unusable for any real game content preview or development workflow.

A physically-based rendering pipeline is the baseline visual quality requirement for a modern game engine. Deferred shading decouples geometry processing from lighting so that the cost of evaluating hundreds of lights scales with screen resolution rather than mesh polygon count. The Cook-Torrance GGX BRDF with metallic-roughness parameterization is the industry standard adopted by glTF 2.0, Unreal Engine, and Unity, meaning artist-authored PBR materials import correctly without remapping. IBL ambient lighting driven by an HDR skybox fills in indirect lighting that would otherwise make scenes look flat and uniformly dark.

Tone mapping (ACES Filmic) is required because HDR lighting values must be mapped to the 0–1 display range. Without it, overexposed surfaces clip to white and the rendered image looks blown out. The emissive channel allows surfaces such as screens, lights, and glowing effects to punch through the lighting calculation, which is used throughout game content.

## What Changes

- Implement the GBuffer pass: render all opaque geometry into 4 MRT textures — RT0: albedo (RGB) + AO (A) in R8G8B8A8_UNORM, RT1: world-space normal (RGB) in R16G16B16A16_SNORM, RT2: roughness (R) + metallic (G) + emissive intensity (B) in R8G8B8A8_UNORM, plus a D32_FLOAT depth/stencil buffer
- Write `shaders/gbuffer_vs.hlsl` and `shaders/gbuffer_ps.hlsl` that sample albedo, normal (with tangent-space TBN), roughness, metallic, and emissive textures from the material's texture slots; normal maps decoded from BC5 with Z reconstruction
- Implement the deferred lighting pass as a full-screen triangle: reads GBuffer textures, reconstructs world position from depth + inverse VP matrix, evaluates Cook-Torrance GGX BRDF (D: GGX/Trowbridge-Reitz, G: Smith-Schlick-GGX, F: Schlick approximation) per light
- Implement IBL ambient: load HDR equirectangular skybox, precompute diffuse irradiance cubemap (32x32) and specular pre-filtered environment map (up to 512x512, 5 mip levels) offline; use split-sum approximation BRDF LUT (128x128) for specular ambient; sample these in the lighting pass for the ambient term
- Add shadow map sampling in the lighting pass: directional light uses a 4096x4096 D32 shadow map (cascaded shadow maps with 3 cascades come in phase2-shadow-maps); for this task a single cascade orthographic shadow map is sufficient; PCF 3x3 kernel for soft edges
- Add tone mapping as the final post-process step: ACES Filmic curve applied in a full-screen pass before JPEG readback; expose `renderer.exposure` float uniform via IPC
- Implement `src/renderer/material.cpp` — `Material` struct with texture handle slots (albedo, normal, roughness, metallic, emissive) and scalar fallback values; serialized in `.uzmat` INI-like format matching the existing file format convention
- Register a `CMeshRenderer` component on scene nodes that references a `Handle<Mesh>` and a `Handle<Material>`; the GBuffer pass iterates all `CMeshRenderer` components in the scene
- Add unit tests verifying GBuffer PSO creation, material serialization/deserialization round-trip, and that the lighting pass binds all GBuffer SRVs without D3D12 validation errors
- Update `docs/specs/renderer.md` with the GBuffer layout table, BRDF equations, IBL precompute pipeline, and material file format reference

## Impact

- Affected specs: docs/specs/renderer.md, docs/specs/shader-system.md, docs/specs/asset-pipeline.md (material assets)
- Affected code: src/renderer/renderer.cpp, src/renderer/gbuffer_pass.cpp, src/renderer/lighting_pass.cpp, src/renderer/material.cpp, shaders/gbuffer_vs.hlsl, shaders/gbuffer_ps.hlsl, shaders/lighting_ps.hlsl, shaders/tonemap_ps.hlsl
- Breaking change: YES — the existing placeholder shader and pyramid geometry are replaced; any code referencing the old single-PSO renderer must be updated to use the GBuffer pass pipeline
- User benefit: All imported meshes with PBR materials render correctly with physically accurate lighting, shadows, and IBL ambient, producing results that match the artist's intent from DCC tools and making the engine viable for real game content development
