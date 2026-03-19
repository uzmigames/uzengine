## 1. Core Graph Data Model
- [x] 1.1 Implement `MaterialPin` struct (id, type enum [Float/Float2/Float3/Float4/Texture], name, default value, connection target). Implemented as `MatPin` in `material_expression.h`.
- [x] 1.2 Implement `MaterialNode` base class (id, type enum, input pins, output pins, parameter storage). Implemented as `MaterialExpression` in `material_expression.h` with scalarParams/stringParams/intParams maps.
- [x] 1.3 Implement `MaterialGraph` class (owns nodes, validates DAG, topological sort, output pin mapping to PBR channels). Implemented in `material_graph.h/.cpp` with Connect/Disconnect/Validate/TopologicalSort/ComputeContentHash.
- [x] 1.4 Implement PBR output node with pins: BaseColor(float3), Metallic(float), Roughness(float), Normal(float3), Emissive(float3), Opacity(float), AO(float). Implemented in `SetupPBROutput()` in `material_expression.cpp`.
- [x] 1.5 Implement `.uzmat` JSON serialization (save/load graph + all node params + connections). Implemented in `MaterialGraph::ToJson()`/`FromJson()`/`SaveToFile()`/`LoadFromFile()`.
- [x] 1.6 Implement graph content hashing (xxHash64 over sorted node types + params + connections) for cache keying. Implemented in `MaterialGraph::ComputeContentHash()` using xxHash64.

## 2. Built-in Node Library
- [x] 2.1 Constant nodes: ScalarConstant, VectorConstant(2/3/4), ColorConstant. All implemented in `material_expression.cpp` with factory + pin setup.
- [x] 2.2 Math nodes: Add, Subtract, Multiply, Divide, Lerp, Clamp, Saturate, Abs, Pow, Sqrt, OneMinus, Frac, Floor, Ceil, Min, Max, Dot, Cross, Normalize, Length. All 22 math ops implemented.
- [x] 2.3 Texture nodes: TextureSample (UV input, texture path param, output RGBA + individual channels), TextureObject. Implemented with TextureSample + TextureParameter.
- [x] 2.4 Coordinate nodes: TexCoord (UV0/UV1), WorldPosition, ObjectPosition, VertexNormal, VertexColor, CameraVector, ScreenPosition. All 8 coordinate nodes implemented.
- [x] 2.5 Procedural nodes: CheckerPattern (UV + scale → float mask), Noise (Perlin/Simplex, UV + scale + octaves → float), GradientLinear, GradientRadial. All 4 procedural nodes implemented.
- [x] 2.6 Utility nodes: TriPlanarMapping, Fresnel, Panner, ComponentMask, Append. All 5 utility nodes implemented.
- [x] 2.7 Material parameter nodes: ScalarParameter, VectorParameter, TextureParameter. All 3 parameter nodes implemented with default values.

## 3. HLSL Code Generator
- [x] 3.1 Implement `MaterialCompiler` that walks the graph in topological order and emits HLSL variable declarations per node output. Implemented as `HlslMaterialTranslator::Compile()` with memoized `EmitExpression()`.
- [x] 3.2 Each node type implements `EmitHLSL(compiler)` returning the HLSL expression string for its output(s). All 37 MatExprType cases handled in `EmitExpression()` switch.
- [x] 3.3 Dead-code elimination: only emit nodes reachable from active output pins (skip disconnected subgraphs). Uses `graph.TopologicalSort()` which only returns reachable nodes.
- [x] 3.4 Type promotion/casting: auto-insert float→float3 splat, float3→float4(,1), etc. at pin connections with mismatched dimensions. `PromoteType()` handles splat, extend, shrink, truncation.
- [x] 3.5 Generate `ComputeMaterialOutputs()` function that writes to `out_baseColor`, `out_metallic`, `out_roughness`, `out_normal`, `out_emissive`, `out_ao`, `out_opacity`.
- [x] 3.6 Generate parameter binding code: each ScalarParameter/VectorParameter reads from `materialParams[N]` in PerObjectCB. `AllocateParamSlot()` assigns contiguous indices.
- [x] 3.7 Generate texture binding code: each TextureSample emits `ResourceDescriptorHeap[texIndex_N]` (bindless). `AllocateTextureSlot()` assigns contiguous indices.

