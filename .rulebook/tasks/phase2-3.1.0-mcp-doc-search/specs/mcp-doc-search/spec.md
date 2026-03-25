# MCP Auto-Documentation Generator + Hybrid Search — Spec Delta

## ADDED Requirements

### Requirement: Doc Comment Parser
The generator SHALL parse all `///` and `///<` doc comment blocks from C++ (`.h`, `.cpp`), HLSL (`.hlsl`, `.hlsli`), and TypeScript (`.ts`) source files under `runtime/`, `mcp/`, and `extension/`. Each parsed doc block MUST be associated with the immediately following code declaration (class, struct, enum, function, field, cbuffer, macro). The parser MUST extract: brief description (first paragraph), extended description, named sections (`# Arguments`, `# Returns`, `# Example`, `# Thread Safety`, etc.), `@ref` citations, `@see` cross-references, type information, and default values.

#### Scenario: Parse C++ class with sections
Given a C++ header file containing a `///` doc block with `# Design` and `# Thread Safety` sections followed by `class TimeManager`
When the C++ parser processes the file
Then a `DocumentedEntity` with kind `"class"` and name `"TimeManager"` is produced
And the entity contains both `"Design"` and `"Thread Safety"` in its sections map

#### Scenario: Parse trailing member comment
Given a C++ struct with a field `float fogDensity = 0.02f; ///< Fog density per meter`
When the C++ parser processes the file
Then a `DocumentedEntity` with kind `"field"` is produced
And its brief is `"Fog density per meter"`
And its defaultValue is `"0.02f"`

#### Scenario: Parse @ref citation
Given a doc comment containing `/// @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — Mie absorption`
When the parser extracts metadata
Then a `RefDoc` is produced with author `"Bruneton"`, year `"2008"`, title `"Precomputed Atmospheric Scattering"`, and note `"Mie absorption"`

#### Scenario: Parse HLSL cbuffer
Given an HLSL file containing `/// Per-frame constants` followed by `cbuffer PerFrameCB : register(b0)`
When the HLSL parser processes the file
Then a `DocumentedEntity` with kind `"shader_cb"` and name `"PerFrameCB"` is produced

#### Scenario: Parse HLSL shader function
Given an HLSL file containing a documented function `float3 GetExponentialFogColor(float3 worldPos)`
When the HLSL parser processes the file
Then a `DocumentedEntity` with kind `"shader_fn"` is produced
And its params include `worldPos` with type `"float3"`

#### Scenario: Parse file-level documentation
Given a C++ file where the first `///` block appears before any declaration
When the parser processes the file
Then a `DocumentedEntity` with kind `"file"` is produced with the file path as its name

### Requirement: Subsystem Mapping
The generator SHALL derive a subsystem name for every entity based on its file path. At minimum, the following path prefixes MUST map to distinct subsystems: `engine/app/` → `application`, `engine/ecs/` → `ecs`, `engine/renderer/` → `renderer`, `engine/renderer/rhi/` → `rhi`, `engine/shader/` → `shader-system`, `engine/asset/` → `assets`, `engine/scripting/` → `scripting`, `engine/math/` → `math`, `engine/job/` → `job-system`, `editor/` → `editor`, `platform/` → `platform`, `shaders/` → `shaders`. The most specific path prefix MUST win (e.g., `engine/renderer/rhi/` over `engine/renderer/`).

#### Scenario: Specific prefix wins
Given an entity from file `runtime/engine/renderer/rhi/dx12_fence.h`
When the subsystem mapper processes it
Then the entity's subsystem is `"rhi"` (not `"renderer"`)

#### Scenario: Unknown path gets fallback
Given an entity from a file not matching any mapped prefix
When the subsystem mapper processes it
Then the entity's subsystem is `"other"`

### Requirement: Auto-Tag Generation
The generator SHALL automatically assign tags to entities based on file path and content patterns. At minimum: `component` for entities in `ecs/` or names ending with `Component`, `pass` for entities in `passes/` or names ending with `Pass`, `shader` for entities from `.hlsl`/`.hlsli` files, `cbuffer` for HLSL cbuffer blocks, `public-api` for documented entities in `.h` files, `internal` for entities only in `.cpp` files, `reference` for entities with `@ref` citations.

#### Scenario: Component auto-tag
Given a struct named `HeightFogComponent` in `engine/ecs/environment/`
When the tag generator processes it
Then the entity's tags include `"component"`

#### Scenario: Reference auto-tag
Given an entity with a `@ref Bruneton 2008` citation
When the tag generator processes it
Then the entity's tags include `"reference"`

