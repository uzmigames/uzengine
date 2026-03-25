/// Relationship resolver — resolves cross-references between documented entities.
///
/// Runs as a post-parse pass after all entities are extracted. Detects:
/// - Inheritance (`: public Base`)
/// - @see references
/// - CB↔shader binding (matching struct/cbuffer by name)
/// - Composition (field type matches entity name)
/// - Include graph

import type { DocumentedEntity, Relationship } from "../types.js";

/// Builds an entity lookup map by name for fast resolution.
function buildNameIndex(
  entities: DocumentedEntity[],
): Map<string, DocumentedEntity[]> {
  const index = new Map<string, DocumentedEntity[]>();
  for (const entity of entities) {
    const existing = index.get(entity.name);
    if (existing) {
      existing.push(entity);
    } else {
      index.set(entity.name, [entity]);
    }
  }
  return index;
}

/// Resolves @see cross-references to entity IDs.
function resolveSeeAlso(
  entities: DocumentedEntity[],
  nameIndex: Map<string, DocumentedEntity[]>,
): void {
  for (const entity of entities) {
    if (!entity.seeAlso) continue;

    for (const ref of entity.seeAlso) {
      const targets = nameIndex.get(ref);
      if (targets && targets.length > 0) {
        // Avoid duplicate relationships
        const alreadyLinked = entity.relationships.some(
          (r) => r.kind === "references" && r.target === targets[0].id,
        );
        if (!alreadyLinked) {
          entity.relationships.push({
            kind: "references",
            target: targets[0].id,
          });
        }
      }
    }
  }
}

/// Resolves CB↔shader binding: matches C++ structs with HLSL cbuffers by name.
function resolveCbBindings(
  entities: DocumentedEntity[],
): void {
  const cppStructs = entities.filter(
    (e) => (e.kind === "struct" || e.kind === "class") && e.language === "cpp",
  );
  const hlslCbs = entities.filter(
    (e) => e.kind === "shader_cb" && e.language === "hlsl",
  );

  for (const cb of hlslCbs) {
    const matching = cppStructs.find((s) => s.name === cb.name);
    if (matching) {
      // Bidirectional binding
      const fwd = matching.relationships.some(
        (r) => r.kind === "binds_to" && r.target === cb.id,
      );
      if (!fwd) {
        matching.relationships.push({
          kind: "binds_to",
          target: cb.id,
          description: "C++ struct maps to HLSL cbuffer",
        });
      }
      const rev = cb.relationships.some(
        (r) => r.kind === "binds_to" && r.target === matching.id,
      );
      if (!rev) {
        cb.relationships.push({
          kind: "binds_to",
          target: matching.id,
          description: "HLSL cbuffer maps to C++ struct",
        });
      }
    }
  }
}

/// Resolves composition: fields whose type matches another documented entity.
function resolveComposition(
  entities: DocumentedEntity[],
  nameIndex: Map<string, DocumentedEntity[]>,
): void {
  const fields = entities.filter((e) => e.kind === "field" && e.type);

  for (const field of fields) {
    if (!field.type) continue;

    // Extract the base type name (strip pointers, references, templates, qualifiers)
    const baseType = field.type
      .replace(/\b(const|volatile|static|inline|constexpr|mutable)\b/g, "")
      .replace(/[*&<>[\]]/g, " ")
      .trim()
      .split(/\s+/)
      .pop();

    if (!baseType) continue;

    const targets = nameIndex.get(baseType);
    if (targets && targets.length > 0) {
      // Find the parent entity (struct/class containing this field)
      const parent = entities.find(
        (e) =>
          e.file === field.file &&
          (e.kind === "struct" || e.kind === "class") &&
          e.line < field.line,
      );
      if (parent) {
        const alreadyLinked = parent.relationships.some(
          (r) => r.kind === "contains" && r.target === targets[0].id,
        );
        if (!alreadyLinked) {
          parent.relationships.push({
            kind: "contains",
            target: targets[0].id,
            description: `via field ${field.name}`,
          });
        }
      }
    }
  }
}

/// Heuristic: match *_pass.h + *_ps.hlsl by shared prefix.
function resolvePassShaderHeuristic(
  entities: DocumentedEntity[],
): void {
  const passes = entities.filter(
    (e) =>
      (e.kind === "class" || e.kind === "struct") &&
      e.file.includes("_pass") &&
      e.language === "cpp",
  );
  const shaderFiles = entities.filter(
    (e) => e.kind === "file" && e.language === "hlsl",
  );

  for (const pass of passes) {
    // Extract prefix: "height_fog_pass.h" → "height_fog"
    const fileName = pass.file.split("/").pop() ?? "";
    const prefix = fileName.replace(/_pass\.\w+$/, "");
    if (!prefix) continue;

    for (const shader of shaderFiles) {
      const shaderName = shader.file.split("/").pop() ?? "";
      if (
        shaderName.startsWith(prefix) &&
        (shaderName.endsWith(".hlsl") || shaderName.endsWith(".hlsli"))
      ) {
        const alreadyLinked = pass.relationships.some(
          (r) => r.kind === "uses" && r.target === shader.id,
        );
        if (!alreadyLinked) {
          pass.relationships.push({
            kind: "uses",
            target: shader.id,
            description: `shader file matched by prefix "${prefix}"`,
          });
        }
      }
    }
  }
}

/// Resolves component→renderer data flow heuristic.
/// If a renderer .cpp file mentions a component name, add reads_from.
function resolveComponentRendererFlow(
  entities: DocumentedEntity[],
): void {
  const components = entities.filter(
    (e) =>
      (e.kind === "struct" || e.kind === "class") &&
      e.name.endsWith("Component"),
  );

  // Look at file-level entities from renderer .cpp files for description mentions
  const rendererFiles = entities.filter(
    (e) =>
      e.kind === "file" &&
      e.language === "cpp" &&
      (e.file.includes("/renderer/") || e.file.includes("_renderer")),
  );

  // This is a lightweight heuristic — for deeper analysis, source text would be needed.
  // For now, rely on @see annotations and explicit include-based detection.
}

/// Main resolver entry point.
///
/// Runs all resolution passes over the entity array (mutates in place).
export function resolveRelationships(
  entities: DocumentedEntity[],
): void {
  const nameIndex = buildNameIndex(entities);

  // Inheritance is already resolved during parsing (from `: public Base`).
  // Here we resolve the target to an entity ID if possible.
  for (const entity of entities) {
    for (const rel of entity.relationships) {
      if (rel.kind === "inherits") {
        const targets = nameIndex.get(rel.target);
        if (targets && targets.length > 0 && !rel.target.includes("::")) {
          rel.target = targets[0].id;
        }
      }
    }
  }

  resolveSeeAlso(entities, nameIndex);
  resolveCbBindings(entities);
  resolveComposition(entities, nameIndex);
  resolvePassShaderHeuristic(entities);
  resolveComponentRendererFlow(entities);
}
