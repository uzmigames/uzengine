import { afterEach, describe, expect, it, vi } from 'vitest';
import { HnswIndex } from '../../src/search/hnsw.js';

afterEach(() => vi.restoreAllMocks());

function vectors(count: number): Float32Array[] {
  return Array.from({ length: count }, (_, i) => {
    const angle = (i * Math.PI * 2) / count;
    return new Float32Array([Math.cos(angle), Math.sin(angle)]);
  });
}

describe('HNSW persistence and nearest neighbors', () => {
  it('rejects sparse/reused IDs and malformed vectors before changing the graph', () => {
    const index = new HnswIndex({ dimensions: 2 });
    for (const id of [-1, 2, NaN, 0.5]) {
      expect(() => index.insert(id, new Float32Array([1, 0]))).toThrow('Expected next vector ID 0');
    }
    for (const vector of [
      new Float32Array([1]),
      new Float32Array([NaN, 0]),
      new Float32Array([0, Infinity]),
    ]) {
      expect(() => index.insert(0, vector)).toThrow('finite vector components');
    }
    expect(index.size).toBe(0);
    index.insert(0, new Float32Array([1, 0]));
    expect(() => index.insert(0, new Float32Array([0, 1]))).toThrow('Expected next vector ID 1');
    expect(index.size).toBe(1);
    expect(HnswIndex.deserialize(index.serialize()).search(new Float32Array([1, 0]), 1)).toEqual([
      { id: 0, distance: 0 },
    ]);
  });
  it('round-trips an empty index without inventing a nearest neighbor', () => {
    const original = new HnswIndex();
    const restored = HnswIndex.deserialize(original.serialize());
    expect(restored.size).toBe(0);
    expect(restored.dimensions).toBe(256);
    expect(restored.search(new Float32Array(256), 10)).toEqual([]);
  });

  it('preserves vector IDs, dimensions and distances after a graph round trip', () => {
    // Seed only the random level boundary, keeping graph construction/search real.
    let seed = 104729;
    vi.spyOn(Math, 'random').mockImplementation(() => {
      seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
      return seed / 2 ** 32;
    });
    const source = vectors(96);
    const original = new HnswIndex({ dimensions: 2, M: 4, efConstruction: 16 });
    source.forEach((v, i) => original.insert(i, v));
    const restored = HnswIndex.deserialize(original.serialize());
    expect(restored.size).toBe(source.length);
    expect(restored.dimensions).toBe(2);
    for (const id of [0, 17, 48, 95]) {
      const hits = restored.search(source[id], 5, 96);
      expect(hits[0].id).toBe(id);
      expect(hits[0].distance).toBeCloseTo(0, 6);
      expect(hits.map((hit) => hit.distance)).toEqual(
        [...hits.map((hit) => hit.distance)].sort((a, b) => a - b),
      );
      expect(hits).toEqual(original.search(source[id], 5, 96));
    }
    expect(restored.serialize()).toEqual(original.serialize());
  });

  it('supports insertion after restoring and limits results to available vectors', () => {
    vi.spyOn(Math, 'random').mockReturnValue(0.99);
    const original = new HnswIndex({ dimensions: 2 });
    original.insert(0, new Float32Array([1, 0]));
    const restored = HnswIndex.deserialize(original.serialize());
    restored.insert(1, new Float32Array([0, 1]));
    expect(restored.search(new Float32Array([0, 1]), 10)).toEqual([
      { id: 1, distance: 0 },
      { id: 0, distance: 1 },
    ]);
    expect(restored.search(new Float32Array([0, 1]), 0)).toEqual([]);
  });

  it('rejects unrelated files, future versions and truncated vector/graph data', () => {
    const index = new HnswIndex({ dimensions: 2 });
    index.insert(0, new Float32Array([1, 0]));
    const binary = index.serialize();
    const badMagic = Buffer.from(binary);
    badMagic.write('NOPE');
    expect(() => HnswIndex.deserialize(badMagic)).toThrow('Invalid magic');
    const future = Buffer.from(binary);
    future.writeUInt32LE(2, 4);
    expect(() => HnswIndex.deserialize(future)).toThrow('Unsupported version');
    for (const length of [0, 4, 23, 26, binary.length - 1]) {
      expect(() => HnswIndex.deserialize(binary.subarray(0, length))).toThrow();
    }
  });
});