### Requirement: Relationship Resolution
The relationship resolver SHALL identify cross-references between entities after all parsing is complete. The resolver MUST detect: inheritance (`: public Base` → `inherits`), `@see` references (→ `references`), and cbuffer↔struct binding (C++ struct name matching HLSL cbuffer name → `binds_to`). The resolver SHOULD detect: composition (field type matches entity name → `contains`), include graph (`#include` → `uses`), component→renderer data flow (→ `reads_from`).

#### Scenario: Inheritance resolution
Given a class declared as `class AudioSubsystem final : public ISubsystem`
When the relationship resolver processes it
Then an `inherits` relationship targeting `"ISubsystem"` is added

#### Scenario: CB↔shader binding
Given a C++ struct named `PerFrameCB` and an HLSL `cbuffer PerFrameCB`
When the relationship resolver processes all entities
Then a `binds_to` relationship is created between them

#### Scenario: @see resolution
Given an entity with `@see TimeManager, EngineLoop`
When the relationship resolver processes it
Then `references` relationships to `"TimeManager"` and `"EngineLoop"` are added

### Requirement: docs.json Output
The generator SHALL produce a single `docs.json` file in `mcp/data/` containing an array of all `DocumentedEntity` objects. The JSON MUST be valid, MUST contain all parsed entities with no data loss, and MUST be under 5 MB for the current codebase. The file MUST be deterministic — same input produces same output (sorted by file path then line number).

#### Scenario: Full generation
Given the UzEngine codebase with ~5,200 doc comment lines
When `generate-docs.ts` is run without flags
Then `mcp/data/docs.json` is written with > 500 entities
And the file is valid JSON under 5 MB
And every entity has a non-empty `id`, `name`, `kind`, `file`, and `line`

#### Scenario: Incremental generation
Given a previously generated docs.json and one modified `.h` file
When `generate-docs.ts --incremental` is run
Then only the modified file is re-parsed
And entities from the modified file are updated in docs.json
And entities from unmodified files are unchanged

### Requirement: TF-IDF Embedding Generation
The search system SHALL compute 256-dimensional float vectors for every entity using TF-IDF weighting with FNV1a 32-bit hashing for bucket assignment. The embedding function MUST use signed hash direction to avoid all-positive vector collapse. Vectors MUST be L2-normalized. The implementation MUST be pure TypeScript with zero external API calls.

#### Scenario: Embedding determinism
Given the same entity with the same doc content
When embeddings are computed twice
Then both embeddings are identical (bitwise)

#### Scenario: Semantic similarity
Given two entities both documenting fog-related functionality
When their embeddings are computed
Then their cosine similarity is > 0.3

#### Scenario: Dissimilar entities
Given an entity about fog rendering and an entity about audio mixing
When their embeddings are computed
Then their cosine similarity is < 0.2

### Requirement: HNSW Vector Index
The search system SHALL build an HNSW (Hierarchical Navigable Small World) index over entity embeddings for approximate nearest neighbor search. The implementation MUST be pure TypeScript. Parameters: M=16, efConstruction=200, efSearch=50 (configurable at query time), dimensions=256, cosine distance metric. The index MUST support serialization to and deserialization from a binary format (`search-index.bin`).

#### Scenario: Search recall
Given an HNSW index built from 1000 entity embeddings
When a query vector is searched with efSearch=50, k=10
Then at least 8 of the 10 results match the brute-force top-10 (recall ≥ 80%)

#### Scenario: Serialization roundtrip
Given an HNSW index built and serialized to `search-index.bin`
When the index is deserialized from the same file
Then search results are identical to the pre-serialization index

### Requirement: BM25 Keyword Search
The search system SHALL build an in-memory inverted index from entity documents and score queries using BM25 with k1=1.2 and b=0.75. Entity fields MUST be weighted: name (3×), brief (2×), refs (1.5×), tags (1.5×), description (1×). The tokenizer MUST split CamelCase, PascalCase, and snake_case identifiers into component words and remove stop words.

#### Scenario: Exact name match
Given entities including one named `TimeManager`
When BM25 search is run for query `"TimeManager"`
Then `TimeManager` is ranked #1

#### Scenario: Partial term match
Given entities including `HeightFogComponent` with brief containing "fog density"
When BM25 search is run for query `"fog density"`
Then `HeightFogComponent` appears in the top 5

### Requirement: Reciprocal Rank Fusion
The search system SHALL combine BM25 and HNSW result lists using Reciprocal Rank Fusion with constant k=60. The formula MUST be: `RRF_score(d) = Σ 1/(k + rank_i(d))` where `rank_i(d)` is the 1-based rank in each list. Items appearing in both lists MUST receive boosted scores from both contributions.

