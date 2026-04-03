## 1. Implementation

- [x] 1.1 Implement SimplePBRMaterial struct (albedo, normal, metallic/roughness textures + scalar params)
- [x] 1.2 Implement material binding to PSO (root signature slots for material textures)
- [x] 1.3 Implement Cook-Torrance BRDF pixel shader (GGX NDF, Smith G, Schlick Fresnel)
- [x] 1.4 Implement directional light struct and per-frame light constant buffer
- [x] 1.5 Implement 64-bit sort-key encoding (material ID, mesh ID, depth)
- [x] 1.6 Implement linear sort-key draw loop (no switch-on-type)
- [x] 1.7 Register forward PBR pass in render graph
- [x] 1.8 Create PBR demo: metallic/rough sphere grid with directional light

## 2. Testing

- [x] 2.1 Test material struct layout and PSO binding
- [x] 2.2 Test sort-key encoding/decoding roundtrip
- [x] 2.3 Test BRDF shader compilation (DXC + FXC paths)
- [x] 2.4 Test draw loop ordering matches sort-key order
- [x] 2.5 Verify 95%+ coverage on all new code

## 3. Documentation

- [x] 3.1 Update `docs/engine/12-rendering/` with material system and PBR docs
- [x] 3.2 Document sort-key layout and draw loop architecture