## 4. Shader Compilation + PSO Integration
- [x] 4.1 Added `#ifdef UZ_MATERIAL_GRAPH` guard to `gbuffer_ps.hlsl` that replaces fixed texture sampling with `#include "material_body.hlsli"` + `ComputeMaterialOutputs()` call. MaterialParamsCB (16 x float4) at register b3. Existing fixed path preserved in `#else`. @source UE5 MaterialTemplate.ush
- [x] 4.2 Integrate with `ShaderCompiler`: compile generated HLSL via DXC with material-specific defines. Implemented in `MaterialPipeline::GetOrCreatePSO()` — uses `ShaderPipeline::LoadBytecodeFromSource()` with `UZ_MATERIAL_GRAPH=1` defines to compile combined gbuffer_ps + material body HLSL.
- [x] 4.3 Implement `MaterialShaderCache`: content-addressed by xxHash64 of generated HLSL source. Created `material_shader_cache.h/.cpp` with Init(), CompileGraph(), IsCached(), ClearCache(), GetHlsl(). Source-level cache (HLSL + metadata), DXC compilation deferred to renderer layer.
- [x] 4.4 Integrate with `PipelineLibrary`: create PSO keyed by (material hash + render pass + vertex layout). Reuse PSO for all instances sharing same graph. Created `MaterialPipeline` class (`material_pipeline.h/.cpp` in `runtime/engine/renderer/material/`) — caches PSOs by content hash with separate bindless/SM5 variants, replaces `#include "material_body.hlsli"` in gbuffer_ps source, creates GBuffer-compatible PSO matching PipelineLibrary::GetGBuffer() RT formats exactly.
- [x] 4.5 Material parameters use separate `MaterialParamsCB` at register b3 (16 x float4 = 256 bytes) instead of extending PerObjectCB. Declared in gbuffer_ps.hlsl under UZ_MATERIAL_GRAPH guard. DrawCommand carries materialParams[64] + materialParamCount for per-draw upload.
- [x] 4.6 GBuffer pass detects `drawCmd.materialGraphPipeline.IsValid()`, switches PSO, allocates + uploads MaterialParamsCB at root slot 3, then restores default PSO after draw. gbuffer_pass.cpp modified.

## 5. Runtime Integration
- [x] 5.1 Extend `Material`/`MaterialInterface` to optionally reference a `MaterialGraph*` instead of fixed texture paths. Created `MaterialAssetGraph` class (`material_asset_graph.h/.cpp`) that owns a MaterialGraph, compiles via MaterialShaderCache, and provides generated HLSL + parameter metadata to the renderer. Coexists with the traditional Material struct.
- [x] 5.2 GBuffer pass detects graph materials via `drawCmd.materialGraphPipeline.IsValid()`. When valid, switches PSO to material-graph-compiled pipeline, uploads MaterialParamsCB at b3, draws, then restores default PSO. Scene renderer populates DrawCommand from MaterialPipeline::GetOrCreatePSO().
- [x] 5.3 Fallback: when `materialGraphPipeline` is invalid (compilation failed or no graph), the draw command uses the default GBuffer PSO with fixed texture sampling. Error logged by MaterialPipeline::GetOrCreatePSO().
- [x] 5.4 Hot-reload: MaterialPipeline::InvalidatePSO(hash) removes cached PSO. Next GetOrCreatePSO() call recompiles from the updated graph. FileWatcher integration for automatic .uzmat detection deferred to editor panel work.
- [x] 5.5 Implement `MI_ProcGrid` equivalent: checker pattern + line mask + angle darkening as a `.uzmat` file, use as PBR test ground material. Created `runtime/demos/pbr_test/assets/mi_procgrid.uzmat` with checker pattern at scale 8, light/dark gray color lerp for BaseColor, smooth/rough lerp for Roughness.
- [x] 5.6 Implement `MaterialBaker` CPU-side graph evaluator (`material_baker.h/.cpp`). Walks the graph in topological order, evaluates all expression types per texel, produces RGBA8 pixel data. Supports BakeBaseColor/BakeRoughness/BakeMetallic/BakeNormal/BakeAO.
- [x] 5.7 Wire MaterialGraph into PBR test demo: loads `mi_procgrid.uzmat`, compiles to HLSL, bakes BaseColor (512x512) and Roughness (256x256) via MaterialBaker, writes PNG textures to disk, assigns to ground material for rendering via standard texture path. End-to-end pipeline verified with visual screenshot.
- [x] 5.8 Build system integration: added `engine/material` and `engine/material/compiler` to runtime build.zig (include paths + source compilation) and demo common build (include paths). Added `mi_procgrid.uzmat` to demo deploy list.
- [x] 5.9 Implement `GpuMaterialBaker` (`gpu_material_baker.h/.cpp`): GPU compute dispatch replaces 8-second CPU MaterialBaker bottleneck. Generates compute shader wrapper around material HLSL, compiles via ShaderPipeline (cached), dispatches 8x8 thread groups, returns RGBA8 UAV+SRV texture directly. PBR test demo updated to use GPU bake for BaseColor (1024x1024) and Roughness (512x512). Added GetDevice()/GetShaderManager() accessors to SceneRenderer.

