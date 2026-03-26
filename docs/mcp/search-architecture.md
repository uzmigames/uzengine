<!-- doc-meta
title: MCP Search Architecture — Hybrid BM25 + HNSW
category: mcp
theme: search
created: 2026-03-25
updated: 2026-03-25
status: planning
author: UzEngine Team
-->

# MCP Search Architecture — Hybrid BM25 + HNSW

> **Goal**: Hybrid search over `docs.json` combining keyword scoring (BM25) with semantic similarity (HNSW vector index), fused via Reciprocal Rank Fusion. Pure TypeScript, zero external API calls.

---

## Stack Overview

| Layer | Technology | Purpose |
|-------|-----------|---------|
| Keyword Search | BM25 via in-memory inverted index | Fast exact-match and term scoring |
| Vector Search | HNSW index | Semantic similarity matching |
| Embeddings | TF-IDF + FNV1a hashing | 256-dim vectors, pure TypeScript, no API calls |
| Ranking | Reciprocal Rank Fusion (RRF) | Combines BM25 + vector scores into final ranking |
| Storage | `docs.json` + `search-index.bin` | Precomputed at generation time |

---

## Pipeline

```
                    ┌──────────────┐
                    │  User Query  │
                    └──────┬───────┘
                           │
                    ┌──────▼───────┐
                    │  Tokenize +  │
                    │  Normalize   │
                    └──────┬───────┘
                           │
              ┌────────────┼────────────┐
              │                         │
       ┌──────▼──────┐          ┌──────▼──────┐
       │  BM25 Search │          │ HNSW Search │
       │  (keywords)  │          │  (semantic)  │
       └──────┬──────┘          └──────┬──────┘
              │                         │
              │  top-K results          │  top-K results
              │  + BM25 scores          │  + cosine scores
              │                         │
              └────────────┬────────────┘
                           │
                    ┌──────▼───────┐
                    │  Reciprocal  │
                    │  Rank Fusion │
                    └──────┬───────┘
                           │
                    ┌──────▼───────┐
                    │ Post-Filter  │
                    │ (kind, tags, │
                    │  subsystem)  │
                    └──────┬───────┘
                           │
                    ┌──────▼───────┐
                    │   Results    │
                    └──────────────┘
```

---

## Layer 1 — Text Preprocessing

### Tokenization

```typescript
function tokenize(text: string): string[] {
  return text
    .toLowerCase()
    .replace(/[^a-z0-9_]/g, " ")       // Keep underscores (C++ identifiers)
    .split(/\s+/)
    .filter(t => t.length > 1);
}
```

### Normalization Rules

| Input | Normalized |
|-------|-----------|
| `TimeManager` | `["time", "manager"]` (camelCase split) |
| `time_manager` | `["time", "manager"]` (snake_case split) |
| `PerFrameCB` | `["per", "frame", "cb"]` |
| `sky_atmosphere_ps.hlsl` | `["sky", "atmosphere", "ps", "hlsl"]` |
| `@ref Bruneton 2008` | `["bruneton", "2008"]` |

### CamelCase / PascalCase Splitting

```typescript
function splitIdentifier(name: string): string[] {
  return name
    .replace(/([a-z])([A-Z])/g, "$1 $2")   // camelCase → camel Case
    .replace(/([A-Z]+)([A-Z][a-z])/g, "$1 $2") // HTMLParser → HTML Parser
    .replace(/_/g, " ")                      // snake_case → snake case
    .toLowerCase()
    .split(/\s+/)
    .filter(t => t.length > 1);
}
```

### Stop Words

Minimal stop list tuned for code documentation:

```typescript
const STOP_WORDS = new Set([
  "the", "a", "an", "is", "are", "was", "were", "be", "been",
  "this", "that", "it", "its", "of", "in", "to", "for", "with",
  "on", "at", "by", "from", "as", "or", "and", "not", "no",
]);
```

---

## Layer 2 — BM25 Keyword Search

### Index Structure

In-memory inverted index built at startup from `docs.json`:

```typescript
interface BM25Index {
  /// term → list of (docIndex, termFrequency) pairs
  invertedIndex: Map<string, Array<{ doc: number; tf: number }>>;

  /// Per-document token count (for length normalization)
  docLengths: Float32Array;

  /// Average document length across corpus
  avgDocLength: number;

  /// Total number of documents
  docCount: number;
}
```

### Scoring

Standard BM25 with tuned parameters:

```
score(q, d) = Σ IDF(t) × (tf(t,d) × (k1 + 1)) / (tf(t,d) + k1 × (1 - b + b × |d| / avgdl))
```

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| `k1` | 1.2 | Standard — controls term frequency saturation |
| `b` | 0.75 | Standard — controls length normalization |

### IDF Calculation

