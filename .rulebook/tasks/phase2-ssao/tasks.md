## 1. SSAO Render Targets

- [ ] 1.1 Create `src/renderer/post_process/ssao.h` -- SSAOState struct with settings and DX12 resource handles
- [ ] 1.2 Allocate half-resolution AO render target: `ao_raw` (R8_UNORM, half-res)
- [ ] 1.3 Allocate half-resolution blur intermediate: `ao_blur_temp` (R8_UNORM, half-res)
- [ ] 1.4 Allocate half-resolution final AO: `ao_final` (R8_UNORM, half-res)
- [ ] 1.5 Handle resize on `renderer.setResolution` IPC (recreate all 3 targets at new half-res dimensions)
- [ ] 1.6 Add descriptor heap slots for SSAO SRVs and UAVs in `cbv_srv_uav` heap

## 2. HBAO Compute Shader

- [ ] 2.1 Create `shaders/include/screen_space.hlsli` -- shared utilities (linearize_depth, uv_to_view_pos, screen_edge_fade)
- [ ] 2.2 Create `shaders/post/hbao.hlsl` -- HBAO+ compute shader (CSMain entry point)
- [ ] 2.3 Implement per-pixel view-space position reconstruction from depth buffer + projection matrix
- [ ] 2.4 Implement horizon angle computation: march along N directions, track max elevation angle at each step
- [ ] 2.5 Per-pixel random rotation via hash function (hash21 from noise.hlsli) seeded with pixel coord + time
- [ ] 2.6 Convert world-space radius to screen-space radius based on view-space depth and projection
- [ ] 2.7 Implement distance-based AO fade (no AO beyond `ssao_max_distance`)
- [ ] 2.8 Early-out for sky pixels (depth >= 1.0) and distant pixels
- [ ] 2.9 Support quality levels via `ssao_num_directions` define/constant: 4 (Low), 8 (Medium), 16 (High)
- [ ] 2.10 Create SSAOParams CBV (register b2): radius, bias, intensity, max_distance, texel sizes, num_directions, projection matrix

## 3. Bilateral Blur Shader

- [ ] 3.1 Create `shaders/post/ssao_blur.hlsl` -- cross-bilateral blur compute shader
- [ ] 3.2 Horizontal blur variant: `#define BLUR_HORIZONTAL` compile-time switch
- [ ] 3.3 Vertical blur variant: default (no define)
- [ ] 3.4 4-tap Gaussian kernel with bilateral depth weighting (preserve depth edges)
- [ ] 3.5 Depth edge sharpness parameter (higher = more edge preservation, default 8.0)
- [ ] 3.6 Create two shader variants (horizontal + vertical) compiled as separate PSOs

## 4. Pipeline Integration

- [ ] 4.1 Create `src/renderer/post_process/ssao.cpp` -- init, resize, execute, shutdown functions
- [ ] 4.2 Compile HBAO compute PSO (DXC: `-T cs_6_0 -E CSMain`)
- [ ] 4.3 Compile blur horizontal PSO (DXC: `-T cs_6_0 -E CSMain -D BLUR_HORIZONTAL`)
- [ ] 4.4 Compile blur vertical PSO (DXC: `-T cs_6_0 -E CSMain`)
- [ ] 4.5 Execute HBAO pass: dispatch `ceil(half_width/8) x ceil(half_height/8)` thread groups
- [ ] 4.6 Resource barrier: `ao_raw` UAV -> SRV transition after HBAO pass
- [ ] 4.7 Execute blur horizontal pass: `ao_raw` (SRV) -> `ao_blur_temp` (UAV)
- [ ] 4.8 Resource barrier: `ao_blur_temp` UAV -> SRV transition
- [ ] 4.9 Execute blur vertical pass: `ao_blur_temp` (SRV) -> `ao_final` (UAV)
- [ ] 4.10 Resource barrier: `ao_final` UAV -> SRV transition (ready for lighting pass)

## 5. Deferred Lighting Integration

- [ ] 5.1 Add `ao_final` as SRV input to deferred lighting shader (register t-slot, e.g. t5)
- [ ] 5.2 Sample AO buffer in lighting shader using bilinear filtering (half-res -> full-res upsample)
- [ ] 5.3 Multiply AO with ambient/IBL term only (NOT direct lighting)
- [ ] 5.4 Blend with baked AO from GBuffer0.a: `final_ao = min(ssao, baked_ao)` for combined result

## 6. Quality Settings and IPC

- [ ] 6.1 Define PostProcessSettings.ssao struct: enabled, quality, radius, bias, intensity, max_distance
- [ ] 6.2 Implement `renderer.setSSAO` IPC handler: parse JSON, update settings, recompile PSO if quality changes
- [ ] 6.3 Support runtime toggling (enabled=false skips all SSAO dispatches, zero GPU cost)
- [ ] 6.4 Add SSAO timing query: GPU timestamp before/after SSAO passes, reported in `renderer.stats`

## 7. Debug Visualization

- [ ] 7.1 Implement `renderer.debugView { "mode": "ssao" }` -- fullscreen display of raw AO buffer
- [ ] 7.2 AO debug view: R8 value mapped to grayscale (white = no occlusion, black = fully occluded)
- [ ] 7.3 Optional: show per-direction horizon angles as color-coded overlay

## 8. Testing

- [ ] 8.1 Visual test: render a Cornell box scene -- corners show ambient darkening
- [ ] 8.2 Visual test: sphere on plane -- contact shadow visible at sphere-floor junction
- [ ] 8.3 Regression: flat wall at 90deg angle to camera -- no self-occlusion artifacts (bias validation)
- [ ] 8.4 Regression: objects at `ssao_max_distance + 10m` -- no visible AO (distance fade works)
- [ ] 8.5 Performance: GPU timer at Low/Medium/High quality at 1080p, verify within budget
- [ ] 8.6 Toggle: enable/disable via IPC, verify zero GPU cost when disabled

## 9. Documentation

- [ ] 9.1 Update `docs/specs/renderer.md` post-process table with HBAO+ details
- [ ] 9.2 Add SSAO section to `docs/specs/screen-space-effects.md` if not already present
- [ ] 9.3 Document IPC handler parameters and valid ranges in `docs/guides/ipc-mcp.md`
