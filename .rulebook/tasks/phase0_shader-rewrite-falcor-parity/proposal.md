# Proposal: Shader System Rewrite — Falcor/UE5 Parity (Phase 0)

## Why

The entire shader system (135 .hlsl + 26 .hlsli) was deleted to break from the ad-hoc legacy implementation. The old shaders had no consistent BRDF reference, no paper citations, root signature mismatches that crashed the GPU (DEVICE_REMOVED 0x887A0006), no SM5 fallback for `ResourceDescriptorHeap[]`, HLSL keyword collisions (`linear` as param name), wrong sentinel conventions (mixed `0` vs `0xFFFFFFFF`), and vector ternary without `select()`. The engine renders nothing until these are reimplemented correctly. Every feature is blocked: GBuffer, lighting, shadows, atmosphere, post-processing, VFX.

**Ground truth**: Falcor (NVIDIA, BSD) for BRDF/material, UE5 published papers for pipeline, Bruneton 2017 for atmosphere, Jimenez 2016 for GTAO, Karis 2013/2014 for PBR/IBL/TAA.

## What Changes

### New Systems
1. **Include headers** (26 files) — math, encoding, sampling, PBR, shadow, atmosphere, VFX utilities
2. **Core rendering** (7 shaders) — mesh VS, GBuffer PS, forward PS, fullscreen VS, shadow depth VS, deferred lighting PS, tonemap PS
3. **Atmosphere** (19 shaders) — Bruneton LUTs, sky, fog, aerial perspective, volumetric clouds
4. **IBL** (4 shaders) — BRDF LUT, irradiance, prefilter, equirect-to-cubemap
5. **Post-processing** (34 shaders) — bloom, exposure, GTAO, TAA, TSR, SSR, motion blur, DOF, AA, CAS, LUT, lens flare, FSR
6. **Compute** (5 shaders) — GPU cull, HiZ, draw compact, mipgen, decal apply
7. **Debug** (3 shaders) — GBuffer vis, pixel debug, target vis
8. **GI** (3 shaders) — global SDF, mesh SDF bake, radiance cache
9. **Shadows** (3 shaders) — DFS cone trace, cull, upsample
10. **SSGI** (5 shaders) — trace, temporal, denoise, upsample, prev color
11. **VFX** (20 files) — particles, trails, decals, distortion

### Refactoring
- `scene_render_frame.cpp` — re-enable `Register*Passes()` one by one as shaders validate
- `scene_renderer_init.cpp` — re-enable `WarmupAll()` after all core shaders pass
- `frame_lighting.cpp` — re-enable sub-passes (GTAO, DFS, SSGI, SSR, atmosphere) one by one

## Impact
- Affected specs: `docs/specs/shader-library.md`, `docs/specs/shader-variants.md`, `docs/specs/renderer.md`
- Affected code: `runtime/shaders/runtime/**`, `runtime/engine/renderer/scene/scene_render_frame.cpp`, `scene_renderer_init.cpp`, `frame/frame_lighting.cpp`
- Breaking change: YES — engine renders nothing until Phase 1 complete
- User benefit: Correct PBR with paper citations, SM5/Vulkan compat, no GPU crashes
- Enables: Falcor PSNR comparison, visual regression, multi-backend
