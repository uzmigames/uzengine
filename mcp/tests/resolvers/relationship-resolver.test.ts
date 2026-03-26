import { describe, it, expect } from "vitest";
import { resolveRelationships } from "../../tools/resolvers/relationship-resolver.js";
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
    subsystem: "",
    language: "cpp",
    relationships: [],
    ...overrides,
  };
}

describe("resolveRelationships", () => {
  it("resolves @see to entity IDs", () => {
    const entities = [
      makeEntity({
        id: "a.h::Foo",
        name: "Foo",
        seeAlso: ["Bar"],
      }),
      makeEntity({ id: "b.h::Bar", name: "Bar" }),
    ];

    resolveRelationships(entities);

    expect(entities[0].relationships).toContainEqual({
      kind: "references",
      target: "b.h::Bar",
    });
  });

  it("resolves CB↔shader binding by name", () => {
    const entities = [
      makeEntity({
        id: "cb.h::PerFrameCB",
        name: "PerFrameCB",
        kind: "struct",
        language: "cpp",
      }),
      makeEntity({
        id: "shader.hlsl::PerFrameCB",
        name: "PerFrameCB",
        kind: "shader_cb",
        language: "hlsl",
      }),
    ];

    resolveRelationships(entities);

    expect(entities[0].relationships).toContainEqual(
      expect.objectContaining({
        kind: "binds_to",
        target: "shader.hlsl::PerFrameCB",
      }),
    );
    expect(entities[1].relationships).toContainEqual(
      expect.objectContaining({
        kind: "binds_to",
        target: "cb.h::PerFrameCB",
      }),
    );
  });

  it("resolves inheritance target to entity ID", () => {
    const entities = [
      makeEntity({
        id: "a.h::Child",
        name: "Child",
        kind: "class",
        relationships: [{ kind: "inherits", target: "Base" }],
      }),
      makeEntity({ id: "b.h::Base", name: "Base", kind: "class" }),
    ];

    resolveRelationships(entities);

    expect(entities[0].relationships[0].target).toBe("b.h::Base");
  });

  it("does not create duplicate relationships", () => {
    const entities = [
      makeEntity({
        id: "a.h::Foo",
        name: "Foo",
        seeAlso: ["Bar", "Bar"],
      }),
      makeEntity({ id: "b.h::Bar", name: "Bar" }),
    ];

    resolveRelationships(entities);

    const refs = entities[0].relationships.filter(
      (r) => r.kind === "references",
    );
    expect(refs).toHaveLength(1);
  });
});
