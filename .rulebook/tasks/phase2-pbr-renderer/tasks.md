## 1. Render Target Infrastructure

- [ ] 1.1 Create `src/renderer/render_targets.h/cpp` — manage GBuffer, depth, HDR, LDR targets
- [ ] 1.2 GBuffer0: `R8G8B8A8_UNORM` — albedo (RGB) + AO (A)
- [ ] 1.3 GBuffer1: `R16G16B16A16_FLOAT` — world normal (XYZ) + roughness (W)
- [ ] 1.4 GBuffer2: `R8G8B8A8_UNORM` — metallic (R), emissive mask (G), flags (BA)
- [ ] 1.5 Depth: `D32_FLOAT`
- [ ] 1.6 HDR color: `R11G11B10_FLOAT`
- [ ] 1.7 Resize all targets on `renderer.setResolution` IPC call

## 2. G-Buffer Pass

- [ ] 2.1 Create `shaders/gbuffer.hlsl` — VSMain + PSMain, output GBuffer0/1/2
- [ ] 2.2 PSMain writes: albedo, normal (world-space), roughness, metallic
- [ ] 2.3 Normal maps decoded from tangent space: requires tangent attribute in vertex buffer
- [ ] 2.4 Vertex layout: POSITION (float3), NORMAL (float3), TANGENT (float4), TEXCOORD (float2)
- [ ] 2.5 G-Buffer pass PSO: depth test=LESS_EQUAL, depth write=true, no blending

## 3. Deferred Lighting Pass

- [ ] 3.1 Create `shaders/deferred_lighting.hlsl` — fullscreen triangle, GGX BRDF
- [ ] 3.2 Reconstruct world position from depth + inverse view-proj
- [ ] 3.3 `PBR_DirectLight` function: GGX distribution, Smith geometry, Schlick Fresnel
- [ ] 3.4 Directional light from per-frame CBV
- [ ] 3.5 Point lights from structured buffer (StructuredBuffer<PointLight>), max 256
- [ ] 3.6 Ambient from constant ambient color (IBL replaces this in step 6)

## 4. Cascaded Shadow Maps (CSM)

- [ ] 4.1 Create `src/renderer/shadow_system.h/cpp` — compute cascade splits, build shadow matrices
- [ ] 4.2 Shadow atlas: 2048×2048 texture (D32_FLOAT), 3 cascade tiles (each 2048×682)
- [ ] 4.3 Create `shaders/shadow_depth.hlsl` — depth-only VS, no PS
- [ ] 4.4 Cascade selection in lighting shader: compare depth vs cascade far-plane splits
- [ ] 4.5 PCF 3×3: sample shadow map 9 times, average for soft shadows
- [ ] 4.6 Cascade debug visualization: overlay cascade regions in different colors

## 5. PBR Materials

- [ ] 5.1 Create `src/renderer/material.h/cpp` — `Material` struct from `.uzmat`
- [ ] 5.2 Texture upload: `src/renderer/texture_manager.h/cpp` — load PNG → DX12 texture
- [ ] 5.3 Add `stb_image` to vcpkg.json (PNG/JPG decode)
- [ ] 5.4 Material CBV: albedo (float3), roughness, metallic — bound at b1
- [ ] 5.5 Descriptor table: albedo_tex t0, normal_tex t1, roughness_tex t2, metallic_tex t3

## 6. Image-Based Lighting (IBL)

- [ ] 6.1 Load HDR equirectangular skybox (`.hdr` file) via stb_image
- [ ] 6.2 Convert equirect → cubemap (compute shader)
- [ ] 6.3 Diffuse irradiance: convolve cubemap (compute shader)
- [ ] 6.4 Specular pre-filtered: generate 5 roughness mip levels (compute shader)
- [ ] 6.5 BRDF LUT: 512×512 pre-computed GGX BRDF integral
- [ ] 6.6 IBL term in lighting shader: diffuse irradiance * kD * albedo + specular pre-filter * BRDF LUT
- [ ] 6.7 `scene.setProperty` on skybox node triggers IBL recompute

## 7. Skybox Rendering

- [ ] 7.1 Create `shaders/skybox.hlsl` — sample cubemap, depth test = EQUAL (render last)
- [ ] 7.2 Skybox node type: `Skybox` with `hdr_texture` property
- [ ] 7.3 Renderer draws skybox after all opaque geometry

## 8. Post-Process Stack

- [ ] 8.1 Create `shaders/post/bloom.hlsl` — downsample + threshold + additive upsample (6 iterations)
- [ ] 8.2 Create `shaders/post/tonemap.hlsl` — ACES Filmic curve, gamma correction
- [ ] 8.3 Create `shaders/post/ssao.hlsl` — 16 hemisphere samples, blur pass
- [ ] 8.4 Post-process passes enabled/disabled via `renderer.*` IPC

## 9. Render Stats

- [ ] 9.1 GPU timing queries: shadow pass, G-Buffer, lighting, post-process ms
- [ ] 9.2 CPU stats: cull count, draw call count, triangle count
- [ ] 9.3 `renderer.stats` IPC handler returns all stats as JSON

## 10. Testing

- [ ] 10.1 Load a PBR sphere scene (metallic/roughness grid): correct BRDF shading visible
- [ ] 10.2 Directional light with shadows: no shadow acne, smooth PCF edges
- [ ] 10.3 IBL: dielectric (roughness=0.5, metallic=0) shows correct diffuse/specular split
- [ ] 10.4 Bloom: bright emissive sphere glows
- [ ] 10.5 SSAO: concave surfaces show ambient occlusion
