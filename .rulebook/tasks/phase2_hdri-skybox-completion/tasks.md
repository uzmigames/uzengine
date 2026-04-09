## 1. Implementation

- [ ] 1.1 Complete `HDRISkyboxPass::Execute()` — bind equirectangular-to-cubemap converted texture (or raw cubemap SRV), draw fullscreen quad at depth=1.0 (behind all geometry).
- [ ] 1.2 Wire `SkyLight::cubemap_path` to trigger HDRI skybox pass instead of procedural sky pass in `SceneRendererInit`.
- [ ] 1.3 Verify `skybox_cubemap_ps.hlsl` correctly samples the cubemap with proper view direction reconstruction from screen UV + inverse view-projection.
- [ ] 1.4 Apply exposure to HDRI skybox output (multiply by scene exposure so HDRI matches lit scene brightness).
- [ ] 1.5 Add CVar `r.HDRISkybox` (0=off, 1=on, default=1) for toggling.

## 2. Testing

### 2.1 Visual Regression (Tier 2)
- [ ] 2.1.1 Load a scene with `cubemap_path` pointing to an HDR environment. Verify skybox renders correctly behind geometry.
- [ ] 2.1.2 Verify IBL reflections match the HDRI skybox (same cubemap used for both).
- [ ] 2.1.3 Rotate camera 360 degrees — verify no seams, no flipping, correct orientation.
- [ ] 2.1.4 Verify HDRI exposure matches scene lighting (no blown-out or too-dark sky).

### 2.2 Diagnostics
- [ ] 2.2.1 Run `--pass-dump` and verify HDRI skybox appears correctly in the pass output.
- [ ] 2.2.2 Run `--diag` and verify no NaN/Inf in the sky pixels.

### 2.3 Coverage
- [ ] 2.3.1 Verify 95%+ coverage on new C++ code.

## 3. Documentation
- [ ] 3.1 Update `docs/specs/rendering/renderer.md` with HDRI skybox pass description.
- [ ] 3.2 Update `docs/guides/scene-authoring.md` with how to set up HDRI skybox via SkyLight cubemap_path.

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
- [ ] 4.2 Write tests covering the new behavior
- [ ] 4.3 Run tests and confirm they pass
