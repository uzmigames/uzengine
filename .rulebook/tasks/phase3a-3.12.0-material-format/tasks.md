## 0. Material Graph Core (prerequisite)

- [x] 0.1 Implement MaterialExpression base struct with pin system, factory, and type enum (material_expression.h/.cpp)
- [x] 0.2 Implement MaterialGraph class with connection management, cycle detection, topological sort, content hashing (material_graph.h/.cpp)
- [x] 0.3 Implement JSON serialization/deserialization for material graphs (ToJson/FromJson)
- [x] 0.4 Implement .uzmat file I/O (SaveToFile/LoadFromFile) using JSON format
- [x] 0.5 Add Material log category to engine logging system (log.h)

## 1. Implementation

- [x] 1.1 Define .uzmat file format spec — JSON-based material graph format with versioning, domain, shading model, expressions array with pins and connections
- [x] 1.2 Implement .uzmat parser (MaterialGraph::FromJson + LoadFromFile)
- [x] 1.3 Implement .uzmat writer (MaterialGraph::ToJson + SaveToFile)
- [x] 1.4 MaterialManager implemented (`material_manager.h/.cpp`): load by path, cache by normalized path, monotonic IDs, hot-reload via file mod time polling. Texture path extraction from TextureSample/TextureParameter nodes. Parameter default extraction from ScalarParameter/VectorParameter.
- [x] 1.5 Texture path resolution: relative paths joined with .uzmat directory, absolute paths passed through. `std::filesystem::weakly_canonical` for normalization.
- [x] 1.6 Material-to-GPU upload: MaterialManager extracts texture paths, SceneRenderer::LoadTexture() creates SRV indices. MaterialPipeline::GetOrCreatePSO() creates PSO from graph HLSL. DrawCommand carries materialGraphPipeline + materialParams.
- [x] 1.7 Material hot-reload: MaterialManager::PollHotReload() checks file mod time each frame. Changed files re-parsed, content hash updated, caller invalidates PSO. Failure tolerance (keeps previous version on parse error).
- [x] 1.8 Default materials created: `engine/material/defaults/default_lit.uzmat` (gray 0.8, roughness 0.5) and `default_unlit.uzmat` (white emissive).
- [x] 1.9 pbr_test demo uses `mi_procgrid.uzmat` for ground material via MaterialGraph → GPU bake pipeline. Material system fully wired.
- [x] 1.10 Material showcase available via pbr_test demo with hot-reloadable .uzmat files.

## 2. Testing

- [x] 2.1 .uzmat roundtrip tested in test_material_graph.cpp (serialization tests, 6 cases).
- [x] 2.2 MaterialManager cache: LoadMaterial returns same ID for same normalized path.
- [x] 2.3 UnloadMaterial removes from cache, ID not reused (monotonic allocation).
- [x] 2.4 Texture path resolution: relative joined with .uzmat dir, absolute pass-through. weakly_canonical normalization.
- [x] 2.5 Hot-reload: PollHotReload() checks filesystem mod time, re-parses changed files, returns affected IDs.
- [x] 2.6 Missing textures: SceneRenderer::LoadTexture returns UINT32_MAX → shader uses fallback (index 0 = default white/flat).
- [x] 2.7 Default materials: default_lit.uzmat (gray albedo, roughness 0.5) and default_unlit.uzmat (white emissive) in engine/material/defaults/.
- [x] 2.8 Malformed .uzmat: MaterialGraph::LoadFromFile logs parse errors via UZ_LOG. MaterialManager returns UINT32_MAX.
- [x] 2.9 Coverage: MaterialManager + MaterialGraph + MaterialShaderCache + 45 unit tests + build clean.

## 3. Documentation

- [x] 3.1 .uzmat format documented in material_graph.h (JSON schema), docs/guides/material-graph-authoring.md (482 lines).
- [x] 3.2 Material authoring guide created at docs/guides/material-graph-authoring.md with .uzmat examples, texture requirements, hot-reload workflow, Lua API.
