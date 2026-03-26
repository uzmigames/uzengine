/// Auto-tag generator for documented entities.
///
/// Assigns classification tags based on file path, entity kind,
/// naming conventions, and content patterns.

import type { DocumentedEntity } from "../types.js";

/// Tag rule: a predicate that, when true, adds the tag to the entity.
interface TagRule {
  tag: string;
  test: (entity: DocumentedEntity) => boolean;
}

const TAG_RULES: ReadonlyArray<TagRule> = [
  // By file path patterns
  {
    tag: "component",
    test: (e) =>
      e.file.includes("/ecs/") || e.name.endsWith("Component"),
  },
  {
    tag: "pass",
    test: (e) =>
      e.file.includes("/passes/") || e.name.endsWith("Pass"),
  },
  {
    tag: "rhi",
    test: (e) => e.file.includes("/rhi/"),
  },
  {
    tag: "shader",
    test: (e) => e.language === "hlsl",
  },
  {
    tag: "cbuffer",
    test: (e) => e.kind === "shader_cb",
  },
  {
    tag: "subsystem",
    test: (e) =>
      e.name.endsWith("Subsystem") ||
      e.name === "ISubsystem" ||
      (e.sections["Design"] ?? "").includes("subsystem"),
  },

  // By entity visibility
  {
    tag: "public-api",
    test: (e) =>
      e.language === "cpp" &&
      e.file.endsWith(".h") &&
      e.brief.length > 0,
  },
  {
    tag: "internal",
    test: (e) =>
      e.language === "cpp" && e.file.endsWith(".cpp"),
  },

  // By content domain
  {
    tag: "rendering",
    test: (e) =>
      e.file.includes("/renderer/") ||
      e.file.includes("/shaders/") ||
      e.file.includes("/atmosphere/"),
  },
  {
    tag: "editor",
    test: (e) => e.file.includes("/editor/"),
  },
  {
    tag: "math",
    test: (e) => e.file.includes("/math/"),
  },
  {
    tag: "platform",
    test: (e) => e.file.includes("/platform/"),
  },

  // By metadata presence
  {
    tag: "reference",
    test: (e) => (e.refs?.length ?? 0) > 0,
  },
  {
    tag: "has-examples",
    test: (e) => (e.examples?.length ?? 0) > 0,
  },
  {
    tag: "thread-safety-documented",
    test: (e) => "Thread Safety" in e.sections,
  },

  // By kind
  {
    tag: "enum",
    test: (e) => e.kind === "enum",
  },
  {
    tag: "macro",
    test: (e) => e.kind === "macro",
  },
];

/// Generates auto-tags for a documented entity.
///
/// Runs all tag rules against the entity and returns matching tags.
/// Tags are deduplicated and sorted alphabetically.
///
/// # Arguments
/// * `entity` — the entity to tag (must have file, kind, name, language set)
///
/// # Returns
/// Array of tag strings, sorted alphabetically.
export function generateTags(entity: DocumentedEntity): string[] {
  const tags = new Set<string>();

  for (const rule of TAG_RULES) {
    if (rule.test(entity)) {
      tags.add(rule.tag);
    }
  }

  return [...tags].sort();
}
