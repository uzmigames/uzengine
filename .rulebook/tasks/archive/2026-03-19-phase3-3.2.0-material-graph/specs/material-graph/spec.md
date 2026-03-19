# Material Graph System — Spec Delta

## ADDED Requirements

### Requirement: MaterialGraph DAG Representation
The MaterialGraph SHALL represent materials as a directed acyclic graph of MaterialNode objects connected via typed MaterialPin links. Each node SHALL have zero or more input pins and one or more output pins. Pin types SHALL be: Float, Float2, Float3, Float4, Texture. The graph MUST reject cycles at connection time.

#### Scenario: Connect two nodes
Given a MaterialGraph with a ScalarConstant node and a Multiply node
When the user connects ScalarConstant.output(Float) to Multiply.inputA(Float)
Then the connection is stored in the graph
And topological sort returns [ScalarConstant, Multiply]

#### Scenario: Reject cycle
Given a MaterialGraph with nodes A→B→C
When the user attempts to connect C.output to A.input
Then the connection is rejected with error "Cycle detected"
And the graph remains unchanged

#### Scenario: Type mismatch auto-promotion
Given a ScalarConstant(Float) connected to a Multiply.inputA(Float3)
When the compiler processes the connection
Then a float→float3 splat is auto-inserted (val → float3(val, val, val))

### Requirement: PBR Output Node
The MaterialGraph MUST contain exactly one PBR Output node with pins: BaseColor(Float3), Metallic(Float), Roughness(Float), Normal(Float3), Emissive(Float3), Opacity(Float), AO(Float). Each pin SHALL have a default value used when unconnected: BaseColor=(1,1,1), Metallic=0, Roughness=0.5, Normal=(0,0,1), Emissive=(0,0,0), Opacity=1, AO=1.

#### Scenario: Unconnected pins use defaults
Given a MaterialGraph with only BaseColor connected
When the compiler generates HLSL
Then Metallic outputs 0.0, Roughness outputs 0.5, Normal outputs float3(0,0,1)
And no dead code is generated for unused node subgraphs

### Requirement: HLSL Code Generation
The MaterialCompiler MUST walk the graph in topological order and generate a valid HLSL function `ComputeMaterialOutputs()` that writes to output variables. Generated code MUST compile with DXC targeting SM6.0+. Dead-code elimination MUST skip nodes not reachable from any active output pin.

#### Scenario: Simple constant color material
Given a MaterialGraph with ColorConstant(0.8, 0.2, 0.1) → BaseColor
When the compiler generates HLSL
Then the output contains `out_baseColor = float3(0.8, 0.2, 0.1);`
And no other node computations are emitted

#### Scenario: Checker pattern material
Given a MaterialGraph with TexCoord → CheckerPattern(scale=10) → Lerp(A=white, B=gray) → BaseColor
When the compiler generates HLSL
Then the output contains a checker formula using `floor(uv * scale)` and `fmod`
And the Lerp blends between the two colors using the checker mask

### Requirement: Material Shader Cache
The MaterialShaderCache MUST cache compiled shader blobs keyed by xxHash64 of the generated HLSL source. On cache hit, the previously compiled blob MUST be reused without recompilation. Cache files SHALL be stored under `cache/materials/` with filename `<hash>.dxil`.

#### Scenario: Cache hit
Given a MaterialGraph that was previously compiled with hash 0xABCD1234
When the same graph is loaded again (identical content hash)
Then no DXC compilation occurs
And the cached DXIL blob is loaded from `cache/materials/abcd1234.dxil`

#### Scenario: Cache miss after graph change
Given a cached material with hash 0xABCD1234
When a node parameter is changed (e.g., checker scale 10 → 20)
Then the content hash changes to a different value
And DXC recompilation occurs
And the new blob is cached

### Requirement: Material Serialization Format
Materials SHALL be serialized as `.uzmat` JSON files. The format MUST include: graph metadata (domain, shading model), node array (id, type, position, parameters), connection array (source node+pin → target node+pin), and exposed parameter list (name, type, default value).

#### Scenario: Round-trip serialization
Given a MaterialGraph with 5 nodes and 4 connections
When the graph is saved to `test.uzmat` and loaded back
Then the loaded graph has identical nodes, connections, and parameters
And the content hash matches the original

### Requirement: Procedural Checker Node
The CheckerPattern node SHALL take UV(Float2) and Scale(Float) inputs and output a Float mask alternating between 0.0 and 1.0 in a checkerboard pattern. The HLSL implementation MUST use `floor(uv * scale)` with modulo to produce the pattern without any texture sampling.

#### Scenario: Checker at scale 10
Given a CheckerPattern node with Scale=10.0
When evaluated at UV=(0.05, 0.05)
Then the output is 0.0 (first tile)

#### Scenario: Checker at adjacent tile
Given a CheckerPattern node with Scale=10.0
When evaluated at UV=(0.15, 0.05)
Then the output is 1.0 (adjacent tile)

### Requirement: TriPlanar Mapping Node
The TriPlanarMapping node SHALL take WorldPosition(Float3), WorldNormal(Float3), and BlendSharpness(Float) inputs. It SHALL output UV(Float2) and BlendWeights(Float3) that can be used to sample textures from three axis-aligned projections and blend them seamlessly. The blending MUST use `pow(abs(normal), sharpness)` normalized to sum to 1.0.

#### Scenario: Flat horizontal surface
Given a TriPlanarMapping node with BlendSharpness=4.0
When the WorldNormal is (0, 0, 1) (Z-up flat surface)
Then BlendWeights.z is approximately 1.0 and BlendWeights.xy are approximately 0.0
And UV outputs the XY world position projection

### Requirement: Material Parameter Override
ScalarParameter and VectorParameter nodes MUST expose named values that can be overridden per MaterialInstance without recompiling the shader. Parameter values SHALL be uploaded via the PerObjectCB material parameter block (16 float4 slots). The compiler MUST assign contiguous indices to parameters and generate corresponding `materialParams[N]` reads in HLSL.

#### Scenario: Override scalar parameter
Given a material with ScalarParameter "Roughness" (default 0.5)
When a MaterialInstance overrides "Roughness" to 0.8
Then the rendered surface uses roughness 0.8
And no shader recompilation occurs

### Requirement: Hot-Reload
When a `.uzmat` file is modified on disk, the runtime MUST detect the change, recompile the graph, regenerate HLSL, recompile via DXC, create a new PSO, and swap to the new PSO at the next frame boundary. The old PSO MUST remain valid until the swap completes (no frame glitches).

#### Scenario: Edit and reload
Given a rendered scene using material `ground.uzmat`
When the user modifies `ground.uzmat` on disk (changes checker scale)
Then within 2 frames, the rendered ground updates to the new scale
And no rendering artifacts occur during the transition
