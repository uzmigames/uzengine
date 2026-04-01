## Critical Shader Bugs (from Falcor analysis audit)
> Source: `docs/analysis/falcor/unified-shader-improvements.md` — CRITICAL section (14 bugs)
> Estimate: 2 sessions

## Session 1: Entry points + data races + stubs (7 items)

- [ ] 1.1 `compute/draw_compact_cs.hlsl` — rename `CSMain` → `main`. Build + verify draw compaction works.
- [ ] 1.2 `compute/mipgen_cs.hlsl` — rename `CSMain` → `main`. Build + verify mip generation works.
- [ ] 1.3 `shadows/dfs_upsample_cs.hlsl` — verify entry point is `main` (rename if CSMain). Build + verify DFS upsample.
- [ ] 1.4 `vfx/particle_mesh_instance_cs.hlsl` — add `GroupMemoryBarrierWithGroupSync()` before `InterlockedAdd`. Verify correct instance count.
- [ ] 1.5 `include/texture_binding.hlsli` — fill `#else` (SM5/non-bindless) block with working macros for `UZ_SAMPLE_TEX2D` etc.
- [ ] 1.6 `include/shadow_debug.hlsli` — implement all 4 diagnostic functions (currently hardcoded). Wire to GBuffer/shadow data. Verify `--shadow-debug 1`.
- [ ] 1.7 `include/aerial_perspective.hlsli` — implement `ComputeAerialPerspective()` (currently returns `float4(0,0,0,1)`). Sample aerial perspective 3D volume. Verify distant haze.

## Session 2: Data flow bugs + GBuffer + VFX (7 items)

- [ ] 2.1 `ssgi/prev_color_reduce_cs.hlsl` — fix mip chain: dispatch mips 1-4 (currently only mip 0). Verify SSGI reads valid data.
- [ ] 2.2 `include/sdf_common.hlsli` + `dfs_cone_trace_cs.hlsl:144` — fix caller passing world position instead of ray direction to `LineBoxIntersect`.
- [ ] 2.3 `gi/global_sdf_composite_cs.hlsl` — fix AABB cull: transform to world space before comparing. Verify SDF composite correct.
- [ ] 2.4 `dfs_cone_trace_cs.hlsl` — fix `worldToVolumeScale` for non-unit-scale objects. Verify cone radius varies with scale.
- [ ] 2.5 `geometry/gbuffer_ps.hlsl` — remove inline `OctEncode` duplicate, use `#include "octahedral.hlsli"`. Build + verify.
- [ ] 2.6 `vfx/particle_billboard_vs.hlsl` — read `sortedIndices[particleID]` instead of raw `vertexID / 6`. Verify back-to-front ordering.
- [ ] 2.7 `ibl/equirect_to_cubemap_cs.hlsl` — audit Z-up vs Y-up cubemap faces. Fix if Y-up. Verify IBL reflections correct.
