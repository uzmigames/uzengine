## Phase 1: Scaffold + Types (`mcp/tools/`)

- [x] 1.1 Create `mcp/tools/types.ts` — DocumentedEntity, ParamDoc, RefDoc, Relationship, SearchResult, DocsDatabase, DocsMeta interfaces.
- [x] 1.2 Create `mcp/tools/resolvers/subsystem-mapper.ts` — 60+ path prefix → subsystem mappings with longest-match lookup.
- [x] 1.3 Create `mcp/tools/resolvers/tag-generator.ts` — 17 auto-tag rules (component, pass, rhi, shader, cbuffer, subsystem, public-api, internal, rendering, editor, math, platform, reference, has-examples, thread-safety-documented, enum, macro).
- [x] 1.4 Update `mcp/package.json` — added generate-docs script, added glob, chokidar, tsx dev deps. Updated all deps to latest.
- [x] 1.5 Update `mcp/tsconfig.json` — rootDir changed to "." to include tools/, include pattern expanded.
- [x] 1.6 Add `mcp/data/` to `.gitignore`.

## Phase 2: Text Processing (`mcp/src/search/`)

- [x] 2.1 Create `mcp/src/search/tokenizer.ts` — tokenize(), splitIdentifier(), removeStopWords(), tokenizeFull(), isStopWord(). 20 unit tests passing.
- [x] 2.2 Unit tests in `mcp/tests/search/tokenizer.test.ts` — CamelCase, PascalCase, snake_case, dots/colons, stop words, empty strings.

## Phase 3: C++ Parser (`mcp/tools/parsers/`)

- [x] 3.1 Create `mcp/tools/parsers/cpp-parser.ts` — parses /// and ///< blocks from .h/.cpp files.
- [x] 3.2 Extracts: class, struct, enum, enum class, functions, methods, fields, #define UZ_* macros, typedef/using aliases.
- [x] 3.3 Parses sections: # Arguments, # Returns, # Example (code blocks), # Thread Safety, # Design, # Behavior, # Ownership.
- [x] 3.4 Parses @ref with quoted and unquoted formats.
- [x] 3.5 Parses @see cross-references.
- [x] 3.6 Extracts default values from field declarations.
- [x] 3.7 Extracts C++ type info for fields and function return types.
- [x] 3.8 Handles file-level doc blocks (before any declaration).
- [x] 3.9 Unit test — time_manager.h: class, methods, sections, inheritance all verified.
- [x] 3.10 Unit test — sky_atmosphere_component.h: struct, @ref Bruneton, field defaults, ///< comments.

## Phase 4: HLSL Parser (`mcp/tools/parsers/`)

- [x] 4.1 Create `mcp/tools/parsers/hlsl-parser.ts` — reuses doc block parsing, adds HLSL-specific extraction.
- [x] 4.2 Extracts cbuffer blocks as shader_cb kind with name and register.
- [x] 4.3 Extracts HLSL functions as shader_fn with params, semantics, numthreads.
- [x] 4.4 Extracts #include directives via extractHlslIncludes().
- [x] 4.5 Unit test — synthetic + real pbr_common.hlsli: D_GGX, F_Schlick, Fd_Burley verified. 7 tests.

## Phase 5: TypeScript Parser (`mcp/tools/parsers/`)

- [x] 5.1 Create `mcp/tools/parsers/ts-parser.ts` — parses classes, interfaces, functions, type aliases, const from .ts files.
- [x] 5.2 Unit test — synthetic + real mcp/src/server.ts. 5 tests passing.

## Phase 6: Relationship Resolver (`mcp/tools/resolvers/`)

- [x] 6.1 Create `mcp/tools/resolvers/relationship-resolver.ts` — post-parse cross-reference resolution.
- [x] 6.2 Resolves inheritance targets to entity IDs.
- [x] 6.3 Resolves composition (field type → entity).
- [x] 6.4 Resolves @see to entity IDs.
- [x] 6.5 Include graph resolution available via extractIncludes().
- [x] 6.6 Resolves CB↔shader binding by matching struct/cbuffer names (bidirectional).
- [x] 6.7 Component→renderer heuristic (lightweight, @see-based).
- [x] 6.8 Pass↔shader heuristic by shared filename prefix.
- [x] 6.9 Unit test — @see, CB binding, inheritance resolution, deduplication. 4 tests.

## Phase 7: Generator Entry Point (`mcp/tools/`)

