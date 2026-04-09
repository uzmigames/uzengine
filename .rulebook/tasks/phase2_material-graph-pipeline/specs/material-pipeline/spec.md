# Material Graph Pipeline — Spec Delta

## ADDED Requirements

### Requirement: GBuffer Shader Material Graph Branch
The GBuffer pixel shader (`gbuffer_ps.hlsl`) MUST support a `UZ_MATERIAL_GRAPH` compile-time branch that replaces the fixed texture-sampling block with a call to the generated `ComputeMaterialOutputs()` function. The injection point SHALL be marked by `#include "material_body.hlsli"` which `MaterialPipeline::BuildCombinedPixelShaderSource()` replaces with the translator output.

#### Scenario: Material graph shader compilation
Given a MaterialGraph with a TextureSample node connected to PBROutput.BaseColor
When MaterialPipeline::GetOrCreatePSO() is called
Then the combined HLSL source contains `ComputeMaterialOutputs()` definition
And DXC compiles the source with `-DUZ_MATERIAL_GRAPH=1` without errors
And a valid RHIPipelineHandle is returned

#### Scenario: Fallback to fixed-PBR when no graph
Given a DrawCommand with materialGraphPipeline == Invalid
When the GBuffer pass processes the command
Then the default PSO is used (compiled without UZ_MATERIAL_GRAPH)
And texture sampling uses the fixed albedoIndex/roughnessIndex/metallicIndex path

### Requirement: TextureSample Texture Index Declaration
The HlslMaterialTranslator MUST emit a `texIndex[16]` array declaration in the generated HLSL preamble so that TextureSample nodes can reference `texIndex[N]` for bindless descriptor heap access. The renderer MUST upload the corresponding SRV indices to a cbuffer at register b4 before dispatch.

#### Scenario: TextureSample HLSL emission
Given a MaterialGraph with 3 TextureSample nodes
When HlslMaterialTranslator::Compile() is called
Then the generated HLSL references `texIndex[0]`, `texIndex[1]`, `texIndex[2]`
And `MaterialCompileResult::textureSlotCount` equals 3

### Requirement: Scene Loader Material Graph Support
The scene loader MUST support a `"material_graph"` field in entity material components that references a VFS-relative path to a `.uzmat` file. When present, the graph material SHALL take precedence over inline texture paths.

#### Scenario: Load entity with .uzmat material
Given a .uzscene with entity having `"material_graph": "materials/helmet.uzmat"`
When the scene is loaded
Then `StaticMeshComponent::materialGraphId` is set to a valid non-zero ID
And the MaterialManager holds the loaded graph

#### Scenario: Fallback when .uzmat missing
Given a .uzscene with entity having `"material_graph": "materials/nonexistent.uzmat"`
When the scene is loaded
Then the materialGraphId remains 0
And a warning is logged
And the entity renders with the fixed-PBR fallback (inline textures if present)

### Requirement: SyncWorldToDrawList Material Graph Integration
When `SyncWorldToDrawList()` encounters a mesh with `materialGraphId != 0`, it MUST resolve the graph via MaterialManager, call `MaterialPipeline::GetOrCreatePSO()`, and set `DrawCommand::materialGraphPipeline` to the resulting PSO handle. It MUST also populate `DrawCommand::materialParams[]` from the graph's parameter defaults.

#### Scenario: Graph material PSO binding
Given a scene with a mesh using a .uzmat material
When SyncWorldToDrawList() builds the draw list
Then cmd.materialGraphPipeline is a valid RHIPipelineHandle
And the GBuffer pass binds the graph PSO for that draw call

### Requirement: .uzmat File Format Compliance
All `.uzmat` files MUST use the format expected by `MaterialGraph::FromJson()`: expression type names from `MatExprTypeToString()`, `"scalarParams"`/`"stringParams"`/`"intParams"` for node parameters, and inline `"srcExpr"`/`"srcPin"` fields on input pins for connections.

#### Scenario: Default lit material loads correctly
Given the file `defaults/default_lit.uzmat`
When MaterialGraph::LoadFromFile() is called
Then the graph parses without error
And the PBROutput node has BaseColor connected to a Vector3Constant
And Validate() returns true

### Requirement: Per-Channel Texture Routing via Material Graph
Materials that need to read specific channels from a packed texture (e.g., glTF metallic-roughness) MUST use TextureSample expression nodes with per-channel output pins (R=pin1, G=pin2, B=pin3, A=pin4) connected to the appropriate PBROutput inputs. The engine SHALL NOT add ad-hoc channel selection fields to the fixed-PBR material path.

#### Scenario: glTF metallic-roughness channel routing
Given a .uzmat with TextureSample("metalRoughness.jpg") where output pin G(id=2) connects to PBROutput.Roughness and output pin B(id=3) connects to PBROutput.Metallic
When the material is compiled and rendered
Then the GBuffer roughness channel contains the Green channel of the texture
And the GBuffer metallic channel contains the Blue channel of the texture
