import { describe, it, expect } from "vitest";
import { SearchEngine } from "../../src/search/search-engine.js";
import { reciprocalRankFusion } from "../../src/search/fusion.js";
import { fnv1a, embed, buildIdfMap, EMBED_DIM, cosineSimilarity } from "../../src/search/embeddings.js";
import { Bm25Index } from "../../src/search/bm25.js";
import type { DocumentedEntity } from "../../tools/types.js";

function makeEntity(
  overrides: Partial<DocumentedEntity> & { id: string; name: string },
): DocumentedEntity {
  return {
    kind: "struct",
    file: "test.h",
    line: 1,
    brief: "",
    sections: {},
    tags: [],
    subsystem: "test",
    language: "cpp",
    relationships: [],
    ...overrides,
  };
}

describe("fnv1a", () => {
  it("produces consistent hashes", () => {
    expect(fnv1a("test")).toBe(fnv1a("test"));
    expect(fnv1a("foo")).not.toBe(fnv1a("bar"));
  });

  it("returns unsigned 32-bit integers", () => {
    expect(fnv1a("test")).toBeGreaterThanOrEqual(0);
    expect(fnv1a("test")).toBeLessThan(2 ** 32);
  });
});

describe("embed", () => {
  it("produces 256-dim vectors", () => {
    const idf = new Map([["fog", 2.0], ["density", 1.5]]);
    const vec = embed(["fog", "density"], idf);
    expect(vec.length).toBe(EMBED_DIM);
  });

  it("produces L2-normalized vectors", () => {
    const idf = new Map([["fog", 2.0], ["density", 1.5]]);
    const vec = embed(["fog", "density"], idf);
    let norm = 0;
    for (let i = 0; i < vec.length; i++) norm += vec[i] * vec[i];
    expect(Math.abs(Math.sqrt(norm) - 1.0)).toBeLessThan(0.001);
  });

  it("similar tokens produce similar vectors", () => {
    const idf = new Map([
      ["fog", 2.0], ["density", 1.5], ["atmosphere", 1.8], ["height", 1.3],
    ]);
    const a = embed(["fog", "density", "height"], idf);
    const b = embed(["fog", "density", "atmosphere"], idf);
    const c = embed(["audio", "mixer", "bus"], new Map([
      ["audio", 2.0], ["mixer", 1.5], ["bus", 1.3],
    ]));

    const simAB = cosineSimilarity(a, b);
    const simAC = cosineSimilarity(a, c);
    expect(simAB).toBeGreaterThan(simAC);
  });
});

describe("Bm25Index", () => {
  it("ranks exact term match highest", () => {
    const bm25 = new Bm25Index();
    bm25.build([
      ["fog", "density", "height", "component"],
      ["audio", "mixer", "bus", "volume"],
      ["fog", "rendering", "atmosphere"],
    ]);

    const results = bm25.search(["fog", "density"], 10);
    expect(results[0].doc).toBe(0); // Has both fog + density
  });
});

describe("reciprocalRankFusion", () => {
  it("boosts items in both lists", () => {
    const listA = [
      { id: 1, rank: 1 },
      { id: 2, rank: 2 },
      { id: 3, rank: 3 },
    ];
    const listB = [
      { id: 2, rank: 1 },
      { id: 4, rank: 2 },
      { id: 1, rank: 3 },
    ];

    const fused = reciprocalRankFusion(listA, listB);

    // Items 1 and 2 appear in both → highest scores
    const ids = fused.slice(0, 2).map((r) => r.id);
    expect(ids).toContain(1);
    expect(ids).toContain(2);
  });

  it("computes correct RRF scores", () => {
    const fused = reciprocalRankFusion(
      [{ id: 0, rank: 3 }],
      [{ id: 0, rank: 5 }],
    );
    const expected = 1 / (60 + 3) + 1 / (60 + 5);
    expect(Math.abs(fused[0].score - expected)).toBeLessThan(0.0001);
  });
});

describe("SearchEngine", () => {
  it("performs hybrid search", () => {
    const engine = new SearchEngine();
    engine.load([
      makeEntity({
        id: "fog.h::HeightFogComponent",
        name: "HeightFogComponent",
        brief: "Height-based exponential fog parameters",
        subsystem: "environment",
        tags: ["component"],
      }),
      makeEntity({
        id: "time.h::TimeManager",
        name: "TimeManager",
        brief: "Tracks frame timing and delta time",
        subsystem: "application",
        tags: ["subsystem"],
      }),
      makeEntity({
        id: "audio.h::AudioMixer",
        name: "AudioMixer",
        brief: "Mixes audio buses and controls volume",
        subsystem: "audio",
        tags: ["subsystem"],
      }),
    ]);

    const results = engine.search("fog height");
    expect(results.length).toBeGreaterThan(0);
    expect(results[0].name).toBe("HeightFogComponent");
  });

  it("filters by kind", () => {
    const engine = new SearchEngine();
    engine.load([
      makeEntity({
        id: "a.h::Foo",
        name: "Foo",
        kind: "class",
        brief: "A class",
      }),
      makeEntity({
        id: "b.h::Bar",
        name: "Bar",
        kind: "struct",
        brief: "A struct",
      }),
    ]);

    const results = engine.search("class struct", { kind: "struct" });
    expect(results.every((r) => r.kind === "struct")).toBe(true);
  });

  it("gets entity by ID", () => {
    const engine = new SearchEngine();
    engine.load([
      makeEntity({ id: "a.h::Foo", name: "Foo", brief: "Test" }),
    ]);

    expect(engine.getEntity("a.h::Foo")?.name).toBe("Foo");
    expect(engine.getEntity("nonexistent")).toBeUndefined();
  });

  it("gets subsystem map", () => {
    const engine = new SearchEngine();
    engine.load([
      makeEntity({ id: "a.h::A", name: "A", subsystem: "renderer" }),
      makeEntity({ id: "b.h::B", name: "B", subsystem: "renderer" }),
      makeEntity({ id: "c.h::C", name: "C", subsystem: "audio" }),
    ]);

    const map = engine.getSubsystemMap();
    expect(map[0].subsystem).toBe("renderer");
    expect(map[0].entityCount).toBe(2);
  });

  it("gets refs filtered by author", () => {
    const engine = new SearchEngine();
    engine.load([
      makeEntity({
        id: "a.h::A",
        name: "A",
        refs: [{ author: "Bruneton", year: "2008", title: "Atmospheric Scattering" }],
      }),
      makeEntity({
        id: "b.h::B",
        name: "B",
        refs: [{ author: "Karis", year: "2013", title: "Real Shading in UE4" }],
      }),
    ]);

    const refs = engine.getRefs("bruneton");
    expect(refs).toHaveLength(1);
    expect(refs[0].author).toBe("Bruneton");
  });
});
