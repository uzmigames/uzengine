## Phase 1: Scaffold + Types (`mcp/tools/`)

- [ ] 1.1 Create `mcp/tools/types.ts` — `DocumentedEntity`, `ParamDoc`, `RefDoc`, `Relationship`, `SearchResult` interfaces matching docs/mcp/auto-doc-generator.md schema.
- [ ] 1.2 Create `mcp/tools/resolvers/subsystem-mapper.ts` — path→subsystem mapping (20+ subsystems from directory prefixes).
- [ ] 1.3 Create `mcp/tools/resolvers/tag-generator.ts` — auto-tag rules (component, pass, rhi, shader, cbuffer, subsystem, public-api, internal, rendering, editor, reference, unsafe).
- [ ] 1.4 Update `mcp/package.json` — add `generate-docs` script, add dev deps (`glob`, `chokidar` for watch mode).
- [ ] 1.5 Update `mcp/tsconfig.json` — include `tools/` directory in compilation.
- [ ] 1.6 Add `mcp/data/` to `.gitignore` — docs.json, search-index.bin, .docs-meta.json are generated artifacts.

## Phase 2: Text Processing (`mcp/src/search/`)

- [ ] 2.1 Create `mcp/src/search/tokenizer.ts` — `tokenize()` (lowercase + split on non-alphanumeric), `splitIdentifier()` (CamelCase/PascalCase/snake_case split), `STOP_WORDS` set.
- [ ] 2.2 Unit test tokenizer — verify CamelCase split (`TimeManager` → `["time", "manager"]`), snake_case split (`time_manager` → `["time", "manager"]`), stop word removal, underscore preservation in identifiers.

## Phase 3: C++ Parser (`mcp/tools/parsers/`)

