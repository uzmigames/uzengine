## 1. Shader Integration (GAP 1)
- [ ] 1.1 Add `#include "material_body.hlsli"` injection marker to `gbuffer_ps.hlsl`
- [ ] 1.2 Add `#ifdef UZ_MATERIAL_GRAPH` branch in `gbuffer_ps.hlsl` main() that declares `materialParams` cbuffer at b3 and calls `ComputeMaterialOutputs()` to populate albedo/metallic/roughness/normal/emissive/opacity/ao
- [ ] 1.3 Add `cbuffer MaterialTextureCB : register(b4) { uint texIndex[16]; }` declaration for texture slot indices used by generated HLSL

## 2. HLSL Translator Fix (GAP 6)
- [ ] 2.1 Fix `HlslMaterialTranslator` TextureSample emission to reference `texIndex[N]` from the MaterialTextureCB instead of undeclared `texIndex_N` variables
- [ ] 2.2 Track texture slot count in `MaterialCompileResult::textureSlotCount` so the renderer knows how many texture indices to upload

## 3. Default Material Fix (GAP 5)
- [ ] 3.1 Fix `defaults/default_lit.uzmat` — use `"Vector3Constant"` type name, `"scalarParams"` key, inline `srcExpr`/`srcPin` on input pins
- [ ] 3.2 Fix `defaults/default_unlit.uzmat` — same format corrections
- [ ] 3.3 Verify both files load via `MaterialGraph::LoadFromFile()` without parse errors

## 4. ECS Component (GAP 4)
- [ ] 4.1 Add `uint32_t materialGraphId = 0` field to `StaticMeshComponent` (0 = no graph, use fixed-PBR path)
- [ ] 4.2 Add `std::string materialGraphPath` field to `StaticMeshComponent` for VFS-relative .uzmat path

## 5. Scene Loader (GAP 3)
- [ ] 5.1 Add `"material_graph": "path/to/material.uzmat"` field parsing in `scene_loader.cpp` `ParseMaterialComponent()`
- [ ] 5.2 When `material_graph` is present, call `MaterialManager::LoadMaterial(path)` and store the returned ID in `StaticMeshComponent::materialGraphId`
- [ ] 5.3 Material graph and inline textures are mutually exclusive — graph takes precedence if both are specified

## 6. Renderer Integration (GAP 2)
- [ ] 6.1 Add `std::unique_ptr<MaterialPipeline> m_materialPipeline` to `SceneRenderer` private members
- [ ] 6.2 Initialize `MaterialPipeline` in `SceneRenderer::Init()` (after shader pipeline is ready)
- [ ] 6.3 In `SyncWorldToDrawList()`: when `meshComp.materialGraphId != 0`, resolve the graph via `MaterialManager`, call `MaterialPipeline::GetOrCreatePSO(graph)`, set `cmd.materialGraphPipeline` and populate `cmd.materialParams[]`
- [ ] 6.4 Upload texture SRV indices for graph materials into the MaterialTextureCB (b4) before draw

## 7. DamagedHelmet Material
- [ ] 7.1 Create `sample/content/materials/damaged_helmet.uzmat` with: TextureSample(Default_albedo.jpg)→BaseColor, TextureSample(Default_metalRoughness.jpg).G→Roughness, TextureSample(Default_metalRoughness.jpg).B→Metallic, TextureSample(Default_AO.jpg).R→AO, TextureSample(Default_normal.jpg)→Normal, TextureSample(Default_emissive.jpg)→Emissive
- [ ] 7.2 Update `damaged_helmet.uzscene` to use `"material_graph": "materials/damaged_helmet.uzmat"` instead of inline texture paths
- [ ] 7.3 Verify DamagedHelmet renders with correct metallic reflections and emissive

## 8. Sample Scene Migration
- [ ] 8.1 Create .uzmat materials for all sample scenes that use inline textures (cornell_box, pbr_showcase, bloom_test, etc.)
- [ ] 8.2 Update each .uzscene to reference .uzmat instead of inline texture paths
- [ ] 8.3 Verify all sample scenes render correctly after migration

## 9. Testing
- [ ] 9.1 Unit test: `MaterialGraph::LoadFromFile()` on fixed default_lit.uzmat parses without error
- [ ] 9.2 Unit test: `HlslMaterialTranslator::Compile()` on a graph with TextureSample produces valid HLSL with `texIndex[N]` references
- [ ] 9.3 Integration test: Load damaged_helmet.uzmat → compile → create PSO → no DXC errors
- [ ] 9.4 Visual test: DamagedHelmet renders with HDRI reflections on metallic surfaces (PSNR comparison with reference)
- [ ] 9.5 Verify 95%+ coverage on all new code

## 10. Documentation
- [ ] 10.1 Create `docs/specs/rendering/material-graph-pipeline.md` — data flow from .uzmat → HLSL → PSO → GBuffer
- [ ] 10.2 Update `docs/guides/shader-authoring.md` with material graph section
- [ ] 10.3 Add .uzmat format reference to `docs/guides/scene-authoring.md`
