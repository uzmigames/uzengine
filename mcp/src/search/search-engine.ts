/// Search engine orchestrator — combines BM25 + HNSW + RRF.
///
/// Loads docs.json, builds BM25 index and HNSW index, and serves
/// hybrid search queries.

import type { DocumentedEntity, SearchFilters, SearchResult } from "../../tools/types.js";
import { splitIdentifier, tokenize, removeStopWords } from "./tokenizer.js";
import { embed, buildIdfMap, entityToTokens } from "./embeddings.js";
import { Bm25Index } from "./bm25.js";
import { HnswIndex } from "./hnsw.js";
import { reciprocalRankFusion } from "./fusion.js";

/// The loaded search state.
export class SearchEngine {
  private entities: DocumentedEntity[] = [];
  private entityById = new Map<string, DocumentedEntity>();
  private bm25 = new Bm25Index();
  private hnsw = new HnswIndex();
  private idfMap = new Map<string, number>();
  private tokenCache: string[][] = [];

  /// Load entities and build all indices.
  load(entities: DocumentedEntity[]): void {
    this.entities = entities;
    this.entityById.clear();
    for (const e of entities) {
      this.entityById.set(e.id, e);
    }

    // Tokenize all entities
    this.tokenCache = entities.map((e) => entityToTokens(e));

    // Build IDF map from corpus
    this.idfMap = buildIdfMap(this.tokenCache);

    // Build BM25 index
    this.bm25.build(this.tokenCache);

    // Build HNSW index — scale efConstruction based on corpus size
    const efC = entities.length > 5000 ? 50 : entities.length > 1000 ? 100 : 200;
    this.hnsw = new HnswIndex({ M: 16, efConstruction: efC });
    for (let i = 0; i < entities.length; i++) {
      const vec = embed(this.tokenCache[i], this.idfMap);
      this.hnsw.insert(i, vec);
    }
  }

  /// Get total entity count.
  get size(): number {
    return this.entities.length;
  }

  /// Hybrid search: BM25 + HNSW + RRF.
  search(
    query: string,
    filters?: SearchFilters,
    limit = 10,
  ): SearchResult[] {
    // Tokenize query
    const queryIdent = splitIdentifier(query);
    const queryText = tokenize(query);
    const allTokens = removeStopWords([...new Set([...queryIdent, ...queryText])]);

    if (allTokens.length === 0) return [];

    // BM25 search (top 50)
    const bm25Hits = this.bm25.search(allTokens, 50);
    const bm25Ranked = bm25Hits.map((h, i) => ({ id: h.doc, rank: i + 1 }));

    // HNSW search (top 50)
    const queryVec = embed(allTokens, this.idfMap);
    const hnswHits = this.hnsw.search(queryVec, 50);
    const hnswRanked = hnswHits.map((h, i) => ({ id: h.id, rank: i + 1 }));

    // Reciprocal Rank Fusion
    let fused = reciprocalRankFusion(bm25Ranked, hnswRanked);

    // Post-filter
    if (filters) {
      fused = fused.filter((r) => {
        const entity = this.entities[r.id];
        if (!entity) return false;
        if (filters.kind && entity.kind !== filters.kind) return false;
        if (filters.subsystem && entity.subsystem !== filters.subsystem) return false;
        if (filters.tags?.length) {
          if (!filters.tags.every((t) => entity.tags.includes(t))) return false;
        }
        return true;
      });
    }

    // Map to SearchResult
    return fused.slice(0, limit).map((r) => {
      const entity = this.entities[r.id];
      return {
        id: entity.id,
        name: entity.name,
        kind: entity.kind,
        file: entity.file,
        line: entity.line,
        brief: entity.brief,
        subsystem: entity.subsystem,
        tags: entity.tags,
        relationships: entity.relationships,
        relevance: r.score,
      };
    });
  }

  /// Get full entity by ID.
  getEntity(id: string): DocumentedEntity | undefined {
    return this.entityById.get(id);
  }

  /// Get entities related to a given entity (graph traversal).
  getRelationships(
    id: string,
    depth = 1,
    kind?: string,
  ): DocumentedEntity[] {
    const visited = new Set<string>();
    const result: DocumentedEntity[] = [];

    const traverse = (entityId: string, currentDepth: number) => {
      if (currentDepth > depth || visited.has(entityId)) return;
      visited.add(entityId);

      const entity = this.entityById.get(entityId);
      if (!entity) return;

      for (const rel of entity.relationships) {
        if (kind && rel.kind !== kind) continue;
        const target = this.entityById.get(rel.target);
        if (target && !visited.has(target.id)) {
          result.push(target);
          traverse(target.id, currentDepth + 1);
        }
      }
    };

    traverse(id, 1);
    return result;
  }

  /// Get all subsystems with entity counts.
  getSubsystemMap(): Array<{ subsystem: string; entityCount: number }> {
    const counts = new Map<string, number>();
    for (const e of this.entities) {
      counts.set(e.subsystem, (counts.get(e.subsystem) ?? 0) + 1);
    }
    return [...counts.entries()]
      .map(([subsystem, entityCount]) => ({ subsystem, entityCount }))
      .sort((a, b) => b.entityCount - a.entityCount);
  }

  /// Get all academic references, optionally filtered.
  getRefs(
    author?: string,
    year?: string,
  ): Array<{
    author: string;
    year?: string;
    title: string;
    note?: string;
    entityIds: string[];
  }> {
    const refMap = new Map<
      string,
      { author: string; year?: string; title: string; note?: string; entityIds: string[] }
    >();

    for (const entity of this.entities) {
      for (const ref of entity.refs ?? []) {
        if (author && !ref.author.toLowerCase().includes(author.toLowerCase())) continue;
        if (year && ref.year !== year) continue;

        const key = `${ref.author}|${ref.year ?? ""}|${ref.title}`;
        const existing = refMap.get(key);
        if (existing) {
          existing.entityIds.push(entity.id);
        } else {
          refMap.set(key, {
            author: ref.author,
            year: ref.year,
            title: ref.title,
            note: ref.note,
            entityIds: [entity.id],
          });
        }
      }
    }

    return [...refMap.values()];
  }

  /// Trace a field through the component→CB→shader pipeline.
  traceDataFlow(
    fieldName: string,
  ): Array<{ entityId: string; file: string; line: number; role: string; field: string }> {
    const stages: Array<{ entityId: string; file: string; line: number; role: string; field: string }> = [];

    for (const entity of this.entities) {
      if (entity.name !== fieldName) continue;

      let role: string;
      if (entity.file.includes("/ecs/") || entity.name.endsWith("Component")) {
        role = "component";
      } else if (entity.kind === "shader_cb") {
        role = "cbuffer";
      } else if (entity.language === "hlsl") {
        role = "shader";
      } else if (entity.file.includes("/renderer/")) {
        role = "renderer";
      } else {
        role = "other";
      }

      stages.push({
        entityId: entity.id,
        file: entity.file,
        line: entity.line,
        role,
        field: entity.name,
      });
    }

    // Sort by pipeline order
    const order = ["component", "renderer", "cbuffer", "shader", "other"];
    stages.sort((a, b) => order.indexOf(a.role) - order.indexOf(b.role));

    return stages;
  }
}
