## 1. Hi-Z Mip Chain Infrastructure

- [ ] 1.1 Create `src/renderer/post_process/hiz.h` -- HiZState struct with DX12 resource handles for mip chain
- [ ] 1.2 Allocate Hi-Z texture: `R32_FLOAT`, full-res at mip 0, `floor(log2(max(w,h)))` total mip levels
- [ ] 1.3 Create SRV for each mip level (individual mip read) and UAV for each mip level (individual mip write)
- [ ] 1.4 Handle resize on `renderer.setResolution` (recreate Hi-Z texture and all per-mip descriptors)

## 2. Hi-Z Generation Compute Shader

- [ ] 2.1 Create `shaders/post/hiz_generate.hlsl` -- min-depth downsample compute shader (CSMain)
- [ ] 2.2 Mip 0: direct copy from depth buffer (D32_FLOAT SRV -> R32_FLOAT UAV)
- [ ] 2.3 Mip N+1: read 2x2 texels from mip N, store `min(d00, d10, d01, d11)` (conservative, closest to camera)
- [ ] 2.4 Handle non-power-of-two dimensions: clamp texel fetch coords at mip level boundaries
- [ ] 2.5 Compile PSO: `dxc -T cs_6_0 -E CSMain`

## 3. Hi-Z Generation Pipeline

- [ ] 3.1 Create `src/renderer/post_process/hiz.cpp` -- init, resize, execute, shutdown
- [ ] 3.2 Execute loop: for each mip level (1..N), dispatch `ceil(mip_width/8) x ceil(mip_height/8)`
- [ ] 3.3 Resource barriers between mip dispatches: UAV barrier on Hi-Z texture (write mip N -> read mip N for next)
- [ ] 3.4 After final mip: transition entire Hi-Z texture to SRV state for SSR ray march

## 4. SSR Render Targets

- [ ] 4.1 Create `src/renderer/post_process/ssr.h` -- SSRState struct with settings and DX12 resources
- [ ] 4.2 Allocate SSR result: `RGBA16F`, full-res (reflected color RGB + confidence A)
- [ ] 4.3 Allocate SSR history (2 buffers for ping-pong): `RGBA16F`, full-res
- [ ] 4.4 Handle resize: recreate all SSR targets

## 5. SSR Ray March Compute Shader

- [ ] 5.1 Create `shaders/post/ssr_raymarch.hlsl` -- Hi-Z ray march compute shader (CSMain)
- [ ] 5.2 Early-out for sky pixels (depth >= 1.0) and roughness above cutoff
- [ ] 5.3 Reconstruct world position and compute reflection direction (reflect view dir around normal)
- [ ] 5.4 Project ray start and end points to screen space
- [ ] 5.5 Implement screen-space ray march: step along projected ray, compare against depth buffer
- [ ] 5.6 Hit detection: ray depth > scene depth AND difference < thickness threshold
- [ ] 5.7 Fetch reflected color from HDR buffer at hit UV
- [ ] 5.8 Compute confidence: screen-edge fade * roughness fade * distance fade
- [ ] 5.9 Temporal blending: reproject previous frame SSR via velocity buffer, blend with current result
- [ ] 5.10 Create SSRParams CBV (register b2): view/proj matrices, max_distance, thickness, max_steps, fade params, screen size

## 6. SSR Resolve Pass

- [ ] 6.1 Create `shaders/post/ssr_resolve.hlsl` -- blend SSR + IBL fallback compute shader
- [ ] 6.2 Where SSR confidence > 0: use SSR reflected color (weighted by confidence * strength)
- [ ] 6.3 Where SSR confidence == 0: keep existing IBL specular (no modification)
- [ ] 6.4 Fresnel-weighted blend: metals reflect more strongly
- [ ] 6.5 Compile resolve PSO

## 7. Pipeline Integration

- [ ] 7.1 Create `src/renderer/post_process/ssr.cpp` -- init, resize, execute, shutdown
- [ ] 7.2 Execute Hi-Z generation (pass 1)
- [ ] 7.3 Execute SSR ray march (pass 2): dispatch `ceil(width/8) x ceil(height/8)`
- [ ] 7.4 Resource barrier: `ssr_result` UAV -> SRV
- [ ] 7.5 Execute SSR resolve (pass 3): blend SSR into HDR color buffer
- [ ] 7.6 Ping-pong SSR history: swap read/write history buffer indices each frame
- [ ] 7.7 Insert into post-process stack: after SSAO, before Bloom

## 8. IPC and Settings

- [ ] 8.1 Implement `renderer.setSSR` IPC handler: enabled, max_steps, max_distance, thickness, roughness_cutoff, temporal_blend
- [ ] 8.2 Runtime toggling: enabled=false skips Hi-Z + ray march + resolve (zero GPU cost)
- [ ] 8.3 Add SSR timing query: GPU timestamps, reported in `renderer.stats`

## 9. Debug Visualization

- [ ] 9.1 Implement `renderer.debugView { "mode": "ssr_result" }` -- show SSR reflected color only (no IBL)
- [ ] 9.2 Implement `renderer.debugView { "mode": "ssr_confidence" }` -- confidence as grayscale
- [ ] 9.3 Implement `renderer.debugView { "mode": "hiz" }` -- visualize Hi-Z mip levels as heat map

## 10. Testing

- [ ] 10.1 Visual test: metallic floor plane with objects above -- floor reflects objects correctly
- [ ] 10.2 Visual test: roughness gradient (0.0 -> 0.4) on a surface -- sharp to blurry reflection fade
- [ ] 10.3 Regression: camera looking at a wall parallel to it -- no SSR artifacts (edge case)
- [ ] 10.4 Regression: objects near screen edge -- graceful fade, no hard cutoff
- [ ] 10.5 Temporal: slow camera pan -- SSR reflections remain stable (no flickering)
- [ ] 10.6 Fallback: rough surface (roughness=0.8) shows only IBL, no SSR contribution
- [ ] 10.7 Performance: GPU timer with 64 steps at 1080p, verify within 2ms budget

## 11. Documentation

- [ ] 11.1 Update `docs/specs/renderer.md` post-process table with SSR details
- [ ] 11.2 Verify `docs/specs/screen-space-effects.md` section B is complete
- [ ] 11.3 Document SSR IPC handler parameters in `docs/guides/ipc-mcp.md`
