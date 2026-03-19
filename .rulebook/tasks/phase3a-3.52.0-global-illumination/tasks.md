# Task 3.52.0 — Global Illumination (UzGI)

## Phase A: Screen-Space Global Illumination (SSGI)

### Research
- [ ] A.1 Read UE5 `ScreenSpaceDiffuseIndirect.usf` — extract the SSGI algorithm (ray march in screen space using HiZ, gather radiance from GBuffer).
- [ ] A.2 Read UE5 `IndirectLightRendering.cpp` — trace the CPU setup (dispatch params, temporal history, denoise).
- [ ] A.3 Create data flow document `docs/analysis/dataflow-ssgi.md` mapping UE5 pipeline to UzEngine.
- [ ] A.4 Identify which existing UzEngine resources can be reused (HiZ pyramid, GBuffer normals, depth, velocity buffer for temporal).

### SSGI Trace Pass (compute shader)
- [ ] A.5 Implement `ssgi_trace_cs.hlsl` — for each pixel, trace N short rays in screen space along cosine-weighted hemisphere directions. March against HiZ depth. On hit, read GBuffer albedo * direct lighting at hit point as bounce radiance.
- [ ] A.6 Ray directions: use interleaved gradient noise or Halton sequence (UE5 pattern) for per-pixel jitter. 4-8 rays per pixel at half resolution.
- [ ] A.7 Hit shading: read scene color (previous frame) at hit UV to get the bounced radiance. This captures both direct light and sky light bounced off surfaces.
- [ ] A.8 Output: half-res RGBA16F texture with accumulated indirect radiance.

### SSGI Denoise Pass
- [ ] A.9 Implement `ssgi_denoise_cs.hlsl` — spatial bilateral filter (depth + normal edge stopping) to reduce noise from few-ray-per-pixel trace.
- [ ] A.10 Temporal accumulation: blend current frame SSGI with previous frame using motion vectors (velocity buffer). Variance-guided blend factor.
- [ ] A.11 Upsample from half-res to full-res with bilateral upsampling.

### Integration into Deferred Lighting
- [ ] A.12 In `deferred_lighting_ps.hlsl`: add SSGI contribution to the ambient term. `ambient += ssgiRadiance * albedo * ao`. The SSGI result is pre-multiplied radiance (already includes the bounced surface's albedo), so multiply by receiving surface's albedo for energy conservation.
- [ ] A.13 Add `ssgiSrvIndex` to PerFrameCB (bindless SRV index for the SSGI texture).
- [ ] A.14 In render graph: register SSGI trace + denoise as passes before deferred lighting.

### C++ Infrastructure
- [ ] A.15 Create `runtime/engine/renderer/gi/ssgi_renderer.h/.cpp` — owns the trace + denoise pipelines, temporal history textures, HiZ reference.
- [ ] A.16 Wire into `frame_lighting.cpp` — dispatch SSGI after GBuffer but before deferred lighting.
- [ ] A.17 Add enable/disable toggle via PostProcessVolume or engine setting.

### Validation
- [ ] A.18 Visual verification: place a bright red wall next to a white floor. The white floor near the red wall should have red color bleeding.
- [ ] A.19 A/B comparison: same scene with SSGI on vs off. Shadowed areas should be visibly brighter with SSGI.
- [ ] A.20 Performance: measure frame time delta with SSGI on at 1080p. Target < 2ms on RTX 3060.
- [ ] A.21 Compare with UE5 Basic template SSGI (Lumen disabled, SSGI enabled). Match the shadow fill level.

## Phase B: Irradiance Probe Grid (Future)

### Research
- [ ] B.1 Read UE5 irradiance cache architecture (`IrradianceCacheComposite.usf`, radiance caching papers).
- [ ] B.2 Study DDGI (Dynamic Diffuse Global Illumination) — real-time probe update via ray tracing.
- [ ] B.3 Design probe placement strategy: uniform grid vs adaptive (octree).

### Implementation
- [ ] B.4 Probe data structure: position, SH L2 coefficients (9 per channel = 27 floats), visibility (depth squared for Chebyshev test).
- [ ] B.5 Probe update: each frame, trace rays from subset of probes, update SH. Hardware RT (DXR) or software (SDF).
- [ ] B.6 Probe sampling in deferred lighting: trilinear interpolation of 8 nearest probes, Chebyshev visibility test to avoid light leaking.
- [ ] B.7 Octahedral encoding for probe irradiance (compact 2D atlas instead of per-probe cubemap).

### Integration
- [ ] B.8 Hybrid SSGI + Probes: SSGI for screen-visible bounces, probes for off-screen and distant GI.
- [ ] B.9 Probe debug visualization: render probe positions as spheres with SH-evaluated color.

## Phase C: Hybrid System (Future)

- [ ] C.1 Screen-space trace for near-field (0-5m): highest quality, handles dynamic objects.
- [ ] C.2 Probe grid for mid-field (5-50m): stable, handles off-screen.
- [ ] C.3 SkyLight SH for far-field (50m+): existing system, cheapest.
- [ ] C.4 Smooth blending between tiers based on distance.