- [ ] 3.1 Create `mcp/tools/parsers/cpp-parser.ts` — parse `///` and `///<` comment blocks from `.h` and `.cpp` files.
- [ ] 3.2 Extract entity declarations: `class`, `struct`, `enum`, `enum class`, free functions, methods (inside class/struct scope), fields, `#define UZ_*` macros, `typedef`/`using` aliases.
- [ ] 3.3 Parse doc comment sections: split on `/// # ` headers, extract `# Arguments` (bullet list of `* \`param\` - description`), `# Returns`, `# Example` (code blocks), `# Thread Safety`, `# Design`, `# Behavior`, `# Ownership`, `# Side Effects`.
- [ ] 3.4 Parse `@ref Author Year "Title" — note` annotations into `RefDoc` objects.
- [ ] 3.5 Parse `@see Entity1, Entity2` annotations into `seeAlso` arrays.
- [ ] 3.6 Extract default values from field declarations (`float fogDensity = 0.02f;` → defaultValue: `"0.02f"`).
- [ ] 3.7 Extract C++ type information for fields and function parameters/return types.
- [ ] 3.8 Handle file-level doc blocks (/// comments before any declaration → kind: `"file"`).
- [ ] 3.9 Unit test C++ parser — parse `runtime/engine/app/time_manager.h` and verify: class `TimeManager` extracted with brief, `BeginFrame()` method with `# Behavior` section, `ConsumeFixedTicks()` with `# Returns`, `SetFixedDeltaTime()` with `# Arguments`.
- [ ] 3.10 Unit test C++ parser — parse `runtime/engine/ecs/environment/sky_atmosphere_component.h` and verify: struct with `@ref Bruneton 2008` citations, field default values, `///<` inline comments.

## Phase 4: HLSL Parser (`mcp/tools/parsers/`)

- [ ] 4.1 Create `mcp/tools/parsers/hlsl-parser.ts` — reuse doc comment parsing from cpp-parser, add HLSL-specific entity extraction.
- [ ] 4.2 Extract `cbuffer` blocks as kind `"shader_cb"` — name, fields with types and semantics.
- [ ] 4.3 Extract HLSL function definitions as kind `"shader_fn"` — name, params with semantics (`SV_Target`, `SV_Position`, `SV_DispatchThreadID`), `[numthreads(X,Y,Z)]` attributes.
- [ ] 4.4 Extract `#include "..."` directives for include-graph relationships.
- [ ] 4.5 Unit test HLSL parser — parse `runtime/shaders/runtime/include/pbr_common.hlsli` and verify shader functions extracted with doc comments.

## Phase 5: TypeScript Parser (`mcp/tools/parsers/`)

- [ ] 5.1 Create `mcp/tools/parsers/ts-parser.ts` — parse `///` doc blocks from `.ts` files, extract classes, interfaces, functions, type aliases.
- [ ] 5.2 Unit test TS parser — parse `mcp/src/server.ts` and verify entities extracted.

## Phase 6: Relationship Resolver (`mcp/tools/resolvers/`)

- [ ] 6.1 Create `mcp/tools/resolvers/relationship-resolver.ts` — post-parse pass that takes all entities and resolves cross-references.
- [ ] 6.2 Resolve inheritance: parse `: public Base` / `extends Base` → `inherits` relationship.
- [ ] 6.3 Resolve composition: fields whose type matches another documented entity name → `contains` relationship.
- [ ] 6.4 Resolve `@see` links: match names to entity IDs → `references` relationship.
- [ ] 6.5 Resolve include graph: `#include "foo.h"` → file-level `uses` relationship.
- [ ] 6.6 Resolve CB↔shader binding: match C++ `struct XCB` with HLSL `cbuffer XCB` by name → `binds_to` relationship.
- [ ] 6.7 Resolve component↔renderer: component types referenced in renderer `.cpp` files → `reads_from` relationship.
- [ ] 6.8 Heuristic: `*_pass.h` + `*_ps.hlsl` sharing prefix → pass `uses` shader.
- [ ] 6.9 Unit test relationships — verify `PerFrameCB` struct in C++ links to `cbuffer PerFrameCB` in HLSL.

## Phase 7: Generator Entry Point (`mcp/tools/`)

- [ ] 7.1 Create `mcp/tools/generate-docs.ts` — main entry point that: globs source files, runs appropriate parser per extension, runs tag-generator + subsystem-mapper on each entity, runs relationship-resolver, writes `docs.json`.
- [ ] 7.2 Implement `--incremental` flag: store file mtimes in `mcp/data/.docs-meta.json`, only re-parse changed files, merge into existing docs.json.
- [ ] 7.3 Implement `--watch` flag: use chokidar to watch source directories, re-generate on file change.
- [ ] 7.4 Integration test — run `generate-docs.ts` on full codebase, verify docs.json is valid JSON, entity count > 500, all entity kinds represented, no empty briefs on documented entities.

## Phase 8: Embeddings + Search Index (`mcp/tools/indexer/` + `mcp/src/search/`)

- [ ] 8.1 Create `mcp/src/search/embeddings.ts` — `fnv1a()` 32-bit hash, `embed()` function producing 256-dim Float32Array via TF-IDF weighted hash buckets with signed direction, L2 normalization.
- [ ] 8.2 Create `mcp/tools/indexer/embedding-builder.ts` — compute embeddings for all entities using weighted fields (name 3×, brief 2×, refs 1.5×, tags 1.5×, description 1×).
- [ ] 8.3 Create `mcp/src/search/hnsw.ts` — pure TypeScript HNSW implementation: insert, search (approximate k-NN), cosine distance. Parameters: M=16, efConstruction=200, efSearch=50, dim=256.
- [ ] 8.4 Implement HNSW serialization/deserialization to binary format (`search-index.bin`): header (magic "UZDX", version, counts, params), embeddings section (Float32Array), graph section (neighbor lists per level), IDF table section.
- [ ] 8.5 Create `mcp/tools/indexer/hnsw-builder.ts` — build HNSW index from embeddings, serialize to `search-index.bin`.
- [ ] 8.6 Create `mcp/tools/indexer/bm25-builder.ts` — compute document term frequencies and IDF values, store in docs.json alongside entities.
- [ ] 8.7 Unit test embeddings — verify `fnv1a("test")` produces consistent hash, embedding dimension is 256, L2 norm ≈ 1.0, similar inputs produce similar vectors (cosine > 0.5).
- [ ] 8.8 Unit test HNSW — insert 100 random vectors, search for nearest neighbor, verify ground-truth recall > 90%.

## Phase 9: BM25 + Fusion (`mcp/src/search/`)

- [ ] 9.1 Create `mcp/src/search/bm25.ts` — in-memory inverted index build from docs.json, BM25 scoring with k1=1.2, b=0.75, field-weighted document construction.
- [ ] 9.2 Create `mcp/src/search/fusion.ts` — Reciprocal Rank Fusion with k=60, combines two ranked lists into final ranking.
- [ ] 9.3 Create `mcp/src/search/index-loader.ts` — load `docs.json` + deserialize `search-index.bin`, build BM25 inverted index, return ready search state.
- [ ] 9.4 Create `mcp/src/search/search-engine.ts` — orchestrator: tokenize query → BM25 top-50 → embed query → HNSW top-50 → RRF fusion → post-filter (kind, subsystem, tags) → return top-N.
- [ ] 9.5 Unit test BM25 — index 10 sample entities, search "fog density", verify `HeightFogComponent` ranked #1.
- [ ] 9.6 Unit test fusion — two ranked lists with overlap, verify RRF output boosts items appearing in both.
- [ ] 9.7 Unit test search engine — end-to-end search "Bruneton atmosphere", verify `SkyAtmosphereComponent` in top 3.

## Phase 10: MCP Tool Registration (`mcp/src/server.ts`)

- [ ] 10.1 Register `search_docs` tool — query + optional kind/subsystem/tags/limit filters. Returns ranked results with id, name, kind, file, line, brief, subsystem, tags, relevance.
- [ ] 10.2 Register `get_entity` tool — fetch full entity by ID (file_path::entity_name). Returns complete entity with all sections, params, examples, refs, relationships.
- [ ] 10.3 Register `get_relationships` tool — graph traversal from entity ID, configurable depth (1-3) and relationship kind filter.
- [ ] 10.4 Register `get_data_flow` tool — trace a field name from component → CB → shader, return ordered pipeline stages.
- [ ] 10.5 Register `get_subsystem_map` tool — list all subsystems with entity counts, sorted descending.
- [ ] 10.6 Register `get_refs` tool — list all `@ref` academic citations, filterable by author/year.
- [ ] 10.7 Load search index at server startup — call `index-loader.ts` in `main()`, log entity count and load time.

## Phase 11: Integration Testing

- [ ] 11.1 End-to-end: run `generate-docs.ts` → start MCP server → call `search_docs("time manager")` → verify `TimeManager` in top 3.
- [ ] 11.2 End-to-end: `search_docs("fog density")` → verify `HeightFogComponent.fogDensity` and `height_fog_ps.hlsl` in results.
- [ ] 11.3 End-to-end: `search_docs("Bruneton atmosphere")` → verify `SkyAtmosphereComponent` and `transmittance_lut_cs.hlsl` in results.
- [ ] 11.4 End-to-end: `search_docs("GGX", kind="shader_fn")` → verify `D_GGX()` from `pbr_common.hlsli` in results.
- [ ] 11.5 End-to-end: `get_entity("runtime/engine/app/time_manager.h::TimeManager")` → verify full entity with sections and methods.
- [ ] 11.6 End-to-end: `get_relationships("PerFrameCB", depth=1)` → verify `binds_to` link to HLSL cbuffer.
- [ ] 11.7 End-to-end: `get_data_flow("fogDensity")` → verify chain from component → renderer → CB → shader.
- [ ] 11.8 End-to-end: `get_subsystem_map()` → verify renderer subsystem has highest entity count.
- [ ] 11.9 End-to-end: `get_refs(author="Bruneton")` → verify atmosphere papers listed.
- [ ] 11.10 Performance: `search_docs` latency < 20ms for any query on full index.
- [ ] 11.11 Performance: `generate-docs.ts` completes in < 5s on full codebase.
- [ ] 11.12 Performance: MCP server startup (index load) < 200ms.

## Phase 12: Documentation

- [ ] 12.1 Update `docs/specs/3.0-mcp-integration.md` — add section 3.1 documenting the 6 new MCP tools.
- [ ] 12.2 Update `mcp/README.md` — add generate-docs usage, search tool descriptions, architecture overview.
- [ ] 12.3 Update `docs/mcp/README.md` — mark planning docs as implemented, add link to generated output.
