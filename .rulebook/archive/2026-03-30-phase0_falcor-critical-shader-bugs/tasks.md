## Critical Shader Bugs (from Falcor analysis audit)
> Source: `docs/analysis/falcor/unified-shader-improvements.md` — CRITICAL section (14 bugs)
> Estimate: 2 sessions

## Session 1: Entry points + data races + stubs (7 items)

- [x] 1.1 `compute/draw_compact_cs.hlsl` — renamed `CSMain` → `main`. Build verified.
- [x] 1.2 `compute/mipgen_cs.hlsl` — renamed `CSMain` → `main`. Build verified.
- [x] 1.3 `shadows/dfs_upsample_cs.hlsl` — renamed `CSMain` → `main`. Build verified.
- [x] 1.4 `vfx/particle_mesh_instance_cs.hlsl` — added `GroupMemoryBarrierWithGroupSync()` after thread 0 init block, before `InterlockedAdd`.
- [x] 1.5 `include/texture_binding.hlsli` — filled `#else` SM5 block with 11 register-bound macros (UZ_SAMPLE_TEX2D, LOD, BIAS, GRAD, CMP, LOAD, TEXCUBE, TEX3D).
- [x] 1.6 `include/shadow_debug.hlsli` — implemented all 4 functions: 3x3 grid (Albedo/Normal/Atlas/Shadow/LIT/Depth/Cascade/AO/Roughness), tile labels, borders, per-channel visualization.
- [x] 1.7 `include/aerial_perspective.hlsli` — implemented `ComputeAerialPerspective()` with 8-step inline ray march, Rayleigh+Mie single-scatter, transmittance LUT sampling, artistic scale multipliers.

## Session 2: Data flow bugs + GBuffer + VFX (7 items)

- [x] 2.1 `ssgi/prev_color_reduce_cs.hlsl` — implemented single-pass mip chain (mips 0-3) via groupshared LDS cooperative downsample. 8x8 group → 4x4 mip 1 → 2x2 mip 2 → 1x1 mip 3. Karis weighting at all levels. SM5 UAV fallbacks added (u1-u3).
- [x] 2.2 `dfs_cone_trace_cs.hlsl:142` — fixed `LineBoxIntersect` call: changed `volumeRayEnd` (position) to `volumeRayEnd - volumeRayStart` (unnormalized direction). Returns t in [0,1] compatible with downstream `* volumeRayLength`.
- [x] 2.3 `gi/global_sdf_composite_cs.hlsl` — fixed AABB cull: transform worldPos to local space via invTransform before comparing against local-space mesh bounds. Cull distance uses max mesh extent.
- [x] 2.4 `dfs_cone_trace_cs.hlsl` — fixed `worldToVolumeScale`: computed as `volumeRayLength / worldRayLength` ratio, applied to cone radius (`tanLightAngle * worldToVolumeScale * sampleRayTime`). Affects both AABB expansion and per-step cone.
- [x] 2.5 `geometry/gbuffer_ps.hlsl` — removed 8-line inline OctEncode, replaced with `#include "octahedral.hlsli"` + `OctEncode(N)` call.
- [x] 2.6 `vfx/particle_billboard_vs.hlsl` — reads `sortedIndices[particleID]` via bindless SRV, uses resolved `sortedIdx` for all attribute buffer reads. Falls back to unsorted `particleID` when sort buffer unavailable.
- [x] 2.7 `ibl/equirect_to_cubemap_cs.hlsl` — audited: already correct. `CubeFaceDirection()` uses Y-up DX12 convention, `DirectionToEquirectUV()` maps `dir.y` to latitude. `WorldToCubemap()` handles Z-up→Y-up conversion at sampling call sites. No fix needed.
