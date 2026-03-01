## 1. Velocity G-Buffer Channel

- [ ] 1.1 Add GBuffer3 render target: `RG16F` at full resolution (screen-space velocity XY)
- [ ] 1.2 Update `src/renderer/render_targets.h` to allocate GBuffer3 alongside GBuffer0-2
- [ ] 1.3 Add RTV descriptor for GBuffer3 in the `rtv` descriptor heap
- [ ] 1.4 Add SRV descriptor for GBuffer3 in the `cbv_srv_uav` descriptor heap
- [ ] 1.5 Update G-Buffer pass PSO to output 4 render targets (RT0-RT3) instead of 3
- [ ] 1.6 Handle resize: recreate GBuffer3 on `renderer.setResolution`

## 2. Per-Object Previous-Frame Transforms

- [ ] 2.1 Add `prev_model` (float4x4) to the per-object CBV (register b0)
- [ ] 2.2 Update `PerObjectData` CPU struct to include `prev_model` (64 bytes additional)
- [ ] 2.3 In scene node system: store previous-frame model matrix for each visible node
- [ ] 2.4 After rendering each frame, copy current `model` -> `prev_model` for all rendered nodes
- [ ] 2.5 For newly spawned nodes: set `prev_model = model` (no velocity on first frame)
- [ ] 2.6 Update `static_assert` for PerObjectData alignment (must be 256-byte aligned for CBV)

## 3. Per-Frame Previous-Frame Data

- [ ] 3.1 Add `prev_view_proj` (float4x4) to the per-frame CBV (register b1)
- [ ] 3.2 Add `jitter_offset` (float2) to the per-frame CBV
- [ ] 3.3 Update `PerFrameData` CPU struct with these new fields
- [ ] 3.4 Each frame: copy current `view_proj` -> `prev_view_proj` before computing new matrices
- [ ] 3.5 Update `static_assert` for PerFrameData alignment

## 4. G-Buffer Vertex/Pixel Shader Update

- [ ] 4.1 Update `shaders/builtin/gbuffer.hlsl` VSOut: add `clip_pos` and `prev_clip_pos` interpolants
- [ ] 4.2 Vertex shader: compute `clip_pos = mul(view_proj, world_pos)` and `prev_clip_pos = mul(prev_view_proj, mul(prev_model, float4(pos, 1.0)))`
- [ ] 4.3 Update PSOut to include `float2 gbuffer3 : SV_TARGET3` (velocity)
- [ ] 4.4 Pixel shader: compute velocity as `(curr_ndc - prev_ndc) * float2(0.5, -0.5)` in UV space
- [ ] 4.5 Ensure transparent pass also writes velocity (or clear GBuffer3 for transparent pixels)

## 5. Halton Jitter

- [ ] 5.1 Create `src/renderer/post_process/taa.h` -- TAAState struct with frame_index, jitter, history buffers
- [ ] 5.2 Implement Halton(2,3) sequence generator (CPU-side)
- [ ] 5.3 Pre-compute 8-sample jitter table: `HALTON_8[8]` centered at [-0.5, 0.5] in pixel units
- [ ] 5.4 Implement `apply_jitter()`: modify projection matrix `proj[2][0]` and `proj[2][1]` by jitter/screen_size
- [ ] 5.5 Apply jittered projection to G-Buffer pass and forward transparent pass only
- [ ] 5.6 Do NOT apply jitter to shadow passes, Hi-Z generation, or post-process passes
- [ ] 5.7 Increment frame_index mod jitter_samples each frame

## 6. TAA History Buffers

- [ ] 6.1 Allocate 2 history buffers (ping-pong): `RGBA16F` or `R11G11B10F`, full-res
- [ ] 6.2 Track ping-pong index (0 or 1): current frame writes to `history[idx]`, reads from `history[1-idx]`
- [ ] 6.3 Clear both history buffers on first frame or on resolution change
- [ ] 6.4 Handle resize: recreate both history buffers at new dimensions

## 7. TAA Resolve Compute Shader

