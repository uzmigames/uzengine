/// HNSW (Hierarchical Navigable Small World) vector index.
///
/// Pure TypeScript implementation for approximate nearest neighbor search.
/// Uses cosine distance (1 - cosine_similarity) as the distance metric.

import { EMBED_DIM } from './embeddings.js';

/// Distance function: cosine distance = 1 - dot(a, b).
/// Assumes both vectors are L2-normalized.
function cosineDistance(a: Float32Array, b: Float32Array): number {
  let dot = 0;
  for (let i = 0; i < a.length; i++) dot += a[i] * b[i];
  return 1 - dot;
}

interface HnswNode {
  id: number;
  vector: Float32Array;
  /// Neighbors at each level.
  neighbors: number[][];
}

/// A node ID paired with its distance to the current query.
interface Candidate {
  id: number;
  dist: number;
}

/// Binary heap of candidates. `outranks(a, b)` is true when `a` belongs
/// above `b`, so `a.dist < b.dist` yields a min-heap and `>` a max-heap.
/// @ref Malkov & Yashunin 2018 "Efficient and robust approximate nearest
/// neighbor search using Hierarchical Navigable Small World graphs" (Alg. 2
/// keeps candidates and results in priority queues: O(log ef) per update).
class CandidateHeap {
  private readonly items: Candidate[] = [];

  constructor(private readonly outranks: (a: Candidate, b: Candidate) => boolean) {}

  get size(): number {
    return this.items.length;
  }

  /// Top element; the heap must be non-empty.
  peek(): Candidate {
    return this.items[0];
  }

  push(item: Candidate): void {
    const items = this.items;
    let i = items.length;
    items.push(item);
    while (i > 0) {
      const parent = (i - 1) >> 1;
      if (!this.outranks(item, items[parent])) break;
      items[i] = items[parent];
      i = parent;
    }
    items[i] = item;
  }

  /// Removes and returns the top element; the heap must be non-empty.
  pop(): Candidate {
    const items = this.items;
    const top = items[0];
    const last = items.pop()!;
    if (items.length === 0) return top;

    let i = 0;
    for (;;) {
      const left = 2 * i + 1;
      if (left >= items.length) break;
      const right = left + 1;
      const child = right < items.length && this.outranks(items[right], items[left]) ? right : left;
      if (!this.outranks(items[child], last)) break;
      items[i] = items[child];
      i = child;
    }
    items[i] = last;
    return top;
  }

  /// Heap contents in unspecified order.
  toArray(): Candidate[] {
    return this.items.slice();
  }
}

const closerFirst = (a: Candidate, b: Candidate): boolean => a.dist < b.dist;
const furtherFirst = (a: Candidate, b: Candidate): boolean => a.dist > b.dist;

/// HNSW index for approximate nearest neighbor search.
export class HnswIndex {
  private nodes: HnswNode[] = [];
  private entryPoint = -1;
  private maxLevel = 0;

  private readonly M: number;
  private readonly efConstruction: number;
  private readonly mL: number;
  readonly dimensions: number;

  constructor(
    options: {
      M?: number;
      efConstruction?: number;
      dimensions?: number;
    } = {},
  ) {
    this.M = options.M ?? 16;
    this.efConstruction = options.efConstruction ?? 200;
    this.dimensions = options.dimensions ?? EMBED_DIM;
    this.mL = 1 / Math.log(this.M);
  }

  get size(): number {
    return this.nodes.length;
  }

  /// Generate a random level for a new node.
  private randomLevel(): number {
    let level = 0;
    while (Math.random() < 1 / Math.log2(this.M) && level < 32) {
      level++;
    }
    return level;
  }

