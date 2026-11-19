> **Lane note 2026-11-19 (assessment, no code):** 0/27 ticked. Real state on tip fa2f9598: xatlas wrapper `GenerateLightmapUVs()` + 3 passing gtests exist (archive 2.60.0 §3) but are not wired into import; runtime `Vertex` is a fixed 48 B struct with no UV1; `.uzmesh` has no UV1 stream; no BC6H, no baker, no pass, no deferred sampling. Every slice is gated by a user decision (D1 UV1 side-stream vs wider Vertex, D2 GBuffer integration — the deferred fullscreen pass has no UV access, D3 bake engine — DXR path tracer vs radiance cache, D4 `.uztexture`+BC6H vs new `.uzlightmap`, D5 per-instance atlas granularity). See `E:/UzmiEngine/_evidence/lighting/baked-lightmap/report.md` for the assessment and the recommended Phase A–E breakdown.

## 1. Implementation

### 1.1 Lightmap UV2 Support
- [ ] 1.1.1 Extend mesh vertex format to include UV2 (lightmap UVs) channel.
  - note: `VertexFormatFlags::HasUV1` (vertex_layout.h) is an IA-descriptor only — engine uses bindless StructuredBuffer fetch of a fixed 48 B `Vertex` mirrored in 4 HLSL structs; `mesh_io.cpp` treats stride>48 as skinned. Blocked on D1 (recommend UZMS v3 side-stream flag + trailing float2 region).
- [ ] 1.1.2 Parse UV2 from glTF `TEXCOORD_1` attribute during mesh import.
  - note: `mesh_importer.cpp:488` reads only `TEXCOORD_0`; intermediate `VertexPosNormUV` has no uv1 slot. Needs D1 sink first (Phase A).
- [ ] 1.1.3 Auto-generate lightmap UVs (xatlas or simple planar unwrap) for meshes without UV2.
  - note: generator EXISTS and is proven — `lightmap_uv_generator.{h,cpp}` + `LightmapUVTest.*` 3/3 PASS (2026-11-19, main-tree test_runtime.exe, sources identical to tip). NOT wired into MeshImporter/FBX (`--generate-lightmap-uvs` deferred in 2.60.0 §3.2); xatlas splits vertices, so the importer must apply the remap. Wiring is Phase A after D1.
- [ ] 1.1.4 Pack lightmap UVs into atlas coordinates (0-1 range per mesh, scaled by atlas allocation).
  - note: blocked on D5 (per-instance scale/bias in `.uzscene` vs xatlas multi-mesh packing at bake time).

### 1.2 Lightmap Baker (GPU Compute)
- [ ] 1.2.1 Create `LightmapBaker` class that allocates lightmap atlas texture (2048x2048 RGBA16F).
  - note: nothing exists. Phase D, after D3.
- [ ] 1.2.2 Implement `lightmap_bake_cs.hlsl` — for each texel in lightmap space: reconstruct world position from UV2, trace hemisphere rays using existing radiance cache / SSGI infrastructure, accumulate indirect lighting.
  - note: SSGI is screen-space and cannot drive a bake; radiance cache is a sparse probe cache. Engine has a DXR path tracer + light BVH (`passes/pathtracing`) — recommended bake engine (D3). World pos per texel needs a UV1-chart raster pass first.
- [ ] 1.2.3 Implement denoising pass (bilateral filter or SVGF) on baked lightmap to reduce noise.
  - note: NRD exists but is screen-space; texture-space à-trous + seam dilation recommended. Phase D.
- [ ] 1.2.4 Compress and save lightmap as `.uzlightmap` asset (BC6H compressed HDR).
  - note: no BC6H anywhere (`RHIFormat` has BC1/3/4/5/7 only; `.uztexture` `TextureFormat` likewise; basis_universal BC6H encoder sources present but only the transcoder is compiled). Blocked on D4 (recommend reuse `.uztexture` + BC6H enums; RGBA16F allowed first). Phase B.

### 1.3 Lightmap Rendering
- [ ] 1.3.1 Create `LightmapPass` or extend deferred lighting to sample lightmap when available.
  - note: blocked on D2 — deferred is a fullscreen pass with no UV1; GBuffer (4 MRT) has no UV1/baked-irradiance channel. Recommend UE5-style: gbuffer_ps samples at UV1 → new RT4, deferred consumes. Phase C, gated by `r.Lightmap` default 0.
- [ ] 1.3.2 In `deferred_lighting_ps.hlsl`: when `lightmapIndex >= 0`, sample lightmap at UV2, add as indirect diffuse contribution (replacing or blending with real-time GI).
  - note: not implementable as written (no UV2 in the deferred pass); re-scope per D2. Must keep the RT2 indirect-only split correct and not fold into emissiveHDR.
- [ ] 1.3.3 Add lightmap SRV binding to deferred lighting root signature.
  - note: follows D2 (bindless index in PerFrameCB/PerObjectCB; cbuffer-layout gate `npm run test:layouts`). Phase C.

### 1.4 Scene Integration
- [ ] 1.4.1 Add `lightmapPath` field to scene file format — per-scene baked lightmap reference.
  - note: `.uzscene` JSON loader/serializer exists (`ecs/loaders/scene_loader`); also needs per-entity scale/bias + static flag (D5). Phase C.
- [ ] 1.4.2 Add editor command to trigger lightmap bake (CLI: `--bake-lightmaps`).
  - note: Phase D.
- [ ] 1.4.3 Add CVar `r.Lightmap` (0=off, 1=baked only, 2=baked+realtime blend).
  - note: not added in this lane (no CVar invented without the feature behind it). Phase C, default 0.

## 2. Testing

### 2.1 Visual Regression (Tier 2)
- [ ] 2.1.1 Bake lightmap for Cornell box scene. Verify color bleeding (red/green walls).
- [ ] 2.1.2 Compare baked lightmap vs real-time radiance cache — verify similar quality.
- [ ] 2.1.3 Verify lightmap + direct lighting combination looks correct (no double-counting).
- [ ] 2.1.4 Verify UV2 seams are not visible in lightmap (proper padding/dilation).

### 2.2 Performance
- [ ] 2.2.1 Verify lightmap rendering adds < 0.1ms GPU (single texture sample per pixel).
  - note: Phase C can prove the sampling path with a hand-authored constant-colour lightmap before any baker exists.
- [ ] 2.2.2 Benchmark bake time: target < 30s for a simple scene (Cornell box) on RTX 3060.

### 2.3 Coverage
- [ ] 2.3.1 Verify 95%+ coverage on all new C++ code.
  - note: no new C++ in this lane.

## 3. Documentation
- [ ] 3.1 Update `docs/specs/rendering/renderer.md` with lightmap baking pipeline.
- [ ] 3.2 Create `docs/guides/lightmap-baking.md` with step-by-step baking workflow.
- [ ] 3.3 Document `.uzlightmap` asset format in `docs/specs/asset-pipeline.md`.
  - note: format undecided (D4). Also: `runtime/schemas/uzmesh.schema.json` describes a JSON-header mesh format that does not match the binary UZMS/UZMH code — reconcile when documenting.

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
- [ ] 4.2 Write tests covering the new behavior
- [ ] 4.3 Run tests and confirm they pass