- [ ] 7.1 Create `shaders/post/taa.hlsl` -- TAA resolve compute shader (CSMain)
- [ ] 7.2 Sample current frame color (post-tonemapping) at pixel center
- [ ] 7.3 Gather 3x3 neighborhood of current frame in YCoCg color space
- [ ] 7.4 Compute neighborhood mean and standard deviation for variance clamping
- [ ] 7.5 Read velocity from GBuffer3, compute previous-frame UV: `prev_uv = uv - velocity`
- [ ] 7.6 Sample history buffer at `prev_uv` using Catmull-Rom bicubic filter (5-tap approximation)
- [ ] 7.7 Clip history color to neighborhood AABB (variance clamp) in YCoCg space
- [ ] 7.8 Blend: `result = lerp(history, current, blend_factor)`
- [ ] 7.9 Velocity-adaptive blend: increase blend_factor for fast-moving pixels (reduce ghosting)
- [ ] 7.10 Off-screen rejection: if `prev_uv` is outside [0,1], use current frame only
- [ ] 7.11 Post-TAA sharpening: 4-tap Laplacian unsharp mask, controlled by `sharpen_strength`
- [ ] 7.12 Clamp final result to [0, +inf) (prevent negative from sharpening overshoot)
- [ ] 7.13 Create TAAParams CBV (register b2): blend_factor, variance_clip_gamma, sharpen_strength, screen_size, texel_size, jitter

## 8. Pipeline Integration

- [ ] 8.1 Create `src/renderer/post_process/taa.cpp` -- init, resize, execute, shutdown
- [ ] 8.2 Compile TAA resolve PSO: `dxc -T cs_6_0 -E CSMain`
- [ ] 8.3 Execute TAA resolve: dispatch `ceil(width/8) x ceil(height/8)` thread groups
- [ ] 8.4 Resource barriers: current frame SRV, velocity SRV, history[1-idx] SRV -> history[idx] UAV
- [ ] 8.5 After resolve: transition history[idx] UAV -> SRV (becomes the final output / input to readback)
- [ ] 8.6 Swap ping-pong index: `idx = 1 - idx`
- [ ] 8.7 Insert into post-process stack: after Tonemapping, before final output/readback

## 9. IPC and Settings

- [ ] 9.1 Implement `renderer.setTAA` IPC handler: enabled, blend, sharpen, variance_clip
- [ ] 9.2 Runtime toggling: enabled=false skips jitter application and TAA resolve (zero cost)
- [ ] 9.3 When TAA is disabled mid-frame: clear history buffers to prevent stale data on re-enable
- [ ] 9.4 Add TAA timing query: GPU timestamp, reported in `renderer.stats`

## 10. Debug Visualization

- [ ] 10.1 Implement `renderer.debugView { "mode": "velocity" }` -- velocity vectors as color-coded image
- [ ] 10.2 Velocity debug: RG channels as red/green intensity, magnitude as brightness
- [ ] 10.3 Optional: draw velocity arrows overlay in debug view

## 11. Testing

- [ ] 11.1 Visual test: static scene with TAA enabled -- geometric edges (cube silhouette) appear smooth
- [ ] 11.2 Visual test: rotate camera slowly -- no ghosting or trailing artifacts
- [ ] 11.3 Visual test: moving object (node translation) -- velocity buffer produces correct motion vectors
- [ ] 11.4 Regression: fast camera whip -- no persistent ghosting (variance clamp + velocity-adaptive blend)
- [ ] 11.5 Regression: object appearing from off-screen -- resolves within 2-3 frames (no long ghost)
- [ ] 11.6 Sharpening: fine text/detail rendered in scene -- legible with sharpen=0.3 (not blurred away)
- [ ] 11.7 Performance: GPU timer at 1080p, verify TAA resolve < 0.5ms
- [ ] 11.8 Toggle: enable/disable via IPC, verify jitter stops and image returns to non-jittered state

## 12. Documentation

- [ ] 12.1 Update `docs/specs/renderer.md` G-Buffer layout table with GBuffer3 velocity
- [ ] 12.2 Update `docs/specs/renderer.md` per-frame CBV documentation with prev_view_proj and jitter
- [ ] 12.3 Verify `docs/specs/screen-space-effects.md` section C is complete
- [ ] 12.4 Document TAA IPC handler parameters in `docs/guides/ipc-mcp.md`
- [ ] 12.5 Add note to `docs/specs/shader-system.md` about jitter projection convention