  /// Search for nearest neighbors at a specific level.
  private searchLayer(
    query: Float32Array,
    entryId: number,
    ef: number,
    level: number,
  ): Candidate[] {
    const visited = new Set<number>([entryId]);
    const entry: Candidate = {
      id: entryId,
      dist: cosineDistance(query, this.nodes[entryId].vector),
    };
    // Closest unexpanded node on top.
    const candidates = new CandidateHeap(closerFirst);
    candidates.push(entry);
    // Furthest kept result on top, so the eviction bound is O(1) to read.
    const results = new CandidateHeap(furtherFirst);
    results.push(entry);

    while (candidates.size > 0) {
      const current = candidates.pop();
      if (current.dist > results.peek().dist && results.size >= ef) break;

      // Explore neighbors
      const neighbors = this.nodes[current.id].neighbors[level] ?? [];
      for (const nId of neighbors) {
        if (visited.has(nId)) continue;
        visited.add(nId);

        const dist = cosineDistance(query, this.nodes[nId].vector);

        if (results.size < ef || dist < results.peek().dist) {
          const candidate: Candidate = { id: nId, dist };
          candidates.push(candidate);
          results.push(candidate);

          // Keep only top ef results
          if (results.size > ef) results.pop();
        }
      }
    }

    return results.toArray().sort((a, b) => a.dist - b.dist);
  }

  /// Select neighbors using simple heuristic (closest M).
  private selectNeighbors(candidates: Array<{ id: number; dist: number }>, M: number): number[] {
    return candidates
      .sort((a, b) => a.dist - b.dist)
      .slice(0, M)
      .map((c) => c.id);
  }

  /// Insert a vector into the index.
  /// IDs are contiguous corpus positions: 0, 1, ... . Reject gaps/replacements
  /// before mutation, because graph edges and the binary format use these positions.
  insert(id: number, vector: Float32Array): void {
    if (id !== this.nodes.length) {
      throw new RangeError(`Expected next vector ID ${this.nodes.length}, got ${id}`);
    }
    if (vector.length !== this.dimensions || !vector.every(Number.isFinite)) {
      throw new RangeError(`Expected ${this.dimensions} finite vector components`);
    }
    const level = this.randomLevel();
    const node: HnswNode = {
      id,
      vector,
      neighbors: Array.from({ length: level + 1 }, () => []),
    };

    if (this.nodes.length === 0) {
      this.nodes.push(node);
      this.entryPoint = 0;
      this.maxLevel = level;
      return;
    }

    this.nodes.push(node);

    let currentNode = this.entryPoint;

    // Traverse from top level down to level+1 (greedy)
    for (let l = this.maxLevel; l > level; l--) {
      const result = this.searchLayer(vector, currentNode, 1, l);
      currentNode = result[0].id;
    }

    // Insert at levels level down to 0
    for (let l = Math.min(level, this.maxLevel); l >= 0; l--) {
      const candidates = this.searchLayer(vector, currentNode, this.efConstruction, l);
      const neighbors = this.selectNeighbors(candidates, this.M);

      node.neighbors[l] = neighbors;

      // Add bidirectional connections
      for (const nId of neighbors) {
        const neighbor = this.nodes[nId];
        if (!neighbor.neighbors[l]) neighbor.neighbors[l] = [];
        neighbor.neighbors[l].push(id);

        // Trim if over capacity
        if (neighbor.neighbors[l].length > this.M * 2) {
          const nCandidates = neighbor.neighbors[l].map((n) => ({
            id: n,
            dist: cosineDistance(neighbor.vector, this.nodes[n].vector),
          }));
          neighbor.neighbors[l] = this.selectNeighbors(nCandidates, this.M);
        }
      }

      if (candidates.length > 0) currentNode = candidates[0].id;
    }

    if (level > this.maxLevel) {
      this.maxLevel = level;
      this.entryPoint = id;
    }
  }

  /// Search for k nearest neighbors.
  search(
    query: Float32Array,
    k: number,
    efSearch?: number,
  ): Array<{ id: number; distance: number }> {
    if (this.nodes.length === 0 || this.entryPoint < 0) return [];

    const ef = efSearch ?? Math.max(k * 2, 50);
    let currentNode = this.entryPoint;

    // Greedy search from top to level 1
    for (let l = this.maxLevel; l > 0; l--) {
      const result = this.searchLayer(query, currentNode, 1, l);
      currentNode = result[0].id;
    }

    // Search at level 0 with ef candidates
    const results = this.searchLayer(query, currentNode, ef, 0);

    return results.slice(0, k).map((r) => ({
      id: r.id,
      distance: r.dist,
    }));
  }

