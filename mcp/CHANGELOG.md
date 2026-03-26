# Changelog — mcp

## [0.3.0] — 2026-03-25

### Added
- Auto-documentation generator (`tools/generate-docs.ts`) — parses `///` doc comments from C++, HLSL, and TypeScript into structured `docs.json`
- Hybrid search engine (BM25 + HNSW + Reciprocal Rank Fusion) — pure TypeScript, zero external API calls
- TF-IDF embeddings with FNV1a hashing (256-dim vectors) for semantic search
- 6 MCP tools: `search_docs`, `get_entity`, `get_relationships`, `get_data_flow`, `get_subsystem_map`, `get_refs`
- C++ parser: classes, structs, enums, functions, methods, fields, macros, `@ref` citations, `@see` cross-refs, `///<` trailing comments
- HLSL parser: shader functions, cbuffers, structs, `[numthreads]`, `#include` extraction
- TypeScript parser: classes, interfaces, functions, types, constants
- Relationship resolver: inheritance, CB↔shader binding, composition, pass↔shader heuristic
- Auto-tagging (17 rules) and subsystem mapping (60+ path prefixes)
- Incremental generation (`--incremental`) with file mtime tracking
- Watch mode (`--watch`) via chokidar
- 68 unit + integration tests

### Stats
- 9,312 entities extracted from 839 source files in 0.56s
- 1,137 entities with documentation sections
- 452 entities with academic references
- 297 entities with cross-reference relationships

### Dependencies
- Updated all packages to latest: vitest 4.1.1, @types/node 25.5.0, tsx 4.21.0
- Added: glob 13.0.6, chokidar 5.0.0, tsx 4.21.0

## [0.2.0] — 2026-03-10

### Added
- MCP server skeleton for AI workflow integration
- TypeScript build with `tsc`
- ESM module (`"type": "module"`)
- ESLint 9 flat config with typescript-eslint 8 (errors only, no warnings)
- Prettier 3.8 config
- Vitest 4 config with 95% coverage threshold
- pino structured logging