- [x] 7.1 Create `mcp/tools/generate-docs.ts` — globs 834 source files, parses with appropriate parser, applies tags + subsystem + relationships, writes docs.json. 9055 entities in 0.53s.
- [x] 7.2 Implemented --incremental with file mtime tracking in .docs-meta.json.
- [x] 7.3 Implemented --watch with chokidar file watcher.
- [x] 7.4 Verified: docs.json valid JSON, 9055 entities, 4.5 MB, all entity kinds represented.

## Phase 8: Embeddings + Search Index (`mcp/src/search/`)

- [x] 8.1 Create `mcp/src/search/embeddings.ts` — fnv1a 32-bit hash, embed() with 256-dim TF-IDF, signed hashing, L2 normalization.
- [x] 8.2 entityToTokens() with weighted fields: name 3×, brief 2×, refs 1.5×, tags 1.5×, description 1×.
- [x] 8.3 Create `mcp/src/search/hnsw.ts` — pure TypeScript HNSW: insert, search, cosine distance. M=16, adaptive efConstruction (50-200 based on corpus size).
- [x] 8.4 HNSW serialize/deserialize with binary format (UZDX magic, version, embeddings + graph sections).
- [x] 8.5 HNSW builder integrated into search-engine.ts load().
- [x] 8.6 BM25 term frequency data built in-memory at load time.
- [x] 8.7 Unit tests: fnv1a consistency, embed dimension=256, L2 norm≈1.0, similar vectors > dissimilar.
- [x] 8.8 HNSW tested via search-engine integration tests.

## Phase 9: BM25 + Fusion (`mcp/src/search/`)

- [x] 9.1 Create `mcp/src/search/bm25.ts` — inverted index + BM25 scoring (k1=1.2, b=0.75).
- [x] 9.2 Create `mcp/src/search/fusion.ts` — Reciprocal Rank Fusion (k=60).
- [x] 9.3 Index loading integrated into search-engine.ts via loadSearchIndex() in server.ts.
- [x] 9.4 Create `mcp/src/search/search-engine.ts` — orchestrator: tokenize → BM25 top-50 → HNSW top-50 → RRF → post-filter → top-N.
- [x] 9.5 Unit test BM25: "fog density" → doc 0 ranked #1.
- [x] 9.6 Unit test RRF: dual-list boost verified, correct score computation.
- [x] 9.7 Unit test search engine: hybrid search, kind filter, getEntity, getSubsystemMap, getRefs. 13 tests.

## Phase 10: MCP Tool Registration (`mcp/src/server.ts`)

- [x] 10.1 Registered search_docs — hybrid BM25+HNSW with kind/subsystem/tags/limit filters.
- [x] 10.2 Registered get_entity — full entity by ID with all sections.
- [x] 10.3 Registered get_relationships — graph traversal with depth and kind filter.
- [x] 10.4 Registered get_data_flow — traces field across component→CB→shader pipeline.
- [x] 10.5 Registered get_subsystem_map — all subsystems with counts, sorted descending.
- [x] 10.6 Registered get_refs — academic citations filtered by author/year.
- [x] 10.7 loadSearchIndex() at server startup with timing log.

## Phase 11: Integration Testing

- [x] 11.1 search_docs("TimeManager", kind=class) → TimeManager is #1.
- [x] 11.2 search_docs("fog density") → fog-related entities in results.
- [x] 11.3 search_docs("Bruneton atmosphere") → atmosphere entities in top 5.
- [x] 11.4 search_docs("GGX", kind=shader_fn) → D_GGX in results.
- [x] 11.5 get_entity("runtime/engine/app/time_manager.h::TimeManager") → full entity with Thread Safety section.
- [x] 11.6 get_relationships — tested via unit tests (CB binding, @see, inheritance).
- [x] 11.7 get_data_flow — tested via unit tests (traceDataFlow).
- [x] 11.8 get_subsystem_map → renderer has >10 entities.
- [x] 11.9 get_refs(author="Bruneton") → atmosphere papers listed.
- [x] 11.10 search_docs latency < 50ms (measured 5-8ms per query).
- [x] 11.11 generate-docs.ts completes in 0.53s (target < 5s).
- [ ] 11.12 MCP server startup index load ~5s for 9K entities (HNSW build dominates). Future: pre-built search-index.bin.

## Phase 12: Documentation

- [x] 12.1 Planning docs: docs/mcp/auto-doc-generator.md, docs/mcp/search-architecture.md.
- [x] 12.2 Updated docs/mcp/README.md with both planning docs.
- [ ] 12.3 Update mcp/README.md — add generate-docs usage and search tool descriptions.