  /// Serialize the index to a binary buffer.
  serialize(): Buffer {
    const headerSize = 24; // magic(4) + version(4) + count(4) + dim(4) + M(4) + levels(4)
    const embSize = this.nodes.length * this.dimensions * 4;

    // Calculate graph size
    let graphSize = 0;
    for (const node of this.nodes) {
      for (const level of node.neighbors) {
        graphSize += 2 + level.length * 4; // count(u16) + neighbors(u32[])
      }
      graphSize += 4; // level count (u32)
    }

    const buf = Buffer.alloc(headerSize + embSize + graphSize);
    let offset = 0;

    // Header
    buf.write('UZDX', offset);
    offset += 4;
    buf.writeUInt32LE(1, offset);
    offset += 4; // version
    buf.writeUInt32LE(this.nodes.length, offset);
    offset += 4;
    buf.writeUInt32LE(this.dimensions, offset);
    offset += 4;
    buf.writeUInt32LE(this.M, offset);
    offset += 4;
    buf.writeUInt32LE(this.maxLevel, offset);
    offset += 4;

    // Embeddings
    for (const node of this.nodes) {
      for (let i = 0; i < this.dimensions; i++) {
        buf.writeFloatLE(node.vector[i], offset);
        offset += 4;
      }
    }

    // Graph
    for (const node of this.nodes) {
      buf.writeUInt32LE(node.neighbors.length, offset);
      offset += 4;
      for (const level of node.neighbors) {
        buf.writeUInt16LE(level.length, offset);
        offset += 2;
        for (const n of level) {
          buf.writeUInt32LE(n, offset);
          offset += 4;
        }
      }
    }

    return buf.subarray(0, offset);
  }

  /// Deserialize an index from a binary buffer.
  static deserialize(buf: Buffer): HnswIndex {
    let offset = 0;

    const magic = buf.toString('utf-8', offset, offset + 4);
    offset += 4;
    if (magic !== 'UZDX') throw new Error(`Invalid magic: ${magic}`);

    const version = buf.readUInt32LE(offset);
    offset += 4;
    if (version !== 1) throw new Error(`Unsupported version: ${version}`);

    const count = buf.readUInt32LE(offset);
    offset += 4;
    const dim = buf.readUInt32LE(offset);
    offset += 4;
    const M = buf.readUInt32LE(offset);
    offset += 4;
    const maxLevel = buf.readUInt32LE(offset);
    offset += 4;

    const index = new HnswIndex({ M, dimensions: dim });
    index.maxLevel = maxLevel;

    // Read embeddings
    const vectors: Float32Array[] = [];
    for (let i = 0; i < count; i++) {
      const vec = new Float32Array(dim);
      for (let j = 0; j < dim; j++) {
        vec[j] = buf.readFloatLE(offset);
        offset += 4;
      }
      vectors.push(vec);
    }

    // Read graph
    for (let i = 0; i < count; i++) {
      const levelCount = buf.readUInt32LE(offset);
      offset += 4;
      const neighbors: number[][] = [];
      for (let l = 0; l < levelCount; l++) {
        const nCount = buf.readUInt16LE(offset);
        offset += 2;
        const level: number[] = [];
        for (let n = 0; n < nCount; n++) {
          level.push(buf.readUInt32LE(offset));
          offset += 4;
        }
        neighbors.push(level);
      }
      index.nodes.push({ id: i, vector: vectors[i], neighbors });
    }

    // Find entry point (highest level node)
    let bestLevel = -1;
    for (let i = 0; i < index.nodes.length; i++) {
      if (index.nodes[i].neighbors.length - 1 > bestLevel) {
        bestLevel = index.nodes[i].neighbors.length - 1;
        index.entryPoint = i;
      }
    }

    return index;
  }
}
