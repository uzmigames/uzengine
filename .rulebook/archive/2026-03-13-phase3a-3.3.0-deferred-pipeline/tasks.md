## 1. Implementation

- [x] 1.1 Define G-Buffer layout: Albedo (RGBA8), Normal (RGBA16Float), MetalRough (RGBA8), Depth (D32)
- [x] 1.2 Implement geometry pass: render all opaque objects → G-Buffer via render graph
- [x] 1.3 Implement G-Buffer pixel shader (pack normals, metallic/roughness)
- [x] 1.4 Implement lighting pass: fullscreen triangle, position reconstruction from depth
- [x] 1.5 Implement directional light evaluation in lighting shader
- [x] 1.6 Implement point light evaluation (attenuation, range) — up to 64 point lights
- [ ] 1.7 ~~Implement forward transparent pass after deferred resolve~~ — DEFERRED to post-processing milestone
- [x] 1.8 Register all passes in render graph with correct read/write dependencies
- [x] 1.9 Add G-Buffer visualization mode (toggle individual buffers)
- [x] 1.10 Create deferred demo: multiple lit objects + point lights
- [x] 1.11 Add RenderPath enum (Deferred/Forward) with Deferred as default
- [x] 1.12 Implement render path selection in frame pipeline (config + runtime toggle)

## 2. Testing

- [x] 2.1 Test G-Buffer format creation and render target setup
- [x] 2.2 Test geometry pass desc struct layout and field accessibility
- [x] 2.3 Test lighting pass desc struct layout and field accessibility
- [x] 2.4 Test point light data layout (PointLightData, LightingCB)
- [ ] 2.5 ~~Test transparent pass composites correctly over deferred output~~ — DEFERRED (1.7 deferred)
- [x] 2.6 Test coverage: 38 new tests in test_deferred_pipeline.cpp, 2079/2079 passing

## 3. Documentation

- [x] 3.1 Update `docs/engine/12-rendering/06-lighting.md` with deferred pipeline
- [x] 3.2 Document G-Buffer layout and deferred pipeline — `docs/engine/12-rendering/10-deferred-pipeline.md`
