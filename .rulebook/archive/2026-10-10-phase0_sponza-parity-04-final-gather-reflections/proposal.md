# Proposal: phase0_sponza-parity-04-final-gather-reflections

## Why

Even with a working radiance cache (task 03), direct interpolation between probes ≈267 cm apart does
not resolve lighting on Sponza's columns and arches (`sponza-surface-lighting-2026-09-07.md`).
SponzaTest added two Lumen-style consumers on top of the SDF/radiance cache:

- **`r.GI.FinalGather`** — cosine-weighted hemisphere rays per surface pixel, traced through the
  global distance field, lit by shadowed sun + probe irradiance. Half resolution, plane/normal-aware
  spatial filter, history reprojected with the previous view-projection, invalidated on geometry
  change. It **replaces** the probe-interpolated diffuse irradiance (no second ambient term).
  The SSGI shaders were rewritten to host it (world mode) while keeping the legacy screen-space
  trace as a separate mode. Two activation blockers were fixed (prepare before `ctx.pipelines`
  was assigned; pass inspector gated only on `r.SSGI`).
- **`r.GI.Reflections`** — GGX rays through the SDF (`sdf_reflections.hlsli`, 227 lines): a hit
  receives shadowed sun + that surface's diffuse; only escaping rays see sky; exhausted steps ≠ sky.
  SSR is lerped in **before** the BRDF (`radiance = lerp(sdf, ssr.rgb, ssr.a)`), replacing the old
  post-TAA additive composite. Geometric normal from `ddx_fine/ddy_fine` prevents rays through the
  pixel's own face; the first intersection is refined before shading.

This is the largest visual change of the port and the most expensive: the maximum preset
(FG32, 16 reflection samples / 256 steps, GDF256, mesh128) ran at ≈1 FPS on a 6 GB GTX 1660 SUPER.
Project default is FG8 / 4 samples / 96 steps.

## What Changes

- New: `runtime/shaders/runtime/ssgi/ssgi_surface.hlsli`, `ssgi_world_gather.hlsli`,
  `runtime/shaders/runtime/gi/sdf_reflections.hlsli`, `sdf_diffuse_gather.hlsli`.
- Rewritten: `ssgi_trace_cs.hlsl`, `ssgi_temporal_accum_cs.hlsl`, `ssgi_spatial_denoise_cs.hlsl`,
  `ssgi_upsample_cs.hlsl` (13-DWORD root constants, PerFrameCB shared).
- `ssgi_renderer.{h,cpp}` (+125/+61), `frame_gi_passes.cpp` (+92), `frame_lighting.cpp`,
  `frame_lighting_pass.cpp`, `frame_setup.cpp`, `frame_postprocess.cpp`.
- `deferred_lighting_ps.hlsl`: PerFrameCB fields `_sdfReflectionsEnabled … _sdfRawClipmapSrv`
  (offsets 852–976), geometric normal, `SdfReflectionScene` setup, `GatherSdfReflections`, SSR lerp,
  `skyTint*` neutralised when the SDF path is active, legacy SSGI add only when `_sdfDiffuseSamples == 0`.
- `constant_buffers.h` / `per_frame_params.h` (+16 each) — keep 16-byte alignment; extend the ABI
  test (36 bytes / 9 DWORDs incl. all eight previous offsets).
- CVars in `frame_constants.cpp`: `r.GI.Reflections`, `r.GI.FinalGather`, `r.GI.FinalGather.Samples` (8),
  `r.GI.Reflections.Samples` (4), `.Steps` (96), `.Distance` (5000); `m_ssrHasPrevColor` gate.
- Sponza project already sets `r.GI.Reflections=1`, `r.GI.FinalGather=1`.

## Impact
- Affected specs: SSGI pass contract (root constants, modes); PerFrameCB layout
- Affected code: SSGI + GI shaders, deferred lighting, renderer frame wiring
- Breaking change: NO for scenes that leave both CVars off (legacy SSGI mode preserved)
- User benefit: readable shading on columns/arches, off-screen reflections, no broad bright
  contact lines at wall/floor; cost is significant on low-end GPUs
