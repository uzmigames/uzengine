import { describe, it, expect, beforeAll } from "vitest";
import { SearchEngine } from "../../src/search/search-engine.js";
import { readFileSync, existsSync } from "node:fs";
import { resolve } from "node:path";
import type { DocsDatabase } from "../../tools/types.js";

const DOCS_PATH = resolve(import.meta.dirname ?? ".", "../../data/docs.json");

describe("Integration: real codebase search", { timeout: 60000 }, () => {
  const engine = new SearchEngine();
  let loaded = false;

  beforeAll(() => {
    // HNSW index build for 9K entities may take up to 30s
    if (!existsSync(DOCS_PATH)) {
      console.warn("docs.json not found — run 'npm run generate-docs' first");
      return;
    }
    const db: DocsDatabase = JSON.parse(readFileSync(DOCS_PATH, "utf-8"));
    const start = performance.now();
    engine.load(db.entities);
    const elapsed = (performance.now() - start).toFixed(0);
    console.log(`Loaded ${db.entities.length} entities in ${elapsed}ms`);
    loaded = true;
  });

  it("search: 'TimeManager' kind=class → TimeManager is #1", () => {
    if (!loaded) return;
    const results = engine.search("TimeManager", { kind: "class" }, 5);
    expect(results.length).toBeGreaterThan(0);
    expect(results[0].name).toBe("TimeManager");
  });

  it("search: 'fog density' → HeightFogComponent-related in results", () => {
    if (!loaded) return;
    const results = engine.search("fog density");
    const hasFog = results.some(
      (r) =>
        r.name.toLowerCase().includes("fog") ||
        r.brief.toLowerCase().includes("fog"),
    );
    expect(hasFog).toBe(true);
  });

  it("search: 'Bruneton atmosphere' → SkyAtmosphereComponent in top 5", () => {
    if (!loaded) return;
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
    if (!loaded) return;
    const results = engine.search("GGX", { kind: "shader_fn" });
    const names = results.map((r) => r.name);
    expect(names).toContain("D_GGX");
  });

  it("get_entity: TimeManager exists", () => {
    if (!loaded) return;
    const entity = engine.getEntity(
      "runtime/engine/app/time_manager.h::TimeManager",
    );
    expect(entity).toBeDefined();
    expect(entity!.kind).toBe("class");
    expect(entity!.sections["Thread Safety"]).toBeDefined();
  });

  it("get_subsystem_map: renderer has entities", () => {
    if (!loaded) return;
    const map = engine.getSubsystemMap();
    const renderer = map.find((s) => s.subsystem === "renderer");
    expect(renderer).toBeDefined();
    expect(renderer!.entityCount).toBeGreaterThan(10);
  });

  it("get_refs: Bruneton refs exist", () => {
    if (!loaded) return;
    const refs = engine.getRefs("bruneton");
    expect(refs.length).toBeGreaterThan(0);
    expect(refs[0].author).toBe("Bruneton");
  });

  it("search latency < 50ms", () => {
    if (!loaded) return;
    const start = performance.now();
    engine.search("atmosphere rendering");
    const elapsed = performance.now() - start;
    expect(elapsed).toBeLessThan(50);
  });

  it("entity count > 500", () => {
    if (!loaded) return;
    expect(engine.size).toBeGreaterThan(500);
  });
});
