## 1. Implementation

- [x] 1.1 Create depth_prepass.h/cpp — render graph pass at `runtime/engine/renderer/passes/geometry/depth_prepass.h/cpp`. Follows GBufferPass pattern: DepthPrepassDesc + AddDepthPrepass(). Depth-only (0 color RT), reversed-Z clear, per-object CB, instancing support. @source UE5 DepthRendering.cpp — FDepthPassMeshProcessor.
- [x] 1.2 Create depth-only PSO variant — `PipelineLibrary::GetDepthPrepass()` reuses shadow_depth_vs, 0 render targets, D32Float, LessEqual, NO depth bias (unlike shadow pass). Both bindless and SM5 variants.
- [ ] 1.3 Modify GBuffer pass depth test — change from LESS to EQUAL when depth prepass is enabled. Depth writes disabled in GBuffer pass (already written).
- [x] 1.4 Create hiz_pyramid_cs.hlsl — compute shader at `runtime/shaders/runtime/compute/hiz_pyramid_cs.hlsl`. Based on UE5 HZB.usf:183-293 HZBBuildCS. Uses Load() with 4 explicit taps, min() reduction for reversed-Z, odd-dimension clamping. [8,8,1] thread groups.
- [x] 1.5 Create HiZPyramidPass — render graph compute pass at `runtime/engine/renderer/passes/postprocess/hiz_pyramid_pass.h/cpp`. Dispatches per-mip with UAV barriers, binds SRV/UAV per level, root constants for dimensions. @source UE5 HZBOcclusion.cpp — BuildHZB.
- [x] 1.6 Register Hi-Z pyramid as persistent render graph resource — Per-mip R32_FLOAT textures stored in `m_hizMipLevels[]` on SceneRenderer. Created in Init(), recreated in Resize(), destroyed in Shutdown(). SRV+UAV descriptors per mip.
- [x] 1.7 Wire into frame pipeline — DepthPrepass added before GBuffer in RenderFrame() (gated on `m_depthPrepassEnabled`), HiZPyramid dispatched after prepass. Instancing pass-through supported.
- [x] 1.8 Add depth prepass toggle — `SceneRenderer::SetDepthPrepassEnabled(bool)` / `IsDepthPrepassEnabled()`. Default: enabled.

## 2. Visual Verification

- [ ] 2.1 Screenshot: GBuffer debug depth channel — must be identical with and without depth prepass (same Z values)
- [ ] 2.2 Screenshot: Hi-Z pyramid mip visualization — debug view showing each mip level, coarser mips must show correct min-depth regions
- [ ] 2.3 Performance: measure GPU time on pbr_test with 100+ objects — prepass must show fillrate reduction (compare GPU timestamp queries)
- [ ] 2.4 Regression: all existing demos produce identical visual output with prepass enabled

## 3. Documentation

- [x] 3.1 Update `docs/engine/12-rendering/03-frame-pipeline.md` with depth prepass stage — added DepthPrepass and HiZPyramid stages between Shadow and GBuffer, updated pass numbering and Implementation Phases table.
- [x] 3.2 Update `docs/engine/12-rendering/11-deferred-pipeline.md` with EQUAL depth test rationale — added Depth Prepass & Hi-Z Pyramid section with API, PSO properties, early-Z interaction, and Hi-Z mip allocation; updated pipeline diagram and Limitations table.
- [x] 3.3 Document Hi-Z pyramid format and mip chain access pattern for downstream passes — created `docs/engine/12-rendering/15-depth-prepass-hiz.md` (280 lines) covering pass ordering, resource flow, compute shader details, mip allocation, downstream consumer table (GTAO/SSR/contact shadows/occlusion culling), performance table, UE5 reference, key files, and rules.
- [x] 3.4 Update `docs/specs/1.3-renderer.md` — added depth_prepass to geometry/ passes, hiz_pyramid_cs.hlsl to compute shader inventory, updated sub-milestone status and Not Yet Implemented list.
- [x] 3.5 Update `docs/specs/rendering/post-process.md` — added Depth Prepass and Hi-Z Pyramid to pipeline execution order diagram, key files table, and status table.
- [x] 3.6 Update `docs/specs/rendering/shadows.md` — added Frame Pipeline Position section showing shadow→prepass→hiz→gbuffer→lighting ordering with cross-reference.
- [x] 3.7 Update `docs/engine/12-rendering/README.md` — added table row for 15-depth-prepass-hiz.md, file manifest entry, and quick navigation section.