```typescript
function idf(term: string, index: BM25Index): number {
  const df = index.invertedIndex.get(term)?.length ?? 0;
  const N = index.docCount;
  return Math.log(1 + (N - df + 0.5) / (df + 0.5));
}
```

### Document Fields (weighted)

Each entity produces a searchable document with weighted fields:

| Field | Weight | Content |
|-------|--------|---------|
| `name` | 3.0× | Entity name + split identifier tokens |
| `brief` | 2.0× | First paragraph of doc comment |
| `description` | 1.0× | Full extended description |
| `params` | 1.0× | Parameter names + descriptions |
| `refs` | 1.5× | `@ref` author + title |
| `tags` | 1.5× | Auto-generated tags |
| `subsystem` | 1.0× | Subsystem name |

Field weighting is applied by repeating tokens in the document vector proportionally.

---

## Layer 3 — TF-IDF Embeddings (256-dim, FNV1a)

### Why Not External Embeddings

- Zero latency (no API roundtrip)
- Zero cost (no token billing)
- Deterministic (same input → same vector every time)
- Works offline
- Proven in Rulebook at similar corpus scale

### Embedding Generation

Each entity is embedded as a 256-dimensional float vector using hashed TF-IDF:

```typescript
const EMBED_DIM = 256;

function embed(tokens: string[], idfMap: Map<string, number>): Float32Array {
  const vec = new Float32Array(EMBED_DIM);

  for (const token of tokens) {
    const idf = idfMap.get(token) ?? 0;
    const tf = tokens.filter(t => t === token).length / tokens.length;
    const weight = tf * idf;

    // FNV1a hash → bucket index
    const bucket = fnv1a(token) % EMBED_DIM;
    // Signed hash for direction (avoids all-positive collapse)
    const sign = (fnv1a(token + "_sign") % 2 === 0) ? 1 : -1;
    vec[bucket] += sign * weight;
  }

  // L2 normalize
  const norm = Math.sqrt(vec.reduce((sum, v) => sum + v * v, 0));
  if (norm > 0) {
    for (let i = 0; i < EMBED_DIM; i++) vec[i] /= norm;
  }

  return vec;
}
```

### FNV1a Hash (32-bit)

```typescript
function fnv1a(str: string): number {
  let hash = 0x811c9dc5;
  for (let i = 0; i < str.length; i++) {
    hash ^= str.charCodeAt(i);
    hash = (hash * 0x01000193) >>> 0;
  }
  return hash >>> 0;
}
```

### Token Sources for Embedding

Same weighted fields as BM25, concatenated:

```typescript
function entityToTokens(entity: DocumentedEntity): string[] {
  const tokens: string[] = [];

  // Name tokens (3× weight → repeat 3 times)
  const nameTokens = splitIdentifier(entity.name);
  tokens.push(...nameTokens, ...nameTokens, ...nameTokens);

  // Brief (2×)
  const briefTokens = tokenize(entity.brief);
  tokens.push(...briefTokens, ...briefTokens);

  // Description (1×)
  if (entity.description) tokens.push(...tokenize(entity.description));

  // Refs (1.5× → repeat some)
  for (const ref of entity.refs ?? []) {
    const refTokens = tokenize(`${ref.author} ${ref.title}`);
    tokens.push(...refTokens, ...refTokens.slice(0, Math.ceil(refTokens.length / 2)));
  }

  // Tags (1.5×)
  const tagTokens = entity.tags.flatMap(t => tokenize(t));
  tokens.push(...tagTokens, ...tagTokens.slice(0, Math.ceil(tagTokens.length / 2)));

  return tokens.filter(t => !STOP_WORDS.has(t));
}
```

---

## Layer 4 — HNSW Vector Index

### Why HNSW

- O(log N) approximate nearest neighbor search
- Pure TypeScript implementation possible (~300 LOC)
- No native dependencies
- Well-understood parameters

### HNSW Parameters

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| `M` | 16 | Max connections per node (standard for <10K docs) |
| `efConstruction` | 200 | Build-time search depth (higher = better recall) |
| `efSearch` | 50 | Query-time search depth (tunable at runtime) |
| `dimensions` | 256 | Matches TF-IDF embedding dimension |
| `metric` | cosine | Standard for TF-IDF vectors |

### Index Lifecycle

```
generate-docs.ts
  │
  ├── Parse all entities → docs.json
  ├── Compute TF-IDF embeddings for each entity
  ├── Build HNSW index
  └── Serialize index → search-index.bin

MCP server startup
  │
  ├── Load docs.json
  ├── Load search-index.bin (deserialize HNSW)
  ├── Build BM25 inverted index (in-memory, fast)
  └── Ready to serve queries
```

### Serialization Format (`search-index.bin`)

Binary format for fast load:

