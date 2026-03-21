# 10. JSON Everywhere for Scene/Asset/Config Serialization

**Status**: proposed
**Date**: 2026-03-15

## Context

Game engines need file formats for scenes, materials, and config. Binary formats are fast but opaque. Text formats are readable but slower. UE5 uses binary .umap (merge-hostile), Unity uses YAML .unity (theoretically readable, impossible to merge in practice), Godot uses .tscn (custom text format).

## Decision

Use JSON for all serialization: .uzscene (scenes), .uzmat (materials), .uzproj (project config). JSON is human-readable, diffable, schema-validatable, LLM-friendly, and has universal tooling support. This is a core design principle of UzEngine — AI-native means AI-readable formats.

## Alternatives Considered

- Binary formats (fast but opaque, merge-hostile)
- YAML (Unity-style, theoretically readable but merge nightmares)
- Custom text format (Godot .tscn, non-standard tooling)
- MessagePack (binary JSON, fast but not human-readable)
- TOML (good for config but limited for complex structures)

## Consequences

Pros: Universal tooling, LLM-compatible, version control friendly, schema-validatable. Cons: Larger file sizes than binary (mitigated by zstd compression for shipping), slower parsing (mitigated by simdjson via fastgltf). JSON parsing is not a bottleneck at scene loading scale.