## 6. Testing
- [x] 6.1 Unit test: graph serialization round-trip (save → load → compare). Written in `tests/material/test_material_graph.cpp`: verifies node count (3), connection wiring, ScalarConstant 'value' param (0.75), domain/shadingModel, graph name, malformed JSON, missing version.
- [x] 6.2 Unit test: topological sort with diamond dependencies (A→B→D, A→C→D). Diamond test verifies A < B, A < C, B < D, C < D, D < PBROutput, all 5 IDs present. Also tests empty-connection (PBROutput only) case.
- [x] 6.3 Unit test: dead-code elimination removes disconnected nodes from HLSL output. Tests single disconnected node and complex dead-path (C→D disconnected from PBR): only live-path nodes appear in TopologicalSort.
- [x] 6.4 Unit test: type promotion (float→float3 splat at Multiply node). ScalarConstant(Float) → PBROutput BaseColor(Float3): asserts "float3(" and "0.5" appear in HLSL. Also tests Float3→Float3 (no extra wrapping needed).
- [x] 6.5 Unit test: HLSL codegen for each math node (Add, Multiply, Lerp, Clamp, etc.) produces correct expressions. Tests Add(+), Multiply(*), Subtract(-), Divide(/), Lerp(lerp()), Clamp(clamp()), Saturate(saturate()). Also verifies explicit variable names and connected input references.
- [x] 6.6 Unit test: CheckerPattern node generates correct HLSL (floor(uv * scale) mod 2 pattern). Asserts "floor(" and "fmod(" both present. Also tests custom scale wired via ScalarConstant.
- [x] 6.7 Unit test: MaterialShaderCache hit/miss (same graph → same hash, modified param → different hash). Tests: same graph → same hash + cache size=1; modified param → different hash + cache size=2; IsCached before/after; ClearCache empties; GetHlsl matches result; GetHlsl unknown hash returns null; hash determinism.
- [x] 6.8 Unit test: parameter binding indices are contiguous and match PerObjectCB layout. Three ScalarParameters → slots sorted must equal [0,1,2]; ScalarParameter type=Float; VectorParameter type=Float3; mixed scalar+vector slots contiguous; default value preserved in ParamInfo.
- [ ] 6.9 Integration test: compile a simple graph (constant color → BaseColor) through full pipeline (graph → HLSL → DXC → PSO).
- [ ] 6.10 Visual test: render MI_ProcGrid equivalent on ground plane, compare with UE5 screenshot.
- [ ] 6.11 Verify 95%+ coverage on all new code.

## 7. Documentation
- [ ] 7.1 Update `docs/engine/12-rendering/16-material-system.md` with implemented architecture, node list, and `.uzmat` format reference.
- [ ] 7.2 Create `docs/guides/material-graph-authoring.md` with examples of creating procedural materials via `.uzmat` JSON.
- [ ] 7.3 Update CHANGELOG with material graph feature entry.