```
Header:
  magic: "UZDX" (4 bytes)
  version: uint32
  entityCount: uint32
  embeddingDim: uint32  (256)
  hnswM: uint32
  hnswLevels: uint32

Embeddings section:
  Float32Array[entityCount × embeddingDim]

HNSW graph section:
  For each level:
    For each node:
      neighborCount: uint16
      neighbors: uint32[neighborCount]

IDF table section:
  termCount: uint32
  For each term:
    termLength: uint16
    termBytes: UTF-8[termLength]
    idfValue: float32
```

---

## Layer 5 — Reciprocal Rank Fusion (RRF)

### Algorithm

Given two ranked lists from BM25 and HNSW, combine using RRF:

```
RRF_score(d) = Σ 1 / (k + rank_i(d))
```

Where:
- `rank_i(d)` = rank of document `d` in result list `i` (1-based)
- `k` = 60 (standard constant, dampens high-rank dominance)

### Implementation

```typescript
const RRF_K = 60;

function reciprocalRankFusion(
  bm25Results: Array<{ id: string; rank: number }>,
  hnswResults: Array<{ id: string; rank: number }>,
): Array<{ id: string; score: number }> {
  const scores = new Map<string, number>();

  for (const { id, rank } of bm25Results) {
    scores.set(id, (scores.get(id) ?? 0) + 1 / (RRF_K + rank));
  }

  for (const { id, rank } of hnswResults) {
    scores.set(id, (scores.get(id) ?? 0) + 1 / (RRF_K + rank));
  }

  return [...scores.entries()]
    .map(([id, score]) => ({ id, score }))
    .sort((a, b) => b.score - a.score);
}
```

### Why RRF Over Linear Combination

- No need to tune α/β weights between BM25 and cosine scores
- Rank-based → immune to score scale differences
- Proven in IR literature (Cormack et al. 2009)
- Same approach validated in Rulebook production

---

## Query Processing Pipeline

### Full Query Flow

```typescript
async function searchDocs(
  query: string,
  filters: { kind?: string; subsystem?: string; tags?: string[] },
  limit: number = 10,
): Promise<SearchResult[]> {

  // 1. Tokenize query
  const queryTokens = tokenize(query);
  const queryIdentTokens = splitIdentifier(query);
  const allTokens = [...new Set([...queryTokens, ...queryIdentTokens])];

  // 2. BM25 search (top 50 candidates)
  const bm25Hits = bm25Search(allTokens, 50);

  // 3. Embed query → 256-dim vector
  const queryVec = embed(allTokens, idfMap);

  // 4. HNSW approximate nearest neighbors (top 50)
  const hnswHits = hnswIndex.search(queryVec, 50);

  // 5. Reciprocal Rank Fusion
  let fused = reciprocalRankFusion(bm25Hits, hnswHits);

  // 6. Post-filter by kind, subsystem, tags
  if (filters.kind) {
    fused = fused.filter(r => entities[r.id].kind === filters.kind);
  }
  if (filters.subsystem) {
    fused = fused.filter(r => entities[r.id].subsystem === filters.subsystem);
  }
  if (filters.tags?.length) {
    fused = fused.filter(r =>
      filters.tags!.every(t => entities[r.id].tags.includes(t))
    );
  }

  // 7. Return top-N with full entity data
  return fused.slice(0, limit).map(r => ({
    ...entities[r.id],
    relevance: r.score,
  }));
}
```

---

## MCP Tool Definitions

### `search_docs` — Primary hybrid search

```typescript
server.tool("search_docs", {
  query:     z.string().describe("Search term or natural language query"),
  kind:      z.enum([...ENTITY_KINDS]).optional().describe("Filter by entity kind"),
  subsystem: z.string().optional().describe("Filter by subsystem"),
  tags:      z.array(z.string()).optional().describe("Filter by tags (AND)"),
  limit:     z.number().default(10).describe("Max results"),
}, async (params) => {
  return searchDocs(params.query, params, params.limit);
});
```

### `get_entity` — Fetch full entity by ID

```typescript
server.tool("get_entity", {
  id: z.string().describe("Entity ID (file_path::entity_name)"),
}, async ({ id }) => {
  return entities[id] ?? { error: "Entity not found" };
});
```

### `get_relationships` — Graph traversal

```typescript
server.tool("get_relationships", {
  id:    z.string().describe("Entity ID"),
  depth: z.number().default(1).describe("Traversal depth (1-3)"),
  kind:  z.enum([...RELATIONSHIP_KINDS]).optional().describe("Filter relationship kind"),
}, async ({ id, depth, kind }) => {
  return traverseRelationships(id, depth, kind);
});
```

### `get_data_flow` — Trace component → CB → shader

```typescript
server.tool("get_data_flow", {
  field: z.string().describe("Field name to trace (e.g. 'fogDensity')"),
}, async ({ field }) => {
  return traceDataFlow(field); // Returns ordered pipeline stages
});
```