#### Scenario: Dual-list boost
Given an entity ranked #3 in BM25 results and #5 in HNSW results
When RRF fusion is applied
Then its RRF score is `1/(60+3) + 1/(60+5)` = `1/63 + 1/65`

#### Scenario: Single-list presence
Given an entity ranked #1 in BM25 results but absent from HNSW top-K
When RRF fusion is applied
Then its RRF score is `1/(60+1)` = `1/61`

### Requirement: search_docs MCP Tool
The MCP server SHALL expose a `search_docs` tool that accepts: `query` (string, required), `kind` (entity kind filter, optional), `subsystem` (string filter, optional), `tags` (string array, AND logic, optional), `limit` (number, default 10). The tool MUST perform hybrid BM25 + HNSW search with RRF fusion, apply post-filters, and return results with: id, name, kind, file, line, brief, subsystem, tags, relevance score.

#### Scenario: Filtered search
Given the full doc index is loaded
When `search_docs(query="shadow", kind="shader_fn")` is called
Then only entities with kind `"shader_fn"` are returned
And results are ranked by relevance to "shadow"

#### Scenario: Tag filter
Given the full doc index is loaded
When `search_docs(query="lighting", tags=["reference"])` is called
Then only entities with the `"reference"` tag are returned

### Requirement: get_entity MCP Tool
The MCP server SHALL expose a `get_entity` tool that accepts an entity `id` (string, format: `file_path::entity_name`) and returns the complete `DocumentedEntity` including all sections, params, examples, refs, and relationships.

#### Scenario: Entity found
Given a valid entity ID `"runtime/engine/app/time_manager.h::TimeManager"`
When `get_entity` is called with that ID
Then the full entity is returned with all documentation sections

#### Scenario: Entity not found
Given an invalid entity ID `"nonexistent.h::Foo"`
When `get_entity` is called with that ID
Then an error response is returned

### Requirement: get_relationships MCP Tool
The MCP server SHALL expose a `get_relationships` tool that accepts an entity `id`, `depth` (1-3, default 1), and optional `kind` filter. The tool MUST traverse the relationship graph to the specified depth and return all connected entities.

#### Scenario: Depth-1 traversal
Given entity A has relationships to B and C, and B has a relationship to D
When `get_relationships(id=A, depth=1)` is called
Then B and C are returned, but D is not

#### Scenario: Depth-2 traversal
Given entity A has relationships to B and C, and B has a relationship to D
When `get_relationships(id=A, depth=2)` is called
Then B, C, and D are all returned

### Requirement: get_data_flow MCP Tool
The MCP server SHALL expose a `get_data_flow` tool that accepts a `field` name (string) and traces it across the component→renderer→CB→shader pipeline. The tool MUST return an ordered array of pipeline stages, each with: entity ID, file, line, role (component/renderer/cbuffer/shader), and the field name at that stage.

#### Scenario: Full pipeline trace
Given a field `fogDensity` existing in a component, CB struct, and HLSL cbuffer
When `get_data_flow("fogDensity")` is called
Then at least 2 pipeline stages are returned in dependency order (component before shader)

### Requirement: get_subsystem_map MCP Tool
The MCP server SHALL expose a `get_subsystem_map` tool with no parameters that returns all subsystems with their entity counts, sorted by count descending.

#### Scenario: Subsystem listing
Given the doc index is loaded with entities across multiple subsystems
When `get_subsystem_map()` is called
Then an array of `{subsystem, entityCount}` objects is returned sorted descending by entityCount

### Requirement: get_refs MCP Tool
The MCP server SHALL expose a `get_refs` tool that lists all `@ref` academic citations extracted from the codebase, optionally filtered by `author` and/or `year`. Each result MUST include: author, year, title, note, and the entity IDs that cite it.

#### Scenario: Filter by author
Given multiple `@ref` citations including Bruneton and Schneider
When `get_refs(author="Bruneton")` is called
Then only citations with author containing "Bruneton" are returned

### Requirement: Performance
The complete `generate-docs.ts` run SHALL complete in under 5 seconds on the full codebase. MCP server startup (loading docs.json + deserializing search-index.bin + building BM25 index) SHALL complete in under 200 milliseconds. Any `search_docs` query SHALL return results in under 20 milliseconds.

#### Scenario: Generation performance
Given the full UzEngine codebase (~300 source files)
When `generate-docs.ts` is run
Then it completes in under 5 seconds

#### Scenario: Search latency
Given a loaded search index
When `search_docs("atmosphere rendering")` is called
Then the response is returned in under 20 milliseconds
