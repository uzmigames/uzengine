## 1. Implementation

### 1.1 Lightmap UV2 Support
- [ ] 1.1.1 Extend mesh vertex format to include UV2 (lightmap UVs) channel.
- [ ] 1.1.2 Parse UV2 from glTF `TEXCOORD_1` attribute during mesh import.
- [ ] 1.1.3 Auto-generate lightmap UVs (xatlas or simple planar unwrap) for meshes without UV2.
- [ ] 1.1.4 Pack lightmap UVs into atlas coordinates (0-1 range per mesh, scaled by atlas allocation).

### 1.2 Lightmap Baker (GPU Compute)
- [ ] 1.2.1 Create `LightmapBaker` class that allocates lightmap atlas texture (2048x2048 RGBA16F).
- [ ] 1.2.2 Implement `lightmap_bake_cs.hlsl` — for each texel in lightmap space: reconstruct world position from UV2, trace hemisphere rays using existing radiance cache / SSGI infrastructure, accumulate indirect lighting.
- [ ] 1.2.3 Implement denoising pass (bilateral filter or SVGF) on baked lightmap to reduce noise.
- [ ] 1.2.4 Compress and save lightmap as `.uzlightmap` asset (BC6H compressed HDR).

### 1.3 Lightmap Rendering
- [ ] 1.3.1 Create `LightmapPass` or extend deferred lighting to sample lightmap when available.
- [ ] 1.3.2 In `deferred_lighting_ps.hlsl`: when `lightmapIndex >= 0`, sample lightmap at UV2, add as indirect diffuse contribution (replacing or blending with real-time GI).
- [ ] 1.3.3 Add lightmap SRV binding to deferred lighting root signature.

### 1.4 Scene Integration
- [ ] 1.4.1 Add `lightmapPath` field to scene file format — per-scene baked lightmap reference.
- [ ] 1.4.2 Add editor command to trigger lightmap bake (CLI: `--bake-lightmaps`).
- [ ] 1.4.3 Add CVar `r.Lightmap` (0=off, 1=baked only, 2=baked+realtime blend).

## 2. Testing

### 2.1 Visual Regression (Tier 2)
- [ ] 2.1.1 Bake lightmap for Cornell box scene. Verify color bleeding (red/green walls).
- [ ] 2.1.2 Compare baked lightmap vs real-time radiance cache — verify similar quality.
- [ ] 2.1.3 Verify lightmap + direct lighting combination looks correct (no double-counting).
- [ ] 2.1.4 Verify UV2 seams are not visible in lightmap (proper padding/dilation).

### 2.2 Performance
- [ ] 2.2.1 Verify lightmap rendering adds < 0.1ms GPU (single texture sample per pixel).
- [ ] 2.2.2 Benchmark bake time: target < 30s for a simple scene (Cornell box) on RTX 3060.

### 2.3 Coverage
- [ ] 2.3.1 Verify 95%+ coverage on all new C++ code.

## 3. Documentation
- [ ] 3.1 Update `docs/specs/rendering/renderer.md` with lightmap baking pipeline.
- [ ] 3.2 Create `docs/guides/lightmap-baking.md` with step-by-step baking workflow.
- [ ] 3.3 Document `.uzlightmap` asset format in `docs/specs/asset-pipeline.md`.

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
- [ ] 4.2 Write tests covering the new behavior
- [ ] 4.3 Run tests and confirm they pass
