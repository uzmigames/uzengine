# Proposal: phase0_sponza-parity-03-sdf-gi-radiance-cache

## Why

Our SDF-based GI has never contained geometry. `runtime/shaders/runtime/gi/mesh_sdf_bake_cs.hlsl`
and `global_sdf_composite_cs.hlsl` are one-line stubs with empty bodies
(`[numthreads(8,8,1)] void main(...){}`), while the C++ callers dispatch, report successful bakes and
expose the outputs. `docs/analysis/known-issues-and-partial-implementations.md` calling the GI
"functional" was therefore never true. SponzaTest (`docs/analysis/rendering/sponza-gi-correction-2026-09-06.md`,
commits `33b0f934` and `97acd374`) implemented both shaders (151 and 111 lines) and fixed the chain
of defects that surfaced once geometry existed:

- Sponza's triangle winding is opposite its vertex normals for 3,739,444 of 3,747,286 triangles and
  the mesh is split into open partitions (42 boundary edges on the exterior stone at Camera A); a
  nearest-face sign cannot define an interior. Two-sided fields (`r.GI.MeshSdf.TwoSided`) are required.
- Radiance cache wrote raw SH into a buffer consumed as a cosine-convolved polynomial (uniform
  radiance test returned about 3.55 instead of 1, inverted response on two axes).
- Only the first 1024 of 4096 probes received first-write initialisation; grid moves did not reset.
- Producer spacing (`extent*2 / N`) disagreed with consumer interpolation (`N-1`); grid coordinates
  uploaded before recentring; world-to-local packing dropped translation.
- GDF refresh tracked camera movement but not mesh load/remove/transform.
- Probe hit lighting used the negated ray as normal, had no sun visibility test, no Lambert 1/pi, and
  misses used the skylight tint as sky radiance; exhausted rays were treated as escaped.
- A valid black GI value fell back to sky light, then an unconditional 0.25 floor masked it.
- The trace shader sampled a dynamic sampler that was never created (now static clamp at s2).
- No feedback from previous probe irradiance, so no multi-bounce propagation.
- Buried probes propagated invalid light (now validity in SH record 6.w; consumers renormalise).

## What Changes

- Real `mesh_sdf_bake_cs.hlsl`, new `mesh_sdf_bvh.h`, rewritten `mesh_sdf_baker.{h,cpp}` (BVH
  build/upload, `r.GI.MeshSdf.Resolution`, `r.GI.MeshSdf.TwoSided`, translation-preserving transforms).
- Real `global_sdf_composite_cs.hlsl`, rewritten `global_distance_field.{h,cpp}`: 4 clipmaps, raw R16F
  fields kept beside the expanded RGBA16F, origin preserved, revision counter bumped on mesh changes,
  `r.GI.GDF.Resolution`. Frozen fixture `scripts/rendering/fixtures/gdf_expanded_before_raw.h` (never regenerate).
- `radiance_cache.{h,cpp}`: spacing `/(N-1)`, full first-write init, history SH buffer, validity,
  `r.GI.RadianceCache.ProbesPerFrame` and `RaysPerProbe`, reset on grid move and GDF revision,
  `Update(camPos)`, `HasProducedResult()`, `GetGridExtent()`, diagnostic readback and log.
- `radiance_cache_trace_cs.hlsl` (+210): surface-normal hit lighting, `TraceGDF` sun shadow ray from
  `hit + N*2*epsilon`, Lambert 1/pi, sky only on confirmed escape, static sampler s2, previous-irradiance
  feedback. **Keep our VFX-to-DDGI emissive injection block (phase16)** behind its existing gate.
- `deferred_lighting_ps.hlsl` RC consumer: normal-weighted trilinear, skip invalid probes,
  renormalise, zero is valid (no sky fallback), `skyLightMinAmbient` only when `!irradianceFromGI`.
- Wiring: `frame_constants.cpp`, `frame_gi_passes.cpp`, `scene_sync.cpp`, `scene_mesh_assets.cpp`,
  `scene_renderer_init.cpp`, `scene_render_frame.cpp` hunks from SponzaTest.
- Sponza project already sets `r.RadianceCache=1`, `GDF 256`, `MeshSdf 128`, `TwoSided 1`,
  `GridExtent 2000` (was 15000 in the removed manual calibration).

## Impact
- Affected specs: `phase0_brixelizer-gi-integration` and `phase0_indoor-lighting-system` describe
  the SDF GI as existing; reconcile their status after this task
- Affected code: GI shaders and C++ (about 1.5k lines), deferred consumer, frame wiring
- Breaking change: NO (paths were inert; CVars default off except where the project enables them)
- User benefit: the radiance cache finally sees the scene; interior bounce light exists
