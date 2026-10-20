import { describe, it, expect, beforeAll } from "vitest";
import { SearchEngine } from "../../src/search/search-engine.js";
import { readFileSync, existsSync } from "node:fs";
import { resolve } from "node:path";
import type { DocsDatabase } from "../../tools/types.js";

const DOCS_PATH = process.env.UZENGINE_TEST_DOCS_PATH
  ? resolve(process.env.UZENGINE_TEST_DOCS_PATH)
  : resolve(import.meta.dirname ?? ".", "../../data/docs.json");

// The HNSW build over ~15K entities took ~46s locally under v8 coverage;
// budget for a CI runner at least 2x slower than that.
const INDEX_BUILD_TIMEOUT_MS = 180_000;

// docs.json is a generated, git-ignored build artifact (run `npm run generate-docs`).
// Skip this integration suite when the fixture is absent (fresh clone / CI before the
// docs step / pre-push gate) instead of hard-failing; it still runs fully when present.
describe.skipIf(!existsSync(DOCS_PATH))("Integration: real codebase search", { timeout: 60000 }, () => {
  const engine = new SearchEngine();

  beforeAll(() => {
    const db: DocsDatabase = JSON.parse(readFileSync(DOCS_PATH, "utf-8"));
    let start = performance.now();
    engine.load(db.entities);
    console.log(`Loaded ${db.entities.length} entities in ${(performance.now() - start).toFixed(0)}ms`);

    // SearchEngine builds its HNSW index lazily on the first query. Pay that
    // cost here so no individual test absorbs it.
    start = performance.now();
    engine.search("warm up vector index");
    console.log(`Built HNSW index in ${(performance.now() - start).toFixed(0)}ms`);
  }, INDEX_BUILD_TIMEOUT_MS);

  it("search: 'TimeManager' kind=class → TimeManager is #1", () => {
    const results = engine.search("TimeManager", { kind: "class" }, 5);
    expect(results.length).toBeGreaterThan(0);
    expect(results[0].name).toBe("TimeManager");
  });

  it("search: 'fog density' → HeightFogComponent-related in results", () => {
    const results = engine.search("fog density");
    const hasFog = results.some(
      (r) =>
        r.name.toLowerCase().includes("fog") ||
        r.brief.toLowerCase().includes("fog"),
    );
    expect(hasFog).toBe(true);
  });

  it("search: 'Bruneton atmosphere' → SkyAtmosphereComponent in top 5", () => {
    const results = engine.search("Bruneton atmosphere", undefined, 5);
    const hasAtmo = results.some(
      (r) =>
        r.name.includes("Atmosphere") ||
        r.name.includes("atmosphere") ||
        r.brief.includes("atmosphere"),
    );
    expect(hasAtmo).toBe(true);
  });

  it("search: 'GGX' kind=shader_fn → D_GGX in results", () => {
    const results = engine.search("GGX", { kind: "shader_fn" });
    const names = results.map((r) => r.name);
    expect(names).toContain("D_GGX");
  });

  it("get_entity: TimeManager exists", () => {
    const entity = engine.getEntity(
      "runtime/engine/app/time_manager.h::TimeManager",
    );
    expect(entity).toBeDefined();
    expect(entity!.kind).toBe("class");
    expect(entity!.sections["Thread Safety"]).toBeDefined();
  });

  it("get_subsystem_map: renderer has entities", () => {
    const map = engine.getSubsystemMap();
    const renderer = map.find((s) => s.subsystem === "renderer");
    expect(renderer).toBeDefined();
    expect(renderer!.entityCount).toBeGreaterThan(10);
  });

  it("get_refs: Bruneton refs exist", () => {
    const refs = engine.getRefs("bruneton");
    expect(refs.length).toBeGreaterThan(0);
    expect(refs[0].author).toBe("Bruneton");
  });

  it("search latency < 50ms", () => {
    const start = performance.now();
    engine.search("atmosphere rendering");
    const elapsed = performance.now() - start;
    expect(elapsed).toBeLessThan(50);
  });

  it("entity count > 500", () => {
    expect(engine.size).toBeGreaterThan(500);
  });
});
