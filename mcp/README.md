# @uzmigames/uzengine-mcp

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![TypeScript](https://img.shields.io/badge/TypeScript-5.9-blue.svg)]()
[![MCP](https://img.shields.io/badge/MCP-1.27-green.svg)]()

UzEngine MCP server — enables AI agents (Claude Desktop, etc.) to interact with the engine via the Model Context Protocol. Includes a hybrid BM25 + HNSW documentation search engine over the entire codebase.

## Build

```bash
npm install
npm run build
```

## Run

```bash
# stdio transport (for AI clients)
npm start

# Development (watch mode)
npm run dev
```

## Documentation Search

### Generate the index

```bash
# Full generation — parses all C++/HLSL/TS source files
npm run generate-docs

# Incremental — only re-parses changed files
npm run generate-docs -- --incremental

# Watch mode — regenerates on file save
npm run generate-docs:watch
```

This produces `data/docs.json` (~4.7 MB, 9,300+ entities) from `///` doc comments across the codebase.

### MCP Tools

| Tool | Description |
|------|-------------|
| `search_docs` | Hybrid BM25 + HNSW semantic search with kind/subsystem/tag filters |
| `get_entity` | Fetch full documentation for an entity by ID |
| `get_relationships` | Graph traversal — inheritance, CB↔shader binding, composition |
| `get_data_flow` | Trace a field from component → renderer → cbuffer → shader |
| `get_subsystem_map` | List all subsystems with entity counts |
| `get_refs` | List academic paper citations (`@ref`) from the codebase |

### Search Architecture

```
Query → Tokenize → ┬─ BM25 (keyword, top-50)  ─┬─ RRF Fusion → Post-filter → Results
                    └─ HNSW (semantic, top-50) ─┘
```

- **BM25**: In-memory inverted index, k1=1.2, b=0.75, field-weighted (name 3×, brief 2×)
- **HNSW**: 256-dim TF-IDF vectors via FNV1a hashing, M=16, cosine distance
- **RRF**: Reciprocal Rank Fusion (k=60) combines both ranked lists
- **Zero API calls**: All embeddings computed locally, no external services

## Tests

```bash
npm test              # Run all tests (68 tests)
npm run test:watch    # Watch mode
npm run test:coverage # With coverage report
```

## Project Structure

```
mcp/
├── src/
│   ├── server.ts              — MCP server + 6 tool registrations
│   ├── logger.ts              — Pino structured logging
│   └── search/
│       ├── tokenizer.ts       — CamelCase/snake_case split, stop words
│       ├── embeddings.ts      — TF-IDF + FNV1a 256-dim vectors
│       ├── bm25.ts            — BM25 inverted index + scoring
│       ├── hnsw.ts            — HNSW approximate nearest neighbor (pure TS)
│       ├── fusion.ts          — Reciprocal Rank Fusion
│       └── search-engine.ts   — Orchestrator combining all layers
├── tools/
│   ├── generate-docs.ts       — Main generator entry point
│   ├── types.ts               — Shared TypeScript interfaces
│   ├── parsers/
│   │   ├── cpp-parser.ts      — C++ header/source parser
│   │   ├── hlsl-parser.ts     — HLSL/HLSLI shader parser
│   │   └── ts-parser.ts       — TypeScript parser
│   └── resolvers/
│       ├── relationship-resolver.ts  — Cross-reference resolution
│       ├── tag-generator.ts          — Auto-tag rules (17 tags)
│       └── subsystem-mapper.ts       — Path → subsystem (60+ mappings)
├── data/                      — Generated (gitignored)
│   ├── docs.json              — Entity database
│   └── .docs-meta.json        — File mtimes for incremental mode
├── tests/                     — 68 tests across 7 files
├── package.json
└── tsconfig.json
```

## License

[Apache License 2.0](LICENSE) — Copyright 2026 Uzmi Games
