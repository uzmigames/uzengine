# Proposal: MCP Auto-Documentation Generator + Hybrid Search (Phase 3.1)

## Why

LLMs working on UzEngine need to understand what each class, struct, function, and shader does — how subsystems connect, what parameters exist, and which academic papers back each rendering technique. Today this knowledge is scattered across ~5,200 `///` doc comment lines in 115+ source files. An LLM must grep and read individual files to find context, which is slow, incomplete, and burns context window tokens on irrelevant code.

The Rulebook MCP server already proves that hybrid BM25 + HNSW search with TF-IDF embeddings and Reciprocal Rank Fusion delivers fast, accurate semantic search with zero external API dependency. Applying the same proven architecture to engine documentation creates a structured, queryable knowledge base that any MCP-connected LLM can search in milliseconds.

## What Changes

### 1. Doc Comment Parser (`mcp/tools/`)
A TypeScript script that globs all `.h`, `.cpp`, `.hlsl`, `.hlsli`, `.ts` files under `runtime/`, `mcp/`, and `extension/`, parses `///` and `///<` doc blocks, and extracts structured entities (class, struct, enum, function, method, field, shader_fn, cbuffer). Each entity includes: brief/description, named sections (# Arguments, # Returns, # Example, # Thread Safety, etc.), `@ref` citations, `@see` cross-references, type info, and default values.

### 2. Relationship Resolver (`mcp/tools/resolvers/`)
Post-parse pass that resolves: inheritance (`: public Base`), composition (fields whose type is a documented entity), include graph (`#include`), cbuffer↔struct binding (matching C++ struct with HLSL cbuffer by name), component→renderer data flow, and `@see` links. Produces a traversable relationship graph.

### 3. Search Index Builder (`mcp/tools/indexer/`)
At generation time, computes TF-IDF embeddings (256-dim, FNV1a hashing, pure TypeScript) for every entity, builds an HNSW vector index (M=16, efConstruction=200), and builds BM25 term frequency data. Outputs `docs.json` (entity database) + `search-index.bin` (serialized HNSW + embeddings + IDF table).

### 4. Hybrid Search Engine (`mcp/src/search/`)
Runtime search module loaded by the MCP server at startup. Combines BM25 keyword scoring with HNSW approximate nearest neighbor search, fused via Reciprocal Rank Fusion (k=60). Supports post-filtering by entity kind, subsystem, and tags.

### 5. MCP Tool Registration (`mcp/src/server.ts`)
Six new MCP tools: `search_docs` (primary hybrid search), `get_entity` (full entity by ID), `get_relationships` (graph traversal), `get_data_flow` (trace field from component→CB→shader), `get_subsystem_map` (subsystem overview), `get_refs` (academic reference index).

### 6. Auto-tagging + Subsystem Mapping
Entities auto-tagged based on file path (ecs/ → `component`, passes/ → `pass`, rhi/ → `rhi`) and content (has `@ref` → `reference`, in `.h` → `public-api`). Subsystem derived from directory structure (20+ mapped subsystems).

## Impact

- **Affected specs**: `docs/specs/3.0-mcp-integration.md` (new tools section)
- **New files**:
  - `mcp/tools/generate-docs.ts` — Main generator entry point
  - `mcp/tools/types.ts` — Shared TypeScript interfaces
  - `mcp/tools/parsers/cpp-parser.ts` — C++ doc comment parser
  - `mcp/tools/parsers/hlsl-parser.ts` — HLSL/HLSLI shader parser
  - `mcp/tools/parsers/ts-parser.ts` — TypeScript parser
  - `mcp/tools/resolvers/relationship-resolver.ts` — Cross-reference resolution
  - `mcp/tools/resolvers/tag-generator.ts` — Auto-tag generation
  - `mcp/tools/resolvers/subsystem-mapper.ts` — Path→subsystem mapping
  - `mcp/tools/indexer/embedding-builder.ts` — TF-IDF embedding computation
  - `mcp/tools/indexer/hnsw-builder.ts` — HNSW index construction + serialization
  - `mcp/tools/indexer/bm25-builder.ts` — BM25 inverted index data
  - `mcp/src/search/tokenizer.ts` — Tokenization + normalization
  - `mcp/src/search/embeddings.ts` — TF-IDF + FNV1a embedding
  - `mcp/src/search/bm25.ts` — BM25 inverted index + scoring
  - `mcp/src/search/hnsw.ts` — HNSW vector index (pure TypeScript)
  - `mcp/src/search/fusion.ts` — Reciprocal Rank Fusion
  - `mcp/src/search/search-engine.ts` — Orchestrator
  - `mcp/src/search/index-loader.ts` — Load docs.json + search-index.bin
- **Modified files**:
  - `mcp/src/server.ts` — Register 6 new tools
  - `mcp/package.json` — Add build scripts, dev dependencies
  - `mcp/tsconfig.json` — Include tools/ in compilation
- **Generated files** (gitignored):
  - `mcp/data/docs.json` — Entity database
  - `mcp/data/search-index.bin` — HNSW + embeddings
  - `mcp/data/.docs-meta.json` — File mtimes for incremental mode
- **No breaking changes**: Additive only, existing MCP server functionality unchanged
- **Planning docs**: `docs/mcp/auto-doc-generator.md`, `docs/mcp/search-architecture.md`
