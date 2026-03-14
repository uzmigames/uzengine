## 1. Implementation

- [ ] 1.1 Create depth_prepass.h/cpp — render graph pass that renders all opaque meshes with depth-only PSO (no pixel shader, no color RT). Output: D32F depth buffer shared with GBuffer pass.
- [ ] 1.2 Create depth-only PSO variant — vertex shader only (mesh_vs.hlsl), null pixel shader, depth write enabled, depth test LESS.
- [ ] 1.3 Modify GBuffer pass depth test — change from LESS to EQUAL when depth prepass is enabled. Depth writes disabled in GBuffer pass (already written).
- [ ] 1.4 Create hiz_pyramid_cs.hlsl — compute shader that downsamples depth buffer by 2x per mip level. Each output texel = min(2x2 input texels). Dispatch per-mip with SRV→UAV binding.
- [ ] 1.5 Create HiZPyramidPass — render graph compute pass that generates full mip chain from depth prepass output. Allocate R32F texture with log2(max(width,height)) mip levels.
- [ ] 1.6 Register Hi-Z pyramid as persistent render graph resource — available to SSAO, SSR, contact shadows, and occlusion culling passes.
- [ ] 1.7 Wire into frame pipeline — insert DepthPrepass before GBuffer, HiZPyramid after DepthPrepass. Ensure barriers (depth write → SRV read → UAV write per mip).
- [ ] 1.8 Add depth prepass toggle — PostProcessStack or renderer config flag to enable/disable prepass (useful for simple scenes where overdraw is minimal).

## 2. Visual Verification

- [ ] 2.1 Screenshot: GBuffer debug depth channel — must be identical with and without depth prepass (same Z values)
- [ ] 2.2 Screenshot: Hi-Z pyramid mip visualization — debug view showing each mip level, coarser mips must show correct min-depth regions
- [ ] 2.3 Performance: measure GPU time on pbr_test with 100+ objects — prepass must show fillrate reduction (compare GPU timestamp queries)
- [ ] 2.4 Regression: all existing demos produce identical visual output with prepass enabled

## 3. Documentation

- [ ] 3.1 Update `docs/engine/12-rendering/03-frame-pipeline.md` with depth prepass stage
- [ ] 3.2 Update `docs/engine/12-rendering/10-deferred-pipeline.md` with EQUAL depth test rationale
- [ ] 3.3 Document Hi-Z pyramid format and mip chain access pattern for downstream passes