### `get_subsystem_map` — Overview of all subsystems

```typescript
server.tool("get_subsystem_map", {}, async () => {
  return Object.entries(subsystemCounts)
    .map(([name, count]) => ({ subsystem: name, entityCount: count }))
    .sort((a, b) => b.entityCount - a.entityCount);
});
```

### `get_refs` — Academic references index

```typescript
server.tool("get_refs", {
  author: z.string().optional().describe("Filter by author name"),
  year:   z.string().optional().describe("Filter by year"),
}, async (params) => {
  return filterRefs(params);
});
```

---

## Performance Targets

| Metric | Target | Rationale |
|--------|--------|-----------|
| Index build time | < 5s | Full regeneration from ~5K doc comments |
| Index load time | < 200ms | MCP server startup (deserialize bin + build BM25) |
| Search latency | < 20ms | BM25 + HNSW + RRF for top-50 candidates |
| `docs.json` size | < 3 MB | ~5K entities with full metadata |
| `search-index.bin` size | < 5 MB | 5K × 256-dim × 4 bytes + HNSW graph |
| Memory (server) | < 50 MB | Index + entities in memory |

---

## File Structure

```
mcp/
├── src/
│   ├── server.ts                  ← Add tool registrations here
│   ├── logger.ts
│   └── search/
│       ├── bm25.ts                ← BM25 inverted index + scoring
│       ├── hnsw.ts                ← HNSW vector index (pure TS)
│       ├── embeddings.ts          ← TF-IDF + FNV1a embedding
│       ├── fusion.ts              ← Reciprocal Rank Fusion
│       ├── tokenizer.ts           ← Tokenization + normalization
│       ├── search-engine.ts       ← Orchestrator (combines all layers)
│       └── index-loader.ts        ← Load docs.json + search-index.bin
├── tools/
│   ├── generate-docs.ts           ← Parser + index builder
│   ├── parsers/
│   │   ├── cpp-parser.ts
│   │   ├── hlsl-parser.ts
│   │   └── ts-parser.ts
│   ├── resolvers/
│   │   ├── relationship-resolver.ts
│   │   ├── tag-generator.ts
│   │   └── subsystem-mapper.ts
│   ├── indexer/
│   │   ├── bm25-builder.ts        ← Build BM25 data at generation time
│   │   ├── hnsw-builder.ts        ← Build HNSW + serialize to .bin
│   │   └── embedding-builder.ts   ← Compute all entity embeddings
│   └── types.ts
├── data/
│   ├── docs.json                  ← Entity database (gitignored)
│   ├── search-index.bin           ← HNSW + embeddings (gitignored)
│   └── .docs-meta.json            ← File mtimes for incremental mode
├── package.json
└── tsconfig.json
```

---

## Implementation Sequence

| Step | What | Depends On |
|------|------|-----------|
| 1 | `tokenizer.ts` — tokenize, normalize, split identifiers, stop words | — |
| 2 | `embeddings.ts` — FNV1a hash, TF-IDF embed function | Step 1 |
| 3 | `bm25.ts` — inverted index build + BM25 scoring | Step 1 |
| 4 | `hnsw.ts` — HNSW insert, search, serialize, deserialize | Step 2 |
| 5 | `fusion.ts` — RRF combiner | — |
| 6 | `search-engine.ts` — orchestrate BM25 + HNSW + RRF + filters | Steps 3-5 |
| 7 | `generate-docs.ts` — wire parsers + index builders | Steps 2-4 + parsers |
| 8 | `server.ts` — register MCP tools, load index at startup | Steps 6-7 |
| 9 | Test with real queries against UzEngine codebase | Step 8 |

---

## Test Queries (validation)

After implementation, these queries should return correct top-3 results:

| Query | Expected Top Results |
|-------|---------------------|
| `"time manager"` | `TimeManager` class, `BeginFrame()`, `ConsumeFixedTicks()` |
| `"fog density"` | `HeightFogComponent.fogDensity`, `height_fog_ps.hlsl`, `FogCB` |
| `"Bruneton atmosphere"` | `SkyAtmosphereComponent`, `transmittance_lut_cs.hlsl`, `sky_atmosphere_ps.hlsl` |
| `"cbuffer layout"` | `PerFrameCB`, `MaterialCB`, `LightCB` |
| `"subsystem lifecycle"` | `ISubsystem`, `InitPhase`, `SubsystemRegistry` |
| `"shadow sampling"` | `shadow_common.hlsli`, `pcss.hlsli`, `ShadowCB` |
| `"HNSW"` | (no results — not in engine code, validates no false positives) |
| kind=`"shader_fn"` + `"GGX"` | `D_GGX()`, `G_Smith()`, `F_Schlick()` in `pbr_common.hlsli` |
